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

#include "dict.h"

struct transport {
  struct transport_ops *transport_ops;
  void *private;

  int32_t (*init) (struct transport *this);
  void (*fini) (struct transport *this);
};

struct transport_ops {
  int32_t (*connect) (struct transport *this, dict_t *address);
  int32_t (*send) (struct transport *this, int8_t *buf, int32_t len);
  int32_t (*recieve) (struct transport *this, int8_t *buf, int32_t len);
};

struct transport *transport_new (dict_t *options);
int32_t transport_destroy (struct transport *this);

#endif /* __TRANSPORT_H__ */
