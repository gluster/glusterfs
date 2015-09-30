/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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
} __attribute__ ((__packed__, __may_alias__));


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
