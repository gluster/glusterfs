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

#include "mem-pool.h"
#include "logging.h"
#include <stdlib.h>


#define GF_MEM_POOL_PAD_BOUNDARY         (sizeof(struct list_head))
#define mem_pool_chunkhead2ptr(head)     ((head) + GF_MEM_POOL_PAD_BOUNDARY)
#define mem_pool_ptr2chunkhead(ptr)      ((ptr) - GF_MEM_POOL_PAD_BOUNDARY)


struct mem_pool *
mem_pool_new_fn (unsigned long sizeof_type,
		 unsigned long count)
{
	struct mem_pool  *mem_pool = NULL;
	unsigned long     padded_sizeof_type = 0;
	void             *pool = NULL;
	int               i = 0;
	struct list_head *list = NULL;
  
	if (!sizeof_type || !count) {
		gf_log ("mem-pool", GF_LOG_ERROR, "invalid argument");
		return NULL;
	}
        padded_sizeof_type = sizeof_type + GF_MEM_POOL_PAD_BOUNDARY;
  
	mem_pool = CALLOC (sizeof (*mem_pool), 1);
	if (!mem_pool)
		return NULL;

	LOCK_INIT (&mem_pool->lock);
	INIT_LIST_HEAD (&mem_pool->list);

	mem_pool->padded_sizeof_type = padded_sizeof_type;
	mem_pool->cold_count = count;
        mem_pool->real_sizeof_type = sizeof_type;

        pool = CALLOC (count, padded_sizeof_type);
	if (!pool) {
                FREE (mem_pool);
		return NULL;
        }

	for (i = 0; i < count; i++) {
		list = pool + (i * (padded_sizeof_type));
		INIT_LIST_HEAD (list);
		list_add_tail (list, &mem_pool->list);
	}

	mem_pool->pool = pool;
	mem_pool->pool_end = pool + (count * (padded_sizeof_type));

	return mem_pool;
}


void *
mem_get (struct mem_pool *mem_pool)
{
	struct list_head *list = NULL;
	void             *ptr = NULL;
  
	if (!mem_pool) {
		gf_log ("mem-pool", GF_LOG_ERROR, "invalid argument");
		return NULL;
	}

	LOCK (&mem_pool->lock);
	{
		if (mem_pool->cold_count) {
			list = mem_pool->list.next;
			list_del (list);

			mem_pool->hot_count++;
			mem_pool->cold_count--;

			ptr = list;
                        goto fwd_addr_out;
		}

                /* This is a problem area. If we've run out of
                 * chunks in our slab above, we need to allocate
                 * enough memory to service this request.
                 * The problem is, these indvidual chunks will fail
                 * the first address range check in __is_member. Now, since
                 * we're not allocating a full second slab, we wont have
                 * enough info perform the range check in __is_member.
                 *
                 * I am working around this by performing a regular allocation
                 * , just the way the caller would've done when not using the
                 * mem-pool. That also means, we're not padding the size with
                 * the list_head structure because, this will not be added to
                 * the list of chunks that belong to the mem-pool allocated
                 * initially.
                 *
                 * This is the best we can do without adding functionality for
                 * managing multiple slabs. That does not interest us at present
                 * because it is too much work knowing that a better slab
                 * allocator is coming RSN.
                 */
		ptr = MALLOC (mem_pool->real_sizeof_type);

                /* Memory coming from the heap need not be transformed from a
                 * chunkhead to a usable pointer since it is not coming from
                 * the pool.
                 */
                goto unlocked_out;
	}
fwd_addr_out:
        ptr = mem_pool_chunkhead2ptr (ptr);
unlocked_out:
        UNLOCK (&mem_pool->lock);

	return ptr;
}


static int
__is_member (struct mem_pool *pool, void *ptr)
{
	if (!pool || !ptr) {
		gf_log ("mem-pool", GF_LOG_ERROR, "invalid argument");
		return -1;
	}
  
	if (ptr < pool->pool || ptr >= pool->pool_end)
		return 0;

	if ((mem_pool_ptr2chunkhead (ptr) - pool->pool)
                        % pool->padded_sizeof_type)
		return -1;

	return 1;
}


void
mem_put (struct mem_pool *pool, void *ptr)
{
	struct list_head *list = NULL;
  
	if (!pool || !ptr) {
		gf_log ("mem-pool", GF_LOG_ERROR, "invalid argument");
		return;
	}
  
	LOCK (&pool->lock);
	{

		switch (__is_member (pool, ptr))
		{
		case 1:
	                list = mem_pool_ptr2chunkhead (ptr);
		        pool->hot_count--;
			pool->cold_count++;
			list_add (list, &pool->list);
			break;
		case -1:
                        /* For some reason, the address given is within
                         * the address range of the mem-pool but does not align
                         * with the expected start of a chunk that includes
                         * the list headers also. Sounds like a problem in
                         * layers of clouds up above us. ;)
                         */
			abort ();
			break;
		case 0:
                        /* The address is outside the range of the mem-pool. We
                         * assume here that this address was allocated at a
                         * point when the mem-pool was out of chunks in mem_get
                         * or the programmer has made a mistake by calling the
                         * wrong de-allocation interface. We do
                         * not have enough info to distinguish between the two
                         * situations.
                         */
			FREE (ptr);
			break;
		default:
			/* log error */
			break;
		}
	}
	UNLOCK (&pool->lock);
}


void
mem_pool_destroy (struct mem_pool *pool)
{
        if (!pool)
                return;

        LOCK_DESTROY (&pool->lock);
        FREE (pool->pool);
        FREE (pool);

        return;
}
