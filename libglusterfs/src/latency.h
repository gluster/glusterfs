/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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

#ifndef __LATENCY_H__
#define __LATENCY_H__


typedef struct fop_latency {
        uint64_t min;           /* min time for the call (microseconds) */
        uint64_t max;           /* max time for the call (microseconds) */
	double total;           /* total time (microseconds) */
        double std;             /* standard deviation */
        double mean;            /* mean (microseconds) */
        uint64_t count;
} fop_latency_t;

void
gf_latency_toggle (int signum);

#endif /* __LATENCY_H__ */
