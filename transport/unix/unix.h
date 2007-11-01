/*
   Copyright (c) 2006 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef _XPORT_SOCKET_H
#define _XPORT_SOCKET_H

#include <stdio.h>
#include <sys/un.h>

#define CLIENT_PORT_CIELING 1023
struct wait_queue {
  struct wait_queue *next;
  char *buf;
  int32_t len;
};

typedef struct unix_private unix_private_t;
struct unix_private {
  int32_t sock;
  unsigned char connected;
  unsigned char connection_in_progress; // PNegri
  					// Best to change these vars to
					// BIT FLAGS, its possible?
					// 1 char = 8 flags
  unsigned char is_debug;
  in_addr_t addr;
  unsigned short port;
  char *volume;
  pthread_mutex_t read_mutex;
  pthread_mutex_t write_mutex;

  //  pthread_mutex_t queue_mutex;
  //  struct wait_queue *queue;

  dict_t *options;
  event_notify_fn_t notify;
};

int32_t unix_recieve (transport_t *this, char *buf, int32_t len);
int32_t unix_disconnect (transport_t *this);
int32_t unix_bail (transport_t *this);
int32_t unix_except (transport_t *this);
int32_t unix_readv (transport_t *this,
		   const struct iovec *vector,
		   int32_t count);

#endif
