/*
   Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#define __XOPEN_SOURCE 500

#include <stdint.h>
#include <sys/time.h>
#include <errno.h>
#include <ftw.h>
#ifndef GF_BSD_HOST_OS
#include <alloca.h>
#endif
#include <sys/resource.h>

#include "glusterfs.h"
#include "dict.h"
#include "logging.h"
#include "posix.h"
#include "xlator.h"
#include "lock.h"
#include "defaults.h"
#include "common-utils.h"


#undef HAVE_SET_FSID
#ifdef HAVE_SET_FSID

#define DECLARE_OLD_FS_UID_VAR int32_t old_fsuid /*, old_fsgid */

#define SET_FS_UID(uid, gid) do {   \
 old_fsuid = setfsuid (uid);                                  \
} while (0)

#define SET_TO_OLD_FS_UID() do {      \
  setfsuid (old_fsuid);                                       \
 /*  setfsgid (old_fsgid);           */                            \
} while (0);

#else

#define DECLARE_OLD_FS_UID_VAR
#define SET_FS_UID(uid, gid)
#define SET_TO_OLD_FS_UID()

#endif

#define MAKE_REAL_PATH(var, this, path) do {                             \
  int base_len = ((struct posix_private *)this->private)->base_path_length; \
  var = alloca (strlen (path) + base_len + 2);                           \
  ERR_ABORT (var);							\
  strcpy (var, ((struct posix_private *)this->private)->base_path);      \
  strcpy (&var[base_len], path);                                         \
} while (0)


int32_t
posix_lookup (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t need_xattr)
{
  struct stat buf = {0, };
  char *real_path;
  int32_t op_ret;
  int32_t op_errno;
  dict_t *xattr = NULL;
  char version[50], ctime[50]; /* do #define the size */

  MAKE_REAL_PATH (real_path, this, loc->path);

  op_ret = lstat (real_path, &buf);
  op_errno = errno;

  if (op_ret == -1 && op_errno != ENOENT) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "lstat on %s: %s", loc->path, strerror (op_errno));
  }

  if (need_xattr && op_ret == 0) {
    xattr = get_new_dict();
    int32_t size = lgetxattr (real_path, GLUSTERFS_VERSION, version, 50);
    /* should size be put into the data_t ? */
    if (size != -1) {
      version[size] = '\0';
      dict_set (xattr, GLUSTERFS_VERSION, 
		data_from_uint32 (strtoll(version, NULL, 10)));
    }
    size = lgetxattr (real_path, GLUSTERFS_CREATETIME, ctime, 50);
    if (size != -1) {
      ctime[size] = '\0';
      dict_set (xattr, GLUSTERFS_CREATETIME, 
		data_from_uint32 (strtoll(ctime, NULL, 10)));
    }
    if ((need_xattr > 0) && buf.st_size <= need_xattr && S_ISREG (buf.st_mode)) {
      char *databuf = NULL;
      data_t *databuf_data = NULL;
      int fd = open (real_path, O_RDONLY);
      int ret;

      databuf = malloc (buf.st_size);
      ERR_ABORT (databuf);
      if (!databuf) {
	/* log */
      }
      if (fd == -1) {
	gf_log (this->name, GF_LOG_ERROR, "open (%s) => -1/%d",
		real_path, errno);
      }
      ret = read (fd, databuf, buf.st_size);
      close (fd);
      databuf_data = bin_to_data (databuf, buf.st_size);
      databuf_data->is_static = 0;

      dict_set (xattr, "glusterfs.content", databuf_data);
    }
  }

  frame->root->rsp_refs = NULL;
  if (xattr)
    dict_ref (xattr);
  STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &buf, xattr);
  if (xattr)
    dict_unref (xattr);
  return 0;
}


int32_t
posix_forget (call_frame_t *frame,
	      xlator_t *this,
	      inode_t *inode)
{
  if (dict_get (inode->ctx, this->name)) {
    int32_t _fd = data_to_int32 (dict_get (inode->ctx, this->name));
    close (_fd);
  }
  return 0;
}

int32_t
posix_stat (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  struct stat buf;
  char *real_path;
  int32_t op_ret;
  int32_t op_errno;
  DECLARE_OLD_FS_UID_VAR;

  MAKE_REAL_PATH (real_path, this, loc->path);

  SET_FS_UID (frame->root->uid, frame->root->gid);

  op_ret = lstat (real_path, &buf);
  op_errno = errno;

  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "lstat on %s: %s", loc->path, strerror (op_errno));
  }

  SET_TO_OLD_FS_UID();  

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &buf);

  return 0;
}



int32_t 
posix_opendir (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc, 
	       fd_t *fd)
{
  char *real_path;
  int32_t op_ret;
  int32_t op_errno;
  DIR *dir;

  DECLARE_OLD_FS_UID_VAR;

  MAKE_REAL_PATH (real_path, this, loc->path);

  SET_FS_UID (frame->root->uid, frame->root->gid);
  
  dir = opendir (real_path);
  op_errno = errno;
  op_ret = (dir == NULL) ? -1 : dirfd (dir);
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "opendir on %s: %s", loc->path, strerror (op_errno));
  }
  
  SET_TO_OLD_FS_UID ();
  
  if (dir) {
    struct posix_fd *pfd = calloc (1, sizeof (*fd));
    if (!pfd) {
      closedir (dir);
      STACK_UNWIND (frame, -1, ENOMEM, NULL);
    }

    pfd->dir = dir;
    pfd->fd = dirfd (dir);
    pfd->path = strdup (real_path);
    dict_set (fd->ctx, this->name, data_from_static_ptr (pfd));
  }

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, fd);

  return 0;
}


int32_t
posix_getdents (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		size_t size,
		off_t off,
		int32_t flag)
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
  data_t *pfd_data = NULL;
  struct posix_fd *pfd;
  DECLARE_OLD_FS_UID_VAR ;

  if (fd && fd->ctx) {
    pfd_data = dict_get (fd->ctx, this->name);
    if (!pfd_data) {
      frame->root->rsp_refs = NULL;
      gf_log (this->name, GF_LOG_ERROR, "fd %p does not have context in %s",
	      fd, this->name);
      STACK_UNWIND (frame, -1, EBADFD, &entries, 0);
      return 0;
    }
  } else {
    gf_log (this->name, GF_LOG_ERROR, "fd or fd->ctx is NULL (fd=%p)", fd);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADFD, &entries, 0);
    return 0;
  }

  pfd = data_to_ptr (pfd_data);

  if (!pfd) {
    gf_log (this->name, GF_LOG_ERROR, "pfd from fd->ctx for %s is NULL", fd);
    STACK_UNWIND (frame, -1, EBADFD, NULL, 0);
    return 0;
  }

  if (!pfd->path) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd does not have path set (possibly file fd, fd=%p)", fd);
    STACK_UNWIND (frame, -1, EBADFD, NULL, 0);
    return 0;
  }

  real_path = pfd->path;
  real_path_len = strlen (real_path);
  entry_path_len = real_path_len + 1024;
  entry_path = calloc (1, entry_path_len);
  ERR_ABORT (entry_path);
  strcpy (entry_path, real_path);
  entry_path[real_path_len] = '/';

  SET_FS_UID (frame->root->uid, frame->root->gid);
  
  dir = pfd->dir;
  
  if (!dir) {
    gf_log (this->name, GF_LOG_ERROR, 
	    "pfd does not have dir set (possibly file fd, fd=%p, path=`%s'",
	    fd, real_path);

    SET_TO_OLD_FS_UID ();

    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, errno, &entries, 0);
    FREE (entry_path);
    return 0;
  } else {
    op_ret = 0;
    op_errno = 0;
  }

  //  seekdir (dir, 0);

  /* TODO: check for all the type of flag, and behave appropriately */

  while ((dirent = readdir (dir))) {
    if (!dirent)
      break;

    /* This helps in self-heal, when only directories needs to be replicated */
    
    /* This is to reduce the network traffic, in case only directory is needed from posix */
    struct stat buf;
    int ret = -1;
    char tmp_real_path[4096];
    strcpy(tmp_real_path, real_path);
    strcat (tmp_real_path, "/");
    strcat(tmp_real_path, dirent->d_name);
    ret = lstat (tmp_real_path, &buf);

    if ((flag == GF_GET_DIR_ONLY) && (ret != -1 && !S_ISDIR(buf.st_mode))) {
      continue;
    }

    tmp = calloc (1, sizeof (*tmp));
    ERR_ABORT (tmp);
    tmp->name = strdup (dirent->d_name);
    if (entry_path_len < real_path_len + 1 + strlen (tmp->name) + 1) {
      entry_path_len = real_path_len + strlen (tmp->name) + 1024;
      entry_path = realloc (entry_path, entry_path_len);
      ERR_ABORT (entry_path);
    }
    strcpy (&entry_path[real_path_len+1], tmp->name);
    lstat (entry_path, &tmp->buf);
    if (S_ISLNK(tmp->buf.st_mode)) {
      char linkpath[PATH_MAX];
      ret = readlink (entry_path, linkpath, PATH_MAX);
      if (ret != -1) {
	linkpath[ret] = '\0';
	tmp->link = strdup (linkpath);
      }
    } else {
      tmp->link = "";
    }

    count++;

    tmp->next = entries.next;
    entries.next = tmp;
    /* if size is 0, count can never be = size, so entire dir is read */
    if (count == size)
      break;
  }
  FREE (entry_path);

  SET_TO_OLD_FS_UID ();
  
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &entries, count);
  while (entries.next) {
    tmp = entries.next;
    entries.next = entries.next->next;
    FREE (tmp->name);
    FREE (tmp);
  }
  return 0;
}


int32_t 
posix_closedir (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd)
{
  int32_t op_ret;
  int32_t op_errno;
  data_t *pfd_data;
  struct posix_fd *pfd;

  op_ret = 0;
  op_errno = errno;

  frame->root->rsp_refs = NULL;

  if (!fd) {
    gf_log (this->name, GF_LOG_ERROR, "fd is NULL");
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }

  if (!fd->ctx) {
    gf_log (this->name, GF_LOG_ERROR, "fd->ctx is NULL for fd=%p", fd);
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }

  pfd_data = dict_get (fd->ctx, this->name);

  if (!pfd_data) {
    gf_log (this->name, GF_LOG_ERROR, "pfd_data from fd=%p is NULL", fd);
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }

  pfd = data_to_ptr (pfd_data);

  if (!pfd) {
    gf_log (this->name, GF_LOG_ERROR, "pfd is NULL from fd=%p", fd);
    STACK_UNWIND (frame, -1, EINVAL);
    return 0;
  }

  if (pfd->dir) {
    closedir (pfd->dir);
    pfd->dir = NULL;
  } else {
    gf_log (this->name, GF_LOG_ERROR, "pfd->dir is NULL for fd=%p path=%s",
	    fd, pfd->path ? pfd->path : "<NULL>");
  }

  if (pfd->path) {
    free (pfd->path);
  } else {
    gf_log (this->name, GF_LOG_ERROR, "pfd->path was NULL. fd=%p pfd=%p",
	    fd, pfd);
  }

  dict_del (fd->ctx, this->name);
  free (pfd);

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}


int32_t 
posix_readlink (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		size_t size)
{
  char *dest = NULL;
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  DECLARE_OLD_FS_UID_VAR;

  dest = alloca (size + 1);
  ERR_ABORT (dest);


  MAKE_REAL_PATH (real_path, this, loc->path);

  SET_FS_UID (frame->root->uid, frame->root->gid);
  
  op_ret = readlink (real_path, dest, size);
  if (op_ret > 0) 
    dest[op_ret] = 0;
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "readlink on %s: %s", loc->path, strerror (op_errno));
  }
    
  SET_TO_OLD_FS_UID ();

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, dest);

  return 0;
}

int32_t 
posix_mknod (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode,
	     dev_t dev)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf = { 0, };
  DECLARE_OLD_FS_UID_VAR;

  MAKE_REAL_PATH (real_path, this, loc->path);

  SET_FS_UID (frame->root->uid, frame->root->gid);
  
  op_ret = mknod (real_path, mode, dev);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "mknod on %s: %s", loc->path, strerror (op_errno));
  }
  
  if (op_ret == 0) {
#ifndef HAVE_SET_FSID
    lchown (real_path, frame->root->uid, frame->root->gid);
#endif
    lstat (real_path, &stbuf);
  }
  
  SET_TO_OLD_FS_UID ();

  frame->root->rsp_refs = NULL;  
  STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &stbuf);

  return 0;
}

int32_t 
posix_mkdir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf = {0, };
  DECLARE_OLD_FS_UID_VAR;

  MAKE_REAL_PATH (real_path, this, loc->path);

  SET_FS_UID (frame->root->uid, frame->root->gid);
  
  op_ret = mkdir (real_path, mode);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "mkdir of %s: %s", loc->path, strerror (op_errno));
  }
  
  if (op_ret == 0) {
#ifndef HAVE_SET_FSID
    chown (real_path, frame->root->uid, frame->root->gid);
#endif
    lstat (real_path, &stbuf);
  }
  
  SET_TO_OLD_FS_UID ();
  
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &stbuf);

  return 0;
}


int32_t 
posix_unlink (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  
  DECLARE_OLD_FS_UID_VAR;

  MAKE_REAL_PATH (real_path, this, loc->path);
  /*
  _fd = open (real_path, O_RDWR);
  */
  SET_FS_UID (frame->root->uid, frame->root->gid);
  
  op_ret = unlink (real_path);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "unlink of %s: %s", loc->path, strerror (op_errno));
  }
  
  SET_TO_OLD_FS_UID ();

  /*
  if (op_ret == -1) {
    close (_fd);
  } else {
    dict_set (loc->inode->ctx, this->name, data_from_int32 (_fd));
  }
  */

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

int32_t 
posix_remove (const char *path, 
	      const struct stat *stat, 
	      int32_t typeflag, 
	      struct FTW *ftw)
{
  return remove (path);
}

int32_t
posix_rmelem (call_frame_t *frame,
	      xlator_t *this,
	      const char *path)
{
  int32_t op_ret, op_errno;
  char *real_path;

  MAKE_REAL_PATH (real_path, this, path);
  op_ret = nftw (real_path, posix_remove, 20, FTW_DEPTH|FTW_PHYS);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "nftw on %s: %s", path, strerror (op_errno));
  }
  /* FTW_DEPTH = traverse subdirs first before calling posix_remove
   * on real_path
   * FTW_PHYS = do not follow symlinks
   */
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

int32_t 
posix_rmdir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;

  DECLARE_OLD_FS_UID_VAR;

  MAKE_REAL_PATH (real_path, this, loc->path);
  /*
  _fd = open (real_path, O_DIRECTORY | O_RDONLY);
  */
  SET_FS_UID (frame->root->uid, frame->root->gid);
  
  op_ret = rmdir (real_path);
  op_errno = errno;
  if (op_ret == -1 && op_errno != ENOTEMPTY) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "rmdir of %s: %s", loc->path, strerror (op_errno));
  }
    
  SET_TO_OLD_FS_UID ();

  /*
  if (op_ret == -1) {
    close (_fd);
  } else {
    dict_set (loc->inode->ctx, this->name, data_from_int32 (_fd));
  }
  */

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

int32_t 
posix_symlink (call_frame_t *frame,
	       xlator_t *this,
	       const char *linkname,
	       loc_t *loc)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf = { 0, };
  DECLARE_OLD_FS_UID_VAR;

  MAKE_REAL_PATH (real_path, this, loc->path);

  SET_FS_UID (frame->root->uid, frame->root->gid);
    
  op_ret = symlink (linkname, real_path);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "symlink of %s --> %s: %s", 
	    loc->path, linkname, strerror (op_errno));
  }
  
  if (op_ret == 0) {
#ifndef HAVE_SET_FS_ID
    lchown (real_path, frame->root->uid, frame->root->gid);
#endif
    lstat (real_path, &stbuf);
  }
    
  SET_TO_OLD_FS_UID ();

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &stbuf);

  return 0;
}

int32_t 
posix_rename (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *oldloc,
	      loc_t *newloc)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_oldpath;
  char *real_newpath;
  struct stat stbuf = {0, };
  DECLARE_OLD_FS_UID_VAR;

  MAKE_REAL_PATH (real_oldpath, this, oldloc->path);
  MAKE_REAL_PATH (real_newpath, this, newloc->path);

  SET_FS_UID (frame->root->uid, frame->root->gid);
  
  op_ret = rename (real_oldpath, real_newpath);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "rename of %s -> %s: %s", 
	    oldloc->path, newloc->path, strerror (op_errno));
  }
    
  if (op_ret == 0) {
    lstat (real_newpath, &stbuf);
  }

  SET_TO_OLD_FS_UID ();

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}

int32_t 
posix_link (call_frame_t *frame, 
	    xlator_t *this,
	    loc_t *oldloc,
	    const char *newpath)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_oldpath;
  char *real_newpath;
  struct stat stbuf = {0, };
  DECLARE_OLD_FS_UID_VAR;

  MAKE_REAL_PATH (real_oldpath, this, oldloc->path);
  MAKE_REAL_PATH (real_newpath, this, newpath);

  SET_FS_UID (frame->root->uid, frame->root->gid);
    
  op_ret = link (real_oldpath, real_newpath);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "link on %s -> %s: %s", 
	    oldloc->path, newpath, strerror (op_errno));
  }
    
  if (op_ret == 0) {
#ifndef HAVE_SET_FSID
    lchown (real_newpath, frame->root->uid, frame->root->gid);
#endif
    lstat (real_newpath, &stbuf);
  }
    
  SET_TO_OLD_FS_UID ();

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, oldloc->inode, &stbuf);

  return 0;
}


int32_t 
posix_chmod (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf;
  DECLARE_OLD_FS_UID_VAR;
  
  MAKE_REAL_PATH (real_path, this, loc->path);

  SET_FS_UID (frame->root->uid, frame->root->gid);
  
  op_ret = chmod (real_path, mode);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "chmod on %s: %s", loc->path, strerror (op_errno));
  }
    
  if (op_ret == 0)
    lstat (real_path, &stbuf);
    
  SET_TO_OLD_FS_UID ();

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}


int32_t 
posix_chown (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     uid_t uid,
	     gid_t gid)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf;
  DECLARE_OLD_FS_UID_VAR;

  MAKE_REAL_PATH (real_path, this, loc->path);

  SET_FS_UID (frame->root->uid, frame->root->gid);
    
  op_ret = lchown (real_path, uid, gid);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "chown on %s: %s", loc->path, strerror (op_errno));
  }
    
  if (op_ret == 0)
    lstat (real_path, &stbuf);
    
  SET_TO_OLD_FS_UID ();

  frame->root->rsp_refs = NULL;  
  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}


int32_t 
posix_truncate (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		off_t offset)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf;
  DECLARE_OLD_FS_UID_VAR;

  MAKE_REAL_PATH (real_path, this, loc->path);

  SET_FS_UID (frame->root->uid, frame->root->gid);

  op_ret = truncate (real_path, offset);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "truncate of %s: %s", loc->path, strerror (op_errno));
  }
    
  if (op_ret == 0) {
    lstat (real_path, &stbuf);
  }
    
  SET_TO_OLD_FS_UID ();

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}


int32_t 
posix_utimens (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       struct timespec ts[2])
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf = {0, };
  struct timeval tv[2];
  DECLARE_OLD_FS_UID_VAR;
  
  MAKE_REAL_PATH (real_path, this, loc->path);

  /* TODO: fix timespec to timeval converstion 
   * Done: Check if this is correct */

  tv[0].tv_sec = ts[0].tv_sec;
  tv[0].tv_usec = ts[0].tv_nsec / 1000;
  tv[1].tv_sec = ts[1].tv_sec;
  tv[1].tv_usec = ts[1].tv_nsec / 1000;

  SET_FS_UID (frame->root->uid, frame->root->gid);
    
  op_ret = lutimes (real_path, tv);
  if (op_ret == -1 && errno == ENOSYS) {
    op_ret = utimes (real_path, tv);
  }
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "utimes on %s: %s", loc->path, strerror (op_errno));
  }

  if (op_ret == 0)
    lstat (real_path, &stbuf);
 
  SET_TO_OLD_FS_UID ();

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}

int32_t 
posix_create (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t flags,
	      mode_t mode,
	      fd_t *fd)
{
  int32_t op_ret = -1;
  int32_t op_errno = 0;
  int32_t _fd;
  char *real_path;
  struct stat stbuf = {0, };
  DECLARE_OLD_FS_UID_VAR;

  MAKE_REAL_PATH (real_path, this, loc->path);

  frame->root->rsp_refs = NULL;
  SET_FS_UID (frame->root->uid, frame->root->gid);
    
  if (!flags) {
    _fd = open (real_path, 
		O_CREAT|O_RDWR|O_EXCL,
		mode);
  } else {
    _fd = open (real_path, 
		flags|O_CREAT,
		mode);
  }

  op_errno = errno;
  if (_fd == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "open on %s: %s", loc->path, strerror (op_errno));
  }
    
  if (_fd >= 0) {
      /* trigger readahead in the kernel */
#if 0
    char buf[1024 * 64];
    read (_fd, buf, 1024 * 64);
    lseek (_fd, 0, SEEK_SET);
#endif
#ifndef HAVE_SET_FSID
    chown (real_path, frame->root->uid, frame->root->gid);
#endif
    fstat (_fd, &stbuf);
  }
  SET_TO_OLD_FS_UID ();

  if (_fd >= 0) {
    struct posix_fd *pfd;

    pfd = calloc (1, sizeof (*pfd));

    if (!pfd) {
      close (_fd);
      STACK_UNWIND (frame, -1, ENOMEM, fd, loc->inode, &stbuf);
      return 0;
    }

    pfd->flags = flags;
    pfd->fd = _fd;
    dict_set (fd->ctx, this->name, data_from_static_ptr (pfd));
    ((struct posix_private *)this->private)->stats.nr_files++;
    op_ret = 0;
  }

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, fd, loc->inode, &stbuf);

  return 0;
}

int32_t 
posix_open (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t flags,
	    fd_t *fd)
{
  int32_t op_ret = -1;
  int32_t op_errno = 0;
  char *real_path;
  int32_t _fd;
  DECLARE_OLD_FS_UID_VAR;

  MAKE_REAL_PATH (real_path, this, loc->path);

  SET_FS_UID (frame->root->uid, frame->root->gid);
    
  _fd = open (real_path, flags, 0);
  op_errno = errno;
  if (_fd == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "open on %s: %s", loc->path, strerror (op_errno));
  }
    
  SET_TO_OLD_FS_UID ();

  if (_fd >= 0) {
    struct posix_fd *pfd = calloc (1, sizeof (*pfd));

    if (!pfd) {
      close (_fd);
      STACK_UNWIND (frame, -1, ENOMEM, fd);
      return 0;
    }

    pfd->flags = flags;
    pfd->fd = _fd;
    dict_set (fd->ctx, this->name, data_from_static_ptr (pfd));

    ((struct posix_private *)this->private)->stats.nr_files++;
    op_ret = 0;
#ifndef HAVE_SET_FSID
    if (flags & O_CREAT)
      chown (real_path, frame->root->uid, frame->root->gid);
#endif
  }

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, fd);

  return 0;
}

int32_t 
posix_readv (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     size_t size,
	     off_t offset)
{
  int32_t op_ret = -1;
  int32_t op_errno = 0;
  char *buf = NULL, *alloc_buf = NULL;
  int32_t _fd;
  struct posix_private *priv = this->private;
  dict_t *reply_dict = NULL;
  struct iovec vec;
  data_t *pfd_data;
  struct posix_fd *pfd;
  struct stat stbuf = {0,};
  int32_t align = 1;

  frame->root->rsp_refs = NULL;
  pfd_data = dict_get (fd->ctx, this->name);

  if (pfd_data == NULL) {
    gf_log (this->name, GF_LOG_ERROR, "pfd_data NULL from fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF, &vec, 0, &stbuf);
    return 0;
  }

  pfd = data_to_ptr (pfd_data);

  if (!pfd) {
    gf_log (this->name, GF_LOG_ERROR, "pfd is NULL from fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF, &vec, 0, &stbuf);
    return 0;
  }

  if (!size) {
    gf_log (this->name, GF_LOG_WARNING, "size == 0");
    STACK_UNWIND (frame, 0, 0, &vec, 0, &stbuf);
    return 0;
  }

  if (pfd->flags & O_DIRECT) {
    align = 4096;
  }

  alloc_buf = malloc (1 * (size + align));
  if (!alloc_buf) {
    gf_log (this->name, GF_LOG_ERROR,
	    "unable to allocate read buffer of %d + %d bytes",
	    size, align);
    STACK_UNWIND (frame, -1, ENOMEM, &vec, 0, &stbuf);
    return 0;
  }

 /* page aligned buffer */
  buf = (void *)((unsigned long)(alloc_buf + align - 1) &
		 (unsigned long)(~(align - 1)));

  _fd = pfd->fd;

  priv->read_value += size;
  priv->interval_read += size;

  if (lseek (_fd, offset, SEEK_SET) == -1) {
    frame->root->rsp_refs = NULL;
    gf_log (this->name, GF_LOG_ERROR, "lseek(%"PRId64") failed", offset); 
    STACK_UNWIND (frame, -1, errno, &vec, 0, &stbuf);
    return 0;
  }
  
  op_ret = read (_fd, buf, size);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "read failed: %s", strerror (op_errno));
  }

  vec.iov_base = buf;
  vec.iov_len = op_ret;
    
  if (op_ret >= 0) {
    data_t *buf_data = get_new_data ();
    reply_dict = get_new_dict ();

    reply_dict->is_locked = 1;
    buf_data->is_locked = 1;
    buf_data->data = alloc_buf;
    buf_data->len = op_ret;

    dict_set (reply_dict, NULL, buf_data);
    frame->root->rsp_refs = dict_ref (reply_dict);
    /* readv successful, we also need to get the stat of the file we read from */
    fstat (_fd, &stbuf);
  }

  STACK_UNWIND (frame, op_ret, op_errno, &vec, 1, &stbuf);

  if (reply_dict)
    dict_unref (reply_dict);
  return 0;
}


int32_t 
posix_writev (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      struct iovec *vector,
	      int32_t count,
	      off_t offset)
{
  int32_t op_ret;
  int32_t op_errno = 0;
  int32_t _fd;
  struct posix_private *priv = this->private;
  data_t *pfd_data = dict_get (fd->ctx, this->name);
  struct posix_fd *pfd;
  struct stat stbuf = {0,};

  frame->root->rsp_refs = NULL;

  if (pfd_data == NULL) {
    gf_log (this->name, GF_LOG_ERROR, "pfd_data is NULL from fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF, &stbuf);
    return 0;
  }
  
  pfd = data_to_ptr (pfd_data); 

  if (!pfd) {
    gf_log (this->name, GF_LOG_ERROR, "pfd is NULL from fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF, &stbuf);
    return 0;
  }

  _fd = pfd->fd;

  if (lseek (_fd, offset, SEEK_SET) == -1) {
    gf_log (this->name, GF_LOG_ERROR, "lseek(%"PRId64") failed", offset);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, errno, &stbuf);
    return 0;
  }

  /* Check for the O_DIRECT flag during open() */
  if (pfd->flags & O_DIRECT) {
    /* This is O_DIRECT'd file */
    int32_t idx = 0;
    int32_t align = 4096;
    int32_t max_buf_size = 0;
    int32_t retval = 0;
    char *buf = NULL; 
    char *alloc_buf = NULL;
    if (offset % align) {
      /* Return EINVAL */
      gf_log (this->name, GF_LOG_ERROR, "O_DIRECT: offset is Invalid");
      frame->root->rsp_refs = NULL;
      STACK_UNWIND (frame, -1, EINVAL, &stbuf);
      return 0; 
    }
    
    op_ret = 0;
    for (idx = 0; idx < count; idx++) {
      if (max_buf_size < vector[idx].iov_len)
	max_buf_size = vector[idx].iov_len;
    }
    alloc_buf = malloc (1 * (max_buf_size + align));
    if (!alloc_buf) {
      gf_log (this->name, GF_LOG_ERROR,
	      "unable to allocate read buffer of %d + %d bytes",
	      vector[idx].iov_len, align);
      STACK_UNWIND (frame, -1, ENOMEM, &stbuf);
      return 0;
    }

    for (idx = 0; idx < count; idx++) {
      /* page aligned buffer */
      buf = (void *)((unsigned long)(alloc_buf + align - 1) &
		     (unsigned long)(~(align - 1)));

      memcpy (buf, vector[idx].iov_base, vector[idx].iov_len);
      
      /* not sure whether writev works on O_DIRECT'd fd */
      retval = write (_fd, buf, vector[idx].iov_len);
      
      if (retval == -1) {
	op_ret = -1;
	op_errno = errno;
	if (op_ret == -1) {
	  gf_log (this->name, GF_LOG_WARNING, 
		  "O_DIRECT enabled: %s", strerror (op_errno));
	}

	break;
      }
      op_ret += retval;
    }
    /* Free the allocated aligned buffer */
    free (alloc_buf);

  } else /* if (O_DIRECT) */ {

    /* This is not O_DIRECT'd fd */
    op_ret = writev (_fd, vector, count);
    op_errno = errno;
    if (op_ret == -1) {
      gf_log (this->name, GF_LOG_WARNING, "writev failed: %s", strerror (op_errno));
    }
  }

  priv->write_value += op_ret;
  priv->interval_write += op_ret;
  
  if (op_ret >= 0) {
    /* wiretv successful, we also need to get the stat of 
     * the file we wrote to 
     */
    fstat (_fd, &stbuf);
  }

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}


int32_t 
posix_statfs (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  char *real_path;
  int32_t op_ret = 0;
  int32_t op_errno = 0;
  struct statvfs buf = {0, };
  struct posix_private *priv = this->private;
  
  MAKE_REAL_PATH (real_path, this, loc->path);

  op_ret = statvfs (real_path, &buf);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_ERROR, "statvfs: %s", strerror (op_errno));
  }
  
  if (!priv->export_statfs) {
    buf.f_blocks = 0;
    buf.f_bfree  = 0;
    buf.f_bavail = 0;
    buf.f_files  = 0;
    buf.f_ffree  = 0;
    buf.f_favail = 0;
    
  }

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &buf);
  return 0;
}


int32_t 
posix_flush (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd)
{
  int32_t op_ret = 0;
  int32_t op_errno = 0;
  int32_t _fd;
  data_t *pfd_data = dict_get (fd->ctx, this->name);
  struct posix_fd *pfd;

  if (pfd_data == NULL) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd_data is NULL on fd=%p", fd);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  pfd = data_to_ptr (pfd_data);

  if (!pfd) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd is NULL on fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  _fd = pfd->fd;
  /* do nothing */

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

int32_t 
posix_close (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd)
{
  int32_t op_ret;
  int32_t op_errno;
  int32_t _fd;
  struct posix_private *priv = this->private;
  data_t *pfd_data = dict_get (fd->ctx, this->name);
  struct posix_fd *pfd;

  priv->stats.nr_files--;

  frame->root->rsp_refs = NULL;

  if (pfd_data == NULL) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd_data is NULL from fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  pfd = data_to_ptr (pfd_data);
  if (!pfd) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd is NULL from fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  _fd = pfd->fd;

  op_ret = close (_fd);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "close(): %s", strerror (op_errno));
  }

  if (pfd->dir) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd->dir is %p (not NULL) for file fd=%p",
	    pfd->dir, fd);
    free (pfd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }
  free (pfd);

  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}


int32_t 
posix_fsync (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     int32_t datasync)
{
  int32_t op_ret = -1;
  int32_t op_errno = ENOSYS;
  int32_t _fd;
  data_t *pfd_data = dict_get (fd->ctx, this->name);
  struct posix_fd *pfd;
  DECLARE_OLD_FS_UID_VAR;

  frame->root->rsp_refs = NULL;

  if (pfd_data == NULL) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd_data is NULL from fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  pfd = data_to_ptr (pfd_data);
  if (!pfd) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd is NULL for fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  _fd = pfd->fd;

  SET_FS_UID (frame->root->uid, frame->root->gid);

  if (datasync) {
    ;
#ifdef HAVE_FDATASYNC
    op_ret = fdatasync (_fd);
#endif
  } else {
    op_ret = fsync (_fd);
    op_errno = errno;
    if (op_ret == -1) {
      gf_log (this->name, GF_LOG_WARNING, "fsync: %s", strerror (op_errno));
    }
  }

  SET_TO_OLD_FS_UID ();

#ifdef GF_DARWIN_HOST_OS
  /* Always return success in case of fsync in MAC OS X */
  op_ret = 0;
#endif 
 
  STACK_UNWIND (frame, op_ret, op_errno);
  
  return 0;
}

int32_t
posix_incver (call_frame_t *frame,
	      xlator_t *this,
	      const char *path)
{
  char *real_path;
  char version[50];
  int32_t size = 0;
  int32_t ver = 0;

  MAKE_REAL_PATH (real_path, this, path);

  size = lgetxattr (real_path, GLUSTERFS_VERSION, version, 50);
  if ((size == -1) && (errno != ENODATA)) {
    gf_log (this->name, GF_LOG_WARNING, "lgetxattr: %s", strerror(errno));
    STACK_UNWIND (frame, -1, errno);
    return 0;
  } else {
    version[size] = '\0';
    ver = strtoll (version, NULL, 10);
  }
  ver++;
  sprintf (version, "%u", ver);
  lsetxattr (real_path, GLUSTERFS_VERSION, version, strlen (version), 0);
  STACK_UNWIND (frame, ver, 0);

  return 0;
}

int32_t 
posix_setxattr (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		dict_t *dict,
		int flags)
{
  int32_t op_ret = 0;
  int32_t op_errno = 0;
  char *real_path;
  data_pair_t *trav = dict->members_list;
  DECLARE_OLD_FS_UID_VAR;

  MAKE_REAL_PATH (real_path, this, loc->path);

  SET_FS_UID (frame->root->uid, frame->root->gid);

  while (trav) {
    if (GF_FILE_CONTENT_REQUEST(trav->key) ) {
      char real_filepath[PATH_MAX] = {0,};
      char *key = NULL;
      int32_t file_fd = -1;

      key = &(trav->key[15]);
      sprintf (real_filepath, "%s/%s", real_path, key);
      if (flags & XATTR_REPLACE) {
	/* if file exists, replace it 
	 * else, error out */
	file_fd = open (real_filepath, O_TRUNC|O_WRONLY);
	if (file_fd != -1) {
	  if (trav->value->len) {
	    op_ret = write (file_fd, trav->value->data, trav->value->len);
	    if (op_ret == -1) {
	      op_errno = errno;
	      gf_log (this->name,
		      GF_LOG_ERROR,
		      "write() while doing setxattr for key %s on path %s", key, loc->path);
	    } else {
	      /* do nothing */
	      op_ret = 0;
	      op_errno = 0;
	    } /* if(file_fd!=-1)...else */
	  } else {
	    op_ret = 0;
	    op_errno = 0;
	  }/* if(file_fd!=-1)...else */
	  close (file_fd);
	} else {
	  op_ret = -1;
	  op_errno = errno;
	  gf_log (this->name,
		  GF_LOG_ERROR,
		  "failed to open file %s with O_TRUNC", key);
	} /* if(file_fd!=-1)...else */
      } else {
	/* we know file doesn't exist, create it */
	file_fd = open (real_filepath, O_CREAT|O_WRONLY);
	if (file_fd != -1) {
	  write (file_fd, trav->value->data, trav->value->len);
	  close (file_fd);
	} else {
	  op_ret = -1;
	  op_errno = errno;
	  gf_log (this->name,
		  GF_LOG_ERROR,
		  "failed to open file %s with O_CREAT", key);
	} /* if(file_fd!=-1)...else */
      }
    } else {
      op_ret = lsetxattr (real_path, 
			  trav->key, 
			  trav->value->data, 
			  trav->value->len, 
			  flags);
      op_errno = errno;
      if ((op_ret == -1) && (op_errno != ENOENT)) {
	gf_log (this->name, GF_LOG_WARNING, 
		"%s: %s", loc->path, strerror (op_errno));
	break;
      }
    } /* if(GF_FILE_CONTENT_REQUEST())...else */
    trav = trav->next;
  } /* while(trav) */

  SET_TO_OLD_FS_UID ();

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

/**
 * posix_getxattr - this function returns a dictionary with all the 
 *                  key:value pair present as xattr. used for both 'listxattr' and
 *                  'getxattr'.
 */
int32_t 
posix_getxattr (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		const char *name)
{
  int32_t op_ret = -1;
  int32_t op_errno = ENOENT;
  int32_t list_offset = 0;
  size_t size = 0;
  size_t remaining_size = 0;
  char key[1024] = {0,};
  char *value = NULL;
  char *list = NULL;
  char *real_path = NULL;
  dict_t *dict = NULL;
  DECLARE_OLD_FS_UID_VAR;

  MAKE_REAL_PATH (real_path, this, loc->path);
  
  /* Get the total size */
  dict = get_new_dict ();

  SET_FS_UID (frame->root->uid, frame->root->gid);
  
  
  if (S_ISDIR(loc->inode->st_mode) && name && GF_FILE_CONTENT_REQUEST(name)) {
    char real_filepath[PATH_MAX] = {0,};
    char *key = NULL;
    int32_t file_fd = -1;
    struct stat stbuf = {0,};
    char *buf = NULL;

    key = (char *)&(name[15]);
    sprintf (real_filepath, "%s/%s", real_path, key);

    lstat (real_filepath, &stbuf);
    file_fd = open (real_filepath, O_RDONLY);
    if (file_fd != -1) {
      buf = calloc (stbuf.st_size + 1, sizeof(char));
      ERR_ABORT (buf);
      op_ret = read (file_fd, buf, stbuf.st_size);
      buf[stbuf.st_size] = '\0';
      dict_set (dict, (char *)name, data_from_dynptr (buf, op_ret));
      close (file_fd);
    } else {
      op_ret = -1;
      op_errno = errno;
      gf_log (this->name,
	      GF_LOG_ERROR,
	      "failed to open file %s with O_TRUNC", key);
    } /* if(file_fd!=-1)...else */
  } else {
    size = llistxattr (real_path, NULL, 0);
    op_errno = errno;
    if (size <= 0) {
      SET_TO_OLD_FS_UID ();
      /* There are no extended attributes, send an empty dictionary */
      
      if (dict) {
	dict_ref (dict);
      }
      if (size == -1 && op_errno != ENODATA) {
	gf_log (this->name, GF_LOG_WARNING, 
		"%s: %s", loc->path, strerror (op_errno));
      }
      
      frame->root->rsp_refs = NULL;    
      STACK_UNWIND (frame, size, op_errno, dict);
      
      if (dict)
	dict_unref (dict);
      
      return 0;
    }

    list = alloca (size + 1);
    ERR_ABORT (list);
    size = llistxattr (real_path, list, size);
    
    remaining_size = size;
    list_offset = 0;
    while (remaining_size > 0) {
      if(*(list+list_offset) == '\0')
	break;
      strcpy (key, list + list_offset);
      op_ret = lgetxattr (real_path, key, NULL, 0);
      if (op_ret == -1)
	break;
      value = calloc (op_ret + 1, sizeof(char));
      ERR_ABORT (value);
      op_ret = lgetxattr (real_path, key, value, op_ret);
      if (op_ret == -1)
	break;
      value [op_ret] = '\0';
      dict_set (dict, key, data_from_dynptr (value, op_ret));
      remaining_size -= strlen (key) + 1;
      list_offset += strlen (key) + 1;
    } /* while (remaining_size > 0) */
    op_ret = size;
  }

  SET_TO_OLD_FS_UID ();
  
  if (dict) {
    dict_ref (dict);
  }

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, dict);

  if (dict)
    dict_unref (dict);
  return 0;
}
		     
int32_t 
posix_removexattr (call_frame_t *frame,
		   xlator_t *this,
		   loc_t *loc,
		   const char *name)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  DECLARE_OLD_FS_UID_VAR;

  MAKE_REAL_PATH (real_path, this, loc->path);
  
  SET_FS_UID (frame->root->uid, frame->root->gid);

  op_ret = lremovexattr (real_path, name);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "removexattr on %s: %s", loc->path, strerror (op_errno));
  }

  SET_TO_OLD_FS_UID ();    

  frame->root->rsp_refs = NULL;  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}


int32_t 
posix_fsyncdir (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		int datasync)
{
  int32_t op_ret;
  int32_t op_errno;
  data_t *pfd_data = dict_get (fd->ctx, this->name);
  struct posix_fd *pfd;
  int32_t _fd;

  frame->root->rsp_refs = NULL;

  if (pfd_data == NULL) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd_data is NULL fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  pfd = data_to_ptr (pfd_data);
  if (!pfd) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd is NULL fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  _fd = pfd->fd;

  op_ret = 0;
  op_errno = errno;

  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}


int32_t 
posix_access (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t mask)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  DECLARE_OLD_FS_UID_VAR;

  MAKE_REAL_PATH (real_path, this, loc->path);

  SET_FS_UID (frame->root->uid, frame->root->gid);
  
  op_ret = access (real_path, mask & 07);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, 
	    "access on %s: %s", loc->path, strerror (op_errno));
  }

  SET_TO_OLD_FS_UID ();

  frame->root->rsp_refs = NULL;  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}


int32_t 
posix_ftruncate (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 off_t offset)
{
  int32_t op_ret;
  int32_t op_errno;
  int32_t _fd = 0;
  struct stat buf;
  data_t *pfd_data = dict_get (fd->ctx, this->name);
  struct posix_fd *pfd;
  DECLARE_OLD_FS_UID_VAR;

  frame->root->rsp_refs = NULL;

  if (pfd_data == NULL) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd_data is NULL fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  pfd = data_to_ptr (pfd_data);
  if (!pfd) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd is NULL fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  _fd = pfd->fd;

  SET_FS_UID (frame->root->uid, frame->root->gid);

  op_ret = ftruncate (_fd, offset);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, "ftruncate: %s", strerror (op_errno));
  }
    
  if (op_ret == 0)
    fstat (_fd, &buf);
  
  SET_TO_OLD_FS_UID ();

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &buf);

  return 0;
}

int32_t 
posix_fchown (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      uid_t uid,
	      gid_t gid)
{
  int32_t op_ret;
  int32_t op_errno;
  int32_t _fd;
  struct stat buf;
  data_t *pfd_data = dict_get (fd->ctx, this->name);
  struct posix_fd *pfd;
  DECLARE_OLD_FS_UID_VAR;

  frame->root->rsp_refs = NULL;

  if (pfd_data == NULL) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd_data is NULL fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  pfd = data_to_ptr (pfd_data);

  if (!pfd) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd is NULL fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  _fd = pfd->fd;

  SET_FS_UID (frame->root->uid, frame->root->gid);

  op_ret = fchown (_fd, uid, gid);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, "fchown: %s", strerror (op_errno));
  }

  if (op_ret == 0)
    fstat (_fd, &buf);

  SET_TO_OLD_FS_UID ();

  STACK_UNWIND (frame, op_ret, op_errno, &buf);

  return 0;
}


int32_t 
posix_fchmod (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      mode_t mode)
{
  int32_t op_ret;
  int32_t op_errno;
  int32_t _fd;
  struct stat buf;
  data_t *pfd_data = dict_get (fd->ctx, this->name);
  struct posix_fd *pfd;
  DECLARE_OLD_FS_UID_VAR;

  if (pfd_data == NULL) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd_data is NULL fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  pfd = data_to_ptr (pfd_data);

  if (!pfd) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd is NULL fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  _fd = pfd->fd;

  SET_FS_UID (frame->root->uid, frame->root->gid);

  op_ret = fchmod (_fd, mode);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, "fchmod: %s", strerror (op_errno));
  }
  if (op_ret == 0)
    fstat (_fd, &buf);
  
  SET_TO_OLD_FS_UID ();

  frame->root->rsp_refs = NULL;  
  STACK_UNWIND (frame, op_ret, op_errno, &buf);

  return 0;
}

int32_t 
posix_setdents (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		int32_t flags,
		dir_entry_t *entries,
		int32_t count)
{
  char *real_path;
  char *entry_path;
  int32_t real_path_len;
  int32_t entry_path_len;
  int32_t ret = 0;
  struct posix_fd *pfd;
  data_t *pfd_data = NULL;
  struct timeval tv[2];

  tv[0].tv_sec = tv[0].tv_usec = 0;
  tv[1].tv_sec = tv[1].tv_usec = 0;

  frame->root->rsp_refs = NULL;

  pfd_data = dict_get (fd->ctx, this->name);
  if (!pfd_data) {
    gf_log (this->name, GF_LOG_ERROR, "fd->ctx not found on fd=%p for %s",
	    fd, this->name);
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }


  pfd = data_to_ptr (pfd_data);

  if (!pfd) {
    gf_log (this->name, GF_LOG_ERROR, "pfd is NULL on fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  real_path = pfd->path;

  if (!real_path) {
    gf_log (this->name, GF_LOG_ERROR,
	    "path is NULL on pfd=%p fd=%p", pfd, fd);
    STACK_UNWIND (frame, -1, EBADFD);
    return 0;
  }

  real_path_len = strlen (real_path);
  entry_path_len = real_path_len + 256;
  entry_path = calloc (1, entry_path_len);

  if (!entry_path) {
    STACK_UNWIND (frame, -1, ENOMEM);
    return 0;
  }

  strcpy (entry_path, real_path);
  entry_path[real_path_len] = '/';

  /* fd exists, and everything looks fine */
  {
    /**
     * create an entry for each one present in '@entries' 
     *  - if flag is set (ie, if its namespace), create both directories and 
     *    files 
     *  - if not set, create only directories.
     *
     *  after the entry is created, change the mode and ownership of the entry
     *  according to the stat present in entries->buf.  
     */
    dir_entry_t *trav = entries->next;
    while (trav) {
      char pathname[4096] = {0,};
      strcpy (pathname, entry_path);
      strcat (pathname, trav->name);

      if (S_ISDIR(trav->buf.st_mode)) {
	/* If the entry is directory, create it by calling 'mkdir'. If 
	 * directory is not present, it will be created, if its present, 
	 * no worries even if it fails.
	 */
	ret = mkdir (pathname, trav->buf.st_mode);
	if (!ret) {
	  gf_log (this->name, 
		  GF_LOG_DEBUG, 
		  "Creating directory %s with mode (0%o)", 
		  pathname,
		  trav->buf.st_mode);
	}
      } else if (flags & GF_SET_IF_NOT_PRESENT || !(flags & GF_SET_DIR_ONLY)) {
	/* Create a 0byte file here */
	if (S_ISREG (trav->buf.st_mode)) {
	  ret = open (pathname, O_CREAT|O_EXCL, trav->buf.st_mode);
	  if ((ret == -1) && (errno != EEXIST)) {
	    gf_log (this->name, GF_LOG_ERROR,
		    "Error creating file %s with mode (0%o): %s",
		    pathname, trav->buf.st_mode, strerror (errno));
	  } else {
	    close (ret);
	  }
	} else if (S_ISLNK(trav->buf.st_mode)) {
	  ret = symlink (trav->link, pathname);
	  if ((ret == -1) && (errno != EEXIST)) {
	    gf_log (this->name, GF_LOG_ERROR,
		    "error creating symlink %s: %s", 
		    pathname, strerror (errno));
	  }
	} else if (S_ISBLK (trav->buf.st_mode) || 
		   S_ISCHR (trav->buf.st_mode) || 
		   S_ISFIFO (trav->buf.st_mode)) {
	  ret = mknod (pathname, trav->buf.st_mode, trav->buf.st_dev);
	  if ((ret == -1) && (errno != EEXIST)) {
	    gf_log (this->name, GF_LOG_ERROR,
		    "error creating device file %s: %s",
		    pathname, strerror (errno));
	  }
	}
      }
      /* TODO: handle another flag, GF_SET_OVERWRITE */

      /* Change the mode */
      chmod (pathname, trav->buf.st_mode);
      /* change the ownership */
      chown (pathname, trav->buf.st_uid, trav->buf.st_gid);
      if (flags & GF_SET_EPOCH_TIME)
	utimes (pathname, tv); /* FIXME check return value */

      /* consider the next entry */
      trav = trav->next;
    }
  }
  //  op_errno = errno;
  
  /* Return success all the time */
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, 0, 0);
  
  FREE (entry_path);
  return 0;
}

int32_t 
posix_fstat (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd)
{
  int32_t _fd;
  int32_t op_ret;
  int32_t op_errno;
  struct stat buf;
  data_t *pfd_data = dict_get (fd->ctx, this->name);
  struct posix_fd *pfd;
  DECLARE_OLD_FS_UID_VAR;

  frame->root->rsp_refs = NULL;

  if (pfd_data == NULL) {
    gf_log (this->name, GF_LOG_ERROR,
	    "fd=%p has no context", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  pfd = data_to_ptr (pfd_data);

  if (!pfd) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd is NULL fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF);
    return 0;
  }

  _fd = pfd->fd;

  SET_FS_UID (frame->root->uid, frame->root->gid);

  op_ret = fstat (_fd, &buf);
  op_errno = errno;
  if (op_ret == -1) {
    gf_log (this->name, GF_LOG_WARNING, "fstat: %s", strerror (op_errno));
  }

  SET_TO_OLD_FS_UID ();

  STACK_UNWIND (frame, op_ret, op_errno, &buf);
  return 0;
}


int32_t 
posix_lk (call_frame_t *frame,
	  xlator_t *this,
	  fd_t *fd,
	  int32_t cmd,
	  struct flock *lock)
{
  struct flock nullock = {0, };
  frame->root->rsp_refs = NULL;
  gf_log (this->name, GF_LOG_ERROR, 
	  "\"features/posix-locks\" translator is not loaded");
  STACK_UNWIND (frame, -1, ENOSYS, &nullock);
  return 0;
}

#define ALIGN(x) (((x) + sizeof (uint64_t) - 1) & ~(sizeof (uint64_t) - 1))

static int32_t
dirent_size (struct dirent *entry)
{
#ifdef GF_DARWIN_HOST_OS
  return ALIGN (24 /* FIX MEEEE!!! */ + entry->d_namlen);
#else
  return ALIGN (24 /* FIX MEEEE!!! */ + entry->d_reclen);
#endif
}

int32_t
posix_readdir (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       size_t size,
	       off_t off)
{
  data_t *pfd_data = dict_get (fd->ctx, this->name);
  struct posix_fd *pfd;
  DIR *dir = NULL;

  frame->root->rsp_refs = NULL;

  if (pfd_data == NULL) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd_data is NULL from fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF, NULL);
    return 0;
  }

  pfd = data_to_ptr (pfd_data);

  if (!pfd) {
    gf_log (this->name, GF_LOG_ERROR,
	    "pfd is NULL for fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF,NULL);
    return 0;
  }

  dir = pfd->dir;

  if (!dir) {
    gf_log (this->name, GF_LOG_ERROR,
	    "dir is NULL for fd=%p", fd);
    STACK_UNWIND (frame, -1, EBADF, NULL);
    return 0;
  }

  {
    char *buf = calloc (size, 1); /* readdir buffer needs 0 padding */
    size_t filled = 0;

    if (!buf) {
      gf_log (this->name, GF_LOG_ERROR,
	      "malloc (%d) returned NULL", size);
      STACK_UNWIND (frame, -1, ENOMEM, NULL);
      return 0;
    }

    /* TODO: verify if offset is where fd is parked at */
    if (!off) {
      rewinddir (dir);
    } else {
      seekdir (dir, off);
    }

    while (filled <= size) {
      gf_dirent_t *this_entry;
      struct dirent *entry;
      off_t in_case;
      int32_t this_size;

      in_case = telldir (dir);
      entry = readdir (dir);
      if (!entry)
	break;

      this_size = dirent_size (entry);

      if (this_size + filled > size) {
	seekdir (dir, in_case);
	break;
      }

      /* TODO - consider endianness here */
      this_entry = (void *)(buf + filled);
      this_entry->d_ino = entry->d_ino;
      this_entry->d_len = entry->d_reclen;

      this_entry->d_off = telldir(dir);

#ifndef GF_SOLARIS_HOST_OS
      this_entry->d_type = entry->d_type;
#endif

#ifdef GF_DARWIN_HOST_OS
      /* d_reclen in Linux == d_namlen in Darwin */
      this_entry->d_len = entry->d_namlen; 
#endif

      strncpy (this_entry->d_name, entry->d_name, this_entry->d_len);

      filled += this_size;
    }

    STACK_UNWIND (frame, filled, 0, buf);
    FREE (buf);
  }

  return 0;
}


int32_t 
posix_stats (call_frame_t *frame,
	     xlator_t *this,
	     int32_t flags)

{
  int32_t op_ret = 0;
  int32_t op_errno = 0;

  struct xlator_stats xlstats = {0, }, *stats = &xlstats;
  struct statvfs buf;
  struct timeval tv;
  struct posix_private *priv = (struct posix_private *)this->private;
  int64_t avg_read = 0;
  int64_t avg_write = 0;
  int64_t _time_ms = 0; 
  DECLARE_OLD_FS_UID_VAR ;

  SET_FS_UID (frame->root->uid, frame->root->gid);
    
  op_ret = statvfs (priv->base_path, &buf);
  op_errno = errno;
    
  SET_TO_OLD_FS_UID ();
  
  stats->nr_files = priv->stats.nr_files;
  stats->nr_clients = priv->stats.nr_clients; /* client info is maintained at FSd */
  stats->free_disk = buf.f_bfree * buf.f_bsize; /* Number of Free block in the filesystem. */
  stats->total_disk_size = buf.f_blocks * buf.f_bsize; /* */
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

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, stats);
  return 0;
}

int32_t 
posix_checksum (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		int32_t flag)
{
  char *real_path;
  DIR *dir;
  struct dirent *dirent;
  uint8_t *file_checksum = NULL;
  uint8_t *dir_checksum = NULL;
  int32_t op_ret = -1;
  int32_t op_errno = 2;
  int32_t i, length = 0;
  MAKE_REAL_PATH (real_path, this, loc->path);

  dir = opendir (real_path);
  
  if (!dir){
    gf_log (this->name, GF_LOG_DEBUG, 
	    "checksum: opendir() failed for `%s'", real_path);
    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, errno, NULL, NULL);
    return 0;
  } 
  op_ret = 0;
  op_errno = 0;
  file_checksum = calloc (1, 4096);
  ERR_ABORT (file_checksum);
  dir_checksum  = calloc (1, 4096);
  ERR_ABORT (dir_checksum);

  while ((dirent = readdir (dir))) {
    struct stat buf;
    char tmp_real_path[4096];
    int ret;

    if (!dirent)
      break;

    length = strlen (dirent->d_name);

    strcpy(tmp_real_path, real_path);
    strcat (tmp_real_path, "/");
    strcat(tmp_real_path, dirent->d_name);
    ret = lstat (tmp_real_path, &buf);

    if (ret == -1)
      continue;

    if (S_ISDIR (buf.st_mode)) {
      for (i = 0; i < length; i++)
	dir_checksum[i] ^= dirent->d_name[i];
    } else {
      for (i = 0; i < length; i++)
	file_checksum[i] ^= dirent->d_name[i];
    }
  }
  closedir (dir);

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, file_checksum, dir_checksum);

  return 0;
}

/**
 * notify - when parent sends PARENT_UP, send CHILD_UP event from here
 */
int32_t
notify (xlator_t *this,
        int32_t event,
        void *data,
        ...)
{
  switch (event)
    {
    case GF_EVENT_PARENT_UP:
      {
	/* Tell the parent that posix xlator is up */
	default_notify (this, GF_EVENT_CHILD_UP, data);
      }
      break;
    default:
      /* */
      break;
    }
  return 0;
}

/**
 * init - 
 */
int32_t 
init (xlator_t *this)
{
  int32_t ret;
  struct stat buf;
  struct rlimit lim;
  struct posix_private *_private = NULL;
  data_t *data = dict_get (this->options, "directory");

  _private = calloc (1, sizeof (*_private));
  ERR_ABORT (_private);


  if (this->children) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "FATAL: storage/posix cannot have subvolumes");
    return -1;
  }

  if (!data) {
    gf_log (this->name, GF_LOG_ERROR,
	    "export directory not specified in spec file");
    return -1;
  }
  umask (000); // umask `masking' is done at the client side
  if (mkdir (data->data, 0777) == 0) {
    gf_log (this->name, GF_LOG_WARNING,
	    "directory '%s' not exists, created", data->data);
  }
  /* Check whether the specified directory exists, if not create it. */
  ret = stat (data->data, &buf);
  if (ret != 0 && !S_ISDIR (buf.st_mode)) {
    gf_log (this->name, GF_LOG_ERROR, 
	    "directory '%s' doesn't exists, Exiting", data->data);
    return -1;
  }
  _private->base_path = strdup (data->data);
  _private->base_path_length = strlen (_private->base_path);

  {
    /* Stats related variables */
    gettimeofday (&_private->init_time, NULL);
    gettimeofday (&_private->prev_fetch_time, NULL);
    _private->max_read = 1;
    _private->max_write = 1;
  }

  _private->export_statfs = 1;
  data = dict_get (this->options, "export-statfs-size");
  if (data) {
    if (!strcasecmp ("no", data->data)) {
      gf_log (this->name, GF_LOG_DEBUG, "'statfs()' returns dummy size");
      _private->export_statfs = 0;
    }
  }

  lim.rlim_cur = 1048576;
  lim.rlim_max = 1048576;

#ifndef GF_DARWIN_HOST_OS
  if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
    gf_log (this->name, GF_LOG_WARNING, "WARNING: Failed to set 'ulimit -n 1048576': %s",
	    strerror(errno));
    lim.rlim_cur = 65536;
    lim.rlim_max = 65536;
  
    if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
      gf_log (this->name, GF_LOG_ERROR, "Failed to set max open fd to 64k: %s", strerror(errno));
    } else {
      gf_log (this->name, GF_LOG_ERROR, "max open fd set to 64k");
    }
  }
#endif

  this->private = (void *)_private;
  return 0;
}

void
fini (xlator_t *this)
{
  struct posix_private *priv = this->private;
  FREE (priv);
  return;
}

struct xlator_mops mops = {
  .stats    = posix_stats,
  .lock     = mop_lock_impl,
  .unlock   = mop_unlock_impl,
  .checksum = posix_checksum,
};

struct xlator_fops fops = {
  .lookup      = posix_lookup,
  .forget      = posix_forget,
  .stat        = posix_stat,
  .opendir     = posix_opendir,
  .readdir     = posix_readdir,
  .closedir    = posix_closedir,
  .readlink    = posix_readlink,
  .mknod       = posix_mknod,
  .mkdir       = posix_mkdir,
  .unlink      = posix_unlink,
  .rmelem      = posix_rmelem,
  .rmdir       = posix_rmdir,
  .symlink     = posix_symlink,
  .rename      = posix_rename,
  .link        = posix_link,
  .chmod       = posix_chmod,
  .chown       = posix_chown,
  .truncate    = posix_truncate,
  .utimens     = posix_utimens,
  .create      = posix_create,
  .open        = posix_open,
  .readv       = posix_readv,
  .writev      = posix_writev,
  .statfs      = posix_statfs,
  .flush       = posix_flush,
  .close       = posix_close,
  .fsync       = posix_fsync,
  .incver      = posix_incver,
  .setxattr    = posix_setxattr,
  .getxattr    = posix_getxattr,
  .removexattr = posix_removexattr,
  .fsyncdir    = posix_fsyncdir,
  .access      = posix_access,
  .ftruncate   = posix_ftruncate,
  .fstat       = posix_fstat,
  .lk          = posix_lk,
  .fchown      = posix_fchown,
  .fchmod      = posix_fchmod,
  .setdents    = posix_setdents,
  .getdents    = posix_getdents,
};
