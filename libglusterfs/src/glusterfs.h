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

#define FUNCTION_CALLED /*\
do {                    \
     gf_log (__FILE__, GF_LOG_DEBUG, "%s called\n", __FUNCTION__); \
     } while (0) */



#define GF_YES 1
#define GF_NO  0

#ifndef EBADFD
/* savannah bug #20049, patch for compiling on darwin */
#define EBADFD EBADRPC
#endif 

#ifndef O_LARGEFILE
/* savannah bug #20053, patch for compiling on darwin */
#define O_LARGEFILE 0
#endif

#ifndef O_DIRECT
/* savannah bug #20050, #20052 */
#define O_DIRECT 0 /* From asm/fcntl.h */
#endif

#ifndef O_DIRECTORY
/* FreeBSD does not need O_DIRECTORY */
#define O_DIRECTORY 0
#endif

#define GLUSTERFS_VERSION "trusted.glusterfs.version"
#define GLUSTERFS_CREATETIME "trusted.glusterfs.createtime"

/* NOTE: add members ONLY at the end (just before _MAXVALUE) */

typedef enum {
  GF_FOP_STAT,       /* 0 */
  GF_FOP_READLINK,   /* 1 */
  GF_FOP_MKNOD,      /* 2 */
  GF_FOP_MKDIR,
  GF_FOP_UNLINK,
  GF_FOP_RMDIR,      /* 5 */
  GF_FOP_SYMLINK,
  GF_FOP_RENAME,
  GF_FOP_LINK,
  GF_FOP_CHMOD,
  GF_FOP_CHOWN,      /* 10 */
  GF_FOP_TRUNCATE,
  GF_FOP_OPEN,
  GF_FOP_READ,
  GF_FOP_WRITE,
  GF_FOP_STATFS,     /* 15 */
  GF_FOP_FLUSH,
  GF_FOP_CLOSE,
  GF_FOP_FSYNC,
  GF_FOP_SETXATTR,
  GF_FOP_GETXATTR,   /* 20 */
  GF_FOP_REMOVEXATTR,
  GF_FOP_OPENDIR,
  GF_FOP_READDIR,
  GF_FOP_CLOSEDIR,
  GF_FOP_FSYNCDIR,   /* 25 */
  GF_FOP_ACCESS,
  GF_FOP_CREATE,
  GF_FOP_FTRUNCATE,
  GF_FOP_FSTAT,
  GF_FOP_LK,         /* 30 */
  GF_FOP_UTIMENS,
  GF_FOP_FCHMOD,
  GF_FOP_FCHOWN,
  GF_FOP_LOOKUP,
  GF_FOP_FORGET,     /* 35 */
  GF_FOP_WRITEDIR,
  GF_FOP_RMELEM,
  GF_FOP_INCVER,
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
  GF_MOP_FSCK,
  GF_MOP_CHECKSUM,
  GF_MOP_MAXVALUE
} glusterfs_mop_t;

/* NOTE: all the miscellaneous flags used by GlusterFS should be listed here */
typedef enum {
  GF_CREATE_ONLY_DIR = 0,
  GF_CREATE_MISSING_FILE,
} glusterfs_misc_flags_t;

typedef enum poll_type {
  SYS_POLL_TYPE_POLL,
  SYS_POLL_TYPE_EPOLL,
  SYS_POLL_TYPE_MAX
} glusterfs_poll_type_t;

struct _glusterfs_ctx {
  void *pool;
  char fin;
  char foreground;
  char *logfile;
  int32_t loglevel;
  glusterfs_poll_type_t poll_type;
  void *poll_ctx;
  void *timer;
  void *graph;
  void *ib;
  char *node_name;
  pthread_mutex_t lock;
};

typedef struct _glusterfs_ctx glusterfs_ctx_t;

typedef enum {
  GF_EVENT_PARENT_UP = 1,
  GF_EVENT_POLLIN,
  GF_EVENT_POLLERR,
  GF_EVENT_CHILD_UP,
  GF_EVENT_CHILD_DOWN
} glusterfs_event_t;

#endif /* _GLUSTERFS_H */
