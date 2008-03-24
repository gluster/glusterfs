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

#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/poll.h>

#include <stdint.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "logging.h"
#include "transport.h"
#include "glusterfs.h"

transport_t *
transport_load (dict_t *options,
		xlator_t *xl,
		event_notify_fn_t notify)
{
  struct transport *trans = calloc (1, sizeof (struct transport));
  data_t *type_data;
  char *name = NULL;
  void *handle = NULL;
  char *type = "ERROR";

  if (!options) {
    freee (trans);
    gf_log ("transport", GF_LOG_ERROR, "options is NULL");
    return NULL;
  }
  if (!xl) {
    freee (trans);
    gf_log ("transport", GF_LOG_ERROR, "xl is NULL");
    return NULL;
  }
  if (!notify) {
    freee (trans);
    gf_log ("transport", GF_LOG_ERROR, "notify is NULL");
    return NULL;
  }

  type_data = dict_get (options, "transport-type"); // transport type, e.g., "tcp"
  trans->xl = xl;

  trans->notify = notify;


  if (type_data) {
    type = data_to_str (type_data);
  } else {
    freee (trans);
    gf_log ("transport", GF_LOG_ERROR,
	    "'option transport-type <xxxx/_____>' missing in specification");
    return NULL;
  }

  asprintf (&name, "%s/%s.so", TRANSPORTDIR, type);
  gf_log ("transport", GF_LOG_DEBUG,
	  "attempt to load file %s", name);

  handle = dlopen (name, RTLD_NOW|RTLD_GLOBAL);

  if (!handle) {
    gf_log ("transport", GF_LOG_ERROR,
	    "dlopen (%s): %s", name, dlerror ());
    freee (name);
    freee (trans);
    return NULL;
  };
  freee (name);

  if (!(trans->ops = dlsym (handle, "transport_ops"))) {
    gf_log ("transport", GF_LOG_ERROR,
	    "dlsym (transport_ops) on %s", dlerror ());
    freee (trans);
    return NULL;
  }

  if (!(trans->init = dlsym (handle, "gf_transport_init"))) {
    gf_log ("transport", GF_LOG_ERROR,
	    "dlsym (gf_transport_init) on %s", dlerror ());
    freee (trans);
    return NULL;
  }

  if (!(trans->fini = dlsym (handle, "gf_transport_fini"))) {
    gf_log ("transport", GF_LOG_ERROR,
	    "dlsym (gf_transport_fini) on %s", dlerror ());
    freee (trans);
    return NULL;
  }

  if (trans->init (trans, options, notify) != 0) {
    gf_log ("transport", GF_LOG_ERROR,
	    "'%s' initialization failed", type);
    freee (trans);
    return NULL;
  }

  pthread_mutex_init (&trans->lock, NULL);

  return trans;
}

int32_t 
transport_notify (transport_t *this, int32_t event)
{
  if (!this)
    return 0;

  int32_t ev = GF_EVENT_CHILD_UP;

  if ((event & POLLIN) || (event & POLLPRI))
    ev = GF_EVENT_POLLIN;
  if ((event & POLLERR) || (event & POLLHUP))
    ev = GF_EVENT_POLLERR;
  return this->notify (this->xl, ev, this);
}

int32_t 
transport_submit (transport_t *this, char *buf, int32_t len)
{
  if (!this && !this->ops)
    return 0;
  return this->ops->submit (this, buf, len);
}

/*
int32_t
transport_flush (transport_t *this)
{
  return this->ops->flush (this);
}
*/

int32_t
transport_except (transport_t *this)
{
  if (!this && !this->ops)
    return 0;
  return this->ops->except (this);
}

int32_t 
transport_connect (transport_t *this)
{
  if (!this && !this->ops)
    return 0;
  return this->ops->connect (this);
}

int32_t 
transport_disconnect (transport_t *this)
{
  if (!this && !this->ops)
    return 0;
  return this->ops->disconnect (this);
}

int32_t
transport_bail (transport_t *this)
{
  if (!this && !this->ops)
    return 0;
  return this->ops->bail (this);
}

int32_t 
transport_destroy (transport_t *this)
{
  if (!this)
    return 0;
  this->fini (this);
  pthread_mutex_destroy (&this->lock);
  freee (this);

  return 0;
}

transport_t *
transport_ref (transport_t *this)
{
  if (!this)
    return NULL;

  pthread_mutex_lock (&this->lock);
  this->refcount ++;
  pthread_mutex_unlock (&this->lock);

  return this;
}

void
transport_unref (transport_t *this)
{
  int32_t refcount;
  if (!this)
    return;

  pthread_mutex_lock (&this->lock);
  refcount = --this->refcount;
  pthread_mutex_unlock (&this->lock);

  if (!refcount) {
    this->notify (this->xl, GF_EVENT_TRANSPORT_CLEANUP, this);
    transport_destroy (this);
  }
}

int32_t
poll_register (glusterfs_ctx_t *ctx,
	       int fd,
	       void *data)
{
  int32_t ret = 0;

  if (!ctx)
    return 0;

#ifdef HAVE_SYS_EPOLL_H

  switch (ctx->poll_type)
    {
    case SYS_POLL_TYPE_EPOLL:
      ret = sys_epoll_register (ctx, fd, data);
      if (ret != -1 || errno != ENOSYS)
	break;
      ctx->poll_type = SYS_POLL_TYPE_POLL;

    case SYS_POLL_TYPE_POLL:
      ret = sys_poll_register (ctx, fd, data);
      break;

    default:
      gf_log ("transport", GF_LOG_ERROR, "Invalid poll type");
      break;
    }
#else
  ret = sys_poll_register (ctx, fd, data);
#endif
  return ret;
}

int32_t
poll_unregister (glusterfs_ctx_t *ctx,
		 int fd)
{
  int32_t ret = 0;

  if (!ctx)
    return 0;

#ifdef HAVE_SYS_EPOLL_H

  switch (ctx->poll_type)
    {
    case SYS_POLL_TYPE_EPOLL:
      ret = sys_epoll_unregister (ctx, fd);
      if (ret != -1 || errno != ENOSYS)
	break;
      ctx->poll_type = SYS_POLL_TYPE_POLL;

    case SYS_POLL_TYPE_POLL:
      ret = sys_poll_unregister (ctx, fd);
      break;

    default:
      gf_log ("transport", GF_LOG_ERROR, "Invalid poll type");
    }
#else
  ret = sys_poll_unregister (ctx, fd);
#endif

  return ret;
}

int32_t
poll_iteration (glusterfs_ctx_t *ctx)
{
  int32_t ret = 0;

  if (!ctx)
    return 0;

#ifdef HAVE_SYS_EPOLL_H

  switch (ctx->poll_type)
    {
    case SYS_POLL_TYPE_EPOLL:
      ret = sys_epoll_iteration (ctx);
      if (ret != -1 || errno != ENOSYS)
	break;
      ctx->poll_type = SYS_POLL_TYPE_POLL;

    case SYS_POLL_TYPE_POLL:
      ret = sys_poll_iteration (ctx);
      break;

    default:
      gf_log ("transport", GF_LOG_ERROR, "Invalid poll type");

      break;
    }
#else
  ret = sys_poll_iteration (ctx);
#endif

  return ret;
}
