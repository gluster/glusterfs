/*
   Copyright IBM, Corp. 2013

   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _BD_AIO_H
#define _BD_AIO_H

#include "xlator.h"
#include "glusterfs.h"

/*
 * Maximum number of concurrently submitted IO events. The heaviest load
 * GlusterFS has been able to handle had 60-80 concurrent calls
 */
#define BD_AIO_MAX_NR_EVENTS 256

/* Maximum number of completed IO operations to reap per getevents syscall */
#define BD_AIO_MAX_NR_GETEVENTS 16

int bd_aio_on (xlator_t *this);
int bd_aio_off (xlator_t *this);

int bd_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                      off_t offset, uint32_t flags, dict_t *xdata);

int bd_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
                       struct iovec *vector, int32_t count, off_t offset,
                       uint32_t flags, struct iobref *iobref, dict_t *xdata);

#endif /* !_BD_AIO_H */
