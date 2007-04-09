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

#ifndef _TIMER_H
#define _TIMER_H

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
