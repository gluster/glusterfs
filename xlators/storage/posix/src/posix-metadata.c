/*
   Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <glusterfs/xlator.h>
#include "posix-metadata.h"
#include "posix-metadata-disk.h"
#include "posix-handle.h"
#include "posix-messages.h"
#include <glusterfs/syscall.h>
#include <glusterfs/compat-errno.h>
#include <glusterfs/compat.h>

static int gf_posix_xattr_enotsup_log;

/* posix_mdata_to_disk converts posix_mdata_t into network byte order to
 * save it on disk in machine independent format
 */
static inline void
posix_mdata_to_disk(posix_mdata_disk_t *out, posix_mdata_t *in)
{
    out->version = in->version;
    out->flags = htobe64(in->flags);

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
posix_mdata_from_disk(posix_mdata_t *out, posix_mdata_disk_t *in)
{
    out->version = in->version;
    out->flags = be64toh(in->flags);

    out->ctime.tv_sec = be64toh(in->ctime.tv_sec);
    out->ctime.tv_nsec = be64toh(in->ctime.tv_nsec);

    out->mtime.tv_sec = be64toh(in->mtime.tv_sec);
    out->mtime.tv_nsec = be64toh(in->mtime.tv_nsec);

    out->atime.tv_sec = be64toh(in->atime.tv_sec);
    out->atime.tv_nsec = be64toh(in->atime.tv_nsec);
}

void
posix_mdata_iatt_from_disk(struct mdata_iatt *out, posix_mdata_disk_t *in)
{
    out->ia_ctime = be64toh(in->ctime.tv_sec);
    out->ia_ctime_nsec = be64toh(in->ctime.tv_nsec);

    out->ia_mtime = be64toh(in->mtime.tv_sec);
    out->ia_mtime_nsec = be64toh(in->mtime.tv_nsec);

    out->ia_atime = be64toh(in->atime.tv_sec);
    out->ia_atime_nsec = be64toh(in->atime.tv_nsec);
}

/* posix_fetch_mdata_xattr fetches the posix_mdata_t from disk */
static int
posix_fetch_mdata_xattr(xlator_t *this, const char *real_path_arg, int _fd,
                        inode_t *inode, posix_mdata_t *metadata, int *op_errno)
{
    size_t size = 256;
    int op_ret = -1;
    char *value = NULL;
    gf_boolean_t fd_based_fop = _gf_false;
    char gfid_str[64] = {0};
    char *real_path = NULL;

    if (!metadata) {
        goto out;
    }

    if (_fd != -1) {
        fd_based_fop = _gf_true;
    }
    if (!(fd_based_fop || real_path_arg)) {
        GF_VALIDATE_OR_GOTO(this->name, inode, out);
        MAKE_HANDLE_PATH(real_path, this, inode->gfid, NULL);
        if (!real_path) {
            *op_errno = errno;
            uuid_utoa_r(inode->gfid, gfid_str);
            gf_msg(this->name, GF_LOG_WARNING, *op_errno, P_MSG_LSTAT_FAILED,
                   "lstat on gfid %s failed", gfid_str);
            goto out;
        }
    }

    value = GF_MALLOC(size * sizeof(char), gf_posix_mt_char);
    if (!value) {
        *op_errno = ENOMEM;
        goto out;
    }

    if (fd_based_fop) {
        size = sys_fgetxattr(_fd, GF_XATTR_MDATA_KEY, value, size);
    } else if (real_path_arg) {
        size = sys_lgetxattr(real_path_arg, GF_XATTR_MDATA_KEY, value, size);
    } else if (real_path) {
        size = sys_lgetxattr(real_path, GF_XATTR_MDATA_KEY, value, size);
    }

    if (size == -1) {
        *op_errno = errno;
        if (value) {
            GF_FREE(value);
            value = NULL;
        }
        if ((*op_errno == ENOTSUP) || (*op_errno == ENOSYS)) {
            GF_LOG_OCCASIONALLY(gf_posix_xattr_enotsup_log, this->name,
                                GF_LOG_WARNING,
                                "Extended attributes not supported"
                                " (try remounting brick with 'user xattr' "
                                "flag)");
        } else if (*op_errno == ENOATTR || *op_errno == ENODATA) {
            gf_msg_debug(this->name, 0,
                         "No such attribute:%s for file %s gfid: %s",
                         GF_XATTR_MDATA_KEY,
                         real_path ? real_path
                                   : (real_path_arg ? real_path_arg : "null"),
                         inode ? uuid_utoa(inode->gfid) : "null");
            goto out;
        }

        if (fd_based_fop) {
            size = sys_fgetxattr(_fd, GF_XATTR_MDATA_KEY, NULL, 0);
        } else if (real_path_arg) {
            size = sys_lgetxattr(real_path_arg, GF_XATTR_MDATA_KEY, NULL, 0);
        } else if (real_path) {
            size = sys_lgetxattr(real_path, GF_XATTR_MDATA_KEY, NULL, 0);
        }

        if (size == -1) { /* give up now and exist with an error */
            *op_errno = errno;
            gf_msg(this->name, GF_LOG_ERROR, *op_errno, P_MSG_XATTR_FAILED,
                   "getxattr failed on %s gfid: %s key: %s ",
                   real_path ? real_path
                             : (real_path_arg ? real_path_arg : "null"),
                   inode ? uuid_utoa(inode->gfid) : "null", GF_XATTR_MDATA_KEY);
            goto out;
        }

        value = GF_MALLOC(size * sizeof(char), gf_posix_mt_char);
        if (!value) {
            *op_errno = ENOMEM;
            goto out;
        }

        if (fd_based_fop) {
            size = sys_fgetxattr(_fd, GF_XATTR_MDATA_KEY, value, size);
        } else if (real_path_arg) {
            size = sys_lgetxattr(real_path_arg, GF_XATTR_MDATA_KEY, value,
                                 size);
        } else if (real_path) {
            size = sys_lgetxattr(real_path, GF_XATTR_MDATA_KEY, value, size);
        }
        if (size == -1) {
            *op_errno = errno;
            gf_msg(this->name, GF_LOG_ERROR, *op_errno, P_MSG_XATTR_FAILED,
                   "getxattr failed on %s gfid: %s key: %s ",
                   real_path ? real_path
                             : (real_path_arg ? real_path_arg : "null"),
                   inode ? uuid_utoa(inode->gfid) : "null", GF_XATTR_MDATA_KEY);
            goto out;
        }
    }
    posix_mdata_from_disk(metadata, (posix_mdata_disk_t *)value);

    op_ret = 0;
out:
    if (value)
        GF_FREE(value);
    return op_ret;
}

/* posix_store_mdata_xattr stores the posix_mdata_t on disk */
static int
posix_store_mdata_xattr(xlator_t *this, const char *real_path_arg, int fd,
                        inode_t *inode, posix_mdata_t *metadata)
{
    char *real_path = NULL;
    int op_ret = 0;
    gf_boolean_t fd_based_fop = _gf_false;
    char *key = GF_XATTR_MDATA_KEY;
    char gfid_str[64] = {0};
    posix_mdata_disk_t disk_metadata;

    if (!metadata) {
        op_ret = -1;
        goto out;
    }

    if (fd != -1) {
        fd_based_fop = _gf_true;
    }
    if (!(fd_based_fop || real_path_arg)) {
        MAKE_HANDLE_PATH(real_path, this, inode->gfid, NULL);
        if (!real_path) {
            uuid_utoa_r(inode->gfid, gfid_str);
            gf_msg(this->name, GF_LOG_DEBUG, errno, P_MSG_LSTAT_FAILED,
                   "lstat on gfid %s failed", gfid_str);
            op_ret = -1;
            goto out;
        }
    }

    /* Set default version as 1 */
    posix_mdata_to_disk(&disk_metadata, metadata);

    if (fd_based_fop) {
        op_ret = sys_fsetxattr(fd, key, (void *)&disk_metadata,
                               sizeof(posix_mdata_disk_t), 0);
    } else if (real_path_arg) {
        op_ret = sys_lsetxattr(real_path_arg, key, (void *)&disk_metadata,
                               sizeof(posix_mdata_disk_t), 0);
    } else if (real_path) {
        op_ret = sys_lsetxattr(real_path, key, (void *)&disk_metadata,
                               sizeof(posix_mdata_disk_t), 0);
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
        gf_msg(this->name, GF_LOG_ERROR, errno, P_MSG_XATTR_FAILED,
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
__posix_get_mdata_xattr(xlator_t *this, const char *real_path, int _fd,
                        inode_t *inode, struct iatt *stbuf)
{
    uint64_t ctx;
    posix_mdata_t *mdata = NULL;
    int ret = -1;
    int op_errno = 0;

    /* Handle readdirp: inode might be null, time attributes should be served
     * from xattr not from backend's file attributes */
    if (inode) {
        ret = __inode_ctx_get1(inode, this, &ctx);
        if (ret == 0) {
            mdata = (posix_mdata_t *)(uintptr_t)ctx;
        }
    } else {
        ret = -1;
    }

    if (ret == -1 || !mdata) {
        mdata = GF_CALLOC(1, sizeof(posix_mdata_t), gf_posix_mt_mdata_attr);
        if (!mdata) {
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, P_MSG_NOMEM,
                   "Could not allocate mdata. file: %s: gfid: %s",
                   real_path ? real_path : "null",
                   inode ? uuid_utoa(inode->gfid) : "null");
            ret = -1;
            goto out;
        }

        ret = posix_fetch_mdata_xattr(this, real_path, _fd, inode, mdata,
                                      &op_errno);

        if (ret == 0) {
            /* Got mdata from disk, set it in inode ctx. This case
             * is hit when in-memory status is lost due to brick
             * down scenario
             */
            if (inode) {
                ctx = (uint64_t)(uintptr_t)mdata;
                __inode_ctx_set1(inode, this, &ctx);
            }
        } else {
            /* Failed to get mdata from disk, xattr missing.
             * This happens when the file is created before
             * ctime is enabled.
             */
            if (stbuf && op_errno != ENOENT) {
                ret = 0;
                GF_FREE(mdata);
                goto out;
            } else {
                /* This case should not be hit. If it hits,
                 * don't fail, log warning, free mdata and move
                 * on
                 */
                gf_msg(this->name, GF_LOG_WARNING, op_errno,
                       P_MSG_FETCHMDATA_FAILED, "file: %s: gfid: %s key:%s ",
                       real_path ? real_path : "null",
                       inode ? uuid_utoa(inode->gfid) : "null",
                       GF_XATTR_MDATA_KEY);
                GF_FREE(mdata);
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
    /* Not set in inode context, hence free mdata */
    if (!inode) {
        GF_FREE(mdata);
    }

out:
    return ret;
}

/* posix_get_mdata_xattr gets posix_mdata_t from inode context. If it fails
 * to get it from inode context, gets it from disk. This is with inode lock.
 */
int
posix_get_mdata_xattr(xlator_t *this, const char *real_path, int _fd,
                      inode_t *inode, struct iatt *stbuf)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    LOCK(&inode->lock);
    {
        ret = __posix_get_mdata_xattr(this, real_path, _fd, inode, stbuf);
    }
    UNLOCK(&inode->lock);

out:
    return ret;
}

static int
posix_compare_timespec(struct timespec *first, struct timespec *second)
{
    if (first->tv_sec == second->tv_sec)
        return first->tv_nsec - second->tv_nsec;
    else
        return first->tv_sec - second->tv_sec;
}

int
posix_set_mdata_xattr_legacy_files(xlator_t *this, inode_t *inode,
                                   const char *realpath,
                                   struct mdata_iatt *mdata_iatt, int *op_errno)
{
    uint64_t ctx;
    posix_mdata_t *mdata = NULL;
    posix_mdata_t imdata = {
        0,
    };
    int ret = 0;
    gf_boolean_t mdata_already_set = _gf_false;

    GF_VALIDATE_OR_GOTO("posix", this, out);
    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    LOCK(&inode->lock);
    {
        ret = __inode_ctx_get1(inode, this, &ctx);
        if (ret == 0 && ctx) {
            mdata = (posix_mdata_t *)(uintptr_t)ctx;
            mdata_already_set = _gf_true;
        } else {
            mdata = GF_CALLOC(1, sizeof(posix_mdata_t), gf_posix_mt_mdata_attr);
            if (!mdata) {
                gf_msg(this->name, GF_LOG_ERROR, ENOMEM, P_MSG_NOMEM,
                       "Could not allocate mdata. gfid: %s",
                       uuid_utoa(inode->gfid));
                ret = -1;
                *op_errno = ENOMEM;
                goto unlock;
            }

            ret = posix_fetch_mdata_xattr(this, realpath, -1, inode,
                                          (void *)mdata, op_errno);
            if (ret == 0) {
                /* Got mdata from disk. This is a race, another client
                 * has healed the xattr during lookup. So set it in inode
                 * ctx */
                ctx = (uint64_t)(uintptr_t)mdata;
                __inode_ctx_set1(inode, this, &ctx);
                mdata_already_set = _gf_true;
            } else {
                *op_errno = 0;
                mdata->version = 1;
                mdata->flags = 0;
                mdata->ctime.tv_sec = mdata_iatt->ia_ctime;
                mdata->ctime.tv_nsec = mdata_iatt->ia_ctime_nsec;
                mdata->atime.tv_sec = mdata_iatt->ia_atime;
                mdata->atime.tv_nsec = mdata_iatt->ia_atime_nsec;
                mdata->mtime.tv_sec = mdata_iatt->ia_mtime;
                mdata->mtime.tv_nsec = mdata_iatt->ia_mtime_nsec;

                ctx = (uint64_t)(uintptr_t)mdata;
                __inode_ctx_set1(inode, this, &ctx);
            }
        }

        if (mdata_already_set) {
            /* Compare and update the larger time */
            imdata.ctime.tv_sec = mdata_iatt->ia_ctime;
            imdata.ctime.tv_nsec = mdata_iatt->ia_ctime_nsec;
            imdata.atime.tv_sec = mdata_iatt->ia_atime;
            imdata.atime.tv_nsec = mdata_iatt->ia_atime_nsec;
            imdata.mtime.tv_sec = mdata_iatt->ia_mtime;
            imdata.mtime.tv_nsec = mdata_iatt->ia_mtime_nsec;

            if (posix_compare_timespec(&imdata.ctime, &mdata->ctime) > 0) {
                mdata->ctime = imdata.ctime;
            }
            if (posix_compare_timespec(&imdata.mtime, &mdata->mtime) > 0) {
                mdata->mtime = imdata.mtime;
            }
            if (posix_compare_timespec(&imdata.atime, &mdata->atime) > 0) {
                mdata->atime = imdata.atime;
            }
        }

        ret = posix_store_mdata_xattr(this, realpath, -1, inode, mdata);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, errno, P_MSG_STOREMDATA_FAILED,
                   "gfid: %s key:%s ", uuid_utoa(inode->gfid),
                   GF_XATTR_MDATA_KEY);
            *op_errno = errno;
            goto unlock;
        }
    }
unlock:
    UNLOCK(&inode->lock);
out:
    return ret;
}

/* posix_set_mdata_xattr updates the posix_mdata_t based on the flag
 * in inode context and stores it on disk
 */
static int
posix_set_mdata_xattr(xlator_t *this, const char *real_path, int fd,
                      inode_t *inode, struct timespec *time,
                      struct timespec *u_atime, struct timespec *u_mtime,
                      struct iatt *stbuf, posix_mdata_flag_t *flag,
                      gf_boolean_t update_utime)
{
    uint64_t ctx;
    posix_mdata_t *mdata = NULL;
    int ret = -1;
    int op_errno = 0;

    GF_VALIDATE_OR_GOTO("posix", this, out);
    GF_VALIDATE_OR_GOTO(this->name, inode, out);
    GF_VALIDATE_OR_GOTO(this->name, time, out);

    if (update_utime && (flag->atime && !u_atime) &&
        (flag->mtime && !u_mtime)) {
        goto out;
    }

    LOCK(&inode->lock);
    {
        ret = __inode_ctx_get1(inode, this, &ctx);
        if (ret == 0) {
            mdata = (posix_mdata_t *)(uintptr_t)ctx;
        }
        if (ret == -1 || !mdata) {
            /*
             * Do we need to fetch the data from xattr
             * If we does we can compare the value and store
             * the largest data in inode ctx.
             */
            mdata = GF_CALLOC(1, sizeof(posix_mdata_t), gf_posix_mt_mdata_attr);
            if (!mdata) {
                gf_msg(this->name, GF_LOG_ERROR, ENOMEM, P_MSG_NOMEM,
                       "Could not allocate mdata. file: %s: gfid: %s",
                       real_path ? real_path : "null", uuid_utoa(inode->gfid));
                ret = -1;
                goto unlock;
            }

            ret = posix_fetch_mdata_xattr(this, real_path, fd, inode,
                                          (void *)mdata, &op_errno);
            if (ret == 0) {
                /* Got mdata from disk, set it in inode ctx. This case
                 * is hit when in-memory status is lost due to brick
                 * down scenario
                 */
                ctx = (uint64_t)(uintptr_t)mdata;
                __inode_ctx_set1(inode, this, &ctx);
            } else {
                /*
                 * This is the first time creating the time attr. This happens
                 * when you activate this feature. On this code path, only new
                 * files will create mdata xattr. The legacy files (files
                 * created before ctime enabled) will not have any xattr set.
                 * The xattr on legacy file will be set via lookup.
                 */

                /* Don't create xattr with utimes/utimensat, only update if
                 * present. This otherwise causes issues during inservice
                 * upgrade. It causes inconsistent xattr values with in replica
                 * set. The scenario happens during upgrade where clients are
                 * older versions (without the ctime feature) and the server is
                 * upgraded to the new version (with the ctime feature which
                 * is enabled by default).
                 */

                if (update_utime) {
                    UNLOCK(&inode->lock);
                    GF_FREE(mdata);
                    return 0;
                }

                mdata->version = 1;
                mdata->flags = 0;
                mdata->ctime.tv_sec = time->tv_sec;
                mdata->ctime.tv_nsec = time->tv_nsec;
                mdata->atime.tv_sec = time->tv_sec;
                mdata->atime.tv_nsec = time->tv_nsec;
                mdata->mtime.tv_sec = time->tv_sec;
                mdata->mtime.tv_nsec = time->tv_nsec;

                ctx = (uint64_t)(uintptr_t)mdata;
                __inode_ctx_set1(inode, this, &ctx);
            }
        }

        /* In distributed systems, there could be races with fops
         * updating mtime/atime which could result in different
         * mtime/atime for same file. So this makes sure, only the
         * highest time is retained. If the mtime/atime update comes
         * from the explicit utime syscall, it is allowed to set to
         * previous or future time but the ctime is always set to
         * current time.
         */
        if (update_utime) {
            if (flag->ctime &&
                posix_compare_timespec(time, &mdata->ctime) > 0) {
                mdata->ctime = *time;
            }
            if (flag->mtime) {
                mdata->mtime = *u_mtime;
            }
            if (flag->atime) {
                mdata->atime = *u_atime;
            }
        } else {
            if (flag->ctime &&
                posix_compare_timespec(time, &mdata->ctime) > 0) {
                mdata->ctime = *time;
            }
            if (flag->mtime &&
                posix_compare_timespec(time, &mdata->mtime) > 0) {
                mdata->mtime = *time;
            }
            if (flag->atime &&
                posix_compare_timespec(time, &mdata->atime) > 0) {
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
        ret = posix_store_mdata_xattr(this, real_path, fd, inode, mdata);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, errno, P_MSG_STOREMDATA_FAILED,
                   "file: %s: gfid: %s key:%s ", real_path ? real_path : "null",
                   uuid_utoa(inode->gfid), GF_XATTR_MDATA_KEY);
            goto unlock;
        }
    }
unlock:
    UNLOCK(&inode->lock);
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
posix_update_utime_in_mdata(xlator_t *this, const char *real_path, int fd,
                            inode_t *inode, struct timespec *ctime,
                            struct iatt *stbuf, int valid)
{
    int32_t ret = 0;
#if defined(HAVE_UTIMENSAT)
    struct timespec tv_atime = {
        0,
    };
    struct timespec tv_mtime = {
        0,
    };
#else
    struct timeval tv_atime = {
        0,
    };
    struct timeval tv_mtime = {
        0,
    };
#endif
    posix_mdata_flag_t flag = {
        0,
    };

    struct posix_private *priv = NULL;

    priv = this->private;

    /* NOTE:
     * This routine (utimes) is intentionally allowed for all internal and
     * external clients even if ctime is not set. This is because AFR and
     * WORM uses time attributes for it's internal operations
     */
    if (inode && priv->ctime) {
        if ((valid & GF_SET_ATTR_ATIME) == GF_SET_ATTR_ATIME) {
            tv_atime.tv_sec = stbuf->ia_atime;
            SET_TIMESPEC_NSEC_OR_TIMEVAL_USEC(tv_atime, stbuf->ia_atime_nsec);

            flag.ctime = 1;
            flag.atime = 1;
        }

        if ((valid & GF_SET_ATTR_MTIME) == GF_SET_ATTR_MTIME) {
            tv_mtime.tv_sec = stbuf->ia_mtime;
            SET_TIMESPEC_NSEC_OR_TIMEVAL_USEC(tv_mtime, stbuf->ia_mtime_nsec);

            flag.ctime = 1;
            flag.mtime = 1;
        }

        if (flag.mtime || flag.atime) {
            ret = posix_set_mdata_xattr(this, real_path, -1, inode, ctime,
                                        &tv_atime, &tv_mtime, NULL, &flag,
                                        _gf_true);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_SETMDATA_FAILED,
                       "posix set mdata atime failed on file:"
                       " %s gfid:%s",
                       real_path, uuid_utoa(inode->gfid));
            }
        }
    }
    return;
}

/* posix_update_ctime_in_mdata updates the posix_mdata_t when ctime needs
 * to be modified
 */
void
posix_update_ctime_in_mdata(xlator_t *this, const char *real_path, int fd,
                            inode_t *inode, struct timespec *ctime,
                            struct iatt *stbuf, int valid)
{
    int32_t ret = 0;
#if defined(HAVE_UTIMENSAT)
    struct timespec tv_ctime = {
        0,
    };
#else
    struct timeval tv_ctime = {
        0,
    };
#endif
    posix_mdata_flag_t flag = {
        0,
    };

    struct posix_private *priv = NULL;
    priv = this->private;

    if (inode && priv->ctime) {
        tv_ctime.tv_sec = stbuf->ia_ctime;
        SET_TIMESPEC_NSEC_OR_TIMEVAL_USEC(tv_ctime, stbuf->ia_ctime_nsec);
        flag.ctime = 1;

        ret = posix_set_mdata_xattr(this, real_path, -1, inode, &tv_ctime, NULL,
                                    NULL, NULL, &flag, _gf_true);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_SETMDATA_FAILED,
                   "posix set mdata atime failed on file:"
                   " %s gfid:%s",
                   real_path, uuid_utoa(inode->gfid));
        }
    }
    return;
}

static void
posix_get_mdata_flag(uint64_t flags, posix_mdata_flag_t *flag)
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
posix_get_parent_mdata_flag(uint64_t flags, posix_mdata_flag_t *flag)
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
posix_set_ctime(call_frame_t *frame, xlator_t *this, const char *real_path,
                int fd, inode_t *inode, struct iatt *stbuf)
{
    posix_mdata_flag_t flag = {
        0,
    };
    int ret = 0;
    struct posix_private *priv = NULL;

    priv = this->private;

    if (priv->ctime) {
        (void)posix_get_mdata_flag(frame->root->flags, &flag);
        if ((flag.ctime == 0) && (flag.mtime == 0) && (flag.atime == 0)) {
            goto out;
        }
        ret = posix_set_mdata_xattr(this, real_path, fd, inode,
                                    &frame->root->ctime, NULL, NULL, stbuf,
                                    &flag, _gf_false);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_SETMDATA_FAILED,
                   "posix set mdata failed on file: %s gfid:%s", real_path,
                   inode ? uuid_utoa(inode->gfid) : "No inode");
        }
    }
out:
    return;
}

void
posix_set_parent_ctime(call_frame_t *frame, xlator_t *this,
                       const char *real_path, int fd, inode_t *inode,
                       struct iatt *stbuf)
{
    posix_mdata_flag_t flag = {
        0,
    };
    int ret = 0;
    struct posix_private *priv = NULL;

    priv = this->private;

    if (inode && priv->ctime) {
        (void)posix_get_parent_mdata_flag(frame->root->flags, &flag);
        if ((flag.ctime == 0) && (flag.mtime == 0) && (flag.atime == 0)) {
            goto out;
        }
        ret = posix_set_mdata_xattr(this, real_path, fd, inode,
                                    &frame->root->ctime, NULL, NULL, stbuf,
                                    &flag, _gf_false);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_SETMDATA_FAILED,
                   "posix set mdata failed on file: %s gfid:%s", real_path,
                   uuid_utoa(inode->gfid));
        }
    }
out:
    return;
}

void
posix_set_ctime_cfr(call_frame_t *frame, xlator_t *this,
                    const char *real_path_in, int fd_in, inode_t *inode_in,
                    struct iatt *stbuf_in, const char *real_path_out,
                    int fd_out, inode_t *inode_out, struct iatt *stbuf_out)
{
    posix_mdata_flag_t flag = {
        0,
    };
    posix_mdata_flag_t flag_dup = {
        0,
    };
    int ret = 0;
    struct posix_private *priv = NULL;
    char in_uuid_str[64] = {0}, out_uuid_str[64] = {0};

    priv = this->private;

    if (priv->ctime) {
        (void)posix_get_mdata_flag(frame->root->flags, &flag);
        if ((flag.ctime == 0) && (flag.mtime == 0) && (flag.atime == 0)) {
            goto out;
        }

        if (frame->root->ctime.tv_sec == 0) {
            gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_SETMDATA_FAILED,
                   "posix set mdata failed, No ctime : in: %s gfid_in:%s "
                   "out: %s gfid_out:%s",
                   real_path_in,
                   (inode_in ? uuid_utoa_r(inode_in->gfid, in_uuid_str)
                             : "No inode"),
                   real_path_out,
                   (inode_out ? uuid_utoa_r(inode_out->gfid, out_uuid_str)
                              : "No inode"));
            goto out;
        }

        flag_dup = flag;

        /*
         * For the destination file, no need to update atime.
         * It got modified. Hence the things that need to be
         * changed are mtime and ctime (provided the utime
         * xlator from the client has set those flags, which
         * are just copied to flag_dup).
         */
        if (flag.atime)
            flag_dup.atime = 0;

        ret = posix_set_mdata_xattr(this, real_path_out, fd_out, inode_out,
                                    &frame->root->ctime, NULL, NULL, stbuf_out,
                                    &flag_dup, _gf_false);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_SETMDATA_FAILED,
                   "posix set mdata failed on file: %s gfid:%s", real_path_out,
                   inode_out ? uuid_utoa(inode_out->gfid) : "No inode");
        }

        /*
         * For the source file, no need to change the mtime and ctime.
         * For source file, it is only read operation. So, if at all
         * anything needs to be updated, it is only the atime.
         */
        if (flag.atime)
            flag_dup.atime = flag.atime;
        flag_dup.mtime = 0;
        flag_dup.ctime = 0;

        ret = posix_set_mdata_xattr(this, real_path_in, fd_out, inode_out,
                                    &frame->root->ctime, NULL, NULL, stbuf_out,
                                    &flag_dup, _gf_false);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_SETMDATA_FAILED,
                   "posix set mdata failed on file: %s gfid:%s", real_path_in,
                   inode_in ? uuid_utoa(inode_in->gfid) : "No inode");
        }
    }
out:
    return;
}
