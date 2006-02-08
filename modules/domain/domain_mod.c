/*
 * $Id$
 *
 * Domain module
 *
 * Copyright (C) 2002-2003 Juha Heinanen
 *
 * This file is part of openser, a free SIP server.
 *
 * openser is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * openser is distributed in the hope that it will be useful,
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
 * 2003-03-11: New module interface (janakj)
 * 2003-03-16: flags export parameter added (janakj)
 * 2003-04-05: default_uri #define used (jiri)
 * 2003-04-06: db connection closed in mod_init (janakj)
 * 2004-06-06: updated to the new DB api, cleanup: static dbf & handler,
 *             calls to domain_db_{bind,init,close,ver} (andrei)
 * 2006-01-22: added is_domain_local(variable) function (dan)
 *
 */


#include "domain_mod.h"
#include <stdio.h>
#include "../../mem/shm_mem.h"
#include "../../mem/mem.h"
#include "../../sr_module.h"
#include "domain.h"
#include "fifo.h"
#include "unixsock.h"

/*
 * Module management function prototypes
 */
static int mod_init(void);
static void destroy(void);
static int child_init(int rank);
static int fixup_avp(void** param, int param_no);

MODULE_VERSION

/*
 * Version of domain table required by the module,
 * increment this value if you change the table in
 * an backwards incompatible way
 */
#define TABLE_VERSION 1

#define DOMAIN_TABLE "domain"
#define DOMAIN_TABLE_LEN (sizeof(DOMAIN_TABLE) - 1)

#define DOMAIN_COL "domain"
#define DOMAIN_COL_LEN (sizeof(DOMAIN_COL) - 1)

/*
 * Module parameter variables
 */
static str db_url = {DEFAULT_RODB_URL, DEFAULT_RODB_URL_LEN};
int db_mode = 0;			/* Database usage mode: 0 = no cache, 1 = cache */
str domain_table = {DOMAIN_TABLE, DOMAIN_TABLE_LEN};     /* Name of domain table */
str domain_col = {DOMAIN_COL, DOMAIN_COL_LEN};           /* Name of domain column */

/*
 * Other module variables
 */
struct domain_list ***hash_table;	/* Pointer to current hash table pointer */
struct domain_list **hash_table_1;	/* Pointer to hash table 1 */
struct domain_list **hash_table_2;	/* Pointer to hash table 2 */


/*
 * Exported functions
 */
static cmd_export_t cmds[] = {
	{"is_from_local",     is_from_local,     0, 0, REQUEST_ROUTE},
	{"is_uri_host_local", is_uri_host_local, 0, 0, REQUEST_ROUTE|BRANCH_ROUTE|FAILURE_ROUTE},
	{"is_domain_local",   w_is_domain_local, 1, fixup_avp, REQUEST_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE},
	{0, 0, 0, 0, 0}
};


/*
 * Exported parameters
 */
static param_export_t params[] = {
	{"db_url",         STR_PARAM, &db_url.s      },
	{"db_mode",        INT_PARAM, &db_mode       },
	{"domain_table",   STR_PARAM, &domain_table.s},
	{"domain_col",     STR_PARAM, &domain_col.s  },
	{0, 0, 0}
};


/*
 * Module interface
 */
struct module_exports exports = {
	"domain", 
	cmds,      /* Exported functions */
	params,    /* Exported parameters */
	0,         /* exported statistics */
	mod_init,  /* module initialization function */
	0,         /* response function*/
	destroy,   /* destroy function */
	child_init /* per-child init function */
};


static int mod_init(void)
{
	int i, ver;

	DBG("domain - initializing\n");
	
	db_url.len = strlen(db_url.s);
	domain_table.len = strlen(domain_table.s);
	domain_col.len = strlen(domain_col.s);

	/* Check if database module has been loaded */
	if (domain_db_bind(db_url.s)<0)  return -1;

	/* Check if cache needs to be loaded from domain table */
	if (db_mode != 0) {
		if (domain_db_init(db_url.s)<0) return -1;
		     /* Check table version */
		ver = domain_db_ver(&domain_table);
		if (ver < 0) {
			LOG(L_ERR, "ERROR: domain:mod_init(): "
					"error while querying table version\n");
			domain_db_close();
			return -1;
		} else if (ver < TABLE_VERSION) {
			LOG(L_ERR, "ERROR: domain:mod_init(): invalid table"
					" version (use openser_mysql.sh reinstall)\n");
			domain_db_close();
			return -1;
		}		

		/* Initialize fifo interface */
		(void)init_domain_fifo();

		if (init_domain_unixsock() < 0) {
			LOG(L_ERR, "ERROR: domain:mod_init(): error while initializing"
					" unix socket interface\n");
			domain_db_close();
			return -1;
		}

		/* Initializing hash tables and hash table variable */
		hash_table_1 = (struct domain_list **)shm_malloc
			(sizeof(struct domain_list *) * DOM_HASH_SIZE);
		if (hash_table_1 == 0) {
			LOG(L_ERR, "ERROR: domain: mod_init(): "
					"No memory for hash table\n");
		}

		hash_table_2 = (struct domain_list **)shm_malloc
			(sizeof(struct domain_list *) * DOM_HASH_SIZE);
		if (hash_table_2 == 0) {
			LOG(L_ERR, "ERROR: domain: mod_init():"
					" No memory for hash table\n");
		}
		for (i = 0; i < DOM_HASH_SIZE; i++) {
			hash_table_1[i] = hash_table_2[i] = (struct domain_list *)0;
		}

		hash_table = (struct domain_list ***)shm_malloc
			(sizeof(struct domain_list *));
		*hash_table = hash_table_1;

		if (reload_domain_table() == -1) {
			LOG(L_CRIT, "ERROR: domain:mod_init():"
					" Domain table reload failed\n");
			return -1;
		}
			
		domain_db_close();
	}

	return 0;
}


static int child_init(int rank)
{
	/* Check if database is needed by child */
	if (((db_mode == 0) && (rank > 0)) || ((db_mode != 0) && (rank == PROC_FIFO))) {
		if (domain_db_init(db_url.s)<0) {
			LOG(L_ERR, "ERROR: domain:child_init():"
					" Unable to connect to the database\n");
			return -1;
		}
	}
	return 0;
}


static void destroy(void)
{
	/* Destroy is called from the main process only,
	 * there is no need to close database here because
	 * it is closed in mod_init already
	 */
}


static int fixup_avp(void** param, int param_no)
{
    struct param_source *ps = NULL;
    int_str avp_name;
    char *src;
    str avp;

    if (param_no==1) {
        src = (char*) *param;

        ps = (struct param_source*) pkg_malloc(sizeof(struct param_source));
        if (ps == NULL) {
            LOG(L_ERR, "ERROR: domain/fixup_avp(): out of pkg mem\n");
            return E_OUT_OF_MEM;
        }
        memset(ps, 0, sizeof(struct param_source));

        if (strcasecmp(src, "$ruri") == 0) {
            ps->source = PARAM_SOURCE_RURI;
        } else if (strcasecmp(src, "$from") == 0) {
            ps->source = PARAM_SOURCE_FROM;
        } else {
            ps->source = PARAM_SOURCE_AVP;
            avp.s = src;
            avp.len = strlen(src);
            if (parse_avp_spec(&avp, &ps->avp_type, &avp_name)!=0) {
                LOG(L_ERR, "ERROR: domain/fixup_avp(): invalid avp specification: %s\n", src);
                pkg_free(ps);
                return E_UNSPEC;
            }
            /* copy the avp name into the ps structure */
            if (ps->avp_type & AVP_NAME_STR) {
                ps->avp_name.s = (str*) pkg_malloc(sizeof(str) + avp_name.s->len + 1);
                if (ps->avp_name.s == NULL) {
                    LOG(L_ERR, "ERROR: domain/fixup_avp(): out of pkg mem\n");
                    pkg_free(ps);
                    return E_OUT_OF_MEM;
                }
                ps->avp_name.s->len = avp_name.s->len;
                ps->avp_name.s->s = ((char*)ps->avp_name.s) + sizeof(str);
                memcpy(ps->avp_name.s->s, avp_name.s->s, avp_name.s->len);
                ps->avp_name.s->s[ps->avp_name.s->len] = 0;
            } else {
                ps->avp_name.n = avp_name.n;
            }
        }

        pkg_free(*param);
        *param = (void*)ps;

    }

    return 0;
}


