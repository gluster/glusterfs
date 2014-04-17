/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __INCLUDE_TIMESPEC_H__
#define __INCLUDE_TIMESPEC_H__

#include <stdint.h>
#include <sys/time.h>

#define TS(ts)  ((ts.tv_sec * 1000000000LL) + ts.tv_nsec)
#define NANO (+1.0E-9)
#define GIGA UINT64_C(1000000000)

void timespec_now (struct timespec *ts);
void timespec_adjust_delta (struct timespec *ts, struct timespec delta);

#endif /*  __INCLUDE_TIMESPEC_H__ */
