/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _MEMORY_H_
#define _MEMORY_H_

#include "glusterfs/list.h"
#include "glusterfs/atomic.h"
#include "glusterfs/logging.h"
#include "glusterfs/mem-types.h"
#include "glusterfs/glusterfs.h" /* for glusterfs_ctx_t */
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

static inline void *
__gf_default_malloc(size_t size)
{
    void *ptr = NULL;

    ptr = malloc(size);
    if (!ptr)
        gf_msg_nomem("", GF_LOG_ALERT, size);

    return ptr;
}

static inline void *
__gf_default_calloc(int cnt, size_t size)
{
    void *ptr = NULL;

    ptr = calloc(cnt, size);
    if (!ptr)
        gf_msg_nomem("", GF_LOG_ALERT, (cnt * size));

    return ptr;
}

static inline void *
__gf_default_realloc(void *oldptr, size_t size)
{
    void *ptr = NULL;

    ptr = realloc(oldptr, size);
    if (!ptr)
        gf_msg_nomem("", GF_LOG_ALERT, size);

    return ptr;
}

static inline void
__gf_default_free(void *ptr)
{
    if (ptr)
        free(ptr);
}

#define MALLOC(size) __gf_default_malloc(size)

#define CALLOC(cnt, size) __gf_default_calloc(cnt, size)

#define REALLOC(ptr, size) __gf_default_realloc(ptr, size)

#define FREE(ptr) __gf_default_free(ptr)

#ifdef GF_DISABLE_ALLOCATION_TRACKING

#ifdef DEBUG

static inline void
gf_mem_acct_enable_set(void *arg)
{
}

#endif /* DEBUG */

static inline void
gf_mem_acct_dump_details(char *type, char *name, void *arg, int fd)
{
}

#define __gf_calloc(cnt, size, type, typestr) __gf_default_calloc(cnt, size)

#define __gf_malloc(size, type, typestr) __gf_default_malloc(size)

#define __gf_realloc(ptr, size) __gf_default_realloc(ptr, size)

#define __gf_free(ptr) __gf_default_free(ptr)

#else /* not GF_DISABLE_ALLOCATION_TRACKING */

struct mem_acct;

struct mem_acct *
gf_mem_acct_init(size_t ntypes);

int
gf_mem_acct_fini(struct mem_acct *acct);

void
gf_mem_acct_enable_set(glusterfs_ctx_t *ctx);

void
gf_mem_acct_dump_details(char *type, char *name, struct mem_acct *acct, int fd);

void *
__gf_calloc(size_t cnt, size_t size, uint32_t type, const char *typestr);

void *
__gf_malloc(size_t size, uint32_t type, const char *typestr);

void *
__gf_realloc(void *ptr, size_t size);

void
__gf_free(void *ptr);

#endif /* GF_DISABLE_ALLOCATION_TRACKING */

#define GF_CALLOC(nmemb, size, type) __gf_calloc(nmemb, size, type, #type)

#define GF_MALLOC(size, type) __gf_malloc(size, type, #type)

#define GF_REALLOC(ptr, size) __gf_realloc(ptr, size)

#define GF_FREE(free_ptr) __gf_free(free_ptr)

/* Old memory pool compatibility code. */

struct mem_pool {
    size_t sizeof_type;
};

#define mem_get(mem_pool)                                                      \
    GF_MALLOC((unsigned long)mem_pool, gf_common_mt_mem_pool)

#define mem_get0(mem_pool)                                                     \
    GF_CALLOC(1, (unsigned long)mem_pool, gf_common_mt_mem_pool)

#define mem_put(ptr) GF_FREE(ptr)

static inline struct mem_pool *
mem_pool_new_fn(glusterfs_ctx_t *ctx, size_t sizeof_type, size_t count,
                char *name)
{
    return (struct mem_pool *)(sizeof_type);
}

#define mem_pool_new(type, count)                                              \
    mem_pool_new_fn(THIS->ctx, sizeof(type), count, #type)

#define mem_pool_new_ctx(ctx, type, count)                                     \
    mem_pool_new_fn(ctx, sizeof(type), count, #type)

/* Since memory pools are gone, COUNT is meaningless. */
#define mem_pool_stub(type) \
    mem_pool_new_fn(THIS->ctx, sizeof(type), 0, #type)

static inline void
mem_pool_destroy(struct mem_pool *pool)
{
}

/* Misc utility functions. */

static inline char *
gf_strndup(const char *src, size_t len)
{
    char *dup_str = NULL;

    if (!src) {
        goto out;
    }

    dup_str = GF_MALLOC(len + 1, gf_common_mt_strdup);
    if (!dup_str) {
        goto out;
    }

    memcpy(dup_str, src, len);
    dup_str[len] = '\0';
out:
    return dup_str;
}

static inline char *
gf_strdup(const char *src)
{
    if (!src)
        return NULL;

    return gf_strndup(src, strlen(src));
}

static inline void *
gf_memdup(const void *src, size_t size)
{
    void *dup_mem = NULL;

    dup_mem = GF_MALLOC(size, gf_common_mt_memdup);
    if (!dup_mem)
        goto out;

    memcpy(dup_mem, src, size);

out:
    return dup_mem;
}

int
gf_vasprintf(char **string_ptr, const char *format, va_list arg);

int
gf_asprintf(char **string_ptr, const char *format, ...)
    __attribute__((__format__(__printf__, 2, 3)));

#endif /* _MEMORY_H_ */
