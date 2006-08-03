
#include "glusterfsd-fops.h"

int
glusterfsd_open (FILE *fp)
{

  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
  struct file_context *ctx = calloc (1, sizeof (struct file_context));
  ctx->next = NULL;
  //FIXME: Make file_context linked list
  int ret = xl->fops->open (xl,
			   data_to_bin (dict_get (dict, DATA_PATH)),
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
glusterfsd_release (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
  int ret = xl->fops->release (xl,
			       data_to_bin (dict_get (dict, DATA_PATH)),
			       data_to_int (dict_get (dict, DATA_FD)));
  
  dict_del (dict, DATA_FD);
  dict_del (dict, DATA_PATH);

  dict_set (dict, DATA_ERRNO, int_to_data (errno));
  dict_set (dict, DATA_RET, int_to_data (ret));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return  0;
}

int
glusterfsd_flush (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
  int ret = xl->fops->flush (xl,
			    data_to_bin (dict_get (dict, DATA_PATH)),
			    data_to_int (dict_get (dict, DATA_FD)));
  
  dict_del (dict, DATA_FD);
  dict_del (dict, DATA_PATH);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return  0;
}


int
glusterfsd_fsync (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
  int ret = xl->fops->fsync (xl,
			    data_to_bin (dict_get (dict, DATA_PATH)),
			    data_to_int (dict_get (dict, DATA_FLAGS)),
			    data_to_int (dict_get (dict, DATA_FD)));
  
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
glusterfsd_write (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
  data_t *datat = dict_get (dict, DATA_BUF);
  int ret = xl->fops->write (xl,
			    data_to_bin (dict_get (dict, DATA_PATH)),
			    datat->data,
			    datat->len,
			    data_to_int (dict_get (dict, DATA_OFFSET)),
			    data_to_int (dict_get (dict, DATA_FD)));

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
glusterfsd_read (FILE *fp)
{
  int len = 0;
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
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
			 data_to_int (dict_get (dict, DATA_FD)));
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
    if (len)
      dict_set (dict, DATA_BUF, bin_to_data (data, len));
  }

  dict_dump (fp, dict);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_readdir (FILE *fp)
{
  int ret = 0;
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
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
glusterfsd_readlink (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
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
glusterfsd_mknod (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();

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
glusterfsd_mkdir (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();

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
glusterfsd_unlink (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;

  struct xlator *xl = get_xlator_tree_node ();
  int ret = xl->fops->unlink (xl, data_to_bin (dict_get (dict, DATA_PATH)));

  dict_del (dict, DATA_PATH);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return 0;
}


int
glusterfsd_chmod (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
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
glusterfsd_chown (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
  
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
glusterfsd_truncate (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
  
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
glusterfsd_ftruncate (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
  int ret = xl->fops->ftruncate (xl,
				data_to_bin (dict_get (dict, DATA_PATH)),
				data_to_int (dict_get (dict, DATA_OFFSET)),
				data_to_int (dict_get (dict, DATA_FD)));

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
glusterfsd_utime (FILE *fp)
{
  struct utimbuf  buf;
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
  
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
glusterfsd_rmdir (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
  int ret = xl->fops->rmdir (xl, data_to_bin (dict_get (dict, DATA_PATH)));

  dict_del (dict, DATA_PATH);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_symlink (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();

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
glusterfsd_rename (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();

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
glusterfsd_link (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();

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
glusterfsd_getattr (FILE *fp)
{
  struct stat stbuf;
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
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
glusterfsd_statfs (FILE *fp)
{
  struct statvfs stbuf;
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
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
glusterfsd_setxattr (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();

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
glusterfsd_getxattr (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
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
glusterfsd_removexattr (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();

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
glusterfsd_listxattr (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();

  char *list = calloc (1, 4096);

  int ret = xl->fops->listxattr (xl,
				 data_to_bin (dict_get (dict, DATA_PATH)),
				 &list,
				 data_to_bin (dict_get (dict, DATA_COUNT)));

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
glusterfsd_opendir (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();

  int ret = xl->fops->opendir (xl,
			       data_to_bin (dict_get (dict, DATA_PATH)),
			       data_to_int (dict_get (dict, DATA_FD)));

  dict_del (dict, DATA_PATH);
  dict_del (dict, DATA_FD);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_releasedir (FILE *fp)
{
  struct xlator *xl = get_xlator_tree_node ();
  return 0;
}

int
glusterfsd_fsyncdir (FILE *fp)
{
  struct xlator *xl = get_xlator_tree_node ();
  return 0;
}

int
glusterfsd_init (FILE *fp)
{
  struct xlator *xl = get_xlator_tree_node ();
  return 0;
}

int
glusterfsd_destroy (FILE *fp)
{
  struct xlator *xl = get_xlator_tree_node ();
  return 0;
}

int
glusterfsd_access (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();

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
glusterfsd_create (FILE *fp)
{
  struct xlator *xl = get_xlator_tree_node ();
  return 0;
}

int
glusterfsd_fgetattr (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  struct xlator *xl = get_xlator_tree_node ();
  struct stat stbuf;
  char buffer[256] = {0,};
  int ret = xl->fops->fgetattr (xl,
				data_to_bin (dict_get (dict, DATA_PATH)),
				&stbuf,
				data_to_int (dict_get (dict, DATA_FD)));

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
server_fs_loop (glusterfsd_fops_t *gfsd, FILE *fp)
{
  int ret;
  int operation;
  
  if (fscanf (fp, "%d\n", &operation) == 0)
    return -1;

  if ((operation < 0) || (operation > 34))
    return -1;

  ret = gfsd[operation].function (fp);
  fflush (fp);

  if (ret != 0) {
    gprintf ("%s: terminating, (errno=%d)\n", __FUNCTION__,
	     errno);
    return -1;
  }
  return 0;
}
