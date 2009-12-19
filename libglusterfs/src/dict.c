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

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "common-utils.h"
#include "dict.h"
#include "hashfn.h"
#include "logging.h"
#include "compat.h"
#include "byte-order.h"

data_pair_t *
get_new_data_pair ()
{
	data_pair_t *data_pair_ptr = NULL;
  
	data_pair_ptr = (data_pair_t *) CALLOC (1, sizeof (data_pair_t));
	ERR_ABORT (data_pair_ptr);
  
	return data_pair_ptr;
}

data_t *
get_new_data ()
{
	data_t *data = NULL;

	data = (data_t *) CALLOC (1, sizeof (data_t));
	if (!data) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"calloc () returned NULL");
		return NULL;
	}

	LOCK_INIT (&data->lock);
	return data;
}

dict_t *
get_new_dict_full (int size_hint)
{
	dict_t *dict = CALLOC (1, sizeof (dict_t));

	if (!dict) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"calloc () returned NULL");
		return NULL;
	}

	dict->hash_size = size_hint;
	dict->members = CALLOC (size_hint, sizeof (data_pair_t *));

	if (!dict->members) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"calloc () returned NULL");
		return NULL;
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
	if (!one || !two || !one->data || !two->data)
		return 1;

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
				FREE (data->data);
			if (data->vec)
				FREE (data->vec);
		}

		data->len = 0xbabababa;
		if (!data->is_const)
			FREE (data);
	}
}

data_t *
data_copy (data_t *old)
{
	if (!old) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@old is NULL");
		return NULL;
	}

	data_t *newdata = (data_t *) CALLOC (1, sizeof (*newdata));

	if (!newdata) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@newdata - NULL returned by CALLOC");
		return NULL;
	}

	if (old) {
		newdata->len = old->len;
		if (old->data)
			newdata->data = memdup (old->data, old->len);
		if (old->vec)
			newdata->vec = memdup (old->vec, old->len * (sizeof (void *) +
								     sizeof (size_t)));
		if (!old->data && !old->vec) {
			gf_log ("dict", GF_LOG_CRITICAL,
				"@newdata->data || @newdata->vec got NULL from CALLOC()");
			return NULL;
		}
	}

	return newdata;
}

static data_pair_t *
_dict_lookup (dict_t *this, char *key)
{
	if (!this || !key) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@this=%p @key=%p", this, key);
		return NULL;
	}

	int hashval = SuperFastHash (key, strlen (key)) % this->hash_size;
	data_pair_t *pair;
   
	for (pair = this->members[hashval]; pair != NULL; pair = pair->hash_next) {
		if (pair->key && !strcmp (pair->key, key))
			return pair;
	}
  
	return NULL;
}


static int32_t
_dict_set (dict_t *this, 
	   char *key, 
	   data_t *value)
{
	int hashval;
	data_pair_t *pair;
	char key_free = 0;
	int tmp = 0;
        int ret = 0;

	if (!key) {
		ret = asprintf (&key, "ref:%p", value);
                if (-1 == ret) {
                        gf_log ("dict", GF_LOG_ERROR, "asprintf failed");
                        return -1;
                }
		key_free = 1;
	}

	tmp = SuperFastHash (key, strlen (key));
	hashval = (tmp % this->hash_size);
	pair = _dict_lookup (this, key);

	if (pair) {
		data_t *unref_data = pair->value;
		pair->value = data_ref (value);
		data_unref (unref_data);
		if (key_free)
			FREE (key);
		/* Indicates duplicate key */
		return 0;
	}
	pair = (data_pair_t *) CALLOC (1, sizeof (*pair));
	if (!pair) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@pair - NULL returned by CALLOC");
		return -1;
	}

	pair->key = (char *) CALLOC (1, strlen (key) + 1);
	if (!pair->key) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@pair->key - NULL returned by CALLOC");
		return -1;
	}

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
		FREE (key);
	return 0;
}

int32_t
dict_set (dict_t *this,
	  char *key,
	  data_t *value)
{
	int32_t ret;

	if (!this || !value) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@this=%p @value=%p", this, value);
		return -1;
	}

	LOCK (&this->lock);

	ret = _dict_set (this, key, value);

	UNLOCK (&this->lock);

	return ret;
}


data_t *
dict_get (dict_t *this,
  	  char *key)
{
	data_pair_t *pair;

	if (!this || !key) {
		gf_log ("dict", GF_LOG_DEBUG,
			"@this=%p @key=%p", this, key);
		return NULL;
	}

	LOCK (&this->lock);

	pair = _dict_lookup (this, key);

	UNLOCK (&this->lock);

	if (pair)
		return pair->value;
  
	return NULL;
}

void
dict_del (dict_t *this,
  	  char *key)
{
	if (!this || !key) {
		gf_log ("dict", GF_LOG_DEBUG,
			"@this=%p @key=%p", this, key);
		return;
	}

	LOCK (&this->lock);

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
  
			FREE (pair->key);
			FREE (pair);
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
		gf_log ("dict", GF_LOG_DEBUG,
			"@this=%p", this);
		return;
	}

	data_pair_t *pair = this->members_list;
	data_pair_t *prev = this->members_list;

	LOCK_DESTROY (&this->lock);

	while (prev) {
		pair = pair->next;
		data_unref (prev->value);
		FREE (prev->key);
		FREE (prev);
		prev = pair;
	}

	FREE (this->members);

	if (this->extra_free)
		FREE (this->extra_free);

	if (!this->is_static)
		FREE (this);

	return;
}

void
dict_unref (dict_t *this)
{
	int32_t ref;

	if (!this) {
		gf_log ("dict", GF_LOG_DEBUG,
			"@this=%p", this);
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
		gf_log ("dict", GF_LOG_DEBUG,
			"@this=%p", this);
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
		gf_log ("dict", GF_LOG_DEBUG,
			"@this=%p", this);
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
		gf_log ("dict", GF_LOG_DEBUG,
			"@this=%p", this);
		return NULL;
	}

	LOCK (&this->lock);

	this->refcount++;

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
dict_serialized_length_old (dict_t *this)
{

	if (!this) {
		gf_log ("dict", GF_LOG_DEBUG,
			"@this=%p", this);
		return -1;
	}

	int32_t len = 9; /* count + \n */
	int32_t count = this->count;
	data_pair_t *pair = this->members_list;

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
dict_serialize_old (dict_t *this, char *buf)
{
	if (!this || !buf) {
		gf_log ("dict", GF_LOG_DEBUG,
			"@this=%p @buf=%p", this, buf);
		return -1;
	}

	data_pair_t *pair = this->members_list;
	int32_t count = this->count;
	uint64_t dcount = this->count;

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

	if (!buf || fill == NULL || !*fill) {
		gf_log ("dict", GF_LOG_ERROR,
			"@buf=%p @fill=%p @*fill=%p", buf, fill, *fill);
		return NULL;
	}

	uint64_t count;
	ret = sscanf (buf, "%"SCNx64"\n", &count);
	(*fill)->count = 0;

	if (!ret){
		gf_log ("dict",
			GF_LOG_ERROR,
			"sscanf on buf failed");
		goto err;
	}
	buf += 9;
  
	if (count == 0) {
		gf_log ("dict",
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
			gf_log ("dict",
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
	FREE (*fill);
	*fill = NULL; 

ret:
	return *fill;
}


int32_t
dict_iovec_len (dict_t *this)
{
	if (!this) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@this=%p", this);
		return -1;
	}

	int32_t len = 0;
	data_pair_t *pair = this->members_list;

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
dict_to_iovec (dict_t *this,
	       struct iovec *vec,
	       int32_t count)
{
	if (!this || !vec) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@this=%p @vec=%p", this, vec);
		return -1;
	}

	int32_t i = 0;
	data_pair_t *pair = this->members_list;

	vec[0].iov_len = 9;
	if (vec[0].iov_base)
		sprintf (vec[0].iov_base,
			 "%08"PRIx64"\n",
			 (int64_t)this->count);
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
        int     ret = 0;
	data_t *data = get_new_data ();

	if (!data) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@data - NULL returned by CALLOC");
		return NULL;
	}

	ret = asprintf (&data->data, "%"PRId64, value);
        if (-1 == ret) {
                gf_log ("dict", GF_LOG_ERROR, "asprintf failed");
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
		gf_log ("dict", GF_LOG_CRITICAL,
			"@data - NULL returned by CALLOC");
		return NULL;
	}
	ret = asprintf (&data->data, "%"PRId64, value);
        if (-1 == ret) {
                gf_log ("dict", GF_LOG_ERROR, "asprintf failed");
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
		gf_log ("dict", GF_LOG_CRITICAL,
			"@data - NULL returned by CALLOC");
		return NULL;
	}
	ret = asprintf (&data->data, "%"PRId32, value);
        if (-1 == ret) {
                gf_log ("dict", GF_LOG_ERROR, "asprintf failed");
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
		gf_log ("dict", GF_LOG_CRITICAL,
			"@data - NULL returned by CALLOC");
		return NULL;
	}
	ret = asprintf (&data->data, "%"PRId16, value);
        if (-1 == ret) {
                gf_log ("dict", GF_LOG_ERROR, "asprintf failed");
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
		gf_log ("dict", GF_LOG_CRITICAL,
			"@data - NULL returned by CALLOC");
		return NULL;
	}
	ret = asprintf (&data->data, "%d", value);
        if (-1 == ret) {
                gf_log ("dict", GF_LOG_ERROR, "asprintf failed");
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
		gf_log ("dict", GF_LOG_CRITICAL,
			"@data - NULL returned by CALLOC");
		return NULL;
	}
	ret = asprintf (&data->data, "%"PRIu64, value);
        if (-1 == ret) {
                gf_log ("dict", GF_LOG_ERROR, "asprintf failed");
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
		gf_log ("dict", GF_LOG_CRITICAL,
			"@data - NULL returned by CALLOC");
		return NULL;
	}

	ret = asprintf (&data->data, "%f", value);
	if (ret == -1) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@data - allocation failed by ASPRINTF");
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
		gf_log ("dict", GF_LOG_CRITICAL,
			"@data - NULL returned by CALLOC");
		return NULL;
	}
	ret = asprintf (&data->data, "%"PRIu32, value);
        if (-1 == ret) {
                gf_log ("dict", GF_LOG_ERROR, "asprintf failed");
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
		gf_log ("dict", GF_LOG_CRITICAL,
			"@data - NULL returned by CALLOC");
		return NULL;
	}
	ret = asprintf (&data->data, "%"PRIu16, value);
        if (-1 == ret) {
                gf_log ("dict", GF_LOG_ERROR, "asprintf failed");
                return NULL;
        }

	data->len = strlen (data->data) + 1;

	return data;
}


data_t *
data_from_ptr (void *value)
{
	if (!value) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@value=%p", value);
		return NULL;
	}

	data_t *data = get_new_data ();

	if (!data) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@data - NULL returned by CALLOC");
		return NULL;
	}

	data->data = value;
	return data;
}

data_t *
data_from_static_ptr (void *value)
{
/*
  this is valid to set 0 as value..

	if (!value) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@value=%p", value);
		return NULL;
	}
*/
	data_t *data = get_new_data ();

	if (!data) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@data - NULL returned by CALLOC");
		return NULL;
	}

	data->is_static = 1;
	data->data = value;

	return data;
}

data_t *
str_to_data (char *value)
{
	if (!value) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@value=%p", value);
		return NULL;
	}
	data_t *data = get_new_data ();

	if (!data) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@data - NULL returned by CALLOC");
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
		gf_log ("dict", GF_LOG_CRITICAL,
			"@value=%p", value);
		return NULL;
	}

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
	if (!value) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@value=%p", value);
		return NULL;
	}

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
	ERR_ABORT (str);
	memcpy (str, data->data, data->len);
	str[data->len] = '\0';
	return (int64_t) strtoull (str, NULL, 0);
}

int32_t
data_to_int32 (data_t *data)
{
	if (!data)
		return -1;

	char *str = alloca (data->len + 1);
	ERR_ABORT (str);
	memcpy (str, data->data, data->len);
	str[data->len] = '\0';

	return strtoul (str, NULL, 0);
}

int16_t
data_to_int16 (data_t *data)
{
	if (!data)
		return -1;

	char *str = alloca (data->len + 1);
	ERR_ABORT (str);
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
	ERR_ABORT (str);
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
	ERR_ABORT (str);
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
	ERR_ABORT (str);
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
	ERR_ABORT (str);
	memcpy (str, data->data, data->len);
	str[data->len] = '\0';

	return strtol (str, NULL, 0);
}

char *
data_to_str (data_t *data)
{
	if (!data) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@data=%p", data);
		return NULL;
	}
	return data->data;
}

void *
data_to_ptr (data_t *data)
{
	if (!data) {
		return NULL;
	}
	return data->data;
}

void *
data_to_bin (data_t *data)
{
	if (!data) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@data=%p", data);
		return NULL;
	}
	return data->data;
}

void
dict_foreach (dict_t *dict,
	      void (*fn)(dict_t *this,
			 char *key,
			 data_t *value,
			 void *data),
	      void *data)
{
	if (!data) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@data=%p", data);
		return;
	}

	data_pair_t *pairs = dict->members_list;

	while (pairs) {
		fn (dict, pairs->key, pairs->value, data);
		pairs = pairs->next;
	}
}


static void
_copy (dict_t *unused,
       char *key,
       data_t *value,
       void *newdict)
{
	dict_set ((dict_t *)newdict, key, (value));
}


dict_t *
dict_copy (dict_t *dict,
	   dict_t *new)
{
	if (!dict) {
		gf_log ("dict", GF_LOG_CRITICAL,
			"@data=%p", dict);
		return NULL;
	}

	if (!new)
		new = get_new_dict_full (dict->hash_size);

	dict_foreach (dict, _copy, new);

	return new;
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

	dict_foreach (dict, _copy, new);
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
		ret = -EINVAL;
		goto err;
	}

	LOCK (&this->lock);
	{
		pair = _dict_lookup (this, key);
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
_data_to_ptr (data_t *data, void **val)
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
_data_to_int8 (data_t *data, int8_t *val)
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
_data_to_int16 (data_t *data, int16_t *val)
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
_data_to_int32 (data_t *data, int32_t *val)
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
_data_to_int64 (data_t *data, int64_t *val)
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
_data_to_uint16 (data_t *data, uint16_t *val)
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
_data_to_uint32 (data_t *data, uint32_t *val)
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
_data_to_uint64 (data_t *data, uint64_t *val)
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
_data_to_double (data_t *data, double *val)
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

	ret = _data_to_int8 (data, val);
    
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

	ret = _data_to_int16 (data, val);
    
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

	ret = _data_to_int32 (data, val);
    
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

	ret = _data_to_int64 (data, val);
    
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

	ret = _data_to_uint16 (data, val);
    
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

	ret = _data_to_uint32 (data, val);
    
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

	ret = _data_to_uint64 (data, val);
    
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

	ret = _data_to_double (data, val);
    
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

err:
	return ret;
}

int
dict_set_static_ptr (dict_t *this, char *key, void *ptr)
{
	data_t * data = NULL;
	int      ret  = 0;

	data = data_from_static_ptr (ptr);
	if (!data) {
		ret = -EINVAL;
		goto err;
	}

	ret = dict_set (this, key, data);

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

	ret = _data_to_ptr (data, ptr);
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

	data = data_from_ptr (ptr);
	if (!data) {
		ret = -EINVAL;
		goto err;
	}

	ret = dict_set (this, key, data);

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

err:
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

err:
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


int
dict_set_bin (dict_t *this, char *key, void *ptr, size_t size)
{
	data_t * data = NULL;
	int      ret  = 0;

	if (!ptr || (size < 0)) {
		ret = -EINVAL;
		goto err;
	}

	data = bin_to_data (ptr, size);
	if (!data) {
		ret = -EINVAL;
		goto err;
	}

	data->data = ptr;
	data->len  = size;
	data->is_static = 0;

	ret = dict_set (this, key, data);

err:
	return ret;
}


int
dict_set_static_bin (dict_t *this, char *key, void *ptr, size_t size)
{
	data_t * data = NULL;
	int      ret  = 0;

	if (!ptr || (size < 0)) {
		ret = -EINVAL;
		goto err;
	}

	data = bin_to_data (ptr, size);
	if (!data) {
		ret = -EINVAL;
		goto err;
	}

	data->data = ptr;
	data->len  = size;
	data->is_static = 1;

	ret = dict_set (this, key, data);

err:
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
 * _dict_serialized_length - return the length of serialized dict. This
 *                           procedure has to be called with this->lock held.
 *
 * @this  : dict to be serialized
 * @return: success: len
 *        : failure: -errno
 */

int
_dict_serialized_length (dict_t *this)
{
	int ret            = -EINVAL;
	int count          = 0;
	int len            = 0;
	int i              = 0;
	data_pair_t * pair = NULL;

	len = DICT_HDR_LEN;
	count = this->count;

	if (count < 0) {
		gf_log ("dict", GF_LOG_ERROR, "count (%d) < 0!", count);
		goto out;
	}

	pair = this->members_list;

	while (count) {
		if (!pair) {
			gf_log ("dict", GF_LOG_ERROR, 
				"less than count data pairs found!");
			goto out;
		}

		len += DICT_DATA_HDR_KEY_LEN + DICT_DATA_HDR_VAL_LEN;

		if (!pair->key) {
			gf_log ("dict", GF_LOG_ERROR, "pair->key is null!");
			goto out;
		}

		len += strlen (pair->key) + 1  /* for '\0' */;

		if (!pair->value) {
			gf_log ("dict", GF_LOG_ERROR,
				"pair->value is null!");
			goto out;
		}

		if (pair->value->vec) {
			for (i = 0; i < pair->value->len; i++) {
				if (pair->value->vec[i].iov_len < 0) {
					gf_log ("dict", GF_LOG_ERROR,
						"iov_len (%"GF_PRI_SIZET") < 0!",
						pair->value->vec[i].iov_len);
					goto out;
				}

				len += pair->value->vec[i].iov_len;
			}
		} else {
			if (pair->value->len < 0) {
				gf_log ("dict", GF_LOG_ERROR,
					"value->len (%d) < 0",
					pair->value->len);
				goto out;
			}

			len += pair->value->len;
		}

		pair = pair->next;
		count--;
	}
	
	ret = len;
out:
	return ret;
}

/**
 * _dict_serialize - serialize a dictionary into a buffer. This procedure has 
 *                   to be called with this->lock held.
 *
 * @this: dict to serialize
 * @buf:  buffer to serialize into. This must be 
 *        atleast dict_serialized_length (this) large
 *
 * @return: success: 0
 *          failure: -errno
 */

int
_dict_serialize (dict_t *this, char *buf)
{
	int           ret     = -1;
	data_pair_t * pair    = NULL;
	int32_t       count   = 0;
	int32_t       keylen  = 0;
	int32_t       vallen  = 0;
	int32_t       netword = 0;
	
	if (!buf) {
		gf_log ("dict", GF_LOG_ERROR,
			"buf is null!");
		goto out;
	}
	
	count = this->count;
	if (count < 0) {
		gf_log ("dict", GF_LOG_ERROR, "count (%d) < 0!", count);
		goto out;
	}

	netword = hton32 (count);
	memcpy (buf, &netword, sizeof(netword));
	buf += DICT_HDR_LEN;
	pair = this->members_list;

	while (count) {
		if (!pair) {
			gf_log ("dict", GF_LOG_ERROR,
				"less than count data pairs found!");
			goto out;
		}

		if (!pair->key) {
			gf_log ("dict", GF_LOG_ERROR,
				"pair->key is null!");
			goto out;
		}

		keylen  = strlen (pair->key);
		netword = hton32 (keylen);
		memcpy (buf, &netword, sizeof(netword));
		buf += DICT_DATA_HDR_KEY_LEN;

		if (!pair->value) {
			gf_log ("dict", GF_LOG_ERROR,
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

		if (!pair->value->data) {
			gf_log ("dict", GF_LOG_ERROR,
				"pair->value->data is null!");
			goto out;
		}
		memcpy (buf, pair->value->data, vallen);
		buf += vallen;

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
		gf_log ("dict", GF_LOG_ERROR, "this is null!");
		goto out;
	}
	
        LOCK (&this->lock);
        {
                ret = _dict_serialized_length (this);
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
	
	if (!this) {
		gf_log ("dict", GF_LOG_ERROR,
			"this is null!");
		goto out;
	}
	if (!buf) {
		gf_log ("dict", GF_LOG_ERROR,
			"buf is null!");
		goto out;
	}

        LOCK (&this->lock);
        {
                ret = _dict_serialize (this, buf);
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
		gf_log ("dict", GF_LOG_ERROR,
			"buf is null!");
		goto out;
	}

	if (size == 0) {
		gf_log ("dict", GF_LOG_ERROR,
			"size is 0!");
		goto out;
	}

	if (!fill) {
		gf_log ("dict", GF_LOG_ERROR,
			"fill is null!");
		goto out;
	}

	if (!*fill) {
		gf_log ("dict", GF_LOG_ERROR,
			"*fill is null!");
		goto out;
	}

	if ((buf + DICT_HDR_LEN) > (orig_buf + size)) {
		gf_log ("dict", GF_LOG_ERROR,
			"undersized buffer passsed");
		goto out;
	}

	memcpy (&hostord, buf, sizeof(hostord));
	count = ntoh32 (hostord);
	buf += DICT_HDR_LEN;

	if (count < 0) {
		gf_log ("dict", GF_LOG_ERROR,
			"count (%d) <= 0", count);
		goto out;
	}

	/* count will be set by the dict_set's below */
	(*fill)->count = 0;

	for (i = 0; i < count; i++) {
		if ((buf + DICT_DATA_HDR_KEY_LEN) > (orig_buf + size)) {
			gf_log ("dict", GF_LOG_DEBUG,
				"No room for keylen (size %d).",
				DICT_DATA_HDR_KEY_LEN);
			gf_log ("dict", GF_LOG_ERROR,
				"undersized buffer passsed");
			goto out;
		}
		memcpy (&hostord, buf, sizeof(hostord));
		keylen = ntoh32 (hostord);
		buf += DICT_DATA_HDR_KEY_LEN;

		if ((buf + DICT_DATA_HDR_VAL_LEN) > (orig_buf + size)) {
			gf_log ("dict", GF_LOG_DEBUG,
				"No room for vallen (size %d).",
				DICT_DATA_HDR_VAL_LEN);
			gf_log ("dict", GF_LOG_ERROR,
				"undersized buffer passsed");
			goto out;
		}
		memcpy (&hostord, buf, sizeof(hostord));
		vallen = ntoh32 (hostord);
		buf += DICT_DATA_HDR_VAL_LEN;

		if ((buf + keylen) > (orig_buf + size)) {
			gf_log ("dict", GF_LOG_DEBUG,
				"No room for key (size %d).", keylen);
			gf_log ("dict", GF_LOG_ERROR,
				"undersized buffer passsed");
			goto out;
		}
		key = buf;
		buf += keylen + 1;  /* for '\0' */

		if ((buf + vallen) > (orig_buf + size)) {
			gf_log ("dict", GF_LOG_DEBUG,
				"No room for value (size %d).", vallen);
			gf_log ("dict", GF_LOG_ERROR,
				"undersized buffer passsed");
			goto out;
		}
		value = get_new_data ();
		value->len  = vallen;
		value->data = buf;
		value->is_static = 1;
		buf += vallen;

		dict_set (*fill, key, value);
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
dict_allocate_and_serialize (dict_t *this, char **buf, size_t *length)
{
	int           ret    = -EINVAL;
        ssize_t       len = 0; 

	if (!this) {
		gf_log ("dict", GF_LOG_DEBUG,
			"NULL passed as this pointer");
		goto out;
	}
	if (!buf) {
		gf_log ("dict", GF_LOG_DEBUG,
			"NULL passed as buf");
		goto out;
	}
	
        LOCK (&this->lock);
        { 
                len = _dict_serialized_length (this);
                if (len < 0) {
                        ret = len;
                        goto unlock;
                }

                *buf = CALLOC (1, len);
                if (*buf == NULL) {
                        ret = -ENOMEM;
                        gf_log ("dict", GF_LOG_ERROR, "out of memory");
                        goto unlock;
                }

                ret = _dict_serialize (this, *buf);
                if (ret < 0) {
                        FREE (*buf);
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
