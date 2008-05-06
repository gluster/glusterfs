/*
   Copyright (c) 2006, 2007 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _CLIENT_PROTOCOL_H
#define _CLIENT_PROTOCOL_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <arpa/inet.h>
#include "inode.h"
#include "timer.h"

#define CLIENT_PORT_CIELING 1023
#define DEFAULT_BLOCK_SIZE     (1048576 * 256)   /* 4MB */

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
  int32_t max_block_size;  /* maximum size of protocol data block that this client can recieve, 0 is unlimited */
  struct timeval last_sent;
  struct timeval last_recieved;
  gf_timer_t *timer;
};

typedef struct {
  inode_t *inode;
  fd_t *fd;
} client_local_t;

#endif
