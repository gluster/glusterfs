/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "glusterfs/common-utils.h"  // for GF_ASSERT, gf_thread_cr...
#include "glusterfs/globals.h"       // for xlator_t, THIS
#include "glusterfs/memory.h"

#include <stdlib.h>
#include <stdarg.h>

#include "unittest/unittest.h"
#include "glusterfs/libglusterfs-messages.h"

#ifndef GF_DISABLE_ALLOCATION_TRACKING

#include "glusterfs/statedump.h"

#define GF_MEM_TRAILER_SIZE 8
#define GF_MEM_HEADER_MAGIC 0xCAFEBABE
#define GF_MEM_TRAILER_MAGIC 0xBAADF00D
#define GF_MEM_INVALID_MAGIC 0xDEADC0DE

struct mem_acct_rec {
    const char *typestr;
    uint64_t size;
    uint64_t max_size;
    uint64_t total_allocs;
    uint32_t num_allocs;
    uint32_t max_num_allocs;
    gf_lock_t lock;
#ifdef DEBUG
    struct list_head obj_list;
#endif
};

struct mem_acct {
    uint32_t num_types;
    gf_atomic_t refcnt;
    struct mem_acct_rec rec[0];
};

struct mem_header {
    uint32_t type;
    size_t size;
    struct mem_acct *mem_acct;
    uint32_t magic;
#ifdef DEBUG
    struct list_head acct_list;
#endif
    int padding[8];
};

#define GF_MEM_HEADER_SIZE (sizeof(struct mem_header))

#ifdef DEBUG
struct mem_invalid {
    uint32_t magic;
    void *mem_acct;
    uint32_t type;
    size_t size;
    void *baseaddr;
};
#endif

struct mem_acct *
gf_mem_acct_init(size_t ntypes)
{
    int i;
    struct mem_acct *acct = MALLOC(sizeof(struct mem_acct) +
                                   sizeof(struct mem_acct_rec) * ntypes);
    if (!acct)
        return NULL;

    acct->num_types = ntypes;
    GF_ATOMIC_INIT(acct->refcnt, 1);

    for (i = 0; i < ntypes; i++) {
        memset(&acct->rec[i], 0, sizeof(struct mem_acct_rec));
        LOCK_INIT(&(acct->rec[i].lock));
#ifdef DEBUG
        INIT_LIST_HEAD(&(acct->rec[i].obj_list));
#endif
    }

    return acct;
}

int
gf_mem_acct_fini(struct mem_acct *acct)
{
    int i;

    if (GF_ATOMIC_DEC(acct->refcnt) == 0) {
        for (i = 0; i < acct->num_types; i++)
            LOCK_DESTROY(&(acct->rec[i].lock));
        FREE(acct);
        return 0;
    }

    return 1;
}

#ifdef DEBUG

void
gf_mem_acct_enable_set(glusterfs_ctx_t *ctx)
{
    GF_ASSERT(ctx != NULL);
    ctx->mem_acct_enable = 1;
}

#endif /* DEBUG */

static void *
gf_mem_header_prepare(struct mem_header *header, size_t size)
{
    void *ptr;

    header->size = size;

    ptr = header + 1;

    /* data follows in this gap of 'size' bytes */
    *(uint32_t *)(ptr + size) = GF_MEM_TRAILER_MAGIC;

    return ptr;
}

static void *
gf_mem_set_acct_info(struct mem_acct *mem_acct, struct mem_header *header,
                     size_t size, uint32_t type, const char *typestr)
{
    struct mem_acct_rec *rec = NULL;
    bool new_ref = false;

    if (mem_acct != NULL) {
        GF_ASSERT(type <= mem_acct->num_types);

        rec = &mem_acct->rec[type];
        LOCK(&rec->lock);
        {
            if (!rec->typestr) {
                rec->typestr = typestr;
            }
            rec->size += size;
            new_ref = (rec->num_allocs == 0);
            rec->num_allocs++;
            rec->total_allocs++;
            rec->max_size = max(rec->max_size, rec->size);
            rec->max_num_allocs = max(rec->max_num_allocs, rec->num_allocs);

#ifdef DEBUG
            list_add(&header->acct_list, &rec->obj_list);
#endif
        }
        UNLOCK(&rec->lock);

        /* We only take a reference for each memory type used, not for each
         * allocation. This minimizes the use of atomic operations. */
        if (new_ref) {
            GF_ATOMIC_INC(mem_acct->refcnt);
        }
    }

    header->type = type;
    header->mem_acct = mem_acct;
    header->magic = GF_MEM_HEADER_MAGIC;

    return gf_mem_header_prepare(header, size);
}

static void *
gf_mem_update_acct_info(struct mem_acct *mem_acct, struct mem_header *header,
                        size_t size)
{
    struct mem_acct_rec *rec = NULL;

    if (mem_acct != NULL) {
        rec = &mem_acct->rec[header->type];
        LOCK(&rec->lock);
        {
            rec->size += size - header->size;
            rec->total_allocs++;
            rec->max_size = max(rec->max_size, rec->size);

#ifdef DEBUG
            /* The old 'header' already was present in 'obj_list', but
             * realloc() could have changed its address. We need to remove
             * the old item from the list and add the new one. This can be
             * done this way because list_move() doesn't use the pointers
             * to the old location (which are not valid anymore) already
             * present in the list, it simply overwrites them. */
            list_move(&header->acct_list, &rec->obj_list);
#endif
        }
        UNLOCK(&rec->lock);
    }

    return gf_mem_header_prepare(header, size);
}

static inline bool
gf_mem_acct_enabled(void)
{
    xlator_t *x = THIS;
    /* Low-level __gf_xxx() may be called
       before ctx is initialized. */
    return x->ctx && x->ctx->mem_acct_enable;
}

void *
__gf_calloc(size_t nmemb, size_t size, uint32_t type, const char *typestr)
{
    size_t tot_size = 0;
    size_t req_size = 0;
    void *ptr = NULL;
    xlator_t *xl = NULL;

    if (!gf_mem_acct_enabled())
        return CALLOC(nmemb, size);

    xl = THIS;

    req_size = nmemb * size;
    tot_size = req_size + GF_MEM_HEADER_SIZE + GF_MEM_TRAILER_SIZE;

    ptr = calloc(1, tot_size);

    if (!ptr) {
        gf_msg_nomem("", GF_LOG_ALERT, tot_size);
        return NULL;
    }

    return gf_mem_set_acct_info(xl->mem_acct, ptr, req_size, type, typestr);
}

void *
__gf_malloc(size_t size, uint32_t type, const char *typestr)
{
    size_t tot_size = 0;
    void *ptr = NULL;
    xlator_t *xl = NULL;

    if (!gf_mem_acct_enabled())
        return MALLOC(size);

    xl = THIS;

    tot_size = size + GF_MEM_HEADER_SIZE + GF_MEM_TRAILER_SIZE;

    ptr = malloc(tot_size);
    if (!ptr) {
        gf_msg_nomem("", GF_LOG_ALERT, tot_size);
        return NULL;
    }

    return gf_mem_set_acct_info(xl->mem_acct, ptr, size, type, typestr);
}

void *
__gf_realloc(void *ptr, size_t size)
{
    size_t tot_size = 0;
    struct mem_header *header = NULL;

    if (!gf_mem_acct_enabled())
        return REALLOC(ptr, size);

    REQUIRE(NULL != ptr);

    header = (struct mem_header *)(ptr - GF_MEM_HEADER_SIZE);
    GF_ASSERT(header->magic == GF_MEM_HEADER_MAGIC);

    tot_size = size + GF_MEM_HEADER_SIZE + GF_MEM_TRAILER_SIZE;
    header = realloc(header, tot_size);
    if (!header) {
        gf_msg_nomem("", GF_LOG_ALERT, tot_size);
        return NULL;
    }

    return gf_mem_update_acct_info(header->mem_acct, header, size);
}

#ifdef DEBUG
void
__gf_mem_invalidate(void *ptr)
{
    struct mem_header *header = ptr;
    void *end = NULL;

    struct mem_invalid inval = {
        .magic = GF_MEM_INVALID_MAGIC,
        .mem_acct = header->mem_acct,
        .type = header->type,
        .size = header->size,
        .baseaddr = ptr + GF_MEM_HEADER_SIZE,
    };

    /* calculate the last byte of the allocated area */
    end = ptr + GF_MEM_HEADER_SIZE + inval.size + GF_MEM_TRAILER_SIZE;

    /* overwrite the old mem_header */
    memcpy(ptr, &inval, sizeof(inval));
    ptr += sizeof(inval);

    /* zero out remaining (old) mem_header bytes) */
    memset(ptr, 0x00, sizeof(*header) - sizeof(inval));
    ptr += sizeof(*header) - sizeof(inval);

    /* zero out the first byte of data */
    *(uint32_t *)(ptr) = 0x00;
    ptr += 1;

    /* repeated writes of invalid structurein data area */
    while ((ptr + (sizeof(inval))) < (end - 1)) {
        memcpy(ptr, &inval, sizeof(inval));
        ptr += sizeof(inval);
    }

    /* fill out remaining data area with 0xff */
    memset(ptr, 0xff, end - ptr);
}
#endif /* DEBUG */

/* Coverity taint NOTE: pointers passed to free, would operate on
pointer-GF_MEM_HEADER_SIZE content and if the pointer was used for any IO
related purpose, the pointer stands tainted, and hence coverity would consider
access to the said region as tainted. The following directive to coverity hence
sanitizes the pointer, thus removing any taint to the same within this function.
If the pointer is accessed outside the scope of this function without any
checks on content read from an IO operation, taints will still be reported, and
needs appropriate addressing. */

/* coverity[ +tainted_data_sanitize : arg-0 ] */
static void
gf_free_sanitize(void *s)
{
}

void
__gf_free(void *free_ptr)
{
    void *ptr = NULL;
    struct mem_acct *mem_acct;
    struct mem_header *header = NULL;
    bool last_ref = false;

    if (!free_ptr)
        return;

    if (!gf_mem_acct_enabled()) {
        FREE(free_ptr);
        return;
    }

    gf_free_sanitize(free_ptr);
    ptr = free_ptr - GF_MEM_HEADER_SIZE;
    header = (struct mem_header *)ptr;

    // Possible corruption, assert here
    GF_ASSERT(GF_MEM_HEADER_MAGIC == header->magic);

    mem_acct = header->mem_acct;
    if (!mem_acct) {
        goto free;
    }

    // This points to a memory overrun
    GF_ASSERT(GF_MEM_TRAILER_MAGIC ==
              *(uint32_t *)((char *)free_ptr + header->size));

    LOCK(&mem_acct->rec[header->type].lock);
    {
        mem_acct->rec[header->type].size -= header->size;
        mem_acct->rec[header->type].num_allocs--;
        /* If all the instances are freed up then ensure typestr is set
         * to NULL */
        if (!mem_acct->rec[header->type].num_allocs) {
            last_ref = true;
            mem_acct->rec[header->type].typestr = NULL;
        }
#ifdef DEBUG
        list_del(&header->acct_list);
#endif
    }
    UNLOCK(&mem_acct->rec[header->type].lock);

    if (last_ref) {
        xlator_mem_acct_unref(mem_acct);
    }

free:
#ifdef DEBUG
    __gf_mem_invalidate(ptr);
#endif

    FREE(ptr);
}

void
gf_proc_dump_xlator_mem_info(xlator_t *xl)
{
    int i = 0;

    if (!xl)
        return;

    if (!xl->mem_acct)
        return;

    gf_proc_dump_add_section("%s.%s - Memory usage", xl->type, xl->name);
    gf_proc_dump_write("num_types", "%d", xl->mem_acct->num_types);

    for (i = 0; i < xl->mem_acct->num_types; i++) {
        if (xl->mem_acct->rec[i].num_allocs == 0)
            continue;

        gf_proc_dump_add_section("%s.%s - usage-type %s memusage", xl->type,
                                 xl->name, xl->mem_acct->rec[i].typestr);
        gf_proc_dump_write("size", "%" PRIu64, xl->mem_acct->rec[i].size);
        gf_proc_dump_write("num_allocs", "%u", xl->mem_acct->rec[i].num_allocs);
        gf_proc_dump_write("max_size", "%" PRIu64,
                           xl->mem_acct->rec[i].max_size);
        gf_proc_dump_write("max_num_allocs", "%u",
                           xl->mem_acct->rec[i].max_num_allocs);
        gf_proc_dump_write("total_allocs", "%" PRIu64,
                           xl->mem_acct->rec[i].total_allocs);
    }
}

void
gf_proc_dump_xlator_mem_info_only_in_use(xlator_t *xl)
{
    int i = 0;

    if (!xl)
        return;

    if (!xl->mem_acct)
        return;

    gf_proc_dump_add_section("%s.%s - Memory usage", xl->type, xl->name);
    gf_proc_dump_write("num_types", "%d", xl->mem_acct->num_types);

    for (i = 0; i < xl->mem_acct->num_types; i++) {
        if (!xl->mem_acct->rec[i].size)
            continue;

        gf_proc_dump_add_section("%s.%s - usage-type %d", xl->type, xl->name,
                                 i);

        gf_proc_dump_write("size", "%" PRIu64, xl->mem_acct->rec[i].size);
        gf_proc_dump_write("max_size", "%" PRIu64,
                           xl->mem_acct->rec[i].max_size);
        gf_proc_dump_write("num_allocs", "%u", xl->mem_acct->rec[i].num_allocs);
        gf_proc_dump_write("max_num_allocs", "%u",
                           xl->mem_acct->rec[i].max_num_allocs);
        gf_proc_dump_write("total_allocs", "%" PRIu64,
                           xl->mem_acct->rec[i].total_allocs);
    }
}

void
gf_mem_acct_dump_details(char *type, char *name, struct mem_acct *acct, int fd)
{
    struct mem_acct_rec *mem_rec;
    int i;

    dprintf(fd, "# %s.%s.total.num_types %d\n", type, name, acct->num_types);

    dprintf(fd,
            "# type, in-use-size, in-use-units, max-size, "
            "max-units, total-allocs\n");

    for (i = 0; i < acct->num_types; i++) {
        mem_rec = &acct->rec[i];
        if (mem_rec->num_allocs == 0)
            continue;
        dprintf(fd, "# %s, %" PRIu64 ", %u, %" PRIu64 ", %u, %" PRIu64 "\n",
                mem_rec->typestr, mem_rec->size, mem_rec->num_allocs,
                mem_rec->max_size, mem_rec->max_num_allocs,
                mem_rec->total_allocs);
    }
}

#endif /* not GF_DISABLE_ALLOCATION_TRACKING */

int
gf_vasprintf(char **string_ptr, const char *format, va_list arg)
{
    va_list arg_save;
    char *str = NULL;
    int size = 0;
    int rv = 0;

    if (!string_ptr || !format)
        return -1;

    va_copy(arg_save, arg);

    size = vsnprintf(NULL, 0, format, arg);
    size++;
    str = GF_MALLOC(size, gf_common_mt_asprintf);
    if (str == NULL) {
        /* log is done in GF_MALLOC itself */
        va_end(arg_save);
        return -1;
    }
    rv = vsnprintf(str, size, format, arg_save);

    *string_ptr = str;
    va_end(arg_save);
    return (rv);
}

int
gf_asprintf(char **string_ptr, const char *format, ...)
{
    va_list arg;
    int rv = 0;

    va_start(arg, format);
    rv = gf_vasprintf(string_ptr, format, arg);
    va_end(arg);

    return rv;
}
