/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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


struct synctask;
struct syncenv;


typedef int (*synctask_cbk_t) (int ret, call_frame_t *frame, void *opaque);

typedef int (*synctask_fn_t) (void *opaque);


/* for one sequential execution of @syncfn */
struct synctask {
        struct list_head    all_tasks;
        struct syncenv     *env;
        xlator_t           *xl;
        call_frame_t       *frame;
        synctask_cbk_t      synccbk;
        synctask_fn_t       syncfn;
        void               *opaque;
        void               *stack;
        int                 complete;

        ucontext_t          ctx;
};

/* hosts the scheduler thread and framework for executing synctasks */
struct syncenv {
        pthread_t           processor;
        struct synctask    *current;

        struct list_head    runq;
        struct list_head    waitq;

        pthread_mutex_t     mutex;
        pthread_cond_t      cond;

        ucontext_t          sched;
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

        /* do not touch */
        pthread_mutex_t     mutex;
        char                complete;
        pthread_cond_t      cond;
        struct synctask    *task;
};


#define __yawn(args) do {                                               \
                struct synctask *task = NULL;                           \
                                                                        \
                task = synctask_get ();                                 \
                if (task) {                                             \
                        args->task = task;                              \
                        synctask_yawn (task);                           \
                } else {                                                \
                        pthread_mutex_init (&args->mutex, NULL);        \
                        pthread_cond_init (&args->cond, NULL);          \
                }                                                       \
        } while (0)


#define __yield(args) do {                                              \
                if (args->task) {                                       \
                        synctask_yield (args->task);                    \
                } else {                                                \
                        pthread_mutex_lock (&args->mutex);              \
                        {                                               \
                                while (!args->complete)                 \
                                        pthread_cond_wait (&args->cond, \
                                                           &args->mutex); \
                        }                                               \
                        pthread_mutex_unlock (&args->mutex);            \
                                                                        \
                        pthread_mutex_destroy (&args->mutex);           \
                        pthread_cond_destroy (&args->cond);             \
                }                                                       \
        } while (0)


#define __wake(args) do {                                               \
                if (args->task) {                                       \
                        synctask_wake (args->task);                     \
                } else {                                                \
                        pthread_mutex_lock (&args->mutex);              \
                        {                                               \
                                args->complete = 1;                     \
                                pthread_cond_broadcast (&args->cond);   \
                        }                                               \
                        pthread_mutex_unlock (&args->mutex);            \
                }                                                       \
        } while (0)


#define SYNCOP(subvol, stb, cbk, op, params ...) do {                   \
                call_frame_t    *frame = NULL;                          \
                                                                        \
                frame = syncop_create_frame ();                         \
                                                                        \
                __yawn (stb);                                           \
                STACK_WIND_COOKIE (frame, cbk, (void *)stb, subvol, op, params); \
                __yield (stb);                                          \
        } while (0)


#define SYNCENV_DEFAULT_STACKSIZE (2 * 1024 * 1024)

struct syncenv * syncenv_new ();
void syncenv_destroy (struct syncenv *);

int synctask_new (struct syncenv *, synctask_fn_t, synctask_cbk_t, call_frame_t* frame, void *);
void synctask_zzzz (struct synctask *task);
void synctask_yawn (struct synctask *task);
void synctask_wake (struct synctask *task);
void synctask_yield (struct synctask *task);

int syncop_lookup (xlator_t *subvol, loc_t *loc, dict_t *xattr_req,
                   /* out */
                   struct iatt *iatt, dict_t **xattr_rsp, struct iatt *parent);

int syncop_readdirp (xlator_t *subvol, fd_t *fd, size_t size, off_t off,
                     dict_t *dict,
                     /* out */
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
int syncop_writev (xlator_t *subvol, fd_t *fd, struct iovec *vector,
                   int32_t count, off_t offset, struct iobref *iobref,
                   uint32_t flags);
int syncop_readv (xlator_t *subvol, fd_t *fd, size_t size, off_t off,
                  uint32_t flags,
                  /* out */
                  struct iovec **vector, int *count, struct iobref **iobref);

int syncop_ftruncate (xlator_t *subvol, fd_t *fd, off_t offset);
int syncop_truncate (xlator_t *subvol, loc_t *loc, off_t offset);

int syncop_unlink (xlator_t *subvol, loc_t *loc);

int syncop_fsync (xlator_t *subvol, fd_t *fd);
int syncop_fstat (xlator_t *subvol, fd_t *fd, struct iatt *stbuf);
int syncop_stat (xlator_t *subvol, loc_t *loc, struct iatt *stbuf);

int syncop_symlink (xlator_t *subvol, loc_t *loc, char *newpath, dict_t *dict);
int syncop_readlink (xlator_t *subvol, loc_t *loc, char **buffer, size_t size);
int syncop_mknod (xlator_t *subvol, loc_t *loc, mode_t mode, dev_t rdev,
                  dict_t *dict);

#endif /* _SYNCOP_H */
