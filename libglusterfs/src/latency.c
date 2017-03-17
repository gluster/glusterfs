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
#include "stack.h"
#include "xlator.h"
#include "common-utils.h"
#include "statedump.h"
#include "libglusterfs-messages.h"

void
gf_update_latency (void *fr)
{
        double elapsed;
        struct timespec *begin, *end;

        fop_latency_t *lat;
        call_frame_t *frame = fr;

        begin = &frame->begin;
        end   = &frame->end;

        if (!(begin->tv_sec && end->tv_sec))
                goto out;

        elapsed = (end->tv_sec - begin->tv_sec) * 1e9
                + (end->tv_nsec - begin->tv_nsec);

        lat = &frame->this->latencies[frame->op];

        lat->total += elapsed;
        lat->count++;
        //lat->mean = lat->mean + ((elapsed - lat->mean) / lat->count);
        lat->mean = lat->total / lat->count;
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

                gf_proc_dump_write (key, "%.03f,%"PRId64",%.03f",
                                    xl->latencies[i].mean,
                                    xl->latencies[i].count,
                                    xl->latencies[i].total);
        }

        memset (xl->latencies, 0, sizeof (xl->latencies));
}


void
gf_latency_toggle (int signum, glusterfs_ctx_t *ctx)
{
        if (ctx) {
                ctx->measure_latency = !ctx->measure_latency;
                gf_msg ("[core]", GF_LOG_INFO, 0,
                        LG_MSG_LATENCY_MEASUREMENT_STATE,
                        "Latency measurement turned %s",
                        ctx->measure_latency ? "on" : "off");
        }
}
