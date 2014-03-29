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
measure_file_fill (xlator_t *this, inode_t *file, strfd_t *strfd)
{
	strprintf (strfd, "%d\n", this->ctx->measure_latency);

	return strfd->size;
}


static int
measure_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
		struct iovec *iov, int count, off_t offset,
		uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
	struct iatt dummy = { };
	long int num = -1;

	num = strtol (iov[0].iov_base, NULL, 0);
	this->ctx->measure_latency = !!num;

	META_STACK_UNWIND (writev, frame, iov_length (iov, count), 0,
			   &dummy, &dummy, xdata);
	return 0;
}


int
measure_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc,
		   off_t offset, dict_t *xdata)
{
	struct iatt iatt = { };

	meta_iatt_fill (&iatt, loc->inode, IA_IFREG);

	META_STACK_UNWIND (truncate, frame, 0, 0, &iatt, &iatt, xdata);
	return 0;
}


int
measure_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd,
		   off_t offset, dict_t *xdata)
{
	struct iatt iatt = { };

	meta_iatt_fill (&iatt, fd->inode, IA_IFREG);

	META_STACK_UNWIND (ftruncate, frame, 0, 0, &iatt, &iatt, xdata);
	return 0;
}

static struct meta_ops measure_file_ops = {
	.file_fill = measure_file_fill,
	.fops = {
		.truncate = measure_truncate,
		.ftruncate = measure_ftruncate,
		.writev = measure_writev
	}
};


int
meta_measure_file_hook (call_frame_t *frame, xlator_t *this, loc_t *loc,
			dict_t *xdata)
{
	meta_ops_set (loc->inode, this, &measure_file_ops);

	return 0;
}
