/*Copyright (c) 2008-2011 Gluster, Inc. <http://www.gluster.com>
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
#ifndef _MARKER_QUOTA_HELPER_H
#define _MARKER_QUOTA_HELPER

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "marker-quota.h"

#define QUOTA_FREE_CONTRIBUTION_NODE(_contribution)     \
        do {                                            \
                list_del (&_contribution->contri_list); \
                GF_FREE (_contribution);                \
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
add_new_contribution_node (xlator_t *, quota_inode_ctx_t *, loc_t *);

int32_t
dict_set_contribution (xlator_t *, dict_t *, loc_t *);

quota_inode_ctx_t *
quota_inode_ctx_new (inode_t *, xlator_t *);

int32_t
quota_inode_ctx_get (inode_t *, xlator_t *, quota_inode_ctx_t **);

int32_t
delete_contribution_node (dict_t *, char *, inode_contribution_t *);

int32_t
quota_inode_loc_fill (const char *, inode_t *, loc_t *);

quota_local_t *
quota_local_new ();

quota_local_t *
quota_local_ref (quota_local_t *);

int32_t
quota_local_unref (xlator_t *, quota_local_t *);

inode_contribution_t *
get_contribution_node (inode_t *, quota_inode_ctx_t *);

inode_contribution_t *
get_contribution_from_loc (xlator_t *this, loc_t *loc);

#endif
