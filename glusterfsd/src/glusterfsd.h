/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __GLUSTERFSD_H__
#define __GLUSTERFSD_H__

#include "rpcsvc.h"
#include "glusterd1-xdr.h"

#define DEFAULT_GLUSTERD_VOLFILE              CONFDIR "/glusterd.vol"
#define DEFAULT_CLIENT_VOLFILE                CONFDIR "/glusterfs.vol"
#define DEFAULT_SERVER_VOLFILE                CONFDIR "/glusterfsd.vol"

#define DEFAULT_EVENT_POOL_SIZE            16384

#define ARGP_LOG_LEVEL_NONE_OPTION        "NONE"
#define ARGP_LOG_LEVEL_TRACE_OPTION       "TRACE"
#define ARGP_LOG_LEVEL_CRITICAL_OPTION    "CRITICAL"
#define ARGP_LOG_LEVEL_ERROR_OPTION       "ERROR"
#define ARGP_LOG_LEVEL_WARNING_OPTION     "WARNING"
#define ARGP_LOG_LEVEL_INFO_OPTION        "INFO"
#define ARGP_LOG_LEVEL_DEBUG_OPTION       "DEBUG"

#define ENABLE_NO_DAEMON_MODE     1
#define ENABLE_DEBUG_MODE         1

#define GF_MEMPOOL_COUNT_OF_DICT_T        4096
/* Considering 4 key/value pairs in a dictionary on an average */
#define GF_MEMPOOL_COUNT_OF_DATA_T        (GF_MEMPOOL_COUNT_OF_DICT_T * 4)
#define GF_MEMPOOL_COUNT_OF_DATA_PAIR_T   (GF_MEMPOOL_COUNT_OF_DICT_T * 4)

#define GF_MEMPOOL_COUNT_OF_LRU_BUF_T     256

enum argp_option_keys {
        ARGP_VOLFILE_SERVER_KEY           = 's',
        ARGP_VOLUME_FILE_KEY              = 'f',
        ARGP_LOG_LEVEL_KEY                = 'L',
        ARGP_LOG_FILE_KEY                 = 'l',
        ARGP_VOLFILE_SERVER_PORT_KEY      = 131,
        ARGP_VOLFILE_SERVER_TRANSPORT_KEY = 132,
        ARGP_PID_FILE_KEY                 = 'p',
        ARGP_SOCK_FILE_KEY                = 'S',
        ARGP_NO_DAEMON_KEY                = 'N',
        ARGP_RUN_ID_KEY                   = 'r',
        ARGP_PRINT_NETGROUPS              = 'n',
        ARGP_PRINT_EXPORTS                = 'e',
        ARGP_DEBUG_KEY                    = 133,
        ARGP_NEGATIVE_TIMEOUT_KEY         = 134,
        ARGP_ENTRY_TIMEOUT_KEY            = 135,
        ARGP_ATTRIBUTE_TIMEOUT_KEY        = 136,
        ARGP_VOLUME_NAME_KEY              = 137,
        ARGP_XLATOR_OPTION_KEY            = 138,
        ARGP_DIRECT_IO_MODE_KEY           = 139,
#ifdef GF_DARWIN_HOST_OS
        ARGP_NON_LOCAL_KEY                = 140,
#endif /* DARWIN */
        ARGP_VOLFILE_ID_KEY               = 143,
        ARGP_VOLFILE_CHECK_KEY            = 144,
        ARGP_VOLFILE_MAX_FETCH_ATTEMPTS   = 145,
        ARGP_LOG_SERVER_KEY               = 146,
        ARGP_LOG_SERVER_PORT_KEY          = 147,
        ARGP_READ_ONLY_KEY                = 148,
        ARGP_MAC_COMPAT_KEY               = 149,
        ARGP_DUMP_FUSE_KEY                = 150,
        ARGP_BRICK_NAME_KEY               = 151,
        ARGP_BRICK_PORT_KEY               = 152,
        ARGP_CLIENT_PID_KEY               = 153,
        ARGP_ACL_KEY                      = 154,
        ARGP_WORM_KEY                     = 155,
        ARGP_USER_MAP_ROOT_KEY            = 156,
        ARGP_MEM_ACCOUNTING_KEY           = 157,
        ARGP_SELINUX_KEY                  = 158,
	ARGP_FOPEN_KEEP_CACHE_KEY	  = 159,
	ARGP_GID_TIMEOUT_KEY		  = 160,
	ARGP_FUSE_BACKGROUND_QLEN_KEY     = 161,
	ARGP_FUSE_CONGESTION_THRESHOLD_KEY = 162,
        ARGP_INODE32_KEY                  = 163,
	ARGP_FUSE_MOUNTOPTS_KEY		  = 164,
        ARGP_FUSE_USE_READDIRP_KEY        = 165,
	ARGP_AUX_GFID_MOUNT_KEY		  = 166,
        ARGP_FUSE_NO_ROOT_SQUASH_KEY      = 167,
        ARGP_LOGGER                       = 168,
        ARGP_LOG_FORMAT                   = 169,
        ARGP_LOG_BUF_SIZE                 = 170,
        ARGP_LOG_FLUSH_TIMEOUT            = 171,
        ARGP_SECURE_MGMT_KEY              = 172,
        ARGP_GLOBAL_TIMER_WHEEL           = 173,
        ARGP_RESOLVE_GIDS_KEY             = 174,
        ARGP_CAPABILITY_KEY               = 175,
#ifdef GF_LINUX_HOST_OS
        ARGP_OOM_SCORE_ADJ_KEY            = 176,
#endif
};

struct _gfd_vol_top_priv_t {
        rpcsvc_request_t        *req;
        gd1_mgmt_brick_op_req   xlator_req;
        uint32_t                blk_count;
        uint32_t                blk_size;
        double                  throughput;
        double                  time;
        int32_t                 ret;
};
typedef struct _gfd_vol_top_priv_t gfd_vol_top_priv_t;

int glusterfs_mgmt_pmap_signout (glusterfs_ctx_t *ctx);
int glusterfs_mgmt_pmap_signin (glusterfs_ctx_t *ctx);
int glusterfs_volfile_fetch (glusterfs_ctx_t *ctx);
void cleanup_and_exit (int signum);

int glusterfs_volume_top_write_perf (uint32_t blk_size, uint32_t blk_count,
                                     char *brick_path, double *throughput,
                                     double *time);
int glusterfs_volume_top_read_perf (uint32_t blk_size, uint32_t blk_count,
                                    char *brick_path, double *throughput,
                                    double *time);

extern glusterfs_ctx_t *glusterfsd_ctx;
#endif /* __GLUSTERFSD_H__ */
