/*
  Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _UTIME_HELPERS_H
#define _UTIME_HELPERS_H

#include "glusterfs-fops.h"
#include "stack.h"
#include "timespec.h"
#include <time.h>

void
gl_timespec_get (struct timespec *ts);
void
utime_update_attribute_flags(call_frame_t *frame, glusterfs_fop_t fop);

#endif /* _UTIME_HELPERS_H */
