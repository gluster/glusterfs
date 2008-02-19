/*
   Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include "glusterfs.h"
#include "dict.h"
#include "logging.h"
#include "bdb.h"
#include "xlator.h"
#include "lock.h"
#include "defaults.h"
#include "common-utils.h"

#define MAKE_REAL_PATH(var, this, path) do {                              \
  int base_len = ((struct bdb_private *)this->private)->base_path_length; \
  var = alloca (strlen (path) + base_len + 2);                            \
  strcpy (var, ((struct bdb_private *)this->private)->base_path);         \
  strcpy (&var[base_len], path);                                          \
} while (0)


#define MAKE_REAL_PATH_TO_DB(var, this, path) do {                        \
  int base_len = ((struct bdb_private *)this->private)->base_path_length; \
  var = alloca (strlen (path) + base_len + 15);                           \
  strcpy (var, ((struct bdb_private *)this->private)->base_path);         \
  strcpy (&var[base_len], path);                                          \
  strcat (var, "/glusterfs.db");                                          \
} while (0)


int32_t 
bdb_mknod (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode,
	   dev_t dev)
{
  frame->root->rsp_refs = NULL;  
  STACK_UNWIND (frame, -1, ENOSYS, NULL, NULL);
  return 0;
}

int32_t 
bdb_rename (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *oldloc,
	    loc_t *newloc)
{

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, -1, ENOSYS, NULL);
  return 0;
}

int32_t 
bdb_link (call_frame_t *frame, 
	  xlator_t *this,
	  loc_t *oldloc,
	  const char *newpath)
{
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, -1, ENOSYS, NULL, NULL);
  return 0;
}

int32_t 
bdb_create (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t flags,
	    mode_t mode,
	    fd_t *fd)
{
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, -1, ENOSYS, fd, loc->inode, NULL);
  return 0;
}

int32_t 
bdb_open (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc,
	  int32_t flags,
	  fd_t *fd)
{
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, -1, ENOSYS, fd);
  return 0;
}

int32_t 
bdb_readv (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   size_t size,
	   off_t offset)
{
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, -1, ENOSYS, NULL, 0, NULL);
  return 0;
}

int32_t 
bdb_writev (call_frame_t *frame,
	    xlator_t *this,
	    fd_t *fd,
	    struct iovec *vector,
	    int32_t count,
	    off_t offset)
{
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, -1, ENOSYS, NULL);
  return 0;
}

int32_t 
bdb_flush (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}

int32_t 
bdb_close (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}


int32_t 
bdb_fsync (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   int32_t datasync)
{
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, -1, ENOSYS);
  return 0;
}

int32_t 
bdb_lk (call_frame_t *frame,
	xlator_t *this,
	fd_t *fd,
	int32_t cmd,
	struct flock *lock)
{
  struct flock nullock = {0, };
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, -1, ENOSYS, &nullock);
  return 0;
}

int32_t
bdb_forget (call_frame_t *frame,
	    xlator_t *this,
	    inode_t *inode)
{
  if (dict_get (inode->ctx, this->name)) {
    DB *dbp = data_to_ptr (dict_get (inode->ctx, this->name));
    dbp->close (dbp, 0);
  }

  return 0;
}

int32_t
bdb_lookup (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t need_xattr)
{
  struct bdb_private *priv = this->private;  
  struct stat buf = {0, };
  char *real_path;
  char *db_path;
  int32_t op_ret;
  int32_t op_errno;
  dict_t *xattr = NULL;
  char version[50], ctime[50]; /* do #define the size */

  MAKE_REAL_PATH (real_path, this, loc->path);

  op_ret = lstat (real_path, &buf);
  op_errno = errno;

  if (need_xattr) {
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
  }

  frame->root->rsp_refs = NULL;
  if (xattr)
    dict_ref (xattr);
  if (op_ret == 0 && S_ISDIR(buf.st_mode) &&
      !(dict_get (loc->inode->ctx, this->name))) {
    int32_t ret = 0;
    DB *dbp = NULL;

    MAKE_REAL_PATH_TO_DB (db_path, this, loc->path);

    if ((ret = db_create(&dbp, priv->dbenv, 0)) == 0) {
      if ((ret = dbp->open (dbp, NULL, db_path, NULL, 
			    DB_BTREE, DB_CREATE, 0664)) != 0) {
	gf_log (this->name, GF_LOG_ERROR, 
		"Failed to open DB %s (%d)", db_path, ret);
	op_ret = -1;
      }
      dict_set (loc->inode->ctx, this->name, data_from_ptr (dbp));
    } else {
      gf_log (this->name, GF_LOG_ERROR, "Failed to create DB (%d)", ret);
      op_ret = -1;
    }
  }

  STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &buf, xattr);
  if (xattr)
    dict_unref (xattr);
  return 0;
}

int32_t
bdb_stat (call_frame_t *frame,
	  xlator_t *this,
	  loc_t *loc)
{
  struct stat buf;
  char *real_path;
  int32_t op_ret;
  int32_t op_errno;

  MAKE_REAL_PATH (real_path, this, loc->path);

  op_ret = lstat (real_path, &buf);
  op_errno = errno;

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &buf);

  return 0;
}



int32_t 
bdb_opendir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc, 
	     fd_t *fd)
{
  char *real_path;
  int32_t op_ret;
  int32_t op_errno;
  DIR *dir;

  MAKE_REAL_PATH (real_path, this, loc->path);
  
  dir = opendir (real_path);
  op_errno = errno;
  op_ret = (dir == NULL) ? -1 : dirfd (dir);
  
  if (dir) {
    struct bdb_fd *pfd = calloc (1, sizeof (*fd));
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
bdb_getdents (call_frame_t *frame,
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
  struct bdb_fd *pfd;

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
  strcpy (entry_path, real_path);
  entry_path[real_path_len] = '/';
  
  dir = pfd->dir;
  
  if (!dir) {
    gf_log (this->name, GF_LOG_ERROR, 
	    "pfd does not have dir set (possibly file fd, fd=%p, path=`%s'",
	    fd, real_path);


    frame->root->rsp_refs = NULL;
    STACK_UNWIND (frame, -1, errno, &entries, 0);
    freee (entry_path);
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
    
    /* This is to reduce the network traffic, in case only directory is needed from bdb */
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
    tmp->name = strdup (dirent->d_name);
    if (entry_path_len < real_path_len + 1 + strlen (tmp->name) + 1) {
      entry_path_len = real_path_len + strlen (tmp->name) + 1024;
      entry_path = realloc (entry_path, entry_path_len);
    }
    strcpy (&entry_path[real_path_len+1], tmp->name);
    lstat (entry_path, &tmp->buf);
    count++;
    
    tmp->next = entries.next;
    entries.next = tmp;
    /* if size is 0, count can never be = size, so entire dir is read */
    if (count == size)
      break;
  }
  freee (entry_path);

  
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &entries, count);
  while (entries.next) {
    tmp = entries.next;
    entries.next = entries.next->next;
    freee (tmp->name);
    freee (tmp);
  }
  return 0;
}


int32_t 
bdb_closedir (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
  int32_t op_ret;
  int32_t op_errno;
  data_t *pfd_data;
  struct bdb_fd *pfd;

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
bdb_readlink (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      size_t size)
{
  char *dest = alloca (size + 1);
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  
  MAKE_REAL_PATH (real_path, this, loc->path);
  
  op_ret = readlink (real_path, dest, size);
  if (op_ret > 0) 
    dest[op_ret] = 0;
  op_errno = errno;

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, dest);

  return 0;
}

int32_t 
bdb_mkdir (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf = {0, };

  MAKE_REAL_PATH (real_path, this, loc->path);

  op_ret = mkdir (real_path, mode);
  op_errno = errno;
  
  if (op_ret == 0) {
    chown (real_path, frame->root->uid, frame->root->gid);
    lstat (real_path, &stbuf);
  }
  
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &stbuf);

  return 0;
}


int32_t 
bdb_unlink (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;

  MAKE_REAL_PATH (real_path, this, loc->path);
  
  op_ret = unlink (real_path);
  op_errno = errno;
  
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

int32_t 
bdb_remove (const char *path, 
	    const struct stat *stat, 
	    int32_t typeflag, 
	    struct FTW *ftw)
{
  return remove (path);
}

int32_t
bdb_rmelem (call_frame_t *frame,
	    xlator_t *this,
	    const char *path)
{
  int32_t op_ret, op_errno;
  char *real_path;

  MAKE_REAL_PATH (real_path, this, path);
  op_ret = nftw (real_path, bdb_remove, 20, FTW_DEPTH|FTW_PHYS);
  op_errno = errno;
  /* FTW_DEPTH = traverse subdirs first before calling bdb_remove
   * on real_path
   * FTW_PHYS = do not follow symlinks
   */
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

int32_t 
bdb_rmdir (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;

  MAKE_REAL_PATH (real_path, this, loc->path);
  
  op_ret = rmdir (real_path);
  op_errno = errno;

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno);

  return 0;
}

int32_t 
bdb_symlink (call_frame_t *frame,
	     xlator_t *this,
	     const char *linkname,
	     loc_t *loc)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf = { 0, };

  MAKE_REAL_PATH (real_path, this, loc->path);

  op_ret = symlink (linkname, real_path);
  op_errno = errno;
  
  if (op_ret == 0) {
    lchown (real_path, frame->root->uid, frame->root->gid);
    lstat (real_path, &stbuf);
  }
    
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, loc->inode, &stbuf);

  return 0;
}

int32_t 
bdb_chmod (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   mode_t mode)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf;
  
  MAKE_REAL_PATH (real_path, this, loc->path);

  op_ret = chmod (real_path, mode);
  op_errno = errno;
    
  if (op_ret == 0)
    lstat (real_path, &stbuf);
    
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}


int32_t 
bdb_chown (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   uid_t uid,
	   gid_t gid)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf;

  MAKE_REAL_PATH (real_path, this, loc->path);
    
  op_ret = lchown (real_path, uid, gid);
  op_errno = errno;
    
  if (op_ret == 0)
    lstat (real_path, &stbuf);
    
  frame->root->rsp_refs = NULL;  
  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}


int32_t 
bdb_truncate (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      off_t offset)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf;

  MAKE_REAL_PATH (real_path, this, loc->path);

  op_ret = truncate (real_path, offset);
  op_errno = errno;
    
  if (op_ret == 0) {
    lstat (real_path, &stbuf);
  }
    
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}


int32_t 
bdb_utimens (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     struct timespec ts[2])
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct stat stbuf = {0, };
  struct timeval tv[2];
  
  MAKE_REAL_PATH (real_path, this, loc->path);

  /* TODO: fix timespec to timeval converstion 
   * Done: Check if this is correct */

  tv[0].tv_sec = ts[0].tv_sec;
  tv[0].tv_usec = ts[0].tv_nsec / 1000;
  tv[1].tv_sec = ts[1].tv_sec;
  tv[1].tv_usec = ts[1].tv_nsec / 1000;

  op_ret = lutimes (real_path, tv);
  if (op_ret == -1 && errno == ENOSYS) {
    op_ret = utimes (real_path, tv);
  }
  op_errno = errno;
    
  lstat (real_path, &stbuf);
 
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &stbuf);

  return 0;
}

int32_t 
bdb_statfs (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)

{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;
  struct statvfs buf = {0, };

  MAKE_REAL_PATH (real_path, this, loc->path);

  op_ret = statvfs (real_path, &buf);
  op_errno = errno;

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &buf);
  return 0;
}

int32_t
bdb_incver (call_frame_t *frame,
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
bdb_setxattr (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      dict_t *dict,
	      int flags)
{
  int32_t ret = -1;
  data_pair_t *trav = dict->members_list;
  DB *dbp = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  DBT key  = {0,};
  DBT data = {0,};

  if (dbp) {
    while (trav) {
      key.data = trav->key;
      key.size = strlen (trav->key)+1;
      data.data = trav->value->data;
      data.size = trav->value->len+1;
      
      if ((ret = dbp->put (dbp, NULL, &key, &data, 0)) != 0) {
	frame->root->rsp_refs = NULL;  
	STACK_UNWIND (frame, -1, ret);
	return 0;
      }
      trav = trav->next;
    }
  }

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, 0, 0);
  return 0;
}

int32_t 
bdb_getxattr (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  int32_t ret = -1;
  dict_t *dict = get_new_dict ();
  DB *dbp = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  DBT key = {0,};
  DBT data = {0,};

  if (dbp) {
    key.data = (char *)loc->path;
    key.size = strlen (loc->path)+1;
    
    if ((ret = dbp->get (dbp, NULL, &key, &data, 0)) != 0) {
      STACK_UNWIND (frame, -1, ret, dict);
      if (dict)
	dict_destroy (dict);
      return 0;
    }
    
    dict_set (dict, (char *)loc->path, bin_to_data (data.data, data.size));
  }

  STACK_UNWIND (frame, 0, 0, dict);

  if (dict)
    dict_destroy (dict);
  
  return 0;
}

/* send the DB->del in the call */
int32_t 
bdb_removexattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
                 const char *name)
{
  int32_t ret;

  DB *dbp = data_to_ptr (dict_get (loc->inode->ctx, this->name));
  if (dbp) {
    DBT key = {
      .data = (char *)name,
      .size = strlen (name),
    };
    
    if ((ret = dbp->del(dbp, NULL, &key, 0)) != 0) {
      frame->root->rsp_refs = NULL;  
      STACK_UNWIND (frame, -1, ret);
      return 0;
    }
  }
  
  frame->root->rsp_refs = NULL;  
  STACK_UNWIND (frame, 0, 0);
  return 0;
}


int32_t 
bdb_fsyncdir (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		int datasync)
{
  int32_t op_ret;
  int32_t op_errno;
  data_t *pfd_data = dict_get (fd->ctx, this->name);
  struct bdb_fd *pfd;
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
bdb_access (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t mask)
{
  int32_t op_ret;
  int32_t op_errno;
  char *real_path;

  MAKE_REAL_PATH (real_path, this, loc->path);

  op_ret = access (real_path, mask);
  op_errno = errno;

  frame->root->rsp_refs = NULL;  
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}


int32_t 
bdb_ftruncate (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 off_t offset)
{
  int32_t op_ret;
  int32_t op_errno;
  int32_t _fd = 0;
  struct stat buf;
  data_t *pfd_data = dict_get (fd->ctx, this->name);
  struct bdb_fd *pfd;

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


  op_ret = ftruncate (_fd, offset);
  op_errno = errno;
    
  fstat (_fd, &buf);
  

  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, op_ret, op_errno, &buf);

  return 0;
}

int32_t 
bdb_fchown (call_frame_t *frame,
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
  struct bdb_fd *pfd;

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


  op_ret = fchown (_fd, uid, gid);
  op_errno = errno;

  fstat (_fd, &buf);


  STACK_UNWIND (frame, op_ret, op_errno, &buf);

  return 0;
}


int32_t 
bdb_fchmod (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      mode_t mode)
{
  int32_t op_ret;
  int32_t op_errno;
  int32_t _fd;
  struct stat buf;
  data_t *pfd_data = dict_get (fd->ctx, this->name);
  struct bdb_fd *pfd;

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


  op_ret = fchmod (_fd, mode);
  op_errno = errno;
  
  fstat (_fd, &buf);


  frame->root->rsp_refs = NULL;  
  STACK_UNWIND (frame, op_ret, op_errno, &buf);

  return 0;
}

int32_t 
bdb_setdents (call_frame_t *frame,
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
  struct bdb_fd *pfd;
  data_t *pfd_data = NULL;

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
      } else if (flags == GF_SET_IF_NOT_PRESENT || flags != GF_SET_DIR_ONLY) {
	/* Create a 0byte file here */
	if (S_ISREG (trav->buf.st_mode)) {
	  ret = open (pathname, O_CREAT|O_EXCL, trav->buf.st_mode);
	  if (ret > 0) {
	    gf_log (this->name,
		    GF_LOG_DEBUG,
		    "Creating file %s with mode (0%o)",
		    pathname, 
		    trav->buf.st_mode);
	    close (ret);
	  }
	} else if (S_ISLNK(trav->buf.st_mode)) {
	  ret = symlink (trav->name, pathname);
	  if (!ret) {
	    gf_log (this->name,
		    GF_LOG_DEBUG,
		    "Creating symlink %s",
		    pathname);
	  }
	} else if (S_ISBLK (trav->buf.st_mode) || 
		   S_ISCHR (trav->buf.st_mode) || 
		   S_ISFIFO (trav->buf.st_mode)) {
	  ret = mknod (pathname, trav->buf.st_mode, trav->buf.st_dev);
	  if (!ret) {
	    gf_log (this->name,
		    GF_LOG_DEBUG,
		    "Creating device file %s",
		    pathname);
	  }
	}
      }
      /* TODO: handle another flag, GF_SET_OVERWRITE */

      /* Change the mode */
      chmod (pathname, trav->buf.st_mode);
      /* change the ownership */
      chown (pathname, trav->buf.st_uid, trav->buf.st_gid);

      /* consider the next entry */
      trav = trav->next;
    }
  }
  //  op_errno = errno;
  
  /* Return success all the time */
  frame->root->rsp_refs = NULL;
  STACK_UNWIND (frame, 0, 0);
  
  freee (entry_path);
  return 0;
}

int32_t 
bdb_fstat (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd)
{
  int32_t _fd;
  int32_t op_ret;
  int32_t op_errno;
  struct stat buf;
  data_t *pfd_data = dict_get (fd->ctx, this->name);
  struct bdb_fd *pfd;

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
    return 0;  }

  _fd = pfd->fd;

  op_ret = fstat (_fd, &buf);
  op_errno = errno;

  STACK_UNWIND (frame, op_ret, op_errno, &buf);
  return 0;
}


#define ALIGN(x) (((x) + sizeof (uint64_t) - 1) & ~(sizeof (uint64_t) - 1))

static int32_t
dirent_size (struct dirent *entry)
{
  return ALIGN (24 /* FIX MEEEE!!! */ + entry->d_reclen);
}

/* TODO: handle both dir and file in the directory */

int32_t
bdb_readdir (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     size_t size,
	     off_t off)
{
  data_t *pfd_data = dict_get (fd->ctx, this->name);
  struct bdb_fd *pfd;
  off_t in_case;
  int32_t this_size;
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
  
      in_case = telldir (dir);
      entry = readdir (dir);
      if (!entry)
	break;

      if (strcmp (entry->d_name, "glusterfs.db") == 0) {
	break;
      }

      this_size = dirent_size (entry);
      if (this_size + filled > size) {
	seekdir (dir, in_case);
	break;
      }

      /* TODO - consider endianness here */
      this_entry = (void *)(buf + filled);
      this_entry->d_ino = entry->d_ino;
      this_entry->d_off = entry->d_off;
      this_entry->d_type = 0; //entry->d_type;
      this_entry->d_len = entry->d_reclen;
      strncpy (this_entry->d_name, entry->d_name, this_entry->d_len);

      filled += this_size;
    }
    /* use the cursor and give the set of keys */
    if (filled < size && !off) {
      gf_dirent_t *this_entry;
      int32_t ret = 0;
      DBT key = {0,};
      DBT value = {0,};
      DBC *dbc = NULL;
      DB *dbp = data_to_ptr (dict_get (fd->inode->ctx, this->name));

      /* Acquire a cursor for the database. */
      if (dbp && ((ret = dbp->cursor(dbp, NULL, &dbc, 0)) == 0)) {
	while (filled < size) {
	  if ((ret = dbc->c_get(dbc, &key, &value, DB_NEXT)) == 0) {
	    this_size = ALIGN (24 /* FIX MEEEE!!! */ + key.size);
	    if (this_size + filled > size) {
	      seekdir (dir, in_case);
	      break;
	    }

	    this_entry = (void *)(buf + filled);
	    this_entry->d_ino = 2;
	    this_entry->d_off = 0xfff;
	    this_entry->d_type = 0; //entry->d_type;
	    this_entry->d_len = key.size;
	    strncpy (this_entry->d_name, key.data, key.size);
	    
	    filled += this_size;
	  } else {
	    dbc->c_close (dbc);
	    break;
	  }
	} /* while */
      } /* if */
    } /* while */

    STACK_UNWIND (frame, filled, 0, buf);
    free (buf);
  }

  return 0;
}


int32_t 
bdb_stats (call_frame_t *frame,
	     xlator_t *this,
	     int32_t flags)

{
  int32_t op_ret = 0;
  int32_t op_errno = 0;

  struct xlator_stats xlstats = {0, }, *stats = &xlstats;
  struct statvfs buf;
  struct timeval tv;
  struct bdb_private *priv = (struct bdb_private *)this->private;
  int64_t avg_read = 0;
  int64_t avg_write = 0;
  int64_t _time_ms = 0; 

    
  op_ret = statvfs (priv->base_path, &buf);
  op_errno = errno;
    
  
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
bdb_checksum (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		int32_t flag)
{
  char *real_path;
  DIR *dir;
  struct dirent *dirent;
  uint8_t file_checksum[4096] = {0,};
  uint8_t dir_checksum[4096] = {0,};
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
  } else {
    op_ret = 0;
    op_errno = 0;
  }

  /* TODO: read the filename in db file and send the checksum */
  
  while ((dirent = readdir (dir))) {
    //struct stat buf;

    if (!dirent)
      break;

    length = strlen (dirent->d_name);

    if (strcmp (dirent->d_name, "glusterfs.db") == 0) 
      continue;

    //lstat (dirent->d_name, &buf);
    
    for (i = 0; i < length; i++)
      dir_checksum[i] ^= dirent->d_name[i];
    {
      /* TODO: */
      /* There will be one file 'glusterfs.db' */
      /* retrive keys from it and send it accross */
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
	/* Tell the parent that bdb xlator is up */
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
  struct bdb_private *_private = calloc (1, sizeof (*_private));
  data_t *directory = dict_get (this->options, "directory");

  if (this->children) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "FATAL: storage/bdb cannot have subvolumes");
    freee (_private);
    return -1;
  }

  if (!directory) {
    gf_log (this->name, GF_LOG_ERROR,
	    "export directory not specified in spec file");
    freee (_private);
    return -1;
  }
  umask (000); // umask `masking' is done at the client side
  if (mkdir (directory->data, 0777) == 0) {
    gf_log (this->name, GF_LOG_WARNING,
	    "directory specified not exists, created");
  }
  /* Check whether the specified directory exists, if not create it. */
  ret = stat (directory->data, &buf);
  if (ret != 0 && !S_ISDIR (buf.st_mode)) {
    gf_log (this->name, GF_LOG_ERROR, 
	    "Specified directory doesn't exists, Exiting");
    freee (_private);
    return -1;
  }

  {
    /* Create a DB environment */
    DB_ENV *dbenv = NULL;
    int32_t ret = 0;

    if ((ret = db_env_create (&dbenv, 0)) != 0) {
      gf_log (this->name, GF_LOG_ERROR, 
	      "Failed to create DB environment (%d)", ret);
      freee (_private);
      return -1;
    }
    dbenv->set_errpfx(dbenv, "txnapp");

    if ((ret = dbenv->set_lk_detect(dbenv, DB_LOCK_DEFAULT)) != 0) { 
      gf_log (this->name, GF_LOG_ERROR, 
	      "Failed to set Deadlock detection (%d)", ret);
      freee (_private);
      return -1;
    }

    if ((ret = dbenv->open(dbenv, directory->data, DB_CREATE | DB_INIT_LOCK | 
			   DB_INIT_LOG |DB_INIT_TXN | DB_INIT_MPOOL | 
			   DB_RECOVER | DB_THREAD, 
			   S_IRUSR | S_IWUSR)) != 0) {
      gf_log (this->name, GF_LOG_ERROR, 
	      "Failed to open DB Environment (%d)", ret);
      freee (_private);
      return -1;
      
    }
    _private->dbenv = dbenv;
  }

  _private->base_path = strdup (directory->data);
  _private->base_path_length = strlen (_private->base_path);

  {
    /* Stats related variables */
    gettimeofday (&_private->init_time, NULL);
    gettimeofday (&_private->prev_fetch_time, NULL);
    _private->max_read = 1;
    _private->max_write = 1;
  }

  this->private = (void *)_private;
  return 0;
}

void
fini (xlator_t *this)
{
  struct bdb_private *priv = this->private;
  if (priv->dbenv)
    //(priv->dbp)->close (priv->dbp, 0);

  freee (priv);
  return;
}

struct xlator_mops mops = {
  .stats    = bdb_stats,
  .lock     = mop_lock_impl,
  .unlock   = mop_unlock_impl,
  .checksum = bdb_checksum,
};

struct xlator_fops fops = {
  .lookup      = bdb_lookup,
  .forget      = bdb_forget,
  .stat        = bdb_stat,
  .opendir     = bdb_opendir,
  .readdir     = bdb_readdir,
  .closedir    = bdb_closedir,
  .readlink    = bdb_readlink,
  .mknod       = bdb_mknod,
  .mkdir       = bdb_mkdir,
  .unlink      = bdb_unlink,
  .rmelem      = bdb_rmelem,
  .rmdir       = bdb_rmdir,
  .symlink     = bdb_symlink,
  .rename      = bdb_rename,
  .link        = bdb_link,
  .chmod       = bdb_chmod,
  .chown       = bdb_chown,
  .truncate    = bdb_truncate,
  .utimens     = bdb_utimens,
  .create      = bdb_create,
  .open        = bdb_open,
  .readv       = bdb_readv,
  .writev      = bdb_writev,
  .statfs      = bdb_statfs,
  .flush       = bdb_flush,
  .close       = bdb_close,
  .fsync       = bdb_fsync,
  .incver      = bdb_incver,
  .setxattr    = bdb_setxattr,
  .getxattr    = bdb_getxattr,
  .removexattr = bdb_removexattr,
  .fsyncdir    = bdb_fsyncdir,
  .access      = bdb_access,
  .ftruncate   = bdb_ftruncate,
  .fstat       = bdb_fstat,
  .lk          = bdb_lk,
  .fchown      = bdb_fchown,
  .fchmod      = bdb_fchmod,
  .setdents    = bdb_setdents,
  .getdents    = bdb_getdents,
};
