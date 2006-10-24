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

#ifndef __TRANSPORT_H__
#define __TRANSPORT_H__

#include <inttypes.h>

typedef struct transport {
  struct transport_ops *transport_ops;
  void *private;

  xlator_t *xl;
  int32_t (*init) (transport_t *this);
  void (*fini) (transport_t *this);
  int32_t (*notify) (xlator_t *xl, transport_t *trans);
} transport_t;

struct transport_ops {
  int32_t (*accept) (transport_t *this, int sockfd, dict_t *address);
  int32_t (*connect) (transport_t *this, dict_t *address);
  int32_t (*send) (transport_t *this, int8_t *buf, int32_t len);
  int32_t (*recieve) (transport_t *this, int8_t *buf, int32_t len);

  int32_t (*submit) (transport_t *this, int8_t *buf, int32_t len);
  int32_t (*except) (transport_t *this);
};

transport_t *transport_new (dict_t *options,
			    int32_t (*notify) (xlator_t *xl, transport_t *trans));

int32_t transport_notify (transport_t *this);
int32_t transport_submit (transport_t *this, int8_t *buf, int32_t len);
int32_t transport_except (transport_t *this);
int32_t transport_destroy (struct transport *this);

#endif /* __TRANSPORT_H__ */
