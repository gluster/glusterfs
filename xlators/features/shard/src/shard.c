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
#include <glusterfs/byte-order.h>
#include <glusterfs/defaults.h>
#include <glusterfs/statedump.h>

static gf_boolean_t
__is_shard_dir(uuid_t gfid)
{
    shard_priv_t *priv = THIS->private;

    if (gf_uuid_compare(gfid, priv->dot_shard_gfid) == 0)
        return _gf_true;

    return _gf_false;
}

static gf_boolean_t
__is_gsyncd_on_shard_dir(call_frame_t *frame, loc_t *loc)
{
    if (frame->root->pid == GF_CLIENT_PID_GSYNCD &&
        (__is_shard_dir(loc->pargfid) ||
         (loc->parent && __is_shard_dir(loc->parent->gfid))))
        return _gf_true;

    return _gf_false;
}

void
shard_make_block_bname(int block_num, uuid_t gfid, char *buf, size_t len)
{
    char gfid_str[GF_UUID_BUF_SIZE] = {
        0,
    };

    gf_uuid_unparse(gfid, gfid_str);
    snprintf(buf, len, "%s.%d", gfid_str, block_num);
}

void
shard_make_block_abspath(int block_num, uuid_t gfid, char *filepath, size_t len)
{
    char gfid_str[GF_UUID_BUF_SIZE] = {
        0,
    };

    gf_uuid_unparse(gfid, gfid_str);
    snprintf(filepath, len, "/%s/%s.%d", GF_SHARD_DIR, gfid_str, block_num);
}

int
__shard_inode_ctx_get(inode_t *inode, xlator_t *this, shard_inode_ctx_t **ctx)
{
    int ret = -1;
    uint64_t ctx_uint = 0;
    shard_inode_ctx_t *ctx_p = NULL;

    ret = __inode_ctx_get(inode, this, &ctx_uint);
    if (ret == 0) {
        *ctx = (shard_inode_ctx_t *)(uintptr_t)ctx_uint;
        return ret;
    }

    ctx_p = GF_CALLOC(1, sizeof(*ctx_p), gf_shard_mt_inode_ctx_t);
    if (!ctx_p)
        return ret;

    INIT_LIST_HEAD(&ctx_p->ilist);
    INIT_LIST_HEAD(&ctx_p->to_fsync_list);

    ctx_uint = (uint64_t)(uintptr_t)ctx_p;
    ret = __inode_ctx_set(inode, this, &ctx_uint);
    if (ret < 0) {
        GF_FREE(ctx_p);
        return ret;
    }

    *ctx = ctx_p;

    return ret;
}

int
shard_inode_ctx_get(inode_t *inode, xlator_t *this, shard_inode_ctx_t **ctx)
{
    int ret = 0;

    LOCK(&inode->lock);
    {
        ret = __shard_inode_ctx_get(inode, this, ctx);
    }
    UNLOCK(&inode->lock);

    return ret;
}

int
__shard_inode_ctx_set(inode_t *inode, xlator_t *this, struct iatt *stbuf,
                      uint64_t block_size, int32_t valid)
{
    int ret = -1;
    shard_inode_ctx_t *ctx = NULL;

    ret = __shard_inode_ctx_get(inode, this, &ctx);
    if (ret)
        return ret;

    if (valid & SHARD_MASK_BLOCK_SIZE)
        ctx->block_size = block_size;

    if (valid & SHARD_MASK_PROT)
        ctx->stat.ia_prot = stbuf->ia_prot;

    if (valid & SHARD_MASK_NLINK)
        ctx->stat.ia_nlink = stbuf->ia_nlink;

    if (valid & SHARD_MASK_UID)
        ctx->stat.ia_uid = stbuf->ia_uid;

    if (valid & SHARD_MASK_GID)
        ctx->stat.ia_gid = stbuf->ia_gid;

    if (valid & SHARD_MASK_SIZE)
        ctx->stat.ia_size = stbuf->ia_size;

    if (valid & SHARD_MASK_BLOCKS)
        ctx->stat.ia_blocks = stbuf->ia_blocks;

    if (valid & SHARD_MASK_TIMES) {
        SHARD_TIME_UPDATE(ctx->stat.ia_mtime, ctx->stat.ia_mtime_nsec,
                          stbuf->ia_mtime, stbuf->ia_mtime_nsec);
        SHARD_TIME_UPDATE(ctx->stat.ia_ctime, ctx->stat.ia_ctime_nsec,
                          stbuf->ia_ctime, stbuf->ia_ctime_nsec);
        SHARD_TIME_UPDATE(ctx->stat.ia_atime, ctx->stat.ia_atime_nsec,
                          stbuf->ia_atime, stbuf->ia_atime_nsec);
    }

    if (valid & SHARD_MASK_OTHERS) {
        ctx->stat.ia_ino = stbuf->ia_ino;
        gf_uuid_copy(ctx->stat.ia_gfid, stbuf->ia_gfid);
        ctx->stat.ia_dev = stbuf->ia_dev;
        ctx->stat.ia_type = stbuf->ia_type;
        ctx->stat.ia_rdev = stbuf->ia_rdev;
        ctx->stat.ia_blksize = stbuf->ia_blksize;
    }

    if (valid & SHARD_MASK_REFRESH_RESET)
        ctx->refresh = _gf_false;

    return 0;
}

int
shard_inode_ctx_set(inode_t *inode, xlator_t *this, struct iatt *stbuf,
                    uint64_t block_size, int32_t valid)
{
    int ret = -1;

    LOCK(&inode->lock);
    {
        ret = __shard_inode_ctx_set(inode, this, stbuf, block_size, valid);
    }
    UNLOCK(&inode->lock);

    return ret;
}

int
__shard_inode_ctx_set_refresh_flag(inode_t *inode, xlator_t *this)
{
    int ret = -1;
    shard_inode_ctx_t *ctx = NULL;

    ret = __shard_inode_ctx_get(inode, this, &ctx);
    if (ret)
        return ret;

    ctx->refresh = _gf_true;

    return 0;
}
int
shard_inode_ctx_set_refresh_flag(inode_t *inode, xlator_t *this)
{
    int ret = -1;

    LOCK(&inode->lock);
    {
        ret = __shard_inode_ctx_set_refresh_flag(inode, this);
    }
    UNLOCK(&inode->lock);

    return ret;
}

int
__shard_inode_ctx_mark_dir_refreshed(inode_t *inode, xlator_t *this)
{
    int ret = -1;
    shard_inode_ctx_t *ctx = NULL;

    ret = __shard_inode_ctx_get(inode, this, &ctx);
    if (ret)
        return ret;

    ctx->refreshed = _gf_true;
    return 0;
}

int
shard_inode_ctx_mark_dir_refreshed(inode_t *inode, xlator_t *this)
{
    int ret = -1;

    LOCK(&inode->lock);
    {
        ret = __shard_inode_ctx_mark_dir_refreshed(inode, this);
    }
    UNLOCK(&inode->lock);

    return ret;
}

int
__shard_inode_ctx_add_to_fsync_list(inode_t *base_inode, xlator_t *this,
                                    inode_t *shard_inode)
{
    int ret = -1;
    shard_inode_ctx_t *base_ictx = NULL;
    shard_inode_ctx_t *shard_ictx = NULL;

    ret = __shard_inode_ctx_get(base_inode, this, &base_ictx);
    if (ret)
        return ret;

    ret = __shard_inode_ctx_get(shard_inode, this, &shard_ictx);
    if (ret)
        return ret;

    if (shard_ictx->fsync_needed) {
        shard_ictx->fsync_needed++;
        return 1;
    }

    list_add_tail(&shard_ictx->to_fsync_list, &base_ictx->to_fsync_list);
    shard_ictx->inode = shard_inode;
    shard_ictx->fsync_needed++;
    base_ictx->fsync_count++;
    shard_ictx->base_inode = base_inode;

    return 0;
}

int
shard_inode_ctx_add_to_fsync_list(inode_t *base_inode, xlator_t *this,
                                  inode_t *shard_inode)
{
    int ret = -1;

    /* This ref acts as a refkeepr on the base inode. We
     * need to keep this inode alive as it holds the head
     * of the to_fsync_list.
     */
    inode_ref(base_inode);
    inode_ref(shard_inode);

    LOCK(&base_inode->lock);
    LOCK(&shard_inode->lock);
    {
        ret = __shard_inode_ctx_add_to_fsync_list(base_inode, this,
                                                  shard_inode);
    }
    UNLOCK(&shard_inode->lock);
    UNLOCK(&base_inode->lock);

    /* Unref the base inode corresponding to the ref above, if the shard is
     * found to be already part of the fsync list.
     */
    if (ret != 0) {
        inode_unref(base_inode);
        inode_unref(shard_inode);
    }
    return ret;
}

gf_boolean_t
__shard_inode_ctx_needs_lookup(inode_t *inode, xlator_t *this)
{
    int ret = -1;
    shard_inode_ctx_t *ctx = NULL;

    ret = __shard_inode_ctx_get(inode, this, &ctx);
    /* If inode ctx get fails, better to err on the side of caution and
     * try again? Unless the failure is due to mem-allocation.
     */
    if (ret)
        return _gf_true;

    return !ctx->refreshed;
}

gf_boolean_t
shard_inode_ctx_needs_lookup(inode_t *inode, xlator_t *this)
{
    gf_boolean_t flag = _gf_false;

    LOCK(&inode->lock);
    {
        flag = __shard_inode_ctx_needs_lookup(inode, this);
    }
    UNLOCK(&inode->lock);

    return flag;
}
int
__shard_inode_ctx_invalidate(inode_t *inode, xlator_t *this, struct iatt *stbuf)
{
    int ret = -1;
    shard_inode_ctx_t *ctx = NULL;

    ret = __shard_inode_ctx_get(inode, this, &ctx);
    if (ret)
        return ret;

    if ((stbuf->ia_size != ctx->stat.ia_size) ||
        (stbuf->ia_blocks != ctx->stat.ia_blocks))
        ctx->refresh = _gf_true;

    return 0;
}

int
shard_inode_ctx_invalidate(inode_t *inode, xlator_t *this, struct iatt *stbuf)
{
    int ret = -1;

    LOCK(&inode->lock);
    {
        ret = __shard_inode_ctx_invalidate(inode, this, stbuf);
    }
    UNLOCK(&inode->lock);

    return ret;
}

int
__shard_inode_ctx_get_block_size(inode_t *inode, xlator_t *this,
                                 uint64_t *block_size)
{
    int ret = -1;
    uint64_t ctx_uint = 0;
    shard_inode_ctx_t *ctx = NULL;

    ret = __inode_ctx_get(inode, this, &ctx_uint);
    if (ret < 0)
        return ret;

    ctx = (shard_inode_ctx_t *)(uintptr_t)ctx_uint;

    *block_size = ctx->block_size;

    return 0;
}

int
shard_inode_ctx_get_block_size(inode_t *inode, xlator_t *this,
                               uint64_t *block_size)
{
    int ret = -1;

    LOCK(&inode->lock);
    {
        ret = __shard_inode_ctx_get_block_size(inode, this, block_size);
    }
    UNLOCK(&inode->lock);

    return ret;
}

int
__shard_inode_ctx_get_fsync_count(inode_t *inode, xlator_t *this,
                                  int *fsync_count)
{
    int ret = -1;
    uint64_t ctx_uint = 0;
    shard_inode_ctx_t *ctx = NULL;

    ret = __inode_ctx_get(inode, this, &ctx_uint);
    if (ret < 0)
        return ret;

    ctx = (shard_inode_ctx_t *)(uintptr_t)ctx_uint;

    *fsync_count = ctx->fsync_needed;

    return 0;
}

int
shard_inode_ctx_get_fsync_count(inode_t *inode, xlator_t *this,
                                int *fsync_count)
{
    int ret = -1;

    LOCK(&inode->lock);
    {
        ret = __shard_inode_ctx_get_fsync_count(inode, this, fsync_count);
    }
    UNLOCK(&inode->lock);

    return ret;
}
int
__shard_inode_ctx_get_all(inode_t *inode, xlator_t *this,
                          shard_inode_ctx_t *ctx_out)
{
    int ret = -1;
    uint64_t ctx_uint = 0;
    shard_inode_ctx_t *ctx = NULL;

    ret = __inode_ctx_get(inode, this, &ctx_uint);
    if (ret < 0)
        return ret;

    ctx = (shard_inode_ctx_t *)(uintptr_t)ctx_uint;

    memcpy(ctx_out, ctx, sizeof(shard_inode_ctx_t));
    return 0;
}

int
shard_inode_ctx_get_all(inode_t *inode, xlator_t *this,
                        shard_inode_ctx_t *ctx_out)
{
    int ret = -1;

    LOCK(&inode->lock);
    {
        ret = __shard_inode_ctx_get_all(inode, this, ctx_out);
    }
    UNLOCK(&inode->lock);

    return ret;
}

int
__shard_inode_ctx_fill_iatt_from_cache(inode_t *inode, xlator_t *this,
                                       struct iatt *buf,
                                       gf_boolean_t *need_refresh)
{
    int ret = -1;
    uint64_t ctx_uint = 0;
    shard_inode_ctx_t *ctx = NULL;

    ret = __inode_ctx_get(inode, this, &ctx_uint);
    if (ret < 0)
        return ret;

    ctx = (shard_inode_ctx_t *)(uintptr_t)ctx_uint;

    if (ctx->refresh == _gf_false)
        *buf = ctx->stat;
    else
        *need_refresh = _gf_true;

    return 0;
}

int
shard_inode_ctx_fill_iatt_from_cache(inode_t *inode, xlator_t *this,
                                     struct iatt *buf,
                                     gf_boolean_t *need_refresh)
{
    int ret = -1;

    LOCK(&inode->lock);
    {
        ret = __shard_inode_ctx_fill_iatt_from_cache(inode, this, buf,
                                                     need_refresh);
    }
    UNLOCK(&inode->lock);

    return ret;
}

void
shard_local_wipe(shard_local_t *local)
{
    int i = 0;
    int count = 0;

    count = local->num_blocks;

    syncbarrier_destroy(&local->barrier);
    loc_wipe(&local->loc);
    loc_wipe(&local->dot_shard_loc);
    loc_wipe(&local->dot_shard_rm_loc);
    loc_wipe(&local->loc2);
    loc_wipe(&local->tmp_loc);
    loc_wipe(&local->int_inodelk.loc);
    loc_wipe(&local->int_entrylk.loc);
    loc_wipe(&local->newloc);

    if (local->name)
        GF_FREE(local->name);

    if (local->int_entrylk.basename)
        GF_FREE(local->int_entrylk.basename);
    if (local->fd)
        fd_unref(local->fd);

    if (local->xattr_req)
        dict_unref(local->xattr_req);
    if (local->xattr_rsp)
        dict_unref(local->xattr_rsp);

    for (i = 0; i < count; i++) {
        if (!local->inode_list)
            break;

        if (local->inode_list[i])
            inode_unref(local->inode_list[i]);
    }

    GF_FREE(local->inode_list);

    GF_FREE(local->vector);
    if (local->iobref)
        iobref_unref(local->iobref);
    if (local->list_inited)
        gf_dirent_free(&local->entries_head);
    if (local->inodelk_frame)
        SHARD_STACK_DESTROY(local->inodelk_frame);
    if (local->entrylk_frame)
        SHARD_STACK_DESTROY(local->entrylk_frame);
}

int
shard_modify_size_and_block_count(struct iatt *stbuf, dict_t *dict)
{
    int ret = -1;
    void *size_attr = NULL;
    uint64_t size_array[4];

    ret = dict_get_ptr(dict, GF_XATTR_SHARD_FILE_SIZE, &size_attr);
    if (ret) {
        gf_msg_callingfn(THIS->name, GF_LOG_ERROR, 0,
                         SHARD_MSG_INTERNAL_XATTR_MISSING,
                         "Failed to "
                         "get " GF_XATTR_SHARD_FILE_SIZE " for %s",
                         uuid_utoa(stbuf->ia_gfid));
        return ret;
    }

    memcpy(size_array, size_attr, sizeof(size_array));

    stbuf->ia_size = ntoh64(size_array[0]);
    stbuf->ia_blocks = ntoh64(size_array[2]);

    return 0;
}

int
shard_call_count_return(call_frame_t *frame)
{
    int call_count = 0;
    shard_local_t *local = NULL;

    local = frame->local;

    LOCK(&frame->lock);
    {
        call_count = --local->call_count;
    }
    UNLOCK(&frame->lock);

    return call_count;
}

static char *
shard_internal_dir_string(shard_internal_dir_type_t type)
{
    char *str = NULL;

    switch (type) {
        case SHARD_INTERNAL_DIR_DOT_SHARD:
            str = GF_SHARD_DIR;
            break;
        case SHARD_INTERNAL_DIR_DOT_SHARD_REMOVE_ME:
            str = GF_SHARD_REMOVE_ME_DIR;
            break;
        default:
            break;
    }
    return str;
}

static int
shard_init_internal_dir_loc(xlator_t *this, shard_local_t *local,
                            shard_internal_dir_type_t type)
{
    int ret = -1;
    char *bname = NULL;
    inode_t *parent = NULL;
    loc_t *internal_dir_loc = NULL;
    shard_priv_t *priv = NULL;

    priv = this->private;
    if (!local)
        return -1;

    switch (type) {
        case SHARD_INTERNAL_DIR_DOT_SHARD:
            internal_dir_loc = &local->dot_shard_loc;
            bname = GF_SHARD_DIR;
            parent = inode_ref(this->itable->root);
            break;
        case SHARD_INTERNAL_DIR_DOT_SHARD_REMOVE_ME:
            internal_dir_loc = &local->dot_shard_rm_loc;
            bname = GF_SHARD_REMOVE_ME_DIR;
            parent = inode_ref(priv->dot_shard_inode);
            break;
        default:
            break;
    }

    internal_dir_loc->inode = inode_new(this->itable);
    internal_dir_loc->parent = parent;
    ret = inode_path(internal_dir_loc->parent, bname,
                     (char **)&internal_dir_loc->path);
    if (ret < 0 || !(internal_dir_loc->inode)) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_PATH_FAILED,
               "Inode path failed on %s", bname);
        goto out;
    }

    internal_dir_loc->name = strrchr(internal_dir_loc->path, '/');
    if (internal_dir_loc->name)
        internal_dir_loc->name++;

    ret = 0;
out:
    return ret;
}

inode_t *
__shard_update_shards_inode_list(inode_t *linked_inode, xlator_t *this,
                                 inode_t *base_inode, int block_num,
                                 uuid_t gfid)
{
    char block_bname[256] = {
        0,
    };
    inode_t *lru_inode = NULL;
    shard_priv_t *priv = NULL;
    shard_inode_ctx_t *ctx = NULL;
    shard_inode_ctx_t *lru_inode_ctx = NULL;
    shard_inode_ctx_t *lru_base_inode_ctx = NULL;
    inode_t *fsync_inode = NULL;
    inode_t *lru_base_inode = NULL;
    gf_boolean_t do_fsync = _gf_false;

    priv = this->private;

    shard_inode_ctx_get(linked_inode, this, &ctx);

    if (list_empty(&ctx->ilist)) {
        if (priv->inode_count + 1 <= priv->lru_limit) {
            /* If this inode was linked here for the first time (indicated
             * by empty list), and if there is still space in the priv list,
             * add this ctx to the tail of the list.
             */
            /* For as long as an inode is in lru list, we try to
             * keep it alive by holding a ref on it.
             */
            inode_ref(linked_inode);
            if (base_inode)
                gf_uuid_copy(ctx->base_gfid, base_inode->gfid);
            else
                gf_uuid_copy(ctx->base_gfid, gfid);
            ctx->block_num = block_num;
            list_add_tail(&ctx->ilist, &priv->ilist_head);
            priv->inode_count++;
            ctx->base_inode = inode_ref(base_inode);
        } else {
            /*If on the other hand there is no available slot for this inode
             * in the list, delete the lru inode from the head of the list,
             * unlink it. And in its place add this new inode into the list.
             */
            lru_inode_ctx = list_first_entry(&priv->ilist_head,
                                             shard_inode_ctx_t, ilist);
            GF_ASSERT(lru_inode_ctx->block_num > 0);
            lru_base_inode = lru_inode_ctx->base_inode;
            list_del_init(&lru_inode_ctx->ilist);
            lru_inode = inode_find(linked_inode->table,
                                   lru_inode_ctx->stat.ia_gfid);
            /* If the lru inode was part of the pending-fsync list,
             * the base inode needs to be unref'd, the lru inode
             * deleted from fsync list and fsync'd in a new frame,
             * and then unlinked in memory and forgotten.
             */
            if (!lru_base_inode)
                goto after_fsync_check;
            LOCK(&lru_base_inode->lock);
            LOCK(&lru_inode->lock);
            {
                if (!list_empty(&lru_inode_ctx->to_fsync_list)) {
                    list_del_init(&lru_inode_ctx->to_fsync_list);
                    lru_inode_ctx->fsync_needed = 0;
                    do_fsync = _gf_true;
                    __shard_inode_ctx_get(lru_base_inode, this,
                                          &lru_base_inode_ctx);
                    lru_base_inode_ctx->fsync_count--;
                }
            }
            UNLOCK(&lru_inode->lock);
            UNLOCK(&lru_base_inode->lock);

        after_fsync_check:
            if (!do_fsync) {
                shard_make_block_bname(lru_inode_ctx->block_num,
                                       lru_inode_ctx->base_gfid, block_bname,
                                       sizeof(block_bname));
                /* The following unref corresponds to the ref held at
                 * the time the shard was added to the lru list.
                 */
                inode_unref(lru_inode);
                inode_unlink(lru_inode, priv->dot_shard_inode, block_bname);
                inode_forget(lru_inode, 0);
            } else {
                /* The following unref corresponds to the ref
                 * held when the shard was added to fsync list.
                 */
                inode_unref(lru_inode);
                fsync_inode = lru_inode;
                if (lru_base_inode)
                    inode_unref(lru_base_inode);
            }
            /* The following unref corresponds to the ref
             * held by inode_find() above.
             */
            inode_unref(lru_inode);

            /* The following unref corresponds to the ref held on the base shard
             * at the time of adding shard inode to lru list
             */
            if (lru_base_inode)
                inode_unref(lru_base_inode);

            /* For as long as an inode is in lru list, we try to
             * keep it alive by holding a ref on it.
             */
            inode_ref(linked_inode);
            if (base_inode)
                gf_uuid_copy(ctx->base_gfid, base_inode->gfid);
            else
                gf_uuid_copy(ctx->base_gfid, gfid);
            ctx->block_num = block_num;
            ctx->base_inode = inode_ref(base_inode);
            list_add_tail(&ctx->ilist, &priv->ilist_head);
        }
    } else {
        /* If this is not the first time this inode is being operated on, move
         * it to the most recently used end of the list.
         */
        list_move_tail(&ctx->ilist, &priv->ilist_head);
    }
    return fsync_inode;
}

int
shard_common_failure_unwind(glusterfs_fop_t fop, call_frame_t *frame,
                            int32_t op_ret, int32_t op_errno)
{
    switch (fop) {
        case GF_FOP_LOOKUP:
            SHARD_STACK_UNWIND(lookup, frame, op_ret, op_errno, NULL, NULL,
                               NULL, NULL);
            break;
        case GF_FOP_STAT:
            SHARD_STACK_UNWIND(stat, frame, op_ret, op_errno, NULL, NULL);
            break;
        case GF_FOP_FSTAT:
            SHARD_STACK_UNWIND(fstat, frame, op_ret, op_errno, NULL, NULL);
            break;
        case GF_FOP_TRUNCATE:
            SHARD_STACK_UNWIND(truncate, frame, op_ret, op_errno, NULL, NULL,
                               NULL);
            break;
        case GF_FOP_FTRUNCATE:
            SHARD_STACK_UNWIND(ftruncate, frame, op_ret, op_errno, NULL, NULL,
                               NULL);
            break;
        case GF_FOP_MKNOD:
            SHARD_STACK_UNWIND(mknod, frame, op_ret, op_errno, NULL, NULL, NULL,
                               NULL, NULL);
            break;
        case GF_FOP_LINK:
            SHARD_STACK_UNWIND(link, frame, op_ret, op_errno, NULL, NULL, NULL,
                               NULL, NULL);
            break;
        case GF_FOP_CREATE:
            SHARD_STACK_UNWIND(create, frame, op_ret, op_errno, NULL, NULL,
                               NULL, NULL, NULL, NULL);
            break;
        case GF_FOP_UNLINK:
            SHARD_STACK_UNWIND(unlink, frame, op_ret, op_errno, NULL, NULL,
                               NULL);
            break;
        case GF_FOP_RENAME:
            SHARD_STACK_UNWIND(rename, frame, op_ret, op_errno, NULL, NULL,
                               NULL, NULL, NULL, NULL);
            break;
        case GF_FOP_WRITE:
            SHARD_STACK_UNWIND(writev, frame, op_ret, op_errno, NULL, NULL,
                               NULL);
            break;
        case GF_FOP_FALLOCATE:
            SHARD_STACK_UNWIND(fallocate, frame, op_ret, op_errno, NULL, NULL,
                               NULL);
            break;
        case GF_FOP_ZEROFILL:
            SHARD_STACK_UNWIND(zerofill, frame, op_ret, op_errno, NULL, NULL,
                               NULL);
            break;
        case GF_FOP_DISCARD:
            SHARD_STACK_UNWIND(discard, frame, op_ret, op_errno, NULL, NULL,
                               NULL);
            break;
        case GF_FOP_READ:
            SHARD_STACK_UNWIND(readv, frame, op_ret, op_errno, NULL, -1, NULL,
                               NULL, NULL);
            break;
        case GF_FOP_FSYNC:
            SHARD_STACK_UNWIND(fsync, frame, op_ret, op_errno, NULL, NULL,
                               NULL);
            break;
        case GF_FOP_REMOVEXATTR:
            SHARD_STACK_UNWIND(removexattr, frame, op_ret, op_errno, NULL);
            break;
        case GF_FOP_FREMOVEXATTR:
            SHARD_STACK_UNWIND(fremovexattr, frame, op_ret, op_errno, NULL);
            break;
        case GF_FOP_FGETXATTR:
            SHARD_STACK_UNWIND(fgetxattr, frame, op_ret, op_errno, NULL, NULL);
            break;
        case GF_FOP_GETXATTR:
            SHARD_STACK_UNWIND(getxattr, frame, op_ret, op_errno, NULL, NULL);
            break;
        case GF_FOP_FSETXATTR:
            SHARD_STACK_UNWIND(fsetxattr, frame, op_ret, op_errno, NULL);
            break;
        case GF_FOP_SETXATTR:
            SHARD_STACK_UNWIND(setxattr, frame, op_ret, op_errno, NULL);
            break;
        case GF_FOP_SETATTR:
            SHARD_STACK_UNWIND(setattr, frame, op_ret, op_errno, NULL, NULL,
                               NULL);
            break;
        case GF_FOP_FSETATTR:
            SHARD_STACK_UNWIND(fsetattr, frame, op_ret, op_errno, NULL, NULL,
                               NULL);
            break;
        case GF_FOP_SEEK:
            SHARD_STACK_UNWIND(seek, frame, op_ret, op_errno, 0, NULL);
            break;
        default:
            gf_msg(THIS->name, GF_LOG_WARNING, 0, SHARD_MSG_INVALID_FOP,
                   "Invalid fop id = %d", fop);
            break;
    }
    return 0;
}

int
shard_common_inode_write_success_unwind(glusterfs_fop_t fop,
                                        call_frame_t *frame, int32_t op_ret)
{
    shard_local_t *local = frame->local;

    /* the below 3 variables are required because, in SHARD_STACK_UNWIND()
       macro, there is a check for local being null. So many static analyzers
       backtrace the code with assumption of possible (local == NULL) case,
       and complains for below lines. By handling it like below, we overcome
       the warnings */

    struct iatt *prebuf = ((local) ? &local->prebuf : NULL);
    struct iatt *postbuf = ((local) ? &local->postbuf : NULL);
    dict_t *xattr_rsp = ((local) ? local->xattr_rsp : NULL);

    switch (fop) {
        case GF_FOP_WRITE:
            SHARD_STACK_UNWIND(writev, frame, op_ret, 0, prebuf, postbuf,
                               xattr_rsp);
            break;
        case GF_FOP_FALLOCATE:
            SHARD_STACK_UNWIND(fallocate, frame, op_ret, 0, prebuf, postbuf,
                               xattr_rsp);
            break;
        case GF_FOP_ZEROFILL:
            SHARD_STACK_UNWIND(zerofill, frame, op_ret, 0, prebuf, postbuf,
                               xattr_rsp);
            break;
        case GF_FOP_DISCARD:
            SHARD_STACK_UNWIND(discard, frame, op_ret, 0, prebuf, postbuf,
                               xattr_rsp);
            break;
        default:
            gf_msg(THIS->name, GF_LOG_WARNING, 0, SHARD_MSG_INVALID_FOP,
                   "Invalid fop id = %d", fop);
            break;
    }
    return 0;
}

int
shard_evicted_inode_fsync_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                              int32_t op_ret, int32_t op_errno,
                              struct iatt *prebuf, struct iatt *postbuf,
                              dict_t *xdata)
{
    char block_bname[256] = {
        0,
    };
    fd_t *anon_fd = cookie;
    inode_t *shard_inode = NULL;
    shard_inode_ctx_t *ctx = NULL;
    shard_priv_t *priv = NULL;

    priv = this->private;

    if (anon_fd == NULL || op_ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, op_errno, SHARD_MSG_MEMALLOC_FAILED,
               "fsync failed on shard");
        goto out;
    }
    shard_inode = anon_fd->inode;

    LOCK(&priv->lock);
    LOCK(&shard_inode->lock);
    {
        __shard_inode_ctx_get(shard_inode, this, &ctx);
        if ((list_empty(&ctx->to_fsync_list)) && (list_empty(&ctx->ilist))) {
            shard_make_block_bname(ctx->block_num, shard_inode->gfid,
                                   block_bname, sizeof(block_bname));
            inode_unlink(shard_inode, priv->dot_shard_inode, block_bname);
            /* The following unref corresponds to the ref held by
             * inode_link() at the time the shard was created or
             * looked up
             */
            inode_unref(shard_inode);
            inode_forget(shard_inode, 0);
        }
    }
    UNLOCK(&shard_inode->lock);
    UNLOCK(&priv->lock);

out:
    if (anon_fd)
        fd_unref(anon_fd);
    STACK_DESTROY(frame->root);
    return 0;
}

int
shard_initiate_evicted_inode_fsync(xlator_t *this, inode_t *inode)
{
    fd_t *anon_fd = NULL;
    call_frame_t *fsync_frame = NULL;

    fsync_frame = create_frame(this, this->ctx->pool);
    if (!fsync_frame) {
        gf_msg(this->name, GF_LOG_WARNING, ENOMEM, SHARD_MSG_MEMALLOC_FAILED,
               "Failed to create new frame "
               "to fsync shard");
        return -1;
    }

    anon_fd = fd_anonymous(inode);
    if (!anon_fd) {
        gf_msg(this->name, GF_LOG_WARNING, ENOMEM, SHARD_MSG_MEMALLOC_FAILED,
               "Failed to create anon fd to"
               " fsync shard");
        STACK_DESTROY(fsync_frame->root);
        return -1;
    }

    STACK_WIND_COOKIE(fsync_frame, shard_evicted_inode_fsync_cbk, anon_fd,
                      FIRST_CHILD(this), FIRST_CHILD(this)->fops->fsync,
                      anon_fd, 1, NULL);
    return 0;
}

int
shard_common_inode_write_post_lookup_shards_handler(call_frame_t *frame,
                                                    xlator_t *this);

int
shard_common_resolve_shards(call_frame_t *frame, xlator_t *this,
                            shard_post_resolve_fop_handler_t post_res_handler)
{
    int i = -1;
    uint32_t shard_idx_iter = 0;
    char path[PATH_MAX] = {
        0,
    };
    uuid_t gfid = {
        0,
    };
    inode_t *inode = NULL;
    inode_t *res_inode = NULL;
    inode_t *fsync_inode = NULL;
    shard_priv_t *priv = NULL;
    shard_local_t *local = NULL;
    uint64_t resolve_count = 0;

    priv = this->private;
    local = frame->local;
    local->call_count = 0;
    shard_idx_iter = local->first_block;
    res_inode = local->resolver_base_inode;

    if ((local->op_ret < 0) || (local->resolve_not))
        goto out;

    /* If this prealloc FOP is for fresh file creation, then the size of the
     * file will be 0. Then there will be no shards associated with this file.
     * So we can skip the lookup process for the shards which do not exists
     * and directly issue mknod to crete shards.
     *
     * In case the prealloc fop is to extend the preallocated file to bigger
     * size then just lookup and populate inodes of existing shards and
     * update the create count
     */
    if (local->fop == GF_FOP_FALLOCATE) {
        if (!local->prebuf.ia_size) {
            local->inode_list[0] = inode_ref(res_inode);
            local->create_count = local->last_block;
            shard_common_inode_write_post_lookup_shards_handler(frame, this);
            return 0;
        }
        if (local->prebuf.ia_size < local->total_size)
            local->create_count = local->last_block -
                                  ((local->prebuf.ia_size - 1) /
                                   local->block_size);
    }

    resolve_count = local->last_block - local->create_count;

    if (res_inode)
        gf_uuid_copy(gfid, res_inode->gfid);
    else
        gf_uuid_copy(gfid, local->base_gfid);

    while (shard_idx_iter <= resolve_count) {
        i++;
        if (shard_idx_iter == 0) {
            local->inode_list[i] = inode_ref(res_inode);
            shard_idx_iter++;
            continue;
        }

        shard_make_block_abspath(shard_idx_iter, gfid, path, sizeof(path));

        inode = NULL;
        inode = inode_resolve(this->itable, path);
        if (inode) {
            gf_msg_debug(this->name, 0,
                         "Shard %d already "
                         "present. gfid=%s. Saving inode for future.",
                         shard_idx_iter, uuid_utoa(inode->gfid));
            local->inode_list[i] = inode;
            /* Let the ref on the inodes that are already present
             * in inode table still be held so that they don't get
             * forgotten by the time the fop reaches the actual
             * write stage.
             */
            LOCK(&priv->lock);
            {
                fsync_inode = __shard_update_shards_inode_list(
                    inode, this, res_inode, shard_idx_iter, gfid);
            }
            UNLOCK(&priv->lock);
            shard_idx_iter++;
            if (fsync_inode)
                shard_initiate_evicted_inode_fsync(this, fsync_inode);
            continue;
        } else {
            local->call_count++;
            shard_idx_iter++;
        }
    }
out:
    post_res_handler(frame, this);
    return 0;
}

int
shard_update_file_size_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, dict_t *dict,
                           dict_t *xdata)
{
    inode_t *inode = NULL;
    shard_local_t *local = NULL;

    local = frame->local;

    if ((local->fd) && (local->fd->inode))
        inode = local->fd->inode;
    else if (local->loc.inode)
        inode = local->loc.inode;

    if (op_ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, op_errno,
               SHARD_MSG_UPDATE_FILE_SIZE_FAILED,
               "Update to file size"
               " xattr failed on %s",
               uuid_utoa(inode->gfid));
        local->op_ret = op_ret;
        local->op_errno = op_errno;
        goto err;
    }

    if (shard_modify_size_and_block_count(&local->postbuf, dict)) {
        local->op_ret = -1;
        local->op_errno = ENOMEM;
        goto err;
    }
err:
    local->post_update_size_handler(frame, this);
    return 0;
}

int
shard_set_size_attrs(int64_t size, int64_t block_count, int64_t **size_attr_p)
{
    int ret = -1;
    int64_t *size_attr = NULL;

    if (!size_attr_p)
        goto out;

    size_attr = GF_CALLOC(4, sizeof(int64_t), gf_shard_mt_int64_t);
    if (!size_attr)
        goto out;

    size_attr[0] = hton64(size);
    /* As sharding evolves, it _may_ be necessary to embed more pieces of
     * information within the same xattr. So allocating slots for them in
     * advance. For now, only bytes 0-63 and 128-191 which would make up the
     * current size and block count respectively of the file are valid.
     */
    size_attr[2] = hton64(block_count);

    *size_attr_p = size_attr;

    ret = 0;
out:
    return ret;
}

int
shard_update_file_size(call_frame_t *frame, xlator_t *this, fd_t *fd,
                       loc_t *loc, shard_post_update_size_fop_handler_t handler)
{
    int ret = -1;
    int64_t *size_attr = NULL;
    int64_t delta_blocks = 0;
    inode_t *inode = NULL;
    shard_local_t *local = NULL;
    dict_t *xattr_req = NULL;

    local = frame->local;
    local->post_update_size_handler = handler;

    xattr_req = dict_new();
    if (!xattr_req) {
        local->op_ret = -1;
        local->op_errno = ENOMEM;
        goto out;
    }

    if (fd)
        inode = fd->inode;
    else
        inode = loc->inode;

    /* If both size and block count have not changed, then skip the xattrop.
     */
    delta_blocks = GF_ATOMIC_GET(local->delta_blocks);
    if ((local->delta_size + local->hole_size == 0) && (delta_blocks == 0)) {
        goto out;
    }

    ret = shard_set_size_attrs(local->delta_size + local->hole_size,
                               delta_blocks, &size_attr);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_SIZE_SET_FAILED,
               "Failed to set size attrs for %s", uuid_utoa(inode->gfid));
        local->op_ret = -1;
        local->op_errno = ENOMEM;
        goto out;
    }

    ret = dict_set_bin(xattr_req, GF_XATTR_SHARD_FILE_SIZE, size_attr, 8 * 4);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_DICT_OP_FAILED,
               "Failed to set key %s into dict. gfid=%s",
               GF_XATTR_SHARD_FILE_SIZE, uuid_utoa(inode->gfid));
        GF_FREE(size_attr);
        local->op_ret = -1;
        local->op_errno = ENOMEM;
        goto out;
    }

    if (fd)
        STACK_WIND(frame, shard_update_file_size_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->fxattrop, fd,
                   GF_XATTROP_ADD_ARRAY64, xattr_req, NULL);
    else
        STACK_WIND(frame, shard_update_file_size_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->xattrop, loc,
                   GF_XATTROP_ADD_ARRAY64, xattr_req, NULL);

    dict_unref(xattr_req);
    return 0;

out:
    if (xattr_req)
        dict_unref(xattr_req);
    handler(frame, this);
    return 0;
}

static inode_t *
shard_link_internal_dir_inode(shard_local_t *local, inode_t *inode,
                              struct iatt *buf, shard_internal_dir_type_t type)
{
    inode_t *linked_inode = NULL;
    shard_priv_t *priv = NULL;
    char *bname = NULL;
    inode_t **priv_inode = NULL;
    inode_t *parent = NULL;

    priv = THIS->private;

    switch (type) {
        case SHARD_INTERNAL_DIR_DOT_SHARD:
            bname = GF_SHARD_DIR;
            priv_inode = &priv->dot_shard_inode;
            parent = inode->table->root;
            break;
        case SHARD_INTERNAL_DIR_DOT_SHARD_REMOVE_ME:
            bname = GF_SHARD_REMOVE_ME_DIR;
            priv_inode = &priv->dot_shard_rm_inode;
            parent = priv->dot_shard_inode;
            break;
        default:
            break;
    }

    linked_inode = inode_link(inode, parent, bname, buf);
    inode_lookup(linked_inode);
    *priv_inode = linked_inode;
    return linked_inode;
}

int
shard_refresh_internal_dir_cbk(call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret, int32_t op_errno,
                               inode_t *inode, struct iatt *buf, dict_t *xdata,
                               struct iatt *postparent)
{
    shard_local_t *local = NULL;
    inode_t *linked_inode = NULL;
    shard_internal_dir_type_t type = (shard_internal_dir_type_t)cookie;

    local = frame->local;

    if (op_ret) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;
        goto out;
    }

    /* To-Do: Fix refcount increment per call to
     * shard_link_internal_dir_inode().
     */
    linked_inode = shard_link_internal_dir_inode(local, inode, buf, type);
    shard_inode_ctx_mark_dir_refreshed(linked_inode, this);
out:
    shard_common_resolve_shards(frame, this, local->post_res_handler);
    return 0;
}

int
shard_refresh_internal_dir(call_frame_t *frame, xlator_t *this,
                           shard_internal_dir_type_t type)
{
    loc_t loc = {
        0,
    };
    inode_t *inode = NULL;
    shard_priv_t *priv = NULL;
    shard_local_t *local = NULL;
    uuid_t gfid = {
        0,
    };

    local = frame->local;
    priv = this->private;

    switch (type) {
        case SHARD_INTERNAL_DIR_DOT_SHARD:
            gf_uuid_copy(gfid, priv->dot_shard_gfid);
            break;
        case SHARD_INTERNAL_DIR_DOT_SHARD_REMOVE_ME:
            gf_uuid_copy(gfid, priv->dot_shard_rm_gfid);
            break;
        default:
            break;
    }

    inode = inode_find(this->itable, gfid);

    if (!shard_inode_ctx_needs_lookup(inode, this)) {
        local->op_ret = 0;
        goto out;
    }

    /* Plain assignment because the ref is already taken above through
     * call to inode_find()
     */
    loc.inode = inode;
    gf_uuid_copy(loc.gfid, gfid);

    STACK_WIND_COOKIE(frame, shard_refresh_internal_dir_cbk, (void *)(long)type,
                      FIRST_CHILD(this), FIRST_CHILD(this)->fops->lookup, &loc,
                      NULL);
    loc_wipe(&loc);

    return 0;

out:
    shard_common_resolve_shards(frame, this, local->post_res_handler);
    return 0;
}

int
shard_lookup_internal_dir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                              int32_t op_ret, int32_t op_errno, inode_t *inode,
                              struct iatt *buf, dict_t *xdata,
                              struct iatt *postparent)
{
    inode_t *link_inode = NULL;
    shard_local_t *local = NULL;
    shard_internal_dir_type_t type = (shard_internal_dir_type_t)cookie;

    local = frame->local;

    if (op_ret) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;
        goto unwind;
    }

    if (!IA_ISDIR(buf->ia_type)) {
        gf_msg(this->name, GF_LOG_CRITICAL, 0, SHARD_MSG_DOT_SHARD_NODIR,
               "%s already exists and "
               "is not a directory. Please remove it from all bricks "
               "and try again",
               shard_internal_dir_string(type));
        local->op_ret = -1;
        local->op_errno = EIO;
        goto unwind;
    }

    link_inode = shard_link_internal_dir_inode(local, inode, buf, type);
    if (link_inode != inode) {
        shard_refresh_internal_dir(frame, this, type);
    } else {
        shard_inode_ctx_mark_dir_refreshed(link_inode, this);
        shard_common_resolve_shards(frame, this, local->post_res_handler);
    }
    return 0;

unwind:
    local->post_res_handler(frame, this);
    return 0;
}

int
shard_lookup_internal_dir(call_frame_t *frame, xlator_t *this,
                          shard_post_resolve_fop_handler_t post_res_handler,
                          shard_internal_dir_type_t type)
{
    int ret = -1;
    dict_t *xattr_req = NULL;
    shard_priv_t *priv = NULL;
    shard_local_t *local = NULL;
    uuid_t *gfid = NULL;
    loc_t *loc = NULL;
    gf_boolean_t free_gfid = _gf_true;

    local = frame->local;
    priv = this->private;
    local->post_res_handler = post_res_handler;

    gfid = GF_MALLOC(sizeof(uuid_t), gf_common_mt_uuid_t);
    if (!gfid)
        goto err;

    xattr_req = dict_new();
    if (!xattr_req) {
        local->op_ret = -1;
        local->op_errno = ENOMEM;
        goto err;
    }

    switch (type) {
        case SHARD_INTERNAL_DIR_DOT_SHARD:
            gf_uuid_copy(*gfid, priv->dot_shard_gfid);
            loc = &local->dot_shard_loc;
            break;
        case SHARD_INTERNAL_DIR_DOT_SHARD_REMOVE_ME:
            gf_uuid_copy(*gfid, priv->dot_shard_rm_gfid);
            loc = &local->dot_shard_rm_loc;
            break;
        default:
            bzero(*gfid, sizeof(uuid_t));
            break;
    }

    ret = dict_set_gfuuid(xattr_req, "gfid-req", *gfid, false);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_DICT_OP_FAILED,
               "Failed to set gfid of %s into dict",
               shard_internal_dir_string(type));
        local->op_ret = -1;
        local->op_errno = ENOMEM;
        goto err;
    } else {
        free_gfid = _gf_false;
    }

    STACK_WIND_COOKIE(frame, shard_lookup_internal_dir_cbk, (void *)(long)type,
                      FIRST_CHILD(this), FIRST_CHILD(this)->fops->lookup, loc,
                      xattr_req);

    dict_unref(xattr_req);
    return 0;

err:
    if (xattr_req)
        dict_unref(xattr_req);
    if (free_gfid)
        GF_FREE(gfid);
    post_res_handler(frame, this);
    return 0;
}

static void
shard_inode_ctx_update(inode_t *inode, xlator_t *this, dict_t *xdata,
                       struct iatt *buf)
{
    int ret = 0;
    uint64_t size = 0;
    void *bsize = NULL;

    if (shard_inode_ctx_get_block_size(inode, this, &size)) {
        /* Fresh lookup */
        ret = dict_get_ptr(xdata, GF_XATTR_SHARD_BLOCK_SIZE, &bsize);
        if (!ret)
            size = ntoh64(*((uint64_t *)bsize));
        /* If the file is sharded, set its block size, otherwise just
         * set 0.
         */

        shard_inode_ctx_set(inode, this, buf, size, SHARD_MASK_BLOCK_SIZE);
    }
    /* If the file is sharded, also set the remaining attributes,
     * except for ia_size and ia_blocks.
     */
    if (size) {
        shard_inode_ctx_set(inode, this, buf, 0, SHARD_LOOKUP_MASK);
        (void)shard_inode_ctx_invalidate(inode, this, buf);
    }
}

int
shard_delete_shards(void *opaque);

int
shard_delete_shards_cbk(int ret, call_frame_t *frame, void *data);

int
shard_start_background_deletion(xlator_t *this)
{
    int ret = 0;
    gf_boolean_t i_cleanup = _gf_true;
    shard_priv_t *priv = NULL;
    call_frame_t *cleanup_frame = NULL;

    priv = this->private;

    LOCK(&priv->lock);
    {
        switch (priv->bg_del_state) {
            case SHARD_BG_DELETION_NONE:
                i_cleanup = _gf_true;
                priv->bg_del_state = SHARD_BG_DELETION_LAUNCHING;
                break;
            case SHARD_BG_DELETION_LAUNCHING:
                i_cleanup = _gf_false;
                break;
            case SHARD_BG_DELETION_IN_PROGRESS:
                priv->bg_del_state = SHARD_BG_DELETION_LAUNCHING;
                i_cleanup = _gf_false;
                break;
            default:
                break;
        }
    }
    UNLOCK(&priv->lock);
    if (!i_cleanup)
        return 0;

    cleanup_frame = create_frame(this, this->ctx->pool);
    if (!cleanup_frame) {
        gf_msg(this->name, GF_LOG_WARNING, ENOMEM, SHARD_MSG_MEMALLOC_FAILED,
               "Failed to create "
               "new frame to delete shards");
        ret = -ENOMEM;
        goto err;
    }

    set_lk_owner_from_ptr(&cleanup_frame->root->lk_owner, cleanup_frame->root);

    ret = synctask_new(this->ctx->env, shard_delete_shards,
                       shard_delete_shards_cbk, cleanup_frame, cleanup_frame);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, errno,
               SHARD_MSG_SHARDS_DELETION_FAILED,
               "failed to create task to do background "
               "cleanup of shards");
        STACK_DESTROY(cleanup_frame->root);
        goto err;
    }
    return 0;

err:
    LOCK(&priv->lock);
    {
        priv->bg_del_state = SHARD_BG_DELETION_NONE;
    }
    UNLOCK(&priv->lock);
    return ret;
}

int
shard_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
    int ret = -1;
    shard_priv_t *priv = NULL;
    gf_boolean_t i_start_cleanup = _gf_false;

    priv = this->private;

    if (op_ret < 0)
        goto unwind;

    if (IA_ISDIR(buf->ia_type))
        goto unwind;

    /* Also, if the file is sharded, get the file size and block cnt xattr,
     * and store them in the stbuf appropriately.
     */

    if (dict_get(xdata, GF_XATTR_SHARD_FILE_SIZE) &&
        frame->root->pid != GF_CLIENT_PID_GSYNCD)
        shard_modify_size_and_block_count(buf, xdata);

    /* If this was a fresh lookup, there are two possibilities:
     * 1) If the file is sharded (indicated by the presence of block size
     *    xattr), store this block size, along with rdev and mode in its
     *    inode ctx.
     * 2) If the file is not sharded, store size along with rdev and mode
     *    (which are anyway don't cares) in inode ctx. Since @ctx_tmp is
     *    already initialised to all zeroes, nothing more needs to be done.
     */

    (void)shard_inode_ctx_update(inode, this, xdata, buf);

    LOCK(&priv->lock);
    {
        if (priv->first_lookup_done == _gf_false) {
            priv->first_lookup_done = _gf_true;
            i_start_cleanup = _gf_true;
        }
    }
    UNLOCK(&priv->lock);

    if (!i_start_cleanup)
        goto unwind;

    ret = shard_start_background_deletion(this);
    if (ret < 0) {
        LOCK(&priv->lock);
        {
            priv->first_lookup_done = _gf_false;
        }
        UNLOCK(&priv->lock);
    }

unwind:
    SHARD_STACK_UNWIND(lookup, frame, op_ret, op_errno, inode, buf, xdata,
                       postparent);
    return 0;
}

int
shard_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xattr_req)
{
    int ret = -1;
    int32_t op_errno = ENOMEM;
    uint64_t block_size = 0;
    shard_local_t *local = NULL;

    this->itable = loc->inode->table;
    if ((frame->root->pid != GF_CLIENT_PID_GSYNCD) &&
        (frame->root->pid != GF_CLIENT_PID_GLFS_HEAL)) {
        SHARD_ENTRY_FOP_CHECK(loc, op_errno, err);
    }

    local = mem_get0(this->local_pool);
    if (!local)
        goto err;

    frame->local = local;

    loc_copy(&local->loc, loc);

    local->xattr_req = xattr_req ? dict_ref(xattr_req) : dict_new();
    if (!local->xattr_req)
        goto err;

    if (shard_inode_ctx_get_block_size(loc->inode, this, &block_size)) {
        ret = dict_set_uint64(local->xattr_req, GF_XATTR_SHARD_BLOCK_SIZE, 0);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, 0, SHARD_MSG_DICT_OP_FAILED,
                   "Failed to set dict"
                   " value: key:%s for path %s",
                   GF_XATTR_SHARD_BLOCK_SIZE, loc->path);
            goto err;
        }
    }

    if (frame->root->pid != GF_CLIENT_PID_GSYNCD) {
        ret = dict_set_uint64(local->xattr_req, GF_XATTR_SHARD_FILE_SIZE,
                              8 * 4);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, 0, SHARD_MSG_DICT_OP_FAILED,
                   "Failed to set dict value: key:%s for path %s.",
                   GF_XATTR_SHARD_FILE_SIZE, loc->path);
            goto err;
        }
    }

    if ((xattr_req) && (dict_get(xattr_req, GF_CONTENT_KEY)))
        dict_del(xattr_req, GF_CONTENT_KEY);

    STACK_WIND(frame, shard_lookup_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->lookup, loc, local->xattr_req);
    return 0;
err:
    shard_common_failure_unwind(GF_FOP_LOOKUP, frame, -1, op_errno);
    return 0;
}

int
shard_set_iattr_invoke_post_handler(call_frame_t *frame, xlator_t *this,
                                    inode_t *inode, int32_t op_ret,
                                    int32_t op_errno, struct iatt *buf,
                                    dict_t *xdata)
{
    int ret = -1;
    int32_t mask = SHARD_INODE_WRITE_MASK;
    shard_local_t *local = frame->local;
    shard_inode_ctx_t ctx = {
        0,
    };

    if (op_ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, op_errno,
               SHARD_MSG_BASE_FILE_LOOKUP_FAILED,
               "Lookup on base file"
               " failed : %s",
               uuid_utoa(inode->gfid));
        local->op_ret = op_ret;
        local->op_errno = op_errno;
        goto unwind;
    }

    local->prebuf = *buf;
    if (shard_modify_size_and_block_count(&local->prebuf, xdata)) {
        local->op_ret = -1;
        local->op_errno = EINVAL;
        goto unwind;
    }

    if (shard_inode_ctx_get_all(inode, this, &ctx))
        mask = SHARD_ALL_MASK;

    ret = shard_inode_ctx_set(inode, this, &local->prebuf, 0,
                              (mask | SHARD_MASK_REFRESH_RESET));
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, SHARD_MSG_INODE_CTX_SET_FAILED, 0,
               "Failed to set inode"
               " write params into inode ctx for %s",
               uuid_utoa(buf->ia_gfid));
        local->op_ret = -1;
        local->op_errno = ENOMEM;
        goto unwind;
    }

unwind:
    local->handler(frame, this);
    return 0;
}

int
shard_fstat_base_file_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, struct iatt *buf,
                          dict_t *xdata)
{
    shard_local_t *local = frame->local;

    shard_set_iattr_invoke_post_handler(frame, this, local->fd->inode, op_ret,
                                        op_errno, buf, xdata);
    return 0;
}

int
shard_lookup_base_file_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, inode_t *inode,
                           struct iatt *buf, dict_t *xdata,
                           struct iatt *postparent)
{
    /* In case of op_ret < 0, inode passed to this function will be NULL
       ex: in case of op_errno = ENOENT. So refer prefilled inode data
       which is part of local.
       Note: Reassigning/overriding the inode passed to this cbk with inode
       which is part of *struct shard_local_t* won't cause any issue as
       both inodes have same reference/address as of the inode passed */
    inode = ((shard_local_t *)frame->local)->loc.inode;

    shard_set_iattr_invoke_post_handler(frame, this, inode, op_ret, op_errno,
                                        buf, xdata);
    return 0;
}

/* This function decides whether to make file based lookup or
 * fd based lookup (fstat) depending on the 3rd and 4th arg.
 * If fd != NULL and loc == NULL then call is for fstat
 * If fd == NULL and loc != NULL then call is for file based
 * lookup. Please pass args based on the requirement.
 */
int
shard_refresh_base_file(call_frame_t *frame, xlator_t *this, loc_t *loc,
                        fd_t *fd, shard_post_fop_handler_t handler)
{
    int ret = -1;
    inode_t *inode = NULL;
    shard_local_t *local = NULL;
    dict_t *xattr_req = NULL;
    gf_boolean_t need_refresh = _gf_false;

    local = frame->local;
    local->handler = handler;
    inode = fd ? fd->inode : loc->inode;

    ret = shard_inode_ctx_fill_iatt_from_cache(inode, this, &local->prebuf,
                                               &need_refresh);
    /* By this time, inode ctx should have been created either in create,
     * mknod, readdirp or lookup. If not it is a bug!
     */
    if ((ret == 0) && (need_refresh == _gf_false)) {
        gf_msg_debug(this->name, 0,
                     "Skipping lookup on base file: %s"
                     "Serving prebuf off the inode ctx cache",
                     uuid_utoa(inode->gfid));
        goto out;
    }

    xattr_req = dict_new();
    if (!xattr_req) {
        local->op_ret = -1;
        local->op_errno = ENOMEM;
        goto out;
    }

    SHARD_MD_READ_FOP_INIT_REQ_DICT(this, xattr_req, inode->gfid, local, out);

    if (fd)
        STACK_WIND(frame, shard_fstat_base_file_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->fstat, fd, xattr_req);
    else
        STACK_WIND(frame, shard_lookup_base_file_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->lookup, loc, xattr_req);

    dict_unref(xattr_req);
    return 0;

out:
    if (xattr_req)
        dict_unref(xattr_req);
    handler(frame, this);
    return 0;
}

int
shard_post_fstat_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->op_ret >= 0)
        shard_inode_ctx_set(local->fd->inode, this, &local->prebuf, 0,
                            SHARD_LOOKUP_MASK);

    SHARD_STACK_UNWIND(fstat, frame, local->op_ret, local->op_errno,
                       &local->prebuf, local->xattr_rsp);
    return 0;
}

int
shard_post_stat_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->op_ret >= 0)
        shard_inode_ctx_set(local->loc.inode, this, &local->prebuf, 0,
                            SHARD_LOOKUP_MASK);

    SHARD_STACK_UNWIND(stat, frame, local->op_ret, local->op_errno,
                       &local->prebuf, local->xattr_rsp);
    return 0;
}

int
shard_common_stat_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *buf,
                      dict_t *xdata)
{
    inode_t *inode = NULL;
    shard_local_t *local = NULL;

    local = frame->local;

    if (op_ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, op_errno, SHARD_MSG_STAT_FAILED,
               "stat failed: %s",
               local->fd ? uuid_utoa(local->fd->inode->gfid)
                         : uuid_utoa((local->loc.inode)->gfid));
        local->op_ret = op_ret;
        local->op_errno = op_errno;
        goto unwind;
    }

    local->prebuf = *buf;
    if (shard_modify_size_and_block_count(&local->prebuf, xdata)) {
        local->op_ret = -1;
        local->op_errno = EINVAL;
        goto unwind;
    }
    local->xattr_rsp = dict_ref(xdata);

    if (local->loc.inode)
        inode = local->loc.inode;
    else
        inode = local->fd->inode;

    shard_inode_ctx_invalidate(inode, this, &local->prebuf);

unwind:
    local->handler(frame, this);
    return 0;
}

int
shard_stat(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    int ret = -1;
    uint64_t block_size = 0;
    shard_local_t *local = NULL;

    if ((IA_ISDIR(loc->inode->ia_type)) || (IA_ISLNK(loc->inode->ia_type))) {
        STACK_WIND(frame, default_stat_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->stat, loc, xdata);
        return 0;
    }

    ret = shard_inode_ctx_get_block_size(loc->inode, this, &block_size);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_CTX_GET_FAILED,
               "Failed to get block "
               "size from inode ctx of %s",
               uuid_utoa(loc->inode->gfid));
        goto err;
    }

    if (!block_size || frame->root->pid == GF_CLIENT_PID_GSYNCD) {
        STACK_WIND(frame, default_stat_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->stat, loc, xdata);
        return 0;
    }

    local = mem_get0(this->local_pool);
    if (!local)
        goto err;

    frame->local = local;

    local->handler = shard_post_stat_handler;
    loc_copy(&local->loc, loc);
    local->xattr_req = (xdata) ? dict_ref(xdata) : dict_new();
    if (!local->xattr_req)
        goto err;

    SHARD_MD_READ_FOP_INIT_REQ_DICT(this, local->xattr_req, local->loc.gfid,
                                    local, err);

    STACK_WIND(frame, shard_common_stat_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->stat, loc, local->xattr_req);
    return 0;
err:
    shard_common_failure_unwind(GF_FOP_STAT, frame, -1, ENOMEM);
    return 0;
}

int
shard_fstat(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    int ret = -1;
    uint64_t block_size = 0;
    shard_local_t *local = NULL;

    if ((IA_ISDIR(fd->inode->ia_type)) || (IA_ISLNK(fd->inode->ia_type))) {
        STACK_WIND(frame, default_fstat_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->fstat, fd, xdata);
        return 0;
    }

    ret = shard_inode_ctx_get_block_size(fd->inode, this, &block_size);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_CTX_GET_FAILED,
               "Failed to get block "
               "size from inode ctx of %s",
               uuid_utoa(fd->inode->gfid));
        goto err;
    }

    if (!block_size || frame->root->pid == GF_CLIENT_PID_GSYNCD) {
        STACK_WIND(frame, default_fstat_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->fstat, fd, xdata);
        return 0;
    }

    if (!this->itable)
        this->itable = fd->inode->table;

    local = mem_get0(this->local_pool);
    if (!local)
        goto err;

    frame->local = local;

    local->handler = shard_post_fstat_handler;
    local->fd = fd_ref(fd);
    local->xattr_req = (xdata) ? dict_ref(xdata) : dict_new();
    if (!local->xattr_req)
        goto err;

    SHARD_MD_READ_FOP_INIT_REQ_DICT(this, local->xattr_req, fd->inode->gfid,
                                    local, err);

    STACK_WIND(frame, shard_common_stat_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fstat, fd, local->xattr_req);
    return 0;
err:
    shard_common_failure_unwind(GF_FOP_FSTAT, frame, -1, ENOMEM);
    return 0;
}

int
shard_post_update_size_truncate_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->fop == GF_FOP_TRUNCATE)
        SHARD_STACK_UNWIND(truncate, frame, local->op_ret, local->op_errno,
                           &local->prebuf, &local->postbuf, NULL);
    else
        SHARD_STACK_UNWIND(ftruncate, frame, local->op_ret, local->op_errno,
                           &local->prebuf, &local->postbuf, NULL);
    return 0;
}

int
shard_truncate_last_shard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                              int32_t op_ret, int32_t op_errno,
                              struct iatt *prebuf, struct iatt *postbuf,
                              dict_t *xdata)
{
    inode_t *inode = NULL;
    int64_t delta_blocks = 0;
    shard_local_t *local = NULL;

    local = frame->local;

    SHARD_UNSET_ROOT_FS_ID(frame, local);

    inode = (local->fop == GF_FOP_TRUNCATE) ? local->loc.inode
                                            : local->fd->inode;
    if (op_ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, op_errno,
               SHARD_MSG_TRUNCATE_LAST_SHARD_FAILED,
               "truncate on last"
               " shard failed : %s",
               uuid_utoa(inode->gfid));
        local->op_ret = op_ret;
        local->op_errno = op_errno;
        goto err;
    }

    local->postbuf.ia_size = local->offset;
    /* Let the delta be negative. We want xattrop to do subtraction */
    local->delta_size = local->postbuf.ia_size - local->prebuf.ia_size;
    delta_blocks = GF_ATOMIC_ADD(local->delta_blocks,
                                 postbuf->ia_blocks - prebuf->ia_blocks);
    GF_ASSERT(delta_blocks <= 0);
    local->postbuf.ia_blocks += delta_blocks;
    local->hole_size = 0;

    shard_inode_ctx_set(inode, this, &local->postbuf, 0, SHARD_MASK_TIMES);
    shard_update_file_size(frame, this, NULL, &local->loc,
                           shard_post_update_size_truncate_handler);
    return 0;
err:
    shard_common_failure_unwind(local->fop, frame, local->op_ret,
                                local->op_errno);
    return 0;
}

int
shard_truncate_last_shard(call_frame_t *frame, xlator_t *this, inode_t *inode)
{
    size_t last_shard_size_after = 0;
    loc_t loc = {
        0,
    };
    shard_local_t *local = NULL;

    local = frame->local;

    /* A NULL inode could be due to the fact that the last shard which
     * needs to be truncated does not exist due to it lying in a hole
     * region. So the only thing left to do in that case would be an
     * update to file size xattr.
     */
    if (!inode) {
        gf_msg_debug(this->name, 0,
                     "Last shard to be truncated absent in backend: %" PRIu64
                     " of gfid %s. Directly proceeding to update file size",
                     local->first_block, uuid_utoa(local->loc.inode->gfid));
        shard_update_file_size(frame, this, NULL, &local->loc,
                               shard_post_update_size_truncate_handler);
        return 0;
    }

    SHARD_SET_ROOT_FS_ID(frame, local);

    loc.inode = inode_ref(inode);
    gf_uuid_copy(loc.gfid, inode->gfid);

    last_shard_size_after = (local->offset % local->block_size);

    STACK_WIND(frame, shard_truncate_last_shard_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->truncate, &loc, last_shard_size_after,
               NULL);
    loc_wipe(&loc);
    return 0;
}

void
shard_unlink_block_inode(shard_local_t *local, int shard_block_num);

int
shard_truncate_htol_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno,
                        struct iatt *preparent, struct iatt *postparent,
                        dict_t *xdata)
{
    int ret = 0;
    int call_count = 0;
    int shard_block_num = (long)cookie;
    uint64_t block_count = 0;
    shard_local_t *local = NULL;

    local = frame->local;

    if (op_ret < 0) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;
        goto done;
    }
    ret = dict_get_uint64(xdata, GF_GET_FILE_BLOCK_COUNT, &block_count);
    if (!ret) {
        GF_ATOMIC_SUB(local->delta_blocks, block_count);
    } else {
        /* dict_get failed possibly due to a heterogeneous cluster? */
        gf_msg(this->name, GF_LOG_WARNING, 0, SHARD_MSG_DICT_OP_FAILED,
               "Failed to get key %s from dict during truncate of gfid %s",
               GF_GET_FILE_BLOCK_COUNT,
               uuid_utoa(local->resolver_base_inode->gfid));
    }

    shard_unlink_block_inode(local, shard_block_num);
done:
    call_count = shard_call_count_return(frame);
    if (call_count == 0) {
        SHARD_UNSET_ROOT_FS_ID(frame, local);
        shard_truncate_last_shard(frame, this, local->inode_list[0]);
    }
    return 0;
}

int
shard_truncate_htol(call_frame_t *frame, xlator_t *this, inode_t *inode)
{
    int i = 1;
    int ret = -1;
    int call_count = 0;
    uint32_t cur_block = 0;
    uint32_t last_block = 0;
    char path[PATH_MAX] = {
        0,
    };
    char *bname = NULL;
    loc_t loc = {
        0,
    };
    gf_boolean_t wind_failed = _gf_false;
    shard_local_t *local = NULL;
    shard_priv_t *priv = NULL;
    dict_t *xdata_req = NULL;

    local = frame->local;
    priv = this->private;

    cur_block = local->first_block + 1;
    last_block = local->last_block;

    /* Determine call count */
    for (i = 1; i < local->num_blocks; i++) {
        if (!local->inode_list[i])
            continue;
        call_count++;
    }

    if (!call_count) {
        /* Call count = 0 implies that all of the shards that need to be
         * unlinked do not exist. So shard xlator would now proceed to
         * do the final truncate + size updates.
         */
        gf_msg_debug(this->name, 0,
                     "Shards to be unlinked as part of "
                     "truncate absent in backend: %s. Directly "
                     "proceeding to update file size",
                     uuid_utoa(inode->gfid));
        local->postbuf.ia_size = local->offset;
        local->postbuf.ia_blocks = local->prebuf.ia_blocks;
        local->delta_size = local->postbuf.ia_size - local->prebuf.ia_size;
        GF_ATOMIC_INIT(local->delta_blocks, 0);
        local->hole_size = 0;
        shard_update_file_size(frame, this, local->fd, &local->loc,
                               shard_post_update_size_truncate_handler);
        return 0;
    }

    local->call_count = call_count;
    i = 1;
    xdata_req = dict_new();
    if (!xdata_req) {
        shard_common_failure_unwind(local->fop, frame, -1, ENOMEM);
        return 0;
    }
    ret = dict_set_uint64(xdata_req, GF_GET_FILE_BLOCK_COUNT, 8 * 8);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, 0, SHARD_MSG_DICT_OP_FAILED,
               "Failed to set key %s into dict during truncate of %s",
               GF_GET_FILE_BLOCK_COUNT,
               uuid_utoa(local->resolver_base_inode->gfid));
        dict_unref(xdata_req);
        shard_common_failure_unwind(local->fop, frame, -1, ENOMEM);
        return 0;
    }

    SHARD_SET_ROOT_FS_ID(frame, local);
    while (cur_block <= last_block) {
        if (!local->inode_list[i]) {
            cur_block++;
            i++;
            continue;
        }
        if (wind_failed) {
            shard_truncate_htol_cbk(frame, (void *)(long)cur_block, this, -1,
                                    ENOMEM, NULL, NULL, NULL);
            goto next;
        }

        shard_make_block_abspath(cur_block, inode->gfid, path, sizeof(path));
        bname = strrchr(path, '/') + 1;
        loc.parent = inode_ref(priv->dot_shard_inode);
        ret = inode_path(loc.parent, bname, (char **)&(loc.path));
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_PATH_FAILED,
                   "Inode path failed"
                   " on %s. Base file gfid = %s",
                   bname, uuid_utoa(inode->gfid));
            local->op_ret = -1;
            local->op_errno = ENOMEM;
            loc_wipe(&loc);
            wind_failed = _gf_true;
            shard_truncate_htol_cbk(frame, (void *)(long)cur_block, this, -1,
                                    ENOMEM, NULL, NULL, NULL);
            goto next;
        }
        loc.name = strrchr(loc.path, '/');
        if (loc.name)
            loc.name++;
        loc.inode = inode_ref(local->inode_list[i]);

        STACK_WIND_COOKIE(frame, shard_truncate_htol_cbk,
                          (void *)(long)cur_block, FIRST_CHILD(this),
                          FIRST_CHILD(this)->fops->unlink, &loc, 0, xdata_req);
        loc_wipe(&loc);
    next:
        i++;
        cur_block++;
        if (!--call_count)
            break;
    }
    dict_unref(xdata_req);
    return 0;
}

int
shard_truncate_do(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->num_blocks == 1) {
        /* This means that there are no shards to be unlinked.
         * The fop boils down to truncating the last shard, updating
         * the size and unwinding.
         */
        shard_truncate_last_shard(frame, this, local->inode_list[0]);
        return 0;
    } else {
        shard_truncate_htol(frame, this, local->loc.inode);
    }
    return 0;
}

int
shard_post_lookup_shards_truncate_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(local->fop, frame, local->op_ret,
                                    local->op_errno);
        return 0;
    }

    shard_truncate_do(frame, this);
    return 0;
}

void
shard_link_block_inode(shard_local_t *local, int block_num, inode_t *inode,
                       struct iatt *buf)
{
    int list_index = 0;
    char block_bname[256] = {
        0,
    };
    uuid_t gfid = {
        0,
    };
    inode_t *linked_inode = NULL;
    xlator_t *this = NULL;
    inode_t *fsync_inode = NULL;
    shard_priv_t *priv = NULL;
    inode_t *base_inode = NULL;

    this = THIS;
    priv = this->private;
    if (local->loc.inode) {
        gf_uuid_copy(gfid, local->loc.inode->gfid);
        base_inode = local->loc.inode;
    } else if (local->resolver_base_inode) {
        gf_uuid_copy(gfid, local->resolver_base_inode->gfid);
        base_inode = local->resolver_base_inode;
    } else {
        gf_uuid_copy(gfid, local->base_gfid);
    }

    shard_make_block_bname(block_num, gfid, block_bname, sizeof(block_bname));

    shard_inode_ctx_set(inode, this, buf, 0, SHARD_LOOKUP_MASK);
    linked_inode = inode_link(inode, priv->dot_shard_inode, block_bname, buf);
    inode_lookup(linked_inode);
    list_index = block_num - local->first_block;
    local->inode_list[list_index] = linked_inode;

    LOCK(&priv->lock);
    {
        fsync_inode = __shard_update_shards_inode_list(
            linked_inode, this, base_inode, block_num, gfid);
    }
    UNLOCK(&priv->lock);
    if (fsync_inode)
        shard_initiate_evicted_inode_fsync(this, fsync_inode);
}

int
shard_common_lookup_shards_cbk(call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret, int32_t op_errno,
                               inode_t *inode, struct iatt *buf, dict_t *xdata,
                               struct iatt *postparent)
{
    int call_count = 0;
    int shard_block_num = (long)cookie;
    uuid_t gfid = {
        0,
    };
    shard_local_t *local = NULL;

    local = frame->local;
    if (local->resolver_base_inode)
        gf_uuid_copy(gfid, local->resolver_base_inode->gfid);
    else
        gf_uuid_copy(gfid, local->base_gfid);

    if (op_ret < 0) {
        /* Ignore absence of shards in the backend in truncate fop. */
        switch (local->fop) {
            case GF_FOP_TRUNCATE:
            case GF_FOP_FTRUNCATE:
            case GF_FOP_RENAME:
            case GF_FOP_UNLINK:
                if (op_errno == ENOENT)
                    goto done;
                break;
            case GF_FOP_WRITE:
            case GF_FOP_READ:
            case GF_FOP_ZEROFILL:
            case GF_FOP_DISCARD:
            case GF_FOP_FALLOCATE:
                if ((!local->first_lookup_done) && (op_errno == ENOENT)) {
                    LOCK(&frame->lock);
                    {
                        local->create_count++;
                    }
                    UNLOCK(&frame->lock);
                    goto done;
                }
                break;
            default:
                break;
        }

        /* else */
        gf_msg(this->name, GF_LOG_ERROR, op_errno,
               SHARD_MSG_LOOKUP_SHARD_FAILED,
               "Lookup on shard %d "
               "failed. Base file gfid = %s",
               shard_block_num, uuid_utoa(gfid));
        local->op_ret = op_ret;
        local->op_errno = op_errno;
        goto done;
    }

    shard_link_block_inode(local, shard_block_num, inode, buf);

done:
    if (local->lookup_shards_barriered) {
        syncbarrier_wake(&local->barrier);
        return 0;
    } else {
        call_count = shard_call_count_return(frame);
        if (call_count == 0) {
            if (!local->first_lookup_done)
                local->first_lookup_done = _gf_true;
            local->pls_fop_handler(frame, this);
        }
    }
    return 0;
}

dict_t *
shard_create_gfid_dict(dict_t *dict)
{
    int ret = 0;
    dict_t *new = NULL;
    unsigned char *gfid = NULL;

    new = dict_copy_with_ref(dict, NULL);
    if (!new)
        return NULL;

    gfid = GF_MALLOC(sizeof(uuid_t), gf_common_mt_char);
    if (!gfid) {
        ret = -1;
        goto out;
    }

    gf_uuid_generate(gfid);

    ret = dict_set_gfuuid(new, "gfid-req", gfid, false);

out:
    if (ret) {
        dict_unref(new);
        new = NULL;
        GF_FREE(gfid);
    }

    return new;
}

int
shard_common_lookup_shards(call_frame_t *frame, xlator_t *this, inode_t *inode,
                           shard_post_lookup_shards_fop_handler_t handler)
{
    int i = 0;
    int ret = 0;
    int count = 0;
    int call_count = 0;
    int32_t shard_idx_iter = 0;
    int lookup_count = 0;
    char path[PATH_MAX] = {
        0,
    };
    char *bname = NULL;
    uuid_t gfid = {
        0,
    };
    loc_t loc = {
        0,
    };
    shard_local_t *local = NULL;
    shard_priv_t *priv = NULL;
    gf_boolean_t wind_failed = _gf_false;
    dict_t *xattr_req = NULL;

    priv = this->private;
    local = frame->local;
    count = call_count = local->call_count;
    shard_idx_iter = local->first_block;
    lookup_count = local->last_block - local->create_count;
    local->pls_fop_handler = handler;
    if (local->lookup_shards_barriered)
        local->barrier.waitfor = local->call_count;

    if (inode)
        gf_uuid_copy(gfid, inode->gfid);
    else
        gf_uuid_copy(gfid, local->base_gfid);

    while (shard_idx_iter <= lookup_count) {
        if (local->inode_list[i]) {
            i++;
            shard_idx_iter++;
            continue;
        }

        if (wind_failed) {
            shard_common_lookup_shards_cbk(frame, (void *)(long)shard_idx_iter,
                                           this, -1, ENOMEM, NULL, NULL, NULL,
                                           NULL);
            goto next;
        }

        shard_make_block_abspath(shard_idx_iter, gfid, path, sizeof(path));

        bname = strrchr(path, '/') + 1;
        loc.inode = inode_new(this->itable);
        loc.parent = inode_ref(priv->dot_shard_inode);
        gf_uuid_copy(loc.pargfid, priv->dot_shard_gfid);
        ret = inode_path(loc.parent, bname, (char **)&(loc.path));
        if (ret < 0 || !(loc.inode)) {
            gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_PATH_FAILED,
                   "Inode path failed"
                   " on %s, base file gfid = %s",
                   bname, uuid_utoa(gfid));
            local->op_ret = -1;
            local->op_errno = ENOMEM;
            loc_wipe(&loc);
            wind_failed = _gf_true;
            shard_common_lookup_shards_cbk(frame, (void *)(long)shard_idx_iter,
                                           this, -1, ENOMEM, NULL, NULL, NULL,
                                           NULL);
            goto next;
        }

        loc.name = strrchr(loc.path, '/');
        if (loc.name)
            loc.name++;

        xattr_req = shard_create_gfid_dict(local->xattr_req);
        if (!xattr_req) {
            local->op_ret = -1;
            local->op_errno = ENOMEM;
            wind_failed = _gf_true;
            loc_wipe(&loc);
            shard_common_lookup_shards_cbk(frame, (void *)(long)shard_idx_iter,
                                           this, -1, ENOMEM, NULL, NULL, NULL,
                                           NULL);
            goto next;
        }

        STACK_WIND_COOKIE(frame, shard_common_lookup_shards_cbk,
                          (void *)(long)shard_idx_iter, FIRST_CHILD(this),
                          FIRST_CHILD(this)->fops->lookup, &loc, xattr_req);
        loc_wipe(&loc);
        dict_unref(xattr_req);
    next:
        shard_idx_iter++;
        i++;

        if (!--call_count)
            break;
    }
    if (local->lookup_shards_barriered) {
        syncbarrier_wait(&local->barrier, count);
        local->pls_fop_handler(frame, this);
    }
    return 0;
}

int
shard_post_resolve_truncate_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->op_ret < 0) {
        if (local->op_errno == ENOENT) {
            /* If lookup on /.shard fails with ENOENT, it means that
             * the file was 0-byte in size but truncated sometime in
             * the past to a higher size which is reflected in the
             * size xattr, and now being truncated to a lower size.
             * In this case, the only thing that needs to be done is
             * to update the size xattr of the file and unwind.
             */
            local->first_block = local->last_block = 0;
            local->num_blocks = 1;
            local->call_count = 0;
            local->op_ret = 0;
            local->postbuf.ia_size = local->offset;
            shard_update_file_size(frame, this, local->fd, &local->loc,
                                   shard_post_update_size_truncate_handler);
            return 0;
        } else {
            shard_common_failure_unwind(local->fop, frame, local->op_ret,
                                        local->op_errno);
            return 0;
        }
    }

    if (!local->call_count)
        shard_truncate_do(frame, this);
    else
        shard_common_lookup_shards(frame, this, local->loc.inode,
                                   shard_post_lookup_shards_truncate_handler);

    return 0;
}

int
shard_truncate_begin(call_frame_t *frame, xlator_t *this)
{
    int ret = 0;
    shard_local_t *local = NULL;
    shard_priv_t *priv = NULL;

    priv = this->private;
    local = frame->local;

    /* First participant block here is the lowest numbered block that would
     * hold the last byte of the file post successful truncation.
     * Last participant block is the block that contains the last byte in
     * the current state of the file.
     * If (first block == last_block):
     *         then that means that the file only needs truncation of the
     *         first (or last since both are same) block.
     * Else
     *         if (new_size % block_size == 0)
     *                 then that means there is no truncate to be done with
     *                 only shards from first_block + 1 through the last
     *                 block needing to be unlinked.
     *         else
     *                 both truncate of the first block and unlink of the
     *                 remaining shards until end of file is required.
     */
    local->first_block = (local->offset == 0)
                             ? 0
                             : get_lowest_block(local->offset - 1,
                                                local->block_size);
    local->last_block = get_highest_block(0, local->prebuf.ia_size,
                                          local->block_size);

    local->num_blocks = local->last_block - local->first_block + 1;
    GF_ASSERT(local->num_blocks > 0);
    local->resolver_base_inode = (local->fop == GF_FOP_TRUNCATE)
                                     ? local->loc.inode
                                     : local->fd->inode;

    if ((local->first_block == 0) && (local->num_blocks == 1)) {
        if (local->fop == GF_FOP_TRUNCATE)
            STACK_WIND(frame, shard_truncate_last_shard_cbk, FIRST_CHILD(this),
                       FIRST_CHILD(this)->fops->truncate, &local->loc,
                       local->offset, local->xattr_req);
        else
            STACK_WIND(frame, shard_truncate_last_shard_cbk, FIRST_CHILD(this),
                       FIRST_CHILD(this)->fops->ftruncate, local->fd,
                       local->offset, local->xattr_req);
        return 0;
    }

    local->inode_list = GF_CALLOC(local->num_blocks, sizeof(inode_t *),
                                  gf_shard_mt_inode_list);
    if (!local->inode_list)
        goto err;

    local->dot_shard_loc.inode = inode_find(this->itable, priv->dot_shard_gfid);
    if (!local->dot_shard_loc.inode) {
        ret = shard_init_internal_dir_loc(this, local,
                                          SHARD_INTERNAL_DIR_DOT_SHARD);
        if (ret)
            goto err;
        shard_lookup_internal_dir(frame, this,
                                  shard_post_resolve_truncate_handler,
                                  SHARD_INTERNAL_DIR_DOT_SHARD);
    } else {
        local->post_res_handler = shard_post_resolve_truncate_handler;
        shard_refresh_internal_dir(frame, this, SHARD_INTERNAL_DIR_DOT_SHARD);
    }
    return 0;

err:
    shard_common_failure_unwind(local->fop, frame, -1, ENOMEM);
    return 0;
}

int
shard_post_lookup_truncate_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;
    struct iatt tmp_stbuf = {
        0,
    };

    local = frame->local;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(local->fop, frame, local->op_ret,
                                    local->op_errno);
        return 0;
    }

    local->postbuf = tmp_stbuf = local->prebuf;

    if (local->prebuf.ia_size == local->offset) {
        /* If the file size is same as requested size, unwind the call
         * immediately.
         */
        if (local->fop == GF_FOP_TRUNCATE)
            SHARD_STACK_UNWIND(truncate, frame, 0, 0, &local->prebuf,
                               &local->postbuf, NULL);
        else
            SHARD_STACK_UNWIND(ftruncate, frame, 0, 0, &local->prebuf,
                               &local->postbuf, NULL);
    } else if (local->offset > local->prebuf.ia_size) {
        /* If the truncate is from a lower to a higher size, set the
         * new size xattr and unwind.
         */
        local->hole_size = local->offset - local->prebuf.ia_size;
        local->delta_size = 0;
        GF_ATOMIC_INIT(local->delta_blocks, 0);
        local->postbuf.ia_size = local->offset;
        tmp_stbuf.ia_size = local->offset;
        shard_inode_ctx_set(local->loc.inode, this, &tmp_stbuf, 0,
                            SHARD_INODE_WRITE_MASK);
        shard_update_file_size(frame, this, NULL, &local->loc,
                               shard_post_update_size_truncate_handler);
    } else {
        /* ... else
         * i.   unlink all shards that need to be unlinked.
         * ii.  truncate the last of the shards.
         * iii. update the new size using setxattr.
         * and unwind the fop.
         */
        local->hole_size = 0;
        local->delta_size = (local->offset - local->prebuf.ia_size);
        GF_ATOMIC_INIT(local->delta_blocks, 0);
        tmp_stbuf.ia_size = local->offset;
        shard_inode_ctx_set(local->loc.inode, this, &tmp_stbuf, 0,
                            SHARD_INODE_WRITE_MASK);
        shard_truncate_begin(frame, this);
    }
    return 0;
}

/* TO-DO:
 * Fix updates to size and block count with racing write(s) and truncate(s).
 */

int
shard_truncate(call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
               dict_t *xdata)
{
    int ret = -1;
    uint64_t block_size = 0;
    shard_local_t *local = NULL;

    ret = shard_inode_ctx_get_block_size(loc->inode, this, &block_size);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_CTX_GET_FAILED,
               "Failed to get block "
               "size from inode ctx of %s",
               uuid_utoa(loc->inode->gfid));
        goto err;
    }

    if (!block_size || frame->root->pid == GF_CLIENT_PID_GSYNCD) {
        STACK_WIND(frame, default_truncate_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->truncate, loc, offset, xdata);
        return 0;
    }

    if (!this->itable)
        this->itable = loc->inode->table;

    local = mem_get0(this->local_pool);
    if (!local)
        goto err;

    frame->local = local;

    ret = syncbarrier_init(&local->barrier);
    if (ret)
        goto err;
    loc_copy(&local->loc, loc);
    local->offset = offset;
    local->block_size = block_size;
    local->fop = GF_FOP_TRUNCATE;
    local->xattr_req = (xdata) ? dict_ref(xdata) : dict_new();
    if (!local->xattr_req)
        goto err;
    local->resolver_base_inode = loc->inode;
    GF_ATOMIC_INIT(local->delta_blocks, 0);

    shard_refresh_base_file(frame, this, &local->loc, NULL,
                            shard_post_lookup_truncate_handler);
    return 0;

err:
    shard_common_failure_unwind(GF_FOP_TRUNCATE, frame, -1, ENOMEM);
    return 0;
}

int
shard_ftruncate(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
                dict_t *xdata)
{
    int ret = -1;
    uint64_t block_size = 0;
    shard_local_t *local = NULL;

    ret = shard_inode_ctx_get_block_size(fd->inode, this, &block_size);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_CTX_GET_FAILED,
               "Failed to get block "
               "size from inode ctx of %s",
               uuid_utoa(fd->inode->gfid));
        goto err;
    }

    if (!block_size || frame->root->pid == GF_CLIENT_PID_GSYNCD) {
        STACK_WIND(frame, default_ftruncate_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->ftruncate, fd, offset, xdata);
        return 0;
    }

    if (!this->itable)
        this->itable = fd->inode->table;

    local = mem_get0(this->local_pool);
    if (!local)
        goto err;

    frame->local = local;
    ret = syncbarrier_init(&local->barrier);
    if (ret)
        goto err;
    local->fd = fd_ref(fd);
    local->offset = offset;
    local->block_size = block_size;
    local->xattr_req = (xdata) ? dict_ref(xdata) : dict_new();
    if (!local->xattr_req)
        goto err;
    local->fop = GF_FOP_FTRUNCATE;

    local->loc.inode = inode_ref(fd->inode);
    gf_uuid_copy(local->loc.gfid, fd->inode->gfid);
    local->resolver_base_inode = fd->inode;
    GF_ATOMIC_INIT(local->delta_blocks, 0);

    shard_refresh_base_file(frame, this, NULL, fd,
                            shard_post_lookup_truncate_handler);
    return 0;
err:
    shard_common_failure_unwind(GF_FOP_FTRUNCATE, frame, -1, ENOMEM);
    return 0;
}

int
shard_mknod_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, inode_t *inode,
                struct iatt *buf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
    int ret = -1;
    shard_local_t *local = NULL;

    local = frame->local;

    if (op_ret == -1)
        goto unwind;

    ret = shard_inode_ctx_set(inode, this, buf, local->block_size,
                              SHARD_ALL_MASK);
    if (ret)
        gf_msg(this->name, GF_LOG_WARNING, 0, SHARD_MSG_INODE_CTX_SET_FAILED,
               "Failed to set inode "
               "ctx for %s",
               uuid_utoa(inode->gfid));

unwind:
    SHARD_STACK_UNWIND(mknod, frame, op_ret, op_errno, inode, buf, preparent,
                       postparent, xdata);

    return 0;
}

int
shard_mknod(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
            dev_t rdev, mode_t umask, dict_t *xdata)
{
    shard_priv_t *priv = NULL;
    shard_local_t *local = NULL;

    priv = this->private;
    local = mem_get0(this->local_pool);
    if (!local)
        goto err;

    frame->local = local;
    local->block_size = priv->block_size;
    if (!__is_gsyncd_on_shard_dir(frame, loc)) {
        SHARD_INODE_CREATE_INIT(this, local->block_size, xdata, loc, 0, 0, err);
    }

    STACK_WIND(frame, shard_mknod_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->mknod, loc, mode, rdev, umask, xdata);
    return 0;
err:
    shard_common_failure_unwind(GF_FOP_MKNOD, frame, -1, ENOMEM);
    return 0;
}

int32_t
shard_link_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *buf, struct iatt *preparent,
               struct iatt *postparent, dict_t *xdata)
{
    shard_local_t *local = NULL;

    local = frame->local;
    if (op_ret < 0)
        goto err;

    shard_inode_ctx_set(inode, this, buf, 0,
                        SHARD_MASK_NLINK | SHARD_MASK_TIMES);
    buf->ia_size = local->prebuf.ia_size;
    buf->ia_blocks = local->prebuf.ia_blocks;

    SHARD_STACK_UNWIND(link, frame, op_ret, op_errno, inode, buf, preparent,
                       postparent, xdata);
    return 0;
err:
    shard_common_failure_unwind(GF_FOP_LINK, frame, op_ret, op_errno);
    return 0;
}

int
shard_post_lookup_link_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->op_ret < 0) {
        SHARD_STACK_UNWIND(link, frame, local->op_ret, local->op_errno, NULL,
                           NULL, NULL, NULL, NULL);
        return 0;
    }

    STACK_WIND(frame, shard_link_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->link, &local->loc, &local->loc2,
               local->xattr_req);
    return 0;
}

int32_t
shard_link(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
           dict_t *xdata)
{
    int ret = -1;
    uint64_t block_size = 0;
    shard_local_t *local = NULL;

    ret = shard_inode_ctx_get_block_size(oldloc->inode, this, &block_size);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_CTX_GET_FAILED,
               "Failed to get block "
               "size from inode ctx of %s",
               uuid_utoa(oldloc->inode->gfid));
        goto err;
    }

    if (!block_size) {
        STACK_WIND_TAIL(frame, FIRST_CHILD(this), FIRST_CHILD(this)->fops->link,
                        oldloc, newloc, xdata);
        return 0;
    }

    if (!this->itable)
        this->itable = oldloc->inode->table;

    local = mem_get0(this->local_pool);
    if (!local)
        goto err;

    frame->local = local;

    loc_copy(&local->loc, oldloc);
    loc_copy(&local->loc2, newloc);
    local->xattr_req = (xdata) ? dict_ref(xdata) : dict_new();
    if (!local->xattr_req)
        goto err;

    shard_refresh_base_file(frame, this, &local->loc, NULL,
                            shard_post_lookup_link_handler);
    return 0;
err:
    shard_common_failure_unwind(GF_FOP_LINK, frame, -1, ENOMEM);
    return 0;
}

int
shard_unlink_shards_do(call_frame_t *frame, xlator_t *this, inode_t *inode);

int
shard_post_lookup_shards_unlink_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;
    uuid_t gfid = {
        0,
    };

    local = frame->local;

    if (local->resolver_base_inode)
        gf_uuid_copy(gfid, local->resolver_base_inode->gfid);
    else
        gf_uuid_copy(gfid, local->base_gfid);

    if ((local->op_ret < 0) && (local->op_errno != ENOENT)) {
        gf_msg(this->name, GF_LOG_ERROR, local->op_errno, SHARD_MSG_FOP_FAILED,
               "failed to delete shards of %s", uuid_utoa(gfid));
        return 0;
    }
    local->op_ret = 0;
    local->op_errno = 0;

    shard_unlink_shards_do(frame, this, local->resolver_base_inode);
    return 0;
}

int
shard_post_resolve_unlink_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;
    local->lookup_shards_barriered = _gf_true;

    if (!local->call_count)
        shard_unlink_shards_do(frame, this, local->resolver_base_inode);
    else
        shard_common_lookup_shards(frame, this, local->resolver_base_inode,
                                   shard_post_lookup_shards_unlink_handler);
    return 0;
}

void
shard_unlink_block_inode(shard_local_t *local, int shard_block_num)
{
    char block_bname[256] = {
        0,
    };
    uuid_t gfid = {
        0,
    };
    inode_t *inode = NULL;
    inode_t *base_inode = NULL;
    xlator_t *this = NULL;
    shard_priv_t *priv = NULL;
    shard_inode_ctx_t *ctx = NULL;
    shard_inode_ctx_t *base_ictx = NULL;
    int unref_base_inode = 0;
    int unref_shard_inode = 0;

    this = THIS;
    priv = this->private;

    inode = local->inode_list[shard_block_num - local->first_block];
    shard_inode_ctx_get(inode, this, &ctx);
    base_inode = ctx->base_inode;
    if (base_inode)
        gf_uuid_copy(gfid, base_inode->gfid);
    else
        gf_uuid_copy(gfid, ctx->base_gfid);
    shard_make_block_bname(shard_block_num, gfid, block_bname,
                           sizeof(block_bname));

    LOCK(&priv->lock);
    if (base_inode)
        LOCK(&base_inode->lock);
    LOCK(&inode->lock);
    {
        __shard_inode_ctx_get(inode, this, &ctx);
        if (!list_empty(&ctx->ilist)) {
            list_del_init(&ctx->ilist);
            priv->inode_count--;
            unref_base_inode++;
            unref_shard_inode++;
            GF_ASSERT(priv->inode_count >= 0);
        }
        if (ctx->fsync_needed) {
            unref_base_inode++;
            unref_shard_inode++;
            list_del_init(&ctx->to_fsync_list);
            if (base_inode) {
                __shard_inode_ctx_get(base_inode, this, &base_ictx);
                base_ictx->fsync_count--;
            }
        }
    }
    UNLOCK(&inode->lock);
    if (base_inode)
        UNLOCK(&base_inode->lock);

    inode_unlink(inode, priv->dot_shard_inode, block_bname);
    inode_ref_reduce_by_n(inode, unref_shard_inode);
    inode_forget(inode, 0);

    if (base_inode && unref_base_inode)
        inode_ref_reduce_by_n(base_inode, unref_base_inode);
    UNLOCK(&priv->lock);
}

int
shard_rename_cbk(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    SHARD_STACK_UNWIND(rename, frame, local->op_ret, local->op_errno,
                       &local->prebuf, &local->preoldparent,
                       &local->postoldparent, &local->prenewparent,
                       &local->postnewparent, local->xattr_rsp);
    return 0;
}

int32_t
shard_unlink_cbk(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = frame->local;

    SHARD_STACK_UNWIND(unlink, frame, local->op_ret, local->op_errno,
                       &local->preoldparent, &local->postoldparent,
                       local->xattr_rsp);
    return 0;
}

int
shard_unlink_shards_do_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno,
                           struct iatt *preparent, struct iatt *postparent,
                           dict_t *xdata)
{
    int shard_block_num = (long)cookie;
    shard_local_t *local = NULL;

    local = frame->local;

    if (op_ret < 0) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;
        goto done;
    }

    shard_unlink_block_inode(local, shard_block_num);
done:
    syncbarrier_wake(&local->barrier);
    return 0;
}

int
shard_unlink_shards_do(call_frame_t *frame, xlator_t *this, inode_t *inode)
{
    int i = 0;
    int ret = -1;
    int count = 0;
    uint32_t cur_block = 0;
    uint32_t cur_block_idx = 0; /*this is idx into inode_list[] array */
    char *bname = NULL;
    char path[PATH_MAX] = {
        0,
    };
    uuid_t gfid = {
        0,
    };
    loc_t loc = {
        0,
    };
    gf_boolean_t wind_failed = _gf_false;
    shard_local_t *local = NULL;
    shard_priv_t *priv = NULL;

    priv = this->private;
    local = frame->local;

    if (inode)
        gf_uuid_copy(gfid, inode->gfid);
    else
        gf_uuid_copy(gfid, local->base_gfid);

    for (i = 0; i < local->num_blocks; i++) {
        if (!local->inode_list[i])
            continue;
        count++;
    }

    if (!count) {
        /* callcount = 0 implies that all of the shards that need to be
         * unlinked are non-existent (in other words the file is full of
         * holes).
         */
        gf_msg_debug(this->name, 0,
                     "All shards that need to be "
                     "unlinked are non-existent: %s",
                     uuid_utoa(gfid));
        return 0;
    }

    SHARD_SET_ROOT_FS_ID(frame, local);
    local->barrier.waitfor = count;
    cur_block = cur_block_idx + local->first_block;

    while (cur_block_idx < local->num_blocks) {
        if (!local->inode_list[cur_block_idx])
            goto next;

        if (wind_failed) {
            shard_unlink_shards_do_cbk(frame, (void *)(long)cur_block, this, -1,
                                       ENOMEM, NULL, NULL, NULL);
            goto next;
        }

        shard_make_block_abspath(cur_block, gfid, path, sizeof(path));
        bname = strrchr(path, '/') + 1;
        loc.parent = inode_ref(priv->dot_shard_inode);
        ret = inode_path(loc.parent, bname, (char **)&(loc.path));
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_PATH_FAILED,
                   "Inode path failed"
                   " on %s, base file gfid = %s",
                   bname, uuid_utoa(gfid));
            local->op_ret = -1;
            local->op_errno = ENOMEM;
            loc_wipe(&loc);
            wind_failed = _gf_true;
            shard_unlink_shards_do_cbk(frame, (void *)(long)cur_block, this, -1,
                                       ENOMEM, NULL, NULL, NULL);
            goto next;
        }

        loc.name = strrchr(loc.path, '/');
        if (loc.name)
            loc.name++;
        loc.inode = inode_ref(local->inode_list[cur_block_idx]);

        STACK_WIND_COOKIE(frame, shard_unlink_shards_do_cbk,
                          (void *)(long)cur_block, FIRST_CHILD(this),
                          FIRST_CHILD(this)->fops->unlink, &loc, local->xflag,
                          local->xattr_req);
        loc_wipe(&loc);
    next:
        cur_block++;
        cur_block_idx++;
    }
    syncbarrier_wait(&local->barrier, count);
    SHARD_UNSET_ROOT_FS_ID(frame, local);
    return 0;
}

int
shard_regulated_shards_deletion(call_frame_t *cleanup_frame, xlator_t *this,
                                int now, int first_block, gf_dirent_t *entry)
{
    int i = 0;
    int ret = 0;
    shard_local_t *local = NULL;
    uuid_t gfid = {
        0,
    };

    local = cleanup_frame->local;

    local->inode_list = GF_CALLOC(now, sizeof(inode_t *),
                                  gf_shard_mt_inode_list);
    if (!local->inode_list)
        return -ENOMEM;

    local->first_block = first_block;
    local->last_block = first_block + now - 1;
    local->num_blocks = now;
    gf_uuid_parse(entry->d_name, gfid);
    gf_uuid_copy(local->base_gfid, gfid);
    local->resolver_base_inode = inode_find(this->itable, gfid);
    local->call_count = 0;
    ret = syncbarrier_init(&local->barrier);
    if (ret) {
        GF_FREE(local->inode_list);
        local->inode_list = NULL;
        inode_unref(local->resolver_base_inode);
        local->resolver_base_inode = NULL;
        return -errno;
    }
    shard_common_resolve_shards(cleanup_frame, this,
                                shard_post_resolve_unlink_handler);

    for (i = 0; i < local->num_blocks; i++) {
        if (local->inode_list[i])
            inode_unref(local->inode_list[i]);
    }
    GF_FREE(local->inode_list);
    local->inode_list = NULL;
    if (local->op_ret)
        ret = -local->op_errno;
    syncbarrier_destroy(&local->barrier);
    inode_unref(local->resolver_base_inode);
    local->resolver_base_inode = NULL;
    STACK_RESET(cleanup_frame->root);
    return ret;
}

int
__shard_delete_shards_of_entry(call_frame_t *cleanup_frame, xlator_t *this,
                               gf_dirent_t *entry, inode_t *inode)
{
    int ret = 0;
    int shard_count = 0;
    int first_block = 0;
    int now = 0;
    uint64_t size = 0;
    uint64_t block_size = 0;
    uint64_t size_array[4] = {
        0,
    };
    void *bsize = NULL;
    void *size_attr = NULL;
    dict_t *xattr_rsp = NULL;
    loc_t loc = {
        0,
    };
    shard_local_t *local = NULL;
    shard_priv_t *priv = NULL;

    priv = this->private;
    local = cleanup_frame->local;
    ret = dict_reset(local->xattr_req);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, 0, SHARD_MSG_DICT_OP_FAILED,
               "Failed to reset dict");
        ret = -ENOMEM;
        goto err;
    }

    ret = dict_set_uint64(local->xattr_req, GF_XATTR_SHARD_BLOCK_SIZE, 0);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, 0, SHARD_MSG_DICT_OP_FAILED,
               "Failed to set dict value: key:%s", GF_XATTR_SHARD_BLOCK_SIZE);
        ret = -ENOMEM;
        goto err;
    }

    ret = dict_set_uint64(local->xattr_req, GF_XATTR_SHARD_FILE_SIZE, 8 * 4);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, 0, SHARD_MSG_DICT_OP_FAILED,
               "Failed to set dict value: key:%s", GF_XATTR_SHARD_FILE_SIZE);
        ret = -ENOMEM;
        goto err;
    }

    loc.inode = inode_ref(inode);
    loc.parent = inode_ref(priv->dot_shard_rm_inode);
    ret = inode_path(loc.parent, entry->d_name, (char **)&(loc.path));
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_PATH_FAILED,
               "Inode path  failed on %s", entry->d_name);
        ret = -ENOMEM;
        goto err;
    }

    loc.name = strrchr(loc.path, '/');
    if (loc.name)
        loc.name++;
    ret = syncop_lookup(FIRST_CHILD(this), &loc, NULL, NULL, local->xattr_req,
                        &xattr_rsp);
    if (ret)
        goto err;

    ret = dict_get_ptr(xattr_rsp, GF_XATTR_SHARD_BLOCK_SIZE, &bsize);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_DICT_OP_FAILED,
               "Failed to get dict value: key:%s", GF_XATTR_SHARD_BLOCK_SIZE);
        goto err;
    }
    block_size = ntoh64(*((uint64_t *)bsize));

    ret = dict_get_ptr(xattr_rsp, GF_XATTR_SHARD_FILE_SIZE, &size_attr);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_DICT_OP_FAILED,
               "Failed to get dict value: key:%s", GF_XATTR_SHARD_FILE_SIZE);
        goto err;
    }

    memcpy(size_array, size_attr, sizeof(size_array));
    size = ntoh64(size_array[0]);

    shard_count = (size / block_size) - 1;
    if (shard_count < 0) {
        gf_msg_debug(this->name, 0,
                     "Size of %s hasn't grown beyond "
                     "its shard-block-size. Nothing to delete. "
                     "Returning",
                     entry->d_name);
        /* File size < shard-block-size, so nothing to delete */
        ret = 0;
        goto delete_marker;
    }
    if ((size % block_size) > 0)
        shard_count++;

    if (shard_count == 0) {
        gf_msg_debug(this->name, 0,
                     "Size of %s is exactly equal to "
                     "its shard-block-size. Nothing to delete. "
                     "Returning",
                     entry->d_name);
        ret = 0;
        goto delete_marker;
    }
    gf_msg_debug(this->name, 0,
                 "base file = %s, "
                 "shard-block-size=%" PRIu64 ", file-size=%" PRIu64
                 ", "
                 "shard_count=%d",
                 entry->d_name, block_size, size, shard_count);

    /* Perform a gfid-based lookup to see if gfid corresponding to marker
     * file's base name exists.
     */
    loc_wipe(&loc);
    loc.inode = inode_new(this->itable);
    if (!loc.inode) {
        ret = -ENOMEM;
        goto err;
    }
    gf_uuid_parse(entry->d_name, loc.gfid);
    ret = syncop_lookup(FIRST_CHILD(this), &loc, NULL, NULL, NULL, NULL);
    if (!ret) {
        gf_msg_debug(this->name, 0,
                     "Base shard corresponding to gfid "
                     "%s is present. Skipping shard deletion. "
                     "Returning",
                     entry->d_name);
        ret = 0;
        goto delete_marker;
    }

    first_block = 1;

    while (shard_count) {
        if (shard_count < local->deletion_rate) {
            now = shard_count;
            shard_count = 0;
        } else {
            now = local->deletion_rate;
            shard_count -= local->deletion_rate;
        }

        gf_msg_debug(this->name, 0,
                     "deleting %d shards starting from "
                     "block %d of gfid %s",
                     now, first_block, entry->d_name);
        ret = shard_regulated_shards_deletion(cleanup_frame, this, now,
                                              first_block, entry);
        if (ret)
            goto err;
        first_block += now;
    }

delete_marker:
    loc_wipe(&loc);
    loc.inode = inode_ref(inode);
    loc.parent = inode_ref(priv->dot_shard_rm_inode);
    ret = inode_path(loc.parent, entry->d_name, (char **)&(loc.path));
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_PATH_FAILED,
               "Inode path  failed on %s", entry->d_name);
        ret = -ENOMEM;
        goto err;
    }
    loc.name = strrchr(loc.path, '/');
    if (loc.name)
        loc.name++;
    ret = syncop_unlink(FIRST_CHILD(this), &loc, NULL, NULL);
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_SHARDS_DELETION_FAILED,
               "Failed to delete %s "
               "from /%s",
               entry->d_name, GF_SHARD_REMOVE_ME_DIR);
err:
    if (xattr_rsp)
        dict_unref(xattr_rsp);
    loc_wipe(&loc);
    return ret;
}

int
shard_delete_shards_of_entry(call_frame_t *cleanup_frame, xlator_t *this,
                             gf_dirent_t *entry, inode_t *inode)
{
    int ret = -1;
    loc_t loc = {
        0,
    };
    shard_priv_t *priv = NULL;

    priv = this->private;
    loc.inode = inode_ref(priv->dot_shard_rm_inode);

    ret = syncop_entrylk(FIRST_CHILD(this), this->name, &loc, entry->d_name,
                         ENTRYLK_LOCK_NB, ENTRYLK_WRLCK, NULL, NULL);
    if (ret < 0) {
        if (ret == -EAGAIN) {
            ret = 0;
        }
        goto out;
    }
    {
        ret = __shard_delete_shards_of_entry(cleanup_frame, this, entry, inode);
    }
    syncop_entrylk(FIRST_CHILD(this), this->name, &loc, entry->d_name,
                   ENTRYLK_UNLOCK, ENTRYLK_WRLCK, NULL, NULL);
out:
    loc_wipe(&loc);
    return ret;
}

int
shard_delete_shards_cbk(int ret, call_frame_t *frame, void *data)
{
    SHARD_STACK_DESTROY(frame);
    return 0;
}

int
shard_resolve_internal_dir(xlator_t *this, shard_local_t *local,
                           shard_internal_dir_type_t type)
{
    int ret = 0;
    char *bname = NULL;
    loc_t *loc = NULL;
    shard_priv_t *priv = NULL;
    uuid_t gfid = {
        0,
    };
    struct iatt stbuf = {
        0,
    };

    priv = this->private;

    switch (type) {
        case SHARD_INTERNAL_DIR_DOT_SHARD:
            loc = &local->dot_shard_loc;
            gf_uuid_copy(gfid, priv->dot_shard_gfid);
            bname = GF_SHARD_DIR;
            break;
        case SHARD_INTERNAL_DIR_DOT_SHARD_REMOVE_ME:
            loc = &local->dot_shard_rm_loc;
            gf_uuid_copy(gfid, priv->dot_shard_rm_gfid);
            bname = GF_SHARD_REMOVE_ME_DIR;
            break;
        default:
            break;
    }

    loc->inode = inode_find(this->itable, gfid);
    if (!loc->inode) {
        ret = shard_init_internal_dir_loc(this, local, type);
        if (ret)
            goto err;
        ret = dict_reset(local->xattr_req);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, 0, SHARD_MSG_DICT_OP_FAILED,
                   "Failed to reset "
                   "dict");
            ret = -ENOMEM;
            goto err;
        }
        ret = dict_set_gfuuid(local->xattr_req, "gfid-req", gfid, true);
        ret = syncop_lookup(FIRST_CHILD(this), loc, &stbuf, NULL,
                            local->xattr_req, NULL);
        if (ret < 0) {
            if (ret != -ENOENT)
                gf_msg(this->name, GF_LOG_ERROR, -ret,
                       SHARD_MSG_SHARDS_DELETION_FAILED,
                       "Lookup on %s failed, exiting", bname);
            goto err;
        } else {
            shard_link_internal_dir_inode(local, loc->inode, &stbuf, type);
        }
    }
    ret = 0;
err:
    return ret;
}

int
shard_lookup_marker_entry(xlator_t *this, shard_local_t *local,
                          gf_dirent_t *entry)
{
    int ret = 0;
    loc_t loc = {
        0,
    };

    loc.inode = inode_new(this->itable);
    if (!loc.inode) {
        ret = -ENOMEM;
        goto err;
    }
    loc.parent = inode_ref(local->fd->inode);

    ret = inode_path(loc.parent, entry->d_name, (char **)&(loc.path));
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_PATH_FAILED,
               "Inode path failed on %s", entry->d_name);
        ret = -ENOMEM;
        goto err;
    }

    loc.name = strrchr(loc.path, '/');
    if (loc.name)
        loc.name++;

    ret = syncop_lookup(FIRST_CHILD(this), &loc, NULL, NULL, NULL, NULL);
    if (ret < 0) {
        goto err;
    }
    entry->inode = inode_ref(loc.inode);
    ret = 0;
err:
    loc_wipe(&loc);
    return ret;
}

int
shard_delete_shards(void *opaque)
{
    int ret = 0;
    off_t offset = 0;
    loc_t loc = {
        0,
    };
    inode_t *link_inode = NULL;
    xlator_t *this = NULL;
    shard_priv_t *priv = NULL;
    shard_local_t *local = NULL;
    gf_dirent_t entries;
    gf_dirent_t *entry = NULL;
    call_frame_t *cleanup_frame = NULL;
    gf_boolean_t done = _gf_false;

    this = THIS;
    priv = this->private;
    INIT_LIST_HEAD(&entries.list);

    cleanup_frame = opaque;

    local = mem_get0(this->local_pool);
    if (!local) {
        gf_msg(this->name, GF_LOG_WARNING, ENOMEM, SHARD_MSG_MEMALLOC_FAILED,
               "Failed to create local to "
               "delete shards");
        ret = -ENOMEM;
        goto err;
    }
    cleanup_frame->local = local;
    local->fop = GF_FOP_UNLINK;

    local->xattr_req = dict_new();
    if (!local->xattr_req) {
        ret = -ENOMEM;
        goto err;
    }
    local->deletion_rate = priv->deletion_rate;

    ret = shard_resolve_internal_dir(this, local, SHARD_INTERNAL_DIR_DOT_SHARD);
    if (ret == -ENOENT) {
        gf_msg_debug(this->name, 0,
                     ".shard absent. Nothing to"
                     " delete. Exiting");
        ret = 0;
        goto err;
    } else if (ret < 0) {
        goto err;
    }

    ret = shard_resolve_internal_dir(this, local,
                                     SHARD_INTERNAL_DIR_DOT_SHARD_REMOVE_ME);
    if (ret == -ENOENT) {
        gf_msg_debug(this->name, 0,
                     ".remove_me absent. "
                     "Nothing to delete. Exiting");
        ret = 0;
        goto err;
    } else if (ret < 0) {
        goto err;
    }

    local->fd = fd_anonymous(local->dot_shard_rm_loc.inode);
    if (!local->fd) {
        ret = -ENOMEM;
        goto err;
    }

    for (;;) {
        offset = 0;
        LOCK(&priv->lock);
        {
            if (priv->bg_del_state == SHARD_BG_DELETION_LAUNCHING) {
                priv->bg_del_state = SHARD_BG_DELETION_IN_PROGRESS;
            } else if (priv->bg_del_state == SHARD_BG_DELETION_IN_PROGRESS) {
                priv->bg_del_state = SHARD_BG_DELETION_NONE;
                done = _gf_true;
            }
        }
        UNLOCK(&priv->lock);
        if (done)
            break;
        while (
            (ret = syncop_readdirp(FIRST_CHILD(this), local->fd, 131072, offset,
                                   &entries, local->xattr_req, NULL))) {
            if (ret > 0)
                ret = 0;
            list_for_each_entry(entry, &entries.list, list)
            {
                offset = entry->d_off;

                if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                    continue;

                if (!entry->inode) {
                    ret = shard_lookup_marker_entry(this, local, entry);
                    if (ret < 0)
                        continue;
                }
                link_inode = inode_link(entry->inode, local->fd->inode,
                                        entry->d_name, &entry->d_stat);

                gf_msg_debug(this->name, 0,
                             "Initiating deletion of "
                             "shards of gfid %s",
                             entry->d_name);
                ret = shard_delete_shards_of_entry(cleanup_frame, this, entry,
                                                   link_inode);
                inode_unlink(link_inode, local->fd->inode, entry->d_name);
                inode_unref(link_inode);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, -ret,
                           SHARD_MSG_SHARDS_DELETION_FAILED,
                           "Failed to clean up shards of gfid %s",
                           entry->d_name);
                    continue;
                }
                gf_msg(this->name, GF_LOG_INFO, 0,
                       SHARD_MSG_SHARD_DELETION_COMPLETED,
                       "Deleted "
                       "shards of gfid=%s from backend",
                       entry->d_name);
            }
            gf_dirent_free(&entries);
            if (ret)
                break;
        }
    }
    ret = 0;
    loc_wipe(&loc);
    return ret;

err:
    LOCK(&priv->lock);
    {
        priv->bg_del_state = SHARD_BG_DELETION_NONE;
    }
    UNLOCK(&priv->lock);
    loc_wipe(&loc);
    return ret;
}

int
shard_unlock_inodelk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    if (op_ret)
        gf_msg(this->name, GF_LOG_ERROR, op_errno, SHARD_MSG_FOP_FAILED,
               "Unlock failed. Please check brick logs for "
               "more details");
    SHARD_STACK_DESTROY(frame);
    return 0;
}

int
shard_unlock_inodelk(call_frame_t *frame, xlator_t *this)
{
    loc_t *loc = NULL;
    call_frame_t *lk_frame = NULL;
    shard_local_t *local = NULL;
    shard_local_t *lk_local = NULL;
    shard_inodelk_t *lock = NULL;

    local = frame->local;
    lk_frame = local->inodelk_frame;
    lk_local = lk_frame->local;
    local->inodelk_frame = NULL;
    loc = &local->int_inodelk.loc;
    lock = &lk_local->int_inodelk;
    lock->flock.l_type = F_UNLCK;

    STACK_WIND(lk_frame, shard_unlock_inodelk_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->inodelk, lock->domain, loc, F_SETLK,
               &lock->flock, NULL);
    local->int_inodelk.acquired_lock = _gf_false;
    return 0;
}

int
shard_rename_src_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *buf,
                     struct iatt *preoldparent, struct iatt *postoldparent,
                     struct iatt *prenewparent, struct iatt *postnewparent,
                     dict_t *xdata);
int
shard_rename_src_base_file(call_frame_t *frame, xlator_t *this)
{
    int ret = 0;
    loc_t *dst_loc = NULL;
    loc_t tmp_loc = {
        0,
    };
    shard_local_t *local = frame->local;

    if (local->dst_block_size) {
        tmp_loc.parent = inode_ref(local->loc2.parent);
        ret = inode_path(tmp_loc.parent, local->loc2.name,
                         (char **)&tmp_loc.path);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_PATH_FAILED,
                   "Inode path failed"
                   " on pargfid=%s bname=%s",
                   uuid_utoa(tmp_loc.parent->gfid), local->loc2.name);
            local->op_ret = -1;
            local->op_errno = ENOMEM;
            goto err;
        }

        tmp_loc.name = strrchr(tmp_loc.path, '/');
        if (tmp_loc.name)
            tmp_loc.name++;
        dst_loc = &tmp_loc;
    } else {
        dst_loc = &local->loc2;
    }

    /* To-Do: Request open-fd count on dst base file */
    STACK_WIND(frame, shard_rename_src_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->rename, &local->loc, dst_loc,
               local->xattr_req);
    loc_wipe(&tmp_loc);
    return 0;
err:
    loc_wipe(&tmp_loc);
    shard_common_failure_unwind(local->fop, frame, local->op_ret,
                                local->op_errno);
    return 0;
}

int
shard_unlink_base_file(call_frame_t *frame, xlator_t *this);

int
shard_set_size_attrs_on_marker_file_cbk(call_frame_t *frame, void *cookie,
                                        xlator_t *this, int32_t op_ret,
                                        int32_t op_errno, dict_t *dict,
                                        dict_t *xdata)
{
    shard_priv_t *priv = NULL;
    shard_local_t *local = NULL;

    priv = this->private;
    local = frame->local;
    if (op_ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, op_errno, SHARD_MSG_FOP_FAILED,
               "Xattrop on marker file failed "
               "while performing %s; entry gfid=%s",
               gf_fop_string(local->fop), local->newloc.name);
        goto err;
    }

    inode_unlink(local->newloc.inode, priv->dot_shard_rm_inode,
                 local->newloc.name);

    if (local->fop == GF_FOP_UNLINK)
        shard_unlink_base_file(frame, this);
    else if (local->fop == GF_FOP_RENAME)
        shard_rename_src_base_file(frame, this);
    return 0;
err:
    shard_common_failure_unwind(local->fop, frame, op_ret, op_errno);
    return 0;
}

int
shard_set_size_attrs_on_marker_file(call_frame_t *frame, xlator_t *this)
{
    int op_errno = ENOMEM;
    uint64_t bs = 0;
    dict_t *xdata = NULL;
    shard_local_t *local = NULL;

    local = frame->local;
    xdata = dict_new();
    if (!xdata)
        goto err;

    if (local->fop == GF_FOP_UNLINK)
        bs = local->block_size;
    else if (local->fop == GF_FOP_RENAME)
        bs = local->dst_block_size;
    SHARD_INODE_CREATE_INIT(this, bs, xdata, &local->newloc,
                            local->prebuf.ia_size, 0, err);
    STACK_WIND(frame, shard_set_size_attrs_on_marker_file_cbk,
               FIRST_CHILD(this), FIRST_CHILD(this)->fops->xattrop,
               &local->newloc, GF_XATTROP_GET_AND_SET, xdata, NULL);
    dict_unref(xdata);
    return 0;
err:
    if (xdata)
        dict_unref(xdata);
    shard_common_failure_unwind(local->fop, frame, -1, op_errno);
    return 0;
}

int
shard_lookup_marker_file_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno, inode_t *inode,
                             struct iatt *buf, dict_t *xdata,
                             struct iatt *postparent)
{
    inode_t *linked_inode = NULL;
    shard_priv_t *priv = NULL;
    shard_local_t *local = NULL;

    local = frame->local;
    priv = this->private;

    if (op_ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, op_errno, SHARD_MSG_FOP_FAILED,
               "Lookup on marker file failed "
               "while performing %s; entry gfid=%s",
               gf_fop_string(local->fop), local->newloc.name);
        goto err;
    }

    linked_inode = inode_link(inode, priv->dot_shard_rm_inode,
                              local->newloc.name, buf);
    inode_unref(local->newloc.inode);
    local->newloc.inode = linked_inode;
    shard_set_size_attrs_on_marker_file(frame, this);
    return 0;
err:
    shard_common_failure_unwind(local->fop, frame, op_ret, op_errno);
    return 0;
}

int
shard_lookup_marker_file(call_frame_t *frame, xlator_t *this)
{
    int op_errno = ENOMEM;
    dict_t *xattr_req = NULL;
    shard_local_t *local = NULL;

    local = frame->local;

    xattr_req = shard_create_gfid_dict(local->xattr_req);
    if (!xattr_req)
        goto err;

    STACK_WIND(frame, shard_lookup_marker_file_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->lookup, &local->newloc, xattr_req);
    dict_unref(xattr_req);
    return 0;
err:
    shard_common_failure_unwind(local->fop, frame, -1, op_errno);
    return 0;
}

int
shard_create_marker_file_under_remove_me_cbk(
    call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
    int32_t op_errno, inode_t *inode, struct iatt *buf, struct iatt *preparent,
    struct iatt *postparent, dict_t *xdata)
{
    inode_t *linked_inode = NULL;
    shard_priv_t *priv = NULL;
    shard_local_t *local = NULL;

    local = frame->local;
    priv = this->private;

    SHARD_UNSET_ROOT_FS_ID(frame, local);
    if (op_ret < 0) {
        if ((op_errno != EEXIST) && (op_errno != ENODATA)) {
            local->op_ret = op_ret;
            local->op_errno = op_errno;
            gf_msg(this->name, GF_LOG_ERROR, op_errno, SHARD_MSG_FOP_FAILED,
                   "Marker file creation "
                   "failed while performing %s; entry gfid=%s",
                   gf_fop_string(local->fop), local->newloc.name);
            goto err;
        } else {
            shard_lookup_marker_file(frame, this);
            return 0;
        }
    }

    linked_inode = inode_link(inode, priv->dot_shard_rm_inode,
                              local->newloc.name, buf);
    inode_unref(local->newloc.inode);
    local->newloc.inode = linked_inode;

    if (local->fop == GF_FOP_UNLINK)
        shard_unlink_base_file(frame, this);
    else if (local->fop == GF_FOP_RENAME)
        shard_rename_src_base_file(frame, this);
    return 0;
err:
    shard_common_failure_unwind(local->fop, frame, -1, local->op_errno);
    return 0;
}

int
shard_create_marker_file_under_remove_me(call_frame_t *frame, xlator_t *this,
                                         loc_t *loc)
{
    int ret = 0;
    int op_errno = ENOMEM;
    uint64_t bs = 0;
    char g1[64] = {
        0,
    };
    char g2[64] = {
        0,
    };
    dict_t *xattr_req = NULL;
    shard_priv_t *priv = NULL;
    shard_local_t *local = NULL;

    priv = this->private;
    local = frame->local;

    SHARD_SET_ROOT_FS_ID(frame, local);

    xattr_req = shard_create_gfid_dict(local->xattr_req);
    if (!xattr_req)
        goto err;

    local->newloc.inode = inode_new(this->itable);
    local->newloc.parent = inode_ref(priv->dot_shard_rm_inode);
    ret = inode_path(local->newloc.parent, uuid_utoa(loc->inode->gfid),
                     (char **)&local->newloc.path);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_PATH_FAILED,
               "Inode path failed on "
               "pargfid=%s bname=%s",
               uuid_utoa_r(priv->dot_shard_rm_gfid, g1),
               uuid_utoa_r(loc->inode->gfid, g2));
        goto err;
    }
    local->newloc.name = strrchr(local->newloc.path, '/');
    if (local->newloc.name)
        local->newloc.name++;

    if (local->fop == GF_FOP_UNLINK)
        bs = local->block_size;
    else if (local->fop == GF_FOP_RENAME)
        bs = local->dst_block_size;

    SHARD_INODE_CREATE_INIT(this, bs, xattr_req, &local->newloc,
                            local->prebuf.ia_size, 0, err);

    STACK_WIND(frame, shard_create_marker_file_under_remove_me_cbk,
               FIRST_CHILD(this), FIRST_CHILD(this)->fops->mknod,
               &local->newloc, 0, 0, 0644, xattr_req);
    dict_unref(xattr_req);
    return 0;

err:
    if (xattr_req)
        dict_unref(xattr_req);
    shard_create_marker_file_under_remove_me_cbk(frame, 0, this, -1, op_errno,
                                                 NULL, NULL, NULL, NULL, NULL);
    return 0;
}

int
shard_unlock_entrylk(call_frame_t *frame, xlator_t *this);

int
shard_unlink_base_file_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno,
                           struct iatt *preparent, struct iatt *postparent,
                           dict_t *xdata)
{
    int ret = 0;
    shard_local_t *local = NULL;

    local = frame->local;

    if (op_ret < 0) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;
    } else {
        shard_inode_ctx_set_refresh_flag(local->int_inodelk.loc.inode, this);
        local->preoldparent = *preparent;
        local->postoldparent = *postparent;
        if (xdata)
            local->xattr_rsp = dict_ref(xdata);
        if (local->cleanup_required)
            shard_start_background_deletion(this);
    }

    if (local->entrylk_frame) {
        ret = shard_unlock_entrylk(frame, this);
        if (ret < 0) {
            local->op_ret = -1;
            local->op_errno = -ret;
        }
    }

    ret = shard_unlock_inodelk(frame, this);
    if (ret < 0) {
        local->op_ret = -1;
        local->op_errno = -ret;
    }

    shard_unlink_cbk(frame, this);
    return 0;
}

int
shard_unlink_base_file(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = frame->local;

    /* To-Do: Request open-fd count on base file */
    STACK_WIND(frame, shard_unlink_base_file_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->unlink, &local->loc, local->xflag,
               local->xattr_req);
    return 0;
}

int
shard_unlock_entrylk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    if (op_ret)
        gf_msg(this->name, GF_LOG_ERROR, op_errno, SHARD_MSG_FOP_FAILED,
               "Unlock failed. Please check brick logs for "
               "more details");
    SHARD_STACK_DESTROY(frame);
    return 0;
}

int
shard_unlock_entrylk(call_frame_t *frame, xlator_t *this)
{
    loc_t *loc = NULL;
    call_frame_t *lk_frame = NULL;
    shard_local_t *local = NULL;
    shard_local_t *lk_local = NULL;
    shard_entrylk_t *lock = NULL;

    local = frame->local;
    lk_frame = local->entrylk_frame;
    lk_local = lk_frame->local;
    local->entrylk_frame = NULL;
    lock = &lk_local->int_entrylk;
    loc = &lock->loc;

    STACK_WIND(lk_frame, shard_unlock_entrylk_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->entrylk, this->name, loc,
               lk_local->int_entrylk.basename, ENTRYLK_UNLOCK, ENTRYLK_WRLCK,
               NULL);
    local->int_entrylk.acquired_lock = _gf_false;
    return 0;
}

int
shard_post_entrylk_fop_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    switch (local->fop) {
        case GF_FOP_UNLINK:
        case GF_FOP_RENAME:
            shard_create_marker_file_under_remove_me(frame, this,
                                                     &local->int_inodelk.loc);
            break;
        default:
            gf_msg(this->name, GF_LOG_WARNING, 0, SHARD_MSG_INVALID_FOP,
                   "post-entrylk handler not defined. This case should not"
                   " be hit");
            break;
    }
    return 0;
}

int
shard_acquire_entrylk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    call_frame_t *main_frame = NULL;
    shard_local_t *local = NULL;
    shard_local_t *main_local = NULL;

    local = frame->local;
    main_frame = local->main_frame;
    main_local = main_frame->local;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(main_local->fop, main_frame, op_ret,
                                    op_errno);
        return 0;
    }
    main_local->int_entrylk.acquired_lock = _gf_true;
    shard_post_entrylk_fop_handler(main_frame, this);
    return 0;
}

int
shard_acquire_entrylk(call_frame_t *frame, xlator_t *this, inode_t *inode,
                      uuid_t gfid)
{
    char gfid_str[GF_UUID_BUF_SIZE] = {
        0,
    };
    shard_local_t *local = NULL;
    shard_local_t *entrylk_local = NULL;
    shard_entrylk_t *int_entrylk = NULL;
    call_frame_t *entrylk_frame = NULL;

    local = frame->local;
    entrylk_frame = create_frame(this, this->ctx->pool);
    if (!entrylk_frame) {
        gf_msg(this->name, GF_LOG_WARNING, ENOMEM, SHARD_MSG_MEMALLOC_FAILED,
               "Failed to create new frame "
               "to lock marker file");
        goto err;
    }

    entrylk_local = mem_get0(this->local_pool);
    if (!entrylk_local) {
        STACK_DESTROY(entrylk_frame->root);
        goto err;
    }

    entrylk_frame->local = entrylk_local;
    entrylk_local->main_frame = frame;
    int_entrylk = &entrylk_local->int_entrylk;

    int_entrylk->loc.inode = inode_ref(inode);
    set_lk_owner_from_ptr(&entrylk_frame->root->lk_owner, entrylk_frame->root);
    local->entrylk_frame = entrylk_frame;
    gf_uuid_unparse(gfid, gfid_str);
    int_entrylk->basename = gf_strdup(gfid_str);

    STACK_WIND(entrylk_frame, shard_acquire_entrylk_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->entrylk, this->name, &int_entrylk->loc,
               int_entrylk->basename, ENTRYLK_LOCK, ENTRYLK_WRLCK, NULL);
    return 0;
err:
    shard_common_failure_unwind(local->fop, frame, -1, ENOMEM);
    return 0;
}

int
shard_post_lookup_base_shard_rm_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;
    shard_priv_t *priv = NULL;

    priv = this->private;
    local = frame->local;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(local->fop, frame, -1, local->op_errno);
        return 0;
    }

    if (local->prebuf.ia_nlink > 1) {
        gf_msg_debug(this->name, 0,
                     "link count on %s > 1:%d, "
                     "performing rename()/unlink()",
                     local->int_inodelk.loc.path, local->prebuf.ia_nlink);
        if (local->fop == GF_FOP_RENAME)
            shard_rename_src_base_file(frame, this);
        else if (local->fop == GF_FOP_UNLINK)
            shard_unlink_base_file(frame, this);
    } else {
        gf_msg_debug(this->name, 0,
                     "link count on %s = 1, creating "
                     "file under .remove_me",
                     local->int_inodelk.loc.path);
        local->cleanup_required = _gf_true;
        shard_acquire_entrylk(frame, this, priv->dot_shard_rm_inode,
                              local->prebuf.ia_gfid);
    }
    return 0;
}

int
shard_post_inodelk_fop_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    switch (local->fop) {
        case GF_FOP_UNLINK:
        case GF_FOP_RENAME:
            shard_refresh_base_file(frame, this, &local->int_inodelk.loc, NULL,
                                    shard_post_lookup_base_shard_rm_handler);
            break;
        default:
            gf_msg(this->name, GF_LOG_WARNING, 0, SHARD_MSG_INVALID_FOP,
                   "post-inodelk handler not defined. This case should not"
                   " be hit");
            break;
    }
    return 0;
}

int
shard_acquire_inodelk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    call_frame_t *main_frame = NULL;
    shard_local_t *local = NULL;
    shard_local_t *main_local = NULL;

    local = frame->local;
    main_frame = local->main_frame;
    main_local = main_frame->local;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(main_local->fop, main_frame, op_ret,
                                    op_errno);
        return 0;
    }
    main_local->int_inodelk.acquired_lock = _gf_true;
    shard_post_inodelk_fop_handler(main_frame, this);
    return 0;
}

int
shard_acquire_inodelk(call_frame_t *frame, xlator_t *this, loc_t *loc)
{
    call_frame_t *lk_frame = NULL;
    shard_local_t *local = NULL;
    shard_local_t *lk_local = NULL;
    shard_inodelk_t *int_inodelk = NULL;

    local = frame->local;
    lk_frame = create_frame(this, this->ctx->pool);
    if (!lk_frame) {
        gf_msg(this->name, GF_LOG_WARNING, ENOMEM, SHARD_MSG_MEMALLOC_FAILED,
               "Failed to create new frame "
               "to lock base shard");
        goto err;
    }
    lk_local = mem_get0(this->local_pool);
    if (!lk_local) {
        STACK_DESTROY(lk_frame->root);
        goto err;
    }

    lk_frame->local = lk_local;
    lk_local->main_frame = frame;
    int_inodelk = &lk_local->int_inodelk;

    int_inodelk->flock.l_len = 0;
    int_inodelk->flock.l_start = 0;
    int_inodelk->domain = this->name;
    int_inodelk->flock.l_type = F_WRLCK;
    loc_copy(&local->int_inodelk.loc, loc);
    set_lk_owner_from_ptr(&lk_frame->root->lk_owner, lk_frame->root);
    local->inodelk_frame = lk_frame;

    STACK_WIND(lk_frame, shard_acquire_inodelk_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->inodelk, int_inodelk->domain,
               &local->int_inodelk.loc, F_SETLKW, &int_inodelk->flock, NULL);
    return 0;
err:
    shard_common_failure_unwind(local->fop, frame, -1, ENOMEM);
    return 0;
}

int
shard_post_mkdir_rm_handler(call_frame_t *frame, xlator_t *this)
{
    loc_t *loc = NULL;
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(local->fop, frame, -1, local->op_errno);
        return 0;
    }
    if (local->fop == GF_FOP_UNLINK)
        loc = &local->loc;
    else if (local->fop == GF_FOP_RENAME)
        loc = &local->loc2;
    shard_acquire_inodelk(frame, this, loc);
    return 0;
}

int
shard_mkdir_internal_dir(call_frame_t *frame, xlator_t *this,
                         shard_post_resolve_fop_handler_t handler,
                         shard_internal_dir_type_t type);
int
shard_pre_mkdir_rm_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(local->fop, frame, -1, local->op_errno);
        return 0;
    }
    shard_mkdir_internal_dir(frame, this, shard_post_mkdir_rm_handler,
                             SHARD_INTERNAL_DIR_DOT_SHARD_REMOVE_ME);
    return 0;
}

void
shard_begin_rm_resolution(call_frame_t *frame, xlator_t *this)
{
    shard_priv_t *priv = NULL;
    shard_local_t *local = NULL;

    priv = this->private;
    local = frame->local;

    local->dot_shard_rm_loc.inode = inode_find(this->itable,
                                               priv->dot_shard_rm_gfid);
    if (!local->dot_shard_rm_loc.inode) {
        local->dot_shard_loc.inode = inode_find(this->itable,
                                                priv->dot_shard_gfid);
        if (!local->dot_shard_loc.inode) {
            shard_mkdir_internal_dir(frame, this, shard_pre_mkdir_rm_handler,
                                     SHARD_INTERNAL_DIR_DOT_SHARD);
        } else {
            local->post_res_handler = shard_pre_mkdir_rm_handler;
            shard_refresh_internal_dir(frame, this,
                                       SHARD_INTERNAL_DIR_DOT_SHARD);
        }
    } else {
        local->post_res_handler = shard_post_mkdir_rm_handler;
        shard_refresh_internal_dir(frame, this,
                                   SHARD_INTERNAL_DIR_DOT_SHARD_REMOVE_ME);
    }
}

int
shard_unlink(call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
             dict_t *xdata)
{
    int ret = -1;
    uint64_t block_size = 0;
    shard_local_t *local = NULL;

    ret = shard_inode_ctx_get_block_size(loc->inode, this, &block_size);
    if ((ret) && (!IA_ISLNK(loc->inode->ia_type))) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_CTX_GET_FAILED,
               "Failed to get block "
               "size from inode ctx of %s",
               uuid_utoa(loc->inode->gfid));
        goto err;
    }

    if (!block_size || frame->root->pid == GF_CLIENT_PID_GSYNCD) {
        STACK_WIND(frame, default_unlink_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->unlink, loc, xflag, xdata);
        return 0;
    }

    local = mem_get0(this->local_pool);
    if (!local)
        goto err;

    frame->local = local;

    loc_copy(&local->loc, loc);
    local->xflag = xflag;
    local->xattr_req = (xdata) ? dict_ref(xdata) : dict_new();
    local->block_size = block_size;
    local->resolver_base_inode = loc->inode;
    local->fop = GF_FOP_UNLINK;
    if (!this->itable)
        this->itable = (local->loc.inode)->table;

    local->resolve_not = _gf_true;
    shard_begin_rm_resolution(frame, this);
    return 0;
err:
    shard_common_failure_unwind(GF_FOP_UNLINK, frame, -1, ENOMEM);
    return 0;
}

int
shard_post_rename_lookup_handler(call_frame_t *frame, xlator_t *this)
{
    shard_rename_cbk(frame, this);
    return 0;
}

int
shard_rename_src_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *buf,
                     struct iatt *preoldparent, struct iatt *postoldparent,
                     struct iatt *prenewparent, struct iatt *postnewparent,
                     dict_t *xdata)
{
    int ret = 0;
    shard_local_t *local = NULL;

    local = frame->local;

    if (op_ret < 0) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;
        goto err;
    }
    /* Set ctx->refresh to TRUE to force a lookup on disk when
     * shard_lookup_base_file() is called next to refresh the hard link
     * count in ctx. Note that this is applicable only to the case where
     * the rename dst is already existent and sharded.
     */
    if ((local->dst_block_size) && (!local->cleanup_required))
        shard_inode_ctx_set_refresh_flag(local->int_inodelk.loc.inode, this);

    local->prebuf = *buf;
    local->preoldparent = *preoldparent;
    local->postoldparent = *postoldparent;
    local->prenewparent = *prenewparent;
    local->postnewparent = *postnewparent;
    if (xdata)
        local->xattr_rsp = dict_ref(xdata);

    if (local->dst_block_size) {
        if (local->entrylk_frame) {
            ret = shard_unlock_entrylk(frame, this);
            if (ret < 0) {
                local->op_ret = -1;
                local->op_errno = -ret;
            }
        }

        ret = shard_unlock_inodelk(frame, this);
        if (ret < 0) {
            local->op_ret = -1;
            local->op_errno = -ret;
            goto err;
        }
        if (local->cleanup_required)
            shard_start_background_deletion(this);
    }

    /* Now the base file of src, if sharded, is looked up to gather ia_size
     * and ia_blocks.*/
    if (local->block_size) {
        local->tmp_loc.inode = inode_new(this->itable);
        gf_uuid_copy(local->tmp_loc.gfid, (local->loc.inode)->gfid);
        shard_refresh_base_file(frame, this, &local->tmp_loc, NULL,
                                shard_post_rename_lookup_handler);
    } else {
        shard_rename_cbk(frame, this);
    }
    return 0;
err:
    shard_common_failure_unwind(local->fop, frame, local->op_ret,
                                local->op_errno);
    return 0;
}

int
shard_post_lookup_dst_base_file_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(local->fop, frame, local->op_ret,
                                    local->op_errno);
        return 0;
    }

    /* Save dst base file attributes into postbuf so the information is not
     * lost when it is overwritten after lookup on base file of src in
     * shard_lookup_base_file_cbk().
     */
    local->postbuf = local->prebuf;
    shard_rename_src_base_file(frame, this);
    return 0;
}

int
shard_rename(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
             dict_t *xdata)
{
    int ret = -1;
    uint64_t block_size = 0;
    uint64_t dst_block_size = 0;
    shard_local_t *local = NULL;

    if (IA_ISDIR(oldloc->inode->ia_type)) {
        STACK_WIND(frame, default_rename_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->rename, oldloc, newloc, xdata);
        return 0;
    }

    ret = shard_inode_ctx_get_block_size(oldloc->inode, this, &block_size);
    if ((ret) && (!IA_ISLNK(oldloc->inode->ia_type))) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_CTX_GET_FAILED,
               "Failed to get block "
               "size from inode ctx of %s",
               uuid_utoa(oldloc->inode->gfid));
        goto err;
    }

    if (newloc->inode)
        ret = shard_inode_ctx_get_block_size(newloc->inode, this,
                                             &dst_block_size);

    /* The following stack_wind covers the case where:
     * a. the src file is not sharded and dst doesn't exist, OR
     * b. the src and dst both exist but are not sharded.
     */
    if (((!block_size) && (!dst_block_size)) ||
        frame->root->pid == GF_CLIENT_PID_GSYNCD) {
        STACK_WIND(frame, default_rename_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->rename, oldloc, newloc, xdata);
        return 0;
    }

    local = mem_get0(this->local_pool);
    if (!local)
        goto err;

    frame->local = local;
    loc_copy(&local->loc, oldloc);
    loc_copy(&local->loc2, newloc);
    local->resolver_base_inode = newloc->inode;
    local->fop = GF_FOP_RENAME;
    local->xattr_req = (xdata) ? dict_ref(xdata) : dict_new();
    if (!local->xattr_req)
        goto err;

    local->block_size = block_size;
    local->dst_block_size = dst_block_size;
    if (!this->itable)
        this->itable = (local->loc.inode)->table;
    local->resolve_not = _gf_true;

    /* The following if-block covers the case where the dst file exists
     * and is sharded.
     */
    if (local->dst_block_size) {
        shard_begin_rm_resolution(frame, this);
    } else {
        /* The following block covers the case where the dst either doesn't
         * exist or is NOT sharded but the src is sharded. In this case, shard
         * xlator would go ahead and rename src to dst. Once done, it would also
         * lookup the base shard of src to get the ia_size and ia_blocks xattr
         * values.
         */
        shard_rename_src_base_file(frame, this);
    }
    return 0;

err:
    shard_common_failure_unwind(GF_FOP_RENAME, frame, -1, ENOMEM);
    return 0;
}

int
shard_create_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                 struct iatt *stbuf, struct iatt *preparent,
                 struct iatt *postparent, dict_t *xdata)
{
    int ret = -1;
    shard_local_t *local = NULL;

    local = frame->local;

    if (op_ret == -1)
        goto unwind;

    ret = shard_inode_ctx_set(inode, this, stbuf, local->block_size,
                              SHARD_ALL_MASK);
    if (ret)
        gf_msg(this->name, GF_LOG_WARNING, 0, SHARD_MSG_INODE_CTX_SET_FAILED,
               "Failed to set inode "
               "ctx for %s",
               uuid_utoa(inode->gfid));

unwind:
    SHARD_STACK_UNWIND(create, frame, op_ret, op_errno, fd, inode, stbuf,
                       preparent, postparent, xdata);
    return 0;
}

int
shard_create(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
             mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
    shard_priv_t *priv = NULL;
    shard_local_t *local = NULL;

    priv = this->private;
    local = mem_get0(this->local_pool);
    if (!local)
        goto err;

    frame->local = local;
    local->block_size = priv->block_size;

    if (!__is_gsyncd_on_shard_dir(frame, loc)) {
        SHARD_INODE_CREATE_INIT(this, local->block_size, xdata, loc, 0, 0, err);
    }

    STACK_WIND(frame, shard_create_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->create, loc, flags, mode, umask, fd,
               xdata);
    return 0;
err:
    shard_common_failure_unwind(GF_FOP_CREATE, frame, -1, ENOMEM);
    return 0;
}

int
shard_open_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
    /* To-Do: Handle open with O_TRUNC under locks */
    SHARD_STACK_UNWIND(open, frame, op_ret, op_errno, fd, xdata);
    return 0;
}

int
shard_open(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           fd_t *fd, dict_t *xdata)
{
    STACK_WIND(frame, shard_open_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->open, loc, flags, fd, xdata);
    return 0;
}

int
shard_readv_do_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iovec *vector,
                   int32_t count, struct iatt *stbuf, struct iobref *iobref,
                   dict_t *xdata)
{
    int i = 0;
    int call_count = 0;
    void *address = NULL;
    uint64_t block_num = 0;
    off_t off = 0;
    struct iovec vec = {
        0,
    };
    shard_local_t *local = NULL;
    fd_t *anon_fd = cookie;
    shard_inode_ctx_t *ctx = NULL;

    local = frame->local;

    /* If shard has already seen a failure here before, there is no point
     * in aggregating subsequent reads, so just go to out.
     */
    if (local->op_ret < 0)
        goto out;

    if (op_ret < 0) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;
        goto out;
    }

    if (local->op_ret >= 0)
        local->op_ret += op_ret;

    shard_inode_ctx_get(anon_fd->inode, this, &ctx);
    block_num = ctx->block_num;

    if (block_num == local->first_block) {
        address = local->iobuf->ptr;
    } else {
        /* else
         * address to start writing to = beginning of buffer +
         *                    number of bytes until end of first block +
         *                    + block_size times number of blocks
         *                    between the current block and the first
         */
        address = (char *)local->iobuf->ptr +
                  (local->block_size - (local->offset % local->block_size)) +
                  ((block_num - local->first_block - 1) * local->block_size);
    }

    for (i = 0; i < count; i++) {
        address = (char *)address + off;
        memcpy(address, vector[i].iov_base, vector[i].iov_len);
        off += vector[i].iov_len;
    }

out:
    if (anon_fd)
        fd_unref(anon_fd);
    call_count = shard_call_count_return(frame);
    if (call_count == 0) {
        SHARD_UNSET_ROOT_FS_ID(frame, local);
        if (local->op_ret < 0) {
            shard_common_failure_unwind(GF_FOP_READ, frame, local->op_ret,
                                        local->op_errno);
        } else {
            if (xdata)
                local->xattr_rsp = dict_ref(xdata);
            vec.iov_base = local->iobuf->ptr;
            if (local->offset + local->req_size > local->prebuf.ia_size)
                local->total_size = local->prebuf.ia_size - local->offset;
            vec.iov_len = local->total_size;
            local->op_ret = local->total_size;
            SHARD_STACK_UNWIND(readv, frame, local->op_ret, local->op_errno,
                               &vec, 1, &local->prebuf, local->iobref,
                               local->xattr_rsp);
            return 0;
        }
    }

    return 0;
}

int
shard_readv_do(call_frame_t *frame, xlator_t *this)
{
    int i = 0;
    int call_count = 0;
    int last_block = 0;
    int cur_block = 0;
    off_t orig_offset = 0;
    off_t shard_offset = 0;
    size_t read_size = 0;
    size_t remaining_size = 0;
    fd_t *fd = NULL;
    fd_t *anon_fd = NULL;
    shard_local_t *local = NULL;
    gf_boolean_t wind_failed = _gf_false;

    local = frame->local;
    fd = local->fd;

    orig_offset = local->offset;
    cur_block = local->first_block;
    last_block = local->last_block;
    remaining_size = local->total_size;
    local->call_count = call_count = local->num_blocks;

    SHARD_SET_ROOT_FS_ID(frame, local);

    if (fd->flags & O_DIRECT)
        local->flags = O_DIRECT;

    while (cur_block <= last_block) {
        if (wind_failed) {
            shard_readv_do_cbk(frame, (void *)(long)0, this, -1, ENOMEM, NULL,
                               0, NULL, NULL, NULL);
            goto next;
        }

        shard_offset = orig_offset % local->block_size;
        read_size = local->block_size - shard_offset;
        if (read_size > remaining_size)
            read_size = remaining_size;

        remaining_size -= read_size;

        if (cur_block == 0) {
            anon_fd = fd_ref(fd);
        } else {
            anon_fd = fd_anonymous(local->inode_list[i]);
            if (!anon_fd) {
                local->op_ret = -1;
                local->op_errno = ENOMEM;
                wind_failed = _gf_true;
                shard_readv_do_cbk(frame, (void *)(long)anon_fd, this, -1,
                                   ENOMEM, NULL, 0, NULL, NULL, NULL);
                goto next;
            }
        }

        STACK_WIND_COOKIE(frame, shard_readv_do_cbk, anon_fd, FIRST_CHILD(this),
                          FIRST_CHILD(this)->fops->readv, anon_fd, read_size,
                          shard_offset, local->flags, local->xattr_req);

        orig_offset += read_size;
    next:
        cur_block++;
        i++;
        call_count--;
    }
    return 0;
}

int
shard_common_mknod_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, inode_t *inode,
                       struct iatt *buf, struct iatt *preparent,
                       struct iatt *postparent, dict_t *xdata)
{
    int shard_block_num = (long)cookie;
    int call_count = 0;
    shard_local_t *local = NULL;

    local = frame->local;

    if (op_ret < 0) {
        if (op_errno == EEXIST) {
            LOCK(&frame->lock);
            {
                local->eexist_count++;
            }
            UNLOCK(&frame->lock);
        } else {
            local->op_ret = op_ret;
            local->op_errno = op_errno;
        }
        gf_msg_debug(this->name, 0,
                     "mknod of shard %d "
                     "failed: %s",
                     shard_block_num, strerror(op_errno));
        goto done;
    }

    shard_link_block_inode(local, shard_block_num, inode, buf);

done:
    call_count = shard_call_count_return(frame);
    if (call_count == 0) {
        SHARD_UNSET_ROOT_FS_ID(frame, local);
        local->create_count = 0;
        local->post_mknod_handler(frame, this);
    }

    return 0;
}

int
shard_common_resume_mknod(call_frame_t *frame, xlator_t *this,
                          shard_post_mknod_fop_handler_t post_mknod_handler)
{
    int i = 0;
    int shard_idx_iter = 0;
    int last_block = 0;
    int ret = 0;
    int call_count = 0;
    char path[PATH_MAX] = {
        0,
    };
    mode_t mode = 0;
    char *bname = NULL;
    shard_priv_t *priv = NULL;
    shard_inode_ctx_t ctx_tmp = {
        0,
    };
    shard_local_t *local = NULL;
    gf_boolean_t wind_failed = _gf_false;
    fd_t *fd = NULL;
    loc_t loc = {
        0,
    };
    dict_t *xattr_req = NULL;

    local = frame->local;
    priv = this->private;
    fd = local->fd;
    shard_idx_iter = local->first_block;
    last_block = local->last_block;
    call_count = local->call_count = local->create_count;
    local->post_mknod_handler = post_mknod_handler;

    SHARD_SET_ROOT_FS_ID(frame, local);

    ret = shard_inode_ctx_get_all(fd->inode, this, &ctx_tmp);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_CTX_GET_FAILED,
               "Failed to get inode "
               "ctx for %s",
               uuid_utoa(fd->inode->gfid));
        local->op_ret = -1;
        local->op_errno = ENOMEM;
        goto err;
    }
    mode = st_mode_from_ia(ctx_tmp.stat.ia_prot, ctx_tmp.stat.ia_type);

    while (shard_idx_iter <= last_block) {
        if (local->inode_list[i]) {
            shard_idx_iter++;
            i++;
            continue;
        }

        if (wind_failed) {
            shard_common_mknod_cbk(frame, (void *)(long)shard_idx_iter, this,
                                   -1, ENOMEM, NULL, NULL, NULL, NULL, NULL);
            goto next;
        }

        shard_make_block_abspath(shard_idx_iter, fd->inode->gfid, path,
                                 sizeof(path));

        xattr_req = shard_create_gfid_dict(local->xattr_req);
        if (!xattr_req) {
            local->op_ret = -1;
            local->op_errno = ENOMEM;
            wind_failed = _gf_true;
            shard_common_mknod_cbk(frame, (void *)(long)shard_idx_iter, this,
                                   -1, ENOMEM, NULL, NULL, NULL, NULL, NULL);
            goto next;
        }

        bname = strrchr(path, '/') + 1;
        loc.inode = inode_new(this->itable);
        loc.parent = inode_ref(priv->dot_shard_inode);
        ret = inode_path(loc.parent, bname, (char **)&(loc.path));
        if (ret < 0 || !(loc.inode)) {
            gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_PATH_FAILED,
                   "Inode path failed"
                   "on %s, base file gfid = %s",
                   bname, uuid_utoa(fd->inode->gfid));
            local->op_ret = -1;
            local->op_errno = ENOMEM;
            wind_failed = _gf_true;
            loc_wipe(&loc);
            dict_unref(xattr_req);
            shard_common_mknod_cbk(frame, (void *)(long)shard_idx_iter, this,
                                   -1, ENOMEM, NULL, NULL, NULL, NULL, NULL);
            goto next;
        }

        loc.name = strrchr(loc.path, '/');
        if (loc.name)
            loc.name++;

        STACK_WIND_COOKIE(frame, shard_common_mknod_cbk,
                          (void *)(long)shard_idx_iter, FIRST_CHILD(this),
                          FIRST_CHILD(this)->fops->mknod, &loc, mode,
                          ctx_tmp.stat.ia_rdev, 0, xattr_req);
        loc_wipe(&loc);
        dict_unref(xattr_req);

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
    SHARD_UNSET_ROOT_FS_ID(frame, local);
    post_mknod_handler(frame, this);
    return 0;
}

int
shard_post_mknod_readv_handler(call_frame_t *frame, xlator_t *this);

int
shard_post_lookup_shards_readv_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(GF_FOP_READ, frame, local->op_ret,
                                    local->op_errno);
        return 0;
    }

    if (local->create_count) {
        shard_common_resume_mknod(frame, this, shard_post_mknod_readv_handler);
    } else {
        shard_readv_do(frame, this);
    }

    return 0;
}

int
shard_post_mknod_readv_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(GF_FOP_READ, frame, local->op_ret,
                                    local->op_errno);
        return 0;
    }

    if (!local->eexist_count) {
        shard_readv_do(frame, this);
    } else {
        local->call_count = local->eexist_count;
        shard_common_lookup_shards(frame, this, local->loc.inode,
                                   shard_post_lookup_shards_readv_handler);
    }
    return 0;
}

int
shard_post_resolve_readv_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->op_ret < 0) {
        if (local->op_errno != ENOENT) {
            shard_common_failure_unwind(GF_FOP_READ, frame, local->op_ret,
                                        local->op_errno);
            return 0;
        } else {
            struct iovec vec = {
                0,
            };

            vec.iov_base = local->iobuf->ptr;
            vec.iov_len = local->total_size;
            local->op_ret = local->total_size;
            SHARD_STACK_UNWIND(readv, frame, local->op_ret, 0, &vec, 1,
                               &local->prebuf, local->iobref, NULL);
            return 0;
        }
    }

    if (local->call_count) {
        shard_common_lookup_shards(frame, this, local->resolver_base_inode,
                                   shard_post_lookup_shards_readv_handler);
    } else {
        shard_readv_do(frame, this);
    }

    return 0;
}

int
shard_post_lookup_readv_handler(call_frame_t *frame, xlator_t *this)
{
    int ret = 0;
    struct iobuf *iobuf = NULL;
    shard_local_t *local = NULL;
    shard_priv_t *priv = NULL;

    priv = this->private;
    local = frame->local;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(GF_FOP_READ, frame, local->op_ret,
                                    local->op_errno);
        return 0;
    }

    if (local->offset >= local->prebuf.ia_size) {
        /* If the read is being performed past the end of the file,
         * unwind the FOP with 0 bytes read as status.
         */
        struct iovec vec = {
            0,
        };

        iobuf = iobuf_get2(this->ctx->iobuf_pool, local->req_size);
        if (!iobuf)
            goto err;

        vec.iov_base = iobuf->ptr;
        vec.iov_len = 0;
        local->iobref = iobref_new();
        iobref_add(local->iobref, iobuf);
        iobuf_unref(iobuf);

        SHARD_STACK_UNWIND(readv, frame, 0, 0, &vec, 1, &local->prebuf,
                           local->iobref, NULL);
        return 0;
    }

    local->first_block = get_lowest_block(local->offset, local->block_size);

    local->total_size = local->req_size;

    local->last_block = get_highest_block(local->offset, local->total_size,
                                          local->block_size);

    local->num_blocks = local->last_block - local->first_block + 1;
    GF_ASSERT(local->num_blocks > 0);
    local->resolver_base_inode = local->loc.inode;

    local->inode_list = GF_CALLOC(local->num_blocks, sizeof(inode_t *),
                                  gf_shard_mt_inode_list);
    if (!local->inode_list)
        goto err;

    iobuf = iobuf_get2(this->ctx->iobuf_pool, local->total_size);
    if (!iobuf)
        goto err;

    local->iobref = iobref_new();
    if (!local->iobref) {
        iobuf_unref(iobuf);
        goto err;
    }

    if (iobref_add(local->iobref, iobuf) != 0) {
        iobuf_unref(iobuf);
        goto err;
    }

    memset(iobuf->ptr, 0, local->total_size);
    iobuf_unref(iobuf);
    local->iobuf = iobuf;

    local->dot_shard_loc.inode = inode_find(this->itable, priv->dot_shard_gfid);
    if (!local->dot_shard_loc.inode) {
        ret = shard_init_internal_dir_loc(this, local,
                                          SHARD_INTERNAL_DIR_DOT_SHARD);
        if (ret)
            goto err;
        shard_lookup_internal_dir(frame, this, shard_post_resolve_readv_handler,
                                  SHARD_INTERNAL_DIR_DOT_SHARD);
    } else {
        local->post_res_handler = shard_post_resolve_readv_handler;
        shard_refresh_internal_dir(frame, this, SHARD_INTERNAL_DIR_DOT_SHARD);
    }
    return 0;
err:
    shard_common_failure_unwind(GF_FOP_READ, frame, -1, ENOMEM);
    return 0;
}

int
shard_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
            off_t offset, uint32_t flags, dict_t *xdata)
{
    int ret = 0;
    uint64_t block_size = 0;
    shard_local_t *local = NULL;

    ret = shard_inode_ctx_get_block_size(fd->inode, this, &block_size);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_CTX_GET_FAILED,
               "Failed to get block "
               "size for %s from its inode ctx",
               uuid_utoa(fd->inode->gfid));
        goto err;
    }

    if (!block_size || frame->root->pid == GF_CLIENT_PID_GSYNCD) {
        /* block_size = 0 means that the file was created before
         * sharding was enabled on the volume.
         */
        STACK_WIND(frame, default_readv_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->readv, fd, size, offset, flags,
                   xdata);
        return 0;
    }

    if (!this->itable)
        this->itable = fd->inode->table;

    local = mem_get0(this->local_pool);
    if (!local)
        goto err;

    frame->local = local;

    ret = syncbarrier_init(&local->barrier);
    if (ret)
        goto err;
    local->fd = fd_ref(fd);
    local->block_size = block_size;
    local->offset = offset;
    local->req_size = size;
    local->flags = flags;
    local->fop = GF_FOP_READ;
    local->xattr_req = (xdata) ? dict_ref(xdata) : dict_new();
    if (!local->xattr_req)
        goto err;

    local->loc.inode = inode_ref(fd->inode);
    gf_uuid_copy(local->loc.gfid, fd->inode->gfid);

    shard_refresh_base_file(frame, this, NULL, fd,
                            shard_post_lookup_readv_handler);
    return 0;
err:
    shard_common_failure_unwind(GF_FOP_READ, frame, -1, ENOMEM);
    return 0;
}

int
shard_common_inode_write_post_update_size_handler(call_frame_t *frame,
                                                  xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(local->fop, frame, local->op_ret,
                                    local->op_errno);
    } else {
        shard_common_inode_write_success_unwind(local->fop, frame,
                                                local->written_size);
    }
    return 0;
}

static gf_boolean_t
shard_is_appending_write(shard_local_t *local)
{
    if (local->fop != GF_FOP_WRITE)
        return _gf_false;
    if (local->flags & O_APPEND)
        return _gf_true;
    if (local->fd->flags & O_APPEND)
        return _gf_true;
    return _gf_false;
}

int
__shard_get_delta_size_from_inode_ctx(shard_local_t *local, inode_t *inode,
                                      xlator_t *this)
{
    int ret = -1;
    uint64_t ctx_uint = 0;
    shard_inode_ctx_t *ctx = NULL;

    ret = __inode_ctx_get(inode, this, &ctx_uint);
    if (ret < 0)
        return ret;

    ctx = (shard_inode_ctx_t *)(uintptr_t)ctx_uint;

    if (shard_is_appending_write(local)) {
        local->delta_size = local->total_size;
    } else if (local->offset + local->total_size > ctx->stat.ia_size) {
        local->delta_size = (local->offset + local->total_size) -
                            ctx->stat.ia_size;
    } else {
        local->delta_size = 0;
    }
    ctx->stat.ia_size += (local->delta_size);
    local->postbuf = ctx->stat;

    return 0;
}

int
shard_get_delta_size_from_inode_ctx(shard_local_t *local, inode_t *inode,
                                    xlator_t *this)
{
    int ret = -1;

    LOCK(&inode->lock);
    {
        ret = __shard_get_delta_size_from_inode_ctx(local, inode, this);
    }
    UNLOCK(&inode->lock);

    return ret;
}

int
shard_common_inode_write_do_cbk(call_frame_t *frame, void *cookie,
                                xlator_t *this, int32_t op_ret,
                                int32_t op_errno, struct iatt *pre,
                                struct iatt *post, dict_t *xdata)
{
    int call_count = 0;
    fd_t *anon_fd = cookie;
    shard_local_t *local = NULL;
    glusterfs_fop_t fop = 0;

    local = frame->local;
    fop = local->fop;

    LOCK(&frame->lock);
    {
        if (op_ret < 0) {
            local->op_ret = op_ret;
            local->op_errno = op_errno;
        } else {
            local->written_size += op_ret;
            GF_ATOMIC_ADD(local->delta_blocks,
                          post->ia_blocks - pre->ia_blocks);
            local->delta_size += (post->ia_size - pre->ia_size);
            shard_inode_ctx_set(local->fd->inode, this, post, 0,
                                SHARD_MASK_TIMES);
            if (local->fd->inode != anon_fd->inode)
                shard_inode_ctx_add_to_fsync_list(local->fd->inode, this,
                                                  anon_fd->inode);
        }
    }
    UNLOCK(&frame->lock);

    if (anon_fd)
        fd_unref(anon_fd);

    call_count = shard_call_count_return(frame);
    if (call_count == 0) {
        SHARD_UNSET_ROOT_FS_ID(frame, local);
        if (local->op_ret < 0) {
            shard_common_failure_unwind(fop, frame, local->op_ret,
                                        local->op_errno);
        } else {
            shard_get_delta_size_from_inode_ctx(local, local->fd->inode, this);
            local->hole_size = 0;
            if (xdata)
                local->xattr_rsp = dict_ref(xdata);
            shard_update_file_size(
                frame, this, local->fd, NULL,
                shard_common_inode_write_post_update_size_handler);
        }
    }

    return 0;
}

int
shard_common_inode_write_wind(call_frame_t *frame, xlator_t *this, fd_t *fd,
                              struct iovec *vec, int count, off_t shard_offset,
                              size_t size)
{
    shard_local_t *local = NULL;

    local = frame->local;

    switch (local->fop) {
        case GF_FOP_WRITE:
            STACK_WIND_COOKIE(
                frame, shard_common_inode_write_do_cbk, fd, FIRST_CHILD(this),
                FIRST_CHILD(this)->fops->writev, fd, vec, count, shard_offset,
                local->flags, local->iobref, local->xattr_req);
            break;
        case GF_FOP_FALLOCATE:
            STACK_WIND_COOKIE(
                frame, shard_common_inode_write_do_cbk, fd, FIRST_CHILD(this),
                FIRST_CHILD(this)->fops->fallocate, fd, local->flags,
                shard_offset, size, local->xattr_req);
            break;
        case GF_FOP_ZEROFILL:
            STACK_WIND_COOKIE(frame, shard_common_inode_write_do_cbk, fd,
                              FIRST_CHILD(this),
                              FIRST_CHILD(this)->fops->zerofill, fd,
                              shard_offset, size, local->xattr_req);
            break;
        case GF_FOP_DISCARD:
            STACK_WIND_COOKIE(frame, shard_common_inode_write_do_cbk, fd,
                              FIRST_CHILD(this),
                              FIRST_CHILD(this)->fops->discard, fd,
                              shard_offset, size, local->xattr_req);
            break;
        default:
            gf_msg(this->name, GF_LOG_WARNING, 0, SHARD_MSG_INVALID_FOP,
                   "Invalid fop id = %d", local->fop);
            break;
    }
    return 0;
}

int
shard_common_inode_write_do(call_frame_t *frame, xlator_t *this)
{
    int i = 0;
    int count = 0;
    int call_count = 0;
    int last_block = 0;
    uint32_t cur_block = 0;
    fd_t *fd = NULL;
    fd_t *anon_fd = NULL;
    shard_local_t *local = NULL;
    struct iovec *vec = NULL;
    gf_boolean_t wind_failed = _gf_false;
    gf_boolean_t odirect = _gf_false;
    off_t orig_offset = 0;
    off_t shard_offset = 0;
    off_t vec_offset = 0;
    size_t remaining_size = 0;
    size_t shard_write_size = 0;

    local = frame->local;
    fd = local->fd;

    orig_offset = local->offset;
    remaining_size = local->total_size;
    cur_block = local->first_block;
    local->call_count = call_count = local->num_blocks;
    last_block = local->last_block;

    SHARD_SET_ROOT_FS_ID(frame, local);

    if (dict_set_uint32(local->xattr_req, GLUSTERFS_WRITE_UPDATE_ATOMIC, 4)) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_DICT_OP_FAILED,
               "Failed to set " GLUSTERFS_WRITE_UPDATE_ATOMIC
               " into "
               "dict: %s",
               uuid_utoa(fd->inode->gfid));
        local->op_ret = -1;
        local->op_errno = ENOMEM;
        local->call_count = 1;
        shard_common_inode_write_do_cbk(frame, (void *)(long)0, this, -1,
                                        ENOMEM, NULL, NULL, NULL);
        return 0;
    }

    if ((fd->flags & O_DIRECT) && (local->fop == GF_FOP_WRITE))
        odirect = _gf_true;

    while (cur_block <= last_block) {
        if (wind_failed) {
            shard_common_inode_write_do_cbk(frame, (void *)(long)0, this, -1,
                                            ENOMEM, NULL, NULL, NULL);
            goto next;
        }

        shard_offset = orig_offset % local->block_size;
        shard_write_size = local->block_size - shard_offset;
        if (shard_write_size > remaining_size)
            shard_write_size = remaining_size;

        remaining_size -= shard_write_size;

        if (local->fop == GF_FOP_WRITE) {
            vec = NULL;
            count = iov_subset(local->vector, local->count, vec_offset,
                               shard_write_size, &vec, 0);
            if (count < 0) {
                local->op_ret = -1;
                local->op_errno = ENOMEM;
                wind_failed = _gf_true;
                shard_common_inode_write_do_cbk(frame, (void *)(long)0, this,
                                                -1, ENOMEM, NULL, NULL, NULL);
                goto next;
            }
        }

        if (cur_block == 0) {
            anon_fd = fd_ref(fd);
        } else {
            anon_fd = fd_anonymous(local->inode_list[i]);
            if (!anon_fd) {
                local->op_ret = -1;
                local->op_errno = ENOMEM;
                wind_failed = _gf_true;
                GF_FREE(vec);
                shard_common_inode_write_do_cbk(frame, (void *)(long)anon_fd,
                                                this, -1, ENOMEM, NULL, NULL,
                                                NULL);
                goto next;
            }

            if (local->fop == GF_FOP_WRITE) {
                if (odirect)
                    local->flags = O_DIRECT;
                else
                    local->flags = GF_ANON_FD_FLAGS;
            }
        }

        shard_common_inode_write_wind(frame, this, anon_fd, vec, count,
                                      shard_offset, shard_write_size);
        if (vec)
            vec_offset += shard_write_size;
        orig_offset += shard_write_size;
        GF_FREE(vec);
        vec = NULL;
    next:
        cur_block++;
        i++;
        call_count--;
    }
    return 0;
}

int
shard_common_inode_write_post_mknod_handler(call_frame_t *frame,
                                            xlator_t *this);

int
shard_common_inode_write_post_lookup_shards_handler(call_frame_t *frame,
                                                    xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(local->fop, frame, local->op_ret,
                                    local->op_errno);
        return 0;
    }

    if (local->create_count) {
        shard_common_resume_mknod(frame, this,
                                  shard_common_inode_write_post_mknod_handler);
    } else {
        shard_common_inode_write_do(frame, this);
    }

    return 0;
}

int
shard_common_inode_write_post_mknod_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(local->fop, frame, local->op_ret,
                                    local->op_errno);
        return 0;
    }

    if (!local->eexist_count) {
        shard_common_inode_write_do(frame, this);
    } else {
        local->call_count = local->eexist_count;
        shard_common_lookup_shards(
            frame, this, local->loc.inode,
            shard_common_inode_write_post_lookup_shards_handler);
    }

    return 0;
}

int
shard_common_inode_write_post_resolve_handler(call_frame_t *frame,
                                              xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(local->fop, frame, local->op_ret,
                                    local->op_errno);
        return 0;
    }

    if (local->call_count) {
        shard_common_lookup_shards(
            frame, this, local->resolver_base_inode,
            shard_common_inode_write_post_lookup_shards_handler);
    } else if (local->create_count) {
        shard_common_inode_write_post_lookup_shards_handler(frame, this);
    } else {
        shard_common_inode_write_do(frame, this);
    }

    return 0;
}

int
shard_common_inode_write_post_lookup_handler(call_frame_t *frame,
                                             xlator_t *this)
{
    shard_local_t *local = frame->local;
    shard_priv_t *priv = this->private;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(local->fop, frame, local->op_ret,
                                    local->op_errno);
        return 0;
    }

    local->postbuf = local->prebuf;

    /*Adjust offset to EOF so that correct shard is chosen for append*/
    if (shard_is_appending_write(local))
        local->offset = local->prebuf.ia_size;

    local->first_block = get_lowest_block(local->offset, local->block_size);
    local->last_block = get_highest_block(local->offset, local->total_size,
                                          local->block_size);
    local->num_blocks = local->last_block - local->first_block + 1;
    GF_ASSERT(local->num_blocks > 0);
    local->inode_list = GF_CALLOC(local->num_blocks, sizeof(inode_t *),
                                  gf_shard_mt_inode_list);
    if (!local->inode_list) {
        shard_common_failure_unwind(local->fop, frame, -1, ENOMEM);
        return 0;
    }

    gf_msg_trace(this->name, 0,
                 "%s: gfid=%s first_block=%" PRIu64
                 " "
                 "last_block=%" PRIu64 " num_blocks=%" PRIu64 " offset=%" PRId64
                 " total_size=%zu flags=%" PRId32 "",
                 gf_fop_list[local->fop],
                 uuid_utoa(local->resolver_base_inode->gfid),
                 local->first_block, local->last_block, local->num_blocks,
                 local->offset, local->total_size, local->flags);

    local->dot_shard_loc.inode = inode_find(this->itable, priv->dot_shard_gfid);

    if (!local->dot_shard_loc.inode) {
        /*change handler*/
        shard_mkdir_internal_dir(frame, this,
                                 shard_common_inode_write_post_resolve_handler,
                                 SHARD_INTERNAL_DIR_DOT_SHARD);
    } else {
        /*change handler*/
        local->post_res_handler = shard_common_inode_write_post_resolve_handler;
        shard_refresh_internal_dir(frame, this, SHARD_INTERNAL_DIR_DOT_SHARD);
    }
    return 0;
}

int
shard_mkdir_internal_dir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno, inode_t *inode,
                             struct iatt *buf, struct iatt *preparent,
                             struct iatt *postparent, dict_t *xdata)
{
    inode_t *link_inode = NULL;
    shard_local_t *local = NULL;
    shard_internal_dir_type_t type = (shard_internal_dir_type_t)cookie;

    local = frame->local;

    SHARD_UNSET_ROOT_FS_ID(frame, local);

    if (op_ret == -1) {
        if (op_errno != EEXIST) {
            local->op_ret = op_ret;
            local->op_errno = op_errno;
            goto unwind;
        } else {
            gf_msg_debug(this->name, 0,
                         "mkdir on %s failed "
                         "with EEXIST. Attempting lookup now",
                         shard_internal_dir_string(type));
            shard_lookup_internal_dir(frame, this, local->post_res_handler,
                                      type);
            return 0;
        }
    }

    link_inode = shard_link_internal_dir_inode(local, inode, buf, type);
    if (link_inode != inode) {
        shard_refresh_internal_dir(frame, this, type);
    } else {
        shard_inode_ctx_mark_dir_refreshed(link_inode, this);
        shard_common_resolve_shards(frame, this, local->post_res_handler);
    }
    return 0;
unwind:
    shard_common_resolve_shards(frame, this, local->post_res_handler);
    return 0;
}

int
shard_mkdir_internal_dir(call_frame_t *frame, xlator_t *this,
                         shard_post_resolve_fop_handler_t handler,
                         shard_internal_dir_type_t type)
{
    int ret = -1;
    shard_local_t *local = NULL;
    shard_priv_t *priv = NULL;
    dict_t *xattr_req = NULL;
    uuid_t *gfid = NULL;
    loc_t *loc = NULL;
    gf_boolean_t free_gfid = _gf_true;

    local = frame->local;
    priv = this->private;

    local->post_res_handler = handler;
    gfid = GF_MALLOC(sizeof(uuid_t), gf_common_mt_uuid_t);
    if (!gfid)
        goto err;

    switch (type) {
        case SHARD_INTERNAL_DIR_DOT_SHARD:
            gf_uuid_copy(*gfid, priv->dot_shard_gfid);
            loc = &local->dot_shard_loc;
            break;
        case SHARD_INTERNAL_DIR_DOT_SHARD_REMOVE_ME:
            gf_uuid_copy(*gfid, priv->dot_shard_rm_gfid);
            loc = &local->dot_shard_rm_loc;
            break;
        default:
            bzero(*gfid, sizeof(uuid_t));
            break;
    }

    xattr_req = dict_new();
    if (!xattr_req)
        goto err;

    ret = shard_init_internal_dir_loc(this, local, type);
    if (ret)
        goto err;

    ret = dict_set_gfuuid(xattr_req, "gfid-req", *gfid, false);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_DICT_OP_FAILED,
               "Failed to set gfid-req for %s",
               shard_internal_dir_string(type));
        goto err;
    } else {
        free_gfid = _gf_false;
    }

    SHARD_SET_ROOT_FS_ID(frame, local);

    STACK_WIND_COOKIE(frame, shard_mkdir_internal_dir_cbk, (void *)(long)type,
                      FIRST_CHILD(this), FIRST_CHILD(this)->fops->mkdir, loc,
                      0755, 0, xattr_req);
    dict_unref(xattr_req);
    return 0;

err:
    if (xattr_req)
        dict_unref(xattr_req);
    local->op_ret = -1;
    local->op_errno = ENOMEM;
    if (free_gfid)
        GF_FREE(gfid);
    handler(frame, this);
    return 0;
}

int
shard_flush_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    /* To-Do: Wind flush on all shards of the file */
    SHARD_STACK_UNWIND(flush, frame, op_ret, op_errno, xdata);
    return 0;
}

int
shard_flush(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    STACK_WIND(frame, shard_flush_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->flush, fd, xdata);
    return 0;
}

int
__shard_get_timestamps_from_inode_ctx(shard_local_t *local, inode_t *inode,
                                      xlator_t *this)
{
    int ret = -1;
    uint64_t ctx_uint = 0;
    shard_inode_ctx_t *ctx = NULL;

    ret = __inode_ctx_get(inode, this, &ctx_uint);
    if (ret < 0)
        return ret;

    ctx = (shard_inode_ctx_t *)(uintptr_t)ctx_uint;

    local->postbuf.ia_ctime = ctx->stat.ia_ctime;
    local->postbuf.ia_ctime_nsec = ctx->stat.ia_ctime_nsec;
    local->postbuf.ia_atime = ctx->stat.ia_atime;
    local->postbuf.ia_atime_nsec = ctx->stat.ia_atime_nsec;
    local->postbuf.ia_mtime = ctx->stat.ia_mtime;
    local->postbuf.ia_mtime_nsec = ctx->stat.ia_mtime_nsec;

    return 0;
}

int
shard_get_timestamps_from_inode_ctx(shard_local_t *local, inode_t *inode,
                                    xlator_t *this)
{
    int ret = 0;

    LOCK(&inode->lock);
    {
        ret = __shard_get_timestamps_from_inode_ctx(local, inode, this);
    }
    UNLOCK(&inode->lock);

    return ret;
}

int
shard_fsync_shards_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                       struct iatt *postbuf, dict_t *xdata)
{
    int call_count = 0;
    uint64_t fsync_count = 0;
    fd_t *anon_fd = cookie;
    shard_local_t *local = NULL;
    shard_inode_ctx_t *ctx = NULL;
    shard_inode_ctx_t *base_ictx = NULL;
    inode_t *base_inode = NULL;
    gf_boolean_t unref_shard_inode = _gf_false;

    local = frame->local;
    base_inode = local->fd->inode;

    if (local->op_ret < 0)
        goto out;

    LOCK(&frame->lock);
    {
        if (op_ret < 0) {
            local->op_ret = op_ret;
            local->op_errno = op_errno;
            UNLOCK(&frame->lock);
            goto out;
        }
        shard_inode_ctx_set(local->fd->inode, this, postbuf, 0,
                            SHARD_MASK_TIMES);
    }
    UNLOCK(&frame->lock);
    fd_ctx_get(anon_fd, this, &fsync_count);
out:
    if (anon_fd && (base_inode != anon_fd->inode)) {
        LOCK(&base_inode->lock);
        LOCK(&anon_fd->inode->lock);
        {
            __shard_inode_ctx_get(anon_fd->inode, this, &ctx);
            __shard_inode_ctx_get(base_inode, this, &base_ictx);
            if (op_ret == 0)
                ctx->fsync_needed -= fsync_count;
            GF_ASSERT(ctx->fsync_needed >= 0);
            if (ctx->fsync_needed != 0) {
                list_add_tail(&ctx->to_fsync_list, &base_ictx->to_fsync_list);
                base_ictx->fsync_count++;
            } else {
                unref_shard_inode = _gf_true;
            }
        }
        UNLOCK(&anon_fd->inode->lock);
        UNLOCK(&base_inode->lock);
    }

    if (unref_shard_inode)
        inode_unref(anon_fd->inode);
    if (anon_fd)
        fd_unref(anon_fd);

    call_count = shard_call_count_return(frame);
    if (call_count != 0)
        return 0;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(GF_FOP_FSYNC, frame, local->op_ret,
                                    local->op_errno);
    } else {
        shard_get_timestamps_from_inode_ctx(local, base_inode, this);
        SHARD_STACK_UNWIND(fsync, frame, local->op_ret, local->op_errno,
                           &local->prebuf, &local->postbuf, local->xattr_rsp);
    }
    return 0;
}

int
shard_post_lookup_fsync_handler(call_frame_t *frame, xlator_t *this)
{
    int ret = 0;
    int call_count = 0;
    int fsync_count = 0;
    fd_t *anon_fd = NULL;
    inode_t *base_inode = NULL;
    shard_local_t *local = NULL;
    shard_inode_ctx_t *ctx = NULL;
    shard_inode_ctx_t *iter = NULL;
    struct list_head copy = {
        0,
    };
    shard_inode_ctx_t *tmp = NULL;

    local = frame->local;
    base_inode = local->fd->inode;
    local->postbuf = local->prebuf;
    INIT_LIST_HEAD(&copy);

    if (local->op_ret < 0) {
        shard_common_failure_unwind(GF_FOP_FSYNC, frame, local->op_ret,
                                    local->op_errno);
        return 0;
    }

    LOCK(&base_inode->lock);
    {
        __shard_inode_ctx_get(base_inode, this, &ctx);
        list_splice_init(&ctx->to_fsync_list, &copy);
        call_count = ctx->fsync_count;
        ctx->fsync_count = 0;
    }
    UNLOCK(&base_inode->lock);

    local->call_count = ++call_count;

    /* Send fsync() on the base shard first */
    anon_fd = fd_ref(local->fd);
    STACK_WIND_COOKIE(frame, shard_fsync_shards_cbk, anon_fd, FIRST_CHILD(this),
                      FIRST_CHILD(this)->fops->fsync, anon_fd, local->datasync,
                      local->xattr_req);
    call_count--;
    anon_fd = NULL;

    list_for_each_entry_safe(iter, tmp, &copy, to_fsync_list)
    {
        list_del_init(&iter->to_fsync_list);
        fsync_count = 0;
        shard_inode_ctx_get_fsync_count(iter->inode, this, &fsync_count);
        GF_ASSERT(fsync_count > 0);
        anon_fd = fd_anonymous(iter->inode);
        if (!anon_fd) {
            local->op_ret = -1;
            local->op_errno = ENOMEM;
            gf_msg(this->name, GF_LOG_WARNING, ENOMEM,
                   SHARD_MSG_MEMALLOC_FAILED,
                   "Failed to create "
                   "anon fd to fsync shard");
            shard_fsync_shards_cbk(frame, (void *)(long)anon_fd, this, -1,
                                   ENOMEM, NULL, NULL, NULL);
            continue;
        }

        ret = fd_ctx_set(anon_fd, this, fsync_count);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_FD_CTX_SET_FAILED,
                   "Failed to set fd "
                   "ctx for shard inode gfid=%s",
                   uuid_utoa(iter->inode->gfid));
            local->op_ret = -1;
            local->op_errno = ENOMEM;
            shard_fsync_shards_cbk(frame, (void *)(long)anon_fd, this, -1,
                                   ENOMEM, NULL, NULL, NULL);
            continue;
        }
        STACK_WIND_COOKIE(frame, shard_fsync_shards_cbk, anon_fd,
                          FIRST_CHILD(this), FIRST_CHILD(this)->fops->fsync,
                          anon_fd, local->datasync, local->xattr_req);
        call_count--;
    }

    return 0;
}

int
shard_fsync(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
            dict_t *xdata)
{
    int ret = 0;
    uint64_t block_size = 0;
    shard_local_t *local = NULL;

    ret = shard_inode_ctx_get_block_size(fd->inode, this, &block_size);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_CTX_GET_FAILED,
               "Failed to get block "
               "size for %s from its inode ctx",
               uuid_utoa(fd->inode->gfid));
        goto err;
    }

    if (!block_size || frame->root->pid == GF_CLIENT_PID_GSYNCD) {
        STACK_WIND(frame, default_fsync_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->fsync, fd, datasync, xdata);
        return 0;
    }

    if (!this->itable)
        this->itable = fd->inode->table;

    local = mem_get0(this->local_pool);
    if (!local)
        goto err;

    frame->local = local;

    local->fd = fd_ref(fd);
    local->fop = GF_FOP_FSYNC;
    local->datasync = datasync;
    local->xattr_req = (xdata) ? dict_ref(xdata) : dict_new();
    if (!local->xattr_req)
        goto err;

    local->loc.inode = inode_ref(fd->inode);
    gf_uuid_copy(local->loc.gfid, fd->inode->gfid);

    shard_refresh_base_file(frame, this, NULL, fd,
                            shard_post_lookup_fsync_handler);
    return 0;
err:
    shard_common_failure_unwind(GF_FOP_FSYNC, frame, -1, ENOMEM);
    return 0;
}

int
shard_readdir_past_dot_shard_cbk(call_frame_t *frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno, gf_dirent_t *orig_entries,
                                 dict_t *xdata)
{
    gf_dirent_t *entry = NULL;
    gf_dirent_t *tmp = NULL;
    shard_local_t *local = NULL;

    local = frame->local;

    if (op_ret < 0)
        goto unwind;

    list_for_each_entry_safe(entry, tmp, (&orig_entries->list), list)
    {
        list_del_init(&entry->list);
        list_add_tail(&entry->list, &local->entries_head.list);

        if (!entry->dict)
            continue;

        if (IA_ISDIR(entry->d_stat.ia_type))
            continue;

        if (dict_get(entry->dict, GF_XATTR_SHARD_FILE_SIZE))
            shard_modify_size_and_block_count(&entry->d_stat, entry->dict);
        if (!entry->inode)
            continue;

        shard_inode_ctx_update(entry->inode, this, entry->dict, &entry->d_stat);
    }
    local->op_ret += op_ret;

unwind:
    if (local->fop == GF_FOP_READDIR)
        SHARD_STACK_UNWIND(readdir, frame, local->op_ret, local->op_errno,
                           &local->entries_head, xdata);
    else
        SHARD_STACK_UNWIND(readdirp, frame, op_ret, op_errno,
                           &local->entries_head, xdata);
    return 0;
}

int32_t
shard_readdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, gf_dirent_t *orig_entries,
                  dict_t *xdata)
{
    fd_t *fd = NULL;
    gf_dirent_t *entry = NULL;
    gf_dirent_t *tmp = NULL;
    shard_local_t *local = NULL;
    gf_boolean_t last_entry = _gf_false;

    local = frame->local;
    fd = local->fd;

    if (op_ret < 0)
        goto unwind;

    list_for_each_entry_safe(entry, tmp, (&orig_entries->list), list)
    {
        if (last_entry)
            last_entry = _gf_false;

        if (__is_root_gfid(fd->inode->gfid) &&
            !(strcmp(entry->d_name, GF_SHARD_DIR))) {
            local->offset = entry->d_off;
            op_ret--;
            last_entry = _gf_true;
            continue;
        }

        list_del_init(&entry->list);
        list_add_tail(&entry->list, &local->entries_head.list);

        if (!entry->dict)
            continue;

        if (IA_ISDIR(entry->d_stat.ia_type))
            continue;

        if (dict_get(entry->dict, GF_XATTR_SHARD_FILE_SIZE) &&
            frame->root->pid != GF_CLIENT_PID_GSYNCD)
            shard_modify_size_and_block_count(&entry->d_stat, entry->dict);

        if (!entry->inode)
            continue;

        shard_inode_ctx_update(entry->inode, this, entry->dict, &entry->d_stat);
    }

    local->op_ret = op_ret;

    if (last_entry) {
        if (local->fop == GF_FOP_READDIR)
            STACK_WIND(frame, shard_readdir_past_dot_shard_cbk,
                       FIRST_CHILD(this), FIRST_CHILD(this)->fops->readdir,
                       local->fd, local->readdir_size, local->offset,
                       local->xattr_req);
        else
            STACK_WIND(frame, shard_readdir_past_dot_shard_cbk,
                       FIRST_CHILD(this), FIRST_CHILD(this)->fops->readdirp,
                       local->fd, local->readdir_size, local->offset,
                       local->xattr_req);
        return 0;
    }

unwind:
    if (local->fop == GF_FOP_READDIR)
        SHARD_STACK_UNWIND(readdir, frame, op_ret, op_errno,
                           &local->entries_head, xdata);
    else
        SHARD_STACK_UNWIND(readdirp, frame, op_ret, op_errno,
                           &local->entries_head, xdata);
    return 0;
}

int
shard_readdir_do(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                 off_t offset, int whichop, dict_t *xdata)
{
    int ret = 0;
    shard_local_t *local = NULL;

    local = mem_get0(this->local_pool);
    if (!local) {
        goto err;
    }

    frame->local = local;

    local->fd = fd_ref(fd);
    local->fop = whichop;
    local->readdir_size = size;
    INIT_LIST_HEAD(&local->entries_head.list);
    local->list_inited = _gf_true;

    if (whichop == GF_FOP_READDIR) {
        STACK_WIND(frame, shard_readdir_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->readdir, fd, size, offset, xdata);
    } else {
        local->xattr_req = (xdata) ? dict_ref(xdata) : dict_new();
        SHARD_MD_READ_FOP_INIT_REQ_DICT(this, local->xattr_req, fd->inode->gfid,
                                        local, err);
        ret = dict_set_uint64(local->xattr_req, GF_XATTR_SHARD_BLOCK_SIZE, 0);
        if (ret) {
            gf_log(this->name, GF_LOG_WARNING,
                   "Failed to set "
                   "dict value: key:%s, directory gfid=%s",
                   GF_XATTR_SHARD_BLOCK_SIZE, uuid_utoa(fd->inode->gfid));
            goto err;
        }

        STACK_WIND(frame, shard_readdir_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->readdirp, fd, size, offset,
                   local->xattr_req);
    }

    return 0;

err:
    STACK_UNWIND_STRICT(readdir, frame, -1, ENOMEM, NULL, NULL);
    return 0;
}

int32_t
shard_readdir(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t offset, dict_t *xdata)
{
    shard_readdir_do(frame, this, fd, size, offset, GF_FOP_READDIR, xdata);
    return 0;
}

int32_t
shard_readdirp(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
               off_t offset, dict_t *xdata)
{
    shard_readdir_do(frame, this, fd, size, offset, GF_FOP_READDIRP, xdata);
    return 0;
}

int32_t
shard_modify_and_set_iatt_in_dict(dict_t *xdata, shard_local_t *local,
                                  char *key)
{
    int ret = 0;
    struct iatt *tmpbuf = NULL;
    struct iatt *stbuf = NULL;
    data_t *data = NULL;

    if (!xdata)
        return 0;

    data = dict_get(xdata, key);
    if (!data)
        return 0;

    tmpbuf = data_to_iatt(data, key);
    stbuf = GF_MALLOC(sizeof(struct iatt), gf_common_mt_char);
    if (stbuf == NULL) {
        local->op_ret = -1;
        local->op_errno = ENOMEM;
        goto err;
    }
    *stbuf = *tmpbuf;
    stbuf->ia_size = local->prebuf.ia_size;
    stbuf->ia_blocks = local->prebuf.ia_blocks;
    ret = dict_set_iatt(xdata, key, stbuf, false);
    if (ret < 0) {
        local->op_ret = -1;
        local->op_errno = ENOMEM;
        goto err;
    }
    return 0;

err:
    GF_FREE(stbuf);
    return -1;
}

int32_t
shard_common_remove_xattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                              int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    int ret = -1;
    shard_local_t *local = NULL;

    local = frame->local;

    if (op_ret < 0) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;
        goto err;
    }

    ret = shard_modify_and_set_iatt_in_dict(xdata, local, GF_PRESTAT);
    if (ret < 0)
        goto err;

    ret = shard_modify_and_set_iatt_in_dict(xdata, local, GF_POSTSTAT);
    if (ret < 0)
        goto err;

    if (local->fd)
        SHARD_STACK_UNWIND(fremovexattr, frame, local->op_ret, local->op_errno,
                           xdata);
    else
        SHARD_STACK_UNWIND(removexattr, frame, local->op_ret, local->op_errno,
                           xdata);
    return 0;

err:
    shard_common_failure_unwind(local->fop, frame, local->op_ret,
                                local->op_errno);
    return 0;
}

int32_t
shard_post_lookup_remove_xattr_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(local->fop, frame, local->op_ret,
                                    local->op_errno);
        return 0;
    }

    if (local->fd)
        STACK_WIND(frame, shard_common_remove_xattr_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->fremovexattr, local->fd,
                   local->name, local->xattr_req);
    else
        STACK_WIND(frame, shard_common_remove_xattr_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->removexattr, &local->loc,
                   local->name, local->xattr_req);
    return 0;
}

int32_t
shard_common_remove_xattr(call_frame_t *frame, xlator_t *this,
                          glusterfs_fop_t fop, loc_t *loc, fd_t *fd,
                          const char *name, dict_t *xdata)
{
    int ret = -1;
    int op_errno = ENOMEM;
    uint64_t block_size = 0;
    shard_local_t *local = NULL;
    inode_t *inode = loc ? loc->inode : fd->inode;

    if ((IA_ISDIR(inode->ia_type)) || (IA_ISLNK(inode->ia_type))) {
        if (loc)
            STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->removexattr, loc, name,
                            xdata);
        else
            STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fremovexattr, fd, name,
                            xdata);
        return 0;
    }

    /* If shard's special xattrs are attempted to be removed,
     * fail the fop with EPERM (except if the client is gsyncd).
     */
    if (frame->root->pid != GF_CLIENT_PID_GSYNCD) {
        GF_IF_NATIVE_XATTR_GOTO(SHARD_XATTR_PREFIX "*", name, op_errno, err);
    }

    /* Repeat the same check for bulk-removexattr */
    if (xdata && (frame->root->pid != GF_CLIENT_PID_GSYNCD)) {
        dict_del(xdata, GF_XATTR_SHARD_BLOCK_SIZE);
        dict_del(xdata, GF_XATTR_SHARD_FILE_SIZE);
    }

    ret = shard_inode_ctx_get_block_size(inode, this, &block_size);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_CTX_GET_FAILED,
               "Failed to get block size from inode ctx of %s",
               uuid_utoa(inode->gfid));
        goto err;
    }

    if (!block_size || frame->root->pid == GF_CLIENT_PID_GSYNCD) {
        if (loc)
            STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->removexattr, loc, name,
                            xdata);
        else
            STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fremovexattr, fd, name,
                            xdata);
        return 0;
    }

    local = mem_get0(this->local_pool);
    if (!local)
        goto err;

    frame->local = local;
    local->fop = fop;
    if (loc) {
        if (loc_copy(&local->loc, loc) != 0)
            goto err;
    }

    if (fd) {
        local->fd = fd_ref(fd);
        local->loc.inode = inode_ref(fd->inode);
        gf_uuid_copy(local->loc.gfid, fd->inode->gfid);
    }

    if (name) {
        local->name = gf_strdup(name);
        if (!local->name)
            goto err;
    }

    if (xdata)
        local->xattr_req = dict_ref(xdata);

    shard_refresh_base_file(frame, this, loc, fd,
                            shard_post_lookup_remove_xattr_handler);
    return 0;
err:
    shard_common_failure_unwind(fop, frame, -1, op_errno);
    return 0;
}

int32_t
shard_removexattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                  const char *name, dict_t *xdata)
{
    shard_common_remove_xattr(frame, this, GF_FOP_REMOVEXATTR, loc, NULL, name,
                              xdata);
    return 0;
}

int32_t
shard_fremovexattr(call_frame_t *frame, xlator_t *this, fd_t *fd,
                   const char *name, dict_t *xdata)
{
    shard_common_remove_xattr(frame, this, GF_FOP_FREMOVEXATTR, NULL, fd, name,
                              xdata);
    return 0;
}

int32_t
shard_fgetxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *dict,
                    dict_t *xdata)
{
    if (op_ret < 0)
        goto unwind;

    if (dict && (frame->root->pid != GF_CLIENT_PID_GSYNCD)) {
        dict_del(dict, GF_XATTR_SHARD_BLOCK_SIZE);
        dict_del(dict, GF_XATTR_SHARD_FILE_SIZE);
    }

unwind:
    SHARD_STACK_UNWIND(fgetxattr, frame, op_ret, op_errno, dict, xdata);
    return 0;
}

int32_t
shard_fgetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name,
                dict_t *xdata)
{
    int op_errno = EINVAL;

    if ((frame->root->pid != GF_CLIENT_PID_GSYNCD) && (name) &&
        (!strncmp(name, SHARD_XATTR_PREFIX, SLEN(SHARD_XATTR_PREFIX)))) {
        op_errno = ENODATA;
        goto out;
    }

    STACK_WIND(frame, shard_fgetxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fgetxattr, fd, name, xdata);
    return 0;
out:
    shard_common_failure_unwind(GF_FOP_FGETXATTR, frame, -1, op_errno);
    return 0;
}

int32_t
shard_getxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *dict,
                   dict_t *xdata)
{
    if (op_ret < 0)
        goto unwind;

    if (dict && (frame->root->pid != GF_CLIENT_PID_GSYNCD)) {
        dict_del(dict, GF_XATTR_SHARD_BLOCK_SIZE);
        dict_del(dict, GF_XATTR_SHARD_FILE_SIZE);
    }

unwind:
    SHARD_STACK_UNWIND(getxattr, frame, op_ret, op_errno, dict, xdata);
    return 0;
}

int32_t
shard_getxattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
               const char *name, dict_t *xdata)
{
    int op_errno = EINVAL;

    if ((frame->root->pid != GF_CLIENT_PID_GSYNCD) && (name) &&
        (!strncmp(name, SHARD_XATTR_PREFIX, sizeof(SHARD_XATTR_PREFIX) - 1))) {
        op_errno = ENODATA;
        goto out;
    }

    STACK_WIND(frame, shard_getxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->getxattr, loc, name, xdata);
    return 0;
out:
    shard_common_failure_unwind(GF_FOP_GETXATTR, frame, -1, op_errno);
    return 0;
}

int32_t
shard_common_set_xattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    int ret = -1;
    shard_local_t *local = NULL;

    local = frame->local;

    if (op_ret < 0) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;
        goto err;
    }

    ret = shard_modify_and_set_iatt_in_dict(xdata, local, GF_PRESTAT);
    if (ret < 0)
        goto err;

    ret = shard_modify_and_set_iatt_in_dict(xdata, local, GF_POSTSTAT);
    if (ret < 0)
        goto err;

    if (local->fd)
        SHARD_STACK_UNWIND(fsetxattr, frame, local->op_ret, local->op_errno,
                           xdata);
    else
        SHARD_STACK_UNWIND(setxattr, frame, local->op_ret, local->op_errno,
                           xdata);
    return 0;

err:
    shard_common_failure_unwind(local->fop, frame, local->op_ret,
                                local->op_errno);
    return 0;
}

int32_t
shard_post_lookup_set_xattr_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->op_ret < 0) {
        shard_common_failure_unwind(local->fop, frame, local->op_ret,
                                    local->op_errno);
        return 0;
    }

    if (local->fd)
        STACK_WIND(frame, shard_common_set_xattr_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->fsetxattr, local->fd,
                   local->xattr_req, local->flags, local->xattr_rsp);
    else
        STACK_WIND(frame, shard_common_set_xattr_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->setxattr, &local->loc,
                   local->xattr_req, local->flags, local->xattr_rsp);
    return 0;
}

int32_t
shard_common_set_xattr(call_frame_t *frame, xlator_t *this, glusterfs_fop_t fop,
                       loc_t *loc, fd_t *fd, dict_t *dict, int32_t flags,
                       dict_t *xdata)
{
    int ret = -1;
    int op_errno = ENOMEM;
    uint64_t block_size = 0;
    shard_local_t *local = NULL;
    inode_t *inode = loc ? loc->inode : fd->inode;

    if ((IA_ISDIR(inode->ia_type)) || (IA_ISLNK(inode->ia_type))) {
        if (loc)
            STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->setxattr, loc, dict, flags,
                            xdata);
        else
            STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsetxattr, fd, dict, flags,
                            xdata);
        return 0;
    }

    /* Sharded or not, if shard's special xattrs are attempted to be set,
     * fail the fop with EPERM (except if the client is gsyncd.
     */
    if (frame->root->pid != GF_CLIENT_PID_GSYNCD) {
        GF_IF_INTERNAL_XATTR_GOTO(SHARD_XATTR_PREFIX "*", dict, op_errno, err);
    }

    ret = shard_inode_ctx_get_block_size(inode, this, &block_size);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_CTX_GET_FAILED,
               "Failed to get block size from inode ctx of %s",
               uuid_utoa(inode->gfid));
        goto err;
    }

    if (!block_size || frame->root->pid == GF_CLIENT_PID_GSYNCD) {
        if (loc)
            STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->setxattr, loc, dict, flags,
                            xdata);
        else
            STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->fsetxattr, fd, dict, flags,
                            xdata);
        return 0;
    }

    local = mem_get0(this->local_pool);
    if (!local)
        goto err;

    frame->local = local;
    local->fop = fop;
    if (loc) {
        if (loc_copy(&local->loc, loc) != 0)
            goto err;
    }

    if (fd) {
        local->fd = fd_ref(fd);
        local->loc.inode = inode_ref(fd->inode);
        gf_uuid_copy(local->loc.gfid, fd->inode->gfid);
    }
    local->flags = flags;
    /* Reusing local->xattr_req and local->xattr_rsp to store the setxattr dict
     * and the xdata dict
     */
    if (dict)
        local->xattr_req = dict_ref(dict);
    if (xdata)
        local->xattr_rsp = dict_ref(xdata);

    shard_refresh_base_file(frame, this, loc, fd,
                            shard_post_lookup_set_xattr_handler);
    return 0;
err:
    shard_common_failure_unwind(fop, frame, -1, op_errno);
    return 0;
}

int32_t
shard_fsetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
                int32_t flags, dict_t *xdata)
{
    shard_common_set_xattr(frame, this, GF_FOP_FSETXATTR, NULL, fd, dict, flags,
                           xdata);
    return 0;
}

int32_t
shard_setxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
               int32_t flags, dict_t *xdata)
{
    shard_common_set_xattr(frame, this, GF_FOP_SETXATTR, loc, NULL, dict, flags,
                           xdata);
    return 0;
}

int
shard_post_setattr_handler(call_frame_t *frame, xlator_t *this)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (local->fop == GF_FOP_SETATTR) {
        if (local->op_ret >= 0)
            shard_inode_ctx_set(local->loc.inode, this, &local->postbuf, 0,
                                SHARD_LOOKUP_MASK);
        SHARD_STACK_UNWIND(setattr, frame, local->op_ret, local->op_errno,
                           &local->prebuf, &local->postbuf, local->xattr_rsp);
    } else if (local->fop == GF_FOP_FSETATTR) {
        if (local->op_ret >= 0)
            shard_inode_ctx_set(local->fd->inode, this, &local->postbuf, 0,
                                SHARD_LOOKUP_MASK);
        SHARD_STACK_UNWIND(fsetattr, frame, local->op_ret, local->op_errno,
                           &local->prebuf, &local->postbuf, local->xattr_rsp);
    }

    return 0;
}

int
shard_common_setattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                         struct iatt *postbuf, dict_t *xdata)
{
    shard_local_t *local = NULL;

    local = frame->local;

    if (op_ret < 0) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;
        goto unwind;
    }

    local->prebuf = *prebuf;
    if (shard_modify_size_and_block_count(&local->prebuf, xdata)) {
        local->op_ret = -1;
        local->op_errno = EINVAL;
        goto unwind;
    }
    if (xdata)
        local->xattr_rsp = dict_ref(xdata);
    local->postbuf = *postbuf;
    local->postbuf.ia_size = local->prebuf.ia_size;
    local->postbuf.ia_blocks = local->prebuf.ia_blocks;

unwind:
    local->handler(frame, this);
    return 0;
}

int
shard_setattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
              struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
    int ret = -1;
    uint64_t block_size = 0;
    shard_local_t *local = NULL;

    if ((IA_ISDIR(loc->inode->ia_type)) || (IA_ISLNK(loc->inode->ia_type))) {
        STACK_WIND(frame, default_setattr_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->setattr, loc, stbuf, valid, xdata);
        return 0;
    }

    ret = shard_inode_ctx_get_block_size(loc->inode, this, &block_size);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_CTX_GET_FAILED,
               "Failed to get block size from inode ctx of %s",
               uuid_utoa(loc->inode->gfid));
        goto err;
    }

    if (!block_size || frame->root->pid == GF_CLIENT_PID_GSYNCD) {
        STACK_WIND(frame, default_setattr_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->setattr, loc, stbuf, valid, xdata);
        return 0;
    }

    local = mem_get0(this->local_pool);
    if (!local)
        goto err;

    frame->local = local;

    local->handler = shard_post_setattr_handler;
    local->xattr_req = (xdata) ? dict_ref(xdata) : dict_new();
    if (!local->xattr_req)
        goto err;
    local->fop = GF_FOP_SETATTR;
    loc_copy(&local->loc, loc);

    SHARD_MD_READ_FOP_INIT_REQ_DICT(this, local->xattr_req, local->loc.gfid,
                                    local, err);

    STACK_WIND(frame, shard_common_setattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->setattr, loc, stbuf, valid,
               local->xattr_req);
    return 0;
err:
    shard_common_failure_unwind(GF_FOP_SETATTR, frame, -1, ENOMEM);
    return 0;
}

int
shard_fsetattr(call_frame_t *frame, xlator_t *this, fd_t *fd,
               struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
    int ret = -1;
    uint64_t block_size = 0;
    shard_local_t *local = NULL;

    if ((IA_ISDIR(fd->inode->ia_type)) || (IA_ISLNK(fd->inode->ia_type))) {
        STACK_WIND(frame, default_fsetattr_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->fsetattr, fd, stbuf, valid, xdata);
        return 0;
    }

    ret = shard_inode_ctx_get_block_size(fd->inode, this, &block_size);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_CTX_GET_FAILED,
               "Failed to get block size from inode ctx of %s",
               uuid_utoa(fd->inode->gfid));
        goto err;
    }

    if (!block_size || frame->root->pid == GF_CLIENT_PID_GSYNCD) {
        STACK_WIND(frame, default_fsetattr_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->fsetattr, fd, stbuf, valid, xdata);
        return 0;
    }

    if (!this->itable)
        this->itable = fd->inode->table;

    local = mem_get0(this->local_pool);
    if (!local)
        goto err;

    frame->local = local;

    local->handler = shard_post_setattr_handler;
    local->xattr_req = (xdata) ? dict_ref(xdata) : dict_new();
    if (!local->xattr_req)
        goto err;
    local->fop = GF_FOP_FSETATTR;
    local->fd = fd_ref(fd);

    SHARD_MD_READ_FOP_INIT_REQ_DICT(this, local->xattr_req, fd->inode->gfid,
                                    local, err);

    STACK_WIND(frame, shard_common_setattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fsetattr, fd, stbuf, valid,
               local->xattr_req);
    return 0;
err:
    shard_common_failure_unwind(GF_FOP_FSETATTR, frame, -1, ENOMEM);
    return 0;
}

int
shard_common_inode_write_begin(call_frame_t *frame, xlator_t *this,
                               glusterfs_fop_t fop, fd_t *fd,
                               struct iovec *vector, int32_t count,
                               off_t offset, uint32_t flags, size_t len,
                               struct iobref *iobref, dict_t *xdata)
{
    int ret = 0;
    int i = 0;
    uint64_t block_size = 0;
    shard_local_t *local = NULL;

    ret = shard_inode_ctx_get_block_size(fd->inode, this, &block_size);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INODE_CTX_GET_FAILED,
               "Failed to get block "
               "size for %s from its inode ctx",
               uuid_utoa(fd->inode->gfid));
        goto out;
    }

    if (!block_size || frame->root->pid == GF_CLIENT_PID_GSYNCD) {
        /* block_size = 0 means that the file was created before
         * sharding was enabled on the volume.
         */
        switch (fop) {
            case GF_FOP_WRITE:
                STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                                FIRST_CHILD(this)->fops->writev, fd, vector,
                                count, offset, flags, iobref, xdata);
                break;
            case GF_FOP_FALLOCATE:
                STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                                FIRST_CHILD(this)->fops->fallocate, fd, flags,
                                offset, len, xdata);
                break;
            case GF_FOP_ZEROFILL:
                STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                                FIRST_CHILD(this)->fops->zerofill, fd, offset,
                                len, xdata);
                break;
            case GF_FOP_DISCARD:
                STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                                FIRST_CHILD(this)->fops->discard, fd, offset,
                                len, xdata);
                break;
            default:
                gf_msg(this->name, GF_LOG_WARNING, 0, SHARD_MSG_INVALID_FOP,
                       "Invalid fop id = %d", fop);
                break;
        }
        return 0;
    }

    if (!this->itable)
        this->itable = fd->inode->table;

    local = mem_get0(this->local_pool);
    if (!local)
        goto out;

    frame->local = local;

    ret = syncbarrier_init(&local->barrier);
    if (ret)
        goto out;
    local->xattr_req = (xdata) ? dict_ref(xdata) : dict_new();
    if (!local->xattr_req)
        goto out;

    if (vector) {
        local->vector = iov_dup(vector, count);
        if (!local->vector)
            goto out;
        for (i = 0; i < count; i++)
            local->total_size += vector[i].iov_len;
        local->count = count;
    } else {
        local->total_size = len;
    }

    local->fop = fop;
    local->offset = offset;
    local->flags = flags;
    if (iobref)
        local->iobref = iobref_ref(iobref);
    local->fd = fd_ref(fd);
    local->block_size = block_size;
    local->resolver_base_inode = local->fd->inode;
    GF_ATOMIC_INIT(local->delta_blocks, 0);

    local->loc.inode = inode_ref(fd->inode);
    gf_uuid_copy(local->loc.gfid, fd->inode->gfid);

    shard_refresh_base_file(frame, this, NULL, fd,
                            shard_common_inode_write_post_lookup_handler);
    return 0;
out:
    shard_common_failure_unwind(fop, frame, -1, ENOMEM);
    return 0;
}

int
shard_writev(call_frame_t *frame, xlator_t *this, fd_t *fd,
             struct iovec *vector, int32_t count, off_t offset, uint32_t flags,
             struct iobref *iobref, dict_t *xdata)
{
    shard_common_inode_write_begin(frame, this, GF_FOP_WRITE, fd, vector, count,
                                   offset, flags, 0, iobref, xdata);
    return 0;
}

int
shard_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd,
                int32_t keep_size, off_t offset, size_t len, dict_t *xdata)
{
    if ((keep_size != 0) && (keep_size != FALLOC_FL_ZERO_RANGE) &&
        (keep_size != (FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE)))
        goto out;

    shard_common_inode_write_begin(frame, this, GF_FOP_FALLOCATE, fd, NULL, 0,
                                   offset, keep_size, len, NULL, xdata);
    return 0;
out:
    shard_common_failure_unwind(GF_FOP_FALLOCATE, frame, -1, ENOTSUP);
    return 0;
}

int
shard_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
               off_t len, dict_t *xdata)
{
    shard_common_inode_write_begin(frame, this, GF_FOP_ZEROFILL, fd, NULL, 0,
                                   offset, 0, len, NULL, xdata);
    return 0;
}

int
shard_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              size_t len, dict_t *xdata)
{
    shard_common_inode_write_begin(frame, this, GF_FOP_DISCARD, fd, NULL, 0,
                                   offset, 0, len, NULL, xdata);
    return 0;
}

int32_t
shard_seek(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
           gf_seek_what_t what, dict_t *xdata)
{
    /* TBD */
    gf_msg(this->name, GF_LOG_INFO, ENOTSUP, SHARD_MSG_FOP_NOT_SUPPORTED,
           "seek called on %s.", uuid_utoa(fd->inode->gfid));
    shard_common_failure_unwind(GF_FOP_SEEK, frame, -1, ENOTSUP);
    return 0;
}

int32_t
mem_acct_init(xlator_t *this)
{
    int ret = -1;

    if (!this)
        return ret;

    ret = xlator_mem_acct_init(this, gf_shard_mt_end + 1);

    if (ret != 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_MEM_ACCT_INIT_FAILED,
               "Memory accounting init"
               "failed");
        return ret;
    }

    return ret;
}

int
init(xlator_t *this)
{
    int ret = -1;
    shard_priv_t *priv = NULL;

    if (!this) {
        gf_msg("shard", GF_LOG_ERROR, 0, SHARD_MSG_NULL_THIS,
               "this is NULL. init() failed");
        return -1;
    }

    if (!this->parents) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INVALID_VOLFILE,
               "Dangling volume. Check volfile");
        goto out;
    }

    if (!this->children || this->children->next) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SHARD_MSG_INVALID_VOLFILE,
               "shard not configured with exactly one sub-volume. "
               "Check volfile");
        goto out;
    }

    priv = GF_CALLOC(1, sizeof(shard_priv_t), gf_shard_mt_priv_t);
    if (!priv)
        goto out;

    GF_OPTION_INIT("shard-block-size", priv->block_size, size_uint64, out);

    GF_OPTION_INIT("shard-deletion-rate", priv->deletion_rate, uint32, out);

    GF_OPTION_INIT("shard-lru-limit", priv->lru_limit, uint64, out);

    this->local_pool = mem_pool_new(shard_local_t, 128);
    if (!this->local_pool) {
        ret = -1;
        goto out;
    }
    gf_uuid_parse(SHARD_ROOT_GFID, priv->dot_shard_gfid);
    gf_uuid_parse(DOT_SHARD_REMOVE_ME_GFID, priv->dot_shard_rm_gfid);

    this->private = priv;
    LOCK_INIT(&priv->lock);
    INIT_LIST_HEAD(&priv->ilist_head);
    ret = 0;
out:
    if (ret) {
        GF_FREE(priv);
        mem_pool_destroy(this->local_pool);
    }

    return ret;
}

void
fini(xlator_t *this)
{
    shard_priv_t *priv = NULL;

    GF_VALIDATE_OR_GOTO("shard", this, out);

    /*Itable was not created by shard, hence setting to NULL.*/
    this->itable = NULL;

    mem_pool_destroy(this->local_pool);
    this->local_pool = NULL;

    priv = this->private;
    if (!priv)
        goto out;

    this->private = NULL;
    LOCK_DESTROY(&priv->lock);
    GF_FREE(priv);

out:
    return;
}

int
reconfigure(xlator_t *this, dict_t *options)
{
    int ret = -1;
    shard_priv_t *priv = NULL;

    priv = this->private;

    GF_OPTION_RECONF("shard-block-size", priv->block_size, options, size, out);

    GF_OPTION_RECONF("shard-deletion-rate", priv->deletion_rate, options,
                     uint32, out);
    ret = 0;

out:
    return ret;
}

int
shard_forget(xlator_t *this, inode_t *inode)
{
    uint64_t ctx_uint = 0;
    shard_inode_ctx_t *ctx = NULL;
    shard_priv_t *priv = NULL;

    priv = this->private;
    if (!priv)
        return 0;

    inode_ctx_del(inode, this, &ctx_uint);
    if (!ctx_uint)
        return 0;

    ctx = (shard_inode_ctx_t *)(uintptr_t)ctx_uint;

    /* When LRU limit reaches inode will be forcefully removed from the
     * table, inode needs to be removed from LRU of shard as well.
     */
    if (!list_empty(&ctx->ilist)) {
        LOCK(&priv->lock);
        {
            list_del_init(&ctx->ilist);
            priv->inode_count--;
        }
        UNLOCK(&priv->lock);
    }
    GF_FREE(ctx);

    return 0;
}

int
shard_release(xlator_t *this, fd_t *fd)
{
    /* TBD */
    return 0;
}

int
shard_priv_dump(xlator_t *this)
{
    shard_priv_t *priv = NULL;
    char key_prefix[GF_DUMP_MAX_BUF_LEN] = {
        0,
    };
    char *str = NULL;

    priv = this->private;

    snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s", this->type, this->name);
    gf_proc_dump_add_section("%s", key_prefix);
    str = gf_uint64_2human_readable(priv->block_size);
    gf_proc_dump_write("shard-block-size", "%s", str);
    gf_proc_dump_write("inode-count", "%d", priv->inode_count);
    gf_proc_dump_write("ilist_head", "%p", &priv->ilist_head);
    gf_proc_dump_write("lru-max-limit", "%" PRIu64, priv->lru_limit);

    GF_FREE(str);

    return 0;
}

int
shard_releasedir(xlator_t *this, fd_t *fd)
{
    return 0;
}

struct xlator_fops fops = {
    .lookup = shard_lookup,
    .open = shard_open,
    .flush = shard_flush,
    .fsync = shard_fsync,
    .stat = shard_stat,
    .fstat = shard_fstat,
    .getxattr = shard_getxattr,
    .fgetxattr = shard_fgetxattr,
    .readv = shard_readv,
    .writev = shard_writev,
    .truncate = shard_truncate,
    .ftruncate = shard_ftruncate,
    .setxattr = shard_setxattr,
    .fsetxattr = shard_fsetxattr,
    .setattr = shard_setattr,
    .fsetattr = shard_fsetattr,
    .removexattr = shard_removexattr,
    .fremovexattr = shard_fremovexattr,
    .fallocate = shard_fallocate,
    .discard = shard_discard,
    .zerofill = shard_zerofill,
    .readdir = shard_readdir,
    .readdirp = shard_readdirp,
    .create = shard_create,
    .mknod = shard_mknod,
    .link = shard_link,
    .unlink = shard_unlink,
    .rename = shard_rename,
    .seek = shard_seek,
};

struct xlator_cbks cbks = {
    .forget = shard_forget,
    .release = shard_release,
    .releasedir = shard_releasedir,
};

struct xlator_dumpops dumpops = {
    .priv = shard_priv_dump,
};

struct volume_options options[] = {
    {
        .key = {"shard"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "off",
        .description = "enable/disable shard",
        .op_version = {GD_OP_VERSION_6_0},
        .flags = OPT_FLAG_SETTABLE,
    },
    {
        .key = {"shard-block-size"},
        .type = GF_OPTION_TYPE_SIZET,
        .op_version = {GD_OP_VERSION_3_7_0},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        .tags = {"shard"},
        .default_value = "64MB",
        .min = SHARD_MIN_BLOCK_SIZE,
        .max = SHARD_MAX_BLOCK_SIZE,
        .description = "The size unit used to break a file into multiple "
                       "chunks",
    },
    {
        .key = {"shard-deletion-rate"},
        .type = GF_OPTION_TYPE_INT,
        .op_version = {GD_OP_VERSION_5_0},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        .tags = {"shard"},
        .default_value = "100",
        .min = 100,
        .max = INT_MAX,
        .description = "The number of shards to send deletes on at a time",
    },
    {
        .key = {"shard-lru-limit"},
        .type = GF_OPTION_TYPE_INT,
        .op_version = {GD_OP_VERSION_5_0},
        .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT,
        .tags = {"shard"},
        .default_value = "16384",
        .min = 20,
        .max = INT_MAX,
        .description = "The number of resolved shard inodes to keep in "
                       "memory. A higher number means shards that are "
                       "resolved will remain in memory longer, avoiding "
                       "frequent lookups on them when they participate in "
                       "file operations. The option also has a bearing on "
                       "amount of memory consumed by these inodes and their "
                       "internal metadata",
    },
    {.key = {NULL}},
};

xlator_api_t xlator_api = {
    .init = init,
    .fini = fini,
    .reconfigure = reconfigure,
    .mem_acct_init = mem_acct_init,
    .op_version = {1}, /* Present from the initial version */
    .dumpops = &dumpops,
    .fops = &fops,
    .cbks = &cbks,
    .options = options,
    .identifier = "shard",
    .category = GF_MAINTAINED,
};
