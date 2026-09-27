# Performance experiment

This experiment compares the built-in `numeric(p,s)` implementation with
the fixed-width `decimal64(p,s)` and `decimal128(p,s)` executor paths.  It
does not claim to measure a columnar table AM: heap stores the self-describing
16-byte and 24-byte PostgreSQL Datums, while a table AM uses the exported
8-byte and 16-byte Arrow codecs.

The falsifiable hypothesis is that same-scale filtering, addition, and
multiplication have at least 15% higher median single-client throughput with
the fixed-width types.  A result at or below 0.95x is treated as a regression;
0.95x through 1.15x is neutral.  Interpret a comparison only when both sides'
coefficient of variation is at most 5%.

The setup creates four unlogged tables with identical centered, uniformly
spaced values.  The default scale is 2,000,000 rows.  Correctness checks compare
sums and products after conversion to `numeric`.  Each workload gets one
discarded warmup and five measured three-second runs, with baseline/fixed order
alternated between repetitions.  JIT and parallel query are disabled.  This is
a warm-cache, one-client CPU experiment using the simple query protocol (so
that table and type names can be substituted into one common script); rerun
with different row counts and concurrency before drawing workload-wide
conclusions.

Build and install PostgreSQL and the extension, start a server, then run:

```sh
PG_CONFIG=/path/to/pg_config \
  contrib/arrow_decimal/benchmark/run.sh postgres
```

The output directory contains raw `pgbench` reports, `samples.csv`,
`summary.csv`, `comparisons.csv`, and the setup/correctness output.  Override
`ROWS`, `REPETITIONS`, `SECONDS_PER_RUN`, `WARMUP_SECONDS`, or `RESULT_DIR` to
change the matrix without editing the scripts.

## Native int128 experiment

`run_int128.sh` isolates the Decimal128 implementation change.  Build the same
source twice, once with `-DARROW_DECIMAL_FORCE_PORTABLE` and once normally, and
pass the two shared-library paths as `PORTABLE_LIB` and `INT128_LIB`.  Both
libraries are loaded into one PostgreSQL instance and invoked alternately over
the same one-million-row table.  This avoids comparing different server
instances or data sets.

The workloads cover same-scale comparison, the `sum` transition, same-scale
addition, addition that must rescale one argument, and multiplication.  The
setup checks that both implementations return identical results before timing.
The primary hypothesis is at least 10% higher median throughput for addition,
rescaling addition, and multiplication.  A ratio below 0.95 is a regression;
0.95 through 1.10 is neutral.  Comparison and `sum` are controls and must not
regress by more than 5%.  Interpret a result only when both coefficients of
variation are at most 5%.
For example:

```sh
PORTABLE_LIB=/path/to/portable/arrow_decimal.so \
INT128_LIB=/path/to/native/arrow_decimal.so \
PG_CONFIG=/path/to/pg_config \
  contrib/arrow_decimal/benchmark/run_int128.sh postgres
```
