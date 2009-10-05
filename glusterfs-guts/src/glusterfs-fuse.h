/*
   Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef __GLUSTERFS_FUSE_H__
#define __GLUSTERFS_FUSE_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif /* _CONFIG_H */

#define DEFAULT_LOG_FILE   DATADIR"/log/glusterfs/glusterfs.log"
#define DEFAULT_GLUSTERFS_CLIENT_VOL CONFDIR "/glusterfs-client.vol"

#define SPEC_LOCAL_FILE      1
#define SPEC_REMOTE_FILE     2

#if 0
#define GF_YES 1
#define GF_NO  0
#endif

#ifdef GF_LOG_FUSE_ARGS
#undef GF_LOG_FUSE_ARGS
#endif

struct gf_spec_location {
  int32_t where;
  union {
    char *file;
    struct {
      char *ip;
      char *port;
      char *transport;
    }server;
  }spec;
};

transport_t * glusterfs_mount (glusterfs_ctx_t *ctx,
			       const char *mount_point);

#endif /* __GLUSTERFS_FUSE_H__ */
