
#include "glusterfs.h"

extern int full_write_sock (int fd, 
			    const void *data,
			    size_t len);

extern int full_read_sock (int fd,
			   char *data,
			   size_t len);


static int
glusterfsd_open (int sock,
		 struct xfer_header *xfer,
		 void  *data)
{
  int fd;

  fd = open (RELATIVE(data), xfer->flags);
  gprintf ("open on %s returned %d\n", (char *)data, fd);
  xfer->remote_ret = fd;
  xfer->remote_errno = errno;
  xfer->fd = fd;
  xfer->size = 0;

  if (full_write_sock (sock, xfer, sizeof (*xfer)) != sizeof (*xfer)) {
    gprintf ("%s: full_write failed with errno=%d\n", __FUNCTION__, errno);
    return -1;
  }
  return 0;
}

static int
glusterfsd_release (int sock,
		    struct xfer_header *xfer,
		    void *data)
{
  int fd;

  fd = xfer->fd;

  xfer->size = 0;
  xfer->remote_ret = close (fd);
  xfer->remote_errno = errno;

  if (full_write_sock (sock, xfer, sizeof (*xfer)) != sizeof (*xfer)) {
    gprintf ("%s: full_write failed with errno=%d\n", __FUNCTION__, errno);
    return -1;
  }
  return  0;
}

static int
glusterfsd_flush (int sock,
		  struct xfer_header *xfer,
		  void *data)
{
  int fd;

  fd = xfer->fd;

  xfer->size = 0;
  xfer->remote_ret = 0; //fsync (fd);
  xfer->remote_errno = errno;

  if (full_write_sock (sock, xfer, sizeof (*xfer)) != sizeof (*xfer)) {
    gprintf ("%s: full_write failed with errno=%d\n", __FUNCTION__, errno);
    return -1;
  }
  return  0;
}


static int
glusterfsd_fsync (int sock,
		  struct xfer_header *xfer,
		  void *data)
{
  int fd;

  fd = xfer->fd;

  xfer->size = 0;
  if (xfer->flags)
    xfer->remote_ret = fdatasync (fd);
  else
    xfer->remote_ret = fsync (fd);
  xfer->remote_errno = errno;

  if (full_write_sock (sock, xfer, sizeof (*xfer)) != sizeof (*xfer)) {
    gprintf ("%s: full_write failed with errno=%d\n", __FUNCTION__, errno);
    return -1;
  }
  return  0;
}

static int
glusterfsd_write (int sock,
		  struct xfer_header *xfer,
		  void *data)
{
  int fd = xfer->fd;
  int len = xfer->len;

  len = full_write_sock (fd, data,  len);

  xfer->remote_ret = len;
  xfer->remote_errno = errno;
  xfer->size = 0;

  if (full_write_sock (sock, xfer, sizeof (*xfer)) != sizeof (*xfer))
    return -1;
  return 0;
}

static int
glusterfsd_read (int sock,
		 struct xfer_header *xfer,
		 void *_data)
{
  int fd = xfer->fd;
  int len = 0;
  int size = xfer->len;
  off_t offset = xfer->offset;
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

  xfer->size = len < 0 ? 0 : len;
  xfer->len = len;
  xfer->remote_ret = len;
  xfer->remote_errno = errno;

  if (full_write_sock (sock, xfer, sizeof (*xfer)) != sizeof (*xfer))
    return -1;

  if (len > 0) {
    if (full_write_sock (sock, data, len) != len) {
      gprintf ("%s: full_write failed on socket (errno=%d)\n",
	       __FUNCTION__, errno);
      return -1;
    }
  } else {
    gprintf ("%d is len, did not write\n", len);
  }

  return 0;
}


static int
glusterfsd_readdir (int sock,
		    struct xfer_header *xfer,
		    void *data)
{
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

  xfer->size = sizeof (*dirent) * length;
  xfer->remote_ret = 0;
  xfer->remote_errno = errno;

  retval = full_write_sock (sock, (void *)xfer, sizeof (*xfer));
  if (retval != sizeof (*xfer)) {
    gprintf ("%s: 1st full_write_sock failed, errno=%d\n", __FUNCTION__,
	     errno);
    return -1;
  }
  retval = full_write_sock (sock, (void *)dirents, xfer->size);
  if (retval != xfer->size) {
    gprintf ("%s: 2nd full_write_sock failed, errno=%d\n", __FUNCTION__,
	     errno);
    return -1;
  }
  return 0;
}

static int
glusterfsd_readlink (int sock,
		     struct xfer_header *xfer,
		     void *data)
{
  int retval;
  char buf[PATH_MAX];

  if (xfer->len >= PATH_MAX)
    xfer->len = PATH_MAX - 1;

  retval = readlink (RELATIVE(data), buf, xfer->len);

  gprintf ("%s: on %s\n", __FUNCTION__, (char *)data);

  if (retval > 0) {
    xfer->remote_ret = retval;
    xfer->size = retval;
    xfer->remote_errno = errno;

    if (full_write_sock (sock, (void *)xfer, sizeof (*xfer)) != sizeof (*xfer)) {
      gprintf ("%s: 1st full_write_sock failed (errno=%d)\n",
	       __FUNCTION__, errno);
      return  -1;
    }

    if (full_write_sock (sock, buf, xfer->size) != xfer->size) {
      gprintf ("%s: 2nd full_write_sock failed (errno=%d)\n",
	       __FUNCTION__, errno);
      return -1;
    }
  } else {
    xfer->remote_ret = retval;
    xfer->remote_errno = errno;
    xfer->size = 0;

    if (full_write_sock (sock, (void *)xfer, sizeof (*xfer)) != sizeof (*xfer)) {
      gprintf ("%s: 2st full_write_sock failed (errno=%d)\n",
	       __FUNCTION__, errno);
      return  -1;
    }
  }

  return 0;
}

static int
glusterfsd_mknod (int  sock,
		  struct xfer_header *xfer,
		  void *data)
{
  xfer->remote_ret = mknod (RELATIVE(data), xfer->mode, xfer->dev);
  xfer->remote_errno = errno;
  xfer->size = 0;

  if (xfer->remote_ret == 0) {
    chown (RELATIVE(data), xfer->uid, xfer->gid);
  }

  if (full_write_sock (sock, (void *)xfer, sizeof (*xfer)) != sizeof (*xfer)) {
    gprintf ("%s: full_write failed (errno=%d)\n", __FUNCTION__, errno);
    return -1;
  }
  return 0;
}


static int
glusterfsd_mkdir (int  sock,
		  struct xfer_header *xfer,
		  void *data)
{
  xfer->remote_ret = mkdir (RELATIVE(data), xfer->mode);
  xfer->remote_errno = errno;
  xfer->size = 0;

  if (xfer->remote_ret == 0) {
    chown (RELATIVE(data), xfer->uid, xfer->gid);
  }

  if (full_write_sock (sock, (void *)xfer, sizeof (*xfer)) != sizeof (*xfer)) {
    gprintf ("%s: full_write failed (errno=%d)\n", __FUNCTION__, errno);
    return -1;
  }

  return 0;
}

static int
glusterfsd_unlink (int  sock,
		   struct xfer_header *xfer,
		   void *data)
{
  xfer->remote_ret = unlink (RELATIVE(data));
  xfer->remote_errno = errno;
  xfer->size = 0;

  if (full_write_sock (sock, (void *)xfer, sizeof (*xfer)) != sizeof (*xfer)) {
    gprintf ("%s: full_write failed (errno=%d)\n", __FUNCTION__, errno);
    return -1;
  }

  return 0;
}


static int
glusterfsd_chmod (int  sock,
		  struct xfer_header *xfer,
		  void *data)
{
  xfer->remote_ret = chmod (RELATIVE(data), xfer->mode);
  xfer->remote_errno = errno;
  xfer->size = 0;

  if (full_write_sock (sock, (void *)xfer, sizeof (*xfer)) != sizeof (*xfer)) {
    gprintf ("%s: full_write failed (errno=%d)\n", __FUNCTION__, errno);
    return -1;
  }

  return 0;
}


static int
glusterfsd_chown (int  sock,
		  struct xfer_header *xfer,
		  void *data)
{
  xfer->remote_ret = lchown (RELATIVE(data), xfer->uid, xfer->gid);
  xfer->remote_errno = errno;
  xfer->size = 0;

  if (full_write_sock (sock, (void *)xfer, sizeof (*xfer)) != sizeof (*xfer)) {
    gprintf ("%s: full_write failed (errno=%d)\n", __FUNCTION__, errno);
    return -1;
  }

  return 0;
}

static int
glusterfsd_truncate (int  sock,
		     struct xfer_header *xfer,
		     void *data)
{
  xfer->remote_ret = truncate (RELATIVE(data), xfer->offset);
  xfer->remote_errno = errno;
  xfer->size = 0;

  if (full_write_sock (sock, (void *)xfer, sizeof (*xfer)) != sizeof (*xfer)) {
    gprintf ("%s: full_write failed (errno=%d)\n", __FUNCTION__, errno);
    return -1;
  }

  return 0;
}

static int
glusterfsd_ftruncate (int  sock,
		     struct xfer_header *xfer,
		     void *data)
{
  xfer->remote_ret = ftruncate (xfer->fd, xfer->offset);
  xfer->remote_errno = errno;
  xfer->size = 0;

  if (full_write_sock (sock, (void *)xfer, sizeof (*xfer)) != sizeof (*xfer)) {
    gprintf ("%s: full_write failed (errno=%d)\n", __FUNCTION__, errno);
    return -1;
  }

  return 0;
}

static int
glusterfsd_utime (int  sock,
		  struct xfer_header *xfer,
		  void *data)
{
  struct utimbuf  buf;

  buf.actime = xfer->actime;
  buf.modtime = xfer->modtime;

  xfer->remote_ret = utime (RELATIVE(data), &buf);
  xfer->remote_errno = errno;
  xfer->size = 0;

  if (full_write_sock (sock, (void *)xfer, sizeof (*xfer)) != sizeof (*xfer)) {
    gprintf ("%s: full_write failed (errno=%d)\n", __FUNCTION__, errno);
    return -1;
  }

  return 0;
}


static int
glusterfsd_rmdir (int  sock,
		  struct xfer_header *xfer,
		  void *data)
{
  xfer->remote_ret = rmdir (RELATIVE(data));
  xfer->remote_errno = errno;
  xfer->size = 0;

  if (full_write_sock (sock, (void *)xfer, sizeof (*xfer)) != sizeof (*xfer)) {
    gprintf ("%s: full_write failed (errno=%d)\n", __FUNCTION__, errno);
    return -1;
  }

  return 0;
}

static int
glusterfsd_symlink (int  sock,
		    struct xfer_header *xfer,
		    void *data)
{
  char *oldpath = RELATIVE(data);
  char *newpath = RELATIVE((char *)data + xfer->len);

  xfer->remote_ret = symlink (oldpath, newpath);
  gprintf ("%s: symlink %s->%s\n", __FUNCTION__, oldpath, newpath);
  xfer->remote_errno = errno;
  xfer->size = 0;

  if (xfer->remote_ret == 0) {
    lchown (newpath, xfer->uid, xfer->gid);
  }

  if (full_write_sock (sock, (void *)xfer, sizeof (*xfer)) != sizeof (*xfer)) {
    gprintf ("%s: full_write failed (errno=%d)\n", __FUNCTION__, errno);
    return -1;
  }

  return 0;
}

static int
glusterfsd_rename (int  sock,
		   struct xfer_header *xfer,
		   void *data)
{
  char *oldpath = RELATIVE(data);
  char *newpath = RELATIVE((char *)data + xfer->len);

  xfer->remote_ret = rename (oldpath, newpath);
  gprintf ("%s: rename %s->%s\n", __FUNCTION__, oldpath, newpath);
  xfer->remote_errno = errno;
  xfer->size = 0;

  if (xfer->remote_ret == 0) {
    chown (newpath, xfer->uid, xfer->gid);
  }

  if (full_write_sock (sock, (void *)xfer, sizeof (*xfer)) != sizeof (*xfer)) {
    gprintf ("%s: full_write failed (errno=%d)\n", __FUNCTION__, errno);
    return -1;
  }

  return 0;
}


static int
glusterfsd_link (int  sock,
		 struct xfer_header *xfer,
		 void *data)
{
  char *oldpath = data;
  char *newpath = RELATIVE((char *)data + xfer->len);

  xfer->remote_ret = link (oldpath, newpath);
  gprintf ("%s: link %s->%s\n", __FUNCTION__, oldpath, newpath);
  xfer->remote_errno = errno;
  xfer->size = 0;

  if (xfer->remote_ret == 0) {
    chown (newpath, xfer->uid, xfer->gid);
  }

  if (full_write_sock (sock, (void *)xfer, sizeof (*xfer)) != sizeof (*xfer)) {
    gprintf ("%s: full_write failed (errno=%d)\n", __FUNCTION__, errno);
    return -1;
  }
  return 0;
}

static int
glusterfsd_getattr (int sock,
		    struct xfer_header *xfer,
		    void *data)
{
  int retval;
  struct stat buf;

  retval = lstat (RELATIVE(data), &buf);

  FUNCTION_CALLED;
  // convert stat to big endian

  xfer->remote_ret = retval;
  xfer->remote_errno = errno;
  if (retval == 0)
    xfer->size = sizeof (buf);
  else
    xfer->size = 0;

  retval = full_write_sock (sock, (void *)xfer, sizeof (*xfer));
  if (retval != sizeof (*xfer)) {
    gprintf ("%s: 1st full_write_sock failed (errno=%d)\n",
	     __FUNCTION__, errno);
    return -1;
  }

  if (xfer->remote_ret == 0) {
    gprintf ("%s: mode for %s is %x\n",
	     __FUNCTION__, (char *)data, buf.st_mode);
    retval = full_write_sock (sock, (void *)&buf, sizeof (buf));
    if (retval != sizeof (buf)) {
      gprintf ("%s: 2nd full_write_sock failed (errno=%d)\n",
	       __FUNCTION__,errno);
      return -1;
    }
  }

  return 0;
}

static int
glusterfsd_statfs (int sock,
		   struct xfer_header *xfer,
		    void *data)
{
  int retval;
  struct statvfs buf;

  retval = statvfs (RELATIVE(data), &buf);

  FUNCTION_CALLED;
  // convert stat to big endian

  xfer->remote_ret = retval;
  xfer->remote_errno = errno;
  if (retval == 0)
    xfer->size = sizeof (buf);
  else
    xfer->size = 0;

  retval = full_write_sock (sock, (void *)xfer, sizeof (*xfer));
  if (retval != sizeof (*xfer)) {
    gprintf ("%s: 1st full_write_sock failed (errno=%d)\n",
	     __FUNCTION__, errno);
    return -1;
  }

  if (xfer->remote_ret == 0) {
    retval = full_write_sock (sock, (void *)&buf, sizeof (buf));
    if (retval != sizeof (buf)) {
      gprintf ("%s: 2nd full_write_sock failed (errno=%d)\n",
	       __FUNCTION__,errno);
      return -1;
    }
  }
  return 0;
}

int
server_fs_loop (int client_sock)
{
  int ret;
  struct xfer_header xfer;
  int buf_len = 4096;
  char *read_buf = (void *) calloc (1, buf_len);

  ret = full_read_sock (client_sock, (void *)&xfer, sizeof (xfer));
  if (ret != sizeof (xfer)) {
    gprintf ("%s: short read (%d != %d), (errno=%d)\n", 
	     __FUNCTION__, ret, sizeof (xfer), errno);
    return -1;
  }

  if (xfer.size > buf_len) {
    free (read_buf);
    read_buf = malloc (xfer.size);
    buf_len = xfer.size;
  }

  ret = full_read_sock (client_sock, read_buf, xfer.size);
  
  if (ret != xfer.size) {
    gprintf ("%s: unexpected size (%d != %d) (errno=%d)\n", 
	     __FUNCTION__, ret, xfer.size, errno);
    return -1;
  }

  switch (xfer.op) {
  case OP_GETATTR:
    ret = glusterfsd_getattr (client_sock, &xfer, read_buf);
    break;
  case OP_READDIR:
    ret = glusterfsd_readdir (client_sock, &xfer, read_buf);
    break;
  case OP_OPEN:
    ret = glusterfsd_open (client_sock, &xfer, read_buf);
    break;
  case OP_READ:
    ret = glusterfsd_read (client_sock, &xfer, read_buf);
    break;
  case OP_RELEASE:
    ret = glusterfsd_release (client_sock, &xfer, read_buf);
    break;
  case OP_WRITE:
    ret = glusterfsd_write (client_sock, &xfer, read_buf);
    break;
  case  OP_READLINK:
    ret = glusterfsd_readlink (client_sock, &xfer, read_buf);
    break;
  case OP_MKNOD:
    ret = glusterfsd_mknod (client_sock, &xfer,  read_buf);
    break;
  case OP_MKDIR:
    ret = glusterfsd_mkdir (client_sock, &xfer,  read_buf);
    break;
  case OP_UNLINK:
    ret = glusterfsd_unlink (client_sock, &xfer,  read_buf);
    break;
  case OP_RMDIR:
    ret = glusterfsd_rmdir (client_sock, &xfer, read_buf);
    break;
  case OP_SYMLINK:
    ret = glusterfsd_symlink (client_sock, &xfer, read_buf);
    break;
  case OP_RENAME:
    ret = glusterfsd_rename (client_sock, &xfer, read_buf);
    break;
  case OP_LINK:
    ret = glusterfsd_link (client_sock, &xfer, read_buf);
    break;
  case OP_CHMOD:
    ret = glusterfsd_chmod (client_sock, &xfer, read_buf);
    break;
  case OP_CHOWN:
    ret = glusterfsd_chown (client_sock, &xfer, read_buf);
    break;
  case OP_TRUNCATE:
    ret = glusterfsd_truncate (client_sock, &xfer, read_buf);
    break;
  case OP_UTIME:
    ret = glusterfsd_utime (client_sock, &xfer, read_buf);
    break;
  case OP_STATFS:
    ret = glusterfsd_statfs (client_sock, &xfer, read_buf);
    break;
  case OP_FLUSH:
    ret = glusterfsd_flush (client_sock, &xfer, read_buf);
    break;
  case OP_FTRUNCATE:
    ret = glusterfsd_ftruncate (client_sock, &xfer, read_buf);
    break;
  default:
    gprintf ("%s: unknown op %d, (errno=%d)\n", __FUNCTION__,
	     xfer.op, errno);
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
