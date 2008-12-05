/*
   Copyright (c) 2006, 2007 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#define CLIENT_PORT_CIELING 1023
#define DEFAULT_BLOCK_SIZE     (1048576 * 256)   /* 4MB */

#define CLIENT_CONNECTION_PRIVATE(this) (((transport_t *)(this->private))->xl_private)

#define RECEIVE_TIMEOUT(_cprivate,_current) \
		((_cprivate->last_received.tv_sec + _cprivate->transport_timeout) < _current.tv_sec)

#define SEND_TIMEOUT(_cprivate,_current) \
		((_cprivate->last_sent.tv_sec + _cprivate->transport_timeout) < _current.tv_sec)

struct saved_frame;
typedef struct saved_frame saved_frame_t;
struct client_connection_private;
typedef struct client_connection_private client_connection_private_t;

#include "stack.h"

/* This will be stored in transport_t->xl_private */
struct client_connection_private {
  pthread_mutex_t lock;
  dict_t *saved_frames;
  dict_t *saved_fds;
  inode_table_t *table;
  int64_t callid;
  int32_t transport_timeout;
  gf_timer_t *reconnect;
  char connected;
  uint64_t max_block_size;  /* maximum size of protocol data block that
			     * this client can recieve, 0 is unlimited */
  struct timeval last_sent;
  struct timeval last_received;
  gf_timer_t *timer;
};

typedef struct {
  inode_t *inode;
  fd_t *fd;
} client_local_t;

static inline void
gf_string_to_stat(char *string, struct stat *stbuf)
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
	
	stbuf->st_dev   = dev;
	stbuf->st_ino   = ino;
	stbuf->st_mode  = mode;
	stbuf->st_nlink = nlink;
	stbuf->st_uid   = uid;
	stbuf->st_gid   = gid;
	stbuf->st_rdev  = rdev;
	stbuf->st_size  = size;
	stbuf->st_blksize = blksize;
	stbuf->st_blocks  = blocks;
	
	stbuf->st_atime = atime;
	stbuf->st_mtime = mtime;
	stbuf->st_ctime = ctime;
	
#ifdef HAVE_TV_NSEC							
	stbuf->st_atim.tv_nsec = atime_nsec;	
	stbuf->st_mtim.tv_nsec = mtime_nsec;
	stbuf->st_ctim.tv_nsec = ctime_nsec;
#endif									
}

#endif
