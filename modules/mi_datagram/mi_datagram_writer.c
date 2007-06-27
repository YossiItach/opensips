/*
 * $Id: mi_datagram_writer.c 1133 2007-04-02 17:31:13Z ancuta_onofrei $
 *
 * Copyright (C) 2007 Voice Sistem SRL
 *
 * This file is part of openser, a free SIP server.
 *
 * openser is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openser is distributed in the hope that it will be useful,
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
 *  2007-06-25  first version (ancuta)
 */


#include <stdio.h>
#include <string.h>

#include "../../str.h"
#include "../../ut.h"
#include "../../dprint.h"
#include "../../mi/tree.h"
#include "../../mem/mem.h"
#include "mi_datagram.h"
#include "datagram_fnc.h"
#include "mi_datagram_parser.h"


static unsigned int mi_write_buffer_len = 0;
static str  mi_datagram_indent;


int mi_datagram_writer_init( unsigned int size , char *indent)
{
	mi_write_buffer_len = size;

	if (indent==NULL || indent[0]==0 ) {
		mi_datagram_indent.s = 0;
		mi_datagram_indent.len = 0;
	} else {
		mi_datagram_indent.s = indent;
		mi_datagram_indent.len = strlen(indent);
	}

	return 0;
}


static inline int mi_datagram_write_node(datagram_stream * dtgram, 
											struct mi_node *node, int level)
{
	struct mi_attr *attr;
	char *start, *end, *p;

	
	start = p = dtgram->current;
	end = dtgram->start + dtgram->len;
	DBG("DBG:mi_datagram:mi_datagram_write_node:writing the name "
		"<%.*s> and value <%.*s> \n",node->name.len, node->name.s, 
			node->value.len, node->value.s);
	/* write indents */
	if (mi_datagram_indent.s) {
		if (p + level*mi_datagram_indent.len>end)
		{
			DBG("datagram: write_node: a too long line\n");
			return -1;
		}
		for( ; level>0 ; level-- ) {
			memcpy( p, mi_datagram_indent.s, mi_datagram_indent.len);
			p += mi_datagram_indent.len;
		}
	}
	/* name and value */
	if (node->name.s!=NULL) {
		if (p+node->name.len+3>end)
		{
			DBG("datagram: write node: too long name\n");
			return -1;
		}
		memcpy(p,node->name.s,node->name.len);
		p += node->name.len;
		*(p++) = MI_ATTR_VAL_SEP1;
		*(p++) = MI_ATTR_VAL_SEP2;
		*(p++) = ' ';
	}
	
	/*DBG("DBG:mi_datagram:mi_datagram_writer: after adding the "
			"name, the datagram is %s\n ", dtgram->datagram.s);*/
	if (node->value.s!=NULL) {
		if (p+node->value.len>end)
		{
			DBG("datagram: write node: too long value\n");
			return -1;
		}
		memcpy(p,node->value.s,node->value.len);
		p += node->value.len;
	}
/*	DBG("DBG:mi_datagram:mi_datagram_writer: after adding the "
			"value,  the datagram is %s\n ", dtgram->datagram.s);*/
	/* attributes */
	for( attr=node->attributes ; attr!=NULL ; attr=attr->next ) {
		if (attr->name.s!=NULL) {
			if (p+attr->name.len+2>end)
			{
				DBG("datagram: write node: too long attr name\n");
				return -1;
			}
			*(p++) = ' ';
			memcpy(p,attr->name.s,attr->name.len);
			p += attr->name.len;
			*(p++) = '=';
		}
		if (attr->value.s!=NULL) {
			if (p+attr->value.len>end)
			{
				DBG("datagram: write node: too long attr value\n");
				return -1;
			}
			memcpy(p,attr->value.s,attr->value.len);
			p += attr->value.len;
		}
	}
/*	DBG("DBG:mi_datagram:mi_datagram_writer: after adding the "
			"attributes, the datagram is %s\n ", dtgram->datagram.s);*/
	if (p+1>end)
	{
		DBG("datagram: write node: overflow before returning\n");
		return -1;
	}
	*(p++) = '\n';

	dtgram->len -= p-start;
	dtgram->current = p;
	
	return 0;
}



static int datagram_recur_write_tree(datagram_stream *dtgram,  
									  struct mi_node *tree, int level)
{
	for( ; tree ; tree=tree->next ) {
		if (mi_datagram_write_node( dtgram, tree, level)!=0) {
			LOG(L_ERR,"ERROR:mi_datagram:recur_write_tree:failed to write -"
				"line too long!!!\n");
			return -1;
		}
		if (tree->kids) {
			if (datagram_recur_write_tree(dtgram, tree->kids, level+1)<0)
				return -1;
		}
	}
	return 0;
}



int mi_datagram_write_tree(datagram_stream * dtgram, struct mi_root *tree)
{
	str code;
	dtgram->current = dtgram->start;
	dtgram->len = mi_write_buffer_len;

	/* write the root node */
	code.s = int2str((unsigned long)tree->code, &code.len);
	if (code.len+tree->reason.len+1 > dtgram->len) {
		LOG(L_ERR,"ERROR:mi_datagram:mi_write_tree: failed to write - "
			"reason too long!!!\n");
		return -1;
	}

	memcpy( dtgram->start, code.s, code.len);
	dtgram->current += code.len;
	*(dtgram->current) = ' ';
	dtgram->current++;

	if (tree->reason.len) {
		memcpy(dtgram->current, tree->reason.s, tree->reason.len);
		dtgram->current += tree->reason.len;
	}

	*(dtgram->current) = '\n';
	dtgram->current++;
	dtgram->len -= code.len + 1 + tree->reason.len+1;

	if (datagram_recur_write_tree(dtgram, tree->node.kids, 0)!=0)
		return -1;

	if (dtgram->len<=0) {
		LOG(L_ERR,"ERROR:mi_datagram:mi_write_tree: failed to write - "
			"EOC does not fit in!!!\n");
		return -1;
	}

	*(dtgram->current) = '\n';
	dtgram->len--;
	*(dtgram->current) = '\0';

	return 0;
}

