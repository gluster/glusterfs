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
  License aint64_t with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#include "glusterfsd.h"
#include "protocol.h"
#include "fnmatch.h"
#include "lock.h"
#include "ns.h"

int
glusterfsd_getspec (struct sock_private *sock_priv)
{
  int32_t ret = -1;
  int32_t spec_fd = -1;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  void *file_data = NULL;
  int32_t file_data_len = 0;
  int32_t offset = 0;

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
  int32_t ret = -1;
  int32_t spec_fd = -1;
  int32_t remote_errno = 0;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);

  data_t *data = dict_get (dict, "spec-file-data");
  void *file_data = data_to_bin (data);
  int32_t file_data_len = data->len;
  int32_t offset = 0;

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
  int32_t ret = -1;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);

  data_t *path_data = dict_get (dict, "PATH");

  if (!path_data) {
    dict_set (dict, "RET", int_to_data (-1));
    dict_set (dict, "ERRNO", int_to_data (ENOENT));
    dict_destroy (dict);
    return -1;
  }

  char *path = data_to_str (path_data);

  ret = gf_lock_try_acquire (path);

  if (!ret) {
    path_data->is_static = 1;

    struct held_locks *newlock = calloc (1, sizeof (*newlock));
    newlock->next = sock_priv->locks;
    sock_priv->locks = newlock;
    newlock->path = strdup (path);
  }
  
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_MGMT_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int
glusterfsd_unlock (struct sock_private *sock_priv)
{
  int32_t ret = -1;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);

  data_t *path_data = dict_get (dict, "PATH");
  char *path = data_to_str (path_data);


  if (!path_data) {
    dict_set (dict, "RET", int_to_data (-1));
    dict_set (dict, "ERRNO", int_to_data (ENOENT));
    dict_destroy (dict);
    return -1;
  }

  path = data_to_str (path_data);

  ret = gf_lock_release (path);

  {
    struct held_locks *l = sock_priv->locks;
    struct held_locks *p = NULL;

    while (l) {
      if (!strcmp (l->path, path))
	break;
      p = l;
      l = l->next;
    }

    if (l) {
      if (p)
	p->next = l->next;
      else
	sock_priv->locks = l->next;

      free (l->path);
      free (l);
    }
  }
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));


  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_MGMT_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int
glusterfsd_listlocks (struct sock_private *sock_priv)
{
  int32_t ret = -1;
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict || !blk){
    gf_log ("glusterfsd", GF_LOG_CRITICAL, "glusterfsd-mgmt.c->glusterfsd_listlocks: get_new_dict failed");
    ret = -1;
    errno = 0;
    goto fail;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  if (!dict){
    gf_log ("glusterfsd", GF_LOG_CRITICAL, "glusterfsd-mgmt.c->glusterfsd_listlocks: dict_unserialised failed");
    ret = -1;
    errno = 0;
    goto fail;
  }

  /* logic to read the locks and send them to the person who requested for it */
  {
    int32_t junk = data_to_int (dict_get (dict, "OP"));
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-mgmt.c->glusterfsd_listlocks: junk is %x", junk);
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-mgmt.c->glusterfsd_listlocks: listlocks called");
    ret = gf_listlocks ();
    
  }

  free (blk->data);
  


  errno = 0;

  dict_set (dict, "RET_OP", int_to_data (0xbabecafe));
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

 fail:
  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_MGMT_REPLY);
  dict_destroy (dict);
  
  return 0;
}


int
glusterfsd_nslookup (struct sock_private *sock_priv)
{
  int32_t ret = -1;
  int32_t remote_errno = -ENOENT;
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);  

  data_t *path_data = dict_get (dict, "PATH");
  char *path = data_to_str (path_data);
  char *ns = ns_lookup (path);

  ns = ns ? (ret = 0, remote_errno = 0, ns) : "";
  
  dict_set (dict, "NS", str_to_data (ns));

  dict_del (dict, "PATH");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (remote_errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_MGMT_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int
glusterfsd_nsupdate (struct sock_private *sock_priv)
{
  int32_t ret = -1;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);

  data_t *path_data = dict_get (dict, "PATH");
  char *path = data_to_str (path_data);
  data_t *ns_data = dict_get (dict, "NS");
  ns_data->is_static = 1;
  path_data->is_static = 1;

  ret = ns_update (path, data_to_str (ns_data));

  dict_del (dict, "PATH");
  dict_del (dict, "NS");
  
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_MGMT_REPLY);
  dict_destroy (dict);
  
  return 0;
}


int
glusterfsd_getvolume (struct sock_private *sock_priv)
{
  return 0;
}

int
glusterfsd_setvolume (struct sock_private *sock_priv)
{
  int32_t ret = 0;
  int32_t remote_errno = 0;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  char *name = data_to_str (dict_get (dict, "remote-subvolume"));
  struct xlator *xl = gf_get_xlator_tree_node ();
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
    int32_t flag = 0;
    if (allow_ip) {
      // check IP range and decide whether the client can do this or not
      socklen_t sock_len = sizeof (struct sockaddr);
      struct sockaddr_in *_sock = calloc (1, sizeof (struct sockaddr_in));
      getpeername (sock_priv->fd, _sock, &sock_len);
      gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-mgmt.c->glusterfsd_setvolume: received port = %d\n", ntohs (_sock->sin_port));
      if (ntohs (_sock->sin_port) < 1024) {
	char *ip_addr_str = NULL;
	char *tmp;
	char *ip_addr_cpy = strdup (allow_ip->data);
	ip_addr_str = strtok_r (ip_addr_cpy , ",", &tmp);
	while (ip_addr_str) {
	  gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-mgmt.c->glusterfsd_setvolume: IP addr = %s, received ip addr = %s\n", 
		  ip_addr_str, 
		  inet_ntoa (_sock->sin_addr));
	  if (fnmatch (ip_addr_str, inet_ntoa (_sock->sin_addr), 0) == 0) {
	    sock_priv->xl = xl;
	    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-mgmt.c->glusterfsd_setvolume: accepted client from %s\n", inet_ntoa (_sock->sin_addr));
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
  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);

  extern int64_t glusterfsd_stats_nr_clients;

  if (!dict)
    return -1;
  struct xlator *xl = gf_get_xlator_tree_node ();
  struct xlator_stats stats;

  int32_t ret = xl->mgmt_ops->stats (xl, &stats);

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  if (ret == 0) {
    char buffer[256] = {0,};
    sprintf (buffer, GF_MGMT_STATS_PRINT_FMT_STR,
	     stats.nr_files,
	     stats.disk_usage,
	     stats.free_disk,
	     stats.read_usage,
	     stats.write_usage,
	     stats.disk_speed,
	     glusterfsd_stats_nr_clients);
    dict_set (dict, "BUF", str_to_data (buffer));
  }

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_MGMT_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int
handle_mgmt (glusterfsd_fn_t *gmgmtd, struct sock_private *sock_priv)
{
  int32_t ret;
  gf_block *blk = (gf_block *) sock_priv->private;
  int32_t op = blk->op;

  ret = gmgmtd[op].function (sock_priv);

  if (ret != 0) {
    gf_log ("glusterfsd", GF_LOG_ERROR, "glusterfsd-mgmt.c->handle_mgmt: terminating, errno=%d, error string is %s", errno, strerror (errno));
    return -1;
  }
  return 0;
}
