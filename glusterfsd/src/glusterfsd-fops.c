
#include "glusterfs.h"

extern int full_write_sock (int fd, 
			    const void *data,
			    size_t len);

extern int full_read_sock (int fd,
			   char *data,
			   size_t len);


static int
glusterfsd_open (int sock,
		 dict_t *dict,
		 void  *data)
{
  int fd;
  FILE *fp;

  fd = open (RELATIVE(data), data_to_int (dict_get (dict, str_to_data (XFER_FLAGS))));
  gprintf ("open on %s returned %d\n", (char *)data, fd);

  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (fd));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_FD), int_to_data (fd));
  dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);

  return 0;
}

static int
glusterfsd_release (int sock,
		    dict_t *dict,
		    void *data)
{
  int fd;
  int ret;
  FILE *fp;

  fd = data_to_int (dict_get (dict, str_to_data (XFER_FD)));

  ret = close (fd);
  
  dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (ret));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);

  return  0;
}

static int
glusterfsd_flush (int sock,
		  dict_t *dict,
		  void *data)
{
  FILE *fp;
  //  int fd = data_to_int (dict_get (dict, str_to_data (XFER_FD)));
  
  // ret = fsync (fd);
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (0));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);

  return  0;
}


static int
glusterfsd_fsync (int sock,
		  dict_t *dict,
		  void *data)
{
  FILE *fp;
  int retval;
  int flags = data_to_int (dict_get (dict, str_to_data (XFER_FLAGS)));
  int fd = data_to_int (dict_get (dict, str_to_data (XFER_FD)));

  if (flags)
    retval = fdatasync (fd);
  else
    retval = fsync (fd);

  dict->count = 0;
  dict->members = NULL;
  dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (retval));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);

  return  0;
}

static int
glusterfsd_write (int sock,
		  dict_t *dict,
		  void *data)
{
  FILE *fp;
  int len = data_to_int (dict_get (dict, str_to_data (XFER_LEN)));
  int fd = data_to_int (dict_get (dict, str_to_data (XFER_FD)));

  len = full_write_sock (fd, data,  len);

  dict->count = 0;
  dict->members = NULL;
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (len));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);
  return 0;
}

static int
glusterfsd_read (int sock,
		 dict_t *dict,
		 void *_data)
{
  int fd = data_to_int (dict_get (dict, str_to_data (XFER_FD)));
  int len = 0;
  int size = data_to_int (dict_get (dict, str_to_data (XFER_LEN)));
  off_t offset = data_to_int (dict_get (dict, str_to_data (XFER_OFFSET)));
  FILE *fp;
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
    len = full_read_sock (fd, data, size);
  } else {
    len = 0;
  }
  dict->count = 0;
  dict->members = NULL;

  dict_set (dict, str_to_data (XFER_SIZE), int_to_data ((len < 0)?0:len));
  dict_set (dict, str_to_data (XFER_LEN), int_to_data (len));
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (len));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_DATA), bin_to_data (data, len));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);

  return 0;
}


static int
glusterfsd_readdir (int sock,
		    dict_t *dict,
		    void *data)
{
  FILE *fp;
  int retval;
  DIR *dir;
  int length = 0;
  struct dirent *dirent;
  static struct dirent *dirents = NULL;
  static int alloced = 0;

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

  dict->count = 0;
  dict->members = NULL;
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (0));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_SIZE), int_to_data (sizeof (*dirent) * length));
  dict_set (dict, str_to_data (XFER_DATA), 
	    bin_to_data ((void *)dirents, sizeof (*dirent) * length));
  
  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);
  return 0;
}

static int
glusterfsd_readlink (int sock,
		     dict_t *dict,
		     void *data)
{
  int retval;
  int len;
  char buf[PATH_MAX];
  FILE *fp;

  len = data_to_int (dict_get (dict, str_to_data (XFER_SIZE)));
  if (len >= PATH_MAX)
    len = PATH_MAX - 1;

  retval = readlink (RELATIVE(data), buf, len);

  gprintf ("%s: on %s\n", __FUNCTION__, (char *)data);
  dict->count = 0;
  dict->members = NULL;

  if (retval > 0) {
    dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (retval));
    dict_set (dict, str_to_data (XFER_SIZE), int_to_data (retval));
    dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
    dict_set (dict, str_to_data (XFER_REMOTE_RET), bin_to_data (buf, retval));
  } else {
    dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (retval));
    dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));
    dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  }

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);
  return 0;
}

static int
glusterfsd_mknod (int  sock,
		  dict_t *dict,
		  void *data)
{
  FILE *fp;
  int mode = data_to_int (dict_get (dict, str_to_data (XFER_MODE)));;
  int dev = data_to_int (dict_get (dict, str_to_data (XFER_DEV)));;
  int uid = data_to_int (dict_get (dict, str_to_data (XFER_UID)));;
  int gid = data_to_int (dict_get (dict, str_to_data (XFER_GID)));;
  int ret;

  ret = mknod (RELATIVE(data), mode, dev);

  if (ret == 0) {
    chown (RELATIVE(data), uid, gid);
  }
  dict->count = 0;
  dict->members = NULL;
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (ret));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);
  return 0;
}


static int
glusterfsd_mkdir (int  sock,
		  dict_t *dict,
		  void *data)
{
  int mode = data_to_int (dict_get (dict, str_to_data (XFER_MODE)));
  int uid = data_to_int (dict_get (dict, str_to_data (XFER_UID)));;
  int gid = data_to_int (dict_get (dict, str_to_data (XFER_GID)));;
  int ret = mkdir (RELATIVE(data), mode);
  FILE *fp;

  if (ret == 0) {
    chown (RELATIVE(data), uid, gid);
  }
  dict->count = 0;
  dict->members = NULL;
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (ret));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);

  return 0;
}

static int
glusterfsd_unlink (int  sock,
		   dict_t *dict,
		   void *data)
{
  FILE *fp;
  int ret = unlink (RELATIVE(data));

  dict->count = 0;
  dict->members = NULL;
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (ret));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);

  return 0;
}


static int
glusterfsd_chmod (int  sock,
		  dict_t *dict,
		  void *data)
{
  FILE *fp;
  int mode = data_to_int (dict_get (dict, str_to_data (XFER_MODE)));
  int ret = chmod (RELATIVE(data), mode);

  dict->count = 0;
  dict->members = NULL;
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (ret));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);

  return 0;
}


static int
glusterfsd_chown (int  sock,
		  dict_t *dict,
		  void *data)
{
  FILE *fp;
  int uid = data_to_int (dict_get (dict, str_to_data (XFER_UID)));
  int gid = data_to_int (dict_get (dict, str_to_data (XFER_GID)));
  int ret = lchown (RELATIVE(data), uid, gid);

  dict->count = 0;
  dict->members = NULL;
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (ret));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);

  return 0;
}

static int
glusterfsd_truncate (int  sock,
		     dict_t *dict,
		     void *data)
{
  FILE *fp;
  int offset = data_to_int (dict_get (dict, str_to_data (XFER_OFFSET)));
  int ret = truncate (RELATIVE(data), offset);

  dict->count = 0;
  dict->members = NULL;
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (ret));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);

  return 0;
}

static int
glusterfsd_ftruncate (int  sock,
		      dict_t *dict,
		      void *data)
{
  FILE *fp;
  int offset = data_to_int (dict_get (dict, str_to_data (XFER_OFFSET)));
  int fd = data_to_int (dict_get (dict, str_to_data (XFER_FD)));
  int ret;
  
  ret = ftruncate (fd, offset);

  dict->count = 0;
  dict->members = NULL;
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (ret));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);

  return 0;
}

static int
glusterfsd_utime (int  sock,
		  dict_t *dict,
		  void *data)
{
  struct utimbuf  buf;
  FILE *fp;
  int ret;
  int actime = data_to_int (dict_get (dict, str_to_data (XFER_ACTIME)));
  int modtime = data_to_int (dict_get (dict, str_to_data (XFER_MODTIME)));
  
  buf.actime = actime;
  buf.modtime = modtime;

  ret = utime (RELATIVE(data), &buf);

  dict->count = 0;
  dict->members = NULL;
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (ret));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);

  return 0;
}


static int
glusterfsd_rmdir (int  sock,
		  dict_t *dict,
		  void *data)
{
  int ret;
  FILE *fp;
  
  ret = rmdir (RELATIVE(data));

  dict->count = 0;
  dict->members = NULL;
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (ret));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);

  return 0;
}

static int
glusterfsd_symlink (int  sock,
		    dict_t *dict,
		    void *data)
{
  FILE *fp;
  int ret;
  int len = data_to_int (dict_get (dict, str_to_data (XFER_LEN)));
  int uid = data_to_int (dict_get (dict, str_to_data (XFER_UID)));;
  int gid = data_to_int (dict_get (dict, str_to_data (XFER_GID)));;
  char *oldpath = RELATIVE(data);
  char *newpath = RELATIVE((char *)data + len);

  ret = symlink (oldpath, newpath);
  gprintf ("%s: symlink %s->%s\n", __FUNCTION__, oldpath, newpath);

  if (ret == 0) {
    lchown (newpath, uid, gid);
  }
  dict->count = 0;
  dict->members = NULL;
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (ret));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);

  return 0;
}

static int
glusterfsd_rename (int  sock,
		   dict_t *dict,
		   void *data)
{
  FILE *fp;
  int ret;
  int len = data_to_int (dict_get (dict, str_to_data (XFER_LEN)));
  int uid = data_to_int (dict_get (dict, str_to_data (XFER_UID)));;
  int gid = data_to_int (dict_get (dict, str_to_data (XFER_GID)));;

  char *oldpath = RELATIVE(data);
  char *newpath = RELATIVE((char *)data + len);

  ret = rename (oldpath, newpath);
  gprintf ("%s: rename %s->%s\n", __FUNCTION__, oldpath, newpath);

  if (ret == 0) {
    chown (newpath, uid, gid);
  }

  dict->count = 0;
  dict->members = NULL;
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (ret));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);

  return 0;
}


static int
glusterfsd_link (int  sock,
		 dict_t *dict,
		 void *data)
{
  FILE *fp;
  int ret;
  int len = data_to_int (dict_get (dict, str_to_data (XFER_LEN)));
  int uid = data_to_int (dict_get (dict, str_to_data (XFER_UID)));;
  int gid = data_to_int (dict_get (dict, str_to_data (XFER_GID)));;
  char *oldpath = data;
  char *newpath = RELATIVE((char *)data + len);

  ret = link (oldpath, newpath);
  gprintf ("%s: link %s->%s\n", __FUNCTION__, oldpath, newpath);

  if (ret == 0) {
    chown (newpath, uid, gid);
  }
  dict->count = 0;
  dict->members = NULL;
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (ret));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));
  dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);
  return 0;
}

static int
glusterfsd_getattr (int sock,
		    dict_t *dict,
		    void *data)
{
  int retval;
  struct stat buf;
  FILE *fp;

  retval = lstat (RELATIVE(data), &buf);

  FUNCTION_CALLED;
  // convert stat to big endian
  dict->count = 0;
  dict->members = NULL;
  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (retval));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (0));

  if (retval == 0)
    dict_set (dict, str_to_data (XFER_SIZE), int_to_data (sizeof (buf)+1));
  else
    dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));

  dict_set (dict, str_to_data (XFER_DATA), bin_to_data ((void *)&buf, sizeof (buf) + 1));
  
  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);
  return 0;
}

static int
glusterfsd_statfs (int sock,
		   dict_t *dict,
		   void *data)
{
  FILE *fp;
  int retval;
  struct statvfs buf;

  retval = statvfs (RELATIVE(data), &buf);

  FUNCTION_CALLED;
  // convert stat to big endian

  dict->count = 0;
  dict->members = NULL;

  if (retval == 0)
    dict_set (dict, str_to_data (XFER_SIZE), int_to_data (sizeof (buf)));
  else
    dict_set (dict, str_to_data (XFER_SIZE), int_to_data (0));

  dict_set (dict, str_to_data (XFER_REMOTE_RET), int_to_data (retval));
  dict_set (dict, str_to_data (XFER_REMOTE_ERRNO), int_to_data (errno));

  if (retval == 0)
    dict_set (dict, str_to_data (XFER_DATA), bin_to_data ((void *)&buf, sizeof (buf)));

  fp = fdopen (sock, "a+");
  dict_dump (fp, dict);
  fflush (fp);
  return 0;
}

int
server_fs_loop (int client_sock)
{
  int ret;
  int buf_len = 4096;
  char *read_buf = (void *) calloc (1, buf_len);
  dict_t *dict;
  int size;
  int operation;
  FILE *fp;
  
  fp = fdopen (client_sock, "a+");
  fflush (fp);
  dict = dict_load (fp);
  if (!dict)
    return -1;

  size = data_to_int (dict_get (dict, str_to_data (XFER_SIZE)));
  
  if (size > buf_len) {
    free (read_buf);
    read_buf = malloc (size);
    buf_len = size;
  }
  
  if (size != 0)
    memcpy (read_buf, (void *)data_to_bin (dict_get (dict, str_to_data (XFER_DATA))), size);

  operation = data_to_int (dict_get (dict, str_to_data (XFER_OPERATION)));

  switch (operation) {
  case OP_GETATTR:
    ret = glusterfsd_getattr (client_sock, dict, read_buf);
    break;
  case OP_READDIR:
    ret = glusterfsd_readdir (client_sock, dict, read_buf);
    break;
  case OP_OPEN:
    ret = glusterfsd_open (client_sock, dict, read_buf);
    break;
  case OP_READ:
    ret = glusterfsd_read (client_sock, dict, read_buf);
    break;
  case OP_RELEASE:
    ret = glusterfsd_release (client_sock, dict, read_buf);
    break;
  case OP_WRITE:
    ret = glusterfsd_write (client_sock, dict, read_buf);
    break;
  case  OP_READLINK:
    ret = glusterfsd_readlink (client_sock, dict, read_buf);
    break;
  case OP_MKNOD:
    ret = glusterfsd_mknod (client_sock, dict,  read_buf);
    break;
  case OP_MKDIR:
    ret = glusterfsd_mkdir (client_sock, dict,  read_buf);
    break;
  case OP_UNLINK:
    ret = glusterfsd_unlink (client_sock, dict,  read_buf);
    break;
  case OP_RMDIR:
    ret = glusterfsd_rmdir (client_sock, dict, read_buf);
    break;
  case OP_SYMLINK:
    ret = glusterfsd_symlink (client_sock, dict, read_buf);
    break;
  case OP_RENAME:
    ret = glusterfsd_rename (client_sock, dict, read_buf);
    break;
  case OP_LINK:
    ret = glusterfsd_link (client_sock, dict, read_buf);
    break;
  case OP_CHMOD:
    ret = glusterfsd_chmod (client_sock, dict, read_buf);
    break;
  case OP_CHOWN:
    ret = glusterfsd_chown (client_sock, dict, read_buf);
    break;
  case OP_TRUNCATE:
    ret = glusterfsd_truncate (client_sock, dict, read_buf);
    break;
  case OP_UTIME:
    ret = glusterfsd_utime (client_sock, dict, read_buf);
    break;
  case OP_STATFS:
    ret = glusterfsd_statfs (client_sock, dict, read_buf);
    break;
  case OP_FLUSH:
    ret = glusterfsd_flush (client_sock, dict, read_buf);
    break;
  case OP_FTRUNCATE:
    ret = glusterfsd_ftruncate (client_sock, dict, read_buf);
    break;
  default:
    gprintf ("%s: unknown op %d, (errno=%d)\n", __FUNCTION__,
	     operation, errno);
    ret = -1;
    break;
  }

  if (ret != 0) {
    gprintf ("%s: terminating, (errno=%d)\n", __FUNCTION__,
	     errno);
    return -1;
  }
  return 0;
}
