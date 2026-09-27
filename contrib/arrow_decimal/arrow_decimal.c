/*-------------------------------------------------------------------------
 *
 * arrow_decimal.c
 *	  Fixed-precision decimal types with Arrow-compatible value codecs.
 *
 * The PostgreSQL executor representation is self-describing: it includes the
 * scale so that output functions and expressions remain correct after a value
 * has been detached from its column typmod.  The Arrow codec functions expose
 * only the little-endian two's-complement integer payload (8 or 16 bytes);
 * precision and scale are schema metadata, just as they are in Arrow.
 *
 * Copyright (c) 2026, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <ctype.h>

#include "arrow_decimal.h"
#include "common/hashfn.h"
#include "common/int.h"
#include "executor/nodeAgg.h"
#include "fmgr.h"
#include "libpq/pqformat.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/fmgrprotos.h"
#include "utils/memutils.h"
#include "utils/numeric.h"

PG_MODULE_MAGIC;

#define DECIMAL64_MAX_PRECISION	ARROW_DECIMAL64_MAX_PRECISION
#define DECIMAL128_MAX_PRECISION ARROW_DECIMAL128_MAX_PRECISION
#define ARROW_DECIMAL_TYPMOD_OFFSET VARHDRSZ
#define ARROW_DECIMAL_AGG_STATE_VERSION 1

#if defined(HAVE_INT128) && !defined(ARROW_DECIMAL_FORCE_PORTABLE)
#define ARROW_DECIMAL_USE_INT128 1
#else
#define ARROW_DECIMAL_USE_INT128 0
#endif

/*
 * These are executor Datums, not Arrow value-buffer entries.  Their first
 * member(s) hold the scaled integer; scale is retained for PostgreSQL paths
 * whose output/function APIs do not carry typmod.
 */
typedef struct Decimal64Datum
{
	int64		value;
	int32		scale;
	int32		reserved;
} Decimal64Datum;

typedef struct Decimal128Datum
{
	uint64		lo;
	int64		hi;
	int32		scale;
	int32		reserved;
} Decimal128Datum;

typedef struct Decimal64AggState
{
	Decimal64Datum sum;
	Numeric		numeric_sum;
	bool		use_numeric;
} Decimal64AggState;

typedef struct Decimal128AggState
{
	Decimal128Datum sum;
	Numeric		numeric_sum;
	bool		use_numeric;
} Decimal128AggState;

/* Unsigned 128-bit magnitude, least-significant limb first. */
typedef struct UInt128Parts
{
	uint32		limb[4];
} UInt128Parts;

typedef struct ArrowDecimalTypmod
{
	int32		precision;
	int32		scale;
} ArrowDecimalTypmod;

StaticAssertDecl(sizeof(Decimal64Datum) == 16,
				 "Decimal64Datum must occupy 16 bytes");
StaticAssertDecl(sizeof(Decimal128Datum) == 24,
				 "Decimal128Datum must occupy 24 bytes");

#define PG_GETARG_DECIMAL64_P(n) ((Decimal64Datum *) PG_GETARG_POINTER(n))
#define PG_GETARG_DECIMAL128_P(n) ((Decimal128Datum *) PG_GETARG_POINTER(n))
#define PG_RETURN_DECIMAL64_P(x) PG_RETURN_POINTER(x)
#define PG_RETURN_DECIMAL128_P(x) PG_RETURN_POINTER(x)

static bool typmod_is_valid(int32 typmod);
static int32 make_arrow_decimal_typmod(int32 precision, int32 scale,
									  int32 max_precision, const char *type_name);
static ArrowDecimalTypmod decode_arrow_decimal_typmod(int32 typmod);
static Datum arrow_decimal_typmod_in(FunctionCallInfo fcinfo,
									 int32 max_precision, const char *type_name);
static Datum arrow_decimal_typmod_out(FunctionCallInfo fcinfo);

static bool u128_is_zero(const UInt128Parts *value);
static bool u128_mul_small(UInt128Parts *value, uint32 multiplier);
static bool u128_add_small(UInt128Parts *value, uint32 addend);
static uint32 u128_div_small(UInt128Parts *value, uint32 divisor);
static int u128_compare(const UInt128Parts *left, const UInt128Parts *right);
static UInt128Parts u128_power_of_ten(int32 exponent);
static char *u128_to_cstring(const UInt128Parts *value);
static UInt128Parts u128_from_words(uint64 hi, uint64 lo);
static void u128_to_words(const UInt128Parts *value, uint64 *hi, uint64 *lo);
static UInt128Parts decimal128_magnitude(const Decimal128Datum *value,
											bool *negative);
static Decimal128Datum *decimal128_from_magnitude(const UInt128Parts *magnitude,
												  bool negative, int32 scale);
#if ARROW_DECIMAL_USE_INT128
static inline int128 decimal128_get_int128(const Decimal128Datum *value);
static inline void decimal128_set_int128(Decimal128Datum *result,
										 int128 value, int32 scale);
#endif

static UInt128Parts parse_decimal_literal(const char *str, int32 target_scale,
										  bool target_scale_is_set,
										  int32 precision, bool *negative,
										  int32 *result_scale,
										  const char *type_name);
static char *format_decimal(const UInt128Parts *magnitude, bool negative,
								int32 scale);
static char *canonical_decimal_string(char *str);

static Decimal64Datum *decimal64_from_string(const char *str, int32 typmod);
static Decimal128Datum *decimal128_from_string(const char *str, int32 typmod);
static Decimal64Datum *decimal64_apply_typmod(const Decimal64Datum *value,
											 int32 typmod);
static Decimal128Datum *decimal128_apply_typmod(const Decimal128Datum *value,
												 int32 typmod);
static void validate_decimal64_precision(int64 value, int32 precision,
										 int32 scale);
static void validate_decimal128_precision(const Decimal128Datum *value,
										  int32 precision);
static char *decimal64_to_cstring(const Decimal64Datum *value);
static char *decimal128_to_cstring(const Decimal128Datum *value);
static int compare_scaled_magnitudes(const UInt128Parts *left, int32 left_scale,
										 const UInt128Parts *right, int32 right_scale);
static int decimal64_cmp_internal(const Decimal64Datum *left,
								  const Decimal64Datum *right);
static int decimal128_cmp_internal(const Decimal128Datum *left,
								   const Decimal128Datum *right);
static void decimal64_add_internal(const Decimal64Datum *left,
								   const Decimal64Datum *right, bool subtract,
								   Decimal64Datum *result);
static void decimal128_add_internal(const Decimal128Datum *left,
									const Decimal128Datum *right, bool subtract,
									Decimal128Datum *result);
static bool decimal128_add_same_scale(const Decimal128Datum *left,
									  const Decimal128Datum *right,
									  Decimal128Datum *result);
static Numeric decimal64_to_numeric_internal(const Decimal64Datum *value);
static Numeric decimal128_to_numeric_internal(const Decimal128Datum *value);
static void decimal64_sum_add_numeric(Decimal64AggState *state,
									  Numeric value, int32 value_scale,
									  MemoryContext agg_context);
static void decimal128_sum_add_numeric(Decimal128AggState *state,
									   Numeric value, int32 value_scale,
									   MemoryContext agg_context);
static void decimal64_sum_add(Decimal64AggState *state,
							  const Decimal64Datum *value,
							  MemoryContext agg_context);
static void decimal128_sum_add(Decimal128AggState *state,
							   const Decimal128Datum *value,
							   MemoryContext agg_context);

/* SQL-callable functions. */
PG_FUNCTION_INFO_V1(decimal64_in);
PG_FUNCTION_INFO_V1(decimal64_out);
PG_FUNCTION_INFO_V1(decimal64_recv);
PG_FUNCTION_INFO_V1(decimal64_send);
PG_FUNCTION_INFO_V1(decimal64_typmod_in);
PG_FUNCTION_INFO_V1(decimal64_typmod_out);
PG_FUNCTION_INFO_V1(decimal64_typmod);
PG_FUNCTION_INFO_V1(decimal64_cmp);
PG_FUNCTION_INFO_V1(decimal64_eq);
PG_FUNCTION_INFO_V1(decimal64_ne);
PG_FUNCTION_INFO_V1(decimal64_lt);
PG_FUNCTION_INFO_V1(decimal64_le);
PG_FUNCTION_INFO_V1(decimal64_gt);
PG_FUNCTION_INFO_V1(decimal64_ge);
PG_FUNCTION_INFO_V1(decimal64_add);
PG_FUNCTION_INFO_V1(decimal64_sub);
PG_FUNCTION_INFO_V1(decimal64_mul);
PG_FUNCTION_INFO_V1(decimal64_uminus);
PG_FUNCTION_INFO_V1(decimal64_sum_transfn);
PG_FUNCTION_INFO_V1(decimal64_sum_finalfn);
PG_FUNCTION_INFO_V1(decimal64_sum_combinefn);
PG_FUNCTION_INFO_V1(decimal64_sum_serializefn);
PG_FUNCTION_INFO_V1(decimal64_sum_deserializefn);
PG_FUNCTION_INFO_V1(decimal64_hash);
PG_FUNCTION_INFO_V1(decimal64_hash_extended);
PG_FUNCTION_INFO_V1(decimal64_arrow_bytes);
PG_FUNCTION_INFO_V1(decimal64_from_arrow);
PG_FUNCTION_INFO_V1(decimal64_to_numeric);
PG_FUNCTION_INFO_V1(numeric_to_decimal64);
PG_FUNCTION_INFO_V1(decimal64_to_decimal128);

PG_FUNCTION_INFO_V1(decimal128_in);
PG_FUNCTION_INFO_V1(decimal128_out);
PG_FUNCTION_INFO_V1(decimal128_recv);
PG_FUNCTION_INFO_V1(decimal128_send);
PG_FUNCTION_INFO_V1(decimal128_typmod_in);
PG_FUNCTION_INFO_V1(decimal128_typmod_out);
PG_FUNCTION_INFO_V1(decimal128_typmod);
PG_FUNCTION_INFO_V1(decimal128_cmp);
PG_FUNCTION_INFO_V1(decimal128_eq);
PG_FUNCTION_INFO_V1(decimal128_ne);
PG_FUNCTION_INFO_V1(decimal128_lt);
PG_FUNCTION_INFO_V1(decimal128_le);
PG_FUNCTION_INFO_V1(decimal128_gt);
PG_FUNCTION_INFO_V1(decimal128_ge);
PG_FUNCTION_INFO_V1(decimal128_add);
PG_FUNCTION_INFO_V1(decimal128_sub);
PG_FUNCTION_INFO_V1(decimal128_mul);
PG_FUNCTION_INFO_V1(decimal128_uminus);
PG_FUNCTION_INFO_V1(decimal128_sum_transfn);
PG_FUNCTION_INFO_V1(decimal128_sum_finalfn);
PG_FUNCTION_INFO_V1(decimal128_sum_combinefn);
PG_FUNCTION_INFO_V1(decimal128_sum_serializefn);
PG_FUNCTION_INFO_V1(decimal128_sum_deserializefn);
PG_FUNCTION_INFO_V1(decimal128_hash);
PG_FUNCTION_INFO_V1(decimal128_hash_extended);
PG_FUNCTION_INFO_V1(decimal128_arrow_bytes);
PG_FUNCTION_INFO_V1(decimal128_from_arrow);
PG_FUNCTION_INFO_V1(decimal128_to_numeric);
PG_FUNCTION_INFO_V1(numeric_to_decimal128);
PG_FUNCTION_INFO_V1(decimal128_to_decimal64);

static bool
typmod_is_valid(int32 typmod)
{
	return typmod >= ARROW_DECIMAL_TYPMOD_OFFSET;
}

static int32
make_arrow_decimal_typmod(int32 precision, int32 scale, int32 max_precision,
						  const char *type_name)
{
	if (precision < 1 || precision > max_precision)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("%s precision %d must be between 1 and %d",
						type_name, precision, max_precision)));

	/* Arrow permits negative scales; keep the prototype's range bounded. */
	if (scale < -max_precision || scale > max_precision)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("%s scale %d must be between %d and %d",
						type_name, scale, -max_precision, max_precision)));

	return ((precision << 16) | ((uint16) scale)) +
		ARROW_DECIMAL_TYPMOD_OFFSET;
}

static ArrowDecimalTypmod
decode_arrow_decimal_typmod(int32 typmod)
{
	ArrowDecimalTypmod result;
	uint32		packed;

	Assert(typmod_is_valid(typmod));
	packed = (uint32) (typmod - ARROW_DECIMAL_TYPMOD_OFFSET);
	result.precision = (packed >> 16) & 0xffff;
	result.scale = (int16) (packed & 0xffff);
	return result;
}

static Datum
arrow_decimal_typmod_in(FunctionCallInfo fcinfo, int32 max_precision,
						const char *type_name)
{
	ArrayType  *ta = PG_GETARG_ARRAYTYPE_P(0);
	int32	   *modifiers;
	int			nmodifiers;
	int32		precision;
	int32		scale;

	modifiers = ArrayGetIntegerTypmods(ta, &nmodifiers);
	if (nmodifiers == 1)
	{
		precision = modifiers[0];
		scale = 0;
	}
	else if (nmodifiers == 2)
	{
		precision = modifiers[0];
		scale = modifiers[1];
	}
	else
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("invalid %s type modifier", type_name)));

	PG_RETURN_INT32(make_arrow_decimal_typmod(precision, scale,
											max_precision, type_name));
}

static Datum
arrow_decimal_typmod_out(FunctionCallInfo fcinfo)
{
	int32		typmod = PG_GETARG_INT32(0);
	char	   *result = palloc(64);

	if (typmod_is_valid(typmod))
	{
		ArrowDecimalTypmod decoded = decode_arrow_decimal_typmod(typmod);

		snprintf(result, 64, "(%d,%d)", decoded.precision, decoded.scale);
	}
	else
		result[0] = '\0';

	PG_RETURN_CSTRING(result);
}

Datum
decimal64_typmod_in(PG_FUNCTION_ARGS)
{
	return arrow_decimal_typmod_in(fcinfo, DECIMAL64_MAX_PRECISION,
								  "DECIMAL64");
}

Datum
decimal64_typmod_out(PG_FUNCTION_ARGS)
{
	return arrow_decimal_typmod_out(fcinfo);
}

Datum
decimal128_typmod_in(PG_FUNCTION_ARGS)
{
	return arrow_decimal_typmod_in(fcinfo, DECIMAL128_MAX_PRECISION,
								  "DECIMAL128");
}

Datum
decimal128_typmod_out(PG_FUNCTION_ARGS)
{
	return arrow_decimal_typmod_out(fcinfo);
}

static bool
u128_is_zero(const UInt128Parts *value)
{
	return value->limb[0] == 0 && value->limb[1] == 0 &&
		value->limb[2] == 0 && value->limb[3] == 0;
}

static bool
u128_mul_small(UInt128Parts *value, uint32 multiplier)
{
	uint64		carry = 0;

	for (int i = 0; i < 4; i++)
	{
		uint64		current = (uint64) value->limb[i] * multiplier + carry;

		value->limb[i] = (uint32) current;
		carry = current >> 32;
	}
	return carry == 0;
}

static bool
u128_add_small(UInt128Parts *value, uint32 addend)
{
	uint64		current = (uint64) value->limb[0] + addend;
	uint64		carry = current >> 32;

	value->limb[0] = (uint32) current;
	for (int i = 1; i < 4 && carry; i++)
	{
		current = (uint64) value->limb[i] + carry;
		value->limb[i] = (uint32) current;
		carry = current >> 32;
	}
	return carry == 0;
}

static uint32
u128_div_small(UInt128Parts *value, uint32 divisor)
{
	uint64		remainder = 0;

	for (int i = 3; i >= 0; i--)
	{
		uint64		current = (remainder << 32) | value->limb[i];

		value->limb[i] = (uint32) (current / divisor);
		remainder = current % divisor;
	}
	return (uint32) remainder;
}

static int
u128_compare(const UInt128Parts *left, const UInt128Parts *right)
{
	for (int i = 3; i >= 0; i--)
	{
		if (left->limb[i] < right->limb[i])
			return -1;
		if (left->limb[i] > right->limb[i])
			return 1;
	}
	return 0;
}

static char *
u128_to_cstring(const UInt128Parts *value)
{
	UInt128Parts work = *value;
	char		reversed[40];
	char	   *result;
	int			ndigits = 0;

	if (u128_is_zero(&work))
		return pstrdup("0");

	while (!u128_is_zero(&work))
		reversed[ndigits++] = '0' + u128_div_small(&work, 10);

	result = palloc(ndigits + 1);
	for (int i = 0; i < ndigits; i++)
		result[i] = reversed[ndigits - i - 1];
	result[ndigits] = '\0';
	return result;
}

static UInt128Parts
u128_power_of_ten(int32 exponent)
{
	UInt128Parts result = {{1, 0, 0, 0}};

	if (exponent == DECIMAL64_MAX_PRECISION)
	{
		result.limb[0] = 0xa7640000U;
		result.limb[1] = 0x0de0b6b3U;
		return result;
	}
	if (exponent == DECIMAL128_MAX_PRECISION)
	{
		result.limb[0] = 0x00000000U;
		result.limb[1] = 0x098a2240U;
		result.limb[2] = 0x5a86c47aU;
		result.limb[3] = 0x4b3b4ca8U;
		return result;
	}
	for (int32 i = 0; i < exponent; i++)
	{
		if (!u128_mul_small(&result, 10))
			pg_unreachable();
	}
	return result;
}

static UInt128Parts
u128_from_words(uint64 hi, uint64 lo)
{
	UInt128Parts result;

	result.limb[0] = (uint32) lo;
	result.limb[1] = (uint32) (lo >> 32);
	result.limb[2] = (uint32) hi;
	result.limb[3] = (uint32) (hi >> 32);
	return result;
}

static void
u128_to_words(const UInt128Parts *value, uint64 *hi, uint64 *lo)
{
	*lo = ((uint64) value->limb[1] << 32) | value->limb[0];
	*hi = ((uint64) value->limb[3] << 32) | value->limb[2];
}

static UInt128Parts
decimal128_magnitude(const Decimal128Datum *value, bool *negative)
{
	uint64		hi = (uint64) value->hi;
	uint64		lo = value->lo;

	*negative = value->hi < 0;
	if (*negative)
	{
		lo = ~lo + 1;
		hi = ~hi + (lo == 0);
	}
	return u128_from_words(hi, lo);
}

static Decimal128Datum *
decimal128_from_magnitude(const UInt128Parts *magnitude, bool negative,
							  int32 scale)
{
	Decimal128Datum *result = palloc0(sizeof(*result));
	uint64			hi;
	uint64			lo;

	u128_to_words(magnitude, &hi, &lo);
	if ((hi & (UINT64CONST(1) << 63)) != 0)
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("DECIMAL128 value is out of range")));

	if (negative && !u128_is_zero(magnitude))
	{
		lo = ~lo + 1;
		hi = ~hi + (lo == 0);
	}
	result->lo = lo;
	result->hi = (int64) hi;
	result->scale = scale;
	return result;
}

/*
 * Keep the on-disk/executor layout independent of the compiler's int128 ABI.
 * Native int128 is only used as a register representation for calculations.
 */
#if ARROW_DECIMAL_USE_INT128
static inline int128
decimal128_get_int128(const Decimal128Datum *value)
{
	return (int128) (((uint128) (uint64) value->hi << 64) | value->lo);
}

static inline void
decimal128_set_int128(Decimal128Datum *result, int128 value, int32 scale)
{
	result->lo = (uint64) value;
	result->hi = (int64) ((uint128) value >> 64);
	result->scale = scale;
	result->reserved = 0;
}
#endif

/*
 * Parse a decimal literal and produce the Arrow unscaled integer magnitude.
 * Rounding when reducing scale follows PostgreSQL numeric's half-away-from-zero
 * behavior.
 */
static UInt128Parts
parse_decimal_literal(const char *str, int32 target_scale,
					  bool target_scale_is_set, int32 precision,
					  bool *negative, int32 *result_scale,
					  const char *type_name)
{
	const char *cursor = str;
	char	   *digits;
	int			ndigits = 0;
	int			fractional_digits = 0;
	bool		saw_decimal_point = false;
	bool		saw_digit = false;
	int64		exponent = 0;
	int			exponent_sign = 1;
	int64		source_scale;
	int64		shift;
	int64		keep;
	bool		round_up = false;
	UInt128Parts magnitude = {{0, 0, 0, 0}};

	while (isspace((unsigned char) *cursor))
		cursor++;
	*negative = false;
	if (*cursor == '+' || *cursor == '-')
	{
		*negative = (*cursor == '-');
		cursor++;
	}

	digits = palloc(strlen(cursor) + 1);
	while (*cursor)
	{
		if (isdigit((unsigned char) *cursor))
		{
			saw_digit = true;
			digits[ndigits++] = *cursor++;
			if (saw_decimal_point)
				fractional_digits++;
		}
		else if (*cursor == '.' && !saw_decimal_point)
		{
			saw_decimal_point = true;
			cursor++;
		}
		else
			break;
	}

	if (!saw_digit)
		goto invalid_syntax;

	if (*cursor == 'e' || *cursor == 'E')
	{
		cursor++;
		if (*cursor == '+' || *cursor == '-')
		{
			exponent_sign = (*cursor == '-') ? -1 : 1;
			cursor++;
		}
		if (!isdigit((unsigned char) *cursor))
			goto invalid_syntax;
		while (isdigit((unsigned char) *cursor))
		{
			if (exponent > 100000)
				ereport(ERROR,
						(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
						 errmsg("value is out of range for type %s", type_name)));
			exponent = exponent * 10 + (*cursor++ - '0');
		}
		exponent *= exponent_sign;
	}

	while (isspace((unsigned char) *cursor))
		cursor++;
	if (*cursor != '\0')
		goto invalid_syntax;

	source_scale = (int64) fractional_digits - exponent;
	if (!target_scale_is_set)
	{
		if (source_scale < PG_INT32_MIN || source_scale > PG_INT32_MAX)
			ereport(ERROR,
					(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
					 errmsg("value is out of range for type %s", type_name)));
		target_scale = (int32) source_scale;
	}
	*result_scale = target_scale;
	shift = (int64) target_scale - source_scale;

	if (shift < 0)
	{
		int64		drop = -shift;

		keep = (int64) ndigits - drop;
		if (drop <= ndigits)
		{
			int64		first_dropped = Max(keep, 0);

			round_up = digits[first_dropped] >= '5';
		}
		if (keep < 0)
			keep = 0;
	}
	else
		keep = ndigits;

	for (int64 i = 0; i < keep; i++)
	{
		if (!u128_mul_small(&magnitude, 10) ||
			!u128_add_small(&magnitude, digits[i] - '0'))
			ereport(ERROR,
					(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
					 errmsg("value is out of range for type %s", type_name)));
	}
	/* Avoid an exponent-sized loop for zero, and fail large shifts early. */
	if (shift > 0 && u128_is_zero(&magnitude))
		shift = 0;
	else if (shift >= precision)
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("numeric field overflow"),
				 errdetail("A field with precision %d, scale %d must round to an absolute value less than 10^%d.",
						   precision, target_scale,
						   precision - target_scale)));
	for (int64 i = 0; i < shift; i++)
	{
		if (!u128_mul_small(&magnitude, 10))
			ereport(ERROR,
					(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
					 errmsg("value is out of range for type %s", type_name)));
	}
	if (round_up && !u128_add_small(&magnitude, 1))
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("value is out of range for type %s", type_name)));

	if (!u128_is_zero(&magnitude))
	{
		UInt128Parts limit = u128_power_of_ten(precision);

		if (u128_compare(&magnitude, &limit) >= 0)
			ereport(ERROR,
					(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
					 errmsg("numeric field overflow"),
					 errdetail("A field with precision %d, scale %d must round to an absolute value less than 10^%d.",
							   precision, target_scale,
							   precision - target_scale)));
	}

	if (u128_is_zero(&magnitude))
	{
		*negative = false;
		if (!target_scale_is_set)
			*result_scale = 0;
	}
	pfree(digits);
	return magnitude;

invalid_syntax:
	ereport(ERROR,
			(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
			 errmsg("invalid input syntax for type %s: \"%s\"",
					type_name, str)));
	pg_unreachable();
}

static char *
format_decimal(const UInt128Parts *magnitude, bool negative, int32 scale)
{
	char	   *digits = u128_to_cstring(magnitude);
	int			ndigits = strlen(digits);
	int			prefix = negative && !u128_is_zero(magnitude) ? 1 : 0;
	int64		length;
	char	   *result;
	char	   *out;

	if (scale > 0)
		length = prefix + Max(ndigits, scale + 1) + 1;
	else
		length = prefix + ndigits - (int64) scale;
	if (length > MaxAllocSize - 1)
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("decimal output is too large")));

	result = palloc(length + 1);
	out = result;
	if (prefix)
		*out++ = '-';

	if (scale > 0)
	{
		if (ndigits <= scale)
		{
			*out++ = '0';
			*out++ = '.';
			for (int i = 0; i < scale - ndigits; i++)
				*out++ = '0';
			memcpy(out, digits, ndigits);
			out += ndigits;
		}
		else
		{
			int			integer_digits = ndigits - scale;

			memcpy(out, digits, integer_digits);
			out += integer_digits;
			*out++ = '.';
			memcpy(out, digits + integer_digits, scale);
			out += scale;
		}
	}
	else
	{
		memcpy(out, digits, ndigits);
		out += ndigits;
		for (int64 i = 0; i < -(int64) scale; i++)
			*out++ = '0';
	}
	*out = '\0';
	pfree(digits);
	return result;
}

static char *
canonical_decimal_string(char *str)
{
	char	   *dot = strchr(str, '.');

	if (dot)
	{
		char	   *end = str + strlen(str) - 1;

		while (end > dot && *end == '0')
			*end-- = '\0';
		if (end == dot)
			*end = '\0';
	}
	return str;
}

static Decimal64Datum *
decimal64_from_string(const char *str, int32 typmod)
{
	ArrowDecimalTypmod modifier = {DECIMAL64_MAX_PRECISION, 0};
	bool			has_typmod = typmod_is_valid(typmod);
	bool			negative;
	int32			scale;
	UInt128Parts magnitude;
	uint64			hi;
	uint64			lo;
	Decimal64Datum *result;

	if (has_typmod)
		modifier = decode_arrow_decimal_typmod(typmod);
	magnitude = parse_decimal_literal(str, modifier.scale, has_typmod,
									  modifier.precision, &negative, &scale,
									  "decimal64");
	u128_to_words(&magnitude, &hi, &lo);
	if (hi != 0 || lo > (uint64) PG_INT64_MAX)
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("value is out of range for type decimal64")));

	result = palloc0(sizeof(*result));
	result->value = negative ? -(int64) lo : (int64) lo;
	result->scale = scale;
	return result;
}

static Decimal128Datum *
decimal128_from_string(const char *str, int32 typmod)
{
	ArrowDecimalTypmod modifier = {DECIMAL128_MAX_PRECISION, 0};
	bool			has_typmod = typmod_is_valid(typmod);
	bool			negative;
	int32			scale;
	UInt128Parts magnitude;

	if (has_typmod)
		modifier = decode_arrow_decimal_typmod(typmod);
	magnitude = parse_decimal_literal(str, modifier.scale, has_typmod,
									  modifier.precision, &negative, &scale,
									  "decimal128");
	return decimal128_from_magnitude(&magnitude, negative, scale);
}

static char *
decimal64_to_cstring(const Decimal64Datum *value)
{
	uint64			magnitude64 = value->value < 0 ?
		(uint64) (0 - (uint64) value->value) : (uint64) value->value;
	UInt128Parts magnitude = u128_from_words(0, magnitude64);

	return format_decimal(&magnitude, value->value < 0, value->scale);
}

static char *
decimal128_to_cstring(const Decimal128Datum *value)
{
	bool			negative;
	UInt128Parts magnitude = decimal128_magnitude(value, &negative);

	return format_decimal(&magnitude, negative, value->scale);
}

static Numeric
decimal64_to_numeric_internal(const Decimal64Datum *value)
{
	char	   *str = decimal64_to_cstring(value);
	Numeric		result;

	result = DatumGetNumeric(DirectFunctionCall3(numeric_in,
										 CStringGetDatum(str),
										 ObjectIdGetDatum(InvalidOid),
										 Int32GetDatum(-1)));
	pfree(str);
	return result;
}

static Numeric
decimal128_to_numeric_internal(const Decimal128Datum *value)
{
	char	   *str = decimal128_to_cstring(value);
	Numeric		result;

	result = DatumGetNumeric(DirectFunctionCall3(numeric_in,
										 CStringGetDatum(str),
										 ObjectIdGetDatum(InvalidOid),
										 Int32GetDatum(-1)));
	pfree(str);
	return result;
}

static Decimal64Datum *
decimal64_apply_typmod(const Decimal64Datum *value, int32 typmod)
{
	ArrowDecimalTypmod modifier;
	char	   *str;
	Decimal64Datum *result;

	if (!typmod_is_valid(typmod))
	{
		result = palloc(sizeof(*result));
		memcpy(result, value, sizeof(*result));
		return result;
	}
	modifier = decode_arrow_decimal_typmod(typmod);
	if (value->scale == modifier.scale)
	{
		validate_decimal64_precision(value->value, modifier.precision,
									 modifier.scale);
		result = palloc(sizeof(*result));
		memcpy(result, value, sizeof(*result));
		return result;
	}
	str = decimal64_to_cstring(value);
	result = decimal64_from_string(str, typmod);
	pfree(str);
	return result;
}

static Decimal128Datum *
decimal128_apply_typmod(const Decimal128Datum *value, int32 typmod)
{
	ArrowDecimalTypmod modifier;
	char	   *str;
	Decimal128Datum *result;

	if (!typmod_is_valid(typmod))
	{
		result = palloc(sizeof(*result));
		memcpy(result, value, sizeof(*result));
		return result;
	}
	modifier = decode_arrow_decimal_typmod(typmod);
	if (value->scale == modifier.scale)
	{
		validate_decimal128_precision(value, modifier.precision);
		result = palloc(sizeof(*result));
		memcpy(result, value, sizeof(*result));
		return result;
	}
	str = decimal128_to_cstring(value);
	result = decimal128_from_string(str, typmod);
	pfree(str);
	return result;
}

Datum
decimal64_in(PG_FUNCTION_ARGS)
{
	PG_RETURN_DECIMAL64_P(decimal64_from_string(PG_GETARG_CSTRING(0),
											   PG_GETARG_INT32(2)));
}

Datum
decimal64_out(PG_FUNCTION_ARGS)
{
	PG_RETURN_CSTRING(decimal64_to_cstring(PG_GETARG_DECIMAL64_P(0)));
}

Datum
decimal128_in(PG_FUNCTION_ARGS)
{
	PG_RETURN_DECIMAL128_P(decimal128_from_string(PG_GETARG_CSTRING(0),
												 PG_GETARG_INT32(2)));
}

Datum
decimal128_out(PG_FUNCTION_ARGS)
{
	PG_RETURN_CSTRING(decimal128_to_cstring(PG_GETARG_DECIMAL128_P(0)));
}

Datum
decimal64_typmod(PG_FUNCTION_ARGS)
{
	PG_RETURN_DECIMAL64_P(decimal64_apply_typmod(PG_GETARG_DECIMAL64_P(0),
												PG_GETARG_INT32(1)));
}

Datum
decimal128_typmod(PG_FUNCTION_ARGS)
{
	PG_RETURN_DECIMAL128_P(decimal128_apply_typmod(PG_GETARG_DECIMAL128_P(0),
												  PG_GETARG_INT32(1)));
}

static bytea *
decimal64_arrow_payload(const Decimal64Datum *value)
{
	bytea	   *result = palloc(VARHDRSZ + 8);
	uint64		raw = (uint64) value->value;
	unsigned char *bytes = (unsigned char *) VARDATA(result);

	SET_VARSIZE(result, VARHDRSZ + 8);
	for (int i = 0; i < 8; i++)
		bytes[i] = (raw >> (i * 8)) & 0xff;
	return result;
}

static bytea *
decimal128_arrow_payload(const Decimal128Datum *value)
{
	bytea	   *result = palloc(VARHDRSZ + 16);
	uint64		hi = (uint64) value->hi;
	unsigned char *bytes = (unsigned char *) VARDATA(result);

	SET_VARSIZE(result, VARHDRSZ + 16);
	for (int i = 0; i < 8; i++)
	{
		bytes[i] = (value->lo >> (i * 8)) & 0xff;
		bytes[i + 8] = (hi >> (i * 8)) & 0xff;
	}
	return result;
}

Datum
decimal64_arrow_bytes(PG_FUNCTION_ARGS)
{
	PG_RETURN_BYTEA_P(decimal64_arrow_payload(PG_GETARG_DECIMAL64_P(0)));
}

Datum
decimal128_arrow_bytes(PG_FUNCTION_ARGS)
{
	PG_RETURN_BYTEA_P(decimal128_arrow_payload(PG_GETARG_DECIMAL128_P(0)));
}

Datum
decimal64_send(PG_FUNCTION_ARGS)
{
	Decimal64Datum *value = PG_GETARG_DECIMAL64_P(0);
	bytea	   *payload = decimal64_arrow_payload(value);
	StringInfoData buf;

	pq_begintypsend(&buf);
	pq_sendint32(&buf, value->scale);
	pq_sendbytes(&buf, VARDATA(payload), 8);
	pfree(payload);
	PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

Datum
decimal128_send(PG_FUNCTION_ARGS)
{
	Decimal128Datum *value = PG_GETARG_DECIMAL128_P(0);
	bytea	   *payload = decimal128_arrow_payload(value);
	StringInfoData buf;

	pq_begintypsend(&buf);
	pq_sendint32(&buf, value->scale);
	pq_sendbytes(&buf, VARDATA(payload), 16);
	pfree(payload);
	PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

static int64
decode_le_int64(const unsigned char *bytes)
{
	uint64		raw = 0;

	for (int i = 0; i < 8; i++)
		raw |= (uint64) bytes[i] << (i * 8);
	return (int64) raw;
}

static Decimal128Datum *
decode_le_int128(const unsigned char *bytes, int32 scale)
{
	Decimal128Datum *result = palloc0(sizeof(*result));
	uint64			hi = 0;

	for (int i = 0; i < 8; i++)
	{
		result->lo |= (uint64) bytes[i] << (i * 8);
		hi |= (uint64) bytes[i + 8] << (i * 8);
	}
	result->hi = (int64) hi;
	result->scale = scale;
	return result;
}

static void
validate_decimal64_precision(int64 value, int32 precision, int32 scale)
{
	uint64		mag = value < 0 ? (uint64) (0 - (uint64) value) : value;
	UInt128Parts parts = u128_from_words(0, mag);
	UInt128Parts limit = u128_power_of_ten(precision);

	if (u128_compare(&parts, &limit) >= 0)
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("numeric field overflow"),
				 errdetail("A field with precision %d, scale %d must round to an absolute value less than 10^%d.",
						   precision, scale, precision - scale)));
}

static void
validate_decimal128_precision(const Decimal128Datum *value, int32 precision)
{
#if ARROW_DECIMAL_USE_INT128
	/*
	 * Arithmetic results always use the maximum precision.  Avoid converting
	 * them to four 32-bit limbs merely to compare against 10^38.
	 */
	if (precision == DECIMAL128_MAX_PRECISION)
	{
		int128		coefficient = decimal128_get_int128(value);
		int128		limit = (int128) (((uint128)
			UINT64CONST(0x4b3b4ca85a86c47a) << 64) |
			UINT64CONST(0x098a224000000000));

		if (coefficient <= -limit || coefficient >= limit)
			goto overflow;
		return;
	}
#endif

	{
		bool		negative;
		UInt128Parts magnitude = decimal128_magnitude(value, &negative);
		UInt128Parts limit = u128_power_of_ten(precision);

		(void) negative;
		if (u128_compare(&magnitude, &limit) >= 0)
			goto overflow;
		return;
	}

overflow:
	ereport(ERROR,
			(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
			 errmsg("numeric field overflow"),
			 errdetail("A field with precision %d, scale %d must round to an absolute value less than 10^%d.",
					   precision, value->scale,
					   precision - value->scale)));
}

bool
arrow_decimal64_typmod(int32 typmod, int32 *precision, int32 *scale)
{
	ArrowDecimalTypmod modifier;

	if (!typmod_is_valid(typmod))
		return false;
	modifier = decode_arrow_decimal_typmod(typmod);
	if (modifier.precision < 1 ||
		modifier.precision > DECIMAL64_MAX_PRECISION ||
		modifier.scale < -DECIMAL64_MAX_PRECISION ||
		modifier.scale > DECIMAL64_MAX_PRECISION)
		return false;
	*precision = modifier.precision;
	*scale = modifier.scale;
	return true;
}

bool
arrow_decimal128_typmod(int32 typmod, int32 *precision, int32 *scale)
{
	ArrowDecimalTypmod modifier;

	if (!typmod_is_valid(typmod))
		return false;
	modifier = decode_arrow_decimal_typmod(typmod);
	if (modifier.precision < 1 ||
		modifier.precision > DECIMAL128_MAX_PRECISION ||
		modifier.scale < -DECIMAL128_MAX_PRECISION ||
		modifier.scale > DECIMAL128_MAX_PRECISION)
		return false;
	*precision = modifier.precision;
	*scale = modifier.scale;
	return true;
}

void
arrow_decimal64_encode(Datum datum, int32 typmod,
					   unsigned char out[ARROW_DECIMAL64_WIDTH])
{
	Decimal64Datum *value = (Decimal64Datum *) DatumGetPointer(datum);
	int32		precision;
	int32		scale;
	uint64		raw;

	if (!arrow_decimal64_typmod(typmod, &precision, &scale))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("Arrow DECIMAL64 storage requires a precision and scale typmod")));
	if (value->scale != scale)
		ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("DECIMAL64 datum scale %d does not match column scale %d",
						value->scale, scale)));
	validate_decimal64_precision(value->value, precision, scale);
	raw = (uint64) value->value;
	for (int i = 0; i < ARROW_DECIMAL64_WIDTH; i++)
		out[i] = (raw >> (i * 8)) & 0xff;
}

Datum
arrow_decimal64_decode(const unsigned char input[ARROW_DECIMAL64_WIDTH],
					   int32 typmod)
{
	Decimal64Datum *result;
	int32		precision;
	int32		scale;

	if (!arrow_decimal64_typmod(typmod, &precision, &scale))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("Arrow DECIMAL64 storage requires a precision and scale typmod")));
	result = palloc0(sizeof(*result));
	result->value = decode_le_int64(input);
	result->scale = scale;
	validate_decimal64_precision(result->value, precision, scale);
	return PointerGetDatum(result);
}

void
arrow_decimal128_encode(Datum datum, int32 typmod,
						unsigned char out[ARROW_DECIMAL128_WIDTH])
{
	Decimal128Datum *value = (Decimal128Datum *) DatumGetPointer(datum);
	int32		precision;
	int32		scale;
	uint64		hi;

	if (!arrow_decimal128_typmod(typmod, &precision, &scale))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("Arrow DECIMAL128 storage requires a precision and scale typmod")));
	if (value->scale != scale)
		ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("DECIMAL128 datum scale %d does not match column scale %d",
						value->scale, scale)));
	validate_decimal128_precision(value, precision);
	hi = (uint64) value->hi;
	for (int i = 0; i < 8; i++)
	{
		out[i] = (value->lo >> (i * 8)) & 0xff;
		out[i + 8] = (hi >> (i * 8)) & 0xff;
	}
}

Datum
arrow_decimal128_decode(const unsigned char input[ARROW_DECIMAL128_WIDTH],
						int32 typmod)
{
	Decimal128Datum *result;
	int32		precision;
	int32		scale;

	if (!arrow_decimal128_typmod(typmod, &precision, &scale))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("Arrow DECIMAL128 storage requires a precision and scale typmod")));
	result = decode_le_int128(input, scale);
	validate_decimal128_precision(result, precision);
	return PointerGetDatum(result);
}

Datum
decimal64_recv(PG_FUNCTION_ARGS)
{
	StringInfo	buf = (StringInfo) PG_GETARG_POINTER(0);
	int32		typmod = PG_GETARG_INT32(2);
	int32		scale;
	const unsigned char *bytes;
	Decimal64Datum *result;

	scale = pq_getmsgint(buf, 4);
	(void) make_arrow_decimal_typmod(DECIMAL64_MAX_PRECISION, scale,
								 DECIMAL64_MAX_PRECISION, "DECIMAL64");
	bytes = (const unsigned char *) pq_getmsgbytes(buf, 8);
	result = palloc0(sizeof(*result));
	result->value = decode_le_int64(bytes);
	result->scale = scale;
	validate_decimal64_precision(result->value, DECIMAL64_MAX_PRECISION, scale);
	if (typmod_is_valid(typmod))
		result = decimal64_apply_typmod(result, typmod);
	PG_RETURN_DECIMAL64_P(result);
}

Datum
decimal128_recv(PG_FUNCTION_ARGS)
{
	StringInfo	buf = (StringInfo) PG_GETARG_POINTER(0);
	int32		typmod = PG_GETARG_INT32(2);
	int32		scale;
	Decimal128Datum *result;

	scale = pq_getmsgint(buf, 4);
	(void) make_arrow_decimal_typmod(DECIMAL128_MAX_PRECISION, scale,
								 DECIMAL128_MAX_PRECISION, "DECIMAL128");
	result = decode_le_int128((const unsigned char *) pq_getmsgbytes(buf, 16),
								  scale);
	validate_decimal128_precision(result, DECIMAL128_MAX_PRECISION);
	if (typmod_is_valid(typmod))
		result = decimal128_apply_typmod(result, typmod);
	PG_RETURN_DECIMAL128_P(result);
}

Datum
decimal64_from_arrow(PG_FUNCTION_ARGS)
{
	bytea	   *input = PG_GETARG_BYTEA_PP(0);
	int32		precision = PG_GETARG_INT32(1);
	int32		scale = PG_GETARG_INT32(2);
	Decimal64Datum *result;

	(void) make_arrow_decimal_typmod(precision, scale,
								 DECIMAL64_MAX_PRECISION, "DECIMAL64");
	if (VARSIZE_ANY_EXHDR(input) != 8)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("DECIMAL64 Arrow payload must contain exactly 8 bytes")));
	result = palloc0(sizeof(*result));
	result->value = decode_le_int64((unsigned char *) VARDATA_ANY(input));
	result->scale = scale;
	validate_decimal64_precision(result->value, precision, scale);
	PG_RETURN_DECIMAL64_P(result);
}

Datum
decimal128_from_arrow(PG_FUNCTION_ARGS)
{
	bytea	   *input = PG_GETARG_BYTEA_PP(0);
	int32		precision = PG_GETARG_INT32(1);
	int32		scale = PG_GETARG_INT32(2);
	Decimal128Datum *result;

	(void) make_arrow_decimal_typmod(precision, scale,
								 DECIMAL128_MAX_PRECISION, "DECIMAL128");
	if (VARSIZE_ANY_EXHDR(input) != 16)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("DECIMAL128 Arrow payload must contain exactly 16 bytes")));
	result = decode_le_int128((unsigned char *) VARDATA_ANY(input), scale);
	validate_decimal128_precision(result, precision);
	PG_RETURN_DECIMAL128_P(result);
}

/* Compare absolute values with decimal points implied by scale. */
static int
compare_scaled_magnitudes(const UInt128Parts *left, int32 left_scale,
						  const UInt128Parts *right, int32 right_scale)
{
	char	   *left_digits;
	char	   *right_digits;
	int			left_len;
	int			right_len;
	int64		left_decimal_weight;
	int64		right_decimal_weight;
	int			max_len;
	int			result = 0;

	if (u128_is_zero(left) || u128_is_zero(right))
		return u128_compare(left, right);

	left_digits = u128_to_cstring(left);
	right_digits = u128_to_cstring(right);
	left_len = strlen(left_digits);
	right_len = strlen(right_digits);
	left_decimal_weight = (int64) left_len - left_scale;
	right_decimal_weight = (int64) right_len - right_scale;
	if (left_decimal_weight < right_decimal_weight)
		result = -1;
	else if (left_decimal_weight > right_decimal_weight)
		result = 1;
	else
	{
		max_len = Max(left_len, right_len);
		for (int i = 0; i < max_len; i++)
		{
			char		left_digit = i < left_len ? left_digits[i] : '0';
			char		right_digit = i < right_len ? right_digits[i] : '0';

			if (left_digit < right_digit)
			{
				result = -1;
				break;
			}
			if (left_digit > right_digit)
			{
				result = 1;
				break;
			}
		}
	}
	pfree(left_digits);
	pfree(right_digits);
	return result;
}

static int
decimal64_cmp_internal(const Decimal64Datum *left,
					   const Decimal64Datum *right)
{
	bool		left_negative = left->value < 0;
	bool		right_negative = right->value < 0;
	uint64		left_mag64 = left_negative ?
		(uint64) (0 - (uint64) left->value) : (uint64) left->value;
	uint64		right_mag64 = right_negative ?
		(uint64) (0 - (uint64) right->value) : (uint64) right->value;
	UInt128Parts left_mag = u128_from_words(0, left_mag64);
	UInt128Parts right_mag = u128_from_words(0, right_mag64);
	int			result;

	if (left_negative != right_negative)
		return left_negative ? -1 : 1;
	if (left->scale == right->scale)
	{
		if (left->value < right->value)
			return -1;
		if (left->value > right->value)
			return 1;
		return 0;
	}
	result = compare_scaled_magnitudes(&left_mag, left->scale,
									   &right_mag, right->scale);
	return left_negative ? -result : result;
}

static int
decimal128_cmp_internal(const Decimal128Datum *left,
						const Decimal128Datum *right)
{
	bool		left_negative;
	bool		right_negative;
	UInt128Parts left_mag = decimal128_magnitude(left, &left_negative);
	UInt128Parts right_mag = decimal128_magnitude(right, &right_negative);
	int			result;

	if (left_negative != right_negative)
		return left_negative ? -1 : 1;
	if (left->scale == right->scale)
	{
#if ARROW_DECIMAL_USE_INT128
		int128		left_value = decimal128_get_int128(left);
		int128		right_value = decimal128_get_int128(right);

		if (left_value < right_value)
			return -1;
		if (left_value > right_value)
			return 1;
		return 0;
#else
		if (left->hi < right->hi)
			return -1;
		if (left->hi > right->hi)
			return 1;
		if (left->lo < right->lo)
			return -1;
		if (left->lo > right->lo)
			return 1;
		return 0;
#endif
	}
	result = compare_scaled_magnitudes(&left_mag, left->scale,
									   &right_mag, right->scale);
	return left_negative ? -result : result;
}

#define DEFINE_DECIMAL_COMPARISON_FUNCTIONS(prefix, DatumType, getarg, cmpfn) \
	Datum prefix##_cmp(PG_FUNCTION_ARGS) \
	{ \
		DatumType *a = getarg(0); \
		DatumType *b = getarg(1); \
		PG_RETURN_INT32(cmpfn(a, b)); \
	} \
	Datum prefix##_eq(PG_FUNCTION_ARGS) \
	{ \
		PG_RETURN_BOOL(cmpfn(getarg(0), getarg(1)) == 0); \
	} \
	Datum prefix##_ne(PG_FUNCTION_ARGS) \
	{ \
		PG_RETURN_BOOL(cmpfn(getarg(0), getarg(1)) != 0); \
	} \
	Datum prefix##_lt(PG_FUNCTION_ARGS) \
	{ \
		PG_RETURN_BOOL(cmpfn(getarg(0), getarg(1)) < 0); \
	} \
	Datum prefix##_le(PG_FUNCTION_ARGS) \
	{ \
		PG_RETURN_BOOL(cmpfn(getarg(0), getarg(1)) <= 0); \
	} \
	Datum prefix##_gt(PG_FUNCTION_ARGS) \
	{ \
		PG_RETURN_BOOL(cmpfn(getarg(0), getarg(1)) > 0); \
	} \
	Datum prefix##_ge(PG_FUNCTION_ARGS) \
	{ \
		PG_RETURN_BOOL(cmpfn(getarg(0), getarg(1)) >= 0); \
	}

DEFINE_DECIMAL_COMPARISON_FUNCTIONS(decimal64, Decimal64Datum,
									 PG_GETARG_DECIMAL64_P, decimal64_cmp_internal)
DEFINE_DECIMAL_COMPARISON_FUNCTIONS(decimal128, Decimal128Datum,
									 PG_GETARG_DECIMAL128_P, decimal128_cmp_internal)

static Decimal64Datum *
decimal64_rescale(const Decimal64Datum *value, int32 scale)
{
	char	   *str = decimal64_to_cstring(value);
	int32		typmod = make_arrow_decimal_typmod(DECIMAL64_MAX_PRECISION,
											 scale, DECIMAL64_MAX_PRECISION,
											 "DECIMAL64");
	Decimal64Datum *result = decimal64_from_string(str, typmod);

	pfree(str);
	return result;
}

static Decimal128Datum *
decimal128_rescale(const Decimal128Datum *value, int32 scale)
{
#if ARROW_DECIMAL_USE_INT128 && defined(HAVE__BUILTIN_OP_OVERFLOW)
	int128		coefficient = decimal128_get_int128(value);
	int128		result_value;
	int32		delta;
	Decimal128Datum *result;

	(void) make_arrow_decimal_typmod(DECIMAL128_MAX_PRECISION, scale,
									 DECIMAL128_MAX_PRECISION, "DECIMAL128");
	if (pg_sub_s32_overflow(scale, value->scale, &delta))
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("DECIMAL128 scale overflow")));

	result_value = coefficient;
	if (delta > 0)
	{
		for (int32 i = 0; i < delta; i++)
		{
			if (__builtin_mul_overflow(result_value, (int128) 10,
								   &result_value))
				ereport(ERROR,
						(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
						 errmsg("DECIMAL128 rescale overflow")));
		}
	}
	else if (delta < 0)
	{
		int32		drop = -delta;

		/* Every valid coefficient has fewer than 39 decimal digits. */
		if (drop > DECIMAL128_MAX_PRECISION)
			result_value = 0;
		else
		{
			int128		divisor = 1;
			int128		remainder;

			for (int32 i = 0; i < drop; i++)
				divisor *= 10;
			result_value = coefficient / divisor;
			remainder = coefficient % divisor;
			if (remainder < 0)
				remainder = -remainder;
			if (remainder >= divisor / 2)
				result_value += coefficient < 0 ? -1 : 1;
		}
	}

	result = palloc0(sizeof(*result));
	decimal128_set_int128(result, result_value, scale);
	validate_decimal128_precision(result, DECIMAL128_MAX_PRECISION);
	return result;
#else
	char	   *str = decimal128_to_cstring(value);
	int32		typmod = make_arrow_decimal_typmod(DECIMAL128_MAX_PRECISION,
											 scale, DECIMAL128_MAX_PRECISION,
											 "DECIMAL128");
	Decimal128Datum *result = decimal128_from_string(str, typmod);

	pfree(str);
	return result;
#endif
}

static void
decimal64_add_internal(const Decimal64Datum *left,
					   const Decimal64Datum *right, bool subtract,
					   Decimal64Datum *result)
{
	Decimal64Datum *left_scaled = (Decimal64Datum *) left;
	Decimal64Datum *right_scaled = (Decimal64Datum *) right;
	int32		result_scale = Max(left->scale, right->scale);

	if (left->scale != result_scale)
		left_scaled = decimal64_rescale(left, result_scale);
	if (right->scale != result_scale)
		right_scaled = decimal64_rescale(right, result_scale);
	if ((subtract &&
		 pg_sub_s64_overflow(left_scaled->value, right_scaled->value,
							 &result->value)) ||
		(!subtract &&
		 pg_add_s64_overflow(left_scaled->value, right_scaled->value,
							 &result->value)))
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("DECIMAL64 %s overflow",
						subtract ? "subtraction" : "addition")));
	result->scale = result_scale;
	result->reserved = 0;
	validate_decimal64_precision(result->value, DECIMAL64_MAX_PRECISION,
								 result->scale);
	if (left_scaled != left)
		pfree(left_scaled);
	if (right_scaled != right)
		pfree(right_scaled);
}

Datum
decimal64_add(PG_FUNCTION_ARGS)
{
	Decimal64Datum *result = palloc0(sizeof(*result));

	decimal64_add_internal(PG_GETARG_DECIMAL64_P(0),
						   PG_GETARG_DECIMAL64_P(1), false, result);
	PG_RETURN_DECIMAL64_P(result);
}

Datum
decimal64_sub(PG_FUNCTION_ARGS)
{
	Decimal64Datum *result = palloc0(sizeof(*result));

	decimal64_add_internal(PG_GETARG_DECIMAL64_P(0),
						   PG_GETARG_DECIMAL64_P(1), true, result);
	PG_RETURN_DECIMAL64_P(result);
}

Datum
decimal64_mul(PG_FUNCTION_ARGS)
{
	Decimal64Datum *left = PG_GETARG_DECIMAL64_P(0);
	Decimal64Datum *right = PG_GETARG_DECIMAL64_P(1);
	Decimal64Datum *result = palloc0(sizeof(*result));

	if (pg_mul_s64_overflow(left->value, right->value, &result->value))
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("DECIMAL64 multiplication overflow")));
	if (pg_add_s32_overflow(left->scale, right->scale, &result->scale))
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("DECIMAL64 scale overflow")));
	(void) make_arrow_decimal_typmod(DECIMAL64_MAX_PRECISION, result->scale,
								 DECIMAL64_MAX_PRECISION, "DECIMAL64");
	validate_decimal64_precision(result->value, DECIMAL64_MAX_PRECISION,
								 result->scale);
	PG_RETURN_DECIMAL64_P(result);
}

Datum
decimal64_uminus(PG_FUNCTION_ARGS)
{
	Decimal64Datum *value = PG_GETARG_DECIMAL64_P(0);
	Decimal64Datum *result = palloc0(sizeof(*result));

	if (value->value == PG_INT64_MIN)
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("DECIMAL64 negation overflow")));
	result->value = -value->value;
	result->scale = value->scale;
	PG_RETURN_DECIMAL64_P(result);
}

/*
 * Add a Numeric value to a decimal64 aggregate state.  This is the fallback
 * used when scales differ or the fixed-width accumulator overflows.  Numeric
 * keeps the transition state exact, while sum.scale records the scale to use
 * when converting the final value back to decimal64.
 */
static void
decimal64_sum_add_numeric(Decimal64AggState *state, Numeric value,
						  int32 value_scale, MemoryContext agg_context)
{
	MemoryContext old_context;
	Numeric		left;
	Numeric		result;

	old_context = MemoryContextSwitchTo(agg_context);
	left = state->use_numeric ? state->numeric_sum :
		decimal64_to_numeric_internal(&state->sum);
	result = numeric_add_safe(left, value, NULL);
	pfree(left);
	state->numeric_sum = result;
	state->use_numeric = true;
	state->sum.scale = Max(state->sum.scale, value_scale);
	MemoryContextSwitchTo(old_context);
}

static void
decimal64_sum_add(Decimal64AggState *state, const Decimal64Datum *value,
				  MemoryContext agg_context)
{
	int64		result;

	if (!state->use_numeric && state->sum.scale == value->scale &&
		!pg_add_s64_overflow(state->sum.value, value->value, &result))
	{
		state->sum.value = result;
		return;
	}
	else
	{
		MemoryContext old_context;
		Numeric		numeric_value;

		old_context = MemoryContextSwitchTo(agg_context);
		numeric_value = decimal64_to_numeric_internal(value);
		MemoryContextSwitchTo(old_context);
		decimal64_sum_add_numeric(state, numeric_value, value->scale,
								  agg_context);
		pfree(numeric_value);
	}
}

Datum
decimal64_sum_transfn(PG_FUNCTION_ARGS)
{
	MemoryContext agg_context;
	Decimal64AggState *state;
	Decimal64Datum *value;

	if (!AggCheckCallContext(fcinfo, &agg_context))
		elog(ERROR, "decimal64_sum_transfn called in non-aggregate context");
	state = PG_ARGISNULL(0) ? NULL :
		(Decimal64AggState *) PG_GETARG_POINTER(0);
	if (PG_ARGISNULL(1))
	{
		if (state == NULL)
			PG_RETURN_NULL();
		PG_RETURN_POINTER(state);
	}
	value = PG_GETARG_DECIMAL64_P(1);
	if (state == NULL)
	{
		state = MemoryContextAllocZero(agg_context, sizeof(*state));
		memcpy(&state->sum, value, sizeof(state->sum));
	}
	else
		decimal64_sum_add(state, value, agg_context);
	PG_RETURN_POINTER(state);
}

Datum
decimal64_sum_finalfn(PG_FUNCTION_ARGS)
{
	Decimal64AggState *state;
	Decimal64Datum *result;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();
	state = (Decimal64AggState *) PG_GETARG_POINTER(0);
	if (state->use_numeric)
	{
		char	   *str = DatumGetCString(DirectFunctionCall1(numeric_out,
											 NumericGetDatum(state->numeric_sum)));
		int32		typmod = make_arrow_decimal_typmod(DECIMAL64_MAX_PRECISION,
												 state->sum.scale,
												 DECIMAL64_MAX_PRECISION,
												 "decimal64");

		result = decimal64_from_string(str, typmod);
		pfree(str);
	}
	else
	{
		validate_decimal64_precision(state->sum.value,
								 DECIMAL64_MAX_PRECISION,
								 state->sum.scale);
		result = palloc(sizeof(*result));
		memcpy(result, &state->sum, sizeof(*result));
	}
	PG_RETURN_DECIMAL64_P(result);
}

Datum
decimal64_sum_combinefn(PG_FUNCTION_ARGS)
{
	MemoryContext agg_context;
	Decimal64AggState *state1;
	Decimal64AggState *state2;

	if (!AggCheckCallContext(fcinfo, &agg_context))
		elog(ERROR, "decimal64_sum_combinefn called in non-aggregate context");
	state1 = PG_ARGISNULL(0) ? NULL :
		(Decimal64AggState *) PG_GETARG_POINTER(0);
	state2 = PG_ARGISNULL(1) ? NULL :
		(Decimal64AggState *) PG_GETARG_POINTER(1);

	if (state2 == NULL)
	{
		if (state1 == NULL)
			PG_RETURN_NULL();
		PG_RETURN_POINTER(state1);
	}
	if (state1 == NULL)
	{
		MemoryContext old_context = MemoryContextSwitchTo(agg_context);

		state1 = palloc0(sizeof(*state1));
		memcpy(&state1->sum, &state2->sum, sizeof(state1->sum));
		state1->use_numeric = state2->use_numeric;
		if (state2->use_numeric)
			state1->numeric_sum = DatumGetNumericCopy(
				NumericGetDatum(state2->numeric_sum));
		MemoryContextSwitchTo(old_context);
		PG_RETURN_POINTER(state1);
	}
	if (state2->use_numeric)
		decimal64_sum_add_numeric(state1, state2->numeric_sum,
								  state2->sum.scale, agg_context);
	else
		decimal64_sum_add(state1, &state2->sum, agg_context);
	PG_RETURN_POINTER(state1);
}

Datum
decimal64_sum_serializefn(PG_FUNCTION_ARGS)
{
	Decimal64AggState *state;
	StringInfoData buf;

	if (!AggCheckCallContext(fcinfo, NULL))
		elog(ERROR, "decimal64_sum_serializefn called in non-aggregate context");
	state = (Decimal64AggState *) PG_GETARG_POINTER(0);
	pq_begintypsend(&buf);
	pq_sendbyte(&buf, ARROW_DECIMAL_AGG_STATE_VERSION);
	pq_sendbyte(&buf, state->use_numeric ? 1 : 0);
	pq_sendint32(&buf, state->sum.scale);
	if (state->use_numeric)
	{
		char	   *str = DatumGetCString(DirectFunctionCall1(numeric_out,
											 NumericGetDatum(state->numeric_sum)));

		pq_sendstring(&buf, str);
		pfree(str);
	}
	else
		pq_sendint64(&buf, (uint64) state->sum.value);
	PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

Datum
decimal64_sum_deserializefn(PG_FUNCTION_ARGS)
{
	MemoryContext agg_context;
	MemoryContext old_context;
	bytea	   *serialized;
	StringInfoData buf;
	Decimal64AggState *result;
	int			version;
	int			use_numeric;
	int32		scale;

	if (!AggCheckCallContext(fcinfo, &agg_context))
		elog(ERROR, "decimal64_sum_deserializefn called in non-aggregate context");
	serialized = PG_GETARG_BYTEA_PP(0);
	initReadOnlyStringInfo(&buf, VARDATA_ANY(serialized),
						   VARSIZE_ANY_EXHDR(serialized));
	version = pq_getmsgbyte(&buf);
	use_numeric = pq_getmsgbyte(&buf);
	scale = (int32) pq_getmsgint(&buf, 4);
	if (version != ARROW_DECIMAL_AGG_STATE_VERSION || use_numeric > 1)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
				 errmsg("invalid serialized decimal64 aggregate state")));
	(void) make_arrow_decimal_typmod(DECIMAL64_MAX_PRECISION, scale,
								  DECIMAL64_MAX_PRECISION, "decimal64");

	old_context = MemoryContextSwitchTo(agg_context);
	result = palloc0(sizeof(*result));
	result->sum.scale = scale;
	result->use_numeric = (use_numeric != 0);
	if (result->use_numeric)
		result->numeric_sum = DatumGetNumeric(DirectFunctionCall3(numeric_in,
									 CStringGetDatum(pq_getmsgstring(&buf)),
									 ObjectIdGetDatum(InvalidOid),
									 Int32GetDatum(-1)));
	else
		result->sum.value = pq_getmsgint64(&buf);
	MemoryContextSwitchTo(old_context);
	pq_getmsgend(&buf);
	PG_RETURN_POINTER(result);
}

static void
decimal128_add_internal(const Decimal128Datum *left,
						const Decimal128Datum *right, bool subtract,
						Decimal128Datum *result)
{
	Decimal128Datum *left_scaled = (Decimal128Datum *) left;
	Decimal128Datum *right_scaled = (Decimal128Datum *) right;
	int32		result_scale = Max(left->scale, right->scale);
#if ARROW_DECIMAL_USE_INT128 && defined(HAVE__BUILTIN_OP_OVERFLOW)
	int128		left_value;
	int128		right_value;
	int128		result_value;
#else
	uint64		right_lo;
	uint64		right_hi;
	uint64		hi;
	bool		left_negative;
	bool		right_negative;
	bool		result_negative;
#endif

	if (left->scale != result_scale)
		left_scaled = decimal128_rescale(left, result_scale);
	if (right->scale != result_scale)
		right_scaled = decimal128_rescale(right, result_scale);

#if ARROW_DECIMAL_USE_INT128 && defined(HAVE__BUILTIN_OP_OVERFLOW)
	left_value = decimal128_get_int128(left_scaled);
	right_value = decimal128_get_int128(right_scaled);
	if ((subtract &&
		 __builtin_sub_overflow(left_value, right_value, &result_value)) ||
		(!subtract &&
		 __builtin_add_overflow(left_value, right_value, &result_value)))
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("DECIMAL128 %s overflow",
						subtract ? "subtraction" : "addition")));
	decimal128_set_int128(result, result_value, result_scale);
#else
	right_lo = right_scaled->lo;
	right_hi = (uint64) right_scaled->hi;
	if (subtract)
	{
		right_lo = ~right_lo + 1;
		right_hi = ~right_hi + (right_lo == 0);
	}
	result->lo = left_scaled->lo + right_lo;
	hi = (uint64) left_scaled->hi + right_hi +
		(result->lo < left_scaled->lo);
	result->hi = (int64) hi;
	result->scale = result_scale;
	result->reserved = 0;

	left_negative = left_scaled->hi < 0;
	right_negative = subtract ? right_scaled->hi >= 0 : right_scaled->hi < 0;
	result_negative = result->hi < 0;
	if (left_negative == right_negative && left_negative != result_negative)
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("DECIMAL128 %s overflow",
						subtract ? "subtraction" : "addition")));
#endif
	validate_decimal128_precision(result, DECIMAL128_MAX_PRECISION);
	if (left_scaled != left)
		pfree(left_scaled);
	if (right_scaled != right)
		pfree(right_scaled);
}

/* Add equal-scale coefficients, reporting only physical int128 overflow. */
static bool
decimal128_add_same_scale(const Decimal128Datum *left,
						  const Decimal128Datum *right,
						  Decimal128Datum *result)
{
	Assert(left->scale == right->scale);

#if ARROW_DECIMAL_USE_INT128 && defined(HAVE__BUILTIN_OP_OVERFLOW)
	{
		int128		value;

		if (__builtin_add_overflow(decimal128_get_int128(left),
							   decimal128_get_int128(right), &value))
			return true;
		decimal128_set_int128(result, value, left->scale);
	}
#else
	{
		uint64		hi;
		bool		left_negative = left->hi < 0;
		bool		right_negative = right->hi < 0;
		bool		result_negative;

		result->lo = left->lo + right->lo;
		hi = (uint64) left->hi + (uint64) right->hi +
			(result->lo < left->lo);
		result->hi = (int64) hi;
		result->scale = left->scale;
		result->reserved = 0;
		result_negative = result->hi < 0;
		if (left_negative == right_negative &&
			left_negative != result_negative)
			return true;
	}
#endif
	return false;
}

Datum
decimal128_add(PG_FUNCTION_ARGS)
{
	Decimal128Datum *result = palloc0(sizeof(*result));

	decimal128_add_internal(PG_GETARG_DECIMAL128_P(0),
							PG_GETARG_DECIMAL128_P(1), false, result);
	PG_RETURN_DECIMAL128_P(result);
}

Datum
decimal128_sub(PG_FUNCTION_ARGS)
{
	Decimal128Datum *result = palloc0(sizeof(*result));

	decimal128_add_internal(PG_GETARG_DECIMAL128_P(0),
							PG_GETARG_DECIMAL128_P(1), true, result);
	PG_RETURN_DECIMAL128_P(result);
}

Datum
decimal128_mul(PG_FUNCTION_ARGS)
{
	Decimal128Datum *left = PG_GETARG_DECIMAL128_P(0);
	Decimal128Datum *right = PG_GETARG_DECIMAL128_P(1);
	int32		result_scale;
#if ARROW_DECIMAL_USE_INT128 && defined(HAVE__BUILTIN_OP_OVERFLOW)
	int128		left_value = decimal128_get_int128(left);
	int128		right_value = decimal128_get_int128(right);
	int128		product;
	Decimal128Datum *result;

	if (__builtin_mul_overflow(left_value, right_value, &product))
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("DECIMAL128 multiplication overflow")));
	if (pg_add_s32_overflow(left->scale, right->scale, &result_scale))
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("DECIMAL128 scale overflow")));
	(void) make_arrow_decimal_typmod(DECIMAL128_MAX_PRECISION, result_scale,
								 DECIMAL128_MAX_PRECISION, "DECIMAL128");
	result = palloc0(sizeof(*result));
	decimal128_set_int128(result, product, result_scale);
	validate_decimal128_precision(result, DECIMAL128_MAX_PRECISION);
	PG_RETURN_DECIMAL128_P(result);
#else
	bool		left_negative;
	bool		right_negative;
	UInt128Parts left_mag = decimal128_magnitude(left, &left_negative);
	UInt128Parts right_mag = decimal128_magnitude(right, &right_negative);
	uint32		product[8] = {0};
	UInt128Parts magnitude;

	for (int i = 0; i < 4; i++)
	{
		uint64		carry = 0;

		for (int j = 0; j < 4; j++)
		{
			uint64		current = (uint64) left_mag.limb[i] *
				right_mag.limb[j] + product[i + j] + carry;

			product[i + j] = (uint32) current;
			carry = current >> 32;
		}
		for (int k = i + 4; carry != 0 && k < 8; k++)
		{
			uint64		current = (uint64) product[k] + carry;

			product[k] = (uint32) current;
			carry = current >> 32;
		}
	}
	if (product[4] != 0 || product[5] != 0 ||
		product[6] != 0 || product[7] != 0 ||
		(product[3] & 0x80000000U) != 0)
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("DECIMAL128 multiplication overflow")));
	memcpy(magnitude.limb, product, sizeof(magnitude.limb));
	if (pg_add_s32_overflow(left->scale, right->scale, &result_scale))
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("DECIMAL128 scale overflow")));
	(void) make_arrow_decimal_typmod(DECIMAL128_MAX_PRECISION, result_scale,
								 DECIMAL128_MAX_PRECISION, "DECIMAL128");
	{
		Decimal128Datum *result = decimal128_from_magnitude(&magnitude,
														 left_negative != right_negative,
														 result_scale);

		validate_decimal128_precision(result, DECIMAL128_MAX_PRECISION);
		PG_RETURN_DECIMAL128_P(result);
	}
#endif
}

Datum
decimal128_uminus(PG_FUNCTION_ARGS)
{
	Decimal128Datum *value = PG_GETARG_DECIMAL128_P(0);
	Decimal128Datum *result = palloc0(sizeof(*result));
#if ARROW_DECIMAL_USE_INT128
	int128		coefficient;
#else
	uint64			hi;
#endif

	if (value->hi == PG_INT64_MIN && value->lo == 0)
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("DECIMAL128 negation overflow")));
#if ARROW_DECIMAL_USE_INT128
	coefficient = decimal128_get_int128(value);
	decimal128_set_int128(result, -coefficient, value->scale);
#else
	result->lo = ~value->lo + 1;
	hi = ~((uint64) value->hi) + (result->lo == 0);
	result->hi = (int64) hi;
	result->scale = value->scale;
#endif
	PG_RETURN_DECIMAL128_P(result);
}

/* See decimal64_sum_add_numeric(). */
static void
decimal128_sum_add_numeric(Decimal128AggState *state, Numeric value,
						   int32 value_scale, MemoryContext agg_context)
{
	MemoryContext old_context;
	Numeric		left;
	Numeric		result;

	old_context = MemoryContextSwitchTo(agg_context);
	left = state->use_numeric ? state->numeric_sum :
		decimal128_to_numeric_internal(&state->sum);
	result = numeric_add_safe(left, value, NULL);
	pfree(left);
	state->numeric_sum = result;
	state->use_numeric = true;
	state->sum.scale = Max(state->sum.scale, value_scale);
	MemoryContextSwitchTo(old_context);
}

static void
decimal128_sum_add(Decimal128AggState *state, const Decimal128Datum *value,
				   MemoryContext agg_context)
{
	Decimal128Datum result;

	if (!state->use_numeric && state->sum.scale == value->scale &&
		!decimal128_add_same_scale(&state->sum, value, &result))
	{
		state->sum = result;
		return;
	}
	else
	{
		MemoryContext old_context;
		Numeric		numeric_value;

		old_context = MemoryContextSwitchTo(agg_context);
		numeric_value = decimal128_to_numeric_internal(value);
		MemoryContextSwitchTo(old_context);
		decimal128_sum_add_numeric(state, numeric_value, value->scale,
								   agg_context);
		pfree(numeric_value);
	}
}

Datum
decimal128_sum_transfn(PG_FUNCTION_ARGS)
{
	MemoryContext agg_context;
	Decimal128AggState *state;
	Decimal128Datum *value;

	if (!AggCheckCallContext(fcinfo, &agg_context))
		elog(ERROR, "decimal128_sum_transfn called in non-aggregate context");
	state = PG_ARGISNULL(0) ? NULL :
		(Decimal128AggState *) PG_GETARG_POINTER(0);
	if (PG_ARGISNULL(1))
	{
		if (state == NULL)
			PG_RETURN_NULL();
		PG_RETURN_POINTER(state);
	}
	value = PG_GETARG_DECIMAL128_P(1);
	if (state == NULL)
	{
		state = MemoryContextAllocZero(agg_context, sizeof(*state));
		memcpy(&state->sum, value, sizeof(state->sum));
	}
	else
		decimal128_sum_add(state, value, agg_context);
	PG_RETURN_POINTER(state);
}

Datum
decimal128_sum_finalfn(PG_FUNCTION_ARGS)
{
	Decimal128AggState *state;
	Decimal128Datum *result;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();
	state = (Decimal128AggState *) PG_GETARG_POINTER(0);
	if (state->use_numeric)
	{
		char	   *str = DatumGetCString(DirectFunctionCall1(numeric_out,
											 NumericGetDatum(state->numeric_sum)));
		int32		typmod = make_arrow_decimal_typmod(DECIMAL128_MAX_PRECISION,
												 state->sum.scale,
												 DECIMAL128_MAX_PRECISION,
												 "decimal128");

		result = decimal128_from_string(str, typmod);
		pfree(str);
	}
	else
	{
		validate_decimal128_precision(&state->sum,
								  DECIMAL128_MAX_PRECISION);
		result = palloc(sizeof(*result));
		memcpy(result, &state->sum, sizeof(*result));
	}
	PG_RETURN_DECIMAL128_P(result);
}

Datum
decimal128_sum_combinefn(PG_FUNCTION_ARGS)
{
	MemoryContext agg_context;
	Decimal128AggState *state1;
	Decimal128AggState *state2;

	if (!AggCheckCallContext(fcinfo, &agg_context))
		elog(ERROR, "decimal128_sum_combinefn called in non-aggregate context");
	state1 = PG_ARGISNULL(0) ? NULL :
		(Decimal128AggState *) PG_GETARG_POINTER(0);
	state2 = PG_ARGISNULL(1) ? NULL :
		(Decimal128AggState *) PG_GETARG_POINTER(1);

	if (state2 == NULL)
	{
		if (state1 == NULL)
			PG_RETURN_NULL();
		PG_RETURN_POINTER(state1);
	}
	if (state1 == NULL)
	{
		MemoryContext old_context = MemoryContextSwitchTo(agg_context);

		state1 = palloc0(sizeof(*state1));
		memcpy(&state1->sum, &state2->sum, sizeof(state1->sum));
		state1->use_numeric = state2->use_numeric;
		if (state2->use_numeric)
			state1->numeric_sum = DatumGetNumericCopy(
				NumericGetDatum(state2->numeric_sum));
		MemoryContextSwitchTo(old_context);
		PG_RETURN_POINTER(state1);
	}
	if (state2->use_numeric)
		decimal128_sum_add_numeric(state1, state2->numeric_sum,
								   state2->sum.scale, agg_context);
	else
		decimal128_sum_add(state1, &state2->sum, agg_context);
	PG_RETURN_POINTER(state1);
}

Datum
decimal128_sum_serializefn(PG_FUNCTION_ARGS)
{
	Decimal128AggState *state;
	StringInfoData buf;

	if (!AggCheckCallContext(fcinfo, NULL))
		elog(ERROR, "decimal128_sum_serializefn called in non-aggregate context");
	state = (Decimal128AggState *) PG_GETARG_POINTER(0);
	pq_begintypsend(&buf);
	pq_sendbyte(&buf, ARROW_DECIMAL_AGG_STATE_VERSION);
	pq_sendbyte(&buf, state->use_numeric ? 1 : 0);
	pq_sendint32(&buf, state->sum.scale);
	if (state->use_numeric)
	{
		char	   *str = DatumGetCString(DirectFunctionCall1(numeric_out,
											 NumericGetDatum(state->numeric_sum)));

		pq_sendstring(&buf, str);
		pfree(str);
	}
	else
	{
		pq_sendint64(&buf, (uint64) state->sum.hi);
		pq_sendint64(&buf, state->sum.lo);
	}
	PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

Datum
decimal128_sum_deserializefn(PG_FUNCTION_ARGS)
{
	MemoryContext agg_context;
	MemoryContext old_context;
	bytea	   *serialized;
	StringInfoData buf;
	Decimal128AggState *result;
	int			version;
	int			use_numeric;
	int32		scale;

	if (!AggCheckCallContext(fcinfo, &agg_context))
		elog(ERROR, "decimal128_sum_deserializefn called in non-aggregate context");
	serialized = PG_GETARG_BYTEA_PP(0);
	initReadOnlyStringInfo(&buf, VARDATA_ANY(serialized),
						   VARSIZE_ANY_EXHDR(serialized));
	version = pq_getmsgbyte(&buf);
	use_numeric = pq_getmsgbyte(&buf);
	scale = (int32) pq_getmsgint(&buf, 4);
	if (version != ARROW_DECIMAL_AGG_STATE_VERSION || use_numeric > 1)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
				 errmsg("invalid serialized decimal128 aggregate state")));
	(void) make_arrow_decimal_typmod(DECIMAL128_MAX_PRECISION, scale,
								  DECIMAL128_MAX_PRECISION, "decimal128");

	old_context = MemoryContextSwitchTo(agg_context);
	result = palloc0(sizeof(*result));
	result->sum.scale = scale;
	result->use_numeric = (use_numeric != 0);
	if (result->use_numeric)
		result->numeric_sum = DatumGetNumeric(DirectFunctionCall3(numeric_in,
									 CStringGetDatum(pq_getmsgstring(&buf)),
									 ObjectIdGetDatum(InvalidOid),
									 Int32GetDatum(-1)));
	else
	{
		result->sum.hi = pq_getmsgint64(&buf);
		result->sum.lo = (uint64) pq_getmsgint64(&buf);
	}
	MemoryContextSwitchTo(old_context);
	pq_getmsgend(&buf);
	PG_RETURN_POINTER(result);
}

Datum
decimal64_hash(PG_FUNCTION_ARGS)
{
	char	   *str = canonical_decimal_string(
		decimal64_to_cstring(PG_GETARG_DECIMAL64_P(0)));
	uint32		result = hash_bytes((unsigned char *) str, strlen(str));

	pfree(str);
	PG_RETURN_UINT32(result);
}

Datum
decimal64_hash_extended(PG_FUNCTION_ARGS)
{
	char	   *str = canonical_decimal_string(
		decimal64_to_cstring(PG_GETARG_DECIMAL64_P(0)));
	uint64		result = hash_bytes_extended((unsigned char *) str, strlen(str),
										PG_GETARG_INT64(1));

	pfree(str);
	PG_RETURN_UINT64(result);
}

Datum
decimal128_hash(PG_FUNCTION_ARGS)
{
	char	   *str = canonical_decimal_string(
		decimal128_to_cstring(PG_GETARG_DECIMAL128_P(0)));
	uint32		result = hash_bytes((unsigned char *) str, strlen(str));

	pfree(str);
	PG_RETURN_UINT32(result);
}

Datum
decimal128_hash_extended(PG_FUNCTION_ARGS)
{
	char	   *str = canonical_decimal_string(
		decimal128_to_cstring(PG_GETARG_DECIMAL128_P(0)));
	uint64		result = hash_bytes_extended((unsigned char *) str, strlen(str),
										PG_GETARG_INT64(1));

	pfree(str);
	PG_RETURN_UINT64(result);
}

Datum
decimal64_to_numeric(PG_FUNCTION_ARGS)
{
	char	   *str = decimal64_to_cstring(PG_GETARG_DECIMAL64_P(0));
	Datum		result;

	result = DirectFunctionCall3(numeric_in,
							 CStringGetDatum(str),
							 ObjectIdGetDatum(InvalidOid),
							 Int32GetDatum(-1));
	pfree(str);
	PG_RETURN_DATUM(result);
}

Datum
numeric_to_decimal64(PG_FUNCTION_ARGS)
{
	char	   *str = DatumGetCString(DirectFunctionCall1(numeric_out,
												 PG_GETARG_DATUM(0)));
	Decimal64Datum *result = decimal64_from_string(str, PG_GETARG_INT32(1));

	pfree(str);
	PG_RETURN_DECIMAL64_P(result);
}

Datum
decimal128_to_numeric(PG_FUNCTION_ARGS)
{
	char	   *str = decimal128_to_cstring(PG_GETARG_DECIMAL128_P(0));
	Datum		result;

	result = DirectFunctionCall3(numeric_in,
							 CStringGetDatum(str),
							 ObjectIdGetDatum(InvalidOid),
							 Int32GetDatum(-1));
	pfree(str);
	PG_RETURN_DATUM(result);
}

Datum
numeric_to_decimal128(PG_FUNCTION_ARGS)
{
	char	   *str = DatumGetCString(DirectFunctionCall1(numeric_out,
												 PG_GETARG_DATUM(0)));
	Decimal128Datum *result = decimal128_from_string(str, PG_GETARG_INT32(1));

	pfree(str);
	PG_RETURN_DECIMAL128_P(result);
}

Datum
decimal64_to_decimal128(PG_FUNCTION_ARGS)
{
	char	   *str = decimal64_to_cstring(PG_GETARG_DECIMAL64_P(0));
	Decimal128Datum *result = decimal128_from_string(str, PG_GETARG_INT32(1));

	pfree(str);
	PG_RETURN_DECIMAL128_P(result);
}

Datum
decimal128_to_decimal64(PG_FUNCTION_ARGS)
{
	char	   *str = decimal128_to_cstring(PG_GETARG_DECIMAL128_P(0));
	Decimal64Datum *result = decimal64_from_string(str, PG_GETARG_INT32(1));

	pfree(str);
	PG_RETURN_DECIMAL64_P(result);
}
