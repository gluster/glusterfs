/*
  Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
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


call_stub_t *
__iot_dequeue (iot_conf_t *conf)
{
        call_stub_t  *stub = NULL;
        int           i = 0;

        for (i = 0; i < IOT_PRI_MAX; i++) {
                if (list_empty (&conf->reqs[i]))
                        continue;
                stub = list_entry (conf->reqs[i].next, call_stub_t, list);
                break;
        }

        if (!stub)
                return NULL;

        conf->queue_size--;
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
        char              timeout = 0;
        char              bye = 0;

        conf = data;
        this = conf->this;
        THIS = this;

        for (;;) {
                sleep_till.tv_sec = time (NULL) + conf->idle_time;

                pthread_mutex_lock (&conf->mutex);
                {
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

                        stub = __iot_dequeue (conf);
                }
                pthread_mutex_unlock (&conf->mutex);

                if (stub) /* guard against spurious wakeups */
                        call_resume (stub);

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


int
iot_schedule_slow (iot_conf_t *conf, call_stub_t *stub)
{
        return do_iot_schedule (conf, stub, IOT_PRI_LO);
}


int
iot_schedule_fast (iot_conf_t *conf, call_stub_t *stub)
{
        return do_iot_schedule (conf, stub, IOT_PRI_HI);
}

int
iot_schedule (iot_conf_t *conf, call_stub_t *stub)
{
        return do_iot_schedule (conf, stub, IOT_PRI_NORMAL);
}


int
iot_schedule_unordered (iot_conf_t *conf, inode_t *inode, call_stub_t *stub)
{
        return do_iot_schedule (conf, stub, 0);
}


int
iot_schedule_ordered (iot_conf_t *conf, inode_t *inode, call_stub_t *stub)
{

        return do_iot_schedule (conf, stub, 0);
}


int
iot_lookup_cbk (call_frame_t *frame, void * cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                inode_t *inode, struct iatt *buf, dict_t *xattr,
                struct iatt *postparent)
{
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno, inode, buf, xattr,
                             postparent);
        return 0;
}


int
iot_lookup_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    dict_t *xattr_req)
{
        STACK_WIND (frame, iot_lookup_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->lookup,
                    loc, xattr_req);
        return 0;
}


int
iot_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xattr_req)
{
        call_stub_t     *stub = NULL;
        int              ret = -1;

        stub = fop_lookup_stub (frame, iot_lookup_wrapper, loc, xattr_req);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cannot create lookup stub (out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_fast (this->private, stub);

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
                 struct iatt *preop, struct iatt *postop)
{
        STACK_UNWIND_STRICT (setattr, frame, op_ret, op_errno, preop, postop);
        return 0;
}


int
iot_setattr_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                     struct iatt *stbuf, int32_t valid)
{
        STACK_WIND (frame, iot_setattr_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setattr,
                    loc, stbuf, valid);
        return 0;
}


int
iot_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
             struct iatt *stbuf, int32_t valid)
{
        call_stub_t     *stub = NULL;
        int              ret = -1;

        stub = fop_setattr_stub (frame, iot_setattr_wrapper, loc, stbuf, valid);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "Cannot create setattr stub"
                        "(Out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (this->private, stub);

out:
        if (ret < 0) {
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }

                STACK_UNWIND_STRICT (setattr, frame, -1, -ret, NULL, NULL);
        }

        return 0;
}


int
iot_fsetattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno,
                  struct iatt *preop, struct iatt *postop)
{
        STACK_UNWIND_STRICT (fsetattr, frame, op_ret, op_errno, preop, postop);
        return 0;
}


int
iot_fsetattr_wrapper (call_frame_t *frame, xlator_t *this,
                      fd_t *fd, struct iatt *stbuf, int32_t valid)
{
        STACK_WIND (frame, iot_fsetattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsetattr, fd, stbuf, valid);
        return 0;
}


int
iot_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
              struct iatt *stbuf, int32_t valid)
{
        call_stub_t     *stub = NULL;
        int              ret = -1;

        stub = fop_fsetattr_stub (frame, iot_fsetattr_wrapper, fd, stbuf,
                                  valid);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create fsetattr stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (this->private, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (fsetattr, frame, -1, -ret, NULL, NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (access, frame, op_ret, op_errno);
        return 0;
}


int
iot_access_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    int32_t mask)
{
        STACK_WIND (frame, iot_access_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->access, loc, mask);
        return 0;
}


int
iot_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_access_stub (frame, iot_access_wrapper, loc, mask);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create access stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_fast (this->private, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (access, frame, -1, -ret);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, const char *path,
                  struct iatt *stbuf)
{
        STACK_UNWIND_STRICT (readlink, frame, op_ret, op_errno, path, stbuf);
        return 0;
}


int
iot_readlink_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                      size_t size)
{
        STACK_WIND (frame, iot_readlink_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readlink,
                    loc, size);
        return 0;
}


int
iot_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_readlink_stub (frame, iot_readlink_wrapper, loc, size);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create readlink stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_fast (this->private, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (readlink, frame, -1, -ret, NULL, NULL);

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
               struct iatt *postparent)
{
        STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
iot_mknod_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
                   dev_t rdev, dict_t *params)
{
        STACK_WIND (frame, iot_mknod_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->mknod, loc, mode, rdev, params);
        return 0;
}


int
iot_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
           dev_t rdev, dict_t *params)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_mknod_stub (frame, iot_mknod_wrapper, loc, mode, rdev,
                               params);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create mknod stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (this->private, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (mknod, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL);

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
               struct iatt *postparent)
{
        STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
iot_mkdir_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
                   dict_t *params)
{
        STACK_WIND (frame, iot_mkdir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->mkdir, loc, mode, params);
        return 0;
}


int
iot_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
           dict_t *params)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_mkdir_stub (frame, iot_mkdir_wrapper, loc, mode, params);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create mkdir stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (this->private, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (mkdir, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *preparent,
               struct iatt *postparent)
{
        STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno, preparent,
                             postparent);
        return 0;
}


int
iot_rmdir_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags)
{
        STACK_WIND (frame, iot_rmdir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->rmdir, loc, flags);
        return 0;
}


int
iot_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_rmdir_stub (frame, iot_rmdir_wrapper, loc, flags);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create rmdir stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (this->private, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (rmdir, frame, -1, -ret, NULL, NULL);

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
                 struct iatt *postparent)
{
        STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
iot_symlink_wrapper (call_frame_t *frame, xlator_t *this, const char *linkname,
                     loc_t *loc, dict_t *params)
{
        STACK_WIND (frame, iot_symlink_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->symlink, linkname, loc, params);
        return 0;
}


int
iot_symlink (call_frame_t *frame, xlator_t *this, const char *linkname,
             loc_t *loc, dict_t *params)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_symlink_stub (frame, iot_symlink_wrapper, linkname, loc,
                                 params);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create symlink stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (this->private, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (symlink, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL);
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
                struct iatt *prenewparent, struct iatt *postnewparent)
{
        STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, buf, preoldparent,
                             postoldparent, prenewparent, postnewparent);
        return 0;
}


int
iot_rename_wrapper (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                    loc_t *newloc)
{
        STACK_WIND (frame, iot_rename_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->rename, oldloc, newloc);
        return 0;
}


int
iot_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_rename_stub (frame, iot_rename_wrapper, oldloc, newloc);
        if (!stub) {
                gf_log (this->name, GF_LOG_DEBUG, "cannot create rename stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (this->private, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (rename, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL, NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

        return 0;
}


int
iot_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, fd_t *fd)
{
	STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd);
	return 0;
}


int
iot_open_wrapper (call_frame_t * frame, xlator_t * this, loc_t *loc,
                  int32_t flags, fd_t * fd, int32_t wbflags)
{
	STACK_WIND (frame, iot_open_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->open, loc, flags, fd, wbflags);
	return 0;
}


int
iot_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          fd_t *fd, int32_t wbflags)
{
        call_stub_t	*stub = NULL;
        int             ret = -1;

        stub = fop_open_stub (frame, iot_open_wrapper, loc, flags, fd, wbflags);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cannot create open call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

	ret = iot_schedule_fast (this->private, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (open, frame, -1, -ret, NULL);

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
                struct iatt *postparent)
{
	STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, stbuf,
                             preparent, postparent);
	return 0;
}


int
iot_create_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    int32_t flags, mode_t mode, fd_t *fd, dict_t *params)
{
	STACK_WIND (frame, iot_create_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->create,
		    loc, flags, mode, fd, params);
	return 0;
}


int
iot_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
            mode_t mode, fd_t *fd, dict_t *params)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_create_stub (frame, iot_create_wrapper, loc, flags, mode,
                                fd, params);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cannot create \"create\" call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (this->private, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (create, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

        return 0;
}


int
iot_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iovec *vector,
               int32_t count, struct iatt *stbuf, struct iobref *iobref)
{
	STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno, vector, count,
                             stbuf, iobref);

	return 0;
}


int
iot_readv_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                   off_t offset)
{
	STACK_WIND (frame, iot_readv_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readv,
		    fd, size, offset);
	return 0;
}


int
iot_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
           off_t offset)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_readv_stub (frame, iot_readv_wrapper, fd, size, offset);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
			"cannot create readv call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

        ret = iot_schedule_slow (this->private, stub);

out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (readv, frame, -1, -ret, NULL, -1, NULL,
                                     NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno)
{
	STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno);
	return 0;
}


int
iot_flush_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
	STACK_WIND (frame, iot_flush_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->flush,
		    fd);
	return 0;
}


int
iot_flush (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_flush_stub (frame, iot_flush_wrapper, fd);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create flush_cbk call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

        ret = iot_schedule (this->private, stub);
out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (flush, frame, -1, -ret);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf)
{
	STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, prebuf, postbuf);
	return 0;
}


int
iot_fsync_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                   int32_t datasync)
{
	STACK_WIND (frame, iot_fsync_cbk,
		    FIRST_CHILD (this),
		    FIRST_CHILD (this)->fops->fsync,
		    fd, datasync);
	return 0;
}


int
iot_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_fsync_stub (frame, iot_fsync_wrapper, fd, datasync);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fsync_cbk call stub"
                        "(out of memory)");
                ret = -1;
                goto out;
	}

        ret = iot_schedule_slow (this->private, stub);

out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (fsync, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                struct iatt *postbuf)
{
	STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf);
	return 0;
}


int
iot_writev_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                    struct iovec *vector, int32_t count,
                    off_t offset, struct iobref *iobref)
{
	STACK_WIND (frame, iot_writev_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->writev,
		    fd, vector, count, offset, iobref);
	return 0;
}


int
iot_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
            struct iovec *vector, int32_t count, off_t offset,
            struct iobref *iobref)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_writev_stub (frame, iot_writev_wrapper,
				fd, vector, count, offset, iobref);

	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create writev call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

        ret = iot_schedule_slow (this->private, stub);
out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (writev, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

	return 0;
}


int32_t
iot_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
            int32_t op_ret, int32_t op_errno, struct gf_flock *flock)
{
	STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno, flock);
	return 0;
}


int
iot_lk_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                int32_t cmd, struct gf_flock *flock)
{
	STACK_WIND (frame, iot_lk_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->lk,
		    fd, cmd, flock);
	return 0;
}


int
iot_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
	struct gf_flock *flock)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_lk_stub (frame, iot_lk_wrapper, fd, cmd, flock);

	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_lk call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

        ret = iot_schedule (this->private, stub);
out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (lk, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
	STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, buf);
	return 0;
}


int
iot_stat_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	STACK_WIND (frame, iot_stat_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->stat,
		    loc);
	return 0;
}


int
iot_stat (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

        stub = fop_stat_stub (frame, iot_stat_wrapper, loc);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_stat call stub"
                        "(out of memory)");
                ret = -1;
                goto out;
	}

        ret = iot_schedule_fast (this->private, stub);

out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (stat, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
	STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, buf);
	return 0;
}


int
iot_fstat_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
	STACK_WIND (frame, iot_fstat_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fstat,
		    fd);
	return 0;
}


int
iot_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_fstat_stub (frame, iot_fstat_wrapper, fd);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_fstat call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

        ret = iot_schedule_fast (this->private, stub);
out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (fstat, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}


int
iot_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf)
{
	STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, prebuf,
                             postbuf);
	return 0;
}


int
iot_truncate_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                      off_t offset)
{
	STACK_WIND (frame, iot_truncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->truncate,
		    loc, offset);
	return 0;
}


int
iot_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset)
{
	call_stub_t *stub;
        int         ret = -1;

        stub = fop_truncate_stub (frame, iot_truncate_wrapper, loc, offset);

	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_stat call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

        ret = iot_schedule_slow (this->private, stub);

out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (truncate, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

	return 0;
}


int
iot_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf)
{
	STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, prebuf,
                             postbuf);
	return 0;
}


int
iot_ftruncate_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                       off_t offset)
{
	STACK_WIND (frame, iot_ftruncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->ftruncate,
		    fd, offset);
	return 0;
}


int
iot_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_ftruncate_stub (frame, iot_ftruncate_wrapper, fd, offset);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_ftruncate call stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
	}

        ret = iot_schedule_slow (this->private, stub);
out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (ftruncate, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
	return 0;
}



int
iot_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                struct iatt *postparent)
{
	STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno, preparent,
                             postparent);
	return 0;
}


int
iot_unlink_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	STACK_WIND (frame, iot_unlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->unlink,
		    loc);
	return 0;
}


int
iot_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
	call_stub_t *stub = NULL;
        int         ret = -1;

	stub = fop_unlink_stub (frame, iot_unlink_wrapper, loc);
	if (!stub) {
		gf_log (this->name, GF_LOG_ERROR,
                        "cannot create fop_unlink call stub"
                        "(out of memory)");
                ret = -1;
                goto out;
	}

        ret = iot_schedule (this->private, stub);

out:
        if (ret < 0) {
		STACK_UNWIND_STRICT (unlink, frame, -1, -ret, NULL, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

	return 0;
}


int
iot_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, inode_t *inode,
              struct iatt *buf, struct iatt *preparent, struct iatt *postparent)
{
        STACK_UNWIND_STRICT (link, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent);
        return 0;
}


int
iot_link_wrapper (call_frame_t *frame, xlator_t *this, loc_t *old, loc_t *new)
{
        STACK_WIND (frame, iot_link_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->link, old, new);

        return 0;
}


int
iot_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_link_stub (frame, iot_link_wrapper, oldloc, newloc);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create link stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (this->private, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (link, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        STACK_UNWIND_STRICT (opendir, frame, op_ret, op_errno, fd);
        return 0;
}


int
iot_opendir_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
        STACK_WIND (frame, iot_opendir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->opendir, loc, fd);
        return 0;
}


int
iot_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
        call_stub_t     *stub  = NULL;
        int             ret = -1;

        stub = fop_opendir_stub (frame, iot_opendir_wrapper, loc, fd);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create opendir stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_fast (this->private, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (opendir, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (fsyncdir, frame, op_ret, op_errno);
        return 0;
}


int
iot_fsyncdir_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                      int datasync)
{
        STACK_WIND (frame, iot_fsyncdir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsyncdir, fd, datasync);
        return 0;
}


int
iot_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int datasync)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_fsyncdir_stub (frame, iot_fsyncdir_wrapper, fd, datasync);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create fsyncdir stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_slow (this->private, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (fsyncdir, frame, -1, -ret);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct statvfs *buf)
{
        STACK_UNWIND_STRICT (statfs, frame, op_ret, op_errno, buf);
        return 0;
}


int
iot_statfs_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        STACK_WIND (frame, iot_statfs_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->statfs, loc);
        return 0;
}


int
iot_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        call_stub_t     *stub = NULL;
        int              ret = -1;

        stub = fop_statfs_stub (frame, iot_statfs_wrapper, loc);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create statfs stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_fast (this->private, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (statfs, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno);
        return 0;
}


int
iot_setxattr_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                      dict_t *dict, int32_t flags)
{
        STACK_WIND (frame, iot_setxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setxattr, loc, dict, flags);
        return 0;
}


int
iot_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
              int32_t flags)
{
        call_stub_t     *stub = NULL;
        int              ret = -1;

        stub = fop_setxattr_stub (frame, iot_setxattr_wrapper, loc, dict,
                                  flags);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create setxattr stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (this->private, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (setxattr, frame, -1, -ret);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict);
        return 0;
}


int
iot_getxattr_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                      const char *name)
{
        STACK_WIND (frame, iot_getxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->getxattr, loc, name);
        return 0;
}


int
iot_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
              const char *name)
{
        call_stub_t     *stub = NULL;
        int              ret = -1;

        stub = fop_getxattr_stub (frame, iot_getxattr_wrapper, loc, name);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create getxattr stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (this->private, stub);

out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (getxattr, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *dict)
{
        STACK_UNWIND_STRICT (fgetxattr, frame, op_ret, op_errno, dict);
        return 0;
}


int
iot_fgetxattr_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                       const char *name)
{
        STACK_WIND (frame, iot_fgetxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fgetxattr, fd, name);
        return 0;
}


int
iot_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
               const char *name)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_fgetxattr_stub (frame, iot_fgetxattr_wrapper, fd, name);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create fgetxattr stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (this->private, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (fgetxattr, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno);
        return 0;
}


int
iot_fsetxattr_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                       dict_t *dict, int32_t flags)
{
        STACK_WIND (frame, iot_fsetxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsetxattr, fd, dict, flags);
        return 0;
}


int
iot_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
               int32_t flags)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_fsetxattr_stub (frame, iot_fsetxattr_wrapper, fd, dict,
                                        flags);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create fsetxattr stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (this->private, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (fsetxattr, frame, -1, -ret);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno);
        return 0;
}


int
iot_removexattr_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                         const char *name)
{
        STACK_WIND (frame, iot_removexattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->removexattr, loc, name);
        return 0;
}


int
iot_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                 const char *name)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_removexattr_stub (frame, iot_removexattr_wrapper, loc,
                                     name);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR,"cannot get removexattr fop"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule (this->private, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (removexattr, frame, -1, -ret);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, gf_dirent_t *entries)
{
        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, entries);
        return 0;
}


int
iot_readdirp_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                      size_t size, off_t offset)
{
        STACK_WIND (frame, iot_readdirp_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readdirp, fd, size, offset);
        return 0;
}


int
iot_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t offset)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_readdirp_stub (frame, iot_readdirp_wrapper, fd, size,
                                  offset);
        if (!stub) {
                gf_log (this->private, GF_LOG_ERROR,"cannot get readdir stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_fast (this->private, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (readdirp, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, gf_dirent_t *entries)
{
        STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, entries);
        return 0;
}


int
iot_readdir_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                     size_t size, off_t offset)
{
        STACK_WIND (frame, iot_readdir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readdir, fd, size, offset);
        return 0;
}


int
iot_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_readdir_stub (frame, iot_readdir_wrapper, fd, size, offset);
        if (!stub) {
                gf_log (this->private, GF_LOG_ERROR,"cannot get readdir stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_fast (this->private, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (readdir, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xattr)
{
        STACK_UNWIND_STRICT (xattrop, frame, op_ret, op_errno, xattr);
        return 0;
}


int
iot_xattrop_wrapper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                     gf_xattrop_flags_t optype, dict_t *xattr)
{
        STACK_WIND (frame, iot_xattrop_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->xattrop, loc, optype, xattr);
        return 0;
}


int
iot_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
             gf_xattrop_flags_t optype, dict_t *xattr)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_xattrop_stub (frame, iot_xattrop_wrapper, loc, optype,
                                        xattr);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create xattrop stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_slow (this->private, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (xattrop, frame, -1, -ret, NULL);

                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int
iot_fxattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xattr)
{
        STACK_UNWIND_STRICT (fxattrop, frame, op_ret, op_errno, xattr);
        return 0;
}

int
iot_fxattrop_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                      gf_xattrop_flags_t optype, dict_t *xattr)
{
        STACK_WIND (frame, iot_fxattrop_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fxattrop, fd, optype, xattr);
        return 0;
}


int
iot_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
              gf_xattrop_flags_t optype, dict_t *xattr)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_fxattrop_stub (frame, iot_fxattrop_wrapper, fd, optype,
                                        xattr);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create fxattrop stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_slow (this->private, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (fxattrop, frame, -1, -ret, NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }
        return 0;
}


int32_t
iot_rchecksum_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, uint32_t weak_checksum,
                   uint8_t *strong_checksum)
{
        STACK_UNWIND_STRICT (rchecksum, frame, op_ret, op_errno, weak_checksum,
                             strong_checksum);
        return 0;
}


int32_t
iot_rchecksum_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd,
                       off_t offset, int32_t len)
{
        STACK_WIND (frame, iot_rchecksum_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rchecksum, fd, offset, len);
        return 0;
}


int32_t
iot_rchecksum (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
               int32_t len)
{
        call_stub_t     *stub = NULL;
        int             ret = -1;

        stub = fop_rchecksum_stub (frame, iot_rchecksum_wrapper, fd, offset,
                                   len);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create rchecksum stub"
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        ret = iot_schedule_slow (this->private, stub);
out:
        if (ret < 0) {
                STACK_UNWIND_STRICT (rchecksum, frame, -1, -ret, -1, NULL);
                if (stub != NULL) {
                        call_stub_destroy (stub);
                }
        }

        return 0;
}


int
__iot_workers_scale (iot_conf_t *conf)
{
        int       log2 = 0;
        int       scale = 0;
        int       diff = 0;
        pthread_t thread;
        int       ret = 0;

        log2 = log_base2 (conf->queue_size);

        scale = log2;

        if (log2 < IOT_MIN_THREADS)
                scale = IOT_MIN_THREADS;

        if (log2 > conf->max_count)
                scale = conf->max_count;

        if (conf->curr_count < scale) {
                diff = scale - conf->curr_count;
        }

        while (diff) {
                diff --;

                ret = pthread_create (&thread, &conf->w_attr, iot_worker, conf);
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

        pthread_attr_init (&conf->w_attr);
        err = pthread_attr_setstacksize (&conf->w_attr, stacksize);
        if (err == EINVAL) {
                gf_log (conf->this->name, GF_LOG_WARNING,
                        "Using default thread stack size");
        }
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
validate_options ( xlator_t *this, dict_t *options, char **op_errstr)
{
        int              ret = 0;
        int              thread_count;


        if (dict_get (options, "thread-count")) {
                thread_count = data_to_int32 (dict_get (options,
                                              "thread-count"));

                if (thread_count < IOT_MIN_THREADS) {
                        gf_log ("io-threads", GF_LOG_DEBUG,
                                "volume set thread_count WRONG,it is lesser");
                        ret = -1;
                        *op_errstr = gf_strdup ("LESSER Than min. threads");
                        goto out;
                }

                if (thread_count > IOT_MAX_THREADS) {
                        gf_log ("io-threads", GF_LOG_DEBUG,
                                "volume set thread_count WRONG,it is greater");
                        *op_errstr = gf_strdup ("GREATER than max. threads");
                        ret = -1;
                        goto out;
                }
        }

        ret = 0;

out:
        return ret;
}


int
reconfigure ( xlator_t *this, dict_t *options)
{
	iot_conf_t      *conf = NULL;
	int		 ret = -1;
	int		 thread_count;

        conf = this->private;
        if (!conf)
                goto out;

        thread_count = conf->max_count;

	if (dict_get (options, "thread-count")) {
                thread_count = data_to_int32 (dict_get (options,
                                                        "thread-count"));

                if (thread_count < IOT_MIN_THREADS) {
                        gf_log ("io-threads", GF_LOG_WARNING,
                                "Number of threads opted (%d) is less then "
                                "min (%d). Restoring it to previous value (%d)",
                                thread_count, IOT_MIN_THREADS, conf->max_count);
			goto out;
                }

                if (thread_count > IOT_MAX_THREADS) {
                        gf_log ("io-threads", GF_LOG_WARNING,
                                "Number of threads opted (%d) is greater than "
                                "max (%d). Restoring it to previous value (%d)",
                                thread_count, IOT_MAX_THREADS, conf->max_count);
			goto out;
                }

		conf->max_count = thread_count;
        } else
                conf->max_count = thread_count;

	ret = 0;

out:
	return ret;
}


int
init (xlator_t *this)
{
        iot_conf_t      *conf = NULL;
        dict_t          *options = this->options;
        int              thread_count = IOT_DEFAULT_THREADS;
        int              idle_time = IOT_DEFAULT_IDLE;
        int              ret = -1;
        int              i = 0;

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

        set_stack_size (conf);

        thread_count = IOT_DEFAULT_THREADS;

	if (dict_get (options, "thread-count")) {
                thread_count = data_to_int32 (dict_get (options,
                                                        "thread-count"));
                if (thread_count < IOT_MIN_THREADS) {
                        gf_log ("io-threads", GF_LOG_WARNING,
                                "Number of threads opted is less then min"
                                "threads allowed scaling it up to min");
                        thread_count = IOT_MIN_THREADS;
                }
                if (thread_count > IOT_MAX_THREADS) {
                        gf_log ("io-threads", GF_LOG_WARNING,
                                "Number of threads opted is more then max"
                                " threads allowed scaling it down to max");
                        thread_count = IOT_MAX_THREADS;
                }
        }
        conf->max_count = thread_count;

	if (dict_get (options, "idle-time")) {
                idle_time = data_to_int32 (dict_get (options,
                                                     "idle-time"));
                if (idle_time < 0)
                        idle_time = 1;
        }
        conf->idle_time = idle_time;

        conf->this = this;

        for (i = 0; i < IOT_PRI_MAX; i++) {
                INIT_LIST_HEAD (&conf->reqs[i]);
        }

	ret = iot_workers_scale (conf);

        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cannot initialize worker threads, exiting init");
                GF_FREE (conf);
                goto out;
        }

	this->private = conf;
        ret = 0;
out:
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
        .readdir     = iot_readdir,
        .readdirp    = iot_readdirp,
        .xattrop     = iot_xattrop,
	.fxattrop    = iot_fxattrop,
        .rchecksum   = iot_rchecksum,
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
	{ .key  = {"thread-count"},
	  .type = GF_OPTION_TYPE_INT,
	  .min  = IOT_MIN_THREADS,
	  .max  = IOT_MAX_THREADS
	},
        {.key   = {"idle-time"},
         .type  = GF_OPTION_TYPE_INT,
         .min   = 1,
         .max   = 0x7fffffff,
        },
	{ .key  = {NULL},
        },
};
