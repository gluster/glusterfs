/*
  Copyright (c) 2008-2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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
#include <limits.h> /* For PATH_MAX */
#include <openssl/sha.h>

#include "glusterfs-fops.h" /* generated XDR values for FOPs */

#ifndef IXDR_GET_LONG
#define IXDR_GET_LONG(buf) ((long)IXDR_GET_U_INT32(buf))
#endif
#ifndef IXDR_PUT_LONG
#define IXDR_PUT_LONG(buf, v) ((long)IXDR_PUT_INT32(buf, (long)(v)))
#endif
#ifndef IXDR_GET_U_LONG
#define IXDR_GET_U_LONG(buf) ((u_long)IXDR_GET_LONG(buf))
#endif
#ifndef IXDR_PUT_U_LONG
#define IXDR_PUT_U_LONG(buf, v) IXDR_PUT_LONG(buf, (long)(v))
#endif

#include "list.h"
#include "locking.h"
#include "logging.h"
#include "lkowner.h"
#include "compat-uuid.h"
#include "refcount.h"
#include "atomic.h"

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

/*gets max-offset on all architectures correctly*/
#define GF_OFF_MAX ((1ULL << (sizeof(off_t) * 8 - 1)) - 1ULL)

#define GLUSTERD_MAX_SNAP_NAME  255
#define GLUSTERFS_SOCKET_LISTEN_BACKLOG  10
#define ZR_MOUNTPOINT_OPT       "mountpoint"
#define ZR_ATTR_TIMEOUT_OPT     "attribute-timeout"
#define ZR_ENTRY_TIMEOUT_OPT    "entry-timeout"
#define ZR_NEGATIVE_TIMEOUT_OPT "negative-timeout"
#define ZR_DIRECT_IO_OPT        "direct-io-mode"
#define ZR_STRICT_VOLFILE_CHECK "strict-volfile-check"
#define ZR_DUMP_FUSE            "dump-fuse"
#define ZR_FUSE_MOUNTOPTS       "fuse-mountopts"
#define IO_THREADS_QUEUE_SIZE_KEY "io-thread-queue-size"

#define GF_XATTR_CLRLK_CMD      "glusterfs.clrlk"
#define GF_XATTR_PATHINFO_KEY   "trusted.glusterfs.pathinfo"
#define GF_XATTR_NODE_UUID_KEY  "trusted.glusterfs.node-uuid"
#define GF_XATTR_LIST_NODE_UUIDS_KEY "trusted.glusterfs.list-node-uuids"
#define GF_REBAL_FIND_LOCAL_SUBVOL "glusterfs.find-local-subvol"
#define GF_REBAL_OLD_FIND_LOCAL_SUBVOL "glusterfs.old-find-local-subvol"
#define GF_XATTR_VOL_ID_KEY   "trusted.glusterfs.volume-id"
#define GF_XATTR_LOCKINFO_KEY   "trusted.glusterfs.lockinfo"
#define GF_META_LOCK_KEY        "glusterfs.lock-migration-meta-lock"
#define GF_META_UNLOCK_KEY      "glusterfs.lock-migration-meta-unlock"
#define GF_XATTR_GET_REAL_FILENAME_KEY "glusterfs.get_real_filename:"
#define GF_XATTR_USER_PATHINFO_KEY   "glusterfs.pathinfo"
#define GF_INTERNAL_IGNORE_DEEM_STATFS "ignore-deem-statfs"
#define GF_XATTR_IOSTATS_DUMP_KEY "trusted.io-stats-dump"

#define GF_READDIR_SKIP_DIRS       "readdir-filter-directories"
#define GF_MDC_LOADED_KEY_NAMES     "glusterfs.mdc.loaded.key.names"

#define BD_XATTR_KEY             "user.glusterfs"
#define GF_PREOP_PARENT_KEY      "glusterfs.preop.parent.key"
#define GF_PREOP_CHECK_FAILED    "glusterfs.preop.check.failed"

#define XATTR_IS_PATHINFO(x)  ((strncmp (x, GF_XATTR_PATHINFO_KEY,       \
                                        strlen (x)) == 0) ||             \
                              (strncmp (x, GF_XATTR_USER_PATHINFO_KEY,   \
                                         strlen (x)) == 0))
#define XATTR_IS_NODE_UUID(x) (strncmp (x, GF_XATTR_NODE_UUID_KEY,      \
                                        strlen (GF_XATTR_NODE_UUID_KEY)) == 0)
#define XATTR_IS_NODE_UUID_LIST(x) (strncmp (x, GF_XATTR_LIST_NODE_UUIDS_KEY, \
                                    strlen (GF_XATTR_LIST_NODE_UUIDS_KEY)) == 0)
#define XATTR_IS_LOCKINFO(x) (strncmp (x, GF_XATTR_LOCKINFO_KEY,        \
                                       strlen (GF_XATTR_LOCKINFO_KEY)) == 0)

#define XATTR_IS_BD(x) (strncmp (x, BD_XATTR_KEY, strlen (BD_XATTR_KEY)) == 0)

#define GF_XATTR_LINKINFO_KEY   "trusted.distribute.linkinfo"
#define GFID_XATTR_KEY          "trusted.gfid"
#define PGFID_XATTR_KEY_PREFIX  "trusted.pgfid."
#define GFID2PATH_VIRT_XATTR_KEY  "glusterfs.gfidtopath"
#define GFID2PATH_XATTR_KEY_PREFIX  "trusted.gfid2path."
#define GFID2PATH_XATTR_KEY_PREFIX_LENGTH 18
#define VIRTUAL_GFID_XATTR_KEY_STR  "glusterfs.gfid.string"
#define VIRTUAL_GFID_XATTR_KEY      "glusterfs.gfid"
#define GF_XATTR_MDATA_KEY  "trusted.glusterfs.mdata"
#define UUID_CANONICAL_FORM_LEN 36

#define GET_ANCESTRY_PATH_KEY "glusterfs.ancestry.path"
#define GET_ANCESTRY_DENTRY_KEY "glusterfs.ancestry.dentry"

#define BITROT_DEFAULT_CURRENT_VERSION  (unsigned long)1
#define BITROT_DEFAULT_SIGNING_VERSION  (unsigned long)0

/* on-disk object signature keys */
#define BITROT_OBJECT_BAD_KEY       "trusted.bit-rot.bad-file"
#define BITROT_CURRENT_VERSION_KEY  "trusted.bit-rot.version"
#define BITROT_SIGNING_VERSION_KEY  "trusted.bit-rot.signature"

/* globally usable bad file marker */
#define GLUSTERFS_BAD_INODE         "glusterfs.bad-inode"

/* on-disk size of signing xattr (not the signature itself) */
#define BITROT_SIGNING_XATTR_SIZE_KEY  "trusted.glusterfs.bit-rot.size"

/* GET/SET object signature */
#define GLUSTERFS_GET_OBJECT_SIGNATURE "trusted.glusterfs.get-signature"
#define GLUSTERFS_SET_OBJECT_SIGNATURE "trusted.glusterfs.set-signature"

/* operation needs to be durable on-disk */
#define GLUSTERFS_DURABLE_OP           "trusted.glusterfs.durable-op"

/* key for version exchange b/w bitrot stub and changelog */
#define GLUSTERFS_VERSION_XCHG_KEY     "glusterfs.version.xchg"

#define GLUSTERFS_INTERNAL_FOP_KEY  "glusterfs-internal-fop"

/* GlusterFS Internal FOP Indicator flags
 * (To pass information on the context in which a paritcular
 *  fop is performed between translators)
 * The presence of a particular flag must be treated as an
 * indicator of the context, however the flag is added only in
 * a scenario where there is a need for such context across translators.
 * So it cannot be an absolute information on context.
 */
#define GF_INTERNAL_CTX_KEY  "glusterfs.internal-ctx"

/*
 * Always append entries to end of the enum, do not delete entries.
 * Currently dict_set_flag allows to set upto 256 flag, if the enum
 * needs to grow beyond this dict_set_flag has to be changed accordingly
 */
enum gf_internal_fop_indicator {
        GF_DHT_HEAL_DIR       /* Index 0 in bit array*/
};

/* Todo:
 * Add GF_FOP_LINK_FILE         0x2ULL
 * address GLUSTERFS_MARKER_DONT_ACCOUNT_KEY and
 * GLUSTERFS_INTERNAL_FOP_KEY with this flag
 */


#define DHT_CHANGELOG_RENAME_OP_KEY   "changelog.rename-op"

#define GLUSTERFS_WRITE_IS_APPEND "glusterfs.write-is-append"
#define GLUSTERFS_WRITE_UPDATE_ATOMIC "glusterfs.write-update-atomic"
#define GLUSTERFS_OPEN_FD_COUNT "glusterfs.open-fd-count"
#define GLUSTERFS_ACTIVE_FD_COUNT "glusterfs.open-active-fd-count"
#define GLUSTERFS_INODELK_COUNT "glusterfs.inodelk-count"
#define GLUSTERFS_ENTRYLK_COUNT "glusterfs.entrylk-count"
#define GLUSTERFS_POSIXLK_COUNT "glusterfs.posixlk-count"
#define GLUSTERFS_PARENT_ENTRYLK "glusterfs.parent-entrylk"
#define GLUSTERFS_INODELK_DOM_COUNT "glusterfs.inodelk-dom-count"
#define GFID_TO_PATH_KEY "glusterfs.gfid2path"
#define GF_XATTR_STIME_PATTERN "trusted.glusterfs.*.stime"
#define GF_XATTR_XTIME_PATTERN "trusted.glusterfs.*.xtime"
#define GF_XATTR_TRIGGER_SYNC "glusterfs.geo-rep.trigger-sync"

/* quota xattrs */
#define QUOTA_SIZE_KEY "trusted.glusterfs.quota.size"
#define QUOTA_LIMIT_KEY "trusted.glusterfs.quota.limit-set"
#define QUOTA_LIMIT_OBJECTS_KEY "trusted.glusterfs.quota.limit-objects"
#define VIRTUAL_QUOTA_XATTR_CLEANUP_KEY "glusterfs.quota-xattr-cleanup"
#define QUOTA_READ_ONLY_KEY "trusted.glusterfs.quota.read-only"

/* afr related */
#define AFR_XATTR_PREFIX "trusted.afr"

/* Index xlator related */
#define GF_XATTROP_INDEX_GFID "glusterfs.xattrop_index_gfid"
#define GF_XATTROP_ENTRY_CHANGES_GFID "glusterfs.xattrop_entry_changes_gfid"
#define GF_XATTROP_INDEX_COUNT "glusterfs.xattrop_index_count"
#define GF_XATTROP_DIRTY_GFID "glusterfs.xattrop_dirty_gfid"
#define GF_XATTROP_DIRTY_COUNT "glusterfs.xattrop_dirty_count"
#define GF_XATTROP_ENTRY_IN_KEY "glusterfs.xattrop-entry-create"
#define GF_XATTROP_ENTRY_OUT_KEY "glusterfs.xattrop-entry-delete"
#define GF_INDEX_IA_TYPE_GET_REQ "glusterfs.index-ia-type-get-req"
#define GF_INDEX_IA_TYPE_GET_RSP "glusterfs.index-ia-type-get-rsp"

#define GF_HEAL_INFO "glusterfs.heal-info"
#define GF_AFR_HEAL_SBRAIN "glusterfs.heal-sbrain"
#define GF_AFR_SBRAIN_STATUS "replica.split-brain-status"
#define GF_AFR_SBRAIN_CHOICE "replica.split-brain-choice"
#define GF_AFR_SPB_CHOICE_TIMEOUT "replica.split-brain-choice-timeout"
#define GF_AFR_SBRAIN_RESOLVE "replica.split-brain-heal-finalize"
#define GF_AFR_ADD_BRICK "trusted.add-brick"
#define GF_AFR_REPLACE_BRICK "trusted.replace-brick"
#define GF_AFR_DIRTY "trusted.afr.dirty"
#define GF_XATTROP_ENTRY_OUT "glusterfs.xattrop-entry-delete"
#define GF_XATTROP_PURGE_INDEX "glusterfs.xattrop-purge-index"

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

#define STARTING_EVENT_THREADS                 1

#define DEFAULT_VAR_RUN_DIRECTORY        DATADIR "/run/gluster"
#define DEFAULT_GLUSTERFSD_MISC_DIRETORY DATADIR "/lib/misc/glusterfsd"
#ifdef GF_LINUX_HOST_OS
#define GLUSTERD_DEFAULT_WORKDIR DATADIR "/lib/glusterd"
#else
#define GLUSTERD_DEFAULT_WORKDIR DATADIR "/db/glusterd"
#endif
#define GF_REPLICATE_TRASH_DIR           ".landfill"

/* GlusterFS's maximum supported Auxiliary GIDs */
#define GF_MAX_AUX_GROUPS   65535

#define GF_UUID_BUF_SIZE 50
#define GF_UUID_BNAME_BUF_SIZE (320) /* (64 + 256) */

#define GF_REBALANCE_TID_KEY     "rebalance-id"
#define GF_REMOVE_BRICK_TID_KEY  "remove-brick-id"
#define GF_TIER_TID_KEY          "tier-id"
#define GF_TIER_ENABLED          "tier-enabled"

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
#define DHT_COMMITHASH_STR "commithash"

#define DHT_SKIP_NON_LINKTO_UNLINK  "unlink-only-if-dht-linkto-file"
#define TIER_SKIP_NON_LINKTO_UNLINK  "unlink-only-if-tier-linkto-file"
#define TIER_LINKFILE_GFID           "tier-linkfile-gfid"
#define DHT_SKIP_OPEN_FD_UNLINK     "dont-unlink-for-open-fd"
#define DHT_IATT_IN_XDATA_KEY       "dht-get-iatt-in-xattr"
#define DHT_MODE_IN_XDATA_KEY       "dht-get-mode-in-xattr"
#define GET_LINK_COUNT              "get-link-count"
#define GF_GET_SIZE                 "get-size"

/*CTR and Marker requires inode dentry link count from posix*/
#define GF_RESPONSE_LINK_COUNT_XDATA "gf_response_link_count"
#define GF_REQUEST_LINK_COUNT_XDATA  "gf_request_link_count"

#define CTR_ATTACH_TIER_LOOKUP    "ctr_attach_tier_lookup"

#define CLIENT_CMD_CONNECT    "trusted.glusterfs.client-connect"
#define CLIENT_CMD_DISCONNECT "trusted.glusterfs.client-disconnect"

#define GF_LOG_LRU_BUFSIZE_DEFAULT 5
#define GF_LOG_LRU_BUFSIZE_MIN 0
#define GF_LOG_LRU_BUFSIZE_MAX 20
#define GF_LOG_LRU_BUFSIZE_MIN_STR "0"
#define GF_LOG_LRU_BUFSIZE_MAX_STR "20"

#define GF_LOG_FLUSH_TIMEOUT_DEFAULT 120
#define GF_LOG_FLUSH_TIMEOUT_MIN 30
#define GF_LOG_FLUSH_TIMEOUT_MAX 300
#define GF_LOG_FLUSH_TIMEOUT_MIN_STR "30"
#define GF_LOG_FLUSH_TIMEOUT_MAX_STR "300"
#define GF_LOG_LOCALTIME_DEFAULT 0

#define GF_BACKTRACE_LEN        4096
#define GF_BACKTRACE_FRAME_COUNT 7

#define GF_LK_ADVISORY 0 /* maps to GLFS_LK_ADVISORY from libgfapi*/
#define GF_LK_MANDATORY 1 /* maps to GLFS_LK_MANDATORY from libgfapi*/
#define GF_LOCK_MODE "glusterfs.lk.lkmode"

#define GF_CHECK_XATTR_KEY_AND_GOTO(key, cmpkey, errval, lbl)   \
        do {                                                    \
                if (key && strcmp (key, cmpkey) == 0) {         \
                        errval = -EINVAL;                       \
                        goto lbl;                               \
                }                                               \
        } while (0)                                             \

#define GF_CS_OBJECT_SIZE  "trusted.glusterfs.cs.object_size"

#define GF_CS_OBJECT_UPLOAD_COMPLETE "trusted.glusterfs.csou.complete"
#define GF_CS_OBJECT_REMOTE "trusted.glusterfs.cs.remote"
#define GF_CS_OBJECT_DOWNLOADING "trusted.glusterfs.cs.downloading"
#define GF_CS_OBJECT_DOWNLOADED "trusted.glusterfs.cs.downloaded"
#define GF_CS_OBJECT_STATUS "trusted.glusterfs.cs.status"
#define GF_CS_OBJECT_REPAIR "trusted.glusterfs.cs.repair"

typedef enum {
        GF_CS_LOCAL = 1,
        GF_CS_REMOTE = 2,
        GF_CS_REPAIR = 4,
        GF_CS_DOWNLOADING = 8,
        GF_CS_ERROR = 16,
} gf_cs_obj_state;

typedef enum {
        GF_FOP_PRI_UNSPEC = -1,         /* Priority not specified */
        GF_FOP_PRI_HI = 0,              /* low latency */
        GF_FOP_PRI_NORMAL,              /* normal */
        GF_FOP_PRI_LO,                  /* bulk */
        GF_FOP_PRI_LEAST,               /* least */
        GF_FOP_PRI_MAX,                 /* Highest */
} gf_fop_pri_t;

static const char * const FOP_PRI_STRINGS[] = {
        "HIGH",
        "NORMAL",
        "LOW",
        "LEAST"
};

static inline const char *fop_pri_to_string (gf_fop_pri_t pri)
{
        if (pri < 0)
                return "UNSPEC";

        if (pri >= GF_FOP_PRI_MAX)
                return "INVALID";

        return FOP_PRI_STRINGS[pri];
}

const char *fop_enum_to_pri_string (glusterfs_fop_t fop);

#define GF_SET_IF_NOT_PRESENT 0x1 /* default behaviour */
#define GF_SET_OVERWRITE      0x2 /* Overwrite with the buf given */
#define GF_SET_DIR_ONLY       0x4
#define GF_SET_EPOCH_TIME     0x8 /* used by afr dir lookup selfheal */
#define GF_AUXILLARY_PARGFID  0xd /* RIO dummy parent gfid */

/* key value which quick read uses to get small files in lookup cbk */
#define GF_CONTENT_KEY "glusterfs.content"

struct _xlator_cmdline_option {
        struct list_head   cmd_args;
        char              *volume;
        char              *key;
        char              *value;
};
typedef struct _xlator_cmdline_option xlator_cmdline_option_t;

struct _server_cmdline {
        struct list_head   list;
        char              *volfile_server;
        char              *transport;
        int                port;
};
typedef struct _server_cmdline server_cmdline_t;

#define GF_OPTION_ENABLE   _gf_true
#define GF_OPTION_DISABLE  _gf_false
#define GF_OPTION_DEFERRED 2

struct _cmd_args {
        /* basic options */
        char              *volfile_server;
        server_cmdline_t  *curr_server;
        /* List of backup  volfile servers, including original */
        struct list_head   volfile_servers;
        char              *volfile;
        char              *log_server;
        gf_loglevel_t      log_level;
        char              *log_file;
        char              *log_ident;
        gf_log_logger_t    logger;
        gf_log_format_t    log_format;
        uint32_t           log_buf_size;
        uint32_t           log_flush_timeout;
        int32_t            max_connect_attempts;
        char              *print_exports;
        char              *print_netgroups;
        int                print_xlatordir;
        int                print_statedumpdir;
        int                print_logdir;
        /* advanced options */
        uint32_t           volfile_server_port;
        char              *volfile_server_transport;
        uint32_t           log_server_port;
        char              *pid_file;
        char              *sock_file;
        int                no_daemon_mode;
        char              *run_id;
        int                debug_mode;
        int                read_only;
        int                acl;
        int                selinux;
        int                capability;
        int                enable_ino32;
        int                worm;
        int                mac_compat;
        int                fopen_keep_cache;
        int                gid_timeout;
        char               gid_timeout_set;
        int                aux_gfid_mount;

        /* need a process wide timer-wheel? */
        int                global_timer_wheel;

        /* list of xlator_option_t */
        struct list_head   xlator_options;

        /* fuse options */
        int                fuse_direct_io_mode;
        char              *use_readdirp;
        int                no_root_squash;
        int                volfile_check;
        double             fuse_entry_timeout;
        double             fuse_negative_timeout;
        double             fuse_attribute_timeout;
        char              *volume_name;
        int                fuse_nodev;
        int                fuse_nosuid;
        char              *dump_fuse;
        pid_t              client_pid;
        int                client_pid_set;
        unsigned           uid_map_root;
        int                background_qlen;
        int                congestion_threshold;
        char              *fuse_mountopts;
        int                mem_acct;
        int                resolve_gids;

        /* key args */
        char              *mount_point;
        char              *volfile_id;

        /* required for portmap */
        int                brick_port;
        char              *brick_name;
        int                brick_port2;

        /* Should management connections use SSL? */
        int                secure_mgmt;

        /* Linux-only OOM killer adjustment */
#ifdef GF_LINUX_HOST_OS
        char              *oom_score_adj;
#endif

        /* Run this process with valgrind? Might want to prevent calling
         * functions that prevent valgrind from working correctly, like
         * dlclose(). */
        int                valgrind;
        int                localtime_logging;

        /* For the subdir mount */
        char           *subdir_mount;

        char              *process_name;
        char              *event_history;
        int                thin_client;
        uint32_t           reader_thread_count;

        /* FUSE writeback cache support */
        int                kernel_writeback_cache;
        uint32_t           attr_times_granularity;
};
typedef struct _cmd_args cmd_args_t;


struct _glusterfs_graph {
        struct list_head   list;
        char               graph_uuid[128];
        struct timeval     dob;
        void              *first;
        void              *top;   /* selected by -n */
        uint32_t           leaf_count;
        int                xl_count;
        int                id;    /* Used in logging */
        int                used;  /* Should be set when fuse gets
                                            first CHILD_UP */
        uint32_t           volfile_checksum;
};
typedef struct _glusterfs_graph glusterfs_graph_t;


typedef int32_t (*glusterfsd_mgmt_event_notify_fn_t) (int32_t event, void *data,
                                                      ...);

typedef enum {
        MGMT_SSL_NEVER = 0,
        MGMT_SSL_COPY_IO,
        MGMT_SSL_ALWAYS
} mgmt_ssl_t;

struct tvec_base;

/* reference counting for the global (per ctx) timer-wheel */
struct gf_ctx_tw {
        GF_REF_DECL;
        struct tvec_base *timer_wheel; /* global timer-wheel instance */
};

struct _glusterfs_ctx {
        cmd_args_t         cmd_args;
        char              *process_uuid;
        FILE              *pidfp;
        char               fin;
        void              *timer;
        void              *ib;
        struct call_pool  *pool;
        void              *event_pool;
        void              *iobuf_pool;
        void              *logbuf_pool;
        gf_lock_t          lock;
        size_t             page_size;

        /* one per volfile parse */
        struct list_head   graphs;

        /* the latest graph in use */
        glusterfs_graph_t *active;

        /* fuse or nfs (but not protocol/server) */
        void              *master;

        /* xlator implementing MOPs for centralized logging, volfile server */
        void              *mgmt;

        /* listener of the commands from glusterd */
        void              *listener;

        /* toggle switch for latency measurement */
        unsigned char      measure_latency;
        pthread_t          sigwaiter;
        char              *cmdlinestr;
        struct mem_pool   *stub_mem_pool;
        unsigned char      cleanup_started;
        int                graph_id; /* Incremented per graph, value should
                                        indicate how many times the graph has
                                        got changed */
        pid_t              mnt_pid; /* pid of the mount agent */
        int                process_mode; /*mode in which process is runninng*/
        struct syncenv    *env;          /* The env pointer to the synctasks */

        struct list_head   mempool_list; /* used to keep a global list of
                                            mempools, used to log details of
                                            mempool in statedump */
        char              *statedump_path;

        struct mem_pool   *dict_pool;
        struct mem_pool   *dict_pair_pool;
        struct mem_pool   *dict_data_pool;

        glusterfsd_mgmt_event_notify_fn_t notify; /* Used for xlators to make
                                                     call to fsd-mgmt */
        gf_log_handle_t    log; /* all logging related variables */

        int                mem_acct_enable;

        int                daemon_pipe[2];

        struct clienttable *clienttable;

        /*
         * Should management connections use SSL?  This is the only place we
         * can put it where both daemon-startup and socket code will see it.
         *
         * Why is it an int?  Because we're included before common-utils.h,
         * which defines gf_boolean_t (what we really want).  It doesn't make
         * any sense, but it's not worth turning the codebase upside-down to
         * fix it.  Thus, an int.
         */
        int                secure_mgmt;

        /* The option is use to set cert_depth while management connection
           use SSL
         */
        int                ssl_cert_depth;

        /*
         * Should *our* server/inbound connections use SSL?  This is only true
         * if we're glusterd and secure_mgmt is set, or if we're glusterfsd
         * and SSL is set on the I/O path.  It should never be set e.g. for
         * NFS.
         */
        mgmt_ssl_t         secure_srvr;
        /* Buffer to 'save' backtrace even under OOM-kill like situations*/
        char               btbuf[GF_BACKTRACE_LEN];

        pthread_mutex_t    notify_lock;
        pthread_cond_t     notify_cond;
        int                notifying;

        struct gf_ctx_tw  *tw; /* refcounted timer_wheel */

        gf_lock_t          volfile_lock;

        /* configuration related elements, which gets changed
           from global xlator */
        struct {
                char *metrics_dumppath;
        } config;

        struct {
                gf_atomic_t max_dict_pairs;
                gf_atomic_t total_pairs_used;
                gf_atomic_t total_dicts_used;
        } stats;

        struct list_head   volfile_list;
};
typedef struct _glusterfs_ctx glusterfs_ctx_t;

typedef struct {
        char               volfile_checksum[SHA256_DIGEST_LENGTH];
        char               vol_id[NAME_MAX+1];
        struct list_head   volfile_list;

} gf_volfile_t;


glusterfs_ctx_t *glusterfs_ctx_new (void);

struct gf_flock {
        short              l_type;
        short              l_whence;
        off_t              l_start;
        off_t              l_len;
        pid_t              l_pid;
        gf_lkowner_t       l_owner;
};

typedef struct lock_migration_info {
        struct list_head   list;
        struct gf_flock    flock;
        char              *client_uid;
        uint32_t           lk_flags;
} lock_migration_info_t;

#define GF_MUST_CHECK __attribute__((warn_unused_result))
/*
 * Some macros (e.g. ALLOC_OR_GOTO) set variables in function scope, but the
 * calling function might not only declare the variable to keep the macro happy
 * and not use it otherwise.  In such cases, the following can be used to
 * suppress the "set but not used" warning that would otherwise occur.
 */
#define GF_UNUSED __attribute__((unused))

/*
 * If present, this has the following effects:
 *
 *      glusterd enables privileged commands over TCP
 *
 *      all code enables SSL for outbound connections to management port
 *
 *      glusterd enables SSL for inbound connections
 *
 * Servers and clients enable/disable SSL among themselves by other means.
 * Making secure management connections conditional on a file is a bit of a
 * hack, but we don't have any other place for such global settings across
 * all of the affected components.  Making it a compile-time option would
 * reduce functionality, both for users and for testing (which can now be
 * done using secure connections for all tests without change elsewhere).
 *
 */
#define SECURE_ACCESS_FILE     GLUSTERD_DEFAULT_WORKDIR "/secure-access"

int glusterfs_graph_prepare (glusterfs_graph_t *graph, glusterfs_ctx_t *ctx,
                             char *volume_name);
int glusterfs_graph_destroy_residual (glusterfs_graph_t *graph);
int glusterfs_graph_deactivate (glusterfs_graph_t *graph);
int glusterfs_graph_destroy (glusterfs_graph_t *graph);
int glusterfs_get_leaf_count (glusterfs_graph_t *graph);
int glusterfs_graph_activate (glusterfs_graph_t *graph, glusterfs_ctx_t *ctx);
glusterfs_graph_t *glusterfs_graph_construct (FILE *fp);
int glusterfs_graph_init (glusterfs_graph_t *graph);
glusterfs_graph_t *glusterfs_graph_new (void);
int glusterfs_graph_reconfigure (glusterfs_graph_t *oldgraph,
                                  glusterfs_graph_t *newgraph);
int glusterfs_graph_attach (glusterfs_graph_t *orig_graph, char *path,
                            glusterfs_graph_t **newgraph);
int glusterfs_graph_parent_up (glusterfs_graph_t *graph);

void
gf_free_mig_locks (lock_migration_info_t *locks);

int glusterfs_read_secure_access_file (void);
#endif /* _GLUSTERFS_H */
