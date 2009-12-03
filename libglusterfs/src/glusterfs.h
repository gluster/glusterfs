/*
   Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

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

#include "list.h"
#include "logging.h"

#define GF_YES 1
#define GF_NO  0

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

#define ZR_FILE_CONTENT_STR     "glusterfs.file."
#define ZR_FILE_CONTENT_STRLEN 15

#define GLUSTERFS_OPEN_FD_COUNT "glusterfs.open-fd-count"
#define GLUSTERFS_INODELK_COUNT "glusterfs.inodelk-count"
#define GLUSTERFS_ENTRYLK_COUNT "glusterfs.entrylk-count"
#define GLUSTERFS_POSIXLK_COUNT "glusterfs.posixlk-count"

#define ZR_FILE_CONTENT_REQUEST(key) (!strncmp(key, ZR_FILE_CONTENT_STR, \
					       ZR_FILE_CONTENT_STRLEN))

/* TODO: Should we use PATH-MAX? On some systems it may save space */
#define ZR_PATH_MAX 4096    


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
        GF_FOP_TRUNCATE,
        GF_FOP_OPEN,       /* 10 */
        GF_FOP_READ,
        GF_FOP_WRITE,
        GF_FOP_STATFS,     /* 15 */
        GF_FOP_FLUSH,
        GF_FOP_FSYNC,
        GF_FOP_SETXATTR,
        GF_FOP_GETXATTR,
        GF_FOP_REMOVEXATTR,/* 20 */
        GF_FOP_OPENDIR,
        GF_FOP_GETDENTS,
        GF_FOP_FSYNCDIR,
        GF_FOP_ACCESS,
        GF_FOP_CREATE,     /* 25 */
        GF_FOP_FTRUNCATE,
        GF_FOP_FSTAT,
        GF_FOP_LK,
        GF_FOP_LOOKUP,
        GF_FOP_SETDENTS,
        GF_FOP_READDIR,
        GF_FOP_INODELK,   /* 35 */
        GF_FOP_FINODELK,
	GF_FOP_ENTRYLK,
	GF_FOP_FENTRYLK,
        GF_FOP_CHECKSUM,
        GF_FOP_XATTROP,  /* 40 */
        GF_FOP_FXATTROP,
        GF_FOP_LOCK_NOTIFY,
        GF_FOP_LOCK_FNOTIFY,
        GF_FOP_FGETXATTR,
        GF_FOP_FSETXATTR, /* 45 */
        GF_FOP_RCHECKSUM,
        GF_FOP_SETATTR,
        GF_FOP_FSETATTR,
        GF_FOP_READDIRP,
        GF_FOP_MAXVALUE,
} glusterfs_fop_t;

/* NOTE: add members ONLY at the end (just before _MAXVALUE) */
typedef enum {
        GF_MOP_SETVOLUME, /* 0 */
        GF_MOP_GETVOLUME, /* 1 */
        GF_MOP_STATS,
        GF_MOP_SETSPEC,
        GF_MOP_GETSPEC,
	GF_MOP_PING,      /* 5 */
        GF_MOP_LOG,
        GF_MOP_NOTIFY,
        GF_MOP_MAXVALUE   /* 8 */
} glusterfs_mop_t;

typedef enum {
	GF_CBK_FORGET,      /* 0 */
	GF_CBK_RELEASE,     /* 1 */
	GF_CBK_RELEASEDIR,  /* 2 */
	GF_CBK_MAXVALUE     /* 3 */
} glusterfs_cbk_t;

typedef enum {
        GF_OP_TYPE_FOP_REQUEST = 1,
        GF_OP_TYPE_MOP_REQUEST,
	GF_OP_TYPE_CBK_REQUEST,
        GF_OP_TYPE_FOP_REPLY,
        GF_OP_TYPE_MOP_REPLY,
	GF_OP_TYPE_CBK_REPLY
} glusterfs_op_type_t;

/* NOTE: all the miscellaneous flags used by GlusterFS should be listed here */
typedef enum {
        GF_LK_GETLK = 0,
        GF_LK_SETLK,
        GF_LK_SETLKW,
} glusterfs_lk_cmds_t;

typedef enum {
        GF_LK_F_RDLCK = 0,
        GF_LK_F_WRLCK,
        GF_LK_F_UNLCK
} glusterfs_lk_types_t;

typedef enum {
        GF_LOCK_POSIX, 
        GF_LOCK_INTERNAL
} gf_lk_domain_t;

typedef enum {
	ENTRYLK_LOCK,
	ENTRYLK_UNLOCK,
	ENTRYLK_LOCK_NB
} entrylk_cmd;

typedef enum {
	ENTRYLK_RDLCK,
	ENTRYLK_WRLCK
} entrylk_type;

typedef enum {
        GF_GET_ALL = 1,
        GF_GET_DIR_ONLY,
        GF_GET_SYMLINK_ONLY,
        GF_GET_REGULAR_FILES_ONLY,
} glusterfs_getdents_flags_t;

typedef enum {
	GF_XATTROP_ADD_ARRAY,
} gf_xattrop_flags_t;

#define GF_SET_IF_NOT_PRESENT 0x1 /* default behaviour */
#define GF_SET_OVERWRITE      0x2 /* Overwrite with the buf given */
#define GF_SET_DIR_ONLY       0x4
#define GF_SET_EPOCH_TIME     0x8 /* used by afr dir lookup selfheal */

/* Directory into which replicate self-heal will move deleted files and
   directories into. The storage/posix janitor thread will periodically
   clean up this directory */

#define GF_REPLICATE_TRASH_DIR          ".landfill"

struct _xlator_cmdline_option {
	struct list_head cmd_args;
	char *volume;
	char *key;
	char *value;
};
typedef struct _xlator_cmdline_option xlator_cmdline_option_t;

struct _cmd_args {
	/* basic options */
	char            *volfile_server;
	char            *volume_file;
        char            *log_server;
	gf_loglevel_t    log_level;
	char            *log_file;
        int32_t          max_connect_attempts;
	/* advanced options */
	uint32_t         volfile_server_port;
	char            *volfile_server_transport;
        uint32_t         log_server_port;
	char            *pid_file;
	int              no_daemon_mode;
	char            *run_id;
	int              debug_mode;
	struct list_head xlator_options;  /* list of xlator_option_t */

	/* fuse options */
	int              fuse_direct_io_mode_flag;
        int              volfile_check;
	double           fuse_entry_timeout;
	double           fuse_attribute_timeout;
	char            *volume_name;
	int              non_local;       /* Used only by darwin os, 
					     used for '-o local' option */
	char            *icon_name;       /* This string will appear as 
					     Desktop icon name when mounted
					     on darwin */
	int              fuse_nodev;
	int              fuse_nosuid;

	/* key args */
	char            *mount_point;
	char            *volfile_id;
};
typedef struct _cmd_args cmd_args_t;

struct _glusterfs_ctx {
	cmd_args_t         cmd_args;
	char              *process_uuid;
	FILE              *specfp;
	FILE              *pidfp;
	char               fin;
	void              *timer;
	void              *ib;
	void              *pool;
	void              *graph;
	void              *top; /* either fuse or server protocol */
	void              *event_pool;
        void              *iobuf_pool;
	pthread_mutex_t    lock;
	int                xl_count;
        uint32_t           volfile_checksum;
        size_t             page_size;
};

typedef struct _glusterfs_ctx glusterfs_ctx_t;

typedef enum {
  GF_EVENT_PARENT_UP = 1,
  GF_EVENT_POLLIN,
  GF_EVENT_POLLOUT,
  GF_EVENT_POLLERR,
  GF_EVENT_CHILD_UP,
  GF_EVENT_CHILD_DOWN,
  GF_EVENT_CHILD_CONNECTING,
  GF_EVENT_TRANSPORT_CLEANUP,
  GF_EVENT_TRANSPORT_CONNECTED,
  GF_EVENT_VOLFILE_MODIFIED,
} glusterfs_event_t;

#define GF_MUST_CHECK __attribute__((warn_unused_result))

#endif /* _GLUSTERFS_H */
