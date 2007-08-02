/*
   Copyright (c) 2006, 2007 Z RESEARCH, Inc. <http://www.zresearch.com>
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
#include "logging.h"

data_pair_t *
get_new_data_pair ()
{
  return (data_pair_t *) calloc (1, sizeof (data_pair_t));
}

data_t *
get_new_data ()
{
  data_t *data = (data_t *) calloc (1, sizeof (data_t));
  LOCK_INIT (&data->lock);
  return data;
}

dict_t *
get_new_dict_full (int size_hint)
{
  dict_t *dict = calloc (1, sizeof (dict_t));

  dict->hash_size = size_hint;
  dict->members = calloc (size_hint, sizeof (data_pair_t *));
  LOCK_INIT (&dict->lock);

  return dict;
}

dict_t *
get_new_dict (void)
{
  return get_new_dict_full (1);
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
    LOCK_DESTROY (&data->lock);

    if (!data->is_static) {
      if (data->data)
	freee (data->data);
      if (data->vec)
	freee (data->vec);
    }

    memset (data, 0xe, sizeof (*data));
    if (!data->is_const)
      freee (data);
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
    if (old->vec)
      newdata->vec = memdup (old->vec, old->len * (sizeof (void *) +
						   sizeof (size_t)));
  }

  return newdata;
}

static data_pair_t *
_dict_lookup (dict_t *this, char *key)
{
  int hashval = SuperFastHash (key, strlen (key)) % this->hash_size;
  data_pair_t *pair;
   
  for (pair = this->members[hashval]; pair != NULL; pair = pair->hash_next) {
    if (pair->key && !strcmp (pair->key, key))
      return pair;
  }
  
  return NULL;
}

int32_t
dict_set (dict_t *this, 
  	  char *key, 
  	  data_t *value)
{
  int hashval;
  static int static_hashval;
  data_pair_t *pair;
  char key_free = 0;

  if (!key) {
    asprintf (&key, "ref:%p", value);
    key_free = 1;
  }

  hashval = static_hashval = SuperFastHash (key, strlen (key)) % this->hash_size;
  pair = _dict_lookup (this, key);

  if (pair) {
    data_t *unref_data = pair->value;
    if (strlen (pair->key) < strlen (key))
      pair->key = realloc (pair->key, strlen (key));
    strcpy (pair->key, key);
    pair->value = data_ref (value);
    data_unref (unref_data);
    if (key_free)
      freee (key);
    return 0;
  }
  pair = (data_pair_t *) calloc (1, sizeof (*pair));
  pair->key = (char *) calloc (1, strlen (key) + 1);
  strcpy (pair->key, key);
  pair->value = data_ref (value);
 
  pair->hash_next = this->members[hashval];
  this->members[hashval] = pair;
 
  pair->next = this->members_list;
  pair->prev = NULL;
  if (this->members_list)
    this->members_list->prev = pair;
  this->members_list = pair;
  this->count++;
  
  if (key_free)
    freee (key);
  return 0;
}

data_t *
dict_get (dict_t *this,
  	  char *key)
{
  if (this == NULL)
    return NULL;
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
  
      data_unref (pair->value);
  
      if (pair->prev)
 	pair->prev->next = pair->next;
      else
	this->members_list = pair->next;

      if (pair->next)
 	pair->next->prev = pair->prev;
  
      freee (pair->key);
      freee (pair);
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

  LOCK_DESTROY (&this->lock);

  while (prev) {
    pair = pair->next;
    data_unref (prev->value);
    freee (prev->key);
    freee (prev);
    prev = pair;
  }

  freee (this->members);

  if (this->extra_free)
    freee (this->extra_free);

  if (!this->is_static)
    freee (this);

  return;
}

void
dict_unref (dict_t *this)
{
  int32_t ref;

  if (this->is_locked)
    LOCK (&this->lock);

  this->refcount--;
  ref = this->refcount;

  if (this->is_locked)
    UNLOCK (&this->lock);

  if (!ref)
    dict_destroy (this);
}

dict_t *
dict_ref (dict_t *this)
{
  if (this->is_locked)
    LOCK (&this->lock);

  this->refcount++;

  if (this->is_locked)
    UNLOCK (&this->lock);

  return this;
}

void
data_unref (data_t *this)
{
  int32_t ref;

  if (this->is_locked)
    LOCK (&this->lock);

  this->refcount--;
  ref = this->refcount;

  if (this->is_locked)
    UNLOCK (&this->lock);

  if (!ref)
    data_destroy (this);
}

data_t *
data_ref (data_t *this)
{
  if (this->is_locked)
    LOCK (&this->lock);

  this->refcount++;

  if (this->is_locked)
    UNLOCK (&this->lock);

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
    len += 18;
    len += strlen (pair->key) + 1;
    if (pair->value->vec) {
      int i;
      for (i=0; i<pair->value->len; i++) {
	len += pair->value->vec[i].iov_len;
      }
    } else {
      len += pair->value->len;
    }
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
  uint64_t dcount = dict->count;

  // FIXME: magic numbers

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
  uint64_t count;

  if (!*fill) {
    gf_log ("libglusterfs/dict",
	    GF_LOG_ERROR,
	    "*fill is NULL");
    goto err;
  }

  ret = sscanf (buf, "%"SCNx64"\n", &count);
  (*fill)->count = 0;

  if (!ret){
    gf_log ("libglusterfs/dict",
	    GF_LOG_ERROR,
	    "sscanf on buf failed");
    goto err;
  }
  buf += 9;
  
  if (count == 0){
    gf_log ("libglusterfs/dict",
	    GF_LOG_ERROR,
	    "count == 0");
    goto err;
  }

  for (cnt = 0; cnt < count; cnt++) {
    data_t *value = NULL;
    char *key = NULL;
    uint64_t key_len, value_len;
    
    ret = sscanf (buf, "%"SCNx64":%"SCNx64"\n", &key_len, &value_len);
    if (ret != 2){
      gf_log ("libglusterfs/dict",
	      GF_LOG_ERROR,
	      "sscanf for key_len and value_len failed");
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

    dict_set (*fill, key, value);
    freee (key);

    memcpy (value->data, buf, value_len);
    buf += value_len;

    value->data[value->len] = 0;
  }

  goto ret;

 err:
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
    gf_log ("libglusterfs/dict",
	    GF_LOG_ERROR,
	    "*fill is NULL");
    return NULL;
  }

  uint64_t count;
  ret = sscanf (buf, "%"SCNx64"\n", &count);
  (*fill)->count = 0;

  if (!ret){
    gf_log ("libglusterfs/dict",
	    GF_LOG_ERROR,
	    "sscanf on buf failed");
    goto err;
  }
  buf += 9;
  
  if (count == 0) {
    gf_log ("libglusterfs/dict",
	    GF_LOG_ERROR,
	    "count == 0");
    goto err;
  }

  for (cnt = 0; cnt < count; cnt++) {
    data_t *value = NULL;
    char *key = NULL;
    uint64_t key_len, value_len;
    
    ret = sscanf (buf, "%"SCNx64":%"SCNx64"\n", &key_len, &value_len);
    if (ret != 2) {
      gf_log ("libglusterfs/dict",
	      GF_LOG_ERROR,
	      "sscanf for key_len and value_len failed");
      goto err;
    }
    buf += 18;

    key = buf;
    buf += key_len;
    
    value = get_new_data ();
    value->len = value_len;
    value->data = buf;
    value->is_static = 1;
    buf += value_len;

    dict_set (*fill, key, value);
  }

  goto ret;

 err:
  freee (*fill);
  *fill = NULL; 

 ret:
  return *fill;
}


int32_t
dict_iovec_len (dict_t *dict)
{
  int32_t len = 0;
  data_pair_t *pair = dict->members_list;

  len++; /* initial header */
  while (pair) {
    len++; /* pair header */
    len++; /* key */

    if (pair->value->vec)
      len += pair->value->len;
    else
      len++;
    pair = pair->next;
  }

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
    int64_t vallen = 0;

    if (pair->value->vec) {
      int i;

      for (i=0; i<pair->value->len; i++) {
	vallen += pair->value->vec[i].iov_len;
      }
    } else {
      vallen = pair->value->len;
    }

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

    if (pair->value->vec) {
      int k;

      for (k=0; k<pair->value->len; k++) {
	vec[i].iov_len = pair->value->vec[k].iov_len;
	vec[i].iov_base = pair->value->vec[k].iov_base;
	i++;
      }
    } else {
      vec[i].iov_len = pair->value->len;
      vec[i].iov_base = pair->value->data;
      i++;
    }

    pair = pair->next;
  }

  return 0;
}

data_t *
int_to_data (int64_t value)
{
  data_t *data = get_new_data ();

  asprintf (&data->data, "%"PRId64, value);
  data->len = strlen (data->data) + 1;

  return data;
}

data_t *
data_from_int64 (int64_t value)
{
  data_t *data = get_new_data ();

  asprintf (&data->data, "%"PRId64, value);
  data->len = strlen (data->data) + 1;

  return data;
}

data_t *
data_from_int32 (int32_t value)
{
  data_t *data = get_new_data ();

  asprintf (&data->data, "%"PRId32, value);
  data->len = strlen (data->data) + 1;

  return data;
}

data_t *
data_from_int16 (int16_t value)
{

  data_t *data = get_new_data ();

  asprintf (&data->data, "%"PRId16, value);
  data->len = strlen (data->data) + 1;

  return data;
}

data_t *
data_from_int8 (int8_t value)
{

  data_t *data = get_new_data ();

  asprintf (&data->data, "%d", value);
  data->len = strlen (data->data) + 1;

  return data;
}

data_t *
data_from_uint64 (uint64_t value)
{
  data_t *data = get_new_data ();

  asprintf (&data->data, "%"PRIu64, value);
  data->len = strlen (data->data) + 1;

  return data;
}


data_t *
data_from_uint32 (uint32_t value)
{
  data_t *data = get_new_data ();

  asprintf (&data->data, "%"PRIu32, value);
  data->len = strlen (data->data) + 1;

  return data;
}


data_t *
data_from_uint16 (uint16_t value)
{
  data_t *data = get_new_data ();

  asprintf (&data->data, "%"PRIu16, value);
  data->len = strlen (data->data) + 1;

  return data;
}


data_t *
data_from_ptr (void *value)
{
  data_t *data = get_new_data ();
  data->data = value;
  return data;
}


data_t *
data_from_static_ptr (void *value)
{
  data_t *data = get_new_data ();

  data->is_static = 1;
  data->data = value;

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
data_from_dynptr (void *value, int32_t len)
{
  data_t *data = get_new_data ();

  data->len = len;
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
data_to_int64 (data_t *data)
{
  if (!data)
    return -1;
  char *str = alloca (data->len + 1);
  memcpy (str, data->data, data->len);
  str[data->len] = '\0';
  return strtoll (str, NULL, 0);
}

int32_t
data_to_int32 (data_t *data)
{
  if (!data)
    return -1;
  char *str = alloca (data->len + 1);
  memcpy (str, data->data, data->len);
  str[data->len] = '\0';

  return strtol (str, NULL, 0);
}

int16_t
data_to_int16 (data_t *data)
{
  if (!data)
    return -1;
  char *str = alloca (data->len + 1);
  memcpy (str, data->data, data->len);
  str[data->len] = '\0';

  return strtol (str, NULL, 0);
}


int8_t
data_to_int8 (data_t *data)
{
  if (!data)
    return -1;
  char *str = alloca (data->len + 1);
  memcpy (str, data->data, data->len);
  str[data->len] = '\0';

  return (int8_t)strtol (str, NULL, 0);
}


uint64_t
data_to_uint64 (data_t *data)
{
  if (!data)
    return -1;
  char *str = alloca (data->len + 1);
  memcpy (str, data->data, data->len);
  str[data->len] = '\0';

  return strtoll (str, NULL, 0);
}

uint32_t
data_to_uint32 (data_t *data)
{
  if (!data)
    return -1;
  char *str = alloca (data->len + 1);
  memcpy (str, data->data, data->len);
  str[data->len] = '\0';

  return strtol (str, NULL, 0);
}

uint16_t
data_to_uint16 (data_t *data)
{
  if (!data)
    return -1;
  char *str = alloca (data->len + 1);
  memcpy (str, data->data, data->len);
  str[data->len] = '\0';

  return strtol (str, NULL, 0);
}

char *
data_to_str (data_t *data)
{
  return data->data;
}

void *
data_to_ptr (data_t *data)
{
  if (data == NULL)
    return NULL;
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
dict_copy (dict_t *dict,
	   dict_t *new)
{
  if (!new)
    new = get_new_dict_full (dict->hash_size);

  auto void _copy (dict_t *unused,
		   char *key,
		   data_t *value,
		   void *newdict)
    {
      dict_set ((dict_t *)newdict,
		key,
		(value));
    }
  dict_foreach (dict, _copy, new);

  return new;
}

data_t *
data_from_iovec (struct iovec *vec,
		 int32_t len)
{
  data_t *new = get_new_data ();

  new->vec = memdup (vec,
		     len * (sizeof (void *) + sizeof (size_t)));
  new->len = len;

  return new;
}
