/*
   Copyright (c) 2008-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _GUTS_PARSE_H_
#define _GUTS_PARSE_H_

#include "glusterfs.h"
#include "glusterfs-guts.h"
#include "fuse_kernel.h"
#include <fuse/fuse_lowlevel.h>
#include "list.h"

#ifndef _FUSE_OPAQUE_
#define _FUSE_OPAQUE_

struct fuse_private {
  int fd;
  struct fuse *fuse;
  struct fuse_session *se;
  struct fuse_chan *ch;
  char *mountpoint;
};

struct fuse_req {
    struct fuse_ll *f;
    uint64_t unique;
    int ctr;
    pthread_mutex_t lock;
    struct fuse_ctx ctx;
    struct fuse_chan *ch;
    int interrupted;
    union {
        struct {
            uint64_t unique;
        } i;
        struct {
            fuse_interrupt_func_t func;
            void *data;
        } ni;
    } u;
    struct fuse_req *next;
    struct fuse_req *prev;
};

struct fuse_ll {
    int debug;
    int allow_root;
    struct fuse_lowlevel_ops op;
    int got_init;
    void *userdata;
    uid_t owner;
    struct fuse_conn_info conn;
    struct fuse_req list;
    struct fuse_req interrupts;
    pthread_mutex_t lock;
    int got_destroy;
};
#endif

#define REQ_BEGIN "GUTS_REQ_BEGIN:"
#define REQ_HEADER_FULL_LEN (strlen(REQ_BEGIN) + sizeof (struct fuse_in_header) + sizeof (int32_t))

#define REP_BEGIN "GUTS_REP_BEGIN:"
#define REP_HEADER_FULL_LEN (strlen(REP_BEGIN) + sizeof (struct fuse_req) + sizeof (int32_t))

#define REQ_HEADER_LEN    (sizeof (struct fuse_in_header) + sizeof (int32_t))
#define REP_HEADER_LEN    (sizeof (struct fuse_req) + sizeof (int32_t))

#define is_request(begin) (0==strcmp(begin, REQ_BEGIN)?1:0)

typedef  void (*func_t)(struct fuse_in_header *, const void *);

typedef struct {
  func_t func; 
  const char *name;
} guts_log_t;

typedef struct {
  struct fuse_in_header header;
  int32_t arg_len;
  struct list_head list;
  void *arg;
} guts_req_t;

typedef struct {
  struct fuse_req req;
  int32_t arg_len;
  void *arg;
} guts_reply_t;

struct guts_replay_ctx {
  int32_t tio_fd;
  struct fuse_ll *guts_ll;
  dict_t *replies;
  dict_t *inodes;
  dict_t *fds;
  struct list_head requests;
  dict_t *requests_dict;
};

typedef struct guts_replay_ctx guts_replay_ctx_t;

extern guts_log_t guts_log[];

int32_t
guts_tio_init (const char *);

void
guts_req_dump (struct fuse_in_header *,
	       const void *,
	       int32_t);

guts_req_t *
guts_read_entry (guts_replay_ctx_t *ctx);

void
guts_reply_dump (fuse_req_t,
		 const void *,
		 int32_t);

guts_reply_t *
guts_read_reply (guts_replay_ctx_t *ctx,
		 uint64_t unique);

#endif /* _GUTS_PARSE_H_ */
