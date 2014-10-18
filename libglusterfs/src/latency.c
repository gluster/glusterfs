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

#include "glusterfs.h"
#include "xlator.h"
#include "common-utils.h"
#include "statedump.h"
#include "libglusterfs-messages.h"

void
gf_update_latency (call_frame_t *frame)
{
        double elapsed;
        struct timespec *begin, *end;

        fop_latency_t *lat;

        begin = &frame->begin;
        end   = &frame->end;

        if (!(begin->tv_sec && end->tv_sec))
                goto out;

        elapsed = (end->tv_sec - begin->tv_sec) * 1e9
                + (end->tv_nsec - begin->tv_nsec);

        if (frame->op < 0 || frame->op >= GF_FOP_MAXVALUE) {
                gf_log ("[core]", GF_LOG_WARNING,
                        "Invalid frame op value: %d",
                        frame->op);
                return;
        }

        /* Can happen mostly at initiator xlator, as STACK_WIND/UNWIND macros
           set it right anyways for those frames */
        if (!frame->op)
                frame->op = frame->root->op;

        lat = &frame->this->stats.interval.latencies[frame->op];

        if (lat->max < elapsed)
                lat->max = elapsed;

        if (lat->min > elapsed)
                lat->min = elapsed;

        lat->total += elapsed;
        lat->count++;
out:
        return;
}


void
gf_proc_dump_latency_info (xlator_t *xl)
{
        char key_prefix[GF_DUMP_MAX_BUF_LEN];
        char key[GF_DUMP_MAX_BUF_LEN];
        int i;

        snprintf (key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.latency", xl->name);
        gf_proc_dump_add_section (key_prefix);

        for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                gf_proc_dump_build_key (key, key_prefix, "%s",
                                        (char *)gf_fop_list[i]);

                fop_latency_t *lat = &xl->stats.interval.latencies[i];

                /* Doesn't make sense to continue if there are no fops
                   came in the given interval */
                if (!lat->count)
                        continue;

                gf_proc_dump_write (key, "%.03f,%"PRId64",%.03f",
                                    (lat->total / lat->count), lat->count,
                                    lat->total);
        }

        memset (xl->stats.interval.latencies, 0,
                sizeof (xl->stats.interval.latencies));

        /* make sure 'min' is set to high value, so it would be
           properly set later */
        for (i = 0; i < GF_FOP_MAXVALUE; i++) {
                xl->stats.interval.latencies[i].min = 0xffffffff;
        }
}

