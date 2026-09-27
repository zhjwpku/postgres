# arrow_decimal

`arrow_decimal` is an experimental PostgreSQL extension providing distinct
`decimal64(p,s)` and `decimal128(p,s)` SQL types.  It deliberately does not
change the syntax, behavior, or representation of the built-in
`numeric`/`decimal` type, so databases that rely on unconstrained `numeric`
remain compatible.

`decimal64` supports precision 1 through 18 and `decimal128` supports precision
1 through 38.  Scale is signed and is currently bounded to the corresponding
maximum precision.  Input is rounded half away from zero when it is coerced to
a lower scale.  Comparisons work across scales; addition, subtraction,
multiplication, unary minus, `sum`, B-tree indexes, hash indexes, and casts to
and from `numeric` are provided.  Scalar fixed-width overflow raises
`numeric_value_out_of_range` rather than widening the value.

`sum` uses an int64 or int128 transition state while inputs have the same
scale and the physical addition fits.  It promotes the transition state to
PostgreSQL `numeric` only when scales differ or the physical accumulator
overflows, then checks the decimal64 precision 18 or decimal128 precision 38
limit when producing the final result.  Consequently, an oversized partial
sum can later cancel without making the answer depend on input order.  The
aggregate supports partial and parallel aggregation.

## Representations

PostgreSQL expressions can lose their column typmod, while type output and most
operators receive only a Datum.  The executor representation therefore keeps
the scale with the integer and occupies 16 bytes for `decimal64` or 24 bytes
for `decimal128`.  PostgreSQL's binary type format likewise contains a
four-byte network-order scale followed by the raw integer, so expression values
round-trip without relying on a column descriptor.

The Arrow storage boundary is different.  Arrow stores a signed
two's-complement scaled integer in a little-endian value buffer, with precision
and scale in the field schema.  `decimal64_arrow_bytes` and
`decimal128_arrow_bytes` expose exactly those 8-byte and 16-byte entries;
`decimal64_from_arrow` and `decimal128_from_arrow` perform the reverse
conversion.

On compilers where PostgreSQL exposes native `int128` and overflow builtins,
Decimal128 arithmetic uses that type as a register representation.  The Datum
still stores explicit 64-bit high and low words, so its 24-byte ABI and the
16-byte Arrow payload are unchanged.  Platforms without native `int128` keep
the portable limb implementation; define `ARROW_DECIMAL_FORCE_PORTABLE` to
exercise that path on a capable platform.

An out-of-tree table AM can include the installed
`extension/arrow_decimal/arrow_decimal.h` header and use:

```c
arrow_decimal64_typmod(atttypmod, &precision, &scale);
arrow_decimal64_encode(value, atttypmod, eight_bytes);
value = arrow_decimal64_decode(eight_bytes, atttypmod);
```

Equivalent `decimal128` entry points operate on 16 bytes.  The codecs require
a `(p,s)` typmod because Arrow schema metadata is column-level; an Arrow-backed
table AM should reject bare `decimal64`/`decimal128` columns.  This extension
supplies the type and codec boundary, not a table AM implementation.

PostgreSQL does not propagate a result typmod through ordinary operator or
aggregate expressions.  Such results remain self-describing Datums, but their
SQL type is reported as bare `decimal64` or `decimal128`.  Before storing an
expression result in an Arrow-backed relation, explicitly cast it to the
destination `(p,s)` type (or insert it into a column that already has that
typmod).

The layout follows the Apache Arrow decimal type and schema definitions:

- <https://arrow.apache.org/docs/cpp/api/datatype.html>
- <https://github.com/apache/arrow/blob/main/format/Schema.fbs>

## Validation and performance

The module-local regression test covers typmods, rounding (including negative
scale), cross-scale comparison and hashing, arithmetic, overflow, casts,
prepared statements, indexes, and exact Arrow bytes.

`benchmark/run.sh` runs an alternating, repeated `pgbench` comparison against
`numeric(18,2)` and `numeric(38,4)`, keeps machine-readable samples, checks
result equivalence, and reports variance.  See `benchmark/README.md` for the
experiment design and `benchmark/RESULTS.md` for the checked-in local result.
`benchmark/run_int128.sh` performs the same style of controlled comparison
between portable-limb and native-int128 Decimal128 builds.

Current limitations include no division operator, no NaN or infinity values,
and no direct Arrow IPC file reader or writer.
