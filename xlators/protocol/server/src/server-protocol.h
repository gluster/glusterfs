/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 

#ifndef _SERVER_PROTOCOL_H_
#define _SERVER_PROTOCOL_H_

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "call-stub.h"
#include <pthread.h>

#define DEFAULT_LOG_FILE   DATADIR"/log/glusterfs/glusterfsd.log"

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
  dict_t *open_files;
  dict_t *open_dirs;
  xlator_t *bound_xl; /* to be set after an authenticated SETVOLUME */
};

struct open_file_cleanup {
  transport_t *trans;
  char isdir;
};

struct _server_state {
  transport_t *trans;
  xlator_t *bound_xl;
  inode_t *inode, *inode2;
};

typedef struct _server_state server_state_t;

typedef struct open_file_cleanup open_file_cleanup_t;
typedef struct server_proto_priv server_proto_priv_t;
#endif
