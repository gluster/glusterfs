/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _SERVER_PROTOCOL_H_
#define _SERVER_PROTOCOL_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <pthread.h>

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "call-stub.h"
#include "authenticate.h"
#include "fd.h"
#include "byte-order.h"

#define DEFAULT_BLOCK_SIZE         4194304   /* 4MB */
#define DEFAULT_VOLUME_FILE_PATH   CONFDIR "/glusterfs.vol"

typedef struct _server_state server_state_t;

struct _locker {
	struct list_head  lockers;
        char             *volume;
	loc_t             loc;
	fd_t             *fd;
	pid_t             pid;
};

struct _lock_table {
	struct list_head  file_lockers;
	struct list_head  dir_lockers;
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
};

typedef struct _server_connection server_connection_t;


server_connection_t *
server_connection_get (xlator_t *this, const char *id);

void
server_connection_put (xlator_t *this, server_connection_t *conn);

int
server_connection_destroy (xlator_t *this, server_connection_t *conn);

int
server_connection_cleanup (xlator_t *this, server_connection_t *conn);

int
server_nop_cbk (call_frame_t *frame, void *cookie,
		xlator_t *this, int32_t op_ret, int32_t op_errno);


struct _volfile_ctx {
        struct _volfile_ctx *next;
        char                *key;
        uint32_t             checksum;
};

typedef struct {
        struct _volfile_ctx *volfile;

	dict_t           *auth_modules;
	transport_t      *trans;
	int32_t           max_block_size;
	int32_t           inode_lru_limit;
	pthread_mutex_t   mutex;
	struct list_head  conns;
        gf_boolean_t      verify_volfile_checksum;
} server_conf_t;


typedef enum {
        RESOLVE_MUST = 1,
        RESOLVE_NOT,
        RESOLVE_MAY,
        RESOLVE_DONTCARE,
        RESOLVE_EXACT
} server_resolve_type_t;

typedef struct {
        server_resolve_type_t  type;
        uint64_t               fd_no;
        ino_t                  ino;
        uint64_t               gen;
        ino_t                  par;
        char                  *path;
        char                  *bname;
	char                  *resolved;
        int                    op_ret;
        int                    op_errno;
} server_resolve_t;


typedef int (*server_resume_fn_t) (call_frame_t *frame, xlator_t *bound_xl);

int
resolve_and_resume (call_frame_t *frame, server_resume_fn_t fn);

struct _server_state {
	transport_t      *trans;
	xlator_t         *bound_xl;
        inode_table_t    *itable;

        server_resume_fn_t resume_fn;

	loc_t             loc;
	loc_t             loc2;
        server_resolve_t  resolve;
        server_resolve_t  resolve2;

        /* used within resolve_and_resume */
        loc_t            *loc_now;
        server_resolve_t *resolve_now;

        struct stat       stbuf;
        int               valid;

	fd_t             *fd;
	int               flags;
        int               wbflags;
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
	struct flock      flock;
        const char       *volume;
};


#endif
