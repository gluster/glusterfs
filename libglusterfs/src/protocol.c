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
#include <errno.h>

#include "protocol.h"
#include "transport.h"
#include "logging.h"
#include "common-utils.h"

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
  /* FIXME: SERIOUS ERROR: memory buf should always be followed by
  length. You should check if sufficient length is passed at the
  entry. Also use snprintf instead of sprintf. */

  GF_ERROR_IF_NULL (b);
  GF_ERROR_IF_NULL (buf);

  memcpy (buf, "Block Start\n", START_LEN);
  buf += START_LEN;

  sprintf (buf, "%016" PRIx64 "\n", b->callid);
  buf += CALLID_LEN;

  sprintf (buf, "%08x\n", b->type);
  buf += TYPE_LEN;

  sprintf (buf, "%08x\n", b->op);
  buf += OP_LEN;

  snprintf (buf, NAME_LEN, "%s", b->name);
  buf[NAME_LEN-1] = '\n';
  buf += NAME_LEN;

  sprintf (buf, "%032x\n", b->size);
  buf += SIZE_LEN;

  memcpy (buf, b->data, b->size);

  buf += b->size;

  memcpy (buf, "Block End\n", END_LEN);
  return (0);
}

int32_t 
gf_block_serialized_length (gf_block_t *b)
{
  GF_ERROR_IF_NULL (b);
  
  return (START_LEN + CALLID_LEN + TYPE_LEN + OP_LEN +
	  NAME_LEN + SIZE_LEN + b->size + END_LEN);
}

gf_block_t *
gf_block_unserialize (int32_t fd)
{
  gf_block_t *blk = gf_block_new (0);
  int32_t header_len = START_LEN + CALLID_LEN + TYPE_LEN + OP_LEN +
    NAME_LEN + SIZE_LEN;
  char *header_buf = alloca (header_len);
  char *header = header_buf;
  char *endptr;

  int32_t ret = full_read (fd, header, header_len);

  if (ret == -1) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "full_read failed");
    goto herr;
  }

  //  fprintf (stderr, "----------\n[READ]\n----------\n");
  //  write (2, header, header_len);

  if (!((header[START_LEN-1] == '\n') &&
	(header[START_LEN+CALLID_LEN-1] == '\n') &&
	(header[START_LEN+CALLID_LEN+TYPE_LEN-1]  == '\n') &&
	(header[START_LEN+CALLID_LEN+TYPE_LEN+OP_LEN-1] == '\n') &&
	(header[START_LEN+CALLID_LEN+TYPE_LEN+OP_LEN+NAME_LEN-1] == '\n') &&
	(header[START_LEN+CALLID_LEN+TYPE_LEN+OP_LEN+NAME_LEN+SIZE_LEN-1]
	 == '\n'))) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_ERROR,
	    "protocol header corrupted");
    goto herr;
  }

  header[START_LEN-1] =
    header[START_LEN+CALLID_LEN-1] =
    header[START_LEN+CALLID_LEN+TYPE_LEN-1] =
    header[START_LEN+CALLID_LEN+TYPE_LEN+OP_LEN-1] =
    header[START_LEN+CALLID_LEN+TYPE_LEN+OP_LEN+NAME_LEN-1] =
    header[START_LEN+CALLID_LEN+TYPE_LEN+OP_LEN+NAME_LEN+SIZE_LEN-1] =
    '\0';

  if (strncmp (header, "Block Start", START_LEN) != 0) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "expected 'Block Start' not found");
    goto herr;
  }
  header += START_LEN;

  blk->callid = strtoll (header, &endptr, 16);
  if (*endptr) {
    gf_log ("libglusterfs/protocl",
	    GF_LOG_ERROR,
	    "invalid call id");
    goto herr;
  }
  header += CALLID_LEN;

  blk->type = strtol (header, &endptr, 16);
  if (*endptr) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_ERROR,
	    "invalid packet type");
    goto herr;
  }
  header += TYPE_LEN;
  
  blk->op = strtol (header, &endptr, 16);
  if (*endptr) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "error reading op");
    goto herr;
  }
  header += OP_LEN;
  
  memcpy (blk->name, header, NAME_LEN-1);
  header += NAME_LEN;

  blk->size = strtol (header, &endptr, 16);
  if (*endptr) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "error reading block size");
    goto herr;
  }

  if (blk->size < 0) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "block size less than zero");
    goto err;
  }

  char *buf = malloc ( blk->size);
  ret = full_read (fd, buf, blk->size);
  if (ret == -1) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "full_read of block failed");
    free (buf);
    goto err;
  }
  blk->data = buf;
  
  char end[END_LEN+1] = {0,};
  ret = full_read (fd, end, END_LEN);
  if ((ret != 0) || (strncmp (end, "Block End\n", END_LEN) != 0)) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "full_read of end-signature failed");
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
  int32_t header_len = START_LEN + CALLID_LEN + TYPE_LEN + OP_LEN +
    NAME_LEN + SIZE_LEN;
  char header_buf[header_len];
  char *header = &header_buf[0];
  char *endptr;

  int32_t ret = trans->ops->recieve (trans, header, header_len);
  if (ret == -1) {
    gf_log ("libglusterfs/protocol", GF_LOG_DEBUG, "full_read of header failed");
    goto herr;
  }

  //  fprintf (stderr, "----------\n[READ]\n----------\n");
  //  write (2, header, header_len);

  /*
  gf_log ("libglusterfs/protocol",
	  GF_LOG_DEBUG,
	  "newline checks - %x.%x.%x.%x.%x.%x",
	  header[START_LEN-1],
	  header[START_LEN+CALLID_LEN-1],
	  header[START_LEN+CALLID_LEN+TYPE_LEN-1],
	  header[START_LEN+CALLID_LEN+TYPE_LEN+OP_LEN-1],
	  header[START_LEN+CALLID_LEN+TYPE_LEN+OP_LEN+NAME_LEN-1],
	  header[START_LEN+CALLID_LEN+TYPE_LEN+OP_LEN+NAME_LEN+SIZE_LEN-1]);
  */

  if (!((header[START_LEN-1] == '\n') &&
	(header[START_LEN+CALLID_LEN-1] == '\n') &&
	(header[START_LEN+CALLID_LEN+TYPE_LEN-1]  == '\n') &&
	(header[START_LEN+CALLID_LEN+TYPE_LEN+OP_LEN-1] == '\n') &&
	(header[START_LEN+CALLID_LEN+TYPE_LEN+OP_LEN+NAME_LEN-1] == '\n') &&
	(header[START_LEN+CALLID_LEN+TYPE_LEN+OP_LEN+NAME_LEN+SIZE_LEN-1]
	 == '\n'))) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_ERROR,
	    "protocol header corrupted (missing newlines)");
    goto herr;
  }

  header[START_LEN-1] =
    header[START_LEN+CALLID_LEN-1] =
    header[START_LEN+CALLID_LEN+TYPE_LEN-1] =
    header[START_LEN+CALLID_LEN+TYPE_LEN+OP_LEN-1] =
    header[START_LEN+CALLID_LEN+TYPE_LEN+OP_LEN+NAME_LEN-1] =
    header[START_LEN+CALLID_LEN+TYPE_LEN+OP_LEN+NAME_LEN+SIZE_LEN-1] =
    '\0';

  if (strncmp (header, "Block Start", START_LEN) != 0) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "expected 'Block Start' not found");
    goto herr;
  }
  header += START_LEN;

  blk->callid = strtoll (header, &endptr, 16);
  if (*endptr) {
    gf_log ("libglusterfs/protocl",
	    GF_LOG_ERROR,
	    "invalid call id");
    goto herr;
  }
  header += CALLID_LEN;

  blk->type = strtol (header, &endptr, 16);
  if (*endptr) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_ERROR,
	    "invalid packet type");
    goto herr;
  }
  header += TYPE_LEN;
  
  blk->op = strtol (header, &endptr, 16);
  if (*endptr) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "error reading op");
    goto herr;
  }
  header += OP_LEN;
  
  memcpy (blk->name, header, NAME_LEN-1);
  header += NAME_LEN;

  blk->size = strtol (header, &endptr, 16);
  if (*endptr) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "error reading block size");
    goto herr;
  }

  if (blk->size < 0) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "block size less than zero");
    goto err;
  }

  char *buf = malloc (blk->size);
  ret = trans->ops->recieve (trans, buf, blk->size);
  if (ret == -1) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "full_read of block failed");
    free (buf);
    goto err;
  }
  blk->data = buf;
  
  char end[END_LEN+1] = {0,};
  ret = trans->ops->recieve (trans, end, END_LEN);
  if ((ret != 0) || (strncmp (end, "Block End\n", END_LEN) != 0)) {
    gf_log ("libglusterfs/protocol",
	    GF_LOG_DEBUG,
	    "full_read of end-signature failed");
    free (buf);
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
  int32_t header_len = (START_LEN + 
			CALLID_LEN + 
			TYPE_LEN +
			OP_LEN +
			NAME_LEN +
			SIZE_LEN);
  int32_t footer_len = END_LEN;
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

