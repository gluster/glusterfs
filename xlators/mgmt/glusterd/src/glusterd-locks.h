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

typedef struct glusterd_mgmt_v3_lock_object_ {
        uuid_t              lock_owner;
} glusterd_mgmt_v3_lock_obj;

typedef struct glusterd_mgmt_v3_lock_valid_entities {
        char          *type;          /* Entity type like vol, snap */
        gf_boolean_t   default_value; /* The default value that  *
                                       * determines if the locks *
                                       * should be held for that *
                                       * entity */
} glusterd_valid_entities;

int32_t
glusterd_mgmt_v3_lock_init ();

void
glusterd_mgmt_v3_lock_fini ();

int32_t
glusterd_get_mgmt_v3_lock_owner (char *volname, uuid_t *uuid);

int32_t
glusterd_mgmt_v3_lock (const char *key, uuid_t uuid, uint32_t *op_errno,
                       char *type);

int32_t
glusterd_mgmt_v3_unlock (const char *key, uuid_t uuid, char *type);

int32_t
glusterd_multiple_mgmt_v3_lock (dict_t *dict, uuid_t uuid, uint32_t *op_errno);

int32_t
glusterd_multiple_mgmt_v3_unlock (dict_t *dict, uuid_t uuid);

#endif
