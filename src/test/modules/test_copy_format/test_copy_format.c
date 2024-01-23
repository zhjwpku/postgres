/*--------------------------------------------------------------------------
 *
 * test_copy_format.c
 *		Code for testing custom COPY format.
 *
 * Portions Copyright (c) 2024, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		src/test/modules/test_copy_format/test_copy_format.c
 *
 * -------------------------------------------------------------------------
 */

#include "postgres.h"

#include "commands/copy.h"
#include "commands/defrem.h"

PG_MODULE_MAGIC;

static bool
CopyToProcessOption(CopyToState cstate, DefElem *defel)
{
	ereport(NOTICE,
			(errmsg("CopyToProcessOption: \"%s\"=\"%s\"",
					defel->defname, defGetString(defel))));
	return true;
}

static int16
CopyToGetFormat(CopyToState cstate)
{
	ereport(NOTICE, (errmsg("CopyToGetFormat")));
	return 0;
}

static void
CopyToStart(CopyToState cstate, TupleDesc tupDesc)
{
	ereport(NOTICE, (errmsg("CopyToStart: natts=%d", tupDesc->natts)));
}

static void
CopyToOneRow(CopyToState cstate, TupleTableSlot *slot)
{
	ereport(NOTICE, (errmsg("CopyToOneRow: tts_nvalid=%u", slot->tts_nvalid)));
}

static void
CopyToEnd(CopyToState cstate)
{
	ereport(NOTICE, (errmsg("CopyToEnd")));
}

static const CopyToRoutine CopyToRoutineTestCopyFormat = {
	.type = T_CopyToRoutine,
	.CopyToProcessOption = CopyToProcessOption,
	.CopyToGetFormat = CopyToGetFormat,
	.CopyToStart = CopyToStart,
	.CopyToOneRow = CopyToOneRow,
	.CopyToEnd = CopyToEnd,
};

PG_FUNCTION_INFO_V1(test_copy_format);
Datum
test_copy_format(PG_FUNCTION_ARGS)
{
	bool		is_from = PG_GETARG_BOOL(0);

	ereport(NOTICE,
			(errmsg("test_copy_format: is_from=%s", is_from ? "true" : "false")));

	if (is_from)
		elog(ERROR, "COPY FROM isn't supported yet");

	PG_RETURN_POINTER(&CopyToRoutineTestCopyFormat);
}
