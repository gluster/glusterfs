/*
  (C) 2006, 2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
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

#include <sys/poll.h>
#include "transport.h"

struct poll_ctx {
  int client_count;
  int pfd_count;
  struct pollfd *pfd;
  struct {
    int32_t (*handler) (int32_t fd,
			int32_t event,
			void *data);
    void *data;
  } *cbk_data;
};

static int32_t
poll_notify (int32_t fd,
	     int32_t event,
	     void *data)
{
  int32_t ret = 0;
  transport_t *trans = (transport_t *)data;

  ret = transport_notify (trans, event);
  if (ret || (event & (POLLERR|POLLHUP))) {
    /* connected on demand on the next transaction */
    transport_disconnect (trans);
    /* force unregister */
    ret = -1;
  }

  return ret;
}
			 

static struct poll_ctx *
get_server_ctx ()
{
  static struct poll_ctx *ctx;

  if (!ctx) {
    ctx = (void *)calloc (1, sizeof (*ctx));
    ctx->pfd_count = 1024;
    ctx->pfd = (void *) calloc (1024, 
				sizeof (struct pollfd));
    ctx->cbk_data = (void *) calloc (1024,
				     sizeof (*ctx->cbk_data));
  }

  return ctx;
}

static void
unregister_member (struct poll_ctx *ctx,
		   int32_t i)
{
  ctx->pfd[i].fd = ctx->pfd[ctx->client_count - 1].fd;
  ctx->pfd[i].events = ctx->pfd[ctx->client_count - 1].events;
  ctx->pfd[i].revents = ctx->pfd[ctx->client_count - 1].revents;
  ctx->cbk_data[i].handler = ctx->cbk_data[ctx->client_count - 1].handler;
  ctx->cbk_data[i].data = ctx->cbk_data[ctx->client_count - 1].data;

  ctx->client_count--;
  return;
}

int32_t
poll_register (int fd, 
	       void *data)
{
  struct poll_ctx *ctx = get_server_ctx ();

  if (ctx->client_count == ctx->pfd_count) {
    ctx->pfd_count *= 2;
    ctx->pfd = realloc (ctx->pfd, 
			sizeof (*ctx->pfd) * ctx->pfd_count);
    ctx->cbk_data = realloc (ctx->pfd, 
			     sizeof (*ctx->cbk_data) * ctx->pfd_count);
  }

  ctx->pfd[ctx->client_count].fd = fd;
  ctx->pfd[ctx->client_count].events = POLLIN | POLLPRI | POLLERR | POLLHUP;
  ctx->pfd[ctx->client_count].revents = 0;

  ctx->cbk_data[ctx->client_count].data = data;

  ctx->client_count++;
  return 0;
}

int32_t
poll_iteration ()
{
  struct poll_ctx *ctx = get_server_ctx ();
  struct pollfd *pfd;

  int32_t ret;
  int32_t i;

  pfd = ctx->pfd;
  ret = poll (pfd,
	      (unsigned int) ctx->client_count,
	      -1);

  if (ret == -1) {
    if (errno == EINTR) {
      return 0;
    } else {
      return -errno;
    }
  }

  for (i=0; i < ctx->client_count; i++) {
    if (pfd[i].revents) {
      if (poll_notify (pfd[i].fd,
		       pfd[i].revents,
		       ctx->cbk_data[i].data) == -1) {
	unregister_member (ctx, i);
	i--;
      }
    }
  }
  return 0;
}

