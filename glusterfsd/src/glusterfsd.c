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


//#include "glusterfsd.h"
#include "protocol.h"

#include <errno.h>
#include <sys/resource.h>
#include <argp.h>
#include <stdint.h>

#include "sdp_inet.h"
#include "transport.h"

#define GF_YES 1
#define GF_NO 0

#define DEFAULT_LOG_FILE DATADIR "/log/glusterfs/glusterfsd.log"

static struct {
  int8_t *f[2];
} f;

/* useful for argp for command line parsing */
static struct argp_option options[] = {
  {"spec-file", 'f', "VOLUMESPEC-FILE", 0, "load the VOLUMESPEC-FILE" },
  {"log-level", 'L', "LOGLEVEL", 0, "default LOGLEVEL"},
  {"log-file", 'l', "LOGFILE", 0, "specify the file to redirect logs"},
  {"no-daemon", 'N', 0, 0, "run glusterfs in foreground"},
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
extern struct confd * file_to_confd (FILE *fp);

int32_t glusterfsd_stats_nr_clients = 0;
static int8_t *specfile = NULL;
static xlator_t *xlator_tree_node = NULL;
struct confd *confd;
static int32_t cmd_def_log_level = GF_LOG_MAX;
static int8_t *cmd_def_log_file = DEFAULT_LOG_FILE;

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

struct server_ctx {
  int client_count;
  int pfd_count;
  struct pollfd *pfd;
  struct {
    int32_t (*handler) (int32_t fd,
			int32_t event,
			void *data);
    void *data;
  } *cbk_data;
};

static struct server_ctx *
get_server_ctx ()
{
  static struct server_ctx *ctx;

  if (!ctx) {
    ctx = (void *)calloc (1, sizeof (*ctx));
    ctx->pfd_count = 1024;
    ctx->pfd = (void *) calloc (1024, 
				sizeof (struct pollfd));
    ctx->cbk_data = (void *) calloc (1024,
				     sizeof (*ctx->cbk_data));
  }

  return ctx;
}

static void
unregister_member (struct server_ctx *ctx,
		    int32_t i)
{
  ctx->pfd[i].fd = ctx->pfd[ctx->client_count - 1].fd;
  ctx->pfd[i].events = ctx->pfd[ctx->client_count - 1].events;
  ctx->pfd[i].revents = ctx->pfd[ctx->client_count - 1].revents;
  ctx->cbk_data[i].handler = ctx->cbk_data[ctx->client_count - 1].handler;
  ctx->cbk_data[i].data = ctx->cbk_data[ctx->client_count - 1].data;

  ctx->client_count--;
  return;
}

static int32_t
new_fd_cbk (int fd, 
	    int32_t (*handler)(int32_t fd,
			       int32_t event,
			       void *data),
	    void *data)
{
  struct server_ctx *ctx = get_server_ctx ();

  if (ctx->client_count == ctx->pfd_count) {
    ctx->pfd_count *= 2;
    ctx->pfd = realloc (ctx->pfd, 
			sizeof (struct pollfd) * ctx->pfd_count);
  }

  ctx->pfd[ctx->client_count].fd = fd;
  ctx->pfd[ctx->client_count].events = POLLIN | POLLOUT | POLLPRI | POLLERR | POLLHUP;
  ctx->pfd[ctx->client_count].revents = 0;

  ctx->cbk_data[ctx->client_count].handler = handler;
  ctx->cbk_data[ctx->client_count].data = data;

  ctx->client_count++;
  return 0;
}

static int32_t
server_init ()
{
  set_transport_register_cbk (new_fd_cbk);
  return 0;
}


static int32_t
server_loop ()
{
  struct server_ctx *ctx = get_server_ctx ();
  struct pollfd *pfd;

  while (1) {
    int32_t ret;
    int32_t i;

    pfd = ctx->pfd;
    ret = poll (pfd,
		(unsigned int) ctx->client_count,
		-1);

    if (ret == -1) {
      if (errno == EINTR) {
	continue;
      } else {
	return -errno;
      }
    }

    for (i=0; i < ctx->client_count; i++) {
      if (pfd[i].revents) {
	if (ctx->cbk_data[i].handler (pfd[i].fd,
				      pfd[i].revents,
				      ctx->cbk_data[i].data) == -1) {
	  unregister_member (ctx, i);
	  i--;
	}
      }
    }
  }
  return 0;
}

static int32_t
glusterfsd_print_version (void)
{
  printf ("%s\n", argp_program_version);
  printf ("Copyright (c) 2006 Z RESEARCH Inc. <http://www.zresearch.com>\n");
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
  
  /*we want to dump the core and
    we also don't want to limit max number of open files on glusterfs */
  {
    struct rlimit lim;
    lim.rlim_cur = RLIM_INFINITY;
    lim.rlim_max = RLIM_INFINITY;
    
    if (setrlimit (RLIMIT_CORE, &lim) < 0) {
      gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->main: failed to set RLIMIT_CORE, error string is %s", strerror (errno));
    }
    
    lim.rlim_cur = 65535; //RLIM_INFINITY;
    lim.rlim_max = 65535; //RLIM_INFINITY;
    
    if (setrlimit (RLIMIT_NOFILE, &lim) < 0) {
      gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->main: failed to set RLIMIT_NOFILE, error string is %s", strerror (errno));
    }
  }

  server_init ();
  
  if (specfile) {
    fp = fopen (specfile, "r");
    xlator_tree_node = get_xlator_graph (fp);
    fclose (fp);
  } else {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->main: specfile not provided as command line arg"); 
    argp_help (&argp, stderr, ARGP_HELP_USAGE, argv[0]);
    exit (0);
  }


  if (gf_cmd_def_daemon_mode == GF_YES)
    daemon (0, 0);

  server_loop ();

  
  return 0;
}
