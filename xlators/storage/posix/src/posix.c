/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#define __XOPEN_SOURCE 500

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
int
posix_forget (xlator_t *this, inode_t *inode)
{
        uint64_t tmp_cache = 0;
        if (!inode_ctx_del (inode, this, &tmp_cache))
                dict_destroy ((dict_t *)(long)tmp_cache);

        return 0;
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
        struct  posix_private *priv    = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;

        /* The Hidden directory should be for housekeeping purpose and it
           should not get any gfid on it */
        if (__is_root_gfid (loc->pargfid) && loc->name
            && (strcmp (loc->name, GF_HIDDEN_PATH) == 0)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Lookup issued on %s, which is not permitted",
                        GF_HIDDEN_PATH);
                op_errno = EPERM;
                op_ret = -1;
                goto out;
        }

        op_ret = dict_get_int32 (xdata, GF_GFIDLESS_LOOKUP, &gfidless);
        op_ret = -1;
        if (uuid_is_null (loc->pargfid) || (loc->name == NULL)) {
                /* nameless lookup */
                MAKE_INODE_HANDLE (real_path, this, loc, &buf);
        } else {
                MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, &buf);

                if (uuid_is_null (loc->inode->gfid)) {
                        posix_gfid_heal (this, real_path, loc, xdata);
                        MAKE_ENTRY_HANDLE (real_path, par_path, this,
                                           loc, &buf);
                }
        }

        op_errno = errno;

        if (op_ret == -1) {
                if (op_errno != ENOENT) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "lstat on %s failed: %s",
                                real_path, strerror (op_errno));
                }

                entry_ret = -1;
                goto parent;
        }

        if (xdata && (op_ret == 0)) {
                xattr = posix_lookup_xattr_fill (this, real_path, loc,
                                                 xdata, &buf);
        }

parent:
        if (par_path) {
                op_ret = posix_pstat (this, loc->pargfid, par_path, &postparent);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "post-operation lstat on parent %s failed: %s",
                                par_path, strerror (op_errno));
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
        if (xattr)
                dict_ref (xattr);

        if (!op_ret && !gfidless && uuid_is_null (buf.ia_gfid)) {
                gf_log (this->name, GF_LOG_ERROR, "buf->ia_gfid is null for "
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
                gf_log (this->name, (op_errno == ENOENT)?
                        GF_LOG_DEBUG:GF_LOG_ERROR,
                        "lstat on %s failed: %s", real_path,
                        strerror (op_errno));
                goto out;
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID();
        STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, &buf, NULL);

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
                gf_log (this->name, GF_LOG_WARNING,
                        "lstat failed: %s (%s)", path, strerror (errno));
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
                gf_log (this->name, GF_LOG_DEBUG,
                        "%s (%s)", path, strerror (errno));
                if (is_symlink) {
                        ret = 0;
                        goto out;
                }

                ret = chmod (path, mode);
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

        ret = lchown (path, uid, gid);

        return ret;
}

static int
posix_do_utimes (xlator_t *this,
                 const char *path,
                 struct iatt *stbuf)
{
        int32_t ret = -1;
        struct timeval tv[2]     = {{0,},{0,}};
        struct stat stat;
        int    is_symlink = 0;

        ret = sys_lstat (path, &stat);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s (%s)", path, strerror (errno));
                goto out;
        }

        if (S_ISLNK (stat.st_mode))
                is_symlink = 1;

        tv[0].tv_sec  = stbuf->ia_atime;
        tv[0].tv_usec = stbuf->ia_atime_nsec / 1000;
        tv[1].tv_sec  = stbuf->ia_mtime;
        tv[1].tv_usec = stbuf->ia_mtime_nsec / 1000;

        ret = lutimes (path, tv);
        if ((ret == -1) && (errno == ENOSYS)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%s (%s)", path, strerror (errno));
                if (is_symlink) {
                        ret = 0;
                        goto out;
                }

                ret = utimes (path, tv);
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

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_INODE_HANDLE (real_path, this, loc, &statpre);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "setattr (lstat) on %s failed: %s", real_path,
                        strerror (op_errno));
                goto out;
        }

        if (valid & GF_SET_ATTR_MODE) {
                op_ret = posix_do_chmod (this, real_path, stbuf);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "setattr (chmod) on %s failed: %s", real_path,
                                strerror (op_errno));
                        goto out;
                }
        }

        if (valid & (GF_SET_ATTR_UID | GF_SET_ATTR_GID)){
                op_ret = posix_do_chown (this, real_path, stbuf, valid);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "setattr (chown) on %s failed: %s", real_path,
                                strerror (op_errno));
                        goto out;
                }
        }

        if (valid & (GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME)) {
                op_ret = posix_do_utimes (this, real_path, stbuf);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "setattr (utimes) on %s failed: %s", real_path,
                                strerror (op_errno));
                        goto out;
                }
        }

        if (!valid) {
                op_ret = lchown (real_path, -1, -1);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "lchown (%s, -1, -1) failed => (%s)",
                                real_path, strerror (op_errno));

                        goto out;
                }
        }

        op_ret = posix_pstat (this, loc->gfid, real_path, &statpost);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "setattr (lstat) on %s failed: %s", real_path,
                        strerror (op_errno));
                goto out;
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno,
                             &statpre, &statpost, NULL);

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

        ret = fchown (fd, uid, gid);

        return ret;
}


int32_t
posix_do_fchmod (xlator_t *this,
                 int fd, struct iatt *stbuf)
{
        mode_t  mode = 0;

        mode = st_mode_from_ia (stbuf->ia_prot, stbuf->ia_type);
        return fchmod (fd, mode);
}

static int
posix_do_futimes (xlator_t *this,
                  int fd,
                  struct iatt *stbuf)
{
        gf_log (this->name, GF_LOG_WARNING, "function not implemented fd(%d)", fd);

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
        int32_t          ret = -1;

        DECLARE_OLD_FS_ID_VAR;

        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = posix_fd_ctx_get (fd, this, &pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd is NULL from fd=%p", fd);
                goto out;
        }

        op_ret = posix_fdstat (this, pfd->fd, &statpre);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "fsetattr (fstat) failed on fd=%p: %s", fd,
                        strerror (op_errno));
                goto out;
        }

        if (valid & GF_SET_ATTR_MODE) {
                op_ret = posix_do_fchmod (this, pfd->fd, stbuf);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "fsetattr (fchmod) failed on fd=%p: %s",
                                fd, strerror (op_errno));
                        goto out;
                }
        }

        if (valid & (GF_SET_ATTR_UID | GF_SET_ATTR_GID)) {
                op_ret = posix_do_fchown (this, pfd->fd, stbuf, valid);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "fsetattr (fchown) failed on fd=%p: %s",
                                fd, strerror (op_errno));
                        goto out;
                }

        }

        if (valid & (GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME)) {
                op_ret = posix_do_futimes (this, pfd->fd, stbuf);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "fsetattr (futimes) on failed fd=%p: %s", fd,
                                strerror (op_errno));
                        goto out;
                }
        }

        if (!valid) {
                op_ret = fchown (pfd->fd, -1, -1);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "fchown (%d, -1, -1) failed => (%s)",
                                pfd->fd, strerror (op_errno));

                        goto out;
                }
        }

        op_ret = posix_fdstat (this, pfd->fd, &statpost);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "fsetattr (fstat) failed on fd=%p: %s", fd,
                        strerror (op_errno));
                goto out;
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (fsetattr, frame, op_ret, op_errno,
                             &statpre, &statpost, NULL);

        return 0;
}

#ifdef FALLOC_FL_KEEP_SIZE
static int32_t
posix_do_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
		   off_t offset, size_t len, struct iatt *statpre,
		   struct iatt *statpost)
{
        struct posix_fd *pfd = NULL;
        int32_t          ret = -1;

        DECLARE_OLD_FS_ID_VAR;

        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = posix_fd_ctx_get (fd, this, &pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd is NULL from fd=%p", fd);
                goto out;
        }

        ret = posix_fdstat (this, pfd->fd, statpre);
        if (ret == -1) {
                ret = -errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "fallocate (fstat) failed on fd=%p: %s", fd,
                        strerror (errno));
                goto out;
        }

	ret = sys_fallocate(pfd->fd, flags, offset, len);
	if (ret == -1) {
		ret = -errno;
		goto out;
	}

        ret = posix_fdstat (this, pfd->fd, statpost);
        if (ret == -1) {
                ret = -errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "fallocate (fstat) failed on fd=%p: %s", fd,
                        strerror (errno));
                goto out;
        }

out:
        SET_TO_OLD_FS_ID ();

        return ret;
}
#endif /* FALLOC_FL_KEEP_SIZE */

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
                        gf_log ("_posix_do_zerofill", GF_LOG_DEBUG,
                                 "memory alloc failed, vect_size %d: %s",
                                  vect_size, strerror(errno));
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
        if (lseek(fd, offset, SEEK_SET) < 0) {
                op_ret = -1;
                goto err;
        }

        for (idx = 0; idx < num_loop; idx++) {
                op_ret = writev(fd, vector, num_vect);
                if (op_ret < 0)
                        goto err;
        }
        if (extra) {
                op_ret = writev(fd, vector, extra);
                if (op_ret < 0)
                        goto err;
        }
        if (remain) {
                vector[0].iov_len = remain;
                op_ret = writev(fd, vector , 1);
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
posix_do_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd,
                  off_t offset, off_t len, struct iatt *statpre,
                  struct iatt *statpost)
{
        struct posix_fd *pfd       = NULL;
        int32_t          ret       = -1;

        DECLARE_OLD_FS_ID_VAR;

        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = posix_fd_ctx_get (fd, this, &pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd is NULL from fd=%p", fd);
                goto out;
        }

        ret = posix_fdstat (this, pfd->fd, statpre);
        if (ret == -1) {
                ret = -errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation fstat failed on fd = %p: %s", fd,
                        strerror (errno));
                goto out;
        }
        ret = _posix_do_zerofill(pfd->fd, offset, len, pfd->flags & O_DIRECT);
        if (ret < 0) {
                ret = -errno;
                gf_log(this->name, GF_LOG_ERROR,
                       "zerofill failed on fd %d length %ld %s",
                        pfd->fd, len, strerror(errno));
                goto out;
        }
        if (pfd->flags & (O_SYNC|O_DSYNC)) {
                ret = fsync (pfd->fd);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "fsync() in writev on fd %d failed: %s",
                        pfd->fd, strerror (errno));
                        ret = -errno;
                        goto out;
                }
        }

        ret = posix_fdstat (this, pfd->fd, statpost);
        if (ret == -1) {
                ret = -errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post operation fstat failed on fd=%p: %s", fd,
                        strerror (errno));
                goto out;
        }

out:
        SET_TO_OLD_FS_ID ();

        return ret;
}

static int32_t
_posix_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t keep_size,
		off_t offset, size_t len, dict_t *xdata)
{
	int32_t ret;
#ifndef FALLOC_FL_KEEP_SIZE
	ret = EOPNOTSUPP;

#else /* FALLOC_FL_KEEP_SIZE */
	int32_t flags = 0;
        struct iatt statpre = {0,};
        struct iatt statpost = {0,};

	if (keep_size)
		flags = FALLOC_FL_KEEP_SIZE;

	ret = posix_do_fallocate(frame, this, fd, flags, offset, len,
				 &statpre, &statpost);
	if (ret < 0)
		goto err;

	STACK_UNWIND_STRICT(fallocate, frame, 0, 0, &statpre, &statpost, NULL);
	return 0;

err:
#endif /* FALLOC_FL_KEEP_SIZE */
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

	ret = posix_do_fallocate(frame, this, fd, flags, offset, len,
				 &statpre, &statpost);
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

        ret = posix_do_zerofill(frame, this, fd, offset, len,
                                 &statpre, &statpost);
        if (ret < 0)
                goto err;

        STACK_UNWIND_STRICT(zerofill, frame, 0, 0, &statpre, &statpost, NULL);
        return 0;

err:
        STACK_UNWIND_STRICT(zerofill, frame, -1, -ret, NULL, NULL, NULL);
        return 0;

}

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

        op_ret = -1;
        dir = opendir (real_path);

        if (dir == NULL) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "opendir failed on %s: %s",
                        real_path, strerror (op_errno));
                goto out;
        }

        op_ret = dirfd (dir);
        if (op_ret < 0) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "dirfd() failed on %s: %s",
                        real_path, strerror (op_errno));
                goto out;
        }

        pfd = GF_CALLOC (1, sizeof (*pfd), gf_posix_mt_posix_fd);
        if (!pfd) {
                op_errno = errno;
                goto out;
        }

        pfd->dir = dir;
        pfd->fd = dirfd (dir);

        op_ret = fd_ctx_set (fd, this, (uint64_t)(long)pfd);
        if (op_ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to set the fd context path=%s fd=%p",
                        real_path, fd);

        op_ret = 0;

out:
        if (op_ret == -1) {
                if (dir) {
                        closedir (dir);
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
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd from fd=%p is NULL", fd);
                goto out;
        }

        pfd = (struct posix_fd *)(long)tmp_pfd;
        if (!pfd->dir) {
                gf_log (this->name, GF_LOG_WARNING,
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
                gf_log (this->name, GF_LOG_ERROR,
                        "lstat on %s failed: %s", real_path,
                        strerror (op_errno));
                goto out;
        }

        op_ret = readlink (real_path, dest, size);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "readlink on %s failed: %s", real_path,
                        strerror (op_errno));
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
        char                  was_present     = 1;
        struct posix_private *priv            = NULL;
        gid_t                 gid             = 0;
        struct iatt           preparent       = {0,};
        struct iatt           postparent      = {0,};
        void *                uuid_req        = NULL;
        int32_t               nlink_samepgfid = 0;
        char                 *pgfid_xattr_key = NULL;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, NULL);

        gid = frame->root->gid;

        SET_FS_ID (frame->root->uid, gid);

        op_ret = posix_pstat (this, loc->pargfid, par_path, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation lstat on parent of %s failed: %s",
                        real_path, strerror (op_errno));
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
                        gf_log (this->name, GF_LOG_DEBUG,
                                "failed to get the gfid from dict for %s",
                                loc->path);
                        goto real_op;
                }
                op_ret = posix_create_link_if_gfid_exists (this, uuid_req,
                                                           real_path);
                if (!op_ret)
                        goto post_op;
        }

real_op:
#ifdef __NetBSD__
	if (S_ISFIFO(mode))
		op_ret = mkfifo (real_path, mode);
	else
#endif /* __NetBSD__ */
        op_ret = mknod (real_path, mode, dev);

        if (op_ret == -1) {
                op_errno = errno;
                if ((op_errno == EINVAL) && S_ISREG (mode)) {
                        /* Over Darwin, mknod with (S_IFREG|mode)
                           doesn't work */
                        tmp_fd = creat (real_path, mode);
                        if (tmp_fd == -1) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "create failed on %s: %s",
                                        real_path, strerror (errno));
                                goto out;
                        }
                        close (tmp_fd);
                } else {

                        gf_log (this->name, GF_LOG_ERROR,
                                "mknod on %s failed: %s", real_path,
                                strerror (op_errno));
                        goto out;
                }
        }

        op_ret = posix_gfid_set (this, real_path, loc, xdata);
        if (op_ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "setting gfid on %s failed", real_path);
        }

#ifndef HAVE_SET_FSID
        op_ret = lchown (real_path, frame->root->uid, gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "lchown on %s failed: %s", real_path,
                        strerror (op_errno));
                goto out;
        }
#endif

post_op:
        op_ret = posix_acl_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "setting ACLs on %s failed (%s)", real_path,
                        strerror (errno));
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
                gf_log (this->name, GF_LOG_ERROR,
                        "setting xattrs on %s failed (%s)", real_path,
                        strerror (errno));
        }

        op_ret = posix_pstat (this, NULL, real_path, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "mknod on %s failed: %s", real_path,
                        strerror (op_errno));
                goto out;
        }

        op_ret = posix_pstat (this, loc->pargfid, par_path, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post-operation lstat on parent %s failed: %s",
                        par_path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno,
                             (loc)?loc->inode:NULL, &stbuf, &preparent,
                             &postparent, NULL);

        if ((op_ret == -1) && (!was_present)) {
                unlink (real_path);
        }

        return 0;
}


int
posix_mkdir (call_frame_t *frame, xlator_t *this,
             loc_t *loc, mode_t mode, mode_t umask, dict_t *xdata)
{
        int32_t               op_ret      = -1;
        int32_t               op_errno    = 0;
        char                 *real_path   = NULL;
        char                 *par_path   = NULL;
        struct iatt           stbuf       = {0, };
        char                  was_present = 1;
        struct posix_private *priv        = NULL;
        gid_t                 gid         = 0;
        struct iatt           preparent = {0,};
        struct iatt           postparent = {0,};

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        /* The Hidden directory should be for housekeeping purpose and it
           should not get created from a user request */
        if (__is_root_gfid (loc->pargfid) &&
            (strcmp (loc->name, GF_HIDDEN_PATH) == 0)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "mkdir issued on %s, which is not permitted",
                        GF_HIDDEN_PATH);
                op_errno = EPERM;
                op_ret = -1;
                goto out;
        }

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, NULL);

        gid = frame->root->gid;

        op_ret = posix_pstat (this, NULL, real_path, &stbuf);
        if ((op_ret == -1) && (errno == ENOENT)) {
                was_present = 0;
        }

        SET_FS_ID (frame->root->uid, gid);

        op_ret = posix_pstat (this, loc->pargfid, par_path, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation lstat on parent %s failed: %s",
                        par_path, strerror (op_errno));
                goto out;
        }

        if (preparent.ia_prot.sgid) {
                gid = preparent.ia_gid;
                mode |= S_ISGID;
        }

        op_ret = mkdir (real_path, mode);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "mkdir of %s failed: %s", real_path,
                        strerror (op_errno));
                goto out;
        }

        op_ret = posix_gfid_set (this, real_path, loc, xdata);
        if (op_ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "setting gfid on %s failed", real_path);
        }

#ifndef HAVE_SET_FSID
        op_ret = chown (real_path, frame->root->uid, gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "chown on %s failed: %s", real_path,
                        strerror (op_errno));
                goto out;
        }
#endif
        op_ret = posix_acl_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "setting ACLs on %s failed (%s)", real_path,
                        strerror (errno));
        }

        op_ret = posix_entry_create_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "setting xattrs on %s failed (%s)", real_path,
                        strerror (errno));
        }

        op_ret = posix_pstat (this, NULL, real_path, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "lstat on %s failed: %s", real_path,
                        strerror (op_errno));
                goto out;
        }

        op_ret = posix_pstat (this, loc->pargfid, par_path, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post-operation lstat on parent of %s failed: %s",
                        real_path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno,
                             (loc)?loc->inode:NULL, &stbuf, &preparent,
                             &postparent, NULL);

        if ((op_ret == -1) && (!was_present)) {
                unlink (real_path);
        }

        return 0;
}


int32_t
posix_unlink (call_frame_t *frame, xlator_t *this,
              loc_t *loc, int xflag, dict_t *xdata)
{
        int32_t               op_ret          = -1;
        int32_t               op_errno        = 0;
        char                 *real_path       = NULL;
        char                 *par_path        = NULL;
        int32_t               fd              = -1;
        struct iatt           stbuf           = {0,};
        struct posix_private *priv            = NULL;
        struct iatt           preparent       = {0,};
        struct iatt           postparent      = {0,};
        char                 *pgfid_xattr_key = NULL;
        int32_t               nlink_samepgfid = 0;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, &stbuf);

        op_ret = posix_pstat (this, loc->pargfid, par_path, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation lstat on parent %s failed: %s",
                        par_path, strerror (op_errno));
                goto out;
        }

        if (stbuf.ia_nlink == 1)
                posix_handle_unset (this, stbuf.ia_gfid, NULL);

        priv = this->private;
        if (priv->background_unlink) {
                if (IA_ISREG (loc->inode->ia_type)) {
                        fd = open (real_path, O_RDONLY);
                        if (fd == -1) {
                                op_ret = -1;
                                op_errno = errno;
                                gf_log (this->name, GF_LOG_ERROR,
                                        "open of %s failed: %s", real_path,
                                        strerror (op_errno));
                                goto out;
                        }
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
                        gf_log (this->name, GF_LOG_WARNING, "modification of "
                                "parent gfid xattr failed (path:%s gfid:%s)",
                                real_path, uuid_utoa (loc->inode->gfid));
                        goto out;
                }
        }

        op_ret = sys_unlink (real_path);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "unlink of %s failed: %s", real_path,
                        strerror (op_errno));
                goto out;
        }

        op_ret = posix_pstat (this, loc->pargfid, par_path, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post-operation lstat on parent %s failed: %s",
                        par_path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno,
                             &preparent, &postparent, NULL);

        if (fd != -1) {
                close (fd);
        }

        return 0;
}


int
posix_rmdir (call_frame_t *frame, xlator_t *this,
             loc_t *loc, int flags, dict_t *xdata)
{
        int32_t op_ret    = -1;
        int32_t op_errno  = 0;
        char *  real_path = NULL;
        char *  par_path = NULL;
        char *  gfid_str = NULL;
        struct iatt   preparent = {0,};
        struct iatt   postparent = {0,};
        struct iatt   stbuf;
        struct posix_private    *priv      = NULL;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);

        /* The Hidden directory should be for housekeeping purpose and it
           should not get deleted from inside process */
        if (__is_root_gfid (loc->pargfid) &&
            (strcmp (loc->name, GF_HIDDEN_PATH) == 0)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "rmdir issued on %s, which is not permitted",
                        GF_HIDDEN_PATH);
                op_errno = EPERM;
                op_ret = -1;
                goto out;
        }

        priv = this->private;

        MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, &stbuf);

        op_ret = posix_pstat (this, loc->pargfid, par_path, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation lstat on parent %s failed: %s",
                        par_path, strerror (op_errno));
                goto out;
        }

        if (flags) {
                gfid_str = uuid_utoa (stbuf.ia_gfid);
                char *tmp_path = alloca (strlen (priv->trash_path) +
                                         strlen ("/") +
                                         strlen (gfid_str) + 1);

                mkdir (priv->trash_path, 0755);
                sprintf (tmp_path, "%s/%s", priv->trash_path, gfid_str);
                op_ret = rename (real_path, tmp_path);
        } else {
                op_ret = rmdir (real_path);
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
                gf_log (this->name, GF_LOG_ERROR,
                        "rmdir of %s failed: %s", real_path,
                        strerror (op_errno));
        }

        if (op_ret == -1) {
                gf_log (this->name,
                        (op_errno == ENOTEMPTY) ? GF_LOG_DEBUG : GF_LOG_ERROR,
                        "%s on %s failed", (flags) ? "rename" : "rmdir",
                        real_path);
                goto out;
        }

        op_ret = posix_pstat (this, loc->pargfid, par_path, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post-operation lstat on parent of %s failed: %s",
                        par_path, strerror (op_errno));
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
        char                  was_present     = 1;
        struct iatt           preparent       = {0,};
        struct iatt           postparent      = {0,};
        char                 *pgfid_xattr_key = NULL;
        int32_t               nlink_samepgfid = 0;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (linkname, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, &stbuf);

        if ((op_ret == -1) && (errno == ENOENT)){
                was_present = 0;
        }

        SET_FS_ID (frame->root->uid, gid);

        gid = frame->root->gid;

        op_ret = posix_pstat (this, loc->pargfid, par_path, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation lstat on parent %s failed: %s",
                        par_path, strerror (op_errno));
                goto out;
        }

        if (preparent.ia_prot.sgid) {
                gid = preparent.ia_gid;
        }

        op_ret = symlink (linkname, real_path);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "symlink of %s --> %s failed: %s",
                        real_path, linkname, strerror (op_errno));
                goto out;
        }

        op_ret = posix_gfid_set (this, real_path, loc, xdata);
        if (op_ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "setting gfid on %s failed", real_path);
        }

#ifndef HAVE_SET_FSID
        op_ret = lchown (real_path, frame->root->uid, gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "lchown failed on %s: %s",
                        real_path, strerror (op_errno));
                goto out;
        }
#endif
        op_ret = posix_acl_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "setting ACLs on %s failed (%s)", real_path,
                        strerror (errno));
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
                gf_log (this->name, GF_LOG_ERROR,
                        "setting xattrs on %s failed (%s)", real_path,
                        strerror (errno));
        }

        op_ret = posix_pstat (this, NULL, real_path, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "lstat failed on %s: %s",
                        real_path, strerror (op_errno));
                goto out;
        }

        op_ret = posix_pstat (this, loc->pargfid, par_path, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post-operation lstat on parent %s failed: %s",
                        par_path, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno,
                             (loc)?loc->inode:NULL, &stbuf, &preparent,
                             &postparent, NULL);

        if ((op_ret == -1) && (!was_present)) {
                unlink (real_path);
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

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (oldloc, out);
        VALIDATE_OR_GOTO (newloc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_ENTRY_HANDLE (real_oldpath, par_oldpath, this, oldloc, NULL);
        MAKE_ENTRY_HANDLE (real_newpath, par_newpath, this, newloc, &stbuf);

        op_ret = posix_pstat (this, oldloc->pargfid, par_oldpath, &preoldparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation lstat on parent %s failed: %s",
                        par_oldpath, strerror (op_errno));
                goto out;
        }

        op_ret = posix_pstat (this, newloc->pargfid, par_newpath, &prenewparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation lstat on parent of %s failed: %s",
                        par_newpath, strerror (op_errno));
                goto out;
        }

        op_ret = posix_pstat (this, NULL, real_newpath, &stbuf);
        if ((op_ret == -1) && (errno == ENOENT)){
                was_present = 0;
        } else {
                uuid_copy (victim, stbuf.ia_gfid);
                if (IA_ISDIR (stbuf.ia_type))
                        was_dir = 1;
                nlink = stbuf.ia_nlink;
        }

        if (was_present && IA_ISDIR(stbuf.ia_type) && !newloc->inode) {
                gf_log (this->name, GF_LOG_WARNING,
                        "found directory at %s while expecting ENOENT",
                        real_newpath);
                op_ret = -1;
                op_errno = EEXIST;
                goto out;
        }

        if (was_present && IA_ISDIR(stbuf.ia_type) &&
            uuid_compare (newloc->inode->gfid, stbuf.ia_gfid)) {
                gf_log (this->name, GF_LOG_WARNING,
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

                op_ret = sys_rename (real_oldpath, real_newpath);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name,
                                (op_errno == ENOTEMPTY ? GF_LOG_DEBUG
                                 : GF_LOG_ERROR),
                                "rename of %s to %s failed: %s",
                                real_oldpath, real_newpath,
                                strerror (op_errno));

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
        UNLOCK (&oldloc->inode->lock);

        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_WARNING, "modification of "
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
                gf_log (this->name, GF_LOG_ERROR,
                        "lstat on %s failed: %s",
                        real_newpath, strerror (op_errno));
                goto out;
        }

        op_ret = posix_pstat (this, oldloc->pargfid, par_oldpath, &postoldparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post-operation lstat on parent %s failed: %s",
                        par_oldpath, strerror (op_errno));
                goto out;
        }

        op_ret = posix_pstat (this, newloc->pargfid, par_newpath, &postnewparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post-operation lstat on parent %s failed: %s",
                        par_newpath, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

out:

        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, &stbuf,
                             &preoldparent, &postoldparent,
                             &prenewparent, &postnewparent, NULL);

        if ((op_ret == -1) && !was_present) {
                unlink (real_newpath);
        }

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
        char                  was_present     = 1;
        struct iatt           preparent       = {0,};
        struct iatt           postparent      = {0,};
        int32_t               nlink_samepgfid = 0;
        char                 *pgfid_xattr_key = NULL;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (oldloc, out);
        VALIDATE_OR_GOTO (newloc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_INODE_HANDLE (real_oldpath, this, oldloc, &stbuf);

        MAKE_ENTRY_HANDLE (real_newpath, par_newpath, this, newloc, &stbuf);
        if ((op_ret == -1) && (errno == ENOENT)) {
                was_present = 0;
        }

        op_ret = posix_pstat (this, newloc->pargfid, par_newpath, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "lstat failed: %s: %s",
                        par_newpath, strerror (op_errno));
                goto out;
        }


        op_ret = sys_link (real_oldpath, real_newpath);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "link %s to %s failed: %s",
                        real_oldpath, real_newpath, strerror (op_errno));
                goto out;
        }

        op_ret = posix_pstat (this, NULL, real_newpath, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "lstat on %s failed: %s",
                        real_newpath, strerror (op_errno));
                goto out;
        }

        op_ret = posix_pstat (this, newloc->pargfid, par_newpath, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "lstat failed: %s: %s",
                        par_newpath, strerror (op_errno));
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
                        gf_log (this->name, GF_LOG_WARNING, "modification of "
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

        if ((op_ret == -1) && (!was_present)) {
                unlink (real_newpath);
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
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation lstat on %s failed: %s",
                        real_path, strerror (op_errno));
                goto out;
        }

        op_ret = truncate (real_path, offset);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "truncate on %s failed: %s",
                        real_path, strerror (op_errno));
                goto out;
        }

        op_ret = posix_pstat (this, loc->gfid, real_path, &postbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "lstat on %s failed: %s",
                        real_path, strerror (op_errno));
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

        op_ret = posix_pstat (this, loc->pargfid, par_path, &preparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation lstat on parent %s failed: %s",
                        par_path, strerror (op_errno));
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

        _fd = open (real_path, _flags, mode);

        if (_fd == -1) {
                op_errno = errno;
                op_ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "open on %s failed: %s", real_path,
                        strerror (op_errno));
                goto out;
        }

        if (was_present)
                goto fill_stat;

        op_ret = posix_gfid_set (this, real_path, loc, xdata);
        if (op_ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "setting gfid on %s failed", real_path);
        }

#ifndef HAVE_SET_FSID
        op_ret = chown (real_path, frame->root->uid, gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "chown on %s failed: %s",
                        real_path, strerror (op_errno));
        }
#endif
        op_ret = posix_acl_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "setting ACLs on %s failed (%s)", real_path,
                        strerror (errno));
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
                gf_log (this->name, GF_LOG_ERROR,
                        "setting xattrs on %s failed (%s)", real_path,
                        strerror (errno));
        }

fill_stat:
        op_ret = posix_fdstat (this, _fd, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "fstat on %d failed: %s", _fd, strerror (op_errno));
                goto out;
        }

        op_ret = posix_pstat (this, loc->pargfid, par_path, &postparent);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post-operation lstat on parent %s failed: %s",
                        par_path, strerror (op_errno));
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
                gf_log (this->name, GF_LOG_WARNING,
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
                close (_fd);

                if (!was_present) {
                        unlink (real_path);
                }
        }

        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno,
                             fd, (loc)?loc->inode:NULL, &stbuf, &preparent,
                             &postparent, xdata);

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

        op_ret = -1;
        SET_FS_ID (frame->root->uid, frame->root->gid);

        if (priv->o_direct)
                flags |= O_DIRECT;

        _fd = open (real_path, flags, 0);
        if (_fd == -1) {
                op_ret   = -1;
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "open on %s: %s", real_path, strerror (op_errno));
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
                gf_log (this->name, GF_LOG_WARNING,
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
                        close (_fd);
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

        ret = posix_fd_ctx_get (fd, this, &pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_WARNING,
                        "pfd is NULL from fd=%p", fd);
                goto out;
        }

        if (!size) {
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_WARNING, "size=%"GF_PRI_SIZET, size);
                goto out;
        }

        iobuf = iobuf_get2 (this->ctx->iobuf_pool, size);
        if (!iobuf) {
                op_errno = ENOMEM;
                goto out;
        }

        _fd = pfd->fd;
        op_ret = pread (_fd, iobuf->ptr, size, offset);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "read failed on fd=%p: %s", fd,
                        strerror (op_errno));
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
                gf_log (this->name, GF_LOG_ERROR,
                        "fstat failed on fd=%p: %s", fd,
                        strerror (op_errno));
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
                retval = pwrite (fd, vector[idx].iov_base, vector[idx].iov_len,
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
                retval = pwrite (fd, buf, vector[idx].iov_len, internal_off);
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

        if (!fd || !fd->inode || uuid_is_null (fd->inode->gfid)) {
                gf_log_callingfn (this->name, GF_LOG_ERROR, "Invalid Args: "
                                  "fd: %p inode: %p gfid:%s", fd, inode?inode:0,
                                  inode?uuid_utoa(inode->gfid):"N/A");
                goto out;
        }

        if (!xdata || !dict_get (xdata, GLUSTERFS_OPEN_FD_COUNT))
                goto out;

        rsp_xdata = dict_new();
        if (!rsp_xdata)
                goto out;

        ret = dict_set_uint32 (rsp_xdata, GLUSTERFS_OPEN_FD_COUNT,
                               fd->inode->fd_count);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING, "%s: Failed to set "
                        "dictionary value for %s", uuid_utoa (fd->inode->gfid),
                        GLUSTERFS_OPEN_FD_COUNT);
        }

        ret = dict_set_uint32 (rsp_xdata, GLUSTERFS_WRITE_IS_APPEND,
                               is_append);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING, "%s: Failed to set "
                        "dictionary value for %s", uuid_utoa (fd->inode->gfid),
                        GLUSTERFS_WRITE_IS_APPEND);
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

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (vector, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        VALIDATE_OR_GOTO (priv, out);

        ret = posix_fd_ctx_get (fd, this, &pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "pfd is NULL from fd=%p", fd);
                op_errno = -ret;
                goto out;
        }

        _fd = pfd->fd;

	if (xdata && dict_get (xdata, GLUSTERFS_WRITE_IS_APPEND)) {
		/* The write_is_append check and write must happen
		   atomically. Else another write can overtake this
		   write after the check and get written earlier.

		   So lock before preop-stat and unlock after write.
		*/
		locked = _gf_true;
		LOCK(&fd->inode->lock);
	}

        op_ret = posix_fdstat (this, _fd, &preop);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation fstat failed on fd=%p: %s", fd,
                        strerror (op_errno));
                goto out;
        }

	if (locked) {
		if (preop.ia_size == offset || (fd->flags & O_APPEND))
			is_append = 1;
	}

        op_ret = __posix_writev (_fd, vector, count, offset,
                                 (pfd->flags & O_DIRECT));

	if (locked) {
		UNLOCK (&fd->inode->lock);
		locked = _gf_false;
	}

        if (op_ret < 0) {
                op_errno = -op_ret;
                op_ret = -1;
                gf_log (this->name, GF_LOG_ERROR, "write failed: offset %"PRIu64
                        ", %s", offset, strerror (op_errno));
                goto out;
        }

        LOCK (&priv->lock);
        {
                priv->write_value    += op_ret;
        }
        UNLOCK (&priv->lock);

        if (op_ret >= 0) {
                rsp_xdata = _fill_writev_xdata (fd, xdata, this, is_append);
                /* wiretv successful, we also need to get the stat of
                 * the file we wrote to
                 */

                if (flags & (O_SYNC|O_DSYNC)) {
                        ret = fsync (_fd);
			if (ret) {
				gf_log (this->name, GF_LOG_ERROR,
					"fsync() in writev on fd %d failed: %s",
					_fd, strerror (errno));
				op_ret = -1;
				op_errno = errno;
				goto out;
			}
                }

                ret = posix_fdstat (this, _fd, &postop);
                if (ret == -1) {
                        op_ret = -1;
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "post-operation fstat failed on fd=%p: %s",
                                fd, strerror (op_errno));
                        goto out;
                }
        }

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

        priv = this->private;

        op_ret = statvfs (real_path, &buf);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "statvfs failed on %s: %s",
                        real_path, strerror (op_errno));
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

        ret = posix_fd_ctx_get (fd, this, &pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_WARNING,
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
                gf_log (this->name, GF_LOG_WARNING,
                        "pfd is NULL from fd=%p", fd);
                goto out;
        }
        pfd = (struct posix_fd *)(long)tmp_pfd;

        if (pfd->dir) {
                gf_log (this->name, GF_LOG_WARNING,
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

        ret = posix_fd_ctx_get (fd, this, &pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_WARNING,
                        "pfd not found in fd's ctx");
                goto out;
        }

        _fd = pfd->fd;

        op_ret = posix_fdstat (this, _fd, &preop);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING,
                        "pre-operation fstat failed on fd=%p: %s", fd,
                        strerror (op_errno));
                goto out;
        }

        if (datasync) {
                ;
#ifdef HAVE_FDATASYNC
                op_ret = fdatasync (_fd);
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "fdatasync on fd=%p failed: %s",
                                fd, strerror (errno));
                }
#endif
        } else {
                op_ret = fsync (_fd);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "fsync on fd=%p failed: %s",
                                fd, strerror (op_errno));
                        goto out;
                }
        }

        op_ret = posix_fdstat (this, _fd, &postop);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING,
                        "post-operation fstat failed on fd=%p: %s", fd,
                        strerror (op_errno));
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
                                  filler->flags);
}

int32_t
posix_setxattr (call_frame_t *frame, xlator_t *this,
                loc_t *loc, dict_t *dict, int flags, dict_t *xdata)
{
        int32_t       op_ret                  = -1;
        int32_t       op_errno                = 0;
        char *        real_path               = NULL;

        posix_xattr_filler_t filler = {0,};

        DECLARE_OLD_FS_ID_VAR;
        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);
        VALIDATE_OR_GOTO (dict, out);

        MAKE_INODE_HANDLE (real_path, this, loc, NULL);

        op_ret = -1;
        dict_del (dict, GFID_XATTR_KEY);
        dict_del (dict, GF_XATTR_VOL_ID_KEY);

        filler.real_path = real_path;
        filler.this = this;
        filler.flags = flags;
        op_ret = dict_foreach (dict, _handle_setxattr_keyvalue_pair,
                               &filler);
        if (op_ret < 0) {
                op_errno = -op_ret;
                op_ret = -1;
        }

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno, NULL);

        return 0;
}


int
posix_xattr_get_real_filename (call_frame_t *frame, xlator_t *this, loc_t *loc,
			       const char *key, dict_t *dict, dict_t *xdata)
{
	char *real_path = NULL;
	struct dirent *dirent = NULL;
	DIR *fd = NULL;
	const char *fname = NULL;
	char *found = NULL;
	int ret = -1;
	int op_ret = -1;

        MAKE_INODE_HANDLE (real_path, this, loc, NULL);

	fd = opendir (real_path);
	if (!fd)
		return -errno;

	fname = key + strlen (GF_XATTR_GET_REAL_FILENAME_KEY);

	while ((dirent = readdir (fd))) {
		if (strcasecmp (dirent->d_name, fname) == 0) {
			found = gf_strdup (dirent->d_name);
			if (!found) {
				closedir (fd);
				return -ENOMEM;
			}
			break;
		}
	}

	closedir (fd);

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
        char    dirpath[PATH_MAX+1]       = {0,};
        inode_t              *inode       = NULL;
        int                   ret         = -1;

        priv = this->private;

        handle_size = POSIX_GFID_HANDLE_SIZE(priv->base_path_length);

        ret = posix_make_ancestryfromgfid (this, dirpath, PATH_MAX + 1, head,
                                           type | POSIX_ANCESTRY_PATH,
                                           leaf_inode->gfid,
                                           handle_size, priv->base_path,
                                           leaf_inode->table, &inode, xdata);
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
                               inode_t *parent, uint64_t ino,
                               gf_dirent_t *head, char **path,
                               int type, dict_t *xdata, int32_t *op_errno)
{
        DIR                  *dirp         = NULL;
        int                   op_ret       = -1;
        struct dirent        *entry        = NULL;
        struct dirent        *result       = NULL;
        inode_t              *linked_inode = NULL;
        gf_dirent_t          *gf_entry     = NULL;
        char    temppath[PATH_MAX+1]       = {0,};
        xlator_t             *this         = NULL;
        struct posix_private *priv         = NULL;
        char                 *tempv        = NULL;

        this = THIS;

        priv = this->private;

        dirp = opendir (dirpath);
        if (!dirp) {
                *op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING,
                        "could not opendir %s: %s", dirpath,
                        strerror (*op_errno));
                goto out;
        }

        entry = alloca (offsetof(struct dirent, d_name) + NAME_MAX + 1);
        if (entry == NULL)
                goto out;

        while (count > 0) {
                *op_errno = readdir_r (dirp, entry, &result);
                if ((result == NULL) || *op_errno)
                        break;

                if (entry->d_ino != ino)
                        continue;

                linked_inode = inode_link (leaf_inode, parent,
                                           entry->d_name, NULL);

                GF_ASSERT (linked_inode == leaf_inode);
                inode_unref (linked_inode);

                if (type & POSIX_ANCESTRY_DENTRY) {
                        loc_t loc = {0, };

                        loc.inode = inode_ref (leaf_inode);
                        uuid_copy (loc.gfid, leaf_inode->gfid);

                        strcpy (temppath, dirpath);
                        strcat (temppath, "/");
                        strcat (temppath, entry->d_name);

                        gf_entry = gf_dirent_for_name (entry->d_name);
                        gf_entry->inode = inode_ref (leaf_inode);
                        gf_entry->dict
                                = posix_lookup_xattr_fill (this,
                                                           temppath,
                                                           &loc, xdata,
                                                           NULL);
                        list_add_tail (&gf_entry->list, &head->list);
                        loc_wipe (&loc);
                }

                if (type & POSIX_ANCESTRY_PATH) {
                        strcpy (temppath,
                                &dirpath[priv->base_path_length]);
                        strcat (temppath, "/");
                        strcat (temppath, entry->d_name);
                        if (!*path) {
                                *path = gf_strdup (temppath);
                        } else {
                                /* creating a colon separated */
                                /* list of hard links */
                                tempv  = GF_REALLOC (*path, strlen (*path)
                                                     + 1  // ':'
                                                     + strlen (temppath) + 1 );
                                if (!tempv) {
                                        gf_log (this->name, GF_LOG_WARNING,
                                                "realloc failed on path");
                                        GF_FREE (*path);
                                        op_ret = -1;
                                        *op_errno = ENOMEM;
                                        goto out;
                                }

                                *path = tempv;
                                strcat (*path, ":");
                                strcat (*path, temppath);
                        }
                }

                count--;
        }

out:
        if (dirp) {
                op_ret = closedir (dirp);
                if (op_ret == -1) {
                        *op_errno = errno;
                        gf_log (this->name, GF_LOG_WARNING,
                                "closedir failed: %s",
                                strerror (*op_errno));
                }
        }

        return op_ret;
}

int
posix_get_ancestry_non_directory (xlator_t *this, inode_t *leaf_inode,
                                  gf_dirent_t *head, char **path, int type,
                                  int32_t *op_errno, dict_t *xdata)
{
        size_t                remaining_size        = 0;
        char    dirpath[PATH_MAX+1]                 = {0,}, *leaf_path = NULL;
        int                   op_ret                = -1, pathlen = -1;
        ssize_t               handle_size           = 0;
        char    pgfidstr[UUID_CANONICAL_FORM_LEN+1] = {0,};
        uuid_t                pgfid                 = {0, };
        int                   nlink_samepgfid       = 0;
        struct stat           stbuf                 = {0,};
        char                 *list                  = NULL;
        int32_t               list_offset           = 0;
        char     key[4096]                          = {0,};
        struct posix_private *priv                  = NULL;
        ssize_t               size                  = 0;
        inode_t              *parent                = NULL;
        loc_t                *loc                   = NULL;

        priv = this->private;

        loc = GF_CALLOC (1, sizeof (*loc), gf_posix_mt_char);
        if (loc == NULL) {
                op_ret = -1;
                *op_errno = ENOMEM;
                goto out;
        }

        uuid_copy (loc->gfid, leaf_inode->gfid);

        MAKE_INODE_HANDLE (leaf_path, this, loc, NULL);

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
                        gf_log (this->name, GF_LOG_WARNING,
                                "listxattr failed on %s: %s",
                                leaf_path, strerror (*op_errno));

                }

                goto out;
        }

        if (size == 0) {
                op_ret = 0;
                goto out;
        }

        list = alloca (size + 1);
        if (!list) {
                *op_errno = errno;
                goto out;
        }

        size = sys_llistxattr (leaf_path, list, size);
        remaining_size = size;
        list_offset = 0;

        op_ret = sys_lstat (leaf_path, &stbuf);
        if (op_ret == -1) {
                *op_errno = errno;
                gf_log (this->name, GF_LOG_WARNING, "lstat failed"
                        " on %s: %s", leaf_path,
                        strerror (*op_errno));
                goto out;
        }

        while (remaining_size > 0) {
                if (*(list + list_offset) == '\0')
                        break;
                strcpy (key, list + list_offset);
                if (strncmp (key, PGFID_XATTR_KEY_PREFIX,
                             strlen (PGFID_XATTR_KEY_PREFIX)) != 0)
                        goto next;

                op_ret = sys_lgetxattr (leaf_path, key,
                                        &nlink_samepgfid,
                                        sizeof(nlink_samepgfid));
                if (op_ret == -1) {
                        *op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR,
                                "getxattr failed on "
                                "%s: key = %s (%s)",
                                leaf_path,
                                key,
                                strerror (*op_errno));
                        goto out;
                }

                nlink_samepgfid = ntoh32 (nlink_samepgfid);

                strcpy (pgfidstr, key + strlen(PGFID_XATTR_KEY_PREFIX));
                uuid_parse (pgfidstr, pgfid);

                handle_size = POSIX_GFID_HANDLE_SIZE(priv->base_path_length);

                /* constructing the absolute real path of parent dir */
                strcpy (dirpath, priv->base_path);
                pathlen = PATH_MAX + 1 - priv->base_path_length;

                op_ret = posix_make_ancestryfromgfid (this,
                                                      dirpath + priv->base_path_length,
                                                      pathlen,
                                                      head,
                                                      POSIX_ANCESTRY_PATH | POSIX_ANCESTRY_DENTRY,
                                                      pgfid,
                                                      handle_size,
                                                      priv->base_path,
                                                      leaf_inode->table,
                                                      &parent, xdata);
                if (op_ret < 0) {
                        goto next;
                }

                dirpath[strlen (dirpath) - 1] = '\0';

                posix_links_in_same_directory (dirpath, nlink_samepgfid,
                                               leaf_inode,
                                               parent, stbuf.st_ino, head,
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

        if (!priv->update_pgfid_nlinks)
                goto out;

        if (IA_ISDIR (leaf_inode->ia_type)) {
                ret = posix_get_ancestry_directory (this, leaf_inode,
                                                    head, path, type, op_errno,
                                                    xdata);
        } else  {
                ret = posix_get_ancestry_non_directory (this, leaf_inode,
                                                        head, path, type,
                                                        op_errno, xdata);
        }

out:
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
        char     host_buf[1024]                     = {0,};
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
        char     key[4096]                          = {0,};

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_INODE_HANDLE (real_path, this, loc, NULL);

        op_ret = -1;
        priv = this->private;

        if (loc->inode && IA_ISDIR(loc->inode->ia_type) && name &&
            ZR_FILE_CONTENT_REQUEST(name)) {
                ret = posix_get_file_contents (this, loc->gfid, &name[15],
                                               &file_contents);
                if (ret < 0) {
                        op_errno = -ret;
                        gf_log (this->name, GF_LOG_ERROR,
                                "getting file contents failed: %s",
                                strerror (op_errno));
                        goto out;
                }
        }

        dict = dict_new ();
        if (!dict) {
		op_errno = ENOMEM;
                goto out;
        }

	if (loc->inode && name &&
	    (strncmp (name, GF_XATTR_GET_REAL_FILENAME_KEY,
		      strlen (GF_XATTR_GET_REAL_FILENAME_KEY)) == 0)) {
		ret = posix_xattr_get_real_filename (frame, this, loc,
						     name, dict, xdata);
		if (ret < 0) {
			op_ret = -1;
			op_errno = -ret;
			gf_log (this->name, (op_errno == ENOENT) ?
                                GF_LOG_DEBUG : GF_LOG_WARNING,
				"Failed to get real filename (%s, %s): %s",
				loc->path, name, strerror (op_errno));
			goto out;
		}

		size = ret;
		goto done;
	}

        if (loc->inode && name && !strcmp (name, GLUSTERFS_OPEN_FD_COUNT)) {
                if (!list_empty (&loc->inode->fd_list)) {
                        ret = dict_set_uint32 (dict, (char *)name, 1);
                        if (ret < 0)
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Failed to set dictionary value for %s",
                                        name);
                } else {
                        ret = dict_set_uint32 (dict, (char *)name, 0);
                        if (ret < 0)
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Failed to set dictionary value for %s",
                                        name);
                }
                goto done;
        }
        if (loc->inode && name && (XATTR_IS_PATHINFO (name))) {
                if (LOC_HAS_ABSPATH (loc))
                        MAKE_REAL_PATH (rpath, this, loc->path);
                else
                        rpath = real_path;

                (void) snprintf (host_buf, 1024,
                                 "<POSIX(%s):%s:%s>", priv->base_path,
                                 ((priv->node_uuid_pathinfo
                                   && !uuid_is_null(priv->glusterd_uuid))
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
                        gf_log (this->name, GF_LOG_WARNING,
                                "could not set value (%s) in dictionary",
                                dyn_rpath);
                        GF_FREE (dyn_rpath);
                }

                goto done;
        }

        if (loc->inode && name &&
            (strcmp (name, GF_XATTR_NODE_UUID_KEY) == 0)
            && !uuid_is_null (priv->glusterd_uuid)) {
                (void) snprintf (host_buf, 1024, "%s",
                                 uuid_utoa (priv->glusterd_uuid));

                dyn_rpath = gf_strdup (host_buf);
                if (!dyn_rpath) {
                        ret = -1;
                        goto done;
                }

                size = strlen (dyn_rpath) + 1;
                ret = dict_set_dynstr (dict, GF_XATTR_NODE_UUID_KEY,
                                       dyn_rpath);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "could not set value (%s) in dictionary",
                                dyn_rpath);
                        GF_FREE (dyn_rpath);
                }
                goto done;
        }

        if (loc->inode && name &&
            (strcmp (name, GFID_TO_PATH_KEY) == 0)) {
                ret = inode_path (loc->inode, NULL, &path);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING, "%s: could not get "
                                "inode path", uuid_utoa (loc->inode->gfid));
                        goto done;
                }

                ret = dict_set_dynstr (dict, GFID_TO_PATH_KEY, path);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "could not set value (%s) in dictionary",
                                host_buf);
                        GF_FREE (path);
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
                        gf_log (this->name, GF_LOG_WARNING, "could not get "
                                "value for key (%s)", GET_ANCESTRY_PATH_KEY);
                        GF_FREE (path);
                        op_errno = -op_ret;
                        op_ret = -1;
                }

                goto done;
        }

        if (name) {
                strcpy (key, name);

                size = sys_lgetxattr (real_path, key, NULL, 0);
                if (size <= 0) {
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
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "No such attribute:%s for file %s",
                                        key, real_path);
                        } else {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "getxattr failed on %s: %s (%s)",
                                        real_path, key, strerror (op_errno));
                        }

                        goto done;
                }
                value = GF_CALLOC (size + 1, sizeof(char), gf_posix_mt_char);
                if (!value) {
                        op_ret = -1;
                        goto out;
                }
                size = sys_lgetxattr (real_path, key, value, size);
                if (size == -1) {
                        op_ret = -1;
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR, "getxattr failed on "
                                "%s: key = %s (%s)", real_path, key,
                                strerror (op_errno));
                        GF_FREE (value);
                        goto out;
                }
                value [size] = '\0';
                op_ret = dict_set_dynptr (dict, key, value, size);
                if (op_ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "dict set operation "
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
                        gf_log (this->name, GF_LOG_ERROR,
                                "listxattr failed on %s: %s",
                                real_path, strerror (op_errno));
                }
                goto out;
        }

        if (size == 0)
                goto done;

        list = alloca (size + 1);
        if (!list) {
                op_errno = errno;
                goto out;
        }

        size = sys_llistxattr (real_path, list, size);

        remaining_size = size;
        list_offset = 0;
        while (remaining_size > 0) {
                if (*(list + list_offset) == '\0')
                        break;

                strcpy (key, list + list_offset);
                size = sys_lgetxattr (real_path, key, NULL, 0);
                if (size == -1) {
                        op_ret = -1;
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR, "getxattr failed on "
                                "%s: key = %s (%s)", real_path, key,
                                strerror (op_errno));
                        break;
                }

                value = GF_CALLOC (size + 1, sizeof(char),
                                   gf_posix_mt_char);
                if (!value) {
                        op_errno = errno;
                        goto out;
                }

                size = sys_lgetxattr (real_path, key, value, size);
                if (size == -1) {
                        op_ret = -1;
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR, "getxattr failed on "
                                "%s: key = %s (%s)", real_path, key,
                                strerror (op_errno));
                        GF_FREE (value);
                        break;
                }

                value [size] = '\0';
                op_ret = dict_set_dynptr (dict, key, value, size);
                if (op_ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "dict set operation "
                                "on %s for the key %s failed.", real_path, key);
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
        int32_t           op_errno       = ENOENT;
        struct posix_fd * pfd            = NULL;
        int               _fd            = -1;
        int32_t           list_offset    = 0;
        ssize_t           size           = 0;
        size_t            remaining_size = 0;
        char              key[4096]      = {0,};
        char *            value          = NULL;
        char *            list           = NULL;
        dict_t *          dict           = NULL;
        int               ret            = -1;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);

        ret = posix_fd_ctx_get (fd, this, &pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_WARNING,
                        "pfd is NULL from fd=%p", fd);
                goto out;
        }

        _fd = pfd->fd;

        /* Get the total size */
        dict = get_new_dict ();
        if (!dict) {
                goto out;
        }

        if (name && !strcmp (name, GLUSTERFS_OPEN_FD_COUNT)) {
                ret = dict_set_uint32 (dict, (char *)name, 1);
                if (ret < 0)
                        gf_log (this->name, GF_LOG_WARNING,
                                "Failed to set dictionary value for %s",
                                name);
                goto done;
        }

        if (name) {
                strcpy (key, name);

                size = sys_fgetxattr (_fd, key, NULL, 0);
                if (size <= 0) {
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR, "fgetxattr failed on "
                                "key %s (%s)", key, strerror (op_errno));
                        goto done;
                }

                value = GF_CALLOC (size + 1, sizeof(char), gf_posix_mt_char);
                if (!value) {
                        op_ret = -1;
                        goto out;
                }
                size = sys_fgetxattr (_fd, key, value, size);
                if (size == -1) {
                        op_ret = -1;
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR, "fgetxattr failed on "
                                "fd %p for the key %s (%s)", fd, key,
                                strerror (op_errno));
                        GF_FREE (value);
                        goto out;
                }
                value [size] = '\0';
                op_ret = dict_set_dynptr (dict, key, value, size);
                if (op_ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "dict set operation "
                                "on key %s failed", key);
                        GF_FREE (value);
                        goto out;
                }
                goto done;
        }

        size = sys_flistxattr (_fd, NULL, 0);
        if (size == -1) {
                op_errno = errno;
                if ((errno == ENOTSUP) || (errno == ENOSYS)) {
                        GF_LOG_OCCASIONALLY (gf_posix_xattr_enotsup_log,
                                             this->name, GF_LOG_WARNING,
                                             "Extended attributes not "
                                             "supported (try remounting "
                                             "brick with 'user_xattr' flag)");
                }
                else {
                        gf_log (this->name, GF_LOG_ERROR,
                                "listxattr failed on %p: %s",
                                fd, strerror (op_errno));
                }
                goto out;
        }

        if (size == 0)
                goto done;

        list = alloca (size + 1);
        if (!list) {
                op_errno = errno;
                goto out;
        }

        size = sys_flistxattr (_fd, list, size);

        remaining_size = size;
        list_offset = 0;
        while (remaining_size > 0) {
                if(*(list + list_offset) == '\0')
                        break;

                strcpy (key, list + list_offset);
                size = sys_fgetxattr (_fd, key, NULL, 0);
                if (size == -1) {
                        op_ret = -1;
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR, "fgetxattr failed on "
                                "fd %p for the key %s (%s)", fd, key,
                                strerror (op_errno));
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
                        gf_log (this->name, GF_LOG_ERROR, "fgetxattr failed on "
                                "the fd %p for the key %s (%s)", fd, key,
                                strerror (op_errno));
                        GF_FREE (value);
                        break;
                }

                value [size] = '\0';
                op_ret = dict_set_dynptr (dict, key, value, size);
                if (op_ret) {
                        gf_log (this->name, GF_LOG_ERROR, "dict set operation "
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
                dict_ref (dict);
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

        return posix_fhandle_pair (filler->this, filler->fd, k, v,
                                   filler->flags);
}

int32_t
posix_fsetxattr (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, dict_t *dict, int flags, dict_t *xdata)
{
        int32_t            op_ret       = -1;
        int32_t            op_errno     = 0;
        struct posix_fd *  pfd          = NULL;
        int                _fd          = -1;
        int                ret          = -1;

        posix_xattr_filler_t filler = {0,};

        DECLARE_OLD_FS_ID_VAR;
        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (dict, out);

        ret = posix_fd_ctx_get (fd, this, &pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_WARNING,
                        "pfd is NULL from fd=%p", fd);
                goto out;
        }
        _fd = pfd->fd;

        dict_del (dict, GFID_XATTR_KEY);
        dict_del (dict, GF_XATTR_VOL_ID_KEY);

        filler.fd = _fd;
        filler.this = this;
        filler.flags = flags;
        op_ret = dict_foreach (dict, _handle_fsetxattr_keyvalue_pair,
                               &filler);
        if (op_ret < 0) {
                op_errno = -op_ret;
                op_ret = -1;
        }

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno, NULL);

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

        op_ret = sys_lremovexattr (filler->real_path, key);
        if (op_ret == -1) {
                filler->op_errno = errno;
                if (errno != ENOATTR && errno != EPERM)
                        gf_log (this->name, GF_LOG_ERROR,
                                "removexattr failed on %s (for %s): %s",
                                filler->real_path, key, strerror (errno));
        }

        return op_ret;
}


int32_t
posix_removexattr (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, const char *name, dict_t *xdata)
{
        int32_t op_ret    = -1;
        int32_t op_errno  = 0;
        char *  real_path = NULL;
        posix_xattr_filler_t filler = {0,};

        DECLARE_OLD_FS_ID_VAR;

        MAKE_INODE_HANDLE (real_path, this, loc, NULL);

        if (!strcmp (GFID_XATTR_KEY, name)) {
                gf_log (this->name, GF_LOG_WARNING, "Remove xattr called"
                        " on gfid for file %s", real_path);
                op_ret = -1;
                goto out;
        }
        if (!strcmp (GF_XATTR_VOL_ID_KEY, name)) {
                gf_log (this->name, GF_LOG_WARNING, "Remove xattr called"
                        " on volume-id for file %s", real_path);
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
                if (op_errno != ENOATTR && op_errno != EPERM)
                        gf_log (this->name, GF_LOG_ERROR,
                                "removexattr on %s (for %s): %s", real_path,
                                name, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno, NULL);
        return 0;
}

int32_t
posix_fremovexattr (call_frame_t *frame, xlator_t *this,
                    fd_t *fd, const char *name, dict_t *xdata)
{
        int32_t           op_ret   = -1;
        int32_t           op_errno = 0;
        struct posix_fd * pfd      = NULL;
        int               _fd      = -1;
        int               ret      = -1;

        DECLARE_OLD_FS_ID_VAR;

        if (!strcmp (GFID_XATTR_KEY, name)) {
                gf_log (this->name, GF_LOG_WARNING, "Remove xattr called"
                        " on gfid for file");
                goto out;
        }
        if (!strcmp (GF_XATTR_VOL_ID_KEY, name)) {
                gf_log (this->name, GF_LOG_WARNING, "Remove xattr called"
                        " on volume-id for file");
                goto out;
        }

        ret = posix_fd_ctx_get (fd, this, &pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_WARNING,
                        "pfd is NULL from fd=%p", fd);
                goto out;
        }
        _fd = pfd->fd;



        SET_FS_ID (frame->root->uid, frame->root->gid);

        op_ret = sys_fremovexattr (_fd, name);
        if (op_ret == -1) {
                op_errno = errno;
                if (op_errno != ENOATTR && op_errno != EPERM)
                        gf_log (this->name, GF_LOG_ERROR,
                                "fremovexattr (for %s): %s",
                                name, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (fremovexattr, frame, op_ret, op_errno, NULL);
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

        ret = posix_fd_ctx_get (fd, this, &pfd);
        if (ret < 0) {
                op_errno = -ret;
                gf_log (this->name, GF_LOG_WARNING,
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
        gf_log ("posix", GF_LOG_DEBUG,
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
                if (destval == 0xffffffff)
                        continue;
                dest[i] = hton32 (destval + ntoh32 (src[i]));
        }
}

static void
__or_array (int32_t *dest, int32_t *src, int count)
{
        int i = 0;
        for (i = 0; i < count; i++) {
                dest[i] = hton32 (ntoh32 (dest[i]) | ntoh32 (src[i]));
        }
}

static void
__and_array (int32_t *dest, int32_t *src, int count)
{
        int i = 0;
        for (i = 0; i < count; i++) {
                dest[i] = hton32 (ntoh32 (dest[i]) & ntoh32 (src[i]));
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
        inode_t              *inode    = NULL;
        xlator_t             *this     = NULL;
        posix_xattr_filler_t *filler   = NULL;

        filler = tmp;

        optype = (gf_xattrop_flags_t)(filler->flags);
        this = filler->this;
        inode = filler->inode;

        count = v->len;
        array = GF_CALLOC (count, sizeof (char), gf_posix_mt_char);

        LOCK (&inode->lock);
        {
                if (filler->real_path) {
                        size = sys_lgetxattr (filler->real_path, k,
                                              (char *)array, v->len);
                } else {
                        size = sys_fgetxattr (filler->fd, k, (char *)array,
                                              v->len);
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
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "getxattr failed on %s while doing "
                                                "xattrop: Key:%s (%s)",
                                                filler->real_path,
                                                k, strerror (op_errno));
                                else
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "fgetxattr failed on fd=%d while doing "
                                                "xattrop: Key:%s (%s)",
                                                filler->fd,
                                                k, strerror (op_errno));
                        }

                        op_ret = -1;
                        goto unlock;
                }

                switch (optype) {

                case GF_XATTROP_ADD_ARRAY:
                        __add_array ((int32_t *) array, (int32_t *) v->data,
                                     v->len / 4);
                        break;

                case GF_XATTROP_ADD_ARRAY64:
                        __add_long_array ((int64_t *) array, (int64_t *) v->data,
                                          v->len / 8);
                        break;

                case GF_XATTROP_OR_ARRAY:
                        __or_array ((int32_t *) array,
                                    (int32_t *) v->data,
                                    v->len / 4);
                        break;

                case GF_XATTROP_AND_ARRAY:
                        __and_array ((int32_t *) array,
                                     (int32_t *) v->data,
                                     v->len / 4);
                        break;

                default:
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unknown xattrop type (%d) on %s. Please send "
                                "a bug report to gluster-devel@nongnu.org",
                                optype, filler->real_path);
                        op_ret = -1;
                        op_errno = EINVAL;
                        goto unlock;
                }

                if (filler->real_path) {
                        size = sys_lsetxattr (filler->real_path, k, array,
                                              v->len, 0);
                } else {
                        size = sys_fsetxattr (filler->fd, k, (char *)array,
                                              v->len, 0);
                }
        }
unlock:
        UNLOCK (&inode->lock);

        if (op_ret == -1)
                goto out;

        op_errno = errno;
        if (size == -1) {
                if (filler->real_path)
                        gf_log (this->name, GF_LOG_ERROR,
                                "setxattr failed on %s while doing xattrop: "
                                "key=%s (%s)", filler->real_path,
                                k, strerror (op_errno));
                else
                        gf_log (this->name, GF_LOG_ERROR,
                                "fsetxattr failed on fd=%d while doing xattrop: "
                                "key=%s (%s)", filler->fd,
                                k, strerror (op_errno));

                op_ret = -1;
                goto out;
        } else {
                size = dict_set_bin (d, k, array, v->len);

                if (size != 0) {
                        if (filler->real_path)
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "dict_set_bin failed (path=%s): "
                                        "key=%s (%s)", filler->real_path,
                                        k, strerror (-size));
                        else
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "dict_set_bin failed (fd=%d): "
                                        "key=%s (%s)", filler->fd,
                                        k, strerror (-size));

                        op_ret = -1;
                        op_errno = EINVAL;
                        goto out;
                }
                array = NULL;
        }

        array = NULL;

out:
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

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (xattr, out);
        VALIDATE_OR_GOTO (this, out);

        if (fd) {
                op_ret = posix_fd_ctx_get (fd, this, &pfd);
                if (op_ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "failed to get pfd from fd=%p",
                                fd);
                        op_errno = EBADFD;
                        goto out;
                }
                _fd = pfd->fd;
        }

        if (loc && !uuid_is_null (loc->gfid))
                MAKE_INODE_HANDLE (real_path, this, loc, NULL);

        if (real_path) {
                inode = loc->inode;
        } else if (fd) {
                inode = fd->inode;
        }

        filler.this = this;
        filler.fd = _fd;
        filler.real_path = real_path;
        filler.flags = (int)optype;
        filler.inode = inode;

        op_ret = dict_foreach (xattr, _posix_handle_xattr_keyvalue_pair,
                               &filler);

out:

        STACK_UNWIND_STRICT (xattrop, frame, op_ret, op_errno, xattr, NULL);
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

        op_ret = access (real_path, mask & 07);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "access failed on %s: %s",
                        real_path, strerror (op_errno));
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

        ret = posix_fd_ctx_get (fd, this, &pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "pfd is NULL, fd=%p", fd);
                op_errno = -ret;
                goto out;
        }

        _fd = pfd->fd;

        op_ret = posix_fdstat (this, _fd, &preop);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "pre-operation fstat failed on fd=%p: %s", fd,
                        strerror (op_errno));
                goto out;
        }

        op_ret = ftruncate (_fd, offset);

        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "ftruncate failed on fd=%p (%"PRId64": %s",
                        fd, offset, strerror (errno));
                goto out;
        }

        op_ret = posix_fdstat (this, _fd, &postop);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "post-operation fstat failed on fd=%p: %s",
                        fd, strerror (errno));
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
        int                   ret      = -1;
        struct posix_private *priv     = NULL;

        DECLARE_OLD_FS_ID_VAR;
        SET_FS_ID (frame->root->uid, frame->root->gid);

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        ret = posix_fd_ctx_get (fd, this, &pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "pfd is NULL, fd=%p", fd);
                op_errno = -ret;
                goto out;
        }

        _fd = pfd->fd;

        op_ret = posix_fdstat (this, _fd, &buf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "fstat failed on fd=%p: %s",
                        fd, strerror (op_errno));
                goto out;
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, &buf, NULL);
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
        off_t     in_case = -1;
        size_t    filled = 0;
        int             count = 0;
        char      entrybuf[sizeof(struct dirent) + 256 + 8];
        struct dirent  *entry          = NULL;
        int32_t               this_size      = -1;
        gf_dirent_t          *this_entry     = NULL;
        uuid_t                rootgfid = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
        struct stat           stbuf = {0,};
        char                 *hpath = NULL;
        int                   len = 0;
        int                   ret = 0;

        if (skip_dirs) {
                len = posix_handle_path (this, fd->inode->gfid, NULL, NULL, 0);
                hpath = alloca (len + 256); /* NAME_MAX */
                posix_handle_path (this, fd->inode->gfid, NULL, hpath, len);
                len = strlen (hpath);
                hpath[len] = '/';
        }

        if (!off) {
                rewinddir (dir);
        } else {
                seekdir (dir, off);
        }

        while (filled <= size) {
                in_case = telldir (dir);

                if (in_case == -1) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "telldir failed on dir=%p: %s",
                                dir, strerror (errno));
                        goto out;
                }

                errno = 0;
                entry = NULL;
                readdir_r (dir, (struct dirent *)entrybuf, &entry);

                if (!entry) {
                        if (errno == EBADF) {
                                gf_log (THIS->name, GF_LOG_WARNING,
                                        "readdir failed on dir=%p: %s",
                                        dir, strerror (errno));
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
		* exended attribute backing file accross storage servers.
		*/
		if ((uuid_compare (fd->inode->gfid, rootgfid) == 0)
		    && (!strcmp(entry->d_name, ".attribute")))
			continue;
#endif /* __NetBSD__ */

                if ((uuid_compare (fd->inode->gfid, rootgfid) == 0)
                    && (!strcmp (GF_HIDDEN_PATH, entry->d_name))) {
                        continue;
                }

                if (skip_dirs) {
                        if (DT_ISDIR (entry->d_type)) {
                                continue;
                        } else if (hpath) {
                                strcpy (&hpath[len+1],entry->d_name);
                                ret = lstat (hpath, &stbuf);
                                if (!ret && S_ISDIR (stbuf.st_mode))
                                        continue;
                        }
                }

                this_size = max (sizeof (gf_dirent_t),
                                 sizeof (gfs3_dirplist))
                        + strlen (entry->d_name) + 1;

                if (this_size + filled > size) {
                        seekdir (dir, in_case);
                        break;
                }

                this_entry = gf_dirent_for_name (entry->d_name);

                if (!this_entry) {
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "could not create gf_dirent for entry %s: (%s)",
                                entry->d_name, strerror (errno));
                        goto out;
                }
                this_entry->d_off = telldir (dir);
                this_entry->d_ino = entry->d_ino;
                this_entry->d_type = entry->d_type;

                list_add_tail (&this_entry->list, &entries->list);

                filled += this_size;
                count ++;
        }

        if ((!readdir (dir) && (errno == 0)))
                /* Indicate EOF */
                errno = ENOENT;
out:
        return count;
}

dict_t *
posix_entry_xattr_fill (xlator_t *this, inode_t *inode,
                        fd_t *fd, char *name, dict_t *dict,
                        struct iatt *stbuf)
{
        loc_t  tmp_loc    = {0,};
        char  *entry_path = NULL;

        /* if we don't send the 'loc', open-fd-count be a problem. */
        tmp_loc.inode = inode;

        MAKE_HANDLE_PATH (entry_path, this, fd->inode->gfid, name);

        return posix_lookup_xattr_fill (this, entry_path,
                                        &tmp_loc, dict, stbuf);

}


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

	if (list_empty(&entries->list))
		return 0;

        itable = fd->inode->table;

	len = posix_handle_path (this, fd->inode->gfid, NULL, NULL, 0);
	hpath = alloca (len + 256); /* NAME_MAX */
	posix_handle_path (this, fd->inode->gfid, NULL, hpath, len);
	len = strlen (hpath);
	hpath[len] = '/';

        list_for_each_entry (entry, &entries->list, list) {
		memset (gfid, 0, 16);
		inode = inode_grep (fd->inode->table, fd->inode,
				    entry->d_name);
		if (inode)
			uuid_copy (gfid, inode->gfid);

		strcpy (&hpath[len+1], entry->d_name);

                posix_pstat (this, gfid, hpath, &stbuf);

		if (!inode)
			inode = inode_find (itable, stbuf.ia_gfid);

		if (!inode)
			inode = inode_new (itable);

		entry->inode = inode;

                if (dict) {
                        entry->dict =
                                posix_entry_xattr_fill (this, entry->inode,
                                                        fd, entry->d_name,
                                                        dict, &stbuf);
                        dict_ref (entry->dict);
                }

                entry->d_stat = stbuf;
                if (stbuf.ia_ino)
                        entry->d_ino = stbuf.ia_ino;
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

        ret = posix_fd_ctx_get (fd, this, &pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "pfd is NULL, fd=%p", fd);
                op_errno = -ret;
                goto out;
        }

        dir = pfd->dir;

        if (!dir) {
                gf_log (this->name, GF_LOG_WARNING,
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

        snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s", this->type,
                 this->name);
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
        int32_t                 weak_checksum   = 0;
        unsigned char           strong_checksum[MD5_DIGEST_LENGTH] = {0};
        struct posix_private    *priv           = NULL;

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

        ret = posix_fd_ctx_get (fd, this, &pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "pfd is NULL, fd=%p", fd);
                op_errno = -ret;
                goto out;
        }

        _fd = pfd->fd;

        LOCK (&fd->lock);
        {
                if (priv->aio_capable && priv->aio_init_done)
                        __posix_fd_set_odirect (fd, pfd, 0, offset, len);

                ret = pread (_fd, buf, len, offset);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "pread of %d bytes returned %d (%s)",
                                len, ret, strerror (errno));

                        op_errno = errno;
                }

        }
        UNLOCK (&fd->lock);

        if (ret < 0)
                goto out;

        weak_checksum = gf_rsync_weak_checksum ((unsigned char *) buf, (size_t) len);
        gf_rsync_strong_checksum ((unsigned char *) buf, (size_t) len, (unsigned char *) strong_checksum);

        op_ret = 0;
out:
        STACK_UNWIND_STRICT (rchecksum, frame, op_ret, op_errno,
                             weak_checksum, strong_checksum, NULL);

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
                gf_log(this->name, GF_LOG_ERROR, "Memory accounting init"
                       "failed");
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
		gf_log (this->name, GF_LOG_ERROR, "Failed to stat "
			"brick path %s (%s)",
			priv->base_path, strerror (errno));
		return ret;
	}

	if ((uid == -1 || st.st_uid == uid) &&
	    (gid == -1 || st.st_gid == gid))
		return 0;

        ret = sys_chown (priv->base_path, uid, gid);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "Failed to set "
                        "uid/gid for brick path %s, %s",
                        priv->base_path, strerror (errno));

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
		gf_log (this->name, GF_LOG_ERROR, "Unknown mode string: %s",
			batch_fsync_mode_str);
		goto out;
	}

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
            (uuid_is_null (priv->glusterd_uuid))) {
                    gf_log (this->name, GF_LOG_INFO,
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
        int                   dict_ret      = 0;
        int                   ret           = 0;
        int                   op_ret        = -1;
        ssize_t               size          = -1;
        int32_t               janitor_sleep = 0;
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
                gf_log (this->name, GF_LOG_CRITICAL,
                        "FATAL: storage/posix cannot have subvolumes");
                ret = -1;
                goto out;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Volume is dangling. Please check the volume file.");
        }

        if (!dir_data) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "Export directory not specified in volume file.");
                ret = -1;
                goto out;
        }

        umask (000); // umask `masking' is done at the client side

        /* Check whether the specified directory exists, if not log it. */
        op_ret = stat (dir_data->data, &buf);
        if ((op_ret != 0) || !S_ISDIR (buf.st_mode)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Directory '%s' doesn't exist, exiting.",
                        dir_data->data);
                ret = -1;
                goto out;
        }

        /* Check for Extended attribute support, if not present, log it */
        op_ret = sys_lsetxattr (dir_data->data,
                                "trusted.glusterfs.test", "working", 8, 0);
        if (op_ret == 0) {
                sys_lremovexattr (dir_data->data, "trusted.glusterfs.test");
        } else {
                tmp_data = dict_get (this->options,
                                     "mandate-attribute");
                if (tmp_data) {
                        if (gf_string2boolean (tmp_data->data,
                                               &tmp_bool) == -1) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "wrong option provided for key "
                                        "\"mandate-attribute\"");
                                ret = -1;
                                goto out;
                        }
                        if (!tmp_bool) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Extended attribute not supported, "
                                        "starting as per option");
                        } else {
                                gf_log (this->name, GF_LOG_CRITICAL,
                                        "Extended attribute not supported, "
                                        "exiting.");
                                ret = -1;
                                goto out;
                        }
                } else {
                        gf_log (this->name, GF_LOG_CRITICAL,
                                "Extended attribute not supported, exiting.");
                        ret = -1;
                        goto out;
                }
        }

        tmp_data = dict_get (this->options, "volume-id");
        if (tmp_data) {
                op_ret = uuid_parse (tmp_data->data, dict_uuid);
                if (op_ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "wrong volume-id (%s) set in volume file",
                                tmp_data->data);
                        ret = -1;
                        goto out;
                }
                size = sys_lgetxattr (dir_data->data,
                                      "trusted.glusterfs.volume-id", old_uuid, 16);
                if (size == 16) {
                        if (uuid_compare (old_uuid, dict_uuid)) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "mismatching volume-id (%s) received. "
                                        "already is a part of volume %s ",
                                        tmp_data->data, uuid_utoa (old_uuid));
                                ret = -1;
                                goto out;
                        }
                } else if ((size == -1) && (errno == ENODATA)) {

                                gf_log (this->name, GF_LOG_ERROR,
                                        "Extended attribute trusted.glusterfs."
                                        "volume-id is absent");
                                ret = -1;
                                goto out;

                }  else if ((size == -1) && (errno != ENODATA)) {
                        /* Wrong 'volume-id' is set, it should be error */
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: failed to fetch volume-id (%s)",
                                dir_data->data, strerror (errno));
                        ret = -1;
                        goto out;
                } else {
                        ret = -1;
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to fetch proper volume id from export");
                        goto out;
                }
        }

        /* Now check if the export directory has some other 'gfid',
           other than that of root '/' */
        size = sys_lgetxattr (dir_data->data, "trusted.gfid", gfid, 16);
        if (size == 16) {
                if (!__is_root_gfid (gfid)) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s: gfid (%s) is not that of glusterfs '/' ",
                                dir_data->data, uuid_utoa (gfid));
                        ret = -1;
                        goto out;
                }
        } else if (size != -1) {
                /* Wrong 'gfid' is set, it should be error */
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: wrong value set as gfid",
                        dir_data->data);
                ret = -1;
                goto out;
        } else if ((size == -1) && (errno != ENODATA)) {
                /* Wrong 'gfid' is set, it should be error */
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to fetch gfid (%s)",
                        dir_data->data, strerror (errno));
                ret = -1;
                goto out;
        } else {
                /* First time volume, set the GFID */
                size = sys_lsetxattr (dir_data->data, "trusted.gfid", rootgfid,
                                     16, XATTR_CREATE);
                if (size) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "%s: failed to set gfid (%s)",
                                dir_data->data, strerror (errno));
                        ret = -1;
                        goto out;
                }
        }

        size = sys_lgetxattr (dir_data->data, POSIX_ACL_ACCESS_XATTR,
                              NULL, 0);
        if ((size < 0) && (errno == ENOTSUP))
                gf_log (this->name, GF_LOG_WARNING,
                        "Posix access control list is not supported.");

        ret = 0;
        _private = GF_CALLOC (1, sizeof (*_private),
                              gf_posix_mt_posix_private);
        if (!_private) {
                ret = -1;
                goto out;
        }

        _private->base_path = gf_strdup (dir_data->data);
        _private->base_path_length = strlen (_private->base_path);

        LOCK_INIT (&_private->lock);

        ret = dict_get_str (this->options, "hostname", &_private->hostname);
        if (ret) {
                _private->hostname = GF_CALLOC (256, sizeof (char),
                                                gf_common_mt_char);
                if (!_private->hostname) {
                        goto out;
                }
                ret = gethostname (_private->hostname, 256);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "could not find hostname (%s)", strerror (errno));
                }
        }

        _private->export_statfs = 1;
        tmp_data = dict_get (this->options, "export-statfs-size");
        if (tmp_data) {
                if (gf_string2boolean (tmp_data->data,
                                       &_private->export_statfs) == -1) {
                        ret = -1;
                        gf_log (this->name, GF_LOG_ERROR,
                                "'export-statfs-size' takes only boolean "
                                "options");
                        goto out;
                }
                if (!_private->export_statfs)
                        gf_log (this->name, GF_LOG_DEBUG,
                                "'statfs()' returns dummy size");
        }

        _private->background_unlink = 0;
        tmp_data = dict_get (this->options, "background-unlink");
        if (tmp_data) {
                if (gf_string2boolean (tmp_data->data,
                                       &_private->background_unlink) == -1) {
                        ret = -1;
                        gf_log (this->name, GF_LOG_ERROR,
                                "'background-unlink' takes only boolean "
                                "options");
                        goto out;
                }

                if (_private->background_unlink)
                        gf_log (this->name, GF_LOG_DEBUG,
                                "unlinks will be performed in background");
        }

        tmp_data = dict_get (this->options, "o-direct");
        if (tmp_data) {
                if (gf_string2boolean (tmp_data->data,
                                       &_private->o_direct) == -1) {
                        ret = -1;
                        gf_log (this->name, GF_LOG_ERROR,
                                "wrong option provided for 'o-direct'");
                        goto out;
                }
                if (_private->o_direct)
                        gf_log (this->name, GF_LOG_DEBUG,
                                "o-direct mode is enabled (O_DIRECT "
                                "for every open)");
        }

        tmp_data = dict_get (this->options, "update-link-count-parent");
        if (tmp_data) {
                if (gf_string2boolean (tmp_data->data,
                                       &_private->update_pgfid_nlinks) == -1) {
                        ret = -1;
                        gf_log (this->name, GF_LOG_ERROR,
                                "wrong value provided for "
                                "'update-link-count-parent'");
                        goto out;
                }
                if (_private->update_pgfid_nlinks)
                        gf_log (this->name, GF_LOG_DEBUG,
                                "update-link-count-parent is enabled. Thus for each "
                                "file an extended attribute representing the "
                                "number of hardlinks for that file within the "
                                "same parent directory is set.");
        }

        ret = dict_get_str (this->options, "glusterd-uuid", &guuid);
        if (!ret) {
                if (uuid_parse (guuid, _private->glusterd_uuid))
                        gf_log (this->name, GF_LOG_WARNING, "Cannot parse "
                                "glusterd (node) UUID, node-uuid xattr "
                                "request would return - \"No such attribute\"");
        } else {
                gf_log (this->name, GF_LOG_DEBUG, "No glusterd (node) UUID "
                        "passed - node-uuid xattr request will return "
                        "\"No such attribute\"");
        }
        ret = 0;

        _private->janitor_sleep_duration = 600;

        dict_ret = dict_get_int32 (this->options, "janitor-sleep-duration",
                                   &janitor_sleep);
        if (dict_ret == 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Setting janitor sleep duration to %d.",
                        janitor_sleep);

                _private->janitor_sleep_duration = janitor_sleep;
        }
        /* performing open dir on brick dir locks the brick dir
         * and prevents it from being unmounted
         */
        _private->mount_lock = opendir (dir_data->data);
        if (!_private->mount_lock) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "Could not lock brick directory");
                goto out;
        }
#ifndef GF_DARWIN_HOST_OS
        {
                struct rlimit lim;
                lim.rlim_cur = 1048576;
                lim.rlim_max = 1048576;

                if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Failed to set 'ulimit -n "
                                " 1048576': %s", strerror(errno));
                        lim.rlim_cur = 65536;
                        lim.rlim_max = 65536;

                        if (setrlimit (RLIMIT_NOFILE, &lim) == -1) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "Failed to set maximum allowed open "
                                        "file descriptors to 64k: %s",
                                        strerror(errno));
                        }
                        else {
                                gf_log (this->name, GF_LOG_INFO,
                                        "Maximum allowed open file descriptors "
                                        "set to 65536");
                        }
                }
        }
#endif
        this->private = (void *)_private;

        op_ret = posix_handle_init (this);
        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Posix handle setup failed");
                ret = -1;
                goto out;
        }

        op_ret = posix_handle_trash_init (this);
        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Posix landfill setup failed");
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
			gf_log (this->name, GF_LOG_ERROR,
				"Posix AIO init failed");
			ret = -1;
			goto out;
		}
	}

        GF_OPTION_INIT ("node-uuid-pathinfo",
                        _private->node_uuid_pathinfo, bool, out);
        if (_private->node_uuid_pathinfo &&
            (uuid_is_null (_private->glusterd_uuid))) {
                        gf_log (this->name, GF_LOG_INFO,
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
		gf_log (this->name, GF_LOG_ERROR, "fsyncer thread"
			" creation failed (%s)", strerror (errno));
		goto out;
	}

	GF_OPTION_INIT ("batch-fsync-mode", batch_fsync_mode_str, str, out);

	if (set_batch_fsync_mode (_private, batch_fsync_mode_str) != 0) {
		gf_log (this->name, GF_LOG_ERROR, "Unknown mode string: %s",
			batch_fsync_mode_str);
		goto out;
	}

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
                closedir (priv->mount_lock);
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
          .type = GF_OPTION_TYPE_INT },
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
          .default_value = "false",
          .description = "Enable placeholders for gfid to path conversion"
        },
        { .key  = {NULL} }
};
