/*
  Copyright (c) 2008-2011 Gluster, Inc. <http://www.gluster.com>
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

#define AFR_XATTR_PREFIX "trusted.afr"
#define AFR_PATHINFO_HEADER "REPLICATE:"

struct _pump_private;

typedef int (*afr_expunge_done_cbk_t) (call_frame_t *frame, xlator_t *this,
                                       int child, int32_t op_error,
                                       int32_t op_errno);

typedef int (*afr_impunge_done_cbk_t) (call_frame_t *frame, xlator_t *this,
                                       int child, int32_t op_error,
                                       int32_t op_errno);
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
        AFR_INODE_SET_READ_CTX = 1,
        AFR_INODE_RM_STALE_CHILDREN,
        AFR_INODE_SET_OPENDIR_DONE,
        AFR_INODE_SET_SPLIT_BRAIN,
        AFR_INODE_GET_READ_CTX,
        AFR_INODE_GET_OPENDIR_DONE,
        AFR_INODE_GET_SPLIT_BRAIN,
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

typedef struct afr_inode_ctx_ {
        uint64_t masks;
        int32_t  *fresh_children;//increasing order of latency
} afr_inode_ctx_t;

typedef struct afr_self_heald_ {
        gf_boolean_t    enabled;
        gf_boolean_t    pending;
        gf_boolean_t    inprogress;
        afr_child_pos_t *pos;
} afr_self_heald_t;

typedef struct _afr_private {
        gf_lock_t lock;               /* to guard access to child_count, etc */
        unsigned int child_count;     /* total number of children   */

        unsigned int read_child_rr;   /* round-robin index of the read_child */
        gf_lock_t read_child_lock;    /* lock to protect above */

        xlator_t **children;

        gf_lock_t root_inode_lk;
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
        int favorite_child;  /* subvolume to be preferred in resolving
                                         split-brain cases */

        unsigned int data_lock_server_count;
        unsigned int metadata_lock_server_count;
        unsigned int entry_lock_server_count;

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
        gf_boolean_t     optimistic_change_log;
        gf_boolean_t     eager_lock;
	gf_boolean_t     enforce_quorum;

        char                   vol_uuid[UUID_SIZE + 1];
        int32_t                *last_event;
        afr_self_heald_t       shd;
} afr_private_t;

typedef struct {
        /* External interface: These are variables (some optional) that
           are set by whoever has triggered self-heal */

        gf_boolean_t do_data_self_heal;
        gf_boolean_t do_metadata_self_heal;
        gf_boolean_t do_entry_self_heal;
        gf_boolean_t do_gfid_self_heal;
        gf_boolean_t do_missing_entry_self_heal;

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
                       int32_t op_errno);

        /* End of external interface members */


        /* array of stat's, one for each child */
        struct iatt *buf;
        struct iatt *parentbufs;
        struct iatt parentbuf;
        struct iatt entrybuf;

        afr_expunge_done_cbk_t expunge_done;
        afr_impunge_done_cbk_t impunge_done;
        int32_t impunge_ret_child;

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

        mode_t impunging_entry_mode;
        const char *linkname;
        gf_boolean_t entries_skipped;

        int   op_failed;

        gf_boolean_t data_lock_held;
        gf_boolean_t eof_reached;
        fd_t  *healing_fd;
        int   file_has_holes;
        blksize_t block_size;
        off_t file_size;
        off_t offset;
        unsigned char *write_needed;
        uint8_t *checksum;
        afr_post_remove_call_t post_remove_call;

        loc_t parent_loc;

        call_frame_t *orig_frame;
        call_frame_t *old_loop_frame;
        gf_boolean_t unwound;

        afr_sh_algo_private_t *private;

        afr_lock_cbk_t data_lock_success_handler;
        afr_lock_cbk_t data_lock_failure_handler;
        int (*completion_cbk) (call_frame_t *frame, xlator_t *this);
        int (*sh_data_algo_start) (call_frame_t *frame, xlator_t *this);
        int (*algo_completion_cbk) (call_frame_t *frame, xlator_t *this);
        int (*algo_abort_cbk) (call_frame_t *frame, xlator_t *this);
        void (*gfid_sh_success_cbk) (call_frame_t *sh_frame, xlator_t *this);

        call_frame_t *sh_frame;
} afr_self_heal_t;

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
        loc_t *lk_loc;
        struct gf_flock lk_flock;

        const char *lk_basename;
        const char *lower_basename;
        const char *higher_basename;
        char lower_locked;
        char higher_locked;

        unsigned char *locked_nodes;
        unsigned char *lower_locked_nodes;
        unsigned char *inode_locked_nodes;
        unsigned char *entry_locked_nodes;

        selfheal_lk_type_t selfheal_lk_type;
        transaction_lk_type_t transaction_lk_type;

        int32_t lock_count;
        int32_t inodelk_lock_count;
        int32_t entrylk_lock_count;

        uint64_t lock_number;
        int32_t lk_call_count;
        int32_t lk_expected_count;

        int32_t lock_op_ret;
        int32_t lock_op_errno;
        afr_lock_cbk_t lock_cbk;
} afr_internal_lock_t;

typedef struct _afr_locked_fd {
        fd_t  *fd;
        struct list_head list;
} afr_locked_fd_t;

typedef struct _afr_local {
        int     uid;
        int     gid;
        unsigned int call_count;
        unsigned int success_count;
        unsigned int enoent_count;


        unsigned int govinda_gOvinda;

        unsigned int read_child_index;
        unsigned char read_child_returned;
        unsigned int first_up_child;

        pid_t saved_pid;

        int32_t op_ret;
        int32_t op_errno;

        int32_t **pending;

        loc_t loc;
        loc_t newloc;

        fd_t *fd;
        unsigned char *fd_open_on;

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

        gf_boolean_t    fop_paused;
        int (*fop_call_continue) (call_frame_t *frame, xlator_t *this);

        /*
          This struct contains the arguments for the "continuation"
          (scheme-like) of fops
        */

        int   op;
        struct {
                struct {
                        unsigned char buf_set;
                        struct statvfs buf;
                } statfs;

                struct {
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
                } lookup;

                struct {
                        int32_t flags;
                        int32_t wbflags;
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
                        long pathinfo_len;
                } getxattr;

                struct {
                        size_t size;
                        off_t offset;
                        int last_index;
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

                        gf_boolean_t failed;
                        int last_index;
                } readdir;
                /* inode write */

                struct {
                        struct iatt prebuf;
                        struct iatt postbuf;

                        int32_t op_ret;

                        struct iovec *vector;
                        struct iobref *iobref;
                        int32_t count;
                        off_t offset;
                } writev;

                struct {
                        struct iatt prebuf;
                        struct iatt postbuf;
                } fsync;

                struct {
                        off_t offset;
                        struct iatt prebuf;
                        struct iatt postbuf;
                } truncate;

                struct {
                        off_t offset;
                        struct iatt prebuf;
                        struct iatt postbuf;
                } ftruncate;

                struct {
                        struct iatt in_buf;
                        int32_t valid;
                        struct iatt preop_buf;
                        struct iatt postop_buf;
                } setattr;

                struct {
                        struct iatt in_buf;
                        int32_t valid;
                        struct iatt preop_buf;
                        struct iatt postop_buf;
                } fsetattr;

                struct {
                        dict_t *dict;
                        int32_t flags;
                } setxattr;

                struct {
                        char *name;
                } removexattr;

                /* dir write */

                struct {
                        fd_t *fd;
                        dict_t *params;
                        int32_t flags;
                        mode_t mode;
                        inode_t *inode;
                        struct iatt buf;
                        struct iatt preparent;
                        struct iatt postparent;
                        struct iatt read_child_buf;
                } create;

                struct {
                        dev_t dev;
                        mode_t mode;
                        dict_t *params;
                        inode_t *inode;
                        struct iatt buf;
                        struct iatt preparent;
                        struct iatt postparent;
                        struct iatt read_child_buf;
                } mknod;

                struct {
                        int32_t mode;
                        dict_t *params;
                        inode_t *inode;
                        struct iatt buf;
                        struct iatt read_child_buf;
                        struct iatt preparent;
                        struct iatt postparent;
                } mkdir;

                struct {
                        int32_t op_ret;
                        int32_t op_errno;
                        struct iatt preparent;
                        struct iatt postparent;
                } unlink;

                struct {
                        int   flags;
                        int32_t op_ret;
                        int32_t op_errno;
                        struct iatt preparent;
                        struct iatt postparent;
                } rmdir;

                struct {
                        struct iatt buf;
                        struct iatt read_child_buf;
                        struct iatt preoldparent;
                        struct iatt prenewparent;
                        struct iatt postoldparent;
                        struct iatt postnewparent;
                } rename;

                struct {
                        inode_t *inode;
                        struct iatt buf;
                        struct iatt read_child_buf;
                        struct iatt preparent;
                        struct iatt postparent;
                } link;

                struct {
                        inode_t *inode;
                        dict_t *params;
                        struct iatt buf;
                        struct iatt read_child_buf;
                        char *linkpath;
                        struct iatt preparent;
                        struct iatt postparent;
                } symlink;

                struct {
                        int32_t flags;
                        dir_entry_t *entries;
                        int32_t count;
                } setdents;
        } cont;

        struct {
                off_t start, len;

                int *eager_lock;

                char *basename;
                char *new_basename;

                loc_t parent_loc;
                loc_t new_parent_loc;

                afr_transaction_type type;

                int success_count;
                int erase_pending;
                int failure_count;

                int last_tried;
                int32_t *child_errno;
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
} afr_local_t;

typedef enum {
        AFR_FD_NOT_OPENED,
        AFR_FD_OPENED,
        AFR_FD_OPENING
} afr_fd_open_status_t;

typedef struct {
        struct list_head call_list;
        call_frame_t    *frame;
} afr_fd_paused_call_t;

typedef struct {
        unsigned int *pre_op_done;
        afr_fd_open_status_t *opened_on; /* which subvolumes the fd is open on */
        unsigned int *pre_op_piggyback;

        unsigned int *lock_piggyback;
        unsigned int *lock_acquired;

        int flags;
        int32_t wbflags;
        uint64_t up_count;   /* number of CHILD_UPs this fd has seen */
        uint64_t down_count; /* number of CHILD_DOWNs this fd has seen */

        int32_t last_tried;

        int  hit, miss;
        gf_boolean_t failed_over;
        struct list_head entries; /* needed for readdir failover */

        unsigned char *locked_on; /* which subvolumes locks have been successful */
	struct list_head  paused_calls; /* queued calls while fix_open happens  */
} afr_fd_ctx_t;


/* try alloc and if it fails, goto label */
#define ALLOC_OR_GOTO(var, type, label) do {                    \
                var = GF_CALLOC (sizeof (type), 1,              \
                                 gf_afr_mt_##type);             \
                if (!var) {                                     \
                        gf_log (this->name, GF_LOG_ERROR,       \
                                "out of memory :(");            \
                        op_errno = ENOMEM;                      \
                        goto label;                             \
                }                                               \
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
afr_notify (xlator_t *this, int32_t event,
            void *data, ...);

int
afr_attempt_lock_recovery (xlator_t *this, int32_t child_index);

int
afr_save_locked_fd (xlator_t *this, fd_t *fd);

int
afr_mark_locked_nodes (xlator_t *this, fd_t *fd,
                       unsigned char *locked_nodes);

void
afr_set_lk_owner (call_frame_t *frame, xlator_t *this);

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

void
afr_lk_transfer_datalock (call_frame_t *dst, call_frame_t *src,
                          unsigned int child_count);

int pump_start (call_frame_t *frame, xlator_t *this);

int
afr_fd_ctx_set (xlator_t *this, fd_t *fd);

int32_t
afr_inode_get_read_ctx (xlator_t *this, inode_t *inode, int32_t *fresh_children);

void
afr_inode_set_read_ctx (xlator_t *this, inode_t *inode, int32_t read_child,
                        int32_t *fresh_children);

void
afr_build_parent_loc (loc_t *parent, loc_t *child);

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
afr_set_split_brain (xlator_t *this, inode_t *inode, gf_boolean_t set);

int
afr_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          fd_t *fd, int32_t wbflags);

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
                afr_local_cleanup (__local, __this);            \
                GF_FREE (__local);                              \
        } while (0);

#define AFR_STACK_DESTROY(frame)                        \
        do {                                            \
                afr_local_t *__local = NULL;            \
                xlator_t    *__this = NULL;             \
                __local = frame->local;                 \
                __this = frame->this;                   \
                frame->local = NULL;                    \
                STACK_DESTROY (frame->root);            \
                afr_local_cleanup (__local, __this);    \
                GF_FREE (__local);                      \
        } while (0);

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
AFR_LOCAL_INIT (afr_local_t *local, afr_private_t *priv);

int
afr_internal_lock_init (afr_internal_lock_t *lk, size_t child_count,
                        transaction_lk_type_t lk_type);

int
afr_first_up_child (unsigned char *child_up, size_t child_count);

int
afr_select_read_child_from_policy (int32_t *fresh_children, int32_t child_count,
                                   int32_t prev_read_child,
                                   int32_t config_read_child, int32_t *sources);

void
afr_set_read_ctx_from_policy (xlator_t *this, inode_t *inode,
                              int32_t *fresh_children, int32_t prev_read_child,
                              int32_t config_read_child);

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
gf_boolean_t
afr_error_more_important (int32_t old_errno, int32_t new_errno);
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
afr_inode_rm_stale_children (xlator_t *this, inode_t *inode, int32_t read_child,
                             int32_t *stale_children);
void
afr_launch_self_heal (call_frame_t *frame, xlator_t *this, inode_t *inode,
                      gf_boolean_t background, ia_type_t ia_type, char *reason,
                      void (*gfid_sh_success_cbk) (call_frame_t *sh_frame,
                                                   xlator_t *this),
                      int (*unwind) (call_frame_t *frame, xlator_t *this,
                                     int32_t op_ret, int32_t op_errno));
int
afr_fix_open (call_frame_t *frame, xlator_t *this, afr_fd_ctx_t *fd_ctx,
              int need_open_count, int *need_open);
int
afr_open_fd_fix (call_frame_t *frame, xlator_t *this, gf_boolean_t pause_fop);
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
                      int flags, int32_t wb_flags);

gf_boolean_t
afr_have_quorum (char *logname, afr_private_t *priv);

/*
 * Having this as a macro will make debugging a bit weirder, but does reduce
 * the probability of functions handling this check inconsistently.
 */
#define QUORUM_CHECK(_func,_label) do {                                  \
	if (priv->enforce_quorum && !afr_have_quorum(this->name,priv)) { \
		gf_log(this->name,GF_LOG_WARNING,                        \
		       "failing "#_func" due to lack of quorum");        \
		op_errno = EROFS;                                        \
		goto _label;                                             \
	}                                                                \
} while (0);

#endif /* __AFR_H__ */
