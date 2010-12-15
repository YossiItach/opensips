/*
 * $Id$
 *
 * presence_xml module -  
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
 *
 * History:
 * --------
 *  2007-04-11  initial version (anca)
 */

#include <string.h>
#include <stdlib.h>
#include <libxml/parser.h>

#include "../../mem/mem.h"
#include "../presence/utils_func.h"
#include "../presence/hash.h"
#include "xcap_auth.h"
#include "pidf.h"
#include "notify_body.h"
#include "presence_xml.h"

str* offline_nbody(str* body);
str* agregate_xmls(str* pres_user, str* pres_domain, str** body_array, int n);
str* get_final_notify_body( subs_t *subs, str* notify_body, xmlNodePtr rule_node);

void free_xml_body(char* body)
{
	if(body== NULL)
		return;

	xmlFree(body);
	body= NULL;
}


str* pres_agg_nbody(str* pres_user, str* pres_domain, str** body_array, int n, int off_index)
{
	str* n_body= NULL;
	str* body= NULL;

	if(body_array== NULL && !pidf_manipulation)
		return NULL;

	if(off_index>= 0)
	{
		body= body_array[off_index];
		body_array[off_index]= offline_nbody(body);

		if(body_array[off_index]== NULL || body_array[off_index]->s== NULL)
		{
			LM_ERR("while constructing offline body\n");
			return NULL;
		}
	}
	LM_DBG("[user]=%.*s  [domain]= %.*s\n",
			pres_user->len, pres_user->s, pres_domain->len, pres_domain->s);
	n_body= agregate_xmls(pres_user, pres_domain, body_array, n);
	if(n_body== NULL && n!= 0)
	{
		LM_ERR("while aggregating body\n");
	}

	if(off_index>= 0)
	{
		xmlFree(body_array[off_index]->s);
		pkg_free(body_array[off_index]);
		body_array[off_index]= body;
	}

	xmlCleanupParser();
    xmlMemoryDump();

	return n_body;
}	

int pres_apply_auth(str* notify_body, subs_t* subs, str** final_nbody)
{
	xmlDocPtr doc= NULL;
	xmlNodePtr node= NULL;
	str* n_body= NULL;
	
	*final_nbody= NULL;
	if(force_active)
		return 0;

	if(subs->auth_rules_doc== NULL)
	{
		LM_ERR("NULL rules doc\n");
		return -1;
	}
	doc= xmlParseMemory(subs->auth_rules_doc->s, subs->auth_rules_doc->len);
	if(doc== NULL)
	{
		LM_ERR("parsing xml doc\n");
		return -1;
	}
	
	node= get_rule_node(subs, doc);
	if(node== NULL)
	{
		LM_DBG("The subscriber didn't match the conditions\n");
		xmlFreeDoc(doc);
		return 0;
	}
	
	n_body= get_final_notify_body(subs, notify_body, node);
	if(n_body== NULL)
	{
		LM_ERR("in function get_final_notify_body\n");
		xmlFreeDoc(doc);
		return -1;
	}

	xmlFreeDoc(doc);
	xmlCleanupParser();
    xmlMemoryDump();

	*final_nbody= n_body;
	return 1;

}

str* get_final_notify_body( subs_t *subs, str* notify_body, xmlNodePtr rule_node)
{
	xmlNodePtr transf_node = NULL, node = NULL, dont_provide = NULL;
	xmlNodePtr doc_root = NULL, doc_node = NULL, provide_node = NULL;
	xmlNodePtr all_node = NULL;
	xmlDocPtr doc= NULL;
	char name[15];
	char service_uri_scheme[10];
	int i= 0, found = 0;
	str* new_body = NULL;
    char* class_cont = NULL, *occurence_ID= NULL, *service_uri= NULL;
	char* deviceID = NULL;
	char* content = NULL;
	char all_name[20];

	strcpy(all_name, "all-");

	new_body = (str*)pkg_malloc(sizeof(str));
	if(new_body == NULL)
	{
		LM_ERR("while allocating memory\n");
		return NULL;
	}

	memset(new_body, 0, sizeof(str));

	doc = xmlParseMemory(notify_body->s, notify_body->len);
	if(doc== NULL)
	{
		LM_ERR("while parsing the xml body message\n[%.*s]\n",
				notify_body->len, notify_body->s);
		goto error;
	}
	doc_root = xmlDocGetNodeByName(doc,"presence", NULL);
	if(doc_root == NULL)
	{
		LM_ERR("while extracting the presence node\n");
		goto error;
	}

	transf_node = xmlNodeGetChildByName(rule_node, "transformations");
	if(transf_node == NULL)
	{ 
		LM_DBG("No transformations node found\n");
		goto done;
	}
	
	for(node = transf_node->children; node; node = node->next )
	{
		if(xmlStrcasecmp(node->name, (unsigned char*)"text")== 0)
			continue;

		LM_DBG("transf_node->name:%s\n",node->name);

		strcpy((char*)name ,(char*)(node->name + 8));
		strcpy(all_name+4, name);
		
		if(xmlStrcasecmp((unsigned char*)name,(unsigned char*)"services") == 0)
			strcpy(name, "tuple");
		if(strncmp((char*)name,"person", 6) == 0)
			name[6] = '\0';

		doc_node = xmlNodeGetNodeByName(doc_root, name, NULL);
		if(doc_node == NULL)
			continue;
		LM_DBG("searched doc_node->name:%s\n",name);
	
		content = (char*)xmlNodeGetContent(node);
		if(content)
		{
			LM_DBG("content = %s\n", content);
		
			if(xmlStrcasecmp((unsigned char*)content,
					(unsigned char*) "FALSE") == 0)
			{
				LM_DBG("found content false\n");
				while( doc_node )
				{
					xmlUnlinkNode(doc_node);	
					xmlFreeNode(doc_node);
					doc_node = xmlNodeGetChildByName(doc_root, name);
				}
				xmlFree(content);
				continue;
			}
		
			if(xmlStrcasecmp((unsigned char*)content,
					(unsigned char*) "TRUE") == 0)
			{
				LM_DBG("found content true\n");
				xmlFree(content);
				continue;
			}
			xmlFree(content);
		}

		while (doc_node )
		{
			if (xmlStrcasecmp(doc_node->name,(unsigned char*)"text")==0)
			{
				doc_node = doc_node->next;
				continue;
			}

			if (xmlStrcasecmp(doc_node->name,(unsigned char*)name)!=0)
			{
				break;
			}
			all_node = xmlNodeGetChildByName(node, all_name) ;
		
			if( all_node )
			{
				LM_DBG("must provide all\n");
				doc_node = doc_node->next;
				continue;
			}

			found = 0;
			class_cont = xmlNodeGetNodeContentByName(doc_node, "class", 
					NULL);
			if(class_cont == NULL)
				LM_DBG("no class tag found\n");
			else
				LM_DBG("found class = %s\n", class_cont);

			occurence_ID = xmlNodeGetAttrContentByName(doc_node, "id");
			if(occurence_ID == NULL)
				LM_DBG("no id found\n");
			else
				LM_DBG("found id = %s\n", occurence_ID);


			deviceID = xmlNodeGetNodeContentByName(doc_node, "deviceID",
					NULL);	
			if(deviceID== NULL)
				LM_DBG("no deviceID found\n");
			else
				LM_DBG("found deviceID = %s\n",	deviceID);


			service_uri = xmlNodeGetNodeContentByName(doc_node, "contact",
					NULL);	
			if(service_uri == NULL)
				LM_DBG("no service_uri found\n");
			else
				LM_DBG("found service_uri = %s\n", service_uri);
			i = 0;
			if(service_uri!= NULL)
			{
				while(service_uri[i]!= ':')
				{
					service_uri_scheme[i] = service_uri[i];
					i++;
				}
				service_uri_scheme[i] = '\0';
				LM_DBG("service_uri_scheme: %s\n", service_uri_scheme);
			}

			provide_node = node->children;
				
			while ( provide_node!= NULL )
			{
				if(xmlStrcasecmp(provide_node->name,(unsigned char*) "text")==0)
				{
					provide_node = 	provide_node->next;
					continue;
				}

				if(xmlStrcasecmp(provide_node->name,(unsigned char*)"class")== 0
						&& class_cont )
				{
					content = (char*)xmlNodeGetContent(provide_node);

					if(content&& xmlStrcasecmp((unsigned char*)content,
								(unsigned char*)class_cont) == 0)
					{
						found = 1;
						LM_DBG("found class= %s", class_cont);
						xmlFree(content);
						break;
					}
					if(content)
						xmlFree(content);
				}
				if(xmlStrcasecmp(provide_node->name,
							(unsigned char*) "deviceID")==0&&deviceID )
				{
					content = (char*)xmlNodeGetContent(provide_node);

					if(content && xmlStrcasecmp ((unsigned char*)content,
								(unsigned char*)deviceID) == 0)
					{
						found = 1;
						LM_DBG("found deviceID= %s", deviceID);
						xmlFree(content);
						break;
					}
					if(content)
						xmlFree(content);

				}
				if(xmlStrcasecmp(provide_node->name,
							(unsigned char*)"occurence-id")== 0&& occurence_ID)
				{
					content = (char*)xmlNodeGetContent(provide_node);
					if(content && xmlStrcasecmp ((unsigned char*)content,
								(unsigned char*)occurence_ID) == 0)
					{
						found = 1;
						LM_DBG("found occurenceID= %s\n", occurence_ID);
						xmlFree(content);
						break;
					}
					if(content)
						xmlFree(content);

				}
				if(xmlStrcasecmp(provide_node->name,
							(unsigned char*)"service-uri")== 0 && service_uri)
				{
					content = (char*)xmlNodeGetContent(provide_node);
					if(content&& xmlStrcasecmp ((unsigned char*)content,
								(unsigned char*)service_uri) == 0)
					{
						found = 1;
						LM_DBG("found service_uri= %s", service_uri);
						xmlFree(content);
						break;
					}
					if(content)
						xmlFree(content);

				}
			
				if(xmlStrcasecmp(provide_node->name,
						(unsigned char*)"service-uri-scheme")==0&& i)
				{
					content = (char*)xmlNodeGetContent(provide_node);
					LM_DBG("service_uri_scheme=%s\n",content);
					if(content && xmlStrcasecmp((unsigned char*)content,
								(unsigned char*)service_uri_scheme) == 0)
					{
						found = 1;
						LM_DBG("found service_uri_scheme= %s", service_uri_scheme);
						xmlFree(content);
						break;
					}	
					if(content)
						xmlFree(content);

				}

				provide_node = provide_node->next;
			}
			
			if(found == 0)
			{
				LM_DBG("delete node: %s\n", doc_node->name);
				dont_provide = doc_node;
				doc_node = doc_node->next;
				xmlUnlinkNode(dont_provide);	
				xmlFreeNode(dont_provide);
			}	
			else
				doc_node = doc_node->next;
	
		}
	}
done:
	xmlDocDumpMemory(doc,(xmlChar**)(void*)&new_body->s,
			&new_body->len);
	LM_DBG("body = \n%.*s\n", new_body->len,
			new_body->s);

    xmlFreeDoc(doc);

	xmlFree(class_cont);
	xmlFree(occurence_ID);
	xmlFree(deviceID);
	xmlFree(service_uri);
    xmlCleanupParser();
    xmlMemoryDump();

    return new_body;
error:
    if(doc)
		xmlFreeDoc(doc);
	if(new_body)
	{
		if(new_body->s)
			xmlFree(new_body->s);
		pkg_free(new_body);
	}
	if(class_cont)
		xmlFree(class_cont);
	if(occurence_ID)
		xmlFree(occurence_ID);
	if(deviceID)
		xmlFree(deviceID);
	if(service_uri)
		xmlFree(service_uri);

	return NULL;
}	

str* agregate_xmls(str* pres_user, str* pres_domain, str** body_array, int n)
{
	int i, j= 0, append ;
	xmlNodePtr p_root= NULL, new_p_root= NULL ;
	xmlDocPtr* xml_array ;
	xmlNodePtr node = NULL;
	xmlNodePtr add_node = NULL ;
	str *body= NULL;
	char* id= NULL, *tuple_id = NULL;
	xmlDocPtr pidf_manip_doc= NULL;
	str* pidf_doc= NULL;

	xml_array = (xmlDocPtr*)pkg_malloc( (n+2)*sizeof(xmlDocPtr));
	if(xml_array== NULL)
	{
	
		LM_ERR("while alocating memory");
		return NULL;
	}
	memset(xml_array, 0, (n+2)*sizeof(xmlDocPtr)) ;

	/* if pidf_manipulation usage is configured */
	if(pidf_manipulation)
	{
		if( get_rules_doc(pres_user, pres_domain, PIDF_MANIPULATION, &pidf_doc)< 0)
		{
			LM_ERR("while getting xcap tree for doc_type PIDF_MANIPULATION\n");
			goto error;
		}	
		if(pidf_doc== NULL)
		{
			LM_DBG("No PIDF_MANIPULATION doc for [user]= %.*s [domain]= %.*s\n"
			,pres_user->len, pres_user->s, pres_domain->len, pres_domain->s);
		}
		else
		{
			pidf_manip_doc= xmlParseMemory(pidf_doc->s, pidf_doc->len);
			pkg_free(pidf_doc->s);
			pkg_free(pidf_doc);

			if(pidf_manip_doc== NULL)
			{
				LM_ERR("parsing xml memory\n");
				goto error;
			}		
			else
			{	
				xml_array[0]= pidf_manip_doc;
				j++;
			}
		}
	}
	
	for(i=0; i<n; i++)
	{
		if(body_array[i] == NULL )
			continue;

		xml_array[j] = NULL;
		xml_array[j] = xmlParseMemory( body_array[i]->s, body_array[i]->len );
		LM_DBG("i = [%d] - body: %.*s\n", i,  body_array[i]->len, body_array[i]->s);

		if( xml_array[j]== NULL)
		{
			LM_ERR("while parsing xml body message\n");
			goto error;
		}
		j++;

	} 

	if(j== 0)  /* no body */
	{
		if(xml_array)
			pkg_free(xml_array);
		return NULL;
	}

	j--;
	p_root = xmlDocGetNodeByName( xml_array[j], "presence", NULL);
	if(p_root ==NULL)
	{
		LM_ERR("while geting the xml_tree root\n");
		goto error;
	}

	for(i= j-1; i>=0; i--)
	{
		LM_DBG("i = %d\n", i);

		new_p_root= xmlDocGetNodeByName( xml_array[i], "presence", NULL);
		if(new_p_root ==NULL)
		{
			LM_ERR("while geting the xml_tree root\n");
			goto error;
		}

		node= xmlNodeGetChildByName(new_p_root, "tuple");
		if(node== NULL)
		{
			LM_DBG("no tuple node found\n");
			append = 1;
			goto append_label;
		}
		tuple_id= xmlNodeGetAttrContentByName(node, "id");
		if(tuple_id== NULL)
		{
			LM_ERR("while extracting tuple id\n");
			goto error;
		}
		append= 1;
		for (node = p_root->children; node!=NULL; node = node->next)
		{		
			if( xmlStrcasecmp(node->name,(unsigned char*)"text")==0)
				continue;
			
			if( xmlStrcasecmp(node->name,(unsigned char*)"tuple")==0)
			{
				id = xmlNodeGetAttrContentByName(node, "id");
				if(id== NULL)
				{
					LM_ERR("while extracting tuple id\n");
					goto error;
				}
				
				if(xmlStrcasecmp((unsigned char*)tuple_id,
							(unsigned char*)id )== 0)
				{
					append = 0;
					xmlFree(id);
					break;
				}
				xmlFree(id);
			}
		}
		xmlFree(tuple_id);
		tuple_id= NULL;

append_label:
		if(append) 
		{
			LM_DBG("in if\n");
			for(node= new_p_root->children; node; node= node->next)
			{
				LM_DBG("adding node [%s]\n", node->name);
				add_node= xmlCopyNode(node, 1);
				if(add_node== NULL)
				{
					LM_ERR("while copying node [%s]\n", node->name);
					goto error;
				}
				if(add_node->ns != NULL)
				{
					/* dump redundant namespace added by xmlCopyNode */
					xmlFreeNsList(add_node->nsDef);
					add_node->nsDef = NULL;
					add_node->ns = NULL;
				}

				if(xmlAddChild(p_root, add_node)== NULL)
				{
					LM_ERR("while adding child\n");
					goto error;
				}
			}
		}
	}

	body = (str*)pkg_malloc(sizeof(str));
	if(body == NULL)
	{
		ERR_MEM(PKG_MEM_STR);
	}

	xmlDocDumpMemory(xml_array[j],(xmlChar**)(void*)&body->s, 
			&body->len);

	LM_DBG("body = %.*s\n", body->len, body->s);

  	for(i=0; i<=j; i++)
	{
		if(xml_array[i]!=NULL)
			xmlFreeDoc( xml_array[i]);
	}
	if(xml_array!=NULL)
		pkg_free(xml_array);
    
	xmlCleanupParser();
    xmlMemoryDump();

	return body;

error:
	if(xml_array!=NULL)
	{
		for(i=0; i<=j; i++)
		{
			if(xml_array[i]!=NULL)
				xmlFreeDoc( xml_array[i]);
		}
		pkg_free(xml_array);
	}
	if(tuple_id)
		xmlFree(tuple_id);
	if(body)
		pkg_free(body);

	return NULL;
}

str* offline_nbody(str* body)
{
	xmlDocPtr doc= NULL;
	xmlDocPtr new_doc= NULL;
	xmlNodePtr node, tuple_node= NULL, status_node;
	xmlNodePtr root_node, add_node, pres_node;
	str* new_body;

	doc= xmlParseMemory(body->s, body->len);
	if(doc==  NULL)
	{
		LM_ERR("while parsing xml memory\n");
		return NULL;
	}
	node= xmlDocGetNodeByName(doc, "basic", NULL);
	if(node== NULL)
	{
		LM_ERR("while extracting basic node\n");
		goto error;
	}
	xmlNodeSetContent(node, (const unsigned char*)"closed");

	tuple_node= xmlDocGetNodeByName(doc, "tuple", NULL);
	if(tuple_node== NULL)
	{
		LM_ERR("while extracting tuple node\n");
		goto error;
	}
	status_node= xmlDocGetNodeByName(doc, "status", NULL);
	if(status_node== NULL)
	{
		LM_ERR("while extracting tuple node\n");
		goto error;
	}

	pres_node= xmlDocGetNodeByName(doc, "presence", NULL);
	if(node== NULL)
	{
		LM_ERR("while extracting presence node\n");
		goto error;
	}

    new_doc = xmlNewDoc(BAD_CAST "1.0");
    if(new_doc==0)
		goto error;
	root_node= xmlCopyNode(pres_node, 2);
	if(root_node== NULL)
	{
		LM_ERR("while copying root node\n");
		goto error;
	}
	if(root_node->ns != NULL)
	{
		/* dump redundant namespace added by xmlCopyNode */
		xmlFreeNsList(root_node->nsDef);
		root_node->nsDef = NULL;
		root_node->ns = NULL;
	}

	xmlDocSetRootElement(new_doc, root_node);

	tuple_node= xmlCopyNode(tuple_node, 2);
	if(tuple_node== NULL)
	{
		LM_ERR("no tuple node found\n");
		goto error;
	}
	if(tuple_node->ns != NULL)
	{
		/* dump redundant namespace added by xmlCopyNode */
		xmlFreeNsList(tuple_node->nsDef);
		tuple_node->nsDef = NULL;
		tuple_node->ns = NULL;
	}


	xmlAddChild(root_node, tuple_node);

	add_node= xmlCopyNode(status_node, 1);
	if(add_node== NULL)
	{
		LM_ERR("while copying node\n");
		goto error;
	}
	if(add_node->ns != NULL)
	{
		/* dump redundant namespace added by xmlCopyNode */
		xmlFreeNsList(add_node->nsDef);
		add_node->nsDef = NULL;
		add_node->ns = NULL;
	}

	xmlAddChild(tuple_node, add_node);

	new_body = (str*)pkg_malloc(sizeof(str));
	if(new_body == NULL)
	{
		ERR_MEM(PKG_MEM_STR);
	}
	memset(new_body, 0, sizeof(str));

	xmlDocDumpMemory(new_doc,(xmlChar**)(void*)&new_body->s,
		&new_body->len);

	xmlFreeDoc(doc);
	xmlFreeDoc(new_doc);
	xmlCleanupParser();
	xmlMemoryDump();

	return new_body;

error:
	if(doc)
		xmlFreeDoc(doc);
	if(new_doc)
		xmlFreeDoc(new_doc);
	return NULL;

}		

