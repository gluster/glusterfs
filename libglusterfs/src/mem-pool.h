/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _MEM_POOL_H_
#define _MEM_POOL_H_

#include "list.h"
#include "locking.h"
#include "logging.h"
#include "mem-types.h"
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <stdarg.h>

/*
 * Need this for unit tests since inline functions
 * access memory allocation and need to use the
 * unit test versions
 */
#ifdef UNIT_TESTING
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#endif

#define GF_MEM_TRAILER_SIZE 8
#define GF_MEM_HEADER_MAGIC  0xCAFEBABE
#define GF_MEM_TRAILER_MAGIC 0xBAADF00D
#define GF_MEM_INVALID_MAGIC 0xDEADC0DE

struct mem_acct_rec {
	const char     *typestr;
        size_t          size;
        size_t          max_size;
        uint32_t        num_allocs;
        uint32_t        total_allocs;
        uint32_t        max_num_allocs;
        gf_lock_t       lock;
};

struct mem_acct {
        uint32_t            num_types;
        /*
         * The lock is only used on ancient platforms (e.g. RHEL5) to keep
         * refcnt increment/decrement atomic.  We could even make its existence
         * conditional on the right set of version/feature checks, but it's so
         * lightweight that it's not worth the obfuscation.
         */
        gf_lock_t           lock;
        unsigned int        refcnt;
        struct mem_acct_rec rec[0];
};

struct mem_header {
        uint32_t        type;
        size_t          size;
        struct mem_acct *mem_acct;
        uint32_t        magic;
        int             padding[8];
};

#define GF_MEM_HEADER_SIZE  (sizeof (struct mem_header))

#ifdef DEBUG
struct mem_invalid {
        uint32_t  magic;
        void     *mem_acct;
        uint32_t  type;
        size_t    size;
        void     *baseaddr;
};
#endif

void *
__gf_calloc (size_t cnt, size_t size, uint32_t type, const char *typestr);

void *
__gf_malloc (size_t size, uint32_t type, const char *typestr);

void *
__gf_realloc (void *ptr, size_t size);

int
gf_vasprintf (char **string_ptr, const char *format, va_list arg);

int
gf_asprintf (char **string_ptr, const char *format, ...);

void
__gf_free (void *ptr);

int
gf_get_mem_type (void *ptr);

static inline
void* __gf_default_malloc (size_t size)
{
        void *ptr = NULL;

        ptr = malloc (size);
        if (!ptr)
                gf_msg_nomem ("", GF_LOG_ALERT, size);

        return ptr;
}

static inline
void* __gf_default_calloc (int cnt, size_t size)
{
        void *ptr = NULL;

        ptr = calloc (cnt, size);
        if (!ptr)
                gf_msg_nomem ("", GF_LOG_ALERT, (cnt * size));

        return ptr;
}

static inline
void* __gf_default_realloc (void *oldptr, size_t size)
{
        void *ptr = NULL;

        ptr = realloc (oldptr, size);
        if (!ptr)
                gf_msg_nomem ("", GF_LOG_ALERT, size);

        return ptr;
}

#define MALLOC(size)       __gf_default_malloc(size)
#define CALLOC(cnt,size)   __gf_default_calloc(cnt,size)
#define REALLOC(ptr,size)  __gf_default_realloc(ptr,size)

#define FREE(ptr)                                       \
        do {                                            \
                if (ptr != NULL) {                      \
                        free ((void *)ptr);             \
                        ptr = (void *)0xeeeeeeee;       \
                }                                       \
        } while (0)

#define GF_CALLOC(nmemb, size, type) __gf_calloc (nmemb, size, type, #type)

#define GF_MALLOC(size, type)  __gf_malloc (size, type, #type)

#define GF_REALLOC(ptr, size)  __gf_realloc (ptr, size)

#define GF_FREE(free_ptr) __gf_free (free_ptr)

static inline
char *gf_strndup (const char *src, size_t len)
{
        char *dup_str = NULL;

        if (!src) {
                goto out;
        }

        dup_str = GF_CALLOC (1, len + 1, gf_common_mt_strdup);
        if (!dup_str) {
                goto out;
        }

        memcpy (dup_str, src, len);
out:
        return dup_str;
}

static inline
char * gf_strdup (const char *src)
{

        char    *dup_str = NULL;
        size_t  len = 0;

        len = strlen (src) + 1;

        dup_str = GF_CALLOC(1, len, gf_common_mt_strdup);

        if (!dup_str)
                return NULL;

        memcpy (dup_str, src, len);

        return dup_str;
}

static inline void *
gf_memdup (const void *src, size_t size)
{
        void *dup_mem = NULL;

        dup_mem = GF_CALLOC(1, size, gf_common_mt_strdup);
        if (!dup_mem)
                goto out;

        memcpy (dup_mem, src, size);

out:
        return dup_mem;
}

typedef struct pooled_obj_hdr {
        unsigned long                   magic;
        struct pooled_obj_hdr           *next;
        struct per_thread_pool_list     *pool_list;
        unsigned int                    power_of_two;
} pooled_obj_hdr_t;

#define AVAILABLE_SIZE(p2)      ((1 << (p2)) - sizeof(pooled_obj_hdr_t))

typedef struct per_thread_pool {
        /* This never changes, so doesn't need a lock. */
        struct mem_pool         *parent;
        /* Everything else is protected by our own lock. */
        pooled_obj_hdr_t        *hot_list;
        pooled_obj_hdr_t        *cold_list;
} per_thread_pool_t;

typedef struct per_thread_pool_list {
        /*
         * These first two members are protected by the global pool lock.  When
         * a thread first tries to use any pool, we create one of these.  We
         * link it into the global list using thr_list so the pool-sweeper
         * thread can find it, and use pthread_setspecific so this thread can
         * find it.  When the per-thread destructor runs, we "poison" the pool
         * list to prevent further allocations.  This also signals to the
         * pool-sweeper thread that the list should be detached and freed after
         * the next time it's swept.
         */
        struct list_head        thr_list;
        unsigned int            poison;
        /*
         * There's really more than one pool, but the actual number is hidden
         * in the implementation code so we just make it a single-element array
         * here.
         */
        pthread_spinlock_t      lock;
        per_thread_pool_t       pools[1];
} per_thread_pool_list_t;

struct mem_pool {
        unsigned int            power_of_two;
        /*
         * Updates to these are *not* protected by a global lock, so races
         * could occur and the numbers might be slightly off.  Don't expect
         * them to line up exactly.  It's the general trends that matter, and
         * it's not worth the locked-bus-cycle overhead to make these precise.
         */
        unsigned long           allocs_hot;
        unsigned long           allocs_cold;
        unsigned long           allocs_stdc;
        unsigned long           frees_to_list;
};

void mem_pools_init (void);

struct mem_pool *
mem_pool_new_fn (unsigned long sizeof_type, unsigned long count, char *name);

#define mem_pool_new(type,count) mem_pool_new_fn (sizeof(type), count, #type)

void mem_put (void *ptr);
void *mem_get (struct mem_pool *pool);
void *mem_get0 (struct mem_pool *pool);

void mem_pool_destroy (struct mem_pool *pool);

void gf_mem_acct_enable_set (void *ctx);

#endif /* _MEM_POOL_H */
