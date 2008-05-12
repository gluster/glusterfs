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

/*
  Field:<field_length_in_bits>
  
  ==================
  Signature: 32      ":O"   (0x3A4F * 0x100 | 0x03 )
  Type:16
  Op:16
  callid:64
  BlockSize:32
  Block:<BlockSize>
  TailSignature:32   ";o" (0x3B6F)
  ==================

      0                  8                 16                 24                 32
      |                  |                  |                  |                  |    
0     |------------------------Signature---------------------->|------Version---->|
1     |----------------Type---------------->|------------------Op---------------->|
2     |-----------------------------------Callid--------------------------------->|
3     |-----------------------------------Callid--------------------------------->|
4     |------------------------------------Size---------------------------------->|
5-n   |------------------------------------Data---------------------------------->|
n     |---------End Signature ------------->|

Data
++++
  0               8              16              24        32
  |               |               |               |         |    
  |--------------------------Fixed------------------------->|
  |--------------------------Fixed------------------------->|
  |--------------------------Fixed------------------------->|
  |--------------------------Fixed------------------------->|
  |---Type------->|------------------FieldLength----------->|
  |---------------------------Data------------------------->|
  |---Type------->|------------------FieldLength----------->|
  |---------------------------Data------------------------->|
  |               |               |               |         |



*/

#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>
#include <sys/uio.h>
#include <arpa/inet.h>

#define GF_PROTO_VERSION        0x13
#define GF_PROTO_HEADER_SIGN    0x3A4F00 /* ":O" */
#define GF_PROTO_END_MSG        0x446F6E65 /* "Done" */
#define GF_PROTO_HEADER_LEN     20
#define GF_PROTO_END_LEN        2
#define GF_PROTO_FIXED_DATA_LEN 16

#define GF_PROTO_CHAR_TYPE   1
#define GF_PROTO_UINT8_TYPE  2
#define GF_PROTO_INT32_TYPE  3
#define GF_PROTO_INT64_TYPE  4
#define GF_PROTO_IOV_TYPE    5
#define GF_PROTO_MISC_TYPE   6

#define GF_PROTO_MAX_FIELDS  7

/* How to pack Data ?
 * Its a binary protocol. Also, there is no fixed length for packates. So, how do we pack data? 
 * the challenge is to reduce the memcpy, hence we need to use iovector interface
 * 
 */


typedef struct _gf_proto_block gf_proto_block;
typedef struct _gf_proto_block gf_proto_block_t;

#include "transport.h"

struct _gf_proto_block {
  int32_t signature;
  int16_t type;
  int16_t op;
  int64_t callid;
  int32_t size;
  int32_t count; /* count of io-vectors, not sent in packet */
  void *args;
};

struct gf_field {
  int8_t type;
  int8_t need_free;
  int32_t len;
  void *ptr;
};
struct _gf_args_request {
  int32_t uid;
  int32_t gid;
  int32_t pid;
  int32_t common; /* Will be used by all the fops, if they have any 32bit argument, if not, will be 0 */
  struct gf_field fields[GF_PROTO_MAX_FIELDS];
};
struct _gf_args_reply {
  int32_t op_ret;
  int32_t op_errno;
  int32_t dummy1;
  int32_t dummy2;
  struct gf_field fields[GF_PROTO_MAX_FIELDS];
};

/* gf_args_reply_t will be same i*/

typedef struct _gf_args_request gf_args_request_t;
typedef struct _gf_args_reply gf_args_reply_t;

struct _gf_args {
  int32_t fixed1;
  int32_t fixed2;
  int32_t fixed3;
  int32_t fixed4;
  struct gf_field fields[GF_PROTO_MAX_FIELDS];

};
typedef struct _gf_args gf_args_t;

static inline int64_t gf_htonl_64 (int64_t hostnum)
{
  /* TODO : suggestion? mostly need to copy the code, 
     and manually do the bytearray shifting */
  int64_t top    = htonl((hostnum & 0xFFFFFFFF00000000ULL) >> 32);
  int64_t bottom = htonl((hostnum & 0x0FFFFFFFF));
  return (int64_t) (top << 32) | bottom;
}

static inline int64_t gf_ntohl_64 (int64_t netnum)
{
  /* TODO : suggestion? mostly need to copy the code, 
     and manually do the bytearray shifting */
  int64_t top    = ntohl((netnum & 0xFFFFFFFF00000000ULL) >> 32);
  int64_t bottom = ntohl((netnum & 0x0FFFFFFFF));
  return (int64_t) (top << 32) | bottom;
}

static inline int64_t gf_ntohl_64_ptr (void *buf)
{
  int64_t netnum = ((int64_t *)buf)[0];
  /* TODO : suggestion? mostly need to copy the code, 
     and manually do the bytearray shifting */
  int64_t top    = ntohl((netnum & 0xFFFFFFFF00000000ULL) >> 32);
  int64_t bottom = ntohl((netnum & 0x0FFFFFFFF));
  return (int64_t) (top << 32) | bottom;
}

static inline void gf_proto_set_tail_signature (void *buf, int32_t data_len)
{
  /* set it as string */
  memcpy ((buf + data_len + GF_PROTO_HEADER_LEN), ";o", 2);
}

static inline void gf_proto_set_header (void *buf, gf_proto_block_t *b)
{
  int32_t sign = ((GF_PROTO_HEADER_SIGN << 8) | GF_PROTO_VERSION);
  
  ((int32_t *)buf)[0] = htonl (sign);
  ((int16_t *)buf)[2] = htons (b->type);
  ((int16_t *)buf)[3] = htons (b->op);
  ((int64_t *)buf)[1] = gf_htonl_64 (b->callid);
  ((int32_t *)buf)[4] = htonl (b->size);
}

/* Header */
static inline int64_t gf_proto_get_callid (void *header)
{
  return gf_ntohl_64_ptr (header + 8);
}

static inline int32_t gf_proto_get_size (void *header)
{
  return ntohl (((int32_t *)header)[4]);

}

static inline int8_t gf_proto_get_type (void *header)
{
  return ntohs (((int16_t *)header)[2]);
}

static inline int8_t gf_proto_get_op (void *header)
{
  return ntohs (((int16_t *)header)[3]);
}

static inline int8_t gf_proto_get_version (void *header)
{
  return ((int8_t *)header)[3];
}

static inline int32_t gf_proto_get_signature (void *header)
{
  int32_t sign = ntohl (((int32_t *)header)[0]);
  return ((sign & 0xFFFFFF00) >> 8);
}

static inline void gf_proto_free_args (gf_args_t *buf)
{
  int32_t i;
  for (i = 0 ; i < GF_PROTO_MAX_FIELDS; i++) {
    if (buf->fields[i].need_free)
      freee (buf->fields[i].ptr);
  }
}

gf_proto_block_t *gf_proto_block_new (int64_t callid);
int32_t gf_proto_block_serialized_length (gf_proto_block_t *b);

gf_proto_block_t *gf_proto_block_unserialize (int32_t fd);
 
gf_proto_block_t *gf_proto_block_unserialize_transport (transport_t *this,
					    const int32_t max_block_size);
 
int32_t gf_proto_get_struct_from_buf (void *buf, gf_args_t *args, int32_t size);

int32_t gf_proto_block_iovec_len (gf_proto_block_t *blk);
int32_t gf_proto_block_to_iovec (gf_proto_block_t *blk, struct iovec *iov, int32_t *cnt);
int32_t gf_proto_get_data_len (gf_args_t *buf);

#endif /* __PROTOCOL_H__ */
#if 0  
{{
  union {
    /* lookup */
    struct {
      int64_t ino;
      char *path;
    } lookup;
    
    /* stat */
    struct {
      int64_t ino;
      char *path;
    } stat;
    
    /* chmod */
    struct {
      int64_t ino;
      char *path;
    } chmod;
    
    /* fchmod */
    struct {
      int32_t mode;
    } fchmod;
    
    /* chown */
    struct {
      int32_t uid;
      int32_t gid;
      int64_t ino;
      char *path;
    } chown;
    
    /* fchown */
    struct {
      int32_t uid;
      int32_t gid;
    } fchown;
    
    /* truncate */
    struct {
      int64_t off;
      int64_t ino;
      char *path;      
    } truncate;
    
    /* ftruncate */
    struct {
      int64_t off;
    } ftruncate;
    
    /* utimens */
    struct {
      struct timespec tv[2];
      int64_t ino;
      char *path;      
    } utimens;
    
    /* access */
    struct {
      int64_t ino;
      char *path;      
    } access;
    
    /* readlink */
    struct {
      int64_t ino;
      char *path;      
    } readlink;
    
    /* mknod */
    struct {
      int32_t rdev;
      char *path;      
    } mknod;
    
    /* mkdir */
    struct {
      char *path;      
    } mkdir;
    
    /* unlink */
    struct {
      int64_t ino;
      char *path;      
    } unlink;
    
    /* rmdir */
    struct {
      int64_t ino;
      char *path;      
    } rmdir;
    
    /* symlink */
    struct {
      int64_t ino;
      char *linkname;
      char *path;      
    } symlink;
    
    /* rename */
    struct {
      int64_t old_ino;
      int64_t new_ino;
      int32_t oldpath_len;
      char *oldpath;      
      int32_t newpath_len;
      char *newpath;      
    } rename;
    
    /* link */
    struct {
      int64_t old_ino;
      int32_t oldpath_len;
      char *oldpath;      
      int32_t newpath_len;
      char *newpath;
    } link;
    
    /* create */
    struct {
      int32_t flags;
      char *path;      
    } create;
    
    /* open */
    struct {
      int64_t ino;
      char *path;
    } open;
    
    /* readv */
    struct {
      int32_t size;
      int64_t off;
    } readv;
    
    /* writev */
    struct {
      int32_t size;
      int64_t off;
      void *buf;
    } writev;
    
    /* fsync */
    struct {
      int32_t datasync;
    } fsync;
    
    /* opendir */
    struct {
      int64_t ino;
      char *path;
    } opendir;
	
    /* getdents */
    struct {
      int32_t size;
      int32_t flag;
      int64_t off;
    } getdents;
    
    /* setdents */
    struct {
      int32_t flags;
      int32_t count;
      void *data;
    } setdents;
    
    /* fsyncdir */
    struct {
      int32_t datasync;
    } fsyncdir;
    
    /* statfs */
    struct {
      int64_t ino;
      char *path;
    } statfs;
    
    /* setxattr */
    struct {
      int64_t ino;
      char *path;
      dict_t *dict;
    } setxattr;
    
    /* getxattr */
    struct {
      int64_t ino;
      int32_t path_len;
      char *path;
      int32_t name_len;
      char *name;
    } getxattr;
    
    /* removexattr */
    struct {
      int64_t ino;
      int32_t name_len; /* len of name */
      char *name;
      char *path;
    } removexattr;
    
    /* lk */
    struct {
      int32_t cmd;
      struct flock lock;
    } lk;
    
    /* readdir */
    struct {
      int32_t size;
      int64_t off;
    } readdir;
    
    /* checksum */
    struct {
      int64_t ino;
      char *path;
    } checksum;
    
    /* setvolume */
    struct {
      char *version;
    } setvolume;
    
    /* lock */
    struct {
      char *path;
    } lock;
    
    /* unlock */
    struct {
      char *path;
    } unlock;
  } args;
}__attribute__ ((__packed__));

#endif

#if 0  
 {{
  union {
    /* lookup */
    struct {
      int64_t ino;
      dict_t *dict;
    } lookup_cbk;
    
    /* readlink */
    struct {
      char *buf;
    } readlink_cbk;
	
    /* mknod */
    struct {
      int64_t ino;
    } mknod_cbk;
    
    /* mkdir */
    struct {
      int64_t ino;
    } mkdir_cbk;
    
    /* symlink */
    struct {
      int64_t ino;
    } symlink_cbk;
    
    /* link */
    struct {
      int64_t ino;
    } link_cbk;
    
    /* create */
    struct {
      int32_t fd_num;
      int64_t ino;
    } create_cbk;
    
    /* open */
    struct {
      int32_t fd_num;
    } open_cbk;
    
    /* readv */
    struct {
      void *buf;
    } readv_cbk;
    
    /* opendir */
    struct {
      int32_t fd_num;
    } opendir_cbk;
    
    /* getdents */
    struct {
      int32_t count;
      void *data;
    } getdents_cbk;
    
    /* statfs */
    struct {
      struct statvfs buf;
    } statfs_cbk;
    
    /* getxattr */
    struct {
      dict_t *dict;
    } getxattr_cbk;
    
    /* lk */
    struct {
      struct flock lock;
    } lk_cbk;
    
    /* readdir */
    struct {
      void *entries;
    } readdir_cbk;
    
    /* stats */
    struct {
      void *buf; /* TODO: can make it a larger structure */
    } stats_cbk;
    
    /* checksum */
    struct {
      char *file_checksum; /* [4096] fixed will do */
      char *dir_checksum;
    } checksum_cbk;

    /* setvolume */
    struct {
      char *error;
    } setvolume_cbk;

    /* getspec */
    struct {
      void *specfiledata;
    } getspec_cbk;
    
    /* listlocks */ /* TODO*/
    struct {
      char *path;
    } listlocks_cbk;
  } args;
}__attribute__ ((__packed__));

#endif
