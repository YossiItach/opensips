/*
 * $Id$
 *
 * MySQL module core functions
 *
 * Copyright (C) 2001-2003 FhG Fokus
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
 */


#ifndef DBASE_H
#define DBASE_H


#include "../../db/db_con.h"
#include "../../db/db_res.h"
#include "../../db/db_key.h"
#include "../../db/db_op.h"
#include "../../db/db_val.h"


/*
 * Initialize database connection
 */
db_con_t* db_mysql_init(const char* _sqlurl);


/*
 * Close a database connection
 */
void db_mysql_close(db_con_t* _h);


/*
 * Free all memory allocated by get_result
 */
int db_mysql_free_result(db_con_t* _h, db_res_t* _r);


/*
 * Do a query
 */
int db_mysql_query(const db_con_t* _h, const db_key_t* _k, const db_op_t* _op,
	     const db_val_t* _v, const db_key_t* _c, const int _n, const int _nc,
	     const db_key_t _o, db_res_t** _r);


/*
 * fetch rows from a result
 */
int db_mysql_fetch_result(const db_con_t* _h, db_res_t** _r, const int nrows);


/*
 * Raw SQL query
 */
int db_mysql_raw_query(const db_con_t* _h, const char* _s, db_res_t** _r);


/*
 * Insert a row into table
 */
int db_mysql_insert(const db_con_t* _h, const db_key_t* _k, const db_val_t* _v, const int _n);


/*
 * Delete a row from table
 */
int db_mysql_delete(const db_con_t* _h, const db_key_t* _k, const 
	db_op_t* _o, const db_val_t* _v, const int _n);


/*
 * Update a row in table
 */
int db_mysql_update(const db_con_t* _h, const db_key_t* _k, const db_op_t* _o,
	const db_val_t* _v, const db_key_t* _uk, const db_val_t* _uv, const int _n,
	const int _un);


/*
 * Just like insert, but replace the row if it exists
 */
int db_mysql_replace(const db_con_t* handle, const db_key_t* keys, const db_val_t* 	vals, const int n);

/*
 * Returns the last inserted ID
 */
int db_last_inserted_id(const db_con_t* _h);

/*
 * Insert a row into table, update on duplicate key
 */
int db_insert_update(const db_con_t* _h, const db_key_t* _k, const db_val_t* _v,
	const int _n);


/*
 * Store name of table that will be used by
 * subsequent database functions
 */
int db_mysql_use_table(db_con_t* _h, const char* _t);


#endif /* DBASE_H */
