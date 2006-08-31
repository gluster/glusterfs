#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <netdb.h>
#include <argp.h>

#include "glusterfs-fops.h"
#include "logging.h"

/* using argp for command line parsing */
static char *configfile = NULL;
static char *mt_options = NULL;
static struct mt_options mt_head;
static char doc[] = "glusterfs is a glusterfs client";
static char argp_doc[] = "MOUNT-POINT";

error_t parse_opts (int key, char *arg, struct argp_state *_state);

static struct {
  char *mt_point;
} cmdline;

struct {
  char *f[2];
} f;

static struct argp_option options[] = {
  {"options", 'o', "OPTIONS", 0, "Filesystem mount options" },
  {"config", 'c', "VOLUMESPEC", 0, "Load volume spec file VOLUMESPEC" },
  { 0, }
};
static struct argp argp = { options, parse_opts, argp_doc, doc };

error_t
parse_opts (int key, char *arg, struct argp_state *_state)
{
  switch (key){
  case 'c':
    configfile = arg;
    break;
  case 'o':
    mt_options = arg;
    /*do {
      struct mt_options *new = calloc (sizeof (struct mt_options), 1);
      new->mt_options = arg;
      new->next = mt_head.next;
      mt_head.next = new;
      mt_head.nopts += 1;
      printf ("recieved mount option: %s\n", arg);
      }while (0); */ //FIXME
    break;
  case ARGP_KEY_NO_ARGS:
    argp_usage (_state);
  case ARGP_KEY_ARG:
    cmdline.mt_point = arg;
    break;
  }
  return 0;
}
  
void 
args_init (int argc, char **argv)
{
  argp_parse (&argp, argc, argv, 0, 0, &f);
}


int
main (int argc, char *argv[])
{
  /* command line options: 
     -o allow_other -o default_permissions -o direct_io
  */

  struct rlimit lim;
  lim.rlim_cur = RLIM_INFINITY;
  lim.rlim_max = RLIM_INFINITY;
  setrlimit (RLIMIT_CORE, &lim);
  setrlimit (RLIMIT_NOFILE, &lim);

  gf_log_init ("/tmp/glusterlog");
  gf_log_set_loglevel (LOG_DEBUG);

  args_init (argc, argv);

  if (configfile && mt_options && cmdline.mt_point){
    printf ("mount point is %s\n", cmdline.mt_point);
    return glusterfs_mount (configfile, cmdline.mt_point, &mt_head);
  } else{
    argp_help (&argp, stderr,ARGP_HELP_USAGE , argv[0]);
    return 1;
  }
}
