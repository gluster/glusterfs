/*
  Copyright (c) 2006-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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


struct _server_state {
	transport_t      *trans;
	xlator_t         *bound_xl;
	loc_t             loc;
	loc_t             loc2;
	int               flags;
	fd_t             *fd;
	size_t            size;
	off_t             offset;
	mode_t            mode;
	dev_t             dev;
	uid_t             uid;
	gid_t             gid;
	size_t            nr_count;
	int               cmd;
	int               type;
	char             *name;
	int               name_len;
	inode_table_t    *itable;
	int64_t           fd_no;
	ino_t             ino;
	ino_t             par;
	ino_t             ino2;
	ino_t             par2;
	char             *path;
	char             *path2;
	char             *bname;
	char             *bname2;
	int               mask;
	char              is_revalidate;
	dict_t           *xattr_req;
	struct flock      flock;
	struct timespec   tv[2];
	char             *resolved;
        const char       *volume;
};


int
server_stub_resume (call_stub_t *stub, int32_t op_ret, int32_t op_errno,
		    inode_t *inode, inode_t *parent);

int
do_path_lookup (call_stub_t *stub, const loc_t *loc);

#endif
