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
        double min;           /* min time for the call (microseconds) */
        double max;           /* max time for the call (microseconds) */
	double total;           /* total time (microseconds) */
        uint64_t count;
} fop_latency_t;

#endif /* __LATENCY_H__ */
