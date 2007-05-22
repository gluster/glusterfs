/*
  (C) 2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
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


#include "xlator.h"
#include "stack.h"
#include "list.h"

typedef struct {
  struct list_head list;
  char wind;
  call_frame_t *frame;
  glusterfs_fop_t fop;

  union {
    /* lookup */
    struct {
      fop_lookup_t fn;
      loc_t loc;
    } lookup;
    struct {
      fop_lookup_cbk_t fn;
      int32_t op_ret, op_errno;
      inode_t *inode;
      struct stat buf;
    } lookup_cbk;

    /* forget */
    struct {
      fop_forget_t fn;
      inode_t *inode;
    } forget;
    struct {
      fop_forget_cbk_t fn;
      int32_t op_ret, op_errno;
    } forget_cbk;

    /* stat */
    struct {
      fop_stat_t fn;
      loc_t loc;
    } stat;
    struct {
      fop_stat_cbk_t fn;
      int32_t op_ret, op_errno;
      struct stat buf;
    } stat_cbk;

    /* fstat */
    struct {
      fop_fstat_t fn;
      fd_t *fd;
    } fstat;
    struct {
      fop_fstat_cbk_t fn;
      int32_t op_ret, op_errno;
      struct stat buf;
    } fstat_cbk;

    /* chmod */
    struct {
      fop_chmod_t fn;
      loc_t loc;
      mode_t mode;
    } chmod;
    struct {
      fop_chmod_cbk_t fn;
      int32_t op_ret, op_errno;
      struct stat buf;
    } chmod_cbk;

    /* fchmod */
    struct {
      fop_fchmod_t fn;
      fd_t *fd;
      mode_t mode;
    } fchmod;
    struct {
      fop_fchmod_cbk_t fn;
      int32_t op_ret, op_errno;
      struct stat buf;
    } fchmod_cbk;

    /* chown */
    struct {
      fop_chown_t fn;
      loc_t loc;
      uid_t uid;
      gid_t gid;
    } chown;
    struct {
      fop_chown_cbk_t fn;
      int32_t op_ret, op_errno;
      struct stat buf;
    } chown_cbk;

    /* fchown */
    struct {
      fop_fchown_t fn;
      fd_t *fd;
      uid_t uid;
      gid_t gid;
    } fchown;
    struct {
      fop_fchown_cbk_t fn;
      int32_t op_ret, op_errno;
      struct stat buf;
    } fchown_cbk;

    /* truncate */
    struct {
      fop_truncate_t fn;
      loc_t loc;
      off_t off;
    } truncate;
    struct {
      fop_truncate_cbk_t fn;
      int32_t op_ret, op_errno;
      struct stat buf;
    } truncate_cbk;

    /* ftruncate */
    struct {
      fop_ftruncate_t fn;
      fd_t *fd;
      off_t off;
    } ftruncate;
    struct {
      fop_ftruncate_cbk_t fn;
      int32_t op_ret, op_errno;
      struct stat buf;
    } ftruncate_cbk;

    /* utimens */
    struct {
      fop_utimens_t fn;
      loc_t loc;
      struct timespec tv[2];
    } utimens;
    struct {
      fop_utimens_cbk_t fn;
      int32_t op_ret, op_errno;
      struct stat buf;
    } utimens_cbk;

    /* access */
    struct {
      fop_access_t fn;
      loc_t loc;
      int32_t mask;
    } access;
    struct {
      fop_access_cbk_t fn;
      int32_t op_ret, op_errno;
    } access_cbk;

    /* readlink */
    struct {
      fop_readlink_t fn;
      loc_t loc;
      size_t size;
    } readlink;
    struct {
      fop_readlink_cbk_t fn;
      int32_t op_ret, op_errno;
      const char *buf;
    } readlink_cbk;

    /* mknod */
    struct {
      fop_mknod_t fn;
      const char *path;
      mode_t mode;
      dev_t rdev;
    } mknod;
    struct {
      fop_mknod_cbk_t fn;
      int32_t op_ret, op_errno;
      inode_t *inode;
      struct stat buf;
    } mknod_cbk;

    /* mkdir */
    struct {
      fop_mkdir_t fn;
      const char *path;
      mode_t mode;
    } mkdir;
    struct {
      fop_mkdir_cbk_t fn;
      int32_t op_ret, op_errno;
      inode_t *inode;
      struct stat buf;
    } mkdir_cbk;

    /* unlink */
    struct {
      fop_unlink_t fn;
      loc_t loc;
    } unlink;
    struct {
      fop_unlink_cbk_t fn;
      int32_t op_ret, op_errno;
    } unlink_cbk;

    /* rmdir */
    struct {
      fop_rmdir_t fn;
      loc_t loc;
    } rmdir;
    struct {
      fop_rmdir_cbk_t fn;
      int32_t op_ret, op_errno;
    } rmdir_cbk;

    /* symlink */
    struct {
      fop_symlink_t fn;
      const char *linkname;
      const char *newpath;
    } symlink;
    struct {
      fop_symlink_cbk_t fn;
      int32_t op_ret, op_errno;
      inode_t *inode;
      struct stat buf;
    } symlink_cbk;

    /* rename */
    struct {
      fop_rename_t fn;
      loc_t old;
      loc_t new;
    } rename;
    struct {
      fop_rename_cbk_t fn;
      int32_t op_ret, op_errno;
      struct stat buf;
    } rename_cbk;

    /* link */
    struct {
      fop_link_t fn;
      loc_t oldloc;
      const char *newpath;
    } link;
    struct {
      fop_link_cbk_t fn;
      int32_t op_ret, op_errno;
      inode_t *inode;
      struct stat buf;
    } link_cbk;

    /* create */
    struct {
      fop_create_t fn;
      const char *path;
      int32_t flags;
      mode_t mode;
    } create;
    struct {
      fop_create_cbk_t fn;
      int32_t op_ret, op_errno;
      fd_t *fd;
      inode_t *inode;
      struct stat buf;
    } create_cbk;

    /* open */
    struct {
      fop_open_t fn;
      loc_t loc;
      int32_t flags;
    } open;
    struct {
      fop_open_cbk_t fn;
      int32_t op_ret, op_errno;
      fd_t *fd;
    } open_cbk;

    /* readv */
    struct {
      fop_readv_t fn;
      fd_t *fd;
      size_t size;
      off_t off;
    } readv;
    struct {
      fop_readv_cbk_t fn;
      int32_t op_ret;
      int32_t op_errno;
      struct iovec *vector;
      int32_t count;
    } readv_cbk;

    /* writev */
    struct {
      fop_writev_t fn;
      fd_t *fd;
      struct iovec *vector;
      int32_t count;
      off_t off;
    } writev;
    struct {
      fop_writev_cbk_t fn;
      int32_t op_ret, op_errno;
    } writev_cbk;

    /* flush */
    struct {
      fop_flush_t fn;
      fd_t *fd;
    } flush;
    struct {
      fop_flush_cbk_t fn;
      int32_t op_ret, op_errno;
    } flush_cbk;

    /* close */
    struct {
      fop_close_t fn;
      fd_t *fd;
    } close;
    struct {
      fop_close_cbk_t fn;
      int32_t op_ret, op_errno;
    } close_cbk;

    /* fsync */
    struct {
      fop_fsync_t fn;
      fd_t *fd;
      int32_t datasync;
    } fsync;
    struct {
      fop_fsync_cbk_t fn;
      int32_t op_ret, op_errno;
    } fsync_cbk;

    /* opendir */
    struct {
      fop_opendir_t fn;
      loc_t loc;
    } opendir;
    struct {
      fop_opendir_cbk_t fn;
      int32_t op_ret, op_errno;
      fd_t *fd;
    } opendir_cbk;

    /* readdir */
    struct {
      fop_readdir_t fn;
      fd_t *fd;
      size_t size;
      off_t off;
    } readdir;
    struct {
      fop_readdir_cbk_t fn;
      int32_t op_ret;
      int32_t op_errno;
      dir_entry_t entries;
      int32_t count;
    } readdir_cbk;

    /* closedir */
    struct {
      fop_closedir_t fn;
      fd_t *fd;
    } closedir;
    struct {
      fop_closedir_cbk_t fn;
      int32_t op_ret, op_errno;
    } closedir_cbk;

    /* fsyncdir */
    struct {
      fop_fsyncdir_t fn;
      fd_t *fd;
      int32_t datasync;
    } fsyncdir;
    struct {
      fop_fsyncdir_cbk_t fn;
      int32_t op_ret, op_errno;
    } fsyncdir_cbk;

    /* statfs */
    struct {
      fop_statfs_t fn;
      loc_t loc;
    } statfs;
    struct {
      fop_statfs_cbk_t fn;
      int32_t op_ret, op_errno;
      struct statvfs buf;
    } statfs_cbk;

    /* setxattr */
    struct {
      fop_setxattr_t fn;
      loc_t loc;
      const char *name;
      const char *value;
      size_t size;
      int32_t flags;
    } setxattr;
    struct {
      fop_setxattr_cbk_t fn;
      int32_t op_ret, op_errno;
    } setxattr_cbk;

    /* getxattr */
    struct {
      fop_getxattr_t fn;
      loc_t loc;
      const char *name;
      size_t size;
    } getxattr;
    struct {
      fop_getxattr_cbk_t fn;
      int32_t op_ret, op_errno;
      const char *value;
    } getxattr_cbk;

    /* listxattr */
    struct {
      fop_listxattr_t fn;
      loc_t loc;
      size_t size;
    } listxattr;
    struct {
      fop_listxattr_cbk_t fn;
      int32_t op_ret, op_errno;
      const char *value;
    } listxattr_cbk;

    /* removexattr */
    struct {
      fop_removexattr_t fn;
      loc_t loc;
      const char *name;
    } removexattr;
    struct {
      fop_removexattr_cbk_t fn;
      int32_t op_ret, op_errno;
    } removexattr_cbk;

    /* lk */
    struct {
      fop_lk_t fn;
      fd_t *fd;
      int32_t cmd;
      struct flock lock;
    } lk;
    struct {
      fop_lk_cbk_t fn;
      int32_t op_ret, op_errno;
      struct flock lock;
    } lk_cbk;
  } args;
} call_stub_t;


static void
loc_wipe (loc_t *loc)
{
  free ((char *)loc->path);
  if (loc->inode)
    inode_unref (loc->inode);
}


static void
loc_copy (loc_t *dest, loc_t *src)
{
  dest->path = strdup (src->path);
  dest->ino = src->ino;
  if (src->inode)
    dest->inode = inode_ref (src->inode);
}


static call_stub_t *
stub_new (call_frame_t *frame,
	  char wind,
	  glusterfs_fop_t fop)
{
  call_stub_t *new;

  new = calloc (1, sizeof (*new));
  if (!new)
    return NULL;

  new->frame = frame;
  new->wind = wind;
  new->fop = fop;

  INIT_LIST_HEAD (&new->list);
  return new;
}


call_stub_t *
fop_lookup_stub (call_frame_t *frame,
		 fop_lookup_t fn,
		 loc_t *loc)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_LOOKUP);
  if (!stub)
    return NULL;

  stub->args.lookup.fn = fn;
  loc_copy (&stub->args.lookup.loc, loc);

  return stub;
}


call_stub_t *
fop_lookup_cbk_stub (call_frame_t *frame,
		     fop_lookup_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     inode_t *inode,
		     struct stat *buf)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_LOOKUP);
  if (!stub)
    return NULL;

  stub->args.lookup_cbk.fn = fn;
  stub->args.lookup_cbk.op_ret = op_ret;
  stub->args.lookup_cbk.op_errno = op_errno;
  stub->args.lookup_cbk.inode = inode_ref (inode);
  stub->args.lookup_cbk.buf = *buf;

  return stub;
}


call_stub_t *
fop_forget_stub (call_frame_t *frame,
		 fop_forget_t fn,
		 inode_t *inode)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_FORGET);
  if (!stub)
    return NULL;

  stub->args.forget.fn = fn;
  stub->args.forget.inode = inode_ref (inode);

  return stub;
}


call_stub_t *
fop_forget_cbk_stub (call_frame_t *frame,
		     fop_forget_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_FORGET);
  if (!stub)
    return NULL;

  stub->args.forget_cbk.fn = fn;
  stub->args.forget_cbk.op_ret = op_ret;
  stub->args.forget_cbk.op_ret = op_errno;

  return stub;
}


call_stub_t *
fop_stat_stub (call_frame_t *frame,
	       fop_stat_t fn,
	       loc_t *loc)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_STAT);
  if (!stub)
    return NULL;

  stub->args.stat.fn = fn;
  loc_copy (&stub->args.stat.loc, loc);

  return stub;
}


call_stub_t *
fop_stat_cbk_stub (call_frame_t *frame,
		   fop_stat_cbk_t fn,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *buf)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_STAT);
  if (!stub)
    return NULL;

  stub->args.stat_cbk.fn = fn;
  stub->args.stat_cbk.op_ret = op_ret;
  stub->args.stat_cbk.op_ret = op_errno;
  stub->args.stat_cbk.buf = *buf;

  return stub;
}


call_stub_t *
fop_fstat_stub (call_frame_t *frame,
		fop_fstat_t fn,
		fd_t *fd)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_FSTAT);
  if (!stub)
    return NULL;

  stub->args.fstat.fn = fn;
  stub->args.fstat.fd = fd;

  return stub;
}


call_stub_t *
fop_fstat_cbk_stub (call_frame_t *frame,
		    fop_fstat_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_FSTAT);
  if (!stub)
    return NULL;

  stub->args.fstat_cbk.fn = fn;
  stub->args.fstat_cbk.op_ret = op_ret;
  stub->args.fstat_cbk.op_ret = op_errno;
  stub->args.fstat_cbk.buf = *buf;

  return stub;
}


call_stub_t *
fop_chmod_stub (call_frame_t *frame,
		fop_chmod_t fn,
		loc_t *loc,
		mode_t mode)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_CHMOD);
  if (!stub)
    return NULL;

  stub->args.chmod.fn = fn;
  loc_copy (&stub->args.chmod.loc, loc);
  stub->args.chmod.mode = mode;

  return stub;
}


call_stub_t *
fop_chmod_cbk_stub (call_frame_t *frame,
		    fop_chmod_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_CHMOD);
  if (!stub)
    return NULL;

  stub->args.chmod_cbk.fn = fn;
  stub->args.chmod_cbk.op_ret = op_ret;
  stub->args.chmod_cbk.op_ret = op_errno;
  stub->args.chmod_cbk.buf = *buf;

  return stub;
}


call_stub_t *
fop_fchmod_stub (call_frame_t *frame,
		 fop_fchmod_t fn,
		 fd_t *fd,
		 mode_t mode)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_FCHMOD);
  if (!stub)
    return NULL;

  stub->args.fchmod.fn = fn;
  stub->args.fchmod.fd = fd;
  stub->args.fchmod.mode = mode;

  return stub;
}


call_stub_t *
fop_fchmod_cbk_stub (call_frame_t *frame,
		     fop_fchmod_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_FCHMOD);
  if (!stub)
    return NULL;

  stub->args.fchmod_cbk.fn = fn;
  stub->args.fchmod_cbk.op_ret = op_ret;
  stub->args.fchmod_cbk.op_ret = op_errno;
  stub->args.fchmod_cbk.buf = *buf;

  return stub;
}


call_stub_t *
fop_chown_stub (call_frame_t *frame,
		fop_chown_t fn,
		loc_t *loc,
		uid_t uid,
		gid_t gid)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_CHOWN);
  if (!stub)
    return NULL;

  stub->args.chown.fn = fn;
  loc_copy (&stub->args.chown.loc, loc);
  stub->args.chown.uid = uid;
  stub->args.chown.gid = gid;

  return stub;
}


call_stub_t *
fop_chown_cbk_stub (call_frame_t *frame,
		    fop_chown_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_CHOWN);
  if (!stub)
    return NULL;

  stub->args.chown_cbk.fn = fn;
  stub->args.chown_cbk.op_ret = op_ret;
  stub->args.chown_cbk.op_ret = op_errno;
  stub->args.chown_cbk.buf = *buf;

  return stub;
}


call_stub_t *
fop_fchown_stub (call_frame_t *frame,
		 fop_fchown_t fn,
		 fd_t *fd,
		 uid_t uid,
		 gid_t gid)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_FCHOWN);
  if (!stub)
    return NULL;

  stub->args.fchown.fn = fn;
  stub->args.fchown.fd = fd;
  stub->args.fchown.uid = uid;
  stub->args.fchown.gid = gid;

  return stub;
}


call_stub_t *
fop_fchown_cbk_stub (call_frame_t *frame,
		     fop_fchown_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_FCHOWN);
  if (!stub)
    return NULL;

  stub->args.fchown_cbk.fn = fn;
  stub->args.fchown_cbk.op_ret = op_ret;
  stub->args.fchown_cbk.op_ret = op_errno;
  stub->args.fchown_cbk.buf = *buf;

  return stub;
}


/* truncate */

call_stub_t *
fop_truncate_stub (call_frame_t *frame,
		   fop_truncate_t fn,
		   loc_t *loc,
		   off_t off)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_TRUNCATE);
  if (!stub)
    return NULL;

  stub->args.truncate.fn = fn;
  loc_copy (&stub->args.truncate.loc, loc);
  stub->args.truncate.off = off;

  return stub;
}


call_stub_t *
fop_truncate_cbk_stub (call_frame_t *frame,
		       fop_truncate_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct stat *buf)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_TRUNCATE);
  if (!stub)
    return NULL;

  stub->args.truncate_cbk.fn = fn;
  stub->args.truncate_cbk.op_ret = op_ret;
  stub->args.truncate_cbk.op_ret = op_errno;
  stub->args.truncate_cbk.buf = *buf;

  return stub;
}


call_stub_t *
fop_ftruncate_stub (call_frame_t *frame,
		    fop_ftruncate_t fn,
		    fd_t *fd,
		    off_t off)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_FTRUNCATE);
  if (!stub)
    return NULL;

  stub->args.ftruncate.fn = fn;
  stub->args.ftruncate.fd = fd;
  stub->args.ftruncate.off = off;

  return stub;
}


call_stub_t *
fop_ftruncate_cbk_stub (call_frame_t *frame,
			fop_ftruncate_cbk_t fn,
			int32_t op_ret,
			int32_t op_errno,
			struct stat *buf)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_FTRUNCATE);
  if (!stub)
    return NULL;

  stub->args.ftruncate_cbk.fn = fn;
  stub->args.ftruncate_cbk.op_ret = op_ret;
  stub->args.ftruncate_cbk.op_ret = op_errno;
  stub->args.ftruncate_cbk.buf = *buf;

  return stub;
}


call_stub_t *
fop_utimens_stub (call_frame_t *frame,
		  fop_utimens_t fn,
		  loc_t *loc,
		  struct timespec tv[2])
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_UTIMENS);
  if (!stub)
    return NULL;

  stub->args.utimens.fn = fn;
  loc_copy (&stub->args.utimens.loc, loc);
  stub->args.utimens.tv[0] = tv[0];
  stub->args.utimens.tv[1] = tv[1];

  return stub;
}


call_stub_t *
fop_utimens_cbk_stub (call_frame_t *frame,
		      fop_utimens_cbk_t fn,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct stat *buf)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_UTIMENS);
  if (!stub)
    return NULL;

  stub->args.utimens_cbk.fn = fn;
  stub->args.utimens_cbk.op_ret = op_ret;
  stub->args.utimens_cbk.op_ret = op_errno;
  stub->args.utimens_cbk.buf = *buf;

  return stub;
}


call_stub_t *
fop_access_stub (call_frame_t *frame,
		 fop_access_t fn,
		 loc_t *loc,
		 int32_t mask)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_ACCESS);
  if (!stub)
    return NULL;

  stub->args.access.fn = fn;
  loc_copy (&stub->args.access.loc, loc);
  stub->args.access.mask = mask;

  return stub;
}


call_stub_t *
fop_access_cbk_stub (call_frame_t *frame,
		     fop_access_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_ACCESS);
  if (!stub)
    return NULL;

  stub->args.access_cbk.fn = fn;
  stub->args.access_cbk.op_ret = op_ret;
  stub->args.access_cbk.op_ret = op_errno;

  return stub;
}


call_stub_t *
fop_readlink_stub (call_frame_t *frame,
		   fop_readlink_t fn,
		   loc_t *loc,
		   size_t size)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_READLINK);
  if (!stub)
    return NULL;

  stub->args.readlink.fn = fn;
  loc_copy (&stub->args.readlink.loc, loc);
  stub->args.readlink.size = size;

  return stub;
}


call_stub_t *
fop_readlink_cbk_stub (call_frame_t *frame,
		       fop_readlink_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       const char *path)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_READLINK);
  if (!stub)
    return NULL;

  stub->args.readlink_cbk.fn = fn;
  stub->args.readlink_cbk.op_ret = op_ret;
  stub->args.readlink_cbk.op_ret = op_errno;
  stub->args.readlink_cbk.buf = strdup (path);

  return stub;
}


call_stub_t *
fop_mknod_stub (call_frame_t *frame,
		fop_mknod_t fn,
		const char *path,
		mode_t mode,
		dev_t rdev)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_MKNOD);
  if (!stub)
    return NULL;

  stub->args.mknod.fn = fn;
  stub->args.mknod.path = strdup (path);
  stub->args.mknod.mode = mode;
  stub->args.mknod.rdev = rdev;

  return stub;
}


call_stub_t *
fop_mknod_cbk_stub (call_frame_t *frame,
		    fop_mknod_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct stat *buf)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_MKNOD);
  if (!stub)
    return NULL;

  stub->args.mknod_cbk.fn = fn;
  stub->args.mknod_cbk.op_ret = op_ret;
  stub->args.mknod_cbk.op_ret = op_errno;
  stub->args.mknod_cbk.inode = inode_ref (inode);
  stub->args.mknod_cbk.buf = *buf;

  return stub;
}


call_stub_t *
fop_mkdir_stub (call_frame_t *frame,
		fop_mkdir_t fn,
		const char *path,
		mode_t mode)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_MKDIR);
  if (!stub)
    return NULL;

  stub->args.mkdir.fn = fn;
  stub->args.mkdir.path = strdup (path);
  stub->args.mkdir.mode = mode;

  return stub;
}


call_stub_t *
fop_mkdir_cbk_stub (call_frame_t *frame,
		    fop_mkdir_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct stat *buf)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_MKDIR);
  if (!stub)
    return NULL;

  stub->args.mkdir_cbk.fn = fn;
  stub->args.mkdir_cbk.op_ret = op_ret;
  stub->args.mkdir_cbk.op_ret = op_errno;
  stub->args.mkdir_cbk.inode = inode_ref (inode);
  stub->args.mkdir_cbk.buf = *buf;

  return stub;
}


call_stub_t *
fop_unlink_stub (call_frame_t *frame,
		 fop_unlink_t fn,
		 loc_t *loc)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_UNLINK);
  if (!stub)
    return NULL;

  stub->args.unlink.fn = fn;
  loc_copy (&stub->args.unlink.loc, loc);

  return stub;
}


call_stub_t *
fop_unlink_cbk_stub (call_frame_t *frame,
		     fop_unlink_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_UNLINK);
  if (!stub)
    return NULL;

  stub->args.unlink_cbk.fn = fn;
  stub->args.unlink_cbk.op_ret = op_ret;
  stub->args.unlink_cbk.op_ret = op_errno;

  return stub;
}



call_stub_t *
fop_rmdir_stub (call_frame_t *frame,
		fop_rmdir_t fn,
		loc_t *loc)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_RMDIR);
  if (!stub)
    return NULL;

  stub->args.rmdir.fn = fn;
  loc_copy (&stub->args.rmdir.loc, loc);

  return stub;
}


call_stub_t *
fop_rmdir_cbk_stub (call_frame_t *frame,
		    fop_rmdir_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_RMDIR);
  if (!stub)
    return NULL;

  stub->args.rmdir_cbk.fn = fn;
  stub->args.rmdir_cbk.op_ret = op_ret;
  stub->args.rmdir_cbk.op_ret = op_errno;

  return stub;
}


call_stub_t *
fop_symlink_stub (call_frame_t *frame,
		  fop_symlink_t fn,
		  const char *linkname,
		  const char *newpath)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_SYMLINK);
  if (!stub)
    return NULL;

  stub->args.symlink.fn = fn;
  stub->args.symlink.linkname = strdup (linkname);
  stub->args.symlink.newpath = strdup (newpath);

  return stub;
}


call_stub_t *
fop_symlink_cbk_stub (call_frame_t *frame,
		      fop_symlink_cbk_t fn,
		      int32_t op_ret,
		      int32_t op_errno,
		      inode_t *inode,
		      struct stat *buf)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_SYMLINK);
  if (!stub)
    return NULL;

  stub->args.symlink_cbk.fn = fn;
  stub->args.symlink_cbk.op_ret = op_ret;
  stub->args.symlink_cbk.op_ret = op_errno;
  stub->args.symlink_cbk.inode = inode_ref (inode);
  stub->args.symlink_cbk.buf = *buf;

  return stub;
}


call_stub_t *
fop_rename_stub (call_frame_t *frame,
		 fop_rename_t fn,
		 loc_t *oldloc,
		 loc_t *newloc)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_RENAME);
  if (!stub)
    return NULL;

  stub->args.rename.fn = fn;
  loc_copy (&stub->args.rename.old, oldloc);
  loc_copy (&stub->args.rename.new, newloc);

  return stub;
}


call_stub_t *
fop_rename_cbk_stub (call_frame_t *frame,
		     fop_rename_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_RENAME);
  if (!stub)
    return NULL;

  stub->args.rename_cbk.fn = fn;
  stub->args.rename_cbk.op_ret = op_ret;
  stub->args.rename_cbk.op_ret = op_errno;
  stub->args.rename_cbk.buf = *buf;

  return stub;
}


call_stub_t *
fop_link_stub (call_frame_t *frame,
	       fop_link_t fn,
	       loc_t *oldloc,
	       const char *newpath)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_LINK);
  if (!stub)
    return NULL;

  stub->args.link.fn = fn;
  loc_copy (&stub->args.link.oldloc, oldloc);
  stub->args.link.newpath = strdup (newpath);

  return stub;
}


call_stub_t *
fop_link_cbk_stub (call_frame_t *frame,
		   fop_link_cbk_t fn,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
		   struct stat *buf)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_LINK);
  if (!stub)
    return NULL;

  stub->args.link_cbk.fn = fn;
  stub->args.link_cbk.op_ret = op_ret;
  stub->args.link_cbk.op_ret = op_errno;
  stub->args.link_cbk.inode = inode_ref (inode);
  stub->args.link_cbk.buf = *buf;

  return stub;
}


call_stub_t *
fop_create_stub (call_frame_t *frame,
		 fop_create_t fn,
		 const char *path,
		 int32_t flags,
		 mode_t mode)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_CREATE);
  if (!stub)
    return NULL;

  stub->args.create.fn = fn;
  stub->args.create.path = strdup (path);
  stub->args.create.flags = flags;
  stub->args.create.mode = mode;

  return stub;
}


call_stub_t *
fop_create_cbk_stub (call_frame_t *frame,
		     fop_create_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     fd_t *fd,
		     inode_t *inode,
		     struct stat *buf)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_CREATE);
  if (!stub)
    return NULL;

  stub->args.create_cbk.fn = fn;
  stub->args.create_cbk.op_ret = op_ret;
  stub->args.create_cbk.op_ret = op_errno;
  stub->args.create_cbk.fd = fd;
  stub->args.create_cbk.inode = inode_ref (inode);
  stub->args.create_cbk.buf = *buf;

  return stub;
}


call_stub_t *
fop_open_stub (call_frame_t *frame,
	       fop_open_t fn,
	       loc_t *loc,
	       int32_t flags)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_OPEN);
  if (!stub)
    return NULL;

  stub->args.open.fn = fn;
  loc_copy (&stub->args.open.loc, loc);
  stub->args.open.flags = flags;

  return stub;
}


call_stub_t *
fop_open_cbk_stub (call_frame_t *frame,
		   fop_open_cbk_t fn,
		   int32_t op_ret,
		   int32_t op_errno,
		   fd_t *fd)

{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_OPEN);
  if (!stub)
    return NULL;

  stub->args.open_cbk.fn = fn;
  stub->args.open_cbk.op_ret = op_ret;
  stub->args.open_cbk.op_ret = op_errno;
  stub->args.open_cbk.fd = fd;

  return stub;
}


call_stub_t *
fop_readv_stub (call_frame_t *frame,
		fop_readv_t fn,
		fd_t *fd,
		size_t size,
		off_t off)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_READ);
  if (!stub)
    return NULL;

  stub->args.readv.fn = fn;
  stub->args.readv.fd = fd;
  stub->args.readv.size = size;
  stub->args.readv.off = off;

  return stub;
}


call_stub_t *
fop_readv_cbk_stub (call_frame_t *frame,
		    fop_readv_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct iovec *vector,
		    int32_t count)

{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_READ);
  if (!stub)
    return NULL;

  stub->args.readv_cbk.fn = fn;
  stub->args.readv_cbk.op_ret = op_ret;
  stub->args.readv_cbk.op_ret = op_errno;
  stub->args.readv_cbk.vector = iov_dup (vector, count);
  stub->args.readv_cbk.count = count;

  if (op_ret > 0)
    dict_ref (frame->root->rsp_refs);

  return stub;
}


call_stub_t *
fop_writev_stub (call_frame_t *frame,
		 fop_writev_t fn,
		 fd_t *fd,
		 struct iovec *vector,
		 int32_t count,
		 off_t off)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_WRITE);
  if (!stub)
    return NULL;

  stub->args.writev.fn = fn;
  stub->args.writev.fd = fd;
  stub->args.writev.vector = iov_dup (vector, count);
  stub->args.writev.count = count;
  stub->args.writev.off = off;

  if (count)
    dict_ref (frame->root->req_refs);

  return stub;
}


call_stub_t *
fop_writev_cbk_stub (call_frame_t *frame,
		     fop_writev_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno)

{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_WRITE);
  if (!stub)
    return NULL;

  stub->args.writev_cbk.fn = fn;
  stub->args.writev_cbk.op_ret = op_ret;
  stub->args.writev_cbk.op_ret = op_errno;

  return stub;
}



call_stub_t *
fop_flush_stub (call_frame_t *frame,
		fop_flush_t fn,
		fd_t *fd)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_FLUSH);
  if (!stub)
    return NULL;

  stub->args.flush.fn = fn;
  stub->args.flush.fd = fd;

  return stub;
}


call_stub_t *
fop_flush_cbk_stub (call_frame_t *frame,
		    fop_flush_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno)

{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_FLUSH);
  if (!stub)
    return NULL;

  stub->args.flush_cbk.fn = fn;
  stub->args.flush_cbk.op_ret = op_ret;
  stub->args.flush_cbk.op_ret = op_errno;

  return stub;
}


call_stub_t *
fop_close_stub (call_frame_t *frame,
		fop_close_t fn,
		fd_t *fd)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_CLOSE);
  if (!stub)
    return NULL;

  stub->args.close.fn = fn;
  stub->args.close.fd = fd;

  return stub;
}


call_stub_t *
fop_close_cbk_stub (call_frame_t *frame,
		    fop_close_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno)

{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_CLOSE);
  if (!stub)
    return NULL;

  stub->args.close_cbk.fn = fn;
  stub->args.close_cbk.op_ret = op_ret;
  stub->args.close_cbk.op_ret = op_errno;

  return stub;
}


call_stub_t *
fop_fsync_stub (call_frame_t *frame,
		fop_fsync_t fn,
		fd_t *fd,
		int32_t datasync)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_FSYNC);
  if (!stub)
    return NULL;

  stub->args.fsync.fn = fn;
  stub->args.fsync.fd = fd;
  stub->args.fsync.datasync = datasync;

  return stub;
}


call_stub_t *
fop_fsync_cbk_stub (call_frame_t *frame,
		    fop_fsync_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno)

{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_FSYNC);
  if (!stub)
    return NULL;

  stub->args.fsync_cbk.fn = fn;
  stub->args.fsync_cbk.op_ret = op_ret;
  stub->args.fsync_cbk.op_ret = op_errno;

  return stub;
}


call_stub_t *
fop_opendir_stub (call_frame_t *frame,
		  fop_opendir_t fn,
		  loc_t *loc)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_OPENDIR);
  if (!stub)
    return NULL;

  stub->args.opendir.fn = fn;
  loc_copy (&stub->args.opendir.loc, loc);

  return stub;
}


call_stub_t *
fop_opendir_cbk_stub (call_frame_t *frame,
		      fop_opendir_cbk_t fn,
		      int32_t op_ret,
		      int32_t op_errno,
		      fd_t *fd)

{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_OPENDIR);
  if (!stub)
    return NULL;

  stub->args.opendir_cbk.fn = fn;
  stub->args.opendir_cbk.op_ret = op_ret;
  stub->args.opendir_cbk.op_ret = op_errno;
  stub->args.opendir_cbk.fd = fd;

  return stub;
}


call_stub_t *
fop_readdir_stub (call_frame_t *frame,
		  fop_readdir_t fn,
		  size_t size,
		  off_t off,
		  fd_t *fd)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_READDIR);
  if (!stub)
    return NULL;

  stub->args.readdir.fn = fn;
  stub->args.readdir.size = size;
  stub->args.readdir.off = off;
  stub->args.readdir.fd = fd;

  return stub;
}


call_stub_t *
fop_readdir_cbk_stub (call_frame_t *frame,
		      fop_readdir_cbk_t fn,
		      int32_t op_ret,
		      int32_t op_errno,
		      dir_entry_t *entries,
		      int32_t count)

{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_READDIR);
  if (!stub)
    return NULL;

  stub->args.readdir_cbk.fn = fn;
  stub->args.readdir_cbk.op_ret = op_ret;
  stub->args.readdir_cbk.op_ret = op_errno;
  if (op_ret >= 0) {
    stub->args.readdir_cbk.entries.next = entries->next;
    entries->next = NULL;
  }
  stub->args.readdir_cbk.count = count;

  return stub;
}



call_stub_t *
fop_closedir_stub (call_frame_t *frame,
		   fop_closedir_t fn,
		   fd_t *fd)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_CLOSEDIR);
  if (!stub)
    return NULL;

  stub->args.closedir.fn = fn;
  stub->args.closedir.fd = fd;

  return stub;
}


call_stub_t *
fop_closedir_cbk_stub (call_frame_t *frame,
		       fop_closedir_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno)

{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_CLOSEDIR);
  if (!stub)
    return NULL;

  stub->args.closedir_cbk.fn = fn;
  stub->args.closedir_cbk.op_ret = op_ret;
  stub->args.closedir_cbk.op_ret = op_errno;

  return stub;
}


call_stub_t *
fop_fsyncdir_stub (call_frame_t *frame,
		   fop_fsyncdir_t fn,
		   fd_t *fd,
		   int32_t datasync)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_FSYNCDIR);
  if (!stub)
    return NULL;

  stub->args.fsyncdir.fn = fn;
  stub->args.fsyncdir.fd = fd;
  stub->args.fsyncdir.datasync = datasync;

  return stub;
}


call_stub_t *
fop_fsyncdir_cbk_stub (call_frame_t *frame,
		       fop_fsyncdir_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno)

{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_FSYNCDIR);
  if (!stub)
    return NULL;

  stub->args.fsyncdir_cbk.fn = fn;
  stub->args.fsyncdir_cbk.op_ret = op_ret;
  stub->args.fsyncdir_cbk.op_ret = op_errno;

  return stub;
}


call_stub_t *
fop_statfs_stub (call_frame_t *frame,
		 fop_statfs_t fn,
		 loc_t *loc)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_STATFS);
  if (!stub)
    return NULL;

  stub->args.statfs.fn = fn;
  loc_copy (&stub->args.statfs.loc, loc);

  return stub;
}


call_stub_t *
fop_statfs_cbk_stub (call_frame_t *frame,
		     fop_statfs_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct statvfs *buf)

{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_STATFS);
  if (!stub)
    return NULL;

  stub->args.statfs_cbk.fn = fn;
  stub->args.statfs_cbk.op_ret = op_ret;
  stub->args.statfs_cbk.op_ret = op_errno;
  stub->args.statfs_cbk.buf = *buf;

  return stub;
}


call_stub_t *
fop_setxattr_stub (call_frame_t *frame,
		   fop_setxattr_t fn,
		   loc_t *loc,
		   const char *name,
		   const char *value,
		   size_t size,
		   int32_t flags)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_SETXATTR);
  if (!stub)
    return NULL;

  stub->args.setxattr.fn = fn;
  loc_copy (&stub->args.setxattr.loc, loc);
  stub->args.setxattr.name = strdup (name);
  stub->args.setxattr.value = memdup (value, size);
  stub->args.setxattr.size = size;
  stub->args.setxattr.flags = flags;

  return stub;
}


call_stub_t *
fop_setxattr_cbk_stub (call_frame_t *frame,
		       fop_setxattr_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_SETXATTR);
  if (!stub)
    return NULL;

  stub->args.setxattr_cbk.fn = fn;
  stub->args.setxattr_cbk.op_ret = op_ret;
  stub->args.setxattr_cbk.op_ret = op_errno;

  return stub;
}



call_stub_t *
fop_getxattr_stub (call_frame_t *frame,
		   fop_getxattr_t fn,
		   loc_t *loc,
		   size_t size)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_GETXATTR);
  if (!stub)
    return NULL;

  stub->args.getxattr.fn = fn;
  loc_copy (&stub->args.getxattr.loc, loc);
  stub->args.getxattr.size = size;

  return stub;
}


call_stub_t *
fop_getxattr_cbk_stub (call_frame_t *frame,
		       fop_getxattr_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       const char *value)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_GETXATTR);
  if (!stub)
    return NULL;

  stub->args.getxattr_cbk.fn = fn;
  stub->args.getxattr_cbk.op_ret = op_ret;
  stub->args.getxattr_cbk.op_ret = op_errno;
  if (op_ret > 0)
    stub->args.getxattr_cbk.value = memdup (value, op_ret);

  return stub;
}


call_stub_t *
fop_listxattr_stub (call_frame_t *frame,
		    fop_listxattr_t fn,
		    loc_t *loc,
		    size_t size)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_LISTXATTR);
  if (!stub)
    return NULL;

  stub->args.listxattr.fn = fn;
  loc_copy (&stub->args.listxattr.loc, loc);
  stub->args.listxattr.size = size;

  return stub;
}


call_stub_t *
fop_listxattr_cbk_stub (call_frame_t *frame,
			fop_listxattr_cbk_t fn,
			int32_t op_ret,
			int32_t op_errno,
			const char *value)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_LISTXATTR);
  if (!stub)
    return NULL;

  stub->args.listxattr_cbk.fn = fn;
  stub->args.listxattr_cbk.op_ret = op_ret;
  stub->args.listxattr_cbk.op_ret = op_errno;
  if (value && op_ret > 0)
    stub->args.listxattr_cbk.value = memdup (value, op_ret);

  return stub;
}


call_stub_t *
fop_removexattr_stub (call_frame_t *frame,
		      fop_removexattr_t fn,
		      loc_t *loc,
		      const char *name)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_REMOVEXATTR);
  if (!stub)
    return NULL;

  stub->args.removexattr.fn = fn;
  loc_copy (&stub->args.removexattr.loc, loc);
  stub->args.removexattr.name = strdup (name);

  return stub;
}


call_stub_t *
fop_removexattr_cbk_stub (call_frame_t *frame,
			  fop_removexattr_cbk_t fn,
			  int32_t op_ret,
			  int32_t op_errno)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_REMOVEXATTR);
  if (!stub)
    return NULL;

  stub->args.removexattr_cbk.fn = fn;
  stub->args.removexattr_cbk.op_ret = op_ret;
  stub->args.removexattr_cbk.op_ret = op_errno;

  return stub;
}


call_stub_t *
fop_lk_stub (call_frame_t *frame,
	     fop_lk_t fn,
	     fd_t *fd,
	     int32_t cmd,
	     struct flock *lock)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_LK);
  if (!stub)
    return NULL;

  stub->args.lk.fn = fn;
  stub->args.lk.fd = fd;
  stub->args.lk.cmd = cmd;
  stub->args.lk.lock = *lock;

  return stub;
}


call_stub_t *
fop_lk_cbk_stub (call_frame_t *frame,
		 fop_lk_cbk_t fn,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct flock *lock)

{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_LK);
  if (!stub)
    return NULL;

  stub->args.lk_cbk.fn = fn;
  stub->args.lk_cbk.op_ret = op_ret;
  stub->args.lk_cbk.op_ret = op_errno;
  stub->args.lk_cbk.lock = *lock;

  return stub;
}


static void
call_resume_wind (call_stub_t *stub)
{
  switch (stub->fop) {
  case GF_FOP_LOOKUP:
    stub->args.lookup.fn (stub->frame, stub->frame->this,
			  &stub->args.lookup.loc);
    loc_wipe (&stub->args.lookup.loc);
    break;
  case GF_FOP_FORGET:
    stub->args.forget.fn (stub->frame, stub->frame->this,
			  stub->args.forget.inode);
    inode_unref (stub->args.forget.inode);
    break;
  }
}


static void
call_resume_unwind (call_stub_t *stub)
{
  switch (stub->fop) {
  case GF_FOP_LOOKUP:
    stub->args.lookup_cbk.fn (stub->frame, stub->frame->cookie,
			      stub->frame->this,
			      stub->args.lookup_cbk.op_ret,
			      stub->args.lookup_cbk.op_errno,
			      stub->args.lookup_cbk.inode,
			      &stub->args.lookup_cbk.buf);
    inode_unref (stub->args.lookup_cbk.inode);
    break;
  case GF_FOP_FORGET:
    stub->args.forget_cbk.fn (stub->frame, stub->frame->cookie,
			      stub->frame->this,
			      stub->args.forget_cbk.op_ret,
			      stub->args.forget_cbk.op_errno);
    break;
  }
}


void
call_resume (call_stub_t *stub)
{
  list_del_init (&stub->list);

  if (stub->wind)
    call_resume_wind (stub);
  else
    call_resume_unwind (stub);

  free (stub);
}
