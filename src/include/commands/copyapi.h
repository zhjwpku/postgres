/*-------------------------------------------------------------------------
 *
 * copyapi.h
 *	  API for COPY TO/FROM handlers
 *
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/commands/copyapi.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef COPYAPI_H
#define COPYAPI_H

#include "executor/execdesc.h"
#include "executor/tuptable.h"
#include "nodes/parsenodes.h"

typedef struct CopyToStateData *CopyToState;

typedef bool (*CopyToProcessOption_function) (CopyToState cstate, DefElem *defel);
typedef int16 (*CopyToGetFormat_function) (CopyToState cstate);
typedef void (*CopyToStart_function) (CopyToState cstate, TupleDesc tupDesc);
typedef void (*CopyToOneRow_function) (CopyToState cstate, TupleTableSlot *slot);
typedef void (*CopyToEnd_function) (CopyToState cstate);

/* Routines for a COPY TO format implementation. */
typedef struct CopyToRoutine
{
	NodeTag		type;

	/*
	 * Called for processing one COPY TO option. This will return false when
	 * the given option is invalid.
	 */
	CopyToProcessOption_function CopyToProcessOption;

	/*
	 * Called when COPY TO is started. This will return a format as int16
	 * value. It's used for the CopyOutResponse message.
	 */
	CopyToGetFormat_function CopyToGetFormat;

	/* Called when COPY TO is started. This will send a header. */
	CopyToStart_function CopyToStart;

	/* Copy one row for COPY TO. */
	CopyToOneRow_function CopyToOneRow;

	/* Called when COPY TO is ended. This will send a trailer. */
	CopyToEnd_function CopyToEnd;
}			CopyToRoutine;

/* Built-in CopyToRoutine for "text", "csv" and "binary". */
extern CopyToRoutine CopyToRoutineText;
extern CopyToRoutine CopyToRoutineCSV;
extern CopyToRoutine CopyToRoutineBinary;

/*
 * Represents whether a header line should be present, and whether it must
 * match the actual names (which implies "true").
 */
typedef enum CopyHeaderChoice
{
	COPY_HEADER_FALSE = 0,
	COPY_HEADER_TRUE,
	COPY_HEADER_MATCH,
} CopyHeaderChoice;

/*
 * Represents where to save input processing errors.  More values to be added
 * in the future.
 */
typedef enum CopyOnErrorChoice
{
	COPY_ON_ERROR_STOP = 0,		/* immediately throw errors, default */
	COPY_ON_ERROR_IGNORE,		/* ignore errors */
} CopyOnErrorChoice;

/*
 * A struct to hold COPY options, in a parsed form. All of these are related
 * to formatting, except for 'freeze', which doesn't really belong here, but
 * it's expedient to parse it along with all the other options.
 */
typedef struct CopyFormatOptions
{
	/* parameters from the COPY command */
	int			file_encoding;	/* file or remote side's character encoding,
								 * -1 if not specified */
	bool		binary;			/* binary format? */
	bool		freeze;			/* freeze rows on loading? */
	bool		csv_mode;		/* Comma Separated Value format? */
	CopyHeaderChoice header_line;	/* header line? */
	char	   *null_print;		/* NULL marker string (server encoding!) */
	int			null_print_len; /* length of same */
	char	   *null_print_client;	/* same converted to file encoding */
	char	   *default_print;	/* DEFAULT marker string */
	int			default_print_len;	/* length of same */
	char	   *delim;			/* column delimiter (must be 1 byte) */
	char	   *quote;			/* CSV quote char (must be 1 byte) */
	char	   *escape;			/* CSV escape char (must be 1 byte) */
	List	   *force_quote;	/* list of column names */
	bool		force_quote_all;	/* FORCE_QUOTE *? */
	bool	   *force_quote_flags;	/* per-column CSV FQ flags */
	List	   *force_notnull;	/* list of column names */
	bool		force_notnull_all;	/* FORCE_NOT_NULL *? */
	bool	   *force_notnull_flags;	/* per-column CSV FNN flags */
	List	   *force_null;		/* list of column names */
	bool		force_null_all; /* FORCE_NULL *? */
	bool	   *force_null_flags;	/* per-column CSV FN flags */
	bool		convert_selectively;	/* do selective binary conversion? */
	CopyOnErrorChoice on_error; /* what to do when error happened */
	List	   *convert_select; /* list of column names (can be NIL) */
	CopyToRoutine *to_routine;	/* callback routines for COPY TO */
} CopyFormatOptions;

/*
 * Represents the different dest cases we need to worry about at
 * the bottom level
 */
typedef enum CopyDest
{
	COPY_DEST_FILE,				/* to file (or a piped program) */
	COPY_DEST_FRONTEND,			/* to frontend */
	COPY_DEST_CALLBACK,			/* to callback function */
} CopyDest;

typedef void (*copy_data_dest_cb) (void *data, int len);

/*
 * This struct contains all the state variables used throughout a COPY TO
 * operation.
 *
 * Multi-byte encodings: all supported client-side encodings encode multi-byte
 * characters by having the first byte's high bit set. Subsequent bytes of the
 * character can have the high bit not set. When scanning data in such an
 * encoding to look for a match to a single-byte (ie ASCII) character, we must
 * use the full pg_encoding_mblen() machinery to skip over multibyte
 * characters, else we might find a false match to a trailing byte. In
 * supported server encodings, there is no possibility of a false match, and
 * it's faster to make useless comparisons to trailing bytes than it is to
 * invoke pg_encoding_mblen() to skip over them. encoding_embeds_ascii is true
 * when we have to do it the hard way.
 */
typedef struct CopyToStateData
{
	/* low-level state data */
	CopyDest	copy_dest;		/* type of copy source/destination */
	FILE	   *copy_file;		/* used if copy_dest == COPY_FILE */
	StringInfo	fe_msgbuf;		/* used for all dests during COPY TO */

	int			file_encoding;	/* file or remote side's character encoding */
	bool		need_transcoding;	/* file encoding diff from server? */
	bool		encoding_embeds_ascii;	/* ASCII can be non-first byte? */

	/* parameters from the COPY command */
	Relation	rel;			/* relation to copy to */
	QueryDesc  *queryDesc;		/* executable query to copy from */
	List	   *attnumlist;		/* integer list of attnums to copy */
	char	   *filename;		/* filename, or NULL for STDOUT */
	bool		is_program;		/* is 'filename' a program to popen? */
	copy_data_dest_cb data_dest_cb; /* function for writing data */

	CopyFormatOptions opts;
	Node	   *whereClause;	/* WHERE condition (or NULL) */

	/*
	 * Working state
	 */
	MemoryContext copycontext;	/* per-copy execution context */

	FmgrInfo   *out_functions;	/* lookup info for output functions */
	MemoryContext rowcontext;	/* per-row evaluation context */
	uint64		bytes_processed;	/* number of bytes processed so far */
} CopyToStateData;

#endif							/* COPYAPI_H */
