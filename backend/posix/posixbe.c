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

#include <unistd.h>
#include <stdlib.h>

#include "posixbe.h"

#define D(dev) ((struct posixbe_data*)(dev)->data)

struct posixbe_data {
	int fd;
};

static void posixbe_pwrite(struct device* dev, void* buf, size_t size, off_t offset, dev_callback_t cb, void* cookie) {
	int ret=pwrite(D(dev)->fd, buf, size, offset);
	cb(cookie, ret);
}

static void posixbe_pread(struct device* dev, void* buf, size_t size, off_t offset, dev_callback_t cb, void* cookie) {
	int ret=pread(D(dev)->fd, buf, size, offset);
	cb(cookie, ret);
}

static int posixbe_close(struct device* dev) {
	int ret=close(D(dev)->fd);
	free(D(dev));
	return ret;
}

struct device_ops posixbe_device_ops = {
	posixbe_pwrite,
	posixbe_pread,
	posixbe_close
};

int posixbe_open(struct device* dev, char* path, int flags) {
	dev->ops = &posixbe_device_ops;
	dev->data = malloc(sizeof(struct posixbe_data));

	D(dev)->fd = open(path, flags, 0644);

	// TODO: error handling

	return 0;
}

