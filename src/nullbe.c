/*
 * HoleyCoW-SBD
 * Copyright (c) 2008-2010,2014 Universidade do Minho
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
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#include <holeycow/nullbe.h>

#define MAXPENDING 1000

#define D(dev) ((struct nullbe_data*)(dev)->data)

struct null_cb {
	dev_callback_t cb;
	void* cookie;
	size_t size;
};

struct nullbe_data {
	pthread_t worker;
	pthread_mutex_t mutex;
	pthread_cond_t empty, full;
	int h, t, n, done;
	struct null_cb blocks[MAXPENDING];
};

static void add_cb(struct device* dev, dev_callback_t cb, void* cookie, size_t size) {
	pthread_mutex_lock(&D(dev)->mutex);
	while(D(dev)->n>=MAXPENDING)
		pthread_cond_wait(&D(dev)->full, &D(dev)->mutex);
	D(dev)->blocks[D(dev)->h].cb = cb;
	D(dev)->blocks[D(dev)->h].cookie = cookie;
	D(dev)->blocks[D(dev)->h].size = size;
	D(dev)->n++;
	D(dev)->h=(D(dev)->h+1)%MAXPENDING;
	pthread_cond_signal(&D(dev)->empty);
	pthread_mutex_unlock(&D(dev)->mutex);
}

static void nullbe_pwrite(struct device* dev, void* buf, size_t size, off64_t offset, dev_callback_t cb, void* cookie) {
	add_cb(dev, cb, cookie, size);
}

static void nullbe_pread(struct device* dev, void* buf, size_t size, off64_t offset, dev_callback_t cb, void* cookie) {
	add_cb(dev, cb, cookie, size);
}

static int nullbe_close(struct device* dev) {
	pthread_mutex_lock(&D(dev)->mutex);
	D(dev)->done = 1;
	pthread_cond_signal(&D(dev)->empty);
	pthread_mutex_unlock(&D(dev)->mutex);
	pthread_join(D(dev)->worker, NULL);
	free(D(dev));
	return 0;
}

struct device_ops nullbe_device_ops = {
	nullbe_pwrite,
	nullbe_pread,
	nullbe_close
};

static void* worker(void* p) {
	struct device* dev = (struct device*) p;

	pthread_mutex_lock(&D(dev)->mutex);
	while(1) {
		dev_callback_t cb;
		void* cookie;
		size_t size;
		while(D(dev)->n==0 && !D(dev)->done)
			pthread_cond_wait(&D(dev)->empty, &D(dev)->mutex);
		if (D(dev)->n==0 && D(dev)->done)
			return NULL;
		cb = D(dev)->blocks[D(dev)->t].cb;
		cookie = D(dev)->blocks[D(dev)->t].cookie;
		size = D(dev)->blocks[D(dev)->t].size;
		D(dev)->n--;
		D(dev)->t=(D(dev)->t+1)%MAXPENDING;
		pthread_mutex_unlock(&D(dev)->mutex);
		cb(cookie, size);
		pthread_mutex_lock(&D(dev)->mutex);
		pthread_cond_signal(&D(dev)->full);
	}
}

int nullbe_open(struct device* dev) {
	dev->ops = &nullbe_device_ops;
	dev->data = malloc(sizeof(struct nullbe_data));

	pthread_mutex_init(&D(dev)->mutex, NULL);
	pthread_cond_init(&D(dev)->empty, NULL);
	pthread_cond_init(&D(dev)->full, NULL);
	D(dev)->n=0;
	D(dev)->h=0;
	D(dev)->t=0;
	D(dev)->done=0;
	pthread_create(&D(dev)->worker, NULL, worker, dev);

	return 0;
}

