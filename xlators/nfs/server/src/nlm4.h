/*
  Copyright (c) 2012 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _NLM4_H_
#define _NLM4_H_

#include <sys/types.h>
#include <signal.h>
#include "rpcsvc.h"
#include "dict.h"
#include "xlator.h"
#include "iobuf.h"
#include "nfs.h"
#include "list.h"
#include "xdr-nfs3.h"
#include "locking.h"
#include "nfs3-fh.h"
#include "compat-uuid.h"
#include "nlm4-xdr.h"
#include "lkowner.h"

#define NLM4_NULL          0
#define NLM4_TEST          1
#define NLM4_LOCK          2
#define NLM4_CANCEL        3
#define NLM4_UNLOCK        4
#define NLM4_GRANTED       5
#define NLM4_TEST_MSG      6
#define NLM4_LOCK_MSG      7
#define NLM4_CANCEL_MSG    8
#define NLM4_UNLOCK_MSG    9
#define NLM4_GRANTED_MSG   10
#define NLM4_TEST_RES      11
#define NLM4_LOCK_RES      12
#define NLM4_CANCEL_RES    13
#define NLM4_UNLOCK_RES    14
#define NLM4_GRANTED_RES   15
#define NLM4_SM_NOTIFY     16
#define NLM4_SEVENTEEN     17
#define NLM4_EIGHTEEN      18
#define NLM4_NINETEEN      19
#define NLM4_SHARE         20
#define NLM4_UNSHARE       21
#define NLM4_NM_LOCK       22
#define NLM4_FREE_ALL      23
#define NLM4_PROC_COUNT    24

/* Registered with portmap */
#define GF_NLM4_PORT            38468
#define GF_NLM                  GF_NFS"-NLM"
#if defined(GF_DARWIN_HOST_OS)
#define GF_RPC_STATD_PROG       "/usr/sbin/rpc.statd"
#define GF_RPC_STATD_PIDFILE    "/var/run/statd.pid"
#define GF_SM_NOTIFY_PIDFILE    "/var/run/statd.notify.pid"
#elif defined(__NetBSD__)
#define GF_RPC_STATD_PROG       "/usr/sbin/rpc.statd"
#define GF_RPC_STATD_PIDFILE    "/var/run/rpc.statd.pid"
#define GF_SM_NOTIFY_PIDFILE    "/var/run/inexistant.pid"
#else
#define GF_RPC_STATD_PROG       "/sbin/rpc.statd"
#define GF_RPC_STATD_PIDFILE    "/var/run/rpc.statd.pid"
#define GF_SM_NOTIFY_PIDFILE    "/var/run/sm-notify.pid"
#endif

extern rpcsvc_program_t *
nlm4svc_init (xlator_t *nfsx);

extern int
nlm4_init_state (xlator_t *nfsx);

#define NLM_PROGRAM 100021
#define NLM_V4 4

typedef struct nlm4_lwowner {
        char temp[1024];
} nlm4_lkowner_t;

typedef struct nlm_client {
        struct sockaddr_storage sa;
        pid_t uniq;
        struct list_head nlm_clients;
        struct list_head fdes;
        struct list_head shares;
        struct rpc_clnt *rpc_clnt;
        char *caller_name;
        int nsm_monitor;
} nlm_client_t;

typedef struct nlm_share {
        struct list_head     client_list;
        struct list_head     inode_list;
        gf_lkowner_t         lkowner;
        inode_t             *inode;
        fsh_mode             mode;
        fsh_access           access;
} nlm_share_t;

typedef struct nlm_fde {
        struct list_head fde_list;
        fd_t *fd;
        int transit_cnt;
} nlm_fde_t;

#endif
