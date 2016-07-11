/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <limits.h>
#include <fnmatch.h>

#include "glusterfs.h"
#include "common-utils.h"
#include "dict.h"
#include "hashfn.h"
#include "logging.h"
#include "compat.h"
#include "byte-order.h"
#include "globals.h"
#include "statedump.h"
#include "libglusterfs-messages.h"

struct dict_cmp {
        dict_t *dict;
        gf_boolean_t (*value_ignore) (char *k);
};

data_t *
get_new_data ()
{
        data_t *data = NULL;

        data = mem_get0 (THIS->ctx->dict_data_pool);
        if (!data) {
                return NULL;
        }

        LOCK_INIT (&data->lock);
        return data;
}

dict_t *
get_new_dict_full (int size_hint)
{
        dict_t *dict = mem_get0 (THIS->ctx->dict_pool);

        if (!dict) {
                return NULL;
        }

        dict->hash_size = size_hint;
        if (size_hint == 1) {
                /*
                 * This is the only case we ever see currently.  If we ever
                 * need to support resizing the hash table, the resize function
                 * will have to take into account the possibility that
                 * "members" is not separately allocated (i.e. don't just call
                 * realloc() blindly.
                 */
                dict->members = &dict->members_internal;
        }
        else {
                /*
                 * We actually need to allocate space for size_hint *pointers*
                 * but we actually allocate space for one *structure*.  Since
                 * a data_pair_t consists of five pointers, we're wasting four
                 * pointers' worth for N=1, and will overrun what we allocated
                 * for N>5.  If anybody ever starts using size_hint, we'll need
                 * to fix this.
                 */
                GF_ASSERT (size_hint <=
                           (sizeof(data_pair_t) / sizeof(data_pair_t *)));
                dict->members = mem_get0 (THIS->ctx->dict_pair_pool);
                if (!dict->members) {
                        mem_put (dict);
                        return NULL;
                }
        }

        LOCK_INIT (&dict->lock);

        return dict;
}

dict_t *
get_new_dict (void)
{
        return get_new_dict_full (1);
}

dict_t *
dict_new (void)
{
        dict_t *dict = NULL;

        dict = get_new_dict_full(1);

        if (dict)
                dict_ref (dict);

        return dict;
}

int32_t
is_data_equal (data_t *one,
               data_t *two)
{
        if (!one || !two || !one->data || !two->data) {
		gf_msg_callingfn ("dict", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG,
				  "input arguments are provided "
				  "with value data_t as NULL");
                return -1;
	}

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

static int
key_value_cmp (dict_t *one, char *key1, data_t *value1, void *data)
{
        struct dict_cmp *cmp = data;
        dict_t *two = NULL;
        data_t *value2 = NULL;

        two = cmp->dict;
        value2 = dict_get (two, key1);

        if (value2) {
                if (cmp->value_ignore && cmp->value_ignore (key1))
                        return 0;

                if (is_data_equal (value1, value2) == 1)
                        return 0;
        }

        if (value2 == NULL) {
                gf_msg_debug (THIS->name, 0, "'%s' found only on one dict",
                              key1);
        } else {
                gf_msg_debug (THIS->name, 0, "'%s' is different in two dicts "
                              "(%u, %u)", key1, value1->len, value2->len);
        }

        return -1;
}

/* If both dicts are NULL then equal. If one of the dicts is NULL but the
 * other has only ignorable keys then also they are equal. If both dicts are
 * non-null then check if for each non-ignorable key, values are same or
 * not.  value_ignore function is used to skip comparing values for the keys
 * which must be present in both the dictionaries but the value could be
 * different.
 */
gf_boolean_t
are_dicts_equal (dict_t *one, dict_t *two,
                 gf_boolean_t (*match) (dict_t *d, char *k, data_t *v,
                                        void *data),
                 gf_boolean_t (*value_ignore) (char *k))
{
        int     num_matches1 = 0;
        int     num_matches2 = 0;
        struct  dict_cmp cmp = {0};

        if (one == two)
                return _gf_true;

        if (!match)
                match = dict_match_everything;

        cmp.dict = two;
        cmp.value_ignore = value_ignore;
        if (!two) {
                num_matches1 = dict_foreach_match (one, match, NULL,
                                                   dict_null_foreach_fn, NULL);
                goto done;
        } else {
                num_matches1 = dict_foreach_match (one, match, NULL,
                                                   key_value_cmp, &cmp);
        }

        if (num_matches1 == -1)
                return _gf_false;

        if ((num_matches1 == one->count) && (one->count == two->count))
                return _gf_true;

        num_matches2 = dict_foreach_match (two, match, NULL,
                                           dict_null_foreach_fn, NULL);
done:
        /* If the number of matches is same in 'two' then for all the
         * valid-keys that exist in 'one' the value matched and no extra valid
         * keys exist in 'two' alone. Otherwise there exists at least one extra
         * valid-key in 'two' which doesn't exist in 'one' */
        if (num_matches1 == num_matches2)
                return _gf_true;
        return _gf_false;
}

void
data_destroy (data_t *data)
{
        if (data) {
                LOCK_DESTROY (&data->lock);

                if (!data->is_static)
                        GF_FREE (data->data);

                data->len = 0xbabababa;
                if (!data->is_const)
                        mem_put (data);
        }
}

data_t *
data_copy (data_t *old)
{
        if (!old) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, 0, LG_MSG_NULL_PTR,
                                  "old is NULL");
                return NULL;
        }

        data_t *newdata = mem_get0 (THIS->ctx->dict_data_pool);
        if (!newdata) {
                return NULL;
        }

        if (old) {
                newdata->len = old->len;
                if (old->data) {
                        newdata->data = memdup (old->data, old->len);
                        if (!newdata->data)
                                goto err_out;
                }
        }

        LOCK_INIT (&newdata->lock);
        return newdata;

err_out:
        mem_put (newdata);

        return NULL;
}

static data_pair_t *
dict_lookup_common (dict_t *this, char *key)
{
        int hashval = 0;
        if (!this || !key) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG,
                                  "!this || !key (%s)", key);
                return NULL;
        }

        /* If the divisor is 1, the modulo is always 0,
         * in such case avoid hash calculation.
         */
        if (this->hash_size != 1)
                hashval = SuperFastHash (key, strlen (key)) % this->hash_size;

        data_pair_t *pair;

        for (pair = this->members[hashval]; pair != NULL; pair = pair->hash_next) {
                if (pair->key && !strcmp (pair->key, key))
                        return pair;
        }

        return NULL;
}

int32_t
dict_lookup (dict_t *this, char *key, data_t **data)
{
        if (!this || !key || !data) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "!this || !key || "
                                  "!data");
                return -1;
        }

        data_pair_t *tmp = NULL;
        LOCK (&this->lock);
        {
                tmp = dict_lookup_common (this, key);
        }
        UNLOCK (&this->lock);

        if (!tmp)
                return -1;

        *data = tmp->value;
        return 0;
}

static int32_t
dict_set_lk (dict_t *this, char *key, data_t *value, gf_boolean_t replace)
{
        int hashval = 0;
        data_pair_t *pair;
        char key_free = 0;
        int tmp = 0;
        int ret = 0;

        if (!key) {
                ret = gf_asprintf (&key, "ref:%p", value);
                if (-1 == ret) {
                        return -1;
                }
                key_free = 1;
        }

        /* If the divisor is 1, the modulo is always 0,
         * in such case avoid hash calculation.
         */
        if (this->hash_size != 1) {
                tmp = SuperFastHash (key, strlen (key));
                hashval = (tmp % this->hash_size);
        }

        /* Search for a existing key if 'replace' is asked for */
        if (replace) {
                pair = dict_lookup_common (this, key);

                if (pair) {
                        data_t *unref_data = pair->value;
                        pair->value = data_ref (value);
                        data_unref (unref_data);
                        if (key_free)
                                GF_FREE (key);
                        /* Indicates duplicate key */
                        return 0;
                }
        }

        if (this->free_pair_in_use) {
                pair = mem_get0 (THIS->ctx->dict_pair_pool);
                if (!pair) {
                        if (key_free)
                                GF_FREE (key);
                        return -1;
                }
        }
        else {
                pair = &this->free_pair;
                this->free_pair_in_use = _gf_true;
        }

        if (key_free) {
                /* It's ours.  Use it. */
                pair->key = key;
                key_free = 0;
        }
        else {
                pair->key = (char *) GF_CALLOC (1, strlen (key) + 1,
                                                gf_common_mt_char);
                if (!pair->key) {
                        if (pair == &this->free_pair) {
                                this->free_pair_in_use = _gf_false;
                        }
                        else {
                                mem_put (pair);
                        }
                        return -1;
                }
                strcpy (pair->key, key);
        }
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
                GF_FREE (key);
        return 0;
}

int32_t
dict_set (dict_t *this,
          char *key,
          data_t *value)
{
        int32_t ret;

        if (!this || !value) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "!this || !value for "
                                  "key=%s", key);
                return -1;
        }

        LOCK (&this->lock);

        ret = dict_set_lk (this, key, value, 1);

        UNLOCK (&this->lock);

        return ret;
}


int32_t
dict_add (dict_t *this, char *key, data_t *value)
{
        int32_t ret;

        if (!this || !value) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG,
                                  "!this || !value for key=%s", key);
                return -1;
        }

        LOCK (&this->lock);

        ret = dict_set_lk (this, key, value, 0);

        UNLOCK (&this->lock);

        return ret;
}


data_t *
dict_get (dict_t *this, char *key)
{
        data_pair_t *pair;

        if (!this || !key) {
                gf_msg_callingfn ("dict", GF_LOG_INFO, EINVAL,
                                  LG_MSG_INVALID_ARG,
                                  "!this || key=%s", (key) ? key : "()");
                return NULL;
        }

        LOCK (&this->lock);

        pair = dict_lookup_common (this, key);

        UNLOCK (&this->lock);

        if (pair)
                return pair->value;

        return NULL;
}

int
dict_key_count (dict_t *this)
{
        int ret = -1;

        if (!this) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "dict passed is NULL");
                return ret;
        }

        LOCK (&this->lock);
        {
                ret = this->count;
        }
        UNLOCK (&this->lock);

        return ret;
}

void
dict_del (dict_t *this, char *key)
{
        int hashval = 0;

        if (!this || !key) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "!this || key=%s", key);
                return;
        }

        LOCK (&this->lock);

        /* If the divisor is 1, the modulo is always 0,
         * in such case avoid hash calculation.
         */
        if (this->hash_size != 1)
                hashval = SuperFastHash (key, strlen (key)) % this->hash_size;

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

                        GF_FREE (pair->key);
                        if (pair == &this->free_pair) {
                                this->free_pair_in_use = _gf_false;
                        }
                        else {
                                mem_put (pair);
                        }
                        this->count--;
                        break;
                }

                prev = pair;
                pair = pair->hash_next;
        }

        UNLOCK (&this->lock);

        return;
}

void
dict_destroy (dict_t *this)
{
        if (!this) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "dict is NULL");
                return;
        }

        data_pair_t *pair = this->members_list;
        data_pair_t *prev = this->members_list;

        LOCK_DESTROY (&this->lock);

        while (prev) {
                pair = pair->next;
                data_unref (prev->value);
                GF_FREE (prev->key);
                if (prev != &this->free_pair) {
                        mem_put (prev);
                }
                prev = pair;
        }

        if (this->members != &this->members_internal) {
                mem_put (this->members);
        }

        GF_FREE (this->extra_free);
        free (this->extra_stdfree);

        if (!this->is_static)
                mem_put (this);

        return;
}

void
dict_unref (dict_t *this)
{
        int32_t ref;

        if (!this) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "dict is NULL");
                return;
        }

        LOCK (&this->lock);

        this->refcount--;
        ref = this->refcount;

        UNLOCK (&this->lock);

        if (!ref)
                dict_destroy (this);
}

dict_t *
dict_ref (dict_t *this)
{
        if (!this) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "dict is NULL");
                return NULL;
        }

        LOCK (&this->lock);

        this->refcount++;

        UNLOCK (&this->lock);

        return this;
}

void
data_unref (data_t *this)
{

        int32_t ref;

        if (!this) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "dict is NULL");
                return;
        }

        LOCK (&this->lock);

        this->refcount--;
        ref = this->refcount;

        UNLOCK (&this->lock);

        if (!ref)
                data_destroy (this);
}

data_t *
data_ref (data_t *this)
{
        if (!this) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "dict is NULL");
                return NULL;
        }

        LOCK (&this->lock);

        this->refcount++;

        UNLOCK (&this->lock);

        return this;
}

data_t *
int_to_data (int64_t value)
{
        int     ret = 0;
        data_t *data = get_new_data ();

        if (!data) {
                return NULL;
        }

        ret = gf_asprintf (&data->data, "%"PRId64, value);
        if (-1 == ret) {
                gf_msg_debug ("dict", 0, "asprintf failed");
                return NULL;
        }
        data->len = strlen (data->data) + 1;

        return data;
}

data_t *
data_from_int64 (int64_t value)
{
        int     ret = 0;
        data_t *data = get_new_data ();

        if (!data) {
                return NULL;
        }
        ret = gf_asprintf (&data->data, "%"PRId64, value);
        if (-1 == ret) {
                gf_msg_debug ("dict", 0, "asprintf failed");
                return NULL;
        }
        data->len = strlen (data->data) + 1;

        return data;
}

data_t *
data_from_int32 (int32_t value)
{
        int     ret = 0;
        data_t *data = get_new_data ();

        if (!data) {
                return NULL;
        }
        ret = gf_asprintf (&data->data, "%"PRId32, value);
        if (-1 == ret) {
                gf_msg_debug ("dict", 0, "asprintf failed");
                return NULL;
        }

        data->len = strlen (data->data) + 1;

        return data;
}

data_t *
data_from_int16 (int16_t value)
{
        int     ret = 0;
        data_t *data = get_new_data ();

        if (!data) {
                return NULL;
        }
        ret = gf_asprintf (&data->data, "%"PRId16, value);
        if (-1 == ret) {
                gf_msg_debug ("dict", 0, "asprintf failed");
                return NULL;
        }

        data->len = strlen (data->data) + 1;

        return data;
}

data_t *
data_from_int8 (int8_t value)
{
        int     ret = 0;
        data_t *data = get_new_data ();

        if (!data) {
                return NULL;
        }
        ret = gf_asprintf (&data->data, "%d", value);
        if (-1 == ret) {
                gf_msg_debug ("dict", 0, "asprintf failed");
                return NULL;
        }

        data->len = strlen (data->data) + 1;

        return data;
}

data_t *
data_from_uint64 (uint64_t value)
{
        int     ret = 0;
        data_t *data = get_new_data ();

        if (!data) {
                return NULL;
        }
        ret = gf_asprintf (&data->data, "%"PRIu64, value);
        if (-1 == ret) {
                gf_msg_debug ("dict", 0, "asprintf failed");
                return NULL;
        }

        data->len = strlen (data->data) + 1;

        return data;
}

static data_t *
data_from_double (double value)
{
        data_t *data = NULL;
        int     ret  = 0;

        data = get_new_data ();

        if (!data) {
                return NULL;
        }

        ret = gf_asprintf (&data->data, "%f", value);
        if (ret == -1) {
                return NULL;
        }
        data->len = strlen (data->data) + 1;

        return data;
}


data_t *
data_from_uint32 (uint32_t value)
{
        int     ret = 0;
        data_t *data = get_new_data ();

        if (!data) {
                return NULL;
        }
        ret = gf_asprintf (&data->data, "%"PRIu32, value);
        if (-1 == ret) {
                gf_msg_debug ("dict", 0, "asprintf failed");
                return NULL;
        }

        data->len = strlen (data->data) + 1;

        return data;
}


data_t *
data_from_uint16 (uint16_t value)
{
        int     ret = 0;
        data_t *data = get_new_data ();

        if (!data) {
                return NULL;
        }
        ret = gf_asprintf (&data->data, "%"PRIu16, value);
        if (-1 == ret) {
                return NULL;
        }

        data->len = strlen (data->data) + 1;

        return data;
}

static data_t *
data_from_ptr_common (void *value, gf_boolean_t is_static)
{
        /* it is valid to set 0/NULL as a value, no need to check *value */

        data_t *data = get_new_data ();
        if (!data) {
                return NULL;
        }

        data->data = value;
        data->is_static = is_static;

        return data;
}

data_t *
str_to_data (char *value)
{
        if (!value) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "value is NULL");
                return NULL;
        }
        data_t *data = get_new_data ();

        if (!data) {
                return NULL;
        }
        data->len = strlen (value) + 1;

        data->data = value;
        data->is_static = 1;

        return data;
}

data_t *
data_from_dynstr (char *value)
{
        if (!value) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "value is NULL");
                return NULL;
        }

        data_t *data = get_new_data ();

        if (!data)
                return NULL;
        data->len = strlen (value) + 1;
        data->data = value;

        return data;
}

data_t *
data_from_dynptr (void *value, int32_t len)
{
        data_t *data = get_new_data ();

        if (!data)
                return NULL;

        data->len = len;
        data->data = value;

        return data;
}

data_t *
bin_to_data (void *value, int32_t len)
{
        if (!value) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "value is NULL");
                return NULL;
        }

        data_t *data = get_new_data ();

        if (!data)
                return NULL;

        data->is_static = 1;
        data->len = len;
        data->data = value;

        return data;
}

int64_t
data_to_int64 (data_t *data)
{
        if (!data) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "data is NULL");
                return -1;
        }

        char *str = alloca (data->len + 1);
        if (!str)
                return -1;

        memcpy (str, data->data, data->len);
        str[data->len] = '\0';
        return (int64_t) strtoull (str, NULL, 0);
}

int32_t
data_to_int32 (data_t *data)
{
        if (!data) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "data is NULL");
                return -1;
        }

        char *str = alloca (data->len + 1);
        if (!str)
                return -1;

        memcpy (str, data->data, data->len);
        str[data->len] = '\0';

        return strtoul (str, NULL, 0);
}

int16_t
data_to_int16 (data_t *data)
{
        int16_t value = 0;

        if (!data) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "data is NULL");
                return -1;
        }

        char *str = alloca (data->len + 1);
        if (!str)
                return -1;

        memcpy (str, data->data, data->len);
        str[data->len] = '\0';

        errno = 0;
        value = strtol (str, NULL, 0);

        if ((value > SHRT_MAX) || (value < SHRT_MIN)) {
                errno = ERANGE;
                gf_msg_callingfn ("dict", GF_LOG_WARNING, errno,
                                  LG_MSG_DATA_CONVERSION_ERROR, "Error in data"
                                  " conversion: detected overflow");
                return -1;
        }

        return (int16_t)value;
}


int8_t
data_to_int8 (data_t *data)
{
        int8_t value = 0;

        if (!data) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "data is NULL");
                return -1;
        }

        char *str = alloca (data->len + 1);
        if (!str)
                return -1;

        memcpy (str, data->data, data->len);
        str[data->len] = '\0';

        errno = 0;
        value = strtol (str, NULL, 0);

        if ((value > SCHAR_MAX) || (value < SCHAR_MIN)) {
                errno = ERANGE;
                gf_msg_callingfn ("dict", GF_LOG_WARNING, errno,
                                  LG_MSG_DATA_CONVERSION_ERROR, "Error in data"
                                  " conversion: detected overflow");
                return -1;
        }

        return (int8_t)value;
}


uint64_t
data_to_uint64 (data_t *data)
{
        if (!data)
                return -1;
        char *str = alloca (data->len + 1);
        if (!str)
                return -1;

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
        if (!str)
                return -1;

        memcpy (str, data->data, data->len);
        str[data->len] = '\0';

        return strtol (str, NULL, 0);
}

uint16_t
data_to_uint16 (data_t *data)
{
	uint16_t value = 0;

        if (!data)
                return -1;

        char *str = alloca (data->len + 1);
        if (!str)
                return -1;

        memcpy (str, data->data, data->len);
        str[data->len] = '\0';

	errno = 0;
	value = strtol (str, NULL, 0);

	if ((USHRT_MAX - value) < 0) {
		errno = ERANGE;
		gf_msg_callingfn ("dict", GF_LOG_WARNING, errno,
                                  LG_MSG_DATA_CONVERSION_ERROR,
				  "Error in data conversion: "
				  "overflow detected");
		return -1;
	}

        return (uint16_t)value;
}

uint8_t
data_to_uint8 (data_t *data)
{
	uint32_t value = 0;

        if (!data) {
		gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "data is NULL");
                return -1;
	}

        char *str = alloca (data->len + 1);
        if (!str)
                return -1;

        memcpy (str, data->data, data->len);
        str[data->len] = '\0';

	errno = 0;
	value = strtol (str, NULL, 0);

	if ((UCHAR_MAX - (uint8_t)value) < 0) {
		errno = ERANGE;
		gf_msg_callingfn ("dict", GF_LOG_WARNING, errno,
                                  LG_MSG_DATA_CONVERSION_ERROR, "data "
                                  "conversion overflow detected");
		return -1;
	}

        return (uint8_t) value;
}

char *
data_to_str (data_t *data)
{
        if (!data) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "data is NULL");
                return NULL;
        }
        return data->data;
}

void *
data_to_ptr (data_t *data)
{
        if (!data) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "data is NULL");
                return NULL;
        }
        return data->data;
}

void *
data_to_bin (data_t *data)
{
        if (!data) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "data is NULL");
                return NULL;
        }
        return data->data;
}

int
dict_null_foreach_fn (dict_t *d, char *k,
                      data_t *v, void *tmp)
{
        return 0;
}

int
dict_remove_foreach_fn (dict_t *d, char *k,
                        data_t *v, void *_tmp)
{
        if (!d || !k) {
                gf_msg ("glusterfs", GF_LOG_WARNING, EINVAL,
                        LG_MSG_INVALID_ENTRY, "%s is NULL",
                        d?"key":"dictionary");
                return -1;
        }

        dict_del (d, k);
        return 0;
}

gf_boolean_t
dict_match_everything (dict_t *d, char *k, data_t *v, void *data)
{
        return _gf_true;
}

int
dict_foreach (dict_t *dict,
              int (*fn)(dict_t *this,
                        char *key,
                        data_t *value,
                        void *data),
              void *data)
{
        int     ret = 0;

        ret = dict_foreach_match (dict, dict_match_everything, NULL, fn, data);

        if (ret > 0)
                ret = 0;

        return ret;
}

/* return values:
   -1 = failure,
    0 = no matches found,
   +n = n number of matches
*/
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
              void *action_data)
{
        if (!dict || !match || !action) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "dict|match|action is "
                                  "NULL");
                return -1;
        }

        int          ret   = -1;
        int          count = 0;
        data_pair_t *pairs = NULL;
        data_pair_t *next  = NULL;

        pairs = dict->members_list;
        while (pairs) {
                next = pairs->next;
                if (match (dict, pairs->key, pairs->value, match_data)) {
                        ret = action (dict, pairs->key, pairs->value,
                                      action_data);
                        if (ret < 0)
                                return ret;
                        count++;
                }
                pairs = next;
        }

        return count;
}

static gf_boolean_t
dict_fnmatch (dict_t *d, char *k, data_t *val, void *match_data)
{
        return (fnmatch (match_data, k, 0) == 0);
}
/* return values:
   -1 = failure,
    0 = no matches found,
   +n = n number of matches
*/
int
dict_foreach_fnmatch (dict_t *dict, char *pattern,
                      int (*fn)(dict_t *this,
                                char *key,
                                data_t *value,
                                void *data),
                      void *data)
{
        return dict_foreach_match (dict, dict_fnmatch, pattern, fn, data);
}


/**
 * dict_keys_join - pack the keys of the dictionary in a buffer.
 *
 * @value     : buffer in which the keys will be packed (can be NULL)
 * @size      : size of the buffer which is sent (can be 0, in which case buffer
 *              is not packed but only length is returned)
 * @dict      : dictionary of which all the keys will be packed
 * @filter_fn : keys matched in filter_fn() is counted.
 *
 * @return : @length of string after joining keys.
 *
 */

int
dict_keys_join (void *value, int size, dict_t *dict,
                int (*filter_fn)(char *k))
{
	int          len = 0;
        data_pair_t *pairs = NULL;
        data_pair_t *next  = NULL;

        pairs = dict->members_list;
        while (pairs) {
                next = pairs->next;

                if (filter_fn && filter_fn (pairs->key)){
		    pairs = next;
		    continue;
		}

		if (value && (size > len))
			strncpy (value + len, pairs->key, size - len);

                len += (strlen (pairs->key) + 1);

                pairs = next;
        }

	return len;
}

static int
dict_copy_one (dict_t *unused, char *key, data_t *value, void *newdict)
{
        return dict_set ((dict_t *)newdict, key, (value));
}

dict_t *
dict_copy (dict_t *dict,
           dict_t *new)
{
        if (!dict) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "dict is NULL");
                return NULL;
        }

        if (!new)
                new = get_new_dict_full (dict->hash_size);

        dict_foreach (dict, dict_copy_one, new);

        return new;
}

int
dict_reset (dict_t *dict)
{
        int32_t         ret = -1;
        if (!dict) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "dict is NULL");
                goto out;
        }
        dict_foreach (dict, dict_remove_foreach_fn, NULL);
        ret = 0;
out:
        return ret;
}

dict_t *
dict_copy_with_ref (dict_t *dict,
                    dict_t *new)
{
        dict_t *local_new = NULL;

        GF_VALIDATE_OR_GOTO("dict", dict, fail);

        if (new == NULL) {
                local_new = dict_new ();
                GF_VALIDATE_OR_GOTO("dict", local_new, fail);
                new = local_new;
        }

        dict_foreach (dict, dict_copy_one, new);
fail:
        return new;
}

/*
 * !!!!!!! CLEANED UP CODE !!!!!!!
 */

/**
 * Common cleaned up interface:
 *
 * Return value:  0   success
 *               -val error, val = errno
 */


static int
dict_get_with_ref (dict_t *this, char *key, data_t **data)
{
        data_pair_t * pair = NULL;
        int           ret  = -ENOENT;

        if (!this || !key || !data) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG,
                                  "dict OR key (%s) is NULL", key);
                ret = -EINVAL;
                goto err;
        }

        LOCK (&this->lock);
        {
                pair = dict_lookup_common (this, key);
        }
        UNLOCK (&this->lock);

        if (pair) {
                ret = 0;
                *data = data_ref (pair->value);
        }

err:
        return ret;
}

static int
data_to_ptr_common (data_t *data, void **val)
{
        int ret = 0;

        if (!data) {
                ret = -EINVAL;
                goto err;
        }

        *val = data->data;
err:
        return ret;
}


static int
data_to_int8_ptr (data_t *data, int8_t *val)
{
        int    ret = 0;
        char * str = NULL;

        if (!data || !val) {
                ret = -EINVAL;
                goto err;
        }

        str = alloca (data->len + 1);
        if (!str) {
                ret = -ENOMEM;
                goto err;
        }
        memcpy (str, data->data, data->len);
        str[data->len] = '\0';

        errno = 0;
        *val = strtol (str, NULL, 0);
        if (errno != 0)
                ret = -errno;

err:
        return ret;
}

static int
data_to_int16_ptr (data_t *data, int16_t *val)
{
        int    ret = 0;
        char * str = NULL;

        if (!data || !val) {
                ret = -EINVAL;
                goto err;
        }

        str = alloca (data->len + 1);
        if (!str) {
                ret = -ENOMEM;
                goto err;
        }
        memcpy (str, data->data, data->len);
        str[data->len] = '\0';

        errno = 0;
        *val = strtol (str, NULL, 0);
        if (errno != 0)
                ret = -errno;

err:
        return ret;
}

static int
data_to_int32_ptr (data_t *data, int32_t *val)
{
        int    ret = 0;
        char * str = NULL;

        if (!data || !val) {
                ret = -EINVAL;
                goto err;
        }

        str = alloca (data->len + 1);
        if (!str) {
                ret = -ENOMEM;
                goto err;
        }
        memcpy (str, data->data, data->len);
        str[data->len] = '\0';

        errno = 0;
        *val = strtol (str, NULL, 0);
        if (errno != 0)
                ret = -errno;

err:
        return ret;
}

static int
data_to_int64_ptr (data_t *data, int64_t *val)
{
        int    ret = 0;
        char * str = NULL;

        if (!data || !val) {
                ret = -EINVAL;
                goto err;
        }

        str = alloca (data->len + 1);
        if (!str) {
                ret = -ENOMEM;
                goto err;
        }
        memcpy (str, data->data, data->len);
        str[data->len] = '\0';

        errno = 0;
        *val = strtoll (str, NULL, 0);
        if (errno != 0)
                ret = -errno;

err:
        return ret;
}

static int
data_to_uint16_ptr (data_t *data, uint16_t *val)
{
        int    ret = 0;
        char * str = NULL;

        if (!data || !val) {
                ret = -EINVAL;
                goto err;
        }

        str = alloca (data->len + 1);
        if (!str) {
                ret = -ENOMEM;
                goto err;
        }
        memcpy (str, data->data, data->len);
        str[data->len] = '\0';

        errno = 0;
        *val = strtoul (str, NULL, 0);
        if (errno != 0)
                ret = -errno;

err:
        return ret;
}

static int
data_to_uint32_ptr (data_t *data, uint32_t *val)
{
        int    ret = 0;
        char * str = NULL;

        if (!data || !val) {
                ret = -EINVAL;
                goto err;
        }

        str = alloca (data->len + 1);
        if (!str) {
                ret = -ENOMEM;
                goto err;
        }
        memcpy (str, data->data, data->len);
        str[data->len] = '\0';

        errno = 0;
        *val = strtoul (str, NULL, 0);
        if (errno != 0)
                ret = -errno;

err:
        return ret;
}

static int
data_to_uint64_ptr (data_t *data, uint64_t *val)
{
        int    ret = 0;
        char * str = NULL;

        if (!data || !val) {
                ret = -EINVAL;
                goto err;
        }

        str = alloca (data->len + 1);
        if (!str) {
                ret = -ENOMEM;
                goto err;
        }
        memcpy (str, data->data, data->len);
        str[data->len] = '\0';

        errno = 0;
        *val = strtoull (str, NULL, 0);
        if (errno != 0)
                ret = -errno;

err:
        return ret;
}

static int
data_to_double_ptr (data_t *data, double *val)
{
        int    ret = 0;
        char * str = NULL;

        if (!data || !val) {
                ret = -EINVAL;
                goto err;
        }

        str = alloca (data->len + 1);
        if (!str) {
                ret = -ENOMEM;
                goto err;
        }
        memcpy (str, data->data, data->len);
        str[data->len] = '\0';

        errno = 0;
        *val = strtod (str, NULL);
        if (errno != 0)
                ret = -errno;

err:
        return ret;
}

int
dict_get_int8 (dict_t *this, char *key, int8_t *val)
{
        data_t * data = NULL;
        int      ret  = 0;

        if (!this || !key || !val) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_get_with_ref (this, key, &data);
        if (ret != 0) {
                goto err;
        }

        ret = data_to_int8_ptr (data, val);

err:
        if (data)
                data_unref (data);
        return ret;
}


int
dict_set_int8 (dict_t *this, char *key, int8_t val)
{
        data_t * data = NULL;
        int      ret  = 0;

        data = data_from_int8 (val);
        if (!data) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_set (this, key, data);
        if (ret < 0)
                data_destroy (data);

err:
        return ret;
}

int
dict_get_int16 (dict_t *this, char *key, int16_t *val)
{
        data_t * data = NULL;
        int      ret  = 0;

        if (!this || !key || !val) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_get_with_ref (this, key, &data);
        if (ret != 0) {
                goto err;
        }

        ret = data_to_int16_ptr (data, val);

err:
        if (data)
                data_unref (data);
        return ret;
}


int
dict_set_int16 (dict_t *this, char *key, int16_t val)
{
        data_t * data = NULL;
        int      ret  = 0;

        data = data_from_int16 (val);
        if (!data) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_set (this, key, data);
        if (ret < 0)
                data_destroy (data);

err:
        return ret;
}

int
dict_get_int32 (dict_t *this, char *key, int32_t *val)
{
        data_t * data = NULL;
        int      ret  = 0;

        if (!this || !key || !val) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_get_with_ref (this, key, &data);
        if (ret != 0) {
                goto err;
        }

        ret = data_to_int32_ptr (data, val);

err:
        if (data)
                data_unref (data);
        return ret;
}


int
dict_set_int32 (dict_t *this, char *key, int32_t val)
{
        data_t * data = NULL;
        int      ret  = 0;

        data = data_from_int32 (val);
        if (!data) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_set (this, key, data);
        if (ret < 0)
                data_destroy (data);

err:
        return ret;
}

int
dict_get_int64 (dict_t *this, char *key, int64_t *val)
{
        data_t * data = NULL;
        int      ret  = 0;

        if (!this || !key || !val) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_get_with_ref (this, key, &data);
        if (ret != 0) {
                goto err;
        }

        ret = data_to_int64_ptr (data, val);

err:
        if (data)
                data_unref (data);
        return ret;
}


int
dict_set_int64 (dict_t *this, char *key, int64_t val)
{
        data_t * data = NULL;
        int      ret  = 0;

        data = data_from_int64 (val);
        if (!data) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_set (this, key, data);
        if (ret < 0)
                data_destroy (data);

err:
        return ret;
}

int
dict_get_uint16 (dict_t *this, char *key, uint16_t *val)
{
        data_t * data = NULL;
        int      ret  = 0;

        if (!this || !key || !val) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_get_with_ref (this, key, &data);
        if (ret != 0) {
                goto err;
        }

        ret = data_to_uint16_ptr (data, val);

err:
        if (data)
                data_unref (data);
        return ret;
}


int
dict_set_uint16 (dict_t *this, char *key, uint16_t val)
{
        data_t * data = NULL;
        int      ret  = 0;

        data = data_from_uint16 (val);
        if (!data) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_set (this, key, data);
        if (ret < 0)
                data_destroy (data);

err:
        return ret;
}

int
dict_get_uint32 (dict_t *this, char *key, uint32_t *val)
{
        data_t * data = NULL;
        int      ret  = 0;

        if (!this || !key || !val) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_get_with_ref (this, key, &data);
        if (ret != 0) {
                goto err;
        }

        ret = data_to_uint32_ptr (data, val);

err:
        if (data)
                data_unref (data);
        return ret;
}



int
dict_set_uint32 (dict_t *this, char *key, uint32_t val)
{
        data_t * data = NULL;
        int      ret  = 0;

        data = data_from_uint32 (val);
        if (!data) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_set (this, key, data);
        if (ret < 0)
                data_destroy (data);

err:
        return ret;
}

int
dict_get_uint64 (dict_t *this, char *key, uint64_t *val)
{
        data_t * data = NULL;
        int      ret  = 0;

        if (!this || !key || !val) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_get_with_ref (this, key, &data);
        if (ret != 0) {
                goto err;
        }

        ret = data_to_uint64_ptr (data, val);

err:
        if (data)
                data_unref (data);
        return ret;
}


int
dict_set_uint64 (dict_t *this, char *key, uint64_t val)
{
        data_t * data = NULL;
        int      ret  = 0;

        data = data_from_uint64 (val);
        if (!data) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_set (this, key, data);
        if (ret < 0)
                data_destroy (data);

err:
        return ret;
}

int
dict_get_double (dict_t *this, char *key, double *val)
{
        data_t *data = NULL;
        int     ret  = 0;

        if (!this || !key || !val) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_get_with_ref (this, key, &data);
        if (ret != 0) {
                goto err;
        }

        ret = data_to_double_ptr (data, val);

err:
        if (data)
                data_unref (data);
        return ret;
}

int
dict_set_double (dict_t *this, char *key, double val)
{
        data_t * data = NULL;
        int      ret  = 0;

        data = data_from_double (val);
        if (!data) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_set (this, key, data);
        if (ret < 0)
                data_destroy (data);

err:
        return ret;
}

int
dict_set_static_ptr (dict_t *this, char *key, void *ptr)
{
        data_t * data = NULL;
        int      ret  = 0;

        data = data_from_ptr_common (ptr, _gf_true);
        if (!data) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_set (this, key, data);
        if (ret < 0)
                data_destroy (data);

err:
        return ret;
}

int
dict_set_dynptr (dict_t *this, char *key, void *ptr, size_t len)
{
        data_t * data = NULL;
        int      ret  = 0;

        data = data_from_dynptr (ptr, len);
        if (!data) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_set (this, key, data);
        if (ret < 0)
                data_destroy (data);

err:
        return ret;
}

int
dict_get_ptr (dict_t *this, char *key, void **ptr)
{
        data_t * data = NULL;
        int      ret  = 0;

        if (!this || !key || !ptr) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_get_with_ref (this, key, &data);
        if (ret != 0) {
                goto err;
        }

        ret = data_to_ptr_common (data, ptr);
        if (ret != 0) {
                goto err;
        }

err:
        if (data)
                data_unref (data);

        return ret;
}

int
dict_get_ptr_and_len (dict_t *this, char *key, void **ptr, int *len)
{
        data_t * data = NULL;
        int      ret  = 0;

        if (!this || !key || !ptr) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_get_with_ref (this, key, &data);
        if (ret != 0) {
                goto err;
        }

	*len = data->len;

        ret = data_to_ptr_common (data, ptr);
        if (ret != 0) {
                goto err;
        }

err:
        if (data)
                data_unref (data);

        return ret;
}

int
dict_set_ptr (dict_t *this, char *key, void *ptr)
{
        data_t * data = NULL;
        int      ret  = 0;

        data = data_from_ptr_common (ptr, _gf_false);
        if (!data) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_set (this, key, data);
        if (ret < 0)
                data_destroy (data);

err:
        return ret;
}


int
dict_get_str (dict_t *this, char *key, char **str)
{
        data_t * data = NULL;
        int      ret  = -EINVAL;

        if (!this || !key || !str) {
                goto err;
        }

        ret = dict_get_with_ref (this, key, &data);
        if (ret < 0) {
                goto err;
        }

        if (!data || !data->data) {
                goto err;
        }
        *str = data->data;

err:
        if (data)
                data_unref (data);

        return ret;
}

int
dict_set_str (dict_t *this, char *key, char *str)
{
        data_t * data = NULL;
        int      ret  = 0;

        data = str_to_data (str);
        if (!data) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_set (this, key, data);
        if (ret < 0)
                data_destroy (data);

err:
        return ret;
}

int
dict_set_dynstr_with_alloc (dict_t *this, char *key, const char *str)
{
        char *alloc_str = NULL;
        int   ret       = -1;

        alloc_str = gf_strdup (str);
        if (!alloc_str)
                return -1;

        ret = dict_set_dynstr (this, key, alloc_str);
        if (ret == -EINVAL)
                GF_FREE (alloc_str);

        return ret;
}

int
dict_set_dynstr (dict_t *this, char *key, char *str)
{
        data_t * data = NULL;
        int      ret  = 0;

        data = data_from_dynstr (str);
        if (!data) {
                ret = -EINVAL;
                goto err;
        }

        ret = dict_set (this, key, data);
        if (ret < 0)
                data_destroy (data);

err:
        return ret;
}

int
dict_add_dynstr_with_alloc (dict_t *this, char *key, char *str)
{
        data_t  *data = NULL;
        int      ret  = 0;
        char    *alloc_str = NULL;

        alloc_str = gf_strdup (str);
        if (!alloc_str)
                goto out;

        data = data_from_dynstr (alloc_str);
        if (!data) {
                GF_FREE (alloc_str);
                ret = -EINVAL;
                goto out;
        }

        ret = dict_add (this, key, data);
        if (ret < 0)
                data_destroy (data);

out:
        return ret;
}


int
dict_get_bin (dict_t *this, char *key, void **bin)
{
        data_t * data = NULL;
        int      ret  = -EINVAL;

        if (!this || !key || !bin) {
                goto err;
        }

        ret = dict_get_with_ref (this, key, &data);
        if (ret < 0) {
                goto err;
        }

        if (!data || !data->data) {
                goto err;
        }
        *bin = data->data;

err:
        if (data)
                data_unref (data);

        return ret;
}


static int
dict_set_bin_common (dict_t *this, char *key, void *ptr, size_t size,
                     gf_boolean_t is_static)
{
        data_t * data = NULL;
        int      ret  = 0;

        if (!ptr || (size > ULONG_MAX)) {
                ret = -EINVAL;
                goto err;
        }

        data = bin_to_data (ptr, size);
        if (!data) {
                ret = -EINVAL;
                goto err;
        }

        data->is_static = is_static;

        ret = dict_set (this, key, data);
        if (ret < 0) {
                /* don't free data->data, let callers handle it */
                data->data = NULL;
                data_destroy (data);
        }

err:
        return ret;
}

int
dict_set_bin (dict_t *this, char *key, void *ptr, size_t size)
{
        return dict_set_bin_common (this, key, ptr, size, _gf_false);
}


int
dict_set_static_bin (dict_t *this, char *key, void *ptr, size_t size)
{
        return dict_set_bin_common (this, key, ptr, size, _gf_true);
}


/**
 * dict_get_str_boolean - get a boolean value based on string representation.
 *
 * @this        : dictionary
 * @key         : dictionary key queried
 * @default_val : default value if key not found
 *
 * @return      : @default_val if key not found
 *              : boolean interpretation of @this[@key] if it makes sense
 *                (ie., "on", "true", "enable" ...)
 *              : -1 if error occurs or @this[@key] doesn't make sens as
 *                  boolean
 *
 *   So if you query a boolean option, then via @default_val you can choose
 *   between following patterns:
 *
 *   - fall back to _gf_false if @key is not set  [@default_val = 0]
 *   - fall back to _gf_true if @key is not set   [@default_val = 1]
 *   - regard as failure if @key is not set       [@default_val = -1]
 *   - handle specially (not as error) if @key is not set
 *                                                [@default_val = anything else]
 */

int
dict_get_str_boolean (dict_t *this, char *key, int default_val)
{
        data_t       *data = NULL;
        gf_boolean_t  boo = _gf_false;
        int           ret  = 0;

        ret = dict_get_with_ref (this, key, &data);
        if (ret < 0) {
                if (ret == -ENOENT)
                        ret = default_val;
                else
                        ret = -1;
                goto err;
        }

        GF_ASSERT (data);

        if (!data->data) {
                ret = -1;
                goto err;
        }

        ret = gf_string2boolean (data->data, &boo);
        if (ret == -1)
                goto err;

        ret = boo;

err:
        if (data)
                data_unref (data);

        return ret;
}


/**
 * Serialization format:
 *  -------- --------  --------  ----------- -------------
 * |  count | key len | val len | key     \0| value
 *  ---------------------------------------- -------------
 *     4        4         4       <key len>   <value len>
 */

#define DICT_HDR_LEN               4
#define DICT_DATA_HDR_KEY_LEN      4
#define DICT_DATA_HDR_VAL_LEN      4

/**
 * dict_serialized_length_lk - return the length of serialized dict. This
 *                             procedure has to be called with this->lock held.
 *
 * @this  : dict to be serialized
 * @return: success: len
 *        : failure: -errno
 */

int
dict_serialized_length_lk (dict_t *this)
{
        int ret            = -EINVAL;
        int count          = 0;
        int len            = 0;
        data_pair_t * pair = NULL;

        len = DICT_HDR_LEN;
        count = this->count;

        if (count < 0) {
                gf_msg ("dict", GF_LOG_ERROR, EINVAL,
                        LG_MSG_COUNT_LESS_THAN_ZERO, "count (%d) < 0!", count);
                goto out;
        }

        pair = this->members_list;

        while (count) {
                if (!pair) {
                        gf_msg ("dict", GF_LOG_ERROR, EINVAL,
                                LG_MSG_COUNT_LESS_THAN_DATA_PAIRS,
                                "less than count data pairs found!");
                        goto out;
                }

                len += DICT_DATA_HDR_KEY_LEN + DICT_DATA_HDR_VAL_LEN;

                if (!pair->key) {
                        gf_msg ("dict", GF_LOG_ERROR, EINVAL,
                                LG_MSG_NULL_PTR, "pair->key is null!");
                        goto out;
                }

                len += strlen (pair->key) + 1  /* for '\0' */;

                if (!pair->value) {
                        gf_msg ("dict", GF_LOG_ERROR, EINVAL,
                                LG_MSG_NULL_PTR, "pair->value is null!");
                        goto out;
                }

                if (pair->value->len < 0) {
                        gf_msg ("dict", GF_LOG_ERROR, EINVAL,
                                LG_MSG_VALUE_LENGTH_LESS_THAN_ZERO,
                                "value->len (%d) < 0", pair->value->len);
                        goto out;
                }

                len += pair->value->len;

                pair = pair->next;
                count--;
        }

        ret = len;
out:
        return ret;
}

/**
 * dict_serialize_lk - serialize a dictionary into a buffer. This procedure has
 *                     to be called with this->lock held.
 *
 * @this: dict to serialize
 * @buf:  buffer to serialize into. This must be
 *        atleast dict_serialized_length (this) large
 *
 * @return: success: 0
 *          failure: -errno
 */

int
dict_serialize_lk (dict_t *this, char *buf)
{
        int           ret     = -1;
        data_pair_t * pair    = NULL;
        int32_t       count   = 0;
        int32_t       keylen  = 0;
        int32_t       vallen  = 0;
        int32_t       netword = 0;


        if (!buf) {
                gf_msg ("dict", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                        "buf is null!");
                goto out;
        }


        count = this->count;
        if (count < 0) {
                gf_msg ("dict", GF_LOG_ERROR, 0, LG_MSG_COUNT_LESS_THAN_ZERO,
                        "count (%d) < 0!", count);
                goto out;
        }

        netword = hton32 (count);
        memcpy (buf, &netword, sizeof(netword));
        buf += DICT_HDR_LEN;
        pair = this->members_list;

        while (count) {
                if (!pair) {
                        gf_msg ("dict", GF_LOG_ERROR, 0,
                                LG_MSG_PAIRS_LESS_THAN_COUNT,
                                "less than count data pairs found!");
                        goto out;
                }

                if (!pair->key) {
                        gf_msg ("dict", GF_LOG_ERROR, 0, LG_MSG_NULL_PTR,
                                "pair->key is null!");
                        goto out;
                }

                keylen  = strlen (pair->key);
                netword = hton32 (keylen);
                memcpy (buf, &netword, sizeof(netword));
                buf += DICT_DATA_HDR_KEY_LEN;

                if (!pair->value) {
                        gf_msg ("dict", GF_LOG_ERROR, 0,
                                LG_MSG_NULL_PTR,
                                "pair->value is null!");
                        goto out;
                }

                vallen  = pair->value->len;
                netword = hton32 (vallen);
                memcpy (buf, &netword, sizeof(netword));
                buf += DICT_DATA_HDR_VAL_LEN;

                memcpy (buf, pair->key, keylen);
                buf += keylen;
                *buf++ = '\0';

                if (pair->value->data) {
                        memcpy (buf, pair->value->data, vallen);
                        buf += vallen;
                }

                pair = pair->next;
                count--;
        }

        ret = 0;
out:
        return ret;
}


/**
 * dict_serialized_length - return the length of serialized dict
 *
 * @this:   dict to be serialized
 * @return: success: len
 *        : failure: -errno
 */

int
dict_serialized_length (dict_t *this)
{
        int ret            = -EINVAL;

        if (!this) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "dict is null!");
                goto out;
        }

        LOCK (&this->lock);
        {
                ret = dict_serialized_length_lk (this);
        }
        UNLOCK (&this->lock);

out:
        return ret;
}

/**
 * dict_serialize - serialize a dictionary into a buffer
 *
 * @this: dict to serialize
 * @buf:  buffer to serialize into. This must be
 *        atleast dict_serialized_length (this) large
 *
 * @return: success: 0
 *          failure: -errno
 */

int
dict_serialize (dict_t *this, char *buf)
{
        int           ret    = -1;

        if (!this || !buf) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "dict is null!");
                goto out;
        }

        LOCK (&this->lock);
        {
                ret = dict_serialize_lk (this, buf);
        }
        UNLOCK (&this->lock);
out:
        return ret;
}


/**
 * dict_unserialize - unserialize a buffer into a dict
 *
 * @buf:  buf containing serialized dict
 * @size: size of the @buf
 * @fill: dict to fill in
 *
 * @return: success: 0
 *          failure: -errno
 */

int32_t
dict_unserialize (char *orig_buf, int32_t size, dict_t **fill)
{
        char   *buf = NULL;
        int     ret   = -1;
        int32_t count = 0;
        int     i     = 0;

        data_t * value   = NULL;
        char   * key     = NULL;
        int32_t  keylen  = 0;
        int32_t  vallen  = 0;
        int32_t  hostord = 0;

        buf = orig_buf;

        if (!buf) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "buf is null!");
                goto out;
        }

        if (size == 0) {
                gf_msg_callingfn ("dict", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "size is 0!");
                goto out;
        }

        if (!fill) {
                gf_msg_callingfn ("dict", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "fill is null!");
                goto out;
        }

        if (!*fill) {
                gf_msg_callingfn ("dict", GF_LOG_ERROR, EINVAL,
                                  LG_MSG_INVALID_ARG, "*fill is null!");
                goto out;
        }

        if ((buf + DICT_HDR_LEN) > (orig_buf + size)) {
                gf_msg_callingfn ("dict", GF_LOG_ERROR, 0,
                                  LG_MSG_UNDERSIZED_BUF, "undersized buffer "
                                  "passed. available (%lu) < required (%lu)",
                                  (long)(orig_buf + size),
                                  (long)(buf + DICT_HDR_LEN));
                goto out;
        }

        memcpy (&hostord, buf, sizeof(hostord));
        count = ntoh32 (hostord);
        buf += DICT_HDR_LEN;

        if (count < 0) {
                gf_msg ("dict", GF_LOG_ERROR, 0, LG_MSG_COUNT_LESS_THAN_ZERO,
                        "count (%d) <= 0", count);
                goto out;
        }

        /* count will be set by the dict_set's below */
        (*fill)->count = 0;

        for (i = 0; i < count; i++) {
                if ((buf + DICT_DATA_HDR_KEY_LEN) > (orig_buf + size)) {
                        gf_msg_callingfn ("dict", GF_LOG_ERROR, 0,
                                          LG_MSG_UNDERSIZED_BUF, "undersized "
                                          "buffer passed. available (%lu) < "
                                          "required (%lu)",
                                          (long)(orig_buf + size),
                                          (long)(buf + DICT_DATA_HDR_KEY_LEN));
                        goto out;
                }
                memcpy (&hostord, buf, sizeof(hostord));
                keylen = ntoh32 (hostord);
                buf += DICT_DATA_HDR_KEY_LEN;

                if ((buf + DICT_DATA_HDR_VAL_LEN) > (orig_buf + size)) {
                        gf_msg_callingfn ("dict", GF_LOG_ERROR, 0,
                                          LG_MSG_UNDERSIZED_BUF, "undersized "
                                          "buffer passed. available (%lu) < "
                                          "required (%lu)",
                                          (long)(orig_buf + size),
                                          (long)(buf + DICT_DATA_HDR_VAL_LEN));
                        goto out;
                }
                memcpy (&hostord, buf, sizeof(hostord));
                vallen = ntoh32 (hostord);
                buf += DICT_DATA_HDR_VAL_LEN;

                if ((buf + keylen) > (orig_buf + size)) {
                        gf_msg_callingfn ("dict", GF_LOG_ERROR, 0,
                                          LG_MSG_UNDERSIZED_BUF,
                                          "undersized buffer passed. "
                                          "available (%lu) < required (%lu)",
                                          (long)(orig_buf + size),
                                          (long)(buf + keylen));
                        goto out;
                }
                key = buf;
                buf += keylen + 1;  /* for '\0' */

                if ((buf + vallen) > (orig_buf + size)) {
                        gf_msg_callingfn ("dict", GF_LOG_ERROR, 0,
                                          LG_MSG_UNDERSIZED_BUF,
                                          "undersized buffer passed. "
                                          "available (%lu) < required (%lu)",
                                          (long)(orig_buf + size),
                                          (long)(buf + vallen));
                        goto out;
                }
                value = get_new_data ();

                if (!value) {
                        ret = -1;
                        goto out;
                }
                value->len  = vallen;
                value->data = memdup (buf, vallen);
                value->is_static = 0;
                buf += vallen;

                dict_add (*fill, key, value);
        }

        ret = 0;
out:
        return ret;
}


/**
 * dict_allocate_and_serialize - serialize a dictionary into an allocated buffer
 *
 * @this: dict to serialize
 * @buf:  pointer to pointer to character. The allocated buffer is stored in
 *        this pointer. The buffer has to be freed by the caller.
 *
 * @return: success: 0
 *          failure: -errno
 */

int32_t
dict_allocate_and_serialize (dict_t *this, char **buf, u_int *length)
{
        int           ret    = -EINVAL;
        ssize_t       len = 0;

        if (!this || !buf) {
                gf_msg_debug ("dict", 0, "dict OR buf is NULL");
                goto out;
        }

        LOCK (&this->lock);
        {
                len = dict_serialized_length_lk (this);
                if (len < 0) {
                        ret = len;
                        goto unlock;
                }

                *buf = GF_CALLOC (1, len, gf_common_mt_char);
                if (*buf == NULL) {
                        ret = -ENOMEM;
                        goto unlock;
                }

                ret = dict_serialize_lk (this, *buf);
                if (ret < 0) {
                        GF_FREE (*buf);
                        *buf = NULL;
                        goto unlock;
                }

                if (length != NULL) {
                        *length = len;
                }
        }
unlock:
        UNLOCK (&this->lock);
out:
        return ret;
}

/**
 * dict_serialize_value_with_delim_lk: serialize the values in the dictionary
 * into a buffer separated by delimiter (except the last)
 *
 * @this      : dictionary to serialize
 * @buf       : the buffer to store the serialized data
 * @serz_len  : the length of the serialized data (excluding the last delimiter)
 * @delimiter : the delimiter to separate the values
 *
 * @return    : 0 -> success
 *            : -errno -> faliure
 */
int
dict_serialize_value_with_delim_lk (dict_t *this, char *buf, int32_t *serz_len,
                                    char delimiter)
{
        int          ret       = -1;
        int32_t      count     = 0;
        int32_t      vallen    = 0;
        int32_t      total_len = 0;
        data_pair_t *pair      = NULL;

        if (!buf) {
                gf_msg ("dict", GF_LOG_ERROR, EINVAL,
                        LG_MSG_INVALID_ARG, "buf is null");
                goto out;
        }

        count = this->count;
        if (count < 0) {
                gf_msg ("dict", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                        "count (%d) < 0", count);
                goto out;
        }

        pair = this->members_list;

        while (count) {
                if (!pair) {
                        gf_msg ("dict", GF_LOG_ERROR, 0,
                                LG_MSG_PAIRS_LESS_THAN_COUNT,
                                "less than count data pairs found");
                        goto out;
                }

                if (!pair->key || !pair->value) {
                        gf_msg ("dict", GF_LOG_ERROR, 0,
                                LG_MSG_KEY_OR_VALUE_NULL,
                                "key or value is null");
                        goto out;
                }

                if (!pair->value->data) {
                        gf_msg ("dict", GF_LOG_ERROR, 0,
                                LG_MSG_NULL_VALUE_IN_DICT,
                                "null value found in dict");
                        goto out;
                }

                vallen = pair->value->len - 1; // length includes \0
                memcpy (buf, pair->value->data, vallen);
                buf += vallen;
                *buf++ = delimiter;

                total_len += (vallen + 1);

                pair = pair->next;
                count--;
        }

        *--buf = '\0'; // remove the last delimiter
        total_len--;   // adjust the length
        ret = 0;

        if (serz_len)
                *serz_len = total_len;

 out:
        return ret;
}

int
dict_serialize_value_with_delim (dict_t *this, char *buf, int32_t *serz_len,
                                 char delimiter)
{
        int           ret    = -1;

        if (!this || !buf) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "dict is null!");
                goto out;
        }

        LOCK (&this->lock);
        {
                ret = dict_serialize_value_with_delim_lk (this, buf, serz_len,
                                                          delimiter);
        }
        UNLOCK (&this->lock);
out:
        return ret;
}

int
dict_dump_to_str (dict_t *dict, char *dump, int dumpsize, char *format)
{
        int          ret                       = 0;
        int          dumplen                   = 0;
        data_pair_t *trav                      = NULL;

        for (trav = dict->members_list; trav; trav = trav->next) {
                ret = snprintf (&dump[dumplen], dumpsize - dumplen,
                                format, trav->key, trav->value->data);
                if ((ret == -1) || !ret)
                        return ret;

                dumplen += ret;
        }
        return 0;
}

void
dict_dump_to_log (dict_t *dict)
{
        int          ret                       = -1;
        char         dump[64*1024]             = {0,};
        char        *format                    = "(%s:%s)";

        if (!dict) {
                gf_msg_callingfn ("dict", GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "dict is NULL");
                return;
        }

        ret = dict_dump_to_str (dict, dump, sizeof(dump), format);
        if (ret) {
                gf_msg ("dict", GF_LOG_WARNING, 0, LG_MSG_FAILED_TO_LOG_DICT,
                        "Failed to log dictionary");
                return;
        }
        gf_msg_callingfn ("dict", GF_LOG_INFO, 0, LG_MSG_DICT_ERROR,
                          "dict=%p (%s)", dict, dump);

        return;
}

void
dict_dump_to_statedump (dict_t *dict, char *dict_name, char *domain)
{
        int          ret                       = -1;
        char         dump[64*1024]             = {0,};
        char         key[4096]                 = {0,};
        char        *format                    = "\n\t%s:%s";

        if (!dict) {
                gf_msg_callingfn (domain, GF_LOG_WARNING, EINVAL,
                                  LG_MSG_INVALID_ARG, "dict is NULL");
                return;
        }

        ret = dict_dump_to_str (dict, dump, sizeof(dump), format);
        if (ret) {
                gf_msg (domain, GF_LOG_WARNING, 0, LG_MSG_FAILED_TO_LOG_DICT,
                        "Failed to log dictionary %s", dict_name);
                return;
        }
        gf_proc_dump_build_key (key, domain, dict_name);
        gf_proc_dump_write (key, "%s", dump);

        return;
}

dict_t *
dict_for_key_value (const char *name, const char *value, size_t size)
{
	dict_t *xattr = NULL;
	int     ret = 0;

	xattr = dict_new ();
	if (!xattr)
		return NULL;

	ret = dict_set_static_bin (xattr, (char *)name, (void *)value, size);
	if (ret) {
		dict_destroy (xattr);
		xattr = NULL;
	}

	return xattr;
}
