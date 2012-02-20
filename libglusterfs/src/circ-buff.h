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

        pthread_mutex_t   lock;
};

typedef struct _buffer buffer_t;

int
cb_add_entry_buffer (buffer_t *buffer, void *item);

void
cb_buffer_show (buffer_t *buffer);

buffer_t *
cb_buffer_new (size_t buffer_size,gf_boolean_t use_buffer_once);

void
cb_buffer_destroy (buffer_t *buffer);

void
cb_buffer_dump (buffer_t *buffer, void *data,
                int (fn) (circular_buffer_t *buffer, void *data));

#endif /* _CB_H */
