/*-------------------------------------------------------------------------
 *
 * pg_copy_handler.h
 *	  definition of the "copy handler" system catalog (pg_copy_handler)
 *
 *
 * Portions Copyright (c) 1996-2023, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/catalog/pg_copy_handler.h
 *
 * NOTES
 *	  The Catalog.pm module reads this file and derives schema
 *	  information.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_COPY_HANDLER_H
#define PG_COPY_HANDLER_H

#include "catalog/genbki.h"
#include "catalog/pg_copy_handler_d.h"

/* ----------------
 *		pg_copy_handler definition.  cpp turns this into
 *		typedef struct FormData_pg_copy_handler
 * ----------------
 */
CATALOG(pg_copy_handler,4551,CopyHandlerRelationId)
{
	Oid			oid;			/* oid */

	/* copy handler name */
	NameData	chname;

	/* handler function */
	regproc		copyhandler BKI_LOOKUP(pg_proc);
} FormData_pg_copy_handler;

/* ----------------
 *		Form_pg_copy_handler corresponds to a pointer to a tuple with
 *		the format of pg_copy_handler relation.
 * ----------------
 */
typedef FormData_pg_copy_handler *Form_pg_copy_handler;

DECLARE_UNIQUE_INDEX(pg_copy_handler_name_index, 4552, CopyHandlerNameIndexId, pg_copy_handler, btree(chname name_ops));
DECLARE_UNIQUE_INDEX_PKEY(pg_copy_handler_oid_index, 4553, CopyHandlerOidIndexId, pg_copy_handler, btree(oid oid_ops));

#endif							/* PG_COPY_HANDLER_H */
