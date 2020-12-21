/*
   Copyright (c) 2013-2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <glusterfs/common-utils.h>
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "glusterd-locks.h"
#include "glusterd-errno.h"
#include <glusterfs/run.h>
#include <glusterfs/syscall.h>
#include "glusterd-messages.h"

#include <signal.h>

#define GF_MAX_LOCKING_ENTITIES 3

/* Valid entities that the mgmt_v3 lock can hold locks upon    *
 * To add newer entities to be locked, we can just add more    *
 * entries to this table along with the type and default value */
glusterd_valid_entities valid_types[] = {
    {"vol", _gf_true},
    {"snap", _gf_false},
    {"global", _gf_false},
    {NULL},
};

/* Checks if the lock request is for a valid entity */
static gf_boolean_t
glusterd_mgmt_v3_is_type_valid(char *type)
{
    int i = 0;

    GF_ASSERT(type);

    for (i = 0; valid_types[i].type; i++) {
        if (!strcmp(type, valid_types[i].type)) {
            return _gf_true;
        }
    }

    return _gf_false;
}

/* Initialize the global mgmt_v3 lock list(dict) when
 * glusterd is spawned */
int32_t
glusterd_mgmt_v3_lock_init()
{
    int32_t ret = -1;
    glusterd_conf_t *priv = NULL;

    priv = THIS->private;
    GF_ASSERT(priv);

    priv->mgmt_v3_lock = dict_new();
    if (!priv->mgmt_v3_lock)
        goto out;

    ret = 0;
out:
    return ret;
}

/* Destroy the global mgmt_v3 lock list(dict) when
 * glusterd cleanup is performed */
void
glusterd_mgmt_v3_lock_fini()
{
    glusterd_conf_t *priv = NULL;

    priv = THIS->private;
    GF_ASSERT(priv);

    if (priv->mgmt_v3_lock)
        dict_unref(priv->mgmt_v3_lock);
}

/* Initialize the global mgmt_v3_timer lock list(dict) when
 * glusterd is spawned */
int32_t
glusterd_mgmt_v3_lock_timer_init()
{
    int32_t ret = -1;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;

    priv = this->private;
    GF_VALIDATE_OR_GOTO(this->name, priv, out);

    priv->mgmt_v3_lock_timer = dict_new();
    if (!priv->mgmt_v3_lock_timer)
        goto out;

    ret = 0;
out:
    return ret;
}

/* Destroy the global mgmt_v3_timer lock list(dict) when
 * glusterd cleanup is performed */
void
glusterd_mgmt_v3_lock_timer_fini()
{
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;

    priv = this->private;
    GF_VALIDATE_OR_GOTO(this->name, priv, out);

    if (priv->mgmt_v3_lock_timer)
        dict_unref(priv->mgmt_v3_lock_timer);
out:
    return;
}

static int32_t
glusterd_get_mgmt_v3_lock_owner(char *key, uuid_t *uuid)
{
    int32_t ret = -1;
    glusterd_mgmt_v3_lock_obj *lock_obj = NULL;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = THIS;

    priv = this->private;
    GF_ASSERT(priv);

    if (!key || !uuid) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "key or uuid is null.");
        ret = -1;
        goto out;
    }

    ret = dict_get_bin(priv->mgmt_v3_lock, key, (void **)&lock_obj);
    if (!ret)
        gf_uuid_copy(*uuid, lock_obj->lock_owner);

    ret = 0;
out:
    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

/* This function is called with the locked_count and type, to   *
 * release all the acquired locks. */
static int32_t
glusterd_release_multiple_locks_per_entity(dict_t *dict, uuid_t uuid,
                                           int32_t locked_count, char *type)
{
    char name_buf[PATH_MAX] = "";
    char *name = NULL;
    int32_t i = -1;
    int32_t op_ret = 0;
    int32_t ret = -1;
    xlator_t *this = THIS;

    GF_ASSERT(dict);
    GF_ASSERT(type);

    if (locked_count == 0) {
        gf_msg_debug(this->name, 0, "No %s locked as part of this transaction",
                     type);
        goto out;
    }

    /* Release all the locks held */
    for (i = 0; i < locked_count; i++) {
        ret = snprintf(name_buf, sizeof(name_buf), "%sname%d", type, i + 1);

        /* Looking for volname1, volname2 or snapname1, *
         * as key in the dict snapname2 */
        ret = dict_get_strn(dict, name_buf, ret, &name);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to get %s locked_count = %d", name_buf,
                   locked_count);
            op_ret = ret;
            continue;
        }

        ret = glusterd_mgmt_v3_unlock(name, uuid, type);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MGMTV3_UNLOCK_FAIL,
                   "Failed to release lock for %s.", name);
            op_ret = ret;
        }
    }

out:
    gf_msg_trace(this->name, 0, "Returning %d", op_ret);
    return op_ret;
}

/* Given the count and type of the entity this function acquires     *
 * locks on multiple elements of the same entity. For example:       *
 * If type is "vol" this function tries to acquire locks on multiple *
 * volumes */
static int32_t
glusterd_acquire_multiple_locks_per_entity(dict_t *dict, uuid_t uuid,
                                           uint32_t *op_errno, int32_t count,
                                           char *type)
{
    char name_buf[PATH_MAX] = "";
    char *name = NULL;
    int32_t i = -1;
    int32_t ret = -1;
    int32_t locked_count = 0;
    xlator_t *this = THIS;

    GF_ASSERT(dict);
    GF_ASSERT(type);

    /* Locking one element after other */
    for (i = 0; i < count; i++) {
        ret = snprintf(name_buf, sizeof(name_buf), "%sname%d", type, i + 1);

        /* Looking for volname1, volname2 or snapname1, *
         * as key in the dict snapname2 */
        ret = dict_get_strn(dict, name_buf, ret, &name);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to get %s count = %d", name_buf, count);
            break;
        }

        ret = glusterd_mgmt_v3_lock(name, uuid, op_errno, type);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MGMTV3_LOCK_GET_FAIL,
                   "Failed to acquire lock for %s %s "
                   "on behalf of %s. Reversing "
                   "this transaction",
                   type, name, uuid_utoa(uuid));
            break;
        }
        locked_count++;
    }

    if (count == locked_count) {
        /* If all locking ops went successfully, return as success */
        ret = 0;
        goto out;
    }

    /* If we failed to lock one element, unlock others and return failure */
    ret = glusterd_release_multiple_locks_per_entity(dict, uuid, locked_count,
                                                     type);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MULTIPLE_LOCK_RELEASE_FAIL,
               "Failed to release multiple %s locks", type);
    }
    ret = -1;
out:
    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

/* Given the type of entity, this function figures out if it should unlock a   *
 * single element of multiple elements of the said entity. For example:        *
 * if the type is "vol", this function will accordingly unlock a single volume *
 * or multiple volumes */
static int32_t
glusterd_mgmt_v3_unlock_entity(dict_t *dict, uuid_t uuid, char *type,
                               gf_boolean_t default_value)
{
    char name_buf[PATH_MAX] = "";
    char *name = NULL;
    int32_t count = -1;
    int32_t ret = -1;
    gf_boolean_t hold_locks = _gf_false;
    xlator_t *this = THIS;

    GF_ASSERT(dict);
    GF_ASSERT(type);

    snprintf(name_buf, sizeof(name_buf), "hold_%s_locks", type);
    hold_locks = dict_get_str_boolean(dict, name_buf, default_value);

    if (hold_locks == _gf_false) {
        /* Locks were not held for this particular entity *
         * Hence nothing to release */
        ret = 0;
        goto out;
    }

    /* Looking for volcount or snapcount in the dict */
    ret = snprintf(name_buf, sizeof(name_buf), "%scount", type);
    ret = dict_get_int32n(dict, name_buf, ret, &count);
    if (ret) {
        /* count is not present. Only one *
         * element name needs to be unlocked */
        ret = snprintf(name_buf, sizeof(name_buf), "%sname", type);
        ret = dict_get_strn(dict, name_buf, ret, &name);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to fetch %sname", type);
            goto out;
        }

        ret = glusterd_mgmt_v3_unlock(name, uuid, type);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MGMTV3_UNLOCK_FAIL,
                   "Failed to release lock for %s %s "
                   "on behalf of %s.",
                   type, name, uuid_utoa(uuid));
            goto out;
        }
    } else {
        /* Unlocking one element name after another */
        ret = glusterd_release_multiple_locks_per_entity(dict, uuid, count,
                                                         type);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   GD_MSG_MULTIPLE_LOCK_RELEASE_FAIL,
                   "Failed to release all %s locks", type);
            goto out;
        }
    }

    ret = 0;
out:
    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

/* Given the type of entity, this function figures out if it should lock a   *
 * single element or multiple elements of the said entity. For example:      *
 * if the type is "vol", this function will accordingly lock a single volume *
 * or multiple volumes */
static int32_t
glusterd_mgmt_v3_lock_entity(dict_t *dict, uuid_t uuid, uint32_t *op_errno,
                             char *type, gf_boolean_t default_value)
{
    char name_buf[PATH_MAX] = "";
    char *name = NULL;
    int32_t count = -1;
    int32_t ret = -1;
    gf_boolean_t hold_locks = _gf_false;
    xlator_t *this = THIS;

    GF_ASSERT(dict);
    GF_ASSERT(type);

    snprintf(name_buf, sizeof(name_buf), "hold_%s_locks", type);
    hold_locks = dict_get_str_boolean(dict, name_buf, default_value);

    if (hold_locks == _gf_false) {
        /* Not holding locks for this particular entity */
        ret = 0;
        goto out;
    }

    /* Looking for volcount or snapcount in the dict */
    ret = snprintf(name_buf, sizeof(name_buf), "%scount", type);
    ret = dict_get_int32n(dict, name_buf, ret, &count);
    if (ret) {
        /* count is not present. Only one *
         * element name needs to be locked */
        ret = snprintf(name_buf, sizeof(name_buf), "%sname", type);
        ret = dict_get_strn(dict, name_buf, ret, &name);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to fetch %sname", type);
            goto out;
        }

        ret = glusterd_mgmt_v3_lock(name, uuid, op_errno, type);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MGMTV3_LOCK_GET_FAIL,
                   "Failed to acquire lock for %s %s "
                   "on behalf of %s.",
                   type, name, uuid_utoa(uuid));
            goto out;
        }
    } else {
        /* Locking one element name after another */
        ret = glusterd_acquire_multiple_locks_per_entity(dict, uuid, op_errno,
                                                         count, type);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   GD_MSG_MULTIPLE_LOCK_ACQUIRE_FAIL,
                   "Failed to acquire all %s locks", type);
            goto out;
        }
    }

    ret = 0;
out:
    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

/* Try to release locks of multiple entities like *
 * volume, snaps etc. */
int32_t
glusterd_multiple_mgmt_v3_unlock(dict_t *dict, uuid_t uuid)
{
    int32_t i = -1;
    int32_t ret = -1;
    int32_t op_ret = 0;
    xlator_t *this = THIS;

    if (!dict) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_EMPTY, "dict is null.");
        ret = -1;
        goto out;
    }

    for (i = 0; valid_types[i].type; i++) {
        ret = glusterd_mgmt_v3_unlock_entity(dict, uuid, valid_types[i].type,
                                             valid_types[i].default_value);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   GD_MSG_MULTIPLE_LOCK_RELEASE_FAIL, "Unable to unlock all %s",
                   valid_types[i].type);
            op_ret = ret;
        }
    }

    ret = op_ret;
out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

/* Try to acquire locks on multiple entities like *
 * volume, snaps etc. */
int32_t
glusterd_multiple_mgmt_v3_lock(dict_t *dict, uuid_t uuid, uint32_t *op_errno)
{
    int32_t i = -1;
    int32_t ret = -1;
    int32_t locked_count = 0;
    xlator_t *this = THIS;

    if (!dict) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_EMPTY, "dict is null.");
        ret = -1;
        goto out;
    }

    /* Locking one entity after other */
    for (i = 0; valid_types[i].type; i++) {
        ret = glusterd_mgmt_v3_lock_entity(dict, uuid, op_errno,
                                           valid_types[i].type,
                                           valid_types[i].default_value);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   GD_MSG_MULTIPLE_LOCK_ACQUIRE_FAIL, "Unable to lock all %s",
                   valid_types[i].type);
            break;
        }
        locked_count++;
    }

    if (locked_count == GF_MAX_LOCKING_ENTITIES) {
        /* If all locking ops went successfully, return as success */
        ret = 0;
        goto out;
    }

    /* If we failed to lock one entity, unlock others and return failure */
    for (i = 0; i < locked_count; i++) {
        ret = glusterd_mgmt_v3_unlock_entity(dict, uuid, valid_types[i].type,
                                             valid_types[i].default_value);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   GD_MSG_MULTIPLE_LOCK_RELEASE_FAIL, "Unable to unlock all %s",
                   valid_types[i].type);
        }
    }
    ret = -1;
out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int32_t
glusterd_mgmt_v3_lock(const char *name, uuid_t uuid, uint32_t *op_errno,
                      char *type)
{
    char key[PATH_MAX] = "";
    int32_t ret = -1;
    glusterd_mgmt_v3_lock_obj *lock_obj = NULL;
    glusterd_mgmt_v3_lock_timer *mgmt_lock_timer = NULL;
    glusterd_conf_t *priv = NULL;
    gf_boolean_t is_valid = _gf_true;
    uuid_t owner = {0};
    xlator_t *this = THIS;
    char *bt = NULL;
    struct timespec delay = {0};
    char *key_dup = NULL;
    glusterfs_ctx_t *mgmt_lock_timer_ctx = NULL;
    xlator_t *mgmt_lock_timer_xl = NULL;

    priv = this->private;
    GF_ASSERT(priv);

    if (!name || !type) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "name or type is null.");
        ret = -1;
        goto out;
    }

    is_valid = glusterd_mgmt_v3_is_type_valid(type);
    if (is_valid != _gf_true) {
        gf_msg_callingfn(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
                         "Invalid entity. Cannot perform locking "
                         "operation on %s types",
                         type);
        ret = -1;
        goto out;
    }

    ret = snprintf(key, sizeof(key), "%s_%s", name, type);
    if (ret != strlen(name) + 1 + strlen(type)) {
        ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_CREATE_KEY_FAIL,
               "Unable to create key");
        goto out;
    }

    gf_msg_debug(this->name, 0, "Trying to acquire lock of %s for %s", key,
                 uuid_utoa(uuid));

    ret = glusterd_get_mgmt_v3_lock_owner(key, &owner);
    if (ret) {
        gf_msg_debug(this->name, 0, "Unable to get mgmt_v3 lock owner");
        goto out;
    }

    /* If the lock has already been held for the given volume
     * we fail */
    if (!gf_uuid_is_null(owner)) {
        gf_msg_callingfn(this->name, GF_LOG_WARNING, 0,
                         GD_MSG_LOCK_ALREADY_HELD, "Lock for %s held by %s",
                         name, uuid_utoa(owner));
        ret = -1;
        *op_errno = EG_ANOTRANS;
        goto out;
    }

    lock_obj = GF_MALLOC(sizeof(glusterd_mgmt_v3_lock_obj),
                         gf_common_mt_mgmt_v3_lock_obj_t);
    if (!lock_obj) {
        ret = -1;
        goto out;
    }

    gf_uuid_copy(lock_obj->lock_owner, uuid);

    ret = dict_set_bin(priv->mgmt_v3_lock, key, lock_obj,
                       sizeof(glusterd_mgmt_v3_lock_obj));
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to set lock owner in mgmt_v3 lock");
        GF_FREE(lock_obj);
        goto out;
    }

    mgmt_lock_timer = GF_CALLOC(1, sizeof(glusterd_mgmt_v3_lock_timer),
                                gf_common_mt_mgmt_v3_lock_timer_t);

    if (!mgmt_lock_timer) {
        ret = -1;
        goto out;
    }

    mgmt_lock_timer->xl = THIS;
    /*changing to default timeout value*/
    priv->mgmt_v3_lock_timeout = GF_LOCK_TIMER;

    ret = -1;
    mgmt_lock_timer_xl = mgmt_lock_timer->xl;
    if (!mgmt_lock_timer_xl) {
        GF_FREE(mgmt_lock_timer);
        goto out;
    }

    mgmt_lock_timer_ctx = mgmt_lock_timer_xl->ctx;
    if (!mgmt_lock_timer_ctx) {
        GF_FREE(mgmt_lock_timer);
        goto out;
    }

    key_dup = gf_strdup(key);
    delay.tv_sec = priv->mgmt_v3_lock_timeout;
    delay.tv_nsec = 0;

    mgmt_lock_timer->timer = gf_timer_call_after(
        mgmt_lock_timer_ctx, delay, gd_mgmt_v3_unlock_timer_cbk, key_dup);

    ret = dict_set_bin(priv->mgmt_v3_lock_timer, key, mgmt_lock_timer,
                       sizeof(glusterd_mgmt_v3_lock_timer));
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to set timer in mgmt_v3 lock");
        GF_FREE(key_dup);
        GF_FREE(mgmt_lock_timer);
        goto out;
    }

    /* Saving the backtrace into the pre-allocated buffer, ctx->btbuf*/
    if ((bt = gf_backtrace_save(NULL))) {
        snprintf(key, sizeof(key), "debug.last-success-bt-%s", key_dup);
        ret = dict_set_dynstr_with_alloc(priv->mgmt_v3_lock, key, bt);
        if (ret)
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to save "
                   "the back trace for lock %s granted to %s",
                   key_dup, uuid_utoa(uuid));
        ret = 0;
    }

    gf_msg_debug(this->name, 0, "Lock for %s successfully held by %s", key_dup,
                 uuid_utoa(uuid));

    ret = 0;
out:
    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

/*
 * This call back will ensure to unlock the lock_obj, in case we hit a situation
 * where unlocking failed and stale lock exist*/
void
gd_mgmt_v3_unlock_timer_cbk(void *data)
{
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    glusterd_mgmt_v3_lock_timer *mgmt_lock_timer = NULL;
    char *key = NULL;
    int keylen;
    char bt_key[PATH_MAX] = "";
    int bt_key_len = 0;
    int32_t ret = -1;
    glusterfs_ctx_t *mgmt_lock_timer_ctx = NULL;
    xlator_t *mgmt_lock_timer_xl = NULL;
    gf_timer_t *timer = NULL;

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    GF_ASSERT(NULL != data);
    key = (char *)data;

    keylen = strlen(key);
    dict_deln(conf->mgmt_v3_lock, key, keylen);

    bt_key_len = snprintf(bt_key, PATH_MAX, "debug.last-success-bt-%s", key);
    if (bt_key_len != SLEN("debug.last-success-bt-") + keylen) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_CREATE_KEY_FAIL,
               "Unable to create backtrace "
               "key");
        goto out;
    }

    dict_deln(conf->mgmt_v3_lock, bt_key, bt_key_len);

    ret = dict_get_bin(conf->mgmt_v3_lock_timer, key,
                       (void **)&mgmt_lock_timer);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to get lock owner in mgmt_v3 lock");
    }

out:
    if (mgmt_lock_timer && mgmt_lock_timer->timer) {
        mgmt_lock_timer_xl = mgmt_lock_timer->xl;
        GF_VALIDATE_OR_GOTO(this->name, mgmt_lock_timer_xl, ret_function);

        mgmt_lock_timer_ctx = mgmt_lock_timer_xl->ctx;
        GF_VALIDATE_OR_GOTO(this->name, mgmt_lock_timer_ctx, ret_function);

        timer = mgmt_lock_timer->timer;
        GF_FREE(timer->data);
        gf_timer_call_cancel(mgmt_lock_timer_ctx, mgmt_lock_timer->timer);
        dict_deln(conf->mgmt_v3_lock_timer, bt_key, bt_key_len);
        mgmt_lock_timer->timer = NULL;
        gf_log(this->name, GF_LOG_INFO,
               "unlock timer is cancelled for volume_type"
               " %s",
               key);
    }

ret_function:

    return;
}

int32_t
glusterd_mgmt_v3_unlock(const char *name, uuid_t uuid, char *type)
{
    char key[PATH_MAX] = "";
    char key_dup[PATH_MAX] = "";
    int keylen;
    int32_t ret = -1;
    gf_boolean_t is_valid = _gf_true;
    glusterd_conf_t *priv = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_mgmt_v3_lock_timer *mgmt_lock_timer = NULL;
    uuid_t owner = {0};
    xlator_t *this = THIS;
    glusterfs_ctx_t *mgmt_lock_timer_ctx = NULL;
    xlator_t *mgmt_lock_timer_xl = NULL;
    gf_timer_t *timer = NULL;

    priv = this->private;
    GF_ASSERT(priv);

    if (!name || !type) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "name is null.");
        ret = -1;
        goto out;
    }

    is_valid = glusterd_mgmt_v3_is_type_valid(type);
    if (is_valid != _gf_true) {
        gf_msg_callingfn(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
                         "Invalid entity. Cannot perform unlocking "
                         "operation on %s types",
                         type);
        ret = -1;
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s_%s", name, type);
    if (keylen != strlen(name) + 1 + strlen(type)) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_CREATE_KEY_FAIL,
               "Unable to create key");
        ret = -1;
        goto out;
    }

    gf_msg_debug(this->name, 0, "Trying to release lock of %s %s for %s as %s",
                 type, name, uuid_utoa(uuid), key);

    ret = glusterd_get_mgmt_v3_lock_owner(key, &owner);
    if (ret) {
        gf_msg_debug(this->name, 0, "Unable to get mgmt_v3 lock owner");
        goto out;
    }

    if (gf_uuid_is_null(owner)) {
        gf_msg_callingfn(this->name, GF_LOG_WARNING, 0, GD_MSG_LOCK_NOT_HELD,
                         "Lock for %s %s not held", type, name);
        ret = -1;
        goto out;
    }

    ret = gf_uuid_compare(uuid, owner);
    if (ret) {
        gf_msg_callingfn(this->name, GF_LOG_WARNING, 0,
                         GD_MSG_LOCK_OWNER_MISMATCH,
                         "Lock owner mismatch. "
                         "Lock for %s %s held by %s",
                         type, name, uuid_utoa(owner));
        goto out;
    }

    /* Removing the mgmt_v3 lock from the global list */
    dict_deln(priv->mgmt_v3_lock, key, keylen);

    ret = dict_get_bin(priv->mgmt_v3_lock_timer, key,
                       (void **)&mgmt_lock_timer);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to get mgmt lock key in mgmt_v3 lock");
        goto out;
    }

    (void)snprintf(key_dup, sizeof(key_dup), "%s", key);

    /* Remove the backtrace key as well */
    ret = snprintf(key, sizeof(key), "debug.last-success-bt-%s", key_dup);
    if (ret != SLEN("debug.last-success-bt-") + keylen) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_CREATE_KEY_FAIL,
               "Unable to create backtrace "
               "key");
        ret = -1;
        goto out;
    }
    dict_deln(priv->mgmt_v3_lock, key, ret);

    gf_msg_debug(this->name, 0, "Lock for %s %s successfully released", type,
                 name);

    /* Release owner reference which was held during lock */
    if (mgmt_lock_timer && mgmt_lock_timer->timer) {
        ret = -1;
        mgmt_lock_timer_xl = mgmt_lock_timer->xl;
        GF_VALIDATE_OR_GOTO(this->name, mgmt_lock_timer_xl, out);

        mgmt_lock_timer_ctx = mgmt_lock_timer_xl->ctx;
        GF_VALIDATE_OR_GOTO(this->name, mgmt_lock_timer_ctx, out);
        ret = 0;

        timer = mgmt_lock_timer->timer;
        GF_FREE(timer->data);
        gf_timer_call_cancel(mgmt_lock_timer_ctx, mgmt_lock_timer->timer);
        dict_deln(priv->mgmt_v3_lock_timer, key_dup, keylen);
    }
    ret = glusterd_volinfo_find(name, &volinfo);
    if (volinfo && volinfo->stage_deleted) {
        /* this indicates a volume still exists and the volume delete
         * operation has failed in some of the phases, need to ensure
         * stage_deleted flag is set back to false
         */
        volinfo->stage_deleted = _gf_false;
        gf_log(this->name, GF_LOG_INFO,
               "Volume %s still exist, setting "
               "stage deleted flag to false for the volume",
               volinfo->volname);
    }
    ret = 0;
out:

    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}
