/*
  Copyright (c) 2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef __XATTR_CACHE_H__
#define __XATTR_CACHE_H__


#include "glusterfs.h"
#include "inode.h"

typedef struct __xattr_cache_handle {
	loc_t loc;
	fd_t  *fd;
} xattr_cache_handle_t;


typedef struct __xattr_cache_entry {
	char *key;               /* name of the xattr */
	int32_t *array;          /* value */
	size_t len;              /* length of array in bytes */
	inode_t *inode;          /* inode for which the entry is for */

	xattr_cache_handle_t *handle;
	unsigned char dirty;
	unsigned long nraccess;  /* number of times accessed */
} xattr_cache_entry_t;


typedef struct __xattr_cache {
	size_t size;
	pthread_mutex_t lock;
	xattr_cache_entry_t **entries;
} xattr_cache_t;


xattr_cache_t * posix_xattr_cache_init (size_t size);

int posix_xattr_cache_read (xlator_t *this, xattr_cache_handle_t *handle, 
			    char *key, int32_t *array, size_t len);

int posix_xattr_cache_write (xlator_t *this, xattr_cache_handle_t *handle,
			     char *key, int32_t *array, size_t len);

int posix_xattr_cache_flush (xlator_t *this, xattr_cache_handle_t *handle);

int posix_xattr_cache_flush_all (xlator_t *this);


#endif /* __XATTR_CACHE_H__ */
