/*
 * openser osp module. 
 *
 * This module enables openser to communicate with an Open Settlement 
 * Protocol (OSP) server.  The Open Settlement Protocol is an ETSI 
 * defined standard for Inter-Domain VoIP pricing, authorization
 * and usage exchange.  The technical specifications for OSP 
 * (ETSI TS 101 321 V4.1.1) are available at www.etsi.org.
 *
 * Uli Abend was the original contributor to this module.
 * 
 * Copyright (C) 2001-2005 Fhg Fokus
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
 * ---------
 *  2006-03-13  TM functions are loaded via API function (bogdan)
 */





#include "tm.h"
#include "destination.h"
#include "usage.h"
#include "../tm/tm_load.h"

static struct tm_binds osp_tmb;


static void onreq( struct cell* t, int type, struct tmcb_params *ps );
static void tmcb_func( struct cell* t, int type, struct tmcb_params *ps );


int mod_init_tm()
{
	LOG(L_INFO, "osp/tm - initializing\n");

	if (load_tm_api(&osp_tmb)!=0) {
		LOG(L_ERR, "ERROR:osp:mod_init_tm: can't load TM API\n");
		LOG(L_ERR,"ERROR:osp:mod_init_tm: tm is required for reporting "
			"call set up usage info\n");
		return -1;
	}

	/* register callbacks*/
	/* listen for all incoming requests  */
	if ( osp_tmb.register_tmcb( 0, 0, TMCB_REQUEST_IN, onreq, 0 ) <=0 ) {
		LOG(L_ERR,"ERROR:osp:mod_init_tm: cannot register TMCB_REQUEST_IN "
			"callback\n");
		LOG(L_ERR,"ERROR:osp:mod_init_tm: tm callbacks are required for "
			"reporting call set up usage info\n");
		return -1;
	}

	return 0;
}

static void onreq( struct cell* t, int type, struct tmcb_params *ps )
{
	int tmcb_types;

	DBG("osp: onreq: Registering transaction call backs\n\n");

	/* install addaitional handlers */
	tmcb_types =
		//TMCB_REQUEST_FWDED | 
		//TMCB_RESPONSE_FWDED | 
		TMCB_ON_FAILURE | 
		//TMCB_LOCAL_COMPLETED  |
		/* report on completed transactions */
		TMCB_RESPONSE_OUT |
		/* account e2e acks if configured to do so */
		TMCB_E2EACK_IN |
		/* report on missed calls */
		TMCB_ON_FAILURE_RO //|
		/* get incoming replies ready for processing */
		//TMCB_RESPONSE_IN
		;

	if (osp_tmb.register_tmcb( 0, t, tmcb_types, tmcb_func, 0 )<=0) {
		LOG(L_ERR,"ERROR:osp:onreq: cannot register for tm callbacks\n");
		LOG(L_ERR,"ERROR:osp:onreq: tm callbacks are required for reporting "
			"call set up usage info\n");
		return;
	}

	/* also, if that is INVITE, disallow silent t-drop */
	if (ps->req->REQ_METHOD==METHOD_INVITE) {
		DBG("DEBUG: noisy_timer set for accounting\n");
		t->flags |= T_NOISY_CTIMER_FLAG;
	}
}



static void tmcb_func( struct cell* t, int type, struct tmcb_params *ps )
{
        if (type&TMCB_RESPONSE_OUT) {
                DBG("osp:tmcb: on-RESPONSE_OUT-out\n");
        } else if (type&TMCB_E2EACK_IN) {
                DBG("osp:tmcb: on-E2EACK_IN\n");
        } else if (type&TMCB_ON_FAILURE_RO) {
                DBG("osp:tmcb: on-FAILURE_RO\n");
        } else if (type&TMCB_RESPONSE_IN) {
                DBG("osp:tmcb: on-RESPONSE_IN\n");
        } else if (type&TMCB_REQUEST_FWDED) {
                DBG("osp:tmcb: on-REQUEST_FWDED\n");
        } else if (type&TMCB_RESPONSE_FWDED) {
                DBG("osp:tmcb: on-RESPONSE_FWDED\n");
        } else if (type&TMCB_ON_FAILURE) {
                DBG("osp:tmcb: on-FAILURE\n");
        } else if (type&TMCB_LOCAL_COMPLETED) {
                DBG("osp:tmcb: on-COMPLETED\n");
        } else {
                DBG("osp:tmcb: on-something-else: %d\n",type);
        }

	if (t) {
		recordEvent(t->uac[t->first_branch].last_received,t->uas.status);
	} else {
                DBG("osp:tmcb: cell is empty\n");
	}
}


