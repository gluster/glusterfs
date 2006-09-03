
#include "glusterfsd.h"
#include <time.h>

#if __WORDSIZE == 64
# define F_L64 "%l"
#else
# define F_L64 "%ll"
#endif

int
glusterfsd_open (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);

  if (!dict)
    return -1;
  char *path = data_to_bin (dict_get (dict, "PATH"));
  struct xlator *xl = sock_priv->xl;
  struct file_ctx_list *fctxl = calloc (1, sizeof (struct file_ctx_list));
  struct file_context *ctx = calloc (1, sizeof (struct file_context));

  fctxl->ctx = ctx;
  fctxl->path = strdup (path);
  fctxl->next = (sock_priv->fctxl)->next;
  (sock_priv->fctxl)->next = fctxl;

  int ret = xl->fops->open (xl,
			    path,
			    data_to_int (dict_get (dict, "FLAGS")),
			    data_to_int (dict_get (dict, "MODE")),
			    ctx);
  
  dict_del (dict, "FLAGS");
  dict_del (dict, "PATH");
  dict_del (dict, "MODE");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));
  dict_set (dict, "FD", int_to_data (ctx));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_release (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);

  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;  
  struct file_ctx_list *trav_fctxl = sock_priv->fctxl;
  struct file_context *tmp_ctx = (struct file_context *)data_to_int (dict_get (dict, "FD"));

  while (trav_fctxl) {
    if (tmp_ctx == trav_fctxl->ctx)
      break;
    trav_fctxl = trav_fctxl->next;
  }

  if (! (trav_fctxl && trav_fctxl->ctx == tmp_ctx))
    return -1;

  trav_fctxl = sock_priv->fctxl;

  int ret = xl->fops->release (xl,
			       data_to_bin (dict_get (dict, "PATH")),
			       tmp_ctx);
  if (tmp_ctx)
    free (tmp_ctx);

  while (trav_fctxl->next) {
    if ((trav_fctxl->next)->ctx == tmp_ctx) {
      struct file_ctx_list *fcl = trav_fctxl->next;
      trav_fctxl->next = fcl->next;
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

int
glusterfsd_flush (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);

  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int ret = xl->fops->flush (xl,
			     data_to_bin (dict_get (dict, "PATH")),
			     (struct file_context *)data_to_int (dict_get (dict, "FD")));
  
  dict_del (dict, "FD");
  dict_del (dict, "PATH");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);

  return  0;
}


int
glusterfsd_fsync (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int ret = xl->fops->fsync (xl,
			     data_to_bin (dict_get (dict, "PATH")),
			     data_to_int (dict_get (dict, "FLAGS")),
			     (struct file_context *)data_to_int (dict_get (dict, "FD")));
  
  dict_del (dict, "PATH");
  dict_del (dict, "FD");
  dict_del (dict, "FLAGS");

  dict_set (dict, "ERRNO", int_to_data (errno));
  dict_set (dict, "RET", int_to_data (ret));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return  0;
}

int
glusterfsd_write (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  data_t *datat = dict_get (dict, "BUF");
  struct file_context *tmp_ctx = data_to_int (dict_get (dict, "FD"));

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

  int ret = xl->fops->write (xl,
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

int
glusterfsd_read (struct sock_private *sock_priv)
{
  int len = 0;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int size = data_to_int (dict_get (dict, "LEN"));
  static char *data = NULL;
  static int data_len = 0;

  {
    struct file_context *tmp_ctx = data_to_int (dict_get (dict, "FD"));
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
			  (struct file_context *) data_to_int (dict_get (dict, "FD")));
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

int
glusterfsd_readdir (struct sock_private *sock_priv)
{
  int ret = 0;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  char *buf = xl->fops->readdir (xl,
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

int
glusterfsd_readlink (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);

  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  char buf[PATH_MAX];
  char *data = data_to_str (dict_get (dict, "PATH"));
  int len = data_to_int (dict_get (dict, "LEN"));

  if (len >= PATH_MAX)
    len = PATH_MAX - 1;

  int ret = xl->fops->readlink (xl, data, buf, len);

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

int
glusterfsd_mknod (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int ret = xl->fops->mknod (xl,
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


int
glusterfsd_mkdir (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int ret = xl->fops->mkdir (xl,
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

int
glusterfsd_unlink (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;

  struct xlator *xl = sock_priv->xl;
  int ret = xl->fops->unlink (xl, data_to_bin (dict_get (dict, "PATH")));

  dict_del (dict, "PATH");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}


int
glusterfsd_chmod (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int ret = xl->fops->chmod (xl,
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


int
glusterfsd_chown (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  
  int ret = xl->fops->chown (xl,
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

int
glusterfsd_truncate (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  
  int ret = xl->fops->truncate (xl,
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

int
glusterfsd_ftruncate (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int ret = xl->fops->ftruncate (xl,
				 data_to_bin (dict_get (dict, "PATH")),
				 data_to_int (dict_get (dict, "OFFSET")),
				 (struct file_context *) data_to_int (dict_get (dict, "FD")));

  dict_del (dict, "OFFSET");
  dict_del (dict, "FD");
  dict_del (dict, "PATH");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int
glusterfsd_utime (struct sock_private *sock_priv)
{
  struct utimbuf  buf;
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  
  buf.actime = data_to_int (dict_get (dict, "ACTIME"));
  buf.modtime = data_to_int (dict_get (dict, "MODTIME"));

  int ret = xl->fops->utime (xl,
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


int
glusterfsd_rmdir (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int ret = xl->fops->rmdir (xl, data_to_bin (dict_get (dict, "PATH")));

  dict_del (dict, "PATH");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  
  return 0;
}

int
glusterfsd_symlink (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int ret = xl->fops->symlink (xl,
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

int
glusterfsd_rename (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int ret = xl->fops->rename (xl,
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

int
glusterfsd_link (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int ret = xl->fops->link (xl,
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

int
glusterfsd_getattr (struct sock_private *sock_priv)
{
  struct stat stbuf;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);

  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  char buffer[256] = {0,};
  int ret = xl->fops->getattr (xl,
			       data_to_bin (dict_get (dict, "PATH")),
			       &stbuf);

  printf ("return = (%d), errno = (%d)\n", ret, errno);
  dict_del (dict, "PATH");

  // convert stat structure to ASCII values (solving endian problem)
  sprintf (buffer, F_L64"x,"F_L64"x,%x,%lx,%x,%x,"F_L64"x,"F_L64"x,%lx,"F_L64"x,%lx,%lx,%lx,%lx,%lx,%lx\n",
	   stbuf.st_dev,
	   stbuf.st_ino,
	   stbuf.st_mode,
	   stbuf.st_nlink,
	   stbuf.st_uid,
	   stbuf.st_gid,
	   stbuf.st_rdev,
	   stbuf.st_size,
	   stbuf.st_blksize,
	   stbuf.st_blocks,
	   stbuf.st_atime,
	   stbuf.st_atim.tv_nsec,
	   stbuf.st_mtime,
	   stbuf.st_mtim.tv_nsec,
	   stbuf.st_ctime,
	   stbuf.st_ctim.tv_nsec);

  dict_set (dict, "BUF", str_to_data (buffer));
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_statfs (struct sock_private *sock_priv)
{
  struct statvfs stbuf;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int ret = xl->fops->statfs (xl,
			      data_to_bin (dict_get (dict, "PATH")),
			      &stbuf);

  dict_del (dict, "PATH");
  
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  if (ret == 0) {
    char buffer[256] = {0,};
    sprintf (buffer, "%lx,%lx,"F_L64"x,"F_L64"x,"F_L64"x,"F_L64"x,"F_L64"x,"F_L64"x,%lx,%lx,%lx\n",
	     stbuf.f_bsize,
	     stbuf.f_frsize,
	     stbuf.f_blocks,
	     stbuf.f_bfree,
	     stbuf.f_bavail,
	     stbuf.f_files,
	     stbuf.f_ffree,
	     stbuf.f_favail,
	     stbuf.f_fsid,
	     stbuf.f_flag,
	     stbuf.f_namemax);
    dict_set (dict, "BUF", str_to_data (buffer));
  }

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_setxattr (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int ret = xl->fops->setxattr (xl,
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

int
glusterfsd_getxattr (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int size = data_to_int (dict_get (dict, "COUNT"));
  char *buf = calloc (1, size);
  int ret = xl->fops->getxattr (xl,
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
  return 0;
}

int
glusterfsd_removexattr (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int ret = xl->fops->removexattr (xl,
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

int
glusterfsd_listxattr (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  char *list = calloc (1, 4096);

  /* listgetxaatr prototype says 3rd arg is 'const char *', arg-3 passed here is char ** */
  int ret = xl->fops->listxattr (xl,
				 (char *)data_to_bin (dict_get (dict, "PATH")),
				 &list,
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

int
glusterfsd_opendir (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int ret = xl->fops->opendir (xl,
			       data_to_bin (dict_get (dict, "PATH")),
			       (struct file_context *) data_to_int (dict_get (dict, "FD")));

  dict_del (dict, "PATH");
  dict_del (dict, "FD");

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_releasedir (struct sock_private *sock_priv)
{
  return 0;
}

int
glusterfsd_fsyncdir (struct sock_private *sock_priv)
{
  return 0;
}

int
glusterfsd_init (struct sock_private *sock_priv)
{
  return 0;
}

int
glusterfsd_destroy (struct sock_private *sock_priv)
{
  return 0;
}

int
glusterfsd_access (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int ret = xl->fops->access (xl,
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

int
glusterfsd_create (struct sock_private *sock_priv)
{
  return 0;
}

int
glusterfsd_fgetattr (struct sock_private *sock_priv)
{
  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  struct stat stbuf;
  char buffer[256] = {0,};
  int ret = xl->fops->fgetattr (xl,
				data_to_bin (dict_get (dict, "PATH")),
				&stbuf,
				(struct file_context *) data_to_int (dict_get (dict, "FD")));

  dict_del (dict, "PATH");
  dict_del (dict, "FD");

  sprintf (buffer, F_L64"x,"F_L64"x,%x,%lx,%x,%x,"F_L64"x,"F_L64"x,%lx,"F_L64"x,%lx,%lx,%lx,%lx,%lx,%lx\n",
	   stbuf.st_dev,
	   stbuf.st_ino,
	   stbuf.st_mode,
	   stbuf.st_nlink,
	   stbuf.st_uid,
	   stbuf.st_gid,
	   stbuf.st_rdev,
	   stbuf.st_size,
	   stbuf.st_blksize,
	   stbuf.st_blocks,
	   stbuf.st_atime,
	   stbuf.st_atim.tv_nsec,
	   stbuf.st_mtime,
	   stbuf.st_mtim.tv_nsec,
	   stbuf.st_ctime,
	   stbuf.st_ctim.tv_nsec);

  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));
  dict_set (dict, "BUF", str_to_data (buffer));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  return 0;
}

int 
glusterfsd_bulk_getattr (struct sock_private *sock_priv)
{
  
  struct bulk_stat *bstbuf = calloc (sizeof (struct bulk_stat), 1);
  struct bulk_stat *curr = NULL;
  struct stat *stbuf = NULL;
  unsigned int nr_entries = 0;

  gf_block *blk = (gf_block *)sock_priv->private;
  dict_t *dict = get_new_dict ();
  dict_unserialize (blk->data, blk->size, dict);
  
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  char buffer[PATH_MAX*257] = {0,};
  char *buffer_ptr = NULL;
  data_t *path_data = dict_get (dict, "PATH");
  
  if (!path_data){
    gf_log ("glusterfsd", LOG_CRITICAL, "glusterfsd-fops.c->bulk_getattr: dictionary entry for path missing\n");
    goto fail;
  }
  char *path_bin = data_to_bin (path_data);
  
  if (!path_bin){
    gf_log ("glusterfsd", LOG_CRITICAL, "glusterfsd-fops.c->bulk_getattr: getting pathname from dict failed\n");
    goto fail;
  }

  int ret = xl->fops->bulk_getattr (xl,
				    path_bin,
				    bstbuf);
  
  if (ret < 0){
    gf_log ("glusterfsd", LOG_CRITICAL, "glusterfsd-fops.c->bulk_getattr: child bulk_getattr failed\n");
    goto fail;
  }
  dict_del (dict, "PATH");

  // convert bulk_stat structure to ASCII values (solving endian problem)
  buffer_ptr = buffer;
  curr = bstbuf->next;
  while (curr) {
    struct bulk_stat *prev = curr;
    int bwritten = 0;
    stbuf = curr->stbuf;
    nr_entries++;
    bwritten = sprintf (buffer_ptr, "%s\n", curr->pathname);
    buffer_ptr += bwritten;
    bwritten = sprintf (buffer_ptr, F_L64"x,"F_L64"x,%x,%lx,%x,%x,"F_L64"x,"F_L64"x,%lx,"F_L64"x,%lx,%lx,%lx,%lx,%lx,%lx\n",
			stbuf->st_dev,
			stbuf->st_ino,
			stbuf->st_mode,
			stbuf->st_nlink,
			stbuf->st_uid,
			stbuf->st_gid,
			stbuf->st_rdev,
			stbuf->st_size,
			stbuf->st_blksize,
			stbuf->st_blocks,
			stbuf->st_atime,
			stbuf->st_atim.tv_nsec,
			stbuf->st_mtime,
			stbuf->st_mtim.tv_nsec,
			stbuf->st_ctime,
			stbuf->st_ctim.tv_nsec);
    buffer_ptr += bwritten;
    curr = curr->next;

    free (stbuf);
    free (prev->pathname);
    free (prev);
  }

  free (bstbuf);

  dict_set (dict, "BUF", str_to_data (buffer));
  dict_set (dict, "NR_ENTRIES", int_to_data (nr_entries));
 fail:
  dict_set (dict, "RET", int_to_data (ret));
  dict_set (dict, "ERRNO", int_to_data (errno));

  dict_dump (sock_priv->fd, dict, blk, OP_TYPE_FOP_REPLY);
  dict_destroy (dict);
  return 0;
}

int
handle_fops (glusterfsd_fn_t *gfopsd, struct sock_private *sock_priv)
{
  int ret;
  gf_block *blk = (gf_block *) sock_priv->private;
  int op = blk->op;

  ret = gfopsd[op].function (sock_priv);

  if (ret != 0) {
    gf_log ("glusterfsd", LOG_CRITICAL, "glusterfsd-fops.c->handle_fops: terminating, (errno=%d)\n",
	    errno);
    return -1;
  }
  return 0;
}

