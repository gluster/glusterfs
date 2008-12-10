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
#include "compat.h"
#include "nufa.h"



int
nufa_linkfile_xattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			int op_ret, int op_errno)
{
	nufa_local_t *local = NULL;


	local = frame->local;
	local->linkfile.linkfile_cbk (frame, cookie, this, op_ret, op_errno,
				      local->linkfile.inode,
				      &local->linkfile.stbuf);

	return 0;
}


int
nufa_linkfile_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			 int op_ret, int op_errno,
			 inode_t *inode, struct stat *stbuf)
{
	nufa_local_t  *local = NULL;
	call_frame_t *prev = NULL;
	dict_t       *xattr = NULL;
	data_t       *str_data = NULL;
	int           ret = -1;

	local = frame->local;
	prev  = cookie;

	if (op_ret == -1)
		goto err;

	xattr = get_new_dict ();
	if (!xattr) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		op_errno = ENOMEM;
		goto err;
	}

	local->linkfile.xattr = dict_ref (xattr);
	local->linkfile.inode = inode_ref (inode);

	str_data = str_to_data (local->linkfile.srcvol->name);
	if (!str_data) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		op_errno = ENOMEM;
		goto err;
	}

	ret = dict_set (xattr, "trusted.glusterfs.dht.linkto", str_data);
	if (ret < 0) {
		gf_log (this->name, GF_LOG_ERROR,
			"failed to initialize linkfile data");
		op_errno = EINVAL;
	}
	str_data = NULL;

	local->linkfile.stbuf = *stbuf;

	STACK_WIND (frame, nufa_linkfile_xattr_cbk,
		    prev->this, prev->this->fops->setxattr,
		    &local->linkfile.loc, local->linkfile.xattr, 0);

	return 0;

err:
	if (str_data) {
		data_destroy (str_data);
		str_data = NULL;
	}

	local->linkfile.linkfile_cbk (frame, cookie, this,
				      op_ret, op_errno, inode, stbuf);
	return 0;
}


int
nufa_linkfile_create (call_frame_t *frame, fop_mknod_cbk_t linkfile_cbk,
		     xlator_t *tovol, xlator_t *fromvol, loc_t *loc)
{
	nufa_local_t *local = NULL;


	local = frame->local;
	local->linkfile.linkfile_cbk = linkfile_cbk;
	local->linkfile.srcvol = tovol;
	loc_copy (&local->linkfile.loc, loc);

	STACK_WIND (frame, nufa_linkfile_create_cbk,
		    fromvol, fromvol->fops->mknod, loc,
		    S_IFREG | NUFA_LINKFILE_MODE, 0);

	return 0;
}

int
nufa_linkfile_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			  int32_t op_ret, int32_t op_errno)
{
	nufa_local_t  *local = NULL;
	call_frame_t  *prev = NULL;
	xlator_t      *subvol = NULL;

	local = frame->local;
	prev = cookie;
	subvol = prev->this;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_WARNING,
			"unlinking linkfile %s on %s failed (%s)",
			local->loc.path, subvol->name, strerror (op_errno));
	}

	NUFA_STACK_DESTROY (frame);

	return 0;
}


int
nufa_linkfile_unlink (call_frame_t *frame, xlator_t *this,
		     xlator_t *subvol, loc_t *loc)
{
	call_frame_t *unlink_frame = NULL;
	nufa_local_t *unlink_local = NULL;
	
	unlink_frame = copy_frame (frame);
	if (!unlink_frame) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}
	
	unlink_local = nufa_local_init (unlink_frame);
	if (!unlink_local) {
		gf_log (this->name, GF_LOG_ERROR,
			"memory allocation failed :(");
		goto err;
	}

	loc_copy (&unlink_local->loc, loc);

	STACK_WIND (unlink_frame, nufa_linkfile_unlink_cbk,
		    subvol, subvol->fops->unlink,
		    &unlink_local->loc);

	return 0;
 err:
	if (unlink_frame)
		NUFA_STACK_DESTROY (unlink_frame);

	return -1;
}

xlator_t *
nufa_linkfile_subvol (xlator_t *this, inode_t *inode, struct stat *stbuf,
		     dict_t *xattr)
{
	nufa_conf_t *conf = NULL;
	xlator_t   *subvol = NULL;
	void       *volname = NULL;
	int         i = 0, ret = 0;

	conf = this->private;

	if (!xattr)
		goto out;

	ret = dict_get_ptr (xattr, "trusted.glusterfs.dht.linkto", &volname);

	if ((-1 == ret) || !volname)
		goto out;

	for (i = 0; i < conf->subvolume_cnt; i++) {
		if (strcmp (conf->subvolumes[i]->name, (char *)volname) == 0) {
			subvol = conf->subvolumes[i];
			break;
		}
	}

out:
	return subvol;
}


