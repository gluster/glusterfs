/*
   Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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
#include "transport.h"

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

struct sys_poll_ctx {
  int client_count;
  int pfd_count;
  struct pollfd *pfd;
  struct {
    int32_t (*handler) (int32_t fd,
			int32_t event,
			void *data);
    void *data;
  } *cbk_data;
  pthread_cond_t cond;
  pthread_mutex_t lock;
};

static int32_t
poll_notify (int32_t fd,
	     int32_t event,
	     void *data)
{
  int32_t ret = 0;
  transport_t *trans = (transport_t *)data;

  if (!trans)
    return 0;

  ret = transport_notify (trans, event);

  return ret;
}
			 

static struct sys_poll_ctx *
sys_poll_ctx (glusterfs_ctx_t *ctx)
{
  if (!ctx) {
    gf_log ("poll", GF_LOG_ERROR, "!ctx");
    return NULL;
  }

  if (!ctx->poll_ctx) {
    struct sys_poll_ctx *pctx;
    pctx = (void *)calloc (1, sizeof (*pctx));
    ERR_ABORT (pctx);
    pctx->pfd_count = 1024;
    pctx->pfd = (void *) calloc (1024, 
				 sizeof (struct pollfd));
    ERR_ABORT (pctx->pfd);
    pctx->cbk_data = (void *) calloc (1024,
				      sizeof (*pctx->cbk_data));
    ERR_ABORT (pctx->cbk_data);
    ctx->poll_ctx = pctx;
    pthread_mutex_init (&pctx->lock, NULL);
    pthread_cond_init (&pctx->cond, NULL);
  }

  return ctx->poll_ctx;
}

static void
unregister_member (struct sys_poll_ctx *ctx,
		   int32_t i)
{
  if (!ctx) {
    gf_log ("poll", GF_LOG_ERROR, "!ctx");
    return;
  }

  ctx->pfd[i].fd = ctx->pfd[ctx->client_count - 1].fd;
  ctx->pfd[i].events = ctx->pfd[ctx->client_count - 1].events;
  ctx->pfd[i].revents = ctx->pfd[ctx->client_count - 1].revents;
  ctx->cbk_data[i].handler = ctx->cbk_data[ctx->client_count - 1].handler;
  ctx->cbk_data[i].data = ctx->cbk_data[ctx->client_count - 1].data;

  ctx->client_count--;
  return;
}

int32_t
sys_poll_unregister (glusterfs_ctx_t *gctx,
		     int fd)
{
  int i = 0;
  struct sys_poll_ctx *ctx = sys_poll_ctx (gctx);

  if (!ctx) {
    gf_log ("poll", GF_LOG_ERROR, "!ctx");
    return 0;
  }

  pthread_mutex_lock (&ctx->lock);
  for (i=0; i<ctx->client_count; i++)
    if (ctx->pfd[i].fd == fd) {
      unregister_member (ctx, i);
      break;
    }
  pthread_mutex_unlock (&ctx->lock);

  return 0;
}

int32_t
sys_poll_register (glusterfs_ctx_t *gctx,
		   int fd,
		   void *data)
{
  struct sys_poll_ctx *ctx = sys_poll_ctx (gctx);

  if (!ctx) {
    gf_log ("poll", GF_LOG_ERROR, "!ctx");
    return 0;
  }

  pthread_mutex_lock (&ctx->lock);
  {
    
    if (ctx->client_count == ctx->pfd_count) {
      ctx->pfd_count *= 2;
      ctx->pfd = realloc (ctx->pfd, 
			  sizeof (*ctx->pfd) * ctx->pfd_count);
      ERR_ABORT (ctx->pfd);
      ctx->cbk_data = realloc (ctx->pfd, 
			       sizeof (*ctx->cbk_data) * ctx->pfd_count);
      ERR_ABORT (ctx->cbk_data);
    }

    ctx->pfd[ctx->client_count].fd = fd;
    ctx->pfd[ctx->client_count].events = POLLIN | POLLPRI | POLLERR | POLLHUP;
    ctx->pfd[ctx->client_count].revents = 0;
    
    ctx->cbk_data[ctx->client_count].data = data;
    
    ctx->client_count++;
    pthread_cond_broadcast (&ctx->cond);
  }
  pthread_mutex_unlock (&ctx->lock);

  transport_notify (data, 0);

  return 0;
}

int32_t
sys_poll_iteration (glusterfs_ctx_t *gctx)
{
  struct sys_poll_ctx *ctx = sys_poll_ctx (gctx);
  struct pollfd *pfd;
  int32_t ret;
  int32_t i;

  if (!ctx) {
    gf_log ("poll", GF_LOG_ERROR, "!ctx");
    return 0;
  }

  pthread_mutex_lock (&ctx->lock);
  {
    while (ctx->client_count == 0)
      pthread_cond_wait (&ctx->cond, &ctx->lock);
    pfd = ctx->pfd;
  }
  pthread_mutex_unlock (&ctx->lock);

  ret = poll (pfd, (unsigned int) ctx->client_count, -1);

  if (ret == -1) {
    if (errno == EINTR) {
      return 0;
    } else {
      return -errno;
    }
  }

  for (i=0; i < ctx->client_count; i++) {
    if (pfd[i].revents) {
      poll_notify (pfd[i].fd,
		   pfd[i].revents,
		   ctx->cbk_data[i].data);
    }
  }
  return 0;
}

