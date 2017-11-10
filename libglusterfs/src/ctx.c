/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <pthread.h>

#include "globals.h"
#include "glusterfs.h"
#include "timer-wheel.h"

glusterfs_ctx_t *
glusterfs_ctx_new ()
{
        int               ret = 0;
	glusterfs_ctx_t  *ctx = NULL;

	/* no GF_CALLOC here, gf_acct_mem_set_enable is not
	   yet decided at this point */
        ctx = calloc (1, sizeof (*ctx));
        if (!ctx) {
                ret = -1;
                goto out;
        }

        ctx->mem_acct_enable = gf_global_mem_acct_enable_get();

        INIT_LIST_HEAD (&ctx->graphs);
        INIT_LIST_HEAD (&ctx->mempool_list);
        INIT_LIST_HEAD (&ctx->volfile_list);

	ctx->daemon_pipe[0] = -1;
	ctx->daemon_pipe[1] = -1;

        ctx->log.loglevel = DEFAULT_LOG_LEVEL;

#ifdef RUN_WITH_VALGRIND
        ctx->cmd_args.valgrind = _gf_true;
#endif

        /* lock is never destroyed! */
	ret = LOCK_INIT (&ctx->lock);
	if (ret) {
		free (ctx);
		ctx = NULL;
                goto out;
	}

        GF_ATOMIC_INIT (ctx->stats.max_dict_pairs, 0);
        GF_ATOMIC_INIT (ctx->stats.total_pairs_used, 0);
        GF_ATOMIC_INIT (ctx->stats.total_dicts_used, 0);
out:
	return ctx;
}

static void
glusterfs_ctx_tw_destroy (struct gf_ctx_tw *ctx_tw)
{
        if (ctx_tw->timer_wheel)
                gf_tw_cleanup_timers (ctx_tw->timer_wheel);

        GF_FREE (ctx_tw);
}

struct tvec_base*
glusterfs_ctx_tw_get (glusterfs_ctx_t *ctx)
{
        struct gf_ctx_tw *ctx_tw = NULL;

        LOCK (&ctx->lock);
        {
                if (ctx->tw) {
                        ctx_tw = GF_REF_GET (ctx->tw);
                } else {
                        ctx_tw = GF_CALLOC (1, sizeof (struct gf_ctx_tw),
                                            gf_common_mt_tw_ctx);
                        ctx_tw->timer_wheel = gf_tw_init_timers();
                        GF_REF_INIT (ctx_tw, glusterfs_ctx_tw_destroy);
                        ctx->tw = ctx_tw;
                }
        }
        UNLOCK (&ctx->lock);

        return ctx_tw->timer_wheel;
}

void
glusterfs_ctx_tw_put (glusterfs_ctx_t *ctx)
{
        GF_REF_PUT (ctx->tw);
}
