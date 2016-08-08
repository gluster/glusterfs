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

	ctx->daemon_pipe[0] = -1;
	ctx->daemon_pipe[1] = -1;

        ctx->log.loglevel = DEFAULT_LOG_LEVEL;

        /* lock is never destroyed! */
	ret = LOCK_INIT (&ctx->lock);
	if (ret) {
		free (ctx);
		ctx = NULL;
	}

out:
	return ctx;
}

