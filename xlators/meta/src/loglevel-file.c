/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "defaults.h"

#include "meta-mem-types.h"
#include "meta.h"
#include "strfd.h"


static int
loglevel_file_fill (xlator_t *this, inode_t *file, strfd_t *strfd)
{
	strprintf (strfd, "%d\n", this->ctx->log.loglevel);

	return strfd->size;
}


static int
loglevel_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
		 struct iovec *iov, int count, off_t offset,
		 uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
	struct iatt dummy = { };
	long int level = -1;

	level = strtol (iov[0].iov_base, NULL, 0);
	if (level >= GF_LOG_NONE && level <= GF_LOG_TRACE)
		gf_log_set_loglevel (level);

	META_STACK_UNWIND (writev, frame, iov_length (iov, count), 0,
			   &dummy, &dummy, xdata);
	return 0;
}


int
loglevel_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc,
		   off_t offset, dict_t *xdata)
{
	struct iatt iatt = { };

	meta_iatt_fill (&iatt, loc->inode, IA_IFREG);

	META_STACK_UNWIND (truncate, frame, 0, 0, &iatt, &iatt, xdata);
	return 0;
}


int
loglevel_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd,
		   off_t offset, dict_t *xdata)
{
	struct iatt iatt = { };

	meta_iatt_fill (&iatt, fd->inode, IA_IFREG);

	META_STACK_UNWIND (ftruncate, frame, 0, 0, &iatt, &iatt, xdata);
	return 0;
}

static struct meta_ops loglevel_file_ops = {
	.file_fill = loglevel_file_fill,
	.fops = {
		.truncate = loglevel_truncate,
		.ftruncate = loglevel_ftruncate,
		.writev = loglevel_writev
	}
};


int
meta_loglevel_file_hook (call_frame_t *frame, xlator_t *this, loc_t *loc,
			 dict_t *xdata)
{
	meta_ops_set (loc->inode, this, &loglevel_file_ops);

	return 0;
}
