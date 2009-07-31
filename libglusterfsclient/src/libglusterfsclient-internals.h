/*
   Copyright (c) 2008-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef __LIBGLUSTERFSCLIENT_INTERNALS_H
#define __LIBGLUSTERFSCLIENT_INTERNALS_H

#include <glusterfs.h>
#include <logging.h>
#include <inode.h>
#include <pthread.h>
#include <stack.h>
#include <list.h>
#include <signal.h>
#include <call-stub.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fd.h>
#include <dirent.h>

#define LIBGF_IOBUF_SIZE        (128 *GF_UNIT_KB)
typedef void (*sighandler_t) (int);
typedef struct list_head list_head_t;

typedef struct libglusterfs_client_ctx {
        glusterfs_ctx_t gf_ctx;
        inode_table_t *itable;
        pthread_t reply_thread;
        call_pool_t pool;
        uint32_t counter;
        time_t lookup_timeout;
        time_t stat_timeout;
        /* We generate a fake fsid for the subvolume being
         * accessed through this context.
         */
        dev_t fake_fsid;
        pid_t pid;
}libglusterfs_client_ctx_t;

typedef struct signal_handler {
        int signo;
        sighandler_t handler;
        list_head_t next;
}libgf_client_signal_handler_t ;

typedef struct {
        pthread_mutex_t lock;
        pthread_cond_t reply_cond;
        call_stub_t *reply_stub;
        char complete;
        union {
                struct {
                        char is_revalidate;
                        loc_t *loc;
                        int32_t size;
                } lookup;
        }fop;
        fd_t *fd;          /* Needed here because we need a ref to the dir
                              fd in the libgf_client_readdir_cbk in order
                              to process the dirents received, without
                              having them added to the reply stub.
                              Also used in updating iattr cache. See
                              readv_cbk for eg.
                              */
}libgf_client_local_t;

typedef struct {
        pthread_cond_t init_con_established;
        pthread_mutex_t lock;
        char complete;
}libglusterfs_client_private_t;

typedef struct {
        pthread_mutex_t lock;
        uint32_t previous_lookup_time;
        uint32_t previous_stat_time;
        struct stat stbuf;
} libglusterfs_client_inode_ctx_t;

/* Our dirent cache is very simplistic when it comes to directory
 * reading workloads. It assumes that all directory traversal operations happen
 * sequentially and that readdir callers dont go jumping around the directory
 * using seekdir, rewinddir. Thats why you'll notice that seekdir, rewinddir
 * API in libglusterfsclient only set the offset. The consequence is that when
 * libgf_dcache_readdir finds that the offset presented to it, is not
 * the same as the offset of the previous dirent returned by dcache (..stored
 * in struct direntcache->prev_off..), it realises that a non-sequential
 * directory read is in progress and returns 0 to signify that the cache is
 * not valid.
 * This could be made a bit more intelligent by using a data structure like
 * a hash-table or a balanced binary tree that allows us to search for the
 * existence of particular offsets in the cache without performing a list or
 * array traversal.
 * Dont use a simple binary search tree because
 * there is no guarantee that offsets in a sequential reading of the directory
 * will be just random integers. If for some reason they are sequential, a BST
 * will end up becoming a list.
 */
struct direntcache {
        gf_dirent_t entries;            /* Head of list of cached dirents. */
        gf_dirent_t *next;              /* Pointer to the next entry that
                                         * should be sent by readdir */
        uint64_t prev_off;              /* Offset where the next read will
                                         * happen.
                                         */
};

typedef struct {
        pthread_mutex_t lock;
        off_t offset;
        libglusterfs_client_ctx_t *ctx;
        /* `man readdir` says readdir is non-re-entrant
         * only if two readdirs are racing on the same
         * handle.
         */
	struct dirent dirp;
        struct direntcache *dcache;

} libglusterfs_client_fd_ctx_t;

typedef struct libglusterfs_client_async_local {
        void *cbk_data;
        union {
                struct {
                        fd_t *fd;
                        glusterfs_readv_cbk_t cbk;
                        char update_offset;
                }readv_cbk;
    
                struct {
                        fd_t *fd;
                        glusterfs_write_cbk_t cbk;
                }write_cbk;

                struct {
                        fd_t *fd;
                }close_cbk;

                struct {
                        void *buf;
                        size_t size;
                        loc_t *loc;
                        char is_revalidate;
                        glusterfs_get_cbk_t cbk;
                }lookup_cbk;
        }fop;
}libglusterfs_client_async_local_t;

#define LIBGF_STACK_WIND_AND_WAIT(frame, rfn, obj, fn, params ...)      \
        do {                                                            \
                STACK_WIND (frame, rfn, obj, fn, params);               \
                pthread_mutex_lock (&local->lock);                      \
                {                                                       \
                        while (!local->complete) {                      \
                                pthread_cond_wait (&local->reply_cond,  \
                                                   &local->lock);       \
                        }                                               \
                }                                                       \
                pthread_mutex_unlock (&local->lock);                    \
        } while (0)


#define LIBGF_CLIENT_SIGNAL(signal_handler_list, signo, handler)        \
        do {                                                            \
                libgf_client_signal_handler_t *libgf_handler = CALLOC (1, \
                                                    sizeof (*libgf_handler)); \
                ERR_ABORT (libgf_handler);                              \
                libgf_handler->signo = signo;                           \
                libgf_handler->handler = signal (signo, handler);       \
                list_add (&libgf_handler->next, signal_handler_list);   \
        } while (0)                                                           

#define LIBGF_INSTALL_SIGNAL_HANDLERS(signal_handlers)                  \
        do {                                                            \
                INIT_LIST_HEAD (&signal_handlers);                      \
                /* Handle SIGABORT and SIGSEGV */                       \
                LIBGF_CLIENT_SIGNAL (&signal_handlers, SIGSEGV, gf_print_trace); \
                LIBGF_CLIENT_SIGNAL (&signal_handlers, SIGABRT, gf_print_trace); \
                LIBGF_CLIENT_SIGNAL (&signal_handlers, SIGHUP, gf_log_logrotate); \
                /* LIBGF_CLIENT_SIGNAL (SIGTERM, glusterfs_cleanup_and_exit); */ \
        } while (0)

#define LIBGF_RESTORE_SIGNAL_HANDLERS(local)                            \
        do {                                                            \
                libgf_client_signal_handler_t *ptr = NULL, *tmp = NULL; \
                list_for_each_entry_safe (ptr, tmp, &local->signal_handlers,\
                                          next) {                       \
                        signal (ptr->signo, ptr->handler);              \
                        FREE (ptr);                                     \
                }                                                       \
        } while (0)                                       

#define LIBGF_CLIENT_FOP_ASYNC(ctx, local, ret_fn, op, args ...)        \
        do {                                                            \
                call_frame_t *frame = get_call_frame_for_req (ctx, 1);  \
                xlator_t *xl = frame->this->children ?                  \
                        frame->this->children->xlator : NULL;           \
                frame->root->state = ctx;                               \
                frame->local = local;                                   \
                STACK_WIND (frame, ret_fn, xl, xl->fops->op, args);     \
        } while (0)

#define LIBGF_CLIENT_FOP(ctx, stub, op, local, args ...)                \
        do {                                                            \
                call_frame_t *frame = get_call_frame_for_req (ctx, 1);  \
                xlator_t *xl = frame->this->children ?                  \
                        frame->this->children->xlator : NULL;           \
                if (!local) {                                           \
                        local = CALLOC (1, sizeof (*local));            \
                }                                                       \
                ERR_ABORT (local);                                      \
                frame->local = local;                                   \
                frame->root->state = ctx;                               \
                pthread_cond_init (&local->reply_cond, NULL);           \
                pthread_mutex_init (&local->lock, NULL);                \
                LIBGF_STACK_WIND_AND_WAIT (frame, libgf_client_##op##_cbk, xl, \
                                           xl->fops->op, args);         \
                stub = local->reply_stub;                               \
                FREE (frame->local);                                    \
                frame->local = NULL;                                    \
                STACK_DESTROY (frame->root);                            \
        } while (0)

#define LIBGF_REPLY_NOTIFY(local)                                       \
        do {                                                            \
                pthread_mutex_lock (&local->lock);                      \
                {                                                       \
                        local->complete = 1;                            \
                        pthread_cond_broadcast (&local->reply_cond);    \
                }                                                       \
                pthread_mutex_unlock (&local->lock);                    \
        } while (0)


void
libgf_client_loc_wipe (loc_t *loc);

int32_t
libgf_client_loc_fill (loc_t *loc,
                       libglusterfs_client_ctx_t *ctx,
                       ino_t ino,
                       ino_t par,
                       const char *name);

int32_t
libgf_client_path_lookup (loc_t *loc,
                          libglusterfs_client_ctx_t *ctx,
                          char lookup_basename);

int32_t
libgf_client_lookup (libglusterfs_client_ctx_t *ctx,
                     loc_t *loc,
                     struct stat *stbuf,
                     dict_t **dict,
                     dict_t *xattr_req);

/* We're not expecting more than 10-15
 * VMPs per process so a list is acceptable.
 */
struct vmp_entry {
        struct list_head list;
        char * vmp;
        int vmplen;
        glusterfs_handle_t handle;
};

#define LIBGF_UPDATE_LOOKUP     0x1
#define LIBGF_UPDATE_STAT       0x2
#define LIBGF_UPDATE_ALL        (LIBGF_UPDATE_LOOKUP | LIBGF_UPDATE_STAT)

#define LIBGF_VALIDATE_LOOKUP  0x1
#define LIBGF_VALIDATE_STAT     0x2

#define LIBGF_INVALIDATE_LOOKUP  0x1
#define LIBGF_INVALIDATE_STAT     0x2
int
libgf_is_iattr_cache_valid (libglusterfs_client_ctx_t *ctx, inode_t *inode,
                            struct stat *sbuf, int flags);

int
libgf_update_iattr_cache (inode_t *inode, int flags, struct stat *buf);

#endif
