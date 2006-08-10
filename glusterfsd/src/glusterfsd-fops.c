
#include "glusterfsd.h"

int
glusterfsd_open (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  char *path = data_to_bin (dict_get (dict, DATA_PATH));
  struct xlator *xl = sock_priv->xl;
  struct file_ctx_list *fctxl = calloc (1, sizeof (struct file_ctx_list));
  struct file_context *ctx = calloc (1, sizeof (struct file_context));

  fctxl->ctx = ctx;
  strcpy(fctxl->path, path);
  fctxl->next = (sock_priv->fctxl)->next;
  (sock_priv->fctxl)->next = fctxl;

  int ret = xl->fops->open (xl,
			    path,
			    data_to_int (dict_get (dict, DATA_FLAGS)),
			    data_to_int (dict_get (dict, DATA_MODE)),
			    ctx);
  
  dict_del (dict, DATA_FLAGS);
  dict_del (dict, DATA_PATH);
  dict_del (dict, DATA_MODE);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));
  dict_set (dict, DATA_FD, int_to_data ((int)ctx));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_release (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;  
  struct file_ctx_list *trav_fctxl = sock_priv->fctxl;
  struct file_context *tmp_ctx = (struct file_context *)data_to_int (dict_get (dict, DATA_FD));

  int ret = xl->fops->release (xl,
			       data_to_bin (dict_get (dict, DATA_PATH)),
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

  dict_del (dict, DATA_FD);
  dict_del (dict, DATA_PATH);

  dict_set (dict, DATA_ERRNO, int_to_data (errno));
  dict_set (dict, DATA_RET, int_to_data (ret));
  
  dict_dump (fp, dict);
  dict_destroy (dict);

  return  0;
}

int
glusterfsd_flush (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int ret = xl->fops->flush (xl,
			    data_to_bin (dict_get (dict, DATA_PATH)),
			    (struct file_context *)data_to_int (dict_get (dict, DATA_FD)));
  
  dict_del (dict, DATA_FD);
  dict_del (dict, DATA_PATH);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return  0;
}


int
glusterfsd_fsync (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int ret = xl->fops->fsync (xl,
			    data_to_bin (dict_get (dict, DATA_PATH)),
			    data_to_int (dict_get (dict, DATA_FLAGS)),
			    (struct file_context *)data_to_int (dict_get (dict, DATA_FD)));
  
  dict_del (dict, DATA_PATH);
  dict_del (dict, DATA_FD);
  dict_del (dict, DATA_FLAGS);

  dict_set (dict, DATA_ERRNO, int_to_data (errno));
  dict_set (dict, DATA_RET, int_to_data (ret));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return  0;
}

int
glusterfsd_write (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  data_t *datat = dict_get (dict, DATA_BUF);
  int ret = xl->fops->write (xl,
			    data_to_bin (dict_get (dict, DATA_PATH)),
			    datat->data,
			    datat->len,
			    data_to_int (dict_get (dict, DATA_OFFSET)),
			    (struct file_context *) data_to_int (dict_get (dict, DATA_FD)));

  dict_del (dict, DATA_PATH);
  dict_del (dict, DATA_OFFSET);
  dict_del (dict, DATA_BUF);
  dict_del (dict, DATA_FD);
  
  {
    dict_set (dict, DATA_RET, int_to_data (ret));
    dict_set (dict, DATA_ERRNO, int_to_data (errno));
  }
  
  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_read (struct sock_private *sock_priv)
{
  int len = 0;
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int size = data_to_int (dict_get (dict, DATA_LEN));
  static char *data = NULL;
  static int data_len = 0;
  
  if (size > 0) {
    if (size > data_len) {
      if (data)
	free (data);
      data = malloc (size * 2);
      data_len = size * 2;
    }
    len = xl->fops->read (xl,
			 data_to_bin (dict_get (dict, DATA_PATH)),
			 data,
			 size,
			 data_to_int (dict_get (dict, DATA_OFFSET)),
			 (struct file_context *) data_to_int (dict_get (dict, DATA_FD)));
  } else {
    len = 0;
  }

  dict_del (dict, DATA_FD);
  dict_del (dict, DATA_OFFSET);
  dict_del (dict, DATA_LEN);
  dict_del (dict, DATA_PATH);

  {
    dict_set (dict, DATA_RET, int_to_data (len));
    dict_set (dict, DATA_ERRNO, int_to_data (errno));
    if (len > 0)
      dict_set (dict, DATA_BUF, bin_to_data (data, len));
    else
      dict_set (dict, DATA_BUF, bin_to_data (" ", 1));      
  }

  dict_dump (fp, dict);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_readdir (struct sock_private *sock_priv)
{
  int ret = 0;
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  char *buf = xl->fops->readdir (xl,
				 data_to_str (dict_get (dict, DATA_PATH)),
				 data_to_int (dict_get (dict, DATA_OFFSET)));
  
  dict_del (dict, DATA_PATH);
  dict_del (dict, DATA_OFFSET);

  if (buf) {
    dict_set (dict, DATA_BUF, str_to_data (buf));
  } else {
    ret = -1;
  }
  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);
  if (buf)
    free (buf);
  return 0;
}

int
glusterfsd_readlink (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  char buf[PATH_MAX];
  char *data = data_to_str (dict_get (dict, DATA_PATH));
  int len = data_to_int (dict_get (dict, DATA_LEN));

  if (len >= PATH_MAX)
    len = PATH_MAX - 1;

  int ret = xl->fops->readlink (xl, data, buf, len);

  dict_del (dict, DATA_LEN);

  if (ret > 0) {
    dict_set (dict, DATA_RET, int_to_data (ret));
    dict_set (dict, DATA_ERRNO, int_to_data (errno));
    dict_set (dict, DATA_PATH, bin_to_data (buf, ret));
  } else {
    dict_del (dict, DATA_PATH);

    dict_set (dict, DATA_RET, int_to_data (ret));
    dict_set (dict, DATA_ERRNO, int_to_data (errno));
  }

  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_mknod (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int ret = xl->fops->mknod (xl,
			    data_to_bin (dict_get (dict, DATA_PATH)),
			    data_to_int (dict_get (dict, DATA_MODE)),
			    data_to_int (dict_get (dict, DATA_DEV)),
			    data_to_int (dict_get (dict, DATA_UID)),
			    data_to_int (dict_get (dict, DATA_GID)));

  dict_del (dict, DATA_PATH);
  dict_del (dict, DATA_MODE);
  dict_del (dict, DATA_DEV);
  dict_del (dict, DATA_UID);
  dict_del (dict, DATA_GID);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
}


int
glusterfsd_mkdir (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int ret = xl->fops->mkdir (xl,
			    data_to_bin (dict_get (dict, DATA_PATH)),
			    data_to_int (dict_get (dict, DATA_MODE)),
			    data_to_int (dict_get (dict, DATA_UID)),
			    data_to_int (dict_get (dict, DATA_GID)));

  dict_del (dict, DATA_MODE);
  dict_del (dict, DATA_UID);
  dict_del (dict, DATA_GID);
  dict_del (dict, DATA_PATH);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_unlink (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;

  struct xlator *xl = sock_priv->xl;
  int ret = xl->fops->unlink (xl, data_to_bin (dict_get (dict, DATA_PATH)));

  dict_del (dict, DATA_PATH);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return 0;
}


int
glusterfsd_chmod (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int ret = xl->fops->chmod (xl,
			    data_to_bin (dict_get (dict, DATA_PATH)),
			    data_to_int (dict_get (dict, DATA_MODE)));

  dict_del (dict, DATA_MODE);
  dict_del (dict, DATA_PATH);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return 0;
}


int
glusterfsd_chown (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  
  int ret = xl->fops->chown (xl,
			    data_to_bin (dict_get (dict, DATA_PATH)),
			    data_to_int (dict_get (dict, DATA_UID)),
			    data_to_int (dict_get (dict, DATA_GID)));

  dict_del (dict, DATA_UID);
  dict_del (dict, DATA_GID);
  dict_del (dict, DATA_PATH);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_truncate (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  
  int ret = xl->fops->truncate (xl,
			       data_to_bin (dict_get (dict, DATA_PATH)),
			       data_to_int (dict_get (dict, DATA_OFFSET)));

  dict_del (dict, DATA_PATH);
  dict_del (dict, DATA_OFFSET);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_ftruncate (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int ret = xl->fops->ftruncate (xl,
				data_to_bin (dict_get (dict, DATA_PATH)),
				data_to_int (dict_get (dict, DATA_OFFSET)),
				(struct file_context *) data_to_int (dict_get (dict, DATA_FD)));

  dict_del (dict, DATA_OFFSET);
  dict_del (dict, DATA_FD);
  dict_del (dict, DATA_PATH);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_utime (struct sock_private *sock_priv)
{
  struct utimbuf  buf;
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  
  buf.actime = data_to_int (dict_get (dict, DATA_ACTIME));
  buf.modtime = data_to_int (dict_get (dict, DATA_MODTIME));

  int ret = xl->fops->utime (xl,
			    data_to_bin (dict_get (dict, DATA_PATH)),
			    &buf);

  dict_del (dict, DATA_ACTIME);
  dict_del (dict, DATA_MODTIME);
  dict_del (dict, DATA_PATH);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return 0;
}


int
glusterfsd_rmdir (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int ret = xl->fops->rmdir (xl, data_to_bin (dict_get (dict, DATA_PATH)));

  dict_del (dict, DATA_PATH);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_symlink (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int ret = xl->fops->symlink (xl,
			      data_to_bin (dict_get (dict, DATA_PATH)),
			      data_to_bin (dict_get (dict, DATA_BUF)),
			      data_to_int (dict_get (dict, DATA_UID)),
			      data_to_int (dict_get (dict, DATA_GID)));

  dict_del (dict, DATA_UID);
  dict_del (dict, DATA_GID);
  dict_del (dict, DATA_PATH);
  dict_del (dict, DATA_BUF);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_rename (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int ret = xl->fops->rename (xl,
			     data_to_bin (dict_get (dict, DATA_PATH)),
			     data_to_bin (dict_get (dict, DATA_BUF)),
			     data_to_int (dict_get (dict, DATA_UID)),
			     data_to_int (dict_get (dict, DATA_GID)));

  dict_del (dict, DATA_UID);
  dict_del (dict, DATA_GID);
  dict_del (dict, DATA_PATH);
  dict_del (dict, DATA_BUF);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return 0;
}


int
glusterfsd_link (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int ret = xl->fops->link (xl,
			   data_to_bin (dict_get (dict, DATA_PATH)),
			   data_to_bin (dict_get (dict, DATA_BUF)),
			   data_to_int (dict_get (dict, DATA_UID)),
			   data_to_int (dict_get (dict, DATA_GID)));

  dict_del (dict, DATA_PATH);
  dict_del (dict, DATA_UID);
  dict_del (dict, DATA_GID);
  dict_del (dict, DATA_BUF);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_getattr (struct sock_private *sock_priv)
{
  struct stat stbuf;
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  char buffer[256] = {0,};
  int ret = xl->fops->getattr (xl,
			      data_to_bin (dict_get (dict, DATA_PATH)),
			      &stbuf);

  dict_del (dict, DATA_PATH);

  // convert stat structure to ASCII values (solving endian problem)
  sprintf (buffer, "%llx,%llx,%x,%x,%x,%x,%llx,%llx,%lx,%llx,%lx,%lx,%lx\n",
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
	   stbuf.st_mtime,
	   stbuf.st_ctime);

  dict_set (dict, DATA_BUF, str_to_data (buffer));
  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_statfs (struct sock_private *sock_priv)
{
  struct statvfs stbuf;
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int ret = xl->fops->statfs (xl,
			     data_to_bin (dict_get (dict, DATA_PATH)),
			     &stbuf);

  dict_del (dict, DATA_PATH);
  
  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  if (ret == 0) {
    char buffer[256] = {0,};
    sprintf (buffer, "%lx,%lx,%llx,%llx,%llx,%llx,%llx,%llx,%lx,%lx,%lx\n",
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
    dict_set (dict, DATA_BUF, str_to_data (buffer));
  }

  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_setxattr (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int ret = xl->fops->setxattr (xl,
				data_to_str (dict_get (dict, DATA_PATH)),
				data_to_str (dict_get (dict, DATA_BUF)),
				data_to_str (dict_get (dict, DATA_FD)), //reused
				data_to_int (dict_get (dict, DATA_COUNT)),
				data_to_int (dict_get (dict, DATA_FLAGS)));

  dict_del (dict, DATA_PATH);
  dict_del (dict, DATA_UID);
  dict_del (dict, DATA_COUNT);
  dict_del (dict, DATA_BUF);
  dict_del (dict, DATA_FLAGS);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_getxattr (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  int size = data_to_int (dict_get (dict, DATA_COUNT));
  char *buf = calloc (1, size);
  int ret = xl->fops->getxattr (xl,
				data_to_str (dict_get (dict, DATA_PATH)),
				data_to_str (dict_get (dict, DATA_BUF)),
				buf,
				size);

  dict_del (dict, DATA_PATH);
  dict_del (dict, DATA_COUNT);

  dict_set (dict, DATA_BUF, str_to_data (buf));
  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_removexattr (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int ret = xl->fops->removexattr (xl,
				   data_to_bin (dict_get (dict, DATA_PATH)),
				   data_to_bin (dict_get (dict, DATA_BUF)));

  dict_del (dict, DATA_PATH);
  dict_del (dict, DATA_BUF);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_listxattr (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  char *list = calloc (1, 4096);

  /* listgetxaatr prototype says 3rd arg is 'const char *', arg-3 passed here is char ** */
  int ret = xl->fops->listxattr (xl,
				 (char *)data_to_bin (dict_get (dict, DATA_PATH)),
				 &list,
				 (size_t)data_to_bin (dict_get (dict, DATA_COUNT)));

  dict_del (dict, DATA_PATH);
  dict_del (dict, DATA_COUNT);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));
  dict_set (dict, DATA_BUF, bin_to_data (list, ret));

  free (list);
  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_opendir (struct sock_private *sock_priv)
{
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int ret = xl->fops->opendir (xl,
			       data_to_bin (dict_get (dict, DATA_PATH)),
			       (struct file_context *) data_to_int (dict_get (dict, DATA_FD)));

  dict_del (dict, DATA_PATH);
  dict_del (dict, DATA_FD);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
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
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;

  int ret = xl->fops->access (xl,
			      data_to_bin (dict_get (dict, DATA_PATH)),
			      data_to_int (dict_get (dict, DATA_MODE)));

  dict_del (dict, DATA_PATH);
  dict_del (dict, DATA_MODE);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
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
  FILE *fp = sock_priv->fp;
  dict_t *dict = dict_load (fp);
  CHECK_ENDFOPS ();
  if (!dict)
    return -1;
  struct xlator *xl = sock_priv->xl;
  struct stat stbuf;
  char buffer[256] = {0,};
  int ret = xl->fops->fgetattr (xl,
				data_to_bin (dict_get (dict, DATA_PATH)),
				&stbuf,
				(struct file_context *) data_to_int (dict_get (dict, DATA_FD)));

  dict_del (dict, DATA_PATH);
  dict_del (dict, DATA_FD);

  sprintf (buffer, "%llx,%llx,%x,%x,%x,%x,%llx,%llx,%lx,%llx,%lx,%lx,%lx\n",
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
	   stbuf.st_mtime,
	   stbuf.st_ctime);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));
  dict_set (dict, DATA_BUF, str_to_data (buffer));

  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
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

  int ret = xl->fops->stats (xl, &stats);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  if (ret == 0) {
    char buffer[256] = {0,};
    sprintf (buffer, "%lx,%lx,%llx,%llx\n",
	     (long)stats.nr_files,
	     (long)stats.free_mem,
	     (long long)stats.free_disk,
	     (long long)glusterfsd_stats_nr_clients);
    dict_set (dict, DATA_BUF, str_to_data (buffer));
    printf ("stats: buf: %s\n", buffer);
  }

  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
}

int
handle_fops (glusterfsd_fn_t *gfopsd, struct sock_private *sock_priv)
{
  int ret;
  int operation;
  char readbuf[80] = {0,};
  FILE *fp = sock_priv->fp;

  if (fgets (readbuf, 80, fp) == NULL)
    return -1;
  
  operation = strtol (readbuf, NULL, 0);

  if ((operation < 0) || (operation > OP_MAXVALUE))
    return -1;

  ret = gfopsd[operation].function (sock_priv);

  if (ret != 0) {
    gprintf ("%s: terminating, (errno=%d)\n", __FUNCTION__,
	     errno);
    return -1;
  }
  return 0;
}

