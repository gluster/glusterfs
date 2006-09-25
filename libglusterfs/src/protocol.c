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
  License aint64_t with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "protocol.h"
#include "logging.h"
#include "common-utils.h"

gf_block
*gf_block_new (void)
{
  gf_block *b = calloc (1, sizeof (gf_block));
  b->type = 0;
  b->op = 0;
  b->size = 0;
  strcpy (b->name, "                               ");
  
  return b;
}

int
gf_block_serialize (gf_block *b, char *buf)
{
  /* FIXME: SERIOUS ERROR: memory buf should always be followed by
  length. You should check if sufficient length is passed at the
  entry. Also use snprintf instead of sprintf. */

  GF_ERROR_IF_NULL (b);
  GF_ERROR_IF_NULL (buf);

  memcpy (buf, "Block Start\n", START_LEN);
  buf += START_LEN;

  sprintf (buf, "%08o\n", b->type);
  buf += TYPE_LEN;

  sprintf (buf, "%08o\n", b->op);
  buf += OP_LEN;

  snprintf (buf, NAME_LEN, "%s\n", b->name);
  buf += NAME_LEN;

  sprintf (buf, "%032o\n", b->size);
  buf += SIZE_LEN;

  memcpy (buf, b->data, b->size);

  buf += b->size;

  memcpy (buf, "Block End\n", END_LEN);
  return (0);
}

int
gf_block_serialized_length (gf_block *b)
{
  GF_ERROR_IF_NULL (b);
  
  return (START_LEN + TYPE_LEN + OP_LEN +
	  NAME_LEN + SIZE_LEN + b->size + END_LEN);
}

gf_block *
gf_block_unserialize (int32_t fd)
{
  gf_block *blk = gf_block_new ();
  int32_t header_len = START_LEN + TYPE_LEN + OP_LEN +
    NAME_LEN + SIZE_LEN;
  char *header_buf = calloc (header_len, 1);
  char *header = header_buf;

  int32_t ret = full_read (fd, header, header_len);
  if (ret == -1) {
    gf_log ("libglusterfs/protocol", GF_LOG_DEBUG, "full_read failed");
    goto err;
  }

  //  fprintf (stderr, "----------\n[READ]\n----------\n");
  //  write (2, header, header_len);

  if (strncmp (header, "Block Start\n", START_LEN) != 0) {
    gf_log ("libglusterfs/protocol", GF_LOG_DEBUG, "expected 'Block Start' not found");
    goto err;
  }
  header += START_LEN;

  ret = sscanf (header, "%o\n", &blk->type);
  if (ret != 1) {
    gf_log ("libglusterfs/protocol", GF_LOG_DEBUG, "error reading block type");
    goto err;
  }
  header += TYPE_LEN;
  
  ret = sscanf (header, "%o\n", &blk->op);
  if (ret != 1) {
    gf_log ("libglusterfs/protocol", GF_LOG_DEBUG, "error reading op");
    goto err;
  }
  header += OP_LEN;
  
  memcpy (blk->name, header, NAME_LEN-1);
  header += NAME_LEN;

  ret = sscanf (header, "%o\n", &blk->size);
  if (ret != 1) {
    gf_log ("libglusterfs/protocol", GF_LOG_DEBUG, "error reading block size");
    goto err;
  }

  free (header_buf);

  if (blk->size < 0) {
    gf_log ("libglusterfs/protocol", GF_LOG_DEBUG, "block size less than zero");
    goto err;
  }

  char *buf = calloc (1, blk->size);
  ret = full_read (fd, buf, blk->size);
  if (ret == -1) {
    gf_log ("libglusterfs/protocol", GF_LOG_DEBUG, "full_read failed");
    free (buf);
    goto err;
  }
  blk->data = buf;
  
  char end[END_LEN+1] = {0,};
  ret = full_read (fd, end, END_LEN);
  if ((ret != 0) || (strncmp (end, "Block End\n", END_LEN) != 0)) {
    gf_log ("libglusterfs/protocol", GF_LOG_DEBUG, "full_read failed");
    goto err;
  }

  return blk;
  
 err:
  free (blk);
  return NULL;
}
