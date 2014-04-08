/*
     Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
     This file is part of GlusterFS.

     This file is licensed to you under your choice of the GNU Lesser
     General Public License, version 3 or any later version (LGPLv3 or
     later), or the GNU General Public License, version 2 (GPLv2), in all
     cases as published by the Free Software Foundation.
*/

#include "changelog-helpers.h"
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

        while ((stub = __chlog_barrier_dequeue (this, queue)))
                call_resume (stub);

        return;
}

/* Disable changelog barrier enable flag */
void
__chlog_barrier_disable (xlator_t *this, struct list_head *queue)
{
        changelog_priv_t  *priv   = this->private;

        list_splice_init (&priv->queue, queue);
        priv->queue_size = 0;
        priv->barrier_enabled = _gf_false;
}
