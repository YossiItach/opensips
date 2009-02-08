/* 
 * $Id$ 
 *
 * Various URI related functions
 *
 * Copyright (C) 2001-2003 FhG Fokus
 *
 * This file is part of opensips, a free SIP server.
 *
 * opensips is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * opensips is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * History:
 * -------
 *  2003-03-11: New module interface (janakj)
 *  2003-03-16: Flags export parameter added (janakj)
 *  2003-03-19: Replaces all mallocs/frees w/ pkg_malloc/pkg_free (andrei)
 *  2003-04-05: default_uri #define used (jiri)
 *  2004-03-20: has_totag introduced (jiri)
 *  2004-06-07: Updated to the new DB api (andrei)
 *  2008-11-07: Added statistics to module: positive_checks and negative_checks (saguti)
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../sr_module.h"
#include "../../dprint.h"
#include "../../ut.h"
#include "../../error.h"
#include "../../mem/mem.h"
#include "uridb_mod.h"
#include "checks.h"

MODULE_VERSION

/*
 * Version of domain table required by the module,
 * increment this value if you change the table in
 * an backwards incompatible way
 */
#define URI_TABLE_VERSION 2
#define SUBSCRIBER_TABLE_VERSION 7

static void destroy(void);       /* Module destroy function */
static int child_init(int rank); /* Per-child initialization function */
static int mod_init(void);       /* Module initialization function */


#define URI_TABLE "uri"

#define SUBSCRIBER_TABLE "subscriber"

#define USER_COL "username"
#define USER_COL_LEN (sizeof(USER_COL) - 1)

#define DOMAIN_COL "domain"
#define DOMAIN_COL_LEN (sizeof(DOMAIN_COL) - 1)

#define URI_USER_COL "uri_user"
#define URI_USER_COL_LEN (sizeof(URI_USER_COL) - 1)



/*
 * Module parameter variables
 */
static str db_url         = {DEFAULT_RODB_URL, DEFAULT_RODB_URL_LEN};
str db_table              = {NULL,0};
str uridb_user_col        = {USER_COL, USER_COL_LEN};
str uridb_domain_col      = {DOMAIN_COL, DOMAIN_COL_LEN};
str uridb_uriuser_col     = {URI_USER_COL, URI_USER_COL_LEN};

int use_uri_table = 0;     /* Should uri table be used */
int use_domain = 0;        /* Should does_uri_exist honor the domain part ? */

static int fixup_exist(void** param, int param_no);

/*
 * Exported functions
 */
static cmd_export_t cmds[] = {
	{"check_to",       (cmd_function)check_to,       0, 0, 0,
		REQUEST_ROUTE},
	{"check_from",     (cmd_function)check_from,     0, 0, 0,
		REQUEST_ROUTE},
	{"does_uri_exist", (cmd_function)does_uri_exist, 0, 0, fixup_exist,
		REQUEST_ROUTE|LOCAL_ROUTE},
	{0, 0, 0, 0, 0, 0}
};


/*
 * Exported parameters
 */
static param_export_t params[] = {
	{"db_url",                   STR_PARAM, &db_url.s               },
	{"db_table",                 STR_PARAM, &db_table.s             },
	{"user_column",              STR_PARAM, &uridb_user_col.s       },
	{"domain_column",            STR_PARAM, &uridb_domain_col.s     },
	{"uriuser_column",           STR_PARAM, &uridb_uriuser_col.s    },
	{"use_uri_table",            INT_PARAM, &use_uri_table          },
	{"use_domain",               INT_PARAM, &use_domain             },
	{0, 0, 0}
};

/*
 *  * Module statistics
 *   */

stat_export_t uridb_stats[] = {
        {"positive checks" ,  0,  &positive_checks  },
        {"negative_checks"  ,  0,  &negative_checks  },
        {0,0,0}
};


/*
 * Module interface
 */
struct module_exports exports = {
	"uri_db",
	DEFAULT_DLFLAGS, /* dlopen flags */
	cmds,      /* Exported functions */
	params,    /* Exported parameters */
	uridb_stats,         /* exported statistics */
	0 ,        /* exported MI functions */
	0,         /* exported pseudo-variables */
	0,         /* extra processes */
	mod_init,  /* module initialization function */
	0,         /* response function */
	destroy,   /* destroy function */
	child_init /* child initialization function */
};


/**
 * Module initialization function callee in each child separately
 */
static int child_init(int rank)
{
	if (db_url.len != 0)
		return uridb_db_init(&db_url);
	else
		return 0;
}


/**
 * Module initialization function that is called before the main process forks
 */
static int mod_init(void)
{
	int checkver=-1;
	db_func_t db_funcs;
	db_con_t *db_conn = NULL;

	

	LM_DBG("uri_db - initializing\n");

	db_url.len = strlen(db_url.s);
	if (db_url.len == 0) {
		if (use_uri_table != 0) {
			LM_ERR("configuration error - no database URL, "
				"but use_uri_table is set!\n");
			goto error;
		}
		return 0;
	}

	if (db_table.s == NULL) {
		/* no table set -> use defaults */
		if (use_uri_table != 0){
			db_table.s = URI_TABLE;
			db_table.len = strlen(URI_TABLE)+1;
		}
		else {
			db_table.s = SUBSCRIBER_TABLE;
			db_table.len = strlen(SUBSCRIBER_TABLE)+1;
		}
	}

	db_table.len = strlen(db_table.s);
	uridb_user_col.len = strlen(uridb_user_col.s);
	uridb_domain_col.len = strlen(uridb_domain_col.s);
	uridb_uriuser_col.len = strlen(uridb_uriuser_col.s);

	if ( db_bind_mod(&db_url, &db_funcs) != 0 ) {
		LM_ERR("No database module found\n");
		goto error;
	}

	db_conn = db_funcs.init(&db_url);
	if( db_conn == NULL ) {
		LM_ERR("Could not connect to database\n");
		return -1;
	}

	checkver = db_check_table_version( &db_funcs, db_conn, &db_table,
		use_uri_table?URI_TABLE_VERSION:SUBSCRIBER_TABLE_VERSION );

	/** If checkver == -1, table validation failed */
	if( checkver == -1 ) {
		LM_ERR("Invalid table version.\n");
		db_funcs.close(db_conn);
		return -1;
	}

	db_funcs.close(db_conn);

	/* done with checkings - init the working connection */
	if (uridb_db_bind(&db_url)!=0) {
		LM_ERR("Failed to bind to a DB module\n");
		return -1;
	}

	return 0;
error:
	return -1;
}


static void destroy(void)
{
	uridb_db_close();
}


static int fixup_exist(void** param, int param_no)
{
	if (db_url.len == 0) {
		LM_ERR("configuration error - does_uri_exist() called with no database URL!\n");
		return E_CFG;
	}
	return 0;
}
