/*
   Copyright (c) 2007 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifdef HAVE_SYS_EPOLL_H

#include <sys/epoll.h>
#include <sys/poll.h>
#include <pthread.h>

#include "transport.h"

struct sys_epoll_ctx {
  int32_t epollfd;
  int32_t fds;
  struct epoll_event *evs;
  int32_t ev_count;
  pthread_mutex_t lock;
  pthread_cond_t cond;
};

static int32_t
epoll_notify (int32_t eevent,
	      void *data)
{
  int32_t ret = 0;
  transport_t *trans = (transport_t *)data;
  int32_t event = 0;

  if (eevent & EPOLLIN)
    event |= POLLIN;
  if (eevent & EPOLLPRI)
    event |= POLLPRI;
  if (eevent & POLLERR)
    event |= POLLERR;
  if (eevent & POLLHUP)
    event |= POLLHUP;

  ret = transport_notify (trans, event);

  return ret;
}


struct sys_epoll_ctx *
sys_epoll_ctx (glusterfs_ctx_t *ctx)
{
  struct sys_epoll_ctx *ectx;

  if (!ctx->poll_ctx) {
    ectx = calloc (1, sizeof (*ectx));
    ectx->epollfd = epoll_create (1024);
    ectx->fds = 0;
    ctx->poll_ctx = ectx;
    pthread_mutex_init (&ectx->lock, NULL);
    pthread_cond_init (&ectx->cond, NULL);
  }

  return ctx->poll_ctx;
}

int32_t
sys_epoll_unregister (glusterfs_ctx_t *ctx,
		      int fd)
{
  struct sys_epoll_ctx *ectx = sys_epoll_ctx (ctx);
  struct epoll_event ev;

  pthread_mutex_lock (&ectx->lock);
  ectx->fds--;
  pthread_mutex_unlock (&ectx->lock);

  return epoll_ctl (ectx->epollfd, EPOLL_CTL_DEL, fd, &ev);
}

int32_t
sys_epoll_register (glusterfs_ctx_t *ctx,
		    int fd, 
		    void *data)
{
  struct sys_epoll_ctx *ectx = sys_epoll_ctx (ctx);
  struct epoll_event ev;
  transport_t *trans = data;
  int32_t ret;

  memset (&ev, 0, sizeof (ev));
  ev.data.ptr = trans;
  ev.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;

  pthread_mutex_lock (&ectx->lock);
  ectx->fds++;
  if (ectx->fds == 1)
    pthread_cond_broadcast (&ectx->cond);
  pthread_mutex_unlock (&ectx->lock);

  ret = epoll_ctl (ectx->epollfd, EPOLL_CTL_ADD, fd, &ev);

  if (ret == -1 && errno == ENOSYS) {
    freee (ectx);
    ctx->poll_ctx = NULL;
  }
  else 
    transport_notify (data, 0);

  return ret;
}


int32_t
sys_epoll_iteration (glusterfs_ctx_t *ctx)
{
  struct sys_epoll_ctx *ectx = sys_epoll_ctx (ctx);
  int32_t ret, i;

  pthread_mutex_lock (&ectx->lock);

  while (ectx->fds == 0)
    pthread_cond_wait (&ectx->cond, &ectx->lock);

  if (ectx->ev_count < ectx->fds) {
    ectx->ev_count = ectx->fds;
    if (!ectx->evs)
      ectx->evs = malloc (ectx->ev_count * sizeof (struct epoll_event));
    else
      ectx->evs = realloc (ectx->evs,
			   ectx->ev_count * sizeof (struct epoll_event));
  }
  pthread_mutex_unlock (&ectx->lock);

  ret = epoll_wait (ectx->epollfd,
		    ectx->evs, 
		    ectx->ev_count,
		    -1);

  if (ret == -1) {
    if (errno == EINTR) {
      return 0;
    } else {
      if (errno == ENOSYS) {
	freee (ectx);
	ctx->poll_ctx = NULL;
      }
      gf_log ("libglusterfs/epoll", GF_LOG_ERROR,
	      "epoll_wait returned %d (%s)", errno, strerror (errno));
      return -1;
    }
  }
  
  for (i=0; i < ret; i++) {
    epoll_notify (ectx->evs[i].events,
		  ectx->evs[i].data.ptr);
  }

  return 0;
}

#endif
