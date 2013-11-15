/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "call-stub.h"
#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "io-threads.h"
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "locking.h"

void *iot_worker (void *arg);
int iot_workers_scale (iot_conf_t *conf);
int __iot_workers_scale (iot_conf_t *conf);
struct volume_options options[];

call_stub_t *
__iot_dequeue (iot_conf_t *conf, int *pri, struct timespec *sleep)
{
        call_stub_t  *stub = NULL;
        int           i = 0;
	struct timeval curtv = {0,}, difftv = {0,};

        *pri = -1;
	sleep->tv_sec = 0;
	sleep->tv_nsec = 0;
        for (i = 0; i < IOT_PRI_MAX; i++) {
                if (list_empty (&conf->reqs[i]) ||
                   (conf->ac_iot_count[i] >= conf->ac_iot_limit[i]))
                        continue;

		if (i == IOT_PRI_LEAST) {
			pthread_mutex_lock(&conf->throttle.lock);
			if (!conf->throttle.sample_time.tv_sec) {
				/* initialize */
				gettimeofday(&conf->throttle.sample_time, NULL);
			} else {
				/*
				 * Maintain a running count of least priority
				 * operations that are handled over a particular
				 * time interval. The count is provided via
				 * state dump and is used as a measure against
				 * least priority op throttling.
				 */
				gettimeofday(&curtv, NULL);
				timersub(&curtv, &conf->throttle.sample_time,
					 &difftv);
				if (difftv.tv_sec >= IOT_LEAST_THROTTLE_DELAY) {
					conf->throttle.cached_rate =
						conf->throttle.sample_cnt;
					conf->throttle.sample_cnt = 0;
					conf->throttle.sample_time = curtv;
				}

				/*
				 * If we're over the configured rate limit,
				 * provide an absolute time to the caller that
				 * represents the soonest we're allowed to
				 * return another least priority request.
				 */
				if (conf->throttle.rate_limit &&
				    conf->throttle.sample_cnt >=
						conf->throttle.rate_limit) {
					struct timeval delay;
					delay.tv_sec = IOT_LEAST_THROTTLE_DELAY;
					delay.tv_usec = 0;

					timeradd(&conf->throttle.sample_time,
						 &delay, &curtv);
					TIMEVAL_TO_TIMESPEC(&curtv, sleep);

					pthread_mutex_unlock(
						&conf->throttle.lock);
					break;
				}
			}
			conf->throttle.sample_cnt++;
			pthread_mutex_unlock(&conf->throttle.lock);
		}

                stub = list_entry (conf->reqs[i].next, call_stub_t, list);
                conf->ac_iot_count[i]++;
                *pri = i;
                break;
        }

        if (!stub)
                return NULL;

        conf->queue_size--;
        conf->queue_sizes[*pri]--;
        list_del_init (&stub->list);

        return stub;
}


void
__iot_enqueue (iot_conf_t *conf, call_stub_t *stub, int pri)
{
        if (pri < 0 || pri >= IOT_PRI_MAX)
                pri = IOT_PRI_MAX-1;

        list_add_tail (&stub->list, &conf->reqs[pri]);

        conf->queue_size++;
        conf->queue_sizes[pri]++;

        return;
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
        char              timeout = 0;
        char              bye = 0;
	struct timespec	  sleep = {0,};

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
                                conf->sleep_count++;

                                ret = pthread_cond_timedwait (&conf->cond,
                                                              &conf->mutex,
                                                              &sleep_till);
                                conf->sleep_count--;

                                if (ret == ETIMEDOUT) {
                                        timeout = 1;
                                        break;
                                }
                        }

                        if (timeout) {
                                if (conf->curr_count > IOT_MIN_THREADS) {
                                        conf->curr_count--;
                                        bye = 1;
                                        gf_log (conf->this->name, GF_LOG_DEBUG,
                                                "timeout, terminated. conf->curr_count=%d",
                                                conf->curr_count);
                                } else {
                                        timeout = 0;
                                }
                        }

                        stub = __iot_dequeue (conf, &pri, &sleep);
			if (!stub && (sleep.tv_sec || sleep.tv_nsec)) {
				pthread_cond_timedwait(&conf->cond,
						       &conf->mutex, &sleep);
				pthread_mutex_unlock(&conf->mutex);
				continue;
			}
                }
                pthread_mutex_unlock (&conf->mutex);

                if (stub) /* guard against spurious wakeups */
                        call_resume (stub);

                if (bye)
                        break;
        }

        if (pri != -1) {
                pthread_mutex_lock (&conf->mutex);
                {
                        conf->ac_iot_count[pri]--;
                }
                pthread_mutex_unlock (&conf->mutex);
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
                pri = IOT_PRI_HI;
                break;

        case GF_FOP_CREATE:
        case GF_FOP_FLUSH:
        case GF_FOP_LK:
        case GF_FOP_INODELK:
        case GF_FOP_FINODELK:
        case GF_FOP_ENTRYLK:
        case GF_FOP_FENTRYLK:
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
                pri = IOT_PRI_LO;
                break;

        case GF_FOP_NULL:
        case GF_FOP_FORGET:
        case GF_FOP_RELEASE:
        case GF_FOP_RELEASEDIR:
        case GF_FOP_GETSPEC:
        case GF_FOP_MAXVALUE:
                //fail compilation on missing fop
                //new fop must choose priority.
                break;
        }
out:
        gf_log (this->name, GF_LOG_DEBUG, "%s scheduled as %s fop",
                gf_fop_list[stub->fop], iot_get_pri_meaning (pri));
        ret = do_iot_schedule (this->private, stub, pri);
        return ret;
}

int
iot_lookup_cbk (call_frame_t *frame, void * cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                inode_t *inode, struct iatt *buf, dict_t *xdata,
                struct iatt *postparent)
{
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf, xdata,
                             postparent);
        return 0;
}


int
iot_lookup_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    dict_t *xdata)
{
        STACK_WIND (frame, iot_lookup_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->lookup,
                    loc, xdata);
        return 0;
}


int
iot_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int              ret = -1;

        stub = fop_lookup_stub (frame, iot_lookup_wrapper, loc, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cannot create lookup stub (out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
                STACK_UNWIND_STRICT (lookup, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL);
        }

        return 0;
}


int
iot_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno, preop, postop,
                             xdata);
        return 0;
}


int
iot_setattr_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                     struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        STACK_WIND (frame, iot_setattr_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setattr,
                    loc, stbuf, valid, xdata);
        return 0;
}


int
iot_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
             struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int              ret = -1;

        stub = fop_setattr_stub (frame, iot_setattr_wrapper, loc, stbuf, valid,
                                 xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "Cannot create setattr stub"
                        "(Out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }

                STACK_UNWIND_STRICT (setattr, frame, -1, -ret, NULL, NULL, NULL);
        }

        return 0;
}


int
iot_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
        STACK_UNWIND_STRICT (fsetattr, frame, op_ret, op_errno, preop, postop,
                             xdata);
        return 0;
}


int
iot_fsetattr_wrapper (call_frame_t *frame, xlator_t *this,
                      fd_t *fd, struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        STACK_WIND (frame, iot_fsetattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsetattr, fd, stbuf, valid,
                    xdata);
        return 0;
}


int
iot_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
              struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int              ret = -1;

        stub = fop_fsetattr_stub (frame, iot_fsetattr_wrapper, fd, stbuf,
                                  valid, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create fsetattr stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (fsetattr, frame, -1, -ret, NULL, NULL,
                                     NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        STACK_UNWIND_STRICT (access, frame, op_ret, op_errno, xdata);
        return 0;
}


int
iot_access_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    int32_t mask, dict_t *xdata)
{
        STACK_WIND (frame, iot_access_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->access, loc, mask, xdata);
        return 0;
}


int
iot_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
            dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_access_stub (frame, iot_access_wrapper, loc, mask, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create access stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (access, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, const char *path,
                  struct iatt *stbuf, dict_t *xdata)
{
        STACK_UNWIND_STRICT (readlink, frame, op_ret, op_errno, path, stbuf,
                             xdata);
        return 0;
}


int
iot_readlink_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                      size_t size, dict_t *xdata)
{
        STACK_WIND (frame, iot_readlink_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readlink,
                    loc, size, xdata);
        return 0;
}


int
iot_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_readlink_stub (frame, iot_readlink_wrapper, loc, size, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create readlink stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (readlink, frame, -1, -ret, NULL, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

        return 0;
}


int
iot_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *buf, struct iatt *preparent,
               struct iatt *postparent, dict_t *xdata)
{
        STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
iot_mknod_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
                   dev_t rdev, mode_t umask, dict_t *xdata)
{
        STACK_WIND (frame, iot_mknod_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->mknod, loc, mode, rdev, umask,
                    xdata);
        return 0;
}


int
iot_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
           dev_t rdev, mode_t umask, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_mknod_stub (frame, iot_mknod_wrapper, loc, mode, rdev,
                               umask, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create mknod stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (mknod, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_mkdir_cbk (call_frame_t *frame, void * cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct iatt *buf, struct iatt *preparent,
               struct iatt *postparent, dict_t *xdata)
{
        STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
iot_mkdir_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
                   mode_t umask, dict_t *xdata)
{
        STACK_WIND (frame, iot_mkdir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->mkdir, loc, mode, umask, xdata);
        return 0;
}


int
iot_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
           mode_t umask, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_mkdir_stub (frame, iot_mkdir_wrapper, loc, mode, umask,
                               xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create mkdir stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (mkdir, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *preparent,
               struct iatt *postparent, dict_t *xdata)
{
        STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno, preparent,
                             postparent, xdata);
        return 0;
}


int
iot_rmdir_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags, dict_t *xdata)
{
        STACK_WIND (frame, iot_rmdir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->rmdir, loc, flags, xdata);
        return 0;
}


int
iot_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_rmdir_stub (frame, iot_rmdir_wrapper, loc, flags, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create rmdir stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (rmdir, frame, -1, -ret, NULL, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_symlink_cbk (call_frame_t *frame, void * cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *buf, struct iatt *preparent,
                 struct iatt *postparent, dict_t *xdata)
{
        STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
iot_symlink_wrapper (call_frame_t *frame, xlator_t *this, const char *linkname,
                     loc_t *loc, mode_t umask, dict_t *xdata)
{
        STACK_WIND (frame, iot_symlink_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->symlink, linkname, loc, umask,
                    xdata);
        return 0;
}


int
iot_symlink (call_frame_t *frame, xlator_t *this, const char *linkname,
             loc_t *loc, mode_t umask, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_symlink_stub (frame, iot_symlink_wrapper, linkname, loc,
                                 umask, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create symlink stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (symlink, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL, NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

        return 0;
}


int
iot_rename_cbk (call_frame_t *frame, void * cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *buf,
                struct iatt *preoldparent, struct iatt *postoldparent,
                struct iatt *prenewparent, struct iatt *postnewparent,
                dict_t *xdata)
{
        STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, buf, preoldparent,
                             postoldparent, prenewparent, postnewparent, xdata);
        return 0;
}


int
iot_rename_wrapper (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                    loc_t *newloc, dict_t *xdata)
{
        STACK_WIND (frame, iot_rename_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->rename, oldloc, newloc, xdata);
        return 0;
}


int
iot_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
            dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_rename_stub (frame, iot_rename_wrapper, oldloc, newloc, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_DEBUG, "cannot create rename stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (rename, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL, NULL, NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

        return 0;
}


int
iot_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, fd_t *fd, dict_t *xdata)
{
	STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd, xdata);
	return 0;
}


int
iot_open_wrapper (call_frame_t * frame, xlator_t * this, loc_t *loc,
                  int32_t flags, fd_t * fd, dict_t *xdata)
{
	STACK_WIND (frame, iot_open_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->open, loc, flags, fd,
                    xdata);
	return 0;
}


int
iot_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          fd_t *fd, dict_t *xdata)
{
        call_stub_t	*stub = NULL;
        int             ret = -1;

        stub = fop_open_stub (frame, iot_open_wrapper, loc, flags, fd,
                              xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cannot create open call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

	ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (open, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

	return 0;
}


int
iot_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                struct iatt *stbuf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
	STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, stbuf,
                             preparent, postparent, xdata);
	return 0;
}


int
iot_create_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    int32_t flags, mode_t mode, mode_t umask, fd_t *fd,
                    dict_t *xdata)
{
	STACK_WIND (frame, iot_create_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->create,
		    loc, flags, mode, umask, fd, xdata);
	return 0;
}


int
iot_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
            mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_create_stub (frame, iot_create_wrapper, loc, flags, mode,
                                umask, fd, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cannot create \"create\" call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (create, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

        return 0;
}


int
iot_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iovec *vector,
               int32_t count, struct iatt *stbuf, struct iobref *iobref,
               dict_t *xdata)
{
	STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector, count,
                             stbuf, iobref, xdata);

	return 0;
}


int
iot_readv_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                   off_t offset, uint32_t flags, dict_t *xdata)
{
	STACK_WIND (frame, iot_readv_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readv,
		    fd, size, offset, flags, xdata);
	return 0;
}


int
iot_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
           off_t offset, uint32_t flags, dict_t *xdata)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_readv_stub (frame, iot_readv_wrapper, fd, size, offset,
                               flags, xdata);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
			"cannot create readv call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (readv, frame, -1, -ret, NULL, -1, NULL,
                                     NULL, NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
	STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno, xdata);
	return 0;
}


int
iot_flush_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
	STACK_WIND (frame, iot_flush_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->flush,
		    fd, xdata);
	return 0;
}


int
iot_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_flush_stub (frame, iot_flush_wrapper, fd, xdata);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create flush_cbk call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (flush, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf, dict_t *xdata)
{
	STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, prebuf, postbuf,
                             xdata);
	return 0;
}


int
iot_fsync_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                   int32_t datasync, dict_t *xdata)
{
	STACK_WIND (frame, iot_fsync_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->fsync,
		    fd, datasync, xdata);
	return 0;
}


int
iot_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
           dict_t *xdata)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_fsync_stub (frame, iot_fsync_wrapper, fd, datasync, xdata);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fsync_cbk call stub"
                        "(out of memory)");
                ret = -1;
                goto out;
	}

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (fsync, frame, -1, -ret, NULL, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                struct iatt *postbuf, dict_t *xdata)
{
	STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf,
                             xdata);
	return 0;
}


int
iot_writev_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                    struct iovec *vector, int32_t count,
                    off_t offset, uint32_t flags, struct iobref *iobref,
                    dict_t *xdata)
{
	STACK_WIND (frame, iot_writev_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->writev,
		    fd, vector, count, offset, flags, iobref, xdata);
	return 0;
}


int
iot_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
            struct iovec *vector, int32_t count, off_t offset,
            uint32_t flags, struct iobref *iobref, dict_t *xdata)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_writev_stub (frame, iot_writev_wrapper, fd, vector,
                                count, offset, flags, iobref, xdata);

	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create writev call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (writev, frame, -1, -ret, NULL, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

	return 0;
}


int32_t
iot_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
            int32_t op_ret, int32_t op_errno, struct gf_flock *flock,
            dict_t *xdata)
{
	STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno, flock, xdata);
	return 0;
}


int
iot_lk_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
	STACK_WIND (frame, iot_lk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->lk,
		    fd, cmd, flock, xdata);
	return 0;
}


int
iot_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
	struct gf_flock *flock, dict_t *xdata)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_lk_stub (frame, iot_lk_wrapper, fd, cmd, flock, xdata);

	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_lk call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (lk, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
	STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, buf, xdata);
	return 0;
}


int
iot_stat_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
	STACK_WIND (frame, iot_stat_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->stat,
		    loc, xdata);
	return 0;
}


int
iot_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

        stub = fop_stat_stub (frame, iot_stat_wrapper, loc, xdata);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_stat call stub"
                        "(out of memory)");
                ret = -1;
                goto out;
	}

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (stat, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
	STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, buf, xdata);
	return 0;
}


int
iot_fstat_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
	STACK_WIND (frame, iot_fstat_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fstat,
		    fd, xdata);
	return 0;
}


int
iot_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_fstat_stub (frame, iot_fstat_wrapper, fd, xdata);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_fstat call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (fstat, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{
	STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);
	return 0;
}


int
iot_truncate_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                      off_t offset, dict_t *xdata)
{
	STACK_WIND (frame, iot_truncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->truncate,
		    loc, offset, xdata);
	return 0;
}


int
iot_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
              dict_t *xdata)
{
	call_stub_t *stub;
        int         ret = -1;

        stub = fop_truncate_stub (frame, iot_truncate_wrapper, loc, offset,
                                  xdata);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_stat call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (truncate, frame, -1, -ret, NULL, NULL,
                                     NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

	return 0;
}


int
iot_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
	STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);
	return 0;
}


int
iot_ftruncate_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                       off_t offset, dict_t *xdata)
{
	STACK_WIND (frame, iot_ftruncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->ftruncate,
		    fd, offset, xdata);
	return 0;
}


int
iot_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
               dict_t *xdata)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_ftruncate_stub (frame, iot_ftruncate_wrapper, fd, offset,
                                   xdata);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_ftruncate call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (ftruncate, frame, -1, -ret, NULL, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}



int
iot_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
	STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno, preparent,
                             postparent, xdata);
	return 0;
}


int
iot_unlink_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    int32_t xflag, dict_t *xdata)
{
	STACK_WIND (frame, iot_unlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->unlink,
		    loc, xflag, xdata);
	return 0;
}


int
iot_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t xflag,
            dict_t *xdata)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_unlink_stub (frame, iot_unlink_wrapper, loc, xflag, xdata);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_unlink call stub"
                        "(out of memory)");
                ret = -1;
                goto out;
	}

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (unlink, frame, -1, -ret, NULL, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

	return 0;
}


int
iot_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, inode_t *inode,
              struct iatt *buf, struct iatt *preparent, struct iatt *postparent,
              dict_t *xdata)
{
        STACK_UNWIND_STRICT (link, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);
        return 0;
}


int
iot_link_wrapper (call_frame_t *frame, xlator_t *this, loc_t *old, loc_t *new,
                  dict_t *xdata)
{
        STACK_WIND (frame, iot_link_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->link, old, new, xdata);

        return 0;
}


int
iot_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
          dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_link_stub (frame, iot_link_wrapper, oldloc, newloc, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create link stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (link, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        STACK_UNWIND_STRICT (opendir, frame, op_ret, op_errno, fd, xdata);
        return 0;
}


int
iot_opendir_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
                     dict_t *xdata)
{
        STACK_WIND (frame, iot_opendir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->opendir, loc, fd, xdata);
        return 0;
}


int
iot_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
             dict_t *xdata)
{
        call_stub_t     *stub  = NULL;
        int             ret = -1;

        stub = fop_opendir_stub (frame, iot_opendir_wrapper, loc, fd, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create opendir stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (opendir, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        STACK_UNWIND_STRICT (fsyncdir, frame, op_ret, op_errno, xdata);
        return 0;
}


int
iot_fsyncdir_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                      int datasync, dict_t *xdata)
{
        STACK_WIND (frame, iot_fsyncdir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsyncdir, fd, datasync, xdata);
        return 0;
}


int
iot_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int datasync,
              dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_fsyncdir_stub (frame, iot_fsyncdir_wrapper, fd, datasync,
                                  xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create fsyncdir stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (fsyncdir, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct statvfs *buf,
                dict_t *xdata)
{
        STACK_UNWIND_STRICT (statfs, frame, op_ret, op_errno, buf, xdata);
        return 0;
}


int
iot_statfs_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    dict_t *xdata)
{
        STACK_WIND (frame, iot_statfs_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->statfs, loc, xdata);
        return 0;
}


int
iot_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int              ret = -1;

        stub = fop_statfs_stub (frame, iot_statfs_wrapper, loc, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create statfs stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (statfs, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno, xdata);
        return 0;
}


int
iot_setxattr_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                      dict_t *dict, int32_t flags, dict_t *xdata)
{
        STACK_WIND (frame, iot_setxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setxattr, loc, dict, flags, xdata);
        return 0;
}


int
iot_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
              int32_t flags, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int              ret = -1;

        stub = fop_setxattr_stub (frame, iot_setxattr_wrapper, loc, dict,
                                  flags, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create setxattr stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (setxattr, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


int
iot_getxattr_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                      const char *name, dict_t *xdata)
{
        STACK_WIND (frame, iot_getxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->getxattr, loc, name, xdata);
        return 0;
}


int
iot_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
              const char *name, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int              ret = -1;

        stub = fop_getxattr_stub (frame, iot_getxattr_wrapper, loc, name, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create getxattr stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (getxattr, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *dict,
                   dict_t *xdata)
{
        STACK_UNWIND_STRICT (fgetxattr, frame, op_ret, op_errno, dict, xdata);
        return 0;
}


int
iot_fgetxattr_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                       const char *name, dict_t *xdata)
{
        STACK_WIND (frame, iot_fgetxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fgetxattr, fd, name, xdata);
        return 0;
}


int
iot_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
               const char *name, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_fgetxattr_stub (frame, iot_fgetxattr_wrapper, fd, name, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create fgetxattr stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (fgetxattr, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno, xdata);
        return 0;
}


int
iot_fsetxattr_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                       dict_t *dict, int32_t flags, dict_t *xdata)
{
        STACK_WIND (frame, iot_fsetxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsetxattr, fd, dict, flags,
                    xdata);
        return 0;
}


int
iot_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
               int32_t flags, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_fsetxattr_stub (frame, iot_fsetxattr_wrapper, fd, dict,
                                   flags, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create fsetxattr stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (fsetxattr, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno, xdata);
        return 0;
}


int
iot_removexattr_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                         const char *name, dict_t *xdata)
{
        STACK_WIND (frame, iot_removexattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->removexattr, loc, name, xdata);
        return 0;
}


int
iot_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                 const char *name, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_removexattr_stub (frame, iot_removexattr_wrapper, loc,
                                     name, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR,"cannot get removexattr fop"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (removexattr, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}

int
iot_fremovexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        STACK_UNWIND_STRICT (fremovexattr, frame, op_ret, op_errno, xdata);
        return 0;
}


int
iot_fremovexattr_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                          const char *name, dict_t *xdata)
{
        STACK_WIND (frame, iot_fremovexattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fremovexattr, fd, name, xdata);
        return 0;
}


int
iot_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 const char *name, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_fremovexattr_stub (frame, iot_fremovexattr_wrapper, fd,
                                      name, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR,"cannot get fremovexattr fop"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (fremovexattr, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                  dict_t *xdata)
{
        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, entries, xdata);
        return 0;
}


int
iot_readdirp_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                      size_t size, off_t offset, dict_t *xdata)
{
        STACK_WIND (frame, iot_readdirp_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readdirp, fd, size, offset, xdata);
        return 0;
}


int
iot_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t offset, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_readdirp_stub (frame, iot_readdirp_wrapper, fd, size,
                                  offset, xdata);
        if (!stub) {
                gf_log (this->private, GF_LOG_ERROR,"cannot get readdir stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (readdirp, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                 dict_t *xdata)
{
        STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, entries, xdata);
        return 0;
}


int
iot_readdir_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     size_t size, off_t offset, dict_t *xdata)
{
        STACK_WIND (frame, iot_readdir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readdir, fd, size, offset, xdata);
        return 0;
}


int
iot_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_readdir_stub (frame, iot_readdir_wrapper, fd, size, offset,
                                 xdata);
        if (!stub) {
                gf_log (this->private, GF_LOG_ERROR,"cannot get readdir stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (readdir, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}

int
iot_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        STACK_UNWIND_STRICT (inodelk, frame, op_ret, op_errno, xdata);
        return 0;
}


int
iot_inodelk_wrapper (call_frame_t *frame, xlator_t *this, const char *volume,
                     loc_t *loc, int32_t cmd, struct gf_flock *lock,
                     dict_t *xdata)
{
        STACK_WIND (frame, iot_inodelk_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->inodelk, volume, loc, cmd, lock,
                    xdata);
        return 0;
}


int
iot_inodelk (call_frame_t *frame, xlator_t *this,
             const char *volume, loc_t *loc, int32_t cmd, struct gf_flock *lock,
             dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_inodelk_stub (frame, iot_inodelk_wrapper,
                                 volume, loc, cmd, lock, xdata);
        if (!stub) {
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (inodelk, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}

int
iot_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        STACK_UNWIND_STRICT (finodelk, frame, op_ret, op_errno, xdata);
        return 0;
}


int
iot_finodelk_wrapper (call_frame_t *frame, xlator_t *this,
                      const char *volume, fd_t *fd, int32_t cmd,
                      struct gf_flock *lock, dict_t *xdata)
{
        STACK_WIND (frame, iot_finodelk_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->finodelk, volume, fd, cmd, lock,
                    xdata);
        return 0;
}


int
iot_finodelk (call_frame_t *frame, xlator_t *this,
              const char *volume, fd_t *fd, int32_t cmd, struct gf_flock *lock,
              dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_finodelk_stub (frame, iot_finodelk_wrapper,
                                  volume, fd, cmd, lock, xdata);
        if (!stub) {
                gf_log (this->private, GF_LOG_ERROR,"cannot get finodelk stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (finodelk, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}

int
iot_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        STACK_UNWIND_STRICT (entrylk, frame, op_ret, op_errno, xdata);
        return 0;
}


int
iot_entrylk_wrapper (call_frame_t *frame, xlator_t *this,
                     const char *volume, loc_t *loc, const char *basename,
                     entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        STACK_WIND (frame, iot_entrylk_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->entrylk,
                    volume, loc, basename, cmd, type, xdata);
        return 0;
}


int
iot_entrylk (call_frame_t *frame, xlator_t *this,
             const char *volume, loc_t *loc, const char *basename,
             entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_entrylk_stub (frame, iot_entrylk_wrapper,
                                 volume, loc, basename, cmd, type, xdata);
        if (!stub) {
                gf_log (this->private, GF_LOG_ERROR,"cannot get entrylk stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (entrylk, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}

int
iot_fentrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        STACK_UNWIND_STRICT (fentrylk, frame, op_ret, op_errno, xdata);
        return 0;
}


int
iot_fentrylk_wrapper (call_frame_t *frame, xlator_t *this,
                      const char *volume, fd_t *fd, const char *basename,
                      entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        STACK_WIND (frame, iot_fentrylk_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fentrylk,
                    volume, fd, basename, cmd, type, xdata);
        return 0;
}


int
iot_fentrylk (call_frame_t *frame, xlator_t *this,
              const char *volume, fd_t *fd, const char *basename,
              entrylk_cmd cmd, entrylk_type type, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_fentrylk_stub (frame, iot_fentrylk_wrapper,
                                  volume, fd, basename, cmd, type, xdata);
        if (!stub) {
                gf_log (this->private, GF_LOG_ERROR,"cannot get fentrylk stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (fentrylk, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xattr, dict_t *xdata)
{
        STACK_UNWIND_STRICT (xattrop, frame, op_ret, op_errno, xattr, xdata);
        return 0;
}


int
iot_xattrop_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                     gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
        STACK_WIND (frame, iot_xattrop_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->xattrop, loc, optype, xattr, xdata);
        return 0;
}


int
iot_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
             gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_xattrop_stub (frame, iot_xattrop_wrapper, loc, optype,
                                        xattr, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create xattrop stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (xattrop, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xattr, dict_t *xdata)
{
        STACK_UNWIND_STRICT (fxattrop, frame, op_ret, op_errno, xattr, xdata);
        return 0;
}

int
iot_fxattrop_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                      gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
        STACK_WIND (frame, iot_fxattrop_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fxattrop, fd, optype, xattr, xdata);
        return 0;
}


int
iot_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
              gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_fxattrop_stub (frame, iot_fxattrop_wrapper, fd, optype,
                                  xattr, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create fxattrop stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (fxattrop, frame, -1, -ret, NULL, NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int32_t
iot_rchecksum_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, uint32_t weak_checksum,
                   uint8_t *strong_checksum, dict_t *xdata)
{
        STACK_UNWIND_STRICT (rchecksum, frame, op_ret, op_errno, weak_checksum,
                             strong_checksum, xdata);
        return 0;
}


int32_t
iot_rchecksum_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                       off_t offset, int32_t len, dict_t *xdata)
{
        STACK_WIND (frame, iot_rchecksum_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rchecksum, fd, offset, len, xdata);
        return 0;
}


int32_t
iot_rchecksum (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
               int32_t len, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_rchecksum_stub (frame, iot_rchecksum_wrapper, fd, offset,
                                   len, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create rchecksum stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (rchecksum, frame, -1, -ret, -1, NULL, NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

        return 0;
}

int
iot_fallocate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
        STACK_UNWIND_STRICT (fallocate, frame, op_ret, op_errno, preop, postop,
                             xdata);
        return 0;
}


int
iot_fallocate_wrapper(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
		      off_t offset, size_t len, dict_t *xdata)
{
        STACK_WIND (frame, iot_fallocate_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fallocate, fd, mode, offset, len,
		    xdata);
        return 0;
}


int
iot_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t mode,
	      off_t offset, size_t len, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int              ret = -1;

        stub = fop_fallocate_stub(frame, iot_fallocate_wrapper, fd, mode, offset,
				  len, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create fallocate stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (fallocate, frame, -1, -ret, NULL, NULL,
                                     NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}

int
iot_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
        STACK_UNWIND_STRICT (discard, frame, op_ret, op_errno, preop, postop,
                             xdata);
        return 0;
}


int
iot_discard_wrapper(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
		    size_t len, dict_t *xdata)
{
        STACK_WIND (frame, iot_discard_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->discard, fd, offset, len, xdata);
        return 0;
}


int
iot_discard(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
	    size_t len, dict_t *xdata)
{
        call_stub_t     *stub = NULL;
        int              ret = -1;

        stub = fop_discard_stub(frame, iot_discard_wrapper, fd, offset, len,
				xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create discard stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (discard, frame, -1, -ret, NULL, NULL,
                                     NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}

int
iot_zerofill_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
        STACK_UNWIND_STRICT (zerofill, frame, op_ret, op_errno, preop, postop,
                             xdata);
        return 0;
}

int
iot_zerofill_wrapper(call_frame_t *frame, xlator_t *this, fd_t *fd,
                     off_t offset, off_t len, dict_t *xdata)
{
        STACK_WIND (frame, iot_zerofill_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->zerofill, fd, offset, len, xdata);
        return 0;
}

int
iot_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            off_t len, dict_t *xdata)
{
        call_stub_t     *stub     = NULL;
        int              ret      = -1;

        stub = fop_zerofill_stub(frame, iot_zerofill_wrapper, fd,
                                 offset, len, xdata);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create zerofill stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (frame, this, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (zerofill, frame, -1, -ret, NULL, NULL,
                                     NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
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
                        gf_log (conf->this->name, GF_LOG_DEBUG,
                                "scaled threads to %d (queue_size=%d/%d)",
                                conf->curr_count, conf->queue_size, scale);
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
                        gf_log (this->name, GF_LOG_WARNING,
                                "Using default thread stack size %zd",
                                stacksize);
                else
                        gf_log (this->name, GF_LOG_WARNING,
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
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                                "failed");
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

	gf_proc_dump_write("cached least rate", "%u",
			   conf->throttle.cached_rate);
	gf_proc_dump_write("least rate limit", "%u", conf->throttle.rate_limit);

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

	GF_OPTION_RECONF("least-rate-limit", conf->throttle.rate_limit, options,
			 int32, out);

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
		gf_log ("io-threads", GF_LOG_ERROR,
			"FATAL: iot not configured with exactly one child");
                goto out;
	}

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

	conf = (void *) GF_CALLOC (1, sizeof (*conf),
                                   gf_iot_mt_iot_conf_t);
        if (conf == NULL) {
                gf_log (this->name, GF_LOG_ERROR,
                        "out of memory");
                goto out;
        }

        if ((ret = pthread_cond_init(&conf->cond, NULL)) != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "pthread_cond_init failed (%d)", ret);
                goto out;
        }

        if ((ret = pthread_mutex_init(&conf->mutex, NULL)) != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "pthread_mutex_init failed (%d)", ret);
                goto out;
        }

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

	GF_OPTION_INIT("least-rate-limit", conf->throttle.rate_limit, int32,
		       out);
        if ((ret = pthread_mutex_init(&conf->throttle.lock, NULL)) != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "pthread_mutex_init failed (%d)", ret);
                goto out;
        }

        conf->this = this;

        for (i = 0; i < IOT_PRI_MAX; i++) {
                INIT_LIST_HEAD (&conf->reqs[i]);
        }

	ret = iot_workers_scale (conf);

        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR,
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


void
fini (xlator_t *this)
{
	iot_conf_t *conf = this->private;

	GF_FREE (conf);

	this->private = NULL;
	return;
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
};

struct xlator_cbks cbks;

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
	{.key	= {"least-rate-limit"},
	 .type	= GF_OPTION_TYPE_INT,
	 .min	= 0,
         .max	= INT_MAX,
	 .default_value = "0",
	 .description = "Max number of least priority operations to handle "
			"per-second"
	},
	{ .key  = {NULL},
        },
};
