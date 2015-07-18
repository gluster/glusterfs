/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _DICT_H
#define _DICT_H

#include <inttypes.h>
#include <sys/uio.h>
#include <pthread.h>

#include "common-utils.h"
#include "libglusterfs-messages.h"

typedef struct _data data_t;
typedef struct _dict dict_t;
typedef struct _data_pair data_pair_t;


#define GF_PROTOCOL_DICT_SERIALIZE(this,from_dict,to,len,ope,labl) do { \
                int    ret     = 0;                                     \
                                                                        \
                if (!from_dict)                                         \
                        break;                                          \
                                                                        \
                ret = dict_allocate_and_serialize (from_dict, to, &len);\
                if (ret < 0) {                                          \
                        gf_msg (this->name, GF_LOG_WARNING, 0,          \
                                LG_MSG_DICT_SERIAL_FAILED,            \
                                "failed to get serialized dict (%s)",   \
                                (#from_dict));                          \
                        ope = EINVAL;                                   \
                        goto labl;                                      \
                }                                                       \
        } while (0)


#define GF_PROTOCOL_DICT_UNSERIALIZE(xl,to,buff,len,ret,ope,labl) do {  \
                if (!len)                                               \
                        break;                                          \
                to = dict_new();                                        \
                GF_VALIDATE_OR_GOTO (xl->name, to, labl);               \
                                                                        \
                ret = dict_unserialize (buff, len, &to);                \
                if (ret < 0) {                                          \
                        gf_msg (xl->name, GF_LOG_WARNING, 0,            \
                                LG_MSG_DICT_UNSERIAL_FAILED,            \
                                "failed to unserialize dictionary (%s)", \
                                (#to));                                 \
                                                                        \
                        ope = EINVAL;                                   \
                        goto labl;                                      \
                }                                                       \
                                                                        \
        } while (0)

struct _data {
        unsigned char  is_static:1;
        unsigned char  is_const:1;
        int32_t        len;
        char          *data;
        int32_t        refcount;
        gf_lock_t      lock;
};

struct _data_pair {
        struct _data_pair *hash_next;
        struct _data_pair *prev;
        struct _data_pair *next;
        data_t            *value;
        char              *key;
};

struct _dict {
        unsigned char   is_static:1;
        int32_t         hash_size;
        int32_t         count;
        int32_t         refcount;
        data_pair_t   **members;
        data_pair_t    *members_list;
        char           *extra_free;
        char           *extra_stdfree;
        gf_lock_t       lock;
        data_pair_t    *members_internal;
        data_pair_t     free_pair;
        gf_boolean_t    free_pair_in_use;
};

typedef gf_boolean_t (*dict_match_t) (dict_t *d, char *k, data_t *v,
                                      void *data);

int32_t is_data_equal (data_t *one, data_t *two);
void data_destroy (data_t *data);

/* function to set a key/value pair (overwrite existing if matches the key */
int32_t dict_set (dict_t *this, char *key, data_t *value);
/* function to set a new key/value pair (without checking for duplicate) */
int32_t dict_add (dict_t *this, char *key, data_t *value);

data_t *dict_get (dict_t *this, char *key);
void dict_del (dict_t *this, char *key);
int dict_reset (dict_t *dict);

int32_t dict_serialized_length (dict_t *dict);
int32_t dict_serialize (dict_t *dict, char *buf);
int32_t dict_unserialize (char *buf, int32_t size, dict_t **fill);

int32_t dict_allocate_and_serialize (dict_t *this, char **buf, u_int *length);

void dict_destroy (dict_t *dict);
void dict_unref (dict_t *dict);
dict_t *dict_ref (dict_t *dict);
data_t *data_ref (data_t *data);
void data_unref (data_t *data);

int32_t dict_lookup  (dict_t *this, char *key, data_t **data);
/*
   TODO: provide converts for different byte sizes, signedness, and void *
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
uint8_t data_to_uint8 (data_t *data);

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
data_t * data_copy (data_t *old);
dict_t *get_new_dict_full (int size_hint);
dict_t *get_new_dict ();

int dict_foreach (dict_t *this,
                  int (*fn)(dict_t *this,
                            char *key,
                            data_t *value,
                            void *data),
                  void *data);

int dict_foreach_fnmatch (dict_t *dict, char *pattern,
                          int (*fn)(dict_t *this,
                                    char *key,
                                    data_t *value,
                                    void *data),
                          void *data);

int
dict_foreach_match (dict_t *dict,
             gf_boolean_t (*match)(dict_t *this,
                                char *key,
                                data_t *value,
                                void *mdata),
             void *match_data,
             int (*action)(dict_t *this,
                                char *key,
                                data_t *value,
                                void *adata),
              void *action_data);

int dict_null_foreach_fn (dict_t *d, char *k,
                          data_t *v, void *tmp);
int dict_remove_foreach_fn (dict_t *d, char *k,
                            data_t *v, void *tmp);
dict_t *dict_copy (dict_t *this, dict_t *new);
int dict_keys_join (void *value, int size, dict_t *dict,
                    int (*filter_fn)(char *key));

/* CLEANED UP FUNCTIONS DECLARATIONS */
GF_MUST_CHECK dict_t *dict_new (void);
dict_t *dict_copy_with_ref (dict_t *this, dict_t *new);

GF_MUST_CHECK int dict_reset (dict_t *dict);

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
GF_MUST_CHECK int dict_get_ptr_and_len (dict_t *this, char *key, void **ptr, int *len);
GF_MUST_CHECK int dict_set_ptr (dict_t *this, char *key, void *ptr);
GF_MUST_CHECK int dict_set_dynptr (dict_t *this, char *key, void *ptr, size_t size);

GF_MUST_CHECK int dict_get_bin (dict_t *this, char *key, void **ptr);
GF_MUST_CHECK int dict_set_bin (dict_t *this, char *key, void *ptr, size_t size);
GF_MUST_CHECK int dict_set_static_bin (dict_t *this, char *key, void *ptr, size_t size);

GF_MUST_CHECK int dict_set_str (dict_t *this, char *key, char *str);
GF_MUST_CHECK int dict_set_dynstr (dict_t *this, char *key, char *str);
GF_MUST_CHECK int dict_set_dynstr_with_alloc (dict_t *this, char *key, const char *str);
GF_MUST_CHECK int dict_add_dynstr_with_alloc (dict_t *this, char *key, char *str);
GF_MUST_CHECK int dict_get_str (dict_t *this, char *key, char **str);

GF_MUST_CHECK int dict_get_str_boolean (dict_t *this, char *key, int default_val);
GF_MUST_CHECK int dict_serialize_value_with_delim (dict_t *this, char *buf, int32_t *serz_len,
                                                    char delimiter);
void
dict_dump_to_statedump (dict_t *dict, char *dict_name, char *domain);

void
dict_dump_to_log (dict_t *dict);

int
dict_dump_to_str (dict_t *dict, char *dump, int dumpsize, char *format);
gf_boolean_t
dict_match_everything (dict_t *d, char *k, data_t *v, void *data);

dict_t *
dict_for_key_value (const char *name, const char *value, size_t size);

gf_boolean_t
are_dicts_equal (dict_t *one, dict_t *two,
                 gf_boolean_t (*match) (dict_t *d, char *k, data_t *v,
                                        void *data),
                 gf_boolean_t (*value_ignore) (char *k));
#endif
