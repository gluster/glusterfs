/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "glusterfs/iobuf.h"
#include "glusterfs/statedump.h"
#include <stdio.h>
#include "glusterfs/libglusterfs-messages.h"
#include "glusterfs/atomic.h"

struct iobuf_pool *
iobuf_pool_new(void)
{
    struct iobuf_pool *iobuf_pool = NULL;

    iobuf_pool = GF_CALLOC(sizeof(*iobuf_pool), 1, gf_common_mt_iobuf_pool);
    if (!iobuf_pool)
        goto out;

    GF_ATOMIC_INIT(iobuf_pool->mem_pool_hit, 0);
    GF_ATOMIC_INIT(iobuf_pool->mem_pool_miss, 0);
    GF_ATOMIC_INIT(iobuf_pool->active_cnt, 0);

out:
    return iobuf_pool;
}

void
iobuf_pool_destroy(struct iobuf_pool *iobuf_pool)
{
    if (!iobuf_pool)
        return;

    if (GF_ATOMIC_GET(iobuf_pool->active_cnt) != 0)
        gf_msg_callingfn(THIS->name, GF_LOG_ERROR, 0, LG_MSG_IOBUFS_NOT_FOUND,
                         "iobuf_pool_destroy called, but there"
                         " are unfreed active iobufs:%" PRId64,
                         GF_ATOMIC_GET(iobuf_pool->active_cnt));

    GF_FREE(iobuf_pool);
    return;
}

struct iobuf *
iobuf_get2(struct iobuf_pool *iobuf_pool, size_t page_size)
{
    struct iobuf *iobuf = NULL;
    gf_boolean_t hit = _gf_false;

    if (page_size == 0) {
        page_size = GF_IOBUF_DEFAULT_PAGE_SIZE;
    }

    iobuf = mem_pool_get0(sizeof(struct iobuf), &hit);
    if (!iobuf)
        goto out;

    iobuf->free_ptr = mem_pool_get(page_size, &hit);
    if (!iobuf->free_ptr) {
        iobuf->free_ptr = GF_MALLOC(page_size, gf_common_mt_char);
        iobuf->stdalloc = _gf_true;
    }
    if (!iobuf->free_ptr) {
        mem_put(iobuf);
        iobuf = NULL;
        goto out;
    }
    if (hit == _gf_true)
        GF_ATOMIC_INC(iobuf_pool->mem_pool_hit);
    else
        GF_ATOMIC_INC(iobuf_pool->mem_pool_miss);

    iobuf->ptr = iobuf->free_ptr;
    LOCK_INIT(&iobuf->lock);

    iobuf->page_size = page_size;
    iobuf->iobuf_pool = iobuf_pool;

    /* Hold a ref because you are allocating and using it */
    iobuf_ref(iobuf);
    GF_ATOMIC_INC(iobuf_pool->active_cnt);
out:
    return iobuf;
}

struct iobuf *
iobuf_get_page_aligned(struct iobuf_pool *iobuf_pool, size_t page_size,
                       size_t align_size)
{
    size_t req_size = 0;
    struct iobuf *iobuf = NULL;

    req_size = page_size;

    if (req_size == 0) {
        req_size = GF_IOBUF_DEFAULT_PAGE_SIZE;
    }

    iobuf = iobuf_get2(iobuf_pool, req_size + align_size);
    if (!iobuf)
        return NULL;

    iobuf->ptr = GF_ALIGN_BUF(iobuf->ptr, align_size);

    return iobuf;
}

struct iobuf *
iobuf_get(struct iobuf_pool *iobuf_pool)
{
    return iobuf_get2(iobuf_pool, GF_IOBUF_DEFAULT_PAGE_SIZE);
}

void
iobuf_put(struct iobuf *iobuf)
{
    LOCK_DESTROY(&iobuf->lock);

    if (iobuf->stdalloc)
        GF_FREE(iobuf->free_ptr);
    else
        mem_put(iobuf->free_ptr);

    GF_ATOMIC_DEC(iobuf->iobuf_pool->active_cnt);
    mem_put(iobuf);

    return;
}

void
iobuf_unref(struct iobuf *iobuf)
{
    int ref = 0;

    GF_VALIDATE_OR_GOTO("iobuf", iobuf, out);

    ref = GF_ATOMIC_DEC(iobuf->ref);

    if (!ref)
        iobuf_put(iobuf);

out:
    return;
}

struct iobuf *
iobuf_ref(struct iobuf *iobuf)
{
    GF_VALIDATE_OR_GOTO("iobuf", iobuf, out);
    GF_ATOMIC_INC(iobuf->ref);

out:
    return iobuf;
}

struct iobref *
iobref_new()
{
    struct iobref *iobref = NULL;

    iobref = GF_MALLOC(sizeof(*iobref), gf_common_mt_iobref);
    if (!iobref)
        return NULL;

    iobref->iobrefs = GF_CALLOC(sizeof(*iobref->iobrefs), 16,
                                gf_common_mt_iobrefs);
    if (!iobref->iobrefs) {
        GF_FREE(iobref);
        return NULL;
    }

    iobref->allocated = 16;
    iobref->used = 0;

    LOCK_INIT(&iobref->lock);

    GF_ATOMIC_INIT(iobref->ref, 1);
    return iobref;
}

struct iobref *
iobref_ref(struct iobref *iobref)
{
    GF_VALIDATE_OR_GOTO("iobuf", iobref, out);
    GF_ATOMIC_INC(iobref->ref);

out:
    return iobref;
}

void
iobref_destroy(struct iobref *iobref)
{
    int i = 0;
    struct iobuf *iobuf = NULL;

    GF_VALIDATE_OR_GOTO("iobuf", iobref, out);

    for (i = 0; i < iobref->allocated; i++) {
        iobuf = iobref->iobrefs[i];

        iobref->iobrefs[i] = NULL;
        if (iobuf)
            iobuf_unref(iobuf);
    }

    GF_FREE(iobref->iobrefs);
    GF_FREE(iobref);

out:
    return;
}

void
iobref_unref(struct iobref *iobref)
{
    int ref = 0;

    GF_VALIDATE_OR_GOTO("iobuf", iobref, out);
    ref = GF_ATOMIC_DEC(iobref->ref);

    if (!ref)
        iobref_destroy(iobref);

out:
    return;
}

void
iobref_clear(struct iobref *iobref)
{
    int i = 0;

    GF_VALIDATE_OR_GOTO("iobuf", iobref, out);

    for (; i < iobref->allocated; i++) {
        if (iobref->iobrefs[i] != NULL) {
            iobuf_unref(iobref->iobrefs[i]);
        } else {
            /** iobuf's are attached serially */
            break;
        }
    }

    iobref_unref(iobref);

out:
    return;
}

static void
__iobref_grow(struct iobref *iobref)
{
    void *newptr = NULL;
    int i = 0;

    newptr = GF_REALLOC(iobref->iobrefs,
                        iobref->allocated * 2 * (sizeof(*iobref->iobrefs)));
    if (newptr) {
        iobref->iobrefs = newptr;
        iobref->allocated *= 2;

        for (i = iobref->used; i < iobref->allocated; i++)
            iobref->iobrefs[i] = NULL;
    }
}

int
__iobref_add(struct iobref *iobref, struct iobuf *iobuf)
{
    int i = 0;
    int ret = -ENOMEM;

    GF_VALIDATE_OR_GOTO("iobuf", iobref, out);
    GF_VALIDATE_OR_GOTO("iobuf", iobuf, out);

    if (iobref->used == iobref->allocated) {
        __iobref_grow(iobref);

        if (iobref->used == iobref->allocated) {
            ret = -ENOMEM;
            goto out;
        }
    }

    for (i = 0; i < iobref->allocated; i++) {
        if (iobref->iobrefs[i] == NULL) {
            iobref->iobrefs[i] = iobuf_ref(iobuf);
            iobref->used++;
            ret = 0;
            break;
        }
    }

out:
    return ret;
}

int
iobref_add(struct iobref *iobref, struct iobuf *iobuf)
{
    int ret = -EINVAL;

    GF_VALIDATE_OR_GOTO("iobuf", iobref, out);
    GF_VALIDATE_OR_GOTO("iobuf", iobuf, out);

    LOCK(&iobref->lock);
    {
        ret = __iobref_add(iobref, iobuf);
    }
    UNLOCK(&iobref->lock);

out:
    return ret;
}

int
iobref_merge(struct iobref *to, struct iobref *from)
{
    int i = 0;
    int ret = 0;
    struct iobuf *iobuf = NULL;

    GF_VALIDATE_OR_GOTO("iobuf", to, out);
    GF_VALIDATE_OR_GOTO("iobuf", from, out);

    LOCK(&from->lock);
    {
        for (i = 0; i < from->allocated; i++) {
            iobuf = from->iobrefs[i];

            if (!iobuf)
                break;

            ret = iobref_add(to, iobuf);

            if (ret < 0)
                break;
        }
    }
    UNLOCK(&from->lock);

out:
    return ret;
}

size_t
iobuf_size(struct iobuf *iobuf)
{
    if (!iobuf)
        return 0;

    return iobuf->page_size;
}

size_t
iobref_size(struct iobref *iobref)
{
    size_t size = 0;
    int i = 0;

    GF_VALIDATE_OR_GOTO("iobuf", iobref, out);

    LOCK(&iobref->lock);
    {
        for (i = 0; i < iobref->allocated; i++) {
            if (iobref->iobrefs[i])
                size += iobuf_size(iobref->iobrefs[i]);
        }
    }
    UNLOCK(&iobref->lock);

out:
    return size;
}

void
iobuf_stats_dump(struct iobuf_pool *iobuf_pool)
{
    GF_VALIDATE_OR_GOTO("iobuf", iobuf_pool, out);

    gf_proc_dump_add_section("iobuf.global");
    gf_proc_dump_write("iobuf_pool", "%p", iobuf_pool);
    gf_proc_dump_write("iobuf_pool.default_page_size", "%llu",
                       GF_IOBUF_DEFAULT_PAGE_SIZE);
    gf_proc_dump_write("iobuf_pool.request_hits", "%" PRId64,
                       GF_ATOMIC_GET(iobuf_pool->mem_pool_hit));
    gf_proc_dump_write("iobuf_pool.request_misses", "%" PRId64,
                       GF_ATOMIC_GET(iobuf_pool->mem_pool_miss));
    gf_proc_dump_write("iobuf_pool.active_cnt", "%" PRId64,
                       GF_ATOMIC_GET(iobuf_pool->active_cnt));

out:
    return;
}

void
iobuf_to_iovec(struct iobuf *iob, struct iovec *iov)
{
    GF_VALIDATE_OR_GOTO("iobuf", iob, out);
    GF_VALIDATE_OR_GOTO("iobuf", iov, out);

    iov->iov_base = iobuf_ptr(iob);
    iov->iov_len = iobuf_pagesize(iob);

out:
    return;
}

int
iobuf_copy(struct iobuf_pool *iobuf_pool, const struct iovec *iovec_src,
           int iovcnt, struct iobref **iobref, struct iobuf **iobuf,
           struct iovec *iov_dst)
{
    size_t size = -1;
    int ret = 0;

    size = iov_length(iovec_src, iovcnt);

    *iobuf = iobuf_get2(iobuf_pool, size);
    if (!(*iobuf)) {
        ret = -1;
        errno = ENOMEM;
        goto out;
    }

    *iobref = iobref_new();
    if (!(*iobref)) {
        iobuf_unref(*iobuf);
        errno = ENOMEM;
        ret = -1;
        goto out;
    }

    ret = iobref_add(*iobref, *iobuf);
    if (ret) {
        iobuf_unref(*iobuf);
        iobref_unref(*iobref);
        errno = ENOMEM;
        ret = -1;
        goto out;
    }

    iov_unload(iobuf_ptr(*iobuf), iovec_src, iovcnt);

    iov_dst->iov_base = iobuf_ptr(*iobuf);
    iov_dst->iov_len = size;

out:
    return ret;
}
