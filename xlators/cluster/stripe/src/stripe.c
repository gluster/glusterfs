/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/**
 * xlators/cluster/stripe:
 *    Stripe translator, stripes the data across its child nodes,
 *    as per the options given in the volfile. The striping works
 *    fairly simple. It writes files at different offset as per
 *    calculation. So, 'ls -l' output at the real posix level will
 *    show file size bigger than the actual size. But when one does
 *    'df' or 'du <file>', real size of the file on the server is shown.
 *
 * WARNING:
 *  Stripe translator can't regenerate data if a child node gets disconnected.
 *  So, no 'self-heal' for stripe. Hence the advice, use stripe only when its
 *  very much necessary, or else, use it in combination with AFR, to have a
 *  backup copy.
 */
#include <fnmatch.h>

#include "stripe.h"
#include "libxlator.h"
#include "byte-order.h"
#include "statedump.h"

struct volume_options options[];

int32_t
stripe_sh_chown_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
        int             callcnt = -1;
        stripe_local_t *local   = NULL;

        if (!this || !frame || !frame->local) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                STRIPE_STACK_DESTROY (frame);
        }
out:
        return 0;
}

int32_t
stripe_sh_make_entry_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, inode_t *inode,
                          struct iatt *buf, struct iatt *preparent,
                          struct iatt *postparent, dict_t *xdata)
{
        stripe_local_t *local = NULL;
        call_frame_t    *prev = NULL;

        if (!frame || !frame->local || !cookie || !this) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;

        STACK_WIND (frame, stripe_sh_chown_cbk, prev->this,
                    prev->this->fops->setattr, &local->loc,
                    &local->stbuf, (GF_SET_ATTR_UID | GF_SET_ATTR_GID), NULL);

out:
        return 0;
}

int32_t
stripe_entry_self_heal (call_frame_t *frame, xlator_t *this,
                        stripe_local_t *local)
{
        xlator_list_t    *trav   = NULL;
        call_frame_t     *rframe = NULL;
        stripe_local_t   *rlocal = NULL;
        stripe_private_t *priv   = NULL;
        dict_t           *xdata  = NULL;
        int               ret    = 0;

        if (!local || !this || !frame) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        if (!(IA_ISREG (local->stbuf.ia_type) ||
              IA_ISDIR (local->stbuf.ia_type)))
                return 0;

        priv = this->private;
        trav = this->children;
        rframe = copy_frame (frame);
        if (!rframe) {
                goto out;
        }
        rlocal = mem_get0 (this->local_pool);
        if (!rlocal) {
                goto out;
        }
        rframe->local = rlocal;
        rlocal->call_count = priv->child_count;
        loc_copy (&rlocal->loc, &local->loc);
        memcpy (&rlocal->stbuf, &local->stbuf, sizeof (struct iatt));

        xdata = dict_new ();
        if (!xdata)
                goto out;

        ret = dict_set_static_bin (xdata, "gfid-req", local->stbuf.ia_gfid, 16);
        if (ret)
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: failed to set gfid-req", local->loc.path);

        while (trav) {
                if (IA_ISREG (local->stbuf.ia_type)) {
                        STACK_WIND (rframe, stripe_sh_make_entry_cbk,
                                    trav->xlator, trav->xlator->fops->mknod,
                                    &local->loc,
                                    st_mode_from_ia (local->stbuf.ia_prot,
                                                     local->stbuf.ia_type),
                                    0, 0, xdata);
                }
                if (IA_ISDIR (local->stbuf.ia_type)) {
                        STACK_WIND (rframe, stripe_sh_make_entry_cbk,
                                    trav->xlator, trav->xlator->fops->mkdir,
                                    &local->loc,
                                    st_mode_from_ia (local->stbuf.ia_prot,
                                                     local->stbuf.ia_type),
                                    0, xdata);
                }
                trav = trav->next;
        }

        if (xdata)
                dict_unref (xdata);
        return 0;

out:
        if (rframe)
                STRIPE_STACK_DESTROY (rframe);
        if (xdata)
                dict_unref (xdata);

        return 0;
}


int32_t
stripe_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
        int32_t         callcnt     = 0;
        stripe_local_t *local       = NULL;
        call_frame_t   *prev        = NULL;
        int             ret         = 0;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        if (op_errno != ENOENT)
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "%s returned error %s",
                                        prev->this->name,
                                        strerror (op_errno));
                        if (local->op_errno != ESTALE)
                                local->op_errno = op_errno;
                        if (((op_errno != ENOENT) && (op_errno != ENOTCONN)) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                        if (op_errno == ENOENT)
                                local->entry_self_heal_needed = 1;
                }

                if (op_ret >= 0) {
                        local->op_ret = 0;
                        if (IA_ISREG (buf->ia_type)) {
                                ret = stripe_ctx_handle (this, prev, local,
                                                         xdata);
                                if (ret)
                                        gf_log (this->name, GF_LOG_ERROR,
                                                 "Error getting fctx info from"
                                                 " dict");
                        }

                        if (FIRST_CHILD(this) == prev->this) {
                                local->stbuf      = *buf;
                                local->postparent = *postparent;
                                local->inode = inode_ref (inode);
                                if (xdata)
                                        local->xdata = dict_ref (xdata);
                                if (local->xattr) {
                                        stripe_aggregate_xattr (local->xdata,
                                                                local->xattr);
                                        dict_unref (local->xattr);
                                        local->xattr = NULL;
                                }
                        }

                        if (!local->xdata && !local->xattr) {
                                local->xattr = dict_ref (xdata);
                        } else if (local->xdata) {
                                stripe_aggregate_xattr (local->xdata, xdata);
                        } else if (local->xattr) {
                                stripe_aggregate_xattr (local->xattr, xdata);
                        }

                        local->stbuf_blocks      += buf->ia_blocks;
                        local->postparent_blocks += postparent->ia_blocks;

			correct_file_size(buf, local->fctx, prev);

                        if (local->stbuf_size < buf->ia_size)
                                local->stbuf_size = buf->ia_size;
                        if (local->postparent_size < postparent->ia_size)
                                local->postparent_size = postparent->ia_size;

                        if (uuid_is_null (local->ia_gfid))
                                uuid_copy (local->ia_gfid, buf->ia_gfid);

                        /* Make sure the gfid on all the nodes are same */
                        if (uuid_compare (local->ia_gfid, buf->ia_gfid)) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "%s: gfid different on subvolume %s",
                                        local->loc.path, prev->this->name);
                        }
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->op_ret == 0 && local->entry_self_heal_needed &&
                    !uuid_is_null (local->loc.inode->gfid))
                        stripe_entry_self_heal (frame, this, local);

                if (local->failed)
                        local->op_ret = -1;

                if (local->op_ret != -1) {
                        local->stbuf.ia_blocks      = local->stbuf_blocks;
                        local->stbuf.ia_size        = local->stbuf_size;
                        local->postparent.ia_blocks = local->postparent_blocks;
                        local->postparent.ia_size   = local->postparent_size;
                        inode_ctx_put (local->inode, this,
                                       (uint64_t) (long)local->fctx);
                }

                STRIPE_STACK_UNWIND (lookup, frame, local->op_ret,
                                     local->op_errno, local->inode,
                                     &local->stbuf, local->xdata,
                                     &local->postparent);
        }
out:
        return 0;
}

int32_t
stripe_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
               dict_t *xdata)
{
        stripe_local_t   *local    = NULL;
        xlator_list_t    *trav     = NULL;
        stripe_private_t *priv     = NULL;
        int32_t           op_errno = EINVAL;
        int64_t           filesize = 0;
        int               ret      = 0;
        uint64_t          tmpctx   = 0;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        loc_copy (&local->loc, loc);

        inode_ctx_get (local->inode, this, &tmpctx);
        if (tmpctx)
                local->fctx = (stripe_fd_ctx_t*) (long)tmpctx;

        /* quick-read friendly changes */
        if (xdata && dict_get (xdata, GF_CONTENT_KEY)) {
                ret = dict_get_int64 (xdata, GF_CONTENT_KEY, &filesize);
                if (!ret && (filesize > priv->block_size))
                        dict_del (xdata, GF_CONTENT_KEY);
        }

        /* get stripe-size xattr on lookup. This would be required for
         * open/read/write/pathinfo calls. Hence we send down the request
         * even when type == IA_INVAL */

	/*
	 * We aren't guaranteed to have xdata here. We need the format info for
	 * the file, so allocate xdata if necessary.
	 */
	if (!xdata)
		xdata = dict_new();
	else
		xdata = dict_ref(xdata);

        if (xdata && (IA_ISREG (loc->inode->ia_type) ||
            (loc->inode->ia_type == IA_INVAL))) {
                ret = stripe_xattr_request_build (this, xdata, 8, 4, 4, 0);
                if (ret)
                        gf_log (this->name , GF_LOG_ERROR, "Failed to build"
                                " xattr request for %s", loc->path);

        }

        /* Everytime in stripe lookup, all child nodes
           should be looked up */
        local->call_count = priv->child_count;
        while (trav) {
                STACK_WIND (frame, stripe_lookup_cbk, trav->xlator,
                            trav->xlator->fops->lookup, loc, xdata);
                trav = trav->next;
        }

	dict_unref(xdata);

        return 0;
err:
        STRIPE_STACK_UNWIND (lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);
        return 0;
}


int32_t
stripe_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }
        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }

                if (op_ret == 0) {
                        local->op_ret = 0;

                        if (FIRST_CHILD(this) == prev->this) {
                                local->stbuf = *buf;
                        }

                        local->stbuf_blocks += buf->ia_blocks;

			correct_file_size(buf, local->fctx, prev);

                        if (local->stbuf_size < buf->ia_size)
                                local->stbuf_size = buf->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if (local->op_ret != -1) {
                        local->stbuf.ia_size   = local->stbuf_size;
                        local->stbuf.ia_blocks = local->stbuf_blocks;
                }

                STRIPE_STACK_UNWIND (stat, frame, local->op_ret,
                                     local->op_errno, &local->stbuf, NULL);
        }
out:
        return 0;
}

int32_t
stripe_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
	stripe_fd_ctx_t  *fctx = NULL;
        int32_t           op_errno = EINVAL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

	if (IA_ISREG(loc->inode->ia_type)) {
		inode_ctx_get(loc->inode, this, (uint64_t *) &fctx);
		if (!fctx)
			goto err;
		local->fctx = fctx;
	}

        while (trav) {
                STACK_WIND (frame, stripe_stat_cbk, trav->xlator,
                            trav->xlator->fops->stat, loc, NULL);
                trav = trav->next;
        }

        return 0;

err:
        STRIPE_STACK_UNWIND (stat, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
stripe_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct statvfs *stbuf, dict_t *xdata)
{
        stripe_local_t *local = NULL;
        int32_t         callcnt = 0;

        if (!this || !frame || !frame->local) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }
        local = frame->local;

        LOCK(&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret && (op_errno != ENOTCONN)) {
                        local->op_errno = op_errno;
                }
                if (op_ret == 0) {
                        struct statvfs *dict_buf = &local->statvfs_buf;
                        dict_buf->f_bsize   = stbuf->f_bsize;
                        dict_buf->f_frsize  = stbuf->f_frsize;
                        dict_buf->f_blocks += stbuf->f_blocks;
                        dict_buf->f_bfree  += stbuf->f_bfree;
                        dict_buf->f_bavail += stbuf->f_bavail;
                        dict_buf->f_files  += stbuf->f_files;
                        dict_buf->f_ffree  += stbuf->f_ffree;
                        dict_buf->f_favail += stbuf->f_favail;
                        dict_buf->f_fsid    = stbuf->f_fsid;
                        dict_buf->f_flag    = stbuf->f_flag;
                        dict_buf->f_namemax = stbuf->f_namemax;
                        local->op_ret = 0;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                STRIPE_STACK_UNWIND (statfs, frame, local->op_ret,
                                     local->op_errno, &local->statvfs_buf, NULL);
        }
out:
        return 0;
}

int32_t
stripe_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = EINVAL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);

        trav = this->children;
        priv = this->private;

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        local->op_errno = ENOTCONN;
        frame->local = local;

        local->call_count = priv->child_count;
        while (trav) {
                STACK_WIND (frame, stripe_statfs_cbk, trav->xlator,
                            trav->xlator->fops->statfs, loc, NULL);
                trav = trav->next;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (statfs, frame, -1, op_errno, NULL, NULL);
        return 0;
}



int32_t
stripe_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }

                if (op_ret == 0) {
                        local->op_ret = 0;
                        if (FIRST_CHILD(this) == prev->this) {
                                local->pre_buf  = *prebuf;
                                local->post_buf = *postbuf;
                        }

                        local->prebuf_blocks  += prebuf->ia_blocks;
                        local->postbuf_blocks += postbuf->ia_blocks;

			correct_file_size(prebuf, local->fctx, prev);
			correct_file_size(postbuf, local->fctx, prev);

                        if (local->prebuf_size < prebuf->ia_size)
                                local->prebuf_size = prebuf->ia_size;

                        if (local->postbuf_size < postbuf->ia_size)
                                local->postbuf_size = postbuf->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if (local->op_ret != -1) {
                        local->pre_buf.ia_blocks  = local->prebuf_blocks;
                        local->pre_buf.ia_size    = local->prebuf_size;
                        local->post_buf.ia_blocks = local->postbuf_blocks;
                        local->post_buf.ia_size   = local->postbuf_size;
                }

                STRIPE_STACK_UNWIND (truncate, frame, local->op_ret,
                                     local->op_errno, &local->pre_buf,
                                     &local->post_buf, NULL);
        }
out:
        return 0;
}

int32_t
stripe_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset, dict_t *xdata)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
	stripe_fd_ctx_t  *fctx = NULL;
        int32_t           op_errno = EINVAL;
	int		  i, eof_idx;
	off_t		  dest_offset, tmp_offset;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

	inode_ctx_get(loc->inode, this, (uint64_t *) &fctx);
	if (!fctx) {
		gf_log(this->name, GF_LOG_ERROR, "no stripe context");
		op_errno = EINVAL;
		goto err;
	}

	local->fctx = fctx;
	eof_idx = (offset / fctx->stripe_size) % fctx->stripe_count;

	for (i = 0; i < fctx->stripe_count; i++) {
		if (!fctx->xl_array[i]) {
			gf_log(this->name, GF_LOG_ERROR,
				"no xlator at index %d", i);
			op_errno = EINVAL;
			goto err;
		}

		if (fctx->stripe_coalesce) {
			/*
	 		 * The node that owns EOF is truncated to the exact
	 		 * coalesced offset. Nodes prior to this index should
	 		 * be rounded up to the size of the complete stripe,
	 		 * while nodes after this index should be rounded down
			 * to the size of the previous stripe.
			 */
			if (i < eof_idx)
				tmp_offset = roof(offset, fctx->stripe_size *
						fctx->stripe_count);
			else if (i > eof_idx)
				tmp_offset = floor(offset, fctx->stripe_size *
						fctx->stripe_count);
			else
				tmp_offset = offset;

			dest_offset = coalesced_offset(tmp_offset,
					fctx->stripe_size, fctx->stripe_count);
		} else {
			dest_offset = offset;
		}

		STACK_WIND(frame, stripe_truncate_cbk, fctx->xl_array[i],
			fctx->xl_array[i]->fops->truncate, loc, dest_offset,
			NULL);
	}

        return 0;
err:
        STRIPE_STACK_UNWIND (truncate, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int32_t
stripe_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno,
                    struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }

                if (op_ret == 0) {
                        local->op_ret = 0;

                        if (FIRST_CHILD(this) == prev->this) {
                                local->pre_buf  = *preop;
                                local->post_buf = *postop;
                        }

                        local->prebuf_blocks  += preop->ia_blocks;
                        local->postbuf_blocks += postop->ia_blocks;

			correct_file_size(preop, local->fctx, prev);
			correct_file_size(postop, local->fctx, prev);

                        if (local->prebuf_size < preop->ia_size)
                                local->prebuf_size = preop->ia_size;
                        if (local->postbuf_size < postop->ia_size)
                                local->postbuf_size = postop->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if (local->op_ret != -1) {
                        local->pre_buf.ia_blocks  = local->prebuf_blocks;
                        local->pre_buf.ia_size    = local->prebuf_size;
                        local->post_buf.ia_blocks = local->postbuf_blocks;
                        local->post_buf.ia_size   = local->postbuf_size;
                }

                STRIPE_STACK_UNWIND (setattr, frame, local->op_ret,
                                     local->op_errno, &local->pre_buf,
                                     &local->post_buf, NULL);
        }
out:
        return 0;
}


int32_t
stripe_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
	stripe_fd_ctx_t	 *fctx = NULL;
        int32_t           op_errno = EINVAL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        if (!IA_ISDIR (loc->inode->ia_type) &&
            !IA_ISREG (loc->inode->ia_type)) {
                local->call_count = 1;
                STACK_WIND (frame, stripe_setattr_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->setattr,
                            loc, stbuf, valid, NULL);
                return 0;
        }

	if (IA_ISREG(loc->inode->ia_type)) {
		inode_ctx_get(loc->inode, this, (uint64_t *) &fctx);
		if (!fctx)
			goto err;
		local->fctx = fctx;
	}

        local->call_count = priv->child_count;
        while (trav) {
                STACK_WIND (frame, stripe_setattr_cbk,
                            trav->xlator, trav->xlator->fops->setattr,
                            loc, stbuf, valid, NULL);
                trav = trav->next;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (setattr, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int32_t
stripe_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = EINVAL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        priv = this->private;
        trav = this->children;

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_setattr_cbk, trav->xlator,
                            trav->xlator->fops->fsetattr, fd, stbuf, valid, NULL);
                trav = trav->next;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (fsetattr, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

int32_t
stripe_stack_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *buf,
                         struct iatt *preoldparent, struct iatt *postoldparent,
                         struct iatt *prenewparent, struct iatt *postnewparent,
                         dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }

                if (op_ret == 0) {
                        local->op_ret = 0;

                        local->stbuf.ia_blocks      += buf->ia_blocks;
                        local->preparent.ia_blocks  += preoldparent->ia_blocks;
                        local->postparent.ia_blocks += postoldparent->ia_blocks;
                        local->pre_buf.ia_blocks    += prenewparent->ia_blocks;
                        local->post_buf.ia_blocks   += postnewparent->ia_blocks;

			correct_file_size(buf, local->fctx, prev);

                        if (local->stbuf.ia_size < buf->ia_size)
                                local->stbuf.ia_size =  buf->ia_size;

                        if (local->preparent.ia_size < preoldparent->ia_size)
                                local->preparent.ia_size = preoldparent->ia_size;

                        if (local->postparent.ia_size < postoldparent->ia_size)
                                local->postparent.ia_size = postoldparent->ia_size;

                        if (local->pre_buf.ia_size < prenewparent->ia_size)
                                local->pre_buf.ia_size = prenewparent->ia_size;

                        if (local->post_buf.ia_size < postnewparent->ia_size)
                                local->post_buf.ia_size = postnewparent->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                STRIPE_STACK_UNWIND (rename, frame, local->op_ret, local->op_errno,
                                     &local->stbuf, &local->preparent,
                                     &local->postparent,  &local->pre_buf,
                                     &local->post_buf, NULL);
        }
out:
        return 0;
}

int32_t
stripe_first_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *buf,
                         struct iatt *preoldparent, struct iatt *postoldparent,
                         struct iatt *prenewparent, struct iatt *postnewparent,
                         dict_t *xdata)
{
        stripe_local_t *local = NULL;
        xlator_list_t  *trav = NULL;

        if (!this || !frame || !frame->local) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                op_errno = EINVAL;
                goto unwind;
        }

        if (op_ret == -1) {
                goto unwind;
        }

        local = frame->local;
        trav = this->children;

        local->stbuf      = *buf;
        local->preparent  = *preoldparent;
        local->postparent = *postoldparent;
        local->pre_buf    = *prenewparent;
        local->post_buf   = *postnewparent;

        local->op_ret = 0;
        local->call_count--;

        trav = trav->next; /* Skip first child */
        while (trav) {
                STACK_WIND (frame, stripe_stack_rename_cbk,
                            trav->xlator, trav->xlator->fops->rename,
                            &local->loc, &local->loc2, NULL);
                trav = trav->next;
        }
        return 0;

unwind:
        STRIPE_STACK_UNWIND (rename, frame, -1, op_errno, buf, preoldparent,
                             postoldparent, prenewparent, postnewparent, NULL);
        return 0;
}

int32_t
stripe_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
               loc_t *newloc, dict_t *xdata)
{
        stripe_private_t *priv = NULL;
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
	stripe_fd_ctx_t	 *fctx = NULL;
        int32_t           op_errno = EINVAL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (oldloc, err);
        VALIDATE_OR_GOTO (oldloc->path, err);
        VALIDATE_OR_GOTO (oldloc->inode, err);
        VALIDATE_OR_GOTO (newloc, err);

        priv = this->private;
        trav = this->children;

        /* If any one node is down, don't allow rename */
        if (priv->nodes_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        loc_copy (&local->loc, oldloc);
        loc_copy (&local->loc2, newloc);

        local->call_count = priv->child_count;

	if (IA_ISREG(oldloc->inode->ia_type)) {
		inode_ctx_get(oldloc->inode, this, (uint64_t *) &fctx);
		if (!fctx)
			goto err;
		local->fctx = fctx;
	}

        frame->local = local;

        STACK_WIND (frame, stripe_first_rename_cbk, trav->xlator,
                    trav->xlator->fops->rename, oldloc, newloc, NULL);

        return 0;
err:
        STRIPE_STACK_UNWIND (rename, frame, -1, op_errno, NULL, NULL, NULL,
                             NULL, NULL, NULL);
        return 0;
}
int32_t
stripe_first_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                         struct iatt *postparent, dict_t *xdata)
{
        stripe_local_t *local   = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "%s returned %s",
                        prev->this->name, strerror (op_errno));
                goto out;
        }
        local->op_ret = 0;
        local->preparent  = *preparent;
        local->postparent = *postparent;
        local->preparent_blocks  += preparent->ia_blocks;
        local->postparent_blocks += postparent->ia_blocks;

        STRIPE_STACK_UNWIND(unlink, frame, local->op_ret, local->op_errno,
                            &local->preparent, &local->postparent, xdata);
        return 0;
out:
        STRIPE_STACK_UNWIND (unlink, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}




int32_t
stripe_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t *local   = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG, "%s returned %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if (op_errno != ENOENT) {
                                local->failed = 1;
                                local->op_ret = op_ret;
                        }
                }
        }
        UNLOCK (&frame->lock);

        if (callcnt == 1) {
                if (local->failed) {
                        op_errno = local->op_errno;
                        goto out;
                }
                STACK_WIND(frame, stripe_first_unlink_cbk, FIRST_CHILD (this),
                           FIRST_CHILD (this)->fops->unlink, &local->loc,
                           local->xflag, local->xdata);
        }
        return 0;
out:
        STRIPE_STACK_UNWIND (unlink, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

int32_t
stripe_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc,
               int xflag, dict_t *xdata)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = EINVAL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Don't unlink a file if a node is down */
        if (priv->nodes_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        loc_copy (&local->loc, loc);
        local->xflag = xflag;

        if (xdata)
            local->xdata = dict_ref (xdata);

        frame->local = local;
        local->call_count = priv->child_count;
        trav = trav->next; /* Skip the first child */

        while (trav) {
                STACK_WIND (frame, stripe_unlink_cbk,
                            trav->xlator, trav->xlator->fops->unlink,
                            loc, xflag, xdata);
                trav = trav->next;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (unlink, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int32_t
stripe_first_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno,struct iatt *preparent,
                        struct iatt *postparent, dict_t *xdata)
{
        stripe_local_t *local = NULL;

        if (!this || !frame || !frame->local) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                op_errno = EINVAL;
                goto err;
        }

        if (op_ret == -1) {
                goto err;
        }

        local = frame->local;
        local->op_ret = 0;

        local->call_count--; /* First child successful */

        local->preparent  = *preparent;
        local->postparent = *postparent;
        local->preparent_size  = preparent->ia_size;
        local->postparent_size = postparent->ia_size;
        local->preparent_blocks  += preparent->ia_blocks;
        local->postparent_blocks += postparent->ia_blocks;

        STRIPE_STACK_UNWIND (rmdir, frame, local->op_ret, local->op_errno,
                             &local->preparent, &local->postparent, xdata);
        return 0;
err:
        STRIPE_STACK_UNWIND (rmdir, frame, op_ret, op_errno, NULL, NULL, NULL);
        return 0;

}

int32_t
stripe_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t *local   = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG, "%s returned %s",
                                prev->this->name, strerror (op_errno));
                        if (op_errno != ENOENT)
                                local->failed = 1;
                }
        }
        UNLOCK (&frame->lock);

        if (callcnt == 1) {
                if (local->failed)
                        goto out;
                STACK_WIND (frame, stripe_first_rmdir_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->rmdir, &local->loc,
                            local->flags, NULL);
        }
        return 0;
out:
        STRIPE_STACK_UNWIND (rmdir, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

int32_t
stripe_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags, dict_t *xdata)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = EINVAL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        /* don't delete a directory if any of the subvolume is down */
        if (priv->nodes_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        loc_copy (&local->loc, loc);
        local->flags = flags;
        local->call_count = priv->child_count;
        trav = trav->next; /* skip the first child */

        while (trav) {
                STACK_WIND (frame, stripe_rmdir_cbk,  trav->xlator,
                            trav->xlator->fops->rmdir, loc, flags, NULL);
                trav = trav->next;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (rmdir, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int32_t
stripe_mknod_ifreg_fail_unlink_cbk (call_frame_t *frame, void *cookie,
                                    xlator_t *this, int32_t op_ret,
                                    int32_t op_errno, struct iatt *preparent,
                                    struct iatt *postparent, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;

        if (!this || !frame || !frame->local) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                STRIPE_STACK_UNWIND (mknod, frame, local->op_ret, local->op_errno,
                                     local->inode, &local->stbuf,
                                     &local->preparent, &local->postparent, NULL);
        }
out:
        return 0;
}


/**
 */
int32_t
stripe_mknod_ifreg_setxattr_cbk (call_frame_t *frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno, dict_t *xdata)
{
        int32_t           callcnt = 0;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
        call_frame_t     *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        priv  = this->private;
        local = frame->local;

	LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_ret = -1;
                        local->op_errno = op_errno;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->op_ret == -1) {
                        local->call_count = priv->child_count;
                        while (trav) {
                                STACK_WIND (frame,
                                            stripe_mknod_ifreg_fail_unlink_cbk,
                                            trav->xlator,
                                            trav->xlator->fops->unlink,
                                            &local->loc, 0, NULL);
                                trav = trav->next;
                        }
                        return 0;
                }

                STRIPE_STACK_UNWIND (mknod, frame, local->op_ret, local->op_errno,
                                     local->inode, &local->stbuf,
                                     &local->preparent, &local->postparent, NULL);
        }
out:
        return 0;
}

int32_t
stripe_mknod_ifreg_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, inode_t *inode,
                        struct iatt *buf, struct iatt *preparent,
                        struct iatt *postparent, dict_t *xdata)
{
        int32_t           callcnt = 0;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        call_frame_t     *prev = NULL;
        xlator_list_t    *trav = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        priv  = this->private;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                        local->op_errno = op_errno;
                }
                if (op_ret >= 0) {
                        local->op_ret = op_ret;

                        /* Can be used as a mechanism to understand if mknod
                           was successful in at least one place */
                        if (uuid_is_null (local->ia_gfid))
                                uuid_copy (local->ia_gfid, buf->ia_gfid);

			if (stripe_ctx_handle(this, prev, local, xdata))
				gf_log(this->name, GF_LOG_ERROR,
					"Error getting fctx info from dict");

                        local->stbuf_blocks += buf->ia_blocks;
                        local->preparent_blocks  += preparent->ia_blocks;
                        local->postparent_blocks += postparent->ia_blocks;

			correct_file_size(buf, local->fctx, prev);

                        if (local->stbuf_size < buf->ia_size)
                                local->stbuf_size = buf->ia_size;
                        if (local->preparent_size < preparent->ia_size)
                                local->preparent_size = preparent->ia_size;
                        if (local->postparent_size < postparent->ia_size)
                                local->postparent_size = postparent->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if ((local->op_ret == -1) && !uuid_is_null (local->ia_gfid)) {
                        /* ia_gfid set means, at least on one node 'mknod'
                           is successful */
                        local->call_count = priv->child_count;
                        trav = this->children;
                        while (trav) {
                                STACK_WIND (frame,
                                            stripe_mknod_ifreg_fail_unlink_cbk,
                                            trav->xlator,
                                            trav->xlator->fops->unlink,
                                            &local->loc, 0, NULL);
                                trav = trav->next;
                        }
                        return 0;
                }


                if (local->op_ret != -1) {
                        local->preparent.ia_blocks  = local->preparent_blocks;
                        local->preparent.ia_size    = local->preparent_size;
                        local->postparent.ia_blocks = local->postparent_blocks;
                        local->postparent.ia_size   = local->postparent_size;
                        local->stbuf.ia_size        = local->stbuf_size;
                        local->stbuf.ia_blocks      = local->stbuf_blocks;
                        inode_ctx_put (local->inode, this,
                                       (uint64_t)(long) local->fctx);

                }
                STRIPE_STACK_UNWIND (mknod, frame, local->op_ret, local->op_errno,
                                     local->inode, &local->stbuf,
                                     &local->preparent, &local->postparent, NULL);
        }
out:
        return 0;
}


int32_t
stripe_mknod_first_ifreg_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, inode_t *inode,
                        struct iatt *buf, struct iatt *preparent,
                        struct iatt *postparent, dict_t *xdata)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        call_frame_t     *prev = NULL;
        xlator_list_t    *trav = NULL;
        int               i    = 1;
        dict_t           *dict           = NULL;
        int               ret            = 0;
        int               need_unref     = 0;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        priv  = this->private;
        local = frame->local;
        trav = this->children;

        local->call_count--;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "%s returned error %s",
                        prev->this->name, strerror (op_errno));
                local->failed = 1;
                local->op_errno = op_errno;
                goto out;
        }

        local->op_ret = op_ret;

        local->stbuf      = *buf;
        local->preparent  = *preparent;
        local->postparent = *postparent;

        if (uuid_is_null (local->ia_gfid))
                uuid_copy (local->ia_gfid, buf->ia_gfid);
        local->preparent.ia_blocks  = local->preparent_blocks;
        local->preparent.ia_size    = local->preparent_size;
        local->postparent.ia_blocks = local->postparent_blocks;
        local->postparent.ia_size   = local->postparent_size;
        local->stbuf.ia_size        = local->stbuf_size;
        local->stbuf.ia_blocks      = local->stbuf_blocks;

        trav = trav->next;
        while (trav) {
                if (priv->xattr_supported) {
                        dict = dict_new ();
                        if (!dict) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "failed to allocate dict %s", local->loc.path);
                        }
                        need_unref = 1;

                        dict_copy (local->xattr, dict);

                        ret = stripe_xattr_request_build (this, dict,
                                                          local->stripe_size,
                                                          priv->child_count, i,
							  priv->coalesce);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Failed to build xattr request");

                } else {
                        dict = local->xattr;
                }

                STACK_WIND (frame, stripe_mknod_ifreg_cbk,
                            trav->xlator, trav->xlator->fops->mknod,
                            &local->loc, local->mode, local->rdev, 0, dict);
                trav = trav->next;
                i++;

                if (dict && need_unref)
                        dict_unref (dict);
        }

        return 0;

out:

       STRIPE_STACK_UNWIND (mknod, frame, op_ret, op_errno, NULL, NULL, NULL, NULL, NULL);
       return 0;
}


int32_t
stripe_single_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, inode_t *inode,
                         struct iatt *buf, struct iatt *preparent,
                         struct iatt *postparent, dict_t *xdata)
{
        STRIPE_STACK_UNWIND (mknod, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
stripe_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
              dev_t rdev, mode_t umask, dict_t *xdata)
{
        stripe_private_t *priv           = NULL;
        stripe_local_t   *local          = NULL;
        int32_t           op_errno       = EINVAL;
        int32_t           i              = 0;
        dict_t           *dict           = NULL;
        int               ret            = 0;
        int               need_unref     = 0;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        if (S_ISREG(mode)) {
                /* NOTE: on older kernels (older than 2.6.9),
                   creat() fops is sent as mknod() + open(). Hence handling
                   S_IFREG files is necessary */
                if (priv->nodes_down) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Some node down, returning EIO");
                        op_errno = EIO;
                        goto err;
                }

                /* Initialization */
                local = mem_get0 (this->local_pool);
                if (!local) {
                        op_errno = ENOMEM;
                        goto err;
                }
                local->op_ret = -1;
                local->op_errno = ENOTCONN;
                local->stripe_size = stripe_get_matching_bs (loc->path, priv);
                frame->local = local;
                local->inode = inode_ref (loc->inode);
                loc_copy (&local->loc, loc);
                local->xattr = dict_copy_with_ref (xdata, NULL);
                local->mode = mode;
                local->umask = umask;
                local->rdev = rdev;

                /* Everytime in stripe lookup, all child nodes should
                   be looked up */
                local->call_count = priv->child_count;

                if (priv->xattr_supported) {
                        dict = dict_new ();
                        if (!dict) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "failed to allocate dict %s", loc->path);
                        }
                        need_unref = 1;

                        dict_copy (xdata, dict);

                        ret = stripe_xattr_request_build (this, dict,
                                                          local->stripe_size,
                                                          priv->child_count,
                                                          i, priv->coalesce);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "failed to build xattr request");
                } else {
                        dict = xdata;
                }

                STACK_WIND (frame, stripe_mknod_first_ifreg_cbk,
                            FIRST_CHILD (this), FIRST_CHILD (this)->fops->mknod,
                            loc, mode, rdev, umask, dict);

                        if (dict && need_unref)
                                dict_unref (dict);
                return 0;
        }

        STACK_WIND (frame, stripe_single_mknod_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->mknod,
                    loc, mode, rdev, umask, xdata);

        return 0;
err:
        STRIPE_STACK_UNWIND (mknod, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);
        return 0;
}


int32_t
stripe_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t  *local   = NULL;
        call_frame_t    *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }

                if (op_ret >= 0) {
                        local->op_ret = 0;

                        local->stbuf_blocks      += buf->ia_blocks;
                        local->preparent_blocks  += preparent->ia_blocks;
                        local->postparent_blocks += postparent->ia_blocks;

                        if (local->stbuf_size < buf->ia_size)
                                local->stbuf_size = buf->ia_size;
                        if (local->preparent_size < preparent->ia_size)
                                local->preparent_size = preparent->ia_size;
                        if (local->postparent_size < postparent->ia_size)
                                local->postparent_size = postparent->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed != -1) {
                        local->preparent.ia_blocks  = local->preparent_blocks;
                        local->preparent.ia_size    = local->preparent_size;
                        local->postparent.ia_blocks = local->postparent_blocks;
                        local->postparent.ia_size   = local->postparent_size;
                        local->stbuf.ia_size        = local->stbuf_size;
                        local->stbuf.ia_blocks      = local->stbuf_blocks;
                }
                STRIPE_STACK_UNWIND (mkdir, frame, local->op_ret,
                                     local->op_errno, local->inode,
                                     &local->stbuf, &local->preparent,
                                     &local->postparent, NULL);
        }
out:
        return 0;
}


int32_t
stripe_first_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, inode_t *inode,
                        struct iatt *buf, struct iatt *preparent,
                        struct iatt *postparent, dict_t *xdata)
{
        stripe_local_t  *local   = NULL;
        call_frame_t    *prev = NULL;
        xlator_list_t        *trav = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;
        trav = this->children;

        local->call_count--; /* first child is successful */
        trav = trav->next;   /* skip first child */

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "%s returned error %s",
                        prev->this->name, strerror (op_errno));
                local->op_errno = op_errno;
                goto out;
        }

        local->op_ret = 0;

        local->inode      = inode_ref (inode);
        local->stbuf      = *buf;
        local->postparent = *postparent;
        local->preparent  = *preparent;

        local->stbuf_blocks      += buf->ia_blocks;
        local->preparent_blocks  += preparent->ia_blocks;
        local->postparent_blocks += postparent->ia_blocks;

        local->stbuf_size = buf->ia_size;
        local->preparent_size = preparent->ia_size;
        local->postparent_size = postparent->ia_size;

        while (trav) {
                STACK_WIND (frame, stripe_mkdir_cbk, trav->xlator,
                            trav->xlator->fops->mkdir, &local->loc, local->mode,
                            local->umask, local->xdata);
                trav = trav->next;
        }
        return 0;
out:
        STRIPE_STACK_UNWIND (mkdir, frame, -1, op_errno, NULL, NULL, NULL,
                             NULL, NULL);

        return 0;

}


int
stripe_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
              mode_t umask, dict_t *xdata)
{
        stripe_private_t *priv = NULL;
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        local->call_count = priv->child_count;
        if (xdata)
                local->xdata = dict_ref (xdata);
        local->mode  = mode;
        local->umask = umask;
        loc_copy (&local->loc, loc);
        frame->local = local;

        /* Everytime in stripe lookup, all child nodes should be looked up */
        STACK_WIND (frame, stripe_first_mkdir_cbk, trav->xlator,
                    trav->xlator->fops->mkdir, loc, mode, umask, xdata);

        return 0;
err:
        STRIPE_STACK_UNWIND (mkdir, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);
        return 0;
}


int32_t
stripe_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *buf, struct iatt *preparent,
                 struct iatt *postparent, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t  *local   = NULL;
        call_frame_t    *prev = NULL;
	stripe_fd_ctx_t *fctx = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }

                if (op_ret >= 0) {
                        local->op_ret = 0;

			if (IA_ISREG(inode->ia_type)) {
				inode_ctx_get(inode, this, (uint64_t *) &fctx);
				if (!fctx) {
					gf_log(this->name, GF_LOG_ERROR,
						"failed to get stripe context");
					op_ret = -1;
					op_errno = EINVAL;
				}
			}

                        if (FIRST_CHILD(this) == prev->this) {
                                local->inode      = inode_ref (inode);
                                local->stbuf      = *buf;
                                local->postparent = *postparent;
                                local->preparent  = *preparent;
                        }
                        local->stbuf_blocks      += buf->ia_blocks;
                        local->preparent_blocks  += preparent->ia_blocks;
                        local->postparent_blocks += postparent->ia_blocks;

			correct_file_size(buf, fctx, prev);

                        if (local->stbuf_size < buf->ia_size)
                                local->stbuf_size = buf->ia_size;
                        if (local->preparent_size < preparent->ia_size)
                                local->preparent_size = preparent->ia_size;
                        if (local->postparent_size < postparent->ia_size)
                                local->postparent_size = postparent->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if (local->op_ret != -1) {
                        local->preparent.ia_blocks  = local->preparent_blocks;
                        local->preparent.ia_size    = local->preparent_size;
                        local->postparent.ia_blocks = local->postparent_blocks;
                        local->postparent.ia_size   = local->postparent_size;
                        local->stbuf.ia_size        = local->stbuf_size;
                        local->stbuf.ia_blocks      = local->stbuf_blocks;
                }
                STRIPE_STACK_UNWIND (link, frame, local->op_ret,
                                     local->op_errno, local->inode,
                                     &local->stbuf, &local->preparent,
                                     &local->postparent, NULL);
        }
out:
        return 0;
}

int32_t
stripe_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (oldloc, err);
        VALIDATE_OR_GOTO (oldloc->path, err);
        VALIDATE_OR_GOTO (oldloc->inode, err);

        priv = this->private;
        trav = this->children;

        /* If any one node is down, don't allow link operation */
        if (priv->nodes_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        /* Everytime in stripe lookup, all child
           nodes should be looked up */
        while (trav) {
                STACK_WIND (frame, stripe_link_cbk,
                            trav->xlator, trav->xlator->fops->link,
                            oldloc, newloc, NULL);
                trav = trav->next;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (link, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);
        return 0;
}

int32_t
stripe_create_fail_unlink_cbk (call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret,
                               int32_t op_errno, struct iatt *preparent,
                               struct iatt *postparent, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;

        if (!this || !frame || !frame->local) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                STRIPE_STACK_UNWIND (create, frame, local->op_ret, local->op_errno,
                                     local->fd, local->inode, &local->stbuf,
                                     &local->preparent, &local->postparent, NULL);
        }
out:
        return 0;
}


int32_t
stripe_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd,
                   inode_t *inode, struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        int32_t           callcnt = 0;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        call_frame_t     *prev = NULL;
        xlator_list_t    *trav = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        priv  = this->private;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->failed = 1;
                        local->op_errno = op_errno;
                }

                if (op_ret >= 0) {
			if (IA_ISREG(buf->ia_type)) {
				if (stripe_ctx_handle(this, prev, local, xdata))
					gf_log(this->name, GF_LOG_ERROR,
						"Error getting fctx info from "
						"dict");
			}

                        local->op_ret = op_ret;

                        local->stbuf_blocks += buf->ia_blocks;
                        local->preparent_blocks  += preparent->ia_blocks;
                        local->postparent_blocks += postparent->ia_blocks;

			correct_file_size(buf, local->fctx, prev);

                        if (local->stbuf_size < buf->ia_size)
                                local->stbuf_size = buf->ia_size;
                        if (local->preparent_size < preparent->ia_size)
                                local->preparent_size = preparent->ia_size;
                        if (local->postparent_size < postparent->ia_size)
                                local->postparent_size = postparent->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if (local->op_ret == -1) {
                        local->call_count = priv->child_count;
                        trav = this->children;
                        while (trav) {
                                STACK_WIND (frame,
                                            stripe_create_fail_unlink_cbk,
                                            trav->xlator,
                                            trav->xlator->fops->unlink,
                                            &local->loc, 0, NULL);
                                trav = trav->next;
                        }

                        return 0;
                }

                if (local->op_ret >= 0) {
                        local->preparent.ia_blocks  = local->preparent_blocks;
                        local->preparent.ia_size    = local->preparent_size;
                        local->postparent.ia_blocks = local->postparent_blocks;
                        local->postparent.ia_size   = local->postparent_size;
                        local->stbuf.ia_size        = local->stbuf_size;
                        local->stbuf.ia_blocks      = local->stbuf_blocks;

			stripe_copy_xl_array(local->fctx->xl_array,
				             priv->xl_array,
					     local->fctx->stripe_count);
			inode_ctx_put(local->inode, this,
					(uint64_t) local->fctx);
                }

                /* Create itself has failed.. so return
                   without setxattring */
                STRIPE_STACK_UNWIND (create, frame, local->op_ret,
                                     local->op_errno, local->fd,
                                     local->inode, &local->stbuf,
                                     &local->preparent, &local->postparent, NULL);
        }

out:
        return 0;
}



int32_t
stripe_first_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd,
                   inode_t *inode, struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        call_frame_t     *prev = NULL;
        xlator_list_t    *trav = NULL;
        int               i    = 1;
        dict_t           *dict = NULL;
        loc_t            *loc  = NULL;
        int32_t           need_unref = 0;
        int32_t           ret  = -1;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        priv  = this->private;
        local = frame->local;
        trav = this->children;
        loc = &local->loc;

        --local->call_count;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG, "%s returned error %s",
                        prev->this->name, strerror (op_errno));
                 local->failed = 1;
                 local->op_errno = op_errno;
        }

        local->op_ret = 0;
        /* Get the mapping in inode private */
        /* Get the stat buf right */
        local->stbuf      = *buf;
        local->preparent  = *preparent;
        local->postparent = *postparent;

        local->stbuf_blocks += buf->ia_blocks;
        local->preparent_blocks  += preparent->ia_blocks;
        local->postparent_blocks += postparent->ia_blocks;

        if (local->stbuf_size < buf->ia_size)
              local->stbuf_size = buf->ia_size;
        if (local->preparent_size < preparent->ia_size)
              local->preparent_size = preparent->ia_size;
        if (local->postparent_size < postparent->ia_size)
              local->postparent_size = postparent->ia_size;

        if (local->failed)
                local->op_ret = -1;

        if (local->op_ret == -1) {
                local->call_count = 1;
                STACK_WIND (frame, stripe_create_fail_unlink_cbk,
                            FIRST_CHILD (this), FIRST_CHILD (this)->fops->unlink,
                            &local->loc, 0, NULL);
                return 0;
        }

        if (local->op_ret >= 0) {
                local->preparent.ia_blocks  = local->preparent_blocks;
                local->preparent.ia_size    = local->preparent_size;
                local->postparent.ia_blocks = local->postparent_blocks;
                local->postparent.ia_size   = local->postparent_size;
                local->stbuf.ia_size        = local->stbuf_size;
                local->stbuf.ia_blocks      = local->stbuf_blocks;
        }

        /* Send a setxattr request to nodes where the
           files are created */
        trav = trav->next;
        while (trav) {
                if (priv->xattr_supported) {
                        dict = dict_new ();
                        if (!dict) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "failed to allocate dict %s", loc->path);
                        }
                        need_unref = 1;

                        dict_copy (local->xattr, dict);

                        ret = stripe_xattr_request_build (this, dict,
                                                          local->stripe_size,
                                                          priv->child_count,
                                                          i, priv->coalesce);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "failed to build xattr request");
                } else {
                        dict = local->xattr;
                }

                STACK_WIND (frame, stripe_create_cbk, trav->xlator,
                            trav->xlator->fops->create, &local->loc,
                            local->flags, local->mode, local->umask, local->fd,
                            dict);
                trav = trav->next;
                if (need_unref && dict)
                        dict_unref (dict);
                i++;
        }

out:
        return 0;
}



/**
 * stripe_create - If a block-size is specified for the 'name', create the
 *    file in all the child nodes. If not, create it in only first child.
 *
 * @name- complete path of the file to be created.
 */
int32_t
stripe_create (call_frame_t *frame, xlator_t *this, loc_t *loc,
               int32_t flags, mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        stripe_private_t *priv = NULL;
        stripe_local_t   *local = NULL;
        int32_t           op_errno = EINVAL;
        int               ret            = 0;
        int               need_unref     = 0;
        int               i              = 0;
        dict_t           *dict           = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;

        /* files created in O_APPEND mode does not allow lseek() on fd */
        flags &= ~O_APPEND;

        if (priv->first_child_down || priv->nodes_down) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "First node down, returning EIO");
                op_errno = EIO;
                goto err;
        }

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        local->op_errno = ENOTCONN;
        local->stripe_size = stripe_get_matching_bs (loc->path, priv);
        frame->local = local;
        local->inode = inode_ref (loc->inode);
        loc_copy (&local->loc, loc);
        local->fd = fd_ref (fd);
        local->flags = flags;
        local->mode = mode;
        local->umask = umask;
        if (xdata)
                local->xattr = dict_ref (xdata);

        local->call_count = priv->child_count;
        /* Send a setxattr request to nodes where the
           files are created */

        if (priv->xattr_supported) {
                dict = dict_new ();
                if (!dict) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to allocate dict %s", loc->path);
                }
                need_unref = 1;

                dict_copy (xdata, dict);

                ret = stripe_xattr_request_build (this, dict,
                                                  local->stripe_size,
                                                  priv->child_count,
                                                  i, priv->coalesce);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to build xattr request");
        } else {
                        dict = xdata;
        }


        STACK_WIND (frame, stripe_first_create_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->create, loc, flags, mode,
                    umask, fd, dict);

        if (need_unref && dict)
                dict_unref (dict);


        return 0;
err:
        STRIPE_STACK_UNWIND (create, frame, -1, op_errno, NULL, NULL, NULL,
                             NULL, NULL, xdata);
        return 0;
}

int32_t
stripe_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {

                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                        local->op_errno = op_errno;
                }

                if (op_ret >= 0)
                        local->op_ret = op_ret;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                STRIPE_STACK_UNWIND (open, frame, local->op_ret,
                                     local->op_errno, local->fd, xdata);
        }
out:
        return 0;
}


/**
 * stripe_open -
 */
int32_t
stripe_open (call_frame_t *frame, xlator_t *this, loc_t *loc,
             int32_t flags, fd_t *fd, dict_t *xdata)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        /* files opened in O_APPEND mode does not allow lseek() on fd */
        flags &= ~O_APPEND;

        local->fd = fd_ref (fd);
        frame->local = local;
        loc_copy (&local->loc, loc);

        /* Striped files */
        local->flags = flags;
        local->call_count = priv->child_count;
        local->stripe_size = stripe_get_matching_bs (loc->path, priv);

        while (trav) {
                STACK_WIND (frame, stripe_open_cbk, trav->xlator,
                            trav->xlator->fops->open,
                            &local->loc, local->flags, local->fd,
                            xdata);
                trav = trav->next;
        }
        return 0;
err:
        STRIPE_STACK_UNWIND (open, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
stripe_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_ret = -1;
                        local->op_errno = op_errno;
                }

                if (op_ret >= 0)
                        local->op_ret = op_ret;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                STRIPE_STACK_UNWIND (opendir, frame, local->op_ret,
                                     local->op_errno, local->fd, NULL);
        }
out:
        return 0;
}


int32_t
stripe_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd, dict_t *xdata)
{
        xlator_list_t    *trav = NULL;
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = EINVAL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        frame->local = local;
        local->call_count = priv->child_count;
        local->fd = fd_ref (fd);

        while (trav) {
                STACK_WIND (frame, stripe_opendir_cbk, trav->xlator,
                            trav->xlator->fops->opendir, loc, fd, NULL);
                trav = trav->next;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (opendir, frame, -1, op_errno, NULL, NULL);
        return 0;
}

int32_t
stripe_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct gf_flock *lock, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }
                if (op_ret >= 0) {
                        if (FIRST_CHILD(this) == prev->this) {
                                /* First successful call, copy the *lock */
                                local->op_ret = op_ret;
                                local->lock = *lock;
                        }
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;
                STRIPE_STACK_UNWIND (lk, frame, local->op_ret,
                                     local->op_errno, &local->lock, NULL);
        }
out:
        return 0;
}

int32_t
stripe_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
           struct gf_flock *lock, dict_t *xdata)
{
        stripe_local_t   *local = NULL;
        xlator_list_t    *trav = NULL;
        stripe_private_t *priv = NULL;
        int32_t           op_errno = EINVAL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        trav = this->children;
        priv = this->private;

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_lk_cbk, trav->xlator,
                            trav->xlator->fops->lk, fd, cmd, lock, NULL);
                trav = trav->next;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (lk, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
stripe_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t *local   = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }
                if (op_ret >= 0)
                        local->op_ret = op_ret;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                STRIPE_STACK_UNWIND (flush, frame, local->op_ret,
                                     local->op_errno, NULL);
        }
out:
        return 0;
}

int32_t
stripe_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        priv = this->private;
        trav = this->children;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }
        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_flush_cbk,  trav->xlator,
                            trav->xlator->fops->flush, fd, NULL);
                trav = trav->next;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (flush, frame, -1, op_errno, NULL);
        return 0;
}



int32_t
stripe_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t *local   = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }
                if (op_ret >= 0) {
                        local->op_ret = op_ret;
                        if (FIRST_CHILD(this) == prev->this) {
                                local->pre_buf  = *prebuf;
                                local->post_buf = *postbuf;
                        }
                        local->prebuf_blocks  += prebuf->ia_blocks;
                        local->postbuf_blocks += postbuf->ia_blocks;

			correct_file_size(prebuf, local->fctx, prev);
			correct_file_size(postbuf, local->fctx, prev);

                        if (local->prebuf_size < prebuf->ia_size)
                                local->prebuf_size = prebuf->ia_size;

                        if (local->postbuf_size < postbuf->ia_size)
                                local->postbuf_size = postbuf->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if (local->op_ret != -1) {
                        local->pre_buf.ia_blocks  = local->prebuf_blocks;
                        local->pre_buf.ia_size    = local->prebuf_size;
                        local->post_buf.ia_blocks = local->postbuf_blocks;
                        local->post_buf.ia_size   = local->postbuf_size;
                }

                STRIPE_STACK_UNWIND (fsync, frame, local->op_ret,
                                     local->op_errno, &local->pre_buf,
                                     &local->post_buf, NULL);
        }
out:
        return 0;
}

int32_t
stripe_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags, dict_t *xdata)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
	stripe_fd_ctx_t  *fctx = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        priv = this->private;
        trav = this->children;

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

	inode_ctx_get(fd->inode, this, (uint64_t *) &fctx);
	if (!fctx) {
		op_errno = EINVAL;
		goto err;
	}
	local->fctx = fctx;

        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_fsync_cbk, trav->xlator,
                            trav->xlator->fops->fsync, fd, flags, NULL);
                trav = trav->next;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (fsync, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

int32_t
stripe_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }

                if (op_ret == 0) {
                        local->op_ret = 0;

                        if (FIRST_CHILD(this) == prev->this)
                                local->stbuf = *buf;

                        local->stbuf_blocks += buf->ia_blocks;

			correct_file_size(buf, local->fctx, prev);

                        if (local->stbuf_size < buf->ia_size)
                                local->stbuf_size = buf->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                if (local->op_ret != -1) {
                        local->stbuf.ia_size   = local->stbuf_size;
                        local->stbuf.ia_blocks = local->stbuf_blocks;
                }

                STRIPE_STACK_UNWIND (fstat, frame, local->op_ret,
                                     local->op_errno, &local->stbuf, NULL);
        }

out:
        return 0;
}

int32_t
stripe_fstat (call_frame_t *frame,
              xlator_t *this,
              fd_t *fd, dict_t *xdata)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
	stripe_fd_ctx_t  *fctx = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        priv = this->private;
        trav = this->children;

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

	if (IA_ISREG(fd->inode->ia_type)) {
		inode_ctx_get(fd->inode, this, (uint64_t *) &fctx);
		if (!fctx)
			goto err;
		local->fctx = fctx;
	}

        while (trav) {
                STACK_WIND (frame, stripe_fstat_cbk, trav->xlator,
                            trav->xlator->fops->fstat, fd, NULL);
                trav = trav->next;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (fstat, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
stripe_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset, dict_t *xdata)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
	stripe_fd_ctx_t  *fctx = NULL;
	int		  i, eof_idx;
	off_t		  dest_offset, tmp_offset;
        int32_t		  op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        priv = this->private;

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

	inode_ctx_get(fd->inode, this, (uint64_t *) &fctx);
	if (!fctx) {
		gf_log(this->name, GF_LOG_ERROR, "no stripe context");
		op_errno = EINVAL;
		goto err;
	}
	if (!fctx->stripe_count) {
		gf_log(this->name, GF_LOG_ERROR, "no stripe count");
		op_errno = EINVAL;
		goto err;
	}

	local->fctx = fctx;
	eof_idx = (offset / fctx->stripe_size) % fctx->stripe_count;

	for (i = 0; i < fctx->stripe_count; i++) {
		if (!fctx->xl_array[i]) {
			gf_log(this->name, GF_LOG_ERROR, "no xlator at index "
				"%d", i);
			op_errno = EINVAL;
			goto err;
		}

		if (fctx->stripe_coalesce) {
			if (i < eof_idx)
				tmp_offset = roof(offset, fctx->stripe_size *
						fctx->stripe_count);
			else if (i > eof_idx)
				tmp_offset = floor(offset, fctx->stripe_size *
						fctx->stripe_count);
			else
				tmp_offset = offset;

			dest_offset = coalesced_offset(tmp_offset,
				fctx->stripe_size, fctx->stripe_count);
		} else {
			dest_offset = offset;
		}

		STACK_WIND(frame, stripe_truncate_cbk, fctx->xl_array[i],
			fctx->xl_array[i]->fops->ftruncate, fd, dest_offset,
			NULL);
	}

        return 0;
err:
        STRIPE_STACK_UNWIND (ftruncate, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int32_t
stripe_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t *local   = NULL;
        call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s returned %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        if ((op_errno != ENOENT) ||
                            (prev->this == FIRST_CHILD (this)))
                                local->failed = 1;
                }
                if (op_ret >= 0)
                        local->op_ret = op_ret;
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (local->failed)
                        local->op_ret = -1;

                STRIPE_STACK_UNWIND (fsyncdir, frame, local->op_ret,
                                     local->op_errno, NULL);
        }
out:
        return 0;
}

int32_t
stripe_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags, dict_t *xdata)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t    *trav = NULL;
        int32_t           op_errno = 1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        priv = this->private;
        trav = this->children;

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        local->call_count = priv->child_count;

        while (trav) {
                STACK_WIND (frame, stripe_fsyncdir_cbk, trav->xlator,
                            trav->xlator->fops->fsyncdir, fd, flags, NULL);
                trav = trav->next;
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (fsyncdir, frame, -1, op_errno, NULL);
        return 0;
}


int32_t
stripe_readv_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
        int32_t         i = 0;
        int32_t         callcnt = 0;
        int32_t         count = 0;
        stripe_local_t *local = NULL;
        struct iovec   *vec = NULL;
        struct iatt     tmp_stbuf = {0,};
        struct iobref  *tmp_iobref = NULL;
        struct iobuf   *iobuf = NULL;
	call_frame_t   *prev = NULL;

        if (!this || !frame || !frame->local) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        local = frame->local;
	prev = cookie;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;
                if (op_ret != -1) {
			correct_file_size(buf, local->fctx, prev);
                        if (local->stbuf_size < buf->ia_size)
                                local->stbuf_size = buf->ia_size;
		}
        }
        UNLOCK (&frame->lock);

        if (!callcnt) {
                op_ret = 0;

                /* Keep extra space for filling in '\0's */
                vec = GF_CALLOC ((local->count * 2), sizeof (struct iovec),
                                 gf_stripe_mt_iovec);
                if (!vec) {
                        op_ret = -1;
                        goto done;
                }

                for (i = 0; i < local->wind_count; i++) {
                        if (local->replies[i].op_ret) {
                                memcpy ((vec + count), local->replies[i].vector,
                                        (local->replies[i].count * sizeof (struct iovec)));
                                count +=  local->replies[i].count;
                                op_ret += local->replies[i].op_ret;
                        }
                        if ((local->replies[i].op_ret <
                             local->replies[i].requested_size) &&
                            (local->stbuf_size > (local->offset + op_ret))) {
                                /* Fill in 0s here */
                                vec[count].iov_len  =
                                        (local->replies[i].requested_size -
                                         local->replies[i].op_ret);
                                iobuf = iobuf_get2 (this->ctx->iobuf_pool,
                                                    vec[count].iov_len);
                                if (!iobuf) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "Out of memory.");
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        goto done;
                                }
                                memset (iobuf->ptr, 0, vec[count].iov_len);
                                vec[count].iov_base = iobuf->ptr;

                                iobref_add (local->iobref, iobuf);
                                iobuf_unref(iobuf);

                                op_ret += vec[count].iov_len;
                                count++;
                        }
                        GF_FREE (local->replies[i].vector);
                }

                /* FIXME: notice that st_ino, and st_dev (gen) will be
                 * different than what inode will have. Make sure this doesn't
                 * cause any bugs at higher levels */
                memcpy (&tmp_stbuf, &local->replies[0].stbuf,
                        sizeof (struct iatt));
                tmp_stbuf.ia_size = local->stbuf_size;

        done:
                GF_FREE (local->replies);
                tmp_iobref = local->iobref;
                STRIPE_STACK_UNWIND (readv, frame, op_ret, op_errno, vec,
                                     count, &tmp_stbuf, tmp_iobref, NULL);

                iobref_unref (tmp_iobref);
                GF_FREE (vec);
        }
out:
        return 0;
}

/**
 * stripe_readv_cbk - get all the striped reads, and order it properly, send it
 *        to above layer after putting it in a single vector.
 */
int32_t
stripe_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iovec *vector,
                  int32_t count, struct iatt *stbuf, struct iobref *iobref, dict_t *xdata)
{
        int32_t         index = 0;
        int32_t         callcnt = 0;
        int32_t         final_count = 0;
        int32_t         need_to_check_proper_size = 0;
        call_frame_t   *mframe = NULL;
        stripe_local_t *mlocal = NULL;
        stripe_local_t *local = NULL;
        struct iovec   *final_vec = NULL;
        struct iatt     tmp_stbuf = {0,};
        struct iatt    *tmp_stbuf_p = NULL; //need it for a warning
        struct iobref  *tmp_iobref = NULL;
        stripe_fd_ctx_t  *fctx = NULL;
	call_frame_t	*prev = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto end;
        }

        local  = frame->local;
        index  = local->node_index;
	prev = cookie;
        mframe = local->orig_frame;
        if (!mframe)
                goto out;

        mlocal = mframe->local;
        if (!mlocal)
                goto out;

        fctx = mlocal->fctx;

        LOCK (&mframe->lock);
        {
                mlocal->replies[index].op_ret = op_ret;
                mlocal->replies[index].op_errno = op_errno;
                mlocal->replies[index].requested_size = local->readv_size;
                if (op_ret >= 0) {
                        mlocal->replies[index].stbuf  = *stbuf;
                        mlocal->replies[index].count  = count;
                        mlocal->replies[index].vector = iov_dup (vector, count);

			correct_file_size(stbuf, fctx, prev);

                        if (local->stbuf_size < stbuf->ia_size)
                                local->stbuf_size = stbuf->ia_size;
                        local->stbuf_blocks += stbuf->ia_blocks;

                        if (!mlocal->iobref)
                                mlocal->iobref = iobref_new ();
                        iobref_merge (mlocal->iobref, iobref);
                }
                callcnt = ++mlocal->call_count;
        }
        UNLOCK(&mframe->lock);

        if (callcnt == mlocal->wind_count) {
                op_ret = 0;

                for (index=0; index < mlocal->wind_count; index++) {
                        /* check whether each stripe returned
                         * 'expected' number of bytes */
                        if (mlocal->replies[index].op_ret == -1) {
                                op_ret = -1;
                                op_errno = mlocal->replies[index].op_errno;
                                break;
                        }
                        /* TODO: handle the 'holes' within the read range
                           properly */
                        if (mlocal->replies[index].op_ret <
                            mlocal->replies[index].requested_size) {
                                need_to_check_proper_size = 1;
                        }

                        op_ret       += mlocal->replies[index].op_ret;
                        mlocal->count += mlocal->replies[index].count;
                }
                if (op_ret == -1)
                        goto done;
                if (need_to_check_proper_size)
                        goto check_size;

                final_vec = GF_CALLOC (mlocal->count, sizeof (struct iovec),
                                       gf_stripe_mt_iovec);

                if (!final_vec) {
                        op_ret = -1;
                        goto done;
                }

                for (index = 0; index < mlocal->wind_count; index++) {
                        memcpy ((final_vec + final_count),
                                mlocal->replies[index].vector,
                                (mlocal->replies[index].count *
                                 sizeof (struct iovec)));
                        final_count +=  mlocal->replies[index].count;
                        GF_FREE (mlocal->replies[index].vector);
                }

                /* FIXME: notice that st_ino, and st_dev (gen) will be
                 * different than what inode will have. Make sure this doesn't
                 * cause any bugs at higher levels */
                memcpy (&tmp_stbuf, &mlocal->replies[0].stbuf,
                        sizeof (struct iatt));
                tmp_stbuf.ia_size = local->stbuf_size;
                tmp_stbuf.ia_blocks = local->stbuf_blocks;

        done:
                /* */
                GF_FREE (mlocal->replies);
                tmp_iobref = mlocal->iobref;
                /* work around for nfs truncated read. Bug 3774 */
                tmp_stbuf_p = &tmp_stbuf;
                WIPE (tmp_stbuf_p);
                STRIPE_STACK_UNWIND (readv, mframe, op_ret, op_errno, final_vec,
                                     final_count, &tmp_stbuf, tmp_iobref, NULL);

                iobref_unref (tmp_iobref);
                GF_FREE (final_vec);
        }

        goto out;

check_size:
        mlocal->call_count = fctx->stripe_count;

        for (index = 0; index < fctx->stripe_count; index++) {
                STACK_WIND (mframe, stripe_readv_fstat_cbk,
                            (fctx->xl_array[index]),
                            (fctx->xl_array[index])->fops->fstat,
                            mlocal->fd, NULL);
        }

out:
        STRIPE_STACK_DESTROY (frame);
end:
        return 0;
}


int32_t
stripe_readv (call_frame_t *frame, xlator_t *this, fd_t *fd,
              size_t size, off_t offset, uint32_t flags, dict_t *xdata)
{
        int32_t           op_errno = EINVAL;
        int32_t           idx = 0;
        int32_t           index = 0;
        int32_t           num_stripe = 0;
        int32_t           off_index = 0;
        size_t            frame_size = 0;
        off_t             rounded_end = 0;
        uint64_t          tmp_fctx = 0;
        uint64_t          stripe_size = 0;
        off_t             rounded_start = 0;
        off_t             frame_offset = offset;
	off_t		  dest_offset = 0;
        stripe_local_t   *local = NULL;
        call_frame_t     *rframe = NULL;
        stripe_local_t   *rlocal = NULL;
        stripe_fd_ctx_t  *fctx = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        inode_ctx_get (fd->inode, this, &tmp_fctx);
        if (!tmp_fctx) {
                op_errno = EBADFD;
                goto err;
        }
        fctx = (stripe_fd_ctx_t *)(long)tmp_fctx;
        stripe_size = fctx->stripe_size;

        STRIPE_VALIDATE_FCTX (fctx, err);

        if (!stripe_size) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Wrong stripe size for the file");
                goto err;
        }
        /* The file is stripe across the child nodes. Send the read request
         * to the child nodes appropriately after checking which region of
         * the file is in which child node. Always '0-<stripe_size>' part of
         * the file resides in the first child.
         */
        rounded_start = floor (offset, stripe_size);
        rounded_end = roof (offset+size, stripe_size);
        num_stripe = (rounded_end- rounded_start)/stripe_size;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        frame->local = local;

        /* This is where all the vectors should be copied. */
        local->replies = GF_CALLOC (num_stripe, sizeof (struct stripe_replies),
                                    gf_stripe_mt_stripe_replies);
        if (!local->replies) {
                op_errno = ENOMEM;
                goto err;
        }

        off_index = (offset / stripe_size) % fctx->stripe_count;
        local->wind_count = num_stripe;
        local->readv_size = size;
        local->offset     = offset;
        local->fd         = fd_ref (fd);
        local->fctx       = fctx;

        for (index = off_index; index < (num_stripe + off_index); index++) {
                rframe = copy_frame (frame);
                rlocal = mem_get0 (this->local_pool);
                if (!rlocal) {
                        op_errno = ENOMEM;
                        goto err;
                }

                frame_size = min (roof (frame_offset+1, stripe_size),
                                  (offset + size)) - frame_offset;

                rlocal->node_index = index - off_index;
                rlocal->orig_frame = frame;
                rlocal->readv_size = frame_size;
                rframe->local = rlocal;
                idx = (index % fctx->stripe_count);

		if (fctx->stripe_coalesce)
			dest_offset = coalesced_offset(frame_offset,
				stripe_size, fctx->stripe_count);
		else
			dest_offset = frame_offset;

                STACK_WIND (rframe, stripe_readv_cbk, fctx->xl_array[idx],
                            fctx->xl_array[idx]->fops->readv,
                            fd, frame_size, dest_offset, flags, xdata);

                frame_offset += frame_size;
        }

        return 0;
err:
        if (rframe)
                STRIPE_STACK_DESTROY (rframe);

        STRIPE_STACK_UNWIND (readv, frame, -1, op_errno, NULL, 0, NULL, NULL, NULL);
        return 0;
}


int32_t
stripe_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
	stripe_local_t *mlocal = NULL;
        call_frame_t   *prev = NULL;
	call_frame_t   *mframe = NULL;
	struct stripe_replies *reply = NULL;
	int32_t		i = 0;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;
	mframe = local->orig_frame;
	mlocal = mframe->local;

        LOCK(&frame->lock);
        {
                callcnt = ++mlocal->call_count;

		mlocal->replies[local->node_index].op_ret = op_ret;
		mlocal->replies[local->node_index].op_errno = op_errno;

                if (op_ret >= 0) {
                        mlocal->post_buf = *postbuf;
                        mlocal->pre_buf = *prebuf;

			mlocal->prebuf_blocks  += prebuf->ia_blocks;
			mlocal->postbuf_blocks += postbuf->ia_blocks;

			correct_file_size(prebuf, mlocal->fctx, prev);
			correct_file_size(postbuf, mlocal->fctx, prev);

			if (mlocal->prebuf_size < prebuf->ia_size)
				mlocal->prebuf_size = prebuf->ia_size;
			if (mlocal->postbuf_size < postbuf->ia_size)
				mlocal->postbuf_size = postbuf->ia_size;
                }
        }
        UNLOCK (&frame->lock);

        if ((callcnt == mlocal->wind_count) && mlocal->unwind) {
		mlocal->pre_buf.ia_size = mlocal->prebuf_size;
		mlocal->pre_buf.ia_blocks = mlocal->prebuf_blocks;
		mlocal->post_buf.ia_size = mlocal->postbuf_size;
		mlocal->post_buf.ia_blocks = mlocal->postbuf_blocks;

		/*
		 * Only return the number of consecutively written bytes up until
		 * the first error. Only return an error if it occurs first.
		 *
		 * When a short write occurs, the application should retry at the
		 * appropriate offset, at which point we'll potentially pass back
		 * the error.
		 */
		for (i = 0, reply = mlocal->replies; i < mlocal->wind_count;
			i++, reply++) {
			if (reply->op_ret == -1) {
				gf_log(this->name, GF_LOG_DEBUG, "reply %d "
					"returned error %s", i,
					strerror(reply->op_errno));
				if (!mlocal->op_ret) {
					mlocal->op_ret = -1;
					mlocal->op_errno = reply->op_errno;
				}
				break;
			}

			mlocal->op_ret += reply->op_ret;

			if (reply->op_ret < reply->requested_size)
				break;
		}

		GF_FREE(mlocal->replies);

                STRIPE_STACK_UNWIND (writev, mframe, mlocal->op_ret,
                                     mlocal->op_errno, &mlocal->pre_buf,
                                     &mlocal->post_buf, NULL);
        }
out:
	STRIPE_STACK_DESTROY(frame);
        return 0;
}

int32_t
stripe_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
               struct iovec *vector, int32_t count, off_t offset,
               uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        struct iovec     *tmp_vec = NULL;
        stripe_local_t   *local = NULL;
        stripe_fd_ctx_t  *fctx = NULL;
        int32_t           op_errno = 1;
        int32_t           idx = 0;
        int32_t           total_size = 0;
        int32_t           offset_offset = 0;
        int32_t           remaining_size = 0;
        int32_t           tmp_count = count;
        off_t             fill_size = 0;
        uint64_t          stripe_size = 0;
        uint64_t          tmp_fctx = 0;
	off_t		  dest_offset = 0;
	off_t		  rounded_start = 0;
	off_t		  rounded_end = 0;
	int32_t		  total_chunks = 0;
	call_frame_t	  *wframe = NULL;
	stripe_local_t	  *wlocal = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        inode_ctx_get (fd->inode, this, &tmp_fctx);
        if (!tmp_fctx) {
                op_errno = EINVAL;
                goto err;
        }
        fctx = (stripe_fd_ctx_t *)(long)tmp_fctx;
        stripe_size = fctx->stripe_size;

        STRIPE_VALIDATE_FCTX (fctx, err);

        /* File has to be stripped across the child nodes */
        for (idx = 0; idx< count; idx ++) {
                total_size += vector[idx].iov_len;
        }
        remaining_size = total_size;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        frame->local = local;
        local->stripe_size = stripe_size;
	local->fctx = fctx;

        if (!stripe_size) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Wrong stripe size for the file");
                op_errno = EINVAL;
                goto err;
        }

	rounded_start = floor(offset, stripe_size);
	rounded_end = roof(offset + total_size, stripe_size);
	total_chunks = (rounded_end - rounded_start) / stripe_size;
	local->replies = GF_CALLOC(total_chunks, sizeof(struct stripe_replies),
				gf_stripe_mt_stripe_replies);
	if (!local->replies) {
		op_errno = ENOMEM;
		goto err;
	}

	total_chunks = 0;
        while (1) {
		wframe = copy_frame(frame);
		wlocal = mem_get0(this->local_pool);
		if (!wlocal) {
			op_errno = ENOMEM;
			goto err;
		}
		wlocal->orig_frame = frame;
		wframe->local = wlocal;

                /* Send striped chunk of the vector to child
                   nodes appropriately. */
                idx = (((offset + offset_offset) /
                        local->stripe_size) % fctx->stripe_count);

                fill_size = (local->stripe_size -
                             ((offset + offset_offset) % local->stripe_size));
                if (fill_size > remaining_size)
                        fill_size = remaining_size;

                remaining_size -= fill_size;

                tmp_count = iov_subset (vector, count, offset_offset,
                                        offset_offset + fill_size, NULL);
                tmp_vec = GF_CALLOC (tmp_count, sizeof (struct iovec),
                                     gf_stripe_mt_iovec);
                if (!tmp_vec) {
                        op_errno = ENOMEM;
                        goto err;
                }
                tmp_count = iov_subset (vector, count, offset_offset,
                                        offset_offset + fill_size, tmp_vec);

                local->wind_count++;
                if (remaining_size == 0)
                        local->unwind = 1;

		/*
		 * Store off the request index (with respect to the chunk of the
		 * initial offset) and the size of the request. This is required
		 * in the callback to calculate an appropriate return value in
		 * the event of a write failure in one or more requests.
		 */
		wlocal->node_index = total_chunks;
		local->replies[total_chunks].requested_size = fill_size;

		dest_offset = offset + offset_offset;
		if (fctx->stripe_coalesce)
			dest_offset = coalesced_offset(dest_offset,
					local->stripe_size, fctx->stripe_count);

                STACK_WIND (wframe, stripe_writev_cbk, fctx->xl_array[idx],
                            fctx->xl_array[idx]->fops->writev, fd, tmp_vec,
                            tmp_count, dest_offset, flags, iobref,
                            xdata);

                GF_FREE (tmp_vec);
                offset_offset += fill_size;
		total_chunks++;
                if (remaining_size == 0)
                        break;
        }

        return 0;
err:
	if (wframe)
		STRIPE_STACK_DESTROY(wframe);

        STRIPE_STACK_UNWIND (writev, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int32_t
stripe_fallocate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
	stripe_local_t *mlocal = NULL;
        call_frame_t   *prev = NULL;
	call_frame_t   *mframe = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;
	mframe = local->orig_frame;
	mlocal = mframe->local;

        LOCK(&frame->lock);
        {
                callcnt = ++mlocal->call_count;

                if (op_ret == 0) {
                        mlocal->post_buf = *postbuf;
                        mlocal->pre_buf = *prebuf;

			mlocal->prebuf_blocks  += prebuf->ia_blocks;
			mlocal->postbuf_blocks += postbuf->ia_blocks;

			correct_file_size(prebuf, mlocal->fctx, prev);
			correct_file_size(postbuf, mlocal->fctx, prev);

			if (mlocal->prebuf_size < prebuf->ia_size)
				mlocal->prebuf_size = prebuf->ia_size;
			if (mlocal->postbuf_size < postbuf->ia_size)
				mlocal->postbuf_size = postbuf->ia_size;
                }

		/* return the first failure */
		if (mlocal->op_ret == 0) {
			mlocal->op_ret = op_ret;
			mlocal->op_errno = op_errno;
		}
        }
        UNLOCK (&frame->lock);

        if ((callcnt == mlocal->wind_count) && mlocal->unwind) {
		mlocal->pre_buf.ia_size = mlocal->prebuf_size;
		mlocal->pre_buf.ia_blocks = mlocal->prebuf_blocks;
		mlocal->post_buf.ia_size = mlocal->postbuf_size;
		mlocal->post_buf.ia_blocks = mlocal->postbuf_blocks;

                STRIPE_STACK_UNWIND (fallocate, mframe, mlocal->op_ret,
                                     mlocal->op_errno, &mlocal->pre_buf,
                                     &mlocal->post_buf, NULL);
        }
out:
	STRIPE_STACK_DESTROY(frame);
        return 0;
}

int32_t
stripe_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
		 off_t offset, size_t len, dict_t *xdata)
{
        stripe_local_t   *local = NULL;
        stripe_fd_ctx_t  *fctx = NULL;
        int32_t           op_errno = 1;
        int32_t           idx = 0;
        int32_t           offset_offset = 0;
        int32_t           remaining_size = 0;
        off_t             fill_size = 0;
        uint64_t          stripe_size = 0;
        uint64_t          tmp_fctx = 0;
	off_t		  dest_offset = 0;
	call_frame_t	  *fframe = NULL;
	stripe_local_t	  *flocal = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        inode_ctx_get (fd->inode, this, &tmp_fctx);
        if (!tmp_fctx) {
                op_errno = EINVAL;
                goto err;
        }
        fctx = (stripe_fd_ctx_t *)(long)tmp_fctx;
        stripe_size = fctx->stripe_size;

        STRIPE_VALIDATE_FCTX (fctx, err);

        remaining_size = len;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        frame->local = local;
        local->stripe_size = stripe_size;
	local->fctx = fctx;

        if (!stripe_size) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Wrong stripe size for the file");
                op_errno = EINVAL;
                goto err;
        }

        while (1) {
		fframe = copy_frame(frame);
		flocal = mem_get0(this->local_pool);
		if (!flocal) {
			op_errno = ENOMEM;
			goto err;
		}
		flocal->orig_frame = frame;
		fframe->local = flocal;

		/* send fallocate request to the associated child node */
                idx = (((offset + offset_offset) /
                        local->stripe_size) % fctx->stripe_count);

                fill_size = (local->stripe_size -
                             ((offset + offset_offset) % local->stripe_size));
                if (fill_size > remaining_size)
                        fill_size = remaining_size;

                remaining_size -= fill_size;

                local->wind_count++;
                if (remaining_size == 0)
                        local->unwind = 1;

		dest_offset = offset + offset_offset;
		if (fctx->stripe_coalesce)
			dest_offset = coalesced_offset(dest_offset,
					local->stripe_size, fctx->stripe_count);

		/*
		 * TODO: Create a separate handler for coalesce mode that sends a
		 * single fallocate per-child (since the ranges are linear).
		 */
		STACK_WIND(fframe, stripe_fallocate_cbk, fctx->xl_array[idx],
			   fctx->xl_array[idx]->fops->fallocate, fd, mode,
			   dest_offset, fill_size, xdata);

                offset_offset += fill_size;
                if (remaining_size == 0)
                        break;
        }

        return 0;
err:
	if (fframe)
		STRIPE_STACK_DESTROY(fframe);

        STRIPE_STACK_UNWIND (fallocate, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}


int32_t
stripe_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
        int32_t         callcnt = 0;
        stripe_local_t *local = NULL;
	stripe_local_t *mlocal = NULL;
        call_frame_t   *prev = NULL;
	call_frame_t   *mframe = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;
	mframe = local->orig_frame;
	mlocal = mframe->local;

        LOCK(&frame->lock);
        {
                callcnt = ++mlocal->call_count;

                if (op_ret == 0) {
                        mlocal->post_buf = *postbuf;
                        mlocal->pre_buf = *prebuf;

			mlocal->prebuf_blocks  += prebuf->ia_blocks;
			mlocal->postbuf_blocks += postbuf->ia_blocks;

			correct_file_size(prebuf, mlocal->fctx, prev);
			correct_file_size(postbuf, mlocal->fctx, prev);

			if (mlocal->prebuf_size < prebuf->ia_size)
				mlocal->prebuf_size = prebuf->ia_size;
			if (mlocal->postbuf_size < postbuf->ia_size)
				mlocal->postbuf_size = postbuf->ia_size;
                }

		/* return the first failure */
		if (mlocal->op_ret == 0) {
			mlocal->op_ret = op_ret;
			mlocal->op_errno = op_errno;
		}
        }
        UNLOCK (&frame->lock);

        if ((callcnt == mlocal->wind_count) && mlocal->unwind) {
		mlocal->pre_buf.ia_size = mlocal->prebuf_size;
		mlocal->pre_buf.ia_blocks = mlocal->prebuf_blocks;
		mlocal->post_buf.ia_size = mlocal->postbuf_size;
		mlocal->post_buf.ia_blocks = mlocal->postbuf_blocks;

                STRIPE_STACK_UNWIND (discard, mframe, mlocal->op_ret,
                                     mlocal->op_errno, &mlocal->pre_buf,
                                     &mlocal->post_buf, NULL);
        }
out:
	STRIPE_STACK_DESTROY(frame);
        return 0;
}

int32_t
stripe_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
	       size_t len, dict_t *xdata)
{
        stripe_local_t   *local = NULL;
        stripe_fd_ctx_t  *fctx = NULL;
        int32_t           op_errno = 1;
        int32_t           idx = 0;
        int32_t           offset_offset = 0;
        int32_t           remaining_size = 0;
        off_t             fill_size = 0;
        uint64_t          stripe_size = 0;
        uint64_t          tmp_fctx = 0;
	off_t		  dest_offset = 0;
	call_frame_t	  *fframe = NULL;
	stripe_local_t	  *flocal = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        inode_ctx_get (fd->inode, this, &tmp_fctx);
        if (!tmp_fctx) {
                op_errno = EINVAL;
                goto err;
        }
        fctx = (stripe_fd_ctx_t *)(long)tmp_fctx;
        stripe_size = fctx->stripe_size;

        STRIPE_VALIDATE_FCTX (fctx, err);

        remaining_size = len;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        frame->local = local;
        local->stripe_size = stripe_size;
	local->fctx = fctx;

        if (!stripe_size) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Wrong stripe size for the file");
                op_errno = EINVAL;
                goto err;
        }

        while (1) {
		fframe = copy_frame(frame);
		flocal = mem_get0(this->local_pool);
		if (!flocal) {
			op_errno = ENOMEM;
			goto err;
		}
		flocal->orig_frame = frame;
		fframe->local = flocal;

		/* send discard request to the associated child node */
                idx = (((offset + offset_offset) /
                        local->stripe_size) % fctx->stripe_count);

                fill_size = (local->stripe_size -
                             ((offset + offset_offset) % local->stripe_size));
                if (fill_size > remaining_size)
                        fill_size = remaining_size;

                remaining_size -= fill_size;

                local->wind_count++;
                if (remaining_size == 0)
                        local->unwind = 1;

		dest_offset = offset + offset_offset;
		if (fctx->stripe_coalesce)
			dest_offset = coalesced_offset(dest_offset,
					local->stripe_size, fctx->stripe_count);

		/*
		 * TODO: Create a separate handler for coalesce mode that sends a
		 * single discard per-child (since the ranges are linear).
		 */
		STACK_WIND(fframe, stripe_discard_cbk, fctx->xl_array[idx],
			   fctx->xl_array[idx]->fops->discard, fd, dest_offset,
			   fill_size, xdata);

                offset_offset += fill_size;
                if (remaining_size == 0)
                        break;
        }

        return 0;
err:
	if (fframe)
		STRIPE_STACK_DESTROY(fframe);

        STRIPE_STACK_UNWIND (discard, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

int32_t
stripe_zerofill_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
        int32_t         callcnt    = 0;
        stripe_local_t *local      = NULL;
        stripe_local_t *mlocal     = NULL;
        call_frame_t   *prev       = NULL;
        call_frame_t   *mframe     = NULL;

        GF_ASSERT (frame);

        if (!this || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }

        prev  = cookie;
        local = frame->local;
        mframe = local->orig_frame;
        mlocal = mframe->local;

        LOCK(&frame->lock);
        {
                callcnt = ++mlocal->call_count;

                if (op_ret == 0) {
                        mlocal->post_buf = *postbuf;
                        mlocal->pre_buf = *prebuf;

                        mlocal->prebuf_blocks  += prebuf->ia_blocks;
                        mlocal->postbuf_blocks += postbuf->ia_blocks;

                        correct_file_size(prebuf, mlocal->fctx, prev);
                        correct_file_size(postbuf, mlocal->fctx, prev);

                        if (mlocal->prebuf_size < prebuf->ia_size)
                                mlocal->prebuf_size = prebuf->ia_size;
                        if (mlocal->postbuf_size < postbuf->ia_size)
                                mlocal->postbuf_size = postbuf->ia_size;
                }

                /* return the first failure */
                if (mlocal->op_ret == 0) {
                        mlocal->op_ret = op_ret;
                        mlocal->op_errno = op_errno;
                }
        }
        UNLOCK (&frame->lock);

        if ((callcnt == mlocal->wind_count) && mlocal->unwind) {
                mlocal->pre_buf.ia_size = mlocal->prebuf_size;
                mlocal->pre_buf.ia_blocks = mlocal->prebuf_blocks;
                mlocal->post_buf.ia_size = mlocal->postbuf_size;
                mlocal->post_buf.ia_blocks = mlocal->postbuf_blocks;

                STRIPE_STACK_UNWIND (zerofill, mframe, mlocal->op_ret,
                                     mlocal->op_errno, &mlocal->pre_buf,
                                     &mlocal->post_buf, NULL);
        }
out:
        STRIPE_STACK_DESTROY(frame);
        return 0;
}

int32_t
stripe_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
               off_t len, dict_t *xdata)
{
        stripe_local_t   *local            = NULL;
        stripe_fd_ctx_t  *fctx             = NULL;
        int32_t           op_errno         = 1;
        int32_t           idx              = 0;
        int32_t           offset_offset    = 0;
        int32_t           remaining_size   = 0;
        off_t             fill_size        = 0;
        uint64_t          stripe_size      = 0;
        uint64_t          tmp_fctx         = 0;
        off_t             dest_offset      = 0;
        call_frame_t     *fframe           = NULL;
        stripe_local_t   *flocal           = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (fd->inode, err);

        inode_ctx_get (fd->inode, this, &tmp_fctx);
        if (!tmp_fctx) {
                op_errno = EINVAL;
                goto err;
        }
        fctx = (stripe_fd_ctx_t *)(long)tmp_fctx;
        stripe_size = fctx->stripe_size;

        STRIPE_VALIDATE_FCTX (fctx, err);

        remaining_size = len;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        frame->local = local;
        local->stripe_size = stripe_size;
        local->fctx = fctx;

        if (!stripe_size) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Wrong stripe size for the file");
                op_errno = EINVAL;
                goto err;
        }

        while (1) {
                fframe = copy_frame(frame);
                flocal = mem_get0(this->local_pool);
                if (!flocal) {
                        op_errno = ENOMEM;
                        goto err;
                }
                flocal->orig_frame = frame;
                fframe->local = flocal;

                idx = (((offset + offset_offset) /
                        local->stripe_size) % fctx->stripe_count);

                fill_size = (local->stripe_size -
                             ((offset + offset_offset) % local->stripe_size));
                if (fill_size > remaining_size)
                        fill_size = remaining_size;

                remaining_size -= fill_size;

                local->wind_count++;
                if (remaining_size == 0)
                        local->unwind = 1;

                dest_offset = offset + offset_offset;
                if (fctx->stripe_coalesce)
                        dest_offset = coalesced_offset(dest_offset,
                                                       local->stripe_size,
                                                       fctx->stripe_count);

                STACK_WIND(fframe, stripe_zerofill_cbk, fctx->xl_array[idx],
                           fctx->xl_array[idx]->fops->zerofill, fd,
                           dest_offset, fill_size, xdata);
                offset_offset += fill_size;
                if (remaining_size == 0)
                        break;
        }

        return 0;
err:
        if (fframe)
                STRIPE_STACK_DESTROY(fframe);

        STRIPE_STACK_UNWIND (zerofill, frame, -1, op_errno, NULL, NULL, NULL);
        return 0;
}

int32_t
stripe_release (xlator_t *this, fd_t *fd)
{
	return 0;
}

int
stripe_forget (xlator_t *this, inode_t *inode)
{
        uint64_t          tmp_fctx = 0;
        stripe_fd_ctx_t  *fctx = NULL;

        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (inode, err);

        (void) inode_ctx_del (inode, this, &tmp_fctx);
        if (!tmp_fctx) {
                goto err;
        }

        fctx = (stripe_fd_ctx_t *)(long)tmp_fctx;

        if (!fctx->static_array)
                GF_FREE (fctx->xl_array);

        GF_FREE (fctx);
err:
        return 0;
}

int32_t
notify (xlator_t *this, int32_t event, void *data, ...)
{
        stripe_private_t *priv = NULL;
        int               down_client = 0;
        int               i = 0;
        gf_boolean_t      heard_from_all_children = _gf_false;

        if (!this)
                return 0;

        priv = this->private;
        if (!priv)
                return 0;

        switch (event)
        {
        case GF_EVENT_CHILD_UP:
        {
                /* get an index number to set */
                for (i = 0; i < priv->child_count; i++) {
                        if (data == priv->xl_array[i])
                                break;
                }

                if (priv->child_count == i) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "got GF_EVENT_CHILD_UP bad subvolume %s",
                                data? ((xlator_t *)data)->name: NULL);
                        break;
                }

                LOCK (&priv->lock);
                {
                        if (data == FIRST_CHILD (this))
                                priv->first_child_down = 0;
                        priv->last_event[i] = event;
                }
                UNLOCK (&priv->lock);
        }
        break;
        case GF_EVENT_CHILD_CONNECTING:
        {
                // 'CONNECTING' doesn't ensure its CHILD_UP, so do nothing
                goto out;
        }
        case GF_EVENT_CHILD_DOWN:
        {
                /* get an index number to set */
                for (i = 0; i < priv->child_count; i++) {
                        if (data == priv->xl_array[i])
                                break;
                }

                if (priv->child_count == i) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "got GF_EVENT_CHILD_DOWN bad subvolume %s",
                                data? ((xlator_t *)data)->name: NULL);
                        break;
                }

                LOCK (&priv->lock);
                {
                        if (data == FIRST_CHILD (this))
                                priv->first_child_down = 1;
                        priv->last_event[i] = event;
                }
                UNLOCK (&priv->lock);
        }
        break;

        default:
        {
                /* */
                default_notify (this, event, data);
                goto out;
        }
        break;
        }

        // Consider child as down if it's last_event is not CHILD_UP
        for (i = 0, down_client = 0; i < priv->child_count; i++)
                if (priv->last_event[i] != GF_EVENT_CHILD_UP)
                        down_client++;

        LOCK (&priv->lock);
        {
                priv->nodes_down = down_client;
        }
        UNLOCK (&priv->lock);

        heard_from_all_children = _gf_true;
        for (i = 0; i < priv->child_count; i++)
                if (!priv->last_event[i])
                        heard_from_all_children = _gf_false;

        if (heard_from_all_children)
                default_notify (this, event, data);
out:
        return 0;
}

int
stripe_setxattr_cbk (call_frame_t *frame, void *cookie,
                     xlator_t *this, int op_ret, int op_errno, dict_t *xdata)
{
        int ret = -1;
        int call_cnt = 0;
        stripe_local_t *local = NULL;

        if (!frame || !frame->local || !this) {
                gf_log ("", GF_LOG_ERROR, "Possible NULL deref");
                return ret;
        }

        local = frame->local;

        LOCK (&frame->lock);
        {
                call_cnt = --local->wind_count;

                /**
                 * We overwrite ->op_* values here for subsequent faliure
                 * conditions, hence we propogate the last errno down the
                 * stack.
                 */
                if (op_ret < 0) {
                        local->op_ret = op_ret;
                        local->op_errno = op_errno;
                        goto unlock;
                }
        }

 unlock:
        UNLOCK (&frame->lock);

        if (!call_cnt) {
                STRIPE_STACK_UNWIND (setxattr, frame, local->op_ret,
                                     local->op_errno, xdata);
        }

        return 0;
}

#ifdef HAVE_BD_XLATOR
int
stripe_is_bd (dict_t *this, char *key, data_t *value, void *data)
{
        gf_boolean_t *is_bd = data;

        if (data == NULL)
                return 0;

        if (XATTR_IS_BD (key))
            *is_bd = _gf_true;

        return 0;
}

inline gf_boolean_t
stripe_setxattr_is_bd (dict_t *dict)
{
        gf_boolean_t is_bd = _gf_false;

        if (dict == NULL)
                goto out;

        dict_foreach (dict, stripe_is_bd, &is_bd);
out:
        return is_bd;
}
#else
#define stripe_setxattr_is_bd(dict) _gf_false
#endif

int
stripe_setxattr (call_frame_t *frame, xlator_t *this,
                 loc_t *loc, dict_t *dict, int flags, dict_t *xdata)
{
        int32_t           op_errno = EINVAL;
        xlator_list_t    *trav     = NULL;
        stripe_private_t *priv     = NULL;
        stripe_local_t   *local    = NULL;
        int               i        = 0;
        gf_boolean_t      is_bd    = _gf_false;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        GF_IF_INTERNAL_XATTR_GOTO ("trusted.*stripe*", dict,
                                   op_errno, err);

        priv = this->private;
        trav = this->children;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        frame->local = local;
        local->wind_count = priv->child_count;
        local->op_ret = local->op_errno = 0;

        is_bd = stripe_setxattr_is_bd (dict);

        /**
         * Set xattrs for directories on all subvolumes. Additionally
         * this power is only given to a special client. Bd xlator
         * also needs xattrs for regular files (ie LVs)
         */
        if (((frame->root->pid == GF_CLIENT_PID_GSYNCD) &&
             IA_ISDIR (loc->inode->ia_type)) || is_bd) {
                for (i = 0; i < priv->child_count; i++, trav = trav->next) {
                        STACK_WIND (frame, stripe_setxattr_cbk,
                                    trav->xlator, trav->xlator->fops->setxattr,
                                    loc, dict, flags, xdata);
                }
        } else {
                local->wind_count = 1;
                STACK_WIND (frame, stripe_setxattr_cbk,
                            FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->setxattr,
                            loc, dict, flags, xdata);
        }

        return 0;
err:
        STRIPE_STACK_UNWIND (setxattr, frame, -1,  op_errno, NULL);
        return 0;
}


int
stripe_fsetxattr_cbk (call_frame_t *frame, void *cookie,
                      xlator_t *this, int op_ret, int op_errno, dict_t *xdata)
{
        STRIPE_STACK_UNWIND (fsetxattr, frame, op_ret, op_errno, xdata);
        return 0;
}


int
stripe_is_special_key (dict_t *this,
                       char *key,
                       data_t *value,
                       void *data)
{
        gf_boolean_t *is_special = NULL;

        if (data == NULL) {
                goto out;
        }

        is_special = data;

        if (XATTR_IS_LOCKINFO (key) || XATTR_IS_BD (key))
                *is_special = _gf_true;

out:
        return 0;
}

int32_t
stripe_fsetxattr_everyone_cbk (call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret, int32_t op_errno,
                               dict_t *xdata)
{
        int             call_count = 0;
        stripe_local_t *local      = NULL;

        local = frame->local;

        LOCK (&frame->lock);
        {
                call_count = --local->wind_count;

                if (op_ret < 0) {
                        local->op_ret = op_ret;
                        local->op_errno = op_errno;
                }
        }
        UNLOCK (&frame->lock);

        if (call_count == 0) {
                STRIPE_STACK_UNWIND (fsetxattr, frame, local->op_ret,
                                     local->op_errno, NULL);
        }
        return 0;
}

int
stripe_fsetxattr_to_everyone (call_frame_t *frame, xlator_t *this, fd_t *fd,
                              dict_t *dict, int flags, dict_t *xdata)
{
        xlator_list_t    *trav  = NULL;
        stripe_private_t *priv  = NULL;
        int               ret   = -1;
        stripe_local_t   *local = NULL;

        priv = this->private;

        local = mem_get0 (this->local_pool);
        if (local == NULL) {
                goto out;
        }

        frame->local = local;

        local->wind_count = priv->child_count;

        trav = this->children;

        while (trav) {
                STACK_WIND (frame, stripe_fsetxattr_everyone_cbk,
                            trav->xlator, trav->xlator->fops->fsetxattr,
                            fd, dict, flags, xdata);
                trav = trav->next;
        }

        ret = 0;
out:
        return ret;
}

inline gf_boolean_t
stripe_fsetxattr_is_special (dict_t *dict)
{
        gf_boolean_t is_spl = _gf_false;

        if (dict == NULL) {
                goto out;
        }

        dict_foreach (dict, stripe_is_special_key, &is_spl);

out:
        return is_spl;
}

int
stripe_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                  dict_t *dict, int flags, dict_t *xdata)
{
        int32_t      op_ret = -1, ret = -1, op_errno = EINVAL;
        gf_boolean_t is_spl = _gf_false;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        GF_IF_INTERNAL_XATTR_GOTO ("trusted.*stripe*", dict,
                                   op_errno, err);

        is_spl = stripe_fsetxattr_is_special (dict);
        if (is_spl) {
                ret = stripe_fsetxattr_to_everyone (frame, this, fd, dict,
                                                    flags, xdata);
                if (ret < 0) {
                        op_errno = ENOMEM;
                        goto err;
                }

                goto out;
        }

        STACK_WIND (frame, stripe_fsetxattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetxattr,
                    fd, dict, flags, xdata);
out:
        return 0;
err:
        STRIPE_STACK_UNWIND (fsetxattr, frame, op_ret, op_errno, NULL);
        return 0;
}

int
stripe_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        STRIPE_STACK_UNWIND (removexattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int
stripe_removexattr (call_frame_t *frame, xlator_t *this,
                    loc_t *loc, const char *name, dict_t *xdata)
{
        int32_t         op_errno = EINVAL;

        VALIDATE_OR_GOTO (this, err);

        GF_IF_NATIVE_XATTR_GOTO ("trusted.*stripe*",
                                 name, op_errno, err);

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (loc, err);

        STACK_WIND (frame, stripe_removexattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->removexattr,
                    loc, name, xdata);
        return 0;
err:
        STRIPE_STACK_UNWIND (removexattr, frame, -1,  op_errno, NULL);
        return 0;
}


int
stripe_fremovexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        STRIPE_STACK_UNWIND (fremovexattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int
stripe_fremovexattr (call_frame_t *frame, xlator_t *this,
                     fd_t *fd, const char *name, dict_t *xdata)
{
        int32_t         op_ret   = -1;
        int32_t         op_errno = EINVAL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        GF_IF_NATIVE_XATTR_GOTO ("trusted.*stripe*",
                                 name, op_errno, err);

        STACK_WIND (frame, stripe_fremovexattr_cbk,
                    FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fremovexattr,
                    fd, name, xdata);
        return 0;
 err:
        STRIPE_STACK_UNWIND (fremovexattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int32_t
stripe_readdirp_lookup_cbk (call_frame_t *frame, void *cookie,
                            xlator_t *this, int op_ret, int op_errno,
                            inode_t *inode, struct iatt *stbuf,
                            dict_t *xattr, struct iatt *parent)
{
        stripe_local_t          *local          = NULL;
        call_frame_t            *main_frame     = NULL;
        stripe_local_t          *main_local     = NULL;
        gf_dirent_t             *entry          = NULL;
        call_frame_t            *prev           = NULL;
        int                      done           = 0;

        local = frame->local;
        prev = cookie;

        entry = local->dirent;

        main_frame = local->orig_frame;
        main_local = main_frame->local;
        LOCK (&frame->lock);
        {

                local->call_count--;
                if (!local->call_count)
                        done = 1;
                if (op_ret == -1) {
                        local->op_errno = op_errno;
                        local->op_ret = op_ret;
                        goto unlock;
                }

		if (stripe_ctx_handle(this, prev, local, xattr))
			gf_log(this->name, GF_LOG_ERROR,
				"Error getting fctx info from dict.");

		correct_file_size(stbuf, local->fctx, prev);

                stripe_iatt_merge (stbuf, &entry->d_stat);
                local->stbuf_blocks += stbuf->ia_blocks;
        }
unlock:
        UNLOCK(&frame->lock);

        if (done) {
                inode_ctx_put (entry->inode, this,
                               (uint64_t) (long)local->fctx);

                done = 0;
                LOCK (&main_frame->lock);
                {
                        main_local->wind_count--;
                        if (!main_local->wind_count)
                                done = 1;
                        if (local->op_ret == -1) {
                                main_local->op_errno = local->op_errno;
                                main_local->op_ret = local->op_ret;
                        }
                        entry->d_stat.ia_blocks = local->stbuf_blocks;
                }
                UNLOCK (&main_frame->lock);
                if (done) {
                        main_frame->local = NULL;
                        STRIPE_STACK_UNWIND (readdir, main_frame,
                                             main_local->op_ret,
                                             main_local->op_errno,
                                             &main_local->entries, NULL);
                        gf_dirent_free (&main_local->entries);
                        stripe_local_wipe (main_local);
                        mem_put (main_local);
                }
                frame->local = NULL;
                stripe_local_wipe (local);
                mem_put (local);
                STRIPE_STACK_DESTROY (frame);
        }

        return 0;
}

int32_t
stripe_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno,
                     gf_dirent_t *orig_entries, dict_t *xdata)
{
        stripe_local_t *local = NULL;
        call_frame_t   *prev = NULL;
        gf_dirent_t    *local_entry = NULL;
        gf_dirent_t    *tmp_entry = NULL;
        xlator_list_t  *trav = NULL;
        loc_t          loc = {0, };
        int32_t        count = 0;
        stripe_private_t *priv = NULL;
        int32_t        subvols = 0;
        dict_t         *xattrs = NULL;
        call_frame_t   *local_frame = NULL;
        stripe_local_t *local_ent = NULL;

        if (!this || !frame || !frame->local || !cookie) {
                gf_log ("stripe", GF_LOG_DEBUG, "possible NULL deref");
                goto out;
        }
        prev  = cookie;
        local = frame->local;
        trav = this->children;
        priv = this->private;

        subvols = priv->child_count;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "%s returned error %s",
                                prev->this->name, strerror (op_errno));
                        local->op_errno = op_errno;
                        local->op_ret = op_ret;
                        goto unlock;
                } else {
                        local->op_ret = op_ret;
                        list_splice_init (&orig_entries->list,
                                          &local->entries.list);
                        local->wind_count = op_ret;
                }

        }
unlock:
        UNLOCK (&frame->lock);

        if (op_ret == -1)
                goto out;

        xattrs = dict_new ();
        if (xattrs)
                (void) stripe_xattr_request_build (this, xattrs, 0, 0, 0, 0);
        count = op_ret;
        list_for_each_entry_safe (local_entry, tmp_entry,
                                  (&local->entries.list), list) {

                if (!local_entry)
                        break;
                if (!IA_ISREG (local_entry->d_stat.ia_type)) {
                        LOCK (&frame->lock);
                        {
                                local->wind_count--;
                                count = local->wind_count;
                        }
                        UNLOCK (&frame->lock);
                        continue;
                }

                local_frame = copy_frame (frame);

                if (!local_frame) {
                        op_errno = ENOMEM;
                        op_ret = -1;
                        goto out;
                }

                local_ent = mem_get0 (this->local_pool);
                if (!local_ent) {
                        op_errno = ENOMEM;
                        op_ret = -1;
                        goto out;
                }

                loc.inode = inode_ref (local_entry->inode);

                uuid_copy (loc.gfid, local_entry->d_stat.ia_gfid);

                local_ent->orig_frame = frame;

                local_ent->call_count = subvols;

                local_ent->dirent = local_entry;

                local_frame->local = local_ent;

                trav = this->children;
                while (trav) {
                        STACK_WIND (local_frame, stripe_readdirp_lookup_cbk,
                                    trav->xlator, trav->xlator->fops->lookup,
                                    &loc, xattrs);
                        trav = trav->next;
                }
                loc_wipe (&loc);
        }
out:
        if (!count) {
                /* all entries are directories */
                frame->local = NULL;
                STRIPE_STACK_UNWIND (readdir, frame, local->op_ret,
                                     local->op_errno, &local->entries, NULL);
                gf_dirent_free (&local->entries);
                stripe_local_wipe (local);
                mem_put (local);
        }
        if (xattrs)
                dict_unref (xattrs);
        return 0;

}
int32_t
stripe_readdirp (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, size_t size, off_t off, dict_t *xdata)
{
        stripe_local_t  *local  = NULL;
        stripe_private_t *priv = NULL;
        xlator_list_t   *trav = NULL;
        int             op_errno = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);

        priv = this->private;
        trav = this->children;

        if (priv->first_child_down) {
                op_errno = ENOTCONN;
                goto err;
        }

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        frame->local = local;

        local->fd = fd_ref (fd);

        local->wind_count = 0;

        local->count = 0;
        local->op_ret = -1;
        INIT_LIST_HEAD(&local->entries);

        if (!trav)
                goto err;

        STACK_WIND (frame, stripe_readdirp_cbk, trav->xlator,
                    trav->xlator->fops->readdirp, fd, size, off, xdata);
        return 0;
err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        STRIPE_STACK_UNWIND (readdir, frame, -1, op_errno, NULL, NULL);

        return 0;

}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                goto out;

        ret = xlator_mem_acct_init (this, gf_stripe_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                        "failed");
                goto out;
        }

out:
        return ret;
}

static int
clear_pattern_list (stripe_private_t *priv)
{
        struct stripe_options *prev = NULL;
        struct stripe_options *trav = NULL;
        int                    ret = -1;

        GF_VALIDATE_OR_GOTO ("stripe", priv, out);

        trav = priv->pattern;
        priv->pattern = NULL;
        while (trav) {
                prev = trav;
                trav = trav->next;
                GF_FREE (prev);
        }

        ret = 0;
 out:
        return ret;


}


int
reconfigure (xlator_t *this, dict_t *options)
{

        stripe_private_t *priv = NULL;
        data_t           *data = NULL;
        int               ret = -1;
        volume_option_t  *opt = NULL;

        GF_ASSERT (this);
        GF_ASSERT (this->private);

        priv = this->private;


        ret = 0;
        LOCK (&priv->lock);
        {
                ret = clear_pattern_list (priv);
                if (ret)
                         goto unlock;

                data = dict_get (options, "block-size");
                if (data) {
                        ret = set_stripe_block_size (this, priv, data->data);
                        if (ret)
                                goto unlock;
                } else {
                        opt = xlator_volume_option_get (this, "block-size");
                        if (!opt) {
                                gf_log (this->name, GF_LOG_WARNING,
                                        "option 'block-size' not found");
                                ret = -1;
                                goto unlock;
                        }

                        if (gf_string2bytesize (opt->default_value, &priv->block_size)){
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Unable to set default block-size ");
                                ret = -1;
                                goto unlock;
                        }
                }

		GF_OPTION_RECONF("coalesce", priv->coalesce, options, bool,
				unlock);
        }
 unlock:
        UNLOCK (&priv->lock);
        if (ret)
                goto out;

        ret = 0;
 out:
        return ret;

}

/**
 * init - This function is called when xlator-graph gets initialized.
 *     The option given in volfiles are parsed here.
 * @this -
 */
int32_t
init (xlator_t *this)
{
        stripe_private_t *priv = NULL;
        volume_option_t  *opt = NULL;
        xlator_list_t    *trav = NULL;
        data_t           *data = NULL;
        int32_t           count = 0;
        int               ret = -1;

        if (!this)
                goto out;

        trav = this->children;
        while (trav) {
                count++;
                trav = trav->next;
        }

        if (!count) {
                gf_log (this->name, GF_LOG_ERROR,
                        "stripe configured without \"subvolumes\" option. "
                        "exiting");
                goto out;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile ");
        }

        if (count == 1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "stripe configured with only one \"subvolumes\" option."
                        " please check the volume. exiting");
                goto out;
        }

        priv = GF_CALLOC (1, sizeof (stripe_private_t),
                          gf_stripe_mt_stripe_private_t);

        if (!priv)
                goto out;
        priv->xl_array = GF_CALLOC (count, sizeof (xlator_t *),
                                    gf_stripe_mt_xlator_t);
        if (!priv->xl_array)
                goto out;

        priv->last_event = GF_CALLOC (count, sizeof (int),
                                      gf_stripe_mt_int32_t);
        if (!priv->last_event)
                goto out;

        priv->child_count = count;
        LOCK_INIT (&priv->lock);

        trav = this->children;
        count = 0;
        while (trav) {
                priv->xl_array[count++] = trav->xlator;
                trav = trav->next;
        }

        if (count > 256) {
                gf_log (this->name, GF_LOG_ERROR,
                        "maximum number of stripe subvolumes supported "
                        "is 256");
                goto out;
        }

        ret = 0;
        LOCK (&priv->lock);
        {
                opt = xlator_volume_option_get (this, "block-size");
                if (!opt) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "option 'block-size' not found");
                        ret = -1;
                        goto unlock;
                }
                if (gf_string2bytesize (opt->default_value, &priv->block_size)){
                        gf_log (this->name, GF_LOG_ERROR,
                                "Unable to set default block-size ");
                        ret = -1;
                        goto unlock;
                }
                /* option stripe-pattern *avi:1GB,*pdf:16K */
                data = dict_get (this->options, "block-size");
                if (data) {
                        ret = set_stripe_block_size (this, priv, data->data);
                        if (ret)
                                goto unlock;
                }
        }
 unlock:
        UNLOCK (&priv->lock);
        if (ret)
                goto out;

        GF_OPTION_INIT ("use-xattr", priv->xattr_supported, bool, out);
        /* notify related */
        priv->nodes_down = priv->child_count;

	GF_OPTION_INIT("coalesce", priv->coalesce, bool, out);

        this->local_pool = mem_pool_new (stripe_local_t, 128);
        if (!this->local_pool) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to create local_t's memory pool");
                goto out;
        }

        this->private = priv;

        ret = 0;
out:
        if (ret) {
                if (priv) {
                        GF_FREE (priv->xl_array);
                        GF_FREE (priv);
                }
        }
        return ret;
}

/**
 * fini -   Free all the private variables
 * @this -
 */
void
fini (xlator_t *this)
{
        stripe_private_t      *priv = NULL;
        struct stripe_options *prev = NULL;
        struct stripe_options *trav = NULL;

        if (!this)
                goto out;

        priv = this->private;
        if (priv) {
                this->private = NULL;
                GF_FREE (priv->xl_array);

                trav = priv->pattern;
                while (trav) {
                        prev = trav;
                        trav = trav->next;
                        GF_FREE (prev);
                }
                GF_FREE (priv->last_event);
                LOCK_DESTROY (&priv->lock);
                GF_FREE (priv);
        }

out:
        return;
}

int32_t
stripe_getxattr_unwind (call_frame_t *frame,
                        int op_ret, int op_errno, dict_t *dict, dict_t *xdata)

{
        STRIPE_STACK_UNWIND (getxattr, frame, op_ret, op_errno, dict, xdata);
        return 0;
}

int
stripe_internal_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                              int op_ret, int op_errno, dict_t *xattr,
                              dict_t *xdata)
{

        char        size_key[256]  = {0,};
        char        index_key[256] = {0,};
        char        count_key[256] = {0,};
        char        coalesce_key[256] = {0,};

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (frame->local, out);

        if (!xattr || (op_ret == -1))
            goto out;

        sprintf (size_key, "trusted.%s.stripe-size", this->name);
        sprintf (count_key, "trusted.%s.stripe-count", this->name);
        sprintf (index_key, "trusted.%s.stripe-index", this->name);
	sprintf (coalesce_key, "trusted.%s.stripe-coalesce", this->name);

        dict_del (xattr, size_key);
        dict_del (xattr, count_key);
        dict_del (xattr, index_key);
        dict_del (xattr, coalesce_key);

out:
        STRIPE_STACK_UNWIND (getxattr, frame, op_ret, op_errno, xattr, xdata);

        return 0;

}

int
stripe_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno, dict_t *xattr, dict_t *xdata)
{
        int                     call_cnt = 0;
        stripe_local_t         *local = NULL;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (frame->local, out);

        local = frame->local;

        LOCK (&frame->lock);
        {
                call_cnt = --local->wind_count;
        }
        UNLOCK (&frame->lock);

        if (!xattr || (op_ret < 0))
                goto out;

        local->op_ret = 0;

        if (!local->xattr) {
                local->xattr = dict_ref (xattr);
        } else {
                stripe_aggregate_xattr (local->xattr, xattr);
        }

out:
        if (!call_cnt) {
                STRIPE_STACK_UNWIND (getxattr, frame, local->op_ret, op_errno,
                                     local->xattr, xdata);
        }

        return 0;
}

int32_t
stripe_vgetxattr_cbk (call_frame_t *frame, void *cookie,
                      xlator_t *this, int32_t op_ret, int32_t op_errno,
                      dict_t *dict, dict_t *xdata)
{
        stripe_local_t      *local         = NULL;
        int32_t              callcnt       = 0;
        int32_t              ret           = -1;
        long                 cky           = 0;
        void                *xattr_val     = NULL;
        void                *xattr_serz    = NULL;
        stripe_xattr_sort_t *xattr         = NULL;
        dict_t              *stripe_xattr  = NULL;

        if (!frame || !frame->local || !this) {
                gf_log ("", GF_LOG_ERROR, "Possible NULL deref");
                return ret;
        }

        local = frame->local;
        cky = (long) cookie;

        if (local->xsel[0] == '\0') {
                gf_log (this->name, GF_LOG_ERROR, "Empty xattr in cbk");
                return ret;
        }

        LOCK (&frame->lock);
        {
                callcnt = --local->wind_count;

                if (!dict || (op_ret < 0))
                        goto out;

                if (!local->xattr_list)
                        local->xattr_list = (stripe_xattr_sort_t *)
                                GF_CALLOC (local->nallocs,
                                           sizeof (stripe_xattr_sort_t),
                                           gf_stripe_mt_xattr_sort_t);

                if (local->xattr_list) {
                        xattr = local->xattr_list + (int32_t) cky;

                        ret = dict_get_ptr_and_len (dict, local->xsel,
                                                    &xattr_val,
                                                    &xattr->xattr_len);
                        if (xattr->xattr_len == 0)
                                goto out;

                        xattr->pos = cky;
                        xattr->xattr_value = gf_memdup (xattr_val,
                                                        xattr->xattr_len);

                        if (xattr->xattr_value != NULL)
                                local->xattr_total_len += xattr->xattr_len + 1;
                }
        }
 out:
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (!local->xattr_total_len)
                        goto unwind;

                stripe_xattr = dict_new ();
                if (!stripe_xattr)
                        goto unwind;

                /* select filler based on ->xsel */
                if (XATTR_IS_PATHINFO (local->xsel))
                        ret = stripe_fill_pathinfo_xattr (this, local,
                                                          (char **)&xattr_serz);
                else if (XATTR_IS_LOCKINFO (local->xsel)) {
                        ret = stripe_fill_lockinfo_xattr (this, local,
                                                          &xattr_serz);
                } else {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Unknown xattr in xattr request");
                        goto unwind;
                }

                if (!ret) {
                        ret = dict_set_dynptr (stripe_xattr, local->xsel,
                                               xattr_serz,
                                               local->xattr_total_len);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Can't set %s key in dict",
                                        local->xsel);
                }

        unwind:
                STRIPE_STACK_UNWIND (getxattr, frame, op_ret, op_errno,
                                     stripe_xattr, NULL);

                ret = stripe_free_xattr_str (local);

                GF_FREE (local->xattr_list);

                if (stripe_xattr)
                        dict_unref (stripe_xattr);
        }

        return ret;
}

int32_t
stripe_getxattr (call_frame_t *frame, xlator_t *this,
                 loc_t *loc, const char *name, dict_t *xdata)
{
        stripe_local_t    *local    = NULL;
        xlator_list_t     *trav     = NULL;
        stripe_private_t  *priv     = NULL;
        int32_t            op_errno = EINVAL;
        int                i        = 0;
        xlator_t         **sub_volumes;
        int                ret      = 0;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (loc->path, err);
        VALIDATE_OR_GOTO (loc->inode, err);

        priv = this->private;
        trav = this->children;

        /* Initialization */
        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->op_ret = -1;
        frame->local = local;
        loc_copy (&local->loc, loc);


        if (name && (strcmp (GF_XATTR_MARKER_KEY, name) == 0)
            && (GF_CLIENT_PID_GSYNCD == frame->root->pid)) {
                local->marker.call_count = priv->child_count;

                sub_volumes = alloca ( priv->child_count *
                                       sizeof (xlator_t *));
                for (i = 0, trav = this->children; trav ;
                     trav = trav->next, i++) {

                        *(sub_volumes + i)  = trav->xlator;

                }

                if (cluster_getmarkerattr (frame, this, loc, name,
                                           local, stripe_getxattr_unwind,
                                           sub_volumes, priv->child_count,
                                           MARKER_UUID_TYPE, marker_uuid_default_gauge,
                                           priv->vol_uuid)) {
                        op_errno = EINVAL;
                        goto err;
                }

                return 0;
        }

        if (name && strncmp (name, GF_XATTR_QUOTA_SIZE_KEY,
                             strlen (GF_XATTR_QUOTA_SIZE_KEY)) == 0) {
                local->wind_count = priv->child_count;

                for (i = 0, trav=this->children; i < priv->child_count; i++,
                             trav = trav->next) {
                        STACK_WIND (frame, stripe_getxattr_cbk,
                                    trav->xlator, trav->xlator->fops->getxattr,
                                    loc, name, xdata);
                }

                return 0;
        }

        if (name && (XATTR_IS_PATHINFO (name))) {
                if (IA_ISREG (loc->inode->ia_type)) {
                        ret = inode_ctx_get (loc->inode, this,
                                             (uint64_t *) &local->fctx);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "stripe size unavailable from fctx"
                                        " relying on pathinfo could lead to"
                                        " wrong results");
                }

                local->nallocs = local->wind_count = priv->child_count;
                (void) strncpy (local->xsel, name, strlen (name));

                /**
                 * for xattrs that need info from all childs, fill ->xsel
                 * as above and call the filler function in cbk based on
                 * it
                 */
                for (i = 0, trav = this->children; i < priv->child_count; i++,
                     trav = trav->next) {
                        STACK_WIND_COOKIE (frame, stripe_vgetxattr_cbk,
                                           (void *) (long) i, trav->xlator,
                                           trav->xlator->fops->getxattr,
                                           loc, name, xdata);
                }

                return 0;
        }

        if (name &&(*priv->vol_uuid)) {
                if ((match_uuid_local (name, priv->vol_uuid) == 0)
                    && (GF_CLIENT_PID_GSYNCD == frame->root->pid)) {

                        if (!IA_FILE_OR_DIR (loc->inode->ia_type))
                                local->marker.call_count = 1;
                        else
                                local->marker.call_count = priv->child_count;

                        sub_volumes = alloca (local->marker.call_count *
                                              sizeof (xlator_t *));

                        for (i = 0, trav = this->children;
                             i < local->marker.call_count;
                             i++, trav = trav->next) {
                                *(sub_volumes + i) = trav->xlator;

                        }

                        if (cluster_getmarkerattr (frame, this, loc, name,
                                                   local,
                                                   stripe_getxattr_unwind,
                                                   sub_volumes,
                                                   local->marker.call_count,
                                                   MARKER_XTIME_TYPE,
                                                   marker_xtime_default_gauge,
                                                   priv->vol_uuid)) {
                                op_errno = EINVAL;
                                goto err;
                        }

                        return 0;
                }
        }


        STACK_WIND (frame, stripe_internal_getxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getxattr, loc, name, xdata);

        return 0;

err:
        STRIPE_STACK_UNWIND (getxattr, frame, -1, op_errno, NULL, NULL);
        return 0;
}

inline gf_boolean_t
stripe_is_special_xattr (const char *name)
{
        gf_boolean_t    is_spl = _gf_false;

        if (!name) {
                goto out;
        }

        if (!strncmp (name, GF_XATTR_LOCKINFO_KEY,
                      strlen (GF_XATTR_LOCKINFO_KEY))
            || XATTR_IS_PATHINFO (name))
                is_spl = _gf_true;
out:
        return is_spl;
}

int32_t
stripe_fgetxattr_from_everyone (call_frame_t *frame, xlator_t *this, fd_t *fd,
                                const char *name, dict_t *xdata)
{
        stripe_local_t   *local = NULL;
        stripe_private_t *priv  = NULL;
        int32_t           ret   = -1, op_errno = 0;
        int               i     = 0;
        xlator_list_t    *trav  = NULL;

        priv = this->private;

        local = mem_get0 (this->local_pool);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->op_ret = -1;
        frame->local = local;

        strncpy (local->xsel, name, strlen (name));
        local->nallocs = local->wind_count = priv->child_count;

        for (i = 0, trav = this->children; i < priv->child_count; i++,
                     trav = trav->next) {
                STACK_WIND_COOKIE (frame, stripe_vgetxattr_cbk,
                                   (void *) (long) i, trav->xlator,
                                   trav->xlator->fops->fgetxattr,
                                   fd, name, xdata);
        }

        return 0;

err:
        STACK_UNWIND_STRICT (fgetxattr, frame, -1, op_errno, NULL, NULL);
        return ret;
}

int32_t
stripe_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                  const char *name, dict_t *xdata)
{
        if (stripe_is_special_xattr (name)) {
                stripe_fgetxattr_from_everyone (frame, this, fd, name, xdata);
                goto out;
        }

        STACK_WIND (frame, stripe_internal_getxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fgetxattr, fd, name, xdata);

out:
        return 0;
}



int32_t
stripe_priv_dump (xlator_t *this)
{
        char                    key[GF_DUMP_MAX_BUF_LEN];
        int                     i = 0;
        stripe_private_t       *priv = NULL;
        int                     ret = -1;
        struct stripe_options  *options = NULL;

        GF_VALIDATE_OR_GOTO ("stripe", this, out);

        priv = this->private;
        if (!priv)
                goto out;

        ret = TRY_LOCK (&priv->lock);
        if (ret != 0)
                goto out;

        gf_proc_dump_add_section("xlator.cluster.stripe.%s.priv", this->name);
        gf_proc_dump_write("child_count","%d", priv->child_count);

        for (i = 0; i < priv->child_count; i++) {
                sprintf (key, "subvolumes[%d]", i);
                gf_proc_dump_write (key, "%s.%s", priv->xl_array[i]->type,
                                    priv->xl_array[i]->name);
        }

        options = priv->pattern;
        while (options != NULL) {
                gf_proc_dump_write ("path_pattern", "%s", priv->pattern->path_pattern);
                gf_proc_dump_write ("options_block_size", "%ul", options->block_size);

                options = options->next;
        }

        gf_proc_dump_write ("block_size", "%ul", priv->block_size);
        gf_proc_dump_write ("nodes-down", "%d", priv->nodes_down);
        gf_proc_dump_write ("first-child_down", "%d", priv->first_child_down);
        gf_proc_dump_write ("xattr_supported", "%d", priv->xattr_supported);

        UNLOCK (&priv->lock);

out:
        return ret;
}

struct xlator_fops fops = {
        .stat           = stripe_stat,
        .unlink         = stripe_unlink,
        .rename         = stripe_rename,
        .link           = stripe_link,
        .truncate       = stripe_truncate,
        .create         = stripe_create,
        .open           = stripe_open,
        .readv          = stripe_readv,
        .writev         = stripe_writev,
        .statfs         = stripe_statfs,
        .flush          = stripe_flush,
        .fsync          = stripe_fsync,
        .ftruncate      = stripe_ftruncate,
        .fstat          = stripe_fstat,
        .mkdir          = stripe_mkdir,
        .rmdir          = stripe_rmdir,
        .lk             = stripe_lk,
        .opendir        = stripe_opendir,
        .fsyncdir       = stripe_fsyncdir,
        .setattr        = stripe_setattr,
        .fsetattr       = stripe_fsetattr,
        .lookup         = stripe_lookup,
        .mknod          = stripe_mknod,
        .setxattr       = stripe_setxattr,
        .fsetxattr      = stripe_fsetxattr,
        .getxattr       = stripe_getxattr,
        .fgetxattr      = stripe_fgetxattr,
        .removexattr    = stripe_removexattr,
        .fremovexattr   = stripe_fremovexattr,
        .readdirp       = stripe_readdirp,
	.fallocate	= stripe_fallocate,
	.discard	= stripe_discard,
        .zerofill       = stripe_zerofill,
};

struct xlator_cbks cbks = {
        .release = stripe_release,
        .forget  = stripe_forget,
};

struct xlator_dumpops dumpops = {
        .priv = stripe_priv_dump,
};

struct volume_options options[] = {
        { .key  = {"block-size"},
          .type = GF_OPTION_TYPE_SIZE_LIST,
          .default_value = "128KB",
          .min = STRIPE_MIN_BLOCK_SIZE,
          .description = "Size of the stripe unit that would be read "
                         "from or written to the striped servers."
        },
        { .key  = {"use-xattr"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "true"
        },
	{ .key = {"coalesce"},
	  .type = GF_OPTION_TYPE_BOOL,
	  .default_value = "true",
	  .description = "Enable/Disable coalesce mode to flatten striped "
			 "files as stored on the server (i.e., eliminate holes "
			 "caused by the traditional format)."
	},
        { .key  = {NULL} },
};
