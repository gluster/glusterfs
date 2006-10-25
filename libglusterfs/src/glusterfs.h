/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
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

#ifndef _GLUSTERFS_H
#define _GLUSTERFS_H

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <netdb.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/poll.h>
#include <pthread.h>

#include "dict.h"
#include "logging.h"

#define gprintf printf

#define FUNCTION_CALLED /*\
do {                    \
     gf_log (__FILE__, GF_LOG_DEBUG, "%s called\n", __FUNCTION__); \
     } while (0) */

typedef enum {
  OP_GETATTR,
  OP_READLINK,
  OP_MKNOD,
  OP_MKDIR,
  OP_UNLINK,
  OP_RMDIR,
  OP_SYMLINK,
  OP_RENAME,
  OP_LINK,
  OP_CHMOD,
  OP_CHOWN,
  OP_TRUNCATE,
  OP_UTIME,
  OP_OPEN,
  OP_READ,
  OP_WRITE,
  OP_STATFS,
  OP_FLUSH,
  OP_RELEASE,
  OP_FSYNC,
  OP_SETXATTR,
  OP_GETXATTR,
  OP_LISTXATTR,
  OP_REMOVEXATTR,
  OP_OPENDIR,
  OP_READDIR,
  OP_RELEASEDIR,
  OP_FSYNCDIR,
  OP_INIT,
  OP_DESTROY,
  OP_ACCESS,
  OP_CREATE,
  OP_FTRUNCATE,
  OP_FGETATTR,
  OP_BULKGETATTR,
  OP_MAXVALUE
} glusterfs_fop_t;

typedef enum {
  OP_SETVOLUME,
  OP_GETVOLUME,
  OP_STATS,
  OP_SETSPEC,
  OP_GETSPEC,
  OP_LOCK,
  OP_UNLOCK,
  OP_LISTLOCKS,
  OP_NSLOOKUP,
  OP_NSUPDATE,
  OP_FSCK,
  MOP_MAXVALUE
} glusterfs_mop_t;

extern data_t * DATA_OP;
extern data_t * DATA_PATH;
extern data_t * DATA_OFFSET;
extern data_t * DATA_FD;
extern data_t * DATA_BUF;
extern data_t * DATA_COUNT;
extern data_t * DATA_FLAGS;
extern data_t * DATA_ERRNO;
extern data_t * DATA_RET;
extern data_t * DATA_MODE;
extern data_t * DATA_DEV;
extern data_t * DATA_UID;
extern data_t * DATA_GID;
extern data_t * DATA_ACTIME;
extern data_t * DATA_MODTIME;
extern data_t * DATA_LEN;

#endif /* _GLUSTERFS_H */
