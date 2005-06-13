/*
 * $Id$
 *
 * Presence Agent, domain support
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
/*
 * History:
 * --------
 *  2003-03-11  converted to the new locking scheme: locking.h (andrei)
 */


#include "pdomain.h"
#include "paerrno.h"
#include "presentity.h"
#include "../../ut.h"
#include "../../dprint.h"
#include "../../mem/shm_mem.h"


/*
 * Hash function
 */
static inline int hash_func(pdomain_t* _d, char* _s, int _l)
{
	int res = 0, i;
	
	for(i = 0; i < _l; i++) {
		res += _s[i];
	}
	
	return res % _d->size;
}


/*
 * Create a new domain structure
 * _n is pointer to str representing
 * name of the domain, the string is
 * not copied, it should point to str
 * structure stored in domain list
 * _s is hash table size
 */
int new_pdomain(str* _n, int _s, pdomain_t** _d, register_watcher_t _r, unregister_watcher_t _u)
{
	int i;
	pdomain_t* ptr;
	
	ptr = (pdomain_t*)shm_malloc(sizeof(pdomain_t));
	if (!ptr) {
		paerrno = PA_NO_MEMORY;
		LOG(L_ERR, "new_pdomain(): No memory left\n");
		return -1;
	}
	memset(ptr, 0, sizeof(pdomain_t));
	
	ptr->table = (hslot_t*)shm_malloc(sizeof(hslot_t) * _s);
	if (!ptr->table) {
		paerrno = PA_NO_MEMORY;
		LOG(L_ERR, "new_pdomain(): No memory left 2\n");
		shm_free(ptr);
		return -2;
	}

	ptr->name = _n;
	
	for(i = 0; i < _s; i++) {
		init_slot(ptr, &ptr->table[i]);
	}

	ptr->size = _s;
	lock_init(&ptr->lock);
	ptr->users = 0;
	ptr->expired = 0;
	
	ptr->reg = _r;
	ptr->unreg = _u;
	*_d = ptr;
	return 0;
}


/*
 * Free all memory allocated for
 * the domain
 */
void free_pdomain(pdomain_t* _d)
{
	int i;
	
	lock_pdomain(_d);
	if (_d->table) {
		for(i = 0; i < _d->size; i++) {
			deinit_slot(_d->table + i);
		}
		shm_free(_d->table);
	}
	unlock_pdomain(_d);

        shm_free(_d);
}


/*
 * Just for debugging
 */
void print_pdomain(FILE* _f, pdomain_t* _d)
{
	struct presentity* p;

	fprintf(_f, "---pdomain---\n");
	fprintf(_f, "name : '%.*s'\n", _d->name->len, ZSW(_d->name->s));
	fprintf(_f, "size : %d\n", _d->size);
	fprintf(_f, "table: %p\n", _d->table);
	fprintf(_f, "first: %p\n", _d->first);
	fprintf(_f, "last : %p\n", _d->last);
	/* fprintf(_f, "lock : %d\n", _d->lock);*/ /*it can be a struct --andrei*/

	if (_d->first) {
		fprintf(_f, "\n");
		p = _d->first;
		while(p) {
			print_presentity(_f, p);
			p = p->next;
		}
		fprintf(_f, "\n");
	}

	fprintf(_f, "---pdomain---\n");
}


int timer_pdomain(pdomain_t* _d)
{
	struct presentity* presentity, *t;

	lock_pdomain(_d);

	presentity = _d->first;

	if (0) LOG(L_ERR, "timer_pdomain: _d=%s d->first=%p\n", _d->name->s, presentity);
	while(presentity) {
		if (timer_presentity(presentity) < 0) {
			LOG(L_ERR, "timer_pdomain(): Error in timer_pdomain\n");
			unlock_pdomain(_d);
			return -1;
		}
		
		     /* Remove the entire record
		      * if it is empty
		      */
		if (presentity->watchers == 0 && presentity->winfo_watchers==0) {
			t = presentity;
			presentity = presentity->next;
			remove_presentity(_d, t);
			free_presentity(t);
		} else {
			presentity = presentity->next;
		}
	}
	
	unlock_pdomain(_d);
	return 0;
}


static int in_pdomain = 0; /* this only works with single or multiprocess execution model, but not multi-threaded */

/*
 * Get lock if this process does not already have it
 */
void lock_pdomain(pdomain_t* _d)
{
	DBG("lock_pdomain\n");
	if (!in_pdomain++)
	     lock_get(&_d->lock);
}


/*
 * Release lock
 */
void unlock_pdomain(pdomain_t* _d)
{
	DBG("unlock_pdomain\n");
	in_pdomain--;
	if (!in_pdomain)
	     lock_release(&_d->lock);
}


/*
 * Find a presentity in domain
 */
int find_presentity(pdomain_t* _d, str* _uri, struct presentity** _p)
{
	int sl, i;
	struct presentity* p;
	
	if (!_d->first) {
	     pdomain_load_presentities(_d);
	}

	sl = hash_func(_d, _uri->s, _uri->len);
	
	p = _d->table[sl].first;
	
	for(i = 0; i < _d->table[sl].n; i++) {
		if ((p->uri.len == _uri->len) && !memcmp(p->uri.s, _uri->s, _uri->len)) {
			*_p = p;
			return 0;
		}
		
		p = p->next;
	}

	return 1;   /* Nothing found */
}


void add_presentity(pdomain_t* _d, struct presentity* _p)
{
	int sl;

	LOG(L_WARN, "add_presentity _p=%p p_uri=%.*s\n", _p, _p->uri.len, _p->uri.s);

	sl = hash_func(_d, _p->uri.s, _p->uri.len);

	slot_add(&_d->table[sl], _p, &_d->first, &_d->last);
}


void remove_presentity(pdomain_t* _d, struct presentity* _p)
{
	return;
	LOG(L_WARN, "remove_presentity _p=%p p_uri=%.*s\n", _p, _p->uri.len, _p->uri.s);
	slot_rem(_p->slot, _p, &_d->first, &_d->last);
}
