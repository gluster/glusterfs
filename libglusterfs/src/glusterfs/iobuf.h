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

#include "glusterfs/list.h"
#include "glusterfs/common-utils.h"
#include <pthread.h>
#include <sys/mman.h>
#include <sys/uio.h>

#define GF_ALIGN_BUF(ptr, bound)                                               \
    ((void *)((unsigned long)(ptr + bound - 1) & (unsigned long)(~(bound - 1))))

#define GF_IOBUF_ALIGN_SIZE 512

#define GF_IOBUF_DEFAULT_PAGE_SIZE (128 * GF_UNIT_KB)

/* one allocatable unit for the consumers of the IOBUF API */
/* each unit hosts @page_size bytes of memory */
struct iobuf;

/* expandable and contractable pool of memory, internally broken into arenas */
struct iobuf_pool;

struct iobuf {
    gf_boolean_t stdalloc; /* indicates whether iobuf is allocated from
                              mem pool or standard alloc*/
    gf_lock_t lock;        /* for ->ptr and ->ref */
    gf_atomic_t ref;       /* 0 == passive, >0 == active */

    void *ptr; /* usable memory region by the consumer */

    void *free_ptr;                /* in case of stdalloc, this is the
                                      one to be freed */
    size_t page_size;              /* iobuf's page size */
    struct iobuf_pool *iobuf_pool; /* iobuf_pool iobuf is associated with */
};

struct iobuf_pool {
    gf_atomic_t mem_pool_hit;
    gf_atomic_t mem_pool_miss;
    gf_atomic_t active_cnt;
};

struct iobuf_pool *
iobuf_pool_new(void);
void
iobuf_pool_destroy(struct iobuf_pool *iobuf_pool);
struct iobuf *
iobuf_get(struct iobuf_pool *iobuf_pool);
void
iobuf_unref(struct iobuf *iobuf);
struct iobuf *
iobuf_ref(struct iobuf *iobuf);
void
iobuf_to_iovec(struct iobuf *iob, struct iovec *iov);

#define iobuf_ptr(iob) ((iob)->ptr)
#define iobuf_pagesize(iob) (iob->page_size)

struct iobref {
    gf_lock_t lock;
    gf_atomic_t ref;
    struct iobuf **iobrefs;
    int allocated;
    int used;
};

struct iobref *
iobref_new(void);
struct iobref *
iobref_ref(struct iobref *iobref);
void
iobref_unref(struct iobref *iobref);
int
iobref_add(struct iobref *iobref, struct iobuf *iobuf);
int
iobref_merge(struct iobref *to, struct iobref *from);
void
iobref_clear(struct iobref *iobref);

size_t
iobuf_size(struct iobuf *iobuf);
size_t
iobref_size(struct iobref *iobref);
void
iobuf_stats_dump(struct iobuf_pool *iobuf_pool);

struct iobuf *
iobuf_get2(struct iobuf_pool *iobuf_pool, size_t page_size);

struct iobuf *
iobuf_get_page_aligned(struct iobuf_pool *iobuf_pool, size_t page_size,
                       size_t align_size);

int
iobuf_copy(struct iobuf_pool *iobuf_pool, const struct iovec *iovec_src,
           int iovcnt, struct iobref **iobref, struct iobuf **iobuf,
           struct iovec *iov_dst);

#endif /* !_IOBUF_H_ */
