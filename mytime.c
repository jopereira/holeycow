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

#include "mytime.h"
#include <sys/time.h>
#include <time.h>

static double begining_of_time = -1;

void timer_start() {

	if(begining_of_time == -1) {
		struct timeval tv;
		gettimeofday(&tv, NULL);
		begining_of_time = tv.tv_sec + (((double)tv.tv_usec) / MICROSECONDS);
	}

}

double now(int prec) {

	struct timeval tv;
 
	gettimeofday(&tv, NULL);
	
	/* return in the precision asked */
	return (double) ((tv.tv_sec + (((double)tv.tv_usec) / MICROSECONDS)) - begining_of_time) * prec;
}

void timer_stop() {
	begining_of_time = -1;
}
