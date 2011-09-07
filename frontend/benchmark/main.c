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

void usage() {
	fprintf(stderr, "HoleyCoW mode: benchmark storage snapshot\n");
	fprintf(stderr, "Standalone mode: benchmark storage\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "\t-p port -- set HoleyCoW coordinator port (default: 12345)\n");
	fprintf(stderr, "\t-a -- use asynchronous I/O (default: no)\n");
	exit(1);
}

int main(int argc, char* argv[]) {
	int fd, opt, aio=0, port=12345;
	struct device storage, snapshot, cow, ba, *target;
	uint64_t max_size;
	struct sockaddr_in coord;

	while((opt = getopt(argc, argv, "ap:"))!=-1) {
		switch(opt) {
			case 'a':
				aio = 1;
				break;
			case 'p':
				port = atoi(optarg);
				break;
			default:
				usage();
		}
	}

	
	if (argc-optind!=1 && argc-optind!=2) {
		fprintf(stderr, "Invalid parameters %d.\n", optind);
		usage();
	}

	fd=open(argv[optind], O_RDWR|O_CREAT|O_EXCL, 0644);
	if (fd>0) {
		workload_init(fd);
		printf("Initialized.\n");
	} else {
		fd=open(argv[1], O_RDWR);
		printf("Opened.\n");
	}
	max_size=lseek(fd, 0, SEEK_END);
	close(fd);

	if (aio)
		aiobe_open(&storage, argv[optind], O_RDWR, 0);
	else
		posixbe_open(&storage, argv[optind], O_RDWR);

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
	
		if (aio)
			aiobe_open(&snapshot, argv[optind+1], O_RDWR|O_CREAT, 0644);
		else
			posixbe_open(&snapshot, argv[optind+1], O_RDWR|O_CREAT);
		holey_open(&cow, &storage, &snapshot, max_size, fd);
		blockalign(&ba, &cow);
	
		target = &ba;
	}

	workload(target);

	while(1)
		pause();
}
