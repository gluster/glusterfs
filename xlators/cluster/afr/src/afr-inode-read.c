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

#include "afr.h"


/**
 * Common algorithm for inode read calls:
 * 
 * - Try the fop on the first child that is up
 * - if we have failed due to ENOTCONN:
 *     try the next child
 *
 * Applicable to: access, stat, fstat, readlink, getxattr
 */

/* {{{ access */

int32_t
afr_access_cbk (call_frame_t *frame, void *cookie,
		xlator_t *this, int32_t op_ret, int32_t op_errno)
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
		last_tried = local->cont.access.last_tried;

		if (all_tried (last_tried, priv->child_count)) {
			goto out;
		}
		this_try    = ++local->cont.access.last_tried;

		unwind = 0;

		STACK_WIND_COOKIE (frame, afr_access_cbk,
				   (void *) (long) this_try,
				   children[this_try], 
				   children[this_try]->fops->access,
				   &local->loc, local->cont.access.mask);
	}

out:
	if (unwind) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno);
	}

	return 0;
}


int32_t
afr_access (call_frame_t *frame, xlator_t *this,
	    loc_t *loc, int32_t mask)
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
	VALIDATE_OR_GOTO (priv->children, out);

	children = priv->children;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	call_child = first_up_child (priv);
	if (call_child == -1) {
		op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR,
			"no child is up :(");
		goto out;
	}

	local->cont.access.last_tried = call_child;
	loc_copy (&local->loc, loc);
	local->cont.access.mask       = mask;

	STACK_WIND_COOKIE (frame, afr_access_cbk,
			   (void *) (long) call_child,
			   children[call_child], children[call_child]->fops->access,
			   loc, mask);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno);
	}
	return 0;
}


/* }}} */

/* {{{ stat */

int32_t
afr_stat_cbk (call_frame_t *frame, void *cookie,
	      xlator_t *this, int32_t op_ret, int32_t op_errno,
	      struct stat *buf)
{
	afr_private_t * priv     = NULL;
	afr_local_t *   local    = NULL;
	xlator_t **     children = NULL;

	int deitransform_child = -1;

	int unwind     = 1;
	int last_tried = -1;
	int this_try = -1;

	priv     = this->private;
	children = priv->children;

	deitransform_child = (long) cookie;

	local = frame->local;

	if (op_ret == -1) {
	retry:
		last_tried = local->cont.stat.last_tried;

		if (all_tried (last_tried, priv->child_count)) {
			goto out;
		}
		this_try = ++local->cont.stat.last_tried;

		if (this_try == deitransform_child) {
			goto retry;
		}

		unwind = 0;

		STACK_WIND_COOKIE (frame, afr_stat_cbk,
				   (void *) (long) deitransform_child,
				   children[this_try], 
				   children[this_try]->fops->stat,
				   &local->loc);
	}

out:
	if (unwind) {
		buf->st_ino = local->cont.stat.ino;

		AFR_STACK_UNWIND (frame, op_ret, op_errno, buf);
	}

	return 0;
}


int32_t
afr_stat (call_frame_t *frame, xlator_t *this,
	  loc_t *loc)
{
	afr_private_t * priv       = NULL;
	afr_local_t   * local      = NULL;
	xlator_t **     children   = NULL;

	int             call_child = 0;

	int32_t         op_ret     = -1;
	int32_t         op_errno   = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv     = this->private;
	VALIDATE_OR_GOTO (priv->children, out);

	children = priv->children;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	frame->local = local;

	call_child = afr_deitransform (loc->inode->ino, priv->child_count);
	loc_copy (&local->loc, loc);

	/* 
	   if stat fails from the deitranform'd child, we try
	   all children starting with the first one
	*/
	local->cont.stat.last_tried = -1;
	local->cont.stat.ino = loc->inode->ino;

	STACK_WIND_COOKIE (frame, afr_stat_cbk, (void *) (long) call_child,
			   children[call_child],
			   children[call_child]->fops->stat,
			   loc);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}

	return 0;
}


/* }}} */

/* {{{ fstat */

int32_t
afr_fstat_cbk (call_frame_t *frame, void *cookie,
	       xlator_t *this, int32_t op_ret, int32_t op_errno,
	       struct stat *buf)
{
	afr_private_t * priv     = NULL;
	afr_local_t *   local    = NULL;
	xlator_t **     children = NULL;

	int deitransform_child = -1;

	int unwind     = 1;
	int last_tried = -1;
	int this_try = -1;

	priv     = this->private;
	children = priv->children;

	deitransform_child = (long) cookie;

	local = frame->local;

	if (op_ret == -1) {
	retry:
		last_tried = local->cont.fstat.last_tried;

		if (all_tried (last_tried, priv->child_count)) {
			goto out;
		}
		this_try   = ++local->cont.fstat.last_tried;

		if (this_try == deitransform_child) {
			/* 
			   skip the deitransform'd child since if we are here
			   we must have already tried that child
			*/
			goto retry;
		}
	       

		unwind = 0;

		STACK_WIND_COOKIE (frame, afr_fstat_cbk,
				   (void *) (long) deitransform_child,
				   children[this_try], 
				   children[this_try]->fops->fstat,
				   local->fd);
	}

out:
	if (unwind) {
		buf->st_ino = local->cont.fstat.ino;

		AFR_STACK_UNWIND (frame, op_ret, op_errno, buf);
	}

	return 0;
}


int32_t
afr_fstat (call_frame_t *frame, xlator_t *this,
	   fd_t *fd)
{
	afr_private_t * priv       = NULL;
	afr_local_t   * local      = NULL;
	xlator_t **     children   = NULL;

	int             call_child = 0;

	int32_t         op_ret     = -1;
	int32_t         op_errno   = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (fd, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv     = this->private;
	VALIDATE_OR_GOTO (priv->children, out);

	children = priv->children;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	frame->local = local;

	VALIDATE_OR_GOTO (fd->inode, out);

	call_child = afr_deitransform (fd->inode->ino, priv->child_count);

	/* 
	   if fstat fails from the deitranform'd child, we try
	   all children starting with the first one
	*/
	local->cont.fstat.last_tried = -1;
	local->cont.fstat.ino = fd->inode->ino;
	local->fd = fd_ref (fd);

	STACK_WIND_COOKIE (frame, afr_fstat_cbk, (void *) (long) call_child,
			   children[call_child],
			   children[call_child]->fops->fstat,
			   fd);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}

	return 0;
}

/* }}} */

/* {{{ readlink */

int32_t
afr_readlink_cbk (call_frame_t *frame, void *cookie,
		  xlator_t *this, int32_t op_ret, int32_t op_errno,
		  const char *buf)
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
		last_tried = local->cont.readlink.last_tried;

		if (all_tried (last_tried, priv->child_count)) {
			goto out;
		}
		this_try = ++local->cont.readlink.last_tried;

		unwind = 0;
		STACK_WIND_COOKIE (frame, afr_readlink_cbk,
				   (void *) (long) this_try,
				   children[this_try], 
				   children[this_try]->fops->readlink,
				   &local->loc,
				   local->cont.readlink.size);
	}

out:
	if (unwind) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, buf);
	}

	return 0;
}


int32_t
afr_readlink (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, size_t size)
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
	VALIDATE_OR_GOTO (priv->children, out);

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

	local->cont.readlink.last_tried = call_child;
	loc_copy (&local->loc, loc);
	local->cont.readlink.size       = size;

	STACK_WIND_COOKIE (frame, afr_readlink_cbk,
			   (void *) (long) call_child,
			   children[call_child], children[call_child]->fops->readlink,
			   loc, size);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}
	return 0;
}


/* }}} */

/* {{{ getxattr */

int32_t
afr_getxattr_cbk (call_frame_t *frame, void *cookie,
		  xlator_t *this, int32_t op_ret, int32_t op_errno,
		  dict_t *dict)
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
		last_tried = local->cont.getxattr.last_tried;

		if (all_tried (last_tried, priv->child_count)) {
			goto out;
		}
		this_try = ++local->cont.getxattr.last_tried;

		unwind = 0;
		STACK_WIND_COOKIE (frame, afr_getxattr_cbk,
				   (void *) (long) this_try,
				   children[this_try], 
				   children[this_try]->fops->getxattr,
				   &local->loc,
				   local->cont.getxattr.name);
	}

out:
	if (unwind) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, dict);
	}

	return 0;
}


int32_t
afr_getxattr (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, const char *name)
{
	afr_private_t *   priv       = NULL;
	xlator_t **       children   = NULL;
	int               call_child = 0;
	afr_local_t     * local      = NULL;

	int32_t op_ret   = -1;
	int32_t op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv     = this->private;
	VALIDATE_OR_GOTO (priv->children, out);

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

	local->cont.getxattr.last_tried = call_child;
	loc_copy (&local->loc, loc);
	if (name)
	  local->cont.getxattr.name       = strdup (name);

	STACK_WIND_COOKIE (frame, afr_getxattr_cbk,
			   (void *) (long) call_child,
			   children[call_child], children[call_child]->fops->getxattr,
			   loc, name);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL);
	}
	return 0;
}


/* }}} */

/* {{{ readv */

/**
 * read algorithm:
 * 
 * if the user has specified a read subvolume, use it
 * otherwise -
 *   use the inode number to hash it to one of the subvolumes, and
 *   read from there (to balance read load)
 *
 * if any of the above read's fail, try the children in sequence
 * beginning at the beginning
 */
 
int32_t
afr_readv_cbk (call_frame_t *frame, void *cookie,
	       xlator_t *this, int32_t op_ret, int32_t op_errno,
	       struct iovec *vector, int32_t count, struct stat *buf)
{
	afr_private_t * priv     = NULL;
	afr_local_t *   local    = NULL;
	xlator_t **     children = NULL;

	int unwind     = 1;
	int last_tried = -1;
	int this_try = -1;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv     = this->private;
	VALIDATE_OR_GOTO (priv->children, out);

	children = priv->children;

	local = frame->local;

	if (op_ret == -1) {
	retry:
		last_tried = local->cont.readv.last_tried;

		if (all_tried (last_tried, priv->child_count)) {
			goto out;
		}
		this_try = ++local->cont.readv.last_tried;

		if (this_try == priv->read_child) {
			/* 
			   skip the read child since if we are here
			   we must have already tried that child
			*/
			goto retry;
		}

		unwind = 0;

		STACK_WIND_COOKIE (frame, afr_readv_cbk,
				   (void *) (long) this_try,
				   children[this_try], 
				   children[this_try]->fops->readv,
				   local->fd, local->cont.readv.size,
				   local->cont.readv.offset);
	}

out:
	if (unwind) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, vector, count, buf);
	}

	return 0;
}


int32_t
afr_readv (call_frame_t *frame, xlator_t *this,
	   fd_t *fd, size_t size, off_t offset)
{
	afr_private_t * priv       = NULL;
	afr_local_t   * local      = NULL;
	xlator_t **     children   = NULL;

	int             call_child = 0;

	int32_t         op_ret     = -1;
	int32_t         op_errno   = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);
	VALIDATE_OR_GOTO (fd, out);

	priv     = this->private;
	children = priv->children;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	frame->local = local;

	if (priv->read_child != -1) {
		call_child = priv->read_child;

		/* 
		   if read fails from the read child, we try
		   all children starting with the first one
		*/
		local->cont.readv.last_tried = -1;
	} else {
		call_child = first_up_child (priv);
		if (call_child == -1) {
			op_errno = ENOTCONN;
			gf_log (this->name, GF_LOG_ERROR,
				"no child is up :(");
			goto out;
		}

		local->cont.readv.last_tried = call_child;
	}

	local->fd                    = fd_ref (fd);

	local->cont.readv.size       = size;
	local->cont.readv.offset     = offset;

	STACK_WIND_COOKIE (frame, afr_readv_cbk,
			   (void *) (long) call_child,
			   children[call_child],
			   children[call_child]->fops->readv,
			   fd, size, offset);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (frame, op_ret, op_errno, NULL, 0, NULL);
	}
	return 0;
}

/* }}} */
