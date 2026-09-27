\set ON_ERROR_STOP on

DROP EXTENSION IF EXISTS arrow_decimal CASCADE;
CREATE EXTENSION arrow_decimal;

DROP TABLE IF EXISTS ad_numeric64;
DROP TABLE IF EXISTS ad_decimal64;
DROP TABLE IF EXISTS ad_numeric128;
DROP TABLE IF EXISTS ad_decimal128;

CREATE UNLOGGED TABLE ad_numeric64 (v numeric(18,2) NOT NULL);
CREATE UNLOGGED TABLE ad_decimal64 (v decimal64(18,2) NOT NULL);
CREATE UNLOGGED TABLE ad_numeric128 (v numeric(38,4) NOT NULL);
CREATE UNLOGGED TABLE ad_decimal128 (v decimal128(38,4) NOT NULL);

INSERT INTO ad_numeric64
SELECT ((g - ((CAST(:rows AS bigint) + 1) / 2))::numeric / 100)::numeric(18,2)
FROM generate_series(1, CAST(:rows AS bigint)) AS g;
INSERT INTO ad_decimal64 SELECT v FROM ad_numeric64;

INSERT INTO ad_numeric128
SELECT ((g - ((CAST(:rows AS bigint) + 1) / 2))::numeric / 10000)::numeric(38,4)
FROM generate_series(1, CAST(:rows AS bigint)) AS g;
INSERT INTO ad_decimal128 SELECT v FROM ad_numeric128;

VACUUM (ANALYZE, FREEZE) ad_numeric64;
VACUUM (ANALYZE, FREEZE) ad_decimal64;
VACUUM (ANALYZE, FREEZE) ad_numeric128;
VACUUM (ANALYZE, FREEZE) ad_decimal128;

SELECT count(*) AS row_count FROM ad_numeric64;
SELECT
    (SELECT sum(v) FROM ad_numeric64) =
        (SELECT sum(v)::numeric FROM ad_decimal64) AS decimal64_sum_matches,
    (SELECT sum(v * 1.01::numeric(18,2)) FROM ad_numeric64) =
        (SELECT sum(v * '1.01'::decimal64(18,2))::numeric
         FROM ad_decimal64) AS decimal64_multiply_matches,
    (SELECT sum(v) FROM ad_numeric128) =
        (SELECT sum(v)::numeric FROM ad_decimal128) AS decimal128_sum_matches,
    (SELECT sum(v * 1.0100::numeric(38,4)) FROM ad_numeric128) =
        (SELECT sum(v * '1.0100'::decimal128(38,4))::numeric
         FROM ad_decimal128) AS decimal128_multiply_matches;

SELECT relname, pg_relation_size(oid) AS heap_bytes
FROM pg_class
WHERE relname IN ('ad_numeric64', 'ad_decimal64',
                  'ad_numeric128', 'ad_decimal128')
ORDER BY relname;
