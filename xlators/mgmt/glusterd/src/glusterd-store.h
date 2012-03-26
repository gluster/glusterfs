/*
  Copyright (c) 2006-2011 Gluster, Inc. <http://www.gluster.com>
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
#include "run.h"
#include "logging.h"
#include "call-stub.h"
#include "fd.h"
#include "byte-order.h"
#include "glusterd.h"
#include "rpcsvc.h"

typedef enum glusterd_store_ver_ac_{
        GLUSTERD_VOLINFO_VER_AC_NONE = 0,
        GLUSTERD_VOLINFO_VER_AC_INCREMENT = 1,
} glusterd_volinfo_ver_ac_t;


#define GLUSTERD_STORE_UUID_KEY           "UUID"

#define GLUSTERD_STORE_KEY_VOL_TYPE       "type"
#define GLUSTERD_STORE_KEY_VOL_COUNT      "count"
#define GLUSTERD_STORE_KEY_VOL_STATUS     "status"
#define GLUSTERD_STORE_KEY_VOL_PORT       "port"
#define GLUSTERD_STORE_KEY_VOL_SUB_COUNT  "sub_count"
#define GLUSTERD_STORE_KEY_VOL_STRIPE_CNT  "stripe_count"
#define GLUSTERD_STORE_KEY_VOL_REPLICA_CNT "replica_count"
#define GLUSTERD_STORE_KEY_VOL_BRICK      "brick"
#define GLUSTERD_STORE_KEY_VOL_VERSION    "version"
#define GLUSTERD_STORE_KEY_VOL_TRANSPORT  "transport-type"
#define GLUSTERD_STORE_KEY_VOL_ID         "volume-id"
#define GLUSTERD_STORE_KEY_RB_STATUS      "rb_status"
#define GLUSTERD_STORE_KEY_RB_SRC_BRICK   "rb_src"
#define GLUSTERD_STORE_KEY_RB_DST_BRICK   "rb_dst"
#define GLUSTERD_STORE_KEY_VOL_DEFRAG     "rebalance_status"
#define GLUSTERD_STORE_KEY_USERNAME       "username"
#define GLUSTERD_STORE_KEY_PASSWORD       "password"

#define GLUSTERD_STORE_KEY_BRICK_HOSTNAME "hostname"
#define GLUSTERD_STORE_KEY_BRICK_PATH     "path"
#define GLUSTERD_STORE_KEY_BRICK_PORT     "listen-port"
#define GLUSTERD_STORE_KEY_BRICK_RDMA_PORT "rdma.listen-port"
#define GLUSTERD_STORE_KEY_BRICK_DECOMMISSIONED "decommissioned"

#define GLUSTERD_STORE_KEY_PEER_UUID      "uuid"
#define GLUSTERD_STORE_KEY_PEER_HOSTNAME  "hostname"
#define GLUSTERD_STORE_KEY_PEER_STATE     "state"

#define GLUSTERD_GET_HOOKS_DIR(path, version, priv) \
        snprintf (path, PATH_MAX, "%s/hooks/%d", priv->workdir,\
                  version);

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
        GD_STORE_ENOMEM,
        GD_STORE_STAT_FAILED
} glusterd_store_op_errno_t;

#define GLUSTERD_HOOK_VER       1
typedef enum glusterd_commit_hook_type {
        GD_COMMIT_HOOK_NONE = 0,
        GD_COMMIT_HOOK_PRE,
        GD_COMMIT_HOOK_POST,
        GD_COMMIT_HOOK_MAX
} glusterd_commit_hook_type_t;

int32_t
glusterd_store_volinfo (glusterd_volinfo_t *volinfo, glusterd_volinfo_ver_ac_t ac);

int32_t
glusterd_store_delete_volume (glusterd_volinfo_t *volinfo);

int32_t
glusterd_store_uuid ();

int32_t
glusterd_store_handle_new (char *path, glusterd_store_handle_t **handle);

int32_t
glusterd_store_save_value (int fd, char *key, char *value);

int32_t
glusterd_store_retrieve_value (glusterd_store_handle_t *handle,
                               char *key, char **value);

int32_t
glusterd_retrieve_uuid ();

int32_t
glusterd_store_peerinfo (glusterd_peerinfo_t *peerinfo);

int32_t
glusterd_store_delete_peerinfo (glusterd_peerinfo_t *peerinfo);

int32_t
glusterd_store_delete_brick (glusterd_volinfo_t *volinfo,
                             glusterd_brickinfo_t *brickinfo);

int32_t
glusterd_store_handle_destroy (glusterd_store_handle_t *handle);

int32_t
glusterd_restore ();

void
glusterd_perform_volinfo_version_action (glusterd_volinfo_t *volinfo,
                                         glusterd_volinfo_ver_ac_t ac);
gf_boolean_t
glusterd_store_is_valid_brickpath (char *volname, char *brick);

int
glusterd_store_create_hooks_directory (char *basedir);

char *
glusterd_store_get_hooks_cmd_subdir (glusterd_op_t op);

int
glusterd_store_run_hooks (char *hooks_path, dict_t *op_ctx);
#endif
