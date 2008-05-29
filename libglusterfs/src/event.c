/*
   Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif


static int
__flush_fd (int fd, int idx, void *data,
	    int poll_in, int poll_out, int poll_err)
{
  char buf[64];
  int ret = -1;

  if (poll_in)
    {
      do {

	ret = read (fd, buf, 64);

	if (ret == -1 && errno != EAGAIN)
	  {
	    /* TODO: log */
	  }
      } while (ret == 64);
    }

  return ret;
}


static int
__event_getindex (struct event_pool *event_pool, int fd, int idx)
{
  int ret = -1, i = 0;
  
  if (event_pool == NULL)
    {
      gf_log ("event", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  if (idx > -1 && idx < event_pool->used)
    {
      if (event_pool->reg[idx].fd == fd)
	ret = idx;
    }

  for (i=0; ret == -1 && i<event_pool->used; i++)
    {
      if (event_pool->reg[i].fd == fd)
	{
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
  int ret = -1;

  event_pool = calloc (1, sizeof (*event_pool));

  if (!event_pool)
    return NULL;

  event_pool->count = count;
  event_pool->reg = calloc (event_pool->count, sizeof (*event_pool->reg));

  if (!event_pool->reg)
    {
      free (event_pool);
      return NULL;
    }

  pthread_mutex_init (&event_pool->mutex, NULL);

  ret = pipe (event_pool->breaker);

  if (ret == -1)
    {
      /* TODO: log */
      free (event_pool->reg);
      free (event_pool);
      return NULL;
    }

  ret = fcntl (event_pool->breaker[0], F_SETFL, O_NONBLOCK);
  if (ret == -1)
    {
      /* TODO: log */
      close (event_pool->breaker[0]);
      close (event_pool->breaker[1]);
      event_pool->breaker[0] = event_pool->breaker[1] = -1;

      free (event_pool->reg);
      free (event_pool);
      return NULL;
    }

  ret = fcntl (event_pool->breaker[1], F_SETFL, O_NONBLOCK);
  if (ret == -1)
    {
      /* TODO: log */
      close (event_pool->breaker[0]);
      close (event_pool->breaker[1]);
      event_pool->breaker[0] = event_pool->breaker[1] = -1;

      free (event_pool->reg);
      free (event_pool);
      return NULL;
    }

  ret = event_register (event_pool, event_pool->breaker[0], __flush_fd,
			NULL, 1, 0);

  if (ret == -1)
    {
      close (event_pool->breaker[0]);
      close (event_pool->breaker[1]);
      event_pool->breaker[0] = event_pool->breaker[1] = -1;

      free (event_pool->reg);
      free (event_pool);
      return NULL;
    }

  return 0;
}


static int
event_register_poll (struct event_pool *event_pool, int fd,
		     event_handler_t handler,
		     void *data, int poll_in, int poll_out)
{
  int idx = -1;

  if (event_pool == NULL)
    {
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

    switch (poll_in)
      {
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

    switch (poll_out)
      {
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

    event_pool->changed = 1;

  }
  pthread_mutex_unlock (&event_pool->mutex);

  return idx;
}


static int
event_unregister_poll (struct event_pool *event_pool, int fd, int idx_hint)
{
  int idx = -1;

  if (event_pool == NULL)
    {
      gf_log ("event", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  pthread_mutex_lock (&event_pool->mutex);
  {
    idx = __event_getindex (event_pool, fd, idx_hint);

    if (idx != -1)
      {
	event_pool->reg[idx] = event_pool->reg[--event_pool->used];
      }
    else
      {
      /* TODO: log */
      }
  }
  pthread_mutex_unlock (&event_pool->mutex);

  return idx;
}


static int
event_select_on_poll (struct event_pool *event_pool, int fd, int idx_hint,
		      int poll_in, int poll_out)
{
  int idx = -1;

  if (event_pool == NULL)
    {
      gf_log ("event", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  pthread_mutex_lock (&event_pool->mutex);
  {
    idx = __event_getindex (event_pool, fd, idx_hint);

    if (idx != -1)
      {
	switch (poll_in)
	  {
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

	switch (poll_out)
	  {
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
    else
      {
	/* TODO: log */
      }
  }
  pthread_mutex_unlock (&event_pool->mutex);

  return idx;
}


static int
event_dispatch_poll (struct event_pool *event_pool)
{
  struct pollfd *ufds = NULL;
  int size = 0, i = 0;
  int ret = -1;

  if (event_pool == NULL)
    {
      gf_log ("event", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  while (1)
    {
      pthread_mutex_lock (&event_pool->mutex);
      {
	if (event_pool->changed)
	  {
	    if (event_pool->used > event_pool->evcache_size)
	      {
		if (event_pool->evcache)
		  free (event_pool->evcache);

		event_pool->evcache = ufds = NULL;

		event_pool->evcache_size = event_pool->used;

		event_pool->evcache = ufds = calloc (sizeof (struct pollfd),
						     event_pool->evcache_size);
	      }

	    for (i=0; i<event_pool->used; i++)
	      {
		ufds[i].fd = event_pool->reg[i].fd;
		ufds[i].events = event_pool->reg[i].events;
		ufds[i].revents = 0;
	      }
	    size = i;
	  }
      }
      pthread_mutex_unlock (&event_pool->mutex);

      ret = poll (ufds, size, 1);

      if (ret == 0)
	/* timeout */
	continue;

      if (ret == -1 && errno == EINTR)
	/* sys call */
	continue;

      for (i=0; i<size; i++)
	{
	  if (ufds[i].revents)
	    {
	      event_handler_t handler = NULL;
	      void *data = NULL;
	      int idx = -1;

	      pthread_mutex_lock (&event_pool->mutex);
	      {
		idx = __event_getindex (event_pool, ufds[i].fd, i);

		if (idx == -1)
		  {
		    /* TODO: log */
		    continue;
		  }

		handler = event_pool->reg[idx].handler;
		data = event_pool->reg[idx].data;
	      }
	      pthread_mutex_unlock (&event_pool->mutex);

	      handler (ufds[i].fd, idx, data,
		       (ufds[i].revents & (POLLIN|POLLPRI)),
		       (ufds[i].revents & (POLLOUT)),
		       (ufds[i].revents & (POLLERR|POLLHUP|POLLNVAL)));
	    }
	}
    }

  return -1;
}


static struct event_ops event_ops_poll = {
  .new = event_pool_new_poll,
  .event_register = event_register_poll,
  .event_select_on = event_select_on_poll,
  .event_unregister = event_unregister_poll,
  .event_dispatch = event_dispatch_poll
};



#ifdef HAVE_SYS_EPOLL_H
#include <sys/epoll.h>


static struct event_pool *
event_pool_new_epoll (int count)
{
  struct event_pool *event_pool = NULL;
  int epfd = -1;

  event_pool = calloc (1, sizeof (*event_pool));

  if (!event_pool)
    return NULL;

  event_pool->count = count;
  event_pool->reg = calloc (event_pool->count, sizeof (*event_pool->reg));

  if (!event_pool->reg)
    {
      free (event_pool);
      return NULL;
    }

  epfd = epoll_create (count);

  if (epfd == -1)
    {
      free (event_pool);
      return NULL;
    }

  event_pool->fd = epfd;
  event_pool->idx_cache = -1;

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
  int idx = -1;

  if (event_pool == NULL)
    {
      gf_log ("event", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  pthread_mutex_lock (&event_pool->mutex);
  {
    if (event_pool->count == event_pool->used)
      {
	event_pool->count *= 2;

	event_pool->reg = realloc (event_pool->reg,
				   event_pool->count *
				   sizeof (*event_pool->reg));

	if (!event_pool->reg)
	  {
	    /* TODO: log */
	    pthread_mutex_unlock (&event_pool->mutex);
	    return -1;
	  }
      }

    if (event_pool->idx_cache != -1)
      {
	idx = event_pool->idx_cache;
	event_pool->idx_cache = -1;
      }
    else
      {
	idx = event_pool->used;
      }
    event_pool->used++;

    event_pool->reg[idx].fd = fd;
    event_pool->reg[idx].events = POLLPRI;
    event_pool->reg[idx].handler = handler;
    event_pool->reg[idx].data = data;

    switch (poll_in)
      {
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

    switch (poll_out)
      {
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


    event_pool->changed = 1;

    {
      struct epoll_event epoll_event = {0, };
      struct event_data *data = (void *)&epoll_event.data;
      int ret = -1;

      epoll_event.events = event_pool->reg[idx].events;
      data->fd = fd;
      data->idx = idx;

      ret = epoll_ctl (event_pool->fd, EPOLL_CTL_ADD, fd, &epoll_event);

      if (ret == -1)
	{
	  /* TODO: log */
	  event_pool->used--;
	  pthread_mutex_unlock (&event_pool->mutex);
	  return -1;
	}
    }

    pthread_cond_broadcast (&event_pool->cond);
  }
  pthread_mutex_unlock (&event_pool->mutex);

  return idx;
}


static int
event_unregister_epoll (struct event_pool *event_pool, int fd, int idx_hint)
{
  int idx = -1;

  if (event_pool == NULL)
    {
      gf_log ("event", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  pthread_mutex_lock (&event_pool->mutex);
  {
    idx = __event_getindex (event_pool, fd, idx_hint);

    if (idx != -1)
      {
	int ret = -1;

	--event_pool->used;

	ret = epoll_ctl (event_pool->fd, EPOLL_CTL_DEL, fd, NULL);
	if (ret == -1)
	  {
	    /* TODO: log */
	  }

	if (event_pool->idx_cache == -1, 0)
	  {
	    event_pool->idx_cache = idx;
	  }
	else
	  {
	    struct epoll_event epoll_event = {0, };
	    struct event_data *data = (void *)&epoll_event.data;

	    event_pool->reg[idx] = event_pool->reg[event_pool->used];

	    epoll_event.events = event_pool->reg[idx].events;
	    data->fd = event_pool->reg[idx].fd;
	    data->idx = idx;

	    ret = epoll_ctl (event_pool->fd, EPOLL_CTL_MOD, data->fd, &epoll_event);
	    if (ret == -1)
	      {
		/* TODO: log */
	      }
	  }
      }
    else
      {
	/* TODO: log */
      }
  }
  pthread_mutex_unlock (&event_pool->mutex);

  return idx;
}


static int
event_select_on_epoll (struct event_pool *event_pool, int fd, int idx_hint,
		       int poll_in, int poll_out)
{
  int idx = -1;

  if (event_pool == NULL)
    {
      gf_log ("event", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  pthread_mutex_lock (&event_pool->mutex);
  {
    idx = __event_getindex (event_pool, fd, idx_hint);

    if (idx != -1)
      {
	int ret = -1;
	struct epoll_event epoll_event = {0, };
	struct event_data *data = (void *)&epoll_event.data;

	switch (poll_in)
	  {
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

	switch (poll_out)
	  {
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

	epoll_event.events = event_pool->reg[idx].events;
	data->fd = fd;
	data->idx = idx;

	ret = epoll_ctl (event_pool->fd, EPOLL_CTL_MOD, fd, &epoll_event);
	if (ret == -1)
	  {
	    /* TODO: log */
	    idx = ret;
	  }
      }
    else
      {
	/* TODO: log */
      }
  }
  pthread_mutex_unlock (&event_pool->mutex);

  return idx;
}


static int
event_dispatch_epoll (struct event_pool *event_pool)
{
  struct epoll_event *events = NULL;
  int size = 0, i = 0;
  int ret = -1;

  if (event_pool == NULL)
    {
      gf_log ("event", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  while (1)
    {
      pthread_mutex_lock (&event_pool->mutex);
      {
	while (event_pool->used == 0)
	  pthread_cond_wait (&event_pool->cond, &event_pool->mutex);

	if (event_pool->used > event_pool->evcache_size)
	  {
	    if (event_pool->evcache)
	      free (event_pool->evcache);

	    event_pool->evcache = events = NULL;

	    event_pool->evcache_size = event_pool->used + 256;

	    event_pool->evcache = events = calloc (sizeof (struct epoll_event),
						   event_pool->evcache_size);
	  }
      }
      pthread_mutex_unlock (&event_pool->mutex);

      ret = epoll_wait (event_pool->fd, event_pool->evcache,
			event_pool->evcache_size, 1);

      if (ret == 0)
	/* timeout */
	continue;

      if (ret == -1 && errno == EINTR)
	/* sys call */
	continue;

      size = ret;

      for (i=0; i<size; i++)
	{
	  if (events[i].events)
	    {
	      struct event_data *event_data = (void *)&events[i].data;
	      event_handler_t handler = NULL;
	      void *data = NULL;
	      int idx = -1;

	      pthread_mutex_lock (&event_pool->mutex);
	      {
		idx = __event_getindex (event_pool, event_data->fd,
					event_data->idx);

		if (idx == -1)
		  {
		    /* TODO: log */
		    pthread_mutex_unlock (&event_pool->mutex);
		    continue;
		  }

		handler = event_pool->reg[idx].handler;
		data = event_pool->reg[idx].data;
	      }
	      pthread_mutex_unlock (&event_pool->mutex);

	      handler (event_data->fd, event_data->idx, data,
		       (events[i].events & (EPOLLIN|EPOLLPRI)),
		       (events[i].events & (EPOLLOUT)),
		       (events[i].events & (EPOLLERR|EPOLLHUP)));
	    }
	}
    }

  return -1;
}


static struct event_ops event_ops_epoll = {
  .new = event_pool_new_epoll,
  .event_register = event_register_epoll,
  .event_select_on = event_select_on_epoll,
  .event_unregister = event_unregister_epoll,
  .event_dispatch = event_dispatch_epoll
};

#endif


struct event_pool *
event_pool_new (int count)
{
  struct event_pool *event_pool = NULL;

#ifdef HAVE_SYS_EPOLL_H
  event_pool = event_ops_epoll.new (count);

  if (event_pool)
    {
      event_pool->ops = &event_ops_epoll;
    }
  else
    {
      /* TODO: log */
    }
#endif

  if (!event_pool)
    {
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
  if (event_pool == NULL)
    {
      gf_log ("event", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  return event_pool->ops->event_register (event_pool, fd, handler, data,
					  poll_in, poll_out);
}


int
event_unregister (struct event_pool *event_pool, int fd, int idx)
{
  if (event_pool == NULL)
    {
      gf_log ("event", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  return event_pool->ops->event_unregister (event_pool, fd, idx);
}


int
event_select_on (struct event_pool *event_pool, int fd, int idx_hint,
		 int poll_in, int poll_out)
{
  if (event_pool == NULL)
    {
      gf_log ("event", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  return event_pool->ops->event_select_on (event_pool, fd, idx_hint,
					   poll_in, poll_out);
}

int
event_dispatch (struct event_pool *event_pool)
{
  if (event_pool == NULL)
    {
      gf_log ("event", GF_LOG_ERROR, "invalid argument");
      return -1;
    }
  
  return event_pool->ops->event_dispatch (event_pool);
}
