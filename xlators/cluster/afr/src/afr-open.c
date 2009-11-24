/*
   Copyright (c) 2007-2009 Gluster, Inc. <http://www.gluster.com>
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

#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

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

#include "afr-self-heal.h"


int
afr_open_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
			int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                        struct stat *postbuf)
{
	afr_local_t * local = frame->local;
        int ret = 0;

        ret = afr_fd_ctx_set (this, local->fd);

        if (ret < 0) {
                local->op_ret   =   -1;
                local->op_errno = -ret;
        }

	AFR_STACK_UNWIND (open, frame, local->op_ret, local->op_errno,
			  local->fd);
	return 0;
}


int
afr_open_cbk (call_frame_t *frame, void *cookie,
	      xlator_t *this, int32_t op_ret, int32_t op_errno,
	      fd_t *fd)
{
	afr_local_t *  local = NULL;
	afr_private_t * priv = NULL;

        int ret = 0;

	int call_count = -1;

	priv  = this->private;
	local = frame->local;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			local->op_errno = op_errno;
		}

		if (op_ret >= 0) {
			local->op_ret = op_ret;
                        local->success_count++;
		}
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		if ((local->cont.open.flags & O_TRUNC)
		    && (local->op_ret >= 0)) {
			STACK_WIND (frame, afr_open_ftruncate_cbk,
				    this, this->fops->ftruncate,
				    fd, 0);
		} else {
                        ret = afr_fd_ctx_set (this, fd);

                        if (ret < 0) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "could not set fd ctx for fd=%p",
                                        fd);

                                local->op_ret   = -1;
                                local->op_errno = -ret;
                        }

                        AFR_STACK_UNWIND (open, frame, local->op_ret,
                                          local->op_errno, local->fd);
		}
	}

	return 0;
}


int
afr_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          fd_t *fd, int32_t wbflags)
{
	afr_private_t * priv  = NULL;
	afr_local_t *   local = NULL;

	int     i = 0;
	int   ret = -1;

	int32_t call_count = 0;	
	int32_t op_ret   = -1;
	int32_t op_errno = 0;
	int32_t wind_flags = flags & (~O_TRUNC);

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);
	VALIDATE_OR_GOTO (loc, out);

	priv = this->private;

        if (afr_is_split_brain (this, loc->inode)) {
		/* self-heal failed */
		op_errno = EIO;
		goto out;
	}

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	frame->local = local;
	call_count   = local->call_count;

	local->cont.open.flags = flags;
	local->fd = fd_ref (fd);

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_open_cbk, (void *) (long) i,
					   priv->children[i],
					   priv->children[i]->fops->open,
					   loc, wind_flags, fd, wbflags);

			if (!--call_count)
				break;
		}
	}

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (open, frame, op_ret, op_errno, fd);
	}

	return 0;
}


int
afr_up_down_flush_sh_completion_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;

        local = frame->local;

        local->transaction.post_post_op (frame, this);

        return 0;
}


int
afr_up_down_flush_post_post_op (call_frame_t *frame, xlator_t *this)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        afr_self_heal_t *sh = NULL;

        priv  = this->private;
        local = frame->local;
        sh    = &local->self_heal;

        sh->calling_fop = GF_FOP_FLUSH;

//        sh->healing_fd = local->fd;

//        sh->healing_fd_opened = _gf_true;

        local->cont.lookup.inode = local->fd->inode;

        inode_path (local->fd->inode, NULL, (char **)&local->loc.path);
        local->loc.name   = strrchr (local->loc.path, '/');
        local->loc.inode  = inode_ref (local->fd->inode);
        local->loc.parent = inode_parent (local->fd->inode, 0, NULL);

        sh->data_lock_held    = _gf_true;

        local->need_data_self_heal     = _gf_true;
        local->cont.lookup.buf.st_mode = local->fd->inode->st_mode;
        local->child_count             = afr_up_children_count (priv->child_count,
                                                                local->child_up);

        sh->flush_self_heal_cbk = afr_up_down_flush_sh_completion_cbk;

        afr_self_heal (frame, this, afr_up_down_flush_sh_completion_cbk,
                       _gf_false);

        return 0;
}


int
afr_up_down_flush_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	local = frame->local;
	priv  = this->private;

        local->transaction.resume (frame, this);
	return 0;
}


int
afr_up_down_flush_done (call_frame_t *frame, xlator_t *this)
{
        afr_private_t *priv = NULL;
	afr_local_t *local  = NULL;

        uint64_t       ctx;
        afr_fd_ctx_t * fd_ctx = NULL;

        int _ret = -1;
        int i    = 0;

        priv  = this->private;
	local = frame->local;

        LOCK (&local->fd->lock);
        {
                _ret = __fd_ctx_get (local->fd, this, &ctx);

                if (_ret < 0) {
                        goto out;
                }

                fd_ctx = (afr_fd_ctx_t *)(long) ctx;

                fd_ctx->down_count = priv->down_count;
                fd_ctx->up_count   = priv->up_count;

                for (i = 0; i < priv->child_count; i++) {
                        if (local->child_up[i])
                                fd_ctx->pre_op_done[i] = 0;
                }
        }
out:
        UNLOCK (&local->fd->lock);

        local->up_down_flush_cbk (frame, this);

	return 0;
}


int
afr_up_down_flush (call_frame_t *frame, xlator_t *this, fd_t *fd,
                   afr_flush_type type)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;

	int op_ret   = -1;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

        local = frame->local;

        local->op = GF_FOP_FLUSH;

        local->fd = fd_ref (local->fd);

        local->transaction.fop          = afr_up_down_flush_wind;
        local->transaction.done         = afr_up_down_flush_done;

        switch (type) {
        case AFR_CHILD_UP_FLUSH:
                local->transaction.post_post_op = afr_up_down_flush_post_post_op;
                break;

        case AFR_CHILD_DOWN_FLUSH:
                local->transaction.post_post_op = NULL;
                break;
        }

        local->transaction.start  = 0;
        local->transaction.len    = 0;

        gf_log (this->name, GF_LOG_TRACE,
                "doing up/down flush on fd=%p",
                fd);

        afr_transaction (frame, this, AFR_FLUSH_TRANSACTION);

	op_ret = 0;
out:
	return 0;
}
