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


#include "iobuf.h"
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

        arena_size = iobuf_arena->iobuf_pool->arena_size;
        page_size  = iobuf_arena->iobuf_pool->page_size;
        iobuf_cnt  = arena_size / page_size;

        iobuf_arena->iobufs = CALLOC (sizeof (*iobuf), iobuf_cnt);
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
}


void
__iobuf_arena_destroy_iobufs (struct iobuf_arena *iobuf_arena)
{
        size_t              arena_size = 0;
        size_t              page_size = 0;
        int                 iobuf_cnt = 0;
        struct iobuf       *iobuf = NULL;
        int                 i = 0;

        arena_size = iobuf_arena->iobuf_pool->arena_size;
        page_size  = iobuf_arena->iobuf_pool->page_size;
        iobuf_cnt  = arena_size / page_size;

        iobuf = iobuf_arena->iobufs;
        for (i = 0; i < iobuf_cnt; i++) {
                assert (iobuf->ref == 0);

                list_del_init (&iobuf->list);
                iobuf++;
        }

        FREE (iobuf_arena->iobufs);
}


void
__iobuf_arena_destroy (struct iobuf_arena *iobuf_arena)
{
        struct iobuf_pool *iobuf_pool = NULL;

        if (!iobuf_arena)
                return;

        iobuf_pool = iobuf_arena->iobuf_pool;

        __iobuf_arena_destroy_iobufs (iobuf_arena);

        if (iobuf_arena->mem_base)
                munmap (iobuf_arena->mem_base, iobuf_pool->arena_size);

        FREE (iobuf_arena);
}


struct iobuf_arena *
__iobuf_arena_alloc (struct iobuf_pool *iobuf_pool)
{
        struct iobuf_arena *iobuf_arena = NULL;
        size_t              arena_size = 0;

        iobuf_arena = CALLOC (sizeof (*iobuf_arena), 1);
        if (!iobuf_arena)
                goto err;

        INIT_LIST_HEAD (&iobuf_arena->list);
        INIT_LIST_HEAD (&iobuf_arena->active.list);
        INIT_LIST_HEAD (&iobuf_arena->passive.list);
        iobuf_arena->iobuf_pool = iobuf_pool;

        arena_size = iobuf_pool->arena_size;
        iobuf_arena->mem_base = mmap (NULL, arena_size, PROT_READ|PROT_WRITE,
                                      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (iobuf_arena->mem_base == MAP_FAILED)
                goto err;

        __iobuf_arena_init_iobufs (iobuf_arena);
        if (!iobuf_arena->iobufs)
                goto err;

        return iobuf_arena;

err:
        __iobuf_arena_destroy (iobuf_arena);
        return NULL;
}


struct iobuf_arena *
__iobuf_pool_add_arena (struct iobuf_pool *iobuf_pool)
{
        struct iobuf_arena *iobuf_arena = NULL;

        iobuf_arena = __iobuf_arena_alloc (iobuf_pool);

        if (!iobuf_arena)
                return NULL;

        list_add_tail (&iobuf_arena->list, &iobuf_pool->arenas.list);
        iobuf_pool->arena_cnt++;

        return iobuf_arena;
}


struct iobuf_arena *
iobuf_pool_add_arena (struct iobuf_pool *iobuf_pool)
{
        struct iobuf_arena *iobuf_arena = NULL;

        pthread_mutex_lock (&iobuf_pool->mutex);
        {
                iobuf_arena = __iobuf_pool_add_arena (iobuf_pool);
        }
        pthread_mutex_unlock (&iobuf_pool->mutex);

        return iobuf_arena;
}


void
iobuf_pool_destroy (struct iobuf_pool *iobuf_pool)
{
        struct iobuf_arena *iobuf_arena = NULL;
        struct iobuf_arena *tmp = NULL;

        if (!iobuf_pool)
                return;

        list_for_each_entry_safe (iobuf_arena, tmp, &iobuf_pool->arenas.list,
                                  list) {

                list_del_init (&iobuf_arena->list);
                iobuf_pool->arena_cnt--;

                __iobuf_arena_destroy (iobuf_arena);
        }
}


struct iobuf_pool *
iobuf_pool_new (size_t arena_size, size_t page_size)
{
        struct iobuf_pool  *iobuf_pool = NULL;

        if (arena_size < page_size)
                return NULL;

        iobuf_pool = CALLOC (sizeof (*iobuf_pool), 1);
        if (!iobuf_pool)
                return NULL;

        pthread_mutex_init (&iobuf_pool->mutex, NULL);
        INIT_LIST_HEAD (&iobuf_pool->arenas.list);
        INIT_LIST_HEAD (&iobuf_pool->filled.list);
        INIT_LIST_HEAD (&iobuf_pool->purge.list);

        iobuf_pool->arena_size = arena_size;
        iobuf_pool->page_size  = page_size;

        iobuf_pool_add_arena (iobuf_pool);

        return iobuf_pool;
}



void
__iobuf_pool_prune (struct iobuf_pool *iobuf_pool)
{
        struct iobuf_arena *iobuf_arena = NULL;
        struct iobuf_arena *tmp = NULL;

        list_for_each_entry_safe (iobuf_arena, tmp, &iobuf_pool->purge.list,
                                  list) {
                if (iobuf_arena->active_cnt)
                        continue;

                list_del_init (&iobuf_arena->list);
                iobuf_pool->arena_cnt--;

                __iobuf_arena_destroy (iobuf_arena);
        }
}


void
iobuf_pool_prune (struct iobuf_pool *iobuf_pool)
{
        pthread_mutex_lock (&iobuf_pool->mutex);
        {
                __iobuf_pool_prune (iobuf_pool);
        }
        pthread_mutex_unlock (&iobuf_pool->mutex);
}


struct iobuf_arena *
__iobuf_select_arena (struct iobuf_pool *iobuf_pool)
{
        struct iobuf_arena *iobuf_arena = NULL;
        struct iobuf_arena *trav = NULL;

        /* look for unused iobuf from the head-most arena */
        list_for_each_entry (trav, &iobuf_pool->arenas.list, list) {
                if (trav->passive_cnt) {
                        iobuf_arena = trav;
                        break;
                }
        }

        if (!iobuf_arena) {
                /* all arenas were full */
                iobuf_arena = __iobuf_pool_add_arena (iobuf_pool);
        }

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
__iobuf_get (struct iobuf_arena *iobuf_arena)
{
        struct iobuf      *iobuf = NULL;
        struct iobuf_pool *iobuf_pool = NULL;

        iobuf_pool = iobuf_arena->iobuf_pool;

        list_for_each_entry (iobuf, &iobuf_arena->passive.list, list)
                break;

        list_del (&iobuf->list);
        iobuf_arena->passive_cnt--;

        list_add (&iobuf->list, &iobuf_arena->active.list);
        iobuf_arena->active_cnt++;

        if (iobuf_arena->passive_cnt == 0) {
                list_del (&iobuf_arena->list);
                list_add (&iobuf_arena->list, &iobuf_pool->filled.list);
        }

        return iobuf;
}


struct iobuf *
iobuf_get (struct iobuf_pool *iobuf_pool)
{
        struct iobuf       *iobuf = NULL;
        struct iobuf_arena *iobuf_arena = NULL;

        pthread_mutex_lock (&iobuf_pool->mutex);
        {
                /* most eligible arena for picking an iobuf */
                iobuf_arena = __iobuf_select_arena (iobuf_pool);
                if (!iobuf_arena)
                        goto unlock;

                iobuf = __iobuf_get (iobuf_arena);
                if (!iobuf)
                        goto unlock;

                __iobuf_ref (iobuf);
        }
unlock:
        pthread_mutex_unlock (&iobuf_pool->mutex);

        return iobuf;
}


void
__iobuf_put (struct iobuf *iobuf, struct iobuf_arena *iobuf_arena)
{
        struct iobuf_pool *iobuf_pool = NULL;

        iobuf_pool = iobuf_arena->iobuf_pool;

        if (iobuf_arena->passive_cnt == 0) {
                list_del (&iobuf_arena->list);
                list_add_tail (&iobuf_arena->list, &iobuf_pool->arenas.list);
        }

        list_del_init (&iobuf->list);
        iobuf_arena->active_cnt--;

        list_add (&iobuf->list, &iobuf_arena->passive.list);
        iobuf_arena->passive_cnt++;

        if (iobuf_arena->active_cnt == 0) {
                list_del (&iobuf_arena->list);
                list_add_tail (&iobuf_arena->list, &iobuf_pool->purge.list);
        }
}


void
iobuf_put (struct iobuf *iobuf)
{
        struct iobuf_arena *iobuf_arena = NULL;
        struct iobuf_pool  *iobuf_pool = NULL;

        if (!iobuf)
                return;

        iobuf_arena = iobuf->iobuf_arena;
        if (!iobuf_arena)
                return;

        iobuf_pool = iobuf_arena->iobuf_pool;
        if (!iobuf_pool)
                return;

        pthread_mutex_lock (&iobuf_pool->mutex);
        {
                __iobuf_put (iobuf, iobuf_arena);
        }
        pthread_mutex_unlock (&iobuf_pool->mutex);

        iobuf_pool_prune (iobuf_pool);
}


void
iobuf_unref (struct iobuf *iobuf)
{
        int  ref = 0;

        if (!iobuf)
                return;

        LOCK (&iobuf->lock);
        {
                __iobuf_unref (iobuf);
                ref = iobuf->ref;
        }
        UNLOCK (&iobuf->lock);

        if (!ref)
                iobuf_put (iobuf);
}


struct iobuf *
iobuf_ref (struct iobuf *iobuf)
{
        if (!iobuf)
                return NULL;

        LOCK (&iobuf->lock);
        {
                __iobuf_ref (iobuf);
        }
        UNLOCK (&iobuf->lock);

        return iobuf;
}


struct iobref *
iobref_new ()
{
        struct iobref *iobref = NULL;

        iobref = CALLOC (sizeof (*iobref), 1);
        if (!iobref)
                return NULL;

        LOCK_INIT (&iobref->lock);

        iobref->ref++;

        return iobref;
}


struct iobref *
iobref_ref (struct iobref *iobref)
{
        if (!iobref)
                return NULL;

        LOCK (&iobref->lock);
        {
                iobref->ref++;
        }
        UNLOCK (&iobref->lock);

        return iobref;
}


void
iobref_destroy (struct iobref *iobref)
{
        int            i = 0;
        struct iobuf  *iobuf = NULL;

        if (!iobref)
                return;

        for (i = 0; i < 8; i++) {
                iobuf = iobref->iobrefs[i];

                iobref->iobrefs[i] = NULL;
                if (iobuf)
                        iobuf_unref (iobuf);
        }

        FREE (iobref);
}


void
iobref_unref (struct iobref *iobref)
{
        int ref = 0;

        if (!iobref)
                return;

        LOCK (&iobref->lock);
        {
                ref = (--iobref->ref);
        }
        UNLOCK (&iobref->lock);

        if (!ref)
                iobref_destroy (iobref);
}


int
__iobref_add (struct iobref *iobref, struct iobuf *iobuf)
{
        int  i = 0;
        int  ret = -ENOMEM;

        for (i = 0; i < 8; i++) {
                if (iobref->iobrefs[i] == NULL) {
                        iobref->iobrefs[i] = iobuf_ref (iobuf);
                        ret = 0;
                        break;
                }
        }

        return ret;
}


int
iobref_add (struct iobref *iobref, struct iobuf *iobuf)
{
        int  ret = 0;

        if (!iobref)
                return -EINVAL;

        if (!iobuf)
                return -EINVAL;

        LOCK (&iobref->lock);
        {
                ret = __iobref_add (iobref, iobuf);
        }
        UNLOCK (&iobref->lock);

        return ret;
}


int
iobref_merge (struct iobref *to, struct iobref *from)
{
        int           i = 0;
        int           ret = 0;
        struct iobuf *iobuf = NULL;

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

        return ret;
}
