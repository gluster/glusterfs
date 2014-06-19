/*
  Copyright (c) 2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <sys/poll.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "logging.h"
#include "event.h"
#include "mem-pool.h"
#include "common-utils.h"

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif


#ifdef HAVE_SYS_EPOLL_H
#include <sys/epoll.h>


struct event_slot_epoll {
	int fd;
	int events;
	int gen;
	int ref;
	int do_close;
	int in_handler;
	void *data;
	event_handler_t handler;
	gf_lock_t lock;
};


static struct event_slot_epoll *
__event_newtable (struct event_pool *event_pool, int table_idx)
{
	struct event_slot_epoll *table = NULL;
	int                      i = -1;

	table = GF_CALLOC (sizeof (*table), EVENT_EPOLL_SLOTS,
			   gf_common_mt_ereg);
	if (!table)
		return NULL;

	for (i = 0; i < EVENT_EPOLL_SLOTS; i++) {
		table[i].fd = -1;
		LOCK_INIT (&table[i].lock);
	}

	event_pool->ereg[table_idx] = table;
	event_pool->slots_used[table_idx] = 0;

	return table;
}


static int
__event_slot_alloc (struct event_pool *event_pool, int fd)
{
        int  i = 0;
	int  table_idx = -1;
	int  gen = -1;
	struct event_slot_epoll *table = NULL;

	for (i = 0; i < EVENT_EPOLL_TABLES; i++) {
		switch (event_pool->slots_used[i]) {
		case EVENT_EPOLL_SLOTS:
			continue;
		case 0:
			if (!event_pool->ereg[i]) {
				table = __event_newtable (event_pool, i);
				if (!table)
					return -1;
			}
			break;
		default:
			table = event_pool->ereg[i];
			break;
		}

		if (table)
			/* break out of the loop */
			break;
	}

	if (!table)
		return -1;

	table_idx = i;

	for (i = 0; i < EVENT_EPOLL_SLOTS; i++) {
		if (table[i].fd == -1) {
			/* wipe everything except bump the generation */
			gen = table[i].gen;
			memset (&table[i], 0, sizeof (table[i]));
			table[i].gen = gen + 1;

			LOCK_INIT (&table[i].lock);

			table[i].fd = fd;
			event_pool->slots_used[table_idx]++;

			break;
		}
	}

	return table_idx * EVENT_EPOLL_SLOTS + i;
}


static int
event_slot_alloc (struct event_pool *event_pool, int fd)
{
	int  idx = -1;

	pthread_mutex_lock (&event_pool->mutex);
	{
		idx = __event_slot_alloc (event_pool, fd);
	}
	pthread_mutex_unlock (&event_pool->mutex);

	return idx;
}



static void
__event_slot_dealloc (struct event_pool *event_pool, int idx)
{
	int                      table_idx = 0;
	int                      offset = 0;
	struct event_slot_epoll *table = NULL;
	struct event_slot_epoll *slot = NULL;

	table_idx = idx / EVENT_EPOLL_SLOTS;
	offset = idx % EVENT_EPOLL_SLOTS;

	table = event_pool->ereg[table_idx];
	if (!table)
		return;

	slot = &table[offset];
	slot->gen++;

	slot->fd = -1;
	event_pool->slots_used[table_idx]--;

	return;
}


static void
event_slot_dealloc (struct event_pool *event_pool, int idx)
{
	pthread_mutex_lock (&event_pool->mutex);
	{
		__event_slot_dealloc (event_pool, idx);
	}
	pthread_mutex_unlock (&event_pool->mutex);

	return;
}


static struct event_slot_epoll *
event_slot_get (struct event_pool *event_pool, int idx)
{
	struct event_slot_epoll *slot = NULL;
	struct event_slot_epoll *table = NULL;
	int                      table_idx = 0;
	int                      offset = 0;

	table_idx = idx / EVENT_EPOLL_SLOTS;
	offset = idx % EVENT_EPOLL_SLOTS;

	table = event_pool->ereg[table_idx];
	if (!table)
		return NULL;

	slot = &table[offset];

	LOCK (&slot->lock);
	{
		slot->ref++;
	}
	UNLOCK (&slot->lock);

	return slot;
}


static void
event_slot_unref (struct event_pool *event_pool, struct event_slot_epoll *slot,
		  int idx)
{
	int ref = -1;
	int fd = -1;
	int do_close = 0;

	LOCK (&slot->lock);
	{
		ref = --slot->ref;
		fd = slot->fd;
		do_close = slot->do_close;
	}
	UNLOCK (&slot->lock);

	if (ref)
		/* slot still alive */
		goto done;

	event_slot_dealloc (event_pool, idx);

	if (do_close)
		close (fd);
done:
	return;
}


static struct event_pool *
event_pool_new_epoll (int count)
{
        struct event_pool *event_pool = NULL;
        int                epfd = -1;

        event_pool = GF_CALLOC (1, sizeof (*event_pool),
                                gf_common_mt_event_pool);

        if (!event_pool)
                goto out;

        epfd = epoll_create (count);

        if (epfd == -1) {
                gf_log ("epoll", GF_LOG_ERROR, "epoll fd creation failed (%s)",
                        strerror (errno));
                GF_FREE (event_pool->reg);
                GF_FREE (event_pool);
                event_pool = NULL;
                goto out;
        }

        event_pool->fd = epfd;

        event_pool->count = count;

        pthread_mutex_init (&event_pool->mutex, NULL);

out:
        return event_pool;
}


static void
__slot_update_events (struct event_slot_epoll *slot, int poll_in, int poll_out)
{
	switch (poll_in) {
	case 1:
		slot->events |= EPOLLIN;
		break;
	case 0:
		slot->events &= ~EPOLLIN;
		break;
	case -1:
		/* do nothing */
		break;
	default:
		gf_log ("epoll", GF_LOG_ERROR,
			"invalid poll_in value %d", poll_in);
		break;
	}

	switch (poll_out) {
	case 1:
		slot->events |= EPOLLOUT;
		break;
	case 0:
		slot->events &= ~EPOLLOUT;
		break;
	case -1:
		/* do nothing */
		break;
	default:
		gf_log ("epoll", GF_LOG_ERROR,
			"invalid poll_out value %d", poll_out);
		break;
	}
}


int
event_register_epoll (struct event_pool *event_pool, int fd,
                      event_handler_t handler,
                      void *data, int poll_in, int poll_out)
{
        int                 idx = -1;
        int                 ret = -1;
        struct epoll_event  epoll_event = {0, };
        struct event_data  *ev_data = (void *)&epoll_event.data;
	struct event_slot_epoll *slot = NULL;


        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

	idx = event_slot_alloc (event_pool, fd);
	if (idx == -1) {
		gf_log ("epoll", GF_LOG_ERROR,
			"could not find slot for fd=%d", fd);
		return -1;
	}

	slot = event_slot_get (event_pool, idx);

	assert (slot->fd == fd);

	LOCK (&slot->lock);
	{
		/* make epoll edge triggered and 'singleshot', which
		   means we need to re-add the fd with
		   epoll_ctl(EPOLL_CTL_MOD) after delivery of every
		   single event. This assures us that while a poller
		   thread has picked up and is processing an event,
		   another poller will not try to pick this at the same
		   time as well.
		*/

		slot->events = EPOLLPRI | EPOLLET | EPOLLONESHOT;
		slot->handler = handler;
		slot->data = data;

		__slot_update_events (slot, poll_in, poll_out);

		epoll_event.events = slot->events;
		ev_data->idx = idx;
		ev_data->gen = slot->gen;

		ret = epoll_ctl (event_pool->fd, EPOLL_CTL_ADD, fd,
				 &epoll_event);
		/* check ret after UNLOCK() to avoid deadlock in
		   event_slot_unref()
		*/
	}
	UNLOCK (&slot->lock);

	if (ret == -1) {
		gf_log ("epoll", GF_LOG_ERROR,
			"failed to add fd(=%d) to epoll fd(=%d) (%s)",
			fd, event_pool->fd, strerror (errno));

		event_slot_unref (event_pool, slot, idx);
		idx = -1;
	}

	/* keep slot->ref (do not event_slot_unref) if successful */
out:
        return idx;
}


static int
event_unregister_epoll_common (struct event_pool *event_pool, int fd,
			       int idx, int do_close)
{
        int  ret = -1;
	struct event_slot_epoll *slot = NULL;

        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

	slot = event_slot_get (event_pool, idx);

	assert (slot->fd == fd);

	LOCK (&slot->lock);
	{
                ret = epoll_ctl (event_pool->fd, EPOLL_CTL_DEL, fd, NULL);

                if (ret == -1) {
                        gf_log ("epoll", GF_LOG_ERROR,
                                "fail to del fd(=%d) from epoll fd(=%d) (%s)",
                                fd, event_pool->fd, strerror (errno));
                        goto unlock;
                }

		slot->do_close = do_close;
		slot->gen++; /* detect unregister in dispatch_handler() */
        }
unlock:
	UNLOCK (&slot->lock);

	event_slot_unref (event_pool, slot, idx); /* one for event_register() */
	event_slot_unref (event_pool, slot, idx); /* one for event_slot_get() */
out:
        return ret;
}


static int
event_unregister_epoll (struct event_pool *event_pool, int fd, int idx_hint)
{
	int ret = -1;

	ret = event_unregister_epoll_common (event_pool, fd, idx_hint, 0);

	return ret;
}


static int
event_unregister_close_epoll (struct event_pool *event_pool, int fd,
			      int idx_hint)
{
	int ret = -1;

	ret = event_unregister_epoll_common (event_pool, fd, idx_hint, 1);

	return ret;
}


static int
event_select_on_epoll (struct event_pool *event_pool, int fd, int idx,
                       int poll_in, int poll_out)
{
        int ret = -1;
	struct event_slot_epoll *slot = NULL;
        struct epoll_event epoll_event = {0, };
        struct event_data *ev_data = (void *)&epoll_event.data;


        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

	slot = event_slot_get (event_pool, idx);

	assert (slot->fd == fd);

	LOCK (&slot->lock);
	{
		__slot_update_events (slot, poll_in, poll_out);

		epoll_event.events = slot->events;
		ev_data->idx = idx;
		ev_data->gen = slot->gen;

		if (slot->in_handler)
			/* in_handler indicates at least one thread
			   executing event_dispatch_epoll_handler()
			   which will perform epoll_ctl(EPOLL_CTL_MOD)
			   anyways (because of EPOLLET)

			   This not only saves a system call, but also
			   avoids possibility of another epoll thread
			   parallely picking up the next event while the
			   ongoing handler is still in progress (and
			   resulting in unnecessary contention on
			   rpc_transport_t->mutex).
			*/
			goto unlock;

		ret = epoll_ctl (event_pool->fd, EPOLL_CTL_MOD, fd,
				 &epoll_event);
		if (ret == -1) {
			gf_log ("epoll", GF_LOG_ERROR,
				"failed to modify fd(=%d) events to %d",
				fd, epoll_event.events);
		}
	}
unlock:
	UNLOCK (&slot->lock);

	event_slot_unref (event_pool, slot, idx);

out:
        return idx;
}


static int
event_dispatch_epoll_handler (struct event_pool *event_pool,
                              struct epoll_event *event)
{
        struct event_data  *ev_data = NULL;
	struct event_slot_epoll *slot = NULL;
        event_handler_t     handler = NULL;
        void               *data = NULL;
        int                 idx = -1;
	int                 gen = -1;
        int                 ret = -1;
	int                 fd = -1;

	ev_data = (void *)&event->data;
        handler = NULL;
        data = NULL;

	idx = ev_data->idx;
	gen = ev_data->gen;

	slot = event_slot_get (event_pool, idx);

	LOCK (&slot->lock);
	{
		fd = slot->fd;
		if (fd == -1) {
			gf_log ("epoll", GF_LOG_ERROR,
				"stale fd found on idx=%d, gen=%d, events=%d, "
				"slot->gen=%d",
				idx, gen, event->events, slot->gen);
			/* fd got unregistered in another thread */
			goto pre_unlock;
		}

		if (gen != slot->gen) {
			gf_log ("epoll", GF_LOG_ERROR,
				"generation mismatch on idx=%d, gen=%d, "
				"slot->gen=%d, slot->fd=%d",
				idx, gen, slot->gen, slot->fd);
			/* slot was re-used and therefore is another fd! */
			goto pre_unlock;
		}

		handler = slot->handler;
		data = slot->data;

		slot->in_handler++;
	}
pre_unlock:
	UNLOCK (&slot->lock);

        if (!handler)
		goto out;

	ret = handler (fd, idx, data,
		       (event->events & (EPOLLIN|EPOLLPRI)),
		       (event->events & (EPOLLOUT)),
		       (event->events & (EPOLLERR|EPOLLHUP)));

	LOCK (&slot->lock);
	{
		slot->in_handler--;

		if (gen != slot->gen) {
			/* event_unregister() happened while we were
			   in handler()
			*/
			gf_log ("epoll", GF_LOG_DEBUG,
				"generation bumped on idx=%d from "
				"gen=%d to slot->gen=%d, fd=%d, "
				"slot->fd=%d",
				idx, gen, slot->gen, fd, slot->fd);
			goto post_unlock;
		}

		/* This call also picks up the changes made by another
		   thread calling event_select_on_epoll() while this
		   thread was busy in handler()
		*/
		event->events = slot->events;
		ret = epoll_ctl (event_pool->fd, EPOLL_CTL_MOD,
				 fd, event);
	}
post_unlock:
	UNLOCK (&slot->lock);
out:
	event_slot_unref (event_pool, slot, idx);

        return ret;
}


static void *
event_dispatch_epoll_worker (void *data)
{
        struct epoll_event  event;
        int                 ret = -1;
	struct event_pool  *event_pool = data;

        GF_VALIDATE_OR_GOTO ("event", event_pool, out);

	for (;;) {
                ret = epoll_wait (event_pool->fd, &event, 1, -1);

                if (ret == 0)
                        /* timeout */
                        continue;

                if (ret == -1 && errno == EINTR)
                        /* sys call */
                        continue;

		ret = event_dispatch_epoll_handler (event_pool, &event);
        }
out:
        return NULL;
}


#define GLUSTERFS_EPOLL_MAXTHREADS 2


static int
event_dispatch_epoll (struct event_pool *event_pool)
{
	int               i = 0;
	pthread_t         pollers[GLUSTERFS_EPOLL_MAXTHREADS];
	int               ret = -1;

	for (i = 0; i < GLUSTERFS_EPOLL_MAXTHREADS; i++) {
		ret = pthread_create (&pollers[i], NULL,
				      event_dispatch_epoll_worker,
				      event_pool);
	}

	for (i = 0; i < GLUSTERFS_EPOLL_MAXTHREADS; i++)
		pthread_join (pollers[i], NULL);

	return ret;
}


struct event_ops event_ops_epoll = {
        .new                    = event_pool_new_epoll,
        .event_register         = event_register_epoll,
        .event_select_on        = event_select_on_epoll,
        .event_unregister       = event_unregister_epoll,
        .event_unregister_close = event_unregister_close_epoll,
        .event_dispatch         = event_dispatch_epoll
};

#endif
