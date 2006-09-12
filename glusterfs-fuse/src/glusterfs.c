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
  {"log-level", 'L', "GF_LOGLEVEL", 0, "Default LOGLEVEL"},
  {"log-file", 'l', "GF_LOGFILE", 0, "Specify the file to redirect logs"},
  { 0, }
};
static struct argp argp = { options, parse_opts, argp_doc, doc };

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
    cmd_def_log_level = atoi (arg);
    break;
  case 'l':
    /* set log file */
    cmd_def_log_file = arg;
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
  gf_log_set_loglevel (GF_LOG_MAX);

  if (mount_point){
    return glusterfs_mount (&spec, mount_point, mt_options);
  } else{
    argp_help (&argp, stderr,ARGP_HELP_USAGE , argv[0]);
    return 1;
  }
}
