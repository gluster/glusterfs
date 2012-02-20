/*
  Copyright (c) 2012 Gluster, Inc. <http://www.gluster.com>
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

#include "event-history.h"

eh_t *
eh_new (size_t buffer_size, gf_boolean_t use_buffer_once)
{
        eh_t *history = NULL;
        buffer_t *buffer = NULL;

        history = GF_CALLOC (1, sizeof (eh_t), gf_common_mt_eh_t);
        if (!history) {
                gf_log ("", GF_LOG_ERROR, "allocating history failed.");
                goto out;
        }

        buffer = cb_buffer_new (buffer_size, use_buffer_once);
        if (!buffer) {
                gf_log ("", GF_LOG_ERROR, "allocating circular buffer failed");
                GF_FREE (history);
                history = NULL;
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
                gf_log ("", GF_LOG_DEBUG, "history is NULL");
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
                gf_log ("", GF_LOG_INFO, "history for the xlator is "
                        "NULL");
                return -1;
        }

        cb_buffer_destroy (history->buffer);
        history->buffer = NULL;

        pthread_mutex_destroy (&history->lock);

        GF_FREE (history);

        return 0;
}
