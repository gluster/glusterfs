/*
  Copyright (c) 2008-2010 Gluster, Inc. <http://www.gluster.com>
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


#ifndef __AFR_H__
#define __AFR_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "call-stub.h"
#include "compat-errno.h"
#include "afr-mem-types.h"

#include "libxlator.h"

#define AFR_XATTR_PREFIX "trusted.afr"
#define AFR_PATHINFO_HEADER "REPLICATE:"

struct _pump_private;

typedef struct afr_inode_params_ {
        uint64_t mask_type;
        union {
                gf_boolean_t value;
                struct {
                        int32_t read_child;
                        int32_t *fresh_children;
                } read_ctx;
        } u;
} afr_inode_params_t;

typedef struct afr_inode_ctx_ {
        uint64_t masks;
        int32_t  *fresh_children;//increasing order of latency
} afr_inode_ctx_t;

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

        gf_boolean_t data_self_heal;              /* on/off */
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

        char                   vol_uuid[UUID_SIZE + 1];
        int32_t                *last_event;
} afr_private_t;

typedef struct {
        /* External interface: These are variables (some optional) that
           are set by whoever has triggered self-heal */

        gf_boolean_t need_data_self_heal;
        gf_boolean_t need_metadata_self_heal;
        gf_boolean_t need_entry_self_heal;

        gf_boolean_t forced_merge;        /* Is this a self-heal triggered to
                                             forcibly merge the directories? */

        gf_boolean_t healing_fd_opened;   /* true if caller has already
                                             opened fd */

        gf_boolean_t data_lock_held;      /* true if caller has already
                                             acquired 0-0 lock */

        fd_t *healing_fd;                 /* set if callers has opened fd */

        gf_boolean_t background;          /* do self-heal in background
                                             if possible */

        ia_type_t type;                   /* st_mode of the entry we're doing
                                             self-heal on */
        inode_t   *inode;                 /* inode on which the self-heal is
                                             performed on */

        /* Function to call to unwind. If self-heal is being done in the
           background, this function will be called as soon as possible. */

        int (*unwind) (call_frame_t *frame, xlator_t *this);

        /* End of external interface members */


        /* array of stat's, one for each child */
        struct iatt *buf;
        struct iatt parentbuf;
        struct iatt entrybuf;

        /* array of xattr's, one for each child */
        dict_t **xattr;

        /* array containing if the lookups succeeded in the order of response
         */
        int32_t *success_children;
        int     success_count;
        /* array containing the fresh children found in the self-heal process */
        int32_t *fresh_children;
        /* array of errno's, one for each child */
        int *child_errno;

        int32_t **pending_matrix;
        int32_t **delta_matrix;

        int *sources;
        int source;
        int active_source;
        int active_sinks;
        int *success;
        unsigned char *locked_nodes;
        int lock_count;

        mode_t impunging_entry_mode;
        const char *linkname;

        int   op_failed;

        int   file_has_holes;
        blksize_t block_size;
        off_t file_size;
        off_t offset;

        loc_t parent_loc;

        call_frame_t *orig_frame;
        gf_boolean_t unwound;

        /* private data for the particular self-heal algorithm */
        void *private;

        int (*flush_self_heal_cbk) (call_frame_t *frame, xlator_t *this);

        int (*completion_cbk) (call_frame_t *frame, xlator_t *this);
        int (*algo_completion_cbk) (call_frame_t *frame, xlator_t *this);
        int (*algo_abort_cbk) (call_frame_t *frame, xlator_t *this);

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

        int32_t lock_op_ret;
        int32_t lock_op_errno;

        int (*lock_cbk) (call_frame_t *, xlator_t *);

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

        int (*openfd_flush_cbk) (call_frame_t *frame, xlator_t *this);

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
                        inode_t *inode;
                        struct iatt buf;
                        struct iatt postparent;
                        ino_t ino;
                        uint64_t gen;
                        ino_t parent_ino;
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
                        int last_tried;  /* index of the child we tried previously */
                } access;

                struct {
                        int last_tried;
                        ino_t ino;
                } stat;

                struct {
                        int last_tried;
                        ino_t ino;
                } fstat;

                struct {
                        size_t size;
                        int last_tried;
                        ino_t ino;
                } readlink;

                struct {
                        char *name;
                        int last_tried;
                        long pathinfo_len;
                } getxattr;

                struct {
                        ino_t ino;
                        size_t size;
                        off_t offset;
                        int last_tried;
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
                        int last_tried;
                } readdir;

                struct {
                        int32_t op_ret;
                        int32_t op_errno;

                        size_t size;
                        off_t offset;
                        int32_t flag;

                        int last_tried;
                } getdents;

                /* inode write */

                struct {
                        ino_t ino;
                        struct iatt prebuf;
                        struct iatt postbuf;

                        int32_t op_ret;

                        struct iovec *vector;
                        struct iobref *iobref;
                        int32_t count;
                        off_t offset;
                } writev;

                struct {
                        ino_t ino;
                        struct iatt prebuf;
                        struct iatt postbuf;
                } fsync;

                struct {
                        ino_t ino;
                        off_t offset;
                        struct iatt prebuf;
                        struct iatt postbuf;
                } truncate;

                struct {
                        ino_t ino;
                        off_t offset;
                        struct iatt prebuf;
                        struct iatt postbuf;
                } ftruncate;

                struct {
                        ino_t ino;
                        struct iatt in_buf;
                        int32_t valid;
                        struct iatt preop_buf;
                        struct iatt postop_buf;
                } setattr;

                struct {
                        ino_t ino;
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
                        ino_t ino;
                        uint64_t gen;
                        ino_t parent_ino;
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
                        ino_t ino;
                        uint64_t gen;
                        ino_t parent_ino;
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
                        ino_t ino;
                        uint64_t gen;
                        ino_t parent_ino;
                        int32_t mode;
                        dict_t *params;
                        inode_t *inode;
                        struct iatt buf;
                        struct iatt read_child_buf;
                        struct iatt preparent;
                        struct iatt postparent;
                } mkdir;

                struct {
                        ino_t parent_ino;
                        int32_t op_ret;
                        int32_t op_errno;
                        struct iatt preparent;
                        struct iatt postparent;
                } unlink;

                struct {
                        int   flags;
                        ino_t parent_ino;
                        int32_t op_ret;
                        int32_t op_errno;
                        struct iatt preparent;
                        struct iatt postparent;
                } rmdir;

                struct {
                        ino_t oldparent_ino;
                        ino_t newparent_ino;
                        ino_t ino;
                        struct iatt buf;
                        struct iatt read_child_buf;
                        struct iatt preoldparent;
                        struct iatt prenewparent;
                        struct iatt postoldparent;
                        struct iatt postnewparent;
                } rename;

                struct {
                        ino_t ino;
                        uint64_t gen;
                        ino_t parent_ino;
                        inode_t *inode;
                        struct iatt buf;
                        struct iatt read_child_buf;
                        struct iatt preparent;
                        struct iatt postparent;
                } link;

                struct {
                        ino_t ino;
                        uint64_t gen;
                        ino_t parent_ino;
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


typedef struct {
        unsigned int *pre_op_done;
        unsigned int *opened_on;     /* which subvolumes the fd is open on */
        unsigned int *pre_op_piggyback;

        int flags;
        int32_t wbflags;
        uint64_t up_count;   /* number of CHILD_UPs this fd has seen */
        uint64_t down_count; /* number of CHILD_DOWNs this fd has seen */

        int32_t last_tried;

        int  hit, miss;
        gf_boolean_t failed_over;
        struct list_head entries; /* needed for readdir failover */

        unsigned char *locked_on; /* which subvolumes locks have been successful */
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

int
afr_up_children_count (int child_count, unsigned char *child_up);

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

uint64_t
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
afr_openfd_flush (call_frame_t *frame, xlator_t *this, fd_t *fd);

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
afr_transaction_local_init (afr_local_t *local, afr_private_t *priv);

int32_t
afr_marker_getxattr (call_frame_t *frame, xlator_t *this,
                     loc_t *loc, const char *name,afr_local_t *local, afr_private_t *priv );

int32_t *
afr_fresh_children_create (int32_t child_count);

int
AFR_LOCAL_INIT (afr_local_t *local, afr_private_t *priv);

int
afr_internal_lock_init (afr_internal_lock_t *lk, size_t child_count,
                        transaction_lk_type_t lk_type);
int
afr_select_read_child_from_policy (int32_t *fresh_children, int32_t child_count,
                                   int32_t prev_read_child,
                                   int32_t config_read_child, int32_t *sources);

void
afr_set_read_ctx_from_policy (xlator_t *this, inode_t *inode,
                              int32_t *fresh_children, int32_t prev_read_child,
                              int32_t config_read_child);

/**
 * first_up_child - return the index of the first child that is up
 */

static inline int
afr_first_up_child (afr_private_t *priv)
{
        xlator_t ** children = NULL;
        int         ret      = -1;
        int         i        = 0;

        LOCK (&priv->lock);
        {
                children = priv->children;
                for (i = 0; i < priv->child_count; i++) {
                        if (priv->child_up[i]) {
                                ret = i;
                                break;
                        }
                }
        }
        UNLOCK (&priv->lock);

        return ret;
}

int32_t
afr_next_call_child (int32_t *fresh_children, size_t child_count,
                     int32_t *last_index, int32_t read_child);
void
afr_get_fresh_children (int32_t *success_children, int32_t *sources,
                        int32_t *fresh_children, unsigned int child_count);
void
afr_fresh_children_add_child (int32_t *fresh_children, int32_t child,
                              int32_t child_count);
void
afr_reset_children (int32_t *fresh_children, int32_t child_count);
#endif /* __AFR_H__ */
