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

#include "glusterfs.h"

typedef struct fop_latency {
        uint64_t min;           /* min time for the call (microseconds) */
        uint64_t max;           /* max time for the call (microseconds) */
	double total;           /* total time (microseconds) */
        double std;             /* standard deviation */
        double mean;            /* mean (microseconds) */
        uint64_t count;
} fop_latency_t;

void
gf_latency_toggle (int signum, glusterfs_ctx_t *ctx);

#endif /* __LATENCY_H__ */
