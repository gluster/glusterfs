#include "glusterfsd.h"
#include "protocol.h"
#include "fnmatch.h"

static char *server_spec, *client_spec;

#include "lock.h"

int
glusterfsd_getspec (struct sock_private *sock_priv)
{
  int ret = -1;
  int spec_fd = -1;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  void *file_data = NULL;
  int file_data_len = 0;
  int offset = 0;

  struct stat *stbuf = alloca (sizeof (struct stat));

  ret = open (GLUSTERFSD_SPEC_PATH, O_RDONLY);
  spec_fd = ret;
  if (spec_fd < 0){
    goto fail;
  }
  
  /* to allocate the proper buffer to hold the file data */
  {
    ret = stat (GLUSTERFSD_SPEC_PATH, stbuf);
    if (ret < 0){
      goto fail;
    }
    
    file_data_len = stbuf->st_size;
    file_data = calloc (1, file_data_len);
  }
  
  while ((ret = read (spec_fd, file_data + offset, file_data_len))){
    if (ret < 0){
      goto fail;
    }
    
    if (ret < file_data_len){
      offset = offset + ret + 1;
      file_data_len = file_data_len - ret;
    }
  }
  
  dict_set (dict, "spec-file-data", bin_to_data (file_data, stbuf->st_size));
 
 fail:
    
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_MGMT_REPLY);
  dict_destroy (dict);
  
  return ret;

}

int
glusterfsd_setspec (struct sock_private *sock_priv)
{
  int ret = -1;
  int spec_fd = -1;
  int remote_errno = 0;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);

  data_t *data = dict_get (dict, "spec-file-data");
  void *file_data = data_to_bin (data);
  int file_data_len = data->len;
  int offset = 0;

  ret = mkdir (GLUSTERFSD_SPEC_DIR, 0x777);
  
  if (ret < 0 && errno != EEXIST){
    remote_errno = errno;
    goto fail;
  }
  
  ret = open (GLUSTERFSD_SPEC_PATH, O_WRONLY | O_CREAT | O_SYNC);
  spec_fd = ret;
  if (spec_fd < 0){
    remote_errno = errno;
    goto fail;
  }

  while ((ret = write (spec_fd, file_data + offset, file_data_len))){
    if (ret < 0){
      remote_errno = errno;
      goto fail;
    }
    
    if (ret < file_data_len){
      offset = ret + 1;
      file_data_len = file_data_len - ret;
    }
  }
      
 fail:
  dict_del (dict, "spec-file-data");
  
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (remote_errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_MGMT_REPLY);
  dict_destroy (dict);
  
  return ret;
}

int
glusterfsd_lock (struct sock_private *sock_priv)
{
  int ret;
  int fd = sock_priv->fd;
  dict_t *dict = dict_load (fp);

  //  CHECK_ENDMGMT ();

  data_t *path_data = dict_get (dict, "PATH");
  char *path;

  if (!path) {
    dict_set (dict, "RET", int_to_data (-1));
    dict_set (dict, "ERRNO", int_to_data (ENOENT));
    dict_dump (fp, dict);
    dict_destroy (dict);
    return -1;
  }

  path = data_to_str (path_data);

  ret = lock_try_acquire (path);

  if (!ret) {
    /* TODO: register the lock with sock_priv
       for unlocking when connex dies
    */
  }
  
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_unlock (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  int ret;

  //  CHECK_ENDMGMT ();

  data_t *path_data = dict_get (dict, "PATH");
  char *path;

  if (!path) {
    dict_set (dict, "RET", int_to_data (-1));
    dict_set (dict, "ERRNO", int_to_data (ENOENT));
    dict_dump (fp, dict);
    dict_destroy (dict);
    return -1;
  }

  path = data_to_str (path_data);

  ret = lock_release (path);
  
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_nslookup (struct sock_private *sock_priv)
{

}

int
glusterfsd_nsupdate (struct sock_private *sock_priv)
{


}


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

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  char *name = data_to_str (dict_get (dict, "remote-subvolume"));
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
    data_t *allow_ip = dict_get (xl->options, "allow-ip");
    int flag = 0;
    if (allow_ip) {
      // check IP range and decide whether the client can do this or not
      int sock_len = sizeof (struct sockaddr);
      struct sockaddr_in *_sock = calloc (1, sizeof (struct sockaddr_in));
      getpeername (sock_priv->fd, _sock, &sock_len);
      printf ("received port = %d\n", ntohs (_sock->sin_port));
      if (ntohs (_sock->sin_port) < 1024) {
	char *ip_addr_str = NULL;
	char *tmp;
	char *ip_addr_cpy = strdup (allow_ip->data);
	ip_addr_str = strtok_r (ip_addr_cpy , ",", &tmp);
	while (ip_addr_str) {
	  printf ("IP addr = %s, received ip addr = %s\n", 
		  ip_addr_str, 
		  inet_ntoa (_sock->sin_addr));
	  if (fnmatch (ip_addr_str, inet_ntoa (_sock->sin_addr), 0) == 0) {
	    sock_priv->xl = xl;
	    printf ("Accepted client from %s\n", inet_ntoa (_sock->sin_addr));
	    flag = 1;
	    break;
	  }
	  ip_addr_str = strtok_r (NULL, ",", &tmp);
	}
	free (ip_addr_cpy);
      }
    }
    if (!flag) {
      ret = -1;
      remote_errno = EACCES;
      sock_priv->xl = NULL;
    }
  }
  dict_del (dict, "remote-subvolume");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (remote_errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_MGMT_REPLY);
  dict_destroy (dict);
  
  return ret;
}

int
glusterfsd_stats (struct sock_private *sock_priv)
{
  FUNCTION_CALLED;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);

  extern int glusterfsd_stats_nr_clients;

  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
  struct xlator_stats stats;

  int ret = xl->mgmt_ops->stats (xl, &stats);

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  if (ret == 0) {
    char buffer[256] = {0,};
    sprintf (buffer, "%lx,%llx,%llx,%llx,%llx,%lx,%lx\n",
	     (long)stats.nr_files,
	     (long long)stats.disk_usage,
	     (long long)stats.free_disk,
	     (long long)stats.read_usage,
	     (long long)stats.write_usage,
	     (long)stats.disk_speed,
	     (long)glusterfsd_stats_nr_clients);
    dict_set (dict, "BUF", str_to_data (buffer));
  }

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_MGMT_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int
handle_mgmt (glusterfsd_fn_t *gmgmtd, struct sock_private *sock_priv)
{
  int ret;
  gf_block *blk = (gf_block *) sock_priv->private;
  int op = blk->op;

  ret = gmgmtd[op].function (sock_priv);

  if (ret != 0) {
    gprintf ("%s: terminating, (errno=%d)\n", __FUNCTION__,
	     errno);
    return -1;
  }
  return 0;
}
