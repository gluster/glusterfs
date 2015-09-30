/*
  Copyright (c) 2012 DataLab, s.l. <http://www.datalab.es>

  This file is part of the cluster/ec translator for GlusterFS.

  The cluster/ec translator for GlusterFS is free software: you can
  redistribute it and/or modify it under the terms of the GNU General
  Public License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.

  The cluster/ec translator for GlusterFS is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE. See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with the cluster/ec translator for GlusterFS. If not, see
  <http://www.gnu.org/licenses/>.
*/

#include "ec-mem-types.h"
#include "ec-helpers.h"
#include "ec-common.h"
#include "ec-data.h"

ec_cbk_data_t * ec_cbk_data_allocate(call_frame_t * frame, xlator_t * this,
                                     ec_fop_data_t * fop, int32_t id,
                                     int32_t idx, int32_t op_ret,
                                     int32_t op_errno)
{
    ec_cbk_data_t * cbk;
    ec_t * ec = this->private;

    if (fop->xl != this)
    {
        gf_log(this->name, GF_LOG_ERROR, "Mismatching xlators between request "
                                         "and answer (req=%s, ans=%s).",
                                         fop->xl->name, this->name);

        return NULL;
    }
    if (fop->frame != frame)
    {
        gf_log(this->name, GF_LOG_ERROR, "Mismatching frames between request "
                                         "and answer (req=%p, ans=%p).",
                                         fop->frame, frame);

        return NULL;
    }
    if (fop->id != id)
    {
        gf_log(this->name, GF_LOG_ERROR, "Mismatching fops between request "
                                         "and answer (req=%d, ans=%d).",
                                         fop->id, id);

        return NULL;
    }

    cbk = mem_get0(ec->cbk_pool);
    if (cbk == NULL)
    {
        gf_log(this->name, GF_LOG_ERROR, "Failed to allocate memory for an "
                                         "answer.");
    }

    cbk->fop = fop;
    cbk->idx = idx;
    cbk->mask = 1ULL << idx;
    cbk->count = 1;
    cbk->op_ret = op_ret;
    cbk->op_errno = op_errno;

    LOCK(&fop->lock);

    list_add_tail(&cbk->answer_list, &fop->answer_list);

    UNLOCK(&fop->lock);

    return cbk;
}

void ec_cbk_data_destroy(ec_cbk_data_t * cbk)
{
    if (cbk->xdata != NULL)
    {
        dict_unref(cbk->xdata);
    }
    if (cbk->dict != NULL)
    {
        dict_unref(cbk->dict);
    }
    if (cbk->inode != NULL)
    {
        inode_unref(cbk->inode);
    }
    if (cbk->fd != NULL)
    {
        fd_unref(cbk->fd);
    }
    if (cbk->buffers != NULL)
    {
        iobref_unref(cbk->buffers);
    }
    GF_FREE(cbk->vector);

    mem_put(cbk);
}

ec_fop_data_t * ec_fop_data_allocate(call_frame_t * frame, xlator_t * this,
                                     int32_t id, uint32_t flags,
                                     uintptr_t target, int32_t minimum,
                                     ec_wind_f wind, ec_handler_f handler,
                                     ec_cbk_t cbks, void * data)
{
    ec_fop_data_t * fop, * parent;
    ec_t * ec = this->private;

    fop = mem_get0(ec->fop_pool);
    if (fop == NULL)
    {
        gf_log(this->name, GF_LOG_ERROR, "Failed to allocate memory for a "
                                         "request.");

        return NULL;
    }

    fop->xl = this;
    fop->req_frame = frame;

    /* fops need a private frame to be able to execute some postop operations
     * even if the original fop has completed and reported back to the upper
     * xlator and it has destroyed the base frame.
     *
     * TODO: minimize usage of private frames. Reuse req_frame as much as
     *       possible.
     */
    if (frame != NULL)
    {
        fop->frame = copy_frame(frame);
    }
    else
    {
        fop->frame = create_frame(this, this->ctx->pool);
    }
    if (fop->frame == NULL)
    {
        gf_log(this->name, GF_LOG_ERROR, "Failed to create a private frame "
                                         "for a request");

        mem_put(fop);

        return NULL;
    }
    fop->id = id;
    fop->refs = 1;

    fop->flags = flags;
    fop->minimum = minimum;
    fop->mask = target;

    INIT_LIST_HEAD(&fop->cbk_list);
    INIT_LIST_HEAD(&fop->answer_list);

    fop->wind = wind;
    fop->handler = handler;
    fop->cbks = cbks;
    fop->data = data;

    LOCK_INIT(&fop->lock);

    fop->frame->local = fop;

    if (frame != NULL)
    {
        parent = frame->local;
        if (parent != NULL)
        {
            LOCK(&parent->lock);

            parent->jobs++;
            parent->refs++;

            UNLOCK(&parent->lock);
        }

        fop->parent = parent;
    }

    return fop;
}

void ec_fop_data_acquire(ec_fop_data_t * fop)
{
    LOCK(&fop->lock);

    ec_trace("ACQUIRE", fop, "");

    fop->refs++;

    UNLOCK(&fop->lock);
}

void ec_fop_data_release(ec_fop_data_t * fop)
{
    ec_cbk_data_t * cbk, * tmp;
    int32_t refs;

    LOCK(&fop->lock);

    ec_trace("RELEASE", fop, "");

    refs = --fop->refs;

    UNLOCK(&fop->lock);

    if (refs == 0)
    {
        fop->frame->local = NULL;
        STACK_DESTROY(fop->frame->root);

        LOCK_DESTROY(&fop->lock);

        if (fop->xdata != NULL)
        {
            dict_unref(fop->xdata);
        }
        if (fop->dict != NULL)
        {
            dict_unref(fop->dict);
        }
        if (fop->inode != NULL)
        {
            inode_unref(fop->inode);
        }
        if (fop->fd != NULL)
        {
            fd_unref(fop->fd);
        }
        if (fop->buffers != NULL)
        {
            iobref_unref(fop->buffers);
        }
        GF_FREE(fop->vector);
        GF_FREE(fop->str[0]);
        GF_FREE(fop->str[1]);
        loc_wipe(&fop->loc[0]);
        loc_wipe(&fop->loc[1]);

        ec_resume_parent(fop, fop->error);

        list_for_each_entry_safe(cbk, tmp, &fop->answer_list, answer_list)
        {
            list_del_init(&cbk->answer_list);

            ec_cbk_data_destroy(cbk);
        }

        mem_put(fop);
    }
}
