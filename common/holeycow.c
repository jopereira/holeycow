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
#include <sys/types.h> 
#include <sys/socket.h> 
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <malloc.h>

#include "holeycow.h"
#include "stability.h"

#define D(dev) ((struct holeycow_data*)(dev)->data)

#define STAB_QUEUE	4000

#define MAXLINE 200

struct holeycow_data {
	/* common */
	pthread_mutex_t mutex_cow; 
	pthread_cond_t init;
	int ready;

	struct device* storage;
	int *bitmap, *transit;
	uint64_t max_size;
	int ctrlfd;
	char inbuf[MAXLINE], outbuf[MAXLINE];
	int incnt;
	char* ptr;

	/* master variables */
	pthread_cond_t blocked;
	int pw;

	/* slave variables */
	int sfd;
	int pc;
	struct device* snapshot;
	pthread_cond_t cond_transit;

	/* statistics */
	/* storage: reads, writes, delayed writes */
	int s_str, s_stw, s_stdw;
	/* snapshot: reads, writes, forced reads */
	int s_ssr, s_ssw, s_stfr;
};

struct pending {
	struct device* dev;
	void* data;
	size_t count;
	uint64_t offset;
	int blocks;
	int ret;
	dev_callback_t cb;
	void* cookie;
};
 
static inline int test_and_set(struct device* dev, uint64_t id) {
		
	int result;
	uint64_t boff=(id&OFFMASK)>>FDBITS;
	
	uint64_t idx=boff/(8*sizeof(int));
	int mask=1<<(boff%(8*sizeof(int))); 
			
	result=D(dev)->bitmap[idx]&mask;
	D(dev)->bitmap[idx]|=mask;
	return result;
}

static inline int test(struct device* dev, uint64_t id) {
	
	int result;
   	uint64_t boff=(id&OFFMASK)>>FDBITS;
	
	uint64_t idx=boff/(8*sizeof(int));
	int mask=1<<(boff%(8*sizeof(int)));

	result=D(dev)->bitmap[idx]&mask;
	return result;
}

static inline int rec_test(int* bitmap, uint64_t id) {

	int result;
   	uint64_t boff=(id&OFFMASK)>>FDBITS;

	uint64_t idx=boff/(8*sizeof(int));
	int mask=1<<(boff%(8*sizeof(int)));

	result=bitmap[idx]&mask;
	return result;
}

static inline void start_transit(struct device* dev, uint64_t id) {
		
	uint64_t boff=(id&OFFMASK)>>FDBITS;
	
	uint64_t idx=boff/(8*sizeof(int));
	int mask=1<<(boff%(8*sizeof(int))); 
			
	D(dev)->transit[idx]|=mask;
}

static inline void end_transit(struct device* dev, uint64_t id) {
		
	uint64_t boff=(id&OFFMASK)>>FDBITS;
	
	uint64_t idx=boff/(8*sizeof(int));
	int mask=1<<(boff%(8*sizeof(int))); 
			
	D(dev)->transit[idx]&=~mask;
	pthread_cond_broadcast(&D(dev)->cond_transit);
}

static inline void wait_transit(struct device* dev, uint64_t id) {
	
   	uint64_t boff=(id&OFFMASK)>>FDBITS;
	
	uint64_t idx=boff/(8*sizeof(int));
	int mask=1<<(boff%(8*sizeof(int)));

	while(D(dev)->transit[idx]&mask)
		pthread_cond_wait(&D(dev)->cond_transit, &D(dev)->mutex_cow);
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

static void master_end_write_cb(void* cookie, int ret) {
	struct pending* pend = (struct pending*) cookie; 

	assert(ret>=0);

	pthread_mutex_lock(&D(pend->dev)->mutex_cow);
	if (--D(pend->dev)->pw == 0)
		pthread_cond_broadcast(&D(pend->dev)->blocked);
	pthread_mutex_unlock(&D(pend->dev)->mutex_cow);

	pend->cb(pend->cookie, ret);
	free(pend);
}

static void master_delayed_write_cb(block_t id, int idx, void* cookie) {
	struct pending* pend = (struct pending*) cookie; 

	int done = 0;

	pthread_mutex_lock(&D(pend->dev)->mutex_cow);
	test_and_set(pend->dev, pend->offset);
	D(pend->dev)->s_stw++;
	if (--pend->blocks == 0)
		done = 1;
	pthread_mutex_unlock(&D(pend->dev)->mutex_cow);

	if (!done)
		return;

	device_pwrite(D(pend->dev)->storage, pend->data, pend->count, pend->offset, master_end_write_cb, pend);
}

static void master_pwrite(struct device* dev, void* data, size_t count, off64_t offset, dev_callback_t cb, void* cookie) {

	size_t done=0;
	int ft;
	int res;

	uint64_t boff=offset>>FDBITS;

	struct pending* pend=(struct pending*) malloc(sizeof(struct pending)); 
	pend->dev = dev;
	pend->data = data;
	pend->offset = offset;
	pend->count = count;
	pend->blocks = 1;
	pend->cb = cb;
	pend->cookie = cookie;

  	pthread_mutex_lock(&D(dev)->mutex_cow);
	D(dev)->pw++;
	while(done<count) {
		uint64_t id=(offset+done)&OFFMASK;
		if (!test(dev, id)) {
			D(dev)->s_stdw++;
			pend->blocks++;
  			pthread_mutex_unlock(&D(dev)->mutex_cow);
			add_block(id, pend);
  			pthread_mutex_lock(&D(dev)->mutex_cow);
		} else
			D(dev)->s_stw++;
		done+=BLKSIZE;
	}

	if (--pend->blocks==0) {
		pthread_mutex_unlock(&D(dev)->mutex_cow);
		device_pwrite(D(dev)->storage, data, count, offset, master_end_write_cb, pend);
	} else
		pthread_mutex_unlock(&D(dev)->mutex_cow);
}

static void master_pread(struct device* dev, void* data, size_t count, off64_t offset, dev_callback_t cb, void* cookie) {
	D(dev)->s_str+=count/BLKSIZE;
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

struct delayed_cow {
	char buffer[BLKSIZE];
	struct device* dev;
	block_t id;
	int idx;
};

static void slave_end_cow_cb(void* cookie, int ret) {
	struct delayed_cow* cow = (struct delayed_cow*) cookie;

	pthread_mutex_lock(&D(cow->dev)->mutex_cow);
	end_transit(cow->dev, cow->id);
	if (!test_and_set(cow->dev, cow->id))
		D(cow->dev)->pc++;
	pthread_mutex_unlock(&D(cow->dev)->mutex_cow);

	free(cow);
}

static void slave_done_cow_cb(void* cookie, int ret) {
	struct delayed_cow* cow = (struct delayed_cow*) cookie;

	/* Ack the writer, as we already hold a copy in memory. */
	done_block(cow->idx);

  	pthread_mutex_lock(&D(cow->dev)->mutex_cow);
	/* There can be concurrent CoWs to the same block. */
	wait_transit(cow->dev, cow->id);
	if (test(cow->dev, cow->id)) {
		pthread_mutex_unlock(&D(cow->dev)->mutex_cow);
		free(cow);
		return;
	}

	/* Not in transit and not CoWed. Do it now. */
	start_transit(cow->dev, cow->id);
  	pthread_mutex_unlock(&D(cow->dev)->mutex_cow);

	device_pwrite(D(cow->dev)->snapshot, cow->buffer, BLKSIZE, cow->id, slave_end_cow_cb, cow);
}

static void slave_cow_cb(block_t id, int idx, void* cookie) {
	struct device* dev = (struct device*) cookie;

  	pthread_mutex_lock(&D(dev)->mutex_cow);
	if (test(dev, id)) {
		pthread_mutex_unlock(&D(dev)->mutex_cow);

		/* Already done. */
		done_block(idx);
		return;
	}
	D(dev)->s_stfr++;
  	pthread_mutex_unlock(&D(dev)->mutex_cow);

	struct delayed_cow* cow = (struct delayed_cow*) memalign(512, sizeof(struct delayed_cow));
	cow->dev = dev;
	cow->id = id;
	cow->idx = idx;
	device_pread(D(dev)->storage, cow->buffer, BLKSIZE, id, slave_done_cow_cb, cow);
}

static void slave_pwrite(struct device* dev, void* data, size_t count,  off64_t offset, dev_callback_t cb, void* cookie) {
  	pthread_mutex_lock(&D(dev)->mutex_cow);
	int bcount;
	for(bcount = 0;bcount<count;bcount+=BLKSIZE) {
		/* As there are no concurrent writes to the same block, it can only
		 * be in transit if previously unmarked. */
		if (!test_and_set(dev, offset+bcount)) {
			D(dev)->pc++;
			wait_transit(dev, offset+bcount);
		}
		D(dev)->s_ssw++;
	}
  	pthread_mutex_unlock(&D(dev)->mutex_cow);

	device_pwrite(D(dev)->snapshot, data, count, offset, cb, cookie);
}

static void slave_end_read_cb(void* cookie, int ret) {
	struct pending* pend = (struct pending*) cookie; 
	int done = 0;

	pthread_mutex_lock(&D(pend->dev)->mutex_cow);
	if (ret<0 || pend->ret<0)
		pend->ret = -1;
	else
		pend->ret+=ret;
	if (--pend->blocks == 0)
		done = 1;
	pthread_mutex_unlock(&D(pend->dev)->mutex_cow);

	if (!done)
		return;

	pend->cb(pend->cookie, pend->ret);
	free(pend);
}

static void slave_pread(struct device* dev, void* data, size_t count, off64_t offset, dev_callback_t cb, void* cookie) {
	struct pending* pend = NULL;
	struct device* target;
	int ccount, bcount = 0;

	while(bcount<count) {
		/* Gather a contigous chunk to the same destination */

  		pthread_mutex_lock(&D(dev)->mutex_cow);
		ccount = 0;
		if (test(dev, offset+bcount)) {
			while(bcount+ccount<count && test(dev, offset+bcount)) {
				ccount+=BLKSIZE;
				D(dev)->s_ssr++;
			}

			target = D(dev)->snapshot;
		} else {
			while(bcount+ccount<count && !test(dev, offset+bcount)) {
				ccount+=BLKSIZE;
				D(dev)->s_str++;
			}

			target = D(dev)->storage;
		}

		/* Read it all at once */

		if (ccount == count) {
			/* Fast path: no splitting */
  			pthread_mutex_unlock(&D(dev)->mutex_cow);
			device_pread(target, data, count, offset, cb, cookie);
			return;
		}
		if (pend == NULL) {
			pend = (struct pending*) malloc(sizeof(struct pending));
			pend->dev = dev;
			pend->data = data;
			pend->offset = offset;
			pend->blocks = 1;
			pend->ret = 0;
			pend->cb = cb;
			pend->cookie = cookie;
		}
		pend->blocks++;
  		pthread_mutex_unlock(&D(dev)->mutex_cow);

		device_pread(target, data, ccount, offset+bcount, slave_end_read_cb, pend);

		bcount += ccount;
	}
	if (pend!=NULL)
		slave_end_read_cb(pend, 0);
}

struct device_ops slave_device_ops = {
	slave_pwrite,
	slave_pread,
	holeycow_close
};

/*************************************************************
 * Blocked state
 */

static void init_pwrite(struct device* dev, void* data, size_t count, off64_t offset, dev_callback_t cb, void* cookie) {
  	pthread_mutex_lock(&D(dev)->mutex_cow);
	while(!D(dev)->ready)
		pthread_cond_wait(&D(dev)->init, &D(dev)->mutex_cow);
  	pthread_mutex_unlock(&D(dev)->mutex_cow);
	dev->ops->pwrite(dev, data, count, offset, cb, cookie);
}

static void init_pread(struct device* dev, void* data, size_t count, off64_t offset, dev_callback_t cb, void* cookie) {
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
 * Recovery
 */

struct recovery_data {
	struct device* dev;
	off64_t offset;
	char* buffer;
};

static void recover_write_cb(void* cookie, int ret) {
	struct recovery_data* data = (struct recovery_data*) cookie;
	assert(ret==BLKSIZE);

  	pthread_mutex_lock(&D(data->dev)->mutex_cow);
	D(data->dev)->pc--;
	pthread_cond_broadcast(&D(data->dev)->cond_transit);
  	pthread_mutex_unlock(&D(data->dev)->mutex_cow);

	free(data->buffer);
	free(data);
}

static void recover_read_cb(void* cookie, int ret) {
	struct recovery_data* data=(struct recovery_data*)cookie;

	assert(ret==BLKSIZE);

	master_pwrite(data->dev, data->buffer, BLKSIZE, data->offset, recover_write_cb, cookie);
}

/*************************************************************
 * CONTROLLER Functions
 */

static int ctrl_read(struct device* dev, char* buf) {
	while(1) {
		while(D(dev)->incnt) {
			D(dev)->incnt--;
			*buf = *D(dev)->ptr++;
			if (*buf == '\n') {
				*buf = 0;
				return 1;
			}
			buf++;
		}
		D(dev)->ptr = D(dev)->inbuf;
		D(dev)->incnt=read(D(dev)->ctrlfd,D(dev)->inbuf,MAXLINE);
		if (D(dev)->incnt<=0)
			return 0;
	}
}

static void ctrl_reply(struct device* dev, char* fmt, ...) {
	va_list va;
	va_start(va, fmt);
	int n=vsprintf(D(dev)->outbuf, fmt, va);
	va_end(va);
	write(D(dev)->ctrlfd, D(dev)->outbuf, n);
}

static int master_init(struct device* dev, int nslaves, struct sockaddr_in* slave) {
	int i;

	// Block and flush pending writes
  	pthread_mutex_lock(&D(dev)->mutex_cow);
	D(dev)->ready = 0;
	dev->ops = &init_device_ops;
	while(D(dev)->pw>0)
		pthread_cond_wait(&D(dev)->blocked, &D(dev)->mutex_cow);
	pthread_mutex_unlock(&D(dev)->mutex_cow);

	slave_stop();

	int* oldbitmap=D(dev)->bitmap;
	D(dev)->bitmap=(int*)calloc((D(dev)->max_size/BLKSIZE)/8+sizeof(int), 1);
	D(dev)->transit=(int*)calloc((D(dev)->max_size/BLKSIZE)/8+sizeof(int), 1);

	master_stab(STAB_QUEUE, master_delayed_write_cb);
	for(i=0;i<nslaves;i++)
		add_slave(slave+i);

	int tot = D(dev)->pc;
	int rem = D(dev)->pc;

	for(i=0;i<D(dev)->max_size;i+=BLKSIZE) {
		if (rec_test(oldbitmap, i)) {
			struct recovery_data* data=(struct recovery_data*)malloc(sizeof(struct recovery_data));
			data->dev=dev;
			data->offset=i;
			data->buffer=memalign(512, BLKSIZE);

			rem--;

			device_pread(D(dev)->snapshot, data->buffer, BLKSIZE, data->offset, recover_read_cb, data);
		}
  		pthread_mutex_lock(&D(dev)->mutex_cow);
		while(D(dev)->pc-rem>=200)
			pthread_cond_wait(&D(dev)->cond_transit, &D(dev)->mutex_cow);
  		pthread_mutex_unlock(&D(dev)->mutex_cow);
	}
 	pthread_mutex_lock(&D(dev)->mutex_cow);
	while(D(dev)->pc>0)
		pthread_cond_wait(&D(dev)->cond_transit, &D(dev)->mutex_cow);

	assert(D(dev)->pw == 0);
  	pthread_mutex_unlock(&D(dev)->mutex_cow);

	free(oldbitmap);

	dev->ops = &master_device_ops;
	D(dev)->ready = 1;
	device_close(D(dev)->snapshot);
	D(dev)->snapshot=NULL;

	pthread_cond_broadcast(&D(dev)->init);
  	pthread_mutex_unlock(&D(dev)->mutex_cow);
}

static int master_add_slave(struct device* dev, struct sockaddr_in* slave) {
	int fd;

	// Block and flush pending writes
  	pthread_mutex_lock(&D(dev)->mutex_cow);
	D(dev)->ready = 0;
	dev->ops = &init_device_ops;
	while(D(dev)->pw>0)
		pthread_cond_wait(&D(dev)->blocked, &D(dev)->mutex_cow);
  	pthread_mutex_unlock(&D(dev)->mutex_cow);

	ctrl_reply(dev, "acknowledge %s %d\n", inet_ntoa(slave->sin_addr), ntohs(slave->sin_port));

	add_slave(slave);
	memset(D(dev)->bitmap, 0, (D(dev)->max_size/BLKSIZE)/8+sizeof(int));

	// Restart writes
  	pthread_mutex_lock(&D(dev)->mutex_cow);
	D(dev)->ready = 1;
	dev->ops = &master_device_ops;
	pthread_cond_broadcast(&D(dev)->init);
  	pthread_mutex_unlock(&D(dev)->mutex_cow);
}

static void pre_init(struct device* dev) {
	int len;
	struct sockaddr_in slave;

	D(dev)->sfd=socket(PF_INET, SOCK_STREAM, 0);

	listen(D(dev)->sfd, SOMAXCONN);

	len = sizeof(struct sockaddr_in);
	memset(&slave, 0, len);
	getsockname(D(dev)->sfd, (struct sockaddr *)&slave, &len);

	ctrl_reply(dev, "booted %s %d\n", inet_ntoa(slave.sin_addr), ntohs(slave.sin_port));
}

static void slave_init(struct device* dev) {
	slave_stab(D(dev)->sfd, STAB_QUEUE, slave_cow_cb, dev);

	pthread_mutex_lock(&D(dev)->mutex_cow);
	dev->ops = &slave_device_ops;
	D(dev)->ready = 1;

	pthread_cond_broadcast(&D(dev)->init);
  	pthread_mutex_unlock(&D(dev)->mutex_cow);
}

void stats(struct device* dev) {
	extern int s_m_num, s_m_size, s_s_num, s_s_size;
	ctrl_reply(dev, "stats %d %d %d %d %d %d %d %d\n", D(dev)->s_str, D(dev)->s_stw, D(dev)->s_stdw, D(dev)->s_ssr, D(dev)->s_ssw, D(dev)->s_stfr, (s_m_num+s_s_num), (s_m_size+s_s_size));
	//fflush(D(dev)->ctrl);
}

static void* ctrl_thread(void* arg) {
	struct device* dev = (struct device*) arg;
	char buffer[100];
	char* cmd[10], i, j;
	struct sockaddr_in slave[10];

	pre_init(dev);

	while(ctrl_read(dev, buffer)) {
		if (strncmp(buffer, "stats", 5))
			fprintf(stderr, "coord: %s\n", buffer);

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
		else if (!strcmp(cmd[0], "booted")) {
			memset(slave, 0, sizeof(struct sockaddr_in));
			slave[0].sin_family = AF_INET;
			slave[0].sin_port = htons(atoi(cmd[2]));
			inet_aton(cmd[1], &slave[0].sin_addr);
			master_add_slave(dev, slave);
		} else if (!strcmp(cmd[0], "failed")) {
			memset(slave, 0, sizeof(struct sockaddr_in));
			slave[0].sin_family = AF_INET;
			slave[0].sin_port = htons(atoi(cmd[2]));
			inet_aton(cmd[1], &slave[0].sin_addr);
			del_slave(slave);
		} else if (!strcmp(cmd[0], "stats")) {
			stats(dev);
			continue;
		}

		fprintf(stderr, "done\n");
	}

	// DANGER! Lost connection to controller.

	exit(1);
}

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
	pthread_cond_init(&D(dev)->cond_transit, NULL);
	D(dev)->storage = storage;
	D(dev)->snapshot = snapshot;
	D(dev)->ctrlfd = ctrlfd;
	D(dev)->max_size = max_size;

	/* create the bitmap */
	D(dev)->bitmap=(int*)calloc((max_size/BLKSIZE)/8+sizeof(int), 1);
	D(dev)->transit=(int*)calloc((max_size/BLKSIZE)/8+sizeof(int), 1);

	pthread_create(&thread, NULL, ctrl_thread, dev);
	pthread_detach(thread);

	return 0;
}

