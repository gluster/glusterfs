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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "call-stub.h"
#include "compat-errno.h"
#include "afr-mem-types.h"
#include "afr-self-heal-algorithm.h"

#include "libxlator.h"
#include "timer.h"

#define AFR_XATTR_PREFIX "trusted.afr"
#define AFR_PATHINFO_HEADER "REPLICATE:"
#define AFR_SH_READDIR_SIZE_KEY "self-heal-readdir-size"
#define AFR_SH_DATA_DOMAIN_FMT "%s:self-heal"

#define AFR_LOCKEE_COUNT_MAX    3
#define AFR_DOM_COUNT_MAX    3

#define afr_inode_missing(op_errno) (op_errno == ENOENT || op_errno == ESTALE)

struct _pump_private;

typedef int (*afr_expunge_done_cbk_t) (call_frame_t *frame, xlator_t *this,
                                       int child, int32_t op_error,
                                       int32_t op_errno);

typedef int (*afr_impunge_done_cbk_t) (call_frame_t *frame, xlator_t *this,
                                       int32_t op_error, int32_t op_errno);
typedef int (*afr_post_remove_call_t) (call_frame_t *frame, xlator_t *this);

typedef int (*afr_lock_cbk_t) (call_frame_t *frame, xlator_t *this);
typedef void (*afr_lookup_done_cbk_t) (call_frame_t *frame, xlator_t *this,
                                      int32_t op_ret, int32_t op_errno);

typedef enum {
        AFR_POS_UNKNOWN,
        AFR_POS_LOCAL,
        AFR_POS_REMOTE
} afr_child_pos_t;

typedef enum {
        SPLIT_BRAIN = 1,
        ALL_FOOLS = 2
} afr_subvol_status_t;

typedef enum {
        AFR_INODE_SET_READ_CTX = 1,
        AFR_INODE_RM_STALE_CHILDREN,
        AFR_INODE_SET_OPENDIR_DONE,
        AFR_INODE_GET_READ_CTX,
        AFR_INODE_GET_OPENDIR_DONE,
} afr_inode_op_t;

typedef struct afr_inode_params_ {
        afr_inode_op_t op;
        union {
                gf_boolean_t value;
                struct {
                        int32_t read_child;
                        int32_t *children;
                } read_ctx;
        } u;
} afr_inode_params_t;

typedef enum afr_spb_state {
        DONT_KNOW,
        SPB,
        NO_SPB
} afr_spb_state_t;

typedef struct afr_inode_ctx_ {
        uint64_t masks;
        int32_t  *fresh_children;//increasing order of latency
        afr_spb_state_t mdata_spb;
        afr_spb_state_t data_spb;
        uint32_t        open_fd_count;
} afr_inode_ctx_t;

typedef enum {
        NONE,
        INDEX,
        INDEX_TO_BE_HEALED,
        FULL,
} afr_crawl_type_t;

typedef struct afr_self_heald_ {
        gf_boolean_t            enabled;
        gf_boolean_t            iamshd;
        afr_crawl_type_t        *pending;
        gf_boolean_t            *inprogress;
        afr_child_pos_t         *pos;
        gf_timer_t              **timer;
        eh_t                    *healed;
        eh_t                    *heal_failed;
        eh_t                    *split_brain;
        eh_t                    **statistics;
        void                    **crawl_events;
        char                    *node_uuid;
        int                     timeout;
} afr_self_heald_t;

typedef struct _afr_private {
        gf_lock_t lock;               /* to guard access to child_count, etc */
        unsigned int child_count;     /* total number of children   */

        unsigned int read_child_rr;   /* round-robin index of the read_child */
        gf_lock_t read_child_lock;    /* lock to protect above */

        xlator_t **children;

        int first_lookup;
        inode_t *root_inode;

        unsigned char *child_up;

        char **pending_key;

        char         *data_self_heal;              /* on/off/open */
        char *       data_self_heal_algorithm;    /* name of algorithm */
        unsigned int data_self_heal_window_size;  /* max number of pipelined
                                                     read/writes */

        unsigned int background_self_heal_count;
        unsigned int background_self_heals_started;
        gf_boolean_t metadata_self_heal;   /* on/off */
        gf_boolean_t entry_self_heal;      /* on/off */

        gf_boolean_t data_change_log;       /* on/off */
        gf_boolean_t metadata_change_log;   /* on/off */
        gf_boolean_t entry_change_log;      /* on/off */

        int read_child;               /* read-subvolume */
        unsigned int hash_mode;       /* for when read_child is not set */
        int favorite_child;  /* subvolume to be preferred in resolving
                                         split-brain cases */

        gf_boolean_t inodelk_trace;
        gf_boolean_t entrylk_trace;

        gf_boolean_t strict_readdir;

        unsigned int wait_count;      /* # of servers to wait for success */

        uint64_t up_count;      /* number of CHILD_UPs we have seen */
        uint64_t down_count;    /* number of CHILD_DOWNs we have seen */

        struct _pump_private *pump_private; /* Set if we are loaded as pump */
        int                   use_afr_in_pump;

        pthread_mutex_t  mutex;
        struct list_head saved_fds;   /* list of fds on which locks have succeeded */
        gf_boolean_t      optimistic_change_log;
        gf_boolean_t      eager_lock;
	uint32_t          post_op_delay_secs;
        unsigned int      quorum_count;

        char                   vol_uuid[UUID_SIZE + 1];
        int32_t                *last_event;
        afr_self_heald_t       shd;
        gf_boolean_t           choose_local;
        gf_boolean_t           did_discovery;
        gf_boolean_t           readdir_failover;
        uint64_t               sh_readdir_size;
        gf_boolean_t           ensure_durability;
        char                   *sh_domain;
} afr_private_t;

typedef enum {
        AFR_SELF_HEAL_NOT_ATTEMPTED,
        AFR_SELF_HEAL_STARTED,
        AFR_SELF_HEAL_FAILED,
        AFR_SELF_HEAL_SYNC_BEGIN,
} afr_self_heal_status;

typedef struct {
        afr_self_heal_status gfid_or_missing_entry_self_heal;
        afr_self_heal_status metadata_self_heal;
        afr_self_heal_status data_self_heal;
        afr_self_heal_status entry_self_heal;
} afr_sh_status_for_all_type;

typedef enum {
        AFR_SELF_HEAL_ENTRY,
        AFR_SELF_HEAL_METADATA,
        AFR_SELF_HEAL_DATA,
        AFR_SELF_HEAL_GFID_OR_MISSING_ENTRY,
        AFR_SELF_HEAL_INVALID = -1,
} afr_self_heal_type;

typedef enum {
        AFR_CHECK_ALL,
        AFR_CHECK_SPECIFIC,
} afr_sh_fail_check_type;

struct afr_self_heal_ {
        /* External interface: These are variables (some optional) that
           are set by whoever has triggered self-heal */

        gf_boolean_t do_data_self_heal;
        gf_boolean_t do_metadata_self_heal;
        gf_boolean_t do_entry_self_heal;
        gf_boolean_t do_gfid_self_heal;
        gf_boolean_t do_missing_entry_self_heal;
        gf_boolean_t force_confirm_spb; /* Check for split-brains even when
                                           self-heal is turned off */

        gf_boolean_t forced_merge;        /* Is this a self-heal triggered to
                                             forcibly merge the directories? */

        gf_boolean_t background;          /* do self-heal in background
                                             if possible */
        ia_type_t type;                   /* st_mode of the entry we're doing
                                             self-heal on */
        inode_t   *inode;                 /* inode on which the self-heal is
                                             performed on */
        uuid_t  sh_gfid_req;                 /* gfid self-heal needs to be done
                                             with this gfid if it is not null */

        /* Function to call to unwind. If self-heal is being done in the
           background, this function will be called as soon as possible. */

        int (*unwind) (call_frame_t *frame, xlator_t *this, int32_t op_ret,
                       int32_t op_errno, int32_t sh_failed);

        /* End of external interface members */


        /* array of stat's, one for each child */
        struct iatt *buf;
        struct iatt *parentbufs;
        struct iatt parentbuf;
        struct iatt entrybuf;

        afr_expunge_done_cbk_t expunge_done;
        afr_impunge_done_cbk_t impunge_done;

        /* array of xattr's, one for each child */
        dict_t **xattr;

        /* array containing if the lookups succeeded in the order of response
         */
        int32_t *success_children;
        int     success_count;
        /* array containing the fresh children found in the self-heal process */
        int32_t *fresh_children;
        /* array containing the fresh children found in the parent lookup */
        int32_t *fresh_parent_dirs;
        /* array of errno's, one for each child */
        int *child_errno;
        /*loc used for lookup*/
        loc_t lookup_loc;
        int32_t lookup_flags;
        afr_lookup_done_cbk_t lookup_done;

        int32_t **pending_matrix;
        int32_t **delta_matrix;

        int32_t op_ret;
        int32_t op_errno;

        int *sources;
        int source;
        int active_source;
        int active_sinks;
        unsigned char *success;
        unsigned char *locked_nodes;
        int lock_count;

        const char *linkname;
        gf_boolean_t entries_skipped;

        gf_boolean_t actual_sh_started;
        gf_boolean_t sync_done;
        gf_boolean_t data_lock_held;
        gf_boolean_t sh_dom_lock_held;
        gf_boolean_t eof_reached;
        fd_t  *healing_fd;
        int   file_has_holes;
        blksize_t block_size;
        off_t file_size;
        off_t offset;
        unsigned char *write_needed;
        uint8_t *checksum;
        afr_post_remove_call_t post_remove_call;

        char    *data_sh_info;
        char    *metadata_sh_info;

        loc_t parent_loc;
        call_frame_t *orig_frame;
        call_frame_t *old_loop_frame;
        gf_boolean_t unwound;

        afr_sh_algo_private_t *private;
        afr_sh_status_for_all_type  afr_all_sh_status;
        afr_self_heal_type       sh_type_in_action;

        struct afr_sh_algorithm  *algo;
        afr_lock_cbk_t data_lock_success_handler;
        afr_lock_cbk_t data_lock_failure_handler;
	gf_boolean_t data_lock_block;
        int (*completion_cbk) (call_frame_t *frame, xlator_t *this);
        int (*sh_data_algo_start) (call_frame_t *frame, xlator_t *this);
        int (*algo_completion_cbk) (call_frame_t *frame, xlator_t *this);
        int (*algo_abort_cbk) (call_frame_t *frame, xlator_t *this);
        void (*gfid_sh_success_cbk) (call_frame_t *sh_frame, xlator_t *this);

        call_frame_t *sh_frame;
};

typedef struct afr_self_heal_ afr_self_heal_t;

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

typedef struct _afr_locked_fd {
        fd_t  *fd;
        struct list_head list;
} afr_locked_fd_t;

struct afr_reply {
	int	valid;
	int32_t	op_ret;
	int32_t	op_errno;
};

typedef struct _afr_local {
        int     uid;
        int     gid;
        unsigned int call_count;
        unsigned int success_count;
        unsigned int enoent_count;
        uint32_t     open_fd_count;
        gf_boolean_t update_open_fd_count;


        unsigned int unhealable;

        unsigned int read_child_index;
        unsigned char read_child_returned;
        unsigned int first_up_child;

	gf_lkowner_t  saved_lk_owner;

        int32_t op_ret;
        int32_t op_errno;

        int32_t **pending;

        loc_t loc;
        loc_t newloc;

        fd_t *fd;

        glusterfs_fop_t fop;

        unsigned char *child_up;
        int32_t       *fresh_children; //in the order of response

        int32_t *child_errno;

        dict_t  *xattr_req;

        int32_t  inodelk_count;
        int32_t  entrylk_count;

        afr_internal_lock_t internal_lock;

        afr_locked_fd_t *locked_fd;
        int32_t          source_child;
        int32_t          lock_recovery_child;

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

        int attempt_self_heal;
        int foreground_self_heal;


        /* This struct contains the arguments for the "continuation"
           (scheme-like) of fops
        */

        int   op;
        struct {
                struct {
                        unsigned char buf_set;
                        struct statvfs buf;
                } statfs;

                struct {
                        uint32_t parent_entrylk;
                        uuid_t  gfid_req;
                        inode_t *inode;
                        struct iatt buf;
                        struct iatt postparent;
                        dict_t **xattrs;
                        dict_t *xattr;
                        struct iatt *postparents;
                        struct iatt *bufs;
                        int32_t read_child;
                        int32_t *sources;
                        int32_t *success_children;
                        int32_t **pending_matrix;
                        gf_boolean_t fresh_lookup;
                        gf_boolean_t possible_spb;
                } lookup;

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
                } xattrop;

                struct {
                        dict_t *xattr;
                } fxattrop;

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

		/* pre-compute the post piggyback status before
		   entering POST-OP phase
		*/
		int              *postop_piggybacked;

		/* stub to resume on destruction
		   of the transaction frame */
		call_stub_t      *resume_stub;

		struct list_head  eager_locked;

                int32_t         **txn_changelog;//changelog after pre+post ops
                unsigned char   *pre_op;

                call_frame_t *main_frame;

                int (*fop) (call_frame_t *frame, xlator_t *this);

                int (*done) (call_frame_t *frame, xlator_t *this);

                int (*resume) (call_frame_t *frame, xlator_t *this);

                int (*unwind) (call_frame_t *frame, xlator_t *this);

                /* post-op hook */
        } transaction;

        afr_self_heal_t self_heal;

        struct marker_str     marker;

        /* extra data for fops */
        dict_t         *xdata_req;
        dict_t         *xdata_rsp;

        mode_t          umask;
        int             xflag;
        gf_boolean_t    do_discovery;
	struct afr_reply *replies;
} afr_local_t;

typedef enum {
        AFR_FD_NOT_OPENED,
        AFR_FD_OPENED,
        AFR_FD_OPENING
} afr_fd_open_status_t;

typedef struct {
        unsigned int *pre_op_done;
        afr_fd_open_status_t *opened_on; /* which subvolumes the fd is open on */
        unsigned int *pre_op_piggyback;

        unsigned int *lock_piggyback;
        unsigned int *lock_acquired;

        int flags;
        uint64_t up_count;   /* number of CHILD_UPs this fd has seen */
        uint64_t down_count; /* number of CHILD_DOWNs this fd has seen */

        int32_t last_tried;

        int  hit, miss;
        gf_boolean_t failed_over;
        struct list_head entries; /* needed for readdir failover */

        unsigned char *locked_on; /* which subvolumes locks have been successful */

	/* used for delayed-post-op optimization */
	pthread_mutex_t    delay_lock;
	gf_timer_t        *delay_timer;
	call_frame_t      *delay_frame;
        int               call_child;

	/* set if any write on this fd was a non stable write
	   (i.e, without O_SYNC or O_DSYNC)
	*/
	gf_boolean_t      witnessed_unstable_write;

	/* list of frames currently in progress */
	struct list_head  eager_locked;
} afr_fd_ctx_t;


/* try alloc and if it fails, goto label */
#define AFR_LOCAL_ALLOC_OR_GOTO(var, label) do {                    \
                var = mem_get0 (THIS->local_pool);                  \
                if (!var) {                                         \
                        gf_log (this->name, GF_LOG_ERROR,           \
                                "out of memory :(");                \
                        op_errno = ENOMEM;                          \
                        goto label;                                 \
                }                                                   \
        } while (0);


/* did a call fail due to a child failing? */
#define child_went_down(op_ret, op_errno) (((op_ret) < 0) &&            \
                                           ((op_errno == ENOTCONN) ||   \
                                            (op_errno == EBADFD)))

#define afr_fop_failed(op_ret, op_errno) ((op_ret) == -1)

/* have we tried all children? */
#define all_tried(i, count)  ((i) == (count) - 1)

int32_t
afr_set_dict_gfid (dict_t *dict, uuid_t gfid);

int
pump_command_reply (call_frame_t *frame, xlator_t *this);

int32_t
afr_notify (xlator_t *this, int32_t event, void *data, void *data2);

int
afr_init_entry_lockee (afr_entry_lockee_t *lockee, afr_local_t *local,
                       loc_t *loc, char *basename, int child_count);

void
afr_entry_lockee_cleanup (afr_internal_lock_t *int_lock);

int
afr_attempt_lock_recovery (xlator_t *this, int32_t child_index);

int
afr_save_locked_fd (xlator_t *this, fd_t *fd);

int
afr_mark_locked_nodes (xlator_t *this, fd_t *fd,
                       unsigned char *locked_nodes);

void
afr_set_lk_owner (call_frame_t *frame, xlator_t *this, void *lk_owner);

int
afr_set_lock_number (call_frame_t *frame, xlator_t *this);


loc_t *
lower_path (loc_t *l1, const char *b1, loc_t *l2, const char *b2);

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

int pump_start (call_frame_t *frame, xlator_t *this);

int
__afr_fd_ctx_set (xlator_t *this, fd_t *fd);

int
afr_fd_ctx_set (xlator_t *this, fd_t *fd);

int32_t
afr_inode_get_read_ctx (xlator_t *this, inode_t *inode, int32_t *fresh_children);

void
afr_inode_set_read_ctx (xlator_t *this, inode_t *inode, int32_t read_child,
                        int32_t *fresh_children);

int
afr_build_parent_loc (loc_t *parent, loc_t *child, int32_t *op_errno);

unsigned int
afr_up_children_count (unsigned char *child_up, unsigned int child_count);

unsigned int
afr_locked_children_count (unsigned char *children, unsigned int child_count);

unsigned int
afr_pre_op_done_children_count (unsigned char *pre_op,
                                unsigned int child_count);

gf_boolean_t
afr_is_fresh_lookup (loc_t *loc, xlator_t *this);

void
afr_update_loc_gfids (loc_t *loc, struct iatt *buf, struct iatt *postparent);

int
afr_locked_nodes_count (unsigned char *locked_nodes, int child_count);

void
afr_local_cleanup (afr_local_t *local, xlator_t *this);

int
afr_frame_return (call_frame_t *frame);

gf_boolean_t
afr_is_split_brain (xlator_t *this, inode_t *inode);

void
afr_set_split_brain (xlator_t *this, inode_t *inode, afr_spb_state_t mdata_spb,
                     afr_spb_state_t data_spb);

int
afr_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          fd_t *fd, dict_t *xdata);

void
afr_set_opendir_done (xlator_t *this, inode_t *inode);

gf_boolean_t
afr_is_opendir_done (xlator_t *this, inode_t *inode);

void
afr_local_transaction_cleanup (afr_local_t *local, xlator_t *this);

int
afr_cleanup_fd_ctx (xlator_t *this, fd_t *fd);

int
afr_launch_openfd_self_heal (call_frame_t *frame, xlator_t *this, fd_t *fd);

#define AFR_STACK_UNWIND(fop, frame, params ...)                \
        do {                                                    \
                afr_local_t *__local = NULL;                    \
                xlator_t    *__this = NULL;                     \
                if (frame) {                                    \
                        __local = frame->local;                 \
                        __this = frame->this;                   \
                        frame->local = NULL;                    \
                }                                               \
                STACK_UNWIND_STRICT (fop, frame, params);       \
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

#define AFR_NUM_CHANGE_LOGS            3 /*data + metadata + entry*/
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

int
afr_transaction_local_init (afr_local_t *local, xlator_t *this);

int32_t
afr_marker_getxattr (call_frame_t *frame, xlator_t *this,
                     loc_t *loc, const char *name,afr_local_t *local, afr_private_t *priv );

int32_t *
afr_children_create (int32_t child_count);

int
afr_local_init (afr_local_t *local, afr_private_t *priv, int32_t *op_errno);

int
afr_internal_lock_init (afr_internal_lock_t *lk, size_t child_count,
                        transaction_lk_type_t lk_type);

int
afr_first_up_child (unsigned char *child_up, size_t child_count);

int
afr_select_read_child_from_policy (int32_t *fresh_children, int32_t child_count,
                                   int32_t prev_read_child,
                                   int32_t config_read_child, int32_t *sources,
                                   unsigned int hmode, uuid_t gfid);

void
afr_set_read_ctx_from_policy (xlator_t *this, inode_t *inode,
                              int32_t *fresh_children, int32_t prev_read_child,
                              int32_t config_read_child, uuid_t gfid);

int32_t
afr_get_call_child (xlator_t *this, unsigned char *child_up, int32_t read_child,
                    int32_t *fresh_children,
                    int32_t *call_child, int32_t *last_index);

int32_t
afr_next_call_child (int32_t *fresh_children, unsigned char *child_up,
                     size_t child_count, int32_t *last_index,
                     int32_t read_child);
void
afr_get_fresh_children (int32_t *success_children, int32_t *sources,
                        int32_t *children, unsigned int child_count);
void
afr_children_add_child (int32_t *children, int32_t child,
                              int32_t child_count);
void
afr_children_rm_child (int32_t *children, int32_t child,
                             int32_t child_count);
void
afr_reset_children (int32_t *children, int32_t child_count);
int32_t
afr_most_important_error(int32_t old_errno, int32_t new_errno,
			 gf_boolean_t eio);
int
afr_errno_count (int32_t *children, int *child_errno,
                 unsigned int child_count, int32_t op_errno);
int
afr_get_children_count (int32_t *children, unsigned int child_count);
gf_boolean_t
afr_is_child_present (int32_t *success_children, int32_t child_count,
                      int32_t child);
void
afr_update_gfid_from_iatts (uuid_t uuid, struct iatt *bufs,
                            int32_t *success_children,
                            unsigned int child_count);
void
afr_reset_xattr (dict_t **xattr, unsigned int child_count);
gf_boolean_t
afr_conflicting_iattrs (struct iatt *bufs, int32_t *success_children,
                        unsigned int child_count, const char *path,
                        const char *xlator_name);
unsigned int
afr_gfid_missing_count (const char *xlator_name, int32_t *children,
                        struct iatt *bufs, unsigned int child_count,
                        const char *path);
void
afr_xattr_req_prepare (xlator_t *this, dict_t *xattr_req, const char *path);
void
afr_children_copy (int32_t *dst, int32_t *src, unsigned int child_count);
afr_transaction_type
afr_transaction_type_get (ia_type_t ia_type);
int32_t
afr_resultant_errno_get (int32_t *children,
                         int *child_errno, unsigned int child_count);
void
afr_inode_rm_stale_children (xlator_t *this, inode_t *inode,
                             int32_t *stale_children);
void
afr_launch_self_heal (call_frame_t *frame, xlator_t *this, inode_t *inode,
                      gf_boolean_t background, ia_type_t ia_type, char *reason,
                      void (*gfid_sh_success_cbk) (call_frame_t *sh_frame,
                                                   xlator_t *this),
                      int (*unwind) (call_frame_t *frame, xlator_t *this,
                                     int32_t op_ret, int32_t op_errno,
                                     int32_t sh_failed));
void
afr_fix_open (xlator_t *this, fd_t *fd, size_t need_open_count, int *need_open);

void
afr_open_fd_fix (fd_t *fd, xlator_t *this);
int
afr_set_elem_count_get (unsigned char *elems, int child_count);

afr_fd_ctx_t *
afr_fd_ctx_get (fd_t *fd, xlator_t *this);

gf_boolean_t
afr_open_only_data_self_heal (char *data_self_heal);

gf_boolean_t
afr_data_self_heal_enabled (char *data_self_heal);

void
afr_set_low_priority (call_frame_t *frame);
int
afr_child_fd_ctx_set (xlator_t *this, fd_t *fd, int32_t child,
                      int flags);

gf_boolean_t
afr_have_quorum (char *logname, afr_private_t *priv);

void
afr_matrix_cleanup (int32_t **pending, unsigned int m);

int32_t**
afr_matrix_create (unsigned int m, unsigned int n);

gf_boolean_t
afr_is_errno_set (int *child_errno, int child);

gf_boolean_t
afr_is_errno_unset (int *child_errno, int child);

gf_boolean_t
afr_is_fd_fixable (fd_t *fd);

void
afr_prepare_new_entry_pending_matrix (int32_t **pending,
                                      gf_boolean_t (*is_pending) (int *, int),
                                      int *ctx, struct iatt *buf,
                                      unsigned int child_count);
void
afr_xattr_array_destroy (dict_t **xattr, unsigned int child_count);
/*
 * Special value indicating we should use the "auto" quorum method instead of
 * a fixed value (including zero to turn off quorum enforcement).
 */
#define AFR_QUORUM_AUTO INT_MAX

/*
 * Having this as a macro will make debugging a bit weirder, but does reduce
 * the probability of functions handling this check inconsistently.
 */
#define QUORUM_CHECK(_func,_label) do {                                  \
        if (priv->quorum_count && !afr_have_quorum(this->name,priv)) { \
                gf_log(this->name,GF_LOG_WARNING,                        \
                       "failing "#_func" due to lack of quorum");        \
                op_errno = EROFS;                                        \
                goto _label;                                             \
        }                                                                \
} while (0);


#define AFR_SBRAIN_MSG "Failed on %s as split-brain is seen. Returning EIO."

#define AFR_SBRAIN_CHECK_FD(fd, label) do {                              \
        if (fd->inode && afr_is_split_brain (this, fd->inode)) {        \
                op_errno = EIO;                                         \
                gf_log (this->name, GF_LOG_WARNING,                     \
                        AFR_SBRAIN_MSG ,uuid_utoa (fd->inode->gfid));   \
                goto label;                                             \
        }                                                               \
} while (0)

#define AFR_SBRAIN_CHECK_LOC(loc, label) do {                           \
        if (loc->inode && afr_is_split_brain (this, loc->inode)) {      \
                op_errno = EIO;                                         \
                loc_path (loc, NULL);                                   \
                gf_log (this->name, GF_LOG_WARNING,                     \
                        AFR_SBRAIN_MSG , loc->path);                    \
                goto label;                                             \
        }                                                               \
} while (0)

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

afr_inode_ctx_t*
afr_inode_ctx_get (inode_t *inode, xlator_t *this);

#endif /* __AFR_H__ */
