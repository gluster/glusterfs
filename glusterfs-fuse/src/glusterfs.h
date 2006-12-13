/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#ifndef __GLUSTERFS_FOPS_H__
#define __GLUSTERFS_FOPS_H__

#include "xlator.h"

#define DEFAULT_LOG_FILE   DATADIR"/log/glusterfs/glusterfs.log"
#define DEFAULT_GLUSTERFS_CLIENT_VOL CONFDIR "/glusterfs-client.vol"

#define SPEC_LOCAL_FILE      1
#define SPEC_REMOTE_FILE     2

#define GF_YES 1
#define GF_NO  0
#ifdef GF_LOG_FUSE_ARGS
#undef GF_LOG_FUSE_ARGS
#endif


/* looks ugly, but is very neat */
struct spec_location {
  int32_t where;
  union {
    char *file;
    struct {
      char *ip;
      char *port;
    }server;
  }spec;
};

int glusterfs_mount (xlator_t *graph, const char *mount_point);

#endif /* __GLUSTERFS_FOPS_H__ */
