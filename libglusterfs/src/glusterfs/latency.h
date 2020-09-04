/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __LATENCY_H__
#define __LATENCY_H__

#include <inttypes.h>
#include <time.h>

typedef struct _gf_latency {
    uint64_t min;   /* min time for the call (nanoseconds) */
    uint64_t max;   /* max time for the call (nanoseconds) */
    uint64_t total; /* total time (nanoseconds) */
    uint64_t count;
} gf_latency_t;

gf_latency_t *
gf_latency_new(size_t n);

void
gf_latency_reset(gf_latency_t *lat);

void
gf_latency_update(gf_latency_t *lat, struct timespec *begin,
                  struct timespec *end);
#endif /* __LATENCY_H__ */
