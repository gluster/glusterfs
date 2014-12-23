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
#include <string.h>

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
#include "afr-transaction.h"


int32_t
afr_opendir_cbk (call_frame_t *frame, void *cookie,
                 xlator_t *this, int32_t op_ret, int32_t op_errno,
                 fd_t *fd, dict_t *xdata)
{
        afr_local_t   *local             = NULL;
        int            call_count        = -1;
        int32_t        child_index       = 0;
	afr_fd_ctx_t  *fd_ctx = NULL;

        local = frame->local;
	fd_ctx = local->fd_ctx;
        child_index = (long) cookie;

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

        if (call_count == 0)
		AFR_STACK_UNWIND (opendir, frame, local->op_ret,
				  local->op_errno, local->fd, NULL);
        return 0;
}


int
afr_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
        afr_private_t * priv        = NULL;
        afr_local_t   * local       = NULL;
        int             i           = 0;
        int             call_count  = -1;
        int32_t         op_errno    = ENOMEM;
	afr_fd_ctx_t *fd_ctx = NULL;

        priv = this->private;

        local = AFR_FRAME_INIT (frame, op_errno);
	if (!local)
		goto out;

	fd_ctx = afr_fd_ctx_get (fd, this);
	if (!fd_ctx)
		goto out;

        loc_copy (&local->loc, loc);

        local->fd    = fd_ref (fd);
	local->fd_ctx = fd_ctx;

        call_count = local->call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_opendir_cbk,
                                           (void*) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->opendir,
                                           loc, fd, NULL);

                        if (!--call_count)
                                break;
                }
        }

	return 0;
out:
	AFR_STACK_UNWIND (opendir, frame, -1, op_errno, fd, NULL);
        return 0;
}


static void
afr_readdir_transform_entries (gf_dirent_t *subvol_entries, int subvol,
			       gf_dirent_t *entries, fd_t *fd)
{
	afr_private_t *priv = NULL;
        gf_dirent_t *entry = NULL;
        gf_dirent_t *tmp = NULL;
	unsigned char *data_readable = NULL;
	unsigned char *metadata_readable = NULL;
	int gen = 0;

	priv = THIS->private;

	data_readable = alloca0 (priv->child_count);
	metadata_readable = alloca0 (priv->child_count);

        list_for_each_entry_safe (entry, tmp, &subvol_entries->list, list) {
                if (__is_root_gfid (fd->inode->gfid) &&
                    !strcmp (entry->d_name, GF_REPLICATE_TRASH_DIR)) {
			continue;
                }

		list_del_init (&entry->list);
		list_add_tail (&entry->list, &entries->list);

		if (entry->inode) {
			gen = 0;
			afr_inode_read_subvol_get (entry->inode, THIS,
						   data_readable,
						   metadata_readable, &gen);

			if (gen != priv->event_generation ||
				!data_readable[subvol] ||
				!metadata_readable[subvol]) {

				inode_unref (entry->inode);
				entry->inode = NULL;
			}
		}
        }
}


int32_t
afr_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		 int32_t op_ret, int32_t op_errno, gf_dirent_t *subvol_entries,
		 dict_t *xdata)
{
        afr_local_t *local = NULL;
	gf_dirent_t  entries;

	INIT_LIST_HEAD (&entries.list);

        local = frame->local;

        if (op_ret < 0 && !local->cont.readdir.offset) {
		/* failover only if this was first readdir, detected
		   by offset == 0 */
		local->op_ret = op_ret;
		local->op_errno = op_errno;

		afr_read_txn_continue (frame, this, (long) cookie);
		return 0;
	}

	if (op_ret >= 0)
		afr_readdir_transform_entries (subvol_entries, (long) cookie,
					       &entries, local->fd);

        AFR_STACK_UNWIND (readdir, frame, op_ret, op_errno, &entries, xdata);

        gf_dirent_free (&entries);

        return 0;
}


int
afr_readdir_wind (call_frame_t *frame, xlator_t *this, int subvol)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	afr_fd_ctx_t *fd_ctx = NULL;

	priv = this->private;
	local = frame->local;
	fd_ctx = afr_fd_ctx_get (local->fd, this);

	if (subvol == -1) {
		AFR_STACK_UNWIND (readdir, frame, local->op_ret,
				  local->op_errno, 0, 0);
		return 0;
	}

	fd_ctx->readdir_subvol = subvol;

        if (local->op == GF_FOP_READDIR)
                STACK_WIND_COOKIE (frame, afr_readdir_cbk,
                                   (void *) (long) subvol,
                                   priv->children[subvol],
                                   priv->children[subvol]->fops->readdir,
				   local->fd, local->cont.readdir.size,
                                   local->cont.readdir.offset,
				   local->xdata_req);
        else
                STACK_WIND_COOKIE (frame, afr_readdir_cbk,
                                   (void *) (long) subvol,
                                   priv->children[subvol],
                                   priv->children[subvol]->fops->readdirp,
				   local->fd, local->cont.readdir.size,
				   local->cont.readdir.offset,
				   local->xdata_req);
	return 0;
}


int
afr_do_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
		off_t offset, int whichop, dict_t *dict)
{
        afr_local_t   *local     = NULL;
        int32_t       op_errno   = 0;
	int           subvol = -1;
	afr_fd_ctx_t *fd_ctx = NULL;

	local = AFR_FRAME_INIT (frame, op_errno);
	if (!local)
		goto out;

	fd_ctx = afr_fd_ctx_get (fd, this);
	if (!fd_ctx) {
	        op_errno = EINVAL;
		goto out;
        }

	local->op = whichop;
        local->fd = fd_ref (fd);
        local->cont.readdir.size = size;
	local->cont.readdir.offset = offset;
        local->xdata_req = (dict)? dict_ref (dict) : NULL;

	subvol = fd_ctx->readdir_subvol;

	if (offset == 0 || subvol == -1) {
		/* First readdir has option of failing over and selecting
		   an appropriate read subvolume */
		afr_read_txn (frame, this, fd->inode, afr_readdir_wind,
			      AFR_DATA_TRANSACTION);
	} else {
		/* But continued readdirs MUST stick to the same subvolume
		   without an option to failover */
		afr_readdir_wind (frame, this, subvol);
	}

        return 0;
out:
        AFR_STACK_UNWIND (readdir, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
afr_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset, dict_t *xdata)
{
	afr_do_readdir (frame, this, fd, size, offset, GF_FOP_READDIR, xdata);

        return 0;
}


int32_t
afr_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t offset, dict_t *dict)
{
        afr_do_readdir (frame, this, fd, size, offset, GF_FOP_READDIRP, dict);

        return 0;
}


int32_t
afr_releasedir (xlator_t *this, fd_t *fd)
{
        afr_cleanup_fd_ctx (this, fd);

        return 0;
}
