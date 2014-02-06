/*
   Copyright (c) 2013-2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _GLUSTERD_LOCKS_H_
#define _GLUSTERD_LOCKS_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

struct volume_lock_object_ {
        uuid_t              lock_owner;
};
typedef struct volume_lock_object_ vol_lock_obj;

int32_t
glusterd_vol_lock_init ();

void
glusterd_vol_lock_fini ();

int32_t
glusterd_get_vol_lock_owner (char *volname, uuid_t *uuid);

int32_t
glusterd_volume_lock (char *volname, uuid_t uuid);

int32_t
glusterd_volume_unlock (char *volname, uuid_t uuid);

#endif
