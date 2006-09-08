#include <stdlib.h>
#include <stdio.h>

#include "protocol.h"
#include <errno.h>
#include "logging.h"
gf_block
*gf_block_new (void)
{
  gf_block *b = calloc (1, sizeof (gf_block));
  b->type = 0;
  b->op = 0;
  b->size = 0;
  strcpy (b->name, "                         NONAME");
  
  return b;
}

int
gf_block_serialize (gf_block *b, char *buf)
{
  /* FIXME: SERIOUS ERROR: memory buf should always be followed by
  length. You should check if sufficient length is passed at the
  entry. Also use snprintf instead of sprintf. */

  if (!b || !buf)
    {
      errno = EINVAL;
      return (-1);
    }

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
  /* gowda debug */
  if (strstr (buf, "NR_ENTRIES")){
    gf_log ("libglusterfs", LOG_CRITICAL, "protocol.c->serialize: vikas string: %s\nbuf_size: %d\n",buf, b->size);
  }

  buf += b->size;

  memcpy (buf, "Block End\n", END_LEN);
  return (0);
}

int
gf_block_serialized_length (gf_block *b)
{
  if (!b)
    {
      errno = EINVAL;
      return (-1);
    }
  
  return (START_LEN + TYPE_LEN + OP_LEN +
	  NAME_LEN + SIZE_LEN + b->size + END_LEN);
}

gf_block *
gf_block_unserialize (int fd)
{
  gf_block *blk = gf_block_new ();
  int header_len = START_LEN + TYPE_LEN + OP_LEN +
    NAME_LEN + SIZE_LEN;
  char *header = malloc (header_len);

  int ret = full_read (fd, header, header_len);
  if (ret == -1)
    goto err;

  //  fprintf (stderr, "----------\n[READ]\n----------\n");
  //  write (2, header, header_len);

  if (strncmp (header, "Block Start\n", START_LEN) != 0) 
    goto err;
  header += START_LEN;

  ret = sscanf (header, "%o\n", &blk->type);
  if (ret != 1)
    goto err;
  header += TYPE_LEN;
  
  ret = sscanf (header, "%o\n", &blk->op);
  if (ret != 1)
    goto err;
  header += OP_LEN;
  
  memcpy (blk->name, header, NAME_LEN-1);
  header += NAME_LEN;

  ret = sscanf (header, "%o\n", &blk->size);
  if (ret != 1)
    goto err;

  if (blk->size < 0)
    goto err;

  char *buf = malloc (blk->size);
  ret = full_read (fd, buf, blk->size);
  if (ret == -1) {
    free (buf);
    goto err;
  }
  /* gowda debug */
  if (strstr (buf, "NR_ENTRIES")){
    gf_log ("libglusterfs", LOG_CRITICAL, "protocol.c->unserialize: vikas string: %s\n blk_size: %d\n", buf, blk->size);
  }
  blk->data = buf;
  
  char end[END_LEN+1] = {0,};
  ret = full_read (fd, end, END_LEN);
  if ((ret != 0) || (strncmp (end, "Block End\n", END_LEN) != 0))
    goto err;

  if (strstr (buf, "NR_ENTRIES")){
    gf_log ("libglusterfs", LOG_CRITICAL, "protocol.c->unserialize: block end string: %s\n", end);
  }
  //  write (2, buf, bytes_read);
  //  write (2, end, END_LEN);
  
  return blk;
  
 err:
  free (blk);
  return NULL;
}
