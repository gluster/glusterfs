/*
   Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <netdb.h>
#include <libgen.h>

#include <stdint.h>
#include <signal.h>
#include <pthread.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif /* _CONFIG_H */

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#ifdef HAVE_MALLOC_STATS
#ifdef DEBUG
#include <mcheck.h>
#endif
#endif

#include "xlator.h"
#include "glusterfs.h"
#include "compat.h"
#include "logging.h"
#include "dict.h"
#include "protocol.h"
#include "timer.h"
#include "glusterfsd.h"
#include "stack.h"
#include "revision.h"
#include "common-utils.h"
#include "event.h"

/* using argp for command line parsing */

static char doc[] = "glusterfs is the executable of the GlusterFS filesystem";
static char argp_doc[] = "--server=SERVER MOUNT-POINT";
const char *argp_program_version = PACKAGE_NAME " " PACKAGE_VERSION " built on " __DATE__ " " __TIME__;
const char *argp_program_bug_address = PACKAGE_BUGREPORT;

static struct gf_spec_location spec;
uint32_t glusterfs_fuse_direct_io_mode = 1;
uint32_t glusterfs_fuse_entry_timeout = 1;
uint32_t glusterfs_fuse_attr_timeout = 1;
error_t parse_opts (int32_t key, char *arg, struct argp_state *_state);

static struct argp_option options[] = {
  {"server", 's', "SERVER", 0, \
   "SERVER to connect to get client specification. This is a mandatory option."},
  {"port", 'P', "PORT", 0, \
   "Connect to PORT on SERVER."},
  {"transport", 't', "TRANSPORT", 0, \
   "Transport type to get the spec from server."},
  {"pidfile", 'p', "PIDFILE", 0, \
   "path for the pidfile"},
  {"spec-file", 'f', "VOLUMESPEC-FILE", 0, \
   "Load a local VOLUMESPEC file. Mandatory if --server option is not passed." },
  {"log-level", 'L', "LOGLEVEL", 0, 
   "LOGLEVEL should be one of DEBUG, WARNING, [ERROR], CRITICAL, NONE"},
  {"log-file", 'l', "LOGFILE", 0, \
   "Specify the file to redirect logs"},
  {"no-daemon", 'N', 0, 0, \
   "Run glusterfs in foreground"},
  {"version", 'V', 0, 0, \
   "print version information"},
  {"volume-name", 'n', "VOLUME-NAME", 0, \
   "Volume name in client spec to use. Defaults to the topmost volume" },
  {"direct-io-mode", 'd', "ENABLE|DISABLE", 0,
   "Whether to force directIO on fuse fd. Defaults to ENABLE"},
  {"entry-timeout", 'e', "SECONDS", 0,
   "Entry timeout for dentries in the kernel. Defaults to 1 second"},
  {"attr-timeout", 'a', "SECONDS", 0,
   "Attribute timeout for inodes in the kernel. Defaults to 1 second"},
  {"run-id", 'r', "RUN-ID", 0,
   "Run ID for the process, used by scripts to keep track of process they started, defaults to none"},
  { 0, }
};
static struct argp argp = { options, parse_opts, argp_doc, doc };

extern FILE *
fetch_spec (glusterfs_ctx_t *ctx,
	    const char *remote_host,
	    const char *remote_port,
	    const char *transport);

static xlator_t *
fuse_graph (xlator_t *graph)
{
  int ret = 0;
  xlator_t *top = NULL;
  glusterfs_ctx_t *ctx = graph->ctx;
  xlator_list_t *xlchild;

  xlchild = calloc (1, sizeof (*xlchild));
  ERR_ABORT (xlchild);
  xlchild->xlator = graph;

  top = calloc (1, sizeof (*top));
  ERR_ABORT (top);
  top->children = xlchild;
  top->ctx = graph->ctx;
  top->next = graph;
  top->options = get_new_dict ();

  gf_log ("glusterfs", GF_LOG_DEBUG,
	  "setting option mount-point to %s", ctx->mount_point);

  dict_set (top->options, "mount-point",
	    data_from_static_ptr (ctx->mount_point));
  dict_set (top->options, "attr-timeout",
	    data_from_uint32 (glusterfs_fuse_attr_timeout));
  dict_set (top->options, "entry-timeout",
	    data_from_uint32 (glusterfs_fuse_entry_timeout));
#ifdef GF_DARWIN_HOST_OS 
  /* On Darwin machines, O_APPEND is not handled, which may corrupt the data */
  dict_set (top->options, "direct-io-mode", data_from_uint32 (0));
  gf_log ("glusterfs", GF_LOG_DEBUG, "Disabling 'direct-io-mode'");
#else 
  dict_set (top->options, "direct-io-mode",
	    data_from_uint32 (glusterfs_fuse_direct_io_mode));
  if (!glusterfs_fuse_direct_io_mode)
    gf_log ("glusterfs", GF_LOG_DEBUG, "Disabling 'direct-io-mode'");

#endif /* GF_DARWIN_HOST_OS */
  graph->parents = calloc (1, sizeof(xlator_list_t));
  graph->parents->xlator = top;

  ret = xlator_set_type (top, "mount/fuse");
  if (ret == -1) 
    {
      gf_log ("", GF_LOG_ERROR, "Failed to initialize 'fuse' translator");
      return NULL;
    }
  return top;
}


static FILE *
get_spec_fp (glusterfs_ctx_t *ctx)
{
  char *specfile = spec.spec.file;
  FILE *conf = NULL;

  if (spec.where == SPEC_LOCAL_FILE) {
    specfile = spec.spec.file;
    
    conf = fopen (specfile, "r");
    
    if (!conf) {
      perror (specfile);
      return NULL;
    }
    gf_log ("glusterfs", GF_LOG_DEBUG,
	    "loading spec from %s", specfile);
  } else if (spec.where == SPEC_REMOTE_FILE) {

    conf = fetch_spec (ctx,
		       spec.spec.server.ip,
		       spec.spec.server.port,
		       spec.spec.server.transport);
  }

  return conf;
}

static xlator_t *
xlator_graph_get (glusterfs_ctx_t *ctx,
		  FILE *conf)
{
  xlator_t *tree, *trav, *new_tree = NULL;

  tree = file_to_xlator_tree (ctx, conf);
  trav = tree;

  if (tree == NULL) {
    gf_log ("glusterfs", GF_LOG_ERROR,
	    "specification file parsing failed, exiting");
    return NULL;
  }

  /* if node != null, then we try to attach to the specified node */
  if (ctx->node_name) {
    while (trav) {
      if (0 == strcmp (trav->name, ctx->node_name)){
	new_tree = trav;
	break;
      }
      trav = trav->next;
    }

    if (!trav) {
      gf_log ("glusterfs", GF_LOG_ERROR,
	      "%s volume not found in xlator graph",
	      ctx->node_name);
      return NULL;
    }

    tree = trav;
  }

  return tree;
}


int32_t
xlator_graph_init (xlator_t *xl)
{
  xlator_t *trav = xl;
  int32_t ret = -1;

  while (trav->prev)
    trav = trav->prev;

  while (trav) {
    if (!trav->ready) {
      ret = xlator_tree_init (trav);
      if (ret < 0)
	break;
    }
    trav = trav->next;
  }

  return ret;
}


static int32_t
glusterfs_print_version (void)
{
  printf ("%s\n", argp_program_version);
  printf ("Repository revision: %s\n", GLUSTERFS_REPOSITORY_REVISION);
  printf ("Copyright (c) 2006, 2007, 2008 Z RESEARCH Inc. <http://www.zresearch.com>\n");
  printf ("GlusterFS comes with ABSOLUTELY NO WARRANTY.\nYou may redistribute copies of GlusterFS under the terms of the GNU General Public License.\n");
  exit (0);
}

error_t
parse_opts (int32_t key, char *arg, struct argp_state *_state)
{
  glusterfs_ctx_t *ctx = _state->input;

  switch (key) {
  case 'f':
    if (spec.where == SPEC_REMOTE_FILE) {
      fprintf (stderr, "glusterfs: -f|--spec-file option cannot be combined with -s|--server option\n");
      exit (EXIT_FAILURE);
    }
    ctx->specfile = strdup (arg);
    spec.where = SPEC_LOCAL_FILE;
    spec.spec.file = ctx->specfile;
    break;
  case 's':
    if (spec.where == SPEC_LOCAL_FILE) {
      fprintf (stderr, "glusterfs: -s|--server option cannot be combined with -f|--spec-file option\n");
      exit (EXIT_FAILURE);
    }
    ctx->serverip = strdup (arg);
    spec.where = SPEC_REMOTE_FILE;
    spec.spec.server.ip = ctx->serverip;
    break;
  case 't':
    spec.spec.server.transport = strdup (arg);
    break;
  case 'p':
    ctx->pidfile = strdup (arg);
    break;
  case 'P':
    spec.spec.server.port = strdup (arg);
    break;
  case 'L':
    /* set log level */
    if (!strncasecmp (arg, "DEBUG", strlen ("DEBUG"))) {
      ctx->loglevel = GF_LOG_DEBUG;
    } else if (!strncasecmp (arg, "WARNING", strlen ("WARNING"))) {
      ctx->loglevel = GF_LOG_WARNING;
    } else if (!strncasecmp (arg, "CRITICAL", strlen ("CRITICAL"))) {
      ctx->loglevel = GF_LOG_CRITICAL;
    } else if (!strncasecmp (arg, "NONE", strlen ("NONE"))) {
      ctx->loglevel = GF_LOG_NONE;
    } else if (!strncasecmp (arg, "ERROR", strlen ("ERROR"))) {
      ctx->loglevel = GF_LOG_ERROR;
    } else {
	fprintf (stderr, "glusterfs: Unrecognized log-level \"%s\", possible values are \"DEBUG|WARNING|[ERROR]|CRITICAL|NONE\"\n", arg);
	exit (EXIT_FAILURE);
    }
    break;
  case 'l':
    /* set log file */
    ctx->logfile = strdup (arg);
    break;
  case 'N':
    ctx->foreground = 1;
    break;
  case 'V':
    glusterfs_print_version ();
    break;
  case 'n':
    ctx->node_name = strdup (arg);
    break;
  case 'd':
    if ((!strcasecmp (arg, "disable"))) {
      //fprintf (stderr, "disabling direct-io mode for write operations in fuse client");
      glusterfs_fuse_direct_io_mode = 0;
    }
    break;
  case 'e':
    if (sscanf (arg, "%d", &glusterfs_fuse_entry_timeout) == 0) {
      fprintf (stderr, "glusterfs: %s not a valid number\n", arg);
      exit (1);
    }
    break;
  case 'a':
    if (sscanf (arg, "%d", &glusterfs_fuse_attr_timeout) == 0) {
      fprintf (stderr, "glusterfs: %s not a valid number\n", arg);
      exit (1);
    }
    break;
  case 'r':
    ctx->run_id = strdup (arg);
    ctx->pidfile = strdup (arg);
    break;
  case ARGP_KEY_NO_ARGS:
    break;
  case ARGP_KEY_ARG:
    {
      char *mpoint = NULL;
      if (arg[0] == '/') {
	mpoint = strdup (arg);
      } else {
	char *env = getenv ("PWD");
	asprintf (&mpoint, "%s/%s", env, arg);
      }
      ctx->mount_point = mpoint;
    }
    break;
  }
  return 0;
}


static int32_t
pidfile_lock (char *pidfile)
{
  int fd = 0;
  char pidstr[8] = {0, };
  struct stat stat;
  pid_t pid;

  fd = open (pidfile, O_RDONLY);

  if (fd != -1) {
    int32_t ret;
    char *err;
    ret = read (fd, pidstr, 8);
    close (fd);
    if (ret > 1) {
      char procstr[16];
      pidstr[ret-1] = 0;
      pid = strtoul (pidstr, &err, 0);
      sprintf (procstr, "/proc/%d", pid);
      if (lstat (procstr, &stat) == 0) {
	fprintf (stderr,
		 "glusterfs: FATAL: already running as PID %d!\n",
		 pid);
	exit (1);
      } else {
	fprintf (stderr,
		 "glusterfs: WARNING: ignoring stale pidfile for PID %d\n",
		 pid);
      }
    }
    unlink (pidfile);
  }
  fd = open (pidfile, O_CREAT|O_EXCL|O_WRONLY|O_TRUNC, S_IWUSR);
  if (fd == -1) {
    fprintf (stderr,
	     "glusterfs: FATAL: unable to create pidfile `%s' (%s)\n",
	     pidfile,
	     strerror (errno));
    exit (1);
  }

  sprintf (pidstr, "%d\n", getpid ());
  write (fd, pidstr, strlen (pidstr));

  return (fd);
}

static void
pidfile_update (int32_t fd)
{
  char pidstr[16];

  sprintf (pidstr, "%d\n", getpid ());
  lseek (fd, 0, SEEK_SET);
  ftruncate (fd, 0);
  write (fd, pidstr, strlen (pidstr));
  close (fd);
}

static char *pidfile;
void 
glusterfs_cleanup_and_exit (int signum)
{
  extern char *pidfile;
  glusterfs_ctx_t *ctx = get_global_ctx_ptr ();

  gf_log ("glusterfs", GF_LOG_WARNING, "shutting down server");

  gf_print_bytes();
  if (pidfile)
    unlink (pidfile);

  if (ctx->graph && ctx->mount_point)
    ((xlator_t *)ctx->graph)->fini (ctx->graph);

  exit (0);
}

int32_t 
main (int32_t argc, char *argv[])
{
  xlator_t *graph = NULL;
  FILE *specfp = NULL;
  struct rlimit lim;
  struct stat stbuf;
  call_pool_t *pool;
  int32_t ret = 0;
  int32_t pidfd = 0;
  glusterfs_ctx_t *ctx = calloc (1, sizeof(glusterfs_ctx_t));

  ERR_ABORT (ctx);
  ctx->loglevel = GF_LOG_WARNING;
  ctx->event_pool = event_pool_new (16384);

  lim.rlim_cur = RLIM_INFINITY;
  lim.rlim_max = RLIM_INFINITY;
  
  if (setrlimit (RLIMIT_CORE, &lim) == -1) {
    fprintf (stderr, "WARNING: Failed to set 'ulimit -c unlimited': %s\n",
	     strerror(errno));
  }

  asprintf (&(ctx->logfile), "%s/log/glusterfs/%s.log",
	    DATADIR, basename (argv[0]));

  argp_parse (&argp, argc, argv, 0, 0, ctx);

  pthread_mutex_init (&(ctx->lock), NULL);

  pool = ctx->pool = calloc (1, sizeof (call_pool_t));
  ERR_ABORT (ctx->pool);
  LOCK_INIT (&pool->lock);
  INIT_LIST_HEAD (&pool->all_frames);
  
  ret = stat (ctx->logfile, &stbuf);
  if (!((ret == 0) && S_ISREG (stbuf.st_mode)))
    {
      /* If its /dev/null, or /dev/stdout, /dev/stderr, let it use the same, no need to alter */
      /* Have seperate logfile per run */
      char tmp_logfile[1024];
      char timestr[256];
      time_t utime = time (NULL);
      struct tm *tm = localtime (&utime);
      strftime (timestr, 256, "%Y%m%d.%H%M%S", tm); 
      sprintf (tmp_logfile, "%s.%s.%d", ctx->logfile, timestr, getpid());

      /* Create symlink to actual log file */
      unlink (ctx->logfile);
      symlink (tmp_logfile, ctx->logfile);

      FREE (ctx->logfile);
      ctx->logfile = strdup (tmp_logfile);      
    }

  if (gf_log_init (ctx->logfile) == -1) {
    fprintf (stderr,
	     "glusterfs: failed to open logfile \"%s\"\n",
	     ctx->logfile);
    return -1;
  }

  gf_log_set_loglevel (ctx->loglevel);

  if (!spec.where) {
    fprintf (stderr, "glusterfs: missing option --server=SERVER or --spec-file=VOLUME-SPEC-FILE\n");
    return -1;
  }

  if (spec.spec.server.port) {
    if (spec.where != SPEC_REMOTE_FILE)
      {
      	fprintf (stderr, "glusterfs: -p|--port requires -s|--server option to be specified\n");
	exit (EXIT_FAILURE);
      }
  }

  specfp = get_spec_fp (ctx);
  if (!specfp) {
    fprintf (stderr, "glusterfs: could not open specfile\n");
    return -1;
  }
  
  set_global_ctx_ptr (ctx);

  /* This global is used in cleanup and exit */
  pidfile = ctx->pidfile;

#ifdef HAVE_MALLOC_STATS
#ifdef DEBUG
  mtrace ();
#endif
  signal (SIGUSR1, (sighandler_t)malloc_stats);
#endif
  /* This is used to dump details */
  signal (SIGUSR2, glusterfs_stats);

#if HAVE_BACKTRACE
  /* Handle SIGABORT and SIGSEGV */
  signal (SIGSEGV, gf_print_trace);
  signal (SIGABRT, gf_print_trace);
#endif /* HAVE_BACKTRACE */

  /* Ignore SIGPIPE */
  signal (SIGPIPE, SIG_IGN);

  signal (SIGHUP, gf_log_logrotate);
  signal (SIGTERM, glusterfs_cleanup_and_exit);

  /* Copy the command to be printed on bt */
  strcpy (ctx->cmd, argv[0]);
  
  /* This is required as after calling 'daemon()' the chroot will change */
  if (ctx->pidfile)
    pidfd = pidfile_lock (ctx->pidfile);

  if (!ctx->foreground) {
    /* customized ps output */

    /* ps output should be something like this:
     * "glusterfs -f specfile -l logfile -r runid mountpoint"
     * "glusterfsd -f specfile -l logfile -r runid"
     * 
     * nothing extra, nothing less
     */
    /*
    int i = 1;
    if (ctx->specfile)
      {
	memcpy (argv[i++], "-f", 2);
	memcpy (argv[i++], ctx->specfile, strlen (ctx->specfile));
      }
    
    if (ctx->run_id)
      {
	memcpy (argv[i++], "-r", 2);
	memcpy (argv[i++], ctx->run_id, strlen (ctx->run_id));
      }

    if (ctx->mount_point)
      {
	memcpy (argv[i++], ctx->mount_point, strlen (ctx->mount_point));
      }

    for (;i<argc;i++)
      memset (argv[i], ' ', strlen (argv[i]));
    */
    daemon (0, 0);
  }

  /* Because process is forked now, we need to update pid file */
  if (pidfd)
    pidfile_update (pidfd);

  gf_timer_registry_init (ctx);

  graph = xlator_graph_get (ctx, specfp);
  if (!graph) {
    gf_log ("glusterfs", GF_LOG_ERROR, "Unable to get xlator graph");
    return -1;
  }
  fclose (specfp);

  if (ctx->mount_point) 
  {
      graph = fuse_graph (graph);
      /* Initialize fuse first */
      if (!(graph && (graph->init (graph) == 0)))
      {
	  gf_log ("glusterfs", GF_LOG_ERROR, "Fuse Translator initialization failed. Exiting");
	  return -1;
      }
      graph->ready = 1; /* Initialization Done */
  }

  ctx->graph = graph;

  /* Log the details about the setup in logfile. */
  raise (SIGUSR2);

  if (xlator_graph_init (graph) == -1) 
    {
      gf_log ("glusterfs", GF_LOG_ERROR, "Error while initializing translators. Exiting");
      if (ctx->mount_point) 
      {
        graph->fini (graph);
      }
      return -1;
  }
  
  event_dispatch (ctx->event_pool);

  return 0;
}
