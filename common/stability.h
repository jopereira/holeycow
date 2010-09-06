/*
 * HoleyCoW
 * Copyright (c) 2008-2010 Universidade do Minho
 * Written by José Pereira, Luis Soares, and J. Paulo
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

#include <stdint.h>

#ifndef __STAB__
#define __STAB__

typedef uint64_t block_t;
typedef void (*callback_t)(block_t, void*);

/* master aka designated writer */
void master_stab(int* sock, int nslaves, int s, callback_t);
void master_start(int npool);
int add_block(block_t id, void*);
void wait_sync(int dump);

/* slave aka replica */
int slave_stab(int sock, int s, callback_t cb, void* cookie);
void slave_start(int npool);

#endif
