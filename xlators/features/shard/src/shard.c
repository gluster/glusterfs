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

        if (gf_uuid_compare (gfid, priv->dot_shard_gfid) == 0)
                return _gf_true;

        return _gf_false;
}

void
shard_make_block_bname (int block_num, uuid_t gfid, char *buf, size_t len)
{
        char gfid_str[GF_UUID_BUF_SIZE] = {0,};

        gf_uuid_unparse (gfid, gfid_str);
        snprintf (buf, len, "%s.%d", gfid_str, block_num);
}

void
shard_make_block_abspath (int block_num, uuid_t gfid, char *filepath,
                          size_t len)
{
        char gfid_str[GF_UUID_BUF_SIZE] = {0,};

        gf_uuid_unparse (gfid, gfid_str);
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
        loc_wipe (&local->loc2);
        loc_wipe (&local->tmp_loc);

        if (local->fd)
                fd_unref (local->fd);

        if (local->xattr_req)
                dict_unref (local->xattr_req);
        if (local->xattr_rsp)
                dict_unref (local->xattr_rsp);

        for (i = 0; i < count; i++) {
                if (!local->inode_list)
                        break;

                if (local->inode_list[i])
                        inode_unref (local->inode_list[i]);
        }

        GF_FREE (local->inode_list);

        GF_FREE (local->vector);
        if (local->iobref)
                iobref_unref (local->iobref);
}

int
shard_modify_size_and_block_count (struct iatt *stbuf, dict_t *dict)
{
        int                  ret       = -1;
        void                *size_attr = NULL;
        uint64_t             size_array[4];

        ret = dict_get_ptr (dict, GF_XATTR_SHARD_FILE_SIZE, &size_attr);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Failed to get "
                        GF_XATTR_SHARD_FILE_SIZE " for %s",
                        uuid_utoa (stbuf->ia_gfid));
                return ret;
        }

        memcpy (size_array, size_attr, sizeof (size_array));

        stbuf->ia_size = ntoh64 (size_array[0]);
        stbuf->ia_blocks = ntoh64 (size_array[2]);

        return 0;
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
        int                ret           = 0;
        uint64_t           size          = 0;
        void              *size_attr     = NULL;
        shard_inode_ctx_t  ctx_tmp       = {0,};
        uint64_t           size_array[4];

        if (op_ret < 0)
                goto unwind;

        if (IA_ISDIR (buf->ia_type))
                goto unwind;

        if (!shard_inode_ctx_get_block_size (inode, this, &size))
                goto unwind;

        ret = dict_get_uint64 (xdata, GF_XATTR_SHARD_BLOCK_SIZE, &size);
        if (!ret) {
                ctx_tmp.block_size = ntoh64 (size);
                ctx_tmp.mode = st_mode_from_ia (buf->ia_prot, buf->ia_type);
                ctx_tmp.rdev = buf->ia_rdev;
                /* Sharding xlator would fetch size and block count only if
                 * @size is present. The absence of GF_XATTR_SHARD_BLOCK_SIZE on
                 * the file looked up could be because it was created before
                 * sharding was enabled on the volume.
                 */
                ret = dict_get_ptr (xdata, GF_XATTR_SHARD_FILE_SIZE,
                                    &size_attr);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "Failed to "
                                "get xattr "GF_XATTR_SHARD_FILE_SIZE" from disk"
                                " for %s", uuid_utoa (inode->gfid));
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto unwind;
                }

                memcpy (size_array, size_attr, sizeof (size_array));

                buf->ia_size = ntoh64 (size_array[0]);
                buf->ia_blocks = ntoh64 (size_array[2]);
        }
        /* else it is assumed that the file was created prior to enabling
         * sharding on the volume.
         */

        ret = shard_inode_ctx_set_all (inode, this, &ctx_tmp);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING, "Failed to set inode ctx "
                        "for %s", uuid_utoa (buf->ia_gfid));

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

        if (shard_inode_ctx_get_block_size (loc->inode, this, &block_size)) {
                ret = dict_set_uint64 (local->xattr_req,
                                       GF_XATTR_SHARD_BLOCK_SIZE, 0);
                if (ret) {
                        gf_log (this->name, GF_LOG_WARNING, "Failed to set dict"
                                " value: key:%s for path %s",
                                GF_XATTR_SHARD_BLOCK_SIZE, loc->path);
                        goto err;
                }
        }

        ret = dict_set_uint64 (local->xattr_req, GF_XATTR_SHARD_FILE_SIZE,
                               8 * 4);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "Failed to set dict value: "
                        "key:%s for path %s.", GF_XATTR_SHARD_FILE_SIZE,
                        loc->path);
                goto err;
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
shard_lookup_base_file_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno, inode_t *inode,
                            struct iatt *buf, dict_t *xdata,
                            struct iatt *postparent)
{
        shard_local_t     *local  = NULL;

        local = frame->local;

        if (op_ret < 0) {
                local->op_ret = op_ret;
                local->op_errno = op_errno;
                goto unwind;
        }

        local->prebuf = *buf;
        if (shard_modify_size_and_block_count (&local->prebuf, xdata)) {
                local->op_ret = -1;
                local->op_errno = EINVAL;
                goto unwind;
        }

unwind:
        local->handler (frame, this);
        return 0;
}

int
shard_lookup_base_file (call_frame_t *frame, xlator_t *this, loc_t *loc,
                        shard_post_fop_handler_t handler)
{
        shard_local_t      *local     = NULL;
        dict_t             *xattr_req = NULL;

        local = frame->local;
        local->handler = handler;

        xattr_req = dict_new ();
        if (!xattr_req) {
                local->op_ret = -1;
                local->op_errno = ENOMEM;
                goto err;
        }

        SHARD_MD_READ_FOP_INIT_REQ_DICT (this, xattr_req, loc->gfid,
                                         local, err);

        STACK_WIND (frame, shard_lookup_base_file_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->lookup, loc, xattr_req);

        dict_unref (xattr_req);
        return 0;

err:
        if (xattr_req)
                dict_unref (xattr_req);
        handler (frame, this);
        return 0;

}

int
shard_post_fstat_handler (call_frame_t *frame, xlator_t *this)
{
        shard_local_t *local = NULL;

        local = frame->local;

        SHARD_STACK_UNWIND (fstat, frame, local->op_ret, local->op_errno,
                            &local->prebuf, local->xattr_rsp);
        return 0;
}

int
shard_post_stat_handler (call_frame_t *frame, xlator_t *this)
{
        shard_local_t *local = NULL;

        local = frame->local;

        SHARD_STACK_UNWIND (stat, frame, local->op_ret, local->op_errno,
                            &local->prebuf, local->xattr_rsp);
        return 0;
}

int
shard_common_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *buf,
                       dict_t *xdata)
{
        shard_local_t *local = NULL;

        local = frame->local;

        if (op_ret < 0) {
                local->op_ret = op_ret;
                local->op_errno = op_errno;
                goto unwind;
        }

        local->prebuf = *buf;
        if (shard_modify_size_and_block_count (&local->prebuf, xdata)) {
                local->op_ret = -1;
                local->op_errno = EINVAL;
                goto unwind;
        }
        local->xattr_rsp = dict_ref (xdata);

unwind:
        local->handler (frame, this);
        return 0;
}

int
shard_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        int                ret        = -1;
        uint64_t           block_size = 0;
        shard_local_t     *local      = NULL;

        if (IA_ISDIR (loc->inode->ia_type)) {
                STACK_WIND (frame, default_stat_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->stat, loc, xdata);
                return 0;
        }

        ret = shard_inode_ctx_get_block_size (loc->inode, this, &block_size);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get block size "
                        "from inode ctx of %s", uuid_utoa (loc->inode->gfid));
                goto err;
        }

        if (!block_size) {
                STACK_WIND (frame, default_stat_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->stat, loc, xdata);
                return 0;
        }

        local = mem_get0 (this->local_pool);
        if (!local)
                goto err;

        frame->local = local;

        local->handler = shard_post_stat_handler;
        local->xattr_req = (xdata) ? dict_ref (xdata) : dict_new ();
        if (!local->xattr_req)
                goto err;

        SHARD_MD_READ_FOP_INIT_REQ_DICT (this, local->xattr_req,
                                         local->loc.gfid, local, err);

        STACK_WIND (frame, shard_common_stat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->stat, loc, local->xattr_req);

        return 0;

err:
        SHARD_STACK_UNWIND (stat, frame, -1, ENOMEM, NULL, NULL);
        return 0;
}

int
shard_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        int                ret        = -1;
        uint64_t           block_size = 0;
        shard_local_t     *local      = NULL;

        if (IA_ISDIR (fd->inode->ia_type)) {
                STACK_WIND (frame, default_fstat_cbk, FIRST_CHILD(this),
                            FIRST_CHILD (this)->fops->fstat, fd, xdata);
                return 0;
        }

        ret = shard_inode_ctx_get_block_size (fd->inode, this, &block_size);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get block size "
                        "from inode ctx of %s", uuid_utoa (fd->inode->gfid));
                goto err;
        }

        if (!block_size) {
                STACK_WIND (frame, default_fstat_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->fstat, fd, xdata);
                return 0;
        }

        if (!this->itable)
                this->itable = fd->inode->table;

        local = mem_get0 (this->local_pool);
        if (!local)
                goto err;

        frame->local = local;

        local->handler = shard_post_fstat_handler;
        local->xattr_req = (xdata) ? dict_ref (xdata) : dict_new ();
        if (!local->xattr_req)
                goto err;

        SHARD_MD_READ_FOP_INIT_REQ_DICT (this, local->xattr_req,
                                         fd->inode->gfid, local, err);

        STACK_WIND (frame, shard_common_stat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fstat, fd, local->xattr_req);
        return 0;

err:
        SHARD_STACK_UNWIND (fstat, frame, -1, ENOMEM, NULL, NULL);
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
shard_set_size_attrs (uint64_t size, uint64_t block_count,
                      uint64_t **size_attr_p)
{
        int             ret       = -1;
        uint64_t       *size_attr = NULL;

        if (!size_attr_p)
                goto out;

        size_attr = GF_CALLOC (4, sizeof (uint64_t), gf_shard_mt_uint64_t);
        if (!size_attr)
                goto out;

        size_attr[0] = hton64 (size);
        /* As sharding evolves, it _may_ be necessary to embed more pieces of
         * information within the same xattr. So allocating slots for them in
         * advance. For now, only bytes 0-63 and 128-191 which would make up the
         * current size and block count respectively of the file are valid.
         */
        size_attr[2] = hton64 (block_count);

        *size_attr_p = size_attr;

        ret = 0;
out:
        return ret;
}

int
shard_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
             dev_t rdev, mode_t umask, dict_t *xdata)
{
        shard_local_t  *local      = NULL;

        local = mem_get0 (this->local_pool);
        if (!local)
                goto err;

        frame->local = local;

        SHARD_INODE_CREATE_INIT (this, local, xdata, loc, err);

        STACK_WIND (frame, shard_mknod_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->mknod, loc, mode, rdev, umask,
                    xdata);
        return 0;

err:
        SHARD_STACK_UNWIND (mknod, frame, -1, ENOMEM, NULL, NULL, NULL,
                            NULL, NULL);
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

        shard_make_block_bname (block_num, (local->loc.inode)->gfid,
                                block_bname, sizeof (block_bname));

        linked_inode = inode_link (inode, priv->dot_shard_inode, block_bname,
                                   buf);
        inode_lookup (linked_inode);
        local->inode_list[block_num - local->first_block] = linked_inode;
        /* Defer unref'ing the inodes until write is complete to prevent
         * them from getting purged. These inodes are unref'd in the event of
         * a failure or after successful fop completion in shard_local_wipe().
         */
}

int
shard_common_lookup_shards_cbk (call_frame_t *frame, void *cookie,
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
                        local->pls_fop_handler (frame, this);
        }
        return 0;

unwind:
        local->pls_fop_handler (frame, this);
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

        gf_uuid_generate (*gfid);

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
shard_common_lookup_shards (call_frame_t *frame, xlator_t *this, inode_t *inode,
                            shard_post_lookup_shards_fop_handler_t handler)
{
        int            i              = 0;
        int            ret            = 0;
        int            call_count     = 0;
        int32_t        shard_idx_iter = 0;
        int            last_block     = 0;
        char           path[PATH_MAX] = {0,};
        char          *bname          = NULL;
        loc_t          loc            = {0,};
        shard_local_t *local          = NULL;
        shard_priv_t  *priv           = NULL;
        gf_boolean_t   wind_failed    = _gf_false;
        dict_t        *xattr_req      = NULL;

        priv = this->private;
        local = frame->local;
        call_count = local->call_count;
        shard_idx_iter = local->first_block;
        last_block = local->last_block;
        local->pls_fop_handler = handler;

        while (shard_idx_iter <= last_block) {
                if (local->inode_list[i]) {
                        i++;
                        shard_idx_iter++;
                        continue;
                }

                if (wind_failed) {
                        shard_common_lookup_shards_cbk (frame,
                                                 (void *) (long) shard_idx_iter,
                                                        this, -1, ENOMEM, NULL,
                                                        NULL, NULL, NULL);
                        goto next;
                }

                shard_make_block_abspath (shard_idx_iter, inode->gfid, path,
                                          sizeof(path));

                bname = strrchr (path, '/') + 1;
                loc.inode = inode_new (this->itable);
                loc.parent = inode_ref (priv->dot_shard_inode);
                ret = inode_path (loc.parent, bname, (char **) &(loc.path));
                if (ret < 0 || !(loc.inode)) {
                        gf_log (this->name, GF_LOG_ERROR, "Inode path failed on"
                                " %s", bname);
                        local->op_ret = -1;
                        local->op_errno = ENOMEM;
                        loc_wipe (&loc);
                        wind_failed = _gf_true;
                        shard_common_lookup_shards_cbk (frame,
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
                        shard_common_lookup_shards_cbk (frame,
                                                 (void *) (long) shard_idx_iter,
                                                        this, -1, ENOMEM, NULL,
                                                        NULL, NULL, NULL);
                        goto next;
                }

                STACK_WIND_COOKIE (frame, shard_common_lookup_shards_cbk,
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
shard_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
        SHARD_STACK_UNWIND (unlink, frame, op_ret, op_errno,  preparent,
                            postparent, xdata);

        return 0;
}

int
shard_unlink_base_file (call_frame_t *frame, xlator_t *this)
{
        shard_local_t *local = NULL;

        local = frame->local;

        if (local->op_ret < 0) {
                shard_unlink_cbk (frame, 0, this, local->op_ret,
                                  local->op_errno, NULL, NULL, NULL);
                return 0;
        }

        STACK_WIND (frame, shard_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink, &local->loc, local->xflag,
                    local->xattr_req);
        return 0;
}

void
shard_unlink_block_inode (shard_local_t *local, int shard_block_num)
{
        char          block_bname[256]  = {0,};
        inode_t      *inode             = NULL;
        shard_priv_t *priv              = NULL;

        priv = THIS->private;

        inode = local->inode_list[shard_block_num - local->first_block];

        shard_make_block_bname (shard_block_num, (local->loc.inode)->gfid,
                                block_bname, sizeof (block_bname));

        inode_unlink (inode, priv->dot_shard_inode, block_bname);
        inode_forget (inode, 0);
}

int
shard_rename_cbk (call_frame_t *frame, xlator_t *this);

int
shard_unlink_shards_do_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno,
                            struct iatt *preparent, struct iatt *postparent,
                            dict_t *xdata)
{
        int            call_count      = 0;
        int            shard_block_num = (long) cookie;
        shard_local_t *local           = NULL;

        local = frame->local;

        if (op_ret < 0) {
                local->op_ret = op_ret;
                local->op_errno = op_errno;
                goto done;
        }

        shard_unlink_block_inode (local, shard_block_num);

done:
        call_count = shard_call_count_return (frame);
        if (call_count == 0) {
                if (local->fop == GF_FOP_UNLINK)
                        shard_unlink_base_file (frame, this);
                else
                        shard_rename_cbk (frame, this);
        }

        return 0;
}

int
shard_unlink_shards_do (call_frame_t *frame, xlator_t *this, inode_t *inode)
{
        int               ret            = -1;
        int               call_count     = 0;
        uint32_t          last_block     = 0;
        uint32_t          cur_block      = 0;
        char             *bname          = NULL;
        char              path[PATH_MAX] = {0,};
        loc_t             loc            = {0,};
        gf_boolean_t      wind_failed    = _gf_false;
        shard_local_t    *local          = NULL;
        shard_priv_t     *priv           = NULL;

        priv = this->private;
        local = frame->local;
        local->call_count = call_count = local->num_blocks - 1;
        last_block = local->last_block;

        while (cur_block <= last_block) {
                /* The base file is unlinked in the end to mark the
                 * successful completion of the fop.
                 */
                if (cur_block == 0) {
                        cur_block++;
                        continue;
                }

                if (wind_failed) {
                        shard_unlink_shards_do_cbk (frame,
                                                    (void *) (long) cur_block,
                                                    this, -1, ENOMEM, NULL,
                                                    NULL, NULL);
                        goto next;
                }

                shard_make_block_abspath (cur_block, inode->gfid, path,
                                          sizeof (path));
                bname = strrchr (path, '/') + 1;
                loc.parent = inode_ref (priv->dot_shard_inode);
                ret = inode_path (loc.parent, bname, (char **) &(loc.path));
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "Inode path failed "
                                "on %s", bname);
                        local->op_ret = -1;
                        local->op_errno = ENOMEM;
                        loc_wipe (&loc);
                        wind_failed = _gf_true;
                        shard_unlink_shards_do_cbk (frame,
                                                    (void *) (long) cur_block,
                                                    this, -1, ENOMEM, NULL,
                                                    NULL, NULL);
                        goto next;
                }

                loc.name = strrchr (loc.path, '/');
                if (loc.name)
                        loc.name++;
                loc.inode = inode_ref (local->inode_list[cur_block]);

                STACK_WIND_COOKIE (frame, shard_unlink_shards_do_cbk,
                                   (void *) (long) cur_block, FIRST_CHILD(this),
                                   FIRST_CHILD (this)->fops->unlink, &loc,
                                   local->xflag, local->xattr_req);
                loc_wipe (&loc);

next:
                cur_block++;
                if (!--call_count)
                        break;
        }

        return 0;
}

int
shard_common_resolve_shards (call_frame_t *frame, xlator_t *this,
                             inode_t *res_inode,
                             shard_post_resolve_fop_handler_t post_res_handler)
{
        int            i              = -1;
        uint32_t       shard_idx_iter = 0;
        char           path[PATH_MAX] = {0,};
        inode_t       *inode          = NULL;
        shard_local_t *local          = NULL;

        local = frame->local;
        shard_idx_iter = local->first_block;

        while (shard_idx_iter <= local->last_block) {
                i++;
                if (shard_idx_iter == 0) {
                        local->inode_list[i] = inode_ref (res_inode);
                        shard_idx_iter++;
                        continue;
                }

                shard_make_block_abspath (shard_idx_iter, res_inode->gfid, path,
                                          sizeof(path));

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

        post_res_handler (frame, this);
        return 0;
}

int
shard_post_lookup_shards_unlink_handler (call_frame_t *frame, xlator_t *this)
{
        shard_local_t *local = NULL;

        local = frame->local;

        if (local->op_ret < 0) {
                if (local->fop == GF_FOP_UNLINK)
                        SHARD_STACK_UNWIND (unlink, frame, local->op_ret,
                                            local->op_errno, NULL, NULL, NULL);
                else
                        SHARD_STACK_UNWIND (rename, frame, local->op_ret,
                                            local->op_errno, NULL, NULL, NULL,
                                            NULL, NULL, NULL);
                return 0;
        }

        shard_unlink_shards_do (frame, this,
                                (local->fop == GF_FOP_RENAME)
                                             ? local->loc2.inode
                                             : local->loc.inode);
        return 0;
}

int
shard_post_resolve_unlink_handler (call_frame_t *frame, xlator_t *this)
{
        shard_local_t *local = NULL;

        local = frame->local;

        if (!local->call_count)
                shard_unlink_shards_do (frame, this,
                                        (local->fop == GF_FOP_RENAME)
                                                     ? local->loc2.inode
                                                     : local->loc.inode);
        else
                shard_common_lookup_shards (frame, this,
                                            (local->fop == GF_FOP_RENAME)
                                                         ? local->loc2.inode
                                                         : local->loc.inode,
                                       shard_post_lookup_shards_unlink_handler);
        return 0;
}

int
shard_post_lookup_unlink_handler (call_frame_t *frame, xlator_t *this)
{
        shard_local_t *local = NULL;

        local = frame->local;

        if (local->op_ret < 0) {
                SHARD_STACK_UNWIND (unlink, frame, local->op_ret,
                                    local->op_errno, NULL, NULL, NULL);
                return 0;
        }

        local->first_block = get_lowest_block (0, local->block_size);
        local->last_block = get_highest_block (0, local->prebuf.ia_size,
                                               local->block_size);
        local->num_blocks = local->last_block - local->first_block + 1;

        if ((local->num_blocks == 1) || (local->prebuf.ia_nlink > 1)) {
                /* num_blocks = 1 implies that the file has not crossed its
                 * shard block size. So unlink boils down to unlinking just the
                 * base file.
                 * Because link() does not create links for all but the
                 * base shard, unlink() must delete these shards only when the
                 * link count is 1.
                 */
                STACK_WIND (frame, shard_unlink_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->unlink, &local->loc,
                            local->xflag, local->xattr_req);
                return 0;
        }

        local->inode_list = GF_CALLOC (local->num_blocks, sizeof (inode_t *),
                                       gf_shard_mt_inode_list);
        if (!local->inode_list)
                goto out;

        shard_common_resolve_shards (frame, this, local->loc.inode,
                                     shard_post_resolve_unlink_handler);
        return 0;

out:
        SHARD_STACK_UNWIND (unlink, frame, -1, ENOMEM, NULL, NULL, NULL);
        return 0;
}

int
shard_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
              dict_t *xdata)
{
        int             ret        = -1;
        uint64_t        block_size = 0;
        shard_local_t  *local      = NULL;

        ret = shard_inode_ctx_get_block_size (loc->inode, this, &block_size);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get block size "
                        "from inode ctx of %s", uuid_utoa (loc->inode->gfid));
                goto err;
        }

        if (!block_size) {
                STACK_WIND (frame, default_unlink_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->unlink, loc, xflag, xdata);
                return 0;
        }

        local = mem_get0 (this->local_pool);
        if (!local)
                goto err;

        frame->local = local;

        loc_copy (&local->loc, loc);
        local->xflag = xflag;
        local->xattr_req = (xdata) ? dict_ref (xdata) : dict_new ();
        local->block_size = block_size;
        local->fop = GF_FOP_UNLINK;

        shard_lookup_base_file (frame, this, &local->loc,
                                shard_post_lookup_unlink_handler);

        return 0;
err:
        SHARD_STACK_UNWIND (unlink, frame, -1, ENOMEM, NULL, NULL, NULL);
        return 0;

}

int
shard_rename_cbk (call_frame_t *frame, xlator_t *this)
{
        shard_local_t *local = NULL;

        local = frame->local;

        SHARD_STACK_UNWIND (rename, frame, local->op_ret, local->op_errno,
                            &local->prebuf, &local->preoldparent,
                            &local->postoldparent, &local->prenewparent,
                            &local->postnewparent, local->xattr_rsp);
        return 0;
}

int
shard_rename_unlink_dst_shards_do (call_frame_t *frame, xlator_t *this)
{
        shard_local_t *local = NULL;

        local = frame->local;

        local->first_block = get_lowest_block (0, local->dst_block_size);
        local->last_block = get_highest_block (0, local->postbuf.ia_size,
                                               local->dst_block_size);
        local->num_blocks = local->last_block - local->first_block + 1;

        if ((local->num_blocks == 1) || (local->postbuf.ia_nlink > 1)) {
                shard_rename_cbk (frame, this);
                return 0;
        }

        local->inode_list = GF_CALLOC (local->num_blocks, sizeof (inode_t *),
                                       gf_shard_mt_inode_list);
        if (!local->inode_list)
                goto out;

        shard_common_resolve_shards (frame, this, local->loc2.inode,
                                     shard_post_resolve_unlink_handler);

        return 0;

out:
        SHARD_STACK_UNWIND (rename, frame, -1, ENOMEM, NULL, NULL, NULL, NULL,
                            NULL, NULL);
        return 0;
}

int
shard_post_rename_lookup_handler (call_frame_t *frame, xlator_t *this)
{
        shard_local_t *local = NULL;

        local = frame->local;

        if (local->op_ret < 0) {
                SHARD_STACK_UNWIND (rename, frame, local->op_ret,
                                    local->op_errno, NULL, NULL, NULL, NULL,
                                    NULL, NULL);
                return 0;
        }

        if (local->dst_block_size)
                shard_rename_unlink_dst_shards_do (frame, this);
        else
                shard_rename_cbk (frame, this);

        return 0;
}

int
shard_rename_src_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *buf,
                      struct iatt *preoldparent, struct iatt *postoldparent,
                      struct iatt *prenewparent, struct iatt *postnewparent,
                      dict_t *xdata)
{
        shard_local_t *local = NULL;

        local = frame->local;

        if (op_ret < 0) {
                local->op_ret = op_ret;
                local->op_errno = op_errno;
                goto err;
        }

        local->prebuf = *buf;
        local->preoldparent = *preoldparent;
        local->postoldparent = *postoldparent;
        local->prenewparent = *prenewparent;
        local->postnewparent = *postnewparent;
        local->xattr_rsp = dict_ref (xdata);

        /* Now the base file is looked up to gather the ia_size and ia_blocks.*/

        if (local->block_size) {
                local->tmp_loc.inode = inode_new (this->itable);
                gf_uuid_copy (local->tmp_loc.gfid, (local->loc.inode)->gfid);
                shard_lookup_base_file (frame, this, &local->tmp_loc,
                                        shard_post_rename_lookup_handler);
        } else {
                shard_rename_unlink_dst_shards_do (frame, this);
        }

        return 0;
err:
        SHARD_STACK_UNWIND (rename, frame, local->op_ret, local->op_errno, NULL,
                            NULL, NULL, NULL, NULL, NULL);
        return 0;
}

int
shard_rename_src_base_file (call_frame_t *frame, xlator_t *this)
{
        shard_local_t *local = NULL;

        local = frame->local;

        STACK_WIND (frame, shard_rename_src_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename, &local->loc, &local->loc2,
                    local->xattr_req);
        return 0;
}

int
shard_post_lookup_dst_base_file_handler (call_frame_t *frame, xlator_t *this)
{
        shard_local_t *local = NULL;

        local = frame->local;

        if (local->op_ret < 0) {
                SHARD_STACK_UNWIND (rename, frame, local->op_ret,
                                    local->op_errno, NULL, NULL, NULL, NULL,
                                    NULL, NULL);
                return 0;
        }

        /* Save dst base file attributes into postbuf so the information is not
         * lost when it is overwritten after lookup on base file of src in
         * shard_lookup_base_file_cbk().
         */
        local->postbuf = local->prebuf;
        shard_rename_src_base_file (frame, this);
        return 0;
}

int
shard_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
              dict_t *xdata)
{
        int             ret            = -1;
        uint64_t        block_size     = 0;
        uint64_t        dst_block_size = 0;
        shard_local_t  *local          = NULL;

        ret = shard_inode_ctx_get_block_size (oldloc->inode, this, &block_size);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get block size "
                        "from inode ctx of %s",
                        uuid_utoa (oldloc->inode->gfid));
                goto err;
        }

        if (newloc->inode)
                ret = shard_inode_ctx_get_block_size (newloc->inode, this,
                                                      &dst_block_size);
        /* The following stack_wind covers the case where:
         * a. the src file is not sharded and dst doesn't exist, OR
         * b. the src and dst both exist but are not sharded.
         */
        if ((!block_size) && (!dst_block_size)) {
                STACK_WIND (frame, default_rename_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->rename, oldloc, newloc,
                            xdata);
                return 0;
        }

        local = mem_get0 (this->local_pool);
        if (!local)
                goto err;

        frame->local = local;
        loc_copy (&local->loc, oldloc);
        loc_copy (&local->loc2, newloc);
        local->fop = GF_FOP_RENAME;
        local->xattr_req = (xdata) ? dict_ref (xdata) : dict_new();
        if (!local->xattr_req)
                goto err;

        local->block_size = block_size;
        local->dst_block_size = dst_block_size;
        if (!this->itable)
                this->itable = (local->loc.inode)->table;

        if (local->dst_block_size)
                /* The if block covers the case where the dst file exists and is
                 * sharded. So it is important to look up this inode, record its
                 * size, before renaming src to dst, so as to NOT lose this
                 * information.
                 */
                shard_lookup_base_file (frame, this, &local->loc2,
                                       shard_post_lookup_dst_base_file_handler);
        else
                /* The following block covers the case where the dst either
                 * doesn't exist or is NOT sharded. In this case, shard xlator
                 * would go ahead and rename src to dst.
                 */
                shard_rename_src_base_file (frame, this);
        return 0;

err:
        SHARD_STACK_UNWIND (rename, frame, -1, ENOMEM, NULL, NULL, NULL,
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
        shard_local_t  *local      = NULL;

        local = mem_get0 (this->local_pool);
        if (!local)
                goto err;

        frame->local = local;

        SHARD_INODE_CREATE_INIT (this, local, xdata, loc, err);

        STACK_WIND (frame, shard_create_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->create, loc, flags, mode, umask,
                    fd, xdata);
        return 0;

err:
        SHARD_STACK_UNWIND (create, frame, -1, ENOMEM, NULL, NULL, NULL,
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

static int
shard_init_dot_shard_loc (xlator_t *this, shard_local_t *local)
{
        int    ret           = -1;
        loc_t *dot_shard_loc = NULL;

        if (!local)
                return -1;

        dot_shard_loc = &local->dot_shard_loc;
        dot_shard_loc->inode = inode_new (this->itable);
        dot_shard_loc->parent = inode_ref (this->itable->root);
        ret = inode_path (dot_shard_loc->parent, GF_SHARD_DIR,
                          (char **)&dot_shard_loc->path);
        if (ret < 0 || !(dot_shard_loc->inode)) {
                gf_log (this->name, GF_LOG_ERROR, "Inode path failed on %s",
                        GF_SHARD_DIR);
                goto out;
        }

        dot_shard_loc->name = strrchr (dot_shard_loc->path, '/');
        if (dot_shard_loc->name)
                dot_shard_loc->name++;

        ret = 0;
out:
        return ret;
}

int
shard_readv_do_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iovec *vector,
                    int32_t count, struct iatt *stbuf, struct iobref *iobref,
                    dict_t *xdata)
{
        int                i             = 0;
        int                call_count    = 0;
        void              *address       = NULL;
        uint64_t           block_num     = 0;
        off_t              off           = 0;
        struct iovec       vec           = {0,};
        shard_local_t     *local         = NULL;
        fd_t              *anon_fd       = cookie;

        local = frame->local;

        if (op_ret < 0) {
                local->op_ret = op_ret;
                local->op_errno = op_errno;
                goto out;
        }

        if (local->op_ret >= 0)
                local->op_ret += op_ret;

        fd_ctx_get (anon_fd, this, &block_num);

        if (block_num == local->first_block) {
                address = local->iobuf->ptr;
        } else {
                /* else
                 * address to start writing to = beginning of buffer +
                 *                    number of bytes until end of first block +
                 *                    + block_size times number of blocks
                 *                    between the current block and the first
                 */
                address = (char *) local->iobuf->ptr + (local->block_size -
                          (local->offset % local->block_size)) +
                          ((block_num - local->first_block - 1) *
                          local->block_size);
        }

        for (i = 0; i < count; i++) {
                address = (char *) address + off;
                memcpy (address, vector[i].iov_base, vector[i].iov_len);
                off += vector[i].iov_len;
        }

out:
        if (anon_fd)
                fd_unref (anon_fd);
        call_count = shard_call_count_return (frame);
        if (call_count == 0) {
                if (local->op_ret < 0) {
                        SHARD_STACK_UNWIND (readv, frame, local->op_ret,
                                            local->op_errno, NULL, 0, NULL,
                                            NULL, NULL);
                } else {
                        if (xdata)
                                local->xattr_rsp = dict_ref (xdata);
                        vec.iov_base = local->iobuf->ptr;
                        vec.iov_len = local->op_ret;
                        SHARD_STACK_UNWIND (readv, frame, local->op_ret,
                                            local->op_errno, &vec, 1,
                                            &local->prebuf, local->iobref,
                                            local->xattr_rsp);
                        return 0;
                }
        }

        return 0;
}

int
shard_readv_do (call_frame_t *frame, xlator_t *this)
{
        int                i                    = 0;
        int                ret                  = 0;
        int                call_count           = 0;
        int                last_block           = 0;
        int                cur_block            = 0;
        off_t              orig_offset          = 0;
        off_t              shard_offset         = 0;
        size_t             read_size            = 0;
        size_t             remaining_size       = 0;
        fd_t              *fd                   = NULL;
        fd_t              *anon_fd              = NULL;
        shard_local_t     *local                = NULL;
        gf_boolean_t       wind_failed          = _gf_false;

        local = frame->local;
        fd = local->fd;

        orig_offset = local->offset;
        cur_block = local->first_block;
        last_block = local->last_block;
        remaining_size = local->total_size;
        local->call_count = call_count = local->num_blocks;

        while (cur_block <= last_block) {
                if (wind_failed) {
                        shard_readv_do_cbk (frame, (void *) (long) 0,  this, -1,
                                            ENOMEM, NULL, 0, NULL, NULL, NULL);
                        goto next;
                }

                shard_offset = orig_offset % local->block_size;
                read_size = local->block_size - shard_offset;
                if (read_size > remaining_size)
                        read_size = remaining_size;

                remaining_size -= read_size;

                if (cur_block == 0) {
                        anon_fd = fd_ref (fd);
                } else {
                        anon_fd = fd_anonymous (local->inode_list[i]);
                        if (!anon_fd) {
                                local->op_ret = -1;
                                local->op_errno = ENOMEM;
                                wind_failed = _gf_true;
                                shard_readv_do_cbk (frame,
                                                    (void *) (long) anon_fd,
                                                    this, -1, ENOMEM, NULL, 0,
                                                    NULL, NULL, NULL);
                                goto next;
                        }
                }

                ret = fd_ctx_set (anon_fd, this, cur_block);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to set fd "
                                "ctx for block %d,  gfid=%s", cur_block,
                                uuid_utoa (local->inode_list[i]->gfid));
                        local->op_ret = -1;
                        local->op_errno = ENOMEM;
                        wind_failed = _gf_true;
                        shard_readv_do_cbk (frame, (void *) (long) anon_fd,
                                            this, -1, ENOMEM, NULL, 0, NULL,
                                            NULL, NULL);
                        goto next;
                }

                STACK_WIND_COOKIE (frame, shard_readv_do_cbk, anon_fd,
                                   FIRST_CHILD(this),
                                   FIRST_CHILD(this)->fops->readv, anon_fd,
                                   read_size, shard_offset, local->flags,
                                   local->xattr_req);

                orig_offset += read_size;
next:
                cur_block++;
                i++;
                call_count--;
        }
        return 0;
}

int
shard_post_lookup_shards_readv_handler (call_frame_t *frame, xlator_t *this)
{
        shard_local_t *local = NULL;

        local = frame->local;

        if (local->op_ret < 0) {
                SHARD_STACK_UNWIND (readv, frame, local->op_ret,
                                    local->op_errno, NULL, 0, NULL, NULL, NULL);
                return 0;
        }

        shard_readv_do (frame, this);

        return 0;
}

int
shard_post_mknod_readv_handler (call_frame_t *frame, xlator_t *this)
{
        shard_local_t *local = NULL;

        local = frame->local;

        if (local->op_ret < 0) {
                SHARD_STACK_UNWIND (readv, frame, local->op_ret,
                                    local->op_errno, NULL, 0, NULL, NULL, NULL);
                return 0;
        }

        if (!local->eexist_count)
                shard_readv_do (frame, this);
        else
                shard_common_lookup_shards (frame, this, local->loc.inode,
                                        shard_post_lookup_shards_readv_handler);
        return 0;
}

int
shard_common_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
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
                gf_log (this->name, GF_LOG_DEBUG, "mknod of shard %d "
                        "failed: %s", shard_block_num, strerror (op_errno));
                goto done;
        }

        shard_link_block_inode (local, shard_block_num, inode, buf);

done:
        call_count = shard_call_count_return (frame);
        if (call_count == 0)
                local->post_mknod_handler (frame, this);

        return 0;
}

int
shard_common_resume_mknod (call_frame_t *frame, xlator_t *this,
                           shard_post_mknod_fop_handler_t post_mknod_handler)
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
        local->post_mknod_handler = post_mknod_handler;

        ret = shard_inode_ctx_get_all (fd->inode, this, &ctx_tmp);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get inode ctx for"
                        " %s", uuid_utoa (fd->inode->gfid));
                local->op_ret = -1;
                local->op_errno = ENOMEM;
                goto err;
        }

        while (shard_idx_iter <= last_block) {
                if (local->inode_list[i]) {
                        shard_idx_iter++;
                        i++;
                        continue;
                }

                if (wind_failed) {
                        shard_common_mknod_cbk (frame,
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
                        shard_common_mknod_cbk (frame,
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
                if (ret < 0 || !(loc.inode)) {
                        gf_log (this->name, GF_LOG_ERROR, "Inode path failed on"
                                " %s", bname);
                        local->op_ret = -1;
                        local->op_errno = ENOMEM;
                        wind_failed = _gf_true;
                        loc_wipe (&loc);
                        dict_unref (xattr_req);
                        shard_common_mknod_cbk (frame,
                                                (void *) (long) shard_idx_iter,
                                                this, -1, ENOMEM, NULL, NULL,
                                                NULL, NULL, NULL);
                        goto next;
                }

                loc.name = strrchr (loc.path, '/');
                if (loc.name)
                        loc.name++;

                STACK_WIND_COOKIE (frame, shard_common_mknod_cbk,
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
        post_mknod_handler (frame, this);
        return 0;
}

int
shard_post_resolve_readv_handler (call_frame_t *frame, xlator_t *this)
{
        shard_local_t *local = NULL;

        local = frame->local;

        if (local->op_ret < 0) {
                SHARD_STACK_UNWIND (readv, frame, local->op_ret,
                                    local->op_errno, NULL, 0, NULL, NULL, NULL);
                return 0;
        }

        if (local->call_count)
                shard_common_resume_mknod (frame, this,
                                           shard_post_mknod_readv_handler);
        else
                shard_readv_do (frame, this);

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

        if (op_ret) {
                local->op_ret = op_ret;
                local->op_errno = op_errno;
                goto unwind;
        }

        if (!IA_ISDIR (buf->ia_type)) {
                gf_log (this->name, GF_LOG_CRITICAL, "/.shard already exists "
                        "and is not a directory. Please remove /.shard from all"
                        " bricks and try again");
                local->op_ret = -1;
                local->op_errno = EIO;
                goto unwind;
        }

        shard_link_dot_shard_inode (local, inode, buf);
        shard_common_resolve_shards (frame, this, local->loc.inode,
                                     local->post_res_handler);
        return 0;

unwind:
        local->post_res_handler (frame, this);
        return 0;
}

int
shard_lookup_dot_shard (call_frame_t *frame, xlator_t *this,
                        shard_post_resolve_fop_handler_t post_res_handler)
{
        int                 ret       = -1;
        dict_t             *xattr_req = NULL;
        shard_priv_t       *priv      = NULL;
        shard_local_t      *local     = NULL;

        local = frame->local;
        priv = this->private;
        local->post_res_handler = post_res_handler;

        xattr_req = dict_new ();
        if (!xattr_req) {
                local->op_ret = -1;
                local->op_errno = ENOMEM;
                goto err;
        }

        ret = dict_set_static_bin (xattr_req, "gfid-req", priv->dot_shard_gfid,
                                   16);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set gfid of "
                        "/.shard into dict");
                local->op_ret = -1;
                local->op_errno = ENOMEM;
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
        post_res_handler (frame, this);
        return 0;
}

int
shard_post_lookup_readv_handler (call_frame_t *frame, xlator_t *this)
{
        int                ret         = 0;
        size_t             read_size   = 0;
        size_t             actual_size = 0;
        struct iobuf      *iobuf       = NULL;
        shard_local_t     *local       = NULL;
        shard_priv_t      *priv        = NULL;

        priv = this->private;
        local = frame->local;

        if (local->op_ret < 0) {
                SHARD_STACK_UNWIND (readv, frame, local->op_ret,
                                    local->op_errno, NULL, 0, NULL, NULL, NULL);
                return 0;
        }

        if (local->offset >= local->prebuf.ia_size) {
                /* If the read is being performed past the end of the file,
                 * unwind the FOP with 0 bytes read as status.
                 */
                struct iovec      vec        = {0,};

                iobuf = iobuf_get2 (this->ctx->iobuf_pool, local->req_size);
                if (!iobuf)
                        goto err;

                vec.iov_base = iobuf->ptr;
                vec.iov_len = 0;
                local->iobref = iobref_new ();
                iobref_add (local->iobref, iobuf);
                iobuf_unref (iobuf);

                SHARD_STACK_UNWIND (readv, frame, 0, 0, &vec, 1, &local->prebuf,
                                    local->iobref, NULL);
                return 0;
        }

        read_size = (local->offset + local->req_size);
        actual_size = local->prebuf.ia_size;

        local->first_block = get_lowest_block (local->offset,
                                               local->block_size);

        /* If the end of read surpasses the file size, only resolve and read
         * till the end of the file size. If the read is confined within the
         * size of the file, read only the requested size.
         */

        if (read_size >= actual_size)
                local->total_size = actual_size - local->offset;
        else
                local->total_size = local->req_size;

        local->last_block = get_highest_block (local->offset, local->total_size,
                                               local->block_size);

        local->num_blocks = local->last_block - local->first_block + 1;

        local->inode_list = GF_CALLOC (local->num_blocks, sizeof (inode_t *),
                                       gf_shard_mt_inode_list);
        if (!local->inode_list)
                goto err;

        iobuf = iobuf_get2 (this->ctx->iobuf_pool, local->total_size);
        if (!iobuf)
                goto err;

        local->iobref = iobref_new ();
        if (!local->iobref) {
                iobuf_unref (iobuf);
                goto err;
        }

        if (iobref_add (local->iobref, iobuf) != 0) {
                iobuf_unref (iobuf);
                goto err;
        }

        iobuf_unref (iobuf);
        local->iobuf = iobuf;
        memset (iobuf->ptr, 0, local->total_size);

        local->dot_shard_loc.inode = inode_find (this->itable,
                                                 priv->dot_shard_gfid);
        if (!local->dot_shard_loc.inode) {
                ret = shard_init_dot_shard_loc (this, local);
                if (ret)
                        goto err;
                shard_lookup_dot_shard (frame, this,
                                        shard_post_resolve_readv_handler);
        } else {
                shard_common_resolve_shards (frame, this, local->loc.inode,
                                             shard_post_resolve_readv_handler);
        }
        return 0;

err:
        SHARD_STACK_UNWIND (readv, frame, -1, ENOMEM, NULL, 0, NULL, NULL,
                            NULL);
        return 0;
}

int
shard_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset, uint32_t flags, dict_t *xdata)
{
        int             ret                     = 0;
        uint64_t        block_size              = 0;
        shard_local_t  *local                   = NULL;

        ret = shard_inode_ctx_get_block_size (fd->inode, this, &block_size);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get block size for"
                        "%s from its inode ctx", uuid_utoa (fd->inode->gfid));
                goto err;
        }

        if (!block_size) {
                /* block_size = 0 means that the file was created before
                 * sharding was enabled on the volume.
                 */
                STACK_WIND (frame, default_readv_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readv, fd, size, offset,
                            flags, xdata);
                return 0;
        }

        if (!this->itable)
                this->itable = fd->inode->table;

        local = mem_get0 (this->local_pool);
        if (!local)
                goto err;

        frame->local = local;

        local->fd = fd_ref (fd);
        local->block_size = block_size;
        local->offset = offset;
        local->req_size = size;
        local->flags = flags;
        local->xattr_req = (xdata) ? dict_ref (xdata) : dict_new ();
        if (!local->xattr_req)
                goto err;

        local->loc.inode = inode_ref (fd->inode);
        gf_uuid_copy (local->loc.gfid, fd->inode->gfid);

        shard_lookup_base_file (frame, this, &local->loc,
                                shard_post_lookup_readv_handler);

        return 0;

err:
        SHARD_STACK_UNWIND (readv, frame, -1, ENOMEM, NULL, 0, NULL, NULL,
                            NULL);
        return 0;

}

int
shard_update_file_size_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        shard_local_t *local = NULL;

        local = frame->local;

        if (op_ret < 0) {
                local->op_ret = op_ret;
                local->op_errno = op_errno;
                goto err;
        }

        SHARD_STACK_UNWIND (writev, frame, local->written_size, local->op_errno,
                            &local->prebuf, &local->postbuf, local->xattr_rsp);
        return 0;

err:
        SHARD_STACK_UNWIND (writev, frame, -1, local->op_errno, NULL,
                            NULL, NULL);
        return 0;
}

int
shard_update_file_size (call_frame_t *frame, xlator_t *this)
{
        int            ret       = -1;
        uint64_t      *size_attr = NULL;
        fd_t          *fd        = NULL;
        shard_local_t *local     = NULL;
        dict_t        *xattr_req = NULL;

        local = frame->local;
        fd = local->fd;

        xattr_req = dict_new ();
        if (!xattr_req)
                goto err;

        ret = shard_set_size_attrs (local->postbuf.ia_size + local->hole_size,
                                    local->postbuf.ia_blocks, &size_attr);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set size attrs for"
                        " %s", uuid_utoa (fd->inode->gfid));
                goto err;
        }

        ret = dict_set_bin (xattr_req, GF_XATTR_SHARD_FILE_SIZE, size_attr,
                            8 * 4);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set key %s into "
                        "dict. gfid=%s", GF_XATTR_SHARD_FILE_SIZE,
                        uuid_utoa (fd->inode->gfid));
                GF_FREE (size_attr);
                goto err;
        }

        STACK_WIND (frame, shard_update_file_size_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetxattr, fd, xattr_req, 0, NULL);

        dict_unref (xattr_req);
        return 0;

err:
        if (xattr_req)
                dict_unref (xattr_req);
        SHARD_STACK_UNWIND (writev, frame, -1, ENOMEM, NULL, NULL, NULL);
        return 0;

}

int
shard_writev_do_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata)
{
        int             call_count = 0;
        fd_t           *anon_fd    = cookie;
        shard_local_t  *local      = NULL;

        local = frame->local;

        if (op_ret < 0) {
                local->op_ret = op_ret;
                local->op_errno = op_errno;
        } else {
                local->written_size += op_ret;
                local->postbuf.ia_blocks += (postbuf->ia_blocks -
                                             prebuf->ia_blocks);
                local->postbuf.ia_size += (postbuf->ia_size - prebuf->ia_size);
        }

        if (anon_fd)
                fd_unref (anon_fd);

        call_count = shard_call_count_return (frame);
        if (call_count == 0) {
                if (local->op_ret < 0) {
                        SHARD_STACK_UNWIND (writev, frame, local->written_size,
                                            local->op_errno, NULL, NULL, NULL);
                } else {
                        if (xdata)
                                local->xattr_rsp = dict_ref (xdata);
                        shard_update_file_size (frame, this);
                }
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

int
shard_post_lookup_writev_handler (call_frame_t *frame, xlator_t *this)
{
        shard_local_t *local = NULL;

        local = frame->local;

        if (local->op_ret < 0) {
                SHARD_STACK_UNWIND (writev, frame, local->op_ret,
                                    local->op_errno, NULL, NULL, NULL);
                return 0;
        }

        local->postbuf = local->prebuf;

        /* At this point, calculate the size of the hole if it is going to be
         * created as part of this write.
         */
        if (local->offset > local->prebuf.ia_size)
                local->hole_size = local->offset - local->prebuf.ia_size;

        shard_writev_do (frame, this);

        return 0;
}

int
shard_post_lookup_shards_writev_handler (call_frame_t *frame, xlator_t *this)
{
        shard_local_t *local = NULL;

        local = frame->local;

        if (local->op_ret < 0) {
                SHARD_STACK_UNWIND (writev, frame, local->op_ret,
                                    local->op_errno, NULL, NULL, NULL);
                return 0;
        }

        shard_lookup_base_file (frame, this, &local->loc,
                                shard_post_lookup_writev_handler);
        return 0;
}

int
shard_post_mknod_writev_handler (call_frame_t *frame, xlator_t *this)
{
        shard_local_t *local = NULL;

        local = frame->local;

        if (local->op_ret < 0) {
                SHARD_STACK_UNWIND (writev, frame, local->op_ret,
                                    local->op_errno, NULL, NULL, NULL);
                return 0;
        }

        if (!local->eexist_count) {
                shard_lookup_base_file (frame, this, &local->loc,
                                        shard_post_lookup_writev_handler);
        } else {
                local->call_count = local->eexist_count;
                shard_common_lookup_shards (frame, this, local->loc.inode,
                                       shard_post_lookup_shards_writev_handler);
        }

        return 0;
}

int
shard_post_resolve_writev_handler (call_frame_t *frame, xlator_t *this)
{
        shard_local_t *local = NULL;

        local = frame->local;

        if (local->op_ret < 0) {
                SHARD_STACK_UNWIND (writev, frame, local->op_ret,
                                    local->op_errno, NULL, NULL, NULL);
                return 0;
        }

        if (local->call_count)
                shard_common_resume_mknod (frame, this,
                                           shard_post_mknod_writev_handler);
        else
                shard_lookup_base_file (frame, this, &local->loc,
                                        shard_post_lookup_writev_handler);
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
                        shard_lookup_dot_shard (frame, this,
                                             shard_post_resolve_writev_handler);
                        return 0;
                }
        }

        shard_link_dot_shard_inode (local, inode, buf);
        shard_common_resolve_shards (frame, this, local->loc.inode,
                                     shard_post_resolve_writev_handler);
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
        dict_t         *xattr_req     = NULL;

        local = frame->local;
        priv = this->private;

        xattr_req = dict_new ();
        if (!xattr_req)
                goto err;

        ret = shard_init_dot_shard_loc (this, local);
        if (ret)
                goto err;

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

int
shard_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
              struct iovec *vector, int32_t count, off_t offset, uint32_t flags,
              struct iobref *iobref, dict_t *xdata)
{
        int             ret            = 0;
        int             i              = 0;
        uint64_t        block_size     = 0;
        shard_local_t  *local          = NULL;
        shard_priv_t   *priv           = NULL;

        priv = this->private;

        ret = shard_inode_ctx_get_block_size (fd->inode, this, &block_size);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get block size "
                        "for %s from its inode ctx",
                        uuid_utoa (fd->inode->gfid));
                goto out;
        }

        if (!block_size) {
                /* block_size = 0 means that the file was created before
                 * sharding was enabled on the volume.
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

        for (i = 0; i < count; i++)
                local->total_size += vector[i].iov_len;

        local->count = count;
        local->offset = offset;
        local->flags = flags;
        local->iobref = iobref_ref (iobref);
        local->fd = fd_ref (fd);
        local->block_size = block_size;
        local->first_block = get_lowest_block (offset, local->block_size);
        local->last_block = get_highest_block (offset, local->total_size,
                                               local->block_size);
        local->num_blocks = local->last_block - local->first_block + 1;
        local->inode_list = GF_CALLOC (local->num_blocks, sizeof (inode_t *),
                                       gf_shard_mt_inode_list);
        if (!local->inode_list)
                goto out;

        local->loc.inode = inode_ref (fd->inode);
        gf_uuid_copy (local->loc.gfid, fd->inode->gfid);

        gf_log (this->name, GF_LOG_TRACE, "gfid=%s first_block=%"PRIu32" "
                "last_block=%"PRIu32" num_blocks=%"PRIu32" offset=%"PRId64" "
                "total_size=%lu", uuid_utoa (fd->inode->gfid),
                local->first_block, local->last_block, local->num_blocks,
                offset, local->total_size);

        local->dot_shard_loc.inode = inode_find (this->itable,
                                                 priv->dot_shard_gfid);
        if (!local->dot_shard_loc.inode)
                shard_writev_mkdir_dot_shard (frame, this);
        else
                shard_common_resolve_shards (frame, this, local->loc.inode,
                                             shard_post_resolve_writev_handler);

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
        fd_t                *fd        = NULL;
        gf_dirent_t         *entry     = NULL;
        gf_dirent_t         *tmp       = NULL;
        shard_local_t       *local     = NULL;
        gf_dirent_t          skipped;

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
                        continue;
                }

                if (!entry->dict)
                        continue;

                shard_modify_size_and_block_count (&entry->d_stat, entry->dict);

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
        shard_local_t  *local    = NULL;

        local = mem_get0 (this->local_pool);
        if (!local) {
                local->op_ret = -1;
                local->op_errno = ENOMEM;
                goto err;
        }

        frame->local = local;

        local->fd = fd_ref (fd);

        if (whichop == GF_FOP_READDIR) {
                STACK_WIND (frame, shard_readdir_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readdir, fd, size, offset,
                            xdata);
        } else {
                local->xattr_req = (xdata) ? dict_ref (xdata) : dict_new ();
                SHARD_MD_READ_FOP_INIT_REQ_DICT (this, local->xattr_req,
                                                 fd->inode->gfid, local, err);

                STACK_WIND (frame, shard_readdir_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->readdirp, fd, size, offset,
                            local->xattr_req);
        }

        return 0;

err:
        STACK_UNWIND_STRICT (readdir, frame, local->op_ret, local->op_errno,
                             NULL, NULL);
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
        gf_uuid_parse (SHARD_ROOT_GFID, priv->dot_shard_gfid);

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

        /* To-Do: Delete all the shards associated with this inode. */
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
