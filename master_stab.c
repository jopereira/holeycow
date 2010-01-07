/*
 * HoleyCoW - The holey copy-on-write library
 * Copyright (C) 2008 José Orlando Pereira, Luís Soares
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

#include <stdlib.h>
#include <pthread.h>
#include <assert.h>

#include "stability.h"
#include <unistd.h>

/* Circular buffer with 2 tails. Invariant: sn <= rn */
static block_t* buffer;
static int max, h;		/* capacity and head */
static int st, sn;		/* sender tail and size */
static int rt, rn;		/* receiver tail and size */
static int ft, fn;		/* fsync tail and size */
static int* sizes;		/* tokens from slaves */

static int eh, et;		/* epoch for sync (h=eh%max, ft=et%max) */

static int started = 0;

static pthread_mutex_t mux;
static pthread_cond_t notempty, ready, sync1;

static int* sock;

static callback_t callback;

static pthread_t sender, receiver, pool;
static int slaves;

extern unsigned long st_n_blks;
extern unsigned long st_n_msgs;

static void send(void* blocks, int size) {
	int i;
	for(i=0;i<slaves;i++) {
		int r=write(sock[i], blocks, size);
		assert(r>0);
	}
}

static void* sender_thread(void* p) {
	pthread_mutex_lock(&mux);

	while(1) {

                int size;
		while(sn==0)
			pthread_cond_wait(&notempty, &mux);

		size=sn>500?500:sn;
		if (st+size>max)
			size=max-st;

		pthread_mutex_unlock(&mux);

		st_n_blks+=size;
		st_n_msgs++;
		send(buffer+st, size*sizeof(int));

		/* printf("MASTER: stability sent %d blocks\n", size); */

		usleep(1000);

		pthread_mutex_lock(&mux);

		st=(st+size)%max;
		sn-=size;
	}
}

static int receive(int id) {
	int result, r=0;
	r=read(sock[id], &result, sizeof(result));
	assert(r>0);
	return result;
}

static void* receiver_thread(void* p) {
	int id=(int)p;

	while(1) {
		int i;
		int size=max;
		int v=receive(id);

		pthread_mutex_lock(&mux);

		sizes[id]+=v;

		for(i=0;i<slaves;i++) {
			if (sizes[i]<size)
				size=sizes[i];
		}

		/* printf("MASTER: stability received %d blocks\n", size); */

		if (size>0) {
			for(i=0;i<slaves;i++)
				sizes[i]-=size;

			rn+=size;

			pthread_cond_broadcast(&ready);
		}

		pthread_mutex_unlock(&mux);
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

		callback(buffer[idx]);

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

void master_stab(int s[], int nslaves, int sz, callback_t cb) {
	int i;

	max=sz;
	buffer=(int*)calloc(sizeof(int), max);

	callback=cb;

	slaves=nslaves;
	sock=s;

	for(i=0;i<nslaves;i++)
		write(sock[i], &i, sizeof(i));

	sizes=(int*)calloc(nslaves, sizeof(int));

	pthread_mutex_init(&mux, NULL);
	pthread_cond_init(&notempty, NULL);
	pthread_cond_init(&ready, NULL);
	pthread_cond_init(&sync1, NULL);
}

void master_start(int npool) {
	int i;

	pthread_create(&sender, NULL, sender_thread, NULL);
	for(i=0;i<slaves;i++)
		pthread_create(&receiver, NULL, receiver_thread, (void*)i);
	for(i=0;i<npool;i++)
		pthread_create(&pool, NULL, pool_thread, NULL);

	started = 1;
}

int add_block(block_t id) {
	int result;

	pthread_mutex_lock(&mux);

	assert(rn!=max);

	buffer[h]=id;
	sn++;
	h=(h+1)%max;

	eh++;

	result=eh-et;

	pthread_cond_signal(&notempty);

	pthread_mutex_unlock(&mux);

	return result;
}

void wait_sync(int dump) {
	int target;

	if(! started )
		return;

	pthread_mutex_lock(&mux);

	target=eh;
	while(et<eh)
		pthread_cond_wait(&sync1, &mux);
	
	pthread_mutex_unlock(&mux);
}
