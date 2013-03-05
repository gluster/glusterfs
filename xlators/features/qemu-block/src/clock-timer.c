/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
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

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "syncop.h"
#include "qemu-block-memory-types.h"

#include "qemu/timer.h"

QEMUClock *vm_clock;
int use_rt_clock = 0;

QEMUTimer *qemu_new_timer (QEMUClock *clock, int scale,
			   QEMUTimerCB *cb, void *opaque)
{
	return NULL;
}

int64_t qemu_get_clock_ns (QEMUClock *clock)
{
	return 0;
}

void qemu_mod_timer (QEMUTimer *ts, int64_t expire_time)
{
	return;
}

void qemu_free_timer (QEMUTimer *ts)
{

}

void qemu_del_timer (QEMUTimer *ts)
{

}

bool qemu_aio_wait()
{
	synctask_wake (synctask_get());
	synctask_yield (synctask_get());
	return 0;
}
