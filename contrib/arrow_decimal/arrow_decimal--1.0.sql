/* contrib/arrow_decimal/arrow_decimal--1.0.sql */

\echo Use "CREATE EXTENSION arrow_decimal" to load this file. \quit

CREATE TYPE decimal64;

CREATE FUNCTION decimal64_in(cstring, oid, integer)
RETURNS decimal64
AS 'MODULE_PATHNAME', 'decimal64_in'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION decimal64_out(decimal64)
RETURNS cstring
AS 'MODULE_PATHNAME', 'decimal64_out'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION decimal64_recv(internal, oid, integer)
RETURNS decimal64
AS 'MODULE_PATHNAME', 'decimal64_recv'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION decimal64_send(decimal64)
RETURNS bytea
AS 'MODULE_PATHNAME', 'decimal64_send'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION decimal64_typmod_in(cstring[])
RETURNS integer
AS 'MODULE_PATHNAME', 'decimal64_typmod_in'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION decimal64_typmod_out(integer)
RETURNS cstring
AS 'MODULE_PATHNAME', 'decimal64_typmod_out'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE TYPE decimal64 (
    INPUT = decimal64_in,
    OUTPUT = decimal64_out,
    RECEIVE = decimal64_recv,
    SEND = decimal64_send,
    TYPMOD_IN = decimal64_typmod_in,
    TYPMOD_OUT = decimal64_typmod_out,
    INTERNALLENGTH = 16,
    ALIGNMENT = double,
    STORAGE = plain,
    CATEGORY = 'N'
);

CREATE FUNCTION decimal64(decimal64, integer, boolean)
RETURNS decimal64
AS 'MODULE_PATHNAME', 'decimal64_typmod'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE CAST (decimal64 AS decimal64)
WITH FUNCTION decimal64(decimal64, integer, boolean)
AS IMPLICIT;

CREATE FUNCTION decimal64_cmp(decimal64, decimal64)
RETURNS integer
AS 'MODULE_PATHNAME', 'decimal64_cmp'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal64_eq(decimal64, decimal64)
RETURNS boolean
AS 'MODULE_PATHNAME', 'decimal64_eq'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal64_ne(decimal64, decimal64)
RETURNS boolean
AS 'MODULE_PATHNAME', 'decimal64_ne'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal64_lt(decimal64, decimal64)
RETURNS boolean
AS 'MODULE_PATHNAME', 'decimal64_lt'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal64_le(decimal64, decimal64)
RETURNS boolean
AS 'MODULE_PATHNAME', 'decimal64_le'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal64_gt(decimal64, decimal64)
RETURNS boolean
AS 'MODULE_PATHNAME', 'decimal64_gt'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal64_ge(decimal64, decimal64)
RETURNS boolean
AS 'MODULE_PATHNAME', 'decimal64_ge'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE OPERATOR = (
    LEFTARG = decimal64, RIGHTARG = decimal64,
    FUNCTION = decimal64_eq, COMMUTATOR = '=', NEGATOR = '<>',
    RESTRICT = eqsel, JOIN = eqjoinsel, HASHES, MERGES
);
CREATE OPERATOR <> (
    LEFTARG = decimal64, RIGHTARG = decimal64,
    FUNCTION = decimal64_ne, COMMUTATOR = '<>', NEGATOR = '=',
    RESTRICT = neqsel, JOIN = neqjoinsel
);
CREATE OPERATOR < (
    LEFTARG = decimal64, RIGHTARG = decimal64,
    FUNCTION = decimal64_lt, COMMUTATOR = '>', NEGATOR = '>=',
    RESTRICT = scalarltsel, JOIN = scalarltjoinsel
);
CREATE OPERATOR <= (
    LEFTARG = decimal64, RIGHTARG = decimal64,
    FUNCTION = decimal64_le, COMMUTATOR = '>=', NEGATOR = '>',
    RESTRICT = scalarlesel, JOIN = scalarlejoinsel
);
CREATE OPERATOR > (
    LEFTARG = decimal64, RIGHTARG = decimal64,
    FUNCTION = decimal64_gt, COMMUTATOR = '<', NEGATOR = '<=',
    RESTRICT = scalargtsel, JOIN = scalargtjoinsel
);
CREATE OPERATOR >= (
    LEFTARG = decimal64, RIGHTARG = decimal64,
    FUNCTION = decimal64_ge, COMMUTATOR = '<=', NEGATOR = '<',
    RESTRICT = scalargesel, JOIN = scalargejoinsel
);

CREATE FUNCTION decimal64_add(decimal64, decimal64)
RETURNS decimal64
AS 'MODULE_PATHNAME', 'decimal64_add'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal64_sub(decimal64, decimal64)
RETURNS decimal64
AS 'MODULE_PATHNAME', 'decimal64_sub'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal64_mul(decimal64, decimal64)
RETURNS decimal64
AS 'MODULE_PATHNAME', 'decimal64_mul'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal64_uminus(decimal64)
RETURNS decimal64
AS 'MODULE_PATHNAME', 'decimal64_uminus'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE OPERATOR + (LEFTARG = decimal64, RIGHTARG = decimal64,
                   FUNCTION = decimal64_add, COMMUTATOR = '+');
CREATE OPERATOR - (LEFTARG = decimal64, RIGHTARG = decimal64,
                   FUNCTION = decimal64_sub);
CREATE OPERATOR * (LEFTARG = decimal64, RIGHTARG = decimal64,
                   FUNCTION = decimal64_mul, COMMUTATOR = '*');
CREATE OPERATOR - (RIGHTARG = decimal64, FUNCTION = decimal64_uminus);

CREATE FUNCTION decimal64_hash(decimal64)
RETURNS integer
AS 'MODULE_PATHNAME', 'decimal64_hash'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal64_hash_extended(decimal64, bigint)
RETURNS bigint
AS 'MODULE_PATHNAME', 'decimal64_hash_extended'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE OPERATOR CLASS decimal64_ops DEFAULT FOR TYPE decimal64 USING btree AS
    OPERATOR 1 <,
    OPERATOR 2 <=,
    OPERATOR 3 =,
    OPERATOR 4 >=,
    OPERATOR 5 >,
    FUNCTION 1 decimal64_cmp(decimal64, decimal64);

CREATE OPERATOR CLASS decimal64_hash_ops DEFAULT FOR TYPE decimal64 USING hash AS
    OPERATOR 1 =,
    FUNCTION 1 decimal64_hash(decimal64),
    FUNCTION 2 decimal64_hash_extended(decimal64, bigint);

CREATE FUNCTION decimal64_sum_transfn(internal, decimal64)
RETURNS internal
AS 'MODULE_PATHNAME', 'decimal64_sum_transfn'
LANGUAGE C IMMUTABLE PARALLEL SAFE;
CREATE FUNCTION decimal64_sum_finalfn(internal)
RETURNS decimal64
AS 'MODULE_PATHNAME', 'decimal64_sum_finalfn'
LANGUAGE C IMMUTABLE PARALLEL SAFE;
CREATE FUNCTION decimal64_sum_combinefn(internal, internal)
RETURNS internal
AS 'MODULE_PATHNAME', 'decimal64_sum_combinefn'
LANGUAGE C IMMUTABLE PARALLEL SAFE;
CREATE FUNCTION decimal64_sum_serializefn(internal)
RETURNS bytea
AS 'MODULE_PATHNAME', 'decimal64_sum_serializefn'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal64_sum_deserializefn(bytea, internal)
RETURNS internal
AS 'MODULE_PATHNAME', 'decimal64_sum_deserializefn'
LANGUAGE C IMMUTABLE PARALLEL SAFE;
CREATE AGGREGATE sum(decimal64) (
    SFUNC = decimal64_sum_transfn,
    STYPE = internal,
    FINALFUNC = decimal64_sum_finalfn,
    COMBINEFUNC = decimal64_sum_combinefn,
    SERIALFUNC = decimal64_sum_serializefn,
    DESERIALFUNC = decimal64_sum_deserializefn,
    PARALLEL = SAFE
);

CREATE FUNCTION decimal64_arrow_bytes(decimal64)
RETURNS bytea
AS 'MODULE_PATHNAME', 'decimal64_arrow_bytes'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal64_from_arrow(bytea, integer, integer)
RETURNS decimal64
AS 'MODULE_PATHNAME', 'decimal64_from_arrow'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;


CREATE TYPE decimal128;

CREATE FUNCTION decimal128_in(cstring, oid, integer)
RETURNS decimal128
AS 'MODULE_PATHNAME', 'decimal128_in'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal128_out(decimal128)
RETURNS cstring
AS 'MODULE_PATHNAME', 'decimal128_out'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal128_recv(internal, oid, integer)
RETURNS decimal128
AS 'MODULE_PATHNAME', 'decimal128_recv'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal128_send(decimal128)
RETURNS bytea
AS 'MODULE_PATHNAME', 'decimal128_send'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal128_typmod_in(cstring[])
RETURNS integer
AS 'MODULE_PATHNAME', 'decimal128_typmod_in'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal128_typmod_out(integer)
RETURNS cstring
AS 'MODULE_PATHNAME', 'decimal128_typmod_out'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE TYPE decimal128 (
    INPUT = decimal128_in,
    OUTPUT = decimal128_out,
    RECEIVE = decimal128_recv,
    SEND = decimal128_send,
    TYPMOD_IN = decimal128_typmod_in,
    TYPMOD_OUT = decimal128_typmod_out,
    INTERNALLENGTH = 24,
    ALIGNMENT = double,
    STORAGE = plain,
    CATEGORY = 'N'
);

CREATE FUNCTION decimal128(decimal128, integer, boolean)
RETURNS decimal128
AS 'MODULE_PATHNAME', 'decimal128_typmod'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE CAST (decimal128 AS decimal128)
WITH FUNCTION decimal128(decimal128, integer, boolean)
AS IMPLICIT;

CREATE FUNCTION decimal128_cmp(decimal128, decimal128)
RETURNS integer
AS 'MODULE_PATHNAME', 'decimal128_cmp'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal128_eq(decimal128, decimal128)
RETURNS boolean
AS 'MODULE_PATHNAME', 'decimal128_eq'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal128_ne(decimal128, decimal128)
RETURNS boolean
AS 'MODULE_PATHNAME', 'decimal128_ne'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal128_lt(decimal128, decimal128)
RETURNS boolean
AS 'MODULE_PATHNAME', 'decimal128_lt'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal128_le(decimal128, decimal128)
RETURNS boolean
AS 'MODULE_PATHNAME', 'decimal128_le'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal128_gt(decimal128, decimal128)
RETURNS boolean
AS 'MODULE_PATHNAME', 'decimal128_gt'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal128_ge(decimal128, decimal128)
RETURNS boolean
AS 'MODULE_PATHNAME', 'decimal128_ge'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE OPERATOR = (
    LEFTARG = decimal128, RIGHTARG = decimal128,
    FUNCTION = decimal128_eq, COMMUTATOR = '=', NEGATOR = '<>',
    RESTRICT = eqsel, JOIN = eqjoinsel, HASHES, MERGES
);
CREATE OPERATOR <> (
    LEFTARG = decimal128, RIGHTARG = decimal128,
    FUNCTION = decimal128_ne, COMMUTATOR = '<>', NEGATOR = '=',
    RESTRICT = neqsel, JOIN = neqjoinsel
);
CREATE OPERATOR < (
    LEFTARG = decimal128, RIGHTARG = decimal128,
    FUNCTION = decimal128_lt, COMMUTATOR = '>', NEGATOR = '>=',
    RESTRICT = scalarltsel, JOIN = scalarltjoinsel
);
CREATE OPERATOR <= (
    LEFTARG = decimal128, RIGHTARG = decimal128,
    FUNCTION = decimal128_le, COMMUTATOR = '>=', NEGATOR = '>',
    RESTRICT = scalarlesel, JOIN = scalarlejoinsel
);
CREATE OPERATOR > (
    LEFTARG = decimal128, RIGHTARG = decimal128,
    FUNCTION = decimal128_gt, COMMUTATOR = '<', NEGATOR = '<=',
    RESTRICT = scalargtsel, JOIN = scalargtjoinsel
);
CREATE OPERATOR >= (
    LEFTARG = decimal128, RIGHTARG = decimal128,
    FUNCTION = decimal128_ge, COMMUTATOR = '<=', NEGATOR = '<',
    RESTRICT = scalargesel, JOIN = scalargejoinsel
);

CREATE FUNCTION decimal128_add(decimal128, decimal128)
RETURNS decimal128
AS 'MODULE_PATHNAME', 'decimal128_add'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal128_sub(decimal128, decimal128)
RETURNS decimal128
AS 'MODULE_PATHNAME', 'decimal128_sub'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal128_mul(decimal128, decimal128)
RETURNS decimal128
AS 'MODULE_PATHNAME', 'decimal128_mul'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal128_uminus(decimal128)
RETURNS decimal128
AS 'MODULE_PATHNAME', 'decimal128_uminus'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE OPERATOR + (LEFTARG = decimal128, RIGHTARG = decimal128,
                   FUNCTION = decimal128_add, COMMUTATOR = '+');
CREATE OPERATOR - (LEFTARG = decimal128, RIGHTARG = decimal128,
                   FUNCTION = decimal128_sub);
CREATE OPERATOR * (LEFTARG = decimal128, RIGHTARG = decimal128,
                   FUNCTION = decimal128_mul, COMMUTATOR = '*');
CREATE OPERATOR - (RIGHTARG = decimal128, FUNCTION = decimal128_uminus);

CREATE FUNCTION decimal128_hash(decimal128)
RETURNS integer
AS 'MODULE_PATHNAME', 'decimal128_hash'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal128_hash_extended(decimal128, bigint)
RETURNS bigint
AS 'MODULE_PATHNAME', 'decimal128_hash_extended'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE OPERATOR CLASS decimal128_ops DEFAULT FOR TYPE decimal128 USING btree AS
    OPERATOR 1 <,
    OPERATOR 2 <=,
    OPERATOR 3 =,
    OPERATOR 4 >=,
    OPERATOR 5 >,
    FUNCTION 1 decimal128_cmp(decimal128, decimal128);

CREATE OPERATOR CLASS decimal128_hash_ops DEFAULT FOR TYPE decimal128 USING hash AS
    OPERATOR 1 =,
    FUNCTION 1 decimal128_hash(decimal128),
    FUNCTION 2 decimal128_hash_extended(decimal128, bigint);

CREATE FUNCTION decimal128_sum_transfn(internal, decimal128)
RETURNS internal
AS 'MODULE_PATHNAME', 'decimal128_sum_transfn'
LANGUAGE C IMMUTABLE PARALLEL SAFE;
CREATE FUNCTION decimal128_sum_finalfn(internal)
RETURNS decimal128
AS 'MODULE_PATHNAME', 'decimal128_sum_finalfn'
LANGUAGE C IMMUTABLE PARALLEL SAFE;
CREATE FUNCTION decimal128_sum_combinefn(internal, internal)
RETURNS internal
AS 'MODULE_PATHNAME', 'decimal128_sum_combinefn'
LANGUAGE C IMMUTABLE PARALLEL SAFE;
CREATE FUNCTION decimal128_sum_serializefn(internal)
RETURNS bytea
AS 'MODULE_PATHNAME', 'decimal128_sum_serializefn'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal128_sum_deserializefn(bytea, internal)
RETURNS internal
AS 'MODULE_PATHNAME', 'decimal128_sum_deserializefn'
LANGUAGE C IMMUTABLE PARALLEL SAFE;
CREATE AGGREGATE sum(decimal128) (
    SFUNC = decimal128_sum_transfn,
    STYPE = internal,
    FINALFUNC = decimal128_sum_finalfn,
    COMBINEFUNC = decimal128_sum_combinefn,
    SERIALFUNC = decimal128_sum_serializefn,
    DESERIALFUNC = decimal128_sum_deserializefn,
    PARALLEL = SAFE
);

CREATE FUNCTION decimal128_arrow_bytes(decimal128)
RETURNS bytea
AS 'MODULE_PATHNAME', 'decimal128_arrow_bytes'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal128_from_arrow(bytea, integer, integer)
RETURNS decimal128
AS 'MODULE_PATHNAME', 'decimal128_from_arrow'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION decimal64_to_numeric(decimal64)
RETURNS numeric
AS 'MODULE_PATHNAME', 'decimal64_to_numeric'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION numeric_to_decimal64(numeric, integer, boolean)
RETURNS decimal64
AS 'MODULE_PATHNAME', 'numeric_to_decimal64'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE CAST (decimal64 AS numeric)
WITH FUNCTION decimal64_to_numeric(decimal64)
AS IMPLICIT;
CREATE CAST (numeric AS decimal64)
WITH FUNCTION numeric_to_decimal64(numeric, integer, boolean)
AS ASSIGNMENT;

CREATE FUNCTION decimal128_to_numeric(decimal128)
RETURNS numeric
AS 'MODULE_PATHNAME', 'decimal128_to_numeric'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION numeric_to_decimal128(numeric, integer, boolean)
RETURNS decimal128
AS 'MODULE_PATHNAME', 'numeric_to_decimal128'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE CAST (decimal128 AS numeric)
WITH FUNCTION decimal128_to_numeric(decimal128)
AS IMPLICIT;
CREATE CAST (numeric AS decimal128)
WITH FUNCTION numeric_to_decimal128(numeric, integer, boolean)
AS ASSIGNMENT;

CREATE FUNCTION decimal64_to_decimal128(decimal64, integer, boolean)
RETURNS decimal128
AS 'MODULE_PATHNAME', 'decimal64_to_decimal128'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION decimal128_to_decimal64(decimal128, integer, boolean)
RETURNS decimal64
AS 'MODULE_PATHNAME', 'decimal128_to_decimal64'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE CAST (decimal64 AS decimal128)
WITH FUNCTION decimal64_to_decimal128(decimal64, integer, boolean)
AS IMPLICIT;
CREATE CAST (decimal128 AS decimal64)
WITH FUNCTION decimal128_to_decimal64(decimal128, integer, boolean)
AS ASSIGNMENT;

COMMENT ON TYPE decimal64 IS
'fixed-precision decimal with an Arrow-compatible 64-bit value codec';
COMMENT ON TYPE decimal128 IS
'fixed-precision decimal with an Arrow-compatible 128-bit value codec';
