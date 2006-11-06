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

#ifndef _XPORT_SOCKET_H
#define _XPORT_SOCKET_H

#include <stdio.h>
#include <arpa/inet.h>

#define CLIENT_PORT_CIELING 1023
struct wait_queue {
  struct wait_queue *next;
  char *buf;
  int32_t len;
};

typedef struct tcp_private tcp_private_t;
struct tcp_private {
  int32_t sock;
  unsigned char connected;
  unsigned char is_debug;
  in_addr_t addr;
  unsigned short port;
  char *volume;
  pthread_mutex_t read_mutex;
  pthread_mutex_t write_mutex;

  //  pthread_mutex_t queue_mutex;
  //  struct wait_queue *queue;

  dict_t *options;
  int32_t (*notify) (xlator_t *xl, transport_t *trans, int32_t event); /* used by tcp/server */
};

int32_t tcp_recieve (transport_t *this, char *buf, int32_t len);
int32_t tcp_disconnect (transport_t *this);

#endif
