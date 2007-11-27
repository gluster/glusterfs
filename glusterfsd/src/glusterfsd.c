/*
   Copyright (c) 2006, 2007 Z RESEARCH, Inc. <http://www.zresearch.com>
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


//#include "glusterfsd.h"
#include "protocol.h"

#include <errno.h>
#include <sys/resource.h>
#include <stdint.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <malloc.h>

#ifdef HAVE_ARGP
#include <argp.h>
#endif

#ifdef HAVE_MALLOC_STATS
#ifdef DEBUG
#include <mcheck.h>
#endif
#endif

#include "transport.h"
#include "timer.h"
#include "xlator.h"
#include "compat.h"

#define DEFAULT_LOG_FILE DATADIR "/log/glusterfs/glusterfsd.log"

/* useful for argp for command line parsing */
static struct argp_option options[] = {
  {"pidfile", 'p', "PIDFILE", 0, "path for the pid file"},
  {"spec-file", 'f', "VOLUMESPEC-FILE", 0, "load the VOLUMESPEC-FILE." },
  {"log-level", 'L', "LOGLEVEL", 0, 
   "LOGLEVEL should be one of DEBUG, WARNING, [ERROR], CRITICAL, NONE"},
  {"log-file", 'l', "LOGFILE", 0, "specify the file to redirect logs"},
  {"no-daemon", 'N', 0, 0, "run glusterfsd in foreground"},
  {"version", 'V', 0, 0, "display version information"},
  { 0, }
};

const char *argp_program_version = PACKAGE_NAME " " PACKAGE_VERSION " built on " __DATE__;
const char *argp_program_bug_address = PACKAGE_BUGREPORT;
static char argp_doc[] = " ";
static char doc[] = "glusterfsd is the server component of GlusterFS";

static int parse_opts (int32_t key, char *arg, struct argp_state *_state);

static struct argp argp = { options, parse_opts, argp_doc, doc };

static char *specfile = CONFDIR "/glusterfs-server.vol";
static char *pidfile = NULL;
static xlator_t *xlator_tree_node = NULL;

static xlator_t *
get_xlator_graph (glusterfs_ctx_t *ctx, FILE *fp)
{
  xlator_t *xl = file_to_xlator_tree (ctx, fp);
  xlator_t *trav = xl;
  int32_t ret = -1;
  
  if (trav == NULL) {
    return NULL;
  }
  while (trav->prev)
    trav = trav->prev;

  while (trav) {
    if (!trav->ready)
      ret = xlator_tree_init (trav);
    trav = trav->next;
  }

  return (ret == 0) ? xl : NULL;
}

xlator_t *
gf_get_xlator_tree_node ()
{
  return xlator_tree_node;
}

static int32_t
glusterfsd_print_version (void)
{
  printf ("%s\n", argp_program_version);
  printf ("Copyright (c) 2006, 2007 Z RESEARCH Inc. <http://www.zresearch.com>\n");
  printf ("GlusterFS comes with ABSOLUTELY NO WARRANTY.\nYou may redistribute copies of GlusterFS under the terms of the GNU General Public License.\n");
  exit (0);
}

static int
parse_opts (int32_t key, char *arg, struct argp_state *_state)
{
  glusterfs_ctx_t *ctx = _state->input;

  switch (key){
  case 'f':
    specfile = strdup (arg);
    break;
  case 'p':
    pidfile = strdup (arg);
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
    } else {
      ctx->loglevel = GF_LOG_ERROR;
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
    glusterfsd_print_version ();
    break;
  case ARGP_KEY_NO_ARGS:
    //argp_usage (_state);
    break;
  }
  return 0;
}

static int32_t
pidfile_lock (char *pidfile)
{
  int fd;
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
		 "glusterfsd: FATAL: already running as PID %d!\n",
		 pid);
	exit (1);
      } else {
	fprintf (stderr,
		 "glusterfsd: WARNING: ignoring stale pidfile for PID %d\n",
		 pid);
      }
    }
    unlink (pidfile);
  }
  fd = open (pidfile, O_CREAT|O_EXCL|O_WRONLY|O_TRUNC, S_IWUSR);
  if (fd == -1) {
    fprintf (stderr,
	     "glusterfsd: FATAL: unable to create pidfile `%s' (%s)\n",
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

void 
glusterfsd_cleanup_and_exit (int signum)
{
  gf_log ("glusterfsd", GF_LOG_WARNING, "shutting down server");

  if (pidfile)
    unlink (pidfile);

  exit (0);
}

int32_t 
main (int32_t argc, char *argv[])
{
  FILE *fp = NULL;
  int32_t pidfd = 0;
  glusterfs_ctx_t ctx = {
    .logfile = DATADIR "/log/glusterfs/glusterfsd.log",
    .loglevel = GF_LOG_WARNING,
    .poll_type = SYS_POLL_TYPE_EPOLL,
  };
  call_pool_t *pool;

  pthread_mutex_init (&ctx.lock, NULL);
  pool = ctx.pool = calloc (1, sizeof  (*pool));
  LOCK_INIT (&pool->lock);
  INIT_LIST_HEAD (&pool->all_frames);

  argp_parse (&argp, argc, argv, 0, 0, &ctx);

  if (pidfile)
    pidfd = pidfile_lock (pidfile);

  if (gf_log_init (ctx.logfile) < 0){
    return 1;
  }
  gf_log_set_loglevel (ctx.loglevel);

  /*we want to dump the core and
    we also don't want to limit max number of open files on glusterfs */
  {
    struct rlimit lim;
    lim.rlim_cur = RLIM_INFINITY;
    lim.rlim_max = RLIM_INFINITY;
    
    if (setrlimit (RLIMIT_CORE, &lim) < 0) {
      gf_log ("glusterfsd",
	      GF_LOG_ERROR,
	      "main: failed to set RLIMIT_CORE, error string is %s",
	      strerror (errno));
    }
    
    lim.rlim_cur = 65535; //RLIM_INFINITY;
    lim.rlim_max = 65535; //RLIM_INFINITY;
    
    if (setrlimit (RLIMIT_NOFILE, &lim) < 0) {
      gf_log ("glusterfsd",
	      GF_LOG_ERROR,
	      "main: failed to set RLIMIT_NOFILE, error string is %s",
	      strerror (errno));
    }
  }

  if (specfile) {
    fp = fopen (specfile, "r");
    if (!fp) {
      gf_log ("glusterfsd",
	      GF_LOG_ERROR,
	      "FATAL: could not open specfile: '%s'",
	      specfile);
      exit (1);
    }
  } else {
    gf_log ("glusterfsd",
	    GF_LOG_DEBUG,
	    "main: specfile not provided as command line arg"); 
    argp_help (&argp, stderr, ARGP_HELP_USAGE, argv[0]);
    exit (0);
  }

  if (!ctx.foreground) {
    int i;
    for (i=0;i<argc;i++)
      memset (argv[i], ' ', strlen (argv[i]));
    sprintf (argv[0], "[glusterfsd]");
    daemon (0, 0);
    pidfile_update (pidfd);
  } 

  gf_timer_registry_init (&ctx);

  xlator_tree_node = get_xlator_graph (&ctx, fp);
  if (!xlator_tree_node) {
    gf_log ("glusterfsd",
	    GF_LOG_ERROR,
	    "FATAL: could not create node graph");
    exit (1);
  }
  fclose (fp);

  /* Ignore SIGPIPE */
  signal (SIGPIPE, SIG_IGN);

#if HAVE_BACKTRACE
  /* Handle SIGABORT and SIGSEGV */
  signal (SIGFPE, gf_print_trace);
  signal (SIGSEGV, gf_print_trace);
  signal (SIGABRT, gf_print_trace);
#endif /* HAVE_BACKTRACE */

  signal (SIGTERM, glusterfsd_cleanup_and_exit);

#ifdef HAVE_MALLOC_STATS
#ifdef DEBUG
  mtrace ();
#endif
  signal (SIGUSR1, (sighandler_t)malloc_stats);
#endif


  while (!poll_iteration (&ctx));

  if (pidfile)
    close (pidfd);
  return 0;
}
