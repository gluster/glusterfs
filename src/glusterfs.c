#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>

int
main (int argc, char *argv[])
{
  /* command line options: 
     -o allow_other -o default_permissions -o direct_io
  */
  return glusterfs_fops_register (argc, argv);
}
