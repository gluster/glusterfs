/*
  (C) 2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#include <sys/epoll.h>
#include <sys/poll.h>
#include "transport.h"

#ifdef HAVE_SYS_EPOLL_H

struct sys_epoll_ctx {
  int32_t epollfd;
  int32_t fds;
  struct epoll_event *evs;
  int32_t ev_count;
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
  if (ret || (event & (POLLERR|POLLHUP))) {
    /* connected on demand on the next transaction */
    transport_disconnect (trans);
    /* force unregister */
    ret = -1;
  }

  return ret;
}


struct sys_epoll_ctx *
sys_epoll_ctx (glusterfs_ctx_t *ctx)
{
  static struct sys_epoll_ctx ectx;

  if (!ctx->poll_ctx) {
    ectx.epollfd = epoll_create (1024);
    ectx.fds = 0;
    ctx->poll_ctx = &ectx;
  }

  return ctx->poll_ctx;
}

int32_t
sys_epoll_unregister (glusterfs_ctx_t *ctx,
		      int fd)
{
  struct sys_epoll_ctx *ectx = sys_epoll_ctx (ctx);
  struct epoll_event ev;

  ectx->fds--;

  return epoll_ctl (ectx->epollfd, EPOLL_CTL_DEL, fd, &ev);
}

int32_t
sys_epoll_register (glusterfs_ctx_t *ctx,
		    int fd, 
		    void *data)
{
  struct sys_epoll_ctx *ectx = sys_epoll_ctx (ctx);
  struct epoll_event ev;

  memset (&ev, 0, sizeof (ev));
  ev.data.ptr = data;
  ev.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;

  ectx->fds++;

  return epoll_ctl (ectx->epollfd, EPOLL_CTL_ADD, fd, &ev);
}

int32_t
sys_epoll_iteration (glusterfs_ctx_t *ctx)
{
  struct sys_epoll_ctx *ectx = sys_epoll_ctx (ctx);
  int32_t ret, i;

  if (ectx->ev_count < ectx->fds) {
    ectx->ev_count = ectx->fds;
    if (!ectx->evs)
      ectx->evs = malloc (ectx->ev_count * sizeof (struct epoll_event));
    else
      ectx->evs = realloc (ectx->evs,
			   ectx->ev_count * sizeof (struct epoll_event));
  }

  ret = epoll_wait (ectx->epollfd,
		    ectx->evs, 
		    ectx->ev_count,
		    -1);

  if (ret == -1) {
    if (errno == EINTR) {
      return 0;
    } else {
      return -1;
    }
  }
  
  for (i=0; i < ret; i++) {
    if (epoll_notify (ectx->evs[i].events,
		      ectx->evs[i].data.ptr) == -1) {
      //	epoll_unregister (evs[i].data.fd);
    }
  }

  return 0;
}

#endif
