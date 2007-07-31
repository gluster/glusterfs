/*
   Copyright (c) 2007 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _FUSE_EXTRA_H
#define _FUSE_EXTRA_H

#include <stdlib.h>
#include <fuse/fuse_lowlevel.h>

uint64_t req_callid (fuse_req_t req);

int32_t
fuse_reply_vec (fuse_req_t req,
		struct iovec *vector,
		int32_t count);

#endif /* _FUSE_EXTRA_H */
