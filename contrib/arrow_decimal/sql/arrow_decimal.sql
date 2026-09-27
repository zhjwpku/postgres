CREATE EXTENSION arrow_decimal;

SELECT '123.45'::decimal64(18,2);
SELECT '-0.006'::decimal64(18,3);
SELECT '999999999999999999'::decimal64(18,0);
SELECT '12345678901234567890123456789012345678'::decimal128(38,0);
SELECT '-1234.5678'::decimal128(38,4);
SELECT '149'::decimal64(3,-2), '150'::decimal64(3,-2);
SELECT '-1.235'::decimal64(18,2);
SELECT pg_column_size('1.00'::decimal64(18,2)),
       pg_column_size('1.00'::decimal128(38,2));

SELECT '1.20'::decimal64(18,2) = '1.200'::decimal64(18,3);
SELECT '1.20'::decimal128(38,2) < '1.201'::decimal128(38,3);
SELECT '-1.20'::decimal128(38,2) < '-1.199'::decimal128(38,3);
SELECT '10.25'::decimal64(18,2) + '2.50'::decimal64(18,2);
SELECT '1.2'::decimal64(18,1) + '0.03'::decimal64(18,2);
SELECT '10.25'::decimal128(38,2) - '2.50'::decimal128(38,2);
SELECT '1.25'::decimal64(18,2) * '2.00'::decimal64(18,2);
SELECT '1.25'::decimal64(18,2) + '2.5000'::decimal128(38,4);
SELECT '50000000000000000000000000000000000000'::decimal128(38,0) +
       '40000000000000000000000000000000000000'::decimal128(38,0);
SELECT '-50000000000000000000000000000000000000'::decimal128(38,0) -
       '40000000000000000000000000000000000000'::decimal128(38,0);
SELECT '9999999999999999999'::decimal128(38,0) *
       '9999999999999999999'::decimal128(38,0);
SELECT '12345678901234567890.1234'::decimal128(38,4) +
       '0.00006'::decimal128(38,5);
SELECT -('-99999999999999999999999999999999999999'::decimal128(38,0));
SELECT sum(v) FROM (VALUES ('1.25'::decimal64(18,2)),
                           (NULL),
                           ('2.75'::decimal64(18,2))) AS t(v);
-- Intermediate precision overflow must not make sum input-order dependent.
SELECT sum(v) FROM (VALUES ('900000000000000000'::decimal64(18,0)),
                           ('900000000000000000'::decimal64(18,0)),
                           ('-900000000000000000'::decimal64(18,0))) AS t(v);
-- Exercise the Numeric fallback after physical int64 overflow.
SELECT sum(CASE WHEN i <= 10
                THEN '999999999999999999'::decimal64(18,0)
                ELSE '-999999999999999999'::decimal64(18,0)
           END)
FROM generate_series(1, 19) AS g(i);
-- Decimal128 falls back when the signed int128 accumulator overflows.
SELECT sum(v) FROM
    (VALUES ('90000000000000000000000000000000000000'::decimal128(38,0)),
            ('90000000000000000000000000000000000000'::decimal128(38,0)),
            ('-90000000000000000000000000000000000000'::decimal128(38,0))) AS t(v);
SELECT sum(v) FROM (VALUES ('1.2'::decimal64(18,1)),
                           ('0.03'::decimal64(18,2))) AS t(v);
SELECT sum(v) IS NULL FROM (VALUES (NULL::decimal64)) AS t(v);
SELECT sum(v) IS NULL FROM
    (SELECT '1'::decimal128 AS v WHERE false) AS t;
SELECT p.oid::regprocedure, p.proparallel,
       a.aggcombinefn <> 0::oid AS combine,
       a.aggserialfn <> 0::oid AS serialize,
       a.aggdeserialfn <> 0::oid AS deserialize
FROM pg_aggregate AS a
JOIN pg_proc AS p ON p.oid = a.aggfnoid
WHERE p.oid IN ('sum(decimal64)'::regprocedure,
                'sum(decimal128)'::regprocedure)
ORDER BY p.oid::regprocedure::text;

-- Exercise partial-state serialization and combination, including fallback
-- states whose intermediate values exceed the fixed-width accumulator.
CREATE TABLE arrow_decimal_parallel (
    d64 decimal64(18,0),
    d128 decimal128(38,0)
);
INSERT INTO arrow_decimal_parallel
SELECT CASE WHEN i <= 10000
            THEN '999999999999999999'::decimal64(18,0)
            ELSE '-999999999999999999'::decimal64(18,0)
       END,
       CASE WHEN i <= 10000
            THEN '90000000000000000000000000000000000000'::decimal128(38,0)
            ELSE '-90000000000000000000000000000000000000'::decimal128(38,0)
       END
FROM generate_series(1, 20000) AS g(i);
SET debug_parallel_query = regress;
SET max_parallel_workers_per_gather = 2;
SET min_parallel_table_scan_size = 0;
SET parallel_setup_cost = 0;
SET parallel_tuple_cost = 0;
EXPLAIN (COSTS OFF)
SELECT sum(d64), sum(d128) FROM arrow_decimal_parallel;
SELECT sum(d64), sum(d128) FROM arrow_decimal_parallel;
RESET debug_parallel_query;
RESET max_parallel_workers_per_gather;
RESET min_parallel_table_scan_size;
RESET parallel_setup_cost;
RESET parallel_tuple_cost;
DROP TABLE arrow_decimal_parallel;

SELECT decimal64_hash('1.20'::decimal64(18,2)) =
       decimal64_hash('1.200'::decimal64(18,3));

SELECT ('12.340'::decimal64(18,3))::numeric;
SELECT (12.345::numeric)::decimal64(5,2);
SELECT (12345::numeric / 100)::decimal64(18,2);
SELECT ('12.34'::decimal64(18,2))::decimal128(38,4);
SELECT ('12.345'::decimal128(38,3))::decimal64(18,2);

SELECT encode(decimal64_arrow_bytes('-123.45'::decimal64(18,2)), 'hex');
SELECT encode(decimal128_arrow_bytes('-123.45'::decimal128(38,2)), 'hex');
SELECT encode(decimal64_send('-123.45'::decimal64(18,2)), 'hex');
SELECT encode(decimal128_send('-123.45'::decimal128(38,2)), 'hex');
SELECT decimal64_from_arrow(decode('c7cfffffffffffff', 'hex'), 18, 2);
SELECT decimal128_from_arrow(
    decode('c7cfffffffffffffffffffffffffffff', 'hex'), 38, 2);

CREATE TABLE arrow_decimal_test (
    id integer,
    d64 decimal64(18,2),
    d128 decimal128(38,4)
);
INSERT INTO arrow_decimal_test VALUES
    (1, '12.34', '123456789012345678901234567890.1234'),
    (2, '-5.60', '-42.0000'),
    (3, '12.34', '0.0001');
PREPARE arrow_decimal_insert(decimal64, decimal128) AS
    INSERT INTO arrow_decimal_test VALUES (4, $1, $2);
EXECUTE arrow_decimal_insert('1.239', '2.34567');
DEALLOCATE arrow_decimal_insert;
CREATE INDEX arrow_decimal_test_d64_idx ON arrow_decimal_test (d64);
CREATE INDEX arrow_decimal_test_d128_idx ON arrow_decimal_test (d128);
CREATE INDEX arrow_decimal_test_d64_hash_idx ON arrow_decimal_test USING hash (d64);
SELECT id, d64, d128 FROM arrow_decimal_test ORDER BY d64, id;
SELECT count(*) FROM arrow_decimal_test WHERE d64 = '12.340'::decimal64(18,3);

SELECT '1000'::decimal64(3,0);
SELECT '1.234'::decimal64(18,2);
SELECT '999999999999999999'::decimal64(18,0) + '1'::decimal64(18,0);
SELECT '99999999999999999999999999999999999999'::decimal128(38,0) +
       '1'::decimal128(38,0);
SELECT sum(v) FROM
    (VALUES ('999999999999999999'::decimal64(18,0)),
            ('999999999999999999'::decimal64(18,0))) AS t(v);
SELECT sum(v) FROM
    (VALUES ('99999999999999999999999999999999999999'::decimal128(38,0)),
            ('99999999999999999999999999999999999999'::decimal128(38,0))) AS t(v);
SELECT '10000000000000000000'::decimal128(38,0) *
       '10000000000000000000'::decimal128(38,0);
SELECT '-10000000000000000000'::decimal128(38,0) *
       '10000000000000000000'::decimal128(38,0);
SELECT decimal64_from_arrow(decode('0000000000000000', 'hex'), 19, 0);
SELECT decimal128_from_arrow(decode('00', 'hex'), 38, 0);
SELECT '1.2.3'::decimal128;
SELECT '0e100000'::decimal64(18,2);
SELECT '0.000000000000000001'::decimal64(18,18) *
       '0.000000000000000001'::decimal64(18,18);
CREATE TABLE invalid_decimal64 (v decimal64(0,0));
CREATE TABLE invalid_decimal128 (v decimal128(39,0));
CREATE TABLE invalid_decimal64_scale (v decimal64(18,19));

DROP EXTENSION arrow_decimal CASCADE;
