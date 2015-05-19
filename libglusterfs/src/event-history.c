/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "event-history.h"
#include "libglusterfs-messages.h"

eh_t *
eh_new (size_t buffer_size, gf_boolean_t use_buffer_once,
        void (*destroy_buffer_data) (void *data))
{
        eh_t *history = NULL;
        buffer_t *buffer = NULL;

        history = GF_CALLOC (1, sizeof (eh_t), gf_common_mt_eh_t);
        if (!history) {
                goto out;
        }

        buffer = cb_buffer_new (buffer_size, use_buffer_once,
                                destroy_buffer_data);
        if (!buffer) {
                GF_FREE (history);
                history = NULL;
                goto out;
        }

        history->buffer = buffer;

        pthread_mutex_init (&history->lock, NULL);
out:
        return history;
}

void
eh_dump (eh_t *history, void *data,
         int (dump_fn) (circular_buffer_t *buffer, void *data))
{
        if (!history) {
                gf_msg_debug ("event-history", 0, "history is NULL");
                goto out;
        }

        cb_buffer_dump (history->buffer, data, dump_fn);

out:
        return;
}

int
eh_save_history (eh_t *history, void *data)
{
        int   ret = -1;

        ret = cb_add_entry_buffer (history->buffer, data);

        return ret;
}

int
eh_destroy (eh_t *history)
{
        if (!history) {
                gf_msg ("event-history", GF_LOG_INFO, 0, LG_MSG_INVALID_ARG,
                        "history for the xlator is NULL");
                return -1;
        }

        cb_buffer_destroy (history->buffer);
        history->buffer = NULL;

        pthread_mutex_destroy (&history->lock);

        GF_FREE (history);

        return 0;
}
