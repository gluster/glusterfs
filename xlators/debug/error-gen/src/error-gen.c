/*
  Copyright (c) 2008-2010 Gluster, Inc. <http://www.gluster.com>
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

#include "xlator.h"
#include "error-gen.h"

sys_error_t error_no_list[] = {
        [GF_FOP_LOOKUP]            = { .error_no_count = 4,
                                    .error_no = {ENOENT,ENOTDIR,
                                                 ENAMETOOLONG,EAGAIN}},
        [GF_FOP_STAT]              = { .error_no_count = 7,
                                    .error_no = {EACCES,EBADF,EFAULT,
                                                 ENAMETOOLONG,ENOENT,
                                                 ENOMEM,ENOTDIR}},
        [GF_FOP_READLINK]          = { .error_no_count = 8,
                                    .error_no = {EACCES,EFAULT,EINVAL,EIO,
                                                 ENAMETOOLONG,ENOENT,ENOMEM,
                                                 ENOTDIR}},
        [GF_FOP_MKNOD]             = { .error_no_count = 11,
                                    .error_no = {EACCES,EEXIST,EFAULT,
                                                 EINVAL,ENAMETOOLONG,
                                                 ENOENT,ENOMEM,ENOSPC,
                                                 ENOTDIR,EPERM,EROFS}},
        [GF_FOP_MKDIR]             = { .error_no_count = 10,
                                    .error_no = {EACCES,EEXIST,EFAULT,
                                                 ENAMETOOLONG,ENOENT,
                                                 ENOMEM,ENOSPC,ENOTDIR,
						 EPERM,EROFS}},
        [GF_FOP_UNLINK]            = { .error_no_count = 10,
                                    .error_no = {EACCES,EBUSY,EFAULT,EIO,
                                                 EISDIR,ENAMETOOLONG,
                                                 ENOENT,ENOMEM,ENOTDIR,
                                                 EPERM,EROFS}},
        [GF_FOP_RMDIR]             = { .error_no_count = 8,
                                    .error_no = {EACCES,EBUSY,EFAULT,
                                                 ENOMEM,ENOTDIR,ENOTEMPTY,
                                                 EPERM,EROFS}},
        [GF_FOP_SYMLINK]           = { .error_no_count = 11,
                                    .error_no = {EACCES,EEXIST,EFAULT,EIO,
                                                 ENAMETOOLONG,ENOENT,ENOMEM,
                                                 ENOSPC,ENOTDIR,EPERM,
                                                 EROFS}},
        [GF_FOP_RENAME]            = { .error_no_count = 13,
                                    .error_no = {EACCES,EBUSY,EFAULT,
                                                 EINVAL,EISDIR,EMLINK,
                                                 ENAMETOOLONG,ENOENT,ENOMEM,
                                                 ENOSPC,ENOTDIR,EEXIST,
                                                 EXDEV}},
        [GF_FOP_LINK]              = { .error_no_count = 13,
                                    .error_no = {EACCES,EFAULT,EEXIST,EIO,
                                                 EMLINK,ENAMETOOLONG,
                                                 ENOENT,ENOMEM,ENOSPC,
                                                 ENOTDIR,EPERM,EROFS,
                                                 EXDEV}},
        [GF_FOP_TRUNCATE]          = { .error_no_count = 10,
                                    .error_no = {EACCES,EFAULT,EFBIG,
                                                 EINTR,EINVAL,EIO,EISDIR,
                                                 ENAMETOOLONG,ENOENT,
                                                 EISDIR}},
        [GF_FOP_CREATE]            = {.error_no_count = 10,
                                   .error_no = {EACCES,EEXIST,EFAULT,
                                                EISDIR,EMFILE,ENAMETOOLONG,
                                                ENFILE,ENODEV,ENOENT,
                                                ENODEV}},
        [GF_FOP_OPEN]              = { .error_no_count = 10,
                                    .error_no = {EACCES,EEXIST,EFAULT,
                                                 EISDIR,EMFILE,
                                                 ENAMETOOLONG,ENFILE,
                                                 ENODEV,ENOENT,ENOMEM}},
        [GF_FOP_READ]              = { .error_no_count = 5,
                                    .error_no = {EINVAL,EBADF,EFAULT,EISDIR,
                                                 ENAMETOOLONG}},
        [GF_FOP_WRITE]             = { .error_no_count = 5,
                                    .error_no = {EINVAL,EBADF,EFAULT,EISDIR,
                                                 ENAMETOOLONG}},
        [GF_FOP_STATFS]            = {.error_no_count = 10,
                                   .error_no = {EACCES,EBADF,EFAULT,EINTR,
                                                EIO,ENAMETOOLONG,ENOENT,
                                                ENOMEM,ENOSYS,ENOTDIR}},
        [GF_FOP_FLUSH]             = { .error_no_count = 5,
                                    .error_no = {EACCES,EFAULT,
                                                 ENAMETOOLONG,ENOSYS,
                                                 ENOENT}},
        [GF_FOP_FSYNC]             = { .error_no_count = 4,
                                    .error_no = {EBADF,EIO,EROFS,EINVAL}},
        [GF_FOP_SETXATTR]          = { .error_no_count = 4,
                                    .error_no = {EACCES,EBADF,EINTR,
                                                 ENAMETOOLONG}},
        [GF_FOP_GETXATTR]          = { .error_no_count = 4,
                                    .error_no = {EACCES,EBADF,ENAMETOOLONG,
                                                 EINTR}},
        [GF_FOP_REMOVEXATTR]       = { .error_no_count = 4,
                                    .error_no = {EACCES,EBADF,ENAMETOOLONG,
                                                 EINTR}},
        [GF_FOP_OPENDIR]           = { .error_no_count = 8,
                                    .error_no = {EACCES,EEXIST,EFAULT,
                                                 EISDIR,EMFILE,
                                                 ENAMETOOLONG,ENFILE,
                                                 ENODEV}},
        [GF_FOP_READDIR]           = { .error_no_count = 5,
                                    .error_no = {EINVAL,EACCES,EBADF,
                                                 EMFILE,ENOENT}},
        [GF_FOP_READDIRP]          = { .error_no_count = 5,
                                    .error_no = {EINVAL,EACCES,EBADF,
                                                 EMFILE,ENOENT}},
        [GF_FOP_FSYNCDIR]          = { .error_no_count = 4,
                                    .error_no = {EBADF,EIO,EROFS,EINVAL}},
        [GF_FOP_ACCESS]            = { .error_no_count = 8,
                                    .error_no = {EACCES,ENAMETOOLONG,
                                                 ENOENT,ENOTDIR,EROFS,
                                                 EFAULT,EINVAL,EIO}},
        [GF_FOP_FTRUNCATE]         = { .error_no_count = 9,
                                    .error_no = {EACCES,EFAULT,EFBIG,
                                                 EINTR,EINVAL,EIO,EISDIR,
                                                 ENAMETOOLONG,ENOENT}},
        [GF_FOP_FSTAT]             = { .error_no_count = 7,
                                    .error_no = {EACCES,EBADF,EFAULT,
                                                 ENAMETOOLONG,ENOENT,
                                                 ENOMEM,ENOTDIR}},
        [GF_FOP_LK]                = { .error_no_count = 4,
                                    .error_no = {EACCES,EFAULT,ENOENT,
                                                 EINTR}},
        [GF_FOP_XATTROP]           = { .error_no_count = 5,
                                    .error_no = {EACCES,EFAULT,
                                                 ENAMETOOLONG,ENOSYS,
                                                 ENOENT}},
        [GF_FOP_FXATTROP]          = { .error_no_count = 4,
                                    .error_no = {EBADF,EIO,EROFS,EINVAL}},
        [GF_FOP_INODELK]           = { .error_no_count = 4,
                                    .error_no = {EACCES,EBADF,EINTR,
                                                 ENAMETOOLONG}},
        [GF_FOP_FINODELK]          = { .error_no_count = 4,
                                    .error_no = {EACCES,EBADF,EINTR,
                                                 ENAMETOOLONG}},
        [GF_FOP_ENTRYLK]           = { .error_no_count = 4,
                                    .error_no = {EACCES,EBADF,
                                                 ENAMETOOLONG,EINTR}},
        [GF_FOP_FENTRYLK]          = { .error_no_count = 10,
                                    .error_no = {EACCES,EEXIST,EFAULT,
                                                 EISDIR,EMFILE,
                                                 ENAMETOOLONG,ENFILE,
                                                 ENODEV,ENOENT,ENOMEM}},
        [GF_FOP_SETATTR]           = {.error_no_count = 11,
                                    .error_no = {EACCES,EFAULT,EIO,
                                                 ENAMETOOLONG,ENOENT,
                                                 ENOMEM,ENOTDIR,EPERM,
                                                 EROFS,EBADF,EIO}},
        [GF_FOP_FSETATTR]          = { .error_no_count = 11,
                                    .error_no = {EACCES,EFAULT,EIO,
                                                 ENAMETOOLONG,ENOENT,
                                                 ENOMEM,ENOTDIR,EPERM,
                                                 EROFS,EBADF,EIO}},
        [GF_FOP_GETSPEC]           = { .error_no_count = 4,
                                    .error_no = {EACCES,EBADF,ENAMETOOLONG,
                                                 EINTR}}
};

int
generate_rand_no (int op_no)
{
        int             rand_no = 0;

        if (op_no < GF_FOP_MAXVALUE)
                rand_no = rand () % error_no_list[op_no].error_no_count;
        return rand_no;
}

int
conv_errno_to_int (char **error_no)
{
        if (!strcmp ((*error_no), "ENOENT"))
                return ENOENT;
        else if (!strcmp ((*error_no), "ENOTDIR"))
                return ENOTDIR;
        else if (!strcmp ((*error_no), "ENAMETOOLONG"))
                return ENAMETOOLONG;
        else if (!strcmp ((*error_no), "EACCES"))
                return EACCES;
        else if (!strcmp ((*error_no), "EBADF"))
                return EBADF;
        else if (!strcmp ((*error_no), "EFAULT"))
                return EFAULT;
        else if (!strcmp ((*error_no), "ENOMEM"))
                return ENOMEM;
        else if (!strcmp ((*error_no), "EINVAL"))
                return EINVAL;
        else if (!strcmp ((*error_no), "EIO"))
                return EIO;
        else if (!strcmp ((*error_no), "EEXIST"))
                return EEXIST;
        else if (!strcmp ((*error_no), "ENOSPC"))
                return ENOSPC;
        else if (!strcmp ((*error_no), "EPERM"))
                return EPERM;
        else if (!strcmp ((*error_no), "EROFS"))
                return EROFS;
        else if (!strcmp ((*error_no), "EBUSY"))
                return EBUSY;
        else if (!strcmp ((*error_no), "EISDIR"))
                return EISDIR;
        else if (!strcmp ((*error_no), "ENOTEMPTY"))
                return ENOTEMPTY;
        else if (!strcmp ((*error_no), "EMLINK"))
                return EMLINK;
        else if (!strcmp ((*error_no), "ENODEV"))
                return ENODEV;
        else if (!strcmp ((*error_no), "EXDEV"))
                return EXDEV;
        else if (!strcmp ((*error_no), "EMFILE"))
                return EMFILE;
        else if (!strcmp ((*error_no), "ENFILE"))
                return ENFILE;
        else if (!strcmp ((*error_no), "ENOSYS"))
                return ENOSYS;
        else if (!strcmp ((*error_no), "EINTR"))
                return EINTR;
        else if (!strcmp ((*error_no), "EFBIG"))
                return EFBIG;
        else
                return EAGAIN;
}

int
get_fop_int (char **op_no_str)
{
        if (!strcmp ((*op_no_str), "lookup"))
                return GF_FOP_LOOKUP;
        else if (!strcmp ((*op_no_str), "stat"))
                return GF_FOP_STAT;
        else if (!strcmp ((*op_no_str), "readlink"))
                return GF_FOP_READLINK;
        else if (!strcmp ((*op_no_str), "mknod"))
                return GF_FOP_MKNOD;
        else if (!strcmp ((*op_no_str), "mkdir"))
                return GF_FOP_MKDIR;
        else if (!strcmp ((*op_no_str), "unlink"))
                return GF_FOP_UNLINK;
        else if (!strcmp ((*op_no_str), "rmdir"))
                return GF_FOP_RMDIR;
        else if (!strcmp ((*op_no_str), "symlink"))
                return GF_FOP_SYMLINK;
        else if (!strcmp ((*op_no_str), "rename"))
                return GF_FOP_RENAME;
        else if (!strcmp ((*op_no_str), "link"))
                return GF_FOP_LINK;
        else if (!strcmp ((*op_no_str), "truncate"))
                return GF_FOP_TRUNCATE;
        else if (!strcmp ((*op_no_str), "create"))
                return GF_FOP_CREATE;
        else if (!strcmp ((*op_no_str), "open"))
                return GF_FOP_OPEN;
        else if (!strcmp ((*op_no_str), "readv"))
                return GF_FOP_READ;
        else if (!strcmp ((*op_no_str), "writev"))
                return GF_FOP_WRITE;
        else if (!strcmp ((*op_no_str), "statfs"))
                return GF_FOP_STATFS;
        else if (!strcmp ((*op_no_str), "flush"))
                return GF_FOP_FLUSH;
        else if (!strcmp ((*op_no_str), "fsync"))
                return GF_FOP_FSYNC;
        else if (!strcmp ((*op_no_str), "setxattr"))
                return GF_FOP_SETXATTR;
        else if (!strcmp ((*op_no_str), "getxattr"))
                return GF_FOP_GETXATTR;
        else if (!strcmp ((*op_no_str), "removexattr"))
                return GF_FOP_REMOVEXATTR;
        else if (!strcmp ((*op_no_str), "opendir"))
                return GF_FOP_OPENDIR;
        else if (!strcmp ((*op_no_str), "readdir"))
                return GF_FOP_READDIR;
        else if (!strcmp ((*op_no_str), "readdirp"))
                return GF_FOP_READDIRP;
        else if (!strcmp ((*op_no_str), "fsyncdir"))
                return GF_FOP_FSYNCDIR;
        else if (!strcmp ((*op_no_str), "access"))
                return GF_FOP_ACCESS;
        else if (!strcmp ((*op_no_str), "ftruncate"))
                return GF_FOP_FTRUNCATE;
        else if (!strcmp ((*op_no_str), "fstat"))
                return GF_FOP_FSTAT;
        else if (!strcmp ((*op_no_str), "lk"))
                return GF_FOP_LK;
        else if (!strcmp ((*op_no_str), "xattrop"))
                return GF_FOP_XATTROP;
        else if (!strcmp ((*op_no_str), "fxattrop"))
                return GF_FOP_FXATTROP;
        else if (!strcmp ((*op_no_str), "inodelk"))
                return GF_FOP_INODELK;
        else if (!strcmp ((*op_no_str), "finodelk"))
                return GF_FOP_FINODELK;
        else if (!strcmp ((*op_no_str), "etrylk"))
                return GF_FOP_ENTRYLK;
        else if (!strcmp ((*op_no_str), "fentrylk"))
                return GF_FOP_FENTRYLK;
        else if (!strcmp ((*op_no_str), "setattr"))
                return GF_FOP_SETATTR;
        else if (!strcmp ((*op_no_str), "fsetattr"))
                return GF_FOP_FSETATTR;
        else if (!strcmp ((*op_no_str), "getspec"))
                return GF_FOP_GETSPEC;
	else
                return -1;
}

int
error_gen (xlator_t *this, int op_no)
{
        eg_t             *egp = NULL;
        int              count = 0;
        int              failure_iter_no = GF_FAILURE_DEFAULT;
        char             *error_no = NULL;
        int              rand_no = 0;
        int              ret = 0;

        egp = this->private;

        LOCK (&egp->lock);
        {
                count = ++egp->op_count;
                failure_iter_no = egp->failure_iter_no;
                error_no = egp->error_no;
        }
        UNLOCK (&egp->lock);

        if((count % failure_iter_no) == 0) {
                LOCK (&egp->lock);
                {
                        egp->op_count = 0;
                }
                UNLOCK (&egp->lock);

                if (error_no)
                        ret = conv_errno_to_int (&error_no);
                else {

                        rand_no = generate_rand_no (op_no);
                        if (op_no >= GF_FOP_MAXVALUE)
                                op_no = 0;
                        if (rand_no >= error_no_list[op_no].error_no_count)
                                rand_no = 0;
                        ret = error_no_list[op_no].error_no[rand_no];
                }
                egp->failure_iter_no = 3 + (rand () % GF_UNIVERSAL_ANSWER);
        }
        return ret;
}


int
error_gen_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno, inode_t *inode,
		      struct iatt *buf, dict_t *dict, struct iatt *postparent)
{
	STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode,
                             buf, dict, postparent);
	return 0;
}


int
error_gen_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
                  dict_t *xattr_req)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_LOOKUP];

        if (enable)
                op_errno = error_gen (this, GF_FOP_LOOKUP);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (lookup, frame, -1, op_errno, NULL, NULL, NULL,
                              NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_lookup_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->lookup,
		    loc, xattr_req);
	return 0;
}


int
error_gen_forget (xlator_t *this, inode_t *inode)
{
	return 0;
}


int
error_gen_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		    int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
	STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, buf);

	return 0;
}

int
error_gen_stat (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_STAT];

        if (enable)
                op_errno = error_gen (this, GF_FOP_STAT);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (stat, frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_stat_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->stat,
		    loc);
	return 0;
}


int
error_gen_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno,
                       struct iatt *preop, struct iatt *postop)
{
	STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno, preop, postop);

	return 0;
}


int
error_gen_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                   struct iatt *stbuf, int32_t valid)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_SETATTR];

        if (enable)
                op_errno = error_gen (this, GF_FOP_SETATTR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (setattr, frame, -1, op_errno, NULL, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_setattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->setattr,
		    loc, stbuf, valid);
	return 0;
}


int
error_gen_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                    struct iatt *stbuf, int32_t valid)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_FSETATTR];

        if (enable)
                op_errno = error_gen (this, GF_FOP_FSETATTR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (fsetattr, frame, -1, op_errno, NULL, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_setattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fsetattr,
		    fd, stbuf, valid);
	return 0;
}


int
error_gen_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno,
			struct iatt *prebuf, struct iatt *postbuf)
{
	STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno,
                             prebuf, postbuf);
	return 0;
}


int
error_gen_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    off_t offset)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_TRUNCATE];

        if (enable)
                op_errno = error_gen (this, GF_FOP_TRUNCATE);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (truncate, frame, -1, op_errno,
                                     NULL, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_truncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->truncate,
		    loc, offset);
	return 0;
}


int
error_gen_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                         struct iatt *postbuf)
{
	STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno,
                             prebuf, postbuf);
	return 0;
}


int
error_gen_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd,
		     off_t offset)
{
	int              op_errno = 0;
        eg_t            *egp =NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_FTRUNCATE];

        if (enable)
                op_errno = error_gen (this, GF_FOP_FTRUNCATE);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (ftruncate, frame, -1, op_errno,
                                     NULL, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_ftruncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->ftruncate,
		    fd, offset);
	return 0;
}


int
error_gen_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno)
{
	STACK_UNWIND_STRICT (access, frame, op_ret, op_errno);

	return 0;
}


int
error_gen_access (call_frame_t *frame, xlator_t *this, loc_t *loc,
		  int32_t mask)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_ACCESS];

        if (enable)
                op_errno = error_gen (this, GF_FOP_ACCESS);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (access, frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame, error_gen_access_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->access,
		    loc, mask);
	return 0;
}


int
error_gen_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno,
			const char *path, struct iatt *sbuf)
{
	STACK_UNWIND_STRICT (readlink, frame, op_ret, op_errno, path, sbuf);
	return 0;
}


int
error_gen_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    size_t size)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_READLINK];

        if (enable)
                op_errno = error_gen (this, GF_FOP_READLINK);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (readlink, frame, -1, op_errno, NULL, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_readlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readlink,
		    loc, size);
	return 0;
}


int
error_gen_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno, inode_t *inode,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent)
{
	STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno,
                             inode, buf,
                             preparent, postparent);
	return 0;
}


int
error_gen_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc,
		 mode_t mode, dev_t rdev, dict_t *params)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_MKNOD];

        if (enable)
                op_errno = error_gen (this, GF_FOP_MKNOD);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (mknod, frame, -1, op_errno, NULL, NULL,
                                     NULL, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_mknod_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mknod,
		    loc, mode, rdev, params);
	return 0;
}


int
error_gen_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno, inode_t *inode,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent)
{
	STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno,
                             inode, buf,
                             preparent, postparent);
	return 0;
}

int
error_gen_mkdir (call_frame_t *frame, xlator_t *this,
		 loc_t *loc, mode_t mode, dict_t *params)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_MKDIR];

        if (enable)
                op_errno = error_gen (this, GF_FOP_MKDIR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (mkdir, frame, -1, op_errno, NULL, NULL,
                                     NULL, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_mkdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mkdir,
		    loc, mode, params);
	return 0;
}


int
error_gen_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno,
                      struct iatt *preparent, struct iatt *postparent)
{
	STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno,
                             preparent, postparent);
	return 0;
}


int
error_gen_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_UNLINK];

        if (enable)
                op_errno = error_gen (this, GF_FOP_UNLINK);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (unlink, frame, -1, op_errno, NULL, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_unlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->unlink,
		    loc);
	return 0;
}


int
error_gen_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno,
                     struct iatt *preparent, struct iatt *postparent)
{
	STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno,
                             preparent, postparent);
	return 0;
}


int
error_gen_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_RMDIR];

        if (enable)
                op_errno = error_gen (this, GF_FOP_RMDIR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (rmdir, frame, -1, op_errno, NULL, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_rmdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rmdir,
		    loc, flags);
	return 0;
}


int
error_gen_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno, inode_t *inode,
                       struct iatt *buf, struct iatt *preparent,
                       struct iatt *postparent)
{
	STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
	return 0;
}


int
error_gen_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
		   loc_t *loc, dict_t *params)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_SYMLINK];

        if (enable)
                op_errno = error_gen (this, GF_FOP_SYMLINK);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (symlink, frame, -1, op_errno, NULL, NULL,
                                     NULL, NULL); /* pre & post parent attr */
		return 0;
	}

	STACK_WIND (frame, error_gen_symlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->symlink,
		    linkpath, loc, params);
	return 0;
}


int
error_gen_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno, struct iatt *buf,
                      struct iatt *preoldparent, struct iatt *postoldparent,
                      struct iatt *prenewparent, struct iatt *postnewparent)
{
	STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, buf,
                             preoldparent, postoldparent,
                             prenewparent, postnewparent);
	return 0;
}


int
error_gen_rename (call_frame_t *frame, xlator_t *this,
		  loc_t *oldloc, loc_t *newloc)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_RENAME];

        if (enable)
                op_errno = error_gen (this, GF_FOP_RENAME);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (rename, frame, -1, op_errno, NULL,
                                     NULL, NULL, NULL, NULL); /* pre & post parent attr */
		return 0;
	}

	STACK_WIND (frame, error_gen_rename_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rename,
		    oldloc, newloc);
	return 0;
}


int
error_gen_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent)
{
	STACK_UNWIND_STRICT (link, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
	return 0;
}


int
error_gen_link (call_frame_t *frame, xlator_t *this,
		loc_t *oldloc, loc_t *newloc)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_LINK];

        if (enable)
                op_errno = error_gen (this, GF_FOP_LINK);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (link, frame, -1, op_errno, NULL, NULL,
                                     NULL, NULL); /* pre & post parent attr */
		return 0;
	}

	STACK_WIND (frame, error_gen_link_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->link,
		    oldloc, newloc);
	return 0;
}


int
error_gen_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno,
		      fd_t *fd, inode_t *inode, struct iatt *buf,
                      struct iatt *preparent, struct iatt *postparent)
{
	STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent);
	return 0;
}


int
error_gen_create (call_frame_t *frame, xlator_t *this, loc_t *loc,
		  int32_t flags, mode_t mode, fd_t *fd, dict_t *params)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_CREATE];

        if (enable)
                op_errno = error_gen (this, GF_FOP_CREATE);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (create, frame, -1, op_errno, NULL, NULL,
                                     NULL, NULL, NULL); /* pre & post attr */
		return 0;
	}

	STACK_WIND (frame, error_gen_create_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->create,
		    loc, flags, mode, fd, params);
	return 0;
}


int
error_gen_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		    int32_t op_ret, int32_t op_errno, fd_t *fd)
{
	STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd);
	return 0;
}


int
error_gen_open (call_frame_t *frame, xlator_t *this, loc_t *loc,
		int32_t flags, fd_t *fd, int32_t wbflags)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_OPEN];

        if (enable)
                op_errno = error_gen (this, GF_FOP_OPEN);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (open, frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_open_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->open,
		    loc, flags, fd, wbflags);
	return 0;
}


int
error_gen_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno,
		     struct iovec *vector, int32_t count,
		     struct iatt *stbuf, struct iobref *iobref)
{
	STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno,
                             vector, count, stbuf, iobref);
	return 0;
}


int
error_gen_readv (call_frame_t *frame, xlator_t *this,
		 fd_t *fd, size_t size, off_t offset)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_READ];

        if (enable)
                op_errno = error_gen (this, GF_FOP_READ);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (readv, frame, -1, op_errno, NULL, 0,
                                     NULL, NULL);
		return 0;
	}


	STACK_WIND (frame, error_gen_readv_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readv,
		    fd, size, offset);
	return 0;
}


int
error_gen_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno,
                      struct iatt *prebuf, struct iatt *postbuf)
{
	STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf);
	return 0;
}


int
error_gen_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
		  struct iovec *vector, int32_t count,
		  off_t off, struct iobref *iobref)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_WRITE];

        if (enable)
                op_errno = error_gen (this, GF_FOP_WRITE);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (writev, frame, -1, op_errno, NULL, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_writev_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->writev,
		    fd, vector, count, off, iobref);
	return 0;
}


int
error_gen_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		     int32_t op_ret, int32_t op_errno)
{
	STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno);
	return 0;
}


int
error_gen_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_FLUSH];

        if (enable)
                op_errno = error_gen (this, GF_FOP_FLUSH);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (flush, frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame, error_gen_flush_cbk,
                    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->flush,
		    fd);
	return 0;
}


int
error_gen_fsync_cbk (call_frame_t *frame, void *cookie,
		     xlator_t *this, int32_t op_ret,
		     int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf)
{
	STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, prebuf, postbuf);
	return 0;
}


int
error_gen_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_FSYNC];

        if (enable)
                op_errno = error_gen (this, GF_FOP_FSYNC);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (fsync, frame, -1, op_errno, NULL, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_fsync_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fsync,
		    fd, flags);
	return 0;
}


int
error_gen_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
	STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, buf);
	return 0;
}


int
error_gen_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_FSTAT];

        if (enable)
                op_errno = error_gen (this, GF_FOP_FSTAT);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (fstat, frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_fstat_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fstat,
		    fd);
	return 0;
}


int
error_gen_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno, fd_t *fd)
{
	STACK_UNWIND_STRICT (opendir, frame, op_ret, op_errno, fd);
	return 0;
}


int
error_gen_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_OPENDIR];

        if (enable)
                op_errno = error_gen (this, GF_FOP_OPENDIR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (opendir, frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_opendir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->opendir,
		    loc, fd);
	return 0;
}

int
error_gen_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno)
{
	STACK_UNWIND_STRICT (fsyncdir, frame, op_ret, op_errno);
	return 0;
}


int
error_gen_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
                    int32_t flags)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_FSYNCDIR];

        if (enable)
                op_errno = error_gen (this, GF_FOP_FSYNCDIR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (fsyncdir, frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame, error_gen_fsyncdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fsyncdir,
		    fd, flags);
	return 0;
}


int
error_gen_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno, struct statvfs *buf)
{
	STACK_UNWIND_STRICT (statfs, frame, op_ret, op_errno, buf);

	return 0;
}


int
error_gen_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_STATFS];

        if (enable)
                op_errno = error_gen (this, GF_FOP_STATFS);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (statfs, frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_statfs_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->statfs,
		    loc);
	return 0;
}


int
error_gen_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno)
{
	STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno);

	return 0;
}


int
error_gen_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
		    dict_t *dict, int32_t flags)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_SETXATTR];

        if (enable)
                op_errno = error_gen (this, GF_FOP_SETXATTR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (setxattr, frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame, error_gen_setxattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->setxattr,
		    loc, dict, flags);
	return 0;
}


int
error_gen_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno, dict_t *dict)
{
	STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict);
	return 0;
}


int
error_gen_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
		    const char *name)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_GETXATTR];

        if (enable)
                op_errno = error_gen (this, GF_FOP_GETXATTR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (getxattr, frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_getxattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->getxattr,
		    loc, name);
	return 0;
}


int
error_gen_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *dict)
{
	STACK_UNWIND_STRICT (xattrop, frame, op_ret, op_errno, dict);

	return 0;
}


int
error_gen_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
		   gf_xattrop_flags_t flags, dict_t *dict)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_XATTROP];

        if (enable)
                op_errno = error_gen (this, GF_FOP_XATTROP);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (xattrop, frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_xattrop_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->xattrop,
		    loc, flags, dict);
	return 0;
}


int
error_gen_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno, dict_t *dict)
{
	STACK_UNWIND_STRICT (fxattrop, frame, op_ret, op_errno, dict);

	return 0;
}


int
error_gen_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
		    gf_xattrop_flags_t flags, dict_t *dict)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_FXATTROP];

        if (enable)
                op_errno = error_gen (this, GF_FOP_FXATTROP);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (fxattrop, frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_fxattrop_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fxattrop,
		    fd, flags, dict);
	return 0;
}


int
error_gen_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			   int32_t op_ret, int32_t op_errno)
{
	STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno);

	return 0;
}


int
error_gen_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
		       const char *name)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_REMOVEXATTR];

        if (enable)
                op_errno = error_gen (this, GF_FOP_REMOVEXATTR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (removexattr, frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame, error_gen_removexattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->removexattr,
		    loc, name);
	return 0;
}


int
error_gen_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int32_t op_ret, int32_t op_errno, struct gf_flock *lock)
{
	STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno, lock);
	return 0;
}


int
error_gen_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
	      struct gf_flock *lock)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_LK];

        if (enable)
                op_errno = error_gen (this, GF_FOP_LK);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (lk, frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_lk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->lk,
		    fd, cmd, lock);
	return 0;
}


int
error_gen_inodelk_cbk (call_frame_t *frame, void *cookie,
		       xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	STACK_UNWIND_STRICT (inodelk, frame, op_ret, op_errno);
	return 0;
}


int
error_gen_inodelk (call_frame_t *frame, xlator_t *this,
		   const char *volume, loc_t *loc, int32_t cmd,
                   struct gf_flock *lock)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_INODELK];

        if (enable)
                op_errno = error_gen (this, GF_FOP_INODELK);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (inodelk, frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame, error_gen_inodelk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->inodelk,
		    volume, loc, cmd, lock);
	return 0;
}


int
error_gen_finodelk_cbk (call_frame_t *frame, void *cookie,
			xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	STACK_UNWIND_STRICT (finodelk, frame, op_ret, op_errno);
	return 0;
}


int
error_gen_finodelk (call_frame_t *frame, xlator_t *this,
		    const char *volume, fd_t *fd, int32_t cmd,
                    struct gf_flock *lock)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_FINODELK];

        if (enable)
                op_errno = error_gen (this, GF_FOP_FINODELK);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (finodelk, frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame, error_gen_finodelk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->finodelk,
		    volume, fd, cmd, lock);
	return 0;
}


int
error_gen_entrylk_cbk (call_frame_t *frame, void *cookie,
		       xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	STACK_UNWIND_STRICT (entrylk, frame, op_ret, op_errno);
	return 0;
}


int
error_gen_entrylk (call_frame_t *frame, xlator_t *this,
		   const char *volume, loc_t *loc, const char *basename,
		   entrylk_cmd cmd, entrylk_type type)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_ENTRYLK];

        if (enable)
                op_errno = error_gen (this, GF_FOP_ENTRYLK);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (entrylk, frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame, error_gen_entrylk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->entrylk,
		    volume, loc, basename, cmd, type);
	return 0;
}


int
error_gen_fentrylk_cbk (call_frame_t *frame, void *cookie,
			xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	STACK_UNWIND_STRICT (fentrylk, frame, op_ret, op_errno);
	return 0;
}


int
error_gen_fentrylk (call_frame_t *frame, xlator_t *this,
		    const char *volume, fd_t *fd, const char *basename,
		    entrylk_cmd cmd, entrylk_type type)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_FENTRYLK];

        if (enable)
                op_errno = error_gen (this, GF_FOP_FENTRYLK);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (fentrylk, frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame, error_gen_fentrylk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fentrylk,
		    volume, fd, basename, cmd, type);
	return 0;
}


/* Management operations */


int
error_gen_getspec_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno, char *spec_data)
{
	STACK_UNWIND_STRICT (getspec, frame, op_ret, op_errno, spec_data);

	return 0;
}


int
error_gen_getspec (call_frame_t *frame, xlator_t *this, const char *key,
		   int32_t flags)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_GETSPEC];

        if (enable)
                op_errno = error_gen (this, GF_FOP_GETSPEC);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (getspec, frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_getspec_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->getspec,
		    key, flags);
	return 0;
}


int
error_gen_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno, gf_dirent_t *entries)
{
	STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, entries);
	return 0;
}


int
error_gen_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
		   size_t size, off_t off)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_READDIR];

        if (enable)
                op_errno = error_gen (this, GF_FOP_READDIR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (readdir, frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_readdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readdir,
		    fd, size, off);
	return 0;
}


int
error_gen_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, gf_dirent_t *entries)
{
	STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, entries);
	return 0;
}


int
error_gen_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                    off_t off)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_READDIRP];

        if (enable)
                op_errno = error_gen (this, GF_FOP_READDIRP);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (readdirp, frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_readdirp_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdirp,
                    fd, size, off);
	return 0;
}


int
error_gen_closedir (xlator_t *this, fd_t *fd)
{
	return 0;
}


int
error_gen_close (xlator_t *this, fd_t *fd)
{
	return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_error_gen_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                        " failed");
                return ret;
        }

        return ret;
}

int
init (xlator_t *this)
{
        eg_t            *pvt = NULL;
        data_t          *error_no = NULL;
        data_t          *failure_percent = NULL;
        data_t          *enable = NULL;
        int32_t          ret = 0;
        char            *error_enable_fops = NULL;
        char            *op_no_str = NULL;
        int              op_no = -1;
        int              i = 0;
        int32_t          failure_percent_int = 0;

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "error-gen not configured with one subvolume");
                ret = -1;
                goto out;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile ");
        }

        error_no = dict_get (this->options, "error-no");
        failure_percent = dict_get (this->options, "failure");
        enable = dict_get (this->options, "enable");

        pvt = GF_CALLOC (1, sizeof (eg_t), gf_error_gen_mt_eg_t);

        if (!pvt) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory.");
                ret = -1;
                goto out;
        }

        LOCK_INIT (&pvt->lock);

        for (i = 0; i < GF_FOP_MAXVALUE; i++)
                pvt->enable[i] = 0;
        if (!error_no) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "error-no not specified.");
        } else {
                pvt->error_no = data_to_str (error_no);
        }

        if (!failure_percent) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failure percent not specified.");
                pvt->failure_iter_no = 100/GF_FAILURE_DEFAULT;
        } else {
                failure_percent_int = data_to_int32 (failure_percent);
                if (failure_percent_int)
                        pvt->failure_iter_no = 100/failure_percent_int;
                else
                        pvt->failure_iter_no = 100/GF_FAILURE_DEFAULT;
        }

        if (!enable) {
                gf_log (this->name, GF_LOG_WARNING,
                        "All fops are enabled.");
                for (i = 0; i < GF_FOP_MAXVALUE; i++)
                        pvt->enable[i] = 1;
        } else {
                error_enable_fops = data_to_str (enable);
                op_no_str = error_enable_fops;
                while ((*error_enable_fops) != '\0') {
                        error_enable_fops++;
                        if (((*error_enable_fops) == ',') ||
                            ((*error_enable_fops) == '\0')) {
                                if ((*error_enable_fops) != '\0') {
                                        (*error_enable_fops) = '\0';
                                        error_enable_fops++;
                                }
                                op_no = get_fop_int (&op_no_str);
                                if (op_no == -1) {
                                        gf_log (this->name, GF_LOG_WARNING,
                                                "Wrong option value %s",
                                                op_no_str);
                                } else
                                        pvt->enable[op_no] = 1;
                                op_no_str = error_enable_fops;
                        }
                }
        }
        this->private = pvt;

        /* Give some seed value here */
        srand (time(NULL));
out:
        return ret;
}


void
fini (xlator_t *this)
{
        eg_t            *pvt = NULL;

        if (!this)
                 return;
        pvt = this->private;

        if (pvt) {
                LOCK_DESTROY (&pvt->lock);
                GF_FREE (pvt);
                gf_log (this->name, GF_LOG_DEBUG, "fini called");
        }
        return;
}

struct xlator_fops fops = {
	.lookup      = error_gen_lookup,
	.stat        = error_gen_stat,
	.readlink    = error_gen_readlink,
	.mknod       = error_gen_mknod,
	.mkdir       = error_gen_mkdir,
	.unlink      = error_gen_unlink,
	.rmdir       = error_gen_rmdir,
	.symlink     = error_gen_symlink,
	.rename      = error_gen_rename,
	.link        = error_gen_link,
	.truncate    = error_gen_truncate,
	.create      = error_gen_create,
	.open        = error_gen_open,
	.readv       = error_gen_readv,
	.writev      = error_gen_writev,
	.statfs      = error_gen_statfs,
	.flush       = error_gen_flush,
	.fsync       = error_gen_fsync,
	.setxattr    = error_gen_setxattr,
	.getxattr    = error_gen_getxattr,
	.removexattr = error_gen_removexattr,
	.opendir     = error_gen_opendir,
	.readdir     = error_gen_readdir,
	.readdirp    = error_gen_readdirp,
	.fsyncdir    = error_gen_fsyncdir,
	.access      = error_gen_access,
	.ftruncate   = error_gen_ftruncate,
	.fstat       = error_gen_fstat,
	.lk          = error_gen_lk,
	.lookup_cbk  = error_gen_lookup_cbk,
	.xattrop     = error_gen_xattrop,
	.fxattrop    = error_gen_fxattrop,
	.inodelk     = error_gen_inodelk,
	.finodelk    = error_gen_finodelk,
	.entrylk     = error_gen_entrylk,
	.fentrylk    = error_gen_fentrylk,
        .setattr     = error_gen_setattr,
        .fsetattr    = error_gen_fsetattr,
	.getspec     = error_gen_getspec,
};

struct xlator_cbks cbks = {
	.release = error_gen_close,
	.releasedir = error_gen_closedir,
};

struct volume_options options[] = {
        { .key  = {"failure"},
          .type = GF_OPTION_TYPE_INT },
        { .key  = {"error-no"},
          .value = {"ENOENT","ENOTDIR","ENAMETOOLONG","EACCES","EBADF",
                    "EFAULT","ENOMEM","EINVAL","EIO","EEXIST","ENOSPC",
                    "EPERM","EROFS","EBUSY","EISDIR","ENOTEMPTY","EMLINK"
                    "ENODEV","EXDEV","EMFILE","ENFILE","ENOSYS","EINTR",
                    "EFBIG","EAGAIN"},
          .type = GF_OPTION_TYPE_STR },
        { .key  = {"enable"},
          .type = GF_OPTION_TYPE_STR },
        { .key  = {NULL} }
};
