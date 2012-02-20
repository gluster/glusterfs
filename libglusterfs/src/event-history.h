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

#ifndef _EH_H
#define _EH_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "mem-types.h"
#include "circ-buff.h"

struct event_hist
{
        buffer_t *buffer;
        pthread_mutex_t lock;
};

typedef struct event_hist eh_t;

void
eh_dump (eh_t *event , void *data,
         int (fn) (circular_buffer_t *buffer, void *data));

eh_t *
eh_new (size_t buffer_size, gf_boolean_t use_buffer_once);

int
eh_save_history (eh_t *history, void *string);

int
eh_destroy (eh_t *history);

#endif /* _EH_H */
