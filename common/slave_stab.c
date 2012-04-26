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

#define _LARGEFILE64_SOURCE
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>

#include "stability.h"
#include "holeycow.h"

/* Circular buffer with 2 tails. Invariant: sn <= rn */
//changed here block_t now is uint64_t
static block_t* buffer;
static int max, h;		/* capacity and head */
static int st, sn;		/* sender tail and size */
static int rt, rn;		/* receiver tail and size */

static pthread_mutex_t mux;
static pthread_cond_t notempty, notfull, ready;

static int sfd=-1, sock=-1;

static callback_t callback;
static void* cookie;

static pthread_t sender, receiver, pool;

/* Statistics */
int s_s_num, s_s_size;

static void* pool_thread(void* p) {
	pthread_mutex_lock(&mux);
	while(1) {
		int idx;

		while(rn==0 && sfd>=0)
			pthread_cond_wait(&notempty, &mux);

		if (sfd<0) {
			pthread_mutex_unlock(&mux);
			return NULL;
		}

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

		/* This loop will be exited on cleanup, failing on write below.
		   and returning. */
		while((sn==0 || buffer[st]!=-1) && sfd>=0)
			pthread_cond_wait(&ready, &mux);

		size=0;
		while(sn-size>0 && buffer[st+size]==-1)
			size++;

		st=(st+size)%max;
		sn-=size;

		pthread_cond_signal(&notfull);

		pthread_mutex_unlock(&mux);

		if (write(sock, &size, sizeof(size))!=sizeof(size)) {
			pthread_mutex_lock(&mux);
			shutdown(sock, SHUT_RD|SHUT_WR);
			close(sock);
			sock=-1;
			pthread_mutex_unlock(&mux);
			return NULL;
		}
	
		s_s_size+=size;
		s_s_num++;
	}
}

static void receiver_thread_loop() {
	pthread_mutex_lock(&mux);

	while(1) {

		int size=0;
		
		while(size==0) {
			size=max-sn;
			if (h+size>max)
				size=max-h;

			if (size==0)
				pthread_cond_wait(&notfull, &mux);
		}

		pthread_mutex_unlock(&mux);

		size=read(sock, buffer+h, size*sizeof(block_t));
	
		if(size <= 0 || (size%sizeof(block_t))) {
			pthread_mutex_lock(&mux);
			shutdown(sock, SHUT_RD|SHUT_WR);
			close(sock);
			sock=-1;
			pthread_mutex_unlock(&mux);
			return;
		}

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

	pthread_create(&sender, NULL, sender_thread, NULL);

	while(sfd>=0) {
		len=sizeof(master);
		memset(&master, 0, sizeof(master));

		sock = accept(sfd, (struct sockaddr*)&master, (socklen_t*)&len);
		if (sfd<0) {
			if (errno==EWOULDBLOCK) {
				sleep(1);
				continue;
			}
			break;
		}

		int flag=1;
		setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));

		receiver_thread_loop();
	}

	pthread_join(sender, NULL);

	return NULL;
}

void slave_stab(int s, int sz, int npool, callback_t cb, void* c) {
	int i;

    int flags;

    if (-1 == (flags = fcntl(s, F_GETFL, 0)))
        flags = 0;
    fcntl(s, F_SETFL, flags | O_NONBLOCK);

	max=sz;
	buffer=(uint64_t*)calloc(sizeof(uint64_t), max);

	callback=cb;
	cookie=c;

	sfd=s;

	pthread_mutex_init(&mux, NULL);
	pthread_cond_init(&notempty, NULL);
	pthread_cond_init(&notfull, NULL);
	pthread_cond_init(&ready, NULL);

	pthread_create(&receiver, NULL, receiver_thread, NULL);
	for(i=0;i<npool;i++)
		pthread_create(&pool, NULL, pool_thread, NULL);
}

void slave_stop() {
	if (sfd<0)
		return;

	pthread_mutex_lock(&mux);

	close(sfd);
	sfd=-1;
	shutdown(sock, SHUT_RD|SHUT_WR);
	close(sock);
	sock=-1;
	pthread_cond_signal(&ready);

	pthread_mutex_unlock(&mux);

	pthread_join(receiver, NULL);
}
