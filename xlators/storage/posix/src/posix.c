/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#define __XOPEN_SOURCE 500

/* for SEEK_HOLE and SEEK_DATA */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <openssl/md5.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <ftw.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/uio.h>
#include <unistd.h>
#include <ftw.h>

#ifndef GF_BSD_HOST_OS
#include <alloca.h>
#endif /* GF_BSD_HOST_OS */

#ifdef HAVE_LINKAT
#include <fcntl.h>
#endif /* HAVE_LINKAT */

#include "glusterfs.h"
#include "checksum.h"
#include "dict.h"
#include "logging.h"
#include "posix.h"
#include "xlator.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat-errno.h"
#include "compat.h"
#include "byte-order.h"
#include "syscall.h"
#include "statedump.h"
#include "locking.h"
#include "timer.h"
#include "glusterfs3-xdr.h"
#include "hashfn.h"
#include "posix-aio.h"
#include "glusterfs-acl.h"
#include "posix-messages.h"
#include "events.h"

extern char *marker_xattrs[];
#define ALIGN_SIZE 4096

#undef HAVE_SET_FSID
#ifdef HAVE_SET_FSID

#define DECLARE_OLD_FS_ID_VAR uid_t old_fsuid; gid_t old_fsgid;

#define SET_FS_ID(uid, gid) do {                \
                old_fsuid = setfsuid (uid);     \
                old_fsgid = setfsgid (gid);     \
        } while (0)

#define SET_TO_OLD_FS_ID() do {                 \
                setfsuid (old_fsuid);           \
                setfsgid (old_fsgid);           \
        } while (0)

#else

#define DECLARE_OLD_FS_ID_VAR
#define SET_FS_ID(uid, gid)
#define SET_TO_OLD_FS_ID()

#endif

dict_t*
posix_dict_set_nlink (dict_t *req, dict_t *res, int32_t nlink)
{
        int   ret  =  -1;

        if (req == NULL || !dict_get (req, GF_REQUEST_LINK_COUNT_XDATA))
                goto out;

        if (res == NULL)
                res = dict_new ();
        if (res == NULL)
                goto out;

        ret = dict_set_uint32 (res, GF_RESPONSE_LINK_COUNT_XDATA, nlink);
        if (ret == -1)
                gf_msg ("posix", GF_LOG_WARNING, 0, P_MSG_SET_XDATA_FAIL,
                        "Failed to set GF_RESPONSE_LINK_COUNT_XDATA");
out:
        return res;
}

int
posix_forget (xlator_t *this, inode_t *inode)
{
        uint64_t tmp_cache = 0;
        int ret = 0;
        char *unlink_path = NULL;
        struct posix_private    *priv_posix = NULL;

        priv_posix = (struct posix_private *) this->private;

        ret = inode_ctx_del (inode, this, &tmp_cache);
        if (ret < 0) {
                ret = 0;
                goto out;
        }
        if (tmp_cache == GF_UNLINK_TRUE) {
                POSIX_GET_FILE_UNLINK_PATH(priv_posix->base_path,
                                           inode->gfid, unlink_path);
                if (!unlink_path) {
                        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                P_MSG_UNLINK_FAILED,
                                "Failed to remove gfid :%s",
                                uuid_utoa (inode->gfid));
                        ret = -1;
                        goto out;
                }
                ret = sys_unlink(unlink_path);
        }
out:
        return ret;
}

/* Regular fops */

int32_t
posix_lookup (call_frame_t *frame, xlator_t *this,
              loc_t *loc, dict_t *xdata)
{
        struct iatt buf                = {0, };
        int32_t     op_ret             = -1;
        int32_t     entry_ret          = 0;
        int32_t     op_errno           = 0;
        dict_t *    xattr              = NULL;
        char *      real_path          = NULL;
        char *      par_path           = NULL;
        struct iatt postparent         = {0,};
        int32_t     gfidless           = 0;
        char        *pgfid_xattr_key   = NULL;
        int32_t     nlink_samepgfid    = 0;
        struct  posix_private *priv    = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;

        /* The Hidden directory should be for housekeeping purpose and it
           should not get any gfid on it */
        if (__is_root_gfid (loc->pargfid) && loc->name
            && (strcmp (loc->name, GF_HIDDEN_PATH) == 0)) {
                gf_msg (this->name, GF_LOG_WARNING, EPERM,
                        P_MSG_LOOKUP_NOT_PERMITTED, "Lookup issued on %s,"
                        " which is not permitted", GF_HIDDEN_PATH);
                op_errno = EPERM;
                op_ret = -1;
                goto out;
        }

        op_ret = dict_get_int32 (xdata, GF_GFIDLESS_LOOKUP, &gfidless);
        op_ret = -1;
        if (gf_uuid_is_null (loc->pargfid) || (loc->name == NULL)) {
                /* nameless lookup */
                MAKE_INODE_HANDLE (real_path, this, loc, &buf);
        } else {
                MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, &buf);

                if (gf_uuid_is_null (loc->inode->gfid)) {
                        posix_gfid_heal (this, real_path, loc, xdata);
                        MAKE_ENTRY_HANDLE (real_path, par_path, this,
                                           loc, &buf);
                }
        }

        op_errno = errno;

        if (op_ret == -1) {
                if (op_errno != ENOENT) {
                        gf_msg (this->name, GF_LOG_WARNING, op_errno,
                                P_MSG_LSTAT_FAILED,
                                "lstat on %s failed",
                                real_path ? real_path : "null");
                }

                entry_ret = -1;
                goto parent;
        }

        if (xdata && (op_ret == 0)) {
                xattr = posix_xattr_fill (this, real_path, loc, NULL, -1, xdata,
                                          &buf);
        }

        if (priv->update_pgfid_nlinks) {
                if (!gf_uuid_is_null (loc->pargfid) && !IA_ISDIR (buf.ia_type)) {
                        MAKE_PGFID_XATTR_KEY (pgfid_xattr_key,
                                              PGFID_XATTR_KEY_PREFIX,
                                              loc->pargfid);

                        LOCK (&loc->inode->lock);
                        {
                                SET_PGFID_XATTR_IF_ABSENT (real_path,
                                                           pgfid_xattr_key,
                                                           nlink_samepgfid,
                                                           XATTR_CREATE, op_ret,
                                                           this, unlock);
                        }
unlock:
                        UNLOCK (&loc->inode->lock);
                }
        }

parent:
        if (par_path) {
                op_ret = posix_pstat (this, loc->pargfid, par_path, &postparent);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_LSTAT_FAILED, "post-operation lstat on"
                                " parent %s failed", par_path);
			if (op_errno == ENOENT)
				/* If parent directory is missing in a lookup,
				   errno should be ESTALE (bad handle) and not
				   ENOENT (missing entry)
				*/
				op_errno = ESTALE;
                        goto out;
                }
        }

        op_ret = entry_ret;
out:
        if (!op_ret && !gfidless && gf_uuid_is_null (buf.ia_gfid)) {
                gf_msg (this->name, GF_LOG_ERROR, ENODATA, P_MSG_NULL_GFID,
                        "buf->ia_gfid is null for "
                        "%s", (real_path) ? real_path: "");
                op_ret = -1;
                op_errno = ENODATA;
        }
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno,
                             (loc)?loc->inode:NULL, &buf, xattr, &postparent);

        if (xattr)
                dict_unref (xattr);

        return 0;
}


int32_t
posix_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        struct iatt           buf       = {0,};
        int32_t               op_ret    = -1;
        int32_t               op_errno  = 0;
        struct posix_private *priv      = NULL;
        char                 *real_path = NULL;
        dict_t               *xattr_rsp = NULL;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);

        MAKE_INODE_HANDLE (real_path, this, loc, &buf);

        if (op_ret == -1) {
                op_errno = errno;
                if (op_errno == ENOENT) {
                        gf_msg_debug(this->name, 0, "lstat on %s failed: %s",
                                     real_path ? real_path : "<null>",
                                     strerror (op_errno));
                } else {
                        gf_msg (this->name, GF_LOG_ERROR, op_errno,
                                P_MSG_LSTAT_FAILED, "lstat on %s failed",
                                real_path ? real_path : "<null>");
                }
                goto out;
        }
        if (xdata)
                xattr_rsp = posix_xattr_fill (this, real_path, loc, NULL, -1,
                                              xdata, &buf);

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID();
        STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, &buf, xattr_rsp);
        if (xattr_rsp)
                dict_unref (xattr_rsp);

        return 0;
}

static int
posix_do_chmod (xlator_t *this, const char *path, struct iatt *stbuf)
{
        int32_t     ret = -1;
        mode_t      mode = 0;
        struct stat stat;
        int         is_symlink = 0;

        ret = sys_lstat (path, &stat);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0, P_MSG_LSTAT_FAILED,
                        "lstat failed: %s", path);
                goto out;
        }

        if (S_ISLNK (stat.st_mode))
                is_symlink = 1;

        mode = st_mode_from_ia (stbuf->ia_prot, stbuf->ia_type);
        ret = lchmod (path, mode);
        if ((ret == -1) && (errno == ENOSYS)) {
                /* in Linux symlinks are always in mode 0777 and no
                   such call as lchmod exists.
                */
                gf_msg_debug (this->name, 0, "%s (%s)", path, strerror (errno));
                if (is_symlink) {
                        ret = 0;
                        goto out;
                }

                ret = sys_chmod (path, mode);
        }
out:
        return ret;
}

static int
posix_do_chown (xlator_t *this,
                const char *path,
                struct iatt *stbuf,
                int32_t valid)
{
        int32_t ret = -1;
        uid_t uid = -1;
        gid_t gid = -1;

        if (valid & GF_SET_ATTR_UID)
                uid = stbuf->ia_uid;

        if (valid & GF_SET_ATTR_GID)
                gid = stbuf->ia_gid;

        ret = sys_lchown (path, uid, gid);

        return ret;
}

static int
posix_do_utimes (xlator_t *this,
                 const char *path,
                 struct iatt *stbuf,
                 int valid)
{
        int32_t ret = -1;
        struct timeval tv[2]     = {{0,},{0,}};
        struct stat stat;
        int    is_symlink = 0;

        ret = sys_lstat (path, &stat);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        P_MSG_FILE_OP_FAILED, "%s", path);
                goto out;
        }

        if (S_ISLNK (stat.st_mode))
                is_symlink = 1;

        if ((valid & GF_SET_ATTR_ATIME) == GF_SET_ATTR_ATIME) {
                tv[0].tv_sec  = stbuf->ia_atime;
                tv[0].tv_usec = stbuf->ia_atime_nsec / 1000;
        } else {
                /* atime is not given, use current values */
                tv[0].tv_sec  = ST_ATIM_SEC (&stat);
                tv[0].tv_usec = ST_ATIM_NSEC (&stat) / 1000;
        }

        if ((valid & GF_SET_ATTR_MTIME) == GF_SET_ATTR_MTIME) {
                tv[1].tv_sec  = stbuf->ia_mtime;
                tv[1].tv_usec = stbuf->ia_mtime_nsec / 1000;
        } else {
                /* mtime is not given, use current values */
                tv[1].tv_sec  = ST_MTIM_SEC (&stat);
                tv[1].tv_usec = ST_MTIM_NSEC (&stat) / 1000;
        }

        ret = lutimes (path, tv);
        if ((ret == -1) && (errno == ENOSYS)) {
                gf_msg_debug (this->name, 0, "%s (%s)",
                        path, strerror (errno));
                if (is_symlink) {
                        ret = 0;
                        goto out;
                }

                ret = sys_utimes (path, tv);
        }

out:
        return ret;
}

int
posix_setattr (call_frame_t *frame, xlator_t *this,
               loc_t *loc, struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        int32_t        op_ret    = -1;
        int32_t        op_errno  = 0;
        char *         real_path = 0;
        struct iatt    statpre     = {0,};
        struct iatt    statpost    = {0,};
        dict_t        *xattr_rsp   = NULL;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_INODE_HANDLE (real_path, this, loc, &statpre);

        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "setattr (lstat) on %s failed",
                        real_path ? real_path : "<null>");
                goto out;
        }

        if (valid & (GF_SET_ATTR_UID | GF_SET_ATTR_GID)){
                op_ret = posix_do_chown (this, real_path, stbuf, valid);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_CHOWN_FAILED, "setattr (chown) on %s "
                                "failed", real_path);
                        goto out;
                }
        }

        if (valid & GF_SET_ATTR_MODE) {
                op_ret = posix_do_chmod (this, real_path, stbuf);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_CHMOD_FAILED,  "setattr (chmod) on %s "
                                "failed", real_path);
                        goto out;
                }
        }

        if (valid & (GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME)) {
                op_ret = posix_do_utimes (this, real_path, stbuf, valid);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_UTIMES_FAILED, "setattr (utimes) on %s "
                                "failed", real_path);
                        goto out;
                }
        }

        if (!valid) {
                op_ret = sys_lchown (real_path, -1, -1);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_LCHOWN_FAILED, "lchown (%s, -1, -1) "
                                "failed", real_path);

                        goto out;
                }
        }

        op_ret = posix_pstat (this, loc->gfid, real_path, &statpost);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "setattr (lstat) on %s failed", real_path);
                goto out;
        }

        if (xdata)
                xattr_rsp = posix_xattr_fill (this, real_path, loc, NULL, -1,
                                              xdata, &statpost);
        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno,
                             &statpre, &statpost, xattr_rsp);
        if (xattr_rsp)
                dict_unref (xattr_rsp);

        return 0;
}

int32_t
posix_do_fchown (xlator_t *this,
                 int fd,
                 struct iatt *stbuf,
                 int32_t valid)
{
        int   ret      = -1;
        uid_t uid = -1;
        gid_t gid = -1;

        if (valid & GF_SET_ATTR_UID)
                uid = stbuf->ia_uid;

        if (valid & GF_SET_ATTR_GID)
                gid = stbuf->ia_gid;

        ret = sys_fchown (fd, uid, gid);

        return ret;
}


int32_t
posix_do_fchmod (xlator_t *this,
                 int fd, struct iatt *stbuf)
{
        mode_t  mode = 0;

        mode = st_mode_from_ia (stbuf->ia_prot, stbuf->ia_type);
        return sys_fchmod (fd, mode);
}

static int
posix_do_futimes (xlator_t *this,
                  int fd,
                  struct iatt *stbuf)
{
        gf_msg (this->name, GF_LOG_WARNING, ENOSYS, P_MSG_UNKNOWN_OP,
                "function not implemented fd(%d)", fd);

        errno = ENOSYS;
        return -1;
}

int
posix_fsetattr (call_frame_t *frame, xlator_t *this,
                fd_t *fd, struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        int32_t        op_ret    = -1;
        int32_t        op_errno  = 0;
        struct iatt    statpre     = {0,};
        struct iatt    statpost    = {0,};
        struct posix_fd *pfd = NULL;
        dict_t          *xattr_rsp = NULL;
        int32_t          ret = -1;

        DECLARE_OLD_FS_ID_VAR;

        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = posix_fd_ctx_get (fd, this, &pfd, &op_errno);
        if (ret < 0) {
                gf_msg_debug (this->name, 0, "pfd is NULL from fd=%p", fd);
                goto out;
        }

        op_ret = posix_fdstat (this, pfd->fd, &statpre);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_FSTAT_FAILED,
                        "fsetattr (fstat) failed on fd=%p", fd);
                goto out;
        }

        if (valid & (GF_SET_ATTR_UID | GF_SET_ATTR_GID)) {
                op_ret = posix_do_fchown (this, pfd->fd, stbuf, valid);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_FCHOWN_FAILED, "fsetattr (fchown) failed"
                                " on fd=%p", fd);
                        goto out;
                }

        }

        if (valid & GF_SET_ATTR_MODE) {
                op_ret = posix_do_fchmod (this, pfd->fd, stbuf);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_FCHMOD_FAILED, "fsetattr (fchmod) failed"
                                " on fd=%p", fd);
                        goto out;
                }
        }

        if (valid & (GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME)) {
                op_ret = posix_do_futimes (this, pfd->fd, stbuf);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_FUTIMES_FAILED, "fsetattr (futimes) on "
                                "failed fd=%p", fd);
                        goto out;
                }
        }

        if (!valid) {
                op_ret = sys_fchown (pfd->fd, -1, -1);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_FCHOWN_FAILED,
                                "fchown (%d, -1, -1) failed",
                                pfd->fd);

                        goto out;
                }
        }

        op_ret = posix_fdstat (this, pfd->fd, &statpost);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_FSTAT_FAILED,
                        "fsetattr (fstat) failed on fd=%p", fd);
                goto out;
        }

        if (xdata)
                xattr_rsp = posix_xattr_fill (this, NULL, NULL, fd, pfd->fd,
                                              xdata, &statpost);
        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (fsetattr, frame, op_ret, op_errno,
                             &statpre, &statpost, xattr_rsp);
        if (xattr_rsp)
                dict_unref (xattr_rsp);

        return 0;
}

static int32_t
posix_do_fallocate (call_frame_t *frame, xlator_t *this, fd_t *fd,
                    int32_t flags, off_t offset, size_t len,
                    struct iatt *statpre, struct iatt *statpost, dict_t *xdata)
{
        int32_t             ret    = -1;
        int32_t             op_errno = 0;
        struct posix_fd    *pfd    = NULL;
        gf_boolean_t        locked = _gf_false;

        DECLARE_OLD_FS_ID_VAR;

        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = posix_fd_ctx_get (fd, this, &pfd, &op_errno);
        if (ret < 0) {
                gf_msg_debug (this->name, 0, "pfd is NULL from fd=%p", fd);
                goto out;
        }

        if (dict_get (xdata, GLUSTERFS_WRITE_UPDATE_ATOMIC)) {
                locked = _gf_true;
                LOCK(&fd->inode->lock);
        }

        ret = posix_fdstat (this, pfd->fd, statpre);
        if (ret == -1) {
                ret = -errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_FSTAT_FAILED,
                        "fallocate (fstat) failed on fd=%p", fd);
                goto out;
        }

	ret = sys_fallocate (pfd->fd, flags, offset, len);
	if (ret == -1) {
		ret = -errno;
		goto out;
	}

        ret = posix_fdstat (this, pfd->fd, statpost);
        if (ret == -1) {
                ret = -errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_FSTAT_FAILED,
                        "fallocate (fstat) failed on fd=%p", fd);
                goto out;
        }

out:
        if (locked) {
                UNLOCK (&fd->inode->lock);
                locked = _gf_false;
        }
        SET_TO_OLD_FS_ID ();

        return ret;
}

char*
_page_aligned_alloc (size_t size, char **aligned_buf)
{
        char            *alloc_buf = NULL;
        char            *buf = NULL;

        alloc_buf = GF_CALLOC (1, (size + ALIGN_SIZE), gf_posix_mt_char);
        if (!alloc_buf)
                goto out;
        /* page aligned buffer */
        buf = GF_ALIGN_BUF (alloc_buf, ALIGN_SIZE);
        *aligned_buf = buf;
out:
        return alloc_buf;
}

static int32_t
_posix_do_zerofill(int fd, off_t offset, off_t len, int o_direct)
{
        off_t               num_vect            = 0;
        off_t               num_loop            = 1;
        off_t               idx                 = 0;
        int32_t             op_ret              = -1;
        int32_t             vect_size           = VECTOR_SIZE;
        off_t               remain              = 0;
        off_t               extra               = 0;
        struct iovec       *vector              = NULL;
        char               *iov_base            = NULL;
        char               *alloc_buf           = NULL;

        if (len == 0)
                return 0;
        if (len < VECTOR_SIZE)
                vect_size = len;

        num_vect = len / (vect_size);
        remain = len % vect_size ;
        if (num_vect > MAX_NO_VECT) {
                extra = num_vect % MAX_NO_VECT;
                num_loop = num_vect / MAX_NO_VECT;
                num_vect = MAX_NO_VECT;
        }

        vector = GF_CALLOC (num_vect, sizeof(struct iovec),
                             gf_common_mt_iovec);
        if (!vector)
                  return -1;
        if (o_direct) {
                alloc_buf = _page_aligned_alloc(vect_size, &iov_base);
                if (!alloc_buf) {
                        GF_FREE(vector);
                        return -1;
                }
        } else {
                iov_base = GF_CALLOC (vect_size, sizeof(char),
                                        gf_common_mt_char);
                if (!iov_base) {
                        GF_FREE(vector);
                        return -1;
                 }
        }

        for (idx = 0; idx < num_vect; idx++) {
                vector[idx].iov_base = iov_base;
                vector[idx].iov_len  = vect_size;
        }
        if (sys_lseek (fd, offset, SEEK_SET) < 0) {
                op_ret = -1;
                goto err;
        }

        for (idx = 0; idx < num_loop; idx++) {
                op_ret = sys_writev (fd, vector, num_vect);
                if (op_ret < 0)
                        goto err;
        }
        if (extra) {
                op_ret = sys_writev (fd, vector, extra);
                if (op_ret < 0)
                        goto err;
        }
        if (remain) {
                vector[0].iov_len = remain;
                op_ret = sys_writev (fd, vector , 1);
                if (op_ret < 0)
                        goto err;
        }
err:
        if (o_direct)
                GF_FREE(alloc_buf);
        else
                GF_FREE(iov_base);
        GF_FREE(vector);
        return op_ret;
}

static int32_t
posix_do_zerofill (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                   off_t len, struct iatt *statpre, struct iatt *statpost,
                   dict_t *xdata)
{
        int32_t            ret       = -1;
        int32_t            op_errno  = 0;
        int32_t            flags     = 0;
        struct posix_fd   *pfd       = NULL;
        gf_boolean_t       locked    = _gf_false;

        DECLARE_OLD_FS_ID_VAR;

        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = posix_fd_ctx_get (fd, this, &pfd, &op_errno);
        if (ret < 0) {
                gf_msg_debug (this->name, 0, "pfd is NULL from fd=%p", fd);
                goto out;
        }

        if (dict_get (xdata, GLUSTERFS_WRITE_UPDATE_ATOMIC)) {
                locked = _gf_true;
                LOCK(&fd->inode->lock);
        }

        ret = posix_fdstat (this, pfd->fd, statpre);
        if (ret == -1) {
                ret = -errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_FSTAT_FAILED,
                        "pre-operation fstat failed on fd = %p", fd);
                goto out;
        }

        /* See if we can use FALLOC_FL_ZERO_RANGE to perform the zero fill.
         * If it fails, fall back to _posix_do_zerofill() and an optional fsync.
         */
        flags = FALLOC_FL_ZERO_RANGE;
        ret = sys_fallocate (pfd->fd, flags, offset, len);
        if (ret == 0)
                goto fsync;

        ret = _posix_do_zerofill (pfd->fd, offset, len, pfd->flags & O_DIRECT);
        if (ret < 0) {
                ret = -errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_ZEROFILL_FAILED,
                       "zerofill failed on fd %d length %" PRId64 ,
                        pfd->fd, len);
                goto out;
        }

fsync:
        if (pfd->flags & (O_SYNC|O_DSYNC)) {
                ret = sys_fsync (pfd->fd);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_WRITEV_FAILED, "fsync() in writev on fd"
                                "%d failed", pfd->fd);
                        ret = -errno;
                        goto out;
                }
        }

        ret = posix_fdstat (this, pfd->fd, statpost);
        if (ret == -1) {
                ret = -errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_FSTAT_FAILED,
                        "post operation fstat failed on fd=%p", fd);
                goto out;
        }

out:
	if (locked) {
		UNLOCK (&fd->inode->lock);
		locked = _gf_false;
	}
        SET_TO_OLD_FS_ID ();

        return ret;
}

static int32_t
_posix_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t keep_size,
		off_t offset, size_t len, dict_t *xdata)
{
	int32_t ret;
	int32_t flags = 0;
        struct iatt statpre = {0,};
        struct iatt statpost = {0,};

#ifdef FALLOC_FL_KEEP_SIZE
	if (keep_size)
		flags = FALLOC_FL_KEEP_SIZE;
#endif /* FALLOC_FL_KEEP_SIZE */

	ret = posix_do_fallocate (frame, this, fd, flags, offset, len,
				  &statpre, &statpost, xdata);
	if (ret < 0)
		goto err;

	STACK_UNWIND_STRICT(fallocate, frame, 0, 0, &statpre, &statpost, NULL);
	return 0;

err:
	STACK_UNWIND_STRICT(fallocate, frame, -1, -ret, NULL, NULL, NULL);
	return 0;
}

static int32_t
posix_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
	      size_t len, dict_t *xdata)
{
	int32_t ret;
#ifndef FALLOC_FL_KEEP_SIZE
	ret = EOPNOTSUPP;

#else /* FALLOC_FL_KEEP_SIZE */
	int32_t flags = FALLOC_FL_KEEP_SIZE|FALLOC_FL_PUNCH_HOLE;
        struct iatt statpre = {0,};
        struct iatt statpost = {0,};

	ret = posix_do_fallocate (frame, this, fd, flags, offset, len,
				  &statpre, &statpost, xdata);
	if (ret < 0)
		goto err;

	STACK_UNWIND_STRICT(discard, frame, 0, 0, &statpre, &statpost, NULL);
	return 0;

err:
#endif /* FALLOC_FL_KEEP_SIZE */
	STACK_UNWIND_STRICT(discard, frame, -1, -ret, NULL, NULL, NULL);
	return 0;
}

static int32_t
posix_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                off_t len, dict_t *xdata)
{
        int32_t ret                      =  0;
        struct  iatt statpre             = {0,};
        struct  iatt statpost            = {0,};

        ret = posix_do_zerofill (frame, this, fd, offset, len,
                                 &statpre, &statpost, xdata);
        if (ret < 0)
                goto err;

        STACK_UNWIND_STRICT(zerofill, frame, 0, 0, &statpre, &statpost, NULL);
        return 0;

err:
        STACK_UNWIND_STRICT(zerofill, frame, -1, -ret, NULL, NULL, NULL);
        return 0;

}

static int32_t
posix_ipc (call_frame_t *frame, xlator_t *this, int32_t op, dict_t *xdata)
{
        /*
         * IPC is for inter-translator communication.  If one gets here, it
         * means somebody sent one that nobody else recognized, which is an
         * error much like an uncaught exception.
         */
        gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_IPC_NOT_HANDLE,
                "GF_LOG_IPC(%d) not handled", op);
        STACK_UNWIND_STRICT (ipc, frame, -1, -EOPNOTSUPP, NULL);
        return 0;

}

#ifdef HAVE_SEEK_HOLE
static int32_t
posix_seek (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            gf_seek_what_t what, dict_t *xdata)
{
        struct posix_fd *pfd       = NULL;
        off_t            ret       = -1;
        int              err       = 0;
        int              whence    = 0;

        DECLARE_OLD_FS_ID_VAR;

        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        switch (what) {
        case GF_SEEK_DATA:
                whence = SEEK_DATA;
                break;
        case GF_SEEK_HOLE:
                whence = SEEK_HOLE;
                break;
        default:
                err = ENOTSUP;
                gf_msg (this->name, GF_LOG_ERROR, ENOTSUP,
                        P_MSG_SEEK_UNKOWN, "don't know what to seek");
                goto out;
        }

        ret = posix_fd_ctx_get (fd, this, &pfd, &err);
        if (ret < 0) {
                gf_msg_debug (this->name, 0, "pfd is NULL from fd=%p", fd);
                goto out;
        }

        ret = sys_lseek (pfd->fd, offset, whence);
        if (ret == -1) {
                err = errno;
                gf_msg (this->name, GF_LOG_ERROR, err, P_MSG_SEEK_FAILED,
                        "seek failed on fd %d length %" PRId64 , pfd->fd,
                        offset);
                goto out;
        }

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (seek, frame, (ret == -1 ? -1 : 0), err,
                             (ret == -1 ? -1 : ret), xdata);
        return 0;
}
#endif

int32_t
posix_opendir (call_frame_t *frame, xlator_t *this,
               loc_t *loc, fd_t *fd, dict_t *xdata)
{
        char *            real_path = NULL;
        int32_t           op_ret    = -1;
        int32_t           op_errno  = EINVAL;
        DIR *             dir       = NULL;
        struct posix_fd * pfd       = NULL;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);
        VALIDATE_OR_GOTO (fd, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_INODE_HANDLE (real_path, this, loc, NULL);
        if (!real_path) {
                op_errno = ESTALE;
                goto out;
        }

        op_ret = -1;
        dir = sys_opendir (real_path);

        if (dir == NULL) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_OPENDIR_FAILED,
                        "opendir failed on %s", real_path);
                goto out;
        }

        op_ret = dirfd (dir);
        if (op_ret < 0) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_DIRFD_FAILED,
                        "dirfd() failed on %s", real_path);
                goto out;
        }

        pfd = GF_CALLOC (1, sizeof (*pfd), gf_posix_mt_posix_fd);
        if (!pfd) {
                op_errno = errno;
                goto out;
        }

        pfd->dir = dir;
        pfd->dir_eof = -1;
        pfd->fd = op_ret;

        op_ret = fd_ctx_set (fd, this, (uint64_t)(long)pfd);
        if (op_ret)
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        P_MSG_FD_PATH_SETTING_FAILED, "failed to set the fd"
                        "context path=%s fd=%p", real_path, fd);

        op_ret = 0;

out:
        if (op_ret == -1) {
                if (dir) {
                        (void) sys_closedir (dir);
                        dir = NULL;
                }
                if (pfd) {
                        GF_FREE (pfd);
                        pfd = NULL;
                }
        }

        SET_TO_OLD_FS_ID ();
        STACK_UNWIND_STRICT (opendir, frame, op_ret, op_errno, fd, NULL);
        return 0;
}

int32_t
posix_releasedir (xlator_t *this,
                  fd_t *fd)
{
        struct posix_fd * pfd      = NULL;
        uint64_t          tmp_pfd  = 0;
        int               ret      = 0;

        struct posix_private *priv = NULL;

        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = fd_ctx_del (fd, this, &tmp_pfd);
        if (ret < 0) {
                gf_msg_debug (this->name, 0, "pfd from fd=%p is NULL", fd);
                goto out;
        }

        pfd = (struct posix_fd *)(long)tmp_pfd;
        if (!pfd->dir) {
                gf_msg (this->name, GF_LOG_WARNING, 0, P_MSG_PFD_NULL,
                        "pfd->dir is NULL for fd=%p", fd);
                goto out;
        }

        priv = this->private;

        pthread_mutex_lock (&priv->janitor_lock);
        {
                INIT_LIST_HEAD (&pfd->list);
                list_add_tail (&pfd->list, &priv->janitor_fds);
                pthread_cond_signal (&priv->janitor_cond);
        }
        pthread_mutex_unlock (&priv->janitor_lock);

out:
        return 0;
}


int32_t
posix_readlink (call_frame_t *frame, xlator_t *this,
                loc_t *loc, size_t size, dict_t *xdata)
{
        char *  dest      = NULL;
        int32_t op_ret    = -1;
        int32_t op_errno  = 0;
        char *  real_path = NULL;
        struct iatt stbuf = {0,};

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);

        dest = alloca (size + 1);

        MAKE_INODE_HANDLE (real_path, this, loc, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "lstat on %s failed",
                        loc->path ? loc->path : "<null>");
                goto out;
        }

        op_ret = sys_readlink (real_path, dest, size);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_READYLINK_FAILED,
                        "readlink on %s failed", real_path);
                goto out;
        }

        dest[op_ret] = 0;
out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (readlink, frame, op_ret, op_errno, dest, &stbuf, NULL);

        return 0;
}


int
posix_mknod (call_frame_t *frame, xlator_t *this,
             loc_t *loc, mode_t mode, dev_t dev, mode_t umask, dict_t *xdata)
{
        int                   tmp_fd          = 0;
        int32_t               op_ret          = -1;
        int32_t               op_errno        = 0;
        char                 *real_path       = 0;
        char                 *par_path        = 0;
        struct iatt           stbuf           = { 0, };
        struct posix_private *priv            = NULL;
        gid_t                 gid             = 0;
        struct iatt           preparent       = {0,};
        struct iatt           postparent      = {0,};
        void *                uuid_req        = NULL;
        int32_t               nlink_samepgfid = 0;
        char                 *pgfid_xattr_key = NULL;
        gf_boolean_t          entry_created   = _gf_false, gfid_set = _gf_false;
        gf_boolean_t          linked          = _gf_false;
        gf_loglevel_t         level           = GF_LOG_NONE;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, NULL);

        gid = frame->root->gid;

        SET_FS_ID (frame->root->uid, gid);

        if (!real_path || !par_path) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }


        op_ret = posix_pstat (this, loc->pargfid, par_path, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "pre-operation lstat on parent of %s failed",
                        real_path);
                goto out;
        }

        if (preparent.ia_prot.sgid) {
                gid = preparent.ia_gid;
        }

        /* Check if the 'gfid' already exists, because this mknod may be an
           internal call from distribute for creating 'linkfile', and that
           linkfile may be for a hardlinked file */
        if (dict_get (xdata, GLUSTERFS_INTERNAL_FOP_KEY)) {
                dict_del (xdata, GLUSTERFS_INTERNAL_FOP_KEY);
                op_ret = dict_get_ptr (xdata, "gfid-req", &uuid_req);
                if (op_ret) {
                        gf_msg_debug (this->name, 0, "failed to get the gfid from "
                                "dict for %s", loc->path);
                        goto real_op;
                }
                op_ret = posix_create_link_if_gfid_exists (this, uuid_req,
                                                           real_path,
                                                           loc->inode->table);
                if (!op_ret) {
                        linked = _gf_true;
                        goto post_op;
                }
        }

real_op:
#ifdef __NetBSD__
	if (S_ISFIFO(mode))
		op_ret = mkfifo (real_path, mode);
	else
#endif /* __NetBSD__ */
        op_ret = sys_mknod (real_path, mode, dev);

        if (op_ret == -1) {
                op_errno = errno;
                if ((op_errno == EINVAL) && S_ISREG (mode)) {
                        /* Over Darwin, mknod with (S_IFREG|mode)
                           doesn't work */
                        tmp_fd = sys_creat (real_path, mode);
                        if (tmp_fd == -1) {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        P_MSG_CREATE_FAILED, "create failed on"
                                        "%s", real_path);
                                goto out;
                        }
                        sys_close (tmp_fd);
                } else {
                        if (op_errno == EEXIST)
                                level = GF_LOG_DEBUG;
                        else
                                level = GF_LOG_ERROR;
                        gf_msg (this->name, level, errno, P_MSG_MKNOD_FAILED,
                                "mknod on %s failed", real_path);
                        goto out;
                }
        }

        entry_created = _gf_true;

#ifndef HAVE_SET_FSID
        op_ret = sys_lchown (real_path, frame->root->uid, gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LCHOWN_FAILED,
                        "lchown on %s failed", real_path);
                goto out;
        }
#endif

post_op:
        op_ret = posix_acl_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_ACL_FAILED,
                        "setting ACLs on %s failed", real_path);
        }

        if (priv->update_pgfid_nlinks) {
                MAKE_PGFID_XATTR_KEY (pgfid_xattr_key, PGFID_XATTR_KEY_PREFIX,
                                      loc->pargfid);
                nlink_samepgfid = 1;

                SET_PGFID_XATTR (real_path, pgfid_xattr_key, nlink_samepgfid,
                                 XATTR_CREATE, op_ret, this, ignore);
        }

ignore:
        op_ret = posix_entry_create_xattr_set (this, real_path, xdata);
        if (op_ret) {
                if (errno != EEXIST)
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_XATTR_FAILED,
                                "setting xattrs on %s failed", real_path);
                else
                        gf_msg_debug (this->name, 0,
                                      "setting xattrs on %s failed", real_path);
        }

        if (!linked) {
                op_ret = posix_gfid_set (this, real_path, loc, xdata);
                if (op_ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_GFID_FAILED,
                                "setting gfid on %s failed", real_path);
                } else {
                        gfid_set = _gf_true;
                }
        }

        op_ret = posix_pstat (this, NULL, real_path, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_MKNOD_FAILED,
                        "mknod on %s failed", real_path);
                goto out;
        }

        op_ret = posix_pstat (this, loc->pargfid, par_path, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "post-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno,
                             (loc)?loc->inode:NULL, &stbuf, &preparent,
                             &postparent, NULL);

        if (op_ret < 0) {
                if (entry_created) {
                        if (S_ISREG (mode))
                                sys_unlink (real_path);
                        else
                                sys_rmdir (real_path);
                }

                if (gfid_set)
                        posix_gfid_unset (this, xdata);
        }

        return 0;
}

int
posix_mkdir (call_frame_t *frame, xlator_t *this,
             loc_t *loc, mode_t mode, mode_t umask, dict_t *xdata)
{
        int32_t               op_ret          = -1;
        int32_t               op_errno        = 0;
        char                 *real_path       = NULL, *gfid_path = NULL;
        char                 *par_path        = NULL, *xattr_name = NULL;
        struct iatt           stbuf           = {0, };
        struct posix_private *priv            = NULL;
        gid_t                 gid             = 0;
        struct iatt           preparent       = {0,};
        struct iatt           postparent      = {0,};
        gf_boolean_t          entry_created   = _gf_false, gfid_set = _gf_false;
        void                 *uuid_req        = NULL;
        ssize_t               size            = 0;
        dict_t               *xdata_rsp       = NULL;
        void                 *disk_xattr      = NULL;
        data_t               *arg_data        = NULL;
        char          pgfid[GF_UUID_BUF_SIZE] = {0};

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        /* The Hidden directory should be for housekeeping purpose and it
           should not get created from a user request */
        if (__is_root_gfid (loc->pargfid) &&
            (strcmp (loc->name, GF_HIDDEN_PATH) == 0)) {
                gf_msg (this->name, GF_LOG_WARNING, EPERM,
                        P_MSG_MKDIR_NOT_PERMITTED, "mkdir issued on %s, which"
                        "is not permitted", GF_HIDDEN_PATH);
                op_errno = EPERM;
                op_ret = -1;
                goto out;
        }

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, NULL);
        if (!real_path || !par_path) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        if (loc->parent)
                gf_uuid_unparse (loc->parent->gfid, pgfid);
        else
                gf_uuid_unparse (loc->pargfid, pgfid);

        gid = frame->root->gid;

        op_ret = posix_pstat (this, NULL, real_path, &stbuf);

        SET_FS_ID (frame->root->uid, gid);

        if (xdata) {
                op_ret = dict_get_ptr (xdata, "gfid-req", &uuid_req);
                if (!op_ret && !gf_uuid_compare (stbuf.ia_gfid, uuid_req)) {
                        op_ret = -1;
                        op_errno = EEXIST;
                        goto out;
                }
        }

        if (uuid_req && !gf_uuid_is_null (uuid_req)) {
                op_ret = posix_istat (this, uuid_req, NULL, &stbuf);
                if ((op_ret == 0) && IA_ISDIR (stbuf.ia_type)) {
                        size = posix_handle_path (this, uuid_req, NULL, NULL,
                                                  0);
                        if (size > 0)
                                gfid_path = alloca (size);

                        if (gfid_path)
                                posix_handle_path (this, uuid_req, NULL,
                                                   gfid_path, size);

                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                P_MSG_DIR_OF_SAME_ID, "mkdir (%s): gfid (%s) "
                                "is already associated with directory (%s). "
                                "Hence, both directories will share same gfid "
                                "and this can lead to inconsistencies.",
                                loc->path, uuid_utoa (uuid_req),
                                gfid_path ? gfid_path : "<NULL>");

                        gf_event (EVENT_POSIX_SAME_GFID, "gfid=%s;path=%s;"
                                  "newpath=%s;brick=%s:%s",
                                  uuid_utoa (uuid_req),
                                  gfid_path ? gfid_path : "<NULL>", loc->path,
                                  priv->hostname, priv->base_path);
                }
        } else if (!uuid_req && frame->root->pid != GF_SERVER_PID_TRASH) {
                op_ret = -1;
                op_errno = EPERM;
                gf_msg_callingfn (this->name, GF_LOG_WARNING, op_errno,
                        P_MSG_NULL_GFID, "mkdir (%s): is issued without "
                        "gfid-req %p", loc->path, xdata);
                goto out;
        }

        op_ret = posix_pstat (this, loc->pargfid, par_path, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "pre-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        if (preparent.ia_prot.sgid) {
                gid = preparent.ia_gid;
                mode |= S_ISGID;
        }

        op_ret = dict_get_str (xdata, GF_PREOP_PARENT_KEY, &xattr_name);
        if (xattr_name != NULL) {
                arg_data = dict_get (xdata, xattr_name);
                if (arg_data) {
                        size = sys_lgetxattr (par_path, xattr_name, NULL, 0);
                        if (size < 0) {
                                op_ret = -1;
                                op_errno = errno;
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        P_MSG_PREOP_CHECK_FAILED,
                                        "mkdir (%s/%s): getxattr on key (%s)"
                                        " path (%s) failed ", pgfid,
                                        loc->name, xattr_name,
                                        par_path);
                                goto out;
                        }

                        disk_xattr = alloca (size);
                        if (disk_xattr == NULL) {
                                op_ret = -1;
                                op_errno = errno;
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        P_MSG_PREOP_CHECK_FAILED,
                                        "mkdir (%s/%s): alloca failed during"
                                        " preop of mkdir (%s)", pgfid,
                                        loc->name, real_path);
                                goto out;
                        }

                        size = sys_lgetxattr (par_path, xattr_name,
                                              disk_xattr, size);
                        if (size < 0) {
                                op_errno = errno;
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        P_MSG_PREOP_CHECK_FAILED,
                                        "mkdir (%s/%s): getxattr on key (%s)"
                                        " path (%s) failed (%s)", pgfid,
                                        loc->name, xattr_name,
                                        par_path, strerror (errno));
                                goto out;
                        }

                        if ((arg_data->len != size)
                            || (memcmp (arg_data->data, disk_xattr, size))) {
                                gf_msg (this->name, GF_LOG_INFO, EIO,
                                        P_MSG_PREOP_CHECK_FAILED,
                                        "mkdir (%s/%s): failing preop of "
                                        "mkdir (%s) as on-disk"
                                        " xattr value differs from argument "
                                        "value for key %s", pgfid, loc->name,
                                        real_path, xattr_name);
                                op_ret = -1;
                                op_errno = EIO;

                                xdata_rsp = dict_new ();
                                if (xdata_rsp == NULL) {
                                        gf_msg (this->name, GF_LOG_ERROR,
                                                ENOMEM,
                                                P_MSG_PREOP_CHECK_FAILED,
                                                "mkdir (%s/%s):  "
                                                "dict allocation failed", pgfid,
                                                loc->name);
                                        op_errno = ENOMEM;
                                        goto out;
                                }

                                op_errno = dict_set_int8 (xdata_rsp,
                                                          GF_PREOP_CHECK_FAILED, 1);
                                goto out;
                        }

                        dict_del (xdata, xattr_name);
                }

                dict_del (xdata, GF_PREOP_PARENT_KEY);
        }

        op_ret = sys_mkdir (real_path, mode);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_MKDIR_FAILED,
                        "mkdir of %s failed", real_path);
                goto out;
        }

        entry_created = _gf_true;

#ifndef HAVE_SET_FSID
        op_ret = sys_chown (real_path, frame->root->uid, gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_CHOWN_FAILED,
                        "chown on %s failed", real_path);
                goto out;
        }
#endif
        op_ret = posix_acl_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_ACL_FAILED,
                        "setting ACLs on %s failed ", real_path);
        }

        op_ret = posix_entry_create_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_XATTR_FAILED,
                        "setting xattrs on %s failed", real_path);
        }

        op_ret = posix_gfid_set (this, real_path, loc, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_GFID_FAILED,
                        "setting gfid on %s failed", real_path);
        } else {
                gfid_set = _gf_true;
        }

        op_ret = posix_pstat (this, NULL, real_path, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "lstat on %s failed", real_path);
                goto out;
        }

        op_ret = posix_pstat (this, loc->pargfid, par_path, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "post-operation lstat on parent of %s failed",
                        real_path);
                goto out;
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno,
                             (loc)?loc->inode:NULL, &stbuf, &preparent,
                             &postparent, xdata_rsp);

        if (op_ret < 0) {
                if (entry_created)
                        sys_rmdir (real_path);

                if (gfid_set)
                        posix_gfid_unset (this, xdata);
        }

        if (xdata_rsp)
                dict_unref (xdata_rsp);

        return 0;
}

int
posix_add_unlink_to_ctx (inode_t *inode, xlator_t *this, char *unlink_path)
{
        uint64_t ctx = GF_UNLINK_FALSE;
        int ret = 0;

        if (!unlink_path) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        P_MSG_UNLINK_FAILED,
                        "Creation of unlink entry failed for gfid: %s",
                        unlink_path);
                ret = -1;
                goto out;
        }

        ctx = GF_UNLINK_TRUE;
        ret = posix_inode_ctx_set (inode, this, ctx);
        if (ret < 0) {
                goto out;
        }

out:
        return ret;
}

int32_t
posix_move_gfid_to_unlink (xlator_t *this, uuid_t gfid, loc_t *loc)
{
        char *unlink_path = NULL;
        char *gfid_path = NULL;
        int ret = 0;
        struct posix_private    *priv_posix = NULL;

        priv_posix = (struct posix_private *) this->private;

        MAKE_HANDLE_GFID_PATH (gfid_path, this, gfid, NULL);

        POSIX_GET_FILE_UNLINK_PATH (priv_posix->base_path,
                                    loc->inode->gfid, unlink_path);
        if (!unlink_path) {
                ret = -1;
                goto out;
        }
        gf_msg_debug (this->name, 0,
                      "Moving gfid: %s to unlink_path : %s",
                      gfid_path, unlink_path);
        ret = sys_rename (gfid_path, unlink_path);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        P_MSG_UNLINK_FAILED,
                        "Creation of unlink entry failed for gfid: %s",
                        unlink_path);
                goto out;
        }
        ret = posix_add_unlink_to_ctx (loc->inode, this, unlink_path);
        if (ret < 0)
                goto out;

out:
        return ret;
}

int32_t
posix_unlink_gfid_handle_and_entry (xlator_t *this, const char *real_path,
                                    struct iatt *stbuf, int32_t *op_errno,
                                    loc_t *loc, gf_boolean_t get_link_count,
                                    dict_t *rsp_dict)
{
        int32_t                ret      = 0;
        struct iatt            prebuf   = {0,};
        gf_boolean_t           locked   = _gf_false;

        /*  Unlink the gfid_handle_first */
        if (stbuf && stbuf->ia_nlink == 1) {

                LOCK (&loc->inode->lock);

                if (loc->inode->fd_count == 0) {
                        UNLOCK (&loc->inode->lock);
                        ret = posix_handle_unset (this, stbuf->ia_gfid, NULL);
                } else {
                        UNLOCK (&loc->inode->lock);
                        ret = posix_move_gfid_to_unlink (this, stbuf->ia_gfid,
                                                         loc);
                }
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_UNLINK_FAILED, "unlink of gfid handle "
                                "failed for path:%s with gfid %s",
                                real_path, uuid_utoa (stbuf->ia_gfid));
                }
        }

        if (get_link_count) {
                LOCK (&loc->inode->lock);
                locked = _gf_true;
                ret = posix_pstat (this, loc->gfid, real_path, &prebuf);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_LSTAT_FAILED, "lstat on %s failed",
                                real_path);
                        goto err;
                }
        }

        /* Unlink the actual file */
        ret = sys_unlink (real_path);
        if (ret == -1) {
                if (op_errno)
                        *op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_UNLINK_FAILED,
                        "unlink of %s failed", real_path);
                goto err;
        }

        if (locked) {
                UNLOCK (&loc->inode->lock);
                locked = _gf_false;
        }

        ret = dict_set_uint32 (rsp_dict, GET_LINK_COUNT, prebuf.ia_nlink);
        if (ret)
                gf_msg (this->name, GF_LOG_WARNING, 0, P_MSG_SET_XDATA_FAIL,
                        "failed to set "GET_LINK_COUNT" for %s", real_path);

        return 0;

err:
        if (locked) {
                UNLOCK (&loc->inode->lock);
                locked = _gf_false;
        }
        return -1;
}

static
int32_t posix_set_iatt_in_dict (dict_t *dict, struct iatt *in_stbuf)
{
        int ret             = -1;
        struct iatt *stbuf  = NULL;
        int32_t len         = sizeof(struct iatt);

        if (!dict || !in_stbuf)
                return ret;

        stbuf = GF_CALLOC (1, len, gf_common_mt_char);
        if (!stbuf)
                return ret;

        memcpy (stbuf, in_stbuf, len);

        ret = dict_set_bin (dict, DHT_IATT_IN_XDATA_KEY, stbuf, len);
        if (ret)
                GF_FREE (stbuf);

        return ret;
}

gf_boolean_t
posix_skip_non_linkto_unlink (dict_t *xdata, loc_t *loc, char *key,
                              const char *linkto_xattr, struct iatt *stbuf,
                              const char *real_path)
{
        gf_boolean_t     skip_unlink            = _gf_false;
        gf_boolean_t     is_dht_linkto_file     = _gf_false;
        int              unlink_if_linkto       = 0;
        ssize_t          xattr_size             = -1;
        int              op_ret                 = -1;

        op_ret = dict_get_int32 (xdata, key,
                                 &unlink_if_linkto);

        if (!op_ret && unlink_if_linkto) {

                is_dht_linkto_file =  IS_DHT_LINKFILE_MODE (stbuf);
                if (!is_dht_linkto_file)
                        return _gf_true;

                LOCK (&loc->inode->lock);

                xattr_size = sys_lgetxattr (real_path, linkto_xattr, NULL, 0);

                if (xattr_size <= 0)
                        skip_unlink = _gf_true;

                UNLOCK (&loc->inode->lock);

                gf_msg ("posix", GF_LOG_INFO, 0, P_MSG_XATTR_STATUS,
                        "linkto_xattr status: %"PRIu32" for %s", skip_unlink,
                        real_path);
        }
        return skip_unlink;

}

int32_t
posix_unlink (call_frame_t *frame, xlator_t *this,
              loc_t *loc, int xflag, dict_t *xdata)
{
        int32_t                op_ret             = -1;
        int32_t                op_errno           = 0;
        char                   *real_path         = NULL;
        char                   *par_path          = NULL;
        int32_t                fd                 = -1;
        struct iatt            stbuf              = {0,};
        struct iatt            postbuf            = {0,};
        struct posix_private  *priv               = NULL;
        struct iatt            preparent          = {0,};
        struct iatt            postparent         = {0,};
        char                  *pgfid_xattr_key    = NULL;
        int32_t                nlink_samepgfid    = 0;
        int32_t                check_open_fd      = 0;
        int32_t                skip_unlink        = 0;
        int32_t                fdstat_requested   = 0;
        dict_t                *unwind_dict        = NULL;
        void                  *uuid               = NULL;
        char                   uuid_str[GF_UUID_BUF_SIZE] = {0};
        char                   gfid_str[GF_UUID_BUF_SIZE] = {0};
        gf_boolean_t           get_link_count     = _gf_false;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, &stbuf);
        if (!real_path || !par_path) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        op_ret = posix_pstat (this, loc->pargfid, par_path, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "pre-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        priv = this->private;

        op_ret = dict_get_ptr (xdata, TIER_LINKFILE_GFID, &uuid);

        if (!op_ret && gf_uuid_compare (uuid, stbuf.ia_gfid)) {
                op_errno = ENOENT;
                op_ret = -1;
                gf_uuid_unparse (uuid, uuid_str);
                gf_uuid_unparse (stbuf.ia_gfid, gfid_str);
                gf_msg_debug (this->name, op_errno, "Mismatch in gfid for path "
                              "%s. Aborting the unlink. loc->gfid = %s, "
                              "stbuf->ia_gfid = %s", real_path,
                              uuid_str, gfid_str);
                goto out;
        }

        op_ret = dict_get_int32 (xdata, DHT_SKIP_OPEN_FD_UNLINK,
                                 &check_open_fd);

        if (!op_ret && check_open_fd) {

                LOCK (&loc->inode->lock);

                if (loc->inode->fd_count) {
                        skip_unlink = 1;
                }

                UNLOCK (&loc->inode->lock);

                gf_msg (this->name, GF_LOG_INFO, 0, P_MSG_KEY_STATUS_INFO,
                        "open-fd-key-status: %"PRIu32" for %s", skip_unlink,
                        real_path);

                if (skip_unlink) {
                        op_ret = -1;
                        op_errno = EBUSY;
                        goto out;
                }
        }
        /*
         * If either of the function return true, skip_unlink.
         * If first first function itself return true,
         * we don't need to call second function, skip unlink.
         */
        skip_unlink = posix_skip_non_linkto_unlink (xdata, loc,
                                                    DHT_SKIP_NON_LINKTO_UNLINK,
                                                    DHT_LINKTO, &stbuf,
                                                    real_path);
        skip_unlink = skip_unlink || posix_skip_non_linkto_unlink (xdata, loc,
                                                    TIER_SKIP_NON_LINKTO_UNLINK,
                                                    TIER_LINKTO, &stbuf,
                                                    real_path);
        if (skip_unlink) {
                op_ret = -1;
                op_errno = EBUSY;
                goto out;
        }

        if (IA_ISREG (loc->inode->ia_type) &&
            xdata && dict_get (xdata, DHT_IATT_IN_XDATA_KEY)) {
                fdstat_requested = 1;
        }

        if (fdstat_requested ||
            (priv->background_unlink && IA_ISREG (loc->inode->ia_type))) {
                fd = sys_open (real_path, O_RDONLY, 0);
                if (fd == -1) {
                        op_ret = -1;
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_OPEN_FAILED,
                                "open of %s failed", real_path);
                        goto out;
                }
        }

        if (priv->update_pgfid_nlinks && (stbuf.ia_nlink > 1)) {
                MAKE_PGFID_XATTR_KEY (pgfid_xattr_key, PGFID_XATTR_KEY_PREFIX,
                                      loc->pargfid);
                LOCK (&loc->inode->lock);
                {
                        UNLINK_MODIFY_PGFID_XATTR (real_path, pgfid_xattr_key,
                                                   nlink_samepgfid, 0, op_ret,
                                                   this, unlock);
                }
        unlock:
                UNLOCK (&loc->inode->lock);

                if (op_ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                P_MSG_XATTR_FAILED, "modification of "
                                "parent gfid xattr failed (path:%s gfid:%s)",
                                real_path, uuid_utoa (loc->inode->gfid));
                        if (op_errno != ENOATTR)
                            /* Allow unlink if pgfid xattr is not set. */
                            goto out;
                }
        }

        unwind_dict = dict_new ();
        if (!unwind_dict) {
                op_errno = -ENOMEM;
                op_ret = -1;
                goto out;
        }

        if (xdata && dict_get (xdata, GET_LINK_COUNT))
                get_link_count = _gf_true;
        op_ret =  posix_unlink_gfid_handle_and_entry (this, real_path, &stbuf,
                                                      &op_errno, loc,
                                                      get_link_count,
                                                      unwind_dict);
        if (op_ret == -1) {
                goto out;
        }

        if (fdstat_requested) {
                op_ret = posix_fdstat (this, fd, &postbuf);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_FSTAT_FAILED, "post operation "
                                "fstat failed on fd=%d", fd);
                        goto out;
                }
                op_ret = posix_set_iatt_in_dict (unwind_dict, &postbuf);
        }

        op_ret = posix_pstat (this, loc->pargfid, par_path, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "post-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        unwind_dict = posix_dict_set_nlink (xdata, unwind_dict, stbuf.ia_nlink);
        op_ret = 0;
out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno,
                             &preparent, &postparent, unwind_dict);

        if (fd != -1) {
                sys_close (fd);
        }

        /* unref unwind_dict*/
        if (unwind_dict) {
                dict_unref (unwind_dict);
        }

        return 0;
}


int
posix_rmdir (call_frame_t *frame, xlator_t *this,
             loc_t *loc, int flags, dict_t *xdata)
{
        int32_t               op_ret     = -1;
        int32_t               op_errno   = 0;
        char                 *real_path  = NULL;
        char                 *par_path   = NULL;
        char                 *gfid_str   = NULL;
        struct iatt           preparent  = {0,};
        struct iatt           postparent = {0,};
        struct iatt           stbuf      = {0,};
        struct posix_private *priv       = NULL;
        char                  tmp_path[PATH_MAX] = {0,};

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);

        /* The Hidden directory should be for housekeeping purpose and it
           should not get deleted from inside process */
        if (__is_root_gfid (loc->pargfid) &&
            (strcmp (loc->name, GF_HIDDEN_PATH) == 0)) {
                gf_msg (this->name, GF_LOG_WARNING, EPERM,
                        P_MSG_RMDIR_NOT_PERMITTED, "rmdir issued on %s, which"
                        "is not permitted", GF_HIDDEN_PATH);
                op_errno = EPERM;
                op_ret = -1;
                goto out;
        }

        priv = this->private;

        MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, &stbuf);
        if (!real_path || !par_path) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        op_ret = posix_pstat (this, loc->pargfid, par_path, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "pre-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        if (flags) {
                gfid_str = uuid_utoa (stbuf.ia_gfid);

                op_ret = sys_mkdir (priv->trash_path, 0755);
                if (errno != EEXIST && op_ret == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_MKDIR_FAILED,
                                "mkdir of %s failed", priv->trash_path);
                } else {
                        (void) snprintf (tmp_path, sizeof(tmp_path), "%s/%s",
                                         priv->trash_path, gfid_str);
                        op_ret = sys_rename (real_path, tmp_path);
                        pthread_cond_signal (&priv->janitor_cond);
                }
        } else {
                op_ret = sys_rmdir (real_path);
        }
        op_errno = errno;

        if (op_ret == 0) {
                posix_handle_unset (this, stbuf.ia_gfid, NULL);
        }

        if (op_errno == EEXIST)
                /* Solaris sets errno = EEXIST instead of ENOTEMPTY */
                op_errno = ENOTEMPTY;

        /* No need to log a common error as ENOTEMPTY */
        if (op_ret == -1 && op_errno != ENOTEMPTY) {
                gf_msg (this->name, GF_LOG_ERROR, op_errno, P_MSG_RMDIR_FAILED,
                        "rmdir of %s failed", real_path);
        }

        if (op_ret == -1) {
                if (op_errno == ENOTEMPTY) {
                        gf_msg_debug (this->name, 0, "%s on %s failed", (flags)
                                      ? "rename" : "rmdir", real_path);
                } else {
                        gf_msg (this->name, GF_LOG_ERROR, op_errno,
                                P_MSG_DIR_OPERATION_FAILED, "%s on %s failed",
                                (flags) ? "rename" : "rmdir", real_path);
                }
                goto out;
        }

        op_ret = posix_pstat (this, loc->pargfid, par_path, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "post-operation lstat on parent of %s failed",
                        par_path);
                goto out;
        }

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno,
                             &preparent, &postparent, NULL);

        return 0;
}


int
posix_symlink (call_frame_t *frame, xlator_t *this,
               const char *linkname, loc_t *loc, mode_t umask, dict_t *xdata)
{
        int32_t               op_ret          = -1;
        int32_t               op_errno        = 0;
        char *                real_path       = 0;
        char *                par_path        = 0;
        struct iatt           stbuf           = { 0, };
        struct posix_private *priv            = NULL;
        gid_t                 gid             = 0;
        struct iatt           preparent       = {0,};
        struct iatt           postparent      = {0,};
        char                 *pgfid_xattr_key = NULL;
        int32_t               nlink_samepgfid = 0;
        gf_boolean_t          entry_created   = _gf_false, gfid_set = _gf_false;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (linkname, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, &stbuf);

        gid = frame->root->gid;
        if (!real_path || !par_path) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        SET_FS_ID (frame->root->uid, gid);

        op_ret = posix_pstat (this, loc->pargfid, par_path, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "pre-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        if (preparent.ia_prot.sgid) {
                gid = preparent.ia_gid;
        }

        op_ret = sys_symlink (linkname, real_path);

        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_SYMLINK_FAILED,
                        "symlink of %s --> %s failed",
                        real_path, linkname);
                goto out;
        }

        entry_created = _gf_true;

#ifndef HAVE_SET_FSID
        op_ret = sys_lchown (real_path, frame->root->uid, gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LCHOWN_FAILED,
                        "lchown failed on %s", real_path);
                goto out;
        }
#endif
        op_ret = posix_acl_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_ACL_FAILED,
                        "setting ACLs on %s failed", real_path);
        }

        if (priv->update_pgfid_nlinks) {
                MAKE_PGFID_XATTR_KEY (pgfid_xattr_key, PGFID_XATTR_KEY_PREFIX,
                                      loc->pargfid);
                nlink_samepgfid = 1;
                SET_PGFID_XATTR (real_path, pgfid_xattr_key, nlink_samepgfid,
                                 XATTR_CREATE, op_ret, this, ignore);
        }
ignore:
        op_ret = posix_entry_create_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_XATTR_FAILED,
                        "setting xattrs on %s failed ", real_path);
        }

        op_ret = posix_gfid_set (this, real_path, loc, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_GFID_FAILED,
                        "setting gfid on %s failed", real_path);
        } else {
                gfid_set = _gf_true;
        }

        op_ret = posix_pstat (this, NULL, real_path, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "lstat failed on %s", real_path);
                goto out;
        }

        op_ret = posix_pstat (this, loc->pargfid, par_path, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "post-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno,
                             (loc)?loc->inode:NULL, &stbuf, &preparent,
                             &postparent, NULL);

        if (op_ret < 0) {
                if (entry_created)
                        sys_unlink (real_path);

                if (gfid_set)
                        posix_gfid_unset (this, xdata);
        }

        return 0;
}


int
posix_rename (call_frame_t *frame, xlator_t *this,
              loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        int32_t               op_ret          = -1;
        int32_t               op_errno        = 0;
        char                 *real_oldpath    = NULL;
        char                 *real_newpath    = NULL;
        char                 *par_oldpath     = NULL;
        char                 *par_newpath     = NULL;
        struct iatt           stbuf           = {0, };
        struct posix_private *priv            = NULL;
        char                  was_present     = 1;
        struct iatt           preoldparent    = {0, };
        struct iatt           postoldparent   = {0, };
        struct iatt           prenewparent    = {0, };
        struct iatt           postnewparent   = {0, };
        char                  olddirid[64];
        char                  newdirid[64];
        uuid_t                victim          = {0};
        int                   was_dir         = 0;
        int                   nlink           = 0;
        char                 *pgfid_xattr_key = NULL;
        int32_t               nlink_samepgfid = 0;
        dict_t               *unwind_dict     = NULL;
        gf_boolean_t          locked          = _gf_false;
        gf_boolean_t          get_link_count  = _gf_false;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (oldloc, out);
        VALIDATE_OR_GOTO (newloc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_ENTRY_HANDLE (real_oldpath, par_oldpath, this, oldloc, NULL);
        if (!real_oldpath || !par_oldpath) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        MAKE_ENTRY_HANDLE (real_newpath, par_newpath, this, newloc, &stbuf);
        if (!real_newpath || !par_newpath) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        unwind_dict = dict_new ();
        if (!unwind_dict) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        op_ret = posix_pstat (this, oldloc->pargfid, par_oldpath, &preoldparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "pre-operation lstat on parent %s failed",
                        par_oldpath);
                goto out;
        }

        op_ret = posix_pstat (this, newloc->pargfid, par_newpath, &prenewparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "pre-operation lstat on parent of %s failed",
                        par_newpath);
                goto out;
        }

        op_ret = posix_pstat (this, NULL, real_newpath, &stbuf);
        if ((op_ret == -1) && (errno == ENOENT)){
                was_present = 0;
        } else {
                gf_uuid_copy (victim, stbuf.ia_gfid);
                if (IA_ISDIR (stbuf.ia_type))
                        was_dir = 1;
                nlink = stbuf.ia_nlink;
        }

        if (was_present && IA_ISDIR(stbuf.ia_type) && !newloc->inode) {
                gf_msg (this->name, GF_LOG_WARNING, EEXIST, P_MSG_DIR_FOUND,
                        "found directory at %s while expecting ENOENT",
                        real_newpath);
                op_ret = -1;
                op_errno = EEXIST;
                goto out;
        }

        if (was_present && IA_ISDIR(stbuf.ia_type) &&
            gf_uuid_compare (newloc->inode->gfid, stbuf.ia_gfid)) {
                gf_msg (this->name, GF_LOG_WARNING, EEXIST, P_MSG_DIR_FOUND,
                        "found directory %s at %s while renaming %s",
                        uuid_utoa_r (newloc->inode->gfid, olddirid),
                        real_newpath,
                        uuid_utoa_r (stbuf.ia_gfid, newdirid));
                op_ret = -1;
                op_errno = EEXIST;
                goto out;
        }

        if (IA_ISDIR (oldloc->inode->ia_type))
                posix_handle_unset (this, oldloc->inode->gfid, NULL);

        LOCK (&oldloc->inode->lock);
        {
                if (!IA_ISDIR (oldloc->inode->ia_type)
                    && priv->update_pgfid_nlinks) {
                        MAKE_PGFID_XATTR_KEY (pgfid_xattr_key,
                                              PGFID_XATTR_KEY_PREFIX,
                                              oldloc->pargfid);
                        UNLINK_MODIFY_PGFID_XATTR (real_oldpath,
                                                   pgfid_xattr_key,
                                                   nlink_samepgfid, 0,
                                                   op_ret,
                                                   this, unlock);
                }

                if ((xdata) && (dict_get (xdata, GET_LINK_COUNT))
                    && (real_newpath) && (was_present)) {
                        LOCK (&newloc->inode->lock);
                        locked = _gf_true;
                        get_link_count = _gf_true;
                        op_ret = posix_pstat (this, newloc->gfid, real_newpath,
                                              &stbuf);
                        if ((op_ret == -1) && (errno != ENOENT)) {
                                op_errno = errno;
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        P_MSG_LSTAT_FAILED,
                                        "lstat on %s failed", real_newpath);
                                goto unlock;
                        }
                }

                op_ret = sys_rename (real_oldpath, real_newpath);
                if (op_ret == -1) {
                        op_errno = errno;
                        if (op_errno == ENOTEMPTY) {
                                gf_msg_debug (this->name, 0, "rename of %s to"
                                              " %s failed: %s", real_oldpath,
                                              real_newpath,
                                              strerror (op_errno));
                        } else {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        P_MSG_RENAME_FAILED,
                                        "rename of %s to %s failed",
                                        real_oldpath, real_newpath);
                       }

                        if (priv->update_pgfid_nlinks
                            && !IA_ISDIR (oldloc->inode->ia_type)) {
                                LINK_MODIFY_PGFID_XATTR (real_oldpath,
                                                         pgfid_xattr_key,
                                                         nlink_samepgfid, 0,
                                                         op_ret,
                                                         this, unlock);
                        }

                        goto unlock;
                }

                if (locked) {
                        UNLOCK (&newloc->inode->lock);
                        locked = _gf_false;
                }

                if ((get_link_count) &&
                    (dict_set_uint32 (unwind_dict, GET_LINK_COUNT,
                                      stbuf.ia_nlink)))
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                P_MSG_SET_XDATA_FAIL, "failed to set "
                                GET_LINK_COUNT" for %s", real_newpath);

                if (!IA_ISDIR (oldloc->inode->ia_type)
                    && priv->update_pgfid_nlinks) {
                        MAKE_PGFID_XATTR_KEY (pgfid_xattr_key,
                                              PGFID_XATTR_KEY_PREFIX,
                                              newloc->pargfid);
                        LINK_MODIFY_PGFID_XATTR (real_newpath,
                                                 pgfid_xattr_key,
                                                 nlink_samepgfid, 0,
                                                 op_ret,
                                                 this, unlock);
                }
        }
unlock:
        if (locked) {
                UNLOCK (&newloc->inode->lock);
                locked = _gf_false;
        }
        UNLOCK (&oldloc->inode->lock);

        if (op_ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0, P_MSG_XATTR_FAILED,
                        "modification of "
                        "parent gfid xattr failed (gfid:%s)",
                        uuid_utoa (oldloc->inode->gfid));
                goto out;
        }

        if (was_dir)
                posix_handle_unset (this, victim, NULL);

        if (was_present && !was_dir && nlink == 1)
                posix_handle_unset (this, victim, NULL);

        if (IA_ISDIR (oldloc->inode->ia_type)) {
                posix_handle_soft (this, real_newpath, newloc,
                                   oldloc->inode->gfid, NULL);
        }

        op_ret = posix_pstat (this, NULL, real_newpath, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "lstat on %s failed", real_newpath);
                goto out;
        }

        op_ret = posix_pstat (this, oldloc->pargfid, par_oldpath, &postoldparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "post-operation lstat on parent %s failed",
                        par_oldpath);
                goto out;
        }

        op_ret = posix_pstat (this, newloc->pargfid, par_newpath, &postnewparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "post-operation lstat on parent %s failed",
                        par_newpath);
                goto out;
        }

        if (was_present)
                unwind_dict = posix_dict_set_nlink (xdata, unwind_dict, nlink);
        op_ret = 0;
out:

        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, &stbuf,
                             &preoldparent, &postoldparent,
                             &prenewparent, &postnewparent, unwind_dict);

        if (unwind_dict)
                dict_unref (unwind_dict);

        return 0;
}


int
posix_link (call_frame_t *frame, xlator_t *this,
            loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        int32_t               op_ret          = -1;
        int32_t               op_errno        = 0;
        char                 *real_oldpath    = 0;
        char                 *real_newpath    = 0;
        char                 *par_newpath     = 0;
        struct iatt           stbuf           = {0, };
        struct posix_private *priv            = NULL;
        struct iatt           preparent       = {0,};
        struct iatt           postparent      = {0,};
        int32_t               nlink_samepgfid = 0;
        char                 *pgfid_xattr_key = NULL;
        gf_boolean_t          entry_created   = _gf_false;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (oldloc, out);
        VALIDATE_OR_GOTO (newloc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_INODE_HANDLE (real_oldpath, this, oldloc, &stbuf);
        if (!real_oldpath) {
                op_errno = errno;
                goto out;
        }

        MAKE_ENTRY_HANDLE (real_newpath, par_newpath, this, newloc, &stbuf);
        if (!real_newpath || !par_newpath) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        op_ret = posix_pstat (this, newloc->pargfid, par_newpath, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "lstat failed: %s", par_newpath);
                goto out;
        }


        op_ret = sys_link (real_oldpath, real_newpath);

        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LINK_FAILED,
                        "link %s to %s failed",
                        real_oldpath, real_newpath);
                goto out;
        }

        entry_created = _gf_true;

        op_ret = posix_pstat (this, NULL, real_newpath, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "lstat on %s failed", real_newpath);
                goto out;
        }

        op_ret = posix_pstat (this, newloc->pargfid, par_newpath, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "lstat failed: %s", par_newpath);
                goto out;
        }

        if (priv->update_pgfid_nlinks) {
                MAKE_PGFID_XATTR_KEY (pgfid_xattr_key, PGFID_XATTR_KEY_PREFIX,
                                      newloc->pargfid);

                LOCK (&newloc->inode->lock);
                {
                        LINK_MODIFY_PGFID_XATTR (real_newpath, pgfid_xattr_key,
                                                 nlink_samepgfid, 0, op_ret,
                                                 this, unlock);
                }
        unlock:
                UNLOCK (&newloc->inode->lock);

                if (op_ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                P_MSG_XATTR_FAILED, "modification of "
                                "parent gfid xattr failed (path:%s gfid:%s)",
                                real_newpath, uuid_utoa (newloc->inode->gfid));
                        goto out;
                }
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (link, frame, op_ret, op_errno,
                             (oldloc)?oldloc->inode:NULL, &stbuf, &preparent,
                             &postparent, NULL);

        if (op_ret < 0) {
                if (entry_created)
                        sys_unlink (real_newpath);
        }

        return 0;
}


int32_t
posix_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
                dict_t *xdata)
{
        int32_t               op_ret    = -1;
        int32_t               op_errno  = 0;
        char                 *real_path = 0;
        struct posix_private *priv      = NULL;
        struct iatt           prebuf    = {0,};
        struct iatt           postbuf   = {0,};

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);

        MAKE_INODE_HANDLE (real_path, this, loc, &prebuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "pre-operation lstat on %s failed",
                        real_path ? real_path : "<null>");
                goto out;
        }

        op_ret = sys_truncate (real_path, offset);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_TRUNCATE_FAILED,
                        "truncate on %s failed", real_path);
                goto out;
        }

        op_ret = posix_pstat (this, loc->gfid, real_path, &postbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "lstat on %s failed", real_path);
                goto out;
        }

        op_ret = 0;
out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno,
                             &prebuf, &postbuf, NULL);

        return 0;
}


int
posix_create (call_frame_t *frame, xlator_t *this,
              loc_t *loc, int32_t flags, mode_t mode,
              mode_t umask, fd_t *fd, dict_t *xdata)
{
        int32_t                op_ret          = -1;
        int32_t                op_errno        = 0;
        int32_t                _fd             = -1;
        int                    _flags          = 0;
        char *                 real_path       = NULL;
        char *                 par_path        = NULL;
        struct iatt            stbuf           = {0, };
        struct posix_fd *      pfd             = NULL;
        struct posix_private * priv            = NULL;
        char                   was_present     = 1;

        gid_t                  gid             = 0;
        struct iatt            preparent       = {0,};
        struct iatt            postparent      = {0,};

        int                    nlink_samepgfid = 0;
        char *                 pgfid_xattr_key = NULL;
        gf_boolean_t           entry_created   = _gf_false, gfid_set = _gf_false;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (loc, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, &stbuf);

        gid = frame->root->gid;

        SET_FS_ID (frame->root->uid, gid);
        if (!real_path || !par_path) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        op_ret = posix_pstat (this, loc->pargfid, par_path, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "pre-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        if (preparent.ia_prot.sgid) {
                gid = preparent.ia_gid;
        }

        if (!flags) {
                _flags = O_CREAT | O_RDWR | O_EXCL;
        }
        else {
                _flags = flags | O_CREAT;
        }

        op_ret = posix_pstat (this, NULL, real_path, &stbuf);
        if ((op_ret == -1) && (errno == ENOENT)) {
                was_present = 0;
        }

        if (priv->o_direct)
                _flags |= O_DIRECT;

        _fd = sys_open (real_path, _flags, mode);

        if (_fd == -1) {
                op_errno = errno;
                op_ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_OPEN_FAILED,
                        "open on %s failed", real_path);
                goto out;
        }

        if ((_flags & O_CREAT) && (_flags & O_EXCL)) {
                entry_created = _gf_true;
        }


        if (was_present)
                goto fill_stat;

#ifndef HAVE_SET_FSID
        op_ret = sys_chown (real_path, frame->root->uid, gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_CHOWN_FAILED,
                        "chown on %s failed", real_path);
        }
#endif
        op_ret = posix_acl_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_ACL_FAILED,
                        "setting ACLs on %s failed", real_path);
        }

        if (priv->update_pgfid_nlinks) {
                MAKE_PGFID_XATTR_KEY (pgfid_xattr_key, PGFID_XATTR_KEY_PREFIX,
                                      loc->pargfid);
                nlink_samepgfid = 1;
                SET_PGFID_XATTR (real_path, pgfid_xattr_key, nlink_samepgfid,
                                 XATTR_CREATE, op_ret, this, ignore);
        }
ignore:
        op_ret = posix_entry_create_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_XATTR_FAILED,
                        "setting xattrs on %s failed ", real_path);
        }

fill_stat:
        op_ret = posix_gfid_set (this, real_path, loc, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_GFID_FAILED,
                        "setting gfid on %s failed", real_path);
        } else {
                gfid_set = _gf_true;
        }

        op_ret = posix_fdstat (this, _fd, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_FSTAT_FAILED,
                        "fstat on %d failed", _fd);
                goto out;
        }

        op_ret = posix_pstat (this, loc->pargfid, par_path, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "post-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        op_ret = -1;
        pfd = GF_CALLOC (1, sizeof (*pfd), gf_posix_mt_posix_fd);
        if (!pfd) {
                op_errno = errno;
                goto out;
        }

        pfd->flags = flags;
        pfd->fd    = _fd;

        op_ret = fd_ctx_set (fd, this, (uint64_t)(long)pfd);
        if (op_ret)
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        P_MSG_FD_PATH_SETTING_FAILED,
                        "failed to set the fd context path=%s fd=%p",
                        real_path, fd);

        LOCK (&priv->lock);
        {
                priv->nr_files++;
        }
        UNLOCK (&priv->lock);

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        if ((-1 == op_ret) && (_fd != -1)) {
                sys_close (_fd);
        }

        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno,
                             fd, (loc)?loc->inode:NULL, &stbuf, &preparent,
                             &postparent, xdata);

        if (op_ret < 0) {
                if (entry_created)
                        sys_unlink (real_path);

                if (gfid_set)
                        posix_gfid_unset (this, xdata);
        }

        return 0;
}

int32_t
posix_open (call_frame_t *frame, xlator_t *this,
            loc_t *loc, int32_t flags, fd_t *fd, dict_t *xdata)
{
        int32_t               op_ret       = -1;
        int32_t               op_errno     = 0;
        char                 *real_path    = NULL;
        int32_t               _fd          = -1;
        struct posix_fd      *pfd          = NULL;
        struct posix_private *priv         = NULL;
        struct iatt           stbuf        = {0, };

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (loc, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        MAKE_INODE_HANDLE (real_path, this, loc, &stbuf);
        if (!real_path) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        if (IA_ISLNK (stbuf.ia_type)) {
                op_ret = -1;
                op_errno = ELOOP;
                goto out;
        }

        op_ret = -1;
        SET_FS_ID (frame->root->uid, frame->root->gid);

        if (priv->o_direct)
                flags |= O_DIRECT;

        _fd = sys_open (real_path, flags, 0);
        if (_fd == -1) {
                op_ret   = -1;
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_FILE_OP_FAILED,
                        "open on %s, flags: %d", real_path, flags);
                goto out;
        }

        pfd = GF_CALLOC (1, sizeof (*pfd), gf_posix_mt_posix_fd);
        if (!pfd) {
                op_errno = errno;
                goto out;
        }

        pfd->flags = flags;
        pfd->fd    = _fd;

        op_ret = fd_ctx_set (fd, this, (uint64_t)(long)pfd);
        if (op_ret)
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        P_MSG_FD_PATH_SETTING_FAILED,
                        "failed to set the fd context path=%s fd=%p",
                        real_path, fd);

        LOCK (&priv->lock);
        {
                priv->nr_files++;
        }
        UNLOCK (&priv->lock);

        op_ret = 0;

out:
        if (op_ret == -1) {
                if (_fd != -1) {
                        sys_close (_fd);
                }
        }

        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd, NULL);

        return 0;
}

int
posix_readv (call_frame_t *frame, xlator_t *this,
             fd_t *fd, size_t size, off_t offset, uint32_t flags, dict_t *xdata)
{
        int32_t                op_ret     = -1;
        int32_t                op_errno   = 0;
        int                    _fd        = -1;
        struct posix_private * priv       = NULL;
        struct iobuf         * iobuf      = NULL;
        struct iobref        * iobref     = NULL;
        struct iovec           vec        = {0,};
        struct posix_fd *      pfd        = NULL;
        struct iatt            stbuf      = {0,};
        int                    ret        = -1;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        ret = posix_fd_ctx_get (fd, this, &pfd, &op_errno);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, op_errno, P_MSG_PFD_NULL,
                        "pfd is NULL from fd=%p", fd);
                goto out;
        }

        if (!size) {
                op_errno = EINVAL;
                gf_msg (this->name, GF_LOG_WARNING, EINVAL,
                        P_MSG_INVALID_ARGUMENT, "size=%"GF_PRI_SIZET, size);
                goto out;
        }

        iobuf = iobuf_get_page_aligned (this->ctx->iobuf_pool, size,
                                        ALIGN_SIZE);
        if (!iobuf) {
                op_errno = ENOMEM;
                goto out;
        }

        _fd = pfd->fd;
        op_ret = sys_pread (_fd, iobuf->ptr, size, offset);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        P_MSG_READ_FAILED, "read failed on gfid=%s, "
                        "fd=%p, offset=%"PRIu64" size=%"GF_PRI_SIZET", "
                        "buf=%p", uuid_utoa (fd->inode->gfid), fd,
                        offset, size, iobuf->ptr);
                goto out;
        }

        LOCK (&priv->lock);
        {
                priv->read_value    += op_ret;
        }
        UNLOCK (&priv->lock);

        vec.iov_base = iobuf->ptr;
        vec.iov_len  = op_ret;

        iobref = iobref_new ();

        iobref_add (iobref, iobuf);

        /*
         *  readv successful, and we need to get the stat of the file
         *  we read from
         */

        op_ret = posix_fdstat (this, _fd, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_FSTAT_FAILED,
                        "fstat failed on fd=%p", fd);
                goto out;
        }

        /* Hack to notify higher layers of EOF. */
        if (!stbuf.ia_size || (offset + vec.iov_len) >= stbuf.ia_size)
                op_errno = ENOENT;

        op_ret = vec.iov_len;
out:

        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno,
                             &vec, 1, &stbuf, iobref, NULL);

        if (iobref)
                iobref_unref (iobref);
        if (iobuf)
                iobuf_unref (iobuf);

        return 0;
}


int32_t
__posix_pwritev (int fd, struct iovec *vector, int count, off_t offset)
{
        int32_t         op_ret = 0;
        int             idx = 0;
        int             retval = 0;
        off_t           internal_off = 0;

        if (!vector)
                return -EFAULT;

        internal_off = offset;
        for (idx = 0; idx < count; idx++) {
                retval = sys_pwrite (fd, vector[idx].iov_base, vector[idx].iov_len,
                                 internal_off);
                if (retval == -1) {
                        op_ret = -errno;
                        goto err;
                }
                op_ret += retval;
                internal_off += retval;
        }

err:
        return op_ret;
}

int32_t
__posix_writev (int fd, struct iovec *vector, int count, off_t startoff,
                int odirect)
{
        int32_t         op_ret = 0;
        int             idx = 0;
        int             max_buf_size = 0;
        int             retval = 0;
        char            *buf = NULL;
        char            *alloc_buf = NULL;
        off_t           internal_off = 0;

        /* Check for the O_DIRECT flag during open() */
        if (!odirect)
                return __posix_pwritev (fd, vector, count, startoff);

        for (idx = 0; idx < count; idx++) {
                if (max_buf_size < vector[idx].iov_len)
                        max_buf_size = vector[idx].iov_len;
        }

        alloc_buf = _page_aligned_alloc (max_buf_size, &buf);
        if (!alloc_buf) {
                op_ret = -errno;
                goto err;
        }

        internal_off = startoff;
        for (idx = 0; idx < count; idx++) {
                memcpy (buf, vector[idx].iov_base, vector[idx].iov_len);

                /* not sure whether writev works on O_DIRECT'd fd */
                retval = sys_pwrite (fd, buf, vector[idx].iov_len, internal_off);
                if (retval == -1) {
                        op_ret = -errno;
                        goto err;
                }

                op_ret += retval;
                internal_off += retval;
        }

err:
        GF_FREE (alloc_buf);

        return op_ret;
}

dict_t*
_fill_writev_xdata (fd_t *fd, dict_t *xdata, xlator_t *this, int is_append)
{
        dict_t  *rsp_xdata = NULL;
        int32_t ret = 0;
        inode_t *inode = NULL;

        if (fd)
                inode = fd->inode;

        if (!fd || !fd->inode || gf_uuid_is_null (fd->inode->gfid)) {
                gf_msg_callingfn (this->name, GF_LOG_ERROR, EINVAL,
                                  P_MSG_XATTR_FAILED, "fd: %p inode: %p"
                                  "gfid:%s", fd, inode?inode:0,
                                  inode?uuid_utoa(inode->gfid):"N/A");
                goto out;
        }

        if (!xdata)
                goto out;

        rsp_xdata = dict_new();
        if (!rsp_xdata)
                goto out;

        if (dict_get (xdata, GLUSTERFS_OPEN_FD_COUNT)) {
                ret = dict_set_uint32 (rsp_xdata, GLUSTERFS_OPEN_FD_COUNT,
                                       fd->inode->fd_count);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                P_MSG_DICT_SET_FAILED, "%s: Failed to set "
                                "dictionary value for %s",
                                uuid_utoa (fd->inode->gfid),
                                GLUSTERFS_OPEN_FD_COUNT);
                }
        }

        if (dict_get (xdata, GLUSTERFS_WRITE_IS_APPEND)) {
                ret = dict_set_uint32 (rsp_xdata, GLUSTERFS_WRITE_IS_APPEND,
                                       is_append);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                P_MSG_DICT_SET_FAILED, "%s: Failed to set "
                                "dictionary value for %s",
                                uuid_utoa (fd->inode->gfid),
                                GLUSTERFS_WRITE_IS_APPEND);
                }
        }
out:
        return rsp_xdata;
}

int32_t
posix_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
              struct iovec *vector, int32_t count, off_t offset,
              uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        int32_t                op_ret   = -1;
        int32_t                op_errno = 0;
        int                    _fd      = -1;
        struct posix_private * priv     = NULL;
        struct posix_fd *      pfd      = NULL;
        struct iatt            preop    = {0,};
        struct iatt            postop    = {0,};
        int                      ret      = -1;
        dict_t                *rsp_xdata = NULL;
	int                    is_append = 0;
	gf_boolean_t           locked = _gf_false;
	gf_boolean_t           write_append = _gf_false;
	gf_boolean_t           update_atomic = _gf_false;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (vector, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        VALIDATE_OR_GOTO (priv, out);

        ret = posix_fd_ctx_get (fd, this, &pfd, &op_errno);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, ret, P_MSG_PFD_NULL,
                        "pfd is NULL from fd=%p", fd);
                op_errno = -ret;
                goto out;
        }

        _fd = pfd->fd;

        if (xdata) {
                if (dict_get (xdata, GLUSTERFS_WRITE_IS_APPEND))
                        write_append = _gf_true;
                if (dict_get (xdata, GLUSTERFS_WRITE_UPDATE_ATOMIC))
                        update_atomic = _gf_true;
	}

        /* The write_is_append check and write must happen
           atomically. Else another write can overtake this
           write after the check and get written earlier.

           So lock before preop-stat and unlock after write.
        */

        /*
         * The update_atomic option is to instruct posix to do prestat,
         * write and poststat atomically. This is to prevent any modification to
         * ia_size and ia_blocks until poststat and the diff in their values
         * between pre and poststat could be of use for some translators (shard
         * as of today).
         */

        if (write_append || update_atomic) {
		locked = _gf_true;
		LOCK(&fd->inode->lock);
        }

        op_ret = posix_fdstat (this, _fd, &preop);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_FSTAT_FAILED,
                        "pre-operation fstat failed on fd=%p", fd);
                goto out;
        }

	if (locked && write_append) {
		if (preop.ia_size == offset || (fd->flags & O_APPEND))
			is_append = 1;
	}

        op_ret = __posix_writev (_fd, vector, count, offset,
                                 (pfd->flags & O_DIRECT));

	if (locked && (!update_atomic)) {
		UNLOCK (&fd->inode->lock);
		locked = _gf_false;
	}

        if (op_ret < 0) {
                op_errno = -op_ret;
                op_ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, op_errno, P_MSG_WRITE_FAILED,
                        "write failed: offset %"PRIu64
                        ",", offset);
                goto out;
        }

        rsp_xdata = _fill_writev_xdata (fd, xdata, this, is_append);
        /* writev successful, we also need to get the stat of
         * the file we wrote to
         */

        ret = posix_fdstat (this, _fd, &postop);
        if (ret == -1) {
                op_ret = -1;
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        P_MSG_FSTAT_FAILED,
                        "post-operation fstat failed on fd=%p",
                        fd);
                goto out;
        }

	if (locked) {
		UNLOCK (&fd->inode->lock);
		locked = _gf_false;
	}

        if (flags & (O_SYNC|O_DSYNC)) {
                ret = sys_fsync (_fd);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_WRITEV_FAILED,
                                "fsync() in writev on fd %d failed",
                                _fd);
                        op_ret = -1;
                        op_errno = errno;
                        goto out;
                }
        }

        LOCK (&priv->lock);
        {
                priv->write_value    += op_ret;
        }
        UNLOCK (&priv->lock);

out:

	if (locked) {
		UNLOCK (&fd->inode->lock);
		locked = _gf_false;
	}

        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, &preop, &postop,
                             rsp_xdata);

        if (rsp_xdata)
                dict_unref (rsp_xdata);
        return 0;
}


int32_t
posix_statfs (call_frame_t *frame, xlator_t *this,
              loc_t *loc, dict_t *xdata)
{
        char *                 real_path = NULL;
        int32_t                op_ret    = -1;
        int32_t                op_errno  = 0;
        struct statvfs         buf       = {0, };
        struct posix_private * priv      = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);
        VALIDATE_OR_GOTO (this->private, out);

        MAKE_INODE_HANDLE (real_path, this, loc, NULL);
        if (!real_path) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        priv = this->private;

        op_ret = sys_statvfs (real_path, &buf);

        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_STATVFS_FAILED,
                        "statvfs failed on %s", real_path);
                goto out;
        }

        if (!priv->export_statfs) {
                buf.f_blocks = 0;
                buf.f_bfree  = 0;
                buf.f_bavail = 0;
                buf.f_files  = 0;
                buf.f_ffree  = 0;
                buf.f_favail = 0;
        }

        op_ret = 0;

out:
        STACK_UNWIND_STRICT (statfs, frame, op_ret, op_errno, &buf, NULL);
        return 0;
}


int32_t
posix_flush (call_frame_t *frame, xlator_t *this,
             fd_t *fd, dict_t *xdata)
{
        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;
        int               ret      = -1;
        struct posix_fd  *pfd      = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = posix_fd_ctx_get (fd, this, &pfd, &op_errno);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, op_errno, P_MSG_PFD_NULL,
                        "pfd is NULL on fd=%p", fd);
                goto out;
        }

        op_ret = 0;

out:
        STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno, NULL);

        return 0;
}


int32_t
posix_release (xlator_t *this, fd_t *fd)
{
        struct posix_private * priv     = NULL;
        struct posix_fd *      pfd      = NULL;
        int                    ret      = -1;
        uint64_t               tmp_pfd  = 0;

        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;

        ret = fd_ctx_del (fd, this, &tmp_pfd);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0, P_MSG_PFD_NULL,
                        "pfd is NULL from fd=%p", fd);
                goto out;
        }
        pfd = (struct posix_fd *)(long)tmp_pfd;

        if (pfd->dir) {
                gf_msg (this->name, GF_LOG_WARNING, 0, P_MSG_DIR_NOT_NULL,
                        "pfd->dir is %p (not NULL) for file fd=%p",
                        pfd->dir, fd);
        }

        pthread_mutex_lock (&priv->janitor_lock);
        {
                INIT_LIST_HEAD (&pfd->list);
                list_add_tail (&pfd->list, &priv->janitor_fds);
                pthread_cond_signal (&priv->janitor_cond);
        }
        pthread_mutex_unlock (&priv->janitor_lock);

        LOCK (&priv->lock);
        {
                priv->nr_files--;
        }
        UNLOCK (&priv->lock);

out:
        return 0;
}


int
posix_batch_fsync (call_frame_t *frame, xlator_t *this,
		     fd_t *fd, int datasync, dict_t *xdata)
{
	call_stub_t *stub = NULL;
	struct posix_private *priv = NULL;

	priv = this->private;

	stub = fop_fsync_stub (frame, default_fsync, fd, datasync, xdata);
	if (!stub) {
		STACK_UNWIND_STRICT (fsync, frame, -1, ENOMEM, 0, 0, 0);
		return 0;
	}

	pthread_mutex_lock (&priv->fsync_mutex);
	{
		list_add_tail (&stub->list, &priv->fsyncs);
		priv->fsync_queue_count++;
		pthread_cond_signal (&priv->fsync_cond);
	}
	pthread_mutex_unlock (&priv->fsync_mutex);

	return 0;
}


int32_t
posix_fsync (call_frame_t *frame, xlator_t *this,
             fd_t *fd, int32_t datasync, dict_t *xdata)
{
        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;
        int               _fd      = -1;
        struct posix_fd * pfd      = NULL;
        int               ret      = -1;
        struct iatt       preop = {0,};
        struct iatt       postop = {0,};
        struct posix_private *priv = NULL;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);

#ifdef GF_DARWIN_HOST_OS
        /* Always return success in case of fsync in MAC OS X */
        op_ret = 0;
        goto out;
#endif

	priv = this->private;
	if (priv->batch_fsync_mode && xdata && dict_get (xdata, "batch-fsync")) {
		posix_batch_fsync (frame, this, fd, datasync, xdata);
		return 0;
	}

        ret = posix_fd_ctx_get (fd, this, &pfd, &op_errno);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, op_errno, P_MSG_PFD_NULL,
                        "pfd not found in fd's ctx");
                goto out;
        }

        _fd = pfd->fd;

        op_ret = posix_fdstat (this, _fd, &preop);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_WARNING, errno, P_MSG_FSTAT_FAILED,
                        "pre-operation fstat failed on fd=%p", fd);
                goto out;
        }

        if (datasync) {
                op_ret = sys_fdatasync (_fd);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_FSYNC_FAILED, "fdatasync on fd=%p"
                                "failed:", fd);
                        goto out;
                }
        } else {
                op_ret = sys_fsync (_fd);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_FSYNC_FAILED, "fsync on fd=%p "
                                "failed", fd);
                        goto out;
                }
        }

        op_ret = posix_fdstat (this, _fd, &postop);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_WARNING, errno, P_MSG_FSTAT_FAILED,
                        "post-operation fstat failed on fd=%p", fd);
                goto out;
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, &preop, &postop,
                             NULL);

        return 0;
}

static int gf_posix_xattr_enotsup_log;
static int
_handle_setxattr_keyvalue_pair (dict_t *d, char *k, data_t *v,
                                void *tmp)
{
        posix_xattr_filler_t *filler = NULL;

        filler = tmp;

        return posix_handle_pair (filler->this, filler->real_path, k, v,
                                  filler->flags, filler->stbuf);
}

#ifdef GF_DARWIN_HOST_OS
static int
map_xattr_flags(int flags)
{
        /* DARWIN has different defines on XATTR_ flags.
           There do not seem to be a POSIX standard
           Parse any other flags over.
        */
        int darwinflags = flags & ~(GF_XATTR_CREATE | GF_XATTR_REPLACE | XATTR_REPLACE);
        if (GF_XATTR_CREATE & flags)
                darwinflags |= XATTR_CREATE;
        if (GF_XATTR_REPLACE & flags)
                darwinflags |= XATTR_REPLACE;
        return darwinflags;
}
#endif

int32_t
posix_setxattr (call_frame_t *frame, xlator_t *this,
                loc_t *loc, dict_t *dict, int flags, dict_t *xdata)
{
        int32_t       op_ret                  = -1;
        int32_t       op_errno                = 0;
        char *        real_path               = NULL;
        char         *acl_xattr               = NULL;
        struct iatt   stbuf                   = {0};
        int32_t       ret                     = 0;
        ssize_t       acl_size                = 0;
        dict_t       *xattr                   = NULL;
        posix_xattr_filler_t filler = {0,};

        DECLARE_OLD_FS_ID_VAR;
        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);
        VALIDATE_OR_GOTO (dict, out);

        MAKE_INODE_HANDLE (real_path, this, loc, NULL);
        if (!real_path) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        posix_pstat(this, loc->gfid, real_path, &stbuf);

        op_ret = -1;

        dict_del (dict, GFID_XATTR_KEY);
        dict_del (dict, GF_XATTR_VOL_ID_KEY);
        /* the io-stats-dump key should not reach disk */
        dict_del (dict, GF_XATTR_IOSTATS_DUMP_KEY);

        filler.real_path = real_path;
        filler.this = this;
        filler.stbuf = &stbuf;

#ifdef GF_DARWIN_HOST_OS
        filler.flags = map_xattr_flags(flags);
#else
        filler.flags = flags;
#endif
        op_ret = dict_foreach (dict, _handle_setxattr_keyvalue_pair,
                               &filler);
        if (op_ret < 0) {
                op_errno = -op_ret;
                op_ret = -1;
                goto out;
        }

        xattr = dict_new();
        if (!xattr)
                goto out;

/*
 * FIXFIX: Send the stbuf info in the xdata for now
 * This is used by DHT to redirect FOPs if the file is being migrated
 * Ignore errors for now
 */
        if (xdata && dict_get (xdata, DHT_IATT_IN_XDATA_KEY)) {
                ret = posix_pstat(this, loc->gfid, real_path, &stbuf);
                if (ret)
                        goto out;

               ret = posix_set_iatt_in_dict (xattr, &stbuf);
        }

/*
 * ACL can be set on a file/folder using GF_POSIX_ACL_*_KEY xattrs which
 * won't aware of access-control xlator. To update its context correctly,
 * POSIX_ACL_*_XATTR stored in xdata which is send in the call_back path.
 */
        if (dict_get (dict, GF_POSIX_ACL_ACCESS)) {

                /*
                 * The size of buffer will be know after calling sys_lgetxattr,
                 * so first we allocate buffer with large size(~4k), then we
                 * reduced into required size using GF_REALLO().
                 */
                acl_xattr = GF_CALLOC (1, ACL_BUFFER_MAX, gf_posix_mt_char);
                if (!acl_xattr)
                        goto out;

                acl_size = sys_lgetxattr (real_path, POSIX_ACL_ACCESS_XATTR,
                                          acl_xattr, ACL_BUFFER_MAX);

                if (acl_size < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_XATTR_FAILED, "Posix acl is not set "
                                "properly at the backend");
                        goto out;
                }

                /* If acl_size is more than max buffer size, just ignore it */
                if (acl_size >= ACL_BUFFER_MAX) {
                        gf_msg (this->name, GF_LOG_WARNING, ENOMEM,
                                P_MSG_BUFFER_OVERFLOW, "size of acl is more"
                                "than the buffer");
                        goto out;
                }

                acl_xattr = GF_REALLOC (acl_xattr, acl_size);
                if (!acl_xattr)
                        goto out;

                ret = dict_set_bin (xattr, POSIX_ACL_ACCESS_XATTR,
                                    acl_xattr, acl_size);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                P_MSG_SET_XDATA_FAIL, "failed to set"
                                "xdata for acl");
                        GF_FREE (acl_xattr);
                        goto out;
                }
        }

        if (dict_get (dict, GF_POSIX_ACL_DEFAULT)) {

                acl_xattr = GF_CALLOC (1, ACL_BUFFER_MAX, gf_posix_mt_char);
                if (!acl_xattr)
                        goto out;

                acl_size = sys_lgetxattr (real_path, POSIX_ACL_DEFAULT_XATTR,
                                          acl_xattr, ACL_BUFFER_MAX);

                if (acl_size < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_XATTR_FAILED, "Posix acl is not set "
                                "properly at the backend");
                        goto out;
                }

                if (acl_size >= ACL_BUFFER_MAX) {
                        gf_msg (this->name, GF_LOG_WARNING, ENOMEM,
                                P_MSG_BUFFER_OVERFLOW, "size of acl is more"
                                "than the buffer");
                        goto out;
                }

                acl_xattr = GF_REALLOC (acl_xattr, acl_size);
                if (!acl_xattr)
                        goto out;

                ret = dict_set_bin (xattr, POSIX_ACL_DEFAULT_XATTR,
                                    acl_xattr, acl_size);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                P_MSG_SET_XDATA_FAIL, "failed to set"
                                "xdata for acl");
                        GF_FREE (acl_xattr);
                        goto out;
                }
        }
out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno, xattr);

        if (xattr)
                dict_unref (xattr);

        return 0;
}


int
posix_xattr_get_real_filename (call_frame_t *frame, xlator_t *this, loc_t *loc,
			       const char *key, dict_t *dict, dict_t *xdata)
{
        int            ret        = -1;
        int            op_ret     = -1;
        const char    *fname      = NULL;
        char          *real_path  = NULL;
        char          *found      = NULL;
        DIR           *fd         = NULL;
        struct dirent *entry      = NULL;
        struct dirent  scratch[2] = {{0,},};

        MAKE_INODE_HANDLE (real_path, this, loc, NULL);
        if (!real_path) {
                return -ESTALE;
        }
        if (op_ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "posix_xattr_get_real_filename (lstat) on %s failed",
                        real_path);
                return -errno;
        }

        fd = sys_opendir (real_path);
        if (!fd)
                return -errno;

	fname = key + strlen (GF_XATTR_GET_REAL_FILENAME_KEY);

        for (;;) {
                errno = 0;
                entry = sys_readdir (fd, scratch);
                if (!entry || errno != 0)
                        break;

		if (strcasecmp (entry->d_name, fname) == 0) {
			found = gf_strdup (entry->d_name);
			if (!found) {
				(void) sys_closedir (fd);
				return -ENOMEM;
			}
			break;
		}
	}

	(void) sys_closedir (fd);

	if (!found)
		return -ENOENT;

	ret = dict_set_dynstr (dict, (char *)key, found);
	if (ret) {
		GF_FREE (found);
		return -ENOMEM;
	}
	ret = strlen (found) + 1;

	return ret;
}

int
posix_get_ancestry_directory (xlator_t *this, inode_t *leaf_inode,
                              gf_dirent_t *head, char **path, int type,
                              int32_t *op_errno, dict_t *xdata)
{
        ssize_t               handle_size = 0;
        struct posix_private *priv        = NULL;
        inode_t              *inode       = NULL;
        int                   ret         = -1;
        char                  dirpath[PATH_MAX] = {0,};

        priv = this->private;

        handle_size = POSIX_GFID_HANDLE_SIZE(priv->base_path_length);

        ret = posix_make_ancestryfromgfid (this, dirpath, PATH_MAX + 1, head,
                                           type | POSIX_ANCESTRY_PATH,
                                           leaf_inode->gfid,
                                           handle_size, priv->base_path,
                                           leaf_inode->table, &inode, xdata,
                                           op_errno);
        if (ret < 0)
                goto out;


        /* there is already a reference in loc->inode */
        inode_unref (inode);

        if ((type & POSIX_ANCESTRY_PATH) && (path != NULL)) {
                if (strcmp (dirpath, "/"))
                        dirpath[strlen (dirpath) - 1] = '\0';

                *path = gf_strdup (dirpath);
        }

out:
        return ret;
}

int32_t
posix_links_in_same_directory (char *dirpath, int count, inode_t *leaf_inode,
                               inode_t *parent, struct stat *stbuf,
                               gf_dirent_t *head, char **path,
                               int type, dict_t *xdata, int32_t *op_errno)
{
        int                   op_ret       = -1;
        gf_dirent_t          *gf_entry     = NULL;
        xlator_t             *this         = NULL;
        struct posix_private *priv         = NULL;
        DIR                  *dirp         = NULL;
        struct dirent        *entry        = NULL;
        struct dirent         scratch[2]   = {{0,},};
        char                  temppath[PATH_MAX] = {0,};
        char                  scr[PATH_MAX * 4] = {0,};

        this = THIS;

        priv = this->private;

        dirp = sys_opendir (dirpath);
        if (!dirp) {
                *op_errno = errno;
                gf_msg (this->name, GF_LOG_WARNING, errno, P_MSG_OPEN_FAILED,
                        "could not opendir %s", dirpath);
                goto out;
        }

        while (count > 0) {
                errno = 0;
                entry = sys_readdir (dirp, scratch);
                if (!entry || errno != 0)
                        break;

                if (entry->d_ino != stbuf->st_ino)
                        continue;

                /* Linking an inode here, can cause a race in posix_acl.
                   Parent inode gets linked here, but before
                   it reaches posix_acl_readdirp_cbk, create/lookup can
                   come on a leaf-inode, as parent-inode-ctx not yet updated
                   in posix_acl_readdirp_cbk, create and lookup can fail
                   with EACCESS. So do the inode linking in the quota xlator

                linked_inode = inode_link (leaf_inode, parent,
                                           entry->d_name, NULL);

                GF_ASSERT (linked_inode == leaf_inode);
                inode_unref (linked_inode);*/

                if (type & POSIX_ANCESTRY_DENTRY) {
                        loc_t loc = {0, };

                        loc.inode = inode_ref (leaf_inode);
                        gf_uuid_copy (loc.gfid, leaf_inode->gfid);

                        (void) snprintf (temppath, sizeof(temppath), "%s/%s",
                                         dirpath, entry->d_name);

                        gf_entry = gf_dirent_for_name (entry->d_name);
                        gf_entry->inode = inode_ref (leaf_inode);
                        gf_entry->dict
                                = posix_xattr_fill (this, temppath, &loc, NULL,
                                                    -1, xdata, NULL);
                        iatt_from_stat (&(gf_entry->d_stat), stbuf);

                        list_add_tail (&gf_entry->list, &head->list);
                        loc_wipe (&loc);
                }

                if (type & POSIX_ANCESTRY_PATH) {
                        (void) snprintf (temppath, sizeof(temppath), "%s/%s",
                                         &dirpath[priv->base_path_length],
                                         entry->d_name);
                        if (!*path) {
                                *path = gf_strdup (temppath);
                        } else {
                                /* creating a colon separated */
                                /* list of hard links */
                                (void) snprintf (scr, sizeof(scr), "%s:%s",
                                                 *path, temppath);

                                GF_FREE (*path);
                                *path = gf_strdup (scr);
                        }
                        if (!*path) {
                                op_ret = -1;
                                *op_errno = ENOMEM;
                                goto out;
                        }
                }

                count--;
        }

        op_ret = 0;
out:
        if (dirp) {
                op_ret = sys_closedir (dirp);
                if (op_ret == -1) {
                        *op_errno = errno;
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_CLOSE_FAILED, "closedir failed");
                }
        }

        return op_ret;
}

int
posix_get_ancestry_non_directory (xlator_t *this, inode_t *leaf_inode,
                                  gf_dirent_t *head, char **path, int type,
                                  int32_t *op_errno, dict_t *xdata)
{
        size_t                remaining_size    = 0;
        int                   op_ret            = -1, pathlen = -1;
        ssize_t               handle_size       = 0;
        uuid_t                pgfid             = {0,};
        int                   nlink_samepgfid   = 0;
        struct stat           stbuf             = {0,};
        char                 *list              = NULL;
        int32_t               list_offset       = 0;
        struct posix_private *priv              = NULL;
        ssize_t               size              = 0;
        inode_t              *parent            = NULL;
        loc_t                *loc               = NULL;
        char                 *leaf_path         = NULL;
        char                  key[4096]         = {0,};
        char                  dirpath[PATH_MAX] = {0,};
        char                  pgfidstr[UUID_CANONICAL_FORM_LEN+1] = {0,};

        priv = this->private;

        loc = GF_CALLOC (1, sizeof (*loc), gf_posix_mt_char);
        if (loc == NULL) {
                op_ret = -1;
                *op_errno = ENOMEM;
                goto out;
        }

        gf_uuid_copy (loc->gfid, leaf_inode->gfid);

        MAKE_INODE_HANDLE (leaf_path, this, loc, NULL);
        if (!leaf_path) {
                GF_FREE (loc);
                *op_errno = ESTALE;
                goto out;
        }
        GF_FREE (loc);

        size = sys_llistxattr (leaf_path, NULL, 0);
        if (size == -1) {
                *op_errno = errno;
                if ((errno == ENOTSUP) || (errno == ENOSYS)) {
                        GF_LOG_OCCASIONALLY (gf_posix_xattr_enotsup_log,
                                             this->name, GF_LOG_WARNING,
                                             "Extended attributes not "
                                             "supported (try remounting brick"
                                             " with 'user_xattr' flag)");

                } else {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_XATTR_FAILED, "listxattr failed on"
                                "%s", leaf_path);

                }

                goto out;
        }

        if (size == 0) {
                op_ret = 0;
                goto out;
        }

        list = alloca (size);
        if (!list) {
                *op_errno = errno;
                goto out;
        }

        size = sys_llistxattr (leaf_path, list, size);
        if (size < 0) {
                op_ret = -1;
                *op_errno = errno;
                goto out;
        }
        remaining_size = size;
        list_offset = 0;

        op_ret = sys_lstat (leaf_path, &stbuf);
        if (op_ret == -1) {
                *op_errno = errno;
                gf_msg (this->name, GF_LOG_WARNING, errno, P_MSG_LSTAT_FAILED,
                        "lstat failed on %s", leaf_path);
                goto out;
        }

        while (remaining_size > 0) {
                strncpy (key, list + list_offset, sizeof(key));
                if (strncmp (key, PGFID_XATTR_KEY_PREFIX,
                             strlen (PGFID_XATTR_KEY_PREFIX)) != 0)
                        goto next;

                op_ret = sys_lgetxattr (leaf_path, key,
                                        &nlink_samepgfid,
                                        sizeof(nlink_samepgfid));
                if (op_ret == -1) {
                        *op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_XATTR_FAILED, "getxattr failed on "
                                "%s: key = %s ", leaf_path, key);
                        goto out;
                }

                nlink_samepgfid = ntoh32 (nlink_samepgfid);

                strncpy (pgfidstr, key + strlen(PGFID_XATTR_KEY_PREFIX),
                         sizeof(pgfidstr));
                gf_uuid_parse (pgfidstr, pgfid);

                handle_size = POSIX_GFID_HANDLE_SIZE(priv->base_path_length);

                /* constructing the absolute real path of parent dir */
                strncpy (dirpath, priv->base_path, sizeof(dirpath));
                pathlen = PATH_MAX + 1 - priv->base_path_length;

                op_ret = posix_make_ancestryfromgfid (this,
                                                      dirpath + priv->base_path_length,
                                                      pathlen,
                                                      head,
                                                      type | POSIX_ANCESTRY_PATH,
                                                      pgfid,
                                                      handle_size,
                                                      priv->base_path,
                                                      leaf_inode->table,
                                                      &parent, xdata, op_errno);
                if (op_ret < 0) {
                        goto next;
                }

                dirpath[strlen (dirpath) - 1] = '\0';

                posix_links_in_same_directory (dirpath, nlink_samepgfid,
                                               leaf_inode, parent, &stbuf, head,
                                               path, type, xdata, op_errno);

                if (parent != NULL) {
                        inode_unref (parent);
                        parent = NULL;
                }

        next:
                remaining_size -= strlen (key) + 1;
                list_offset += strlen (key) + 1;
        } /* while (remaining_size > 0) */

        op_ret = 0;

out:
        return op_ret;
}

int
posix_get_ancestry (xlator_t *this, inode_t *leaf_inode,
                    gf_dirent_t *head, char **path, int type, int32_t *op_errno,
                    dict_t *xdata)
{
        int                   ret  = -1;
        struct posix_private *priv = NULL;

        priv = this->private;

        if (IA_ISDIR (leaf_inode->ia_type)) {
                ret = posix_get_ancestry_directory (this, leaf_inode,
                                                    head, path, type, op_errno,
                                                    xdata);
        } else  {

                if (!priv->update_pgfid_nlinks)
                        goto out;
                ret = posix_get_ancestry_non_directory (this, leaf_inode,
                                                        head, path, type,
                                                        op_errno, xdata);
        }

out:
        if (ret && path && *path) {
                GF_FREE (*path);
                *path = NULL;
        }

        return ret;
}

/**
 * posix_getxattr - this function returns a dictionary with all the
 *                  key:value pair present as xattr. used for
 *                  both 'listxattr' and 'getxattr'.
 */
int32_t
posix_getxattr (call_frame_t *frame, xlator_t *this,
                loc_t *loc, const char *name, dict_t *xdata)
{
        struct posix_private *priv                  = NULL;
        int32_t               op_ret                = -1;
        int32_t               op_errno              = 0;
        char                 *value                 = NULL;
        char                 *real_path             = NULL;
        dict_t               *dict                  = NULL;
        char                 *file_contents         = NULL;
        int                   ret                   = -1;
        char                 *path                  = NULL;
        char                 *rpath                 = NULL;
        char                 *dyn_rpath             = NULL;
        ssize_t               size                  = 0;
        char                 *list                  = NULL;
        int32_t               list_offset           = 0;
        size_t                remaining_size        = 0;
        char                  host_buf[1024]        = {0,};
        char                  keybuffer[4096]       = {0,};

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_INODE_HANDLE (real_path, this, loc, NULL);

        op_ret = -1;
        priv = this->private;

        /* Allow access to stime xattr only to geo-rep worker */
        if (frame->root->pid != GF_CLIENT_PID_GSYNCD && name &&
            fnmatch ("*.glusterfs.*.stime", name, FNM_PERIOD) == 0) {
                op_ret = -1;
                op_errno = ENOATTR;
                goto out;
        }

        if (loc->inode && IA_ISDIR(loc->inode->ia_type) && name &&
            ZR_FILE_CONTENT_REQUEST(name)) {
                ret = posix_get_file_contents (this, loc->gfid, &name[15],
                                               &file_contents);
                if (ret < 0) {
                        op_errno = -ret;
                        gf_msg (this->name, GF_LOG_ERROR, op_errno,
                                P_MSG_FILE_FAILED, "getting file contents"
                                "failed");
                        goto out;
                }
        }

        dict = dict_new ();
        if (!dict) {
                op_errno = ENOMEM;
                goto out;
        }

        if (loc->inode && name && GF_POSIX_ACL_REQUEST (name)) {
                ret = posix_pacl_get (real_path, name, &value);
                if (ret || !value) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_ACL_FAILED, "could not get acl (%s) for"
                                "%s", name, real_path);
                        op_ret = -1;
                        op_errno = errno;
                        goto out;
                }

                ret = dict_set_dynstr (dict, (char *)name, value);
                if (ret < 0) {
                        GF_FREE (value);
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_ACL_FAILED, "could not set acl (%s) for"
                                "%s in dictionary", name, real_path);
                        op_ret = -1;
                        op_errno = errno;
                        goto out;
                }

                size = ret;
                goto done;
        }

	if (loc->inode && name &&
	    (strncmp (name, GF_XATTR_GET_REAL_FILENAME_KEY,
		      strlen (GF_XATTR_GET_REAL_FILENAME_KEY)) == 0)) {
		ret = posix_xattr_get_real_filename (frame, this, loc,
						     name, dict, xdata);
		if (ret < 0) {
			op_ret = -1;
			op_errno = -ret;
                        if (op_errno == ENOENT) {
                                gf_msg_debug (this->name, 0, "Failed to get "
                                              "real filename (%s, %s)",
                                              loc->path, name);
                        } else {
                                gf_msg (this->name, GF_LOG_WARNING, op_errno,
                                        P_MSG_GETTING_FILENAME_FAILED,
				        "Failed to get real filename (%s, %s):"
                                        , loc->path, name);
                        }
			goto out;
		}

		size = ret;
		goto done;
	}

        if (loc->inode && name && !strcmp (name, GLUSTERFS_OPEN_FD_COUNT)) {
                if (!fd_list_empty (loc->inode)) {
                        ret = dict_set_uint32 (dict, (char *)name, 1);
                        if (ret < 0)
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        P_MSG_DICT_SET_FAILED, "Failed to set "
                                        "dictionary value for %s", name);
                } else {
                        ret = dict_set_uint32 (dict, (char *)name, 0);
                        if (ret < 0)
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        P_MSG_DICT_SET_FAILED,  "Failed to set "
                                        "dictionary value for %s", name);
                }
                goto done;
        }
        if (loc->inode && name && (XATTR_IS_PATHINFO (name))) {
                if (LOC_HAS_ABSPATH (loc))
                        MAKE_REAL_PATH (rpath, this, loc->path);
                else
                        rpath = real_path;

                (void) snprintf (host_buf, sizeof(host_buf),
                                 "<POSIX(%s):%s:%s>", priv->base_path,
                                 ((priv->node_uuid_pathinfo
                                   && !gf_uuid_is_null(priv->glusterd_uuid))
                                      ? uuid_utoa (priv->glusterd_uuid)
                                      : priv->hostname),
                                 rpath);

                dyn_rpath = gf_strdup (host_buf);
                if (!dyn_rpath) {
                        ret = -1;
                        goto done;
                }
                size = strlen (dyn_rpath) + 1;
                ret = dict_set_dynstr (dict, (char *)name, dyn_rpath);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                P_MSG_DICT_SET_FAILED, "could not set value"
                                " (%s) in dictionary", dyn_rpath);
                        GF_FREE (dyn_rpath);
                }

                goto done;
        }

        if (loc->inode && name &&
            (strcmp (name, GF_XATTR_NODE_UUID_KEY) == 0)
            && !gf_uuid_is_null (priv->glusterd_uuid)) {
                (void) snprintf (host_buf, sizeof(host_buf), "%s",
                                 uuid_utoa (priv->glusterd_uuid));

                dyn_rpath = gf_strdup (host_buf);
                if (!dyn_rpath) {
                        op_errno = ENOMEM;
                        goto out;
                }

                size = strlen (dyn_rpath) + 1;
                ret = dict_set_dynstr (dict, GF_XATTR_NODE_UUID_KEY,
                                       dyn_rpath);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, -ret,
                                P_MSG_DICT_SET_FAILED, "could not set value"
                                "(%s) in dictionary", dyn_rpath);
                        GF_FREE (dyn_rpath);
                        op_errno = -ret;
                        goto out;
                }
                goto done;
        }

        if (loc->inode && name &&
            (strcmp (name, GFID_TO_PATH_KEY) == 0)) {
                ret = inode_path (loc->inode, NULL, &path);
                if (ret < 0) {
                        op_errno = -ret;
                        gf_msg (this->name, GF_LOG_WARNING, op_errno,
                                P_MSG_INODE_PATH_GET_FAILED,
                                "%s: could not get "
                                "inode path", uuid_utoa (loc->inode->gfid));
                        goto out;
                }

                size = ret;
                ret = dict_set_dynstr (dict, GFID_TO_PATH_KEY, path);
                if (ret < 0) {
                        op_errno = ENOMEM;
                        GF_FREE (path);
                        goto out;
                }
                goto done;
        }

        if (loc->inode && name
            && (strcmp (name, GET_ANCESTRY_PATH_KEY) == 0)) {
                int type = POSIX_ANCESTRY_PATH;

                op_ret = posix_get_ancestry (this, loc->inode, NULL,
                                             &path, type, &op_errno,
                                             xdata);
                if (op_ret < 0) {
                        op_ret = -1;
                        op_errno = ENODATA;
                        goto out;
                }

                op_ret = dict_set_dynstr (dict, GET_ANCESTRY_PATH_KEY, path);
                if (op_ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, -op_ret,
                                P_MSG_GET_KEY_VALUE_FAILED, "could not get "
                                "value for key (%s)", GET_ANCESTRY_PATH_KEY);
                        GF_FREE (path);
                        op_errno = -op_ret;
                        op_ret = -1;
                }

                goto done;
        }

        if (loc->inode && name
             && (strncmp (name, GLUSTERFS_GET_OBJECT_SIGNATURE,
                          strlen (GLUSTERFS_GET_OBJECT_SIGNATURE)) == 0)) {
                op_ret = posix_get_objectsignature (real_path, dict);
                if (op_ret < 0) {
                        op_errno = -op_ret;
                        op_ret = -1;
                }

                goto done;
        }

        if (name) {
                strncpy (keybuffer, name, sizeof(keybuffer));
                char *key = keybuffer;
#if defined(GF_DARWIN_HOST_OS_DISABLED)
                if (priv->xattr_user_namespace == XATTR_STRIP) {
                        if (strncmp(key, "user.",5) == 0) {
                                key += 5;
                                gf_msg_debug (this->name, 0, "getxattr for file %s"
                                        " stripping user key: %s -> %s",
                                        real_path, keybuffer, key);
                        }
                }
#endif
                size = sys_lgetxattr (real_path, key, NULL, 0);
                if (size == -1) {
                        op_errno = errno;
                        if ((op_errno == ENOTSUP) || (op_errno == ENOSYS)) {
                                GF_LOG_OCCASIONALLY (gf_posix_xattr_enotsup_log,
                                                     this->name, GF_LOG_WARNING,
                                                     "Extended attributes not "
                                                     "supported (try remounting"
                                                     " brick with 'user_xattr' "
                                                     "flag)");
                        } else if (op_errno == ENOATTR ||
                                        op_errno == ENODATA) {
                                gf_msg_debug (this->name, 0,
                                        "No such attribute:%s for file %s",
                                        key, real_path);
                        } else {
                                gf_msg (this->name, GF_LOG_ERROR, op_errno,
                                        P_MSG_XATTR_FAILED, "getxattr failed"
                                        " on %s: %s ", real_path, key);
                        }

                        goto done;
                }
                value = GF_CALLOC (size + 1, sizeof(char), gf_posix_mt_char);
                if (!value) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto out;
                }
                size = sys_lgetxattr (real_path, key, value, size);
                if (size == -1) {
                        op_ret = -1;
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_XATTR_FAILED, "getxattr failed on "
                                "%s: key = %s", real_path, key);
                        GF_FREE (value);
                        goto out;
                }
                value [size] = '\0';
                op_ret = dict_set_dynptr (dict, key, value, size);
                if (op_ret < 0) {
                        op_errno = -op_ret;
                        gf_msg (this->name, GF_LOG_ERROR, op_errno,
                                P_MSG_DICT_SET_FAILED, "dict set operation "
                                "on %s for the key %s failed.", real_path, key);
                        GF_FREE (value);
                        goto out;
                }

                goto done;
        }

        size = sys_llistxattr (real_path, NULL, 0);
        if (size == -1) {
                op_errno = errno;
                if ((errno == ENOTSUP) || (errno == ENOSYS)) {
                        GF_LOG_OCCASIONALLY (gf_posix_xattr_enotsup_log,
                                             this->name, GF_LOG_WARNING,
                                             "Extended attributes not "
                                             "supported (try remounting"
                                             " brick with 'user_xattr' "
                                             "flag)");
                }
                else {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_XATTR_FAILED,
                                "listxattr failed on %s",
                                real_path);
                }
                goto out;
        }

        if (size == 0)
                goto done;

        list = alloca (size);
        if (!list) {
                op_errno = errno;
                goto out;
        }

        size = sys_llistxattr (real_path, list, size);
        if (size < 0) {
                op_ret = -1;
                op_errno = errno;
                goto out;
        }

        remaining_size = size;
        list_offset = 0;
        while (remaining_size > 0) {
                strncpy (keybuffer, list + list_offset, sizeof(keybuffer));
                if (frame->root->pid != GF_CLIENT_PID_GSYNCD &&
                    fnmatch ("*.glusterfs.*.stime", keybuffer, FNM_PERIOD) == 0)
                        goto ignore;

                size = sys_lgetxattr (real_path, keybuffer, NULL, 0);
                if (size == -1) {
                        op_ret = -1;
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_XATTR_FAILED, "getxattr failed on "
                                "%s: key = %s ", real_path, keybuffer);
                        break;
                }

                value = GF_CALLOC (size + 1, sizeof(char),
                                   gf_posix_mt_char);
                if (!value) {
                        op_errno = errno;
                        goto out;
                }

                size = sys_lgetxattr (real_path, keybuffer, value, size);
                if (size == -1) {
                        op_ret = -1;
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_XATTR_FAILED, "getxattr failed on "
                                "%s: key = %s ", real_path, keybuffer);
                        GF_FREE (value);
                        break;
                }

                value [size] = '\0';
#ifdef GF_DARWIN_HOST_OS
                /* The protocol expect namespace for now */
                char *newkey = NULL;
                gf_add_prefix (XATTR_USER_PREFIX, keybuffer, &newkey);
                strncpy (keybuffer, newkey, sizeof(keybuffer));
                GF_FREE (newkey);
#endif
                op_ret = dict_set_dynptr (dict, keybuffer, value, size);
                if (op_ret < 0) {
                        op_errno = -op_ret;
                        gf_msg (this->name, GF_LOG_ERROR, op_errno,
                                P_MSG_DICT_SET_FAILED, "dict set operation "
                                "on %s for the key %s failed.", real_path,
                                keybuffer);
                        GF_FREE (value);
                        goto out;
                }

ignore:
                remaining_size -= strlen (keybuffer) + 1;
                list_offset += strlen (keybuffer) + 1;

        } /* while (remaining_size > 0) */

done:
        op_ret = size;

        if (dict) {
                dict_del (dict, GFID_XATTR_KEY);
                dict_del (dict, GF_XATTR_VOL_ID_KEY);
        }

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict, NULL);

        if (dict) {
                dict_unref (dict);
        }

        return 0;
}


int32_t
posix_fgetxattr (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, const char *name, dict_t *xdata)
{
        int32_t           op_ret         = -1;
        int32_t           op_errno       = EINVAL;
        struct posix_fd * pfd            = NULL;
        int               _fd            = -1;
        int32_t           list_offset    = 0;
        ssize_t           size           = 0;
        size_t            remaining_size = 0;
        char *            value          = NULL;
        char *            list           = NULL;
        dict_t *          dict           = NULL;
        int               ret            = -1;
        char              key[4096]      = {0,};

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);

        ret = posix_fd_ctx_get (fd, this, &pfd, &op_errno);
        if (ret < 0) {
                op_ret = -1;
                gf_msg (this->name, GF_LOG_WARNING, op_errno, P_MSG_PFD_NULL,
                        "pfd is NULL from fd=%p", fd);
                goto out;
        }

        _fd = pfd->fd;

        /* Get the total size */
        dict = dict_new ();
        if (!dict) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        if (name && !strcmp (name, GLUSTERFS_OPEN_FD_COUNT)) {
                ret = dict_set_uint32 (dict, (char *)name, 1);
                if (ret < 0) {
                        op_ret = -1;
                        size = -1;
                        op_errno = ENOMEM;
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                P_MSG_DICT_SET_FAILED, "Failed to set "
                                "dictionary value for %s", name);
                }
                goto done;
        }

        if (name && strncmp (name, GLUSTERFS_GET_OBJECT_SIGNATURE,
                      strlen (GLUSTERFS_GET_OBJECT_SIGNATURE)) == 0) {
                op_ret = posix_fdget_objectsignature (_fd, dict);
                if (op_ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, 0,
                                "posix_fdget_objectsignature failed");
                        op_errno = -op_ret;
                        op_ret = -1;
                        size = -1;
                }

                goto done;
        }

        if (name) {
                strncpy (key, name, sizeof(key));
#ifdef GF_DARWIN_HOST_OS
                struct posix_private *priv       = NULL;
                priv = this->private;
                if (priv->xattr_user_namespace == XATTR_STRIP) {
                        char *newkey = NULL;
                        gf_add_prefix (XATTR_USER_PREFIX, key, &newkey);
                        strncpy (key, newkey, sizeof(key));
                        GF_FREE (newkey);
                }
#endif
                size = sys_fgetxattr (_fd, key, NULL, 0);
                if (size == -1) {
                        op_ret = -1;
                        op_errno = errno;
                        if (errno == ENODATA || errno == ENOATTR) {
                                gf_msg_debug (this->name, 0, "fgetxattr failed"
                                              " on key %s (%s)", key,
                                              strerror (op_errno));
                        } else {
                               gf_msg (this->name, GF_LOG_ERROR, errno,
                                       P_MSG_XATTR_FAILED, "fgetxattr failed "
                                       "on key %s", key);
                        }
                        goto done;
                }

                value = GF_CALLOC (size + 1, sizeof(char), gf_posix_mt_char);
                if (!value) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto out;
                }
                size = sys_fgetxattr (_fd, key, value, size);
                if (size == -1) {
                        op_ret = -1;
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_XATTR_FAILED, "fgetxattr failed on "
                                "fd %p for the key %s ", fd, key);
                        GF_FREE (value);
                        goto out;
                }

                value [size] = '\0';
                op_ret = dict_set_dynptr (dict, key, value, size);
                if (op_ret < 0) {
                        op_errno = -op_ret;
                        op_ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_DICT_SET_FAILED, "dict set operation "
                                "on key %s failed", key);
                        GF_FREE (value);
                        goto out;
                }

                goto done;
        }

        size = sys_flistxattr (_fd, NULL, 0);
        if (size == -1) {
                op_ret = -1;
                op_errno = errno;
                if ((errno == ENOTSUP) || (errno == ENOSYS)) {
                        GF_LOG_OCCASIONALLY (gf_posix_xattr_enotsup_log,
                                             this->name, GF_LOG_WARNING,
                                             "Extended attributes not "
                                             "supported (try remounting "
                                             "brick with 'user_xattr' flag)");
                }
                else {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_XATTR_FAILED, "listxattr failed on %p:",
                                fd);
                }
                goto out;
        }

        if (size == 0)
                goto done;

        list = alloca (size + 1);
        if (!list) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        size = sys_flistxattr (_fd, list, size);

        remaining_size = size;
        list_offset = 0;
        while (remaining_size > 0) {
                if(*(list + list_offset) == '\0')
                        break;

                strncpy (key, list + list_offset, sizeof(key));
                size = sys_fgetxattr (_fd, key, NULL, 0);
                if (size == -1) {
                        op_ret = -1;
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_XATTR_FAILED, "fgetxattr failed on "
                                "fd %p for the key %s ", fd, key);
                        break;
                }

                value = GF_CALLOC (size + 1, sizeof(char),
                                   gf_posix_mt_char);
                if (!value) {
                        op_ret = -1;
                        op_errno = errno;
                        goto out;
                }

                size = sys_fgetxattr (_fd, key, value, size);
                if (size == -1) {
                        op_ret = -1;
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_XATTR_FAILED, "fgetxattr failed on "
                                "the fd %p for the key %s ", fd, key);
                        GF_FREE (value);
                        break;
                }

                value [size] = '\0';

                op_ret = dict_set_dynptr (dict, key, value, size);
                if (op_ret) {
                        op_errno = -op_ret;
                        op_ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_DICT_SET_FAILED, "dict set operation "
                                "failed on key %s", key);
                        GF_FREE (value);
                        goto out;
                }
                remaining_size -= strlen (key) + 1;
                list_offset += strlen (key) + 1;

        } /* while (remaining_size > 0) */

done:
        op_ret = size;

        if (dict) {
                dict_del (dict, GFID_XATTR_KEY);
                dict_del (dict, GF_XATTR_VOL_ID_KEY);
        }

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (fgetxattr, frame, op_ret, op_errno, dict, NULL);

        if (dict)
                dict_unref (dict);

        return 0;
}

static int
_handle_fsetxattr_keyvalue_pair (dict_t *d, char *k, data_t *v,
                                void *tmp)
{
        posix_xattr_filler_t *filler = NULL;

        filler = tmp;

        return posix_fhandle_pair (filler->this, filler->fdnum, k, v,
                                   filler->flags, filler->stbuf);
}

int32_t
posix_fsetxattr (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, dict_t *dict, int flags, dict_t *xdata)
{
        int32_t            op_ret         = -1;
        int32_t            op_errno       = 0;
        struct posix_fd   *pfd            = NULL;
        int                _fd            = -1;
        int                ret            = -1;
        struct  iatt       stbuf          = {0,};
        dict_t            *xattr          = NULL;
        posix_xattr_filler_t filler       = {0,};

        DECLARE_OLD_FS_ID_VAR;
        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (dict, out);

        ret = posix_fd_ctx_get (fd, this, &pfd, &op_errno);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, op_errno, P_MSG_PFD_NULL,
                        "pfd is NULL from fd=%p", fd);
                goto out;
        }
        _fd = pfd->fd;

        posix_fdstat (this, pfd->fd, &stbuf);

        dict_del (dict, GFID_XATTR_KEY);
        dict_del (dict, GF_XATTR_VOL_ID_KEY);

        filler.fdnum = _fd;
        filler.this = this;
        filler.stbuf = &stbuf;
#ifdef GF_DARWIN_HOST_OS
        filler.flags = map_xattr_flags(flags);
#else
        filler.flags = flags;
#endif
        op_ret = dict_foreach (dict, _handle_fsetxattr_keyvalue_pair,
                               &filler);
        if (op_ret < 0) {
                op_errno = -op_ret;
                op_ret = -1;
        }

        if (!ret && xdata && dict_get (xdata, GLUSTERFS_DURABLE_OP)) {
                op_ret = sys_fsync (_fd);
                if (op_ret < 0) {
                        op_ret = -1;
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_DURABILITY_REQ_NOT_SATISFIED,
                                "could not satisfy durability request: "
                                "reason ");
                }
        }

        if (xdata && dict_get (xdata, DHT_IATT_IN_XDATA_KEY)) {
                ret = posix_fdstat (this, pfd->fd, &stbuf);
                if (ret == -1) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, op_errno,
                                P_MSG_XATTR_FAILED, "fsetxattr (fstat)"
                                "failed on fd=%p", fd);
                        goto out;
                }

                xattr = dict_new ();
                if (!xattr)
                        goto out;
                ret = posix_set_iatt_in_dict (xattr, &stbuf);
        }

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno, xattr);

        if (xattr)
                dict_unref (xattr);

        return 0;
}

int
_posix_remove_xattr (dict_t *dict, char *key, data_t *value, void *data)
{
        int32_t               op_ret   = 0;
        xlator_t             *this     = NULL;
        posix_xattr_filler_t *filler   = NULL;

        filler = (posix_xattr_filler_t *) data;
        this = filler->this;
#ifdef GF_DARWIN_HOST_OS
        struct posix_private  *priv = NULL;
        priv = (struct posix_private *) this->private;
        char *newkey = NULL;
        if (priv->xattr_user_namespace == XATTR_STRIP) {
                gf_remove_prefix (XATTR_USER_PREFIX, key, &newkey);
                gf_msg_debug ("remove_xattr", 0, "key %s => %s" , key,
                       newkey);
                key = newkey;
        }
#endif
    /* Bulk remove xattr is internal fop in gluster. Some of the xattrs may
     * have special behavior. Ex: removexattr("posix.system_acl_access"),
     * removes more than one xattr on the file that could be present in the
     * bulk-removal request.  Removexattr of these deleted xattrs will fail
     * with either ENODATA/ENOATTR.  Since all this fop cares is removal of the
     * xattrs in bulk-remove request and if they are already deleted, it can be
     * treated as success.
     */

        op_ret = sys_lremovexattr (filler->real_path, key);
        if (op_ret == -1) {
                if (errno == ENODATA || errno == ENOATTR)
                        op_ret = 0;
        }

        if (op_ret == -1) {
                filler->op_errno = errno;
                if (errno != ENOATTR && errno != ENODATA && errno != EPERM)
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_XATTR_FAILED, "removexattr failed on %s"
                                " (for %s)", filler->real_path, key);
        }
#ifdef GF_DARWIN_HOST_OS
        GF_FREE(newkey);
#endif
        return op_ret;
}


int32_t
posix_removexattr (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, const char *name, dict_t *xdata)
{
        int32_t op_ret    = -1;
        int32_t op_errno  = 0;
        int32_t ret    = -1;
        char *  real_path = NULL;
        struct iatt   stbuf         = {0};
        dict_t  *xattr    = NULL;
        posix_xattr_filler_t filler = {0,};

        DECLARE_OLD_FS_ID_VAR;

        MAKE_INODE_HANDLE (real_path, this, loc, NULL);
        if (!real_path) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }


        if (!strcmp (GFID_XATTR_KEY, name)) {
                gf_msg (this->name, GF_LOG_WARNING, 0, P_MSG_XATTR_NOT_REMOVED,
                        "Remove xattr called on gfid for file %s", real_path);
                op_ret = -1;
                goto out;
        }
        if (!strcmp (GF_XATTR_VOL_ID_KEY, name)) {
                gf_msg (this->name, GF_LOG_WARNING, 0, P_MSG_XATTR_NOT_REMOVED,
                        "Remove xattr called on volume-id for file %s",
                        real_path);
                op_ret = -1;
                goto out;
        }


        SET_FS_ID (frame->root->uid, frame->root->gid);

        /**
         * sending an empty key name with xdata containing the
         * list of key(s) to be removed implies "bulk remove request"
         * for removexattr.
         */
        if (name && (strcmp (name, "") == 0) && xdata) {
                filler.real_path = real_path;
                filler.this = this;
                op_ret = dict_foreach (xdata, _posix_remove_xattr, &filler);
                if (op_ret) {
                        op_errno = filler.op_errno;
                }

                goto out;
        }

        op_ret = sys_lremovexattr (real_path, name);
        if (op_ret == -1) {
                op_errno = errno;
                if (op_errno != ENOATTR && op_errno != ENODATA &&
                    op_errno != EPERM)
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_XATTR_FAILED, "removexattr on %s "
                                "(for %s)", real_path, name);
                goto out;
        }

        if (xdata && dict_get (xdata, DHT_IATT_IN_XDATA_KEY)) {
                ret = posix_pstat(this, loc->gfid, real_path, &stbuf);
                if (ret)
                        goto out;
                xattr = dict_new();
                if (!xattr)
                        goto out;

                ret = posix_set_iatt_in_dict (xattr, &stbuf);
        }
        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno, xattr);

        if (xattr)
                dict_unref (xattr);

        return 0;
}

int32_t
posix_fremovexattr (call_frame_t *frame, xlator_t *this,
                    fd_t *fd, const char *name, dict_t *xdata)
{
        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;
        struct posix_fd * pfd      = NULL;
        struct iatt       stbuf    = {0,};
        dict_t           *xattr    = NULL;
        int               _fd      = -1;
        int               ret      = -1;

        DECLARE_OLD_FS_ID_VAR;

        if (!strcmp (GFID_XATTR_KEY, name)) {
                gf_msg (this->name, GF_LOG_WARNING, 0, P_MSG_XATTR_NOT_REMOVED,
                        "Remove xattr called on gfid for file");
                goto out;
        }
        if (!strcmp (GF_XATTR_VOL_ID_KEY, name)) {
                gf_msg (this->name, GF_LOG_WARNING, 0, P_MSG_XATTR_NOT_REMOVED,
                        "Remove xattr called on volume-id for file");
                goto out;
        }

        ret = posix_fd_ctx_get (fd, this, &pfd, &op_errno);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, op_errno, P_MSG_PFD_NULL,
                        "pfd is NULL from fd=%p", fd);
                goto out;
        }
        _fd = pfd->fd;



        SET_FS_ID (frame->root->uid, frame->root->gid);

        op_ret = sys_fremovexattr (_fd, name);
        if (op_ret == -1) {
                op_errno = errno;
                if (op_errno != ENOATTR && op_errno != ENODATA &&
                    op_errno != EPERM)
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_XATTR_FAILED, "fremovexattr (for %s)",
                                name);
                goto out;
        }

        if (xdata && dict_get (xdata, DHT_IATT_IN_XDATA_KEY)) {
                ret = posix_fdstat (this, pfd->fd, &stbuf);
                if (ret)
                        goto out;
                xattr = dict_new();
                if (!xattr)
                        goto out;

                ret = posix_set_iatt_in_dict (xattr, &stbuf);
        }
        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (fremovexattr, frame, op_ret, op_errno, xattr);

        if (xattr)
                dict_unref (xattr);

        return 0;
}


int32_t
posix_fsyncdir (call_frame_t *frame, xlator_t *this,
                fd_t *fd, int datasync, dict_t *xdata)
{
        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;
        int               ret      = -1;
        struct posix_fd  *pfd      = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = posix_fd_ctx_get (fd, this, &pfd, &op_errno);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, op_errno, P_MSG_PFD_NULL,
                        "pfd is NULL, fd=%p", fd);
                goto out;
        }

        op_ret = 0;

out:
        STACK_UNWIND_STRICT (fsyncdir, frame, op_ret, op_errno, NULL);

        return 0;
}


void
posix_print_xattr (dict_t *this,
                   char *key,
                   data_t *value,
                   void *data)
{
        gf_msg_debug ("posix", 0,
                "(key/val) = (%s/%d)", key, data_to_int32 (value));
}


/**
 * add_array - add two arrays of 32-bit numbers (stored in network byte order)
 * dest = dest + src
 * @count: number of 32-bit numbers
 * FIXME: handle overflow
 */

static void
__add_array (int32_t *dest, int32_t *src, int count)
{
        int     i = 0;
        int32_t destval = 0;
        for (i = 0; i < count; i++) {
                destval = ntoh32 (dest[i]);
                dest[i] = hton32 (destval + ntoh32 (src[i]));
        }
}

static void
__add_long_array (int64_t *dest, int64_t *src, int count)
{
        int i = 0;
        for (i = 0; i < count; i++) {
                dest[i] = hton64 (ntoh64 (dest[i]) + ntoh64 (src[i]));
        }
}


/* functions:
       __add_array_with_default
       __add_long_array_with_default

   xattrop type:
       GF_XATTROP_ADD_ARRAY_WITH_DEFAULT
       GF_XATTROP_ADD_ARRAY64_WITH_DEFAULT

   These operations are similar to 'GF_XATTROP_ADD_ARRAY',
   except that it adds a default value if xattr is missing
   or its value is zero on disk.

   One use-case of this operation is in inode-quota.
   When a new directory is created, its default dir_count
   should be set to 1. So when a xattrop performed setting
   inode-xattrs, it should account initial dir_count
   1 if the xattrs are not present

   Here is the usage of this operation

   value required in xdata for each key
   struct array {
       int32_t   newvalue_1;
       int32_t   newvalue_2;
       ...
       int32_t   newvalue_n;
       int32_t   default_1;
       int32_t   default_2;
       ...
       int32_t   default_n;
   };

   or

   struct array {
       int32_t   value_1;
       int32_t   value_2;
       ...
       int32_t   value_n;
   } data[2];
   fill data[0] with new value to add
   fill data[1] with default value

   xattrop GF_XATTROP_ADD_ARRAY_WITH_DEFAULT
   for i from 1 to n
   {
       if (xattr (dest_i) is zero or not set in the disk)
           dest_i = newvalue_i + default_i
       else
           dest_i = dest_i + newvalue_i
   }

   value in xdata after xattrop is successful
   struct array {
       int32_t   dest_1;
       int32_t   dest_2;
       ...
       int32_t   dest_n;
   };
*/
static void
__add_array_with_default (int32_t *dest, int32_t *src, int count)
{
        int     i       = 0;
        int32_t destval = 0;

        for (i = 0; i < count; i++) {
                destval = ntoh32 (dest[i]);
                if (destval == 0)
                        dest[i] = hton32 (ntoh32 (src[i]) +
                                          ntoh32 (src[count + i]));
                else
                        dest[i] = hton32 (destval + ntoh32 (src[i]));
        }
}

static void
__add_long_array_with_default (int64_t *dest, int64_t *src, int count)
{
        int     i       = 0;
        int64_t destval = 0;

        for (i = 0; i < count; i++) {
                destval = ntoh64 (dest[i]);
                if (destval == 0)
                        dest[i] = hton64 (ntoh64 (src[i]) +
                                          ntoh64 (src[i + count]));
                else
                        dest[i] = hton64 (destval + ntoh64 (src[i]));
        }
}

static int
_posix_handle_xattr_keyvalue_pair (dict_t *d, char *k, data_t *v,
                                   void *tmp)
{
        int                   size     = 0;
        int                   count    = 0;
        int                   op_ret   = 0;
        int                   op_errno = 0;
        gf_xattrop_flags_t    optype   = 0;
        char                 *array    = NULL;
        char                 *dst_data = NULL;
        inode_t              *inode    = NULL;
        xlator_t             *this     = NULL;
        posix_xattr_filler_t *filler   = NULL;

        filler = tmp;

        optype = (gf_xattrop_flags_t)(filler->flags);
        this = filler->this;
        inode = filler->inode;
        count = v->len;
        if (optype == GF_XATTROP_ADD_ARRAY_WITH_DEFAULT ||
            optype == GF_XATTROP_ADD_ARRAY64_WITH_DEFAULT)
                count = count / 2;

        array = GF_CALLOC (count, sizeof (char), gf_posix_mt_char);

#ifdef GF_DARWIN_HOST_OS
        struct posix_private *priv     = NULL;
        priv = this->private;
        if (priv->xattr_user_namespace == XATTR_STRIP) {
                if (strncmp(k, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN) == 0) {
                        k += XATTR_USER_PREFIX_LEN;
                }
        }
#endif

        LOCK (&inode->lock);
        {
                if (filler->real_path) {
                        size = sys_lgetxattr (filler->real_path, k,
                                              (char *)array, count);
                } else {
                        size = sys_fgetxattr (filler->fdnum, k, (char *)array,
                                              count);
                }

                op_errno = errno;
                if ((size == -1) && (op_errno != ENODATA) &&
                    (op_errno != ENOATTR)) {
                        if (op_errno == ENOTSUP) {
                                GF_LOG_OCCASIONALLY(gf_posix_xattr_enotsup_log,
                                                    this->name, GF_LOG_WARNING,
                                                    "Extended attributes not "
                                                    "supported by filesystem");
                        } else if (op_errno != ENOENT ||
                                   !posix_special_xattr (marker_xattrs,
                                                         k)) {
                                if (filler->real_path)
                                        gf_msg (this->name, fop_log_level (GF_FOP_XATTROP,
                                                op_errno),  op_errno, P_MSG_XATTR_FAILED,
                                                "getxattr failed on %s while "
                                                "doing xattrop: Key:%s ",
                                                filler->real_path, k);
                                else
                                        gf_msg (this->name, GF_LOG_ERROR,
                                                op_errno, P_MSG_XATTR_FAILED,
                                                "fgetxattr failed on gfid=%s "
                                                "while doing xattrop: "
                                                "Key:%s (%s)",
                                                uuid_utoa (filler->inode->gfid),
                                                k, strerror (op_errno));
                        }

                        op_ret = -1;
                        goto unlock;
                }

                if (size == -1 && optype == GF_XATTROP_GET_AND_SET) {
                        GF_FREE (array);
                        array = NULL;
                }

                /* We only write back the xattr if it has been really modified
                 * (i.e. v->data is not all 0's). Otherwise we return its value
                 * but we don't update anything.
                 *
                 * If the xattr does not exist, a value of all 0's is returned
                 * without creating it. */
                size = count;
                if (optype != GF_XATTROP_GET_AND_SET &&
                    mem_0filled(v->data, v->len) == 0)
                        goto unlock;

                dst_data = array;
                switch (optype) {

                case GF_XATTROP_ADD_ARRAY:
                        __add_array ((int32_t *) array,
                                     (int32_t *) v->data, count / 4);
                        break;

                case GF_XATTROP_ADD_ARRAY64:
                        __add_long_array ((int64_t *) array,
                                          (int64_t *) v->data,
                                          count / 8);
                        break;

                case GF_XATTROP_ADD_ARRAY_WITH_DEFAULT:
                        __add_array_with_default ((int32_t *) array,
                                                  (int32_t *) v->data,
                                                  count / 4);
                        break;

                case GF_XATTROP_ADD_ARRAY64_WITH_DEFAULT:
                        __add_long_array_with_default ((int64_t *) array,
                                                       (int64_t *) v->data,
                                                       count / 8);
                        break;

                case GF_XATTROP_GET_AND_SET:
                        dst_data = v->data;
                        break;

                default:
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                P_MSG_UNKNOWN_OP, "Unknown xattrop type (%d)"
                                " on %s. Please send a bug report to "
                                "gluster-devel@gluster.org", optype,
                                filler->real_path);
                        op_ret = -1;
                        op_errno = EINVAL;
                        goto unlock;
                }

                if (filler->real_path) {
                        size = sys_lsetxattr (filler->real_path, k,
                                              dst_data, count, 0);
                } else {
                        size = sys_fsetxattr (filler->fdnum, k,
                                              (char *)dst_data,
                                              count, 0);
                }
                op_errno = errno;
        }
unlock:
        UNLOCK (&inode->lock);

        if (op_ret == -1)
                goto out;

        if (size == -1) {
                if (filler->real_path)
                        gf_msg (this->name, GF_LOG_ERROR, op_errno,
                                P_MSG_XATTR_FAILED, "setxattr failed on %s "
                                "while doing xattrop: key=%s",
                                filler->real_path, k);
                else
                        gf_msg (this->name, GF_LOG_ERROR, op_errno,
                                P_MSG_XATTR_FAILED,
                                "fsetxattr failed on gfid=%s while doing "
                                "xattrop: key=%s (%s)",
                                uuid_utoa (filler->inode->gfid),
                                k, strerror (op_errno));
                op_ret = -1;
                goto out;
        } else if (array) {
                op_ret = dict_set_bin (filler->xattr, k, array, count);
                if (op_ret) {
                        if (filler->real_path)
                                gf_msg_debug (this->name, 0,
                                        "dict_set_bin failed (path=%s): "
                                        "key=%s (%s)", filler->real_path,
                                        k, strerror (-size));
                        else
                                gf_msg_debug (this->name, 0,
                                        "dict_set_bin failed (gfid=%s): "
                                        "key=%s (%s)",
                                        uuid_utoa (filler->inode->gfid),
                                        k, strerror (-size));

                        op_ret = -1;
                        op_errno = EINVAL;
                        GF_FREE (array);
                        goto out;
                }
                array = NULL;
        }

out:
        if (op_ret < 0)
                filler->op_errno = op_errno;

        if (array)
                GF_FREE (array);

        return op_ret;
}

/**
 * xattrop - xattr operations - for internal use by GlusterFS
 * @optype: ADD_ARRAY:
 *            dict should contain:
 *               "key" ==> array of 32-bit numbers
 */

int
do_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
            gf_xattrop_flags_t optype, dict_t *xattr)
{
        int                   op_ret    = 0;
        int                   op_errno  = 0;
        int                   _fd       = -1;
        char                 *real_path = NULL;
        struct posix_fd      *pfd       = NULL;
        inode_t              *inode     = NULL;
        posix_xattr_filler_t  filler    = {0,};
        dict_t               *xdata     = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (xattr, out);
        VALIDATE_OR_GOTO (this, out);

        if (fd) {
                op_ret = posix_fd_ctx_get (fd, this, &pfd, &op_errno);
                if (op_ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING,
                                fop_log_level(GF_FOP_FXATTROP, op_errno),
                                P_MSG_PFD_GET_FAILED, "failed to get pfd from"
                                " fd=%p", fd);
                        goto out;
                }
                _fd = pfd->fd;
        }

        if (loc && !gf_uuid_is_null (loc->gfid)) {
                MAKE_INODE_HANDLE (real_path, this, loc, NULL);
                if (!real_path) {
                        op_ret = -1;
                        op_errno = ESTALE;
                        goto out;
                }
        }

        if (real_path) {
                inode = loc->inode;
        } else if (fd) {
                inode = fd->inode;
        }

        xdata = dict_new ();
        if (xdata == NULL) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        filler.this = this;
        filler.fdnum = _fd;
        filler.real_path = real_path;
        filler.flags = (int)optype;
        filler.inode = inode;
        filler.xattr = xdata;

        op_ret = dict_foreach (xattr, _posix_handle_xattr_keyvalue_pair,
                               &filler);
        op_errno = filler.op_errno;

out:

        STACK_UNWIND_STRICT (xattrop, frame, op_ret, op_errno, xdata, NULL);

        if (xdata)
                dict_unref (xdata);

        return 0;
}


int
posix_xattrop (call_frame_t *frame, xlator_t *this,
               loc_t *loc, gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
        do_xattrop (frame, this, loc, NULL, optype, xattr);
        return 0;
}


int
posix_fxattrop (call_frame_t *frame, xlator_t *this,
                fd_t *fd, gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
        do_xattrop (frame, this, NULL, fd, optype, xattr);
        return 0;
}


int
posix_access (call_frame_t *frame, xlator_t *this,
              loc_t *loc, int32_t mask, dict_t *xdata)
{
        int32_t                 op_ret    = -1;
        int32_t                 op_errno  = 0;
        char                   *real_path = NULL;

        DECLARE_OLD_FS_ID_VAR;
        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        MAKE_INODE_HANDLE (real_path, this, loc, NULL);
        if (!real_path) {
                op_ret = -1;
                op_errno = errno;
                goto out;
        }

        op_ret = sys_access (real_path, mask & 07);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_ACCESS_FAILED,
                        "access failed on %s", real_path);
                goto out;
        }
        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (access, frame, op_ret, op_errno, NULL);
        return 0;
}


int32_t
posix_ftruncate (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, off_t offset, dict_t *xdata)
{
        int32_t               op_ret   = -1;
        int32_t               op_errno = 0;
        int                   _fd      = -1;
        struct iatt           preop    = {0,};
        struct iatt           postop   = {0,};
        struct posix_fd      *pfd      = NULL;
        int                   ret      = -1;
        struct posix_private *priv     = NULL;

        DECLARE_OLD_FS_ID_VAR;
        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        ret = posix_fd_ctx_get (fd, this, &pfd, &op_errno);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, op_errno, P_MSG_PFD_NULL,
                        "pfd is NULL, fd=%p", fd);
                goto out;
        }

        _fd = pfd->fd;

        op_ret = posix_fdstat (this, _fd, &preop);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_FSTAT_FAILED,
                        "pre-operation fstat failed on fd=%p", fd);
                goto out;
        }

        op_ret = sys_ftruncate (_fd, offset);

        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_TRUNCATE_FAILED,
                        "ftruncate failed on fd=%p (%"PRId64"", fd, offset);
                goto out;
        }

        op_ret = posix_fdstat (this, _fd, &postop);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_FSTAT_FAILED,
                        "post-operation fstat failed on fd=%p", fd);
                goto out;
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, &preop,
                             &postop, NULL);

        return 0;
}


int32_t
posix_fstat (call_frame_t *frame, xlator_t *this,
             fd_t *fd, dict_t *xdata)
{
        int                   _fd      = -1;
        int32_t               op_ret   = -1;
        int32_t               op_errno = 0;
        struct iatt           buf      = {0,};
        struct posix_fd      *pfd      = NULL;
        dict_t               *xattr_rsp = NULL;
        int                   ret      = -1;
        struct posix_private *priv     = NULL;

        DECLARE_OLD_FS_ID_VAR;
        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        ret = posix_fd_ctx_get (fd, this, &pfd, &op_errno);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, op_errno, P_MSG_PFD_NULL,
                        "pfd is NULL, fd=%p", fd);
                op_errno = -ret;
                goto out;
        }

        _fd = pfd->fd;

        op_ret = posix_fdstat (this, _fd, &buf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_FSTAT_FAILED,
                        "fstat failed on fd=%p", fd);
                goto out;
        }

        if (xdata)
                xattr_rsp = posix_xattr_fill (this, NULL, NULL, fd, _fd, xdata,
                                              &buf);

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, &buf, xattr_rsp);
        if (xattr_rsp)
                dict_unref (xattr_rsp);
        return 0;
}

int32_t
posix_lease (call_frame_t *frame, xlator_t *this,
             loc_t *loc, struct gf_lease *lease, dict_t *xdata)
{
        struct gf_lease nullease = {0, };

        gf_msg (this->name, GF_LOG_CRITICAL, EINVAL, P_MSG_LEASE_DISABLED,
                "\"features/leases\" translator is not loaded. You need"
                "to use it for proper functioning of your application");

        STACK_UNWIND_STRICT (lease, frame, -1, ENOSYS, &nullease, NULL);
        return 0;
}

static int gf_posix_lk_log;

int32_t
posix_lk (call_frame_t *frame, xlator_t *this,
          fd_t *fd, int32_t cmd, struct gf_flock *lock, dict_t *xdata)
{
        struct gf_flock nullock = {0, };

        GF_LOG_OCCASIONALLY (gf_posix_lk_log, this->name, GF_LOG_CRITICAL,
                             "\"features/locks\" translator is "
                             "not loaded. You need to use it for proper "
                             "functioning of your application.");

        STACK_UNWIND_STRICT (lk, frame, -1, ENOSYS, &nullock, NULL);
        return 0;
}

int32_t
posix_inodelk (call_frame_t *frame, xlator_t *this,
               const char *volume, loc_t *loc, int32_t cmd,
               struct gf_flock *lock, dict_t *xdata)
{
        GF_LOG_OCCASIONALLY (gf_posix_lk_log, this->name, GF_LOG_CRITICAL,
                             "\"features/locks\" translator is "
                             "not loaded. You need to use it for proper "
                             "functioning of your application.");

        STACK_UNWIND_STRICT (inodelk, frame, -1, ENOSYS, NULL);
        return 0;
}

int32_t
posix_finodelk (call_frame_t *frame, xlator_t *this,
                const char *volume, fd_t *fd, int32_t cmd,
                struct gf_flock *lock, dict_t *xdata)
{
        GF_LOG_OCCASIONALLY (gf_posix_lk_log, this->name, GF_LOG_CRITICAL,
                             "\"features/locks\" translator is "
                             "not loaded. You need to use it for proper "
                             "functioning of your application.");

        STACK_UNWIND_STRICT (finodelk, frame, -1, ENOSYS, NULL);
        return 0;
}


int32_t
posix_entrylk (call_frame_t *frame, xlator_t *this,
               const char *volume, loc_t *loc, const char *basename,
               entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        GF_LOG_OCCASIONALLY (gf_posix_lk_log, this->name, GF_LOG_CRITICAL,
                             "\"features/locks\" translator is "
                             "not loaded. You need to use it for proper "
                             "functioning of your application.");

        STACK_UNWIND_STRICT (entrylk, frame, -1, ENOSYS, NULL);
        return 0;
}

int32_t
posix_fentrylk (call_frame_t *frame, xlator_t *this,
                const char *volume, fd_t *fd, const char *basename,
                entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        GF_LOG_OCCASIONALLY (gf_posix_lk_log, this->name, GF_LOG_CRITICAL,
                             "\"features/locks\" translator is "
                             "not loaded. You need to use it for proper "
                             "functioning of your application.");

        STACK_UNWIND_STRICT (fentrylk, frame, -1, ENOSYS, NULL);
        return 0;
}


int
posix_fill_readdir (fd_t *fd, DIR *dir, off_t off, size_t size,
                    gf_dirent_t *entries, xlator_t *this, int32_t skip_dirs)
{
        off_t            in_case        = -1;
        off_t            last_off       = 0;
        size_t           filled         = 0;
        int              count          = 0;
        int32_t          this_size      = -1;
        gf_dirent_t     *this_entry     = NULL;
        struct posix_fd *pfd            = NULL;
        struct stat      stbuf          = {0,};
        char            *hpath          = NULL;
        int              len            = 0;
        int              ret            = 0;
        int              op_errno       = 0;
        struct dirent   *entry          = NULL;
        struct dirent    scratch[2]     = {{0,},};

        ret = posix_fd_ctx_get (fd, this, &pfd, &op_errno);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, op_errno, P_MSG_PFD_NULL,
                        "pfd is NULL, fd=%p", fd);
                count = -1;
                errno = op_errno;
                goto out;
        }

        if (skip_dirs) {
                len = posix_handle_path (this, fd->inode->gfid, NULL, NULL, 0);
                if (len <= 0) {
                        errno = ESTALE;
                        count = -1;
                        goto out;
                }
                hpath = alloca (len + 256); /* NAME_MAX */

                if (posix_handle_path (this, fd->inode->gfid, NULL, hpath,
                                       len) <= 0) {
                        errno = ESTALE;
                        count = -1;
                        goto out;
                }

                len = strlen (hpath);
                hpath[len] = '/';
        }

        if (!off) {
                rewinddir (dir);
        } else {
                seekdir (dir, off);
#ifndef GF_LINUX_HOST_OS
                if ((u_long)telldir(dir) != off && off != pfd->dir_eof) {
                        gf_msg (THIS->name, GF_LOG_ERROR, EINVAL,
                                P_MSG_DIR_OPERATION_FAILED,
                                "seekdir(0x%llx) failed on dir=%p: "
                                "Invalid argument (offset reused from "
                                "another DIR * structure?)", off, dir);
                        errno = EINVAL;
                        count = -1;
                        goto out;
                }
#endif /* GF_LINUX_HOST_OS */
        }

        while (filled <= size) {
                in_case = (u_long)telldir (dir);

                if (in_case == -1) {
                        gf_msg (THIS->name, GF_LOG_ERROR, errno,
                                P_MSG_DIR_OPERATION_FAILED,
                                "telldir failed on dir=%p", dir);
                        goto out;
                }

                errno = 0;

                entry = sys_readdir (dir, scratch);

                if (!entry || errno != 0) {
                        if (errno == EBADF) {
                                gf_msg (THIS->name, GF_LOG_WARNING, errno,
                                        P_MSG_DIR_OPERATION_FAILED,
                                        "readdir failed on dir=%p",
                                        dir);
                                goto out;
                        }
                        break;
                }

#ifdef __NetBSD__
	       /*
		* NetBSD with UFS1 backend uses backing files for
		* extended attributes. They can be found in a
		* .attribute file located at the root of the filesystem
		* We hide it to glusterfs clients, since chaos will occur
		* when the cluster/dht xlator decides to distribute
		* exended attribute backing file across storage servers.
		*/
		if (__is_root_gfid (fd->inode->gfid) == 0
		    && (!strcmp(entry->d_name, ".attribute")))
			continue;
#endif /* __NetBSD__ */

                if (__is_root_gfid (fd->inode->gfid)
                    && (!strcmp (GF_HIDDEN_PATH, entry->d_name))) {
                        continue;
                }

                if (skip_dirs) {
                        if (DT_ISDIR (entry->d_type)) {
                                continue;
                        } else if (hpath) {
                                strcpy (&hpath[len+1], entry->d_name);
                                ret = sys_lstat (hpath, &stbuf);
                                if (!ret && S_ISDIR (stbuf.st_mode))
                                        continue;
                        }
                }

                this_size = max (sizeof (gf_dirent_t),
                                 sizeof (gfs3_dirplist))
                        + strlen (entry->d_name) + 1;

                if (this_size + filled > size) {
                        seekdir (dir, in_case);
#ifndef GF_LINUX_HOST_OS
                        if ((u_long)telldir(dir) != in_case &&
                            in_case != pfd->dir_eof) {
                                gf_msg (THIS->name, GF_LOG_ERROR, EINVAL,
                                        P_MSG_DIR_OPERATION_FAILED,
                                        "seekdir(0x%llx) failed on dir=%p: "
                                        "Invalid argument (offset reused from "
                                        "another DIR * structure?)",
                                        in_case, dir);
                                errno = EINVAL;
                                count = -1;
                                goto out;
                        }
#endif /* GF_LINUX_HOST_OS */
                        break;
                }

                this_entry = gf_dirent_for_name (entry->d_name);

                if (!this_entry) {
                        gf_msg (THIS->name, GF_LOG_ERROR, errno,
                                P_MSG_GF_DIRENT_CREATE_FAILED,
                                "could not create "
                                "gf_dirent for entry %s", entry->d_name);
                        goto out;
                }
                /*
                 * we store the offset of next entry here, which is
                 * probably not intended, but code using syncop_readdir()
                 * (glfs-heal.c, afr-self-heald.c, pump.c) rely on it
                 * for directory read resumption.
                 */
                last_off = (u_long)telldir(dir);
                this_entry->d_off = last_off;
                this_entry->d_ino = entry->d_ino;
                this_entry->d_type = entry->d_type;

                list_add_tail (&this_entry->list, &entries->list);

                filled += this_size;
                count ++;
        }

        if ((!sys_readdir (dir, scratch) && (errno == 0))) {
                /* Indicate EOF */
                errno = ENOENT;
                /* Remember EOF offset for later detection */
                pfd->dir_eof = (u_long)last_off;
        }
out:
        return count;
}

dict_t *
posix_entry_xattr_fill (xlator_t *this, inode_t *inode,
                        fd_t *fd, char *entry_path, dict_t *dict,
                        struct iatt *stbuf)
{
        loc_t  tmp_loc    = {0,};

        /* if we don't send the 'loc', open-fd-count be a problem. */
        tmp_loc.inode = inode;

        return posix_xattr_fill (this, entry_path, &tmp_loc, NULL, -1, dict,
                                 stbuf);

}


#ifdef _DIRENT_HAVE_D_TYPE
static int
posix_d_type_from_ia_type (ia_type_t type)
{
        switch (type) {
        case IA_IFDIR:      return DT_DIR;
        case IA_IFCHR:      return DT_CHR;
        case IA_IFBLK:      return DT_BLK;
        case IA_IFIFO:      return DT_FIFO;
        case IA_IFLNK:      return DT_LNK;
        case IA_IFREG:      return DT_REG;
        case IA_IFSOCK:     return DT_SOCK;
        default:            return DT_UNKNOWN;
        }
}
#endif


int
posix_readdirp_fill (xlator_t *this, fd_t *fd, gf_dirent_t *entries, dict_t *dict)
{
        gf_dirent_t     *entry    = NULL;
        inode_table_t   *itable   = NULL;
	inode_t         *inode    = NULL;
	char            *hpath    = NULL;
	int              len      = 0;
        struct iatt      stbuf    = {0, };
	uuid_t           gfid;
        int              ret      = -1;

	if (list_empty(&entries->list))
		return 0;

        itable = fd->inode->table;

	len = posix_handle_path (this, fd->inode->gfid, NULL, NULL, 0);
        if (len <= 0)
                return -1;
	hpath = alloca (len + 256); /* NAME_MAX */
	if (posix_handle_path (this, fd->inode->gfid, NULL, hpath, len) <= 0)
                return -1;
	len = strlen (hpath);
	hpath[len] = '/';

        list_for_each_entry (entry, &entries->list, list) {
		memset (gfid, 0, 16);
		inode = inode_grep (fd->inode->table, fd->inode,
				    entry->d_name);
		if (inode)
			gf_uuid_copy (gfid, inode->gfid);

		strcpy (&hpath[len+1], entry->d_name);

                ret = posix_pstat (this, gfid, hpath, &stbuf);

                if (ret == -1) {
                        if (inode)
                                inode_unref (inode);
                      continue;
                }

		if (!inode)
			inode = inode_find (itable, stbuf.ia_gfid);

		if (!inode)
			inode = inode_new (itable);

		entry->inode = inode;

                if (dict) {
                        entry->dict =
                                posix_entry_xattr_fill (this, entry->inode,
                                                        fd, hpath,
                                                        dict, &stbuf);
                }

                entry->d_stat = stbuf;
                if (stbuf.ia_ino)
                        entry->d_ino = stbuf.ia_ino;

#ifdef _DIRENT_HAVE_D_TYPE
                if (entry->d_type == DT_UNKNOWN && !IA_ISINVAL(stbuf.ia_type)) {
                        /* The platform supports d_type but the underlying
                           filesystem doesn't. We set d_type to the correct
                           value from ia_type */
                        entry->d_type =
                                posix_d_type_from_ia_type (stbuf.ia_type);
                }
#endif

		inode = NULL;
        }

	return 0;
}


int32_t
posix_do_readdir (call_frame_t *frame, xlator_t *this,
                  fd_t *fd, size_t size, off_t off, int whichop, dict_t *dict)
{
        struct posix_fd *pfd       = NULL;
        DIR             *dir       = NULL;
        int              ret       = -1;
        int              count     = 0;
        int32_t          op_ret    = -1;
        int32_t          op_errno  = 0;
        gf_dirent_t      entries;
        int32_t          skip_dirs = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        INIT_LIST_HEAD (&entries.list);

        ret = posix_fd_ctx_get (fd, this, &pfd, &op_errno);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, -ret, P_MSG_PFD_NULL,
                        "pfd is NULL, fd=%p", fd);
                goto out;
        }

        dir = pfd->dir;

        if (!dir) {
                gf_msg (this->name, GF_LOG_WARNING, EINVAL, P_MSG_PFD_NULL,
                        "dir is NULL for fd=%p", fd);
                op_errno = EINVAL;
                goto out;
	}

        /* When READDIR_FILTER option is set to on, we can filter out
         * directory's entry from the entry->list.
         */
        ret = dict_get_int32 (dict, GF_READDIR_SKIP_DIRS, &skip_dirs);

	LOCK (&fd->lock);
	{
		/* posix_fill_readdir performs multiple separate individual
		   readdir() calls to fill up the buffer.

		   In case of NFS where the same anonymous FD is shared between
		   different applications, reading a common directory can
		   result in the anonymous fd getting re-used unsafely between
		   the two readdir requests (in two different io-threads).

		   It would also help, in the future, to replace the loop
		   around readdir() with a single large getdents() call.
		*/
		count = posix_fill_readdir (fd, dir, off, size, &entries, this,
					    skip_dirs);
	}
	UNLOCK (&fd->lock);

        /* pick ENOENT to indicate EOF */
        op_errno = errno;
        op_ret = count;

        if (whichop != GF_FOP_READDIRP)
                goto out;

	posix_readdirp_fill (this, fd, &entries, dict);

out:
        STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, &entries, NULL);

        gf_dirent_free (&entries);

        return 0;
}


int32_t
posix_readdir (call_frame_t *frame, xlator_t *this,
               fd_t *fd, size_t size, off_t off, dict_t *xdata)
{
        posix_do_readdir (frame, this, fd, size, off, GF_FOP_READDIR, xdata);
        return 0;
}


int32_t
posix_readdirp (call_frame_t *frame, xlator_t *this,
                fd_t *fd, size_t size, off_t off, dict_t *dict)
{
        gf_dirent_t entries;
        int32_t     op_ret = -1, op_errno = 0;
        gf_dirent_t     *entry     = NULL;


        if ((dict != NULL) && (dict_get (dict, GET_ANCESTRY_DENTRY_KEY))) {
                INIT_LIST_HEAD (&entries.list);

                op_ret = posix_get_ancestry (this, fd->inode, &entries, NULL,
                                             POSIX_ANCESTRY_DENTRY,
                                             &op_errno, dict);
                if (op_ret >= 0) {
                        op_ret = 0;

                        list_for_each_entry (entry, &entries.list, list) {
                                op_ret++;
                        }
                }

                STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, &entries,
                                     NULL);

                gf_dirent_free (&entries);
                return 0;
        }

        posix_do_readdir (frame, this, fd, size, off, GF_FOP_READDIRP, dict);
        return 0;
}

int32_t
posix_priv (xlator_t *this)
{
        struct posix_private *priv = NULL;
        char  key_prefix[GF_DUMP_MAX_BUF_LEN];

        (void) snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s",
                        this->type, this->name);
        gf_proc_dump_add_section(key_prefix);

        if (!this)
                return 0;

        priv = this->private;

        if (!priv)
                return 0;

        gf_proc_dump_write("base_path","%s", priv->base_path);
        gf_proc_dump_write("base_path_length","%d", priv->base_path_length);
        gf_proc_dump_write("max_read","%d", priv->read_value);
        gf_proc_dump_write("max_write","%d", priv->write_value);
        gf_proc_dump_write("nr_files","%ld", priv->nr_files);

        return 0;
}

int32_t
posix_inode (xlator_t *this)
{
        return 0;
}


int32_t
posix_rchecksum (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, off_t offset, int32_t len, dict_t *xdata)
{
        char                    *alloc_buf      = NULL;
        char                    *buf            = NULL;
        int                     _fd             = -1;
        struct posix_fd         *pfd            = NULL;
        int                     op_ret          = -1;
        int                     op_errno        = 0;
        int                     ret             = 0;
        ssize_t                 bytes_read      = 0;
        int32_t                 weak_checksum   = 0;
        int32_t                 zerofillcheck   = 0;
        unsigned char           strong_checksum[MD5_DIGEST_LENGTH] = {0};
        struct posix_private    *priv           = NULL;
        dict_t                  *rsp_xdata      = NULL;
        gf_boolean_t            buf_has_zeroes  = _gf_false;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;
        memset (strong_checksum, 0, MD5_DIGEST_LENGTH);

        alloc_buf = _page_aligned_alloc (len, &buf);
        if (!alloc_buf) {
                op_errno = ENOMEM;
                goto out;
        }

        rsp_xdata = dict_new();
        if (!rsp_xdata) {
                op_errno = ENOMEM;
                goto out;
        }

        ret = posix_fd_ctx_get (fd, this, &pfd, &op_errno);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, -ret, P_MSG_PFD_NULL,
                        "pfd is NULL, fd=%p", fd);
                goto out;
        }

        _fd = pfd->fd;

        LOCK (&fd->lock);
        {
                if (priv->aio_capable && priv->aio_init_done)
                        __posix_fd_set_odirect (fd, pfd, 0, offset, len);

                bytes_read = sys_pread (_fd, buf, len, offset);
                if (bytes_read < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_PREAD_FAILED,
                                "pread of %d bytes returned %zd", len,
                                bytes_read);

                        op_errno = errno;
                }

        }
        UNLOCK (&fd->lock);

        if (bytes_read < 0)
                goto out;

        if (xdata && dict_get_int32 (xdata, "check-zero-filled",
                                     &zerofillcheck) == 0) {
                buf_has_zeroes = (mem_0filled (buf, bytes_read)) ? _gf_false :
                                 _gf_true;
                ret = dict_set_uint32 (rsp_xdata, "buf-has-zeroes",
                                       buf_has_zeroes);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, -ret,
                                P_MSG_DICT_SET_FAILED, "%s: Failed to set "
                                "dictionary value for key: %s",
                                uuid_utoa (fd->inode->gfid), "buf-has-zeroes");
                        op_errno = -ret;
                        goto out;
                }
        }
        weak_checksum = gf_rsync_weak_checksum ((unsigned char *) buf, (size_t) ret);
        gf_rsync_strong_checksum ((unsigned char *) buf, (size_t) bytes_read,
                                  (unsigned char *) strong_checksum);

        op_ret = 0;
out:
        STACK_UNWIND_STRICT (rchecksum, frame, op_ret, op_errno,
                             weak_checksum, strong_checksum, rsp_xdata);
        if (rsp_xdata)
                dict_unref (rsp_xdata);
        GF_FREE (alloc_buf);

        return 0;
}


/**
 * notify - when parent sends PARENT_UP, send CHILD_UP event from here
 */
int32_t
notify (xlator_t *this,
        int32_t event,
        void *data,
        ...)
{
        switch (event)
        {
        case GF_EVENT_PARENT_UP:
        {
                /* Tell the parent that posix xlator is up */
                default_notify (this, GF_EVENT_CHILD_UP, data);
        }
        break;
        default:
                /* */
                break;
        }
        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_posix_mt_end + 1);

        if (ret != 0) {
                return ret;
        }

        return ret;
}

static int
posix_set_owner (xlator_t *this, uid_t uid, gid_t gid)
{
        struct posix_private *priv = NULL;
        int                   ret  = -1;
	struct stat st = {0,};

        priv = this->private;

	ret = sys_lstat (priv->base_path, &st);
	if (ret) {
		gf_msg (this->name, GF_LOG_ERROR, errno,
                        P_MSG_DIR_OPERATION_FAILED, "Failed to stat "
			"brick path %s",
			priv->base_path);
		return ret;
	}

	if ((uid == -1 || st.st_uid == uid) &&
	    (gid == -1 || st.st_gid == gid))
		return 0;

        ret = sys_chown (priv->base_path, uid, gid);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        P_MSG_DIR_OPERATION_FAILED, "Failed to set uid/gid for"
                        " brick path %s", priv->base_path);

        return ret;
}


static int
set_batch_fsync_mode (struct posix_private *priv, const char *str)
{
	if (strcmp (str, "none") == 0)
		priv->batch_fsync_mode = BATCH_NONE;
	else if (strcmp (str, "syncfs") == 0)
		priv->batch_fsync_mode = BATCH_SYNCFS;
	else if (strcmp (str, "syncfs-single-fsync") == 0)
		priv->batch_fsync_mode = BATCH_SYNCFS_SINGLE_FSYNC;
	else if (strcmp (str, "syncfs-reverse-fsync") == 0)
		priv->batch_fsync_mode = BATCH_SYNCFS_REVERSE_FSYNC;
	else if (strcmp (str, "reverse-fsync") == 0)
		priv->batch_fsync_mode = BATCH_REVERSE_FSYNC;
	else
		return -1;

	return 0;
}

#ifdef GF_DARWIN_HOST_OS
static int
set_xattr_user_namespace_mode (struct posix_private *priv, const char *str)
{
        if (strcmp (str, "none") == 0)
                priv->xattr_user_namespace = XATTR_NONE;
        else if (strcmp (str, "strip") == 0)
                priv->xattr_user_namespace = XATTR_STRIP;
        else if (strcmp (str, "append") == 0)
                priv->xattr_user_namespace = XATTR_APPEND;
        else if (strcmp (str, "both") == 0)
                priv->xattr_user_namespace = XATTR_BOTH;
        else
                return -1;
        return 0;
}
#endif

int
reconfigure (xlator_t *this, dict_t *options)
{
	int                   ret = -1;
struct posix_private *priv = NULL;
        int32_t               uid = -1;
        int32_t               gid = -1;
	char                 *batch_fsync_mode_str = NULL;

	priv = this->private;

        GF_OPTION_RECONF ("brick-uid", uid, options, int32, out);
        GF_OPTION_RECONF ("brick-gid", gid, options, int32, out);
	if (uid != -1 || gid != -1)
		posix_set_owner (this, uid, gid);

	GF_OPTION_RECONF ("batch-fsync-delay-usec", priv->batch_fsync_delay_usec,
			  options, uint32, out);

	GF_OPTION_RECONF ("batch-fsync-mode", batch_fsync_mode_str,
			  options, str, out);

	if (set_batch_fsync_mode (priv, batch_fsync_mode_str) != 0) {
		gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_INVALID_ARGUMENT,
                        "Unknown mode string: %s", batch_fsync_mode_str);
		goto out;
	}

#ifdef GF_DARWIN_HOST_OS

        char   *xattr_user_namespace_mode_str = NULL;

        GF_OPTION_RECONF ("xattr-user-namespace-mode", xattr_user_namespace_mode_str,
                          options, str, out);

        if (set_xattr_user_namespace_mode (priv, xattr_user_namespace_mode_str) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_UNKNOWN_ARGUMENT,
                        "Unknown xattr user namespace mode string: %s",
                        xattr_user_namespace_mode_str);
                goto out;
        }

#endif

	GF_OPTION_RECONF ("linux-aio", priv->aio_configured,
			  options, bool, out);

	if (priv->aio_configured)
		posix_aio_on (this);
	else
		posix_aio_off (this);

        GF_OPTION_RECONF ("update-link-count-parent", priv->update_pgfid_nlinks,
                          options, bool, out);

        GF_OPTION_RECONF ("node-uuid-pathinfo", priv->node_uuid_pathinfo,
                          options, bool, out);

        if (priv->node_uuid_pathinfo &&
                        (gf_uuid_is_null (priv->glusterd_uuid))) {
                gf_msg (this->name, GF_LOG_INFO, 0, P_MSG_UUID_NULL,
                        "glusterd uuid is NULL, pathinfo xattr would"
                        " fallback to <hostname>:<export>");
        }

        GF_OPTION_RECONF ("health-check-interval", priv->health_check_interval,
                          options, uint32, out);
        posix_spawn_health_check_thread (this);

	ret = 0;
out:
	return ret;
}

int32_t
posix_delete_unlink_entry (const char *fpath, const struct stat *sb,
                   int typeflag, struct FTW *ftwbuf) {

        int    ret = 0;

        if (!fpath)
                goto out;

        switch (typeflag) {
        case FTW_SL:
        case FTW_NS:
        case FTW_F:
        case FTW_SLN:
                ret = sys_unlink(fpath);
                break;
        case FTW_D:
        case FTW_DP:
        case FTW_DNR:
                if (ftwbuf->level != 0) {
                        ret = sys_rmdir(fpath);
                }
                break;
        default:
                break;
        }
        if (ret) {
                gf_msg ("posix_delete_unlink_entry", GF_LOG_WARNING, errno,
                        P_MSG_HANDLE_CREATE,
                        "Deletion of entries %s failed"
                        "Please delete it manually",
                        fpath);
        }
out:
        return 0;
}

int32_t
posix_delete_unlink (const char *unlink_path) {

        int    ret = -1;
        int    flags = 0;

        flags |= (FTW_DEPTH | FTW_PHYS);

        ret = nftw(unlink_path, posix_delete_unlink_entry, 2, flags);
        if (ret) {
                gf_msg ("posix_delete_unlink", GF_LOG_ERROR, 0,
                        P_MSG_HANDLE_CREATE,
                        "Deleting files from  %s failed",
                        unlink_path);
        }
        return ret;
}

int32_t
posix_create_unlink_dir (xlator_t *this) {

        struct posix_private *priv = NULL;
        struct stat           stbuf;
        int                   ret = -1;
        uuid_t                gfid = {0};
        char                  gfid_str[64] = {0};
        char                  unlink_path[PATH_MAX] = {0,};
        char                  landfill_path[PATH_MAX] = {0,};

        priv = this->private;

        (void) snprintf (unlink_path, sizeof(unlink_path), "%s/%s",
                         priv->base_path, GF_UNLINK_PATH);

        gf_uuid_generate (gfid);
        uuid_utoa_r (gfid, gfid_str);

        (void) snprintf (landfill_path, sizeof(landfill_path), "%s/%s/%s",
                         priv->base_path, GF_LANDFILL_PATH, gfid_str);

        ret = sys_stat (unlink_path, &stbuf);
        switch (ret) {
        case -1:
                if (errno != ENOENT) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_HANDLE_CREATE,
                                "Checking for %s failed",
                                unlink_path);
                        return -1;
                }
                break;
        case 0:
                if (!S_ISDIR (stbuf.st_mode)) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_HANDLE_CREATE,
                                "Not a directory: %s",
                                unlink_path);
                        return -1;
                }
                ret = posix_delete_unlink (unlink_path);
                return 0;
        default:
                break;
        }
        ret = sys_mkdir (unlink_path, 0600);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        P_MSG_HANDLE_CREATE,
                        "Creating directory %s failed",
                        unlink_path);
                return -1;
        }

        return 0;
}

/**
 * init -
 */
int
init (xlator_t *this)
{
        struct posix_private *_private      = NULL;
        data_t               *dir_data      = NULL;
        data_t               *tmp_data      = NULL;
        struct stat           buf           = {0,};
        gf_boolean_t          tmp_bool      = 0;
        int                   ret           = 0;
        int                   op_ret        = -1;
        int                   op_errno      = 0;
        ssize_t               size          = -1;
        uuid_t                old_uuid      = {0,};
        uuid_t                dict_uuid     = {0,};
        uuid_t                gfid          = {0,};
        uuid_t                rootgfid      = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
        char                 *guuid         = NULL;
        int32_t               uid           = -1;
        int32_t               gid           = -1;
	char                 *batch_fsync_mode_str;

        dir_data = dict_get (this->options, "directory");

        if (this->children) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0, P_MSG_SUBVOLUME_ERROR,
                        "FATAL: storage/posix cannot have subvolumes");
                ret = -1;
                goto out;
        }

        if (!this->parents) {
                gf_msg (this->name, GF_LOG_WARNING, 0, P_MSG_VOLUME_DANGLING,
                        "Volume is dangling. Please check the volume file.");
        }

        if (!dir_data) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        P_MSG_EXPORT_DIR_MISSING,
                        "Export directory not specified in volume file.");
                ret = -1;
                goto out;
        }

        umask (000); // umask `masking' is done at the client side

        /* Check whether the specified directory exists, if not log it. */
        op_ret = sys_stat (dir_data->data, &buf);
        if ((op_ret != 0) || !S_ISDIR (buf.st_mode)) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_DIR_OPERATION_FAILED,
                        "Directory '%s' doesn't exist, exiting.",
                        dir_data->data);
                ret = -1;
                goto out;
        }

        _private = GF_CALLOC (1, sizeof (*_private),
                              gf_posix_mt_posix_private);
        if (!_private) {
                ret = -1;
                goto out;
        }

        _private->base_path = gf_strdup (dir_data->data);
        _private->base_path_length = strlen (_private->base_path);

        ret = dict_get_str (this->options, "hostname", &_private->hostname);
        if (ret) {
                _private->hostname = GF_CALLOC (256, sizeof (char),
                                                gf_common_mt_char);
                if (!_private->hostname) {
                        goto out;
                }
                ret = gethostname (_private->hostname, 256);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_HOSTNAME_MISSING,
                                "could not find hostname ");
                }
        }

        /* Check for Extended attribute support, if not present, log it */
        op_ret = sys_lsetxattr (dir_data->data,
                                "trusted.glusterfs.test", "working", 8, 0);
        if (op_ret != -1) {
                sys_lremovexattr (dir_data->data, "trusted.glusterfs.test");
        } else {
                tmp_data = dict_get (this->options,
                                     "mandate-attribute");
                if (tmp_data) {
                        if (gf_string2boolean (tmp_data->data,
                                               &tmp_bool) == -1) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        P_MSG_INVALID_OPTION,
                                        "wrong option provided for key "
                                        "\"mandate-attribute\"");
                                ret = -1;
                                goto out;
                        }
                        if (!tmp_bool) {
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        P_MSG_XATTR_NOTSUP,
                                        "Extended attribute not supported, "
                                        "starting as per option");
                        } else {
                                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                                        P_MSG_XATTR_NOTSUP,
                                        "Extended attribute not supported, "
                                        "exiting.");
                                ret = -1;
                                goto out;
                        }
                } else {
                        gf_msg (this->name, GF_LOG_CRITICAL, 0,
                                P_MSG_XATTR_NOTSUP,
                                "Extended attribute not supported, exiting.");
                        ret = -1;
                        goto out;
                }
        }

        tmp_data = dict_get (this->options, "volume-id");
        if (tmp_data) {
                op_ret = gf_uuid_parse (tmp_data->data, dict_uuid);
                if (op_ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_INVALID_VOLUME_ID,
                                "wrong volume-id (%s) set"
                                " in volume file", tmp_data->data);
                        ret = -1;
                        goto out;
                }
                size = sys_lgetxattr (dir_data->data,
                                      "trusted.glusterfs.volume-id", old_uuid, 16);
                if (size == 16) {
                        if (gf_uuid_compare (old_uuid, dict_uuid)) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        P_MSG_INVALID_VOLUME_ID,
                                        "mismatching volume-id (%s) received. "
                                        "already is a part of volume %s ",
                                        tmp_data->data, uuid_utoa (old_uuid));
                                gf_event (EVENT_POSIX_ALREADY_PART_OF_VOLUME,
                                        "volume-id=%s;brick=%s:%s",
                                        uuid_utoa (old_uuid),
                                       _private->hostname, _private->base_path);
                                ret = -1;
                                goto out;
                        }
                } else if ((size == -1) &&
                           (errno == ENODATA || errno == ENOATTR)) {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        P_MSG_VOLUME_ID_ABSENT,
                                        "Extended attribute trusted.glusterfs."
                                        "volume-id is absent");
                                gf_event (EVENT_POSIX_BRICK_NOT_IN_VOLUME,
                                        "brick=%s:%s",
                                       _private->hostname, _private->base_path);
                                ret = -1;
                                goto out;

                }  else if ((size == -1) && (errno != ENODATA) &&
                            (errno != ENOATTR)) {
                        /* Wrong 'volume-id' is set, it should be error */
                        gf_event (EVENT_POSIX_BRICK_VERIFICATION_FAILED,
                                "brick=%s:%s",
                                _private->hostname, _private->base_path);
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_VOLUME_ID_FETCH_FAILED,
                                "%s: failed to fetch volume-id",
                                dir_data->data);
                        ret = -1;
                        goto out;
                } else {
                        ret = -1;
                        gf_event (EVENT_POSIX_BRICK_VERIFICATION_FAILED,
                                "brick=%s:%s",
                                _private->hostname, _private->base_path);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_VOLUME_ID_FETCH_FAILED,
                                "failed to fetch proper volume id from export");
                        goto out;
                }
        }

        /* Now check if the export directory has some other 'gfid',
           other than that of root '/' */
        size = sys_lgetxattr (dir_data->data, "trusted.gfid", gfid, 16);
        if (size == 16) {
                if (!__is_root_gfid (gfid)) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_GFID_SET_FAILED,
                                "%s: gfid (%s) is not that of glusterfs '/' ",
                                dir_data->data, uuid_utoa (gfid));
                        ret = -1;
                        goto out;
                }
        } else if (size != -1) {
                /* Wrong 'gfid' is set, it should be error */
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        P_MSG_GFID_SET_FAILED,
                        "%s: wrong value set as gfid",
                        dir_data->data);
                ret = -1;
                goto out;
        } else if ((size == -1) && (errno != ENODATA) &&
                   (errno != ENOATTR)) {
                /* Wrong 'gfid' is set, it should be error */
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        P_MSG_GFID_SET_FAILED,
                        "%s: failed to fetch gfid",
                        dir_data->data);
                ret = -1;
                goto out;
        } else {
                /* First time volume, set the GFID */
                size = sys_lsetxattr (dir_data->data, "trusted.gfid", rootgfid,
                                     16, XATTR_CREATE);
                if (size == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_GFID_SET_FAILED,
                                "%s: failed to set gfid",
                                dir_data->data);
                        ret = -1;
                        goto out;
                }
        }

        ret = 0;

        size = sys_lgetxattr (dir_data->data, POSIX_ACL_ACCESS_XATTR,
                              NULL, 0);
        if ((size < 0) && (errno == ENOTSUP)) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        P_MSG_ACL_NOTSUP,
                        "Posix access control list is not supported.");
                gf_event (EVENT_POSIX_ACL_NOT_SUPPORTED,
                        "brick=%s:%s", _private->hostname, _private->base_path);
        }

        /*
         * _XOPEN_PATH_MAX is the longest file path len we MUST
         * support according to POSIX standard. When prepended
         * by the brick base path it may exceed backed filesystem
         * capacity (which MAY be bigger than _XOPEN_PATH_MAX). If
         * this is the case, chdir() to the brick base path and
         * use relative paths when they are too long. See also
         * MAKE_REAL_PATH in posix-handle.h
          */
        _private->path_max = pathconf(_private->base_path, _PC_PATH_MAX);
        if (_private->path_max != -1 &&
            _XOPEN_PATH_MAX + _private->base_path_length > _private->path_max) {
                ret = chdir(_private->base_path);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_BASEPATH_CHDIR_FAILED,
                                "chdir() to \"%s\" failed",
                                _private->base_path);
                        goto out;
                }
#ifdef __NetBSD__
                /*
                 * At least on NetBSD, the chdir() above uncovers a
                 * race condition which cause file lookup to fail
                 * with ENODATA for a few seconds. The volume quickly
                 * reaches a sane state, but regression tests are fast
                 * enough to choke on it. The reason is obscure (as
                 * often with race conditions), but sleeping here for
                 * a second seems to workaround the problem.
                 */
                sleep(1);
#endif
        }


        LOCK_INIT (&_private->lock);

        _private->export_statfs = 1;
        tmp_data = dict_get (this->options, "export-statfs-size");
        if (tmp_data) {
                if (gf_string2boolean (tmp_data->data,
                                       &_private->export_statfs) == -1) {
                        ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_INVALID_OPTION_VAL,
                                "'export-statfs-size' takes only boolean "
                                "options");
                        goto out;
                }
                if (!_private->export_statfs)
                        gf_msg_debug (this->name, 0,
                                "'statfs()' returns dummy size");
        }

        _private->background_unlink = 0;
        tmp_data = dict_get (this->options, "background-unlink");
        if (tmp_data) {
                if (gf_string2boolean (tmp_data->data,
                                       &_private->background_unlink) == -1) {
                        ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_INVALID_OPTION_VAL, "'background-unlink'"
                                " takes only boolean options");
                        goto out;
                }

                if (_private->background_unlink)
                        gf_msg_debug (this->name, 0,
                                "unlinks will be performed in background");
        }

        tmp_data = dict_get (this->options, "o-direct");
        if (tmp_data) {
                if (gf_string2boolean (tmp_data->data,
                                       &_private->o_direct) == -1) {
                        ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_INVALID_OPTION_VAL,
                                "wrong option provided for 'o-direct'");
                        goto out;
                }
                if (_private->o_direct)
                        gf_msg_debug (this->name, 0, "o-direct mode is enabled"
                                      " (O_DIRECT for every open)");
        }

        tmp_data = dict_get (this->options, "update-link-count-parent");
        if (tmp_data) {
                if (gf_string2boolean (tmp_data->data,
                                       &_private->update_pgfid_nlinks) == -1) {
                        ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                P_MSG_INVALID_OPTION, "wrong value provided "
                                "for 'update-link-count-parent'");
                        goto out;
                }
                if (_private->update_pgfid_nlinks)
                        gf_msg_debug (this->name, 0, "update-link-count-parent"
                                      " is enabled. Thus for each file an "
                                      "extended attribute representing the "
                                      "number of hardlinks for that file "
                                      "within the same parent directory is"
                                      " set.");
        }

        ret = dict_get_str (this->options, "glusterd-uuid", &guuid);
        if (!ret) {
                if (gf_uuid_parse (guuid, _private->glusterd_uuid))
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                P_MSG_INVALID_NODE_UUID, "Cannot parse "
                                "glusterd (node) UUID, node-uuid xattr "
                                "request would return - \"No such attribute\"");
        } else {
                gf_msg_debug (this->name, 0, "No glusterd (node) UUID passed -"
                              " node-uuid xattr request will return \"No such"
                              " attribute\"");
        }
        ret = 0;

        GF_OPTION_INIT ("janitor-sleep-duration",
                        _private->janitor_sleep_duration, int32, out);

        /* performing open dir on brick dir locks the brick dir
         * and prevents it from being unmounted
         */
        _private->mount_lock = sys_opendir (dir_data->data);
        if (!_private->mount_lock) {
                ret = -1;
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        P_MSG_DIR_OPERATION_FAILED,
                        "Could not lock brick directory (%s)",
                        strerror (op_errno));
                goto out;
        }
#ifndef GF_DARWIN_HOST_OS
        {
                struct rlimit lim;
                lim.rlim_cur = 1048576;
                lim.rlim_max = 1048576;

                if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_SET_ULIMIT_FAILED,
                                "Failed to set 'ulimit -n "
                                " 1048576'");
                        lim.rlim_cur = 65536;
                        lim.rlim_max = 65536;

                        if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
                                gf_msg (this->name, GF_LOG_WARNING, errno,
                                        P_MSG_SET_FILE_MAX_FAILED,
                                        "Failed to set maximum allowed open "
                                        "file descriptors to 64k");
                        }
                        else {
                                gf_msg (this->name, GF_LOG_INFO, 0,
                                        P_MSG_MAX_FILE_OPEN, "Maximum allowed "
                                        "open file descriptors set to 65536");
                        }
                }
        }
#endif
        this->private = (void *)_private;

        op_ret = posix_handle_init (this);
        if (op_ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_HANDLE_CREATE,
                        "Posix handle setup failed");
                ret = -1;
                goto out;
        }

        op_ret = posix_handle_trash_init (this);
        if (op_ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_HANDLE_CREATE_TRASH,
                        "Posix landfill setup failed");
                ret = -1;
                goto out;
	}

        op_ret = posix_create_unlink_dir (this);
        if (op_ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        P_MSG_HANDLE_CREATE,
                        "Creation of unlink directory failed");
                ret = -1;
                goto out;
        }

	_private->aio_init_done = _gf_false;
	_private->aio_capable = _gf_false;

        GF_OPTION_INIT ("brick-uid", uid, int32, out);
        GF_OPTION_INIT ("brick-gid", gid, int32, out);
	if (uid != -1 || gid != -1)
		posix_set_owner (this, uid, gid);

	GF_OPTION_INIT ("linux-aio", _private->aio_configured, bool, out);

	if (_private->aio_configured) {
		op_ret = posix_aio_on (this);

		if (op_ret == -1) {
			gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_POSIX_AIO,
				"Posix AIO init failed");
			ret = -1;
			goto out;
		}
	}

        GF_OPTION_INIT ("node-uuid-pathinfo",
                        _private->node_uuid_pathinfo, bool, out);
        if (_private->node_uuid_pathinfo &&
            (gf_uuid_is_null (_private->glusterd_uuid))) {
                        gf_msg (this->name, GF_LOG_INFO, 0, P_MSG_UUID_NULL,
                                "glusterd uuid is NULL, pathinfo xattr would"
                                " fallback to <hostname>:<export>");
        }

        _private->health_check_active = _gf_false;
        GF_OPTION_INIT ("health-check-interval",
                        _private->health_check_interval, uint32, out);
        if (_private->health_check_interval)
                posix_spawn_health_check_thread (this);

        pthread_mutex_init (&_private->janitor_lock, NULL);
        pthread_cond_init (&_private->janitor_cond, NULL);
        INIT_LIST_HEAD (&_private->janitor_fds);

        posix_spawn_janitor_thread (this);

	pthread_mutex_init (&_private->fsync_mutex, NULL);
	pthread_cond_init (&_private->fsync_cond, NULL);
	INIT_LIST_HEAD (&_private->fsyncs);

	ret = gf_thread_create (&_private->fsyncer, NULL, posix_fsyncer, this);
	if (ret) {
		gf_msg (this->name, GF_LOG_ERROR, errno,
                        P_MSG_FSYNCER_THREAD_CREATE_FAILED,
                        "fsyncer thread creation failed");
		goto out;
	}

	GF_OPTION_INIT ("batch-fsync-mode", batch_fsync_mode_str, str, out);

	if (set_batch_fsync_mode (_private, batch_fsync_mode_str) != 0) {
		gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_INVALID_ARGUMENT,
                        "Unknown mode string: %s", batch_fsync_mode_str);
		goto out;
	}

#ifdef GF_DARWIN_HOST_OS

        char  *xattr_user_namespace_mode_str = NULL;

        GF_OPTION_INIT ("xattr-user-namespace-mode",
                        xattr_user_namespace_mode_str, str, out);

        if (set_xattr_user_namespace_mode (_private,
                                           xattr_user_namespace_mode_str) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_INVALID_ARGUMENT,
                        "Unknown xattr user namespace mode string: %s",
                        xattr_user_namespace_mode_str);
                goto out;
        }
#endif

        GF_OPTION_INIT ("batch-fsync-delay-usec", _private->batch_fsync_delay_usec,
                        uint32, out);
out:
        return ret;
}

void
fini (xlator_t *this)
{
        struct posix_private *priv = this->private;
        if (!priv)
                return;
        this->private = NULL;
        /*unlock brick dir*/
        if (priv->mount_lock)
                (void) sys_closedir (priv->mount_lock);
        GF_FREE (priv);
        return;
}
struct xlator_dumpops dumpops = {
        .priv    = posix_priv,
        .inode   = posix_inode,
};

struct xlator_fops fops = {
        .lookup      = posix_lookup,
        .stat        = posix_stat,
        .opendir     = posix_opendir,
        .readdir     = posix_readdir,
        .readdirp    = posix_readdirp,
        .readlink    = posix_readlink,
        .mknod       = posix_mknod,
        .mkdir       = posix_mkdir,
        .unlink      = posix_unlink,
        .rmdir       = posix_rmdir,
        .symlink     = posix_symlink,
        .rename      = posix_rename,
        .link        = posix_link,
        .truncate    = posix_truncate,
        .create      = posix_create,
        .open        = posix_open,
        .readv       = posix_readv,
        .writev      = posix_writev,
        .statfs      = posix_statfs,
        .flush       = posix_flush,
        .fsync       = posix_fsync,
        .setxattr    = posix_setxattr,
        .fsetxattr   = posix_fsetxattr,
        .getxattr    = posix_getxattr,
        .fgetxattr   = posix_fgetxattr,
        .removexattr = posix_removexattr,
        .fremovexattr = posix_fremovexattr,
        .fsyncdir    = posix_fsyncdir,
        .access      = posix_access,
        .ftruncate   = posix_ftruncate,
        .fstat       = posix_fstat,
        .lk          = posix_lk,
        .inodelk     = posix_inodelk,
        .finodelk    = posix_finodelk,
        .entrylk     = posix_entrylk,
        .fentrylk    = posix_fentrylk,
        .rchecksum   = posix_rchecksum,
        .xattrop     = posix_xattrop,
        .fxattrop    = posix_fxattrop,
        .setattr     = posix_setattr,
        .fsetattr    = posix_fsetattr,
	.fallocate   = _posix_fallocate,
	.discard     = posix_discard,
        .zerofill    = posix_zerofill,
        .ipc         = posix_ipc,
#ifdef HAVE_SEEK_HOLE
        .seek        = posix_seek,
#endif
        .lease       = posix_lease,
};

struct xlator_cbks cbks = {
        .release     = posix_release,
        .releasedir  = posix_releasedir,
        .forget      = posix_forget
};

struct volume_options options[] = {
        { .key  = {"o-direct"},
          .type = GF_OPTION_TYPE_BOOL },
        { .key  = {"directory"},
          .type = GF_OPTION_TYPE_PATH },
        { .key  = {"hostname"},
          .type = GF_OPTION_TYPE_ANY },
        { .key  = {"export-statfs-size"},
          .type = GF_OPTION_TYPE_BOOL },
        { .key  = {"mandate-attribute"},
          .type = GF_OPTION_TYPE_BOOL },
        { .key  = {"background-unlink"},
          .type = GF_OPTION_TYPE_BOOL },
        { .key  = {"janitor-sleep-duration"},
          .type = GF_OPTION_TYPE_INT,
          .min = 1,
          .validate = GF_OPT_VALIDATE_MIN,
          .default_value = "10",
          .description = "Interval (in seconds) between times the internal "
                         "'landfill' directory is emptied."
        },
        { .key  = {"volume-id"},
          .type = GF_OPTION_TYPE_ANY },
        { .key  = {"glusterd-uuid"},
          .type = GF_OPTION_TYPE_STR },
	{
	  .key  = {"linux-aio"},
	  .type = GF_OPTION_TYPE_BOOL,
	  .default_value = "off",
          .description = "Support for native Linux AIO"
	},
        {
          .key = {"brick-uid"},
          .type = GF_OPTION_TYPE_INT,
          .min = -1,
          .validate = GF_OPT_VALIDATE_MIN,
	  .default_value = "-1",
          .description = "Support for setting uid of brick's owner"
        },
        {
          .key = {"brick-gid"},
          .type = GF_OPTION_TYPE_INT,
          .min = -1,
          .validate = GF_OPT_VALIDATE_MIN,
	  .default_value = "-1",
          .description = "Support for setting gid of brick's owner"
        },
        { .key = {"node-uuid-pathinfo"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "return glusterd's node-uuid in pathinfo xattr"
                         " string instead of hostname"
        },
        {
          .key = {"health-check-interval"},
          .type = GF_OPTION_TYPE_INT,
          .min = 0,
          .default_value = "30",
          .validate = GF_OPT_VALIDATE_MIN,
          .description = "Interval in seconds for a filesystem health check, "
                         "set to 0 to disable"
        },
	{ .key = {"batch-fsync-mode"},
	  .type = GF_OPTION_TYPE_STR,
	  .default_value = "reverse-fsync",
	  .description = "Possible values:\n"
	  "\t- syncfs: Perform one syncfs() on behalf oa batch"
	  "of fsyncs.\n"
	  "\t- syncfs-single-fsync: Perform one syncfs() on behalf of a batch"
	  " of fsyncs and one fsync() per batch.\n"
	  "\t- syncfs-reverse-fsync: Preform one syncfs() on behalf of a batch"
	  " of fsyncs and fsync() each file in the batch in reverse order.\n"
	  " in reverse order.\n"
	  "\t- reverse-fsync: Perform fsync() of each file in the batch in"
	  " reverse order."
	},
	{ .key = {"batch-fsync-delay-usec"},
	  .type = GF_OPTION_TYPE_INT,
	  .default_value = "0",
	  .description = "Num of usecs to wait for aggregating fsync"
	  " requests",
	},
        { .key = {"update-link-count-parent"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "Enable placeholders for gfid to path conversion"
        },
#if GF_DARWIN_HOST_OS
        { .key = {"xattr-user-namespace-mode"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = "none",
          .description = "Option to control XATTR user namespace on the raw filesystem: "
	  "\t- None: Will use the user namespace, so files will be exchangable with Linux.\n"
	  " The raw filesystem will not be compatible with OS X Finder.\n"
	  "\t- Strip: Will strip the user namespace before setting. The raw filesystem will work in OS X.\n"
        },
#endif
        { .key  = {NULL} }
};
