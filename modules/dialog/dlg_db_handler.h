/*
 * $Id$
 *
 * Copyright (C) 2007 Voice System SRL
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
 * --------
 * 2007-05-10  initial version (ancuta)
 */


#ifndef _DLG_DB_HANDLER_H_
#define _DLG_DB_HANDLER_H_

#include "../../str.h"
#include "../../db/db.h"

#define CALL_ID_COL				"callid"
#define FROM_URI_COL			"from_uri"
#define FROM_TAG_COL			"from_tag"
#define TO_URI_COL				"to_uri"
#define TO_TAG_COL				"to_tag"
#define HASH_ID_COL				"hash_id"
#define HASH_ENTRY_COL			"hash_entry"
#define STATE_COL				"state"
#define START_TIME_COL			"start_time"
#define TIMEOUT_COL				"timeout"
#define TO_CSEQ_COL				"callee_cseq"
#define FROM_CSEQ_COL			"caller_cseq"
#define TO_ROUTE_COL			"callee_route_set"
#define FROM_ROUTE_COL			"caller_route_set"
#define TO_CONTACT_COL			"callee_contact"
#define FROM_CONTACT_COL		"caller_contact"
#define FROM_SOCK_COL			"caller_sock"
#define TO_SOCK_COL				"callee_sock"
#define DIALOG_TABLE_NAME		"dialog"

#define DLG_TABLE_VERSION		2

/*every minute the dialogs' information will be refreshed*/
#define DB_DEFAULT_UPDATE_PERIOD	60
#define DB_MODE_NONE				0
#define DB_MODE_REALTIME			1
#define DB_MODE_DELAYED				2
#define DB_MODE_SHUTDOWN			3

#define DIALOG_TABLE_COL_NO 		18

#define DIALOG_FETCH_SIZE			128

extern str call_id_column; 
extern str from_uri_column;
extern str from_tag_column;
extern str to_uri_column;
extern str to_tag_column;
extern str h_id_column;
extern str h_entry_column;
extern str state_column;
extern str start_time_column;
extern str timeout_column;
extern str to_cseq_column;
extern str from_cseq_column;
extern str to_route_column;
extern str from_route_column;
extern str to_contact_column;
extern str from_contact_column;
extern str to_sock_column;
extern str from_sock_column;
extern str dialog_table_name;
extern int dlg_db_mode;

#define should_remove_dlg_db() (dlg_db_mode && (dlg_db_mode!=DB_MODE_SHUTDOWN))


int init_dlg_db(const str *db_url, int dlg_hash_size, int db_update_period);
int dlg_connect_db(const str *db_url);
void destroy_dlg_db();

int remove_dialog_from_db(struct dlg_cell * cell);
int update_dialog_dbinfo(struct dlg_cell * cell);
void dialog_update_db(unsigned int ticks, void * param);

#endif
