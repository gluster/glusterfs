/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __QB_COROUTINES_H
#define __QB_COROUTINES_H

#include "syncop.h"
#include "call-stub.h"
#include "block/block_int.h"
#include "monitor/monitor.h"

int qb_format_and_resume (void *opaque);
int qb_snapshot_create (void *opaque);
int qb_snapshot_delete (void *opaque);
int qb_snapshot_goto (void *opaque);
int qb_co_open (void *opaque);
int qb_co_close (void *opaque);
int qb_co_writev (void *opaque);
int qb_co_readv (void *opaque);
int qb_co_fsync (void *opaque);
int qb_co_truncate (void *opaque);

#endif /* __QB_COROUTINES_H */
