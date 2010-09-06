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

#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>

#include "stability.h"
#include "holeycow.h"
#include <unistd.h>

/* Circular buffer with 2 tails. Invariant: sn <= rn */
//changed here block_t now is uint64_t
static block_t* buffer;
static int max, h;		/* capacity and head */
static int st, sn;		/* sender tail and size */
static int rt, rn;		/* receiver tail and size */

static pthread_mutex_t mux;
static pthread_cond_t notempty, ready;

static int sock;

static callback_t callback;
static void* cookie;

static pthread_t sender, receiver, pool;

static int receive(void* buffer, int size) {
	return read(sock, buffer, size);
}

static void* receiver_thread(void* p) {
	pthread_mutex_lock(&mux);

	while(1) {

		int size=max-sn;
		if (h+size>max)
			size=max-h;

		assert(size>0);

		pthread_mutex_unlock(&mux);

		size=receive(buffer+h, size*sizeof(uint64_t));
	
		if(size == 0) {		/* closed socket */
			/* TODO: flush? return error? what? */
			fprintf(stderr, " *** Bailing out! Master closed socket! *** \n");
			exit(0);
		}
	
		assert(size>0 && !(size%sizeof(uint64_t)));
		
		size/=sizeof(uint64_t);
		h=(h+size)%max;

		pthread_mutex_lock(&mux);

		sn+=size;
		rn+=size;

		pthread_cond_broadcast(&notempty);
	}
}

static void* pool_thread(void* p) {
	pthread_mutex_lock(&mux);
	while(1) {

                int idx;
		while(rn==0)
			pthread_cond_wait(&notempty, &mux);

		idx=rt;
		rt=(rt+1)%max;
		rn--;

		pthread_mutex_unlock(&mux);

		callback(buffer[idx], cookie);

		pthread_mutex_lock(&mux);

		buffer[idx]=-1;
		if (idx==st)
			pthread_cond_signal(&ready);
	}
}

static void send(int size) {
	write(sock, &size, sizeof(size));
}

static void* sender_thread(void* p) {
	while(1) {
                int size;
 		pthread_mutex_lock(&mux);

		while(sn==0 || buffer[st]!=-1)
			pthread_cond_wait(&ready, &mux);

		size=0;
		while(sn-size>0 && buffer[st+size]==-1)
			size++;

		st=(st+size)%max;
		sn-=size;

		pthread_mutex_unlock(&mux);

		send(size);

		usleep(1000);
	}
}

int slave_stab(int s, int sz, callback_t cb, void* c) {
	int i, id;

	max=sz;
	buffer=(uint64_t*)calloc(sizeof(uint64_t), max);

	callback=cb;
	cookie=c;

	sock=s;

	read(sock, &id, sizeof(id));

	pthread_mutex_init(&mux, NULL);
	pthread_cond_init(&notempty, NULL);
	pthread_cond_init(&ready, NULL);

	return id;
}

void slave_start(int npool) {
	int i;

	pthread_create(&sender, NULL, sender_thread, NULL);
	pthread_create(&receiver, NULL, receiver_thread, NULL);
	for(i=0;i<npool;i++)
		pthread_create(&pool, NULL, pool_thread, NULL);
}

