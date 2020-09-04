/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/*
 * This file contains functions to support dumping of
 * latencies of FOPs broken down by subvolumes.
 */

#include "glusterfs/glusterfs.h"
#include "glusterfs/statedump.h"

gf_latency_t *
gf_latency_new(size_t n)
{
    int i = 0;
    gf_latency_t *lat = NULL;

    lat = GF_MALLOC(n * sizeof(*lat), gf_common_mt_latency_t);
    if (!lat)
        return NULL;

    for (i = 0; i < n; i++) {
        gf_latency_reset(lat + i);
    }
    return lat;
}

void
gf_latency_update(gf_latency_t *lat, struct timespec *begin,
                  struct timespec *end)
{
    if (!(begin->tv_sec && end->tv_sec)) {
        /*Measure latency might have been enabled/disabled during the op*/
        return;
    }

    double elapsed = gf_tsdiff(begin, end);

    if (lat->max < elapsed)
        lat->max = elapsed;

    if (lat->min > elapsed)
        lat->min = elapsed;

    lat->total += elapsed;
    lat->count++;
}

void
gf_latency_reset(gf_latency_t *lat)
{
    if (!lat)
        return;
    memset(lat, 0, sizeof(*lat));
    lat->min = ULLONG_MAX;
    /* make sure 'min' is set to high value, so it would be
       properly set later */
}

void
gf_frame_latency_update(call_frame_t *frame)
{
    gf_latency_t *lat;
    /* Can happen mostly at initiator xlator, as STACK_WIND/UNWIND macros
       set it right anyways for those frames */
    if (!frame->op)
        frame->op = frame->root->op;

    if (frame->op < 0 || frame->op >= GF_FOP_MAXVALUE) {
        gf_log("[core]", GF_LOG_WARNING, "Invalid frame op value: %d",
               frame->op);
        return;
    }

    lat = &frame->this->stats.interval.latencies[frame->op];
    gf_latency_update(lat, &frame->begin, &frame->end);
}
