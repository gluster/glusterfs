/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
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

#include <fnmatch.h>
#include "call-stub.h"
#include "defaults.h"
#include "xlator.h"
#include "glfs.h"
#include "glfs-internal.h"
#include "run.h"
#include "common-utils.h"
#include "syncop.h"
#include "syscall.h"
#include "compat-errno.h"

#include "jbr-internal.h"
#include "jbr-messages.h"

#define JBR_FLUSH_INTERVAL      5

enum {
        /* echo "cluster/jbr-server" | md5sum | cut -c 1-8 */
        JBR_SERVER_IPC_BASE = 0x0e2d66a5,
        JBR_SERVER_TERM_RANGE,
        JBR_SERVER_OPEN_TERM,
        JBR_SERVER_NEXT_ENTRY
};

/*
 * Need to declare jbr_lk_call_dispatch as jbr_lk_continue and *
 * jbr_lk_perform_local_op call it, before code is generated.  *
 */
int32_t
jbr_lk_call_dispatch (call_frame_t *frame, xlator_t *this, int *op_errno,
                      fd_t *fd, int32_t cmd, struct gf_flock *lock,
                      dict_t *xdata);

int32_t
jbr_lk_dispatch (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, int32_t cmd, struct gf_flock *lock,
                 dict_t *xdata);

/* Used to check the quorum of acks received after the fop
 * confirming the status of the fop on all the brick processes
 * for this particular subvolume
 */
gf_boolean_t
fop_quorum_check (xlator_t *this, double n_children,
                  double current_state)
{
        jbr_private_t   *priv           = NULL;
        gf_boolean_t     result         = _gf_false;
        double           required       = 0;
        double           current        = 0;

        GF_VALIDATE_OR_GOTO ("jbr", this, out);
        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        required = n_children * priv->quorum_pct;

        /*
         * Before performing the fop on the leader, we need to check,
         * if there is any merit in performing the fop on the leader.
         * In a case, where even a successful write on the leader, will
         * not meet quorum, there is no point in trying the fop on the
         * leader.
         * When this function is called after the leader has tried
         * performing the fop, this check will calculate quorum taking into
         * account the status of the fop on the leader. If the leader's
         * op_ret was -1, the complete function would account that by
         * decrementing successful_acks by 1
         */

        current = current_state * 100.0;

        if (current < required) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        J_MSG_QUORUM_NOT_MET,
                        "Quorum not met. quorum_pct = %f "
                        "Current State = %f, Required State = %f",
                        priv->quorum_pct, current,
                        required);
        } else
                result = _gf_true;

out:
        return result;
}

jbr_inode_ctx_t *
jbr_get_inode_ctx (xlator_t *this, inode_t *inode)
{
        uint64_t                ctx_int         = 0LL;
        jbr_inode_ctx_t         *ctx_ptr;

        if (__inode_ctx_get(inode, this, &ctx_int) == 0) {
                ctx_ptr = (jbr_inode_ctx_t *)(long)ctx_int;
        } else {
                ctx_ptr = GF_CALLOC (1, sizeof(*ctx_ptr),
                                     gf_mt_jbr_inode_ctx_t);
                if (ctx_ptr) {
                        ctx_int = (uint64_t)(long)ctx_ptr;
                        if (__inode_ctx_set(inode, this, &ctx_int) == 0) {
                                LOCK_INIT(&ctx_ptr->lock);
                                INIT_LIST_HEAD(&ctx_ptr->aqueue);
                                INIT_LIST_HEAD(&ctx_ptr->pqueue);
                        } else {
                                GF_FREE(ctx_ptr);
                                ctx_ptr = NULL;
                        }
                }

        }

        return ctx_ptr;
}

jbr_fd_ctx_t *
jbr_get_fd_ctx (xlator_t *this, fd_t *fd)
{
        uint64_t                ctx_int         = 0LL;
        jbr_fd_ctx_t            *ctx_ptr;

        if (__fd_ctx_get(fd, this, &ctx_int) == 0) {
                ctx_ptr = (jbr_fd_ctx_t *)(long)ctx_int;
        } else {
                ctx_ptr = GF_CALLOC (1, sizeof(*ctx_ptr), gf_mt_jbr_fd_ctx_t);
                if (ctx_ptr) {
                        if (__fd_ctx_set(fd, this, (uint64_t)ctx_ptr) == 0) {
                                INIT_LIST_HEAD(&ctx_ptr->dirty_list);
                                INIT_LIST_HEAD(&ctx_ptr->fd_list);
                        } else {
                                GF_FREE(ctx_ptr);
                                ctx_ptr = NULL;
                        }
                }

        }

        return ctx_ptr;
}

void
jbr_mark_fd_dirty (xlator_t *this, jbr_local_t *local)
{
        fd_t                    *fd             = local->fd;
        jbr_fd_ctx_t            *ctx_ptr;
        jbr_dirty_list_t        *dirty;
        jbr_private_t           *priv           = this->private;

        /*
         * TBD: don't do any of this for O_SYNC/O_DIRECT writes.
         * Unfortunately, that optimization requires that we distinguish
         * between writev and other "write" calls, saving the original flags
         * and checking them in the callback.  Too much work for too little
         * gain right now.
         */

        LOCK(&fd->lock);
                ctx_ptr = jbr_get_fd_ctx(this, fd);
                dirty = GF_CALLOC(1, sizeof(*dirty), gf_mt_jbr_dirty_t);
                if (ctx_ptr && dirty) {
                        gf_msg_trace (this->name, 0,
                                      "marking fd %p as dirty (%p)", fd, dirty);
                        /* TBD: fill dirty->id from what changelog gave us */
                        list_add_tail(&dirty->links, &ctx_ptr->dirty_list);
                        if (list_empty(&ctx_ptr->fd_list)) {
                                /* Add a ref so _release doesn't get called. */
                                ctx_ptr->fd = fd_ref(fd);
                                LOCK(&priv->dirty_lock);
                                        list_add_tail (&ctx_ptr->fd_list,
                                                       &priv->dirty_fds);
                                UNLOCK(&priv->dirty_lock);
                        }
                } else {
                        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                J_MSG_MEM_ERR, "could not mark %p dirty", fd);
                        if (ctx_ptr) {
                                GF_FREE(ctx_ptr);
                        }
                        if (dirty) {
                                GF_FREE(dirty);
                        }
                }
        UNLOCK(&fd->lock);
}

#define JBR_TERM_XATTR          "trusted.jbr.term"
#define JBR_INDEX_XATTR         "trusted.jbr.index"
#define JBR_REP_COUNT_XATTR     "trusted.jbr.rep-count"
#define RECON_TERM_XATTR        "trusted.jbr.recon-term"
#define RECON_INDEX_XATTR       "trusted.jbr.recon-index"

int32_t
jbr_leader_checks_and_init (call_frame_t *frame, xlator_t *this, int *op_errno,
                            dict_t *xdata, fd_t *fd)
{
        jbr_local_t     *local         = NULL;
        jbr_private_t   *priv          = NULL;
        int32_t          ret           = -1;
        gf_boolean_t     result        = _gf_false;
        int              from_leader   = _gf_false;
        int              from_recon    = _gf_false;

        GF_VALIDATE_OR_GOTO ("jbr", this, out);
        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);
        GF_VALIDATE_OR_GOTO (this->name, op_errno, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);

        /*
         * Our first goal here is to avoid "split brain surprise" for users who
         * specify exactly 50% with two- or three-way replication.  That means
         * either a more-than check against half the total replicas or an
         * at-least check against half of our peers (one less).  Of the two,
         * only an at-least check supports the intuitive use of 100% to mean
         * all replicas must be present, because "more than 100%" will never
         * succeed regardless of which count we use.  This leaves us with a
         * slightly non-traditional definition of quorum ("at least X% of peers
         * not including ourselves") but one that's useful enough to be worth
         * it.
         *
         * Note that n_children and up_children *do* include the local
         * subvolume, so we need to subtract one in each case.
         */
        if (priv->leader) {
                result = fop_quorum_check (this, (double)(priv->n_children - 1),
                                   (double)(priv->up_children - 1));

                if (result == _gf_false) {
                        /* Emulate the AFR client-side-quorum behavior. */
                        gf_msg (this->name, GF_LOG_ERROR, EROFS,
                                J_MSG_QUORUM_NOT_MET, "Sufficient number of "
                                "subvolumes are not up to meet quorum.");
                        *op_errno = EROFS;
                        goto out;
                }
        } else {
                if (xdata) {
                        from_leader = !!dict_get(xdata, JBR_TERM_XATTR);
                        from_recon = !!dict_get(xdata, RECON_TERM_XATTR)
                                  && !!dict_get(xdata, RECON_INDEX_XATTR);
                } else {
                        from_leader = from_recon = _gf_false;
                }

                /* follower/recon path        *
                 * just send it to local node *
                 */
                if (!from_leader && !from_recon) {
                        *op_errno = EREMOTE;
                        goto out;
                }
        }

        local = mem_get0(this->local_pool);
        if (!local) {
                goto out;
        }

        if (fd)
                local->fd = fd_ref(fd);
        else
                local->fd = NULL;

        INIT_LIST_HEAD(&local->qlinks);
        frame->local = local;

        ret = 0;
out:
        return ret;
}

int32_t
jbr_initialize_xdata_set_attrs (xlator_t *this, dict_t **xdata)
{
        jbr_local_t     *local         = NULL;
        jbr_private_t   *priv          = NULL;
        int32_t          ret           = -1;
        uint32_t         ti            = 0;

        GF_VALIDATE_OR_GOTO ("jbr", this, out);
        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);
        GF_VALIDATE_OR_GOTO (this->name, xdata, out);

        if (!*xdata) {
                *xdata = dict_new();
                if (!*xdata) {
                        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                J_MSG_MEM_ERR, "failed to allocate xdata");
                        goto out;
                }
        }

        if (dict_set_int32(*xdata, JBR_TERM_XATTR, priv->current_term) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        J_MSG_DICT_FLR, "failed to set jbr-term");
                goto out;
        }

        LOCK(&priv->index_lock);
        ti = ++(priv->index);
        UNLOCK(&priv->index_lock);
        if (dict_set_int32(*xdata, JBR_INDEX_XATTR, ti) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        J_MSG_DICT_FLR, "failed to set index");
                goto out;
        }

        ret = 0;
out:
        return ret;
}

int32_t
jbr_remove_from_queue (call_frame_t *frame, xlator_t *this)
{
        int32_t          ret       = -1;
        jbr_inode_ctx_t *ictx      = NULL;
        jbr_local_t     *local     = NULL;
        jbr_local_t     *next      = NULL;

        GF_VALIDATE_OR_GOTO ("jbr", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        local = frame->local;
        GF_VALIDATE_OR_GOTO (this->name, local, out);

        if (local->qlinks.next != &local->qlinks) {
                list_del(&local->qlinks);
                ictx = jbr_get_inode_ctx(this, local->fd->inode);
                if (ictx) {
                        LOCK(&ictx->lock);
                                if (ictx->pending) {
                                        /*
                                         * TBD: dequeue *all* non-conflicting
                                         * reqs
                                         *
                                         * With the stub implementation there
                                         * can only be one request active at a
                                         * time (zero here) so it's not an
                                         * issue.  In a real implementation
                                         * there might still be other active
                                         * requests to check against, and
                                         * multiple pending requests that could
                                         * continue.
                                         */
                                        gf_msg_debug (this->name, 0,
                                                     "unblocking next request");
                                        --(ictx->pending);
                                        next = list_entry (ictx->pqueue.next,
                                                           jbr_local_t, qlinks);
                                        list_del(&next->qlinks);
                                        list_add_tail(&next->qlinks,
                                                      &ictx->aqueue);
                                        call_resume(next->qstub);
                                } else {
                                        --(ictx->active);
                                }
                        UNLOCK(&ictx->lock);
                }
        }

        ret = 0;

out:
        return ret;
}

int32_t
jbr_lk_complete (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 struct gf_flock *flock, dict_t *xdata)
{
        int32_t          ret       = -1;
        jbr_private_t   *priv      = NULL;
        jbr_local_t     *local     = NULL;
        gf_boolean_t     result    = _gf_false;

        GF_VALIDATE_OR_GOTO ("jbr", this, err);
        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, err);
        GF_VALIDATE_OR_GOTO (this->name, frame, err);
        local = frame->local;
        GF_VALIDATE_OR_GOTO (this->name, local, err);
        GF_VALIDATE_OR_GOTO (this->name, flock, err);
        GF_VALIDATE_OR_GOTO (this->name, xdata, err);

        /*
         * Remove from queue for unlock operation only   *
         * For lock operation, it will be done in fan-in *
         */
        if (flock->l_type == F_UNLCK) {
                ret = jbr_remove_from_queue (frame, this);
                if (ret)
                        goto err;
        }

        /*
         * On a follower, unwind with the op_ret and op_errno. On a *
         * leader, if the fop is a locking fop, and its a failure,  *
         * send fail, else call stub which will dispatch the fop to *
         * the followers.                                           *
         *                                                          *
         * If the fop is a unlocking fop, check quorum. If quorum   *
         * is met, then send success. Else Rollback on leader,      *
         * followed by followers, and then send -ve ack to client.  *
         */
        if (priv->leader) {

                /* Increase the successful acks if it's a success. */
                LOCK(&frame->lock);
                if (op_ret != -1)
                        (local->successful_acks)++;
                UNLOCK(&frame->lock);

                if (flock->l_type == F_UNLCK) {
                        result = fop_quorum_check (this,
                                            (double)priv->n_children,
                                            (double)local->successful_acks);
                        if (result == _gf_false) {
                                op_ret = -1;
                                op_errno = EROFS;
                                gf_msg (this->name, GF_LOG_ERROR, EROFS,
                                        J_MSG_QUORUM_NOT_MET,
                                        "Quorum is not met. "
                                        "The operation has failed.");

                                /* TODO: PERFORM UNLOCK ROLLBACK ON LEADER *
                                 * FOLLOWED BY FOLLOWERS. */
                        } else {
                                op_ret = 0;
                                op_errno = 0;
                        }

                        fd_unref(local->fd);
                        STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno,
                                             flock, xdata);
                } else {
                        if (op_ret == -1) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        J_MSG_LOCK_FAILURE,
                                        "The lock operation failed on "
                                        "the leader.");

                                fd_unref(local->fd);
                                STACK_UNWIND_STRICT (lk, frame, op_ret,
                                                     op_errno, flock, xdata);
                        } else {
                                if (!local->stub) {
                                        goto err;
                                }

                                call_resume(local->stub);
                        }
                }
        } else {
                fd_unref(local->fd);
                STACK_UNWIND_STRICT (lk, frame, op_ret, op_errno,
                                     flock, xdata);
        }

        return 0;

err:
        if (local) {
                if (local->stub) {
                        call_stub_destroy(local->stub);
                }
                if (local->qstub) {
                        call_stub_destroy(local->qstub);
                }
                if (local->fd) {
                        fd_unref(local->fd);
                }
                mem_put(local);
        }
        STACK_UNWIND_STRICT (lk, frame, -1, op_errno,
                             flock, xdata);
        return 0;
}

int32_t
jbr_lk_fan_in (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct gf_flock *flock,
               dict_t *xdata)
{
        uint8_t          call_count = -1;
        int32_t          ret        = -1;
        gf_boolean_t     result     = _gf_false;
        jbr_local_t     *local      = NULL;
        jbr_private_t   *priv       = NULL;

        GF_VALIDATE_OR_GOTO ("jbr", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        priv = this->private;
        local = frame->local;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);
        GF_VALIDATE_OR_GOTO (this->name, local, out);

        gf_msg_trace (this->name, 0, "op_ret = %d, op_errno = %d\n",
                      op_ret, op_errno);

        LOCK(&frame->lock);
        call_count = --(local->call_count);
        if (op_ret != -1) {
                /* Increment the number of successful acks *
                 * received for the operation.             *
                 */
                (local->successful_acks)++;
                local->successful_op_ret = op_ret;
        }
        gf_msg_debug (this->name, 0, "succ_acks = %d, op_ret = %d, op_errno = %d\n",
                      op_ret, op_errno, local->successful_acks);
        UNLOCK(&frame->lock);

        if (call_count == 0) {
                /*
                 * If the fop is a locking fop, then check quorum. If quorum *
                 * is met, send successful ack to the client. If quorum is   *
                 * not met, then rollback locking on followers, followed by  *
                 * rollback of locking on leader, and then sending -ve ack   *
                 * to the client.                                            *
                 *                                                           *
                 * If the fop is a unlocking fop, then call stub.            *
                 */
                if (flock->l_type == F_UNLCK) {
                        call_resume(local->stub);
                } else {
                        /*
                         * Remove from queue for locking fops, for unlocking *
                         * fops, it is taken care of in jbr_lk_complete      *
                         */
                        ret = jbr_remove_from_queue (frame, this);
                        if (ret)
                                goto out;

                        fd_unref(local->fd);

                        result = fop_quorum_check (this,
                                          (double)priv->n_children,
                                          (double)local->successful_acks);
                        if (result == _gf_false) {
                                gf_msg (this->name, GF_LOG_ERROR, EROFS,
                                        J_MSG_QUORUM_NOT_MET,
                                        "Didn't receive enough acks to meet "
                                        "quorum. Failing the locking "
                                        "operation and initiating rollback on "
                                        "followers and the leader "
                                        "respectively.");

                                /* TODO: PERFORM ROLLBACK OF LOCKING ON
                                 * FOLLOWERS, FOLLOWED BY ROLLBACK ON
                                 * LEADER.
                                 */

                                STACK_UNWIND_STRICT (lk, frame, -1, EROFS,
                                                     flock, xdata);
                        } else {
                                STACK_UNWIND_STRICT (lk, frame, 0, 0,
                                                     flock, xdata);
                        }
                }
        }

        ret = 0;
out:
        return ret;
}

/*
 * Called from leader for locking fop, being writen as a separate *
 * function so as to support queues.                              *
 */
int32_t
jbr_perform_lk_on_leader (call_frame_t *frame, xlator_t *this,
                         fd_t *fd, int32_t cmd, struct gf_flock *flock,
                         dict_t *xdata)
{
        int32_t          ret    = -1;

        GF_VALIDATE_OR_GOTO ("jbr", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, flock, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        STACK_WIND (frame, jbr_lk_complete,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->lk,
                    fd, cmd, flock, xdata);

        ret = 0;
out:
        return ret;
}

int32_t
jbr_lk_perform_local_op (call_frame_t *frame, xlator_t *this, int *op_errno,
                         fd_t *fd, int32_t cmd, struct gf_flock *flock,
                         dict_t *xdata)
{
        int32_t          ret    = -1;
        jbr_local_t     *local  = NULL;

        GF_VALIDATE_OR_GOTO ("jbr", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        local = frame->local;
        GF_VALIDATE_OR_GOTO (this->name, local, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, op_errno, out);
        GF_VALIDATE_OR_GOTO (this->name, flock, out);

        /*
         * Check if the fop is a locking fop or unlocking fop, and
         * handle it accordingly. If it is a locking fop, take the
         * lock on leader first, and then send it to the followers.
         * If it is a unlocking fop, unlock the followers first,
         * and then on meeting quorum perform the unlock on the leader.
         */
        if (flock->l_type == F_UNLCK) {
                ret = jbr_lk_call_dispatch (frame, this, op_errno,
                                            fd, cmd, flock, xdata);
                if (ret)
                        goto out;
        } else {
                jbr_inode_ctx_t  *ictx  = jbr_get_inode_ctx(this, fd->inode);

                if (!ictx) {
                        *op_errno = EIO;
                        goto out;
                }

                LOCK(&ictx->lock);
                        if (ictx->active) {
                                gf_msg_debug (this->name, 0,
                                              "queuing request due to conflict");

                                local->qstub = fop_lk_stub (frame,
                                                       jbr_perform_lk_on_leader,
                                                       fd, cmd, flock, xdata);
                                if (!local->qstub) {
                                        UNLOCK(&ictx->lock);
                                        goto out;
                                }
                                list_add_tail(&local->qlinks, &ictx->pqueue);
                                ++(ictx->pending);
                                UNLOCK(&ictx->lock);
                                ret = 0;
                                goto out;
                        } else {
                                list_add_tail(&local->qlinks, &ictx->aqueue);
                                ++(ictx->active);
                        }
                UNLOCK(&ictx->lock);
                ret = jbr_perform_lk_on_leader (frame, this, fd, cmd,
                                                flock, xdata);
        }

        ret = 0;
out:
        return ret;
}

int32_t
jbr_lk_continue (call_frame_t *frame, xlator_t *this,
                 fd_t *fd, int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
        int32_t          ret      = -1;
        jbr_local_t     *local    = NULL;
        jbr_private_t   *priv     = NULL;

        GF_VALIDATE_OR_GOTO ("jbr", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        priv = this->private;
        local = frame->local;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);
        GF_VALIDATE_OR_GOTO (this->name, local, out);
        GF_VALIDATE_OR_GOTO (this->name, flock, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, xdata, out);

        /*
         * If it's a locking fop, then call dispatch to followers  *
         * If it's a unlock fop, then perform the unlock operation *
         */
        if (flock->l_type == F_UNLCK) {
                STACK_WIND (frame, jbr_lk_complete,
                            FIRST_CHILD(this), FIRST_CHILD(this)->fops->lk,
                            fd, cmd, flock, xdata);
        } else {
                /*
                 * Directly call jbr_lk_dispatch instead of appending *
                 * in queue, which is done at jbr_lk_perform_local_op *
                 * for locking fops                                   *
                 */
                ret = jbr_lk_dispatch (frame, this, fd, cmd,
                                       flock, xdata);
                if (ret) {
                        STACK_UNWIND_STRICT (lk, frame, -1, 0,
                                             flock, xdata);
                        goto out;
                }
        }

        ret = 0;
out:
        return ret;
}

#pragma generate

uint8_t
jbr_count_up_kids (jbr_private_t *priv)
{
        uint8_t         retval  = 0;
        uint8_t         i;

        for (i = 0; i < priv->n_children; ++i) {
                if (priv->kid_state & (1 << i)) {
                        ++retval;
                }
        }

        return retval;
}

/*
 * The fsync machinery looks a lot like that for any write call, but there are
 * some important differences that are easy to miss.  First, we don't care
 * about the xdata that shows whether the call came from a leader or
 * reconciliation process.  If we're the leader we fan out; if we're not we
 * don't.  Second, we don't wait for followers before we issue the local call.
 * The code generation system could be updated to handle this, and still might
 * if we need to implement other "almost identical" paths (e.g. for open), but
 * a copy is more readable as long as it's just one.
 */

int32_t
jbr_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf, dict_t *xdata)
{
        jbr_local_t     *local  = frame->local;
        gf_boolean_t    unwind;

        LOCK(&frame->lock);
                unwind = !--(local->call_count);
        UNLOCK(&frame->lock);

        if (unwind) {
                STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, prebuf,
                                     postbuf, xdata);
        }
        return 0;
}

int32_t
jbr_fsync_local_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata)
{
        jbr_dirty_list_t        *dirty;
        jbr_dirty_list_t        *dtmp;
        jbr_local_t             *local  = frame->local;

        list_for_each_entry_safe (dirty, dtmp, &local->qlinks, links) {
                gf_msg_trace (this->name, 0,
                              "sending post-op on %p (%p)", local->fd, dirty);
                GF_FREE(dirty);
        }

        return jbr_fsync_cbk (frame, cookie, this, op_ret, op_errno,
                              prebuf, postbuf, xdata);
}

int32_t
jbr_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
           dict_t *xdata)
{
        jbr_private_t   *priv   = this->private;
        jbr_local_t     *local;
        uint64_t        ctx_int         = 0LL;
        jbr_fd_ctx_t    *ctx_ptr;
        xlator_list_t   *trav;

        local = mem_get0(this->local_pool);
        if (!local) {
                STACK_UNWIND_STRICT(fsync, frame, -1, ENOMEM,
                                    NULL, NULL, xdata);
                return 0;
        }
        INIT_LIST_HEAD(&local->qlinks);
        frame->local = local;

        /* Move the dirty list from the fd to the fsync request. */
        LOCK(&fd->lock);
                if (__fd_ctx_get(fd, this, &ctx_int) == 0) {
                        ctx_ptr = (jbr_fd_ctx_t *)(long)ctx_int;
                        list_splice_init (&ctx_ptr->dirty_list,
                                          &local->qlinks);
                }
        UNLOCK(&fd->lock);

        /* Issue the local call. */
        local->call_count = priv->leader ? priv->n_children : 1;
        STACK_WIND (frame, jbr_fsync_local_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fsync,
                    fd, flags, xdata);

        /* Issue remote calls if we're the leader. */
        if (priv->leader) {
                for (trav = this->children->next; trav; trav = trav->next) {
                        STACK_WIND (frame, jbr_fsync_cbk,
                                    FIRST_CHILD(this),
                                    FIRST_CHILD(this)->fops->fsync,
                                    fd, flags, xdata);
                }
        }

        return 0;
}

int32_t
jbr_getxattr_special (call_frame_t *frame, xlator_t *this, loc_t *loc,
                      const char *name, dict_t *xdata)
{
        dict_t          *result;
        jbr_private_t   *priv   = this->private;

        if (!priv->leader) {
                STACK_UNWIND_STRICT (getxattr, frame, -1, EREMOTE, NULL, NULL);
                return 0;
        }

        if (!name || (strcmp(name, JBR_REP_COUNT_XATTR) != 0)) {
                STACK_WIND_TAIL (frame,
                                 FIRST_CHILD(this),
                                 FIRST_CHILD(this)->fops->getxattr,
                                 loc, name, xdata);
                return 0;
        }

        result = dict_new();
        if (!result) {
                goto dn_failed;
        }

        priv->up_children = jbr_count_up_kids(this->private);
        if (dict_set_uint32(result, JBR_REP_COUNT_XATTR,
                            priv->up_children) != 0) {
                goto dsu_failed;
        }

        STACK_UNWIND_STRICT (getxattr, frame, 0, 0, result, NULL);
        dict_destroy(result);
        return 0;

dsu_failed:
        dict_destroy(result);
dn_failed:
        STACK_UNWIND_STRICT (getxattr, frame, -1, ENOMEM, NULL, NULL);
        return 0;
}

void
jbr_flush_fd (xlator_t *this, jbr_fd_ctx_t *fd_ctx)
{
        jbr_dirty_list_t        *dirty;
        jbr_dirty_list_t        *dtmp;

        list_for_each_entry_safe (dirty, dtmp, &fd_ctx->dirty_list, links) {
                gf_msg_trace (this->name, 0,
                              "sending post-op on %p (%p)", fd_ctx->fd, dirty);
                GF_FREE(dirty);
        }

        INIT_LIST_HEAD(&fd_ctx->dirty_list);
}

void *
jbr_flush_thread (void *ctx)
{
        xlator_t                *this   = ctx;
        jbr_private_t           *priv   = this->private;
        struct list_head        dirty_fds;
        jbr_fd_ctx_t            *fd_ctx;
        jbr_fd_ctx_t            *fd_tmp;
        int                     ret;

        for (;;) {
                /*
                 * We have to be very careful to avoid lock inversions here, so
                 * we can't just hold priv->dirty_lock while we take and
                 * release locks for each fd.  Instead, we only hold dirty_lock
                 * at the beginning of each iteration, as we (effectively) make
                 * a copy of the current list head and then clear the original.
                 * This leads to four scenarios for adding the first entry to
                 * an fd and potentially putting it on the global list.
                 *
                 * (1) While we're asleep.  No lock contention, it just gets
                 *     added and will be processed on the next iteration.
                 *
                 * (2) After we've made a local copy, but before we've started
                 *     processing that fd.  The new entry will be added to the
                 *     fd (under its lock), and we'll process it on the current
                 *     iteration.
                 *
                 * (3) While we're processing the fd.  They'll block on the fd
                 *     lock, then see that the list is empty and put it on the
                 *     global list.  We'll process it here on the next
                 *     iteration.
                 *
                 * (4) While we're working, but after we've processed that fd.
                 *     Same as (1) as far as that fd is concerned.
                 */
                INIT_LIST_HEAD(&dirty_fds);
                LOCK(&priv->dirty_lock);
                list_splice_init(&priv->dirty_fds, &dirty_fds);
                UNLOCK(&priv->dirty_lock);

                list_for_each_entry_safe (fd_ctx, fd_tmp, &dirty_fds, fd_list) {
                        ret = syncop_fsync(FIRST_CHILD(this), fd_ctx->fd, 0,
                                           NULL, NULL);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        J_MSG_SYS_CALL_FAILURE,
                                        "failed to fsync %p (%d)",
                                        fd_ctx->fd, -ret);
                        }

                        LOCK(&fd_ctx->fd->lock);
                                jbr_flush_fd(this, fd_ctx);
                                list_del_init(&fd_ctx->fd_list);
                        UNLOCK(&fd_ctx->fd->lock);
                        fd_unref(fd_ctx->fd);
                }

                sleep(JBR_FLUSH_INTERVAL);
        }

        return NULL;
}


int32_t
jbr_get_changelog_dir (xlator_t *this, char **cl_dir_p)
{
        xlator_t        *cl_xl;

        /* Find our changelog translator. */
        cl_xl = this;
        while (cl_xl) {
                if (strcmp(cl_xl->type, "features/changelog") == 0) {
                        break;
                }
                cl_xl = cl_xl->children->xlator;
        }
        if (!cl_xl) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        J_MSG_INIT_FAIL,
                        "failed to find changelog translator");
                return ENOENT;
        }

        /* Find the actual changelog directory. */
        if (dict_get_str(cl_xl->options, "changelog-dir", cl_dir_p) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        J_MSG_INIT_FAIL,
                        "failed to find changelog-dir for %s", cl_xl->name);
                return ENODATA;
        }

        return 0;
}


void
jbr_get_terms (call_frame_t *frame, xlator_t *this)
{
        int32_t         op_errno;
        char            *cl_dir;
        DIR             *fp             = NULL;
        struct dirent   *rd_entry;
        struct dirent   *rd_result;
        int32_t         term_first      = -1;
        int32_t         term_contig     = -1;
        int32_t         term_last       = -1;
        int             term_num;
        char            *probe_str;
        dict_t          *my_xdata       = NULL;

        op_errno = jbr_get_changelog_dir(this, &cl_dir);
        if (op_errno) {
                goto err;       /* Error was already logged. */
        }
        op_errno = ENODATA;     /* Most common error after this. */

        rd_entry = alloca (offsetof(struct dirent, d_name) +
                           pathconf(cl_dir, _PC_NAME_MAX) + 1);
        if (!rd_entry) {
                goto err;
        }

        fp = sys_opendir (cl_dir);
        if (!fp) {
                op_errno = errno;
                goto err;
        }

        /* Find first and last terms. */
        for (;;) {
                if (readdir_r(fp, rd_entry, &rd_result) != 0) {
                        op_errno = errno;
                        goto err;
                }
                if (!rd_result) {
                        break;
                }
                if (fnmatch("TERM.*", rd_entry->d_name, FNM_PATHNAME) != 0) {
                        continue;
                }
                /* +5 points to the character after the period */
                term_num = atoi(rd_entry->d_name+5);
                gf_msg (this->name, GF_LOG_INFO, 0,
                        J_MSG_GENERIC,
                        "%s => %d", rd_entry->d_name, term_num);
                if (term_num < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                J_MSG_INVALID,
                                "invalid term file name %s", rd_entry->d_name);
                        op_errno = EINVAL;
                        goto err;
                }
                if ((term_first < 0) || (term_first > term_num)) {
                        term_first = term_num;
                }
                if ((term_last < 0) || (term_last < term_num)) {
                        term_last = term_num;
                }
        }
        if ((term_first < 0) || (term_last < 0)) {
                /* TBD: are we *sure* there should always be at least one? */
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        J_MSG_NO_DATA, "no terms found");
                op_errno = EINVAL;
                goto err;
        }

        sys_closedir (fp);
        fp = NULL;

        /*
         * Find term_contig, which is the earliest term for which there are
         * no gaps between it and term_last.
         */
        for (term_contig = term_last; term_contig > 0; --term_contig) {
                if (gf_asprintf(&probe_str, "%s/TERM.%d",
                                cl_dir, term_contig-1) <= 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                J_MSG_MEM_ERR,
                                "failed to format term %d", term_contig-1);
                        goto err;
                }
                if (sys_access(probe_str, F_OK) != 0) {
                        GF_FREE(probe_str);
                        break;
                }
                GF_FREE(probe_str);
        }

        gf_msg (this->name, GF_LOG_INFO, 0,
                J_MSG_GENERIC,
                "found terms %d-%d (%d)",
                term_first, term_last, term_contig);

        /* Return what we've found */
        my_xdata = dict_new();
        if (!my_xdata) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        J_MSG_MEM_ERR,
                        "failed to allocate reply dictionary");
                goto err;
        }
        if (dict_set_int32(my_xdata, "term-first", term_first) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        J_MSG_DICT_FLR,
                        "failed to set term-first");
                goto err;
        }
        if (dict_set_int32(my_xdata, "term-contig", term_contig) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        J_MSG_DICT_FLR,
                        "failed to set term-contig");
                goto err;
        }
        if (dict_set_int32(my_xdata, "term-last", term_last) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        J_MSG_DICT_FLR,
                        "failed to set term-last");
                goto err;
        }

        /* Finally! */
        STACK_UNWIND_STRICT (ipc, frame, 0, 0, my_xdata);
        dict_unref(my_xdata);
        return;

err:
        if (fp) {
                sys_closedir (fp);
        }
        if (my_xdata) {
                dict_unref(my_xdata);
        }
        STACK_UNWIND_STRICT (ipc, frame, -1, op_errno, NULL);
}


long
get_entry_count (xlator_t *this, int fd)
{
        struct stat     buf;
        long            min;            /* last entry not known to be empty */
        long            max;            /* first entry known to be empty */
        long            curr;
        char            entry[CHANGELOG_ENTRY_SIZE];

        if (sys_fstat (fd, &buf) < 0) {
                return -1;
        }

        min = 0;
        max = buf.st_size / CHANGELOG_ENTRY_SIZE;

        while ((min+1) < max) {
                curr = (min + max) / 2;
                if (sys_lseek(fd, curr*CHANGELOG_ENTRY_SIZE, SEEK_SET) < 0) {
                        return -1;
                }
                if (sys_read(fd, entry, sizeof(entry)) != sizeof(entry)) {
                        return -1;
                }
                if ((entry[0] == '_') && (entry[1] == 'P')) {
                        min = curr;
                } else {
                        max = curr;
                }
        }

        if (sys_lseek(fd, 0, SEEK_SET) < 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        J_MSG_SYS_CALL_FAILURE,
                        "failed to reset offset");
        }
        return max;
}


void
jbr_open_term (call_frame_t *frame, xlator_t *this, dict_t *xdata)
{
        int32_t         op_errno;
        char            *cl_dir;
        char            *term;
        char            *path;
        jbr_private_t   *priv           = this->private;

        op_errno = jbr_get_changelog_dir(this, &cl_dir);
        if (op_errno) {
                goto err;
        }

        if (dict_get_str(xdata, "term", &term) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        J_MSG_NO_DATA, "missing term");
                op_errno = ENODATA;
                goto err;
        }

        if (gf_asprintf(&path, "%s/TERM.%s", cl_dir, term) < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        J_MSG_MEM_ERR, "failed to construct path");
                op_errno = ENOMEM;
                goto err;
        }

        if (priv->term_fd >= 0) {
                sys_close (priv->term_fd);
        }
        priv->term_fd = open(path, O_RDONLY);
        if (priv->term_fd < 0) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        J_MSG_SYS_CALL_FAILURE,
                        "failed to open term file");
                goto err;
        }

        priv->term_total = get_entry_count(this, priv->term_fd);
        if (priv->term_total < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        J_MSG_NO_DATA, "failed to get entry count");
                sys_close (priv->term_fd);
                priv->term_fd = -1;
                op_errno = EIO;
                goto err;
        }
        priv->term_read = 0;

        /* Success! */
        STACK_UNWIND_STRICT (ipc, frame, 0, 0, NULL);
        return;

err:
        STACK_UNWIND_STRICT (ipc, frame, -1, op_errno, NULL);
}


void
jbr_next_entry (call_frame_t *frame, xlator_t *this)
{
        int32_t         op_errno        = ENOMEM;
        jbr_private_t   *priv           = this->private;
        ssize_t          nbytes;
        dict_t          *my_xdata;

        if (priv->term_fd < 0) {
                op_errno = EBADFD;
                goto err;
        }

        if (priv->term_read >= priv->term_total) {
                op_errno = ENODATA;
                goto err;
        }

        nbytes = sys_read (priv->term_fd, priv->term_buf, CHANGELOG_ENTRY_SIZE);
        if (nbytes < CHANGELOG_ENTRY_SIZE) {
                if (nbytes < 0) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                J_MSG_SYS_CALL_FAILURE,
                                "error reading next entry: %s",
                                strerror(errno));
                } else {
                        op_errno = EIO;
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                J_MSG_SYS_CALL_FAILURE,
                                "got %ld/%d bytes for next entry",
                                nbytes, CHANGELOG_ENTRY_SIZE);
                }
                goto err;
        }
        ++(priv->term_read);

        my_xdata = dict_new();
        if (!my_xdata) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        J_MSG_MEM_ERR, "failed to allocate reply xdata");
                goto err;
        }

        if (dict_set_static_bin(my_xdata, "data",
                                priv->term_buf, CHANGELOG_ENTRY_SIZE) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        J_MSG_DICT_FLR, "failed to assign reply xdata");
                goto err;
        }

        STACK_UNWIND_STRICT (ipc, frame, 0, 0, my_xdata);
        dict_unref(my_xdata);
        return;

err:
        STACK_UNWIND_STRICT (ipc, frame, -1, op_errno, NULL);
}


int32_t
jbr_ipc (call_frame_t *frame, xlator_t *this, int32_t op, dict_t *xdata)
{
        switch (op) {
        case JBR_SERVER_TERM_RANGE:
                jbr_get_terms(frame, this);
                break;
        case JBR_SERVER_OPEN_TERM:
                jbr_open_term(frame, this, xdata);
                break;
        case JBR_SERVER_NEXT_ENTRY:
                jbr_next_entry(frame, this);
                break;
        default:
                STACK_WIND_TAIL (frame,
                                 FIRST_CHILD(this),
                                 FIRST_CHILD(this)->fops->ipc,
                                 op, xdata);
        }

        return 0;
}


int32_t
jbr_forget (xlator_t *this, inode_t *inode)
{
        uint64_t        ctx     = 0LL;

        if ((inode_ctx_del(inode, this, &ctx) == 0) && ctx) {
                GF_FREE((void *)(long)ctx);
        }

        return 0;
}

int32_t
jbr_release (xlator_t *this, fd_t *fd)
{
        uint64_t        ctx     = 0LL;

        if ((fd_ctx_del(fd, this, &ctx) == 0) && ctx) {
                GF_FREE((void *)(long)ctx);
        }

        return 0;
}

struct xlator_cbks cbks = {
        .forget  = jbr_forget,
        .release = jbr_release,
};

int
jbr_reconfigure (xlator_t *this, dict_t *options)
{
        jbr_private_t   *priv   = this->private;

        GF_OPTION_RECONF ("leader",
                          priv->config_leader, options, bool, err);
        GF_OPTION_RECONF ("quorum-percent",
                          priv->quorum_pct, options, percent, err);
        gf_msg (this->name, GF_LOG_INFO, 0, J_MSG_GENERIC,
                "reconfigure called, config_leader = %d, quorum_pct = %.1f\n",
                priv->leader, priv->quorum_pct);

        priv->leader = priv->config_leader;

        return 0;

err:
        return -1;
}

int
jbr_get_child_index (xlator_t *this, xlator_t *kid)
{
        xlator_list_t   *trav;
        int             retval = -1;

        for (trav = this->children; trav; trav = trav->next) {
                ++retval;
                if (trav->xlator == kid) {
                        return retval;
                }
        }

        return -1;
}

/*
 * Child notify handling is unreasonably FUBAR.  Sometimes we'll get a
 * CHILD_DOWN for a protocol/client child before we ever got a CHILD_UP for it.
 * Other times we won't.  Because it's effectively random (probably racy), we
 * can't just maintain a count.  We actually have to keep track of the state
 * for each child separately, to filter out the bogus CHILD_DOWN events, and
 * then generate counts on demand.
 */
int
jbr_notify (xlator_t *this, int event, void *data, ...)
{
        jbr_private_t   *priv         = this->private;
        int             index         = -1;
        int             ret           = -1;
        gf_boolean_t    result        = _gf_false;
        gf_boolean_t    relevant      = _gf_false;

        switch (event) {
        case GF_EVENT_CHILD_UP:
                index = jbr_get_child_index(this, data);
                if (index >= 0) {
                        /* Check if the child was previously down
                         * and it's not a false CHILD_UP
                         */
                        if (!(priv->kid_state & (1 << index))) {
                                relevant = _gf_true;
                        }

                        priv->kid_state |= (1 << index);
                        priv->up_children = jbr_count_up_kids(priv);
                        gf_msg (this->name, GF_LOG_INFO, 0, J_MSG_GENERIC,
                                "got CHILD_UP for %s, now %u kids",
                                ((xlator_t *)data)->name,
                                priv->up_children);
                        if (!priv->config_leader && (priv->up_children > 1)) {
                                priv->leader = _gf_false;
                        }

                        /* If it's not relevant, or we have already *
                         * sent CHILD_UP just break */
                        if (!relevant || priv->child_up)
                                break;

                        /* If it's not a leader, just send the notify up */
                        if (!priv->leader) {
                                ret = default_notify(this, event, data);
                                if (!ret)
                                        priv->child_up = _gf_true;
                                break;
                        }

                        result = fop_quorum_check (this,
                                                (double)(priv->n_children - 1),
                                               (double)(priv->up_children - 1));
                        if (result == _gf_false) {
                                gf_msg (this->name, GF_LOG_INFO, 0,
                                        J_MSG_GENERIC, "Not enough children "
                                        "are up to meet quorum. Waiting to "
                                        "send CHILD_UP from leader");
                        } else {
                                gf_msg (this->name, GF_LOG_INFO, 0,
                                        J_MSG_GENERIC, "Enough children are up "
                                        "to meet quorum. Sending CHILD_UP "
                                        "from leader");
                                ret = default_notify(this, event, data);
                                if (!ret)
                                        priv->child_up = _gf_true;
                        }
                }
                break;
        case GF_EVENT_CHILD_DOWN:
                index = jbr_get_child_index(this, data);
                if (index >= 0) {
                        /* Check if the child was previously up
                         * and it's not a false CHILD_DOWN
                         */
                        if (priv->kid_state & (1 << index)) {
                                relevant = _gf_true;
                        }
                        priv->kid_state &= ~(1 << index);
                        priv->up_children = jbr_count_up_kids(priv);
                        gf_msg (this->name, GF_LOG_INFO, 0, J_MSG_GENERIC,
                                "got CHILD_DOWN for %s, now %u kids",
                                ((xlator_t *)data)->name,
                                priv->up_children);
                        if (!priv->config_leader && (priv->up_children < 2)
                            && relevant) {
                                priv->leader = _gf_true;
                        }

                        /* If it's not relevant, or we have already *
                         * sent CHILD_DOWN just break */
                        if (!relevant || !priv->child_up)
                                break;

                        /* If it's not a leader, just break coz we shouldn't  *
                         * propagate the failure from the failure till it     *
                         * itself goes down                                   *
                         */
                        if (!priv->leader) {
                                break;
                        }

                        result = fop_quorum_check (this,
                                           (double)(priv->n_children - 1),
                                           (double)(priv->up_children - 1));
                        if (result == _gf_false) {
                                gf_msg (this->name, GF_LOG_INFO, 0,
                                        J_MSG_GENERIC, "Enough children are "
                                        "to down to fail quorum. "
                                        "Sending CHILD_DOWN from leader");
                                ret = default_notify(this, event, data);
                                if (!ret)
                                        priv->child_up = _gf_false;
                        } else {
                                gf_msg (this->name, GF_LOG_INFO, 0,
                                        J_MSG_GENERIC, "Not enough children "
                                        "are down to fail quorum. Waiting to "
                                        "send CHILD_DOWN from leader");
                        }
                }
                break;
        default:
                ret = default_notify(this, event, data);
        }

        return ret;
}


int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        GF_VALIDATE_OR_GOTO ("jbr", this, out);

        ret = xlator_mem_acct_init (this, gf_mt_jbr_end + 1);

        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, J_MSG_MEM_ERR,
                        "Memory accounting init" "failed");
                return ret;
        }
out:
        return ret;
}


void
jbr_deallocate_priv (jbr_private_t *priv)
{
        if (!priv) {
                return;
        }

        GF_FREE(priv);
}


int32_t
jbr_init (xlator_t *this)
{
        xlator_list_t   *remote;
        xlator_list_t   *local;
        jbr_private_t   *priv           = NULL;
        xlator_list_t   *trav;
        pthread_t       kid;
        extern xlator_t global_xlator;
        glusterfs_ctx_t *oldctx         = global_xlator.ctx;

        /*
         * Any fop that gets special treatment has to be patched in here,
         * because the compiled-in table is produced by the code generator and
         * only contains generated functions.  Note that we have to go through
         * this->fops because of some dynamic-linking strangeness; modifying
         * the static table doesn't work.
         */
        this->fops->getxattr = jbr_getxattr_special;
        this->fops->fsync = jbr_fsync;
        this->fops->ipc = jbr_ipc;

        local = this->children;
        if (!local) {
                gf_msg (this->name, GF_LOG_ERROR, 0, J_MSG_NO_DATA,
                        "no local subvolume");
                goto err;
        }

        remote = local->next;
        if (!remote) {
                gf_msg (this->name, GF_LOG_ERROR, 0, J_MSG_NO_DATA,
                        "no remote subvolumes");
                goto err;
        }

        this->local_pool = mem_pool_new (jbr_local_t, 128);
        if (!this->local_pool) {
                gf_msg (this->name, GF_LOG_ERROR, 0, J_MSG_MEM_ERR,
                        "failed to create jbr_local_t pool");
                goto err;
        }

        priv = GF_CALLOC (1, sizeof(*priv), gf_mt_jbr_private_t);
        if (!priv) {
                gf_msg (this->name, GF_LOG_ERROR, 0, J_MSG_MEM_ERR,
                        "could not allocate priv");
                goto err;
        }

        for (trav = this->children; trav; trav = trav->next) {
                ++(priv->n_children);
        }

        LOCK_INIT(&priv->dirty_lock);
	LOCK_INIT(&priv->index_lock);
        INIT_LIST_HEAD(&priv->dirty_fds);
        priv->term_fd = -1;

        this->private = priv;

        GF_OPTION_INIT ("leader", priv->config_leader, bool, err);
        GF_OPTION_INIT ("quorum-percent", priv->quorum_pct, percent, err);

        priv->leader = priv->config_leader;
        priv->child_up = _gf_false;

        if (pthread_create(&kid, NULL, jbr_flush_thread,
                           this) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, J_MSG_SYS_CALL_FAILURE,
                        "could not start flush thread");
                /* TBD: treat this as a fatal error? */
        }

        /*
         * Calling glfs_new changes old->ctx, even if THIS still points
         * to global_xlator.  That causes problems later in the main
         * thread, when gf_log_dump_graph tries to use the FILE after
         * we've mucked with it and gets a segfault in __fprintf_chk.
         * We can avoid all that by undoing the damage before we
         * continue.
         */
        global_xlator.ctx = oldctx;

	return 0;

err:
        jbr_deallocate_priv(priv);
        return -1;
}


void
jbr_fini (xlator_t *this)
{
        jbr_deallocate_priv(this->private);
}

class_methods_t class_methods = {
        .init           = jbr_init,
        .fini           = jbr_fini,
        .reconfigure    = jbr_reconfigure,
        .notify         = jbr_notify,
};

struct volume_options options[] = {
        { .key = {"leader"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "false",
          .description = "Start in the leader role.  This is only for "
                         "bootstrapping the code, and should go away when we "
                         "have real leader election."
        },
        { .key = {"vol-name"},
          .type = GF_OPTION_TYPE_STR,
          .description = "volume name"
        },
        { .key = {"my-name"},
          .type = GF_OPTION_TYPE_STR,
          .description = "brick name in form of host:/path"
        },
        { .key = {"etcd-servers"},
          .type = GF_OPTION_TYPE_STR,
          .description = "list of comma separated etc servers"
        },
        { .key = {"subvol-uuid"},
          .type = GF_OPTION_TYPE_STR,
          .description = "UUID for this JBR (sub)volume"
        },
        { .key = {"quorum-percent"},
          .type = GF_OPTION_TYPE_PERCENT,
          .default_value = "50.0",
          .description = "percentage of rep_count-1 that must be up"
        },
	{ .key = {NULL} },
};
