/*
   Copyright (c) 2008-2010 Gluster, Inc. <http://www.gluster.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU Affero General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#include "mem-pool.h"
#include "logging.h"
#include "xlator.h"
#include <stdlib.h>
#include <stdarg.h>

#define GF_MEM_POOL_LIST_BOUNDARY        (sizeof(struct list_head))
#define GF_MEM_POOL_PAD_BOUNDARY         (GF_MEM_POOL_LIST_BOUNDARY + sizeof(int))
#define mem_pool_chunkhead2ptr(head)     ((head) + GF_MEM_POOL_PAD_BOUNDARY)
#define mem_pool_ptr2chunkhead(ptr)      ((ptr) - GF_MEM_POOL_PAD_BOUNDARY)
#define is_mem_chunk_in_use(ptr)         (*ptr == 1)

#define GF_MEM_HEADER_SIZE  (4 + sizeof (size_t) + sizeof (xlator_t *) + 4 + 8)
#define GF_MEM_TRAILER_SIZE 8

#define GF_MEM_HEADER_MAGIC  0xCAFEBABE
#define GF_MEM_TRAILER_MAGIC 0xBAADF00D

#define GLUSTERFS_ENV_MEM_ACCT_STR  "GLUSTERFS_DISABLE_MEM_ACCT"

static int gf_mem_acct_enable = 0;

int
gf_mem_acct_is_enabled ()
{
        return gf_mem_acct_enable;
}

void
gf_mem_acct_enable_set ()
{
        char    *opt = NULL;
        long    val = -1;

#ifdef DEBUG
        gf_mem_acct_enable = 1;
        return;
#endif

        opt = getenv (GLUSTERFS_ENV_MEM_ACCT_STR);

        if (!opt)
                return;

        val = strtol (opt, NULL, 0);

        if (val)
                gf_mem_acct_enable = 0;
        else
                gf_mem_acct_enable = 1;

}

void
gf_mem_set_acct_info (xlator_t *xl, char **alloc_ptr,
                      size_t size, uint32_t type)
{

        char    *ptr = NULL;

        if (!alloc_ptr)
                return;

        ptr = (char *) (*alloc_ptr);

        if (!xl) {
                GF_ASSERT (0);
        }

        if (!(xl->mem_acct.rec)) {
                GF_ASSERT (0);
        }

        if (type > xl->mem_acct.num_types) {
                GF_ASSERT (0);
        }

        LOCK(&xl->mem_acct.rec[type].lock);
        {
                xl->mem_acct.rec[type].size += size;
                xl->mem_acct.rec[type].num_allocs++;
                xl->mem_acct.rec[type].max_size =
                           max (xl->mem_acct.rec[type].max_size,
                                xl->mem_acct.rec[type].size);
                xl->mem_acct.rec[type].max_num_allocs =
                           max (xl->mem_acct.rec[type].max_num_allocs,
                                xl->mem_acct.rec[type].num_allocs);
        }
        UNLOCK(&xl->mem_acct.rec[type].lock);

        *(uint32_t *)(ptr) = type;
        ptr = ptr + 4;
        memcpy (ptr, &size, sizeof(size_t));
        ptr += sizeof (size_t);
        memcpy (ptr, &xl, sizeof(xlator_t *));
        ptr += sizeof (xlator_t *);
        *(uint32_t *)(ptr) = GF_MEM_HEADER_MAGIC;
        ptr = ptr + 4;
        ptr = ptr + 8; //padding
        *(uint32_t *) (ptr + size) = GF_MEM_TRAILER_MAGIC;

        *alloc_ptr = (void *)ptr;
        return;
}


void *
__gf_calloc (size_t nmemb, size_t size, uint32_t type)
{
        size_t          tot_size = 0;
        size_t          req_size = 0;
        char            *ptr = NULL;
        xlator_t        *xl = NULL;

        if (!gf_mem_acct_enable)
                return CALLOC (nmemb, size);

        xl = THIS;

        req_size = nmemb * size;
        tot_size = req_size + GF_MEM_HEADER_SIZE + GF_MEM_TRAILER_SIZE;

        ptr = calloc (1, tot_size);

        if (!ptr)
                return NULL;

        gf_mem_set_acct_info (xl, &ptr, req_size, type);

        return (void *)ptr;
}

void *
__gf_malloc (size_t size, uint32_t type)
{
        size_t          tot_size = 0;
        char            *ptr = NULL;
        xlator_t        *xl = NULL;

        if (!gf_mem_acct_enable)
                return MALLOC (size);

        xl = THIS;

        tot_size = size + GF_MEM_HEADER_SIZE + GF_MEM_TRAILER_SIZE;

        ptr = malloc (tot_size);
        if (!ptr)
                return NULL;

        gf_mem_set_acct_info (xl, &ptr, size, type);

        return (void *)ptr;
}

void *
__gf_realloc (void *ptr, size_t size)
{
        size_t          tot_size = 0;
        char            *orig_ptr = NULL;
        xlator_t        *xl = NULL;
        uint32_t        type = 0;

        if (!gf_mem_acct_enable)
                return realloc (ptr, size);

        tot_size = size + GF_MEM_HEADER_SIZE + GF_MEM_TRAILER_SIZE;

        orig_ptr = (char *)ptr - 8 - 4;

        GF_ASSERT (*(uint32_t *)orig_ptr == GF_MEM_HEADER_MAGIC);

        orig_ptr = orig_ptr - sizeof(xlator_t *);
        xl = *((xlator_t **)orig_ptr);

        orig_ptr = (char *)ptr - GF_MEM_HEADER_SIZE;
        type = *(uint32_t *)orig_ptr;

        ptr = realloc (orig_ptr, tot_size);
        if (!ptr)
                return NULL;

        gf_mem_set_acct_info (xl, (char **)&ptr, size, type);

        return (void *)ptr;
}

int
gf_vasprintf (char **string_ptr, const char *format, va_list arg)
{
        va_list arg_save;
	char    *str = NULL;
	int     size = 0;
	int     rv = 0;

	if (!string_ptr || !format)
		return -1;

        va_copy (arg_save, arg);

	size = vsnprintf (NULL, 0, format, arg);
	size++;
	str = GF_MALLOC (size, gf_common_mt_asprintf);
	if (str == NULL) {
		/*
		 * Strictly speaking, GNU asprintf doesn't do this,
		 * but the caller isn't checking the return value.
		 */
		gf_log ("libglusterfs", GF_LOG_CRITICAL,
                         "failed to allocate memory");
		return -1;
	}
	rv = vsnprintf (str, size, format, arg_save);

	*string_ptr = str;
	return (rv);
}

int
gf_asprintf (char **string_ptr, const char *format, ...)
{
	va_list arg;
	int     rv = 0;

	va_start (arg, format);
	rv = gf_vasprintf (string_ptr, format, arg);
	va_end (arg);

	return rv;
}

void
__gf_free (void *free_ptr)
{
        size_t          req_size = 0;
        char            *ptr = NULL;
        uint32_t        type = 0;
        xlator_t        *xl = NULL;

        if (!gf_mem_acct_enable) {
                FREE (free_ptr);
                return;
        }

        if (!free_ptr)
                return;

        ptr = (char *)free_ptr - 8 - 4;

        if (GF_MEM_HEADER_MAGIC != *(uint32_t *)ptr) {
                //Possible corruption, assert here
                GF_ASSERT (0);
        }

        *(uint32_t *)ptr = 0;

        ptr = ptr - sizeof(xlator_t *);
        memcpy (&xl, ptr, sizeof(xlator_t *));

        if (!xl) {
                //gf_free expects xl to be available
                GF_ASSERT (0);
        }

        if (!xl->mem_acct.rec) {
                ptr = (char *)free_ptr - GF_MEM_HEADER_SIZE;
                goto free;
        }


        ptr = ptr - sizeof(size_t);
        memcpy (&req_size, ptr, sizeof (size_t));
        ptr = ptr - 4;
        type = *(uint32_t *)ptr;

        if (GF_MEM_TRAILER_MAGIC != *(uint32_t *)
                                    ((char *)free_ptr + req_size)) {
                // This points to a memory overrun
                GF_ASSERT (0);
        }
        *(uint32_t *) ((char *)free_ptr + req_size) = 0;

        LOCK (&xl->mem_acct.rec[type].lock);
        {
                xl->mem_acct.rec[type].size -= req_size;
                xl->mem_acct.rec[type].num_allocs--;
        }
        UNLOCK (&xl->mem_acct.rec[type].lock);
free:
        FREE (ptr);
}



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

	mem_pool = GF_CALLOC (sizeof (*mem_pool), 1, gf_common_mt_mem_pool);
	if (!mem_pool)
		return NULL;

	LOCK_INIT (&mem_pool->lock);
	INIT_LIST_HEAD (&mem_pool->list);

	mem_pool->padded_sizeof_type = padded_sizeof_type;
	mem_pool->cold_count = count;
        mem_pool->real_sizeof_type = sizeof_type;

        pool = GF_CALLOC (count, padded_sizeof_type, gf_common_mt_long);
	if (!pool) {
                GF_FREE (mem_pool);
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

void*
mem_get0 (struct mem_pool *mem_pool)
{
        void             *ptr = NULL;

        if (!mem_pool) {
                gf_log ("mem-pool", GF_LOG_ERROR, "invalid argument");
                return NULL;
        }

        ptr = mem_get(mem_pool);

        if (ptr)
                memset(ptr, 0, mem_pool->real_sizeof_type);

        return ptr;
}

void *
mem_get (struct mem_pool *mem_pool)
{
	struct list_head *list = NULL;
	void             *ptr = NULL;
        int             *in_use = NULL;

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
                        in_use = (ptr + GF_MEM_POOL_LIST_BOUNDARY);
                        *in_use = 1;

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
	int    *in_use = NULL;
	void   *head = NULL;

	if (!pool || !ptr) {
		gf_log ("mem-pool", GF_LOG_ERROR, "invalid argument");
		return;
	}

	LOCK (&pool->lock);
	{

		switch (__is_member (pool, ptr))
		{
		case 1:
	                list = head = mem_pool_ptr2chunkhead (ptr);
			in_use = (head + GF_MEM_POOL_LIST_BOUNDARY);
			if (!is_mem_chunk_in_use(in_use)) {
                               gf_log_callingfn ("mem-pool", GF_LOG_CRITICAL,
                                       "mem_put called on freed ptr %p of mem "
                                       "pool %p", ptr, pool);
                               break;
                       } 
		        pool->hot_count--;
			pool->cold_count++;
			*in_use = 0;
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
        GF_FREE (pool->pool);
        GF_FREE (pool);

        return;
}
