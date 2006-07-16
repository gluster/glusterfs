
#include "glusterfs.h"



static int
try_connect (struct glusterfs_private *priv)
{
  struct sockaddr_in sin;

  if (priv->sock == -1)
    priv->sock = socket (PF_INET, SOCK_STREAM, 0);

  if (priv->sock == -1) {
    perror ("socket()");
    return -errno;
  }

  sin.sin_family = PF_INET;
  sin.sin_port = priv->port;
  sin.sin_addr.s_addr = priv->addr;

  if (connect (priv->sock, (struct sockaddr *)&sin, sizeof (sin)) != 0) {
    perror ("connect()");
    close (priv->sock);
    priv->sock = -1;
    return -errno;
  }

  return 0;
}

static int
glusterfs_getattr (const char *path,
		   struct stat *stbuf)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  xfer.op = OP_GETATTR;
  xfer.size = strlen (path) + 1;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_write\n");
    return -errno;
  }

  if (full_write (priv, path, xfer.size) != xfer.size) {
    gprintf ("returning from 2nd full_write\n");
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_read\n");
    return -errno;
  }

  if (xfer.size != sizeof (struct stat)) {
    gprintf ("xfer.size(%d) != sizeof (struct stat)(%d)\n",
	     xfer.size, sizeof (struct stat));
    full_read (priv, NULL, xfer.size);
    return -errno;
  }


  if (xfer.remote_ret != 0) {
    gprintf ("xfer.remote_ret = %d\n", xfer.remote_ret);
    return -xfer.remote_errno;
  }

  if (full_read (priv, (void *) stbuf, xfer.size) != xfer.size) {
    gprintf ("returning from 2nd full_read\n");
    return -errno;
  }

  gprintf ("%s returning successfully\n", __FUNCTION__);
  errno = 0;
  // convert stbuf endian
  return 0;
}


static int
glusterfs_readlink (const char *path,
		    char *dest,
		    size_t size)
{

  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  xfer.op = OP_READLINK;
  xfer.size = strlen (path) + 1;
  xfer.len = size;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_write\n");
    return -errno;
  }

  if (full_write (priv, path, xfer.size) != xfer.size) {
    gprintf ("returning from 2nd full_write\n");
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_read\n");
    return -errno;
  }

  if (xfer.remote_ret <= 0)
    return -xfer.remote_errno;

  if (full_read (priv, dest, xfer.size) != xfer.size) {
    gprintf ("%s: full_read returned errno=%d\n", __FUNCTION__, errno);
    return -errno;
  }

  dest[xfer.size] = 0;

  return 0;
}

/*
static int
glusterfs_getdir (const char *path,
		  fuse_dirh_t dirh,
		  fuse_dirfil_t dirfil)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}
*/

static int
glusterfs_mknod (const char *path,
		 mode_t mode,
		 dev_t dev)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  xfer.op = OP_MKNOD;
  xfer.size = strlen (path) + 1;
  xfer.mode = mode;
  xfer.dev = dev;
  xfer.uid = fuse_get_context ()->uid;
  xfer.gid = fuse_get_context ()->gid;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_write\n");
    return -errno;
  }

  if (full_write (priv, path, xfer.size) != xfer.size) {
    gprintf ("returning from full_write with errno=%d\n", errno);
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_read\n");
    return -errno;
  }

  if (xfer.remote_ret < 0)
    return -xfer.remote_errno;

  return 0;

}

static int
glusterfs_mkdir (const char *path,
		 mode_t mode)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  xfer.op = OP_MKDIR;
  xfer.size = strlen (path) + 1;
  xfer.mode = mode;
  xfer.uid = fuse_get_context ()->uid;
  xfer.gid = fuse_get_context ()->gid;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_write\n");
    return -errno;
  }

  if (full_write (priv, path, xfer.size) != xfer.size) {
    gprintf ("returning from full_write with errno=%d\n", errno);
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_read\n");
    return -errno;
  }

  if (xfer.remote_ret < 0)
    return -xfer.remote_errno;

  return 0;
}

static int
glusterfs_unlink (const char *path)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  xfer.op = OP_UNLINK;
  xfer.size = strlen (path) + 1;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_write\n");
    return -errno;
  }

  if (full_write (priv, path, xfer.size) != xfer.size) {
    gprintf ("returning from full_write with errno=%d\n", errno);
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_read\n");
    return -errno;
  }

  if (xfer.remote_ret < 0)
    return -xfer.remote_errno;

  return 0;
}

static int
glusterfs_rmdir (const char *path)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  xfer.op = OP_RMDIR;
  xfer.size = strlen (path) + 1;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_write\n");
    return -errno;
  }

  if (full_write (priv, path, xfer.size) != xfer.size) {
    gprintf ("returning from full_write with errno=%d\n", errno);
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_read\n");
    return -errno;
  }

  if (xfer.remote_ret < 0)
    return -xfer.remote_errno;

  return 0;
}


static int
glusterfs_symlink (const char *oldpath,
		   const char *newpath)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  xfer.op = OP_SYMLINK;
  int old_len = strlen (oldpath);
  int new_len = strlen (newpath);

  xfer.size = old_len + 1 + new_len + 1;
  xfer.len = old_len + 1;
  xfer.uid = fuse_get_context ()->uid;
  xfer.gid = fuse_get_context ()->gid;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_write\n");
    return -errno;
  }

  if (full_write (priv, oldpath, old_len + 1) != (old_len+1)) {
    gprintf ("returning from full_write with errno=%d\n", errno);
    return -errno;
  }

  if (full_write (priv, newpath, new_len + 1) != (new_len+1)) {
    gprintf ("returning from full_write with errno=%d\n", errno);
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_read\n");
    return -errno;
  }

  if (xfer.remote_ret < 0)
    return -xfer.remote_errno;

  return 0;
}

static int
glusterfs_rename (const char *oldpath,
		  const char *newpath)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  xfer.op = OP_RENAME;
  int old_len = strlen (oldpath);
  int new_len = strlen (newpath);

  xfer.size = old_len + 1 + new_len + 1;
  xfer.len = old_len + 1;
  xfer.uid = fuse_get_context ()->uid;
  xfer.gid = fuse_get_context ()->gid;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_write\n");
    return -errno;
  }

  if (full_write (priv, oldpath, old_len + 1) != (old_len+1)) {
    gprintf ("returning from full_write with errno=%d\n", errno);
    return -errno;
  }

  if (full_write (priv, newpath, new_len + 1) != (new_len+1)) {
    gprintf ("returning from full_write with errno=%d\n", errno);
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_read\n");
    return -errno;
  }

  if (xfer.remote_ret < 0)
    return -xfer.remote_errno;

  return 0;
}

static int
glusterfs_link (const char *oldpath,
		const char *newpath)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  xfer.op = OP_LINK;
  int old_len = strlen (oldpath);
  int new_len = strlen (newpath);

  xfer.size = old_len + 1 + new_len + 1;
  xfer.len = old_len + 1;
  xfer.uid = fuse_get_context ()->uid;
  xfer.gid = fuse_get_context ()->gid;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_write\n");
    return -errno;
  }

  if (full_write (priv, oldpath, old_len + 1) != (old_len+1)) {
    gprintf ("returning from full_write with errno=%d\n", errno);
    return -errno;
  }

  if (full_write (priv, newpath, new_len + 1) != (new_len+1)) {
    gprintf ("returning from full_write with errno=%d\n", errno);
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_read\n");
    return -errno;
  }

  if (xfer.remote_ret < 0)
    return -xfer.remote_errno;

  return 0;
}

static int
glusterfs_chmod (const char *path,
		 mode_t mode)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  xfer.op = OP_CHMOD;
  xfer.size = strlen (path) + 1;
  xfer.mode = mode;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_write\n");
    return -errno;
  }

  if (full_write (priv, path, xfer.size) != xfer.size) {
    gprintf ("returning from full_write with errno=%d\n", errno);
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_read\n");
    return -errno;
  }

  if (xfer.remote_ret < 0)
    return -xfer.remote_errno;

  return 0;
}

static int
glusterfs_chown (const char *path,
		 uid_t uid,
		 gid_t gid)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  xfer.op = OP_CHOWN;
  xfer.size = strlen (path) + 1;
  xfer.uid = uid;
  xfer.gid = gid;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_write\n");
    return -errno;
  }

  if (full_write (priv, path, xfer.size) != xfer.size) {
    gprintf ("returning from full_write with errno=%d\n", errno);
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_read\n");
    return -errno;
  }

  if (xfer.remote_ret < 0)
    return -xfer.remote_errno;

  return 0;
}

static int
glusterfs_truncate (const char *path,
		    off_t offset)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  xfer.op = OP_TRUNCATE;
  xfer.size = strlen (path) + 1;
  xfer.offset = offset;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_write\n");
    return -errno;
  }

  if (full_write (priv, path, xfer.size) != xfer.size) {
    gprintf ("returning from full_write with errno=%d\n", errno);
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_read\n");
    return -errno;
  }

  if (xfer.remote_ret < 0)
    return -xfer.remote_errno;

  return 0;
}

static int
glusterfs_utime (const char *path,
		 struct utimbuf *buf)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  xfer.op = OP_UTIME;
  xfer.size = strlen (path) + 1;
  xfer.actime = buf->actime;
  xfer.modtime = buf->modtime;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_write\n");
    return -errno;
  }

  if (full_write (priv, path, xfer.size) != xfer.size) {
    gprintf ("returning from full_write with errno=%d\n", errno);
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_read\n");
    return -errno;
  }

  if (xfer.remote_ret < 0)
    return -xfer.remote_errno;

  return 0;
}

static int
glusterfs_open (const char *path,
		struct fuse_file_info *info)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  xfer.op = OP_OPEN;
  xfer.size = strlen (path) + 1;
  xfer.flags = info->flags;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_write\n");
    return -errno;
  }

  if (full_write (priv, path, xfer.size) != xfer.size) {
    gprintf ("returning from 2nd full_write\n");
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_read\n");
    return -errno;
  }

  if (xfer.remote_ret == -1) {
    gprintf ("xfer.remote_ret = %d\n", xfer.remote_ret);
    return -xfer.remote_errno;
  }

  info->fh = xfer.fd;
  gprintf ("storing open fd as %d\n", xfer.remote_ret);
  return 0;
}

static int
glusterfs_read (const char *path,
		char *buf,
		size_t size,
		off_t offset,
		struct fuse_file_info *info)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  //  FUNCTION_CALLED;

  xfer.op = OP_READ;
  xfer.size = 0;
  xfer.offset = offset;
  xfer.fd = info->fh;
  xfer.len = size;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("%s: returning from 1st full_write\n", __FUNCTION__);
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("%s: returning from 1st full_read\n", __FUNCTION__);
    return -errno;
  }

  if (xfer.remote_ret <= 0) {
    gprintf ("xfer.remote_ret = %d\n", xfer.remote_ret);
    return -xfer.remote_errno;
  }

  if (full_read (priv, buf, xfer.size) != xfer.size) {
    gprintf ("%s: 2nd  full_read failed with errno  %d\n",
	     __FUNCTION__, errno);
    return -xfer.remote_errno;
  }
  if (info->direct_io) {
    gprintf ("%s: returning %d\n", __FUNCTION__, xfer.remote_ret);
    return xfer.len;
  }

  return xfer.size;
}

static int
glusterfs_write (const char *path,
		 const char *buf,
		 size_t size,
		 off_t offset,
		 struct fuse_file_info *info)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  //  FUNCTION_CALLED;

  xfer.op = OP_WRITE;
  xfer.size = size;
  xfer.offset = offset;
  xfer.fd = info->fh;
  xfer.len = size;


  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("%s: returning from 1st full_write (errno=%d)\n", 
	     __FUNCTION__, errno);
    return -errno;
  }

  if (full_write (priv, (void *)buf, size) != size) {
    gprintf  ("%s: returning from 2nd full_write (errno=%d)\n", 
	      __FUNCTION__, errno);
    return -errno;
  }


  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("%s: returning from 1st full_read\n", __FUNCTION__);
    return -errno;
  }

  if (xfer.remote_ret <= 0) {
    gprintf ("xfer.remote_ret = %d\n", xfer.remote_ret);
    return -xfer.remote_errno;
  }

  return xfer.remote_ret;
}

static int
glusterfs_statfs (const char *path,
		  struct statvfs *buf)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  //  FUNCTION_CALLED;

  xfer.op = OP_STATFS;
  xfer.size = strlen (path) + 1;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("%s: returning from 1st full_write\n", __FUNCTION__);
    return -errno;
  }

  if (full_write (priv, (void *)path, xfer.size) != xfer.size) {
    gprintf ("%s: returning from 2nd full_write\n", __FUNCTION__);
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("%s: returning from 1st full_read\n", __FUNCTION__);
    return -errno;
  }

  if (xfer.remote_ret < 0) {
    gprintf ("xfer.remote_ret = %d\n", xfer.remote_ret);
    return -xfer.remote_errno;
  }

  if (full_read (priv, buf, xfer.size) != xfer.size) {
    gprintf ("%s: 2nd  full_read failed with errno  %d\n",
	     __FUNCTION__, errno);
    return -xfer.remote_errno;
  }
  return 0;
}

static int
glusterfs_flush (const char *path,
		 struct fuse_file_info *info)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  //  FUNCTION_CALLED;

  xfer.op = OP_FLUSH;
  xfer.fd = info->fh;
  xfer.size = 0;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("%s: returning from 1st full_write\n", __FUNCTION__);
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("%s: returning from 1st full_read\n", __FUNCTION__);
    return -errno;
  }

  if (xfer.remote_ret < 0) {
    gprintf ("xfer.remote_ret = %d\n", xfer.remote_ret);
    return -xfer.remote_errno;
  }

  return 0;
}

static int
glusterfs_release (const char *path,
		   struct fuse_file_info *info)
{

  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  xfer.op = OP_RELEASE;
  xfer.size = 0;
  xfer.fd = info->fh;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("%s: returning from 1st full_write\n", __FUNCTION__);
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("%s: returning from 1st full_read\n", __FUNCTION__);
    return -errno;
  }

  return -xfer.remote_ret;
}

static int
glusterfs_fsync (const char *path,
		 int datasync,
		 struct fuse_file_info *info)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  //  FUNCTION_CALLED;

  xfer.op = OP_FSYNC;
  xfer.fd = info->fh;
  xfer.size = 0;
  xfer.flags = datasync;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("%s: returning from 1st full_write\n", __FUNCTION__);
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("%s: returning from 1st full_read\n", __FUNCTION__);
    return -errno;
  }

  if (xfer.remote_ret < 0) {
    gprintf ("xfer.remote_ret = %d\n", xfer.remote_ret);
    return -xfer.remote_errno;
  }

  return 0;
}

static int
glusterfs_setxattr (const char *path,
		    const char *name,
		    const char *value,
		    size_t size,
		    int flags)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
glusterfs_getxattr (const char *path,
		    const char *name,
		    char *value,
		    size_t size)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
glusterfs_listxattr (const char *path,
		     char *list,
		     size_t size)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}
		     
static int
glusterfs_removexattr (const char *path,
		       const char *name)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
glusterfs_opendir (const char *path,
		   struct fuse_file_info *info)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
glusterfs_readdir (const char *path,
		   void *buf,
		   fuse_fill_dir_t fill,
		   off_t offset,
		   struct fuse_file_info *info)
{
  struct dirent *dir;

  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  xfer.op = OP_READDIR;
  xfer.size = strlen (path) + 1;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("%s: 1st full_write failed\n", __FUNCTION__);
    return -EIO;
  }

  if (full_write (priv, path, xfer.size) != xfer.size) {
    gprintf ("%s: 2nd full_write failed\n", __FUNCTION__);
    return -EIO;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("%s: 1st full_read failed\n", __FUNCTION__);
    return -EIO;
  }

  dir = (void *) calloc (xfer.size + 1, 1);
  {
    int ret;
    if ((ret = full_read (priv, (void *)dir, xfer.size)) != xfer.size) {
      gprintf ("%s: 2nd full_read failed xfer.size=%d, returned=%d\n",
	       __FUNCTION__,
	       xfer.size, ret);
      return -EIO;
    }
  }

  {
    int i = 0;
    while (i < (xfer.size / sizeof (struct dirent))) {
      fill (buf, strdup (dir[i].d_name), NULL, 0);
      i++;
    }
  }

  free (dir);

  gprintf ("%s: successfully returning\n", __FUNCTION__);
  errno = 0;
  return 0;
}

static int
glusterfs_releasedir (const char *path,
		      struct fuse_file_info *info)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
glusterfs_fsyncdir (const char *path,
		    int datasync,
		    struct fuse_file_info *info)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static void *
glusterfs_init (void)
{
  FUNCTION_CALLED;
  struct glusterfs_private *_private = (void *) calloc (1, sizeof (*_private));
  _private->addr = inet_addr ("192.168.1.2");
  _private->port = htons (5252);
  _private->sock = -1;
  try_connect (_private);
  return (void *)_private;
}

static void
glusterfs_destroy (void *data)
{
  struct glusterfs_private *priv = data;

  if (priv->sock != -1)
    close (priv->sock);
  free (priv);
  return;
}

static int
glusterfs_access (const char *path,
		  int mode)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
glusterfs_create (const char *path,
		  mode_t mode,
		  struct fuse_file_info *info)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static int
glusterfs_ftruncate (const char *path,
		     off_t offset,
		     struct fuse_file_info *info)
{
  struct xfer_header xfer;
  struct glusterfs_private *priv = fuse_get_context ()->private_data;

  FUNCTION_CALLED;

  xfer.op = OP_FTRUNCATE;
  xfer.fd = info->fh;
  xfer.offset = offset;
  xfer.size = 0;

  if (full_write (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_write\n");
    return -errno;
  }

  if (full_read (priv, (void *)&xfer, sizeof (xfer)) != sizeof (xfer)) {
    gprintf ("returning from 1st full_read\n");
    return -errno;
  }

  if (xfer.remote_ret < 0)
    return -xfer.remote_errno;

  return 0;
}

static int
glusterfs_fgetattr (const char *path,
		    struct stat *buf,
		    struct fuse_file_info *info)
{
  int ret = 0;
  FUNCTION_CALLED;
  return ret;
}

static struct fuse_operations glusterfs_fops = {
  .getattr     = glusterfs_getattr,
  .readlink    = glusterfs_readlink,
  .getdir      = NULL /*glusterfs_getdir */,
  .mknod       = glusterfs_mknod,
  .mkdir       = glusterfs_mkdir,
  .unlink      = glusterfs_unlink,
  .rmdir       = glusterfs_rmdir,
  .symlink     = glusterfs_symlink,
  .rename      = glusterfs_rename,
  .link        = glusterfs_link,
  .chmod       = glusterfs_chmod,
  .chown       = glusterfs_chown,
  .truncate    = glusterfs_truncate,
  .utime       = glusterfs_utime,
  .open        = glusterfs_open,
  .read        = glusterfs_read,
  .write       = glusterfs_write,
  .statfs      = glusterfs_statfs,
  .flush       = glusterfs_flush,
  .release     = glusterfs_release,
  .fsync       = glusterfs_fsync,
  .setxattr    = glusterfs_setxattr,
  .getxattr    = glusterfs_getxattr,
  .listxattr   = glusterfs_listxattr,
  .removexattr = glusterfs_removexattr,
  .opendir     = glusterfs_opendir,
  .readdir     = glusterfs_readdir,
  .releasedir  = glusterfs_releasedir,
  .fsyncdir    = glusterfs_fsyncdir,
  .init        = glusterfs_init,
  .destroy     = glusterfs_destroy,
  .access      = glusterfs_access,
  .create      = NULL /*glusterfs_create */,
  .ftruncate   = glusterfs_ftruncate,
  .fgetattr    = glusterfs_fgetattr
};

int
glusterfs_fops_register (int argc, char *argv[])
{
  return fuse_main (argc, argv, &glusterfs_fops);
}
