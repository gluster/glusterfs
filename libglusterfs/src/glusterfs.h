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

#ifndef _GLUSTERFS_H
#define _GLUSTERFS_H

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

#define FUNCTION_CALLED /*\
do {                    \
     gf_log (__FILE__, GF_LOG_DEBUG, "%s called\n", __FUNCTION__); \
     } while (0) */


/* NOTE: add members ONLY at the end (just before _MAXVALUE) */
typedef enum {
  GF_FOP_GETATTR,
  GF_FOP_READLINK,
  GF_FOP_MKNOD,
  GF_FOP_MKDIR,
  GF_FOP_UNLINK,
  GF_FOP_RMDIR,
  GF_FOP_SYMLINK,
  GF_FOP_RENAME,
  GF_FOP_LINK,
  GF_FOP_CHMOD,
  GF_FOP_CHOWN,
  GF_FOP_TRUNCATE,
  GF_FOP_UTIMES,
  GF_FOP_OPEN,
  GF_FOP_READ,
  GF_FOP_WRITE,
  GF_FOP_STATFS,
  GF_FOP_FLUSH,
  GF_FOP_RELEASE,
  GF_FOP_FSYNC,
  GF_FOP_SETXATTR,
  GF_FOP_GETXATTR,
  GF_FOP_LISTXATTR,
  GF_FOP_REMOVEXATTR,
  GF_FOP_OPENDIR,
  GF_FOP_READDIR,
  GF_FOP_RELEASEDIR,
  GF_FOP_FSYNCDIR,
  GF_FOP_ACCESS,
  GF_FOP_CREATE,
  GF_FOP_FTRUNCATE,
  GF_FOP_FGETATTR,
  GF_FOP_LK,
  GF_FOP_MAXVALUE
} glusterfs_fop_t;

/* NOTE: add members ONLY at the end (just before _MAXVALUE) */
typedef enum {
  GF_MOP_SETVOLUME,
  GF_MOP_GETVOLUME,
  GF_MOP_STATS,
  GF_MOP_SETSPEC,
  GF_MOP_GETSPEC,
  GF_MOP_LOCK,
  GF_MOP_UNLOCK,
  GF_MOP_LISTLOCKS,
  GF_MOP_NSLOOKUP,
  GF_MOP_NSUPDATE,
  GF_MOP_FSCK,
  GF_MOP_MAXVALUE
} glusterfs_mop_t;

#endif /* _GLUSTERFS_H */
