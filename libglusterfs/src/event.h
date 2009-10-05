/*
   Copyright (c) 2008-2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _EVENT_H_
#define _EVENT_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <pthread.h>

struct event_pool;
struct event_ops;
struct event_data {
  int fd;
  int idx;
} __attribute__ ((__packed__));


typedef int (*event_handler_t) (int fd, int idx, void *data,
				int poll_in, int poll_out, int poll_err);

struct event_pool {
  struct event_ops *ops;

  int fd;
  int breaker[2];

  int count;
  struct {
    int fd;
    int events;
    void *data;
    event_handler_t handler;
  } *reg;

  int used;
  int idx_cache;
  int changed;

  pthread_mutex_t mutex;
  pthread_cond_t cond;

  void *evcache;
  int evcache_size;
};

struct event_ops {
  struct event_pool * (*new) (int count);

  int (*event_register) (struct event_pool *event_pool, int fd,
			 event_handler_t handler,
			 void *data, int poll_in, int poll_out);

  int (*event_select_on) (struct event_pool *event_pool, int fd, int idx,
			  int poll_in, int poll_out);

  int (*event_unregister) (struct event_pool *event_pool, int fd, int idx);

  int (*event_dispatch) (struct event_pool *event_pool);
};

struct event_pool * event_pool_new (int count);
int event_select_on (struct event_pool *event_pool, int fd, int idx,
		     int poll_in, int poll_out);
int event_register (struct event_pool *event_pool, int fd,
		    event_handler_t handler,
		    void *data, int poll_in, int poll_out);
int event_unregister (struct event_pool *event_pool, int fd, int idx);
int event_dispatch (struct event_pool *event_pool);

#endif /* _EVENT_H_ */
