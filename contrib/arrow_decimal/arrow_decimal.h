/*-------------------------------------------------------------------------
 *
 * arrow_decimal.h
 *    Table-AM interface for Arrow-compatible fixed decimals.
 *
 * Copyright (c) 2026, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#ifndef ARROW_DECIMAL_H
#define ARROW_DECIMAL_H

#include "postgres.h"

#define ARROW_DECIMAL64_WIDTH 8
#define ARROW_DECIMAL128_WIDTH 16
#define ARROW_DECIMAL64_MAX_PRECISION 18
#define ARROW_DECIMAL128_MAX_PRECISION 38

/*
 * Decode type metadata from a decimal64/decimal128 atttypmod.  A table AM
 * should reject an Arrow-backed column when this returns false, because an
 * Arrow decimal's scale belongs to the field schema rather than each value.
 */
extern PGDLLEXPORT bool arrow_decimal64_typmod(int32 typmod,
											  int32 *precision, int32 *scale);
extern PGDLLEXPORT bool arrow_decimal128_typmod(int32 typmod,
											   int32 *precision, int32 *scale);

/*
 * Encode/decode exactly one Arrow value-buffer entry.  The byte arrays are
 * little-endian signed two's-complement scaled integers of exactly 8 or 16
 * bytes.  Precision and scale are taken from typmod and are not serialized.
 */
extern PGDLLEXPORT void arrow_decimal64_encode(Datum datum, int32 typmod,
											  unsigned char out[ARROW_DECIMAL64_WIDTH]);
extern PGDLLEXPORT Datum arrow_decimal64_decode(
	const unsigned char input[ARROW_DECIMAL64_WIDTH], int32 typmod);
extern PGDLLEXPORT void arrow_decimal128_encode(Datum datum, int32 typmod,
											   unsigned char out[ARROW_DECIMAL128_WIDTH]);
extern PGDLLEXPORT Datum arrow_decimal128_decode(
	const unsigned char input[ARROW_DECIMAL128_WIDTH], int32 typmod);

#endif							/* ARROW_DECIMAL_H */
