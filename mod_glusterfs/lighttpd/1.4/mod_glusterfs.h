/*
  Copyright (c) 2008-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _MOD_GLUSTERFS_FILE_CACHE_H_
#define _MOD_GLUSTERFS_FILE_CACHE_H_

#include "stat_cache.h"
#include <libglusterfsclient.h>
#include "base.h"

handler_t glusterfs_stat_cache_get_entry(server *srv, connection *con,
                                         glusterfs_handle_t handle,
                                         buffer *glusterfs_path, buffer *name,
                                         void *buf, size_t size,
                                         stat_cache_entry **fce);

#endif
