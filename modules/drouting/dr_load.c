/*
 * $Id: dr_load.c,v 1.2 2007/09/12 08:08:22 daniel Exp $
 *
 * Copyright (C) 2005-2008 Voice Sistem SRL
 *
 * This file is part of Open SIP Server (OpenSIPS).
 *
 * DROUTING OpenSIPS-module is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * DROUTING OpenSIPS-module is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * For any questions about this software and its license, please contact
 * Voice Sistem at following e-mail address:
 *         office@voice-system.ro
 *
 * History:
 * ---------
 *  2005-02-20  first version (cristian)
 *  2005-02-27  ported to 0.9.0 (bogdan)
 */


#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>


#include "../../dprint.h"
#include "../../db/db.h"
#include "../../mem/shm_mem.h"

#include "dr_load.h"
#include "routing.h"
#include "prefix_tree.h"
#include "dr_time.h"
#include "parse.h"


#define DST_ID_DRD_COL   "gwid"
#define ADDRESS_DRD_COL  "address"
#define STRIP_DRD_COL    "strip"
#define PREFIX_DRD_COL   "pri_prefix"
#define TYPE_DRD_COL     "type"
static str dst_id_drd_col = str_init("gwid");
static str address_drd_col = str_init("address");
static str strip_drd_col = str_init("strip");
static str prefix_drd_col = str_init("pri_prefix");
static str type_drd_col = str_init("type");

#define RULE_ID_DRR_COL   "ruleid"
#define GROUP_DRR_COL     "groupid"
#define PREFIX_DRR_COL    "prefix"
#define TIME_DRR_COL      "timerec"
#define PRIORITY_DRR_COL  "priority"
#define ROUTEID_DRR_COL   "routeid"
#define DSTLIST_DRR_COL   "gwlist"
static str rule_id_drr_col = str_init("ruleid");
static str group_drr_col = str_init("groupid");
static str prefix_drr_col = str_init("prefix");
static str time_drr_col = str_init("timerec");
static str priority_drr_col = str_init("priority");
static str routeid_drr_col = str_init("routeid");
static str dstlist_drr_col = str_init("gwlist");


#define check_val( _val, _type, _not_null, _is_empty_str) \
	do{\
		if ((_val)->type!=_type) { \
			LM_ERR("bad colum type\n");\
			goto error;\
		} \
		if (_not_null && (_val)->nul) { \
			LM_ERR("nul column\n");\
			goto error;\
		} \
		if (_is_empty_str && VAL_STRING(_val)==0) { \
			LM_ERR("empty str column\n");\
			goto error;\
		} \
	}while(0)


#define TR_SEPARATOR '|'

#define load_TR_value( _p,_s, _tr, _func, _err, _done) \
	do{ \
		_s = strchr(_p, (int)TR_SEPARATOR); \
		if (_s) \
			*_s = 0; \
		/*DBG("----parsing tr param <%s>\n",_p);*/\
		if(_s != _p) {\
			if( _func( _tr, _p)) {\
				if (_s) *_s = TR_SEPARATOR; \
				goto _err; \
			} \
		} \
		if (_s) { \
			*_s = TR_SEPARATOR; \
			_p = _s+1;\
			if ( *(_p)==0 ) \
				goto _done; \
		} else {\
			goto _done; \
		}\
	} while(0)

extern int dr_fetch_rows;

static inline tmrec_t* parse_time_def(char *time_str)
{
	tmrec_t *time_rec;
	char *p,*s;

	p = time_str;
	time_rec = 0;

	time_rec = (tmrec_t*)shm_malloc(sizeof(tmrec_t));
	if (time_rec==0) {
		LM_ERR("no more pkg mem\n");
		goto error;
	}
	memset( time_rec, 0, sizeof(tmrec_t));

	/* empty definition? */
	if ( time_str==0 || *time_str==0 )
		goto done;

	load_TR_value( p, s, time_rec, tr_parse_dtstart, parse_error, done);
	load_TR_value( p, s, time_rec, tr_parse_duration, parse_error, done);
	load_TR_value( p, s, time_rec, tr_parse_freq, parse_error, done);
	load_TR_value( p, s, time_rec, tr_parse_until, parse_error, done);
	load_TR_value( p, s, time_rec, tr_parse_interval, parse_error, done);
	load_TR_value( p, s, time_rec, tr_parse_byday, parse_error, done);
	load_TR_value( p, s, time_rec, tr_parse_bymday, parse_error, done);
	load_TR_value( p, s, time_rec, tr_parse_byyday, parse_error, done);
	load_TR_value( p, s, time_rec, tr_parse_byweekno, parse_error, done);
	load_TR_value( p, s, time_rec, tr_parse_bymonth, parse_error, done);

	/* success */
done:
	return time_rec;
parse_error:
	LM_ERR("parse error in <%s> around position %i\n",
		time_str, (int)(long)(p-time_str));
error:
	if (time_rec)
		tmrec_free( time_rec );
	return 0;
}


static int add_rule(rt_data_t *rdata, char *grplst, str *prefix, rt_info_t *rule)
{
	long int t;
	char *tmp;
	char *ep;
	int n;

	tmp=grplst;
	n=0;
	/* parse the grplst */
	while(tmp && (*tmp!=0)) {
		errno = 0;
		t = strtol(tmp, &ep, 10);
		if (ep == tmp) {
			LM_ERR("bad grp id '%c' (%d)[%s]\n",
				*ep, (int)(ep-grplst), grplst);
			goto error;
		}
		if ((!IS_SPACE(*ep)) && (*ep != SEP) && (*ep != SEP1) && (*ep!=0)) {
			LM_ERR("bad char %c (%d) [%s]\n",
					*ep, (int)(ep-grplst), grplst);
			goto error;
		}
		if (errno == ERANGE && (t== LONG_MAX || t== LONG_MIN)) {
			LM_ERR("out of bounds\n");
			goto error;
		}
		n++;
		/* add rule -> has prefix? */
		if (prefix->len) {
			/* add the routing rule */
			if ( add_prefix(rdata->pt, prefix, rule, (unsigned int)t)!=0 ) {
				LM_ERR("failed to add prefix route\n");
					goto error;
			}
		} else {
			if ( add_rt_info( &rdata->noprefix, rule, (unsigned int)t)!=0 ) {
				LM_ERR("failed to add prefixless route\n");
					goto error;
			}
		}
		/* keep parsing */
		if(IS_SPACE(*ep))
			EAT_SPACE(ep);
		if(ep && (*ep == SEP || *ep == SEP1))
			ep++;
		tmp = ep;
	}

	if(n==0) {
		LM_ERR("no id in grp list [%s]\n",
			grplst);
		goto error;
	}

	return 0;
error:
	return -1;
}


rt_data_t* dr_load_routing_info( db_func_t *dr_dbf, db_con_t* db_hdl,
											str *drd_table, str* drr_table )
{
	int    int_vals[4];
	char * str_vals[4];
	str tmp;
	db_key_t columns[7];
	db_res_t* res;
	db_row_t* row;
	rt_info_t *ri;
	rt_data_t *rdata;
	tmrec_t   *time_rec;
	int i,n;

	res = 0;
	ri = 0;
	rdata = 0;

	/* init new data structure */
	if ( (rdata=build_rt_data())==0 ) {
		LM_ERR("failed to build rdata\n");
		goto error;
	}

	/* read the destinations */
	if (dr_dbf->use_table( db_hdl, drd_table) < 0) {
		LM_ERR("cannot select table \"%.*s\"\n", drd_table->len,drd_table->s);
		goto error;
	}

	columns[0] = &dst_id_drd_col;
	columns[1] = &address_drd_col;
	columns[2] = &strip_drd_col;
	columns[3] = &prefix_drd_col;
	columns[4] = &type_drd_col;

	if (DB_CAPABILITY(*dr_dbf, DB_CAP_FETCH)) {
		if ( dr_dbf->query( db_hdl, 0, 0, 0, columns, 0, 5, 0, 0 ) < 0) {
			LM_ERR("DB query failed\n");
			goto error;
		}
		if(dr_dbf->fetch_result(db_hdl, &res, dr_fetch_rows)<0) {
			LM_ERR("Error fetching rows\n");
			goto error;
		}
	} else {
		if ( dr_dbf->query( db_hdl, 0, 0, 0, columns, 0, 5, 0, &res) < 0) {
			LM_ERR("DB query failed\n");
			goto error;
		}
	}

	if (RES_ROW_N(res) == 0) {
		LM_WARN("table \"%.*s\" empty\n", drd_table->len,drd_table->s );
	}
	LM_DBG("%d records found in %.*s\n",
		RES_ROW_N(res), drd_table->len,drd_table->s);
	n = 0;
	do {
		for(i=0; i < RES_ROW_N(res); i++) {
			row = RES_ROWS(res) + i;
			/* DST_ID column */
			check_val( ROW_VALUES(row), DB_INT, 1, 0);
			int_vals[0] = VAL_INT   (ROW_VALUES(row));
			/* ADDRESS column */
			check_val( ROW_VALUES(row)+1, DB_STRING, 1, 1);
			str_vals[0] = (char*)VAL_STRING(ROW_VALUES(row)+1);
			/* STRIP column */
			check_val( ROW_VALUES(row)+2, DB_INT, 1, 0);
			int_vals[1] = VAL_INT   (ROW_VALUES(row)+2);
			/* PREFIX column */
			check_val( ROW_VALUES(row)+3, DB_STRING, 0, 0);
			str_vals[1] = (char*)VAL_STRING(ROW_VALUES(row)+3);
			/* TYPE column */
			check_val( ROW_VALUES(row)+4, DB_INT, 1, 0);
			int_vals[2] = VAL_INT(ROW_VALUES(row)+4);

			/* add the destinaton definition in */
			if ( add_dst( rdata, int_vals[0], str_vals[0], int_vals[1],
					str_vals[1], int_vals[2])<0 ) {
				LM_ERR("failed to add destination -> skipping\n");
				continue;
			}
			n++;
		}
		if (DB_CAPABILITY(*dr_dbf, DB_CAP_FETCH)) {
			if(dr_dbf->fetch_result(db_hdl, &res, dr_fetch_rows)<0) {
				LM_ERR( "fetching rows (1)\n");
				goto error;
			}
		} else {
			break;
		}
	} while(RES_ROW_N(res)>0);

	dr_dbf->free_result(db_hdl, res);
	res = 0;

	if (n==0) {
		LM_WARN("no valid "
			"destinations set -> ignoring the routing rules\n");
		return rdata;
	}

	/* read the routing rules */
	if (dr_dbf->use_table( db_hdl, drr_table) < 0) {
		LM_ERR("cannot select table \"%.*s\"\n", drr_table->len, drr_table->s);
		goto error;
	}

	columns[0] = &rule_id_drr_col;
	columns[1] = &group_drr_col;
	columns[2] = &prefix_drr_col;
	columns[3] = &time_drr_col;
	columns[4] = &priority_drr_col;
	columns[5] = &routeid_drr_col;
	columns[6] = &dstlist_drr_col;

	if (DB_CAPABILITY(*dr_dbf, DB_CAP_FETCH)) {
		if ( dr_dbf->query( db_hdl, 0, 0, 0, columns, 0, 7, 0, 0) < 0) {
			LM_ERR("DB query failed\n");
			goto error;
		}
		if(dr_dbf->fetch_result(db_hdl, &res, dr_fetch_rows)<0) {
			LM_ERR("Error fetching rows\n");
			goto error;
		}
	} else {
		if ( dr_dbf->query( db_hdl, 0, 0, 0, columns, 0, 7, 0, &res) < 0) {
			LM_ERR("DB query failed\n");
			goto error;
		}
	}

	if (RES_ROW_N(res) == 0) {
		LM_WARN("table \"%.*s\" is empty\n", drr_table->len, drr_table->s);
	}

	LM_DBG("%d records found in %.*s\n", RES_ROW_N(res),
		drr_table->len, drr_table->s);

	n = 0;
	do {
		for(i=0; i < RES_ROW_N(res); i++) {
			row = RES_ROWS(res) + i;
			/* RULE_ID column */
			check_val( ROW_VALUES(row), DB_INT, 1, 0);
			int_vals[0] = VAL_INT (ROW_VALUES(row));
			/* GROUP column */
			check_val( ROW_VALUES(row)+1, DB_STRING, 1, 1);
			str_vals[0] = (char*)VAL_STRING(ROW_VALUES(row)+1);
			/* PREFIX column */
			check_val( ROW_VALUES(row)+2, DB_STRING, 1, 1);
			str_vals[1] = (char*)VAL_STRING(ROW_VALUES(row)+2);
			tmp.s = str_vals[1];
			tmp.len = strlen(str_vals[1]);
			/* TIME column */
			check_val( ROW_VALUES(row)+3, DB_STRING, 1, 1);
			str_vals[2] = (char*)VAL_STRING(ROW_VALUES(row)+3);
			/* PRIORITY column */
			check_val( ROW_VALUES(row)+4, DB_INT, 1, 0);
			int_vals[2] = VAL_INT   (ROW_VALUES(row)+4);
			/* ROUTE_ID column */
			check_val( ROW_VALUES(row)+5, DB_INT, 1, 0);
			int_vals[3] = VAL_INT   (ROW_VALUES(row)+5);
			/* DSTLIST column */
			check_val( ROW_VALUES(row)+6, DB_STRING, 1, 1);
			str_vals[3] = (char*)VAL_STRING(ROW_VALUES(row)+6);
			/* parse the time definition */
			if ((time_rec=parse_time_def(str_vals[2]))==0) {
				LM_ERR("bad time definition "
					"<%s> -> skipping rule\n",str_vals[2]);
				continue;
			}
			/* build the routing rule */
			if ((ri = build_rt_info( int_vals[2], time_rec, int_vals[3],
					str_vals[3], rdata->pgw_l))== 0 ) {
				LM_ERR("failed to add "
					"routing rule-> skipping rule\n");
				tmrec_free( time_rec );
				continue;
			}
			/* add the rule */
			if (add_rule( rdata, str_vals[0], &tmp, ri)!=0) {
				LM_ERR("failed to add"
						" rule -> skipping\n");
				free_rt_info( ri );
				continue;
			}
			n++;
		}
		if (DB_CAPABILITY(*dr_dbf, DB_CAP_FETCH)) {
			if(dr_dbf->fetch_result(db_hdl, &res, dr_fetch_rows)<0) {
				LM_ERR( "fetching rows (1)\n");
				goto error;
			}
		} else {
			break;
		}
	} while(RES_ROW_N(res)>0);

	dr_dbf->free_result(db_hdl, res);
	res = 0;

	if (n==0) {
		LM_WARN("no valid routing rules -> discarding all destinations\n");
		free_rt_data( rdata, 0 );
	}

	return rdata;
error:
	if (res)
		dr_dbf->free_result(db_hdl, res);
	if (rdata)
		free_rt_data( rdata, 1 );
	return 0;
}
