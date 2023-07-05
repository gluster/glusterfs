/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "glusterfs/mem-pool.h"
#include "glusterfs/common-utils.h"  // for GF_ASSERT, gf_thread_cr...
#include "glusterfs/globals.h"       // for xlator_t, THIS
#include <stdlib.h>
#include <stdarg.h>

#include "unittest/unittest.h"
#include "glusterfs/libglusterfs-messages.h"

void
gf_mem_acct_enable_set(void *data)
{
    glusterfs_ctx_t *ctx = NULL;

    REQUIRE(data != NULL);

    ctx = data;

    GF_ASSERT(ctx != NULL);

    ctx->mem_acct_enable = 1;

    ENSURE(1 == ctx->mem_acct_enable);

    return;
}

/* Calculate the total allocation size required, taking alignment
 * requirements into consideration.
 */
static size_t
__gf_total_alloc_size(size_t req_size)
{
    return req_size + GF_MEM_HEADER_SIZE + GF_MEM_TRAILER_SIZE;
}

/* Byte by byte read/write of the trailer, because the trailer may not be
 * placed on a naturally aligned address - some platforms require memory
 * accesses to be aligned with the word size.
 */
static void
__gf_mem_trailer_write(uint8_t *trailer)
{
    int i = 0;
    for (i = GF_MEM_TRAILER_SIZE - 1; i > 0; i--) {
        *trailer++ = (uint8_t)(GF_MEM_TRAILER_MAGIC >> (i * 8));
    }
    *trailer = (uint8_t)GF_MEM_TRAILER_MAGIC;
}

static gf_mem_magic_t
__gf_mem_trailer_read(uint8_t *trailer)
{
    gf_mem_magic_t magic = 0;

    int i;
    for (i = GF_MEM_TRAILER_SIZE - 1; i > 0; i--) {
        magic |= (gf_mem_magic_t)(*trailer++ << (i * 8));
    }
    magic |= (gf_mem_magic_t)*trailer;

    return magic;
}

static void *
gf_mem_header_prepare(struct mem_header *header, size_t size)
{
    header->size = size;

    /* data follows in this gap of 'size' bytes */
    uint8_t *end = ((uint8_t *)header) + __gf_total_alloc_size(header->size);
    uint8_t *trailer = end - GF_MEM_TRAILER_SIZE;

    __gf_mem_trailer_write(trailer);

    return header->data;
}

static void *
gf_mem_set_acct_info(struct mem_acct *mem_acct, struct mem_header *header,
                     size_t size, uint32_t type, const char *typestr)
{
    struct mem_acct_rec *rec = NULL;
    uint64_t num_allocs;

    if (mem_acct != NULL) {
        GF_ASSERT(type <= mem_acct->num_types);

        rec = &mem_acct->rec[type];
        num_allocs = GF_ATOMIC_INC(rec->num_allocs);
        if (num_allocs == 1) {
            GF_ATOMIC_INC(mem_acct->refcnt);
            rec->typestr = typestr;
        }
#ifdef DEBUG
        LOCK(&rec->lock);
        {
            rec->size += size;
            rec->max_size = max(rec->max_size, (num_allocs * size));
            rec->max_num_allocs = max(rec->max_num_allocs, num_allocs);
            list_add(&header->acct_list, &rec->obj_list);
        }
        UNLOCK(&rec->lock);
#endif
    }

    header->mem_acct = mem_acct;
    header->type = type;
    header->magic = GF_MEM_HEADER_MAGIC;

    return gf_mem_header_prepare(header, size);
}

#ifdef DEBUG
static struct mem_acct_rec *
gf_mem_remove_acct_info(struct mem_acct *mem_acct, struct mem_header *header)
{
    struct mem_acct_rec *rec;

    if (mem_acct == NULL) {
        return NULL;
    }

    GF_ASSERT(header->type <= mem_acct->num_types);

    rec = &mem_acct->rec[header->type];
    LOCK(&rec->lock);
    {
        list_del_init(&header->acct_list);
    }
    UNLOCK(&rec->lock);

    return rec;
}

static void
gf_mem_update_acct_info(struct mem_acct_rec *rec, struct mem_header *header,
                        size_t size)
{
    if (rec != NULL) {
        LOCK(&rec->lock);
        {
            rec->size += size - header->size;
            rec->max_size = max(rec->max_size, rec->size);
            list_add(&header->acct_list, &rec->obj_list);
        }
        UNLOCK(&rec->lock);
    }
}
#endif /* DEBUG */

static bool
gf_mem_acct_enabled(xlator_t *xl)
{
    /* Low-level __gf_xxx() may be called
       before ctx is initialized. */
    return xl->ctx && xl->ctx->mem_acct_enable;
}

void *
__gf_calloc(size_t nmemb, size_t size, uint32_t type, const char *typestr)
{
    size_t tot_size = 0;
    size_t req_size = 0;
    void *ptr = NULL;
    xlator_t *xl = NULL;

    xl = THIS;

    if (!gf_mem_acct_enabled(xl))
        return CALLOC(nmemb, size);

    req_size = nmemb * size;
    tot_size = __gf_total_alloc_size(req_size);

    ptr = calloc(1, tot_size);

    if (caa_unlikely(!ptr)) {
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

    xl = THIS;

    if (!gf_mem_acct_enabled(xl))
        return MALLOC(size);

    tot_size = __gf_total_alloc_size(size);

    ptr = malloc(tot_size);
    if (caa_unlikely(!ptr)) {
        gf_msg_nomem("", GF_LOG_ALERT, tot_size);
        return NULL;
    }

    return gf_mem_set_acct_info(xl->mem_acct, ptr, size, type, typestr);
}

void *
__gf_realloc(void *ptr, size_t size)
{
    size_t tot_size = 0;
    struct mem_header *tmp, *header = NULL;

    if (!gf_mem_acct_enabled(THIS))
        return REALLOC(ptr, size);

    REQUIRE(NULL != ptr);

    header = (struct mem_header *)(ptr - GF_MEM_HEADER_SIZE);
    GF_ASSERT(header->magic == GF_MEM_HEADER_MAGIC);

    tot_size = __gf_total_alloc_size(size);
#ifdef DEBUG
    struct mem_acct_rec *rec;

    rec = gf_mem_remove_acct_info(header->mem_acct, header);
#endif
    tmp = realloc(header, tot_size);
    if (caa_unlikely(!tmp)) {
#ifdef DEBUG
        gf_mem_update_acct_info(rec, header, header->size);
#endif
        gf_msg_nomem("", GF_LOG_ALERT, tot_size);
        return NULL;
    }

#ifdef DEBUG
    gf_mem_update_acct_info(rec, tmp, size);
#endif

    return gf_mem_header_prepare(tmp, size);
}

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

#ifdef DEBUG
static void
__gf_mem_invalidate(void *ptr)
{
    struct mem_header *header = ptr;
    void *end, *old_ptr = NULL;

    struct mem_invalid inval = {
        .magic = GF_MEM_INVALID_MAGIC,
        .type = header->type,
        .mem_acct = header->mem_acct,
        .size = header->size,
        .baseaddr = ptr + GF_MEM_HEADER_SIZE,
    };

    /* calculate the last byte of the allocated area */
    end = ptr + __gf_total_alloc_size(inval.size);

    old_ptr = ptr;

    /* repeated writes of invalid structure in data area */
    while ((ptr + (sizeof(inval))) < (end - 1)) {
        memcpy(ptr, &inval, sizeof(inval));
        ptr += sizeof(inval);
    }

    /* fill out remaining data area with 0xff */
    memset(ptr, 0xff, end - ptr);

    /* zero out remaining (old) mem_header bytes) */
    /* and the first byte of data */
    memset(old_ptr + sizeof(inval), 0x00,
           (sizeof(struct mem_header) - sizeof(inval)) + 1);
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
    uint64_t num_allocs = 0;

    if (caa_unlikely(!free_ptr))
        return;

    if (!gf_mem_acct_enabled(THIS)) {
        FREE(free_ptr);
        return;
    }

    gf_free_sanitize(free_ptr);
    ptr = free_ptr - GF_MEM_HEADER_SIZE;
    header = (struct mem_header *)ptr;

    mem_acct = header->mem_acct;
    // Possible corruption, assert here
    GF_ASSERT(GF_MEM_HEADER_MAGIC == header->magic);

    if (!mem_acct) {
        goto free;
    }

    // This points to a memory overrun
    {
        void *end = ((char *)ptr) + __gf_total_alloc_size(header->size);
        uint8_t *trailer = end - GF_MEM_TRAILER_SIZE;
        GF_ASSERT(GF_MEM_TRAILER_MAGIC == __gf_mem_trailer_read(trailer));
    }

    num_allocs = GF_ATOMIC_DEC(mem_acct->rec[header->type].num_allocs);
#ifdef DEBUG
    LOCK(&mem_acct->rec[header->type].lock);
    {
        list_del(&header->acct_list);
    }
    UNLOCK(&mem_acct->rec[header->type].lock);
#endif
    if (!num_allocs && (GF_ATOMIC_DEC(mem_acct->refcnt) == 0)) {
        xlator_mem_acct_destroy(mem_acct);
    }

free:
#ifdef DEBUG
    __gf_mem_invalidate(ptr);
#endif

    FREE(ptr);
}

#if defined(GF_DISABLE_MEMPOOL)

struct mem_pool *
mem_pool_new_fn(glusterfs_ctx_t *ctx, unsigned long sizeof_type,
                unsigned long count, char *name)
{
    return (struct mem_pool *)(sizeof_type);
}

void
mem_pool_destroy(struct mem_pool *pool)
{
}

#else /* !GF_DISABLE_MEMPOOL */

static pthread_mutex_t pool_lock = PTHREAD_MUTEX_INITIALIZER;
static struct list_head pool_threads;
static pthread_mutex_t pool_free_lock = PTHREAD_MUTEX_INITIALIZER;
static struct list_head pool_free_threads;
static struct mem_pool_shared pools[NPOOLS];
static size_t pool_list_size;

static __thread per_thread_pool_list_t *thread_pool_list = NULL;

#define N_COLD_LISTS 1024
#define POOL_SWEEP_SECS 30

typedef struct {
    pooled_obj_hdr_t *cold_lists[N_COLD_LISTS];
    unsigned int n_cold_lists;
} sweep_state_t;

enum init_state {
    GF_MEMPOOL_INIT_NONE = 0,
    GF_MEMPOOL_INIT_EARLY,
    GF_MEMPOOL_INIT_LATE,
    GF_MEMPOOL_INIT_DESTROY
};

static enum init_state init_done = GF_MEMPOOL_INIT_NONE;
static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned int init_count = 0;
static pthread_t sweeper_tid;

static bool
collect_garbage(sweep_state_t *state, per_thread_pool_list_t *pool_list)
{
    unsigned int i;
    per_thread_pool_t *pt_pool;

    (void)pthread_spin_lock(&pool_list->lock);

    for (i = 0; i < NPOOLS; ++i) {
        pt_pool = &pool_list->pools[i];
        if (pt_pool->cold_list) {
            if (state->n_cold_lists >= N_COLD_LISTS) {
                (void)pthread_spin_unlock(&pool_list->lock);
                return true;
            }
            state->cold_lists[state->n_cold_lists++] = pt_pool->cold_list;
        }
        pt_pool->cold_list = pt_pool->hot_list;
        pt_pool->hot_list = NULL;
    }

    (void)pthread_spin_unlock(&pool_list->lock);

    return false;
}

static void
free_obj_list(pooled_obj_hdr_t *victim)
{
    pooled_obj_hdr_t *next;

    while (victim) {
        next = victim->next;
        free(victim);
        victim = next;
    }
}

static void *
pool_sweeper(void *arg)
{
    sweep_state_t state;
    per_thread_pool_list_t *pool_list;
    uint32_t i;
    bool pending;

    /*
     * This is all a bit inelegant, but the point is to avoid doing
     * expensive things (like freeing thousands of objects) while holding a
     * global lock.  Thus, we split each iteration into two passes, with
     * only the first and fastest holding the lock.
     */

    pending = true;

    for (;;) {
        /* If we know there's pending work to do (or it's the first run), we
         * do collect garbage more often. */
        sleep(pending ? POOL_SWEEP_SECS / 5 : POOL_SWEEP_SECS);

        (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        state.n_cold_lists = 0;
        pending = false;

        /* First pass: collect stuff that needs our attention. */
        (void)pthread_mutex_lock(&pool_lock);
        list_for_each_entry(pool_list, &pool_threads, thr_list)
        {
            if (collect_garbage(&state, pool_list)) {
                pending = true;
            }
        }
        (void)pthread_mutex_unlock(&pool_lock);

        /* Second pass: free cold objects from live pools. */
        for (i = 0; i < state.n_cold_lists; ++i) {
            free_obj_list(state.cold_lists[i]);
        }
        (void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    }

    return NULL;
}

void
mem_pool_thread_destructor(per_thread_pool_list_t *pool_list)
{
    per_thread_pool_t *pt_pool;
    uint32_t i;

    if (pool_list == NULL) {
        pool_list = thread_pool_list;
    }

    /* The current thread is terminating. None of the allocated objects will
     * be used again. We can directly destroy them here instead of delaying
     * it until the next sweeper loop. */
    if (pool_list != NULL) {
        /* Remove pool_list from the global list to avoid that sweeper
         * could touch it. */
        pthread_mutex_lock(&pool_lock);
        list_del(&pool_list->thr_list);
        pthread_mutex_unlock(&pool_lock);

        /* We need to protect hot/cold changes from potential mem_put() calls
         * that reference this pool_list. Once poison is set to true, we are
         * sure that no one else will touch hot/cold lists. The only possible
         * race is when at the same moment a mem_put() is adding a new item
         * to the hot list. We protect from that by taking pool_list->lock.
         * After that we don't need the lock to destroy the hot/cold lists. */
        pthread_spin_lock(&pool_list->lock);
        pool_list->poison = true;
        pthread_spin_unlock(&pool_list->lock);

        for (i = 0; i < NPOOLS; i++) {
            pt_pool = &pool_list->pools[i];

            free_obj_list(pt_pool->hot_list);
            pt_pool->hot_list = NULL;

            free_obj_list(pt_pool->cold_list);
            pt_pool->cold_list = NULL;
        }

        pthread_mutex_lock(&pool_free_lock);
        list_add(&pool_list->thr_list, &pool_free_threads);
        pthread_mutex_unlock(&pool_free_lock);

        thread_pool_list = NULL;
    }
}

static __attribute__((constructor)) void
mem_pools_preinit(void)
{
    unsigned int i;

    INIT_LIST_HEAD(&pool_threads);
    INIT_LIST_HEAD(&pool_free_threads);

    for (i = 0; i < NPOOLS; ++i) {
        pools[i].power_of_two = POOL_SMALLEST + i;
    }

    pool_list_size = sizeof(per_thread_pool_list_t) +
                     sizeof(per_thread_pool_t) * NPOOLS;
    init_done = GF_MEMPOOL_INIT_EARLY;
}

static __attribute__((destructor)) void
mem_pools_postfini(void)
{
    /* TODO: This function should destroy all per thread memory pools that
     *       are still alive, but this is not possible right now because glibc
     *       starts calling destructors as soon as exit() is called, and
     *       gluster doesn't ensure that all threads have been stopped before
     *       calling exit(). Existing threads would crash when they try to use
     *       memory or they terminate if we destroy things here.
     *
     *       When we propertly terminate all threads, we can add the needed
     *       code here. Till then we need to leave the memory allocated. Most
     *       probably this function will be executed on process termination,
     *       so the memory will be released anyway by the system. */
}

/* Call mem_pools_init() once threading has been configured completely. This
 * prevent the pool_sweeper thread from getting killed once the main() thread
 * exits during deamonizing. */
void
mem_pools_init(void)
{
    pthread_mutex_lock(&init_mutex);
    if ((init_count++) == 0) {
        (void)gf_thread_create(&sweeper_tid, NULL, pool_sweeper, NULL,
                               "memsweep");

        init_done = GF_MEMPOOL_INIT_LATE;
    }
    pthread_mutex_unlock(&init_mutex);
}

void
mem_pools_fini(void)
{
    pthread_mutex_lock(&init_mutex);
    switch (init_count) {
        case 0:
            /*
             * If init_count is already zero (as e.g. if somebody called this
             * before mem_pools_init) then the sweeper was probably never even
             * started so we don't need to stop it. Even if there's some crazy
             * circumstance where there is a sweeper but init_count is still
             * zero, that just means we'll leave it running. Not perfect, but
             * far better than any known alternative.
             */
            break;
        case 1: {
            /* if mem_pools_init() was not called, sweeper_tid will be invalid
             * and the functions will error out. That is not critical. In all
             * other cases, the sweeper_tid will be valid and the thread gets
             * stopped. */
            (void)pthread_cancel(sweeper_tid);
            (void)pthread_join(sweeper_tid, NULL);

            /* There could be threads still running in some cases, so we can't
             * destroy pool_lists in use. We can also not destroy unused
             * pool_lists because some allocated objects may still be pointing
             * to them. */
            mem_pool_thread_destructor(NULL);

            init_done = GF_MEMPOOL_INIT_DESTROY;
            /* Fall through. */
        }
        default:
            --init_count;
    }
    pthread_mutex_unlock(&init_mutex);
}

void
mem_pool_destroy(struct mem_pool *pool)
{
    if (!pool)
        return;

    /* remove this pool from the owner (glusterfs_ctx_t) */
    LOCK(&pool->ctx->lock);
    {
        list_del(&pool->owner);
    }
    UNLOCK(&pool->ctx->lock);

    /* free this pool, but keep the mem_pool_shared */
    GF_FREE(pool);

    /*
     * Pools are now permanent, so the mem_pool->pool is kept around. All
     * of the objects *in* the pool will eventually be freed via the
     * pool-sweeper thread, and this way we don't have to add a lot of
     * reference-counting complexity.
     */
}

struct mem_pool *
mem_pool_new_fn(glusterfs_ctx_t *ctx, unsigned long sizeof_type,
                unsigned long count, char *name)
{
    unsigned long extra_size, size;
    unsigned int power;
    struct mem_pool *new = NULL;
    struct mem_pool_shared *pool = NULL;

    if (!sizeof_type) {
        gf_msg_callingfn("mem-pool", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "invalid argument");
        return NULL;
    }

    /* This is the overhead we'll have because of memory accounting for each
     * memory block. */
    extra_size = sizeof(pooled_obj_hdr_t);

    /* We need to compute the total space needed to hold the data type and
     * the header. Given that the smallest block size we have in the pools
     * is 2^POOL_SMALLEST, we need to take the MAX(size, 2^POOL_SMALLEST).
     * However, since this value is only needed to compute its rounded
     * logarithm in base 2, and this only depends on the highest bit set,
     * we can simply do a bitwise or with the minimum size. We need to
     * subtract 1 for correct handling of sizes that are exactly a power
     * of 2. */
    size = (sizeof_type + extra_size - 1UL) | ((1UL << POOL_SMALLEST) - 1UL);

    /* We compute the logarithm in base 2 rounded up of the resulting size.
     * This value will identify which pool we need to use from the pools of
     * powers of 2. This is equivalent to finding the position of the highest
     * bit set. */
    power = sizeof(size) * 8 - __builtin_clzl(size);
    if (power > POOL_LARGEST) {
        gf_msg_callingfn("mem-pool", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "invalid argument");
        return NULL;
    }
    pool = &pools[power - POOL_SMALLEST];

    new = GF_MALLOC(sizeof(struct mem_pool), gf_common_mt_mem_pool);
    if (!new)
        return NULL;

    new->ctx = ctx;
    new->sizeof_type = sizeof_type;
    new->count = count;
    new->name = name;
    new->xl_name = THIS->name;
    new->pool = pool;
    GF_ATOMIC_INIT(new->active, 0);
#ifdef DEBUG
    GF_ATOMIC_INIT(new->hit, 0);
    GF_ATOMIC_INIT(new->miss, 0);
#endif
    INIT_LIST_HEAD(&new->owner);

    LOCK(&ctx->lock);
    {
        list_add(&new->owner, &ctx->mempool_list);
    }
    UNLOCK(&ctx->lock);

    return new;
}

per_thread_pool_list_t *
mem_get_pool_list(void)
{
    per_thread_pool_list_t *pool_list;
    unsigned int i;

    pool_list = thread_pool_list;
    if (pool_list) {
        return pool_list;
    }

    (void)pthread_mutex_lock(&pool_free_lock);
    if (!list_empty(&pool_free_threads)) {
        pool_list = list_entry(pool_free_threads.next, per_thread_pool_list_t,
                               thr_list);
        list_del(&pool_list->thr_list);
    }
    (void)pthread_mutex_unlock(&pool_free_lock);

    if (!pool_list) {
        pool_list = MALLOC(pool_list_size);
        if (!pool_list) {
            return NULL;
        }

        INIT_LIST_HEAD(&pool_list->thr_list);
        (void)pthread_spin_init(&pool_list->lock, PTHREAD_PROCESS_PRIVATE);
        for (i = 0; i < NPOOLS; ++i) {
            pool_list->pools[i].parent = &pools[i];
            pool_list->pools[i].hot_list = NULL;
            pool_list->pools[i].cold_list = NULL;
        }
    }

    /* There's no need to take pool_list->lock, because this is already an
     * atomic operation and we don't need to synchronize it with any change
     * in hot/cold lists. */
    pool_list->poison = false;

    (void)pthread_mutex_lock(&pool_lock);
    list_add(&pool_list->thr_list, &pool_threads);
    (void)pthread_mutex_unlock(&pool_lock);

    thread_pool_list = pool_list;

    /* Ensure that all memory objects associated to the new pool_list are
     * destroyed when the thread terminates. */
    gf_thread_needs_cleanup();

    return pool_list;
}

static pooled_obj_hdr_t *
mem_get_from_pool(struct mem_pool *mem_pool)
{
    per_thread_pool_list_t *pool_list;
    per_thread_pool_t *pt_pool;
    pooled_obj_hdr_t *retval;
#ifdef DEBUG
    gf_boolean_t hit = _gf_true;
#endif

    pool_list = mem_get_pool_list();
    if (!pool_list || pool_list->poison) {
        return NULL;
    }

    pt_pool = &pool_list->pools[mem_pool->pool->power_of_two - POOL_SMALLEST];

    (void)pthread_spin_lock(&pool_list->lock);

    retval = pt_pool->hot_list;
    if (retval) {
        pt_pool->hot_list = retval->next;
        (void)pthread_spin_unlock(&pool_list->lock);
    } else {
        retval = pt_pool->cold_list;
        if (retval) {
            pt_pool->cold_list = retval->next;
            (void)pthread_spin_unlock(&pool_list->lock);
        } else {
            (void)pthread_spin_unlock(&pool_list->lock);
            retval = malloc(1 << pt_pool->parent->power_of_two);
#ifdef DEBUG
            hit = _gf_false;
#endif
        }
    }

    if (retval != NULL) {
        retval->pool = mem_pool;
        retval->power_of_two = mem_pool->pool->power_of_two;
#ifdef DEBUG
        if (hit == _gf_true)
            GF_ATOMIC_INC(mem_pool->hit);
        else
            GF_ATOMIC_INC(mem_pool->miss);
#endif
        retval->magic = GF_MEM_HEADER_MAGIC;
        retval->pool_list = pool_list;
    }

    return retval;
}

void *
mem_get_calloc(struct mem_pool *mem_pool)
{
    void *ptr = mem_get(mem_pool);
    if (ptr) {
        memset(ptr, 0, AVAILABLE_SIZE(mem_pool->pool->power_of_two));
    }

    return ptr;
}

void *
mem_get_malloc(struct mem_pool *mem_pool)
{
    if (!mem_pool) {
        gf_msg_callingfn("mem-pool", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "invalid argument");
        return NULL;
    }

    pooled_obj_hdr_t *retval = mem_get_from_pool(mem_pool);
    if (!retval) {
        return NULL;
    }

    GF_ATOMIC_INC(mem_pool->active);

    return retval + 1;
}

void
mem_put_pool(void *ptr)
{
    pooled_obj_hdr_t *hdr;
    per_thread_pool_list_t *pool_list;
    per_thread_pool_t *pt_pool;

    if (!ptr) {
        gf_msg_callingfn("mem-pool", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "invalid argument");
        return;
    }

    hdr = ((pooled_obj_hdr_t *)ptr) - 1;
    if (hdr->magic != GF_MEM_HEADER_MAGIC) {
        /* Not one of ours; don't touch it. */
        return;
    }

    if (!hdr->pool_list) {
        gf_msg_callingfn("mem-pool", GF_LOG_CRITICAL, EINVAL,
                         LG_MSG_INVALID_ARG,
                         "invalid argument hdr->pool_list NULL");
        return;
    }

    pool_list = hdr->pool_list;
    pt_pool = &pool_list->pools[hdr->power_of_two - POOL_SMALLEST];

    if (hdr->pool)
        GF_ATOMIC_DEC(hdr->pool->active);

    hdr->magic = GF_MEM_INVALID_MAGIC;

    (void)pthread_spin_lock(&pool_list->lock);
    if (!pool_list->poison) {
        hdr->next = pt_pool->hot_list;
        pt_pool->hot_list = hdr;
        (void)pthread_spin_unlock(&pool_list->lock);
    } else {
        /* If the owner thread of this element has terminated, we simply
         * release its memory. */
        (void)pthread_spin_unlock(&pool_list->lock);
        free(hdr);
    }
}

#endif /* GF_DISABLE_MEMPOOL */
