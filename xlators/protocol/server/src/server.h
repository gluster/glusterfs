/*
  Copyright (c) 2010-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _SERVER_H
#define _SERVER_H

#include <pthread.h>

#include "fd.h"
#include "rpcsvc.h"

#include "fd.h"
#include "protocol-common.h"
#include "server-mem-types.h"
#include "glusterfs3.h"
#include "timer.h"
#include "client_t.h"
#include "gidcache.h"

#define DEFAULT_BLOCK_SIZE         4194304   /* 4MB */
#define DEFAULT_VOLUME_FILE_PATH   CONFDIR "/glusterfs.vol"
#define GF_MAX_SOCKET_WINDOW_SIZE  (1 * GF_UNIT_MB)
#define GF_MIN_SOCKET_WINDOW_SIZE  (0)

typedef enum {
        INTERNAL_LOCKS = 1,
        POSIX_LOCKS = 2,
} server_lock_flags_t;

typedef struct _server_state server_state_t;

int server_null (rpcsvc_request_t *req);

struct _volfile_ctx {
        struct _volfile_ctx *next;
        char                *key;
        uint32_t             checksum;
};

struct server_conf {
        rpcsvc_t               *rpc;
        struct rpcsvc_config    rpc_conf;
        int                     inode_lru_limit;
        gf_boolean_t            verify_volfile;
        gf_boolean_t            trace;
        gf_boolean_t            lk_heal; /* If true means lock self
                                            heal is on else off. */
        char                   *conf_dir;
        struct _volfile_ctx    *volfile;
        struct timespec         grace_ts;
        dict_t                 *auth_modules;
        pthread_mutex_t         mutex;
        struct list_head        xprt_list;

        gf_boolean_t            server_manage_gids; /* resolve gids on brick */
        gid_cache_t             gid_cache;
        int32_t                 gid_cache_timeout;
};
typedef struct server_conf server_conf_t;


typedef enum {
        RESOLVE_MUST = 1,
        RESOLVE_NOT,
        RESOLVE_MAY,
        RESOLVE_DONTCARE,
        RESOLVE_EXACT
} server_resolve_type_t;


struct resolve_comp {
        char      *basename;
        inode_t   *inode;
};

typedef struct {
        server_resolve_type_t  type;
        int64_t               fd_no;
        u_char                 gfid[16];
        u_char                 pargfid[16];
        char                  *path;
        char                  *bname;
        int                    op_ret;
        int                    op_errno;
        loc_t                  resolve_loc;
} server_resolve_t;


typedef int (*server_resume_fn_t) (call_frame_t *frame, xlator_t *bound_xl);

int
resolve_and_resume (call_frame_t *frame, server_resume_fn_t fn);

struct _server_state {
        rpc_transport_t  *xprt;
        inode_table_t    *itable;

        server_resume_fn_t resume_fn;

        loc_t             loc;
        loc_t             loc2;
        server_resolve_t  resolve;
        server_resolve_t  resolve2;

        /* used within resolve_and_resume */
        loc_t            *loc_now;
        server_resolve_t *resolve_now;

        struct iatt       stbuf;
        int               valid;

        fd_t             *fd;
        dict_t           *params;
        int32_t           flags;
        int               wbflags;
        struct iovec      payload_vector[MAX_IOVEC];
        int               payload_count;
        struct iobuf     *iobuf;
        struct iobref    *iobref;

        size_t            size;
        off_t             offset;
        mode_t            mode;
        dev_t             dev;
        size_t            nr_count;
        int               cmd;
        int               type;
        char             *name;
        int               name_len;

        int               mask;
        char              is_revalidate;
        dict_t           *dict;
        struct gf_flock   flock;
        const char       *volume;
        dir_entry_t      *entry;

        dict_t           *xdata;
        mode_t            umask;
};


extern struct rpcsvc_program gluster_handshake_prog;
extern struct rpcsvc_program glusterfs3_3_fop_prog;
extern struct rpcsvc_program gluster_ping_prog;


typedef struct _server_ctx {
        gf_lock_t            fdtable_lock;
        fdtable_t           *fdtable;
        struct _gf_timer    *grace_timer;
        uint32_t             lk_version;
} server_ctx_t;


int
server_submit_reply (call_frame_t *frame, rpcsvc_request_t *req, void *arg,
                     struct iovec *payload, int payloadcount,
                     struct iobref *iobref, xdrproc_t xdrproc);

int gf_server_check_setxattr_cmd (call_frame_t *frame, dict_t *dict);
int gf_server_check_getxattr_cmd (call_frame_t *frame, const char *name);

#endif /* !_SERVER_H */
