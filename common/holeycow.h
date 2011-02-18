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

#ifndef __COW__
#define __COW__

#include <sys/types.h>
#include <stdint.h>

#define FDBITS 12

#define OFF64 uint64_t

#define FDMASK ((1<<FDBITS)-1)
#define OFFMASK (~((1<<FDBITS)-1))
#define BLKSIZE (1<<FDBITS)

#define GETBLK(id) (id&OFFMASK)>>FDBITS

/* Generic asynchronous block device */
struct device;

typedef void (*dev_callback_t)(void* cookie, int ret);

struct device_ops {
	void (*pwrite)(struct device*, void*, size_t, OFF64, dev_callback_t, void*);
	void (*pread)(struct device*, void*, size_t, OFF64, dev_callback_t, void*);
	int (*close)(struct device*);
};

struct device {
	struct device_ops* ops;
	void* data;
};

#define device_pwrite(dev, buf, size, off, cb, cookie) (dev)->ops->pwrite(dev, buf, size, off, cb, cookie)
#define device_pread(dev, buf, size, off, cb, cookie) (dev)->ops->pread(dev, buf, size, off, cb, cookie)
#define device_close(dev) (dev)->ops->close(dev)

/* Simulate synchronous I/O */
extern int device_pwrite_sync(struct device*, void*, size_t, OFF64);
extern int device_pread_sync(struct device*, void*, size_t, OFF64);

/* Enforce block aligned I/O */
extern void blockalign(struct device*, struct device*);

int holey_open(struct device* dev, struct device* storage, struct device* snapshot, uint64_t max_size, int ctrlfd);

#endif
