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

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#include "common-utils.h"
#include "protocol.h"
#include "glusterfs.h"
#include "dict.h"
#include "hashfn.h"

data_pair_t *
get_new_data_pair ()
{
  return (data_pair_t *) calloc (1, sizeof (data_pair_t));
}

data_t *
get_new_data ()
{
  return (data_t *) calloc (1, sizeof (data_t));
}

dict_t *
get_new_dict_full (int size_hint)
{
  dict_t *dict = calloc (1, sizeof (dict_t));
  dict->hash_size = size_hint;
  dict->members = calloc (size_hint, sizeof (data_pair_t *));
  return dict;
}

dict_t *
get_new_dict (void)
{
  return get_new_dict_full (15);
}

void *
memdup (void *old, 
	int32_t len)
{
  void *newdata = calloc (1, len);
  memcpy (newdata, old, len);
  return newdata;
}

int32_t 
is_data_equal (data_t *one,
	       data_t *two)
{
  if (one == two)
    return 1;

  if (one->len != two->len)
    return 0;

  if (one->data == two->data)
    return 1;

  if (memcmp (one->data, two->data, one->len) == 0)
    return 1;

  return 0;
}

void
data_destroy (data_t *data)
{
  if (data) {
    if (!data->is_static && data->data)
      free (data->data);

    if (!data->is_const)
      free (data);
  }
}

data_t *
data_copy (data_t *old)
{
  data_t *newdata = (data_t *) calloc (1, sizeof (*newdata));
  if (old) {
    newdata->len = old->len;
    if (old->data)
      newdata->data = memdup (old->data, old->len);
  }

  return newdata;
}

int32_t 
dict_set (dict_t *this, 
	  int8_t *key, 
	  data_t *value)
{
  data_pair_t *pair = this->members;
  int32_t count = this->count;

  while (count) {
    if (strcmp (pair->key, key) == 0) {
      data_destroy (pair->value);
      if (strlen (pair->key) < strlen (key))
	pair->key = realloc (pair->key, strlen (key));
      strcpy (pair->key, key);
      pair->value = value;
      return 0;
    }
    pair = pair->next;
    count--;
  }

  pair = (data_pair_t *) calloc (1, sizeof (*pair));
  pair->key = (int8_t *) calloc (1, strlen (key) + 1);
  strcpy (pair->key, key);
  pair->value = (value);
  pair->next = this->members;
  this->members = pair;
  this->count++;

  return 0;
}


int32_t 
dict_case_set (dict_t *this, 
	       int8_t *key, 
	       data_t *value)
{
  data_pair_t *pair = this->members;
  int32_t count = this->count;

  while (count) {
    if (strcasecmp (pair->key, key) == 0) {
      data_destroy (pair->value);
      if (strlen (pair->key) < strlen (key))
	pair->key = realloc (pair->key, strlen (key) + 1);
      strcpy (pair->key, key);
      pair->value = value;
      return 0;
    }
    pair = pair->next;
    count--;
  }

  pair = (data_pair_t *) calloc (1, sizeof (*pair));
  pair->key = (int8_t *) calloc (1, strlen (key) + 1);
  strcpy (pair->key, key);
  pair->value = (value);
  pair->next = this->members;
  this->members = pair;
  this->count++;

  return 0;
}

data_t *
dict_get (dict_t *this,
	  int8_t *key)
{
  data_pair_t *pair = this->members;

  while (pair) {
    if (strcmp (pair->key, key) == 0)
      return (pair->value);
    pair = pair->next;
  }
  return NULL;
}


data_t *
dict_case_get (dict_t *this,
	       int8_t *key)
{
  data_pair_t *pair = this->members;

  while (pair) {
    if (strcasecmp (pair->key, key) == 0)
      return (pair->value);
    pair = pair->next;
  }
  return NULL;
}

void
dict_del (dict_t *this,
	  int8_t *key)
{
  data_pair_t *pair = this->members;
  data_pair_t *prev = NULL;

  while (pair) {
    if (strcmp (pair->key, key) == 0) {
      if (prev)
	prev->next = pair->next;
      else
	this->members = pair->next;
      data_destroy (pair->value);
      free (pair->key);
      free (pair);
      this->count --;
      return;
    }
    prev = pair;
    pair = pair->next;
  }
  return;
}


void
dict_case_del (dict_t *this,
	       int8_t *key)
{
  data_pair_t *pair = this->members;
  data_pair_t *prev = NULL;

  while (pair) {
    if (strcasecmp (pair->key, key) == 0) {
      if (prev)
	prev->next = pair->next;
      else
	this->members = pair->next;
      data_destroy (pair->value);
      free (pair->key);
      free (pair);
      this->count --;
      return;
    }
    prev = pair;
    pair = pair->next;
  }
  return;
}

void
dict_destroy (dict_t *this)
{
  data_pair_t *pair = this->members_list;
  data_pair_t *prev = this->members_list;

  while (prev) {
    pair = pair->next;
    data_destroy (prev->value);
    free (prev->key);
    free (prev);
    prev = pair;
  }

  if (!this->is_static)
    free (this);
  return;
}

/*
  Serialization format:
  ----
  Count:8
  Key_len:8:Value_len:8
  Key
  Value
  .
  .
  .
*/

int32_t 
dict_serialized_length (dict_t *dict)
{
  int32_t len = 9; /* count + \n */
  int32_t count = dict->count;
  data_pair_t *pair = dict->members;

  while (count) {
    len += 18 + strlen (pair->key) + pair->value->len;
    pair = pair->next;
    count--;
  }

  return len;
}

int32_t 
dict_serialize (dict_t *dict, int8_t *buf)
{
  GF_ERROR_IF_NULL (dict);
  GF_ERROR_IF_NULL (buf);

  data_pair_t *pair = dict->members;
  int32_t count = dict->count;

  // FIXME: magic numbers
  uint64_t dcount = dict->count;
  sprintf (buf, "%08"PRIx64"\n", dcount);
  buf += 9;
  while (count) {
    uint64_t keylen = strlen (pair->key);
    uint64_t vallen = pair->value->len;
    sprintf (buf, "%08"PRIx64":%08"PRIx64"\n", keylen, vallen);
    buf += 18;
    memcpy (buf, pair->key, strlen (pair->key));
    buf += strlen (pair->key);
    memcpy (buf, pair->value->data, pair->value->len);
    buf += pair->value->len;
    pair = pair->next;
    count--;
  }
  return (0);
}

dict_t *
dict_unserialize (int8_t *buf, int32_t size, dict_t **fill)
{
  int32_t ret = 0;
  int32_t cnt = 0;

  uint64_t count;
  ret = sscanf (buf, "%"SCNx64"\n", &count);
  (*fill)->count = count;

  if (!ret){
    gf_log ("libglusterfs", GF_LOG_ERROR, "dict.c->dict_unserialize: sscanf on buf failed");
    goto err;
  }
  buf += 9;
  
  if ((*fill)->count == 0){
    gf_log ("libglusterfs", GF_LOG_ERROR, "dict.c->dict_unserialize: (*fill)->count == 0");
    goto err;
  }

  (*fill)->members = NULL; 

  for (cnt = 0; cnt < (*fill)->count; cnt++) {
    data_pair_t *pair = NULL; //get_new_data_pair ();
    data_t *value = NULL; // = get_new_data ();
    int8_t *key = NULL;
    uint64_t key_len, value_len;
    
    ret = sscanf (buf, "%"SCNx64":%"SCNx64"\n", &key_len, &value_len);
    if (ret != 2){
      gf_log ("libglusterfs", GF_LOG_ERROR, "dict.c->dict_unserialize: sscanf for key_len and value_len failed");
      goto err;
    }
    buf += 18;

    key = calloc (1, key_len + 1);
    memcpy (key, buf, key_len);
    buf += key_len;
    key[key_len] = 0;
    
    value = get_new_data ();
    value->len = value_len;
    value->data = calloc (1, value->len + 1);

    pair = get_new_data_pair ();
    pair->key = key;
    pair->value = value;

    pair->next = (*fill)->members;
    (*fill)->members = pair;

    memcpy (value->data, buf, value_len);
    buf += value_len;

    value->data[value->len] = 0;
  }

  goto ret;

 err:
/*   dict_destroy (fill); */
  *fill = NULL; 

 ret:
  return NULL;
}

/*
  Encapsulate a dict in a block and write it to the fd
*/

int32_t 
dict_dump (int32_t fd, dict_t *dict, gf_block *blk, int32_t type)
{
  GF_ERROR_IF_NULL (dict);
  GF_ERROR_IF_NULL (blk);
  
  int32_t dict_len = dict_serialized_length (dict);
  int8_t *dict_buf = malloc (dict_len);
  dict_serialize (dict, dict_buf);

  blk->data = dict_buf;
  blk->type = type;
  blk->size = dict_len;
  int32_t blk_len = gf_block_serialized_length (blk);
  int8_t *blk_buf = malloc (blk_len);
  gf_block_serialize (blk, blk_buf);
  
  int32_t ret = full_write (fd, blk_buf, blk_len);
  
  free (blk_buf);
  free (dict_buf);
  return ret;
}

dict_t *
dict_load (FILE *fp)
{
  dict_t *newdict = get_new_dict ();
  int32_t ret = 0;
  int32_t cnt = 0;

  ret = fscanf (fp, "%x", &newdict->count);
  if (!ret)
    goto err;
  
  if (newdict->count == 0)
    goto err;

  for (cnt = 0; cnt < newdict->count; cnt++) {
    data_pair_t *pair = get_new_data_pair ();
    data_t *value = get_new_data ();
    int8_t *key = NULL;
    int32_t key_len = 0;

    ret = fscanf (fp, "\n%x:%x:", &key_len, &value->len);
    if (ret != 2)
      goto err;
    
    key = calloc (1, key_len + 1);
    ret = fread (key, key_len, 1, fp);
    if (!ret)
      goto err;
    key[key_len] = 0;
    
    value->data = malloc (value->len+1);
    ret = fread (value->data, value->len, 1, fp);
    if (!ret)
      goto err;
    value->data[value->len] = 0;

    pair->key = key;
    pair->value = value;
    pair->next = newdict->members;
    newdict->members = pair;
  }

  goto ret;

 err:
  dict_destroy (newdict);
  newdict = NULL;

 ret:
  return newdict;
}

data_t *
int_to_data (int64_t value)
{
  data_t *data = get_new_data ();
  /*  if (data == NULL) {
    data = get_new_data ();
    data->data = NULL;
  }
  if (data->data == NULL)
    data->data = malloc (32);
  */
  asprintf (&data->data, "%"PRIx64, value);
  data->len = strlen (data->data);
  return data;
}

data_t *
str_to_data (int8_t *value)
{
  data_t *data = get_new_data ();
  /*  if (data == NULL) {
    data = get_new_data ();
    data->data = NULL;
  }
  if (data->data == NULL)
  */
  data->len = strlen (value);
  /*  data->data = malloc (data->len); */
  /* strcpy (data->data, value); */
  data->data = value;
  data->is_static = 1;
  return data;
}

data_t *
bin_to_data (void *value, int32_t len)
{
  data_t *data = get_new_data ();
  /*
  static int32_t data_len = 64*1024;
  if (data == NULL) {
    data = get_new_data ();
    data->data = NULL;
  }
  if (data->data == NULL)
    data->data = malloc (64 * 1024);
  if (len > data_len) {
    free (data->data);
    data->data = malloc (len);
    data_len = len;
  }
  */
  /*  data->data = memdup (value, len); */
  data->is_static = 1;
  data->len = len;
  data->data = value;
  return data;
}

int64_t
data_to_int (data_t *data)
{
  if (!data)
    return -1;

  return atoll (data->data);
}

int8_t *
data_to_str (data_t *data)
{
  /*  return strdup (data->data); */
  return data->data;
}

void *
data_to_bin (data_t *data)
{/*
  static void *ret = NULL;
  static data_len = 64*1024;
  if (ret == NULL)
    ret = malloc (64 * 1024);
  if (data->len > data_len) {
    free (ret);
    ret = malloc (data->len);
    data_len = data->len;
  }
  memcpy (ret, data->data, data->len);
  return ret;
 */
  /*  return  memdup (data->data,  data->len);
   */
  if (data)
    return data->data;

  return NULL;
}

void
dict_foreach (dict_t *dict,
	      void (*fn)(dict_t *this, int8_t *key, data_t *value))
{
  data_pair_t *pairs = dict->members_list;

  while (pairs) {
    fn (dict, pairs->key, pairs->value);
    pairs = pairs->next;
  }
}
