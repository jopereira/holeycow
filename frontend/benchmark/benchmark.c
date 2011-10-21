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

#define _GNU_SOURCE

#include <sys/types.h> 
#include <sys/socket.h> 
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>

#include <holeycow.h>

static int verify=0, maxthr=100, maxblk=1024, time;

void* workload_thread(void* p) {
	struct device* dev = (struct device*)p;
	char bogus[BLKSIZE];
	int i,j;

	for(i=0;;i++) {
		for(j=0;j<10;j++) {
			int id=(random()%maxblk)|(random()%maxblk);
			device_pread_sync(dev, bogus, BLKSIZE, id*BLKSIZE);
			if (verify && *(int*)bogus!=id) {
				printf("expected %d got %d\n", id, *(int*)bogus);
				exit(1);
			}
			device_pwrite_sync(dev, bogus, BLKSIZE, id*BLKSIZE);
			usleep(time);
		}
		printf("\r%d pages",i*10);
		fflush(stdout);
	}
}

void workload_init(struct device* dev) {
	int i;

	for(i=0;i<maxblk;i++)
		device_pwrite_sync(dev, &i, sizeof(i), i*BLKSIZE);
}

void workload(struct device* dev) {
	int i;
	pthread_t load[maxthr];

	for(i=0;i<maxthr;i++)
		pthread_create(&load[i], NULL, workload_thread, dev);
	for(i=0;i<maxthr;i++)
		pthread_join(load[i], NULL);
}


void usage() {
	fprintf(stderr, "HoleyCoW mode: benchmark storage snapshot\n");
	fprintf(stderr, "Standalone mode: benchmark storage\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "\t-p port -- set HoleyCoW coordinator port (default: 12345)\n");
	fprintf(stderr, "\t-a -- use asynchronous I/O (default: no)\n");
	fprintf(stderr, "\t-n -- use null backend (default: no)\n");
	fprintf(stderr, "\t-i -- initialize storage (default: no)\n");
	fprintf(stderr, "\t-v -- verify data read (default: no)\n");
	fprintf(stderr, "\t-t threads -- workload threads (default: 100)\n");
	fprintf(stderr, "\t-b blocks -- storage size bits (default: 10, i.e. 1024 blocks)\n");
	fprintf(stderr, "\t-r rate -- blocks/second (default: 1000)\n");
	exit(1);
}

int main(int argc, char* argv[]) {
	int fd, opt, aio=0, null=0, port=12345, init=0, rate=1000;
	struct device storage, snapshot, cow, ba, *target;
	struct sockaddr_in coord;

	while((opt = getopt(argc, argv, "anip:t:b:vr:"))!=-1) {
		switch(opt) {
			case 'a':
				aio = 1;
				break;
			case 'n':
				null = 1;
				break;
			case 'i':
				init = 1;
				break;
			case 'p':
				port = atoi(optarg);
				break;
			case 't':
				maxthr = atoi(optarg);
				break;
			case 'b':
				maxblk = 1<<atoi(optarg);
				break;
			case 'r':
				rate = atoi(optarg);
				break;
			case 'v':
				verify = 1;
				break;
			default:
				usage();
		}
	}

	
	if (argc-optind!=1 && argc-optind!=2) {
		fprintf(stderr, "Invalid parameters %d.\n", optind);
		usage();
	}

	time = (int)(1000000/(((double)rate)/maxthr));

	printf("Target IO rate: %.2lf blocks/second (%d threads)\n", ((double)1000000)*maxthr/time, maxthr);

	if (null) {
		printf("Null storage.\n");
		nullbe_open(&storage);
	} else if (aio) {
		printf("Disk storage (AIO backend).\n");
		aiobe_open(&storage, argv[optind], O_RDWR|O_DIRECT, 0);
	} else {
		printf("Disk storage (synchronous backend).\n");
		posixbe_open(&storage, argv[optind], O_RDWR, 0);
	}

	if (!null && init) {
		printf("Initializing storage: %d blocks of %d bytes\n", maxblk, BLKSIZE);
		workload_init(&storage);
	} else 
		printf("Opened: %d blocks of %d bytes.\n", maxblk, BLKSIZE);

	if (argc-optind==1) {
		/* Standalone mode */
		printf("Running in Standalone mode.\n");
		target = &storage;
	} else {
		printf("Running in HoleyCoW mode (coordinator port = %d).\n", port);
		/* HoleyCoW mode */
		fd=socket(PF_INET, SOCK_STREAM, 0);

		memset(&coord, 0, sizeof(struct sockaddr_in));
		coord.sin_family = AF_INET;
		coord.sin_port = htons(port);
		inet_aton("127.0.0.1", &coord.sin_addr);

		if (connect(fd, (struct sockaddr*) &coord, sizeof(struct sockaddr_in))<0) {
			perror("connect coordination");
			exit(1);
		}
	
		if (null)
			nullbe_open(&snapshot);
		else if (aio)
			aiobe_open(&snapshot, argv[optind+1], O_RDWR|O_CREAT, 0644);
		else
			posixbe_open(&snapshot, argv[optind+1], O_RDWR|O_CREAT, 0644);
		holey_open(&cow, &storage, &snapshot, maxblk*BLKSIZE, fd);
		blockalign(&ba, &cow);
	
		target = &ba;
	}

	workload(target);

	while(1)
		pause();
}
