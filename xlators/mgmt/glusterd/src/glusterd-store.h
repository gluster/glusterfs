/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _GLUSTERD_HA_H_
#define _GLUSTERD_HA_H_

#include <pthread.h>
#include "compat-uuid.h"

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
        GLUSTERD_VOLINFO_VER_AC_DECREMENT = 2,
} glusterd_volinfo_ver_ac_t;


#define GLUSTERD_STORE_UUID_KEY                 "UUID"

#define GLUSTERD_STORE_KEY_VOL_TYPE             "type"
#define GLUSTERD_STORE_KEY_VOL_COUNT            "count"
#define GLUSTERD_STORE_KEY_VOL_STATUS           "status"
#define GLUSTERD_STORE_KEY_VOL_PORT             "port"
#define GLUSTERD_STORE_KEY_VOL_SUB_COUNT        "sub_count"
#define GLUSTERD_STORE_KEY_VOL_STRIPE_CNT       "stripe_count"
#define GLUSTERD_STORE_KEY_VOL_REPLICA_CNT      "replica_count"
#define GLUSTERD_STORE_KEY_VOL_DISPERSE_CNT     "disperse_count"
#define GLUSTERD_STORE_KEY_VOL_REDUNDANCY_CNT   "redundancy_count"
#define GLUSTERD_STORE_KEY_VOL_ARBITER_CNT      "arbiter_count"
#define GLUSTERD_STORE_KEY_VOL_BRICK            "brick"
#define GLUSTERD_STORE_KEY_VOL_VERSION          "version"
#define GLUSTERD_STORE_KEY_VOL_TRANSPORT        "transport-type"
#define GLUSTERD_STORE_KEY_VOL_ID               "volume-id"
#define GLUSTERD_STORE_KEY_VOL_RESTORED_SNAP    "restored_from_snap"
#define GLUSTERD_STORE_KEY_RB_STATUS            "rb_status"
#define GLUSTERD_STORE_KEY_RB_SRC_BRICK         "rb_src"
#define GLUSTERD_STORE_KEY_RB_DST_BRICK         "rb_dst"
#define GLUSTERD_STORE_KEY_RB_DST_PORT          "rb_port"
#define GLUSTERD_STORE_KEY_VOL_DEFRAG           "rebalance_status"
#define GLUSTERD_STORE_KEY_VOL_DEFRAG_STATUS    "status"
#define GLUSTERD_STORE_KEY_DEFRAG_OP            "rebalance_op"
#define GLUSTERD_STORE_KEY_USERNAME             "username"
#define GLUSTERD_STORE_KEY_PASSWORD             "password"
#define GLUSTERD_STORE_KEY_PARENT_VOLNAME       "parent_volname"
#define GLUSTERD_STORE_KEY_VOL_OP_VERSION       "op-version"
#define GLUSTERD_STORE_KEY_VOL_CLIENT_OP_VERSION "client-op-version"
#define GLUSTERD_STORE_KEY_VOL_QUOTA_VERSION    "quota-version"

#define GLUSTERD_STORE_KEY_VOL_TIER_STATUS      "tier_status"
#define GLUSTERD_STORE_KEY_TIER_DETACH_OP       "tier_op"
#define GLUSTERD_STORE_KEY_COLD_TYPE            "cold_type"
#define GLUSTERD_STORE_KEY_COLD_COUNT           "cold_count"
#define GLUSTERD_STORE_KEY_COLD_REPLICA_COUNT   "cold_replica_count"
#define GLUSTERD_STORE_KEY_COLD_DISPERSE_COUNT  "cold_disperse_count"
#define GLUSTERD_STORE_KEY_COLD_REDUNDANCY_COUNT  "cold_redundancy_count"
#define GLUSTERD_STORE_KEY_HOT_TYPE             "hot_type"
#define GLUSTERD_STORE_KEY_HOT_COUNT            "hot_count"
#define GLUSTERD_STORE_KEY_HOT_REPLICA_COUNT    "hot_replica_count"

#define GLUSTERD_STORE_KEY_SNAP_NAME            "name"
#define GLUSTERD_STORE_KEY_SNAP_ID              "snap-id"
#define GLUSTERD_STORE_KEY_SNAP_DESC            "desc"
#define GLUSTERD_STORE_KEY_SNAP_TIMESTAMP       "time-stamp"
#define GLUSTERD_STORE_KEY_SNAP_STATUS          "status"
#define GLUSTERD_STORE_KEY_SNAP_RESTORED        "snap-restored"
#define GLUSTERD_STORE_KEY_SNAP_MAX_HARD_LIMIT  "snap-max-hard-limit"
#define GLUSTERD_STORE_KEY_SNAP_AUTO_DELETE     "auto-delete"
#define GLUSTERD_STORE_KEY_SNAP_MAX_SOFT_LIMIT  "snap-max-soft-limit"
#define GLUSTERD_STORE_KEY_SNAPD_PORT           "snapd-port"
#define GLUSTERD_STORE_KEY_SNAP_ACTIVATE        "snap-activate-on-create"
#define GLUSTERD_STORE_KEY_GANESHA_GLOBAL       "nfs-ganesha"

#define GLUSTERD_STORE_KEY_BRICK_HOSTNAME       "hostname"
#define GLUSTERD_STORE_KEY_BRICK_PATH           "path"
#define GLUSTERD_STORE_KEY_BRICK_REAL_PATH      "real_path"
#define GLUSTERD_STORE_KEY_BRICK_PORT           "listen-port"
#define GLUSTERD_STORE_KEY_BRICK_RDMA_PORT      "rdma.listen-port"
#define GLUSTERD_STORE_KEY_BRICK_DECOMMISSIONED "decommissioned"
#define GLUSTERD_STORE_KEY_BRICK_VGNAME         "vg"
#define GLUSTERD_STORE_KEY_BRICK_DEVICE_PATH    "device_path"
#define GLUSTERD_STORE_KEY_BRICK_MOUNT_DIR      "mount_dir"
#define GLUSTERD_STORE_KEY_BRICK_SNAP_STATUS    "snap-status"
#define GLUSTERD_STORE_KEY_BRICK_FSTYPE         "fs-type"
#define GLUSTERD_STORE_KEY_BRICK_MNTOPTS        "mnt-opts"
#define GLUSTERD_STORE_KEY_BRICK_ID             "brick-id"

#define GLUSTERD_STORE_KEY_PEER_UUID            "uuid"
#define GLUSTERD_STORE_KEY_PEER_HOSTNAME        "hostname"
#define GLUSTERD_STORE_KEY_PEER_STATE           "state"

#define GLUSTERD_STORE_KEY_VOL_CAPS             "caps"

#define GLUSTERD_STORE_KEY_VOL_DEFRAG_REB_FILES "rebalanced-files"
#define GLUSTERD_STORE_KEY_VOL_DEFRAG_SIZE      "size"
#define GLUSTERD_STORE_KEY_VOL_DEFRAG_SCANNED   "scanned"
#define GLUSTERD_STORE_KEY_VOL_DEFRAG_FAILURES  "failures"
#define GLUSTERD_STORE_KEY_VOL_DEFRAG_SKIPPED   "skipped"
#define GLUSTERD_STORE_KEY_VOL_DEFRAG_RUN_TIME  "run-time"

#define GLUSTERD_STORE_KEY_VOL_MIGRATED_FILES           "migrated-files"
#define GLUSTERD_STORE_KEY_VOL_MIGRATED_SIZE            "migration-size"
#define GLUSTERD_STORE_KEY_VOL_MIGRATIONS_SCANNED       "migration-scanned"
#define GLUSTERD_STORE_KEY_VOL_MIGRATIONS_FAILURES      "migration-failures"
#define GLUSTERD_STORE_KEY_VOL_MIGRATIONS_SKIPPED       "migration-skipped"
#define GLUSTERD_STORE_KEY_VOL_MIGRATION_RUN_TIME       "migration-run-time"

int32_t
glusterd_store_volinfo (glusterd_volinfo_t *volinfo, glusterd_volinfo_ver_ac_t ac);

int32_t
glusterd_store_delete_volume (glusterd_volinfo_t *volinfo);

int32_t
glusterd_store_delete_snap (glusterd_snap_t *snap);

int32_t
glusterd_retrieve_uuid ();

int32_t
glusterd_store_peerinfo (glusterd_peerinfo_t *peerinfo);

int32_t
glusterd_store_delete_peerinfo (glusterd_peerinfo_t *peerinfo);

int32_t
glusterd_store_delete_brick (glusterd_brickinfo_t *brickinfo,
                             char *delete_path);

int32_t
glusterd_restore ();

void
glusterd_perform_volinfo_version_action (glusterd_volinfo_t *volinfo,
                                         glusterd_volinfo_ver_ac_t ac);
gf_boolean_t
glusterd_store_is_valid_brickpath (char *volname, char *brick);

int32_t
glusterd_store_perform_node_state_store (glusterd_volinfo_t *volinfo);

int
glusterd_retrieve_op_version (xlator_t *this, int *op_version);

int
glusterd_store_global_info (xlator_t *this);

int32_t
glusterd_store_retrieve_options (xlator_t *this);

int32_t
glusterd_store_retrieve_bricks (glusterd_volinfo_t *volinfo);

int32_t
glusterd_store_options (xlator_t *this, dict_t *opts);

void
glusterd_replace_slash_with_hyphen (char *str);

int32_t
glusterd_store_perform_volume_store (glusterd_volinfo_t *volinfo);

int32_t
glusterd_store_create_quota_conf_sh_on_absence (glusterd_volinfo_t *volinfo);

int
glusterd_store_retrieve_quota_version (glusterd_volinfo_t *volinfo);

int
glusterd_store_save_quota_version_and_cksum (glusterd_volinfo_t *volinfo);

int32_t
glusterd_store_snap (glusterd_snap_t *snap);

int32_t
glusterd_store_update_missed_snaps ();

glusterd_volinfo_t*
glusterd_store_retrieve_volume (char *volname, glusterd_snap_t *snap);

int
glusterd_restore_op_version (xlator_t *this);

int32_t
glusterd_quota_conf_write_header (int fd);

int32_t
glusterd_quota_conf_write_gfid (int fd, void *buf, char type);

#endif
