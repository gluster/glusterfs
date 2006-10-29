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

#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

/*
  All value in bytes. '\n' is field seperator.
  Field:<field_length>
  
  ==================
  "Block Start\n":12
  callid:16
  Type:8
  Code:8
  Name:32
  BlockSize:32
  Block:<BlockSize>
  "Block End\n":10
  ==================
*/

#include <inttypes.h>

typedef struct _gf_block gf_block;
typedef struct _gf_block gf_block_t;

#include "transport.h"

#define START_LEN 12
#define CALLID_LEN 17
#define TYPE_LEN  9
#define OP_LEN  9
#define NAME_LEN  33
#define SIZE_LEN  33
#define END_LEN   10

struct _gf_block {
  int64_t callid;
  int32_t type;
  int32_t op;
  int8_t name[32];
  int32_t size;
  int8_t *data;
};

typedef enum {
  OP_TYPE_FOP_REQUEST,
  OP_TYPE_MOP_REQUEST,
  OP_TYPE_FOP_REPLY,
  OP_TYPE_MOP_REPLY
} glusterfs_op_type_t;

gf_block *gf_block_new (int64_t callid);
int32_t gf_block_serialize (gf_block *b, int8_t *buf);
int32_t gf_block_serialized_length (gf_block *b);

gf_block *gf_block_unserialize (int32_t fd);
gf_block *gf_block_unserialize_transport (transport_t *this);

#endif
