/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "mem-pool.h"
#include "logging.h"
#include "xlator.h"
#include <stdlib.h>
#include <stdarg.h>

#include "unittest/unittest.h"
#include "libglusterfs-messages.h"

void
gf_mem_acct_enable_set (void *data)
{
        glusterfs_ctx_t *ctx = NULL;

        REQUIRE(data != NULL);

        ctx = data;

        GF_ASSERT (ctx != NULL);

        ctx->mem_acct_enable = 1;

        ENSURE(1 == ctx->mem_acct_enable);

        return;
}

int
gf_mem_set_acct_info (xlator_t *xl, char **alloc_ptr, size_t size,
		      uint32_t type, const char *typestr)
{

        void              *ptr    = NULL;
        struct mem_header *header = NULL;

        if (!alloc_ptr)
                return -1;

        ptr = *alloc_ptr;

        GF_ASSERT (xl != NULL);

        GF_ASSERT (xl->mem_acct != NULL);

        GF_ASSERT (type <= xl->mem_acct->num_types);

        LOCK(&xl->mem_acct->rec[type].lock);
        {
		if (!xl->mem_acct->rec[type].typestr)
			xl->mem_acct->rec[type].typestr = typestr;
                xl->mem_acct->rec[type].size += size;
                xl->mem_acct->rec[type].num_allocs++;
                xl->mem_acct->rec[type].total_allocs++;
                xl->mem_acct->rec[type].max_size =
                        max (xl->mem_acct->rec[type].max_size,
                             xl->mem_acct->rec[type].size);
                xl->mem_acct->rec[type].max_num_allocs =
                        max (xl->mem_acct->rec[type].max_num_allocs,
                             xl->mem_acct->rec[type].num_allocs);
        }
        UNLOCK(&xl->mem_acct->rec[type].lock);

        GF_ATOMIC_INC (xl->mem_acct->refcnt);

        header = (struct mem_header *) ptr;
        header->type = type;
        header->size = size;
        header->mem_acct = xl->mem_acct;
        header->magic = GF_MEM_HEADER_MAGIC;

#ifdef DEBUG
        INIT_LIST_HEAD(&header->acct_list);
        LOCK(&xl->mem_acct->rec[type].lock);
        {
                list_add (&header->acct_list,
                          &(xl->mem_acct->rec[type].obj_list));
        }
        UNLOCK(&xl->mem_acct->rec[type].lock);
#endif
        ptr += sizeof (struct mem_header);
        /* data follows in this gap of 'size' bytes */
        *(uint32_t *) (ptr + size) = GF_MEM_TRAILER_MAGIC;

        *alloc_ptr = ptr;
        return 0;
}


void *
__gf_calloc (size_t nmemb, size_t size, uint32_t type, const char *typestr)
{
        size_t          tot_size = 0;
        size_t          req_size = 0;
        char            *ptr = NULL;
        xlator_t        *xl = NULL;

        if (!THIS->ctx->mem_acct_enable)
                return CALLOC (nmemb, size);

        xl = THIS;

        req_size = nmemb * size;
        tot_size = req_size + GF_MEM_HEADER_SIZE + GF_MEM_TRAILER_SIZE;

        ptr = calloc (1, tot_size);

        if (!ptr) {
                gf_msg_nomem ("", GF_LOG_ALERT, tot_size);
                return NULL;
        }
        gf_mem_set_acct_info (xl, &ptr, req_size, type, typestr);

        return (void *)ptr;
}

void *
__gf_malloc (size_t size, uint32_t type, const char *typestr)
{
        size_t          tot_size = 0;
        char            *ptr = NULL;
        xlator_t        *xl = NULL;

        if (!THIS->ctx->mem_acct_enable)
                return MALLOC (size);

        xl = THIS;

        tot_size = size + GF_MEM_HEADER_SIZE + GF_MEM_TRAILER_SIZE;

        ptr = malloc (tot_size);
        if (!ptr) {
                gf_msg_nomem ("", GF_LOG_ALERT, tot_size);
                return NULL;
        }
        gf_mem_set_acct_info (xl, &ptr, size, type, typestr);

        return (void *)ptr;
}

void *
__gf_realloc (void *ptr, size_t size)
{
        size_t             tot_size = 0;
        char              *new_ptr;
        struct mem_header *old_header = NULL;
        struct mem_header *new_header = NULL;
        struct mem_header  tmp_header;

        if (!THIS->ctx->mem_acct_enable)
                return REALLOC (ptr, size);

        REQUIRE(NULL != ptr);

        old_header = (struct mem_header *) (ptr - GF_MEM_HEADER_SIZE);
        GF_ASSERT (old_header->magic == GF_MEM_HEADER_MAGIC);
        tmp_header = *old_header;

#ifdef DEBUG
        int type = 0;
        size_t copy_size = 0;

        /* Making these changes for realloc is not straightforward. So
         * I am simulating realloc using calloc and free
         */

        type = tmp_header.type;
        new_ptr = __gf_calloc (1, size, type,
                               tmp_header.mem_acct->rec[type].typestr);
        if (new_ptr) {
                copy_size = (size > tmp_header.size) ? tmp_header.size : size;
                memcpy (new_ptr, ptr, copy_size);
                __gf_free (ptr);
        }

        /* This is not quite what the man page says should happen */
        return new_ptr;
#endif

        tot_size = size + GF_MEM_HEADER_SIZE + GF_MEM_TRAILER_SIZE;
        new_ptr = realloc (old_header, tot_size);
        if (!new_ptr) {
                gf_msg_nomem ("", GF_LOG_ALERT, tot_size);
                return NULL;
        }

        /*
         * We used to pass (char **)&ptr as the second
         * argument after the value of realloc was saved
         * in ptr, but the compiler warnings complained
         * about the casting to and forth from void ** to
         * char **.
         * TBD: it would be nice to adjust the memory accounting info here,
         * but calling gf_mem_set_acct_info here is wrong because it bumps
         * up counts as though this is a new allocation - which it's not.
         * The consequence of doing nothing here is only that the sizes will be
         * wrong, but at least the counts won't be.
        uint32_t           type = 0;
        xlator_t          *xl = NULL;
        type = header->type;
        xl = (xlator_t *) header->xlator;
        gf_mem_set_acct_info (xl, &new_ptr, size, type, NULL);
         */

        new_header = (struct mem_header *) new_ptr;
        *new_header = tmp_header;
        new_header->size = size;

        new_ptr += sizeof (struct mem_header);
        /* data follows in this gap of 'size' bytes */
        *(uint32_t *) (new_ptr + size) = GF_MEM_TRAILER_MAGIC;

        return (void *)new_ptr;
}

int
gf_vasprintf (char **string_ptr, const char *format, va_list arg)
{
        va_list arg_save;
        char    *str = NULL;
        int     size = 0;
        int     rv = 0;

        if (!string_ptr || !format)
                return -1;

        va_copy (arg_save, arg);

        size = vsnprintf (NULL, 0, format, arg);
        size++;
        str = GF_MALLOC (size, gf_common_mt_asprintf);
        if (str == NULL) {
                /* log is done in GF_MALLOC itself */
                va_end (arg_save);
                return -1;
        }
        rv = vsnprintf (str, size, format, arg_save);

        *string_ptr = str;
        va_end (arg_save);
        return (rv);
}

int
gf_asprintf (char **string_ptr, const char *format, ...)
{
        va_list arg;
        int     rv = 0;

        va_start (arg, format);
        rv = gf_vasprintf (string_ptr, format, arg);
        va_end (arg);

        return rv;
}

#ifdef DEBUG
void
__gf_mem_invalidate (void *ptr)
{
        struct mem_header *header = ptr;
        void              *end    = NULL;

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
        memcpy (ptr, &inval, sizeof (inval));
        ptr += sizeof (inval);

        /* zero out remaining (old) mem_header bytes) */
        memset (ptr, 0x00, sizeof (*header) - sizeof (inval));
        ptr += sizeof (*header) - sizeof (inval);

        /* zero out the first byte of data */
        *(uint32_t *)(ptr) = 0x00;
        ptr += 1;

        /* repeated writes of invalid structurein data area */
        while ((ptr + (sizeof (inval))) < (end - 1)) {
                memcpy (ptr, &inval, sizeof (inval));
                ptr += sizeof (inval);
        }

        /* fill out remaining data area with 0xff */
        memset (ptr, 0xff, end - ptr);
}
#endif /* DEBUG */

void
__gf_free (void *free_ptr)
{
        void              *ptr = NULL;
        struct mem_acct   *mem_acct;
        struct mem_header *header = NULL;

        if (!THIS->ctx->mem_acct_enable) {
                FREE (free_ptr);
                return;
        }

        if (!free_ptr)
                return;

        ptr = free_ptr - GF_MEM_HEADER_SIZE;
        header = (struct mem_header *) ptr;

        //Possible corruption, assert here
        GF_ASSERT (GF_MEM_HEADER_MAGIC == header->magic);

        mem_acct = header->mem_acct;
        if (!mem_acct) {
                goto free;
        }

        // This points to a memory overrun
        GF_ASSERT (GF_MEM_TRAILER_MAGIC ==
                *(uint32_t *)((char *)free_ptr + header->size));

        LOCK (&mem_acct->rec[header->type].lock);
        {
                mem_acct->rec[header->type].size -= header->size;
                mem_acct->rec[header->type].num_allocs--;
                /* If all the instances are freed up then ensure typestr is set
                 * to NULL */
                if (!mem_acct->rec[header->type].num_allocs)
                        mem_acct->rec[header->type].typestr = NULL;
#ifdef DEBUG
                list_del (&header->acct_list);
#endif
        }
        UNLOCK (&mem_acct->rec[header->type].lock);

        if (GF_ATOMIC_DEC (mem_acct->refcnt) == 0) {
                FREE (mem_acct);
        }

free:
#ifdef DEBUG
        __gf_mem_invalidate (ptr);
#endif

        FREE (ptr);
}

#define POOL_SMALLEST   7       /* i.e. 128 */
#define POOL_LARGEST    20      /* i.e. 1048576 */
#define NPOOLS          (POOL_LARGEST - POOL_SMALLEST + 1)

static pthread_key_t            pool_key;
static pthread_mutex_t          pool_lock       = PTHREAD_MUTEX_INITIALIZER;
static struct list_head         pool_threads;
static pthread_mutex_t          pool_free_lock  = PTHREAD_MUTEX_INITIALIZER;
static struct list_head         pool_free_threads;
static struct mem_pool_shared   pools[NPOOLS];
static size_t                   pool_list_size;

#if !defined(GF_DISABLE_MEMPOOL)
#define N_COLD_LISTS    1024
#define POOL_SWEEP_SECS 30

static unsigned long            sweep_times;
static unsigned long            sweep_usecs;
static unsigned long            frees_to_system;

typedef struct {
        struct list_head        death_row;
        pooled_obj_hdr_t        *cold_lists[N_COLD_LISTS];
        unsigned int            n_cold_lists;
} sweep_state_t;

enum init_state {
        GF_MEMPOOL_INIT_NONE = 0,
        GF_MEMPOOL_INIT_PREINIT,
        GF_MEMPOOL_INIT_EARLY,
        GF_MEMPOOL_INIT_LATE,
        GF_MEMPOOL_INIT_DESTROY
};

static enum init_state  init_done       = GF_MEMPOOL_INIT_NONE;
static pthread_mutex_t  init_mutex      = PTHREAD_MUTEX_INITIALIZER;
static unsigned int     init_count      = 0;
static pthread_t        sweeper_tid;


void
collect_garbage (sweep_state_t *state, per_thread_pool_list_t *pool_list)
{
        unsigned int            i;
        per_thread_pool_t       *pt_pool;

        if (pool_list->poison) {
                list_del (&pool_list->thr_list);
                list_add (&pool_list->thr_list, &state->death_row);
                return;
        }

        if (state->n_cold_lists >= N_COLD_LISTS) {
                return;
        }

        (void) pthread_spin_lock (&pool_list->lock);
        for (i = 0; i < NPOOLS; ++i) {
                pt_pool = &pool_list->pools[i];
                if (pt_pool->cold_list) {
                        state->cold_lists[state->n_cold_lists++]
                                = pt_pool->cold_list;
                }
                pt_pool->cold_list = pt_pool->hot_list;
                pt_pool->hot_list = NULL;
                if (state->n_cold_lists >= N_COLD_LISTS) {
                        /* We'll just catch up on a future pass. */
                        break;
                }
        }
        (void) pthread_spin_unlock (&pool_list->lock);
}


void
free_obj_list (pooled_obj_hdr_t *victim)
{
        pooled_obj_hdr_t        *next;

        while (victim) {
                next = victim->next;
                free (victim);
                victim = next;
                ++frees_to_system;
        }
}

void *
pool_sweeper (void *arg)
{
        sweep_state_t           state;
        per_thread_pool_list_t  *pool_list;
        per_thread_pool_list_t  *next_pl;
        per_thread_pool_t       *pt_pool;
        unsigned int            i;
        struct timeval          begin_time;
        struct timeval          end_time;
        struct timeval          elapsed;

        /*
         * This is all a bit inelegant, but the point is to avoid doing
         * expensive things (like freeing thousands of objects) while holding a
         * global lock.  Thus, we split each iteration into three passes, with
         * only the first and fastest holding the lock.
         */

        for (;;) {
                sleep (POOL_SWEEP_SECS);
                (void) pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);
                INIT_LIST_HEAD (&state.death_row);
                state.n_cold_lists = 0;

                /* First pass: collect stuff that needs our attention. */
                (void) gettimeofday (&begin_time, NULL);
                (void) pthread_mutex_lock (&pool_lock);
                list_for_each_entry_safe (pool_list, next_pl,
                                          &pool_threads, thr_list) {
                        collect_garbage (&state, pool_list);
                }
                (void) pthread_mutex_unlock (&pool_lock);
                (void) gettimeofday (&end_time, NULL);
                timersub (&end_time, &begin_time, &elapsed);
                sweep_usecs += elapsed.tv_sec * 1000000 + elapsed.tv_usec;
                sweep_times += 1;

                /* Second pass: free dead pools. */
                (void) pthread_mutex_lock (&pool_free_lock);
                list_for_each_entry_safe (pool_list, next_pl,
                                          &state.death_row, thr_list) {
                        for (i = 0; i < NPOOLS; ++i) {
                                pt_pool = &pool_list->pools[i];
                                free_obj_list (pt_pool->cold_list);
                                free_obj_list (pt_pool->hot_list);
                                pt_pool->hot_list = pt_pool->cold_list = NULL;
                        }
                        list_del (&pool_list->thr_list);
                        list_add (&pool_list->thr_list, &pool_free_threads);
                }
                (void) pthread_mutex_unlock (&pool_free_lock);

                /* Third pass: free cold objects from live pools. */
                for (i = 0; i < state.n_cold_lists; ++i) {
                        free_obj_list (state.cold_lists[i]);
                }
                (void) pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
        }
}


void
pool_destructor (void *arg)
{
        per_thread_pool_list_t  *pool_list      = arg;

        /* The pool-sweeper thread will take it from here. */
        pool_list->poison = 1;
}


static __attribute__((constructor)) void
mem_pools_preinit (void)
{
        unsigned int    i;

        INIT_LIST_HEAD (&pool_threads);
        INIT_LIST_HEAD (&pool_free_threads);

        for (i = 0; i < NPOOLS; ++i) {
                pools[i].power_of_two = POOL_SMALLEST + i;

                GF_ATOMIC_INIT (pools[i].allocs_hot, 0);
                GF_ATOMIC_INIT (pools[i].allocs_cold, 0);
                GF_ATOMIC_INIT (pools[i].allocs_stdc, 0);
                GF_ATOMIC_INIT (pools[i].frees_to_list, 0);
        }

        pool_list_size = sizeof (per_thread_pool_list_t)
                       + sizeof (per_thread_pool_t) * (NPOOLS - 1);

        init_done = GF_MEMPOOL_INIT_PREINIT;
}

/* Use mem_pools_init_early() function for basic initialization. There will be
 * no cleanup done by the pool_sweeper thread until mem_pools_init_late() has
 * been called. Calling mem_get() will be possible after this function has
 * setup the basic structures. */
void
mem_pools_init_early (void)
{
        pthread_mutex_lock (&init_mutex);
        /* Use a pthread_key destructor to clean up when a thread exits.
         *
         * We won't increase init_count here, that is only done when the
         * pool_sweeper thread is started too.
         */
        if (init_done == GF_MEMPOOL_INIT_PREINIT ||
            init_done == GF_MEMPOOL_INIT_DESTROY) {
                /* key has not been created yet */
                if (pthread_key_create (&pool_key, pool_destructor) != 0) {
                        gf_log ("mem-pool", GF_LOG_CRITICAL,
                                "failed to initialize mem-pool key");
                }

                init_done = GF_MEMPOOL_INIT_EARLY;
        } else {
                gf_log ("mem-pool", GF_LOG_CRITICAL,
                        "incorrect order of mem-pool initialization "
                        "(init_done=%d)", init_done);
        }

        pthread_mutex_unlock (&init_mutex);
}

/* Call mem_pools_init_late() once threading has been configured completely.
 * This prevent the pool_sweeper thread from getting killed once the main()
 * thread exits during deamonizing. */
void
mem_pools_init_late (void)
{
        pthread_mutex_lock (&init_mutex);
        if ((init_count++) == 0) {
                (void) gf_thread_create (&sweeper_tid, NULL, pool_sweeper,
                                         NULL, "memsweep");

                init_done = GF_MEMPOOL_INIT_LATE;
        }
        pthread_mutex_unlock (&init_mutex);
}

void
mem_pools_fini (void)
{
        pthread_mutex_lock (&init_mutex);
        switch (init_count) {
        case 0:
                /*
                 * If init_count is already zero (as e.g. if somebody called
                 * this before mem_pools_init_late) then the sweeper was
                 * probably never even started so we don't need to stop it.
                 * Even if there's some crazy circumstance where there is a
                 * sweeper but init_count is still zero, that just means we'll
                 * leave it running.  Not perfect, but far better than any
                 * known alternative.
                 */
                break;
        case 1:
        {
                per_thread_pool_list_t *pool_list;
                per_thread_pool_list_t *next_pl;
                unsigned int            i;

                /* if only mem_pools_init_early() was called, sweeper_tid will
                 * be invalid and the functions will error out. That is not
                 * critical. In all other cases, the sweeper_tid will be valid
                 * and the thread gets stopped. */
                (void) pthread_cancel (sweeper_tid);
                (void) pthread_join (sweeper_tid, NULL);

                /* Need to clean the pool_key to prevent further usage of the
                 * per_thread_pool_list_t structure that is stored for each
                 * thread.
                 * This also prevents calling pool_destructor() when a thread
                 * exits, so there is no chance on a use-after-free of the
                 * per_thread_pool_list_t structure. */
                (void) pthread_key_delete (pool_key);

                /* free all objects from all pools */
                list_for_each_entry_safe (pool_list, next_pl,
                                          &pool_threads, thr_list) {
                        for (i = 0; i < NPOOLS; ++i) {
                                free_obj_list (pool_list->pools[i].hot_list);
                                free_obj_list (pool_list->pools[i].cold_list);
                                pool_list->pools[i].hot_list = NULL;
                                pool_list->pools[i].cold_list = NULL;
                        }

                        list_del (&pool_list->thr_list);
                        FREE (pool_list);
                }

                list_for_each_entry_safe (pool_list, next_pl,
                                          &pool_free_threads, thr_list) {
                        list_del (&pool_list->thr_list);
                        FREE (pool_list);
                }

                init_done = GF_MEMPOOL_INIT_DESTROY;
                /* Fall through. */
        }
        default:
                --init_count;
        }
        pthread_mutex_unlock (&init_mutex);
}

#else
void mem_pools_init_early (void) {}
void mem_pools_init_late (void) {}
void mem_pools_fini (void) {}
#endif

struct mem_pool *
mem_pool_new_fn (glusterfs_ctx_t *ctx, unsigned long sizeof_type,
                 unsigned long count, char *name)
{
        unsigned int            i;
        struct mem_pool         *new = NULL;
        struct mem_pool_shared  *pool = NULL;

        if (!sizeof_type) {
                gf_msg_callingfn ("mem-pool", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid argument");
                return NULL;
        }

        for (i = 0; i < NPOOLS; ++i) {
                if (sizeof_type <= AVAILABLE_SIZE(pools[i].power_of_two)) {
                        pool = &pools[i];
                        break;
                }
        }

        if (!pool) {
                gf_msg_callingfn ("mem-pool", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid argument");
                return NULL;
        }

        new = GF_CALLOC (sizeof (struct mem_pool), 1, gf_common_mt_mem_pool);
        if (!new)
                return NULL;

        new->ctx = ctx;
        new->sizeof_type = sizeof_type;
        new->count = count;
        new->name = name;
        new->pool = pool;
        GF_ATOMIC_INIT (new->active, 0);
        INIT_LIST_HEAD (&new->owner);

        LOCK (&ctx->lock);
        {
                list_add (&new->owner, &ctx->mempool_list);
        }
        UNLOCK (&ctx->lock);

        return new;
}

void*
mem_get0 (struct mem_pool *mem_pool)
{
        void             *ptr = NULL;

        if (!mem_pool) {
                gf_msg_callingfn ("mem-pool", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid argument");
                return NULL;
        }

        ptr = mem_get(mem_pool);
        if (ptr) {
#if defined(GF_DISABLE_MEMPOOL)
                memset (ptr, 0, mem_pool->sizeof_type);
#else
                memset (ptr, 0, AVAILABLE_SIZE(mem_pool->pool->power_of_two));
#endif
        }

        return ptr;
}


per_thread_pool_list_t *
mem_get_pool_list (void)
{
        per_thread_pool_list_t  *pool_list;
        unsigned int            i;

        pool_list = pthread_getspecific (pool_key);
        if (pool_list) {
                return pool_list;
        }

        (void) pthread_mutex_lock (&pool_free_lock);
        if (!list_empty (&pool_free_threads)) {
                pool_list = list_entry (pool_free_threads.next,
                                        per_thread_pool_list_t, thr_list);
                list_del (&pool_list->thr_list);
        }
        (void) pthread_mutex_unlock (&pool_free_lock);

        if (!pool_list) {
                pool_list = CALLOC (pool_list_size, 1);
                if (!pool_list) {
                        return NULL;
                }

                INIT_LIST_HEAD (&pool_list->thr_list);
                (void) pthread_spin_init (&pool_list->lock,
                                          PTHREAD_PROCESS_PRIVATE);
                for (i = 0; i < NPOOLS; ++i) {
                        pool_list->pools[i].parent = &pools[i];
                        pool_list->pools[i].hot_list = NULL;
                        pool_list->pools[i].cold_list = NULL;
                }
        }

        (void) pthread_mutex_lock (&pool_lock);
        pool_list->poison = 0;
        list_add (&pool_list->thr_list, &pool_threads);
        (void) pthread_mutex_unlock (&pool_lock);

        (void) pthread_setspecific (pool_key, pool_list);
        return pool_list;
}

pooled_obj_hdr_t *
mem_get_from_pool (per_thread_pool_t *pt_pool)
{
        pooled_obj_hdr_t        *retval;

        retval = pt_pool->hot_list;
        if (retval) {
                GF_ATOMIC_INC (pt_pool->parent->allocs_hot);
                pt_pool->hot_list = retval->next;
                return retval;
        }

        retval = pt_pool->cold_list;
        if (retval) {
                GF_ATOMIC_INC (pt_pool->parent->allocs_cold);
                pt_pool->cold_list = retval->next;
                return retval;
        }

        GF_ATOMIC_INC (pt_pool->parent->allocs_stdc);
        return malloc (1 << pt_pool->parent->power_of_two);
}


void *
mem_get (struct mem_pool *mem_pool)
{
#if defined(GF_DISABLE_MEMPOOL)
        return GF_MALLOC (mem_pool->sizeof_type, gf_common_mt_mem_pool);
#else
        per_thread_pool_list_t  *pool_list;
        per_thread_pool_t       *pt_pool;
        pooled_obj_hdr_t        *retval;

        if (!mem_pool) {
                gf_msg_callingfn ("mem-pool", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid argument");
                return NULL;
        }

        pool_list = mem_get_pool_list ();
        if (!pool_list || pool_list->poison) {
                return NULL;
        }

        (void) pthread_spin_lock (&pool_list->lock);
        pt_pool = &pool_list->pools[mem_pool->pool->power_of_two-POOL_SMALLEST];
        retval = mem_get_from_pool (pt_pool);
        (void) pthread_spin_unlock (&pool_list->lock);

        if (!retval) {
                return NULL;
        }

        retval->magic = GF_MEM_HEADER_MAGIC;
        retval->pool = mem_pool;
        retval->pool_list = pool_list;
        retval->power_of_two = mem_pool->pool->power_of_two;

        GF_ATOMIC_INC (mem_pool->active);

        return retval + 1;
#endif /* GF_DISABLE_MEMPOOL */
}


void
mem_put (void *ptr)
{
#if defined(GF_DISABLE_MEMPOOL)
        GF_FREE (ptr);
#else
        pooled_obj_hdr_t        *hdr;
        per_thread_pool_list_t  *pool_list;
        per_thread_pool_t       *pt_pool;

        if (!ptr) {
                gf_msg_callingfn ("mem-pool", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "invalid argument");
                return;
        }

        hdr = ((pooled_obj_hdr_t *)ptr) - 1;
        if (hdr->magic != GF_MEM_HEADER_MAGIC) {
                /* Not one of ours; don't touch it. */
                return;
        }
        pool_list = hdr->pool_list;
        pt_pool = &pool_list->pools[hdr->power_of_two-POOL_SMALLEST];

        GF_ATOMIC_DEC (hdr->pool->active);

        (void) pthread_spin_lock (&pool_list->lock);
        hdr->magic = GF_MEM_INVALID_MAGIC;
        hdr->next = pt_pool->hot_list;
        pt_pool->hot_list = hdr;
        GF_ATOMIC_INC (pt_pool->parent->frees_to_list);
        (void) pthread_spin_unlock (&pool_list->lock);
#endif /* GF_DISABLE_MEMPOOL */
}

void
mem_pool_destroy (struct mem_pool *pool)
{
        if (!pool)
                return;

        /* remove this pool from the owner (glusterfs_ctx_t) */
        LOCK (&pool->ctx->lock);
        {
                list_del (&pool->owner);
        }
        UNLOCK (&pool->ctx->lock);

        /* free this pool, but keep the mem_pool_shared */
        GF_FREE (pool);

        /*
         * Pools are now permanent, so the mem_pool->pool is kept around. All
         * of the objects *in* the pool will eventually be freed via the
         * pool-sweeper thread, and this way we don't have to add a lot of
         * reference-counting complexity.
         */
}
