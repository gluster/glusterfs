#include <inttypes.h>

#include "dict.h"

int main (void)
{
  dict_t *d = get_new_dict ();
  int32_t len;

  dict_set (d, "cow", str_to_data ("calf"));
  dict_set (d, "dog", str_to_data ("puppy"));
  dict_set (d, "remote-subvolume", str_to_data ("who knows"));
  dict_del (d, "remote-subvolume");
  dict_set (d, "cat", str_to_data ("kitten"));

  gf_block *blk = gf_block_new ();


  len = dict_serialized_length (d);
  char *buf = malloc (len) + 1;
  dict_serialize (d, buf);
  buf[len] = '\0';
  blk->data = malloc (len);
  blk->size = len;
  memcpy (blk->data, buf, len);
  
  int blen = gf_block_serialized_length (blk);
  char *bbuf = malloc (blen);

  gf_block_serialize (blk, bbuf);

  write (1, bbuf, blen);

  dict_t *fill = get_new_dict ();
  dict_unserialize (buf, len, &fill);

  printf ("cow: %s\n", data_to_str (dict_get (fill, "cow")));
  printf ("dog: %s\n", data_to_str (dict_get (fill, "dog")));
  printf ("cat: %s\n", data_to_str (dict_get (fill, "cat")));

  return 0;
}
