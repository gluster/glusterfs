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

#ifndef _MARKER_COMMON_H
#define _MARKER_COMMON_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "inode.h"
#include "xlator.h"
#include "marker.h"

int32_t
marker_force_inode_ctx_get (inode_t *, xlator_t *, marker_inode_ctx_t **);

void
marker_filter_quota_xattr (dict_t *, char *, data_t *, void *);
#endif
