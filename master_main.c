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

#include "cow.h"

int main(int argc, char* argv[]) {
	int fd;

	if (argc!=3) {
		fprintf(stderr, "usage: master ncopiers file\n");
		exit(1);
	}

	fd=open(argv[2], O_RDWR);
	workload_init(fd);
	close(fd);

	holey_init(HOLEY_SERVER_STATUS_MASTER, NULL, atoi(argv[1]), NULL);

	fd=holey_open(argv[2], O_RDWR);

	holey_start(1);

	workload(fd);
}
