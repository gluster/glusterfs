/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _MARKER_QUOTA_HELPER_H
#define _MARKER_QUOTA_HELPER_H

#include "marker.h"

#define QUOTA_FREE_CONTRIBUTION_NODE(ctx, _contribution)             \
        do {                                                         \
                LOCK (&ctx->lock);                                   \
                {                                                    \
                        list_del_init (&_contribution->contri_list); \
                        GF_REF_PUT (_contribution);                  \
                }                                                    \
                UNLOCK (&ctx->lock);                                 \
        } while (0)

#define QUOTA_SAFE_INCREMENT(lock, var)                 \
        do {                                            \
                LOCK (lock);                            \
                        var ++;                         \
                UNLOCK (lock);                          \
        } while (0)

#define QUOTA_SAFE_DECREMENT(lock, var, value)  \
        do {                                    \
                LOCK (lock);                    \
                {                               \
                      value = --var;            \
                }                               \
                UNLOCK (lock);                  \
        } while (0)

inode_contribution_t *
mq_add_new_contribution_node (xlator_t *, quota_inode_ctx_t *, loc_t *);

int32_t
mq_dict_set_contribution (xlator_t *, dict_t *, loc_t *, uuid_t, char *);

quota_inode_ctx_t *
mq_inode_ctx_new (inode_t *, xlator_t *);

int32_t
mq_inode_ctx_get (inode_t *, xlator_t *, quota_inode_ctx_t **);

int32_t
mq_delete_contribution_node (dict_t *, char *, inode_contribution_t *);

int32_t
mq_inode_loc_fill (const char *, inode_t *, loc_t *);

quota_local_t *
mq_local_new ();

quota_local_t *
mq_local_ref (quota_local_t *);

int32_t
mq_local_unref (xlator_t *, quota_local_t *);

void
mq_contri_fini (void *data);

inode_contribution_t*
mq_contri_init (inode_t *inode);

inode_contribution_t *
mq_get_contribution_node (inode_t *, quota_inode_ctx_t *);

inode_contribution_t *
mq_get_contribution_from_loc (xlator_t *this, loc_t *loc);

#endif
