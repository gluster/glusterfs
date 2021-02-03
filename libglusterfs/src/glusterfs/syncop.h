/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _SYNCOP_H
#define _SYNCOP_H

#include <sys/time.h>
#include <pthread.h>
#include <ucontext.h>
#include "glusterfs/dict.h"   // for dict_t
#include "glusterfs/stack.h"  // for call_frame_t, STACK_DESTROY, STACK_...
#include "glusterfs/timer.h"

#define SYNCENV_PROC_MAX 16
#define SYNCENV_PROC_MIN 2
#define SYNCPROC_IDLE_TIME 600

/*
 * Flags for syncopctx valid elements
 */
#define SYNCOPCTX_UID 0x00000001
#define SYNCOPCTX_GID 0x00000002
#define SYNCOPCTX_GROUPS 0x00000004
#define SYNCOPCTX_PID 0x00000008
#define SYNCOPCTX_LKOWNER 0x00000010

#ifdef HAVE_TSAN_API
/* Currently hardcoded within thread context maintained by the sanitizer. */
#define TSAN_THREAD_NAMELEN 64
#endif

struct synctask;
struct syncproc;
struct syncenv;
struct synccond;

typedef int (*synctask_cbk_t)(int ret, call_frame_t *frame, void *opaque);

typedef int (*synctask_fn_t)(void *opaque);

typedef enum {
    SYNCTASK_INIT = 0,
    SYNCTASK_RUN,
    SYNCTASK_SUSPEND,
    SYNCTASK_WAIT,
    SYNCTASK_DONE,
    SYNCTASK_ZOMBIE,
} synctask_state_t;

/* for one sequential execution of @syncfn */
struct synctask {
    struct list_head all_tasks;
    struct syncenv *env;
    xlator_t *xl;
    call_frame_t *frame;
    call_frame_t *opframe;
    synctask_cbk_t synccbk;
    synctask_fn_t syncfn;
    struct timespec *delta;
    gf_timer_t *timer;
    struct synccond *synccond;
    void *opaque;
    void *stack;
    synctask_state_t state;
    int woken;
    int slept;
    int ret;

    uid_t uid;
    gid_t gid;

#ifdef HAVE_TSAN_API
    struct {
        void *fiber;
        char name[TSAN_THREAD_NAMELEN];
    } tsan;
#endif

    ucontext_t ctx;
    struct syncproc *proc;

    pthread_mutex_t mutex; /* for synchronous spawning of synctask */
    pthread_cond_t cond;
    int done;

    struct list_head waitq; /* can wait only "once" at a time */
};

struct syncproc {
    pthread_t processor;

#ifdef HAVE_TSAN_API
    struct {
        void *fiber;
        char name[TSAN_THREAD_NAMELEN];
    } tsan;
#endif

    ucontext_t sched;
    struct syncenv *env;
    struct synctask *current;
};

/* hosts the scheduler thread and framework for executing synctasks */
struct syncenv {
    struct syncproc proc[SYNCENV_PROC_MAX];

    pthread_mutex_t mutex;
    pthread_cond_t cond;

    struct list_head runq;
    struct list_head waitq;

    int procs;
    int procs_idle;

    int runcount;

    int procmin;
    int procmax;

    size_t stacksize;

    int destroy; /* FLAG to mark syncenv is in destroy mode
                    so that no more synctasks are accepted*/
};

typedef enum { LOCK_NULL = 0, LOCK_TASK, LOCK_THREAD } lock_type_t;

typedef enum {
    SYNC_LOCK_DEFAULT = 0,
    SYNC_LOCK_RECURSIVE, /*it allows recursive locking*/
} lock_attr_t;

struct synclock {
    pthread_mutex_t guard;  /* guard the remaining members, pair @cond */
    pthread_cond_t cond;    /* waiting non-synctasks */
    struct list_head waitq; /* waiting synctasks */
    volatile int lock;      /* true(non zero) or false(zero), lock status */
    lock_attr_t attr;
    struct synctask *owner; /* NULL if current owner is not a synctask */
    pthread_t owner_tid;
    lock_type_t type;
};
typedef struct synclock synclock_t;

struct synccond {
    pthread_mutex_t pmutex;
    pthread_cond_t pcond;
    struct list_head waitq;
};
typedef struct synccond synccond_t;

struct syncbarrier {
    gf_boolean_t initialized; /*Set on successful initialization*/
    pthread_mutex_t guard;    /* guard the remaining members, pair @cond */
    pthread_cond_t cond;      /* waiting non-synctasks */
    struct list_head waitq;   /* waiting synctasks */
    int count;                /* count the number of wakes */
    int waitfor;              /* no. of wakes until which task can be in
                                 waitq before being woken up. */
};
typedef struct syncbarrier syncbarrier_t;

struct syncargs {
    int op_ret;
    int op_errno;

    /*
     * The below 3 iatt structures are used in the fops
     * whose callbacks get struct iatt as one of the
     * a return arguments. Currently, the maximum number
     * of iatt structures returned is 3 for some fops
     * such as mknod, copy_file_range, mkdir etc. So
     * all the following 3 iatt structures would be used
     * for those fops.
     */
    struct iatt iatt1;
    struct iatt iatt2;
    struct iatt iatt3;
    dict_t *xattr;
    struct statvfs statvfs_buf;
    struct iovec *vector;
    int count;
    struct iobref *iobref;
    char *buffer;
    dict_t *xdata;
    struct gf_flock flock;
    struct gf_lease lease;
    dict_t *dict_out;

    /* some more _cbk needs */
    uuid_t uuid;
    char *errstr;
    dict_t *dict;
    pthread_mutex_t lock_dict;

    syncbarrier_t barrier;

    /* do not touch */
    struct synctask *task;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int done;

    gf_dirent_t entries;
    off_t offset;

    lock_migration_info_t locklist;
};

struct syncopctx {
    unsigned int valid; /* valid flags for elements that are set */
    uid_t uid;
    gid_t gid;
    int grpsize;
    int ngrps;
    gid_t *groups;
    pid_t pid;
    gf_lkowner_t lk_owner;
};

#define __yawn(args)                                                           \
    do {                                                                       \
        args->task = synctask_get();                                           \
        if (args->task)                                                        \
            break;                                                             \
        pthread_mutex_init(&args->mutex, NULL);                                \
        pthread_cond_init(&args->cond, NULL);                                  \
        args->done = 0;                                                        \
    } while (0)

#define __wake(args)                                                           \
    do {                                                                       \
        if (args->task) {                                                      \
            synctask_wake(args->task);                                         \
        } else {                                                               \
            pthread_mutex_lock(&args->mutex);                                  \
            {                                                                  \
                args->done = 1;                                                \
                pthread_cond_signal(&args->cond);                              \
            }                                                                  \
            pthread_mutex_unlock(&args->mutex);                                \
        }                                                                      \
    } while (0)

#define __yield(args)                                                          \
    do {                                                                       \
        if (args->task) {                                                      \
            synctask_yield(args->task, NULL);                                  \
        } else {                                                               \
            pthread_mutex_lock(&args->mutex);                                  \
            {                                                                  \
                while (!args->done)                                            \
                    pthread_cond_wait(&args->cond, &args->mutex);              \
            }                                                                  \
            pthread_mutex_unlock(&args->mutex);                                \
            pthread_mutex_destroy(&args->mutex);                               \
            pthread_cond_destroy(&args->cond);                                 \
        }                                                                      \
    } while (0)

#define SYNCOP(subvol, stb, cbk, fn_op, params...)                             \
    do {                                                                       \
        struct synctask *task = NULL;                                          \
        call_frame_t *frame = NULL;                                            \
                                                                               \
        task = synctask_get();                                                 \
        stb->task = task;                                                      \
        if (task)                                                              \
            frame = copy_frame(task->opframe);                                 \
        else                                                                   \
            frame = syncop_create_frame(THIS);                                 \
                                                                               \
        if (!frame) {                                                          \
            stb->op_ret = -1;                                                  \
            stb->op_errno = errno;                                             \
            break;                                                             \
        }                                                                      \
                                                                               \
        if (task) {                                                            \
            frame->root->uid = task->uid;                                      \
            frame->root->gid = task->gid;                                      \
        }                                                                      \
                                                                               \
        __yawn(stb);                                                           \
                                                                               \
        frame->op = get_fop_index_from_fn(subvol, fn_op);                      \
        STACK_WIND_COOKIE(frame, cbk, (void *)stb, subvol, fn_op, params);     \
                                                                               \
        __yield(stb);                                                          \
        STACK_DESTROY(frame->root);                                            \
    } while (0)

/*
 * syncop_xxx() calls are executed in two ways, one is inside a synctask where
 * the executing function will do 'swapcontext' and the other is without
 * synctask where the executing thread is made to wait using pthread_cond_wait.
 * Executing thread may change when syncop_xxx() is executed inside a synctask.
 * This leads to errno_location change i.e. errno may give errno of
 * non-executing thread. So errno is not touched inside a synctask execution.
 * All gfapi calls are executed using the second way of executing syncop_xxx()
 * where the executing thread waits using pthread_cond_wait so it is ok to set
 * errno in these cases. The following macro makes syncop_xxx() behave just
 * like a system call, where -1 is returned and errno is set when a failure
 * occurs.
 */
#define DECODE_SYNCOP_ERR(ret)                                                 \
    do {                                                                       \
        if (ret < 0) {                                                         \
            errno = -ret;                                                      \
            ret = -1;                                                          \
        } else {                                                               \
            errno = 0;                                                         \
        }                                                                      \
    } while (0)

#define SYNCENV_DEFAULT_STACKSIZE (2 * 1024 * 1024)

struct syncenv *
syncenv_new(size_t stacksize, int procmin, int procmax);
void
syncenv_destroy(struct syncenv *);
void
syncenv_scale(struct syncenv *env);

int
synctask_new1(struct syncenv *, size_t stacksize, synctask_fn_t, synctask_cbk_t,
              call_frame_t *frame, void *);
int
synctask_new(struct syncenv *, synctask_fn_t, synctask_cbk_t,
             call_frame_t *frame, void *);
struct synctask *
synctask_create(struct syncenv *, size_t stacksize, synctask_fn_t,
                synctask_cbk_t, call_frame_t *, void *);
int
synctask_join(struct synctask *task);
void
synctask_wake(struct synctask *task);
void
synctask_yield(struct synctask *task, struct timespec *delta);
void
synctask_sleep(int32_t secs);
void
synctask_usleep(int32_t usecs);
void
synctask_waitfor(struct synctask *task, int count);

#define synctask_barrier_init(args) syncbarrier_init(&args->barrier)
#define synctask_barrier_wait(args, n) syncbarrier_wait(&args->barrier, n)
#define synctask_barrier_wake(args) syncbarrier_wake(&args->barrier)

int
synctask_setid(struct synctask *task, uid_t uid, gid_t gid);
#define SYNCTASK_SETID(uid, gid) synctask_setid(synctask_get(), uid, gid);

int
syncopctx_setfsuid(void *uid);
int
syncopctx_setfsgid(void *gid);
int
syncopctx_setfsgroups(int count, const void *groups);
int
syncopctx_setfspid(void *pid);
int
syncopctx_setfslkowner(gf_lkowner_t *lk_owner);

static inline call_frame_t *
syncop_create_frame(xlator_t *this)
{
    call_frame_t *frame = NULL;
    int ngrps = -1;
    struct syncopctx *opctx = NULL;

    frame = create_frame(this, this->ctx->pool);
    if (!frame)
        return NULL;

    frame->root->type = GF_OP_TYPE_FOP;
    opctx = syncopctx_getctx();

    if (opctx && (opctx->valid & SYNCOPCTX_PID))
        frame->root->pid = opctx->pid;
    else
        frame->root->pid = getpid();

    if (opctx && (opctx->valid & SYNCOPCTX_UID))
        frame->root->uid = opctx->uid;
    else
        frame->root->uid = geteuid();

    if (opctx && (opctx->valid & SYNCOPCTX_GID))
        frame->root->gid = opctx->gid;
    else
        frame->root->gid = getegid();

    if (opctx && (opctx->valid & SYNCOPCTX_GROUPS)) {
        ngrps = opctx->ngrps;

        if (ngrps != 0 && opctx->groups != NULL) {
            if (call_stack_alloc_groups(frame->root, ngrps) != 0) {
                STACK_DESTROY(frame->root);
                return NULL;
            }

            memcpy(frame->root->groups, opctx->groups, (sizeof(gid_t) * ngrps));
        }
    } else {
        ngrps = getgroups(0, 0);
        if (ngrps < 0) {
            STACK_DESTROY(frame->root);
            return NULL;
        }

        if (call_stack_alloc_groups(frame->root, ngrps) != 0) {
            STACK_DESTROY(frame->root);
            return NULL;
        }

        if (getgroups(ngrps, frame->root->groups) < 0) {
            STACK_DESTROY(frame->root);
            return NULL;
        }
    }

    if (opctx && (opctx->valid & SYNCOPCTX_LKOWNER))
        frame->root->lk_owner = opctx->lk_owner;

    return frame;
}

int
synclock_init(synclock_t *lock, lock_attr_t attr);
int
synclock_destroy(synclock_t *lock);
int
synclock_lock(synclock_t *lock);
int
synclock_trylock(synclock_t *lock);
int
synclock_unlock(synclock_t *lock);

int32_t
synccond_init(synccond_t *cond);

void
synccond_destroy(synccond_t *cond);

int
synccond_wait(synccond_t *cond, synclock_t *lock);

int
synccond_timedwait(synccond_t *cond, synclock_t *lock, struct timespec *delta);

void
synccond_signal(synccond_t *cond);

void
synccond_broadcast(synccond_t *cond);

int
syncbarrier_init(syncbarrier_t *barrier);
int
syncbarrier_wait(syncbarrier_t *barrier, int waitfor);
int
syncbarrier_wake(syncbarrier_t *barrier);
int
syncbarrier_destroy(syncbarrier_t *barrier);

int
syncop_lookup(xlator_t *subvol, loc_t *loc,
              /* out */
              struct iatt *iatt, struct iatt *parent,
              /* xdata */
              dict_t *xdata_in, dict_t **xdata_out);

int
syncop_readdirp(xlator_t *subvol, fd_t *fd, size_t size, off_t off,
                /* out */
                gf_dirent_t *entries, dict_t *xdata_in, dict_t **xdata_out);

int
syncop_readdir(xlator_t *subvol, fd_t *fd, size_t size, off_t off,
               gf_dirent_t *entries, dict_t *xdata_in, dict_t **xdata_out);

int
syncop_opendir(xlator_t *subvol, loc_t *loc, fd_t *fd, dict_t *xdata_in,
               dict_t **xdata_out);

int
syncop_setattr(xlator_t *subvol, loc_t *loc, struct iatt *iatt, int valid,
               /* out */
               struct iatt *preop, struct iatt *postop, dict_t *xdata_in,
               dict_t **xdata_out);

int
syncop_fsetattr(xlator_t *subvol, fd_t *fd, struct iatt *iatt, int valid,
                /* out */
                struct iatt *preop, struct iatt *postop, dict_t *xdata_in,
                dict_t **xdata_out);

int
syncop_statfs(xlator_t *subvol, loc_t *loc,
              /* out */
              struct statvfs *buf, dict_t *xdata_in, dict_t **xdata_out);

int
syncop_setxattr(xlator_t *subvol, loc_t *loc, dict_t *dict, int32_t flags,
                dict_t *xdata_in, dict_t **xdata_out);

int
syncop_fsetxattr(xlator_t *subvol, fd_t *fd, dict_t *dict, int32_t flags,
                 dict_t *xdata_in, dict_t **xdata_out);

int
syncop_listxattr(xlator_t *subvol, loc_t *loc, dict_t **dict, dict_t *xdata_in,
                 dict_t **xdata_out);

int
syncop_getxattr(xlator_t *xl, loc_t *loc, dict_t **dict, const char *key,
                dict_t *xdata_in, dict_t **xdata_out);

int
syncop_fgetxattr(xlator_t *xl, fd_t *fd, dict_t **dict, const char *key,
                 dict_t *xdata_in, dict_t **xdata_out);

int
syncop_removexattr(xlator_t *subvol, loc_t *loc, const char *name,
                   dict_t *xdata_in, dict_t **xdata_out);

int
syncop_fremovexattr(xlator_t *subvol, fd_t *fd, const char *name,
                    dict_t *xdata_in, dict_t **xdata_out);

int
syncop_create(xlator_t *subvol, loc_t *loc, int32_t flags, mode_t mode,
              fd_t *fd, struct iatt *iatt, dict_t *xdata_in,
              dict_t **xdata_out);

int
syncop_open(xlator_t *subvol, loc_t *loc, int32_t flags, fd_t *fd,
            dict_t *xdata_in, dict_t **xdata_out);

int
syncop_close(fd_t *fd);

int
syncop_write(xlator_t *subvol, fd_t *fd, const char *buf, int size,
             off_t offset, struct iobref *iobref, uint32_t flags,
             dict_t *xdata_in, dict_t **xdata_out);

int
syncop_writev(xlator_t *subvol, fd_t *fd, const struct iovec *vector,
              int32_t count, off_t offset, struct iobref *iobref,
              uint32_t flags, struct iatt *preiatt, struct iatt *postiatt,
              dict_t *xdata_in, dict_t **xdata_out);

int
syncop_readv(xlator_t *subvol, fd_t *fd, size_t size, off_t off, uint32_t flags,
             /* out */
             struct iovec **vector, int *count, struct iobref **iobref,
             struct iatt *iatt, dict_t *xdata_in, dict_t **xdata_out);

int
syncop_ftruncate(xlator_t *subvol, fd_t *fd, off_t offset, struct iatt *preiatt,
                 struct iatt *postiatt, dict_t *xdata_in, dict_t **xdata_out);

int
syncop_truncate(xlator_t *subvol, loc_t *loc, off_t offset, dict_t *xdata_in,
                dict_t **xdata_out);

int
syncop_unlink(xlator_t *subvol, loc_t *loc, dict_t *xdata_in,
              dict_t **xdata_out);

int
syncop_rmdir(xlator_t *subvol, loc_t *loc, int flags, dict_t *xdata_in,
             dict_t **xdata_out);

int
syncop_fsync(xlator_t *subvol, fd_t *fd, int dataonly, struct iatt *preiatt,
             struct iatt *postiatt, dict_t *xdata_in, dict_t **xdata_out);

int
syncop_flush(xlator_t *subvol, fd_t *fd, dict_t *xdata_in, dict_t **xdata_out);

int
syncop_fstat(xlator_t *subvol, fd_t *fd, struct iatt *stbuf, dict_t *xdata_in,
             dict_t **xdata_out);

int
syncop_stat(xlator_t *subvol, loc_t *loc, struct iatt *stbuf, dict_t *xdata_in,
            dict_t **xdata_out);

int
syncop_symlink(xlator_t *subvol, loc_t *loc, const char *newpath,
               struct iatt *iatt, dict_t *xdata_in, dict_t **xdata_out);

int
syncop_readlink(xlator_t *subvol, loc_t *loc, char **buffer, size_t size,
                dict_t *xdata_in, dict_t **xdata_out);

int
syncop_mknod(xlator_t *subvol, loc_t *loc, mode_t mode, dev_t rdev,
             struct iatt *iatt, dict_t *xdata_in, dict_t **xdata_out);

int
syncop_mkdir(xlator_t *subvol, loc_t *loc, mode_t mode, struct iatt *iatt,
             dict_t *xdata_in, dict_t **xdata_out);

int
syncop_link(xlator_t *subvol, loc_t *oldloc, loc_t *newloc, struct iatt *iatt,
            dict_t *xdata_in, dict_t **xdata_out);

int
syncop_fsyncdir(xlator_t *subvol, fd_t *fd, int datasync, dict_t *xdata_in,
                dict_t **xdata_out);

int
syncop_access(xlator_t *subvol, loc_t *loc, int32_t mask, dict_t *xdata_in,
              dict_t **xdata_out);

int
syncop_fallocate(xlator_t *subvol, fd_t *fd, int32_t keep_size, off_t offset,
                 size_t len, dict_t *xdata_in, dict_t **xdata_out);

int
syncop_discard(xlator_t *subvol, fd_t *fd, off_t offset, size_t len,
               dict_t *xdata_in, dict_t **xdata_out);

int
syncop_zerofill(xlator_t *subvol, fd_t *fd, off_t offset, off_t len,
                dict_t *xdata_in, dict_t **xdata_out);

int
syncop_rename(xlator_t *subvol, loc_t *oldloc, loc_t *newloc, dict_t *xdata_in,
              dict_t **xdata_out);

int
syncop_lk(xlator_t *subvol, fd_t *fd, int cmd, struct gf_flock *flock,
          dict_t *xdata_in, dict_t **xdata_out);

int
syncop_inodelk(xlator_t *subvol, const char *volume, loc_t *loc, int32_t cmd,
               struct gf_flock *lock, dict_t *xdata_in, dict_t **xdata_out);

int
syncop_lease(xlator_t *subvol, loc_t *loc, struct gf_lease *lease,
             dict_t *xdata_in, dict_t **xdata_out);

int
syncop_ipc(xlator_t *subvol, int op, dict_t *xdata_in, dict_t **xdata_out);

int
syncop_xattrop(xlator_t *subvol, loc_t *loc, gf_xattrop_flags_t flags,
               dict_t *dict, dict_t *xdata_in, dict_t **dict_out,
               dict_t **xdata_out);

int
syncop_fxattrop(xlator_t *subvol, fd_t *fd, gf_xattrop_flags_t flags,
                dict_t *dict, dict_t *xdata_in, dict_t **dict_out,
                dict_t **xdata_out);

int
syncop_seek(xlator_t *subvol, fd_t *fd, off_t offset, gf_seek_what_t what,
            dict_t *xdata_in, off_t *off);

int
syncop_getactivelk(xlator_t *subvol, loc_t *loc,
                   lock_migration_info_t *locklist, dict_t *xdata_in,
                   dict_t **xdata_out);

int
syncop_setactivelk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata);

int
syncop_setactivelk(xlator_t *subvol, loc_t *loc,
                   lock_migration_info_t *locklist, dict_t *xdata_in,
                   dict_t **xdata_out);

int
syncop_put(xlator_t *subvol, loc_t *loc, mode_t mode, mode_t umask,
           uint32_t flags, struct iovec *vector, int32_t count, off_t offset,
           struct iobref *iobref, dict_t *xattr, struct iatt *iatt,
           dict_t *xdata_in, dict_t **xdata_out);

int
syncop_setactivelk_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata);

int
syncop_icreate(xlator_t *subvol, loc_t *loc, mode_t mode, dict_t *xdata_out);

int
syncop_entrylk(xlator_t *subvol, const char *volume, loc_t *loc,
               const char *basename, entrylk_cmd cmd, entrylk_type type,
               dict_t *xdata_in, dict_t **xdata_out);

int
syncop_copy_file_range(xlator_t *subvol, fd_t *fd_in, off64_t off_in,
                       fd_t *fd_out, off64_t off_out, size_t len,
                       uint32_t flags, struct iatt *stbuf,
                       struct iatt *preiatt_dst, struct iatt *postiatt_dst,
                       dict_t *xdata_in, dict_t **xdata_out);

int
syncop_copy_file_range_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                           int op_ret, int op_errno, struct iatt *stbuf,
                           struct iatt *prebuf_dst, struct iatt *postbuf_dst,
                           dict_t *xdata);

#endif /* _SYNCOP_H */
