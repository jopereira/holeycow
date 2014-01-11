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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <pthread.h>

#include <holeycow/device.h>

/****
 * Synchronous I/O
 */

struct cb_data {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int flag, ret;
};

static void sync_cb(void* cookie, int ret) {
	struct cb_data* data = (struct cb_data*) cookie;

	pthread_mutex_lock(&data->mutex);
	data->flag = 1;
	data->ret = ret;
	pthread_cond_signal(&data->cond);
	pthread_mutex_unlock(&data->mutex);
}

int device_pwrite_sync(struct device* dev, const void* buf, size_t size, off64_t offset) {
	struct cb_data data = {
		PTHREAD_MUTEX_INITIALIZER,
		PTHREAD_COND_INITIALIZER,
		0,
		0
	};

	device_pwrite(dev, buf, size, offset, sync_cb, &data);

	pthread_mutex_lock(&data.mutex);
	while(data.flag == 0)
		pthread_cond_wait(&data.cond, &data.mutex);
	pthread_mutex_unlock(&data.mutex);

	return data.ret;
}

int device_pread_sync(struct device* dev, void* buf, size_t size, off64_t offset) {
	struct cb_data data = {
		PTHREAD_MUTEX_INITIALIZER,
		PTHREAD_COND_INITIALIZER,
		0,
		0
	};

	device_pread(dev, buf, size, offset, sync_cb, &data);

	pthread_mutex_lock(&data.mutex);
	while(data.flag == 0)
		pthread_cond_wait(&data.cond, &data.mutex);
	pthread_mutex_unlock(&data.mutex);

	return data.ret;
}

/****
 * Block align
 */

struct blockalign_data {
	struct device* impl;
};

#define D(dev) ((struct blockalign_data*)(dev)->data)

struct fragmented {
	dev_callback_t cb;
	void * cookie;

	pthread_mutex_t mutex;
	int count, ret;
};

struct incomplete {
	char tmp[BLKSIZE];
	struct fragmented* frag;
	struct device* dev;

	void* buffer;
	uint64_t offset, cursor;
	int bcount;
};

static void frag_cb(void* cookie, int ret) {
	struct fragmented* frag = (struct fragmented*) cookie;

	pthread_mutex_lock(&frag->mutex);

	if (ret<0 || frag->ret<0)
		frag->ret = -1;
	else
		frag->ret += ret;
	frag->count--;

	if (frag->count==0) {
		frag->cb(frag->cookie, frag->ret);
		pthread_mutex_destroy(&frag->mutex);
		free(frag);
	} else
		pthread_mutex_unlock(&frag->mutex);
}

static void cleanup_cb(void* cookie, int ret) {
	struct incomplete* inc = (struct incomplete*) cookie;

	if (ret==BLKSIZE)
		frag_cb(inc->frag, inc->bcount);
	else
		frag_cb(inc->frag, -1);
	free(inc);
}

static void pwrite_cb(void* cookie, int ret) {
	struct incomplete* inc = (struct incomplete*) cookie;

	if (ret==BLKSIZE) {
		memcpy(inc->tmp+(inc->offset-inc->cursor), inc->buffer, inc->bcount);
		device_pwrite(D(inc->dev)->impl, inc->tmp, BLKSIZE, inc->cursor, cleanup_cb, inc);
	} else
		cleanup_cb(inc, -1);
}

static void pread_cb(void* cookie, int ret) {
	struct incomplete* inc = (struct incomplete*) cookie;

	if (ret==BLKSIZE) {
		memcpy(inc->buffer, inc->tmp+(inc->offset-inc->cursor), inc->bcount);
		frag_cb(inc->frag, inc->bcount);
	} else
		frag_cb(inc->frag, -1);
	free(inc);
}

static void blockalign_pwrite(struct device* dev, void* data, size_t count, off64_t offset, dev_callback_t cb, void* cookie) {
	struct fragmented* frag;

	if (count%BLKSIZE == 0 && offset%BLKSIZE == 0) {
		/* Fast path */
		device_pwrite(D(dev)->impl, data, count, offset, cb, cookie);
		return;
	}

	frag = (struct fragmented*) malloc(sizeof(struct fragmented));
	memset(frag, 0 , sizeof(*frag));

	frag->cb = cb;
	frag->cookie = cookie;
	frag->count = 1;

	pthread_mutex_init(&frag->mutex, NULL);

	while(count>0) {
		pthread_mutex_lock(&frag->mutex);
		frag->count++;
		pthread_mutex_unlock(&frag->mutex);

		uint64_t cursor=offset & OFFMASK;
		int bcount;
				
		if (cursor != offset || count<BLKSIZE) {
			bcount = offset+count > cursor+BLKSIZE ? cursor+BLKSIZE-offset : count;

			struct incomplete* inc = (struct incomplete*) memalign(512, sizeof(struct incomplete));
			memset(inc, 0 , sizeof(*inc));

			inc->frag = frag;
			inc->dev = dev;
			inc->cursor = cursor;
			inc->bcount = bcount;
			inc->offset = offset;
			inc->buffer = data;

			device_pread(D(dev)->impl, inc->tmp, BLKSIZE, cursor, pwrite_cb, inc);
		} else {
			bcount = count%BLKSIZE;

			device_pwrite(D(dev)->impl, data, bcount, cursor, frag_cb, frag);
		}
		offset+=bcount;
		count-=bcount;
		data+=bcount;
	}

	frag_cb(frag, 0);
}

static void blockalign_pread(struct device* dev, void* data, size_t count, off64_t offset, dev_callback_t cb, void* cookie) {
	struct fragmented* frag;

	if (count%BLKSIZE == 0 && offset%BLKSIZE == 0) {
		/* Fast path */
		device_pread(D(dev)->impl, data, count, offset, cb, cookie);
		return;
	}

	frag = (struct fragmented*) malloc(sizeof(struct fragmented));
	memset(frag, 0 , sizeof(*frag));

	frag->cb = cb;
	frag->cookie = cookie;
	frag->count = 1;

	pthread_mutex_init(&frag->mutex, NULL);

	while(count>0) {
		pthread_mutex_lock(&frag->mutex);
		frag->count++;
		pthread_mutex_unlock(&frag->mutex);

		uint64_t cursor=offset & OFFMASK;
		int bcount;
				
		if (cursor != offset || count<BLKSIZE) {
			bcount = offset+count > cursor+BLKSIZE ? cursor+BLKSIZE-offset : count;

			struct incomplete* inc = (struct incomplete*) memalign(512, sizeof(struct incomplete));
			memset(inc, 0 , sizeof(*inc));

			inc->frag = frag;
			inc->cursor = cursor;
			inc->bcount = bcount;
			inc->offset = offset;
			inc->buffer = data;

			device_pread(D(dev)->impl, inc->tmp, BLKSIZE, cursor, pread_cb, inc);
		} else {
			bcount = count%BLKSIZE;
			device_pread(D(dev)->impl, data, bcount, cursor, frag_cb, frag);
		}

		offset+=bcount;
		count-=bcount;
		data+=bcount;
	}

	frag_cb(frag, 0);
}

static int blockalign_close(struct device* dev) {
	int ret=device_close(D(dev)->impl);
	free(dev->data);
	return ret;
}

static struct device_ops blockalign_device_ops = {
	blockalign_pwrite,
	blockalign_pread,
	blockalign_close
};

extern void blockalign(struct device* dev, struct device* impl) {
	dev->ops = &blockalign_device_ops,
	dev->data = (struct blockalign_data*)malloc(sizeof(struct blockalign_data));
	memset(dev->data, 0, sizeof(struct blockalign_data));
	D(dev)->impl = impl;
}
