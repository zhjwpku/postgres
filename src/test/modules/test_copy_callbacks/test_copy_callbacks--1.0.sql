/* src/test/modules/test_copy_callbacks/test_copy_callbacks--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION test_copy_callbacks" to load this file. \quit

CREATE FUNCTION test_copy_to_callback(pg_catalog.regclass)
	RETURNS pg_catalog.void
	AS 'MODULE_PATHNAME' LANGUAGE C;

CREATE FUNCTION test_copy_from_json_callback(pg_catalog.regclass, pg_catalog.text,
	pg_catalog.int4, pg_catalog.bool DEFAULT false)
	RETURNS pg_catalog.int8
	AS 'MODULE_PATHNAME' LANGUAGE C STRICT;
