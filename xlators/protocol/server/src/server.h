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
#include "defaults.h"

#define DEFAULT_BLOCK_SIZE         4194304   /* 4MB */
#define DEFAULT_VOLUME_FILE_PATH   CONFDIR "/glusterfs.vol"
#define GF_MAX_SOCKET_WINDOW_SIZE  (1 * GF_UNIT_MB)
#define GF_MIN_SOCKET_WINDOW_SIZE  (0)

#define CPD_REQ_FIELD(v, f)  ((v)->compound_req_u.compound_##f##_req)
#define CPD_RSP_FIELD(v, f)  ((v)->compound_rsp_u.compound_##f##_rsp)

#define SERVER_COMMON_RSP_CLEANUP(rsp, fop, i)                                \
        do {                                                                  \
                compound_rsp            *this_rsp       = NULL;               \
                this_rsp = &rsp->compound_rsp_array.compound_rsp_array_val[i];\
                gf_common_rsp  *_this_rsp = &CPD_RSP_FIELD(this_rsp, fop);    \
                                                                              \
                GF_FREE (_this_rsp->xdata.xdata_val);                         \
        } while (0)

#define SERVER_FOP_RSP_CLEANUP(rsp, fop, i)                                   \
        do {                                                                  \
                compound_rsp            *this_rsp       = NULL;               \
                this_rsp = &rsp->compound_rsp_array.compound_rsp_array_val[i];\
                gfs3_##fop##_rsp  *_this_rsp = &CPD_RSP_FIELD(this_rsp, fop); \
                                                                              \
                GF_FREE (_this_rsp->xdata.xdata_val);                         \
        } while (0)

#define SERVER_COMPOUND_FOP_CLEANUP(curr_req, fop)                            \
        do {                                                                  \
                gfs3_##fop##_req *_req = &CPD_REQ_FIELD(curr_req, fop);       \
                                                                              \
                free (_req->xdata.xdata_val);                                 \
        } while (0)

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
        uint32_t                grace_timeout;
        dict_t                 *auth_modules;
        pthread_mutex_t         mutex;
        struct list_head        xprt_list;
        pthread_t               barrier_th;

        gf_boolean_t            server_manage_gids; /* resolve gids on brick */
        gid_cache_t             gid_cache;
        int32_t                 gid_cache_timeout;

        int                     event_threads; /* # of event threads
                                                * configured */

        gf_boolean_t            parent_up;
        gf_boolean_t            dync_auth; /* if set authenticate dynamically,
                                            * in case if volume set options
                                            * (say *.allow | *.reject) are
                                            * tweeked */
        gf_boolean_t            child_up; /* Set to true, when child is up, and
                                           * false, when child is down */

        gf_lock_t               itable_lock;
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
        gf_seek_what_t    what;

        dict_t           *xdata;
        mode_t            umask;
        struct gf_lease   lease;
        lock_migration_info_t locklist;
        /* required for compound fops */
        gfs3_compound_req  req;
        /* last length till which iovec for compound
         * writes was processed */
        int               write_length;
        struct iovec      rsp_vector[MAX_IOVEC];
        int               rsp_count;
        struct iobuf     *rsp_iobuf;
        struct iobref    *rsp_iobref;
        compound_args_t  *args;
};


extern struct rpcsvc_program gluster_handshake_prog;
extern struct rpcsvc_program glusterfs3_3_fop_prog;

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

void
forget_inode_if_no_dentry (inode_t *inode);

int
unserialize_req_locklist (gfs3_setactivelk_req *req,
                          lock_migration_info_t *lmi);

int
serialize_rsp_dirent (gf_dirent_t *entries, gfs3_readdir_rsp *rsp);

int
serialize_rsp_direntp (gf_dirent_t *entries, gfs3_readdirp_rsp *rsp);

server_ctx_t*
server_ctx_get (client_t *client, xlator_t *xlator);
#endif /* !_SERVER_H */
