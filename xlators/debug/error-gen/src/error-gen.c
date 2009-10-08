/*
  Copyright (c) 2008-2009 Gluster, Inc. <http://www.gluster.com>
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
        [ERR_LOOKUP]            = { .error_no_count = 4,
                                    .error_no = {ENOENT,ENOTDIR,
                                                 ENAMETOOLONG,EAGAIN}},
        [ERR_STAT]              = { .error_no_count = 7,
                                    .error_no = {EACCES,EBADF,EFAULT,
                                                 ENAMETOOLONG,ENOENT,
                                                 ENOMEM,ENOTDIR}},
        [ERR_READLINK]          = { .error_no_count = 8,
                                    .error_no = {EACCES,EFAULT,EINVAL,EIO,
                                                 ENAMETOOLONG,ENOENT,ENOMEM,
                                                 ENOTDIR}},
        [ERR_MKNOD]             = { .error_no_count = 11,
                                    .error_no = {EACCES,EEXIST,EFAULT,
                                                 EINVAL,ENAMETOOLONG,
                                                 ENOENT,ENOMEM,ENOSPC,
                                                 ENOTDIR,EPERM,EROFS}},
        [ERR_MKDIR]             = { .error_no_count = 10,
                                    .error_no = {EACCES,EEXIST,EFAULT,
                                                 ENAMETOOLONG,ENOENT,
                                                 ENOMEM,ENOSPC,ENOTDIR,
						 EPERM,EROFS}},
        [ERR_UNLINK]            = { .error_no_count = 10,
                                    .error_no = {EACCES,EBUSY,EFAULT,EIO,
                                                 EISDIR,ENAMETOOLONG,
                                                 ENOENT,ENOMEM,ENOTDIR,
                                                 EPERM,EROFS}},
        [ERR_RMDIR]             = { .error_no_count = 8,
                                    .error_no = {EACCES,EBUSY,EFAULT,
                                                 ENOMEM,ENOTDIR,ENOTEMPTY,
                                                 EPERM,EROFS}},
        [ERR_SYMLINK]           = { .error_no_count = 11,
                                    .error_no = {EACCES,EEXIST,EFAULT,EIO,
                                                 ENAMETOOLONG,ENOENT,ENOMEM,
                                                 ENOSPC,ENOTDIR,EPERM,
                                                 EROFS}},
        [ERR_RENAME]            = { .error_no_count = 13,
                                    .error_no = {EACCES,EBUSY,EFAULT,
                                                 EINVAL,EISDIR,EMLINK,
                                                 ENAMETOOLONG,ENOENT,ENOMEM,
                                                 ENOSPC,ENOTDIR,EEXIST,
                                                 EXDEV}},
        [ERR_LINK]              = { .error_no_count = 13,
                                    .error_no = {EACCES,EFAULT,EEXIST,EIO,
                                                 EMLINK,ENAMETOOLONG,
                                                 ENOENT,ENOMEM,ENOSPC,
                                                 ENOTDIR,EPERM,EROFS,
                                                 EXDEV}},
        [ERR_TRUNCATE]          = { .error_no_count = 10,
                                    .error_no = {EACCES,EFAULT,EFBIG,
                                                 EINTR,EINVAL,EIO,EISDIR,
                                                 ENAMETOOLONG,ENOENT,
                                                 EISDIR}},
        [ERR_CREATE]            = {.error_no_count = 10,
                                   .error_no = {EACCES,EEXIST,EFAULT,
                                                EISDIR,EMFILE,ENAMETOOLONG,
                                                ENFILE,ENODEV,ENOENT,
                                                ENODEV}},
        [ERR_OPEN]              = { .error_no_count = 10,
                                    .error_no = {EACCES,EEXIST,EFAULT,
                                                 EISDIR,EMFILE,
                                                 ENAMETOOLONG,ENFILE,
                                                 ENODEV,ENOENT,ENOMEM}},
        [ERR_READV]             = { .error_no_count = 5,
                                    .error_no = {EINVAL,EBADF,EFAULT,EISDIR,
                                                 ENAMETOOLONG}},
        [ERR_WRITEV]            = { .error_no_count = 5,
                                    .error_no = {EINVAL,EBADF,EFAULT,EISDIR,
                                                 ENAMETOOLONG}},
        [ERR_STATFS]            = {.error_no_count = 10,
                                   .error_no = {EACCES,EBADF,EFAULT,EINTR,
                                                EIO,ENAMETOOLONG,ENOENT,
                                                ENOMEM,ENOSYS,ENOTDIR}},
        [ERR_FLUSH]             = { .error_no_count = 5,
                                    .error_no = {EACCES,EFAULT,
                                                 ENAMETOOLONG,ENOSYS,
                                                 ENOENT}},
        [ERR_FSYNC]             = { .error_no_count = 4,
                                    .error_no = {EBADF,EIO,EROFS,EINVAL}},
        [ERR_SETXATTR]          = { .error_no_count = 4,
                                    .error_no = {EACCES,EBADF,EINTR,
                                                 ENAMETOOLONG}},
        [ERR_GETXATTR]          = { .error_no_count = 4,
                                    .error_no = {EACCES,EBADF,ENAMETOOLONG,
                                                 EINTR}},
        [ERR_REMOVEXATTR]       = { .error_no_count = 4,
                                    .error_no = {EACCES,EBADF,ENAMETOOLONG,
                                                 EINTR}},
        [ERR_OPENDIR]           = { .error_no_count = 8,
                                    .error_no = {EACCES,EEXIST,EFAULT,
                                                 EISDIR,EMFILE,
                                                 ENAMETOOLONG,ENFILE,
                                                 ENODEV}},
        [ERR_READDIR]           = { .error_no_count = 5,
                                    .error_no = {EINVAL,EACCES,EBADF,
                                                 EMFILE,ENOENT}},
        [ERR_READDIRP]          = { .error_no_count = 5,
                                    .error_no = {EINVAL,EACCES,EBADF,
                                                 EMFILE,ENOENT}},
        [ERR_GETDENTS]          = { .error_no_count = 5,
                                    .error_no = {EBADF,EFAULT,EINVAL,
                                                 ENOENT,ENOTDIR}},
        [ERR_FSYNCDIR]          = { .error_no_count = 4,
                                    .error_no = {EBADF,EIO,EROFS,EINVAL}},
        [ERR_ACCESS]            = { .error_no_count = 8,
                                    .error_no = {EACCES,ENAMETOOLONG,
                                                 ENOENT,ENOTDIR,EROFS,
                                                 EFAULT,EINVAL,EIO}},
        [ERR_FTRUNCATE]         = { .error_no_count = 9,
                                    .error_no = {EACCES,EFAULT,EFBIG,
                                                 EINTR,EINVAL,EIO,EISDIR,
                                                 ENAMETOOLONG,ENOENT}},
        [ERR_FSTAT]             = { .error_no_count = 7,
                                    .error_no = {EACCES,EBADF,EFAULT,
                                                 ENAMETOOLONG,ENOENT,
                                                 ENOMEM,ENOTDIR}},
        [ERR_LK]                = { .error_no_count = 4,
                                    .error_no = {EACCES,EFAULT,ENOENT,
                                                 EINTR}},
        [ERR_SETDENTS]          = { .error_no_count = 4,
                                    .error_no = {EACCES,EBADF,EINTR,
                                                 ENAMETOOLONG}},
        [ERR_CHECKSUM]          = { .error_no_count = 4,
                                    .error_no = {EACCES,EBADF,
                                                 ENAMETOOLONG,EINTR}},
        [ERR_XATTROP]           = { .error_no_count = 5,
                                    .error_no = {EACCES,EFAULT,
                                                 ENAMETOOLONG,ENOSYS,
                                                 ENOENT}},
        [ERR_FXATTROP]          = { .error_no_count = 4,
                                    .error_no = {EBADF,EIO,EROFS,EINVAL}},
        [ERR_INODELK]           = { .error_no_count = 4,
                                    .error_no = {EACCES,EBADF,EINTR,
                                                 ENAMETOOLONG}},
        [ERR_FINODELK]          = { .error_no_count = 4,
                                    .error_no = {EACCES,EBADF,EINTR,
                                                 ENAMETOOLONG}},
        [ERR_ENTRYLK]           = { .error_no_count = 4,
                                    .error_no = {EACCES,EBADF,
                                                 ENAMETOOLONG,EINTR}},
        [ERR_FENTRYLK]          = { .error_no_count = 10,
                                    .error_no = {EACCES,EEXIST,EFAULT,
                                                 EISDIR,EMFILE,
                                                 ENAMETOOLONG,ENFILE,
                                                 ENODEV,ENOENT,ENOMEM}},
        [ERR_SETATTR]           = {.error_no_count = 11,
                                    .error_no = {EACCES,EFAULT,EIO,
                                                 ENAMETOOLONG,ENOENT,
                                                 ENOMEM,ENOTDIR,EPERM,
                                                 EROFS,EBADF,EIO}},
        [ERR_FSETATTR]          = { .error_no_count = 11,
                                    .error_no = {EACCES,EFAULT,EIO,
                                                 ENAMETOOLONG,ENOENT,
                                                 ENOMEM,ENOTDIR,EPERM,
                                                 EROFS,EBADF,EIO}},
        [ERR_STATS]             = { .error_no_count = 4,
                                    .error_no = {EACCES,EBADF,ENAMETOOLONG,
                                                 EINTR}},
        [ERR_GETSPEC]           = { .error_no_count = 4,
                                    .error_no = {EACCES,EBADF,ENAMETOOLONG,
                                                 EINTR}}
};

int
generate_rand_no (int op_no)
{
        int             rand_no = 0;

        if (op_no < NO_OF_FOPS)
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
                return ERR_LOOKUP;
        else if (!strcmp ((*op_no_str), "stat"))
                return ERR_STAT;
        else if (!strcmp ((*op_no_str), "readlink"))
                return ERR_READLINK;
        else if (!strcmp ((*op_no_str), "mknod"))
                return ERR_MKNOD;
        else if (!strcmp ((*op_no_str), "mkdir"))
                return ERR_MKDIR;
        else if (!strcmp ((*op_no_str), "unlink"))
                return ERR_UNLINK;
        else if (!strcmp ((*op_no_str), "rmdir"))
                return ERR_RMDIR;
        else if (!strcmp ((*op_no_str), "symlink"))
                return ERR_SYMLINK;
        else if (!strcmp ((*op_no_str), "rename"))
                return ERR_RENAME;
        else if (!strcmp ((*op_no_str), "link"))
                return ERR_LINK;
        else if (!strcmp ((*op_no_str), "truncate"))
                return ERR_TRUNCATE;
        else if (!strcmp ((*op_no_str), "create"))
                return ERR_CREATE;
        else if (!strcmp ((*op_no_str), "open"))
                return ERR_OPEN;
        else if (!strcmp ((*op_no_str), "readv"))
                return ERR_READV;
        else if (!strcmp ((*op_no_str), "writev"))
                return ERR_WRITEV;
        else if (!strcmp ((*op_no_str), "statfs"))
                return ERR_STATFS;
        else if (!strcmp ((*op_no_str), "flush"))
                return ERR_FLUSH;
        else if (!strcmp ((*op_no_str), "fsync"))
                return ERR_FSYNC;
        else if (!strcmp ((*op_no_str), "setxattr"))
                return ERR_SETXATTR;
        else if (!strcmp ((*op_no_str), "getxattr"))
                return ERR_GETXATTR;
        else if (!strcmp ((*op_no_str), "removexattr"))
                return ERR_REMOVEXATTR;
        else if (!strcmp ((*op_no_str), "opendir"))
                return ERR_OPENDIR;
        else if (!strcmp ((*op_no_str), "readdir"))
                return ERR_READDIR;
        else if (!strcmp ((*op_no_str), "readdirp"))
                return ERR_READDIRP;
	else if (!strcmp ((*op_no_str), "getdents"))
                return ERR_GETDENTS;
        else if (!strcmp ((*op_no_str), "fsyncdir"))
                return ERR_FSYNCDIR;
        else if (!strcmp ((*op_no_str), "access"))
                return ERR_ACCESS;
        else if (!strcmp ((*op_no_str), "ftruncate"))
                return ERR_FTRUNCATE;
        else if (!strcmp ((*op_no_str), "fstat"))
                return ERR_FSTAT;
        else if (!strcmp ((*op_no_str), "lk"))
                return ERR_LK;
        else if (!strcmp ((*op_no_str), "setdents"))
                return ERR_SETDENTS;
        else if (!strcmp ((*op_no_str), "checksum"))
                return ERR_CHECKSUM;
        else if (!strcmp ((*op_no_str), "xattrop"))
                return ERR_XATTROP;
        else if (!strcmp ((*op_no_str), "fxattrop"))
                return ERR_FXATTROP;
        else if (!strcmp ((*op_no_str), "inodelk"))
                return ERR_INODELK;
        else if (!strcmp ((*op_no_str), "finodelk"))
                return ERR_FINODELK;
        else if (!strcmp ((*op_no_str), "etrylk"))
                return ERR_ENTRYLK;
        else if (!strcmp ((*op_no_str), "fentrylk"))
                return ERR_FENTRYLK;
        else if (!strcmp ((*op_no_str), "setattr"))
                return ERR_SETATTR;
        else if (!strcmp ((*op_no_str), "fsetattr"))
                return ERR_FSETATTR;
        else if (!strcmp ((*op_no_str), "stats"))
                return ERR_STATS;
        else if (!strcmp ((*op_no_str), "getspec"))
                return ERR_GETSPEC;
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
                        if (op_no >= NO_OF_FOPS)
                                op_no = 0;
                        if (rand_no >= error_no_list[op_no].error_no_count)
                                rand_no = 0;
                        ret = error_no_list[op_no].error_no[rand_no];
                }
        }
        return ret;
}

static int32_t
error_gen_lookup_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      inode_t *inode,
		      struct stat *buf,
		      dict_t *dict,
                      struct stat *postparent)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      inode,
		      buf,
		      dict, postparent);
	return 0;
}

int32_t
error_gen_lookup (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  dict_t *xattr_req)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_LOOKUP];

        if (enable)
                op_errno = error_gen (this, ERR_LOOKUP);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL, NULL,
                              NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_lookup_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->lookup,
		    loc,
		    xattr_req);
	return 0;
}

int32_t
error_gen_forget (xlator_t *this,
		  inode_t *inode)
{
	return 0;
}

int32_t
error_gen_stat_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t
error_gen_stat (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_STAT];

        if (enable)
                op_errno = error_gen (this, ERR_STAT);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_stat_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->stat,
		    loc);
	return 0;
}

int32_t
error_gen_setattr_cbk (call_frame_t *frame,
                       void *cookie,
                       xlator_t *this,
                       int32_t op_ret,
                       int32_t op_errno,
                       struct stat *preop,
                       struct stat *postop)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      preop, postop);
	return 0;
}

int32_t
error_gen_setattr (call_frame_t *frame,
                   xlator_t *this,
                   loc_t *loc,
                   struct stat *stbuf,
                   int32_t valid)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_SETATTR];

        if (enable)
                op_errno = error_gen (this, ERR_SETATTR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_setattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->setattr,
		    loc,
		    stbuf, valid);
	return 0;
}

int32_t
error_gen_fsetattr (call_frame_t *frame,
                    xlator_t *this,
                    fd_t *fd,
                    struct stat *stbuf,
                    int32_t valid)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_FSETATTR];

        if (enable)
                op_errno = error_gen (this, ERR_FSETATTR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_setattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fsetattr,
		    fd,
		    stbuf, valid);
	return 0;
}

int32_t
error_gen_truncate_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			struct stat *prebuf,
                        struct stat *postbuf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      prebuf,
                      postbuf);
	return 0;
}

int32_t
error_gen_truncate (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc,
		    off_t offset)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_TRUNCATE];

        if (enable)
                op_errno = error_gen (this, ERR_TRUNCATE);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL,
                              NULL, NULL,  /* pre & post old parent attr */
                              NULL, NULL); /* pre & post new parent attr */
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_truncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->truncate,
		    loc,
		    offset);
	return 0;
}

int32_t
error_gen_ftruncate_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct stat *prebuf,
                         struct stat *postbuf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      prebuf,
                      postbuf);
	return 0;
}

int32_t
error_gen_ftruncate (call_frame_t *frame,
		     xlator_t *this,
		     fd_t *fd,
		     off_t offset)
{
	int             op_errno = 0;
        eg_t            *egp =NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_FTRUNCATE];

        if (enable)
                op_errno = error_gen (this, ERR_FTRUNCATE);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_ftruncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->ftruncate,
		    fd,
		    offset);
	return 0;
}

int32_t
error_gen_access_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}

int32_t
error_gen_access (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  int32_t mask)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_ACCESS];

        if (enable)
                op_errno = error_gen (this, ERR_ACCESS);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_access_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->access,
		    loc,
		    mask);
	return 0;
}

int32_t
error_gen_readlink_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			const char *path,
                        struct stat *sbuf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      path,
                      sbuf);
	return 0;
}

int32_t
error_gen_readlink (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc,
		    size_t size)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_READLINK];

        if (enable)
                op_errno = error_gen (this, ERR_READLINK);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_readlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readlink,
		    loc,
		    size);
	return 0;
}

int32_t
error_gen_mknod_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     inode_t *inode,
                     struct stat *buf,
                     struct stat *preparent,
                     struct stat *postparent)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      inode,
		      buf,
                      preparent, postparent);
	return 0;
}

int32_t
error_gen_mknod (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 mode_t mode,
		 dev_t rdev)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_MKNOD];

        if (enable)
                op_errno = error_gen (this, ERR_MKNOD);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_mknod_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mknod,
		    loc, mode, rdev);
	return 0;
}

int32_t
error_gen_mkdir_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     inode_t *inode,
                     struct stat *buf,
                     struct stat *preparent,
                     struct stat *postparent)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      inode,
		      buf,
                      preparent, postparent);
	return 0;
}

int32_t
error_gen_mkdir (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 mode_t mode)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_MKDIR];

        if (enable)
                op_errno = error_gen (this, ERR_MKDIR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_mkdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mkdir,
		    loc, mode);
	return 0;
}

int32_t
error_gen_unlink_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
                      struct stat *preparent,
                      struct stat *postparent)
{
	STACK_UNWIND (frame, op_ret, op_errno, preparent, postparent);
	return 0;
}

int32_t
error_gen_unlink (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_UNLINK];

        if (enable)
                op_errno = error_gen (this, ERR_UNLINK);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_unlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->unlink,
		    loc);
	return 0;
}

int32_t
error_gen_rmdir_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
                     struct stat *preparent,
                     struct stat *postparent)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
                      preparent, postparent);
	return 0;
}

int32_t
error_gen_rmdir (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_RMDIR];

        if (enable)
                op_errno = error_gen (this, ERR_RMDIR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL,
                              NULL, NULL); /* pre & post parent attr */
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_rmdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rmdir,
		    loc);
	return 0;
}

int32_t
error_gen_symlink_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       inode_t *inode,
                       struct stat *buf,
                       struct stat *preparent,
                       struct stat *postparent)
{
	STACK_UNWIND (frame, op_ret, op_errno, inode, buf,
                      preparent, postparent);
	return 0;
}

int32_t
error_gen_symlink (call_frame_t *frame,
		   xlator_t *this,
		   const char *linkpath,
		   loc_t *loc)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_SYMLINK];

        if (enable)
                op_errno = error_gen (this, ERR_SYMLINK);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL,
                              NULL, NULL); /* pre & post parent attr */
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_symlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->symlink,
		    linkpath, loc);
	return 0;
}

int32_t
error_gen_rename_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct stat *buf,
                      struct stat *preoldparent,
                      struct stat *postoldparent,
                      struct stat *prenewparent,
                      struct stat *postnewparent)
{
	STACK_UNWIND (frame, op_ret, op_errno, buf,
                      preoldparent, postoldparent,
                      prenewparent, postnewparent);
	return 0;
}

int32_t
error_gen_rename (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *oldloc,
		  loc_t *newloc)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_RENAME];

        if (enable)
                op_errno = error_gen (this, ERR_RENAME);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL,
                              NULL, NULL); /* pre & post parent attr */
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_rename_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rename,
		    oldloc, newloc);
	return 0;
}

int32_t
error_gen_link_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
                    struct stat *buf,
                    struct stat *preparent,
                    struct stat *postparent)
{
	STACK_UNWIND (frame, op_ret, op_errno, inode, buf,
                      preparent, postparent);
	return 0;
}

int32_t
error_gen_link (call_frame_t *frame,
		xlator_t *this,
		loc_t *oldloc,
		loc_t *newloc)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_LINK];

        if (enable)
                op_errno = error_gen (this, ERR_LINK);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL,
                              NULL, NULL); /* pre & post parent attr */
		return 0;
	}
	STACK_WIND (frame,
		    error_gen_link_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->link,
		    oldloc, newloc);
	return 0;
}

int32_t
error_gen_create_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      fd_t *fd,
		      inode_t *inode,
		      struct stat *buf,
                      struct stat *preparent,
                      struct stat *postparent)
{
	STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf,
                      preparent, postparent);
	return 0;
}

int32_t
error_gen_create (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  int32_t flags,
		  mode_t mode, fd_t *fd)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_CREATE];

        if (enable)
                op_errno = error_gen (this, ERR_CREATE);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL, NULL,
                              NULL, NULL); /* pre & post attr */
		return 0;
	}

	STACK_WIND (frame, error_gen_create_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->create,
		    loc, flags, mode, fd);
	return 0;
}

int32_t
error_gen_open_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    fd_t *fd)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      fd);
	return 0;
}

int32_t
error_gen_open (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		int32_t flags, fd_t *fd, int32_t wbflags)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_OPEN];

        if (enable)
                op_errno = error_gen (this, ERR_OPEN);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_open_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->open,
		    loc, flags, fd, wbflags);
	return 0;
}

int32_t
error_gen_readv_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct iovec *vector,
		     int32_t count,
		     struct stat *stbuf,
                     struct iobref *iobref)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      vector,
		      count,
		      stbuf,
                      iobref);
	return 0;
}

int32_t
error_gen_readv (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 size_t size,
		 off_t offset)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_READV];

        if (enable)
                op_errno = error_gen (this, ERR_READV);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, 0, NULL);
		return 0;
	}


	STACK_WIND (frame,
		    error_gen_readv_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readv,
		    fd,
		    size,
		    offset);
	return 0;
}

int32_t
error_gen_writev_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
                      struct stat *prebuf,
		      struct stat *postbuf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
                      prebuf,
		      postbuf);
	return 0;
}

int32_t
error_gen_writev (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  struct iovec *vector,
		  int32_t count,
		  off_t off,
                  struct iobref *iobref)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_WRITEV];

        if (enable)
                op_errno = error_gen (this, ERR_WRITEV);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}


	STACK_WIND (frame,
		    error_gen_writev_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->writev,
		    fd,
		    vector,
		    count,
		    off,
                    iobref);
	return 0;
}

int32_t
error_gen_flush_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}

int32_t
error_gen_flush (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_FLUSH];

        if (enable)
                op_errno = error_gen (this, ERR_FLUSH);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_flush_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->flush,
		    fd);
	return 0;
}

int32_t
error_gen_fsync_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
                     struct stat *prebuf,
                     struct stat *postbuf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}

int32_t
error_gen_fsync (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 int32_t flags)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_FSYNC];

        if (enable)
                op_errno = error_gen (this, ERR_FSYNC);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_fsync_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fsync,
		    fd,
		    flags);
	return 0;
}

int32_t
error_gen_fstat_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t
error_gen_fstat (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_FSTAT];

        if (enable)
                op_errno = error_gen (this, ERR_FSTAT);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_fstat_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fstat,
		    fd);
	return 0;
}

int32_t
error_gen_opendir_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       fd_t *fd)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      fd);
	return 0;
}

int32_t
error_gen_opendir (call_frame_t *frame,
		   xlator_t *this,
		   loc_t *loc, fd_t *fd)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_OPENDIR];

        if (enable)
                op_errno = error_gen (this, ERR_OPENDIR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_opendir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->opendir,
		    loc, fd);
	return 0;
}

int32_t
error_gen_getdents_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			dir_entry_t *entries,
			int32_t count)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      entries,
		      count);
	return 0;
}

int32_t
error_gen_getdents (call_frame_t *frame,
		    xlator_t *this,
		    fd_t *fd,
		    size_t size,
		    off_t offset,
		    int32_t flag)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_GETDENTS];

        if (enable)
                op_errno = error_gen (this, ERR_GETDENTS);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, 0);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_getdents_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->getdents,
		    fd,
		    size,
		    offset,
		    flag);
	return 0;
}

int32_t
error_gen_setdents_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}

int32_t
error_gen_setdents (call_frame_t *frame,
		    xlator_t *this,
		    fd_t *fd,
		    int32_t flags,
		    dir_entry_t *entries,
		    int32_t count)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_SETDENTS];

        if (enable)
                op_errno = error_gen (this, ERR_SETDENTS);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, 0);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_setdents_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->setdents,
		    fd,
		    flags,
		    entries,
		    count);
	return 0;
}

int32_t
error_gen_fsyncdir_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}

int32_t
error_gen_fsyncdir (call_frame_t *frame,
		    xlator_t *this,
		    fd_t *fd,
		    int32_t flags)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_FSYNCDIR];

        if (enable)
                op_errno = error_gen (this, ERR_FSYNCDIR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_fsyncdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fsyncdir,
		    fd,
		    flags);
	return 0;
}

int32_t
error_gen_statfs_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct statvfs *buf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t
error_gen_statfs (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_STATFS];

        if (enable)
                op_errno = error_gen (this, ERR_STATFS);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_statfs_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->statfs,
		    loc);
	return 0;
}

int32_t
error_gen_setxattr_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}

int32_t
error_gen_setxattr (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc,
		    dict_t *dict,
		    int32_t flags)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_SETXATTR];

        if (enable)
                op_errno = error_gen (this, ERR_SETXATTR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_setxattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->setxattr,
		    loc,
		    dict,
		    flags);
	return 0;
}

int32_t
error_gen_getxattr_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			dict_t *dict)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      dict);
	return 0;
}

int32_t
error_gen_getxattr (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc,
		    const char *name)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_GETXATTR];

        if (enable)
                op_errno = error_gen (this, ERR_GETXATTR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_getxattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->getxattr,
		    loc,
		    name);
	return 0;
}

int32_t
error_gen_xattrop_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       dict_t *dict)
{
	STACK_UNWIND (frame, op_ret, op_errno, dict);
	return 0;
}

int32_t
error_gen_xattrop (call_frame_t *frame,
		   xlator_t *this,
		   loc_t *loc,
		   gf_xattrop_flags_t flags,
		   dict_t *dict)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_XATTROP];

        if (enable)
                op_errno = error_gen (this, ERR_XATTROP);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_xattrop_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->xattrop,
		    loc, flags, dict);
	return 0;
}

int32_t
error_gen_fxattrop_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			dict_t *dict)
{
	STACK_UNWIND (frame, op_ret, op_errno, dict);
	return 0;
}

int32_t
error_gen_fxattrop (call_frame_t *frame,
		    xlator_t *this,
		    fd_t *fd,
		    gf_xattrop_flags_t flags,
		    dict_t *dict)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_FXATTROP];

        if (enable)
                op_errno = error_gen (this, ERR_FXATTROP);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_fxattrop_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fxattrop,
		    fd, flags, dict);
	return 0;
}

int32_t
error_gen_removexattr_cbk (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}

int32_t
error_gen_removexattr (call_frame_t *frame,
		       xlator_t *this,
		       loc_t *loc,
		       const char *name)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_REMOVEXATTR];

        if (enable)
                op_errno = error_gen (this, ERR_REMOVEXATTR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_removexattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->removexattr,
		    loc,
		    name);
	return 0;
}

int32_t
error_gen_lk_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct flock *lock)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      lock);
	return 0;
}

int32_t
error_gen_lk (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      int32_t cmd,
	      struct flock *lock)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_LK];

        if (enable)
                op_errno = error_gen (this, ERR_LK);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_lk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->lk,
		    fd,
		    cmd,
		    lock);
	return 0;
}

int32_t
error_gen_inodelk_cbk (call_frame_t *frame, void *cookie,
		       xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t
error_gen_inodelk (call_frame_t *frame, xlator_t *this,
		   const char *volume, loc_t *loc, int32_t cmd,
                   struct flock *lock)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_INODELK];

        if (enable)
                op_errno = error_gen (this, ERR_INODELK);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_inodelk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->inodelk,
		    volume, loc, cmd, lock);
	return 0;
}

int32_t
error_gen_finodelk_cbk (call_frame_t *frame, void *cookie,
			xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t
error_gen_finodelk (call_frame_t *frame, xlator_t *this,
		    const char *volume, fd_t *fd, int32_t cmd,
                    struct flock *lock)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_FINODELK];

        if (enable)
                op_errno = error_gen (this, ERR_FINODELK);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_finodelk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->finodelk,
		    volume, fd, cmd, lock);
	return 0;
}

int32_t
error_gen_entrylk_cbk (call_frame_t *frame, void *cookie,
		       xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t
error_gen_entrylk (call_frame_t *frame, xlator_t *this,
		   const char *volume, loc_t *loc, const char *basename,
		   entrylk_cmd cmd, entrylk_type type)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_ENTRYLK];

        if (enable)
                op_errno = error_gen (this, ERR_ENTRYLK);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame, error_gen_entrylk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->entrylk,
		    volume, loc, basename, cmd, type);
	return 0;
}

int32_t
error_gen_fentrylk_cbk (call_frame_t *frame, void *cookie,
			xlator_t *this, int32_t op_ret, int32_t op_errno)

{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t
error_gen_fentrylk (call_frame_t *frame, xlator_t *this,
		    const char *volume, fd_t *fd, const char *basename,
		    entrylk_cmd cmd, entrylk_type type)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_FENTRYLK];

        if (enable)
                op_errno = error_gen (this, ERR_FENTRYLK);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno);
		return 0;
	}

	STACK_WIND (frame, error_gen_fentrylk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fentrylk,
		    volume, fd, basename, cmd, type);
	return 0;
}


/* Management operations */

int32_t
error_gen_stats_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct xlator_stats *stats)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      stats);
	return 0;
}

int32_t
error_gen_stats (call_frame_t *frame,
		 xlator_t *this,
		 int32_t flags)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_STATS];

        if (enable)
                op_errno = error_gen (this, ERR_STATS);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_stats_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->mops->stats,
		    flags);
	return 0;
}

int32_t
error_gen_getspec_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       char *spec_data)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      spec_data);
	return 0;
}

int32_t
error_gen_getspec (call_frame_t *frame,
		   xlator_t *this,
		   const char *key,
		   int32_t flags)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_GETSPEC];

        if (enable)
                op_errno = error_gen (this, ERR_GETSPEC);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_getspec_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->mops->getspec,
		    key, flags);
	return 0;
}

int32_t
error_gen_checksum_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			uint8_t *file_checksum,
			uint8_t *dir_checksum)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      file_checksum,
		      dir_checksum);
	return 0;
}

int32_t
error_gen_checksum (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc,
		    int32_t flag)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_CHECKSUM];

        if (enable)
                op_errno = error_gen (this, ERR_CHECKSUM);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_checksum_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->checksum,
		    loc,
		    flag);
	return 0;
}

int32_t
error_gen_readdir_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       gf_dirent_t *entries)
{
	STACK_UNWIND (frame, op_ret, op_errno, entries);
	return 0;
}

int32_t
error_gen_readdir (call_frame_t *frame,
		   xlator_t *this,
		   fd_t *fd,
		   size_t size,
		   off_t off)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_READDIR];

        if (enable)
                op_errno = error_gen (this, ERR_READDIR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    error_gen_readdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readdir,
		    fd, size, off);
	return 0;
}

int32_t
error_gen_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, gf_dirent_t *entries)
{
	STACK_UNWIND (frame, op_ret, op_errno, entries);
	return 0;
}


int32_t
error_gen_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                    off_t off)
{
	int             op_errno = 0;
        eg_t            *egp = NULL;
        int             enable = 1;

        egp = this->private;
        enable = egp->enable[ERR_READDIRP];

        if (enable)
                op_errno = error_gen (this, ERR_READDIRP);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND (frame, -1, op_errno, NULL);
		return 0;
	}

	STACK_WIND (frame, error_gen_readdirp_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdirp, fd, size, off);
	return 0;
}

int32_t
error_gen_closedir (xlator_t *this,
		    fd_t *fd)
{
	return 0;
}

int32_t
error_gen_close (xlator_t *this,
		 fd_t *fd)
{
	return 0;
}

int
init (xlator_t *this)
{
        eg_t            *pvt = NULL;
        data_t          *error_no = NULL;
        data_t          *failure_percent = NULL;
        data_t          *enable = NULL;
        int32_t         ret = 0;
        char            *error_enable_fops = NULL;
        char            *op_no_str = NULL;
        int             op_no = -1;
        int             i = 0;
        int32_t         failure_percent_int = 0;

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

        pvt = CALLOC (1, sizeof (eg_t));

        if (!pvt) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory.");
                ret = -1;
                goto out;
        }

        LOCK_INIT (&pvt->lock);

        for (i = 0; i < NO_OF_FOPS; i++)
                pvt->enable[i] = 0;
        if (!error_no) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Warning error-no not specified.");
        } else {
                pvt->error_no = data_to_str (error_no);
        }

        if (!failure_percent) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Warning, failure percent not specified.");
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
                        "Warning, all fops are enabled.");
                for (i = 0; i < NO_OF_FOPS; i++)
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
                FREE (pvt);
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
	.getdents    = error_gen_getdents,
	.fsyncdir    = error_gen_fsyncdir,
	.access      = error_gen_access,
	.ftruncate   = error_gen_ftruncate,
	.fstat       = error_gen_fstat,
	.lk          = error_gen_lk,
	.setdents    = error_gen_setdents,
	.lookup_cbk  = error_gen_lookup_cbk,
	.checksum    = error_gen_checksum,
	.xattrop     = error_gen_xattrop,
	.fxattrop    = error_gen_fxattrop,
	.inodelk     = error_gen_inodelk,
	.finodelk    = error_gen_finodelk,
	.entrylk     = error_gen_entrylk,
	.fentrylk    = error_gen_fentrylk,
        .setattr     = error_gen_setattr,
        .fsetattr    = error_gen_fsetattr,
};

struct xlator_mops mops = {
	.stats = error_gen_stats,
	.getspec = error_gen_getspec,
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
