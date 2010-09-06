/*
 * HoleyCoW - The holey copy-on-write library
 * Copyright (C) 2008 José Orlando Pereira, Luís Soares
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

#include <sys/types.h> 
#include <sys/socket.h> 
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#include "holeycow.h"

#define MAXBLK 1024
#define MAXTHR 100

void* workload_thread(void* p) {
	struct device* dev = (struct device*)p;
	char bogus[BLKSIZE];
	int i,j;

	for(i=0;i<100;i++) {
		for(j=0;j<10;j++) {
			int id=(random()%MAXBLK)|(random()%MAXBLK);
			device_pread_sync(dev, bogus, BLKSIZE, id*BLKSIZE);
			if (*(int*)bogus!=id) {
				printf("expected %d got %d\n", id, *(int*)bogus);
				exit(1);
			}
			device_pwrite_sync(dev, bogus, BLKSIZE, id*BLKSIZE);
			usleep(10000);
		}
		printf(".");
		fflush(stdout);
	}
}

void workload_init(int fd) {
	int i;

	for(i=0;i<MAXBLK;i++)
		pwrite(fd, &i, sizeof(i), i*BLKSIZE);
}

void workload(struct device* dev) {
	int i;
	pthread_t load[MAXTHR];

	for(i=0;i<MAXTHR;i++)
		pthread_create(&load[i], NULL, workload_thread, dev);
	for(i=0;i<MAXTHR;i++)
		pthread_join(load[i], NULL);
}

