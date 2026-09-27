#!/bin/sh

set -eu

benchmark_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
database=${1:-postgres}
pg_config_bin=${PG_CONFIG:-pg_config}
portable_lib=${PORTABLE_LIB:?set PORTABLE_LIB to the portable arrow_decimal library}
int128_lib=${INT128_LIB:?set INT128_LIB to the native-int128 arrow_decimal library}
bindir=$($pg_config_bin --bindir)
psql_bin=$bindir/psql
pgbench_bin=$bindir/pgbench
rows=${ROWS:-1000000}
repetitions=${REPETITIONS:-5}
seconds=${SECONDS_PER_RUN:-2}
warmup_seconds=${WARMUP_SECONDS:-1}
result_dir=${RESULT_DIR:-$benchmark_dir/results/$(date +%Y%m%d-%H%M%S-int128)}
raw_dir=$result_dir/raw
samples=$result_dir/samples.csv
summary=$result_dir/summary.csv
comparisons=$result_dir/comparisons.csv

test -f "$portable_lib"
test -f "$int128_lib"
mkdir -p "$raw_dir"

{
  printf 'postgres=%s\n' "$("$pg_config_bin" --version)"
  printf 'system=%s\n' "$(uname -a)"
  printf 'rows=%s\nrepetitions=%s\nseconds_per_run=%s\nwarmup_seconds=%s\n' \
    "$rows" "$repetitions" "$seconds" "$warmup_seconds"
  printf 'portable_lib=%s\nint128_lib=%s\n' "$portable_lib" "$int128_lib"
} >"$result_dir/environment.txt"

PGOPTIONS="${PGOPTIONS:+$PGOPTIONS }-c jit=off -c max_parallel_workers_per_gather=0"
export PGOPTIONS

"$psql_bin" -X -v ON_ERROR_STOP=1 -v rows="$rows" \
  -v portable_lib="$portable_lib" -v int128_lib="$int128_lib" \
  -d "$database" -f "$benchmark_dir/int128_setup.sql" \
  >"$result_dir/setup.out" 2>&1

printf '%s\n' 'implementation,workload,run,tps,latency_ms' >"$samples"

run_pgbench()
{
  mode=$1
  implementation=$2
  workload=$3
  run=$4
  aggregate=benchmark_sum_$implementation
  operator=benchmark_decimal128_${workload}_$implementation
  script=$benchmark_dir/int128_$workload.sql

  case "$workload" in
    compare)
      operator=benchmark_decimal128_cmp_$implementation
      ;;
    sum)
      operator=unused
      ;;
    add_rescale)
      operator=benchmark_decimal128_add_$implementation
      ;;
    multiply)
      operator=benchmark_decimal128_mul_$implementation
      ;;
  esac

  if test "$mode" = warmup; then
    "$pgbench_bin" -n -M simple -c 1 -j 1 -T "$warmup_seconds" \
      -D aggregate="$aggregate" -D operator="$operator" \
      -f "$script" "$database" >/dev/null 2>&1
    return
  fi

  raw=$raw_dir/${implementation}-${workload}-run${run}.txt
  "$pgbench_bin" -n -M simple -c 1 -j 1 -T "$seconds" \
    -D aggregate="$aggregate" -D operator="$operator" \
    -f "$script" "$database" >"$raw" 2>&1
  tps=$(awk '/^tps = / && /without initial connection time/ { print $3; exit }' "$raw")
  latency=$(awk '/^latency average = / { print $4; exit }' "$raw")
  test -n "$tps"
  test -n "$latency"
  printf '%s,%s,%s,%s,%s\n' \
    "$implementation" "$workload" "$run" "$tps" "$latency" >>"$samples"
}

for workload in compare sum add add_rescale multiply; do
  run_pgbench warmup portable "$workload" 0
  run_pgbench warmup int128 "$workload" 0

  run=1
  while test "$run" -le "$repetitions"; do
    if test $((run % 2)) -eq 1; then
      run_pgbench measure portable "$workload" "$run"
      run_pgbench measure int128 "$workload" "$run"
    else
      run_pgbench measure int128 "$workload" "$run"
      run_pgbench measure portable "$workload" "$run"
    fi
    run=$((run + 1))
  done
done

printf '%s\n' 'implementation,workload,n,mean_tps,median_tps,cv_percent' >"$summary"

summarize_case()
{
  implementation=$1
  workload=$2
  values=$result_dir/$implementation-$workload.tps

  awk -F, -v impl="$implementation" -v work="$workload" \
    'NR > 1 && $1 == impl && $2 == work { print $4 }' "$samples" \
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
  printf '%s,%s,%s\n' \
    "$implementation" "$workload" "$stats" >>"$summary"
}

for workload in compare sum add add_rescale multiply; do
  summarize_case portable "$workload"
  summarize_case int128 "$workload"
done

printf '%s\n' 'workload,portable_median_tps,int128_median_tps,int128_over_portable' \
  >"$comparisons"
for workload in compare sum add add_rescale multiply; do
  portable_median=$(awk -F, -v work="$workload" \
    '$1 == "portable" && $2 == work { print $5 }' "$summary")
  int128_median=$(awk -F, -v work="$workload" \
    '$1 == "int128" && $2 == work { print $5 }' "$summary")
  ratio=$(awk -v native="$int128_median" -v portable="$portable_median" \
    'BEGIN { printf "%.4f", native / portable }')
  printf '%s,%s,%s,%s\n' \
    "$workload" "$portable_median" "$int128_median" "$ratio" \
    >>"$comparisons"
done

printf 'Results: %s\n' "$result_dir"
