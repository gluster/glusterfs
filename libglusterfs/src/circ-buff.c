/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "circ-buff.h"
#include "libglusterfs-messages.h"

void
cb_destroy_data (circular_buffer_t *cb,
                 void (*destroy_buffer_data) (void *data))
{
        if (destroy_buffer_data)
                destroy_buffer_data (cb->data);
        GF_FREE (cb->data);
        return;
}


/* hold lock while calling this function */
int
__cb_add_entry_buffer (buffer_t *buffer, void *item)
{
        circular_buffer_t   *ptr  = NULL;
        int    ret   = -1;
        //DO we really need the assert here?
        GF_ASSERT (buffer->used_len <= buffer->size_buffer);

        if (buffer->use_once == _gf_true &&
            buffer->used_len == buffer->size_buffer) {
                gf_msg  ("circ-buff", GF_LOG_WARNING, 0, LG_MSG_BUFFER_ERROR,
                         "buffer %p is use once buffer", buffer);
                return -1;
        } else {
                if (buffer->used_len == buffer->size_buffer) {
                        if (buffer->cb[buffer->w_index]) {
                                ptr = buffer->cb[buffer->w_index];
                                if (ptr->data) {
                                        cb_destroy_data (ptr,
                                                   buffer->destroy_buffer_data);
                                        ptr->data = NULL;
                                        GF_FREE (ptr);
                                }
                                buffer->cb[buffer->w_index] = NULL;
                                ptr = NULL;
                        }
                }

                buffer->cb[buffer->w_index] =
                        GF_CALLOC (1, sizeof (circular_buffer_t),
                                   gf_common_mt_circular_buffer_t);
                if (!buffer->cb[buffer->w_index])
                        return -1;

                buffer->cb[buffer->w_index]->data = item;
                ret = gettimeofday (&buffer->cb[buffer->w_index]->tv, NULL);
                if (ret == -1)
                        gf_msg_callingfn ("circ-buff", GF_LOG_WARNING, 0,
                                          LG_MSG_GETTIMEOFDAY_FAILED,
                                          "getting time of the day failed");
                buffer->w_index++;
                buffer->w_index %= buffer->size_buffer;
                //used_buffer size cannot be greater than the total buffer size

                if (buffer->used_len < buffer->size_buffer)
                        buffer->used_len++;
                return buffer->w_index;
        }
}

int
cb_add_entry_buffer (buffer_t *buffer, void *item)
{
        int write_index = -1;

        pthread_mutex_lock (&buffer->lock);
        {
                write_index = __cb_add_entry_buffer (buffer, item);
        }
        pthread_mutex_unlock (&buffer->lock);

        return write_index;
}

void
cb_buffer_show (buffer_t *buffer)
{
        pthread_mutex_lock (&buffer->lock);
        {
                gf_msg_debug ("circ-buff", 0, "w_index: %d, size: %"
                              GF_PRI_SIZET" used_buffer: %d", buffer->w_index,
                              buffer->size_buffer, buffer->used_len);
        }
        pthread_mutex_unlock (&buffer->lock);
}

void
cb_buffer_dump (buffer_t *buffer, void *data,
                int (fn) (circular_buffer_t *buffer, void *data))
{
        int index = 0;
        circular_buffer_t *entry = NULL;
        int  entries = 0;
        int ul = 0;
        int w_ind = 0;
        int size_buff = 0;
        int i = 0;

        ul = buffer->used_len;
        w_ind = buffer->w_index;
        size_buff = buffer->size_buffer;

        pthread_mutex_lock (&buffer->lock);
        {
                if (buffer->use_once == _gf_false) {
                        index = (size_buff + (w_ind - ul))%size_buff;
                        for (entries = 0; entries < buffer->used_len;
                             entries++) {
                                entry = buffer->cb[index];
                                if (entry)
                                        fn (entry, data);
                                else
                                        gf_msg_callingfn ("circ-buff",
                                                          GF_LOG_WARNING, 0,
                                                          LG_MSG_NULL_PTR,
                                                          "Null entry in "
                                                          "circular buffer at "
                                                          "index %d.", index);

                                index++;
                                index %= buffer->size_buffer;
                        }
                } else {
                        for (i = 0; i < buffer->used_len ; i++) {
                                entry = buffer->cb[i];
                                fn (entry, data);
                        }
                }
        }
        pthread_mutex_unlock (&buffer->lock);
}

buffer_t *
cb_buffer_new (size_t buffer_size, gf_boolean_t use_once,
               void (*destroy_buffer_data) (void *data))
{
        buffer_t    *buffer = NULL;

        buffer = GF_CALLOC (1, sizeof (*buffer), gf_common_mt_buffer_t);
        if (!buffer) {
                goto out;
        }

        buffer->cb = GF_CALLOC (buffer_size,
                                sizeof (circular_buffer_t *),
                                gf_common_mt_circular_buffer_t);
        if (!buffer->cb) {
                GF_FREE (buffer);
                buffer = NULL;
                goto out;
        }

        buffer->w_index = 0;
        buffer->size_buffer = buffer_size;
        buffer->use_once = use_once;
        buffer->used_len = 0;
        buffer->destroy_buffer_data = destroy_buffer_data;
        pthread_mutex_init (&buffer->lock, NULL);

out:
        return buffer;
}

void
cb_buffer_destroy (buffer_t *buffer)
{
        int                     i = 0;
        circular_buffer_t       *ptr = NULL;
        if (buffer) {
                if (buffer->cb) {
                        for (i = 0; i < buffer->used_len ; i++) {
                                ptr = buffer->cb[i];
                                if (ptr->data) {
                                        cb_destroy_data (ptr,
                                                   buffer->destroy_buffer_data);
                                        ptr->data = NULL;
                                        GF_FREE (ptr);
                                }
                        }
                        GF_FREE (buffer->cb);
                }
                pthread_mutex_destroy (&buffer->lock);
                GF_FREE (buffer);
        }
}

