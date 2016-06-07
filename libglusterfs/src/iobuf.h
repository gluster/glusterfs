/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _IOBUF_H_
#define _IOBUF_H_

#include "list.h"
#include "common-utils.h"
#include <pthread.h>
#include <sys/mman.h>
#include <sys/uio.h>

#define GF_VARIABLE_IOBUF_COUNT 32

#define GF_RDMA_DEVICE_COUNT 8

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

#define GF_ALIGN_BUF(ptr,bound) ((void *)((unsigned long)(ptr + bound - 1) & \
                                          (unsigned long)(~(bound - 1))))

#define GF_IOBUF_ALIGN_SIZE 512

/* one allocatable unit for the consumers of the IOBUF API */
/* each unit hosts @page_size bytes of memory */
struct iobuf;

/* one region of memory MMAPed from the operating system */
/* each region MMAPs @arena_size bytes of memory */
/* each arena hosts @arena_size / @page_size IOBUFs */
struct iobuf_arena;

/* expandable and contractable pool of memory, internally broken into arenas */
struct iobuf_pool;

struct iobuf_init_config {
        size_t   pagesize;
        int32_t  num_pages;
};

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

        void                *free_ptr; /* in case of stdalloc, this is the
                                          one to be freed */
};


struct iobuf_arena {
        union {
                struct list_head            list;
                struct {
                        struct iobuf_arena *next;
                        struct iobuf_arena *prev;
                };
        };

        struct list_head    all_list;
        size_t              page_size;  /* size of all iobufs in this arena */
        size_t              arena_size; /* this is equal to
                                           (iobuf_pool->arena_size / page_size)
                                           * page_size */
        size_t              page_count;

        struct iobuf_pool  *iobuf_pool;

        void               *mem_base;
        struct iobuf       *iobufs;     /* allocated iobufs list */

        int                 active_cnt;
        struct iobuf        active;     /* head node iobuf
                                           (unused by itself) */
        int                 passive_cnt;
        struct iobuf        passive;    /* head node iobuf
                                           (unused by itself) */
        uint64_t            alloc_cnt;  /* total allocs in this pool */
        int                 max_active; /* max active buffers at a given time */
};


struct iobuf_pool {
        pthread_mutex_t     mutex;
        size_t              arena_size; /* size of memory region in
                                           arena */
        size_t              default_page_size; /* default size of iobuf */

        int                 arena_cnt;
        struct list_head    all_arenas;
        struct list_head    arenas[GF_VARIABLE_IOBUF_COUNT];
        /* array of arenas. Each element of the array is a list of arenas
           holding iobufs of particular page_size */

        struct list_head    filled[GF_VARIABLE_IOBUF_COUNT];
        /* array of arenas without free iobufs */

        struct list_head    purge[GF_VARIABLE_IOBUF_COUNT];
        /* array of of arenas which can be purged */

        uint64_t            request_misses; /* mostly the requests for higher
                                              value of iobufs */
        int                 rdma_device_count;
        struct list_head    *mr_list[GF_RDMA_DEVICE_COUNT];
        void                *device[GF_RDMA_DEVICE_COUNT];
        int (*rdma_registration)(void **, void*);
        int (*rdma_deregistration)(struct list_head**, struct iobuf_arena *);

};


struct iobuf_pool *iobuf_pool_new (void);
void iobuf_pool_destroy (struct iobuf_pool *iobuf_pool);
struct iobuf *iobuf_get (struct iobuf_pool *iobuf_pool);
void iobuf_unref (struct iobuf *iobuf);
struct iobuf *iobuf_ref (struct iobuf *iobuf);
void iobuf_pool_destroy (struct iobuf_pool *iobuf_pool);
void iobuf_to_iovec(struct iobuf *iob, struct iovec *iov);

#define iobuf_ptr(iob) ((iob)->ptr)
#define iobpool_default_pagesize(iobpool) ((iobpool)->default_page_size)
#define iobuf_pagesize(iob) (iob->iobuf_arena->page_size)


struct iobref {
        gf_lock_t          lock;
        int                ref;
        struct iobuf     **iobrefs;
	int                alloced;
	int                used;
};

struct iobref *iobref_new ();
struct iobref *iobref_ref (struct iobref *iobref);
void iobref_unref (struct iobref *iobref);
int iobref_add (struct iobref *iobref, struct iobuf *iobuf);
int iobref_merge (struct iobref *to, struct iobref *from);
void iobref_clear (struct iobref *iobref);

size_t iobuf_size (struct iobuf *iobuf);
size_t iobref_size (struct iobref *iobref);
void   iobuf_stats_dump (struct iobuf_pool *iobuf_pool);

struct iobuf *
iobuf_get2 (struct iobuf_pool *iobuf_pool, size_t page_size);

struct iobuf *
iobuf_get_page_aligned (struct iobuf_pool *iobuf_pool, size_t page_size,
                        size_t align_size);
#endif /* !_IOBUF_H_ */
