/*--------------------------------------------------------------------------
 *
 * test_copy_callbacks.c
 *		Code for testing COPY callbacks.
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *		src/test/modules/test_copy_callbacks/test_copy_callbacks.c
 *
 * -------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/table.h"
#include "commands/copy.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "parser/parse_relation.h"
#include "utils/rel.h"
#include "varatt.h"

PG_MODULE_MAGIC;

typedef struct TestCopyFromJsonState
{
	const char *data;
	int			len;
	int			pos;
	int			chunk_size;
	bool		cancel;
} TestCopyFromJsonState;

static TestCopyFromJsonState *from_json_state;

static int
from_json_cb(void *data, int minread, int maxread)
{
	TestCopyFromJsonState *state = from_json_state;
	int			nbytes = Min(state->len - state->pos,
							 Min(state->chunk_size, maxread));

	if (state->cancel)
	{
		/* Request cancellation during the first read, without a timing race. */
		if (state->pos == 0)
		{
			InterruptPending = true;
			QueryCancelPending = true;
		}
		else
			elog(ERROR, "COPY did not process cancellation before reading more data");
	}

	memcpy(data, state->data + state->pos, nbytes);
	state->pos += nbytes;
	return nbytes;
}

PG_FUNCTION_INFO_V1(test_copy_from_json_callback);
Datum
test_copy_from_json_callback(PG_FUNCTION_ARGS)
{
	Relation	rel = table_open(PG_GETARG_OID(0), RowExclusiveLock);
	ParseState *pstate = make_parsestate(NULL);
	text	   *data = PG_GETARG_TEXT_PP(1);
	TestCopyFromJsonState state;
	TestCopyFromJsonState *saved_state = from_json_state;
	volatile uint64 processed = 0;

	state.data = VARDATA_ANY(data);
	state.len = VARSIZE_ANY_EXHDR(data);
	state.pos = 0;
	state.chunk_size = PG_GETARG_INT32(2);
	state.cancel = PG_GETARG_BOOL(3);
	if (state.chunk_size <= 0)
		elog(ERROR, "chunk size must be positive");

	addRangeTableEntryForRelation(pstate, rel, RowExclusiveLock,
								  NULL, false, false);

	from_json_state = &state;
	PG_TRY();
	{
		CopyFromState cstate;
		List	   *options = list_make1(makeDefElem("format",
													 (Node *) makeString("json"), -1));

		cstate = BeginCopyFrom(pstate, rel, NULL, NULL, false,
							   from_json_cb, NIL, options);
		processed = CopyFrom(cstate);
		EndCopyFrom(cstate);
	}
	PG_FINALLY();
	{
		from_json_state = saved_state;
	}
	PG_END_TRY();

	free_parsestate(pstate);
	table_close(rel, NoLock);
	PG_RETURN_INT64(processed);
}

static void
to_cb(void *data, int len)
{
	ereport(NOTICE,
			(errmsg("COPY TO callback called with data \"%s\" and length %d",
					(char *) data, len)));
}

PG_FUNCTION_INFO_V1(test_copy_to_callback);
Datum
test_copy_to_callback(PG_FUNCTION_ARGS)
{
	Relation	rel = table_open(PG_GETARG_OID(0), AccessShareLock);
	CopyToState cstate;
	int64		processed;

	cstate = BeginCopyTo(NULL, rel, NULL, RelationGetRelid(rel), NULL, false,
						 to_cb, NIL, NIL);
	processed = DoCopyTo(cstate);
	EndCopyTo(cstate);

	ereport(NOTICE, (errmsg("COPY TO callback has processed %" PRId64 " rows",
							processed)));

	table_close(rel, NoLock);

	PG_RETURN_VOID();
}
