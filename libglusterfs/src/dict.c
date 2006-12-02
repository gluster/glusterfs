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

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#include "common-utils.h"
#include "protocol.h"
#include "glusterfs.h"
#include "transport.h"
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

static data_pair_t *
_dict_lookup (dict_t *this, char *key)
{
  int hashval = SuperFastHash (key, strlen (key)) % this->hash_size;
  data_pair_t *pair;
   
  for (pair = this->members[hashval]; pair != NULL; pair = pair->hash_next) {
    if (!strcmp (pair->key, key))
      return pair;
  }
  
  return NULL;
}

int32_t
dict_set (dict_t *this, 
  	  char *key, 
  	  data_t *value)
{
  int hashval = SuperFastHash (key, strlen (key)) % this->hash_size;
  data_pair_t *pair = _dict_lookup (this, key);
   
  if (pair) {
    data_destroy (pair->value);
    if (strlen (pair->key) < strlen (key))
      pair->key = realloc (pair->key, strlen (key));
    strcpy (pair->key, key);
    pair->value = value;
    return 0;
  }
  
  pair = (data_pair_t *) calloc (1, sizeof (*pair));
  pair->key = (char *) calloc (1, strlen (key) + 1);
  strcpy (pair->key, key);
  pair->value = (value);
 
  pair->hash_next = this->members[hashval];
  this->members[hashval] = pair;
 
  pair->next = this->members_list;
  pair->prev = NULL;
  if (this->members_list)
    this->members_list->prev = pair;
  this->members_list = pair;
  this->count++;
  
  return 0;
}

data_t *
dict_get (dict_t *this,
  	  char *key)
{
  data_pair_t *pair = _dict_lookup (this, key);
  if (pair)
    return pair->value;
  
  return NULL;
}

void
dict_del (dict_t *this,
  	  char *key)
{
  int hashval = SuperFastHash (key, strlen (key)) % this->hash_size;
  data_pair_t *pair = this->members[hashval];
  data_pair_t *prev = NULL;
  
  while (pair) {
    if (strcmp (pair->key, key) == 0) {
      if (prev)
 	prev->hash_next = pair->hash_next;
      else
 	this->members[hashval] = pair->hash_next;
  
      data_destroy (pair->value);
  
      if (pair->prev)
 	pair->prev->next = pair->next;
      else
	this->members_list = pair->next;

      if (pair->next)
 	pair->next->prev = pair->prev;
  
      free (pair->key);
      free (pair);
      this->count--;
      return;
    }
 
    prev = pair;
    pair = pair->hash_next;
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

  free (this->members);

  if (this->extra_free)
    free (this->extra_free);

  if (!this->is_static)
    free (this);

  return;
}

void
dict_unref (dict_t *this)
{
  this->count--;
  if (!this->count)
    dict_destroy (this);
}

dict_t *
dict_ref (dict_t *this)
{
  this->count++;
  return this;
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
  data_pair_t *pair = dict->members_list;

  while (count) {
    len += 18 + strlen (pair->key) + 1 + pair->value->len;
    pair = pair->next;
    count--;
  }

  return len;
}

int32_t 
dict_serialize (dict_t *dict, char *buf)
{
  GF_ERROR_IF_NULL (dict);
  GF_ERROR_IF_NULL (buf);

  data_pair_t *pair = dict->members_list;
  int32_t count = dict->count;

  // FIXME: magic numbers
  uint64_t dcount = dict->count;
  sprintf (buf, "%08"PRIx64"\n", dcount);
  buf += 9;
  while (count) {
    uint64_t keylen = strlen (pair->key) + 1;
    uint64_t vallen = pair->value->len;
    sprintf (buf, "%08"PRIx64":%08"PRIx64"\n", keylen, vallen);
    buf += 18;
    memcpy (buf, pair->key, keylen);
    buf += keylen;
    memcpy (buf, pair->value->data, pair->value->len);
    buf += pair->value->len;
    pair = pair->next;
    count--;
  }
  return (0);
}

dict_t *
dict_unserialize_old (char *buf, int32_t size, dict_t **fill)
{
  int32_t ret = 0;
  int32_t cnt = 0;

  if (!*fill) {
    gf_log ("libglusterfs", GF_LOG_ERROR, "dict.c: dict_unserialize: *fill is NULL");
    goto err;
  }

  uint64_t count;
  ret = sscanf (buf, "%"SCNx64"\n", &count);
  (*fill)->count = 0;

  if (!ret){
    gf_log ("libglusterfs", GF_LOG_ERROR, "dict.c: dict_unserialize: sscanf on buf failed");
    goto err;
  }
  buf += 9;
  
  if (count == 0){
    gf_log ("libglusterfs", GF_LOG_ERROR, "dict.c: dict_unserialize: count == 0");
    goto err;
  }

  //  (*fill)->extra_free = buf;

  for (cnt = 0; cnt < count; cnt++) {
    data_t *value = NULL; // = get_new_data ();
    char *key = NULL;
    uint64_t key_len, value_len;
    
    ret = sscanf (buf, "%"SCNx64":%"SCNx64"\n", &key_len, &value_len);
    if (ret != 2){
      gf_log ("libglusterfs", GF_LOG_ERROR, "dict.c: dict_unserialize: sscanf for key_len and value_len failed");
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

/*     pair = get_new_data_pair (); */
/*     pair->key = key; */
/*     pair->value = value; */

    dict_set (*fill, key, value);
    free (key);

    memcpy (value->data, buf, value_len);
    buf += value_len;

    value->data[value->len] = 0;
  }

  goto ret;

 err:
/*   dict_destroy (fill); */
  *fill = NULL; 

 ret:
  return *fill;
}

dict_t *
dict_unserialize (char *buf, int32_t size, dict_t **fill)
{
  int32_t ret = 0;
  int32_t cnt = 0;

  if (!*fill) {
    gf_log ("libglusterfs", GF_LOG_ERROR, "dict.c: dict_unserialize: *fill is NULL");
    goto err;
  }

  uint64_t count;
  ret = sscanf (buf, "%"SCNx64"\n", &count);
  (*fill)->count = 0;

  if (!ret){
    gf_log ("libglusterfs", GF_LOG_ERROR, "dict.c: dict_unserialize: sscanf on buf failed");
    goto err;
  }
  buf += 9;
  
  if (count == 0){
    gf_log ("libglusterfs", GF_LOG_ERROR, "dict.c: dict_unserialize: count == 0");
    goto err;
  }

  //  (*fill)->extra_free = buf;

  for (cnt = 0; cnt < count; cnt++) {
    data_t *value = NULL; // = get_new_data ();
    char *key = NULL;
    uint64_t key_len, value_len;
    
    ret = sscanf (buf, "%"SCNx64":%"SCNx64"\n", &key_len, &value_len);
    if (ret != 2){
      gf_log ("libglusterfs",
	      GF_LOG_ERROR,
	      "dict.c: dict_unserialize: sscanf for key_len and value_len failed");
      goto err;
    }
    buf += 18;

    /*    key = calloc (1, key_len + 1);
    memcpy (key, buf, key_len);
    buf += key_len;
    key[key_len] = 0;
    */
    key = buf;
    buf += key_len;
    
    value = get_new_data ();
    value->len = value_len;
    /*    value->data = calloc (1, value->len + 1);
    memcpy (value->data, buf, value_len);
    value->data[value->len] = 0; */
    value->data = buf;
    value->is_static = 1;
    buf += value_len;

/*     pair = get_new_data_pair (); */
/*     pair->key = key; */
/*     pair->value = value; */

    dict_set (*fill, key, value);
    //    free (key);
  }

  goto ret;

 err:
/*   dict_destroy (fill); */
  *fill = NULL; 

 ret:
  return *fill;
}


int32_t
dict_iovec_len (dict_t *dict)
{
  int32_t len = 0;

  len = 1 + (3 * dict->count);

  return len;
}

int32_t
dict_to_iovec (dict_t *dict,
	       struct iovec *vec,
	       int32_t count)
{
  int32_t i = 0;
  data_pair_t *pair = dict->members_list;

  vec[0].iov_len = 9;
  if (vec[0].iov_base)
    sprintf (vec[0].iov_base,
	     "%08"PRIx64"\n",
	     (int64_t)dict->count);
  i++;

  while (pair) {
    int64_t keylen = strlen (pair->key) + 1;
    int64_t vallen = pair->value->len;

    vec[i].iov_len = 18;
    if (vec[i].iov_base)
      sprintf (vec[i].iov_base,
	       "%08"PRIx64":%08"PRIx64"\n",
	       keylen,
	       vallen);
    i++;

    vec[i].iov_len = keylen;
    vec[i].iov_base = pair->key;
    i++;

    vec[i].iov_len = pair->value->len;
    vec[i].iov_base = pair->value->data;
    i++;

    pair = pair->next;
  }

  return 0;
}

data_t *
int_to_data (int64_t value)
{
  data_t *data = get_new_data ();
  asprintf (&data->data, "%lld", value);
  data->len = strlen (data->data) + 1;
  return data;
}

data_t *
str_to_data (char *value)
{
  data_t *data = get_new_data ();

  data->len = strlen (value) + 1;

  data->data = value;
  data->is_static = 1;
  return data;
}

data_t *
data_from_dynstr (char *value)
{
  data_t *data = get_new_data ();

  data->len = strlen (value) + 1;

  data->data = value;
  return data;
}

data_t *
bin_to_data (void *value, int32_t len)
{
  data_t *data = get_new_data ();
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

  return strtoll (data->data, NULL, 0);
}

char *
data_to_str (data_t *data)
{
  return data->data;
}

void *
data_to_bin (data_t *data)
{
  if (data)
    return data->data;

  return NULL;
}

void
dict_foreach (dict_t *dict,
	      void (*fn)(dict_t *this,
			 char *key,
			 data_t *value,
			 void *data),
	      void *data)
{
  data_pair_t *pairs = dict->members_list;

  while (pairs) {
    fn (dict, pairs->key, pairs->value, data);
    pairs = pairs->next;
  }
}

dict_t *
dict_copy (dict_t *dict)
{
  dict_t *new = get_new_dict_full (dict->hash_size);
  void _copy (dict_t *unused,
	      char *key,
	      data_t *value,
	      void *data)
    {
      dict_set (new,
		key,
		data_copy (value));
    }
  dict_foreach (dict, _copy, NULL);
  return new;
}
