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


uint64_t
afr_is_split_brain (xlator_t *this, inode_t *inode)
{
        int ret = 0;

        uint64_t ctx         = 0;
        uint64_t split_brain = 0;

        VALIDATE_OR_GOTO (inode, out);

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &ctx);

                if (ret < 0)
                        goto unlock;
                
                split_brain = ctx & 0xFFFFFFFF00000000ULL;
        }
unlock:
        UNLOCK (&inode->lock);

out:
        return split_brain;
}


void
afr_set_split_brain (xlator_t *this, inode_t *inode, int32_t split_brain)
{
        uint64_t ctx = 0;
        int      ret = 0;

        VALIDATE_OR_GOTO (inode, out);

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &ctx);

                if (ret < 0) {
                        ctx = 0;
                }

                ctx = (0x00000000FFFFFFFFULL & ctx)
                        | (split_brain & 0xFFFFFFFF00000000ULL);

                __inode_ctx_put (inode, this, ctx);
        }
        UNLOCK (&inode->lock);
out:
        return;
}


uint64_t
afr_read_child (xlator_t *this, inode_t *inode)
{
        int ret = 0;

        uint64_t ctx         = 0;
        uint64_t read_child  = 0;

        VALIDATE_OR_GOTO (inode, out);

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &ctx);

                if (ret < 0)
                        goto unlock;
                
                read_child = ctx & 0x00000000FFFFFFFFULL;
        }
unlock:
        UNLOCK (&inode->lock);

out:
        return read_child;
}


void
afr_set_read_child (xlator_t *this, inode_t *inode, int32_t read_child)
{
        uint64_t ctx = 0;
        int      ret = 0;

        VALIDATE_OR_GOTO (inode, out);

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &ctx);

                if (ret < 0) {
                        ctx = 0;
                }

                ctx = (0xFFFFFFFF00000000ULL & ctx) 
                        | (0x00000000FFFFFFFFULL & read_child);

                __inode_ctx_put (inode, this, ctx);
        }
        UNLOCK (&inode->lock);
        
out:
        return;
}


/**
 * afr_local_cleanup - cleanup everything in frame->local
 */

void
afr_local_sh_cleanup (afr_local_t *local, xlator_t *this)
{
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              i = 0;


	sh = &local->self_heal;
	priv = this->private;

	if (sh->buf)
		FREE (sh->buf);

	if (sh->xattr) {
		for (i = 0; i < priv->child_count; i++) {
			if (sh->xattr[i]) {
				dict_unref (sh->xattr[i]);
				sh->xattr[i] = NULL;
			}
		}
		FREE (sh->xattr);
	}

	if (sh->child_errno)
		FREE (sh->child_errno);

	if (sh->pending_matrix) {
		for (i = 0; i < priv->child_count; i++) {
			FREE (sh->pending_matrix[i]);
		}
		FREE (sh->pending_matrix);
	}

	if (sh->delta_matrix) {
		for (i = 0; i < priv->child_count; i++) {
			FREE (sh->delta_matrix[i]);
		}
		FREE (sh->delta_matrix);
	}

	if (sh->sources)
		FREE (sh->sources);

	if (sh->success)
		FREE (sh->success);

	if (sh->locked_nodes)
		FREE (sh->locked_nodes);

	if (sh->healing_fd) {
		fd_unref (sh->healing_fd);
		sh->healing_fd = NULL;
	}

	loc_wipe (&sh->parent_loc);
}


void 
afr_local_cleanup (afr_local_t *local, xlator_t *this)
{
        int i;
        afr_private_t * priv = NULL;

	if (!local)
		return;

	afr_local_sh_cleanup (local, this);

	FREE (local->child_errno);

        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (local->pending && local->pending[i])
                        FREE (local->pending[i]);
        }

        FREE (local->pending);

	loc_wipe (&local->loc);
	loc_wipe (&local->newloc);

	FREE (local->transaction.locked_nodes);
	FREE (local->transaction.child_errno);

	FREE (local->transaction.basename);
	FREE (local->transaction.new_basename);

	loc_wipe (&local->transaction.parent_loc);	
	loc_wipe (&local->transaction.new_parent_loc);

	if (local->fd)
		fd_unref (local->fd);
	
	if (local->xattr_req)
		dict_unref (local->xattr_req);

	FREE (local->child_up);

	{ /* lookup */
		if (local->cont.lookup.xattr)
			dict_unref (local->cont.lookup.xattr);
	}

	{ /* getxattr */
		if (local->cont.getxattr.name)
			FREE (local->cont.getxattr.name);
	}

	{ /* lk */
		if (local->cont.lk.locked_nodes)
			FREE (local->cont.lk.locked_nodes);
	}

	{ /* checksum */
		if (local->cont.checksum.file_checksum)
			FREE (local->cont.checksum.file_checksum);
		if (local->cont.checksum.dir_checksum)
			FREE (local->cont.checksum.dir_checksum);
	}

	{ /* create */
		if (local->cont.create.fd)
			fd_unref (local->cont.create.fd);
	}

	{ /* writev */
		FREE (local->cont.writev.vector);
	}

	{ /* setxattr */
		if (local->cont.setxattr.dict)
			dict_unref (local->cont.setxattr.dict);
	}

	{ /* removexattr */
		FREE (local->cont.removexattr.name);
	}

	{ /* symlink */
		FREE (local->cont.symlink.linkpath);
	}
}


int
afr_frame_return (call_frame_t *frame)
{
	afr_local_t *local = NULL;
	int          call_count = 0;

	local = frame->local;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	return call_count;
}


/**
 * up_children_count - return the number of children that are up
 */

int
afr_up_children_count (int child_count, unsigned char *child_up)
{
	int i   = 0;
	int ret = 0;

	for (i = 0; i < child_count; i++)
		if (child_up[i])
			ret++;
	return ret;
}


int
afr_locked_nodes_count (unsigned char *locked_nodes, int child_count)
{
	int ret = 0;
	int i;

	for (i = 0; i < child_count; i++)
		if (locked_nodes[i])
			ret++;

	return ret;
}


ino64_t
afr_itransform (ino64_t ino, int child_count, int child_index)
{
	ino64_t scaled_ino = -1;

	if (ino == ((uint64_t) -1)) {
		scaled_ino = ((uint64_t) -1);
		goto out;
	}

	scaled_ino = (ino * child_count) + child_index;

out:
	return scaled_ino;
}


int
afr_deitransform_orig (ino64_t ino, int child_count)
{
	int index = -1;

	index = ino % child_count;

	return index;
}


int
afr_deitransform (ino64_t ino, int child_count)
{
	return 0;
}


int
afr_self_heal_cbk (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;

	local = frame->local;

	if (local->govinda_gOvinda) {
                afr_set_split_brain (this, local->cont.lookup.inode, 1);
	} else {
                afr_set_split_brain (this, local->cont.lookup.inode, 0);
	}

	AFR_STACK_UNWIND (lookup, frame, local->op_ret, local->op_errno,
			  local->cont.lookup.inode,
			  &local->cont.lookup.buf,
			  local->cont.lookup.xattr,
                          NULL);

	return 0;
}


int
afr_lookup_cbk (call_frame_t *frame, void *cookie,
		xlator_t *this,	int32_t op_ret,	int32_t op_errno,
                inode_t *inode,	struct stat *buf, dict_t *xattr,
                struct stat *postparent)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
	struct stat *   lookup_buf = NULL;
        
	int             call_count      = -1;
	int             child_index     = -1;
        int             first_up_child  = -1;

	uint32_t        open_fd_count = 0;
	int             ret = 0;

	child_index = (long) cookie;
	priv = this->private;

	LOCK (&frame->lock);
	{
		local = frame->local;

		lookup_buf = &local->cont.lookup.buf;

		if (op_ret == -1) {
			if (op_errno == ENOENT)
				local->enoent_count++;
			
			if (op_errno != ENOTCONN) {
                                if (local->op_errno != ESTALE)
                                        local->op_errno = op_errno;
                        }

                        if (op_errno == ESTALE) {
                                /* no matter what other subvolumes return for
                                 * this call, ESTALE _must_ be sent to parent
                                 */
                                local->op_ret = -1;
                                local->op_errno = ESTALE;
                        }
			goto unlock;
		}

		if (afr_sh_has_metadata_pending (xattr, child_index, this))
			local->need_metadata_self_heal = 1;

		if (afr_sh_has_entry_pending (xattr, child_index, this))
			local->need_entry_self_heal = 1;

		if (afr_sh_has_data_pending (xattr, child_index, this))
			local->need_data_self_heal = 1;

		ret = dict_get_uint32 (xattr, GLUSTERFS_OPEN_FD_COUNT,
				       &open_fd_count);
		local->open_fd_count += open_fd_count;


                first_up_child = afr_first_up_child (priv);

                if (child_index == first_up_child) {
                        local->cont.lookup.ino = 
                                afr_itransform (buf->st_ino,
                                                priv->child_count,
                                                first_up_child);
                }

		/* in case of revalidate, we need to send stat of the
		 * child whose stat was sent during the first lookup.
		 * (so that time stamp does not vary with revalidate.
		 * in case it is down, stat of the fist success will
		 * be replied */

		/* inode number should be preserved across revalidates */

		if (local->success_count == 0) {
                        if (local->op_errno != ESTALE)
                                local->op_ret   = op_ret;
				
			local->cont.lookup.inode = inode;
			local->cont.lookup.xattr = dict_ref (xattr);

			*lookup_buf = *buf;
                        
                        lookup_buf->st_ino = afr_itransform (buf->st_ino,
                                                             priv->child_count,
                                                             child_index);

                        if (priv->read_child >= 0) {
                                afr_set_read_child (this,
                                                    local->cont.lookup.inode, 
                                                    priv->read_child);
                        } else {
                                afr_set_read_child (this,
                                                    local->cont.lookup.inode, 
                                                    child_index);
                        }

		} else {
			if (FILETYPE_DIFFERS (buf, lookup_buf)) {
				/* mismatching filetypes with same name
				   -- Govinda !! GOvinda !!!
				*/
				local->govinda_gOvinda = 1;
			}

			if (PERMISSION_DIFFERS (buf, lookup_buf)) {
				/* mismatching permissions */
				local->need_metadata_self_heal = 1;
			}

			if (OWNERSHIP_DIFFERS (buf, lookup_buf)) {
				/* mismatching permissions */
				local->need_metadata_self_heal = 1;
			}

			if (SIZE_DIFFERS (buf, lookup_buf)
			    && S_ISREG (buf->st_mode)) {
				local->need_data_self_heal = 1;
			}

                        if (child_index == local->read_child_index) {
                                
                                /* 
                                   lookup has succeeded on the read child.
                                   So use its inode number
                                */
                                if (local->op_errno != ESTALE)
                                        local->op_ret = op_ret;

                                if (local->cont.lookup.xattr)
                                        dict_unref (local->cont.lookup.xattr);
                                
                                local->cont.lookup.inode = inode;
                                local->cont.lookup.xattr = dict_ref (xattr);

                                *lookup_buf = *buf;

                                if (priv->read_child >= 0) {
                                        afr_set_read_child (this,
                                                            local->cont.lookup.inode,
                                                            priv->read_child);
                                } else {
                                        afr_set_read_child (this,
                                                            local->cont.lookup.inode,
                                                            local->read_child_index);
                                }
                        }

		}

		local->success_count++;
	}
unlock:
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
                if (local->cont.lookup.ino) {
                        local->cont.lookup.buf.st_ino = local->cont.lookup.ino;
                }

		if (local->op_ret == 0) {
			/* KLUDGE: assuming DHT will not itransform in 
			   revalidate */
			if (local->cont.lookup.inode->ino)
				local->cont.lookup.buf.st_ino = 
					local->cont.lookup.inode->ino;
		}

		if (local->success_count && local->enoent_count) {
			local->need_metadata_self_heal = 1;
			local->need_data_self_heal = 1;
			local->need_entry_self_heal = 1;
		}

		if (local->success_count) {
			/* check for split-brain case in previous lookup */
			if (afr_is_split_brain (this,
                                                local->cont.lookup.inode))
				local->need_data_self_heal = 1;
		}

		if ((local->need_metadata_self_heal
		     || local->need_data_self_heal
		     || local->need_entry_self_heal)
		    && (!local->open_fd_count)) {

			if (!local->cont.lookup.inode->st_mode) {
				/* fix for RT #602 */
				local->cont.lookup.inode->st_mode =
					lookup_buf->st_mode;
			}

			afr_self_heal (frame, this, afr_self_heal_cbk);
		} else {
			AFR_STACK_UNWIND (lookup, frame, local->op_ret,
					  local->op_errno,
					  local->cont.lookup.inode, 
					  &local->cont.lookup.buf,
					  local->cont.lookup.xattr,
                                          NULL);
		}
	}

	return 0;
}


int
afr_lookup (call_frame_t *frame, xlator_t *this,
	    loc_t *loc, dict_t *xattr_req)
{
	afr_private_t *priv = NULL;
	afr_local_t   *local = NULL;
	int            ret = -1;
	int            i = 0;

        uint64_t       ctx;

	int32_t        op_errno = 0;

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	local->op_ret = -1;

	frame->local = local;

	loc_copy (&local->loc, loc);

        ret = inode_ctx_get (loc->inode, this, &ctx);
        if (ret == 0) {
                /* lookup is a revalidate */

                local->read_child_index = afr_read_child (this, loc->inode);
        } else {
                LOCK (&priv->read_child_lock);
                {
                        local->read_child_index = (++priv->read_child_rr)
                                % (priv->child_count);
                }
                UNLOCK (&priv->read_child_lock);
        }

	local->call_count = priv->child_count;

	local->child_up = memdup (priv->child_up, priv->child_count);
	local->child_count = afr_up_children_count (priv->child_count,
						    local->child_up);

	/* By default assume ENOTCONN. On success it will be set to 0. */
	local->op_errno = ENOTCONN;
	
	if (xattr_req == NULL)
		local->xattr_req = dict_new ();
	else
		local->xattr_req = dict_ref (xattr_req);

        for (i = 0; i < priv->child_count; i++) {
		ret = dict_set_uint64 (local->xattr_req, priv->pending_key[i],
				       3 * sizeof(int32_t));
                
                /* 3 = data+metadata+entry */
        }

	ret = dict_set_uint64 (local->xattr_req, GLUSTERFS_OPEN_FD_COUNT, 0);

	for (i = 0; i < priv->child_count; i++) {
		STACK_WIND_COOKIE (frame, afr_lookup_cbk, (void *) (long) i,
				   priv->children[i],
				   priv->children[i]->fops->lookup,
				   loc, local->xattr_req);
	}

	ret = 0;
out:
	if (ret == -1)
		AFR_STACK_UNWIND (lookup, frame, -1, op_errno,
                                  NULL, NULL, NULL, NULL);

	return 0;
}


/* {{{ open */

int
afr_fd_ctx_set (xlator_t *this, fd_t *fd)
{
        afr_private_t * priv = NULL;

        int op_ret = 0;
        int ret    = 0;

        uint64_t       ctx;
        afr_fd_ctx_t * fd_ctx = NULL;

        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;

        LOCK (&fd->lock);
        {
                ret = __fd_ctx_get (fd, this, &ctx);
                
                if (ret == 0)
                        goto unlock;

                fd_ctx = CALLOC (1, sizeof (afr_fd_ctx_t));
                if (!fd_ctx) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Out of memory");
                        
                        op_ret = -ENOMEM;
                        goto unlock;
                }

                fd_ctx->child_failed = CALLOC (sizeof (*fd_ctx->child_failed),
                                               priv->child_count);
                
                if (!fd_ctx->child_failed) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Out of memory");

                        op_ret = -ENOMEM;
                        goto unlock;
                }

                ret = __fd_ctx_set (fd, this, (uint64_t)(long) fd_ctx);
                if (ret < 0) {
                        op_ret = ret;
                }
        }
unlock:
        UNLOCK (&fd->lock);
out:
        return ret;
}


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

/* }}} */

/* {{{ flush */

int
afr_flush_unwind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
	call_frame_t   *main_frame = NULL;

	local = frame->local;
	priv  = this->private;

	LOCK (&frame->lock);
	{
		if (local->transaction.main_frame)
			main_frame = local->transaction.main_frame;
		local->transaction.main_frame = NULL;
	}
	UNLOCK (&frame->lock);

	if (main_frame) {
		AFR_STACK_UNWIND (flush, main_frame,
                                  local->op_ret, local->op_errno);
	}
        
	return 0;
}


int
afr_flush_wind_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		      int32_t op_ret, int32_t op_errno)
{
        afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;

	int call_count  = -1;
	int child_index = (long) cookie;
	int need_unwind = 0;

	local = frame->local;
	priv  = this->private;

	LOCK (&frame->lock);
	{
		if (afr_fop_failed (op_ret, op_errno))
			afr_transaction_fop_failed (frame, this, child_index);

		if (op_ret != -1) {
			if (local->success_count == 0) {
				local->op_ret = op_ret;
			}
			local->success_count++;

			if (local->success_count == priv->wait_count) {
				need_unwind = 1;
			}
		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (need_unwind)
		afr_flush_unwind (frame, this);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
	}
	
	return 0;
}


int
afr_flush_wind (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	
	int i = 0;
	int call_count = -1;

	local = frame->local;
	priv = this->private;

	call_count = afr_up_children_count (priv->child_count, local->child_up);

	if (call_count == 0) {
		local->transaction.resume (frame, this);
		return 0;
	}

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {				
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame, afr_flush_wind_cbk, 
					   (void *) (long) i,	
					   priv->children[i], 
					   priv->children[i]->fops->flush,
					   local->fd);
		
			if (!--call_count)
				break;
		}
	}
	
	return 0;
}


int
afr_flush_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;

	local = frame->local;

	local->transaction.unwind (frame, this);

        AFR_STACK_DESTROY (frame);

	return 0;
}


int
afr_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
	afr_private_t * priv  = NULL;
	afr_local_t   * local = NULL;

        call_frame_t  * transaction_frame = NULL;

	int ret        = -1;

	int op_ret   = -1;
	int op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

        transaction_frame = copy_frame (frame);
        if (!transaction_frame) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_ERROR,
			"Out of memory.");
		goto out;
        }

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	transaction_frame->local = local;

        local->op = GF_FOP_FLUSH;
        
        local->transaction.fop    = afr_flush_wind;
        local->transaction.done   = afr_flush_done;
        local->transaction.unwind = afr_flush_unwind;

        local->fd                 = fd_ref (fd);

        local->transaction.main_frame = frame;
        local->transaction.start  = LLONG_MAX - 1;
        local->transaction.len    = 0;

        afr_transaction (transaction_frame, this, AFR_FLUSH_TRANSACTION);

	op_ret = 0;
out:
	if (op_ret == -1) {
                if (transaction_frame)
                        AFR_STACK_DESTROY (transaction_frame);

		AFR_STACK_UNWIND (flush, frame, op_ret, op_errno);
	}

	return 0;
}

/* }}} */


int
afr_release (xlator_t *this, fd_t *fd)
{
        uint64_t        ctx;
        afr_fd_ctx_t *  fd_ctx;

        int ret = 0;

        ret = fd_ctx_get (fd, this, &ctx);

        if (ret < 0)
                goto out;

        fd_ctx = (afr_fd_ctx_t *)(long) ctx;

        if (fd_ctx) {
                if (fd_ctx->child_failed)
                        FREE (fd_ctx->child_failed);

                FREE (fd_ctx);
        }
        
out:
        return 0;
}


/* {{{ fsync */

int
afr_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct stat *prebuf,
               struct stat *postbuf)
{
	afr_local_t *local = NULL;
	
	int call_count = -1;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (op_ret == 0)
			local->op_ret = 0;

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		AFR_STACK_UNWIND (fsync, frame, local->op_ret, local->op_errno,
                                  NULL, NULL);

	return 0;
}


int
afr_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd,
	   int32_t datasync)
{
	afr_private_t *priv = NULL;
	afr_local_t *local = NULL;

	int ret = -1;

	int i = 0;
	int32_t call_count = 0;
	int32_t op_ret   = -1;
	int32_t op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	call_count = local->call_count;
	frame->local = local;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND (frame, afr_fsync_cbk,
				    priv->children[i],
				    priv->children[i]->fops->fsync,
				    fd, datasync);
			if (!--call_count)
				break;
		}
	}

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (fsync, frame, op_ret, op_errno, NULL, NULL);
	}
	return 0;
}

/* }}} */

/* {{{ fsync */

int32_t
afr_fsyncdir_cbk (call_frame_t *frame, void *cookie,
		  xlator_t *this, int32_t op_ret, int32_t op_errno)
{
	afr_local_t *local = NULL;
	
	int call_count = -1;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (op_ret == 0)
			local->op_ret = 0;

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		AFR_STACK_UNWIND (fsyncdir, frame, local->op_ret,
                                  local->op_errno);

	return 0;
}


int32_t
afr_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd,
	      int32_t datasync)
{
	afr_private_t *priv = NULL;
	afr_local_t *local = NULL;

	int ret = -1;

	int i = 0;
	int32_t call_count = 0;
	int32_t op_ret   = -1;
	int32_t op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	call_count = local->call_count;
	frame->local = local;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND (frame, afr_fsyncdir_cbk,
				    priv->children[i],
				    priv->children[i]->fops->fsyncdir,
				    fd, datasync);
			if (!--call_count)
				break;
		}
	}

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (fsyncdir, frame, op_ret, op_errno);
	}
	return 0;
}

/* }}} */

/* {{{ xattrop */

int32_t
afr_xattrop_cbk (call_frame_t *frame, void *cookie,
		 xlator_t *this, int32_t op_ret, int32_t op_errno,
		 dict_t *xattr)
{
	afr_local_t *local = NULL;
	
	int call_count = -1;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (op_ret == 0)
			local->op_ret = 0;

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		AFR_STACK_UNWIND (xattrop, frame, local->op_ret, local->op_errno,
                                  xattr);

	return 0;
}


int32_t
afr_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
	     gf_xattrop_flags_t optype, dict_t *xattr)
{
	afr_private_t *priv = NULL;
	afr_local_t *local  = NULL;

	int ret = -1;

	int i = 0;
	int32_t call_count = 0;
	int32_t op_ret   = -1;
	int32_t op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	call_count = local->call_count;
	frame->local = local;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND (frame, afr_xattrop_cbk,
				    priv->children[i],
				    priv->children[i]->fops->xattrop,
				    loc, optype, xattr);
			if (!--call_count)
				break;
		}
	}

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (xattrop, frame, op_ret, op_errno, NULL);
	}
	return 0;
}

/* }}} */

/* {{{ fxattrop */

int32_t
afr_fxattrop_cbk (call_frame_t *frame, void *cookie,
		  xlator_t *this, int32_t op_ret, int32_t op_errno,
		  dict_t *xattr)
{
	afr_local_t *local = NULL;
	
	int call_count = -1;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (op_ret == 0)
			local->op_ret = 0;

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		AFR_STACK_UNWIND (fxattrop, frame, local->op_ret, local->op_errno,
                                  xattr);

	return 0;
}


int32_t
afr_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
	      gf_xattrop_flags_t optype, dict_t *xattr)
{
	afr_private_t *priv = NULL;
	afr_local_t *local  = NULL;

	int ret = -1;

	int i = 0;
	int32_t call_count = 0;
	int32_t op_ret   = -1;
	int32_t op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	call_count = local->call_count;
	frame->local = local;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND (frame, afr_fxattrop_cbk,
				    priv->children[i],
				    priv->children[i]->fops->fxattrop,
				    fd, optype, xattr);
			if (!--call_count)
				break;
		}
	}

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (fxattrop, frame, op_ret, op_errno, NULL);
	}
	return 0;
}

/* }}} */


int32_t
afr_inodelk_cbk (call_frame_t *frame, void *cookie,
		 xlator_t *this, int32_t op_ret, int32_t op_errno)
		
{
	afr_local_t *local = NULL;
	
	int call_count = -1;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (op_ret == 0)
			local->op_ret = 0;

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		AFR_STACK_UNWIND (inodelk, frame, local->op_ret,
                                  local->op_errno);

	return 0;
}


int32_t
afr_inodelk (call_frame_t *frame, xlator_t *this, 
             const char *volume, loc_t *loc, int32_t cmd, struct flock *flock)
{
	afr_private_t *priv = NULL;
	afr_local_t *local  = NULL;

	int ret = -1;

	int i = 0;
	int32_t call_count = 0;
	int32_t op_ret   = -1;
	int32_t op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	call_count = local->call_count;
	frame->local = local;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND (frame, afr_inodelk_cbk,
				    priv->children[i],
				    priv->children[i]->fops->inodelk,
				    volume, loc, cmd, flock);

			if (!--call_count)
				break;
		}
	}

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (inodelk, frame, op_ret, op_errno);
	}
	return 0;
}


int32_t
afr_finodelk_cbk (call_frame_t *frame, void *cookie,
		  xlator_t *this, int32_t op_ret, int32_t op_errno)
		
{
	afr_local_t *local = NULL;
	
	int call_count = -1;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (op_ret == 0)
			local->op_ret = 0;

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		AFR_STACK_UNWIND (finodelk, frame, local->op_ret,
                                  local->op_errno);

	return 0;
}


int32_t
afr_finodelk (call_frame_t *frame, xlator_t *this, 
              const char *volume, fd_t *fd, int32_t cmd, struct flock *flock)
{
	afr_private_t *priv = NULL;
	afr_local_t *local  = NULL;

	int ret = -1;

	int i = 0;
	int32_t call_count = 0;
	int32_t op_ret   = -1;
	int32_t op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	call_count = local->call_count;
	frame->local = local;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND (frame, afr_finodelk_cbk,
				    priv->children[i],
				    priv->children[i]->fops->finodelk,
				    volume, fd, cmd, flock);

			if (!--call_count)
				break;
		}
	}

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (finodelk, frame, op_ret, op_errno);
	}
	return 0;
}


int32_t
afr_entrylk_cbk (call_frame_t *frame, void *cookie,
		 xlator_t *this, int32_t op_ret, int32_t op_errno)
		
{
	afr_local_t *local = NULL;
	
	int call_count = -1;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (op_ret == 0)
			local->op_ret = 0;

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		AFR_STACK_UNWIND (entrylk, frame, local->op_ret,
                                  local->op_errno);

	return 0;
}


int32_t
afr_entrylk (call_frame_t *frame, xlator_t *this, 
             const char *volume, loc_t *loc,
	     const char *basename, entrylk_cmd cmd, entrylk_type type)
{
	afr_private_t *priv = NULL;
	afr_local_t *local  = NULL;

	int ret = -1;

	int i = 0;
	int32_t call_count = 0;
	int32_t op_ret   = -1;
	int32_t op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	call_count = local->call_count;
	frame->local = local;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND (frame, afr_entrylk_cbk,
				    priv->children[i],
				    priv->children[i]->fops->entrylk,
				    volume, loc, basename, cmd, type);

			if (!--call_count)
				break;
		}
	}

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (entrylk, frame, op_ret, op_errno);
	}
	return 0;
}



int32_t
afr_fentrylk_cbk (call_frame_t *frame, void *cookie,
		 xlator_t *this, int32_t op_ret, int32_t op_errno)
		
{
	afr_local_t *local = NULL;
	
	int call_count = -1;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (op_ret == 0)
			local->op_ret = 0;

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		AFR_STACK_UNWIND (fentrylk, frame, local->op_ret,
                                  local->op_errno);

	return 0;
}


int32_t
afr_fentrylk (call_frame_t *frame, xlator_t *this, 
              const char *volume, fd_t *fd,
	      const char *basename, entrylk_cmd cmd, entrylk_type type)
{
	afr_private_t *priv = NULL;
	afr_local_t *local  = NULL;

	int ret = -1;

	int i = 0;
	int32_t call_count = 0;
	int32_t op_ret   = -1;
	int32_t op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	call_count = local->call_count;
	frame->local = local;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND (frame, afr_fentrylk_cbk,
				    priv->children[i],
				    priv->children[i]->fops->fentrylk,
				    volume, fd, basename, cmd, type);

			if (!--call_count)
				break;
		}
	}

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (fentrylk, frame, op_ret, op_errno);
	}
	return 0;
}


int32_t
afr_checksum_cbk (call_frame_t *frame, void *cookie,
		  xlator_t *this, int32_t op_ret, int32_t op_errno,
		  uint8_t *file_checksum, uint8_t *dir_checksum)
		
{
	afr_local_t *local = NULL;
	
	int call_count = -1;

	local = frame->local;

	LOCK (&frame->lock);
	{
		if (op_ret == 0 && (local->op_ret != 0)) {
			local->op_ret = 0;

			local->cont.checksum.file_checksum = MALLOC (NAME_MAX);
			memcpy (local->cont.checksum.file_checksum, file_checksum, 
				NAME_MAX);

			local->cont.checksum.dir_checksum = MALLOC (NAME_MAX);
			memcpy (local->cont.checksum.dir_checksum, dir_checksum, 
				NAME_MAX);

		}

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		AFR_STACK_UNWIND (checksum, frame, local->op_ret, local->op_errno,
				  local->cont.checksum.file_checksum, 
				  local->cont.checksum.dir_checksum);

	return 0;
}


int32_t
afr_checksum (call_frame_t *frame, xlator_t *this, loc_t *loc,
	      int32_t flag)
{
	afr_private_t *priv = NULL;
	afr_local_t *local  = NULL;

	int ret = -1;

	int i = 0;
	int32_t call_count = 0;
	int32_t op_ret   = -1;
	int32_t op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);

	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	call_count = local->call_count;
	frame->local = local;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND (frame, afr_checksum_cbk,
				    priv->children[i],
				    priv->children[i]->fops->checksum,
				    loc, flag);

			if (!--call_count)
				break;
		}
	}

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (checksum, frame, op_ret, op_errno,
                                  NULL, NULL);
	}
	return 0;
}


int32_t
afr_statfs_cbk (call_frame_t *frame, void *cookie,
		xlator_t *this, int32_t op_ret, int32_t op_errno,
		struct statvfs *statvfs)
{
	afr_local_t *local = NULL;

	int call_count = 0;

	LOCK (&frame->lock);
	{
		local = frame->local;

		if (op_ret == 0) {
			local->op_ret   = op_ret;
			
			if (local->cont.statfs.buf_set) {
				if (statvfs->f_bavail < local->cont.statfs.buf.f_bavail)
					local->cont.statfs.buf = *statvfs;
			} else {
				local->cont.statfs.buf = *statvfs;
				local->cont.statfs.buf_set = 1;
			}
		}

		if (op_ret == -1)
			local->op_errno = op_errno;

	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		AFR_STACK_UNWIND (statfs, frame, local->op_ret, local->op_errno, 
				  &local->cont.statfs.buf);

	return 0;
}


int32_t
afr_statfs (call_frame_t *frame, xlator_t *this,
	    loc_t *loc)
{
	afr_private_t *  priv        = NULL;
	int              child_count = 0;
	afr_local_t   *  local       = NULL;
	int              i           = 0;

	int ret = -1;
	int              call_count = 0;
	int32_t          op_ret      = -1;
	int32_t          op_errno    = 0;

	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);
	VALIDATE_OR_GOTO (loc, out);

	priv = this->private;
	child_count = priv->child_count;

	ALLOC_OR_GOTO (local, afr_local_t, out);
	
	ret = AFR_LOCAL_INIT (local, priv);
	if (ret < 0) {
		op_errno = -ret;
		goto out;
	}

	frame->local = local;
	call_count = local->call_count;

	for (i = 0; i < child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND (frame, afr_statfs_cbk,
				    priv->children[i],
				    priv->children[i]->fops->statfs, 
				    loc);
			if (!--call_count)
				break;
		}
	}
	
	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (statfs, frame, op_ret, op_errno, NULL);
	}
	return 0;
}


int32_t
afr_lk_unlock_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
		   int32_t op_ret, int32_t op_errno, struct flock *lock)
{
	afr_local_t * local = NULL;

	int call_count = -1;

	local = frame->local;
	call_count = afr_frame_return (frame);

	if (call_count == 0)
		AFR_STACK_UNWIND (lk, frame, local->op_ret, local->op_errno,
				  lock);

	return 0;
}


int32_t 
afr_lk_unlock (call_frame_t *frame, xlator_t *this)
{
	afr_local_t   * local = NULL;
	afr_private_t * priv  = NULL;

	int i;
	int call_count = 0;

	local = frame->local;
	priv  = this->private;

	call_count = afr_locked_nodes_count (local->cont.lk.locked_nodes, 
					     priv->child_count);

	if (call_count == 0) {
		AFR_STACK_UNWIND (lk, frame, local->op_ret, local->op_errno,
				  &local->cont.lk.flock);
		return 0;
	}

	local->call_count = call_count;

	local->cont.lk.flock.l_type = F_UNLCK;

	for (i = 0; i < priv->child_count; i++) {
		if (local->cont.lk.locked_nodes[i]) {
			STACK_WIND (frame, afr_lk_unlock_cbk,
				    priv->children[i],
				    priv->children[i]->fops->lk,
				    local->fd, F_SETLK, 
				    &local->cont.lk.flock);

			if (!--call_count)
				break;
		}
	}

	return 0;
}


int32_t
afr_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
	    int32_t op_ret, int32_t op_errno, struct flock *lock)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	int call_count  = -1;
	int child_index = -1;

	local = frame->local;
	priv  = this->private;

	child_index = (long) cookie;

	call_count = --local->call_count;

	if (!child_went_down (op_ret, op_errno) && (op_ret == -1)) {
		local->op_ret   = -1;
		local->op_errno = op_errno;

		afr_lk_unlock (frame, this);
		return 0;
	}

	if (op_ret == 0) {
		local->op_ret        = 0;
		local->op_errno      = 0;
		local->cont.lk.locked_nodes[child_index] = 1;
	}

	child_index++;

	if (child_index < priv->child_count) {
		STACK_WIND_COOKIE (frame, afr_lk_cbk, (void *) (long) child_index,
				   priv->children[child_index],
				   priv->children[child_index]->fops->lk,
				   local->fd, local->cont.lk.cmd, 
				   &local->cont.lk.flock);
	} else if (local->op_ret == -1) {
		/* all nodes have gone down */
		
		AFR_STACK_UNWIND (lk, frame, -1, ENOTCONN, &local->cont.lk.flock);
	} else {
		/* locking has succeeded on all nodes that are up */
		
		AFR_STACK_UNWIND (lk, frame, local->op_ret, local->op_errno,
			      &local->cont.lk.flock);
	}

	return 0;
}


int
afr_lk (call_frame_t *frame, xlator_t *this,
	fd_t *fd, int32_t cmd,
	struct flock *flock)
{
	afr_private_t *priv = NULL;
	afr_local_t *local = NULL;

	int i = 0;

	int32_t op_ret   = -1;
	int32_t op_errno = 0;

	VALIDATE_OR_GOTO (frame, out);
	VALIDATE_OR_GOTO (this, out);
	VALIDATE_OR_GOTO (this->private, out);

	priv = this->private;

	ALLOC_OR_GOTO (local, afr_local_t, out);
	AFR_LOCAL_INIT (local, priv);

	frame->local  = local;

	local->cont.lk.locked_nodes = CALLOC (priv->child_count, 
					      sizeof (*local->cont.lk.locked_nodes));
	
	if (!local->cont.lk.locked_nodes) {
		gf_log (this->name, GF_LOG_ERROR, "Out of memory");
		op_errno = ENOMEM;
		goto out;
	}

	local->fd            = fd_ref (fd);
	local->cont.lk.cmd   = cmd;
	local->cont.lk.flock = *flock;

	STACK_WIND_COOKIE (frame, afr_lk_cbk, (void *) (long) 0,
			   priv->children[i],
			   priv->children[i]->fops->lk,
			   fd, cmd, flock);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (lk, frame, op_ret, op_errno, NULL);
	}
	return 0;
}

int
afr_priv_dump (xlator_t *this)
{
	afr_private_t *priv = NULL;
        char  key_prefix[GF_DUMP_MAX_BUF_LEN];
        char  key[GF_DUMP_MAX_BUF_LEN];
        int   i = 0;


        assert(this);
	priv = this->private;

        assert(priv);
        snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s", this->type, this->name);
        gf_proc_dump_add_section(key_prefix);
        gf_proc_dump_build_key(key, key_prefix, "child_count");
        gf_proc_dump_write(key, "%u", priv->child_count);
        gf_proc_dump_build_key(key, key_prefix, "read_child_rr");
        gf_proc_dump_write(key, "%u", priv->read_child_rr);
	for (i = 0; i < priv->child_count; i++) {
                gf_proc_dump_build_key(key, key_prefix, "child_up[%d]", i);
                gf_proc_dump_write(key, "%d", priv->child_up[i]);
                gf_proc_dump_build_key(key, key_prefix, 
                                        "pending_key[%d]", i);
                gf_proc_dump_write(key, "%s", priv->pending_key[i]);
        }
        gf_proc_dump_build_key(key, key_prefix, "data_self_heal");
        gf_proc_dump_write(key, "%d", priv->data_self_heal);
        gf_proc_dump_build_key(key, key_prefix, "metadata_self_heal");
        gf_proc_dump_write(key, "%d", priv->metadata_self_heal);
        gf_proc_dump_build_key(key, key_prefix, "entry_self_heal");
        gf_proc_dump_write(key, "%d", priv->entry_self_heal);
        gf_proc_dump_build_key(key, key_prefix, "data_change_log");
        gf_proc_dump_write(key, "%d", priv->data_change_log);
        gf_proc_dump_build_key(key, key_prefix, "metadata_change_log");
        gf_proc_dump_write(key, "%d", priv->metadata_change_log);
        gf_proc_dump_build_key(key, key_prefix, "entry_change_log");
        gf_proc_dump_write(key, "%d", priv->entry_change_log);
        gf_proc_dump_build_key(key, key_prefix, "read_child");
        gf_proc_dump_write(key, "%d", priv->read_child);
        gf_proc_dump_build_key(key, key_prefix, "favorite_child");
        gf_proc_dump_write(key, "%u", priv->favorite_child);
        gf_proc_dump_build_key(key, key_prefix, "data_lock_server_count");
        gf_proc_dump_write(key, "%u", priv->data_lock_server_count);
        gf_proc_dump_build_key(key, key_prefix, "metadata_lock_server_count");
        gf_proc_dump_write(key, "%u", priv->metadata_lock_server_count);
        gf_proc_dump_build_key(key, key_prefix, "entry_lock_server_count");
        gf_proc_dump_write(key, "%u", priv->entry_lock_server_count);
        gf_proc_dump_build_key(key, key_prefix, "wait_count");
        gf_proc_dump_write(key, "%u", priv->wait_count);

        return 0;
}


/**
 * find_child_index - find the child's index in the array of subvolumes
 * @this: AFR
 * @child: child
 */

static int
find_child_index (xlator_t *this, xlator_t *child)
{
	afr_private_t *priv = NULL;

	int i = -1;

	priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
		if ((xlator_t *) child == priv->children[i])
			break;
	}

	return i;
}


int32_t
notify (xlator_t *this, int32_t event,
	void *data, ...)
{
	afr_private_t *     priv     = NULL;
	unsigned char *     child_up = NULL;

	int i           = -1;
	int up_children = 0;

	priv = this->private;

	if (!priv)
		return 0;

	child_up = priv->child_up;

	switch (event) {
	case GF_EVENT_CHILD_UP:
		i = find_child_index (this, data);

		child_up[i] = 1;

		/* 
		   if all the children were down, and one child came up, 
		   send notify to parent
		*/

		for (i = 0; i < priv->child_count; i++)
			if (child_up[i])
				up_children++;

		if (up_children == 1) {
                        gf_log (this->name, GF_LOG_NORMAL,
                                "Subvolume '%s' came back up; "
                                "going online.", ((xlator_t *)data)->name);
                        
			default_notify (this, event, data);
                }

		break;

	case GF_EVENT_CHILD_DOWN:
		i = find_child_index (this, data);

		child_up[i] = 0;
		
		/* 
		   if all children are down, and this was the last to go down,
		   send notify to parent
		*/

		for (i = 0; i < priv->child_count; i++)
			if (child_up[i])
				up_children++;

		if (up_children == 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "All subvolumes are down. Going offline "
                                "until atleast one of them comes back up.");

			default_notify (this, event, data);
                }

		break;

	default:
		default_notify (this, event, data);
	}

	return 0;
}


static const char *favorite_child_warning_str = "You have specified subvolume '%s' "
	"as the 'favorite child'. This means that if a discrepancy in the content "
	"or attributes (ownership, permission, etc.) of a file is detected among "
	"the subvolumes, the file on '%s' will be considered the definitive "
	"version and its contents will OVERWRITE the contents of the file on other "
	"subvolumes. All versions of the file except that on '%s' "
	"WILL BE LOST.";

static const char *no_lock_servers_warning_str = "You have set lock-server-count = 0. "
	"This means correctness is NO LONGER GUARANTEED in all cases. If two or more "
	"applications write to the same region of a file, there is a possibility that "
	"its copies will be INCONSISTENT. Set it to a value greater than 0 unless you "
	"are ABSOLUTELY SURE of what you are doing and WILL NOT HOLD GlusterFS "
	"RESPONSIBLE for inconsistent data. If you are in doubt, set it to a value "
	"greater than 0.";

int32_t 
init (xlator_t *this)
{
	afr_private_t * priv        = NULL;
	int             child_count = 0;
	xlator_list_t * trav        = NULL;
	int             i           = 0;
	int             ret         = -1;
	int             op_errno    = 0;

	char * read_subvol = NULL;
	char * fav_child   = NULL;
	char * self_heal   = NULL;
        char * algo        = NULL;
	char * change_log  = NULL;

        int32_t background_count  = 0;
	int32_t lock_server_count = 1;
        int32_t window_size;

	int    fav_ret       = -1;
	int    read_ret      = -1;
	int    dict_ret      = -1;

	if (!this->children) {
		gf_log (this->name, GF_LOG_ERROR,
			"replicate translator needs more than one "
                        "subvolume defined.");
		return -1;
	}
  
	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"Volume is dangling.");
	}

	ALLOC_OR_GOTO (this->private, afr_private_t, out);

	priv = this->private;

	read_ret = dict_get_str (this->options, "read-subvolume", &read_subvol);
	priv->read_child = -1;

	fav_ret = dict_get_str (this->options, "favorite-child", &fav_child);
	priv->favorite_child = -1;

        priv->background_self_heal_count = 16;

	dict_ret = dict_get_int32 (this->options, "background-self-heal-count",
				   &background_count);
	if (dict_ret == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"Setting background self-heal count to %d.",
			window_size);

		priv->background_self_heal_count = background_count;
	}

	/* Default values */

	priv->data_self_heal     = 1;
	priv->metadata_self_heal = 1;
	priv->entry_self_heal    = 1;

	dict_ret = dict_get_str (this->options, "data-self-heal", &self_heal);
	if (dict_ret == 0) {
		ret = gf_string2boolean (self_heal, &priv->data_self_heal);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option data-self-heal %s'. "
				"Defaulting to data-self-heal as 'on'",
				self_heal);
			priv->data_self_heal = 1;
		} 
	}

        priv->data_self_heal_algorithm = "full";

        dict_ret = dict_get_str (this->options, "data-self-heal-algorithm",
                                 &algo);
        if (dict_ret == 0) {
                priv->data_self_heal_algorithm = strdup (algo);
        }


        priv->data_self_heal_window_size = 16;

	dict_ret = dict_get_int32 (this->options, "data-self-heal-window-size",
				   &window_size);
	if (dict_ret == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"Setting data self-heal window size to %d.",
			window_size);

		priv->data_self_heal_window_size = window_size;
	}

	dict_ret = dict_get_str (this->options, "metadata-self-heal",
				 &self_heal);
	if (dict_ret == 0) {
		ret = gf_string2boolean (self_heal, &priv->metadata_self_heal);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option metadata-self-heal %s'. "
				"Defaulting to metadata-self-heal as 'on'.", 
				self_heal);
			priv->metadata_self_heal = 1;
		} 
	}

	dict_ret = dict_get_str (this->options, "entry-self-heal", &self_heal);
	if (dict_ret == 0) {
		ret = gf_string2boolean (self_heal, &priv->entry_self_heal);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option entry-self-heal %s'. "
				"Defaulting to entry-self-heal as 'on'.", 
				self_heal);
			priv->entry_self_heal = 1;
		} 
	}

	/* Change log options */

	priv->data_change_log     = 1;
	priv->metadata_change_log = 0;
	priv->entry_change_log    = 1;

	dict_ret = dict_get_str (this->options, "data-change-log",
				 &change_log);
	if (dict_ret == 0) {
		ret = gf_string2boolean (change_log, &priv->data_change_log);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option data-change-log %s'. "
				"Defaulting to data-change-log as 'on'.", 
				change_log);
			priv->data_change_log = 1;
		} 
	}

	dict_ret = dict_get_str (this->options, "metadata-change-log",
				 &change_log);
	if (dict_ret == 0) {
		ret = gf_string2boolean (change_log,
					 &priv->metadata_change_log);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option metadata-change-log %s'. "
				"Defaulting to metadata-change-log as 'off'.",
				change_log);
			priv->metadata_change_log = 0;
		} 
	}

	dict_ret = dict_get_str (this->options, "entry-change-log",
				 &change_log);
	if (dict_ret == 0) {
		ret = gf_string2boolean (change_log, &priv->entry_change_log);
		if (ret < 0) {
			gf_log (this->name, GF_LOG_WARNING,
				"Invalid 'option entry-change-log %s'. "
				"Defaulting to entry-change-log as 'on'.", 
				change_log);
			priv->entry_change_log = 1;
		} 
	}

	/* Locking options */

	priv->data_lock_server_count = 1;
	priv->metadata_lock_server_count = 0;
	priv->entry_lock_server_count = 1;

	dict_ret = dict_get_int32 (this->options, "data-lock-server-count", 
				   &lock_server_count);
	if (dict_ret == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"Setting data lock server count to %d.",
			lock_server_count);

		if (lock_server_count == 0) 
			gf_log (this->name, GF_LOG_WARNING, "%s",
                                no_lock_servers_warning_str);

		priv->data_lock_server_count = lock_server_count;
	}


	dict_ret = dict_get_int32 (this->options,
				   "metadata-lock-server-count", 
				   &lock_server_count);
	if (dict_ret == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"Setting metadata lock server count to %d.",
			lock_server_count);
		priv->metadata_lock_server_count = lock_server_count;
	}


	dict_ret = dict_get_int32 (this->options, "entry-lock-server-count", 
				   &lock_server_count);
	if (dict_ret == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"Setting entry lock server count to %d.",
			lock_server_count);

		priv->entry_lock_server_count = lock_server_count;
	}

	trav = this->children;
	while (trav) {
		if (!read_ret && !strcmp (read_subvol, trav->xlator->name)) {
			gf_log (this->name, GF_LOG_DEBUG,
				"Subvolume '%s' specified as read child.",
				trav->xlator->name);

			priv->read_child = child_count;
		}

		if (fav_ret == 0 && !strcmp (fav_child, trav->xlator->name)) {
			gf_log (this->name, GF_LOG_WARNING,
				favorite_child_warning_str, trav->xlator->name,
				trav->xlator->name, trav->xlator->name);
			priv->favorite_child = child_count;
		}

		child_count++;
		trav = trav->next;
	}

	priv->wait_count = 1;

	priv->child_count = child_count;

	LOCK_INIT (&priv->lock);
        LOCK_INIT (&priv->read_child_lock);

	priv->child_up = CALLOC (sizeof (unsigned char), child_count);
	if (!priv->child_up) {
		gf_log (this->name, GF_LOG_ERROR,	
			"Out of memory.");		
		op_errno = ENOMEM;			
		goto out;
	}

	priv->children = CALLOC (sizeof (xlator_t *), child_count);
	if (!priv->children) {
		gf_log (this->name, GF_LOG_ERROR,	
			"Out of memory.");		
		op_errno = ENOMEM;			
		goto out;
	}

        priv->pending_key = CALLOC (sizeof (*priv->pending_key), child_count);
        if (!priv->pending_key) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                op_errno = ENOMEM;
                goto out;
        }

	trav = this->children;
	i = 0;
	while (i < child_count) {
		priv->children[i] = trav->xlator;

                ret = asprintf (&priv->pending_key[i], "%s.%s", AFR_XATTR_PREFIX,
                                trav->xlator->name);
                if (-1 == ret) {
                        gf_log (this->name, GF_LOG_ERROR, 
                                "asprintf failed to set pending key");
                        op_errno = ENOMEM;
                        goto out;
                }

		trav = trav->next;
		i++;
	}

	ret = 0;
out:
	return ret;
}


int
fini (xlator_t *this)
{
	return 0;
}


struct xlator_fops fops = {
	.lookup      = afr_lookup,
	.open        = afr_open,
	.lk          = afr_lk,
	.flush       = afr_flush,
	.statfs      = afr_statfs,
	.fsync       = afr_fsync,
	.fsyncdir    = afr_fsyncdir,
	.xattrop     = afr_xattrop,
	.fxattrop    = afr_fxattrop,
	.inodelk     = afr_inodelk,
	.finodelk    = afr_finodelk,
	.entrylk     = afr_entrylk,
	.fentrylk    = afr_fentrylk,
	.checksum    = afr_checksum,

	/* inode read */
	.access      = afr_access,
	.stat        = afr_stat,
	.fstat       = afr_fstat,
	.readlink    = afr_readlink,
	.getxattr    = afr_getxattr,
	.readv       = afr_readv,

	/* inode write */
	.writev      = afr_writev,
	.truncate    = afr_truncate,
	.ftruncate   = afr_ftruncate,
	.setxattr    = afr_setxattr,
        .setattr     = afr_setattr,
	.fsetattr    = afr_fsetattr,
	.removexattr = afr_removexattr,

	/* dir read */
	.opendir     = afr_opendir,
	.readdir     = afr_readdir,
	.readdir     = afr_readdirp,
	.getdents    = afr_getdents,

	/* dir write */
	.create      = afr_create,
	.mknod       = afr_mknod,
	.mkdir       = afr_mkdir,
	.unlink      = afr_unlink,
	.rmdir       = afr_rmdir,
	.link        = afr_link,
	.symlink     = afr_symlink,
	.rename      = afr_rename,
	.setdents    = afr_setdents,
};


struct xlator_mops mops = {
};

struct xlator_dumpops dumpops = {
        .priv       = afr_priv_dump,
};


struct xlator_cbks cbks = {
        .release     = afr_release,
};


struct volume_options options[] = {
	{ .key  = {"read-subvolume" }, 
	  .type = GF_OPTION_TYPE_XLATOR
	},
	{ .key  = {"favorite-child"}, 
	  .type = GF_OPTION_TYPE_XLATOR
	},
        { .key  = {"background-self-heal-count"},
          .type = GF_OPTION_TYPE_INT,
          .min  = 0
        },
	{ .key  = {"data-self-heal"},  
	  .type = GF_OPTION_TYPE_BOOL 
	},
        { .key  = {"data-self-heal-algorithm"},
          .type = GF_OPTION_TYPE_STR
        },
        { .key  = {"data-self-heal-window-size"},
          .type = GF_OPTION_TYPE_INT,
          .min  = 1,
          .max  = 1024
        },
	{ .key  = {"metadata-self-heal"},  
	  .type = GF_OPTION_TYPE_BOOL
	},
	{ .key  = {"entry-self-heal"},  
	  .type = GF_OPTION_TYPE_BOOL 
	},
	{ .key  = {"data-change-log"},  
	  .type = GF_OPTION_TYPE_BOOL 
	},
	{ .key  = {"metadata-change-log"},  
	  .type = GF_OPTION_TYPE_BOOL
	},
	{ .key  = {"entry-change-log"},  
	  .type = GF_OPTION_TYPE_BOOL
	},
	{ .key  = {"data-lock-server-count"},  
	  .type = GF_OPTION_TYPE_INT, 
	  .min  = 0
	},
	{ .key  = {"metadata-lock-server-count"},  
	  .type = GF_OPTION_TYPE_INT, 
	  .min  = 0
	},
	{ .key  = {"entry-lock-server-count"},  
	  .type = GF_OPTION_TYPE_INT,
	  .min  = 0
	},
	{ .key  = {NULL} },
};
