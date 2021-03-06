/*
 * $Id$
 *
 * Copyright (C) 2011 VoIP Embedded Inc.
 * Copyright (C) 2005 Voice Sistem SRL
 *
 * This file is part of opnser, a free SIP server.
 *
 * Registrant OpenSIPS-module is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * Registrant OpenSIPS-module is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *
 * History:
 * ---------
 *  2005-01-31  first version (ramona)
 *  2011-02-20  import file from uac (Ovidiu Sas)
 */


#ifndef _REG_AUTH_H_
#define _REG_AUTH_H_

#include "../../parser/msg_parser.h"

#define WWW_AUTH_CODE       401
#define WWW_AUTH_HDR        "WWW-Authenticate"
#define WWW_AUTH_HDR_LEN    (sizeof(WWW_AUTH_HDR)-1)
#define PROXY_AUTH_CODE     407
#define PROXY_AUTH_HDR      "Proxy-Authenticate"
#define PROXY_AUTH_HDR_LEN  (sizeof(PROXY_AUTH_HDR)-1)


#define HASHLEN 16
typedef char HASH[HASHLEN];

#define HASHHEXLEN 32
typedef char HASHHEX[HASHHEXLEN+1];

struct uac_credential {
	str realm;
	str user;
	str passwd;
};

struct authenticate_nc_cnonce {
	str *nc;
	str *cnonce;
};


int uac_auth( struct sip_msg *msg);
void do_uac_auth(str *method, str *uri, struct uac_credential *crd,
		struct authenticate_body *auth, struct authenticate_nc_cnonce *auth_nc_cnonce,
		HASHHEX response);
str* build_authorization_hdr(int code, str *uri,
		struct uac_credential *crd, struct authenticate_body *auth,
		struct authenticate_nc_cnonce *auth_nc_cnonce, char *response);

#endif
