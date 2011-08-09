/*
  Copyright (c) 2010-2011-2011-2011 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _NFS_RPCSVC_SOCKET_H_
#define _NFS_RPCSVC_SOCKET_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "rpcsvc.h"
#include "dict.h"
#include "logging.h"
#include "byte-order.h"
#include "common-utils.h"
#include "compat-errno.h"

#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>

#define SA(ptr)                 ((struct sockaddr *)ptr)
#define GF_RPCSVC_SOCK          "rpc-socket"
extern int
nfs_rpcsvc_socket_listen (int addrfam, char *listenhost, uint16_t listenport);

extern int
nfs_rpcsvc_socket_accept (int listenfd);

extern ssize_t
nfs_rpcsvc_socket_read (int sockfd, char *readaddr, size_t readsize);

extern ssize_t
nfs_rpcsvc_socket_write (int sockfd, char *buffer, size_t size, int *eagain);

extern int
nfs_rpcsvc_socket_peername (int sockfd, char *hostname, int hostlen);

extern int
nfs_rpcsvc_socket_peeraddr (int sockfd, char *addrstr, int addrlen,
                            struct sockaddr *returnsa, socklen_t sasize);
extern int
nfs_rpcsvc_socket_block_tx (int sockfd);

extern int
nfs_rpcsvc_socket_unblock_tx (int sockfd);
#endif
