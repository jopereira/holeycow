/* block-aio.c
 *
 * libaio-based raw disk implementation.
 *
 * (c) 2006 Andrew Warfield and Julian Chesterfield
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
#include "tapdisk.h"
#include "holeyaio.h"
#include "blk.h"

#include "holeycow.c"
//#include "cow.h"
//#include "stability.h"
//#include "defs.h"
//#include "mytime.h"

#define MAX_AIO_REQS (MAX_REQUESTS * MAX_SEGMENTS_PER_REQ)

/* *BSD has no O_LARGEFILE */
#ifndef O_LARGEFILE
#define O_LARGEFILE	0
#endif

//debug
FILE *stream2;
FILE *stream1;
int firstread;

struct tdholey_state {
	int fd;
	holey_aio_context_t aio;
        char name[200];
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


//OPEN normal faz so o holy_open na primeira leitura....
/* Open the disk file and initialize aio state. */
static int tdholey_open (struct disk_driver *dd, const char *name, td_flag_t flags)
{
	int i, fd, ret = 0, o_flags;
        //int master;
        FILE *fp;
        char realname[100];
	struct td_state    *s   = dd->td_state;
	struct tdholey_state *prv = (struct tdholey_state *)dd->private;

        stream1 = freopen( "/home/jtpaulo/holey/logs/stdout", "a+", stdout);
        stream2 = freopen( "/home/jtpaulo/holey/logs/stderr", "a+", stderr ); 
        firstread=0;      
 

	DPRINTF("block-aio open('%s')", name);

	/* Initialize AIO */
	ret = holey_aio_init(&prv->aio, 0, MAX_AIO_REQS);
	if (ret != 0)
		return ret;

        fp=fopen("/home/jtpaulo/holey/logs/open","a");
        fprintf(fp,"antes ver master\n");
        fclose(fp);
        
        /*
        master = atoi(&name[strlen(name)-1]);

        fp=fopen("home/jtpaulo/holey/logs/open","a");
        fprintf(fp,"master = %d\n",master);
        fclose(fp);

        
        //Holey initialization......
        if(master==1){
          holey_init(HOLEY_SERVER_STATUS_MASTER, NULL, 1, NULL);
        }
        else{
          //holey_init(HOLEY_SERVER_STATUS_SLAVE, "127.0.0.1", 1000, "/storage/jtpaulo-msc/cow");
        }

        fp=fopen("home/jtpaulo/holey/logs/open","a");
        fprintf(fp,"depois init %d\n",master);
        fclose(fp);

        */

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

        prv->fd = fd;
        strcpy(prv->name,name);

	init_fds(dd); 
        //Duvida get_image_info_sera necessário???
        //devo ter de mudar aqui.... abir storage de master ou slave consoante....
        fp=fopen("/home/jtpaulo/holey/logs/open","a");
        fprintf(fp,"antes get_image\n");
        fclose(fp);
        
        //if(master==1){
	  ret = get_image_info(s, fd);
        //}
        //else{
          //DUVIDA isto estará bem??? ou deveria abrir da storage na mesma???
          //ret = get_image_info(s, herd[fd].snapshot);
        //}

        fp=fopen("/home/jtpaulo/holey/logs/open","a");
        fprintf(fp,"fim get_image\n");
        fclose(fp);

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
        int master, o_flags;
        char realname[100];
        int ret;
        FILE *fp;       

        master = atoi(&prv->name[strlen(prv->name)-1]);

        if(firstread==0){        

          
          fp=fopen("/home/jtpaulo/holey/logs/firstread","a");
          fprintf(fp,"master = %d\n",master);
          fclose(fp);

        
          //Holey initialization......
          if(master==1){
            holey_init(HOLEY_SERVER_STATUS_MASTER, NULL, 1, NULL);
          }
          else{
            holey_init(HOLEY_SERVER_STATUS_SLAVE, "127.0.0.1", 1000, "/storage/jtpaulo-msc/cow");
          }

          fp=fopen("home/jtpaulo/holey/logs/firstread","a");
          fprintf(fp,"depois init %d\n",master);
          fclose(fp);

          strcpy(realname,prv->name);
          realname[strlen(realname)-1]='\0';


	  /* Open the file */
	  o_flags = O_DIRECT | O_LARGEFILE | O_RDWR;
          prv->fd = holey_open(realname, o_flags);

          if ( (prv->fd == -1) && (errno == EINVAL) ) {

                /* Maybe O_DIRECT isn't supported. */
		o_flags &= ~O_DIRECT;
                prv->fd = holey_open(realname, o_flags);
                if (prv->fd != -1) DPRINTF("WARNING: Accessing image without"
                                     "O_DIRECT! (%s)\n", realname);

          } else if (prv->fd != -1) DPRINTF("open(%s) with O_DIRECT\n", realname);
	
          if (prv->fd == -1) {
		DPRINTF("Unable to open [%s] (%d)!\n", realname, 0 - errno);
          }
          
          holey_start(1);

          firstread=1;
        }
        
 
        fp=fopen("home/jtpaulo/holey/logs/read","a");
        fprintf(fp,"read... %d offset %llu size %d %d %llu\n",master,offset,size,id,sector);
        fclose(fp);


       
        
        ret = holey_pread(prv->fd,buf,size,offset,&prv->aio,cb,id,sector,private);
        if (ret != size) {
			ret = 0 - errno;
	} else {
			ret = 1;
	} 
	
        

        fp=fopen("home/jtpaulo/holey/logs/read","a");
        fprintf(fp,"read... %d offset %llu size %d AGENDADO %d %llu ret %d\n",master,offset,size,id,sector,ret);
        fclose(fp);
        

        return cb(dd, (ret < 0) ? ret: 0, sector, nb_sectors, id, private);

	//return holey_aio_read(&prv->aio, prv->fd, size, offset, buf, 
		//cb, id, sector, private);
}
			
static int tdholey_queue_write(struct disk_driver *dd, uint64_t sector,
		      int nb_sectors, char *buf, td_callback_t cb,
		      int id, void *private)
{
         
	struct   td_state    *s   = dd->td_state;
	struct   tdholey_state *prv = (struct tdholey_state *)dd->private;
	int      size    = nb_sectors * s->sector_size;
	uint64_t offset  = sector * (uint64_t)s->sector_size;
        FILE *fp;
        int master;  
        int ret;     
        int wait =0; 
        uint64_t boff,cursor;

        
        struct disk_driver **aux= malloc(sizeof(struct disk_driver *));
        *aux = dd;

        cursor=offset&OFFMASK;
        boff=cursor>>FDBITS;

        master = atoi(&prv->name[strlen(prv->name)-1]);

        fp=fopen("home/jtpaulo/holey/logs/write","a");
        fprintf(fp,"write... %d offset %llu size %d %d %llu\n",master,offset,size,id,sector);
        fclose(fp);
       

        ret = holey_pwrite(prv->fd,buf,size,offset,&prv->aio,cb,id,sector,nb_sectors,private,aux,&wait);
        if (ret != size) {
			ret = 0 - errno;
	} else {
			ret = 1;
	}
       
        
        
        if(wait==1){

          fp=fopen("home/jtpaulo/holey/logs/writecallback","a");
          fprintf(fp,"antes bloquear era 1 write... %d offset %llu size %d AGENDADo %d %llu rest%d\n",master,offset,size,id,sector,ret);
          fclose(fp);

          
          
          
          pthread_cond_wait(&writecomp, &writemutex);
 

          pthread_mutex_unlock(&writemutex);

          fp=fopen("home/jtpaulo/holey/logs/writecallback","a");
          fprintf(fp,"sai wait era 1 write... %d offset %llu size %d AGENDADo %d %llu ret %d boff %llu \n",master,offset,size,id,sector,ret,boff);
          fclose(fp);
          
          

          ret = pwrite(herd.storage, herd.cache[boff].data, BLKSIZE, offset);
          if (ret != size) {
			ret = 0 - errno;
	  } else {
			ret = 1;
	  } 
	  
          fp=fopen("home/jtpaulo/holey/logs/writecallback","a");
          fprintf(fp,"done era 1 write... %d offset %llu size %d AGENDADo %d %llu ret %d\n",master,offset,size,id,sector,ret);
          fclose(fp);
          

         }

        //TODO alterar para assync
        ret = cb(dd, (ret < 0) ? ret: 0, sector, nb_sectors, id, private);

        if(master ==1){
          pthread_mutex_unlock(&mutex_cow);

        }

        if(wait==1){

           fp=fopen("home/jtpaulo/holey/logs/writecallback","a");
          fprintf(fp,"antes de fazer free... %d offset %llu size %d AGENDADo %d %llu ret %d\n",master,offset,size,id,sector,ret);
          fclose(fp);
          
          //TODO código a ser colado nos callbacks ver os locks....
          //free(herd.cache[boff].data);
          herd.cache[boff].data=NULL;
          st_w_stab_blks++;

        }
 
        fp=fopen("home/jtpaulo/holey/logs/write","a");
        fprintf(fp,"write... %d offset %llu size %d AGENDADo %d %llu\n",master,offset,size,id,sector);
        fclose(fp);
        

        //TODO alterar para assync
        return ret;
	//return holey_aio_write(&prv->aio, prv->fd, size, offset, buf,
		//cb, id, sector, private);
}

static int tdholey_submit(struct disk_driver *dd)
{
        //TODO alterar para ssync
	//struct tdholey_state *prv = (struct tdholey_state *)dd->private;

	//return holey_aio_submit(&prv->aio);
        
        return 0;
}
			
static int tdholey_close(struct disk_driver *dd)
{
	struct tdholey_state *prv = (struct tdholey_state *)dd->private;
	
	io_destroy(prv->aio.aio_ctx.aio_ctx);
	holey_close(prv->fd);

	return 0;
}

static int tdholey_do_callbacks(struct disk_driver *dd, int sid)
{
        FILE* fp;
	int i, nr_events, rsp = 0;
	struct io_event *ep;
	struct tdholey_state *prv = (struct tdholey_state *)dd->private;

        fp=fopen("home/jtpaulo/holey/logs/aiolog","a");
        fprintf(fp,"do callbacks...\n");
        fclose(fp);

	nr_events = holey_aio_get_events(&prv->aio.aio_ctx);
repeat:
	for (ep = prv->aio.aio_events, i = nr_events; i-- > 0; ep++) {
		struct iocb        *io  = ep->obj;
		struct pending_aio *pio;
		
                

		pio = &prv->aio.pending_aio[(long)io->data];
 
                fp=fopen("home/jtpaulo/holey/logs/callbacks","a");
                fprintf(fp,"antes de chamar o calback %d %llu\n",pio->id,pio->sector);
                fclose(fp);

              
		rsp += pio->cb(dd, ep->res == io->u.c.nbytes ? 0 : 1,
			       pio->sector, io->u.c.nbytes >> 9, 
			       pio->id, pio->private);

                pthread_mutex_unlock(&mutex_cow);

                fp=fopen("home/jtpaulo/holey/logs/callbacks","a");
                fprintf(fp,"%d %llu\n",pio->id,pio->sector);
                fclose(fp);

		prv->aio.iocb_free[prv->aio.iocb_free_count++] = io;
	}

	if (nr_events) {
		nr_events = holey_aio_more_events(&prv->aio.aio_ctx);
		goto repeat;
	}

	holey_aio_continue(&prv->aio.aio_ctx);
      
        //TODO alterar para assync
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
