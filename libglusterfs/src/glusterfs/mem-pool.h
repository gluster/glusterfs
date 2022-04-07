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

#include "glusterfs/list.h"
#include "glusterfs/atomic.h"
#include "glusterfs/logging.h"
#include "glusterfs/mem-types.h"
#include "glusterfs/glusterfs.h" /* for glusterfs_ctx_t */
#include <urcu/compiler.h>       /* for caa_likely/unlikely() */
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

typedef uint32_t gf_mem_magic_t;
#define GF_MEM_TRAILER_SIZE sizeof(gf_mem_magic_t)
#define GF_MEM_HEADER_MAGIC 0xCAFEBABE
#define GF_MEM_TRAILER_MAGIC 0xBAADF00D
#define GF_MEM_INVALID_MAGIC 0xDEADC0DE

#define POOL_SMALLEST 7 /* i.e. 128 */
#define POOL_LARGEST 20 /* i.e. 1048576 */
#define NPOOLS (POOL_LARGEST - POOL_SMALLEST + 1)

struct mem_acct_rec {
    const char *typestr;
    gf_atomic_t num_allocs;
#ifdef DEBUG
    uint64_t size;
    uint64_t max_size;
    uint32_t max_num_allocs;
    gf_lock_t lock;
    struct list_head obj_list;
#endif
};

struct mem_acct {
    uint32_t num_types;
    gf_atomic_t refcnt;
    struct mem_acct_rec rec[];
};

struct mem_header {
    struct mem_acct *mem_acct;
    size_t size;
    uint32_t type;
    gf_mem_magic_t magic;
#ifdef DEBUG
    struct list_head acct_list;
#endif
    /* ensures alignment */
    void *data[];
};

#define GF_MEM_HEADER_SIZE (sizeof(struct mem_header))

#ifdef DEBUG
struct mem_invalid {
    gf_mem_magic_t magic;
    void *mem_acct;
    uint32_t type;
    size_t size;
    void *baseaddr;
};
#endif

void *
__gf_calloc(size_t cnt, size_t size, uint32_t type, const char *typestr);

void *
__gf_malloc(size_t size, uint32_t type, const char *typestr);

void *
__gf_realloc(void *ptr, size_t size);

int
gf_vasprintf(char **string_ptr, const char *format, va_list arg);

int
gf_asprintf(char **string_ptr, const char *format, ...)
    __attribute__((__format__(__printf__, 2, 3)));

void
__gf_free(void *ptr);

static inline void *
__gf_default_malloc(size_t size)
{
    void *ptr = NULL;

    ptr = malloc(size);
    if (caa_unlikely(!ptr))
        gf_msg_nomem("", GF_LOG_ALERT, size);

    return ptr;
}

static inline void *
__gf_default_calloc(int cnt, size_t size)
{
    void *ptr = NULL;

    ptr = calloc(cnt, size);
    if (caa_unlikely(!ptr))
        gf_msg_nomem("", GF_LOG_ALERT, (cnt * size));

    return ptr;
}

static inline void *
__gf_default_realloc(void *oldptr, size_t size)
{
    void *ptr = NULL;

    ptr = realloc(oldptr, size);
    if (caa_unlikely(!ptr))
        gf_msg_nomem("", GF_LOG_ALERT, size);

    return ptr;
}

#define MALLOC(size) __gf_default_malloc(size)
#define CALLOC(cnt, size) __gf_default_calloc(cnt, size)
#define REALLOC(ptr, size) __gf_default_realloc(ptr, size)

#define FREE(ptr)                                                              \
    do {                                                                       \
        if (ptr != NULL) {                                                     \
            free((void *)ptr);                                                 \
            ptr = (void *)0xeeeeeeee;                                          \
        }                                                                      \
    } while (0)

#define GF_CALLOC(nmemb, size, type) __gf_calloc(nmemb, size, type, #type)

#define GF_MALLOC(size, type) __gf_malloc(size, type, #type)

#define GF_REALLOC(ptr, size) __gf_realloc(ptr, size)

#define GF_FREE(free_ptr) __gf_free(free_ptr)

/* If mempool is disabled, mem_pool represents a object size.The object size
   type cast((struct mem_pool *) at the time of calling mem_pool_new_fn and at
   the time access original mem_pool value uncast same object type.If mempool is
   enabled it is a mem_pool object created by a mem_pool_new_fn.
*/
#if defined(GF_DISABLE_MEMPOOL)
#define mem_get(mem_pool)                                                      \
    GF_MALLOC((unsigned long)mem_pool, gf_common_mt_mem_pool);
#define mem_get0(mem_pool)                                                     \
    GF_CALLOC(1, (unsigned long)mem_pool, gf_common_mt_mem_pool);
#define mem_put(ptr) GF_FREE(ptr);
#else
#define mem_get(mem_pool) mem_get_malloc(mem_pool);
#define mem_get0(mem_pool) mem_get_calloc(mem_pool);
#define mem_put(ptr) mem_put_pool(ptr);
#endif

static inline char *
gf_strndup(const char *src, size_t len)
{
    char *dup_str = NULL;

    if (caa_unlikely(!src)) {
        goto out;
    }

    dup_str = GF_MALLOC(len + 1, gf_common_mt_strdup);
    if (caa_likely(dup_str)) {
        memcpy(dup_str, src, len);
        dup_str[len] = '\0';
    }
out:
    return dup_str;
}

static inline char *
gf_strdup(const char *src)
{
    if (caa_unlikely(!src))
        return NULL;

    return gf_strndup(src, strlen(src));
}

static inline void *
gf_memdup(const void *src, size_t size)
{
    void *dup_mem = NULL;

    dup_mem = GF_MALLOC(size, gf_common_mt_memdup);
    if (caa_likely(dup_mem))
        memcpy(dup_mem, src, size);

    return dup_mem;
}

#ifdef GF_DISABLE_MEMPOOL

/* No-op memory pool enough to fit current API without massive redesign. */

struct mem_pool {
    unsigned long sizeof_type;
};

#define mem_pools_init()                                                       \
    do {                                                                       \
    } while (0)
#define mem_pools_fini()                                                       \
    do {                                                                       \
    } while (0)
#define mem_pool_thread_destructor(pool_list) (void)pool_list

#else /* !GF_DISABLE_MEMPOOL */

/* kind of 'header' for the actual mem_pool_shared structure, this might make
 * it possible to dump some more details in a statedump */
struct mem_pool {
    /* object size, without pooled_obj_hdr_t */
    unsigned long sizeof_type;
    unsigned long count; /* requested pool size (unused) */
    char *name;
    char *xl_name;
    gf_atomic_t active;     /* current allocations */
#ifdef DEBUG
    gf_atomic_t hit;        /* number of allocations served from pt_pool */
    gf_atomic_t miss;       /* number of std allocs due to miss */
#endif
    struct list_head owner; /* glusterfs_ctx_t->mempool_list */
    glusterfs_ctx_t *ctx;   /* take ctx->lock when updating owner */

    struct mem_pool_shared *pool; /* the initial pool that was returned */
};

typedef struct pooled_obj_hdr {
    unsigned long magic;
    struct pooled_obj_hdr *next;
    struct per_thread_pool_list *pool_list;
    unsigned int power_of_two;

    /* track the pool that was used to request this object */
    struct mem_pool *pool;
} pooled_obj_hdr_t;

/* Each memory block inside a pool has a fixed size that is a power of two.
 * However each object will have a header that will reduce the available
 * space. */
#define AVAILABLE_SIZE(p2) ((1UL << (p2)) - sizeof(pooled_obj_hdr_t))

typedef struct per_thread_pool {
    /* the pool that was used to request this allocation */
    struct mem_pool_shared *parent;
    /* Everything else is protected by our own lock. */
    pooled_obj_hdr_t *hot_list;
    pooled_obj_hdr_t *cold_list;
} per_thread_pool_t;

typedef struct per_thread_pool_list {
    /* thr_list is used to place the TLS pool_list into the active global list
     * (pool_threads) or the inactive global list (pool_free_threads). It's
     * protected by the global pool_lock. */
    struct list_head thr_list;

    /* This lock is used to update poison and the hot/cold lists of members
     * of 'pools' array. */
    pthread_spinlock_t lock;

    /* This field is used to mark a pool_list as not being owned by any thread.
     * This means that the sweeper thread won't be cleaning objects stored in
     * its pools. mem_put() uses it to decide if the object being released is
     * placed into its original pool_list or directly destroyed. */
    bool poison;

    /*
     * There's really more than one pool, but the actual number is hidden
     * in the implementation code so we just make it a single-element array
     * here.
     */
    per_thread_pool_t pools[];
} per_thread_pool_list_t;

/* actual pool structure, shared between different mem_pools */
struct mem_pool_shared {
    unsigned int power_of_two;
};

void
mem_pools_init(void); /* start the pool_sweeper thread */
void
mem_pools_fini(void); /* cleanup memory pools */
void
mem_pool_thread_destructor(per_thread_pool_list_t *pool_list);

#endif /* GF_DISABLE_MEMPOOL */

struct mem_pool *
mem_pool_new_fn(glusterfs_ctx_t *ctx, unsigned long sizeof_type,
                unsigned long count, char *name);

#define mem_pool_new(type, count)                                              \
    mem_pool_new_fn(THIS->ctx, sizeof(type), count, #type)

#define mem_pool_new_ctx(ctx, type, count)                                     \
    mem_pool_new_fn(ctx, sizeof(type), count, #type)

void
mem_put_pool(void *ptr);
void *
mem_get_malloc(struct mem_pool *pool);
void *
mem_get_calloc(struct mem_pool *pool);

void
mem_pool_destroy(struct mem_pool *pool);

void
gf_mem_acct_enable_set(void *ctx);

#endif /* _MEM_POOL_H */
