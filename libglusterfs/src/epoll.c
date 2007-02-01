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


static int32_t
epoll_create_once ()
{
  static int32_t sock = -1;

  if (sock == -1) {
    sock = epoll_create (1024);
  }

  return sock;
}

static int32_t fds;

int32_t
epoll_unregister (int fd)
{
  int32_t epollfd = epoll_create_once ();
  struct epoll_event ev;

  fds--;

  return epoll_ctl (epollfd, EPOLL_CTL_DEL, fd, &ev);
}

int32_t
epoll_register (int fd, 
		void *data)
{
  int32_t epollfd = epoll_create_once ();
  struct epoll_event ev;

  memset (&ev, 0, sizeof (ev));
  ev.data.ptr = data;
  ev.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;

  fds++;

  return epoll_ctl (epollfd, EPOLL_CTL_ADD, fd, &ev);
}

int32_t
epoll_iteration ()
{
  int32_t epollfd = epoll_create_once ();
  int32_t ret, i;
  static struct epoll_event *evs = NULL;
  static int32_t ev_count;

  if (ev_count < fds) {
    ev_count = fds;
    if (!evs)
      evs = malloc (ev_count * sizeof (struct epoll_event));
    else
      evs = realloc (evs, ev_count * sizeof (struct epoll_event));
  }

  ret = epoll_wait (epollfd, evs, ev_count, -1);

  if (ret == -1) {
    if (errno == EINTR) {
      return 0;
    } else {
      return -1;
    }
  }
  
  for (i=0; i < ret; i++) {
    if (epoll_notify (evs[i].events,
		      evs[i].data.ptr) == -1) {
      //	epoll_unregister (evs[i].data.fd);
    }
  }

  return 0;
}

