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


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

static int
event_register_poll (struct event_pool *event_pool, int fd,
		     event_handler_t handler,
		     void *data, int poll_in, int poll_out);


static int
__flush_fd (int fd, int idx, void *data,
	    int poll_in, int poll_out, int poll_err)
{
	char buf[64];
	int ret = -1;

	if (!poll_in)
		return ret;

	do {
		ret = read (fd, buf, 64);
		if (ret == -1 && errno != EAGAIN) {
			gf_log ("poll", GF_LOG_ERROR,
				"read on %d returned error (%s)",
				fd, strerror (errno));
		}
	} while (ret == 64);

	return ret;
}


static int
__event_getindex (struct event_pool *event_pool, int fd, int idx)
{
	int  ret = -1;
	int  i = 0;
  
	if (event_pool == NULL) {
		gf_log ("event", GF_LOG_ERROR, "invalid argument");
		return -1;
	}
  
	if (idx > -1 && idx < event_pool->used) {
		if (event_pool->reg[idx].fd == fd)
			ret = idx;
	}

	for (i=0; ret == -1 && i<event_pool->used; i++) {
		if (event_pool->reg[i].fd == fd) {
			ret = i;
			break;
		}
	}

	return ret;
}


static struct event_pool *
event_pool_new_poll (int count)
{
	struct event_pool *event_pool = NULL;
	int                ret = -1;

	event_pool = CALLOC (1, sizeof (*event_pool));

	if (!event_pool)
		return NULL;

	event_pool->count = count;
	event_pool->reg = CALLOC (event_pool->count,
				  sizeof (*event_pool->reg));

	if (!event_pool->reg) {
		gf_log ("poll", GF_LOG_CRITICAL,
			"failed to allocate event registry");
		free (event_pool);
		return NULL;
	}

	pthread_mutex_init (&event_pool->mutex, NULL);

	ret = pipe (event_pool->breaker);

	if (ret == -1) {
		gf_log ("poll", GF_LOG_ERROR,
			"pipe creation failed (%s)", strerror (errno));
		free (event_pool->reg);
		free (event_pool);
		return NULL;
	}

	ret = fcntl (event_pool->breaker[0], F_SETFL, O_NONBLOCK);
	if (ret == -1) {
		gf_log ("poll", GF_LOG_ERROR,
			"could not set pipe to non blocking mode (%s)",
			strerror (errno));
		close (event_pool->breaker[0]);
		close (event_pool->breaker[1]);
		event_pool->breaker[0] = event_pool->breaker[1] = -1;

		free (event_pool->reg);
		free (event_pool);
		return NULL;
	}

	ret = fcntl (event_pool->breaker[1], F_SETFL, O_NONBLOCK);
	if (ret == -1) {
		gf_log ("poll", GF_LOG_ERROR,
			"could not set pipe to non blocking mode (%s)",
			strerror (errno));

		close (event_pool->breaker[0]);
		close (event_pool->breaker[1]);
		event_pool->breaker[0] = event_pool->breaker[1] = -1;

		free (event_pool->reg);
		free (event_pool);
		return NULL;
	}

	ret = event_register_poll (event_pool, event_pool->breaker[0],
				   __flush_fd, NULL, 1, 0);
	if (ret == -1) {
		gf_log ("poll", GF_LOG_ERROR,
			"could not register pipe fd with poll event loop");
		close (event_pool->breaker[0]);
		close (event_pool->breaker[1]);
		event_pool->breaker[0] = event_pool->breaker[1] = -1;

		free (event_pool->reg);
		free (event_pool);
		return NULL;
	}

	return event_pool;
}


static int
event_register_poll (struct event_pool *event_pool, int fd,
		     event_handler_t handler,
		     void *data, int poll_in, int poll_out)
{
	int idx = -1;

	if (event_pool == NULL) {
		gf_log ("event", GF_LOG_ERROR, "invalid argument");
		return -1;
	}
  
	pthread_mutex_lock (&event_pool->mutex);
	{
		if (event_pool->count == event_pool->used)
		{
			event_pool->count += 256;

			event_pool->reg = realloc (event_pool->reg,
						   event_pool->count *
						   sizeof (*event_pool->reg));
		}

		idx = event_pool->used++;

		event_pool->reg[idx].fd = fd;
		event_pool->reg[idx].events = POLLPRI;
		event_pool->reg[idx].handler = handler;
		event_pool->reg[idx].data = data;

		switch (poll_in) {
		case 1:
			event_pool->reg[idx].events |= POLLIN;
			break;
		case 0:
			event_pool->reg[idx].events &= ~POLLIN;
			break;
		case -1:
			/* do nothing */
			break;
		default:
			gf_log ("poll", GF_LOG_ERROR,
				"invalid poll_in value %d", poll_in);
			break;
		}

		switch (poll_out) {
		case 1:
			event_pool->reg[idx].events |= POLLOUT;
			break;
		case 0:
			event_pool->reg[idx].events &= ~POLLOUT;
			break;
		case -1:
			/* do nothing */
			break;
		default:
			gf_log ("poll", GF_LOG_ERROR,
				"invalid poll_out value %d", poll_out);
			break;
		}

		event_pool->changed = 1;

	}
	pthread_mutex_unlock (&event_pool->mutex);

	return idx;
}


static int
event_unregister_poll (struct event_pool *event_pool, int fd, int idx_hint)
{
	int idx = -1;

	if (event_pool == NULL) {
		gf_log ("event", GF_LOG_ERROR, "invalid argument");
		return -1;
	}
  
	pthread_mutex_lock (&event_pool->mutex);
	{
		idx = __event_getindex (event_pool, fd, idx_hint);

		if (idx == -1) {
			gf_log ("poll", GF_LOG_ERROR,
				"index not found for fd=%d (idx_hint=%d)",
				fd, idx_hint);
			errno = ENOENT;
			goto unlock;
		}

		event_pool->reg[idx] = 	event_pool->reg[--event_pool->used];
		event_pool->changed = 1;
	}
unlock:
	pthread_mutex_unlock (&event_pool->mutex);

	return idx;
}


static int
event_select_on_poll (struct event_pool *event_pool, int fd, int idx_hint,
		      int poll_in, int poll_out)
{
	int idx = -1;

	if (event_pool == NULL) {
		gf_log ("event", GF_LOG_ERROR, "invalid argument");
		return -1;
	}
  
	pthread_mutex_lock (&event_pool->mutex);
	{
		idx = __event_getindex (event_pool, fd, idx_hint);

		if (idx == -1) {
			gf_log ("poll", GF_LOG_ERROR,
				"index not found for fd=%d (idx_hint=%d)",
				fd, idx_hint);
			errno = ENOENT;
			goto unlock;
		}

		switch (poll_in) {
		case 1:
			event_pool->reg[idx].events |= POLLIN;
			break;
		case 0:
			event_pool->reg[idx].events &= ~POLLIN;
			break;
		case -1:
			/* do nothing */
			break;
		default:
			/* TODO: log error */
			break;
		}

		switch (poll_out) {
		case 1:
			event_pool->reg[idx].events |= POLLOUT;
			break;
		case 0:
			event_pool->reg[idx].events &= ~POLLOUT;
			break;
		case -1:
			/* do nothing */
			break;
		default:
			/* TODO: log error */
			break;
		}

		if (poll_in + poll_out > -2)
			event_pool->changed = 1;
	}
unlock:
	pthread_mutex_unlock (&event_pool->mutex);

	return idx;
}


static int
event_dispatch_poll_handler (struct event_pool *event_pool,
			     struct pollfd *ufds, int i)
{
	event_handler_t  handler = NULL;
	void            *data = NULL;
	int              idx = -1;
	int              ret = 0;

	handler = NULL;
	data    = NULL;
	idx     = -1;

	pthread_mutex_lock (&event_pool->mutex);
	{
		idx = __event_getindex (event_pool, ufds[i].fd, i);

		if (idx == -1) {
			gf_log ("poll", GF_LOG_ERROR,
				"index not found for fd=%d (idx_hint=%d)",
				ufds[i].fd, i);
			goto unlock;
		}

		handler = event_pool->reg[idx].handler;
		data = event_pool->reg[idx].data;
	}
unlock:
	pthread_mutex_unlock (&event_pool->mutex);

	if (handler)
		ret = handler (ufds[i].fd, idx, data,
			       (ufds[i].revents & (POLLIN|POLLPRI)),
			       (ufds[i].revents & (POLLOUT)),
			       (ufds[i].revents & (POLLERR|POLLHUP|POLLNVAL)));

	return ret;
}


static int
event_dispatch_poll_resize (struct event_pool *event_pool,
			    struct pollfd *ufds, int size)
{
	int              i = 0;

	pthread_mutex_lock (&event_pool->mutex);
	{
		if (event_pool->changed == 0) {
			goto unlock;
		}

		if (event_pool->used > event_pool->evcache_size) {
			if (event_pool->evcache)
				free (event_pool->evcache);

			event_pool->evcache = ufds = NULL;

			event_pool->evcache_size = event_pool->used;

			ufds = CALLOC (sizeof (struct pollfd),
					       event_pool->evcache_size);
			event_pool->evcache = ufds;
		}

		for (i = 0; i < event_pool->used; i++) {
			ufds[i].fd = event_pool->reg[i].fd;
			ufds[i].events = event_pool->reg[i].events;
			ufds[i].revents = 0;
		}

		size = i;
	}
unlock:
	pthread_mutex_unlock (&event_pool->mutex);

	return size;
}


static int
event_dispatch_poll (struct event_pool *event_pool)
{
	struct pollfd   *ufds = NULL;
	int              size = 0;
	int              i = 0;
	int              ret = -1;


	if (event_pool == NULL) {
		gf_log ("event", GF_LOG_ERROR, "invalid argument");
		return -1;
	}
  
	while (1) {
		size = event_dispatch_poll_resize (event_pool, ufds, size);
		ufds = event_pool->evcache;

		ret = poll (ufds, size, 1);

		if (ret == 0)
			/* timeout */
			continue;

		if (ret == -1 && errno == EINTR)
			/* sys call */
			continue;

		for (i = 0; i < size; i++) {
			if (!ufds[i].revents)
				continue;

			event_dispatch_poll_handler (event_pool, ufds, i);
		}
	}

	return -1;
}


static struct event_ops event_ops_poll = {
	.new              = event_pool_new_poll,
	.event_register   = event_register_poll,
	.event_select_on  = event_select_on_poll,
	.event_unregister = event_unregister_poll,
	.event_dispatch   = event_dispatch_poll
};



#ifdef HAVE_SYS_EPOLL_H
#include <sys/epoll.h>


static struct event_pool *
event_pool_new_epoll (int count)
{
	struct event_pool *event_pool = NULL;
	int                epfd = -1;

	event_pool = CALLOC (1, sizeof (*event_pool));

	if (!event_pool)
		return NULL;

	event_pool->count = count;
	event_pool->reg = CALLOC (event_pool->count,
				  sizeof (*event_pool->reg));

	if (!event_pool->reg) {
		gf_log ("epoll", GF_LOG_CRITICAL,
			"event registry allocation failed");
		free (event_pool);
		return NULL;
	}

	epfd = epoll_create (count);

	if (epfd == -1) {
		gf_log ("epoll", GF_LOG_ERROR, "epoll fd creation failed (%s)",
			strerror (errno));
		free (event_pool->reg);
		free (event_pool);
		return NULL;
	}

	event_pool->fd = epfd;

	event_pool->count = count;

	pthread_mutex_init (&event_pool->mutex, NULL);
	pthread_cond_init (&event_pool->cond, NULL);

	return event_pool;
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


	if (event_pool == NULL) {
		gf_log ("event", GF_LOG_ERROR, "invalid argument");
		return -1;
	}

	pthread_mutex_lock (&event_pool->mutex);
	{
		if (event_pool->count == event_pool->used) {
			event_pool->count *= 2;

			event_pool->reg = realloc (event_pool->reg,
						   event_pool->count *
						   sizeof (*event_pool->reg));

			if (!event_pool->reg) {
				gf_log ("epoll", GF_LOG_ERROR,
					"event registry re-allocation failed");
				goto unlock;
			}
		}

		idx = event_pool->used;
		event_pool->used++;

		event_pool->reg[idx].fd = fd;
		event_pool->reg[idx].events = EPOLLPRI;
		event_pool->reg[idx].handler = handler;
		event_pool->reg[idx].data = data;

		switch (poll_in) {
		case 1:
			event_pool->reg[idx].events |= EPOLLIN;
			break;
		case 0:
			event_pool->reg[idx].events &= ~EPOLLIN;
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
			event_pool->reg[idx].events |= EPOLLOUT;
			break;
		case 0:
			event_pool->reg[idx].events &= ~EPOLLOUT;
			break;
		case -1:
			/* do nothing */
			break;
		default:
			gf_log ("epoll", GF_LOG_ERROR,
				"invalid poll_out value %d", poll_out);
			break;
		}

		event_pool->changed = 1;

		epoll_event.events = event_pool->reg[idx].events;
		ev_data->fd = fd;
		ev_data->idx = idx;

		ret = epoll_ctl (event_pool->fd, EPOLL_CTL_ADD, fd,
				 &epoll_event);

		if (ret == -1) {
			gf_log ("epoll", GF_LOG_ERROR,
				"failed to add fd(=%d) to epoll fd(=%d) (%s)",
				fd, event_pool->fd, strerror (errno));
			goto unlock;
		}

		pthread_cond_broadcast (&event_pool->cond);
	}
unlock:
	pthread_mutex_unlock (&event_pool->mutex);

	return ret;
}


static int
event_unregister_epoll (struct event_pool *event_pool, int fd, int idx_hint)
{
	int  idx = -1;
	int  ret = -1;

	struct epoll_event epoll_event = {0, };
	struct event_data *ev_data = (void *)&epoll_event.data;
	int                lastidx = -1;

	if (event_pool == NULL) {
		gf_log ("event", GF_LOG_ERROR, "invalid argument");
		return -1;
	}
  
	pthread_mutex_lock (&event_pool->mutex);
	{
		idx = __event_getindex (event_pool, fd, idx_hint);

		if (idx == -1) {
			gf_log ("epoll", GF_LOG_ERROR,
				"index not found for fd=%d (idx_hint=%d)",
				fd, idx_hint);
			errno = ENOENT;
			goto unlock;
		}

		ret = epoll_ctl (event_pool->fd, EPOLL_CTL_DEL, fd, NULL);

		/* if ret is -1, this array member should never be accessed */
		/* if it is 0, the array member might be used by idx_cache
		 * in which case the member should not be accessed till
		 * it is reallocated
		 */

		event_pool->reg[idx].fd = -1;

		if (ret == -1) {
			gf_log ("epoll", GF_LOG_ERROR,
				"fail to del fd(=%d) from epoll fd(=%d) (%s)",
				fd, event_pool->fd, strerror (errno));
			goto unlock;
		}

		lastidx = event_pool->used - 1;
		if (lastidx == idx) {
			event_pool->used--;
			goto unlock;
		}

		epoll_event.events = event_pool->reg[lastidx].events;
		ev_data->fd = event_pool->reg[lastidx].fd;
		ev_data->idx = idx;

		ret = epoll_ctl (event_pool->fd, EPOLL_CTL_MOD, ev_data->fd,
				 &epoll_event);
		if (ret == -1) {
			gf_log ("epoll", GF_LOG_ERROR,
				"fail to modify fd(=%d) index %d to %d (%s)",
				ev_data->fd, event_pool->used, idx,
				strerror (errno));
			goto unlock;
		}

		/* just replace the unregistered idx by last one */
		event_pool->reg[idx] = event_pool->reg[lastidx];
		event_pool->used--;
	}
unlock:
	pthread_mutex_unlock (&event_pool->mutex);

	return ret;
}


static int
event_select_on_epoll (struct event_pool *event_pool, int fd, int idx_hint,
		       int poll_in, int poll_out)
{
	int idx = -1;
	int ret = -1;

	struct epoll_event epoll_event = {0, };
	struct event_data *ev_data = (void *)&epoll_event.data;


	if (event_pool == NULL) {
		gf_log ("event", GF_LOG_ERROR, "invalid argument");
		return -1;
	}
  
	pthread_mutex_lock (&event_pool->mutex);
	{
		idx = __event_getindex (event_pool, fd, idx_hint);

		if (idx == -1) {
			gf_log ("epoll", GF_LOG_ERROR,
				"index not found for fd=%d (idx_hint=%d)",
				fd, idx_hint);
			errno = ENOENT;
			goto unlock;
		}

		switch (poll_in) {
		case 1:
			event_pool->reg[idx].events |= EPOLLIN;
			break;
		case 0:
			event_pool->reg[idx].events &= ~EPOLLIN;
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
			event_pool->reg[idx].events |= EPOLLOUT;
			break;
		case 0:
			event_pool->reg[idx].events &= ~EPOLLOUT;
			break;
		case -1:
			/* do nothing */
			break;
		default:
			gf_log ("epoll", GF_LOG_ERROR,
				"invalid poll_out value %d", poll_out);
			break;
		}

		epoll_event.events = event_pool->reg[idx].events;
		ev_data->fd = fd;
		ev_data->idx = idx;

		ret = epoll_ctl (event_pool->fd, EPOLL_CTL_MOD, fd,
				 &epoll_event);
		if (ret == -1) {
			gf_log ("epoll", GF_LOG_ERROR,
				"failed to modify fd(=%d) events to %d",
				fd, epoll_event.events);
		}
	}
unlock:
	pthread_mutex_unlock (&event_pool->mutex);

	return ret;
}


static int
event_dispatch_epoll_handler (struct event_pool *event_pool,
			      struct epoll_event *events, int i)
{
	struct event_data  *event_data = NULL;
	event_handler_t     handler = NULL;
	void               *data = NULL;
	int                 idx = -1;
	int                 ret = -1;


	event_data = (void *)&events[i].data;
	handler = NULL;
	data = NULL;
	idx = -1;

	pthread_mutex_lock (&event_pool->mutex);
	{
		idx = __event_getindex (event_pool, event_data->fd,
					event_data->idx);

		if (idx == -1) {
			gf_log ("epoll", GF_LOG_ERROR,
				"index not found for fd(=%d) (idx_hint=%d)",
				event_data->fd, event_data->idx);
			goto unlock;
		}

		handler = event_pool->reg[idx].handler;
		data = event_pool->reg[idx].data;
	}
unlock:
	pthread_mutex_unlock (&event_pool->mutex);

	if (handler)
		ret = handler (event_data->fd, event_data->idx, data,
			       (events[i].events & (EPOLLIN|EPOLLPRI)),
			       (events[i].events & (EPOLLOUT)),
			       (events[i].events & (EPOLLERR|EPOLLHUP)));
	return ret;
}


static int
event_dispatch_epoll (struct event_pool *event_pool)
{
	struct epoll_event *events = NULL;
	int                 size = 0;
	int                 i = 0;
	int                 ret = -1;


	if (event_pool == NULL) {
		gf_log ("event", GF_LOG_ERROR, "invalid argument");
		return -1;
	}
  
	while (1) {
		pthread_mutex_lock (&event_pool->mutex);
		{
			while (event_pool->used == 0)
				pthread_cond_wait (&event_pool->cond,
						   &event_pool->mutex);

			if (event_pool->used > event_pool->evcache_size) {
				if (event_pool->evcache)
					free (event_pool->evcache);

				event_pool->evcache = events = NULL;

				event_pool->evcache_size =
					event_pool->used + 256;

				events = CALLOC (event_pool->evcache_size,
						 sizeof (struct epoll_event));

				event_pool->evcache = events;
			}
		}
		pthread_mutex_unlock (&event_pool->mutex);

		ret = epoll_wait (event_pool->fd, event_pool->evcache,
				  event_pool->evcache_size, -1);

		if (ret == 0)
			/* timeout */
			continue;

		if (ret == -1 && errno == EINTR)
			/* sys call */
			continue;

		size = ret;

		for (i = 0; i < size; i++) {
			if (!events[i].events)
				continue;

			ret = event_dispatch_epoll_handler (event_pool,
							    events, i);
		}
	}

	return -1;
}


static struct event_ops event_ops_epoll = {
	.new              = event_pool_new_epoll,
	.event_register   = event_register_epoll,
	.event_select_on  = event_select_on_epoll,
	.event_unregister = event_unregister_epoll,
	.event_dispatch   = event_dispatch_epoll
};

#endif


struct event_pool *
event_pool_new (int count)
{
	struct event_pool *event_pool = NULL;

#ifdef HAVE_SYS_EPOLL_H
	event_pool = event_ops_epoll.new (count);

	if (event_pool) {
		event_pool->ops = &event_ops_epoll;
	} else {
		gf_log ("event", GF_LOG_WARNING,
			"failing back to poll based event handling");
	}
#endif

	if (!event_pool) {
		event_pool = event_ops_poll.new (count);

		if (event_pool)
			event_pool->ops = &event_ops_poll;
	}

	return event_pool;
}


int
event_register (struct event_pool *event_pool, int fd,
		event_handler_t handler,
		void *data, int poll_in, int poll_out)
{
	int ret = -1;

	if (event_pool == NULL) {
		gf_log ("event", GF_LOG_ERROR, "invalid argument");
		return -1;
	}
  
	ret = event_pool->ops->event_register (event_pool, fd, handler, data,
					       poll_in, poll_out);
	return ret;
}


int
event_unregister (struct event_pool *event_pool, int fd, int idx)
{
	int ret = -1;

	if (event_pool == NULL) {
		gf_log ("event", GF_LOG_ERROR, "invalid argument");
		return -1;
	}
  
	ret = event_pool->ops->event_unregister (event_pool, fd, idx);

	return ret;
}


int
event_select_on (struct event_pool *event_pool, int fd, int idx_hint,
		 int poll_in, int poll_out)
{
	int ret = -1;

	if (event_pool == NULL) {
		gf_log ("event", GF_LOG_ERROR, "invalid argument");
		return -1;
	}
  
	ret = event_pool->ops->event_select_on (event_pool, fd, idx_hint,
						poll_in, poll_out);
	return ret;
}


int
event_dispatch (struct event_pool *event_pool)
{
	int ret = -1;

	if (event_pool == NULL) {
		gf_log ("event", GF_LOG_ERROR, "invalid argument");
		return -1;
	}
  
	ret = event_pool->ops->event_dispatch (event_pool);

	return ret;
}
