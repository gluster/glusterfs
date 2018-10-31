/*
   Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "xlator.h"
#include "posix-metadata.h"
#include "posix-metadata-disk.h"
#include "posix-handle.h"
#include "posix-messages.h"
#include "syscall.h"
#include "compat-errno.h"
#include "compat.h"

static int gf_posix_xattr_enotsup_log;

/* posix_mdata_to_disk converts posix_mdata_t into network byte order to
 * save it on disk in machine independant format
 */
static inline void
posix_mdata_to_disk (posix_mdata_disk_t *out, posix_mdata_t *in)
{
        out->version = in->version;
        out->flags   = htobe64(in->flags);

        out->ctime.tv_sec = htobe64(in->ctime.tv_sec);
        out->ctime.tv_nsec = htobe64(in->ctime.tv_nsec);

        out->mtime.tv_sec = htobe64(in->mtime.tv_sec);
        out->mtime.tv_nsec = htobe64(in->mtime.tv_nsec);

        out->atime.tv_sec = htobe64(in->atime.tv_sec);
        out->atime.tv_nsec = htobe64(in->atime.tv_nsec);
}

/* posix_mdata_from_disk converts posix_mdata_disk_t into host byte order
 */
static inline void
posix_mdata_from_disk (posix_mdata_t *out, posix_mdata_disk_t *in)
{
        out->version = in->version;
        out->flags   = be64toh(in->flags);

        out->ctime.tv_sec = be64toh(in->ctime.tv_sec);
        out->ctime.tv_nsec = be64toh(in->ctime.tv_nsec);

        out->mtime.tv_sec = be64toh(in->mtime.tv_sec);
        out->mtime.tv_nsec = be64toh(in->mtime.tv_nsec);

        out->atime.tv_sec = be64toh(in->atime.tv_sec);
        out->atime.tv_nsec = be64toh(in->atime.tv_nsec);
}

/* posix_fetch_mdata_xattr fetches the posix_mdata_t from disk */
static int
posix_fetch_mdata_xattr (xlator_t *this, const char *real_path_arg, int _fd,
                         inode_t *inode, posix_mdata_t *metadata, int *op_errno)
{
        size_t               size            = -1;
        int                  op_ret          = -1;
        char                *value           = NULL;
        gf_boolean_t         fd_based_fop    = _gf_false;
        char                 gfid_str[64]    = {0};
        char                *real_path       = NULL;

        char *key = GF_XATTR_MDATA_KEY;

        if (!metadata) {
                op_ret = -1;
                goto out;
        }

        if (_fd != -1) {
                fd_based_fop = _gf_true;
        }
        if (!(fd_based_fop || real_path_arg)) {
                MAKE_HANDLE_PATH (real_path, this, inode->gfid, NULL);
                if (!real_path) {
                        uuid_utoa_r (inode->gfid, gfid_str);
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_LSTAT_FAILED, "lstat on gfid %s failed",
                                gfid_str);
                        op_ret = -1;
                        *op_errno = errno;
                        goto out;
                }
        }

        if (fd_based_fop) {
                size = sys_fgetxattr (_fd, key, NULL, 0);
        } else if (real_path_arg) {
                size = sys_lgetxattr (real_path_arg, key, NULL, 0);
        } else if (real_path) {
                size = sys_lgetxattr (real_path, key, NULL, 0);
        }

        if (size == -1) {
                *op_errno = errno;
                if ((*op_errno == ENOTSUP) || (*op_errno == ENOSYS)) {
                        GF_LOG_OCCASIONALLY (gf_posix_xattr_enotsup_log,
                                             this->name, GF_LOG_WARNING,
                                             "Extended attributes not "
                                             "supported (try remounting"
                                             " brick with 'user_xattr' "
                                             "flag)");
                } else if (*op_errno == ENOATTR ||
                                *op_errno == ENODATA) {
                        gf_msg_debug (this->name, 0,
                                      "No such attribute:%s for file %s "
                                      "gfid: %s",
                                      key, real_path ? real_path : (real_path_arg ? real_path_arg : "null"),
                                      uuid_utoa(inode->gfid));
                } else {
                        gf_msg (this->name, GF_LOG_DEBUG, *op_errno,
                                P_MSG_XATTR_FAILED, "getxattr failed"
                                " on %s gfid: %s key: %s ",
                                real_path ? real_path : (real_path_arg ? real_path_arg : "null"),
                                uuid_utoa(inode->gfid), key);
                }
                op_ret = -1;
                goto out;
        }

        value = GF_CALLOC (size + 1, sizeof(char), gf_posix_mt_char);
        if (!value) {
                op_ret = -1;
                *op_errno = ENOMEM;
                goto out;
        }

        if (fd_based_fop) {
                size = sys_fgetxattr (_fd, key, value, size);
        } else if (real_path_arg) {
                size = sys_lgetxattr (real_path_arg, key, value, size);
        } else if (real_path) {
                size = sys_lgetxattr (real_path, key, value, size);
        }
        if (size == -1) {
                op_ret = -1;
                *op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        P_MSG_XATTR_FAILED, "getxattr failed on "
                        " on %s gfid: %s key: %s ",
                        real_path ? real_path : (real_path_arg ? real_path_arg : "null"),
                        uuid_utoa(inode->gfid), key);
                goto out;
        }

        posix_mdata_from_disk (metadata, (posix_mdata_disk_t*)value);

        op_ret = 0;
out:
        GF_FREE (value);
        return op_ret;
}

/* posix_store_mdata_xattr stores the posix_mdata_t on disk */
static int
posix_store_mdata_xattr (xlator_t *this, const char *real_path_arg, int fd,
                         inode_t *inode, posix_mdata_t *metadata)
{
        char                *real_path       = NULL;
        int                  op_ret          = 0;
        gf_boolean_t         fd_based_fop    = _gf_false;
        char                *key             = GF_XATTR_MDATA_KEY;
        char                 gfid_str[64]    = {0};
        posix_mdata_disk_t   disk_metadata;

        if (!metadata) {
                op_ret = -1;
                goto out;
        }

        if (fd != -1) {
                fd_based_fop = _gf_true;
        }
        if (!(fd_based_fop || real_path_arg)) {
                MAKE_HANDLE_PATH (real_path, this, inode->gfid, NULL);
                if (!real_path) {
                        uuid_utoa_r (inode->gfid, gfid_str);
                        gf_msg (this->name, GF_LOG_DEBUG, errno,
                                P_MSG_LSTAT_FAILED, "lstat on gfid %s failed",
                                gfid_str);
                        op_ret = -1;
                        goto out;
                }
        }

        /* Set default version as 1 */
        posix_mdata_to_disk (&disk_metadata, metadata);

        if (fd_based_fop) {
                op_ret = sys_fsetxattr (fd, key,
                                        (void *) &disk_metadata,
                                        sizeof (posix_mdata_disk_t), 0);
        } else if (real_path_arg) {
                op_ret = sys_lsetxattr (real_path_arg, key,
                                        (void *) &disk_metadata,
                                        sizeof (posix_mdata_disk_t), 0);
        } else if (real_path) {
                op_ret = sys_lsetxattr (real_path, key,
                                        (void *) &disk_metadata,
                                        sizeof (posix_mdata_disk_t), 0);
        }

#ifdef GF_DARWIN_HOST_OS
        if (real_path_arg) {
                posix_dump_buffer(this, real_path_arg, key, value, 0);
        } else if (real_path) {
                posix_dump_buffer(this, real_path, key, value, 0);
        }
#endif
out:
        if (op_ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_XATTR_FAILED,
                        "file: %s: gfid: %s key:%s ",
                        real_path ? real_path : (real_path_arg ? real_path_arg : "null"),
                        uuid_utoa(inode->gfid), key);
       }
       return op_ret;
}

/* _posix_get_mdata_xattr gets posix_mdata_t from inode context. If it fails
 * to get it from inode context, gets it from disk. This is with out inode lock.
 */
int
__posix_get_mdata_xattr (xlator_t *this, const char *real_path, int _fd,
                         inode_t *inode, struct iatt *stbuf)
{
        posix_mdata_t  *mdata       = NULL;
        int             ret         = -1;
        int             op_errno    = 0;

        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        ret = __inode_ctx_get1 (inode, this,
                                (uint64_t *)&mdata);
        if (ret == -1 || !mdata) {
                mdata = GF_CALLOC (1, sizeof (posix_mdata_t),
                                   gf_posix_mt_mdata_attr);
                if (!mdata) {
                        ret = -1;
                        goto out;
                }

                ret = posix_fetch_mdata_xattr (this, real_path, _fd, inode,
                                               mdata, &op_errno);

                if (ret == 0) {
                        /* Got mdata from disk, set it in inode ctx. This case
                         * is hit when in-memory status is lost due to brick
                         * down scenario
                         */
                        __inode_ctx_set1 (inode, this, (uint64_t *)&mdata);
                } else {
                        /* Failed to get mdata from disk, xattr missing.
                         * This happens on two cases.
                         * 1. File is created before ctime is enabled.
                         * 2. On new file creation.
                         *
                         * Do nothing, just return success. It is as
                         * good as ctime feature is not enabled for this
                         * file. For files created before ctime is enabled,
                         * time attributes gets updated into ctime structure
                         * once the metadata modification fop happens and
                         * time attributes become consistent eventually.
                         * For new files, it would obviously get updated
                         * before the fop completion.
                         */
                        if (stbuf && op_errno != ENOENT) {
                                ret = 0;
                                goto out;
                        } else {
                                /* This case should not be hit. If it hits,
                                 * don't fail, log warning, free mdata and move
                                 * on
                                 */
                                gf_msg (this->name, GF_LOG_WARNING, op_errno,
                                        P_MSG_FETCHMDATA_FAILED,
                                        "file: %s: gfid: %s key:%s ",
                                        real_path ? real_path : "null",
                                        uuid_utoa(inode->gfid),
                                        GF_XATTR_MDATA_KEY);
                                GF_FREE (mdata);
                                ret = 0;
                                goto out;
                        }
                }
        }

        ret = 0;

        if (ret == 0 && stbuf) {
                stbuf->ia_ctime = mdata->ctime.tv_sec;
                stbuf->ia_ctime_nsec = mdata->ctime.tv_nsec;
                stbuf->ia_mtime = mdata->mtime.tv_sec;
                stbuf->ia_mtime_nsec = mdata->mtime.tv_nsec;
                stbuf->ia_atime = mdata->atime.tv_sec;
                stbuf->ia_atime_nsec = mdata->atime.tv_nsec;
        }

out:
        return ret;
}

/* posix_get_mdata_xattr gets posix_mdata_t from inode context. If it fails
 * to get it from inode context, gets it from disk. This is with inode lock.
 */
int
posix_get_mdata_xattr (xlator_t *this, const char *real_path, int _fd,
                       inode_t *inode, struct iatt *stbuf)
{
        int             ret         = -1;

        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        LOCK (&inode->lock);
        {
                ret = __posix_get_mdata_xattr (this, real_path, _fd, inode, stbuf);
        }
        UNLOCK (&inode->lock);

out:
        return ret;
}

static int
posix_compare_timespec (struct timespec *first, struct timespec *second)
{
        if (first->tv_sec == second->tv_sec)
                return first->tv_nsec - second->tv_nsec;
        else
                return first->tv_sec - second->tv_sec;
}


/* posix_set_mdata_xattr updates the posix_mdata_t based on the flag
 * in inode context and stores it on disk
 */
static int
posix_set_mdata_xattr (xlator_t *this, const char *real_path, int fd,
                       inode_t *inode, struct timespec *time,
                       struct iatt *stbuf, posix_mdata_flag_t *flag,
                       gf_boolean_t update_utime)
{
        posix_mdata_t  *mdata       = NULL;
        int             ret         = -1;
        int             op_errno    = 0;

        GF_VALIDATE_OR_GOTO ("posix", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get1 (inode, this,
                                        (uint64_t *)&mdata);
                if (ret == -1 || !mdata) {
                        /*
                         * Do we need to fetch the data from xattr
                         * If we does we can compare the value and store
                         * the largest data in inode ctx.
                         */
                        mdata = GF_CALLOC (1, sizeof (posix_mdata_t),
                                           gf_posix_mt_mdata_attr);
                        if (!mdata) {
                                ret = -1;
                                goto unlock;
                        }

                        ret = posix_fetch_mdata_xattr (this, real_path, fd,
                                                       inode,
                                                       (void *)mdata, &op_errno);
                        if (ret == 0) {
                                /* Got mdata from disk, set it in inode ctx. This case
                                 * is hit when in-memory status is lost due to brick
                                 * down scenario
                                 */
                                __inode_ctx_set1 (inode, this,
                                                  (uint64_t *)&mdata);
                        } else if (ret && time) {
                                /*
                                 * This is the first time creating the time
                                 * attr. This happens when you activate this
                                 * feature, and the legacy file will not have
                                 * any xattr set.
                                 *
                                 * New files will create extended attributes.
                                 */

                                /*
                                 * TODO: This is wrong approach, because before
                                 * creating fresh xattr, we should consult
                                 * to all replica and/or distribution set.
                                 *
                                 * We should contact the time management
                                 * xlators, and ask them to create an xattr.
                                 */
                                /* We should not be relying on backend file's
                                 * time attributes to load the initial ctime
                                 * time attribute structure. This is incorrect
                                 * as each replica set would have witnessed the
                                 * file creation at different times.
                                 *
                                 * For new file creation, ctime, atime and mtime
                                 * should be same, hence initiate the ctime
                                 * structure with the time from the frame. But
                                 * for the files which were created before ctime
                                 * feature is enabled, this is not accurate but
                                 * still fine as the times would get eventually
                                 * accurate.
                                 */
                                mdata->version = 1;
                                mdata->flags = 0;
                                mdata->ctime.tv_sec = time->tv_sec;
                                mdata->ctime.tv_nsec = time->tv_nsec;
                                mdata->atime.tv_sec = time->tv_sec;
                                mdata->atime.tv_nsec = time->tv_nsec;
                                mdata->mtime.tv_sec = time->tv_sec;
                                mdata->mtime.tv_nsec = time->tv_nsec;

                                __inode_ctx_set1 (inode, this,
                                                  (uint64_t *)&mdata);
                        }
                }

                /* Earlier, mdata was updated only if the existing time is less
                 * than the time to be updated. This would fail the scenarios
                 * where mtime can be set to any time using the syscall. Hence
                 * just updating without comparison. But the ctime is not
                 * allowed to changed to older date.
                 */

                if (flag->ctime &&
                    posix_compare_timespec (time, &mdata->ctime) > 0) {
                        mdata->ctime = *time;
                }

                /* In distributed systems, there could be races with fops
                 * updating mtime/atime which could result in different
                 * mtime/atime for same file. So this makes sure, only the
                 * highest time is retained. If the mtime/atime update comes
                 * from the explicit utime syscall, it is allowed to set to
                 * previous time
                 */
                if (update_utime) {
                        if (flag->mtime) {
                                mdata->mtime = *time;
                        }
                        if (flag->atime) {
                                mdata->atime = *time;
                        }
                } else {
                        if (flag->mtime &&
                            posix_compare_timespec (time, &mdata->mtime) > 0) {
                                mdata->mtime = *time;
                        }
                        if (flag->atime &&
                            posix_compare_timespec (time, &mdata->atime) > 0) {
                                mdata->atime = *time;
                        }
                }

                if (inode->ia_type == IA_INVAL) {
                        /*
                         * TODO: This is non-linked inode. So we have to sync the
                         * data into backend. Because inode_link may return
                         * a different inode.
                         */
                        /*  ret = posix_store_mdata_xattr (this, loc, fd,
                         *                                 mdata); */
                }
                /*
                 * With this patch set, we are setting the xattr for each update
                 * We should evaluate the performance, and based on that we can
                 * decide on asynchronous updation.
                 */
                ret = posix_store_mdata_xattr (this, real_path, fd, inode,
                                               mdata);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_STOREMDATA_FAILED,
                                "file: %s: gfid: %s key:%s ",
                                real_path ? real_path : "null",
                                uuid_utoa(inode->gfid), GF_XATTR_MDATA_KEY);
                                goto unlock;
                }
        }
unlock:
        UNLOCK (&inode->lock);
out:
        if (ret == 0 && stbuf) {
                stbuf->ia_ctime = mdata->ctime.tv_sec;
                stbuf->ia_ctime_nsec = mdata->ctime.tv_nsec;
                stbuf->ia_mtime = mdata->mtime.tv_sec;
                stbuf->ia_mtime_nsec = mdata->mtime.tv_nsec;
                stbuf->ia_atime = mdata->atime.tv_sec;
                stbuf->ia_atime_nsec = mdata->atime.tv_nsec;
        }

        return ret;
}

/* posix_update_utime_in_mdata updates the posix_mdata_t when mtime/atime
 * is modified using syscall
 */
void
posix_update_utime_in_mdata (xlator_t *this, const char *real_path, int fd,
                             inode_t *inode,
                             struct iatt *stbuf, int valid)
{
        int32_t ret = 0;
#if defined(HAVE_UTIMENSAT)
        struct timespec tv    = {0, };
#else
        struct timeval tv     = {0, };
#endif
        posix_mdata_flag_t       flag            = {0, };

        struct posix_private *priv = NULL;

        priv = this->private;

        if (inode && priv->ctime) {
                if ((valid & GF_SET_ATTR_ATIME) == GF_SET_ATTR_ATIME) {
                        tv.tv_sec  = stbuf->ia_atime;
                        SET_TIMESPEC_NSEC_OR_TIMEVAL_USEC(tv, stbuf->ia_atime_nsec);

                        flag.ctime = 0;
                        flag.mtime = 0;
                        flag.atime = 1;
                        ret = posix_set_mdata_xattr (this, real_path, -1, inode, &tv, NULL,
                                                     &flag, _gf_true);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_WARNING, errno,
                                        P_MSG_SETMDATA_FAILED,
                                        "posix set mdata atime failed on file:"
                                        " %s gfid:%s",
                                        real_path, uuid_utoa (inode->gfid));
                        }
                }

                if ((valid & GF_SET_ATTR_MTIME) == GF_SET_ATTR_MTIME) {
                        tv.tv_sec  = stbuf->ia_mtime;
                        SET_TIMESPEC_NSEC_OR_TIMEVAL_USEC(tv, stbuf->ia_mtime_nsec);

                        flag.ctime = 1;
                        flag.mtime = 1;
                        flag.atime = 0;

                        ret = posix_set_mdata_xattr (this, real_path, -1, inode, &tv, NULL,
                                                     &flag, _gf_true);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_WARNING, errno,
                                        P_MSG_SETMDATA_FAILED,
                                        "posix set mdata mtime failed on file:"
                                        " %s gfid:%s",
                                        real_path, uuid_utoa (inode->gfid));
                        }
                }
        }
        return;
}

static void
posix_get_mdata_flag (uint64_t flags, posix_mdata_flag_t *flag)
{
        if (!flag)
                return;

        flag->ctime = 0;
        flag->atime = 0;
        flag->mtime = 0;

        if (flags & MDATA_CTIME)
                flag->ctime = 1;
        if (flags & MDATA_MTIME)
                flag->mtime = 1;
        if (flags & MDATA_ATIME)
                flag->atime = 1;
}

static void
posix_get_parent_mdata_flag (uint64_t flags, posix_mdata_flag_t *flag)
{
        if (!flag)
                return;

        flag->ctime = 0;
        flag->atime = 0;
        flag->mtime = 0;

        if (flags & MDATA_PAR_CTIME)
                flag->ctime = 1;
        if (flags & MDATA_PAR_MTIME)
                flag->mtime = 1;
        if (flags & MDATA_PAR_ATIME)
                flag->atime = 1;
}

void
posix_set_ctime (call_frame_t *frame, xlator_t *this, const char* real_path,
                 int fd, inode_t *inode, struct iatt *stbuf)
{
        posix_mdata_flag_t    flag            = {0,};
        int                   ret             = 0;
        struct posix_private *priv            = NULL;

        priv = this->private;

        if (priv->ctime) {
                (void) posix_get_mdata_flag (frame->root->flags, &flag);
                if (frame->root->ctime.tv_sec == 0) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_SETMDATA_FAILED,
                                "posix set mdata failed, No ctime : %s gfid:%s",
                                real_path,
                                inode ? uuid_utoa (inode->gfid) : "No inode");
                        goto out;
                }

                ret = posix_set_mdata_xattr (this, real_path, fd, inode,
                                             &frame->root->ctime, stbuf, &flag,
                                             _gf_false);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_SETMDATA_FAILED,
                                "posix set mdata failed on file: %s gfid:%s",
                                real_path,
                                inode ? uuid_utoa (inode->gfid) : "No inode");
                }
        }
 out:
        return;
}

void
posix_set_parent_ctime (call_frame_t *frame, xlator_t *this,
                        const char* real_path, int fd, inode_t *inode,
                        struct iatt *stbuf)
{
        posix_mdata_flag_t    flag            = {0,};
        int                   ret             = 0;
        struct posix_private *priv            = NULL;

        priv = this->private;

        if (inode && priv->ctime) {
                (void) posix_get_parent_mdata_flag (frame->root->flags, &flag);
                ret = posix_set_mdata_xattr (this, real_path, fd, inode,
                                             &frame->root->ctime, stbuf, &flag,
                                             _gf_false);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_SETMDATA_FAILED,
                                "posix set mdata failed on file: %s gfid:%s",
                                real_path, uuid_utoa (inode->gfid));
                }
        }
        return;
}
