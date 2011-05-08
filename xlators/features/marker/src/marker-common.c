/*Copyright (c) 2008-2010 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif
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

void
marker_filter_quota_xattr (dict_t *dict, char *key,
                           data_t *value, void *data)
{
        int ret = -1;

        GF_VALIDATE_OR_GOTO ("marker", dict, out);
        GF_VALIDATE_OR_GOTO ("marker", key, out);

        ret = fnmatch ("trusted.glusterfs.quota*", key, 0);
        if (ret == 0)
                dict_del (dict, key);
out:
        return;
}
