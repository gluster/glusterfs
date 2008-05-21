/*
   Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "protocol-binary.h"
#include "transport.h"
#include "logging.h"
#include "common-utils.h"

static int32_t
validate_header (void *header)
{
  if (!header) {
    gf_log ("protocol", GF_LOG_ERROR, "Header missing");
    return 0;
  }

  if (gf_proto_get_signature (header) != GF_PROTO_HEADER_SIGN) {
    gf_log ("protocol", GF_LOG_DEBUG, "0x%x got, 0x%x required", 
	    gf_proto_get_signature (header), GF_PROTO_HEADER_SIGN);
    gf_log ("protocol", GF_LOG_ERROR, "Signature Missmatch :O");
    return 0;
  }

  if (gf_proto_get_version (header) != GF_PROTO_VERSION) {
    gf_log ("protocol", GF_LOG_DEBUG, "0x%x got, 0x%x required", 
	    gf_proto_get_version (header), GF_PROTO_VERSION);
    gf_log ("protocol", GF_LOG_ERROR, " Version Missmatch");
    return 0;
  }

  return 1;
}

gf_proto_block_t *
gf_proto_block_new (int64_t callid)
{
  gf_proto_block_t *b = calloc (1, sizeof (gf_proto_block));
  if (!b) {
    gf_log ("protocol", GF_LOG_CRITICAL, "calloc failed");
    return NULL;
  }

  b->type = 0;
  b->op = 0;
  b->size = 0;
  b->callid = callid;
  
  return b;
}

int32_t 
gf_proto_block_serialized_length (gf_proto_block_t *b)
{
  GF_ERROR_IF_NULL (b);
  
  return (GF_PROTO_HEADER_LEN + b->size + GF_PROTO_END_LEN);
}

gf_proto_block_t *
gf_proto_block_unserialize (int32_t fd)
{
  gf_proto_block_t *blk = gf_proto_block_new (0);
  socklen_t sock_len = sizeof (struct sockaddr_in);
  struct sockaddr_in *_sock = NULL;
  int32_t header_len = GF_PROTO_HEADER_LEN;
  char header[GF_PROTO_HEADER_LEN + 1];
  int32_t ret = 0;

  getpeername (fd, _sock, &sock_len);

  ret = gf_full_read (fd, header, header_len);
  if (ret == -1) {
    gf_log ("protocol", GF_LOG_ERROR,
	    "full_read failed: peer (%s:%d)",
	    inet_ntoa (_sock->sin_addr), ntohs (_sock->sin_port));
    goto herr;
  }

  if (!validate_header (header)) {
    gf_log ("protocol", GF_LOG_ERROR,
	    "validate_header failed: peer (%s:%d)",
	    inet_ntoa (_sock->sin_addr), ntohs (_sock->sin_port));
    goto herr;
  }

  blk->callid = gf_proto_get_callid (header);
  blk->type   = gf_proto_get_type (header);
  blk->op     = gf_proto_get_op (header);
  blk->size   = gf_proto_get_size (header);
  if (blk->size) {
    char end[4];
    void *buf = malloc (blk->size);
    ERR_ABORT (buf);
    ret = gf_full_read (fd, buf, blk->size);
    if (ret == -1) {
      gf_log ("protocol", GF_LOG_ERROR,
	      "full_read of block failed: peer (%s:%d)",
	      inet_ntoa (_sock->sin_addr), ntohs (_sock->sin_port));
      FREE (buf);
      goto err;
    }
    blk->args = buf;
    ret = gf_full_read (fd, end, GF_PROTO_END_LEN);
    if ((ret == -1) || ((end[0] != ';') && (end[1] != 'o'))) { 
      gf_log ("protocol", GF_LOG_ERROR,
	      "full_read of end-signature failed: peer (%s:%d)",
	      inet_ntoa (_sock->sin_addr), ntohs (_sock->sin_port));
      FREE (buf);
      goto err;
    }
  } else {
    gf_log ("protocol", GF_LOG_WARNING, "Block Size is 0");
  }
  return blk;

 herr:
 err:
  FREE (blk);
  return NULL;
}

gf_proto_block_t *
gf_proto_block_unserialize_transport (struct transport *trans,
				const int32_t max_block_size)
{
  if (!trans)
    return NULL;

  gf_proto_block_t *blk = gf_proto_block_new (0);
  char header[GF_PROTO_HEADER_LEN + 1];
  int32_t ret = 0;
 
  ret = trans->ops->recieve (trans, header, GF_PROTO_HEADER_LEN);
  if (ret == -1) {
    gf_log (trans->xl->name, GF_LOG_ERROR,
	    "EOF from peer (%s:%d)",
	    inet_ntoa (trans->peerinfo.sockaddr.sin_addr),
	    ntohs (trans->peerinfo.sockaddr.sin_port));
    goto herr;
  }

  if (!validate_header (header)) {
    gf_log ("protocol", GF_LOG_ERROR,
	    "validate_header failed: peer (%s:%d)",
	    inet_ntoa (trans->peerinfo.sockaddr.sin_addr), 
	    ntohs (trans->peerinfo.sockaddr.sin_port));
    goto herr;
  }

  blk->callid = gf_proto_get_callid (header);
  blk->type   = gf_proto_get_type (header);
  blk->op     = gf_proto_get_op (header);
  blk->size   = gf_proto_get_size (header);
  if (max_block_size && (blk->size > max_block_size)) {
    gf_log (trans->xl->name, GF_LOG_ERROR,
	    "frame size (%"PRId32") > max (%"PRId32")",
	    blk->size, max_block_size);
    /* block size exceeds the maximum block size permitted by the protocol controlling 
     * this transport */
    goto herr;
  }

  /* TODO: do this check with lock */
  if (blk->size) {
    char end[4];
    char *buf = NULL;
    if (trans->buf)
      data_unref (trans->buf);
    buf = calloc (1, blk->size);
    ERR_ABORT (buf);
    ret = trans->ops->recieve (trans, buf, blk->size);    
    if (ret == -1) {
      gf_log (trans->xl->name, GF_LOG_ERROR,
	      "full_read of block failed: peer (%s:%d)",
	      inet_ntoa (trans->peerinfo.sockaddr.sin_addr),
	      ntohs (trans->peerinfo.sockaddr.sin_port));
      goto err;
    }
    blk->args = buf;
    
    ret = trans->ops->recieve (trans, end, GF_PROTO_END_LEN);
    if ((ret == -1) || ((end[0] != ';') && (end[1] != 'o'))) {
      gf_log (trans->xl->name, GF_LOG_ERROR,
	      "full_read of end-signature failed: peer (%s:%d)",
	      inet_ntoa (trans->peerinfo.sockaddr.sin_addr),
	      ntohs (trans->peerinfo.sockaddr.sin_port));
      goto err;
    }
  } else {
    gf_log (trans->xl->name, GF_LOG_WARNING, "Block size 0");
  }

  return blk;

 herr:
 err:
  FREE (blk);
  return NULL;
}

int32_t 
gf_proto_get_struct_from_buf (void *buf, gf_args_t *args, int32_t size)
{
  if (!buf) 
    return -1;
  
  char *var_buf = (char *)buf + GF_PROTO_FIXED_DATA_LEN;
  int32_t i = 0;
  int32_t len = GF_PROTO_FIXED_DATA_LEN;

  args->fixed1 = ntohl (((int32_t *)buf)[0]);
  args->fixed2 = ntohl (((int32_t *)buf)[1]);
  args->fixed3 = ntohl (((int32_t *)buf)[2]);
  args->fixed4 = ntohl (((int32_t *)buf)[3]);

  for (i = 0; len < size ; i++) {
    int32_t top = ntohl (((int32_t *)var_buf)[0]);
    int32_t type = ((top & 0xFF000000) >> 24);
    int32_t field_len = (top & 0x00FFFFFF);
    int32_t tmp_len = 0;
    args->fields[i].type = type;
    args->fields[i].len = field_len;
    args->fields[i].ptr = (void *)var_buf + 4;
    if (type != GF_PROTO_CHAR_TYPE)
      tmp_len = (4+field_len +((field_len%4)?(4-(field_len%4)):0));
    else 
      tmp_len = (4+field_len +(4-(field_len%4)));
      
    len += tmp_len;
    var_buf += tmp_len;
  }

  return 0;
}

int32_t 
gf_proto_get_data_len (gf_args_t *buf)
{
  int32_t len = GF_PROTO_FIXED_DATA_LEN;
  int32_t i;
  for (i = 0 ; i < GF_PROTO_MAX_FIELDS; i++) {
    if (!buf->fields[i].len)
      continue;
    int32_t tmp_len = buf->fields[i].len;
    /* Offset it to nearest 4 */
    
    if (buf->fields[i].type == GF_PROTO_IOV_TYPE) {
      int32_t k;
      int32_t count = buf->fields[i].len;
      struct iovec *vec = buf->fields[i].ptr;
      len += 4;
      for (k = 0; k < count; k++) {
	len += vec[k].iov_len;
      }
    } else if (buf->fields[i].type == GF_PROTO_CHAR_TYPE) {
      len += (4 + tmp_len + (4-(tmp_len%4)));
    } else {
      len += (4 + tmp_len + ((tmp_len%4)?(4-(tmp_len%4)):0));
    }
  }

  return len;
}


int32_t
gf_proto_block_iovec_len (gf_proto_block_t *blk)
{
  int32_t count = 2 + 1;
  int32_t i;
  gf_args_t *tmp = blk->args;
  for (i = 0; i < GF_PROTO_MAX_FIELDS; i++) {
    if (tmp->fields[i].len) {
      if (tmp->fields[i].type != GF_PROTO_IOV_TYPE)
	count += 2;
      else
	count += (1 + tmp->fields[i].len);
    }
  }
  return count;
}

int32_t
gf_proto_block_to_iovec (gf_proto_block_t *blk,
                   struct iovec *iov,
                   int32_t *cnt)
{
  int32_t i = 0;
  int32_t k = 2;
  gf_args_t *args = NULL;
  if (!blk || !iov) {
    gf_log ("protocol", GF_LOG_ERROR, "!blk || !iov");
    return 0;
  }

  iov[0].iov_len = GF_PROTO_HEADER_LEN;
  if (iov[0].iov_base) {
    gf_proto_set_header (iov[0].iov_base, blk);
  }

  args = blk->args;
  iov[1].iov_len = GF_PROTO_FIXED_DATA_LEN;
  if (iov[1].iov_base) {
    void *buf = iov[1].iov_base;
    ((int32_t *)buf)[0] = htonl (args->fixed1);
    ((int32_t *)buf)[1] = htonl (args->fixed2);
    ((int32_t *)buf)[2] = htonl (args->fixed3);
    ((int32_t *)buf)[3] = htonl (args->fixed4);
  }

  for (i = 0; i < GF_PROTO_MAX_FIELDS; i++) {
    int len = args->fields[i].len;
    char type = args->fields[i].type;
    /* Check if IOV */
    if (type == GF_PROTO_IOV_TYPE) {
      int32_t j;
      int32_t count = args->fields[i].len;
      struct iovec *vec = args->fields[i].ptr;
      len = 0;
      for (j = 0; j < count; j++) {
	len += vec[j].iov_len;
      }
      iov[k].iov_len = 4;
      if (iov[k].iov_base) {
	int32_t first_word = (len | type << 24);
	((int32_t *)iov[k].iov_base)[0] = htonl (first_word);
      }
      k++;
      for (j = 0; j < count; j++) {
	iov[k].iov_len = vec[j].iov_len;
	iov[k].iov_base = vec[j].iov_base;
	k++;
      }
      continue;
    }
    if (len) {
      iov[k].iov_len = 4;
      if (iov[k].iov_base) {	
	int32_t first_word = (len | type << 24);
	((int32_t *)iov[k].iov_base)[0] = htonl (first_word);
      }
      k++;
      if (type == GF_PROTO_CHAR_TYPE)
	iov[k].iov_len = len + (4 - (len%4));
      else
	iov[k].iov_len = len + ((len%4)?(4 - (len%4)):0);

      iov[k].iov_base = args->fields[i].ptr;
      k++;
    }
  }

  iov[k].iov_len = GF_PROTO_END_LEN;
  if (iov[k].iov_base) {
    iov[k].iov_base = ";o";
  }  

  *cnt = ++k;

  return 0;
}

