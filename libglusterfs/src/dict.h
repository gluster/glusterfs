/*
   Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _DICT_H
#define _DICT_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <inttypes.h>
#include <sys/uio.h>
#include <pthread.h>

#include "common-utils.h"

typedef struct _data data_t;
typedef struct _dict dict_t;
typedef struct _data_pair data_pair_t;

struct _data {
  unsigned char is_static:1;
  unsigned char is_const:1;
  int32_t len;
  struct iovec *vec;
  char *data;
  int32_t refcount;
  gf_lock_t lock;
};

struct _data_pair {
  struct _data_pair *hash_next;
  struct _data_pair *prev;
  struct _data_pair *next;
  data_t *value;
  char *key;
};

struct _dict {
  unsigned char is_static:1;
  int32_t hash_size;
  int32_t count;
  int32_t refcount;
  data_pair_t **members;
  data_pair_t *members_list;
  char *extra_free;
  gf_lock_t lock;
};


int32_t is_data_equal (data_t *one, data_t *two);
void data_destroy (data_t *data);

int32_t dict_set (dict_t *this, char *key, data_t *value);
data_t *dict_get (dict_t *this, char *key);
void dict_del (dict_t *this, char *key);

int32_t dict_serialized_length (dict_t *dict);
int32_t dict_serialize (dict_t *dict, char *buf);
int32_t dict_unserialize (char *buf, int32_t size, dict_t **fill);

int32_t
dict_allocate_and_serialize (dict_t *this, char **buf, size_t *length);

int32_t dict_iovec_len (dict_t *dict);
int32_t dict_to_iovec (dict_t *dict, struct iovec *vec, int32_t count);
			  
void dict_destroy (dict_t *dict);
void dict_unref (dict_t *dict);
dict_t *dict_ref (dict_t *dict);
data_t *data_ref (data_t *data);
void data_unref (data_t *data);

/* 
   TODO: provide converts for differnt byte sizes, signedness, and void *
 */
data_t *int_to_data (int64_t value);
data_t *str_to_data (char *value);
data_t *data_from_dynstr (char *value);
data_t *data_from_dynptr (void *value, int32_t len);
data_t *bin_to_data (void *value, int32_t len);
data_t *static_str_to_data (char *value);
data_t *static_bin_to_data (void *value);

int64_t data_to_int64 (data_t *data);
int32_t data_to_int32 (data_t *data);
int16_t data_to_int16 (data_t *data);
int8_t data_to_int8 (data_t *data);

uint64_t data_to_uint64 (data_t *data);
uint32_t data_to_uint32 (data_t *data);
uint16_t data_to_uint16 (data_t *data);

data_t *data_from_ptr (void *value);
data_t *data_from_static_ptr (void *value);

data_t *data_from_int64 (int64_t value);
data_t *data_from_int32 (int32_t value);
data_t *data_from_int16 (int16_t value);
data_t *data_from_int8 (int8_t value);

data_t *data_from_uint64 (uint64_t value);
data_t *data_from_uint32 (uint32_t value);
data_t *data_from_uint16 (uint16_t value);

char *data_to_str (data_t *data);
void *data_to_bin (data_t *data);
void *data_to_ptr (data_t *data);

data_t *get_new_data ();
dict_t *get_new_dict_full (int size_hint);
dict_t *get_new_dict ();

data_pair_t *get_new_data_pair ();

void dict_foreach (dict_t *this,
		   void (*fn)(dict_t *this,
			      char *key,
			      data_t *value,
			      void *data),
		   void *data);

dict_t *dict_copy (dict_t *this,
		   dict_t *new);

/* CLEANED UP FUNCTIONS DECLARATIONS */
GF_MUST_CHECK dict_t *dict_new (void);
dict_t *dict_copy_with_ref (dict_t *this,
			    dict_t *new);

GF_MUST_CHECK int dict_get_int8 (dict_t *this, char *key, int8_t *val);
GF_MUST_CHECK int dict_set_int8 (dict_t *this, char *key, int8_t val);

GF_MUST_CHECK int dict_get_int16 (dict_t *this, char *key, int16_t *val);
GF_MUST_CHECK int dict_set_int16 (dict_t *this, char *key, int16_t val);

GF_MUST_CHECK int dict_get_int32 (dict_t *this, char *key, int32_t *val);
GF_MUST_CHECK int dict_set_int32 (dict_t *this, char *key, int32_t val);

GF_MUST_CHECK int dict_get_int64 (dict_t *this, char *key, int64_t *val);
GF_MUST_CHECK int dict_set_int64 (dict_t *this, char *key, int64_t val);

GF_MUST_CHECK int dict_get_uint16 (dict_t *this, char *key, uint16_t *val);
GF_MUST_CHECK int dict_set_uint16 (dict_t *this, char *key, uint16_t val);

GF_MUST_CHECK int dict_get_uint32 (dict_t *this, char *key, uint32_t *val);
GF_MUST_CHECK int dict_set_uint32 (dict_t *this, char *key, uint32_t val);

GF_MUST_CHECK int dict_get_uint64 (dict_t *this, char *key, uint64_t *val);
GF_MUST_CHECK int dict_set_uint64 (dict_t *this, char *key, uint64_t val);

GF_MUST_CHECK int dict_get_double (dict_t *this, char *key, double *val);
GF_MUST_CHECK int dict_set_double (dict_t *this, char *key, double val);

GF_MUST_CHECK int dict_set_static_ptr (dict_t *this, char *key, void *ptr);
GF_MUST_CHECK int dict_get_ptr (dict_t *this, char *key, void **ptr);
GF_MUST_CHECK int dict_set_ptr (dict_t *this, char *key, void *ptr);
GF_MUST_CHECK int dict_set_dynptr (dict_t *this, char *key, void *ptr, size_t size);

GF_MUST_CHECK int dict_get_bin (dict_t *this, char *key, void **ptr);
GF_MUST_CHECK int dict_set_bin (dict_t *this, char *key, void *ptr, size_t size);
GF_MUST_CHECK int dict_set_static_bin (dict_t *this, char *key, void *ptr, size_t size);

GF_MUST_CHECK int dict_set_str (dict_t *this, char *key, char *str);
GF_MUST_CHECK int dict_set_dynstr (dict_t *this, char *key, char *str);
GF_MUST_CHECK int dict_get_str (dict_t *this, char *key, char **str);

#endif
