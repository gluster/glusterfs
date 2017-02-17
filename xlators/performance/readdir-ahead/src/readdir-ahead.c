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

#include "glusterfs.h"
#include "xlator.h"
#include "call-stub.h"
#include "readdir-ahead.h"
#include "readdir-ahead-mem-types.h"
#include "defaults.h"
#include "readdir-ahead-messages.h"
static int rda_fill_fd(call_frame_t *, xlator_t *, fd_t *);

/*
 * Get (or create) the fd context for storing prepopulated directory
 * entries.
 */
static struct
rda_fd_ctx *get_rda_fd_ctx(fd_t *fd, xlator_t *this)
{
	uint64_t val;
	struct rda_fd_ctx *ctx;

	LOCK(&fd->lock);

	if (__fd_ctx_get(fd, this, &val) < 0) {
		ctx = GF_CALLOC(1, sizeof(struct rda_fd_ctx),
				gf_rda_mt_rda_fd_ctx);
		if (!ctx)
			goto out;

		LOCK_INIT(&ctx->lock);
		INIT_LIST_HEAD(&ctx->entries.list);
                ctx->state = RDA_FD_NEW;
		/* ctx offset values initialized to 0 */
                ctx->xattrs = NULL;

		if (__fd_ctx_set(fd, this, (uint64_t) ctx) < 0) {
			GF_FREE(ctx);
			ctx = NULL;
			goto out;
		}
	} else {
		ctx = (struct rda_fd_ctx *) val;
	}
out:
	UNLOCK(&fd->lock);
	return ctx;
}

/*
 * Reset the tracking state of the context.
 */
static void
rda_reset_ctx(struct rda_fd_ctx *ctx)
{
	ctx->state = RDA_FD_NEW;
	ctx->cur_offset = 0;
	ctx->cur_size = 0;
	ctx->next_offset = 0;
        ctx->op_errno = 0;
	gf_dirent_free(&ctx->entries);
        if (ctx->xattrs) {
                dict_unref (ctx->xattrs);
                ctx->xattrs = NULL;
        }
}

/*
 * Check whether we can handle a request. Offset verification is done by the
 * caller, so we only check whether the preload buffer has completion status
 * (including an error) or has some data to return.
 */
static gf_boolean_t
rda_can_serve_readdirp(struct rda_fd_ctx *ctx, size_t request_size)
{
	if ((ctx->state & RDA_FD_EOD) ||
	    (ctx->state & RDA_FD_ERROR) ||
	    (!(ctx->state & RDA_FD_PLUGGED) && (ctx->cur_size > 0)) ||
            (request_size && ctx->cur_size >= request_size))
		return _gf_true;

	return _gf_false;
}

/*
 * Serve a request from the fd dentry list based on the size of the request
 * buffer. ctx must be locked.
 */
static int32_t
__rda_fill_readdirp (xlator_t *this, gf_dirent_t *entries, size_t request_size,
		     struct rda_fd_ctx *ctx)
{
	gf_dirent_t     *dirent, *tmp;
	size_t           dirent_size, size = 0, inodectx_size = 0;
	int32_t          count             = 0;
	struct rda_priv *priv              = NULL;

        priv = this->private;

	list_for_each_entry_safe(dirent, tmp, &ctx->entries.list, list) {
		dirent_size = gf_dirent_size(dirent->d_name);
		if (size + dirent_size > request_size)
			break;

                inodectx_size = 0;

                inode_ctx_del (dirent->inode, this, (void *)&inodectx_size);

		size += dirent_size;
		list_del_init(&dirent->list);
		ctx->cur_size -= dirent_size;

                priv->rda_cache_size -= (dirent_size + inodectx_size);

		list_add_tail(&dirent->list, &entries->list);
		ctx->cur_offset = dirent->d_off;
		count++;
	}

	if (ctx->cur_size <= priv->rda_low_wmark)
		ctx->state |= RDA_FD_PLUGGED;

	return count;
}

static int32_t
__rda_serve_readdirp (xlator_t *this, struct rda_fd_ctx *ctx, size_t size,
                      gf_dirent_t *entries, int *op_errno)
{
        int32_t      ret     = 0;

        ret = __rda_fill_readdirp (this, entries, size, ctx);

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
        struct rda_fd_ctx   *ctx      = NULL;
        int                  fill     = 0;
        gf_dirent_t          entries;
        int                  ret      = 0;
        int                  op_errno = 0;
        gf_boolean_t         serve    = _gf_false;

	ctx = get_rda_fd_ctx(fd, this);
	if (!ctx)
		goto err;

	if (ctx->state & RDA_FD_BYPASS)
		goto bypass;

        INIT_LIST_HEAD (&entries.list);
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
		rda_reset_ctx(ctx);
                /*
                 * Unref and discard the 'list of xattrs to be fetched'
                 * stored during opendir call. This is done above - inside
                 * rda_reset_ctx().
                 * Now, ref the xdata passed by md-cache in actual readdirp()
                 * call and use that for all subsequent internal readdirp()
                 * requests issued by this xlator.
                 */
                ctx->xattrs = dict_ref (xdata);
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
	if (rda_can_serve_readdirp (ctx, size)) {
                ret = __rda_serve_readdirp (this, ctx, size, &entries,
                                            &op_errno);
                serve = _gf_true;
        } else {
                ctx->stub = fop_readdirp_stub (frame, NULL, fd, size, off,
                                               xdata);
                if (!ctx->stub) {
                        UNLOCK(&ctx->lock);
                        goto err;
                }

                if (!(ctx->state & RDA_FD_RUNNING)) {
                        fill = 1;
                        ctx->state |= RDA_FD_RUNNING;
                }
        }

	UNLOCK(&ctx->lock);

        if (serve) {
                STACK_UNWIND_STRICT (readdirp, frame, ret, op_errno, &entries,
                                     xdata);
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
rda_fill_fd_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		 int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
		 dict_t *xdata)
{
        gf_dirent_t       *dirent        = NULL;
        gf_dirent_t       *tmp           = NULL;
        gf_dirent_t        serve_entries;
        struct rda_local  *local         = frame->local;
        struct rda_fd_ctx *ctx           = local->ctx;
        struct rda_priv   *priv          = this->private;
        int                fill          = 1;
        size_t             inodectx_size = 0;
        size_t             dirent_size   = 0;
        int                ret           = 0;
        gf_boolean_t       serve         = _gf_false;
        call_stub_t       *stub          = NULL;

        INIT_LIST_HEAD (&serve_entries.list);
	LOCK(&ctx->lock);

	/* Verify that the preload buffer is still pending on this data. */
	if (ctx->next_offset != local->offset) {
		gf_msg(this->name, GF_LOG_ERROR,
                       0, READDIR_AHEAD_MSG_OUT_OF_SEQUENCE,
                       "Out of sequence directory preload.");
		ctx->state |= (RDA_FD_BYPASS|RDA_FD_ERROR);
		ctx->op_errno = EUCLEAN;

		goto out;
	}

	if (entries) {
		list_for_each_entry_safe(dirent, tmp, &entries->list, list) {
			list_del_init(&dirent->list);
			/* must preserve entry order */
			list_add_tail(&dirent->list, &ctx->entries.list);

                        dirent_size = gf_dirent_size (dirent->d_name);
                        inodectx_size = 0;

                        if (dirent->inode) {
                                inodectx_size = inode_ctx_size (dirent->inode);
                                inode_ctx_set (dirent->inode, this,
                                               (void *)inodectx_size);
                        }

			ctx->cur_size += dirent_size;

                        priv->rda_cache_size += (dirent_size + inodectx_size);

			ctx->next_offset = dirent->d_off;
		}
	}

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
	if (ctx->stub &&
	    rda_can_serve_readdirp (ctx, ctx->stub->args.size)) {
                ret = __rda_serve_readdirp (this, ctx, ctx->stub->args.size,
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
	if (!ctx->stub && ((ctx->state & RDA_FD_BYPASS)
                           || (priv->rda_cache_size > priv->rda_cache_limit)))
		ctx->state &= ~RDA_FD_RUNNING;

	if (!(ctx->state & RDA_FD_RUNNING)) {
		fill = 0;
                if (ctx->xattrs) {
                        /*
                         * fill = 0 and hence rda_fill_fd() won't be invoked.
                         * unref for ref taken in rda_fill_fd()
                         */
                        dict_unref (ctx->xattrs);
                        ctx->xattrs = NULL;
                }

		STACK_DESTROY(ctx->fill_frame->root);
		ctx->fill_frame = NULL;
	}

	UNLOCK(&ctx->lock);

        if (serve) {
                STACK_UNWIND_STRICT (readdirp, stub->frame, ret, op_errno,
                                     &serve_entries, xdata);
                gf_dirent_free (&serve_entries);
                call_stub_destroy (stub);
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
        int ret = 0;

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
		local->fd = fd;
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
        if (local->skip_dir) {
                ret = dict_set_int32 (ctx->xattrs, GF_READDIR_SKIP_DIRS, 1);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR,
                                0, READDIR_AHEAD_MSG_DICT_OP_FAILED,
                                "Dict set of key:%s failed with :%d",
                                GF_READDIR_SKIP_DIRS, ret);
                }
        }

	UNLOCK(&ctx->lock);

        STACK_WIND(nframe, rda_fill_fd_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->readdirp, fd,
                   priv->rda_req_size, offset, ctx->xattrs);

	return 0;

err:
	if (nframe)
		FRAME_DESTROY(nframe);

	return -1;
}


static int
rda_unpack_mdc_loaded_keys_to_dict(char *payload, dict_t *dict)
{
        int      ret = -1;
        char    *mdc_key = NULL;

        if (!payload || !dict) {
                goto out;
        }

        mdc_key = strtok(payload, " ");
        while (mdc_key != NULL) {
                ret = dict_set_int8 (dict, mdc_key, 0);
                if (ret) {
                        goto out;
                }
                mdc_key = strtok(NULL, " ");
        }

out:
        return ret;
}


static int32_t
rda_opendir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
		    int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        struct rda_local *local = frame->local;

	if (!op_ret)
		rda_fill_fd(frame, this, fd);

        frame->local = NULL;

	STACK_UNWIND_STRICT(opendir, frame, op_ret, op_errno, fd, xdata);

        if (local && local->xattrs) {
                /* unref for dict_new() done in rda_opendir */
                dict_unref (local->xattrs);
                local->xattrs = NULL;
        }

        if (local)
                mem_put (local);

	return 0;
}

static int32_t
rda_opendir(call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
		dict_t *xdata)
{
        int                  ret = -1;
        int                  op_errno = 0;
        char                *payload = NULL;
        struct rda_local    *local = NULL;
        dict_t              *xdata_from_req = NULL;

        if (xdata) {
                /*
                 * Retrieve list of keys set by md-cache xlator and store it
                 * in local to be consumed in rda_opendir_cbk
                 */
                ret = dict_get_str (xdata, GF_MDC_LOADED_KEY_NAMES, &payload);
                if (ret)
                        goto wind;

                xdata_from_req = dict_new();
                if (!xdata_from_req) {
                        op_errno = ENOMEM;
                        goto unwind;
                }

                ret = rda_unpack_mdc_loaded_keys_to_dict((char *) payload,
                                                         xdata_from_req);
                if (ret) {
                        dict_unref(xdata_from_req);
                        goto wind;
                }

                local = mem_get0(this->local_pool);
                if (!local) {
                        dict_unref(xdata_from_req);
                        op_errno = ENOMEM;
                        goto unwind;
                }

                local->xattrs = xdata_from_req;
                ret = dict_get_int32 (xdata, GF_READDIR_SKIP_DIRS, &local->skip_dir);
                frame->local = local;
        }

wind:
        if (xdata)
                /* Remove the key after consumption. */
                dict_del (xdata, GF_MDC_LOADED_KEY_NAMES);

        STACK_WIND(frame, rda_opendir_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->opendir, loc, fd, xdata);
        return 0;

unwind:
        STACK_UNWIND_STRICT(opendir, frame, -1, op_errno, fd, xdata);
        return 0;
}

static int32_t
rda_releasedir(xlator_t *this, fd_t *fd)
{
	uint64_t val;
	struct rda_fd_ctx *ctx;

	if (fd_ctx_del(fd, this, &val) < 0)
		return -1;

	ctx = (struct rda_fd_ctx *) val;
	if (!ctx)
		return 0;

	rda_reset_ctx(ctx);

	if (ctx->fill_frame)
		STACK_DESTROY(ctx->fill_frame->root);

	if (ctx->stub)
		gf_msg(this->name, GF_LOG_ERROR, 0,
		        READDIR_AHEAD_MSG_DIR_RELEASE_PENDING_STUB,
                       "released a directory with a pending stub");

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
		gf_msg(this->name, GF_LOG_ERROR, ENOMEM,
                       READDIR_AHEAD_MSG_NO_MEMORY, "Memory accounting init"
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
	GF_OPTION_RECONF("rda-low-wmark", priv->rda_low_wmark, options,
                         size_uint64, err);
	GF_OPTION_RECONF("rda-high-wmark", priv->rda_high_wmark, options,
                         size_uint64, err);
        GF_OPTION_RECONF("rda-cache-limit", priv->rda_cache_limit, options,
                         size_uint64, err);

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
                gf_msg(this->name,  GF_LOG_ERROR, 0,
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

	this->local_pool = mem_pool_new(struct rda_local, 32);
	if (!this->local_pool)
		goto err;

	GF_OPTION_INIT("rda-request-size", priv->rda_req_size, size_uint64,
                       err);
	GF_OPTION_INIT("rda-low-wmark", priv->rda_low_wmark, size_uint64, err);
	GF_OPTION_INIT("rda-high-wmark", priv->rda_high_wmark, size_uint64,
                       err);
        GF_OPTION_INIT("rda-cache-limit", priv->rda_cache_limit, size_uint64,
                       err);

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
        GF_VALIDATE_OR_GOTO ("readdir-ahead", this, out);

	GF_FREE(this->private);

out:
        return;
}

struct xlator_fops fops = {
	.opendir	= rda_opendir,
	.readdirp	= rda_readdirp,
};

struct xlator_cbks cbks = {
	.releasedir	= rda_releasedir,
};

struct volume_options options[] = {
	{ .key = {"rda-request-size"},
	  .type = GF_OPTION_TYPE_SIZET,
	  .min = 4096,
	  .max = 131072,
	  .default_value = "131072",
	  .description = "size of buffer in readdirp calls initiated by "
                         "readdir-ahead ",
	},
	{ .key = {"rda-low-wmark"},
	  .type = GF_OPTION_TYPE_SIZET,
	  .min = 0,
	  .max = 10 * GF_UNIT_MB,
	  .default_value = "4096",
	  .description = "the value under which readdir-ahead plugs",
	},
	{ .key = {"rda-high-wmark"},
	  .type = GF_OPTION_TYPE_SIZET,
	  .min = 0,
	  .max = 100 * GF_UNIT_MB,
	  .default_value = "128KB",
	  .description = "the value over which readdir-ahead unplugs",
	},
        { .key = {"rda-cache-limit"},
          .type = GF_OPTION_TYPE_SIZET,
          .min = 0,
          .max = 1 * GF_UNIT_GB,
          .default_value = "10MB",
          .description = "maximum size of cache consumed by readdir-ahead "
                         "xlator. This value is global and total memory "
                         "consumption by readdir-ahead is capped by this "
                         "value, irrespective of the number/size of "
                         "directories cached",
        },
        { .key = {NULL} },
};

