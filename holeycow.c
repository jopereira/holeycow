/*
 * Holeycow-xen
 * (c) 2010 U. Minho. Written by J. Paulo
 *
 * Based on:
 *
 * Holeycow-mysql
 * (c) 2008 José Orlando Pereira, Luís Soares
 * 
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
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#include "cow.h"
#include "stability.h"
#include "defs.h"
#include "mytime.h"

//#include "tapdisk.h"

#define STAB_PORT	12345
#define STAB_QUEUE	1000

pthread_mutex_t mutex_cow = PTHREAD_MUTEX_INITIALIZER; 


static int ncow;
static int slaveid;
static char* cow_basedir;


struct ctap{

    void* data;
        
};

struct {
	/* common */
	int storage;
	char* storage_fname;
	int *bitmap;

	/* master variables */
        struct ctap *cache;
 

	/* slave variables */
	int snapshot;
	char* snapshot_fname;
 
  
} herd;
 
/* stats */
unsigned long st_r_stab_blks;
unsigned long st_r_reg_blks;
unsigned long st_w_stab_blks;
unsigned long st_w_reg_blks;
unsigned long st_frag_blks;
unsigned long st_fetch_blks;
unsigned long st_c_blks;
unsigned long st_n_blks;
unsigned long st_n_msgs;

static void* stats_thread(void* p) {

	while(1) {
		sleep(10);
		printf("*HC* %.0lf %ld %ld %ld %ld %ld %ld %ld %ld %ld\n", 
			now(SECONDS), 
			st_r_reg_blks, 
			st_w_reg_blks,
			st_r_stab_blks,
			st_w_stab_blks,
			st_frag_blks,
			st_fetch_blks,
			st_c_blks,
			st_n_blks,
			st_n_msgs);
	}
}

/*
 * no master: reg (normais), stab (atrasados pelo stab), frag
 * no slave: reg (da storage), stab (da copia), frag, fetch (adicionais)
 */

static inline int test_and_set(uint64_t id) {
        
        int result;
	uint64_t boff=(id&OFFMASK)>>FDBITS;
	
        uint64_t idx=boff/(8*sizeof(int));
	uint64_t mask=1LLU<<(boff%(8*sizeof(int))); 
        	
        result=herd.bitmap[idx]&mask;
	herd.bitmap[idx]|=mask;
	if (!result) st_c_blks++;
	return result;
}

static inline int test(uint64_t id) {
	
        int result;
       	uint64_t boff=(id&OFFMASK)>>FDBITS;
	
	uint64_t idx=boff/(8*sizeof(int));
	uint64_t mask=1LLU<<(boff%(8*sizeof(int)));

        result=herd.bitmap[idx]&mask;
	return result;
}

/*************************************************************
 * MASTER Functions
 */

static void master_cb(block_t id) {

        FILE *fp;

        int ft;
        
        
        off_t offset;
        uint64_t boff;
        int res=0;
 
        pthread_mutex_lock(&mutex_cow);
        
        offset=id&OFFMASK;
	boff=offset>>FDBITS;

        pwrite(herd.storage, herd.cache[boff].data, BLKSIZE, offset);
	
        free(herd.cache[boff].data);
	herd.cache[boff].data=NULL;

	st_w_stab_blks++;

	fp=fopen(HLOG,"a");
        fprintf(fp,"MASTER: storage persistent - block\n");
        fclose(fp);

        pthread_mutex_unlock(&mutex_cow);


}

static int master_open(char* path, int flags) {

        int res;
        int fdaux;
        uint64_t maxblk;  

	pthread_mutex_lock(&mutex_cow);
	

	// FIXME: should propagate permission flags
	fdaux = open(path, flags, 0644);
	if(fdaux < 0) {
		res = -1;
	} else {
	
                herd.storage=fdaux;
		herd.storage_fname = strdup(path);

		maxblk=lseek(herd.storage, 0, SEEK_END)/BLKSIZE;
		lseek(herd.storage, 0, SEEK_SET);

		herd.bitmap=(int*)calloc(maxblk/8+sizeof(int),1);
		herd.cache=(struct ctap*)calloc(maxblk,sizeof(struct ctap));

		res = 0;
	}

	pthread_mutex_unlock(&mutex_cow);

	return res;
}


static int master_pwrite(int fde,void* data, size_t count, off_t offset){

        int done;
        int ft;
        int res;

      	pthread_mutex_lock(&mutex_cow);

        done=0;

	while(count>0) {
		uint64_t cursor=offset&OFFMASK;
		int bcount=offset+count > cursor+BLKSIZE ?
						cursor+BLKSIZE-offset : count;

		block_t id=cursor;
		uint64_t boff=cursor>>FDBITS;

		if (test_and_set(id)) {
                         pwrite(herd.storage, data, bcount, offset);
                         
			 st_w_reg_blks++;
		} else {

                         if (herd.cache[boff].data == NULL) {
                                herd.cache[boff].data=malloc(BLKSIZE);
                        	if (bcount!=BLKSIZE) {

                                       st_frag_blks++;
                                       //Sync
				       pread(herd.storage, herd.cache[boff].data, BLKSIZE, cursor);
				}
                                             
				add_block(id);
                                
                                
			}
                        memcpy(herd.cache[boff].data+(offset-cursor), data, bcount);
                        //TODO If wait_sync is not commented we have deadlock here...
                        //wait_sync(0);
                       

		}

		offset+=bcount;
		count-=bcount;
		data+=bcount;
                done+=bcount;
	}

     
        pthread_mutex_unlock(&mutex_cow);

     	return done;
}

static int master_pread(int fde,void* data, size_t count, off_t offset){

        int done;
        
       
        pthread_mutex_lock(&mutex_cow);

        
	done =0;

	while(count>0) {
		uint64_t cursor=offset&OFFMASK;
		int bcount=offset+count > cursor+BLKSIZE ?
						cursor+BLKSIZE-offset : count;

		block_t id=cursor;
		uint64_t boff=cursor>>FDBITS;

                

		if (herd.cache[boff].data!=NULL) {

                        st_r_stab_blks++;
                        memcpy(data, herd.cache[boff].data+(offset-cursor), bcount);
		} else {
                        st_r_reg_blks++;
                        pread(herd.storage, data, bcount, offset);
		}

               
		offset+=bcount;
		count-=bcount;
		data+=bcount;
		done+=bcount;
	}

       pthread_mutex_unlock(&mutex_cow);

      
	return done;
}

static int master_fsync(int dum) {
	/* printf("MASTER: SYNCing...\n"); */

	wait_sync(0);

	pthread_mutex_lock(&mutex_cow);
	fsync(herd.storage);
	pthread_mutex_unlock(&mutex_cow);

	/* printf("MASTER: SYNC DONE \n"); */

	return 0;
}

static void master_init(int nslaves, int sz) {
        
	int sfd,*fd,len,i,j,on=1;
	struct sockaddr_in master, slave;
                    

        fd=(int*)calloc(nslaves, sizeof(int));

	sfd=socket(PF_INET, SOCK_STREAM, 0);

	if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
		perror("setsockopt(SO_REUSEADDR) failed");
	}

	memset(&master, 0, sizeof(master));
	master.sin_family = PF_INET;
	master.sin_port = htons(STAB_PORT);
        master.sin_addr.s_addr = htonl(INADDR_ANY);

        
	while (bind(sfd, (struct sockaddr*)&master, sizeof(master))<0) {
                perror("port 12345");
                sleep(20);
		//exit(1);
	}

        listen(sfd, SOMAXCONN);


        
        
	for(i=0;i<nslaves;i++) {
                len=0;
		memset(&slave, 0, sizeof(slave));

                fd[i] = accept(sfd, (struct sockaddr*)&slave,(socklen_t*)&len);

        }

        
	master_stab(fd, nslaves, sz, master_cb);
}

static off_t master_lseek(int fde,off_t offset, int whence) {
	off_t ret;

	pthread_mutex_lock(&mutex_cow);
	ret = lseek(herd.storage, offset, whence);	
	pthread_mutex_unlock(&mutex_cow);

	return ret;
}

static int master_close(int dum) {
	pthread_mutex_lock(&mutex_cow);

	close(herd.storage);
	free(herd.storage_fname);

	// FIXME: should't assume LIFO open/close
	//ncow--;
	pthread_mutex_unlock(&mutex_cow);
        //ADDED
        return 0;
}

/*****************************************************
 * Slave Functions
 */

static void slave_cb(block_t id) {

        FILE* fp;


        
	pthread_mutex_lock(&mutex_cow);
	if (!test_and_set(id)) {

		char data[BLKSIZE];
		off_t offset=id&OFFMASK;

                pread(herd.storage, data, BLKSIZE, offset);
		pwrite(herd.snapshot, data, BLKSIZE, offset);

                fp=fopen(HLOG,"a");
                fprintf(fp,"SLAVE: copied block %llu\n", id);
                fclose(fp);
		
		/* update stats */
		st_fetch_blks ++;

	} /*else {
		printf("SLAVE: block %d already copied\n", id);
	}*/
	pthread_mutex_unlock(&mutex_cow);

        


}


static int slave_pwrite(int fde, void* data, size_t count, off_t offset) {
        int done;
 
     
    
	pthread_mutex_lock(&mutex_cow);

	done=0;

	while(count>0) {
		uint64_t cursor=offset&OFFMASK;
		int bcount=offset+count > cursor+BLKSIZE ?
						cursor+BLKSIZE-offset : count;
                
               

		block_t id=cursor;
            
               
		char tmp[BLKSIZE];
		void* buf=data;
		int ecount=bcount;
		uint64_t eoffset=offset;
 
                
		if (!test_and_set(id) && bcount!=BLKSIZE) {
			st_frag_blks++;
			pread(herd.storage, tmp, BLKSIZE, cursor);
			memcpy(tmp+(offset-cursor), data, bcount);
			buf=tmp;
			ecount=BLKSIZE;
			eoffset=cursor;
                       
		}

                pwrite(herd.snapshot, buf, ecount, eoffset);

                st_w_stab_blks++;

		offset+=bcount;
		count-=bcount;
		data+=bcount;
                done+=bcount;
	}

	pthread_mutex_unlock(&mutex_cow);

      

	return done;
}

static int slave_pread(int fde,void* data, size_t count, off_t offset) {
        int done;

         
        pthread_mutex_lock(&mutex_cow);
        

	done=0;

	while(count>0) {
		uint64_t cursor=offset&OFFMASK;
		int bcount=offset+count > cursor+BLKSIZE ?
						cursor+BLKSIZE-offset : count;

        
		block_t id=cursor;
                int src;
        

		
		if (test(id)) {
        		src=herd.snapshot;
			st_r_stab_blks++;
		} else {
        		src=herd.storage;
			st_r_reg_blks++;
		}
		
                pread(src, data, bcount, offset);

                offset+=bcount;
		count-=bcount;
		data+=bcount;
		done+=bcount;
	}

       pthread_mutex_unlock(&mutex_cow);
   
     
       return done;
}

static int slave_fsync(int dum) {
	/* printf("SLAVE: SYNCing...\n"); */

	/* printf("SLAVE: SYNC DONE\n"); */

	return 0;
}

static int slave_open(char* path, int flags) {

        int res;
	int fdaux;

	pthread_mutex_lock(&mutex_cow);

	/* TODO: what about O_RDONLY flag */
	// FIXME: should propagate permission flags
        
	fdaux = open(path, flags, 0644);

	if(fdaux < 0) {
		res = fdaux;
	} else {
		char name[200];
		char stats_fname[200];
		char c;
		char* fname;
		int i;
		off_t max_size, snap_size; 

		/* get the filename */
		fname = rindex(path, '/') == NULL ? path : (rindex(path, '/') +1);
		sprintf(name, "%s%s.%d.cow", cow_basedir, fname, slaveid);

		/* collect file descriptors */
		herd.storage=fdaux;
		herd.storage_fname = strdup(path);

		herd.snapshot=open(name, O_RDWR|O_CREAT, 0644);
		herd.snapshot_fname = strdup(name);

		/* get storage size */
		max_size = lseek(herd.storage, 0, SEEK_END);
		snap_size = lseek(herd.snapshot, 0, SEEK_END);

		/* grow snapshot to the size of the original file */
		if(snap_size != max_size) {
			lseek(herd.snapshot, 0, SEEK_SET);
			lseek(herd.snapshot, max_size-1, SEEK_END);
			read(herd.snapshot, &c, 1);
			write(herd.snapshot, &c, 1);

		}

		/* reset offset to begining of storage and snapshot */
		lseek(herd.snapshot, 0, SEEK_SET);
		lseek(herd.storage, 0, SEEK_SET);

		/* create the bitmap */
		herd.bitmap=(int*)calloc((max_size/BLKSIZE)/8+sizeof(int), 1);

		res = 0;
	}

	pthread_mutex_unlock(&mutex_cow);
	return res;
}

static void slave_init(char* addr, int sz, char* p_cow_basedir) {
	int fd;
	struct sockaddr_in master;
	char* append;

	cow_basedir = (char*) malloc(strlen(p_cow_basedir) * sizeof(char) + 1);
	sprintf(cow_basedir, "%s/", p_cow_basedir);

	fd=socket(PF_INET, SOCK_STREAM, 0);

	memset(&master, 0, sizeof(master));
	master.sin_family = AF_INET;
	master.sin_port = htons(STAB_PORT);
	inet_aton(addr, &master.sin_addr);

	if (connect(fd, (struct sockaddr*)&master, sizeof(master))<0) {
		perror(addr);
		exit(1);
	}

	/* stats */
	timer_start(0);

	slaveid=slave_stab(fd, sz, slave_cb);
}

static off_t slave_lseek(int fde, off_t offset, int whence) {
	off_t ret;

	pthread_mutex_lock(&mutex_cow);
	ret = lseek(herd.snapshot, offset, whence);	
	pthread_mutex_unlock(&mutex_cow);

	return ret;

}

static int slave_close(int dum) {

	/* TODO: assuming that reopen is on the same order */
	pthread_mutex_lock(&mutex_cow);
	
	close(herd.storage);
	close(herd.snapshot);

	free(herd.storage_fname);
	free(herd.snapshot_fname);

	// Shouldn't assume LIFO open/close order
	//ncow--;
	pthread_mutex_unlock(&mutex_cow);
        //ADDED
        return 0;

}

/******************************************
 * Regular Function mapping 
 */
static void holey_cow_start_null(int size) { 
	return; 
}

static int orig_open(char* path, int flags) { 
	// FIXME: should propagate permission flags
	return open(path, flags, 0644);
}

static int orig_close(int fd) {
	return close(fd);
}

//FIXME We must change here the return...
static int orig_pwrite(int fd, void* data, size_t count, off_t offset) {
	pthread_mutex_lock(&mutex_cow);
	st_w_reg_blks++;
	pthread_mutex_unlock(&mutex_cow);
        return pwrite(fd, data, count, offset);
}

static int orig_pread(int fd, void* data, size_t count, off_t offset) {
	pthread_mutex_lock(&mutex_cow);
	st_r_reg_blks++;
	pthread_mutex_unlock(&mutex_cow);
        return pread(fd, data, count, offset);
}

static int orig_fsync(int fd) {
	fsync(fd);
        //ADDED
        return 0;
}

static off_t orig_lseek(int fd, off_t offset, int whence) {
	return lseek(fd, offset, whence);
}


/***
 * Initialize function.
 */
void holey_init(int profile, char* master_ip, int n_slaves, char* cow_basedir) {
	pthread_t logger;
        FILE* fp;

        fp = fopen(HLOG,"a");
	fprintf(fp,"Holey init: %d - %s - %d - %s\n", profile, master_ip, n_slaves, cow_basedir);
        fclose(fp);        

	pthread_create(&logger, NULL, stats_thread, NULL);

	switch(profile) {
		
		case HOLEY_SERVER_STATUS_MASTER:
                       	printf("Selected Holey Server Profile: MASTER\n");

			holey_start = master_start;
			holey_open = master_open;
			holey_close = master_close;
			holey_pwrite = master_pwrite;
			holey_pread = master_pread;
			holey_fsync = master_fsync;
			holey_lseek = master_lseek;
		
                        
			master_init(n_slaves, STAB_QUEUE);
                        break;

		case HOLEY_SERVER_STATUS_SLAVE:
			printf("Selected Holey Server Profile: SLAVE\n");

			holey_start = slave_start;
			holey_open = slave_open;
			holey_close = slave_close;
			holey_pwrite = slave_pwrite;
			holey_pread = slave_pread;
			holey_fsync = slave_fsync;
			holey_lseek = slave_lseek;

			slave_init(master_ip, STAB_QUEUE, cow_basedir);
			break;
	
		case HOLEY_SERVER_STATUS_INACTIVE:
		default:
			printf("Selected Holey Server Profile: INACTIVE\n");
			holey_start = holey_cow_start_null;
			holey_open = orig_open;
			holey_close = orig_close;
			holey_pwrite = orig_pwrite;
			holey_pread = orig_pread;
			holey_fsync = orig_fsync;
			holey_lseek = orig_lseek;
			break;
		
	}
}


/* initially functions point to null ones */
void (*holey_start)(int) = holey_cow_start_null;
int (*holey_open)(char*, int) = orig_open;
int (*holey_close)(int) = orig_close;
int (*holey_pwrite)(int, void*, size_t, off_t) = orig_pwrite;
int (*holey_pread)(int, void*, size_t, off_t) = orig_pread;
int (*holey_fsync)(int) = orig_fsync;
off_t (*holey_lseek)(int, off_t, int) = orig_lseek;


