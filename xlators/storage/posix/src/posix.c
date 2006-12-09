/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>

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

#include "glusterfs.h"
#include "dict.h"
#include "logging.h"
#include "posix.h"
#include "xlator.h"
#include <sys/time.h>

#define MAKE_REAL_PATH(var, this, path) do {                             \
  int base_len = ((struct posix_private *)this->private)->base_path_length; \
  var = alloca (strlen (path) + base_len + 2);                           \
  strcpy (var, ((struct posix_private *)this->private)->base_path);      \
  strcpy (&var[base_len], path);                                         \
} while (0)

static int32_t 
posix_getattr (call_frame_t *frame,
	       xlator_t *this,
	       const char *path)
{
  struct stat stbuf;
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);

  MAKE_REAL_PATH (real_path, this, path);

  op_ret = lstat (real_path, &stbuf);
  op_errno = errno;
  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}


static int32_t 
posix_readlink (call_frame_t *frame,
		xlator_t *this,
		const char *path,
		size_t size)
{
  char *dest = alloca (size + 1);
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (dest);

  MAKE_REAL_PATH (real_path, this, path);

  op_ret = readlink (real_path, dest, size);
  if (op_ret > 0) 
    dest[op_ret] = 0;
  op_errno = errno;
  STACK_UNWIND (frame, op_ret, op_errno, dest);

  return 0;
}

static int32_t 
posix_mknod (call_frame_t *frame,
	     xlator_t *this,
	     const char *path,
	     mode_t mode,
	     dev_t dev)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf = { 0, };

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);


  MAKE_REAL_PATH (real_path, this, path);

  op_ret = mknod (real_path, mode, dev);
  op_errno = errno;

  if (op_ret == 0) {
    lchown (real_path, frame->root->uid, frame->root->gid);
    lstat (real_path, &stbuf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}

static int32_t 
posix_mkdir (call_frame_t *frame,
	     xlator_t *this,
	     const char *path,
	     mode_t mode)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf = {0, };

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);


  MAKE_REAL_PATH (real_path, this, path);

  op_ret = mkdir (real_path, mode);
  op_errno = errno;

  if (op_ret == 0) {
    chown (real_path, frame->root->uid, frame->root->gid);
    lstat (real_path, &stbuf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}


static int32_t 
posix_unlink (call_frame_t *frame,
	      xlator_t *this,
	      const char *path)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);

  MAKE_REAL_PATH (real_path, this, path);

  op_ret = unlink (real_path);
  op_errno = errno;

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}


static int32_t 
posix_rmdir (call_frame_t *frame,
	     struct xlator *this,
	     const char *path)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);

  MAKE_REAL_PATH (real_path, this, path);
  op_ret = rmdir (real_path);
  op_errno = errno;

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}



static int32_t 
posix_symlink (call_frame_t *frame,
	       struct xlator *this,
	       const char *oldpath,
	       const char *newpath)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf = { 0, };

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (oldpath);
  GF_ERROR_IF_NULL (newpath);

  MAKE_REAL_PATH (real_path, this, newpath);

  op_ret = symlink (oldpath, real_path);
  op_errno = errno;

  if (op_ret == 0) {
    lchown (real_path, frame->root->uid, frame->root->gid);
    lstat (real_path, &stbuf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}

static int32_t 
posix_rename (call_frame_t *frame,
	      xlator_t *this,
	      const char *oldpath,
	      const char *newpath)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_oldpath;
  char *real_newpath;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (oldpath);
  GF_ERROR_IF_NULL (newpath);


  MAKE_REAL_PATH (real_oldpath, this, oldpath);
  MAKE_REAL_PATH (real_newpath, this, newpath);

  op_ret = rename (real_oldpath, real_newpath);
  op_errno = errno;
  
  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

static int32_t 
posix_link (call_frame_t *frame, 
	    xlator_t *this,
	    const char *oldpath,
	    const char *newpath)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_oldpath;
  char *real_newpath;
  struct stat stbuf = {0, };

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (oldpath);
  GF_ERROR_IF_NULL (newpath);


  MAKE_REAL_PATH (real_oldpath, this, oldpath);
  MAKE_REAL_PATH (real_newpath, this, newpath);
  op_ret = link (real_oldpath, real_newpath);
  op_errno = errno;

  if (op_ret == 0) {
    lchown (real_newpath, frame->root->uid, frame->root->gid);
    lstat (real_newpath, &stbuf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}


static int32_t 
posix_chmod (call_frame_t *frame,
	     xlator_t *this,
	     const char *path,
	     mode_t mode)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf;
  
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);

  MAKE_REAL_PATH (real_path, this, path);

  op_ret = chmod (real_path, mode);
  op_errno = errno;

  lstat (real_path, &stbuf);

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}


static int32_t 
posix_chown (call_frame_t *frame,
	     xlator_t *this,
	     const char *path,
	     uid_t uid,
	     gid_t gid)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);

  MAKE_REAL_PATH (real_path, this, path);
  op_ret = lchown (real_path, uid, gid);
  op_errno = errno;

  lstat (real_path, &stbuf);

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}


static int32_t 
posix_truncate (call_frame_t *frame,
		xlator_t *this,
		const char *path,
		off_t offset)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);


  MAKE_REAL_PATH (real_path, this, path);
  op_ret = truncate (real_path, offset);
  op_errno = errno;

  lstat (real_path, &stbuf);

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}


static int32_t 
posix_utimes (call_frame_t *frame,
	      xlator_t *this,
	      const char *path,
	      struct timespec *buf)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf = {0, };
  
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);
  
  MAKE_REAL_PATH (real_path, this, path);
  op_ret = utimes (real_path, (struct timeval *)buf);
  op_errno = errno;

  lstat (real_path, &stbuf);

  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}

static int32_t 
posix_create (call_frame_t *frame,
	      xlator_t *this,
	      const char *path,
	      mode_t mode)
{
  int32_t op_ret = -1;
  int32_t op_errno = 0;
  char *real_path;
  struct stat stbuf = {0, };
  dict_t *file_ctx = NULL;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);

  MAKE_REAL_PATH (real_path, this, path);

  int32_t fd = open (real_path, 
		     O_CREAT|O_RDWR|O_EXCL|O_LARGEFILE,
		     mode);
  op_errno = errno;

  if (fd >= 0) {
    /* trigger readahead in the kernel */
    char buf[1024 * 64];
    read (fd, buf, 1024 * 64);
    lseek (fd, 0, SEEK_SET);

    file_ctx = get_new_dict ();
    dict_set (file_ctx, this->name, int_to_data (fd));

    ((struct posix_private *)this->private)->stats.nr_files++;
    op_ret = 0;

    chown (real_path, frame->root->uid, frame->root->gid);
  }

  lstat (real_path, &stbuf);

  STACK_UNWIND (frame, op_ret, op_errno, file_ctx, &stbuf);

  return 0;
}

static int32_t 
posix_open (call_frame_t *frame,
	    xlator_t *this,
	    const char *path,
	    int32_t flags,
	    mode_t mode)
{
  int32_t op_ret = -1;
  int32_t op_errno = 0;
  char *real_path;
  struct stat stbuf = {0, };
  dict_t *file_ctx = NULL;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);

  MAKE_REAL_PATH (real_path, this, path);

  int32_t fd = open (real_path,
		     flags,
		     mode);
  op_errno = errno;

  if (fd >= 0) {
    file_ctx = get_new_dict ();
    dict_set (file_ctx, this->name, int_to_data (fd));

    ((struct posix_private *)this->private)->stats.nr_files++;
    op_ret = 0;

    if (flags & O_CREAT)
      chown (real_path, frame->root->uid, frame->root->gid);
  }

  lstat (real_path, &stbuf);

  STACK_UNWIND (frame, op_ret, op_errno, file_ctx, &stbuf);

  return 0;
}

static int32_t 
posix_read (call_frame_t *frame,
	    xlator_t *this,
	    dict_t *fdctx,
	    size_t size,
	    off_t offset)
{
  int32_t op_ret = -1;
  int32_t op_errno = 0;
  char *buf = alloca (size);
  int fd;
  struct posix_private *priv = this->private;

  buf[0] = '\0';
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (fdctx);

  data_t *fd_data = dict_get (fdctx, this->name);

  if (fd_data == NULL) {
    STACK_UNWIND (frame, -1, EBADF, "");
    return 0;
  }

  fd = data_to_int (fd_data);

  priv->read_value += size;
  priv->interval_read += size;

  if (lseek (fd, offset, SEEK_SET) == -1) {
    STACK_UNWIND (frame, -1, errno, "");
    return 0;
  }

  op_ret = read(fd, buf, size);
  op_errno = errno;

  STACK_UNWIND (frame, op_ret, op_errno, buf);
#if 1
  if ((offset/(4 * 1024 * 2048)) < ((offset+size)/(4 * 1024 * 2048))) {
    //   printf ("reading 2048 pages from %d\n", size+offset);
    readahead (fd, size + offset, 2048);
  }
#endif 
  return 0;
}

static int32_t 
posix_write (call_frame_t *frame,
	     xlator_t *this,
	     dict_t *fdctx,
	     char *buf,
	     size_t size,
	     off_t offset)
{
  int32_t op_ret;
  int32_t op_errno;
  int32_t fd;
  struct posix_private *priv = this->private;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (fdctx);
  
  data_t *fd_data = dict_get (fdctx, this->name);

  if (fd_data == NULL) {
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }
  fd = data_to_int (fd_data);

  priv->write_value += size;
  priv->interval_write += size;

  if (lseek (fd, offset, SEEK_SET) == -1) {
    STACK_UNWIND (frame, -1, errno);
    return 0;
  }

  op_ret = write (fd, buf, size);
  op_errno = errno;
 
  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

static int32_t 
posix_statfs (call_frame_t *frame,
	      xlator_t *this,
	      const char *path)

{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct statvfs buf = {0, };

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);

  MAKE_REAL_PATH (real_path, this, path);

  op_ret = statvfs (real_path, &buf);
  op_errno = errno;

  STACK_UNWIND (frame, op_ret, op_errno, &buf);
  return 0;
}

static int32_t 
posix_flush (call_frame_t *frame,
	     xlator_t *this,
	     dict_t *fdctx)
{
  int32_t op_ret = -1;
  int32_t op_errno = ENOSYS;
  int32_t fd;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (fdctx);

  data_t *fd_data = dict_get (fdctx, this->name);

  if (fd_data == NULL) {
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  fd = data_to_int (fd_data);
  /* do nothing */

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

static int32_t 
posix_release (call_frame_t *frame,
	       xlator_t *this,
	       dict_t *fdctx)
{
  int32_t op_ret;
  int32_t op_errno;
  int32_t fd;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (fdctx);

  struct posix_private *priv = this->private;
  priv->stats.nr_files--;
  
  data_t *fd_data = dict_get (fdctx, this->name);

  if (fd_data == NULL) {
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }
  fd = data_to_int (fd_data);

  dict_del (fdctx, this->name);

  op_ret = close (fd);
  op_errno = errno;

  STACK_UNWIND (frame, op_ret, op_errno);
  dict_destroy (fdctx);

  return 0;
}

static int32_t 
posix_fsync (call_frame_t *frame,
	     xlator_t *this,
	     dict_t *fdctx,
	     int32_t datasync)
{
  int32_t op_ret;
  int32_t op_errno;
  int32_t fd;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (fdctx);

  data_t *fd_data = dict_get (fdctx, this->name);
  if (fd_data == NULL) {
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }
  fd = data_to_int (fd_data);
 
  if (datasync)
    op_ret = fdatasync (fd);
  else
    op_ret = fsync (fd);
  op_errno = errno;
  
  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

static int32_t 
posix_setxattr (call_frame_t *frame,
		xlator_t *this,
		const char *path,
		const char *name,
		const char *value,
		size_t size,
		int flags)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  
  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (name);
  GF_ERROR_IF_NULL (value);


  MAKE_REAL_PATH (real_path, this, path);

  op_ret = lsetxattr (real_path, name, value, size, flags);
  op_errno = errno;

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

static int32_t 
posix_getxattr (call_frame_t *frame,
		xlator_t *this,
		const char *path,
		const char *name,
		size_t size)
{
  int32_t op_ret;
  int32_t op_errno;
  char *value = alloca (size);
  char *real_path;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (name);
  GF_ERROR_IF_NULL (value);

  MAKE_REAL_PATH (real_path, this, path);
  op_ret = lgetxattr (real_path, name, value, size);
  op_errno = errno;

  STACK_UNWIND (frame, op_ret, op_errno, value);
  return 0;
}

static int32_t 
posix_listxattr (call_frame_t *frame,
		 xlator_t *this,
		 const char *path,
		 size_t size)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  char *list = alloca (size);

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (list);

  MAKE_REAL_PATH (real_path, this, path);
  op_ret = llistxattr (real_path, list, size);
  op_errno = errno;

  STACK_UNWIND (frame, op_ret, op_errno, list);

  return 0;
}
		     
static int32_t 
posix_removexattr (call_frame_t *frame,
		   xlator_t *this,
		   const char *path,
		   const char *name)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);
  GF_ERROR_IF_NULL (name);

  MAKE_REAL_PATH (real_path, this, path);
  
  op_ret = lremovexattr (real_path, name);
  op_errno = errno;

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t 
posix_opendir (call_frame_t *frame,
	       xlator_t *this,
	       const char *path)
{
  int32_t op_ret = -1;
  int32_t op_errno = 0;
  char *real_path;
  struct stat stbuf = {0, };
  int32_t fd;
  dict_t *file_ctx = NULL;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);

  MAKE_REAL_PATH (real_path, this, path);

  fd = open (real_path, 
	     O_RDONLY|O_NONBLOCK|O_LARGEFILE|O_DIRECTORY);    
  op_errno = errno;

  if (fd >= 0) {
    file_ctx = get_new_dict ();
    dict_set (file_ctx, this->name, int_to_data (fd));

    ((struct posix_private *)this->private)->stats.nr_files++;
    op_ret = 0;
  }

  lstat (real_path, &stbuf);

  STACK_UNWIND (frame, op_ret, op_errno, file_ctx, &stbuf);

  return 0;
}

static int32_t
posix_readdir (call_frame_t *frame,
	       xlator_t *this,
	       const char *path)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  dir_entry_t entries = {0, };
  dir_entry_t *tmp;
  DIR *dir;
  struct dirent *dirent;
  int real_path_len;
  int entry_path_len;
  char *entry_path;
  int count = 0;

  MAKE_REAL_PATH (real_path, this, path);
  real_path_len = strlen (real_path);
  entry_path_len = real_path_len + 256;
  entry_path = calloc (entry_path_len, 1);
  strcpy (entry_path, real_path);
  entry_path[real_path_len] = '/';

  dir = opendir (real_path);
  
  if (!dir){
    gf_log ("posix", GF_LOG_DEBUG, "posix.c: posix_readdir: failed to do opendir for %s", path);
    STACK_UNWIND (frame, -1, errno, &entries, 0);
    return 0;
  } else {
    op_ret = 0;
    op_errno = 0;
  }

  while ((dirent = readdir (dir))) {
    if (!dirent)
      break;
    tmp = alloca (sizeof (*tmp));
    tmp->name = strdupa (dirent->d_name);
    if (entry_path_len < real_path_len + 1 + strlen (tmp->name) + 1) {
      entry_path_len = real_path_len + strlen (tmp->name) + 256;
      entry_path = realloc (entry_path, entry_path_len);
    }
    strcpy (&entry_path[real_path_len+1], tmp->name);
    lstat (entry_path, &tmp->buf);
    count++;

    tmp->next = entries.next;
    entries.next = tmp;
  }
  free (entry_path);
  closedir (dir);

  STACK_UNWIND (frame, op_ret, op_errno, &entries, count);
  return 0;
}

static int32_t 
posix_releasedir (call_frame_t *frame,
		  xlator_t *this,
		  dict_t *fdctx)
{
  int32_t op_ret;
  int32_t op_errno;
  int32_t fd;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (fdctx);

  struct posix_private *priv = this->private;
  priv->stats.nr_files--;

  data_t *fd_data = dict_get (fdctx, this->name);

  if (fd_data == NULL) {
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }
  fd = data_to_int (fd_data);

  dict_del (fdctx, this->name);

  op_ret = close (fd);
  op_errno = errno;

  STACK_UNWIND (frame, op_ret, op_errno);
  dict_destroy (fdctx);
  return 0;
}

static int32_t 
posix_fsyncdir (call_frame_t *frame,
		xlator_t *this,
		dict_t *fdctx,
		int datasync)
{
  int32_t op_ret;
  int32_t op_errno;
  int32_t fd;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (fdctx);

  data_t *fd_data = dict_get (fdctx, this->name);
  if (fd_data == NULL) {
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }
  fd = data_to_int (fd_data);
 
  if (datasync)
    op_ret = fdatasync (fd);
  else
    op_ret = fsync (fd);
  op_errno = errno;
  
  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}


static int32_t 
posix_access (call_frame_t *frame,
	      xlator_t *this,
	      const char *path,
	      mode_t mode)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (path);

  MAKE_REAL_PATH (real_path, this, path);
  op_ret = access (real_path, mode);
  op_errno = errno;

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t 
posix_ftruncate (call_frame_t *frame,
		 xlator_t *this,
		 dict_t *ctx,
		 off_t offset)
{
  int32_t op_ret;
  int32_t op_errno;
  int32_t fd;
  struct stat buf;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (ctx);

  data_t *fd_data = dict_get (ctx, this->name);

  if (fd_data == NULL) {
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }
  fd = data_to_int (fd_data);

  op_ret = ftruncate (fd, offset);
  op_errno = errno;

  fstat (fd, &buf);

  STACK_UNWIND (frame, op_ret, op_errno, &buf);

  return 0;
}

static int32_t 
posix_fgetattr (call_frame_t *frame,
		xlator_t *this,
		dict_t *ctx)
{
  int32_t fd;
  int32_t op_ret;
  int32_t op_errno;
  struct stat buf;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (ctx);

  data_t *fd_data = dict_get (ctx, this->name);

  if (fd_data == NULL) {
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }
  fd = data_to_int (fd_data);

  op_ret = fstat (fd, &buf);
  op_errno = errno;

  STACK_UNWIND (frame, op_ret, op_errno, &buf);
  return 0;
}

struct lk_pass {
  call_frame_t *frame;
  int fd;
  int cmd;
  struct flock *lock;
};

static void *
lk_thread_fn (void *data)
{
  struct lk_pass *pass = data;
  int32_t op_ret;
  int32_t op_errno;

  op_ret = fcntl (pass->fd, pass->cmd, pass->lock);
  op_errno = errno;

  STACK_UNWIND (pass->frame, op_ret, op_errno, pass->lock);

  free (pass->lock);
  free (pass);

  return 0;
}

static int32_t 
posix_lk (call_frame_t *frame,
	  xlator_t *this,
	  dict_t *ctx,
	  int32_t cmd,
	  struct flock *lock)
{
  int32_t fd;
  int32_t op_ret = -1;
  int32_t op_errno = EINVAL;

  GF_ERROR_IF_NULL (this);
  GF_ERROR_IF_NULL (ctx);

  data_t *fd_data = dict_get (ctx, this->name);

  if (fd_data == NULL) {
    struct flock nullock = {0, };
    STACK_UNWIND (frame, -1, EBADF, &nullock);
    return 0;
  }
  fd = data_to_int (fd_data);

  if (cmd == F_GETLK || cmd == F_SETLK) {
    op_ret = fcntl (fd, cmd, lock);
    op_errno = errno;
  } else {
    pthread_t lk_thread;
    struct flock *newlock = calloc (sizeof (*lock), 1);
    struct lk_pass *pass = calloc (sizeof (*pass), 1);
    *newlock = *lock;
    pass->lock = newlock;
    pass->fd = fd;
    pass->cmd = cmd;

    if (pthread_create (&lk_thread,
			NULL,
			lk_thread_fn,
			(void *)pass) != 0) {
      struct flock nullock = {0, };
      STACK_UNWIND (frame, -1, ENOLCK, &nullock);
    }
    return 0;
  }

  printf ("lk returned: %d (%d)\n", op_ret, op_errno);
  STACK_UNWIND (frame, op_ret, op_errno, lock);
  return 0;
}

static int32_t 
posix_stats (call_frame_t *frame,
	     xlator_t *this,
	     int32_t flags)

{
  int32_t op_ret = 0;
  int32_t op_errno = 0;
  char *real_path;

  struct xlator_stats xlstats, *stats = &xlstats;
  struct statvfs buf;
  struct timeval tv;
  struct posix_private *priv = (struct posix_private *)this->private;
  int64_t avg_read = 0;
  int64_t avg_write = 0;
  int64_t _time_ms = 0; 

  MAKE_REAL_PATH (real_path, this, "/");
  op_ret = statvfs (real_path, &buf);
  op_errno = errno;

  stats->nr_files = priv->stats.nr_files;
  stats->nr_clients = priv->stats.nr_clients; /* client info is maintained at FSd */
  stats->free_disk = buf.f_bfree * buf.f_bsize; // Number of Free block in the filesystem.
  stats->disk_usage = (buf.f_blocks - buf.f_bavail) * buf.f_bsize;

  /* Calculate read and write usage */
  gettimeofday (&tv, NULL);
  
  /* Read */
  _time_ms = (tv.tv_sec - priv->init_time.tv_sec) * 1000 +
             ((tv.tv_usec - priv->init_time.tv_usec) / 1000);

  avg_read = (_time_ms) ? (priv->read_value / _time_ms) : 0; /* KBps */
  avg_write = (_time_ms) ? (priv->write_value / _time_ms) : 0; /* KBps */
  
  _time_ms = (tv.tv_sec - priv->prev_fetch_time.tv_sec) * 1000 +
             ((tv.tv_usec - priv->prev_fetch_time.tv_usec) / 1000);
  if (_time_ms && ((priv->interval_read / _time_ms) > priv->max_read)) {
    priv->max_read = (priv->interval_read / _time_ms);
  }
  if (_time_ms && ((priv->interval_write / _time_ms) > priv->max_write)) {
    priv->max_write = priv->interval_write / _time_ms;
  }

  stats->read_usage = avg_read / priv->max_read;
  stats->write_usage = avg_write / priv->max_write;

  gettimeofday (&(priv->prev_fetch_time), NULL);
  priv->interval_read = 0;
  priv->interval_write = 0;

  STACK_UNWIND (frame, op_ret, op_errno, stats);
  return 0;
}

int32_t 
init (struct xlator *xl)
{
  struct posix_private *_private = calloc (1, sizeof (*_private));

  data_t *directory = dict_get (xl->options, "directory");
  data_t *debug = dict_get (xl->options, "debug");

  if (xl->first_child) {
    gf_log ("storage/posix",
	    GF_LOG_ERROR,
	    "FATAL: storage/posix cannot have subvolumes");
    return -1;
  }

  if (!directory){
    gf_log ("posix", GF_LOG_ERROR, "posix.c->init: export directory not specified in spec file\n");
    exit (1);
  }
  umask (000); // umask `masking' is done at the client side
  if (mkdir (directory->data, 0777) == 0) {
    gf_log ("posix", GF_LOG_NORMAL, "directory specified not exists, created");
  }

  strcpy (_private->base_path, directory->data);
  _private->base_path_length = strlen (_private->base_path);

  _private->is_debug = 0;
  if (debug && strcasecmp (debug->data, "on") == 0) {
    _private->is_debug = 1;
    FUNCTION_CALLED;
    gf_log ("posix", GF_LOG_DEBUG, "Directory: %s", directory->data);
  }

  {
    /* Stats related variables */
    gettimeofday (&_private->init_time, NULL);
    gettimeofday (&_private->prev_fetch_time, NULL);
    _private->max_read = 1;
    _private->max_write = 1;
  }

  xl->private = (void *)_private;
  return 0;
}

void
fini (struct xlator *xl)
{
  struct posix_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  free (priv);
  return;
}

struct xlator_mops mops = {
  .stats = posix_stats
};

struct xlator_fops fops = {
  .getattr     = posix_getattr,
  .readlink    = posix_readlink,
  .mknod       = posix_mknod,
  .mkdir       = posix_mkdir,
  .unlink      = posix_unlink,
  .rmdir       = posix_rmdir,
  .symlink     = posix_symlink,
  .rename      = posix_rename,
  .link        = posix_link,
  .chmod       = posix_chmod,
  .chown       = posix_chown,
  .truncate    = posix_truncate,
  .utimes      = posix_utimes,
  .create      = posix_create,
  .open        = posix_open,
  .read        = posix_read,
  .write       = posix_write,
  .statfs      = posix_statfs,
  .flush       = posix_flush,
  .release     = posix_release,
  .fsync       = posix_fsync,
  .setxattr    = posix_setxattr,
  .getxattr    = posix_getxattr,
  .listxattr   = posix_listxattr,
  .removexattr = posix_removexattr,
  .opendir     = posix_opendir,
  .readdir     = posix_readdir,
  .releasedir  = posix_releasedir,
  .fsyncdir    = posix_fsyncdir,
  .access      = posix_access,
  .ftruncate   = posix_ftruncate,
  .fgetattr    = posix_fgetattr,
  .lk          = posix_lk,
};
