/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "xlator.h"
#include "defaults.h"

#include "meta-mem-types.h"
#include "meta.h"

#include "meta-hooks.h"


int
meta_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
	inode_t *inode = NULL;

	if (META_HOOK (loc) || IS_META_ROOT_GFID (loc->gfid)) {
		struct iatt iatt = { };
		struct iatt parent = { };

		meta_root_dir_hook (frame, this, loc, xdata);

		meta_iatt_fill (&iatt, loc->inode, IA_IFDIR);
		gf_uuid_parse (META_ROOT_GFID, iatt.ia_gfid);

		META_STACK_UNWIND (lookup, frame, 0, 0, loc->inode, &iatt,
				   xdata, &parent);
		return 0;
	}

	if (loc->parent)
		inode = loc->parent;
	else
		inode = loc->inode;

	META_FOP (inode, lookup, frame, this, loc, xdata);

	return 0;
}


int
meta_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
	      dict_t *xdata)
{
	META_FOP (fd->inode, opendir, frame, this, loc, fd, xdata);

	return 0;
}


int
meta_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags, fd_t *fd,
	   dict_t *xdata)
{
	META_FOP (fd->inode, open, frame, this, loc, flags, fd, xdata);

	return 0;
}


int
meta_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
	    off_t offset, uint32_t flags, dict_t *xdata)
{
	META_FOP (fd->inode, readv, frame, this, fd, size, offset, flags, xdata);

	return 0;
}


int
meta_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
	META_FOP (fd->inode, flush, frame, this, fd, xdata);

	return 0;
}


int
meta_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
	META_FOP (loc->inode, stat, frame, this, loc, xdata);

	return 0;
}


int
meta_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
	META_FOP (fd->inode, fstat, frame, this, fd, xdata);

	return 0;
}


int
meta_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
	      off_t offset, dict_t *xdata)
{
	META_FOP (fd->inode, readdir, frame, this, fd, size, offset, xdata);

	return 0;
}


int
meta_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
	       off_t offset, dict_t *xdata)
{
	META_FOP (fd->inode, readdirp, frame, this, fd, size, offset, xdata);

	return 0;
}


int
meta_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
	       dict_t *xdata)
{
	META_FOP (loc->inode, readlink, frame, this, loc, size, xdata);

	return 0;
}


int
meta_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *iov,
	     int count, off_t offset, uint32_t flags, struct iobref *iobref,
	     dict_t *xdata)
{
	META_FOP (fd->inode, writev, frame, this, fd, iov, count, offset, flags,
		  iobref, xdata);
	return 0;
}


int
meta_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
	       dict_t *xdata)
{
	META_FOP (loc->inode, truncate, frame, this, loc, offset, xdata);

	return 0;
}


int
meta_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
		dict_t *xdata)
{
	META_FOP (fd->inode, ftruncate, frame, this, fd, offset, xdata);

	return 0;
}

int32_t
meta_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
            dict_t *xdata)
{
	META_FOP (fd->inode, fsync, frame, this, fd, flags, xdata);

        return 0;
}

int32_t
meta_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
               dict_t *xdata)
{
	META_FOP (fd->inode, fsyncdir, frame, this, fd, flags, xdata);

        return 0;
}

int
meta_forget (xlator_t *this, inode_t *inode)
{
	return 0;
}


int
meta_release (xlator_t *this, fd_t *fd)
{
	return meta_fd_release (fd, this);
}


int
meta_releasedir (xlator_t *this, fd_t *fd)
{
	return meta_fd_release (fd, this);
}


int
mem_acct_init (xlator_t *this)
{
        int ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_meta_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR,
			"Memory accounting init failed");
                return ret;
        }

        return ret;
}


int
init (xlator_t *this)
{
	meta_priv_t *priv = NULL;

	priv = GF_CALLOC (sizeof(*priv), 1, gf_meta_mt_priv_t);
	if (!priv)
		return -1;

	GF_OPTION_INIT ("meta-dir-name", priv->meta_dir_name, str, out);

	this->private = priv;
out:
	return 0;
}


int
fini (xlator_t *this)
{
	return 0;
}


struct xlator_fops fops = {
	.lookup    = meta_lookup,
	.opendir   = meta_opendir,
	.open      = meta_open,
	.readv     = meta_readv,
	.flush     = meta_flush,
	.stat      = meta_stat,
	.fstat     = meta_fstat,
	.readdir   = meta_readdir,
	.readdirp  = meta_readdirp,
	.readlink  = meta_readlink,
	.writev    = meta_writev,
	.truncate  = meta_truncate,
	.ftruncate = meta_ftruncate,
        .fsync     = meta_fsync,
        .fsyncdir  = meta_fsyncdir
};


struct xlator_cbks cbks = {
	.forget = meta_forget,
	.release = meta_release,
	.releasedir = meta_releasedir,
};


struct volume_options options[] = {
	{ .key = {"meta-dir-name"},
	  .type = GF_OPTION_TYPE_STR,
	  .default_value = DEFAULT_META_DIR_NAME,
	  .description = "Name of default meta directory."
	},
	{ .key = {NULL} },
};
