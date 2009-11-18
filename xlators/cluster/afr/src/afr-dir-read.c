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
#include "checksum.h"

#include "afr.h"
#include "afr-self-heal.h"


int
afr_examine_dir_completion_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local  = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh    = &local->self_heal;

        afr_set_opendir_done (this, local->fd->inode, 1);

        AFR_STACK_UNWIND (opendir, sh->orig_frame, local->op_ret,
                          local->op_errno, local->fd);

        return 0;
}


gf_boolean_t
__checksums_differ (uint32_t *checksum, int child_count)
{
        int ret = _gf_false;
        int i = 0;

        uint32_t cksum;

        cksum = checksum[0];

        while (i < child_count) {
                if (cksum != checksum[i]) {
                        ret = _gf_true;
                        break;
                }

                cksum = checksum[i];
                i++;
        }

        return ret;
}


int32_t
afr_examine_dir_readdir_cbk (call_frame_t *frame, void *cookie,
                             xlator_t *this, int32_t op_ret, int32_t op_errno,
                             gf_dirent_t *entries)
{
	afr_private_t * priv     = NULL;
	afr_local_t *   local    = NULL;

        gf_dirent_t * entry = NULL;
        gf_dirent_t * tmp   = NULL;

        int child_index = 0;

        uint32_t entry_cksum;

        int call_count    = 0;
        off_t last_offset = 0;

        priv  = this->private;
        local = frame->local;

        child_index = (long) cookie;

        if (op_ret == -1) {
                local->op_ret = -1;
                local->op_ret = op_errno;
                goto out;
        }

        if (op_ret == 0)
                goto out;

        list_for_each_entry_safe (entry, tmp, &entries->list, list) {
                entry_cksum = gf_rsync_weak_checksum (entry->d_name,
                                                      strlen (entry->d_name));
                local->cont.opendir.checksum[child_index] ^= entry_cksum;
        }

        list_for_each_entry (entry, &entries->list, list) {
                last_offset = entry->d_off;
        }

        /* read more entries */

        STACK_WIND_COOKIE (frame, afr_examine_dir_readdir_cbk,
                           (void *) (long) child_index,
                           priv->children[child_index],
                           priv->children[child_index]->fops->readdir,
                           local->fd, 131072, last_offset);

out:
        if ((op_ret == 0) || (op_ret == -1)) {
                call_count = afr_frame_return (frame);

                if (call_count == 0) {
                        if (__checksums_differ (local->cont.opendir.checksum,
                                                priv->child_count)) {

                                local->need_entry_self_heal   = _gf_true;
                                local->self_heal.forced_merge = _gf_true;

                                local->cont.lookup.buf.st_mode = local->fd->inode->st_mode;

                                local->child_count = afr_up_children_count (priv->child_count,
                                                                            local->child_up);

                                gf_log (this->name, GF_LOG_DEBUG,
                                        "checksums of directory %s differ,"
                                        " triggering forced merge",
                                        local->loc.path);

                                afr_self_heal (frame, this,
                                               afr_examine_dir_completion_cbk);
                        } else {
                                afr_set_opendir_done (this, local->fd->inode, 1);

                                AFR_STACK_UNWIND (opendir, frame, local->op_ret,
                                                  local->op_errno, local->fd);
                        }
                }
        }

        return 0;
}


int
afr_examine_dir (call_frame_t *frame, xlator_t *this)
{
        afr_private_t * priv  = NULL;
        afr_local_t *   local = NULL;

        int i;
        int call_count = 0;

        local = frame->local;
        priv  = this->private;

        local->cont.opendir.checksum = CALLOC (priv->child_count,
                                               sizeof (*local->cont.opendir.checksum));

        call_count = afr_up_children_count (priv->child_count, local->child_up);

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_examine_dir_readdir_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->readdir,
                                           local->fd, 131072, 0);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


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

		if (op_ret >= 0)
			local->op_ret = op_ret;

		local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
                if ((local->op_ret == 0) &&
                    !afr_is_opendir_done (this, fd->inode)) {

                        /*
                         * This is the first opendir on this inode. We need
                         * to check if the directory's entries are the same
                         * on all subvolumes. This is needed in addition
                         * to regular entry self-heal because the readdir
                         * call is sent only to the first subvolume, and
                         * thus files that exist only there will never be healed
                         * otherwise (assuming changelog shows no anamolies).
                         */

                        gf_log (this->name, GF_LOG_TRACE,
                                "reading contents of directory %s looking for mismatch",
                                local->loc.path);

                        afr_examine_dir (frame, this);

                } else {
                        AFR_STACK_UNWIND (opendir, frame, local->op_ret,
                                          local->op_errno, local->fd);
                }
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

        loc_copy (&local->loc, loc);

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
		AFR_STACK_UNWIND (opendir, frame, op_ret, op_errno, fd);
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
        gf_dirent_t * tmp   = NULL;

        int child_index = -1;

	priv     = this->private;
	children = priv->children;

	local = frame->local;

        child_index = (long) cookie;

	if (op_ret != -1) {
                list_for_each_entry_safe (entry, tmp, &entries->list, list) {
                        entry->d_ino = afr_itransform (entry->d_ino,
                                                       priv->child_count,
                                                       child_index);

                        if ((local->fd->inode == local->fd->inode->table->root)
                            && !strcmp (entry->d_name, AFR_TRASH_DIR)) {
                                list_del_init (&entry->list);
                                FREE (entry);
                        }
                }
    	}

        AFR_STACK_UNWIND (readdir, frame, op_ret, op_errno, entries);

	return 0;
}


int32_t
afr_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, gf_dirent_t *entries)
{
        afr_private_t * priv     = NULL;
        afr_local_t *   local    = NULL;
        xlator_t **     children = NULL;
        ino_t           inum = 0;

        gf_dirent_t * entry = NULL;
        gf_dirent_t * tmp   = NULL;

        int child_index = -1;

        priv     = this->private;
        children = priv->children;

        local = frame->local;

        child_index = (long) cookie;

        if (op_ret != -1) {
                list_for_each_entry_safe (entry, tmp, &entries->list, list) {
                        inum = afr_itransform (entry->d_ino, priv->child_count,
                                               child_index);
                        entry->d_ino = inum;
                        inum  = afr_itransform (entry->d_stat.st_ino,
                                                priv->child_count, child_index);
                        entry->d_stat.st_ino = inum;

                        if ((local->fd->inode == local->fd->inode->table->root)
                            && !strcmp (entry->d_name, AFR_TRASH_DIR)) {
                                list_del_init (&entry->list);
                                FREE (entry);
                        }
                }
        }

        AFR_STACK_UNWIND (readdirp, frame, op_ret, op_errno, entries);

        return 0;
}


int32_t
afr_do_readdir (call_frame_t *frame, xlator_t *this,
	        fd_t *fd, size_t size, off_t offset, int whichop)
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

        local->fd                  = fd_ref (fd);
        local->cont.readdir.size   = size;
        local->cont.readdir.offset = offset;

        if (whichop == GF_FOP_READDIR)
                STACK_WIND_COOKIE (frame, afr_readdir_cbk,
                                   (void *) (long) call_child,
                                   children[call_child],
                                   children[call_child]->fops->readdir, fd,
                                   size, offset);
        else
                STACK_WIND_COOKIE (frame, afr_readdirp_cbk,
                                   (void *) (long) call_child,
                                   children[call_child],
                                   children[call_child]->fops->readdirp, fd,
                                   size, offset);

	op_ret = 0;
out:
	if (op_ret == -1) {
		AFR_STACK_UNWIND (readdir, frame, op_ret, op_errno, NULL);
	}
	return 0;
}


int32_t
afr_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset)
{
        afr_do_readdir (frame, this, fd, size, offset, GF_FOP_READDIR);
        return 0;
}


int32_t
afr_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t offset)
{
        afr_do_readdir (frame, this, fd, size, offset, GF_FOP_READDIRP);
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
		AFR_STACK_UNWIND (getdents, frame, op_ret, op_errno,
                                  entry, count);
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
		AFR_STACK_UNWIND (getdents, frame, op_ret, op_errno,
                                  NULL, 0);
	}

	return 0;
}


