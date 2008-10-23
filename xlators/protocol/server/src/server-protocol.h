/*
  Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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


#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "call-stub.h"
#include <pthread.h>
#include "authenticate.h"
#include "fd.h"
#include "byte-order.h"

#define DEFAULT_LOG_FILE   DATADIR"/log/glusterfs/glusterfsd.log"
#define DEFAULT_BLOCK_SIZE     4194304   /* 4MB */
#define GLUSTERFSD_SPEC_DIR    CONFDIR
#define GLUSTERFSD_SPEC_PATH   CONFDIR "/glusterfs-client.vol"

typedef struct _server_state server_state_t;

#define STATE(frame)        ((server_state_t *)frame->root->state)

#define BOUND_XL(frame)     ((xlator_t *) STATE (frame)->bound_xl)


struct _server_reply {
	struct list_head list;
	call_frame_t *frame;
	dict_t *reply;
	dict_t *refs;
	int32_t op;
	int32_t type;
};
typedef struct _server_reply server_reply_t;

struct _server_reply_queue {
	struct list_head list;
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	uint64_t pending;
};
typedef struct _server_reply_queue server_reply_queue_t;

struct _locker {
	struct list_head lockers;
	loc_t loc;
	fd_t *fd;
	pid_t pid;
};

struct _lock_table {
	struct list_head file_lockers;
	struct list_head dir_lockers;
	gf_lock_t lock;
	size_t count;
};

/* private structure per connection (transport object)
 * used as transport_t->xl_private
 */
struct _connection_priv {
	pthread_mutex_t lock;
	char disconnected;    /* represents a disconnected object, if set */
	fdtable_t *fdtable; 
	struct _lock_table *ltable;
	xlator_t *bound_xl;   /* to be set after an authenticated SETVOLUME */
};

typedef struct {
	server_reply_queue_t *queue;
	int32_t max_block_size;
	int32_t inode_lru_limit;
} server_conf_t;

struct _server_state {
	transport_t *trans;
	xlator_t *bound_xl;
	loc_t loc;
	loc_t loc2;
	int flags;
	int64_t fd_no;
	fd_t *fd;
	size_t size;
	off_t offset;
	mode_t mode;
	dev_t dev;
	uid_t uid;
	gid_t gid;
	int32_t dict_len;
	int32_t nr_count;
	int cmd;
	int type;
	char *name;
	int name_len;
	inode_table_t *itable;
	ino_t ino;
	ino_t ino2;
	char *path;
	char *path2;
	char *basename;
	int mask;
	char is_revalidate;
	char need_xattr;
	struct flock flock;
	struct timespec tv[2];
	char *resolved;
};


typedef struct {
	dict_t *auth_modules;
	transport_t *trans;
} server_private_t;

typedef struct _connection_priv connection_private_t;

static inline __attribute__((always_inline))
void
server_loc_wipe (loc_t *loc)
{
	if (loc->parent)
		inode_unref (loc->parent);
	if (loc->inode)
		inode_unref (loc->inode);
	if (loc->path)
		free ((char *)loc->path);
}

static inline void
free_state (server_state_t *state)
{
	transport_t *trans = NULL;	

	trans    = state->trans;

	if (state->fd)
		fd_unref (state->fd);

	transport_unref (trans);

	FREE (state);
}

int32_t
server_stub_resume (call_stub_t *stub,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    inode_t *parent);

int32_t
do_path_lookup (call_stub_t *stub,
		const loc_t *loc);

#endif
