/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "barrier.h"
#include "defaults.h"
#include "call-stub.h"

#include "statedump.h"

void
barrier_local_set_gfid (call_frame_t *frame, uuid_t gfid, xlator_t *this)
{
        if (gfid) {
                uuid_t *id = GF_MALLOC (sizeof (uuid_t), gf_common_mt_uuid_t);
                if (!id) {
                        gf_log (this->name, GF_LOG_WARNING, "Could not set gfid"
                                ". gfid will not be dumped in statedump file.");
                        return;
                }
                gf_uuid_copy (*id, gfid);
                frame->local = id;
        }
}

void
barrier_local_free_gfid (call_frame_t *frame)
{
        if (frame->local) {
                GF_FREE (frame->local);
                frame->local = NULL;
        }
}

int32_t
barrier_truncate_cbk_resume (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno,
                             struct iatt *prebuf, struct iatt *postbuf,
                             dict_t *xdata)
{
        barrier_local_free_gfid (frame);
        STACK_UNWIND_STRICT (truncate, frame, op_ret, op_errno, prebuf, postbuf,
                             xdata);
        return 0;
}

int32_t
barrier_ftruncate_cbk_resume (call_frame_t *frame, void *cookie, xlator_t *this,
                              int32_t op_ret, int32_t op_errno,
                              struct iatt *prebuf, struct iatt *postbuf,
                              dict_t *xdata)
{
        barrier_local_free_gfid (frame);
        STACK_UNWIND_STRICT (ftruncate, frame, op_ret, op_errno, prebuf,
                             postbuf, xdata);
        return 0;
}

int32_t
barrier_unlink_cbk_resume (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno,
                           struct iatt *preparent, struct iatt *postparent,
                           dict_t *xdata)
{
        barrier_local_free_gfid (frame);
        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno, preparent,
                             postparent, xdata);
        return 0;
}

int32_t
barrier_rmdir_cbk_resume (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno,
                          struct iatt *preparent, struct iatt *postparent,
                          dict_t *xdata)
{
        barrier_local_free_gfid (frame);
        STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno, preparent,
                             postparent, xdata);
        return 0;
}

int32_t
barrier_rename_cbk_resume (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, struct iatt *buf,
                           struct iatt *preoldparent, struct iatt *postoldparent,
                           struct iatt *prenewparent, struct iatt *postnewparent,
                           dict_t *xdata)
{
        barrier_local_free_gfid (frame);
        STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, buf, preoldparent,
                             postoldparent, prenewparent, postnewparent, xdata);
        return 0;
}

int32_t
barrier_writev_cbk_resume (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno,
                           struct iatt *prebuf, struct iatt *postbuf,
                           dict_t *xdata)
{
        barrier_local_free_gfid (frame);
        STACK_UNWIND_STRICT (writev, frame, op_ret, op_errno, prebuf, postbuf,
                             xdata);
        return 0;
}

int32_t
barrier_fsync_cbk_resume (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                          struct iatt *postbuf, dict_t *xdata)
{
        barrier_local_free_gfid (frame);
        STACK_UNWIND_STRICT (fsync, frame, op_ret, op_errno, prebuf, postbuf,
                             xdata);
        return 0;
}

int32_t
barrier_removexattr_cbk_resume (call_frame_t *frame, void *cookie,
                                xlator_t *this, int32_t op_ret,
                                int32_t op_errno, dict_t *xdata)
{
        barrier_local_free_gfid (frame);
        STACK_UNWIND_STRICT (removexattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int32_t
barrier_fremovexattr_cbk_resume (call_frame_t *frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno, dict_t *xdata)
{
        barrier_local_free_gfid (frame);
        STACK_UNWIND_STRICT (fremovexattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int32_t
barrier_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                    struct iatt *postbuf,
                    dict_t *xdata)
{
        BARRIER_FOP_CBK (writev, out, frame, this, op_ret, op_errno,
                         prebuf, postbuf, xdata);
out:
        return 0;
}

int32_t
barrier_fremovexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        BARRIER_FOP_CBK (fremovexattr, out, frame, this, op_ret, op_errno,
                         xdata);
out:
        return 0;
}

int32_t
barrier_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        BARRIER_FOP_CBK (removexattr, out, frame, this, op_ret, op_errno,
                         xdata);
out:
        return 0;
}

int32_t
barrier_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
        BARRIER_FOP_CBK (truncate, out, frame, this, op_ret, op_errno, prebuf,
                         postbuf, xdata);
out:
        return 0;
}

int32_t
barrier_ftruncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                       struct iatt *postbuf, dict_t *xdata)
{
        BARRIER_FOP_CBK (ftruncate, out, frame, this, op_ret, op_errno, prebuf,
                         postbuf, xdata);
out:
        return 0;
}

int32_t
barrier_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *buf,
                    struct iatt *preoldparent, struct iatt *postoldparent,
                    struct iatt *prenewparent, struct iatt *postnewparent,
                    dict_t *xdata)
{
        BARRIER_FOP_CBK (rename, out, frame, this, op_ret, op_errno, buf,
                         preoldparent, postoldparent, prenewparent,
                         postnewparent, xdata);
out:
        return 0;
}

int32_t
barrier_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        BARRIER_FOP_CBK (rmdir, out, frame, this, op_ret, op_errno, preparent,
                         postparent, xdata);
out:
        return 0;
}

int32_t
barrier_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                    struct iatt *postparent, dict_t *xdata)
{
        BARRIER_FOP_CBK (unlink, out, frame, this, op_ret, op_errno, preparent,
                         postparent, xdata);
out:
        return 0;
}

int32_t
barrier_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
        BARRIER_FOP_CBK (fsync, out, frame, this, op_ret, op_errno,
                         prebuf, postbuf, xdata);
out:
        return 0;
}

int32_t
barrier_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
                struct iovec *vector, int32_t count, off_t off, uint32_t flags,
                struct iobref *iobref, dict_t *xdata)
{
        if (!((flags | fd->flags) & (O_SYNC | O_DSYNC))) {
                STACK_WIND_TAIL (frame, FIRST_CHILD(this),
                                 FIRST_CHILD(this)->fops->writev,
                                 fd, vector, count, off, flags, iobref, xdata);

                return 0;
        }

        barrier_local_set_gfid (frame, fd->inode->gfid, this);
        STACK_WIND (frame, barrier_writev_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev, fd, vector, count,
                    off, flags, iobref, xdata);
        return 0;
}

int32_t
barrier_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                      const char *name, dict_t *xdata)
{
        barrier_local_set_gfid (frame, fd->inode->gfid, this);
        STACK_WIND (frame, barrier_fremovexattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fremovexattr,
                    fd, name, xdata);
        return 0;
}

int32_t
barrier_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                     const char *name, dict_t *xdata)
{
        barrier_local_set_gfid (frame, loc->inode->gfid, this);
        STACK_WIND (frame, barrier_removexattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->removexattr,
                    loc, name, xdata);
        return 0;
}

int32_t
barrier_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc,
                  off_t offset, dict_t *xdata)
{
        barrier_local_set_gfid (frame, loc->inode->gfid, this);
        STACK_WIND (frame, barrier_truncate_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->truncate,
                    loc, offset, xdata);
        return 0;
}


int32_t
barrier_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                loc_t *newloc, dict_t *xdata)
{
        barrier_local_set_gfid (frame, oldloc->inode->gfid, this);
        STACK_WIND (frame, barrier_rename_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->rename,
                    oldloc, newloc, xdata);
        return 0;
}

int
barrier_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
               dict_t *xdata)
{
        barrier_local_set_gfid (frame, loc->inode->gfid, this);
        STACK_WIND (frame, barrier_rmdir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->rmdir,
                    loc, flags, xdata);
        return 0;
}

int32_t
barrier_unlink (call_frame_t *frame, xlator_t *this,
                loc_t *loc, int xflag, dict_t *xdata)
{
        barrier_local_set_gfid (frame, loc->inode->gfid, this);
        STACK_WIND (frame, barrier_unlink_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->unlink,
                    loc, xflag, xdata);
        return 0;
}

int32_t
barrier_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd,
                   off_t offset, dict_t *xdata)
{
        barrier_local_set_gfid (frame, fd->inode->gfid, this);
        STACK_WIND (frame, barrier_ftruncate_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->ftruncate,
                    fd, offset, xdata);
        return 0;
}

int32_t
barrier_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd,
               int32_t flags, dict_t *xdata)
{
        barrier_local_set_gfid (frame, fd->inode->gfid, this);
        STACK_WIND (frame, barrier_fsync_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsync,
                    fd, flags, xdata);
        return 0;
}

call_stub_t *
__barrier_dequeue (xlator_t *this, struct list_head *queue)
{
        call_stub_t    *stub            = NULL;
        barrier_priv_t *priv            = NULL;

        priv = this->private;
        GF_ASSERT (priv);

        if (list_empty (queue))
                goto out;

        stub = list_entry (queue->next, call_stub_t, list);
        list_del_init (&stub->list);

out:
        return stub;
}

void
barrier_dequeue_all (xlator_t *this, struct list_head *queue)
{
        call_stub_t            *stub    = NULL;

        gf_log (this->name, GF_LOG_INFO, "Dequeuing all the barriered fops");

        /* TODO: Start the below task in a new thread */
        while ((stub = __barrier_dequeue (this, queue)))
                call_resume (stub);

        gf_log (this->name, GF_LOG_INFO, "Dequeuing the barriered fops is "
                                         "finished");
        return;
}

void
barrier_timeout (void *data)
{
        xlator_t               *this    = NULL;
        barrier_priv_t         *priv    = NULL;
        struct list_head        queue   = {0,};

        this = data;
        THIS = this;
        priv = this->private;

        INIT_LIST_HEAD (&queue);

        gf_log (this->name, GF_LOG_CRITICAL, "Disabling barrier because of "
                                             "the barrier timeout.");

        LOCK (&priv->lock);
        {
                __barrier_disable (this, &queue);
        }
        UNLOCK (&priv->lock);

        barrier_dequeue_all (this, &queue);

        return;
}

void
__barrier_enqueue (xlator_t *this, call_stub_t *stub)
{
        barrier_priv_t *priv    = NULL;

        priv = this->private;
        GF_ASSERT (priv);

        list_add_tail (&stub->list, &priv->queue);
        priv->queue_size++;

        return;
}

void
__barrier_disable (xlator_t *this, struct list_head *queue)
{
        GF_UNUSED int   ret     = 0;
        barrier_priv_t *priv    = NULL;

        priv = this->private;
        GF_ASSERT (priv);

        if (priv->timer) {
                ret = gf_timer_call_cancel (this->ctx, priv->timer);
                priv->timer = NULL;
        }

        list_splice_init (&priv->queue, queue);
        priv->queue_size = 0;
        priv->barrier_enabled = _gf_false;
}

int
__barrier_enable (xlator_t *this, barrier_priv_t *priv)
{
        int             ret     = -1;

        priv->timer = gf_timer_call_after (this->ctx, priv->timeout,
                                           barrier_timeout, (void *) this);
        if (!priv->timer) {
                gf_log (this->name, GF_LOG_CRITICAL, "Couldn't add barrier "
                                                     "timeout event.");
                goto out;
        }

        priv->barrier_enabled = _gf_true;
        ret = 0;
out:
        return ret;
}

int
notify (xlator_t *this, int event, void *data, ...)
{
        barrier_priv_t  *priv                   = NULL;
        dict_t          *dict                   = NULL;
        gf_boolean_t     past                   = _gf_false;
        int              ret                    = -1;
        int              barrier_enabled        = _gf_false;
        struct list_head queue                  = {0,};

        priv = this->private;
        GF_ASSERT (priv);
        INIT_LIST_HEAD (&queue);

        switch (event) {
        case GF_EVENT_TRANSLATOR_OP:
        {
                dict = data;
                barrier_enabled = dict_get_str_boolean (dict, "barrier", -1);

                if (barrier_enabled == -1) {
                        gf_log (this->name, GF_LOG_ERROR, "Could not fetch "
                                " barrier key from the dictionary.");
                        goto out;
                }

                LOCK (&priv->lock);
                {
                        past = priv->barrier_enabled;

                        switch (past) {
                        case _gf_false:
                                if (barrier_enabled) {
                                        ret = __barrier_enable (this,priv);
                                        if (ret)
                                                goto unlock;
                                } else {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "Already disabled.");
                                        goto unlock;
                                }
                                break;

                        case _gf_true:
                                if (!barrier_enabled) {
                                        __barrier_disable(this, &queue);
                                } else {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "Already enabled");
                                        goto unlock;
                                }
                                break;
                        }
                        ret = 0;
                }
unlock:
                UNLOCK (&priv->lock);

                if (!list_empty (&queue))
                        barrier_dequeue_all (this, &queue);

                break;
        }
        default:
        {
                default_notify (this, event, data);
                ret = 0;
                goto out;
        }
        }
out:
        return ret;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        barrier_priv_t  *priv                   = NULL;
        gf_boolean_t     past                   = _gf_false;
        int              ret                    = -1;
        gf_boolean_t     barrier_enabled        = _gf_false;
        uint32_t         timeout                = {0,};
        struct list_head queue                  = {0,};

        priv = this->private;
        GF_ASSERT (priv);

        GF_OPTION_RECONF ("barrier", barrier_enabled, options, bool, out);
        GF_OPTION_RECONF ("barrier-timeout", timeout, options, time, out);

        INIT_LIST_HEAD (&queue);

        LOCK (&priv->lock);
        {
                past = priv->barrier_enabled;

                switch (past) {
                case _gf_false:
                        if (barrier_enabled) {
                                ret = __barrier_enable (this, priv);
                                if (ret) {
                                        goto unlock;
                                }
                        }
                        break;

                case _gf_true:
                        if (!barrier_enabled) {
                                __barrier_disable (this, &queue);

                        }
                        break;
                }
                priv->timeout.tv_sec = timeout;
                ret = 0;
        }
unlock:
        UNLOCK (&priv->lock);

        if (!list_empty (&queue))
                barrier_dequeue_all (this, &queue);

out:
        return ret;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        ret = xlator_mem_acct_init (this, gf_barrier_mt_end + 1);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting "
                        "initialization failed.");

        return ret;
}

int
init (xlator_t *this)
{
        int                     ret     = -1;
        barrier_priv_t         *priv    = NULL;
        uint32_t                timeout = {0,};

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "'barrier' not configured with exactly one child");
                goto out;
        }

        if (!this->parents)
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile ");

        priv = GF_CALLOC (1, sizeof (*priv), gf_barrier_mt_priv_t);
        if (!priv)
                goto out;

        LOCK_INIT (&priv->lock);

        GF_OPTION_INIT ("barrier", priv->barrier_enabled, bool, out);
        GF_OPTION_INIT ("barrier-timeout", timeout, time, out);
        priv->timeout.tv_sec = timeout;

        INIT_LIST_HEAD (&priv->queue);

        if (priv->barrier_enabled) {
                ret = __barrier_enable (this, priv);
                if (ret == -1)
                        goto out;
        }

        this->private = priv;
        ret = 0;
out:
        return ret;
}

void
fini (xlator_t *this)
{
        barrier_priv_t         *priv    = NULL;
        struct list_head        queue   = {0,};

        priv = this->private;
        if (!priv)
                goto out;

        INIT_LIST_HEAD (&queue);

        gf_log (this->name, GF_LOG_INFO, "Disabling barriering and dequeuing "
                                         "all the queued fops");
        LOCK (&priv->lock);
        {
                __barrier_disable (this, &queue);
        }
        UNLOCK (&priv->lock);

        if (!list_empty (&queue))
                barrier_dequeue_all (this, &queue);

        this->private = NULL;

        LOCK_DESTROY (&priv->lock);
        GF_FREE (priv);
out:
        return;
}

static void
barrier_dump_stub (call_stub_t *stub, char *prefix)
{
        char key[GF_DUMP_MAX_BUF_LEN] = {0,};

        gf_proc_dump_build_key (key, prefix, "fop");
        gf_proc_dump_write (key, "%s", gf_fop_list[stub->fop]);

        if (stub->frame->local) {
                gf_proc_dump_build_key (key, prefix, "gfid");
                gf_proc_dump_write (key, "%s",
                                    uuid_utoa (*(uuid_t*)(stub->frame->local)));
        }
        if (stub->args.loc.path) {
                gf_proc_dump_build_key (key, prefix, "path");
                gf_proc_dump_write (key, "%s", stub->args.loc.path);
        }
        if (stub->args.loc.name) {
                gf_proc_dump_build_key (key, prefix, "name");
                gf_proc_dump_write (key, "%s", stub->args.loc.name);
        }

        return;
}

static void
__barrier_dump_queue (barrier_priv_t *priv)
{
        call_stub_t *stub = NULL;
        char key[GF_DUMP_MAX_BUF_LEN] = {0,};
        int i = 0;

        GF_VALIDATE_OR_GOTO ("barrier", priv, out);

        list_for_each_entry (stub, &priv->queue, list) {
                snprintf (key, sizeof (key), "stub.%d", i++);
                gf_proc_dump_add_section (key);
                barrier_dump_stub(stub, key);
        }

out:
        return;
}

int
barrier_dump_priv (xlator_t *this)
{
        int ret = -1;
        char key[GF_DUMP_MAX_BUF_LEN] = {0,};
        barrier_priv_t *priv = NULL;

        GF_VALIDATE_OR_GOTO ("barrier", this, out);

        priv = this->private;
        if (!priv)
                return 0;

        gf_proc_dump_build_key (key, "xlator.features.barrier", "priv");
        gf_proc_dump_add_section (key);

        LOCK (&priv->lock);
        {
                gf_proc_dump_build_key (key, "barrier", "enabled");
                gf_proc_dump_write (key, "%d", priv->barrier_enabled);
                gf_proc_dump_build_key (key, "barrier", "timeout");
                gf_proc_dump_write (key, "%"PRId64, priv->timeout.tv_sec);
                if (priv->barrier_enabled) {
                        gf_proc_dump_build_key (key, "barrier", "queue_size");
                        gf_proc_dump_write (key, "%d", priv->queue_size);
                        __barrier_dump_queue (priv);
                }
        }
        UNLOCK (&priv->lock);

out:
        return ret;
}

struct xlator_fops fops = {

        /* Barrier Class fops */
        .rmdir          = barrier_rmdir,
        .unlink         = barrier_unlink,
        .rename         = barrier_rename,
        .removexattr    = barrier_removexattr,
        .fremovexattr   = barrier_fremovexattr,
        .truncate       = barrier_truncate,
        .ftruncate      = barrier_ftruncate,
        .fsync          = barrier_fsync,

        /* Writes with only O_SYNC flag */
        .writev         = barrier_writev,
};

struct xlator_dumpops dumpops = {
        .priv = barrier_dump_priv,
};

struct xlator_cbks cbks;

struct volume_options options[] = {
        { .key  = {"barrier"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "When \"on\", blocks acknowledgements to application "
                         "for file operations such as rmdir, rename, unlink, "
                         "removexattr, fremovexattr, truncate, ftruncate, "
                         "write (with O_SYNC), fsync. It is turned \"off\" by "
                         "default."
        },
        { .key = {"barrier-timeout"},
          .type = GF_OPTION_TYPE_TIME,
          .default_value = BARRIER_TIMEOUT,
          .description = "After 'timeout' seconds since the time 'barrier' "
                         "option was set to \"on\", acknowledgements to file "
                         "operations are no longer blocked and previously "
                         "blocked acknowledgements are sent to the application"
        },
        { .key  = {NULL} },
};
