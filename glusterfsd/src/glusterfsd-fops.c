
#include "glusterfsd-fops.h"

int
glusterfsd_open (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  if (!dict)
    return -1;
  char *data = data_to_bin (dict_get (dict, DATA_PATH));
  
  int fd = open (RELATIVE(data),
		 data_to_int (dict_get (dict, DATA_FLAGS)),
		 data_to_int (dict_get (dict, DATA_MODE)));
  gprintf ("open on %s returned %d\n", (char *)data, fd);

  dict_del (dict, DATA_FLAGS);
  dict_del (dict, DATA_PATH);
  dict_del (dict, DATA_MODE);

  dict_set (dict, DATA_RET, int_to_data (fd));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));
  dict_set (dict, DATA_FD, int_to_data (fd));

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
  int fd = data_to_int (dict_get (dict, DATA_FD));
  int ret = close (fd);

  dict_del (dict, DATA_FD);
  
  dict_set (dict, DATA_ERRNO, int_to_data (errno));
  dict_set (dict, DATA_RET, int_to_data (ret));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return  0;
}

int
glusterfsd_flush (FILE *fp)
{
  int ret = 0;
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  //int fd = data_to_int (dict_get (dict, DATA_FD));
  
  //  ret = fsync (fd);
  dict_del (dict, DATA_FD);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return  0;
}


int
glusterfsd_fsync (FILE *fp)
{
  int retval;
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  int flags = data_to_int (dict_get (dict, DATA_FLAGS));
  int fd = data_to_int (dict_get (dict, DATA_FD));

  if (flags)
    retval = fdatasync (fd);
  else
    retval = fsync (fd);
  
  dict_del (dict, DATA_FD);
  dict_del (dict, DATA_FLAGS);

  dict_set (dict, DATA_ERRNO, int_to_data (errno));
  dict_set (dict, DATA_RET, int_to_data (retval));

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
  data_t *datat = dict_get (dict, DATA_BUF);
  int fd = data_to_int (dict_get (dict, DATA_FD));
  int offset = data_to_int (dict_get (dict, DATA_OFFSET));
  int len = 0;

  {
    lseek (fd, offset, SEEK_SET);
    len = write (fd, datat->data, datat->len);
  }

  dict_del (dict, DATA_OFFSET);
  dict_del (dict, DATA_BUF);
  dict_del (dict, DATA_FD);
  
  {
    dict_set (dict, DATA_RET, int_to_data (len));
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
  int fd = data_to_int (dict_get (dict, DATA_FD));
  int size = data_to_int (dict_get (dict, DATA_LEN));
  off_t offset = data_to_int (dict_get (dict, DATA_OFFSET));
  static char *data = NULL;
  static int data_len = 0;
  
  //  gprintf ("read request for %d bytes\n", size);
  if (size > 0) {
    if (size > data_len) {
      if (data)
	free (data);
      data = malloc (size * 2);
      data_len = size * 2;
    }
    lseek (fd, offset, SEEK_SET);
    len = read(fd, data, size);
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
  DIR *dir;
  struct dirent *dirent = NULL;
  int length = 0;
  int buf_len = 0;
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  char *buf = calloc (1, 1024); // #define the value
  int alloced = 1024;
  char *data = data_to_bin (dict_get (dict, DATA_PATH));

  FUNCTION_CALLED;

  /* Send the name of the dirents with '/' as seperator to the client */
  gprintf  ("readdir on %s\n", (char *)data);
  dir = opendir (RELATIVE(data));
  while ((dirent = readdir (dir))) {
    if (!dirent)
      break;
    length += strlen (dirent->d_name) + 1;
    if (length > alloced) {
      alloced = length * 2;
      buf = realloc (buf, alloced);
    }
    memcpy (&buf[buf_len], dirent->d_name, strlen (dirent->d_name) + 1);
    buf_len = length;
    buf[length - 1] = '/';
  }
  closedir (dir);

  dict_del (dict, DATA_PATH);

  {
    dict_set (dict, DATA_RET, int_to_data (0));
    dict_set (dict, DATA_ERRNO, int_to_data (errno));
    dict_set (dict, DATA_BUF, bin_to_data (buf, length));
  }

  free (buf);
  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_readlink (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  int retval;
  char buf[PATH_MAX];
  char *data = data_to_bin (dict_get (dict, DATA_PATH));
  int len = PATH_MAX - 1;
  
  retval = readlink (RELATIVE(data), buf, len);

  if (retval > 0) {
    dict_set (dict, DATA_RET, int_to_data (retval));
    dict_set (dict, DATA_ERRNO, int_to_data (errno));
    dict_set (dict, DATA_PATH, bin_to_data (buf, retval));
  } else {
    dict_del (dict, DATA_PATH);

    dict_set (dict, DATA_RET, int_to_data (retval));
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
  char *data = data_to_bin (dict_get (dict, DATA_PATH));
  int mode = data_to_int (dict_get (dict, DATA_MODE));;
  int dev = data_to_int (dict_get (dict, DATA_DEV));;
  int uid = data_to_int (dict_get (dict, DATA_UID));;
  int gid = data_to_int (dict_get (dict, DATA_GID));;
  int ret;

  ret = mknod (RELATIVE(data), mode, dev);

  if (ret == 0) {
    chown (RELATIVE(data), uid, gid);
  }

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
  int mode = data_to_int (dict_get (dict, DATA_MODE));
  int uid = data_to_int (dict_get (dict, DATA_UID));;
  int gid = data_to_int (dict_get (dict, DATA_GID));;
  char *data = data_to_bin (dict_get (dict, DATA_PATH));
  int ret = mkdir (RELATIVE(data), mode);

  if (ret == 0) {
    chown (RELATIVE(data), uid, gid);
  }
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
  char *data = data_to_bin (dict_get (dict, DATA_PATH));
  int ret = unlink (RELATIVE(data));
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
  char *data = data_to_bin (dict_get (dict, DATA_PATH));
  int mode = data_to_int (dict_get (dict, DATA_MODE));
  int ret = chmod (RELATIVE(data), mode);

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
  char *data = data_to_bin (dict_get (dict, DATA_PATH));
  int uid = data_to_int (dict_get (dict, DATA_UID));
  int gid = data_to_int (dict_get (dict, DATA_GID));
  int ret = lchown (RELATIVE(data), uid, gid);

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
  char *data = data_to_bin (dict_get (dict, DATA_PATH));
  int offset = data_to_int (dict_get (dict, DATA_OFFSET));
  int ret = truncate (RELATIVE(data), offset);

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
  int offset = data_to_int (dict_get (dict, DATA_OFFSET));
  int fd = data_to_int (dict_get (dict, DATA_FD));
  int ret = ftruncate (fd, offset);

  dict_del (dict, DATA_OFFSET);
  dict_del (dict, DATA_FD);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_utime (FILE *fp)
{
  int ret;
  struct utimbuf  buf;
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  char *data = data_to_bin (dict_get (dict, DATA_PATH));
  
  buf.actime = data_to_int (dict_get (dict, DATA_ACTIME));
  buf.modtime = data_to_int (dict_get (dict, DATA_MODTIME));

  ret = utime (RELATIVE(data), &buf);

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
  char *data = data_to_bin (dict_get (dict, DATA_PATH));
  int ret = rmdir (RELATIVE(data));

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
  char *oldpath = data_to_bin (dict_get (dict, DATA_PATH));
  char *newpath = data_to_bin (dict_get (dict, DATA_BUF));
  int uid = data_to_int (dict_get (dict, DATA_UID));;
  int gid = data_to_int (dict_get (dict, DATA_GID));;
  int ret = symlink (oldpath, newpath);

  if (ret == 0) {
    lchown (newpath, uid, gid);
  }

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
  char *oldpath = data_to_bin (dict_get (dict, DATA_PATH));
  char *newpath = data_to_bin (dict_get (dict, DATA_BUF));
  int uid = data_to_int (dict_get (dict, DATA_UID));;
  int gid = data_to_int (dict_get (dict, DATA_GID));;
  int ret = rename (oldpath, newpath);

  if (ret == 0) {
    chown (newpath, uid, gid);
  }

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
  char *oldpath = data_to_bin (dict_get (dict, DATA_PATH));
  char *newpath = data_to_bin (dict_get (dict, DATA_BUF));
  int uid = data_to_int (dict_get (dict, DATA_UID));;
  int gid = data_to_int (dict_get (dict, DATA_GID));;
  int ret = link (oldpath, newpath);

  if (ret == 0) {
    chown (newpath, uid, gid);
  }

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
  char buffer[256] = {0,};
  char *data = data_to_bin (dict_get (dict, DATA_PATH));
  int retval = lstat (RELATIVE(data), &stbuf);

  FUNCTION_CALLED;

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

  dict_set (dict, DATA_BUF, bin_to_data (buffer, strlen(buffer) + 1));
  dict_set (dict, DATA_RET, int_to_data (retval));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_statfs (FILE *fp)
{
  struct statvfs buf;
  dict_t *dict = dict_load (fp);
  if (!dict)
    return -1;
  char *data = data_to_bin (dict_get (dict, DATA_PATH));
  int retval = statvfs (RELATIVE(data), &buf);

  FUNCTION_CALLED;

  dict_del (dict, DATA_PATH);
  
  dict_set (dict, DATA_RET, int_to_data (retval));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  // FIXME : check whether this () needs ASCII convertion too..

  if (retval == 0)
    dict_set (dict, DATA_BUF, bin_to_data ((void *)&buf, sizeof (buf)));

  dict_dump (fp, dict);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_getdir (FILE *fp)
{
  return 0;
}

int
glusterfsd_setxattr (FILE *fp)
{
  return 0;
}

int
glusterfsd_getxattr (FILE *fp)
{
  return 0;
}

int
glusterfsd_removexattr (FILE *fp)
{
  return 0;
}

int
glusterfsd_opendir (FILE *fp)
{
  return 0;
}

int
glusterfsd_releasedir (FILE *fp)
{
  return 0;
}

int
glusterfsd_fsyncdir (FILE *fp)
{
  return 0;
}

int
glusterfsd_init (FILE *fp)
{
  return 0;
}

int
glusterfsd_destroy (FILE *fp)
{
  return 0;
}

int
glusterfsd_access (FILE *fp)
{
  return 0;
}

int
glusterfsd_create (FILE *fp)
{
  return 0;
}

int
glusterfsd_fgetattr (FILE *fp)
{
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
