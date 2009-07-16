/*
 * $Id$
 *
 * pua_dialoginfo module - sending publish with dialog info from dialog module
 * 
 * Copyright (C) 2006 Voice Sistem S.R.L.
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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/parser.h>
#include <time.h>

#include "../../parser/parse_expires.h"
#include "../../parser/msg_parser.h"
#include "../../str.h"
#include "../../name_alias.h"
#include "../../socket_info.h"
#include "../usrloc/usrloc.h"
#include "../usrloc/ul_callback.h"
#include "../tm/tm_load.h"
#include "../pua/pua.h"
#include "pua_dialoginfo.h"

/* global modul parameters */
extern int include_callid;
extern int include_localremote;
extern int include_tags;


/* for debug purpose only */
void print_publ(publ_info_t* p)
{
	LM_DBG("publ:\n");
	LM_DBG("uri= %.*s\n", p->pres_uri->len, p->pres_uri->s);
	LM_DBG("id= %.*s\n", p->id.len, p->id.s);
	LM_DBG("expires= %d\n", p->expires);
}	

str* build_dialoginfo(char *state, str *entity, str *peer, str *callid, 
	unsigned int initiator, str *localtag, str *remotetag)
{
	xmlDocPtr  doc = NULL; 
	xmlNodePtr root_node = NULL;
	xmlNodePtr dialog_node = NULL;
	xmlNodePtr state_node = NULL;
	xmlNodePtr remote_node = NULL;
	xmlNodePtr local_node = NULL;
	xmlNodePtr tag_node = NULL;
	str *body= NULL;
	char buf[MAX_URI_SIZE+1];

	if (entity->len > MAX_URI_SIZE) {
		LM_ERR("entity URI '%.*s' too long, maximum=%d\n",entity->len, entity->s, MAX_URI_SIZE);
		return NULL;
	}
    memcpy(buf, entity->s, entity->len);
	buf[entity->len]= '\0';

	/* create the Publish body  */
	doc = xmlNewDoc(BAD_CAST "1.0");
	if(doc==0)
		return NULL;

    root_node = xmlNewNode(NULL, BAD_CAST "dialog-info");
	if(root_node==0)
		goto error;
    
	xmlDocSetRootElement(doc, root_node);

    xmlNewProp(root_node, BAD_CAST "xmlns",
			BAD_CAST "urn:ietf:params:xml:ns:dialog-info");
	/* we set the version to 0 but it should be set to the correct value
       in the pua module */
	xmlNewProp(root_node, BAD_CAST "version",
			BAD_CAST "0");
	xmlNewProp(root_node, BAD_CAST  "state",
			BAD_CAST "full" );
	xmlNewProp(root_node, BAD_CAST "entity", 
			BAD_CAST buf);

	/* RFC 3245 differs between id and call-id. For example if a call
	   is forked and 2 early dialogs are established, we should send 2
	    PUBLISH requests, both have the same call-id but different id.
	    Thus, id could be for example derived from the totag.

	    Currently the dialog module does not support multiple dialogs.
	    Thus, it does no make sense to differ here between multiple dialog.
	    Thus, id and call-id will be populated identically */

	/* dialog tag */
	dialog_node =xmlNewChild(root_node, NULL, BAD_CAST "dialog", NULL) ;
	if( dialog_node ==NULL)
	{
		LM_ERR("while adding child\n");
		goto error;
	}

	if (callid->len > MAX_URI_SIZE) {
		LM_ERR("call-id '%.*s' too long, maximum=%d\n", callid->len, callid->s, MAX_URI_SIZE);
		return NULL;
	}
    memcpy(buf, callid->s, callid->len);
	buf[callid->len]= '\0';

	xmlNewProp(dialog_node, BAD_CAST "id", BAD_CAST buf);
	if (include_callid) {
		xmlNewProp(dialog_node, BAD_CAST "call-id", BAD_CAST buf);
	}
	if (include_tags) {
		if (localtag && localtag->s) {
			if (localtag->len > MAX_URI_SIZE) {
				LM_ERR("localtag '%.*s' too long, maximum=%d\n", localtag->len, localtag->s, MAX_URI_SIZE);
				return NULL;
			}
		    memcpy(buf, localtag->s, localtag->len);
			buf[localtag->len]= '\0';
			xmlNewProp(dialog_node, BAD_CAST "local-tag", BAD_CAST buf);
		}
		if (remotetag && remotetag->s) {
			if (remotetag->len > MAX_URI_SIZE) {
				LM_ERR("remotetag '%.*s' too long, maximum=%d\n", remotetag->len, remotetag->s, MAX_URI_SIZE);
				return NULL;
			}
		    memcpy(buf, remotetag->s, remotetag->len);
			buf[remotetag->len]= '\0';
			xmlNewProp(dialog_node, BAD_CAST "remote-tag", BAD_CAST buf);
		}
	}

	if (initiator) {
		xmlNewProp(dialog_node, BAD_CAST "direction", BAD_CAST "initiator");
	}else {
		xmlNewProp(dialog_node, BAD_CAST "direction", BAD_CAST "recipient");
	}

	/* state tag */
	state_node = xmlNewChild(dialog_node, NULL, BAD_CAST "state", BAD_CAST state) ;
	if( state_node ==NULL)
	{
		LM_ERR("while adding child\n");
		goto error;
	}

	if (include_localremote) {
		/* remote tag*/	
		remote_node = xmlNewChild(dialog_node, NULL, BAD_CAST "remote", NULL) ;
		if( remote_node ==NULL)
		{
			LM_ERR("while adding child\n");
			goto error;
		}

		if (peer->len > MAX_URI_SIZE) {
			LM_ERR("peer '%.*s' too long, maximum=%d\n", peer->len, peer->s, MAX_URI_SIZE);
			return NULL;
		}
    	memcpy(buf, peer->s, peer->len);
		buf[peer->len]= '\0';

		tag_node = xmlNewChild(remote_node, NULL, BAD_CAST "identity", BAD_CAST buf) ;
		if( tag_node ==NULL)
		{
			LM_ERR("while adding child\n");
			goto error;
		}
		tag_node = xmlNewChild(remote_node, NULL, BAD_CAST "target", NULL) ;
		if( tag_node ==NULL)
		{
			LM_ERR("while adding child\n");
			goto error;
		}
		xmlNewProp(tag_node, BAD_CAST "uri", BAD_CAST buf);

		/* local tag*/	
		local_node = xmlNewChild(dialog_node, NULL, BAD_CAST "local", NULL) ;
		if( local_node ==NULL)
		{
			LM_ERR("while adding child\n");
			goto error;
		}

		if (entity->len > MAX_URI_SIZE) {
			LM_ERR("entity '%.*s' too long, maximum=%d\n", entity->len, entity->s, MAX_URI_SIZE);
			return NULL;
		}
    	memcpy(buf, entity->s, entity->len);
		buf[entity->len]= '\0';

		tag_node = xmlNewChild(local_node, NULL, BAD_CAST "identity", BAD_CAST buf) ;
		if( tag_node ==NULL)
		{
			LM_ERR("while adding child\n");
			goto error;
		}
		tag_node = xmlNewChild(local_node, NULL, BAD_CAST "target", NULL) ;
		if( tag_node ==NULL)
		{
			LM_ERR("while adding child\n");
			goto error;
		}
		xmlNewProp(tag_node, BAD_CAST "uri", BAD_CAST buf);
	}

	/* create the body */
	body = (str*)pkg_malloc(sizeof(str));
	if(body == NULL)
	{
		LM_ERR("while allocating memory\n");
		return NULL;
	}
	memset(body, 0, sizeof(str));

	xmlDocDumpFormatMemory(doc,(unsigned char**)(void*)&body->s,&body->len,1);

	LM_DBG("new_body:\n%.*s\n",body->len, body->s);

    /*free the document */
	xmlFreeDoc(doc);
    xmlCleanupParser();

	return body;

error:
	if(body)
	{
		if(body->s)
			xmlFree(body->s);
		pkg_free(body);
	}
	if(doc)
		xmlFreeDoc(doc);
	return NULL;
}	

void dialog_publish(char *state, str *entity, str *peer, str *callid, 
	unsigned int initiator, unsigned int lifetime, str *localtag, str *remotetag)
{
	str* body= NULL;
	publ_info_t publ;
    struct sip_uri entity_uri;

	/* send PUBLISH only if the receiver (entity, RURI) is local*/
	if (parse_uri(entity->s, entity->len, &entity_uri) < 0) {
		LM_ERR("failed to parse the entity URI\n");
		return;
	}
	if (!check_self(&(entity_uri.host), 0, 0)) {
		LM_DBG("do not send PUBLISH to external URI %.*s\n",entity->len, entity->s);
		return;
	}

	body= build_dialoginfo(state, entity, peer, callid, initiator, localtag, remotetag);
	if(body == NULL || body->s == NULL)
	{
		LM_ERR("failed to construct dialoginfo body\n");
		goto error;
	}
	
	memset(&publ, 0, sizeof(publ_info_t));

	publ.pres_uri= entity;
	publ.body = body;

	publ.id.s = (char*)pkg_malloc(15/* DIALOG_PUBLISH. */ + callid->len);
	if(publ.id.s== NULL) {
		LM_ERR("no more memory\n");
		goto error;
	}
	publ.id.len = sprintf(publ.id.s, "DIALOG_PUBLISH.%.*s", callid->len, callid->s);
	
	publ.content_type.s= "application/dialog-info+xml";
	publ.content_type.len= 27;
		
	publ.expires= lifetime;
	
	/* make UPDATE_TYPE, as if this "publish dialog" is not found 
	   by pua it will fallback to INSERT_TYPE anyway */
	publ.flag|= UPDATE_TYPE;

	publ.source_flag|= DIALOG_PUBLISH;
	publ.event|= DIALOG_EVENT;
	publ.extra_headers= NULL;
	publ.outbound_proxy = presence_server;

	print_publ(&publ);
	if(pua_send_publish(&publ)< 0)
	{
		LM_ERR("sending publish failed\n");
	}	

error:

	if(body)
	{
		if(body->s)
			xmlFree(body->s);
		pkg_free(body);
	}
	
	if(publ.id.s)
		pkg_free(publ.id.s);

	return;
}
