/*
   Copyright (c) 2007-2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _TIMER_H
#define _TIMER_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include <sys/time.h>
#include <pthread.h>

typedef void (*gf_timer_cbk_t) (void *);

struct _gf_timer {
  struct _gf_timer *next, *prev;
  struct timeval at;
  gf_timer_cbk_t cbk;
  void *data;
};

struct _gf_timer_registry {
  pthread_t th;
  char fin;
  struct _gf_timer stale;
  struct _gf_timer active;
  pthread_mutex_t lock;
};

typedef struct _gf_timer gf_timer_t;
typedef struct _gf_timer_registry gf_timer_registry_t;

gf_timer_t *
gf_timer_call_after (glusterfs_ctx_t *ctx,
		     struct timeval delta,
		     gf_timer_cbk_t cbk,
		     void *data);

int32_t
gf_timer_call_cancel (glusterfs_ctx_t *ctx,
		      gf_timer_t *event);

void *
gf_timer_proc (void *data);

gf_timer_registry_t *
gf_timer_registry_init (glusterfs_ctx_t *ctx);

#endif /* _TIMER_H */
