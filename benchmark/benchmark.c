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

#include <common/holeycow.h>

static int verify=0, maxthr=100, time, length=1, verbose=0, align=1, csv=0;
static uint64_t maxblk=1024;

pthread_mutex_t mtx;
int cnt;
struct timeval start;

void* reporter_thread(void* p) {
	struct timeval now, init;
	init=start;
	double time,elapsed;
	while(1) {
		sleep(1);
		pthread_mutex_lock(&mtx);
		gettimeofday(&now, NULL);
		time=now.tv_sec-init.tv_sec+(now.tv_usec-init.tv_usec)/(double)1e6;
		elapsed=now.tv_sec-start.tv_sec+(now.tv_usec-start.tv_usec)/(double)1e6;
		if (csv)
			printf("%.2lf, %.2lf\n",time,cnt/(BLKSIZE*elapsed));
		else
			printf("\r%.2lf blocks/s",cnt/(BLKSIZE*elapsed));
		cnt=0;
		start=now;
		pthread_mutex_unlock(&mtx);
		fflush(stdout);
	}
}

void* workload_thread(void* p) {
	struct device* dev = (struct device*)p;
	char *buf, *bogus;
	int i,j;
	struct timeval now;
	double elapsed;

	buf=malloc(BLKSIZE*length+512);
	bogus=buf+(512-((long)buf)%512);
	for(i=0;;i++) {
		for(j=0;j<10;j++) {
			struct timeval before;
			int id=(random()%(maxblk-length+1));
			int offset=id*BLKSIZE;
			int count=BLKSIZE*length;
			int noise=0;
			if (!align) {
				noise=random()%((BLKSIZE-2)/2);
				offset+=noise;
				count-=noise*2;
			}
			if (verbose) {
				printf("begin read %d %d %d\n", id, length, noise);
				gettimeofday(&before, NULL);
			}
			device_pread_sync(dev, bogus, count, offset);
			if (verbose) {
				gettimeofday(&now, NULL);
				elapsed=(now.tv_sec-before.tv_sec)*(double)1e6+(now.tv_usec-before.tv_usec);
				printf("end read %d %d %.1lf\n", id, length, elapsed);
			}
			if (verify && *(int*)bogus!=id) {
				printf("expected %d got %d\n", id, *(int*)bogus);
				exit(1);
			}
			if (verbose) {
				gettimeofday(&before, NULL);
				printf("begin write %d %d %d\n", id, length, noise);
			}
			device_pwrite_sync(dev, bogus, count, offset);
			if (verbose) {
				gettimeofday(&now, NULL);
				elapsed=(now.tv_sec-before.tv_sec)*(double)1e6+(now.tv_usec-before.tv_usec);
				printf("end write %d %d %.1lf\n", id, length, elapsed);
			}
			usleep(time);
			pthread_mutex_lock(&mtx);
			cnt+=count;
			pthread_mutex_unlock(&mtx);
		}
	}
	free(bogus);
}

void workload_init(struct device* dev) {
	int i;

	for(i=0;i<maxblk;i++)
		device_pwrite_sync(dev, &i, sizeof(i), i*BLKSIZE);
}

void workload(struct device* dev) {
	int i;
	pthread_t load[maxthr];
	pthread_t rep;

	pthread_mutex_init(&mtx, NULL);
	gettimeofday(&start, NULL);

	for(i=0;i<maxthr;i++)
		pthread_create(&load[i], NULL, workload_thread, dev);
	pthread_create(&rep, NULL, reporter_thread, NULL);
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
	fprintf(stderr, "\t-l -- request length in pages (default: 1)\n");
	fprintf(stderr, "\t-f -- verify data read (default: no)\n");
	fprintf(stderr, "\t-t threads -- workload threads (default: 100)\n");
	fprintf(stderr, "\t-b blocks -- storage size (default: 1024 blocks)\n");
	fprintf(stderr, "\t-u -- unaligned blocks (default: no)\n");
	fprintf(stderr, "\t-r rate -- blocks/second (default: 1000)\n");
	fprintf(stderr, "\t-c -- CSV output (default: no)\n");
	fprintf(stderr, "\t-v -- verbose (default: no)\n");
	exit(1);
}

int main(int argc, char* argv[]) {
	int fd, opt, aio=0, null=0, port=12345, init=0, rate=1000;
	struct device storage, snapshot, cow, ba, *target;
	struct sockaddr_in coord;

	while((opt = getopt(argc, argv, "anip:t:b:vr:l:fuc"))!=-1) {
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
				maxblk = atoi(optarg);
				break;
			case 'r':
				rate = atoi(optarg);
				break;
			case 'u':
				align = 0;
				break;
			case 'f':
				verify = 1;
				break;
			case 'v':
				verbose = 1;
				break;
			case 'l':
				length = atoi(optarg);
				break;
			case 'c':
				csv = 1;
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

	printf("Target IO rate: %.2lf blocks/second (%d threads)\n", ((double)1000000)*maxthr*length/time, maxthr);

	if (null) {
		printf("Null storage.\n");
		nullbe_open(&storage);
	} else if (aio) {
		printf("Disk storage (AIO backend).\n");
		aiobe_open(&storage, argv[optind], O_RDWR|O_SYNC|O_DIRECT, 0);
		//aiobe_open(&storage, argv[optind], O_RDWR, 0);
	} else {
		printf("Disk storage (synchronous backend).\n");
		posixbe_open(&storage, argv[optind], O_RDWR, 0);
	}

	if (!null && init) {
		printf("Initializing storage: %ld blocks of %d bytes\n", maxblk, BLKSIZE);
		workload_init(&storage);
	} else {
		struct stat sbuf;
		if (stat(argv[optind], &sbuf)<0) {
			perror(argv[optind]);
			exit(1);
		}
		maxblk = sbuf.st_size/BLKSIZE;
		printf("Opened: %ld blocks of %d bytes.\n", maxblk, BLKSIZE);
	}

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
			aiobe_open(&snapshot, argv[optind+1], O_RDWR|O_CREAT|O_DIRECT, 0644);
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
