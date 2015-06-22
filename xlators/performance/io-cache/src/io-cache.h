/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __IO_CACHE_H
#define __IO_CACHE_H

#include <sys/types.h>
#include "compat-errno.h"

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "common-utils.h"
#include "call-stub.h"
#include "rbthash.h"
#include "hashfn.h"
#include <sys/time.h>
#include <fnmatch.h>
#include "io-cache-messages.h"

#define IOC_PAGE_SIZE    (1024 * 128)   /* 128KB */
#define IOC_CACHE_SIZE   (32 * 1024 * 1024)
#define IOC_PAGE_TABLE_BUCKET_COUNT 1

struct ioc_table;
struct ioc_local;
struct ioc_page;
struct ioc_inode;

struct ioc_priority {
        struct list_head list;
        char             *pattern;
        uint32_t         priority;
};

/*
 * ioc_waitq - this structure is used to represents the waiting
 *             frames on a page
 *
 * @next: pointer to next object in waitq
 * @data: pointer to the frame which is waiting
 */
struct ioc_waitq {
        struct ioc_waitq *next;
        void             *data;
        off_t            pending_offset;
        size_t           pending_size;
};

/*
 * ioc_fill -
 *
 */
struct ioc_fill {
        struct list_head list;  /* list of ioc_fill structures of a frame */
        off_t            offset;
        size_t           size;
        struct iovec     *vector;
        int32_t          count;
        struct iobref    *iobref;
};

struct ioc_local {
        mode_t           mode;
        int32_t          flags;
        loc_t            file_loc;
        off_t            offset;
        size_t           size;
        int32_t          op_ret;
        int32_t          op_errno;
        struct list_head fill_list;      /* list of ioc_fill structures */
        off_t            pending_offset; /*
                                          * offset from this frame should
                                          * continue
                                          */
        size_t           pending_size;   /*
                                          * size of data this frame is waiting
                                          * on
                                          */
        struct ioc_inode *inode;
        int32_t          wait_count;
        pthread_mutex_t  local_lock;
        struct ioc_waitq *waitq;
        void             *stub;
        fd_t             *fd;
        int32_t          need_xattr;
        dict_t           *xattr_req;
};

/*
 * ioc_page - structure to store page of data from file
 *
 */
struct ioc_page {
        struct list_head    page_lru;
        struct ioc_inode    *inode;   /* inode this page belongs to */
        struct ioc_priority *priority;
        char                dirty;
        char                ready;
        struct iovec        *vector;
        int32_t             count;
        off_t               offset;
        size_t              size;
        struct ioc_waitq    *waitq;
        struct iobref       *iobref;
        pthread_mutex_t     page_lock;
        int32_t             op_errno;
        char                stale;
};

struct ioc_cache {
        rbthash_table_t  *page_table;
        struct list_head  page_lru;
        time_t            mtime;       /*
                                        * seconds component of file mtime
                                        */
        time_t            mtime_nsec;  /*
                                        * nanosecond component of file mtime
                                        */
        struct timeval    tv;          /*
                                        * time-stamp at last re-validate
                                        */
};

struct ioc_inode {
        struct ioc_table      *table;
        off_t                  ia_size;
        struct ioc_cache       cache;
        struct list_head       inode_list; /*
                                            * list of inodes, maintained by
                                            * io-cache translator
                                            */
        struct list_head       inode_lru;
        struct ioc_waitq      *waitq;
        pthread_mutex_t        inode_lock;
        uint32_t               weight;      /*
                                             * weight of the inode, increases
                                             * on each read
                                             */
        inode_t               *inode;
};

struct ioc_table {
        uint64_t         page_size;
        uint64_t         cache_size;
        uint64_t         cache_used;
        uint64_t         min_file_size;
        uint64_t         max_file_size;
        struct list_head inodes; /* list of inodes cached */
        struct list_head active;
        struct list_head *inode_lru;
        struct list_head priority_list;
        int32_t          readv_count;
        pthread_mutex_t  table_lock;
        xlator_t         *xl;
        uint32_t         inode_count;
        int32_t          cache_timeout;
        int32_t          max_pri;
        struct mem_pool  *mem_pool;
};

typedef struct ioc_table ioc_table_t;
typedef struct ioc_local ioc_local_t;
typedef struct ioc_page ioc_page_t;
typedef struct ioc_inode ioc_inode_t;
typedef struct ioc_waitq ioc_waitq_t;
typedef struct ioc_fill ioc_fill_t;

void *
str_to_ptr (char *string);

char *
ptr_to_str (void *ptr);

int32_t
ioc_readv_disabled_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, struct iovec *vector,
                        int32_t count, struct iatt *stbuf,
                        struct iobref *iobref, dict_t *xdata);

ioc_page_t *
__ioc_page_get (ioc_inode_t *ioc_inode, off_t offset);

ioc_page_t *
__ioc_page_create (ioc_inode_t *ioc_inode, off_t offset);

void
ioc_page_fault (ioc_inode_t *ioc_inode, call_frame_t *frame, fd_t *fd,
                off_t offset);
void
__ioc_wait_on_page (ioc_page_t *page, call_frame_t *frame, off_t offset,
                  size_t size);

ioc_waitq_t *
__ioc_page_wakeup (ioc_page_t *page, int32_t op_errno);

void
ioc_page_flush (ioc_page_t *page);

ioc_waitq_t *
__ioc_page_error (ioc_page_t *page, int32_t op_ret, int32_t op_errno);

void
ioc_frame_return (call_frame_t *frame);

void
ioc_waitq_return (ioc_waitq_t *waitq);

int32_t
ioc_frame_fill (ioc_page_t *page, call_frame_t *frame, off_t offset,
                size_t size, int32_t op_errno);

#define ioc_inode_lock(ioc_inode)                                       \
        do {                                                            \
                gf_msg_trace (ioc_inode->table->xl->name, 0,            \
                              "locked inode(%p)", ioc_inode);           \
                pthread_mutex_lock (&ioc_inode->inode_lock);            \
        } while (0)


#define ioc_inode_unlock(ioc_inode)                                     \
        do {                                                            \
                gf_msg_trace (ioc_inode->table->xl->name, 0,            \
                              "unlocked inode(%p)", ioc_inode);         \
                pthread_mutex_unlock (&ioc_inode->inode_lock);          \
        } while (0)


#define ioc_table_lock(table)                                           \
        do {                                                            \
                gf_msg_trace (table->xl->name, 0,                       \
                              "locked table(%p)", table);               \
                pthread_mutex_lock (&table->table_lock);                \
        } while (0)


#define ioc_table_unlock(table)                                         \
        do {                                                            \
                gf_msg_trace (table->xl->name, 0,                       \
                              "unlocked table(%p)", table);             \
                pthread_mutex_unlock (&table->table_lock);              \
        } while (0)


#define ioc_local_lock(local)                                           \
        do {                                                            \
                gf_msg_trace (local->inode->table->xl->name, 0,         \
                              "locked local(%p)", local);               \
                pthread_mutex_lock (&local->local_lock);                \
        } while (0)


#define ioc_local_unlock(local)                                         \
        do {                                                            \
                gf_msg_trace (local->inode->table->xl->name, 0,         \
                              "unlocked local(%p)", local);             \
                pthread_mutex_unlock (&local->local_lock);              \
        } while (0)


#define ioc_page_lock(page)                                             \
        do {                                                            \
                gf_msg_trace (page->inode->table->xl->name, 0,          \
                              "locked page(%p)", page);                 \
                pthread_mutex_lock (&page->page_lock);                  \
        } while (0)


#define ioc_page_unlock(page)                                           \
        do {                                                            \
                gf_msg_trace (page->inode->table->xl->name, 0,          \
                              "unlocked page(%p)", page);               \
                pthread_mutex_unlock (&page->page_lock);                \
        } while (0)


static inline uint64_t
time_elapsed (struct timeval *now,
              struct timeval *then)
{
        uint64_t sec = now->tv_sec - then->tv_sec;

        if (sec)
                return sec;

        return 0;
}

ioc_inode_t *
ioc_inode_search (ioc_table_t *table, inode_t *inode);

void
ioc_inode_destroy (ioc_inode_t *ioc_inode);

ioc_inode_t *
ioc_inode_update (ioc_table_t *table, inode_t *inode, uint32_t weight);

int64_t
__ioc_page_destroy (ioc_page_t *page);

int64_t
__ioc_inode_flush (ioc_inode_t *ioc_inode);

void
ioc_inode_flush (ioc_inode_t *ioc_inode);

void
ioc_inode_wakeup (call_frame_t *frame, ioc_inode_t *ioc_inode,
                  struct iatt *stbuf);

int8_t
ioc_cache_still_valid (ioc_inode_t *ioc_inode, struct iatt *stbuf);

int32_t
ioc_prune (ioc_table_t *table);

int32_t
ioc_need_prune (ioc_table_t *table);

#endif /* __IO_CACHE_H */
