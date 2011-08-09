/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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


#include "iobuf.h"
#include "statedump.h"
#include <stdio.h>


/*
  TODO: implement destroy margins and prefetching of arenas
*/

void
__iobuf_arena_init_iobufs (struct iobuf_arena *iobuf_arena)
{
        size_t              arena_size = 0;
        size_t              page_size = 0;
        int                 iobuf_cnt = 0;
        struct iobuf       *iobuf = NULL;
        int                 offset = 0;
        int                 i = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_arena, out);

        arena_size = iobuf_arena->arena_size;
        page_size  = iobuf_arena->page_size;
        iobuf_cnt  = arena_size / page_size;

        iobuf_arena->iobufs = GF_CALLOC (sizeof (*iobuf), iobuf_cnt,
                                         gf_common_mt_iobuf);
        if (!iobuf_arena->iobufs)
                return;

        iobuf = iobuf_arena->iobufs;
        for (i = 0; i < iobuf_cnt; i++) {
                INIT_LIST_HEAD (&iobuf->list);
                LOCK_INIT (&iobuf->lock);

                iobuf->iobuf_arena = iobuf_arena;

                iobuf->ptr = iobuf_arena->mem_base + offset;

                list_add (&iobuf->list, &iobuf_arena->passive.list);
                iobuf_arena->passive_cnt++;

                offset += page_size;
                iobuf++;
        }

out:
        return;
}


void
__iobuf_arena_destroy_iobufs (struct iobuf_arena *iobuf_arena)
{
        size_t              arena_size = 0;
        size_t              page_size = 0;
        int                 iobuf_cnt = 0;
        struct iobuf       *iobuf = NULL;
        int                 i = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_arena, out);

        arena_size = iobuf_arena->arena_size;
        page_size  = iobuf_arena->page_size;
        iobuf_cnt  = arena_size / page_size;

        if (!iobuf_arena->iobufs) {
                gf_log_callingfn (THIS->name, GF_LOG_DEBUG, "iobufs not found");
                return;
        }

        iobuf = iobuf_arena->iobufs;
        for (i = 0; i < iobuf_cnt; i++) {
                GF_ASSERT (iobuf->ref == 0);

                list_del_init (&iobuf->list);
                iobuf++;
        }

        GF_FREE (iobuf_arena->iobufs);

out:
        return;
}


void
__iobuf_arena_destroy (struct iobuf_arena *iobuf_arena)
{
        struct iobuf_pool *iobuf_pool = NULL;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_arena, out);

        iobuf_pool = iobuf_arena->iobuf_pool;

        __iobuf_arena_destroy_iobufs (iobuf_arena);

        if (iobuf_arena->mem_base
            && iobuf_arena->mem_base != MAP_FAILED)
                munmap (iobuf_arena->mem_base, iobuf_pool->arena_size);

        GF_FREE (iobuf_arena);

out:
        return;
}


struct iobuf_arena *
__iobuf_arena_alloc (struct iobuf_pool *iobuf_pool, size_t page_size)
{
        struct iobuf_arena *iobuf_arena = NULL;
        size_t              arena_size = 0, rounded_size = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_pool, out);

        iobuf_arena = GF_CALLOC (sizeof (*iobuf_arena), 1,
                                 gf_common_mt_iobuf_arena);
        if (!iobuf_arena)
                goto err;

        INIT_LIST_HEAD (&iobuf_arena->list);
        INIT_LIST_HEAD (&iobuf_arena->active.list);
        INIT_LIST_HEAD (&iobuf_arena->passive.list);
        iobuf_arena->iobuf_pool = iobuf_pool;

        arena_size = iobuf_pool->arena_size;

        rounded_size = gf_roundup_power_of_two (page_size);
        iobuf_arena->page_size = rounded_size;

        if ((arena_size % rounded_size) != 0) {
                arena_size = (arena_size / rounded_size) * rounded_size;
        }

        iobuf_arena->arena_size = arena_size;

        iobuf_arena->mem_base = mmap (NULL, arena_size, PROT_READ|PROT_WRITE,
                                      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (iobuf_arena->mem_base == MAP_FAILED) {
                gf_log (THIS->name, GF_LOG_WARNING, "maping failed");
                goto err;
        }

        __iobuf_arena_init_iobufs (iobuf_arena);
        if (!iobuf_arena->iobufs) {
                gf_log (THIS->name, GF_LOG_DEBUG, "init failed");
                goto err;
        }

        iobuf_pool->arena_cnt++;

        return iobuf_arena;

err:
        __iobuf_arena_destroy (iobuf_arena);

out:
        return NULL;
}


struct iobuf_arena *
__iobuf_arena_unprune (struct iobuf_pool *iobuf_pool, size_t page_size)
{
        struct iobuf_arena *iobuf_arena  = NULL;
        struct iobuf_arena *tmp          = NULL;
        int                 index        = 0;
 
        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_pool, out);
 
        index = log_base2 (page_size);
        if (index > GF_VARIABLE_IOBUF_COUNT) {
                gf_log ("iobuf", GF_LOG_DEBUG, "no arena corresponding to "
                        "page_size (%"GF_PRI_SIZET") is present. max supported "
                        "size (%llu)", page_size,
                        1LL << GF_VARIABLE_IOBUF_COUNT);
                return NULL;
        }

        list_for_each_entry (tmp, &iobuf_pool->purge[index], list) {
                list_del_init (&tmp->list);
                iobuf_arena = tmp;
                break;
        }
out:
        return iobuf_arena;
}


struct iobuf_arena *
__iobuf_pool_add_arena (struct iobuf_pool *iobuf_pool, size_t page_size)
{
        struct iobuf_arena *iobuf_arena  = NULL;
        int                 index        = 0;
        uint32_t            rounded_size = 0;

        rounded_size = gf_roundup_power_of_two (page_size);

        index = log_base2 (rounded_size);
        if (index > GF_VARIABLE_IOBUF_COUNT) {
                gf_log ("iobuf", GF_LOG_DEBUG, "page_size %u of "
                        "iobufs in arena being added is greater than max "
                        "supported size (%llu)", rounded_size,
                        1ULL << GF_VARIABLE_IOBUF_COUNT);
                return NULL;
        }

        iobuf_arena = __iobuf_arena_unprune (iobuf_pool, rounded_size);

        if (!iobuf_arena)
                iobuf_arena = __iobuf_arena_alloc (iobuf_pool, rounded_size);

        if (!iobuf_arena) {
                gf_log (THIS->name, GF_LOG_WARNING, "arena not found");
                return NULL;
        }

        list_add_tail (&iobuf_arena->list, &iobuf_pool->arenas[index]);

        return iobuf_arena;
}


struct iobuf_arena *
iobuf_pool_add_arena (struct iobuf_pool *iobuf_pool, size_t page_size)
{
        struct iobuf_arena *iobuf_arena = NULL;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_pool, out);

        pthread_mutex_lock (&iobuf_pool->mutex);
        {
                iobuf_arena = __iobuf_pool_add_arena (iobuf_pool, page_size);
        }
        pthread_mutex_unlock (&iobuf_pool->mutex);

out:
        return iobuf_arena;
}


void
iobuf_pool_destroy (struct iobuf_pool *iobuf_pool)
{
        struct iobuf_arena *iobuf_arena = NULL;
        struct iobuf_arena *tmp         = NULL;
        int                 i           = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_pool, out);

        for (i = 0; i < GF_VARIABLE_IOBUF_COUNT; i++) {
                list_for_each_entry_safe (iobuf_arena, tmp,
                                          &iobuf_pool->arenas[i], list) {
                        list_del_init (&iobuf_arena->list);
                        iobuf_pool->arena_cnt--;
                        __iobuf_arena_destroy (iobuf_arena);
                }

        }

out:
        return;
}


struct iobuf_pool *
iobuf_pool_new (size_t arena_size, size_t page_size)
{
        struct iobuf_pool  *iobuf_pool = NULL;
        int                 i          = 0;
        unsigned long long  max_size   = 0;

        max_size = ((1ULL << (GF_VARIABLE_IOBUF_COUNT)) - 1);
        if ((arena_size < page_size) || (max_size < arena_size)) {
                gf_log ("", GF_LOG_WARNING,
                        "arena size (%zu) is less than page size(%zu)",
                        arena_size, page_size);
                goto out;
        }

        iobuf_pool = GF_CALLOC (sizeof (*iobuf_pool), 1,
                                gf_common_mt_iobuf_pool);
        if (!iobuf_pool)
                goto out;

        pthread_mutex_init (&iobuf_pool->mutex, NULL);
        for (i = 0; i < GF_VARIABLE_IOBUF_COUNT; i++) {
                INIT_LIST_HEAD (&iobuf_pool->arenas[i]);
                INIT_LIST_HEAD (&iobuf_pool->filled[i]);
                INIT_LIST_HEAD (&iobuf_pool->purge[i]);
        }

        iobuf_pool->arena_size = arena_size;
        iobuf_pool->default_page_size  = page_size;

        iobuf_pool_add_arena (iobuf_pool, page_size);
out:

        return iobuf_pool;
}


void
__iobuf_pool_prune (struct iobuf_pool *iobuf_pool)
{
        struct iobuf_arena *iobuf_arena = NULL;
        struct iobuf_arena *tmp = NULL;
        int                 i   = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_pool, out);

        for (i = 0; i < GF_VARIABLE_IOBUF_COUNT; i++) {
                if (list_empty (&iobuf_pool->arenas[i])) {
                        continue;
                }

                list_for_each_entry_safe (iobuf_arena, tmp,
                                          &iobuf_pool->purge[i], list) {
                        if (iobuf_arena->active_cnt)
                                continue;

                        list_del_init (&iobuf_arena->list);
                        iobuf_pool->arena_cnt--;

                        __iobuf_arena_destroy (iobuf_arena);
                }
        }

out:
        return;
}


void
iobuf_pool_prune (struct iobuf_pool *iobuf_pool)
{
        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_pool, out);

        pthread_mutex_lock (&iobuf_pool->mutex);
        {
                __iobuf_pool_prune (iobuf_pool);
        }
        pthread_mutex_unlock (&iobuf_pool->mutex);

out:
        return;
}


struct iobuf_arena *
__iobuf_select_arena (struct iobuf_pool *iobuf_pool, size_t page_size)
{
        struct iobuf_arena *iobuf_arena  = NULL;
        struct iobuf_arena *trav         = NULL;
        size_t              rounded_size = 0;
        int                 index        = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_pool, out);

        rounded_size = gf_roundup_power_of_two (page_size);

        index = log_base2 (rounded_size);
        if (index > GF_VARIABLE_IOBUF_COUNT) {
                gf_log ("iobuf", GF_LOG_DEBUG, "size of iobuf requested (%"
                        GF_PRI_SIZET") is greater than max supported size (%"
                        "llu)", rounded_size, 1ULL << GF_VARIABLE_IOBUF_COUNT);
                return NULL;
        }

        /* look for unused iobuf from the head-most arena */
        list_for_each_entry (trav, &iobuf_pool->arenas[index], list) {
                if (trav->passive_cnt) {
                        iobuf_arena = trav;
                        break;
                }
        }

        if (!iobuf_arena) {
                /* all arenas were full */
                iobuf_arena = __iobuf_pool_add_arena (iobuf_pool, rounded_size);
        }

out:
        return iobuf_arena;
}


struct iobuf *
__iobuf_ref (struct iobuf *iobuf)
{
        iobuf->ref++;

        return iobuf;
}


struct iobuf *
__iobuf_unref (struct iobuf *iobuf)
{
        iobuf->ref--;

        return iobuf;
}

struct iobuf *
__iobuf_get (struct iobuf_arena *iobuf_arena, size_t page_size)
{
        struct iobuf      *iobuf        = NULL;
        struct iobuf_pool *iobuf_pool   = NULL;
        int                index        = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_arena, out);

        iobuf_pool = iobuf_arena->iobuf_pool;

        list_for_each_entry (iobuf, &iobuf_arena->passive.list, list)
                break;

        list_del (&iobuf->list);
        iobuf_arena->passive_cnt--;

        list_add (&iobuf->list, &iobuf_arena->active.list);
        iobuf_arena->active_cnt++;

        if (iobuf_arena->passive_cnt == 0) {
                index = log_base2 (page_size);
                list_del (&iobuf_arena->list);
                list_add (&iobuf_arena->list, &iobuf_pool->filled[index]);
        }

out:
        return iobuf;
}

struct iobuf *
iobuf_get2 (struct iobuf_pool *iobuf_pool, size_t page_size)
{
        struct iobuf       *iobuf        = NULL;
        struct iobuf_arena *iobuf_arena  = NULL;
        size_t              rounded_size = 0;

        if (page_size == 0) {
                page_size = iobuf_pool->default_page_size;
        }

        rounded_size = gf_roundup_power_of_two (page_size);

        pthread_mutex_lock (&iobuf_pool->mutex);
        {
                /* most eligible arena for picking an iobuf */
                iobuf_arena = __iobuf_select_arena (iobuf_pool, rounded_size);
                if (!iobuf_arena)
                        goto unlock;

                iobuf = __iobuf_get (iobuf_arena, rounded_size);
                if (!iobuf)
                        goto unlock;

                __iobuf_ref (iobuf);
         }
unlock:
        pthread_mutex_unlock (&iobuf_pool->mutex);

        return iobuf;
}

struct iobuf *
iobuf_get (struct iobuf_pool *iobuf_pool)
{
        struct iobuf       *iobuf        = NULL;
        struct iobuf_arena *iobuf_arena  = NULL;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_pool, out);

        pthread_mutex_lock (&iobuf_pool->mutex);
        {
                /* most eligible arena for picking an iobuf */
                iobuf_arena = __iobuf_select_arena (iobuf_pool,
                                                    iobuf_pool->default_page_size);
                if (!iobuf_arena) {
                        gf_log (THIS->name, GF_LOG_WARNING, "arena not found");
                        goto unlock;
                }

                iobuf = __iobuf_get (iobuf_arena,
                                     iobuf_pool->default_page_size);
                if (!iobuf) {
                        gf_log (THIS->name, GF_LOG_WARNING, "iobuf not found");
                        goto unlock;
                }

                __iobuf_ref (iobuf);
        }
unlock:
        pthread_mutex_unlock (&iobuf_pool->mutex);

out:
        return iobuf;
}

void
__iobuf_put (struct iobuf *iobuf, struct iobuf_arena *iobuf_arena)
{
        struct iobuf_pool *iobuf_pool = NULL;
        int                index      = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_arena, out);
        GF_VALIDATE_OR_GOTO ("iobuf", iobuf, out);

        iobuf_pool = iobuf_arena->iobuf_pool;

        index = log_base2 (iobuf_arena->page_size);
        if (index > GF_VARIABLE_IOBUF_COUNT) {
                gf_log ("iobuf", GF_LOG_DEBUG, "size of iobuf being returned to"
                        " pool(%"GF_PRI_SIZET") is greater than max supported "
                        "size(%llu) arena = %p",
                        iobuf_arena->page_size, 1ULL << GF_VARIABLE_IOBUF_COUNT,
                        iobuf_arena);
                return;
        }

        if (iobuf_arena->passive_cnt == 0) {
                list_del (&iobuf_arena->list);
                list_add_tail (&iobuf_arena->list, &iobuf_pool->arenas[index]);
        }

        list_del_init (&iobuf->list);
        iobuf_arena->active_cnt--;

        list_add (&iobuf->list, &iobuf_arena->passive.list);
        iobuf_arena->passive_cnt++;

        if (iobuf_arena->active_cnt == 0) {
                list_del (&iobuf_arena->list);
                list_add_tail (&iobuf_arena->list, &iobuf_pool->purge[index]);
        }
out:
        return;
}


void
iobuf_put (struct iobuf *iobuf)
{
        struct iobuf_arena *iobuf_arena = NULL;
        struct iobuf_pool  *iobuf_pool = NULL;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf, out);

        iobuf_arena = iobuf->iobuf_arena;
        if (!iobuf_arena) {
                gf_log (THIS->name, GF_LOG_WARNING, "arena not found");
                return;
        }

        iobuf_pool = iobuf_arena->iobuf_pool;
        if (!iobuf_pool) {
                gf_log (THIS->name, GF_LOG_WARNING, "iobuf pool not found");
                return;
        }

        pthread_mutex_lock (&iobuf_pool->mutex);
        {
                __iobuf_put (iobuf, iobuf_arena);
        }
        pthread_mutex_unlock (&iobuf_pool->mutex);

        iobuf_pool_prune (iobuf_pool);

out:
        return;
}


void
iobuf_unref (struct iobuf *iobuf)
{
        int  ref = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf, out);

        LOCK (&iobuf->lock);
        {
                __iobuf_unref (iobuf);
                ref = iobuf->ref;
        }
        UNLOCK (&iobuf->lock);

        if (!ref)
                iobuf_put (iobuf);

out:
        return;
}


struct iobuf *
iobuf_ref (struct iobuf *iobuf)
{
        GF_VALIDATE_OR_GOTO ("iobuf", iobuf, out);

        LOCK (&iobuf->lock);
        {
                __iobuf_ref (iobuf);
        }
        UNLOCK (&iobuf->lock);

out:
        return iobuf;
}


struct iobref *
iobref_new ()
{
        struct iobref *iobref = NULL;

        iobref = GF_CALLOC (sizeof (*iobref), 1,
                            gf_common_mt_iobref);
        if (!iobref)
                return NULL;

        LOCK_INIT (&iobref->lock);

        iobref->ref++;

        return iobref;
}


struct iobref *
iobref_ref (struct iobref *iobref)
{
        GF_VALIDATE_OR_GOTO ("iobuf", iobref, out);

        LOCK (&iobref->lock);
        {
                iobref->ref++;
        }
        UNLOCK (&iobref->lock);

out:
        return iobref;
}


void
iobref_destroy (struct iobref *iobref)
{
        int            i = 0;
        struct iobuf  *iobuf = NULL;

        GF_VALIDATE_OR_GOTO ("iobuf", iobref, out);

        for (i = 0; i < 8; i++) {
                iobuf = iobref->iobrefs[i];

                iobref->iobrefs[i] = NULL;
                if (iobuf)
                        iobuf_unref (iobuf);
        }

        GF_FREE (iobref);

out:
        return;
}


void
iobref_unref (struct iobref *iobref)
{
        int ref = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobref, out);

        LOCK (&iobref->lock);
        {
                ref = (--iobref->ref);
        }
        UNLOCK (&iobref->lock);

        if (!ref)
                iobref_destroy (iobref);

out:
        return;
}


int
__iobref_add (struct iobref *iobref, struct iobuf *iobuf)
{
        int  i = 0;
        int  ret = -ENOMEM;

        GF_VALIDATE_OR_GOTO ("iobuf", iobref, out);
        GF_VALIDATE_OR_GOTO ("iobuf", iobuf, out);

        for (i = 0; i < 8; i++) {
                if (iobref->iobrefs[i] == NULL) {
                        iobref->iobrefs[i] = iobuf_ref (iobuf);
                        ret = 0;
                        break;
                }
        }

out:
        return ret;
}


int
iobref_add (struct iobref *iobref, struct iobuf *iobuf)
{
        int  ret = -EINVAL;

        GF_VALIDATE_OR_GOTO ("iobuf", iobref, out);
        GF_VALIDATE_OR_GOTO ("iobuf", iobuf, out);

        LOCK (&iobref->lock);
        {
                ret = __iobref_add (iobref, iobuf);
        }
        UNLOCK (&iobref->lock);

out:
        return ret;
}


int
iobref_merge (struct iobref *to, struct iobref *from)
{
        int           i = 0;
        int           ret = -1;
        struct iobuf *iobuf = NULL;

        GF_VALIDATE_OR_GOTO ("iobuf", to, out);
        GF_VALIDATE_OR_GOTO ("iobuf", from, out);

        LOCK (&from->lock);
        {
                for (i = 0; i < 8; i++) {
                        iobuf = from->iobrefs[i];

                        if (!iobuf)
                                break;

                        ret = iobref_add (to, iobuf);

                        if (ret < 0)
                                break;
                }
        }
        UNLOCK (&from->lock);

out:
        return ret;
}


size_t
iobuf_size (struct iobuf *iobuf)
{
        size_t size = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf, out);

        if (!iobuf->iobuf_arena) {
                gf_log (THIS->name, GF_LOG_WARNING, "arena not found");
                goto out;
        }

        if (!iobuf->iobuf_arena->iobuf_pool) {
                gf_log (THIS->name, GF_LOG_WARNING, "pool not found");
                goto out;
        }

        size = iobuf->iobuf_arena->page_size;
out:
        return size;
}


size_t
iobref_size (struct iobref *iobref)
{
        size_t size = 0;
        int    i = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobref, out);

        LOCK (&iobref->lock);
        {
                for (i = 0; i < 8; i++) {
                        if (iobref->iobrefs[i])
                                size += iobuf_size (iobref->iobrefs[i]);
                }
        }
        UNLOCK (&iobref->lock);

out:
        return size;
}

void
iobuf_info_dump (struct iobuf *iobuf, const char *key_prefix)
{
        char   key[GF_DUMP_MAX_BUF_LEN];
        struct iobuf my_iobuf;
        int    ret = 0;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf, out);

        memset(&my_iobuf, 0, sizeof(my_iobuf));

        ret = TRY_LOCK(&iobuf->lock);
        if (ret) {
                return;
        }
        memcpy(&my_iobuf, iobuf, sizeof(my_iobuf));
        UNLOCK(&iobuf->lock);

        gf_proc_dump_build_key(key, key_prefix,"ref");
        gf_proc_dump_write(key, "%d", my_iobuf.ref);
        gf_proc_dump_build_key(key, key_prefix,"ptr");
        gf_proc_dump_write(key, "%p", my_iobuf.ptr);

out:
        return;
}

void
iobuf_arena_info_dump (struct iobuf_arena *iobuf_arena, const char *key_prefix)
{
        char key[GF_DUMP_MAX_BUF_LEN];
        int  i = 1;
        struct iobuf *trav;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_arena, out);

        gf_proc_dump_build_key(key, key_prefix,"mem_base");
        gf_proc_dump_write(key, "%p", iobuf_arena->mem_base);
        gf_proc_dump_build_key(key, key_prefix, "active_cnt");
        gf_proc_dump_write(key, "%d", iobuf_arena->active_cnt);
        gf_proc_dump_build_key(key, key_prefix, "passive_cnt");
        gf_proc_dump_write(key, "%d", iobuf_arena->passive_cnt);
        list_for_each_entry (trav, &iobuf_arena->active.list, list) {
                gf_proc_dump_build_key(key, key_prefix,"active_iobuf.%d", i++);
                gf_proc_dump_add_section(key);
                iobuf_info_dump(trav, key);
        }

out:
        return;
}

void
iobuf_stats_dump (struct iobuf_pool *iobuf_pool)
{

        char               msg[1024];
        struct iobuf_arena *trav = NULL;
        int                i = 1;
        int                j = 0;
        int                ret = -1;

        GF_VALIDATE_OR_GOTO ("iobuf", iobuf_pool, out);

        memset(msg, 0, sizeof(msg));

        ret = pthread_mutex_trylock(&iobuf_pool->mutex);

        if (ret) {
                return;
        }
        gf_proc_dump_add_section("iobuf.global");
        gf_proc_dump_write("iobuf.global.iobuf_pool","%p", iobuf_pool);
        gf_proc_dump_write("iobuf.global.iobuf_pool.default_page_size", "%d",
                                                iobuf_pool->default_page_size);
        gf_proc_dump_write("iobuf.global.iobuf_pool.arena_size", "%d",
                           iobuf_pool->arena_size);
        gf_proc_dump_write("iobuf.global.iobuf_pool.arena_cnt", "%d",
                           iobuf_pool->arena_cnt);

        for (j = 0; j < GF_VARIABLE_IOBUF_COUNT; j++) {
                list_for_each_entry (trav, &iobuf_pool->arenas[j], list) {
                        snprintf(msg, sizeof(msg),
                                 "iobuf.global.iobuf_pool.arena.%d", i);
                        gf_proc_dump_add_section(msg);
                        iobuf_arena_info_dump(trav,msg);
                        i++;
                }

        }

        pthread_mutex_unlock(&iobuf_pool->mutex);

out:
        return;
}


void
iobuf_to_iovec(struct iobuf *iob, struct iovec *iov)
{
        GF_VALIDATE_OR_GOTO ("iobuf", iob, out);
        GF_VALIDATE_OR_GOTO ("iobuf", iov, out);

        iov->iov_base = iobuf_ptr (iob);
        iov->iov_len =  iobuf_pagesize (iob);

out:
        return;
}
