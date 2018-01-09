/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "xlator.h"
#include "error-gen.h"
#include "statedump.h"
#include "defaults.h"

/*
 * The user can specify an error probability as a float percentage, but we
 * store it internally as a numerator with this as the denominator.  When it's
 * used, it's like this:
 *
 *    (rand() % FAILURE_GRANULARITY) < error_rate
 *
 * To minimize rounding errors from the modulo operation, it's good for this to
 * be a power of two.
 *
 * (BTW this is just the normal case.  If "random-failure" is set, that does
 * something completely different and this number is irrelevant.  See error_gen
 * for the legacy code.)
 */
#define FAILURE_GRANULARITY     (1 << 20)

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
        [GF_FOP_WRITE]             = { .error_no_count = 7,
                                    .error_no = {EINVAL,EBADF,EFAULT,EISDIR,
                                                 ENAMETOOLONG,ENOSPC,
						 GF_ERROR_SHORT_WRITE}},
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
        [GF_FOP_FSETXATTR]          = { .error_no_count = 4,
                                        .error_no = {EACCES,EBADF,EINTR,
                                                     ENAMETOOLONG}},
        [GF_FOP_FGETXATTR]          = { .error_no_count = 4,
                                        .error_no = {EACCES,EBADF,ENAMETOOLONG,
                                                     EINTR}},
        [GF_FOP_FREMOVEXATTR]       = { .error_no_count = 4,
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
                /* coverty[DC.WEAK_CRYPTO] */
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
	else if (!strcmp((*error_no), "GF_ERROR_SHORT_WRITE"))
		return GF_ERROR_SHORT_WRITE;
        else
                return EAGAIN;
}

int
error_gen (xlator_t *this, int op_no)
{
        eg_t             *egp = NULL;
        int              count = 0;
        int              error_no_int = 0;
        int              rand_no = 0;
        int              ret = 0;
        gf_boolean_t     should_err = _gf_false;

        egp = this->private;

        if (egp->random_failure) {
                /*
                 * I honestly don't know why anyone would use this "feature"
                 * but I'll try to preserve its functionality anyway.  Without
                 * locking twice to update failure_iter_no and egp->op_count
                 * separately, then not locking at all to update
                 * egp->failure_iter_no.  That's not needed for compatibility,
                 * and it's abhorrently wrong.  I have *some* standards.
                 */
                LOCK (&egp->lock);
                {
                        count = ++(egp->op_count);
                        error_no_int = egp->error_no_int;
                        if ((count % egp->failure_iter_no) == 0) {
                                egp->op_count = 0;
                                /* coverty[DC.WEAK_CRYPTO] */
                                egp->failure_iter_no = 3
                                        + (rand () % GF_UNIVERSAL_ANSWER);
                                should_err = _gf_true;
                        }
                }
                UNLOCK (&egp->lock);
        } else {
                /*
                 * It turns out that rand() is almost universally implemented
                 * as a linear congruential PRNG, which is about as cheap as
                 * it gets.  This gets us real random behavior, including
                 * phenomena like streaks and dry spells, with controllable
                 * long-term probability, cheaply.
                 */
                if ((rand () % FAILURE_GRANULARITY) < egp->failure_iter_no) {
                        should_err = _gf_true;
                }
        }

        if (should_err) {
                if (error_no_int)
                        ret = error_no_int;
                else {
                        rand_no = generate_rand_no (op_no);
                        if (op_no >= GF_FOP_MAXVALUE)
                                op_no = 0;
                        if (rand_no >= error_no_list[op_no].error_no_count)
                                rand_no = 0;
                        ret = error_no_list[op_no].error_no[rand_no];
                }
        }

        return ret;
}

int
error_gen_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
                  dict_t *xdata)
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

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->lookup,
		    loc, xdata);
        return 0;
}

int
error_gen_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
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
		STACK_UNWIND_STRICT (stat, frame, -1, op_errno, NULL, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->stat, loc, xdata);
        return 0;
}

int
error_gen_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                   struct iatt *stbuf, int32_t valid, dict_t *xdata)
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
		STACK_UNWIND_STRICT (setattr, frame, -1, op_errno, NULL, NULL, xdata);
	        return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->setattr,
		    loc, stbuf, valid, xdata);
        return 0;
}


int
error_gen_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                    struct iatt *stbuf, int32_t valid, dict_t *xdata)
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
		STACK_UNWIND_STRICT (fsetattr, frame, -1, op_errno, NULL, NULL, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fsetattr,
		    fd, stbuf, valid, xdata);
        return 0;
}

int
error_gen_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    off_t offset, dict_t *xdata)
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
                                     NULL, NULL, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->truncate,
		    loc, offset, xdata);
        return 0;
}

int
error_gen_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd,
		     off_t offset, dict_t *xdata)
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
                                     NULL, NULL, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->ftruncate,
		    fd, offset, xdata);
        return 0;
}

int
error_gen_access (call_frame_t *frame, xlator_t *this, loc_t *loc,
		  int32_t mask, dict_t *xdata)
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
		STACK_UNWIND_STRICT (access, frame, -1, op_errno, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->access,
		    loc, mask, xdata);
        return 0;
}

int
error_gen_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    size_t size, dict_t *xdata)
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
		STACK_UNWIND_STRICT (readlink, frame, -1, op_errno, NULL, NULL, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readlink,
		    loc, size, xdata);
        return 0;
}

int
error_gen_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc,
		 mode_t mode, dev_t rdev, mode_t umask, dict_t *xdata)
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
                                     NULL, NULL, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mknod,
		    loc, mode, rdev, umask, xdata);
        return 0;
}

int
error_gen_mkdir (call_frame_t *frame, xlator_t *this,
		 loc_t *loc, mode_t mode, mode_t umask, dict_t *xdata)
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
                                     NULL, NULL, xdata);
                return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mkdir,
		    loc, mode, umask, xdata);
        return 0;
}

int
error_gen_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
                  dict_t *xdata)
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
		STACK_UNWIND_STRICT (unlink, frame, -1, op_errno, NULL, NULL,
                                     xdata);
                return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->unlink,
		    loc, xflag, xdata);
        return 0;
}

int
error_gen_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
                 dict_t *xdata)
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
		STACK_UNWIND_STRICT (rmdir, frame, -1, op_errno, NULL, NULL, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rmdir,
		    loc, flags, xdata);
        return 0;
}

int
error_gen_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
		   loc_t *loc, mode_t umask, dict_t *xdata)
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
                                     NULL, NULL, NULL); /* pre & post parent attr */
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->symlink,
		    linkpath, loc, umask, xdata);
        return 0;
}

int
error_gen_rename (call_frame_t *frame, xlator_t *this,
		  loc_t *oldloc, loc_t *newloc, dict_t *xdata)
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
                                     NULL, NULL, NULL, NULL, NULL);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rename,
		    oldloc, newloc, xdata);
        return 0;
}

int
error_gen_link (call_frame_t *frame, xlator_t *this,
		loc_t *oldloc, loc_t *newloc, dict_t *xdata)
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
                                     NULL, NULL, NULL);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->link,
		    oldloc, newloc, xdata);
        return 0;
}

int
error_gen_create (call_frame_t *frame, xlator_t *this, loc_t *loc,
		  int32_t flags, mode_t mode, mode_t umask, fd_t *fd,
                  dict_t *xdata)
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
                                     NULL, NULL, NULL, NULL);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->create,
		    loc, flags, mode, umask, fd, xdata);
        return 0;
}

int
error_gen_open (call_frame_t *frame, xlator_t *this, loc_t *loc,
		int32_t flags, fd_t *fd, dict_t *xdata)
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
		STACK_UNWIND_STRICT (open, frame, -1, op_errno, NULL, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->open,
		    loc, flags, fd, xdata);
        return 0;
}

int
error_gen_readv (call_frame_t *frame, xlator_t *this,
		 fd_t *fd, size_t size, off_t offset, uint32_t flags, dict_t *xdata)
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
                                     NULL, NULL, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readv,
		    fd, size, offset, flags, xdata);
        return 0;
}

int
error_gen_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
		  struct iovec *vector, int32_t count,
		  off_t off, uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;
	struct iovec *shortvec = NULL;

        egp = this->private;
        enable = egp->enable[GF_FOP_WRITE];

        if (enable)
                op_errno = error_gen (this, GF_FOP_WRITE);

	if (op_errno == GF_ERROR_SHORT_WRITE) {

		/*
		 * A short write error returns some value less than what was
		 * requested from a write. To simulate this, replace the vector
		 * with one half the size;
		 */
		shortvec = iov_dup(vector, 1);
		shortvec->iov_len /= 2;
		goto wind;
	} else if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (writev, frame, -1, op_errno, NULL, NULL, xdata);
        	return 0;
	}
wind:
	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->writev,
                   fd, shortvec?shortvec:vector,
                   count, off, flags, iobref, xdata);

	if (shortvec)
		GF_FREE (shortvec);
        return 0;
}

int
error_gen_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
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
		STACK_UNWIND_STRICT (flush, frame, -1, op_errno, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->flush,
		    fd, xdata);
        return 0;
}

int
error_gen_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags, dict_t *xdata)
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
		STACK_UNWIND_STRICT (fsync, frame, -1, op_errno, NULL, NULL, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fsync,
		    fd, flags, xdata);
        return 0;
}

int
error_gen_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
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
		STACK_UNWIND_STRICT (fstat, frame, -1, op_errno, NULL, xdata);
	        return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fstat,
		    fd, xdata);
        return 0;
}

int
error_gen_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd, dict_t *xdata)
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
		STACK_UNWIND_STRICT (opendir, frame, -1, op_errno, NULL, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->opendir,
		    loc, fd, xdata);
        return 0;
}

int
error_gen_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
                    int32_t flags, dict_t *xdata)
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
		STACK_UNWIND_STRICT (fsyncdir, frame, -1, op_errno, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fsyncdir,
		    fd, flags, xdata);
        return 0;
}

int
error_gen_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
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
		STACK_UNWIND_STRICT (statfs, frame, -1, op_errno, NULL, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->statfs,
		    loc, xdata);
        return 0;
}

int
error_gen_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
		    dict_t *dict, int32_t flags, dict_t *xdata)
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
		STACK_UNWIND_STRICT (setxattr, frame, -1, op_errno, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->setxattr,
		    loc, dict, flags, xdata);
        return 0;
}

int
error_gen_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
		    const char *name, dict_t *xdata)
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
		STACK_UNWIND_STRICT (getxattr, frame, -1, op_errno, NULL, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->getxattr,
		    loc, name, xdata);
        return 0;
}

int
error_gen_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     dict_t *dict, int32_t flags, dict_t *xdata)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_FSETXATTR];

        if (enable)
                op_errno = error_gen (this, GF_FOP_FSETXATTR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (fsetxattr, frame, -1, op_errno, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fsetxattr,
		    fd, dict, flags, xdata);
        return 0;
}

int
error_gen_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     const char *name, dict_t *xdata)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_FGETXATTR];

        if (enable)
                op_errno = error_gen (this, GF_FOP_FGETXATTR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (fgetxattr, frame, -1, op_errno, NULL, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fgetxattr,
		    fd, name, xdata);
        return 0;
}

int
error_gen_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
		   gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
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
		STACK_UNWIND_STRICT (xattrop, frame, -1, op_errno, NULL, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->xattrop,
		    loc, flags, dict, xdata);
        return 0;
}

int
error_gen_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
		    gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
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
		STACK_UNWIND_STRICT (fxattrop, frame, -1, op_errno, NULL, xdata);
	        return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fxattrop,
		    fd, flags, dict, xdata);
        return 0;
}

int
error_gen_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
		       const char *name, dict_t *xdata)
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
		STACK_UNWIND_STRICT (removexattr, frame, -1, op_errno, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->removexattr,
		    loc, name, xdata);
        return 0;
}

int
error_gen_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                        const char *name, dict_t *xdata)
{
	int              op_errno = 0;
        eg_t            *egp = NULL;
        int              enable = 1;

        egp = this->private;
        enable = egp->enable[GF_FOP_FREMOVEXATTR];

        if (enable)
                op_errno = error_gen (this, GF_FOP_FREMOVEXATTR);

	if (op_errno) {
		GF_ERROR(this, "unwind(-1, %s)", strerror (op_errno));
		STACK_UNWIND_STRICT (fremovexattr, frame, -1, op_errno, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fremovexattr,
		    fd, name, xdata);
        return 0;
}

int
error_gen_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
	      struct gf_flock *lock, dict_t *xdata)
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
		STACK_UNWIND_STRICT (lk, frame, -1, op_errno, NULL, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->lk,
		    fd, cmd, lock, xdata);
        return 0;
}

int
error_gen_inodelk (call_frame_t *frame, xlator_t *this,
		   const char *volume, loc_t *loc, int32_t cmd,
                   struct gf_flock *lock, dict_t *xdata)
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
		STACK_UNWIND_STRICT (inodelk, frame, -1, op_errno, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->inodelk,
		    volume, loc, cmd, lock, xdata);
        return 0;
}

int
error_gen_finodelk (call_frame_t *frame, xlator_t *this,
		    const char *volume, fd_t *fd, int32_t cmd,
                    struct gf_flock *lock, dict_t *xdata)
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
		STACK_UNWIND_STRICT (finodelk, frame, -1, op_errno, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->finodelk,
		    volume, fd, cmd, lock, xdata);
        return 0;
}

int
error_gen_entrylk (call_frame_t *frame, xlator_t *this,
		   const char *volume, loc_t *loc, const char *basename,
		   entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
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
		STACK_UNWIND_STRICT (entrylk, frame, -1, op_errno, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->entrylk,
		    volume, loc, basename, cmd, type, xdata);
        return 0;
}

int
error_gen_fentrylk (call_frame_t *frame, xlator_t *this,
		    const char *volume, fd_t *fd, const char *basename,
		    entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
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
		STACK_UNWIND_STRICT (fentrylk, frame, -1, op_errno, xdata);
                return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fentrylk,
		    volume, fd, basename, cmd, type, xdata);
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

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->getspec,
		    key, flags);
	return 0;
}

int
error_gen_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
		   size_t size, off_t off, dict_t *xdata)
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
		STACK_UNWIND_STRICT (readdir, frame, -1, op_errno, NULL, xdata);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readdir,
		    fd, size, off, xdata);
	return 0;
}

int
error_gen_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                    off_t off, dict_t *dict)
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
		STACK_UNWIND_STRICT (readdirp, frame, -1, op_errno, NULL, NULL);
		return 0;
	}

	STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdirp,
                    fd, size, off, dict);
	return 0;
}

static void
error_gen_set_failure (eg_t *pvt, double percent)
{
        double  ppm;

        GF_ASSERT (pvt);

        ppm = (percent / 100.0) * (double)FAILURE_GRANULARITY;
        pvt->failure_iter_no = (int)ppm;
}

static void
error_gen_parse_fill_fops (eg_t *pvt, char *enable_fops)
{
        char            *op_no_str = NULL;
        int              op_no = -1;
        int              i = 0;
        xlator_t        *this = THIS;
        char            *saveptr = NULL;

        GF_ASSERT (pvt);
        GF_ASSERT (this);

        for (i = 0; i < GF_FOP_MAXVALUE; i++)
                pvt->enable[i] = 0;

        if (!enable_fops) {
                gf_log (this->name, GF_LOG_WARNING,
                        "All fops are enabled.");
                for (i = 0; i < GF_FOP_MAXVALUE; i++)
                        pvt->enable[i] = 1;
        } else {
                op_no_str = strtok_r (enable_fops, ",", &saveptr);
                while (op_no_str) {
                        op_no = gf_fop_int (op_no_str);
                        if (op_no == -1) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Wrong option value %s", op_no_str);
                        } else
                                pvt->enable[op_no] = 1;

                        op_no_str = strtok_r (NULL, ",", &saveptr);
                }
        }
}

int32_t
error_gen_priv_dump (xlator_t *this)
{
        char            key_prefix[GF_DUMP_MAX_BUF_LEN];
        int             ret = -1;
        eg_t            *conf = NULL;

        if (!this)
            goto out;

        conf = this->private;
        if (!conf)
            goto out;

        ret = TRY_LOCK(&conf->lock);
        if (ret != 0) {
                return ret;
        }

        gf_proc_dump_add_section("xlator.debug.error-gen.%s.priv", this->name);
        gf_proc_dump_build_key(key_prefix,"xlator.debug.error-gen","%s.priv",
                               this->name);

        gf_proc_dump_write("op_count", "%d", conf->op_count);
        gf_proc_dump_write("failure_iter_no", "%d", conf->failure_iter_no);
        gf_proc_dump_write("error_no", "%s", conf->error_no);
        gf_proc_dump_write("random_failure", "%d", conf->random_failure);

        UNLOCK(&conf->lock);
out:
        return ret;
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
reconfigure (xlator_t *this, dict_t *options)
{
        eg_t            *pvt = NULL;
        int32_t          ret = 0;
        char            *error_enable_fops = NULL;
        double           failure_percent_dbl = 0.0;

        if (!this || !this->private)
                goto out;

        pvt = this->private;

        ret = -1;

        GF_OPTION_RECONF ("error-no", pvt->error_no, options, str, out);

        if (pvt->error_no)
                pvt->error_no_int = conv_errno_to_int (&pvt->error_no);

        GF_OPTION_RECONF ("failure", failure_percent_dbl, options, percent,
                          out);

        GF_OPTION_RECONF ("enable", error_enable_fops, options, str, out);

        GF_OPTION_RECONF ("random-failure", pvt->random_failure, options,
                          bool, out);

        error_gen_parse_fill_fops (pvt, error_enable_fops);
        error_gen_set_failure (pvt, failure_percent_dbl);

        ret = 0;
out:
        gf_log (this->name, GF_LOG_DEBUG, "reconfigure returning %d", ret);
        return ret;
}

int
init (xlator_t *this)
{
        eg_t            *pvt = NULL;
        int32_t          ret = 0;
        char            *error_enable_fops = NULL;
        double          failure_percent_dbl = 0.0;

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

        pvt = GF_CALLOC (1, sizeof (eg_t), gf_error_gen_mt_eg_t);

        if (!pvt) {
                ret = -1;
                goto out;
        }

        LOCK_INIT (&pvt->lock);

        ret = -1;

        GF_OPTION_INIT ("error-no", pvt->error_no, str, out);

        if (pvt->error_no)
                pvt->error_no_int = conv_errno_to_int (&pvt->error_no);

        GF_OPTION_INIT ("failure", failure_percent_dbl, percent, out);

        GF_OPTION_INIT ("enable", error_enable_fops, str, out);

        GF_OPTION_INIT ("random-failure", pvt->random_failure, bool, out);


        error_gen_parse_fill_fops (pvt, error_enable_fops);
        error_gen_set_failure (pvt, failure_percent_dbl);

        this->private = pvt;

        /* Give some seed value here */
        srand (time(NULL));

        ret = 0;
out:
        if (ret)
                GF_FREE (pvt);
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

struct xlator_dumpops dumpops = {
        .priv = error_gen_priv_dump,
};

struct xlator_cbks cbks;

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
	.fsetxattr    = error_gen_fsetxattr,
	.fgetxattr    = error_gen_fgetxattr,
	.fremovexattr = error_gen_fremovexattr,
	.opendir     = error_gen_opendir,
	.readdir     = error_gen_readdir,
	.readdirp    = error_gen_readdirp,
	.fsyncdir    = error_gen_fsyncdir,
	.access      = error_gen_access,
	.ftruncate   = error_gen_ftruncate,
	.fstat       = error_gen_fstat,
	.lk          = error_gen_lk,
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

struct volume_options options[] = {
        { .key  = {"failure"},
          .type = GF_OPTION_TYPE_PERCENT,
          .description = "Percentage failure of operations when enabled.",
        },

        { .key  = {"error-no"},
          .value = {"ENOENT","ENOTDIR","ENAMETOOLONG","EACCES","EBADF",
                    "EFAULT","ENOMEM","EINVAL","EIO","EEXIST","ENOSPC",
                    "EPERM","EROFS","EBUSY","EISDIR","ENOTEMPTY","EMLINK"
                    "ENODEV","EXDEV","EMFILE","ENFILE","ENOSYS","EINTR",
                    "EFBIG","EAGAIN","GF_ERROR_SHORT_WRITE"},
          .type = GF_OPTION_TYPE_STR,
          .op_version = {3},
          .tags = {"error-gen"},
          .flags = OPT_FLAG_SETTABLE,

        },

        { .key = {"random-failure"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .op_version = {3},
          .tags = {"error-gen"},
          .flags = OPT_FLAG_SETTABLE,
        },

        { .key  = {"enable", "error-fops"},
          .type = GF_OPTION_TYPE_STR,
          .description = "Accepts a string which takes ',' separated fop "
                         "strings to denote which fops are enabled for error",
          .op_version = {3},
          .tags = {"error-gen"},
          .flags = OPT_FLAG_SETTABLE,
        },

        { .key  = {NULL} }
};
