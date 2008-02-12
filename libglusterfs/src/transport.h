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

#ifndef __TRANSPORT_H__
#define __TRANSPORT_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>

#define GF_DEFAULT_LISTEN_PORT 6996

struct transport_ops;
typedef struct transport transport_t;

#include "xlator.h"
#include "dict.h"

struct peer_info_t {
  struct sockaddr_in sockaddr;
};

struct transport {
  struct transport_ops *ops;
  void *private;
  void *xl_private;
  pthread_mutex_t lock;
  int32_t refcount;

  xlator_t *xl;
  void *dnscache;
  data_t *buf;
  int32_t (*init) (transport_t *this,
		   dict_t *options,
		   event_notify_fn_t notify);

  struct peer_info_t peerinfo;
  void (*fini) (transport_t *this);

  event_notify_fn_t notify;
};

struct transport_ops {
  int32_t (*flush) (transport_t *this); 

  int32_t (*recieve) (transport_t *this, char *buf, int32_t len);
  int32_t (*submit) (transport_t *this, char *buf, int32_t len);
  int32_t (*writev) (transport_t *this,
		     const struct iovec *vector,
		     int32_t count);
  int32_t (*readv) (transport_t *this,
		    const struct iovec *vector,
		    int32_t count);
  int32_t (*connect) (transport_t *this);
  int32_t (*disconnect) (transport_t *this);
  int32_t (*except) (transport_t *this);
  int32_t (*bail) (transport_t *this);
};

transport_t *transport_load (dict_t *options, 
			     xlator_t *xl,
			     event_notify_fn_t notify);

int32_t transport_connect (transport_t *this);
int32_t transport_disconnect (transport_t *this);
int32_t transport_notify (transport_t *this, int32_t event);
int32_t transport_submit (transport_t *this, char *buf, int32_t len);
int32_t transport_except (transport_t *this);
int32_t transport_flush (transport_t *this);
int32_t transport_destroy (transport_t *this);
int32_t transport_bail (transport_t *this);

transport_t *
transport_ref (transport_t *trans);
void transport_unref (transport_t *trans);

int32_t register_transport (transport_t *new_trans, int32_t fd);

int32_t poll_register (glusterfs_ctx_t *ctx,
		       int32_t fd,
		       void *data);
int32_t poll_unregister (glusterfs_ctx_t *ctx,
			      int32_t fd);

int32_t poll_iteration (glusterfs_ctx_t *ctx);

uint8_t is_sys_epoll_implemented (void);

int32_t sys_epoll_register (glusterfs_ctx_t *ctx,
			    int32_t fd, void *data);
int32_t sys_epoll_unregister (glusterfs_ctx_t *ctx,
			      int32_t fd);
int32_t sys_epoll_iteration (glusterfs_ctx_t *ctx);

int32_t sys_poll_register (glusterfs_ctx_t *ctx,
			   int32_t fd, void *data);
int32_t sys_poll_unregister (glusterfs_ctx_t *ctx,
			  int32_t fd);
int32_t sys_poll_iteration (glusterfs_ctx_t *ctx);

#endif /* __TRANSPORT_H__ */
