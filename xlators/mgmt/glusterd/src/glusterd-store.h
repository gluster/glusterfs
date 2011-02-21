/*
  Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _GLUSTERD_HA_H_
#define _GLUSTERD_HA_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <pthread.h>
#include "uuid.h"

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "call-stub.h"
#include "fd.h"
#include "byte-order.h"
#include "glusterd.h"
#include "rpcsvc.h"


#define GLUSTERD_STORE_UUID_KEY           "UUID"

#define GLUSTERD_STORE_KEY_VOL_TYPE       "type"
#define GLUSTERD_STORE_KEY_VOL_COUNT      "count"
#define GLUSTERD_STORE_KEY_VOL_STATUS     "status"
#define GLUSTERD_STORE_KEY_VOL_PORT       "port"
#define GLUSTERD_STORE_KEY_VOL_SUB_COUNT  "sub_count"
#define GLUSTERD_STORE_KEY_VOL_BRICK      "brick"
#define GLUSTERD_STORE_KEY_VOL_VERSION    "version"
#define GLUSTERD_STORE_KEY_VOL_TRANSPORT  "transport-type"
#define GLUSTERD_STORE_KEY_VOL_ID         "volume-id"

#define GLUSTERD_STORE_KEY_BRICK_HOSTNAME "hostname"
#define GLUSTERD_STORE_KEY_BRICK_PATH     "path"
#define GLUSTERD_STORE_KEY_BRICK_PORT     "listen-port"

#define GLUSTERD_STORE_KEY_PEER_UUID      "uuid"
#define GLUSTERD_STORE_KEY_PEER_HOSTNAME  "hostname"
#define GLUSTERD_STORE_KEY_PEER_STATE     "state"

#define glusterd_for_each_entry(entry, dir) \
        do {\
                entry = NULL;\
                if (dir) {\
                        entry = readdir (dir);\
                        while (entry && (!strcmp (entry->d_name, ".") ||\
                            !strcmp (entry->d_name, ".."))) {\
                                entry = readdir (dir);\
                        }\
                }\
        } while (0); \

typedef enum {
        GD_STORE_SUCCESS,
        GD_STORE_KEY_NULL,
        GD_STORE_VALUE_NULL,
        GD_STORE_KEY_VALUE_NULL,
        GD_STORE_EOF,
        GD_STORE_ENOMEM
} glusterd_store_op_errno_t;

int32_t
glusterd_store_create_volume (glusterd_volinfo_t *volinfo);

int32_t
glusterd_store_delete_volume (glusterd_volinfo_t *volinfo);

int32_t
glusterd_store_uuid ();

int32_t
glusterd_store_handle_new (char *path, glusterd_store_handle_t **handle);

int32_t
glusterd_store_save_value (glusterd_store_handle_t *handle,
                           char *key, char *value);

int32_t
glusterd_store_retrieve_value (glusterd_store_handle_t *handle,
                               char *key, char **value);

int32_t
glusterd_store_update_volume (glusterd_volinfo_t *volinfo);

int32_t
glusterd_retrieve_uuid ();

int32_t
glusterd_store_update_peerinfo (glusterd_peerinfo_t *peerinfo);

int32_t
glusterd_store_delete_peerinfo (glusterd_peerinfo_t *peerinfo);

int32_t
glusterd_store_delete_brick (glusterd_volinfo_t *volinfo,
                             glusterd_brickinfo_t *brickinfo);

int32_t
glusterd_store_handle_destroy (glusterd_store_handle_t *handle);

int32_t
glusterd_restore ();

#endif
