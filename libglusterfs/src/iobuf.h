/*
   Copyright (c) 2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _IOBUF_H_
#define _IOBUF_H_

#include "list.h"
#include "common-utils.h"
#include <pthread.h>
#include <sys/mman.h>

/* Lets try to define the new anonymous mapping
 * flag, in case the system is still using the
 * now deprecated MAP_ANON flag.
 *
 * Also, this should ideally be in a centralized/common
 * header which can be used by other source files also.
 */
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

/* one allocatable unit for the consumers of the IOBUF API */
/* each unit hosts @page_size bytes of memory */
struct iobuf;

/* one region of memory MMAPed from the operating system */
/* each region MMAPs @arena_size bytes of memory */
/* each arena hosts @arena_size / @page_size IOBUFs */
struct iobuf_arena;

/* expandable and contractable pool of memory, internally broken into arenas */
struct iobuf_pool;


struct iobuf {
        union {
                struct list_head      list;
                struct {
                        struct iobuf *next;
                        struct iobuf *prev;
                };
        };
        struct iobuf_arena  *iobuf_arena;

        gf_lock_t            lock; /* for ->ptr and ->ref */
        int                  ref;  /* 0 == passive, >0 == active */

        void                *ptr;  /* usable memory region by the consumer */
};


struct iobuf_arena {
        union {
                struct list_head            list;
                struct {
                        struct iobuf_arena *next;
                        struct iobuf_arena *prev;
                };
        };
        struct iobuf_pool  *iobuf_pool;

        void               *mem_base;
        struct iobuf       *iobufs;     /* allocated iobufs list */

        int                 active_cnt;
        struct iobuf        active;     /* head node iobuf
                                           (unused by itself) */
        int                 passive_cnt;
        struct iobuf        passive;    /* head node iobuf
                                           (unused by itself) */
};


struct iobuf_pool {
        pthread_mutex_t     mutex;
        size_t              page_size;  /* size of all iobufs in this pool */
        size_t              arena_size; /* size of memory region in arena */

        int                 arena_cnt;
        struct iobuf_arena  arenas;     /* head node arena
                                           (unused by itself) */
        struct iobuf_arena  filled;     /* arenas without  free iobufs */
        struct iobuf_arena  purge;      /* arenas which can be purged */
};




struct iobuf_pool *iobuf_pool_new (size_t arena_size, size_t page_size);
void iobuf_pool_destroy (struct iobuf_pool *iobuf_pool);
struct iobuf *iobuf_get (struct iobuf_pool *iobuf_pool);
void iobuf_unref (struct iobuf *iobuf);


struct iobref {
        gf_lock_t          lock;
        int                ref;
        struct iobuf      *iobrefs[8];
};

struct iobref *iobref_new ();
struct iobref *iobref_ref (struct iobref *iobref);
void iobref_unref (struct iobref *iobref);
int iobref_add (struct iobref *iobref, struct iobuf *iobuf);
int iobref_merge (struct iobref *to, struct iobref *from);


size_t iobuf_size (struct iobuf *iobuf);
size_t iobref_size (struct iobref *iobref);
void   iobuf_stats_dump (struct iobuf_pool *iobuf_pool);

#endif /* !_IOBUF_H_ */
