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

#ifndef __COW__
#define __COW__

#include "defs.h"

#define HOLEY_SERVER_STATUS_INACTIVE 0
#define HOLEY_SERVER_STATUS_SLAVE 1
#define HOLEY_SERVER_STATUS_MASTER 2

#define FDBITS 12
#define FDMASK ((1<<FDBITS)-1)
#define OFFMASK (~((1<<FDBITS)-1))

#define GETBLK(id) (id&OFFMASK)>>FDBITS
#define GETFD(id) (id&FDMASK)

/* initially functions point to null ones */
extern void (*holey_start)(int);
extern int (*holey_open)(char*, int);
extern int (*holey_close)(int);
extern int (*holey_pwrite)(int, void*, size_t, off_t, holey_aio_context_t*, td_callback_t, int, uint64_t, int, void*,struct disk_driver**,int *);
extern int (*holey_pread)(int, void*, size_t, off_t, holey_aio_context_t*, td_callback_t, int, uint64_t, void*);
extern int (*holey_fsync)(int);
extern off_t (*holey_lseek)(int, off_t, int);

/*
 * @profile
 *   0 - inactive
 *   1 - slave
 *   2 - master
 *
 * @master_ip
 *   ip address of master (slaves only)
 *
 * @n_slaves
 * 	 the number of slaves (master only)
 *
 * @cow_basedir
 * 	 the directory path holding the snapshots	
 */
void holey_init(int profile, char* master_ip, int n_slaves, char* cow_basedir);

#endif
