/*
   Copyright (c) 2013-2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "common-utils.h"
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "glusterd-locks.h"
#include "run.h"
#include "syscall.h"

#include <signal.h>

static dict_t *vol_lock;

/* Initialize the global vol-lock list(dict) when
 * glusterd is spawned */
int32_t
glusterd_vol_lock_init ()
{
        int32_t ret = -1;

        vol_lock = dict_new ();
        if (!vol_lock)
                goto out;

        ret = 0;
out:
        return ret;
}

/* Destroy the global vol-lock list(dict) when
 * glusterd cleanup is performed */
void
glusterd_vol_lock_fini ()
{
        if (vol_lock)
                dict_unref (vol_lock);
}

int32_t
glusterd_get_vol_lock_owner (char *volname, uuid_t *uuid)
{
        int32_t        ret      = -1;
        vol_lock_obj  *lock_obj = NULL;
        uuid_t         no_owner = {0,};

        if (!volname || !uuid) {
                gf_log ("", GF_LOG_ERROR, "volname or uuid is null.");
                ret = -1;
                goto out;
        }

        ret = dict_get_bin (vol_lock, volname, (void **) &lock_obj);
        if (!ret)
                uuid_copy (*uuid, lock_obj->lock_owner);
        else
                uuid_copy (*uuid, no_owner);

        ret = 0;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_volume_lock (char *volname, uuid_t uuid)
{
        int32_t        ret      = -1;
        vol_lock_obj  *lock_obj = NULL;
        uuid_t         owner    = {0};

        if (!volname) {
                gf_log ("", GF_LOG_ERROR, "volname is null.");
                ret = -1;
                goto out;
        }

        ret = glusterd_get_vol_lock_owner (volname, &owner);
        if (ret) {
                gf_log ("", GF_LOG_DEBUG, "Unable to get volume lock owner");
                goto out;
        }

        /* If the lock has already been held for the given volume
         * we fail */
        if (!uuid_is_null (owner)) {
                gf_log ("", GF_LOG_ERROR, "Unable to acquire lock. "
                        "Lock for %s held by %s", volname,
                        uuid_utoa (owner));
                ret = -1;
                goto out;
        }

        lock_obj = GF_CALLOC (1, sizeof(vol_lock_obj),
                              gf_common_mt_vol_lock_obj_t);
        if (!lock_obj) {
                ret = -1;
                goto out;
        }

        uuid_copy (lock_obj->lock_owner, uuid);

        ret = dict_set_bin (vol_lock, volname, lock_obj, sizeof(vol_lock_obj));
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set lock owner "
                                          "in volume lock");
                if (lock_obj)
                        GF_FREE (lock_obj);
                goto out;
        }

        gf_log ("", GF_LOG_DEBUG, "Lock for %s successfully held by %s",
                volname, uuid_utoa (uuid));

        ret = 0;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_volume_unlock (char *volname, uuid_t uuid)
{
        int32_t        ret      = -1;
        uuid_t         owner    = {0};

        if (!volname) {
                gf_log ("", GF_LOG_ERROR, "volname is null.");
                ret = -1;
                goto out;
        }

        ret = glusterd_get_vol_lock_owner (volname, &owner);
        if (ret)
                goto out;

        if (uuid_is_null (owner)) {
                gf_log ("", GF_LOG_ERROR, "Lock for %s not held", volname);
                ret = -1;
                goto out;
        }

        ret = uuid_compare (uuid, owner);
        if (ret) {
                gf_log (THIS->name, GF_LOG_ERROR, "Lock owner mismatch. "
                        "Lock for %s held by %s",
                        volname, uuid_utoa (owner));
                goto out;
        }

        /* Removing the volume lock from the global list */
        dict_del (vol_lock, volname);

        gf_log ("", GF_LOG_DEBUG, "Lock for %s successfully released",
                volname);

        ret = 0;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}
