/*
  (C) 2006, 2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 


//#include "glusterfsd.h"
#include "protocol.h"

#include <errno.h>
#include <sys/resource.h>
#include <argp.h>
#include <stdint.h>
#include <signal.h>

#include "sdp_inet.h"
#include "transport.h"

#define GF_YES 1
#define GF_NO 0

#define DEFAULT_LOG_FILE DATADIR "/log/glusterfs/glusterfsd.log"

static struct {
  char *f[2];
} f;

/* useful for argp for command line parsing */
static struct argp_option options[] = {
  {"spec-file", 'f', "VOLUMESPEC-FILE", 0, "load the VOLUMESPEC-FILE" },
  {"log-level", 'L', "LOGLEVEL", 0, 
   "LOGLEVEL should be one of DEBUG, WARNING, [ERROR], CRITICAL, NONE"},
  {"log-file", 'l', "LOGFILE", 0, "specify the file to redirect logs"},
  {"no-daemon", 'N', 0, 0, "run glusterfsd in foreground"},
  {"version", 'V', 0, 0, "display version information"},
  { 0, }
};

const char *argp_program_version = PACKAGE_NAME " " PACKAGE_VERSION;
const char *argp_program_bug_address = PACKAGE_BUGREPORT;
static char argp_doc[] = " ";
static char doc[] = "glusterfsd is glusterfs server";

static error_t parse_opts (int32_t key, char *arg, struct argp_state *_state);

static struct argp argp = { options, parse_opts, argp_doc, doc };

static int32_t gf_cmd_def_daemon_mode = GF_YES;

int32_t glusterfsd_stats_nr_clients = 0;
static char *specfile = CONFDIR "/glusterfs-server.vol";
static xlator_t *xlator_tree_node = NULL;
static int32_t cmd_def_log_level = GF_LOG_ERROR;
static char *cmd_def_log_file = DEFAULT_LOG_FILE;

static xlator_t *
get_xlator_graph (FILE *fp)
{
  xlator_t *xl = file_to_xlator_tree (fp);
  xlator_t *trav = xl;

  while (trav) {
    if (trav->init)
      trav->init (trav);
    trav = trav->next;
  }
  return xl;
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

static error_t
parse_opts (int32_t key, char *arg, struct argp_state *_state)
{
  switch (key){
  case 'f':
    specfile = strdup (arg);
    break;
  case 'L':
    /* set log level */
    if (!strncasecmp (arg, "DEBUG", strlen ("DEBUG"))) {
	cmd_def_log_level = GF_LOG_DEBUG;
      } else if (!strncasecmp (arg, "WARNING", strlen ("WARNING"))) {
	cmd_def_log_level = GF_LOG_WARNING;
      } else if (!strncasecmp (arg, "CRITICAL", strlen ("CRITICAL"))) {
	cmd_def_log_level = GF_LOG_CRITICAL;
      } else if (!strncasecmp (arg, "NONE", strlen ("NONE"))) {
	cmd_def_log_level = GF_LOG_NONE;
      } else {
	cmd_def_log_level = GF_LOG_ERROR;
      }
    break;
  case 'l':
    /* set log file */
    cmd_def_log_file = strdup (arg);
    printf ("Using logfile %s\n", cmd_def_log_file);
    break;
  case 'N':
    gf_cmd_def_daemon_mode = GF_NO;
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

static void
args_init (int32_t argc, char **argv)
{
  argp_parse (&argp, argc, argv, 0, 0, &f);
}


int32_t 
main (int32_t argc, char *argv[])
{
  FILE *fp;


  args_init (argc, argv);
  if (gf_log_init (cmd_def_log_file) < 0){
    return 1;
  }
  gf_log_set_loglevel (cmd_def_log_level);

  if (gf_cmd_def_daemon_mode == GF_YES) {
    int i;
    for (i=0;i<argc;i++)
      memset (argv[i], ' ', strlen (argv[i]));
    sprintf (argv[0], "[glusterfsd]");
    daemon (0, 0);
  }

  /*we want to dump the core and
    we also don't want to limit max number of open files on glusterfs */
  {
    struct rlimit lim;
    lim.rlim_cur = RLIM_INFINITY;
    lim.rlim_max = RLIM_INFINITY;
    
    if (setrlimit (RLIMIT_CORE, &lim) < 0) {
      gf_log ("glusterfsd",
	      GF_LOG_DEBUG,
	      ">main: failed to set RLIMIT_CORE, error string is %s",
	      strerror (errno));
    }
    
    lim.rlim_cur = 65535; //RLIM_INFINITY;
    lim.rlim_max = 65535; //RLIM_INFINITY;
    
    if (setrlimit (RLIMIT_NOFILE, &lim) < 0) {
      gf_log ("glusterfsd",
	      GF_LOG_DEBUG,
	      "main: failed to set RLIMIT_NOFILE, error string is %s",
	      strerror (errno));
    }
  }

  /* Ignore SIGPIPE */
  signal (SIGPIPE, SIG_IGN);

  if (specfile) {
    fp = fopen (specfile, "r");
    xlator_tree_node = get_xlator_graph (fp);
    if (!xlator_tree_node) {
      gf_log ("glusterfsd",
	      GF_LOG_ERROR,
	      "FATAL: could not create node graph");
      exit (1);
    }
    fclose (fp);
  } else {
    gf_log ("glusterfsd",
	    GF_LOG_DEBUG,
	    "main: specfile not provided as command line arg"); 
    argp_help (&argp, stderr, ARGP_HELP_USAGE, argv[0]);
    exit (0);
  }


  while (!transport_poll ());

  return 0;
}
