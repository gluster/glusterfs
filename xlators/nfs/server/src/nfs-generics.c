/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
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


#include "string.h"

#include "inode.h"
#include "nfs.h"
#include "nfs-fops.h"
#include "nfs-inodes.h"
#include "nfs-generics.h"
#include "xlator.h"


int
nfs_fstat (xlator_t *xl, nfs_user_t *nfu, fd_t *fd, fop_stat_cbk_t cbk,
           void *local)
{
        int             ret = -EFAULT;

        if ((!xl) || (!fd) || (!nfu))
                return ret;

        ret = nfs_fop_fstat (xl, nfu, fd, cbk, local);
        return ret;
}


int
nfs_stat (xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc, fop_stat_cbk_t cbk,
          void *local)
{
        int             ret = -EFAULT;

        if ((!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_fop_stat (xl, nfu, pathloc, cbk, local);

        return ret;
}



int
nfs_readdirp (xlator_t *xl, nfs_user_t *nfu, fd_t *dirfd, size_t bufsize,
              off_t offset, fop_readdir_cbk_t cbk, void *local)
{
        if ((!xl) || (!dirfd) || (!nfu))
                return -EFAULT;

        return nfs_fop_readdirp (xl, nfu, dirfd, bufsize, offset, cbk,
                                 local);
}



int
nfs_lookup (xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
            fop_lookup_cbk_t cbk, void *local)
{
        int             ret = -EFAULT;

        if ((!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_fop_lookup (xl, nfu, pathloc, cbk, local);
        return ret;
}

int
nfs_create (xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc, int flags,
            mode_t mode, fop_create_cbk_t cbk, void *local)
{
        int                     ret = -EFAULT;

        if ((!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_inode_create (xl, nfu, pathloc, flags, mode, cbk,local);
        return ret;
}


int
nfs_flush (xlator_t *xl, nfs_user_t *nfu, fd_t *fd, fop_flush_cbk_t cbk,
           void *local)
{
        return nfs_fop_flush (xl, nfu, fd, cbk, local);
}



int
nfs_mkdir (xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc, mode_t mode,
           fop_mkdir_cbk_t cbk, void *local)
{
        int                     ret = -EFAULT;

        if ((!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_inode_mkdir (xl, nfu, pathloc, mode, cbk, local);
        return ret;
}



int
nfs_truncate (xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc, off_t offset,
              fop_truncate_cbk_t cbk, void *local)
{
        int     ret = -EFAULT;

        if ((!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_fop_truncate (xl, nfu, pathloc, offset, cbk, local);
        return ret;
}


int
nfs_read (xlator_t *xl, nfs_user_t *nfu, fd_t *fd, size_t size,
          off_t offset, fop_readv_cbk_t cbk, void *local)
{
        return nfs_fop_read (xl, nfu, fd, size, offset, cbk, local);
}


int
nfs_fsync (xlator_t *xl, nfs_user_t *nfu, fd_t *fd, int32_t datasync,
           fop_fsync_cbk_t cbk, void *local)
{
        return nfs_fop_fsync (xl, nfu, fd, datasync, cbk, local);
}


int
nfs_write (xlator_t *xl, nfs_user_t *nfu, fd_t *fd, struct iobuf *srciob,
           struct iovec *vector, int32_t count, off_t offset,
           fop_writev_cbk_t cbk, void *local)
{
        return nfs_fop_write (xl, nfu, fd, srciob, vector, count, offset, cbk,
                              local);
}


int
nfs_open (xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc, int32_t flags,
          fop_open_cbk_t cbk, void *local)
{
        int             ret = -EFAULT;

        if ((!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_inode_open (xl, nfu, pathloc, flags, GF_OPEN_NOWB, cbk,
                              local);
        return ret;
}


int
nfs_rename (xlator_t *xl, nfs_user_t *nfu, loc_t *oldloc, loc_t *newloc,
            fop_rename_cbk_t cbk, void *local)
{
        int                     ret = -EFAULT;

        if ((!xl) || (!oldloc) || (!newloc) || (!nfu))
                return ret;

        ret = nfs_inode_rename (xl, nfu, oldloc, newloc, cbk, local);
        return ret;
}


int
nfs_link (xlator_t *xl, nfs_user_t *nfu, loc_t *oldloc, loc_t *newloc,
          fop_link_cbk_t cbk, void *local)
{
        int                     ret = -EFAULT;

        if ((!xl) || (!oldloc) || (!newloc) || (!nfu))
                return ret;

        ret = nfs_inode_link (xl, nfu, oldloc, newloc, cbk, local);
        return ret;
}


int
nfs_unlink (xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
            fop_unlink_cbk_t cbk, void *local)
{
        int                     ret = -EFAULT;

        if ((!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_inode_unlink (xl, nfu, pathloc, cbk, local);
        return ret;
}


int
nfs_rmdir (xlator_t *xl, nfs_user_t *nfu, loc_t *path, fop_rmdir_cbk_t cbk,
           void *local)
{
        int                     ret = -EFAULT;

        if ((!xl) || (!path) || (!nfu))
                return ret;

        ret = nfs_inode_rmdir (xl, nfu, path, cbk, local);
        return ret;
}


int
nfs_mknod (xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc, mode_t mode,
           dev_t dev, fop_mknod_cbk_t cbk, void *local)
{
        int                     ret = -EFAULT;

        if ((!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_inode_mknod (xl, nfu, pathloc, mode, dev, cbk, local);
        return ret;
}


int
nfs_readlink (xlator_t *xl, nfs_user_t *nfu, loc_t *linkloc,
              fop_readlink_cbk_t cbk, void *local)
{
        int                     ret = -EFAULT;

        if ((!xl) || (!linkloc) || (!nfu))
                return ret;

        ret = nfs_fop_readlink (xl, nfu, linkloc, NFS_PATH_MAX, cbk, local);
        return ret;
}



int
nfs_symlink (xlator_t *xl, nfs_user_t *nfu, char *target, loc_t *linkloc,
             fop_symlink_cbk_t cbk, void *local)
{
        int                     ret = -EFAULT;

        if ((!xl) || (!linkloc) || (!target) || (!nfu))
                return ret;

        ret = nfs_inode_symlink (xl, nfu, target, linkloc, cbk, local);
        return ret;
}



int
nfs_setattr (xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
             struct iatt *buf, int32_t valid, fop_setattr_cbk_t cbk,
             void *local)
{
        int     ret = -EFAULT;

        if ((!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_fop_setattr (xl, nfu, pathloc, buf, valid, cbk, local);
        return ret;
}


int
nfs_statfs (xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
            fop_statfs_cbk_t cbk, void *local)
{
        int     ret = -EFAULT;

        if ((!xl) || (!pathloc) || (!nfu))
                return ret;

        ret = nfs_fop_statfs (xl, nfu, pathloc, cbk, local);
        return ret;
}


int
nfs_open_sync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        nfs_syncfop_t   *sf = frame->local;

        if (!sf)
                return -1;

        if (op_ret == -1)
                gf_log (GF_NFS, GF_LOG_TRACE, "Sync open failed: %s",
                        strerror (op_errno));
        else
                gf_log (GF_NFS, GF_LOG_TRACE, "Sync open done");

        sf->replystub = fop_open_cbk_stub (frame, NULL, op_ret, op_errno, fd);

        nfs_syncfop_notify (sf);
        return 0;
}


call_stub_t *
nfs_open_sync (xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc, int32_t flags)
{
        nfs_syncfop_t   *sf = NULL;
        call_stub_t     *reply = NULL;
        int             ret = -1;

        if ((!xl) || (!pathloc) || (!nfu))
                return NULL;

        sf = nfs_syncfop_init ();
        if (!sf) {
                gf_log (GF_NFS, GF_LOG_ERROR, "synclocal init failed");
                goto err;
        }

        ret = nfs_open (xl, nfu, pathloc, flags, nfs_open_sync_cbk, sf);
        if (ret < 0)
                goto err;

        reply = nfs_syncfop_wait (sf);

err:
        if (ret < 0)
                FREE (sf);

        return reply;
}



int
nfs_fdctx_alloc (fd_t *fd, xlator_t *xl)
{
        nfs_fdctx_t     *fdctx = NULL;
        int             ret = -EFAULT;

        if ((!fd) || (!xl))
                return ret;

        fdctx = CALLOC (1, sizeof (*fdctx));
        if (!fdctx) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Memory allocation failure");
                ret = -ENOMEM;
                goto err;
        }

        pthread_mutex_init (&fdctx->lock, NULL);

        ret = fd_ctx_set (fd, xl, (uint64_t)fdctx);
        if (ret == -1) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Failed to set fd context");
                ret = -EFAULT;
                goto free_ctx;
        }

        ret = 0;

free_ctx:
        if (ret < 0)
                FREE (fdctx);

err:
        return ret;
}


void
nfs_fdctx_del (fd_t *fd, xlator_t *xl)
{
        nfs_fdctx_t     *fdctx = NULL;
        uint64_t        ctxaddr = 0;
        int             ret = -1;

        if ((!fd) || (!xl))
                return;

        ret = fd_ctx_del (fd, xl, &ctxaddr);
        if (ret == -1)
                goto err;

        fdctx = (nfs_fdctx_t *)ctxaddr;
        FREE (fdctx);

err:
        return;
}


nfs_fdctx_t *
nfs_fdctx_get (fd_t *fd, xlator_t *xl)
{
        nfs_fdctx_t     *fdctx = NULL;
        int             ret = -1;
        uint64_t        ctxptr = 0;

        if ((!fd) || (!xl))
                return NULL;

        ret = fd_ctx_get (fd, xl, &ctxptr);
        if (ret == -1) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Failed to get fd context");
                goto err;
        }

        fdctx = (nfs_fdctx_t *)ctxptr;
err:
        return fdctx;
}


int
nfs_dir_fdctx_init (fd_t *dirfd, xlator_t *xl, xlator_t *fopxl, size_t bufsize)
{
        int             ret = -EFAULT;
        nfs_fdctx_t     *fdctx = NULL;

        if ((!dirfd) || (!xl))
                return ret;

        ret = nfs_fdctx_alloc (dirfd, xl);
        if (ret < 0) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Failed to alloc dir fd context");
                goto err;
        }

        fdctx = nfs_fdctx_get (dirfd, xl);
        if (!fdctx) {
                ret = -EFAULT;
                gf_log (GF_NFS, GF_LOG_ERROR, "Failed to get dir fd context");
                goto err;
        }

        fdctx->dcache = CALLOC (1, sizeof (struct nfs_direntcache));
        if (!fdctx->dcache) {
                ret = -ENOMEM;
                gf_log (GF_NFS, GF_LOG_ERROR, "Failed to allocate dirent"
                        " cache");
                goto free_ctx;
        }

        INIT_LIST_HEAD (&fdctx->dcache->entries.list);
        fdctx->dirent_bufsize = bufsize;
        fdctx->dirvol = fopxl;

        ret = 0;

free_ctx:
        if (ret < 0)
                nfs_fdctx_del (dirfd, xl);
err:
        return ret;
}




/* Dirent caching code copied from libglusterfsclient.
 * Please duplicate enhancements and bug fixes there too.
 */
void
nfs_dcache_invalidate (xlator_t *nfsx, fd_t *fd)
{
        nfs_fdctx_t     *fd_ctx = NULL;

        if (!fd)
                return;

        fd_ctx = nfs_fdctx_get (fd, nfsx);
        if (!fd_ctx) {
                gf_log (GF_NFS, GF_LOG_ERROR, "No fd context present");
                return;
        }

        if (!fd_ctx->dcache) {
                gf_log (GF_NFS, GF_LOG_TRACE, "No dirent cache present");
                return;
        }

        if (!list_empty (&fd_ctx->dcache->entries.list)) {
                gf_log (GF_NFS, GF_LOG_TRACE, "Freeing dirents");
                gf_dirent_free (&fd_ctx->dcache->entries);
        }

        INIT_LIST_HEAD (&fd_ctx->dcache->entries.list);

        fd_ctx->dcache->next = NULL;
        fd_ctx->dcache->prev_off = 0;
        gf_log (GF_NFS, GF_LOG_TRACE, "Dirent cache invalidated");

        return;
}

/* Dirent caching code copied from libglusterfsclient.
 * Please duplicate enhancements and bug fixes there too.
 */
/* The first entry in the entries is always a placeholder
 * or the list head. The real entries begin from entries->next.
 */
int
nfs_dcache_update (xlator_t *nfsx, fd_t *fd, gf_dirent_t *entries)
{
        nfs_fdctx_t     *fdctx = NULL;
        int             ret = -EFAULT;

        if ((!fd) || (!entries))
                return ret;

        fdctx = nfs_fdctx_get (fd, nfsx);
        if (!fdctx) {
                gf_log (GF_NFS, GF_LOG_ERROR, "No fd context present");
                return ret;
        }

        /* dcache is not enabled. */
        if (!fdctx->dcache) {
                gf_log (GF_NFS, GF_LOG_TRACE, "No dirent cache present");
                return ret;
        }

        /* If we're updating, we must begin with invalidating any previous
         * entries.
         */
        nfs_dcache_invalidate (nfsx, fd);

        fdctx->dcache->next = entries->next;
        /* We still need to store a pointer to the head
         * so we start free'ing from the head when invalidation
         * is required.
         *
         * Need to delink the entries from the list
         * given to us by an underlying translators. Most translators will
         * free this list after this call so we must preserve the dirents in
         * order to cache them.
         */
        list_splice_init (&entries->list, &fdctx->dcache->entries.list);
        ret = 0;
        gf_log (GF_NFS, GF_LOG_TRACE, "Dirent cache updated");

        return ret;
}

/* Dirent caching code copied from libglusterfsclient.
 * Please duplicate enhancements and bug fixes there too.
 */
int
nfs_dcache_readdir (xlator_t *nfsx, fd_t *fd, gf_dirent_t *dirp, off_t *offset)
{
        nfs_fdctx_t     *fdctx = NULL;
        int             cachevalid = 0;

        if ((!fd) || (!dirp) || (!offset))
                return 0;

        fdctx = nfs_fdctx_get (fd, nfsx);
        if (!fdctx) {
                gf_log (GF_NFS, GF_LOG_ERROR, "No fd context present");
                goto out;
        }

        if (!fdctx->dcache) {
                gf_log (GF_NFS, GF_LOG_TRACE, "No dirent cache present");
                goto out;
        }

        /* We've either run out of entries in the cache
         * or the cache is empty.
         */
        if (!fdctx->dcache->next) {
                gf_log (GF_NFS, GF_LOG_TRACE, "Dirent cache is empty");
                goto out;
        }

        /* The dirent list is created as a circular linked list
         * so this check is needed to ensure, we dont start
         * reading old entries again.
         * If we're reached this situation, the cache is exhausted
         * and we'll need to pre-fetch more entries to continue serving.
         */
        if (fdctx->dcache->next == &fdctx->dcache->entries) {
                gf_log (GF_NFS, GF_LOG_TRACE, "Dirent cache was exhausted");
                goto out;
        }

        /* During sequential reading we generally expect that the offset
         * requested is the same as the offset we served in the previous call
         * to readdir. But, seekdir, rewinddir and libgf_dcache_invalidate
         * require special handling because seekdir/rewinddir change the offset
         * in the fd_ctx and libgf_dcache_invalidate changes the prev_off.
         */
        if (*offset != fdctx->dcache->prev_off) {
                /* For all cases of the if branch above, we know that the
                 * cache is now invalid except for the case below. It handles
                 * the case where the two offset values above are different
                 * but different because the previous readdir block was
                 * exhausted, resulting in a prev_off being set to 0 in
                 * libgf_dcache_invalidate, while the requested offset is non
                 * zero because that is what we returned for the last dirent
                 * of the previous readdir block.
                 */
                if ((*offset != 0) && (fdctx->dcache->prev_off == 0)) {
                        gf_log (GF_NFS, GF_LOG_TRACE, "Dirent cache was"
                                " exhausted");
                        cachevalid = 1;
                } else
                        gf_log (GF_NFS, GF_LOG_TRACE, "Dirent cache is"
                                " invalid");
        } else {
                gf_log (GF_NFS, GF_LOG_TRACE, "Dirent cache is valid");
                cachevalid = 1;
        }

        if (!cachevalid)
                goto out;

        dirp->d_ino = fdctx->dcache->next->d_ino;
        strncpy (dirp->d_name, fdctx->dcache->next->d_name,
                 fdctx->dcache->next->d_len + 1);
        dirp->d_len = fdctx->dcache->next->d_len;
        dirp->d_stat = fdctx->dcache->next->d_stat;
//        nfs_map_dev (fdctx->dirvol, &dirp->d_stat.st_dev);

        *offset = fdctx->dcache->next->d_off;
        dirp->d_off = *offset;
        fdctx->dcache->prev_off = fdctx->dcache->next->d_off;
        fdctx->dcache->next = fdctx->dcache->next->next;

out:
        return cachevalid;
}

int
__nfs_readdir_sync (xlator_t *nfsx, xlator_t *fopxl, nfs_user_t *nfu,
                    fd_t *dirfd, off_t *offset, gf_dirent_t *entry,
                    size_t bufsize)
{
        int             ret = -1;
        call_stub_t     *reply = NULL;

        if ((!fopxl) || (!dirfd) || (!entry))
                return ret;

        ret = nfs_dcache_readdir (nfsx, dirfd, entry, offset);
        if (ret) {
                gf_log (GF_NFS, GF_LOG_TRACE, "Dirent served from cache");
                ret = 0;
                goto err;
        }

        reply = nfs_fop_readdirp_sync (fopxl, nfu, dirfd, *offset,bufsize);
        if (!reply) {
                ret = -1;
                gf_log (GF_NFS, GF_LOG_ERROR, "Sync readdir failed");
                goto err;
        }

        if (reply->args.readdir_cbk.op_ret <= 0) {
                ret = -1;
                goto err;
        }

        ret = nfs_dcache_update (nfsx, dirfd, &reply->args.readdir_cbk.entries);
        if (ret < 0) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Failed to update dirent cache");
                goto err;
        }

        ret = nfs_dcache_readdir (nfsx, dirfd, entry, offset);
        if (ret) {
                gf_log (GF_NFS, GF_LOG_TRACE, "Dirent served from cache,"
                        " after updating from server");
                ret = 0;
        } else {
                gf_log (GF_NFS, GF_LOG_TRACE, "Dirent still not served from"
                        " cache, even after updating from server");
                ret = -1;
        }

err:
        if (reply)
                call_stub_destroy (reply);
        return ret;
}



int
nfs_readdir_sync (xlator_t *nfsx, xlator_t *fopxl, nfs_user_t *nfu,
                  fd_t *dirfd, gf_dirent_t *entry)
{
        int             ret = -EFAULT;
        nfs_fdctx_t     *fdctx = NULL;

        if ((!nfsx) || (!fopxl) || (!dirfd) || (!entry) || (!nfu))
                return ret;

        fdctx = nfs_fdctx_get (dirfd, nfsx);
        if (!fdctx) {
                gf_log (GF_NFS, GF_LOG_ERROR, "No fd context present");
                goto err;
        }

        pthread_mutex_lock (&fdctx->lock);
        {
                ret =  __nfs_readdir_sync (nfsx, fopxl, nfu, dirfd,
                                           &fdctx->offset, entry,
                                           fdctx->dirent_bufsize);
        }
        pthread_mutex_unlock (&fdctx->lock);

        if (ret < 0)
                gf_log (GF_NFS, GF_LOG_ERROR, "Sync readdir failed: %s",
                        strerror (-ret));
        else
                gf_log (GF_NFS, GF_LOG_TRACE, "Entry read: %s: len %d, ino %"
                        PRIu64", igen: %"PRIu64, entry->d_name, entry->d_len,
                        entry->d_ino, entry->d_stat.ia_dev);

err:
        return ret;
}


int32_t
nfs_flush_sync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno)
{
        nfs_syncfop_t   *sf = frame->local;

        if (!sf)
                return -1;

        if (op_ret == -1)
                gf_log (GF_NFS, GF_LOG_TRACE, "Sync open failed: %s",
                        strerror (op_errno));
        else
                gf_log (GF_NFS, GF_LOG_TRACE, "Sync open done");

        sf->replystub = fop_flush_cbk_stub (frame, NULL, op_ret, op_errno);

        nfs_syncfop_notify (sf);
        return 0;
}


call_stub_t *
nfs_flush_sync (xlator_t *xl, nfs_user_t *nfu, fd_t *fd)
{
        nfs_syncfop_t   *sf = NULL;
        call_stub_t     *reply = NULL;
        int             ret = -1;

        if ((!xl) || (!fd) || (!nfu))
                return NULL;

        sf = nfs_syncfop_init ();
        if (!sf) {
                gf_log (GF_NFS, GF_LOG_ERROR, "synclocal init failed");
                goto err;
        }

        ret = nfs_flush (xl, nfu, fd, nfs_flush_sync_cbk, sf);
        if (ret < 0) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Sync flush failed: %s",
                        strerror (-ret));
                goto err;
        }

        reply = nfs_syncfop_wait (sf);

err:
        if (ret < 0)
                FREE (sf);

        return reply;
}


int32_t
nfs_writev_sync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf)
{
        nfs_syncfop_t   *sf = frame->local;

        if (!sf)
                return -1;

        if (op_ret == -1)
                gf_log (GF_NFS, GF_LOG_TRACE, "Sync write failed: %s",
                        strerror (op_errno));
        else
                gf_log (GF_NFS, GF_LOG_TRACE, "Sync write done");

        sf->replystub = fop_writev_cbk_stub (frame, NULL, op_ret, op_errno,
                                             prebuf, postbuf);

        nfs_syncfop_notify (sf);
        return 0;
}



call_stub_t *
nfs_write_sync (xlator_t *xl, nfs_user_t *nfu, fd_t *fd, struct iobuf *srciob,
                struct iovec *vec, int count, off_t offset)
{
        nfs_syncfop_t   *sf = NULL;
        call_stub_t     *reply = NULL;
        int             ret = -1;

        if ((!xl) || (!fd) || (!vec) || (!nfu) || (!srciob))
                return NULL;

        sf = nfs_syncfop_init ();
        if (!sf) {
                gf_log (GF_NFS, GF_LOG_ERROR, "synclocal init failed");
                goto err;
        }

        ret = nfs_write (xl, nfu, fd, srciob, vec, count, offset,
                         nfs_writev_sync_cbk, sf);
        if (ret < 0) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Sync flush failed: %s",
                        strerror (-ret));
                goto err;
        }

        reply = nfs_syncfop_wait (sf);

err:
        if (ret < 0)
                FREE (sf);

        return reply;
}



int32_t
nfs_fsync_sync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                    struct iatt *postbuf)
{
        nfs_syncfop_t   *sf = frame->local;

        if (!sf)
                return -1;

        if (op_ret == -1)
                gf_log (GF_NFS, GF_LOG_TRACE, "Sync fsync failed: %s",
                        strerror (op_errno));
        else
                gf_log (GF_NFS, GF_LOG_TRACE, "Sync fsync done");

        sf->replystub = fop_fsync_cbk_stub (frame, NULL, op_ret, op_errno,
                                            prebuf, postbuf);

        nfs_syncfop_notify (sf);
        return 0;
}



call_stub_t *
nfs_fsync_sync (xlator_t *xl, nfs_user_t *nfu, fd_t *fd, int32_t datasync)
{
        nfs_syncfop_t   *sf = NULL;
        call_stub_t     *reply = NULL;
        int             ret = -1;

        if ((!xl) || (!fd) || (!nfu))
                return NULL;

        sf = nfs_syncfop_init ();
        if (!sf) {
                gf_log (GF_NFS, GF_LOG_ERROR, "synclocal init failed");
                goto err;
        }

        ret = nfs_fsync (xl, nfu, fd, datasync, nfs_fsync_sync_cbk, sf);
        if (ret < 0) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Sync fsync failed: %s",
                        strerror (-ret));
                goto err;
        }

        reply = nfs_syncfop_wait (sf);

err:
        if (ret < 0)
                FREE (sf);

        return reply;
}

int32_t
nfs_read_sync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iovec *vector,
                   int32_t count, struct iatt *buf, struct iobref *iobref)
{
        nfs_syncfop_t   *sf = frame->local;

        if (!sf)
                return -1;

        if (op_ret == -1)
                gf_log (GF_NFS, GF_LOG_TRACE, "Sync read failed: %s",
                        strerror (op_errno));
        else
                gf_log (GF_NFS, GF_LOG_TRACE, "Sync read done");

        sf->replystub = fop_readv_cbk_stub (frame, NULL, op_ret, op_errno,
                                            vector, count, buf, iobref);

        nfs_syncfop_notify (sf);
        return 0;
}



call_stub_t *
nfs_read_sync (xlator_t *xl, nfs_user_t *nfu, fd_t *fd, size_t size,
               off_t offset)
{
        nfs_syncfop_t   *sf = NULL;
        call_stub_t     *reply = NULL;
        int             ret = -1;

        if ((!xl) || (!fd) || (!nfu))
                return NULL;

        sf = nfs_syncfop_init ();
        if (!sf) {
                gf_log (GF_NFS, GF_LOG_ERROR, "synclocal init failed");
                goto err;
        }

        ret = nfs_read (xl, nfu, fd, size, offset, nfs_read_sync_cbk, sf);
        if (ret < 0) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Sync read failed: %s",
                        strerror (-ret));
                goto err;
        }

        reply = nfs_syncfop_wait (sf);

err:
        if (ret < 0)
                FREE (sf);

        return reply;
}


int32_t
nfs_opendir_sync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        nfs_syncfop_t   *sf = frame->local;

        if (!sf)
                return -1;

        if (op_ret == -1)
                gf_log (GF_NFS, GF_LOG_TRACE, "Sync opendir failed: %s",
                        strerror (op_errno));
        else
                gf_log (GF_NFS, GF_LOG_TRACE, "Sync opendir done");

        sf->replystub = fop_opendir_cbk_stub (frame, NULL, op_ret, op_errno,fd);

        nfs_syncfop_notify (sf);
        return 0;
}


call_stub_t *
nfs_opendir_sync (xlator_t *nfsx, xlator_t *fopxl, nfs_user_t *nfu,
                  loc_t *pathloc, size_t bufsize)
{
        int             ret = -EFAULT;
        call_stub_t     *reply = NULL;
        nfs_syncfop_t   *sf = NULL;
        fd_t            *dirfd = NULL;

        if ((!nfsx) || (!fopxl) || (!pathloc) || (!nfu))
                return NULL;

        sf = nfs_syncfop_init ();
        if (!sf) {
                gf_log (GF_NFS, GF_LOG_ERROR, "synclocal init failed");
                ret = -ENOMEM;
                goto err;
        }

        ret = nfs_inode_opendir (fopxl, nfu, pathloc, nfs_opendir_sync_cbk, sf);
        if (ret < 0)
                goto err;

        reply = nfs_syncfop_wait (sf);
        if (!reply) {
                ret = -EFAULT;
                goto err;
        }

        dirfd = reply->args.opendir_cbk.fd;
        ret = nfs_dir_fdctx_init (dirfd, nfsx, fopxl, bufsize);
        if (ret < 0) {
                gf_log (GF_NFS, GF_LOG_ERROR, "fd context allocation failed");
                goto err;
        }

        ret = 0;

err:
        if (ret < 0)
                FREE (sf);

        return reply;
}


int
nfs_opendir (xlator_t *fopxl, nfs_user_t *nfu, loc_t *pathloc,
             fop_opendir_cbk_t cbk, void *local)
{
        if ((!fopxl) || (!pathloc) || (!nfu))
                return -EFAULT;

        return nfs_inode_opendir (fopxl, nfu, pathloc, cbk, local);
}
