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
  if (inode)
    stub->args.lookup_cbk.inode = inode_ref (inode);
  if (buf)
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
  if (inode)
    stub->args.forget.inode =  inode_ref (inode);

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
  stub->args.forget_cbk.op_errno = op_errno;

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
  stub->args.stat_cbk.op_errno = op_errno;
  if (op_ret == 0)
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
  stub->args.fstat_cbk.op_errno = op_errno;
  if (buf)
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
  stub->args.chmod_cbk.op_errno = op_errno;
  if (buf)
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
  stub->args.fchmod_cbk.op_errno = op_errno;
  if (buf)
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
  stub->args.chown_cbk.op_errno = op_errno;
  if (buf)
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
  stub->args.fchown_cbk.op_errno = op_errno;
  if (buf)
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
  stub->args.truncate_cbk.op_errno = op_errno;
  if (buf)
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
  stub->args.ftruncate_cbk.op_errno = op_errno;
  if (buf)
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
  stub->args.utimens_cbk.op_errno = op_errno;
  if (buf)
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
  stub->args.access_cbk.op_errno = op_errno;

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
  stub->args.readlink_cbk.op_errno = op_errno;
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
  stub->args.mknod_cbk.op_errno = op_errno;
  if (inode)
    stub->args.mknod_cbk.inode = inode_ref (inode);
  if (buf)
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
  stub->args.mkdir_cbk.op_errno = op_errno;
  if (inode)
    stub->args.mkdir_cbk.inode = inode_ref (inode);
  if (buf)
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
  stub->args.unlink_cbk.op_errno = op_errno;

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
  stub->args.rmdir_cbk.op_errno = op_errno;

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
  stub->args.symlink_cbk.op_errno = op_errno;
  if (inode)
    stub->args.symlink_cbk.inode = inode_ref (inode);
  if (buf)
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
  stub->args.rename_cbk.op_errno = op_errno;
  if (buf)
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
  stub->args.link_cbk.op_errno = op_errno;
  if (inode)
    stub->args.link_cbk.inode = inode_ref (inode);
  if (buf)
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
  stub->args.create_cbk.op_errno = op_errno;
  stub->args.create_cbk.fd = fd;
  if (inode)
    stub->args.create_cbk.inode = inode_ref (inode);
  if (buf)
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
  stub->args.open_cbk.op_errno = op_errno;
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
		    int32_t count,
		    struct stat *stbuf)

{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_READ);
  if (!stub)
    return NULL;

  stub->args.readv_cbk.fn = fn;
  stub->args.readv_cbk.op_ret = op_ret;
  stub->args.readv_cbk.op_errno = op_errno;
  if (op_ret >= 0) {
    stub->args.readv_cbk.vector = iov_dup (vector, count);
    stub->args.readv_cbk.count = count;
    stub->args.readv_cbk.stbuf = *stbuf;
  }

  if (op_ret >= 0)
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
		     int32_t op_errno,
		     struct stat *stbuf)

{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_WRITE);
  if (!stub)
    return NULL;

  stub->args.writev_cbk.fn = fn;
  stub->args.writev_cbk.op_ret = op_ret;
  stub->args.writev_cbk.op_errno = op_errno;
  if (op_ret >= 0)
    stub->args.writev_cbk.stbuf = *stbuf;

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
  stub->args.flush_cbk.op_errno = op_errno;

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
  stub->args.close_cbk.op_errno = op_errno;

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
  stub->args.fsync_cbk.op_errno = op_errno;

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
  stub->args.opendir_cbk.op_errno = op_errno;
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
  stub->args.readdir_cbk.op_errno = op_errno;
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
  stub->args.closedir_cbk.op_errno = op_errno;

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
  stub->args.fsyncdir_cbk.op_errno = op_errno;

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
  stub->args.statfs_cbk.op_errno = op_errno;
  if (op_ret == 0)
    stub->args.statfs_cbk.buf = *buf;

  return stub;
}


call_stub_t *
fop_setxattr_stub (call_frame_t *frame,
		   fop_setxattr_t fn,
		   loc_t *loc,
		   dict_t *dict,
		   int32_t flags)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_SETXATTR);
  if (!stub)
    return NULL;

  stub->args.setxattr.fn = fn;
  loc_copy (&stub->args.setxattr.loc, loc);
  /* TODO */
  stub->args.setxattr.dict = dict;
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
  stub->args.setxattr_cbk.op_errno = op_errno;

  return stub;
}



call_stub_t *
fop_getxattr_stub (call_frame_t *frame,
		   fop_getxattr_t fn,
		   loc_t *loc)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_GETXATTR);
  if (!stub)
    return NULL;

  stub->args.getxattr.fn = fn;
  loc_copy (&stub->args.getxattr.loc, loc);

  return stub;
}


call_stub_t *
fop_getxattr_cbk_stub (call_frame_t *frame,
		       fop_getxattr_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       dict_t *dict)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_GETXATTR);
  if (!stub)
    return NULL;

  stub->args.getxattr_cbk.fn = fn;
  stub->args.getxattr_cbk.op_ret = op_ret;
  stub->args.getxattr_cbk.op_errno = op_errno;
  /* TODO */
  if (dict)
    stub->args.getxattr_cbk.dict = dict_ref (dict);
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
  stub->args.removexattr_cbk.op_errno = op_errno;

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
  stub->args.lk_cbk.op_errno = op_errno;
  if (op_ret == 0)
    stub->args.lk_cbk.lock = *lock;

  return stub;
}


/* FIXME: Not in callstub? */
call_stub_t *
fop_writedir_stub (call_frame_t *frame,
		   fop_writedir_t fn,
		   fd_t *fd,
		   int32_t flags,
		   dir_entry_t *entries,
		   int32_t count)
{ 
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_WRITEDIR);
  if (!stub)
    return NULL;

  stub->args.writedir.fn = fn;
  stub->args.writedir.flags = flags;
  stub->args.writedir.count = count;
  if (entries) {
    stub->args.writedir.entries.next = entries->next;
    entries->next = NULL;
  }
  
  return stub;
}

call_stub_t *
fop_writedir_cbk_stub (call_frame_t *frame,
		       fop_writedir_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno)
{  
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 0, GF_FOP_WRITEDIR);
  if (!stub)
    return NULL;

  stub->args.writedir_cbk.fn = fn;
  stub->args.writedir_cbk.op_ret = op_ret;
  stub->args.writedir_cbk.op_errno = op_errno;

  return stub;
  
}


static void
call_resume_wind (call_stub_t *stub)
{
  switch (stub->fop) {
  case GF_FOP_OPEN:
    {
      stub->args.open.fn (stub->frame, 
			  stub->frame->this,
			  &stub->args.open.loc, 
			  stub->args.open.flags);
      loc_wipe (&stub->args.open.loc);

      break;
    }
  case GF_FOP_CREATE:
    {
      stub->args.create.fn (stub->frame,
			    stub->frame->this,
			    stub->args.create.path,
			    stub->args.create.flags,
			    stub->args.create.mode);
      free ((char *)stub->args.create.path);
      break;
    }
  case GF_FOP_STAT:
    {
      stub->args.stat.fn (stub->frame,
			  stub->frame->this,
			  &stub->args.stat.loc);
      loc_wipe (&stub->args.stat.loc);
      break;
    }
  case GF_FOP_READLINK:
    {
      stub->args.readlink.fn (stub->frame,
			      stub->frame->this,
			      &stub->args.readlink.loc,
			      stub->args.readlink.size);
      loc_wipe (&stub->args.readlink.loc);
      break;
    }
  
  case GF_FOP_MKNOD:
    {
      stub->args.mknod.fn (stub->frame,
			   stub->frame->this,
			   stub->args.mknod.path,
			   stub->args.mknod.mode,
			   stub->args.mknod.rdev);
      free ((char *)stub->args.mknod.path);
    }
    break;
  
  case GF_FOP_MKDIR:
    {
      stub->args.mkdir.fn (stub->frame,
			   stub->frame->this,
			   stub->args.mkdir.path,
			   stub->args.mkdir.mode);
      free ((char *)stub->args.mkdir.path);
    }
    break;
  
  case GF_FOP_UNLINK:
    {
      stub->args.unlink.fn (stub->frame,
			    stub->frame->this,
			    &stub->args.unlink.loc);
      loc_wipe (&stub->args.unlink.loc);
    }
   break;

  case GF_FOP_RMDIR:
    {
      stub->args.rmdir.fn (stub->frame,
			   stub->frame->this,
			   &stub->args.rmdir.loc);
      loc_wipe (&stub->args.rmdir.loc);
    }
    break;
      
  case GF_FOP_SYMLINK:
    {
      stub->args.symlink.fn (stub->frame,
			     stub->frame->this,
			     stub->args.symlink.linkname,
			     stub->args.symlink.newpath);
      free ((char *)stub->args.symlink.linkname);
      free ((char *)stub->args.symlink.newpath);
    }
    break;
  
  case GF_FOP_RENAME:
    {
      stub->args.rename.fn (stub->frame,
			    stub->frame->this,
			    &stub->args.rename.old,
			    &stub->args.rename.new);

      loc_wipe (&stub->args.rename.old);
      loc_wipe (&stub->args.rename.new);
    }
    break;

  case GF_FOP_LINK:
    {
      stub->args.link.fn (stub->frame,
			  stub->frame->this,
			  &stub->args.link.oldloc,
			  stub->args.link.newpath);
      loc_wipe (&stub->args.link.oldloc);
      free ((char *)stub->args.link.newpath);
    }
    break;
  
  case GF_FOP_CHMOD:
    {
      stub->args.chmod.fn (stub->frame,
			   stub->frame->this,
			   &stub->args.chmod.loc,
			   stub->args.chmod.mode);
      loc_wipe (&stub->args.chmod.loc);
    }
    break;

  case GF_FOP_CHOWN:
    {
      stub->args.chown.fn (stub->frame,
			   stub->frame->this,
			   &stub->args.chown.loc,
			   stub->args.chown.uid,
			   stub->args.chown.gid);
      loc_wipe (&stub->args.chown.loc);
      
      break;
    }
  case GF_FOP_TRUNCATE:
    {
      stub->args.truncate.fn (stub->frame,
			      stub->frame->this,
			      &stub->args.truncate.loc,
			      stub->args.truncate.off);
      loc_wipe (&stub->args.truncate.loc);
      
      break;
    }
      
  case GF_FOP_READ:
    {
      stub->args.readv.fn (stub->frame,
			   stub->frame->this,
			   stub->args.readv.fd,
			   stub->args.readv.size,
			   stub->args.readv.off);
      break;
    }
  
  case GF_FOP_WRITE:
    {
      dict_t *refs = stub->frame->root->req_refs;
      stub->args.writev.fn (stub->frame,
			    stub->frame->this,
			    stub->args.writev.fd,
			    stub->args.writev.vector,
			    stub->args.writev.count,
			    stub->args.writev.off);
      free ((char *)stub->args.writev.vector);
      if (stub->args.writev.count)
	dict_unref (refs);
      break;
    }
  
  case GF_FOP_STATFS:
    {
      stub->args.statfs.fn (stub->frame,
			    stub->frame->this,
			    &stub->args.statfs.loc);
      loc_wipe (&stub->args.statfs.loc);
      break;
    }
  case GF_FOP_FLUSH:
    {
      stub->args.flush.fn (stub->frame,
			   stub->frame->this,
			   stub->args.flush.fd);
      
      break;
    }
  
  case GF_FOP_CLOSE:
    {
      stub->args.close.fn (stub->frame,
			   stub->frame->this,
			   stub->args.close.fd);
      
      break;
    }
  
  case GF_FOP_FSYNC:
    {
      stub->args.fsync.fn (stub->frame,
			   stub->frame->this,
			   stub->args.fsync.fd,
			   stub->args.fsync.datasync);
      break;
    }

  case GF_FOP_SETXATTR:
    {
      stub->args.setxattr.fn (stub->frame,
			      stub->frame->this,
			      &stub->args.setxattr.loc,
			      stub->args.setxattr.dict,
			      stub->args.setxattr.flags);
      loc_wipe (&stub->args.setxattr.loc);
      break;
    }
  
  case GF_FOP_GETXATTR:
    {
      stub->args.getxattr.fn (stub->frame,
			      stub->frame->this,
			      &stub->args.getxattr.loc);
      loc_wipe (&stub->args.getxattr.loc);
      break;
    }

  case GF_FOP_REMOVEXATTR:
    {
      stub->args.removexattr.fn (stub->frame,
				 stub->frame->this,
				 &stub->args.removexattr.loc,
				 stub->args.removexattr.name);
      loc_wipe (&stub->args.removexattr.loc);
      free ((char *)stub->args.removexattr.name);
      break;
    }
  
  case GF_FOP_OPENDIR:
    {
      stub->args.opendir.fn (stub->frame,
			     stub->frame->this,
			     &stub->args.opendir.loc);
      loc_wipe (&stub->args.opendir.loc);

      break;
    }

  case GF_FOP_READDIR:
    {
      stub->args.readdir.fn (stub->frame,
			     stub->frame->this,
			     stub->args.readdir.size,
			     stub->args.readdir.off,
			     stub->args.readdir.fd);
      break;
    }

  case GF_FOP_CLOSEDIR:
    {
      stub->args.closedir.fn (stub->frame,
			      stub->frame->this,
			      stub->args.closedir.fd);
      break;
    }
  
  case GF_FOP_FSYNCDIR:
    {
      stub->args.fsyncdir.fn (stub->frame,
			      stub->frame->this,
			      stub->args.fsyncdir.fd,
			      stub->args.fsyncdir.datasync);
      break;
    }
  
  case GF_FOP_ACCESS:
    {
      stub->args.access.fn (stub->frame,
			    stub->frame->this,
			    &stub->args.access.loc,
			    stub->args.access.mask);
      loc_wipe (&stub->args.access.loc);

      break;
    }
  
  case GF_FOP_FTRUNCATE:
    {
      stub->args.ftruncate.fn (stub->frame,
			       stub->frame->this,
			       stub->args.ftruncate.fd,
			       stub->args.ftruncate.off);
      break;
    }
  
  case GF_FOP_FSTAT:
    {
      stub->args.fstat.fn (stub->frame,
			   stub->frame->this,
			   stub->args.fstat.fd);
      break;
    }
  
  case GF_FOP_LK:
    {
      stub->args.lk.fn (stub->frame,
			stub->frame->this,
			stub->args.lk.fd,
			stub->args.lk.cmd,
			&stub->args.lk.lock);
      break;
    }
  
  case GF_FOP_UTIMENS:
    {
      stub->args.utimens.fn (stub->frame,
			     stub->frame->this,
			     &stub->args.utimens.loc,
			     stub->args.utimens.tv);
      loc_wipe (&stub->args.utimens.loc);
      
      break;
    }
  
  
    break;
  case GF_FOP_FCHMOD:
    {
      stub->args.fchmod.fn (stub->frame,
			    stub->frame->this,
			    stub->args.fchmod.fd,
			    stub->args.fchmod.mode);
      break;
    }
  
  case GF_FOP_FCHOWN:
    {
      stub->args.fchown.fn (stub->frame,
			    stub->frame->this,
			    stub->args.fchown.fd,
			    stub->args.fchown.uid,
			    stub->args.fchown.gid);
      break;
    }
  
  case GF_FOP_LOOKUP:
    {
      stub->args.lookup.fn (stub->frame, 
			    stub->frame->this,
			    &stub->args.lookup.loc);
      loc_wipe (&stub->args.lookup.loc);
      break;
    }

  case GF_FOP_FORGET:
    {
      stub->args.forget.fn (stub->frame, 
			    stub->frame->this,
			    stub->args.forget.inode);

      
      if (stub->args.forget.inode)
	inode_unref (stub->args.forget.inode);

      
      
      
    }
    break;

  case GF_FOP_WRITEDIR:
    {
      dir_entry_t *entry, *next;
      stub->args.writedir.fn (stub->frame,
			      stub->frame->this,
			      stub->args.writedir.fd,
			      stub->args.writedir.flags,
			      &stub->args.writedir.entries,
			      stub->args.writedir.count);
      entry = stub->args.writedir.entries.next;
      while (entry) {
	next = entry->next;
	free (entry->name);
	free (entry);
	entry = next;
      }
      break;
    }

  case GF_FOP_MAXVALUE:
    {
      gf_log ("call-stub",
	      GF_LOG_DEBUG,
	      "Invalid value of FOP");
    }
    break;
  }
}


static void
call_resume_unwind (call_stub_t *stub)
{
  switch (stub->fop) {
  case GF_FOP_OPEN:
    {
      if (!stub->args.open_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.open_cbk.op_ret,
		      stub->args.open_cbk.op_errno,
		      stub->args.open_cbk.fd);
      else
	stub->args.open_cbk.fn (stub->frame, 
				stub->frame->cookie,
				stub->frame->this,
				stub->args.open_cbk.op_ret, 
				stub->args.open_cbk.op_errno,
				stub->args.open_cbk.fd);
      break;
    }

  case GF_FOP_CREATE:
    {
      if (!stub->args.create_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.create_cbk.op_ret,
		      stub->args.create_cbk.op_errno,
		      stub->args.create_cbk.fd,
		      stub->args.create_cbk.inode,
		      &stub->args.create_cbk.buf);
      else
	stub->args.create_cbk.fn (stub->frame,
				  stub->frame->cookie,
				  stub->frame->this,
				  stub->args.create_cbk.op_ret,
				  stub->args.create_cbk.op_errno,
				  stub->args.create_cbk.fd,
				  stub->args.create_cbk.inode,
				  &stub->args.create_cbk.buf);
      
      
      if (stub->args.create_cbk.inode)
	inode_unref (stub->args.create_cbk.inode);
      
      break;
    }

  case GF_FOP_STAT:
    {
      if (!stub->args.stat_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.stat_cbk.op_ret,
		      stub->args.stat_cbk.op_errno,
		      &stub->args.stat_cbk.buf);
      else
	stub->args.stat_cbk.fn (stub->frame,
				stub->frame->cookie,
				stub->frame->this,
				stub->args.stat_cbk.op_ret,
				stub->args.stat_cbk.op_errno,
				&stub->args.stat_cbk.buf);
      break;
    }

  case GF_FOP_READLINK:
    {
      if (!stub->args.readlink_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.readlink_cbk.op_ret,
		      stub->args.readlink_cbk.op_errno,
		      stub->args.readlink_cbk.buf);
      else
	stub->args.readlink_cbk.fn (stub->frame,
				    stub->frame->cookie,
				    stub->frame->this,
				    stub->args.readlink_cbk.op_ret,
				    stub->args.readlink_cbk.op_errno,
				    stub->args.readlink_cbk.buf);
      free ((char *)stub->args.readlink_cbk.buf);
      break;
    }
  
  case GF_FOP_MKNOD:
    {
      if (!stub->args.mknod_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.mknod_cbk.op_ret,
		      stub->args.mknod_cbk.op_errno,
		      stub->args.mknod_cbk.inode,
		      &stub->args.mknod_cbk.buf);
      else
	stub->args.mknod_cbk.fn (stub->frame,
				 stub->frame->cookie,
				 stub->frame->this,
				 stub->args.mknod_cbk.op_ret,
				 stub->args.mknod_cbk.op_errno,
				 stub->args.mknod_cbk.inode,
				 &stub->args.mknod_cbk.buf);

      if (stub->args.mknod_cbk.inode)
	inode_unref (stub->args.mknod_cbk.inode);
      break;
    }

  case GF_FOP_MKDIR:
    {
      if (!stub->args.mkdir_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.mkdir_cbk.op_ret,
		      stub->args.mkdir_cbk.op_errno,
		      stub->args.mkdir_cbk.inode,
		      &stub->args.mkdir_cbk.buf);
      else
	stub->args.mkdir_cbk.fn (stub->frame,
				 stub->frame->cookie,
				 stub->frame->this,
				 stub->args.mkdir_cbk.op_ret,
				 stub->args.mkdir_cbk.op_errno,
				 stub->args.mkdir_cbk.inode,
				 &stub->args.mkdir_cbk.buf);

      if (stub->args.mkdir_cbk.inode)
	inode_unref (stub->args.mkdir_cbk.inode);
      break;
    }
  
  case GF_FOP_UNLINK:
    {
      if (!stub->args.unlink_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.unlink_cbk.op_ret,
		      stub->args.unlink_cbk.op_errno);
      else
	stub->args.unlink_cbk.fn (stub->frame,
				  stub->frame->cookie,
				  stub->frame->this,
				  stub->args.unlink_cbk.op_ret,
				  stub->args.unlink_cbk.op_errno);
      break;
    }
  
  case GF_FOP_RMDIR:
    {
      if (!stub->args.rmdir_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.rmdir_cbk.op_ret,
		      stub->args.rmdir_cbk.op_errno);
      else
	stub->args.unlink_cbk.fn (stub->frame,
				  stub->frame->cookie,
				  stub->frame->this,
				  stub->args.rmdir_cbk.op_ret,
				  stub->args.rmdir_cbk.op_errno);
      break;
    }

  case GF_FOP_SYMLINK:
    {
      if (!stub->args.symlink_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.symlink_cbk.op_ret,
		      stub->args.symlink_cbk.op_errno,
		      stub->args.symlink_cbk.inode,
		      &stub->args.symlink_cbk.buf);
      else
	stub->args.symlink_cbk.fn (stub->frame,
				   stub->frame->cookie,
				   stub->frame->this,
				   stub->args.symlink_cbk.op_ret,
				   stub->args.symlink_cbk.op_errno,
				   stub->args.symlink_cbk.inode,
				   &stub->args.symlink_cbk.buf);

      if (stub->args.symlink_cbk.inode)
	inode_unref (stub->args.symlink_cbk.inode);
    }
    break;
  
  case GF_FOP_RENAME:
    {
#if 0
      if (!stub->args.rename_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.rename_cbk.op_ret,
		      stub->args.rename_cbk.op_errno,
		      &stub->args.rename_cbk.buf);
      else
	stub->args.rename_cbk.fn (stub->frame,
				  stub->frame->cookie,
				  stub->frame->this,
				  stub->args.rename_cbk.op_ret,
				  stub->args.rename_cbk.op_errno,
				  &stub->args.rename_cbk.buf);
#endif
      break;
    }
  
  case GF_FOP_LINK:
    {
      if (!stub->args.link_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.link_cbk.op_ret,
		      stub->args.link_cbk.op_errno,
		      stub->args.link_cbk.inode,
		      &stub->args.link_cbk.buf);
      else
	stub->args.link_cbk.fn (stub->frame,
				stub->frame->cookie,
				stub->frame->this,
				stub->args.link_cbk.op_ret,
				stub->args.link_cbk.op_errno,
				stub->args.link_cbk.inode,
				&stub->args.link_cbk.buf);

      if (stub->args.link_cbk.inode)
	inode_unref (stub->args.link_cbk.inode);
      break;
    }
  
  case GF_FOP_CHMOD:
    {
      if (!stub->args.chmod_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.chmod_cbk.op_ret,
		      stub->args.chmod_cbk.op_errno,
		      &stub->args.chmod_cbk.buf);
      else
	stub->args.chmod_cbk.fn (stub->frame,
				 stub->frame->cookie,
				 stub->frame->this,
				 stub->args.chmod_cbk.op_ret,
				 stub->args.chmod_cbk.op_errno,
				 &stub->args.chmod_cbk.buf);
      break;
    }
  
  case GF_FOP_CHOWN:
    {
      if (!stub->args.chown_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.chown_cbk.op_ret,
		      stub->args.chown_cbk.op_errno,
		      &stub->args.chown_cbk.buf);
      else
	stub->args.chown_cbk.fn (stub->frame,
				 stub->frame->cookie,
				 stub->frame->this,
				 stub->args.chown_cbk.op_ret,
				 stub->args.chown_cbk.op_errno,
				 &stub->args.chown_cbk.buf);
      break;
    }
  
  case GF_FOP_TRUNCATE:
    {
      if (!stub->args.truncate_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.truncate_cbk.op_ret,
		      stub->args.truncate_cbk.op_errno,
		      &stub->args.truncate_cbk.buf);
      else
	stub->args.truncate_cbk.fn (stub->frame,
				    stub->frame->cookie,
				    stub->frame->this,
				    stub->args.truncate_cbk.op_ret,
				    stub->args.truncate_cbk.op_errno,
				    &stub->args.truncate_cbk.buf);
      break;
    }
      
    case GF_FOP_READ:
      {
	dict_t *refs = stub->frame->root->rsp_refs;
	int32_t ret = stub->args.readv_cbk.op_ret;

	if (!stub->args.readv_cbk.fn)
	  STACK_UNWIND (stub->frame,
			stub->args.readv_cbk.op_ret,
			stub->args.readv_cbk.op_errno,
			stub->args.readv_cbk.vector,
			stub->args.readv_cbk.count,
			&stub->args.readv_cbk.stbuf);
	else
	  stub->args.readv_cbk.fn (stub->frame,
				   stub->frame->cookie,
				   stub->frame->this,
				   stub->args.readv_cbk.op_ret,
				   stub->args.readv_cbk.op_errno,
				   stub->args.readv_cbk.vector,
				   stub->args.readv_cbk.count,
				   &stub->args.readv_cbk.stbuf);
	free ((char *)stub->args.readv_cbk.vector);

	if (refs && ret >= 0)
	  dict_unref (refs);
      }
      break;
  
  case GF_FOP_WRITE:
    {
      if (!stub->args.writev_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.writev_cbk.op_ret,
		      stub->args.writev_cbk.op_errno,
		      &stub->args.writev_cbk.stbuf);
      else
	stub->args.writev_cbk.fn (stub->frame,
				  stub->frame->cookie,
				  stub->frame->this,
				  stub->args.writev_cbk.op_ret,
				  stub->args.writev_cbk.op_errno,
				  &stub->args.writev_cbk.stbuf);
      break;
    }
  
  case GF_FOP_STATFS:
    {
      if (!stub->args.statfs_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.statfs_cbk.op_ret,
		      stub->args.statfs_cbk.op_errno,
		      &(stub->args.statfs_cbk.buf));
      else
	stub->args.statfs_cbk.fn (stub->frame,
				  stub->frame->cookie,
				  stub->frame->this,
				  stub->args.statfs_cbk.op_ret,
				  stub->args.statfs_cbk.op_errno,
				  &(stub->args.statfs_cbk.buf));
    }
    break;
  
  case GF_FOP_FLUSH:
    {
      if (!stub->args.flush_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.flush_cbk.op_ret,
		      stub->args.flush_cbk.op_errno);
      else
	stub->args.flush_cbk.fn (stub->frame,
				 stub->frame->cookie,
				 stub->frame->this,
				 stub->args.flush_cbk.op_ret,
				 stub->args.flush_cbk.op_errno);
      
      break;
    }
  
  case GF_FOP_CLOSE:
    {
      if (!stub->args.close_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.close_cbk.op_ret,
		      stub->args.close_cbk.op_errno);
      else
	stub->args.close_cbk.fn (stub->frame,
				 stub->frame->cookie,
				 stub->frame->this,
				 stub->args.close_cbk.op_ret,
				 stub->args.close_cbk.op_errno);
      break;
    }
  
  case GF_FOP_FSYNC:
    {
      if (!stub->args.fsync_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.fsync_cbk.op_ret,
		      stub->args.fsync_cbk.op_errno);
      else
	stub->args.fsync_cbk.fn (stub->frame,
				 stub->frame->cookie,
				 stub->frame->this,
				 stub->args.fsync_cbk.op_ret,
				 stub->args.fsync_cbk.op_errno);
      break;
    }
  
  case GF_FOP_SETXATTR:
    {
      if (!stub->args.setxattr_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.setxattr_cbk.op_ret,
		      stub->args.setxattr_cbk.op_errno);

      else
	stub->args.setxattr_cbk.fn (stub->frame,
				    stub->frame->cookie,
				    stub->frame->this,
				    stub->args.setxattr_cbk.op_ret,
				    stub->args.setxattr_cbk.op_errno);

      break;
    }
  
  case GF_FOP_GETXATTR:
    {
      if (!stub->args.getxattr_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.getxattr_cbk.op_ret,
		      stub->args.getxattr_cbk.op_errno,
		      stub->args.getxattr_cbk.dict);
      else
	stub->args.getxattr_cbk.fn (stub->frame,
				    stub->frame->cookie,
				    stub->frame->this,
				    stub->args.getxattr_cbk.op_ret,
				    stub->args.getxattr_cbk.op_errno,
				    stub->args.getxattr_cbk.dict);
      if (stub->args.getxattr_cbk.dict)
	dict_unref (stub->args.getxattr_cbk.dict);
      break;
    }
  
  case GF_FOP_REMOVEXATTR:
    {
      if (!stub->args.removexattr_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.removexattr_cbk.op_ret,
		      stub->args.removexattr_cbk.op_errno);
      else
	stub->args.removexattr_cbk.fn (stub->frame,
				       stub->frame->cookie,
				       stub->frame->this,
				       stub->args.removexattr_cbk.op_ret,
				       stub->args.removexattr_cbk.op_errno);

      break;
    }
  
  case GF_FOP_OPENDIR:
    {
      if (!stub->args.opendir_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.opendir_cbk.op_ret,
		      stub->args.opendir_cbk.op_errno,
		      stub->args.opendir_cbk.fd);
      else
	stub->args.opendir_cbk.fn (stub->frame,
				   stub->frame->cookie,
				   stub->frame->this,
				   stub->args.opendir_cbk.op_ret,
				   stub->args.opendir_cbk.op_errno,
				   stub->args.opendir_cbk.fd);
      break;
    }
  
  case GF_FOP_READDIR:
    {
      dir_entry_t *entry, *next;
      if (!stub->args.readdir_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.readdir_cbk.op_ret,
		      stub->args.readdir_cbk.op_errno,
		      &stub->args.readdir_cbk.entries,
		      stub->args.readdir_cbk.count);
      else
	stub->args.readdir_cbk.fn (stub->frame,
				   stub->frame->cookie,
				   stub->frame->this,
				   stub->args.readdir_cbk.op_ret,
				   stub->args.readdir_cbk.op_errno,
				   &stub->args.readdir_cbk.entries,
				   stub->args.readdir_cbk.count);
      entry = stub->args.readdir_cbk.entries.next;
      while (entry) {
	next = entry->next;
	free (entry->name);
	free (entry);
	entry = next;
      }
      break;
    }
  
  case GF_FOP_CLOSEDIR:
    {
      if (!stub->args.closedir_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.closedir_cbk.op_ret,
		      stub->args.closedir_cbk.op_errno);	
      else
	stub->args.closedir_cbk.fn (stub->frame,
				    stub->frame->cookie,
				    stub->frame->this,
				    stub->args.closedir_cbk.op_ret,
				    stub->args.closedir_cbk.op_errno);
      break;
    }
  
  case GF_FOP_FSYNCDIR:
    {
      if (!stub->args.fsyncdir_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.fsyncdir_cbk.op_ret,
		      stub->args.fsyncdir_cbk.op_errno);
      else
	stub->args.fsyncdir_cbk.fn (stub->frame,
				    stub->frame->cookie,
				    stub->frame->this,
				    stub->args.fsyncdir_cbk.op_ret,
				    stub->args.fsyncdir_cbk.op_errno);
      break;
    }
  
  case GF_FOP_ACCESS:
    {
      if (!stub->args.access_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.access_cbk.op_ret,
		      stub->args.access_cbk.op_errno);
      else
	stub->args.access_cbk.fn (stub->frame,
				  stub->frame->cookie,
				  stub->frame->this,
				  stub->args.access_cbk.op_ret,
				  stub->args.access_cbk.op_errno);

      break;
    }
  
  case GF_FOP_FTRUNCATE:
    {
      if (!stub->args.ftruncate_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.ftruncate_cbk.op_ret,
		      stub->args.ftruncate_cbk.op_errno,
		      &stub->args.ftruncate_cbk.buf);
      else
	stub->args.ftruncate_cbk.fn (stub->frame,
				     stub->frame->cookie,
				     stub->frame->this,
				     stub->args.ftruncate_cbk.op_ret,
				     stub->args.ftruncate_cbk.op_errno,
				     &stub->args.ftruncate_cbk.buf);
      break;
    }
  
  case GF_FOP_FSTAT:
    {
      if (!stub->args.fstat_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.fstat_cbk.op_ret,
		      stub->args.fstat_cbk.op_errno,
		      &stub->args.fstat_cbk.buf);
      else
	stub->args.fstat_cbk.fn (stub->frame,
				 stub->frame->cookie,
				 stub->frame->this,
				 stub->args.fstat_cbk.op_ret,
				 stub->args.fstat_cbk.op_errno,
				 &stub->args.fstat_cbk.buf);
      
      break;
    }
  
  case GF_FOP_LK:
    {
      if (!stub->args.lk_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.lk_cbk.op_ret,
		      stub->args.lk_cbk.op_errno,
		      &stub->args.lk_cbk.lock);
      else
	stub->args.lk_cbk.fn (stub->frame,
			      stub->frame->cookie,
			      stub->frame->this,
			      stub->args.lk_cbk.op_ret,
			      stub->args.lk_cbk.op_errno,
			      &stub->args.lk_cbk.lock);
      break;
    }
  
  case GF_FOP_UTIMENS:
    {
      if (!stub->args.utimens_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.utimens_cbk.op_ret,
		      stub->args.utimens_cbk.op_errno,
		      &stub->args.utimens_cbk.buf);
      else
	stub->args.utimens_cbk.fn (stub->frame,
				   stub->frame->cookie,
				   stub->frame->this,
				   stub->args.utimens_cbk.op_ret,
				   stub->args.utimens_cbk.op_errno,
				   &stub->args.utimens_cbk.buf);
      
      break;
    }
  
  
    break;
  case GF_FOP_FCHMOD:
    {
      if (!stub->args.fchmod_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.fchmod_cbk.op_ret,
		      stub->args.fchmod_cbk.op_errno,
		      &stub->args.fchmod_cbk.buf);
      else
	stub->args.fchmod_cbk.fn (stub->frame,
				  stub->frame->cookie,
				  stub->frame->this,
				  stub->args.fchmod_cbk.op_ret,
				  stub->args.fchmod_cbk.op_errno,
				  &stub->args.fchmod_cbk.buf);
      break;
    }
  
  case GF_FOP_FCHOWN:
    {
      if (!stub->args.fchown_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.fchown_cbk.op_ret,
		      stub->args.fchown_cbk.op_errno,
		      &stub->args.fchown_cbk.buf);
      else
	stub->args.fchown_cbk.fn (stub->frame,
				  stub->frame->cookie,
				  stub->frame->this,
				  stub->args.fchown_cbk.op_ret,
				  stub->args.fchown_cbk.op_errno,
				  &stub->args.fchown_cbk.buf);

      break;
    }
  
  case GF_FOP_LOOKUP:
    {
      if (!stub->args.lookup_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.lookup_cbk.op_ret,
		      stub->args.lookup_cbk.op_errno,
		      stub->args.lookup_cbk.inode,
		      &stub->args.lookup_cbk.buf);
      else
	stub->args.lookup_cbk.fn (stub->frame, 
				  stub->frame->cookie,
				  stub->frame->this,
				  stub->args.lookup_cbk.op_ret,
				  stub->args.lookup_cbk.op_errno,
				  stub->args.lookup_cbk.inode,
				  &stub->args.lookup_cbk.buf);
      /*
      if (stub->args.lookup_cbk.inode)
	inode_unref (stub->args.lookup_cbk.inode);

      */
      
      break;
    }
  case GF_FOP_FORGET:
    {
      if (!stub->args.forget_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.forget_cbk.op_ret,
		      stub->args.forget_cbk.op_errno);
      else
	stub->args.forget_cbk.fn (stub->frame, 
				  stub->frame->cookie,
				  stub->frame->this,
				  stub->args.forget_cbk.op_ret,
				  stub->args.forget_cbk.op_errno);
      break;
    }
  case GF_FOP_WRITEDIR:
    {
      if (!stub->args.writedir_cbk.fn)
	STACK_UNWIND (stub->frame,
		      stub->args.writedir_cbk.op_ret,
		      stub->args.writedir_cbk.op_errno);
      else
	stub->args.writedir_cbk.fn (stub->frame,
				    stub->frame->cookie,
				    stub->frame->this,
				    stub->args.writedir_cbk.op_ret,
				    stub->args.writedir_cbk.op_errno);
      break;
    }
  case GF_FOP_MAXVALUE:
    {
      gf_log ("call-stub",
	      GF_LOG_DEBUG,
	      "Invalid value of FOP");
    }
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
