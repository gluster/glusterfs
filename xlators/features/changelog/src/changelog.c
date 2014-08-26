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

#include "xlator.h"
#include "defaults.h"
#include "logging.h"
#include "iobuf.h"

#include "changelog-rt.h"
#include "changelog-helpers.h"

#include "changelog-encoders.h"
#include "changelog-mem-types.h"

#include <pthread.h>

#include "changelog-notifier.h"

static struct changelog_bootstrap
cb_bootstrap[] = {
        {
                .mode = CHANGELOG_MODE_RT,
                .ctor = changelog_rt_init,
                .dtor = changelog_rt_fini,
        },
};

/* Entry operations - TYPE III */

/**
 * entry operations do not undergo inode version checking.
 */

/* {{{ */

/* rmdir */

int32_t
changelog_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_ENTRY);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (rmdir, frame, op_ret, op_errno,
                                preparent, postparent, xdata);
        return 0;
}

int32_t
changelog_rmdir_resume (call_frame_t *frame, xlator_t *this,
                        loc_t *loc, int xflags, dict_t *xdata)
{
        changelog_priv_t *priv     = NULL;

        priv = this->private;

        gf_log (this->name, GF_LOG_DEBUG, "Dequeue rmdir");
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_rmdir_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->rmdir,
                    loc, xflags, xdata);
        return 0;
}

int32_t
changelog_rmdir (call_frame_t *frame, xlator_t *this,
                 loc_t *loc, int xflags, dict_t *xdata)
{
        size_t                  xtra_len                = 0;
        changelog_priv_t       *priv                    = NULL;
        changelog_opt_t        *co                      = NULL;
        call_stub_t            *stub                    = NULL;
        struct list_head        queue                   = {0, };
        gf_boolean_t            barrier_enabled         = _gf_false;

        INIT_LIST_HEAD (&queue);

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);

        CHANGELOG_INIT_NOCHECK (this, frame->local,
                                NULL, loc->inode->gfid, 2);

        co = changelog_get_usable_buffer (frame->local);
        if (!co)
                goto wind;

        CHANGLOG_FILL_FOP_NUMBER (co, frame->root->op, fop_fn, xtra_len);

        co++;
        CHANGELOG_FILL_ENTRY (co, loc->pargfid, loc->name,
                              entry_fn, entry_free_fn, xtra_len, wind);

        changelog_set_usable_record_and_length (frame->local, xtra_len, 2);

/* changelog barrier */
        /* Color assignment and increment of fop_cnt for rmdir/unlink/rename
         * should be made with in priv lock if changelog barrier is not enabled.
         * Because if counter is not incremented yet, draining wakes up and
         * publishes the changelog but later these fops might hit the disk and
         * present in snapped volume but where as the intention is these fops
         * should not be present in snapped volume.
         */
        LOCK (&priv->lock);
        {
                if ((barrier_enabled = priv->barrier_enabled)) {
                        stub = fop_rmdir_stub (frame, changelog_rmdir_resume,
                                                loc, xflags, xdata);
                        if (!stub)
                               __chlog_barrier_disable (this, &queue);
                        else
                               __chlog_barrier_enqueue (this, stub);
                } else {
                        ((changelog_local_t *)frame->local)->color
                                                          = priv->current_color;
                        changelog_inc_fop_cnt (this, priv, frame->local);
                }
        }
        UNLOCK (&priv->lock);

        if (barrier_enabled && stub) {
                gf_log (this->name, GF_LOG_DEBUG, "Enqueue rmdir");
                goto out;
        }
        if (barrier_enabled && !stub) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to barrier FOPs, disabling changelog barrier "
                        "FOP: rmdir, ERROR: %s", strerror (ENOMEM));
                chlog_barrier_dequeue_all (this, &queue);
        }

/* changelog barrier */

 wind:
        STACK_WIND (frame, changelog_rmdir_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->rmdir,
                    loc, xflags, xdata);
 out:
        return 0;
}

/* unlink */

int32_t
changelog_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                      struct iatt *postparent, dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_ENTRY);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (unlink, frame, op_ret, op_errno,
                                preparent, postparent, xdata);
        return 0;
}

int32_t
changelog_unlink_resume (call_frame_t *frame, xlator_t *this,
                         loc_t *loc, int xflags, dict_t *xdata)
{
        changelog_priv_t *priv     = NULL;

        priv = this->private;

        gf_log (this->name, GF_LOG_DEBUG, "Dequeue unlink");
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_unlink_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->unlink,
                    loc, xflags, xdata);
        return 0;
}

int32_t
changelog_unlink (call_frame_t *frame, xlator_t *this,
                  loc_t *loc, int xflags, dict_t *xdata)
{
        size_t                  xtra_len                = 0;
        changelog_priv_t       *priv                    = NULL;
        changelog_opt_t        *co                      = NULL;
        call_stub_t            *stub                    = NULL;
        struct list_head        queue                   = {0, };
        gf_boolean_t            barrier_enabled         = _gf_false;

        INIT_LIST_HEAD (&queue);

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);
        CHANGELOG_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, wind);

        CHANGELOG_INIT_NOCHECK (this, frame->local, NULL, loc->inode->gfid, 2);

        co = changelog_get_usable_buffer (frame->local);
        if (!co)
                goto wind;

        CHANGLOG_FILL_FOP_NUMBER (co, frame->root->op, fop_fn, xtra_len);

        co++;
        CHANGELOG_FILL_ENTRY (co, loc->pargfid, loc->name,
                              entry_fn, entry_free_fn, xtra_len, wind);

        changelog_set_usable_record_and_length (frame->local, xtra_len, 2);

/* changelog barrier */
        LOCK (&priv->lock);
        {
                if ((barrier_enabled = priv->barrier_enabled)) {
                        stub = fop_unlink_stub (frame, changelog_unlink_resume,
                                                loc, xflags, xdata);
                        if (!stub)
                               __chlog_barrier_disable (this, &queue);
                        else
                               __chlog_barrier_enqueue (this, stub);
                } else {
                        ((changelog_local_t *)frame->local)->color
                                                          = priv->current_color;
                        changelog_inc_fop_cnt (this, priv, frame->local);
                }
        }
        UNLOCK (&priv->lock);

        if (barrier_enabled && stub) {
                gf_log (this->name, GF_LOG_DEBUG, "Enqueue unlink");
                goto out;
        }
        if (barrier_enabled && !stub) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to barrier FOPs, disabling changelog barrier "
                        "FOP: unlink, ERROR: %s", strerror (ENOMEM));
                chlog_barrier_dequeue_all (this, &queue);
        }

/* changelog barrier */

 wind:
        STACK_WIND (frame, changelog_unlink_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->unlink,
                    loc, xflags, xdata);
 out:
        return 0;
}

/* rename */

int32_t
changelog_rename_cbk (call_frame_t *frame,
                      void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      struct iatt *buf, struct iatt *preoldparent,
                      struct iatt *postoldparent, struct iatt *prenewparent,
                      struct iatt *postnewparent, dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_ENTRY);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (rename, frame, op_ret, op_errno,
                                buf, preoldparent, postoldparent,
                                prenewparent, postnewparent, xdata);
        return 0;
}


int32_t
changelog_rename_resume (call_frame_t *frame, xlator_t *this,
                         loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        changelog_priv_t *priv     = NULL;

        priv = this->private;

        gf_log (this->name, GF_LOG_DEBUG, "Dequeue rename");
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_rename_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->rename,
                    oldloc, newloc, xdata);
        return 0;
}

int32_t
changelog_rename (call_frame_t *frame, xlator_t *this,
                  loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        size_t                  xtra_len                = 0;
        changelog_priv_t       *priv                    = NULL;
        changelog_opt_t        *co                      = NULL;
        call_stub_t            *stub                    = NULL;
        struct list_head        queue                   = {0, };
        gf_boolean_t            barrier_enabled         = _gf_false;

        INIT_LIST_HEAD (&queue);

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);

        /* 3 == fop + oldloc + newloc */
        CHANGELOG_INIT_NOCHECK (this, frame->local,
                                NULL, oldloc->inode->gfid, 3);

        co = changelog_get_usable_buffer (frame->local);
        if (!co)
                goto wind;

        CHANGLOG_FILL_FOP_NUMBER (co, frame->root->op, fop_fn, xtra_len);

        co++;
        CHANGELOG_FILL_ENTRY (co, oldloc->pargfid, oldloc->name,
                              entry_fn, entry_free_fn, xtra_len, wind);

        co++;
        CHANGELOG_FILL_ENTRY (co, newloc->pargfid, newloc->name,
                              entry_fn, entry_free_fn, xtra_len, wind);

        changelog_set_usable_record_and_length (frame->local, xtra_len, 3);
/* changelog barrier */
        LOCK (&priv->lock);
        {
                if ((barrier_enabled = priv->barrier_enabled)) {
                        stub = fop_rename_stub (frame, changelog_rename_resume,
                                                oldloc, newloc, xdata);
                        if (!stub)
                               __chlog_barrier_disable (this, &queue);
                        else
                               __chlog_barrier_enqueue (this, stub);
                } else {
                        ((changelog_local_t *)frame->local)->color
                                                          = priv->current_color;
                        changelog_inc_fop_cnt (this, priv, frame->local);
                }
        }
        UNLOCK (&priv->lock);

        if (barrier_enabled && stub) {
                gf_log (this->name, GF_LOG_DEBUG, "Enqueue rename");
                goto out;
        }
        if (barrier_enabled && !stub) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to barrier FOPs, disabling changelog barrier "
                        "FOP: rename, ERROR: %s", strerror (ENOMEM));
                chlog_barrier_dequeue_all (this, &queue);
        }
/* changelog barrier */

 wind:
        STACK_WIND (frame, changelog_rename_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->rename,
                    oldloc, newloc, xdata);
 out:
        return 0;
}

/* link */

int32_t
changelog_link_cbk (call_frame_t *frame,
                    void *cookie, xlator_t *this, int32_t op_ret,
                    int32_t op_errno, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_ENTRY);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (link, frame, op_ret, op_errno,
                                inode, buf, preparent, postparent, xdata);
        return 0;
}

int32_t
changelog_link_resume (call_frame_t *frame, xlator_t *this,
                        loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        changelog_priv_t *priv     = NULL;

        GF_VALIDATE_OR_GOTO ("changelog", this, out);
        GF_VALIDATE_OR_GOTO ("changelog", this->fops, out);
        GF_VALIDATE_OR_GOTO ("changelog", frame, out);

        priv = this->private;

        gf_log (this->name, GF_LOG_DEBUG, "Dequeuing link");
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_link_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->link,
                    oldloc, newloc, xdata);
        return 0;
out:
        return -1;
}
int32_t
changelog_link (call_frame_t *frame,
                xlator_t *this, loc_t *oldloc,
                loc_t *newloc, dict_t *xdata)
{
        size_t            xtra_len         = 0;
        changelog_priv_t *priv             = NULL;
        changelog_opt_t  *co               = NULL;
        call_stub_t      *stub             = NULL;
        struct list_head queue             = {0, };
        gf_boolean_t     barrier_enabled   = _gf_false;

        priv = this->private;

        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);
        CHANGELOG_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, wind);

        CHANGELOG_INIT_NOCHECK (this, frame->local, NULL, oldloc->gfid, 2);

        co = changelog_get_usable_buffer (frame->local);
        if (!co)
                goto wind;

        CHANGLOG_FILL_FOP_NUMBER (co, frame->root->op, fop_fn, xtra_len);

        co++;
        CHANGELOG_FILL_ENTRY (co, newloc->pargfid, newloc->name,
                              entry_fn, entry_free_fn, xtra_len, wind);

        changelog_set_usable_record_and_length (frame->local, xtra_len, 2);

        LOCK (&priv->lock);
        {
                if ((barrier_enabled = priv->barrier_enabled)) {
                        stub = fop_link_stub (frame, changelog_link_resume,
                                               oldloc, newloc, xdata);
                        if (!stub)
                               __chlog_barrier_disable (this, &queue);
                        else
                               __chlog_barrier_enqueue (this, stub);
                } else {
                        ((changelog_local_t *)frame->local)->color
                                                          = priv->current_color;
                        changelog_inc_fop_cnt (this, priv, frame->local);
                }
        }
        UNLOCK (&priv->lock);

        if (barrier_enabled && stub) {
                gf_log (this->name, GF_LOG_DEBUG, "Enqueued link");
                goto out;
        }

        if (barrier_enabled && !stub) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to barrier FOPs, disabling changelog barrier "
                        "FOP: link, ERROR: %s", strerror (ENOMEM));
                chlog_barrier_dequeue_all (this, &queue);
        }
 wind:
        STACK_WIND (frame, changelog_link_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->link,
                    oldloc, newloc, xdata);
out:
        return 0;
}

/* mkdir */

int32_t
changelog_mkdir_cbk (call_frame_t *frame,
                     void *cookie, xlator_t *this, int32_t op_ret,
                     int32_t op_errno, inode_t *inode,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_ENTRY);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (mkdir, frame, op_ret, op_errno,
                                inode, buf, preparent, postparent, xdata);
        return 0;
}

int32_t
changelog_mkdir_resume (call_frame_t *frame, xlator_t *this,
                        loc_t *loc, mode_t mode,
                        mode_t umask, dict_t *xdata)
{
        changelog_priv_t *priv     = NULL;

        GF_VALIDATE_OR_GOTO ("changelog", this, out);
        GF_VALIDATE_OR_GOTO ("changelog", this->fops, out);
        GF_VALIDATE_OR_GOTO ("changelog", frame, out);

        priv = this->private;

        gf_log (this->name, GF_LOG_DEBUG, "Dequeuing mkdir");
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_mkdir_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->mkdir,
                    loc, mode, umask, xdata);
        return 0;
out:
        return -1;
}

int32_t
changelog_mkdir (call_frame_t *frame, xlator_t *this,
                 loc_t *loc, mode_t mode, mode_t umask, dict_t *xdata)
{
        int               ret              = -1;
        uuid_t            gfid             = {0,};
        void             *uuid_req         = NULL;
        size_t            xtra_len         = 0;
        changelog_priv_t *priv             = NULL;
        changelog_opt_t  *co               = NULL;
        call_stub_t      *stub             = NULL;
        struct list_head queue             = {0, };
        gf_boolean_t     barrier_enabled   = _gf_false;

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);

        ret = dict_get_ptr (xdata, "gfid-req", &uuid_req);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get gfid from dict");
                goto wind;
        }
        uuid_copy (gfid, uuid_req);

        CHANGELOG_INIT_NOCHECK (this, frame->local, NULL, gfid, 5);

        co = changelog_get_usable_buffer (frame->local);
        if (!co)
                goto wind;

        CHANGLOG_FILL_FOP_NUMBER (co, frame->root->op, fop_fn, xtra_len);
        co++;

        CHANGELOG_FILL_UINT32 (co, S_IFDIR | mode, number_fn, xtra_len);
        co++;

        CHANGELOG_FILL_UINT32 (co, frame->root->uid, number_fn, xtra_len);
        co++;

        CHANGELOG_FILL_UINT32 (co, frame->root->gid, number_fn, xtra_len);
        co++;

        CHANGELOG_FILL_ENTRY (co, loc->pargfid, loc->name,
                              entry_fn, entry_free_fn, xtra_len, wind);

        changelog_set_usable_record_and_length (frame->local, xtra_len, 5);

        LOCK (&priv->lock);
        {
                if ((barrier_enabled = priv->barrier_enabled)) {
                        stub = fop_mkdir_stub (frame, changelog_mkdir_resume,
                                               loc, mode, umask, xdata);
                        if (!stub)
                               __chlog_barrier_disable (this, &queue);
                        else
                               __chlog_barrier_enqueue (this, stub);
                } else {
                        ((changelog_local_t *)frame->local)->color
                                                          = priv->current_color;
                        changelog_inc_fop_cnt (this, priv, frame->local);
                }
        }
        UNLOCK (&priv->lock);

        if (barrier_enabled && stub) {
                gf_log (this->name, GF_LOG_DEBUG, "Enqueued mkdir");
                goto out;
        }

        if (barrier_enabled && !stub) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to barrier FOPs, disabling changelog barrier "
                        "FOP: mkdir, ERROR: %s", strerror (ENOMEM));
                chlog_barrier_dequeue_all (this, &queue);
        }

 wind:
        STACK_WIND (frame, changelog_mkdir_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->mkdir,
                    loc, mode, umask, xdata);
out:
        return 0;
}

/* symlink */

int32_t
changelog_symlink_cbk (call_frame_t *frame,
                       void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno,
                       inode_t *inode, struct iatt *buf, struct iatt *preparent,
                       struct iatt *postparent, dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_ENTRY);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (symlink, frame, op_ret, op_errno,
                                inode, buf, preparent, postparent, xdata);
        return 0;
}


int32_t
changelog_symlink_resume (call_frame_t *frame, xlator_t *this,
                          const char *linkname, loc_t *loc,
                          mode_t umask, dict_t *xdata)
{
        changelog_priv_t *priv     = NULL;

        GF_VALIDATE_OR_GOTO ("changelog", this, out);
        GF_VALIDATE_OR_GOTO ("changelog", this->fops, out);
        GF_VALIDATE_OR_GOTO ("changelog", frame, out);

        priv = this->private;

        gf_log (this->name, GF_LOG_DEBUG, "Dequeuing symlink");
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_symlink_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->symlink,
                    linkname, loc, umask, xdata);
        return 0;
out:
        return -1;
}

int32_t
changelog_symlink (call_frame_t *frame, xlator_t *this,
                   const char *linkname, loc_t *loc,
                   mode_t umask, dict_t *xdata)
{
        int               ret              = -1;
        size_t            xtra_len         = 0;
        uuid_t            gfid             = {0,};
        void             *uuid_req         = NULL;
        changelog_priv_t *priv             = NULL;
        changelog_opt_t  *co               = NULL;
        call_stub_t      *stub             = NULL;
        struct list_head queue             = {0, };
        gf_boolean_t     barrier_enabled   = _gf_false;

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);

        ret = dict_get_ptr (xdata, "gfid-req", &uuid_req);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get gfid from dict");
                goto wind;
        }
        uuid_copy (gfid, uuid_req);

        CHANGELOG_INIT_NOCHECK (this, frame->local, NULL, gfid, 2);

        co = changelog_get_usable_buffer (frame->local);
        if (!co)
                goto wind;

        CHANGLOG_FILL_FOP_NUMBER (co, frame->root->op, fop_fn, xtra_len);
        co++;

        CHANGELOG_FILL_ENTRY (co, loc->pargfid, loc->name,
                              entry_fn, entry_free_fn, xtra_len, wind);

        changelog_set_usable_record_and_length (frame->local, xtra_len, 2);

        LOCK (&priv->lock);
        {
                if ((barrier_enabled = priv->barrier_enabled)) {
                        stub = fop_symlink_stub (frame,
                                                 changelog_symlink_resume,
                                                 linkname, loc, umask, xdata);
                        if (!stub)
                               __chlog_barrier_disable (this, &queue);
                        else
                               __chlog_barrier_enqueue (this, stub);
                } else {
                        ((changelog_local_t *)frame->local)->color
                                                          = priv->current_color;
                        changelog_inc_fop_cnt (this, priv, frame->local);
                }
        }
        UNLOCK (&priv->lock);

        if (barrier_enabled && stub) {
                gf_log (this->name, GF_LOG_DEBUG, "Enqueued symlink");
                goto out;
        }

        if (barrier_enabled && !stub) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to barrier FOPs, disabling changelog barrier "
                        "FOP: symlink, ERROR: %s", strerror (ENOMEM));
                chlog_barrier_dequeue_all (this, &queue);
        }

 wind:
        STACK_WIND (frame, changelog_symlink_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->symlink,
                    linkname, loc, umask, xdata);
out:
        return 0;
}

/* mknod */

int32_t
changelog_mknod_cbk (call_frame_t *frame,
                     void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, inode_t *inode,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent, dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_ENTRY);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (mknod, frame, op_ret, op_errno,
                                inode, buf, preparent, postparent, xdata);
        return 0;
}

int32_t
changelog_mknod_resume (call_frame_t *frame, xlator_t *this,
                        loc_t *loc, mode_t mode, dev_t rdev,
                        mode_t umask, dict_t *xdata)
{
        changelog_priv_t *priv     = NULL;

        GF_VALIDATE_OR_GOTO ("changelog", this, out);
        GF_VALIDATE_OR_GOTO ("changelog", this->fops, out);
        GF_VALIDATE_OR_GOTO ("changelog", frame, out);

        priv = this->private;

        gf_log (this->name, GF_LOG_DEBUG, "Dequeuing mknod");
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_mknod_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->mknod,
                    loc, mode, rdev, umask, xdata);
        return 0;
out:
        return -1;
}

int32_t
changelog_mknod (call_frame_t *frame,
                 xlator_t *this, loc_t *loc,
                 mode_t mode, dev_t dev, mode_t umask, dict_t *xdata)
{
        int               ret              = -1;
        uuid_t            gfid             = {0,};
        void             *uuid_req         = NULL;
        size_t            xtra_len         = 0;
        changelog_priv_t *priv             = NULL;
        changelog_opt_t  *co               = NULL;
        call_stub_t      *stub             = NULL;
        struct list_head queue             = {0, };
        gf_boolean_t     barrier_enabled   = _gf_false;

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);
        CHANGELOG_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, wind);

        ret = dict_get_ptr (xdata, "gfid-req", &uuid_req);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get gfid from dict");
                goto wind;
        }
        uuid_copy (gfid, uuid_req);

        CHANGELOG_INIT_NOCHECK (this, frame->local, NULL, gfid, 5);

        co = changelog_get_usable_buffer (frame->local);
        if (!co)
                goto wind;

        CHANGLOG_FILL_FOP_NUMBER (co, frame->root->op, fop_fn, xtra_len);
        co++;

        CHANGELOG_FILL_UINT32 (co, mode, number_fn, xtra_len);
        co++;

        CHANGELOG_FILL_UINT32 (co, frame->root->uid, number_fn, xtra_len);
        co++;

        CHANGELOG_FILL_UINT32 (co, frame->root->gid, number_fn, xtra_len);
        co++;

        CHANGELOG_FILL_ENTRY (co, loc->pargfid, loc->name,
                              entry_fn, entry_free_fn, xtra_len, wind);

        changelog_set_usable_record_and_length (frame->local, xtra_len, 5);

        LOCK (&priv->lock);
        {
                if ((barrier_enabled = priv->barrier_enabled)) {
                        stub = fop_mknod_stub (frame, changelog_mknod_resume,
                                               loc, mode, dev, umask, xdata);
                        if (!stub)
                               __chlog_barrier_disable (this, &queue);
                        else
                               __chlog_barrier_enqueue (this, stub);
                } else {
                        ((changelog_local_t *)frame->local)->color
                                                          = priv->current_color;
                        changelog_inc_fop_cnt (this, priv, frame->local);
                }
        }
        UNLOCK (&priv->lock);

        if (barrier_enabled && stub) {
                gf_log (this->name, GF_LOG_DEBUG, "Enqueued mknod");
                goto out;
        }

        if (barrier_enabled && !stub) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to barrier FOPs, disabling changelog barrier "
                        "FOP: mknod, ERROR: %s", strerror (ENOMEM));
                chlog_barrier_dequeue_all (this, &queue);
        }

 wind:
        STACK_WIND (frame, changelog_mknod_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->mknod,
                    loc, mode, dev, umask, xdata);
out:
        return 0;
}

/* creat */

int32_t
changelog_create_cbk (call_frame_t *frame,
                      void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      fd_t *fd, inode_t *inode, struct iatt *buf,
                      struct iatt *preparent,
                      struct iatt *postparent, dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_ENTRY);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (create, frame,
                                op_ret, op_errno, fd, inode,
                                buf, preparent, postparent, xdata);
        return 0;
}

int32_t
changelog_create_resume (call_frame_t *frame, xlator_t *this,
                         loc_t *loc, int32_t flags, mode_t mode,
                         mode_t umask, fd_t *fd, dict_t *xdata)
{
        changelog_priv_t *priv     = NULL;

        GF_VALIDATE_OR_GOTO ("changelog", this, out);
        GF_VALIDATE_OR_GOTO ("changelog", this->fops, out);
        GF_VALIDATE_OR_GOTO ("changelog", frame, out);

        priv = this->private;

        gf_log (this->name, GF_LOG_DEBUG, "Dequeuing create");
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_create_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->create,
                    loc, flags, mode, umask, fd, xdata);
        return 0;

out:
        return -1;
}

int32_t
changelog_create (call_frame_t *frame, xlator_t *this,
                  loc_t *loc, int32_t flags, mode_t mode,
                  mode_t umask, fd_t *fd, dict_t *xdata)
{
        int               ret              = -1;
        uuid_t            gfid             = {0,};
        void             *uuid_req         = NULL;
        changelog_opt_t  *co               = NULL;
        changelog_priv_t *priv             = NULL;
        size_t            xtra_len         = 0;
        call_stub_t      *stub             = NULL;
        struct list_head queue             = {0, };
        gf_boolean_t     barrier_enabled   = _gf_false;

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);

        ret = dict_get_ptr (xdata, "gfid-req", &uuid_req);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to get gfid from dict");
                goto wind;
        }
        uuid_copy (gfid, uuid_req);

        /* init with two extra records */
        CHANGELOG_INIT_NOCHECK (this, frame->local, NULL, gfid, 5);
        if (!frame->local)
                goto wind;

        co = changelog_get_usable_buffer (frame->local);
        if (!co)
                goto wind;

        CHANGLOG_FILL_FOP_NUMBER (co, frame->root->op, fop_fn, xtra_len);
        co++;

        CHANGELOG_FILL_UINT32 (co, mode, number_fn, xtra_len);
        co++;

        CHANGELOG_FILL_UINT32 (co, frame->root->uid, number_fn, xtra_len);
        co++;

        CHANGELOG_FILL_UINT32 (co, frame->root->gid, number_fn, xtra_len);
        co++;

        CHANGELOG_FILL_ENTRY (co, loc->pargfid, loc->name,
                              entry_fn, entry_free_fn, xtra_len, wind);

        changelog_set_usable_record_and_length (frame->local, xtra_len, 5);

        LOCK (&priv->lock);
        {
                if ((barrier_enabled = priv->barrier_enabled)) {
                        stub = fop_create_stub (frame, changelog_create_resume,
                                               loc, flags, mode, umask, fd,
                                               xdata);
                        if (!stub)
                               __chlog_barrier_disable (this, &queue);
                        else
                               __chlog_barrier_enqueue (this, stub);
                } else {
                        ((changelog_local_t *)frame->local)->color
                                                          = priv->current_color;
                        changelog_inc_fop_cnt (this, priv, frame->local);
                }
        }
        UNLOCK (&priv->lock);

        if (barrier_enabled && stub) {
                gf_log (this->name, GF_LOG_DEBUG, "Enqueued create");
                goto out;
        }

        if (barrier_enabled && !stub) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to barrier FOPs, disabling changelog barrier "
                        "FOP: create, ERROR: %s", strerror (ENOMEM));
                chlog_barrier_dequeue_all (this, &queue);
        }

 wind:
        STACK_WIND (frame, changelog_create_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->create,
                    loc, flags, mode, umask, fd, xdata);
out:
        return 0;
}

/* }}} */


/* Metadata modification fops - TYPE II */

/* {{{ */

/* {f}setattr */

int32_t
changelog_fsetattr_cbk (call_frame_t *frame,
                        void *cookie, xlator_t *this, int32_t op_ret,
                        int32_t op_errno, struct iatt *preop_stbuf,
                        struct iatt *postop_stbuf, dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_METADATA);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (fsetattr, frame, op_ret, op_errno,
                                preop_stbuf, postop_stbuf, xdata);

        return 0;


}

int32_t
changelog_fsetattr (call_frame_t *frame,
                    xlator_t *this, fd_t *fd,
                    struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        changelog_priv_t *priv     = NULL;
        changelog_opt_t  *co       = NULL;
        size_t            xtra_len = 0;

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);

        CHANGELOG_INIT (this, frame->local,
                        fd->inode, fd->inode->gfid, 1);
        if (!frame->local)
                goto wind;

        co = changelog_get_usable_buffer (frame->local);
        if (!co)
                goto wind;

        CHANGLOG_FILL_FOP_NUMBER (co, frame->root->op, fop_fn, xtra_len);

        changelog_set_usable_record_and_length (frame->local, xtra_len, 1);

 wind:
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_fsetattr_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->fsetattr,
                    fd, stbuf, valid, xdata);
        return 0;


}

int32_t
changelog_setattr_cbk (call_frame_t *frame,
                       void *cookie, xlator_t *this, int32_t op_ret,
                       int32_t op_errno, struct iatt *preop_stbuf,
                       struct iatt *postop_stbuf, dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_METADATA);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (setattr, frame, op_ret, op_errno,
                                preop_stbuf, postop_stbuf, xdata);

        return 0;
}

int32_t
changelog_setattr (call_frame_t *frame,
                   xlator_t *this, loc_t *loc,
                   struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        changelog_priv_t *priv     = NULL;
        changelog_opt_t  *co       = NULL;
        size_t            xtra_len = 0;

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);

        CHANGELOG_INIT (this, frame->local,
                        loc->inode, loc->inode->gfid, 1);
        if (!frame->local)
                goto wind;

        co = changelog_get_usable_buffer (frame->local);
        if (!co)
                goto wind;

        CHANGLOG_FILL_FOP_NUMBER (co, frame->root->op, fop_fn, xtra_len);

        changelog_set_usable_record_and_length (frame->local, xtra_len, 1);

 wind:
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_setattr_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->setattr,
                    loc, stbuf, valid, xdata);
        return 0;
}

/* {f}removexattr */

int32_t
changelog_fremovexattr_cbk (call_frame_t *frame,
                            void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_METADATA);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (fremovexattr, frame, op_ret, op_errno, xdata);

        return 0;
}

int32_t
changelog_fremovexattr (call_frame_t *frame, xlator_t *this,
                        fd_t *fd, const char *name, dict_t *xdata)
{
        changelog_priv_t *priv     = NULL;
        changelog_opt_t  *co       = NULL;
        size_t            xtra_len = 0;

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);

        CHANGELOG_INIT (this, frame->local,
                        fd->inode, fd->inode->gfid, 1);

        co = changelog_get_usable_buffer (frame->local);
        if (!co)
                goto wind;

        CHANGLOG_FILL_FOP_NUMBER (co, frame->root->op, fop_fn, xtra_len);

        changelog_set_usable_record_and_length (frame->local, xtra_len, 1);

 wind:
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_fremovexattr_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->fremovexattr,
                    fd, name, xdata);
        return 0;
}

int32_t
changelog_removexattr_cbk (call_frame_t *frame,
                           void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_METADATA);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (removexattr, frame, op_ret, op_errno, xdata);

        return 0;
}

int32_t
changelog_removexattr (call_frame_t *frame, xlator_t *this,
                       loc_t *loc, const char *name, dict_t *xdata)
{
        changelog_priv_t *priv     = NULL;
        changelog_opt_t  *co       = NULL;
        size_t            xtra_len = 0;

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);

        CHANGELOG_INIT (this, frame->local,
                        loc->inode, loc->inode->gfid, 1);

        co = changelog_get_usable_buffer (frame->local);
        if (!co)
                goto wind;

        CHANGLOG_FILL_FOP_NUMBER (co, frame->root->op, fop_fn, xtra_len);

        changelog_set_usable_record_and_length (frame->local, xtra_len, 1);

 wind:
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_removexattr_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->removexattr,
                    loc, name, xdata);
        return 0;
}

/* {f}setxattr */

int32_t
changelog_setxattr_cbk (call_frame_t *frame,
                        void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_METADATA);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (setxattr, frame, op_ret, op_errno, xdata);

        return 0;
}

int32_t
changelog_setxattr (call_frame_t *frame,
                    xlator_t *this, loc_t *loc,
                    dict_t *dict, int32_t flags, dict_t *xdata)
{
        changelog_priv_t *priv     = NULL;
        changelog_opt_t  *co       = NULL;
        size_t            xtra_len = 0;

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);

        CHANGELOG_INIT (this, frame->local,
                        loc->inode, loc->inode->gfid, 1);

        co = changelog_get_usable_buffer (frame->local);
        if (!co)
                goto wind;

        CHANGLOG_FILL_FOP_NUMBER (co, frame->root->op, fop_fn, xtra_len);

        changelog_set_usable_record_and_length (frame->local, xtra_len, 1);

 wind:
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_setxattr_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->setxattr,
                    loc, dict, flags, xdata);
        return 0;
}

int32_t
changelog_fsetxattr_cbk (call_frame_t *frame,
                         void *cookie, xlator_t *this, int32_t op_ret,
                         int32_t op_errno, dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_METADATA);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (fsetxattr, frame, op_ret, op_errno, xdata);

        return 0;
}

int32_t
changelog_fsetxattr (call_frame_t *frame,
                     xlator_t *this, fd_t *fd, dict_t *dict,
                     int32_t flags, dict_t *xdata)
{
        changelog_priv_t *priv     = NULL;
        changelog_opt_t  *co       = NULL;
        size_t            xtra_len = 0;

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);

        CHANGELOG_INIT (this, frame->local,
                        fd->inode, fd->inode->gfid, 1);

        co = changelog_get_usable_buffer (frame->local);
        if (!co)
                goto wind;

        CHANGLOG_FILL_FOP_NUMBER (co, frame->root->op, fop_fn, xtra_len);

        changelog_set_usable_record_and_length (frame->local, xtra_len, 1);

 wind:
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_fsetxattr_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->fsetxattr,
                    fd, dict, flags, xdata);
        return 0;
}

/* }}} */


/* Data modification fops - TYPE I */

/* {{{ */

/* {f}truncate() */

int32_t
changelog_truncate_cbk (call_frame_t *frame,
                        void *cookie, xlator_t *this, int32_t op_ret,
                        int32_t op_errno, struct iatt *prebuf,
                        struct iatt *postbuf, dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_DATA);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (truncate, frame,
                                op_ret, op_errno, prebuf, postbuf, xdata);
        return 0;
}

int32_t
changelog_truncate (call_frame_t *frame,
                    xlator_t *this, loc_t *loc, off_t offset, dict_t *xdata)
{
        changelog_priv_t *priv = NULL;

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);

        CHANGELOG_INIT (this, frame->local,
                        loc->inode, loc->inode->gfid, 0);
        LOCK(&priv->c_snap_lock);
        {
                if (priv->c_snap_fd != -1 &&
                    priv->barrier_enabled == _gf_true) {
                        changelog_snap_handle_ascii_change (this,
                              &( ((changelog_local_t *)(frame->local))->cld));
                }
        }
        UNLOCK(&priv->c_snap_lock);


 wind:
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_truncate_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->truncate,
                    loc, offset, xdata);
        return 0;
}

int32_t
changelog_ftruncate_cbk (call_frame_t *frame,
                         void *cookie, xlator_t *this, int32_t op_ret,
                         int32_t op_errno, struct iatt *prebuf,
                         struct iatt *postbuf, dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_DATA);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (ftruncate, frame,
                                op_ret, op_errno, prebuf, postbuf, xdata);
        return 0;
}

int32_t
changelog_ftruncate (call_frame_t *frame,
                     xlator_t *this, fd_t *fd, off_t offset, dict_t *xdata)
{
        changelog_priv_t *priv = NULL;

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);

        CHANGELOG_INIT (this, frame->local,
                        fd->inode, fd->inode->gfid, 0);
        LOCK(&priv->c_snap_lock);
        {
                if (priv->c_snap_fd != -1 &&
                    priv->barrier_enabled == _gf_true) {
                        changelog_snap_handle_ascii_change (this,
                              &( ((changelog_local_t *)(frame->local))->cld));
                }
        }
        UNLOCK(&priv->c_snap_lock);

 wind:
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_ftruncate_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->ftruncate,
                    fd, offset, xdata);
        return 0;
}

/* writev() */

int32_t
changelog_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                    struct iatt *postbuf,
                    dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret <= 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_DATA);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (writev, frame,
                                op_ret, op_errno, prebuf, postbuf, xdata);
        return 0;
}

int32_t
changelog_writev (call_frame_t *frame,
                  xlator_t *this, fd_t *fd, struct iovec *vector,
                  int32_t count, off_t offset, uint32_t flags,
                  struct iobref *iobref, dict_t *xdata)
{
        changelog_priv_t *priv = NULL;

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);

        CHANGELOG_INIT (this, frame->local,
                        fd->inode, fd->inode->gfid, 0);
        LOCK(&priv->c_snap_lock);
        {
                if (priv->c_snap_fd != -1 &&
                    priv->barrier_enabled == _gf_true) {
                        changelog_snap_handle_ascii_change (this,
                              &( ((changelog_local_t *)(frame->local))->cld));
                }
        }
        UNLOCK(&priv->c_snap_lock);

 wind:
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_writev_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->writev, fd, vector,
                    count, offset, flags, iobref, xdata);
        return 0;
}

/* }}} */

/**
 * The
 *   - @init ()
 *   - @fini ()
 *   - @reconfigure ()
 *   ... and helper routines
 */

/**
 * needed if there are more operation modes in the future.
 */
static void
changelog_assign_opmode (changelog_priv_t *priv, char *mode)
{
        if ( strncmp (mode, "realtime", 8) == 0 ) {
                priv->op_mode = CHANGELOG_MODE_RT;
        }
}

static void
changelog_assign_encoding (changelog_priv_t *priv, char *enc)
{
        if ( strncmp (enc, "binary", 6) == 0 ) {
                priv->encode_mode = CHANGELOG_ENCODE_BINARY;
        } else if ( strncmp (enc, "ascii", 5) == 0 ) {
                priv->encode_mode = CHANGELOG_ENCODE_ASCII;
        }
}

static void
changelog_assign_barrier_timeout(changelog_priv_t *priv, uint32_t timeout)
{
       LOCK (&priv->lock);
       {
               priv->timeout.tv_sec = timeout;
       }
       UNLOCK (&priv->lock);
}

/* cleanup any helper threads that are running */
static void
changelog_cleanup_helper_threads (xlator_t *this, changelog_priv_t *priv)
{
        int ret = 0;

        if (priv->cr.rollover_th) {
                changelog_thread_cleanup (this, priv->cr.rollover_th);
                priv->cr.rollover_th = 0;
                ret = close (priv->cr_wfd);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR,
                                "error closing write end of rollover pipe"
                                " (reason: %s)", strerror (errno));
        }

        if (priv->cf.fsync_th) {
                changelog_thread_cleanup (this, priv->cf.fsync_th);
                priv->cf.fsync_th = 0;
        }
}

/* spawn helper thread; cleaning up in case of errors */
static int
changelog_spawn_helper_threads (xlator_t *this, changelog_priv_t *priv)
{
        int ret = 0;
        int flags = 0;
        int pipe_fd[2] = {0, 0};

        /* Geo-Rep snapshot dependency:
         *
         * To implement explicit rollover of changlog journal on barrier
         * notification, a pipe is created to communicate between
         * 'changelog_rollover' thread and changelog main thread. The select
         * call used to wait till roll-over time in changelog_rollover thread
         * is modified to wait on read end of the pipe. When barrier
         * notification comes (i.e, in 'reconfigure'), select in
         * changelog_rollover thread is woken up explicitly by writing into
         * the write end of the pipe in 'reconfigure'.
         */

        ret = pipe (pipe_fd);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Cannot create pipe (reason: %s)", strerror (errno));
                goto out;
        }

        /* writer is non-blocking */
        flags = fcntl (pipe_fd[1], F_GETFL);
        flags |= O_NONBLOCK;

        ret = fcntl (pipe_fd[1], F_SETFL, flags);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to set O_NONBLOCK flag");
                goto out;
        }

        priv->cr_wfd = pipe_fd[1];
        priv->cr.rfd = pipe_fd[0];

        priv->cr.this = this;
        ret = gf_thread_create (&priv->cr.rollover_th,
				NULL, changelog_rollover, priv);
        if (ret)
                goto out;

        if (priv->fsync_interval) {
                priv->cf.this = this;
                ret = gf_thread_create (&priv->cf.fsync_th,
					NULL, changelog_fsync_thread, priv);
        }

        if (ret)
                changelog_cleanup_helper_threads (this, priv);

 out:
        return ret;
}

/* cleanup the notifier thread */
static int
changelog_cleanup_notifier (xlator_t *this, changelog_priv_t *priv)
{
        int ret = 0;

        if (priv->cn.notify_th) {
                changelog_thread_cleanup (this, priv->cn.notify_th);
                priv->cn.notify_th = 0;

                ret = close (priv->wfd);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR,
                                "error closing writer end of notifier pipe"
                                " (reason: %s)", strerror (errno));
        }

        return ret;
}

/* spawn the notifier thread - nop if already running */
static int
changelog_spawn_notifier (xlator_t *this, changelog_priv_t *priv)
{
        int ret        = 0;
        int flags      = 0;
        int pipe_fd[2] = {0, 0};

        if (priv->cn.notify_th)
                goto out; /* notifier thread already running */

        ret = pipe (pipe_fd);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Cannot create pipe (reason: %s)", strerror (errno));
                goto out;
        }

        /* writer is non-blocking */
        flags = fcntl (pipe_fd[1], F_GETFL);
        flags |= O_NONBLOCK;

        ret = fcntl (pipe_fd[1], F_SETFL, flags);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to set O_NONBLOCK flag");
                goto out;
        }

        priv->wfd = pipe_fd[1];

        priv->cn.this = this;
        priv->cn.rfd  = pipe_fd[0];

        ret = gf_thread_create (&priv->cn.notify_th,
				NULL, changelog_notifier, priv);

 out:
        return ret;
}

int
notify (xlator_t *this, int event, void *data, ...)
{
        changelog_priv_t       *priv            = NULL;
        dict_t                 *dict            = NULL;
        char                    buf[1]          = {1};
        int                     barrier         = DICT_DEFAULT;
        gf_boolean_t            bclean_req      = _gf_false;
        int                     ret             = 0;
        struct list_head        queue           = {0, };

        INIT_LIST_HEAD (&queue);

        priv = this->private;
        if (!priv)
                goto out;

        if (event == GF_EVENT_TRANSLATOR_OP) {

                dict = data;

                barrier = dict_get_str_boolean (dict, "barrier", DICT_DEFAULT);

                switch (barrier) {
                case DICT_ERROR:
                        gf_log (this->name, GF_LOG_ERROR,
                                "Barrier dict_get_str_boolean failed");
                        ret = -1;
                        goto out;

                case BARRIER_OFF:
                        gf_log (this->name, GF_LOG_INFO,
                                "Barrier off notification");

                        CHANGELOG_NOT_ON_THEN_GOTO(priv, ret, out);
                        LOCK(&priv->c_snap_lock);
                        {
                                changelog_snap_logging_stop (this, priv);
                        }
                        UNLOCK(&priv->c_snap_lock);

                        LOCK (&priv->bflags.lock);
                        {
                                if (priv->bflags.barrier_ext == _gf_false)
                                        ret = -1;
                        }
                        UNLOCK (&priv->bflags.lock);

                        if (ret == -1 ) {
                                gf_log (this->name, GF_LOG_ERROR, "Received"
                                        " another barrier off notification"
                                        " while already off");
                                goto out;
                        }

                        /* Stop changelog barrier and dequeue all fops */
                        LOCK (&priv->lock);
                        {
                                if (priv->barrier_enabled == _gf_true)
                                        __chlog_barrier_disable (this, &queue);
                                else
                                        ret = -1;
                        }
                        UNLOCK (&priv->lock);
                        /* If ret = -1, then changelog barrier is already
                         * disabled because of error or timeout.
                         */
                        if (ret == 0) {
                                chlog_barrier_dequeue_all(this, &queue);
                                gf_log(this->name, GF_LOG_INFO,
                                       "Disabled changelog barrier");
                        } else {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Changelog barrier already disabled");
                        }

                        LOCK (&priv->bflags.lock);
                        {
                                priv->bflags.barrier_ext = _gf_false;
                        }
                        UNLOCK (&priv->bflags.lock);

                        goto out;

                case BARRIER_ON:
                        gf_log (this->name, GF_LOG_INFO,
                                "Barrier on notification");

                        CHANGELOG_NOT_ON_THEN_GOTO(priv, ret, out);
                        LOCK(&priv->c_snap_lock);
                        {
                                changelog_snap_logging_start (this, priv);
                        }
                        UNLOCK(&priv->c_snap_lock);

                        LOCK (&priv->bflags.lock);
                        {
                                if (priv->bflags.barrier_ext == _gf_true)
                                        ret = -1;
                                else
                                        priv->bflags.barrier_ext = _gf_true;
                        }
                        UNLOCK (&priv->bflags.lock);

                        if (ret == -1 ) {
                                gf_log (this->name, GF_LOG_ERROR, "Received"
                                        " another barrier on notification when"
                                        " last one is not served yet");
                                goto out;
                        }

                        ret = pthread_mutex_lock (&priv->bn.bnotify_mutex);
                        CHANGELOG_PTHREAD_ERROR_HANDLE_1 (ret, out,
                                                                    bclean_req);
                        {
                                priv->bn.bnotify = _gf_true;
                        }
                        ret = pthread_mutex_unlock (&priv->bn.bnotify_mutex);
                        CHANGELOG_PTHREAD_ERROR_HANDLE_1 (ret, out,
                                                                    bclean_req);

                        /* Start changelog barrier */
                        LOCK (&priv->lock);
                        {
                                ret = __chlog_barrier_enable (this, priv);
                        }
                        UNLOCK (&priv->lock);
                        if (ret == -1) {
                                changelog_barrier_cleanup (this, priv, &queue);
                                goto out;
                        }

                        gf_log(this->name, GF_LOG_INFO,
                                           "Enabled changelog barrier");

                        ret = changelog_barrier_notify(priv, buf);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "Explicit roll over: write failed");
                                changelog_barrier_cleanup (this, priv, &queue);
                                ret = -1;
                                goto out;
                        }

                        ret = pthread_mutex_lock (&priv->bn.bnotify_mutex);
                        CHANGELOG_PTHREAD_ERROR_HANDLE_1 (ret, out,
                                                                    bclean_req);
                        {
                                /* The while condition check is required here to
                                 * handle spurious wakeup of cond wait that can
                                 * happen with pthreads. See man page */
                                while (priv->bn.bnotify == _gf_true) {
                                        ret = pthread_cond_wait (
                                                       &priv->bn.bnotify_cond,
                                                       &priv->bn.bnotify_mutex);
                                        CHANGELOG_PTHREAD_ERROR_HANDLE_1 (ret,
                                                                          out,
                                                                    bclean_req);
                                }
                        }
                        ret = pthread_mutex_unlock (&priv->bn.bnotify_mutex);
                        CHANGELOG_PTHREAD_ERROR_HANDLE_1 (ret, out, bclean_req);
                        gf_log (this->name, GF_LOG_INFO,
                                "Woke up: bnotify conditional wait");

                        ret = 0;
                        goto out;

                case DICT_DEFAULT:
                        gf_log (this->name, GF_LOG_ERROR,
                                "barrier key not found");
                        ret = -1;
                        goto out;

                default:
                        gf_log (this->name, GF_LOG_ERROR,
                                "Something went bad in dict_get_str_boolean");
                        ret = -1;
                        goto out;
                }
        } else {
                ret = default_notify (this, event, data);
        }

 out:
        if (bclean_req)
                changelog_barrier_cleanup (this, priv, &queue);

        return ret;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_changelog_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_WARNING, "Memory accounting"
                        " init failed");
                return ret;
        }

        return ret;
}

static int
changelog_init (xlator_t *this, changelog_priv_t *priv)
{
        int                  i   = 0;
        int                  ret = -1;
        struct timeval       tv  = {0,};
        changelog_log_data_t cld = {0,};

        ret = gettimeofday (&tv, NULL);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "gettimeofday() failure");
                goto out;
        }

        priv->slice.tv_start = tv;

        priv->maps[CHANGELOG_TYPE_DATA]     = "D ";
        priv->maps[CHANGELOG_TYPE_METADATA] = "M ";
        priv->maps[CHANGELOG_TYPE_ENTRY]    = "E ";

        for (; i < CHANGELOG_MAX_TYPE; i++) {
                /* start with version 1 */
                priv->slice.changelog_version[i] = 1;
        }

        if (!priv->active)
                return ret;

        /* spawn the notifier thread */
        ret = changelog_spawn_notifier (this, priv);
        if (ret)
                goto out;

        /**
         * start with a fresh changelog file every time. this is done
         * in case there was an encoding change. so... things are kept
         * simple here.
         */
        ret = changelog_fill_rollover_data (&cld, _gf_false);
        if(ret)
                goto out;

        ret = htime_open (this, priv, cld.cld_roll_time);
        /* call htime open with cld's rollover_time */
        if (ret)
                goto out;

        LOCK (&priv->lock);
        {
                ret = changelog_inject_single_event (this, priv, &cld);
        }
        UNLOCK (&priv->lock);

        /* ... and finally spawn the helpers threads */
        ret = changelog_spawn_helper_threads (this, priv);

 out:
        return ret;
}

/* Init all pthread condition variables and locks in changelog*/
static int
changelog_pthread_init (xlator_t *this, changelog_priv_t *priv)
{
        gf_boolean_t    bn_mutex_init         = _gf_false;
        gf_boolean_t    bn_cond_init          = _gf_false;
        gf_boolean_t    dm_mutex_black_init   = _gf_false;
        gf_boolean_t    dm_cond_black_init    = _gf_false;
        gf_boolean_t    dm_mutex_white_init   = _gf_false;
        gf_boolean_t    dm_cond_white_init    = _gf_false;
        int             ret                   = 0;

        if ((ret = pthread_mutex_init(&priv->bn.bnotify_mutex, NULL)) != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "bnotify pthread_mutex_init failed (%d)", ret);
                ret = -1;
                goto out;
        }
        bn_mutex_init = _gf_true;

        if ((ret = pthread_cond_init(&priv->bn.bnotify_cond, NULL)) != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "bnotify pthread_cond_init failed (%d)", ret);
                ret = -1;
                goto out;
        }
        bn_cond_init = _gf_true;

        if ((ret = pthread_mutex_init(&priv->dm.drain_black_mutex, NULL)) != 0)
        {
                gf_log (this->name, GF_LOG_ERROR,
                        "drain_black pthread_mutex_init failed (%d)", ret);
                ret = -1;
                goto out;
        }
        dm_mutex_black_init = _gf_true;

        if ((ret = pthread_cond_init(&priv->dm.drain_black_cond, NULL)) != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "drain_black pthread_cond_init failed (%d)", ret);
                ret = -1;
                goto out;
        }
        dm_cond_black_init = _gf_true;

        if ((ret = pthread_mutex_init(&priv->dm.drain_white_mutex, NULL)) != 0)
        {
                gf_log (this->name, GF_LOG_ERROR,
                        "drain_white pthread_mutex_init failed (%d)", ret);
                ret = -1;
                goto out;
        }
        dm_mutex_white_init = _gf_true;

        if ((ret = pthread_cond_init(&priv->dm.drain_white_cond, NULL)) != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "drain_white pthread_cond_init failed (%d)", ret);
                ret = -1;
                goto out;
        }
        dm_cond_white_init = _gf_true;
 out:
        if (ret) {
                if (bn_mutex_init)
                        pthread_mutex_destroy(&priv->bn.bnotify_mutex);
                if (bn_cond_init)
                        pthread_cond_destroy (&priv->bn.bnotify_cond);
                if (dm_mutex_black_init)
                        pthread_mutex_destroy(&priv->dm.drain_black_mutex);
                if (dm_cond_black_init)
                        pthread_cond_destroy (&priv->dm.drain_black_cond);
                if (dm_mutex_white_init)
                        pthread_mutex_destroy(&priv->dm.drain_white_mutex);
                if (dm_cond_white_init)
                        pthread_cond_destroy (&priv->dm.drain_white_cond);
        }
        return ret;
}

/* Destroy all pthread condition variables and locks in changelog */
static inline void
changelog_pthread_destroy (changelog_priv_t *priv)
{
        pthread_mutex_destroy (&priv->bn.bnotify_mutex);
        pthread_cond_destroy (&priv->bn.bnotify_cond);
        pthread_mutex_destroy (&priv->dm.drain_black_mutex);
        pthread_cond_destroy (&priv->dm.drain_black_cond);
        pthread_mutex_destroy (&priv->dm.drain_white_mutex);
        pthread_cond_destroy (&priv->dm.drain_white_cond);
        LOCK_DESTROY (&priv->bflags.lock);
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        int                     ret            = 0;
        char                   *tmp            = NULL;
        changelog_priv_t       *priv           = NULL;
        gf_boolean_t            active_earlier = _gf_true;
        gf_boolean_t            active_now     = _gf_true;
        changelog_time_slice_t *slice          = NULL;
        changelog_log_data_t    cld            = {0,};
        char    htime_dir[PATH_MAX]            = {0,};
        char    csnap_dir[PATH_MAX]            = {0,};
        struct timeval          tv             = {0,};
        uint32_t                timeout        = 0;

        priv = this->private;
        if (!priv)
                goto out;

        ret = -1;
        active_earlier = priv->active;

        /* first stop the rollover and the fsync thread */
        changelog_cleanup_helper_threads (this, priv);

        GF_OPTION_RECONF ("changelog-dir", tmp, options, str, out);
        if (!tmp) {
                gf_log (this->name, GF_LOG_ERROR,
                        "\"changelog-dir\" option is not set");
                goto out;
        }

        GF_FREE (priv->changelog_dir);
        priv->changelog_dir = gf_strdup (tmp);
        if (!priv->changelog_dir)
                goto out;

        ret = mkdir_p (priv->changelog_dir, 0600, _gf_true);

        if (ret)
                goto out;
        CHANGELOG_FILL_HTIME_DIR(priv->changelog_dir, htime_dir);
        ret = mkdir_p (htime_dir, 0600, _gf_true);

        if (ret)
                goto out;

        CHANGELOG_FILL_CSNAP_DIR(priv->changelog_dir, csnap_dir);
        ret = mkdir_p (csnap_dir, 0600, _gf_true);

        if (ret)
                goto out;

        GF_OPTION_RECONF ("changelog", active_now, options, bool, out);

        /**
         * changelog_handle_change() handles changes that could possibly
         * have been submit changes before changelog deactivation.
         */
        if (!active_now)
                priv->active = _gf_false;

        GF_OPTION_RECONF ("op-mode", tmp, options, str, out);
        changelog_assign_opmode (priv, tmp);

        tmp = NULL;

        GF_OPTION_RECONF ("encoding", tmp, options, str, out);
        changelog_assign_encoding (priv, tmp);

        GF_OPTION_RECONF ("rollover-time",
                          priv->rollover_time, options, int32, out);
        GF_OPTION_RECONF ("fsync-interval",
                          priv->fsync_interval, options, int32, out);
        GF_OPTION_RECONF ("changelog-barrier-timeout",
                          timeout, options, time, out);
        changelog_assign_barrier_timeout (priv, timeout);

        if (active_now || active_earlier) {
                ret = changelog_fill_rollover_data (&cld, !active_now);
                if (ret)
                        goto out;

                slice = &priv->slice;

                LOCK (&priv->lock);
                {
                        ret = changelog_inject_single_event (this, priv, &cld);
                        if (!ret && active_now)
                                SLICE_VERSION_UPDATE (slice);
                }
                UNLOCK (&priv->lock);

                if (ret)
                        goto out;

                if (active_now) {
                        if (!active_earlier) {
                                if (gettimeofday(&tv, NULL) ) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                 "unable to fetch htime");
                                        ret = -1;
                                        goto out;
                                }
                                htime_open(this, priv, tv.tv_sec);
                        }
                        ret = changelog_spawn_notifier (this, priv);
                        if (!ret)
                                ret = changelog_spawn_helper_threads (this,
                                                                      priv);
                } else
                        ret = changelog_cleanup_notifier (this, priv);
        }

 out:
        if (ret) {
                ret = changelog_cleanup_notifier (this, priv);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "changelog reconfigured");
                if (active_now)
                        priv->active = _gf_true;
        }

        return ret;
}

int32_t
init (xlator_t *this)
{
        int                     ret                     = -1;
        char                    *tmp                    = NULL;
        changelog_priv_t        *priv                   = NULL;
        gf_boolean_t            cond_lock_init          = _gf_false;
        char                    htime_dir[PATH_MAX]     = {0,};
        char                    csnap_dir[PATH_MAX]     = {0,};
        uint32_t                timeout                 = 0;

        GF_VALIDATE_OR_GOTO ("changelog", this, out);

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "translator needs a single subvolume");
                goto out;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_ERROR,
                        "dangling volume. please check volfile");
                goto out;
        }

        priv = GF_CALLOC (1, sizeof (*priv), gf_changelog_mt_priv_t);
        if (!priv)
                goto out;

        this->local_pool = mem_pool_new (changelog_local_t, 64);
        if (!this->local_pool) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to create local memory pool");
                goto out;
        }

        LOCK_INIT (&priv->lock);
        LOCK_INIT (&priv->c_snap_lock);

        GF_OPTION_INIT ("changelog-brick", tmp, str, out);
        if (!tmp) {
                gf_log (this->name, GF_LOG_ERROR,
                        "\"changelog-brick\" option is not set");
                goto out;
        }

        priv->changelog_brick = gf_strdup (tmp);
        if (!priv->changelog_brick)
                goto out;
        tmp = NULL;

        GF_OPTION_INIT ("changelog-dir", tmp, str, out);
        if (!tmp) {
                gf_log (this->name, GF_LOG_ERROR,
                        "\"changelog-dir\" option is not set");
                goto out;
        }

        priv->changelog_dir = gf_strdup (tmp);
        if (!priv->changelog_dir)
                goto out;
        tmp = NULL;

        /**
         * create the directory even if change-logging would be inactive
         * so that consumers can _look_ into it (finding nothing...)
         */
        ret = mkdir_p (priv->changelog_dir, 0600, _gf_true);

        if (ret)
                goto out;

        CHANGELOG_FILL_HTIME_DIR(priv->changelog_dir, htime_dir);
        ret = mkdir_p (htime_dir, 0600, _gf_true);
        if (ret)
                goto out;

        CHANGELOG_FILL_CSNAP_DIR(priv->changelog_dir, csnap_dir);
        ret = mkdir_p (csnap_dir, 0600, _gf_true);
        if (ret)
                goto out;

        GF_OPTION_INIT ("changelog", priv->active, bool, out);

        GF_OPTION_INIT ("op-mode", tmp, str, out);
        changelog_assign_opmode (priv, tmp);

        tmp = NULL;

        GF_OPTION_INIT ("encoding", tmp, str, out);
        changelog_assign_encoding (priv, tmp);

        GF_OPTION_INIT ("rollover-time", priv->rollover_time, int32, out);

        GF_OPTION_INIT ("fsync-interval", priv->fsync_interval, int32, out);
        GF_OPTION_INIT ("changelog-barrier-timeout", timeout, time, out);
        priv->timeout.tv_sec = timeout;

        changelog_encode_change(priv);

        GF_ASSERT (cb_bootstrap[priv->op_mode].mode == priv->op_mode);
        priv->cb = &cb_bootstrap[priv->op_mode];

        /* ... now bootstrap the logger */
        ret = priv->cb->ctor (this, &priv->cd);
        if (ret)
                goto out;

        priv->changelog_fd = -1;

        /* snap dependency changes */
        priv->dm.black_fop_cnt = 0;
        priv->dm.white_fop_cnt = 0;
        priv->dm.drain_wait_black = _gf_false;
        priv->dm.drain_wait_white = _gf_false;
        priv->current_color = FOP_COLOR_BLACK;
        priv->explicit_rollover = _gf_false;
        /* Mutex is not needed as threads are not spawned yet */
        priv->bn.bnotify = _gf_false;
        ret = changelog_pthread_init (this, priv);
        if (ret)
                goto out;

        LOCK_INIT (&priv->bflags.lock);
        cond_lock_init = _gf_true;
        priv->bflags.barrier_ext = _gf_false;

        /* Changelog barrier init */
        INIT_LIST_HEAD (&priv->queue);
        priv->barrier_enabled = _gf_false;

        ret = changelog_init (this, priv);
        if (ret)
                goto out;

        gf_log (this->name, GF_LOG_DEBUG, "changelog translator loaded");

 out:
        if (ret) {
                if (this && this->local_pool)
                        mem_pool_destroy (this->local_pool);
                if (priv) {
                        if (priv->cb) {
                                ret = priv->cb->dtor (this, &priv->cd);
                                if (ret)
                                        gf_log (this->name, GF_LOG_ERROR,
                                        "error in cleanup during init()");
                        }
                        GF_FREE (priv->changelog_brick);
                        GF_FREE (priv->changelog_dir);
                        if (cond_lock_init)
                                changelog_pthread_destroy (priv);
                        GF_FREE (priv);
                }
                this->private = NULL;
        } else
                this->private = priv;

        return ret;
}

void
fini (xlator_t *this)
{
        int               ret  = -1;
        changelog_priv_t *priv = NULL;

        priv = this->private;

        if (priv) {
                ret = priv->cb->dtor (this, &priv->cd);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR,
                                "error in fini");
                mem_pool_destroy (this->local_pool);
                GF_FREE (priv->changelog_brick);
                GF_FREE (priv->changelog_dir);
                changelog_pthread_destroy (priv);
                GF_FREE (priv);
        }

        this->private = NULL;

        return;
}

struct xlator_fops fops = {
        .mknod        = changelog_mknod,
        .mkdir        = changelog_mkdir,
        .create       = changelog_create,
        .symlink      = changelog_symlink,
        .writev       = changelog_writev,
        .truncate     = changelog_truncate,
        .ftruncate    = changelog_ftruncate,
        .link         = changelog_link,
        .rename       = changelog_rename,
        .unlink       = changelog_unlink,
        .rmdir        = changelog_rmdir,
        .setattr      = changelog_setattr,
        .fsetattr     = changelog_fsetattr,
        .setxattr     = changelog_setxattr,
        .fsetxattr    = changelog_fsetxattr,
        .removexattr  = changelog_removexattr,
        .fremovexattr = changelog_fremovexattr,
};

struct xlator_cbks cbks = {
        .forget = changelog_forget,
};

struct volume_options options[] = {
        {.key = {"changelog"},
         .type = GF_OPTION_TYPE_BOOL,
         .default_value = "off",
         .description = "enable/disable change-logging"
        },
        {.key = {"changelog-brick"},
         .type = GF_OPTION_TYPE_PATH,
         .description = "brick path to generate unique socket file name."
                       " should be the export directory of the volume strictly."
        },
        {.key = {"changelog-dir"},
         .type = GF_OPTION_TYPE_PATH,
         .description = "directory for the changelog files"
        },
        {.key = {"op-mode"},
         .type = GF_OPTION_TYPE_STR,
         .default_value = "realtime",
         .value = {"realtime"},
         .description = "operation mode - futuristic operation modes"
        },
        {.key = {"encoding"},
         .type = GF_OPTION_TYPE_STR,
         .default_value = "ascii",
         .value = {"binary", "ascii"},
         .description = "encoding type for changelogs"
        },
        {.key = {"rollover-time"},
         .default_value = "15",
         .type = GF_OPTION_TYPE_TIME,
         .description = "time to switch to a new changelog file (in seconds)"
        },
        {.key = {"fsync-interval"},
         .type = GF_OPTION_TYPE_TIME,
         .default_value = "5",
         .description = "do not open CHANGELOG file with O_SYNC mode."
                        " instead perform fsync() at specified intervals"
        },
        { .key = {"changelog-barrier-timeout"},
          .type = GF_OPTION_TYPE_TIME,
          .default_value = BARRIER_TIMEOUT,
          .description = "After 'timeout' seconds since the time 'barrier' "
                         "option was set to \"on\", unlink/rmdir/rename  "
                         "operations are no longer blocked and previously "
                         "blocked fops are allowed to go through"
        },
        {.key = {NULL}
        },
};
