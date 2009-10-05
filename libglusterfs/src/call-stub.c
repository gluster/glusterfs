/*
  Copyright (c) 2007-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include <inttypes.h>

#include "md5.h"
#include "call-stub.h"


static call_stub_t *
stub_new (call_frame_t *frame,
	  char wind,
	  glusterfs_fop_t fop)
{
	call_stub_t *new = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	new = CALLOC (1, sizeof (*new));
	GF_VALIDATE_OR_GOTO ("call-stub", new, out);

	new->frame = frame;
	new->wind = wind;
	new->fop = fop;

	INIT_LIST_HEAD (&new->list);
out:
	return new;
}


call_stub_t *
fop_lookup_stub (call_frame_t *frame,
		 fop_lookup_t fn,
		 loc_t *loc,
		 dict_t *xattr_req)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

	stub = stub_new (frame, 1, GF_FOP_LOOKUP);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.lookup.fn = fn;

	if (xattr_req)
		stub->args.lookup.xattr_req = dict_ref (xattr_req);

	loc_copy (&stub->args.lookup.loc, loc);
out:
	return stub;
}


call_stub_t *
fop_lookup_cbk_stub (call_frame_t *frame,
		     fop_lookup_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     inode_t *inode,
		     struct stat *buf,
                     dict_t *dict,
                     struct stat *postparent)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_LOOKUP);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.lookup_cbk.fn = fn;
	stub->args.lookup_cbk.op_ret = op_ret;
	stub->args.lookup_cbk.op_errno = op_errno;
	if (inode)
		stub->args.lookup_cbk.inode = inode_ref (inode);
	if (buf)
		stub->args.lookup_cbk.buf = *buf;
	if (dict)
		stub->args.lookup_cbk.dict = dict_ref (dict);
        if (postparent)
                stub->args.lookup_cbk.postparent = *postparent;
out:
	return stub;
}



call_stub_t *
fop_stat_stub (call_frame_t *frame,
	       fop_stat_t fn,
	       loc_t *loc)
{
	call_stub_t *stub = NULL;
  
	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

	stub = stub_new (frame, 1, GF_FOP_STAT);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.stat.fn = fn;
	loc_copy (&stub->args.stat.loc, loc);
out:
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
	
	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_STAT);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.stat_cbk.fn = fn;
	stub->args.stat_cbk.op_ret = op_ret;
	stub->args.stat_cbk.op_errno = op_errno;
	if (op_ret == 0)
		stub->args.stat_cbk.buf = *buf;
out:
	return stub;
}


call_stub_t *
fop_fstat_stub (call_frame_t *frame,
		fop_fstat_t fn,
		fd_t *fd)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 1, GF_FOP_FSTAT);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.fstat.fn = fn;

	if (fd)
		stub->args.fstat.fd = fd_ref (fd);
out:
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

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_FSTAT);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.fstat_cbk.fn = fn;
	stub->args.fstat_cbk.op_ret = op_ret;
	stub->args.fstat_cbk.op_errno = op_errno;
	if (buf)
		stub->args.fstat_cbk.buf = *buf;
out:
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

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);	
	GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

	stub = stub_new (frame, 1, GF_FOP_TRUNCATE);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.truncate.fn = fn;
	loc_copy (&stub->args.truncate.loc, loc);
	stub->args.truncate.off = off;
out:
	return stub;
}


call_stub_t *
fop_truncate_cbk_stub (call_frame_t *frame,
		       fop_truncate_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct stat *prebuf,
                       struct stat *postbuf)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_TRUNCATE);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.truncate_cbk.fn = fn;
	stub->args.truncate_cbk.op_ret = op_ret;
	stub->args.truncate_cbk.op_errno = op_errno;
	if (prebuf)
		stub->args.truncate_cbk.prebuf = *prebuf;
        if (postbuf)
                stub->args.truncate_cbk.postbuf = *postbuf;
out:
	return stub;
}


call_stub_t *
fop_ftruncate_stub (call_frame_t *frame,
		    fop_ftruncate_t fn,
		    fd_t *fd,
		    off_t off)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 1, GF_FOP_FTRUNCATE);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.ftruncate.fn = fn;
	if (fd)
		stub->args.ftruncate.fd = fd_ref (fd);

	stub->args.ftruncate.off = off;
out:
	return stub;
}


call_stub_t *
fop_ftruncate_cbk_stub (call_frame_t *frame,
			fop_ftruncate_cbk_t fn,
			int32_t op_ret,
			int32_t op_errno,
			struct stat *prebuf,
                        struct stat *postbuf)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_FTRUNCATE);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.ftruncate_cbk.fn = fn;
	stub->args.ftruncate_cbk.op_ret = op_ret;
	stub->args.ftruncate_cbk.op_errno = op_errno;
	if (prebuf)
		stub->args.ftruncate_cbk.prebuf = *prebuf;
	if (postbuf)
		stub->args.ftruncate_cbk.postbuf = *postbuf;
out:
	return stub;
}


call_stub_t *
fop_access_stub (call_frame_t *frame,
		 fop_access_t fn,
		 loc_t *loc,
		 int32_t mask)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

	stub = stub_new (frame, 1, GF_FOP_ACCESS);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.access.fn = fn;
	loc_copy (&stub->args.access.loc, loc);
	stub->args.access.mask = mask;
out:
	return stub;
}


call_stub_t *
fop_access_cbk_stub (call_frame_t *frame,
		     fop_access_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_ACCESS);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.access_cbk.fn = fn;
	stub->args.access_cbk.op_ret = op_ret;
	stub->args.access_cbk.op_errno = op_errno;
out:
	return stub;
}


call_stub_t *
fop_readlink_stub (call_frame_t *frame,
		   fop_readlink_t fn,
		   loc_t *loc,
		   size_t size)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", loc, out);
	
	stub = stub_new (frame, 1, GF_FOP_READLINK);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.readlink.fn = fn;
	loc_copy (&stub->args.readlink.loc, loc);
	stub->args.readlink.size = size;
out:
	return stub;
}


call_stub_t *
fop_readlink_cbk_stub (call_frame_t *frame,
		       fop_readlink_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       const char *path,
                       struct stat *sbuf)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_READLINK);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.readlink_cbk.fn = fn;
	stub->args.readlink_cbk.op_ret = op_ret;
	stub->args.readlink_cbk.op_errno = op_errno;
	if (path)
		stub->args.readlink_cbk.buf = strdup (path);
        if (sbuf)
                stub->args.readlink_cbk.sbuf = *sbuf;
out:
	return stub;
}


call_stub_t *
fop_mknod_stub (call_frame_t *frame,
		fop_mknod_t fn,
		loc_t *loc,
		mode_t mode,
		dev_t rdev)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

	stub = stub_new (frame, 1, GF_FOP_MKNOD);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.mknod.fn = fn;
	loc_copy (&stub->args.mknod.loc, loc);
	stub->args.mknod.mode = mode;
	stub->args.mknod.rdev = rdev;
out:
	return stub;
}


call_stub_t *
fop_mknod_cbk_stub (call_frame_t *frame,
		    fop_mknod_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
                    struct stat *buf,
                    struct stat *preparent,
                    struct stat *postparent)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_MKNOD);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.mknod_cbk.fn = fn;
	stub->args.mknod_cbk.op_ret = op_ret;
	stub->args.mknod_cbk.op_errno = op_errno;
	if (inode)
		stub->args.mknod_cbk.inode = inode_ref (inode);
	if (buf)
		stub->args.mknod_cbk.buf = *buf;
        if (preparent)
                stub->args.mknod_cbk.preparent = *preparent;
        if (postparent)
                stub->args.mknod_cbk.postparent = *postparent;
out:
	return stub;
}


call_stub_t *
fop_mkdir_stub (call_frame_t *frame,
		fop_mkdir_t fn,
		loc_t *loc,
		mode_t mode)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

	stub = stub_new (frame, 1, GF_FOP_MKDIR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.mkdir.fn = fn;
	loc_copy (&stub->args.mkdir.loc, loc);
	stub->args.mkdir.mode = mode;
out:
	return stub;
}


call_stub_t *
fop_mkdir_cbk_stub (call_frame_t *frame,
		    fop_mkdir_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
                    struct stat *buf,
                    struct stat *preparent,
                    struct stat *postparent)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_MKDIR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.mkdir_cbk.fn = fn;
	stub->args.mkdir_cbk.op_ret = op_ret;
	stub->args.mkdir_cbk.op_errno = op_errno;
	if (inode)
		stub->args.mkdir_cbk.inode = inode_ref (inode);
	if (buf)
		stub->args.mkdir_cbk.buf = *buf;
        if (preparent)
                stub->args.mkdir_cbk.preparent = *preparent;
        if (postparent)
                stub->args.mkdir_cbk.postparent = *postparent;
out:
	return stub;
}


call_stub_t *
fop_unlink_stub (call_frame_t *frame,
		 fop_unlink_t fn,
		 loc_t *loc)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

	stub = stub_new (frame, 1, GF_FOP_UNLINK);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.unlink.fn = fn;
	loc_copy (&stub->args.unlink.loc, loc);
out:
	return stub;
}


call_stub_t *
fop_unlink_cbk_stub (call_frame_t *frame,
		     fop_unlink_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
                     struct stat *preparent,
                     struct stat *postparent)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_UNLINK);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.unlink_cbk.fn = fn;
	stub->args.unlink_cbk.op_ret = op_ret;
	stub->args.unlink_cbk.op_errno = op_errno;
        if (preparent)
                stub->args.unlink_cbk.preparent = *preparent;
        if (postparent)
                stub->args.unlink_cbk.postparent = *postparent;
out:
	return stub;
}



call_stub_t *
fop_rmdir_stub (call_frame_t *frame,
		fop_rmdir_t fn,
		loc_t *loc)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

	stub = stub_new (frame, 1, GF_FOP_RMDIR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.rmdir.fn = fn;
	loc_copy (&stub->args.rmdir.loc, loc);
out:
	return stub;
}


call_stub_t *
fop_rmdir_cbk_stub (call_frame_t *frame,
		    fop_rmdir_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
                    struct stat *preparent,
                    struct stat *postparent)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_RMDIR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.rmdir_cbk.fn = fn;
	stub->args.rmdir_cbk.op_ret = op_ret;
	stub->args.rmdir_cbk.op_errno = op_errno;
        if (preparent)
                stub->args.rmdir_cbk.preparent = *preparent;
        if (postparent)
                stub->args.rmdir_cbk.postparent = *postparent;
out:
	return stub;
}


call_stub_t *
fop_symlink_stub (call_frame_t *frame,
		  fop_symlink_t fn,
		  const char *linkname,
		  loc_t *loc)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", loc, out);
	GF_VALIDATE_OR_GOTO ("call-stub", linkname, out);

	stub = stub_new (frame, 1, GF_FOP_SYMLINK);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.symlink.fn = fn;
	stub->args.symlink.linkname = strdup (linkname);
	loc_copy (&stub->args.symlink.loc, loc);
out:
	return stub;
}


call_stub_t *
fop_symlink_cbk_stub (call_frame_t *frame,
		      fop_symlink_cbk_t fn,
		      int32_t op_ret,
		      int32_t op_errno,
		      inode_t *inode,
                      struct stat *buf,
                      struct stat *preparent,
                      struct stat *postparent)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_SYMLINK);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.symlink_cbk.fn = fn;
	stub->args.symlink_cbk.op_ret = op_ret;
	stub->args.symlink_cbk.op_errno = op_errno;
	if (inode)
		stub->args.symlink_cbk.inode = inode_ref (inode);
	if (buf)
		stub->args.symlink_cbk.buf = *buf;
        if (preparent)
                stub->args.symlink_cbk.preparent = *preparent;
        if (postparent)
                stub->args.symlink_cbk.postparent = *postparent;
out:
	return stub;
}


call_stub_t *
fop_rename_stub (call_frame_t *frame,
		 fop_rename_t fn,
		 loc_t *oldloc,
		 loc_t *newloc)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", oldloc, out);
	GF_VALIDATE_OR_GOTO ("call-stub", newloc, out);

	stub = stub_new (frame, 1, GF_FOP_RENAME);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.rename.fn = fn;
	loc_copy (&stub->args.rename.old, oldloc);
	loc_copy (&stub->args.rename.new, newloc);
out:
	return stub;
}


call_stub_t *
fop_rename_cbk_stub (call_frame_t *frame,
		     fop_rename_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf,
                     struct stat *preoldparent,
                     struct stat *postoldparent,
                     struct stat *prenewparent,
                     struct stat *postnewparent)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_RENAME);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.rename_cbk.fn = fn;
	stub->args.rename_cbk.op_ret = op_ret;
	stub->args.rename_cbk.op_errno = op_errno;
	if (buf)
		stub->args.rename_cbk.buf = *buf;
        if (preoldparent)
                stub->args.rename_cbk.preoldparent = *preoldparent;
        if (postoldparent)
                stub->args.rename_cbk.postoldparent = *postoldparent;
        if (prenewparent)
                stub->args.rename_cbk.prenewparent = *prenewparent;
        if (postnewparent)
                stub->args.rename_cbk.postnewparent = *postnewparent;
out:
	return stub;
}


call_stub_t *
fop_link_stub (call_frame_t *frame,
	       fop_link_t fn,
	       loc_t *oldloc,
	       loc_t *newloc)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", oldloc, out);
	GF_VALIDATE_OR_GOTO ("call-stub", newloc, out);

	stub = stub_new (frame, 1, GF_FOP_LINK);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.link.fn = fn;
	loc_copy (&stub->args.link.oldloc, oldloc);
	loc_copy (&stub->args.link.newloc, newloc);

out:
	return stub;
}


call_stub_t *
fop_link_cbk_stub (call_frame_t *frame,
		   fop_link_cbk_t fn,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
                   struct stat *buf,
                   struct stat *preparent,
                   struct stat *postparent)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_LINK);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.link_cbk.fn = fn;
	stub->args.link_cbk.op_ret = op_ret;
	stub->args.link_cbk.op_errno = op_errno;
	if (inode)
		stub->args.link_cbk.inode = inode_ref (inode);
	if (buf)
		stub->args.link_cbk.buf = *buf;
        if (preparent)
                stub->args.link_cbk.preparent = *preparent;
        if (postparent)
                stub->args.link_cbk.postparent = *postparent;
out:
	return stub;
}


call_stub_t *
fop_create_stub (call_frame_t *frame,
		 fop_create_t fn,
		 loc_t *loc,
		 int32_t flags,
		 mode_t mode, fd_t *fd)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

	stub = stub_new (frame, 1, GF_FOP_CREATE);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.create.fn = fn;
	loc_copy (&stub->args.create.loc, loc);
	stub->args.create.flags = flags;
	stub->args.create.mode = mode;
	if (fd)
		stub->args.create.fd = fd_ref (fd);
out:
	return stub;
}


call_stub_t *
fop_create_cbk_stub (call_frame_t *frame,
		     fop_create_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
		     fd_t *fd,
		     inode_t *inode,
		     struct stat *buf,
                     struct stat *preparent,
                     struct stat *postparent)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_CREATE);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.create_cbk.fn = fn;
	stub->args.create_cbk.op_ret = op_ret;
	stub->args.create_cbk.op_errno = op_errno;
	if (fd)
		stub->args.create_cbk.fd = fd_ref (fd);
	if (inode)
		stub->args.create_cbk.inode = inode_ref (inode);
	if (buf)
		stub->args.create_cbk.buf = *buf;
        if (preparent)
                stub->args.create_cbk.preparent = *preparent;
        if (postparent)
                stub->args.create_cbk.postparent = *postparent;
out:
	return stub;
}


call_stub_t *
fop_open_stub (call_frame_t *frame,
	       fop_open_t fn,
	       loc_t *loc,
	       int32_t flags, fd_t *fd,
               int32_t wbflags)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

	stub = stub_new (frame, 1, GF_FOP_OPEN);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.open.fn = fn;
	loc_copy (&stub->args.open.loc, loc);
	stub->args.open.flags = flags;
        stub->args.open.wbflags = wbflags;
	if (fd)
		stub->args.open.fd = fd_ref (fd);
out:
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

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_OPEN);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.open_cbk.fn = fn;
	stub->args.open_cbk.op_ret = op_ret;
	stub->args.open_cbk.op_errno = op_errno;
	if (fd)
		stub->args.open_cbk.fd = fd_ref (fd);
out:
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

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 1, GF_FOP_READ);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.readv.fn = fn;
	if (fd)
		stub->args.readv.fd = fd_ref (fd);
	stub->args.readv.size = size;
	stub->args.readv.off = off;
out:
	return stub;
}


call_stub_t *
fop_readv_cbk_stub (call_frame_t *frame,
		    fop_readv_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct iovec *vector,
		    int32_t count,
		    struct stat *stbuf,
                    struct iobref *iobref)

{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_READ);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.readv_cbk.fn = fn;
	stub->args.readv_cbk.op_ret = op_ret;
	stub->args.readv_cbk.op_errno = op_errno;
	if (op_ret >= 0) {
		stub->args.readv_cbk.vector = iov_dup (vector, count);
		stub->args.readv_cbk.count = count;
		stub->args.readv_cbk.stbuf = *stbuf;
		stub->args.readv_cbk.iobref = iobref_ref (iobref);
	}
out:
	return stub;
}


call_stub_t *
fop_writev_stub (call_frame_t *frame,
		 fop_writev_t fn,
		 fd_t *fd,
		 struct iovec *vector,
		 int32_t count,
		 off_t off,
                 struct iobref *iobref)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", vector, out);

	stub = stub_new (frame, 1, GF_FOP_WRITE);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.writev.fn = fn;
	if (fd)
		stub->args.writev.fd = fd_ref (fd);
	stub->args.writev.vector = iov_dup (vector, count);
	stub->args.writev.count = count;
	stub->args.writev.off = off;
        stub->args.writev.iobref = iobref_ref (iobref);
out:
	return stub;
}


call_stub_t *
fop_writev_cbk_stub (call_frame_t *frame,
		     fop_writev_cbk_t fn,
		     int32_t op_ret,
		     int32_t op_errno,
                     struct stat *prebuf,
		     struct stat *postbuf)

{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_WRITE);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.writev_cbk.fn = fn;
	stub->args.writev_cbk.op_ret = op_ret;
	stub->args.writev_cbk.op_errno = op_errno;
	if (op_ret >= 0)
		stub->args.writev_cbk.postbuf = *postbuf;
        if (prebuf)
                stub->args.writev_cbk.prebuf = *prebuf;
out:
	return stub;
}



call_stub_t *
fop_flush_stub (call_frame_t *frame,
		fop_flush_t fn,
		fd_t *fd)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 1, GF_FOP_FLUSH);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.flush.fn = fn;
	if (fd)
		stub->args.flush.fd = fd_ref (fd);
out:
	return stub;
}


call_stub_t *
fop_flush_cbk_stub (call_frame_t *frame,
		    fop_flush_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno)

{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_FLUSH);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.flush_cbk.fn = fn;
	stub->args.flush_cbk.op_ret = op_ret;
	stub->args.flush_cbk.op_errno = op_errno;
out:
	return stub;
}




call_stub_t *
fop_fsync_stub (call_frame_t *frame,
		fop_fsync_t fn,
		fd_t *fd,
		int32_t datasync)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 1, GF_FOP_FSYNC);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.fsync.fn = fn;
	if (fd)
		stub->args.fsync.fd = fd_ref (fd);
	stub->args.fsync.datasync = datasync;
out:
	return stub;
}


call_stub_t *
fop_fsync_cbk_stub (call_frame_t *frame,
		    fop_fsync_cbk_t fn,
		    int32_t op_ret,
		    int32_t op_errno,
                    struct stat *prebuf,
                    struct stat *postbuf)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_FSYNC);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.fsync_cbk.fn = fn;
	stub->args.fsync_cbk.op_ret = op_ret;
	stub->args.fsync_cbk.op_errno = op_errno;
        if (prebuf)
                stub->args.fsync_cbk.prebuf = *prebuf;
        if (postbuf)
                stub->args.fsync_cbk.postbuf = *postbuf;
out:
	return stub;
}


call_stub_t *
fop_opendir_stub (call_frame_t *frame,
		  fop_opendir_t fn,
		  loc_t *loc, fd_t *fd)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

	stub = stub_new (frame, 1, GF_FOP_OPENDIR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.opendir.fn = fn;
	loc_copy (&stub->args.opendir.loc, loc);
	if (fd)
		stub->args.opendir.fd = fd_ref (fd);
out:
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

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_OPENDIR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.opendir_cbk.fn = fn;
	stub->args.opendir_cbk.op_ret = op_ret;
	stub->args.opendir_cbk.op_errno = op_errno;

	if (fd)
		stub->args.opendir_cbk.fd = fd_ref (fd);
out:
	return stub;
}


call_stub_t *
fop_getdents_stub (call_frame_t *frame,
		   fop_getdents_t fn,
		   fd_t *fd,
		   size_t size,
		   off_t off,
		   int32_t flag)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 1, GF_FOP_GETDENTS);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.getdents.fn = fn;
	stub->args.getdents.size = size;
	stub->args.getdents.off = off;
	if (fd)
		stub->args.getdents.fd = fd_ref (fd);
	stub->args.getdents.flag = flag;
out:
	return stub;
}


call_stub_t *
fop_getdents_cbk_stub (call_frame_t *frame,
		       fop_getdents_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       dir_entry_t *entries,
		       int32_t count)

{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_GETDENTS);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.getdents_cbk.fn = fn;
	stub->args.getdents_cbk.op_ret = op_ret;
	stub->args.getdents_cbk.op_errno = op_errno;
	if (op_ret >= 0) {
		stub->args.getdents_cbk.entries.next = entries->next;
		/* FIXME: are entries not needed in the caller after
		 * creating stub? */
		entries->next = NULL;
	}

	stub->args.getdents_cbk.count = count;
out:
	return stub;
}



call_stub_t *
fop_fsyncdir_stub (call_frame_t *frame,
		   fop_fsyncdir_t fn,
		   fd_t *fd,
		   int32_t datasync)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 1, GF_FOP_FSYNCDIR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.fsyncdir.fn = fn;
	if (fd)
		stub->args.fsyncdir.fd = fd_ref (fd);
	stub->args.fsyncdir.datasync = datasync;
out:
	return stub;
}


call_stub_t *
fop_fsyncdir_cbk_stub (call_frame_t *frame,
		       fop_fsyncdir_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno)

{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_FSYNCDIR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.fsyncdir_cbk.fn = fn;
	stub->args.fsyncdir_cbk.op_ret = op_ret;
	stub->args.fsyncdir_cbk.op_errno = op_errno;
out:
	return stub;
}


call_stub_t *
fop_statfs_stub (call_frame_t *frame,
		 fop_statfs_t fn,
		 loc_t *loc)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", loc, out); 

	stub = stub_new (frame, 1, GF_FOP_STATFS);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.statfs.fn = fn;
	loc_copy (&stub->args.statfs.loc, loc);
out:
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

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_STATFS);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.statfs_cbk.fn = fn;
	stub->args.statfs_cbk.op_ret = op_ret;
	stub->args.statfs_cbk.op_errno = op_errno;
	if (op_ret == 0)
		stub->args.statfs_cbk.buf = *buf;
out:
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

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

	stub = stub_new (frame, 1, GF_FOP_SETXATTR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.setxattr.fn = fn;
	loc_copy (&stub->args.setxattr.loc, loc);
	/* TODO */
	if (dict)
		stub->args.setxattr.dict = dict_ref (dict);
	stub->args.setxattr.flags = flags;
out:
	return stub;
}


call_stub_t *
fop_setxattr_cbk_stub (call_frame_t *frame,
		       fop_setxattr_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_SETXATTR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.setxattr_cbk.fn = fn;
	stub->args.setxattr_cbk.op_ret = op_ret;
	stub->args.setxattr_cbk.op_errno = op_errno;
out:
	return stub;
}

call_stub_t *
fop_getxattr_stub (call_frame_t *frame,
		   fop_getxattr_t fn,
		   loc_t *loc,
		   const char *name)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

	stub = stub_new (frame, 1, GF_FOP_GETXATTR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.getxattr.fn = fn;
	loc_copy (&stub->args.getxattr.loc, loc);

	if (name)
	        stub->args.getxattr.name = strdup (name);
out:
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

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_GETXATTR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.getxattr_cbk.fn = fn;
	stub->args.getxattr_cbk.op_ret = op_ret;
	stub->args.getxattr_cbk.op_errno = op_errno;
	/* TODO */
	if (dict)
		stub->args.getxattr_cbk.dict = dict_ref (dict);
out:
	return stub;
}


call_stub_t *
fop_fsetxattr_stub (call_frame_t *frame,
                    fop_fsetxattr_t fn,
                    fd_t *fd,
                    dict_t *dict,
                    int32_t flags)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", fd, out);

	stub = stub_new (frame, 1, GF_FOP_FSETXATTR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.fsetxattr.fn = fn;
	stub->args.fsetxattr.fd = fd_ref (fd);

	/* TODO */
	if (dict)
		stub->args.fsetxattr.dict = dict_ref (dict);
	stub->args.fsetxattr.flags = flags;
out:
	return stub;
}


call_stub_t *
fop_fsetxattr_cbk_stub (call_frame_t *frame,
                        fop_fsetxattr_cbk_t fn,
                        int32_t op_ret,
                        int32_t op_errno)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_FSETXATTR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.fsetxattr_cbk.fn = fn;
	stub->args.fsetxattr_cbk.op_ret = op_ret;
	stub->args.fsetxattr_cbk.op_errno = op_errno;
out:
	return stub;
}


call_stub_t *
fop_fgetxattr_stub (call_frame_t *frame,
                    fop_fgetxattr_t fn,
                    fd_t *fd,
                    const char *name)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", fd, out);

	stub = stub_new (frame, 1, GF_FOP_FGETXATTR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.fgetxattr.fn = fn;
	stub->args.fgetxattr.fd = fd_ref (fd);

	if (name)
	        stub->args.fgetxattr.name = strdup (name);
out:
	return stub;
}


call_stub_t *
fop_fgetxattr_cbk_stub (call_frame_t *frame,
                        fop_fgetxattr_cbk_t fn,
                        int32_t op_ret,
                        int32_t op_errno,
                        dict_t *dict)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_GETXATTR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.fgetxattr_cbk.fn = fn;
	stub->args.fgetxattr_cbk.op_ret = op_ret;
	stub->args.fgetxattr_cbk.op_errno = op_errno;

	/* TODO */
	if (dict)
		stub->args.fgetxattr_cbk.dict = dict_ref (dict);
out:
	return stub;
}


call_stub_t *
fop_removexattr_stub (call_frame_t *frame,
		      fop_removexattr_t fn,
		      loc_t *loc,
		      const char *name)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", loc, out);
	GF_VALIDATE_OR_GOTO ("call-stub", name, out);

	stub = stub_new (frame, 1, GF_FOP_REMOVEXATTR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.removexattr.fn = fn;
	loc_copy (&stub->args.removexattr.loc, loc);
	stub->args.removexattr.name = strdup (name);
out:
	return stub;
}


call_stub_t *
fop_removexattr_cbk_stub (call_frame_t *frame,
			  fop_removexattr_cbk_t fn,
			  int32_t op_ret,
			  int32_t op_errno)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_REMOVEXATTR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.removexattr_cbk.fn = fn;
	stub->args.removexattr_cbk.op_ret = op_ret;
	stub->args.removexattr_cbk.op_errno = op_errno;
out:
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

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", lock, out);
	
	stub = stub_new (frame, 1, GF_FOP_LK);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.lk.fn = fn;
	if (fd)
		stub->args.lk.fd = fd_ref (fd);
	stub->args.lk.cmd = cmd;
	stub->args.lk.lock = *lock;
out:
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

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_LK);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.lk_cbk.fn = fn;
	stub->args.lk_cbk.op_ret = op_ret;
	stub->args.lk_cbk.op_errno = op_errno;
	if (op_ret == 0)
		stub->args.lk_cbk.lock = *lock;
out:
	return stub;
}

call_stub_t *
fop_inodelk_stub (call_frame_t *frame, fop_inodelk_t fn,
		  const char *volume, loc_t *loc, int32_t cmd, struct flock *lock)
{
  call_stub_t *stub = NULL;

  if (!frame || !lock)
    return NULL;

  stub = stub_new (frame, 1, GF_FOP_INODELK);
  if (!stub)
    return NULL;

  stub->args.inodelk.fn = fn;

  if (volume)
          stub->args.inodelk.volume = strdup (volume);

  loc_copy (&stub->args.inodelk.loc, loc);
  stub->args.inodelk.cmd  = cmd;
  stub->args.inodelk.lock = *lock;

  return stub;
}

call_stub_t *
fop_inodelk_cbk_stub (call_frame_t *frame, fop_inodelk_cbk_t fn,
		      int32_t op_ret, int32_t op_errno)
{
  call_stub_t *stub = NULL;

  if (!frame)
    return NULL;

  stub = stub_new (frame, 0, GF_FOP_INODELK);
  if (!stub)
    return NULL;

  stub->args.inodelk_cbk.fn       = fn;
  stub->args.inodelk_cbk.op_ret   = op_ret;
  stub->args.inodelk_cbk.op_errno = op_errno;

  return stub;
}


call_stub_t *
fop_finodelk_stub (call_frame_t *frame, fop_finodelk_t fn,
		   const char *volume, fd_t *fd, int32_t cmd, struct flock *lock)
{
  call_stub_t *stub = NULL;

  if (!frame || !lock)
    return NULL;

  stub = stub_new (frame, 1, GF_FOP_FINODELK);
  if (!stub)
    return NULL;

  stub->args.finodelk.fn = fn;

  if (fd)
	  stub->args.finodelk.fd   = fd_ref (fd);

  if (volume)
          stub->args.finodelk.volume = strdup (volume);

  stub->args.finodelk.cmd  = cmd;
  stub->args.finodelk.lock = *lock;

  return stub;
}


call_stub_t *
fop_finodelk_cbk_stub (call_frame_t *frame, fop_inodelk_cbk_t fn,
		       int32_t op_ret, int32_t op_errno)
{
  call_stub_t *stub = NULL;

  if (!frame)
    return NULL;

  stub = stub_new (frame, 0, GF_FOP_FINODELK);
  if (!stub)
    return NULL;

  stub->args.finodelk_cbk.fn       = fn;
  stub->args.finodelk_cbk.op_ret   = op_ret;
  stub->args.finodelk_cbk.op_errno = op_errno;

  return stub;
}


call_stub_t *
fop_entrylk_stub (call_frame_t *frame, fop_entrylk_t fn,
		  const char *volume, loc_t *loc, const char *name,
		  entrylk_cmd cmd, entrylk_type type)
{
  call_stub_t *stub = NULL;

  if (!frame)
    return NULL;

  stub = stub_new (frame, 1, GF_FOP_ENTRYLK);
  if (!stub)
    return NULL;

  stub->args.entrylk.fn = fn;

  if (volume)
          stub->args.entrylk.volume = strdup (volume);

  loc_copy (&stub->args.entrylk.loc, loc);

  stub->args.entrylk.cmd = cmd;
  stub->args.entrylk.type = type;
  if (name)
	  stub->args.entrylk.name = strdup (name);

  return stub;
}

call_stub_t *
fop_entrylk_cbk_stub (call_frame_t *frame, fop_entrylk_cbk_t fn,
		      int32_t op_ret, int32_t op_errno)
{
  call_stub_t *stub = NULL;

  if (!frame)
    return NULL;

  stub = stub_new (frame, 0, GF_FOP_ENTRYLK);
  if (!stub)
    return NULL;

  stub->args.entrylk_cbk.fn       = fn;
  stub->args.entrylk_cbk.op_ret   = op_ret;
  stub->args.entrylk_cbk.op_errno = op_errno;

  return stub;
}


call_stub_t *
fop_fentrylk_stub (call_frame_t *frame, fop_fentrylk_t fn,
		   const char *volume, fd_t *fd, const char *name,
		   entrylk_cmd cmd, entrylk_type type)
{
  call_stub_t *stub = NULL;

  if (!frame)
    return NULL;

  stub = stub_new (frame, 1, GF_FOP_FENTRYLK);
  if (!stub)
    return NULL;

  stub->args.fentrylk.fn = fn;

  if (volume)
          stub->args.fentrylk.volume = strdup (volume);

  if (fd)
	  stub->args.fentrylk.fd = fd_ref (fd);
  stub->args.fentrylk.cmd = cmd;
  stub->args.fentrylk.type = type;
  if (name)
	  stub->args.fentrylk.name = strdup (name);

  return stub;
}

call_stub_t *
fop_fentrylk_cbk_stub (call_frame_t *frame, fop_fentrylk_cbk_t fn,
		       int32_t op_ret, int32_t op_errno)
{
  call_stub_t *stub = NULL;

  if (!frame)
    return NULL;

  stub = stub_new (frame, 0, GF_FOP_FENTRYLK);
  if (!stub)
    return NULL;

  stub->args.fentrylk_cbk.fn       = fn;
  stub->args.fentrylk_cbk.op_ret   = op_ret;
  stub->args.fentrylk_cbk.op_errno = op_errno;

  return stub;
}


call_stub_t *
fop_setdents_stub (call_frame_t *frame,
		   fop_setdents_t fn,
		   fd_t *fd,
		   int32_t flags,
		   dir_entry_t *entries,
		   int32_t count)
{ 
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 1, GF_FOP_SETDENTS);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	if (fd)
		stub->args.setdents.fd = fd_ref (fd);
	stub->args.setdents.fn = fn;
	stub->args.setdents.flags = flags;
	stub->args.setdents.count = count;
	if (entries) {
		stub->args.setdents.entries.next = entries->next;
		entries->next = NULL;
	}
out:
	return stub;
}

call_stub_t *
fop_setdents_cbk_stub (call_frame_t *frame,
		       fop_setdents_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno)
{  
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_SETDENTS);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.setdents_cbk.fn = fn;
	stub->args.setdents_cbk.op_ret = op_ret;
	stub->args.setdents_cbk.op_errno = op_errno;
out:
	return stub;
  
}

call_stub_t *
fop_readdirp_cbk_stub (call_frame_t *frame,
		       fop_readdirp_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       gf_dirent_t *entries)
{
	call_stub_t *stub = NULL;
	gf_dirent_t *stub_entry = NULL, *entry = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_READDIRP);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.readdirp_cbk.fn = fn;
	stub->args.readdirp_cbk.op_ret = op_ret;
	stub->args.readdirp_cbk.op_errno = op_errno;
	INIT_LIST_HEAD (&stub->args.readdirp_cbk.entries.list);

        /* This check must come after the init of head above
         * so we're sure the list is empty for list_empty.
         */
        if (!entries)
                goto out;

	if (op_ret > 0) {
		list_for_each_entry (entry, &entries->list, list) {
			stub_entry = gf_dirent_for_name (entry->d_name);
			ERR_ABORT (stub_entry);
			stub_entry->d_off = entry->d_off;
			stub_entry->d_ino = entry->d_ino;

			list_add_tail (&stub_entry->list,
				       &stub->args.readdirp_cbk.entries.list);
		}
	}
out:
	return stub;
}


call_stub_t *
fop_readdir_cbk_stub (call_frame_t *frame,
		      fop_readdir_cbk_t fn,
		      int32_t op_ret,
		      int32_t op_errno,
		      gf_dirent_t *entries)
{
	call_stub_t *stub = NULL;
	gf_dirent_t *stub_entry = NULL, *entry = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_READDIR);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);
	
	stub->args.readdir_cbk.fn = fn;
	stub->args.readdir_cbk.op_ret = op_ret;
	stub->args.readdir_cbk.op_errno = op_errno;
	INIT_LIST_HEAD (&stub->args.readdir_cbk.entries.list);

        /* This check must come after the init of head above
         * so we're sure the list is empty for list_empty.
         */
        if (!entries)
                goto out;

	if (op_ret > 0) {
		list_for_each_entry (entry, &entries->list, list) {
			stub_entry = gf_dirent_for_name (entry->d_name);
			ERR_ABORT (stub_entry);
			stub_entry->d_off = entry->d_off;
			stub_entry->d_ino = entry->d_ino;

			list_add_tail (&stub_entry->list, 
				       &stub->args.readdir_cbk.entries.list);
		}
	}
out:
	return stub;
}

call_stub_t *
fop_readdir_stub (call_frame_t *frame,
		  fop_readdir_t fn,
		  fd_t *fd,
		  size_t size,
		  off_t off)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_READDIR);
  stub->args.readdir.fn = fn;
  stub->args.readdir.fd = fd_ref (fd);
  stub->args.readdir.size = size;
  stub->args.readdir.off = off;

  return stub;
}

call_stub_t *
fop_readdirp_stub (call_frame_t *frame,
		   fop_readdirp_t fn,
		   fd_t *fd,
		   size_t size,
		   off_t off)
{
  call_stub_t *stub = NULL;

  stub = stub_new (frame, 1, GF_FOP_READDIRP);
  stub->args.readdirp.fn = fn;
  stub->args.readdirp.fd = fd_ref (fd);
  stub->args.readdirp.size = size;
  stub->args.readdirp.off = off;

  return stub;
}

call_stub_t *
fop_checksum_stub (call_frame_t *frame,
		   fop_checksum_t fn,
		   loc_t *loc,
		   int32_t flags)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

	stub = stub_new (frame, 1, GF_FOP_CHECKSUM);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.checksum.fn = fn;
	loc_copy (&stub->args.checksum.loc, loc);
	stub->args.checksum.flags = flags;
out:
	return stub;
}


call_stub_t *
fop_checksum_cbk_stub (call_frame_t *frame,
		       fop_checksum_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       uint8_t *file_checksum,
		       uint8_t *dir_checksum)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_CHECKSUM);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.checksum_cbk.fn = fn;
	stub->args.checksum_cbk.op_ret = op_ret;
	stub->args.checksum_cbk.op_errno = op_errno;
	if (op_ret >= 0)
	{
		stub->args.checksum_cbk.file_checksum = 
			memdup (file_checksum, NAME_MAX);

		stub->args.checksum_cbk.dir_checksum = 
			memdup (dir_checksum, NAME_MAX);
	}
out:
	return stub;
}


call_stub_t *
fop_rchecksum_stub (call_frame_t *frame,
                    fop_rchecksum_t fn,
                    fd_t *fd, off_t offset,
                    int32_t len)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	GF_VALIDATE_OR_GOTO ("call-stub", fd, out);

	stub = stub_new (frame, 1, GF_FOP_RCHECKSUM);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.rchecksum.fn = fn;
        stub->args.rchecksum.fd = fd_ref (fd);
	stub->args.rchecksum.offset = offset;
	stub->args.rchecksum.len    = len;
out:
	return stub;
}


call_stub_t *
fop_rchecksum_cbk_stub (call_frame_t *frame,
                        fop_rchecksum_cbk_t fn,
                        int32_t op_ret,
                        int32_t op_errno,
                        uint32_t weak_checksum,
                        uint8_t *strong_checksum)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 0, GF_FOP_RCHECKSUM);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.rchecksum_cbk.fn = fn;
	stub->args.rchecksum_cbk.op_ret = op_ret;
	stub->args.rchecksum_cbk.op_errno = op_errno;

	if (op_ret >= 0)
	{
		stub->args.rchecksum_cbk.weak_checksum =
                        weak_checksum;

		stub->args.rchecksum_cbk.strong_checksum = 
			memdup (strong_checksum, MD5_DIGEST_LEN);
	}
out:
	return stub;
}


call_stub_t *
fop_xattrop_cbk_stub (call_frame_t *frame,
		      fop_xattrop_cbk_t fn,
		      int32_t op_ret,
		      int32_t op_errno)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	
	stub = stub_new (frame, 0, GF_FOP_XATTROP);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.xattrop_cbk.fn       = fn;
	stub->args.xattrop_cbk.op_ret   = op_ret;
	stub->args.xattrop_cbk.op_errno = op_errno;

out:
	return stub;
}


call_stub_t *
fop_fxattrop_cbk_stub (call_frame_t *frame,
		       fop_fxattrop_cbk_t fn,
		       int32_t op_ret,
		       int32_t op_errno,
		       dict_t *xattr)
{
	call_stub_t *stub = NULL;
	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

	stub = stub_new (frame, 1, GF_FOP_FXATTROP);
	stub->args.fxattrop_cbk.fn = fn;
	stub->args.fxattrop_cbk.op_ret = op_ret;
	stub->args.fxattrop_cbk.op_errno = op_errno;
	if (xattr) 
		stub->args.fxattrop_cbk.xattr = dict_ref (xattr);

out:
	return stub;
}


call_stub_t *
fop_xattrop_stub (call_frame_t *frame,
		  fop_xattrop_t fn,
		  loc_t *loc,
		  gf_xattrop_flags_t optype,
		  dict_t *xattr)
{
	call_stub_t *stub = NULL;

	if (!frame || !xattr)
		return NULL;

	stub = stub_new (frame, 1, GF_FOP_XATTROP);
	if (!stub)
		return NULL;

	stub->args.xattrop.fn = fn;
	
	loc_copy (&stub->args.xattrop.loc, loc);

	stub->args.xattrop.optype = optype;
	stub->args.xattrop.xattr = dict_ref (xattr);

	return stub;
}

call_stub_t *
fop_fxattrop_stub (call_frame_t *frame,
		   fop_fxattrop_t fn,
		   fd_t *fd,
		   gf_xattrop_flags_t optype,
		   dict_t *xattr)
{
	call_stub_t *stub = NULL;

	if (!frame || !xattr)
		return NULL;

	stub = stub_new (frame, 1, GF_FOP_FXATTROP);
	if (!stub)
		return NULL;

	stub->args.fxattrop.fn = fn;
	
	stub->args.fxattrop.fd = fd_ref (fd);

	stub->args.fxattrop.optype = optype;
	stub->args.fxattrop.xattr = dict_ref (xattr);

	return stub;
}


call_stub_t *
fop_lock_notify_cbk_stub (call_frame_t *frame, fop_lock_notify_cbk_t fn,
                          int32_t op_ret, int32_t op_errno)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	
	stub = stub_new (frame, 0, GF_FOP_LOCK_NOTIFY);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.lock_notify_cbk.fn       = fn;
	stub->args.lock_notify_cbk.op_ret   = op_ret;
	stub->args.lock_notify_cbk.op_errno = op_errno;

out:
	return stub;
}


call_stub_t *
fop_lock_notify_stub (call_frame_t *frame, fop_lock_notify_t fn,
		      loc_t *loc, int32_t timeout)
{
	call_stub_t *stub = NULL;

	if (!frame)
		return NULL;

	stub = stub_new (frame, 1, GF_FOP_LOCK_NOTIFY);
	if (!stub)
		return NULL;

	stub->args.lock_notify.fn = fn;
	
	loc_copy (&stub->args.lock_notify.loc, loc);

	stub->args.lock_notify.timeout = timeout;

	return stub;
}


call_stub_t *
fop_lock_fnotify_cbk_stub (call_frame_t *frame, fop_lock_fnotify_cbk_t fn,
                           int32_t op_ret, int32_t op_errno)
{
	call_stub_t *stub = NULL;

	GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
	
	stub = stub_new (frame, 0, GF_FOP_LOCK_FNOTIFY);
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	stub->args.lock_fnotify_cbk.fn       = fn;
	stub->args.lock_fnotify_cbk.op_ret   = op_ret;
	stub->args.lock_fnotify_cbk.op_errno = op_errno;

out:
	return stub;
}


call_stub_t *
fop_lock_fnotify_stub (call_frame_t *frame, fop_lock_fnotify_t fn,
		       fd_t *fd, int32_t timeout)
{
	call_stub_t *stub = NULL;

	if (!frame)
		return NULL;

	stub = stub_new (frame, 1, GF_FOP_LOCK_FNOTIFY);
	if (!stub)
		return NULL;

	stub->args.lock_fnotify.fn = fn;

	stub->args.lock_fnotify.fd      = fd_ref (fd);
	stub->args.lock_fnotify.timeout = timeout;

	return stub;
}

call_stub_t *
fop_setattr_cbk_stub (call_frame_t *frame,
                      fop_setattr_cbk_t fn,
                      int32_t op_ret,
                      int32_t op_errno,
                      struct stat *statpre,
                      struct stat *statpost)
{
        call_stub_t *stub = NULL;

        if (frame == NULL)
                goto out;

	stub = stub_new (frame, 1, GF_FOP_SETATTR);
	if (stub == NULL)
                goto out;

	stub->args.setattr_cbk.fn = fn;

        stub->args.setattr_cbk.op_ret = op_ret;
        stub->args.setattr_cbk.op_errno = op_errno;

        if (statpre)
                stub->args.setattr_cbk.statpre = *statpre;
        if (statpost)
                stub->args.setattr_cbk.statpost = *statpost;

out:
	return stub;
}

call_stub_t *
fop_fsetattr_cbk_stub (call_frame_t *frame,
                       fop_setattr_cbk_t fn,
                       int32_t op_ret,
                       int32_t op_errno,
                       struct stat *statpre,
                       struct stat *statpost)
{
        call_stub_t *stub = NULL;

        if (frame == NULL)
                goto out;

	stub = stub_new (frame, 1, GF_FOP_FSETATTR);
	if (stub == NULL)
                goto out;

	stub->args.fsetattr_cbk.fn = fn;

        stub->args.fsetattr_cbk.op_ret = op_ret;
        stub->args.fsetattr_cbk.op_errno = op_errno;

        if (statpre)
                stub->args.setattr_cbk.statpre = *statpre;
        if (statpost)
                stub->args.fsetattr_cbk.statpost = *statpost;
out:
	return stub;
}

call_stub_t *
fop_setattr_stub (call_frame_t *frame,
                  fop_setattr_t fn,
                  loc_t *loc,
                  struct stat *stbuf,
                  int32_t valid)
{
        call_stub_t *stub = NULL;

        if (frame == NULL)
                goto out;

        if (fn == NULL)
                goto out;

	stub = stub_new (frame, 1, GF_FOP_SETATTR);
	if (stub == NULL)
                goto out;

	stub->args.setattr.fn = fn;

	loc_copy (&stub->args.setattr.loc, loc);

        if (stbuf)
                stub->args.setattr.stbuf = *stbuf;

        stub->args.setattr.valid = valid;

out:
	return stub;
}

call_stub_t *
fop_fsetattr_stub (call_frame_t *frame,
                   fop_fsetattr_t fn,
                   fd_t *fd,
                   struct stat *stbuf,
                   int32_t valid)
{
        call_stub_t *stub = NULL;

        if (frame == NULL)
                goto out;

        if (fn == NULL)
                goto out;

	stub = stub_new (frame, 1, GF_FOP_FSETATTR);
	if (stub == NULL)
                goto out;

	stub->args.fsetattr.fn = fn;

        if (fd)
                stub->args.fsetattr.fd = fd_ref (fd);

        if (stbuf)
                stub->args.fsetattr.stbuf = *stbuf;

        stub->args.fsetattr.valid = valid;

out:
	return stub;
}

static void
call_resume_wind (call_stub_t *stub)
{
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	switch (stub->fop) {
	case GF_FOP_OPEN:
	{
		stub->args.open.fn (stub->frame, 
				    stub->frame->this,
				    &stub->args.open.loc, 
				    stub->args.open.flags, stub->args.open.fd,
                                    stub->args.open.wbflags);
		break;
	}
	case GF_FOP_CREATE:
	{
		stub->args.create.fn (stub->frame,
				      stub->frame->this,
				      &stub->args.create.loc,
				      stub->args.create.flags,
				      stub->args.create.mode,
				      stub->args.create.fd);
		break;
	}
	case GF_FOP_STAT:
	{
		stub->args.stat.fn (stub->frame,
				    stub->frame->this,
				    &stub->args.stat.loc);
		break;
	}
	case GF_FOP_READLINK:
	{
		stub->args.readlink.fn (stub->frame,
					stub->frame->this,
					&stub->args.readlink.loc,
					stub->args.readlink.size);
		break;
	}
  
	case GF_FOP_MKNOD:
	{
		stub->args.mknod.fn (stub->frame,
				     stub->frame->this,
				     &stub->args.mknod.loc,
				     stub->args.mknod.mode,
				     stub->args.mknod.rdev);
	}
	break;
  
	case GF_FOP_MKDIR:
	{
		stub->args.mkdir.fn (stub->frame,
				     stub->frame->this,
				     &stub->args.mkdir.loc,
				     stub->args.mkdir.mode);
	}
	break;
  
	case GF_FOP_UNLINK:
	{
		stub->args.unlink.fn (stub->frame,
				      stub->frame->this,
				      &stub->args.unlink.loc);
	}
	break;

	case GF_FOP_RMDIR:
	{
		stub->args.rmdir.fn (stub->frame,
				     stub->frame->this,
				     &stub->args.rmdir.loc);
	}
	break;
      
	case GF_FOP_SYMLINK:
	{
		stub->args.symlink.fn (stub->frame,
				       stub->frame->this,
				       stub->args.symlink.linkname,
				       &stub->args.symlink.loc);
	}
	break;
  
	case GF_FOP_RENAME:
	{
		stub->args.rename.fn (stub->frame,
				      stub->frame->this,
				      &stub->args.rename.old,
				      &stub->args.rename.new);
	}
	break;

	case GF_FOP_LINK:
	{
		stub->args.link.fn (stub->frame,
				    stub->frame->this,
				    &stub->args.link.oldloc,
				    &stub->args.link.newloc);
	}
	break;
  
	case GF_FOP_TRUNCATE:
	{
		stub->args.truncate.fn (stub->frame,
					stub->frame->this,
					&stub->args.truncate.loc,
					stub->args.truncate.off);
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
		stub->args.writev.fn (stub->frame,
				      stub->frame->this,
				      stub->args.writev.fd,
				      stub->args.writev.vector,
				      stub->args.writev.count,
				      stub->args.writev.off,
                                      stub->args.writev.iobref);
		break;
	}
  
	case GF_FOP_STATFS:
	{
		stub->args.statfs.fn (stub->frame,
				      stub->frame->this,
				      &stub->args.statfs.loc);
		break;
	}
	case GF_FOP_FLUSH:
	{
		stub->args.flush.fn (stub->frame,
				     stub->frame->this,
				     stub->args.flush.fd);
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
		break;
	}
  
	case GF_FOP_GETXATTR:
	{
		stub->args.getxattr.fn (stub->frame,
					stub->frame->this,
					&stub->args.getxattr.loc,
					stub->args.getxattr.name);
		break;
	}

	case GF_FOP_FSETXATTR:
	{
		stub->args.fsetxattr.fn (stub->frame,
                                         stub->frame->this,
                                         stub->args.fsetxattr.fd,
                                         stub->args.fsetxattr.dict,
                                         stub->args.fsetxattr.flags);
		break;
	}

	case GF_FOP_FGETXATTR:
	{
		stub->args.fgetxattr.fn (stub->frame,
                                         stub->frame->this,
                                         stub->args.fgetxattr.fd,
                                         stub->args.fgetxattr.name);
		break;
	}

	case GF_FOP_REMOVEXATTR:
	{
		stub->args.removexattr.fn (stub->frame,
					   stub->frame->this,
					   &stub->args.removexattr.loc,
					   stub->args.removexattr.name);
		break;
	}
  
	case GF_FOP_OPENDIR:
	{
		stub->args.opendir.fn (stub->frame,
				       stub->frame->this,
				       &stub->args.opendir.loc,
				       stub->args.opendir.fd);
		break;
	}

	case GF_FOP_GETDENTS:
	{
		stub->args.getdents.fn (stub->frame,
					stub->frame->this,
					stub->args.getdents.fd,
					stub->args.getdents.size,
					stub->args.getdents.off,
					stub->args.getdents.flag);
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

	case GF_FOP_INODELK:
	{
		stub->args.inodelk.fn (stub->frame,
				       stub->frame->this,
                                       stub->args.inodelk.volume,
				       &stub->args.inodelk.loc,
				       stub->args.inodelk.cmd,
				       &stub->args.inodelk.lock);
		break;
	}

	case GF_FOP_FINODELK:
	{
		stub->args.finodelk.fn (stub->frame,
					stub->frame->this,
                                        stub->args.finodelk.volume,
					stub->args.finodelk.fd,
					stub->args.finodelk.cmd,
					&stub->args.finodelk.lock);
		break;
	}

	case GF_FOP_ENTRYLK:
	{
		stub->args.entrylk.fn (stub->frame,
				       stub->frame->this,
                                       stub->args.entrylk.volume,
				       &stub->args.entrylk.loc,
				       stub->args.entrylk.name,
				       stub->args.entrylk.cmd,
				       stub->args.entrylk.type);
		break;
	}

	case GF_FOP_FENTRYLK:
	{
		stub->args.fentrylk.fn (stub->frame,
					stub->frame->this,
                                        stub->args.fentrylk.volume,
					stub->args.fentrylk.fd,
					stub->args.fentrylk.name,
					stub->args.fentrylk.cmd,
					stub->args.fentrylk.type);
		break;
	}
  
	break;
  
	case GF_FOP_LOOKUP:
	{
		stub->args.lookup.fn (stub->frame, 
				      stub->frame->this,
				      &stub->args.lookup.loc,
				      stub->args.lookup.xattr_req);
		break;
	}

	case GF_FOP_SETDENTS:
	{
		stub->args.setdents.fn (stub->frame,
					stub->frame->this,
					stub->args.setdents.fd,
					stub->args.setdents.flags,
					&stub->args.setdents.entries,
					stub->args.setdents.count);
		break;
	}

	case GF_FOP_CHECKSUM:
	{
		stub->args.checksum.fn (stub->frame,
					stub->frame->this,
					&stub->args.checksum.loc,
					stub->args.checksum.flags);
		break;
	}

	case GF_FOP_RCHECKSUM:
	{
		stub->args.rchecksum.fn (stub->frame,
                                         stub->frame->this,
                                         stub->args.rchecksum.fd,
                                         stub->args.rchecksum.offset,
                                         stub->args.rchecksum.len);
		break;
	}

	case GF_FOP_READDIR:
	{
		stub->args.readdir.fn (stub->frame,
				       stub->frame->this,
				       stub->args.readdir.fd,
				       stub->args.readdir.size,
				       stub->args.readdir.off);
		break;
	}

        case GF_FOP_READDIRP:
	{
		stub->args.readdirp.fn (stub->frame,
				        stub->frame->this,
				        stub->args.readdirp.fd,
				        stub->args.readdirp.size,
				        stub->args.readdirp.off);
		break;
	}

	case GF_FOP_XATTROP:
	{
		stub->args.xattrop.fn (stub->frame,
				       stub->frame->this,
				       &stub->args.xattrop.loc,
				       stub->args.xattrop.optype,
				       stub->args.xattrop.xattr);

		break;
	}
	case GF_FOP_FXATTROP:
	{
		stub->args.fxattrop.fn (stub->frame,
					stub->frame->this,
					stub->args.fxattrop.fd,
					stub->args.fxattrop.optype,
					stub->args.fxattrop.xattr);

		break;
	}
	case GF_FOP_LOCK_NOTIFY:
	{
		stub->args.lock_notify.fn (stub->frame,
					   stub->frame->this,
					   &stub->args.lock_notify.loc,
					   stub->args.lock_notify.timeout);
		break;
	}
	case GF_FOP_LOCK_FNOTIFY:
	{
		stub->args.lock_fnotify.fn (stub->frame,
					    stub->frame->this,
					    stub->args.lock_fnotify.fd,
					    stub->args.lock_fnotify.timeout);
		break;
	}
        case GF_FOP_SETATTR:
        {
                stub->args.setattr.fn (stub->frame,
                                       stub->frame->this,
                                       &stub->args.setattr.loc,
                                       &stub->args.setattr.stbuf,
                                       stub->args.setattr.valid);
                break;
        }
        case GF_FOP_FSETATTR:
        {
                stub->args.fsetattr.fn (stub->frame,
                                        stub->frame->this,
                                        stub->args.fsetattr.fd,
                                        &stub->args.fsetattr.stbuf,
                                        stub->args.fsetattr.valid);
                break;
        }
	default:
	{
		gf_log ("call-stub",
			GF_LOG_DEBUG,
			"Invalid value of FOP");
	}
	break;
	}
out:
	return;
}



static void
call_resume_unwind (call_stub_t *stub)
{
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

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
				      &stub->args.create_cbk.buf,
                                      &stub->args.create_cbk.preparent,
                                      &stub->args.create_cbk.postparent);
		else
			stub->args.create_cbk.fn (stub->frame,
						  stub->frame->cookie,
						  stub->frame->this,
						  stub->args.create_cbk.op_ret,
						  stub->args.create_cbk.op_errno,
						  stub->args.create_cbk.fd,
						  stub->args.create_cbk.inode,
						  &stub->args.create_cbk.buf,
                                                  &stub->args.create_cbk.preparent,
                                                  &stub->args.create_cbk.postparent);
      
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
				      stub->args.readlink_cbk.buf,
                                      &stub->args.readlink_cbk.sbuf);
		else
			stub->args.readlink_cbk.fn (stub->frame,
						    stub->frame->cookie,
						    stub->frame->this,
						    stub->args.readlink_cbk.op_ret,
						    stub->args.readlink_cbk.op_errno,
						    stub->args.readlink_cbk.buf,
                                                    &stub->args.readlink_cbk.sbuf);

		break;
	}
  
	case GF_FOP_MKNOD:
	{
		if (!stub->args.mknod_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.mknod_cbk.op_ret,
				      stub->args.mknod_cbk.op_errno,
				      stub->args.mknod_cbk.inode,
                                      &stub->args.mknod_cbk.buf,
                                      &stub->args.mknod_cbk.preparent,
                                      &stub->args.mknod_cbk.postparent);
		else
			stub->args.mknod_cbk.fn (stub->frame,
						 stub->frame->cookie,
						 stub->frame->this,
						 stub->args.mknod_cbk.op_ret,
						 stub->args.mknod_cbk.op_errno,
						 stub->args.mknod_cbk.inode,
                                                 &stub->args.mknod_cbk.buf,
                                                 &stub->args.mknod_cbk.preparent,
                                                 &stub->args.mknod_cbk.postparent);
		break;
	}

	case GF_FOP_MKDIR:
	{
		if (!stub->args.mkdir_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.mkdir_cbk.op_ret,
				      stub->args.mkdir_cbk.op_errno,
				      stub->args.mkdir_cbk.inode,
                                      &stub->args.mkdir_cbk.buf,
                                      &stub->args.mkdir_cbk.preparent,
                                      &stub->args.mkdir_cbk.postparent);
		else
			stub->args.mkdir_cbk.fn (stub->frame,
						 stub->frame->cookie,
						 stub->frame->this,
						 stub->args.mkdir_cbk.op_ret,
						 stub->args.mkdir_cbk.op_errno,
						 stub->args.mkdir_cbk.inode,
                                                 &stub->args.mkdir_cbk.buf,
                                                 &stub->args.mkdir_cbk.preparent,
                                                 &stub->args.mkdir_cbk.postparent);

		if (stub->args.mkdir_cbk.inode)
			inode_unref (stub->args.mkdir_cbk.inode);

		break;
	}
  
	case GF_FOP_UNLINK:
	{
		if (!stub->args.unlink_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.unlink_cbk.op_ret,
				      stub->args.unlink_cbk.op_errno,
                                      &stub->args.unlink_cbk.preparent,
                                      &stub->args.unlink_cbk.postparent);
		else
			stub->args.unlink_cbk.fn (stub->frame,
						  stub->frame->cookie,
						  stub->frame->this,
						  stub->args.unlink_cbk.op_ret,
						  stub->args.unlink_cbk.op_errno,
                                                  &stub->args.unlink_cbk.preparent,
                                                  &stub->args.unlink_cbk.postparent);
		break;
	}
  
	case GF_FOP_RMDIR:
	{
		if (!stub->args.rmdir_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.rmdir_cbk.op_ret,
				      stub->args.rmdir_cbk.op_errno,
                                      &stub->args.rmdir_cbk.preparent,
                                      &stub->args.rmdir_cbk.postparent);
		else
			stub->args.unlink_cbk.fn (stub->frame,
						  stub->frame->cookie,
						  stub->frame->this,
						  stub->args.rmdir_cbk.op_ret,
						  stub->args.rmdir_cbk.op_errno,
                                                  &stub->args.rmdir_cbk.preparent,
                                                  &stub->args.rmdir_cbk.postparent);
		break;
	}

	case GF_FOP_SYMLINK:
	{
		if (!stub->args.symlink_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.symlink_cbk.op_ret,
				      stub->args.symlink_cbk.op_errno,
				      stub->args.symlink_cbk.inode,
                                      &stub->args.symlink_cbk.buf,
                                      &stub->args.symlink_cbk.preparent,
                                      &stub->args.symlink_cbk.postparent);
		else
			stub->args.symlink_cbk.fn (stub->frame,
						   stub->frame->cookie,
						   stub->frame->this,
						   stub->args.symlink_cbk.op_ret,
						   stub->args.symlink_cbk.op_errno,
						   stub->args.symlink_cbk.inode,
                                                   &stub->args.symlink_cbk.buf,
                                                   &stub->args.symlink_cbk.preparent,
                                                   &stub->args.symlink_cbk.postparent);
	}
	break;
  
	case GF_FOP_RENAME:
	{
#if 0
		if (!stub->args.rename_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.rename_cbk.op_ret,
				      stub->args.rename_cbk.op_errno,
				      &stub->args.rename_cbk.buf,
                                      &stub->args.rename_cbk.preoldparent,
                                      &stub->args.rename_cbk.postoldparent,
                                      &stub->args.rename_cbk.prenewparent,
                                      &stub->args.rename_cbk.postnewparent);
		else
			stub->args.rename_cbk.fn (stub->frame,
						  stub->frame->cookie,
						  stub->frame->this,
						  stub->args.rename_cbk.op_ret,
						  stub->args.rename_cbk.op_errno,
						  &stub->args.rename_cbk.buf,
                                                  &stub->args.rename_cbk.preoldparent,
                                                  &stub->args.rename_cbk.postoldparent,
                                                  &stub->args.rename_cbk.prenewparent,
                                                  &stub->args.rename_cbk.postnewparent);
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
                                                &stub->args.link_cbk.buf,
                                                &stub->args.link_cbk.preparent,
                                                &stub->args.link_cbk.postparent);
		break;
	}
  
	case GF_FOP_TRUNCATE:
	{
		if (!stub->args.truncate_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.truncate_cbk.op_ret,
				      stub->args.truncate_cbk.op_errno,
				      &stub->args.truncate_cbk.prebuf,
                                      &stub->args.truncate_cbk.postbuf);
		else
			stub->args.truncate_cbk.fn (stub->frame,
						    stub->frame->cookie,
						    stub->frame->this,
						    stub->args.truncate_cbk.op_ret,
						    stub->args.truncate_cbk.op_errno,
						    &stub->args.truncate_cbk.prebuf,
                                                    &stub->args.truncate_cbk.postbuf);
		break;
	}
      
	case GF_FOP_READ:
	{
		if (!stub->args.readv_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.readv_cbk.op_ret,
				      stub->args.readv_cbk.op_errno,
				      stub->args.readv_cbk.vector,
				      stub->args.readv_cbk.count,
				      &stub->args.readv_cbk.stbuf,
                                      stub->args.readv_cbk.iobref);
		else
			stub->args.readv_cbk.fn (stub->frame,
						 stub->frame->cookie,
						 stub->frame->this,
						 stub->args.readv_cbk.op_ret,
						 stub->args.readv_cbk.op_errno,
						 stub->args.readv_cbk.vector,
						 stub->args.readv_cbk.count,
						 &stub->args.readv_cbk.stbuf,
                                                 stub->args.readv_cbk.iobref);
	}
	break;
  
	case GF_FOP_WRITE:
	{
		if (!stub->args.writev_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.writev_cbk.op_ret,
				      stub->args.writev_cbk.op_errno,
                                      &stub->args.writev_cbk.prebuf,
				      &stub->args.writev_cbk.postbuf);
		else
			stub->args.writev_cbk.fn (stub->frame,
						  stub->frame->cookie,
						  stub->frame->this,
						  stub->args.writev_cbk.op_ret,
						  stub->args.writev_cbk.op_errno,
                                                  &stub->args.writev_cbk.prebuf,
						  &stub->args.writev_cbk.postbuf);
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
  
	case GF_FOP_FSYNC:
	{
		if (!stub->args.fsync_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.fsync_cbk.op_ret,
				      stub->args.fsync_cbk.op_errno,
                                      &stub->args.fsync_cbk.prebuf,
                                      &stub->args.fsync_cbk.postbuf);
		else
			stub->args.fsync_cbk.fn (stub->frame,
						 stub->frame->cookie,
						 stub->frame->this,
						 stub->args.fsync_cbk.op_ret,
						 stub->args.fsync_cbk.op_errno,
                                                 &stub->args.fsync_cbk.prebuf,
                                                 &stub->args.fsync_cbk.postbuf);
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
		break;
	}

	case GF_FOP_FSETXATTR:
	{
		if (!stub->args.fsetxattr_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.fsetxattr_cbk.op_ret,
				      stub->args.fsetxattr_cbk.op_errno);

		else
			stub->args.fsetxattr_cbk.fn (stub->frame,
                                                     stub->frame->cookie,
                                                     stub->frame->this,
                                                     stub->args.fsetxattr_cbk.op_ret,
                                                     stub->args.fsetxattr_cbk.op_errno);

		break;
	}

	case GF_FOP_FGETXATTR:
	{
		if (!stub->args.fgetxattr_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.fgetxattr_cbk.op_ret,
				      stub->args.fgetxattr_cbk.op_errno,
				      stub->args.fgetxattr_cbk.dict);
		else
			stub->args.fgetxattr_cbk.fn (stub->frame,
                                                     stub->frame->cookie,
                                                     stub->frame->this,
                                                     stub->args.fgetxattr_cbk.op_ret,
                                                     stub->args.fgetxattr_cbk.op_errno,
                                                     stub->args.fgetxattr_cbk.dict);
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
  
	case GF_FOP_GETDENTS:
	{
		if (!stub->args.getdents_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.getdents_cbk.op_ret,
				      stub->args.getdents_cbk.op_errno,
				      &stub->args.getdents_cbk.entries,
				      stub->args.getdents_cbk.count);
		else
			stub->args.getdents_cbk.fn (stub->frame,
						    stub->frame->cookie,
						    stub->frame->this,
						    stub->args.getdents_cbk.op_ret,
						    stub->args.getdents_cbk.op_errno,
						    &stub->args.getdents_cbk.entries,
						    stub->args.getdents_cbk.count);
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
				      &stub->args.ftruncate_cbk.prebuf,
				      &stub->args.ftruncate_cbk.postbuf);
		else
			stub->args.ftruncate_cbk.fn (stub->frame,
						     stub->frame->cookie,
						     stub->frame->this,
						     stub->args.ftruncate_cbk.op_ret,
						     stub->args.ftruncate_cbk.op_errno,
						     &stub->args.ftruncate_cbk.prebuf,
						     &stub->args.ftruncate_cbk.postbuf);
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

	case GF_FOP_INODELK:
	{
		if (!stub->args.inodelk_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.inodelk_cbk.op_ret,
				      stub->args.inodelk_cbk.op_errno);

		else
			stub->args.inodelk_cbk.fn (stub->frame,
						   stub->frame->cookie,
						   stub->frame->this,
						   stub->args.inodelk_cbk.op_ret,
						   stub->args.inodelk_cbk.op_errno);
		break;
	}

	case GF_FOP_FINODELK:
	{
		if (!stub->args.finodelk_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.finodelk_cbk.op_ret,
				      stub->args.finodelk_cbk.op_errno);

		else
			stub->args.finodelk_cbk.fn (stub->frame,
						    stub->frame->cookie,
						    stub->frame->this,
						    stub->args.finodelk_cbk.op_ret,
						    stub->args.finodelk_cbk.op_errno);
		break;
	}

	case GF_FOP_ENTRYLK:
	{
		if (!stub->args.entrylk_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.entrylk_cbk.op_ret,
				      stub->args.entrylk_cbk.op_errno);

		else
			stub->args.entrylk_cbk.fn (stub->frame,
						   stub->frame->cookie,
						   stub->frame->this,
						   stub->args.entrylk_cbk.op_ret,
						   stub->args.entrylk_cbk.op_errno);
		break;
	}

	case GF_FOP_FENTRYLK:
	{
		if (!stub->args.fentrylk_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.fentrylk_cbk.op_ret,
				      stub->args.fentrylk_cbk.op_errno);

		else
			stub->args.fentrylk_cbk.fn (stub->frame,
						    stub->frame->cookie,
						    stub->frame->this,
						    stub->args.fentrylk_cbk.op_ret,
						    stub->args.fentrylk_cbk.op_errno);
		break;
	}
  
	case GF_FOP_LOOKUP:
	{
		if (!stub->args.lookup_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.lookup_cbk.op_ret,
				      stub->args.lookup_cbk.op_errno,
				      stub->args.lookup_cbk.inode,
				      &stub->args.lookup_cbk.buf,
                                      stub->args.lookup_cbk.dict,
                                      &stub->args.lookup_cbk.postparent);
		else
			stub->args.lookup_cbk.fn (stub->frame, 
						  stub->frame->cookie,
						  stub->frame->this,
						  stub->args.lookup_cbk.op_ret,
						  stub->args.lookup_cbk.op_errno,
						  stub->args.lookup_cbk.inode,
                                                  &stub->args.lookup_cbk.buf,
                                                  stub->args.lookup_cbk.dict,
                                                  &stub->args.lookup_cbk.postparent);
		/* FIXME NULL should not be passed */

		if (stub->args.lookup_cbk.dict)
			dict_unref (stub->args.lookup_cbk.dict);
		if (stub->args.lookup_cbk.inode)
			inode_unref (stub->args.lookup_cbk.inode);

		break;
	}
	case GF_FOP_SETDENTS:
	{
		if (!stub->args.setdents_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.setdents_cbk.op_ret,
				      stub->args.setdents_cbk.op_errno);
		else
			stub->args.setdents_cbk.fn (stub->frame,
						    stub->frame->cookie,
						    stub->frame->this,
						    stub->args.setdents_cbk.op_ret,
						    stub->args.setdents_cbk.op_errno);
		break;
	}

	case GF_FOP_CHECKSUM:
	{
		if (!stub->args.checksum_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.checksum_cbk.op_ret,
				      stub->args.checksum_cbk.op_errno,
				      stub->args.checksum_cbk.file_checksum,
				      stub->args.checksum_cbk.dir_checksum);
		else
			stub->args.checksum_cbk.fn (stub->frame, 
						    stub->frame->cookie,
						    stub->frame->this,
						    stub->args.checksum_cbk.op_ret, 
						    stub->args.checksum_cbk.op_errno,
						    stub->args.checksum_cbk.file_checksum,
						    stub->args.checksum_cbk.dir_checksum);
		if (stub->args.checksum_cbk.op_ret >= 0)
		{
			FREE (stub->args.checksum_cbk.file_checksum);
			FREE (stub->args.checksum_cbk.dir_checksum);
		}

		break;
	}

	case GF_FOP_RCHECKSUM:
	{
		if (!stub->args.rchecksum_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.rchecksum_cbk.op_ret,
				      stub->args.rchecksum_cbk.op_errno,
				      stub->args.rchecksum_cbk.weak_checksum,
				      stub->args.rchecksum_cbk.strong_checksum);
		else
			stub->args.rchecksum_cbk.fn (stub->frame, 
                                                     stub->frame->cookie,
                                                     stub->frame->this,
                                                     stub->args.rchecksum_cbk.op_ret, 
                                                     stub->args.rchecksum_cbk.op_errno,
                                                     stub->args.rchecksum_cbk.weak_checksum,
                                                     stub->args.rchecksum_cbk.strong_checksum);
		if (stub->args.rchecksum_cbk.op_ret >= 0)
		{
			FREE (stub->args.rchecksum_cbk.strong_checksum);
		}

		break;
	}

	case GF_FOP_READDIR:
	{
		if (!stub->args.readdir_cbk.fn) 
			STACK_UNWIND (stub->frame,
				      stub->args.readdir_cbk.op_ret,
				      stub->args.readdir_cbk.op_errno,
				      &stub->args.readdir_cbk.entries);
		else 
			stub->args.readdir_cbk.fn (stub->frame,
						   stub->frame->cookie,
						   stub->frame->this,
						   stub->args.readdir_cbk.op_ret,
						   stub->args.readdir_cbk.op_errno,
						   &stub->args.readdir_cbk.entries);
		
		if (stub->args.readdir_cbk.op_ret > 0) 
			gf_dirent_free (&stub->args.readdir_cbk.entries);

		break;
	}

        case GF_FOP_READDIRP:
	{
		if (!stub->args.readdirp_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.readdirp_cbk.op_ret,
				      stub->args.readdirp_cbk.op_errno,
				      &stub->args.readdirp_cbk.entries);
		else
			stub->args.readdirp_cbk.fn (stub->frame,
						    stub->frame->cookie,
						    stub->frame->this,
						    stub->args.readdirp_cbk.op_ret,
						    stub->args.readdirp_cbk.op_errno,
						    &stub->args.readdirp_cbk.entries);

		if (stub->args.readdirp_cbk.op_ret > 0)
			gf_dirent_free (&stub->args.readdirp_cbk.entries);

		break;
	}

	case GF_FOP_XATTROP:
	{
		if (!stub->args.xattrop_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.xattrop_cbk.op_ret,
				      stub->args.xattrop_cbk.op_errno);
		else
			stub->args.xattrop_cbk.fn (stub->frame,
						   stub->frame->cookie,
						   stub->frame->this,
						   stub->args.xattrop_cbk.op_ret,
						   stub->args.xattrop_cbk.op_errno,
						   stub->args.xattrop_cbk.xattr);

		if (stub->args.xattrop_cbk.xattr)
			dict_unref (stub->args.xattrop_cbk.xattr);

		break;
	}
	case GF_FOP_FXATTROP:
	{
		if (!stub->args.fxattrop_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.fxattrop_cbk.op_ret,
				      stub->args.fxattrop_cbk.op_errno);
		else
			stub->args.fxattrop_cbk.fn (stub->frame,
						    stub->frame->cookie,
						    stub->frame->this,
						    stub->args.fxattrop_cbk.op_ret,
						    stub->args.fxattrop_cbk.op_errno,
						    stub->args.fxattrop_cbk.xattr);

		if (stub->args.fxattrop_cbk.xattr)
			dict_unref (stub->args.fxattrop_cbk.xattr);

		break;
	}
	case GF_FOP_LOCK_NOTIFY:
	{
		if (!stub->args.lock_notify_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.lock_notify_cbk.op_ret,
				      stub->args.lock_notify_cbk.op_errno);
		else
			stub->args.lock_notify_cbk.fn (stub->frame,
						       stub->frame->cookie,
						       stub->frame->this,
						       stub->args.lock_notify_cbk.op_ret,
						       stub->args.lock_notify_cbk.op_errno);
		break;
	}
	case GF_FOP_LOCK_FNOTIFY:
	{
		if (!stub->args.lock_fnotify_cbk.fn)
			STACK_UNWIND (stub->frame,
				      stub->args.lock_fnotify_cbk.op_ret,
				      stub->args.lock_fnotify_cbk.op_errno);
		else
			stub->args.lock_fnotify_cbk.fn (stub->frame,
							stub->frame->cookie,
							stub->frame->this,
							stub->args.lock_fnotify_cbk.op_ret,
							stub->args.lock_fnotify_cbk.op_errno);
		break;
	}
        case GF_FOP_SETATTR:
        {
                if (!stub->args.setattr_cbk.fn)
                        STACK_UNWIND (stub->frame,
                                      stub->args.setattr_cbk.op_ret,
                                      stub->args.setattr_cbk.op_errno,
                                      &stub->args.setattr_cbk.statpre,
                                      &stub->args.setattr_cbk.statpost);
                else
                        stub->args.setattr_cbk.fn (
                                stub->frame,
                                stub->frame->cookie,
                                stub->frame->this,
                                stub->args.setattr_cbk.op_ret,
                                stub->args.setattr_cbk.op_errno,
                                &stub->args.setattr_cbk.statpre,
                                &stub->args.setattr_cbk.statpost);
                break;
        }
        case GF_FOP_FSETATTR:
        {
                if (!stub->args.fsetattr_cbk.fn)
                        STACK_UNWIND (stub->frame,
                                      stub->args.fsetattr_cbk.op_ret,
                                      stub->args.fsetattr_cbk.op_errno,
                                      &stub->args.fsetattr_cbk.statpre,
                                      &stub->args.fsetattr_cbk.statpost);
                else
                        stub->args.fsetattr_cbk.fn (
                                stub->frame,
                                stub->frame->cookie,
                                stub->frame->this,
                                stub->args.fsetattr_cbk.op_ret,
                                stub->args.fsetattr_cbk.op_errno,
                                &stub->args.fsetattr_cbk.statpre,
                                &stub->args.fsetattr_cbk.statpost);
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
out:
	return;
}


static void
call_stub_destroy_wind (call_stub_t *stub)
{
	switch (stub->fop) {
	case GF_FOP_OPEN:
	{
		loc_wipe (&stub->args.open.loc);
		if (stub->args.open.fd)
			fd_unref (stub->args.open.fd);
		break;
	}
	case GF_FOP_CREATE:
	{
		loc_wipe (&stub->args.create.loc);
		if (stub->args.create.fd)
			fd_unref (stub->args.create.fd);
		break;
	}
	case GF_FOP_STAT:
	{
		loc_wipe (&stub->args.stat.loc);
		break;
	}
	case GF_FOP_READLINK:
	{
		loc_wipe (&stub->args.readlink.loc);
		break;
	}
  
	case GF_FOP_MKNOD:
	{
		loc_wipe (&stub->args.mknod.loc);
	}
	break;
  
	case GF_FOP_MKDIR:
	{
		loc_wipe (&stub->args.mkdir.loc);
	}
	break;
  
	case GF_FOP_UNLINK:
	{
		loc_wipe (&stub->args.unlink.loc);
	}
	break;

	case GF_FOP_RMDIR:
	{
		loc_wipe (&stub->args.rmdir.loc);
	}
	break;
      
	case GF_FOP_SYMLINK:
	{
		FREE (stub->args.symlink.linkname);
		loc_wipe (&stub->args.symlink.loc);
	}
	break;
  
	case GF_FOP_RENAME:
	{
		loc_wipe (&stub->args.rename.old);
		loc_wipe (&stub->args.rename.new);
	}
	break;

	case GF_FOP_LINK:
	{
		loc_wipe (&stub->args.link.oldloc);
		loc_wipe (&stub->args.link.newloc);
	}
	break;
  
	case GF_FOP_TRUNCATE:
	{
		loc_wipe (&stub->args.truncate.loc);
		break;
	}
      
	case GF_FOP_READ:
	{
		if (stub->args.readv.fd)
			fd_unref (stub->args.readv.fd);
		break;
	}
  
	case GF_FOP_WRITE:
	{
		struct iobref *iobref = stub->args.writev.iobref;
		if (stub->args.writev.fd)
			fd_unref (stub->args.writev.fd);
		FREE (stub->args.writev.vector);
		if (iobref)
			iobref_unref (iobref);
		break;
	}
  
	case GF_FOP_STATFS:
	{
		loc_wipe (&stub->args.statfs.loc);
		break;
	}
	case GF_FOP_FLUSH:
	{
		if (stub->args.flush.fd)
			fd_unref (stub->args.flush.fd);      
		break;
	}
  
	case GF_FOP_FSYNC:
	{
		if (stub->args.fsync.fd)
			fd_unref (stub->args.fsync.fd);
		break;
	}

	case GF_FOP_SETXATTR:
	{
		loc_wipe (&stub->args.setxattr.loc);
		if (stub->args.setxattr.dict)
			dict_unref (stub->args.setxattr.dict);
		break;
	}
  
	case GF_FOP_GETXATTR:
	{
		if (stub->args.getxattr.name)
			FREE (stub->args.getxattr.name);
		loc_wipe (&stub->args.getxattr.loc);
		break;
	}

	case GF_FOP_FSETXATTR:
	{
		fd_unref (stub->args.fsetxattr.fd);
		if (stub->args.fsetxattr.dict)
			dict_unref (stub->args.fsetxattr.dict);
		break;
	}

	case GF_FOP_FGETXATTR:
	{
		if (stub->args.fgetxattr.name)
			FREE (stub->args.fgetxattr.name);
		fd_unref (stub->args.fgetxattr.fd);
		break;
	}

	case GF_FOP_REMOVEXATTR:
	{
		loc_wipe (&stub->args.removexattr.loc);
		FREE (stub->args.removexattr.name);
		break;
	}

	case GF_FOP_OPENDIR:
	{
		loc_wipe (&stub->args.opendir.loc);
		if (stub->args.opendir.fd)
			fd_unref (stub->args.opendir.fd);
		break;
	}

	case GF_FOP_GETDENTS:
	{
		if (stub->args.getdents.fd)
			fd_unref (stub->args.getdents.fd);
		break;
	}

	case GF_FOP_FSYNCDIR:
	{
		if (stub->args.fsyncdir.fd)
			fd_unref (stub->args.fsyncdir.fd);
		break;
	}
  
	case GF_FOP_ACCESS:
	{
		loc_wipe (&stub->args.access.loc);
		break;
	}
  
	case GF_FOP_FTRUNCATE:
	{
		if (stub->args.ftruncate.fd)
			fd_unref (stub->args.ftruncate.fd);
		break;
	}
  
	case GF_FOP_FSTAT:
	{
		if (stub->args.fstat.fd)
			fd_unref (stub->args.fstat.fd);
		break;
	}
  
	case GF_FOP_LK:
	{
		if (stub->args.lk.fd)
			fd_unref (stub->args.lk.fd);
		break;
	}

	case GF_FOP_INODELK:
	{
                if (stub->args.inodelk.volume)
                        FREE (stub->args.inodelk.volume);

		loc_wipe (&stub->args.inodelk.loc);
		break;
	}
	case GF_FOP_FINODELK:
	{
                if (stub->args.finodelk.volume)
                        FREE (stub->args.finodelk.volume);

		if (stub->args.finodelk.fd)
			fd_unref (stub->args.finodelk.fd);
		break;
	}
	case GF_FOP_ENTRYLK:
	{
                if (stub->args.entrylk.volume)
                        FREE (stub->args.entrylk.volume);

		if (stub->args.entrylk.name)
			FREE (stub->args.entrylk.name);
		loc_wipe (&stub->args.entrylk.loc);
		break;
	}
	case GF_FOP_FENTRYLK:
	{
                if (stub->args.fentrylk.volume)
                        FREE (stub->args.fentrylk.volume);

		if (stub->args.fentrylk.name)
			FREE (stub->args.fentrylk.name);

 		if (stub->args.fentrylk.fd)
			fd_unref (stub->args.fentrylk.fd);
		break;
	}
  
	case GF_FOP_LOOKUP:
	{
		loc_wipe (&stub->args.lookup.loc);
		if (stub->args.lookup.xattr_req)
			dict_unref (stub->args.lookup.xattr_req);
		break;
	}

	case GF_FOP_SETDENTS:
	{
		dir_entry_t *entry, *next;
		if (stub->args.setdents.fd)
			fd_unref (stub->args.setdents.fd);
		entry = stub->args.setdents.entries.next;
		while (entry) {
			next = entry->next;
			FREE (entry->name);
			FREE (entry);
			entry = next;
		}
		break;
	}

	case GF_FOP_CHECKSUM:
	{
		loc_wipe (&stub->args.checksum.loc);
		break;
	}

	case GF_FOP_RCHECKSUM:
	{
                if (stub->args.rchecksum.fd)
                        fd_unref (stub->args.rchecksum.fd);
		break;
	}

	case GF_FOP_READDIR:
	{
		if (stub->args.readdir.fd)
			fd_unref (stub->args.readdir.fd);
		break;
	}

        case GF_FOP_READDIRP:
	{
		if (stub->args.readdirp.fd)
			fd_unref (stub->args.readdirp.fd);
		break;
	}

	case GF_FOP_XATTROP:
	{
		loc_wipe (&stub->args.xattrop.loc);
		dict_unref (stub->args.xattrop.xattr);
		break;
	}
	case GF_FOP_FXATTROP:
	{
		if (stub->args.fxattrop.fd)
			fd_unref (stub->args.fxattrop.fd);
		dict_unref (stub->args.xattrop.xattr);
		break;
	}
	case GF_FOP_LOCK_NOTIFY:
	{
		loc_wipe (&stub->args.lock_notify.loc);
		break;
	}
	case GF_FOP_LOCK_FNOTIFY:
	{
		if (stub->args.lock_fnotify.fd)
			fd_unref (stub->args.lock_fnotify.fd);
		break;
	}
        case GF_FOP_SETATTR:
        {
                loc_wipe (&stub->args.setattr.loc);
                break;
        }
        case GF_FOP_FSETATTR:
        {
                if (stub->args.fsetattr.fd)
                        fd_unref (stub->args.fsetattr.fd);
                break;
        }
	case GF_FOP_MAXVALUE:
	{
		gf_log ("call-stub",
			GF_LOG_DEBUG,
			"Invalid value of FOP");
	}
	break;
	default:
		break;
	}
}


static void
call_stub_destroy_unwind (call_stub_t *stub)
{
	switch (stub->fop) {
	case GF_FOP_OPEN:
	{
		if (stub->args.open_cbk.fd) 
			fd_unref (stub->args.open_cbk.fd);
	}
	break;

	case GF_FOP_CREATE:
	{
		if (stub->args.create_cbk.fd) 
			fd_unref (stub->args.create_cbk.fd);

		if (stub->args.create_cbk.inode)
			inode_unref (stub->args.create_cbk.inode);
	}
	break;

	case GF_FOP_STAT:
		break;

	case GF_FOP_READLINK:
	{
		if (stub->args.readlink_cbk.buf) 
			FREE (stub->args.readlink_cbk.buf);
	}
	break;
  
	case GF_FOP_MKNOD:
	{
		if (stub->args.mknod_cbk.inode)
			inode_unref (stub->args.mknod_cbk.inode);
	}
	break;
  
	case GF_FOP_MKDIR:
	{
		if (stub->args.mkdir_cbk.inode)
			inode_unref (stub->args.mkdir_cbk.inode);
	}
	break;
  
	case GF_FOP_UNLINK:
		break;

	case GF_FOP_RMDIR:
		break;
      
	case GF_FOP_SYMLINK:
	{
		if (stub->args.symlink_cbk.inode) 
			inode_unref (stub->args.symlink_cbk.inode);
	}
	break;
  
	case GF_FOP_RENAME:
		break;

	case GF_FOP_LINK:
	{
		if (stub->args.link_cbk.inode)
			inode_unref (stub->args.link_cbk.inode);
	}
	break;
  
	case GF_FOP_TRUNCATE:
		break;

	case GF_FOP_READ:
	{
		if (stub->args.readv_cbk.op_ret >= 0) {
			struct iobref *iobref = stub->args.readv_cbk.iobref;
			FREE (stub->args.readv_cbk.vector);
			
			if (iobref) {
				iobref_unref (iobref);
			}
		}
	}
	break;

	case GF_FOP_WRITE:
		break;
  
	case GF_FOP_STATFS:
		break;

	case GF_FOP_FLUSH:
		break;
  
	case GF_FOP_FSYNC:
		break;

	case GF_FOP_SETXATTR:
		break;
  
	case GF_FOP_GETXATTR:
	{
		if (stub->args.getxattr_cbk.dict)
			dict_unref (stub->args.getxattr_cbk.dict);
	}
	break;

	case GF_FOP_FSETXATTR:
		break;

	case GF_FOP_FGETXATTR:
	{
		if (stub->args.fgetxattr_cbk.dict)
			dict_unref (stub->args.fgetxattr_cbk.dict);
	}
	break;

	case GF_FOP_REMOVEXATTR:
		break;

	case GF_FOP_OPENDIR:
	{
		if (stub->args.opendir_cbk.fd)
			fd_unref (stub->args.opendir_cbk.fd);
	}
	break;

	case GF_FOP_GETDENTS:
	{
		dir_entry_t *tmp = NULL, *entries = NULL;

		entries = &stub->args.getdents_cbk.entries;
		if (stub->args.getdents_cbk.op_ret >= 0) {
			while (entries->next) {
				tmp = entries->next;
				entries->next = entries->next->next;
				FREE (tmp->name);
				FREE (tmp);
			}
		}
	}
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

	case GF_FOP_INODELK:
		break;

	case GF_FOP_FINODELK:
		break;

	case GF_FOP_ENTRYLK:
		break;

	case GF_FOP_FENTRYLK:
		break;

	case GF_FOP_LOOKUP:
	{
		if (stub->args.lookup_cbk.inode)
			inode_unref (stub->args.lookup_cbk.inode);

		if (stub->args.lookup_cbk.dict)
			dict_unref (stub->args.lookup_cbk.dict);
	}
	break;

	case GF_FOP_SETDENTS:
		break;

	case GF_FOP_CHECKSUM:
	{
		if (stub->args.checksum_cbk.op_ret >= 0) {
			FREE (stub->args.checksum_cbk.file_checksum);
			FREE (stub->args.checksum_cbk.dir_checksum); 
		}
	}
  	break;

	case GF_FOP_RCHECKSUM:
	{
		if (stub->args.rchecksum_cbk.op_ret >= 0) {
			FREE (stub->args.rchecksum_cbk.strong_checksum); 
		}
	}
  	break;

	case GF_FOP_READDIR:
	{
		if (stub->args.readdir_cbk.op_ret > 0) {
			gf_dirent_free (&stub->args.readdir_cbk.entries);
		}
	}
	break;

        case GF_FOP_READDIRP:
	{
		if (stub->args.readdirp_cbk.op_ret > 0) {
			gf_dirent_free (&stub->args.readdirp_cbk.entries);
		}
	}
	break;

	case GF_FOP_XATTROP:
	{
		if (stub->args.xattrop_cbk.xattr)
			dict_unref (stub->args.xattrop_cbk.xattr);
	}
	break;

	case GF_FOP_FXATTROP:
	{
		if (stub->args.fxattrop_cbk.xattr) 
			dict_unref (stub->args.fxattrop_cbk.xattr);
	}
	break;
        
        case GF_FOP_SETATTR:
        {
                break;
        }

        case GF_FOP_FSETATTR:
        {
                break;
        }

	case GF_FOP_MAXVALUE:
	{
		gf_log ("call-stub",
			GF_LOG_DEBUG,
			"Invalid value of FOP");
	}
	break;

	default:
		break;
	}
}

 
void
call_stub_destroy (call_stub_t *stub)
{
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);
	
	if (stub->wind) {
		call_stub_destroy_wind (stub);
	} else {
		call_stub_destroy_unwind (stub);
	}

	FREE (stub);
out:
	return;
}

void
call_resume (call_stub_t *stub)
{
        xlator_t *old_THIS = NULL;

	errno = EINVAL;
	GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

	list_del_init (&stub->list);

        old_THIS = THIS;
        THIS = stub->frame->this;
        {
                if (stub->wind)
                        call_resume_wind (stub);
                else
                        call_resume_unwind (stub);
        }
        THIS = old_THIS;

	call_stub_destroy (stub);
out:
	return;
}


