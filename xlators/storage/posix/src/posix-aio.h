/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
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

#ifndef _POSIX_AIO_H
#define _POSIX_AIO_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "glusterfs.h"

// Maximum number of concurrently submitted IO events. The heaviest load
// GlusterFS has been able to handle had 60-80 concurrent calls
#define POSIX_AIO_MAX_NR_EVENTS 256

// Maximum number of completed IO operations to reap per getevents syscall
#define POSIX_AIO_MAX_NR_GETEVENTS 16


int posix_aio_on (xlator_t *this);
int posix_aio_off (xlator_t *this);

int posix_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
		 off_t offset, uint32_t flags, dict_t *xdata);

int posix_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
		  struct iovec *vector, int32_t count, off_t offset,
		  uint32_t flags, struct iobref *iobref, dict_t *xdata);

#endif /* !_POSIX_AIO_H */
