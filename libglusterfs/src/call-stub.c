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

#include "call-stub.h"

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
call_resume_wind (call_stub_t *stub, void *data)
{
  switch (stub->fop) {
  case GF_FOP_OPEN:
    {
      loc_t *loc = (loc_t *) data;
      stub->args.open.loc.inode = loc->inode;
      stub->args.open.loc.ino = loc->ino;
      stub->args.open.fn (stub->frame, 
			  stub->frame->this,
			  &stub->args.open.loc, 
			  stub->args.open.flags);
      break;
    }
  case GF_FOP_CREATE:
    /*loc_t *loc = (loc_t *) data;
    stub->args.create.loc.inode = loc->inode;
    stub->args.create.loc.ino = loc->ino;
    stub->args.create.fn (stub->frame,
			  stub->frame->this,
			  stub->args.create.path,
			  stub->args.create.flags,
			  stub->args.create.mode);*/
    break;
  case GF_FOP_STAT:
    {
      loc_t *loc = (loc_t *)data;
      stub->args.stat.loc.inode = loc->inode;
      stub->args.stat.loc.ino = loc->ino;
      stub->args.stat.fn (stub->frame,
			  stub->frame->this,
			  &stub->args.stat.loc);
      
      break;
    }
  case GF_FOP_READLINK:
    break;
  
  case GF_FOP_MKNOD:
    break;
  
  case GF_FOP_MKDIR:
    break;
  
  case GF_FOP_UNLINK:
    {
      loc_t *loc = (loc_t *)data;
      stub->args.unlink.loc.inode = loc->inode;
      stub->args.unlink.loc.ino = loc->ino;
      stub->args.unlink.fn (stub->frame,
			    stub->frame->this,
			    &stub->args.unlink.loc);
      
      break;
    }
  
  case GF_FOP_RMDIR:
    {
      loc_t *loc = (loc_t *) data;
      stub->args.rmdir.loc.inode = loc->inode;
      stub->args.rmdir.loc.ino = loc->ino;
      stub->args.rmdir.fn (stub->frame,
			   stub->frame->this,
			   &stub->args.rmdir.loc);
      
      break;
    }
  
  case GF_FOP_SYMLINK:
    break;
  
  case GF_FOP_RENAME:
    {
#if 0
      /* this is a special case. 
       * we need to lookup two inodes before we proceed */
      loc_t *loc = (loc_t *)data;
      if (stub->args.rename.old.inode) {
	/* now we are called by lookup of oldpath */
	stub->args.rename.old.inode = loc->inode;
	stub->args.rename.old.ino = loc->ino;
	/* now lookup for newpath */
	loc_t *newloc = calloc (1, sizeof (loc_t));
	newloc->path = strdup (stub->args.rename.new.path);
	newloc->inode = inode_update (table, NULL, NULL, newloc->ino);
	
	if (!newloc->inode) {
	  /* lookup for newpath */
	  STACK_WIND (stub->frame,
		      server_lookup_cbk,
		      stub->frame->this,
		      stub->frame->this->fops->lookup,
		      newloc);
	  free (newloc->path);
	  free (newloc);
	  break;
	}
	
      } else {
	/* we are called by the lookup of newpath */
	if (loc->inode) {
	  stub->args.rename.new.inode = loc->inode;
	  stub->args.rename.new.ino = loc->ino;
	}
      }
      
      /* after looking up for oldpath as well as newpath, 
       * we are ready to resume */
#endif
      stub->args.rename.fn (stub->frame,
			    stub->frame->this,
			    &stub->args.rename.old,
			    &stub->args.rename.new);
      
      break;
    }
  case GF_FOP_LINK:
    break;
  
  case GF_FOP_CHMOD:
    {
      loc_t *loc = (loc_t *) data;
      stub->args.chmod.loc.inode = loc->inode;
      stub->args.chmod.loc.ino = loc->ino;
      stub->args.chmod.fn (stub->frame,
			   stub->frame->this,
			   &stub->args.chmod.loc,
			   stub->args.chmod.mode);
      
      break;
    }
  case GF_FOP_CHOWN:
    {
      loc_t *loc = (loc_t *) data;
      stub->args.chown.loc.inode = loc->inode;
      stub->args.chown.loc.ino = loc->ino;
      stub->args.chown.fn (stub->frame,
			   stub->frame->this,
			   &stub->args.chown.loc,
			   stub->args.chown.uid,
			   stub->args.chown.gid);
      
      break;
    }
  case GF_FOP_TRUNCATE:
    {
      loc_t *loc = (loc_t *) data;
      stub->args.truncate.loc.inode = loc->inode;
      stub->args.truncate.loc.ino = loc->ino;
      stub->args.truncate.fn (stub->frame,
			      stub->frame->this,
			      &stub->args.truncate.loc,
			      stub->args.truncate.off);
      
      break;
    }
      
  case GF_FOP_READ:
    break;
  
  case GF_FOP_WRITE:
    break;
  
  case GF_FOP_STATFS:
    {
      loc_t *loc = (loc_t *)data;
      stub->args.statfs.loc.inode = loc->inode;
      stub->args.statfs.loc.ino = loc->ino;
      stub->args.statfs.fn (stub->frame,
			    stub->frame->this,
			    &stub->args.statfs.loc);
      break;
    }
  case GF_FOP_FLUSH:
    break;
  
  case GF_FOP_CLOSE:
    break;
  
  case GF_FOP_FSYNC:
    break;
  
  case GF_FOP_SETXATTR:
    {
      loc_t *loc = (loc_t *)data;
      stub->args.setxattr.loc.inode = loc->inode;
      stub->args.setxattr.loc.ino = loc->ino;
      stub->args.setxattr.fn (stub->frame,
			      stub->frame->this,
			      &stub->args.setxattr.loc,
			      stub->args.setxattr.name,
			      stub->args.setxattr.value,
			      stub->args.setxattr.size,
			      stub->args.setxattr.flags);
      
      break;
    }
  
  case GF_FOP_GETXATTR:
    {
      loc_t *loc = (loc_t *)data;
      stub->args.getxattr.loc.inode = loc->inode;
      stub->args.getxattr.loc.ino = loc->ino;
      stub->args.getxattr.fn (stub->frame,
			      stub->frame->this,
			      &stub->args.getxattr.loc,
			      stub->args.getxattr.name,
			      stub->args.getxattr.size);
      break;
    }
  
  case GF_FOP_LISTXATTR:
    {
      loc_t *loc = (loc_t *)data;
      stub->args.listxattr.loc.inode = loc->inode;
      stub->args.listxattr.loc.ino = loc->ino;
      stub->args.listxattr.fn (stub->frame,
			       stub->frame->this,
			       &stub->args.listxattr.loc,
			       stub->args.listxattr.size);
      break;
    }
  
  case GF_FOP_REMOVEXATTR:
    {
      loc_t *loc = (loc_t *)data;
      stub->args.removexattr.loc.inode = loc->inode;
      stub->args.removexattr.loc.ino = loc->ino;
      stub->args.removexattr.fn (stub->frame,
				 stub->frame->this,
				 &stub->args.removexattr.loc,
				 stub->args.removexattr.name);
      break;
    }
  
  case GF_FOP_OPENDIR:
    {
      loc_t *loc = (loc_t *)data;
      stub->args.opendir.loc.inode = loc->inode;
      stub->args.opendir.loc.ino = loc->ino;
      stub->args.opendir.fn (stub->frame,
			     stub->frame->this,
			     &stub->args.opendir.loc);
      break;
    }
  case GF_FOP_READDIR:
    break;
  
  case GF_FOP_CLOSEDIR:
    break;
  
  case GF_FOP_FSYNCDIR:
    break;
  
  case GF_FOP_ACCESS:
    {
      loc_t *loc = (loc_t *)data;
      stub->args.access.loc.inode = loc->inode;
      stub->args.access.loc.ino = loc->ino;
      stub->args.access.fn (stub->frame,
			    stub->frame->this,
			    &stub->args.access.loc,
			    stub->args.access.mask);
      break;
    }
  
  case GF_FOP_FTRUNCATE:
    break;
  
  case GF_FOP_FSTAT:
    break;
  
  case GF_FOP_LK:
    break;
  
  case GF_FOP_UTIMENS:
    {
      loc_t *loc = (loc_t *)data;
      stub->args.utimens.loc.inode = loc->inode;
      stub->args.utimens.loc.ino = loc->ino;
      stub->args.utimens.fn (stub->frame,
			     stub->frame->this,
			     &stub->args.utimens.loc,
			     stub->args.utimens.tv);
      
      break;
    }
  
  
    break;
  case GF_FOP_FCHMOD:
    break;
  
  case GF_FOP_FCHOWN:
    break;
  
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
  case GF_FOP_WRITEDIR:
    break;
  case GF_FOP_MAXVALUE:
    break;
  }
}


static void
call_resume_unwind (call_stub_t *stub, void *data)
{
  switch (stub->fop) {
  case GF_FOP_OPEN:
    stub->args.open_cbk.fn (stub->frame, 
			    stub->frame->cookie,
			    stub->frame->this,
			    stub->args.open_cbk.op_ret, 
			    stub->args.open_cbk.op_errno,
			    stub->args.open_cbk.fd);
    break;
  case GF_FOP_CREATE:
    stub->args.create_cbk.fn (stub->frame,
			      stub->frame->cookie,
			      stub->frame->this,
			      stub->args.create_cbk.op_ret,
			      stub->args.create_cbk.op_errno,
			      stub->args.create_cbk.fd,
			      stub->args.create_cbk.inode,
			      &stub->args.create_cbk.buf);
  case GF_FOP_STAT:
    break;
  case GF_FOP_READLINK:
    break;
  
  case GF_FOP_MKNOD:
    break;
  
  case GF_FOP_MKDIR:
    break;
  
  case GF_FOP_UNLINK:
    break;
  
  case GF_FOP_RMDIR:
    break;
  
  case GF_FOP_SYMLINK:
    break;
  
  case GF_FOP_RENAME:
    break;
  
  case GF_FOP_LINK:
    break;
  
  case GF_FOP_CHMOD:
    break;
  
  case GF_FOP_CHOWN:
    break;
  
  case GF_FOP_TRUNCATE:
    break;
  
  case GF_FOP_READ:
    break;
  
  case GF_FOP_WRITE:
    break;
  
  case GF_FOP_STATFS:
    break;
  
  case GF_FOP_FLUSH:
    break;
  
  case GF_FOP_CLOSE:
    break;
  
  case GF_FOP_FSYNC:
    break;
  
  case GF_FOP_SETXATTR:
    break;
  
  case GF_FOP_GETXATTR:
    break;
  
  case GF_FOP_LISTXATTR:
    break;
  
  case GF_FOP_REMOVEXATTR:
    break;
  
  case GF_FOP_OPENDIR:
    break;
  
  case GF_FOP_READDIR:
    break;
  
  case GF_FOP_CLOSEDIR:
    break;
  
  case GF_FOP_FSYNCDIR:
    break;
  
  case GF_FOP_ACCESS:
    break;
  
  case GF_FOP_FTRUNCATE:
    break;
  
  case GF_FOP_FSTAT:
    break;
  
  case GF_FOP_LK:
    break;
  
  case GF_FOP_UTIMENS:
    break;
  
  
    break;
  case GF_FOP_FCHMOD:
    break;
  
  case GF_FOP_FCHOWN:
    break;
  
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
  case GF_FOP_WRITEDIR:
    break;
  case GF_FOP_MAXVALUE:
    break;
  }
}


void
call_resume (call_stub_t *stub, void *data)
{
  list_del_init (&stub->list);

  if (stub->wind)
    call_resume_wind (stub, data);
  else
    call_resume_unwind (stub, data);

  free (stub);
}
