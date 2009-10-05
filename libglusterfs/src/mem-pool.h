/*
   Copyright (c) 2008-2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _MEM_POOL_H_
#define _MEM_POOL_H_

#include "list.h"
#include "locking.h"
#include <stdlib.h>


#define MALLOC(size) malloc(size)
#define CALLOC(cnt,size) calloc(cnt,size)

#define FREE(ptr)				\
	if (ptr != NULL) {			\
		free ((void *)ptr);		\
		ptr = (void *)0xeeeeeeee;	\
	}                      

struct mem_pool {
	struct list_head  list;
	int               hot_count;
	int               cold_count;
	gf_lock_t         lock;
	unsigned long     padded_sizeof_type;
	void             *pool;
	void             *pool_end;
        int               real_sizeof_type;
};

struct mem_pool *
mem_pool_new_fn (unsigned long sizeof_type, unsigned long count);

#define mem_pool_new(type,count) mem_pool_new_fn (sizeof(type), count)

void mem_put (struct mem_pool *pool, void *ptr);
void *mem_get (struct mem_pool *pool);

void mem_pool_destroy (struct mem_pool *pool);

#endif /* _MEM_POOL_H */
