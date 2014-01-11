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

#include <holeycow/posixbe.h>

#define D(dev) ((struct posixbe_data*)(dev)->data)

struct posixbe_data {
	int fd;
};

static void posixbe_pwrite(struct device* dev, void* buf, size_t size, off64_t offset, dev_callback_t cb, void* cookie) {
	int ret=pwrite64(D(dev)->fd, buf, size, offset);
	cb(cookie, ret);
}

static void posixbe_pread(struct device* dev, void* buf, size_t size, off64_t offset, dev_callback_t cb, void* cookie) {
	int ret=pread64(D(dev)->fd, buf, size, offset);
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

int posixbe_open(struct device* dev, const char* path, int flags, mode_t mode) {
	dev->ops = &posixbe_device_ops;
	dev->data = malloc(sizeof(struct posixbe_data));

	int ret = open(path, flags, mode);
	if (ret>=0) {
		D(dev)->fd = ret;
		return 0;
	}

	return -1;
}

