/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _SERVER_H
#define _SERVER_H

#include <pthread.h>

#include "rpcsvc.h"

#include "fd.h"
#include "protocol-common.h"
#include "server-mem-types.h"
#include "glusterfs3.h"

#define DEFAULT_BLOCK_SIZE         4194304   /* 4MB */
#define DEFAULT_VOLUME_FILE_PATH   CONFDIR "/glusterfs.vol"

typedef struct _server_state server_state_t;

struct _locker {
        struct list_head  lockers;
        char             *volume;
        loc_t             loc;
        fd_t             *fd;
        uint64_t          owner;
        pid_t             pid;
};

struct _lock_table {
        struct list_head  inodelk_lockers;
        struct list_head  entrylk_lockers;
        gf_lock_t         lock;
        size_t            count;
};

/* private structure per connection (transport object)
 * used as transport_t->xl_private
 */
struct _server_connection {
        struct list_head    list;
        char               *id;
        int                 ref;
        int                 active_transports;
        pthread_mutex_t     lock;
        char                disconnected;
        fdtable_t          *fdtable;
        struct _lock_table *ltable;
        xlator_t           *bound_xl;
        xlator_t           *this;
};

typedef struct _server_connection server_connection_t;


server_connection_t *
server_connection_get (xlator_t *this, const char *id);

void
server_connection_put (xlator_t *this, server_connection_t *conn);

int
server_connection_cleanup (xlator_t *this, server_connection_t *conn);

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
        char                   *conf_dir;
        struct _volfile_ctx    *volfile;

        dict_t                 *auth_modules;
        pthread_mutex_t         mutex;
        struct list_head        conns;
        struct list_head        xprt_list;
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
        uint64_t               fd_no;
        u_char                 gfid[16];
        u_char                 pargfid[16];
        char                  *path;
        char                  *bname;
        int                    op_ret;
        int                    op_errno;
        loc_t                  deep_loc;
} server_resolve_t;


typedef int (*server_resume_fn_t) (call_frame_t *frame, xlator_t *bound_xl);

int
resolve_and_resume (call_frame_t *frame, server_resume_fn_t fn);

struct _server_state {
        server_connection_t  *conn;
        rpc_transport_t      *xprt;
        inode_table_t        *itable;

        server_resume_fn_t    resume_fn;

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
        int               flags;
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
        struct gf_flock      flock;
        const char       *volume;
        dir_entry_t      *entry;
};

extern struct rpcsvc_program gluster_handshake_prog;
extern struct rpcsvc_program glusterfs3_1_fop_prog;
extern struct rpcsvc_program gluster_ping_prog;

int
server_submit_reply (call_frame_t *frame, rpcsvc_request_t *req, void *arg,
                     struct iovec *payload, int payloadcount,
                     struct iobref *iobref, xdrproc_t xdrproc);

int gf_server_check_setxattr_cmd (call_frame_t *frame, dict_t *dict);
int gf_server_check_getxattr_cmd (call_frame_t *frame, const char *name);

#endif /* !_SERVER_H */
