/*
 * HoleyCoW-SBD
 * Copyright (C) 2008,2014 Universidade do Minho
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

#include <holeycow/device.h>

#ifdef DISKSIM
static char* parv = NULL;
#endif

static int verify=0, maxthr=100, time, length=1, verbose=0, align=1, csv=0, ops=3;
static uint64_t maxblk=1024;
static char* host = "127.0.0.1";

pthread_mutex_t mtx;
int cnt,iocnt;
double total;
struct timeval start;

void* reporter_thread(void* p) {
	struct timeval now, init;
	init=start;
	double delta,elapsed;
	while(1) {
		sleep(1);
		pthread_mutex_lock(&mtx);
		gettimeofday(&now, NULL);
		delta=now.tv_sec-init.tv_sec+(now.tv_usec-init.tv_usec)/(double)1e6;
		elapsed=now.tv_sec-start.tv_sec+(now.tv_usec-start.tv_usec)/(double)1e6;
		if (csv)
			printf("%.2lf, %.2lf, %.2lf, %.2lf\n",delta,cnt/(BLKSIZE*elapsed), iocnt/elapsed, total/iocnt);
		else
			printf("\r%.2lf blocks/s, %.2lf IOPS, latency %.2lf us",cnt/(BLKSIZE*elapsed), iocnt/elapsed, total/iocnt);
		cnt=0;
		iocnt=0;
		total=0;
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
			off64_t offset=((off64_t)id)*BLKSIZE;
			int count=BLKSIZE*length;
			int noise=0;
			if (!align) {
				noise=random()%((BLKSIZE-2)/2);
				offset+=noise;
				count-=noise*2;
			}
			gettimeofday(&before, NULL);
			if (ops&1) {
				if (verbose>0)
					printf("begin read %d %d %d\n", id, length, noise);
				device_pread_sync(dev, bogus, count, offset);
				if (verbose>0)
					printf("end read %d %d %.1lf\n", id, length);
				if (verify && *(int*)bogus!=id) {
					printf("expected %d got %d\n", id, *(int*)bogus);
					exit(1);
				}
			}
			if (ops&2) {
				if (verbose>0)
					printf("begin write %d %d %d\n", id, length, noise);
				device_pwrite_sync(dev, bogus, count, offset);
				if (verbose>0)
					printf("end write %d %d %.1lf\n", id, length);
			}
			gettimeofday(&now, NULL);
			elapsed=(now.tv_sec-before.tv_sec)*(double)1e6+(now.tv_usec-before.tv_usec);
			if (time!=0)
				usleep(time);
			pthread_mutex_lock(&mtx);
			cnt+=count;
			iocnt++;
			total+=elapsed;
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
	if (verbose>=0)
		pthread_create(&rep, NULL, reporter_thread, NULL);
	for(i=0;i<maxthr;i++)
		pthread_join(load[i], NULL);
}


void usage() {
	fprintf(stderr, "Usage: benchmark storage\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "\t-a -- use asynchronous I/O (default: no)\n");
	fprintf(stderr, "\t-n -- use null backend (default: no)\n");
#ifdef DISKSIM
	fprintf(stderr, "\t-S parv -- use DiskSim model (default: none)\n");
#endif
	fprintf(stderr, "\t-i -- initialize storage (default: no)\n");
	fprintf(stderr, "\t-l -- request length in pages (default: 1)\n");
	fprintf(stderr, "\t-f -- verify data read (default: no)\n");
	fprintf(stderr, "\t-t threads -- workload threads (default: 100)\n");
	fprintf(stderr, "\t-b blocks -- storage size (default: 1024 blocks)\n");
	fprintf(stderr, "\t-u -- unaligned blocks (default: no)\n");
	fprintf(stderr, "\t-r rate -- blocks/second (default: 1000)\n");
	fprintf(stderr, "\t-o rw -- read, write, or read/write workload (default: rw)\n");
	fprintf(stderr, "\t-c -- CSV output (default: no)\n");
	fprintf(stderr, "\t-v -- verbose (default: no)\n");
	fprintf(stderr, "\t-q -- quiet (default: no)\n");
	exit(1);
}

int main(int argc, char* argv[]) {
	int fd, opt, aio=0, null=0, init=0, rate=1000;
	struct device nulls, storage, sim, cow, ba, *target;

	while((opt = getopt(argc, argv, "anit:b:vr:l:fuco:qS:"))!=-1) {
		switch(opt) {
			case 'a':
				aio = 1;
				break;
			case 'n':
				null = 1;
				break;
#ifdef DISKSIM
			case 'S':
				parv = optarg;
				break;
#endif
			case 'i':
				init = 1;
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
			case 'q':
				verbose = -1;
				break;
			case 'l':
				length = atoi(optarg);
				break;
			case 'o':
				ops = (strchr(optarg, 'r')?1:0) | (strchr(optarg, 'w')?2:0);
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

	if (rate!=0) {
		time = (int)(1000000/(((double)rate)/maxthr));
		fprintf(stderr, "Target IO rate: %.2lf blocks/second (%d threads)\n", ((double)1000000)*maxthr*length/time, maxthr);
	} else {
		fprintf(stderr, "Unbounded IO rate (%d threads)\n", maxthr);
		time = 0;
	}


	if (null) {
		fprintf(stderr, "Null storage.\n");
		nullbe_open(&storage);
	} else if (aio) {
		fprintf(stderr, "Disk storage (AIO backend).\n");
		aiobe_open(&storage, argv[optind], O_RDWR|O_DIRECT, 0);
	} else {
		fprintf(stderr, "Disk storage (synchronous backend).\n");
		posixbe_open(&storage, argv[optind], O_RDWR, 0);
	}

	if (!null && init) {
		fprintf(stderr, "Initializing storage: %ld blocks of %d bytes\n", maxblk, BLKSIZE);
		workload_init(&storage);
	} else {
		if (!null) {
			struct stat sbuf;
			if (stat(argv[optind], &sbuf)<0) {
				perror(argv[optind]);
				exit(1);
			}
			maxblk = sbuf.st_size/BLKSIZE;
		}
		fprintf(stderr, "Opened: %ld blocks of %d bytes.\n", maxblk, BLKSIZE);
	}

	target = &storage;

#ifdef DISKSIM
	if (parv!=NULL) {
		fprintf(stderr, "Adding DiskSim storage performance filter: %s.\n", parv);
		simbe_open(&sim, target, parv);

		target = &sim;
	}
#endif

	workload(target);

	while(1)
		pause();
}
