#!/bin/sh

set -eu

benchmark_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
database=${1:-postgres}
pg_config_bin=${PG_CONFIG:-pg_config}
bindir=$($pg_config_bin --bindir)
psql_bin=$bindir/psql
pgbench_bin=$bindir/pgbench
rows=${ROWS:-2000000}
repetitions=${REPETITIONS:-5}
seconds=${SECONDS_PER_RUN:-3}
warmup_seconds=${WARMUP_SECONDS:-1}
result_dir=${RESULT_DIR:-$benchmark_dir/results/$(date +%Y%m%d-%H%M%S)}
raw_dir=$result_dir/raw
samples=$result_dir/samples.csv
summary=$result_dir/summary.csv
comparisons=$result_dir/comparisons.csv

mkdir -p "$raw_dir"

PGOPTIONS="${PGOPTIONS:+$PGOPTIONS }-c jit=off -c max_parallel_workers_per_gather=0"
export PGOPTIONS

"$psql_bin" -X -v ON_ERROR_STOP=1 -v rows="$rows" \
  -d "$database" -f "$benchmark_dir/setup.sql" \
  >"$result_dir/setup.out" 2>&1

printf '%s\n' 'case_name,family,width,workload,run,tps,latency_ms' >"$samples"

run_pgbench()
{
  mode=$1
  case_name=$2
  family=$3
  width=$4
  workload=$5
  run=$6
  table_name=$7
  type_name=$8
  low=$9
  shift 9
  high=$1
  script=$benchmark_dir/$workload.sql

  if test "$mode" = warmup; then
    "$pgbench_bin" -n -M simple -c 1 -j 1 -T "$warmup_seconds" \
      -D table="$table_name" -D type="$type_name" \
      -D low="$low" -D high="$high" \
      -f "$script" "$database" >/dev/null 2>&1
    return
  fi

  raw=$raw_dir/${case_name}-run${run}.txt
  "$pgbench_bin" -n -M simple -c 1 -j 1 -T "$seconds" \
    -D table="$table_name" -D type="$type_name" \
    -D low="$low" -D high="$high" \
    -f "$script" "$database" >"$raw" 2>&1
  tps=$(awk '/^tps = / && /without initial connection time/ { print $3; exit }' "$raw")
  latency=$(awk '/^latency average = / { print $4; exit }' "$raw")
  test -n "$tps"
  test -n "$latency"
  printf '%s,%s,%s,%s,%s,%s,%s\n' \
    "$case_name" "$family" "$width" "$workload" "$run" "$tps" "$latency" \
    >>"$samples"
}

run_pair()
{
  width=$1
  workload=$2
  numeric_table=$3
  numeric_type=$4
  decimal_table=$5
  decimal_type=$6
  low=$7
  high=$8
  numeric_case=numeric${width}_${workload}
  decimal_case=decimal${width}_${workload}

  run_pgbench warmup "$numeric_case" numeric "$width" "$workload" 0 \
    "$numeric_table" "$numeric_type" "$low" "$high"
  run_pgbench warmup "$decimal_case" fixed "$width" "$workload" 0 \
    "$decimal_table" "$decimal_type" "$low" "$high"

  run=1
  while test "$run" -le "$repetitions"; do
    if test $((run % 2)) -eq 1; then
      run_pgbench measure "$numeric_case" numeric "$width" "$workload" "$run" \
        "$numeric_table" "$numeric_type" "$low" "$high"
      run_pgbench measure "$decimal_case" fixed "$width" "$workload" "$run" \
        "$decimal_table" "$decimal_type" "$low" "$high"
    else
      run_pgbench measure "$decimal_case" fixed "$width" "$workload" "$run" \
        "$decimal_table" "$decimal_type" "$low" "$high"
      run_pgbench measure "$numeric_case" numeric "$width" "$workload" "$run" \
        "$numeric_table" "$numeric_type" "$low" "$high"
    fi
    run=$((run + 1))
  done
}

for workload in filter add multiply; do
  run_pair 64 "$workload" ad_numeric64 'numeric(18,2)' \
    ad_decimal64 'decimal64(18,2)' -2500 2500
  run_pair 128 "$workload" ad_numeric128 'numeric(38,4)' \
    ad_decimal128 'decimal128(38,4)' -25 25
done

printf '%s\n' 'case_name,family,width,workload,n,mean_tps,median_tps,cv_percent' \
  >"$summary"

summarize_case()
{
  case_name=$1
  family=$2
  width=$3
  workload=$4
  values=$result_dir/$case_name.tps

  awk -F, -v key="$case_name" 'NR > 1 && $1 == key { print $6 }' "$samples" \
    | sort -n >"$values"
  stats=$(awk '
    { value[NR] = $1; sum += $1; sumsq += $1 * $1 }
    END {
      mean = sum / NR
      if (NR % 2) median = value[(NR + 1) / 2]
      else median = (value[NR / 2] + value[NR / 2 + 1]) / 2
      variance = (sumsq / NR) - (mean * mean)
      if (variance < 0) variance = 0
      cv = mean == 0 ? 0 : 100 * sqrt(variance) / mean
      printf "%d,%.6f,%.6f,%.3f", NR, mean, median, cv
    }' "$values")
  printf '%s,%s,%s,%s,%s\n' \
    "$case_name" "$family" "$width" "$workload" "$stats" >>"$summary"
}

for workload in filter add multiply; do
  summarize_case "numeric64_$workload" numeric 64 "$workload"
  summarize_case "decimal64_$workload" fixed 64 "$workload"
  summarize_case "numeric128_$workload" numeric 128 "$workload"
  summarize_case "decimal128_$workload" fixed 128 "$workload"
done

printf '%s\n' 'width,workload,numeric_median_tps,fixed_median_tps,fixed_over_numeric' \
  >"$comparisons"
for workload in filter add multiply; do
  for width in 64 128; do
    numeric_median=$(awk -F, -v key="numeric${width}_$workload" \
      '$1 == key { print $7 }' "$summary")
    fixed_median=$(awk -F, -v key="decimal${width}_$workload" \
      '$1 == key { print $7 }' "$summary")
    ratio=$(awk -v fixed="$fixed_median" -v numeric="$numeric_median" \
      'BEGIN { printf "%.4f", fixed / numeric }')
    printf '%s,%s,%s,%s,%s\n' \
      "$width" "$workload" "$numeric_median" "$fixed_median" "$ratio" \
      >>"$comparisons"
  done
done

printf 'Results: %s\n' "$result_dir"
