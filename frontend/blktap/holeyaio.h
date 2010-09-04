/*
 *
 * Holeycow-xen
 * (c) 2010 U. Minho. Written by J. Paulo
 *
 * Based on:
 *
 * Holeycow-mysql
 * (c) 2008 José Orlando Pereira, Luís Soares
 * 
 * Blktap-Xen
 * Copyright (c) 2006 Andrew Warfield and Julian Chesterfield
 * Copyright (c) 2007 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __HOLEYAIO_H__
#define __HOLEYAIO_H__

#include <pthread.h>
#include <libaio.h>
#include <stdint.h>

#include "tapdisk.h"

#define IOCB_IDX(_ctx, _io) ((_io) - (_ctx)->iocb_list)

struct holey_aio_internal_context {
        io_context_t     aio_ctx;

        struct io_event *aio_events;
        int              max_aio_events;

        pthread_t        aio_thread;
        int              command_fd[2];
        int              completion_fd[2];
        int              pollfd;
        unsigned int     poll_in_thread : 1;
};
	

typedef struct holey_aio_internal_context holey_aio_internal_context_t;


struct pending_aio {
	td_callback_t cb;
	int id;
	void *private;
	int nb_sectors;
	char *buf;
	uint64_t sector;
        int boff;
        int fd;
        int rc;
};

	
struct holey_aio_context {
	holey_aio_internal_context_t    aio_ctx;

	int                  max_aio_reqs;
	struct iocb         *iocb_list;
	struct iocb        **iocb_free;
	struct pending_aio  *pending_aio;
	int                  iocb_free_count;
	struct iocb        **iocb_queue;
	int	             iocb_queued;
	struct io_event     *aio_events;

	/* Locking bitmap for AIO reads/writes */
	uint8_t *sector_lock;		   
};

typedef struct holey_aio_context holey_aio_context_t;

void holey_aio_continue   (holey_aio_internal_context_t *ctx);
int  holey_aio_get_events (holey_aio_internal_context_t *ctx);
int  holey_aio_more_events(holey_aio_internal_context_t *ctx);


int holey_aio_init(holey_aio_context_t *ctx, uint64_t sectors,
		int max_aio_reqs);
void holey_aio_free(holey_aio_context_t *ctx);

int holey_aio_can_lock(holey_aio_context_t *ctx, uint64_t sector);
int holey_aio_lock(holey_aio_context_t *ctx, uint64_t sector);
void holey_aio_unlock(holey_aio_context_t *ctx, uint64_t sector);


int holey_aio_read(holey_aio_context_t *ctx, int fd, int size, 
		uint64_t offset, char *buf, td_callback_t cb,
		int id, uint64_t sector, void *private);
int holey_aio_write(holey_aio_context_t *ctx, int fd, int size,
		uint64_t offset, char *buf, td_callback_t cb,
		int id, uint64_t sector, void *private,uint64_t boff,int fd1,int rc);
int holey_aio_submit(holey_aio_context_t *ctx);

#endif /* __HOLEYAIO_H__ */
