/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "syncop.h"

call_frame_t *
syncop_create_frame ()
{
        struct synctask *task = NULL;
        call_frame_t *frame = NULL;

        task = synctask_get ();

        if (task) {
                frame = task->frame;
        }

        return (call_frame_t *)frame;
}

void
synctask_yield (struct synctask *task)
{
        struct syncenv   *env = NULL;

        env = task->env;

        if (swapcontext (&task->ctx, &env->sched) < 0) {
                gf_log ("syncop", GF_LOG_ERROR,
                        "swapcontext failed (%s)", strerror (errno));
        }
}


void
synctask_yawn (struct synctask *task)
{
        struct syncenv  *env = NULL;

        env  = task->env;

        pthread_mutex_lock (&env->mutex);
        {
                list_del_init (&task->all_tasks);
                list_add (&task->all_tasks, &env->waitq);
        }
        pthread_mutex_unlock (&env->mutex);
}


void
synctask_zzzz (struct synctask *task)
{
        synctask_yawn (task);

        synctask_yield (task);
}


void
synctask_wake (struct synctask *task)
{
        struct syncenv *env = NULL;

        env = task->env;

        pthread_mutex_lock (&env->mutex);
        {
                list_del_init (&task->all_tasks);
                list_add_tail (&task->all_tasks, &env->runq);
        }
        pthread_mutex_unlock (&env->mutex);

        pthread_cond_broadcast (&env->cond);
}


void
synctask_wrap (struct synctask *task)
{
        int              ret;

        ret = task->syncfn (task->opaque);
        task->synccbk (ret, task->frame, task->opaque);

        /* cannot destroy @task right here as we are
           in the execution stack of @task itself
        */
        task->complete = 1;
        synctask_wake (task);

        synctask_yield (task);
}


void
synctask_destroy (struct synctask *task)
{
        if (!task)
                return;

        if (task->stack)
                FREE (task->stack);
        FREE (task);
}


int
synctask_new (struct syncenv *env, synctask_fn_t fn, synctask_cbk_t cbk,
              call_frame_t *frame, void *opaque)
{
        struct synctask *newtask = NULL;
        xlator_t        *this    = THIS;

        VALIDATE_OR_GOTO (env, err);
        VALIDATE_OR_GOTO (fn, err);
        VALIDATE_OR_GOTO (cbk, err);
        VALIDATE_OR_GOTO (frame, err);

        newtask = CALLOC (1, sizeof (*newtask));
        if (!newtask)
                return -ENOMEM;

        newtask->env        = env;
        newtask->xl         = this;
        newtask->syncfn     = fn;
        newtask->synccbk    = cbk;
        newtask->opaque     = opaque;
        newtask->frame      = frame;

        INIT_LIST_HEAD (&newtask->all_tasks);

        if (getcontext (&newtask->ctx) < 0) {
                gf_log ("syncop", GF_LOG_ERROR,
                        "getcontext failed (%s)",
                        strerror (errno));
                goto err;
        }

        newtask->stack = CALLOC (1, env->stacksize);
        if (!newtask->stack) {
                gf_log ("syncop", GF_LOG_ERROR,
                        "out of memory for stack");
                goto err;
        }

        newtask->ctx.uc_stack.ss_sp   = newtask->stack;
        newtask->ctx.uc_stack.ss_size = env->stacksize;

        makecontext (&newtask->ctx, (void *) synctask_wrap, 2, newtask);

        synctask_wake (newtask);

        return 0;
err:
        if (newtask) {
                if (newtask->stack)
                        FREE (newtask->stack);
                FREE (newtask);
        }
        return -1;
}


struct synctask *
syncenv_task (struct syncenv *env)
{
        struct synctask  *task = NULL;

        pthread_mutex_lock (&env->mutex);
        {
                while (list_empty (&env->runq))
                        pthread_cond_wait (&env->cond, &env->mutex);

                task = list_entry (env->runq.next, struct synctask, all_tasks);

                list_del_init (&task->all_tasks);
        }
        pthread_mutex_unlock (&env->mutex);

        return task;
}


void
synctask_switchto (struct synctask *task)
{
        struct syncenv *env = NULL;

        env = task->env;

        synctask_set (task);
        THIS = task->xl;

        if (swapcontext (&env->sched, &task->ctx) < 0) {
                gf_log ("syncop", GF_LOG_ERROR,
                        "swapcontext failed (%s)", strerror (errno));
        }
}


void *
syncenv_processor (void *thdata)
{
        struct syncenv  *env = NULL;
        struct synctask *task = NULL;

        env = thdata;

        for (;;) {
                task = syncenv_task (env);

                if (task->complete) {
                        synctask_destroy (task);
                        continue;
                }

                synctask_switchto (task);
        }

        return NULL;
}


void
syncenv_destroy (struct syncenv *env)
{

}


struct syncenv *
syncenv_new (size_t stacksize)
{
        struct syncenv *newenv = NULL;
        int             ret = 0;

        newenv = CALLOC (1, sizeof (*newenv));

        if (!newenv)
                return NULL;

        pthread_mutex_init (&newenv->mutex, NULL);
        pthread_cond_init (&newenv->cond, NULL);

        INIT_LIST_HEAD (&newenv->runq);
        INIT_LIST_HEAD (&newenv->waitq);

        newenv->stacksize    = SYNCENV_DEFAULT_STACKSIZE;
        if (stacksize)
                newenv->stacksize = stacksize;

        ret = pthread_create (&newenv->processor, NULL,
                              syncenv_processor, newenv);

        if (ret != 0)
                syncenv_destroy (newenv);

        return newenv;
}


/* FOPS */


int
syncop_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno, inode_t *inode,
                   struct iatt *iatt, dict_t *xattr, struct iatt *parent)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        if (op_ret == 0) {
                args->iatt1  = *iatt;
                args->iatt2  = *parent;
                if (xattr)
                        args->xattr  = dict_ref (xattr);
        }

        __wake (args);

        return 0;
}


int
syncop_lookup (xlator_t *subvol, loc_t *loc, dict_t *xattr_req,
               struct iatt *iatt, dict_t **xattr_rsp, struct iatt *parent)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_lookup_cbk, subvol->fops->lookup,
                loc, xattr_req);

        if (iatt)
                *iatt = args.iatt1;
        if (xattr_rsp)
                *xattr_rsp = args.xattr;
        if (parent)
                *parent = args.iatt2;

        errno = args.op_errno;
        return args.op_ret;
}

static gf_dirent_t *
entry_copy (gf_dirent_t *source)
{
        gf_dirent_t *sink = NULL;

        sink = gf_dirent_for_name (source->d_name);

        sink->d_off = source->d_off;
        sink->d_ino = source->d_ino;
        sink->d_type = source->d_type;

        return sink;
}

int32_t
syncop_readdirp_cbk (call_frame_t *frame,
                     void *cookie,
                     xlator_t *this,
                     int32_t op_ret,
                     int32_t op_errno,
                     gf_dirent_t *entries)
{
        struct syncargs *args = NULL;
        gf_dirent_t *entry = NULL;
        gf_dirent_t  *tmp = NULL;

        int count = 0;

        args = cookie;

        INIT_LIST_HEAD (&args->entries.list);

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        if (op_ret >= 0) {
                list_for_each_entry (entry, &entries->list, list) {
                        tmp = entry_copy (entry);
                        gf_log (this->name, GF_LOG_TRACE,
                                "adding entry=%s, count=%d",
                                tmp->d_name, count);
                        list_add_tail (&tmp->list, &(args->entries.list));
                        count++;
                }
        }

        __wake (args);

        return 0;

}

int
syncop_readdirp (xlator_t *subvol,
                 fd_t *fd,
                 size_t size,
                 off_t off,
                 gf_dirent_t *entries)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_readdirp_cbk, subvol->fops->readdirp,
                fd, size, off);

        if (entries)
                list_splice_init (&args.entries.list, &entries->list);

        errno = args.op_errno;
        return args.op_ret;

}

int32_t
syncop_opendir_cbk (call_frame_t *frame,
                    void *cookie,
                    xlator_t *this,
                    int32_t op_ret,
                    int32_t op_errno,
                    fd_t *fd)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}

int
syncop_opendir (xlator_t *subvol,
                loc_t *loc,
                fd_t *fd)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_opendir_cbk, subvol->fops->opendir,
                loc, fd);

        errno = args.op_errno;
        return args.op_ret;

}

int
syncop_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}

int
syncop_removexattr (xlator_t *subvol, loc_t *loc, const char *name)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_removexattr_cbk, subvol->fops->removexattr,
                loc, name);

        errno = args.op_errno;
        return args.op_ret;
}

int
syncop_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int op_ret, int op_errno)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}


int
syncop_setxattr (xlator_t *subvol, loc_t *loc, dict_t *dict, int32_t flags)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_setxattr_cbk, subvol->fops->setxattr,
                loc, dict, flags);

        errno = args.op_errno;
        return args.op_ret;
}

int
syncop_listxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, dict_t *dict)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;
        if (op_ret >= 0)
                args->xattr    = dict_ref (dict);

        __wake (args);

        return 0;
}

int
syncop_listxattr (xlator_t *subvol, loc_t *loc, dict_t **dict)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_listxattr_cbk, subvol->fops->getxattr,
                loc, NULL);

        if (dict)
                *dict = args.xattr;

        errno = args.op_errno;
        return args.op_ret;
}

int
syncop_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno,
                   struct statvfs *buf)

{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        if (op_ret == 0) {
                args->statvfs_buf  = *buf;
        }

        __wake (args);

        return 0;
}


int
syncop_statfs (xlator_t *subvol, loc_t *loc, struct statvfs *buf)

{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_statfs_cbk, subvol->fops->statfs,
                loc);

        if (buf)
                *buf = args.statvfs_buf;

        errno = args.op_errno;
        return args.op_ret;
}

int
syncop_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno,
                    struct iatt *preop, struct iatt *postop)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        if (op_ret == 0) {
                args->iatt1  = *preop;
                args->iatt2  = *postop;
        }

        __wake (args);

        return 0;
}


int
syncop_setattr (xlator_t *subvol, loc_t *loc, struct iatt *iatt, int valid,
                struct iatt *preop, struct iatt *postop)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_setattr_cbk, subvol->fops->setattr,
                loc, iatt, valid);

        if (preop)
                *preop = args.iatt1;
        if (postop)
                *postop = args.iatt2;

        errno = args.op_errno;
        return args.op_ret;
}


int
syncop_fsetattr (xlator_t *subvol, fd_t *fd, struct iatt *iatt, int valid,
                 struct iatt *preop, struct iatt *postop)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_setattr_cbk, subvol->fops->fsetattr,
                fd, iatt, valid);

        if (preop)
                *preop = args.iatt1;
        if (postop)
                *postop = args.iatt2;

        errno = args.op_errno;
        return args.op_ret;
}


int32_t
syncop_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        if (op_ret != -1)
                fd_ref (fd);

        __wake (args);

        return 0;
}

int
syncop_open (xlator_t *subvol, loc_t *loc, int32_t flags, fd_t *fd)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_open_cbk, subvol->fops->open,
                loc, flags, fd, 0);

        errno = args.op_errno;
        return args.op_ret;

}


int32_t
syncop_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iovec *vector,
                  int32_t count, struct iatt *stbuf, struct iobref *iobref)
{
        struct syncargs *args = NULL;

        args = cookie;

        INIT_LIST_HEAD (&args->entries.list);

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        if (args->op_ret >= 0) {
                if (iobref)
                        args->iobref = iobref_ref (iobref);
                args->vector = iov_dup (vector, count);
                args->count  = count;
        }

        __wake (args);

        return 0;

}

int
syncop_readv (xlator_t *subvol, fd_t *fd, size_t size, off_t off,
              struct iovec **vector, int *count, struct iobref **iobref)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_readv_cbk, subvol->fops->readv,
                fd, size, off);

        if (vector)
                *vector = args.vector;

        if (count)
                *count = args.count;

        /* Do we need a 'ref' here? */
        if (iobref)
                *iobref = args.iobref;

        errno = args.op_errno;
        return args.op_ret;

}

int
syncop_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno, struct iatt *prebuf,
                   struct iatt *postbuf)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}

int
syncop_writev (xlator_t *subvol, fd_t *fd, struct iovec *vector,
               int32_t count, off_t offset, struct iobref *iobref)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_writev_cbk, subvol->fops->writev,
                fd, vector, count, offset, iobref);

        errno = args.op_errno;
        return args.op_ret;
}

int syncop_write (xlator_t *subvol, fd_t *fd, const char *buf, int size,
                  off_t offset, struct iobref *iobref)
{
        struct syncargs args = {0,};
        struct iovec    vec  = {0,};

        vec.iov_len = size;
        vec.iov_base = (void *)buf;

        SYNCOP (subvol, (&args), syncop_writev_cbk, subvol->fops->writev,
                fd, &vec, 1, offset, iobref);

        errno = args.op_errno;
        return args.op_ret;
}


int
syncop_close (fd_t *fd)
{
        if (fd)
                fd_unref (fd);
        return 0;
}

int32_t
syncop_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        if (op_ret != -1)
                fd_ref (fd);

        __wake (args);

        return 0;
}

int
syncop_create (xlator_t *subvol, loc_t *loc, int32_t flags, mode_t mode,
               fd_t *fd, dict_t *dict)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_create_cbk, subvol->fops->create,
                loc, flags, mode, fd, dict);

        errno = args.op_errno;
        return args.op_ret;

}

int
syncop_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno, struct iatt *preparent,
                   struct iatt *postparent)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}

int
syncop_unlink (xlator_t *subvol, loc_t *loc)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_unlink_cbk, subvol->fops->unlink, loc);

        errno = args.op_errno;
        return args.op_ret;
}

int
syncop_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, struct iatt *prebuf,
                      struct iatt *postbuf)
{
        struct syncargs *args = NULL;

        args = cookie;

        args->op_ret   = op_ret;
        args->op_errno = op_errno;

        __wake (args);

        return 0;
}

int
syncop_ftruncate (xlator_t *subvol, fd_t *fd, off_t offset)
{
        struct syncargs args = {0, };

        SYNCOP (subvol, (&args), syncop_ftruncate_cbk, subvol->fops->ftruncate,
                fd, offset);

        errno = args.op_errno;
        return args.op_ret;
}
