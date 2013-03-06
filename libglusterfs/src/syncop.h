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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include <sys/time.h>
#include <pthread.h>
#include <ucontext.h>

#define SYNCENV_PROC_MAX 16
#define SYNCENV_PROC_MIN 2
#define SYNCPROC_IDLE_TIME 600

struct synctask;
struct syncproc;
struct syncenv;


typedef int (*synctask_cbk_t) (int ret, call_frame_t *frame, void *opaque);

typedef int (*synctask_fn_t) (void *opaque);


typedef enum {
        SYNCTASK_INIT = 0,
        SYNCTASK_RUN,
        SYNCTASK_SUSPEND,
        SYNCTASK_WAIT,
        SYNCTASK_DONE,
} synctask_state_t;

/* for one sequential execution of @syncfn */
struct synctask {
        struct list_head    all_tasks;
        struct syncenv     *env;
        xlator_t           *xl;
        call_frame_t       *frame;
        call_frame_t       *opframe;
        synctask_cbk_t      synccbk;
        synctask_fn_t       syncfn;
        synctask_state_t    state;
        void               *opaque;
        void               *stack;
        int                 woken;
        int                 slept;
        int                 waitfor;
        int                 ret;

        uid_t               uid;
        gid_t               gid;

        ucontext_t          ctx;
        struct syncproc    *proc;

        pthread_mutex_t     mutex; /* for synchronous spawning of synctask */
        pthread_cond_t      cond;
        int                 done;
};


struct syncproc {
        pthread_t           processor;
        ucontext_t          sched;
        struct syncenv     *env;
        struct synctask    *current;
};

/* hosts the scheduler thread and framework for executing synctasks */
struct syncenv {
        struct syncproc     proc[SYNCENV_PROC_MAX];
        int                 procs;

        struct list_head    runq;
        int                 runcount;
        struct list_head    waitq;
        int                 waitcount;

        pthread_mutex_t     mutex;
        pthread_cond_t      cond;

        size_t              stacksize;
};


struct syncargs {
        int                 op_ret;
        int                 op_errno;
        struct iatt         iatt1;
        struct iatt         iatt2;
        dict_t             *xattr;
        gf_dirent_t        entries;
        struct statvfs     statvfs_buf;
        struct iovec       *vector;
        int                 count;
        struct iobref      *iobref;
        char               *buffer;
        dict_t             *xdata;

        /* some more _cbk needs */
        uuid_t              uuid;
        char               *errstr;
        dict_t             *dict;

        /* do not touch */
        struct synctask    *task;
        pthread_mutex_t     mutex;
        pthread_cond_t      cond;
        int                 wakecnt;
};


#define __yawn(args) do {                                       \
        args->task = synctask_get ();                           \
        if (args->task) {                                       \
                synctask_yawn (args->task);                     \
        } else {                                                \
                pthread_mutex_init (&args->mutex, NULL);        \
                pthread_cond_init (&args->cond, NULL);          \
                args->wakecnt = 0;                              \
        }                                                       \
        } while (0)


#define __wake(args) do {                                       \
        if (args->task) {                                       \
                synctask_wake (args->task);                     \
        } else {                                                \
                pthread_mutex_lock (&args->mutex);              \
                {                                               \
                        args->wakecnt++;                        \
                        pthread_cond_signal (&args->cond);      \
                }                                               \
                pthread_mutex_unlock (&args->mutex);            \
        }                                                       \
        } while (0)


#define __waitfor(args, cnt) do {                                       \
        if (args->task) {                                               \
                synctask_waitfor (args->task, cnt);                     \
        } else {                                                        \
                pthread_mutex_lock (&args->mutex);                      \
                {                                                       \
                        while (args->wakecnt < cnt)                     \
                                pthread_cond_wait (&args->cond,         \
                                                   &args->mutex);       \
                }                                                       \
                pthread_mutex_unlock (&args->mutex);                    \
                pthread_mutex_destroy (&args->mutex);                   \
                pthread_cond_destroy (&args->cond);                     \
        }                                                               \
        } while (0)


#define __yield(args) __waitfor(args, 1)


#define SYNCOP(subvol, stb, cbk, op, params ...) do {                   \
                struct  synctask        *task = NULL;                   \
                call_frame_t            *frame = NULL;                  \
                                                                        \
                task = synctask_get ();                                 \
                stb->task = task;                                       \
                if (task)                                               \
                        frame = task->opframe;                          \
                else                                                    \
                        frame = create_frame (THIS, THIS->ctx->pool);   \
                if (task) {                                             \
                        frame->root->uid = task->uid;                   \
                        frame->root->gid = task->gid;                   \
                }                                                       \
                                                                        \
                __yawn (stb);                                           \
                                                                        \
                STACK_WIND_COOKIE (frame, cbk, (void *)stb, subvol,     \
                                   op, params);                         \
                if (task)                                               \
                        task->state = SYNCTASK_SUSPEND;                 \
                                                                        \
                __yield (stb);                                          \
                if (task)                                               \
                        STACK_RESET (frame->root);                      \
                else                                                    \
                        STACK_DESTROY (frame->root);                    \
        } while (0)


#define SYNCENV_DEFAULT_STACKSIZE (2 * 1024 * 1024)

struct syncenv * syncenv_new ();
void syncenv_destroy (struct syncenv *);
void syncenv_scale (struct syncenv *env);

int synctask_new (struct syncenv *, synctask_fn_t, synctask_cbk_t, call_frame_t* frame, void *);
void synctask_wake (struct synctask *task);
void synctask_yield (struct synctask *task);
void synctask_yawn (struct synctask *task);
void synctask_waitfor (struct synctask *task, int count);

#define synctask_barrier_init(args) __yawn (args)
#define synctask_barrier_wait(args, n) __waitfor (args, n)
#define synctask_barrier_wake(args) __wake (args)

int synctask_setid (struct synctask *task, uid_t uid, gid_t gid);
#define SYNCTASK_SETID(uid, gid) synctask_setid (synctask_get(), uid, gid);

int syncop_lookup (xlator_t *subvol, loc_t *loc, dict_t *xattr_req,
                   /* out */
                   struct iatt *iatt, dict_t **xattr_rsp, struct iatt *parent);

int syncop_readdirp (xlator_t *subvol, fd_t *fd, size_t size, off_t off,
                     dict_t *dict,
                     /* out */
                     gf_dirent_t *entries);

int syncop_readdir (xlator_t *subvol, fd_t *fd, size_t size, off_t off,
                    gf_dirent_t *entries);

int syncop_opendir (xlator_t *subvol, loc_t *loc, fd_t *fd);

int syncop_setattr (xlator_t *subvol, loc_t *loc, struct iatt *iatt, int valid,
                    /* out */
                    struct iatt *preop, struct iatt *postop);

int syncop_fsetattr (xlator_t *subvol, fd_t *fd, struct iatt *iatt, int valid,
                    /* out */
                    struct iatt *preop, struct iatt *postop);

int syncop_statfs (xlator_t *subvol, loc_t *loc, struct statvfs *buf);

int syncop_setxattr (xlator_t *subvol, loc_t *loc, dict_t *dict, int32_t flags);
int syncop_fsetxattr (xlator_t *subvol, fd_t *fd, dict_t *dict, int32_t flags);
int syncop_listxattr (xlator_t *subvol, loc_t *loc, dict_t **dict);
int syncop_getxattr (xlator_t *xl, loc_t *loc, dict_t **dict, const char *key);
int syncop_fgetxattr (xlator_t *xl, fd_t *fd, dict_t **dict, const char *key);
int syncop_removexattr (xlator_t *subvol, loc_t *loc, const char *name);
int syncop_fremovexattr (xlator_t *subvol, fd_t *fd, const char *name);

int syncop_create (xlator_t *subvol, loc_t *loc, int32_t flags, mode_t mode,
                   fd_t *fd, dict_t *dict);
int syncop_open (xlator_t *subvol, loc_t *loc, int32_t flags, fd_t *fd);
int syncop_close (fd_t *fd);

int syncop_write (xlator_t *subvol, fd_t *fd, const char *buf, int size,
                  off_t offset, struct iobref *iobref, uint32_t flags);
int syncop_writev (xlator_t *subvol, fd_t *fd, const struct iovec *vector,
                   int32_t count, off_t offset, struct iobref *iobref,
                   uint32_t flags);
int syncop_readv (xlator_t *subvol, fd_t *fd, size_t size, off_t off,
                  uint32_t flags,
                  /* out */
                  struct iovec **vector, int *count, struct iobref **iobref);

int syncop_ftruncate (xlator_t *subvol, fd_t *fd, off_t offset);
int syncop_truncate (xlator_t *subvol, loc_t *loc, off_t offset);

int syncop_unlink (xlator_t *subvol, loc_t *loc);
int syncop_rmdir (xlator_t *subvol, loc_t *loc);

int syncop_fsync (xlator_t *subvol, fd_t *fd, int dataonly);
int syncop_flush (xlator_t *subvol, fd_t *fd);
int syncop_fstat (xlator_t *subvol, fd_t *fd, struct iatt *stbuf);
int syncop_stat (xlator_t *subvol, loc_t *loc, struct iatt *stbuf);

int syncop_symlink (xlator_t *subvol, loc_t *loc, const char *newpath,
                    dict_t *dict);
int syncop_readlink (xlator_t *subvol, loc_t *loc, char **buffer, size_t size);
int syncop_mknod (xlator_t *subvol, loc_t *loc, mode_t mode, dev_t rdev,
                  dict_t *dict);
int syncop_mkdir (xlator_t *subvol, loc_t *loc, mode_t mode, dict_t *dict);
int syncop_link (xlator_t *subvol, loc_t *oldloc, loc_t *newloc);
int syncop_fsyncdir (xlator_t *subvol, fd_t *fd, int datasync);
int syncop_access (xlator_t *subvol, loc_t *loc, int32_t mask);

int syncop_rename (xlator_t *subvol, loc_t *oldloc, loc_t *newloc);

#endif /* _SYNCOP_H */
