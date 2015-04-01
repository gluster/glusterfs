/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <unistd.h>

#include "shard.h"
#include "shard-mem-types.h"
#include "byte-order.h"
#include "defaults.h"

static gf_boolean_t
__is_shard_dir (uuid_t gfid)
{
        shard_priv_t  *priv = THIS->private;

        if (uuid_compare (gfid, priv->dot_shard_gfid) == 0)
                return _gf_true;

        return _gf_false;
}

void
shard_make_block_bname (int block_num, uuid_t gfid, char *buf, size_t len)
{
        char gfid_str[GF_UUID_BUF_SIZE] = {0,};

        uuid_unparse (gfid, gfid_str);
        snprintf (buf, len, "%s.%d", gfid_str, block_num);
}

void
shard_make_block_abspath (int block_num, uuid_t gfid, char *filepath,
                          size_t len)
{
        char gfid_str[GF_UUID_BUF_SIZE] = {0,};

        uuid_unparse (gfid, gfid_str);
        snprintf (filepath, len, "/%s/%s.%d", GF_SHARD_DIR, gfid_str,
                  block_num);
}

int
__shard_inode_ctx_get (inode_t *inode, xlator_t *this, shard_inode_ctx_t **ctx)
{
        int                 ret      = -1;
        uint64_t            ctx_uint = 0;
        shard_inode_ctx_t  *ctx_p    = NULL;

        ret = __inode_ctx_get (inode, this, &ctx_uint);
        if (ret == 0) {
                *ctx = (shard_inode_ctx_t *) ctx_uint;
                return ret;
        }

        ctx_p = GF_CALLOC (1, sizeof (*ctx_p), gf_shard_mt_inode_ctx_t);
        if (!ctx_p)
                return ret;

        ret = __inode_ctx_set (inode, this, (uint64_t *)&ctx_p);
        if (ret < 0) {
                GF_FREE (ctx_p);
                return ret;
        }

        *ctx = ctx_p;

        return ret;
}


int
__shard_inode_ctx_set (inode_t *inode, xlator_t *this,
                       shard_inode_ctx_t *ctx_in)
{
        int                 ret = -1;
        shard_inode_ctx_t  *ctx = NULL;

        ret = __shard_inode_ctx_get (inode, this, &ctx);
        if (ret)
                return ret;

        ctx->block_size = ctx_in->block_size;
        ctx->mode = ctx_in->mode;
        ctx->rdev = ctx_in->rdev;

        return 0;
}

int
shard_inode_ctx_set_all (inode_t *inode, xlator_t *this,
                         shard_inode_ctx_t *ctx_in)
{
        int ret = -1;

        LOCK (&inode->lock);
        {
                ret = __shard_inode_ctx_set (inode, this, ctx_in);
        }
        UNLOCK (&inode->lock);

        return ret;

}

int
__shard_inode_ctx_get_block_size (inode_t *inode, xlator_t *this,
                                  uint64_t *block_size)
{
        int                 ret      = -1;
        uint64_t            ctx_uint = 0;
        shard_inode_ctx_t  *ctx      = NULL;

        ret = __inode_ctx_get (inode, this, &ctx_uint);
        if (ret < 0)
                return ret;

        ctx = (shard_inode_ctx_t *) ctx_uint;

        *block_size = ctx->block_size;

        return 0;
}

int
shard_inode_ctx_get_block_size (inode_t *inode, xlator_t *this,
                                uint64_t *block_size)
{
        int ret = -1;

        LOCK (&inode->lock);
        {
                ret = __shard_inode_ctx_get_block_size (inode, this,
                                                        block_size);
        }
        UNLOCK (&inode->lock);

        return ret;
}

int
__shard_inode_ctx_get_all (inode_t *inode, xlator_t *this,
                           shard_inode_ctx_t *ctx_out)
{
        int                 ret      = -1;
        uint64_t            ctx_uint = 0;
        shard_inode_ctx_t  *ctx      = NULL;

        ret = __inode_ctx_get (inode, this, &ctx_uint);
        if (ret < 0)
                return ret;

        ctx = (shard_inode_ctx_t *) ctx_uint;

        ctx_out->block_size = ctx->block_size;
        ctx_out->mode = ctx->mode;
        ctx_out->rdev = ctx->rdev;

        return 0;
}

int
shard_inode_ctx_get_all (inode_t *inode, xlator_t *this,
                         shard_inode_ctx_t *ctx_out)
{
        int ret = -1;

        LOCK (&inode->lock);
        {
                ret = __shard_inode_ctx_get_all (inode, this, ctx_out);
        }
        UNLOCK (&inode->lock);

        return ret;
}

void
shard_local_wipe (shard_local_t *local)
{
        int  i     = 0;
        int  count = 0;

        count = local->num_blocks;

        loc_wipe (&local->loc);
        loc_wipe (&local->dot_shard_loc);

        if (local->fd)
                fd_unref (local->fd);

        if (local->xattr_req)
                dict_unref (local->xattr_req);
        if (local->xattr_rsp)
                dict_unref (local->xattr_rsp);

        for (i = 0; i < count; i++) {
                if (local->inode_list[i])
                        inode_unref (local->inode_list[i]);
        }

        GF_FREE (local->inode_list);

        GF_FREE (local->vector);
        if (local->iobref)
                iobref_unref (local->iobref);
}

int
shard_call_count_return (call_frame_t *frame)
{
        int             call_count = 0;
        shard_local_t  *local      = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                call_count = --local->call_count;
        }
        UNLOCK (&frame->lock);

        return call_count;
}

int
shard_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
        int                ret     = 0;
        uint64_t           size    = 0;
        shard_inode_ctx_t  ctx_tmp = {0,};

        if (op_ret < 0)
                goto unwind;

        if ((op_ret == 0) && (!IA_ISDIR (buf->ia_type))) {
                ret = dict_get_uint64 (xdata, GF_XATTR_SHARD_BLOCK_SIZE, &size);
                if (!ret && size) {
                        ctx_tmp.block_size = ntoh64 (size);
                        ctx_tmp.mode = st_mode_from_ia (buf->ia_prot,
                                                        buf->ia_type);
                        ctx_tmp.rdev = buf->ia_rdev;
                        ret = shard_inode_ctx_set_all (inode, this, &ctx_tmp);
                        if (ret)
                                gf_log (this->name, GF_LOG_WARNING, "Failed to "
                                        "set inode ctx for %s",
                                        uuid_utoa (inode->gfid));
                }
        }

        /* To-Do: return the call with aggregated values of ia_size and
         * ia_blocks
         */

unwind:
        SHARD_STACK_UNWIND (lookup, frame, op_ret, op_errno, inode, buf,
                            xdata, postparent);
        return 0;
}

int
shard_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
              dict_t *xattr_req)
{
        int             ret        = -1;
        int32_t         op_errno   = ENOMEM;
        uint64_t        block_size = 0;
        shard_local_t  *local      = NULL;

        SHARD_ENTRY_FOP_CHECK (loc, op_errno, err);

        local = mem_get0 (this->local_pool);
        if (!local)
                goto err;

        frame->local = local;

        loc_copy (&local->loc, loc);

        local->xattr_req = xattr_req ? dict_ref (xattr_req) : dict_new ();
        if (!local->xattr_req)
                goto err;

        if ((shard_inode_ctx_get_block_size (loc->inode, this, &block_size) ||
            !block_size)) {
                ret = dict_set_uint64 (local->xattr_req,
                                       GF_XATTR_SHARD_BLOCK_SIZE, 0);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "Failed to set dict"
                                " value: key:%s for path %s",
                                GF_XATTR_SHARD_BLOCK_SIZE, loc->path);
                        goto err;
                }
        }

        STACK_WIND (frame, shard_lookup_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->lookup, loc, local->xattr_req);

        return 0;


err:
        SHARD_STACK_UNWIND (lookup, frame, -1, op_errno, NULL, NULL,
                             NULL, NULL);
        return 0;

}

int
shard_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *buf,
                dict_t *xdata)
{
        /* To-Do: Update ia_size and ia_blocks in @buf before presenting it
         * to the parent.
         */

        SHARD_STACK_UNWIND (stat, frame, op_ret, op_errno, buf, xdata);
        return 0;
}

int
shard_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        STACK_WIND (frame, shard_stat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat, loc, xdata);

        return 0;
}

int
shard_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *buf,
                 dict_t *xdata)
{
        /* To-Do: Update ia_size and ia_blocks in @buf before presenting it
         * to the parent.
         */
        SHARD_STACK_UNWIND (fstat, frame, op_ret, op_errno, buf, xdata);
        return 0;
}

int
shard_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        STACK_WIND (frame, shard_fstat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fstat, fd, xdata);
        return 0;
}

int
shard_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
                dict_t *xdata)
{
        /*TBD - once the ability to determine the size/number of shards for a
         * file in place, this can be filled.
         */
        SHARD_STACK_UNWIND (truncate, frame, -1, ENOTCONN, NULL, NULL, NULL);
        return 0;
}

int
shard_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                 dict_t *xdata)
{
        /*TBD - once the ability to determine the size/number of shards for a
         * file in place, this can be filled.
         */
        SHARD_STACK_UNWIND (ftruncate, frame, -1, ENOTCONN, NULL, NULL, NULL);
        return 0;
}

int
shard_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *buf, struct iatt *preparent,
                 struct iatt *postparent, dict_t *xdata)
{
        int                ret     = -1;
        shard_local_t     *local   = NULL;
        shard_inode_ctx_t  ctx_tmp = {0,};

        local = frame->local;

        if (op_ret == -1)
                goto unwind;

        ctx_tmp.block_size = ntoh64 (local->block_size);
        ctx_tmp.mode = st_mode_from_ia (buf->ia_prot, buf->ia_type);
        ctx_tmp.rdev = buf->ia_rdev;
        ret = shard_inode_ctx_set_all (inode, this, &ctx_tmp);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING, "Failed to set inode ctx "
                        "for %s", uuid_utoa (inode->gfid));

unwind:
        SHARD_STACK_UNWIND (mknod, frame, op_ret, op_errno, inode, buf,
                            preparent, postparent, xdata);

        return 0;
}

int
shard_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
             dev_t rdev, mode_t umask, dict_t *xdata)
{
        int             ret        = -1;
        int32_t         op_errno   = ENOMEM;
        shard_local_t  *local      = NULL;
        shard_priv_t   *priv       = NULL;

        priv = this->private;

        local = mem_get0 (this->local_pool);
        if (!local)
                goto err;

        frame->local = local;

        local->block_size = hton64 (priv->block_size);

        ret = dict_set_static_bin (xdata, GF_XATTR_SHARD_BLOCK_SIZE,
                                   &local->block_size, sizeof (uint64_t));
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "Failed to set key: %s "
                        "on path %s", GF_XATTR_SHARD_BLOCK_SIZE, loc->path);
                goto err;
        }

        STACK_WIND (frame, shard_mknod_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->mknod, loc, mode, rdev, umask,
                    xdata);
        return 0;


err:
        SHARD_STACK_UNWIND (mknod, frame, -1, op_errno, NULL, NULL, NULL,
                            NULL, NULL);
        return 0;

}

int
shard_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
        /* To-Do: Unlink all the shards associated with the file */

        SHARD_STACK_UNWIND (unlink, frame, op_ret, op_errno,  preparent,
                            postparent, xdata);

        return 0;
}

int
shard_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
              dict_t *xdata)
{
        int             op_errno = ENOMEM;
        shard_local_t  *local    = NULL;

        local = mem_get0 (this->local_pool);
        if (!local)
                goto err;

        frame->local = local;

        loc_copy (&local->loc, loc);
        local->xflag = xflag;

        if (xdata)
                local->xattr_req = dict_ref (xdata);

        STACK_WIND (frame, shard_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink, loc, xflag, xdata);

        return 0;
err:
        SHARD_STACK_UNWIND (unlink, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;

}


int
shard_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *buf,
                  struct iatt *preoldparent, struct iatt *postoldparent,
                  struct iatt *prenewparent, struct iatt *postnewparent,
                  dict_t *xdata)
{
        /* To-Do: When both src and dst names exist, and src is renamed to
         * dst, all the shards associated with the dst file must be unlinked.
         */
        SHARD_STACK_UNWIND (rename, frame, op_ret, op_errno, buf,
                            preoldparent, postoldparent, prenewparent,
                            postnewparent, xdata);
        return 0;
}

int
shard_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
              dict_t *xdata)
{
        int32_t         op_errno = ENOMEM;
        shard_local_t  *local    = NULL;

        local = mem_get0 (this->local_pool);
        if (!local)
                goto err;

        frame->local = local;

        STACK_WIND (frame, shard_rename_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename, oldloc, newloc, xdata);
        return 0;

err:
        SHARD_STACK_UNWIND (rename, frame, -1, op_errno, NULL, NULL, NULL,
                             NULL, NULL, NULL);
        return 0;

}


int
shard_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                  struct iatt *stbuf, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
        int             ret        = -1;
        shard_local_t  *local      = NULL;
        shard_inode_ctx_t ctx_tmp = {0,};

        local = frame->local;

        if (op_ret == -1)
                goto unwind;

        ctx_tmp.block_size = ntoh64 (local->block_size);
        ctx_tmp.mode = st_mode_from_ia (stbuf->ia_prot, stbuf->ia_type);
        ctx_tmp.rdev = stbuf->ia_rdev;
        ret = shard_inode_ctx_set_all (inode, this, &ctx_tmp);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "Failed to set block size "
                        "for %s in inode ctx", uuid_utoa (inode->gfid));
                goto unwind;
        }

unwind:
        SHARD_STACK_UNWIND (create, frame, op_ret, op_errno, fd, inode, stbuf,
                            preparent, postparent, xdata);
        return 0;
}

int
shard_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
              mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        int             ret        = -1;
        int32_t         op_errno   = ENOMEM;
        shard_local_t  *local      = NULL;
        shard_priv_t   *priv       = NULL;

        priv = this->private;

        local = mem_get0 (this->local_pool);
        if (!local)
                goto err;

        frame->local = local;

        local->block_size = hton64 (priv->block_size);

        ret = dict_set_static_bin (xdata, GF_XATTR_SHARD_BLOCK_SIZE,
                                   &local->block_size, sizeof (uint64_t));
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "Failed to set key: %s "
                        "on path %s", GF_XATTR_SHARD_BLOCK_SIZE, loc->path);
                goto err;
        }

        STACK_WIND (frame, shard_create_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->create, loc, flags, mode, umask,
                    fd, xdata);
        return 0;

err:
        SHARD_STACK_UNWIND (create, frame, -1, op_errno, NULL, NULL, NULL,
                             NULL, NULL, NULL);
        return 0;

}

int
shard_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        /* To-Do: Handle open with O_TRUNC under locks */
        SHARD_STACK_UNWIND (open, frame, op_ret, op_errno, fd, xdata);
        return 0;
}

int
shard_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
            fd_t *fd, dict_t *xdata)
{
        STACK_WIND (frame, shard_open_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open, loc, flags, fd, xdata);
        return 0;
}

/* - Incomplete - TBD */
int
shard_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset, uint32_t flags, dict_t *xdata)
{
/*
        int             i                       = 0;
        int32_t         op_errno                = ENOMEM;
        uint64_t        block_size              = 0;
        int             highest_block           = 0;
        int             num_blocks              = 0;
        int             cur_block               = 0;
        char            shard_abspath[PATH_MAX] = {0,};
        off_t           cur_offset              = 0;
        size_t          total_size              = 0;
        fd_t           *cur_fd                  = NULL;
        inode_t        *inode                   = NULL;
        shard_local_t  *local                   = NULL;

        ret = shard_inode_ctx_get_block_size (fd->inode, this, &block_size);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get inode ctx for "
                        "%s", uuid_utoa(fd->inode->gfid));
                goto out;
        }

        if (!block_size) {
                STACK_WIND (frame, default_readv_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readv, fd, size, offset,
                            flags, xdata);
                return 0;
        }

        local = mem_get0 (this->local_pool);
        if (!local)
                goto err;

        frame->local = local;

        local->block_size = block_size;
        local->offset = offset;
        local->len = size;
        local->first_block = get_lowest_block (offset, block_size);
        highest_block = get_highest_block (offset, size, block_size);
        num_blocks = local->num_blocks = highest_block - local->first_block + 1;

        while (num_blocks--) {
                cur_fd = (local->first_block == 0) ? fd_ref (fd) :
                         fd_anonymous (inode);
                cur_offset = (cur_block == local->first_block) ?
                              get_shard_offset(offset, block_size):0;
                STACK_WIND_COOKIE (frame, shard_readv_cbk, cur_fd,
                                   FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->readv, cur_fd,
                                   cur_size, cur_offset, flags, xdata);
        }

        return 0;

err:
        SHARD_STACK_UNWIND (readv, frame, -1, op_errno, NULL, 0, NULL, NULL,
                            NULL);
*/
        SHARD_STACK_UNWIND (readv, frame, -1, ENOTCONN, NULL, 0, NULL, NULL,
                            NULL);
        return 0;

}

int
shard_writev_do_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata)
{
        int             ret        = 0;
        int             call_count = 0;
        fd_t           *anon_fd    = cookie;
        shard_local_t  *local      = NULL;

        local = frame->local;

        if (op_ret < 0) {
                local->op_ret = op_ret;
                local->op_errno = op_errno;
        } else {
                local->written_size += op_ret;
        }

        if (anon_fd)
                fd_unref (anon_fd);

        call_count = shard_call_count_return (frame);
        if (call_count == 0) {
                ret = (local->op_ret < 0) ? local->op_ret : local->written_size;
                SHARD_STACK_UNWIND (writev, frame, ret, local->op_errno, prebuf,
                                    postbuf, xdata);
        }

        return 0;
}

int
shard_writev_do (call_frame_t *frame, xlator_t *this)
{
        int             i                 = 0;
        int             count             = 0;
        int             call_count        = 0;
        int             last_block        = 0;
        uint32_t        cur_block         = 0;
        fd_t           *fd                = NULL;
        fd_t           *anon_fd           = NULL;
        shard_local_t  *local             = NULL;
        struct iovec   *vec               = NULL;
        gf_boolean_t    wind_failed       = _gf_false;
        off_t           orig_offset       = 0;
        off_t           shard_offset      = 0;
        off_t           vec_offset        = 0;
        size_t          remaining_size    = 0;
        size_t          write_size        = 0;

        local = frame->local;
        fd = local->fd;

        orig_offset = local->offset;
        remaining_size = local->total_size;
        cur_block = local->first_block;
        local->call_count = call_count = local->num_blocks;
        last_block = local->last_block;

        while (cur_block <= last_block) {
                if (wind_failed) {
                        shard_writev_do_cbk (frame, (void *) (long) 0, this, -1,
                                             ENOMEM, NULL, NULL, NULL);
                        goto next;
                }

                shard_offset = orig_offset % local->block_size;
                write_size = local->block_size - shard_offset;
                if (write_size > remaining_size)
                        write_size = remaining_size;

                remaining_size -= write_size;

                count = iov_subset (local->vector, local->count, vec_offset,
                                    vec_offset + write_size, NULL);

                vec = GF_CALLOC (count, sizeof (struct iovec),
                                 gf_shard_mt_iovec);
                if (!vec) {
                        local->op_ret = -1;
                        local->op_errno = ENOMEM;
                        wind_failed = _gf_true;
                        GF_FREE (vec);
                        shard_writev_do_cbk (frame, (void *) (long) 0, this, -1,
                                             ENOMEM, NULL, NULL, NULL);
                        goto next;
                }

                count = iov_subset (local->vector, local->count, vec_offset,
                                    vec_offset + write_size, vec);

                if (cur_block == 0) {
                        anon_fd = fd_ref (fd);
                } else {
                        anon_fd = fd_anonymous (local->inode_list[i]);
                        if (!anon_fd) {
                                local->op_ret = -1;
                                local->op_errno = ENOMEM;
                                wind_failed = _gf_true;
                                GF_FREE (vec);
                                shard_writev_do_cbk (frame,
                                                     (void *) (long) anon_fd,
                                                     this, -1, ENOMEM, NULL,
                                                     NULL, NULL);
                                goto next;
                        }
                }

                STACK_WIND_COOKIE (frame, shard_writev_do_cbk, anon_fd,
                                   FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->writev, anon_fd,
                                   vec, count, shard_offset, local->flags,
                                   local->iobref, local->xattr_req);
                GF_FREE (vec);
                vec = NULL;
                orig_offset += write_size;
                vec_offset += write_size;
next:
                cur_block++;
                i++;
                call_count--;
        }
        return 0;
}

void
shard_link_block_inode (shard_local_t *local, int block_num, inode_t *inode,
                        struct iatt *buf)
{
        char            block_bname[256] = {0,};
        inode_t        *linked_inode     = NULL;
        shard_priv_t   *priv             = NULL;

        priv = THIS->private;

        shard_make_block_bname (block_num, local->fd->inode->gfid, block_bname,
                                sizeof (block_bname));

        linked_inode = inode_link (inode, priv->dot_shard_inode, block_bname,
                                   buf);
        inode_lookup (linked_inode);
        local->inode_list[block_num - local->first_block] = linked_inode;
        /* Defer unref'ing the inodes until write is complete to prevent
         * them from getting purged. These inodes are unref'd in the event of
         * a failure or after successfull fop completion in shard_local_wipe().
         */
}

int
shard_writev_lookup_shards_cbk (call_frame_t *frame, void *cookie,
                                xlator_t *this, int32_t op_ret,
                                int32_t op_errno, inode_t *inode,
                                struct iatt *buf, dict_t *xdata,
                                struct iatt *postparent)
{
        int             call_count      = 0;
        int             shard_block_num = (long) cookie;
        shard_local_t  *local           = NULL;

        local = frame->local;

        if (op_ret < 0) {
                local->op_ret = op_ret;
                local->op_errno = op_errno;
                goto done;
        }

        shard_link_block_inode (local, shard_block_num, inode, buf);

done:
        call_count = shard_call_count_return (frame);
        if (call_count == 0) {
                if (local->op_ret < 0)
                        goto unwind;
                else
                        shard_writev_do (frame, this);
        }
        return 0;

unwind:
        SHARD_STACK_UNWIND (writev, frame, local->op_ret, local->op_errno, NULL,
                            NULL, NULL);
        return 0;
}

dict_t*
shard_create_gfid_dict (dict_t *dict)
{
        int    ret   = 0;
        dict_t *new  = NULL;
        uuid_t *gfid = NULL;

        new = dict_copy_with_ref (dict, NULL);
        if (!new)
                return NULL;

        gfid = GF_CALLOC (1, sizeof (uuid_t), gf_common_mt_char);
        if (!gfid) {
                ret = -1;
                goto out;
        }

        uuid_generate (*gfid);

        ret = dict_set_dynptr (new, "gfid-req", gfid, sizeof (uuid_t));

out:
        if (ret) {
                dict_unref (new);
                new = NULL;
                GF_FREE (gfid);
        }

        return new;
}

int
shard_writev_lookup_shards (call_frame_t *frame, xlator_t *this)
{
        int            i              = 0;
        int            ret            = 0;
        int            call_count     = 0;
        int32_t        shard_idx_iter = 0;
        int            last_block     = 0;
        char           path[PATH_MAX] = {0,};
        char          *bname          = NULL;
        loc_t          loc            = {0,};
        fd_t          *fd             = NULL;
        shard_local_t *local          = NULL;
        shard_priv_t  *priv           = NULL;
        gf_boolean_t   wind_failed    = _gf_false;
        dict_t        *xattr_req      = NULL;

        priv = this->private;
        local = frame->local;
        fd = local->fd;
        call_count = local->call_count;
        shard_idx_iter = local->first_block;
        last_block = local->last_block;

        while (shard_idx_iter <= last_block) {
                if (local->inode_list[i]) {
                        i++;
                        shard_idx_iter++;
                        continue;
                }

                if (wind_failed) {
                        shard_writev_lookup_shards_cbk (frame,
                                                 (void *) (long) shard_idx_iter,
                                                        this, -1, ENOMEM, NULL,
                                                        NULL, NULL, NULL);
                        goto next;
                }

                shard_make_block_abspath (shard_idx_iter, fd->inode->gfid,
                                          path, sizeof(path));

                bname = strrchr (path, '/') + 1;
                loc.inode = inode_new (this->itable);
                loc.parent = inode_ref (priv->dot_shard_inode);
                ret = inode_path (loc.parent, bname, (char **) &(loc.path));
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "Inode path failed on"
                                " %s", bname);
                        local->op_ret = -1;
                        local->op_errno = ENOMEM;
                        loc_wipe (&loc);
                        shard_writev_lookup_shards_cbk (frame,
                                                 (void *) (long) shard_idx_iter,
                                                        this, -1, ENOMEM, NULL,
                                                        NULL, NULL, NULL);
                        goto next;
                }

                loc.name = strrchr (loc.path, '/');
                if (loc.name)
                        loc.name++;

                xattr_req = shard_create_gfid_dict (local->xattr_req);
                if (!xattr_req) {
                        local->op_ret = -1;
                        local->op_errno = ENOMEM;
                        wind_failed = _gf_true;
                        loc_wipe (&loc);
                        shard_writev_lookup_shards_cbk (frame,
                                                 (void *) (long) shard_idx_iter,
                                                        this, -1, ENOMEM, NULL,
                                                        NULL, NULL, NULL);
                        goto next;
                }

                STACK_WIND_COOKIE (frame, shard_writev_lookup_shards_cbk,
                                   (void *) (long) shard_idx_iter,
                                   FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->lookup, &loc,
                                   xattr_req);
                loc_wipe (&loc);
                dict_unref (xattr_req);
next:
                shard_idx_iter++;
                i++;

                if (!--call_count)
                        break;
        }

        return 0;
}

int
shard_writev_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, inode_t *inode,
                        struct iatt *buf, struct iatt *preparent,
                        struct iatt *postparent, dict_t *xdata)
{
        int             shard_block_num  = (long) cookie;
        int             call_count       = 0;
        shard_local_t  *local            = NULL;

        local = frame->local;

        if (op_ret < 0) {
                if (op_errno == EEXIST) {
                        local->eexist_count++;
                } else {
                        local->op_ret = op_ret;
                        local->op_errno = op_errno;
                }
                gf_log (this->name, GF_LOG_DEBUG, "SHARD WRITEV: mknod of "
                        "shard %d failed: %s", shard_block_num,
                        strerror (op_errno));
                goto done;
        }

        shard_link_block_inode (local, shard_block_num, inode, buf);

done:
        call_count = shard_call_count_return (frame);
        if (call_count == 0) {
                if (local->op_ret < 0) {
                        goto unwind;
                } else {
                        if (!local->eexist_count) {
                                shard_writev_do (frame, this);
                        } else {
                                local->call_count = local->eexist_count;
                                shard_writev_lookup_shards (frame, this);
                        }
                }
        }
        return 0;

unwind:
        SHARD_STACK_UNWIND (writev, frame, local->op_ret, local->op_errno, NULL,
                            NULL, NULL);
        return 0;
}

int
shard_writev_resume_mknod (call_frame_t *frame, xlator_t *this)
{
        int                 i              = 0;
        int                 shard_idx_iter = 0;
        int                 last_block     = 0;
        int                 ret            = 0;
        int                 call_count     = 0;
        char                path[PATH_MAX] = {0,};
        char               *bname          = NULL;
        shard_priv_t       *priv           = NULL;
        shard_inode_ctx_t   ctx_tmp        = {0,};
        shard_local_t      *local          = NULL;
        gf_boolean_t        wind_failed    = _gf_false;
        fd_t               *fd             = NULL;
        loc_t               loc            = {0,};
        dict_t             *xattr_req      = NULL;

        local = frame->local;
        priv = this->private;
        fd = local->fd;
        shard_idx_iter = local->first_block;
        last_block = local->last_block;
        call_count = local->call_count;

        ret = shard_inode_ctx_get_all (fd->inode, this, &ctx_tmp);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get inode ctx for"
                        " %s", uuid_utoa (fd->inode->gfid));
                goto err;
        }

        while (shard_idx_iter <= last_block) {
                if (local->inode_list[i]) {
                        shard_idx_iter++;
                        i++;
                        continue;
                }

                if (wind_failed) {
                        shard_writev_mknod_cbk (frame,
                                                (void *) (long) shard_idx_iter,
                                                this, -1, ENOMEM, NULL, NULL,
                                                NULL, NULL, NULL);
                        goto next;
                }

                shard_make_block_abspath (shard_idx_iter, fd->inode->gfid,
                                          path, sizeof(path));

                xattr_req = shard_create_gfid_dict (local->xattr_req);
                if (!xattr_req) {
                        local->op_ret = -1;
                        local->op_errno = ENOMEM;
                        wind_failed = _gf_true;
                        shard_writev_mknod_cbk (frame,
                                                (void *) (long) shard_idx_iter,
                                                this, -1, ENOMEM, NULL, NULL,
                                                NULL, NULL, NULL);
                        goto next;
                }

                bname = strrchr (path, '/') + 1;
                loc.inode = inode_new (this->itable);
                loc.parent = inode_ref (priv->dot_shard_inode);
                ret = inode_path (loc.parent, bname,
                                       (char **) &(loc.path));
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "Inode path failed on"
                                " %s", bname);
                        local->op_ret = -1;
                        local->op_errno = ENOMEM;
                        wind_failed = _gf_true;
                        loc_wipe (&loc);
                        dict_unref (xattr_req);
                        shard_writev_mknod_cbk (frame,
                                                (void *) (long) shard_idx_iter,
                                                this, -1, ENOMEM, NULL, NULL,
                                                NULL, NULL, NULL);
                        goto next;
                }

                loc.name = strrchr (loc.path, '/');
                if (loc.name)
                        loc.name++;

                STACK_WIND_COOKIE (frame, shard_writev_mknod_cbk,
                                   (void *) (long) shard_idx_iter,
                                   FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->mknod, &loc,
                                   ctx_tmp.mode, ctx_tmp.rdev, 0, xattr_req);
                loc_wipe (&loc);
                dict_unref (xattr_req);

next:
                shard_idx_iter++;
                i++;
                if (!--call_count)
                        break;
        }

        return 0;
err:
        /*
         * This block is for handling failure in shard_inode_ctx_get_all().
         * Failures in the while-loop are handled within the loop.
         */
        SHARD_STACK_UNWIND (writev, frame, -1, ENOMEM, NULL, NULL, NULL);
        return 0;
}

int
shard_writev_create_write_shards (call_frame_t *frame, xlator_t *this)
{
        int            i              = -1;
        uint32_t       shard_idx_iter = 0;
        char           path[PATH_MAX] = {0,};
        fd_t          *fd             = NULL;
        inode_t       *inode          = NULL;
        shard_local_t *local          = NULL;

        local = frame->local;
        fd = local->fd;
        shard_idx_iter = local->first_block;

        while (shard_idx_iter <= local->last_block) {
                i++;
                if (shard_idx_iter == 0) {
                        local->inode_list[i] = inode_ref (fd->inode);
                        shard_idx_iter++;
                        continue;
                }

                shard_make_block_abspath (shard_idx_iter, fd->inode->gfid,
                                          path, sizeof(path));

                inode = NULL;
                inode = inode_resolve (this->itable, path);
                if (inode) {
                        gf_log (this->name, GF_LOG_DEBUG, "Shard %d already "
                                "present. gfid=%s. Saving inode for future.",
                                shard_idx_iter, uuid_utoa(inode->gfid));
                        shard_idx_iter++;
                        local->inode_list[i] = inode;
                        /* Let the ref on the inodes that are already present
                         * in inode table still be held so that they don't get
                         * forgotten by the time the fop reaches the actual
                         * write stage.
                         */
                         continue;
                } else {
                        local->call_count++;
                        shard_idx_iter++;
                }
        }

        if (local->call_count)
                shard_writev_resume_mknod (frame, this);
        else
                shard_writev_do (frame, this);

        return 0;
}

static void
shard_link_dot_shard_inode (shard_local_t *local, inode_t *inode,
                            struct iatt *buf)
{
        inode_t       *linked_inode = NULL;
        shard_priv_t  *priv         = NULL;

        priv = THIS->private;

        linked_inode = inode_link (inode, local->dot_shard_loc.parent,
                                   local->dot_shard_loc.name, buf);
        inode_lookup (linked_inode);
        priv->dot_shard_inode = linked_inode;
}

int
shard_lookup_dot_shard_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno, inode_t *inode,
                            struct iatt *buf, dict_t *xdata,
                            struct iatt *postparent)
{
        shard_local_t *local = NULL;

        local = frame->local;

        if (op_ret)
                goto unwind;

        if (!IA_ISDIR (buf->ia_type)) {
                gf_log (this->name, GF_LOG_CRITICAL, "/.shard already exists "
                        "and is not a directory. Please remove /.shard from all"
                        " bricks and try again");
                op_errno = EIO;
                goto unwind;
        }

        shard_link_dot_shard_inode (local, inode, buf);
        shard_writev_create_write_shards (frame, this);
        return 0;

unwind:
        SHARD_STACK_UNWIND (writev, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

int
shard_lookup_dot_shard (call_frame_t *frame, xlator_t *this)
{
        int                 ret       = -1;
        dict_t             *xattr_req = NULL;
        shard_priv_t       *priv      = NULL;
        shard_local_t      *local     = NULL;

        local = frame->local;
        priv = this->private;

        xattr_req = dict_new ();
        if (!xattr_req)
                goto err;

        ret = dict_set_static_bin (xattr_req, "gfid-req", priv->dot_shard_gfid,
                                   16);
        if (!ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set gfid of "
                        "/.shard into dict");
                goto err;
        }

        STACK_WIND (frame, shard_lookup_dot_shard_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, &local->dot_shard_loc,
                    xattr_req);
        dict_unref (xattr_req);
        return 0;

err:
        if (xattr_req)
                dict_unref (xattr_req);
        SHARD_STACK_UNWIND (writev, frame, -1, ENOMEM, NULL, NULL, NULL);
        return 0;
}

int
shard_writev_mkdir_dot_shard_cbk (call_frame_t *frame, void *cookie,
                                  xlator_t *this, int32_t op_ret,
                                  int32_t op_errno, inode_t *inode,
                                  struct iatt *buf, struct iatt *preparent,
                                  struct iatt *postparent, dict_t *xdata)
{
        shard_local_t *local = NULL;

        local = frame->local;

        if (op_ret == -1) {
                if (op_errno != EEXIST) {
                        goto unwind;
                } else {
                        gf_log (this->name, GF_LOG_DEBUG, "mkdir on /.shard "
                                "failed with EEXIST. Attempting lookup now");
                        shard_lookup_dot_shard (frame, this);
                        return 0;
                }
        }

        shard_link_dot_shard_inode (local, inode, buf);
        shard_writev_create_write_shards (frame, this);
        return 0;

unwind:
        SHARD_STACK_UNWIND (writev, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

int
shard_writev_mkdir_dot_shard (call_frame_t *frame, xlator_t *this)
{
        int             ret           = -1;
        shard_local_t  *local         = NULL;
        shard_priv_t   *priv          = NULL;
        loc_t          *dot_shard_loc = NULL;
        dict_t         *xattr_req     = NULL;

        local = frame->local;
        priv = this->private;

        xattr_req = dict_new ();
        if (!xattr_req)
                goto err;

        dot_shard_loc = &local->dot_shard_loc;

        dot_shard_loc->inode = inode_new (this->itable);
        dot_shard_loc->parent = inode_ref (this->itable->root);
        ret = inode_path (dot_shard_loc->parent, GF_SHARD_DIR,
                                          (char **)&dot_shard_loc->path);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "Inode path failed on"
                        " %s", GF_SHARD_DIR);
                goto err;
        }

        dot_shard_loc->name = strrchr (dot_shard_loc->path, '/');
        if (dot_shard_loc->name)
                dot_shard_loc->name++;

        ret = dict_set_static_bin (xattr_req, "gfid-req", priv->dot_shard_gfid,
                                   16);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set gfid-req for "
                        "/.shard");
                goto err;
        }

        STACK_WIND (frame, shard_writev_mkdir_dot_shard_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mkdir, &local->dot_shard_loc,
                    0755, 0, xattr_req);
        dict_unref (xattr_req);
        return 0;

err:
        if (xattr_req)
                dict_unref (xattr_req);
        SHARD_STACK_UNWIND (writev, frame, -1, ENOMEM, NULL, NULL, NULL);
        return 0;
}

/* shard_writev - still a WIP */
int
shard_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
              struct iovec *vector, int32_t count, off_t offset, uint32_t flags,
              struct iobref *iobref, dict_t *xdata)
{
        int             i              = 0;
        uint64_t        block_size     = 0;
        uint32_t        first_block    = 0;
        uint32_t        last_block     = 0;
        uint32_t        num_blocks     = 0;
        size_t          total_size     = 0;
        shard_local_t  *local          = NULL;
        shard_priv_t   *priv           = NULL;

        priv = this->private;

        if (shard_inode_ctx_get_block_size (fd->inode, this, &block_size)) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get inode ctx for "
                        "%s", uuid_utoa(fd->inode->gfid));
                goto out;
        }

        for (i = 0; i < count; i++)
                total_size += vector[i].iov_len;

        first_block = get_lowest_block (offset, block_size);
        last_block = get_highest_block (offset, total_size, block_size);
        num_blocks = last_block - first_block + 1;

        gf_log (this->name, GF_LOG_TRACE, "gfid=%s first_block=%"PRIu32" "
                "last_block=%"PRIu32" num_blocks=%"PRIu32" offset=%"PRId64" "
                "total_size=%lu", uuid_utoa (fd->inode->gfid), first_block,
                last_block, num_blocks, offset, total_size);

        if (!block_size ||
            ((first_block == 0) && (first_block == last_block))) {
                /* To-Do: Replace default_writev_cbk with a specific cbk
                 * that would collect total size and block count before unwind
                 */
                STACK_WIND (frame, default_writev_cbk,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->writev,
                            fd, vector, count, offset, flags, iobref, xdata);
                return 0;
        }

        if (!this->itable)
                this->itable = fd->inode->table;

        local = mem_get0 (this->local_pool);
        if (!local)
                goto out;

        frame->local = local;

        local->xattr_req = (xdata) ? dict_ref (xdata) : dict_new ();
        if (!local->xattr_req)
                goto out;

        local->vector = iov_dup (vector, count);
        if (!local->vector)
                goto out;

        local->count = count;
        local->offset = offset;
        local->flags = flags;
        local->iobref = iobref_ref (iobref);
        local->fd = fd_ref (fd);
        local->first_block = first_block;
        local->last_block = last_block;
        local->total_size = total_size;
        local->block_size = block_size;
        local->num_blocks = num_blocks;
        local->inode_list = GF_CALLOC (num_blocks, sizeof (inode_t *),
                                       gf_shard_mt_inode_list);
        if (!local->inode_list)
                goto out;

        local->dot_shard_loc.inode = inode_find (this->itable,
                                                 priv->dot_shard_gfid);
        if (!local->dot_shard_loc.inode)
                shard_writev_mkdir_dot_shard (frame, this);
        else
                shard_writev_create_write_shards (frame, this);

        return 0;
out:
        SHARD_STACK_UNWIND (writev, frame, -1, ENOMEM, NULL, NULL, NULL);
        return 0;
}

int
shard_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        /* To-Do: Wind flush on all shards of the file */
        SHARD_STACK_UNWIND (flush, frame, op_ret, op_errno, xdata);
        return 0;
}

int
shard_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        STACK_WIND (frame, shard_flush_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->flush, fd, xdata);
        return 0;
}

int
shard_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                 struct iatt *postbuf, dict_t *xdata)
{
        /* To-Do: Wind fsync on all shards of the file */
        SHARD_STACK_UNWIND (fsync, frame, op_ret, op_errno, prebuf, postbuf,
                            xdata);
        return 0;
}

int
shard_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
             dict_t *xdata)
{
        STACK_WIND (frame, shard_fsync_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsync, fd, datasync, xdata);
        return 0;
}

int32_t
shard_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                   dict_t *xdata)
{
        fd_t          *fd    = NULL;
        gf_dirent_t   *entry = NULL;
        gf_dirent_t   *tmp   = NULL;
        shard_local_t *local = NULL;
        gf_dirent_t    skipped;

        INIT_LIST_HEAD (&skipped.list);

        local = frame->local;
        fd = local->fd;

        if (op_ret < 0)
                goto unwind;

        list_for_each_entry_safe (entry, tmp, &entries->list, list) {
                if (__is_root_gfid (fd->inode->gfid) &&
                    !(strcmp (entry->d_name, GF_SHARD_DIR))) {
                        list_del_init (&entry->list);
                        list_add_tail (&entry->list, &skipped.list);
                        break;
                }
        }

unwind:
        SHARD_STACK_UNWIND (readdir, frame, op_ret, op_errno, entries, xdata);
        gf_dirent_free (&skipped);
        return 0;
}


int
shard_readdir_do (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
               off_t offset, int whichop, dict_t *xdata)
{
        int             op_errno = ENOMEM;
        shard_local_t  *local    = NULL;

        local = mem_get0 (this->local_pool);
        if (!local)
                goto err;

        frame->local = local;

        local->fd = fd_ref (fd);

        if (whichop == GF_FOP_READDIR)
                STACK_WIND (frame, shard_readdir_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readdir, fd, size, offset,
                            xdata);
        else
                STACK_WIND (frame, shard_readdir_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readdirp, fd, size, offset,
                            xdata);

        return 0;

err:
        STACK_UNWIND_STRICT (readdir, frame, -1, op_errno, NULL, NULL);
        return 0;

}


int32_t
shard_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
               off_t offset, dict_t *xdata)
{
        shard_readdir_do (frame, this, fd, size, offset, GF_FOP_READDIR, xdata);
        return 0;
}


int32_t
shard_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                off_t offset, dict_t *xdata)
{
        shard_readdir_do (frame, this, fd, size, offset, GF_FOP_READDIRP,
                          xdata);
        return 0;
}

int
shard_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
        /* To-Do: Call fsetattr on all shards. */
        SHARD_STACK_UNWIND (setattr, frame, op_ret, op_errno, prebuf, postbuf,
                            xdata);
        return 0;
}

int
shard_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
               struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        STACK_WIND (frame, shard_setattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setattr, loc, stbuf, valid, xdata);

        return 0;
}

int
shard_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
        /* To-Do: Call fsetattr on all shards. */
        SHARD_STACK_UNWIND (fsetattr, frame, op_ret, op_errno, prebuf, postbuf,
                            xdata);
        return 0;
}

int
shard_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        STACK_WIND (frame, shard_fsetattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetattr, fd, stbuf, valid, xdata);

        return 0;
}

int
shard_fallocate (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 int32_t keep_size, off_t offset, size_t len, dict_t *xdata)
{
        /* TBD */
        SHARD_STACK_UNWIND (fallocate, frame, -1, ENOTCONN, NULL, NULL, NULL);
        return 0;
}

int
shard_discard (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              size_t len, dict_t *xdata)
{
        /* TBD */
        SHARD_STACK_UNWIND (discard, frame, -1, ENOTCONN, NULL, NULL, NULL);
        return 0;
}

int
shard_zerofill (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                off_t len, dict_t *xdata)
{
        /* TBD */
        SHARD_STACK_UNWIND (zerofill, frame, -1, ENOTCONN, NULL, NULL, NULL);
        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_shard_mt_end + 1);

        if (ret != 0) {
                gf_log(this->name, GF_LOG_ERROR, "Memory accounting init"
                       "failed");
                return ret;
        }

        return ret;
}

int
init (xlator_t *this)
{
        int           ret  = -1;
        shard_priv_t *priv = NULL;

        if (!this) {
                gf_log ("shard", GF_LOG_ERROR, "this is NULL. init() failed");
                goto out;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_ERROR, "Dangling volume. "
                        "Check volfile");
                goto out;
        }

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR, "shard not configured with "
                        "exactly one sub-volume. Check volfile");
                goto out;
        }

        priv = GF_CALLOC (1, sizeof (shard_priv_t), gf_shard_mt_priv_t);
        if (!priv)
                goto out;

        GF_OPTION_INIT ("shard-block-size", priv->block_size, size_uint64, out);

        this->local_pool = mem_pool_new (shard_local_t, 128);
        if (!this->local_pool) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR, "Failed to allocate locals "
                        "from mempool");
                goto out;
        }
        uuid_parse (SHARD_ROOT_GFID, priv->dot_shard_gfid);

        this->private = priv;
        ret = 0;
out:
        if  (ret) {
                GF_FREE (priv);
                mem_pool_destroy (this->local_pool);
        }

        return ret;

}

void
fini (xlator_t *this)
{
        shard_priv_t *priv = NULL;

        GF_VALIDATE_OR_GOTO ("shard", this, out);

        mem_pool_destroy (this->local_pool);
        this->local_pool = NULL;

        priv = this->private;
        if (!priv)
                goto out;

        this->private = NULL;
        GF_FREE (priv);

out:
        return;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        int            ret  = -1;
        shard_priv_t  *priv = NULL;

        priv = this->private;

        GF_OPTION_RECONF ("shard-block-size", priv->block_size, options, size,
                          out);

        ret = 0;

out:
        return ret;
}

int
shard_forget (xlator_t *this, inode_t *inode)
{
        uint64_t            ctx_uint = 0;
        shard_inode_ctx_t  *ctx      = NULL;

        inode_ctx_del (inode, this, &ctx_uint);
        if (!ctx_uint)
                return 0;

        ctx = (shard_inode_ctx_t *)ctx_uint;

        GF_FREE (ctx);

        return 0;
}

int
shard_release (xlator_t *this, fd_t *fd)
{
        /* TBD */
        return 0;
}

int
shard_priv_dump (xlator_t *this)
{
        /* TBD */
        return 0;
}

int
shard_releasedir (xlator_t *this, fd_t *fd)
{
        return 0;
}

struct xlator_fops fops = {
        .lookup      = shard_lookup,
        .open        = shard_open,
        .flush       = shard_flush,
        .fsync       = shard_fsync,
        .stat        = shard_stat,
        .fstat       = shard_fstat,
        .readv       = shard_readv,
        .writev      = shard_writev,
        .truncate    = shard_truncate,
        .ftruncate   = shard_ftruncate,
        .setattr     = shard_setattr,
        .fsetattr    = shard_fsetattr,
        .fallocate   = shard_fallocate,
        .discard     = shard_discard,
        .zerofill    = shard_zerofill,
        .readdir     = shard_readdir,
        .readdirp    = shard_readdirp,
        .create      = shard_create,
        .mknod       = shard_mknod,
        .unlink      = shard_unlink,
        .rename      = shard_rename,
};

struct xlator_cbks cbks = {
        .forget  = shard_forget,
        .release = shard_release,
        .releasedir = shard_releasedir,
};

struct xlator_dumpops dumpops = {
        .priv = shard_priv_dump,
};

struct volume_options options[] = {
        {  .key = {"shard-block-size"},
           .type = GF_OPTION_TYPE_SIZET,
           .default_value = "4MB",
           .min = SHARD_MIN_BLOCK_SIZE,
           .max = SHARD_MAX_BLOCK_SIZE,
           .description = "The size unit used to break a file into multiple "
                          "chunks",
        },
        { .key = {NULL} },
};
