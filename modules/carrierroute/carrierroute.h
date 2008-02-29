/*
 * $Id$
 *
 * Copyright (C) 2007-2008 1&1 Internet AG
 *
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
 */

/**
 * @file carrierroute.h
 * @brief Contains the functions exported by the module.
 */

#ifndef SP_ROUTE_H
#define SP_ROUTE_H

#include "../../str.h"
#include "../../usr_avp.h"
#include "../../pvar.h"

#define SIP_URI "sip:"
#define SIP_URI_LEN 4
#define SIPS_URI "sips:"
#define SIPS_URI_LEN 5
#define AT_SIGN "@"
#define AT_SIGN_LEN 1

#define SP_EMPTY_PREFIX "null"

#define DICE_MAX 1000

#define COLUMN_NUM 10
#define COL_ID             0
#define COL_CARRIER        1
#define COL_SCAN_PREFIX    2
#define COL_DOMAIN         3
#define COL_PROB           4
#define COL_REWRITE_HOST   5
#define COL_STRIP          6
#define COL_REWRITE_PREFIX 7
#define COL_REWRITE_SUFFIX 8
#define COL_COMMENT        9

#define FAILURE_COLUMN_NUM 10
#define FCOL_ID             0
#define FCOL_CARRIER        1
#define FCOL_SCAN_PREFIX    2
#define FCOL_DOMAIN         3
#define FCOL_HOST_NAME      4
#define FCOL_REPLY_CODE     5
#define FCOL_FLAGS          6
#define FCOL_MASK           7
#define FCOL_NEXT_DOMAIN    8
#define FCOL_COMMENT        9

#define SUBSCRIBER_COLUMN_NUM 3
#define SUBSCRIBER_USERNAME_COL 0
#define SUBSCRIBER_DOMAIN_COL   1
#define SUBSCRIBER_CARRIER_COL  2

#define CARRIER_COLUMN_NUM 2
#define CARRIER_ID_COL 0
#define CARRIER_NAME_COL 1

#define SP_ROUTE_MODE_DB 1
#define SP_ROUTE_MODE_FILE 2

#define ROUTE_TABLE_VER 1
#define CARRIER_TABLE_VER 1
#define FAILURE_TABLE_VER 1

extern str db_url;
extern str db_table;
extern str db_failure_table;
extern str carrier_table;
extern str subscriber_table;
extern str * subscriber_columns[];
extern str * carrier_columns[];
extern str * columns[];
extern str * failure_columns[];
extern char * config_source;
extern char * config_file;
extern char * default_tree;

extern int mode;
extern int use_domain;
extern int fallback_default;

enum hash_algorithm {
	alg_crc32 = 1, /*!< hashing algorithm is CRC32 */
	alg_prime, /*!< hashing algorithm is (right 18 digits of hash_source % prime_number) % max_targets + 1 */
	alg_error
};

struct multiparam_t {
	enum {
		MP_INT,
		MP_STR,
		MP_AVP,
		MP_PVE,
	} type;
	union {
		int n;
		str s;
		struct {
			unsigned short flags;
			int_str name;
		} a;
		pv_elem_t *p;
	} u;
};

#endif
