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
//#include "master_stab.c"
//#include "slave_stab.c"
//#include "mytime.c"

//#include "tapdisk.h"
#include "holeyaio.h"

#define STAB_PORT	12345
#define STAB_QUEUE	1000

pthread_mutex_t mutex_cow = PTHREAD_MUTEX_INITIALIZER; 

static int ncow;
static int slaveid;
static char* cow_basedir;

struct {
	/* common */
	int storage;
	char* storage_fname;
	int *bitmap;

	/* master variables */
	void **cache;

	/* slave variables */
	int snapshot;
	char* snapshot_fname;

} herd[MAXFILES];
 
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
        //isto da sempre pequeno mas havera prob de ter o if uint64 ??'
	int fd=id&FDMASK;
	uint64_t boff=(id&OFFMASK)>>FDBITS;
	
	uint64_t idx=boff/(8*sizeof(int));
	int mask=1<<(boff%(8*sizeof(int)));
	int result=herd[fd].bitmap[idx]&mask;
	herd[fd].bitmap[idx]|=mask;
	if (!result) st_c_blks++;
	return result;
}

static inline int test(int id) {
	int fd=id&FDMASK;
	uint64_t boff=(id&OFFMASK)>>FDBITS;
	
	uint64_t idx=boff/(8*sizeof(int));
	int mask=1<<(boff%(8*sizeof(int)));
	int result=herd[fd].bitmap[idx]&mask;
	return result;
}

/*************************************************************
 * MASTER Functions
 */

static void master_cb(block_t id) {

        FILE *fp;

        
        /* printf("MASTER: everyone copied block %d\n", id); */
        //CHANGED
        int fd;
        off_t offset;
        uint64_t boff;

        fp=fopen("home/jtpaulo/holey/logs/cbmaster","a");
        fprintf(fp,"entrei callback master...\n");
        fclose(fp);

	pthread_mutex_lock(&mutex_cow);

	fd=id&FDMASK;
	offset=id&OFFMASK;
	boff=offset>>FDBITS;

        
        //TODO substituir com o holey_aio_write...
        //holey_aio_write(aio, herd[fd].storage, BLKSIZE, offset, herd[fd].cache[boff], cb, id, sector, private,boff,fd,1);
	pwrite(herd[fd].storage, herd[fd].cache[boff], BLKSIZE, offset);
        //cb(dd, (ret < 0) ? ret: 0, sector, nb_sectors, id, private);


        //TODO código a ser colado nos callbacks ver os locks....
        free(herd[fd].cache[boff]);
        herd[fd].cache[boff]=NULL;
        st_w_stab_blks++;

	pthread_mutex_unlock(&mutex_cow);

	/* printf("MASTER: storage persistent - block %d\n", id); */

        fp=fopen("home/jtpaulo/holey/logs/cbmaster","a");
        fprintf(fp,"sai callback master...\n");
        fclose(fp);

}

/*
static void run_after_write_cb(int fd,int boff){


    //TODO código a ser colado nos callbacks ver os locks....
    free(herd[fd].cache[boff]);
    herd[fd].cache[boff]=NULL;
    st_w_stab_blks++;


}*/

static int master_open(char* path, int flags) {

        int res;
        int fdaux;
        int mycow;
        uint64_t maxblk;      

	pthread_mutex_lock(&mutex_cow);
	

	// FIXME: should propagate permission flags
	fdaux = open(path, flags, 0644);
	if(fdaux < 0) {
		res = -1;
	} else {
		mycow=ncow++;

		herd[mycow].storage=fdaux;
		herd[mycow].storage_fname = strdup(path);

		maxblk=lseek(herd[mycow].storage, 0, SEEK_END)/BLKSIZE;
		lseek(herd[mycow].storage, 0, SEEK_SET);

		herd[mycow].bitmap=(int*)calloc(maxblk/8+sizeof(int),1);
		herd[mycow].cache=(void**)calloc(maxblk,sizeof(void*));

		res = mycow;
	}

	pthread_mutex_unlock(&mutex_cow);

	return res;
}

static int master_pwrite(int fd, void* data, size_t count, off_t offset, holey_aio_context_t* aio, td_callback_t cb, int id1, uint64_t sector, void* private) {

        int done;
        FILE* fp;


        fp=fopen("home/jtpaulo/holey/logs/writemaster","a");
        fprintf(fp,"write... offset %llu size %d\n",offset,count);
        fclose(fp);

	pthread_mutex_lock(&mutex_cow);

	done=0;

	while(count>0) {
		uint64_t cursor=offset&OFFMASK;
		int bcount=offset+count > cursor+BLKSIZE ?
						cursor+BLKSIZE-offset : count;

		block_t id=cursor|fd;
		uint64_t boff=cursor>>FDBITS;

                //temp
                //bcount=count;
                fp=fopen("home/jtpaulo/holey/logs/writemaster","a");
                fprintf(fp,"offset %llu count %d cursor %llu bcount %d boff %llu\n",offset,count,cursor,bcount,boff);
                fclose(fp);

		if (test_and_set(id)) {
                         fp=fopen("home/jtpaulo/holey/logs/writemaster","a");
                         fprintf(fp,"if...\n");
                         fclose(fp);
			 holey_aio_write(aio, herd[fd].storage, bcount, offset, (char*) data, cb, id1, sector, private,0,0,0);
                         //pwrite(herd[fd].storage, data, bcount, offset);

			st_w_reg_blks++;
		} else {
                        fp=fopen("home/jtpaulo/holey/logs/writemaster","a");
                        fprintf(fp,"else...\n");
                        fclose(fp);
			if (herd[fd].cache[boff]==NULL) {
				herd[fd].cache[boff]=malloc(BLKSIZE);
				if (bcount!=BLKSIZE) {
					st_frag_blks++;
                                        //deixei sincrono
					pread(herd[fd].storage, herd[fd].cache[boff], BLKSIZE, cursor);
				}
                                fp=fopen("home/jtpaulo/holey/logs/writemaster","a");
                                fprintf(fp,"antes add block\n");
                                fclose(fp);
				add_block(id);
                                fp=fopen("home/jtpaulo/holey/logs/writemaster","a");
                                fprintf(fp,"add block\n");
                                fclose(fp);
			}
			memcpy(herd[fd].cache[boff]+(offset-cursor), data, bcount);
		}

		offset+=bcount;
		count-=bcount;
		data+=bcount;
		done+=bcount;
	}

        fp=fopen("home/jtpaulo/holey/logs/writemaster","a");
        fprintf(fp,"antes libertar lock\n");
        fclose(fp);
	pthread_mutex_unlock(&mutex_cow);

        fp=fopen("home/jtpaulo/holey/logs/writemaster","a");
        fprintf(fp,"depois libertar lock\n");
        fclose(fp);

	return done;
}

static int master_pread(int fd, void* data, size_t count, off_t offset, holey_aio_context_t* aio, td_callback_t cb, int id1, uint64_t sector, void* private) {

        int done;
        FILE* fp;


        fp=fopen("home/jtpaulo/holey/logs/readmaster","a");
        fprintf(fp,"read... offset %llu size %d\n",offset,count);
        fclose(fp);
	pthread_mutex_lock(&mutex_cow);

        fp=fopen("home/jtpaulo/holey/logs/readmaster","a");
        fprintf(fp,"entrei lock offset %llu size %d\n",offset,count);
        fclose(fp);

	done =0;

	while(count>0) {
		uint64_t cursor=offset&OFFMASK;
		int bcount=offset+count > cursor+BLKSIZE ?
						cursor+BLKSIZE-offset : count;

		block_t id=cursor|fd;
		uint64_t boff=cursor>>FDBITS;

                fp=fopen("home/jtpaulo/holey/logs/readmaster","a");
                fprintf(fp,"offset %llu count %d cursor %llu bcount %d boff %llu\n",offset,count,cursor,bcount,boff);
                fclose(fp);

                //temp
                //bcount=count;

		if (herd[fd].cache[boff]!=NULL) {

                        fp=fopen("home/jtpaulo/holey/logs/readmaster","a");
                        fprintf(fp,"cache dif null %llu size %d\n",offset,count);
                        fclose(fp);
			st_r_stab_blks++;
                        //DUVIDA o que aqui faz é se cache nao ta vazia le o resultado daqui...
                        //TODO caso isto aconteça tenho de chamar o callback na mesma aqui porque isto nao vai pro aio...
			memcpy(data, herd[fd].cache[boff]+(offset-cursor), bcount);
		} else {
                        fp=fopen("home/jtpaulo/holey/logs/readmaster","a");
                        fprintf(fp,"cache nula %llu size %d\n",offset,count);
                        fclose(fp);
			st_r_reg_blks++;
                        holey_aio_read(aio, herd[fd].storage, bcount, offset, (char*) data, cb, id1, sector, private);
			//pread(herd[fd].storage, data, bcount, offset);
		}

                fp=fopen("home/jtpaulo/holey/logs/readmaster","a");
                fprintf(fp,"sai if %llu size %d\n",offset,count);
                fclose(fp);
		offset+=bcount;
		count-=bcount;
		data+=bcount;
		done+=bcount;
	}

        fp=fopen("home/jtpaulo/holey/logs/readmaster","a");
        fprintf(fp,"sai ciclo %llu size %d\n",offset,count);
        fclose(fp);
	pthread_mutex_unlock(&mutex_cow);

        fp=fopen("home/jtpaulo/holey/logs/readmaster","a");
        fprintf(fp,"unlock %llu size %d\n",offset,count);
        fclose(fp);

	return done;
}

static int master_fsync(int fd) {
	/* printf("MASTER: SYNCing...\n"); */

	wait_sync(0);

	pthread_mutex_lock(&mutex_cow);
	fsync(herd[fd].storage);
	pthread_mutex_unlock(&mutex_cow);

	/* printf("MASTER: SYNC DONE \n"); */

	return 0;
}

static void master_init(int nslaves, int sz) {
        
	int sfd,*fd,len,i,j,on=1;
	struct sockaddr_in master, slave;
        FILE* fp;

        fp=fopen("home/jtpaulo/holey/logs/log","a");
        fprintf(fp,"inicio \n");
        fclose(fp);              

        //TODO aqui é int ou uint64_t
	fd=(int*)calloc(nslaves, sizeof(int));

	sfd=socket(PF_INET, SOCK_STREAM, 0);

	if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
		perror("setsockopt(SO_REUSEADDR) failed");
	}

	memset(&master, 0, sizeof(master));
	master.sin_family = PF_INET;
	master.sin_port = htons(STAB_PORT);
        //master.sin_port = htons(12350);
	master.sin_addr.s_addr = htonl(INADDR_ANY);

        fp=fopen("home/jtpaulo/holey/logs/log","a");
        fprintf(fp,"antes bind \n");
        fclose(fp);
        
	while (bind(sfd, (struct sockaddr*)&master, sizeof(master))<0) {
                perror("port 12345");
                sleep(20);
		//exit(1);
	}

        fp=fopen("home/jtpaulo/holey/logs/log","a");
        fprintf(fp,"sai uma vez \n");
        fclose(fp);

    
	listen(sfd, SOMAXCONN);


        //sleep(60);

        fp=fopen("home/jtpaulo/holey/logs/log","a");
        fprintf(fp,"depois listen addddhhh \n");
        fclose(fp);

	for(i=0;i<nslaves;i++) {
                fp=fopen("home/jtpaulo/holey/logs/log","a");
                fprintf(fp,"dentro for \n");
                fclose(fp);

		len=0;
		memset(&slave, 0, sizeof(slave));

                fp=fopen("home/jtpaulo/holey/logs/log","a");
                fprintf(fp,"depois memset \n");
                fclose(fp);

		fd[i] = accept(sfd, (struct sockaddr*)&slave,(socklen_t*)&len);

                fp=fopen("home/jtpaulo/holey/logs/log","a");
                fprintf(fp,"depois accept \n");
                fclose(fp);
	}

        fp=fopen("home/jtpaulo/holey/logs/log","a");
        fprintf(fp,"antes do master_stab \n");
        fclose(fp);

	master_stab(fd, nslaves, sz, master_cb);
}

static off_t master_lseek(int fd, off_t offset, int whence) {
	off_t ret;

	pthread_mutex_lock(&mutex_cow);
	ret = lseek(herd[fd].storage, offset, whence);	
	pthread_mutex_unlock(&mutex_cow);

	return ret;
}

static int master_close(int fd) {
	pthread_mutex_lock(&mutex_cow);

	close(herd[fd].storage);
	free(herd[fd].storage_fname);

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


        fp=fopen("home/jtpaulo/holey/logs/cbslave","a");
        fprintf(fp,"entrei callback slave...\n");
        fclose(fp);

	pthread_mutex_lock(&mutex_cow);
	if (!test_and_set(id)) {

		char data[BLKSIZE];
		int fd=id&FDMASK;
 		off_t offset=id&OFFMASK;

                //TODO ver o que isto faz....
                //DUVIDA aqui como faço?? ponho I/O assincrono??
                //não é necessário saber quando isto acaba?? tou a pensar que pode dar problemas de concorrencia
                //se master depois escreve primeiro... aqui se for assincrono quando tiver o callback tenho de responder para
                //o master a dizer que ja tenho o bloco.... Isso não é feito neste código
		pread(herd[fd].storage, data, BLKSIZE, offset);
		pwrite(herd[fd].snapshot, data, BLKSIZE, offset);

		/* printf("SLAVE: copied block %d\n", id); */

		/* update stats */
		st_fetch_blks ++;

	} /*else {
		printf("SLAVE: block %d already copied\n", id);
	}*/
	pthread_mutex_unlock(&mutex_cow);

        fp=fopen("home/jtpaulo/holey/logs/cbslave","a");
        fprintf(fp,"sai callback slave...\n");
        fclose(fp);


}


static int slave_pwrite(int fd, void* data, size_t count, off_t offset, holey_aio_context_t* aio, td_callback_t cb, int id1, uint64_t sector, void* private) {
        int done;

        FILE* fp;


        fp=fopen("home/jtpaulo/holey/logs/writeslave","a");
        fprintf(fp,"write... offset %llu size %d\n",offset,count);
        fclose(fp);

	pthread_mutex_lock(&mutex_cow);

	done=0;

	while(count>0) {
		uint64_t cursor=offset&OFFMASK;
		int bcount=offset+count > cursor+BLKSIZE ?
						cursor+BLKSIZE-offset : count;
                
                //temp
                //int bcount=count;


		block_t id=cursor|fd;

               
               
		char tmp[BLKSIZE];
		void* buf=data;
		int ecount=bcount;
		uint64_t eoffset=offset;
 
                fp=fopen("home/jtpaulo/holey/logs/writeslave","a");
                fprintf(fp,"offset %llu count %d cursor %llu bcount %d\n",offset,count,cursor,bcount);
                fclose(fp);
     

		if (!test_and_set(id) && bcount!=BLKSIZE) {
			st_frag_blks++;
			pread(herd[fd].storage, tmp, BLKSIZE, cursor);
			memcpy(tmp+(offset-cursor), data, bcount);
			buf=tmp;
			ecount=BLKSIZE;
			eoffset=cursor;
		}

                holey_aio_write(aio, herd[fd].snapshot, ecount, eoffset, (char*) buf, cb, id1, sector, private,0,0,0);
		//pwrite(herd[fd].snapshot, buf, ecount, eoffset);


                fp=fopen("home/jtpaulo/holey/logs/writeslave","a");
                fprintf(fp,"depois holy write\n");
                fclose(fp);
		st_w_stab_blks++;

		offset+=bcount;
		count-=bcount;
		data+=bcount;
		done+=bcount;
	}

	pthread_mutex_unlock(&mutex_cow);

        fp=fopen("home/jtpaulo/holey/logs/writeslave","a");
        fprintf(fp,"depois libertar lock\n");
        fclose(fp);

	return done;
}

static int slave_pread(int fd, void* data, size_t count, off_t offset, holey_aio_context_t* aio, td_callback_t cb, int id1, uint64_t sector, void* private) {
        int done;
        FILE *fp;

        fp=fopen("home/jtpaulo/holey/logs/readslave","a");
        fprintf(fp,"li %llu size %d\n",offset,count);
        fclose(fp);
	pthread_mutex_lock(&mutex_cow);
        fp=fopen("home/jtpaulo/holey/logs/readslave","a");
        fprintf(fp,"depois lock %llu size %d\n",offset,count);
        fclose(fp);


	done=0;

	while(count>0) {
		uint64_t cursor=offset&OFFMASK;
		int bcount=offset+count > cursor+BLKSIZE ?
						cursor+BLKSIZE-offset : count;

                //temp
                //int bcount=count;

		block_t id=cursor|fd;
                int src;
                fp=fopen("home/jtpaulo/holey/logs/readslave","a");
                fprintf(fp,"offset %llu count %d cursor %llu bcount %d\n",offset,count,cursor,bcount);
                fclose(fp);
     

		
		if (test(id)) {
                        fp=fopen("home/jtpaulo/holey/logs/readslave","a");
                        fprintf(fp,"if test %llu size %d\n",offset,count);
                        fclose(fp);
			src=herd[fd].snapshot;
			st_r_stab_blks++;
		} else {
                        fp=fopen("home/jtpaulo/holey/logs/readslave","a");
                        fprintf(fp,"else %llu size %d\n",offset,count);
                        fclose(fp);
			src=herd[fd].storage;
			st_r_reg_blks++;
		}
		
                fp=fopen("home/jtpaulo/holey/logs/readslave","a");
                fprintf(fp,"antes agenda %llu size %d\n",offset,count);
                fclose(fp);
                holey_aio_read(aio, src, bcount, offset, (char*) data, cb, id1, sector, private);
                //DUVIDA aqui sector ta bem??
                //vai ser usado no callback mas não quer dizer que seja o correcto mas acho que é o que queremos responder à VM...	
		//pread(src, data, bcount, offset);

                fp=fopen("home/jtpaulo/holey/logs/readslave","a");
                fprintf(fp,"depois agenda %llu size %d\n",offset,count);
                fclose(fp);
		offset+=bcount;
		count-=bcount;
		data+=bcount;
		done+=bcount;
	}

        fp=fopen("home/jtpaulo/holey/logs/readslave","a");
        fprintf(fp,"antes unlock %llu size %d\n",offset,count);
        fclose(fp);
	pthread_mutex_unlock(&mutex_cow);


        fp=fopen("home/jtpaulo/holey/logs/readslave","a");
        fprintf(fp,"fim %llu size %d\n",offset,count);
        fclose(fp);
	return done;
}

static int slave_fsync(int fd) {
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
		int mycow=ncow++, i;
		off_t max_size, snap_size; 

		/* get the filename */
		fname = rindex(path, '/') == NULL ? path : (rindex(path, '/') +1);
		sprintf(name, "%s%s.%d.cow", cow_basedir, fname, slaveid);

		/* collect file descriptors */
		herd[mycow].storage=fdaux;
		herd[mycow].storage_fname = strdup(path);

		herd[mycow].snapshot=open(name, O_RDWR|O_CREAT, 0644);
		herd[mycow].snapshot_fname = strdup(name);

		/* get storage size */
		max_size = lseek(herd[mycow].storage, 0, SEEK_END);
		snap_size = lseek(herd[mycow].snapshot, 0, SEEK_END);

		/* grow snapshot to the size of the original file */
		if(snap_size != max_size) {
			lseek(herd[mycow].snapshot, 0, SEEK_SET);
			lseek(herd[mycow].snapshot, max_size-1, SEEK_END);
			read(herd[mycow].snapshot, &c, 1);
			write(herd[mycow].snapshot, &c, 1);

		}

		/* reset offset to begining of storage and snapshot */
		lseek(herd[mycow].snapshot, 0, SEEK_SET);
		lseek(herd[mycow].storage, 0, SEEK_SET);

		/* create the bitmap */
		herd[mycow].bitmap=(int*)calloc((max_size/BLKSIZE)/8+sizeof(int), 1);

		res = mycow;
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

static off_t slave_lseek(int fd, off_t offset, int whence) {
	off_t ret;

	pthread_mutex_lock(&mutex_cow);
	ret = lseek(herd[fd].snapshot, offset, whence);	
	pthread_mutex_unlock(&mutex_cow);

	return ret;

}

static int slave_close(int fd) {

	/* TODO: assuming that reopen is on the same order */
	pthread_mutex_lock(&mutex_cow);
	
	close(herd[fd].storage);
	close(herd[fd].snapshot);

	free(herd[fd].storage_fname);
	free(herd[fd].snapshot_fname);

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
	return ; 
}

static int orig_open(char* path, int flags) { 
	// FIXME: should propagate permission flags
	return open(path, flags, 0644);
}

static int orig_close(int fd) {
	return close(fd);
}

//TODO falta mudar aqui....
static int orig_pwrite(int fd, void* data, size_t count, off_t offset, holey_aio_context_t* aio, td_callback_t cb, int id1, uint64_t sector, void* private) {
	pthread_mutex_lock(&mutex_cow);
	st_w_reg_blks++;
	pthread_mutex_unlock(&mutex_cow);
        return holey_aio_write(aio, fd, count, offset, (char*) data, cb, id1, sector, private,0,0,0);
	//return pwrite(fd, data, count, offset);
}

static int orig_pread(int fd, void* data, size_t count, off_t offset, holey_aio_context_t* aio, td_callback_t cb, int id1, uint64_t sector, void* private) {
	pthread_mutex_lock(&mutex_cow);
	st_r_reg_blks++;
	pthread_mutex_unlock(&mutex_cow);
        return holey_aio_read(aio, fd, count, offset, (char*) data, cb, id1, sector, private);
	//return pread(fd, data, count, offset);
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

	printf("Holey init: %d - %s - %d - %s\n", profile, master_ip, n_slaves, cow_basedir);
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
int (*holey_pwrite)(int, void*, size_t, off_t, holey_aio_context_t*, td_callback_t, int, uint64_t, void*) = orig_pwrite;
int (*holey_pread)(int, void*, size_t, off_t, holey_aio_context_t*, td_callback_t, int, uint64_t, void*) = orig_pread;
int (*holey_fsync)(int) = orig_fsync;
off_t (*holey_lseek)(int, off_t, int) = orig_lseek;

