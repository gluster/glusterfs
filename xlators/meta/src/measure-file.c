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
#include "strfd.h"


static int
measure_file_fill (xlator_t *this, inode_t *file, strfd_t *strfd)
{
	strprintf (strfd, "%d\n", this->ctx->measure_latency);

	return strfd->size;
}


static int
measure_file_write (xlator_t *this, fd_t *fd, struct iovec *iov, int count)
{
	long int num = -1;

	num = strtol (iov[0].iov_base, NULL, 0);
	this->ctx->measure_latency = !!num;

	return iov_length (iov, count);
}

static struct meta_ops measure_file_ops = {
	.file_fill = measure_file_fill,
	.file_write = measure_file_write,
};


int
meta_measure_file_hook (call_frame_t *frame, xlator_t *this, loc_t *loc,
			dict_t *xdata)
{
	meta_ops_set (loc->inode, this, &measure_file_ops);

	return 0;
}
