/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "mem-pool.h"
#include "logging.h"
#include "xlator.h"
#include <stdlib.h>
#include <stdarg.h>

#define GF_MEM_POOL_LIST_BOUNDARY        (sizeof(struct list_head))
#define GF_MEM_POOL_PTR                  (sizeof(struct mem_pool*))
#define GF_MEM_POOL_PAD_BOUNDARY         (GF_MEM_POOL_LIST_BOUNDARY  + GF_MEM_POOL_PTR + sizeof(int))
#define mem_pool_chunkhead2ptr(head)     ((head) + GF_MEM_POOL_PAD_BOUNDARY)
#define mem_pool_ptr2chunkhead(ptr)      ((ptr) - GF_MEM_POOL_PAD_BOUNDARY)
#define is_mem_chunk_in_use(ptr)         (*ptr == 1)
#define mem_pool_from_ptr(ptr)           ((ptr) + GF_MEM_POOL_LIST_BOUNDARY)

#define GLUSTERFS_ENV_MEM_ACCT_STR  "GLUSTERFS_DISABLE_MEM_ACCT"

#include "unittest/unittest.h"
#include "libglusterfs-messages.h"

void
gf_mem_acct_enable_set (void *data)
{
        glusterfs_ctx_t *ctx = NULL;

        REQUIRE(data != NULL);

        ctx = data;

        GF_ASSERT (ctx != NULL);

        ctx->mem_acct_enable = 1;

        ENSURE(1 == ctx->mem_acct_enable);

        return;
}

int
gf_mem_set_acct_info (xlator_t *xl, char **alloc_ptr, size_t size,
		      uint32_t type, const char *typestr)
{

        void              *ptr    = NULL;
        struct mem_header *header = NULL;

        if (!alloc_ptr)
                return -1;

        ptr = *alloc_ptr;

        GF_ASSERT (xl != NULL);

        GF_ASSERT (xl->mem_acct != NULL);

        GF_ASSERT (type <= xl->mem_acct->num_types);

        LOCK(&xl->mem_acct->rec[type].lock);
        {
		if (!xl->mem_acct->rec[type].typestr)
			xl->mem_acct->rec[type].typestr = typestr;
                xl->mem_acct->rec[type].size += size;
                xl->mem_acct->rec[type].num_allocs++;
                xl->mem_acct->rec[type].total_allocs++;
                xl->mem_acct->rec[type].max_size =
                        max (xl->mem_acct->rec[type].max_size,
                             xl->mem_acct->rec[type].size);
                xl->mem_acct->rec[type].max_num_allocs =
                        max (xl->mem_acct->rec[type].max_num_allocs,
                             xl->mem_acct->rec[type].num_allocs);
        }
        UNLOCK(&xl->mem_acct->rec[type].lock);

        INCREMENT_ATOMIC (xl->mem_acct->lock, xl->mem_acct->refcnt);

        header = (struct mem_header *) ptr;
        header->type = type;
        header->size = size;
        header->mem_acct = xl->mem_acct;
        header->magic = GF_MEM_HEADER_MAGIC;

        ptr += sizeof (struct mem_header);

        /* data follows in this gap of 'size' bytes */
        *(uint32_t *) (ptr + size) = GF_MEM_TRAILER_MAGIC;

        *alloc_ptr = ptr;
        return 0;
}


void *
__gf_calloc (size_t nmemb, size_t size, uint32_t type, const char *typestr)
{
        size_t          tot_size = 0;
        size_t          req_size = 0;
        char            *ptr = NULL;
        xlator_t        *xl = NULL;

        if (!THIS->ctx->mem_acct_enable)
                return CALLOC (nmemb, size);

        xl = THIS;

        req_size = nmemb * size;
        tot_size = req_size + GF_MEM_HEADER_SIZE + GF_MEM_TRAILER_SIZE;

        ptr = calloc (1, tot_size);

        if (!ptr) {
                gf_msg_nomem ("", GF_LOG_ALERT, tot_size);
                return NULL;
        }
        gf_mem_set_acct_info (xl, &ptr, req_size, type, typestr);

        return (void *)ptr;
}

void *
__gf_malloc (size_t size, uint32_t type, const char *typestr)
{
        size_t          tot_size = 0;
        char            *ptr = NULL;
        xlator_t        *xl = NULL;

        if (!THIS->ctx->mem_acct_enable)
                return MALLOC (size);

        xl = THIS;

        tot_size = size + GF_MEM_HEADER_SIZE + GF_MEM_TRAILER_SIZE;

        ptr = malloc (tot_size);
        if (!ptr) {
                gf_msg_nomem ("", GF_LOG_ALERT, tot_size);
                return NULL;
        }
        gf_mem_set_acct_info (xl, &ptr, size, type, typestr);

        return (void *)ptr;
}

void *
__gf_realloc (void *ptr, size_t size)
{
        size_t             tot_size = 0;
        char              *new_ptr;
        struct mem_header *old_header = NULL;
        struct mem_header *new_header = NULL;
        struct mem_header  tmp_header;

        if (!THIS->ctx->mem_acct_enable)
                return REALLOC (ptr, size);

        REQUIRE(NULL != ptr);

        old_header = (struct mem_header *) (ptr - GF_MEM_HEADER_SIZE);
        GF_ASSERT (old_header->magic == GF_MEM_HEADER_MAGIC);
        tmp_header = *old_header;

        tot_size = size + GF_MEM_HEADER_SIZE + GF_MEM_TRAILER_SIZE;
        new_ptr = realloc (old_header, tot_size);
        if (!new_ptr) {
                gf_msg_nomem ("", GF_LOG_ALERT, tot_size);
                return NULL;
        }

        /*
         * We used to pass (char **)&ptr as the second
         * argument after the value of realloc was saved
         * in ptr, but the compiler warnings complained
         * about the casting to and forth from void ** to
         * char **.
         * TBD: it would be nice to adjust the memory accounting info here,
         * but calling gf_mem_set_acct_info here is wrong because it bumps
         * up counts as though this is a new allocation - which it's not.
         * The consequence of doing nothing here is only that the sizes will be
         * wrong, but at least the counts won't be.
        uint32_t           type = 0;
        xlator_t          *xl = NULL;
        type = header->type;
        xl = (xlator_t *) header->xlator;
        gf_mem_set_acct_info (xl, &new_ptr, size, type, NULL);
         */

        new_header = (struct mem_header *) new_ptr;
        *new_header = tmp_header;
        new_header->size = size;

        new_ptr += sizeof (struct mem_header);
        /* data follows in this gap of 'size' bytes */
        *(uint32_t *) (new_ptr + size) = GF_MEM_TRAILER_MAGIC;

        return (void *)new_ptr;
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
                /* log is done in GF_MALLOC itself */
                return -1;
        }
        rv = vsnprintf (str, size, format, arg_save);

        *string_ptr = str;
        va_end (arg_save);
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

#ifdef DEBUG
void
__gf_mem_invalidate (void *ptr)
{
        struct mem_header *header = ptr;
        void              *end    = NULL;

        struct mem_invalid inval = {
                .magic = GF_MEM_INVALID_MAGIC,
                .mem_acct = header->mem_acct,
                .type = header->type,
                .size = header->size,
                .baseaddr = ptr + GF_MEM_HEADER_SIZE,
        };

        /* calculate the last byte of the allocated area */
        end = ptr + GF_MEM_HEADER_SIZE + inval.size + GF_MEM_TRAILER_SIZE;

        /* overwrite the old mem_header */
        memcpy (ptr, &inval, sizeof (inval));
        ptr += sizeof (inval);

        /* zero out remaining (old) mem_header bytes) */
        memset (ptr, 0x00, sizeof (*header) - sizeof (inval));
        ptr += sizeof (*header) - sizeof (inval);

        /* zero out the first byte of data */
        *(uint32_t *)(ptr) = 0x00;
        ptr += 1;

        /* repeated writes of invalid structurein data area */
        while ((ptr + (sizeof (inval))) < (end - 1)) {
                memcpy (ptr, &inval, sizeof (inval));
                ptr += sizeof (inval);
        }

        /* fill out remaining data area with 0xff */
        memset (ptr, 0xff, end - ptr);
}
#endif /* DEBUG */

void
__gf_free (void *free_ptr)
{
        void              *ptr = NULL;
        struct mem_acct   *mem_acct;
        struct mem_header *header = NULL;

        if (!THIS->ctx->mem_acct_enable) {
                FREE (free_ptr);
                return;
        }

        if (!free_ptr)
                return;

        ptr = free_ptr - GF_MEM_HEADER_SIZE;
        header = (struct mem_header *) ptr;

        //Possible corruption, assert here
        GF_ASSERT (GF_MEM_HEADER_MAGIC == header->magic);

        mem_acct = header->mem_acct;
        if (!mem_acct) {
                goto free;
        }

        // This points to a memory overrun
        GF_ASSERT (GF_MEM_TRAILER_MAGIC ==
                *(uint32_t *)((char *)free_ptr + header->size));

        LOCK (&mem_acct->rec[header->type].lock);
        {
                mem_acct->rec[header->type].size -= header->size;
                mem_acct->rec[header->type].num_allocs--;
        }
        UNLOCK (&mem_acct->rec[header->type].lock);

        if (DECREMENT_ATOMIC (mem_acct->lock, mem_acct->refcnt) == 0) {
                FREE (mem_acct);
        }

free:
#ifdef DEBUG
        __gf_mem_invalidate (ptr);
#endif

        FREE (ptr);
}



struct mem_pool *
mem_pool_new_fn (unsigned long sizeof_type,
                 unsigned long count, char *name)
{
        struct mem_pool  *mem_pool = NULL;
        unsigned long     padded_sizeof_type = 0;
        GF_UNUSED void             *pool = NULL;
        GF_UNUSED int               i = 0;
        int               ret = 0;
        GF_UNUSED struct list_head *list = NULL;
        glusterfs_ctx_t  *ctx = NULL;

        if (!sizeof_type || !count) {
                gf_msg_callingfn ("mem-pool", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid argument");
                return NULL;
        }
        padded_sizeof_type = sizeof_type + GF_MEM_POOL_PAD_BOUNDARY;

        mem_pool = GF_CALLOC (sizeof (*mem_pool), 1, gf_common_mt_mem_pool);
        if (!mem_pool)
                return NULL;

        ret = gf_asprintf (&mem_pool->name, "%s:%s", THIS->name, name);
        if (ret < 0)
                return NULL;

        if (!mem_pool->name) {
                GF_FREE (mem_pool);
                return NULL;
        }

        LOCK_INIT (&mem_pool->lock);
        INIT_LIST_HEAD (&mem_pool->list);
        INIT_LIST_HEAD (&mem_pool->global_list);

        mem_pool->padded_sizeof_type = padded_sizeof_type;
        mem_pool->real_sizeof_type = sizeof_type;

#ifndef DEBUG
        mem_pool->cold_count = count;
        pool = GF_CALLOC (count, padded_sizeof_type, gf_common_mt_long);
        if (!pool) {
                GF_FREE (mem_pool->name);
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
#endif

        /* add this pool to the global list */
        ctx = THIS->ctx;
        if (!ctx)
                goto out;

        list_add (&mem_pool->global_list, &ctx->mempool_list);

out:
        return mem_pool;
}

void*
mem_get0 (struct mem_pool *mem_pool)
{
        void             *ptr = NULL;

        if (!mem_pool) {
                gf_msg_callingfn ("mem-pool", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid argument");
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
        struct mem_pool **pool_ptr = NULL;

        if (!mem_pool) {
                gf_msg_callingfn ("mem-pool", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid argument");
                return NULL;
        }

        LOCK (&mem_pool->lock);
        {
                mem_pool->alloc_count++;
                if (mem_pool->cold_count) {
                        list = mem_pool->list.next;
                        list_del (list);

                        mem_pool->hot_count++;
                        mem_pool->cold_count--;

                        if (mem_pool->max_alloc < mem_pool->hot_count)
                                mem_pool->max_alloc = mem_pool->hot_count;

                        ptr = list;
                        in_use = (ptr + GF_MEM_POOL_LIST_BOUNDARY +
                                  GF_MEM_POOL_PTR);
                        *in_use = 1;

                        goto fwd_addr_out;
                }

                /* This is a problem area. If we've run out of
                 * chunks in our slab above, we need to allocate
                 * enough memory to service this request.
                 * The problem is, these individual chunks will fail
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
                mem_pool->pool_misses++;
                mem_pool->curr_stdalloc++;
                if (mem_pool->max_stdalloc < mem_pool->curr_stdalloc)
                        mem_pool->max_stdalloc = mem_pool->curr_stdalloc;
                ptr = GF_CALLOC (1, mem_pool->padded_sizeof_type,
                                 gf_common_mt_mem_pool);

                /* Memory coming from the heap need not be transformed from a
                 * chunkhead to a usable pointer since it is not coming from
                 * the pool.
                 */
        }
fwd_addr_out:
        pool_ptr = mem_pool_from_ptr (ptr);
        *pool_ptr = (struct mem_pool *)mem_pool;
        ptr = mem_pool_chunkhead2ptr (ptr);
        UNLOCK (&mem_pool->lock);

        return ptr;
}


static int
__is_member (struct mem_pool *pool, void *ptr)
{
        if (!pool || !ptr) {
                gf_msg_callingfn ("mem-pool", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid argument");
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
mem_put (void *ptr)
{
        struct list_head *list = NULL;
        int    *in_use = NULL;
        void   *head = NULL;
        struct mem_pool **tmp = NULL;
        struct mem_pool *pool = NULL;

        if (!ptr) {
                gf_msg_callingfn ("mem-pool", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid argument");
                return;
        }

        list = head = mem_pool_ptr2chunkhead (ptr);
        tmp = mem_pool_from_ptr (head);
        if (!tmp) {
                gf_msg_callingfn ("mem-pool", GF_LOG_ERROR, 0,
                                  LG_MSG_PTR_HEADER_CORRUPTED,
                                  "ptr header is corrupted");
                return;
        }

        pool = *tmp;
        if (!pool) {
                gf_msg_callingfn ("mem-pool", GF_LOG_ERROR, 0,
                                  LG_MSG_MEMPOOL_PTR_NULL,
                                  "mem-pool ptr is NULL");
                return;
        }
        LOCK (&pool->lock);
        {

                switch (__is_member (pool, ptr))
                {
                case 1:
                        in_use = (head + GF_MEM_POOL_LIST_BOUNDARY +
                                  GF_MEM_POOL_PTR);
                        if (!is_mem_chunk_in_use(in_use)) {
                                gf_msg_callingfn ("mem-pool", GF_LOG_CRITICAL,
                                                  0,
                                                  LG_MSG_MEMPOOL_INVALID_FREE,
                                                  "mem_put called on freed ptr"
                                                  " %p of mem pool %p", ptr,
                                                  pool);
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
                        pool->curr_stdalloc--;
                        GF_FREE (list);
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

        gf_msg (THIS->name, GF_LOG_INFO, 0, LG_MSG_MEM_POOL_DESTROY, "size=%lu "
                "max=%d total=%"PRIu64, pool->padded_sizeof_type,
                pool->max_alloc, pool->alloc_count);

        list_del (&pool->global_list);

        LOCK_DESTROY (&pool->lock);
        GF_FREE (pool->name);
        GF_FREE (pool->pool);
        GF_FREE (pool);

        return;
}
