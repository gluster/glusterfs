/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __AFR_H__
#define __AFR_H__

#include <glusterfs/call-stub.h>
#include <glusterfs/compat-errno.h>
#include "afr-mem-types.h"

#include "libxlator.h"
#include <glusterfs/timer.h>
#include <glusterfs/syncop.h>

#include "afr-self-heald.h"
#include "afr-messages.h"

#define SHD_INODE_LRU_LIMIT 1
#define AFR_PATHINFO_HEADER "REPLICATE:"
#define AFR_SH_READDIR_SIZE_KEY "self-heal-readdir-size"
#define AFR_SH_DATA_DOMAIN_FMT "%s:self-heal"
#define AFR_DIRTY_DEFAULT AFR_XATTR_PREFIX ".dirty"
#define AFR_DIRTY (((afr_private_t *)(THIS->private))->afr_dirty)

#define AFR_LOCKEE_COUNT_MAX 3
#define AFR_DOM_COUNT_MAX 3
#define AFR_NUM_CHANGE_LOGS 3              /*data + metadata + entry*/
#define AFR_DEFAULT_SPB_CHOICE_TIMEOUT 300 /*in seconds*/

#define ARBITER_BRICK_INDEX 2
#define THIN_ARBITER_BRICK_INDEX 2
#define AFR_TA_DOM_NOTIFY "afr.ta.dom-notify"
#define AFR_TA_DOM_MODIFY "afr.ta.dom-modify"

#define AFR_LK_HEAL_DOM "afr.lock-heal.domain"

#define AFR_HALO_MAX_LATENCY 99999
#define AFR_ANON_DIR_PREFIX ".glusterfs-anonymous-inode"

#define PFLAG_PENDING (1 << 0)
#define PFLAG_SBRAIN (1 << 1)

typedef int (*afr_lock_cbk_t)(call_frame_t *frame, xlator_t *this);

typedef int (*afr_read_txn_wind_t)(call_frame_t *frame, xlator_t *this,
                                   int subvol);

typedef int (*afr_inode_refresh_cbk_t)(call_frame_t *frame, xlator_t *this,
                                       int err);

typedef int (*afr_changelog_resume_t)(call_frame_t *frame, xlator_t *this);

#define AFR_COUNT(array, max)                                                  \
    ({                                                                         \
        int __i;                                                               \
        int __res = 0;                                                         \
        for (__i = 0; __i < max; __i++)                                        \
            if (array[__i])                                                    \
                __res++;                                                       \
        __res;                                                                 \
    })
#define AFR_INTERSECT(dst, src1, src2, max)                                    \
    ({                                                                         \
        int __i;                                                               \
        for (__i = 0; __i < max; __i++)                                        \
            dst[__i] = src1[__i] && src2[__i];                                 \
    })
#define AFR_CMP(a1, a2, len)                                                   \
    ({                                                                         \
        int __cmp = 0;                                                         \
        int __i;                                                               \
        for (__i = 0; __i < len; __i++)                                        \
            if (a1[__i] != a2[__i]) {                                          \
                __cmp = 1;                                                     \
                break;                                                         \
            }                                                                  \
        __cmp;                                                                 \
    })
#define AFR_IS_ARBITER_BRICK(priv, index)                                      \
    ((priv->arbiter_count == 1) && (index == ARBITER_BRICK_INDEX))

#define AFR_SET_ERROR_AND_CHECK_SPLIT_BRAIN(ret, errnum)                       \
    do {                                                                       \
        local->op_ret = ret;                                                   \
        local->op_errno = errnum;                                              \
        if (local->op_errno == EIO)                                            \
            gf_msg(this->name, GF_LOG_ERROR, local->op_errno,                  \
                   AFR_MSG_SPLIT_BRAIN,                                        \
                   "Failing %s on gfid %s: "                                   \
                   "split-brain observed.",                                    \
                   gf_fop_list[local->op], uuid_utoa(local->inode->gfid));     \
    } while (0)

#define AFR_ERROR_OUT_IF_FDCTX_INVALID(__fd, __this, __error, __label)         \
    do {                                                                       \
        afr_fd_ctx_t *__fd_ctx = NULL;                                         \
        __fd_ctx = afr_fd_ctx_get(__fd, __this);                               \
        if (__fd_ctx && __fd_ctx->is_fd_bad) {                                 \
            __error = EBADF;                                                   \
            goto __label;                                                      \
        }                                                                      \
    } while (0)

typedef enum {
    AFR_READ_POLICY_FIRST_UP,
    AFR_READ_POLICY_GFID_HASH,
    AFR_READ_POLICY_GFID_PID_HASH,
    AFR_READ_POLICY_LESS_LOAD,
    AFR_READ_POLICY_LEAST_LATENCY,
    AFR_READ_POLICY_LOAD_LATENCY_HYBRID,
} afr_read_hash_mode_t;

typedef enum {
    AFR_FAV_CHILD_NONE,
    AFR_FAV_CHILD_BY_SIZE,
    AFR_FAV_CHILD_BY_CTIME,
    AFR_FAV_CHILD_BY_MTIME,
    AFR_FAV_CHILD_BY_MAJORITY,
    AFR_FAV_CHILD_POLICY_MAX,
} afr_favorite_child_policy;

typedef enum {
    AFR_SELFHEAL_DATA_FULL = 0,
    AFR_SELFHEAL_DATA_DIFF,
    AFR_SELFHEAL_DATA_DYNAMIC,
} afr_data_self_heal_type_t;

typedef enum {
    AFR_CHILD_UNKNOWN = -1,
    AFR_CHILD_ZERO,
    AFR_CHILD_ONE,
    AFR_CHILD_THIN_ARBITER,
} afr_child_index;

typedef enum {
    TA_WAIT_FOR_NOTIFY_LOCK_REL, /*FOP came after notify domain lock upcall
                                   notification and waiting for its release.*/
    TA_GET_INFO_FROM_TA_FILE,    /*FOP needs post-op on ta file to get
                                  *info about which brick is bad.*/
    TA_INFO_IN_MEMORY_SUCCESS,   /*Bad brick info is in memory and fop failed
                                  *on BAD brick - Success*/
    TA_INFO_IN_MEMORY_FAILED,    /*Bad brick info is in memory and fop failed
                                  *on GOOD brick - Failed*/
    TA_SUCCESS,                  /*FOP succeeded on both data bricks.*/
} afr_ta_fop_state_t;

struct afr_nfsd {
    uint32_t halo_max_latency_msec;
    gf_boolean_t iamnfsd;
};

typedef struct _afr_lk_heal_info {
    fd_t *fd;
    int32_t cmd;
    struct gf_flock flock;
    dict_t *xdata_req;
    unsigned char *locked_nodes;
    struct list_head pos;
    gf_lkowner_t lk_owner;
    pid_t pid;
    int32_t *child_up_event_gen;
    int32_t *child_down_event_gen;
} afr_lk_heal_info_t;

typedef struct _afr_private {
    gf_lock_t lock;             /* to guard access to child_count, etc */
    unsigned int child_count;   /* total number of children   */
    unsigned int arbiter_count; /*subset of child_count.
                                  Has to be 0 or 1.*/

    xlator_t **children;

    inode_t *root_inode;

    int favorite_child; /* subvolume to be preferred in resolving
                                    split-brain cases */
    /* For thin-arbiter. */
    uuid_t ta_gfid;
    unsigned int thin_arbiter_count; /* 0 or 1 at the moment.*/
    int ta_bad_child_index;
    int ta_event_gen;
    unsigned int ta_in_mem_txn_count;
    unsigned int ta_on_wire_txn_count;
    struct list_head ta_waitq;
    struct list_head ta_onwireq;

    unsigned char *anon_inode;
    unsigned char *child_up;
    unsigned char *halo_child_up;
    int64_t *child_latency;
    unsigned char *local;

    char **pending_key;

    afr_data_self_heal_type_t data_self_heal_algorithm;
    unsigned int data_self_heal_window_size; /* max number of pipelined
                                                read/writes */

    struct list_head heal_waiting; /*queue for files that need heal*/
    uint32_t heal_wait_qlen; /*configurable queue length for heal_waiting*/
    int32_t heal_waiters;    /* No. of elements currently in wait queue.*/

    struct list_head healing;            /* queue for files that are undergoing
                                            background heal*/
    uint32_t background_self_heal_count; /*configurable queue length for
                                           healing queue*/
    int32_t healers; /* No. of elements currently undergoing background
                      heal*/

    gf_boolean_t release_ta_notify_dom_lock;

    gf_boolean_t metadata_self_heal; /* on/off */
    gf_boolean_t entry_self_heal;    /* on/off */

    gf_boolean_t metadata_splitbrain_forced_heal; /* on/off */
    int read_child;                               /* read-subvolume */
    gf_atomic_t *pending_reads; /*No. of pending read cbks per child.*/

    gf_timer_t *timer; /* launched when parent up is received */

    unsigned int wait_count; /* # of servers to wait for success */

    unsigned char ta_child_up;
    gf_boolean_t optimistic_change_log;
    gf_boolean_t eager_lock;
    gf_boolean_t pre_op_compat; /* on/off */
    uint32_t post_op_delay_secs;
    unsigned int quorum_count;

    off_t ta_notify_dom_lock_offset;
    afr_favorite_child_policy fav_child_policy; /*Policy to use for automatic
                                              resolution of split-brains.*/
    afr_read_hash_mode_t hash_mode; /* for when read_child is not set */

    int32_t *last_event;

    /* @event_generation: Keeps count of number of events received which can
       potentially impact consistency decisions. The events are CHILD_UP
       and CHILD_DOWN, when we have to recalculate the freshness/staleness
       of copies to detect if changes had happened while the other server
       was down. CHILD_DOWN and CHILD_UP can also be received on network
       disconnect/reconnects and not necessarily server going down/up.
       Recalculating freshness/staleness on network events is equally
       important as we might have had a network split brain.
    */
    uint32_t event_generation;
    char vol_uuid[UUID_SIZE + 1];

    gf_boolean_t choose_local;
    gf_boolean_t did_discovery;
    gf_boolean_t ensure_durability;
    gf_boolean_t halo_enabled;
    gf_boolean_t consistent_metadata;
    gf_boolean_t need_heal;
    gf_boolean_t granular_locks;
    uint64_t sh_readdir_size;
    char *sh_domain;
    char *afr_dirty;

    uint64_t spb_choice_timeout;

    afr_self_heald_t shd;
    struct afr_nfsd nfsd;

    uint32_t halo_max_latency_msec;
    uint32_t halo_max_replicas;
    uint32_t halo_min_replicas;

    gf_boolean_t full_lock;
    gf_boolean_t esh_granular;
    gf_boolean_t consistent_io;
    gf_boolean_t data_self_heal; /* on/off */
    gf_boolean_t use_anon_inode;

    /*For lock healing.*/
    struct list_head saved_locks;
    struct list_head lk_healq;

    /*For anon-inode handling */
    char anon_inode_name[NAME_MAX + 1];
    char anon_gfid_str[UUID_SIZE + 1];
} afr_private_t;

typedef enum {
    AFR_DATA_TRANSACTION,         /* truncate, write, ... */
    AFR_METADATA_TRANSACTION,     /* chmod, chown, ... */
    AFR_ENTRY_TRANSACTION,        /* create, rmdir, ... */
    AFR_ENTRY_RENAME_TRANSACTION, /* rename */
} afr_transaction_type;

/*
  xattr format: trusted.afr.volume = [x y z]
  x - data pending
  y - metadata pending
  z - entry pending
*/

static inline int
afr_index_for_transaction_type(afr_transaction_type type)
{
    switch (type) {
        case AFR_DATA_TRANSACTION:
            return 0;

        case AFR_METADATA_TRANSACTION:
            return 1;

        case AFR_ENTRY_TRANSACTION:
        case AFR_ENTRY_RENAME_TRANSACTION:
            return 2;
    }

    return -1; /* make gcc happy */
}

static inline int
afr_index_from_ia_type(ia_type_t type)
{
    switch (type) {
        case IA_IFDIR:
            return afr_index_for_transaction_type(AFR_ENTRY_TRANSACTION);
        case IA_IFREG:
            return afr_index_for_transaction_type(AFR_DATA_TRANSACTION);
        default:
            return -1;
    }
}

typedef struct {
    struct gf_flock flock;
    loc_t loc;
    fd_t *fd;
    char *basename;
    unsigned char *locked_nodes;
    int locked_count;

} afr_lockee_t;

int
afr_entry_lockee_cmp(const void *l1, const void *l2);

typedef struct {
    loc_t *lk_loc;

    afr_lockee_t lockee[AFR_LOCKEE_COUNT_MAX];

    const char *lk_basename;
    const char *lower_basename;
    const char *higher_basename;

    unsigned char *lower_locked_nodes;

    afr_lock_cbk_t lock_cbk;

    int lockee_count;

    int32_t lk_call_count;
    int32_t lk_expected_count;
    int32_t lk_attempted_count;

    int32_t lock_op_ret;
    int32_t lock_op_errno;
    char *domain; /* Domain on which inode/entry lock/unlock in progress.*/
    int32_t lock_count;
    char lower_locked;
    char higher_locked;
} afr_internal_lock_t;

struct afr_reply {
    int valid;
    int32_t op_ret;
    dict_t *xattr; /*For xattrop*/
    dict_t *xdata;
    struct iatt poststat;
    struct iatt postparent;
    struct iatt prestat;
    struct iatt preparent;
    struct iatt preparent2;
    struct iatt postparent2;
    int32_t op_errno;
    /* For rchecksum */
    uint8_t checksum[SHA256_DIGEST_LENGTH];
    gf_boolean_t buf_has_zeroes;
    gf_boolean_t fips_mode_rchecksum;
    /* For lookup */
    int8_t need_heal;
};

typedef enum {
    AFR_FD_NOT_OPENED,
    AFR_FD_OPENED,
    AFR_FD_OPENING
} afr_fd_open_status_t;

typedef struct {
    afr_fd_open_status_t *opened_on; /* which subvolumes the fd is open on */
    int flags;

    /* the subvolume on which the latest sequence of readdirs (starting
       at offset 0) has begun. Till the next readdir request with 0 offset
       arrives, we continue to read off this subvol.
    */
    int readdir_subvol;
    /* lock-healing related members. */
    gf_boolean_t is_fd_bad;
    afr_lk_heal_info_t *lk_heal_info;

} afr_fd_ctx_t;

typedef enum {
    AFR_FOP_LOCK_PARALLEL,
    AFR_FOP_LOCK_SERIAL,
    AFR_FOP_LOCK_QUORUM_FAILED,
} afr_fop_lock_state_t;

typedef struct _afr_inode_lock_t {
    /* @num_inodelks:
       Number of inodelks queried from the server, as queried through
       xdata in FOPs. Currently, used to decide if eager-locking must be
       temporarily disabled.
    */
    int32_t num_inodelks;
    unsigned int event_generation;
    gf_timer_t *delay_timer;
    struct list_head owners;  /*Transactions that are performing fop*/
    struct list_head post_op; /*Transactions that are done with the fop
                               *So can not conflict with the fops*/
    struct list_head waiting; /*Transaction that are waiting for
                               *conflicting transactions to complete*/
    struct list_head frozen;  /*Transactions that need to go as part of
                               * next batch of eager-lock*/
    gf_boolean_t release;
    gf_boolean_t acquired;
} afr_lock_t;

typedef struct _afr_inode_ctx {
    uint64_t read_subvol;
    uint64_t write_subvol;
    int lock_count;
    int spb_choice;
    gf_timer_t *timer;
    unsigned int *pre_op_done[AFR_NUM_CHANGE_LOGS];
    int inherited[AFR_NUM_CHANGE_LOGS];
    int on_disk[AFR_NUM_CHANGE_LOGS];
    /*Only 2 types of transactions support eager-locks now. DATA/METADATA*/
    afr_lock_t lock[2];

    /* @open_fd_count:
       Number of open FDs queried from the server, as queried through
       xdata in FOPs. Currently, used to decide if eager-locking must be
       temporarily disabled.
    */
    uint32_t open_fd_count;
    gf_boolean_t need_refresh;

    /* set if any write on this fd was a non stable write
       (i.e, without O_SYNC or O_DSYNC)
    */
    gf_boolean_t witnessed_unstable_write;
} afr_inode_ctx_t;

typedef struct _afr_local {
    glusterfs_fop_t op;
    unsigned int call_count;

    /* @event_generation: copy of priv->event_generation taken at the
       time of starting the transaction. The copy is made so that we
       have a stable value through the various phases of the transaction.
    */
    unsigned int event_generation;

    uint32_t open_fd_count;
    int32_t num_inodelks;

    int32_t op_ret;
    int32_t op_errno;

    int dirty[AFR_NUM_CHANGE_LOGS];

    int32_t **pending;

    loc_t loc;
    loc_t newloc;

    fd_t *fd;
    afr_fd_ctx_t *fd_ctx;

    /* @child_up: copy of priv->child_up taken at the time of transaction
       start. The copy is taken so that we have a stable child_up array
       through the phases of the transaction as priv->child_up[i] can keep
       changing through time.
    */
    unsigned char *child_up;

    /* @read_attempted:
       array of flags representing subvolumes where read operations of
       the read transaction have already been attempted. The array is
       first pre-filled with down subvolumes, and as reads are performed
       on other subvolumes, those are set as well. This way if the read
       operation fails we do not retry on that subvolume again.
    */
    unsigned char *read_attempted;

    /* @readfn:

       pointer to function which will perform the read operation on a given
       subvolume. Used in read transactions.
    */

    afr_read_txn_wind_t readfn;

    /* @inode:

       the inode on which the read txn is performed on. ref'ed and copied
       from either fd->inode or loc.inode
    */

    inode_t *inode;

    /* @parent[2]:

       parent inode[s] on which directory transactions are performed.
    */

    inode_t *parent;
    inode_t *parent2;

    /* @readable:

       array of flags representing servers from which a read can be
       performed. This is the output of afr_inode_refresh()
    */
    unsigned char *readable;
    unsigned char *readable2; /*For rename transaction*/

    afr_inode_refresh_cbk_t refreshfn;

    /* @refreshinode:

       Inode currently getting refreshed.
    */
    inode_t *refreshinode;

    dict_t *xattr_req;

    dict_t *dict;

    int read_subvol; /* Current read subvolume */

    int optimistic_change_log;

    afr_internal_lock_t internal_lock;

    /*To handle setattr/setxattr on yet to be linked inode from dht*/
    uuid_t refreshgfid;

    /* @refreshed:

       the inode was "refreshed" (i.e, pending xattrs from all subvols
       freshly inspected and inode ctx updated accordingly) as part of
       this transaction already.
    */
    gf_boolean_t refreshed;

    gf_boolean_t update_num_inodelks;
    gf_boolean_t update_open_fd_count;

    /*
      @pre_op_compat:

      compatibility mode of pre-op. send a separate pre-op and
      op operations as part of transaction, rather than combining
    */

    gf_boolean_t pre_op_compat;

    /* Is the current writev() going to perform a stable write?
       i.e, is fd->flags or @flags writev param have O_SYNC or
       O_DSYNC?
    */
    gf_boolean_t stable_write;

    /* This write appended to the file. Nnot necessarily O_APPEND,
       just means the offset of write was at the end of file.
    */
    gf_boolean_t append_write;

    /*
      This struct contains the arguments for the "continuation"
      (scheme-like) of fops
    */

    struct {
        struct {
            struct statvfs buf;
            unsigned char buf_set;
        } statfs;

        struct {
            fd_t *fd;
            int32_t flags;
        } open;

        struct {
            struct gf_flock user_flock;
            struct gf_flock ret_flock;
            unsigned char *locked_nodes;
            int32_t cmd;
            /*For lock healing only.*/
            unsigned char *dom_locked_nodes;
            int32_t *dom_lock_op_ret;
            int32_t *dom_lock_op_errno;
            struct gf_flock *getlk_rsp;
        } lk;

        /* inode read */

        struct {
            int32_t mask;
            int last_index; /* index of the child we tried previously */
        } access;

        struct {
            int last_index;
        } stat;

        struct {
            int last_index;
        } fstat;

        struct {
            size_t size;
            int last_index;
        } readlink;

        struct {
            char *name;
            long xattr_len;
            int last_index;
        } getxattr;

        struct {
            size_t size;
            off_t offset;
            int last_index;
            uint32_t flags;
        } readv;

        /* dir read */

        struct {
            uint32_t *checksum;
            int success_count;
            int32_t op_ret;
            int32_t op_errno;
        } opendir;

        struct {
            int32_t op_ret;
            int32_t op_errno;
            size_t size;
            off_t offset;
            dict_t *dict;
            int last_index;
            gf_boolean_t failed;
        } readdir;
        /* inode write */

        struct {
            struct iatt prebuf;
            struct iatt postbuf;
        } inode_wfop;  // common structure for all inode-write-fops

        struct {
            struct iovec *vector;
            struct iobref *iobref;
            off_t offset;
            int32_t op_ret;
            int32_t count;
            uint32_t flags;
        } writev;

        struct {
            off_t offset;
        } truncate;

        struct {
            off_t offset;
        } ftruncate;

        struct {
            struct iatt in_buf;
            int32_t valid;
        } setattr;

        struct {
            struct iatt in_buf;
            int32_t valid;
        } fsetattr;

        struct {
            dict_t *dict;
            int32_t flags;
        } setxattr;

        struct {
            dict_t *dict;
            int32_t flags;
        } fsetxattr;

        struct {
            char *name;
        } removexattr;

        struct {
            dict_t *xattr;
            gf_xattrop_flags_t optype;
        } xattrop;

        /* dir write */

        struct {
            inode_t *inode;
            struct iatt buf;
            struct iatt preparent;
            struct iatt postparent;
            struct iatt prenewparent;
            struct iatt postnewparent;
        } dir_fop;  // common structure for all dir fops

        struct {
            fd_t *fd;
            dict_t *params;
            int32_t flags;
            mode_t mode;
        } create;

        struct {
            dict_t *params;
            dev_t dev;
            mode_t mode;
        } mknod;

        struct {
            dict_t *params;
            int32_t mode;
        } mkdir;

        struct {
            dict_t *params;
            char *linkpath;
        } symlink;

        struct {
            off_t offset;
            size_t len;
            int32_t mode;
        } fallocate;

        struct {
            off_t offset;
            size_t len;
        } discard;

        struct {
            off_t offset;
            off_t len;
            struct iatt prebuf;
            struct iatt postbuf;
        } zerofill;

        struct {
            char *volume;
            int32_t cmd;
            int32_t in_cmd;
            struct gf_flock in_flock;
            struct gf_flock flock;
            void *xdata;
        } inodelk;

        struct {
            char *volume;
            char *basename;
            void *xdata;
            entrylk_cmd in_cmd;
            entrylk_cmd cmd;
            entrylk_type type;
        } entrylk;

        struct {
            off_t offset;
            gf_seek_what_t what;
        } seek;

        struct {
            struct gf_lease user_lease;
            struct gf_lease ret_lease;
            unsigned char *locked_nodes;
        } lease;

        struct {
            int flags;
        } rmdir;

        struct {
            int32_t datasync;
        } fsync;

        struct {
            uuid_t gfid_req;
            gf_boolean_t needs_fresh_lookup;
        } lookup;

    } cont;

    struct {
        char *basename;
        char *new_basename;

        loc_t parent_loc;
        loc_t new_parent_loc;

        /* stub to resume on destruction
           of the transaction frame */
        call_stub_t *resume_stub;

        struct list_head owner_list;
        struct list_head wait_list;

        unsigned char *pre_op;

        /* Changelog xattr dict for [f]xattrop*/
        dict_t **changelog_xdata;
        unsigned char *pre_op_sources;

        /* @failed_subvols: subvolumes on which a pre-op or a
            FOP failed. */
        unsigned char *failed_subvols;

        call_frame_t *main_frame; /*Fop frame*/
        call_frame_t *frame;      /*Transaction frame*/

        int (*wind)(call_frame_t *frame, xlator_t *this, int subvol);

        int (*unwind)(call_frame_t *frame, xlator_t *this);

        off_t start, len;

        afr_transaction_type type;

        int32_t in_flight_sb_errno; /* This is where the cause of the
                                       failure on the last good copy of
                                       the file is stored.
                                       */

        /* @changelog_resume: function to be called after changlogging
           (either pre-op or post-op) is done
        */
        afr_changelog_resume_t changelog_resume;

        gf_boolean_t eager_lock_on;
        gf_boolean_t do_eager_unlock;

        /* @dirtied: flag which indicates whether we set dirty flag
           in the OP. Typically true when we are performing operation
           on more than one subvol and optimistic changelog is disabled

           A 'true' value set in @dirtied flag means an 'undirtying'
           has to be done in POST-OP phase.
        */
        gf_boolean_t dirtied;

        /* @inherited: flag which indicates that the dirty flags
           of the previous transaction were inherited
        */
        gf_boolean_t inherited;

        /*
          @no_uninherit: flag which indicates that a pre_op_uninherit()
          must _not_ be attempted (and returned as failure) always. This
          flag is set when a hard pre-op is performed, but not accounted
          for it in fd_ctx->on_disk[]. Such transactions are "isolated"
          from the pre-op piggybacking entirely and therefore uninherit
          must not be attempted.
        */
        gf_boolean_t no_uninherit;

        gf_boolean_t in_flight_sb; /* Indicator for occurrence of
                                      split-brain while in the middle of
                                      a txn. */

        /* @uninherit_done:
           @uninherit_value:

           The above pair variables make pre_op_uninherit() idempotent.
           Both are FALSE initially. The first call to pre_op_uninherit
           sets @uninherit_done to TRUE and the return value to
           @uninherit_value. Further calls will check for @uninherit_done
           to be TRUE and if so will simply return @uninherit_value.
        */
        gf_boolean_t uninherit_done;
        gf_boolean_t uninherit_value;

        gf_boolean_t disable_delayed_post_op;
    } transaction;

    syncbarrier_t barrier;

    /* extra data for fops */
    dict_t *xdata_req;
    dict_t *xdata_rsp;

    dict_t *xattr_rsp; /*for [f]xattrop*/

    mode_t umask;
    int xflag;
    struct afr_reply *replies;

    /* For  client side background heals. */
    struct list_head healer;
    call_frame_t *heal_frame;

    afr_inode_ctx_t *inode_ctx;

    /*For thin-arbiter transactions.*/
    int ta_failed_subvol;
    int ta_event_gen;
    struct list_head ta_waitq;
    struct list_head ta_onwireq;
    afr_ta_fop_state_t fop_state;
    afr_fop_lock_state_t fop_lock_state;
    gf_lkowner_t saved_lk_owner;
    unsigned char read_txn_query_child;
    unsigned char ta_child_up;
    gf_boolean_t do_discovery;
    gf_boolean_t need_full_crawl;
    gf_boolean_t is_read_txn;
    gf_boolean_t is_new_entry;
} afr_local_t;

typedef struct afr_spbc_timeout {
    call_frame_t *frame;
    loc_t *loc;
    int spb_child_index;
    gf_boolean_t d_spb;
    gf_boolean_t m_spb;
} afr_spbc_timeout_t;

typedef struct afr_spb_status {
    call_frame_t *frame;
    loc_t *loc;
} afr_spb_status_t;

typedef struct afr_empty_brick_args {
    call_frame_t *frame;
    char *op_type;
    loc_t loc;
    int empty_index;
} afr_empty_brick_args_t;

typedef struct afr_read_subvol_args {
    ia_type_t ia_type;
    uuid_t gfid;
} afr_read_subvol_args_t;

typedef struct afr_granular_esh_args {
    fd_t *heal_fd;
    xlator_t *xl;
    call_frame_t *frame;
    gf_boolean_t mismatch; /* flag to represent occurrence of type/gfid
                              mismatch */
} afr_granular_esh_args_t;

int
afr_inode_get_readable(call_frame_t *frame, inode_t *inode, xlator_t *this,
                       unsigned char *readable, int *event_p, int type);
int
afr_inode_read_subvol_get(inode_t *inode, xlator_t *this,
                          unsigned char *data_subvols,
                          unsigned char *metadata_subvols,
                          int *event_generation);
int
__afr_inode_read_subvol_get(inode_t *inode, xlator_t *this,
                            unsigned char *data_subvols,
                            unsigned char *metadata_subvols,
                            int *event_generation);

int
__afr_inode_read_subvol_set(inode_t *inode, xlator_t *this,
                            unsigned char *data_subvols,
                            unsigned char *metadata_subvol,
                            int event_generation);
int
afr_inode_read_subvol_set(inode_t *inode, xlator_t *this,
                          unsigned char *data_subvols,
                          unsigned char *metadata_subvols,
                          int event_generation);

int
__afr_inode_need_refresh_set(inode_t *inode, xlator_t *this);

int
afr_inode_need_refresh_set(inode_t *inode, xlator_t *this);

int
afr_read_subvol_select_by_policy(inode_t *inode, xlator_t *this,
                                 unsigned char *readable,
                                 afr_read_subvol_args_t *args);

int
afr_inode_read_subvol_type_get(inode_t *inode, xlator_t *this,
                               unsigned char *readable, int *event_p, int type);
int
afr_read_subvol_get(inode_t *inode, xlator_t *this, int *subvol_p,
                    unsigned char *readables, int *event_p,
                    afr_transaction_type type, afr_read_subvol_args_t *args);

#define afr_data_subvol_get(i, t, s, r, e, a)                                  \
    afr_read_subvol_get(i, t, s, r, e, AFR_DATA_TRANSACTION, a)

#define afr_metadata_subvol_get(i, t, s, r, e, a)                              \
    afr_read_subvol_get(i, t, s, r, e, AFR_METADATA_TRANSACTION, a)

int
afr_inode_refresh(call_frame_t *frame, xlator_t *this, inode_t *inode,
                  uuid_t gfid, afr_inode_refresh_cbk_t cbk);

int32_t
afr_notify(xlator_t *this, int32_t event, void *data, void *data2);

int
xattr_is_equal(dict_t *this, char *key1, data_t *value1, void *data);

int
afr_add_entry_lockee(afr_local_t *local, loc_t *loc, char *basename,
                     int child_count);

int
afr_add_inode_lockee(afr_local_t *local, int child_count);

void
afr_lockees_cleanup(afr_internal_lock_t *int_lock);

int
afr_attempt_lock_recovery(xlator_t *this, int32_t child_index);

int
afr_mark_locked_nodes(xlator_t *this, fd_t *fd, unsigned char *locked_nodes);

void
afr_set_lk_owner(call_frame_t *frame, xlator_t *this, void *lk_owner);

int
afr_set_lock_number(call_frame_t *frame, xlator_t *this);

int32_t
afr_unlock(call_frame_t *frame, xlator_t *this);

int
afr_lock_nonblocking(call_frame_t *frame, xlator_t *this);

int
afr_blocking_lock(call_frame_t *frame, xlator_t *this);

int
afr_internal_lock_finish(call_frame_t *frame, xlator_t *this);

int
__afr_fd_ctx_set(xlator_t *this, fd_t *fd);

afr_fd_ctx_t *
afr_fd_ctx_get(fd_t *fd, xlator_t *this);

int
afr_build_parent_loc(loc_t *parent, loc_t *child, int32_t *op_errno);

int
afr_locked_nodes_count(unsigned char *locked_nodes, int child_count);

int
afr_replies_interpret(call_frame_t *frame, xlator_t *this, inode_t *inode,
                      gf_boolean_t *start_heal);

void
afr_local_replies_wipe(afr_local_t *local, afr_private_t *priv);

void
afr_local_cleanup(afr_local_t *local, xlator_t *this);

int
afr_frame_return(call_frame_t *frame);

int
afr_open(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, dict_t *xdata);

void
afr_local_transaction_cleanup(afr_local_t *local, xlator_t *this);

int
afr_cleanup_fd_ctx(xlator_t *this, fd_t *fd);

#define AFR_STACK_UNWIND(fop, frame, op_ret, op_errno, params...)              \
    do {                                                                       \
        afr_local_t *__local = NULL;                                           \
        xlator_t *__this = NULL;                                               \
        int32_t __op_ret = 0;                                                  \
        int32_t __op_errno = 0;                                                \
                                                                               \
        __op_ret = op_ret;                                                     \
        __op_errno = op_errno;                                                 \
        if (frame) {                                                           \
            __local = frame->local;                                            \
            __this = frame->this;                                              \
            afr_handle_inconsistent_fop(frame, &__op_ret, &__op_errno);        \
            if (__local && __local->is_read_txn)                               \
                afr_pending_read_decrement(__this->private,                    \
                                           __local->read_subvol);              \
            if (__local && __local->xdata_req &&                               \
                afr_is_lock_mode_mandatory(__local->xdata_req))                \
                afr_dom_lock_release(frame);                                   \
            frame->local = NULL;                                               \
        }                                                                      \
                                                                               \
        STACK_UNWIND_STRICT(fop, frame, __op_ret, __op_errno, params);         \
        if (__local) {                                                         \
            afr_local_cleanup(__local, __this);                                \
            mem_put(__local);                                                  \
        }                                                                      \
    } while (0)

#define AFR_STACK_DESTROY(frame)                                               \
    do {                                                                       \
        afr_local_t *__local = NULL;                                           \
        xlator_t *__this = NULL;                                               \
        __local = frame->local;                                                \
        __this = frame->this;                                                  \
        frame->local = NULL;                                                   \
        STACK_DESTROY(frame->root);                                            \
        if (__local) {                                                         \
            afr_local_cleanup(__local, __this);                                \
            mem_put(__local);                                                  \
        }                                                                      \
    } while (0);

#define AFR_FRAME_INIT(frame, op_errno)                                        \
    ({                                                                         \
        frame->local = mem_get0(THIS->local_pool);                             \
        if (afr_local_init(frame->local, frame->this->private, &op_errno)) {   \
            afr_local_cleanup(frame->local, frame->this);                      \
            mem_put(frame->local);                                             \
            frame->local = NULL;                                               \
        };                                                                     \
        frame->local;                                                          \
    })

#define AFR_STACK_RESET(frame)                                                 \
    do {                                                                       \
        afr_local_t *__local = NULL;                                           \
        xlator_t *__this = NULL;                                               \
        __local = frame->local;                                                \
        __this = frame->this;                                                  \
        frame->local = NULL;                                                   \
        int __opr;                                                             \
        STACK_RESET(frame->root);                                              \
        if (__local) {                                                         \
            afr_local_cleanup(__local, __this);                                \
            mem_put(__local);                                                  \
        }                                                                      \
        AFR_FRAME_INIT(frame, __opr);                                          \
    } while (0)

/* allocate and return a string that is the basename of argument */
static inline char *
AFR_BASENAME(const char *str)
{
    char *__tmp_str = NULL;
    char *__basename_str = NULL;
    __tmp_str = gf_strdup(str);
    __basename_str = gf_strdup(basename(__tmp_str));
    GF_FREE(__tmp_str);
    return __basename_str;
}

call_frame_t *
afr_copy_frame(call_frame_t *base);

int
afr_transaction_local_init(afr_local_t *local, xlator_t *this);

int32_t
afr_marker_getxattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                    const char *name, afr_local_t *local, afr_private_t *priv);

int
afr_local_init(afr_local_t *local, afr_private_t *priv, int32_t *op_errno);

int
afr_internal_lock_init(afr_internal_lock_t *lk, size_t child_count);

int
afr_higher_errno(int32_t old_errno, int32_t new_errno);

int
afr_final_errno(afr_local_t *local, afr_private_t *priv);

int
afr_xattr_req_prepare(xlator_t *this, dict_t *xattr_req);

void
afr_fix_open(fd_t *fd, xlator_t *this);

afr_fd_ctx_t *
afr_fd_ctx_get(fd_t *fd, xlator_t *this);

void
afr_set_low_priority(call_frame_t *frame);
int
afr_child_fd_ctx_set(xlator_t *this, fd_t *fd, int32_t child, int flags);

void
afr_matrix_cleanup(int32_t **pending, unsigned int m);

int32_t **
afr_matrix_create(unsigned int m, unsigned int n);

int **
afr_mark_pending_changelog(afr_private_t *priv, unsigned char *pending,
                           dict_t *xattr, ia_type_t iat);

void
afr_filter_xattrs(dict_t *xattr);

/*
 * Special value indicating we should use the "auto" quorum method instead of
 * a fixed value (including zero to turn off quorum enforcement).
 */
#define AFR_QUORUM_AUTO INT_MAX

int
afr_fd_report_unstable_write(xlator_t *this, afr_local_t *local);

gf_boolean_t
afr_fd_has_witnessed_unstable_write(xlator_t *this, inode_t *inode);

void
afr_reply_wipe(struct afr_reply *reply);

void
afr_replies_wipe(struct afr_reply *replies, int count);

gf_boolean_t
afr_xattrs_are_equal(dict_t *dict1, dict_t *dict2);

gf_boolean_t
afr_is_xattr_ignorable(char *key);

int
afr_get_heal_info(call_frame_t *frame, xlator_t *this, loc_t *loc);

int
afr_heal_splitbrain_file(call_frame_t *frame, xlator_t *this, loc_t *loc);

int
afr_get_split_brain_status(void *opaque);

int
afr_get_split_brain_status_cbk(int ret, call_frame_t *frame, void *opaque);

int
afr_inode_split_brain_choice_set(inode_t *inode, xlator_t *this,
                                 int spb_choice);
int
afr_split_brain_read_subvol_get(inode_t *inode, xlator_t *this,
                                call_frame_t *frame, int *spb_subvol);
int
afr_get_child_index_from_name(xlator_t *this, char *name);

int
afr_is_split_brain(call_frame_t *frame, xlator_t *this, inode_t *inode,
                   uuid_t gfid, gf_boolean_t *d_spb, gf_boolean_t *m_spb);
int
afr_spb_choice_timeout_cancel(xlator_t *this, inode_t *inode);

int
afr_set_split_brain_choice(int ret, call_frame_t *frame, void *opaque);

gf_boolean_t
afr_get_need_heal(xlator_t *this);

void
afr_set_need_heal(xlator_t *this, afr_local_t *local);

int
afr_selfheal_data_open(xlator_t *this, inode_t *inode, fd_t **fd);

int
afr_get_msg_id(char *op_type);

int
afr_set_in_flight_sb_status(xlator_t *this, call_frame_t *frame,
                            inode_t *inode);

int32_t
afr_quorum_errno(afr_private_t *priv);

gf_boolean_t
afr_is_consistent_io_possible(afr_local_t *local, afr_private_t *priv,
                              int32_t *op_errno);
void
afr_handle_inconsistent_fop(call_frame_t *frame, int32_t *op_ret,
                            int32_t *op_errno);

void
afr_inode_write_fill(call_frame_t *frame, xlator_t *this, int child_index,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata);
void
afr_process_post_writev(call_frame_t *frame, xlator_t *this);

void
afr_writev_unwind(call_frame_t *frame, xlator_t *this);

void
afr_writev_copy_outvars(call_frame_t *src_frame, call_frame_t *dst_frame);

void
afr_update_uninodelk(afr_local_t *local, afr_internal_lock_t *int_lock,
                     int32_t child_index);
afr_fd_ctx_t *
__afr_fd_ctx_get(fd_t *fd, xlator_t *this);

gf_boolean_t
afr_is_inode_refresh_reqd(inode_t *inode, xlator_t *this, int event_gen1,
                          int event_gen2);

int
afr_serialize_xattrs_with_delimiter(call_frame_t *frame, xlator_t *this,
                                    char *buf, const char *default_str,
                                    int32_t *serz_len, char delimiter);
gf_boolean_t
afr_is_symmetric_error(call_frame_t *frame, xlator_t *this);

int
__afr_inode_ctx_get(xlator_t *this, inode_t *inode, afr_inode_ctx_t **ctx);

uint64_t
afr_write_subvol_get(call_frame_t *frame, xlator_t *this);

int
afr_write_subvol_set(call_frame_t *frame, xlator_t *this);

int
afr_write_subvol_reset(call_frame_t *frame, xlator_t *this);

int
afr_set_inode_local(xlator_t *this, afr_local_t *local, inode_t *inode);

int
afr_fill_ta_loc(xlator_t *this, loc_t *loc, gf_boolean_t is_gfid_based_fop);

int
afr_ta_post_op_lock(xlator_t *this, loc_t *loc);

int
afr_ta_post_op_unlock(xlator_t *this, loc_t *loc);

gf_boolean_t
afr_is_pending_set(xlator_t *this, dict_t *xdata, int type);

int
__afr_get_up_children_count(afr_private_t *priv);

call_frame_t *
afr_ta_frame_create(xlator_t *this);

gf_boolean_t
afr_ta_has_quorum(afr_private_t *priv, afr_local_t *local);

void
afr_ta_lock_release_synctask(xlator_t *this);

void
afr_ta_locked_priv_invalidate(afr_private_t *priv);

gf_boolean_t
afr_lookup_has_quorum(call_frame_t *frame,
                      const unsigned int up_children_count);

void
afr_mark_new_entry_changelog(call_frame_t *frame, xlator_t *this);

void
afr_handle_replies_quorum(call_frame_t *frame, xlator_t *this);

gf_boolean_t
afr_ta_dict_contains_pending_xattr(dict_t *dict, afr_private_t *priv,
                                   int child);

void
afr_selfheal_childup(xlator_t *this, afr_private_t *priv);

gf_boolean_t
afr_is_lock_mode_mandatory(dict_t *xdata);

void
afr_dom_lock_release(call_frame_t *frame);

void
afr_fill_success_replies(afr_local_t *local, afr_private_t *priv,
                         unsigned char *replies);

gf_boolean_t
afr_is_private_directory(afr_private_t *priv, uuid_t pargfid, const char *name,
                         pid_t pid);
#endif /* __AFR_H__ */
