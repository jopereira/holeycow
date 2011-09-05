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

#include <holeycow.h>

int main(int argc, char* argv[]) {
	int fd;
	struct device storage, snapshot, cow, ba;
	uint64_t max_size;
	struct sockaddr_in coord;

	if (argc!=4) {
		fprintf(stderr, "usage: benchmark storage snapshot port\n");
		exit(1);
	}

	fd=open(argv[1], O_RDWR|O_CREAT|O_EXCL, 0644);
	if (fd>0) {
		workload_init(fd);
		printf("Initialized.\n");
	} else {
		fd=open(argv[1], O_RDWR);
		printf("Opened.\n");
	}
	max_size=lseek(fd, 0, SEEK_END);
	close(fd);

	fd=socket(PF_INET, SOCK_STREAM, 0);

	memset(&coord, 0, sizeof(struct sockaddr_in));
	coord.sin_family = AF_INET;
	coord.sin_port = htons(atoi(argv[3]));
	inet_aton("127.0.0.1", &coord.sin_addr);

	if (connect(fd, (struct sockaddr*) &coord, sizeof(struct sockaddr_in))<0) {
		perror("connect coordination");
		exit(1);
	}
	
	posixbe_open(&storage, argv[1], O_RDWR);
	posixbe_open(&snapshot, argv[2], O_RDWR|O_CREAT);
	holey_open(&cow, &storage, &snapshot, max_size, fd);
	blockalign(&ba, &cow);

	workload(&ba);

	while(1)
		pause();
}
