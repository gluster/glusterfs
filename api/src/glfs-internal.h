/*
  Copyright (c) 2012-2018 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GLFS_INTERNAL_H
#define _GLFS_INTERNAL_H

#include <glusterfs/xlator.h>
#include <glusterfs/glusterfs.h>
#include <glusterfs/upcall-utils.h>
#include "glfs-handles.h"
#include <glusterfs/refcount.h>
#include <glusterfs/syncop.h>

#define GLFS_SYMLINK_MAX_FOLLOW 2048

#define DEFAULT_REVAL_COUNT 1

/*
 * According to  pthread mutex and conditional variable ( cond,
 * child_down_count, upcall mutex and mutex) initialization of struct glfs
 * members, below GLFS_INIT_* flags are set in 'pthread_flags' member of struct
 * glfs. The flags are set from glfs_init() and  glfs_new_from_ctx() functions
 * as part of fs inititialization.
 *
 * These flag bits are validated in glfs_fini() to destroy all or partially
 * initialized mutex and conditional variables of glfs object.
 * If you introduce new pthread mutex or conditional variable in glfs object,
 * please make sure you have a flag bit intorduced here for proper cleanup
 * in glfs_fini().
 *
 */

#define PTHREAD_MUTEX_INIT(mutex, attr, flags, mask, label)                    \
    do {                                                                       \
        int __ret = -1;                                                        \
        __ret = pthread_mutex_init(mutex, attr);                               \
        if (__ret == 0)                                                        \
            flags |= mask;                                                     \
        else                                                                   \
            goto label;                                                        \
    } while (0)

#define PTHREAD_MUTEX_DESTROY(mutex, flags, mask)                              \
    do {                                                                       \
        if (flags & mask)                                                      \
            (void)pthread_mutex_destroy(mutex);                                \
    } while (0)

#define PTHREAD_COND_INIT(cond, attr, flags, mask, label)                      \
    do {                                                                       \
        int __ret = -1;                                                        \
        __ret = pthread_cond_init(cond, attr);                                 \
        if (__ret == 0)                                                        \
            flags |= mask;                                                     \
        else                                                                   \
            goto label;                                                        \
    } while (0)

#define PTHREAD_COND_DESTROY(cond, flags, mask)                                \
    do {                                                                       \
        if (flags & mask)                                                      \
            (void)pthread_cond_destroy(cond);                                  \
    } while (0)

#define GLFS_INIT_MUTEX 0x00000001        /* pthread_mutex_flag */
#define GLFS_INIT_COND 0x00000002         /* pthread_cond_flag */
#define GLFS_INIT_COND_CHILD 0x00000004   /* pthread_cond_child_down_flag */
#define GLFS_INIT_MUTEX_UPCALL 0x00000008 /* pthread_mutex_upcall_flag */

#ifndef GF_DARWIN_HOST_OS
#ifndef GFAPI_PUBLIC
#define GFAPI_PUBLIC(sym, ver) /**/
#endif
#ifndef GFAPI_PRIVATE
#define GFAPI_PRIVATE(sym, ver) /**/
#endif
#if __GNUC__ >= 10
#define GFAPI_SYMVER_PUBLIC_DEFAULT(fn, ver)                                   \
    __attribute__((__symver__(STR(fn) "@@GFAPI_" STR(ver))))

#define GFAPI_SYMVER_PRIVATE_DEFAULT(fn, ver)                                  \
    __attribute__((__symver__(STR(fn) "@@GFAPI_PRIVATE_" STR(ver))))

#define GFAPI_SYMVER_PUBLIC(fn1, fn2, ver)                                     \
    __attribute__((__symver__(STR(fn2) "@GFAPI_" STR(ver))))

#define GFAPI_SYMVER_PRIVATE(fn1, fn2, ver)                                    \
    __attribute__((__symver__(STR(fn2) "@GFAPI_PRIVATE_" STR(ver))))

#else
#define GFAPI_SYMVER_PUBLIC_DEFAULT(fn, ver)                                   \
    asm(".symver pub_" STR(fn) ", " STR(fn) "@@GFAPI_" STR(ver));

#define GFAPI_SYMVER_PRIVATE_DEFAULT(fn, ver)                                  \
    asm(".symver priv_" STR(fn) ", " STR(fn) "@@GFAPI_PRIVATE_" STR(ver));

#define GFAPI_SYMVER_PUBLIC(fn1, fn2, ver)                                     \
    asm(".symver pub_" STR(fn1) ", " STR(fn2) "@GFAPI_" STR(ver));

#define GFAPI_SYMVER_PRIVATE(fn1, fn2, ver)                                    \
    asm(".symver priv_" STR(fn1) ", " STR(fn2) "@GFAPI_PRIVATE_" STR(ver));
#endif
#define STR(str) #str
#else
#ifndef GFAPI_PUBLIC
#define GFAPI_PUBLIC(sym, ver) __asm("_" __STRING(sym) "$GFAPI_" __STRING(ver));
#endif
#ifndef GFAPI_PRIVATE
#define GFAPI_PRIVATE(sym, ver)                                                \
    __asm("_" __STRING(sym) "$GFAPI_PRIVATE_" __STRING(ver));
#endif
#define GFAPI_SYMVER_PUBLIC_DEFAULT(fn, dotver)  /**/
#define GFAPI_SYMVER_PRIVATE_DEFAULT(fn, dotver) /**/
#define GFAPI_SYMVER_PUBLIC(fn1, fn2, dotver)    /**/
#define GFAPI_SYMVER_PRIVATE(fn1, fn2, dotver)   /**/
#endif

#define ESTALE_RETRY(ret, errno, reval, loc, label)                            \
    do {                                                                       \
        if (ret == -1 && errno == ESTALE) {                                    \
            if (reval < DEFAULT_REVAL_COUNT) {                                 \
                reval++;                                                       \
                loc_wipe(loc);                                                 \
                goto label;                                                    \
            }                                                                  \
        }                                                                      \
    } while (0)

#define GLFS_LOC_FILL_INODE(oinode, loc, label)                                \
    do {                                                                       \
        loc.inode = inode_ref(oinode);                                         \
        gf_uuid_copy(loc.gfid, oinode->gfid);                                  \
        ret = glfs_loc_touchup(&loc);                                          \
        if (ret != 0) {                                                        \
            errno = EINVAL;                                                    \
            goto label;                                                        \
        }                                                                      \
    } while (0)

#define GLFS_LOC_FILL_PINODE(pinode, loc, ret, errno, label, path)             \
    do {                                                                       \
        loc.inode = inode_new(pinode->table);                                  \
        if (!loc.inode) {                                                      \
            ret = -1;                                                          \
            errno = ENOMEM;                                                    \
            goto label;                                                        \
        }                                                                      \
        loc.parent = inode_ref(pinode);                                        \
        loc.name = path;                                                       \
        ret = glfs_loc_touchup(&loc);                                          \
        if (ret != 0) {                                                        \
            errno = EINVAL;                                                    \
            goto label;                                                        \
        }                                                                      \
    } while (0)

struct glfs;

struct _upcall_entry {
    struct list_head upcall_list;
    struct gf_upcall upcall_data;
};
typedef struct _upcall_entry upcall_entry;

typedef int (*glfs_init_cbk)(struct glfs *fs, int ret);

struct glfs {
    char *volname;
    uuid_t vol_uuid;

    glusterfs_ctx_t *ctx;

    pthread_t poller;

    glfs_init_cbk init_cbk;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_cond_t child_down_cond; /* for broadcasting CHILD_DOWN */
    int init;
    int ret;
    int err;

    xlator_t *active_subvol; /* active graph */
    xlator_t *mip_subvol;    /* graph for which migration is in
                              * progress */
    xlator_t *next_subvol;   /* Any new graph is put to
                              * next_subvol, the graph in
                              * next_subvol can either be moved
                              * to mip_subvol (if any IO picks it
                              * up for migration), or be
                              * destroyed (if there is a new
                              * graph, and this was never picked
                              * for migration) */
    xlator_t *old_subvol;

    char *oldvolfile;
    ssize_t oldvollen;

    inode_t *cwd;

    uint32_t dev_id; /* Used to fill st_dev in struct stat */

    struct list_head openfds;

    gf_boolean_t migration_in_progress;

    gf_boolean_t cache_upcalls; /* add upcalls to the upcall_list? */
    struct list_head upcall_list;
    pthread_mutex_t upcall_list_mutex; /* mutex for upcall entry list */

    uint32_t pin_refcnt;
    uint32_t pthread_flags; /* GLFS_INIT_* # defines set this flag */

    uint32_t upcall_events; /* Mask of upcall events application
                             * is interested in */
    glfs_upcall_cbk up_cbk; /* upcall cbk function to be registered */
    void *up_data;          /* Opaque data provided by application
                             * during upcall registration */
    struct list_head waitq; /* waiting synctasks */
};

/* This enum is used to maintain the state of glfd. In case of async fops
 * fd might be closed before the actual fop is complete. Therefore we need
 * to track whether the fd is closed or not, instead actually closing it.*/
enum glfs_fd_state { GLFD_INIT, GLFD_OPEN, GLFD_CLOSE };

struct glfs_fd {
    struct list_head openfds;
    struct list_head list;
    GF_REF_DECL;
    struct glfs *fs;
    enum glfs_fd_state state;
    off_t offset;
    fd_t *fd; /* Currently guared by @fs->mutex. TODO: per-glfd lock */
    struct list_head entries;
    gf_dirent_t *next;
    struct dirent *readdirbuf;
    gf_lkowner_t lk_owner;
    glfs_leaseid_t lease_id; /* Stores lease_id of client in glfd */
    gf_lock_t lock;          /* lock taken before updating fd state */
    glfs_recall_cbk cbk;
    void *cookie;
};

/* glfs object handle introduced for the alternate gfapi implementation based
   on glfs handles/gfid/inode
*/
struct glfs_object {
    inode_t *inode;
    uuid_t gfid;
};

struct glfs_upcall {
    struct glfs *fs;                /* glfs object */
    enum glfs_upcall_reason reason; /* Upcall event type */
    void *event;                    /* changes based in the event type */
    void (*free_event)(void *);     /* free event after the usage */
};

struct glfs_upcall_inode {
    struct glfs_object *object;      /* Object which need to be acted upon */
    int flags;                       /* Cache UPDATE/INVALIDATE flags */
    struct stat buf;                 /* Latest stat of this entry */
    unsigned int expire_time_attr;   /* the amount of time for which
                                      * the application need to cache
                                      * this entry */
    struct glfs_object *p_object;    /* parent Object to be updated */
    struct stat p_buf;               /* Latest stat of parent dir handle */
    struct glfs_object *oldp_object; /* Old parent Object to be updated */
    struct stat oldp_buf;            /* Latest stat of old parent dir handle */
};

struct glfs_upcall_lease {
    struct glfs_object *object; /* Object which need to be acted upon */
    uint32_t lease_type;        /* Lease type to which client can downgrade to*/
};

struct glfs_upcall_lease_fd {
    uint32_t lease_type; /* Lease type to which client can downgrade to*/
    void *fd_cookie;     /* Object which need to be acted upon */
};

struct glfs_xreaddirp_stat {
    struct stat
        st; /* Stat for that dirent - corresponds to GFAPI_XREADDIRP_STAT */
    struct glfs_object *object; /* handled for GFAPI_XREADDIRP_HANDLE */
    uint32_t flags_handled;     /* final set of flags successfulyy handled */
};

#define DEFAULT_EVENT_POOL_SIZE 16384
#define GF_MEMPOOL_COUNT_OF_DICT_T 4096
#define GF_MEMPOOL_COUNT_OF_DATA_T (GF_MEMPOOL_COUNT_OF_DICT_T * 4)
#define GF_MEMPOOL_COUNT_OF_DATA_PAIR_T (GF_MEMPOOL_COUNT_OF_DICT_T * 4)

#define GF_MEMPOOL_COUNT_OF_LRU_BUF_T 256

typedef void(glfs_mem_release_t)(void *ptr);

struct glfs_mem_header {
    uint32_t magic;
    size_t nmemb;
    size_t size;
    glfs_mem_release_t *release;
};

#define GLFS_MEM_HEADER_SIZE (sizeof(struct glfs_mem_header))
#define GLFS_MEM_HEADER_MAGIC 0x20170830

static inline void *
__glfs_calloc(size_t nmemb, size_t size, glfs_mem_release_t release,
              uint32_t type, const char *typestr)
{
    struct glfs_mem_header *header = NULL;

    header = __gf_calloc(nmemb, (size + GLFS_MEM_HEADER_SIZE), type, typestr);
    if (!header)
        return NULL;

    header->magic = GLFS_MEM_HEADER_MAGIC;
    header->nmemb = nmemb;
    header->size = size;
    header->release = release;

    return header + 1;
}

static inline void *
__glfs_malloc(size_t size, glfs_mem_release_t release, uint32_t type,
              const char *typestr)
{
    struct glfs_mem_header *header = NULL;

    header = __gf_malloc((size + GLFS_MEM_HEADER_SIZE), type, typestr);
    if (!header)
        return NULL;

    header->magic = GLFS_MEM_HEADER_MAGIC;
    header->nmemb = 1;
    header->size = size;
    header->release = release;

    return header + 1;
}

static inline void *
__glfs_realloc(void *ptr, size_t size)
{
    struct glfs_mem_header *old_header = NULL;
    struct glfs_mem_header *new_header = NULL;
    struct glfs_mem_header tmp_header;
    void *new_ptr = NULL;

    GF_ASSERT(NULL != ptr);

    old_header = (struct glfs_mem_header *)(ptr - GLFS_MEM_HEADER_SIZE);
    GF_ASSERT(old_header->magic == GLFS_MEM_HEADER_MAGIC);
    tmp_header = *old_header;

    new_ptr = __gf_realloc(old_header, (size + GLFS_MEM_HEADER_SIZE));
    if (!new_ptr)
        return NULL;

    new_header = (struct glfs_mem_header *)new_ptr;
    *new_header = tmp_header;
    new_header->size = size;

    return new_header + 1;
}

static inline void
__glfs_free(void *free_ptr)
{
    struct glfs_mem_header *header = NULL;
    void *release_ptr = NULL;
    int i = 0;

    if (!free_ptr)
        return;

    header = (struct glfs_mem_header *)(free_ptr - GLFS_MEM_HEADER_SIZE);
    GF_ASSERT(header->magic == GLFS_MEM_HEADER_MAGIC);

    if (header->release) {
        release_ptr = free_ptr;
        for (i = 0; i < header->nmemb; i++) {
            header->release(release_ptr);
            release_ptr += header->size;
        }
    }

    __gf_free(header);
}

#define GLFS_CALLOC(nmemb, size, release, type)                                \
    __glfs_calloc(nmemb, size, release, type, #type)

#define GLFS_MALLOC(size, release, type)                                       \
    __glfs_malloc(size, release, type, #type)

#define GLFS_REALLOC(ptr, size) __glfs_realloc(ptr, size)

#define GLFS_FREE(free_ptr) __glfs_free(free_ptr)

int
glfs_mgmt_init(struct glfs *fs);
void
glfs_init_done(struct glfs *fs, int ret) GFAPI_PRIVATE(glfs_init_done, 3.4.0);
int
glfs_process_volfp(struct glfs *fs, FILE *fp);
int
glfs_resolve(struct glfs *fs, xlator_t *subvol, const char *path, loc_t *loc,
             struct iatt *iatt, int reval) GFAPI_PRIVATE(glfs_resolve, 3.7.0);
int
glfs_lresolve(struct glfs *fs, xlator_t *subvol, const char *path, loc_t *loc,
              struct iatt *iatt, int reval);
fd_t *
glfs_resolve_fd(struct glfs *fs, xlator_t *subvol, struct glfs_fd *glfd);

fd_t *
__glfs_migrate_fd(struct glfs *fs, xlator_t *subvol, struct glfs_fd *glfd);

int
glfs_first_lookup(xlator_t *subvol);

void
glfs_process_upcall_event(struct glfs *fs, void *data)
    GFAPI_PRIVATE(glfs_process_upcall_event, 3.7.0);

#define __GLFS_ENTRY_VALIDATE_FS(fs, label)                                    \
    do {                                                                       \
        if (!fs) {                                                             \
            errno = EINVAL;                                                    \
            goto label;                                                        \
        }                                                                      \
        old_THIS = THIS;                                                       \
        THIS = fs->ctx->primary;                                               \
    } while (0)

#define __GLFS_EXIT_FS                                                         \
    do {                                                                       \
        THIS = old_THIS;                                                       \
    } while (0)

#define __GLFS_ENTRY_VALIDATE_FD(glfd, label)                                  \
    do {                                                                       \
        if (!glfd || !glfd->fd || !glfd->fd->inode ||                          \
            glfd->state != GLFD_OPEN) {                                        \
            errno = EBADF;                                                     \
            goto label;                                                        \
        }                                                                      \
        old_THIS = THIS;                                                       \
        THIS = glfd->fd->inode->table->xl->ctx->primary;                       \
    } while (0)

#define __GLFS_LOCK_WAIT(fs)                                                   \
    do {                                                                       \
        struct synctask *task = NULL;                                          \
                                                                               \
        task = synctask_get();                                                 \
                                                                               \
        if (task) {                                                            \
            list_add_tail(&task->waitq, &fs->waitq);                           \
            pthread_mutex_unlock(&fs->mutex);                                  \
            synctask_yield(task, NULL);                                        \
            pthread_mutex_lock(&fs->mutex);                                    \
        } else {                                                               \
            /* non-synctask */                                                 \
            pthread_cond_wait(&fs->cond, &fs->mutex);                          \
        }                                                                      \
    } while (0)

#define __GLFS_SYNCTASK_WAKE(fs)                                               \
    do {                                                                       \
        struct synctask *waittask = NULL;                                      \
                                                                               \
        while (!list_empty(&fs->waitq)) {                                      \
            waittask = list_entry(fs->waitq.next, struct synctask, waitq);     \
            list_del_init(&waittask->waitq);                                   \
            synctask_wake(waittask);                                           \
        }                                                                      \
    } while (0)

/*
  By default all lock attempts from user context must
  use glfs_lock() and glfs_unlock(). This allows
  for a safe implementation of graph migration where
  we can give up the mutex during syncop calls so
  that bottom up calls (particularly CHILD_UP notify)
  can do a mutex_lock() on @glfs without deadlocking
  the filesystem.

  All the fops should wait for graph migration to finish
  before starting the fops. Therefore these functions should
  call glfs_lock with wait_for_migration as true. But waiting
  for migration to finish in call-back path can result thread
  dead-locks. The reason for this is we only have finite
  number of epoll threads. so if we wait on epoll threads
  there will not be any thread left to handle outstanding
  rpc replies.
*/
static inline int
glfs_lock(struct glfs *fs, gf_boolean_t wait_for_migration)
{
    pthread_mutex_lock(&fs->mutex);

    while (!fs->init)
        __GLFS_LOCK_WAIT(fs);

    while (wait_for_migration && fs->migration_in_progress)
        __GLFS_LOCK_WAIT(fs);

    return 0;
}

static inline void
glfs_unlock(struct glfs *fs)
{
    pthread_mutex_unlock(&fs->mutex);
}

struct glfs_fd *
glfs_fd_new(struct glfs *fs);
void
glfs_fd_bind(struct glfs_fd *glfd);
void
glfd_set_state_bind(struct glfs_fd *glfd);

xlator_t *
glfs_active_subvol(struct glfs *fs) GFAPI_PRIVATE(glfs_active_subvol, 3.4.0);
xlator_t *
__glfs_active_subvol(struct glfs *fs);
void
glfs_subvol_done(struct glfs *fs, xlator_t *subvol)
    GFAPI_PRIVATE(glfs_subvol_done, 3.4.0);

inode_t *
glfs_refresh_inode(xlator_t *subvol, inode_t *inode);

inode_t *
glfs_cwd_get(struct glfs *fs);
int
glfs_cwd_set(struct glfs *fs, inode_t *inode);
inode_t *
glfs_resolve_inode(struct glfs *fs, xlator_t *subvol,
                   struct glfs_object *object);
int
glfs_create_object(loc_t *loc, struct glfs_object **retobject);
int
__glfs_cwd_set(struct glfs *fs, inode_t *inode);

int
glfs_resolve_base(struct glfs *fs, xlator_t *subvol, inode_t *inode,
                  struct iatt *iatt);

int
glfs_resolve_at(struct glfs *fs, xlator_t *subvol, inode_t *at,
                const char *origpath, loc_t *loc, struct iatt *iatt, int follow,
                int reval) GFAPI_PRIVATE(glfs_resolve_at, 3.4.0);
int
glfs_loc_touchup(loc_t *loc) GFAPI_PRIVATE(glfs_loc_touchup, 3.4.0);
void
glfs_iatt_to_stat(struct glfs *fs, struct iatt *iatt, struct stat *stat);
void
glfs_iatt_from_stat(struct stat *stat, int valid, struct iatt *iatt,
                    int *gvalid);
int
glfs_loc_link(loc_t *loc, struct iatt *iatt);
int
glfs_loc_unlink(loc_t *loc);
int
glfs_getxattr_process(void *value, size_t size, dict_t *xattr,
                      const char *name);

/* Sends RPC call to glusterd to fetch required volume info */
int
glfs_get_volume_info(struct glfs *fs);

/*
  SYNOPSIS

       glfs_new_from_ctx: Creates a virtual mount object by taking a
       glusterfs_ctx_t object.

  DESCRIPTION

       glfs_new_from_ctx() is not same as glfs_new(). It takes the
       glusterfs_ctx_t object instead of creating one by glusterfs_ctx_new().
       Again the usage is restricted to NFS MOUNT over UDP i.e. in
       glfs_resolve_at() which would take fs object as input but never use
       (purpose is not to change the ABI of glfs_resolve_at()).

  PARAMETERS

       @ctx: glusterfs_ctx_t object

  RETURN VALUES

       fs     : Pointer to the newly created glfs_t object.
       NULL   : Otherwise.
*/

struct glfs *
glfs_new_from_ctx(glusterfs_ctx_t *ctx) GFAPI_PRIVATE(glfs_new_from_ctx, 3.7.0);

/*
  SYNOPSIS

       glfs_free_from_ctx: Free up the memory occupied by glfs_t object
       created by glfs_new_from_ctx().

  DESCRIPTION

       The glfs_t object allocated by glfs_new_from_ctx() must be released
       by the caller using this routine. The usage can be found
       at glfs_fini() or NFS, MOUNT over UDP i.e.
                        __mnt3udp_get_export_subdir_inode ()
                                => glfs_resolve_at().

  PARAMETERS

       @fs: The glfs_t object to be deallocated.

  RETURN VALUES

       void
*/

void
glfs_free_from_ctx(struct glfs *fs) GFAPI_PRIVATE(glfs_free_from_ctx, 3.7.0);

int
glfs_recall_lease_fd(struct glfs *fs, struct gf_upcall *up_data);

int
glfs_get_upcall_cache_invalidation(struct gf_upcall *to_up_data,
                                   struct gf_upcall *from_up_data);
int
glfs_h_poll_cache_invalidation(struct glfs *fs, struct glfs_upcall *up_arg,
                               struct gf_upcall *upcall_data);

ssize_t
glfs_anonymous_preadv(struct glfs *fs, struct glfs_object *object,
                      const struct iovec *iovec, int iovcnt, off_t offset,
                      int flags);
ssize_t
glfs_anonymous_pwritev(struct glfs *fs, struct glfs_object *object,
                       const struct iovec *iovec, int iovcnt, off_t offset,
                       int flags);

struct glfs_object *
glfs_h_resolve_symlink(struct glfs *fs, struct glfs_object *object);

/* Deprecated structures that were passed to client applications, replaced by
 * accessor functions. Do not use these in new applications, and update older
 * usage.
 *
 * See http://review.gluster.org/14701 for more details.
 *
 * WARNING: These structures will be removed in the future.
 */
struct glfs_callback_arg {
    struct glfs *fs;
    enum glfs_upcall_reason reason;
    void *event_arg;
};

struct glfs_callback_inode_arg {
    struct glfs_object *object;      /* Object which need to be acted upon */
    int flags;                       /* Cache UPDATE/INVALIDATE flags */
    struct stat buf;                 /* Latest stat of this entry */
    unsigned int expire_time_attr;   /* the amount of time for which
                                      * the application need to cache
                                      * this entry
                                      */
    struct glfs_object *p_object;    /* parent Object to be updated */
    struct stat p_buf;               /* Latest stat of parent dir handle */
    struct glfs_object *oldp_object; /* Old parent Object
                                      * to be updated */
    struct stat oldp_buf;            /* Latest stat of old parent
                                      * dir handle */
};
struct dirent *
glfs_readdirbuf_get(struct glfs_fd *glfd);

gf_dirent_t *
glfd_entry_next(struct glfs_fd *glfd, int plus);

void
gf_dirent_to_dirent(gf_dirent_t *gf_dirent, struct dirent *dirent);

void
gf_lease_to_glfs_lease(struct gf_lease *gf_lease, struct glfs_lease *lease);

void
glfs_lease_to_gf_lease(struct glfs_lease *lease, struct gf_lease *gf_lease);

void
glfs_release_upcall(void *ptr);

int
get_fop_attr_glfd(dict_t **fop_attr, struct glfs_fd *glfd);

int
set_fop_attr_glfd(struct glfs_fd *glfd);

int
get_fop_attr_thrd_key(dict_t **fop_attr);

void
unset_fop_attr(dict_t **fop_attr);

/*
  SYNOPSIS
  glfs_statx: Fetch extended file attributes for the given path.

  DESCRIPTION
  This function fetches extended file attributes for the given path.

  PARAMETERS
  @fs: The 'virtual mount' object referencing a volume, under which file exists.
  @path: Path of the file within the virtual mount.
  @mask: Requested extended file attributes mask, (See mask defines above)

  RETURN VALUES
  -1 : Failure. @errno will be set with the type of failure.
  0  : Filled in statxbuf with appropriate masks for valid items in the
       structure.

  ERRNO VALUES
    EINVAL: fs is invalid
    EINVAL: mask has unsupported bits set
    Other errors as returned by stat(2)
 */

int
glfs_statx(struct glfs *fs, const char *path, unsigned int mask,
           struct glfs_stat *statxbuf) GFAPI_PRIVATE(glfs_statx, 6.0);

void
glfs_iatt_from_statx(struct iatt *, const struct glfs_stat *)
    GFAPI_PRIVATE(glfs_iatt_from_statx, 6.0);

/*
 * This API is a per thread setting, similar to glfs_setfs{u/g}id, because of
 * the call to syncopctx_setfspid.
 */
int
glfs_setfspid(struct glfs *, pid_t) GFAPI_PRIVATE(glfs_setfspid, 6.1);
#endif /* !_GLFS_INTERNAL_H */
