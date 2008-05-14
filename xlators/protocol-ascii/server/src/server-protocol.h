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

#define DEFAULT_LOG_FILE   DATADIR"/log/glusterfs/glusterfsd.log"
#define DEFAULT_BLOCK_SIZE     4194304   /* 4MB */
#define GLUSTERFSD_SPEC_DIR    CONFDIR
#define GLUSTERFSD_SPEC_PATH   CONFDIR "/glusterfs-client.vol"



struct held_locks {
  struct held_locks *next;
  char *path;
};

/* private structure per socket (transport object)
   used as transport_t->xl_private
 */


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

struct server_proto_priv {
  pthread_mutex_t lock;
  char disconnected;
  fdtable_t *fdtable;
  xlator_t *bound_xl; /* to be set after an authenticated SETVOLUME */
};

typedef struct {
  server_reply_queue_t *queue;
  int32_t max_block_size;
} server_conf_t;

struct _server_state {
  transport_t *trans;
  xlator_t *bound_xl;
  inode_t *inode, *inode2;
};


typedef struct {
  dict_t *auth_modules;
  transport_t *trans;
} server_private_t;

typedef struct _server_state server_state_t;

typedef struct server_proto_priv server_proto_priv_t;
#endif
