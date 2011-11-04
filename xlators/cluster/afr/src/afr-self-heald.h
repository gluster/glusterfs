/*
   Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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

#ifndef __AFR_SELF_HEALD_H__
#define __AFR_SELF_HEALD_H__
#include "xlator.h"

#define IS_ROOT_PATH(path) (!strcmp (path, "/"))
#define IS_ENTRY_CWD(entry) (!strcmp (entry, "."))
#define IS_ENTRY_PARENT(entry) (!strcmp (entry, ".."))
#define AFR_ALL_CHILDREN -1

typedef struct afr_crawl_data_ {
        int     child;
        pid_t   pid;
} afr_crawl_data_t;

void afr_proactive_self_heal (xlator_t *this, int idx);

void afr_build_root_loc (inode_t *inode, loc_t *loc);

int afr_set_root_gfid (dict_t *dict);

inline void
afr_fill_loc_info (loc_t *loc, struct iatt *iatt, struct iatt *parent);

#endif /* __AFR_SELF_HEALD_H__ */
