/*
     Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
     This file is part of GlusterFS.

     This file is licensed to you under your choice of the GNU Lesser
     General Public License, version 3 or any later version (LGPLv3 or
     later), or the GNU General Public License, version 2 (GPLv2), in all
     cases as published by the Free Software Foundation.
*/

#include "changelog-helpers.h"
#include "changelog-messages.h"
#include "call-stub.h"

/* Enqueue a stub*/
void
__chlog_barrier_enqueue (xlator_t *this, call_stub_t *stub)
{
        changelog_priv_t *priv    = NULL;

        priv = this->private;
        GF_ASSERT (priv);

        list_add_tail (&stub->list, &priv->queue);
        priv->queue_size++;

        return;
}

/* Dequeue a stub */
call_stub_t *
__chlog_barrier_dequeue (xlator_t *this, struct list_head *queue)
{
        call_stub_t      *stub            = NULL;
        changelog_priv_t *priv            = NULL;

        priv = this->private;
        GF_ASSERT (priv);

        if (list_empty (queue))
                goto out;

        stub = list_entry (queue->next, call_stub_t, list);
        list_del_init (&stub->list);

out:
        return stub;
}

/* Dequeue all the stubs and call corresponding resume functions */
void
chlog_barrier_dequeue_all (xlator_t *this, struct list_head *queue)
{
        call_stub_t            *stub    = NULL;

        gf_msg (this->name, GF_LOG_INFO, 0,
                CHANGELOG_MSG_BARRIER_INFO,
                "Dequeuing all the changelog barriered fops");

        while ((stub = __chlog_barrier_dequeue (this, queue)))
                call_resume (stub);

        gf_msg (this->name, GF_LOG_INFO, 0,
                CHANGELOG_MSG_BARRIER_INFO,
                "Dequeuing changelog barriered fops is finished");
        return;
}

/* Function called on changelog barrier timeout */
void
chlog_barrier_timeout (void *data)
{
        xlator_t               *this    = NULL;
        changelog_priv_t       *priv    = NULL;
        struct list_head        queue   = {0,};

        this = data;
        THIS = this;
        priv = this->private;

        INIT_LIST_HEAD (&queue);

        gf_msg (this->name, GF_LOG_ERROR, 0,
                CHANGELOG_MSG_BARRIER_ERROR,
                "Disabling changelog barrier because of the timeout.");

        LOCK (&priv->lock);
        {
                __chlog_barrier_disable (this, &queue);
        }
        UNLOCK (&priv->lock);

        chlog_barrier_dequeue_all (this, &queue);

        return;
}

/* Disable changelog barrier enable flag */
void
__chlog_barrier_disable (xlator_t *this, struct list_head *queue)
{
        changelog_priv_t  *priv   = this->private;
        GF_ASSERT (priv);

        if (priv->timer) {
                gf_timer_call_cancel (this->ctx, priv->timer);
                priv->timer = NULL;
        }

        list_splice_init (&priv->queue, queue);
        priv->queue_size = 0;
        priv->barrier_enabled = _gf_false;
}

/* Enable chagelog barrier enable with timer */
int
__chlog_barrier_enable (xlator_t *this, changelog_priv_t *priv)
{
        int             ret     = -1;

        priv->timer = gf_timer_call_after (this->ctx, priv->timeout,
                                           chlog_barrier_timeout, (void *)this);
        if (!priv->timer) {
                gf_msg (this->name, GF_LOG_CRITICAL, 0,
                        CHANGELOG_MSG_BARRIER_ERROR,
                        "Couldn't add changelog barrier timeout event.");
                goto out;
        }

        priv->barrier_enabled = _gf_true;
        ret = 0;
out:
        return ret;
}
