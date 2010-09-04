/*
 * Holeycow-xen
 * (c) 2010 U. Minho. Written by J. Paulo
 *
 * Based on:
 *
 * Holeycow-mysql
 * (c) 2008 José Orlando Pereira, Luís Soares
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

#define VARPATH "/var/run/holey/"

struct master_sts{
  int ncopiers;
  char stderr[40];
  char log[40];
};

struct slave_sts{
  
  char cowdir[40];
  char masteradd[30];
  char stderr[40];
  char log[40];
};


