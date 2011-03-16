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


struct mem_acct {
        uint32_t            num_types;
        struct mem_acct_rec     *rec;
};

struct mem_acct_rec {
        size_t          size;
        size_t          max_size;
        uint32_t        num_allocs;
        uint32_t        max_num_allocs;
        gf_lock_t       lock;
};


void *
__gf_calloc (size_t cnt, size_t size, uint32_t type);

void *
__gf_malloc (size_t size, uint32_t type);

void *
__gf_realloc (void *ptr, size_t size);

int
gf_vasprintf (char **string_ptr, const char *format, va_list arg);

int
gf_asprintf (char **string_ptr, const char *format, ...);

void
__gf_free (void *ptr);


static inline
void* __gf_default_malloc (size_t size)
{
        void *ptr = NULL;

        ptr = malloc (size);
        if (!ptr)
                gf_log_nomem ("", GF_LOG_ALERT, size);

        return ptr;
}

static inline
void* __gf_default_calloc (int cnt, size_t size)
{
        void *ptr = NULL;

        ptr = calloc (cnt, size);
        if (!ptr)
                gf_log_nomem ("", GF_LOG_ALERT, (cnt * size));

        return ptr;
}

static inline
void* __gf_default_realloc (void *oldptr, size_t size)
{
        void *ptr = NULL;

        ptr = realloc (oldptr, size);
        if (!ptr)
                gf_log_nomem ("", GF_LOG_ALERT, size);

        return ptr;
}

#define MALLOC(size)       __gf_default_malloc(size)
#define CALLOC(cnt,size)   __gf_default_calloc(cnt,size)
#define REALLOC(ptr,size)  __gf_default_realloc(ptr,size)

#define FREE(ptr)                               \
        if (ptr != NULL) {                      \
                free ((void *)ptr);             \
                ptr = (void *)0xeeeeeeee;       \
        }

#define GF_CALLOC(nmemb, size, type) __gf_calloc (nmemb, size, type)

#define GF_MALLOC(size, type)  __gf_malloc (size, type)

#define GF_REALLOC(ptr, size)  __gf_realloc (ptr, size)

#define GF_FREE(free_ptr) __gf_free (free_ptr);

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
void *mem_get0 (struct mem_pool *pool);

void mem_pool_destroy (struct mem_pool *pool);

int gf_mem_acct_is_enabled ();
void gf_mem_acct_enable_set ();

#endif /* _MEM_POOL_H */
