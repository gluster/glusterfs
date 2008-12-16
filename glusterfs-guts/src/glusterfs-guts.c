/*
   Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <argp.h>
#include <string.h>

#include "glusterfs.h"
#include "xlator.h"
#include "glusterfs-guts.h"

/* argp initializations */
static char doc[] = "glusterfs-guts is unit testing suite for glusterfs";
static char argp_doc[] = "";
const char *argp_program_version = PACKAGE_NAME " " PACKAGE_VERSION " built on " __DATE__;
const char *argp_program_bug_address = PACKAGE_BUGREPORT;

guts_ctx_t guts_ctx;
error_t parse_opts (int32_t key, char *arg, struct argp_state *_state);

static struct argp_option options[] = {
  {"spec-file", 'f', "VOLUMESPEC-FILE", 0,\
   "Load VOLUMESPEC-FILE."},
  {"threads", 't', "NUMBER", 0,\
   "Load NUMBER of threads."},
  {"tio-file", 'i', "FILE", 0,\
   "Replay fops from FILE."},
  {"tio-directory", 'I', "DIRECTORY", 0,\
   "Replay fops from files in DIRECTORY. Valid option only when using more than one thread."},
  {"log-level", 'L', "LOGLEVEL", 0, 
   "LOGLEVEL should be one of DEBUG, WARNING, [ERROR], CRITICAL, NONE"},
  {"log-file", 'l', "LOGFILE", 0, \
   "Specify the file to redirect logs"},
  {"trace", 'T', "MOUNTPOINT", 0, \
   "Run guts in trace mode. Guts mounts glusterfs on MOUNTPOINT specified"},
  {"output", 'o', "OUTPUT-TIOFILE", 0, \
   "Write trace io output to OUTPUT-TIOFILE. Valid only when run in trace(-T) mode."},
  {"version", 'V', 0, 0,\
   "print version information"},
  { 0, }
};

static struct argp argp = { options, parse_opts, argp_doc, doc };

/* guts_print_version - used by argument parser routine to print version information for guts */
static int32_t
guts_print_version (void)
{
  printf ("%s\n", argp_program_version);
  printf ("Copyright (c) 2006, 2007 Z RESEARCH Inc. <http://www.zresearch.com>\n");
  printf ("GlusterFS comes with ABSOLUTELY NO WARRANTY.\nYou may redistribute copies of GlusterFS under the terms of the GNU General Public License.\n");
  exit (0);
}

/* parse_opts - argument parsing helper routine for argp library */
error_t
parse_opts (int32_t key, char *arg, struct argp_state *_state)
{
  guts_ctx_t *state = _state->input;

  switch (key) {
  case 'f':
    if (!state->specfile) {
      state->specfile = strdup (arg);
    }
    break;

  case 't':
    if (!state->threads) {
      state->threads = strtol (arg, NULL, 0);
    }
    break;

  case 'i':
    if (state->threads == 1) {
      state->file = strdup (arg);
    } else {
      fprintf (stderr, "glusterfs-guts: -i option is valid only when guts is running single thread\n");
      exit (1);
    }
    break;

  case 'I':
    if (state->threads > 1) {
      state->directory = strdup (arg);
    } else {
      fprintf (stderr, "glusterfs-guts: -I option is valid only when guts is running multiple threads\n");
      exit (1);
    }
    break;

  case 'L':
    /* set log level */
    if (!strncasecmp (arg, "DEBUG", strlen ("DEBUG"))) {
      state->loglevel = GF_LOG_DEBUG;
    } else if (!strncasecmp (arg, "WARNING", strlen ("WARNING"))) {
      state->loglevel = GF_LOG_WARNING;
    } else if (!strncasecmp (arg, "CRITICAL", strlen ("CRITICAL"))) {
      state->loglevel = GF_LOG_CRITICAL;
    } else if (!strncasecmp (arg, "NONE", strlen ("NONE"))) {
      state->loglevel = GF_LOG_NONE;
    } else if (!strncasecmp (arg, "ERROR", strlen ("ERROR"))) {
      state->loglevel = GF_LOG_ERROR;
    } else {
	fprintf (stderr, "glusterfs-guts: Unrecognized log-level \"%s\", possible values are \"DEBUG|WARNING|[ERROR]|CRITICAL|NONE\"\n", arg);
	exit (EXIT_FAILURE);
    }
    break;
  case 'l':
    /* set log file */
    state->logfile = strdup (arg);
    break;

  case 'T':
    state->trace = 1;
    state->mountpoint = strdup (arg);
    break;

  case 'o':
    state->file = strdup (arg);
    break;

  case 'V':
    guts_print_version ();
    break;

  }
  return 0;
}

/* get_xlator_graph - creates a translator graph and returns the pointer to the root of the xlator tree
 * 
 * @ctx: guts context structure
 * @conf: file handle to volume specfile
 *
 * returns pointer to the root of the translator tree
 */
static xlator_t *
get_xlator_graph (glusterfs_ctx_t *ctx,
		  FILE *conf)
{
  xlator_t *tree, *trav = NULL;

  tree = file_to_xlator_tree (ctx, conf);
  trav = tree;

  if (tree == NULL) {
    gf_log ("glusterfs-guts",
	    GF_LOG_ERROR,
	    "specification file parsing failed, exiting");
    return NULL;
  }

  tree = trav;

  return tree;
}

/* get_spec_fp - get file handle to volume spec file specified. 
 * 
 * @ctx: guts context structure
 *
 * returns FILE pointer to the volume spec file.
 */
static FILE *
get_spec_fp (guts_ctx_t *ctx)
{
  char *specfile = ctx->specfile;
  FILE *conf = NULL;

  specfile = ctx->specfile;
  
  conf = fopen (specfile, "r");
  
  if (!conf) {
    perror (specfile);
    return NULL;
  }
  gf_log ("glusterfs-guts",
	  GF_LOG_DEBUG,
	  "loading spec from %s",
	  specfile);

  return conf;
}

static void *
guts_thread_main (void *ctx)
{
  guts_thread_ctx_t *tctx = (guts_thread_ctx_t *) ctx;
  
  printf ("starting thread main with %s:\n", tctx->file);
  guts_replay (tctx);
  printf ("ending thread main.\n");
  
  return NULL;
}

/* guts_create_threads - creates different threads based on thread number specified in ctx and assigns a
 *                       tio file to each thread and attaches each thread to the graph created by main().
 * @ctx: guts_ctx_t which contains the context corresponding to the current run of guts
 *
 * returns the guts_threads_t structure which contains handles to the different threads created.
 *
 */
static guts_threads_t *
guts_create_threads (guts_ctx_t *ctx)
{
  guts_threads_t *threads = NULL;
  int32_t thread_count = ctx->threads;

  threads = CALLOC (1, sizeof (*threads));
  ERR_ABORT (threads);
  

  INIT_LIST_HEAD (&(threads->threads));
  
  if (thread_count == 1) {
    /* special case: we have only one thread and we are given a tio-file as argument instead of a directory.
     * handling differently */
    guts_thread_ctx_t *thread = NULL;
    thread = CALLOC (1, sizeof (*thread));
    ERR_ABORT (thread);
    list_add (&thread->threads, &threads->threads);
    thread->file = strdup (ctx->file);
    thread->ctx = ctx;
  } else {
    /* look for .tio files in the directory given and assign to each of the threads */
    DIR *dir = opendir (ctx->directory);
        
    if (!dir) {
      gf_log ("guts",
	      GF_LOG_ERROR,
	      "failed to open directory %s", ctx->directory);
    } else {
      guts_thread_ctx_t *thread = NULL;
      struct dirent *dirp = NULL;
      /* to pass through "." and ".." */
      readdir (dir);
      readdir (dir);
      
      while (thread_count > 0) {
	char pathname[256] = {0,};

	thread = CALLOC (1, sizeof (*thread));
	ERR_ABORT (thread);
	dirp = NULL;
	
	list_add (&thread->threads, &threads->threads);
	dirp = readdir (dir);
	if (dirp) {
	  sprintf (pathname, "%s/%s", ctx->directory, dirp->d_name);
	  printf ("file name for thread(%d) is %s\n", thread_count, pathname);
	  thread->file = strdup (pathname);
	  thread->ctx = ctx;
	} else if (thread_count > 0) {
	  gf_log ("guts",
		  GF_LOG_ERROR,
		  "number of tio files less than %d, number of threads specified", ctx->threads);
	  /* TODO: cleanup */
	  return NULL;
	}
	--thread_count;
      }
    }
  }
  return threads;
}

/* guts_start_threads - starts all the threads in @threads.
 *
 * @threads: guts_threads_t structure containing the handles to threads created by guts_create_threads.
 *
 * returns <0 on error.
 *
 */
static void
guts_start_threads (guts_threads_t *gthreads)
{
  guts_thread_ctx_t *thread = NULL;
  list_for_each_entry (thread, &gthreads->threads, threads) {
    if (pthread_create (&thread->pthread, NULL, guts_thread_main, (void *)thread) < 0) {
      gf_log ("guts",
	      GF_LOG_ERROR,
	      "failed to start thread");
    } else {
      gf_log ("guts",
	      GF_LOG_DEBUG,
	      "started thread with file %s", thread->file);
    }
  }
}

static int32_t
guts_join_threads (guts_threads_t *gthreads)
{
  guts_thread_ctx_t *thread = NULL;
  list_for_each_entry (thread, &gthreads->threads, threads) {
    if (pthread_join (thread->pthread, NULL) < 0) {
      gf_log ("guts",
	      GF_LOG_ERROR,
	      "failed to join thread");
    } else {
      gf_log ("guts",
	      GF_LOG_DEBUG,
	      "joined thread with file %s", thread->file);
    }
  }
  return 0;
}


int32_t 
main (int32_t argc, char *argv[])
{
  /* glusterfs_ctx_t is required to be passed to 
   * 1. get_xlator_graph
   * 2. glusterfs_mount
   */
  glusterfs_ctx_t gfs_ctx = {
    .logfile = DATADIR "/log/glusterfs/glusterfs-guts.log",
    .loglevel = GF_LOG_DEBUG,
    .poll_type = SYS_POLL_TYPE_EPOLL,
  };
  
  guts_ctx_t guts_ctx = {0,};
  FILE *specfp = NULL;
  xlator_t *graph = NULL;
  guts_threads_t *threads = NULL;

  argp_parse (&argp, argc, argv, 0, 0, &guts_ctx);
  
  if (gf_log_init (gfs_ctx.logfile) == -1 ) {
    fprintf (stderr,
	     "glusterfs-guts: failed to open logfile \"%s\"\n",
	     gfs_ctx.logfile);
    return -1;
  }
  gf_log_set_loglevel (gfs_ctx.loglevel);

  specfp = get_spec_fp (&guts_ctx);
  if (!specfp) {
    fprintf (stderr,
	     "glusterfs-guts: could not open specfile\n");
    return -1;
  }
  
  graph = get_xlator_graph (&gfs_ctx, specfp);
  if (!graph) {
    gf_log ("guts", GF_LOG_ERROR,
	    "Unable to get xlator graph");
    return -1;
  }
  fclose (specfp);

  guts_ctx.graph = graph;
  
  if (guts_ctx.trace) {
    return guts_trace (&guts_ctx);
  } else {
    /* now that we have the xlator graph, we need to create as many threads as requested and assign a tio file
     * to each of the threads and tell each thread to attach to the graph we just created. */

    if (!guts_ctx.file && !guts_ctx.directory) {
      fprintf (stderr,
	       "glusterfs-guts: no tio file specified");
      return -1;
    }

    threads = guts_create_threads (&guts_ctx);
    
    if (threads) {
      guts_start_threads (threads);
      guts_join_threads (threads);
    } else {
      gf_log ("guts", GF_LOG_ERROR,
	      "unable to create threads");
      return 0;
    }
  }
  
  return 0;
}

