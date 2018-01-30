/*
  Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "monitoring.h"
#include "xlator.h"
#include "syscall.h"

#include <stdlib.h>

static void
dump_mem_acct_details(xlator_t *xl, int fd)
{
        struct mem_acct_rec *mem_rec;
        int i = 0;

        if (!xl || !xl->mem_acct || (xl->ctx->active != xl->graph))
                return;

        dprintf (fd, "# %s.%s.total.num_types %d\n", xl->type, xl->name,
                 xl->mem_acct->num_types);

        dprintf (fd, "# type, in-use-size, in-use-units, max-size, "
                 "max-units, total-allocs\n");

        for (i = 0; i < xl->mem_acct->num_types; i++) {
                mem_rec = &xl->mem_acct->rec[i];
                if (mem_rec->num_allocs == 0)
                        continue;
                dprintf (fd, "# %s, %"GF_PRI_SIZET", %u, %"GF_PRI_SIZET", %u,"
                         " %u\n", mem_rec->typestr, mem_rec->size,
                         mem_rec->num_allocs, mem_rec->max_size,
                         mem_rec->max_num_allocs, mem_rec->total_allocs);
        }
}

static void
dump_global_memory_accounting (int fd)
{
#if MEMORY_ACCOUNTING_STATS
        int      i        = 0;
        uint64_t count    = 0;

        uint64_t tcalloc = GF_ATOMIC_GET (gf_memory_stat_counts.total_calloc);
        uint64_t tmalloc = GF_ATOMIC_GET (gf_memory_stat_counts.total_malloc);
        uint64_t tfree   = GF_ATOMIC_GET (gf_memory_stat_counts.total_free);

        dprintf (fd, "memory.total.calloc %lu\n", tcalloc);
        dprintf (fd, "memory.total.malloc %lu\n", tmalloc);
        dprintf (fd, "memory.total.realloc %lu\n",
                 GF_ATOMIC_GET (gf_memory_stat_counts.total_realloc));
        dprintf (fd, "memory.total.free %lu\n", tfree);
        dprintf (fd, "memory.total.in-use %lu\n", ((tcalloc + tmalloc) - tfree));

        for (i = 0; i < GF_BLK_MAX_VALUE; i++) {
                count = GF_ATOMIC_GET (gf_memory_stat_counts.blk_size[i]);
                dprintf (fd, "memory.total.blk_size.%s %lu\n",
                         gf_mem_stats_blk[i].blk_size_str, count);
        }

        dprintf (fd, "#----\n");
#endif

        /* This is not a metric to be watched in admin guide,
           but keeping it here till we resolve all leak-issues
           would be great */
}


static void
dump_latency_and_count (xlator_t *xl, int fd)
{
        int32_t  index = 0;
        uint64_t fop;
        uint64_t cbk;
        uint64_t count;

        if (xl->winds)
                dprintf (fd, "%s.total.pending-winds.count %lu\n", xl->name, xl->winds);

        /* Need 'fuse' data, and don't need all the old graph info */
        if ((xl != xl->ctx->master) && (xl->ctx->active != xl->graph))
                return;

        count = GF_ATOMIC_GET (xl->stats.total.count);
        dprintf (fd, "%s.total.fop-count %lu\n", xl->name, count);

        count = GF_ATOMIC_GET (xl->stats.interval.count);
        dprintf (fd, "%s.interval.fop-count %lu\n", xl->name, count);
        GF_ATOMIC_INIT (xl->stats.interval.count, 0);

        for (index = 0; index < GF_FOP_MAXVALUE; index++) {
                fop = GF_ATOMIC_GET (xl->stats.total.metrics[index].fop);
                if (fop) {
                        dprintf (fd, "%s.total.%s.count %lu\n",
                                 xl->name, gf_fop_list[index], fop);
                }
                fop = GF_ATOMIC_GET (xl->stats.interval.metrics[index].fop);
                if (fop) {
                        dprintf (fd, "%s.interval.%s.count %lu\n",
                                 xl->name, gf_fop_list[index], fop);
                }
                cbk = GF_ATOMIC_GET (xl->stats.interval.metrics[index].cbk);
                if (cbk) {
                        dprintf (fd, "%s.interval.%s.fail_count %lu\n",
                                 xl->name, gf_fop_list[index], cbk);
                }
                if (xl->stats.interval.latencies[index].count != 0.0) {
                        dprintf (fd, "%s.interval.%s.latency %lf\n",
                                 xl->name, gf_fop_list[index],
                                 (xl->stats.interval.latencies[index].total /
                                  xl->stats.interval.latencies[index].count));
                        dprintf (fd, "%s.interval.%s.max %lf\n",
                                 xl->name, gf_fop_list[index],
                                 xl->stats.interval.latencies[index].max);
                        dprintf (fd, "%s.interval.%s.min %lf\n",
                                 xl->name, gf_fop_list[index],
                                 xl->stats.interval.latencies[index].min);
                }
                GF_ATOMIC_INIT (xl->stats.interval.metrics[index].cbk, 0);
                GF_ATOMIC_INIT (xl->stats.interval.metrics[index].fop, 0);
        }
        memset (xl->stats.interval.latencies, 0,
                sizeof (xl->stats.interval.latencies));
}

static inline void
dump_call_stack_details (glusterfs_ctx_t *ctx, int fd)
{
        dprintf (fd, "total.stack.count %lu\n",
                 GF_ATOMIC_GET (ctx->pool->total_count));
        dprintf (fd, "total.stack.in-flight %lu\n",
                 ctx->pool->cnt);
}

static inline void
dump_dict_details (glusterfs_ctx_t *ctx, int fd)
{
        uint64_t total_dicts = 0;
        uint64_t total_pairs = 0;

        total_dicts = GF_ATOMIC_GET (ctx->stats.total_dicts_used);
        total_pairs = GF_ATOMIC_GET (ctx->stats.total_pairs_used);

        dprintf (fd, "total.dict.max-pairs-per %lu\n",
                 GF_ATOMIC_GET (ctx->stats.max_dict_pairs));
        dprintf (fd, "total.dict.pairs-used %lu\n", total_pairs);
        dprintf (fd, "total.dict.used %lu\n", total_dicts);
        dprintf (fd, "total.dict.average-pairs %lu\n",
                            (total_pairs / total_dicts));
}

static void
dump_inode_stats (glusterfs_ctx_t *ctx, int fd)
{
}

static void
dump_global_metrics (glusterfs_ctx_t *ctx, int fd)
{
        struct timeval tv;
        time_t nowtime;
        struct tm *nowtm;
        char tmbuf[64] = {0,};

        gettimeofday(&tv, NULL);
        nowtime = tv.tv_sec;
        nowtm = localtime(&nowtime);
        strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);

        /* Let every file have information on which process dumped info */
        dprintf (fd, "## %s\n", ctx->cmdlinestr);
        dprintf (fd, "### %s\n", tmbuf);
        dprintf (fd, "### BrickName: %s\n", ctx->cmd_args.brick_name);
        dprintf (fd, "### MountName: %s\n", ctx->cmd_args.mount_point);
        dprintf (fd, "### VolumeName: %s\n", ctx->cmd_args.volume_name);

        /* Dump memory accounting */
        dump_global_memory_accounting (fd);
        dprintf (fd, "# -----\n");

        dump_call_stack_details (ctx, fd);
        dump_dict_details (ctx, fd);
        dprintf (fd, "# -----\n");

        dump_inode_stats (ctx, fd);
        dprintf (fd, "# -----\n");
}

static void
dump_xl_metrics (glusterfs_ctx_t *ctx, int fd)
{
        xlator_t *xl;

        xl = ctx->active->top;

        while (xl) {
                dump_latency_and_count (xl, fd);
                dump_mem_acct_details (xl, fd);
                if (xl->dump_metrics)
                        xl->dump_metrics (xl, fd);
                xl = xl->next;
        }

        if (ctx->master) {
                xl = ctx->master;

                dump_latency_and_count (xl, fd);
                dump_mem_acct_details (xl, fd);
                if (xl->dump_metrics)
                        xl->dump_metrics (xl, fd);
        }

        return;
}

char *
gf_monitor_metrics (glusterfs_ctx_t *ctx)
{
        int ret = -1;
        int fd = 0;
        char *filepath, *dumppath;

        dumppath = ctx->config.metrics_dumppath;
        if (dumppath == NULL) {
                dumppath = GLUSTER_METRICS_DIR;
        }

        ret = gf_asprintf(&filepath, "%s/gmetrics.XXXXXX", dumppath);
        if (ret < 0) {
                return NULL;
        }

        fd = mkstemp (filepath);
        if (fd < 0) {
                gf_msg ("monitoring", GF_LOG_ERROR, 0, LG_MSG_STRDUP_ERROR,
                        "failed to open tmp file %s (%s)",
                        filepath, strerror (errno));
                GF_FREE (filepath);
                return NULL;
        }

        dump_global_metrics (ctx, fd);

        dump_xl_metrics (ctx, fd);

        /* This below line is used just to capture any errors with dprintf() */
        ret = dprintf (fd, "\n# End of metrics\n");
        if (ret < 0) {
                gf_msg ("monitoring", GF_LOG_WARNING, 0, LG_MSG_STRDUP_ERROR,
                        "dprintf() failed: %s", strerror (errno));
        }

        ret = sys_fsync (fd);
        if (ret < 0) {
                gf_msg ("monitoring", GF_LOG_WARNING, 0, LG_MSG_STRDUP_ERROR,
                        "fsync() failed: %s", strerror (errno));
        }
        sys_close (fd);

        /* Figure this out, not happy with returning this string */
        return filepath;
}
