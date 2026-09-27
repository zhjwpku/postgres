\set ON_ERROR_STOP on

DROP TABLE IF EXISTS ad_int128;
DROP EXTENSION IF EXISTS arrow_decimal CASCADE;
CREATE EXTENSION arrow_decimal;

CREATE UNLOGGED TABLE ad_int128 (v decimal128(38,4) NOT NULL);
INSERT INTO ad_int128
SELECT ((g - ((CAST(:rows AS bigint) + 1) / 2))::numeric / 10000)::decimal128(38,4)
FROM generate_series(1, CAST(:rows AS bigint)) AS g;
VACUUM (ANALYZE, FREEZE) ad_int128;

CREATE FUNCTION benchmark_decimal128_add_portable(decimal128, decimal128)
RETURNS decimal128
AS :'portable_lib', 'decimal128_add'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION benchmark_decimal128_mul_portable(decimal128, decimal128)
RETURNS decimal128
AS :'portable_lib', 'decimal128_mul'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION benchmark_decimal128_cmp_portable(decimal128, decimal128)
RETURNS integer
AS :'portable_lib', 'decimal128_cmp'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION benchmark_decimal128_sum_trans_portable(internal, decimal128)
RETURNS internal
AS :'portable_lib', 'decimal128_sum_transfn'
LANGUAGE C IMMUTABLE PARALLEL SAFE;
CREATE FUNCTION benchmark_decimal128_sum_final_portable(internal)
RETURNS decimal128
AS :'portable_lib', 'decimal128_sum_finalfn'
LANGUAGE C IMMUTABLE PARALLEL SAFE;
CREATE AGGREGATE benchmark_sum_portable(decimal128) (
    SFUNC = benchmark_decimal128_sum_trans_portable,
    STYPE = internal,
    FINALFUNC = benchmark_decimal128_sum_final_portable
);

CREATE FUNCTION benchmark_decimal128_add_int128(decimal128, decimal128)
RETURNS decimal128
AS :'int128_lib', 'decimal128_add'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION benchmark_decimal128_mul_int128(decimal128, decimal128)
RETURNS decimal128
AS :'int128_lib', 'decimal128_mul'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION benchmark_decimal128_cmp_int128(decimal128, decimal128)
RETURNS integer
AS :'int128_lib', 'decimal128_cmp'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION benchmark_decimal128_sum_trans_int128(internal, decimal128)
RETURNS internal
AS :'int128_lib', 'decimal128_sum_transfn'
LANGUAGE C IMMUTABLE PARALLEL SAFE;
CREATE FUNCTION benchmark_decimal128_sum_final_int128(internal)
RETURNS decimal128
AS :'int128_lib', 'decimal128_sum_finalfn'
LANGUAGE C IMMUTABLE PARALLEL SAFE;
CREATE AGGREGATE benchmark_sum_int128(decimal128) (
    SFUNC = benchmark_decimal128_sum_trans_int128,
    STYPE = internal,
    FINALFUNC = benchmark_decimal128_sum_final_int128
);

SELECT count(*) AS row_count FROM ad_int128;
SELECT benchmark_sum_portable(v)::numeric =
           benchmark_sum_int128(v)::numeric AS sum_matches,
       benchmark_sum_portable(
           benchmark_decimal128_add_portable(v, '1.2500'))::numeric =
           benchmark_sum_int128(
           benchmark_decimal128_add_int128(v, '1.2500'))::numeric
           AS add_matches,
       benchmark_sum_portable(
           benchmark_decimal128_add_portable(
               v, '1.25001'::decimal128(38,5)))::numeric =
           benchmark_sum_int128(
           benchmark_decimal128_add_int128(
               v, '1.25001'::decimal128(38,5)))::numeric
           AS rescale_matches,
       benchmark_sum_portable(
           benchmark_decimal128_mul_portable(v, '1.0100'))::numeric =
           benchmark_sum_int128(
           benchmark_decimal128_mul_int128(v, '1.0100'))::numeric
           AS multiply_matches
FROM ad_int128;
