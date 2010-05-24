#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slave_master_def.h"

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


  if (connect (sock, (struct sockaddr *) &name, size) < 0)
    {
      perror ("bind");
      exit (EXIT_FAILURE);
    }

  return sock;
}


int main(int argc, char* argv[]){

     if (argc!=5) {
		fprintf(stderr, "usage: master id ncopiers stderr_file log_file\n");
		exit(1);
     }

     char path[40];

     mkdir(VARPATH,S_IRWXU);

     strcpy(path,VARPATH);
     strcat(path,argv[1]);

     int newsockfd = make_named_socket (path);


     int master=1;
     send(newsockfd,&master,sizeof(int),0);
 
     struct master_sts ms;
     ms.ncopiers=atoi(argv[2]);
     strcpy(ms.stderr,argv[3]);
     strcpy(ms.log,argv[4]);

     send(newsockfd,&ms,sizeof(struct master_sts),0);




 return 0;

}


