/*
 * HoleyCoW
 * Copyright (c) 2008-2010 Universidade do Minho
 * Written by José Pereira, Luis Soares, and J. Paulo
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <sys/types.h> 
#include <sys/socket.h> 
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "holeycow.h"
#include "stability.h"

/* Circular buffer with 2 tails. Invariant: sn <= rn */
static block_t* buffer;
static void** cookiejar;
static int max, h;		/* capacity and head */
static int st, sn;		/* sender tail and size */
static int rt, rn;		/* receiver tail and size */
static int ft, fn;		/* fsync tail and size */

static struct slave {
	int dead;
	int ssize, rsize;	/* tokens to/from slaves */
	int sock;
	struct sockaddr_in* addr;
	pthread_t sender;
	struct slave* next;
} *slaves;

static int eh, et;		/* epoch for sync (h=eh%max, ft=et%max) */

static int started = 0;

static pthread_mutex_t mux;
static pthread_cond_t notempty, ready, sync1;

static callback_t callback;

static pthread_t pool;

static void* receiver_thread(void* param) {
	struct slave* me=(struct slave*)param, *p;

	while(1) {
		int i;
		int size=max;
		int v;

		if (read(me->sock, &v, sizeof(v))!=sizeof(v)) {
			pthread_mutex_lock(&mux);
			close(me->sock);
			me->sock=-1;
			pthread_mutex_unlock(&mux);
			return NULL;
		}

		pthread_mutex_lock(&mux);

		me->rsize += v;

		for(p=slaves; p!=NULL; p=p->next)
			if (p->rsize<size)
				size=p->rsize;

		if (size>0) {
			for(p=slaves; p!=NULL; p=p->next)
				p->rsize-=size;

			rn+=size;

			pthread_cond_broadcast(&ready);
		}

		pthread_mutex_unlock(&mux);
	}
}

static void gc_slaves() {
	struct slave *p;
	int size;

	if (slaves==NULL)
		return;

	size=max;
	for(p=slaves; p!=NULL; p=p->next)
		if (p->ssize<size)
			size=p->ssize;

	if (size>0) {
		for(p=slaves; p!=NULL; p=p->next)
			p->ssize-=size;

		st=(st+size)%max;
		sn-=size;

		assert(sn>=0);
	}

}

static void sender_thread_loop(struct slave* me) {
	pthread_mutex_lock(&mux);

	while(1) {

		int size, v, est;

		while(sn-me->ssize==0 && me->sock!=-1) {
			pthread_cond_wait(&notempty, &mux);
		}

		est=(st+me->ssize)%max;
                
		size=sn>500?500:sn-me->ssize;
		if (est+size>max)
			size=max-est;

		pthread_mutex_unlock(&mux);

		v=write(me->sock, buffer+est, size*sizeof(uint64_t));
		if (v<=0 || (v%sizeof(uint64_t))) {
			pthread_mutex_lock(&mux);
			close(me->sock);
			me->sock=-1;
			pthread_mutex_unlock(&mux);
			return;
		}

		usleep(1000);

		pthread_mutex_lock(&mux);

		me->ssize += v/sizeof(uint64_t);

		gc_slaves();
	}
}

static void* sender_thread(void* param) {
	struct slave* me=(struct slave*)param;
	static pthread_t receiver;

	while(!me->dead) {
		me->sock=socket(PF_INET, SOCK_STREAM, 0);
		if (connect(me->sock, (struct sockaddr*) me->addr, sizeof(struct sockaddr_in))<0) {
			sleep(1);
			continue;
		}

		pthread_create(&receiver, NULL, receiver_thread, me);

		sender_thread_loop(me);

		pthread_join(receiver, NULL);
	}
}

static void* pool_thread(void* p) {
       
	pthread_mutex_lock(&mux);
	while(1) {

		int idx;
		while(rn==0)
			pthread_cond_wait(&ready, &mux);
                      
		idx=rt;
		rt=(rt+1)%max;
		rn--;

		fn++;

		pthread_mutex_unlock(&mux);

		callback(buffer[idx], cookiejar[idx]);

		pthread_mutex_lock(&mux);

		buffer[idx]=-1;

		while(fn>0 && buffer[ft]==-1) {
			fn--;
			ft=(ft+1)%max;

			et++;
		}
		pthread_cond_broadcast(&sync1);
	}
}

void master_stab(int sz, callback_t cb, int npool) {
	int i;

	max=sz;
	buffer=(uint64_t*)calloc(sizeof(uint64_t), max);
	cookiejar=(void**)calloc(sizeof(void*), max);

	callback=cb;

	pthread_mutex_init(&mux, NULL);
	pthread_cond_init(&notempty, NULL);
	pthread_cond_init(&ready, NULL);
	pthread_cond_init(&sync1, NULL);

	for(i=0;i<npool;i++)
		pthread_create(&pool, NULL, pool_thread, NULL);

	started = 1;
}

void add_slave(struct sockaddr_in* addr) {
	struct slave* p;

	pthread_mutex_lock(&mux);

	assert(rn == 0 && sn == 0 && fn == 0);

	p = (struct slave*) malloc(sizeof(struct slave));
	memset(p, 0, sizeof(*p));
	p->sock = -1;
	p->rsize = 0;
	p->addr = addr;
	p->next = slaves;
	slaves = p;

	pthread_create(&p->sender, NULL, sender_thread, p);

	pthread_mutex_unlock(&mux);
}

void del_slave(struct sockaddr_in* addr) {
	struct slave** p, *d;

	pthread_mutex_lock(&mux);

	for(p=&slaves;*p!=NULL;p=&(*p)->next)
		if ((*p)->addr->sin_addr.s_addr == addr->sin_addr.s_addr)
			break;

	assert(*p!=NULL);

	d=*p;
	*p=d->next;

	close(d->sock);
	d->sock=-1;
	d->dead=1;
	d->next=NULL;

	pthread_cond_broadcast(&notempty);
	gc_slaves();

	pthread_mutex_unlock(&mux);

	pthread_join(d->sender, NULL);
	free(d);
}

int add_block(block_t id, void* cookie) {
	int result;

	pthread_mutex_lock(&mux);

	assert(rn!=max);

	buffer[h]=id;
	cookiejar[h]=cookie;
	sn++;
	h=(h+1)%max;

	eh++;

	result=eh-et;

	pthread_cond_broadcast(&notempty);

	if (slaves==0) {
		/* Fake send */
		sn--;
		st=(st+1)%max;

		/* Fake receive */
		rn++;
		pthread_cond_signal(&ready);
	}

	pthread_mutex_unlock(&mux);

	return result;
}

void wait_sync(int dump) {
	int target;

	if(! started )
		return;

	pthread_mutex_lock(&mux);

	target=eh;
	while(et<target)
		pthread_cond_wait(&sync1, &mux);
	
	pthread_mutex_unlock(&mux);
}
