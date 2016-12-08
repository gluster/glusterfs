/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "xlator.h"
#include "defaults.h"
#include "syscall.h"
#include "logging.h"
#include "iobuf.h"

#include "changelog-rt.h"

#include "changelog-encoders.h"
#include "changelog-mem-types.h"
#include "changelog-messages.h"

#include <pthread.h>

#include "changelog-rpc.h"
#include "errno.h"

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

        gf_msg_debug (this->name, 0, "Dequeue rmdir");
                      changelog_color_fop_and_inc_cnt (this, priv,
                      frame->local);
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
        if (priv->capture_del_path) {
                CHANGELOG_FILL_ENTRY_DIR_PATH (co, loc->pargfid, loc->name,
                                               del_entry_fn, del_entry_free_fn,
                                              xtra_len, wind, _gf_true);
        } else {
                CHANGELOG_FILL_ENTRY_DIR_PATH (co, loc->pargfid, loc->name,
                                               del_entry_fn, del_entry_free_fn,
                                              xtra_len, wind, _gf_false);
        }

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
                gf_msg_debug (this->name, 0, "Enqueue rmdir");
                goto out;
        }
        if (barrier_enabled && !stub) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        CHANGELOG_MSG_NO_MEMORY,
                        "Failed to barrier FOPs, disabling changelog barrier "
                        "FOP: rmdir");
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

        gf_msg_debug (this->name, 0, "Dequeue unlink");
                      changelog_color_fop_and_inc_cnt
                      (this, priv, frame->local);
        STACK_WIND (frame, changelog_unlink_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->unlink,
                    loc, xflags, xdata);
        return 0;
}

int32_t
changelog_unlink (call_frame_t *frame, xlator_t *this,
                  loc_t *loc, int xflags, dict_t *xdata)
{
        size_t                       xtra_len        = 0;
        changelog_priv_t             *priv           = NULL;
        changelog_opt_t              *co             = NULL;
        call_stub_t                  *stub           = NULL;
        struct list_head             queue           = {0, };
        gf_boolean_t                 barrier_enabled = _gf_false;
        dht_changelog_rename_info_t  *info           = NULL;
        int                          ret             = 0;
        char                         old_name[NAME_MAX] = {0};
        char                         new_name[NAME_MAX] = {0};
        char                         *nname             = NULL;

        INIT_LIST_HEAD (&queue);
        priv = this->private;

        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);

        ret = dict_get_bin (xdata, DHT_CHANGELOG_RENAME_OP_KEY, (void **)&info);
        if (!ret) {     /* special case: unlink considered as rename */
                /* 3 == fop + oldloc + newloc */
                CHANGELOG_INIT_NOCHECK (this, frame->local,
                                        NULL, loc->inode->gfid, 3);

                co = changelog_get_usable_buffer (frame->local);
                if (!co)
                        goto wind;

                CHANGLOG_FILL_FOP_NUMBER (co, GF_FOP_RENAME, fop_fn, xtra_len);

                co++;
                strncpy (old_name, info->buffer, info->oldname_len);
                CHANGELOG_FILL_ENTRY (co, info->old_pargfid, old_name,
                                      entry_fn, entry_free_fn, xtra_len, wind);

                co++;
                /* new name resides just after old name */
                nname = info->buffer + info->oldname_len;
                strncpy (new_name, nname, info->newname_len);
                CHANGELOG_FILL_ENTRY (co, info->new_pargfid, new_name,
                                      entry_fn, entry_free_fn, xtra_len, wind);

                changelog_set_usable_record_and_length (frame->local,
                                                        xtra_len, 3);
        } else {        /* default unlink */
                CHANGELOG_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, wind);
                CHANGELOG_INIT_NOCHECK (this, frame->local, NULL,
                                                        loc->inode->gfid, 2);

                co = changelog_get_usable_buffer (frame->local);
                if (!co)
                        goto wind;

                CHANGLOG_FILL_FOP_NUMBER (co, frame->root->op,
                                                fop_fn, xtra_len);

                co++;
                if (priv->capture_del_path) {
                        CHANGELOG_FILL_ENTRY_DIR_PATH (co, loc->pargfid,
                                     loc->name, del_entry_fn, del_entry_free_fn,
                                     xtra_len, wind, _gf_true);
                } else {
                        CHANGELOG_FILL_ENTRY_DIR_PATH (co, loc->pargfid,
                                     loc->name, del_entry_fn, del_entry_free_fn,
                                     xtra_len, wind, _gf_false);
                }

                changelog_set_usable_record_and_length (frame->local,
                                                        xtra_len, 2);
        }

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
                gf_msg_debug (this->name, 0, "Enqueue unlink");
                goto out;
        }
        if (barrier_enabled && !stub) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        CHANGELOG_MSG_NO_MEMORY,
                        "Failed to barrier FOPs, disabling changelog barrier "
                        "FOP: unlink");
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
        changelog_priv_t                  *priv  = NULL;
        changelog_local_t                 *local = NULL;

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

        gf_msg_debug (this->name, 0, "Dequeue rename");
                      changelog_color_fop_and_inc_cnt
                      (this, priv, frame->local);
        STACK_WIND (frame, changelog_rename_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->rename,
                    oldloc, newloc, xdata);
        return 0;
}

int32_t
changelog_rename (call_frame_t *frame, xlator_t *this,
                  loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        size_t               xtra_len        = 0;
        changelog_priv_t     *priv           = NULL;
        changelog_opt_t      *co             = NULL;
        call_stub_t          *stub           = NULL;
        struct list_head     queue           = {0, };
        gf_boolean_t         barrier_enabled = _gf_false;
        dht_changelog_rename_info_t  *info   = NULL;
        int                  ret             = 0;

        INIT_LIST_HEAD (&queue);

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);

        ret = dict_get_bin (xdata, DHT_CHANGELOG_RENAME_OP_KEY, (void **)&info);
        if (ret && oldloc->inode->ia_type != IA_IFDIR)  {
                /* xdata "NOT" set for a non-directory,
                 * Special rename => avoid logging */
               goto wind;
        }

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
                gf_msg_debug (this->name, 0, "Enqueue rename");
                goto out;
        }
        if (barrier_enabled && !stub) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        CHANGELOG_MSG_NO_MEMORY,
                        "Failed to barrier FOPs, disabling changelog barrier "
                        "FOP: rename");
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

        gf_msg_debug (this->name, 0, "Dequeuing link");
                      changelog_color_fop_and_inc_cnt
                      (this, priv, frame->local);
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
                gf_msg_debug (this->name, 0, "Enqueued link");
                goto out;
        }

        if (barrier_enabled && !stub) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_NO_MEMORY,
                        "Failed to barrier FOPs, disabling changelog barrier "
                        "FOP: link");
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

        gf_msg_debug (this->name, 0, "Dequeuing mkdir");
                      changelog_color_fop_and_inc_cnt
                      (this, priv, frame->local);
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
                gf_msg_debug (this->name, 0,
                              "failed to get gfid from dict");
                goto wind;
        }
        gf_uuid_copy (gfid, uuid_req);

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
                gf_msg_debug (this->name, 0, "Enqueued mkdir");
                goto out;
        }

        if (barrier_enabled && !stub) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        CHANGELOG_MSG_NO_MEMORY,
                        "Failed to barrier FOPs, disabling changelog barrier "
                        "FOP: mkdir");
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

        gf_msg_debug (this->name, 0, "Dequeuing symlink");
                      changelog_color_fop_and_inc_cnt
                      (this, priv, frame->local);
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
                gf_msg_debug (this->name, 0,
                              "failed to get gfid from dict");
                goto wind;
        }
        gf_uuid_copy (gfid, uuid_req);

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
                gf_msg_debug (this->name, 0, "Enqueued symlink");
                goto out;
        }

        if (barrier_enabled && !stub) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        CHANGELOG_MSG_NO_MEMORY,
                        "Failed to barrier FOPs, disabling changelog barrier "
                        "FOP: symlink");
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

        gf_msg_debug (this->name, 0, "Dequeuing mknod");
                      changelog_color_fop_and_inc_cnt
                      (this, priv, frame->local);
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

        /* Check whether changelog active */
        if (!(priv->active))
                goto wind;

        /* Check whether rebalance activity */
        if (frame->root->pid == GF_CLIENT_PID_DEFRAG)
                goto wind;

        /* If tier-dht linkto is SET, ignore about verifiying :
         * 1. Whether internal fop AND
         * 2. Whether tier rebalance process activity (this will help in
         * recording mknod if tier rebalance process calls this mknod) */
        if (!(dict_get (xdata, "trusted.tier.tier-dht.linkto"))) {
                CHANGELOG_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, wind);
                if (frame->root->pid == GF_CLIENT_PID_TIER_DEFRAG)
                        goto wind;
        }

        ret = dict_get_ptr (xdata, "gfid-req", &uuid_req);
        if (ret) {
                gf_msg_debug (this->name, 0,
                              "failed to get gfid from dict");
                goto wind;
        }
        gf_uuid_copy (gfid, uuid_req);

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
                gf_msg_debug (this->name, 0, "Enqueued mknod");
                goto out;
        }

        if (barrier_enabled && !stub) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        CHANGELOG_MSG_NO_MEMORY,
                        "Failed to barrier FOPs, disabling changelog barrier "
                        "FOP: mknod");
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
        int32_t ret = 0;
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;
        changelog_event_t  ev    = {0,};

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        /* fill the event structure.. similar to open() */
        ev.ev_type = CHANGELOG_OP_TYPE_CREATE;
        gf_uuid_copy (ev.u.create.gfid, buf->ia_gfid);
        ev.u.create.flags = fd->flags;
        changelog_dispatch_event (this, priv, &ev);

        if (changelog_ev_selected
                   (this, &priv->ev_selection, CHANGELOG_OP_TYPE_RELEASE)) {
                ret = fd_ctx_set (fd, this, (uint64_t)(long) 0x1);
                if (ret)
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                CHANGELOG_MSG_SET_FD_CONTEXT,
                                "could not set fd context (for release cbk)");
        }

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

        gf_msg_debug (this->name, 0, "Dequeuing create");
                      changelog_color_fop_and_inc_cnt
                      (this, priv, frame->local);
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
                gf_msg_debug (this->name, 0,
                              "failed to get gfid from dict");
                goto wind;
        }
        gf_uuid_copy (gfid, uuid_req);

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
                gf_msg_debug (this->name, 0, "Enqueued create");
                goto out;
        }

        if (barrier_enabled && !stub) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        CHANGELOG_MSG_NO_MEMORY,
                        "Failed to barrier FOPs, disabling changelog barrier "
                        "FOP: create");
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

        CHANGELOG_OP_BOUNDARY_CHECK (frame, wind);

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
        uuid_t            shard_root_gfid = {0,};

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);

        CHANGELOG_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, wind);

        /* Do not record META on .shard */
        gf_uuid_parse (SHARD_ROOT_GFID, shard_root_gfid);
        if (gf_uuid_compare (loc->gfid, shard_root_gfid) == 0) {
                goto wind;
        }

        CHANGELOG_OP_BOUNDARY_CHECK (frame, wind);

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

        CHANGELOG_OP_BOUNDARY_CHECK (frame, wind);

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

        CHANGELOG_OP_BOUNDARY_CHECK (frame, wind);

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

/* changelog_handle_virtual_xattr:
 *         Handles virtual setxattr 'glusterfs.geo-rep.trigger-sync' on files.
 *         Following is the behaviour based on the value of xattr.
 *                         1: Captures only DATA entry in changelog.
 *                         2: Tries to captures both ENTRY and DATA entry in
 *                            changelog. If failed to get pargfid, only DATA
 *                            entry is captured.
 *           any other value: ENOTSUP is returned.
 */
static void
changelog_handle_virtual_xattr (call_frame_t *frame, xlator_t *this,
                                loc_t *loc, dict_t *dict)
{
        changelog_priv_t     *priv         = NULL;
        changelog_local_t    *local        = NULL;
        int32_t               value        = 0;
        int                   ret          = 0;
        int                   dict_ret     = 0;
        gf_boolean_t          valid        = _gf_false;

        priv = this->private;
        GF_ASSERT (priv);

        dict_ret = dict_get_int32 (dict, GF_XATTR_TRIGGER_SYNC, &value);

        if ((dict_ret == 0 && value == 1) && ((loc->inode->ia_type == IA_IFDIR)
            || (loc->inode->ia_type == IA_IFREG)))
                valid = _gf_true;

        if (valid) {
                ret = changelog_fill_entry_buf (frame, this, loc, &local);
                if (ret) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                CHANGELOG_MSG_ENTRY_BUF_INFO,
                                "Entry cannot be"
                                " captured for gfid: %s. Capturing DATA"
                                " entry.", uuid_utoa (loc->inode->gfid));
                        goto unwind;
                }
                changelog_update (this, priv, local, CHANGELOG_TYPE_ENTRY);

 unwind:
                /* Capture DATA only if it's a file. */
                if (loc->inode->ia_type != IA_IFDIR)
                        changelog_update (this, priv, frame->local,
                                          CHANGELOG_TYPE_DATA);
                /* Assign local to prev_entry, so unwind will take
                 * care of cleanup. */
                ((changelog_local_t *)(frame->local))->prev_entry = local;
                CHANGELOG_STACK_UNWIND (setxattr, frame, 0, 0, NULL);
                return;
        } else {
                CHANGELOG_STACK_UNWIND (setxattr, frame, -1, ENOTSUP, NULL);
                return;
        }
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

        CHANGELOG_OP_BOUNDARY_CHECK (frame, wind);

        CHANGELOG_INIT (this, frame->local,
                        loc->inode, loc->inode->gfid, 1);

        /* On setting this virtual xattr on a file, an explicit data
         * sync is triggered from geo-rep as CREATE|DATA entry is
         * recorded in changelog based on xattr value.
         */
        if (dict_get (dict, GF_XATTR_TRIGGER_SYNC)) {
                changelog_handle_virtual_xattr (frame, this, loc, dict);
                return 0;
        }

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
        CHANGELOG_IF_INTERNAL_FOP_THEN_GOTO (frame, xdata, wind);

        CHANGELOG_OP_BOUNDARY_CHECK (frame, wind);

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

int32_t
changelog_xattrop_cbk (call_frame_t *frame,
                       void *cookie, xlator_t *this, int32_t op_ret,
                       int32_t op_errno, dict_t *xattr, dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_METADATA);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (xattrop, frame, op_ret, op_errno, xattr, xdata);

        return 0;
}

int32_t
changelog_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
                   gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
        changelog_priv_t *priv      = NULL;
        changelog_opt_t  *co        = NULL;
        size_t            xtra_len  = 0;
        int               ret       = 0;
        void             *size_attr = NULL;

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);
        ret = dict_get_ptr (xattr, GF_XATTR_SHARD_FILE_SIZE, &size_attr);
        if (ret)
                goto wind;

        CHANGELOG_OP_BOUNDARY_CHECK (frame, wind);

        CHANGELOG_INIT (this, frame->local,
                        loc->inode, loc->inode->gfid, 1);

        co = changelog_get_usable_buffer (frame->local);
        if (!co)
                goto wind;

        CHANGLOG_FILL_FOP_NUMBER (co, frame->root->op, fop_fn, xtra_len);

        changelog_set_usable_record_and_length (frame->local, xtra_len, 1);

 wind:
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_xattrop_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->xattrop,
                    loc, optype, xattr, xdata);
        return 0;
}

int32_t
changelog_fxattrop_cbk (call_frame_t *frame,
                         void *cookie, xlator_t *this, int32_t op_ret,
                         int32_t op_errno, dict_t *xattr, dict_t *xdata)
{
        changelog_priv_t  *priv  = NULL;
        changelog_local_t *local = NULL;

        priv  = this->private;
        local = frame->local;

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !local), unwind);

        changelog_update (this, priv, local, CHANGELOG_TYPE_METADATA);

 unwind:
        changelog_dec_fop_cnt (this, priv, local);
        CHANGELOG_STACK_UNWIND (fxattrop, frame,
                                op_ret, op_errno, xattr, xdata);

        return 0;
}

int32_t
changelog_fxattrop (call_frame_t *frame,
                     xlator_t *this, fd_t *fd, gf_xattrop_flags_t optype,
                     dict_t *xattr, dict_t *xdata)
{
        changelog_priv_t *priv      = NULL;
        changelog_opt_t  *co        = NULL;
        size_t            xtra_len  = 0;
        void             *size_attr = NULL;
        int               ret       = 0;

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);
        ret = dict_get_ptr (xattr, GF_XATTR_SHARD_FILE_SIZE, &size_attr);
        if (ret)
                goto wind;


        CHANGELOG_OP_BOUNDARY_CHECK (frame, wind);

        CHANGELOG_INIT (this, frame->local,
                        fd->inode, fd->inode->gfid, 1);

        co = changelog_get_usable_buffer (frame->local);
        if (!co)
                goto wind;

        CHANGLOG_FILL_FOP_NUMBER (co, frame->root->op, fop_fn, xtra_len);

        changelog_set_usable_record_and_length (frame->local, xtra_len, 1);

 wind:
        changelog_color_fop_and_inc_cnt (this, priv, frame->local);
        STACK_WIND (frame, changelog_fxattrop_cbk,
                    FIRST_CHILD (this), FIRST_CHILD (this)->fops->fxattrop,
                    fd, optype, xattr, xdata);
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

/* open, release and other beasts */

/* {{{ */



int
changelog_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int op_ret, int op_errno, fd_t *fd, dict_t *xdata)
{
        int                ret    = 0;
        changelog_priv_t  *priv   = NULL;
        changelog_event_t  ev     = {0,};
        gf_boolean_t logopen = _gf_false;

        priv = this->private;
        if (frame->local) {
                frame->local = NULL;
                logopen = _gf_true;
        }

        CHANGELOG_COND_GOTO (priv, ((op_ret < 0) || !logopen), unwind);

        /* fill the event structure */
        ev.ev_type = CHANGELOG_OP_TYPE_OPEN;
        gf_uuid_copy (ev.u.open.gfid, fd->inode->gfid);
        ev.u.open.flags = fd->flags;
        changelog_dispatch_event (this, priv, &ev);

        if (changelog_ev_selected
                   (this, &priv->ev_selection, CHANGELOG_OP_TYPE_RELEASE)) {
                ret = fd_ctx_set (fd, this, (uint64_t)(long) 0x1);
                if (ret)
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                CHANGELOG_MSG_SET_FD_CONTEXT,
                                "could not set fd context (for release cbk)");
        }

 unwind:
        CHANGELOG_STACK_UNWIND (open, frame, op_ret, op_errno, fd, xdata);
        return 0;
}

int
changelog_open (call_frame_t *frame, xlator_t *this,
                loc_t *loc, int flags, fd_t *fd, dict_t *xdata)
{
        changelog_priv_t *priv = NULL;

        priv = this->private;
        CHANGELOG_NOT_ACTIVE_THEN_GOTO (frame, priv, wind);

        frame->local = (void *)0x1; /* do not dereference in ->cbk */

 wind:
        STACK_WIND (frame, changelog_open_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->open, loc, flags, fd, xdata);
        return 0;
}

/* }}} */

/* {{{ */


/* }}} */

int32_t
_changelog_generic_dispatcher (dict_t *dict,
                               char *key, data_t *value, void *data)
{
        xlator_t *this = NULL;
        changelog_priv_t *priv = NULL;

        this = data;
        priv = this->private;

        changelog_dispatch_event (this, priv, (changelog_event_t *)value->data);
        return 0;
}

/**
 * changelog ipc dispatches events, pointers of which are passed in
 * @xdata. Dispatching is orderless (whatever order dict_foreach()
 * traverses the dictionary).
 */
int32_t
changelog_ipc (call_frame_t *frame, xlator_t *this, int32_t op, dict_t *xdata)
{
        if (op != GF_IPC_TARGET_CHANGELOG)
                goto wind;

        /* it's for us, do the job */
        if (xdata)
                (void) dict_foreach (xdata,
                                     _changelog_generic_dispatcher, this);

        STACK_UNWIND_STRICT (ipc, frame, 0, 0, NULL);
        return 0;

 wind:
        STACK_WIND (frame, default_ipc_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->ipc, op, xdata);
        return 0;
}


/* {{{ */

int32_t
changelog_release (xlator_t *this, fd_t *fd)
{
        changelog_event_t ev = {0,};
        changelog_priv_t *priv = NULL;

        priv = this->private;

        ev.ev_type = CHANGELOG_OP_TYPE_RELEASE;
        gf_uuid_copy (ev.u.release.gfid, fd->inode->gfid);
        changelog_dispatch_event (this, priv, &ev);

        (void) fd_ctx_del (fd, this, NULL);

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
        if (priv->cr.rollover_th) {
                (void) changelog_thread_cleanup (this, priv->cr.rollover_th);
                priv->cr.rollover_th = 0;
        }

        if (priv->cf.fsync_th) {
                (void) changelog_thread_cleanup (this, priv->cf.fsync_th);
                priv->cf.fsync_th = 0;
        }
}

/* spawn helper thread; cleaning up in case of errors */
static int
changelog_spawn_helper_threads (xlator_t *this, changelog_priv_t *priv)
{
        int ret = 0;

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

        priv->cr.notify = _gf_false;
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

int
notify (xlator_t *this, int event, void *data, ...)
{
        changelog_priv_t       *priv            = NULL;
        dict_t                 *dict            = NULL;
        char                    buf[1]          = {1};
        int                     barrier         = DICT_DEFAULT;
        gf_boolean_t            bclean_req      = _gf_false;
        int                     ret             = 0;
        int                     ret1            = 0;
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                CHANGELOG_MSG_DICT_GET_FAILED,
                                "Barrier dict_get_str_boolean failed");
                        ret = -1;
                        goto out;

                case BARRIER_OFF:
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                CHANGELOG_MSG_BARRIER_INFO,
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
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        CHANGELOG_MSG_BARRIER_ERROR,
                                        "Received another barrier off"
                                        " notification while already off");
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
                                gf_msg(this->name, GF_LOG_INFO, 0,
                                       CHANGELOG_MSG_BARRIER_INFO,
                                       "Disabled changelog barrier");
                        } else {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        CHANGELOG_MSG_BARRIER_ERROR,
                                        "Changelog barrier already disabled");
                        }

                        LOCK (&priv->bflags.lock);
                        {
                                priv->bflags.barrier_ext = _gf_false;
                        }
                        UNLOCK (&priv->bflags.lock);

                        goto out;

                case BARRIER_ON:
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                CHANGELOG_MSG_BARRIER_INFO,
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
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        CHANGELOG_MSG_BARRIER_ERROR,
                                        "Received another barrier on"
                                        "notification when last one is"
                                        "not served yet");
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

                        gf_msg(this->name, GF_LOG_INFO, 0,
                               CHANGELOG_MSG_BARRIER_INFO,
                               "Enabled changelog barrier");

                        ret = changelog_barrier_notify(priv, buf);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        CHANGELOG_MSG_WRITE_FAILED,
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
                                if (priv->bn.bnotify_error == _gf_true) {
                                        ret = -1;
                                        priv->bn.bnotify_error = _gf_false;
                                }
                        }
                        ret1 = pthread_mutex_unlock (&priv->bn.bnotify_mutex);
                        CHANGELOG_PTHREAD_ERROR_HANDLE_1 (ret1, out,
                                                          bclean_req);
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                CHANGELOG_MSG_BNOTIFY_INFO,
                                "Woke up: bnotify conditional wait");

                        goto out;

                case DICT_DEFAULT:
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                CHANGELOG_MSG_DICT_GET_FAILED,
                                "barrier key not found");
                        ret = -1;
                        goto out;

                default:
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                CHANGELOG_MSG_DICT_GET_FAILED,
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
                gf_msg (this->name, GF_LOG_WARNING, ENOMEM,
                        CHANGELOG_MSG_NO_MEMORY, "Memory accounting"
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
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_GET_TIME_OP_FAILED,
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

/**
 * Init barrier related condition variables and locks
 */
static int
changelog_barrier_pthread_init (xlator_t *this, changelog_priv_t *priv)
{
        gf_boolean_t    bn_mutex_init         = _gf_false;
        gf_boolean_t    bn_cond_init          = _gf_false;
        gf_boolean_t    dm_mutex_black_init   = _gf_false;
        gf_boolean_t    dm_cond_black_init    = _gf_false;
        gf_boolean_t    dm_mutex_white_init   = _gf_false;
        gf_boolean_t    dm_cond_white_init    = _gf_false;
        gf_boolean_t    cr_mutex_init         = _gf_false;
        gf_boolean_t    cr_cond_init          = _gf_false;
        int             ret                   = 0;

        if ((ret = pthread_mutex_init(&priv->bn.bnotify_mutex, NULL)) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_PTHREAD_MUTEX_INIT_FAILED,
                        "bnotify pthread_mutex_init failed (%d)", ret);
                ret = -1;
                goto out;
        }
        bn_mutex_init = _gf_true;

        if ((ret = pthread_cond_init(&priv->bn.bnotify_cond, NULL)) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_PTHREAD_COND_INIT_FAILED,
                        "bnotify pthread_cond_init failed (%d)", ret);
                ret = -1;
                goto out;
        }
        bn_cond_init = _gf_true;

        if ((ret = pthread_mutex_init(&priv->dm.drain_black_mutex, NULL)) != 0)
        {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_PTHREAD_MUTEX_INIT_FAILED,
                        "drain_black pthread_mutex_init failed (%d)", ret);
                ret = -1;
                goto out;
        }
        dm_mutex_black_init = _gf_true;

        if ((ret = pthread_cond_init(&priv->dm.drain_black_cond, NULL)) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_PTHREAD_COND_INIT_FAILED,
                        "drain_black pthread_cond_init failed (%d)", ret);
                ret = -1;
                goto out;
        }
        dm_cond_black_init = _gf_true;

        if ((ret = pthread_mutex_init(&priv->dm.drain_white_mutex, NULL)) != 0)
        {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_PTHREAD_MUTEX_INIT_FAILED,
                        "drain_white pthread_mutex_init failed (%d)", ret);
                ret = -1;
                goto out;
        }
        dm_mutex_white_init = _gf_true;

        if ((ret = pthread_cond_init(&priv->dm.drain_white_cond, NULL)) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_PTHREAD_COND_INIT_FAILED,
                        "drain_white pthread_cond_init failed (%d)", ret);
                ret = -1;
                goto out;
        }
        dm_cond_white_init = _gf_true;

        if ((pthread_mutex_init(&priv->cr.lock, NULL)) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_PTHREAD_MUTEX_INIT_FAILED,
                        "changelog_rollover lock init failed (%d)", ret);
                ret = -1;
                goto out;
        }
        cr_mutex_init = _gf_true;

        if ((pthread_cond_init(&priv->cr.cond, NULL)) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        CHANGELOG_MSG_PTHREAD_COND_INIT_FAILED,
                        "changelog_rollover cond init failed (%d)", ret);
                ret = -1;
                goto out;
        }
        cr_cond_init = _gf_true;
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
                if (cr_mutex_init)
                        pthread_mutex_destroy(&priv->cr.lock);
                if (cr_cond_init)
                        pthread_cond_destroy (&priv->cr.cond);
        }
        return ret;
}

/* Destroy barrier related condition variables and locks */
static void
changelog_barrier_pthread_destroy (changelog_priv_t *priv)
{
        pthread_mutex_destroy (&priv->bn.bnotify_mutex);
        pthread_cond_destroy (&priv->bn.bnotify_cond);
        pthread_mutex_destroy (&priv->dm.drain_black_mutex);
        pthread_cond_destroy (&priv->dm.drain_black_cond);
        pthread_mutex_destroy (&priv->dm.drain_white_mutex);
        pthread_cond_destroy (&priv->dm.drain_white_cond);
        pthread_mutex_destroy(&priv->cr.lock);
        pthread_cond_destroy (&priv->cr.cond);
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_DIR_OPTIONS_NOT_SET,
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

        GF_OPTION_RECONF ("capture-del-path", priv->capture_del_path, options,
                          bool, out);

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
                                gf_msg (this->name, GF_LOG_INFO, 0,
                                        CHANGELOG_MSG_HTIME_INFO,
                                        "Reconfigure: Changelog Enable");
                                if (gettimeofday(&tv, NULL) ) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                CHANGELOG_MSG_HTIME_ERROR,
                                                 "unable to fetch htime");
                                        ret = -1;
                                        goto out;
                                }
                                htime_create (this, priv, tv.tv_sec);
                        }
                        ret = changelog_spawn_helper_threads (this, priv);
                }
        }

 out:
        if (ret) {
                /* TODO */
        } else {
                gf_msg_debug (this->name, 0,
                              "changelog reconfigured");
                if (active_now && priv)
                        priv->active = _gf_true;
        }

        return ret;
}

static void
changelog_freeup_options (xlator_t *this, changelog_priv_t *priv)
{
        int ret = 0;

        ret = priv->cb->dtor (this, &priv->cd);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_FREEUP_FAILED,
                        "could not cleanup bootstrapper");
        GF_FREE (priv->changelog_brick);
        GF_FREE (priv->changelog_dir);
}

static int
changelog_init_options (xlator_t *this, changelog_priv_t *priv)
{
        int       ret            = 0;
        char     *tmp            = NULL;
        uint32_t  timeout        = 0;
        char htime_dir[PATH_MAX] = {0,};
        char csnap_dir[PATH_MAX] = {0,};

        GF_OPTION_INIT ("changelog-brick", tmp, str, error_return);
        priv->changelog_brick = gf_strdup (tmp);
        if (!priv->changelog_brick)
                goto error_return;

        tmp = NULL;

        GF_OPTION_INIT ("changelog-dir", tmp, str, dealloc_1);
        priv->changelog_dir = gf_strdup (tmp);
        if (!priv->changelog_dir)
                goto dealloc_1;

        tmp = NULL;

        /**
         * create the directory even if change-logging would be inactive
         * so that consumers can _look_ into it (finding nothing...)
         */
        ret = mkdir_p (priv->changelog_dir, 0600, _gf_true);

        if (ret)
                goto dealloc_2;

        CHANGELOG_FILL_HTIME_DIR (priv->changelog_dir, htime_dir);
        ret = mkdir_p (htime_dir, 0600, _gf_true);
        if (ret)
                goto dealloc_2;

        CHANGELOG_FILL_CSNAP_DIR (priv->changelog_dir, csnap_dir);
        ret = mkdir_p (csnap_dir, 0600, _gf_true);
        if (ret)
                goto dealloc_2;

        GF_OPTION_INIT ("changelog", priv->active, bool, dealloc_2);
        GF_OPTION_INIT ("capture-del-path", priv->capture_del_path,
                        bool, dealloc_2);

        GF_OPTION_INIT ("op-mode", tmp, str, dealloc_2);
        changelog_assign_opmode (priv, tmp);

        tmp = NULL;

        GF_OPTION_INIT ("encoding", tmp, str, dealloc_2);
        changelog_assign_encoding (priv, tmp);
        changelog_encode_change (priv);

        GF_OPTION_INIT ("rollover-time",
                        priv->rollover_time, int32, dealloc_2);

        GF_OPTION_INIT ("fsync-interval",
                        priv->fsync_interval, int32, dealloc_2);

        GF_OPTION_INIT ("changelog-barrier-timeout",
                        timeout, time, dealloc_2);
        changelog_assign_barrier_timeout (priv, timeout);

        GF_ASSERT (cb_bootstrap[priv->op_mode].mode == priv->op_mode);
        priv->cb = &cb_bootstrap[priv->op_mode];

        /* ... now bootstrap the logger */
        ret = priv->cb->ctor (this, &priv->cd);
        if (ret)
                goto dealloc_2;

        priv->changelog_fd = -1;

        return 0;

 dealloc_2:
        GF_FREE (priv->changelog_dir);
 dealloc_1:
        GF_FREE (priv->changelog_brick);
 error_return:
        return -1;
}

static void
changelog_cleanup_rpc (xlator_t *this, changelog_priv_t *priv)
{
        /* terminate rpc server */
        changelog_destroy_rpc_listner (this, priv);

        /* cleanup rot buffs */
        rbuf_dtor (priv->rbuf);

        /* cleanup poller thread */
        if (priv->poller)
                (void) changelog_thread_cleanup (this, priv->poller);
}

static int
changelog_init_rpc (xlator_t *this, changelog_priv_t *priv)
{
        rpcsvc_t  *rpc      = NULL;
        changelog_ev_selector_t *selection = NULL;

        selection = &priv->ev_selection;

        /* initialize event selection */
        changelog_init_event_selection (this, selection);

        priv->rbuf = rbuf_init (NR_ROTT_BUFFS);
        if (!priv->rbuf)
                goto cleanup_thread;

        rpc = changelog_init_rpc_listener (this, priv,
                                          priv->rbuf, NR_DISPATCHERS);
        if (!rpc)
                goto cleanup_rbuf;
        priv->rpc = rpc;

        return 0;

 cleanup_rbuf:
        rbuf_dtor (priv->rbuf);
 cleanup_thread:
        if (priv->poller)
                (void) changelog_thread_cleanup (this, priv->poller);

        return -1;
}

int32_t
init (xlator_t *this)
{
        int               ret  = -1;
        changelog_priv_t *priv = NULL;

        GF_VALIDATE_OR_GOTO ("changelog", this, error_return);

        if (!this->children || this->children->next) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_CHILD_MISCONFIGURED,
                        "translator needs a single subvolume");
                goto error_return;
        }

        if (!this->parents) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        CHANGELOG_MSG_VOL_MISCONFIGURED,
                        "dangling volume. please check volfile");
                goto error_return;
        }

        priv = GF_CALLOC (1, sizeof (*priv), gf_changelog_mt_priv_t);
        if (!priv)
                goto error_return;

        this->local_pool = mem_pool_new (changelog_local_t, 64);
        if (!this->local_pool) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        CHANGELOG_MSG_NO_MEMORY,
                        "failed to create local memory pool");
                goto cleanup_priv;
        }

        LOCK_INIT (&priv->lock);
        LOCK_INIT (&priv->c_snap_lock);

        ret = changelog_init_options (this, priv);
        if (ret)
                goto cleanup_mempool;

        /* snap dependency changes */
        priv->dm.black_fop_cnt = 0;
        priv->dm.white_fop_cnt = 0;
        priv->dm.drain_wait_black = _gf_false;
        priv->dm.drain_wait_white = _gf_false;
        priv->current_color = FOP_COLOR_BLACK;
        priv->explicit_rollover = _gf_false;

        priv->cr.notify = _gf_false;
        /* Mutex is not needed as threads are not spawned yet */
        priv->bn.bnotify = _gf_false;
        priv->bn.bnotify_error = _gf_false;
        ret = changelog_barrier_pthread_init (this, priv);
        if (ret)
                goto cleanup_options;
        LOCK_INIT (&priv->bflags.lock);
        priv->bflags.barrier_ext = _gf_false;

        /* Changelog barrier init */
        INIT_LIST_HEAD (&priv->queue);
        priv->barrier_enabled = _gf_false;

        /* RPC ball rolling.. */
        ret = changelog_init_rpc (this, priv);
        if (ret)
                goto cleanup_barrier;

        ret = changelog_init (this, priv);
        if (ret)
                goto cleanup_rpc;

        gf_msg_debug (this->name, 0, "changelog translator loaded");

        this->private = priv;
        return 0;

 cleanup_rpc:
        changelog_cleanup_rpc (this, priv);
 cleanup_barrier:
        changelog_barrier_pthread_destroy (priv);
 cleanup_options:
        changelog_freeup_options (this, priv);
 cleanup_mempool:
        mem_pool_destroy (this->local_pool);
 cleanup_priv:
        GF_FREE (priv);
 error_return:
        this->private = NULL;
        return -1;
}

void
fini (xlator_t *this)
{
        changelog_priv_t *priv = NULL;

        priv = this->private;

        if (priv) {
                /* terminate RPC server/threads */
                changelog_cleanup_rpc (this, priv);

                /* cleanup barrier related objects */
                changelog_barrier_pthread_destroy (priv);

                /* cleanup allocated options */
                changelog_freeup_options (this, priv);

                /* deallocate mempool */
                mem_pool_destroy (this->local_pool);
                /* finally, dealloac private variable */
                GF_FREE (priv);
        }

        this->private = NULL;

        return;
}

struct xlator_fops fops = {
        .open         = changelog_open,
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
        .ipc          = changelog_ipc,
        .xattrop      = changelog_xattrop,
        .fxattrop     = changelog_fxattrop,
};

struct xlator_cbks cbks = {
        .forget = changelog_forget,
        .release = changelog_release,
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
        {.key = {"capture-del-path"},
         .type = GF_OPTION_TYPE_BOOL,
         .default_value = "off",
         .description = "enable/disable capturing paths of deleted entries"
        },
        {.key = {NULL}
        },
};
