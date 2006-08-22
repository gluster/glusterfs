
#include "glusterfsd.h"
#include <errno.h>
#include <sys/resource.h>
#include <argp.h>

#define SCRATCH_DIR confd->scratch_dir
#define LISTEN_PORT confd->port

struct {
  char *f[2];
} f;

static struct argp_option options[] = {
  {"config", 'c', "CONFIGFILE", 0, "Load the CONFIGFILE" },
  {"spec", 's', "SPECFILE", 0, "Load the SPECFILE" },
  { 0, }
};

error_t parse_opts (int key, char *arg, struct argp_state *_state);

int glusterfsd_stats_nr_clients = 0;
static char *configfile = NULL;
static char *specfile = NULL;
static char doc[] = "glusterfsd is glusterfs server";
static char argp_doc[] = " ";
static struct argp argp = { options, parse_opts, argp_doc, doc };
static struct xlator *xlator_tree_node = NULL;
struct confd *confd;

void
set_xlator_tree_node (FILE *fp)
{
  struct xlator *xl = file_to_xlator_tree (fp);
  struct xlator *trav = xl;
  xlator_tree_node = xl;
  
  while (trav) {
    if (trav->init)
      trav->init (trav);
    trav = trav->next;
  }
}

struct xlator *
get_xlator_tree_node ()
{
  return xlator_tree_node;
}

static int
server_init ()
{
  int sock;
  struct sockaddr_in sin;
  int opt;
  
  sock = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (sock == -1) {
    perror ("socket()");
    return -1;
  }

  sin.sin_family = PF_INET;
  sin.sin_port = htons (LISTEN_PORT);
  sin.sin_addr.s_addr = INADDR_ANY;

  opt = 1;
  setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt));
  if (bind (sock, (struct sockaddr *)&sin, sizeof (sin)) != 0) {
    perror ("bind()");
    return -1;
  }

  if (listen (sock, 10) != 0) {
    perror ("listen()");
    return -1;
  }

  return sock;
}

int
register_new_sock (int s) 
{
  int client_sock;
  struct sockaddr_in sin;
  int len = sizeof (sin);

  client_sock = accept (s, (struct sockaddr *)&sin, &len);
  gluster_log ("glusterfsd", LOG_NORMAL, "Accepted connection from %s", inet_ntoa (sin.sin_addr));

  if (client_sock == -1) {
    perror ("accept()");
    return -1;
  }

  return client_sock;
}

static void
server_loop (int main_sock)
{
  int s;
  int ret = 0;
  int max_pfd = 0;
  int num_pfd = 0;
  int allocfd_count = 1024;
  struct sock_private sock_priv[64*1024] = {{0,},}; //FIXME: is this value right?
  glusterfsd_fn_t gfopsd[] = { 
    {glusterfsd_getattr},
    {glusterfsd_readlink},
    {glusterfsd_mknod},
    {glusterfsd_mkdir},
    {glusterfsd_unlink},
    {glusterfsd_rmdir},
    {glusterfsd_symlink},
    {glusterfsd_rename},
    {glusterfsd_link},
    {glusterfsd_chmod},
    {glusterfsd_chown},
    {glusterfsd_truncate},
    {glusterfsd_utime},
    {glusterfsd_open},
    {glusterfsd_read},
    {glusterfsd_write},
    {glusterfsd_statfs},
    {glusterfsd_flush},
    {glusterfsd_release},
    {glusterfsd_fsync},
    {glusterfsd_setxattr},
    {glusterfsd_getxattr},
    {glusterfsd_listxattr},
    {glusterfsd_removexattr},
    {glusterfsd_opendir},
    {glusterfsd_readdir},
    {glusterfsd_releasedir},
    {glusterfsd_fsyncdir},
    {glusterfsd_init},
    {glusterfsd_destroy},
    {glusterfsd_access},
    {glusterfsd_create},
    {glusterfsd_ftruncate},
    {glusterfsd_fgetattr},
    {glusterfsd_bulk_getattr},
    {NULL},
  };
  glusterfsd_fn_t gmgmtd[] = {
    {glusterfsd_setvolume},
    {glusterfsd_getvolume},
    {glusterfsd_stats},
    {NULL}
  };
  struct pollfd *pfd = (struct pollfd *)malloc (allocfd_count * sizeof (struct pollfd *));
  
  pfd[num_pfd].fd = main_sock;
  pfd[num_pfd].events = POLLIN | POLLPRI | POLLOUT;
  num_pfd++; max_pfd++;
  
  while (1) {
    if (poll(pfd, max_pfd, -1) < 0) {
      /* This should not get timedout (look at -1) */
      if (errno == EINTR)
	continue;
      gprintf("poll(): %s", strerror(errno));
      return;
    }
    
    for (s=0; s < max_pfd; s++) {
      if ((pfd[s].revents & POLLIN) || (pfd[s].revents & POLLPRI) || (pfd[s].revents & POLLOUT)) {
	/* If activity is on main socket, accept the new connection */
	ret = 0;
	if (pfd[s].fd == main_sock) {
	  int client_sock = register_new_sock (pfd[s].fd);
	  glusterfsd_stats_nr_clients++;
	  pfd[num_pfd].fd = client_sock;
	  pfd[num_pfd].events = POLLIN | POLLPRI;
	  sock_priv[client_sock].fd = client_sock;
	  sock_priv[client_sock].fp = fdopen (client_sock, "a+");
	  sock_priv[client_sock].fctxl = calloc (1, sizeof (struct file_ctx_list));

	  num_pfd++;
	  if (num_pfd == allocfd_count) {
	    allocfd_count *= 2;
	    pfd = realloc (pfd, allocfd_count * sizeof (struct pollfd *));
	  }
	  pfd[s].revents = 0;
	  continue;
	}
	FILE *fp = sock_priv[pfd[s].fd].fp;
	char readbuf[80];
	fgets (readbuf, 80, fp);
	if (strcasecmp (readbuf, "BeginFops\n") == 0) {
	  ret = handle_fops (gfopsd, &sock_priv[pfd[s].fd]);
	} else if (strcasecmp (readbuf, "BeginMgmt\n") == 0) {
	  ret = handle_mgmt (gmgmtd, &sock_priv[pfd[s].fd]);
	} else {
	  gluster_log ("glusterfsd", LOG_CRITICAL, "Protocol error: no begining command found");
	  ret = -1;
	}
	fflush (fp);
	if (ret == -1) {
	  int idx = pfd[s].fd;
	  gluster_log ("glusterfsd", LOG_DEBUG, "Closing socket %d\n", idx);
	  /* Some error in the socket, close it */
	  if (sock_priv[idx].xl) {
	    struct file_ctx_list *trav_fctxl = sock_priv[idx].fctxl->next;
	    while (trav_fctxl) {
	      sock_priv[idx].xl->fops->release (sock_priv[idx].xl, 
						trav_fctxl->path, 
						trav_fctxl->ctx);
	      trav_fctxl = trav_fctxl->next;
	    }
	  }
	  free (sock_priv[idx].fctxl);
	  close (idx);
	  glusterfsd_stats_nr_clients--;
	  fclose (sock_priv[idx].fp);

	  pfd[s].fd = pfd[num_pfd].fd;
	  pfd[s].revents = 0;
	  num_pfd--;
	}
      }
      pfd[s].revents = 0;
    }
    max_pfd = num_pfd;
  }

  return;
}

error_t
parse_opts (int key, char *arg, struct argp_state *_state)
{
  switch (key){
  case 'c':
    configfile = arg;
    break;
  case 's':
    specfile = arg;
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
  int main_sock;
  FILE *fp;
  struct rlimit lim;

  lim.rlim_cur = RLIM_INFINITY;
  lim.rlim_max = RLIM_INFINITY;
  setrlimit (RLIMIT_CORE, &lim);
  setrlimit (RLIMIT_NOFILE, &lim);
  
  gluster_log_init ("/tmp/glusterlog");
  gluster_log_set_loglevel (LOG_DEBUG);

  args_init (argc, argv);
  if (specfile) {
    fp = fopen (specfile, "r");
    set_xlator_tree_node (fp);
    fclose (fp);
  } else {
    argp_help (&argp, stderr, ARGP_HELP_USAGE, argv[0]);
    exit (0);
  }

  if (configfile) {
    fp = fopen (configfile, "r");
    confd = file_to_confd (fp);
    if (!confd) {
      /* config file error or not found, use the default values */
      struct confd *default_confd = calloc (1, sizeof (struct confd));
      default_confd->chroot_dir = calloc (1, strlen ("/tmp") + 1);
      default_confd->scratch_dir = calloc (1, strlen ("/tmp") + 1);
      strcpy (default_confd->chroot_dir, "/tmp");
      strcpy (default_confd->scratch_dir, "/tmp");
      default_confd->key_len = 4096;
      default_confd->port = 5252;
      confd = default_confd;
    }
    fclose (fp);
  } else {
    // FIXME: What should be done ? default values or compulsary config file ?
    argp_help (&argp, stderr, ARGP_HELP_USAGE, argv[0]);
    exit (0);    
  }

  chdir (confd->chroot_dir);
  
  main_sock = server_init ();
  if (main_sock == -1) 
    return 1;
  
  server_loop (main_sock);
  return 0;
}
