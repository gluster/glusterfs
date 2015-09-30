/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <fnmatch.h>
#include "marker-common.h"

marker_inode_ctx_t *
marker_inode_ctx_new ()
{
        marker_inode_ctx_t *ctx = NULL;

        ctx = GF_CALLOC (1, sizeof (marker_inode_ctx_t),
                         gf_marker_mt_marker_inode_ctx_t);
        if (ctx == NULL)
                goto out;

        ctx->quota_ctx = NULL;
out:
        return ctx;
}

int32_t
marker_force_inode_ctx_get (inode_t *inode, xlator_t *this,
                            marker_inode_ctx_t **ctx)
{
        int32_t  ret     = -1;
        uint64_t ctx_int = 0;

        LOCK (&inode->lock);
        {
                ret = __inode_ctx_get (inode, this, &ctx_int);
                if (ret == 0)
                        *ctx = (marker_inode_ctx_t *) (unsigned long)ctx_int;
                else {
                        *ctx = marker_inode_ctx_new ();
                        if (*ctx == NULL)
                                goto unlock;

                        ret = __inode_ctx_put (inode, this,
                                               (uint64_t )(unsigned long) *ctx);
                        if (ret == -1) {
                                GF_FREE (*ctx);
                                goto unlock;
                        }
                        ret = 0;
                }
        }
unlock: UNLOCK (&inode->lock);

        return ret;
}

int
marker_filter_quota_xattr (dict_t *dict, char *key,
                           data_t *value, void *data)
{
        dict_del (dict, key);
        return 0;
}
