#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <netdb.h>

#include "logging.h"

int
main (int argc, char *argv[])
{
  /* command line options: 
     -o allow_other -o default_permissions -o direct_io
  */

  struct rlimit dump_core;
  dump_core.rlim_cur = RLIM_INFINITY;
  dump_core.rlim_max = RLIM_INFINITY;
  setrlimit (RLIMIT_CORE, &dump_core);

  gluster_log_init ("/tmp/glusterlog");
  gluster_log_set_loglevel (LOG_DEBUG);

  if (argc != 5) {
    fprintf (stderr, "Usage: %s <volume specfile> <mountpoint> -o <options>\n",
	     argv[0]);
    exit (1);
  }
  return glusterfs_mount (argv[1], argv[2], argv[4]);
}
