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

#include "call-stub.h"
#include "compat-errno.h"
#include "afr-mem-types.h"

#include "libxlator.h"
#include "timer.h"
#include "syncop.h"

#include "afr-self-heald.h"
#include "afr-messages.h"

#define AFR_PATHINFO_HEADER "REPLICATE:"
#define AFR_SH_READDIR_SIZE_KEY "self-heal-readdir-size"
#define AFR_SH_DATA_DOMAIN_FMT "%s:self-heal"
#define AFR_DIRTY_DEFAULT AFR_XATTR_PREFIX ".dirty"
#define AFR_DIRTY (((afr_private_t *) (THIS->private))->afr_dirty)

#define AFR_LOCKEE_COUNT_MAX    3
#define AFR_DOM_COUNT_MAX    3
#define AFR_NUM_CHANGE_LOGS            3 /*data + metadata + entry*/
#define AFR_DEFAULT_SPB_CHOICE_TIMEOUT 300 /*in seconds*/

#define ARBITER_BRICK_INDEX 2

typedef int (*afr_lock_cbk_t) (call_frame_t *frame, xlator_t *this);

typedef int (*afr_read_txn_wind_t) (call_frame_t *frame, xlator_t *this, int subvol);

typedef int (*afr_inode_refresh_cbk_t) (call_frame_t *frame, xlator_t *this, int err);

typedef int (*afr_changelog_resume_t) (call_frame_t *frame, xlator_t *this);

typedef int (*afr_compound_cbk_t) (call_frame_t *frame, void *cookie,
                                   xlator_t *this, int op_ret, int op_errno,
                                   void *data, dict_t *xdata);

#define AFR_COUNT(array,max) ({int __i; int __res = 0; for (__i = 0; __i < max; __i++) if (array[__i]) __res++; __res;})
#define AFR_INTERSECT(dst,src1,src2,max) ({int __i; for (__i = 0; __i < max; __i++) dst[__i] = src1[__i] && src2[__i];})
#define AFR_CMP(a1,a2,len) ({int __cmp = 0; int __i; for (__i = 0; __i < len; __i++) if (a1[__i] != a2[__i]) { __cmp = 1; break;} __cmp;})
#define AFR_IS_ARBITER_BRICK(priv, index) ((priv->arbiter_count == 1) && (index == ARBITER_BRICK_INDEX))

#define AFR_SET_ERROR_AND_CHECK_SPLIT_BRAIN(ret, errnum)                       \
        do {                                                                   \
                local->op_ret = ret;                                           \
                local->op_errno = errnum;                                      \
                if (local->op_errno == EIO)                                    \
                        gf_msg (this->name, GF_LOG_ERROR, local->op_errno,     \
                                AFR_MSG_SPLIT_BRAIN, "Failing %s on gfid %s: " \
                                "split-brain observed.",                       \
                                gf_fop_list[local->op],                        \
                                uuid_utoa (local->inode->gfid));               \
        } while (0)

typedef enum {
        AFR_FAV_CHILD_NONE,
        AFR_FAV_CHILD_BY_SIZE,
        AFR_FAV_CHILD_BY_CTIME,
        AFR_FAV_CHILD_BY_MTIME,
        AFR_FAV_CHILD_BY_MAJORITY,
        AFR_FAV_CHILD_POLICY_MAX,
} afr_favorite_child_policy;

typedef struct _afr_private {
        gf_lock_t lock;               /* to guard access to child_count, etc */
        unsigned int child_count;     /* total number of children   */
        unsigned int arbiter_count;   /*subset of child_count.
                                        Has to be 0 or 1.*/

        xlator_t **children;

        inode_t *root_inode;

        unsigned char *child_up;
        unsigned char *local;

        char **pending_key;

        char         *data_self_heal;              /* on/off/open */
        char *       data_self_heal_algorithm;    /* name of algorithm */
        unsigned int data_self_heal_window_size;  /* max number of pipelined
                                                     read/writes */

        struct list_head heal_waiting; /*queue for files that need heal*/
        uint32_t  heal_wait_qlen; /*configurable queue length for heal_waiting*/
        int32_t  heal_waiters; /* No. of elements currently in wait queue.*/

        struct list_head healing;/* queue for files that are undergoing
                                    background heal*/
        uint32_t  background_self_heal_count;/*configurable queue length for
                                               healing queue*/
        int32_t  healers;/* No. of elements currently undergoing background
                          heal*/

        gf_boolean_t metadata_self_heal;   /* on/off */
        gf_boolean_t entry_self_heal;      /* on/off */

        gf_boolean_t data_change_log;       /* on/off */
        gf_boolean_t metadata_change_log;   /* on/off */
        gf_boolean_t entry_change_log;      /* on/off */

	gf_boolean_t metadata_splitbrain_forced_heal; /* on/off */
        int read_child;               /* read-subvolume */
        unsigned int hash_mode;       /* for when read_child is not set */
        int favorite_child;  /* subvolume to be preferred in resolving
                                         split-brain cases */

        afr_favorite_child_policy fav_child_policy;/*Policy to use for automatic
                                                 resolution of split-brains.*/

        gf_boolean_t inodelk_trace;
        gf_boolean_t entrylk_trace;

        unsigned int wait_count;      /* # of servers to wait for success */

        gf_timer_t *timer;      /* launched when parent up is received */

        gf_boolean_t      optimistic_change_log;
        gf_boolean_t      eager_lock;
        gf_boolean_t      pre_op_compat;      /* on/off */
	uint32_t          post_op_delay_secs;
        unsigned int      quorum_count;
        gf_boolean_t      quorum_reads;

        char                   vol_uuid[UUID_SIZE + 1];
        int32_t                *last_event;

	/* @event_generation: Keeps count of number of events received which can
	   potentially impact consistency decisions. The events are CHILD_UP
	   and CHILD_DOWN, when we have to recalculate the freshness/staleness
	   of copies to detect if changes had happened while the other server
	   was down. CHILD_DOWN and CHILD_UP can also be received on network
	   disconnect/reconnects and not necessarily server going down/up.
	   Recalculating freshness/staleness on network events is equally
	   important as we might have had a network split brain.
	*/
	uint32_t               event_generation;

        gf_boolean_t           choose_local;
        gf_boolean_t           did_discovery;
        uint64_t               sh_readdir_size;
        gf_boolean_t           ensure_durability;
        char                   *sh_domain;
	char                   *afr_dirty;

	afr_self_heald_t       shd;

        gf_boolean_t           consistent_metadata;
        uint64_t               spb_choice_timeout;
        gf_boolean_t           need_heal;

	/* pump dependencies */
	void                   *pump_private;
	gf_boolean_t           use_afr_in_pump;
	char                   *locking_scheme;
        gf_boolean_t            esh_granular;
        gf_boolean_t           consistent_io;
        gf_boolean_t            use_compound_fops;
} afr_private_t;


typedef enum {
        AFR_DATA_TRANSACTION,          /* truncate, write, ... */
        AFR_METADATA_TRANSACTION,      /* chmod, chown, ... */
        AFR_ENTRY_TRANSACTION,         /* create, rmdir, ... */
        AFR_ENTRY_RENAME_TRANSACTION,  /* rename */
} afr_transaction_type;

typedef enum {
        AFR_TRANSACTION_LK,
        AFR_SELFHEAL_LK,
} transaction_lk_type_t;

typedef enum {
        AFR_LOCK_OP,
        AFR_UNLOCK_OP,
} afr_lock_op_type_t;

typedef enum {
        AFR_DATA_SELF_HEAL_LK,
        AFR_METADATA_SELF_HEAL_LK,
        AFR_ENTRY_SELF_HEAL_LK,
}selfheal_lk_type_t;

typedef enum {
        AFR_INODELK_TRANSACTION,
        AFR_INODELK_NB_TRANSACTION,
        AFR_ENTRYLK_TRANSACTION,
        AFR_ENTRYLK_NB_TRANSACTION,
        AFR_INODELK_SELFHEAL,
        AFR_INODELK_NB_SELFHEAL,
        AFR_ENTRYLK_SELFHEAL,
        AFR_ENTRYLK_NB_SELFHEAL,
} afr_lock_call_type_t;

/*
  xattr format: trusted.afr.volume = [x y z]
  x - data pending
  y - metadata pending
  z - entry pending
*/

static inline int
afr_index_for_transaction_type (afr_transaction_type type)
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

        return -1;  /* make gcc happy */
}

static inline int
afr_index_from_ia_type (ia_type_t type)
{
        switch (type) {
        case IA_IFDIR:
                return afr_index_for_transaction_type (AFR_ENTRY_TRANSACTION);
        case IA_IFREG:
                return afr_index_for_transaction_type (AFR_DATA_TRANSACTION);
        default: return -1;
        }
}

typedef struct {
        loc_t                   loc;
        char                    *basename;
        unsigned char           *locked_nodes;
        int                     locked_count;

} afr_entry_lockee_t;

int
afr_entry_lockee_cmp (const void *l1, const void *l2);

typedef struct {
        char    *domain; /* Domain on which inodelk is taken */
        struct gf_flock flock;
        unsigned char *locked_nodes;
        int32_t lock_count;
} afr_inodelk_t;

typedef struct {
        loc_t *lk_loc;

        int                     lockee_count;
        afr_entry_lockee_t      lockee[AFR_LOCKEE_COUNT_MAX];

        afr_inodelk_t       inodelk[AFR_DOM_COUNT_MAX];
        const char *lk_basename;
        const char *lower_basename;
        const char *higher_basename;
        char lower_locked;
        char higher_locked;

        unsigned char *locked_nodes;
        unsigned char *lower_locked_nodes;

        selfheal_lk_type_t selfheal_lk_type;
        transaction_lk_type_t transaction_lk_type;

        int32_t lock_count;
        int32_t entrylk_lock_count;

        uint64_t lock_number;
        int32_t lk_call_count;
        int32_t lk_expected_count;
        int32_t lk_attempted_count;

        int32_t lock_op_ret;
        int32_t lock_op_errno;
        afr_lock_cbk_t lock_cbk;
        char *domain; /* Domain on which inode/entry lock/unlock in progress.*/
} afr_internal_lock_t;

struct afr_reply {
	int	valid;
	int32_t	op_ret;
	int32_t	op_errno;
	dict_t *xattr;/*For xattrop*/
	dict_t *xdata;
	struct iatt poststat;
	struct iatt postparent;
	struct iatt prestat;
	struct iatt preparent;
	struct iatt preparent2;
	struct iatt postparent2;
        /* For rchecksum */
	uint8_t checksum[MD5_DIGEST_LENGTH];
        gf_boolean_t buf_has_zeroes;
        /* For lookup */
        int8_t need_heal;
};

typedef enum {
        AFR_FD_NOT_OPENED,
        AFR_FD_OPENED,
        AFR_FD_OPENING
} afr_fd_open_status_t;

typedef struct {
        unsigned int *pre_op_done[AFR_NUM_CHANGE_LOGS];
	int inherited[AFR_NUM_CHANGE_LOGS];
	int on_disk[AFR_NUM_CHANGE_LOGS];
        afr_fd_open_status_t *opened_on; /* which subvolumes the fd is open on */

        unsigned int *lock_piggyback;
        unsigned int *lock_acquired;

        int flags;

	/* used for delayed-post-op optimization */
	pthread_mutex_t    delay_lock;
	gf_timer_t        *delay_timer;
	call_frame_t      *delay_frame;

	/* set if any write on this fd was a non stable write
	   (i.e, without O_SYNC or O_DSYNC)
	*/
	gf_boolean_t      witnessed_unstable_write;

	/* @open_fd_count:
	   Number of open FDs queried from the server, as queried through
	   xdata in FOPs. Currently, used to decide if eager-locking must be
	   temporarily disabled.
	*/
        uint32_t        open_fd_count;


	/* list of frames currently in progress */
	struct list_head  eager_locked;

	/* the subvolume on which the latest sequence of readdirs (starting
	   at offset 0) has begun. Till the next readdir request with 0 offset
	   arrives, we continue to read off this subvol.
	*/
	int readdir_subvol;
} afr_fd_ctx_t;

typedef enum {
        AFR_FOP_LOCK_PARALLEL,
        AFR_FOP_LOCK_SERIAL,
        AFR_FOP_LOCK_QUORUM_FAILED,
} afr_fop_lock_state_t;

typedef struct _afr_local {
	glusterfs_fop_t  op;
        unsigned int call_count;

	/* @event_generation: copy of priv->event_generation taken at the
	   time of starting the transaction. The copy is made so that we
	   have a stable value through the various phases of the transaction.
	*/
	unsigned int event_generation;

        uint32_t     open_fd_count;
        gf_boolean_t update_open_fd_count;

	gf_lkowner_t  saved_lk_owner;

        int32_t op_ret;
        int32_t op_errno;

        int32_t **pending;

	int dirty[AFR_NUM_CHANGE_LOGS];

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

	/* @refreshed:

	   the inode was "refreshed" (i.e, pending xattrs from all subvols
	   freshly inspected and inode ctx updated accordingly) as part of
	   this transaction already.
	*/
	gf_boolean_t refreshed;

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

        /*To handle setattr/setxattr on yet to be linked inode from dht*/
        uuid_t  refreshgfid;

	/*
	  @pre_op_compat:

	  compatibility mode of pre-op. send a separate pre-op and
	  op operations as part of transaction, rather than combining
	*/

	gf_boolean_t pre_op_compat;

        dict_t  *xattr_req;

        afr_internal_lock_t internal_lock;

        dict_t  *dict;

        int      optimistic_change_log;
	gf_boolean_t      delayed_post_op;

	/* Is the current writev() going to perform a stable write?
	   i.e, is fd->flags or @flags writev param have O_SYNC or
	   O_DSYNC?
	*/
	gf_boolean_t      stable_write;

	/* This write appended to the file. Nnot necessarily O_APPEND,
	   just means the offset of write was at the end of file.
	*/
	gf_boolean_t      append_write;

        /*
          This struct contains the arguments for the "continuation"
          (scheme-like) of fops
        */

        struct {
                struct {
                        gf_boolean_t needs_fresh_lookup;
                        uuid_t gfid_req;
                } lookup;

                struct {
                        unsigned char buf_set;
                        struct statvfs buf;
                } statfs;

                struct {
                        int32_t flags;
                } open;

                struct {
                        int32_t cmd;
                        struct gf_flock user_flock;
                        struct gf_flock ret_flock;
                        unsigned char *locked_nodes;
                } lk;

                /* inode read */

                struct {
                        int32_t mask;
                        int last_index;  /* index of the child we tried previously */
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
                        int last_index;
                        long xattr_len;
                } getxattr;

                struct {
                        size_t size;
                        off_t offset;
                        int last_index;
                        uint32_t flags;
                } readv;

                /* dir read */

                struct {
                        int success_count;
                        int32_t op_ret;
                        int32_t op_errno;

                        uint32_t *checksum;
                } opendir;

                struct {
                        int32_t op_ret;
                        int32_t op_errno;
                        size_t size;
                        off_t offset;
                        dict_t *dict;
                        gf_boolean_t failed;
                        int last_index;
                } readdir;
                /* inode write */

                struct {
                        struct iatt prebuf;
                        struct iatt postbuf;
                } inode_wfop; //common structure for all inode-write-fops

                struct {
                        int32_t op_ret;

                        struct iovec *vector;
                        struct iobref *iobref;
                        int32_t count;
                        off_t offset;
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
                } dir_fop; //common structure for all dir fops

                struct {
                        fd_t *fd;
                        dict_t *params;
                        int32_t flags;
                        mode_t mode;
                } create;

                struct {
                        dev_t dev;
                        mode_t mode;
                        dict_t *params;
                } mknod;

                struct {
                        int32_t mode;
                        dict_t *params;
                } mkdir;

                struct {
                        int flags;
                } rmdir;

                struct {
                        dict_t *params;
                        char *linkpath;
                } symlink;

		struct {
			int32_t mode;
			off_t offset;
			size_t len;
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
                        entrylk_cmd in_cmd;
                        entrylk_cmd cmd;
                        entrylk_type type;
                        void *xdata;
                } entrylk;

                struct {
                        off_t offset;
                        gf_seek_what_t what;
                } seek;

        } cont;

        struct {
                off_t start, len;

                gf_boolean_t    eager_lock_on;
                int *eager_lock;

                char *basename;
                char *new_basename;

                loc_t parent_loc;
                loc_t new_parent_loc;

                afr_transaction_type type;

		/* stub to resume on destruction
		   of the transaction frame */
		call_stub_t      *resume_stub;

		struct list_head  eager_locked;

                unsigned char   *pre_op;

                /* For arbiter configuration only. */
                dict_t **pre_op_xdata;
                unsigned char *pre_op_sources;

		/* @failed_subvols: subvolumes on which a pre-op or a
                    FOP failed. */
                unsigned char   *failed_subvols;

		/* @dirtied: flag which indicates whether we set dirty flag
		   in the OP. Typically true when we are performing operation
		   on more than one subvol and optimistic changelog is disabled

		   A 'true' value set in @dirtied flag means an 'undirtying'
		   has to be done in POST-OP phase.
		*/
		gf_boolean_t  dirtied;

		/* @inherited: flag which indicates that the dirty flags
		   of the previous transaction were inherited
		*/
		gf_boolean_t  inherited;

		/*
		  @no_uninherit: flag which indicates that a pre_op_uninherit()
		  must _not_ be attempted (and returned as failure) always. This
		  flag is set when a hard pre-op is performed, but not accounted
		  for it in fd_ctx->on_disk[]. Such transactions are "isolated"
		  from the pre-op piggybacking entirely and therefore uninherit
		  must not be attempted.
		*/
		gf_boolean_t no_uninherit;

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

                gf_boolean_t in_flight_sb; /* Indicator for occurrence of
                                              split-brain while in the middle of
                                              a txn. */
                int32_t in_flight_sb_errno; /* This is where the cause of the
                                               failure on the last good copy of
                                               the file is stored.
                                               */

		/* @changelog_resume: function to be called after changlogging
		   (either pre-op or post-op) is done
		*/
		afr_changelog_resume_t changelog_resume;

                call_frame_t *main_frame;

                int (*wind) (call_frame_t *frame, xlator_t *this, int subvol);

                int (*fop) (call_frame_t *frame, xlator_t *this);

                int (*done) (call_frame_t *frame, xlator_t *this);

                int (*resume) (call_frame_t *frame, xlator_t *this);

                int (*unwind) (call_frame_t *frame, xlator_t *this);

                /* post-op hook */
        } transaction;

	syncbarrier_t barrier;

        /* extra data for fops */
        dict_t         *xdata_req;
        dict_t         *xdata_rsp;

        dict_t         *xattr_rsp; /*for [f]xattrop*/

        mode_t          umask;
        int             xflag;
        gf_boolean_t    do_discovery;
	struct afr_reply *replies;

        /* For  client side background heals. */
        struct list_head healer;
        call_frame_t *heal_frame;

        gf_boolean_t need_full_crawl;
        gf_boolean_t compound;
        afr_fop_lock_state_t fop_lock_state;
        compound_args_t *c_args;

        gf_boolean_t is_read_txn;
} afr_local_t;


typedef struct _afr_inode_ctx {
        uint64_t        read_subvol;
        int             spb_choice;
        gf_timer_t      *timer;
        gf_boolean_t    need_refresh;
} afr_inode_ctx_t;

typedef struct afr_spbc_timeout {
        call_frame_t *frame;
        gf_boolean_t d_spb;
        gf_boolean_t m_spb;
        loc_t        *loc;
        int          spb_child_index;
} afr_spbc_timeout_t;

typedef struct afr_spb_status {
        call_frame_t *frame;
        loc_t        *loc;
} afr_spb_status_t;

typedef struct afr_empty_brick_args {
        call_frame_t *frame;
        loc_t loc;
        int empty_index;
        char *op_type;
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

/* did a call fail due to a child failing? */
#define child_went_down(op_ret, op_errno) (((op_ret) < 0) &&            \
                                           ((op_errno == ENOTCONN) ||   \
                                            (op_errno == EBADFD)))

int
afr_inode_get_readable (call_frame_t *frame, inode_t *inode, xlator_t *this,
                        unsigned char *readable, int *event_p, int type);
int
afr_inode_read_subvol_get (inode_t *inode, xlator_t *this,
			   unsigned char *data_subvols,
			   unsigned char *metadata_subvols,
			   int *event_generation);
int
__afr_inode_read_subvol_get (inode_t *inode, xlator_t *this,
			     unsigned char *data_subvols,
			     unsigned char *metadata_subvols,
			     int *event_generation);

int
__afr_inode_read_subvol_set (inode_t *inode, xlator_t *this,
			     unsigned char *data_subvols,
			     unsigned char *metadata_subvol,
			     int event_generation);
int
afr_inode_read_subvol_set (inode_t *inode, xlator_t *this,
			   unsigned char *data_subvols,
			   unsigned char *metadata_subvols,
			   int event_generation);

int
afr_inode_event_gen_reset (inode_t *inode, xlator_t *this);

int
afr_read_subvol_select_by_policy (inode_t *inode, xlator_t *this,
				  unsigned char *readable,
                                  afr_read_subvol_args_t *args);

int
afr_inode_read_subvol_type_get (inode_t *inode, xlator_t *this,
				unsigned char *readable, int *event_p,
				int type);
int
afr_read_subvol_get (inode_t *inode, xlator_t *this, int *subvol_p,
                     unsigned char *readables,
		     int *event_p, afr_transaction_type type,
                     afr_read_subvol_args_t *args);

#define afr_data_subvol_get(i, t, s, r, e, a) \
	afr_read_subvol_get(i, t, s, r, e, AFR_DATA_TRANSACTION, a)

#define afr_metadata_subvol_get(i, t, s, r, e, a) \
	afr_read_subvol_get(i, t, s, r, e, AFR_METADATA_TRANSACTION, a)

int
afr_inode_refresh (call_frame_t *frame, xlator_t *this, inode_t *inode,
                   uuid_t gfid, afr_inode_refresh_cbk_t cbk);

int32_t
afr_notify (xlator_t *this, int32_t event, void *data, void *data2);

int
xattr_is_equal (dict_t *this, char *key1, data_t *value1, void *data);

int
afr_init_entry_lockee (afr_entry_lockee_t *lockee, afr_local_t *local,
                       loc_t *loc, char *basename, int child_count);

void
afr_entry_lockee_cleanup (afr_internal_lock_t *int_lock);

int
afr_attempt_lock_recovery (xlator_t *this, int32_t child_index);

int
afr_mark_locked_nodes (xlator_t *this, fd_t *fd,
                       unsigned char *locked_nodes);

void
afr_set_lk_owner (call_frame_t *frame, xlator_t *this, void *lk_owner);

int
afr_set_lock_number (call_frame_t *frame, xlator_t *this);

int32_t
afr_unlock (call_frame_t *frame, xlator_t *this);

int
afr_nonblocking_entrylk (call_frame_t *frame, xlator_t *this);

int
afr_nonblocking_inodelk (call_frame_t *frame, xlator_t *this);

int
afr_blocking_lock (call_frame_t *frame, xlator_t *this);

int
afr_internal_lock_finish (call_frame_t *frame, xlator_t *this);

int
afr_lk_transfer_datalock (call_frame_t *dst, call_frame_t *src, char *dom,
                          unsigned int child_count);

int
__afr_fd_ctx_set (xlator_t *this, fd_t *fd);

afr_fd_ctx_t *
afr_fd_ctx_get (fd_t *fd, xlator_t *this);

int
afr_build_parent_loc (loc_t *parent, loc_t *child, int32_t *op_errno);

int
afr_locked_nodes_count (unsigned char *locked_nodes, int child_count);

int
afr_replies_interpret (call_frame_t *frame, xlator_t *this, inode_t *inode,
                       gf_boolean_t *start_heal);

void
afr_local_replies_wipe (afr_local_t *local, afr_private_t *priv);

void
afr_local_cleanup (afr_local_t *local, xlator_t *this);

int
afr_frame_return (call_frame_t *frame);

int
afr_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          fd_t *fd, dict_t *xdata);

void
afr_local_transaction_cleanup (afr_local_t *local, xlator_t *this);

int
afr_cleanup_fd_ctx (xlator_t *this, fd_t *fd);

#define AFR_STACK_UNWIND(fop, frame, op_ret, op_errno, params ...)\
        do {                                                    \
                afr_local_t *__local = NULL;                    \
                xlator_t    *__this = NULL;                     \
                int32_t     __op_ret   = 0;                     \
                int32_t     __op_errno = 0;                     \
                                                                \
                __op_ret = op_ret;                              \
                __op_errno = op_errno;                          \
                if (frame) {                                    \
                        __local = frame->local;                 \
                        __this = frame->this;                   \
                        afr_handle_inconsistent_fop (frame, &__op_ret,\
                                                     &__op_errno);\
                        frame->local = NULL;                    \
                }                                               \
                                                                \
                STACK_UNWIND_STRICT (fop, frame, __op_ret,      \
                                     __op_errno, params);       \
                if (__local) {                                  \
                        afr_local_cleanup (__local, __this);    \
                        mem_put (__local);                      \
                }                                               \
        } while (0)

#define AFR_STACK_DESTROY(frame)                                \
        do {                                                    \
                afr_local_t *__local = NULL;                    \
                xlator_t    *__this = NULL;                     \
                __local = frame->local;                         \
                __this = frame->this;                           \
                frame->local = NULL;                            \
                STACK_DESTROY (frame->root);                    \
                if (__local) {                                  \
                        afr_local_cleanup (__local, __this);    \
                        mem_put (__local);                      \
                }                                               \
        } while (0);

#define AFR_FRAME_INIT(frame, op_errno)				       \
	({frame->local = mem_get0 (THIS->local_pool);		       \
	if (afr_local_init (frame->local, THIS->private, &op_errno)) { \
		afr_local_cleanup (frame->local, THIS);		       \
		mem_put (frame->local);				       \
		frame->local = NULL; };				       \
	frame->local;})

#define AFR_STACK_RESET(frame)                                         \
        do {                                                           \
                afr_local_t *__local = NULL;                           \
                xlator_t    *__this = NULL;                            \
                __local = frame->local;                                \
                __this = frame->this;                                  \
                frame->local = NULL;                                   \
                int __opr;                                             \
                STACK_RESET (frame->root);                             \
                if (__local) {                                         \
                        afr_local_cleanup (__local, __this);           \
                        mem_put (__local);                             \
                }                                                      \
                AFR_FRAME_INIT (frame, __opr);                         \
        } while (0)

/* allocate and return a string that is the basename of argument */
static inline char *
AFR_BASENAME (const char *str)
{
        char *__tmp_str = NULL;
        char *__basename_str = NULL;
        __tmp_str = gf_strdup (str);
        __basename_str = gf_strdup (basename (__tmp_str));
        GF_FREE (__tmp_str);
        return __basename_str;
}

call_frame_t *
afr_copy_frame (call_frame_t *base);

int
afr_transaction_local_init (afr_local_t *local, xlator_t *this);

int32_t
afr_marker_getxattr (call_frame_t *frame, xlator_t *this,
                     loc_t *loc, const char *name,afr_local_t *local, afr_private_t *priv );

int
afr_local_init (afr_local_t *local, afr_private_t *priv, int32_t *op_errno);

int
afr_internal_lock_init (afr_internal_lock_t *lk, size_t child_count,
                        transaction_lk_type_t lk_type);

int
afr_higher_errno (int32_t old_errno, int32_t new_errno);

int
afr_final_errno (afr_local_t *local, afr_private_t *priv);

int
afr_xattr_req_prepare (xlator_t *this, dict_t *xattr_req);

void
afr_fix_open (fd_t *fd, xlator_t *this);

afr_fd_ctx_t *
afr_fd_ctx_get (fd_t *fd, xlator_t *this);

void
afr_set_low_priority (call_frame_t *frame);
int
afr_child_fd_ctx_set (xlator_t *this, fd_t *fd, int32_t child,
                      int flags);

void
afr_matrix_cleanup (int32_t **pending, unsigned int m);

int32_t**
afr_matrix_create (unsigned int m, unsigned int n);

int**
afr_mark_pending_changelog (afr_private_t *priv, unsigned char *pending,
                            dict_t *xattr, ia_type_t iat);

void
afr_filter_xattrs (dict_t *xattr);

/*
 * Special value indicating we should use the "auto" quorum method instead of
 * a fixed value (including zero to turn off quorum enforcement).
 */
#define AFR_QUORUM_AUTO INT_MAX

int
afr_fd_report_unstable_write (xlator_t *this, fd_t *fd);

gf_boolean_t
afr_fd_has_witnessed_unstable_write (xlator_t *this, fd_t *fd);

void
afr_delayed_changelog_wake_resume (xlator_t *this, fd_t *fd, call_stub_t *stub);

int
afr_inodelk_init (afr_inodelk_t *lk, char *dom, size_t child_count);

void
afr_handle_open_fd_count (call_frame_t *frame, xlator_t *this);

void
afr_remove_eager_lock_stub (afr_local_t *local);

void
afr_replies_wipe (struct afr_reply *replies, int count);

gf_boolean_t
afr_xattrs_are_equal (dict_t *dict1, dict_t *dict2);

gf_boolean_t
afr_is_xattr_ignorable (char *key);

int
afr_get_heal_info (call_frame_t *frame, xlator_t *this, loc_t *loc);

int
afr_heal_splitbrain_file(call_frame_t *frame, xlator_t *this, loc_t *loc);

int
afr_get_split_brain_status (void *opaque);

int
afr_get_split_brain_status_cbk (int ret, call_frame_t *frame, void *opaque);

int
afr_inode_split_brain_choice_set (inode_t *inode, xlator_t *this,
                                  int spb_choice);
int
afr_inode_split_brain_choice_get (inode_t *inode, xlator_t *this,
                                  int *spb_choice);
int
afr_get_child_index_from_name (xlator_t *this, char *name);

int
afr_is_split_brain (call_frame_t *frame, xlator_t *this, inode_t *inode,
                    uuid_t gfid, gf_boolean_t *d_spb, gf_boolean_t *m_spb);
int
afr_spb_choice_timeout_cancel (xlator_t *this, inode_t *inode);

int
afr_set_split_brain_choice (int ret, call_frame_t *frame, void *opaque);

gf_boolean_t
afr_get_need_heal (xlator_t *this);

void
afr_set_need_heal (xlator_t *this, afr_local_t *local);

int
afr_selfheal_data_open (xlator_t *this, inode_t *inode, fd_t **fd);

int
afr_get_msg_id (char *op_type);

int
afr_set_in_flight_sb_status (xlator_t *this, afr_local_t *local,
                             inode_t *inode);

int32_t
afr_quorum_errno (afr_private_t *priv);

gf_boolean_t
afr_is_consistent_io_possible (afr_local_t *local, afr_private_t *priv,
                               int32_t *op_errno);
void
afr_handle_inconsistent_fop (call_frame_t *frame, int32_t *op_ret,
                             int32_t *op_errno);

void
afr_inode_write_fill (call_frame_t *frame, xlator_t *this, int child_index,
                     int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                     struct iatt *postbuf, dict_t *xdata);
void
afr_process_post_writev (call_frame_t *frame, xlator_t *this);

void
afr_writev_unwind (call_frame_t *frame, xlator_t *this);

void
afr_writev_copy_outvars (call_frame_t *src_frame, call_frame_t *dst_frame);

void
afr_update_uninodelk (afr_local_t *local, afr_internal_lock_t *int_lock,
                    int32_t child_index);
gf_boolean_t
afr_can_compound_pre_op_and_op (afr_private_t *priv, glusterfs_fop_t fop);

afr_compound_cbk_t
afr_pack_fop_args (call_frame_t *frame, compound_args_t *args,
                   glusterfs_fop_t fop, int index);
int
afr_is_inodelk_transaction(afr_local_t *local);

afr_fd_ctx_t *
__afr_fd_ctx_get (fd_t *fd, xlator_t *this);

gf_boolean_t
afr_is_inode_refresh_reqd (inode_t *inode, xlator_t *this,
                           int event_gen1, int event_gen2);
#endif /* __AFR_H__ */
