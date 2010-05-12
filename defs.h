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

#define HLOG "/home/jtpaulo/holey/logs/log"
#define HSTDERR "/home/jtpaulo/holey/logs/stderr"
#define NSLAVES 1
#define MASTER_ADD "127.0.0.1"
#define COW_DIR "/storage/jtpaulo-msc/cow"

#ifndef __DEFS__
#define __DEFS__

#define MAXFILES 16
//changed here
#define BLKSIZE (4*1024)

#endif
