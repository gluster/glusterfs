#include "glusterfsd.h"
#include "fnmatch.h"

int
glusterfsd_getvolume (struct sock_private *sock_priv)
{
  return 0;
}

int
glusterfsd_setvolume (struct sock_private *sock_priv)
{
  int ret = 0;
  int remote_errno = 0;
  char readbuf[80] = {0,};
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDMGMT ();
  char *name = data_to_str (dict_get (dict, "RemoteSubVolume"));
  struct xlator *xl = get_xlator_tree_node ();
  FUNCTION_CALLED;

  while (xl) {
    if (strcmp (xl->name, name) == 0)
      break;
    xl = xl->next;
  }
  
  if (!xl) {
    ret = -1;
    remote_errno = ENOENT;
    sock_priv->xl = NULL;
  } else {
    data_t *allow_ip = dict_get (xl->options, "AllowIP");
    int flag = 0;
    if (allow_ip) {
      // check IP range and decide whether the client can do this or not
      int sock_len = sizeof (struct sockaddr);
      struct sockaddr_in *_sock = calloc (1, sizeof (struct sockaddr_in));
      getpeername (sock_priv->fd, _sock, &sock_len);
      if (ntohs (_sock->sin_port) < 1024) {
	char *ip_addr_str = NULL;
	char *tmp;
	ip_addr_str = strtok_r (allow_ip->data, ",", &tmp);
	while (ip_addr_str) {
	  if (fnmatch (ip_addr_str, inet_ntoa (_sock->sin_addr), 0) == 0) {
	    sock_priv->xl = xl;
	    printf ("Accepted client from %s\n", inet_ntoa (_sock->sin_addr));
	    flag = 1;
	    break;
	  }
	  ip_addr_str = strtok_r (NULL, ",", &tmp);
	}
      }
    }
    if (!flag) {
      ret = -1;
      remote_errno = EACCES;
      sock_priv->xl = NULL;
    }
  }
  dict_del (dict, "RemoteSubVolume");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (remote_errno));

  dict_dump (fp, dict);
  dict_destroy (dict);
  return ret;
}

int
glusterfsd_stats (struct sock_private *sock_priv)
{
  FUNCTION_CALLED;
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);

  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
  struct xlator_stats stats;
  extern int glusterfsd_stats_nr_clients;

  int ret = xl->mgmt_ops->stats (xl, &stats);

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  if (ret == 0) {
    char buffer[256] = {0,};
    sprintf (buffer, "%lx,%lx,%llx,%llx\n",
	     (long)stats.nr_files,
	     (long)stats.free_mem,
	     (long long)stats.free_disk,
	     (long long)glusterfsd_stats_nr_clients);
    dict_set (dict, "BUF", str_to_data (buffer));
    printf ("stats: buf: %s\n", buffer);
  }

  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
}

int
handle_mgmt (glusterfsd_fn_t *gmgmtd, struct sock_private *sock_priv)
{
  int ret;
  int operation;
  char readbuf[80] = {0,};
  FILE *fp = sock_priv->fp;

  if (fgets (readbuf, 80, fp) == NULL)
    return -1;
  
  operation = strtol (readbuf, NULL, 0);

  if ((operation < 0) || (operation >= MGMT_MAXVALUE))
    return -1;
  
  ret = gmgmtd[operation].function (sock_priv);

  if (ret != 0) {
    gprintf ("%s: terminating, (errno=%d)\n", __FUNCTION__,
	     errno);
    return -1;
  }
  return 0;
}
