/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
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
static char *mt_options = NULL;
static char *mount_point = NULL;
static char doc[] = "glusterfs is a glusterfs client";
static char argp_doc[] = "MOUNT-POINT";
static int cmd_def_log_level = GF_LOG_MAX;
static char *cmd_def_log_file = DEFAULT_LOG_FILE;
int gf_cmd_def_daemon_mode = GF_YES;


const char *argp_program_version = PACKAGE_NAME " " PACKAGE_VERSION;
const char *argp_program_bug_address = PACKAGE_BUGREPORT;

struct spec_location spec;
error_t parse_opts (int key, char *arg, struct argp_state *_state);

struct {
  char *f[2];
} f;

static struct argp_option options[] = {
  {"options", 'o', "OPTIONS", 0, "Filesystem mount options" },
  {"spec-file", 'f', "VOLUMESPEC-FILE", 0, "Load volume spec file VOLUMESPEC" },
  {"spec-server-ip", 's', "VOLUMESPEC-SERVERIP", 0, "Get volume spec file from VOLUMESPEC-SERVERIP"},
  {"spec-server-port", 'p', "VOLUMESPEC-SERVERPORT", 0, "connect to VOLUMESPEC_SERVERPORT on spec server"},
  {"log-level", 'L', "LOGLEVEL", 0, "Default LOGLEVEL"},
  {"log-file", 'l', "LOGFILE", 0, "Specify the file to redirect logs"},
  {"no-daemon", 'N', 0, 0, "Run glusterfs in foreground"},
  {"version", 'V', 0, 0, "print version information"},
  { 0, }
};
static struct argp argp = { options, parse_opts, argp_doc, doc };

static int
glusterfsd_print_version (void)
{
  printf ("%s\n", argp_program_version);
  printf ("Copyright (c) 2006 Z RESEARCH Inc. <http://www.zresearch.com>\n");
  printf ("GlusterFS comes with ABSOLUTELY NO WARRANTY.\nYou may redistribute copies of GlusterFS under the terms of the GNU General Public License.\n");
  exit (0);
}

error_t
parse_opts (int key, char *arg, struct argp_state *_state)
{
  switch (key){
  case 'f':
    spec.where = SPEC_LOCAL_FILE;
    spec.spec.file = strdup (arg);
    break;
  case 'o':
    mt_options = arg;
    break;
  case 's':
    spec.where = SPEC_REMOTE_FILE;
    spec.spec.server.ip = strdup (arg);
    break;
  case 'p':
    spec.spec.server.port = strdup (arg);
    break;
  case 'L':
    /* set log level */
    if (!strncmp (arg, "DEBUG", strlen ("DEBUG"))) {
      cmd_def_log_level = GF_LOG_DEBUG;
    } else if (!strncmp (arg, "NORMAL", strlen ("NORMAL"))) {
      cmd_def_log_level = GF_LOG_NORMAL;
    } else if (!strncmp (arg, "CRITICAL", strlen ("CRITICAL"))) {
      cmd_def_log_level = GF_LOG_CRITICAL;
    } else {
      cmd_def_log_level = GF_LOG_NORMAL;
    }
    break;
  case 'l':
    /* set log file */
    cmd_def_log_file = arg;
    break;
  case 'N':
    gf_cmd_def_daemon_mode = GF_NO;
    break;
  case 'V':
    glusterfsd_print_version ();
    break;
  case ARGP_KEY_NO_ARGS:
    argp_usage (_state);
    break;
  case ARGP_KEY_ARG:
    mount_point = arg;
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

  args_init (argc, argv);
  if (gf_log_init (cmd_def_log_file) == -1) {
    fprintf (stderr, "%s: failed to open logfile \"%s\"\n", argv[0], cmd_def_log_file);
    return 1;
  }
  gf_log_set_loglevel (cmd_def_log_level);

  if (mount_point){
    return glusterfs_mount (&spec, mount_point, mt_options);
  } else{
    argp_help (&argp, stderr,ARGP_HELP_USAGE , argv[0]);
    return 1;
  }
}
