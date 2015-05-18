/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _EH_H
#define _EH_H

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
eh_new (size_t buffer_size, gf_boolean_t use_buffer_once,
        void (*destroy_data) (void *data));

int
eh_save_history (eh_t *history, void *string);

int
eh_destroy (eh_t *history);

#endif /* _EH_H */
