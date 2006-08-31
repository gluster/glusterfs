#include <stdlib.h>
#include <stdio.h>

#include "protocol.h"

gf_block
*gf_block_new (void)
{
  gf_block *b = calloc (1, sizeof (gf_block));
  b->type = 0;
  b->code = 0;
  b->size = 0;
  strcpy (b->name, "                         NONAME");
  
  return b;
}

int
gf_block_serialize (gf_block *b, char *buf)
{
  memcpy (buf, "Block Start\n", START_LEN);
  buf += START_LEN;

  sprintf (buf, "%08o\n", b->type);
  buf += TYPE_LEN;

  sprintf (buf, "%08o\n", b->code);
  buf += CODE_LEN;

  snprintf (buf, NAME_LEN, "%s\n", b->name);
  buf += NAME_LEN;

  sprintf (buf, "%032o\n", b->size);
  buf += SIZE_LEN;

  memcpy (buf, b->data, b->size);
  buf += b->size;

  memcpy (buf, "Block End\n", END_LEN);
}

int
gf_block_serialized_length (gf_block *b)
{
  return (START_LEN + TYPE_LEN + CODE_LEN +
	  NAME_LEN + SIZE_LEN + b->size + END_LEN);
}

gf_block *
gf_block_unserialize (int fd)
{
  gf_block *blk = gf_block_new ();
  int header_len = START_LEN + TYPE_LEN + CODE_LEN +
    NAME_LEN + SIZE_LEN;
  char *header = malloc (header_len);
  int ret = read (fd, header, header_len);

  if (ret != header_len) /* FIXME: Can we assume that we'll always get header_len bytes */
    goto err;            /* at once? */

  if (strncmp (header, "Block Start\n", START_LEN) != 0) 
    goto err;
  header += START_LEN;

  ret = sscanf (header, "%o\n", &blk->type);
  if (ret != 1)
    goto err;
  header += TYPE_LEN;
  
  ret = sscanf (header, "%o\n", &blk->code);
  if (ret != 1)
    goto err;
  header += CODE_LEN;
  
  memcpy (blk->name, header, NAME_LEN-1);
  header += NAME_LEN;

  ret = sscanf (header, "%o\n", &blk->size);
  if (ret != 1)
    goto err;

  if (blk->size < 0)
    goto err;

  int bytes_read = 0;
  char *buf = malloc (blk->size);
  char *p = buf;
  
  while (bytes_read < blk->size) {
    int ret = read (fd, p, blk->size - bytes_read);
    if (ret <= 0) {
      free (buf);
      goto err;
    }
    
    bytes_read += ret;
    p += bytes_read;
  }

  blk->data = buf;
  char end[END_LEN];
  ret = read (fd, buf, END_LEN);
  if ((ret != END_LEN) || (strncmp (end, "Block End\n", END_LEN) != 0))
    goto err;

  return blk;
  
 err:
  free (blk);
  return NULL;
}
