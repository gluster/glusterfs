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


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "xlator.h"
#include "nufa.h"
#include "defaults.h"


int
nufa_rename_dir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		    int32_t op_ret, int32_t op_errno, struct stat *stbuf)
{
	nufa_local_t  *local = NULL;
	int           this_call_cnt = 0;
	call_frame_t *prev = NULL;


	local = frame->local;
	prev = cookie;

	if (op_ret == -1) {
		/* TODO: undo the damage */

		gf_log (this->name, GF_LOG_ERROR,
			"rename %s -> %s on %s failed (%s)",
			local->loc.path, local->loc2.path,
			prev->this->name, strerror (op_errno));

		local->op_ret   = op_ret;
		local->op_errno = op_errno;
	} else {
		/* TODO: construct proper stbuf for dir */
		local->stbuf = *stbuf;
	}

	this_call_cnt = nufa_frame_return (frame);
	if (is_last_call (this_call_cnt)) {
		DHT_STACK_UNWIND (frame, local->op_ret, local->op_errno,
				  &local->stbuf);
	}

	return 0;
}



int
nufa_rename_dir_do (call_frame_t *frame, xlator_t *this)
{
	nufa_local_t  *local = NULL;
	nufa_conf_t   *conf = NULL;
	int           i = 0;

	conf = this->private;
	local = frame->local;

	if (local->op_ret == -1)
		goto err;

	local->call_cnt = conf->subvolume_cnt;
	local->op_ret = 0;

	for (i = 0; i < conf->subvolume_cnt; i++) {
		STACK_WIND (frame, nufa_rename_dir_cbk,
			    conf->subvolumes[i],
			    conf->subvolumes[i]->fops->rename,
			    &local->loc, &local->loc2);
	}

	return 0;

err:
	DHT_STACK_UNWIND (frame, local->op_ret, local->op_errno);
	return 0;
}


int
nufa_rename_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int op_ret, int op_errno, gf_dirent_t *entries)
{
	nufa_local_t  *local = NULL;
	int           this_call_cnt = -1;
	call_frame_t *prev = NULL;

	local = frame->local;
	prev  = cookie;

	if (op_ret > 2) {
		gf_log (this->name, GF_LOG_DEBUG,
			"readdir on %s for %s returned %d entries",
			prev->this->name, local->loc.path, op_ret);
		local->op_ret = -1;
		local->op_errno = ENOTEMPTY;
	}

	this_call_cnt = nufa_frame_return (frame);

	if (is_last_call (this_call_cnt)) {
		nufa_rename_dir_do (frame, this);
	}

	return 0;
}


int
nufa_rename_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int op_ret, int op_errno, fd_t *fd)
{
	nufa_local_t  *local = NULL;
	int           this_call_cnt = -1;
	call_frame_t *prev = NULL;


	local = frame->local;
	prev  = cookie;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_ERROR,
			"opendir on %s for %s failed (%s)",
			prev->this->name, local->loc.path,
			strerror (op_errno));
		goto err;
	}

	STACK_WIND (frame, nufa_rename_readdir_cbk,
		    prev->this, prev->this->fops->readdir,
		    local->fd, 4096, 0);

	return 0;

err:
	this_call_cnt = nufa_frame_return (frame);

	if (is_last_call (this_call_cnt)) {
		nufa_rename_dir_do (frame, this);
	}

	return 0;
}


int
nufa_rename_dir (call_frame_t *frame, xlator_t *this)
{
	nufa_conf_t  *conf = NULL;
	nufa_local_t *local = NULL;
	int          i = 0;
	int          op_errno = -1;


	conf = frame->this->private;
	local = frame->local;

	local->call_cnt = conf->subvolume_cnt;

	local->fd = fd_create (local->loc.inode, frame->root->pid);
	if (!local->fd) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		op_errno = ENOMEM;
		goto err;
	}

	local->op_ret = 0;

	if (!local->dst_cached) {
		nufa_rename_dir_do (frame, this);
		return 0;
	}

	for (i = 0; i < conf->subvolume_cnt; i++) {
		STACK_WIND (frame, nufa_rename_opendir_cbk,
			    conf->subvolumes[i],
			    conf->subvolumes[i]->fops->opendir,
			    &local->loc2, local->fd);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL);
	return 0;
}


int
nufa_rename_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int32_t op_ret, int32_t op_errno)
{
	nufa_local_t *local = NULL;
	int          this_call_cnt = 0;

	local = frame->local;

	this_call_cnt = nufa_frame_return (frame);

	if (is_last_call (this_call_cnt))
		DHT_STACK_UNWIND (frame, local->op_ret, local->op_errno,
				  &local->stbuf);

	return 0;
}


int
nufa_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int32_t op_ret, int32_t op_errno, struct stat *stbuf)
{
	nufa_local_t  *local = NULL;
	call_frame_t *prev = NULL;
	xlator_t     *src_hashed = NULL;
	xlator_t     *src_cached = NULL;
	xlator_t     *dst_hashed = NULL;
	xlator_t     *dst_cached = NULL;


	local = frame->local;
	prev = cookie;

	src_hashed = local->src_hashed;
	src_cached = local->src_cached;
	dst_hashed = local->dst_hashed;
	dst_cached = local->dst_cached;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_DEBUG,
			"rename on %s failed (%s)", prev->this->name,
			strerror (op_errno));
		local->op_ret   = op_ret;
		local->op_errno = op_errno;
		goto unwind;
	}

	/* TODO: delete files in background */

	if (src_cached != dst_hashed && src_cached != dst_cached)
		local->call_cnt++;

	if (src_cached != src_hashed)
		local->call_cnt++;

	if (dst_cached && dst_cached != dst_hashed && dst_cached != src_cached)
		local->call_cnt++;

	if (local->call_cnt == 0)
		goto unwind;

	if (src_cached != dst_hashed && src_cached != dst_cached) {
		gf_log (this->name, GF_LOG_DEBUG,
			"deleting old src datafile %s @ %s",
			local->loc.path, src_cached->name);

		STACK_WIND (frame, nufa_rename_unlink_cbk,
			    src_cached, src_cached->fops->unlink,
			    &local->loc);
	}

	if (src_cached != src_hashed) {
		gf_log (this->name, GF_LOG_DEBUG,
			"deleting old src linkfile %s @ %s",
			local->loc.path, src_hashed->name);

		STACK_WIND (frame, nufa_rename_unlink_cbk,
			    src_hashed, src_hashed->fops->unlink,
			    &local->loc);
	}

	if (dst_cached
	    && (dst_cached != dst_hashed)
	    && (dst_cached != src_cached)) {
		gf_log (this->name, GF_LOG_DEBUG,
			"deleting old dst datafile %s @ %s",
			local->loc2.path, dst_cached->name);

		STACK_WIND (frame, nufa_rename_unlink_cbk,
			    dst_cached, dst_cached->fops->unlink,
			    &local->loc2);
	}
	return 0;

unwind:
	DHT_STACK_UNWIND (frame, local->op_ret, local->op_errno,
			  &local->stbuf);

	return 0;
}


int
nufa_do_rename (call_frame_t *frame)
{
	nufa_local_t *local = NULL;
	xlator_t    *dst_hashed = NULL;
	xlator_t    *src_cached = NULL;
	xlator_t    *dst_cached = NULL;
	xlator_t    *this = NULL;
	xlator_t    *rename_subvol = NULL;


	local = frame->local;
	this  = frame->this;

	dst_hashed = local->dst_hashed;
	dst_cached = local->dst_cached;
	src_cached = local->src_cached;

	if (src_cached == dst_cached)
		rename_subvol = src_cached;
	else
		rename_subvol = dst_hashed;

	gf_log (this->name, GF_LOG_DEBUG,
		"renaming %s => %s (%s)",
		local->loc.path, local->loc2.path, rename_subvol->name);

	STACK_WIND (frame, nufa_rename_cbk,
		    rename_subvol, rename_subvol->fops->rename,
		    &local->loc, &local->loc2);

	return 0;
}


int
nufa_rename_links_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		      int32_t op_ret, int32_t op_errno,
		      inode_t *inode, struct stat *stbuf)
{
	nufa_local_t  *local = NULL;
	call_frame_t *prev = NULL;
	int           this_call_cnt  = 0;


	local = frame->local;
	prev = cookie;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_DEBUG,
			"link/file on %s failed (%s)",
			prev->this->name, strerror (op_errno));
		local->op_ret   = -1;
		local->op_errno = op_errno;
	}

	this_call_cnt = nufa_frame_return (frame);
	if (is_last_call (this_call_cnt)) {
		if (local->op_ret == -1)
			goto unwind;

		nufa_do_rename (frame);
	}

	return 0;

unwind:
	DHT_STACK_UNWIND (frame, local->op_ret, local->op_errno,
			  &local->stbuf);

	return 0;
}


int
nufa_rename_create_links (call_frame_t *frame)
{
	nufa_local_t *local = NULL;
	xlator_t    *this = NULL;
	xlator_t    *src_hashed = NULL;
	xlator_t    *src_cached = NULL;
	xlator_t    *dst_hashed = NULL;
	xlator_t    *dst_cached = NULL;
	int          call_cnt = 0;


	local = frame->local;
	this  = frame->this;

	src_hashed = local->src_hashed;
	src_cached = local->src_cached;
	dst_hashed = local->dst_hashed;
	dst_cached = local->dst_cached;

	if (src_cached == dst_cached)
		goto nolinks;

	if (dst_hashed != src_hashed && dst_hashed != src_cached)
		call_cnt++;

	if (src_cached != dst_hashed)
		call_cnt++;

	local->call_cnt = call_cnt;

	if (dst_hashed != src_hashed && dst_hashed != src_cached) {
		gf_log (this->name, GF_LOG_DEBUG,
			"linkfile %s @ %s => %s",
			local->loc.path, dst_hashed->name, src_cached->name);
		nufa_linkfile_create (frame, nufa_rename_links_cbk,
				     src_cached, dst_hashed, &local->loc);
	}

	if (src_cached != dst_hashed) {
		gf_log (this->name, GF_LOG_DEBUG,
			"link %s => %s (%s)", local->loc.path,
			local->loc2.path, src_cached->name);
		STACK_WIND (frame, nufa_rename_links_cbk,
			    src_cached, src_cached->fops->link,
			    &local->loc, &local->loc2);
	}

nolinks:
	if (!call_cnt) {
		/* skip to next step */
		nufa_do_rename (frame);
	}

	return 0;
}


int
nufa_rename (call_frame_t *frame, xlator_t *this,
	    loc_t *oldloc, loc_t *newloc)
{
	xlator_t    *src_cached = NULL;
	xlator_t    *src_hashed = NULL;
	xlator_t    *dst_cached = NULL;
	xlator_t    *dst_hashed = NULL;
	int          op_errno = -1;
	int          ret = -1;
	nufa_local_t *local = NULL;


	VALIDATE_OR_GOTO (frame, err);
	VALIDATE_OR_GOTO (this, err);
	VALIDATE_OR_GOTO (oldloc, err);
	VALIDATE_OR_GOTO (newloc, err);

	src_hashed = nufa_subvol_get_hashed (this, oldloc);
	if (!src_hashed) {
		gf_log (this->name, GF_LOG_ERROR,
			"no subvolume in layout for path=%s",
			oldloc->path);
		op_errno = EINVAL;
		goto err;
	}

	src_cached = nufa_subvol_get_cached (this, oldloc->inode);
	if (!src_cached) {
		gf_log (this->name, GF_LOG_ERROR,
			"no cached subvolume for path=%s", oldloc->path);
		op_errno = EINVAL;
		goto err;
	}

	dst_hashed = nufa_subvol_get_hashed (this, newloc);
	if (!dst_hashed) {
		gf_log (this->name, GF_LOG_ERROR,
			"no subvolume in layout for path=%s",
			newloc->path);
		op_errno = EINVAL;
		goto err;
	}

	if (newloc->inode)
		dst_cached = nufa_subvol_get_cached (this, newloc->inode);

	local = nufa_local_init (frame);
	if (!local) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	ret = loc_copy (&local->loc, oldloc);
	if (ret == -1) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	ret = loc_copy (&local->loc2, newloc);
	if (ret == -1) {
		op_errno = ENOMEM;
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	local->src_hashed = src_hashed;
	local->src_cached = src_cached;
	local->dst_hashed = dst_hashed;
	local->dst_cached = dst_cached;

	gf_log (this->name, GF_LOG_DEBUG,
		"renaming %s (hash=%s/cache=%s) => %s (hash=%s/cache=%s)",
		oldloc->path, src_hashed->name, src_cached->name,
		newloc->path, dst_hashed->name,
		dst_cached ? dst_cached->name : "<nul>");

	if (S_ISDIR (oldloc->inode->st_mode)) {
		nufa_rename_dir (frame, this);
	} else {
		local->op_ret = 0;
		nufa_rename_create_links (frame);
	}

	return 0;

err:
	op_errno = (op_errno == -1) ? errno : op_errno;
	DHT_STACK_UNWIND (frame, -1, op_errno, NULL, NULL);

	return 0;
}
