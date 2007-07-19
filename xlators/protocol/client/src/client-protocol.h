/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
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

#ifndef _CLIENT_PROTOCOL_H
#define _CLIENT_PROTOCOL_H

#include <stdio.h>
#include <arpa/inet.h>
#include "inode.h"
#include "timer.h"

#define CLIENT_PORT_CIELING 1023

struct saved_frame;
typedef struct saved_frame saved_frame_t;
struct client_proto_priv;
typedef struct client_proto_priv client_proto_priv_t;

#include "stack.h"

/* This will be stored in transport_t->xl_private */
struct client_proto_priv {
  pthread_mutex_t lock;
  dict_t *saved_frames;
  dict_t *saved_fds;
  inode_table_t *table;
  int64_t callid;
  int32_t transport_timeout;
  gf_timer_t *reconnect;
  char connected;
  int32_t n_minus_1;
  int32_t n;
  struct timeval last_sent;
  struct timeval last_recieved;
  gf_timer_t *timer;
};

typedef struct {
  inode_t *inode;
  fd_t *fd;
} client_local_t;

#endif
