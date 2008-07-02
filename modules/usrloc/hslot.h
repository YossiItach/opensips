/* 
 * $Id$ 
 *
 * Usrloc hash table collision slot
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

/*! \file
 *  \brief USRLOC - Hash table collision slot related functions
 *  \ingroup usrloc
 */



#ifndef HSLOT_H
#define HSLOT_H

#include "../../locking.h"

#include "udomain.h"
#include "urecord.h"


struct udomain;
struct urecord;


typedef struct hslot {
	int n;                  /*!< Number of elements in the collision slot */
	struct urecord* first;  /*!< First element in the list */
	struct urecord* last;   /*!< Last element in the list */
	struct udomain* d;      /*!< Domain we belong to */
#ifdef GEN_LOCK_T_PREFERED
	gen_lock_t *lock;       /*!< Lock for hash entry - fastlock */
#else
	int lockidx;            /*!< Lock index for hash entry - the rest*/
#endif
} hslot_t;

/*! \brief
 * Initialize slot structure
 */
int init_slot(struct udomain* _d, hslot_t* _s, int n);


/*! \brief
 * Deinitialize given slot structure
 */
void deinit_slot(hslot_t* _s);


/*! \brief
 * Add an element to slot linked list
 */
void slot_add(hslot_t* _s, struct urecord* _r);


/*! \brief
 * Remove an element from slot linked list
 */
void slot_rem(hslot_t* _s, struct urecord* _r);

int ul_init_locks();
void ul_unlock_locks();
void ul_destroy_locks();

#ifndef GEN_LOCK_T_PREFERED
void ul_lock_idx(int idx);
void ul_release_idx(int idx);
#endif

#endif /* HSLOT_H */
