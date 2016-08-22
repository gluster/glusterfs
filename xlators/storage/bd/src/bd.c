/*
  BD translator V2 - Exports Block devices on server side as regular
  files to client

  Now only exporting Logical volumes supported.

  Copyright IBM, Corp. 2013

  This file is part of GlusterFS.

  Author:
  M. Mohan Kumar <mohan@in.ibm.com>

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <lvm2app.h>
#include <openssl/md5.h>
#include <time.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#ifdef HAVE_LIBAIO
#include <libaio.h>
#endif

#include "bd.h"
#include "bd-aio.h"
#include "bd-mem-types.h"
#include "defaults.h"
#include "glusterfs3-xdr.h"
#include "run.h"
#include "protocol-common.h"
#include "checksum.h"
#include "syscall.h"
#include "lvm-defaults.h"

/*
 * Call back function for setxattr and removexattr.
 * does not do anything. FIXME: How to handle remove/setxattr failure
 */
int
bd_null_rmsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, dict_t *xdata)
{
        STACK_DESTROY (frame->root);
        return 0;
}

/*
 * returns 0 if a file is mapped to BD or not.
 */
int
bd_get_bd_info (call_frame_t *frame, xlator_t *this, dict_t *xattr, uuid_t gfid,
                char **type, uint64_t *size)
{
        char         *bd_xattr = NULL;
        char         *bd       = NULL;
        int           ret      = -1;
        loc_t         loc      = {0, };
        dict_t       *dict     = NULL;
        char         *p        = NULL;
        call_frame_t *bd_frame = NULL;

        if (!xattr)
                return 1;

        if (dict_get_str (xattr, BD_XATTR, &p))
                return 1;

        bd_xattr = gf_strdup (p);

        memcpy (loc.gfid, gfid, sizeof (uuid_t));

        bd_frame = copy_frame (frame);
        BD_VALIDATE_MEM_ALLOC (bd_frame, ret, out);

        ret = bd_validate_bd_xattr (this,  bd_xattr, type, size, gfid);
        if (ret < 0) {/* LV does not exist */
                STACK_WIND (bd_frame, bd_null_rmsetxattr_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->removexattr, &loc,
                            BD_XATTR, NULL);

                gf_log (this->name, GF_LOG_WARNING,
                        "Mapped LV not available for posix file <gfid:%s>, "
                        "deleting mapping", uuid_utoa (gfid));
        } else if (ret == 1) {
                /* BD_XATTR size and LV size mismatch. Update BD_XATTR */
                gf_asprintf (&bd, "%s:%ld", *type, *size);

                dict = dict_new ();
                BD_VALIDATE_MEM_ALLOC (dict, ret, out);

                ret = dict_set_dynstr (dict, BD_XATTR, bd);
                if (ret)
                        goto out;

                STACK_WIND (bd_frame, bd_null_rmsetxattr_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->setxattr, &loc, dict, 0,
                            NULL);
        }

out:
        dict_del (xattr, BD_XATTR);
        GF_FREE (bd_xattr);
        GF_FREE (bd);
        return ret;
}

/*
 * bd_lookup_cbk: Call back from posix_lookup.
 */
int32_t
bd_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
               int op_errno, inode_t *inode, struct iatt *buf, dict_t *xattr,
               struct iatt *postparent)
{
        int           ret    = -1;
        bd_attr_t    *bdatt  = NULL;
        uint64_t      size   = 0;
        char         *type   = NULL;

        /* only regular files are part of BD object */
        if (op_ret < 0 || buf->ia_type != IA_IFREG)
                goto out;

        /* iatt already cached */
        if (!bd_inode_ctx_get (inode, this, &bdatt))
                goto next;

        if (bd_get_bd_info (frame, this, xattr, buf->ia_gfid, &type, &size))
                goto out;

        /* BD file, update buf */
        bdatt = GF_CALLOC (1, sizeof (bd_attr_t), gf_bd_attr);
        if (!bdatt) {
                op_errno = ENOMEM;
                goto out;
        }
        memcpy (&bdatt->iatt, buf, sizeof (struct iatt));
        bdatt->type = type;

        /* Cache LV size in inode_ctx */
        ret = bd_inode_ctx_set (inode, this, bdatt);
        if (ret < 0) {
                GF_FREE (bdatt);
                op_errno = EINVAL;
                goto out;
        }

        bdatt->iatt.ia_size = size;
        bdatt->iatt.ia_blocks = size / 512;

next:
        dict_del (xattr, GF_CONTENT_KEY);
        memcpy (buf, &bdatt->iatt, sizeof (struct iatt));

out:
        BD_STACK_UNWIND (lookup, frame, op_ret, op_errno, inode, buf,
                         xattr, postparent);
        return 0;
}

/*
 * bd_lookup: Issues posix_lookup to find out if file is mapped to BD
 * bd_lookup -> posix_lookup -> bd_lookup_cbk
*/
int32_t
bd_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xattr_req)
{
        dict_t     *bd_xattr = NULL;
        bd_attr_t  *bdatt    = NULL;
        int         op_errno = EINVAL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);
        VALIDATE_OR_GOTO (loc->path, out);
        VALIDATE_OR_GOTO (this->private, out);

        if (bd_inode_ctx_get (loc->inode, this, &bdatt) < 0) {
                if (!xattr_req) {
                        bd_xattr = dict_new ();
                        BD_VALIDATE_MEM_ALLOC (bd_xattr, op_errno, out);
                        xattr_req = bd_xattr;
                }
                if (dict_set_int8 (xattr_req, BD_XATTR, 1) < 0)
                        goto out;
        }

        STACK_WIND (frame, bd_lookup_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->lookup, loc, xattr_req);

        if (bd_xattr)
                dict_unref (bd_xattr);
        return 0;
out:
        BD_STACK_UNWIND (lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);

        return 0;
}

int
bd_forget (xlator_t *this, inode_t *inode)
{
        int          ret   = -1;
        uint64_t     ctx   = 0;
        bd_attr_t   *bdatt = NULL;

        ret = bd_inode_ctx_get (inode, this, &bdatt);
        if (!ret) {
                inode_ctx_del (inode, this, &ctx);
                GF_FREE (bdatt);
        }
        return 0;
}

int
bd_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                 int op_errno, gf_dirent_t *entries, dict_t *xdata)
{
        gf_dirent_t    *entry  = NULL;
        uint64_t        size   = 0;
        char           *type   = NULL;

        if (op_ret < 0)
                goto out;

        list_for_each_entry (entry, &entries->list, list) {
                if (entry->d_type != DT_REG)
                        continue;
                if (!bd_get_bd_info (frame, this, entry->dict,
                                     entry->d_stat.ia_gfid, &type, &size)) {
                        entry->d_stat.ia_size = size;
                        entry->d_stat.ia_blocks = size / 512;
                        GF_FREE (type);
                }
        }

out:
        BD_STACK_UNWIND (readdirp, frame, op_ret, op_errno, entries, xdata);
        return 0;
}

/*
 * bd_readdirp: In bd_readdirp_cbk if the file and BD_XATTR_SIZE is set
 * ia_size is updated with the LV(BD_XATTR_SIZE) size
 */
int32_t
bd_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t off, dict_t *dict)
{
        int          op_errno = EINVAL;
        bd_local_t  *local    = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (this->private, out);

        if (!dict) {
                local = bd_local_init (frame, this);
                BD_VALIDATE_MEM_ALLOC (local, op_errno, out);
                local->dict = dict_new ();
                BD_VALIDATE_MEM_ALLOC (local->dict, op_errno, out);
                dict = local->dict;
        }

        if (dict_set_int8 (dict, BD_XATTR, 0)) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to set key %s", BD_XATTR);
                goto out;
        }

        STACK_WIND (frame, bd_readdirp_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdirp, fd, size, off, dict);

        return 0;
out:
        BD_STACK_UNWIND (readdirp, frame, -1, op_errno, NULL, dict);
        return 0;
}

int
bd_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
             int op_errno, struct iatt *buf, dict_t *xdata)
{
        bd_local_t  *local = frame->local;
        bd_attr_t   *bdatt = NULL;

        /* only regular files are part of BD object */
        if (op_ret < 0 || buf->ia_type != IA_IFREG)
                goto out;

        BD_VALIDATE_LOCAL_OR_GOTO (local, op_errno, out);

        /* update buf with LV size */
        if (!bd_inode_ctx_get (local->inode, this, &bdatt))
                memcpy (buf, bdatt, sizeof (struct iatt));

out:
        BD_STACK_UNWIND (stat, frame, op_ret, op_errno, buf, xdata);
        return 0;
}

int
bd_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        int          op_errno = EINVAL;
        bd_local_t  *local    = NULL;
        bd_attr_t   *bdatt    = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);
        VALIDATE_OR_GOTO (loc->path, out);
        VALIDATE_OR_GOTO (this->private, out);

        if (!bd_inode_ctx_get (loc->inode, this, &bdatt)) {
                BD_STACK_UNWIND (stat, frame, 0, 0, &bdatt->iatt, xdata);
                return 0;
        }

        local = bd_local_init (frame, this);
        BD_VALIDATE_MEM_ALLOC (local, op_errno, out);
        local->inode = inode_ref (loc->inode);

        STACK_WIND(frame, bd_stat_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->stat, loc, xdata);
        return 0;
out:
        BD_STACK_UNWIND (stat, frame, -1, op_errno, NULL, xdata);
        return 0;
}

int
bd_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
               int op_errno, struct statvfs *buff, dict_t *xdata)
{
        uint64_t      size    = 0;
        uint64_t      fr_size = 0;
        bd_priv_t    *priv    = NULL;
        vg_t          vg      = NULL;

        if (op_ret < 0)
                goto out;

        priv = this->private;

        vg = lvm_vg_open (priv->handle, priv->vg, "r", 0);
        if (!vg) {
                gf_log (this->name, GF_LOG_WARNING, "opening VG %s failed",
                        priv->vg);
                op_ret = -1;
                op_errno = EAGAIN;
                goto out;
        }
        size = lvm_vg_get_size (vg);
        fr_size = lvm_vg_get_free_size (vg);
        lvm_vg_close (vg);

        buff->f_blocks += size / buff->f_frsize;
        buff->f_bfree += fr_size / buff->f_frsize;
        buff->f_bavail += fr_size / buff->f_frsize;

out:
        BD_STACK_UNWIND (statfs, frame, op_ret, op_errno, buff, xdata);
        return 0;
}

/*
 * bd_statfs: Mimics statfs by returning used/free extents in the VG
 */
int
bd_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (loc, out);

        STACK_WIND (frame, bd_statfs_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->statfs, loc, xdata);
        return 0;
out:
        BD_STACK_UNWIND (statfs, frame, -1, EINVAL, NULL, NULL);
        return 0;
}

int
bd_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
              int op_errno, struct iatt *buf, dict_t *xdata)
{
        bd_attr_t  *bdatt = NULL;
        bd_local_t *local = frame->local;

        /* only regular files are part of BD object */
        if (op_ret < 0 || buf->ia_type != IA_IFREG)
                goto out;

        BD_VALIDATE_LOCAL_OR_GOTO (local, op_errno, out);

        /* update buf with LV size */
        if (!bd_inode_ctx_get (local->inode, this, &bdatt))
                memcpy (buf, &bdatt->iatt, sizeof (struct iatt));

out:
        BD_STACK_UNWIND (fstat, frame, op_ret, op_errno, buf, xdata);
        return 0;
}

int
bd_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        int          op_errno = EINVAL;
        bd_local_t  *local    = NULL;
        bd_attr_t   *bdatt    = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (this->private, out);

        /* if its already cached return it */
        if (!bd_inode_ctx_get (fd->inode, this, &bdatt)) {
                BD_STACK_UNWIND (fstat, frame, 0, 0, &bdatt->iatt, xdata);
                return 0;
        }

        local = bd_local_init (frame, this);
        BD_VALIDATE_MEM_ALLOC (local, op_errno, out);

        local->inode = inode_ref (fd->inode);

        STACK_WIND (frame, bd_fstat_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->fstat, fd, xdata);

        return 0;
out:
        BD_STACK_UNWIND (fstat, frame, -1, op_errno, NULL, xdata);
        return 0;
}

/*
 * bd_readv: If posix file, invokes posix_readv otherwise reads from the BD
 * file
 */
int
bd_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset, uint32_t flags, dict_t *xdata)
{
        int             ret        = -1;
        int             _fd        = -1;
        int32_t         op_ret     = -1;
        int32_t         op_errno   = 0;
        bd_fd_t        *bd_fd      = NULL;
        struct iovec    vec        = {0, };
        struct iobuf   *iobuf      = NULL;
        struct iobref  *iobref     = NULL;
        uint64_t        bd_size    = 0;
        bd_attr_t      *bdatt      = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (this->private, out);

        ret = bd_fd_ctx_get (this, fd, &bd_fd);
        if (ret < 0 || !bd_fd) {
                STACK_WIND (frame, default_readv_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readv,
                            fd, size, offset, flags, xdata);
                return 0;
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
        _fd = bd_fd->fd;
        op_ret = sys_pread (_fd, iobuf->ptr, size, offset);
        if (op_ret == -1) {
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR,
                                "read failed on fd=%p: %s", fd,
                                strerror (op_errno));
                goto out;
        }

        vec.iov_base = iobuf->ptr;
        vec.iov_len = op_ret;

        iobref = iobref_new ();
        iobref_add (iobref, iobuf);

        if (bd_inode_ctx_get (fd->inode, this, &bdatt)) {
                op_errno = EINVAL;
                op_ret = -1;
                goto out;
        }
        bd_size = bdatt->iatt.ia_size;
        if (!bd_size || (offset + vec.iov_len) >= bd_size)
                op_errno = ENOENT;

        op_ret = vec.iov_len;
        bd_update_amtime (&bdatt->iatt, GF_SET_ATTR_ATIME);

out:
        BD_STACK_UNWIND (readv, frame, op_ret, op_errno,
                         &vec, 1, &bdatt->iatt, iobref, NULL);

        if (iobref)
                iobref_unref (iobref);
        if (iobuf)
                iobuf_unref (iobuf);

        return 0;
}

#ifdef BLKDISCARD
/*
 * bd_discard: Sends BLKDISCARD ioctl to the block device
 */
int
bd_discard (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            size_t len, dict_t *xdata)
{
        int           ret      = -1;
        int           op_errno = EINVAL;
        bd_fd_t      *bd_fd    = NULL;
        uint64_t      param[2] = {0, };
        bd_attr_t    *bdatt    = NULL;
        struct iatt   prebuf   = {0, };

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (fd, out);

        /* posix */
        if (bd_inode_ctx_get (fd->inode, this, &bdatt)) {
                STACK_WIND (frame, default_discard_cbk, FIRST_CHILD(this),
                           FIRST_CHILD(this)->fops->discard,
                           fd, offset, len, xdata);
                return 0;
        }

        ret = bd_fd_ctx_get (this, fd, &bd_fd);
        if (ret < 0 || !bd_fd) {
                op_errno = EINVAL;
                goto out;
        }

        param[0] = offset;
        param[1] = len;
        ret = ioctl (bd_fd->fd, BLKDISCARD, param);
        if (ret < 0) {
                if (errno == ENOTTY)
                        op_errno = ENOSYS;
                else
                        op_errno = errno;
                goto out;
        }
        memcpy (&prebuf, &bdatt->iatt, sizeof (prebuf));
        bd_update_amtime (&bdatt->iatt, GF_SET_ATTR_MTIME);

        BD_STACK_UNWIND (discard, frame, ret, op_errno, &prebuf,
                         &bdatt->iatt, xdata);
        return 0;

out:
        BD_STACK_UNWIND (discard, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}
#else

int
bd_discard (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            size_t len, dict_t *xdata)
{
        BD_STACK_UNWIND (discard, frame, -1, ENOSYS, NULL, NULL, NULL);
        return 0;
}
#endif

/*
 * Call back from posix_open for opening the backing posix file
 * If it failed, close BD fd
 */
int
bd_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int op_ret, int op_errno, fd_t *fd, dict_t *xdata)
{
        bd_fd_t    *bd_fd = NULL;
        bd_attr_t  *bdatt = NULL;

        if (!op_ret)
                goto out;

        bd_inode_ctx_get (fd->inode, this, &bdatt);
        if (!bdatt) /* posix file */
                goto out;

        /* posix open failed */
        if (bd_fd_ctx_get (this, fd, &bd_fd) < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "bd_fd is NULL from fd=%p", fd);
                goto out;
        }
        sys_close (bd_fd->fd);
        GF_FREE (bd_fd);

out:
        BD_STACK_UNWIND (open, frame, op_ret, op_errno, fd, NULL);

        return 0;
}

/*
 * bd_open: Opens BD file if given posix file is mapped to BD. Also opens
 * posix file.
 * fd contains both posix and BD fd
 */
int32_t
bd_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, dict_t *xdata)
{
        int32_t      ret     = EINVAL;
        bd_fd_t     *bd_fd   = NULL;
        bd_attr_t   *bdatt   = NULL;
        bd_gfid_t    gfid    = {0, };
        char        *devpath = NULL;
        bd_priv_t   *priv    = this->private;
        int         _fd      = -1;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (loc, out);
        VALIDATE_OR_GOTO (fd, out);

        /* not bd file */
        if (fd->inode->ia_type != IA_IFREG ||
            bd_inode_ctx_get (fd->inode, this, &bdatt))
                goto posix;

        uuid_utoa_r (fd->inode->gfid, gfid);
        gf_asprintf (&devpath, "/dev/%s/%s", priv->vg, gfid);
        BD_VALIDATE_MEM_ALLOC (devpath, ret, out);

        _fd = open (devpath, flags | O_LARGEFILE, 0);
        if (_fd < 0) {
                ret = errno;
                gf_log (this->name, GF_LOG_ERROR, "open on %s: %s", devpath,
                        strerror (ret));
                goto out;
        }
        bd_fd = GF_CALLOC (1, sizeof(bd_fd_t), gf_bd_fd);
        BD_VALIDATE_MEM_ALLOC (bd_fd, ret, out);

        bd_fd->fd = _fd;
        bd_fd->flag = flags | O_LARGEFILE;

        if (fd_ctx_set (fd, this, (uint64_t)(long)bd_fd) < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "failed to set the fd context fd=%p", fd);
                goto out;
        }

        ret = 0;

posix:

        /* open posix equivalant of this file, fd needed for fd related
           operations like fsetxattr, ftruncate etc */
        STACK_WIND (frame, bd_open_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open, loc, flags, fd, xdata);

        return 0;
out:
        BD_STACK_UNWIND (open, frame, -1, ret, fd, NULL);

        GF_FREE (devpath);
        if (ret) {
                if (_fd >= 0)
                        sys_close (_fd);
                GF_FREE (bd_fd);
        }

        return 0;
}

/*
 * call back from posix_setattr after updating iatt to posix file.
 */
int
bd_fsync_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, struct iatt *pre,
                      struct iatt *post, dict_t *xdata)
{
        bd_local_t *local = frame->local;
        bd_attr_t  *bdatt = local->bdatt;

        BD_STACK_UNWIND (fsync, frame, op_ret, op_errno, &bdatt->iatt,
                         &bdatt->iatt, NULL);
        return 0;
}

int
bd_do_fsync (int fd, int datasync)
{
        int   op_errno = 0;

        if (datasync) {
                if (sys_fdatasync (fd)) {
                        op_errno = errno;
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "fdatasync on fd=%d failed: %s",
                                fd, strerror (errno));
                }

        } else

        {
                if (sys_fsync (fd)) {
                        op_errno = errno;
                        gf_log (THIS->name, GF_LOG_ERROR,
                                "fsync on fd=%d failed: %s",
                                 fd, strerror (op_errno));
                }
        }

        return op_errno;
}

/*
 * bd_fsync: Syncs if BD fd, forwards the request to posix
 * fsync -> posix_setattr -> posix_fsync
*/
int32_t
bd_fsync (call_frame_t *frame, xlator_t *this,
          fd_t *fd, int32_t datasync, dict_t *xdata)
{
        int         ret      = -1;
        int32_t     op_ret   = -1;
        int32_t     op_errno = 0;
        bd_fd_t    *bd_fd    = NULL;
        bd_attr_t  *bdatt    = NULL;
        bd_local_t *local    = NULL;
        int         valid    = GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME;
        struct iatt prebuf   = {0, };

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (this->private, out);

        ret = bd_inode_ctx_get (fd->inode, this, &bdatt);
        ret = bd_fd_ctx_get (this, fd, &bd_fd);
        if (ret < 0 || !bd_fd || !bdatt) {
                STACK_WIND (frame, default_fsync_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fsync, fd, datasync,
                            xdata);
                return 0;
        }

        memcpy (&prebuf, &bdatt->iatt, sizeof (struct iatt));

        op_errno = bd_do_fsync (bd_fd->fd, datasync);
        if (op_errno)
                goto out;

        /* For BD, Update the a|mtime during full fsync only */
        if (!datasync) {
                local = bd_local_init (frame, this);
                /* In case of mem failure, should posix flush called ? */
                BD_VALIDATE_MEM_ALLOC (local, op_errno, out);

                local->bdatt = GF_CALLOC (1, sizeof (bd_attr_t), gf_bd_attr);
                BD_VALIDATE_MEM_ALLOC (local->bdatt, op_errno, out);

                local->bdatt->type = gf_strdup (bdatt->type);
                memcpy (&local->bdatt->iatt, &bdatt->iatt, sizeof (struct iatt));
                bd_update_amtime (&local->bdatt->iatt, valid);
                gf_uuid_copy (local->loc.gfid, fd->inode->gfid);
                STACK_WIND (frame, bd_fsync_setattr_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->setattr, &local->loc,
                            &local->bdatt->iatt,
                            valid, NULL);
                return 0;
        }

out:
        BD_STACK_UNWIND (fsync, frame, op_ret, op_errno, &prebuf,
                         &bdatt->iatt, NULL);
        return 0;
}

int
bd_flush_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, struct iatt *pre,
                      struct iatt *post, dict_t *xdata)
{
        BD_STACK_UNWIND (flush, frame, op_ret, op_errno, xdata);
        return 0;
}

int
bd_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        int          ret    = -1;
        bd_fd_t     *bd_fd  = NULL;
        bd_attr_t   *bdatt  = NULL;
        int          valid    = GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME;
        bd_local_t  *local    = NULL;
        loc_t        loc      = {0, };

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (this->private, out);

        ret = bd_inode_ctx_get (fd->inode, this, &bdatt);
        if (!bdatt)
                goto out;

        ret = bd_fd_ctx_get (this, fd, &bd_fd);
        if (ret < 0 || !bd_fd || !bdatt) {
                gf_log (this->name, GF_LOG_WARNING,
                        "bdfd/bdatt is NULL from fd=%p", fd);
                goto out;
        }

        local = bd_local_init (frame, this);
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto out;
        }

        local->fd = fd_ref (fd);
        gf_uuid_copy (loc.gfid, bdatt->iatt.ia_gfid);

        /* Update the a|mtime during flush */
        STACK_WIND (frame, bd_flush_setattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setattr, &loc, &bdatt->iatt,
                    valid, NULL);

        return 0;

out:
        STACK_WIND (frame, default_flush_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->flush, fd, xdata);

        return 0;
}

int32_t
bd_release (xlator_t *this, fd_t *fd)
{
        int          ret      = -1;
        bd_fd_t     *bd_fd    = NULL;
        uint64_t     tmp_bfd  = 0;
        bd_attr_t   *bdatt    = NULL;
        bd_priv_t   *priv     = this->private;

        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (priv, out);

        ret = bd_inode_ctx_get (fd->inode, this, &bdatt);
        if (ret || !bdatt) /* posix file */
                goto out;

        /* FIXME: Update amtime during release */

        ret = fd_ctx_del (fd, this, &tmp_bfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "bfd is NULL from fd=%p", fd);
                goto out;
        }
        bd_fd = (bd_fd_t *)(long)tmp_bfd;

        sys_close (bd_fd->fd);
        GF_FREE (bd_fd);
out:
        return 0;
}

/*
 * Call back for removexattr after removing BD_XATTR incase of
 * bd create failure
 */
int
bd_setx_rm_xattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, dict_t *xdata)
{
        bd_local_t *local = frame->local;

        if (local->fd)
                BD_STACK_UNWIND (setxattr, frame, -1, EIO, xdata);
        else
                BD_STACK_UNWIND (setxattr, frame, -1, EIO, xdata);
        return 0;

}

/*
 * Call back after setting BD_XATTR. Creates BD. If BD creation is a failure
 * invokes posix_removexattr to remove created BD_XATTR
 */
int
bd_setx_setx_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno, dict_t *xdata)
{
        bd_local_t *local = frame->local;
        bd_attr_t  *bdatt = NULL;

        if (op_ret < 0)
                goto next;

        /* Create LV */
        op_errno = bd_create (local->inode->gfid, local->bdatt->iatt.ia_size,
                              local->bdatt->type, this->private);
        if (!op_errno)
                goto out;

        /* LV creation failed, remove BD_XATTR */
        if (local->fd)
                STACK_WIND (frame, bd_setx_rm_xattr_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fremovexattr,
                            local->fd, BD_XATTR, NULL);
        else
                STACK_WIND (frame, bd_setx_rm_xattr_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->removexattr,
                            &local->loc, BD_XATTR, NULL);

        return 0;
out:

        bdatt = GF_CALLOC (1, sizeof (bd_attr_t), gf_bd_attr);
        if (!bdatt) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto next;
        }

        memcpy (&bdatt->iatt, &local->bdatt->iatt, sizeof (struct iatt));
        bdatt->type = gf_strdup (local->bdatt->type);

        bd_inode_ctx_set (local->inode, THIS, bdatt);

next:
        if (local->fd)
                BD_STACK_UNWIND (setxattr, frame, op_ret, op_errno, xdata);
        else
                BD_STACK_UNWIND (setxattr, frame, op_ret, op_errno, xdata);
        return 0;

}

/*
 * Call back from posix_stat
 */
int
bd_setx_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno, struct iatt *iatt,
                  dict_t *xdata)
{
        char       *param  = NULL;
        char       *type   = NULL;
        char       *s_size = NULL;
        char       *p      = NULL;
        char       *copy   = NULL;
        bd_local_t *local  = frame->local;
        bd_priv_t  *priv   = this->private;
        char       *bd     = NULL;
        uint64_t    size   = 0;

        if (op_ret < 0)
                goto out;

        if (!IA_ISREG (iatt->ia_type)) {
                op_errno = EOPNOTSUPP;
                goto out;
        }

        param = copy = GF_CALLOC (1, local->data->len + 1, gf_common_mt_char);
        BD_VALIDATE_MEM_ALLOC (param, op_errno, out);

        strncpy (param, local->data->data, local->data->len);

        type = strtok_r (param, ":", &p);
        if (!type) {
                op_errno = EINVAL;
                goto out;
        }

        if (strcmp (type, BD_LV) && strcmp (type, BD_THIN)) {
                gf_log (this->name, GF_LOG_WARNING, "Invalid bd type %s given",
                        type);
                op_errno = EINVAL;
                goto out;
        }

        if (!strcmp (type, BD_THIN) && !(priv->caps & BD_CAPS_THIN)) {
                gf_log (this->name, GF_LOG_WARNING, "THIN lv not supported by "
                        "this volume");
                op_errno = EOPNOTSUPP;
                goto out;
        }

        s_size = strtok_r (NULL, ":", &p);

        /* If size not specified get default size */
        if (!s_size)
                size = bd_get_default_extent (priv);
        else
                gf_string2bytesize (s_size, &size);

        gf_asprintf (&bd, "%s:%ld", type, size);
        BD_VALIDATE_MEM_ALLOC (bd, op_errno, out);

        local->dict = dict_new ();
        BD_VALIDATE_MEM_ALLOC (local->dict, op_errno, out);

        local->bdatt = GF_CALLOC (1, sizeof (bd_attr_t), gf_bd_attr);
        BD_VALIDATE_MEM_ALLOC (local->bdatt, op_errno, out);

        if (dict_set_dynstr (local->dict, BD_XATTR, bd) < 0) {
                op_errno = EINVAL;
                goto out;
        }

        local->bdatt->type = gf_strdup (type);
        memcpy (&local->bdatt->iatt, iatt, sizeof (struct iatt));
        local->bdatt->iatt.ia_size = size;

        if (local->fd)
                STACK_WIND (frame, bd_setx_setx_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsetxattr,
                            local->fd, local->dict, 0, NULL);
        else
                STACK_WIND (frame, bd_setx_setx_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->setxattr,
                            &local->loc, local->dict, 0, NULL);

        return 0;

out:
        if (local->fd)
                BD_STACK_UNWIND (fsetxattr, frame, -1, op_errno, xdata);
        else
                BD_STACK_UNWIND (setxattr, frame, -1, op_errno, xdata);

        GF_FREE (bd);
        GF_FREE (copy);
        return 0;
}

int
bd_offload_rm_xattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, dict_t *xdata)
{
        bd_local_t *local = frame->local;

        if (local->fd)
                BD_STACK_UNWIND (fsetxattr, frame, -1, EIO, NULL);
        else
                BD_STACK_UNWIND (setxattr, frame, -1, EIO, NULL);

        return 0;
}

int
bd_offload_setx_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, dict_t *xdata)
{
        bd_local_t *local = frame->local;

        if (op_ret < 0)
                goto out;

        if (local->offload == BD_OF_SNAPSHOT)
                op_ret = bd_snapshot_create (frame->local, this->private);
        else
                op_ret = bd_clone (frame->local, this->private);

        if (op_ret) {
                STACK_WIND (frame, bd_offload_rm_xattr_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->removexattr,
                            local->dloc, BD_XATTR, NULL);
                return 0;
        }

out:
        if (local->fd)
                BD_STACK_UNWIND (fsetxattr, frame, op_ret, op_errno, NULL);
        else
                BD_STACK_UNWIND (setxattr, frame, op_ret, op_errno, NULL);

        return 0;
}

int
bd_offload_getx_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, dict_t *xattr, dict_t *xdata)
{
        char       *bd    = NULL;
        bd_local_t *local = frame->local;
        char       *type  = NULL;
        char       *p     = NULL;

        if (op_ret < 0)
                goto out;

        if (dict_get_str (xattr, BD_XATTR, &p)) {
                op_errno = EINVAL;
                goto out;
        }

        type = gf_strdup (p);
        BD_VALIDATE_MEM_ALLOC (type, op_errno, out);

        p = strrchr (type, ':');
        if (!p) {
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_WARNING,
                        "source file xattr %s corrupted?", type);
                goto out;
        }

        *p='\0';

        /* For clone size is taken from source LV */
        if (!local->size) {
                p++;
                gf_string2bytesize (p, &local->size);
        }
        gf_asprintf (&bd, "%s:%ld", type, local->size);
        local->bdatt->type = gf_strdup (type);
        dict_del (local->dict, BD_XATTR);
        dict_del (local->dict, LINKTO);
        if (dict_set_dynstr (local->dict, BD_XATTR, bd)) {
                op_errno = EINVAL;
                goto out;
        }

        STACK_WIND (frame, bd_offload_setx_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr,
                    local->dloc, local->dict, 0, NULL);

        return 0;

out:
        if (local->fd)
                BD_STACK_UNWIND (fsetxattr, frame, -1, op_errno, NULL);
        else
                BD_STACK_UNWIND (setxattr, frame, -1, op_errno, NULL);

        GF_FREE (type);
        GF_FREE (bd);

        return 0;
}

int
bd_offload_dest_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int op_ret, int op_errno,
                            inode_t *inode, struct iatt *iatt,
                            dict_t *xattr, struct iatt *postparent)
{
        bd_local_t *local  = frame->local;
        char       *bd     = NULL;
        char       *linkto = NULL;
        int         ret    = -1;

        if (op_ret < 0 && op_errno != ENODATA) {
                op_errno = EINVAL;
                goto out;
        }

        if (!IA_ISREG (iatt->ia_type)) {
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_WARNING, "destination gfid is not a "
                        "regular file");
                goto out;
        }

        ret = dict_get_str (xattr, LINKTO, &linkto);
        if (linkto) {
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_WARNING, "destination file not "
                        "present in same brick");
                goto out;
        }

        ret = dict_get_str (xattr, BD_XATTR, &bd);
        if (bd) {
                op_errno = EEXIST;
                goto out;
        }

        local->bdatt = GF_CALLOC (1, sizeof (bd_attr_t), gf_bd_attr);
        BD_VALIDATE_MEM_ALLOC (local->bdatt, op_errno, out);

        STACK_WIND (frame, bd_offload_getx_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr,
                    &local->loc, BD_XATTR, NULL);

        return 0;
out:
        if (local->fd)
                BD_STACK_UNWIND (fsetxattr, frame, -1, op_errno, NULL);
        else
                BD_STACK_UNWIND (setxattr, frame, -1, op_errno, NULL);

        return (ret == 0) ? 0 : ret;
}

int
bd_merge_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata)
{
        /* FIXME: if delete failed, remove xattr */

        BD_STACK_UNWIND (setxattr, frame, op_ret, op_errno, NULL);
        return 0;
}

int
bd_do_merge(call_frame_t *frame, xlator_t *this)
{
        bd_local_t *local    = frame->local;
        inode_t    *parent   = NULL;
        char       *p        = NULL;
        int         op_errno = 0;

        op_errno = bd_merge (this->private, local->inode->gfid);
        if (op_errno)
                goto out;

        /*
         * posix_unlink needs loc->pargfid to be valid, but setxattr FOP does
         * not have loc->pargfid set. Get parent's gfid by getting parents inode
         */
        parent = inode_parent (local->inode, NULL, NULL);
        if (!parent) {
                /*
                 * FIXME: Snapshot LV already deleted.
                 * remove xattr, instead of returning failure
                 */
                op_errno = EINVAL;
                goto out;
        }
        gf_uuid_copy (local->loc.pargfid, parent->gfid);

        p = strrchr (local->loc.path, '/');
        if (p)
                p++;
        local->loc.name = p;

        STACK_WIND (frame, bd_merge_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink,
                    &local->loc, 0, NULL);

        return 0;
out:
        BD_STACK_UNWIND (fsetxattr, frame, -1, op_errno, NULL);

        return op_errno;
}

int
bd_offload (call_frame_t *frame, xlator_t *this, loc_t *loc,
            fd_t *fd, bd_offload_t offload)
{
        char       *param      = NULL;
        char       *param_copy = NULL;
        char       *p          = NULL;
        char       *size       = NULL;
        char       *gfid       = NULL;
        int         op_errno   = 0;
        bd_local_t *local      = frame->local;

        param = GF_CALLOC (1, local->data->len + 1, gf_common_mt_char);
        BD_VALIDATE_MEM_ALLOC (param, op_errno, out);
        param_copy = param;

        local->dict = dict_new ();
        BD_VALIDATE_MEM_ALLOC (local->dict, op_errno, out);

        local->dloc = GF_CALLOC (1, sizeof (loc_t), gf_bd_loc_t);
        BD_VALIDATE_MEM_ALLOC (local->dloc, op_errno, out);

        strncpy (param, local->data->data, local->data->len);

        gfid = strtok_r (param, ":", &p);
        size = strtok_r (NULL, ":", &p);
        if (size)
                gf_string2bytesize (size, &local->size);
        else if (offload != BD_OF_CLONE)
                local->size = bd_get_default_extent (this->private);

        if (dict_set_int8 (local->dict, BD_XATTR, 1) < 0) {
                op_errno = EINVAL;
                goto out;
        }
        if (dict_set_int8 (local->dict, LINKTO, 1) < 0) {
                op_errno = EINVAL;
                goto out;
        }

        gf_uuid_parse (gfid, local->dloc->gfid);
        local->offload = offload;

        STACK_WIND (frame, bd_offload_dest_lookup_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->lookup, local->dloc,
                    local->dict);

        return 0;

out:
        if (fd)
                BD_STACK_UNWIND (fsetxattr, frame, -1, op_errno, NULL);
        else
                BD_STACK_UNWIND (setxattr, frame, -1, op_errno, NULL);

        GF_FREE (param_copy);
        return 0;
}

/*
 * bd_setxattr: Used to create & map an LV to a posix file using
 * BD_XATTR xattr
 * bd_setxattr -> posix_stat -> bd_setx_stat_cbk -> posix_setxattr ->
 * bd_setx_setx_cbk -> create_lv
 * if create_lv failed, posix_removexattr -> bd_setx_rm_xattr_cbk
 */
int
bd_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
             int flags, dict_t *xdata)
{
        int           op_errno = 0;
        data_t       *data     = NULL;
        bd_local_t   *local    = NULL;
        bd_attr_t    *bdatt    = NULL;
        bd_offload_t  cl_type  = BD_OF_NONE;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);

        if ((data = dict_get (dict, BD_XATTR)))
                cl_type = BD_OF_NONE;
        else if ((data = dict_get (dict, BD_CLONE)))
                cl_type = BD_OF_CLONE;
        else if ((data = dict_get (dict, BD_SNAPSHOT)))
                cl_type = BD_OF_SNAPSHOT;
        else if ((data = dict_get (dict, BD_MERGE)))
                cl_type = BD_OF_MERGE;

        bd_inode_ctx_get (loc->inode, this, &bdatt);
        if (!cl_type && !data) {
                STACK_WIND (frame, default_setxattr_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->setxattr, loc, dict,
                            flags, xdata);
                return 0;
        }

        local = bd_local_init (frame, this);
        BD_VALIDATE_MEM_ALLOC (local, op_errno, out);

        local->data = data;
        loc_copy (&local->loc, loc);
        local->inode = inode_ref (loc->inode);

        if (cl_type) {
                /* For cloning/snapshot, source file must be mapped to LV */
                if (!bdatt) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s not mapped to BD", loc->path);
                        op_errno = EINVAL;
                        goto out;
                }
                if (cl_type == BD_OF_MERGE)
                        bd_do_merge (frame, this);
                else
                        bd_offload (frame, this, loc, NULL, cl_type);
        } else if (data) {
                if (bdatt) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s already mapped to BD", loc->path);
                        op_errno = EEXIST;
                        goto out;
                }
                STACK_WIND (frame, bd_setx_stat_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->stat, loc, xdata);
        }

        return 0;
out:
        if (op_errno)
                STACK_UNWIND_STRICT (setxattr, frame, -1, op_errno, xdata);

        return 0;
}

/*
 * bd_fsetxattr: Used to create/map an LV to a posix file using
 * BD_XATTR xattr
 * bd_fsetxattr ->  posix_fstat -> bd_setx_stat_cbk -> posix_fsetxattr ->
 * bd_setx_setx_cbk -> create_lv
 * if create_lv failed, posix_removexattr -> bd_setx_rm_xattr_cbk
 * -> bd_fsetxattr_cbk
 */
int32_t
bd_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
              int flags, dict_t *xdata)
{
        int           op_errno = 0;
        data_t       *data     = NULL;
        bd_attr_t    *bdatt    = NULL;
        bd_local_t   *local    = NULL;
        bd_offload_t  cl_type  = BD_OF_NONE;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (fd, out);

        bd_inode_ctx_get (fd->inode, this, &bdatt);

        if ((data = dict_get (dict, BD_XATTR)))
                cl_type = BD_OF_NONE;
        else if ((data = dict_get (dict, BD_CLONE)))
                cl_type = BD_OF_CLONE;
        else if ((data = dict_get (dict, BD_SNAPSHOT)))
                cl_type = BD_OF_SNAPSHOT;
        else if ((data = dict_get (dict, BD_MERGE))) {
                /*
                 * bd_merge is not supported for fsetxattr, because snapshot LV
                 * is opened and it causes problem in snapshot merge
                 */
                op_errno = EOPNOTSUPP;
                goto out;
        }

        bd_inode_ctx_get (fd->inode, this, &bdatt);

        if (!cl_type && !data) {
                /* non bd file object */
                STACK_WIND (frame, default_fsetxattr_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsetxattr,
                            fd, dict, flags, xdata);
                return 0;
        }

        local = bd_local_init (frame, this);
        BD_VALIDATE_MEM_ALLOC (local, op_errno, out);

        local->inode = inode_ref (fd->inode);
        local->fd = fd_ref (fd);
        local->data = data;

        if (cl_type) {
                /* For cloning/snapshot, source file must be mapped to LV */
                if (!bdatt) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "fd %p not mapped to BD", fd);
                        op_errno = EINVAL;
                        goto out;

                }
                bd_offload (frame, this, NULL, fd, cl_type);
        } else if (data) {
                if (bdatt) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "fd %p already mapped to BD", fd);
                        op_errno = EEXIST;
                        goto out;
                }
                STACK_WIND(frame, bd_setx_stat_cbk, FIRST_CHILD(this),
                           FIRST_CHILD(this)->fops->fstat, fd, xdata);
        }

        return 0;
out:

        BD_STACK_UNWIND (setxattr, frame, -1, op_errno, NULL);

        return 0;
}

int32_t
bd_removexattr (call_frame_t *frame, xlator_t *this,
                   loc_t *loc, const char *name, dict_t *xdata)
{
        if (!strcmp (name, BD_XATTR))
            goto out;

        STACK_WIND (frame, default_removexattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->removexattr, loc, name, xdata);
        return 0;
out:
        BD_STACK_UNWIND (removexattr, frame, -1, ENODATA, NULL);
        return 0;
}

int32_t
bd_fremovexattr (call_frame_t *frame, xlator_t *this,
                    fd_t *fd, const char *name, dict_t *xdata)
{
        if (!strcmp (name, BD_XATTR))
            goto out;

        STACK_WIND (frame, default_removexattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fremovexattr, fd, name, xdata);

        return 0;
out:
        BD_STACK_UNWIND (fremovexattr, frame, -1, ENODATA, NULL);
        return 0;
}

int
bd_trunc_setxattr_setx_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, dict_t *xdata)
{
        bd_local_t *local = frame->local;

        if (local->fd)
                BD_STACK_UNWIND (ftruncate, frame, -1, EIO, NULL, NULL, NULL);
        else
                BD_STACK_UNWIND (truncate, frame, -1, EIO, NULL, NULL, NULL);

        return 0;
}

/*
 * Call back for setxattr after setting BD_XATTR_SIZE.
 */
int
bd_trunc_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno, dict_t *xdata)
{
        bd_local_t *local = frame->local;
        bd_attr_t   *bdatt = NULL;
        struct iatt prebuf = {0, };
        char         *bd   = NULL;

        if (op_ret < 0)
                goto out;

        bd_inode_ctx_get (local->inode, this, &bdatt);
        if (!bdatt)
                goto revert_xattr;

        op_errno = bd_resize (this->private, local->inode->gfid,
                              local->bdatt->iatt.ia_size);
        if (op_errno)
                goto revert_xattr;

        memcpy (&prebuf, &bdatt->iatt, sizeof (struct iatt));
        /* LV resized, update new size in the cache */
        bdatt->iatt.ia_size = local->bdatt->iatt.ia_size;

        if (local->fd)
                BD_STACK_UNWIND (ftruncate, frame, 0, 0, &prebuf, &bdatt->iatt,
                                 NULL);
        else
                BD_STACK_UNWIND (truncate, frame, 0, 0, &prebuf, &bdatt->iatt,
                                 NULL);

        return 0;

revert_xattr:
        /* revert setxattr */
        op_ret = dict_get_str (local->dict, BD_XATTR, &bd);
        GF_FREE (bd);
	if (bdatt)
		gf_asprintf (&bd, "%s:%ld", bdatt->type, bdatt->iatt.ia_size);

        if (local->fd)
                STACK_WIND (frame, bd_trunc_setxattr_setx_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsetxattr,
                            local->fd, local->dict, 0, NULL);
        else
                STACK_WIND (frame, bd_trunc_setxattr_setx_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->setxattr,
                            &local->loc, local->dict, 0, NULL);

        return 0;
out:
        if (local->fd)
                BD_STACK_UNWIND (ftruncate, frame, -1, EIO, NULL, NULL, NULL);
        else
                BD_STACK_UNWIND (truncate, frame, -1, EIO, NULL, NULL, NULL);

        return 0;
}

/*
 * call back from posix_[f]truncate_stat
 * If offset > LV size, it resizes the LV and calls posix_setxattr
 * to update new LV size in xattr else calls posix_setattr for updating
 * the posix file so that truncate fop behaves properly
 */
int
bd_trunc_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno, struct iatt *buf, dict_t *xdata)
{
        char       *bd    = NULL;
        bd_local_t *local = frame->local;
        bd_attr_t  *bdatt = NULL;

        if (op_ret < 0)
                goto out;

        local->dict  = dict_new ();
        BD_VALIDATE_MEM_ALLOC (local->dict, op_errno, out);

        bd_inode_ctx_get (local->inode, this, &bdatt);
        if (!bdatt) {
                op_errno = EINVAL;
                goto out;
        }

        gf_asprintf (&bd, "%s:%ld", bdatt->type, local->bdatt->iatt.ia_size);
        if (dict_set_dynstr (local->dict, BD_XATTR, bd)) {
                op_errno = EINVAL;
                goto out;
        }

        if (local->fd)
                STACK_WIND (frame, bd_trunc_setxattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsetxattr,
                            local->fd, local->dict, 0, NULL);
        else
                STACK_WIND (frame, bd_trunc_setxattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->setxattr,
                            &local->loc, local->dict, 0, NULL);

        return 0;
out:
        if (local->fd)
                BD_STACK_UNWIND (ftruncate, frame, -1, op_errno, NULL, NULL,
                                 NULL);
        else
                BD_STACK_UNWIND (truncate, frame, -1, op_errno, NULL, NULL,
                                 NULL);
        GF_FREE (bd);
        return 0;
}

void
bd_do_trunc (call_frame_t *frame, xlator_t *this, fd_t *fd, loc_t *loc,
             off_t offset, bd_attr_t *bdatt)
{
        bd_local_t *local    = NULL;
        struct iatt prebuf   = {0, };
        int         op_errno = 0;
        int         op_ret   = -1;

        /* If requested size is less than LV size, return success */
        if (offset <= bdatt->iatt.ia_size) {
                memcpy (&prebuf, &bdatt->iatt, sizeof (struct iatt));
                bd_update_amtime (&bdatt->iatt, GF_SET_ATTR_MTIME);
                op_ret = 0;
                goto out;
        }

        local = bd_local_init (frame, this);
        BD_VALIDATE_MEM_ALLOC (local, op_errno, out);

        local->bdatt = GF_CALLOC (1, sizeof (bd_attr_t), gf_bd_attr);
        BD_VALIDATE_MEM_ALLOC (local->bdatt, op_errno, out);

        if (fd) {
                local->inode = inode_ref (fd->inode);
                local->fd = fd_ref (fd);
        } else {
                local->inode = inode_ref (loc->inode);
                loc_copy (&local->loc, loc);
        }

        local->bdatt->iatt.ia_size =
                bd_adjust_size (this->private, offset);

        STACK_WIND (frame, bd_trunc_stat_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->fstat, fd, NULL);

        return;

out:
        if (fd)
                BD_STACK_UNWIND (ftruncate, frame, op_ret, op_errno,
                                 &prebuf, &bdatt->iatt, NULL);
        else
                BD_STACK_UNWIND (truncate, frame, op_ret, op_errno,
                                 &prebuf, &bdatt->iatt, NULL);
        return;
}

/*
 * bd_ftruncate: Resizes a LV if fd belongs to BD.
 */
int32_t
bd_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              dict_t *xdata)
{
        int         op_errno = 0;
        bd_attr_t  *bdatt    = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        if (bd_inode_ctx_get (fd->inode, this, &bdatt)) {
                STACK_WIND (frame, default_ftruncate_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->ftruncate, fd,
                            offset, xdata);
                return 0;
        }

        bd_do_trunc (frame, this, fd, NULL, offset, bdatt);
        return 0;
out:
        BD_STACK_UNWIND (ftruncate, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

/*
 * bd_truncate: Resizes a LV if file maps to LV.
 */
int32_t
bd_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
             dict_t *xdata)
{
        int         op_errno = 0;
        bd_attr_t  *bdatt    = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        if (bd_inode_ctx_get (loc->inode, this, &bdatt)) {
                STACK_WIND (frame, default_truncate_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->truncate, loc,
                            offset, xdata);
                return 0;
        }

        bd_do_trunc (frame, this, NULL, loc, offset, bdatt);
        return 0;

out:
        BD_STACK_UNWIND (truncate, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

int32_t
__bd_pwritev (int fd, struct iovec *vector, int count, off_t offset,
              uint64_t bd_size)
{
        int        index           = 0;
        int        retval          = 0;

        if (!vector)
                return -EFAULT;

        retval = sys_pwritev (fd, vector, count, offset);
        if (retval == -1) {
                int64_t off = offset;
                gf_log (THIS->name, GF_LOG_WARNING,
                        "base %p, length %zd, offset %" PRId64 ", message %s",
                        vector[index].iov_base, vector[index].iov_len,
                        off, strerror (errno));
                retval = -errno;
                goto err;
        }
/*


        internal_offset = offset;
        for (index = 0; index < count; index++) {
                if (internal_offset > bd_size) {
                        op_ret = -ENOSPC;
                        goto err;
                }
                if (internal_offset + vector[index].iov_len > bd_size) {
                        vector[index].iov_len = bd_size - internal_offset;
                        no_space = 1;
                }
                retval = sys_pwritev (fd, vector[index].iov_base,
                                      vector[index].iov_len, internal_offset);
                if (retval == -1) {
                        gf_log (THIS->name, GF_LOG_WARNING,
                                "base %p, length %ld, offset %ld, message %s",
                                vector[index].iov_base, vector[index].iov_len,
                                internal_offset, strerror (errno));
                        op_ret = -errno;
                        goto err;
                }
                op_ret += retval;
                internal_offset += retval;
                if (no_space)
                        break;
        }
*/
err:
        return retval;
}

/*
 * bd_writev: Writes to LV if its BD file or forwards the request to posix_write
 * bd_writev -> posix_writev -> bd_writev_cbk
 */
int
bd_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t offset, uint32_t flags, struct iobref *iobref,
           dict_t *xdict)
{
        int32_t         op_ret    = -1;
        int32_t         op_errno  = 0;
        int             _fd       = -1;
        bd_fd_t         *bd_fd    = NULL;
        int             ret       = -1;
        uint64_t        size      = 0;
        struct iatt     prebuf    = {0, };
        bd_attr_t      *bdatt     = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (vector, out);

        ret = bd_fd_ctx_get (this, fd, &bd_fd);
        if (ret < 0 || !bd_fd) { /* posix fd */
                STACK_WIND (frame, default_writev_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->writev, fd, vector, count,
                            offset, flags, iobref, xdict);
                return 0;
        }

        _fd = bd_fd->fd;

        if (bd_inode_ctx_get (fd->inode, this, &bdatt)) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }
        size = bdatt->iatt.ia_size;

        op_ret = __bd_pwritev (_fd, vector, count, offset, size);
        if (op_ret < 0) {
                op_errno = -op_ret;
                op_ret = -1;
                gf_log (this->name, GF_LOG_ERROR, "write failed: offset %"PRIu64
                                ", %s", offset, strerror (op_errno));
                goto out;
        }

        memcpy (&prebuf, &bdatt->iatt, sizeof (struct iatt));
        bd_update_amtime (&bdatt->iatt, GF_SET_ATTR_MTIME);
out:

        BD_STACK_UNWIND (writev, frame, op_ret, op_errno, &prebuf,
                         &bdatt->iatt, NULL);
        return 0;
}

int
bd_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                int op_errno, struct iatt *prebuf, struct iatt *postbuf,
                dict_t *xdata)
{
        bd_attr_t   *bdatt = NULL;
        int         *valid = cookie;
        bd_local_t  *local = frame->local;

        if (op_ret < 0 || !valid || !local)
                goto out;

        if (bd_inode_ctx_get (local->inode, this, &bdatt))
                goto out;

        if (*valid & GF_SET_ATTR_UID)
                bdatt->iatt.ia_uid = postbuf->ia_uid;
        else if (*valid & GF_SET_ATTR_GID)
                bdatt->iatt.ia_gid = postbuf->ia_gid;
        else if (*valid & GF_SET_ATTR_MODE) {
                bdatt->iatt.ia_type = postbuf->ia_type;
                bdatt->iatt.ia_prot = postbuf->ia_prot;
        } else if (*valid & GF_SET_ATTR_ATIME) {
                bdatt->iatt.ia_atime = postbuf->ia_atime;
                bdatt->iatt.ia_atime_nsec = postbuf->ia_atime_nsec;
        } else if (*valid & GF_SET_ATTR_MTIME) {
                bdatt->iatt.ia_mtime = postbuf->ia_mtime;
                bdatt->iatt.ia_mtime_nsec = postbuf->ia_mtime_nsec;
        }

        bdatt->iatt.ia_ctime = postbuf->ia_ctime;
        bdatt->iatt.ia_ctime_nsec = postbuf->ia_ctime_nsec;

        memcpy (postbuf, &bdatt->iatt, sizeof (struct iatt));
out:
        GF_FREE (valid);
        BD_STACK_UNWIND (setattr, frame, op_ret, op_errno, prebuf,
                         postbuf, xdata);
        return 0;
}

int
bd_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc, struct iatt *stbuf,
            int32_t valid, dict_t *xdata)
{
        bd_local_t *local     = NULL;
        bd_attr_t  *bdatt     = NULL;
        int        *ck_valid  = NULL;

        if (bd_inode_ctx_get (loc->inode, this, &bdatt)) {
                STACK_WIND(frame, default_setattr_cbk, FIRST_CHILD(this),
                           FIRST_CHILD(this)->fops->setattr,
                           loc, stbuf, valid, xdata);
                return 0;
        }

        local = bd_local_init (frame, this);
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto out;
        }

        ck_valid = GF_CALLOC (1, sizeof (valid), gf_bd_int32_t);
        if (!ck_valid) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto out;
        }

        local->inode = inode_ref (loc->inode);
        *ck_valid = valid;

        STACK_WIND_COOKIE (frame, bd_setattr_cbk, ck_valid, FIRST_CHILD(this),
                           FIRST_CHILD(this)->fops->setattr,
                           loc, stbuf, valid, xdata);

        return 0;
out:
        BD_STACK_UNWIND (setattr, frame, -1, ENOMEM, NULL, NULL, xdata);
        return 0;
}

int
bd_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int op_ret, int op_errno, inode_t *inode, struct iatt *buf,
             struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        bd_attr_t *bdatt = NULL;

        if (op_ret < 0)
                goto out;

        if (bd_inode_ctx_get (inode, this, &bdatt))
                goto out;

        bdatt->iatt.ia_ctime = buf->ia_ctime;
        bdatt->iatt.ia_ctime_nsec = buf->ia_ctime_nsec;
        bdatt->iatt.ia_nlink = buf->ia_nlink;
        memcpy (buf, &bdatt->iatt, sizeof (struct iatt));

out:
        BD_STACK_UNWIND (link, frame, op_ret, op_errno, inode, buf,
                         preparent, postparent, NULL);
        return 0;
}

int
bd_link (call_frame_t *frame, xlator_t *this,
         loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        STACK_WIND (frame, bd_link_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->link, oldloc, newloc, xdata);
        return 0;
}

int
bd_handle_special_xattrs (call_frame_t *frame, xlator_t *this, loc_t *loc,
                          fd_t *fd, const char *name, dict_t *xdata)
{
        dict_t *xattr   = NULL;
        int op_ret      = -1;
        int op_errno    = ENOMEM;;
        bd_priv_t *priv = this->private;

        xattr = dict_new ();
        if (!xattr)
                goto out;

        if (!strcmp (name, VOL_TYPE))
                op_ret = dict_set_int64 (xattr, (char *)name, 1);
        else if (!strcmp (name, VOL_CAPS))
                op_ret = dict_set_int64 (xattr, (char *)name, priv->caps);
        else
                op_ret = bd_get_origin (this->private, loc, fd, xattr);

out:
        if (loc)
                BD_STACK_UNWIND (getxattr, frame, op_ret, op_errno, xattr,
                                 xdata);
        else
                BD_STACK_UNWIND (fgetxattr, frame, op_ret, op_errno, xattr,
                                 xdata);

        op_ret = dict_reset (xattr);
        dict_unref (xattr);

        return 0;
}

int
bd_fgetxattr (call_frame_t *frame, xlator_t *this,
              fd_t *fd, const char *name, dict_t *xdata)
{
        if (name && (!strcmp (name, VOL_TYPE) || !strcmp (name, VOL_CAPS)
                     || !strcmp (name, BD_ORIGIN)))
                bd_handle_special_xattrs (frame, this, NULL, fd, name, xdata);
        else
                STACK_WIND (frame, default_fgetxattr_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fgetxattr,
                            fd, name, xdata);
        return 0;
}

int
bd_getxattr (call_frame_t *frame, xlator_t *this,
             loc_t *loc, const char *name, dict_t *xdata)
{
        if (name && (!strcmp (name, VOL_TYPE) || !strcmp (name, VOL_CAPS)
                     || !strcmp (name, BD_ORIGIN)))
                bd_handle_special_xattrs (frame, this, loc, NULL, name, xdata);
        else
                STACK_WIND (frame, default_getxattr_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->getxattr,
                            loc, name, xdata);

        return 0;
}

int
bd_unlink_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, inode_t *inode,
                      struct iatt *buf, dict_t *xattr,
                      struct iatt *postparent)
{
        bd_gfid_t     gfid  = {0, };
        bd_local_t   *local = frame->local;

        if (buf->ia_nlink > 1)
                goto posix;

        BD_VALIDATE_LOCAL_OR_GOTO (local, op_errno, out);

        uuid_utoa_r (inode->gfid, gfid);
        if (bd_delete_lv (this->private, gfid, &op_errno) < 0) {
                if (op_errno != ENOENT)
                        goto out;
        }

posix:
        /* remove posix */
        STACK_WIND (frame, default_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink,
                    &local->loc, 0, NULL);

        return 0;
out:
        BD_STACK_UNWIND (unlink, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

int
bd_unlink (call_frame_t *frame, xlator_t *this,
           loc_t *loc, int xflag, dict_t *xdata)
{
        int          op_errno = 0;
        bd_attr_t   *bdatt     = NULL;
        bd_local_t  *local     = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        if (bd_inode_ctx_get (loc->inode, this, &bdatt)) {
                STACK_WIND (frame, default_unlink_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink,
                            loc, xflag, xdata);
                return 0;
        }

        local = bd_local_init (frame, this);
        BD_VALIDATE_MEM_ALLOC (local, op_errno, out);

        loc_copy (&local->loc, loc);

        STACK_WIND (frame, bd_unlink_lookup_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, loc, NULL);
        return 0;
out:
        BD_STACK_UNWIND (unlink, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

int32_t
bd_priv (xlator_t *this)
{
        return 0;
}

int32_t
bd_inode (xlator_t *this)
{
        return 0;
}

int32_t
bd_rchecksum (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              int32_t len, dict_t *xdata)
{
        int             op_ret          = -1;
        int             op_errno        = 0;
        int             ret             = 0;
        int             _fd             = -1;
        char           *alloc_buf       = NULL;
        char           *buf             = NULL;
        int32_t         weak_checksum   = 0;
        bd_fd_t        *bd_fd           = NULL;
        unsigned char   strong_checksum[MD5_DIGEST_LENGTH] = {0};

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);

        ret = bd_fd_ctx_get (this, fd, &bd_fd);
        if (ret < 0 || !bd_fd) {
                STACK_WIND (frame, default_rchecksum_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->rchecksum, fd, offset,
                            len, xdata);
                return 0;
        }

        memset (strong_checksum, 0, MD5_DIGEST_LENGTH);

        alloc_buf = page_aligned_alloc (len, &buf);
        if (!alloc_buf) {
                op_errno = ENOMEM;
                goto out;
        }

        _fd = bd_fd->fd;

        LOCK (&fd->lock);
        {
                ret = sys_pread (_fd, buf, len, offset);
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

        weak_checksum = gf_rsync_weak_checksum ((unsigned char *) buf,
                                                (size_t) len);
        gf_rsync_strong_checksum ((unsigned char *) buf, (size_t) len,
                                  (unsigned char *) strong_checksum);

        op_ret = 0;
out:
        BD_STACK_UNWIND (rchecksum, frame, op_ret, op_errno,
                         weak_checksum, strong_checksum, NULL);

        GF_FREE (alloc_buf);

        return 0;
}

static int
bd_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            off_t len, dict_t *xdata)
{
        int32_t ret             =  0;
        struct  iatt statpre    = {0,};
        struct  iatt statpost   = {0,};
        bd_attr_t *bdatt = NULL;

        /* iatt already cached */
        if (bd_inode_ctx_get (fd->inode, this, &bdatt) < 0) {
                STACK_WIND (frame, default_zerofill_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->zerofill,
                            fd, offset, len, xdata);
                return 0;
        }

        ret = bd_do_zerofill(frame, this, fd, offset, len,
                             &statpre, &statpost);
        if (ret)
                goto err;

        STACK_UNWIND_STRICT(zerofill, frame, 0, 0, &statpre, &statpost, NULL);
        return 0;

err:
        STACK_UNWIND_STRICT(zerofill, frame, -1, ret, NULL, NULL, NULL);
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
                /* Tell the parent that bd xlator is up */
                default_notify (this, GF_EVENT_CHILD_UP, data);
        }
        break;
        default:
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

        ret = xlator_mem_acct_init (this, gf_bd_mt_end + 1);

        if (ret != 0)
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                       "failed");

        return ret;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        int   ret = -1;
        bd_priv_t *priv = this->private;

        GF_OPTION_RECONF ("bd-aio", priv->aio_configured, options,
                          bool, out);

        if (priv->aio_configured)
                bd_aio_on (this);
        else
                bd_aio_off (this);

        ret = 0;
out:
        return ret;
}

/**
 * bd xlator init - Validate configured VG
 */
int
init (xlator_t *this)
{
        char       *vg_data     = NULL;
        char       *device      = NULL;
        bd_priv_t  *_private    = NULL;

        if (!this->children) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "FATAL: storage/bd needs posix as subvolume");
                return -1;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Volume is dangling. Please check the volume file.");
        }

        GF_OPTION_INIT ("export", vg_data, str, error);
        GF_OPTION_INIT ("device", device, str, error);

        /* Now we support only LV device */
        if (strcasecmp (device, BACKEND_VG)) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "FATAL: unknown %s backend %s", BD_XLATOR, device);
                return -1;
        }

        this->local_pool = mem_pool_new (bd_local_t, 64);
        if (!this->local_pool) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "FATAL: Failed to create bd memory pool");
                return -1;
        }

        _private = GF_CALLOC (1, sizeof (*_private), gf_bd_private);
        if (!_private)
                goto error;

        this->private = _private;
        _private->vg = gf_strdup (vg_data);
        if (!_private->vg)
                goto error;

        _private->handle = lvm_init (NULL);
        if (!_private->handle) {
                gf_log (this->name, GF_LOG_CRITICAL, "lvm_init failed");
                goto error;
        }
        _private->caps = BD_CAPS_BD;
        if (bd_scan_vg (this, _private))
                goto error;

        _private->aio_init_done = _gf_false;
        _private->aio_capable = _gf_false;

        GF_OPTION_INIT ("bd-aio", _private->aio_configured, bool, error);
        if (_private->aio_configured) {
                if (bd_aio_on (this)) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "BD AIO init failed");
                        goto error;
                }
        }

        _private->caps |= BD_CAPS_OFFLOAD_COPY | BD_CAPS_OFFLOAD_SNAPSHOT |
                BD_CAPS_OFFLOAD_ZERO;

        return 0;
error:
        if (_private) {
                GF_FREE (_private->vg);
                if (_private->handle)
                        lvm_quit (_private->handle);
                GF_FREE (_private);
        }

        mem_pool_destroy (this->local_pool);

        return -1;
}

void
fini (xlator_t *this)
{
        bd_priv_t *priv = this->private;
        mem_pool_destroy (this->local_pool);
        this->local_pool = NULL;
        if (!priv)
                return;
        lvm_quit (priv->handle);
        GF_FREE (priv->vg);
        this->private = NULL;
        GF_FREE (priv);
        return;
}

struct xlator_dumpops dumpops = {
        .priv    = bd_priv,
        .inode   = bd_inode,
};

struct xlator_fops fops = {
        .readdirp    = bd_readdirp,
        .lookup      = bd_lookup,
        .stat        = bd_stat,
        .statfs      = bd_statfs,
        .open        = bd_open,
        .fstat       = bd_fstat,
        .rchecksum   = bd_rchecksum,
        .readv       = bd_readv,
        .fsync       = bd_fsync,
        .setxattr    = bd_setxattr,
        .fsetxattr   = bd_fsetxattr,
        .removexattr = bd_removexattr,
        .fremovexattr=bd_fremovexattr,
        .truncate    = bd_truncate,
        .ftruncate   = bd_ftruncate,
        .writev      = bd_writev,
        .getxattr    = bd_getxattr,
        .fgetxattr   = bd_fgetxattr,
        .unlink      = bd_unlink,
        .link        = bd_link,
        .flush       = bd_flush,
        .setattr     = bd_setattr,
        .discard     = bd_discard,
        .zerofill    = bd_zerofill,
};

struct xlator_cbks cbks = {
        .release     = bd_release,
        .forget      = bd_forget,
};

struct volume_options options[] = {
        { .key = {"export"},
          .type = GF_OPTION_TYPE_STR},
        { .key = {"device"},
          .type = GF_OPTION_TYPE_STR,
          .default_value = BACKEND_VG},
        {
          .key  = {"bd-aio"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "Support for native Linux AIO"
        },

        { .key = {NULL} }
};
