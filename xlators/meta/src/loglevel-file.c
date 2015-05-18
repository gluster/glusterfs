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
loglevel_file_fill (xlator_t *this, inode_t *file, strfd_t *strfd)
{
	strprintf (strfd, "%d\n", this->ctx->log.loglevel);

	return strfd->size;
}


static int
loglevel_file_write (xlator_t *this, fd_t *fd, struct iovec *iov, int count)
{
	long int level = -1;

	level = strtol (iov[0].iov_base, NULL, 0);
	if (level >= GF_LOG_NONE && level <= GF_LOG_TRACE)
		gf_log_set_loglevel (level);

	return iov_length (iov, count);
}


static struct meta_ops loglevel_file_ops = {
	.file_fill = loglevel_file_fill,
	.file_write = loglevel_file_write,
};


int
meta_loglevel_file_hook (call_frame_t *frame, xlator_t *this, loc_t *loc,
			 dict_t *xdata)
{
	meta_ops_set (loc->inode, this, &loglevel_file_ops);

	return 0;
}
