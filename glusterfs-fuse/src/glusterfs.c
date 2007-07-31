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
#include <stdint.h>
#include <signal.h>
#include <pthread.h>
#include <malloc.h>


#include "xlator.h"
#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "protocol.h"
#include "timer.h"
#include "glusterfs-fuse.h"
#include "stack.h"

extern char glusterfs_direct_io_mode;

/* using argp for command line parsing */
static char *mount_point = NULL;

static char doc[] = "glusterfs is client component of GlusterFS filesystem";
static char argp_doc[] = "--server=SERVER MOUNT-POINT";
const char *argp_program_version = PACKAGE_NAME " " PACKAGE_VERSION;
const char *argp_program_bug_address = PACKAGE_BUGREPORT;

/* looks ugly, but is very neat */
static struct gf_spec_location spec;
error_t parse_opts (int32_t key, char *arg, struct argp_state *_state);

static struct argp_option options[] = {
  {"server", 's', "SERVER", 0, \
   "SERVER to connect to get client specification. This is a mandatory option."},
  {"transport", 't', "TRANSPORT", 0, \
   "Transport type to get the spec from server"},
  {"port", 'p', "PORT", 0, \
   "Connect to PORT on SERVER"},
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
  {"direct-io-mode", 'd', "DIRECT-IO-MODE", 0,
   "File mode to force directIO on fuse fd. Defaults to none"},
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
  xlator_t *top = calloc (1, sizeof (*top));
  xlator_list_t *xlchild;

  xlchild = calloc (1, sizeof(*xlchild));
  xlchild->xlator = graph;
  top->children = xlchild;
  top->ctx = graph->ctx;
  top->next = graph;
  graph->parent = top;

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
    gf_log ("glusterfs",
	    GF_LOG_DEBUG,
	    "loading spec from %s",
	    specfile);
  } else if (spec.where == SPEC_REMOTE_FILE){

    conf = fetch_spec (ctx,
		       spec.spec.server.ip,
		       spec.spec.server.port,
		       spec.spec.server.transport);
  }

  return conf;
}

static xlator_t *
get_xlator_graph (glusterfs_ctx_t *ctx,
		  FILE *conf)
{
  xlator_t *tree, *trav, *new_tree = NULL;

  tree = file_to_xlator_tree (ctx, conf);
  trav = tree;

  if (tree == NULL) {
    gf_log ("glusterfs",
	    GF_LOG_ERROR,
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
      gf_log ("glusterfs-fuse",
	      GF_LOG_ERROR,
	      "%s volume not found in xlator graph",
	      ctx->node_name);
      return NULL;
    }

    tree = trav;
  }

  return tree;
}


static int32_t
glusterfs_print_version (void)
{
  printf ("%s\n", argp_program_version);
  printf ("Copyright (c) 2006, 2007 Z RESEARCH Inc. <http://www.zresearch.com>\n");
  printf ("GlusterFS comes with ABSOLUTELY NO WARRANTY.\nYou may redistribute copies of GlusterFS under the terms of the GNU General Public License.\n");
  exit (0);
}

char *gf_node_name = NULL;
error_t
parse_opts (int32_t key, char *arg, struct argp_state *_state)
{
  glusterfs_ctx_t *ctx = _state->input;

  switch (key){
  case 'f':
    if (spec.where == SPEC_REMOTE_FILE) {
      fprintf (stderr, "glusterfs: -f|--spec-file option cannot be combined with -s|--server option\n");
      exit (EXIT_FAILURE);
    }
    spec.where = SPEC_LOCAL_FILE;
    spec.spec.file = strdup (arg);
    break;
  case 's':
    if (spec.where == SPEC_LOCAL_FILE) {
      fprintf (stderr, "glusterfs: -s|--server option cannot be combined with -f|--spec-file option\n");
      exit (EXIT_FAILURE);
    }
    spec.where = SPEC_REMOTE_FILE;
    spec.spec.server.ip = strdup (arg);
    break;
  case 't':
    spec.spec.server.transport = strdup (arg);
    break;
  case 'p':
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
    ctx->logfile = arg;
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
    if ((!strcasecmp (arg, "rdonly")) ||
	(!strcasecmp (arg, "o_rdonly")) ||
	(!strcasecmp (arg, "readonly")) ||
	(!strcasecmp (arg, "read-only"))) {
      glusterfs_direct_io_mode = O_RDONLY;
    } else if ((!strcasecmp (arg, "wronly")) ||
	       (!strcasecmp (arg, "o_wronly")) ||
	       (!strcasecmp (arg, "writeonly")) ||
	       (!strcasecmp (arg, "write-only"))) {
      glusterfs_direct_io_mode = O_WRONLY;
    } else if ((!strcasecmp (arg, "rdwr")) ||
	       (!strcasecmp (arg, "o_rdwr")) ||
	       (!strcasecmp (arg, "readwrite")) ||
	       (!strcasecmp (arg, "read-write"))) {
      glusterfs_direct_io_mode = O_RDWR;
    } else {
      fprintf (stderr, "glusterfs: Unrecognized mode \"%s\", possible values are \"READONLY|WRITEONLY|READWRITE\"",arg);
      exit (EXIT_FAILURE);
    }
  case ARGP_KEY_NO_ARGS:
    break;
  case ARGP_KEY_ARG:
    mount_point = arg;
    break;
  }
  return 0;
}
  
int32_t 
main (int32_t argc, char *argv[])
{
  xlator_t *graph = NULL;
  FILE *specfp = NULL;
  transport_t *mp = NULL;
  glusterfs_ctx_t ctx = {
    .logfile = DATADIR "/log/glusterfs/glusterfs.log",
    .loglevel = GF_LOG_ERROR,
    .poll_type = SYS_POLL_TYPE_MAX,
  };
  struct rlimit lim;
  call_pool_t *pool;


#ifdef HAVE_MALLOC_STATS
#ifdef DEBUG
  mtrace ();
#endif
  signal (SIGUSR1, (sighandler_t)malloc_stats);
#endif

  lim.rlim_cur = RLIM_INFINITY;
  lim.rlim_max = RLIM_INFINITY;
  setrlimit (RLIMIT_CORE, &lim);
  setrlimit (RLIMIT_NOFILE, &lim);

  pool = ctx.pool = calloc (1, sizeof (call_pool_t));
  LOCK_INIT (&pool->lock);
  INIT_LIST_HEAD (&pool->all_frames);
  
  argp_parse (&argp, argc, argv, 0, 0, &ctx);

  if (gf_log_init (ctx.logfile) == -1) {
    fprintf (stderr,
	     "glusterfs: failed to open logfile \"%s\"\n",
	     ctx.logfile);
    return -1;
  }
  gf_log_set_loglevel (ctx.loglevel);

  if (!mount_point) {
    fprintf (stderr, "glusterfs: MOUNT-POINT not specified\n");
    return -1;
  }

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


  specfp = get_spec_fp (&ctx);
  if (!specfp) {
    fprintf (stderr,
	     "glusterfs: could not open specfile\n");
    return -1;
  }

  /* Ignore SIGPIPE */
  signal (SIGPIPE, SIG_IGN);

#if HAVE_BACKTRACE
  /* Handle SIGABORT and SIGSEGV */
  signal (SIGSEGV, gf_print_trace);
  signal (SIGABRT, gf_print_trace);
#endif /* HAVE_BACKTRACE */

  /* glusterfs_mount has to be ideally placed after all the initialisation stuff */
  if (!(mp = glusterfs_mount (&ctx, mount_point))) {
    gf_log ("glusterfs", GF_LOG_ERROR, "Unable to mount glusterfs");
    return 1;
  }

  if (!ctx.foreground) {
    /* funky ps output */
    int i;
    for (i=0;i<argc;i++)
      memset (argv[i], ' ', strlen (argv[i]));
    sprintf (argv[0], "[glusterfs]");
    daemon (0, 0);
  }

  gf_timer_registry_init (&ctx);

  graph = get_xlator_graph (&ctx, specfp);
  if (!graph) {
    gf_log ("glusterfs",
	    GF_LOG_ERROR,
	    "Unable to get xlator graph");
    return -1;
  }
  fclose (specfp);

  ctx.graph = graph;

  mp->xl = fuse_graph (graph);

  //  fuse_thread (&thread, mp);

  while (!poll_iteration (&ctx));

  return 0;
}
