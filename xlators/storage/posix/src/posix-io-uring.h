/*
   Copyright (c) 2020 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _POSIX_IO_URING_H
#define _POSIX_IO_URING_H

#define POSIX_URING_MAX_ENTRIES 512
int
posix_io_uring_on(xlator_t *this);

int
posix_io_uring_off(xlator_t *this);

#ifdef HAVE_LIBURING
int
posix_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
            off_t offset, uint32_t flags, dict_t *xdata);

int
posix_writev(call_frame_t *frame, xlator_t *this, fd_t *fd,
             struct iovec *vector, int32_t count, off_t offset, uint32_t flags,
             struct iobref *iobref, dict_t *xdata);
#endif

#endif /* _POSIX_IO_URING_H */
