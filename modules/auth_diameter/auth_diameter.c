/*
 * $Id$ 
 *
 * Digest Authentication - Diameter support
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
 *
 * History:
 * -------
 *  
 *  
 * 2006-03-01 pseudo variables support for domain name (bogdan)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 

#include "../../sr_module.h"
#include "../../error.h"
#include "../../dprint.h"
#include "../../items.h"
#include "../../mem/mem.h"

#include "diameter_msg.h"
#include "auth_diameter.h"
#include "authorize.h"
#include "tcp_comm.h"

MODULE_VERSION


/* Pointer to reply function in stateless module */
int (*sl_reply)(struct sip_msg* _msg, char* _str1, char* _str2);

static int mod_init(void);                        /* Module initialization function*/
static int mod_child_init(int r);                 /* Child initialization function*/
static int auth_fixup(void** param, int param_no);
static int group_fixup(void** param, int param_no);

int diameter_www_authorize(struct sip_msg* _msg, char* _realm, char* _s2);
int diameter_proxy_authorize(struct sip_msg* _msg, char* _realm, char* _s2);
int diameter_is_user_in(struct sip_msg* _msg, char* group, char* _s2);

/*
 * Module parameter variables
 */
char* diameter_client_host = "localhost";
int diameter_client_port = 3000;
int use_domain = 0;

rd_buf_t *rb;

/*
 * Exported functions
 */
static cmd_export_t cmds[] = {
	{"diameter_www_authorize",   diameter_www_authorize,   1, auth_fixup,
			REQUEST_ROUTE},
	{"diameter_proxy_authorize", diameter_proxy_authorize, 1, auth_fixup,
			REQUEST_ROUTE},
	{"diameter_is_user_in",      diameter_is_user_in,      2, group_fixup,
			REQUEST_ROUTE},
	{0, 0, 0, 0, 0}
};


/*
 * Exported parameters
 */
static param_export_t params[] = {
	{"diameter_client_host", STR_PARAM, &diameter_client_host},
	{"diameter_client_port", INT_PARAM, &diameter_client_port},
	{"use_domain", INT_PARAM, &use_domain},
	{0, 0, 0}
};


/*
 * Module interface
 */
struct module_exports exports = {
	"auth_diameter",
	DEFAULT_DLFLAGS, /* dlopen flags */
	cmds,          /* Exported functions */
	params,        /* Exported parameters */
	0,             /* exported statistics */
	0,             /* exported MI functions */
	0,             /* exported pseudo-variables */
	mod_init,      /* module initialization function */
	0,             /* response function */
	0,             /* destroy function */
	mod_child_init /* child initialization function */
};


/*
 * Module initialization function
 */
static int mod_init(void)
{
	DBG("auth_diameter - Initializing\n");

	sl_reply = find_export("sl_send_reply", 2, 0);
	if (!sl_reply) {
		LOG(L_ERR, "auth_diameter.c:mod_init(): This module requires"
					" sl module\n");
		return -1;
	}
	
	return 0;
}

static int mod_child_init(int r)
{	
	/* open TCP connection */
	DBG("auth_diameter.c: mod_child_init(): Initializing TCP connection\n");

	sockfd = init_mytcp(diameter_client_host, diameter_client_port);
	if(sockfd==-1) 
	{
		DBG("auth_diameter.c: mod_child_init(): TCP connection not"
				" established\n");
		return -1;
	}

	DBG("auth_diameter.c: mod_child_init(): TCP connection established"
				" on socket=%d\n", sockfd);
	
	rb = (rd_buf_t*)pkg_malloc(sizeof(rd_buf_t));
	if(!rb)
	{
		DBG("auth_diameter.c: mod_child_init: no more free memory\n");
		return -1;
	}
	rb->buf = 0;
	rb->chall = 0;

	return 0;
}

#if 0
static void destroy(void)
{
	close_tcp_connection(sockfd);
}
#endif


/*
 * Convert char* parameter to xl_elem_t* parameter
 */
static int auth_fixup(void** param, int param_no)
{
	xl_elem_t *model;
	char* s;

	if (param_no == 1) {
		s = (char*)*param;
		if (s==0 || s[0]==0) {
			model = 0;
		} else {
			if (xl_parse_format(s,&model,XL_DISABLE_COLORS)<0) {
				LOG(L_ERR, "ERROR:auth_diameter:auth_fixup: xl_parse_format "
					"failed\n");
				return E_OUT_OF_MEM;
			}
		}
		*param = (void*)model;
	}

	return 0;
}


/*
 * Authorize using Proxy-Authorization header field
 */
int diameter_proxy_authorize(struct sip_msg* _msg, char* _realm, char* _s2)
{
	/* realm parameter is converted to str* in str_fixup */
	return authorize(_msg, (xl_elem_t*)_realm, HDR_PROXYAUTH_T);
}


/*
 * Authorize using WWW-Authorization header field
 */
int diameter_www_authorize(struct sip_msg* _msg, char* _realm, char* _s2)
{
	return authorize(_msg, (xl_elem_t*)_realm, HDR_AUTHORIZATION_T);
}


static int group_fixup(void** param, int param_no)
{
	void* ptr;
	str* s;

	if (param_no == 1) 
	{
		ptr = *param;
		
		if (!strcasecmp((char*)*param, "Request-URI")) 
		{
			*param = (void*)1;
			goto end;
		} 

		if(!strcasecmp((char*)*param, "To")) 
		{
			*param = (void*)2;
			goto end;
		} 

		if (!strcasecmp((char*)*param, "From")) 
		{
			*param = (void*)3;
			goto end;
		} 

		if (!strcasecmp((char*)*param, "Credentials")) 
		{
			*param = (void*)4;
			goto end;
		}
				
		LOG(L_ERR, "group_fixup(): Unsupported Header Field identifier\n");
		return E_UNSPEC;
		
		//pkg_free(ptr);
	} 
	
	if (param_no == 2) 
	{
		s = (str*)pkg_malloc(sizeof(str));
		if (!s) 
		{
			LOG(L_ERR, "group_fixup(): No memory left\n");
			return E_UNSPEC;
		}
		ptr = *param;
		s->s = (char*)*param;
		s->len = strlen(s->s);
		*param = (void*)s;
	}

end:
//	pkg_free(ptr);
	return 0;
}


