/*
  Copyright (c) 2008-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/*
 * performance/readdir-ahead preloads a local buffer with directory entries
 * on opendir. The optimization involves using maximum sized gluster rpc
 * requests (128k) to minimize overhead of smaller client requests.
 *
 * For example, fuse currently supports a maximum readdir buffer of 4k
 * (regardless of the filesystem client's buffer size). readdir-ahead should
 * effectively convert these smaller requests into fewer, larger sized requests
 * for simple, sequential workloads (i.e., ls).
 *
 * The translator is currently designed to handle the simple, sequential case
 * only. If a non-sequential directory read occurs, readdir-ahead disables
 * preloads on the directory.
 */

#include <math.h>
#include <glusterfs/glusterfs.h>
#include <glusterfs/xlator.h>
#include <glusterfs/call-stub.h>
#include "readdir-ahead.h"
#include "readdir-ahead-mem-types.h"
#include <glusterfs/defaults.h>
#include "readdir-ahead-messages.h"
static int
rda_fill_fd(call_frame_t *, xlator_t *, fd_t *);

static void
rda_local_wipe(struct rda_local *local)
{
    if (local->fd)
        fd_unref(local->fd);
    if (local->xattrs)
        dict_unref(local->xattrs);
    if (local->inode)
        inode_unref(local->inode);
}

/*
 * Get (or create) the fd context for storing prepopulated directory
 * entries.
 */
static struct rda_fd_ctx *
get_rda_fd_ctx(fd_t *fd, xlator_t *this)
{
    uint64_t val;
    struct rda_fd_ctx *ctx;

    LOCK(&fd->lock);

    if (__fd_ctx_get(fd, this, &val) < 0) {
        ctx = GF_CALLOC(1, sizeof(struct rda_fd_ctx), gf_rda_mt_rda_fd_ctx);
        if (!ctx)
            goto out;

        LOCK_INIT(&ctx->lock);
        INIT_LIST_HEAD(&ctx->entries.list);
        ctx->state = RDA_FD_NEW;
        /* ctx offset values initialized to 0 */
        ctx->xattrs = NULL;

        if (__fd_ctx_set(fd, this, (uint64_t)(uintptr_t)ctx) < 0) {
            GF_FREE(ctx);
            ctx = NULL;
            goto out;
        }
    } else {
        ctx = (struct rda_fd_ctx *)(uintptr_t)val;
    }
out:
    UNLOCK(&fd->lock);
    return ctx;
}

static rda_inode_ctx_t *
__rda_inode_ctx_get(inode_t *inode, xlator_t *this)
{
    int ret = -1;
    uint64_t ctx_uint = 0;
    rda_inode_ctx_t *ctx_p = NULL;

    ret = __inode_ctx_get1(inode, this, &ctx_uint);
    if (ret == 0)
        return (rda_inode_ctx_t *)(uintptr_t)ctx_uint;

    ctx_p = GF_CALLOC(1, sizeof(*ctx_p), gf_rda_mt_inode_ctx_t);
    if (!ctx_p)
        return NULL;

    GF_ATOMIC_INIT(ctx_p->generation, 0);

    ctx_uint = (uint64_t)(uintptr_t)ctx_p;
    ret = __inode_ctx_set1(inode, this, &ctx_uint);
    if (ret < 0) {
        GF_FREE(ctx_p);
        return NULL;
    }

    return ctx_p;
}

static int
__rda_inode_ctx_update_iatts(inode_t *inode, xlator_t *this,
                             struct iatt *stbuf_in, struct iatt *stbuf_out,
                             uint64_t generation)
{
    rda_inode_ctx_t *ctx_p = NULL;
    struct iatt tmp_stat = {
        0,
    };

    ctx_p = __rda_inode_ctx_get(inode, this);
    if (!ctx_p)
        return -1;

    if ((!stbuf_in) || (stbuf_in->ia_ctime == 0)) {
        /* A fop modified a file but valid stbuf is not provided.
         * Can't update iatt to reflect results of fop and hence
         * invalidate the iatt stored in dentry.
         *
         * An example of this case can be response of write request
         * that is cached in write-behind.
         */
        if (stbuf_in)
            tmp_stat = *stbuf_in;
        else
            tmp_stat = ctx_p->statbuf;
        memset(&ctx_p->statbuf, 0, sizeof(ctx_p->statbuf));
        gf_uuid_copy(ctx_p->statbuf.ia_gfid, tmp_stat.ia_gfid);
        ctx_p->statbuf.ia_type = tmp_stat.ia_type;
        GF_ATOMIC_INC(ctx_p->generation);
    } else {
        if (ctx_p->statbuf.ia_ctime) {
            if (stbuf_in->ia_ctime < ctx_p->statbuf.ia_ctime) {
                goto out;
            }

            if ((stbuf_in->ia_ctime == ctx_p->statbuf.ia_ctime) &&
                (stbuf_in->ia_ctime_nsec < ctx_p->statbuf.ia_ctime_nsec)) {
                goto out;
            }
        } else {
            if ((generation != -1) &&
                (generation != GF_ATOMIC_GET(ctx_p->generation)))
                goto out;
        }

        ctx_p->statbuf = *stbuf_in;
    }

out:
    if (stbuf_out)
        *stbuf_out = ctx_p->statbuf;

    return 0;
}

static int
rda_inode_ctx_update_iatts(inode_t *inode, xlator_t *this,
                           struct iatt *stbuf_in, struct iatt *stbuf_out,
                           uint64_t generation)
{
    int ret = -1;

    LOCK(&inode->lock);
    {
        ret = __rda_inode_ctx_update_iatts(inode, this, stbuf_in, stbuf_out,
                                           generation);
    }
    UNLOCK(&inode->lock);

    return ret;
}

/*
 * Reset the tracking state of the context.
 */
static void
rda_reset_ctx(xlator_t *this, struct rda_fd_ctx *ctx)
{
    struct rda_priv *priv = NULL;

    priv = this->private;

    ctx->state = RDA_FD_NEW;
    ctx->cur_offset = 0;
    ctx->next_offset = 0;
    ctx->op_errno = 0;

    gf_dirent_free(&ctx->entries);
    GF_ATOMIC_SUB(priv->rda_cache_size, ctx->cur_size);
    ctx->cur_size = 0;

    if (ctx->xattrs) {
        dict_unref(ctx->xattrs);
        ctx->xattrs = NULL;
    }
}

static void
rda_mark_inode_dirty(xlator_t *this, inode_t *inode)
{
    inode_t *parent = NULL;
    fd_t *fd = NULL;
    uint64_t val = 0;
    int32_t ret = 0;
    struct rda_fd_ctx *fd_ctx = NULL;
    char gfid[GF_UUID_BUF_SIZE] = {0};

    parent = inode_parent(inode, NULL, NULL);
    if (parent) {
        LOCK(&parent->lock);
        {
            list_for_each_entry(fd, &parent->fd_list, inode_list)
            {
                val = 0;
                fd_ctx_get(fd, this, &val);
                if (val == 0)
                    continue;

                fd_ctx = (void *)(uintptr_t)val;
                uuid_utoa_r(inode->gfid, gfid);
                if (!GF_ATOMIC_GET(fd_ctx->prefetching))
                    continue;

                LOCK(&fd_ctx->lock);
                {
                    if (GF_ATOMIC_GET(fd_ctx->prefetching)) {
                        if (fd_ctx->writes_during_prefetch == NULL)
                            fd_ctx->writes_during_prefetch = dict_new();

                        ret = dict_set_int8(fd_ctx->writes_during_prefetch,
                                            gfid, 1);
                        if (ret < 0) {
                            gf_log(this->name, GF_LOG_WARNING,
                                   "marking to invalidate stats of %s from an "
                                   "in progress "
                                   "prefetching has failed, might result in "
                                   "stale stat to "
                                   "application",
                                   gfid);
                        }
                    }
                }
                UNLOCK(&fd_ctx->lock);
            }
        }
        UNLOCK(&parent->lock);
        inode_unref(parent);
    }

    return;
}

/*
 * Check whether we can handle a request. Offset verification is done by the
 * caller, so we only check whether the preload buffer has completion status
 * (including an error) or has some data to return.
 */
static gf_boolean_t
rda_can_serve_readdirp(struct rda_fd_ctx *ctx, size_t request_size)
{
    if ((ctx->state & RDA_FD_EOD) || (ctx->state & RDA_FD_ERROR) ||
        (!(ctx->state & RDA_FD_PLUGGED) && (ctx->cur_size > 0)) ||
        (request_size && ctx->cur_size >= request_size))
        return _gf_true;

    return _gf_false;
}

void
rda_inode_ctx_get_iatt(inode_t *inode, xlator_t *this, struct iatt *attr)
{
    rda_inode_ctx_t *ctx_p = NULL;

    if (!inode || !this || !attr)
        goto out;

    LOCK(&inode->lock);
    {
        ctx_p = __rda_inode_ctx_get(inode, this);
        if (ctx_p) {
            *attr = ctx_p->statbuf;
        }
    }
    UNLOCK(&inode->lock);

out:
    return;
}

/*
 * Serve a request from the fd dentry list based on the size of the request
 * buffer. ctx must be locked.
 */
static int32_t
__rda_fill_readdirp(xlator_t *this, gf_dirent_t *entries, size_t request_size,
                    struct rda_fd_ctx *ctx)
{
    gf_dirent_t *dirent, *tmp;
    size_t dirent_size, size = 0;
    int32_t count = 0;
    struct rda_priv *priv = NULL;
    struct iatt tmp_stat = {
        0,
    };

    priv = this->private;

    list_for_each_entry_safe(dirent, tmp, &ctx->entries.list, list)
    {
        dirent_size = gf_dirent_size(dirent->d_name);
        if (size + dirent_size > request_size)
            break;

        memset(&tmp_stat, 0, sizeof(tmp_stat));

        if (dirent->inode && (!((strcmp(dirent->d_name, ".") == 0) ||
                                (strcmp(dirent->d_name, "..") == 0)))) {
            rda_inode_ctx_get_iatt(dirent->inode, this, &tmp_stat);
            dirent->d_stat = tmp_stat;
        }

        size += dirent_size;
        list_del_init(&dirent->list);
        ctx->cur_size -= dirent_size;

        GF_ATOMIC_SUB(priv->rda_cache_size, dirent_size);

        list_add_tail(&dirent->list, &entries->list);
        ctx->cur_offset = dirent->d_off;
        count++;
    }

    if (ctx->cur_size <= priv->rda_low_wmark)
        ctx->state |= RDA_FD_PLUGGED;

    return count;
}

static int32_t
__rda_serve_readdirp(xlator_t *this, struct rda_fd_ctx *ctx, size_t size,
                     gf_dirent_t *entries, int *op_errno)
{
    int32_t ret = 0;

    ret = __rda_fill_readdirp(this, entries, size, ctx);

    if (!ret && (ctx->state & RDA_FD_ERROR)) {
        ret = -1;
        ctx->state &= ~RDA_FD_ERROR;

        /*
         * the preload has stopped running in the event of an error, so
         * pass all future requests along
         */
        ctx->state |= RDA_FD_BYPASS;
    }
    /*
     * Use the op_errno sent by lower layers as xlators above will check
     * the op_errno for identifying whether readdir is completed or not.
     */
    *op_errno = ctx->op_errno;

    return ret;
}

static int32_t
rda_readdirp(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t off, dict_t *xdata)
{
    struct rda_fd_ctx *ctx = NULL;
    int fill = 0;
    gf_dirent_t entries;
    int ret = 0;
    int op_errno = 0;
    gf_boolean_t serve = _gf_false;

    ctx = get_rda_fd_ctx(fd, this);
    if (!ctx)
        goto err;

    if (ctx->state & RDA_FD_BYPASS)
        goto bypass;

    INIT_LIST_HEAD(&entries.list);
    LOCK(&ctx->lock);

    /* recheck now that we have the lock */
    if (ctx->state & RDA_FD_BYPASS) {
        UNLOCK(&ctx->lock);
        goto bypass;
    }

    /*
     * If a new read comes in at offset 0 and the buffer has been
     * completed, reset the context and kickstart the filler again.
     */
    if (!off && (ctx->state & RDA_FD_EOD) && (ctx->cur_size == 0)) {
        rda_reset_ctx(this, ctx);
        /*
         * Unref and discard the 'list of xattrs to be fetched'
         * stored during opendir call. This is done above - inside
         * rda_reset_ctx().
         * Now, ref the xdata passed by md-cache in actual readdirp()
         * call and use that for all subsequent internal readdirp()
         * requests issued by this xlator.
         */
        ctx->xattrs = dict_ref(xdata);
        fill = 1;
    }

    /*
     * If a readdir occurs at an unexpected offset or we already have a
     * request pending, admit defeat and just get out of the way.
     */
    if (off != ctx->cur_offset || ctx->stub) {
        ctx->state |= RDA_FD_BYPASS;
        UNLOCK(&ctx->lock);
        goto bypass;
    }

    /*
     * If we haven't bypassed the preload, this means we can either serve
     * the request out of the preload or the request that enables us to do
     * so is in flight...
     */
    if (rda_can_serve_readdirp(ctx, size)) {
        ret = __rda_serve_readdirp(this, ctx, size, &entries, &op_errno);
        serve = _gf_true;

        if (op_errno == ENOENT &&
            !((ctx->state & RDA_FD_EOD) && (ctx->cur_size == 0)))
            op_errno = 0;
    } else {
        ctx->stub = fop_readdirp_stub(frame, NULL, fd, size, off, xdata);
        if (!ctx->stub) {
            UNLOCK(&ctx->lock);
            goto err;
        }

        if (!(ctx->state & RDA_FD_RUNNING)) {
            fill = 1;
            if (!ctx->xattrs)
                ctx->xattrs = dict_ref(xdata);
            ctx->state |= RDA_FD_RUNNING;
        }
    }

    UNLOCK(&ctx->lock);

    if (serve) {
        STACK_UNWIND_STRICT(readdirp, frame, ret, op_errno, &entries, xdata);
        gf_dirent_free(&entries);
    }

    if (fill)
        rda_fill_fd(frame, this, fd);

    return 0;

bypass:
    STACK_WIND(frame, default_readdirp_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readdirp, fd, size, off, xdata);
    return 0;

err:
    STACK_UNWIND_STRICT(readdirp, frame, -1, ENOMEM, NULL, NULL);
    return 0;
}

static int32_t
rda_fill_fd_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                dict_t *xdata)
{
    gf_dirent_t *dirent = NULL;
    gf_dirent_t *tmp = NULL;
    gf_dirent_t serve_entries;
    struct rda_local *local = frame->local;
    struct rda_fd_ctx *ctx = local->ctx;
    struct rda_priv *priv = this->private;
    int fill = 1;
    size_t dirent_size = 0;
    int ret = 0;
    gf_boolean_t serve = _gf_false;
    call_stub_t *stub = NULL;
    char gfid[GF_UUID_BUF_SIZE] = {
        0,
    };
    uint64_t generation = 0;
    call_frame_t *fill_frame = NULL;

    INIT_LIST_HEAD(&serve_entries.list);
    LOCK(&ctx->lock);

    /* Verify that the preload buffer is still pending on this data. */
    if (ctx->next_offset != local->offset) {
        gf_msg(this->name, GF_LOG_ERROR, 0, READDIR_AHEAD_MSG_OUT_OF_SEQUENCE,
               "Out of sequence directory preload.");
        ctx->state |= (RDA_FD_BYPASS | RDA_FD_ERROR);
        ctx->op_errno = EUCLEAN;

        goto out;
    }

    if (entries) {
        list_for_each_entry_safe(dirent, tmp, &entries->list, list)
        {
            list_del_init(&dirent->list);

            /* must preserve entry order */
            list_add_tail(&dirent->list, &ctx->entries.list);
            if (dirent->inode) {
                /* If ctxp->stat is invalidated, don't update it
                 * with dirent->d_stat as we don't have
                 * generation number of the inode when readdirp
                 * request was initiated. So, we pass 0 for
                 * generation number
                 */

                generation = -1;
                if (ctx->writes_during_prefetch) {
                    memset(gfid, 0, sizeof(gfid));
                    uuid_utoa_r(dirent->inode->gfid, gfid);
                    if (dict_get(ctx->writes_during_prefetch, gfid))
                        generation = 0;
                }

                if (!((strcmp(dirent->d_name, ".") == 0) ||
                      (strcmp(dirent->d_name, "..") == 0))) {
                    rda_inode_ctx_update_iatts(dirent->inode, this,
                                               &dirent->d_stat, &dirent->d_stat,
                                               generation);
                }
            }

            dirent_size = gf_dirent_size(dirent->d_name);

            ctx->cur_size += dirent_size;

            GF_ATOMIC_ADD(priv->rda_cache_size, dirent_size);

            ctx->next_offset = dirent->d_off;
        }
    }

    if (ctx->writes_during_prefetch) {
        dict_unref(ctx->writes_during_prefetch);
        ctx->writes_during_prefetch = NULL;
    }

    GF_ATOMIC_DEC(ctx->prefetching);

    if (ctx->cur_size >= priv->rda_high_wmark)
        ctx->state &= ~RDA_FD_PLUGGED;

    if (!op_ret || op_errno == ENOENT) {
        /* we've hit eod */
        ctx->state &= ~RDA_FD_RUNNING;
        ctx->state |= RDA_FD_EOD;
        ctx->op_errno = op_errno;
    } else if (op_ret == -1) {
        /* kill the preload and pend the error */
        ctx->state &= ~RDA_FD_RUNNING;
        ctx->state |= RDA_FD_ERROR;
        ctx->op_errno = op_errno;
    }

    /*
     * NOTE: The strict bypass logic in readdirp() means a pending request
     * is always based on ctx->cur_offset.
     */
    if (ctx->stub && rda_can_serve_readdirp(ctx, ctx->stub->args.size)) {
        ret = __rda_serve_readdirp(this, ctx, ctx->stub->args.size,
                                   &serve_entries, &op_errno);
        serve = _gf_true;
        stub = ctx->stub;
        ctx->stub = NULL;
    }

out:
    /*
     * If we have been marked for bypass and have no pending stub, clear the
     * run state so we stop preloading the context with entries.
     */
    if (!ctx->stub &&
        ((ctx->state & RDA_FD_BYPASS) ||
         GF_ATOMIC_GET(priv->rda_cache_size) > priv->rda_cache_limit))
        ctx->state &= ~RDA_FD_RUNNING;

    if (!(ctx->state & RDA_FD_RUNNING)) {
        fill = 0;
        if (ctx->xattrs) {
            /*
             * fill = 0 and hence rda_fill_fd() won't be invoked.
             * unref for ref taken in rda_fill_fd()
             */
            dict_unref(ctx->xattrs);
            ctx->xattrs = NULL;
        }

        fill_frame = ctx->fill_frame;
        ctx->fill_frame = NULL;
    }

    if (op_errno == ENOENT &&
        !((ctx->state & RDA_FD_EOD) && (ctx->cur_size == 0)))
        op_errno = 0;

    UNLOCK(&ctx->lock);
    if (fill_frame) {
        rda_local_wipe(fill_frame->local);
        STACK_DESTROY(fill_frame->root);
    }

    if (serve) {
        STACK_UNWIND_STRICT(readdirp, stub->frame, ret, op_errno,
                            &serve_entries, xdata);
        gf_dirent_free(&serve_entries);
        call_stub_destroy(stub);
    }

    if (fill)
        rda_fill_fd(frame, this, local->fd);

    return 0;
}

/*
 * Start prepopulating the fd context with directory entries.
 */
static int
rda_fill_fd(call_frame_t *frame, xlator_t *this, fd_t *fd)
{
    call_frame_t *nframe = NULL;
    struct rda_local *local = NULL;
    struct rda_local *orig_local = frame->local;
    struct rda_fd_ctx *ctx;
    off_t offset;
    struct rda_priv *priv = this->private;

    ctx = get_rda_fd_ctx(fd, this);
    if (!ctx)
        goto err;

    LOCK(&ctx->lock);

    if (ctx->state & RDA_FD_NEW) {
        ctx->state &= ~RDA_FD_NEW;
        ctx->state |= RDA_FD_RUNNING;
        if (priv->rda_low_wmark)
            ctx->state |= RDA_FD_PLUGGED;
    }

    offset = ctx->next_offset;

    if (!ctx->fill_frame) {
        nframe = copy_frame(frame);
        if (!nframe) {
            UNLOCK(&ctx->lock);
            goto err;
        }

        local = mem_get0(this->local_pool);
        if (!local) {
            UNLOCK(&ctx->lock);
            goto err;
        }

        local->ctx = ctx;
        local->fd = fd_ref(fd);
        nframe->local = local;

        ctx->fill_frame = nframe;

        if (!ctx->xattrs && orig_local && orig_local->xattrs) {
            /* when this function is invoked by rda_opendir_cbk */
            ctx->xattrs = dict_ref(orig_local->xattrs);
        }
    } else {
        nframe = ctx->fill_frame;
        local = nframe->local;
    }

    local->offset = offset;
    GF_ATOMIC_INC(ctx->prefetching);

    UNLOCK(&ctx->lock);

    STACK_WIND(nframe, rda_fill_fd_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readdirp, fd, priv->rda_req_size,
               offset, ctx->xattrs);

    return 0;

err:
    if (nframe) {
        rda_local_wipe(nframe->local);
        FRAME_DESTROY(nframe);
    }

    return -1;
}

static int32_t
rda_opendir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
    if (!op_ret)
        rda_fill_fd(frame, this, fd);

    RDA_STACK_UNWIND(opendir, frame, op_ret, op_errno, fd, xdata);
    return 0;
}

static int32_t
rda_opendir(call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
            dict_t *xdata)
{
    int op_errno = 0;
    struct rda_local *local = NULL;

    if (xdata) {
        local = mem_get0(this->local_pool);
        if (!local) {
            op_errno = ENOMEM;
            goto unwind;
        }

        /*
         * Retrieve list of keys set by md-cache xlator and store it
         * in local to be consumed in rda_opendir_cbk
         */
        local->xattrs = dict_copy_with_ref(xdata, NULL);
        frame->local = local;
    }

    STACK_WIND(frame, rda_opendir_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->opendir, loc, fd, xdata);
    return 0;

unwind:
    STACK_UNWIND_STRICT(opendir, frame, -1, op_errno, fd, xdata);
    return 0;
}

static int32_t
rda_writev_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf, dict_t *xdata)
{
    struct rda_local *local = NULL;
    struct iatt postbuf_out = {
        0,
    };

    if (op_ret < 0)
        goto unwind;

    local = frame->local;

    rda_mark_inode_dirty(this, local->inode);

    rda_inode_ctx_update_iatts(local->inode, this, postbuf, &postbuf_out,
                               local->generation);

unwind:
    RDA_STACK_UNWIND(writev, frame, op_ret, op_errno, prebuf, &postbuf_out,
                     xdata);
    return 0;
}

static int32_t
rda_writev(call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t off, uint32_t flags, struct iobref *iobref,
           dict_t *xdata)
{
    RDA_COMMON_MODIFICATION_FOP(writev, frame, this, fd->inode, xdata, fd,
                                vector, count, off, flags, iobref);
    return 0;
}

static int32_t
rda_fallocate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{
    struct rda_local *local = NULL;
    struct iatt postbuf_out = {
        0,
    };

    if (op_ret < 0)
        goto unwind;

    local = frame->local;
    rda_mark_inode_dirty(this, local->inode);
    rda_inode_ctx_update_iatts(local->inode, this, postbuf, &postbuf_out,
                               local->generation);

unwind:
    RDA_STACK_UNWIND(fallocate, frame, op_ret, op_errno, prebuf, &postbuf_out,
                     xdata);
    return 0;
}

static int32_t
rda_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t keep_size,
              off_t offset, size_t len, dict_t *xdata)
{
    RDA_COMMON_MODIFICATION_FOP(fallocate, frame, this, fd->inode, xdata, fd,
                                keep_size, offset, len);
    return 0;
}

static int32_t
rda_zerofill_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                 struct iatt *postbuf, dict_t *xdata)
{
    struct rda_local *local = NULL;
    struct iatt postbuf_out = {
        0,
    };

    if (op_ret < 0)
        goto unwind;

    local = frame->local;
    rda_mark_inode_dirty(this, local->inode);
    rda_inode_ctx_update_iatts(local->inode, this, postbuf, &postbuf_out,
                               local->generation);

unwind:
    RDA_STACK_UNWIND(zerofill, frame, op_ret, op_errno, prebuf, &postbuf_out,
                     xdata);
    return 0;
}

static int32_t
rda_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
             off_t len, dict_t *xdata)
{
    RDA_COMMON_MODIFICATION_FOP(zerofill, frame, this, fd->inode, xdata, fd,
                                offset, len);
    return 0;
}

static int32_t
rda_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                struct iatt *postbuf, dict_t *xdata)
{
    struct rda_local *local = NULL;
    struct iatt postbuf_out = {
        0,
    };

    if (op_ret < 0)
        goto unwind;

    local = frame->local;
    rda_mark_inode_dirty(this, local->inode);
    rda_inode_ctx_update_iatts(local->inode, this, postbuf, &postbuf_out,
                               local->generation);

unwind:
    RDA_STACK_UNWIND(discard, frame, op_ret, op_errno, prebuf, &postbuf_out,
                     xdata);
    return 0;
}

static int32_t
rda_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            size_t len, dict_t *xdata)
{
    RDA_COMMON_MODIFICATION_FOP(discard, frame, this, fd->inode, xdata, fd,
                                offset, len);
    return 0;
}

static int32_t
rda_ftruncate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{
    struct rda_local *local = NULL;
    struct iatt postbuf_out = {
        0,
    };

    if (op_ret < 0)
        goto unwind;

    local = frame->local;
    rda_mark_inode_dirty(this, local->inode);
    rda_inode_ctx_update_iatts(local->inode, this, postbuf, &postbuf_out,
                               local->generation);

unwind:
    RDA_STACK_UNWIND(ftruncate, frame, op_ret, op_errno, prebuf, &postbuf_out,
                     xdata);
    return 0;
}

static int32_t
rda_ftruncate(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              dict_t *xdata)
{
    RDA_COMMON_MODIFICATION_FOP(ftruncate, frame, this, fd->inode, xdata, fd,
                                offset);
    return 0;
}

static int32_t
rda_truncate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                 struct iatt *postbuf, dict_t *xdata)
{
    struct rda_local *local = NULL;
    struct iatt postbuf_out = {
        0,
    };

    if (op_ret < 0)
        goto unwind;

    local = frame->local;
    rda_mark_inode_dirty(this, local->inode);
    rda_inode_ctx_update_iatts(local->inode, this, postbuf, &postbuf_out,
                               local->generation);

unwind:
    RDA_STACK_UNWIND(ftruncate, frame, op_ret, op_errno, prebuf, &postbuf_out,
                     xdata);
    return 0;
}

static int32_t
rda_truncate(call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
             dict_t *xdata)
{
    RDA_COMMON_MODIFICATION_FOP(truncate, frame, this, loc->inode, xdata, loc,
                                offset);
    return 0;
}

static int32_t
rda_setxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    struct rda_local *local = NULL;

    if (op_ret < 0)
        goto unwind;

    local = frame->local;
    rda_mark_inode_dirty(this, local->inode);
    rda_inode_ctx_update_iatts(local->inode, this, NULL, NULL,
                               local->generation);
unwind:
    RDA_STACK_UNWIND(setxattr, frame, op_ret, op_errno, xdata);
    return 0;
}

static int32_t
rda_setxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
             int32_t flags, dict_t *xdata)
{
    RDA_COMMON_MODIFICATION_FOP(setxattr, frame, this, loc->inode, xdata, loc,
                                dict, flags);
    return 0;
}

static int32_t
rda_fsetxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    struct rda_local *local = NULL;

    if (op_ret < 0)
        goto unwind;

    local = frame->local;
    rda_mark_inode_dirty(this, local->inode);
    rda_inode_ctx_update_iatts(local->inode, this, NULL, NULL,
                               local->generation);
unwind:
    RDA_STACK_UNWIND(fsetxattr, frame, op_ret, op_errno, xdata);
    return 0;
}

static int32_t
rda_fsetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
              int32_t flags, dict_t *xdata)
{
    RDA_COMMON_MODIFICATION_FOP(fsetxattr, frame, this, fd->inode, xdata, fd,
                                dict, flags);
    return 0;
}

static int32_t
rda_setattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                struct iatt *statpost, dict_t *xdata)
{
    struct rda_local *local = NULL;
    struct iatt postbuf_out = {
        0,
    };

    if (op_ret < 0)
        goto unwind;

    local = frame->local;
    rda_mark_inode_dirty(this, local->inode);
    rda_inode_ctx_update_iatts(local->inode, this, statpost, &postbuf_out,
                               local->generation);

unwind:
    RDA_STACK_UNWIND(setattr, frame, op_ret, op_errno, statpre, &postbuf_out,
                     xdata);
    return 0;
}

static int32_t
rda_setattr(call_frame_t *frame, xlator_t *this, loc_t *loc, struct iatt *stbuf,
            int32_t valid, dict_t *xdata)
{
    RDA_COMMON_MODIFICATION_FOP(setattr, frame, this, loc->inode, xdata, loc,
                                stbuf, valid);
    return 0;
}

static int32_t
rda_fsetattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                 struct iatt *statpost, dict_t *xdata)
{
    struct rda_local *local = NULL;
    struct iatt postbuf_out = {
        0,
    };

    if (op_ret < 0)
        goto unwind;

    local = frame->local;
    rda_mark_inode_dirty(this, local->inode);
    rda_inode_ctx_update_iatts(local->inode, this, statpost, &postbuf_out,
                               local->generation);

unwind:
    RDA_STACK_UNWIND(fsetattr, frame, op_ret, op_errno, statpre, &postbuf_out,
                     xdata);
    return 0;
}

static int32_t
rda_fsetattr(call_frame_t *frame, xlator_t *this, fd_t *fd, struct iatt *stbuf,
             int32_t valid, dict_t *xdata)
{
    RDA_COMMON_MODIFICATION_FOP(fsetattr, frame, this, fd->inode, xdata, fd,
                                stbuf, valid);
    return 0;
}

static int32_t
rda_removexattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    struct rda_local *local = NULL;

    if (op_ret < 0)
        goto unwind;

    local = frame->local;
    rda_mark_inode_dirty(this, local->inode);
    rda_inode_ctx_update_iatts(local->inode, this, NULL, NULL,
                               local->generation);
unwind:
    RDA_STACK_UNWIND(removexattr, frame, op_ret, op_errno, xdata);
    return 0;
}

static int32_t
rda_removexattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name, dict_t *xdata)
{
    RDA_COMMON_MODIFICATION_FOP(removexattr, frame, this, loc->inode, xdata,
                                loc, name);
    return 0;
}

static int32_t
rda_fremovexattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    struct rda_local *local = NULL;

    if (op_ret < 0)
        goto unwind;

    local = frame->local;
    rda_mark_inode_dirty(this, local->inode);
    rda_inode_ctx_update_iatts(local->inode, this, NULL, NULL,
                               local->generation);
unwind:
    RDA_STACK_UNWIND(fremovexattr, frame, op_ret, op_errno, xdata);
    return 0;
}

static int32_t
rda_fremovexattr(call_frame_t *frame, xlator_t *this, fd_t *fd,
                 const char *name, dict_t *xdata)
{
    RDA_COMMON_MODIFICATION_FOP(fremovexattr, frame, this, fd->inode, xdata, fd,
                                name);
    return 0;
}

static int32_t
rda_releasedir(xlator_t *this, fd_t *fd)
{
    uint64_t val;
    struct rda_fd_ctx *ctx;

    if (fd_ctx_del(fd, this, &val) < 0)
        return -1;

    ctx = (struct rda_fd_ctx *)(uintptr_t)val;
    if (!ctx)
        return 0;

    rda_reset_ctx(this, ctx);

    if (ctx->fill_frame)
        STACK_DESTROY(ctx->fill_frame->root);

    if (ctx->stub)
        gf_msg(this->name, GF_LOG_ERROR, 0,
               READDIR_AHEAD_MSG_DIR_RELEASE_PENDING_STUB,
               "released a directory with a pending stub");

    GF_FREE(ctx);
    return 0;
}

static int
rda_forget(xlator_t *this, inode_t *inode)
{
    uint64_t ctx_uint = 0;
    rda_inode_ctx_t *ctx = NULL;

    inode_ctx_del1(inode, this, &ctx_uint);
    if (!ctx_uint)
        return 0;

    ctx = (rda_inode_ctx_t *)(uintptr_t)ctx_uint;

    GF_FREE(ctx);

    return 0;
}

int32_t
mem_acct_init(xlator_t *this)
{
    int ret = -1;

    if (!this)
        goto out;

    ret = xlator_mem_acct_init(this, gf_rda_mt_end + 1);

    if (ret != 0)
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, READDIR_AHEAD_MSG_NO_MEMORY,
               "Memory accounting init"
               "failed");

out:
    return ret;
}

int
reconfigure(xlator_t *this, dict_t *options)
{
    struct rda_priv *priv = this->private;

    GF_OPTION_RECONF("rda-request-size", priv->rda_req_size, options,
                     size_uint64, err);
    GF_OPTION_RECONF("rda-low-wmark", priv->rda_low_wmark, options, size_uint64,
                     err);
    GF_OPTION_RECONF("rda-high-wmark", priv->rda_high_wmark, options,
                     size_uint64, err);
    GF_OPTION_RECONF("rda-cache-limit", priv->rda_cache_limit, options,
                     size_uint64, err);
    GF_OPTION_RECONF("parallel-readdir", priv->parallel_readdir, options, bool,
                     err);
    GF_OPTION_RECONF("pass-through", this->pass_through, options, bool, err);

    return 0;
err:
    return -1;
}

int
init(xlator_t *this)
{
    struct rda_priv *priv = NULL;

    GF_VALIDATE_OR_GOTO("readdir-ahead", this, err);

    if (!this->children || this->children->next) {
        gf_msg(this->name, GF_LOG_ERROR, 0,
               READDIR_AHEAD_MSG_XLATOR_CHILD_MISCONFIGURED,
               "FATAL: readdir-ahead not configured with exactly one"
               " child");
        goto err;
    }

    if (!this->parents) {
        gf_msg(this->name, GF_LOG_WARNING, 0,
               READDIR_AHEAD_MSG_VOL_MISCONFIGURED,
               "dangling volume. check volfile ");
    }

    priv = GF_CALLOC(1, sizeof(struct rda_priv), gf_rda_mt_rda_priv);
    if (!priv)
        goto err;
    this->private = priv;

    GF_ATOMIC_INIT(priv->rda_cache_size, 0);

    this->local_pool = mem_pool_new(struct rda_local, 32);
    if (!this->local_pool)
        goto err;

    GF_OPTION_INIT("rda-request-size", priv->rda_req_size, size_uint64, err);
    GF_OPTION_INIT("rda-low-wmark", priv->rda_low_wmark, size_uint64, err);
    GF_OPTION_INIT("rda-high-wmark", priv->rda_high_wmark, size_uint64, err);
    GF_OPTION_INIT("rda-cache-limit", priv->rda_cache_limit, size_uint64, err);
    GF_OPTION_INIT("parallel-readdir", priv->parallel_readdir, bool, err);
    GF_OPTION_INIT("pass-through", this->pass_through, bool, err);

    return 0;

err:
    if (this->local_pool)
        mem_pool_destroy(this->local_pool);
    if (priv)
        GF_FREE(priv);

    return -1;
}

void
fini(xlator_t *this)
{
    GF_VALIDATE_OR_GOTO("readdir-ahead", this, out);

    GF_FREE(this->private);

out:
    return;
}

struct xlator_fops fops = {
    .opendir = rda_opendir,
    .readdirp = rda_readdirp,
    /* inode write */
    /* TODO: invalidate a dentry's stats if its pointing to a directory
     * when entry operations happen in that directory
     */
    .writev = rda_writev,
    .truncate = rda_truncate,
    .ftruncate = rda_ftruncate,
    .fallocate = rda_fallocate,
    .discard = rda_discard,
    .zerofill = rda_zerofill,
    /* metadata write */
    .setxattr = rda_setxattr,
    .fsetxattr = rda_fsetxattr,
    .setattr = rda_setattr,
    .fsetattr = rda_fsetattr,
    .removexattr = rda_removexattr,
    .fremovexattr = rda_fremovexattr,
};

struct xlator_cbks cbks = {
    .releasedir = rda_releasedir,
    .forget = rda_forget,
};

struct volume_options options[] = {
    {
        .key = {"readdir-ahead"},
        .type = GF_OPTION_TYPE_BOOL,
        .default_value = "off",
        .description = "enable/disable readdir-ahead",
        .op_version = {GD_OP_VERSION_6_0},
        .flags = OPT_FLAG_SETTABLE,
    },
    {
        .key = {"rda-request-size"},
        .type = GF_OPTION_TYPE_SIZET,
        .min = 4096,
        .max = 131072,
        .default_value = "131072",
        .description = "size of buffer in readdirp calls initiated by "
                       "readdir-ahead ",
    },
    {
        .key = {"rda-low-wmark"},
        .type = GF_OPTION_TYPE_SIZET,
        .min = 0,
        .max = 10 * GF_UNIT_MB,
        .default_value = "4096",
        .description = "the value under which readdir-ahead plugs",
    },
    {
        .key = {"rda-high-wmark"},
        .type = GF_OPTION_TYPE_SIZET,
        .min = 0,
        .max = 100 * GF_UNIT_MB,
        .default_value = "128KB",
        .description = "the value over which readdir-ahead unplugs",
    },
    {
        .key = {"rda-cache-limit"},
        .type = GF_OPTION_TYPE_SIZET,
        .min = 0,
        .max = INFINITY,
        .default_value = "10MB",
        .description = "maximum size of cache consumed by readdir-ahead "
                       "xlator. This value is global and total memory "
                       "consumption by readdir-ahead is capped by this "
                       "value, irrespective of the number/size of "
                       "directories cached",
    },
    {.key = {"parallel-readdir"},
     .type = GF_OPTION_TYPE_BOOL,
     .op_version = {GD_OP_VERSION_3_10_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
     .default_value = "off",
     .description = "If this option is enabled, the readdir operation "
                    "is performed in parallel on all the bricks, thus "
                    "improving the performance of readdir. Note that "
                    "the performance improvement is higher in large "
                    "clusters"},
    {.key = {"pass-through"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "false",
     .op_version = {GD_OP_VERSION_4_1_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC | OPT_FLAG_CLIENT_OPT,
     .tags = {"readdir-ahead"},
     .description = "Enable/Disable readdir ahead translator"},
    {.key = {NULL}},
};

xlator_api_t xlator_api = {
    .init = init,
    .fini = fini,
    .reconfigure = reconfigure,
    .mem_acct_init = mem_acct_init,
    .op_version = {1}, /* Present from the initial version */
    .fops = &fops,
    .cbks = &cbks,
    .options = options,
    .identifier = "readdir-ahead",
    .category = GF_MAINTAINED,
};
