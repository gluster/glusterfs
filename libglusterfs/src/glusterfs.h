/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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
#include <limits.h> /* For PATH_MAX */

#include "list.h"
#include "logging.h"
#include "lkowner.h"

#define GF_YES 1
#define GF_NO  0

#ifndef O_LARGEFILE
/* savannah bug #20053, patch for compiling on darwin */
#define O_LARGEFILE 0100000 /* from bits/fcntl.h */
#endif

#ifndef O_FMODE_EXEC
/* redhat bug 843080, added from linux/fs.h */
#define O_FMODE_EXEC 040 //0x20
#endif

#ifndef O_DIRECT
/* savannah bug #20050, #20052 */
#define O_DIRECT 0 /* From asm/fcntl.h */
#endif

#ifndef O_DIRECTORY
/* FreeBSD does not need O_DIRECTORY */
#define O_DIRECTORY 0
#endif

#ifndef EBADFD
/* Mac OS X does not have EBADFD */
#define EBADFD EBADF
#endif

#ifndef FNM_EXTMATCH
#define FNM_EXTMATCH 0
#endif

#define ZR_MOUNTPOINT_OPT       "mountpoint"
#define ZR_ATTR_TIMEOUT_OPT     "attribute-timeout"
#define ZR_ENTRY_TIMEOUT_OPT    "entry-timeout"
#define ZR_NEGATIVE_TIMEOUT_OPT "negative-timeout"
#define ZR_DIRECT_IO_OPT        "direct-io-mode"
#define ZR_STRICT_VOLFILE_CHECK "strict-volfile-check"
#define ZR_DUMP_FUSE            "dump-fuse"
#define ZR_FUSE_MOUNTOPTS       "fuse-mountopts"

#define GF_XATTR_CLRLK_CMD      "glusterfs.clrlk"
#define GF_XATTR_PATHINFO_KEY   "trusted.glusterfs.pathinfo"
#define GF_XATTR_NODE_UUID_KEY  "trusted.glusterfs.node-uuid"
#define GF_XATTR_VOL_ID_KEY   "trusted.glusterfs.volume-id"
#define GF_XATTR_LOCKINFO_KEY   "trusted.glusterfs.lockinfo"
#define GF_XATTR_GET_REAL_FILENAME_KEY "glusterfs.get_real_filename:"
#define GF_XATTR_USER_PATHINFO_KEY   "glusterfs.pathinfo"
#define QUOTA_LIMIT_KEY "trusted.glusterfs.quota.limit-set"
#define VIRTUAL_QUOTA_XATTR_CLEANUP_KEY "glusterfs.quota-xattr-cleanup"

#define GF_READDIR_SKIP_DIRS       "readdir-filter-directories"

#define BD_XATTR_KEY             "user.glusterfs"

#define XATTR_IS_PATHINFO(x)  ((strncmp (x, GF_XATTR_PATHINFO_KEY,       \
                                        strlen (x)) == 0) ||             \
                              (strncmp (x, GF_XATTR_USER_PATHINFO_KEY,   \
                                         strlen (x)) == 0))
#define XATTR_IS_NODE_UUID(x) (strncmp (x, GF_XATTR_NODE_UUID_KEY,      \
                                        strlen (GF_XATTR_NODE_UUID_KEY)) == 0)
#define XATTR_IS_LOCKINFO(x) (strncmp (x, GF_XATTR_LOCKINFO_KEY,        \
                                       strlen (GF_XATTR_LOCKINFO_KEY)) == 0)

#define XATTR_IS_BD(x) (strncmp (x, BD_XATTR_KEY, strlen (BD_XATTR_KEY)) == 0)

#define GF_XATTR_LINKINFO_KEY   "trusted.distribute.linkinfo"
#define GFID_XATTR_KEY          "trusted.gfid"
#define PGFID_XATTR_KEY_PREFIX  "trusted.pgfid."
#define VIRTUAL_GFID_XATTR_KEY_STR  "glusterfs.gfid.string"
#define VIRTUAL_GFID_XATTR_KEY      "glusterfs.gfid"
#define UUID_CANONICAL_FORM_LEN 36

#define GET_ANCESTRY_PATH_KEY "glusterfs.ancestry.path"
#define GET_ANCESTRY_DENTRY_KEY "glusterfs.ancestry.dentry"

#define GLUSTERFS_INTERNAL_FOP_KEY  "glusterfs-internal-fop"

#define ZR_FILE_CONTENT_STR     "glusterfs.file."
#define ZR_FILE_CONTENT_STRLEN 15

#define GLUSTERFS_WRITE_IS_APPEND "glusterfs.write-is-append"
#define GLUSTERFS_OPEN_FD_COUNT "glusterfs.open-fd-count"
#define GLUSTERFS_INODELK_COUNT "glusterfs.inodelk-count"
#define GLUSTERFS_ENTRYLK_COUNT "glusterfs.entrylk-count"
#define GLUSTERFS_POSIXLK_COUNT "glusterfs.posixlk-count"
#define GLUSTERFS_PARENT_ENTRYLK "glusterfs.parent-entrylk"
#define GLUSTERFS_INODELK_DOM_COUNT "glusterfs.inodelk-dom-count"
#define QUOTA_SIZE_KEY "trusted.glusterfs.quota.size"
#define GFID_TO_PATH_KEY "glusterfs.gfid2path"
#define GF_XATTR_STIME_PATTERN "trusted.glusterfs.*.stime"

/* Index xlator related */
#define GF_XATTROP_INDEX_GFID "glusterfs.xattrop_index_gfid"
#define GF_BASE_INDICES_HOLDER_GFID "glusterfs.base_indicies_holder_gfid"

#define GF_GFIDLESS_LOOKUP "gfidless-lookup"
/* replace-brick and pump related internal xattrs */
#define RB_PUMP_CMD_START       "glusterfs.pump.start"
#define RB_PUMP_CMD_PAUSE       "glusterfs.pump.pause"
#define RB_PUMP_CMD_COMMIT      "glusterfs.pump.commit"
#define RB_PUMP_CMD_ABORT       "glusterfs.pump.abort"
#define RB_PUMP_CMD_STATUS      "glusterfs.pump.status"

#define GLUSTERFS_MARKER_DONT_ACCOUNT_KEY "glusters.marker.dont-account"
#define GLUSTERFS_RDMA_INLINE_THRESHOLD       (2048)
#define GLUSTERFS_RDMA_MAX_HEADER_SIZE        (228) /* (sizeof (rdma_header_t)                 \
                                                       + RDMA_MAX_SEGMENTS \
                                                       * sizeof (rdma_read_chunk_t))
                                                       */

#define GLUSTERFS_RPC_REPLY_SIZE               24

#define ZR_FILE_CONTENT_REQUEST(key) (!strncmp(key, ZR_FILE_CONTENT_STR, \
                                               ZR_FILE_CONTENT_STRLEN))

#define DEFAULT_VAR_RUN_DIRECTORY    DATADIR "/run/gluster"
#define GF_REPLICATE_TRASH_DIR          ".landfill"

/* GlusterFS's maximum supported Auxilary GIDs */
/* TODO: Keeping it to 200, so that we can fit in 2KB buffer for auth data
 * in RPC server code, if there is ever need for having more aux-gids, then
 * we have to add aux-gid in payload of actors */
#define GF_MAX_AUX_GROUPS   65536

#define GF_UUID_BUF_SIZE 50

#define GF_REBALANCE_TID_KEY     "rebalance-id"
#define GF_REMOVE_BRICK_TID_KEY  "remove-brick-id"
#define GF_REPLACE_BRICK_TID_KEY "replace-brick-id"

#define UUID_CANONICAL_FORM_LEN  36

/* Adding this here instead of any glusterd*.h files as it is also required by
 * cli
 */
#define DEFAULT_GLUSTERD_SOCKFILE             DATADIR "/run/glusterd.socket"

/* features/marker-quota also needs to have knowledge of link-files so as to
 * exclude them from accounting.
 */
#define DHT_LINKFILE_MODE        (S_ISVTX)

#define IS_DHT_LINKFILE_MODE(iabuf) ((st_mode_from_ia ((iabuf)->ia_prot, \
                                                       (iabuf)->ia_type) & ~S_IFMT)\
                                     == DHT_LINKFILE_MODE)
#define DHT_LINKFILE_STR "linkto"

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
        GF_FOP_FREMOVEXATTR,
	GF_FOP_FALLOCATE,
	GF_FOP_DISCARD,
        GF_FOP_ZEROFILL,
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
        GF_XATTROP_ADD_ARRAY64,
        GF_XATTROP_OR_ARRAY,
        GF_XATTROP_AND_ARRAY
} gf_xattrop_flags_t;


#define GF_SET_IF_NOT_PRESENT 0x1 /* default behaviour */
#define GF_SET_OVERWRITE      0x2 /* Overwrite with the buf given */
#define GF_SET_DIR_ONLY       0x4
#define GF_SET_EPOCH_TIME     0x8 /* used by afr dir lookup selfheal */

/* key value which quick read uses to get small files in lookup cbk */
#define GF_CONTENT_KEY "glusterfs.content"

struct _xlator_cmdline_option {
        struct list_head    cmd_args;
        char               *volume;
        char               *key;
        char               *value;
};
typedef struct _xlator_cmdline_option xlator_cmdline_option_t;

struct _server_cmdline {
        struct list_head  list;
        char              *volfile_server;
};
typedef struct _server_cmdline server_cmdline_t;

#define GF_OPTION_ENABLE   _gf_true
#define GF_OPTION_DISABLE  _gf_false
#define GF_OPTION_DEFERRED 2

struct _cmd_args {
        /* basic options */
        char             *volfile_server;
        server_cmdline_t *curr_server;
        /* List of backup volfile servers, including original */
        struct list_head volfile_servers;
        char             *volfile;
        char             *log_server;
        gf_loglevel_t    log_level;
        char            *log_file;
        int32_t          max_connect_attempts;
        /* advanced options */
        uint32_t         volfile_server_port;
        char            *volfile_server_transport;
        uint32_t         log_server_port;
        char            *pid_file;
        char            *sock_file;
        int              no_daemon_mode;
        char            *run_id;
        int              debug_mode;
        int              read_only;
        int              acl;
        int              selinux;
        int              enable_ino32;
        int              worm;
        int              mac_compat;
        int              fopen_keep_cache;
        int              gid_timeout;
        char             gid_timeout_set;
        int              aux_gfid_mount;
        struct list_head xlator_options;  /* list of xlator_option_t */

        /* fuse options */
        int              fuse_direct_io_mode;
        char             *use_readdirp;
        int              volfile_check;
        double           fuse_entry_timeout;
        double           fuse_negative_timeout;
        double           fuse_attribute_timeout;
        char            *volume_name;
        int              fuse_nodev;
        int              fuse_nosuid;
        char            *dump_fuse;
        pid_t            client_pid;
        int              client_pid_set;
        unsigned         uid_map_root;
        int              background_qlen;
        int              congestion_threshold;
        char             *fuse_mountopts;

        /* key args */
        char            *mount_point;
        char            *volfile_id;

        /* required for portmap */
        int             brick_port;
        char           *brick_name;
        int             brick_port2;
};
typedef struct _cmd_args cmd_args_t;


struct _glusterfs_graph {
        struct list_head          list;
        char                      graph_uuid[128];
        struct timeval            dob;
        void                     *first;
        void                     *top;   /* selected by -n */
        int                       xl_count;
        int                       id;    /* Used in logging */
        int                       used;  /* Should be set when fuse gets
                                            first CHILD_UP */
        uint32_t                  volfile_checksum;
};
typedef struct _glusterfs_graph glusterfs_graph_t;


typedef int32_t (*glusterfsd_mgmt_event_notify_fn_t) (int32_t event, void *data,
                                                      ...);
struct _glusterfs_ctx {
        cmd_args_t          cmd_args;
        char               *process_uuid;
        FILE               *pidfp;
        char                fin;
        void               *timer;
        void               *ib;
        struct call_pool   *pool;
        void               *event_pool;
        void               *iobuf_pool;
        pthread_mutex_t     lock;
        size_t              page_size;
        struct list_head    graphs; /* double linked list of graphs - one per volfile parse */
        glusterfs_graph_t  *active; /* the latest graph in use */
        void               *master; /* fuse, or libglusterfsclient (however, not protocol/server) */
        void               *mgmt;   /* xlator implementing MOPs for centralized logging, volfile server */
        void               *listener; /* listener of the commands from glusterd */
        unsigned char       measure_latency; /* toggle switch for latency measurement */
        pthread_t           sigwaiter;
        struct mem_pool    *stub_mem_pool;
        unsigned char       cleanup_started;
        int                 graph_id; /* Incremented per graph, value should
                                         indicate how many times the graph has
                                         got changed */
        pid_t               mnt_pid; /* pid of the mount agent */
        int                 process_mode; /*mode in which process is runninng*/
        struct syncenv     *env;          /* The env pointer to the synctasks */

        struct list_head    mempool_list; /* used to keep a global list of
                                             mempools, used to log details of
                                             mempool in statedump */
        char               *statedump_path;

        struct mem_pool    *dict_pool;
        struct mem_pool    *dict_pair_pool;
        struct mem_pool    *dict_data_pool;

        glusterfsd_mgmt_event_notify_fn_t notify; /* Used for xlators to make
                                                     call to fsd-mgmt */
        gf_log_handle_t     log; /* all logging related variables */

        int                 mem_acct_enable;

        int                 daemon_pipe[2];

        struct clienttable *clienttable;
};
typedef struct _glusterfs_ctx glusterfs_ctx_t;

glusterfs_ctx_t *glusterfs_ctx_new (void);

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
        GF_EVENT_TRANSLATOR_INFO,
        GF_EVENT_TRANSLATOR_OP,
        GF_EVENT_AUTH_FAILED,
        GF_EVENT_VOLUME_DEFRAG,
        GF_EVENT_PARENT_DOWN,
        GF_EVENT_MAXVAL,
} glusterfs_event_t;

struct gf_flock {
        short        l_type;
        short        l_whence;
        off_t        l_start;
        off_t        l_len;
        pid_t        l_pid;
        gf_lkowner_t l_owner;
};

#define GF_MUST_CHECK __attribute__((warn_unused_result))
/*
 * Some macros (e.g. ALLOC_OR_GOTO) set variables in function scope, but the
 * calling function might not only declare the variable to keep the macro happy
 * and not use it otherwise.  In such cases, the following can be used to
 * suppress the "set but not used" warning that would otherwise occur.
 */
#define GF_UNUSED __attribute__((unused))

int glusterfs_graph_prepare (glusterfs_graph_t *graph, glusterfs_ctx_t *ctx);
int glusterfs_graph_destroy (glusterfs_graph_t *graph);
int glusterfs_graph_activate (glusterfs_graph_t *graph, glusterfs_ctx_t *ctx);
glusterfs_graph_t *glusterfs_graph_construct (FILE *fp);
glusterfs_graph_t *glusterfs_graph_new ();
int glusterfs_graph_reconfigure (glusterfs_graph_t *oldgraph,
                                  glusterfs_graph_t *newgraph);

#endif /* _GLUSTERFS_H */
