/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __NFS_H__
#define __NFS_H__

#include "rpcsvc.h"
#include <glusterfs/dict.h>
#include <glusterfs/lkowner.h>
#include <glusterfs/gidcache.h>

#define GF_NFS "nfs"

#define GF_NFS_CONCURRENT_OPS_MULT 15

#define GF_NFS_INODE_LRU_MULT 6000

/* Threading limits for nfs xlator event threads. */
#define NFS_MIN_EVENT_THREADS 1
#define NFS_MAX_EVENT_THREADS 32

#define GF_NFS_DEFAULT_MEMFACTOR 15
#define GF_NFS_MIN_MEMFACTOR 1
#define GF_NFS_MAX_MEMFACTOR 30

#define GF_NFS_DVM_ON 1
#define GF_NFS_DVM_OFF 0

/* Disable using the exports file by default */
#define GF_NFS_DEFAULT_EXPORT_AUTH 0

#define GF_NFS_DEFAULT_AUTH_REFRESH_INTERVAL_SEC 2
#define GF_NFS_DEFAULT_AUTH_CACHE_TTL_SEC 300 /* 5 min */

/* This corresponds to the max 16 number of group IDs that are sent through an
 * RPC request. Since NFS is the only one going to set this, we can be safe
 * in keeping this size hardcoded.
 */
#define GF_REQUEST_MAXGROUPS 16

/* Callback into a version-specific NFS protocol.
 * The return type is used by the nfs.c code to register the protocol.
 * with the RPC service.
 */
typedef rpcsvc_program_t *(*nfs_version_initer_t)(xlator_t *nfsx);

/* List of version-specific protocol initiators */
struct nfs_initer_list {
    struct list_head list;
    nfs_version_initer_t init;
    rpcsvc_program_t *program;
    gf_boolean_t required;
};

struct nfs_state {
    struct list_head versions;
    gid_cache_t gid_cache;
    gf_lock_t svinitlock;
    rpcsvc_t *rpcsvc;
    struct mount3_state *mstate;
    struct nfs3_state *nfs3state;
    struct nlm4_state *nlm4state;
    struct mem_pool *foppool;
    xlator_list_t *subvols;
    xlator_t **initedxl;
    char *rmtab;
    struct rpc_clnt *rpc_clnt;
    char *rpc_statd;
    char *rpc_statd_pid_file;
    unsigned int memfactor;
    int allsubvols;
    int upsubvols;
    int subvols_started;
    int dynamicvolumes;
    int enable_ino32;
    unsigned int override_portnum;
    int allow_insecure;
    int enable_nlm;
    int enable_acl;
    int mount_udp;

    /* Enable exports auth model */
    int exports_auth;
    /* Refresh auth params from disk periodically */
    int refresh_auth;

    unsigned int auth_refresh_time_secs;
    unsigned int auth_cache_ttl_sec;

    uint32_t server_aux_gids_max_age;
    uint32_t generation;
    uint32_t event_threads;

    gf_boolean_t server_aux_gids;
    gf_boolean_t register_portmap;
    gf_boolean_t rdirplus;

#ifdef HAVE_LIBTIRPC
    int svc_running;
#endif
};

struct nfs_inode_ctx {
    struct list_head shares;
    uint32_t generation;
};

#define gf_nfs_dvm_on(nfsstt)                                                  \
    (((struct nfs_state *)nfsstt)->dynamicvolumes == GF_NFS_DVM_ON)
#define gf_nfs_dvm_off(nfsstt)                                                 \
    (((struct nfs_state *)nfsstt)->dynamicvolumes == GF_NFS_DVM_OFF)
#define __gf_nfs_enable_ino32(nfsstt)                                          \
    (((struct nfs_state *)nfsstt)->enable_ino32)
#define gf_nfs_this_private ((struct nfs_state *)((xlator_t *)THIS)->private)
#define gf_nfs_enable_ino32() (__gf_nfs_enable_ino32(gf_nfs_this_private))

/* We have one gid more than the glusterfs maximum since we pass the primary
 * gid as the first element of the array.
 */
#define NFS_NGROUPS (GF_REQUEST_MAXGROUPS + 1)

/* Index of the primary gid */
#define NFS_PRIMGID_IDX 0

typedef struct nfs_user_info {
    uid_t uid;
    gid_t gids[NFS_NGROUPS];
    int ngrps;
    gf_lkowner_t lk_owner;
    char identifier[UNIX_PATH_MAX]; /* ip of user */
} nfs_user_t;

extern int
nfs_user_root_create(nfs_user_t *newnfu);

extern int
nfs_user_create(nfs_user_t *newnfu, uid_t uid, gid_t gid,
                rpc_transport_t *trans, gid_t *auxgids, int auxcount);

extern void
nfs_request_user_init(nfs_user_t *nfu, rpcsvc_request_t *req);

extern void
nfs_request_primary_user_init(nfs_user_t *nfu, rpcsvc_request_t *req, uid_t uid,
                              gid_t gid);
extern int
nfs_subvolume_started(struct nfs_state *nfs, xlator_t *xl);

extern void
nfs_fix_groups(xlator_t *this, call_stack_t *root);

void
nfs_start_rpc_poller(struct nfs_state *state);

#endif
