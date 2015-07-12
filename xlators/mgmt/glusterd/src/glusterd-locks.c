/*
   Copyright (c) 2013-2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "common-utils.h"
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "glusterd-locks.h"
#include "glusterd-errno.h"
#include "run.h"
#include "syscall.h"
#include "glusterd-messages.h"

#include <signal.h>

#define GF_MAX_LOCKING_ENTITIES 3

/* Valid entities that the mgmt_v3 lock can hold locks upon    *
 * To add newer entities to be locked, we can just add more    *
 * entries to this table along with the type and default value */
glusterd_valid_entities   valid_types[] = {
        { "vol",  _gf_true  },
        { "snap", _gf_false },
        { "global", _gf_false},
        { NULL              },
};

/* Checks if the lock request is for a valid entity */
gf_boolean_t
glusterd_mgmt_v3_is_type_valid (char *type)
{
        int32_t         i   = 0;
        gf_boolean_t    ret = _gf_false;

        GF_ASSERT (type);

        for (i = 0; valid_types[i].type; i++) {
                if (!strcmp (type, valid_types[i].type)) {
                        ret = _gf_true;
                        break;
                }
        }

        return ret;
}

/* Initialize the global mgmt_v3 lock list(dict) when
 * glusterd is spawned */
int32_t
glusterd_mgmt_v3_lock_init ()
{
        int32_t             ret = -1;
        xlator_t           *this   = NULL;
        glusterd_conf_t    *priv   = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        priv->mgmt_v3_lock = dict_new ();
        if (!priv->mgmt_v3_lock)
                goto out;

        ret = 0;
out:
        return ret;
}

/* Destroy the global mgmt_v3 lock list(dict) when
 * glusterd cleanup is performed */
void
glusterd_mgmt_v3_lock_fini ()
{
        xlator_t           *this   = NULL;
        glusterd_conf_t    *priv   = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        if (priv->mgmt_v3_lock)
                dict_unref (priv->mgmt_v3_lock);
}

int32_t
glusterd_get_mgmt_v3_lock_owner (char *key, uuid_t *uuid)
{
        int32_t                         ret      = -1;
        glusterd_mgmt_v3_lock_obj      *lock_obj = NULL;
        glusterd_conf_t                *priv     = NULL;
        uuid_t                          no_owner = {0,};
        xlator_t                       *this     = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        if (!key || !uuid) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INVALID_ENTRY, "key or uuid is null.");
                ret = -1;
                goto out;
        }

        ret = dict_get_bin (priv->mgmt_v3_lock, key, (void **) &lock_obj);
        if (!ret)
                gf_uuid_copy (*uuid, lock_obj->lock_owner);
        else
                gf_uuid_copy (*uuid, no_owner);

        ret = 0;
out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

/* This function is called with the locked_count and type, to   *
 * release all the acquired locks. */
static int32_t
glusterd_release_multiple_locks_per_entity (dict_t *dict, uuid_t uuid,
                                            int32_t locked_count,
                                            char *type)
{
        char           name_buf[PATH_MAX]    = "";
        char          *name                  = NULL;
        int32_t        i                     = -1;
        int32_t        op_ret                = 0;
        int32_t        ret                   = -1;
        xlator_t      *this                  = NULL;

        this = THIS;
        GF_ASSERT(this);
        GF_ASSERT (dict);
        GF_ASSERT (type);

        if (locked_count == 0) {
                gf_msg_debug (this->name, 0,
                        "No %s locked as part of this transaction",
                        type);
                goto out;
        }

        /* Release all the locks held */
        for (i = 0; i < locked_count; i++) {
                snprintf (name_buf, sizeof(name_buf),
                          "%sname%d", type, i+1);

                /* Looking for volname1, volname2 or snapname1, *
                 * as key in the dict snapname2 */
                ret = dict_get_str (dict, name_buf, &name);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Unable to get %s locked_count = %d",
                                name_buf, locked_count);
                        op_ret = ret;
                        continue;
                }

                ret = glusterd_mgmt_v3_unlock (name, uuid, type);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MGMTV3_UNLOCK_FAIL,
                                "Failed to release lock for %s.",
                                name);
                        op_ret = ret;
                }
        }

out:
        gf_msg_trace (this->name, 0, "Returning %d", op_ret);
        return op_ret;
}

/* Given the count and type of the entity this function acquires     *
 * locks on multiple elements of the same entity. For example:       *
 * If type is "vol" this function tries to acquire locks on multiple *
 * volumes */
static int32_t
glusterd_acquire_multiple_locks_per_entity (dict_t *dict, uuid_t uuid,
                                            uint32_t *op_errno,
                                            int32_t count, char *type)
{
        char           name_buf[PATH_MAX]    = "";
        char          *name                  = NULL;
        int32_t        i                     = -1;
        int32_t        ret                   = -1;
        int32_t        locked_count          = 0;
        xlator_t      *this                  = NULL;

        this = THIS;
        GF_ASSERT(this);
        GF_ASSERT (dict);
        GF_ASSERT (type);

        /* Locking one element after other */
        for (i = 0; i < count; i++) {
                snprintf (name_buf, sizeof(name_buf),
                          "%sname%d", type, i+1);

                /* Looking for volname1, volname2 or snapname1, *
                 * as key in the dict snapname2 */
                ret = dict_get_str (dict, name_buf, &name);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Unable to get %s count = %d",
                                name_buf, count);
                        break;
                }

                ret = glusterd_mgmt_v3_lock (name, uuid, op_errno, type);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MGMTV3_LOCK_GET_FAIL,
                                "Failed to acquire lock for %s %s "
                                "on behalf of %s. Reversing "
                                "this transaction", type, name,
                                uuid_utoa(uuid));
                        break;
                }
                locked_count++;
        }

        if (count == locked_count) {
                /* If all locking ops went successfuly, return as success */
                ret = 0;
                goto out;
        }

        /* If we failed to lock one element, unlock others and return failure */
        ret = glusterd_release_multiple_locks_per_entity (dict, uuid,
                                                          locked_count,
                                                          type);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MULTIPLE_LOCK_RELEASE_FAIL,
                        "Failed to release multiple %s locks",
                        type);
        }
        ret = -1;
out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

/* Given the type of entity, this function figures out if it should unlock a   *
 * single element of multiple elements of the said entity. For example:        *
 * if the type is "vol", this function will accordingly unlock a single volume *
 * or multiple volumes */
static int32_t
glusterd_mgmt_v3_unlock_entity (dict_t *dict, uuid_t uuid, char *type,
                                gf_boolean_t default_value)
{
        char           name_buf[PATH_MAX]    = "";
        char          *name                  = NULL;
        int32_t        count                 = -1;
        int32_t        ret                   = -1;
        gf_boolean_t   hold_locks            = _gf_false;
        xlator_t      *this                  = NULL;

        this = THIS;
        GF_ASSERT(this);
        GF_ASSERT (dict);
        GF_ASSERT (type);

        snprintf (name_buf, sizeof(name_buf), "hold_%s_locks", type);
        hold_locks = dict_get_str_boolean (dict, name_buf, default_value);

        if (hold_locks == _gf_false) {
                /* Locks were not held for this particular entity *
                 * Hence nothing to release */
                ret = 0;
                goto out;
        }

        /* Looking for volcount or snapcount in the dict */
        snprintf (name_buf, sizeof(name_buf), "%scount", type);
        ret = dict_get_int32 (dict, name_buf, &count);
        if (ret) {
                /* count is not present. Only one *
                 * element name needs to be unlocked */
                snprintf (name_buf, sizeof(name_buf), "%sname",
                          type);
                ret = dict_get_str (dict, name_buf, &name);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Unable to fetch %sname", type);
                        goto out;
                }

                ret = glusterd_mgmt_v3_unlock (name, uuid, type);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MGMTV3_UNLOCK_FAIL,
                                "Failed to release lock for %s %s "
                                "on behalf of %s.", type, name,
                                uuid_utoa(uuid));
                        goto out;
                }
        } else {
                /* Unlocking one element name after another */
                ret = glusterd_release_multiple_locks_per_entity (dict,
                                                                  uuid,
                                                                  count,
                                                                  type);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MULTIPLE_LOCK_RELEASE_FAIL,
                                "Failed to release all %s locks", type);
                        goto out;
                }
        }

        ret = 0;
out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

/* Given the type of entity, this function figures out if it should lock a   *
 * single element or multiple elements of the said entity. For example:      *
 * if the type is "vol", this function will accordingly lock a single volume *
 * or multiple volumes */
static int32_t
glusterd_mgmt_v3_lock_entity (dict_t *dict, uuid_t uuid, uint32_t *op_errno,
                              char *type, gf_boolean_t default_value)
{
        char           name_buf[PATH_MAX]    = "";
        char          *name                  = NULL;
        int32_t        count                 = -1;
        int32_t        ret                   = -1;
        gf_boolean_t   hold_locks            = _gf_false;
        xlator_t      *this                  = NULL;

        this = THIS;
        GF_ASSERT(this);
        GF_ASSERT (dict);
        GF_ASSERT (type);

        snprintf (name_buf, sizeof(name_buf), "hold_%s_locks", type);
        hold_locks = dict_get_str_boolean (dict, name_buf, default_value);

        if (hold_locks == _gf_false) {
                /* Not holding locks for this particular entity */
                ret = 0;
                goto out;
        }

        /* Looking for volcount or snapcount in the dict */
        snprintf (name_buf, sizeof(name_buf), "%scount", type);
        ret = dict_get_int32 (dict, name_buf, &count);
        if (ret) {
                /* count is not present. Only one *
                 * element name needs to be locked */
                snprintf (name_buf, sizeof(name_buf), "%sname",
                          type);
                ret = dict_get_str (dict, name_buf, &name);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Unable to fetch %sname", type);
                        goto out;
                }

                ret = glusterd_mgmt_v3_lock (name, uuid, op_errno, type);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MGMTV3_LOCK_GET_FAIL,
                                "Failed to acquire lock for %s %s "
                                "on behalf of %s.", type, name,
                                uuid_utoa(uuid));
                        goto out;
                }
        } else {
                /* Locking one element name after another */
                ret = glusterd_acquire_multiple_locks_per_entity (dict,
                                                                  uuid,
                                                                  op_errno,
                                                                  count,
                                                                  type);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MULTIPLE_LOCK_ACQUIRE_FAIL,
                                "Failed to acquire all %s locks", type);
                        goto out;
                }
        }

        ret = 0;
out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

/* Try to release locks of multiple entities like *
 * volume, snaps etc. */
int32_t
glusterd_multiple_mgmt_v3_unlock (dict_t *dict, uuid_t uuid)
{
        int32_t        i                     = -1;
        int32_t        ret                   = -1;
        int32_t        op_ret                = 0;
        xlator_t      *this                  = NULL;

        this = THIS;
        GF_ASSERT(this);

        if (!dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_EMPTY, "dict is null.");
                ret = -1;
                goto out;
        }

        for (i = 0; valid_types[i].type; i++) {
                ret = glusterd_mgmt_v3_unlock_entity
                                            (dict, uuid,
                                             valid_types[i].type,
                                             valid_types[i].default_value);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MULTIPLE_LOCK_RELEASE_FAIL,
                                "Unable to unlock all %s",
                                valid_types[i].type);
                        op_ret = ret;
                }
        }

        ret = op_ret;
out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

/* Try to acquire locks on multiple entities like *
 * volume, snaps etc. */
int32_t
glusterd_multiple_mgmt_v3_lock (dict_t *dict, uuid_t uuid, uint32_t *op_errno)
{
        int32_t        i                     = -1;
        int32_t        ret                   = -1;
        int32_t        locked_count          = 0;
        xlator_t      *this                  = NULL;

        this = THIS;
        GF_ASSERT(this);

        if (!dict) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_EMPTY, "dict is null.");
                ret = -1;
                goto out;
        }

        /* Locking one entity after other */
        for (i = 0; valid_types[i].type; i++) {
                ret = glusterd_mgmt_v3_lock_entity
                                            (dict, uuid, op_errno,
                                             valid_types[i].type,
                                             valid_types[i].default_value);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MULTIPLE_LOCK_ACQUIRE_FAIL,
                                "Unable to lock all %s",
                                valid_types[i].type);
                        break;
                }
                locked_count++;
        }

        if (locked_count == GF_MAX_LOCKING_ENTITIES) {
                /* If all locking ops went successfuly, return as success */
                ret = 0;
                goto out;
        }

        /* If we failed to lock one entity, unlock others and return failure */
        for (i = 0; i < locked_count; i++) {
                ret = glusterd_mgmt_v3_unlock_entity
                                              (dict, uuid,
                                               valid_types[i].type,
                                               valid_types[i].default_value);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_MULTIPLE_LOCK_RELEASE_FAIL,
                                "Unable to unlock all %s",
                                valid_types[i].type);
                }
        }
        ret = -1;
out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}


int32_t
glusterd_mgmt_v3_lock (const char *name, uuid_t uuid, uint32_t *op_errno,
                       char *type)
{
        char                            key[PATH_MAX]   = "";
        int32_t                         ret             = -1;
        glusterd_mgmt_v3_lock_obj      *lock_obj        = NULL;
        glusterd_conf_t                *priv            = NULL;
        gf_boolean_t                    is_valid        = _gf_true;
        uuid_t                          owner           = {0};
        xlator_t                       *this            = NULL;
        char                           *bt              = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        if (!name || !type) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INVALID_ENTRY, "name or type is null.");
                ret = -1;
                goto out;
        }

        is_valid = glusterd_mgmt_v3_is_type_valid (type);
        if (is_valid != _gf_true) {
                gf_msg_callingfn (this->name, GF_LOG_ERROR,
                        EINVAL, GD_MSG_INVALID_ENTRY,
                        "Invalid entity. Cannot perform locking "
                        "operation on %s types", type);
                ret = -1;
                goto out;
        }

        ret = snprintf (key, sizeof(key), "%s_%s", name, type);
        if (ret != strlen(name) + 1 + strlen(type)) {
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_CREATE_KEY_FAIL, "Unable to create key");
                goto out;
        }

        gf_msg_debug (this->name, 0,
                "Trying to acquire lock of %s %s for %s as %s",
                type, name, uuid_utoa (uuid), key);

        ret = glusterd_get_mgmt_v3_lock_owner (key, &owner);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "Unable to get mgmt_v3 lock owner");
                goto out;
        }

        /* If the lock has already been held for the given volume
         * we fail */
        if (!gf_uuid_is_null (owner)) {
                gf_msg_callingfn (this->name, GF_LOG_WARNING,
                                  0, GD_MSG_LOCK_ALREADY_HELD,
                                  "Lock for %s held by %s",
                                  name, uuid_utoa (owner));
                ret = -1;
                *op_errno = EG_ANOTRANS;
                goto out;
        }

        lock_obj = GF_CALLOC (1, sizeof(glusterd_mgmt_v3_lock_obj),
                              gf_common_mt_mgmt_v3_lock_obj_t);
        if (!lock_obj) {
                ret = -1;
                goto out;
        }

        gf_uuid_copy (lock_obj->lock_owner, uuid);

        ret = dict_set_bin (priv->mgmt_v3_lock, key, lock_obj,
                            sizeof(glusterd_mgmt_v3_lock_obj));
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED,
                        "Unable to set lock owner in mgmt_v3 lock");
                GF_FREE (lock_obj);
                goto out;
        }

        /* Saving the backtrace into the pre-allocated buffer, ctx->btbuf*/
        if ((bt = gf_backtrace_save (NULL))) {
                snprintf (key, sizeof (key), "debug.last-success-bt-%s-%s",
                          name, type);
                ret = dict_set_dynstr_with_alloc (priv->mgmt_v3_lock, key, bt);
                if (ret)
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_DICT_SET_FAILED, "Failed to save "
                                "the back trace for lock %s-%s granted to %s",
                                name, type, uuid_utoa (uuid));
                ret = 0;
        }

        gf_msg_debug (this->name, 0,
                "Lock for %s %s successfully held by %s",
                type, name, uuid_utoa (uuid));

        ret = 0;
out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_mgmt_v3_unlock (const char *name, uuid_t uuid, char *type)
{
        char                    key[PATH_MAX]   = "";
        int32_t                 ret             = -1;
        gf_boolean_t            is_valid        = _gf_true;
        glusterd_conf_t        *priv            = NULL;
        uuid_t                  owner           = {0};
        xlator_t               *this            = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        if (!name || !type) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INVALID_ENTRY, "name is null.");
                ret = -1;
                goto out;
        }

        is_valid = glusterd_mgmt_v3_is_type_valid (type);
        if (is_valid != _gf_true) {
                gf_msg_callingfn (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INVALID_ENTRY,
                        "Invalid entity. Cannot perform unlocking "
                        "operation on %s types", type);
                ret = -1;
                goto out;
        }

        ret = snprintf (key, sizeof(key), "%s_%s",
                        name, type);
        if (ret != strlen(name) + 1 + strlen(type)) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_CREATE_KEY_FAIL, "Unable to create key");
                ret = -1;
                goto out;
        }

        gf_msg_debug (this->name, 0,
                "Trying to release lock of %s %s for %s as %s",
                type, name, uuid_utoa (uuid), key);

        ret = glusterd_get_mgmt_v3_lock_owner (key, &owner);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "Unable to get mgmt_v3 lock owner");
                goto out;
        }

        if (gf_uuid_is_null (owner)) {
                gf_msg_callingfn (this->name, GF_LOG_WARNING,
                        0, GD_MSG_LOCK_NOT_HELD,
                        "Lock for %s %s not held", type, name);
                ret = -1;
                goto out;
        }

        ret = gf_uuid_compare (uuid, owner);
        if (ret) {
                gf_msg_callingfn (this->name, GF_LOG_WARNING,
                                  0, GD_MSG_LOCK_OWNER_MISMATCH,
                                  "Lock owner mismatch. "
                                  "Lock for %s %s held by %s",
                                  type, name, uuid_utoa (owner));
                goto out;
        }

        /* Removing the mgmt_v3 lock from the global list */
        dict_del (priv->mgmt_v3_lock, key);

        /* Remove the backtrace key as well */
        ret = snprintf (key, sizeof(key), "debug.last-success-bt-%s-%s", name,
                        type);
        if (ret != strlen ("debug.last-success-bt-") + strlen (name) +
                   strlen (type) + 1) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_CREATE_KEY_FAIL, "Unable to create backtrace "
                        "key");
                ret = -1;
                goto out;
        }
        dict_del (priv->mgmt_v3_lock, key);

        gf_msg_debug (this->name, 0,
                "Lock for %s %s successfully released",
                type, name);

        ret = 0;
out:
        gf_msg_trace (this->name, 0, "Returning %d", ret);
        return ret;
}
