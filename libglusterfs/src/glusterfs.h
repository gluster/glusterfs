/*
   Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU Affero General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
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

#ifndef FNM_EXTMATCH
#define FNM_EXTMATCH 0
#endif

#define GF_XATTR_PATHINFO_KEY   "trusted.glusterfs.pathinfo"
#define GF_XATTR_LINKINFO_KEY   "trusted.distribute.linkinfo"

#define ZR_FILE_CONTENT_STR     "glusterfs.file."
#define ZR_FILE_CONTENT_STRLEN 15

#define GLUSTERFS_OPEN_FD_COUNT "glusterfs.open-fd-count"
#define GLUSTERFS_INODELK_COUNT "glusterfs.inodelk-count"
#define GLUSTERFS_ENTRYLK_COUNT "glusterfs.entrylk-count"
#define GLUSTERFS_POSIXLK_COUNT "glusterfs.posixlk-count"
#define GLUSTERFS_RDMA_INLINE_THRESHOLD       (2048)
#define GLUSTERFS_RDMA_MAX_HEADER_SIZE        (228) /* (sizeof (rdma_header_t)                 \
                                                       + RDMA_MAX_SEGMENTS \
                                                       * sizeof (rdma_read_chunk_t))
                                                       */

#define GLUSTERFS_RPC_REPLY_SIZE               24

#define ZR_FILE_CONTENT_REQUEST(key) (!strncmp(key, ZR_FILE_CONTENT_STR, \
					       ZR_FILE_CONTENT_STRLEN))

/* TODO: Should we use PATH-MAX? On some systems it may save space */
#define ZR_PATH_MAX 4096

/* NOTE: add members ONLY at the end (just before _MAXVALUE) */
typedef enum {
        GF_FOP_NULL = 0,
        GF_FOP_STAT,
        GF_FOP_READLINK,
        GF_FOP_MKNOD,
        GF_FOP_MKDIR,
        GF_FOP_UNLINK,
        GF_FOP_RMDIR,
        GF_FOP_SYMLINK,
        GF_FOP_RENAME,
        GF_FOP_LINK,
        GF_FOP_TRUNCATE,
        GF_FOP_OPEN,
        GF_FOP_READ,
        GF_FOP_WRITE,
        GF_FOP_STATFS,
        GF_FOP_FLUSH,
        GF_FOP_FSYNC,      /* 15 */
        GF_FOP_SETXATTR,
        GF_FOP_GETXATTR,
        GF_FOP_REMOVEXATTR,
        GF_FOP_OPENDIR,
        GF_FOP_FSYNCDIR,
        GF_FOP_ACCESS,
        GF_FOP_CREATE,
        GF_FOP_FTRUNCATE,
        GF_FOP_FSTAT,      /* 25 */
        GF_FOP_LK,
        GF_FOP_LOOKUP,
        GF_FOP_READDIR,
        GF_FOP_INODELK,
        GF_FOP_FINODELK,
	GF_FOP_ENTRYLK,
	GF_FOP_FENTRYLK,
        GF_FOP_XATTROP,
        GF_FOP_FXATTROP,
        GF_FOP_FGETXATTR,
        GF_FOP_FSETXATTR,
        GF_FOP_RCHECKSUM,
        GF_FOP_SETATTR,
        GF_FOP_FSETATTR,
        GF_FOP_READDIRP,
        GF_FOP_FORGET,
        GF_FOP_RELEASE,
        GF_FOP_RELEASEDIR,
        GF_FOP_GETSPEC,
        GF_FOP_MAXVALUE,
} glusterfs_fop_t;


typedef enum {
        GF_MGMT_NULL = 0,
        GF_MGMT_MAXVALUE,
} glusterfs_mgmt_t;

typedef enum {
        GF_OP_TYPE_NULL = 0,
        GF_OP_TYPE_FOP,
        GF_OP_TYPE_MGMT,
        GF_OP_TYPE_MAX,
} gf_op_type_t;

struct gf_flock {
        short    l_type;
        short    l_whence;
        off_t    l_start;
        off_t    l_len;
        pid_t    l_pid;
        uint64_t l_owner;
};

/* NOTE: all the miscellaneous flags used by GlusterFS should be listed here */
typedef enum {
        GF_LK_GETLK = 0,
        GF_LK_SETLK,
        GF_LK_SETLKW,
        GF_LK_RESLK_LCK,
        GF_LK_RESLK_LCKW,
        GF_LK_RESLK_UNLCK,
        GF_LK_GETLK_FD,
} glusterfs_lk_cmds_t;


typedef enum {
        GF_LK_F_RDLCK = 0,
        GF_LK_F_WRLCK,
        GF_LK_F_UNLCK,
        GF_LK_EOL,
} glusterfs_lk_types_t;

typedef enum {
        F_RESLK_LCK = 200,
        F_RESLK_LCKW,
        F_RESLK_UNLCK,
        F_GETLK_FD,
} glusterfs_lk_recovery_cmds_t;

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

/* key value which quick read uses to get small files in lookup cbk */
#define GF_CONTENT_KEY "glusterfs.content"

struct _xlator_cmdline_option {
	struct list_head    cmd_args;
	char               *volume;
	char               *key;
	char               *value;
};
typedef struct _xlator_cmdline_option xlator_cmdline_option_t;


#define GF_OPTION_ENABLE   _gf_true
#define GF_OPTION_DISABLE  _gf_false
#define GF_OPTION_DEFERRED 2

struct _cmd_args {
	/* basic options */
	char            *volfile_server;
	char            *volfile;
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
        int              read_only;
        int              mac_compat;
	struct list_head xlator_options;  /* list of xlator_option_t */

	/* fuse options */
	int              fuse_direct_io_mode;
        int              volfile_check;
	double           fuse_entry_timeout;
	double           fuse_attribute_timeout;
	char            *volume_name;
	int              fuse_nodev;
	int              fuse_nosuid;
	char            *dump_fuse;
        pid_t            client_pid;
        int              client_pid_set;

	/* key args */
	char            *mount_point;
	char            *volfile_id;

        /* required for portmap */
        int             brick_port;
        char           *brick_name;
};
typedef struct _cmd_args cmd_args_t;


struct _glusterfs_graph {
        struct list_head          list;
        char                      graph_uuid[128];
        struct timeval            dob;
        void                     *first;
        void                     *top;   /* selected by -n */
        int                       xl_count;
        uint32_t                  volfile_checksum;
};
typedef struct _glusterfs_graph glusterfs_graph_t;


struct _glusterfs_ctx {
	cmd_args_t          cmd_args;
	char               *process_uuid;
	FILE               *pidfp;
	char                fin;
	void               *timer;
	void               *ib;
	void               *pool;
	void               *event_pool;
        void               *iobuf_pool;
	pthread_mutex_t     lock;
        size_t              page_size;
        struct list_head    graphs; /* double linked list of graphs - one per volfile parse */
        glusterfs_graph_t  *active; /* the latest graph in use */
        void               *master; /* fuse, or libglusterfsclient (however, not protocol/server) */
        void               *mgmt;   /* xlator implementing MOPs for centralized logging, volfile server */
        unsigned char       measure_latency; /* toggle switch for latency measurement */
        pthread_t           sigwaiter;
        struct mem_pool    *stub_mem_pool;
        unsigned char       cleanup_started;

};
typedef struct _glusterfs_ctx glusterfs_ctx_t;


/* If you edit this structure then, make a corresponding change in
 * globals.c in the eventstring.
 */
typedef enum {
        GF_EVENT_PARENT_UP = 1,
        GF_EVENT_POLLIN,
        GF_EVENT_POLLOUT,
        GF_EVENT_POLLERR,
        GF_EVENT_CHILD_UP,
        GF_EVENT_CHILD_DOWN,
        GF_EVENT_CHILD_CONNECTING,
        GF_EVENT_CHILD_MODIFIED,
        GF_EVENT_TRANSPORT_CLEANUP,
        GF_EVENT_TRANSPORT_CONNECTED,
        GF_EVENT_VOLFILE_MODIFIED,
        GF_EVENT_GRAPH_NEW,
        GF_EVENT_MAXVAL,
} glusterfs_event_t;

extern char *
glusterfs_strevent (glusterfs_event_t ev);

#define GF_MUST_CHECK __attribute__((warn_unused_result))

int glusterfs_graph_prepare (glusterfs_graph_t *graph, glusterfs_ctx_t *ctx);
int glusterfs_graph_destroy (glusterfs_graph_t *graph);
int glusterfs_graph_activate (glusterfs_graph_t *graph, glusterfs_ctx_t *ctx);
glusterfs_graph_t *glusterfs_graph_construct (FILE *fp);
glusterfs_graph_t *glusterfs_graph_new ();
int glusterfs_graph_reconfigure (glusterfs_graph_t *oldgraph,
                                  glusterfs_graph_t *newgraph);

#endif /* _GLUSTERFS_H */
