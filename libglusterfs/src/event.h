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

#include <pthread.h>

struct event_pool;
struct event_ops;
struct event_slot_poll;
struct event_slot_epoll;
struct event_data {
	int idx;
	int gen;
} __attribute__ ((__packed__, __may_alias__));


typedef int (*event_handler_t) (int fd, int idx, void *data,
				int poll_in, int poll_out, int poll_err);

#define EVENT_EPOLL_TABLES 1024
#define EVENT_EPOLL_SLOTS 1024
#define EVENT_MAX_THREADS  1024

struct event_pool {
	struct event_ops *ops;

	int fd;
	int breaker[2];

	int count;
	struct event_slot_poll  *reg;
	struct event_slot_epoll *ereg[EVENT_EPOLL_TABLES];
	int slots_used[EVENT_EPOLL_TABLES];

	int used;
	int changed;

	pthread_mutex_t mutex;
	pthread_cond_t cond;

	void *evcache;
	int evcache_size;

        /* NOTE: Currently used only when event processing is done using
         * epoll. */
        int eventthreadcount; /* number of event threads to execute. */
        pthread_t pollers[EVENT_MAX_THREADS]; /* poller thread_id store,
                                                     * and live status */
        int destroy;
        int activethreadcount;

        /*
         * Number of threads created by auto-scaling, *in addition to* the
         * configured number of threads.  This is only applicable on the
         * server, where we try to keep the number of threads around the number
         * of bricks.  In that case, the configured number is just "extra"
         * threads to handle requests in excess of one per brick (including
         * requests on the GlusterD connection).  For clients or GlusterD, this
         * number will always be zero, so the "extra" is all we have.
         *
         * TBD: consider auto-scaling for clients as well
         */
        int auto_thread_count;

};

struct event_ops {
        struct event_pool * (*new) (int count, int eventthreadcount);

        int (*event_register) (struct event_pool *event_pool, int fd,
                               event_handler_t handler,
                               void *data, int poll_in, int poll_out);

        int (*event_select_on) (struct event_pool *event_pool, int fd, int idx,
                                int poll_in, int poll_out);

        int (*event_unregister) (struct event_pool *event_pool, int fd, int idx);

        int (*event_unregister_close) (struct event_pool *event_pool, int fd,
				       int idx);

        int (*event_dispatch) (struct event_pool *event_pool);

        int (*event_reconfigure_threads) (struct event_pool *event_pool,
                                          int newcount);
        int (*event_pool_destroy) (struct event_pool *event_pool);
};

struct event_pool *event_pool_new (int count, int eventthreadcount);
int event_select_on (struct event_pool *event_pool, int fd, int idx,
		     int poll_in, int poll_out);
int event_register (struct event_pool *event_pool, int fd,
		    event_handler_t handler,
		    void *data, int poll_in, int poll_out);
int event_unregister (struct event_pool *event_pool, int fd, int idx);
int event_unregister_close (struct event_pool *event_pool, int fd, int idx);
int event_dispatch (struct event_pool *event_pool);
int event_reconfigure_threads (struct event_pool *event_pool, int value);
int event_pool_destroy (struct event_pool *event_pool);
int event_dispatch_destroy (struct event_pool *event_pool);
#endif /* _EVENT_H_ */
