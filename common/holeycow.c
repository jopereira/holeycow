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
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>

#include "holeycow.h"
#include "stability.h"

#define D(dev) ((struct holeycow_data*)(dev)->data)

#define STAB_QUEUE	1000

struct holeycow_data {
	/* common */
	pthread_mutex_t mutex_cow; 
	pthread_cond_t init; 
	int ready;

	struct device* storage;
	int *bitmap;
	FILE* ctrl;

	/* slave variables */
	int sfd;
	struct device* snapshot;
};

struct pending {
	struct device* dev;
	void* data;
	uint64_t offset;
	dev_callback_t cb;
	void* cookie;
};
 
static inline int test_and_set(struct device* dev, uint64_t id) {
		
	int result;
	uint64_t boff=(id&OFFMASK)>>FDBITS;
	
	uint64_t idx=boff/(8*sizeof(int));
	uint64_t mask=1LLU<<(boff%(8*sizeof(int))); 
			
	result=D(dev)->bitmap[idx]&mask;
	D(dev)->bitmap[idx]|=mask;
	return result;
}

static inline int test(struct device* dev, uint64_t id) {
	
	int result;
   	uint64_t boff=(id&OFFMASK)>>FDBITS;
	
	uint64_t idx=boff/(8*sizeof(int));
	uint64_t mask=1LLU<<(boff%(8*sizeof(int)));

	result=D(dev)->bitmap[idx]&mask;
	return result;
}

static int holeycow_close(struct device* dev) {

	/* TODO: assuming that reopen is on the same order */
	pthread_mutex_lock(&D(dev)->mutex_cow);
	
	device_close(D(dev)->storage);
	device_close(D(dev)->snapshot);

	// Shouldn't assume LIFO open/close order
	pthread_mutex_unlock(&D(dev)->mutex_cow);
	return 0;
}


/*************************************************************
 * MASTER Functions
 */

static void master_cb(block_t id, void* cookie) {
	struct pending* pend = (struct pending*) cookie; 

	pthread_mutex_lock(&D(pend->dev)->mutex_cow);
	test_and_set(pend->dev, pend->offset);
	pthread_mutex_unlock(&D(pend->dev)->mutex_cow);

	device_pwrite(D(pend->dev)->storage, pend->data, BLKSIZE, pend->offset, pend->cb, pend->cookie);
}

static void master_pwrite(struct device* dev, void* data, size_t count, off_t offset, dev_callback_t cb, void* cookie) {

	int done;
	int ft;
	int res;

	uint64_t id=offset&OFFMASK;
	uint64_t boff=offset>>FDBITS;

  	pthread_mutex_lock(&D(dev)->mutex_cow);
	int copied = test(dev, id);
	pthread_mutex_unlock(&D(dev)->mutex_cow);

	if (copied)
		 device_pwrite(D(dev)->storage, data, count, offset, cb, cookie);
	else {
		struct pending* pend=(struct pending*) malloc(sizeof(struct pending)); 
		pend->dev = dev;
		pend->data = data;
		pend->offset = offset;
		pend->cb = cb;
		pend->cookie = cookie;

		add_block(id, pend);
	}
}

static void master_pread(struct device* dev, void* data, size_t count, off_t offset, dev_callback_t cb, void* cookie) {
	device_pread(D(dev)->storage, data, count, offset, cb, cookie);
}

static struct device_ops master_device_ops = {
	master_pwrite,
	master_pread,
	holeycow_close
};

/*****************************************************
 * Slave Functions
 */

static void slave_cb(block_t id, void* cookie) {
	struct device* dev = (struct device*) cookie;
	char buffer[BLKSIZE];

  	pthread_mutex_lock(&D(dev)->mutex_cow);
	if (test(dev, id)) {
		pthread_mutex_unlock(&D(dev)->mutex_cow);
		return;
	}
	pthread_mutex_unlock(&D(dev)->mutex_cow);

	device_pread_sync(dev, buffer, BLKSIZE, id);

  	pthread_mutex_lock(&D(dev)->mutex_cow);

	if (!test_and_set(dev, id))
		device_pwrite_sync(D(dev)->snapshot, buffer, BLKSIZE, id);

	pthread_mutex_unlock(&D(dev)->mutex_cow);
}

static void slave_pwrite(struct device* dev, void* data, size_t count, off_t offset, dev_callback_t cb, void* cookie) {
  	pthread_mutex_lock(&D(dev)->mutex_cow);

	if (!test_and_set(dev, offset)) {

		int ret=device_pwrite_sync(D(dev)->snapshot, data, count, offset);

		pthread_mutex_unlock(&D(dev)->mutex_cow);

		cb(cookie, ret);
	} else {
		pthread_mutex_unlock(&D(dev)->mutex_cow);

		device_pwrite(D(dev)->snapshot, data, count, offset, cb, cookie);
	}
}

static void slave_pread(struct device* dev, void* data, size_t count, off_t offset, dev_callback_t cb, void* cookie) {
  	pthread_mutex_lock(&D(dev)->mutex_cow);
	int copied = test(dev, offset);
	pthread_mutex_unlock(&D(dev)->mutex_cow);

	if (copied)
		device_pread(D(dev)->snapshot, data, count, offset, cb, cookie);
	else
		device_pread(D(dev)->storage, data, count, offset, cb, cookie);
}

struct device_ops slave_device_ops = {
	slave_pwrite,
	slave_pread,
	holeycow_close
};


/*************************************************************
 * CONTROLLER Functions
 */

static int master_init(struct device* dev, int nslaves, struct sockaddr_in* slave) {
	int i, *fd;
	struct sockaddr_in master;

	fd=(int*)calloc(nslaves, sizeof(int));

	for(i=0;i<nslaves;i++) {
		fd[i]=socket(PF_INET, SOCK_STREAM, 0);

		if (connect(fd[i], (struct sockaddr*) slave+i, sizeof(struct sockaddr_in))<0) {
			perror("connect slave");
			exit(1);
		}
	}

	fprintf(D(dev)->ctrl, "copying\n");
	fflush(D(dev)->ctrl);:
		
	master_stab(fd, nslaves, STAB_QUEUE, master_cb);

  	pthread_mutex_lock(&D(dev)->mutex_cow);

	dev->ops = &master_device_ops;
	D(dev)->ready = 1;
	device_close(D(dev)->snapshot);
	D(dev)->snapshot=NULL;

	pthread_cond_broadcast(&D(dev)->init);
  	pthread_mutex_unlock(&D(dev)->mutex_cow);

	master_start(5);
}

static void pre_init(struct device* dev) {
	int len;
	struct sockaddr_in slave;

	D(dev)->sfd=socket(PF_INET, SOCK_STREAM, 0);

	listen(D(dev)->sfd, SOMAXCONN);

	len = sizeof(struct sockaddr_in);
	memset(&slave, 0, len);
	getsockname(D(dev)->sfd, (struct sockaddr *)&slave, &len);

	fprintf(D(dev)->ctrl, "ok %s %d\n", inet_ntoa(slave.sin_addr), ntohs(slave.sin_port));
	fflish(D(dev)->ctrl);
}

static void slave_init(struct device* dev) {
	int fd, len;
	struct sockaddr_in master;

	len=sizeof(master);
	memset(&master, 0, sizeof(master));

	fd = accept(D(dev)->sfd, (struct sockaddr*)&master, (socklen_t*)&len);
	slave_stab(fd, STAB_QUEUE, slave_cb, dev);

  	pthread_mutex_lock(&D(dev)->mutex_cow);
	dev->ops = &slave_device_ops;
	D(dev)->ready = 1;

	pthread_cond_broadcast(&D(dev)->init);
  	pthread_mutex_unlock(&D(dev)->mutex_cow);

	slave_start(5);
}

static void* ctrl_thread(void* arg) {
	struct device* dev = (struct device*) arg;
	char buffer[100];
	char* cmd[10], i, j;
	struct sockaddr_in slave[10];

	pre_init(dev);

	while(fgets(buffer, 100, D(dev)->ctrl)!=NULL) {
		i=0;
		cmd[i]=strtok(buffer, " \t\n");
		while(cmd[i]!=NULL)
			cmd[++i]=strtok(NULL, " \t\n");

		if (!strcmp(cmd[0], "makewriter")) {
			for(j=0;j<(i-1)/2;j++) {
				memset(slave+j, 0, sizeof(struct sockaddr_in));
				slave[j].sin_family = AF_INET;
				slave[j].sin_port = htons(atoi(cmd[j*2+2]));
				inet_aton(cmd[j*2+1], &slave[j].sin_addr);
			}
			master_init(dev, j, slave);
		} else if (!strcmp(cmd[0], "makecopier"))
			slave_init(dev);

	}

	// DANGER! Lost connection to controller.

	exit(1);
}

static void init_pwrite(struct device* dev, void* data, size_t count, off_t offset, dev_callback_t cb, void* cookie) {
  	pthread_mutex_lock(&D(dev)->mutex_cow);
	while(!D(dev)->ready)
		pthread_cond_wait(&D(dev)->init, &D(dev)->mutex_cow);
  	pthread_mutex_unlock(&D(dev)->mutex_cow);
	dev->ops->pwrite(dev, data, count, offset, cb, cookie);
}

static void init_pread(struct device* dev, void* data, size_t count, off_t offset, dev_callback_t cb, void* cookie) {
  	pthread_mutex_lock(&D(dev)->mutex_cow);
	while(!D(dev)->ready)
		pthread_cond_wait(&D(dev)->init, &D(dev)->mutex_cow);
  	pthread_mutex_unlock(&D(dev)->mutex_cow);
	dev->ops->pread(dev, data, count, offset, cb, cookie);
}

struct device_ops init_device_ops = {
	init_pwrite,
	init_pread,
	holeycow_close
};

/*************************************************************
 * API
 */

int holey_open(struct device* dev, struct device* storage, struct device* snapshot, uint64_t max_size, int ctrlfd) {
	pthread_t thread;

	dev->data = malloc(sizeof(struct holeycow_data));
	dev->ops = &init_device_ops;

	memset(dev->data, 0, sizeof(struct holeycow_data));
	pthread_mutex_init(&D(dev)->mutex_cow, NULL);
	pthread_cond_init(&D(dev)->init, NULL);
	D(dev)->storage = storage;
	D(dev)->snapshot = snapshot;
	D(dev)->ctrl = fdopen(ctrlfd, "r+");

	/* create the bitmap */
	D(dev)->bitmap=(int*)calloc((max_size/BLKSIZE)/8+sizeof(int), 1);

	pthread_create(&thread, NULL, ctrl_thread, dev);
	pthread_detach(thread);

	return 0;
}

