
#include "glusterfsd-fops.h"
#include <errno.h>

static struct xlator *xlator_tree_node = NULL;

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

  sock = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (sock == -1) {
    perror ("socket()");
    return -1;
  }

  sin.sin_family = PF_INET;
  sin.sin_port = htons (5252);
  sin.sin_addr.s_addr = INADDR_ANY;

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
  printf ("Accepted connection\n");

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
  int ret;
  int max_pfd = 0;
  int num_pfd = 0;
  int allocfd_count = 1024;
  FILE *fp[64*1024] = {0, };
  glusterfsd_fops_t gfsd[] = { 
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
    {NULL},
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
	if (pfd[s].fd == main_sock) {
	  int client_sock = register_new_sock (pfd[s].fd);
	  pfd[num_pfd].fd = client_sock;
	  pfd[num_pfd].events = POLLIN | POLLPRI;
	  fp[client_sock] = fdopen (client_sock, "a+");
	  num_pfd++;
	  
	  if (num_pfd == allocfd_count) {
	    allocfd_count *= 2;
	    pfd = realloc (pfd, allocfd_count * sizeof (struct pollfd *));
	  }
	  pfd[s].revents = 0;
	  continue;
	}
	FILE *foo = fp[pfd[s].fd];
	ret = server_fs_loop (gfsd, foo);
	if (ret == -1) {
	  printf ("Closing socket %d\n", pfd[s].fd);
	  /* Some error in the socket, close it */
	  close (pfd[s].fd);
	  fclose (fp[pfd[s].fd]);
	  pfd[s].fd = pfd[num_pfd].fd;
	  pfd[s].revents = 0;
	  num_pfd--;
	}
      }
      
      /* NOTE : Make the write more modular */
      /*
	if (pfd[s].revents & POLLOUT) {
         if (glusterfsd_context[pfd[s].fd] != NULL) {
	  struct write_queue *temp;
	  temp = glusterfsd_context[pfd[s].fd]->gfsd_write_queue;
	  
	  printf ("%s: I came here (%s)\n", __FUNCTION__, temp->buffer);
	  write (pfd[s].fd, temp->buffer, temp->buf_len);
	  if (temp->next == NULL) {
	    free (glusterfsd_context[pfd[s].fd]);
	    glusterfsd_context[pfd[s].fd] = NULL;
	  } else {
	    glusterfsd_context[pfd[s].fd]->gfsd_write_queue = temp->next;
	  }
	  pfd[s].revents = 0;
	  free (temp);
	 }
	} */ 
      pfd[s].revents = 0;
    }
    max_pfd = num_pfd;
  }

  return;
}

int
main (int argc, char *argv[])
{
  int main_sock;
  FILE *fp;

  if (argc > 1) {
    fp = fopen (argv[1], "r"); // this is config file
    set_xlator_tree_node (fp);
  }

  main_sock = server_init ();
  if (main_sock == -1) 
    return 1;
  
  server_loop (main_sock);
  return 0;
}

