/*
 * HoleyCoW
 * Copyright (c) 2008-2012 Universidade do Minho
 * Written by José Pereira
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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "simbe.h"

#include <disksim_interface.h>

#define D(dev) ((struct simbe_data*)(dev)->data)

struct simbe_request {
	struct disksim_request sr;

	struct device* dev;

	double deadline;
	int done;
	int ret;
	struct simbe_request* next;

	dev_callback_t cb;
	void* cookie;
};

struct simbe_data {
	struct device* target;

	pthread_mutex_t mtx;
	pthread_cond_t cond;
	struct simbe_request* queue;
	pthread_t thr;

	struct disksim_interface *disksim;
	disksim_interface_callback_t cb;
	double next;

	double zero;
};

static double time_sync(struct device* dev) {
	struct timeval now;
	double time;

	pthread_mutex_lock(&D(dev)->mtx);
	while(1) {
		gettimeofday(&now, NULL);

		time = ((double)now.tv_sec)*1e3 + (((double)now.tv_usec)*1e-3);
		if ((D(dev)->queue == NULL || D(dev)->queue->deadline > time) &&
			(D(dev)->next < 0 || D(dev)->next+D(dev)->zero > time)) {
			pthread_mutex_unlock(&D(dev)->mtx);
			return time;
		}
		
		pthread_cond_wait(&D(dev)->cond, &D(dev)->mtx);
	}
}

static void* time_thread(void* p) {
	struct device* dev = (struct device*) p;
	struct timeval now;
	double time, target, delta;

	while(1) {
		pthread_mutex_lock(&D(dev)->mtx);
		gettimeofday(&now, NULL);

		time = ((double)now.tv_sec)*1e3 + (((double)now.tv_usec)*1e-3);
		while(D(dev)->next > 0 && D(dev)->next+D(dev)->zero < time) {
			double event = D(dev)->next;
			D(dev)->next = -1;
			disksim_interface_internal_event(D(dev)->disksim, event, 0);
		}

		if (D(dev)->queue!=NULL)
		while(D(dev)->queue != NULL && D(dev)->queue->deadline <= time && D(dev)->queue->done) {
			struct simbe_request* req = D(dev)->queue;
			D(dev)->queue = req->next;
			pthread_mutex_unlock(&D(dev)->mtx);

			gettimeofday(&now, NULL);
			delta = ((double)now.tv_sec)*1e3 + (((double)now.tv_usec)*1e-3);
			delta -= req->deadline;
			if (delta >= 1)
				fprintf(stderr, "simbe: warning: deadline exceeded by %lfms\n", delta);

			req->cb(req->cookie, req->ret);
			free(req);

			pthread_mutex_lock(&D(dev)->mtx);
		}

		target = -1;
		if (D(dev)->queue != NULL && (target < 0 || D(dev)->queue->deadline < target))
			target = D(dev)->queue->deadline;
		if (D(dev)->next > 0 && (target < 0 || D(dev)->next+D(dev)->zero < target))
			target = D(dev)->next+D(dev)->zero;
	
		if (target < 0)
			pthread_cond_wait(&D(dev)->cond, &D(dev)->mtx);
		else {
			struct timespec ts;
			delta = target - time;

			if (delta > 0) {
				ts.tv_sec = (int) delta*1e-3;
				ts.tv_nsec = (long int) ((delta-ts.tv_sec*1e3)*1e6);
				pthread_cond_timedwait(&D(dev)->cond, &D(dev)->mtx, &ts);
			}
		}
		pthread_mutex_unlock(&D(dev)->mtx);
	}
}

static void sim_schedule_callback(disksim_interface_callback_t cb, double time, void* ctx) {
	struct device* dev = (struct device*) ctx;

	/* No locking. This is called already within the lock. */
	D(dev)->next = time;
	D(dev)->cb = cb;
	pthread_cond_broadcast(&D(dev)->cond);
}

static void sim_deschedule_callback(double time, void *ctx) {
	struct device* dev = (struct device*) ctx;

	/* No locking. This is called already within the lock. */
	D(dev)->cb = NULL;
	pthread_cond_broadcast(&D(dev)->cond);
}

static void sim_report_completion(double time, struct disksim_request *r, void *ctx) {
	struct device* dev = (struct device*) ctx;
	struct simbe_request* req = (struct simbe_request*) r;
	struct simbe_request** p;

	/* No locking. This is called already within the lock. */
	req->deadline = time+D(dev)->zero;
	for(p=&D(dev)->queue;*p != NULL && (*p)->deadline <= time; p=&(*p)->next)
		;
	req->next = *p;
	*p = req;

	if (req->done)
		pthread_cond_broadcast(&D(dev)->cond);
}

static void simbe_complete(void* cookie, int ret) {
	struct simbe_request* req = (struct simbe_request*) cookie;

	pthread_mutex_lock(&D(req->dev)->mtx);
	req->done = 1;
	if (req->deadline > 0)
		pthread_cond_broadcast(&D(req->dev)->cond);
	pthread_mutex_unlock(&D(req->dev)->mtx);
}

static void simbe_pwrite(struct device* dev, void* buf, size_t size, off64_t offset, dev_callback_t cb, void* cookie) {
	struct simbe_request* req=(struct simbe_request*)malloc(sizeof(struct simbe_request));
	memset(req, 0, sizeof(*req));

	req->cb=cb;
	req->cookie=cookie;
	req->dev = dev;

	double now = time_sync(dev);
	req->sr.start = now-D(dev)->zero;
	req->sr.flags = DISKSIM_WRITE;
	req->sr.devno = 0;
	req->sr.bytecount = size;
	req->sr.blkno = offset/512;

	pthread_mutex_lock(&D(dev)->mtx);
	disksim_interface_request_arrive(D(dev)->disksim, now-D(dev)->zero, &req->sr);
	pthread_mutex_unlock(&D(dev)->mtx);

	device_pwrite(D(dev)->target, buf, size, offset, simbe_complete, req);
}

static void simbe_pread(struct device* dev, void* buf, size_t size, off64_t offset, dev_callback_t cb, void* cookie) {
	struct simbe_request* req=(struct simbe_request*)malloc(sizeof(struct simbe_request));
	memset(req, 0, sizeof(*req));

	req->cb=cb;
	req->cookie=cookie;
	req->dev = dev;

	double now = time_sync(dev);
	req->sr.start = now-D(dev)->zero;
	req->sr.flags = DISKSIM_READ;
	req->sr.devno = 0;
	req->sr.bytecount = size;
	req->sr.blkno = offset/512;

	pthread_mutex_lock(&D(dev)->mtx);
	disksim_interface_request_arrive(D(dev)->disksim, now-D(dev)->zero, &req->sr);
	pthread_mutex_unlock(&D(dev)->mtx);

	device_pread(D(dev)->target, buf, size, offset, simbe_complete, req);
}

static int simbe_close(struct device* dev) {
	double now = time_sync(dev);
	disksim_interface_shutdown(D(dev)->disksim, now-D(dev)->zero);
	free(D(dev));
	return 0;
}

struct device_ops simbe_device_ops = {
	simbe_pwrite,
	simbe_pread,
	simbe_close
};

int simbe_open(struct device* dev, struct device* target, char* priv) {
	struct timeval now;

	dev->ops = &simbe_device_ops;
	dev->data = malloc(sizeof(struct simbe_data));
	memset(dev->data, 0, sizeof(struct simbe_data));

	D(dev)->target = target;
	gettimeofday(&now, NULL);
	D(dev)->zero = ((double)now.tv_sec)*1e3 + (((double)now.tv_usec)*1e-3);
	D(dev)->next = -1;

	pthread_mutex_init(&D(dev)->mtx, NULL);
	pthread_cond_init(&D(dev)->cond, NULL);

	D(dev)->disksim = disksim_interface_initialize(priv, "out",
		sim_report_completion, sim_schedule_callback, sim_deschedule_callback,
		dev, 0, NULL);

	pthread_create(&D(dev)->thr, NULL, time_thread, dev);

	return 0;
}

