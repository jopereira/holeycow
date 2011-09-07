/* block-aio.c
 *
 * Holeycow-xen
 * (c) 2010 U. Minho. Written by J. Paulo
 *
 * Based on:
 *
 * Holeycow-mysql
 * (c) 2008 José Orlando Pereira, Luís Soares
 * 
 * blktap-xen
 * (c) 2006 Andrew Warfield and Julian Chesterfield
 * 
 *
 * NB: This code is not thread-safe.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */


#include <errno.h>
#include <libaio.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <stddef.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <assert.h>

#include "tapdisk.h"
#include "holeyaio.h"
#include "blk.h"
#include "defs.h"
#include "posixbe.h"
#include <holeycow.h>

//#include "slave_master_def.h"

#define MAX_AIO_REQS (MAX_REQUESTS * MAX_SEGMENTS_PER_REQ)

/* *BSD has no O_LARGEFILE */
#ifndef O_LARGEFILE
#define O_LARGEFILE	0
#endif


int make_named_socket (const char *filename);
//debug
FILE *stream1;
int firstread;

struct tdholey_state {
	//int fd;
	holey_aio_context_t aio;
        //struct device storage, snapshot, cow, ba;
        struct device ba;
        //uint64_t max_size;
	//struct sockaddr_in coord;
        char name[200];
        char storagename[200];
        char cowname[200];
        //int master;
};


/*Get Image size, secsize*/
static int get_image_info(struct td_state *s, int fd)
{
	int ret;
	long size;
	unsigned long total_size;
	struct statvfs statBuf;
	struct stat stat;

	ret = fstat(fd, &stat);
	if (ret != 0) {
		DPRINTF("ERROR: fstat failed, Couldn't stat image");
		return -EINVAL;
	}

	if (S_ISBLK(stat.st_mode)) {
		/*Accessing block device directly*/
		if (blk_getimagesize(fd, &s->size) != 0)
			return -EINVAL;

		DPRINTF("Image size: \n\tpre sector_shift  [%llu]\n\tpost "
			"sector_shift [%llu]\n",
			(long long unsigned)(s->size << SECTOR_SHIFT),
			(long long unsigned)s->size);

		/*Get the sector size*/
		if (blk_getsectorsize(fd, &s->sector_size) != 0)
			s->sector_size = DEFAULT_SECTOR_SIZE;

	} else {
		/*Local file? try fstat instead*/
		s->size = (stat.st_size >> SECTOR_SHIFT);
		s->sector_size = DEFAULT_SECTOR_SIZE;
		DPRINTF("Image size: \n\tpre sector_shift  [%llu]\n\tpost "
			"sector_shift [%llu]\n",
			(long long unsigned)(s->size << SECTOR_SHIFT),
			(long long unsigned)s->size);
	}

	if (s->size == 0) {		
		s->size =((uint64_t) 25769807872);
		s->sector_size = DEFAULT_SECTOR_SIZE;
	}
	s->info = 0;

        //TODO hardcoded must change this in next revision
        s->size = (uint64_t) 10737418240  >> SECTOR_SHIFT;
	s->info = 0;

	return 0;
}

static inline void init_fds(struct disk_driver *dd)
{
	int i;
	struct tdholey_state *prv = (struct tdholey_state *)dd->private;

	for(i = 0; i < MAX_IOFD; i++) 
		dd->io_fd[i] = 0;

	dd->io_fd[0] = prv->aio.aio_ctx.pollfd;
}


//OPEN holy_open is only called in the first read
/* Open the disk file and initialize aio state. */
static int tdholey_open (struct disk_driver *dd, const char *name, td_flag_t flags)
{
	int i, fd, ret = 0, o_flags;
        char realname[100];
	struct td_state    *s   = dd->td_state;
	struct tdholey_state *prv = (struct tdholey_state *)dd->private;

        stream1 = freopen( HSTDERR, "a+", stderr ); 
        firstread=0;      
 

	DPRINTF("block-aio open('%s')", name);

	/* Initialize AIO */
	ret = holey_aio_init(&prv->aio, 0, MAX_AIO_REQS);
	if (ret != 0)
		return ret;

        strcpy(realname,name);
        realname[strlen(realname)-1]='\0';


	/* Open the file */
	o_flags = O_DIRECT | O_LARGEFILE | 
		((flags == TD_RDONLY) ? O_RDONLY : O_RDWR);
        fd = open(realname, o_flags);

        if ( (fd == -1) && (errno == EINVAL) ) {

                /* Maybe O_DIRECT isn't supported. */
		o_flags &= ~O_DIRECT;
                fd = open(realname, o_flags);
                if (fd != -1) DPRINTF("WARNING: Accessing image without"
                                     "O_DIRECT! (%s)\n", realname);

        } else if (fd != -1) DPRINTF("open(%s) with O_DIRECT\n", realname);
	
        if (fd == -1) {
		DPRINTF("Unable to open [%s] (%d)!\n", name, 0 - errno);
        	ret = 0 - errno;
        	goto done;
        }



        

        //prv->fd = fd;
        strcpy(prv->name,name);
        strcpy(prv->storagename,realname);

	init_fds(dd); 
        ret = get_image_info(s, fd);
       
done:
	return ret;	
}

static int tdholey_queue_read(struct disk_driver *dd, uint64_t sector,
		     int nb_sectors, char *buf, td_callback_t cb,
		     int id, void *private)
{
	struct   td_state    *s   = dd->td_state;
	struct   tdholey_state *prv = (struct tdholey_state *)dd->private;
	int      size    = nb_sectors * s->sector_size;
	uint64_t offset  = sector * (uint64_t)s->sector_size;
        //int idvm,master, o_flags;
        //char realname[100];
        int ret;
       
        if(firstread==0){        
           char path[200];
           int fd;
	  struct device storage, snapshot, cow;
	  uint64_t max_size;
	  struct sockaddr_in coord;
          
          struct args_sts r;     

          int newsockfd,bcount,size,br;
          
          mkdir(VARPATH,S_IRWXU);

          strcpy(path,VARPATH);
          strcat(path,&prv->name[strlen(prv->name)-1]);

          newsockfd = make_named_socket (path);
  

          size = sizeof(r);
          bcount= 0;
          br= 0;
          while (bcount < size) {             /* loop until full buffer */
             if ((br= recv(newsockfd,(&r)+bcount,size-bcount,0)) > 0) {
               //printf("no while bcount %d %d %d\n",bcount,br,size-bcount);
               bcount += br;                /* increment byte counter */
               /* move buffer ptr for next read */
          }
          else if (br < 0)               /* signal an error to the caller */
            perror("ERROR reading from socket");
          } 
   
          strcpy(HLOG,r.log);
          strcpy(HSTDERR,r.stderr);
          strcpy(prv->cowname,r.cowdir);

          close(newsockfd);
          unlink(path);  

          fd=open(prv->storagename, O_RDWR);
	  max_size=lseek(fd, 0, SEEK_END);
	  close(fd);

	  fd=socket(PF_INET, SOCK_STREAM, 0);

	  memset(&coord, 0, sizeof(struct sockaddr_in));
	  coord.sin_family = AF_INET;
	  coord.sin_port = htons(12345);
	  inet_aton("127.0.0.1", &coord.sin_addr);

	  if (connect(fd, (struct sockaddr*) &coord, sizeof(struct sockaddr_in))<0) {
		perror("connect coordination");
		exit(1); 
          }
	
          posixbe_open(&storage, prv->storagename, O_RDWR | O_LARGEFILE, 0);
          posixbe_open(&snapshot, prv->cowname, O_RDWR | O_LARGEFILE, 0);
          holey_open(&cow, &storage, &snapshot, max_size, fd);
          blockalign(&(prv->ba), &cow);

          sleep(15);

          firstread=1;
        }
        
        ret = device_pread_sync(&(prv->ba), buf, size, offset);
        //TODO this must be changed when block align bug is fixed
        if (ret != size) {
			ret = 0 - errno;
	} else {
			ret = 1;
	} 
        

        return cb(dd, (ret < 0) ? ret: 0, sector, nb_sectors, id, private);

}
			
static int tdholey_queue_write(struct disk_driver *dd, uint64_t sector,
		      int nb_sectors, char *buf, td_callback_t cb,
		      int id, void *private)
{
         
	struct   td_state    *s   = dd->td_state;
	struct   tdholey_state *prv = (struct tdholey_state *)dd->private;
	int      size    = nb_sectors * s->sector_size;
	uint64_t offset  = sector * (uint64_t)s->sector_size;
        int ret;     
       
        ret = device_pwrite_sync(&(prv->ba), buf, size, offset);
        if (ret != size) {
			ret = 0 - errno;
	} else {
			ret = 1;
	}
       
        return cb(dd, (ret < 0) ? ret: 0, sector, nb_sectors, id, private);
}

static int tdholey_submit(struct disk_driver *dd)
{
        //when we change to assync
	//struct tdholey_state *prv = (struct tdholey_state *)dd->private;

	//return holey_aio_submit(&prv->aio);
        
        return 0;
}
			
static int tdholey_close(struct disk_driver *dd)
{
	struct tdholey_state *prv = (struct tdholey_state *)dd->private;
	
	io_destroy(prv->aio.aio_ctx.aio_ctx);
	//holey_close(prv->fd);

	return 0;
}

static int tdholey_do_callbacks(struct disk_driver *dd, int sid)
{
        //return rsp;
        return 1;
}

static int tdholey_get_parent_id(struct disk_driver *dd, struct disk_id *id)
{
	return TD_NO_PARENT;
}

static int tdholey_validate_parent(struct disk_driver *dd, 
			  struct disk_driver *parent, td_flag_t flags)
{
	return -EINVAL;
}

struct tap_disk tapdisk_holey = {
	.disk_type          = "tapdisk_holey",
	.private_data_size  = sizeof(struct tdholey_state),
	.td_open            = tdholey_open,
	.td_queue_read      = tdholey_queue_read,
	.td_queue_write     = tdholey_queue_write,
	.td_submit          = tdholey_submit,
	.td_close           = tdholey_close,
	.td_do_callbacks    = tdholey_do_callbacks,
	.td_get_parent_id   = tdholey_get_parent_id,
	.td_validate_parent = tdholey_validate_parent
};

/************************************* AUX function***********************/

int make_named_socket (const char *filename)
{
  struct sockaddr_un name;
  int sock;
  size_t size;

  /* Create the socket. */
  
  sock = socket (PF_UNIX, SOCK_DGRAM, 0);
  if (sock < 0)
    {
      perror ("socket");
      exit (EXIT_FAILURE);
    }

  /* Bind a name to the socket. */

  name.sun_family = AF_FILE;
  strcpy (name.sun_path, filename);

  /* The size of the address is
     the offset of the start of the filename,
     plus its length,
     plus one for the terminating null byte. */
  size = (offsetof (struct sockaddr_un, sun_path)
          + strlen (name.sun_path) + 1);

  if (bind (sock, (struct sockaddr *) &name, size) < 0)
    {
      perror ("bind");
      exit (EXIT_FAILURE);
    }

  return sock;
}


