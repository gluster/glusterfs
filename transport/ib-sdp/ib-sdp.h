/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
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

#ifndef _IB_SDP_H
#define _IB_SDP_H

#include <stdio.h>
#include <arpa/inet.h>

#define CLIENT_PORT_CIELING 1023
struct wait_queue {
  struct wait_queue *next;
  int8_t *buf;
  int32_t len;
};

typedef struct ib_sdp_private ib_sdp_private_t;
struct ib_sdp_private {
  int32_t sock;
  uint8_t connected;
  uint8_t is_debug;
  in_addr_t addr;
  unsigned short port;
  int8_t *volume;
  pthread_mutex_t read_mutex;
  pthread_mutex_t write_mutex;

  pthread_mutex_t queue_mutex;
  struct wait_queue *queue;

  dict_t *options;
  int32_t (*notify) (xlator_t *xl, transport_t *trans, int32_t event); /* used by ib-sdp/server */
};

int32_t ib_sdp_flush (transport_t *this);

int32_t ib_sdp_recieve (transport_t *this, int8_t *buf, int32_t len);
int32_t ib_sdp_submit (transport_t *this, int8_t *buf, int32_t len);

int32_t ib_sdp_except (transport_t *this);

#endif
