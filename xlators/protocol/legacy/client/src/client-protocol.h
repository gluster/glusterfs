/*
  Copyright (c) 2006-2011 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _CLIENT_PROTOCOL_H
#define _CLIENT_PROTOCOL_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <arpa/inet.h>
#include "inode.h"
#include "timer.h"
#include "byte-order.h"
#include "saved-frames.h"

#define CLIENT_PORT_CEILING        1023

#define GF_CLIENT_INODE_SELF   0
#define GF_CLIENT_INODE_PARENT 1

#define CLIENT_CONF(this) ((client_conf_t *)(this->private))

#define RECEIVE_TIMEOUT(_cprivate,_current)     \
        ((_cprivate->last_received.tv_sec +     \
          _cprivate->frame_timeout) <           \
         _current.tv_sec)

#define SEND_TIMEOUT(_cprivate,_current)        \
        ((_cprivate->last_sent.tv_sec +         \
          _cprivate->frame_timeout) <           \
         _current.tv_sec)

enum {
	CHANNEL_BULK = 0,
	CHANNEL_LOWLAT = 1,
	CHANNEL_MAX
};

#define CLIENT_CHANNEL client_channel

struct client_connection;
typedef struct client_connection client_connection_t;

#include "stack.h"
#include "xlator.h"
#include "transport.h"
#include "protocol.h"

typedef struct _client_fd_ctx {
        struct list_head  sfd_pos;      /*  Stores the reference to this
                                            fd's position in the saved_fds list.
                                        */
        int64_t           remote_fd;
        inode_t          *inode;
        uint64_t          ino;
        uint64_t          gen;
        char              is_dir;
        char              released;
        int32_t           flags;
        int32_t           wbflags;
} client_fd_ctx_t;

struct _client_conf {
	transport_t         *transport[CHANNEL_MAX];
	struct list_head     saved_fds;
	struct timeval       last_sent;
	struct timeval       last_received;
	pthread_mutex_t      mutex;
        int                  connecting;
};
typedef struct _client_conf client_conf_t;

/* This will be stored in transport_t->xl_private */
struct client_connection {
	pthread_mutex_t      lock;
	uint64_t             callid;
	struct saved_frames *saved_frames;
	int32_t              frame_timeout;
	int32_t              ping_started;
	int32_t              ping_timeout;
	int32_t              transport_activity;
	gf_timer_t          *reconnect;
	char                 connected;
	uint64_t             max_block_size;
	gf_timer_t          *timer;
	gf_timer_t          *ping_timer;
};

typedef struct {
	loc_t              loc;
	loc_t              loc2;
	fd_t              *fd;
        gf_op_t            op;
        client_fd_ctx_t   *fdctx;
        uint32_t           flags;
        uint32_t           wbflags;
} client_local_t;


static inline void
gf_string_to_stat(char *string, struct iatt *stbuf)
{
	uint64_t dev        = 0;
	uint64_t ino        = 0;
	uint32_t mode       = 0;
	uint32_t nlink      = 0;
	uint32_t uid        = 0;
	uint32_t gid        = 0;
	uint64_t rdev       = 0;
	uint64_t size       = 0;
	uint32_t blksize    = 0;
	uint64_t blocks     = 0;
	uint32_t atime      = 0;
	uint32_t atime_nsec = 0;
	uint32_t mtime      = 0;
	uint32_t mtime_nsec = 0;
	uint32_t ctime      = 0;
	uint32_t ctime_nsec = 0;

	sscanf (string, GF_STAT_PRINT_FMT_STR,
		&dev,
		&ino,
		&mode,
		&nlink,
		&uid,
		&gid,
		&rdev,
		&size,
		&blksize,
		&blocks,
		&atime,
		&atime_nsec,
		&mtime,
		&mtime_nsec,
		&ctime,
		&ctime_nsec);

	stbuf->ia_gen   = dev;
	stbuf->ia_ino   = ino;
	stbuf->ia_prot  = ia_prot_from_st_mode (mode);
        stbuf->ia_type  = ia_type_from_st_mode (mode);
	stbuf->ia_nlink = nlink;
	stbuf->ia_uid   = uid;
	stbuf->ia_gid   = gid;
	stbuf->ia_rdev  = rdev;
	stbuf->ia_size  = size;
	stbuf->ia_blksize = blksize;
	stbuf->ia_blocks  = blocks;

	stbuf->ia_atime = atime;
	stbuf->ia_mtime = mtime;
	stbuf->ia_ctime = ctime;

	stbuf->ia_atime_nsec = atime_nsec;
	stbuf->ia_mtime_nsec = mtime_nsec;
	stbuf->ia_ctime_nsec = ctime_nsec;
}

#endif
