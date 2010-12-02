/*
 * $Id: records.c $
 *
 * back-to-back logic module
 *
 * Copyright (C) 2009 Free Software Fundation
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
 *  2009-08-03  initial version (Anca Vamanu)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../../mem/shm_mem.h"
#include "../../ut.h"
#include "../presence/hash.h"
#include "records.h"

extern b2bl_cb_params_t cb_params;

static void _print_entity(int index, b2bl_entity_id_t* c)
{
	LM_INFO("{%d} type=%d [%p]->[%.*s] state=%d no=%d peer=[%p]\n",
		index, c->type, c, c->key.len, c->key.s, c->state, c->no, c->peer);
}

void b2bl_print_tuple(b2bl_tuple_t* tuple)
{
	int index;
	b2bl_entity_id_t* e;

	if(tuple)
	{
		LM_INFO("[%p]->[%.*s]\n", tuple, tuple->key->len, tuple->key->s);
		for (index = 0; index < MAX_B2BL_ENT; index++)
		{
			e = tuple->servers[index];
			if (e) _print_entity(index, e);
		}
		for (index = 0; index < MAX_B2BL_ENT; index++)
		{
			e = tuple->clients[index];
			if (e) _print_entity(index, e);
		}
	}
}

/* Function that inserts a new b2b_logic record - the lock remains taken */
b2bl_tuple_t* b2bl_insert_new(struct sip_msg* msg,
		unsigned int hash_index, b2b_scenario_t* scenario,
		str* args[], str* body, str** b2bl_key_s)
{
	b2bl_tuple_t * it, *prev_it;
	b2bl_tuple_t* tuple;
	str* b2bl_key;
	int i;
	static char buf[256];
	int buf_len= 255;
	int size;
	str extra_headers={0, 0};

	size = sizeof(b2bl_tuple_t);
	if(body && use_init_sdp)
	{
		size+= body->len;
	}

	tuple = (b2bl_tuple_t*)shm_malloc(size);
	if(tuple == NULL)
	{
		LM_ERR("No more shared memory\n");
		return NULL;
	}
	memset(tuple, 0, size);

	if(body && use_init_sdp)
	{
		tuple->sdp.s = (char*)tuple + sizeof(b2bl_tuple_t);
		memcpy(tuple->sdp.s, body->s, body->len);
		tuple->sdp.len =  body->len;
	}

	tuple->scenario = scenario;

	tuple->insert_time = get_ticks();

	if(msg)
	{
		if(b2b_extra_headers(msg, 0, &extra_headers)< 0)
		{
			LM_ERR("Failed to create extra headers\n");
			goto error;
		}
		if(extra_headers.s)
		{
			tuple->extra_headers = (str*)shm_malloc(sizeof(str) + extra_headers.len);
			if(tuple->extra_headers == NULL)
			{
				LM_ERR("No more shared memory\n");
				goto error;
			}
			tuple->extra_headers->s = (char*)tuple->extra_headers + sizeof(str);
			memcpy(tuple->extra_headers->s, extra_headers.s, extra_headers.len);
			tuple->extra_headers->len = extra_headers.len;
			pkg_free(extra_headers.s);
		}
	}

	/* copy the function parameters that customize the scenario */
	memset(tuple->scenario_params, 0, MAX_SCENARIO_PARAMS* sizeof(str));
	if(scenario && args)
	{
		for(i = 0; i< scenario->param_no; i++)
		{
			if(args[i]==NULL)
			{
				LM_DBG("Fewer parameters, expected [%d] received [%d]\n",
						scenario->param_no, i);
				break;
			}
			/* must print the value of the argument */
			if(msg && b2bl_caller != CALLER_MODULE)
			{
				buf_len= 255;
				if(pv_printf(msg, (pv_elem_t*)args[i], buf, &buf_len)<0)
				{
					LM_ERR("cannot print the format\n");
					goto error;
				}

				tuple->scenario_params[i].s = (char*)shm_malloc(buf_len);
				if(tuple->scenario_params[i].s == NULL)
				{
					LM_ERR("No more shared memory\n");
					goto error;
				}
				memcpy(tuple->scenario_params[i].s, buf, buf_len);
				tuple->scenario_params[i].len = buf_len;
				LM_DBG("Printed parameter [%.*s]\n", buf_len, buf);
			}
			else
			{
				if(args[i]->s==NULL || args[i]->len==0)
				{
					LM_DBG("Fewer parameters, expected [%d] received [%d]\n",
							scenario->param_no, i);
					break;
				}

				tuple->scenario_params[i].s = (char*)shm_malloc(args[i]->len);
				if(tuple->scenario_params[i].s == NULL)
				{
					LM_ERR("No more shared memory\n");
					goto error;
				}
				memcpy(tuple->scenario_params[i].s, args[i]->s, args[i]->len);
				tuple->scenario_params[i].len = args[i]->len;
			}
		}
	}
	tuple->scenario_state = B2B_NOTDEF_STATE;

	lock_get(&b2bl_htable[hash_index].lock);

	it = b2bl_htable[hash_index].first;
	if(it == NULL)
	{
		b2bl_htable[hash_index].first = tuple;
		tuple->prev = tuple->next = NULL;
		tuple->id = 0;
	}
	else
	{
		while(it)
		{
			prev_it = it;
			it = it->next;
		}
		prev_it->next = tuple;
		tuple->prev = prev_it;
		tuple->id = prev_it->id +1;
	}

	b2bl_key = b2bl_generate_key(hash_index, tuple->id);
	if(b2bl_key == NULL)
	{
		LM_ERR("failed to generate b2b logic key\n");
		lock_release(&b2bl_htable[hash_index].lock);
		return NULL;
	}
	tuple->key = b2bl_key;

	*b2bl_key_s = b2bl_key;
	tuple->db_flag = INSERTDB_FLAG;

	LM_DBG("new tuple [%p]->[%.*s]\n", tuple, b2bl_key->len, b2bl_key->s);

	return tuple;
error:
	lock_release(&b2bl_htable[hash_index].lock);
	return 0;
}


int b2bl_drop_entity(b2bl_entity_id_t* entity, b2bl_tuple_t* tuple)
{
	b2bl_entity_id_t* e;
	unsigned int index;
	int found = 0;

	for (index = 0; index < MAX_B2BL_ENT; index++)
	{
		e = tuple->servers[index];
		if (e == entity)
		{
			found = 1;
			switch(index)
			{
				case 0:
					tuple->servers[0] = tuple->servers[1];
					tuple->servers[1] = NULL;
					break;
				case 1:
					tuple->servers[1] = NULL;
					if (tuple->servers[0] == NULL)
						LM_ERR("inconsistent tuple [%p]->[%.*s]\n",
							tuple, tuple->key->len, tuple->key->s);
					break;
				default:
					LM_CRIT("we should never end up here\n");
			}
			break;
		}
		e = tuple->clients[index];
		if (e == entity)
		{
			found = 1;
			switch(index)
			{
				case 0:
					tuple->clients[0] = tuple->clients[1];
					tuple->clients[1] = NULL;
					break;
				case 1:
					tuple->clients[1] = NULL;
					if (tuple->clients[0] == NULL)
						LM_ERR("inconsistent tuple [%p]->[%.*s]\n",
							tuple, tuple->key->len, tuple->key->s);
					break;
				default:
					LM_CRIT("we should never end up here\n");
			}
			break;
		}
	}
	return found;
}

void b2bl_delete_entity(b2bl_entity_id_t* entity, b2bl_tuple_t* tuple)
{
	unsigned int i;
	int found = 0;

	LM_DBG("Delete entity [%p]->[%.*s]\n", entity, entity->key.len, entity->key.s);

	found = b2bl_drop_entity(entity, tuple);

	if(found)
		b2b_api.entity_delete(entity->type, &entity->key, entity->dlginfo);

	if(entity->dlginfo)
		shm_free(entity->dlginfo);

	for(i = 0; i< MAX_BRIDGE_ENT; i++)
		if(tuple->bridge_entities[i] == entity)
			tuple->bridge_entities[i] = NULL;

/*	if(entity->peer && entity->peer->peer==entity)
		entity->peer->peer = NULL;
*/

	for(i = 0; i< MAX_B2BL_ENT; i++)
	{
		if(tuple->servers[i] && tuple->servers[i]->peer==entity)
			tuple->servers[i]->peer= NULL;
		if(tuple->clients[i] && tuple->clients[i]->peer==entity)
			tuple->clients[i]->peer= NULL;
	}

	LM_INFO("delete tuple [%.*s], entity [%.*s]\n",
			tuple->key->len, tuple->key->s, entity->key.len, entity->key.s);
	shm_free(entity);

	/* for debuging */
	b2bl_print_tuple(tuple);
}


int b2bl_add_client(b2bl_tuple_t* tuple, b2bl_entity_id_t* entity)
{
	LM_INFO("adding entity [%p]->[%.*s] to tuple [%p]->[%.*s]\n",
		entity, entity->key.len, entity->key.s,
		tuple, tuple->key->len, tuple->key->s);

	if (tuple->clients[0] == NULL)
	{
		if (tuple->clients[1])
		{
			LM_ERR("inconsistent clients state for tuple [%p]->[%.*s]\n",
				tuple, tuple->key->len, tuple->key->s);
			return -1;
		}
		tuple->clients[0] = entity;
	}
	else if (tuple->clients[1] == NULL)
		tuple->clients[1] = entity;
	else
	{
		LM_ERR("unable to add entity [%p]->[%.*s] to tuple [%p]->[%.*s], all spots taken\n",
			entity, entity->key.len, entity->key.s,
			tuple, tuple->key->len, tuple->key->s);
		return -1;
	}
		
	b2bl_print_tuple(tuple);
	return 0;
}


int b2bl_add_server(b2bl_tuple_t* tuple, b2bl_entity_id_t* entity)
{
	LM_INFO("adding entity [%p]->[%.*s] to tuple [%p]->[%.*s]\n",
		entity, entity->key.len, entity->key.s,
		tuple, tuple->key->len, tuple->key->s);

	if (tuple->servers[0] == NULL)
	{
		if (tuple->servers[1])
		{
			LM_ERR("inconsistent servers state for tuple [%p]->[%.*s]\n",
				tuple, tuple->key->len, tuple->key->s);
			return -1;
		}
		tuple->servers[0] = entity;
	}
	else if (tuple->servers[1] == NULL)
		tuple->servers[1] = entity;
	else
	{
		LM_ERR("unable to add entity [%p]->[%.*s] to tuple [%p]->[%.*s], all spots taken\n",
			entity, entity->key.len, entity->key.s,
			tuple, tuple->key->len, tuple->key->s);
		return -1;
	}
		
	b2bl_print_tuple(tuple);
	return 0;
}


void b2bl_delete(b2bl_tuple_t* tuple, unsigned int hash_index,
		int not_del_b2be)
{
	b2bl_entity_id_t *e;
	int i;
	int index;

	LM_DBG("Delete record [%p]->[%.*s], hash_index=[%d], local_index=[%d]\n",
			tuple, tuple->key->len, tuple->key->s, hash_index, tuple->id);

	if(tuple->cbf)
	{
		memset(&cb_params, 0, sizeof(b2bl_cb_params_t));
		cb_params.param = tuple->cb_param;
		cb_params.stat = NULL;
		cb_params.msg = NULL;
		tuple->cbf(&cb_params, B2B_DESTROY);
	}
	if(!not_del_b2be)
		b2bl_db_delete(tuple);
	if(b2bl_htable[hash_index].first == tuple)
	{
		b2bl_htable[hash_index].first = tuple->next;
		if(tuple->next)
			tuple->next->prev = NULL;
	}
	else
	{
		if(tuple->prev)
			tuple->prev->next = tuple->next;
		if(tuple->next)
			tuple->next->prev = tuple->prev;
	}

	for (index = 0; index < MAX_B2BL_ENT; index++)
	{
		e = tuple->servers[index];
		if (e)
		{
			if (e->key.s && e->key.len && !not_del_b2be)
				b2b_api.entity_delete(e->type, &e->key, e->dlginfo);
			if(e->dlginfo)
				shm_free(e->dlginfo);
			shm_free(e);
		}
		e = tuple->clients[index];
		if (e)
		{
			if (e->key.s && e->key.len && !not_del_b2be)
				b2b_api.entity_delete(e->type, &e->key, e->dlginfo);
			if(e->dlginfo)
				shm_free(e->dlginfo);
			shm_free(e);
		}
	}

//	if(tuple->bridge_entities[1] && tuple->bridge_entities[1]->key.s != NULL)
//		shm_free(tuple->bridge_entities[1]->key.s);

	for(i = 0; i< MAX_SCENARIO_PARAMS; i++)
	{
		if(tuple->scenario_params[i].s)
			shm_free(tuple->scenario_params[i].s);
	}

	if(tuple->key)
		shm_free(tuple->key);

	if(tuple->extra_headers)
		shm_free(tuple->extra_headers);

	if(tuple->b1_sdp.s)
		shm_free(tuple->b1_sdp.s);

	shm_free(tuple);
}

/* key format : hash_index.local *
 */

int b2bl_parse_key(str* key, unsigned int* hash_index,
		unsigned int* local_index)
{
	char* p;
	int hi_len;
	str s;

	if(!key || !key->s || !key->len)
		return -1;

	p= strchr(key->s, '.');
	if(p == NULL)
	{
		LM_ERR("Wrong b2b logic key\n");
		return -1;
	}

	hi_len = p - key->s;
	s.s = key->s;
	s.len = hi_len;
	if(str2int(&s, hash_index)< 0)
		return -1;
	
	s.s = p+1;
	s.len = key->s + key->len - s.s;
	if(str2int(&s, local_index)< 0)
		return -1;

	LM_DBG("hash_index = [%d]  - local_index= [%d]\n", *hash_index, *local_index);
	return 0;
}


str* b2bl_generate_key(unsigned int hash_index, unsigned int local_index)
{
	char buf[15];
	str* b2b_key;
	int len;

	len = sprintf(buf, "%d.%d", hash_index, local_index);

	b2b_key = (str*)shm_malloc(sizeof(str)+ len);
	if(b2b_key== NULL)
	{
		LM_ERR("no more shared memory\n");
		return NULL;
	}
	b2b_key->s = (char*)b2b_key + sizeof(str);
	memcpy(b2b_key->s, buf, len);
	b2b_key->len = len;

	return b2b_key;
}

b2bl_tuple_t* b2bl_search_tuple_safe(unsigned int hash_index, unsigned int local_index)
{
	b2bl_tuple_t* tuple;

	tuple = b2bl_htable[hash_index].first;
	while(tuple && tuple->id != local_index)
		tuple = tuple->next;

	return tuple;
}

int init_b2bl_htable(void)
{
	int i;
	b2bl_htable = (b2bl_table_t)shm_malloc(b2bl_hsize* sizeof(b2bl_entry_t));
	if(!b2bl_htable)
		ERR_MEM(SHARE_MEM);
	
	memset(b2bl_htable, 0, b2bl_hsize* sizeof(b2bl_entry_t));
	for(i= 0; i< b2bl_hsize; i++)
	{
		lock_init(&b2bl_htable[i].lock);
		b2bl_htable[i].first = NULL;
	}

	return 0;
error:
	return -1;
}

void destroy_b2bl_htable(void)
{
	int i;
	b2bl_tuple_t* tuple;

	if(!b2bl_htable)
		return;

	for(i= 0; i< b2bl_hsize; i++)
	{
		lock_destroy(&b2bl_htable[i].lock);
		tuple = b2bl_htable[i].first;

		while(tuple)
		{
			b2bl_delete(tuple, i, 1);
			tuple = b2bl_htable[i].first;
		}
	}
	shm_free(b2bl_htable);
}

/* Take headers to pass on the other side:
 *	Content-Type: 
 *	Allow: 
 *	Supported:
 *	Require
 *	RSeq
 *	Session-Expires
 *	Min-SE
*/
int b2b_extra_headers(struct sip_msg* msg, str* b2bl_key, str* extra_headers)
{
	char* p;
	struct hdr_field* require_hdr;
	struct hdr_field* rseq_hdr;
	struct hdr_field* hdr;
	struct hdr_field* hdrs[HDR_LST_LEN + HDR_DEFAULT_LEN];
	int hdrs_no = 0;
	int len = 0;
	int i;

	if(msg->content_type)
		hdrs[hdrs_no++] = msg->content_type;
	if(msg->supported)
		hdrs[hdrs_no++] = msg->supported;
	if(msg->allow)
		hdrs[hdrs_no++] = msg->allow;
	if(msg->proxy_require)
		hdrs[hdrs_no++] = msg->proxy_require;
	if(msg->session_expires)
		hdrs[hdrs_no++] = msg->session_expires;
	if(msg->min_se)
		hdrs[hdrs_no++] = msg->min_se;
	if(msg->maxforwards)
		hdrs[hdrs_no++] = msg->maxforwards;

	require_hdr = get_header_by_static_name( msg, "Require");
	if(require_hdr)
		hdrs[hdrs_no++] = require_hdr;

	rseq_hdr = get_header_by_static_name( msg, "RSeq");
	if(rseq_hdr)
		hdrs[hdrs_no++] = rseq_hdr;


	/* add also the custom headers */
	for(i = 0; i< custom_headers_lst_len; i++)
	{
		hdr = get_header_by_name( msg, custom_headers_lst[i].s,
				custom_headers_lst[i].len);
		if(hdr)
		{
			hdrs[hdrs_no++] = hdr;
		}
	}

	/* calculate the length*/
	for(i = 0; i< hdrs_no; i++)
		len += hdrs[i]->len;

	if(len == 0)
		return 0;

	extra_headers->s = (char*)pkg_malloc(len);
	if(extra_headers->s == NULL)
	{
		LM_ERR("No more memory\n");
		return -1;
	}

	p = extra_headers->s;

	/* construct the headers string */
	for(i = 0; i< hdrs_no; i++)
	{
		memcpy(p, hdrs[i]->name.s, hdrs[i]->len);
		p += hdrs[i]->len;
	}
	extra_headers->len = p - extra_headers->s;

	return 0;
}

