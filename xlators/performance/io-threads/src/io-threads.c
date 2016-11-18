/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "call-stub.h"
#include "defaults.h"
#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "io-threads.h"
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "locking.h"
#include "io-threads-messages.h"

void *iot_worker (void *arg);
int iot_workers_scale (iot_conf_t *conf);
int __iot_workers_scale (iot_conf_t *conf);
struct volume_options options[];

#define IOT_FOP(name, frame, this, args ...)                                   \
        do {                                                                   \
                call_stub_t     *__stub     = NULL;                            \
                int              __ret      = -1;                              \
                                                                               \
                __stub = fop_##name##_stub(frame, default_##name##_resume, args);  \
                if (!__stub) {                                                 \
                        __ret = -ENOMEM;                                       \
                        goto out;                                              \
                }                                                              \
                                                                               \
                __ret = iot_schedule (frame, this, __stub);                    \
                                                                               \
        out:                                                                   \
                if (__ret < 0) {                                               \
                        default_##name##_failure_cbk (frame, -__ret);          \
                        if (__stub != NULL) {                                  \
                                call_stub_destroy (__stub);                    \
                        }                                                      \
                }                                                              \
        } while (0)

iot_client_ctx_t *
iot_get_ctx (xlator_t *this, client_t *client)
{
        iot_client_ctx_t        *ctx    = NULL;
        int                      i;

        if (client_ctx_get (client, this, (void **)&ctx) != 0) {
                ctx = GF_CALLOC (IOT_PRI_MAX, sizeof(*ctx),
                                 gf_iot_mt_client_ctx_t);
                if (ctx) {
                        for (i = 0; i < IOT_PRI_MAX; ++i) {
                                INIT_LIST_HEAD (&ctx[i].clients);
                                INIT_LIST_HEAD (&ctx[i].reqs);
                        }
                        if (client_ctx_set (client, this, ctx) != 0) {
                                GF_FREE (ctx);
                                ctx = NULL;
                        }
                }
        }

        return ctx;
}

call_stub_t *
__iot_dequeue (iot_conf_t *conf, int *pri)
{
        call_stub_t             *stub = NULL;
        int                     i = 0;
        iot_client_ctx_t        *ctx;

        *pri = -1;
        for (i = 0; i < IOT_PRI_MAX; i++) {

                if (conf->ac_iot_count[i] >= conf->ac_iot_limit[i]) {
                        continue;
                }

                if (list_empty (&conf->clients[i])) {
                        continue;
                }

                /* Get the first per-client queue for this priority. */
                ctx = list_first_entry (&conf->clients[i],
                                        iot_client_ctx_t, clients);
                if (!ctx) {
                        continue;
                }

                if (list_empty (&ctx->reqs)) {
                        continue;
                }

                /* Get the first request on that queue. */
                stub = list_first_entry (&ctx->reqs, call_stub_t, list);
                list_del_init (&stub->list);
                if (list_empty (&ctx->reqs)) {
                        list_del_init (&ctx->clients);
                } else {
                        list_rotate_left (&conf->clients[i]);
                }

                conf->ac_iot_count[i]++;
                *pri = i;
                break;
        }

        if (!stub)
                return NULL;

        conf->queue_size--;
        conf->queue_sizes[*pri]--;

        return stub;
}


void
__iot_enqueue (iot_conf_t *conf, call_stub_t *stub, int pri)
{
        client_t                *client = stub->frame->root->client;
        iot_client_ctx_t        *ctx;

        if (pri < 0 || pri >= IOT_PRI_MAX)
                pri = IOT_PRI_MAX-1;

        if (client) {
                ctx = iot_get_ctx (THIS, client);
                if (ctx) {
                        ctx = &ctx[pri];
                }
        } else {
                ctx = NULL;
        }
        if (!ctx) {
                ctx = &conf->no_client[pri];
        }

        if (list_empty (&ctx->reqs)) {
                list_add_tail (&ctx->clients, &conf->clients[pri]);
        }
        list_add_tail (&stub->list, &ctx->reqs);

        conf->queue_size++;
        conf->queue_sizes[pri]++;
}


void *
iot_worker (void *data)
{
        iot_conf_t       *conf = NULL;
        xlator_t         *this = NULL;
        call_stub_t      *stub = NULL;
        struct timespec   sleep_till = {0, };
        int               ret = 0;
        int               pri = -1;
        gf_boolean_t      bye = _gf_false;

        conf = data;
        this = conf->this;
        THIS = this;

        for (;;) {
                sleep_till.tv_sec = time (NULL) + conf->idle_time;

                pthread_mutex_lock (&conf->mutex);
                {
                        if (pri != -1) {
                                conf->ac_iot_count[pri]--;
                                pri = -1;
                        }
                        while (conf->queue_size == 0) {
                                if (conf->down) {
                                        bye = _gf_true;/*Avoid sleep*/
                                        break;
                                }

                                conf->sleep_count++;

                                ret = pthread_cond_timedwait (&conf->cond,
                                                              &conf->mutex,
                                                              &sleep_till);
                                conf->sleep_count--;

                                if (conf->down || ret == ETIMEDOUT) {
                                        bye = _gf_true;
                                        break;
                                }
                        }

                        if (bye) {
                                if (conf->down || conf->curr_count > IOT_MIN_THREADS) {
                                        conf->curr_count--;
                                        if (conf->curr_count == 0)
                                           pthread_cond_broadcast (&conf->cond);
                                        gf_msg_debug (conf->this->name, 0,
                                                      "terminated. "
                                                      "conf->curr_count=%d",
                                                      conf->curr_count);
                                } else {
                                        bye = _gf_false;
                                }
                        }

                        if (!bye)
                                stub = __iot_dequeue (conf, &pri);
                }
                pthread_mutex_unlock (&conf->mutex);

                if (stub) /* guard against spurious wakeups */
                        call_resume (stub);
                stub = NULL;

                if (bye)
                        break;
        }

        return NULL;
}


int
do_iot_schedule (iot_conf_t *conf, call_stub_t *stub, int pri)
{
        int   ret = 0;

        pthread_mutex_lock (&conf->mutex);
        {
                __iot_enqueue (conf, stub, pri);

                pthread_cond_signal (&conf->cond);

                ret = __iot_workers_scale (conf);
        }
        pthread_mutex_unlock (&conf->mutex);

        return ret;
}

char*
iot_get_pri_meaning (iot_pri_t pri)
{
        char    *name = NULL;
        switch (pri) {
        case IOT_PRI_HI:
                name = "fast";
                break;
        case IOT_PRI_NORMAL:
                name = "normal";
                break;
        case IOT_PRI_LO:
                name = "slow";
                break;
        case IOT_PRI_LEAST:
                name = "least priority";
                break;
        case IOT_PRI_MAX:
                name = "invalid";
                break;
        }
        return name;
}

int
iot_schedule (call_frame_t *frame, xlator_t *this, call_stub_t *stub)
{
        int             ret = -1;
        iot_pri_t       pri = IOT_PRI_MAX - 1;
        iot_conf_t      *conf = this->private;

        if ((frame->root->pid < GF_CLIENT_PID_MAX) && conf->least_priority) {
                pri = IOT_PRI_LEAST;
                goto out;
        }

        switch (stub->fop) {
        case GF_FOP_OPEN:
        case GF_FOP_STAT:
        case GF_FOP_FSTAT:
        case GF_FOP_LOOKUP:
        case GF_FOP_ACCESS:
        case GF_FOP_READLINK:
        case GF_FOP_OPENDIR:
        case GF_FOP_STATFS:
        case GF_FOP_READDIR:
        case GF_FOP_READDIRP:
        case GF_FOP_GETACTIVELK:
        case GF_FOP_SETACTIVELK:
                pri = IOT_PRI_HI;
                break;

        case GF_FOP_CREATE:
        case GF_FOP_FLUSH:
        case GF_FOP_LK:
        case GF_FOP_INODELK:
        case GF_FOP_FINODELK:
        case GF_FOP_ENTRYLK:
        case GF_FOP_FENTRYLK:
        case GF_FOP_LEASE:
        case GF_FOP_UNLINK:
        case GF_FOP_SETATTR:
        case GF_FOP_FSETATTR:
        case GF_FOP_MKNOD:
        case GF_FOP_MKDIR:
        case GF_FOP_RMDIR:
        case GF_FOP_SYMLINK:
        case GF_FOP_RENAME:
        case GF_FOP_LINK:
        case GF_FOP_SETXATTR:
        case GF_FOP_GETXATTR:
        case GF_FOP_FGETXATTR:
        case GF_FOP_FSETXATTR:
        case GF_FOP_REMOVEXATTR:
        case GF_FOP_FREMOVEXATTR:
                pri = IOT_PRI_NORMAL;
                break;

        case GF_FOP_READ:
        case GF_FOP_WRITE:
        case GF_FOP_FSYNC:
        case GF_FOP_TRUNCATE:
        case GF_FOP_FTRUNCATE:
        case GF_FOP_FSYNCDIR:
        case GF_FOP_XATTROP:
        case GF_FOP_FXATTROP:
        case GF_FOP_RCHECKSUM:
        case GF_FOP_FALLOCATE:
        case GF_FOP_DISCARD:
        case GF_FOP_ZEROFILL:
        case GF_FOP_SEEK:
                pri = IOT_PRI_LO;
                break;

        case GF_FOP_FORGET:
        case GF_FOP_RELEASE:
        case GF_FOP_RELEASEDIR:
        case GF_FOP_GETSPEC:
                break;
        case GF_FOP_IPC:
        default:
                return -EINVAL;
        }
out:
        gf_msg_debug (this->name, 0, "%s scheduled as %s fop",
                      gf_fop_list[stub->fop], iot_get_pri_meaning (pri));
        ret = do_iot_schedule (this->private, stub, pri);
        return ret;
}

int
iot_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        IOT_FOP (lookup, frame, this, loc, xdata);
        return 0;
}


int
iot_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
             struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        IOT_FOP (setattr, frame, this, loc, stbuf, valid, xdata);
        return 0;
}


int
iot_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
              struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        IOT_FOP (fsetattr, frame, this, fd, stbuf, valid, xdata);
        return 0;
}


int
iot_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
            dict_t *xdata)
{
        IOT_FOP (access, frame, this, loc, mask, xdata);
        return 0;
}


int
iot_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size, dict_t *xdata)
{
        IOT_FOP (readlink, frame, this, loc, size, xdata);
        return 0;
}


int
iot_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
           dev_t rdev, mode_t umask, dict_t *xdata)
{
        IOT_FOP (mknod, frame, this, loc, mode, rdev, umask, xdata);
        return 0;
}


int
iot_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
           mode_t umask, dict_t *xdata)
{
        IOT_FOP (mkdir, frame, this, loc, mode, umask, xdata);
        return 0;
}


int
iot_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags, dict_t *xdata)
{
        IOT_FOP (rmdir, frame, this, loc, flags, xdata);
        return 0;
}


int
iot_symlink (call_frame_t *frame, xlator_t *this, const char *linkname,
             loc_t *loc, mode_t umask, dict_t *xdata)
{
        IOT_FOP (symlink, frame, this, linkname, loc, umask, xdata);
        return 0;
}


int
iot_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
            dict_t *xdata)
{
        IOT_FOP (rename, frame, this, oldloc, newloc, xdata);
        return 0;
}


int
iot_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          fd_t *fd, dict_t *xdata)
{
        IOT_FOP (open, frame, this, loc, flags, fd, xdata);
        return 0;
}


int
iot_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
            mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        IOT_FOP (create, frame, this, loc, flags, mode, umask, fd, xdata);
        return 0;
}


int
iot_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
           off_t offset, uint32_t flags, dict_t *xdata)
{
        IOT_FOP (readv, frame, this, fd, size, offset, flags, xdata);
        return 0;
}


int
iot_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        IOT_FOP (flush, frame, this, fd, xdata);
        return 0;
}


int
iot_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
           dict_t *xdata)
{
        IOT_FOP (fsync, frame, this, fd, datasync, xdata);
        return 0;
}


int
iot_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
            struct iovec *vector, int32_t count, off_t offset,
            uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
        IOT_FOP (writev, frame, this, fd, vector, count, offset,
                 flags, iobref, xdata);
        return 0;
}


int
iot_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
	struct gf_flock *flock, dict_t *xdata)
{
        IOT_FOP (lk, frame, this, fd, cmd, flock, xdata);
        return 0;
}


int
iot_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        IOT_FOP (stat, frame, this, loc, xdata);
        return 0;
}


int
iot_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        IOT_FOP (fstat, frame, this, fd, xdata);
        return 0;
}


int
iot_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
              dict_t *xdata)
{
        IOT_FOP (truncate, frame, this, loc, offset, xdata);
        return 0;
}


int
iot_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
               dict_t *xdata)
{
        IOT_FOP (ftruncate, frame, this, fd, offset, xdata);
        return 0;
}



int
iot_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t xflag,
            dict_t *xdata)
{
        IOT_FOP (unlink, frame, this, loc, xflag, xdata);
        return 0;
}


int
iot_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
          dict_t *xdata)
{
        IOT_FOP (link, frame, this, oldloc, newloc, xdata);
        return 0;
}


int
iot_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
             dict_t *xdata)
{
        IOT_FOP (opendir, frame, this, loc, fd, xdata);
        return 0;
}


int
iot_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int datasync,
              dict_t *xdata)
{
        IOT_FOP (fsyncdir, frame, this, fd, datasync, xdata);
        return 0;
}


int
iot_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        IOT_FOP (statfs, frame, this, loc, xdata);
        return 0;
}


int
iot_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
              int32_t flags, dict_t *xdata)
{
        IOT_FOP (setxattr, frame, this, loc, dict, flags, xdata);
        return 0;
}


int
iot_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
              const char *name, dict_t *xdata)
{
        IOT_FOP (getxattr, frame, this, loc, name, xdata);
        return 0;
}


int
iot_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
               const char *name, dict_t *xdata)
{
        IOT_FOP (fgetxattr, frame, this, fd, name, xdata);
        return 0;
}


int
iot_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
               int32_t flags, dict_t *xdata)
{
        IOT_FOP (fsetxattr, frame, this, fd, dict, flags, xdata);
        return 0;
}


int
iot_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                 const char *name, dict_t *xdata)
{
        IOT_FOP (removexattr, frame, this, loc, name, xdata);
        return 0;
}

int
iot_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 const char *name, dict_t *xdata)
{
        IOT_FOP (fremovexattr, frame, this, fd, name, xdata);
        return 0;
}


int
iot_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t offset, dict_t *xdata)
{
        IOT_FOP (readdirp, frame, this, fd, size, offset, xdata);
        return 0;
}


int
iot_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset, dict_t *xdata)
{
        IOT_FOP (readdir, frame, this, fd, size, offset, xdata);
        return 0;
}

int
iot_inodelk (call_frame_t *frame, xlator_t *this,
             const char *volume, loc_t *loc, int32_t cmd, struct gf_flock *lock,
             dict_t *xdata)
{
        IOT_FOP (inodelk, frame, this, volume, loc, cmd, lock, xdata);
        return 0;
}

int
iot_finodelk (call_frame_t *frame, xlator_t *this,
              const char *volume, fd_t *fd, int32_t cmd, struct gf_flock *lock,
              dict_t *xdata)
{
        IOT_FOP (finodelk, frame, this, volume, fd, cmd, lock, xdata);
        return 0;
}

int
iot_entrylk (call_frame_t *frame, xlator_t *this,
             const char *volume, loc_t *loc, const char *basename,
             entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        IOT_FOP (entrylk, frame, this, volume, loc, basename, cmd, type, xdata);
        return 0;
}

int
iot_fentrylk (call_frame_t *frame, xlator_t *this,
              const char *volume, fd_t *fd, const char *basename,
              entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        IOT_FOP (fentrylk, frame, this, volume, fd, basename, cmd, type, xdata);
        return 0;
}


int
iot_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
             gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
        IOT_FOP (xattrop, frame, this, loc, optype, xattr, xdata);
        return 0;
}


int
iot_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
              gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
        IOT_FOP (fxattrop, frame, this, fd, optype, xattr, xdata);
        return 0;
}


int32_t
iot_rchecksum (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
               int32_t len, dict_t *xdata)
{
        IOT_FOP (rchecksum, frame, this, fd, offset, len, xdata);
        return 0;
}

int
iot_fallocate (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
	      off_t offset, size_t len, dict_t *xdata)
{
        IOT_FOP (fallocate, frame, this, fd, mode, offset, len, xdata);
        return 0;
}

int
iot_discard (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
	    size_t len, dict_t *xdata)
{
        IOT_FOP (discard, frame, this, fd, offset, len, xdata);
        return 0;
}

int
iot_zerofill (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            off_t len, dict_t *xdata)
{
        IOT_FOP (zerofill, frame, this, fd, offset, len, xdata);
        return 0;
}

int
iot_seek (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
          gf_seek_what_t what, dict_t *xdata)
{
        IOT_FOP (seek, frame, this, fd, offset, what, xdata);
        return 0;
}

int
iot_lease (call_frame_t *frame, xlator_t *this, loc_t *loc,
           struct gf_lease *lease, dict_t *xdata)
{
        IOT_FOP (lease, frame, this, loc, lease, xdata);
        return 0;
}

int
iot_getactivelk (call_frame_t *frame, xlator_t *this,
                 loc_t *loc, dict_t *xdata)
{
        IOT_FOP (getactivelk, frame, this, loc, xdata);
        return 0;
}

int
iot_setactivelk (call_frame_t *frame, xlator_t *this, loc_t *loc,
                 lock_migration_info_t *locklist, dict_t *xdata)
{
        IOT_FOP (setactivelk, frame, this, loc, locklist, xdata);
        return 0;
}


int
__iot_workers_scale (iot_conf_t *conf)
{
        int       scale = 0;
        int       diff = 0;
        pthread_t thread;
        int       ret = 0;
        int       i = 0;

        for (i = 0; i < IOT_PRI_MAX; i++)
                scale += min (conf->queue_sizes[i], conf->ac_iot_limit[i]);

        if (scale < IOT_MIN_THREADS)
                scale = IOT_MIN_THREADS;

        if (scale > conf->max_count)
                scale = conf->max_count;

        if (conf->curr_count < scale) {
                diff = scale - conf->curr_count;
        }

        while (diff) {
                diff --;

                ret = gf_thread_create (&thread, &conf->w_attr, iot_worker, conf);
                if (ret == 0) {
                        conf->curr_count++;
                        gf_msg_debug (conf->this->name, 0,
                                      "scaled threads to %d (queue_size=%d/%d)",
                                      conf->curr_count,
                                      conf->queue_size, scale);
                } else {
                        break;
                }
        }

        return diff;
}


int
iot_workers_scale (iot_conf_t *conf)
{
        int     ret = -1;

        if (conf == NULL) {
                ret = -EINVAL;
                goto out;
        }

        pthread_mutex_lock (&conf->mutex);
        {
                ret = __iot_workers_scale (conf);
        }
        pthread_mutex_unlock (&conf->mutex);

out:
        return ret;
}


void
set_stack_size (iot_conf_t *conf)
{
        int     err = 0;
        size_t  stacksize = IOT_THREAD_STACK_SIZE;
        xlator_t *this = NULL;

        this = THIS;

        pthread_attr_init (&conf->w_attr);
        err = pthread_attr_setstacksize (&conf->w_attr, stacksize);
        if (err == EINVAL) {
                err = pthread_attr_getstacksize (&conf->w_attr, &stacksize);
                if (!err)
                        gf_msg (this->name, GF_LOG_WARNING,
                                0, IO_THREADS_MSG_SIZE_NOT_SET,
                                "Using default thread stack size %zd",
                                stacksize);
                else
                        gf_msg (this->name, GF_LOG_WARNING,
                                0, IO_THREADS_MSG_SIZE_NOT_SET,
                                "Using default thread stack size");
        }

        conf->stack_size = stacksize;
}


int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_iot_mt_end + 1);

        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR,
                        ENOMEM, IO_THREADS_MSG_NO_MEMORY,
                        "Memory accounting init failed");
                return ret;
        }

        return ret;
}

int
iot_priv_dump (xlator_t *this)
{
        iot_conf_t     *conf   =   NULL;
        char           key_prefix[GF_DUMP_MAX_BUF_LEN];

        if (!this)
                return 0;

        conf = this->private;
        if (!conf)
                return 0;

        snprintf (key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s", this->type,
                  this->name);

        gf_proc_dump_add_section(key_prefix);

        gf_proc_dump_write("maximum_threads_count", "%d", conf->max_count);
        gf_proc_dump_write("current_threads_count", "%d", conf->curr_count);
        gf_proc_dump_write("sleep_count", "%d", conf->sleep_count);
        gf_proc_dump_write("idle_time", "%d", conf->idle_time);
        gf_proc_dump_write("stack_size", "%zd", conf->stack_size);
        gf_proc_dump_write("high_priority_threads", "%d",
                           conf->ac_iot_limit[IOT_PRI_HI]);
        gf_proc_dump_write("normal_priority_threads", "%d",
                           conf->ac_iot_limit[IOT_PRI_NORMAL]);
        gf_proc_dump_write("low_priority_threads", "%d",
                           conf->ac_iot_limit[IOT_PRI_LO]);
        gf_proc_dump_write("least_priority_threads", "%d",
                           conf->ac_iot_limit[IOT_PRI_LEAST]);

        return 0;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
	iot_conf_t      *conf = NULL;
	int		 ret = -1;

        conf = this->private;
        if (!conf)
                goto out;

        GF_OPTION_RECONF ("thread-count", conf->max_count, options, int32, out);

        GF_OPTION_RECONF ("high-prio-threads",
                          conf->ac_iot_limit[IOT_PRI_HI], options, int32, out);

        GF_OPTION_RECONF ("normal-prio-threads",
                          conf->ac_iot_limit[IOT_PRI_NORMAL], options, int32,
                          out);

        GF_OPTION_RECONF ("low-prio-threads",
                          conf->ac_iot_limit[IOT_PRI_LO], options, int32, out);

        GF_OPTION_RECONF ("least-prio-threads",
                          conf->ac_iot_limit[IOT_PRI_LEAST], options, int32,
                          out);
        GF_OPTION_RECONF ("enable-least-priority", conf->least_priority,
                          options, bool, out);

	ret = 0;
out:
	return ret;
}


int
init (xlator_t *this)
{
        iot_conf_t *conf = NULL;
        int         ret  = -1;
        int         i    = 0;

	if (!this->children || this->children->next) {
		gf_msg ("io-threads", GF_LOG_ERROR, 0,
                        IO_THREADS_MSG_XLATOR_CHILD_MISCONFIGURED,
			"FATAL: iot not configured "
                        "with exactly one child");
                goto out;
	}

	if (!this->parents) {
		gf_msg (this->name, GF_LOG_WARNING, 0,
			IO_THREADS_MSG_VOL_MISCONFIGURED,
                        "dangling volume. check volfile ");
	}

	conf = (void *) GF_CALLOC (1, sizeof (*conf),
                                   gf_iot_mt_iot_conf_t);
        if (conf == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        IO_THREADS_MSG_NO_MEMORY, "out of memory");
                goto out;
        }

        if ((ret = pthread_cond_init(&conf->cond, NULL)) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        IO_THREADS_MSG_INIT_FAILED,
                        "pthread_cond_init failed (%d)", ret);
                goto out;
        }
        conf->cond_inited = _gf_true;

        if ((ret = pthread_mutex_init(&conf->mutex, NULL)) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        IO_THREADS_MSG_INIT_FAILED,
                        "pthread_mutex_init failed (%d)", ret);
                goto out;
        }
        conf->mutex_inited = _gf_true;

        set_stack_size (conf);

        GF_OPTION_INIT ("thread-count", conf->max_count, int32, out);

        GF_OPTION_INIT ("high-prio-threads",
                        conf->ac_iot_limit[IOT_PRI_HI], int32, out);

        GF_OPTION_INIT ("normal-prio-threads",
                        conf->ac_iot_limit[IOT_PRI_NORMAL], int32, out);

        GF_OPTION_INIT ("low-prio-threads",
                        conf->ac_iot_limit[IOT_PRI_LO], int32, out);

        GF_OPTION_INIT ("least-prio-threads",
                        conf->ac_iot_limit[IOT_PRI_LEAST], int32, out);

        GF_OPTION_INIT ("idle-time", conf->idle_time, int32, out);
        GF_OPTION_INIT ("enable-least-priority", conf->least_priority,
                        bool, out);

        conf->this = this;

        for (i = 0; i < IOT_PRI_MAX; i++) {
                INIT_LIST_HEAD (&conf->clients[i]);
                INIT_LIST_HEAD (&conf->no_client[i].clients);
                INIT_LIST_HEAD (&conf->no_client[i].reqs);
        }

	ret = iot_workers_scale (conf);

        if (ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        IO_THREADS_MSG_INIT_FAILED,
                        "cannot initialize worker threads, exiting init");
                goto out;
        }

	this->private = conf;
        ret = 0;
out:
        if (ret)
                GF_FREE (conf);

	return ret;
}

static void
iot_exit_threads (iot_conf_t *conf)
{
        pthread_mutex_lock (&conf->mutex);
        {
                conf->down = _gf_true;
                /*Let all the threads know that xl is going down*/
                pthread_cond_broadcast (&conf->cond);
                while (conf->curr_count)/*Wait for threads to exit*/
                        pthread_cond_wait (&conf->cond, &conf->mutex);
        }
        pthread_mutex_unlock (&conf->mutex);
}

int
notify (xlator_t *this, int32_t event, void *data, ...)
{
        iot_conf_t *conf = this->private;

        if (GF_EVENT_PARENT_DOWN == event)
                iot_exit_threads (conf);

        default_notify (this, event, data);

        return 0;
}

void
fini (xlator_t *this)
{
	iot_conf_t *conf = this->private;

        if (!conf)
                return;

        if (conf->mutex_inited && conf->cond_inited)
                iot_exit_threads (conf);

        if (conf->cond_inited)
                pthread_cond_destroy (&conf->cond);

        if (conf->mutex_inited)
                pthread_mutex_destroy (&conf->mutex);

	GF_FREE (conf);

	this->private = NULL;
	return;
}

int
iot_client_destroy (xlator_t *this, client_t *client)
{
        void    *tmp    = NULL;

        if (client_ctx_del (client, this, &tmp) == 0) {
                GF_FREE (tmp);
        }

        return 0;
}


struct xlator_dumpops dumpops = {
        .priv    = iot_priv_dump,
};

struct xlator_fops fops = {
	.open        = iot_open,
	.create      = iot_create,
	.readv       = iot_readv,
	.writev      = iot_writev,
	.flush       = iot_flush,
	.fsync       = iot_fsync,
	.lk          = iot_lk,
	.stat        = iot_stat,
	.fstat       = iot_fstat,
	.truncate    = iot_truncate,
	.ftruncate   = iot_ftruncate,
	.unlink      = iot_unlink,
        .lookup      = iot_lookup,
        .setattr     = iot_setattr,
        .fsetattr    = iot_fsetattr,
        .access      = iot_access,
        .readlink    = iot_readlink,
        .mknod       = iot_mknod,
        .mkdir       = iot_mkdir,
        .rmdir       = iot_rmdir,
        .symlink     = iot_symlink,
        .rename      = iot_rename,
        .link        = iot_link,
        .opendir     = iot_opendir,
        .fsyncdir    = iot_fsyncdir,
        .statfs      = iot_statfs,
        .setxattr    = iot_setxattr,
        .getxattr    = iot_getxattr,
        .fgetxattr   = iot_fgetxattr,
        .fsetxattr   = iot_fsetxattr,
        .removexattr = iot_removexattr,
        .fremovexattr = iot_fremovexattr,
        .readdir     = iot_readdir,
        .readdirp    = iot_readdirp,
        .inodelk     = iot_inodelk,
        .finodelk    = iot_finodelk,
        .entrylk     = iot_entrylk,
        .fentrylk    = iot_fentrylk,
        .xattrop     = iot_xattrop,
        .fxattrop    = iot_fxattrop,
        .rchecksum   = iot_rchecksum,
        .fallocate   = iot_fallocate,
        .discard     = iot_discard,
        .zerofill    = iot_zerofill,
        .seek        = iot_seek,
        .lease       = iot_lease,
        .getactivelk = iot_getactivelk,
        .setactivelk = iot_setactivelk,
};

struct xlator_cbks cbks = {
        .client_destroy = iot_client_destroy,
};

struct volume_options options[] = {
	{ .key  = {"thread-count"},
	  .type = GF_OPTION_TYPE_INT,
	  .min  = IOT_MIN_THREADS,
	  .max  = IOT_MAX_THREADS,
          .default_value = "16",
          .description = "Number of threads in IO threads translator which "
                         "perform concurrent IO operations"

	},
	{ .key  = {"high-prio-threads"},
	  .type = GF_OPTION_TYPE_INT,
	  .min  = IOT_MIN_THREADS,
	  .max  = IOT_MAX_THREADS,
          .default_value = "16",
          .description = "Max number of threads in IO threads translator which "
                         "perform high priority IO operations at a given time"

	},
	{ .key  = {"normal-prio-threads"},
	  .type = GF_OPTION_TYPE_INT,
	  .min  = IOT_MIN_THREADS,
	  .max  = IOT_MAX_THREADS,
          .default_value = "16",
          .description = "Max number of threads in IO threads translator which "
                         "perform normal priority IO operations at a given time"

	},
	{ .key  = {"low-prio-threads"},
	  .type = GF_OPTION_TYPE_INT,
	  .min  = IOT_MIN_THREADS,
	  .max  = IOT_MAX_THREADS,
          .default_value = "16",
          .description = "Max number of threads in IO threads translator which "
                         "perform low priority IO operations at a given time"

	},
	{ .key  = {"least-prio-threads"},
	  .type = GF_OPTION_TYPE_INT,
	  .min  = IOT_MIN_THREADS,
	  .max  = IOT_MAX_THREADS,
          .default_value = "1",
          .description = "Max number of threads in IO threads translator which "
                         "perform least priority IO operations at a given time"
	},
        { .key  = {"enable-least-priority"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "on",
          .description = "Enable/Disable least priority"
        },
        {.key   = {"idle-time"},
         .type  = GF_OPTION_TYPE_INT,
         .min   = 1,
         .max   = 0x7fffffff,
         .default_value = "120",
        },
	{ .key  = {NULL},
        },
};
