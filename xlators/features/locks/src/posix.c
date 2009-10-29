/*
  Copyright (c) 2006, 2007, 2008 Gluster, Inc. <http://www.gluster.com>
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

#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "compat.h"
#include "xlator.h"
#include "inode.h"
#include "logging.h"
#include "common-utils.h"

#include "locks.h"
#include "common.h"
#include "statedump.h"

#ifndef LLONG_MAX
#define LLONG_MAX LONG_LONG_MAX /* compat with old gcc */
#endif /* LLONG_MAX */

/* Forward declarations */


void do_blocked_rw (pl_inode_t *);
static int __rw_allowable (pl_inode_t *, posix_lock_t *, glusterfs_fop_t);

struct _truncate_ops {
        loc_t  loc;
        fd_t  *fd;
        off_t  offset;
        enum {TRUNCATE, FTRUNCATE} op;
};


int
pl_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                 struct stat *postbuf)
{
        struct _truncate_ops *local = NULL;

        local = frame->local;

        if (local->op == TRUNCATE)
                loc_wipe (&local->loc);

        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno,
                             prebuf, postbuf);
        return 0;
}


static int
truncate_allowed (pl_inode_t *pl_inode, 
                  transport_t *transport, pid_t client_pid, 
                  off_t offset)
{
        posix_lock_t *l = NULL;
        posix_lock_t  region = {.list = {0, }, };
        int           ret = 1;

        region.fl_start   = offset;
        region.fl_end     = LLONG_MAX;
        region.transport  = transport;
        region.client_pid = client_pid;

        pthread_mutex_lock (&pl_inode->mutex);
        {
                list_for_each_entry (l, &pl_inode->ext_list, list) {
                        if (!l->blocked
                            && locks_overlap (&region, l)
                            && !same_owner (&region, l)) {
                                ret = 0;
                                break;
                        }
                }
        }
        pthread_mutex_unlock (&pl_inode->mutex);

        return ret;
}


static int
truncate_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct stat *buf)
{
        posix_locks_private_t *priv = NULL;
        struct _truncate_ops  *local = NULL;
        inode_t               *inode = NULL;
        pl_inode_t            *pl_inode = NULL;


        priv = this->private;
        local = frame->local;

        if (op_ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, 
                        "got error (errno=%d, stderror=%s) from child", 
                        op_errno, strerror (op_errno));
                goto unwind;
        }

        if (local->op == TRUNCATE)
                inode = local->loc.inode;
        else
                inode = local->fd->inode;

        pl_inode = pl_inode_get (this, inode);
        if (!pl_inode) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                op_ret   = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        if (priv->mandatory
            && pl_inode->mandatory
            && !truncate_allowed (pl_inode, frame->root->trans,
                                  frame->root->pid, local->offset)) {
                op_ret   = -1;
                op_errno = EAGAIN;
                goto unwind;
        }

        switch (local->op) {
        case TRUNCATE:
                STACK_WIND (frame, pl_truncate_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->truncate,
                            &local->loc, local->offset);
                break;
        case FTRUNCATE:
                STACK_WIND (frame, pl_truncate_cbk, FIRST_CHILD (this),
                            FIRST_CHILD (this)->fops->ftruncate,
                            local->fd, local->offset);
                break;
        }

        return 0;

unwind:
        if (local->op == TRUNCATE)
                loc_wipe (&local->loc);

        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, buf, NULL);
        return 0;
}


int
pl_truncate (call_frame_t *frame, xlator_t *this,
             loc_t *loc, off_t offset)
{
        struct _truncate_ops *local = NULL;

        local = CALLOC (1, sizeof (struct _truncate_ops));
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                goto unwind;
        }

        local->op         = TRUNCATE;
        local->offset     = offset;
        loc_copy (&local->loc, loc);

        frame->local = local;

        STACK_WIND (frame, truncate_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, loc);

        return 0;

unwind:
        STACK_UNWIND_STRICT (truncate, frame, -1, ENOMEM, NULL, NULL);

        return 0;
}


int
pl_ftruncate (call_frame_t *frame, xlator_t *this,
              fd_t *fd, off_t offset)
{
        struct _truncate_ops *local = NULL;

        local = CALLOC (1, sizeof (struct _truncate_ops));
        if (!local) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                goto unwind;
        }

        local->op         = FTRUNCATE;
        local->offset     = offset;
        local->fd         = fd;

        frame->local = local;

        STACK_WIND (frame, truncate_stat_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fstat, fd);
        return 0;

unwind:
        STACK_UNWIND_STRICT (ftruncate, frame, -1, ENOMEM, NULL, NULL);

        return 0;
}


static void
__delete_locks_of_owner (pl_inode_t *pl_inode,
                         transport_t *transport, pid_t pid)
{
        posix_lock_t *tmp = NULL;
        posix_lock_t *l = NULL;

        /* TODO: what if it is a blocked lock with pending l->frame */

        list_for_each_entry_safe (l, tmp, &pl_inode->ext_list, list) {
                if ((l->transport == transport)
                    && (l->client_pid == pid)) {
                        __delete_lock (pl_inode, l);
                        __destroy_lock (l);
                }
        }

        return;
}


int
pl_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno)
{
        STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno);

        return 0;
}


int
pl_flush (call_frame_t *frame, xlator_t *this,
          fd_t *fd)
{
        posix_locks_private_t *priv = NULL;
        pl_inode_t            *pl_inode = NULL;

        priv = this->private;

        pl_inode = pl_inode_get (this, fd->inode);

        if (!pl_inode) {
                gf_log (this->name, GF_LOG_DEBUG, "Could not get inode.");
                STACK_UNWIND_STRICT (flush, frame, -1, EBADFD);
                return 0;
        }

        pl_trace_flush (this, frame, fd);

        pthread_mutex_lock (&pl_inode->mutex);
        {
                __delete_locks_of_owner (pl_inode, frame->root->trans,
                                         frame->root->pid);
        }
        pthread_mutex_unlock (&pl_inode->mutex);

        grant_blocked_locks (this, pl_inode);

        do_blocked_rw (pl_inode);

        STACK_WIND (frame, pl_flush_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->flush, fd);
        return 0;
}


int
pl_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd);

        return 0;
}


int
pl_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, int32_t wbflags)
{
        /* why isn't O_TRUNC being handled ? */
        STACK_WIND (frame, pl_open_cbk, 
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->open, 
                    loc, flags & ~O_TRUNC, fd, wbflags);

        return 0;
}


int
pl_create_cbk (call_frame_t *frame, void *cookie,
               xlator_t *this, int32_t op_ret, int32_t op_errno,
               fd_t *fd, inode_t *inode, struct stat *buf,
               struct stat *preparent, struct stat *postparent)
{
        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno, fd, inode, buf,
                             preparent, postparent);

        return 0;
}


int
pl_create (call_frame_t *frame, xlator_t *this,
           loc_t *loc, int32_t flags, mode_t mode, fd_t *fd)
{
        STACK_WIND (frame, pl_create_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->create, 
                    loc, flags, mode, fd);
        return 0;
}


int
pl_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno,
              struct iovec *vector, int32_t count, struct stat *stbuf,
              struct iobref *iobref)
{
        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno,
                             vector, count, stbuf, iobref);

        return 0;
}

int
pl_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this, 
               int32_t op_ret, int32_t op_errno, struct stat *prebuf,
               struct stat *postbuf)
{
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf);

        return 0;
}


void
do_blocked_rw (pl_inode_t *pl_inode)
{
        struct list_head  wind_list;
        pl_rw_req_t      *rw = NULL;
        pl_rw_req_t      *tmp = NULL;

        INIT_LIST_HEAD (&wind_list);

        pthread_mutex_lock (&pl_inode->mutex);
        {
                list_for_each_entry_safe (rw, tmp, &pl_inode->rw_list, list) {
                        if (__rw_allowable (pl_inode, &rw->region,
                                            rw->stub->fop)) {
                                list_del_init (&rw->list);
                                list_add_tail (&rw->list, &wind_list);
                        }
                }
        }
        pthread_mutex_unlock (&pl_inode->mutex);

        list_for_each_entry_safe (rw, tmp, &wind_list, list) {
                list_del_init (&rw->list);
                call_resume (rw->stub);
                free (rw);
        }

        return;
}


static int
__rw_allowable (pl_inode_t *pl_inode, posix_lock_t *region,
                glusterfs_fop_t op)
{
        posix_lock_t *l = NULL;
        int           ret = 1;

        list_for_each_entry (l, &pl_inode->ext_list, list) {
                if (locks_overlap (l, region) && !same_owner (l, region)) {
                        if ((op == GF_FOP_READ) && (l->fl_type != F_WRLCK))
                                continue;
                        ret = 0;
                        break;
                }
        }

        return ret;
}


int
pl_readv_cont (call_frame_t *frame, xlator_t *this,
               fd_t *fd, size_t size, off_t offset)
{
        STACK_WIND (frame, pl_readv_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->readv,
                    fd, size, offset);

        return 0;
}


int
pl_readv (call_frame_t *frame, xlator_t *this,
          fd_t *fd, size_t size, off_t offset)
{
        posix_locks_private_t *priv = NULL;
        pl_inode_t            *pl_inode = NULL;
        pl_rw_req_t           *rw = NULL;
        posix_lock_t           region = {.list = {0, }, };
        int                    op_ret = 0;
        int                    op_errno = 0;
        char                   wind_needed = 1;


        priv = this->private;
        pl_inode = pl_inode_get (this, fd->inode);

        if (priv->mandatory && pl_inode->mandatory) {
                region.fl_start   = offset;
                region.fl_end     = offset + size - 1;
                region.transport  = frame->root->trans;
                region.client_pid = frame->root->pid;
    
                pthread_mutex_lock (&pl_inode->mutex);
                {
                        wind_needed = __rw_allowable (pl_inode, &region,
                                                      GF_FOP_READ);
                        if (wind_needed) {
                                goto unlock;
                        }

                        if (fd->flags & O_NONBLOCK) {
                                gf_log (this->name, GF_LOG_TRACE,
                                        "returning EAGAIN as fd is O_NONBLOCK");
                                op_errno = EAGAIN;
                                op_ret = -1;
                                goto unlock;
                        }

                        rw = CALLOC (1, sizeof (*rw));
                        if (!rw) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Out of memory.");
                                op_errno = ENOMEM;
                                op_ret = -1;
                                goto unlock;
                        }

                        rw->stub = fop_readv_stub (frame, pl_readv_cont,
                                                   fd, size, offset);
                        if (!rw->stub) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Out of memory.");
                                op_errno = ENOMEM;
                                op_ret = -1;
                                free (rw);
                                goto unlock;
                        }

                        rw->region = region;

                        list_add_tail (&rw->list, &pl_inode->rw_list);
                }
        unlock:
                pthread_mutex_unlock (&pl_inode->mutex);
        }


        if (wind_needed) {
                STACK_WIND (frame, pl_readv_cbk, 
                            FIRST_CHILD (this), FIRST_CHILD (this)->fops->readv,
                            fd, size, offset);
        }

        if (op_ret == -1)
                STACK_UNWIND_STRICT (readv, frame, -1, op_errno,
                                     NULL, 0, NULL, NULL);

        return 0;
}


int
pl_writev_cont (call_frame_t *frame, xlator_t *this, fd_t *fd,
                struct iovec *vector, int count, off_t offset,
                struct iobref *iobref)
{
        STACK_WIND (frame, pl_writev_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->writev,
                    fd, vector, count, offset, iobref);

        return 0;
}


int
pl_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
           struct iovec *vector, int32_t count, off_t offset,
           struct iobref *iobref)
{
        posix_locks_private_t *priv = NULL;
        pl_inode_t            *pl_inode = NULL;
        pl_rw_req_t           *rw = NULL;
        posix_lock_t           region = {.list = {0, }, };
        int                    op_ret = 0;
        int                    op_errno = 0;
        char                   wind_needed = 1;


        priv = this->private;
        pl_inode = pl_inode_get (this, fd->inode);

        if (priv->mandatory && pl_inode->mandatory) {
                region.fl_start   = offset;
                region.fl_end     = offset + iov_length (vector, count) - 1;
                region.transport  = frame->root->trans;
                region.client_pid = frame->root->pid;
    
                pthread_mutex_lock (&pl_inode->mutex);
                {
                        wind_needed = __rw_allowable (pl_inode, &region,
                                                      GF_FOP_WRITE);
                        if (wind_needed)
                                goto unlock;

                        if (fd->flags & O_NONBLOCK) {
                                gf_log (this->name, GF_LOG_TRACE,
                                        "returning EAGAIN because fd is "
                                        "O_NONBLOCK");
                                op_errno = EAGAIN;
                                op_ret = -1;
                                goto unlock;
                        }

                        rw = CALLOC (1, sizeof (*rw));
                        if (!rw) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Out of memory.");
                                op_errno = ENOMEM;
                                op_ret = -1;
                                goto unlock;
                        }

                        rw->stub = fop_writev_stub (frame, pl_writev_cont,
                                                    fd, vector, count, offset,
                                                    iobref);
                        if (!rw->stub) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Out of memory.");
                                op_errno = ENOMEM;
                                op_ret = -1;
                                free (rw);
                                goto unlock;
                        }

                        rw->region = region;

                        list_add_tail (&rw->list, &pl_inode->rw_list);
                }
        unlock:
                pthread_mutex_unlock (&pl_inode->mutex);
        }


        if (wind_needed)
                STACK_WIND (frame, pl_writev_cbk, 
                            FIRST_CHILD (this), FIRST_CHILD (this)->fops->writev,
                            fd, vector, count, offset, iobref);

        if (op_ret == -1)
                STACK_UNWIND_STRICT (writev, frame, -1, op_errno, NULL, NULL);

        return 0;
}


int
pl_lk (call_frame_t *frame, xlator_t *this,
       fd_t *fd, int32_t cmd, struct flock *flock)
{
        transport_t           *transport = NULL;
        pid_t                  client_pid = 0;
        posix_locks_private_t *priv = NULL;
        pl_inode_t            *pl_inode = NULL;
        int                    op_ret = 0;
        int                    op_errno = 0;
        int                    can_block = 0;
        posix_lock_t          *reqlock = NULL;
        posix_lock_t          *conf = NULL;
        int                    ret = 0;

        transport  = frame->root->trans;
        client_pid = frame->root->pid;
        priv       = this->private;

        pl_inode = pl_inode_get (this, fd->inode);
        if (!pl_inode) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        reqlock = new_posix_lock (flock, transport, client_pid);
        if (!reqlock) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                op_ret = -1;
                op_errno = ENOMEM;
                goto unwind;
        }

        pl_trace_in (this, frame, fd, NULL, cmd, flock, NULL);

        switch (cmd) {

#if F_GETLK != F_GETLK64
        case F_GETLK64:
#endif
        case F_GETLK:
                conf = pl_getlk (pl_inode, reqlock);
                posix_lock_to_flock (conf, flock);
                __destroy_lock (reqlock);

                break;

#if F_SETLKW != F_SETLKW64
        case F_SETLKW64:
#endif
        case F_SETLKW:
                can_block = 1;
                reqlock->frame = frame;
                reqlock->this  = this;
                reqlock->fd    = fd;

                /* fall through */

#if F_SETLK != F_SETLK64
        case F_SETLK64:
#endif
        case F_SETLK:
                memcpy (&reqlock->user_flock, flock, sizeof (struct flock));
                ret = pl_setlk (this, pl_inode, reqlock,
                                can_block);

                if (ret == -1) {
                        if (can_block) {
                                pl_trace_block (this, frame, fd, NULL, cmd, flock, NULL);
                                goto out;
                        }
                        gf_log (this->name, GF_LOG_DEBUG, "returning EAGAIN");
                        op_ret = -1;
                        op_errno = EAGAIN;
                        __destroy_lock (reqlock);
                }
        }

unwind:
        pl_trace_out (this, frame, fd, NULL, cmd, flock, op_ret, op_errno, NULL);
        pl_update_refkeeper (this, fd->inode);
        STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno, flock);
out:
        return 0;
}


/* TODO: this function just logs, no action required?? */
int
pl_forget (xlator_t *this,
           inode_t *inode)
{
        pl_inode_t   *pl_inode = NULL;

        posix_lock_t *ext_tmp = NULL;
        posix_lock_t *ext_l   = NULL;

        pl_inode_lock_t *ino_tmp = NULL;
        pl_inode_lock_t *ino_l   = NULL;

        pl_rw_req_t *rw_tmp = NULL;
        pl_rw_req_t *rw_req = NULL;

        pl_entry_lock_t *entry_tmp = NULL;
        pl_entry_lock_t *entry_l   = NULL;

        pl_dom_list_t *dom = NULL;
        pl_dom_list_t *dom_tmp = NULL;

        pl_inode = pl_inode_get (this, inode);

        if (!list_empty (&pl_inode->rw_list)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Pending R/W requests found, releasing.");

                list_for_each_entry_safe (rw_req, rw_tmp, &pl_inode->rw_list,
                                          list) {

                        list_del (&rw_req->list);
                        FREE (rw_req);
                }
        }

        if (!list_empty (&pl_inode->ext_list)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Pending fcntl locks found, releasing.");

                list_for_each_entry_safe (ext_l, ext_tmp, &pl_inode->ext_list,
                                          list) {

                        __delete_lock (pl_inode, ext_l);
                        __destroy_lock (ext_l);
                }
        }


        list_for_each_entry_safe (dom, dom_tmp, &pl_inode->dom_list, inode_list) {

                if (!list_empty (&dom->inodelk_list)) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Pending inode locks found, releasing.");

                        list_for_each_entry_safe (ino_l, ino_tmp, &dom->inodelk_list, list) {
                                __delete_inode_lock (ino_l);
                                grant_blocked_inode_locks (this, pl_inode, ino_l, dom);
                        }

                }
                if (!list_empty (&dom->entrylk_list)) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Pending entry locks found, releasing.");

                        list_for_each_entry_safe (entry_l, entry_tmp, &dom->entrylk_list, domain_list) {
                                list_del_init (&entry_l->domain_list);
                                grant_blocked_entry_locks (this, pl_inode, entry_l, dom);

                                if (entry_l->basename)
                                        FREE (entry_l->basename);
                                FREE (entry_l);
                        }

                }

                list_del (&dom->inode_list);
                gf_log ("posix-locks", GF_LOG_TRACE,
                        " Cleaning up domain: %s", dom->domain);
                FREE (dom->domain);
                FREE (dom);
        }

        FREE (pl_inode);

        return 0;
}

static int32_t
__get_posixlk_count (xlator_t *this, pl_inode_t *pl_inode)
{
        posix_lock_t *lock   = NULL;
        int32_t       count  = 0;

        list_for_each_entry (lock, &pl_inode->ext_list, list) {

			gf_log (this->name, GF_LOG_DEBUG,
                                " XATTR DEBUG"
				"%s (pid=%d) %"PRId64" - %"PRId64" state: %s",
				lock->fl_type == F_UNLCK ? "Unlock" : "Lock",
				lock->client_pid,
				lock->user_flock.l_start,
				lock->user_flock.l_len,
                                lock->blocked == 1 ? "Blocked" : "Active");

                count++;
        }

        return count;
}

int32_t
get_posixlk_count (xlator_t *this, inode_t *inode)
{
        pl_inode_t   *pl_inode = NULL;
        uint64_t      tmp_pl_inode = 0;
        int           ret      = 0;
        int32_t       count    = 0;

        ret = inode_ctx_get (inode, this, &tmp_pl_inode);
        if (ret != 0) {
                goto out;
        }

        pl_inode = (pl_inode_t *)(long) tmp_pl_inode;

        pthread_mutex_lock (&pl_inode->mutex);
        {
                count =__get_posixlk_count (this, pl_inode);
        }
        pthread_mutex_unlock (&pl_inode->mutex);

out:
        return count;
}

void pl_entrylk_xattr_fill (xlator_t *this, inode_t *inode, 
                            dict_t *dict)
{
        int32_t     count = 0;
        int         ret   = -1;

        count = get_entrylk_count (this, inode);
        ret = dict_set_int32 (dict, GLUSTERFS_ENTRYLK_COUNT, count);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        " dict_set failed on key %s", GLUSTERFS_ENTRYLK_COUNT);
        }

}

void pl_inodelk_xattr_fill (xlator_t *this, inode_t *inode, 
                            dict_t *dict)
{
        int32_t     count = 0;
        int         ret   = -1;

        count = get_inodelk_count (this, inode);
        ret = dict_set_int32 (dict, GLUSTERFS_INODELK_COUNT, count);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        " dict_set failed on key %s", GLUSTERFS_INODELK_COUNT);
        }

}

void pl_posixlk_xattr_fill (xlator_t *this, inode_t *inode, 
                            dict_t *dict)
{
        int32_t     count = 0;
        int         ret   = -1;

        count = get_posixlk_count (this, inode);
        ret = dict_set_int32 (dict, GLUSTERFS_POSIXLK_COUNT, count);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        " dict_set failed on key %s", GLUSTERFS_POSIXLK_COUNT);
        }

}

int32_t
pl_lookup_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct stat *buf,
                    dict_t *dict,
                    struct stat *postparent)
{
        pl_local_t *local = NULL;

        if (!frame->local) {
                goto out;
        }

        local = frame->local;

        if (local->entrylk_count_req)
                pl_entrylk_xattr_fill (this, inode, dict);
        if (local->inodelk_count_req)
                pl_inodelk_xattr_fill (this, inode, dict);
        if (local->posixlk_count_req)
                pl_posixlk_xattr_fill (this, inode, dict);


        frame->local = NULL;

        if (local != NULL)
                FREE (local);

out:
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      inode,
		      buf,
                      dict,
                      postparent);
	return 0;
}

int32_t
pl_lookup (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		dict_t *xattr_req)
{
        pl_local_t *local  = NULL;
        int         ret    = -1;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        local = CALLOC (1, sizeof (*local));
        if (!local) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        " Out of memory");
                goto out;
        }

        if (dict_get (xattr_req, GLUSTERFS_ENTRYLK_COUNT))
                local->entrylk_count_req = 1;
        if (dict_get (xattr_req, GLUSTERFS_INODELK_COUNT))
                local->inodelk_count_req = 1;
        if (dict_get (xattr_req, GLUSTERFS_POSIXLK_COUNT))
                local->posixlk_count_req = 1;

        frame->local = local;

	STACK_WIND (frame,
		    pl_lookup_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->lookup,
		    loc,
		    xattr_req);
        ret = 0;
out:
        if (ret == -1)
                STACK_UNWIND_STRICT (lookup, frame, -1, 0, NULL, NULL, NULL, NULL);
    
	return 0;
}

int32_t
pl_dump_inode_priv (xlator_t *this, inode_t *inode)
{

        int             ret = -1;
        uint64_t        tmp_pl_inode = 0;
        pl_inode_t      *pl_inode = NULL;
        char            key[GF_DUMP_MAX_BUF_LEN];

        if (!inode)
                return -1;

        ret = inode_ctx_get (inode, this, &tmp_pl_inode);

        if (ret != 0) 
                return ret;
                
        pl_inode = (pl_inode_t *)(long)tmp_pl_inode;

        if (!pl_inode) 
                return -1;

        gf_proc_dump_build_key(key, 
                               "xlator.feature.locks.inode",
                               "%ld.mandatory",inode->ino);
        gf_proc_dump_write(key, "%d", pl_inode->mandatory);

        return 0;
}



/*
 * pl_dump_inode - inode dump function for posix locks
 *
 */
int
pl_dump_inode (xlator_t *this)
{

        assert(this);
        
        if (this->itable) {
                inode_table_dump(this->itable,
                                 "xlator.features.locks.inode_table");
        }

        return 0;
}



int
init (xlator_t *this)
{
        posix_locks_private_t *priv = NULL;
        xlator_list_t         *trav = NULL;
        data_t                *mandatory = NULL;
	data_t                *trace = NULL;

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_CRITICAL, 
                        "FATAL: posix-locks should have exactly one child");
                return -1;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Volume is dangling. Please check the volume file.");
        }

        trav = this->children;
        while (trav->xlator->children)
                trav = trav->xlator->children;

        if (strncmp ("storage/", trav->xlator->type, 8)) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "'locks' translator is not loaded over a storage "
                        "translator");
                return -1;
        }

        priv = CALLOC (1, sizeof (*priv));

        mandatory = dict_get (this->options, "mandatory-locks");
        if (mandatory) {
                if (gf_string2boolean (mandatory->data,
                                       &priv->mandatory) == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "'mandatory-locks' takes on only boolean "
                                "values.");
                        return -1;
                }
        }

	trace = dict_get (this->options, "trace");
	if (trace) {
		if (gf_string2boolean (trace->data,
				       &priv->trace) == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"'trace' takes on only boolean values.");
			return -1;
		}
	}

        this->private = priv;
        return 0;
}


int
fini (xlator_t *this)
{
        posix_locks_private_t *priv = NULL;

        priv = this->private;
        free (priv);

        return 0;
}


int
pl_inodelk (call_frame_t *frame, xlator_t *this, 
            const char *volume, loc_t *loc, int32_t cmd, struct flock *flock);

int
pl_finodelk (call_frame_t *frame, xlator_t *this, 
             const char *volume, fd_t *fd, int32_t cmd, struct flock *flock);

int
pl_entrylk (call_frame_t *frame, xlator_t *this, 
            const char *volume, loc_t *loc, const char *basename, 
            entrylk_cmd cmd, entrylk_type type);

int
pl_fentrylk (call_frame_t *frame, xlator_t *this, 
             const char *volume, fd_t *fd, const char *basename, 
             entrylk_cmd cmd, entrylk_type type);

struct xlator_fops fops = {
        .lookup      = pl_lookup,
        .create      = pl_create,
        .truncate    = pl_truncate,
        .ftruncate   = pl_ftruncate,
        .open        = pl_open,
        .readv       = pl_readv,
        .writev      = pl_writev,
        .lk          = pl_lk,
        .inodelk     = pl_inodelk,
        .finodelk    = pl_finodelk,
        .entrylk     = pl_entrylk,
        .fentrylk    = pl_fentrylk,
        .flush       = pl_flush,
};


struct xlator_mops mops = {
};

struct xlator_dumpops dumpops = {
        .inodectx    = pl_dump_inode_priv,
};

struct xlator_cbks cbks = {
        .forget      = pl_forget,
};


struct volume_options options[] = {
        { .key  = { "mandatory-locks", "mandatory" }, 
          .type = GF_OPTION_TYPE_BOOL 
        },
	{ .key  = { "trace" }, 
	  .type = GF_OPTION_TYPE_BOOL 
	},
        { .key = {NULL} },
};
