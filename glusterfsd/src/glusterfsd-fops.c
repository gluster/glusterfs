
#include "glusterfsd-fops.h"

int
glusterfsd_open (FILE *fp)
{
  int fd;
  dict_t *dict = dict_load (fp);
  char *data = data_to_bin (dict_get (dict, DATA_BUF));

  fd = open (RELATIVE(data), data_to_int (dict_get (dict, DATA_FLAGS)));
  gprintf ("open on %s returned %d\n", (char *)data, fd);

  dict_del (dict, DATA_FLAGS);
  dict_del (dict, DATA_BUF);

  dict_set (dict, DATA_RET, int_to_data (fd));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));
  dict_set (dict, DATA_FD, int_to_data (fd));

  dict_dump (fp, dict);
  fflush (fp);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_release (FILE *fp)
{
  int fd;
  int ret;
  dict_t *dict = dict_load (fp);

  fd = data_to_int (dict_get (dict, DATA_FD));
  dict_del (dict, DATA_FD);

  ret = close (fd);
  
  dict_set (dict, DATA_ERRNO, int_to_data (errno));
  dict_set (dict, DATA_RET, int_to_data (ret));

  dict_dump (fp, dict);
  fflush (fp);
  dict_destroy (dict);

  return  0;
}

int
glusterfsd_flush (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  //  int fd = data_to_int (dict_get (dict, DATA_FD));
  //  dict_del (dict, DATA_FD);
  
  //  ret = fsync (fd);
  dict_set (dict, DATA_RET, int_to_data (0));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  fflush (fp);
  dict_destroy (dict);

  return  0;
}


int
glusterfsd_fsync (FILE *fp)
{
  int retval;
  dict_t *dict = dict_load (fp);
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
  fflush (fp);
  dict_destroy (dict);

  return  0;
}

int
glusterfsd_write (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  char *data = data_to_bin (dict_get (dict, DATA_BUF));
  int len = data_to_int (dict_get (dict, DATA_LEN));
  int fd = data_to_int (dict_get (dict, DATA_FD));

  len = write (fd, data, len);

  dict_del (dict, DATA_BUF);
  dict_del (dict, DATA_LEN);
  dict_del (dict, DATA_FD);

  dict_set (dict, DATA_RET, int_to_data (len));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  fflush (fp);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_read (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  int fd = data_to_int (dict_get (dict, DATA_FD));
  int len = 0;
  int size = data_to_int (dict_get (dict, DATA_LEN));
  off_t offset = data_to_int (dict_get (dict, DATA_OFFSET));
  static char *data = NULL;
  int data_len = 0;
  
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

  dict_set (dict, DATA_LEN, int_to_data (len));
  dict_set (dict, DATA_RET, int_to_data (len));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));
  dict_set (dict, DATA_BUF, bin_to_data (data, len));

  dict_dump (fp, dict);
  fflush (fp);
  dict_destroy (dict);

  return 0;
}


int
glusterfsd_readdir (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  int retval;
  DIR *dir;
  int length = 0;
  struct dirent *dirent;
  static struct dirent *dirents = NULL;
  int alloced = 0;
  char *data = data_to_bin (dict_get (dict, DATA_BUF));

  FUNCTION_CALLED;

  gprintf  ("readdir on %s\n", (char *)data);
  dir = opendir (RELATIVE(data));
  while ((dirent = readdir (dir))) {
    length++;
    if (length > alloced) {
      alloced = length * 2;
      if (!dirents)
	dirents = (void *) malloc (alloced * sizeof (struct dirent));
      else
	dirents = (void *) realloc ((void *)dirents, 
				    alloced * sizeof (struct dirent));
    }
    memcpy ((void *)&dirents[length-1], dirent, sizeof (struct dirent));
  }
  closedir (dir);

  dict_set (dict, DATA_RET, int_to_data (0));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));
  dict_set (dict, DATA_BUF, 
	    bin_to_data ((void *)dirents, sizeof (*dirent) * length));
  
  dict_dump (fp, dict);
  fflush (fp);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_readlink (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  int retval;
  int len;
  char buf[PATH_MAX];
  char *data = data_to_bin (dict_get (dict, DATA_BUF));

  if (len >= PATH_MAX)
    len = PATH_MAX - 1;

  retval = readlink (RELATIVE(data), buf, len);

  gprintf ("%s: on %s\n", __FUNCTION__, (char *)data);

  if (retval > 0) {
    dict_set (dict, DATA_RET, int_to_data (retval));
    dict_set (dict, DATA_ERRNO, int_to_data (errno));
    dict_set (dict, DATA_BUF, bin_to_data (buf, retval));
  } else {
    dict_set (dict, DATA_RET, int_to_data (retval));
    dict_set (dict, DATA_ERRNO, int_to_data (errno));
    dict_del (dict, DATA_BUF);
  }

  dict_dump (fp, dict);
  fflush (fp);
  dict_destroy (dict);
  return 0;
}

int
glusterfsd_mknod (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  char *data = data_to_bin (dict_get (dict, DATA_BUF));
  int mode = data_to_int (dict_get (dict, DATA_MODE));;
  int dev = data_to_int (dict_get (dict, DATA_DEV));;
  int uid = data_to_int (dict_get (dict, DATA_UID));;
  int gid = data_to_int (dict_get (dict, DATA_GID));;
  int ret;

  ret = mknod (RELATIVE(data), mode, dev);

  if (ret == 0) {
    chown (RELATIVE(data), uid, gid);
  }
  dict_del (dict, DATA_BUF);
  dict_del (dict, DATA_MODE);
  dict_del (dict, DATA_DEV);
  dict_del (dict, DATA_UID);
  dict_del (dict, DATA_GID);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);
  fflush (fp);
  return 0;
}


int
glusterfsd_mkdir (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  int mode = data_to_int (dict_get (dict, DATA_MODE));
  int uid = data_to_int (dict_get (dict, DATA_UID));;
  int gid = data_to_int (dict_get (dict, DATA_GID));;
  char *data = data_to_bin (dict_get (dict, DATA_BUF));
  int ret = mkdir (RELATIVE(data), mode);

  if (ret == 0) {
    chown (RELATIVE(data), uid, gid);
  }
  dict_del (dict, DATA_MODE);
  dict_del (dict, DATA_UID);
  dict_del (dict, DATA_GID);
  dict_del (dict, DATA_BUF);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  fflush (fp);

  return 0;
}

int
glusterfsd_unlink (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  char *data = data_to_bin (dict_get (dict, DATA_BUF));
  int ret = unlink (RELATIVE(data));
  dict_del (dict, DATA_BUF);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  fflush (fp);
  dict_destroy (dict);

  return 0;
}


int
glusterfsd_chmod (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  int mode = data_to_int (dict_get (dict, DATA_MODE));
  char *data = data_to_bin (dict_get (dict, DATA_BUF));
  int ret = chmod (RELATIVE(data), mode);
  dict_del (dict, DATA_MODE);
  dict_del (dict, DATA_BUF);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  fflush (fp);
  dict_destroy (dict);

  return 0;
}


int
glusterfsd_chown (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  int uid = data_to_int (dict_get (dict, DATA_UID));
  int gid = data_to_int (dict_get (dict, DATA_GID));
  char *data = data_to_bin (dict_get (dict, DATA_BUF));
  int ret = lchown (RELATIVE(data), uid, gid);
  dict_del (dict, DATA_UID);
  dict_del (dict, DATA_GID);
  dict_del (dict, DATA_BUF);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  fflush (fp);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_truncate (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  int offset = data_to_int (dict_get (dict, DATA_OFFSET));
  char *data = data_to_bin (dict_get (dict, DATA_BUF));
  int ret = truncate (RELATIVE(data), offset);
  dict_del (dict, DATA_BUF);
  dict_del (dict, DATA_OFFSET);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  fflush (fp);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_ftruncate (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  int offset = data_to_int (dict_get (dict, DATA_OFFSET));
  int fd = data_to_int (dict_get (dict, DATA_FD));
  int ret;
  
  ret = ftruncate (fd, offset);
  dict_del (dict, DATA_OFFSET);
  dict_del (dict, DATA_FD);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  fflush (fp);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_utime (FILE *fp)
{
  dict_t *dict = dict_load (fp);
  struct utimbuf  buf;
  int ret;
  int actime = data_to_int (dict_get (dict, DATA_ACTIME));
  int modtime = data_to_int (dict_get (dict, DATA_MODTIME));
  char *data = data_to_bin (dict_get (dict, DATA_BUF));
  
  buf.actime = actime;
  buf.modtime = modtime;

  ret = utime (RELATIVE(data), &buf);
  dict_del (dict, DATA_ACTIME);
  dict_del (dict, DATA_MODTIME);
  dict_del (dict, DATA_BUF);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  fflush (fp);
  dict_destroy (dict);

  return 0;
}


int
glusterfsd_rmdir (FILE *fp)
{
  int ret;
  dict_t *dict = dict_load (fp);
  char *data = data_to_bin (dict_get (dict, DATA_BUF));
  
  ret = rmdir (RELATIVE(data));
  dict_del (dict, DATA_BUF);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  fflush (fp);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_symlink (FILE *fp)
{
  int ret;
  dict_t *dict = dict_load (fp);
  int len = data_to_int (dict_get (dict, DATA_LEN));
  int uid = data_to_int (dict_get (dict, DATA_UID));;
  int gid = data_to_int (dict_get (dict, DATA_GID));;
  char *data = data_to_bin (dict_get (dict, DATA_BUF));
  char *oldpath = RELATIVE(data);
  char *newpath = RELATIVE((char *)data + len);

  ret = symlink (oldpath, newpath);
  gprintf ("%s: symlink %s->%s\n", __FUNCTION__, oldpath, newpath);

  if (ret == 0) {
    lchown (newpath, uid, gid);
  }
  dict_del (dict, DATA_LEN);
  dict_del (dict, DATA_UID);
  dict_del (dict, DATA_GID);
  dict_del (dict, DATA_BUF);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  fflush (fp);
  dict_destroy (dict);

  return 0;
}

int
glusterfsd_rename (FILE *fp)
{
  int ret;
  dict_t *dict = dict_load (fp);
  int len = data_to_int (dict_get (dict, DATA_LEN));
  int uid = data_to_int (dict_get (dict, DATA_UID));;
  int gid = data_to_int (dict_get (dict, DATA_GID));;
  char *data = data_to_bin (dict_get (dict, DATA_BUF));

  char *oldpath = RELATIVE(data);
  char *newpath = RELATIVE((char *)data + len);

  ret = rename (oldpath, newpath);
  gprintf ("%s: rename %s->%s\n", __FUNCTION__, oldpath, newpath);

  if (ret == 0) {
    chown (newpath, uid, gid);
  }
  dict_del (dict, DATA_LEN);
  dict_del (dict, DATA_UID);
  dict_del (dict, DATA_GID);
  dict_del (dict, DATA_BUF);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);
  fflush (fp);

  return 0;
}


int
glusterfsd_link (FILE *fp)
{
  int ret;
  dict_t *dict = dict_load (fp);
  int len = data_to_int (dict_get (dict, DATA_LEN));
  int uid = data_to_int (dict_get (dict, DATA_UID));;
  int gid = data_to_int (dict_get (dict, DATA_GID));;
  char *data = data_to_bin (dict_get (dict, DATA_BUF));
  char *oldpath = data;
  char *newpath = RELATIVE((char *)data + len);

  ret = link (oldpath, newpath);
  gprintf ("%s: link %s->%s\n", __FUNCTION__, oldpath, newpath);

  if (ret == 0) {
    chown (newpath, uid, gid);
  }
  dict_del (dict, DATA_LEN);
  dict_del (dict, DATA_UID);
  dict_del (dict, DATA_GID);
  dict_del (dict, DATA_BUF);

  dict_set (dict, DATA_RET, int_to_data (ret));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  dict_dump (fp, dict);
  dict_destroy (dict);
  fflush (fp);
  return 0;
}

int
glusterfsd_getattr (FILE *fp)
{
  int retval;
  struct stat buf;
  dict_t *dict = dict_load (fp);
  char *data = data_to_bin (dict_get (dict, DATA_BUF));

  retval = lstat (RELATIVE(data), &buf);

  FUNCTION_CALLED;
  // convert stat to big endian
  dict_set (dict, DATA_RET, int_to_data (retval));
  dict_set (dict, DATA_ERRNO, int_to_data (0));

  dict_set (dict, DATA_BUF, bin_to_data ((void *)&buf, sizeof (buf) + 1));
  
  dict_dump (fp, dict);
  dict_destroy (dict);
  fflush (fp);
  return 0;
}

int
glusterfsd_statfs (FILE *fp)
{
  int retval;
  struct statvfs buf;
  dict_t *dict = dict_load (fp);
  char *data = data_to_bin (dict_get (dict, DATA_BUF));
  
  retval = statvfs (RELATIVE(data), &buf);

  FUNCTION_CALLED;
  // convert stat to big endian
  
  dict_set (dict, DATA_RET, int_to_data (retval));
  dict_set (dict, DATA_ERRNO, int_to_data (errno));

  if (retval == 0)
    dict_set (dict, DATA_BUF, bin_to_data ((void *)&buf, sizeof (buf)));
  else
    dict_del (dict, DATA_BUF);

  dict_dump (fp, dict);
  dict_destroy (dict);
  fflush (fp);
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
  
  fscanf (fp, "%d", &operation);
  ret = gfsd[operation].function (fp);

  if (ret != 0) {
    gprintf ("%s: terminating, (errno=%d)\n", __FUNCTION__,
	     errno);
    return -1;
  }
  return 0;
}
