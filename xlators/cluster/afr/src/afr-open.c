/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>

#include "glusterfs.h"
#include "afr.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"
#include "list.h"
#include "call-stub.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat-errno.h"
#include "compat.h"
#include "byte-order.h"
#include "statedump.h"

#include "fd.h"

#include "afr-inode-read.h"
#include "afr-inode-write.h"
#include "afr-dir-read.h"
#include "afr-dir-write.h"
#include "afr-transaction.h"


gf_boolean_t
afr_is_fd_fixable (fd_t *fd)
{
        if (!fd || !fd->inode)
                return _gf_false;
        else if (fd_is_anonymous (fd))
                return _gf_false;
        else if (gf_uuid_is_null (fd->inode->gfid))
                return _gf_false;

        return _gf_true;
}


int
afr_open_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                        struct iatt *postbuf, dict_t *xdata)
{
        afr_local_t * local = frame->local;

        AFR_STACK_UNWIND (open, frame, local->op_ret, local->op_errno,
                          local->fd, xdata);
        return 0;
}


int
afr_open_cbk (call_frame_t *frame, void *cookie,
              xlator_t *this, int32_t op_ret, int32_t op_errno,
              fd_t *fd, dict_t *xdata)
{
        afr_local_t *  local       = NULL;
        int            call_count  = -1;
        int            child_index = (long) cookie;
	afr_fd_ctx_t  *fd_ctx = NULL;

        local = frame->local;
	fd_ctx = local->fd_ctx;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        local->op_errno = op_errno;
			fd_ctx->opened_on[child_index] = AFR_FD_NOT_OPENED;
                } else {
                        local->op_ret = op_ret;
			fd_ctx->opened_on[child_index] = AFR_FD_OPENED;
			if (!local->xdata_rsp && xdata)
				local->xdata_rsp = dict_ref (xdata);
                }
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                if ((fd_ctx->flags & O_TRUNC) && (local->op_ret >= 0)) {
                        STACK_WIND (frame, afr_open_ftruncate_cbk,
                                    this, this->fops->ftruncate,
                                    fd, 0, NULL);
                } else {
                        AFR_STACK_UNWIND (open, frame, local->op_ret,
                                          local->op_errno, local->fd,
					  local->xdata_rsp);
                }
        }

        return 0;
}

int
afr_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          fd_t *fd, dict_t *xdata)
{
        afr_private_t * priv       = NULL;
        afr_local_t *   local      = NULL;
        int             i          = 0;
        int32_t         call_count = 0;
        int32_t         op_errno   = 0;
	afr_fd_ctx_t   *fd_ctx = NULL;

        //We can't let truncation to happen outside transaction.

        priv = this->private;

	local = AFR_FRAME_INIT (frame, op_errno);
	if (!local)
		goto out;

        local->op = GF_FOP_OPEN;
	fd_ctx = afr_fd_ctx_get (fd, this);
	if (!fd_ctx) {
		op_errno = ENOMEM;
		goto out;
	}

        if (!afr_is_consistent_io_possible (local, priv, &op_errno))
		goto out;

        local->fd = fd_ref (fd);
	local->fd_ctx = fd_ctx;
	fd_ctx->flags = flags;

        call_count = local->call_count;

        local->cont.open.flags = flags;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_open_cbk, (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->open,
                                           loc, (flags & ~O_TRUNC), fd, xdata);
                        if (!--call_count)
                                break;
                }
        }

	return 0;
out:
	AFR_STACK_UNWIND (open, frame, -1, op_errno, fd, NULL);

        return 0;
}

int
afr_openfd_fix_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, fd_t *fd,
                         dict_t *xdata)
{
        afr_local_t   *local      = NULL;
        afr_private_t *priv       = NULL;
        afr_fd_ctx_t  *fd_ctx     = NULL;
        int           call_count  = 0;
        int           child_index = (long) cookie;

        priv     = this->private;
        local    = frame->local;

        if (op_ret >= 0) {
                gf_msg_debug (this->name, 0, "fd for %s opened "
                              "successfully on subvolume %s", local->loc.path,
                              priv->children[child_index]->name);
        } else {
                gf_msg (this->name, fop_log_level (GF_FOP_OPEN, op_errno),
                        op_errno, AFR_MSG_OPEN_FAIL, "Failed to open %s on "
                        "subvolume %s", local->loc.path,
                        priv->children[child_index]->name);
        }

        fd_ctx = local->fd_ctx;

        LOCK (&local->fd->lock);
        {
                if (op_ret >= 0) {
                        fd_ctx->opened_on[child_index] = AFR_FD_OPENED;
                } else {
                        fd_ctx->opened_on[child_index] = AFR_FD_NOT_OPENED;
                }
        }
        UNLOCK (&local->fd->lock);

        call_count = afr_frame_return (frame);
        if (call_count == 0)
                AFR_STACK_DESTROY (frame);

        return 0;
}


static int
afr_fd_ctx_need_open (fd_t *fd, xlator_t *this, unsigned char *need_open)
{
	afr_fd_ctx_t *fd_ctx = NULL;
	afr_private_t *priv = NULL;
	int i = 0;
	int count = 0;

	priv = this->private;

	fd_ctx = afr_fd_ctx_get (fd, this);
	if (!fd_ctx)
		return 0;

	LOCK (&fd->lock);
	{
		for (i = 0; i < priv->child_count; i++) {
			if (fd_ctx->opened_on[i] == AFR_FD_NOT_OPENED &&
			    priv->child_up[i]) {
				fd_ctx->opened_on[i] = AFR_FD_OPENING;
				need_open[i] = 1;
				count++;
			} else {
				need_open[i] = 0;
			}
		}
	}
	UNLOCK (&fd->lock);

	return count;
}


void
afr_fix_open (fd_t *fd, xlator_t *this)
{
        afr_private_t *priv    = NULL;
        int           i        = 0;
        call_frame_t  *frame   = NULL;
        afr_local_t   *local   = NULL;
        int           ret      = -1;
        int32_t       op_errno = 0;
        afr_fd_ctx_t  *fd_ctx  = NULL;
	unsigned char *need_open = NULL;
	int call_count = 0;

        priv  = this->private;

        if (!afr_is_fd_fixable (fd))
                goto out;

        fd_ctx = afr_fd_ctx_get (fd, this);
        if (!fd_ctx)
                goto out;

	need_open = alloca0 (priv->child_count);

	call_count = afr_fd_ctx_need_open (fd, this, need_open);
	if (!call_count)
		goto out;

        frame = create_frame (this, this->ctx->pool);
        if (!frame)
                goto out;

	local = AFR_FRAME_INIT (frame, op_errno);
	if (!local)
		goto out;

        local->loc.inode = inode_ref (fd->inode);
        ret = loc_path (&local->loc, NULL);
        if (ret < 0)
                goto out;

        local->fd = fd_ref (fd);
	local->fd_ctx = fd_ctx;

        local->call_count = call_count;

        gf_msg_debug (this->name, 0, "need open count: %d",
                      call_count);

        for (i = 0; i < priv->child_count; i++) {
                if (!need_open[i])
                        continue;

                if (IA_IFDIR == fd->inode->ia_type) {
                        gf_msg_debug (this->name, 0,
                                      "opening fd for dir %s on subvolume %s",
                                      local->loc.path, priv->children[i]->name);

                        STACK_WIND_COOKIE (frame, afr_openfd_fix_open_cbk,
                                           (void*) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->opendir,
                                           &local->loc, local->fd,
                                           NULL);
                } else {
                        gf_msg_debug (this->name, 0,
                                      "opening fd for file %s on subvolume %s",
                                      local->loc.path, priv->children[i]->name);

                        STACK_WIND_COOKIE (frame, afr_openfd_fix_open_cbk,
                                           (void *)(long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->open,
                                           &local->loc,
                                           fd_ctx->flags & (~O_TRUNC),
                                           local->fd, NULL);
                }

		if (!--call_count)
			break;
        }

	return;
out:
        if (frame)
                AFR_STACK_DESTROY (frame);
}
