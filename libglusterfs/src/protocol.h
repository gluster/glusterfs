/*
   Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif


/*
  All value in bytes. '\n' is field seperator.
  Field:<field_length>
  
  ==================
  "Block Start\n":12
  callid:16
  Type:8
  Op:8
  Name:32
  BlockSize:32
  Block:<BlockSize>
  "Block End\n":10
  ==================
*/

#include <inttypes.h>
#include <sys/uio.h>
#include "dict.h"

typedef struct _gf_block gf_block;
typedef struct _gf_block gf_block_t;

#include "transport.h"

#define GF_START_LEN 12
#define GF_CALLID_LEN 17
#define GF_TYPE_LEN  9
#define GF_OP_LEN  9
#define GF_NAME_LEN  33
#define GF_SIZE_LEN  33
#define GF_END_LEN   10

struct _gf_block {
  int64_t callid;
  int32_t type;
  int32_t op;
  char name[32];
  int32_t size;
  char *data;
  dict_t *dict;
};

typedef enum {
  GF_OP_TYPE_FOP_REQUEST,
  GF_OP_TYPE_MOP_REQUEST,
  GF_OP_TYPE_FOP_REPLY,
  GF_OP_TYPE_MOP_REPLY
} glusterfs_op_type_t;

gf_block_t *gf_block_new (int64_t callid);
int32_t gf_block_serialize (gf_block_t *b, char *buf);
int32_t gf_block_serialized_length (gf_block_t *b);

gf_block_t *gf_block_unserialize (int32_t fd);
 
gf_block_t *gf_block_unserialize_transport (transport_t *this,
					    const int32_t max_block_size);
 
int32_t gf_block_iovec_len (gf_block_t *blk);
int32_t gf_block_to_iovec (gf_block_t *blk, struct iovec *iov, int32_t cnt);


#endif
