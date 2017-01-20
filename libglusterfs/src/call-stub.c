/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <openssl/md5.h>
#include <inttypes.h>

#include "call-stub.h"
#include "mem-types.h"
#include "libglusterfs-messages.h"

static call_stub_t *
stub_new (call_frame_t *frame,
          char wind,
          glusterfs_fop_t fop)
{
        call_stub_t *new = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        new = mem_get0 (frame->this->ctx->stub_mem_pool);
        GF_VALIDATE_OR_GOTO ("call-stub", new, out);

        new->frame = frame;
        new->wind = wind;
        new->fop = fop;
        new->stub_mem_pool = frame->this->ctx->stub_mem_pool;
        INIT_LIST_HEAD (&new->list);

        INIT_LIST_HEAD (&new->args_cbk.entries);
out:
        return new;
}


call_stub_t *
fop_lookup_stub (call_frame_t *frame, fop_lookup_t fn, loc_t *loc,
                 dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

        stub = stub_new (frame, 1, GF_FOP_LOOKUP);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.lookup = fn;
        args_lookup_store (&stub->args, loc, xdata);
out:
        return stub;
}


call_stub_t *
fop_lookup_cbk_stub (call_frame_t *frame, fop_lookup_cbk_t fn,
                     int32_t op_ret, int32_t op_errno,
                     inode_t *inode, struct iatt *buf,
                     dict_t *xdata, struct iatt *postparent)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_LOOKUP);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.lookup = fn;
        args_lookup_cbk_store (&stub->args_cbk, op_ret, op_errno, inode,
                               buf, xdata, postparent);
out:
        return stub;
}


call_stub_t *
fop_stat_stub (call_frame_t *frame, fop_stat_t fn,
               loc_t *loc, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

        stub = stub_new (frame, 1, GF_FOP_STAT);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.stat = fn;
        args_stat_store (&stub->args, loc, xdata);
out:
        return stub;
}


call_stub_t *
fop_stat_cbk_stub (call_frame_t *frame, fop_stat_cbk_t fn,
                   int32_t op_ret, int32_t op_errno,
                   struct iatt *buf, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_STAT);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.stat = fn;
        args_stat_cbk_store (&stub->args_cbk, op_ret, op_errno, buf,
                             xdata);
out:
        return stub;
}


call_stub_t *
fop_fstat_stub (call_frame_t *frame, fop_fstat_t fn,
                fd_t *fd, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 1, GF_FOP_FSTAT);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.fstat = fn;
        args_fstat_store (&stub->args, fd, xdata);
out:
        return stub;
}


call_stub_t *
fop_fstat_cbk_stub (call_frame_t *frame, fop_fstat_cbk_t fn,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *buf, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_FSTAT);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.fstat = fn;
        args_fstat_cbk_store (&stub->args_cbk, op_ret, op_errno, buf,
                              xdata);
out:
        return stub;
}


call_stub_t *
fop_truncate_stub (call_frame_t *frame, fop_truncate_t fn,
                   loc_t *loc, off_t off, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

        stub = stub_new (frame, 1, GF_FOP_TRUNCATE);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.truncate = fn;
        args_truncate_store (&stub->args, loc, off, xdata);
out:
        return stub;
}


call_stub_t *
fop_truncate_cbk_stub (call_frame_t *frame, fop_truncate_cbk_t fn,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
		       struct iatt *postbuf, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_TRUNCATE);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.truncate = fn;
        args_truncate_cbk_store (&stub->args_cbk, op_ret, op_errno,
                                 prebuf, postbuf, xdata);
out:
        return stub;
}


call_stub_t *
fop_ftruncate_stub (call_frame_t *frame, fop_ftruncate_t fn,
                    fd_t *fd, off_t off, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 1, GF_FOP_FTRUNCATE);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.ftruncate = fn;
        args_ftruncate_store (&stub->args, fd, off, xdata);

out:
        return stub;
}


call_stub_t *
fop_ftruncate_cbk_stub (call_frame_t *frame, fop_ftruncate_cbk_t fn,
                        int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
			struct iatt *postbuf, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_FTRUNCATE);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.ftruncate = fn;
        args_ftruncate_cbk_store (&stub->args_cbk, op_ret, op_errno,
                                  prebuf, postbuf, xdata);

out:
        return stub;
}


call_stub_t *
fop_access_stub (call_frame_t *frame, fop_access_t fn,
                 loc_t *loc, int32_t mask, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

        stub = stub_new (frame, 1, GF_FOP_ACCESS);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.access = fn;
        args_access_store (&stub->args, loc, mask, xdata);
out:
        return stub;
}


call_stub_t *
fop_access_cbk_stub (call_frame_t *frame, fop_access_cbk_t fn,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_ACCESS);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.access = fn;
        args_access_cbk_store (&stub->args_cbk, op_ret, op_errno, xdata);
out:
        return stub;
}


call_stub_t *
fop_readlink_stub (call_frame_t *frame, fop_readlink_t fn,
                   loc_t *loc, size_t size, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

        stub = stub_new (frame, 1, GF_FOP_READLINK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.readlink = fn;
        args_readlink_store (&stub->args, loc, size, xdata);
out:
        return stub;
}


call_stub_t *
fop_readlink_cbk_stub (call_frame_t *frame, fop_readlink_cbk_t fn,
                       int32_t op_ret, int32_t op_errno,
                       const char *path, struct iatt *stbuf, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_READLINK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.readlink = fn;
        args_readlink_cbk_store (&stub->args_cbk, op_ret, op_errno, path,
                                 stbuf, xdata);
out:
        return stub;
}


call_stub_t *
fop_mknod_stub (call_frame_t *frame, fop_mknod_t fn, loc_t *loc, mode_t mode,
                dev_t rdev, mode_t umask, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

        stub = stub_new (frame, 1, GF_FOP_MKNOD);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.mknod = fn;
        args_mknod_store (&stub->args, loc, mode, rdev, umask, xdata);
out:
        return stub;
}


call_stub_t *
fop_mknod_cbk_stub (call_frame_t *frame, fop_mknod_cbk_t fn, int32_t op_ret,
                    int32_t op_errno, inode_t *inode, struct iatt *buf,
                    struct iatt *preparent, struct iatt *postparent,
		    dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_MKNOD);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.mknod = fn;
        args_mknod_cbk_store (&stub->args_cbk, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
out:
        return stub;
}


call_stub_t *
fop_mkdir_stub (call_frame_t *frame, fop_mkdir_t fn,
                loc_t *loc, mode_t mode, mode_t umask, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

        stub = stub_new (frame, 1, GF_FOP_MKDIR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.mkdir = fn;
        args_mkdir_store (&stub->args, loc, mode, umask, xdata);
out:
        return stub;
}


call_stub_t *
fop_mkdir_cbk_stub (call_frame_t *frame, fop_mkdir_cbk_t fn,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_MKDIR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.mkdir = fn;
        args_mkdir_cbk_store (&stub->args_cbk, op_ret, op_errno, inode,
                              buf, preparent, postparent, xdata);
out:
        return stub;
}


call_stub_t *
fop_unlink_stub (call_frame_t *frame, fop_unlink_t fn,
                 loc_t *loc, int xflag, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

        stub = stub_new (frame, 1, GF_FOP_UNLINK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.unlink = fn;
        args_unlink_store (&stub->args, loc, xflag, xdata);

out:
        return stub;
}


call_stub_t *
fop_unlink_cbk_stub (call_frame_t *frame, fop_unlink_cbk_t fn,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *preparent, struct iatt *postparent,
		     dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_UNLINK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.unlink = fn;
        args_unlink_cbk_store (&stub->args_cbk, op_ret, op_errno, preparent,
                               postparent, xdata);
out:
        return stub;
}



call_stub_t *
fop_rmdir_stub (call_frame_t *frame, fop_rmdir_t fn,
                loc_t *loc, int flags, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

        stub = stub_new (frame, 1, GF_FOP_RMDIR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.rmdir = fn;
        args_rmdir_store (&stub->args, loc, flags, xdata);

out:
        return stub;
}


call_stub_t *
fop_rmdir_cbk_stub (call_frame_t *frame, fop_rmdir_cbk_t fn,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *preparent, struct iatt *postparent,
		    dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_RMDIR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.rmdir = fn;
        args_rmdir_cbk_store (&stub->args_cbk, op_ret, op_errno, preparent,
                              postparent, xdata);
out:
        return stub;
}


call_stub_t *
fop_symlink_stub (call_frame_t *frame, fop_symlink_t fn,
                  const char *linkname, loc_t *loc, mode_t umask, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", loc, out);
        GF_VALIDATE_OR_GOTO ("call-stub", linkname, out);

        stub = stub_new (frame, 1, GF_FOP_SYMLINK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.symlink = fn;
        args_symlink_store (&stub->args, linkname, loc, umask, xdata);
out:
        return stub;
}


call_stub_t *
fop_symlink_cbk_stub (call_frame_t *frame, fop_symlink_cbk_t fn,
                      int32_t op_ret, int32_t op_errno,
                      inode_t *inode, struct iatt *buf,
                      struct iatt *preparent, struct iatt *postparent,
		      dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_SYMLINK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.symlink = fn;
        args_symlink_cbk_store (&stub->args_cbk, op_ret, op_errno, inode, buf,
                                preparent, postparent, xdata);
out:
        return stub;
}


call_stub_t *
fop_rename_stub (call_frame_t *frame, fop_rename_t fn,
                 loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", oldloc, out);
        GF_VALIDATE_OR_GOTO ("call-stub", newloc, out);

        stub = stub_new (frame, 1, GF_FOP_RENAME);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.rename = fn;
        args_rename_store (&stub->args, oldloc, newloc, xdata);
out:
        return stub;
}


call_stub_t *
fop_rename_cbk_stub (call_frame_t *frame, fop_rename_cbk_t fn,
                     int32_t op_ret, int32_t op_errno, struct iatt *buf,
                     struct iatt *preoldparent, struct iatt *postoldparent,
                     struct iatt *prenewparent, struct iatt *postnewparent,
		     dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_RENAME);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.rename = fn;
        args_rename_cbk_store (&stub->args_cbk, op_ret, op_errno, buf,
                               preoldparent, postoldparent, prenewparent,
                               postnewparent, xdata);
out:
        return stub;
}


call_stub_t *
fop_link_stub (call_frame_t *frame, fop_link_t fn,
               loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", oldloc, out);
        GF_VALIDATE_OR_GOTO ("call-stub", newloc, out);

        stub = stub_new (frame, 1, GF_FOP_LINK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.link = fn;
        args_link_store (&stub->args, oldloc, newloc, xdata);
out:
        return stub;
}


call_stub_t *
fop_link_cbk_stub (call_frame_t *frame, fop_link_cbk_t fn,
                   int32_t op_ret, int32_t op_errno,
                   inode_t *inode, struct iatt *buf,
                   struct iatt *preparent, struct iatt *postparent,
		   dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_LINK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.link = fn;
        args_link_cbk_store (&stub->args_cbk, op_ret, op_errno,
                             inode, buf, preparent, postparent, xdata);
out:
        return stub;
}


call_stub_t *
fop_create_stub (call_frame_t *frame, fop_create_t fn,
                 loc_t *loc, int32_t flags, mode_t mode,
                 mode_t umask, fd_t *fd, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

        stub = stub_new (frame, 1, GF_FOP_CREATE);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.create = fn;
        args_create_store (&stub->args, loc, flags, mode,
                           umask, fd, xdata);
out:
        return stub;
}


call_stub_t *
fop_create_cbk_stub (call_frame_t *frame, fop_create_cbk_t fn,
                     int32_t op_ret, int32_t op_errno,
                     fd_t *fd, inode_t *inode, struct iatt *buf,
                     struct iatt *preparent, struct iatt *postparent,
		     dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_CREATE);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.create = fn;
        args_create_cbk_store (&stub->args_cbk, op_ret, op_errno, fd, inode,
                               buf, preparent, postparent, xdata);
out:
        return stub;
}


call_stub_t *
fop_open_stub (call_frame_t *frame, fop_open_t fn,
               loc_t *loc, int32_t flags, fd_t *fd, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

        stub = stub_new (frame, 1, GF_FOP_OPEN);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.open = fn;
        args_open_store (&stub->args, loc, flags, fd, xdata);
out:
        return stub;
}


call_stub_t *
fop_open_cbk_stub (call_frame_t *frame, fop_open_cbk_t fn,
                   int32_t op_ret, int32_t op_errno,
                   fd_t *fd, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_OPEN);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.open = fn;
        args_open_cbk_store (&stub->args_cbk, op_ret, op_errno, fd, xdata);
out:
        return stub;
}


call_stub_t *
fop_readv_stub (call_frame_t *frame, fop_readv_t fn,
                fd_t *fd, size_t size, off_t off, uint32_t flags,
		dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 1, GF_FOP_READ);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.readv = fn;
        args_readv_store (&stub->args, fd, size, off, flags, xdata);
out:
        return stub;
}


call_stub_t *
fop_readv_cbk_stub (call_frame_t *frame, fop_readv_cbk_t fn,
                    int32_t op_ret, int32_t op_errno, struct iovec *vector,
		    int32_t count, struct iatt *stbuf,
                    struct iobref *iobref, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_READ);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.readv = fn;
        args_readv_cbk_store (&stub->args_cbk, op_ret, op_errno, vector,
                              count, stbuf, iobref, xdata);
out:
        return stub;
}


call_stub_t *
fop_writev_stub (call_frame_t *frame, fop_writev_t fn,
                 fd_t *fd, struct iovec *vector, int32_t count, off_t off,
		 uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", vector, out);

        stub = stub_new (frame, 1, GF_FOP_WRITE);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.writev = fn;
        args_writev_store (&stub->args, fd, vector, count, off, flags,
                           iobref, xdata);
out:
        return stub;
}


call_stub_t *
fop_writev_cbk_stub (call_frame_t *frame, fop_writev_cbk_t fn,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_WRITE);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.writev = fn;
        args_writev_cbk_store (&stub->args_cbk, op_ret, op_errno,
                               prebuf, postbuf, xdata);
out:
        return stub;
}


call_stub_t *
fop_flush_stub (call_frame_t *frame, fop_flush_t fn,
                fd_t *fd, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 1, GF_FOP_FLUSH);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.flush = fn;
        args_flush_store (&stub->args, fd, xdata);
out:
        return stub;
}


call_stub_t *
fop_flush_cbk_stub (call_frame_t *frame, fop_flush_cbk_t fn,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_FLUSH);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.flush = fn;
        args_flush_cbk_store (&stub->args_cbk, op_ret, op_errno, xdata);
out:
        return stub;
}


call_stub_t *
fop_fsync_stub (call_frame_t *frame, fop_fsync_t fn,
                fd_t *fd, int32_t datasync, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 1, GF_FOP_FSYNC);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.fsync = fn;
        args_fsync_store (&stub->args, fd, datasync, xdata);
out:
        return stub;
}


call_stub_t *
fop_fsync_cbk_stub (call_frame_t *frame, fop_fsync_cbk_t fn,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *prebuf, struct iatt *postbuf, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_FSYNC);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.fsync = fn;
        args_fsync_cbk_store (&stub->args_cbk, op_ret, op_errno,
                              prebuf, postbuf, xdata);
out:
        return stub;
}


call_stub_t *
fop_opendir_stub (call_frame_t *frame, fop_opendir_t fn,
                  loc_t *loc, fd_t *fd, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

        stub = stub_new (frame, 1, GF_FOP_OPENDIR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.opendir = fn;
        args_opendir_store (&stub->args, loc, fd, xdata);
out:
        return stub;
}


call_stub_t *
fop_opendir_cbk_stub (call_frame_t *frame, fop_opendir_cbk_t fn,
                      int32_t op_ret, int32_t op_errno,
                      fd_t *fd, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_OPENDIR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.opendir = fn;
        args_opendir_cbk_store (&stub->args_cbk, op_ret, op_errno, fd, xdata);
out:
        return stub;
}


call_stub_t *
fop_fsyncdir_stub (call_frame_t *frame, fop_fsyncdir_t fn,
                   fd_t *fd, int32_t datasync, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 1, GF_FOP_FSYNCDIR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.fsyncdir = fn;
        args_fsyncdir_store (&stub->args, fd, datasync, xdata);
out:
        return stub;
}


call_stub_t *
fop_fsyncdir_cbk_stub (call_frame_t *frame, fop_fsyncdir_cbk_t fn,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_FSYNCDIR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.fsyncdir = fn;
        args_fsyncdir_cbk_store (&stub->args_cbk, op_ret, op_errno, xdata);
out:
        return stub;
}


call_stub_t *
fop_statfs_stub (call_frame_t *frame, fop_statfs_t fn,
                 loc_t *loc, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

        stub = stub_new (frame, 1, GF_FOP_STATFS);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.statfs = fn;
        args_statfs_store (&stub->args, loc, xdata);
out:
        return stub;
}


call_stub_t *
fop_statfs_cbk_stub (call_frame_t *frame, fop_statfs_cbk_t fn,
                     int32_t op_ret, int32_t op_errno,
                     struct statvfs *buf, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_STATFS);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.statfs = fn;
        args_statfs_cbk_store (&stub->args_cbk, op_ret, op_errno, buf, xdata);
out:
        return stub;
}


call_stub_t *
fop_setxattr_stub (call_frame_t *frame, fop_setxattr_t fn,
                   loc_t *loc, dict_t *dict,
                   int32_t flags, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

        stub = stub_new (frame, 1, GF_FOP_SETXATTR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.setxattr = fn;
        args_setxattr_store (&stub->args, loc, dict, flags, xdata);
out:
        return stub;
}


call_stub_t *
fop_setxattr_cbk_stub (call_frame_t *frame,
                       fop_setxattr_cbk_t fn,
                       int32_t op_ret,
                       int32_t op_errno, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_SETXATTR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.setxattr = fn;
        args_setxattr_cbk_store (&stub->args_cbk, op_ret, op_errno, xdata);
out:
        return stub;
}


call_stub_t *
fop_getxattr_stub (call_frame_t *frame, fop_getxattr_t fn,
                   loc_t *loc, const char *name, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", loc, out);

        stub = stub_new (frame, 1, GF_FOP_GETXATTR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.getxattr = fn;
        args_getxattr_store (&stub->args, loc, name, xdata);
out:
        return stub;
}


call_stub_t *
fop_getxattr_cbk_stub (call_frame_t *frame, fop_getxattr_cbk_t fn,
                       int32_t op_ret, int32_t op_errno,
                       dict_t *dict, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_GETXATTR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.getxattr = fn;
        args_getxattr_cbk_store (&stub->args_cbk, op_ret, op_errno, dict,
                                 xdata);
out:
        return stub;
}


call_stub_t *
fop_fsetxattr_stub (call_frame_t *frame, fop_fsetxattr_t fn,
                    fd_t *fd, dict_t *dict, int32_t flags, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", fd, out);

        stub = stub_new (frame, 1, GF_FOP_FSETXATTR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.fsetxattr = fn;
        args_fsetxattr_store (&stub->args, fd, dict, flags, xdata);
out:
        return stub;
}


call_stub_t *
fop_fsetxattr_cbk_stub (call_frame_t *frame, fop_fsetxattr_cbk_t fn,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_FSETXATTR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.fsetxattr = fn;
        args_fsetxattr_cbk_store (&stub->args_cbk, op_ret, op_errno, xdata);
out:
        return stub;
}


call_stub_t *
fop_fgetxattr_stub (call_frame_t *frame, fop_fgetxattr_t fn,
                    fd_t *fd, const char *name, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", fd, out);

        stub = stub_new (frame, 1, GF_FOP_FGETXATTR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.fgetxattr = fn;
        args_fgetxattr_store (&stub->args, fd, name, xdata);
out:
        return stub;
}


call_stub_t *
fop_fgetxattr_cbk_stub (call_frame_t *frame, fop_fgetxattr_cbk_t fn,
                        int32_t op_ret, int32_t op_errno,
                        dict_t *dict, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_GETXATTR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.fgetxattr = fn;
        args_fgetxattr_cbk_store (&stub->args_cbk, op_ret, op_errno, dict,
                                  xdata);
out:
        return stub;
}


call_stub_t *
fop_removexattr_stub (call_frame_t *frame, fop_removexattr_t fn,
                      loc_t *loc, const char *name, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", loc, out);
        GF_VALIDATE_OR_GOTO ("call-stub", name, out);

        stub = stub_new (frame, 1, GF_FOP_REMOVEXATTR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.removexattr = fn;
        args_removexattr_store (&stub->args, loc, name, xdata);
out:
        return stub;
}


call_stub_t *
fop_removexattr_cbk_stub (call_frame_t *frame, fop_removexattr_cbk_t fn,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_REMOVEXATTR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.removexattr = fn;
        args_removexattr_cbk_store (&stub->args_cbk, op_ret, op_errno, xdata);
out:
        return stub;
}


call_stub_t *
fop_fremovexattr_stub (call_frame_t *frame, fop_fremovexattr_t fn,
                       fd_t *fd, const char *name, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", fd, out);
        GF_VALIDATE_OR_GOTO ("call-stub", name, out);

        stub = stub_new (frame, 1, GF_FOP_FREMOVEXATTR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.fremovexattr = fn;
        args_fremovexattr_store (&stub->args, fd, name, xdata);
out:
        return stub;
}


call_stub_t *
fop_fremovexattr_cbk_stub (call_frame_t *frame, fop_fremovexattr_cbk_t fn,
                           int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_FREMOVEXATTR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.fremovexattr = fn;
        args_fremovexattr_cbk_store (&stub->args_cbk, op_ret, op_errno, xdata);
out:
        return stub;
}


call_stub_t *
fop_lk_stub (call_frame_t *frame, fop_lk_t fn,
             fd_t *fd, int32_t cmd,
             struct gf_flock *lock, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", lock, out);

        stub = stub_new (frame, 1, GF_FOP_LK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.lk = fn;
        args_lk_store (&stub->args, fd, cmd, lock, xdata);
out:
        return stub;
}


call_stub_t *
fop_lk_cbk_stub (call_frame_t *frame, fop_lk_cbk_t fn,
                 int32_t op_ret, int32_t op_errno,
                 struct gf_flock *lock, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_LK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.lk = fn;
        args_lk_cbk_store (&stub->args_cbk, op_ret, op_errno, lock, xdata);
out:
        return stub;
}


call_stub_t *
fop_inodelk_stub (call_frame_t *frame, fop_inodelk_t fn,
                  const char *volume, loc_t *loc, int32_t cmd,
                  struct gf_flock *lock, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", lock, out);

        stub = stub_new (frame, 1, GF_FOP_INODELK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.inodelk = fn;
        args_inodelk_store (&stub->args, volume, loc, cmd,
                            lock, xdata);
out:
        return stub;
}


call_stub_t *
fop_inodelk_cbk_stub (call_frame_t *frame, fop_inodelk_cbk_t fn,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_INODELK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.inodelk       = fn;
        args_inodelk_cbk_store (&stub->args_cbk, op_ret, op_errno, xdata);
out:
        return stub;
}


call_stub_t *
fop_finodelk_stub (call_frame_t *frame, fop_finodelk_t fn,
                   const char *volume, fd_t *fd, int32_t cmd,
                   struct gf_flock *lock, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", lock, out);

        stub = stub_new (frame, 1, GF_FOP_FINODELK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.finodelk = fn;
        args_finodelk_store (&stub->args, volume, fd, cmd,
                             lock, xdata);

out:
        return stub;
}


call_stub_t *
fop_finodelk_cbk_stub (call_frame_t *frame, fop_inodelk_cbk_t fn,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_FINODELK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.finodelk       = fn;
        args_finodelk_cbk_store (&stub->args_cbk, op_ret, op_errno, xdata);
out:
        return stub;
}


call_stub_t *
fop_entrylk_stub (call_frame_t *frame, fop_entrylk_t fn,
                  const char *volume, loc_t *loc, const char *name,
                  entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 1, GF_FOP_ENTRYLK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.entrylk = fn;
        args_entrylk_store (&stub->args, volume, loc, name, cmd, type, xdata);

out:
        return stub;
}


call_stub_t *
fop_entrylk_cbk_stub (call_frame_t *frame, fop_entrylk_cbk_t fn,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_ENTRYLK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.entrylk       = fn;
        args_entrylk_cbk_store (&stub->args_cbk, op_ret, op_errno, xdata);
out:
        return stub;
}


call_stub_t *
fop_fentrylk_stub (call_frame_t *frame, fop_fentrylk_t fn,
                   const char *volume, fd_t *fd, const char *name,
                   entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 1, GF_FOP_FENTRYLK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.fentrylk = fn;
        args_fentrylk_store (&stub->args, volume, fd, name, cmd, type, xdata);
out:
        return stub;
}


call_stub_t *
fop_fentrylk_cbk_stub (call_frame_t *frame, fop_fentrylk_cbk_t fn,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_FENTRYLK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.fentrylk       = fn;
        args_fentrylk_cbk_store (&stub->args_cbk, op_ret, op_errno, xdata);
out:
        return stub;
}


call_stub_t *
fop_readdirp_cbk_stub (call_frame_t *frame, fop_readdirp_cbk_t fn,
                       int32_t op_ret, int32_t op_errno,
                       gf_dirent_t *entries, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_READDIRP);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.readdirp = fn;
        args_readdirp_cbk_store (&stub->args_cbk, op_ret, op_errno, entries,
                                 xdata);
out:
        return stub;
}


call_stub_t *
fop_readdir_cbk_stub (call_frame_t *frame, fop_readdir_cbk_t fn,
                      int32_t op_ret, int32_t op_errno,
                      gf_dirent_t *entries, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_READDIR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.readdir = fn;
        args_readdir_cbk_store (&stub->args_cbk, op_ret, op_errno, entries,
                                xdata);
out:
        return stub;
}


call_stub_t *
fop_readdir_stub (call_frame_t *frame, fop_readdir_t fn,
                  fd_t *fd, size_t size,
                  off_t off, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        stub = stub_new (frame, 1, GF_FOP_READDIR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.readdir = fn;
        args_readdir_store (&stub->args, fd, size, off, xdata);
out:
        return stub;
}


call_stub_t *
fop_readdirp_stub (call_frame_t *frame, fop_readdirp_t fn,
                   fd_t *fd, size_t size, off_t off, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        stub = stub_new (frame, 1, GF_FOP_READDIRP);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.readdirp = fn;
        args_readdirp_store (&stub->args, fd, size, off, xdata);
out:
        return stub;
}


call_stub_t *
fop_rchecksum_stub (call_frame_t *frame, fop_rchecksum_t fn,
                    fd_t *fd, off_t offset, int32_t len, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", fd, out);

        stub = stub_new (frame, 1, GF_FOP_RCHECKSUM);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.rchecksum = fn;
        args_rchecksum_store (&stub->args, fd, offset, len, xdata);
out:
        return stub;
}


call_stub_t *
fop_rchecksum_cbk_stub (call_frame_t *frame, fop_rchecksum_cbk_t fn,
                        int32_t op_ret, int32_t op_errno,
                        uint32_t weak_checksum, uint8_t *strong_checksum,
			dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_RCHECKSUM);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.rchecksum = fn;
        args_rchecksum_cbk_store (&stub->args_cbk, op_ret, op_errno,
                                  weak_checksum, strong_checksum, xdata);
out:
        return stub;
}


call_stub_t *
fop_xattrop_cbk_stub (call_frame_t *frame, fop_xattrop_cbk_t fn, int32_t op_ret,
                      int32_t op_errno, dict_t *xattr, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_XATTROP);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.xattrop       = fn;
        args_xattrop_cbk_store (&stub->args_cbk, op_ret, op_errno, xattr,
                                xdata);
out:
        return stub;
}


call_stub_t *
fop_fxattrop_cbk_stub (call_frame_t *frame, fop_fxattrop_cbk_t fn,
                       int32_t op_ret, int32_t op_errno,
                       dict_t *xattr, dict_t *xdata)
{
        call_stub_t *stub = NULL;
        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_FXATTROP);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.fxattrop = fn;
        args_xattrop_cbk_store (&stub->args_cbk, op_ret, op_errno, xattr,
                                xdata);
out:
        return stub;
}


call_stub_t *
fop_xattrop_stub (call_frame_t *frame, fop_xattrop_t fn,
                  loc_t *loc, gf_xattrop_flags_t optype,
                  dict_t *xattr, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", xattr, out);

        stub = stub_new (frame, 1, GF_FOP_XATTROP);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.xattrop = fn;
        args_xattrop_store (&stub->args, loc, optype, xattr, xdata);
out:
        return stub;
}


call_stub_t *
fop_fxattrop_stub (call_frame_t *frame, fop_fxattrop_t fn,
                   fd_t *fd, gf_xattrop_flags_t optype,
                   dict_t *xattr, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", xattr, out);

        stub = stub_new (frame, 1, GF_FOP_FXATTROP);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.fxattrop = fn;
        args_fxattrop_store (&stub->args, fd, optype, xattr, xdata);
out:
        return stub;
}


call_stub_t *
fop_setattr_cbk_stub (call_frame_t *frame, fop_setattr_cbk_t fn,
                      int32_t op_ret, int32_t op_errno,
                      struct iatt *statpre, struct iatt *statpost,
		      dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_SETATTR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.setattr = fn;
        args_setattr_cbk_store (&stub->args_cbk, op_ret, op_errno, statpre,
                                statpost, xdata);
out:
        return stub;
}


call_stub_t *
fop_fsetattr_cbk_stub (call_frame_t *frame, fop_setattr_cbk_t fn,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt *statpre, struct iatt *statpost,
		       dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_FSETATTR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.fsetattr = fn;
        args_fsetattr_cbk_store (&stub->args_cbk, op_ret, op_errno, statpre,
                                 statpost, xdata);
out:
        return stub;
}


call_stub_t *
fop_setattr_stub (call_frame_t *frame, fop_setattr_t fn,
                  loc_t *loc, struct iatt *stbuf,
                  int32_t valid, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", fn, out);

        stub = stub_new (frame, 1, GF_FOP_SETATTR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.setattr = fn;
        args_setattr_store (&stub->args, loc, stbuf, valid, xdata);
out:
        return stub;
}


call_stub_t *
fop_fsetattr_stub (call_frame_t *frame, fop_fsetattr_t fn,
                   fd_t *fd, struct iatt *stbuf,
                   int32_t valid, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", fn, out);

        stub = stub_new (frame, 1, GF_FOP_FSETATTR);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.fsetattr = fn;
        args_fsetattr_store (&stub->args, fd, stbuf, valid, xdata);
out:
        return stub;
}

call_stub_t *
fop_fallocate_cbk_stub(call_frame_t *frame, fop_fallocate_cbk_t fn,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt *statpre, struct iatt *statpost,
		       dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_FALLOCATE);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.fallocate = fn;

        args_fallocate_cbk_store (&stub->args_cbk, op_ret, op_errno, statpre,
                                  statpost, xdata);
out:
        return stub;
}

call_stub_t *
fop_fallocate_stub(call_frame_t *frame, fop_fallocate_t fn, fd_t *fd,
		   int32_t mode, off_t offset, size_t len, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", fn, out);

        stub = stub_new (frame, 1, GF_FOP_FALLOCATE);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.fallocate = fn;
        args_fallocate_store (&stub->args, fd, mode, offset, len, xdata);
out:
        return stub;

}

call_stub_t *
fop_discard_cbk_stub(call_frame_t *frame, fop_discard_cbk_t fn,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *statpre, struct iatt *statpost,
		     dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_DISCARD);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.discard = fn;

        args_discard_cbk_store (&stub->args_cbk, op_ret, op_errno, statpre,
                                statpost, xdata);
out:
        return stub;
}

call_stub_t *
fop_discard_stub(call_frame_t *frame, fop_discard_t fn, fd_t *fd,
		 off_t offset, size_t len, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", fn, out);

        stub = stub_new (frame, 1, GF_FOP_DISCARD);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.discard = fn;
        args_discard_store (&stub->args, fd, offset, len, xdata);
out:
        return stub;

}

call_stub_t *
fop_zerofill_cbk_stub(call_frame_t *frame, fop_zerofill_cbk_t fn,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *statpre, struct iatt *statpost,
                     dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_ZEROFILL);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.zerofill = fn;

        args_zerofill_cbk_store (&stub->args_cbk, op_ret, op_errno, statpre,
                                 statpost, xdata);
out:
        return stub;
}

call_stub_t *
fop_zerofill_stub(call_frame_t *frame, fop_zerofill_t fn, fd_t *fd,
                 off_t offset, off_t len, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", fn, out);

        stub = stub_new (frame, 1, GF_FOP_ZEROFILL);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.zerofill = fn;
        args_zerofill_store (&stub->args, fd, offset, len, xdata);
out:
        return stub;

}


call_stub_t *
fop_ipc_cbk_stub (call_frame_t *frame, fop_ipc_cbk_t fn,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_IPC);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.ipc = fn;

        args_ipc_cbk_store (&stub->args_cbk, op_ret, op_errno, xdata);
out:
        return stub;
}

call_stub_t *
fop_ipc_stub (call_frame_t *frame, fop_ipc_t fn,
              int32_t op, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", fn, out);

        stub = stub_new (frame, 1, GF_FOP_IPC);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.ipc = fn;
        args_ipc_store (&stub->args, op, xdata);
out:
        return stub;

}

call_stub_t *
fop_lease_cbk_stub (call_frame_t *frame, fop_lease_cbk_t fn,
                    int32_t op_ret, int32_t op_errno,
                    struct gf_lease *lease, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_LEASE);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.lease = fn;
        args_lease_cbk_store (&stub->args_cbk, op_ret, op_errno, lease, xdata);
out:
        return stub;
}

call_stub_t *
fop_lease_stub (call_frame_t *frame, fop_lease_t fn,
                loc_t *loc, struct gf_lease *lease, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", fn, out);
        GF_VALIDATE_OR_GOTO ("call-stub", lease, out);

        stub = stub_new (frame, 1, GF_FOP_LEASE);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.lease = fn;
        args_lease_store (&stub->args, loc, lease, xdata);
out:
        return stub;

}

call_stub_t *
fop_seek_cbk_stub (call_frame_t *frame, fop_seek_cbk_t fn,
                   int32_t op_ret, int32_t op_errno, off_t offset,
                   dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_SEEK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.seek = fn;

        args_seek_cbk_store (&stub->args_cbk, op_ret, op_errno, offset, xdata);
out:
        return stub;
}


call_stub_t *
fop_seek_stub (call_frame_t *frame, fop_seek_t fn, fd_t *fd,
               off_t offset, gf_seek_what_t what, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", fn, out);

        stub = stub_new (frame, 1, GF_FOP_SEEK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.seek = fn;
        args_seek_store (&stub->args, fd, offset, what, xdata);
out:
        return stub;

}

call_stub_t *
fop_getactivelk_cbk_stub (call_frame_t *frame, fop_getactivelk_cbk_t fn,
                          int32_t op_ret, int32_t op_errno,
                          lock_migration_info_t *lmi, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_GETACTIVELK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.getactivelk = fn;

        args_getactivelk_cbk_store (&stub->args_cbk, op_ret, op_errno, lmi,
                                    xdata);
out:
        return stub;
}


call_stub_t *
fop_getactivelk_stub (call_frame_t *frame, fop_getactivelk_t fn, loc_t *loc,
                      dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", fn, out);

        stub = stub_new (frame, 1, GF_FOP_GETACTIVELK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.getactivelk = fn;

        loc_copy (&stub->args.loc, loc);

        if (xdata)
                stub->args.xdata = dict_ref (xdata);
out:
        return stub;

}

call_stub_t *
fop_setactivelk_cbk_stub (call_frame_t *frame, fop_setactivelk_cbk_t fn,
                           int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);

        stub = stub_new (frame, 0, GF_FOP_SETACTIVELK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn_cbk.setactivelk = fn;
        stub->args_cbk.op_ret = op_ret;
        stub->args_cbk.op_errno = op_errno;

        if (xdata)
                stub->args.xdata = dict_ref (xdata);

out:
        return stub;
}

call_stub_t *
fop_setactivelk_stub (call_frame_t *frame, fop_setactivelk_t fn,
                        loc_t *loc, lock_migration_info_t *locklist,
                        dict_t *xdata)
{
        call_stub_t *stub = NULL;

        GF_VALIDATE_OR_GOTO ("call-stub", frame, out);
        GF_VALIDATE_OR_GOTO ("call-stub", fn, out);

        stub = stub_new (frame, 1, GF_FOP_SETACTIVELK);
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        stub->fn.setactivelk = fn;

        args_setactivelk_store (&stub->args, loc, locklist, xdata);

out:
        return stub;

}

void
call_resume_wind (call_stub_t *stub)
{
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        switch (stub->fop) {
        case GF_FOP_OPEN:
                stub->fn.open (stub->frame, stub->frame->this,
			       &stub->args.loc, stub->args.flags,
			       stub->args.fd, stub->args.xdata);
                break;
        case GF_FOP_CREATE:
                stub->fn.create (stub->frame, stub->frame->this,
				 &stub->args.loc, stub->args.flags,
				 stub->args.mode, stub->args.umask,
				 stub->args.fd, stub->args.xdata);
                break;
        case GF_FOP_STAT:
                stub->fn.stat (stub->frame, stub->frame->this,
			       &stub->args.loc, stub->args.xdata);
                break;
        case GF_FOP_READLINK:
                stub->fn.readlink (stub->frame, stub->frame->this,
				   &stub->args.loc, stub->args.size,
				   stub->args.xdata);
                break;
        case GF_FOP_MKNOD:
                stub->fn.mknod (stub->frame, stub->frame->this,
				&stub->args.loc, stub->args.mode,
				stub->args.rdev, stub->args.umask,
				stub->args.xdata);
		break;
        case GF_FOP_MKDIR:
                stub->fn.mkdir (stub->frame, stub->frame->this,
				&stub->args.loc, stub->args.mode,
				stub->args.umask, stub->args.xdata);
		break;
        case GF_FOP_UNLINK:
                stub->fn.unlink (stub->frame, stub->frame->this,
				 &stub->args.loc, stub->args.xflag,
				 stub->args.xdata);
		break;
        case GF_FOP_RMDIR:
                stub->fn.rmdir (stub->frame, stub->frame->this,
				&stub->args.loc, stub->args.flags,
				stub->args.xdata);
		break;
        case GF_FOP_SYMLINK:
                stub->fn.symlink (stub->frame, stub->frame->this,
				  stub->args.linkname, &stub->args.loc,
				  stub->args.umask, stub->args.xdata);
		break;
        case GF_FOP_RENAME:
                stub->fn.rename (stub->frame, stub->frame->this,
				 &stub->args.loc, &stub->args.loc2,
				 stub->args.xdata);
		break;
        case GF_FOP_LINK:
                stub->fn.link (stub->frame, stub->frame->this,
			       &stub->args.loc, &stub->args.loc2,
			       stub->args.xdata);
		break;
        case GF_FOP_TRUNCATE:
                stub->fn.truncate (stub->frame, stub->frame->this,
				   &stub->args.loc, stub->args.offset,
				   stub->args.xdata);
                break;
        case GF_FOP_READ:
                stub->fn.readv (stub->frame, stub->frame->this,
				stub->args.fd, stub->args.size,
				stub->args.offset, stub->args.flags,
				stub->args.xdata);
                break;
        case GF_FOP_WRITE:
                stub->fn.writev (stub->frame, stub->frame->this,
				 stub->args.fd, stub->args.vector,
				 stub->args.count, stub->args.offset,
				 stub->args.flags, stub->args.iobref,
				 stub->args.xdata);
                break;
        case GF_FOP_STATFS:
                stub->fn.statfs (stub->frame, stub->frame->this,
				 &stub->args.loc, stub->args.xdata);
                break;
        case GF_FOP_FLUSH:
                stub->fn.flush (stub->frame, stub->frame->this,
				stub->args.fd, stub->args.xdata);
                break;
        case GF_FOP_FSYNC:
                stub->fn.fsync (stub->frame, stub->frame->this,
				stub->args.fd, stub->args.datasync,
				stub->args.xdata);
                break;
        case GF_FOP_SETXATTR:
                stub->fn.setxattr (stub->frame, stub->frame->this,
				   &stub->args.loc, stub->args.xattr,
				   stub->args.flags, stub->args.xdata);
                break;
        case GF_FOP_GETXATTR:
                stub->fn.getxattr (stub->frame, stub->frame->this,
				   &stub->args.loc, stub->args.name,
				   stub->args.xdata);
                break;
        case GF_FOP_FSETXATTR:
                stub->fn.fsetxattr (stub->frame, stub->frame->this,
				    stub->args.fd, stub->args.xattr,
				    stub->args.flags, stub->args.xdata);
                break;
        case GF_FOP_FGETXATTR:
                stub->fn.fgetxattr (stub->frame, stub->frame->this,
				    stub->args.fd, stub->args.name,
				    stub->args.xdata);
                break;
        case GF_FOP_REMOVEXATTR:
                stub->fn.removexattr (stub->frame, stub->frame->this,
				      &stub->args.loc, stub->args.name,
				      stub->args.xdata);
                break;
        case GF_FOP_FREMOVEXATTR:
                stub->fn.fremovexattr (stub->frame, stub->frame->this,
				       stub->args.fd, stub->args.name,
				       stub->args.xdata);
                break;
        case GF_FOP_OPENDIR:
                stub->fn.opendir (stub->frame, stub->frame->this,
				  &stub->args.loc, stub->args.fd,
				  stub->args.xdata);
                break;
        case GF_FOP_FSYNCDIR:
                stub->fn.fsyncdir (stub->frame, stub->frame->this,
				   stub->args.fd, stub->args.datasync,
				   stub->args.xdata);
                break;
        case GF_FOP_ACCESS:
                stub->fn.access (stub->frame, stub->frame->this,
				 &stub->args.loc, stub->args.mask,
				 stub->args.xdata);
                break;

        case GF_FOP_FTRUNCATE:
                stub->fn.ftruncate (stub->frame, stub->frame->this,
				    stub->args.fd, stub->args.offset,
				    stub->args.xdata);
                break;
        case GF_FOP_FSTAT:
                stub->fn.fstat (stub->frame, stub->frame->this,
				stub->args.fd, stub->args.xdata);
                break;
        case GF_FOP_LK:
                stub->fn.lk (stub->frame, stub->frame->this,
			     stub->args.fd, stub->args.cmd,
			     &stub->args.lock, stub->args.xdata);
                break;
        case GF_FOP_INODELK:
                stub->fn.inodelk (stub->frame, stub->frame->this,
				  stub->args.volume, &stub->args.loc,
				  stub->args.cmd, &stub->args.lock,
				  stub->args.xdata);
                break;
        case GF_FOP_FINODELK:
                stub->fn.finodelk (stub->frame, stub->frame->this,
				   stub->args.volume, stub->args.fd,
				   stub->args.cmd, &stub->args.lock,
				   stub->args.xdata);
                break;
        case GF_FOP_ENTRYLK:
                stub->fn.entrylk (stub->frame, stub->frame->this,
				  stub->args.volume, &stub->args.loc,
				  stub->args.name, stub->args.entrylkcmd,
				  stub->args.entrylktype, stub->args.xdata);
                break;
        case GF_FOP_FENTRYLK:
                stub->fn.fentrylk (stub->frame, stub->frame->this,
				   stub->args.volume, stub->args.fd,
				   stub->args.name, stub->args.entrylkcmd,
				   stub->args.entrylktype, stub->args.xdata);
		break;
        case GF_FOP_LOOKUP:
                stub->fn.lookup (stub->frame, stub->frame->this,
				 &stub->args.loc, stub->args.xdata);
                break;
        case GF_FOP_RCHECKSUM:
                stub->fn.rchecksum (stub->frame, stub->frame->this,
				    stub->args.fd, stub->args.offset,
				    stub->args.size, stub->args.xdata);
                break;
        case GF_FOP_READDIR:
                stub->fn.readdir (stub->frame, stub->frame->this,
				  stub->args.fd, stub->args.size,
				  stub->args.offset, stub->args.xdata);
                break;
        case GF_FOP_READDIRP:
                stub->fn.readdirp (stub->frame, stub->frame->this,
				   stub->args.fd, stub->args.size,
				   stub->args.offset, stub->args.xdata);
                break;
        case GF_FOP_XATTROP:
                stub->fn.xattrop (stub->frame, stub->frame->this,
				  &stub->args.loc, stub->args.optype,
				  stub->args.xattr, stub->args.xdata);
                break;
        case GF_FOP_FXATTROP:
                stub->fn.fxattrop (stub->frame, stub->frame->this,
				   stub->args.fd, stub->args.optype,
				   stub->args.xattr, stub->args.xdata);
                break;
        case GF_FOP_SETATTR:
                stub->fn.setattr (stub->frame, stub->frame->this,
				  &stub->args.loc, &stub->args.stat,
				  stub->args.valid, stub->args.xdata);
                break;
        case GF_FOP_FSETATTR:
                stub->fn.fsetattr (stub->frame, stub->frame->this,
				   stub->args.fd, &stub->args.stat,
				   stub->args.valid, stub->args.xdata);
                break;
	case GF_FOP_FALLOCATE:
		stub->fn.fallocate(stub->frame, stub->frame->this,
				   stub->args.fd, stub->args.flags,
				   stub->args.offset, stub->args.size,
				   stub->args.xdata);
		break;
	case GF_FOP_DISCARD:
		stub->fn.discard(stub->frame, stub->frame->this,
				 stub->args.fd, stub->args.offset,
				 stub->args.size, stub->args.xdata);
		break;
        case GF_FOP_ZEROFILL:
                stub->fn.zerofill(stub->frame, stub->frame->this,
                                 stub->args.fd, stub->args.offset,
                                 stub->args.size, stub->args.xdata);
                break;
        case GF_FOP_IPC:
                stub->fn.ipc (stub->frame, stub->frame->this,
                              stub->args.cmd, stub->args.xdata);
                break;
        case GF_FOP_SEEK:
                stub->fn.seek (stub->frame, stub->frame->this,
                               stub->args.fd, stub->args.offset,
                               stub->args.what, stub->args.xdata);
                break;
        case GF_FOP_LEASE:
                stub->fn.lease (stub->frame, stub->frame->this,
                                &stub->args.loc, &stub->args.lease,
                                stub->args.xdata);
                break;

        case GF_FOP_GETACTIVELK:
                stub->fn.getactivelk (stub->frame, stub->frame->this,
                                       &stub->args.loc, stub->args.xdata);

        case GF_FOP_SETACTIVELK:
                stub->fn.setactivelk (stub->frame, stub->frame->this,
                                        &stub->args.loc, &stub->args.locklist,
                                        stub->args.xdata);

        default:
                gf_msg_callingfn ("call-stub", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ENTRY, "Invalid value of FOP"
                                  " (%d)", stub->fop);
                break;
        }
out:
        return;
}


#define STUB_UNWIND(stb, fop, args ...) do {				\
	if (stb->fn_cbk.fop)						\
		stb->fn_cbk.fop (stb->frame, stb->frame->cookie,	\
				 stb->frame->this, stb->args_cbk.op_ret, \
				 stb->args_cbk.op_errno, args);		\
	else								\
		STACK_UNWIND_STRICT (fop, stb->frame, stb->args_cbk.op_ret, \
				     stb->args_cbk.op_errno, args);	\
	} while (0)


static void
call_resume_unwind (call_stub_t *stub)
{
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        switch (stub->fop) {
        case GF_FOP_OPEN:
		STUB_UNWIND (stub, open, stub->args_cbk.fd,
			     stub->args_cbk.xdata);
                break;
        case GF_FOP_CREATE:
		STUB_UNWIND (stub, create, stub->args_cbk.fd,
			     stub->args_cbk.inode, &stub->args_cbk.stat,
			     &stub->args_cbk.preparent,
			     &stub->args_cbk.postparent,
			     stub->args_cbk.xdata);
                break;
        case GF_FOP_STAT:
		STUB_UNWIND (stub, stat, &stub->args_cbk.stat,
			     stub->args_cbk.xdata);
                break;
        case GF_FOP_READLINK:
		STUB_UNWIND (stub, readlink, stub->args_cbk.buf,
			     &stub->args_cbk.stat, stub->args.xdata);
		break;
        case GF_FOP_MKNOD:
		STUB_UNWIND (stub, mknod, stub->args_cbk.inode,
			     &stub->args_cbk.stat, &stub->args_cbk.preparent,
			     &stub->args_cbk.postparent, stub->args_cbk.xdata);
		break;
        case GF_FOP_MKDIR:
		STUB_UNWIND (stub, mkdir, stub->args_cbk.inode,
			     &stub->args_cbk.stat, &stub->args_cbk.preparent,
			     &stub->args_cbk.postparent, stub->args_cbk.xdata);
		break;
        case GF_FOP_UNLINK:
		STUB_UNWIND (stub, unlink, &stub->args_cbk.preparent,
			     &stub->args_cbk.postparent, stub->args_cbk.xdata);
		break;
        case GF_FOP_RMDIR:
		STUB_UNWIND (stub, rmdir, &stub->args_cbk.preparent,
			     &stub->args_cbk.postparent, stub->args_cbk.xdata);
		break;
        case GF_FOP_SYMLINK:
		STUB_UNWIND (stub, symlink, stub->args_cbk.inode,
			     &stub->args_cbk.stat, &stub->args_cbk.preparent,
			     &stub->args_cbk.postparent, stub->args_cbk.xdata);
		break;
        case GF_FOP_RENAME:
		STUB_UNWIND (stub, rename, &stub->args_cbk.stat,
			     &stub->args_cbk.preparent,
			     &stub->args_cbk.postparent,
			     &stub->args_cbk.preparent2,
			     &stub->args_cbk.postparent2,
			     stub->args_cbk.xdata);
		break;
        case GF_FOP_LINK:
		STUB_UNWIND (stub, link, stub->args_cbk.inode,
			     &stub->args_cbk.stat, &stub->args_cbk.preparent,
			     &stub->args_cbk.postparent, stub->args_cbk.xdata);
		break;
        case GF_FOP_TRUNCATE:
		STUB_UNWIND (stub, truncate, &stub->args_cbk.prestat,
			     &stub->args_cbk.poststat, stub->args_cbk.xdata);
		break;
        case GF_FOP_READ:
		STUB_UNWIND (stub, readv, stub->args_cbk.vector,
			     stub->args_cbk.count, &stub->args_cbk.stat,
			     stub->args_cbk.iobref, stub->args_cbk.xdata);
		break;
        case GF_FOP_WRITE:
		STUB_UNWIND (stub, writev, &stub->args_cbk.prestat,
			     &stub->args_cbk.poststat, stub->args_cbk.xdata);
		break;
        case GF_FOP_STATFS:
		STUB_UNWIND (stub, statfs, &stub->args_cbk.statvfs,
			     stub->args_cbk.xdata);
		break;
        case GF_FOP_FLUSH:
		STUB_UNWIND (stub, flush, stub->args_cbk.xdata);
		break;
        case GF_FOP_FSYNC:
		STUB_UNWIND (stub, fsync, &stub->args_cbk.prestat,
			     &stub->args_cbk.poststat, stub->args_cbk.xdata);
		break;
        case GF_FOP_SETXATTR:
		STUB_UNWIND (stub, setxattr, stub->args_cbk.xdata);
		break;
        case GF_FOP_GETXATTR:
		STUB_UNWIND (stub, getxattr, stub->args_cbk.xattr,
			     stub->args_cbk.xdata);
		break;
        case GF_FOP_FSETXATTR:
		STUB_UNWIND (stub, fsetxattr, stub->args_cbk.xdata);
		break;
        case GF_FOP_FGETXATTR:
		STUB_UNWIND (stub, fgetxattr, stub->args_cbk.xattr,
			     stub->args_cbk.xdata);
		break;
        case GF_FOP_REMOVEXATTR:
		STUB_UNWIND (stub, removexattr, stub->args_cbk.xdata);
		break;
        case GF_FOP_FREMOVEXATTR:
		STUB_UNWIND (stub, fremovexattr, stub->args_cbk.xdata);
		break;
        case GF_FOP_OPENDIR:
		STUB_UNWIND (stub, opendir, stub->args_cbk.fd,
			     stub->args_cbk.xdata);
		break;
        case GF_FOP_FSYNCDIR:
		STUB_UNWIND (stub, fsyncdir, stub->args_cbk.xdata);
                break;
        case GF_FOP_ACCESS:
		STUB_UNWIND (stub, access, stub->args_cbk.xdata);
                break;
        case GF_FOP_FTRUNCATE:
		STUB_UNWIND (stub, ftruncate, &stub->args_cbk.prestat,
			     &stub->args_cbk.poststat, stub->args_cbk.xdata);
                break;
        case GF_FOP_FSTAT:
		STUB_UNWIND (stub, fstat, &stub->args_cbk.stat,
			     stub->args_cbk.xdata);
                break;
        case GF_FOP_LK:
		STUB_UNWIND (stub, lk, &stub->args_cbk.lock,
			     stub->args_cbk.xdata);
		break;
        case GF_FOP_INODELK:
		STUB_UNWIND (stub, inodelk, stub->args_cbk.xdata);
                break;
        case GF_FOP_FINODELK:
		STUB_UNWIND (stub, finodelk, stub->args_cbk.xdata);
                break;
        case GF_FOP_ENTRYLK:
		STUB_UNWIND (stub, entrylk, stub->args_cbk.xdata);
		break;
        case GF_FOP_FENTRYLK:
		STUB_UNWIND (stub, fentrylk, stub->args_cbk.xdata);
                break;
        case GF_FOP_LOOKUP:
		STUB_UNWIND (stub, lookup, stub->args_cbk.inode,
			     &stub->args_cbk.stat, stub->args_cbk.xdata,
			     &stub->args_cbk.postparent);
                break;
        case GF_FOP_RCHECKSUM:
		STUB_UNWIND (stub, rchecksum, stub->args_cbk.weak_checksum,
			     stub->args_cbk.strong_checksum, stub->args_cbk.xdata);
		break;
        case GF_FOP_READDIR:
		STUB_UNWIND (stub, readdir, &stub->args_cbk.entries,
			     stub->args_cbk.xdata);
		break;
        case GF_FOP_READDIRP:
		STUB_UNWIND (stub, readdir, &stub->args_cbk.entries,
			     stub->args_cbk.xdata);
                break;
        case GF_FOP_XATTROP:
		STUB_UNWIND (stub, xattrop, stub->args_cbk.xattr,
			     stub->args_cbk.xdata);
		break;
        case GF_FOP_FXATTROP:
		STUB_UNWIND (stub, fxattrop, stub->args_cbk.xattr,
			     stub->args_cbk.xdata);
		break;
        case GF_FOP_SETATTR:
		STUB_UNWIND (stub, setattr, &stub->args_cbk.prestat,
			     &stub->args_cbk.poststat, stub->args_cbk.xdata);
                break;
        case GF_FOP_FSETATTR:
		STUB_UNWIND (stub, fsetattr, &stub->args_cbk.prestat,
			     &stub->args_cbk.poststat, stub->args_cbk.xdata);
                break;
	case GF_FOP_FALLOCATE:
		STUB_UNWIND(stub, fallocate, &stub->args_cbk.prestat,
			    &stub->args_cbk.poststat, stub->args_cbk.xdata);
		break;
	case GF_FOP_DISCARD:
		STUB_UNWIND(stub, discard, &stub->args_cbk.prestat,
			    &stub->args_cbk.poststat, stub->args_cbk.xdata);
		break;
        case GF_FOP_ZEROFILL:
                STUB_UNWIND(stub, zerofill, &stub->args_cbk.prestat,
                            &stub->args_cbk.poststat, stub->args_cbk.xdata);
                break;
        case GF_FOP_IPC:
                STUB_UNWIND (stub, ipc, stub->args_cbk.xdata);
                break;
        case GF_FOP_SEEK:
                STUB_UNWIND (stub, seek, stub->args_cbk.offset,
                             stub->args_cbk.xdata);
                break;
        case GF_FOP_LEASE:
                STUB_UNWIND (stub, lease, &stub->args_cbk.lease,
                             stub->args_cbk.xdata);
                break;

        case GF_FOP_GETACTIVELK:
                STUB_UNWIND (stub, getactivelk, &stub->args_cbk.locklist,
                             stub->args_cbk.xdata);

        case GF_FOP_SETACTIVELK:
                STUB_UNWIND (stub, setactivelk, stub->args_cbk.xdata);

        default:
                gf_msg_callingfn ("call-stub", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ENTRY, "Invalid value of FOP"
                                  " (%d)", stub->fop);
                break;
        }
out:
        return;
}


static void
call_stub_wipe_args (call_stub_t *stub)
{
        args_wipe (&stub->args);
}

static void
call_stub_wipe_args_cbk (call_stub_t *stub)
{
        args_cbk_wipe (&stub->args_cbk);
}


void
call_stub_destroy (call_stub_t *stub)
{
        GF_VALIDATE_OR_GOTO ("call-stub", stub, out);

        if (stub->wind)
                call_stub_wipe_args (stub);
        else
                call_stub_wipe_args_cbk (stub);

        stub->stub_mem_pool = NULL;

        mem_put (stub);
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


void
call_unwind_error (call_stub_t *stub, int op_ret, int op_errno)
{
        xlator_t *old_THIS = NULL;

        list_del_init (&stub->list);

        old_THIS = THIS;
        THIS = stub->frame->this;
        {
		stub->args_cbk.op_ret = op_ret;
		stub->args_cbk.op_errno = op_errno;
		call_resume_unwind (stub);
        }
        THIS = old_THIS;

        call_stub_destroy (stub);

        return;

}


void
call_unwind_error_keep_stub (call_stub_t *stub, int op_ret, int op_errno)
{
        xlator_t *old_THIS = NULL;

        list_del_init (&stub->list);

        old_THIS = THIS;
        THIS = stub->frame->this;
        {
                stub->args_cbk.op_ret = op_ret;
                stub->args_cbk.op_errno = op_errno;
                call_resume_unwind (stub);
        }

        THIS = old_THIS;

        return;

}

void
call_resume_keep_stub (call_stub_t *stub)
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

out:
        return;
}
