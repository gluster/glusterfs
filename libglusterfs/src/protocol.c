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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "transport.h"
#include "logging.h"
#include "common-utils.h"

static int32_t
validate_header (char *header)
{

  if (!((header[GF_START_LEN-1] == '\n') &&
	(header[GF_START_LEN+GF_CALLID_LEN-1] == '\n') &&
	(header[GF_START_LEN+GF_CALLID_LEN+GF_TYPE_LEN-1]  == '\n') &&
	(header[GF_START_LEN+GF_CALLID_LEN+GF_TYPE_LEN+GF_OP_LEN-1] == '\n') &&
	(header[GF_START_LEN+GF_CALLID_LEN+GF_TYPE_LEN+GF_OP_LEN+GF_NAME_LEN-1] == '\n') &&
	(header[GF_START_LEN+GF_CALLID_LEN+GF_TYPE_LEN+GF_OP_LEN+GF_NAME_LEN+GF_SIZE_LEN-1]
	 == '\n'))) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_ERROR,
	    "validate_header: protocol header corrupted");
    return 0;
  }

  header[GF_START_LEN-1] =
    header[GF_START_LEN+GF_CALLID_LEN-1] =
    header[GF_START_LEN+GF_CALLID_LEN+GF_TYPE_LEN-1] =
    header[GF_START_LEN+GF_CALLID_LEN+GF_TYPE_LEN+GF_OP_LEN-1] =
    header[GF_START_LEN+GF_CALLID_LEN+GF_TYPE_LEN+GF_OP_LEN+GF_NAME_LEN-1] =
    header[GF_START_LEN+GF_CALLID_LEN+GF_TYPE_LEN+GF_OP_LEN+GF_NAME_LEN+GF_SIZE_LEN-1] = '\0';

  return 1;
}

gf_block_t
*gf_block_new (int64_t callid)
{
  gf_block_t *b = calloc (1, sizeof (gf_block));

  b->type = 0;
  b->op = 0;
  b->size = 0;
  b->callid = callid;
  memset (b->name, '-', 32);
  
  return b;
}

int32_t 
gf_block_serialize (gf_block_t *b, char *buf)
{
  GF_ERROR_IF_NULL (b);
  GF_ERROR_IF_NULL (buf);

  memcpy (buf, "Block Start\n", GF_START_LEN);
  buf += GF_START_LEN;

  sprintf (buf, "%016" PRIx64 "\n", b->callid);
  buf += GF_CALLID_LEN;

  sprintf (buf, "%08x\n", b->type);
  buf += GF_TYPE_LEN;

  sprintf (buf, "%08x\n", b->op);
  buf += GF_OP_LEN;

  snprintf (buf, GF_NAME_LEN, "%s", b->name);
  buf[GF_NAME_LEN-1] = '\n';
  buf += GF_NAME_LEN;

  sprintf (buf, "%032x\n", b->size);
  buf += GF_SIZE_LEN;

  memcpy (buf, b->data, b->size);

  buf += b->size;

  memcpy (buf, "Block End\n", GF_END_LEN);

  return (0);
}

int32_t 
gf_block_serialized_length (gf_block_t *b)
{
  GF_ERROR_IF_NULL (b);
  
  return (GF_START_LEN + GF_CALLID_LEN + GF_TYPE_LEN + GF_OP_LEN +
	  GF_NAME_LEN + GF_SIZE_LEN + b->size + GF_END_LEN);
}

gf_block_t *
gf_block_unserialize (int32_t fd)
{
  gf_block_t *blk = gf_block_new (0);

  int32_t header_len = GF_START_LEN + GF_CALLID_LEN + GF_TYPE_LEN + GF_OP_LEN +
    GF_NAME_LEN + GF_SIZE_LEN;
  char *header_buf = alloca (header_len);
  char *header = header_buf;
  char *endptr;

  int32_t ret = full_read (fd, header, header_len);

  if (ret == -1) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "gf_block_unserialize: full_read failed");
    goto herr;
  }

  //  fprintf (stderr, "----------\n[READ]\n----------\n");
  //  write (2, header, header_len);

  if (!validate_header (header))
    goto herr;

  if (strncmp (header, "Block Start", GF_START_LEN) != 0) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "gf_block_unserialize: expected 'Block Start' not found");
    goto herr;
  }
  header += GF_START_LEN;

  blk->callid = strtoll (header, &endptr, 16);
  if (*endptr) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_ERROR,
	    "gf_block_unserialize: invalid call id");
    goto herr;
  }
  header += GF_CALLID_LEN;

  blk->type = strtol (header, &endptr, 16);
  if (*endptr) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_ERROR,
	    "gf_block_unserialize: invalid packet type");
    goto herr;
  }
  header += GF_TYPE_LEN;
  
  blk->op = strtol (header, &endptr, 16);
  if (*endptr) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "gf_block_unserialize: error reading op");
    goto herr;
  }
  header += GF_OP_LEN;
  
  memcpy (blk->name, header, GF_NAME_LEN-1);
  header += GF_NAME_LEN;

  blk->size = strtol (header, &endptr, 16);
  if (*endptr) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "gf_block_unserialize: error reading block size");
    goto herr;
  }

  if (blk->size < 0) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "gf_block_unserialize: block size less than zero");
    goto err;
  }

  char *buf = malloc (blk->size);
  ret = full_read (fd, buf, blk->size);
  if (ret == -1) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "gf_block_unserialize: full_read of block failed");
    free (buf);
    goto err;
  }

  blk->dict = get_new_dict ();
  dict_unserialize (buf, blk->size, &blk->dict);
  if (!blk->dict) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "gf_block_unserialize: dict_unserialize failed");
    goto err;
  }
  blk->dict->extra_free = buf;

  char end[GF_END_LEN+1] = {0,};
  ret = full_read (fd, end, GF_END_LEN);
  if ((ret != 0) || (strncmp (end, "Block End\n", GF_END_LEN) != 0)) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "gf_block_unserialize: full_read of end-signature failed");
    free (buf);
    goto err;
  }

  return blk;

 herr:
 err:
  free (blk);
  return NULL;
}

gf_block_t *
gf_block_unserialize_transport (struct transport *trans)
{
  gf_block_t *blk = gf_block_new (0);
  int32_t header_len = GF_START_LEN + GF_CALLID_LEN + GF_TYPE_LEN + 
    GF_OP_LEN + GF_NAME_LEN + GF_SIZE_LEN;
  char header_buf[header_len];
  char *header = &header_buf[0];
  char *endptr;

  int32_t ret = trans->ops->recieve (trans, header, header_len);
  if (ret == -1) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "gf_block_unserialize_transport: full_read of header failed");
    goto herr;
  }

  if (!validate_header (header))
    goto herr;

  if (strncmp (header, "Block Start", GF_START_LEN) != 0) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "gf_block_unserialize_transport: expected 'Block Start' not found");
    goto herr;
  }

  header += GF_START_LEN;

  blk->callid = strtoll (header, &endptr, 16);
  if (*endptr) {
    gf_log ("libglusterfs/protocl",
	    GF_LOG_ERROR,
	    "gf_block_unserialize_transport: invalid call id");
    goto herr;
  }
  header += GF_CALLID_LEN;

  blk->type = strtol (header, &endptr, 16);
  if (*endptr) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_ERROR,
	    "gf_block_unserialize_transport: invalid packet type");
    goto herr;
  }
  header += GF_TYPE_LEN;
  
  blk->op = strtol (header, &endptr, 16);
  if (*endptr) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "gf_block_unserialize_transport: error reading op");
    goto herr;
  }
  header += GF_OP_LEN;
  
  memcpy (blk->name, header, GF_NAME_LEN-1);
  header += GF_NAME_LEN;

  blk->size = strtol (header, &endptr, 16);
  if (*endptr) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "gf_block_unserialize_transport: error reading block size");
    goto herr;
  }

  if (blk->size < 0) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "gf_block_unserialize_transport: block size less than zero");
    goto err;
  }

  /* TODO: do this check with lock */
  if (trans->buf) {
    int ref;
    pthread_mutex_lock (trans->buf->lock);
    ref = trans->buf->refcount;
    pthread_mutex_unlock (trans->buf->lock);
    if (ref > 1) {
      data_unref (trans->buf);
      trans->buf = NULL;
    }
  }
  if (!trans->buf) {
    trans->buf = data_ref (data_from_dynptr (malloc (blk->size),
					     blk->size));
    trans->buf->lock = calloc (1, sizeof (pthread_mutex_t));
    pthread_mutex_init (trans->buf->lock, NULL);
  }
  if (blk->size > trans->buf->len) {
    free (trans->buf->data);
    trans->buf->data = malloc (blk->size);
  }
  ret = trans->ops->recieve (trans, trans->buf->data, blk->size);

  if (ret == -1) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "gf_block_unserialize_transport: full_read of block failed");
    goto err;
  }

  blk->dict = get_new_dict ();
  blk->dict->lock = calloc (1, sizeof (pthread_mutex_t));
  pthread_mutex_init (blk->dict->lock, NULL);

  dict_unserialize (trans->buf->data, blk->size, &blk->dict);
  if (!blk->dict) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "gf_block_unserialize_transport: dict_unserialize failed");
    goto err;
  }
  dict_set (blk->dict, NULL, trans->buf);
  
  char end[GF_END_LEN+1] = {0,};
  ret = trans->ops->recieve (trans, end, GF_END_LEN);
  if ((ret != 0) || (strncmp (end, "Block End\n", GF_END_LEN) != 0)) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "gf_block_unserialize_transport: full_read of end-signature failed");
    goto err;
  }

  return blk;

 herr:
 err:
  free (blk);
  return NULL;
}

int32_t 
gf_block_iovec_len (gf_block_t *blk)
{
  return 2 + dict_iovec_len (blk->dict);
}

int32_t
gf_block_to_iovec (gf_block_t *blk,
		   struct iovec *iov,
		   int32_t cnt)
{
  int32_t header_len = (GF_START_LEN +
			GF_CALLID_LEN + 
			GF_TYPE_LEN +
			GF_OP_LEN +
			GF_NAME_LEN +
			GF_SIZE_LEN);
  int32_t footer_len = GF_END_LEN;
  int32_t size = dict_serialized_length (blk->dict);

  iov[0].iov_len = header_len;
  if (iov[0].iov_base)
    sprintf (iov[0].iov_base,
	     "Block Start\n"
	     "%016" PRIx64 "\n"
	     "%08x\n"
	     "%08x\n"
	     "%32s\n"
	     "%032x\n",
	     blk->callid,
	     blk->type,
	     blk->op,
	     blk->name,
	     size);

  dict_to_iovec (blk->dict, &iov[1], cnt - 2);

  iov[cnt-1].iov_len = footer_len;
  iov[cnt-1].iov_base = "Block End\n";
  return 0;
}

