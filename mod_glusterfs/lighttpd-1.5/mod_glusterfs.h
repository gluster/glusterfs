#ifndef _MOD_GLUSTERFS_FILE_CACHE_H_
#define _MOD_GLUSTERFS_FILE_CACHE_H_

#include "stat_cache.h"
#include <libglusterfsclient.h>
#include "base.h"

handler_t glusterfs_stat_cache_get_entry(server *srv, connection *con, libglusterfs_handle_t handle, buffer *glusterfs_path, buffer *name, void *buf, size_t size, stat_cache_entry **fce);

#endif
