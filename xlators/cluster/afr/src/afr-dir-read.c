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

#include "afr.h"


int32_t
afr_opendir_cbk (call_frame_t *frame, void *cookie,
		 xlator_t *this, int32_t op_ret, int32_t op_errno,
		 fd_t *fd)
{
	afr_local_t * local  = NULL;

	int call_count = -1;

	LOCK (&frame->lock);
	{
		local = frame->local;

		if (op_ret == 0)
			local->op_ret = 0;

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		AFR_STACK_UNWIND (frame, local->op_ret,
				  local->op_errno, local->fd);
	}

	return 0;
}


int32_t 
afr_opendir (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, fd_t *fd)
{
	afr_private_t * priv        = NULL;
	afr_local_t   * local       = NULL;

	int             child_count = 0;
	int             i           = 0;

	int ret = -1;
	int call_count = -1;

	int32_t         op_ret   = -1;
	int32_t         op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	child_count = priv->child_count;

	ALLOC_OR_GOTO (local, afr_local_t, out);
	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	frame->local = local;
	local->fd    = fd_ref (fd);

	call_count = local->call_count;
	
	for (i = 0; i < child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND (frame, afr_opendir_cbk, 
				    priv->children[i],
				    priv->children[i]->fops->opendir,
				    loc, fd);

			if (!--call_count)
				break;
		}
	}

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, fd);
	}

	return 0;
}


/**
 * Common algorithm for directory read calls:
 * 
 * - Try the fop on the first child that is up
 * - if we have failed due to ENOTCONN:
 *     try the next child
 *
 * Applicable to: readdir
 */

int32_t
afr_readdir_cbk (call_frame_t *frame, void *cookie,
		 xlator_t *this, int32_t op_ret, int32_t op_errno,
		 gf_dirent_t *entries)
{
        afr_private_t * priv     = NULL;
        afr_local_t *   local    = NULL;
        xlator_t **     children = NULL;

        gf_dirent_t * entry = NULL;

        int child_index = -1;

        priv     = this->private;
        children = priv->children;

        local = frame->local;

        child_index = (long) cookie;

        if (op_ret != -1) {
                list_for_each_entry (entry, &entries->list, list) {
                        entry->d_ino = afr_itransform (entry->d_ino,
                                                       priv->child_count,
                                                       child_index);
                }
        }

        AFR_STACK_UNWIND (frame, op_ret, op_errno, entries);

        return 0;
}


int32_t
afr_readdir (call_frame_t *frame, xlator_t *this,
	     fd_t *fd, size_t size, off_t offset)
{
	afr_private_t * priv       = NULL;
	xlator_t **     children   = NULL;
	int             call_child = 0;
	afr_local_t     *local     = NULL;

	int ret = -1;

	int32_t op_ret   = -1;
	int32_t op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv     = this->private;
	children = priv->children;

	ALLOC_OR_GOTO (local, afr_local_t, out);
	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}
						
	frame->local = local;

	call_child = afr_first_up_child (priv);
	if (call_child == -1) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_DEBUG,
			"no child is up");
		goto out;
	}

	local->cont.readdir.last_tried = call_child;

	local->fd                  = fd_ref (fd);
	local->cont.readdir.size   = size;
	local->cont.readdir.offset = offset;

	STACK_WIND_COOKIE (frame, afr_readdir_cbk, (void *) (long) call_child,
                           children[call_child], 
                           children[call_child]->fops->readdir,
                           fd, size, offset);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}
	return 0;
}


int32_t
afr_getdents_cbk (call_frame_t *frame, void *cookie,
		  xlator_t *this, int32_t op_ret, int32_t op_errno,
		  dir_entry_t *entry, int32_t count)
{
	afr_private_t * priv     = NULL;
	afr_local_t *   local    = NULL;
	xlator_t **     children = NULL;

	int unwind     = 1;
	int last_tried = -1;
	int this_try = -1;

	priv     = this->private;
	children = priv->children;

	local = frame->local;

	if (op_ret == -1) {
		last_tried = local->cont.getdents.last_tried;

		if (all_tried (last_tried, priv->child_count)) {
			goto out;
		}

		this_try = ++local->cont.getdents.last_tried;
		unwind = 0;

		STACK_WIND (frame, afr_getdents_cbk,
			    children[this_try],
			    children[this_try]->fops->getdents,
			    local->fd, local->cont.getdents.size,
			    local->cont.getdents.offset, local->cont.getdents.flag);
	}

out:
	if (unwind) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, entry, count);
	}

	return 0;
}


int32_t
afr_getdents (call_frame_t *frame, xlator_t *this,
	      fd_t *fd, size_t size, off_t offset, int32_t flag)
{
	afr_private_t * priv       = NULL;
	xlator_t **     children   = NULL;
	int             call_child = 0;
	afr_local_t     *local     = NULL;

	int32_t op_ret   = -1;
	int32_t op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv     = this->private;
	children = priv->children;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	call_child = afr_first_up_child (priv);
	if (call_child == -1) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_DEBUG,
			"no child is up.");
		goto out;
	}

	local->cont.getdents.last_tried = call_child;

	local->fd                   = fd_ref (fd);

	local->cont.getdents.size   = size;
	local->cont.getdents.offset = offset;
	local->cont.getdents.flag   = flag;
	
	frame->local = local;

	STACK_WIND (frame, afr_getdents_cbk,
		    children[call_child], children[call_child]->fops->getdents,
		    fd, size, offset, flag);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}

	return 0;
}


