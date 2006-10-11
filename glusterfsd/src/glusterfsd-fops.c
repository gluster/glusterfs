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


#include "glusterfsd.h"
#include "xlator.h"

#include <time.h>

#if __WORDSIZE == 64
# define F_L64 "%l"
#else
# define F_L64 "%ll"
#endif

int32_t 
glusterfsd_open (struct sock_private *sock_priv)
{
  
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_open: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  int32_t ret = -1;
  
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_open: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);

  data_t *path_data = dict_get (dict, "PATH");
  
  if (!path_data){
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_open: dict_get failed for key \"PATH\"");
    ret = -1;
    goto fail;
  }

  int8_t *path = data_to_bin (path_data);
  
  if (!path) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_open: data_to_bin failed to to get \"PATH\"");
    ret = -1;
    goto fail;
  }

  struct xlator *xl = sock_priv->xl;
  struct file_ctx_list *fctxl = calloc (1, sizeof (struct file_ctx_list));
  struct file_context *ctx = calloc (1, sizeof (struct file_context));

  if (!fctxl || !ctx){
    gf_log ("glusterfsd", GF_LOG_CRITICAL, "glusterfsd-fops.c->glusterfsd_open: failed to calloc for fctxl/ctxt");
    ret = -1;
    goto fail;
  }

  {
    data_t *flags_d = dict_get (dict, "FLAGS");
    data_t *mode_d =  dict_get (dict, "MODE");
    
    if (!flags_d || !mode_d){
      gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_open:  dict_get failed for key \"FLAGS\"/\"MODE\"");
      ret = -1;
      goto fail;
    }

    int32_t flags = data_to_int (flags_d);
    int32_t mode = data_to_int (mode_d);

    ret = xl->fops->open (xl, path, flags, mode, ctx);
    if (ret >= 0) {
      fctxl->ctx = ctx;
      fctxl->path = strdup (path);
      fctxl->next = (sock_priv->fctxl)->next;
      (sock_priv->fctxl)->next = fctxl;
    } else {
      free (fctxl);
      free (ctx);
    }
  }
 
 fail:
  dict_del (dict, "FLAGS");
  dict_del (dict, "PATH");
  dict_del (dict, "MODE");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));
  dict_set (dict, "FD", int_to_data ((long)ctx));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);

  return 0;
}

int32_t 
glusterfsd_release (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_release: invalid argument");
    return -1;
  }
  
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_release: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);

  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;  
  struct file_ctx_list *trav_fctxl = sock_priv->fctxl;
  struct file_context *tmp_ctx = (struct file_context *)(long)data_to_int (dict_get (dict, "FD"));

  while (trav_fctxl) {
    if (tmp_ctx == trav_fctxl->ctx)
      break;
    trav_fctxl = trav_fctxl->next;
  }

  if (! (trav_fctxl && trav_fctxl->ctx == tmp_ctx))
    return -1;

  trav_fctxl = sock_priv->fctxl;

  int32_t ret = xl->fops->release (xl,
			       data_to_bin (dict_get (dict, "PATH")),
			       tmp_ctx);

  while (trav_fctxl->next) {
    if ((trav_fctxl->next)->ctx == tmp_ctx) {
      struct file_ctx_list *fcl = trav_fctxl->next;
      trav_fctxl->next = fcl->next;
      free (fcl->path);
      free (fcl->ctx);
      free (fcl);
      break;
    }
    trav_fctxl = trav_fctxl->next;
  }

  dict_del (dict, "FD");
  dict_del (dict, "PATH");

  dict_set (dict, "ERRNO", int_to_data (errno));
  dict_set (dict, "RET", int_to_data (ret));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);

  return  0;
}

int32_t 
glusterfsd_flush (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_flush: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();

  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_flush: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);

  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int32_t ret = xl->fops->flush (xl,
			     data_to_bin (dict_get (dict, "PATH")),
			     (struct file_context *)(long)data_to_int (dict_get (dict, "FD")));
  
  dict_del (dict, "FD");
  dict_del (dict, "PATH");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);

  return  0;
}


int32_t 
glusterfsd_fsync (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_fsync: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_fsync: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int32_t ret = xl->fops->fsync (xl,
			     data_to_bin (dict_get (dict, "PATH")),
			     data_to_int (dict_get (dict, "FLAGS")),
			     (struct file_context *)(long)data_to_int (dict_get (dict, "FD")));
  
  dict_del (dict, "PATH");
  dict_del (dict, "FD");
  dict_del (dict, "FLAGS");

  dict_set (dict, "ERRNO", int_to_data (errno));
  dict_set (dict, "RET", int_to_data (ret));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return  0;
}

int32_t 
glusterfsd_write (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_write: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();

  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_write: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  data_t *datat = dict_get (dict, "BUF");
  struct file_context *tmp_ctx = (struct file_context *)(long)data_to_int (dict_get (dict, "FD"));

  {
    struct file_ctx_list *fctxl = sock_priv->fctxl;

    while (fctxl) {
      if (fctxl->ctx == tmp_ctx)
	break;
      fctxl = fctxl->next;
    }

    if (!fctxl)
      /* TODO: write error to socket instead of returning */
      return -1;
  }

  int32_t ret = xl->fops->write (xl,
			     data_to_bin (dict_get (dict, "PATH")),
			     datat->data,
			     datat->len,
			     data_to_int (dict_get (dict, "OFFSET")),
			     tmp_ctx);

  dict_del (dict, "PATH");
  dict_del (dict, "OFFSET");
  dict_del (dict, "BUF");
  dict_del (dict, "FD");
  
  {
    dict_set (dict, "RET", int_to_data (ret));
    dict_set (dict, "ERRNO", int_to_data (errno));
  }

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int32_t 
glusterfsd_read (struct sock_private *sock_priv)
{
  int32_t len = 0;

  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_read: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();

  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_read: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int32_t size = data_to_int (dict_get (dict, "LEN"));
  static int8_t *data = NULL;
  static int32_t data_len = 0;

  {
    struct file_context *tmp_ctx = (struct file_context *)(long)data_to_int (dict_get (dict, "FD"));
    struct file_ctx_list *fctxl = sock_priv->fctxl;

    while (fctxl) {
      if (fctxl->ctx == tmp_ctx)
	break;
      fctxl = fctxl->next;
    }

    if (!fctxl)
      /* TODO: write error to socket instead of returning */
      return -1;
  }
  
  if (size > 0) {
    if (size > data_len) {
      if (data)
	free (data);
      data = malloc (size * 2);
      data_len = size * 2;
    }
    len = xl->fops->read (xl,
			  data_to_bin (dict_get (dict, "PATH")),
			  data,
			  size,
			  data_to_int (dict_get (dict, "OFFSET")),
			  (struct file_context *) (long)data_to_int (dict_get (dict, "FD")));
  } else {
    len = 0;
  }

  dict_del (dict, "FD");
  dict_del (dict, "OFFSET");
  dict_del (dict, "LEN");
  dict_del (dict, "PATH");

  {
    dict_set (dict, "RET", int_to_data (len));
    dict_set (dict, "ERRNO", int_to_data (errno));
    if (len > 0)
      dict_set (dict, "BUF", bin_to_data (data, len));
    else
      dict_set (dict, "BUF", bin_to_data (" ", 1));      
  }

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int32_t 
glusterfsd_readdir (struct sock_private *sock_priv)
{
  int32_t ret = 0;

  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_readdir: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();

  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_readdir: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int8_t *buf = xl->fops->readdir (xl,
				 data_to_str (dict_get (dict, "PATH")),
				 data_to_int (dict_get (dict, "OFFSET")));
  
  dict_del (dict, "PATH");
  dict_del (dict, "OFFSET");

  if (buf) {
    dict_set (dict, "BUF", str_to_data (buf));
  } else {
    ret = -1;
  }
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  if (buf)
    free (buf);
  return 0;
}

int32_t 
glusterfsd_readlink (struct sock_private *sock_priv)
{

  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_readlink: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();

  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_readlink: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);

  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int8_t buf[PATH_MAX];
  int8_t *data = data_to_str (dict_get (dict, "PATH"));
  int32_t len = data_to_int (dict_get (dict, "LEN"));

  if (len >= PATH_MAX)
    len = PATH_MAX - 1;

  int32_t ret = xl->fops->readlink (xl, data, buf, len);

  dict_del (dict, "LEN");

  if (ret > 0) {
    dict_set (dict, "RET", int_to_data (ret));
    dict_set (dict, "ERRNO", int_to_data (errno));
    dict_set (dict, "PATH", bin_to_data (buf, ret));
  } else {
    dict_del (dict, "PATH");

    dict_set (dict, "RET", int_to_data (ret));
    dict_set (dict, "ERRNO", int_to_data (errno));
  }

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int32_t 
glusterfsd_mknod (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_mknod: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();

  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_mknod: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int32_t ret = xl->fops->mknod (xl,
			     data_to_bin (dict_get (dict, "PATH")),
			     data_to_int (dict_get (dict, "MODE")),
			     data_to_int (dict_get (dict, "DEV")),
			     data_to_int (dict_get (dict, "UID")),
			     data_to_int (dict_get (dict, "GID")));

  dict_del (dict, "PATH");
  dict_del (dict, "MODE");
  dict_del (dict, "DEV");
  dict_del (dict, "UID");
  dict_del (dict, "GID");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}


int32_t 
glusterfsd_mkdir (struct sock_private *sock_priv)
{

  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_mkdir: invalid argument");
    return -1;
  }
    
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();

  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_mkdir: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int32_t ret = xl->fops->mkdir (xl,
			     data_to_bin (dict_get (dict, "PATH")),
			     data_to_int (dict_get (dict, "MODE")),
			     data_to_int (dict_get (dict, "UID")),
			     data_to_int (dict_get (dict, "GID")));

  dict_del (dict, "MODE");
  dict_del (dict, "UID");
  dict_del (dict, "GID");
  dict_del (dict, "PATH");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int32_t 
glusterfsd_unlink (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_unlink: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();

  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_unlink: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;

  struct xlator *xl = sock_priv->xl;
  int32_t ret = xl->fops->unlink (xl, data_to_bin (dict_get (dict, "PATH")));

  dict_del (dict, "PATH");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}


int32_t 
glusterfsd_chmod (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_chmod: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();

  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_chmod: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int32_t ret = xl->fops->chmod (xl,
			     data_to_bin (dict_get (dict, "PATH")),
			     data_to_int (dict_get (dict, "MODE")));

  dict_del (dict, "MODE");
  dict_del (dict, "PATH");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}


int32_t 
glusterfsd_chown (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_chown: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_chown: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  
  int32_t ret = xl->fops->chown (xl,
			     data_to_bin (dict_get (dict, "PATH")),
			     data_to_int (dict_get (dict, "UID")),
			     data_to_int (dict_get (dict, "GID")));

  dict_del (dict, "UID");
  dict_del (dict, "GID");
  dict_del (dict, "PATH");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int32_t 
glusterfsd_truncate (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_truncate: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_truncate: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  
  int32_t ret = xl->fops->truncate (xl,
				data_to_bin (dict_get (dict, "PATH")),
				data_to_int (dict_get (dict, "OFFSET")));

  dict_del (dict, "PATH");
  dict_del (dict, "OFFSET");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int32_t 
glusterfsd_ftruncate (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_ftruncate: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_ftruncate: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int32_t ret = xl->fops->ftruncate (xl,
				 data_to_bin (dict_get (dict, "PATH")),
				 data_to_int (dict_get (dict, "OFFSET")),
				 (struct file_context *) (long)data_to_int (dict_get (dict, "FD")));

  dict_del (dict, "OFFSET");
  dict_del (dict, "FD");
  dict_del (dict, "PATH");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int32_t 
glusterfsd_utime (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_utime: invalid argument");
    return -1;
  }

  struct utimbuf  buf;
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_utime: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  
  buf.actime = data_to_int (dict_get (dict, "ACTIME"));
  buf.modtime = data_to_int (dict_get (dict, "MODTIME"));

  int32_t ret = xl->fops->utime (xl,
			     data_to_bin (dict_get (dict, "PATH")),
			     &buf);

  dict_del (dict, "ACTIME");
  dict_del (dict, "MODTIME");
  dict_del (dict, "PATH");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}


int32_t 
glusterfsd_rmdir (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_rmdir: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_rmdir: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int32_t ret = xl->fops->rmdir (xl, data_to_bin (dict_get (dict, "PATH")));

  dict_del (dict, "PATH");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int32_t 
glusterfsd_symlink (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_symlink: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_symlink: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int32_t ret = xl->fops->symlink (xl,
			       data_to_bin (dict_get (dict, "PATH")),
			       data_to_bin (dict_get (dict, "BUF")),
			       data_to_int (dict_get (dict, "UID")),
			       data_to_int (dict_get (dict, "GID")));

  dict_del (dict, "UID");
  dict_del (dict, "GID");
  dict_del (dict, "PATH");
  dict_del (dict, "BUF");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int32_t 
glusterfsd_rename (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_rename: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_rename: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int32_t ret = xl->fops->rename (xl,
			      data_to_bin (dict_get (dict, "PATH")),
			      data_to_bin (dict_get (dict, "BUF")),
			      data_to_int (dict_get (dict, "UID")),
			      data_to_int (dict_get (dict, "GID")));

  dict_del (dict, "UID");
  dict_del (dict, "GID");
  dict_del (dict, "PATH");
  dict_del (dict, "BUF");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int32_t 
glusterfsd_link (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_link: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_link: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int32_t ret = xl->fops->link (xl,
			    data_to_bin (dict_get (dict, "PATH")),
			    data_to_bin (dict_get (dict, "BUF")),
			    data_to_int (dict_get (dict, "UID")),
			    data_to_int (dict_get (dict, "GID")));

  dict_del (dict, "PATH");
  dict_del (dict, "UID");
  dict_del (dict, "GID");
  dict_del (dict, "BUF");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int32_t 
glusterfsd_getattr (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_getattr: invalid argument");
    return -1;
  }

  struct stat stbuf;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_getattr: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);

  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int8_t buffer[256] = {0,};
  int32_t ret = xl->fops->getattr (xl,
			       data_to_bin (dict_get (dict, "PATH")),
			       &stbuf);

  dict_del (dict, "PATH");

  {
    uint64_t dev = stbuf.st_dev;
    uint64_t ino = stbuf.st_ino;
    uint32_t mode = stbuf.st_mode;
    uint32_t nlink = stbuf.st_nlink;
    uint32_t uid = stbuf.st_uid;
    uint32_t gid = stbuf.st_gid;
    uint64_t rdev = stbuf.st_rdev;
    uint64_t size = stbuf.st_size;
    uint32_t blksize = stbuf.st_blksize;
    uint64_t blocks = stbuf.st_blocks;
    uint32_t atime = stbuf.st_atime;
    uint32_t atime_nsec = stbuf.st_atim.tv_nsec;
    uint32_t mtime = stbuf.st_mtime;
    uint32_t mtime_nsec = stbuf.st_mtim.tv_nsec;
    uint32_t ctime = stbuf.st_ctime;
    uint32_t ctime_nsec = stbuf.st_ctim.tv_nsec;

    // convert stat structure to ASCII values (solving endian problem)
    sprintf (buffer, GF_STAT_PRINT_FMT_STR,
	     dev,
	     ino,
	     mode,
	     nlink,
	     uid,
	     gid,
	     rdev,
	     size,
	     blksize,
	     blocks,
	     atime,
	     atime_nsec,
	     mtime,
	     mtime_nsec,
	     ctime,
	     ctime_nsec);
  }

  dict_set (dict, "BUF", str_to_data (buffer));
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  return 0;
}

int32_t 
glusterfsd_statfs (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_statfs: invalid argument");
    return -1;
  }

  struct statvfs stbuf;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_statfs: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int32_t ret = xl->fops->statfs (xl,
			      data_to_bin (dict_get (dict, "PATH")),
			      &stbuf);

  dict_del (dict, "PATH");
  
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  if (ret == 0) {
    int8_t buffer[256] = {0,};

    uint32_t bsize = stbuf.f_bsize;
    uint32_t frsize = stbuf.f_frsize;
    uint64_t blocks = stbuf.f_blocks;
    uint64_t bfree = stbuf.f_bfree;
    uint64_t bavail = stbuf.f_bavail;
    uint64_t files = stbuf.f_files;
    uint64_t ffree = stbuf.f_ffree;
    uint64_t favail = stbuf.f_favail;
    uint32_t fsid = stbuf.f_fsid;
    uint32_t flag = stbuf.f_flag;
    uint32_t namemax = stbuf.f_namemax;

    sprintf (buffer, GF_STATFS_PRINT_FMT_STR,
	     bsize,
	     frsize,
	     blocks,
	     bfree,
	     bavail,
	     files,
	     ffree,
	     favail,
	     fsid,
	     flag,
	     namemax);

    dict_set (dict, "BUF", str_to_data (buffer));
  }

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  return 0;
}

int32_t 
glusterfsd_setxattr (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_setxattr: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_setxattr: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int32_t ret = xl->fops->setxattr (xl,
				data_to_str (dict_get (dict, "PATH")),
				data_to_str (dict_get (dict, "BUF")),
				data_to_str (dict_get (dict, "FD")), //reused
				data_to_int (dict_get (dict, "COUNT")),
				data_to_int (dict_get (dict, "FLAGS")));

  dict_del (dict, "PATH");
  dict_del (dict, "UID");
  dict_del (dict, "COUNT");
  dict_del (dict, "BUF");
  dict_del (dict, "FLAGS");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  return 0;
}

int32_t 
glusterfsd_getxattr (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_getxattr: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_getxattr: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int32_t size = data_to_int (dict_get (dict, "COUNT"));
  int8_t *buf = calloc (1, size);
  int32_t ret = xl->fops->getxattr (xl,
				data_to_str (dict_get (dict, "PATH")),
				data_to_str (dict_get (dict, "BUF")),
				buf,
				size);

  dict_del (dict, "PATH");
  dict_del (dict, "COUNT");

  dict_set (dict, "BUF", str_to_data (buf));
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  free (buf);
  return 0;
}

int32_t 
glusterfsd_removexattr (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_removexattr: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_removexattr: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int32_t ret = xl->fops->removexattr (xl,
				   data_to_bin (dict_get (dict, "PATH")),
				   data_to_bin (dict_get (dict, "BUF")));

  dict_del (dict, "PATH");
  dict_del (dict, "BUF");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  return 0;
}

int32_t 
glusterfsd_listxattr (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_listxattr: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_listxattr: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int8_t *list = calloc (1, 4096);

  /* listgetxaatr prototype says 3rd arg is 'const int8_t *', arg-3 passed here is int8_t ** */
  int32_t ret = xl->fops->listxattr (xl,
				 (int8_t *)data_to_bin (dict_get (dict, "PATH")),
				 list,
				 (size_t)data_to_bin (dict_get (dict, "COUNT")));

  dict_del (dict, "PATH");
  dict_del (dict, "COUNT");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));
  dict_set (dict, "BUF", bin_to_data (list, ret));

  free (list);

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  return 0;
}

int32_t 
glusterfsd_opendir (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_opendir: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_opendir: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int32_t ret = xl->fops->opendir (xl,
			       data_to_bin (dict_get (dict, "PATH")),
			       (struct file_context *)(long)data_to_int (dict_get (dict, "FD")));

  dict_del (dict, "PATH");
  dict_del (dict, "FD");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  return 0;
}

int32_t 
glusterfsd_releasedir (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_releasedir: invalid argument");
    return -1;
  }

  return 0;
}

int32_t 
glusterfsd_fsyncdir (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_fsyncdir: invalid argument");
    return -1;
  }

  return 0;
}

int32_t 
glusterfsd_init (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_init: invalid argument");
    return -1;
  }

  return 0;
}

int32_t 
glusterfsd_destroy (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_destroy: invalid argument");
    return -1;
  }

  return 0;
}

int32_t 
glusterfsd_access (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_access: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_access: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int32_t ret = xl->fops->access (xl,
			      data_to_bin (dict_get (dict, "PATH")),
			      data_to_int (dict_get (dict, "MODE")));

  dict_del (dict, "PATH");
  dict_del (dict, "MODE");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  return 0;
}

int32_t 
glusterfsd_create (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_create: invalid argument");
    return -1;
  }

  return 0;
}

int32_t 
glusterfsd_fgetattr (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_fgetattr: invalid argument");
    return -1;
  }

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_fgetattr: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  struct stat stbuf;
  int8_t buffer[256] = {0,};
  int32_t ret = xl->fops->fgetattr (xl,
				data_to_bin (dict_get (dict, "PATH")),
				&stbuf,
				(struct file_context *)(long)data_to_int (dict_get (dict, "FD")));

  dict_del (dict, "PATH");
  dict_del (dict, "FD");

  {
    uint64_t dev = stbuf.st_dev;
    uint64_t ino = stbuf.st_ino;
    uint32_t mode = stbuf.st_mode;
    uint32_t nlink = stbuf.st_nlink;
    uint32_t uid = stbuf.st_uid;
    uint32_t gid = stbuf.st_gid;
    uint64_t rdev = stbuf.st_rdev;
    uint64_t size = stbuf.st_size;
    uint32_t blksize = stbuf.st_blksize;
    uint64_t blocks = stbuf.st_blocks;
    uint32_t atime = stbuf.st_atime;
    uint32_t atime_nsec = stbuf.st_atim.tv_nsec;
    uint32_t mtime = stbuf.st_mtime;
    uint32_t mtime_nsec = stbuf.st_mtim.tv_nsec;
    uint32_t ctime = stbuf.st_ctime;
    uint32_t ctime_nsec = stbuf.st_ctim.tv_nsec;

    // convert stat structure to ASCII values (solving endian problem)
    sprintf (buffer, GF_STAT_PRINT_FMT_STR,
	     dev,
	     ino,
	     mode,
	     nlink,
	     uid,
	     gid,
	     rdev,
	     size,
	     blksize,
	     blocks,
	     atime,
	     atime_nsec,
	     mtime,
	     mtime_nsec,
	     ctime,
	     ctime_nsec);
  }

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));
  dict_set (dict, "BUF", str_to_data (buffer));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  return 0;
}

int32_t 
glusterfsd_bulk_getattr (struct sock_private *sock_priv)
{
  if (!sock_priv) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->glusterfsd_bulk_getattr: invalid argument");
    return -1;
  }

  struct bulk_stat bstbuf = {NULL,};
  struct bulk_stat *curr = NULL;
  struct bulk_stat *prev = NULL;
  struct stat *stbuf = NULL;
  uint32_t nr_entries = 0;
  int32_t bwritten = 0;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  if (!dict) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd-fops.c->glusterfsd_bulk_getattr: get_new_dict() returned NULL");
    return -1;
  }

  dict_unserialize (blk->data, blk->size, &dict);
  free (blk->data);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int32_t ret = -1;
  int8_t buffer[PATH_MAX*257] = {0,};
  int8_t *buffer_ptr = NULL;
  data_t *path_data = dict_get (dict, "PATH");
  
  if (!path_data){
    gf_log ("glusterfsd", GF_LOG_ERROR, "glusterfsd-fops.c->bulk_getattr: dictionary entry for path missing\n");
    goto fail;
  }
  int8_t *path_bin = data_to_bin (path_data);
  
  if (!path_bin){
    gf_log ("glusterfsd", GF_LOG_ERROR, "glusterfsd-fops.c->bulk_getattr: getting pathname from dict failed\n");
    goto fail;
  }

  ret = xl->fops->bulk_getattr (xl, path_bin, &bstbuf);
  
  if (ret < 0){
    gf_log ("glusterfsd", GF_LOG_ERROR, "glusterfsd-fops.c->bulk_getattr: child bulk_getattr failed\n");
    goto fail;
  }
  dict_del (dict, "PATH");

  // convert bulk_stat structure to ASCII values (solving endian problem)
  buffer_ptr = buffer;
  curr = bstbuf.next;
  while (curr) {
    bwritten = 0;
    prev = curr;
    stbuf = curr->stbuf;
    nr_entries++;
    bwritten = sprintf (buffer_ptr, "%s/", curr->pathname);
    buffer_ptr += bwritten;

    {
      uint64_t dev;
      uint64_t ino;
      uint32_t mode;
      uint32_t nlink;
      uint32_t uid;
      uint32_t gid;
      uint64_t rdev;
      uint64_t size;
      uint32_t blksize;
      uint64_t blocks;
      uint32_t atime;
      uint32_t atime_nsec;
      uint32_t mtime;
      uint32_t mtime_nsec;
      uint32_t ctime;
      uint32_t ctime_nsec;

      dev = stbuf->st_dev;
      ino = stbuf->st_ino;
      mode = stbuf->st_mode;
      nlink = stbuf->st_nlink;
      uid = stbuf->st_uid;
      gid = stbuf->st_gid;
      rdev = stbuf->st_rdev;
      size = stbuf->st_size;
      blksize = stbuf->st_blksize;
      blocks = stbuf->st_blocks;
      atime = stbuf->st_atime;
      atime_nsec = stbuf->st_atim.tv_nsec;
      mtime = stbuf->st_mtime;
      mtime_nsec = stbuf->st_mtim.tv_nsec;
      ctime = stbuf->st_ctime;
      ctime_nsec = stbuf->st_ctim.tv_nsec;

      // convert stat structure to ASCII values (solving endian problem)
      bwritten = sprintf (buffer_ptr, GF_STAT_PRINT_FMT_STR,
			  dev,
			  ino,
			  mode,
			  nlink,
			  uid,
			  gid,
			  rdev,
			  size,
			  blksize,
			  blocks,
			  atime,
			  atime_nsec,
			  mtime,
			  mtime_nsec,
			  ctime,
			  ctime_nsec);
    }
  
    buffer_ptr += bwritten;
    curr = curr->next;

    free (prev->stbuf);
    free (prev->pathname);
    free (prev);
  }

  dict_set (dict, "BUF", str_to_data (buffer));
  dict_set (dict, "NR_ENTRIES", int_to_data (nr_entries));
 fail:
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  return 0;
}

int32_t 
handle_fops (glusterfsd_fn_t *gfopsd, struct sock_private *sock_priv)
{
  if (!sock_priv || !gfopsd) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "glusterfsd.c->handle_fops: invalid argument");
    return -1;
  }

  int32_t ret;
  gf_block *blk = (gf_block *) sock_priv->private;
  int32_t op = blk->op;

  ret = gfopsd[op].function (sock_priv);

  if (ret != 0) {
    gf_log ("glusterfsd", GF_LOG_ERROR, "glusterfsd-fops.c->handle_fops: terminating, errno=%d, error string is %s", errno, strerror (errno));
    return -1;
  }
  return 0;
}

