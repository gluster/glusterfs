/*
   Copyright (c) 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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
	int           unwind = 0;

	LOCK (&frame->lock);
	{
		local = frame->local;
		local->call_count--;

		if (!child_went_down (op_ret, op_errno)) {
			/* we got a result, store it */

			local->cont.opendir.success_count++;
			local->cont.opendir.op_ret = op_ret;
			local->cont.opendir.op_ret = op_errno;
			local->fd = fd;
		}

		if (local->call_count == 0) {
			if (local->cont.opendir.success_count == 0) {
				/* no child is up */
			
				gf_log (this->name, GF_LOG_ERROR,
					"no child is up :(");

				local->cont.opendir.op_ret   = -1;
				local->cont.opendir.op_errno = ENOTCONN;
			}
		
			unwind = 1;
		}
	}
	UNLOCK (&frame->lock);

/* out: */
	if (unwind)
		AFR_STACK_UNWIND (frame, 
			      local->cont.opendir.op_ret, 
			      local->cont.opendir.op_errno, 
			      local->fd);

	return 0;
}


int32_t 
afr_opendir (call_frame_t *frame, xlator_t *this,
	     loc_t *loc, fd_t *fd)
{
	afr_private_t * priv        = NULL;
	afr_local_t   * local       = NULL;

	unsigned char * child_up    = NULL;
	int             child_count = 0;
	int             i           = 0;

	int32_t         op_ret   = -1;
	int32_t         op_errno = 0;

	priv = this->private;

	child_count = priv->child_count;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	frame->local = local;
	local->fd    = fd;

	child_up = alloca (sizeof (unsigned char) * child_count);

	LOCK (&priv->lock);
	{
		memcpy (child_up, priv->child_up, 
			sizeof (unsigned char) * child_count);
	}
	UNLOCK (&priv->lock);

	local->call_count = up_children_count (priv->child_count,
					       child_up);
	
	if (local->call_count == 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"no child is up :(");
		op_errno = ENOTCONN;
		goto out;
	}

	for (i = 0; i < child_count; i++) {
		if (child_up[i]) {
			STACK_WIND (frame, afr_opendir_cbk, 
				    priv->children[i],
				    priv->children[i]->fops->opendir,
				    loc, fd);
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
		 gf_dirent_t *buf)
{
	afr_private_t * priv     = NULL;
	afr_local_t *   local    = NULL;
	xlator_t **     children = NULL;

	int unwind     = 1;
	int last_tried = -1;

	priv     = this->private;
	children = priv->children;

	local = frame->local;

	if (child_went_down (op_ret, op_errno)) {
		last_tried = local->cont.readdir.last_tried;

		if (all_tried (last_tried, priv->child_count)) {
			op_ret   = -1;
			op_errno = ENOTCONN;
			goto out;
		}

		local->cont.readdir.last_tried++;
		unwind = 0;

		STACK_WIND (frame, afr_readdir_cbk,
			    children[last_tried], 
			    children[last_tried]->fops->readdir,
			    local->fd, local->cont.readdir.size,
			    local->cont.readdir.offset);
	}

out:
	if (unwind) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, buf);
	}

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

	int32_t op_ret   = -1;
	int32_t op_errno = 0;

	priv     = this->private;
	children = priv->children;

	ALLOC_OR_GOTO (local, afr_local_t, out);
						
	frame->local = local;

	call_child = first_up_child (priv);
	if (call_child == -1) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no child is up :(");
		goto out;
	}

	local->cont.readdir.last_tried = call_child;

	local->fd                  = fd;
	local->cont.readdir.size   = size;
	local->cont.readdir.offset = offset;

	STACK_WIND (frame, afr_readdir_cbk,
		    children[call_child], children[call_child]->fops->readdir,
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

	priv     = this->private;
	children = priv->children;

	local = frame->local;

	if (child_went_down (op_ret, op_errno)) {
		last_tried = local->cont.getdents.last_tried;

		if (all_tried (last_tried, priv->child_count)) {
			op_ret   = -1;
			op_errno = ENOTCONN;
			goto out;
		}

		local->cont.getdents.last_tried++;
		unwind = 0;

		STACK_WIND (frame, afr_getdents_cbk,
			    children[last_tried], 
			    children[last_tried]->fops->getdents,
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

	priv     = this->private;
	children = priv->children;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	call_child = first_up_child (priv);
	if (call_child == -1) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no child is up :(");
		goto out;
	}

	local->cont.getdents.last_tried = call_child;

	local->fd                   = fd;

	local->cont.getdents.size   = size;
	local->cont.getdents.offset = offset;
	local->cont.getdents.flag   = flag;

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


int32_t 
afr_checksum_cbk (call_frame_t *frame, void *cookie,
		  xlator_t *this, int32_t op_ret, int32_t op_errno,
		  uint8_t *fchecksum, uint8_t *dchecksum)
{
	AFR_STACK_UNWIND (frame, op_ret, op_errno, fchecksum, dchecksum);
	return 0;
}


int32_t
afr_checksum (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, int32_t flags)
{
	afr_private_t *priv = NULL;

	priv = this->private;

	STACK_WIND (frame, afr_checksum_cbk,
		    priv->children[0],
		    priv->children[0]->fops->checksum,
		    loc, flags);
	return 0;
}


