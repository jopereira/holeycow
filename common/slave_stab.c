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
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "stability.h"
#include "holeycow.h"

/* Circular buffer with 2 tails. Invariant: sn <= rn */
//changed here block_t now is uint64_t
static block_t* buffer;
static int max, h;		/* capacity and head */
static int st, sn;		/* sender tail and size */
static int rt, rn;		/* receiver tail and size */

static pthread_mutex_t mux;
static pthread_cond_t notempty, ready;

static int sfd, sock;

static callback_t callback;
static void* cookie;

static pthread_t sender, receiver, pool;

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

		write(sock, &size, sizeof(size));

		usleep(1000);
	}
}

static void receiver_thread_loop() {
	pthread_mutex_lock(&mux);

	while(1) {

		int size=max-sn;
		if (h+size>max)
			size=max-h;

		assert(size>0);

		pthread_mutex_unlock(&mux);

		size=read(sock, buffer+h, size*sizeof(block_t));
	
		if(size <= 0 || (size%sizeof(block_t)))
			return;
	
		size/=sizeof(block_t);
		h=(h+size)%max;

		pthread_mutex_lock(&mux);

		sn+=size;
		rn+=size;

		pthread_cond_broadcast(&notempty);
	}
}

static void* receiver_thread(void* p) {
	int len;
	struct sockaddr_in master;

	while(1) {
		len=sizeof(master);
		memset(&master, 0, sizeof(master));

		sock = accept(sfd, (struct sockaddr*)&master, (socklen_t*)&len);

		pthread_create(&sender, NULL, sender_thread, NULL);

		receiver_thread_loop();

		close(sock);
		sock=-1;

		pthread_join(sender, NULL);
	}
}

int slave_stab(int s, int sz, int npool, callback_t cb, void* c) {
	int i;

	max=sz;
	buffer=(uint64_t*)calloc(sizeof(uint64_t), max);

	callback=cb;
	cookie=c;

	sfd=s;

	pthread_mutex_init(&mux, NULL);
	pthread_cond_init(&notempty, NULL);
	pthread_cond_init(&ready, NULL);

	pthread_create(&receiver, NULL, receiver_thread, NULL);
	for(i=0;i<npool;i++)
		pthread_create(&pool, NULL, pool_thread, NULL);
}
