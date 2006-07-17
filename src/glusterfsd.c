
#include "glusterfs.h"

extern int server_fs_loop (int client_sock);

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
  int allocfd_count = GLUSTER_OPENFD_MIN;
  struct pollfd *pfd = (struct pollfd *)malloc (allocfd_count * sizeof (struct pollfd *));

  pfd[num_pfd].fd = main_sock;
  pfd[num_pfd].events = POLLIN | POLLPRI | POLLOUT;
  num_pfd++; max_pfd++;

  while (1) {
    if (poll(pfd, max_pfd, -1) < 0) {
      /* This should not get timedout */
      gprintf("poll(): %s", strerror(errno));
      return;
    }
    for (s=0; s < max_pfd; s++) {
      if ((pfd[s].revents | POLLIN) || (pfd[s].revents | POLLPRI) || (pfd[s].revents | POLLOUT)) {
	if (pfd[s].fd == main_sock) {
	  int client_sock;

	  client_sock = register_new_sock (pfd[s].fd);
	  pfd[num_pfd].fd = client_sock;
	  pfd[num_pfd].events = POLLIN | POLLPRI | POLLOUT;
	  num_pfd++;
	  if (num_pfd == allocfd_count)
	    allocfd_count *= 2;
	  pfd = realloc (pfd, allocfd_count * sizeof (struct pollfd *));
	  
	  continue;
	}
	ret = server_fs_loop (pfd[s].fd);
	if (ret == -1) {
	  /* Some error in the socket, close it */
	  pfd[s].fd = pfd[num_pfd].fd;
	  close (pfd[num_pfd].fd);
	  num_pfd--;
	}
      } else if (pfd[s].revents | POLLOUT) {
	/* Write data on the socket */
      }
    }
    max_pfd = num_pfd;
  }
  return;
}

int
main (int argc, char *argv[])
{
  int main_sock;

  if (argc > 1)
    chdir (argv[1]);
  else
    chdir ("/");
  
  /* change this to handle multiple requests */
  main_sock = server_init ();
  if (main_sock == -1) 
    return 1;
  
  server_loop (main_sock);
  return 0;
}

