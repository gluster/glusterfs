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

#include "glusterfs/dict.h"
#define XXH_INLINE_ALL
#include "xxhash.h"
#include "glusterfs/compat.h"
#include "glusterfs/compat-errno.h"
#include "glusterfs/byte-order.h"
#include "glusterfs/statedump.h"
#include "glusterfs/libglusterfs-messages.h"

struct dict_cmp {
    dict_t *dict;
    gf_boolean_t (*value_ignore)(char *k);
};

#define VALIDATE_DATA_AND_LOG(data, type, key, ret_val)                        \
    do {                                                                       \
        if (!data || !data->data) {                                            \
            gf_msg_callingfn("dict", GF_LOG_DEBUG, EINVAL, LG_MSG_INVALID_ARG, \
                             "data is NULL");                                  \
            return ret_val;                                                    \
        }                                                                      \
        /* Not of the asked type, or old version */                            \
        if ((data->data_type != type) &&                                       \
            (data->data_type != GF_DATA_TYPE_STR_OLD)) {                       \
            gf_msg_callingfn("dict", GF_LOG_DEBUG, EINVAL, LG_MSG_INVALID_ARG, \
                             "key %s, %s type asked, has %s type", key,        \
                             data_type_name[type],                             \
                             data_type_name[data->data_type]);                 \
        }                                                                      \
    } while (0)

static data_t *
get_new_data()
{
    data_t *data = mem_get(THIS->ctx->dict_data_pool);

    if (!data)
        return NULL;

    GF_ATOMIC_INIT(data->refcount, 0);
    data->is_static = _gf_false;

    return data;
}

static dict_t *
get_new_dict_full(int size_hint)
{
    dict_t *dict = mem_get0(THIS->ctx->dict_pool);

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
    } else {
        /*
         * We actually need to allocate space for size_hint *pointers*
         * but we actually allocate space for one *structure*.  Since
         * a data_pair_t consists of five pointers, we're wasting four
         * pointers' worth for N=1, and will overrun what we allocated
         * for N>5.  If anybody ever starts using size_hint, we'll need
         * to fix this.
         */
        GF_ASSERT(size_hint <= (sizeof(data_pair_t) / sizeof(data_pair_t *)));
        dict->members = mem_get0(THIS->ctx->dict_pair_pool);
        if (!dict->members) {
            mem_put(dict);
            return NULL;
        }
    }

    dict->free_pair.key = NULL;
    dict->totkvlen = 0;
    LOCK_INIT(&dict->lock);

    return dict;
}

dict_t *
dict_new(void)
{
    dict_t *dict = get_new_dict_full(1);

    if (dict)
        dict_ref(dict);

    return dict;
}

int32_t
is_data_equal(data_t *one, data_t *two)
{
    struct iatt *iatt1, *iatt2;
    struct mdata_iatt *mdata_iatt1, *mdata_iatt2;

    if (!one || !two || !one->data || !two->data) {
        gf_msg_callingfn("dict", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "input arguments are provided "
                         "with value data_t as NULL");
        return -1;
    }

    if (one == two)
        return 1;

    if (one->data == two->data)
        return 1;

    if (one->data_type != two->data_type) {
        return 0;
    }

    if (one->data_type == GF_DATA_TYPE_IATT) {
        if ((one->len < sizeof(struct iatt)) ||
            (two->len < sizeof(struct iatt))) {
            return 0;
        }

        iatt1 = (struct iatt *)one->data;
        iatt2 = (struct iatt *)two->data;

        /* Two iatt structs are considered equal if main fields are
         * equal, even if times differ.
         * TODO: maybe when ctime if fully operational we could
         *       enforce time matching. */
        if (iatt1->ia_ino != iatt2->ia_ino) {
            return 0;
        }
        if (iatt1->ia_type != iatt2->ia_type) {
            return 0;
        }
        if ((iatt1->ia_type == IA_IFBLK) || (iatt1->ia_type == IA_IFCHR)) {
            if (iatt1->ia_rdev != iatt2->ia_rdev) {
                return 0;
            }
        }
        if (gf_uuid_compare(iatt1->ia_gfid, iatt2->ia_gfid) != 0) {
            return 0;
        }

        /* TODO: ia_uid, ia_gid, ia_prot and ia_size can be changed
         *       with some commands. Here we don't have enough
         *       information to decide if they should match or not. */
        /*
                        if ((iatt1->ia_uid != iatt2->ia_uid) ||
                            (iatt1->ia_gid != iatt2->ia_gid) ||
                            (st_mode_from_ia(iatt1->ia_prot, iatt1->ia_type) !=
                                    st_mode_from_ia(iatt2->ia_prot,
           iatt2->ia_type))) { return 0;
                        }
                        if (iatt1->ia_type == IA_IFREG) {
                                if (iatt1->ia_size != iatt2->ia_size) {
                                        return 0;
                                }
                        }
        */
        return 1;
    }
    if (one->data_type == GF_DATA_TYPE_MDATA) {
        if ((one->len < sizeof(struct mdata_iatt)) ||
            (two->len < sizeof(struct mdata_iatt))) {
            return 0;
        }
        mdata_iatt1 = (struct mdata_iatt *)one->data;
        mdata_iatt2 = (struct mdata_iatt *)two->data;

        if (mdata_iatt1->ia_atime != mdata_iatt2->ia_atime ||
            mdata_iatt1->ia_mtime != mdata_iatt2->ia_mtime ||
            mdata_iatt1->ia_ctime != mdata_iatt2->ia_ctime ||
            mdata_iatt1->ia_atime_nsec != mdata_iatt2->ia_atime_nsec ||
            mdata_iatt1->ia_mtime_nsec != mdata_iatt2->ia_mtime_nsec ||
            mdata_iatt1->ia_ctime_nsec != mdata_iatt2->ia_ctime_nsec) {
            return 0;
        }
        return 1;
    }

    if (one->len != two->len)
        return 0;

    if (memcmp(one->data, two->data, one->len) == 0)
        return 1;

    return 0;
}

static int
key_value_cmp(dict_t *one, char *key1, data_t *value1, void *data)
{
    struct dict_cmp *cmp = data;
    dict_t *two = cmp->dict;
    data_t *value2 = dict_get(two, key1);

    if (value2) {
        if (cmp->value_ignore && cmp->value_ignore(key1))
            return 0;

        if (is_data_equal(value1, value2) == 1)
            return 0;
    }

    if (value2 == NULL) {
        gf_msg_debug(THIS->name, 0, "'%s' found only on one dict", key1);
    } else {
        gf_msg_debug(THIS->name, 0,
                     "'%s' is different in two dicts "
                     "(%u, %u)",
                     key1, value1->len, value2->len);
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
are_dicts_equal(dict_t *one, dict_t *two,
                gf_boolean_t (*match)(dict_t *d, char *k, data_t *v,
                                      void *data),
                gf_boolean_t (*value_ignore)(char *k))
{
    int num_matches1 = 0;
    int num_matches2 = 0;
    struct dict_cmp cmp = {0};

    if (one == two)
        return _gf_true;

    if (!match)
        match = dict_match_everything;

    if ((one == NULL) || (two == NULL)) {
        num_matches1 = dict_foreach_match(one ? one : two, match, NULL,
                                          dict_null_foreach_fn, NULL);
        goto done;
    }

    cmp.dict = two;
    cmp.value_ignore = value_ignore;
    num_matches1 = dict_foreach_match(one, match, NULL, key_value_cmp, &cmp);

    if (num_matches1 == -1)
        return _gf_false;

    if ((num_matches1 == one->count) && (one->count == two->count))
        return _gf_true;

    num_matches2 = dict_foreach_match(two, match, NULL, dict_null_foreach_fn,
                                      NULL);
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
data_destroy(data_t *data)
{
    if (data) {
        if (!data->is_static)
            GF_FREE(data->data);

        data->len = 0xbabababa;
        mem_put(data);
    }
}

data_t *
data_copy(data_t *old)
{
    if (!old) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, 0, LG_MSG_NULL_PTR,
                         "old is NULL");
        return NULL;
    }

    data_t *newdata = mem_get0(THIS->ctx->dict_data_pool);
    if (!newdata) {
        return NULL;
    }

    newdata->len = old->len;
    if (old->data) {
        newdata->data = gf_memdup(old->data, old->len);
        if (!newdata->data)
            goto err_out;
    }
    newdata->data_type = old->data_type;

    return newdata;

err_out:
    mem_put(newdata);

    return NULL;
}

/* Always need to be called under lock
 * Always this and key variables are not null -
 * checked by callers.
 */
static data_pair_t *
dict_lookup_common(const dict_t *this, const char *key, const uint32_t hash)
{
    int hashval = 0;
    data_pair_t *pair;

    /* If the divisor is 1, the modulo is always 0,
     * in such case avoid hash calculation.
     */
    if (this->hash_size != 1)
        hashval = hash % this->hash_size;

    for (pair = this->members[hashval]; pair != NULL; pair = pair->hash_next) {
        if (pair->key && (hash == pair->key_hash) && !strcmp(pair->key, key))
            return pair;
    }

    return NULL;
}

int32_t
dict_lookup(dict_t *this, char *key, data_t **data)
{
    if (!this || !key || !data) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "!this || !key || "
                         "!data");
        return -1;
    }

    data_pair_t *tmp = NULL;

    uint32_t hash = (uint32_t)XXH64(key, strlen(key), 0);

    LOCK(&this->lock);
    {
        tmp = dict_lookup_common(this, key, hash);
    }
    UNLOCK(&this->lock);

    if (!tmp)
        return -1;

    *data = tmp->value;
    return 0;
}

static int32_t
dict_set_lk(dict_t *this, char *key, const int key_len, data_t *value,
            const uint32_t hash, gf_boolean_t replace)
{
    int hashval = 0;
    data_pair_t *pair;
    int key_free = 0;
    uint32_t key_hash;
    int keylen;

    if (!key) {
        keylen = gf_asprintf(&key, "ref:%p", value);
        if (-1 == keylen) {
            return -1;
        }
        key_free = 1;
        key_hash = (uint32_t)XXH64(key, keylen, 0);
    } else {
        keylen = key_len;
        key_hash = hash;
    }

    /* Search for a existing key if 'replace' is asked for */
    if (replace) {
        pair = dict_lookup_common(this, key, key_hash);
        if (pair) {
            data_t *unref_data = pair->value;
            pair->value = data_ref(value);
            this->totkvlen += (value->len - unref_data->len);
            data_unref(unref_data);
            if (key_free)
                GF_FREE(key);
            /* Indicates duplicate key */
            return 0;
        }
    }

    if (this->free_pair.key) { /* the free_pair is used */
        pair = mem_get(THIS->ctx->dict_pair_pool);
        if (!pair) {
            if (key_free)
                GF_FREE(key);
            return -1;
        }
    } else { /* assign the pair to the free pair */
        pair = &this->free_pair;
    }

    if (key_free) {
        /* It's ours.  Use it. */
        pair->key = key;
        key_free = 0;
    } else {
        pair->key = (char *)GF_MALLOC(keylen + 1, gf_common_mt_char);
        if (!pair->key) {
            if (pair != &this->free_pair) {
                mem_put(pair);
            }
            return -1;
        }
        strcpy(pair->key, key);
    }
    pair->key_hash = key_hash;
    pair->value = data_ref(value);
    this->totkvlen += (keylen + 1 + value->len);

    /* If the divisor is 1, the modulo is always 0,
     * in such case avoid hash calculation.
     */
    if (this->hash_size != 1) {
        hashval = (key_hash % this->hash_size);
    }
    pair->hash_next = this->members[hashval];
    this->members[hashval] = pair;

    pair->next = this->members_list;
    pair->prev = NULL;
    if (this->members_list)
        this->members_list->prev = pair;
    this->members_list = pair;
    this->count++;

    if (key_free)
        GF_FREE(key);

    if (this->max_count < this->count)
        this->max_count = this->count;
    return 0;
}

int32_t
dict_set(dict_t *this, char *key, data_t *value)
{
    if (key)
        return dict_setn(this, key, strlen(key), value);
    else
        return dict_setn(this, NULL, 0, value);
}

int32_t
dict_setn(dict_t *this, char *key, const int keylen, data_t *value)
{
    int32_t ret;
    uint32_t key_hash = 0;

    if (!this || !value) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "!this || !value for "
                         "key=%s",
                         key);
        return -1;
    }

    if (key) {
        key_hash = (uint32_t)XXH64(key, keylen, 0);
    }

    LOCK(&this->lock);

    ret = dict_set_lk(this, key, keylen, value, key_hash, 1);

    UNLOCK(&this->lock);

    return ret;
}

int32_t
dict_add(dict_t *this, char *key, data_t *value)
{
    if (key)
        return dict_addn(this, key, strlen(key), value);
    else
        return dict_addn(this, NULL, 0, value);
}

int32_t
dict_addn(dict_t *this, char *key, const int keylen, data_t *value)
{
    int32_t ret;
    uint32_t key_hash = 0;

    if (!this || !value) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "!this || !value for key=%s", key);
        return -1;
    }

    if (key) {
        key_hash = (uint32_t)XXH64(key, keylen, 0);
    }

    LOCK(&this->lock);

    ret = dict_set_lk(this, key, keylen, value, key_hash, 0);

    UNLOCK(&this->lock);

    return ret;
}

data_t *
dict_get(dict_t *this, char *key)
{
    if (!this || !key) {
        gf_msg_callingfn("dict", GF_LOG_DEBUG, EINVAL, LG_MSG_INVALID_ARG,
                         "!this || key=%s", (key) ? key : "()");
        return NULL;
    }

    return dict_getn(this, key, strlen(key));
}

data_t *
dict_getn(dict_t *this, char *key, const int keylen)
{
    data_pair_t *pair;
    uint32_t hash;

    if (!this || !key) {
        gf_msg_callingfn("dict", GF_LOG_DEBUG, EINVAL, LG_MSG_INVALID_ARG,
                         "!this || key=%s", (key) ? key : "()");
        return NULL;
    }

    hash = (uint32_t)XXH64(key, keylen, 0);

    LOCK(&this->lock);
    {
        pair = dict_lookup_common(this, key, hash);
    }
    UNLOCK(&this->lock);

    if (pair)
        return pair->value;

    return NULL;
}

int
dict_key_count(dict_t *this)
{
    int ret = -1;

    if (!this) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "dict passed is NULL");
        return ret;
    }

    LOCK(&this->lock);
    {
        ret = this->count;
    }
    UNLOCK(&this->lock);

    return ret;
}

void
dict_del(dict_t *this, char *key)
{
    if (!this || !key) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "!this || key=%s", key);
        return;
    }

    return dict_deln(this, key, strlen(key));
}

void
dict_deln(dict_t *this, char *key, const int keylen)
{
    int hashval = 0;
    uint32_t hash;

    if (!this || !key) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "!this || key=%s", key);
        return;
    }

    hash = (uint32_t)XXH64(key, keylen, 0);

    LOCK(&this->lock);

    /* If the divisor is 1, the modulo is always 0,
     * in such case avoid hash calculation.
     */
    if (this->hash_size != 1)
        hashval = hash % this->hash_size;

    data_pair_t *pair = this->members[hashval];
    data_pair_t *prev = NULL;

    while (pair) {
        if ((hash == pair->key_hash) && strcmp(pair->key, key) == 0) {
            if (prev)
                prev->hash_next = pair->hash_next;
            else
                this->members[hashval] = pair->hash_next;

            this->totkvlen -= pair->value->len;
            data_unref(pair->value);

            if (pair->prev)
                pair->prev->next = pair->next;
            else
                this->members_list = pair->next;

            if (pair->next)
                pair->next->prev = pair->prev;

            this->totkvlen -= (strlen(pair->key) + 1);
            GF_FREE(pair->key);
            if (pair == &this->free_pair) {
                this->free_pair.key = NULL;
            } else {
                mem_put(pair);
            }
            this->count--;
            break;
        }

        prev = pair;
        pair = pair->hash_next;
    }

    UNLOCK(&this->lock);

    return;
}

void
dict_destroy(dict_t *this)
{
    if (!this) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "dict is NULL");
        return;
    }

    data_pair_t *pair = this->members_list;
    data_pair_t *prev = this->members_list;
    glusterfs_ctx_t *ctx = NULL;
    uint64_t current_max = 0;
    uint32_t total_pairs = 0;

    LOCK_DESTROY(&this->lock);

    while (prev) {
        pair = pair->next;
        data_unref(prev->value);
        GF_FREE(prev->key);
        if (prev != &this->free_pair) {
            mem_put(prev);
        } else {
            this->free_pair.key = NULL;
        }
        total_pairs++;
        prev = pair;
    }

    this->totkvlen = 0;
    if (this->members != &this->members_internal) {
        mem_put(this->members);
    }

    free(this->extra_stdfree);

    /* update 'ctx->stats.dict.details' using max_count */
    ctx = THIS->ctx;

    /* NOTE: below logic is not totaly race proof */
    /* thread0 and thread1 gets current_max as 10 */
    /* thread0 has 'this->max_count as 11 */
    /* thread1 has 'this->max_count as 20 */
    /* thread1 goes ahead and sets the max_dict_pairs to 20 */
    /* thread0 then goes and sets it to 11 */
    /* As it is for information purpose only, no functionality will be
       broken by this, but a point to consider about ATOMIC macros. */
    current_max = GF_ATOMIC_GET(ctx->stats.max_dict_pairs);
    if (current_max < this->max_count)
        GF_ATOMIC_INIT(ctx->stats.max_dict_pairs, this->max_count);

    GF_ATOMIC_ADD(ctx->stats.total_pairs_used, total_pairs);
    GF_ATOMIC_INC(ctx->stats.total_dicts_used);

    mem_put(this);

    return;
}

void
dict_unref(dict_t *this)
{
    uint64_t ref = 0;

    if (!this) {
        gf_msg_callingfn("dict", GF_LOG_DEBUG, EINVAL, LG_MSG_INVALID_ARG,
                         "dict is NULL");
        return;
    }

    ref = GF_ATOMIC_DEC(this->refcount);

    if (!ref)
        dict_destroy(this);
}

dict_t *
dict_ref(dict_t *this)
{
    if (!this) {
        gf_msg_callingfn("dict", GF_LOG_DEBUG, EINVAL, LG_MSG_INVALID_ARG,
                         "dict is NULL");
        return NULL;
    }

    GF_ATOMIC_INC(this->refcount);
    return this;
}

void
data_unref(data_t *this)
{
    uint64_t ref;

    if (!this) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "data is NULL");
        return;
    }

    ref = GF_ATOMIC_DEC(this->refcount);

    if (!ref)
        data_destroy(this);
}

data_t *
data_ref(data_t *this)
{
    if (!this) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "data is NULL");
        return NULL;
    }

    GF_ATOMIC_INC(this->refcount);

    return this;
}

data_t *
int_to_data(int64_t value)
{
    data_t *data = get_new_data();

    if (!data) {
        return NULL;
    }

    data->len = gf_asprintf(&data->data, "%" PRId64, value);
    if (-1 == data->len) {
        gf_msg_debug("dict", 0, "asprintf failed");
        data_destroy(data);
        return NULL;
    }
    data->len++; /* account for terminating NULL */
    data->data_type = GF_DATA_TYPE_INT;

    return data;
}

data_t *
data_from_int64(int64_t value)
{
    data_t *data = get_new_data();

    if (!data) {
        return NULL;
    }
    data->len = gf_asprintf(&data->data, "%" PRId64, value);
    if (-1 == data->len) {
        gf_msg_debug("dict", 0, "asprintf failed");
        data_destroy(data);
        return NULL;
    }
    data->len++; /* account for terminating NULL */
    data->data_type = GF_DATA_TYPE_INT;

    return data;
}

data_t *
data_from_int32(int32_t value)
{
    data_t *data = get_new_data();

    if (!data) {
        return NULL;
    }
    data->len = gf_asprintf(&data->data, "%" PRId32, value);
    if (-1 == data->len) {
        gf_msg_debug("dict", 0, "asprintf failed");
        data_destroy(data);
        return NULL;
    }

    data->len++; /* account for terminating NULL */
    data->data_type = GF_DATA_TYPE_INT;

    return data;
}

data_t *
data_from_int16(int16_t value)
{
    data_t *data = get_new_data();

    if (!data) {
        return NULL;
    }
    data->len = gf_asprintf(&data->data, "%" PRId16, value);
    if (-1 == data->len) {
        gf_msg_debug("dict", 0, "asprintf failed");
        data_destroy(data);
        return NULL;
    }

    data->len++; /* account for terminating NULL */
    data->data_type = GF_DATA_TYPE_INT;

    return data;
}

data_t *
data_from_int8(int8_t value)
{
    data_t *data = get_new_data();

    if (!data) {
        return NULL;
    }
    data->len = gf_asprintf(&data->data, "%d", value);
    if (-1 == data->len) {
        gf_msg_debug("dict", 0, "asprintf failed");
        data_destroy(data);
        return NULL;
    }

    data->len++; /* account for terminating NULL */
    data->data_type = GF_DATA_TYPE_INT;

    return data;
}

data_t *
data_from_uint64(uint64_t value)
{
    data_t *data = get_new_data();

    if (!data) {
        return NULL;
    }
    data->len = gf_asprintf(&data->data, "%" PRIu64, value);
    if (-1 == data->len) {
        gf_msg_debug("dict", 0, "asprintf failed");
        data_destroy(data);
        return NULL;
    }

    data->len++; /* account for terminating NULL */
    data->data_type = GF_DATA_TYPE_UINT;

    return data;
}

data_t *
data_from_double(double value)
{
    data_t *data = get_new_data();

    if (!data) {
        return NULL;
    }

    data->len = gf_asprintf(&data->data, "%f", value);
    if (data->len == -1) {
        gf_msg_debug("dict", 0, "asprintf failed");
        data_destroy(data);
        return NULL;
    }
    data->len++; /* account for terminating NULL */
    data->data_type = GF_DATA_TYPE_DOUBLE;

    return data;
}

data_t *
data_from_uint32(uint32_t value)
{
    data_t *data = get_new_data();

    if (!data) {
        return NULL;
    }
    data->len = gf_asprintf(&data->data, "%" PRIu32, value);
    if (-1 == data->len) {
        gf_msg_debug("dict", 0, "asprintf failed");
        data_destroy(data);
        return NULL;
    }

    data->len++; /* account for terminating NULL */
    data->data_type = GF_DATA_TYPE_UINT;

    return data;
}

data_t *
data_from_uint16(uint16_t value)
{
    data_t *data = get_new_data();

    if (!data) {
        return NULL;
    }
    data->len = gf_asprintf(&data->data, "%" PRIu16, value);
    if (-1 == data->len) {
        gf_msg_debug("dict", 0, "asprintf failed");
        data_destroy(data);
        return NULL;
    }

    data->len++; /* account for terminating NULL */
    data->data_type = GF_DATA_TYPE_UINT;

    return data;
}

static data_t *
data_from_ptr_common(void *value, gf_boolean_t is_static)
{
    /* it is valid to set 0/NULL as a value, no need to check *value */

    data_t *data = get_new_data();
    if (!data) {
        return NULL;
    }

    data->data = value;
    data->len = 0;
    data->is_static = is_static;

    data->data_type = GF_DATA_TYPE_PTR;
    return data;
}

data_t *
str_to_data(char *value)
{
    if (!value) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "value is NULL");
        return NULL;
    }

    return strn_to_data(value, strlen(value));
}

data_t *
strn_to_data(char *value, const int vallen)
{
    if (!value) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "value is NULL");
        return NULL;
    }
    data_t *data = get_new_data();

    if (!data) {
        return NULL;
    }
    data->len = vallen + 1;
    data->data_type = GF_DATA_TYPE_STR;

    data->data = value;
    data->is_static = _gf_true;

    return data;
}

static data_t *
data_from_dynstr(char *value)
{
    if (!value) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "value is NULL");
        return NULL;
    }

    data_t *data = get_new_data();

    if (!data)
        return NULL;
    data->len = strlen(value) + 1;
    data->data = value;
    data->data_type = GF_DATA_TYPE_STR;

    return data;
}

data_t *
data_from_dynptr(void *value, int32_t len)
{
    data_t *data = get_new_data();

    if (!data)
        return NULL;

    data->len = len;
    data->data = value;
    data->data_type = GF_DATA_TYPE_PTR;

    return data;
}

data_t *
bin_to_data(void *value, int32_t len)
{
    if (!value) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "value is NULL");
        return NULL;
    }

    data_t *data = get_new_data();

    if (!data)
        return NULL;

    data->is_static = _gf_true;
    data->len = len;
    data->data = value;

    return data;
}

static char *data_type_name[GF_DATA_TYPE_MAX] = {
    [GF_DATA_TYPE_UNKNOWN] = "unknown",
    [GF_DATA_TYPE_STR_OLD] = "string-old-version",
    [GF_DATA_TYPE_INT] = "integer",
    [GF_DATA_TYPE_UINT] = "unsigned integer",
    [GF_DATA_TYPE_DOUBLE] = "float",
    [GF_DATA_TYPE_STR] = "string",
    [GF_DATA_TYPE_PTR] = "pointer",
    [GF_DATA_TYPE_GFUUID] = "gf-uuid",
    [GF_DATA_TYPE_IATT] = "iatt",
    [GF_DATA_TYPE_MDATA] = "mdata",
};

int64_t
data_to_int64(data_t *data)
{
    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_INT, "null", -1);

    char *endptr = NULL;
    int64_t value = 0;

    errno = 0;
    value = strtoll(data->data, &endptr, 0);

    if (endptr && *endptr != '\0')
        /* Unrecognized characters at the end of string. */
        errno = EINVAL;
    if (errno) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, errno,
                         LG_MSG_DATA_CONVERSION_ERROR,
                         "Error in data conversion: '%s' can't "
                         "be represented as int64_t",
                         data->data);
        return -1;
    }
    return value;
}

/* Like above but implies signed range check. */

#define DATA_TO_RANGED_SIGNED(endptr, value, data, type, min, max)             \
    do {                                                                       \
        errno = 0;                                                             \
        value = strtoll(data->data, &endptr, 0);                               \
        if (endptr && *endptr != '\0')                                         \
            errno = EINVAL;                                                    \
        if (errno || value > max || value < min) {                             \
            gf_msg_callingfn("dict", GF_LOG_WARNING, errno,                    \
                             LG_MSG_DATA_CONVERSION_ERROR,                     \
                             "Error in data conversion: '%s' can't "           \
                             "be represented as " #type,                       \
                             data->data);                                      \
            return -1;                                                         \
        }                                                                      \
        return (type)value;                                                    \
    } while (0)

int32_t
data_to_int32(data_t *data)
{
    char *endptr = NULL;
    int64_t value = 0;

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_INT, "null", -1);
    DATA_TO_RANGED_SIGNED(endptr, value, data, int32_t, INT_MIN, INT_MAX);
}

int16_t
data_to_int16(data_t *data)
{
    char *endptr = NULL;
    int64_t value = 0;

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_INT, "null", -1);
    DATA_TO_RANGED_SIGNED(endptr, value, data, int16_t, SHRT_MIN, SHRT_MAX);
}

int8_t
data_to_int8(data_t *data)
{
    char *endptr = NULL;
    int64_t value = 0;

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_INT, "null", -1);
    DATA_TO_RANGED_SIGNED(endptr, value, data, int8_t, CHAR_MIN, CHAR_MAX);
}

uint64_t
data_to_uint64(data_t *data)
{
    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_UINT, "null", -1);

    char *endptr = NULL;
    uint64_t value = 0;

    errno = 0;
    value = strtoull(data->data, &endptr, 0);

    if (endptr && *endptr != '\0')
        errno = EINVAL;
    if (errno) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, errno,
                         LG_MSG_DATA_CONVERSION_ERROR,
                         "Error in data conversion: '%s' can't "
                         "be represented as uint64_t",
                         data->data);
        return -1;
    }
    return value;
}

/* Like above but implies unsigned range check. */

#define DATA_TO_RANGED_UNSIGNED(endptr, value, data, type, max)                \
    do {                                                                       \
        errno = 0;                                                             \
        value = strtoull(data->data, &endptr, 0);                              \
        if (endptr && *endptr != '\0')                                         \
            errno = EINVAL;                                                    \
        if (errno || value > max) {                                            \
            gf_msg_callingfn("dict", GF_LOG_WARNING, errno,                    \
                             LG_MSG_DATA_CONVERSION_ERROR,                     \
                             "Error in data conversion: '%s' can't "           \
                             "be represented as " #type,                       \
                             data->data);                                      \
            return -1;                                                         \
        }                                                                      \
        return (type)value;                                                    \
    } while (0)

uint32_t
data_to_uint32(data_t *data)
{
    char *endptr = NULL;
    uint64_t value = 0;

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_UINT, "null", -1);
    DATA_TO_RANGED_UNSIGNED(endptr, value, data, uint32_t, UINT_MAX);
}

uint16_t
data_to_uint16(data_t *data)
{
    char *endptr = NULL;
    uint64_t value = 0;

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_UINT, "null", -1);
    DATA_TO_RANGED_UNSIGNED(endptr, value, data, uint16_t, USHRT_MAX);
}

uint8_t
data_to_uint8(data_t *data)
{
    char *endptr = NULL;
    uint64_t value = 0;

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_UINT, "null", -1);
    DATA_TO_RANGED_UNSIGNED(endptr, value, data, uint8_t, UCHAR_MAX);
}

char *
data_to_str(data_t *data)
{
    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_STR, "null", NULL);
    return data->data;
}

void *
data_to_ptr(data_t *data)
{
    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_PTR, "null", NULL);
    return data->data;
}

void *
data_to_bin(data_t *data)
{
    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_PTR, "null", NULL);
    return data->data;
}

struct iatt *
data_to_iatt(data_t *data, char *key)
{
    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_IATT, key, NULL);

    /* We only check for smaller size. If it's bigger we simply ignore
     * the extra data. This way it's easy to do changes in the future that
     * pass more data but are backward compatible (if the initial contents
     * of the struct are maintained, of course). */
    if (data->len < sizeof(struct iatt)) {
        gf_smsg("glusterfs", GF_LOG_ERROR, ENOBUFS, LG_MSG_UNDERSIZED_BUF,
                "key=%s", key, NULL);
        return NULL;
    }

    return (struct iatt *)data->data;
}

int
dict_null_foreach_fn(dict_t *d, char *k, data_t *v, void *tmp)
{
    return 0;
}

int
dict_remove_foreach_fn(dict_t *d, char *k, data_t *v, void *_tmp)
{
    if (!d || !k) {
        gf_smsg("glusterfs", GF_LOG_WARNING, EINVAL, LG_MSG_KEY_OR_VALUE_NULL,
                "d=%s", d ? "key" : "dictionary", NULL);
        return -1;
    }

    dict_del(d, k);
    return 0;
}

gf_boolean_t
dict_match_everything(dict_t *d, char *k, data_t *v, void *data)
{
    return _gf_true;
}

int
dict_foreach(dict_t *dict,
             int (*fn)(dict_t *this, char *key, data_t *value, void *data),
             void *data)
{
    int ret = dict_foreach_match(dict, dict_match_everything, NULL, fn, data);

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
dict_foreach_match(dict_t *dict,
                   gf_boolean_t (*match)(dict_t *this, char *key, data_t *value,
                                         void *mdata),
                   void *match_data,
                   int (*action)(dict_t *this, char *key, data_t *value,
                                 void *adata),
                   void *action_data)
{
    if (!dict || !match || !action) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "dict|match|action is "
                         "NULL");
        return -1;
    }

    int ret = -1;
    int count = 0;
    data_pair_t *pairs = dict->members_list;
    data_pair_t *next = NULL;

    while (pairs) {
        next = pairs->next;
        if (match(dict, pairs->key, pairs->value, match_data)) {
            ret = action(dict, pairs->key, pairs->value, action_data);
            if (ret < 0)
                return ret;
            count++;
        }
        pairs = next;
    }

    return count;
}

static gf_boolean_t
dict_fnmatch(dict_t *d, char *k, data_t *val, void *match_data)
{
    return (fnmatch(match_data, k, 0) == 0);
}
/* return values:
   -1 = failure,
    0 = no matches found,
   +n = n number of matches
*/
int
dict_foreach_fnmatch(dict_t *dict, char *pattern,
                     int (*fn)(dict_t *this, char *key, data_t *value,
                               void *data),
                     void *data)
{
    return dict_foreach_match(dict, dict_fnmatch, pattern, fn, data);
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
dict_keys_join(void *value, int size, dict_t *dict, int (*filter_fn)(char *k))
{
    int len = 0;
    data_pair_t *pairs = dict->members_list;
    data_pair_t *next = NULL;

    while (pairs) {
        next = pairs->next;

        if (filter_fn && filter_fn(pairs->key)) {
            pairs = next;
            continue;
        }

        if (value && (size > len))
            strncpy(value + len, pairs->key, size - len);

        len += (strlen(pairs->key) + 1);

        pairs = next;
    }

    return len;
}

static int
dict_copy_one(dict_t *unused, char *key, data_t *value, void *newdict)
{
    return dict_set((dict_t *)newdict, key, (value));
}

dict_t *
dict_copy(dict_t *dict, dict_t *new)
{
    if (!dict) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "dict is NULL");
        return NULL;
    }

    if (!new)
        new = get_new_dict_full(dict->hash_size);

    dict_foreach(dict, dict_copy_one, new);

    return new;
}

int
dict_reset(dict_t *dict)
{
    int32_t ret = -1;
    if (!dict) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "dict is NULL");
        goto out;
    }
    dict_foreach(dict, dict_remove_foreach_fn, NULL);
    ret = 0;
out:
    return ret;
}

dict_t *
dict_copy_with_ref(dict_t *dict, dict_t *new)
{
    dict_t *local_new = NULL;

    GF_VALIDATE_OR_GOTO("dict", dict, fail);

    if (new == NULL) {
        local_new = dict_new();
        GF_VALIDATE_OR_GOTO("dict", local_new, fail);
        new = local_new;
    }

    dict_foreach(dict, dict_copy_one, new);
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
dict_get_with_refn(dict_t *this, char *key, const int keylen, data_t **data)
{
    data_pair_t *pair = NULL;
    int ret = -ENOENT;
    uint32_t hash;

    hash = (uint32_t)XXH64(key, keylen, 0);

    LOCK(&this->lock);
    {
        pair = dict_lookup_common(this, key, hash);

        if (pair) {
            ret = 0;
            *data = data_ref(pair->value);
        }
    }
    UNLOCK(&this->lock);

    return ret;
}

int
dict_get_with_ref(dict_t *this, char *key, data_t **data)
{
    if (!this || !key || !data) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "dict OR key (%s) is NULL", key);
        return -EINVAL;
    }

    return dict_get_with_refn(this, key, strlen(key), data);
}

static int
data_to_ptr_common(data_t *data, void **val)
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
data_to_int8_ptr(data_t *data, int8_t *val)
{
    int ret = 0;

    if (!data || !val) {
        ret = -EINVAL;
        goto err;
    }

    errno = 0;
    *val = strtol(data->data, NULL, 0);
    if (errno != 0)
        ret = -errno;

err:
    return ret;
}

static int
data_to_int16_ptr(data_t *data, int16_t *val)
{
    int ret = 0;

    if (!data || !val) {
        ret = -EINVAL;
        goto err;
    }

    errno = 0;
    *val = strtol(data->data, NULL, 0);
    if (errno != 0)
        ret = -errno;

err:
    return ret;
}

static int
data_to_int32_ptr(data_t *data, int32_t *val)
{
    int ret = 0;

    if (!data || !val) {
        ret = -EINVAL;
        goto err;
    }

    errno = 0;
    *val = strtol(data->data, NULL, 0);
    if (errno != 0)
        ret = -errno;

err:
    return ret;
}

static int
data_to_int64_ptr(data_t *data, int64_t *val)
{
    int ret = 0;

    if (!data || !val) {
        ret = -EINVAL;
        goto err;
    }

    errno = 0;
    *val = strtoll(data->data, NULL, 0);
    if (errno != 0)
        ret = -errno;

err:
    return ret;
}

static int
data_to_uint16_ptr(data_t *data, uint16_t *val)
{
    int ret = 0;

    if (!data || !val) {
        ret = -EINVAL;
        goto err;
    }

    errno = 0;
    *val = strtoul(data->data, NULL, 0);
    if (errno != 0)
        ret = -errno;

err:
    return ret;
}

static int
data_to_uint32_ptr(data_t *data, uint32_t *val)
{
    int ret = 0;

    if (!data || !val) {
        ret = -EINVAL;
        goto err;
    }

    errno = 0;
    *val = strtoul(data->data, NULL, 0);
    if (errno != 0)
        ret = -errno;

err:
    return ret;
}

static int
data_to_uint64_ptr(data_t *data, uint64_t *val)
{
    int ret = 0;

    if (!data || !val) {
        ret = -EINVAL;
        goto err;
    }

    errno = 0;
    *val = strtoull(data->data, NULL, 0);
    if (errno != 0)
        ret = -errno;

err:
    return ret;
}

static int
data_to_double_ptr(data_t *data, double *val)
{
    int ret = 0;

    if (!data || !val) {
        ret = -EINVAL;
        goto err;
    }

    errno = 0;
    *val = strtod(data->data, NULL);
    if (errno != 0)
        ret = -errno;

err:
    return ret;
}

int
dict_get_int8(dict_t *this, char *key, int8_t *val)
{
    data_t *data = NULL;
    int ret = 0;

    if (!val) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_get_with_ref(this, key, &data);
    if (ret != 0) {
        goto err;
    }

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_INT, key, -EINVAL);

    ret = data_to_int8_ptr(data, val);

err:
    if (data)
        data_unref(data);
    return ret;
}

int
dict_set_int8(dict_t *this, char *key, int8_t val)
{
    data_t *data = NULL;
    int ret = 0;

    data = data_from_int8(val);
    if (!data) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_set(this, key, data);
    if (ret < 0)
        data_destroy(data);

err:
    return ret;
}

int
dict_get_int16(dict_t *this, char *key, int16_t *val)
{
    data_t *data = NULL;
    int ret = 0;

    if (!val) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_get_with_ref(this, key, &data);
    if (ret != 0) {
        goto err;
    }

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_INT, key, -EINVAL);

    ret = data_to_int16_ptr(data, val);

err:
    if (data)
        data_unref(data);
    return ret;
}

int
dict_set_int16(dict_t *this, char *key, int16_t val)
{
    data_t *data = NULL;
    int ret = 0;

    data = data_from_int16(val);
    if (!data) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_set(this, key, data);
    if (ret < 0)
        data_destroy(data);

err:
    return ret;
}

int
dict_get_int32n(dict_t *this, char *key, const int keylen, int32_t *val)
{
    data_t *data = NULL;
    int ret = 0;

    if (!this || !key || !val) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_get_with_refn(this, key, keylen, &data);
    if (ret != 0) {
        goto err;
    }

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_INT, key, -EINVAL);

    ret = data_to_int32_ptr(data, val);

err:
    if (data)
        data_unref(data);
    return ret;
}

int
dict_get_int32(dict_t *this, char *key, int32_t *val)
{
    data_t *data = NULL;
    int ret = 0;

    if (!val) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_get_with_ref(this, key, &data);
    if (ret != 0) {
        goto err;
    }

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_INT, key, -EINVAL);

    ret = data_to_int32_ptr(data, val);

err:
    if (data)
        data_unref(data);
    return ret;
}

int
dict_set_int32n(dict_t *this, char *key, const int keylen, int32_t val)
{
    data_t *data = NULL;
    int ret = 0;

    data = data_from_int32(val);
    if (!data) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_setn(this, key, keylen, data);
    if (ret < 0)
        data_destroy(data);

err:
    return ret;
}

int
dict_set_int32(dict_t *this, char *key, int32_t val)
{
    data_t *data = data_from_int32(val);
    int ret = 0;

    if (!data) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_set(this, key, data);
    if (ret < 0)
        data_destroy(data);

err:
    return ret;
}

int
dict_get_int64(dict_t *this, char *key, int64_t *val)
{
    data_t *data = NULL;
    int ret = 0;

    if (!val) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_get_with_ref(this, key, &data);
    if (ret != 0) {
        goto err;
    }

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_INT, key, -EINVAL);

    ret = data_to_int64_ptr(data, val);

err:
    if (data)
        data_unref(data);
    return ret;
}

int
dict_set_int64(dict_t *this, char *key, int64_t val)
{
    data_t *data = data_from_int64(val);
    int ret = 0;

    if (!data) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_set(this, key, data);
    if (ret < 0)
        data_destroy(data);

err:
    return ret;
}

int
dict_get_uint16(dict_t *this, char *key, uint16_t *val)
{
    data_t *data = NULL;
    int ret = 0;

    if (!val) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_get_with_ref(this, key, &data);
    if (ret != 0) {
        goto err;
    }

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_UINT, key, -EINVAL);

    ret = data_to_uint16_ptr(data, val);

err:
    if (data)
        data_unref(data);
    return ret;
}

int
dict_set_uint16(dict_t *this, char *key, uint16_t val)
{
    data_t *data = data_from_uint16(val);
    int ret = 0;

    if (!data) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_set(this, key, data);
    if (ret < 0)
        data_destroy(data);

err:
    return ret;
}

int
dict_get_uint32(dict_t *this, char *key, uint32_t *val)
{
    data_t *data = NULL;
    int ret = 0;

    if (!val) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_get_with_ref(this, key, &data);
    if (ret != 0) {
        goto err;
    }

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_UINT, key, -EINVAL);

    ret = data_to_uint32_ptr(data, val);

err:
    if (data)
        data_unref(data);
    return ret;
}

int
dict_set_uint32(dict_t *this, char *key, uint32_t val)
{
    data_t *data = data_from_uint32(val);
    int ret = 0;

    if (!data) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_set(this, key, data);
    if (ret < 0)
        data_destroy(data);

err:
    return ret;
}

int
dict_get_uint64(dict_t *this, char *key, uint64_t *val)
{
    data_t *data = NULL;
    int ret = 0;

    if (!val) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_get_with_ref(this, key, &data);
    if (ret != 0) {
        goto err;
    }

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_UINT, key, -EINVAL);

    ret = data_to_uint64_ptr(data, val);

err:
    if (data)
        data_unref(data);
    return ret;
}

int
dict_set_uint64(dict_t *this, char *key, uint64_t val)
{
    data_t *data = data_from_uint64(val);
    int ret = 0;

    if (!data) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_set(this, key, data);
    if (ret < 0)
        data_destroy(data);

err:
    return ret;
}

/*
 * dict_check_flag can be used to check a one bit flag in an array of flags
 * The flag argument indicates the bit position (within the array of bits).
 * Currently limited to max of 256 flags for a key.
 * return value,
 * 1 : flag is set
 * 0 : flag is not set
 * <0: Error
 */
int
dict_check_flag(dict_t *this, char *key, int flag)
{
    data_t *data = NULL;
    int ret = -ENOENT;

    ret = dict_get_with_ref(this, key, &data);
    if (ret < 0) {
        return ret;
    }

    if (BIT_VALUE((unsigned char *)(data->data), flag))
        ret = 1;
    else
        ret = 0;

    data_unref(data);
    return ret;
}

/*
 * _dict_modify_flag can be used to set/clear a bit flag in an array of flags
 * flag: indicates the bit position. limited to max of DICT_MAX_FLAGS.
 * op: Indicates operation DICT_FLAG_SET / DICT_FLAG_CLEAR
 */
static int
_dict_modify_flag(dict_t *this, char *key, int flag, int op)
{
    data_t *data = NULL;
    int ret = 0;
    data_pair_t *pair = NULL;
    char *ptr = NULL;
    int hashval = 0;
    uint32_t hash;

    if (!this || !key) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "dict OR key (%s) is NULL", key);
        ret = -EINVAL;
        goto err;
    }

    /*
     * Using a size of 32 bytes to support max of 256
     * flags in a single key. This should be suffcient.
     */
    GF_ASSERT(flag >= 0 && flag < DICT_MAX_FLAGS);

    hash = (uint32_t)XXH64(key, strlen(key), 0);
    LOCK(&this->lock);
    {
        pair = dict_lookup_common(this, key, hash);

        if (pair) {
            data = pair->value;
            if (op == DICT_FLAG_SET)
                BIT_SET((unsigned char *)(data->data), flag);
            else
                BIT_CLEAR((unsigned char *)(data->data), flag);
        } else {
            ptr = GF_CALLOC(1, DICT_MAX_FLAGS / 8, gf_common_mt_char);
            if (!ptr) {
                gf_smsg("dict", GF_LOG_ERROR, ENOMEM, LG_MSG_NO_MEMORY,
                        "flag bit array", NULL);
                ret = -ENOMEM;
                goto err;
            }

            data = data_from_dynptr(ptr, DICT_MAX_FLAGS / 8);

            if (!data) {
                gf_smsg("dict", GF_LOG_ERROR, ENOMEM, LG_MSG_NO_MEMORY, "data",
                        NULL);
                GF_FREE(ptr);
                ret = -ENOMEM;
                goto err;
            }

            if (op == DICT_FLAG_SET)
                BIT_SET((unsigned char *)(data->data), flag);
            else
                BIT_CLEAR((unsigned char *)(data->data), flag);

            if (this->free_pair.key) { /* the free pair is in use */
                pair = mem_get0(THIS->ctx->dict_pair_pool);
                if (!pair) {
                    gf_smsg("dict", GF_LOG_ERROR, ENOMEM, LG_MSG_NO_MEMORY,
                            "dict pair", NULL);
                    ret = -ENOMEM;
                    goto err;
                }
            } else { /* use the free pair */
                pair = &this->free_pair;
            }

            pair->key = (char *)GF_MALLOC(strlen(key) + 1, gf_common_mt_char);
            if (!pair->key) {
                gf_smsg("dict", GF_LOG_ERROR, ENOMEM, LG_MSG_NO_MEMORY,
                        "dict pair", NULL);
                ret = -ENOMEM;
                goto err;
            }
            strcpy(pair->key, key);
            pair->key_hash = hash;
            pair->value = data_ref(data);
            this->totkvlen += (strlen(key) + 1 + data->len);
            hashval = hash % this->hash_size;
            pair->hash_next = this->members[hashval];
            this->members[hashval] = pair;

            pair->next = this->members_list;
            pair->prev = NULL;
            if (this->members_list)
                this->members_list->prev = pair;
            this->members_list = pair;
            this->count++;

            if (this->max_count < this->count)
                this->max_count = this->count;
        }
    }

    UNLOCK(&this->lock);
    return 0;

err:
    if (key && this)
        UNLOCK(&this->lock);

    if (pair) {
        if (pair->key) {
            GF_FREE(pair->key);
            pair->key = NULL;
        }
        if (pair != &this->free_pair) {
            mem_put(pair);
        }
    }

    if (data)
        data_destroy(data);

    gf_smsg("dict", GF_LOG_ERROR, EINVAL, LG_MSG_DICT_SET_FAILED, "key=%s", key,
            NULL);

    return ret;
}

/*
 * Todo:
 * Add below primitives as needed:
 * dict_check_flags(this, key, flag...): variadic function to check
 *                                       multiple flags at a time.
 * dict_set_flags(this, key, flag...): set multiple flags
 * dict_clear_flags(this, key, flag...): reset multiple flags
 */

int
dict_set_flag(dict_t *this, char *key, int flag)
{
    return _dict_modify_flag(this, key, flag, DICT_FLAG_SET);
}

int
dict_clear_flag(dict_t *this, char *key, int flag)
{
    return _dict_modify_flag(this, key, flag, DICT_FLAG_CLEAR);
}

int
dict_get_double(dict_t *this, char *key, double *val)
{
    data_t *data = NULL;
    int ret = 0;

    if (!val) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_get_with_ref(this, key, &data);
    if (ret != 0) {
        goto err;
    }

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_DOUBLE, key, -EINVAL);

    ret = data_to_double_ptr(data, val);

err:
    if (data)
        data_unref(data);
    return ret;
}

int
dict_set_double(dict_t *this, char *key, double val)
{
    data_t *data = data_from_double(val);
    int ret = 0;

    if (!data) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_set(this, key, data);
    if (ret < 0)
        data_destroy(data);

err:
    return ret;
}

int
dict_set_static_ptr(dict_t *this, char *key, void *ptr)
{
    data_t *data = data_from_ptr_common(ptr, _gf_true);
    int ret = 0;

    if (!data) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_set(this, key, data);
    if (ret < 0)
        data_destroy(data);

err:
    return ret;
}

int
dict_set_dynptr(dict_t *this, char *key, void *ptr, size_t len)
{
    data_t *data = data_from_dynptr(ptr, len);
    int ret = 0;

    if (!data) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_set(this, key, data);
    if (ret < 0)
        data_destroy(data);

err:
    return ret;
}

int
dict_get_ptr(dict_t *this, char *key, void **ptr)
{
    data_t *data = NULL;
    int ret = 0;

    if (!ptr) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_get_with_ref(this, key, &data);
    if (ret != 0) {
        goto err;
    }

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_PTR, key, -EINVAL);

    ret = data_to_ptr_common(data, ptr);
    if (ret != 0) {
        goto err;
    }

err:
    if (data)
        data_unref(data);

    return ret;
}

int
dict_get_ptr_and_len(dict_t *this, char *key, void **ptr, int *len)
{
    data_t *data = NULL;
    int ret = 0;

    if (!ptr) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_get_with_ref(this, key, &data);
    if (ret != 0) {
        goto err;
    }

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_PTR, key, -EINVAL);

    *len = data->len;

    ret = data_to_ptr_common(data, ptr);
    if (ret != 0) {
        goto err;
    }

err:
    if (data)
        data_unref(data);

    return ret;
}

/* Get string - with known key length */
int
dict_get_strn(dict_t *this, char *key, const int keylen, char **str)
{
    data_t *data = NULL;
    int ret = -EINVAL;

    if (!this || !key || !str) {
        goto err;
    }
    ret = dict_get_with_refn(this, key, keylen, &data);
    if (ret < 0) {
        goto err;
    }

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_STR, key, -EINVAL);

    *str = data->data;

err:
    if (data)
        data_unref(data);

    return ret;
}

int
dict_get_str(dict_t *this, char *key, char **str)
{
    data_t *data = NULL;
    int ret = -EINVAL;

    if (!str) {
        goto err;
    }
    ret = dict_get_with_ref(this, key, &data);
    if (ret < 0) {
        goto err;
    }

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_STR, key, -EINVAL);

    *str = data->data;

err:
    if (data)
        data_unref(data);

    return ret;
}

int
dict_set_str(dict_t *this, char *key, char *str)
{
    data_t *data = str_to_data(str);
    int ret = 0;

    if (!data) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_set(this, key, data);
    if (ret < 0)
        data_destroy(data);

err:
    return ret;
}

/* Set string - with known key length */
int
dict_set_strn(dict_t *this, char *key, const int keylen, char *str)
{
    data_t *data = NULL;
    int ret = 0;

    data = str_to_data(str);
    if (!data) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_setn(this, key, keylen, data);
    if (ret < 0)
        data_destroy(data);

err:
    return ret;
}

/* Set string - with known key length and known value length */
int
dict_set_nstrn(dict_t *this, char *key, const int keylen, char *str,
               const int vallen)
{
    data_t *data = strn_to_data(str, vallen);
    int ret = 0;

    if (!data) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_setn(this, key, keylen, data);
    if (ret < 0)
        data_destroy(data);

err:
    return ret;
}

int
dict_set_dynstr_with_alloc(dict_t *this, char *key, const char *str)
{
    char *alloc_str = gf_strdup(str);
    int ret = -1;

    if (!alloc_str)
        return ret;

    ret = dict_set_dynstr(this, key, alloc_str);
    if (ret == -EINVAL)
        GF_FREE(alloc_str);

    return ret;
}

int
dict_set_dynstr(dict_t *this, char *key, char *str)
{
    const int keylen = strlen(key);
    return dict_set_dynstrn(this, key, keylen, str);
}

int
dict_set_dynstrn(dict_t *this, char *key, const int keylen, char *str)
{
    data_t *data = data_from_dynstr(str);
    int ret = 0;

    if (!data) {
        ret = -EINVAL;
        goto err;
    }

    ret = dict_setn(this, key, keylen, data);
    if (ret < 0)
        data_destroy(data);

err:
    return ret;
}

/* This function is called only by the volgen for now.
   Check how else you can handle it */
int
dict_set_option(dict_t *this, char *key, char *str)
{
    data_t *data = data_from_dynstr(str);
    int ret = 0;

    if (!data) {
        ret = -EINVAL;
        goto err;
    }

    data->data_type = GF_DATA_TYPE_STR_OLD;
    ret = dict_set(this, key, data);
    if (ret < 0)
        data_destroy(data);
err:
    return ret;
}

int
dict_add_dynstr_with_alloc(dict_t *this, char *key, char *str)
{
    data_t *data = NULL;
    int ret = 0;
    char *alloc_str = gf_strdup(str);

    if (!alloc_str)
        goto out;

    data = data_from_dynstr(alloc_str);
    if (!data) {
        GF_FREE(alloc_str);
        ret = -EINVAL;
        goto out;
    }

    ret = dict_add(this, key, data);
    if (ret < 0)
        data_destroy(data);

out:
    return ret;
}

int
dict_get_bin(dict_t *this, char *key, void **bin)
{
    data_t *data = NULL;
    int ret = -EINVAL;

    if (!bin) {
        goto err;
    }

    ret = dict_get_with_ref(this, key, &data);
    if (ret < 0) {
        goto err;
    }

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_PTR, key, ret);

    *bin = data->data;

err:
    if (data)
        data_unref(data);

    return ret;
}

/********************************************************************
 *
 * dict_set_bin_common:
 *      This is the common function to set key and its value in
 *      dictionary. Flag(is_static) should be set appropriately based
 *      on the type of memory type used for value(*ptr). If flag is set
 *      to false value(*ptr) will be freed using GF_FREE() on destroy.
 *
 *******************************************************************/
static int
dict_set_bin_common(dict_t *this, char *key, void *ptr, size_t size,
                    gf_boolean_t is_static, gf_dict_data_type_t type)
{
    data_t *data = NULL;
    int ret = 0;

    if (!ptr || (size > DICT_KEY_VALUE_MAX_SIZE)) {
        ret = -EINVAL;
        goto err;
    }

    data = bin_to_data(ptr, size);
    if (!data) {
        ret = -EINVAL;
        goto err;
    }

    data->is_static = is_static;
    data->data_type = type;

    ret = dict_set(this, key, data);
    if (ret < 0) {
        /* don't free data->data, let callers handle it */
        data->data = NULL;
        data_destroy(data);
    }

err:
    return ret;
}

/********************************************************************
 *
 * dict_set_bin:
 *      Set key and its value in the dictionary. This function should
 *      be called if the value is stored in dynamic memory.
 *
 *******************************************************************/
int
dict_set_bin(dict_t *this, char *key, void *ptr, size_t size)
{
    return dict_set_bin_common(this, key, ptr, size, _gf_false,
                               GF_DATA_TYPE_PTR);
}

/********************************************************************
 *
 * dict_set_static_bin:
 *      Set key and its value in the dictionary. This function should
 *      be called if the value is stored in static memory.
 *
 *******************************************************************/
int
dict_set_static_bin(dict_t *this, char *key, void *ptr, size_t size)
{
    return dict_set_bin_common(this, key, ptr, size, _gf_true,
                               GF_DATA_TYPE_PTR);
}

/*  */
int
dict_set_gfuuid(dict_t *this, char *key, uuid_t gfid, bool is_static)
{
    return dict_set_bin_common(this, key, gfid, sizeof(uuid_t), is_static,
                               GF_DATA_TYPE_GFUUID);
}

int
dict_get_gfuuid(dict_t *this, char *key, uuid_t *gfid)
{
    data_t *data = NULL;
    int ret = -EINVAL;

    if (!gfid) {
        goto err;
    }
    ret = dict_get_with_ref(this, key, &data);
    if (ret < 0) {
        goto err;
    }

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_GFUUID, key, -EINVAL);

    memcpy(*gfid, data->data, min(data->len, sizeof(uuid_t)));

err:
    if (data)
        data_unref(data);

    return ret;
}

int
dict_set_mdata(dict_t *this, char *key, struct mdata_iatt *mdata,
               bool is_static)
{
    return dict_set_bin_common(this, key, mdata, sizeof(struct mdata_iatt),
                               is_static, GF_DATA_TYPE_MDATA);
}

int
dict_get_mdata(dict_t *this, char *key, struct mdata_iatt *mdata)
{
    data_t *data = NULL;
    int ret = -EINVAL;

    if (!mdata) {
        goto err;
    }
    ret = dict_get_with_ref(this, key, &data);
    if (ret < 0) {
        goto err;
    }

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_MDATA, key, -EINVAL);
    if (data->len < sizeof(struct mdata_iatt)) {
        gf_smsg("glusterfs", GF_LOG_ERROR, ENOBUFS, LG_MSG_UNDERSIZED_BUF,
                "key=%s", key, NULL);
        ret = -ENOBUFS;
        goto err;
    }

    memcpy(mdata, data->data, min(data->len, sizeof(struct mdata_iatt)));

err:
    if (data)
        data_unref(data);

    return ret;
}

int
dict_set_iatt(dict_t *this, char *key, struct iatt *iatt, bool is_static)
{
    return dict_set_bin_common(this, key, iatt, sizeof(struct iatt), is_static,
                               GF_DATA_TYPE_IATT);
}

int
dict_get_iatt(dict_t *this, char *key, struct iatt *iatt)
{
    data_t *data = NULL;
    int ret = -EINVAL;

    if (!iatt) {
        goto err;
    }
    ret = dict_get_with_ref(this, key, &data);
    if (ret < 0) {
        goto err;
    }

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_IATT, key, -EINVAL);

    memcpy(iatt, data->data, min(data->len, sizeof(struct iatt)));

err:
    if (data)
        data_unref(data);

    return ret;
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
dict_get_str_boolean(dict_t *this, char *key, int default_val)
{
    data_t *data = NULL;
    gf_boolean_t boo = _gf_false;
    int ret = 0;

    ret = dict_get_with_ref(this, key, &data);
    if (ret < 0) {
        if (ret == -ENOENT)
            ret = default_val;
        else
            ret = -1;
        goto err;
    }

    VALIDATE_DATA_AND_LOG(data, GF_DATA_TYPE_INT, key, -EINVAL);

    ret = gf_strn2boolean(data->data, data->len - 1, &boo);
    if (ret == -1)
        goto err;

    ret = boo;

err:
    if (data)
        data_unref(data);

    return ret;
}

int
dict_rename_key(dict_t *this, char *key, char *replace_key)
{
    data_pair_t *pair = NULL;
    int ret = -EINVAL;
    uint32_t hash;
    uint32_t replacekey_hash;
    int replacekey_len;

    /* replacing a key by itself is a NO-OP */
    if (strcmp(key, replace_key) == 0)
        return 0;

    if (!this) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "dict is NULL");
        return ret;
    }

    hash = (uint32_t)XXH64(key, strlen(key), 0);
    replacekey_len = strlen(replace_key);
    replacekey_hash = (uint32_t)XXH64(replace_key, replacekey_len, 0);

    LOCK(&this->lock);
    {
        /* no need to data_ref(pair->value), dict_set_lk() does it */
        pair = dict_lookup_common(this, key, hash);
        if (!pair)
            ret = -ENODATA;
        else
            ret = dict_set_lk(this, replace_key, replacekey_len, pair->value,
                              replacekey_hash, 1);
    }
    UNLOCK(&this->lock);

    if (!ret)
        /* only delete the key on success */
        dict_del(this, key);

    return ret;
}

/**
 * Serialization format:
 *  -------- --------  --------  ----------- -------------
 * |  count | key len | val len | key     \0| value
 *  ---------------------------------------- -------------
 *     4        4         4       <key len>   <value len>
 */

/**
 * dict_serialized_length_lk - return the length of serialized dict. This
 *                             procedure has to be called with this->lock held.
 *
 * @this  : dict to be serialized
 * @return: success: len
 *        : failure: -errno
 */

int
dict_serialized_length_lk(dict_t *this)
{
    int ret = -EINVAL;
    int count = this->count;
    const int keyhdrlen = DICT_DATA_HDR_KEY_LEN + DICT_DATA_HDR_VAL_LEN;

    if (count < 0) {
        gf_smsg("dict", GF_LOG_ERROR, EINVAL, LG_MSG_COUNT_LESS_THAN_ZERO,
                "count=%d", count, NULL);
        goto out;
    }

    ret = DICT_HDR_LEN + this->totkvlen + (count * keyhdrlen);
out:
    return ret;
}

/**
 * dict_serialize_lk - serialize a dictionary into a buffer. This procedure has
 *                     to be called with this->lock held.
 *
 * @this: dict to serialize
 * @buf:  buffer to serialize into. This must be
 *        at least dict_serialized_length (this) large
 *
 * @return: success: 0
 *          failure: -errno
 */

static int
dict_serialize_lk(dict_t *this, char *buf)
{
    int ret = -1;
    data_pair_t *pair = this->members_list;
    int32_t count = this->count;
    int32_t keylen = 0;
    int32_t netword = 0;

    if (!buf) {
        gf_smsg("dict", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG, NULL);
        goto out;
    }

    if (count < 0) {
        gf_smsg("dict", GF_LOG_ERROR, 0, LG_MSG_COUNT_LESS_THAN_ZERO,
                "count=%d", count, NULL);
        goto out;
    }

    netword = hton32(count);
    memcpy(buf, &netword, sizeof(netword));
    buf += DICT_HDR_LEN;

    while (count) {
        if (!pair) {
            gf_smsg("dict", GF_LOG_ERROR, 0, LG_MSG_PAIRS_LESS_THAN_COUNT,
                    NULL);
            goto out;
        }

        if (!pair->key) {
            gf_smsg("dict", GF_LOG_ERROR, 0, LG_MSG_NULL_PTR, NULL);
            goto out;
        }

        keylen = strlen(pair->key);
        netword = hton32(keylen);
        memcpy(buf, &netword, sizeof(netword));
        buf += DICT_DATA_HDR_KEY_LEN;

        if (!pair->value) {
            gf_smsg("dict", GF_LOG_ERROR, 0, LG_MSG_NULL_PTR, NULL);
            goto out;
        }

        netword = hton32(pair->value->len);
        memcpy(buf, &netword, sizeof(netword));
        buf += DICT_DATA_HDR_VAL_LEN;

        memcpy(buf, pair->key, keylen);
        buf += keylen;
        *buf++ = '\0';

        if (pair->value->data) {
            memcpy(buf, pair->value->data, pair->value->len);
            buf += pair->value->len;
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
dict_serialized_length(dict_t *this)
{
    int ret = -EINVAL;

    if (!this) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "dict is null!");
        goto out;
    }

    LOCK(&this->lock);
    {
        ret = dict_serialized_length_lk(this);
    }
    UNLOCK(&this->lock);

out:
    return ret;
}

/**
 * dict_serialize - serialize a dictionary into a buffer
 *
 * @this: dict to serialize
 * @buf:  buffer to serialize into. This must be
 *        at least dict_serialized_length (this) large
 *
 * @return: success: 0
 *          failure: -errno
 */

int
dict_serialize(dict_t *this, char *buf)
{
    int ret = -1;

    if (!this || !buf) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "dict is null!");
        goto out;
    }

    LOCK(&this->lock);
    {
        ret = dict_serialize_lk(this, buf);
    }
    UNLOCK(&this->lock);
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
dict_unserialize(char *orig_buf, int32_t size, dict_t **fill)
{
    char *buf = orig_buf;
    int ret = -1;
    int32_t count = 0;
    int i = 0;

    data_t *value = NULL;
    char *key = NULL;
    int32_t keylen = 0;
    int32_t vallen = 0;
    int32_t hostord = 0;

    if (!buf) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "buf is null!");
        goto out;
    }

    if (size == 0) {
        gf_msg_callingfn("dict", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "size is 0!");
        goto out;
    }

    if (!fill) {
        gf_msg_callingfn("dict", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "fill is null!");
        goto out;
    }

    if (!*fill) {
        gf_msg_callingfn("dict", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG,
                         "*fill is null!");
        goto out;
    }

    if ((buf + DICT_HDR_LEN) > (orig_buf + size)) {
        gf_msg_callingfn("dict", GF_LOG_ERROR, 0, LG_MSG_UNDERSIZED_BUF,
                         "undersized buffer "
                         "passed. available (%lu) < required (%lu)",
                         (long)(orig_buf + size), (long)(buf + DICT_HDR_LEN));
        goto out;
    }

    memcpy(&hostord, buf, sizeof(hostord));
    count = ntoh32(hostord);
    buf += DICT_HDR_LEN;

    if (count < 0) {
        gf_smsg("dict", GF_LOG_ERROR, 0, LG_MSG_COUNT_LESS_THAN_ZERO,
                "count=%d", count, NULL);
        goto out;
    }

    /* count will be set by the dict_set's below */
    (*fill)->count = 0;

    for (i = 0; i < count; i++) {
        if ((buf + DICT_DATA_HDR_KEY_LEN) > (orig_buf + size)) {
            gf_msg_callingfn("dict", GF_LOG_ERROR, 0, LG_MSG_UNDERSIZED_BUF,
                             "undersized "
                             "buffer passed. available (%lu) < "
                             "required (%lu)",
                             (long)(orig_buf + size),
                             (long)(buf + DICT_DATA_HDR_KEY_LEN));
            goto out;
        }
        memcpy(&hostord, buf, sizeof(hostord));
        keylen = ntoh32(hostord);
        buf += DICT_DATA_HDR_KEY_LEN;

        if ((buf + DICT_DATA_HDR_VAL_LEN) > (orig_buf + size)) {
            gf_msg_callingfn("dict", GF_LOG_ERROR, 0, LG_MSG_UNDERSIZED_BUF,
                             "undersized "
                             "buffer passed. available (%lu) < "
                             "required (%lu)",
                             (long)(orig_buf + size),
                             (long)(buf + DICT_DATA_HDR_VAL_LEN));
            goto out;
        }
        memcpy(&hostord, buf, sizeof(hostord));
        vallen = ntoh32(hostord);
        buf += DICT_DATA_HDR_VAL_LEN;

        if ((keylen < 0) || (vallen < 0)) {
            gf_msg_callingfn("dict", GF_LOG_ERROR, 0, LG_MSG_UNDERSIZED_BUF,
                             "undersized length passed "
                             "key:%d val:%d",
                             keylen, vallen);
            goto out;
        }
        if ((buf + keylen) > (orig_buf + size)) {
            gf_msg_callingfn("dict", GF_LOG_ERROR, 0, LG_MSG_UNDERSIZED_BUF,
                             "undersized buffer passed. "
                             "available (%lu) < required (%lu)",
                             (long)(orig_buf + size), (long)(buf + keylen));
            goto out;
        }
        key = buf;
        buf += keylen + 1; /* for '\0' */

        if ((buf + vallen) > (orig_buf + size)) {
            gf_msg_callingfn("dict", GF_LOG_ERROR, 0, LG_MSG_UNDERSIZED_BUF,
                             "undersized buffer passed. "
                             "available (%lu) < required (%lu)",
                             (long)(orig_buf + size), (long)(buf + vallen));
            goto out;
        }
        value = get_new_data();

        if (!value) {
            ret = -1;
            goto out;
        }
        value->len = vallen;
        value->data = gf_memdup(buf, vallen);
        value->data_type = GF_DATA_TYPE_STR_OLD;
        value->is_static = _gf_false;
        buf += vallen;

        ret = dict_addn(*fill, key, keylen, value);
        if (ret < 0)
            goto out;
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
dict_allocate_and_serialize(dict_t *this, char **buf, u_int *length)
{
    int ret = -EINVAL;
    ssize_t len = 0;

    if (!this || !buf) {
        gf_msg_debug("dict", 0, "dict OR buf is NULL");
        goto out;
    }

    LOCK(&this->lock);
    {
        len = dict_serialized_length_lk(this);
        if (len < 0) {
            ret = len;
            goto unlock;
        }

        *buf = GF_MALLOC(len, gf_common_mt_char);
        if (*buf == NULL) {
            ret = -ENOMEM;
            goto unlock;
        }

        ret = dict_serialize_lk(this, *buf);
        if (ret < 0) {
            GF_FREE(*buf);
            *buf = NULL;
            goto unlock;
        }

        if (length != NULL) {
            *length = len;
        }
    }
unlock:
    UNLOCK(&this->lock);
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
 *            : -errno -> failure
 */
int
dict_serialize_value_with_delim_lk(dict_t *this, char *buf, int32_t *serz_len,
                                   char delimiter)
{
    int ret = -1;
    int32_t count = this->count;
    int32_t vallen = 0;
    int32_t total_len = 0;
    data_pair_t *pair = this->members_list;

    if (!buf) {
        gf_smsg("dict", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG, NULL);
        goto out;
    }

    if (count < 0) {
        gf_smsg("dict", GF_LOG_ERROR, EINVAL, LG_MSG_INVALID_ARG, "count=%d",
                count, NULL);
        goto out;
    }

    while (count) {
        if (!pair) {
            gf_smsg("dict", GF_LOG_ERROR, 0, LG_MSG_PAIRS_LESS_THAN_COUNT,
                    NULL);
            goto out;
        }

        if (!pair->key || !pair->value) {
            gf_smsg("dict", GF_LOG_ERROR, 0, LG_MSG_KEY_OR_VALUE_NULL, NULL);
            goto out;
        }

        if (!pair->value->data) {
            gf_smsg("dict", GF_LOG_ERROR, 0, LG_MSG_NULL_VALUE_IN_DICT, NULL);
            goto out;
        }

        vallen = pair->value->len - 1;  // length includes \0
        memcpy(buf, pair->value->data, vallen);
        buf += vallen;
        *buf++ = delimiter;

        total_len += (vallen + 1);

        pair = pair->next;
        count--;
    }

    *--buf = '\0';  // remove the last delimiter
    total_len--;    // adjust the length
    ret = 0;

    if (serz_len)
        *serz_len = total_len;

out:
    return ret;
}

int
dict_serialize_value_with_delim(dict_t *this, char *buf, int32_t *serz_len,
                                char delimiter)
{
    int ret = -1;

    if (!this || !buf) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "dict is null!");
        goto out;
    }

    LOCK(&this->lock);
    {
        ret = dict_serialize_value_with_delim_lk(this, buf, serz_len,
                                                 delimiter);
    }
    UNLOCK(&this->lock);
out:
    return ret;
}

int
dict_dump_to_str(dict_t *dict, char *dump, int dumpsize, char *format)
{
    int ret = 0;
    int dumplen = 0;
    data_pair_t *trav = NULL;

    if (!dict)
        return 0;

    for (trav = dict->members_list; trav; trav = trav->next) {
        ret = snprintf(&dump[dumplen], dumpsize - dumplen, format, trav->key,
                       trav->value->data);
        if ((ret == -1) || !ret)
            return ret;

        dumplen += ret;
    }
    return 0;
}

void
dict_dump_to_log(dict_t *dict)
{
    int ret = -1;
    char *dump = NULL;
    const int dump_size = 64 * 1024;
    char *format = "(%s:%s)";

    if (!dict) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "dict is NULL");
        goto out;
    }

    dump = GF_MALLOC(dump_size, gf_common_mt_char);
    if (!dump) {
        gf_msg_callingfn("dict", GF_LOG_WARNING, ENOMEM, LG_MSG_NO_MEMORY,
                         "dump buffer is NULL");
        goto out;
    }

    ret = dict_dump_to_str(dict, dump, dump_size, format);
    if (ret) {
        gf_smsg("dict", GF_LOG_WARNING, 0, LG_MSG_FAILED_TO_LOG_DICT, NULL);
        goto out;
    }
    gf_smsg("dict", GF_LOG_INFO, 0, LG_MSG_DICT_ERROR, "dict=%p", dict,
            "dump=%s", dump, NULL);
out:
    GF_FREE(dump);

    return;
}

void
dict_dump_to_statedump(dict_t *dict, char *dict_name, char *domain)
{
    int ret = -1;
    char *dump = NULL;
    const int dump_size = 64 * 1024;
    char key[4096] = {
        0,
    };
    char *format = "\n\t%s:%s";

    if (!dict) {
        gf_msg_callingfn(domain, GF_LOG_WARNING, EINVAL, LG_MSG_INVALID_ARG,
                         "dict is NULL");
        goto out;
    }

    dump = GF_MALLOC(dump_size, gf_common_mt_char);
    if (!dump) {
        gf_msg_callingfn(domain, GF_LOG_WARNING, ENOMEM, LG_MSG_NO_MEMORY,
                         "dump buffer is NULL");
        goto out;
    }

    ret = dict_dump_to_str(dict, dump, dump_size, format);
    if (ret) {
        gf_smsg(domain, GF_LOG_WARNING, 0, LG_MSG_FAILED_TO_LOG_DICT, "name=%s",
                dict_name, NULL);
        goto out;
    }
    gf_proc_dump_build_key(key, domain, "%s", dict_name);
    gf_proc_dump_write(key, "%s", dump);

out:
    GF_FREE(dump);

    return;
}

dict_t *
dict_for_key_value(const char *name, const char *value, size_t size,
                   gf_boolean_t is_static)
{
    dict_t *xattr = dict_new();
    int ret = 0;

    if (!xattr)
        return NULL;

    if (is_static)
        ret = dict_set_static_bin(xattr, (char *)name, (void *)value, size);
    else
        ret = dict_set_bin(xattr, (char *)name, (void *)value, size);

    if (ret) {
        dict_destroy(xattr);
        xattr = NULL;
    }

    return xattr;
}

/*
 * "strings" should be NULL terminated strings array.
 */
int
dict_has_key_from_array(dict_t *dict, char **strings, gf_boolean_t *result)
{
    int i = 0;
    uint32_t hash = 0;

    if (!dict || !strings || !result)
        return -EINVAL;

    LOCK(&dict->lock);
    {
        for (i = 0; strings[i]; i++) {
            hash = (uint32_t)XXH64(strings[i], strlen(strings[i]), 0);
            if (dict_lookup_common(dict, strings[i], hash)) {
                *result = _gf_true;
                goto unlock;
            }
        }
        *result = _gf_false;
    }
unlock:
    UNLOCK(&dict->lock);
    return 0;
}
