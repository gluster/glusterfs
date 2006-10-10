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

#ifndef _DICT_H
#define _DICT_H

#include "protocol.h"

struct _data {
  int32_t len;
  char *data;
  int8_t is_static;
  int8_t is_const;
};
typedef struct _data data_t;

struct _data_pair {
  struct _data_pair *next;
  data_t *value;
  int8_t *key;
};
typedef struct _data_pair data_pair_t;

struct _dict {
  int8_t is_static;
  int32_t count;
  data_pair_t *members;
};
typedef struct _dict dict_t;

int32_t is_data_equal (data_t *one, data_t *two);
void data_destroy (data_t *data);

int32_t dict_set (dict_t *this, int8_t *key, data_t *value);
data_t *dict_get (dict_t *this, int8_t *key);
void dict_del (dict_t *this, int8_t *key);

int32_t dict_dump (int32_t fd, dict_t *dict, gf_block *blk, int32_t type);

int32_t dict_serialized_length (dict_t *dict);
int32_t dict_serialize (dict_t *dict, int8_t *buf);
dict_t *dict_unserialize (int8_t *buf, int32_t size, dict_t **fill);
			  
dict_t *dict_load (FILE *fp);
dict_t *dict_fill (FILE *fp, dict_t *dict);
void dict_destroy (dict_t *dict);

data_t *int_to_data (int64_t value);
data_t *str_to_data (int8_t *value);
data_t *bin_to_data (void *value, int32_t len);
data_t *static_str_to_data (int8_t *value);
data_t *static_bin_to_data (void *value);

int64_t data_to_int (data_t *data);
int8_t *data_to_str (data_t *data);
void *data_to_bin (data_t *data);

data_t *get_new_data ();
dict_t *get_new_dict ();
data_pair_t *get_new_data_pair ();

void dict_foreach (dict_t *this,
		   void (*fn)(dict_t *this,
			      int8_t *key,
			      data_t *value));

#define STATIC_DICT {1, 0, NULL};
#define STATIC_DATA_STR(str) {strlen (str) + 1, str, 1, 1};

#endif
