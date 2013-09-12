/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _CB_H
#define _CB_H

#include "common-utils.h"
#include "logging.h"
#include "mem-types.h"

#define BUFFER_SIZE 10
#define TOTAL_SIZE BUFFER_SIZE + 1


struct _circular_buffer {
        struct timeval tv;
        void *data;
};

typedef struct _circular_buffer circular_buffer_t;

struct _buffer {
        unsigned int w_index;
        size_t  size_buffer;
        gf_boolean_t use_once;
        /* This variable is assigned the proper value at the time of initing */
        /* the buffer. It indicates, whether the buffer should be used once */
        /*  it becomes full. */

        int  used_len;
        /* indicates the amount of circular buffer used. */

        circular_buffer_t **cb;
        void (*destroy_buffer_data) (void *data);
        pthread_mutex_t   lock;
};

typedef struct _buffer buffer_t;

int
cb_add_entry_buffer (buffer_t *buffer, void *item);

void
cb_buffer_show (buffer_t *buffer);

buffer_t *
cb_buffer_new (size_t buffer_size,gf_boolean_t use_buffer_once,
               void (*destroy_data) (void *data));

void
cb_buffer_destroy (buffer_t *buffer);

void
cb_buffer_dump (buffer_t *buffer, void *data,
                int (fn) (circular_buffer_t *buffer, void *data));

#endif /* _CB_H */
