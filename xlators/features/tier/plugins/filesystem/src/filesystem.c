/*
 Copyright (c) 2021 Pavilion Data Systems, Inc. <https://pavilion.io>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
*/

#include <stdlib.h>
#include <glusterfs/xlator.h>
#include <glusterfs/syscall.h>
#include <glusterfs/glusterfs.h>
#include "filesystem.h"
#include "filesystem-messages.h"

static char *plugin = "vfs_tier";

/* Default size of block is 1MB */
#define BITMAP_BLOCK_SIZE (1 * GF_UNIT_MB)
#define TIER_REMOTE_FILE "user.tier.remote-file"

static int
get_local_block_count(uint8_t *bitmap, size_t bmsize)
{
    int count = 0;
    int i;

    for (i = 0; i < bmsize; i++) {
        uint8_t byte = bitmap[i];
        int bit;
        for (bit = 0; bit < 7; bit++) {
            if (byte & (1 << bit))
                count++;
        }
    }
    return count;
}

static tierfs_inode_ctx_t *
get_inode_tierfs_ctx(inode_t *inode, tierfs_t *priv, char *remote_path,
                     tier_local_t *local, bool create_ctx, bool bmcreate,
                     bool full_download)
{
    uint64_t tmp_ctx = 0;
    tierfs_inode_ctx_t *ctx = NULL;
    xlator_t *this = priv->xl;
    tier_private_t *tierpriv = this->private;
    char src_path[PATH_MAX] = "";
    bool add_to_dl_list = false;
    size_t bmsize = 0;
    int ret = 0;

    if (!inode)
        return NULL;

    LOCK(&inode->lock);
    {
        __inode_ctx_get1(inode, this, &tmp_ctx);
        if (tmp_ctx) {
            ctx = (tierfs_inode_ctx_t *)(uintptr_t)tmp_ctx;
            /* context already present, no need to disturb */
            create_ctx = false;
            goto unlock;
        }
        if (!create_ctx)
            goto unlock;

        ctx = GF_CALLOC(1, sizeof(tierfs_inode_ctx_t), gf_common_mt_inode_ctx);
        if (!ctx) {
            goto unlock;
        }
        tmp_ctx = (uint64_t)(uintptr_t)ctx;
        __inode_ctx_set1(inode, this, &tmp_ctx);
        create_ctx = true;
        ctx->bmfd = -1;
        LOCK_INIT(&ctx->bmlock);
        /* set it to 1 here, and when it needs to be deleted, it will be
         * unrefed extra */
        GF_ATOMIC_INIT(ctx->ref, 1);
    }
unlock:
    UNLOCK(&inode->lock);
    if (!ctx) {
        return NULL;
    }

    if (!create_ctx && ctx->bitmap)
        goto out;

    /* This lock is large, but is specific to one particular
       inode, so it should be isolated */
    LOCK(&ctx->bmlock);
    {
        if (!remote_path) {
            gf_msg(plugin, GF_LOG_INFO, 0, 0, "remote-path is null");
            goto unlock2;
        }

        if (create_ctx) {
            snprintf(src_path, PATH_MAX, "%s/%s", priv->mount_point,
                     remote_path);
            ctx->tierfd = sys_open(src_path, O_RDONLY, 0);
            if (ctx->tierfd < 0) {
                gf_msg(plugin, GF_LOG_ERROR, errno, 0,
                       "%s: failed to open file on cold tier", src_path);
                ret = -1;
                goto unlock2;
            }
            /* required for delete like operations */
            ctx->remotepath = gf_strdup(remote_path);
            if (!ctx->remotepath) {
                gf_msg(plugin, GF_LOG_ERROR, ENOMEM, 0,
                       "%s: failed to set remote path", src_path);
                sys_close(ctx->tierfd);
                ret = -1;
                goto unlock2;
            }
        }

        /* for all write() ops, local will be set */
        if (!local) {
            gf_msg(plugin, GF_LOG_INFO, 0, 0, "%s: local is null", remote_path);
            goto unlock2;
        }

        if (ctx->bitmap)
            goto unlock2;

        char bmfile[PATH_MAX] = "";
        int open_flags = O_RDWR | O_CREAT;
        bool bmfileexists = false;

        if (!bmsize) {
            if (!priv->block_size) {
                priv->block_size = BITMAP_BLOCK_SIZE;
            }
            bmsize = (local->ia_size / (8 * priv->block_size)) + 1;
        }

        ret = snprintf(bmfile, PATH_MAX - 1, "%s/%s", tierpriv->tierdir,
                       uuid_utoa(inode->gfid));
        if (ret < 0) {
            gf_msg(plugin, GF_LOG_ERROR, errno, 0,
                   "failed build bitmap file path %d", ret);
        }
        if (!ctx->bmfile_check_done) {
            struct stat buf = {
                0,
            };

            ret = sys_stat(bmfile, &buf);
            if (ret >= 0) {
                /* in this case, we need to allocate bitmap */
                bmsize = buf.st_size;
                bmfileexists = true;
                /* regardless of the function argument, continue with bitmap
                 * create
                 */
                bmcreate = true;
                /* This case deserves a log */
                gf_msg(plugin, GF_LOG_DEBUG, 0, 0,
                       "%s: bitmap file exists (%zu)", remote_path, bmsize);
                open_flags = O_RDWR;
            }
            ctx->bmfile_check_done = true;
        }

        ret = 0;

        /* this optimizes performance for read only operations */
        if (!bmcreate)
            goto unlock2;

        ctx->bitmap = GF_CALLOC(1, bmsize, gf_common_mt_char);
        if (!ctx->bitmap) {
            gf_msg(plugin, GF_LOG_ERROR, ENOMEM, 0, "%s: memory problem",
                   remote_path);
            goto unlock2;
        }

        ctx->bm_inprogress = GF_CALLOC(1, bmsize, gf_common_mt_char);
        if (!ctx->bm_inprogress) {
            gf_msg(plugin, GF_LOG_ERROR, ENOMEM, 0, "%s: memory problem",
                   remote_path);
            GF_FREE(ctx->bitmap);
            ctx->bitmap = NULL;
            goto unlock2;
        }

        ctx->bmfd = sys_open(bmfile, open_flags, 0);
        if (ctx->bmfd < 0) {
            gf_msg(plugin, GF_LOG_ERROR, errno, 0,
                   "%s: bitmap file (%s) failed to open", remote_path, bmfile);
            GF_FREE(ctx->bm_inprogress);
            GF_FREE(ctx->bitmap);
            ctx->bitmap = NULL;
            goto unlock2;
        }

        if (bmfileexists) {
            ret = sys_read(ctx->bmfd, ctx->bitmap, bmsize);
            if (ret <= 0) {
                gf_msg(plugin, GF_LOG_WARNING, errno, 0,
                       "%s: failed to read bitmap file. Retry! (%d)",
                       remote_path, ret);
                /* treat this as error? In this case, one needs to retry the
                 * read operations */
                GF_FREE(ctx->bm_inprogress);
                GF_FREE(ctx->bitmap);
                ctx->bmfile_check_done = false;
                ctx->bitmap = NULL;
                goto unlock2;
            }
            ctx->block_count = get_local_block_count(ctx->bitmap, bmsize);
        } else {
            /* this is ideally not required, but adding for a proper
               reference to actual cold file */
            ret = sys_fsetxattr(ctx->bmfd, TIER_REMOTE_FILE, remote_path,
                                strlen(remote_path), 0);
            if (ret < 0) {
                gf_msg(plugin, GF_LOG_ERROR, errno, 0,
                       "%s: fsetxattr failed on bitmap file", remote_path);
                GF_FREE(ctx->bm_inprogress);
                GF_FREE(ctx->bitmap);
                ctx->bitmap = NULL;
                goto unlock2;
            }
        }

        ret = 0;

        /* Lets open a file for doing the local write. This is tied to the fact,
         * it needs a bitmap with it */
        fd_t *local_fd = fd_create(inode, 0);
        if (!local_fd) {
            gf_msg(plugin, GF_LOG_ERROR, ENOMEM, 0,
                   "%s: local file opening failed", remote_path);
            goto unlock2;
        }

        loc_t tmp_loc = {
            0,
        };
        gf_uuid_copy(tmp_loc.gfid, inode->gfid);
        int dstfd = syncop_open(FIRST_CHILD(this), &tmp_loc, O_RDWR, local_fd,
                                NULL, NULL);
        if (dstfd < 0) {
            gf_msg(plugin, GF_LOG_ERROR, -dstfd, 0, "%s: syncop_open failed",
                   remote_path);
            goto unlock2;
        }

        gf_uuid_copy(ctx->gfid, inode->gfid);
        ctx->localfd = fd_ref(local_fd);
        ctx->bmsize = bmsize;
        ctx->full_download = full_download;
        ctx->filesize = local->ia_size;
        if (!full_download)
            add_to_dl_list = true;
        ret = 0;
    }
unlock2:
    UNLOCK(&ctx->bmlock);

    if (ret) {
        if (create_ctx) {
            /* In this case, we created context in this call, so good to delete
             * it too */
            /* Else, it will get deleted during inode_forget() */
            inode_ctx_del1(inode, this, &tmp_ctx);
            GF_FREE(ctx);
        }
        ctx = NULL;
        goto out;
    }

    gf_msg_callingfn(plugin, GF_LOG_TRACE, 0, 0, "%s: ref (%ld)", remote_path,
                     GF_ATOMIC_GET(ctx->ref));
    INIT_LIST_HEAD(&ctx->active_list);
    if (add_to_dl_list) {
        LOCK(&priv->lock);
        {
            list_add_tail(&ctx->active_list, &priv->dl_list);
        }
        UNLOCK(&priv->lock);
    }
    /* This means, there is a bitmap file created. Need a fsync on parent
     * dir */
    if (bmsize) {
        ret = sys_fsync(priv->bmdirfd);
        if (ret) {
            gf_msg(plugin, GF_LOG_WARNING, errno, 0,
                   "bitmap dir fsync() failed");
        }
    }

out:
    return ctx;
}

/* reset_parent_inodectx : Useful when the plugin runs threads which are not
 * controlled by parent to reset inode ctx */
static void
inode_clear_tierfs_ctx(tierfs_inode_ctx_t *curr_ctx, inode_t *inode,
                       xlator_t *this, bool remove_parent)
{
    tier_private_t *tierpriv = this->private;
    tierfs_inode_ctx_t *ctx = curr_ctx;
    uint64_t tmp_ctx = 0;
    uint64_t tmp_ctx1 = 0;
    tier_inode_ctx_t *ctx0 = NULL;

    if (remove_parent && !inode)
        return;

    if (!remove_parent)
        goto no_parent_removal;

    inode_ctx_del2(inode, this, &tmp_ctx, &tmp_ctx1);
    if (!tmp_ctx) {
        goto reset_tierfs_ctx;
    }

    ctx0 = (tier_inode_ctx_t *)(uintptr_t)tmp_ctx;

    GF_FREE(ctx0->remote_path);
    GF_FREE(ctx0);

reset_tierfs_ctx:
    if (!tmp_ctx1) {
        return;
    }

    ctx = (tierfs_inode_ctx_t *)(uintptr_t)tmp_ctx1;

no_parent_removal:
    if (ctx->bmfd >= 0) {
        /* unlink bmfd */
        char bmfile[PATH_MAX] = "";
        int ret = snprintf(bmfile, PATH_MAX - 1, "%s/%s", tierpriv->tierdir,
                           uuid_utoa(ctx->gfid));
        if (ret < 0) {
            gf_msg(plugin, GF_LOG_WARNING, errno, 0,
                   "failed build bitmap file path %d", ret);
        }
        ret = sys_unlink(bmfile);
        if (ret) {
            gf_msg(plugin, GF_LOG_ERROR, errno, 0,
                   "%s: unlinking of bitmap file failed", uuid_utoa(ctx->gfid));
            /* Continuing here, as no operations are blocked at this
             * time */
            // Discuss the need for below
            // ret = sys_fallocate(ctx->bmfd,
            //                     (FALLOC_FL_PUNCH_HOLE |
            //                     FALLOC_FL_KEEP_SIZE), 0, 4 * 1024 *
            //                     1024);
        }

        sys_close(ctx->bmfd);
        ctx->bmfd = -2;
    }
    if (ctx->localfd)
        fd_unref(ctx->localfd);
    if (ctx->tierfd)
        sys_close(ctx->tierfd);

    LOCK_DESTROY(&ctx->bmlock);

    GF_FREE(ctx->remotepath);
    GF_FREE(ctx->bm_inprogress);
    GF_FREE(ctx->bitmap);
    GF_FREE(ctx);

    return;
}

void
tierfs_inode_unref(tierfs_inode_ctx_t *ctx, inode_t *inode, xlator_t *this)
{
    int64_t ref = GF_ATOMIC_DEC(ctx->ref);
    gf_msg_callingfn(plugin, GF_LOG_TRACE, 0, 0, "%s: ref (%ld)",
                     ctx->remotepath, ref);
    if (!ref) {
        gf_msg_callingfn(plugin, GF_LOG_DEBUG, 0, 0, "%s: ref==0",
                         ctx->remotepath);
        inode_clear_tierfs_ctx(ctx, inode, this, true);
    }
    if (ref < 0) {
        gf_msg(plugin, GF_LOG_ERROR, 0, 0,
               "%s: ref is negative, should have not happened (%ld)",
               ctx->remotepath, ref);
    }
}

/* fremovexattr() to remove all traces of a file's remote status */
static int
tierfs_clear_local_file_xattr(xlator_t *this, fd_t *fd)
{
    int ret = -1;
    dict_t *tmp_xdata = dict_new();
    if (!tmp_xdata) {
        goto err;
    }
    ret = dict_set_sizen_str_sizen(tmp_xdata, GF_CS_OBJECT_REMOTE, "0");
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
               "%s: failed to set key CS_OBJECT_REMOTE in dict",
               uuid_utoa(fd->inode->gfid));
    }

    ret = dict_set_sizen_str_sizen(tmp_xdata, GF_CS_OBJECT_SIZE, "0");
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
               "%s: failed to set key CS_OBJECT_SIZE in dict",
               uuid_utoa(fd->inode->gfid));
    }

    ret = dict_set_sizen_str_sizen(tmp_xdata, GF_CS_NUM_BLOCKS, "0");
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
               "%s: failed to set key CS_OBJECT_NUM_BLOCKS in dict",
               uuid_utoa(fd->inode->gfid));
    }

    ret = syncop_fremovexattr(this, fd, "", tmp_xdata, NULL);
    if (ret) {
        gf_msg(this->name, GF_LOG_DEBUG, -ret, 0,
               "%s: failed to mark file local (after migration)",
               uuid_utoa(fd->inode->gfid));
        ret = syncop_fremovexattr(this, fd, GF_CS_OBJECT_REMOTE, tmp_xdata,
                                  NULL);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, -ret, 0,
                   "%s: failed to mark file local", uuid_utoa(fd->inode->gfid));
        }
    }
    dict_unref(tmp_xdata);

err:
    return ret;
}

static inline bool
is_block_local(tierfs_inode_ctx_t *ctx, int bm_idx, int shift, bool is_locked)
{
    /* if bitmap is not present, then it means, the file is only used for
     * 'reading' */
    if (!ctx->bitmap)
        return false;

    bool is_local = false;
    uint8_t *bitmap = ctx->bitmap;

    if (!is_locked)
        LOCK(&ctx->bmlock);
    {
        is_local = (bitmap[bm_idx] && (bitmap[bm_idx] & (1 << shift)));
    }
    if (!is_locked)
        UNLOCK(&ctx->bmlock);

    return is_local;
}

static inline gf_tier_obj_state
update_data_and_bitmap(xlator_t *this, tierfs_inode_ctx_t *ctx, int bm_idx,
                       int shift, char *data_buf, uint64_t data_size,
                       off_t offset, struct iobref *iobref, int flags,
                       dict_t *xdata)
{
    int ret = 0;

    LOCK(&ctx->bmlock);
    {
        bool is_progress = (ctx->bm_inprogress[bm_idx] &&
                            (ctx->bm_inprogress[bm_idx] & (1 << shift)));
        if (is_progress) {
            /* If migration is in progress, it means same block is being read.
               we can skip write to local file */
            UNLOCK(&ctx->bmlock);
            goto out;
        }
        ctx->bm_inprogress[bm_idx] = (ctx->bm_inprogress[bm_idx] |
                                      (1 << shift));

        if (is_block_local(ctx, bm_idx, shift, true)) {
            /* This check ensures we are not overwriting a block which
               is locally modified */
            /* Also required as we are doing IO outside of lock */
            gf_msg(plugin, GF_LOG_INFO, 0, 0,
                   "block is marked locally modified %d %d", bm_idx, shift);
            UNLOCK(&ctx->bmlock);
            goto out;
        }
    }
    UNLOCK(&ctx->bmlock);

    /* This is the only place for write */
    ret = syncop_write(FIRST_CHILD(this), ctx->localfd, data_buf, data_size,
                       offset, iobref, flags, xdata, NULL);
    if (ret < 0) {
        gf_msg(plugin, GF_LOG_ERROR, errno, 0,
               "failed to update local file (%s) [%lu - %lu]",
               uuid_utoa(ctx->gfid), offset, data_size);
        goto err;
    }

    LOCK(&ctx->bmlock);
    {
        ctx->bitmap[bm_idx] = (ctx->bitmap[bm_idx] | (1 << shift));
        ctx->block_count++;
        /* always write at offset 0, as we write complete bitmap data everytime.
         */
        ret = sys_pwrite(ctx->bmfd, ctx->bitmap, ctx->bmsize, 0);
    }
    UNLOCK(&ctx->bmlock);
    if (ret < 0) {
        gf_msg(plugin, GF_LOG_ERROR, errno, 0,
               "%s: failed to update bitmap file write(%d) %lu %d %d",
               uuid_utoa(ctx->gfid), ctx->bmfd, ctx->bmsize, bm_idx, shift);
        goto err;
    }
out:
    return GF_TIER_LOCAL;

err:
    return GF_TIER_ERROR;
}

// keep counters for all the operations on remote fd, so we can take future
// decisions on it.
//   -> Currently added counters in the tier xlator itself for read fops, so
//   users can fetch it with virtual xattr.

int
copy_the_data(xlator_t *this, tierfs_inode_ctx_t *ctx, off_t limit,
              uint64_t block_size)
{
    gf_tier_obj_state state = GF_TIER_ERROR;
    char *data_buf = NULL;
    fd_t *local_fd = NULL;
    ssize_t read_size = 0;
    off_t offset = 0;
    int ret = -1;

    dict_t *xdata = dict_new();
    if (!xdata) {
        gf_msg(plugin, GF_LOG_ERROR, ENOMEM, 0, "failed to allocate memory");
        goto out;
    }
    ret = dict_set_str_sizen(xdata, GLUSTERFS_INTERNAL_FOP_KEY, "1");
    if (ret) {
        /* If this fails, the error would be ENOSPC even though there
           is space due to 'storage.reserve' option */
        gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
               "%s: failed to set INTERNAL_FOP_KEY in dict", ctx->remotepath);
    }

    data_buf = GF_MALLOC(block_size, gf_common_mt_char);
    if (!data_buf)
        goto out;

    local_fd = fd_ref(ctx->localfd);

    if (!limit)
        limit = ctx->filesize;

    while (true) {
        if (limit && (limit <= offset)) {
            /* This case is mostly useful when we are downloading only partial
               file as part of truncate or similar ops */
            gf_msg_debug(plugin, 0, "breaking filesize(%lu) <= (%lu)offset",
                         limit, offset);
            break;
        }

        /* If the block is not local, we need to read from the cold tier, and
         * update bitmap */
        int bm_array_idx = offset / (8 * block_size);
        int shift = (offset / block_size) % 8;

        if (is_block_local(ctx, bm_array_idx, shift, false)) {
            /* perform the write call */
            offset += block_size;
            continue;
        }
        read_size = sys_pread(ctx->tierfd, data_buf, block_size, offset);
        if (read_size < 0) {
            gf_msg(plugin, GF_LOG_ERROR, 0, 0,
                   "%s: read from remote location failed (%s)",
                   uuid_utoa(ctx->gfid), strerror(errno));
            goto out;
        }
        if (!read_size) {
            break;
        }
        state = update_data_and_bitmap(this, ctx, bm_array_idx, shift, data_buf,
                                       read_size, offset, NULL, 0, xdata);
        if (state != GF_TIER_LOCAL) {
            ret = -1;
            gf_msg(plugin, GF_LOG_ERROR, 0, 0,
                   "%s: failed to update the bitmap", uuid_utoa(ctx->gfid));
            goto out;
        }
        offset += min(block_size, read_size);
    }
    ret = syncop_fsync(FIRST_CHILD(this), local_fd, 0, NULL, NULL, NULL, NULL);
    if (ret) {
        ret = -1;
        gf_msg(plugin, GF_LOG_ERROR, 0, 0, "%s: failed to fsync the local copy",
               uuid_utoa(ctx->gfid));
        goto out;
    }

out:
    GF_FREE(data_buf);
    if (local_fd)
        fd_unref(local_fd);
    if (xdata)
        dict_unref(xdata);

    return ret;
}

static int
download_one_file(xlator_t *this, tierfs_t *priv, tierfs_inode_ctx_t *ctx)
{
    /* */
    /* Steps to be taken:
       1. Start with offset 0.
       2. Check if the block is local in bitmap.
       3. Download/Read the block from cold tier.
       4. Under lock: { Check the bitmap again, write to local file, update the
       bitmap }
       5. repeat 1-4 till our offset is above 'size'.
      */
    if (ctx->full_download) {
        /* no need to 'unref' */
        return 1;
    }
    gf_msg_debug(plugin, 0, "picking up migration of file (%s)",
                 ctx->remotepath);

    /* add a ref here, unref at the end of this function */
    GF_ATOMIC_INC(ctx->ref);

    int op_ret = copy_the_data(this, ctx, 0, priv->block_size);
    /*
     * Submit the restore request.
     */
    if (op_ret < 0) {
        gf_msg(plugin, GF_LOG_ERROR, 0, TIERFS_RESTORE_FAILED,
               " failed to restore file gfid=%s from data management store",
               uuid_utoa(ctx->gfid));
        return -1;
    }

    char src_path[PATH_MAX];
    snprintf(src_path, PATH_MAX, "%s/%s", priv->mount_point, ctx->remotepath);

    /* download is now finished, call fremovexattr() */
    op_ret = tierfs_clear_local_file_xattr(this, ctx->localfd);
    if (!op_ret) {
        /* remove the context on success */
        /* ref -> unref are managed between
           open() and release() of the application */
        /* this one is for 'unlink' part, should be matched for create_ctx get
         */
        tierfs_inode_unref(ctx, ctx->localfd->inode, this);

        /* remove the cold file, after all the things are sorted out */
        op_ret = sys_unlink(src_path);
        if (op_ret) {
            gf_msg(plugin, GF_LOG_ERROR, errno, TIERFS_RESTORE_FAILED,
                   "failed to remove file(%s) in remote tier", src_path);
            goto out;
        }
    }

out:
    return 0;
}

static void
finish_all_active_downloads(xlator_t *this, tierfs_t *priv)
{
    tierfs_inode_ctx_t *tmp;
    tierfs_inode_ctx_t *tmp2;
    struct list_head migrate = {
        0,
    };

    INIT_LIST_HEAD(&migrate);

    LOCK(&priv->lock);
    {
        if (list_empty(&priv->dl_list)) {
            UNLOCK(&priv->lock);
            return;
        }
        list_splice_init(&priv->dl_list, &migrate);
    }
    UNLOCK(&priv->lock);

    list_for_each_entry_safe(tmp, tmp2, &migrate, active_list)
    {
        /* if the file is less than trigger threshold blocks, or if already more
         * than required threshold */
        if (!((tmp->block_count > priv->dl_trigger_threshold) ||
              (tmp->bmsize < (priv->dl_trigger_threshold / 8)))) {
            continue;
        }

        gf_msg(plugin, GF_LOG_INFO, 0, 0, "Migrating %s from COLD (ref = %ld)",
               uuid_utoa(tmp->gfid), GF_ATOMIC_GET(tmp->ref));
        list_del_init(&tmp->active_list);
        download_one_file(this, priv, tmp);
    }
    return;
}

static void *
migrate_thread_proc(void *data)
{
    /* FIXME: decide on the interval, provide option */
    tierfs_t *priv = data;
    uint32_t interval = 3;
    int ret = -1;
    xlator_t *this = priv->xl;

    while (true) {
        /* Start checking the 'in migration' files, and complete the migration
         */
        finish_all_active_downloads(this, priv);

        /* aborting sleep() is a request to exit this thread, sleep()
         * will normally not return when cancelled */
        ret = sleep(interval);
        if (ret > 0)
            break;
    }

    return NULL;
}

static int
tierfs_set_thread(xlator_t *xl, tierfs_t *priv)
{
    int ret = -1;

    ret = gf_thread_create(&priv->download_thr, NULL, migrate_thread_proc, priv,
                           "tierfs");
    if (ret) {
        gf_msg(plugin, GF_LOG_ERROR, errno, 0,
               "unable to setup tier download thread");
    }

    return ret;
}

static int
tierfs_clear_thread(xlator_t *this, pthread_t thr_id)
{
    void *retval = NULL;
    int ret;

    gf_log(this->name, GF_LOG_DEBUG, "clearing thread");

    /* send a cancel request to the thread */
    ret = pthread_cancel(thr_id);
    if (ret != 0) {
        gf_msg(plugin, GF_LOG_ERROR, errno, 0, "pthread_cancel() failed %s",
               strerror(errno));
        goto out;
    }

    errno = 0;
    ret = pthread_join(thr_id, &retval);
    if ((ret != 0) || (retval != PTHREAD_CANCELED)) {
        gf_msg(plugin, GF_LOG_ERROR, errno, 0, "pthread_join() failed %s",
               strerror(errno));
    }

out:
    return ret;
}

void *
filesystem_init(xlator_t *this, dict_t *options)
{
    char *mount_point = NULL;
    tierfs_t *priv = NULL;
    tier_private_t *tierpriv = this->private;
    int ret = -1;
    int mntfd = -1;
    bool need_migration_thread = true;

    priv = GF_CALLOC(1, sizeof(tierfs_t), gf_filesystem_mt_private_t);
    if (!priv) {
        goto out;
    }

    GF_OPTION_RECONF("tier-cold-mountpoint", mount_point, options, str, out);
    if (!mount_point) {
        gf_msg(plugin, GF_LOG_ERROR, EINVAL, 0,
               "missing tier-cold-mountpoint option");
        ret = -1;
        goto out;
    }

    mntfd = sys_open(mount_point, O_DIRECTORY, 0);
    if (mntfd < 0) {
        gf_msg(this->name, GF_LOG_ERROR, errno, 0, "open(%s) failed: %s",
               mount_point, strerror(errno));
        ret = -1;
        goto out;
    }

    /* the open() call is for checking the existence, we don't need it further
     */
    sys_close(mntfd);

    GF_OPTION_RECONF("tier-threshold-block-count", priv->dl_trigger_threshold,
                     options, int32, out);

    GF_OPTION_RECONF("tier-plugin-migrate-thread", need_migration_thread,
                     options, bool, out);

    GF_OPTION_RECONF("tier-cold-block-size", priv->block_size, options, size,
                     out);
    if (!priv->block_size) {
        priv->block_size = BITMAP_BLOCK_SIZE;
    }

    if (priv->mount_point)
        GF_FREE(priv->mount_point);

    priv->mount_point = gf_strdup(mount_point);
    if (!priv->mount_point) {
        gf_msg(plugin, GF_LOG_ERROR, ENOMEM, 0,
               "failed to set tier-cold-mountpoint option");
        ret = -1;
        goto out;
    }

    priv->bmdirfd = sys_open(tierpriv->tierdir, O_DIRECTORY, 0);
    if (priv->bmdirfd < 0) {
        gf_msg(this->name, GF_LOG_ERROR, errno, 0, "open(%s) failed",
               tierpriv->tierdir);
        ret = -1;
        goto out;
    }

    priv->xl = this;
    INIT_LIST_HEAD(&priv->dl_list);
    LOCK_INIT(&priv->lock);

    if (need_migration_thread && !priv->download_thr) {
        tierfs_set_thread(this, priv);
    }

    ret = 0;

out:
    if (ret) {
        GF_FREE(priv);
        priv = NULL;
    }
    return priv;
}

int
filesystem_reconfigure(xlator_t *this, dict_t *options)
{
    tier_private_t *tierpriv = NULL;
    tierfs_t *priv = NULL;
    size_t block_size = 0;
    bool need_migration_thread = true;

    tierpriv = this->private;
    priv = (tierfs_t *)tierpriv->stores->config;

    GF_OPTION_RECONF("tier-cold-block-size", block_size, options, size, out);
    if (block_size) {
        priv->block_size = block_size;
    } else if (!priv->block_size) {
        priv->block_size = 1 * GF_UNIT_MB;
    }

    GF_OPTION_RECONF("tier-threshold-block-count", priv->dl_trigger_threshold,
                     options, int32, out);

    GF_OPTION_RECONF("tier-plugin-migrate-thread", need_migration_thread,
                     options, bool, out);

    if (!need_migration_thread && priv->download_thr) {
        tierfs_clear_thread(this, priv->download_thr);
        priv->download_thr = 0;
    }
    if (need_migration_thread && !priv->download_thr) {
        tierfs_set_thread(this, priv);
    }

out:
    return 0;
}

void
filesystem_fini(void *config)
{
    tierfs_t *priv = (tierfs_t *)config;

    if (!priv)
        return;
    tierfs_clear_thread(THIS, priv->download_thr);
    GF_FREE(priv);
    return;
}

int
restore()
{
    return 0;
}

int
filesystem_download(call_frame_t *frame, void *config)
{
    tierfs_t *priv = (tierfs_t *)config;
    tier_local_t *local = frame->local;
    tierfs_inode_ctx_t *ctx = NULL;
    int32_t op_ret;

    ctx = get_inode_tierfs_ctx(local->inode, priv, local->remotepath, local,
                               true, true, true);
    if (!ctx || !ctx->bitmap) {
        gf_msg(plugin, GF_LOG_ERROR, 0, 0, "%s: ctx get failed",
               local->remotepath);
        goto err;
    }

    gf_msg_debug(plugin, 0, "download called on file (%s) : %s",
                 uuid_utoa(ctx->gfid), local->remotepath);

    LOCK(&priv->lock);
    {
        /* This makes sure we are not being handled by migration thread if it is
         * active */
        list_del_init(&ctx->active_list);
    }
    UNLOCK(&priv->lock);

    op_ret = copy_the_data(frame->this, ctx, 0, priv->block_size);
    /*
     * Submit the restore request.
     */
    if (op_ret < 0) {
        gf_msg(plugin, GF_LOG_ERROR, 0, TIERFS_RESTORE_FAILED,
               "failed to restore file gfid=%s from cold tier(%s)",
               uuid_utoa(ctx->gfid), local->remotepath);
        goto err;
    }

    char src_path[PATH_MAX];
    snprintf(src_path, PATH_MAX, "%s/%s", priv->mount_point, ctx->remotepath);

    /* Who will clear the local xattrs ? Ideally it should be the caller */
    /* download is now finished, call fremovexattr() */
    op_ret = tierfs_clear_local_file_xattr(frame->this, ctx->localfd);
    if (!op_ret) {
        tierfs_inode_unref(ctx, local->inode, frame->this);

        op_ret = sys_unlink(src_path);
        if (op_ret) {
            gf_msg(plugin, GF_LOG_WARNING, errno, TIERFS_RESTORE_FAILED,
                   "failed to remove file(%s) in remote tier", src_path);
        }
    }

    return 0;

err:
    return -1;
}

int
filesystem_triggerdl(xlator_t *this, char *bmfile)
{
    /* If required, this would be called when the special
       xattr is called to download the file */
    gf_msg(plugin, GF_LOG_INFO, 0, 0,
           "trigger download called on file (%s)... Currently ignored. Will "
           "handle it when there are fops on file",
           bmfile);

    return 0;
}

gf_tier_obj_state
filesystem_readblk(call_frame_t *frame, void *config)
{
    gf_tier_obj_state state = GF_TIER_ERROR;
    int32_t op_ret = -1;
    int32_t op_errno = 0;
    struct iobref *iobref = NULL;
    tier_local_t *local = frame->local;
    tierfs_t *priv = (tierfs_t *)config;
    size_t size = local->size;
    off_t off = local->offset;
    off_t offset = off;
    char *data_buf = NULL;
    dict_t *xdata = NULL;
    tierfs_inode_ctx_t *ctx = NULL;

    ctx = get_inode_tierfs_ctx(local->inode, priv, local->remotepath, local,
                               true, true, false);

    /* need the above call in case of append too, so the file migration can be
     * triggered. */
    if (off >= local->ia_size) {
        /* This means, a write happended on the file (with O_APPEND), and read
         * came on the block before the file was completely 'DOWNLOADED'. Should
         * be treated as local file for this segment */
        gf_msg_debug(plugin, 0, "%s: offset bigger (%lu > %lu)",
                     local->remotepath, off, local->ia_size);
        state = GF_TIER_LOCAL;
        goto out;
    }

    if (!size) {
        gf_msg(plugin, GF_LOG_ERROR, 0, 0, "%s: asked size from remote is 0",
               local->remotepath);
        goto out;
    }

    if (!ctx || !ctx->bitmap) {
        gf_msg(plugin, GF_LOG_ERROR, 0, 0, "%s: ctx get failed",
               local->remotepath);
        goto out;
    }

    xdata = dict_new();
    if (!xdata) {
        gf_msg(plugin, GF_LOG_ERROR, ENOMEM, 0, "%s: dict_new() failed",
               local->remotepath);
        goto out;
    }

    op_ret = dict_set_str_sizen(xdata, GLUSTERFS_INTERNAL_FOP_KEY, "1");
    if (op_ret) {
        /* If this fails, the error would be ENOSPC even though there
           is space due to 'storage.reserve' option */
        gf_msg(plugin, GF_LOG_WARNING, EINVAL, 0,
               "%s: failed to set INTERNAL_FOP_KEY in dict", local->remotepath);
    }

    iobref = iobref_new();
    if (!iobref) {
        gf_msg(plugin, GF_LOG_WARNING, ENOMEM, 0, "%s: iobref is NULL",
               local->remotepath);
        op_errno = ENOMEM;
        goto out;
    }

    data_buf = GF_MALLOC(priv->block_size, gf_common_mt_char);
    if (!data_buf) {
        gf_msg(plugin, GF_LOG_ERROR, ENOMEM, 0, "databuf is null");
        op_errno = ENOMEM;
        goto out;
    }

    int64_t blk_start = (off / priv->block_size) * priv->block_size;
    if ((blk_start + priv->block_size) < off + size) {
        gf_msg_debug(plugin, 0, "writev() spans on more than one block");
        gf_msg_debug(plugin, 0, "%ld %ld %zu %ld", blk_start, off, size,
                     ctx->bmsize);
    }

    for (; offset < local->ia_size; blk_start += priv->block_size) {
        /* If the block is not local, we need to read from the cold tier, and
         * update bitmap */
        if ((off + size) < offset) {
            break;
        }
        int bm_array_idx = offset / (8 * priv->block_size);
        int shift = (offset / priv->block_size) % 8;

        if (is_block_local(ctx, bm_array_idx, shift, false)) {
            /* perform the write call */
            gf_msg(plugin, GF_LOG_DEBUG, 0, 0, "%s: already-local %d %d",
                   local->remotepath, bm_array_idx, shift);
            state = GF_TIER_LOCAL;
            goto out;
        }

        /* We can further optimize to read of data which is about to be written
         */
        op_ret = sys_pread(ctx->tierfd, data_buf, priv->block_size, blk_start);
        if (op_ret < 0) {
            op_errno = errno;
            gf_msg(plugin, GF_LOG_ERROR, op_errno, 0, "%s: read failed",
                   local->remotepath);
            state = GF_TIER_ERROR;
            goto out;
        }

        if (op_ret == 0) {
            gf_msg(plugin, GF_LOG_DEBUG, 0, 0, "%s: read returned 0",
                   local->remotepath);
            goto out;
        }
        /* It is critical for us to make sure we have not updated the same
         * index, write block locally, and then update the bitmap 'atomically'
         */
        state = update_data_and_bitmap(frame->this, ctx, bm_array_idx, shift,
                                       data_buf, priv->block_size, blk_start,
                                       iobref, 0, xdata);
        offset = blk_start + priv->block_size;
    }

out:
    if (iobref) {
        iobref_unref(iobref);
    }
    if (xdata) {
        dict_unref(xdata);
    }
    GF_FREE(data_buf);

    return state;
}

gf_tier_obj_state
filesystem_read(call_frame_t *frame, char *data_buf, size_t *data_size,
                void *config)
{
    gf_tier_obj_state state = GF_TIER_ERROR;
    tierfs_t *priv = (tierfs_t *)config;
    tier_local_t *local = frame->local;

    int32_t op_ret = -1;
    size_t size = local->size;
    off_t off = local->offset;
    size_t read_size = 0;
    tierfs_inode_ctx_t *ctx = NULL;

    if (off >= local->ia_size) {
        /* This means, a write happended on the file (with O_APPEND), and read
         * came on the block before the file was completely 'DOWNLOADED'. Should
         * be treated as local file for this segment */
        return GF_TIER_LOCAL;
    }

    /* if offset + size requested is above the ia_size, then we may need to
     * download the last segment */
    if ((off + size) > local->ia_size) {
        gf_msg_debug(plugin, 0,
                     "read requested outside of the file remote size");
        /* this is a check added to see if we get read on last block */
    }
    uint64_t blk_start = (off / priv->block_size) * priv->block_size;
    uint64_t blk_end = blk_start + priv->block_size;
    bool more_than_one_block = false;

    if (blk_end < (off + size)) {
        gf_msg_debug(plugin, 0,
                     "read requested from 2 or more blocks (%lu > %lu %zu)",
                     blk_end, off, size);
        more_than_one_block = true;
    }

    ctx = get_inode_tierfs_ctx(local->inode, priv, local->remotepath, local,
                               true, false, false);
    if (!ctx || !size || !data_buf || !data_size) {
        gf_msg(plugin, GF_LOG_WARNING, 0, 0, "%s: ctx get failed",
               local->remotepath);
        goto err;
    }

    int64_t tmp_size = (blk_end - off);
    bool good_to_break = false;

    while (true) {
        if (tmp_size > (size - read_size)) {
            tmp_size = (size - read_size);
            good_to_break = true;
        }

        if (tmp_size == 0) {
            break;
        }
        /* If the block is not local, we need to read from the cold tier, and
         * update bitmap */
        int bm_array_idx = off / (8 * priv->block_size);
        int shift = (off / priv->block_size) % 8;

        if (is_block_local(ctx, bm_array_idx, shift, false)) {
            /* perform the write call */
            gf_msg_debug(plugin, 0, "block is local as per bitmap");
            if ((!read_size) && good_to_break) {
                return GF_TIER_LOCAL;
            }

            if (more_than_one_block) {
                /* pass the call to readblk() as that takes care of making all
                 * blocks involved here as local. */
                gf_msg_debug(plugin, 0, "forwarding to readblock() method");

                return filesystem_readblk(frame, config);
            }
        }

        op_ret = sys_pread(ctx->tierfd, &data_buf[read_size], tmp_size, off);
        if (op_ret < 0) {
            gf_msg(plugin, GF_LOG_ERROR, errno, 0,
                   "%s: remote read failed - read(off=%lu, size=%ld), "
                   "received(%lu, %zu)",
                   local->remotepath, off, tmp_size, local->offset,
                   local->size);
            goto err;
        }
        read_size += op_ret;

        if (good_to_break)
            break;

        off = blk_end;
        blk_end += priv->block_size;
        tmp_size = (blk_end - off);
    }
    *data_size = read_size;
    state = GF_TIER_REMOTE;
err:
    return state;
}

int
filesystem_delete(call_frame_t *frame, int flags, void *config)
{
    int32_t op_ret = -1;
    int32_t op_errno = 0;
    tierfs_t *priv = config;
    tier_local_t *local = frame->local;
    tierfs_inode_ctx_t *ctx = NULL;
    char src_path[PATH_MAX];

    gf_msg_debug(plugin, 0, "delete invoked for path = %s [%u %u]",
                 local->remotepath, local->inode->fd_count,
                 local->inode->active_fd_count);

    ctx = get_inode_tierfs_ctx(local->inode, priv, local->remotepath, local,
                               true, false, false);
    if (!ctx) {
        goto out;
    }

    snprintf(src_path, PATH_MAX, "%s/%s", priv->mount_point, local->remotepath);
    op_ret = sys_unlink(src_path);
    if (op_ret) {
        op_errno = errno;
        gf_msg(plugin, GF_LOG_ERROR, op_errno, 0,
               "%s: failed to unlink remote file", local->remotepath);
        /* error is sent back to higher layer, no need to do anything now */
        goto out;
    }

    /* if the remote file is deleted, the inode's ctx should go too (if
     * any). */
    if (ctx) {
        tierfs_inode_unref(ctx, local->inode, frame->this);
    }
out:
    return op_errno;
}

int
filesystem_fsync(call_frame_t *frame, void *config, fd_t *fd)
{
    tier_local_t *local = frame->local;
    tierfs_t *priv = (tierfs_t *)config;
    tierfs_inode_ctx_t *ctx = NULL;
    int ret = 0;

    ctx = get_inode_tierfs_ctx(local->inode, priv, local->remotepath, local,
                               false, false, false);
    if (!ctx) {
        gf_msg(plugin, GF_LOG_DEBUG, 0, 0, "%s: ctx get failed",
               local->remotepath);
        goto out;
    }

    if (ctx->bmfd >= 0 && ctx->bitmap) {
        ret = sys_fsync(ctx->bmfd);
        if (ret) {
            gf_msg(plugin, GF_LOG_ERROR, errno, 0,
                   "%s: failed to do fsync of bitmap file", local->remotepath);
            goto out;
        }
    }

    ret = 0;
out:
    return ret;
}

int
filesystem_open(inode_t *inode, fd_t *fd, char *remotepath, void *config)
{
    int ret = 0;
    tierfs_t *priv = config;
    tierfs_inode_ctx_t *ctx = NULL;

    ctx = get_inode_tierfs_ctx(inode, priv, remotepath, NULL, true, false,
                               false);
    if (!ctx) {
        gf_msg(plugin, GF_LOG_DEBUG, 0, 0, "%s: ctx get failed", remotepath);
        ret = -1;
        goto err;
    }

    /* add a ref here, unref in release */
    GF_ATOMIC_INC(ctx->ref);

err:
    return ret;
}

int
filesystem_release(inode_t *inode, fd_t *fd, void *config)
{
    int ret = 0;
    tierfs_t *priv = config;
    tierfs_inode_ctx_t *ctx = NULL;

    /* create_ctx is false here, so no ref if there are no contexts */
    ctx = get_inode_tierfs_ctx(inode, priv, NULL, NULL, false, false, false);
    if (!ctx) {
        gf_msg(plugin, GF_LOG_DEBUG, 0, 0, "%s: ctx get failed",
               uuid_utoa(inode->gfid));
        ret = -1;
        goto err;
    }

    /* This should handle a unref */
    tierfs_inode_unref(ctx, inode, priv->xl);

err:
    return ret;
}

int
filesystem_forget(void *ctxptr, void *config)
{
    tierfs_t *priv = config;
    tierfs_inode_ctx_t *ctx = ctxptr;

    gf_msg(plugin, GF_LOG_INFO, 0, 0, "%s: in forget (ref == %ld)",
           ctx->remotepath, GF_ATOMIC_GET(ctx->ref));

    inode_clear_tierfs_ctx(ctx, NULL, priv->xl, false);
    return 0;
}

int
filesystem_rmdir(call_frame_t *frame, void *config, const char *path)
{
    int32_t op_ret = -1;
    int32_t op_errno = 0;
    char dirpath[PATH_MAX] = "";
    tierfs_t *priv = config;

    gf_msg_debug(plugin, 0, "rmdir invoked for path = %s", path);
    op_ret = snprintf(dirpath, PATH_MAX - 1, "%s/%s", priv->mount_point, path);
    if (op_ret < 0) {
        op_errno = errno;
        gf_msg(plugin, GF_LOG_INFO, errno, 0,
               "%s: failed to rmdir on remote tier (due to snprintf() failure",
               path);
        goto out;
    }
    op_ret = sys_rmdir(dirpath);
    if (op_ret) {
        /* if the remote file is deleted, the inode's ctx should go too (if
         * any). */
        op_errno = errno;
        if (!((op_errno == ENOENT) || (op_errno == ENOTEMPTY))) {
            gf_msg(plugin, GF_LOG_INFO, errno, 0,
                   "%s: failed to rmdir on remote tier", path);
        } else {
            gf_msg_debug(plugin, errno, "%s: failed to rmdir on remote tier",
                         path);
        }
        /* error is sent back to higher layer, no need to do anything now */
    }

out:
    return op_errno;
}

int
filesystem_get_value(inode_t *inode, void *config, tier_getvalue_keys_t key,
                     dict_t *dict)
{
    int ret = -1;
    int32_t op_errno = EINVAL;
    tierfs_t *priv = config;
    tierfs_inode_ctx_t *ctx = NULL;

    if (!inode || !config || !dict)
        goto out;

    ctx = get_inode_tierfs_ctx(inode, priv, NULL, NULL, false, false, false);
    if (!ctx) {
        /* Why this happens? */
        /*
           1. file just got migrated, so the tier xlator has context set on the
           inode.
           2. the getxattr() called before any open() or other file operation
           call.
           3. this get_ctx() is called with `create_ctx == false`, so there is
           possibility the context is not set in plugin's inode context.
           4. here, ctx can be NULL in plugin.
        */
        gf_msg(plugin, GF_LOG_DEBUG, 0, 0, "%s: ctx get failed",
               uuid_utoa(inode->gfid));
    }

    switch (key) {
        case GF_TIER_WRITE_COUNT: {
            /* If ctx is not set, then send 0, because there is no write
             * operation on the file yet. */
            int32_t count = (ctx) ? ctx->block_count : 0;
            ret = dict_set_int32(dict, TIER_MIGRATED_BLOCK_CNT, count);
            if (ret) {
                gf_msg(plugin, GF_LOG_DEBUG, 0, 0,
                       "%s: failed to set migrated block count",
                       uuid_utoa(inode->gfid));
            }
        } break;
    }

    op_errno = ret;
out:
    return -op_errno;
}

store_methods_t store_ops = {
    .fop_download = filesystem_download,
    .fop_init = filesystem_init,
    .fop_reconfigure = filesystem_reconfigure,
    .fop_fini = filesystem_fini,
    .fop_remote_read = filesystem_read,
    .fop_remote_delete = filesystem_delete,
    .fop_remote_readblk = filesystem_readblk,
    .fop_sync = filesystem_fsync,
    .fop_rmdir = filesystem_rmdir,
    .get_value = filesystem_get_value,
    .forget = filesystem_forget,
};
