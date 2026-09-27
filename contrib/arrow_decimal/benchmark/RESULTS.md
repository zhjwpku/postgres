# Local result: 2026-09-10 Darwin arm64

The checked-in sample used PostgreSQL `20devel` at base commit `195e8e427c`,
a Meson `debugoptimized` (`-O2`) build with assertions enabled, 500,000 rows,
one client, JIT and parallel query disabled, one one-second warmup, and five
three-second measured runs.  All coefficients of variation were below 1.6%.

| Width | Workload | `numeric` median TPS | fixed median TPS | fixed / numeric |
|---:|:---|---:|---:|---:|
| 64 | filter | 20.548 | 46.224 | 2.250x |
| 64 | add + sum | 20.621 | 32.860 | 1.594x |
| 64 | multiply + sum | 20.920 | 34.102 | 1.630x |
| 128 | filter | 20.653 | 43.455 | 2.104x |
| 128 | add + sum | 20.850 | 31.868 | 1.528x |
| 128 | multiply + sum | 20.396 | 28.089 | 1.377x |

The setup correctness guard reported equality with `numeric` for all four sum
and multiply checks.  These results satisfy the experiment's predeclared 1.15x
threshold on this host; they are not a claim about every workload or platform.

Heap storage was larger because PostgreSQL must keep scale in each standalone
Datum: 22,142,976 bytes for `decimal64` versus 18,128,896 for `numeric`, and
26,091,520 bytes for `decimal128` versus 18,128,896 for `numeric`.  This is
separate from an Arrow-aware table AM, whose value buffers use exactly 8 and 16
bytes per non-null value through the exported codec interface.

Machine-readable inputs are in
`results/20260910-darwin-arm64/{samples,summary,comparisons}.csv`.

## Native int128 result: 2026-09-11 Darwin arm64

The portable-limb and native-int128 libraries were built from the same source
against PostgreSQL 18.6 (Homebrew, Apple clang 17, `-O2`) and loaded into one
server.  The run used 1,000,000 rows, one client, JIT and parallel query
disabled, a one-second warmup, and five alternating two-second measurements.
All coefficients of variation were below 1.6%, and the setup correctness guard
reported identical results for both implementations.

| Workload | portable median TPS | int128 median TPS | int128 / portable |
|:---|---:|---:|---:|
| compare | 20.676 | 20.996 | 1.016x |
| sum | 27.108 | 28.851 | 1.064x |
| add | 17.538 | 19.399 | 1.106x |
| add with rescale | 7.172 | 14.832 | 2.068x |
| multiply | 14.686 | 17.820 | 1.213x |

Native int128 met the predeclared 1.10x target for addition, rescaling addition,
and multiplication.  The comparison control was neutral, and `sum` improved
without exceeding the allowed 5% regression threshold.  These are local
warm-cache CPU results, not a claim about every compiler, platform, or query.

Machine-readable inputs and raw reports are in
`results/20260911-darwin-arm64-int128/`.
