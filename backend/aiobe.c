/*
 * HoleyCoW
 * Copyright (c) 2008-2011 Universidade do Minho
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

#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <libaio.h>
#include <assert.h>

#include "aiobe.h"

#define D(dev) ((struct aiobe_data*)(dev)->data)

#define POOL 10

struct aiobe_data {
	int fd;
	io_context_t ctx;
	pthread_t cbt[POOL];
};

struct aiobe_request {
	struct iocb iocb;
	dev_callback_t cb;
	void* cookie;
};

static inline aiobe_complete(struct aiobe_request* req, int ret) {
	req->cb(req->cookie, ret);
	free(req);
}

static void* aiobe_thread(void* p) {
	struct device* dev = (struct device*)p;
	struct io_event event;
	while(1) {
		int ret=io_getevents(D(dev)->ctx, 1, 1, &event, NULL);
		if (ret!=1)
			break;
		aiobe_complete((struct aiobe_request*) event.data, event.res);
	}
	return NULL;
}

static void aiobe_pwrite(struct device* dev, void* buf, size_t size, off64_t offset, dev_callback_t cb, void* cookie) {
	int ret;
	struct aiobe_request* req=(struct aiobe_request*)malloc(sizeof(struct aiobe_request));
	struct iocb* iocb = &req->iocb;
	memset(req, 0, sizeof(*req));

	io_prep_pwrite(iocb, D(dev)->fd, buf, size, offset);
	req->cb=cb;
	req->cookie=cookie;
	iocb->data = req;

	ret=io_submit(D(dev)->ctx, 1, &iocb);
	if (ret!=1)
		aiobe_complete(req, -1);
}

static void aiobe_pread(struct device* dev, void* buf, size_t size, off64_t offset, dev_callback_t cb, void* cookie) {
	int ret;
	struct aiobe_request* req=(struct aiobe_request*)malloc(sizeof(struct aiobe_request));
	struct iocb* iocb = &req->iocb;
	memset(req, 0, sizeof(*req));

	io_prep_pread(iocb, D(dev)->fd, buf, size, offset);
	req->cb=cb;
	req->cookie=cookie;
	iocb->data=req;

	ret=io_submit(D(dev)->ctx, 1, &iocb);

	if (ret!=1)
		aiobe_complete(req, -1);
}

static int aiobe_close(struct device* dev) {
	int ret=close(D(dev)->fd);
	free(D(dev));
	return ret;
}

struct device_ops aiobe_device_ops = {
	aiobe_pwrite,
	aiobe_pread,
	aiobe_close
};

int aiobe_open(struct device* dev, char* path, int flags, mode_t mode) {
	int i;

	dev->ops = &aiobe_device_ops;
	dev->data = malloc(sizeof(struct aiobe_data));

	int ret = open(path, flags, mode);
	if (ret<0) {
		free(dev->data);
		return -1;
	}

	D(dev)->fd = ret;

	if (io_queue_init(1000, &D(dev)->ctx)) {
		close(D(dev)->fd);
		free(dev->data);
		return -1;
	}

	for(i=0;i<10;i++)
		pthread_create(&D(dev)->cbt[i], NULL, aiobe_thread, dev);
	return 0;
}

