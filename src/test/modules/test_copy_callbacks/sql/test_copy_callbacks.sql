CREATE EXTENSION test_copy_callbacks;
CREATE TABLE public.test (a INT, b INT, c INT);
INSERT INTO public.test VALUES (1, 2, 3), (12, 34, 56), (123, 456, 789);
SELECT test_copy_to_callback('public.test'::pg_catalog.regclass);

CREATE TABLE public.test_json (a text);
-- Delimiters, nesting and string escapes can span callback reads.
SELECT test_copy_from_json_callback('test_json',
    '[{"a":"brace } and quote \"","ignored":[{},[]]},{"a":"backslash \\"}]', 1);
SELECT * FROM test_json;
TRUNCATE test_json;

-- Many rows share a buffer, with a partial row at the next refill.
SELECT test_copy_from_json_callback('test_json', repeat('{"a":"x"}', 8192), 65536);
SELECT count(*), bool_and(a = 'x') FROM test_json;
TRUNCATE test_json;

-- Consecutive objects span multiple buffers; consumed rows must be discarded.
SELECT test_copy_from_json_callback('test_json',
    repeat('{"a":"' || repeat('x', 70000) || '"}', 4), 65536);
SELECT count(*), bool_and(a = repeat('x', 70000)) FROM test_json;
TRUNCATE test_json;

-- Commas between concatenated objects are rejected identically for all chunks.
SELECT test_copy_from_json_callback('test_json', '{"a":1},{"a":2}', 14);
SELECT test_copy_from_json_callback('test_json', '{"a":1},{"a":2}', 7);
SELECT count(*) FROM test_json;

-- Cancellation must be checked before refilling, even without a complete row.
SELECT test_copy_from_json_callback('test_json', repeat(' ', 131072), 65536, true);
SELECT test_copy_from_json_callback('test_json', '[]' || repeat(' ', 131072), 65536, true);
SELECT test_copy_from_json_callback('test_json', '{"a":"' || repeat('x', 131072), 65536, true);
SELECT test_copy_from_json_callback('test_json', '{"a":"ok"}', 1);
SELECT * FROM test_json;
