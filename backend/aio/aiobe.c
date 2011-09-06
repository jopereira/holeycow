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

#define _LARGEFILE64_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <aio.h>

#include "aiobe.h"

#define D(dev) ((struct aiobe_data*)(dev)->data)

struct aiobe_data {
	int fd;
};

struct aiobe_request {
	struct aiocb64 op;
	dev_callback_t cb;
	void* cookie;
};

static void aiobe_notification(sigval_t val) {
	int ret;
	struct aiobe_request* req=(struct aiobe_request*)val.sival_ptr;

	req->cb(req->cookie, aio_return64(&req->op));
	free(req);
}

static void aiobe_pwrite(struct device* dev, void* buf, size_t size, off64_t offset, dev_callback_t cb, void* cookie) {
	int ret;
	struct aiobe_request* req=(struct aiobe_request*)malloc(sizeof(struct aiobe_request));
	memset(req, 0, sizeof(*req));

	req->op.aio_fildes=D(dev)->fd;
	req->op.aio_buf=buf;
	req->op.aio_nbytes=size;
	req->op.aio_offset=offset;

	req->op.aio_sigevent.sigev_notify = SIGEV_THREAD;
	req->op.aio_sigevent.sigev_notify_function = aiobe_notification;
	req->op.aio_sigevent.sigev_notify_attributes = NULL;
	req->op.aio_sigevent.sigev_value.sival_ptr = req;

	req->cb=cb;
	req->cookie=cookie;

	ret = aio_write64(&req->op);
	if (ret < 0)
		aiobe_notification((sigval_t)(void*)req);
}

static void aiobe_pread(struct device* dev, void* buf, size_t size, off64_t offset, dev_callback_t cb, void* cookie) {
	int ret;
	struct aiobe_request* req=(struct aiobe_request*)malloc(sizeof(struct aiobe_request));
	memset(req, 0, sizeof(*req));

	req->op.aio_fildes=D(dev)->fd;
	req->op.aio_buf=buf;
	req->op.aio_nbytes=size;
	req->op.aio_offset=offset;

	req->op.aio_sigevent.sigev_notify = SIGEV_THREAD;
	req->op.aio_sigevent.sigev_notify_function = aiobe_notification;
	req->op.aio_sigevent.sigev_notify_attributes = NULL;
	req->op.aio_sigevent.sigev_value.sival_ptr = req;

	req->cb=cb;
	req->cookie=cookie;

	ret = aio_read64(&req->op);
	if (ret < 0)
		aiobe_notification((sigval_t)(void*)req);
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
	dev->ops = &aiobe_device_ops;
	dev->data = malloc(sizeof(struct aiobe_data));

	int ret = open(path, flags, mode);
	if (ret>=0) {
		D(dev)->fd = ret;
		return 0;
	}

	return -1;
}

