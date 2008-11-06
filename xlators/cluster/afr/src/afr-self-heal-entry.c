/*
  Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include "afr-transaction.h"
#include "afr-self-heal.h"
#include "afr-self-heal-common.h"



int
afr_sh_entry_done (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	/* 
	   TODO: cleanup sh->* 
	*/

	gf_log (this->name, GF_LOG_WARNING,
		"self heal of %s completed",
		local->loc.path);

	sh->completion_cbk (frame, this);

	return 0;
}


int
afr_sh_entry_unlck_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int32_t op_ret, int32_t op_errno)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	int           call_count = 0;
	int           child_index = (long) cookie;

	/* TODO: what if lock fails? */
	
	local = frame->local;
	sh = &local->self_heal;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_ERROR, 
				"unlocking inode of %s on child %d failed: %s",
				local->loc.path, child_index,
				strerror (op_errno));
		} else {
			gf_log (this->name, GF_LOG_DEBUG,
				"unlocked inode of %s on child %d",
				local->loc.path, child_index);
		}

		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		if (sh->healing_fd)
			fd_unref (sh->healing_fd);
		sh->healing_fd = NULL;
		afr_sh_entry_done (frame, this);
	}

	return 0;
}


int
afr_sh_entry_unlock (call_frame_t *frame, xlator_t *this)
{
	int i = 0;				
	int call_count = 0;		     

	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
	afr_self_heal_t * sh  = NULL;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	call_count = local->child_count;

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			gf_log (this->name, GF_LOG_DEBUG,
				"unlocking %s on subvolume %s",
				local->loc.path, priv->children[i]->name);

			STACK_WIND_COOKIE (frame, afr_sh_entry_unlck_cbk,
					   (void *) (long) i,
					   priv->children[i], 
					   priv->children[i]->fops->entrylk,
					   &local->loc, NULL,
					   GF_DIR_LK_UNLOCK, GF_DIR_LK_WRLCK);
			if (!--call_count)
				break;
		}
	}

	return 0;
}


int
afr_sh_entry_finish (call_frame_t *frame, xlator_t *this)
{
	afr_local_t   *local = NULL;

	local = frame->local;

	gf_log (this->name, GF_LOG_DEBUG,
		"finishing entry selfheal of %s", local->loc.path);

	afr_sh_entry_unlock (frame, this);

	return 0;
}


int
afr_sh_entry_erase_pending_cbk (call_frame_t *frame, void *cookie,
				xlator_t *this, int32_t op_ret,
				int32_t op_errno, dict_t *xattr)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int             call_count = 0;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0)
		afr_sh_entry_finish (frame, this);

	return 0;
}


int
afr_sh_entry_erase_pending (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              call_count = 0;
	int              i = 0;
	dict_t          **erase_xattr = NULL;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;


	afr_sh_pending_to_delta (sh->pending_matrix, sh->delta_matrix,
				 sh->success, priv->child_count);

	erase_xattr = calloc (sizeof (*erase_xattr), priv->child_count);

	for (i = 0; i < priv->child_count; i++) {
		if (sh->xattr[i]) {
			call_count++;

			erase_xattr[i] = get_new_dict();
			dict_ref (erase_xattr[i]);
		}
	}

	afr_sh_delta_to_xattr (sh->delta_matrix, erase_xattr,
			       priv->child_count, AFR_ENTRY_PENDING);

	local->call_count = call_count;
	for (i = 0; i < priv->child_count; i++) {
		if (!erase_xattr[i])
			continue;

		gf_log (this->name, GF_LOG_DEBUG,
			"erasing pending flags from %s on %s",
			local->loc.path, priv->children[i]->name);

		STACK_WIND_COOKIE (frame, afr_sh_entry_erase_pending_cbk,
				   (void *) (long) i,
				   priv->children[i],
				   priv->children[i]->fops->xattrop,
				   &local->loc,
				   GF_XATTROP_ADD_ARRAY, erase_xattr[i]);
		if (!--call_count)
			break;
	}

	for (i = 0; i < priv->child_count; i++) {
		if (erase_xattr[i]) {
			dict_unref (erase_xattr[i]);
		}
	}
	FREE (erase_xattr);

	return 0;
}



static int
next_active_source (call_frame_t *frame, xlator_t *this,
		    int current_active_source)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *local  = NULL;
	afr_self_heal_t *sh  = NULL;
	int              source = -1;
	int              next_active_source = -1;
	int              i = 0;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	source = sh->source;

	if (source != -1) {
		if (current_active_source != source)
			next_active_source = source;
		goto out;
	}

	/*
	  the next active sink becomes the source for the
	  'conservative decision' of merging all entries
	*/

	for (i = 0; i < priv->child_count; i++) {
		if ((sh->sources[i] == 0)
		    && (local->child_up[i] == 1)
		    && (i > current_active_source)) {

			next_active_source = i;
			break;
		}
	}
out:
	return next_active_source;
}



static int
next_active_sink (call_frame_t *frame, xlator_t *this,
		  int current_active_sink)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *local  = NULL;
	afr_self_heal_t *sh  = NULL;
	int              next_active_sink = -1;
	int              i = 0;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	/*
	  the next active sink becomes the source for the
	  'conservative decision' of merging all entries
	*/

	for (i = 0; i < priv->child_count; i++) {
		if ((sh->sources[i] == 0)
		    && (local->child_up[i] == 1)
		    && (i > current_active_sink)) {

			next_active_sink = i;
			break;
		}
	}

	return next_active_sink;
}


int
build_child_loc (xlator_t *this, loc_t *child, loc_t *parent, char *name)
{
	int   ret = -1;

	if (!child) {
		goto out;
	}

	if (strcmp (parent->path, "/") == 0)
		asprintf ((char **)&child->path, "/%s", name);
	else
		asprintf ((char **)&child->path, "%s/%s", parent->path, name);

	if (!child->path) {
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory :(");
		goto out;
	}

	child->name = strrchr (child->path, '/');
	if (child->name)
		child->name++;

	child->parent = inode_ref (parent->inode);
	child->inode = inode_new (parent->inode->table);

	if (!child->inode) {
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory :(");
		goto out;
	}

	ret = 0;
out:
	if (ret == -1)
		loc_wipe (child);

	return ret;
}


int
afr_sh_entry_expunge_all (call_frame_t *frame, xlator_t *this);

int
afr_sh_entry_expunge_subvol (call_frame_t *frame, xlator_t *this,
			     int active_src);

int
afr_sh_entry_expunge_entry_done (call_frame_t *frame, xlator_t *this,
				 int active_src)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *local  = NULL;
	afr_self_heal_t *sh  = NULL;
	int              call_count = 0;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0)
		afr_sh_entry_expunge_subvol (frame, this, active_src);

	return 0;
}


int
afr_sh_entry_expunge_remove_cbk (call_frame_t *expunge_frame, void *cookie,
				 xlator_t *this,
				 int32_t op_ret, int32_t op_errno)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *expunge_local = NULL;
	afr_self_heal_t *expunge_sh = NULL;
	int              active_src = 0;
	call_frame_t    *frame = NULL;


	priv = this->private;
	expunge_local = expunge_frame->local;
	expunge_sh = &expunge_local->self_heal;
	frame = expunge_sh->sh_frame;

	active_src = (long) cookie;

	if (op_ret == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"removed %s on %s",
			expunge_local->loc.path,
			priv->children[active_src]->name);
	} else {
		gf_log (this->name, GF_LOG_ERROR,
			"removing %s on %s failed (%s)",
			expunge_local->loc.path,
			priv->children[active_src]->name,
			strerror (op_errno));
	}

	AFR_STACK_DESTROY (expunge_frame);
	afr_sh_entry_expunge_entry_done (frame, this, active_src);

	return 0;
}


int
afr_sh_entry_expunge_rmdir (call_frame_t *expunge_frame, xlator_t *this,
			     int active_src)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *expunge_local = NULL;

	priv = this->private;
	expunge_local = expunge_frame->local;

	gf_log (this->name, GF_LOG_WARNING,
		"removing directory %s on %s",
		expunge_local->loc.path, priv->children[active_src]->name);

	STACK_WIND_COOKIE (expunge_frame, afr_sh_entry_expunge_remove_cbk,
			   (void *) (long) active_src,
			   priv->children[active_src],
			   priv->children[active_src]->fops->rmdir,
			   &expunge_local->loc);

	return 0;
}


int
afr_sh_entry_expunge_unlink (call_frame_t *expunge_frame, xlator_t *this,
			     int active_src)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *expunge_local = NULL;

	priv = this->private;
	expunge_local = expunge_frame->local;

	gf_log (this->name, GF_LOG_WARNING,
		"unlinking file %s on %s",
		expunge_local->loc.path, priv->children[active_src]->name);
	
	STACK_WIND_COOKIE (expunge_frame, afr_sh_entry_expunge_remove_cbk,
			   (void *) (long) active_src,
			   priv->children[active_src],
			   priv->children[active_src]->fops->unlink,
			   &expunge_local->loc);

	return 0;
}


int
afr_sh_entry_expunge_remove (call_frame_t *expunge_frame, xlator_t *this,
			     int active_src, struct stat *buf)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *expunge_local = NULL;
	afr_self_heal_t *expunge_sh = NULL;
	int              source = 0;
	call_frame_t    *frame = NULL;
	int              type = 0;

	priv = this->private;
	expunge_local = expunge_frame->local;
	expunge_sh = &expunge_local->self_heal;
	frame = expunge_sh->sh_frame;
	source = expunge_sh->source;

	type = (buf->st_mode & S_IFMT);

	switch (type) {
	case S_IFSOCK:
	case S_IFREG:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFIFO:
	case S_IFLNK:
		afr_sh_entry_expunge_unlink (expunge_frame, this, active_src);

		break;
	case S_IFDIR:
		afr_sh_entry_expunge_rmdir (expunge_frame, this, active_src);
		break;
	default:
		gf_log (this->name, GF_LOG_ERROR,
			"%s has unknown file type on %s: 0%o",
			expunge_local->loc.path,
			priv->children[source]->name, type);
		goto out;
		break;
	}

	return 0;
out:
	AFR_STACK_DESTROY (expunge_frame);
	afr_sh_entry_expunge_entry_done (frame, this, active_src);

	return 0;
}


int
afr_sh_entry_expunge_lookup_cbk (call_frame_t *expunge_frame, void *cookie,
				xlator_t *this,
				int32_t op_ret,	int32_t op_errno,
				inode_t *inode, struct stat *buf, dict_t *x)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *expunge_local = NULL;
	afr_self_heal_t *expunge_sh = NULL;
	call_frame_t    *frame = NULL;
	int              active_src = 0;

	priv = this->private;
	expunge_local = expunge_frame->local;
	expunge_sh = &expunge_local->self_heal;
	frame = expunge_sh->sh_frame;
	active_src = (long) cookie;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_ERROR,
			"lookup of %s on %s failed (%s)",
			expunge_local->loc.path,
			priv->children[active_src]->name,
			strerror (op_errno));
		goto out;
	}

	afr_sh_entry_expunge_remove (expunge_frame, this, active_src, buf);

	return 0;
out:
	AFR_STACK_DESTROY (expunge_frame);
	afr_sh_entry_expunge_entry_done (frame, this, active_src);

	return 0;
}


int
afr_sh_entry_expunge_purge (call_frame_t *expunge_frame, xlator_t *this,
			    int active_src)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *expunge_local = NULL;

	priv = this->private;
	expunge_local = expunge_frame->local;

	gf_log (this->name, GF_LOG_WARNING,
		"looking up %s on %s",
		expunge_local->loc.path, priv->children[active_src]->name);
	
	STACK_WIND_COOKIE (expunge_frame, afr_sh_entry_expunge_lookup_cbk,
			   (void *) (long) active_src,
			   priv->children[active_src],
			   priv->children[active_src]->fops->lookup,
			   &expunge_local->loc, 0);

	return 0;
}


int
afr_sh_entry_expunge_entry_cbk (call_frame_t *expunge_frame, void *cookie,
				xlator_t *this,
				int32_t op_ret,	int32_t op_errno,
				inode_t *inode, struct stat *buf, dict_t *x)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *expunge_local = NULL;
	afr_self_heal_t *expunge_sh = NULL;
	int              source = 0;
	call_frame_t    *frame = NULL;
	int              active_src = 0;


	priv = this->private;
	expunge_local = expunge_frame->local;
	expunge_sh = &expunge_local->self_heal;
	frame = expunge_sh->sh_frame;
	active_src = expunge_sh->active_source;
	source = (long) cookie;

	if (op_ret == -1 && op_errno == ENOENT) {

		gf_log (this->name, GF_LOG_DEBUG,
			"missing entry %s on %s",
			expunge_local->loc.path,
			priv->children[source]->name);

		afr_sh_entry_expunge_purge (expunge_frame, this, active_src);

		return 0;
	}

	if (op_ret == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"%s exists under %s",
			expunge_local->loc.path,
			priv->children[source]->name);
	} else {
		gf_log (this->name, GF_LOG_ERROR,
			"looking up %s under %s failed (%s)",
			expunge_local->loc.path,
			priv->children[source]->name,
			strerror (op_errno));
	}

	AFR_STACK_DESTROY (expunge_frame);
	afr_sh_entry_expunge_entry_done (frame, this, active_src);

	return 0;
}


int
afr_sh_entry_expunge_entry (call_frame_t *frame, xlator_t *this,
			    char *name)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *local  = NULL;
	afr_self_heal_t *sh  = NULL;
	int              ret = -1;
	call_frame_t    *expunge_frame = NULL;
	afr_local_t     *expunge_local = NULL;
	afr_self_heal_t *expunge_sh = NULL;
	int              active_src = 0;
	int              source = 0;
	int              op_errno = 0;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	active_src = sh->active_source;
	source = sh->source;

	if ((strcmp (name, ".") == 0)
	    || (strcmp (name, "..") == 0)) {
		gf_log (this->name, GF_LOG_DEBUG,
			"skipping inspection of %s under %s",
			name, local->loc.path);
		goto out;
	}

	gf_log (this->name, GF_LOG_DEBUG,
		"inspecting existance of %s under %s",
		name, local->loc.path);

	expunge_frame = copy_frame (frame);
	if (!expunge_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory :(");
		goto out;
	}

	ALLOC_OR_GOTO (expunge_local, afr_local_t, out);

	expunge_frame->local = expunge_local;
	expunge_sh = &expunge_local->self_heal;
	expunge_sh->sh_frame = frame;
	expunge_sh->active_source = active_src;

	ret = build_child_loc (this, &expunge_local->loc, &local->loc, name);
	if (ret != 0) {
		goto out;
	}

	gf_log (this->name, GF_LOG_DEBUG,
		"looking up %s on %s", expunge_local->loc.path,
		priv->children[source]->name);

	STACK_WIND_COOKIE (expunge_frame,
			   afr_sh_entry_expunge_entry_cbk,
			   (void *) (long) source,
			   priv->children[source],
			   priv->children[source]->fops->lookup,
			   &expunge_local->loc, 0);

	ret = 0;
out:
	if (ret == -1)
		afr_sh_entry_expunge_entry_done (frame, this, active_src);

	return 0;
}


int
afr_sh_entry_expunge_readdir_cbk (call_frame_t *frame, void *cookie,
				  xlator_t *this,
				  int32_t op_ret, int32_t op_errno,
				  gf_dirent_t *entries)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *local  = NULL;
	afr_self_heal_t *sh  = NULL;
	gf_dirent_t     *entry = NULL;
	off_t            last_offset = 0;
	int              active_src = 0;
	int              entry_count = 0;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	active_src = sh->active_source;

	if (op_ret <= 0) {
		if (op_ret < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"readdir of %s on subvolume %s failed (%s)",
				local->loc.path,
				priv->children[active_src]->name,
				strerror (op_errno));
		} else {
			gf_log (this->name, GF_LOG_DEBUG,
				"readdir of %s on subvolume %s complete",
				local->loc.path,
				priv->children[active_src]->name);
		}

		afr_sh_entry_expunge_all (frame, this);
		return 0;
	}

	list_for_each_entry (entry, &entries->list, list) {
		last_offset = entry->d_off;
		entry_count++;
	}

	gf_log (this->name, GF_LOG_DEBUG,
		"readdir'ed %d entries from %s",
		entry_count, priv->children[active_src]->name);

	sh->offset = last_offset;
	local->call_count = entry_count;

	list_for_each_entry (entry, &entries->list, list) {
		afr_sh_entry_expunge_entry (frame, this, entry->d_name);
	}

	return 0;
}

int
afr_sh_entry_expunge_subvol (call_frame_t *frame, xlator_t *this,
			     int active_src)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *local  = NULL;
	afr_self_heal_t *sh  = NULL;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	STACK_WIND (frame, afr_sh_entry_expunge_readdir_cbk,
		    priv->children[active_src],
		    priv->children[active_src]->fops->readdir,
		    sh->healing_fd, sh->block_size, sh->offset);

	return 0;
}


int
afr_sh_entry_expunge_all (call_frame_t *frame, xlator_t *this)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *local  = NULL;
	afr_self_heal_t *sh  = NULL;
	int              active_src = -1;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	sh->offset = 0;

	if (sh->source == -1) {
		gf_log (this->name, GF_LOG_WARNING,
			"no active sources for %s to expunge entries",
			local->loc.path);
		goto out;
	}

	active_src = next_active_sink (frame, this, sh->active_source);
	sh->active_source = active_src;

	if (sh->op_failed) {
		goto out;
	}

	if (active_src == -1) {
		/* completed creating missing files on all subvolumes */
		goto out;
	}

	gf_log (this->name, GF_LOG_WARNING,
		"expunging entries of %s on %s to other sinks",
		local->loc.path, priv->children[active_src]->name);

	afr_sh_entry_expunge_subvol (frame, this, active_src);

	return 0;
out:
	afr_sh_entry_erase_pending (frame, this);
	return 0;

}


int
afr_sh_entry_impunge_all (call_frame_t *frame, xlator_t *this);

int
afr_sh_entry_impunge_subvol (call_frame_t *frame, xlator_t *this,
			     int active_src);

int
afr_sh_entry_impunge_entry_done (call_frame_t *frame, xlator_t *this,
				 int active_src)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *local  = NULL;
	afr_self_heal_t *sh  = NULL;
	int              call_count = 0;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0)
		afr_sh_entry_impunge_subvol (frame, this, active_src);

	return 0;
}


int
afr_sh_entry_impunge_utimens_cbk (call_frame_t *impunge_frame, void *cookie,
				  xlator_t *this, int32_t op_ret,
				  int32_t op_errno, struct stat *stbuf)
{
	int              call_count = 0;
	afr_private_t   *priv = NULL;
	afr_local_t     *impunge_local = NULL;
	afr_self_heal_t *impunge_sh = NULL;
	call_frame_t    *frame = NULL;
	int              active_src = 0;
	int              child_index = 0;

	priv = this->private;
	impunge_local = impunge_frame->local;
	impunge_sh = &impunge_local->self_heal;
	frame = impunge_sh->sh_frame;
	child_index = (long) cookie;

	if (op_ret == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"utimes set for %s on %s",
			impunge_local->loc.path,
			priv->children[child_index]->name);
	} else {
		gf_log (this->name, GF_LOG_ERROR,
			"setting utimes of %s on %s failed (%s)",
			impunge_local->loc.path,
			priv->children[child_index]->name,
			strerror (op_errno));
	}

	LOCK (&impunge_frame->lock);
	{
		call_count = --impunge_local->call_count;
	}
	UNLOCK (&impunge_frame->lock);

	if (call_count == 0) {
		AFR_STACK_DESTROY (impunge_frame);
		afr_sh_entry_impunge_entry_done (frame, this, active_src);
	}

	return 0;
}


int
afr_sh_entry_impunge_chown_cbk (call_frame_t *impunge_frame, void *cookie,
				xlator_t *this, int32_t op_ret,
				int32_t op_errno, struct stat *stbuf)
{
	int              call_count = 0;
	afr_private_t   *priv = NULL;
	afr_local_t     *impunge_local = NULL;
	afr_self_heal_t *impunge_sh = NULL;
	call_frame_t    *frame = NULL;
	int              active_src = 0;
	int              child_index = 0;
	struct timespec  ts[2];


	priv = this->private;
	impunge_local = impunge_frame->local;
	impunge_sh = &impunge_local->self_heal;
	frame = impunge_sh->sh_frame;
	child_index = (long) cookie;

	if (op_ret == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"ownership of %s on %s changed",
			impunge_local->loc.path,
			priv->children[child_index]->name);
	} else {
		gf_log (this->name, GF_LOG_ERROR,
			"setting ownership of %s on %s failed (%s)",
			impunge_local->loc.path,
			priv->children[child_index]->name,
			strerror (op_errno));
		goto out;
	}

#ifdef HAVE_TV_NSEC
	ts[0] = impunge_local->cont.lookup.buf.st_atim;
	ts[1] = impunge_local->cont.lookup.buf.st_mtim;
#else
	ts[0].tv_sec = impunge_local->cont.lookup.buf.st_atime;
	ts[1].tv_sec = impunge_local->cont.lookup.buf.st_mtime;
#endif

	STACK_WIND_COOKIE (impunge_frame,
			   afr_sh_entry_impunge_utimens_cbk,
			   (void *) (long) child_index,
			   priv->children[child_index],
			   priv->children[child_index]->fops->utimens,
			   &impunge_local->loc, ts);

	return 0;

out:
	LOCK (&impunge_frame->lock);
	{
		call_count = --impunge_local->call_count;
	}
	UNLOCK (&impunge_frame->lock);

	if (call_count == 0) {
		AFR_STACK_DESTROY (impunge_frame);
		afr_sh_entry_impunge_entry_done (frame, this, active_src);
	}

	return 0;
}


int
afr_sh_entry_impunge_newfile_cbk (call_frame_t *impunge_frame, void *cookie,
				  xlator_t *this,
				  int32_t op_ret, int32_t op_errno,
				  inode_t *inode, struct stat *stbuf)
{
	int              call_count = 0;
	afr_private_t   *priv = NULL;
	afr_local_t     *impunge_local = NULL;
	afr_self_heal_t *impunge_sh = NULL;
	call_frame_t    *frame = NULL;
	int              active_src = 0;
	int              child_index = 0;

	priv = this->private;
	impunge_local = impunge_frame->local;
	impunge_sh = &impunge_local->self_heal;
	frame = impunge_sh->sh_frame;

	child_index = (long) cookie;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_ERROR,
			"creation of %s on %s failed (%s)",
			impunge_local->loc.path,
			priv->children[child_index]->name,
			strerror (op_errno));
		goto out;
	}

	gf_log (this->name, GF_LOG_DEBUG,
		"setting ownership of %s on %s to %d/%d",
		impunge_local->loc.path,
		priv->children[child_index]->name,
		impunge_local->cont.lookup.buf.st_uid,
		impunge_local->cont.lookup.buf.st_gid);

	STACK_WIND_COOKIE (impunge_frame, afr_sh_entry_impunge_chown_cbk,
			   (void *) (long) child_index,
			   priv->children[child_index],
			   priv->children[child_index]->fops->chown,
			   &impunge_local->loc,
			   impunge_local->cont.lookup.buf.st_uid,
			   impunge_local->cont.lookup.buf.st_gid);
	return 0;

out:
	LOCK (&impunge_frame->lock);
	{
		call_count = --impunge_local->call_count;
	}
	UNLOCK (&impunge_frame->lock);

	if (call_count == 0) {
		AFR_STACK_DESTROY (impunge_frame);
		afr_sh_entry_impunge_entry_done (frame, this, active_src);
	}

	return 0;
}


int
afr_sh_entry_impunge_mknod (call_frame_t *impunge_frame, xlator_t *this,
			    int child_index, struct stat *stbuf)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *impunge_local = NULL;
	afr_self_heal_t *impunge_sh = NULL;


	priv = this->private;
	impunge_local = impunge_frame->local;
	impunge_sh = &impunge_local->self_heal;

	gf_log (this->name, GF_LOG_WARNING,
		"creating file %s mode=0%o dev=0x%llx on %s",
		impunge_local->loc.path,
		stbuf->st_mode, stbuf->st_rdev,
		priv->children[child_index]->name);

	STACK_WIND_COOKIE (impunge_frame, afr_sh_entry_impunge_newfile_cbk,
			   (void *) (long) child_index,
			   priv->children[child_index],
			   priv->children[child_index]->fops->mknod,
			   &impunge_local->loc,
			   stbuf->st_mode, stbuf->st_rdev);

	return 0;
}



int
afr_sh_entry_impunge_mkdir (call_frame_t *impunge_frame, xlator_t *this,
			    int child_index, struct stat *stbuf)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *impunge_local = NULL;
	afr_self_heal_t *impunge_sh = NULL;


	priv = this->private;
	impunge_local = impunge_frame->local;
	impunge_sh = &impunge_local->self_heal;

	gf_log (this->name, GF_LOG_WARNING,
		"creating directory %s mode=0%o on %s",
		impunge_local->loc.path,
		stbuf->st_mode,
		priv->children[child_index]->name);

	STACK_WIND_COOKIE (impunge_frame, afr_sh_entry_impunge_newfile_cbk,
			   (void *) (long) child_index,
			   priv->children[child_index],
			   priv->children[child_index]->fops->mkdir,
			   &impunge_local->loc, stbuf->st_mode);

	return 0;
}


int
afr_sh_entry_impunge_symlink (call_frame_t *impunge_frame, xlator_t *this,
			      int child_index, const char *linkname)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *impunge_local = NULL;
	afr_self_heal_t *impunge_sh = NULL;


	priv = this->private;
	impunge_local = impunge_frame->local;
	impunge_sh = &impunge_local->self_heal;

	gf_log (this->name, GF_LOG_WARNING,
		"creating symlink %s -> %s on %s",
		impunge_local->loc.path, linkname,
		priv->children[child_index]->name);

	STACK_WIND_COOKIE (impunge_frame, afr_sh_entry_impunge_newfile_cbk,
			   (void *) (long) child_index,
			   priv->children[child_index],
			   priv->children[child_index]->fops->symlink,
			   linkname, &impunge_local->loc);

	return 0;
}


int
afr_sh_entry_impunge_readlink_cbk (call_frame_t *impunge_frame, void *cookie,
				   xlator_t *this,
				   int32_t op_ret, int32_t op_errno,
				   const char *linkname)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *impunge_local = NULL;
	afr_self_heal_t *impunge_sh = NULL;
	int              child_index = -1;
	call_frame_t    *frame = NULL;
	int              call_count = -1;
	int              active_src = -1;

	priv = this->private;
	impunge_local = impunge_frame->local;
	impunge_sh = &impunge_local->self_heal;
	frame = impunge_sh->sh_frame;
	active_src = impunge_sh->active_source;

	child_index = (long) cookie;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_ERROR,
			"readlink of %s on %s failed (%s)",
			impunge_local->loc.path,
			priv->children[active_src]->name,
			strerror (op_errno));
		goto out;
	}

	afr_sh_entry_impunge_symlink (impunge_frame, this, child_index,
				      linkname);
	return 0;

out:
	LOCK (&impunge_frame->lock);
	{
		call_count = --impunge_local->call_count;
	}
	UNLOCK (&impunge_frame->lock);

	if (call_count == 0) {
		AFR_STACK_DESTROY (impunge_frame);
		afr_sh_entry_impunge_entry_done (frame, this, active_src);
	}

	return 0;
}


int
afr_sh_entry_impunge_readlink (call_frame_t *impunge_frame, xlator_t *this,
			       int child_index, struct stat *stbuf)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *impunge_local = NULL;
	afr_self_heal_t *impunge_sh = NULL;
	int              active_src = -1;

	priv = this->private;
	impunge_local = impunge_frame->local;
	impunge_sh = &impunge_local->self_heal;
	active_src = impunge_sh->active_source;

	STACK_WIND_COOKIE (impunge_frame, afr_sh_entry_impunge_readlink_cbk,
			   (void *) (long) child_index,
			   priv->children[active_src],
			   priv->children[active_src]->fops->readlink,
			   &impunge_local->loc, 4096);

	return 0;
}


int
afr_sh_entry_impunge_recreate_lookup_cbk (call_frame_t *impunge_frame,
					  void *cookie, xlator_t *this,
					  int32_t op_ret, int32_t op_errno,
					  inode_t *inode, struct stat *buf,
					  dict_t *xattr)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *impunge_local = NULL;
	afr_self_heal_t *impunge_sh = NULL;
	int              active_src = 0;
	int              type = 0;
	int              child_index = 0;
	call_frame_t    *frame = NULL;
	int              call_count = 0;

	priv = this->private;
	impunge_local = impunge_frame->local;
	impunge_sh = &impunge_local->self_heal;
	frame = impunge_sh->sh_frame;

	child_index = (long) cookie;

	active_src = impunge_sh->active_source;

	if (op_ret != 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"looking up %s on %s (for %s) failed (%s)",
			impunge_local->loc.path,
			priv->children[active_src]->name,
			priv->children[child_index]->name,
			strerror (op_errno));
		goto out;
	}

	impunge_local->cont.lookup.buf = *buf;
	type = (buf->st_mode & S_IFMT);

	switch (type) {
	case S_IFSOCK:
	case S_IFREG:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFIFO:
		afr_sh_entry_impunge_mknod (impunge_frame, this,
					    child_index, buf);
		break;
	case S_IFLNK:
		afr_sh_entry_impunge_readlink (impunge_frame, this,
					       child_index, buf);
		break;
	case S_IFDIR:
		afr_sh_entry_impunge_mkdir (impunge_frame, this,
					    child_index, buf);
		break;
	default:
		gf_log (this->name, GF_LOG_ERROR,
			"%s has unknown file type on %s: 0%o",
			impunge_local->loc.path,
			priv->children[active_src]->name, type);
		goto out;
		break;
	}

	return 0;

out:
	LOCK (&impunge_frame->lock);
	{
		call_count = --impunge_local->call_count;
	}
	UNLOCK (&impunge_frame->lock);

	if (call_count == 0) {
		AFR_STACK_DESTROY (impunge_frame);
		afr_sh_entry_impunge_entry_done (frame, this, active_src);
	}

	return 0;
}


int
afr_sh_entry_impunge_recreate (call_frame_t *impunge_frame, xlator_t *this,
			       int child_index)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *impunge_local = NULL;
	afr_self_heal_t *impunge_sh = NULL;
	int              active_src = 0;


	priv = this->private;
	impunge_local = impunge_frame->local;
	impunge_sh = &impunge_local->self_heal;

	active_src = impunge_sh->active_source;

	STACK_WIND_COOKIE (impunge_frame,
			   afr_sh_entry_impunge_recreate_lookup_cbk,
			   (void *) (long) child_index,
			   priv->children[active_src],
			   priv->children[active_src]->fops->lookup,
			   &impunge_local->loc, 0);

	return 0;
}


int
afr_sh_entry_impunge_entry_cbk (call_frame_t *impunge_frame, void *cookie,
				xlator_t *this,
				int32_t op_ret,	int32_t op_errno,
				inode_t *inode, struct stat *buf, dict_t *x)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *impunge_local = NULL;
	afr_self_heal_t *impunge_sh = NULL;
	int              call_count = 0;
	int              child_index = 0;
	call_frame_t    *frame = NULL;
	int              active_src = 0;

	priv = this->private;
	impunge_local = impunge_frame->local;
	impunge_sh = &impunge_local->self_heal;
	frame = impunge_sh->sh_frame;
	child_index = (long) cookie;
	active_src = impunge_sh->active_source;

	if (op_ret == -1 && op_errno == ENOENT) {
		/* decrease call_count in recreate-callback */
		gf_log (this->name, GF_LOG_DEBUG,
			"missing entry %s on %s",
			impunge_local->loc.path,
			priv->children[child_index]->name);

		afr_sh_entry_impunge_recreate (impunge_frame, this,
					       child_index);
		return 0;
	}

	if (op_ret == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"%s exists under %s",
			impunge_local->loc.path,
			priv->children[child_index]->name);
	} else {
		gf_log (this->name, GF_LOG_ERROR,
			"looking up %s under %s failed (%s)",
			impunge_local->loc.path,
			priv->children[child_index]->name,
			strerror (op_errno));
	}

	LOCK (&impunge_frame->lock);
	{
		call_count = --impunge_local->call_count;
	}
	UNLOCK (&impunge_frame->lock);

	if (call_count == 0) {
		AFR_STACK_DESTROY (impunge_frame);
		afr_sh_entry_impunge_entry_done (frame, this, active_src);
	}

	return 0;
}


int
afr_sh_entry_impunge_entry (call_frame_t *frame, xlator_t *this,
			    char *name)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *local  = NULL;
	afr_self_heal_t *sh  = NULL;
	int              ret = -1;
	call_frame_t    *impunge_frame = NULL;
	afr_local_t     *impunge_local = NULL;
	afr_self_heal_t *impunge_sh = NULL;
	int              active_src = 0;
	int              i = 0;
	int              call_count = 0;
	int              op_errno = 0;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	active_src = sh->active_source;

	if ((strcmp (name, ".") == 0)
	    || (strcmp (name, "..") == 0)) {
		gf_log (this->name, GF_LOG_DEBUG,
			"skipping inspection of %s under %s",
			name, local->loc.path);
		goto out;
	}

	gf_log (this->name, GF_LOG_DEBUG,
		"inspecting existance of %s under %s",
		name, local->loc.path);

	impunge_frame = copy_frame (frame);
	if (!impunge_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"out of memory :(");
		goto out;
	}

	ALLOC_OR_GOTO (impunge_local, afr_local_t, out);

	impunge_frame->local = impunge_local;
	impunge_sh = &impunge_local->self_heal;
	impunge_sh->sh_frame = frame;
	impunge_sh->active_source = active_src;

	ret = build_child_loc (this, &impunge_local->loc, &local->loc, name);
	if (ret != 0) {
		goto out;
	}

	for (i = 0; i < priv->child_count; i++) {
		if (i == active_src)
			continue;
		if (local->child_up[i] == 0)
			continue;
		if (sh->sources[i] == 1)
			continue;
		call_count++;
	}

	impunge_local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {
		if (i == active_src)
			continue;
		if (local->child_up[i] == 0)
			continue;
		if (sh->sources[i] == 1)
			continue;

		gf_log (this->name, GF_LOG_DEBUG,
			"looking up %s on %s", impunge_local->loc.path,
			priv->children[i]->name);

		STACK_WIND_COOKIE (impunge_frame,
				   afr_sh_entry_impunge_entry_cbk,
				   (void *) (long) i,
				   priv->children[i],
				   priv->children[i]->fops->lookup,
				   &impunge_local->loc, 0);

		if (!--call_count)
			break;
	}

	ret = 0;
out:
	if (ret == -1)
		afr_sh_entry_impunge_entry_done (frame, this, active_src);
	
	return 0;
}


int
afr_sh_entry_impunge_readdir_cbk (call_frame_t *frame, void *cookie,
				  xlator_t *this,
				  int32_t op_ret, int32_t op_errno,
				  gf_dirent_t *entries)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *local  = NULL;
	afr_self_heal_t *sh  = NULL;
	gf_dirent_t     *entry = NULL;
	off_t            last_offset = 0;
	int              active_src = 0;
	int              entry_count = 0;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	active_src = sh->active_source;

	if (op_ret <= 0) {
		if (op_ret < 0) {
			gf_log (this->name, GF_LOG_ERROR,
				"readdir of %s on subvolume %s failed (%s)",
				local->loc.path,
				priv->children[active_src]->name,
				strerror (op_errno));
		} else {
			gf_log (this->name, GF_LOG_DEBUG,
				"readdir of %s on subvolume %s complete",
				local->loc.path,
				priv->children[active_src]->name);
		}

		afr_sh_entry_impunge_all (frame, this);
		return 0;
	}

	list_for_each_entry (entry, &entries->list, list) {
		last_offset = entry->d_off;
		entry_count++;
	}

	gf_log (this->name, GF_LOG_DEBUG,
		"readdir'ed %d entries from %s",
		entry_count, priv->children[active_src]->name);

	sh->offset = last_offset;
	local->call_count = entry_count;

	list_for_each_entry (entry, &entries->list, list) {
		afr_sh_entry_impunge_entry (frame, this, entry->d_name);
	}

	return 0;
}
				  

int
afr_sh_entry_impunge_subvol (call_frame_t *frame, xlator_t *this,
			     int active_src)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *local  = NULL;
	afr_self_heal_t *sh  = NULL;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	STACK_WIND (frame, afr_sh_entry_impunge_readdir_cbk,
		    priv->children[active_src],
		    priv->children[active_src]->fops->readdir,
		    sh->healing_fd, sh->block_size, sh->offset);

	return 0;
}


int
afr_sh_entry_impunge_all (call_frame_t *frame, xlator_t *this)
{
	afr_private_t   *priv = NULL;
	afr_local_t     *local  = NULL;
	afr_self_heal_t *sh  = NULL;
	int              active_src = -1;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	sh->offset = 0;

	active_src = next_active_source (frame, this, sh->active_source);
	sh->active_source = active_src;

	if (sh->op_failed) {
		afr_sh_entry_finish (frame, this);
		return 0;
	}

	if (active_src == -1) {
		/* completed creating missing files on all subvolumes */
		afr_sh_entry_expunge_all (frame, this);
		return 0;
	}

	gf_log (this->name, GF_LOG_WARNING,
		"impunging entries of %s on %s to other sinks",
		local->loc.path, priv->children[active_src]->name);

	afr_sh_entry_impunge_subvol (frame, this, active_src);

	return 0;
}


int
afr_sh_entry_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			  int32_t op_ret, int32_t op_errno, fd_t *fd)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              call_count = 0;
	int              child_index = 0;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	child_index = (long) cookie;

	/* TODO: some of the open's might fail.
	   In that case, modify cleanup fn to send flush on those 
	   fd's which are already open */

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;
		
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"opendir of %s failed on child %s (%s)",
				local->loc.path,
				priv->children[child_index]->name,
				strerror (op_errno));
			sh->op_failed = 1;
		}

	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		if (sh->op_failed) {
			afr_sh_entry_finish (frame, this);
			return 0;
		}
		gf_log (this->name, GF_LOG_DEBUG,
			"fd for %s opened, commencing sync",
			local->loc.path);

		sh->active_source = -1;
		afr_sh_entry_impunge_all (frame, this);
	}

	return 0;
}


int
afr_sh_entry_open (call_frame_t *frame, xlator_t *this)
{
	int i = 0;				
	int call_count = 0;		     

	int source = -1;
	int *sources = NULL;

	fd_t *fd = NULL;

	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
	afr_self_heal_t *sh = NULL;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	source  = local->self_heal.source;
	sources = local->self_heal.sources;

	sh->block_size = 131072;
	sh->offset = 0;

	call_count = sh->active_sinks;
	if (source != -1)
		call_count++;

	local->call_count = call_count;

	fd = fd_create (local->loc.inode, frame->root->pid);
	sh->healing_fd = fd;

	if (source != -1) {
		gf_log (this->name, GF_LOG_DEBUG,
			"opening directory %s on subvolume %s (source)",
			local->loc.path, priv->children[source]->name);

		/* open source */
		STACK_WIND_COOKIE (frame, afr_sh_entry_opendir_cbk,
				   (void *) (long) source,
				   priv->children[source],
				   priv->children[source]->fops->opendir,
				   &local->loc, fd);
		call_count--;
	}

	/* open sinks */
	for (i = 0; i < priv->child_count; i++) {
		if (sources[i] || !local->child_up[i])
			continue;

		gf_log (this->name, GF_LOG_DEBUG,
			"opening directory %s on subvolume %s (sink)",
			local->loc.path, priv->children[i]->name);

		STACK_WIND_COOKIE (frame, afr_sh_entry_opendir_cbk,
				   (void *) (long) i,
				   priv->children[i], 
				   priv->children[i]->fops->opendir,
				   &local->loc, fd);

		if (!--call_count)
			break;
	}

	return 0;
}


int
afr_sh_entry_sync_prepare (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              active_sinks = 0;
	int              source = 0;
	int              i = 0;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	source = sh->source;

	for (i = 0; i < priv->child_count; i++) {
		if (sh->sources[i] == 0 && local->child_up[i] == 1) {
			active_sinks++;
			sh->success[i] = 1;
		}
	}
	if (source != -1)
		sh->success[source] = 1;

	if (active_sinks == 0) {
		gf_log (this->name, GF_LOG_DEBUG,
			"no active sinks for self-heal on dir %s",
			local->loc.path);
		afr_sh_entry_finish (frame, this);
		return 0;
	}
	if (source == -1 && active_sinks < 2) {
		gf_log (this->name, GF_LOG_WARNING,
			"cannot sync with 0 sources and 1 sink on dir %s",
			local->loc.path);
		afr_sh_entry_finish (frame, this);
		return 0;
	}
	sh->active_sinks = active_sinks;

	if (source != -1)
		gf_log (this->name, GF_LOG_DEBUG,
			"syncing data of %s from subvolume %s to %d active sinks",
			local->loc.path, priv->children[source]->name,
			active_sinks);
	else
		gf_log (this->name, GF_LOG_WARNING,
			"no active sources for %s found. merging all entries as a conservative decision",
			local->loc.path);

	afr_sh_entry_open (frame, this);

	return 0;
}


int
afr_sh_entry_fix (call_frame_t *frame, xlator_t *this)
{
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;
	afr_private_t   *priv = NULL;
	int              source = 0;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	afr_sh_build_pending_matrix (sh->pending_matrix, sh->xattr, 
				     priv->child_count, AFR_ENTRY_PENDING);

	afr_sh_print_pending_matrix (sh->pending_matrix, this);


	afr_sh_mark_sources (sh->pending_matrix, sh->sources, 
			     priv->child_count);

	afr_sh_supress_errenous_children (sh->sources, sh->child_errno,
					  priv->child_count);

	source = afr_sh_select_source (sh->sources, priv->child_count);
	sh->source = source;

	afr_sh_entry_sync_prepare (frame, this);

	return 0;
}



int
afr_sh_entry_lookup_cbk (call_frame_t *frame, void *cookie,
			 xlator_t *this, int32_t op_ret, int32_t op_errno,
			 inode_t *inode, struct stat *buf, dict_t *xattr)
{
	afr_private_t   *priv  = NULL;
	afr_local_t     *local = NULL;
	afr_self_heal_t *sh = NULL;

	int call_count  = -1;
	int child_index = (long) cookie;

	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	LOCK (&frame->lock);
	{
		call_count = --local->call_count;

		if (op_ret != -1) {
			sh->xattr[child_index] = dict_ref (xattr);
			sh->buf[child_index] = *buf;
		}
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		afr_sh_entry_fix (frame, this);
	}

	return 0;
}



int
afr_sh_entry_lookup (call_frame_t *frame, xlator_t *this)
{
	afr_self_heal_t * sh    = NULL; 
	afr_local_t    *  local = NULL;
	afr_private_t  *  priv  = NULL;

	int NEED_XATTR_YES = 1;
	int call_count = 0;
	int i = 0;

	priv  = this->private;
	local = frame->local;
	sh    = &local->self_heal;

	call_count = local->child_count;

	local->call_count = call_count;

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			STACK_WIND_COOKIE (frame,
					   afr_sh_entry_lookup_cbk,
					   (void *) (long) i,
					   priv->children[i], 
					   priv->children[i]->fops->lookup,
					   &local->loc, NEED_XATTR_YES);
			if (!--call_count)
				break;
		}
	}

	return 0;
}



int
afr_sh_entry_lock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno)
{
	afr_local_t * local = NULL;
	int           call_count = 0;
	int           child_index = (long) cookie;

	/* TODO: what if lock fails? */
	
	local = frame->local;

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_ERROR, 
				"locking inode of %s on child %d failed: %s",
				local->loc.path, child_index,
				strerror (op_errno));
		} else {
			gf_log (this->name, GF_LOG_DEBUG,
				"inode of %s on child %d locked",
				local->loc.path, child_index);
		}

		call_count = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (call_count == 0) {
		afr_sh_entry_lookup (frame, this);
	}

	return 0;
}


int
afr_sh_entry_lock (call_frame_t *frame, xlator_t *this)
{
	int i = 0;				
	int call_count = 0;		     

	afr_local_t *   local = NULL;
	afr_private_t * priv  = NULL;
	afr_self_heal_t * sh  = NULL;


	local = frame->local;
	sh = &local->self_heal;
	priv = this->private;

	call_count = local->child_count;

	local->call_count = call_count;		

	for (i = 0; i < priv->child_count; i++) {
		if (local->child_up[i]) {
			gf_log (this->name, GF_LOG_DEBUG,
				"locking %s on subvolume %s",
				local->loc.path, priv->children[i]->name);

			STACK_WIND_COOKIE (frame, afr_sh_entry_lock_cbk,
					   (void *) (long) i,
					   priv->children[i], 
					   priv->children[i]->fops->entrylk,
					   &local->loc, NULL,
					   GF_DIR_LK_LOCK, GF_DIR_LK_WRLCK);
			if (!--call_count)
				break;
		}
	}

	return 0;
}


int
afr_self_heal_entry (call_frame_t *frame, xlator_t *this)
{
	afr_local_t   *local = NULL;
	afr_self_heal_t *sh = NULL;


	local = frame->local;
	sh = &local->self_heal;

	if (local->need_entry_self_heal) {
		afr_sh_entry_lock (frame, this);
	} else {
		gf_log (this->name, GF_LOG_DEBUG,
			"proceeding to completion on %s",
			local->loc.path);
		afr_sh_entry_done (frame, this);
	}

	return 0;
}

