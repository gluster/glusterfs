/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _CHANGELOG_HELPERS_H
#define _CHANGELOG_HELPERS_H

#include "locking.h"
#include "timer.h"
#include "pthread.h"
#include "iobuf.h"
#include "rot-buffs.h"

#include "changelog-misc.h"
#include "call-stub.h"

#include "rpcsvc.h"
#include "changelog-ev-handle.h"

#include "changelog.h"
#include "changelog-messages.h"

/**
 * the changelog entry
 */
typedef struct changelog_log_data {
        /* rollover related */
        unsigned long cld_roll_time;

        /* reopen changelog? */
        gf_boolean_t cld_finale;

        changelog_log_type cld_type;

        /**
         * sincd gfid is _always_ a necessity, it's not a part
         * of the iobuf. by doing this we do not add any overhead
         * for data and metadata related fops.
         */
        uuid_t        cld_gfid;

        /**
         * iobufs are used for optionals records: pargfid, path,
         * write offsets etc.. It's the fop implementers job
         * to allocate (iobuf_get() in the fop) and get unref'ed
         * in the callback (CHANGELOG_STACK_UNWIND).
         */
        struct iobuf *cld_iobuf;

#define cld_ptr cld_iobuf->ptr

        /**
         * after allocation you can point this to the length of
         * usable data, but make sure it does not exceed the
         * the size of the requested iobuf.
         */
        size_t        cld_iobuf_len;

#define cld_ptr_len cld_iobuf_len

        /**
         * number of optional records
         */
        int cld_xtra_records;
} changelog_log_data_t;

/**
 * holder for dispatch function and private data
 */

typedef struct changelog_priv changelog_priv_t;

typedef struct changelog_dispatcher {
        void *cd_data;
        int (*dispatchfn) (xlator_t *, changelog_priv_t *, void *,
                           changelog_log_data_t *, changelog_log_data_t *);
} changelog_dispatcher_t;

struct changelog_bootstrap {
        changelog_mode_t mode;
        int (*ctor) (xlator_t *, changelog_dispatcher_t *);
        int (*dtor) (xlator_t *, changelog_dispatcher_t *);
};

struct changelog_encoder {
        changelog_encoder_t encoder;
        int (*encode) (xlator_t *, changelog_log_data_t *);
};


/* xlator private */

typedef struct changelog_time_slice {
        /**
         * just in case we need nanosecond granularity some day.
         * field is unused as of now (maybe we'd need it later).
         */
        struct timeval tv_start;

        /**
         * version of changelog file, incremented each time changes
         * rollover.
         */
        unsigned long changelog_version[CHANGELOG_MAX_TYPE];
} changelog_time_slice_t;

typedef struct changelog_rollover {
        /* rollover thread */
        pthread_t rollover_th;

        xlator_t *this;

        pthread_mutex_t lock;
        pthread_cond_t cond;
        gf_boolean_t notify;
} changelog_rollover_t;

typedef struct changelog_fsync {
        /* fsync() thread */
        pthread_t fsync_th;

        xlator_t *this;
} changelog_fsync_t;

/* Draining during changelog rollover (for geo-rep snapshot dependency):
 * --------------------------------------------------------------------
 * The introduction of draining of in-transit fops during changelog rollover
 * (both explicit/timeout triggered) requires coloring of fops. Basically the
 * implementation requires two counters, one counter which keeps the count of
 * current intransit fops which should end up in current changelog and the other
 * counter to keep track of incoming fops which should be drained as part of
 * next changelog rollover event. The fops are colored w.r.t these counters.
 * The fops that are to be drained as part of current changelog rollover is
 * given one color and the fops which keep incoming during this and not
 * necessarily should end up in current changelog and should be drained as part
 * of next changelog rollover are given other color. The color switching
 * continues with each changelog rollover. Two colors(black and white) are
 * chosen here and initially black is chosen is default.
 */

typedef enum chlog_fop_color {
         FOP_COLOR_BLACK,
         FOP_COLOR_WHITE
} chlog_fop_color_t;

/* Barrier notify variable */
typedef struct barrier_notify {
         pthread_mutex_t        bnotify_mutex;
         pthread_cond_t         bnotify_cond;
         gf_boolean_t           bnotify;
         gf_boolean_t           bnotify_error;
} barrier_notify_t;

/* Two separate mutex and conditional variable set is used
 * to drain white and black fops. */

typedef struct drain_mgmt {
         pthread_mutex_t        drain_black_mutex;
         pthread_cond_t         drain_black_cond;
         pthread_mutex_t        drain_white_mutex;
         pthread_cond_t         drain_white_cond;
         /* Represents black fops count in-transit */
         unsigned long          black_fop_cnt;
         /* Represents white fops count in-transit */
         unsigned long          white_fop_cnt;
         gf_boolean_t           drain_wait_black;
         gf_boolean_t           drain_wait_white;
} drain_mgmt_t;

/* External barrier as a result of snap on/off indicating flag*/
typedef struct barrier_flags {
        gf_lock_t lock;
        gf_boolean_t barrier_ext;
} barrier_flags_t;

/* Event selection */
typedef struct changelog_ev_selector {
        gf_lock_t reflock;

        /**
         * Array of references for each selection bit.
         */
        unsigned int ref[CHANGELOG_EV_SELECTION_RANGE];
} changelog_ev_selector_t;


/* changelog's private structure */
struct changelog_priv {
        gf_boolean_t active;

        /* to generate unique socket file per brick */
        char *changelog_brick;

        /* logging directory */
        char *changelog_dir;

        /* htime directory */
        char *htime_dir;

        /* one file for all changelog types */
        int changelog_fd;

        /* htime fd for current changelog session */
        int htime_fd;

        /*  c_snap_fd is fd for call-path changelog */
        int c_snap_fd;

        /* rollover_count used by htime */
        int  rollover_count;

        gf_lock_t lock;

        /*  lock to synchronize CSNAP updation */
        gf_lock_t c_snap_lock;

        /* written end of the pipe */
        int wfd;

        /* rollover time */
        int32_t rollover_time;

        /* fsync() interval */
        int32_t fsync_interval;

        /* changelog type maps */
        const char *maps[CHANGELOG_MAX_TYPE];

        /* time slicer */
        changelog_time_slice_t slice;

        /* context of the updater */
        changelog_dispatcher_t cd;

        /* context of the rollover thread */
        changelog_rollover_t cr;

        /* context of fsync thread */
        changelog_fsync_t cf;

        /* operation mode */
        changelog_mode_t op_mode;

        /* bootstrap routine for 'current' logger */
        struct changelog_bootstrap *cb;

        /* encoder mode */
        changelog_encoder_t encode_mode;

        /* encoder */
        struct changelog_encoder *ce;

        /**
         * snapshot dependency changes
         */

        /* Draining of fops*/
        drain_mgmt_t dm;

        /* Represents the active color. Initially by default black */
        chlog_fop_color_t current_color;

        /* flag to determine explicit rollover is triggered */
        gf_boolean_t explicit_rollover;

        /* barrier notification variable protected by mutex */
        barrier_notify_t bn;

        /* barrier on/off indicating flags */
        barrier_flags_t bflags;

        /* changelog barrier on/off indicating flag */
        gf_boolean_t      barrier_enabled;
        struct list_head  queue;
        uint32_t          queue_size;
        gf_timer_t       *timer;
        struct timespec   timeout;

        /**
         * buffers, RPC, event selection, notifications and other
         * beasts.
         */

        /* epoll pthread */
        pthread_t poller;

        /* rotational buffer */
        rbuf_t *rbuf;

        /* changelog RPC server */
        rpcsvc_t *rpc;

        /* event selection */
        changelog_ev_selector_t ev_selection;

        /* client handling (reverse connection) */
        pthread_t connector;

        int nr_dispatchers;
        pthread_t *ev_dispatcher;

        changelog_clnt_t connections;

        /* glusterfind dependency to capture paths on deleted entries*/
        gf_boolean_t capture_del_path;
};

struct changelog_local {
        inode_t              *inode;
        gf_boolean_t          update_no_check;

        changelog_log_data_t  cld;

        /**
         * ->prev_entry is used in cases when there needs to be
         * additional changelog entry for the parent (eg. rename)
         * It's analogous to ->next in single linked list world,
         * but we call it as ->prev_entry... ha ha ha
         */
        struct changelog_local *prev_entry;

        /* snap dependency changes */
        chlog_fop_color_t color;
};

typedef struct changelog_local changelog_local_t;

/* inode version is stored in inode ctx */
typedef struct changelog_inode_ctx {
        unsigned long iversion[CHANGELOG_MAX_TYPE];
} changelog_inode_ctx_t;

#define CHANGELOG_INODE_VERSION_TYPE(ctx, type)  &(ctx->iversion[type])

/**
 * Optional Records:
 *  fops that need to save additional information request a array of
 *  @changelog_opt_t struct. The array is allocated via @iobufs.
 */
typedef enum {
        CHANGELOG_OPT_REC_FOP,
        CHANGELOG_OPT_REC_ENTRY,
        CHANGELOG_OPT_REC_UINT32,
} changelog_optional_rec_type_t;

struct changelog_entry_fields {
        uuid_t  cef_uuid;
        char   *cef_bname;
        char   *cef_path;
};

typedef struct {
        /**
         * @co_covert can be used to do post-processing of the record before
         * it's persisted to the CHANGELOG. If this is NULL, then the record
         * is persisted as per it's in memory format.
         */
        size_t (*co_convert) (void *data, char *buffer, gf_boolean_t encode);

        /* release routines */
        void (*co_free) (void *data);

        /* type of the field */
        changelog_optional_rec_type_t co_type;

        /**
         * sizeof of the 'valid' field in the union. This field is not used if
         * @co_convert is specified.
         */
        size_t co_len;

        union {
                unsigned int                  co_uint32;
                glusterfs_fop_t               co_fop;
                struct changelog_entry_fields co_entry;
        };
} changelog_opt_t;

#define CHANGELOG_OPT_RECORD_LEN  sizeof (changelog_opt_t)

/**
 * helpers routines
 */

int
changelog_thread_cleanup (xlator_t *this, pthread_t thr_id);

void *
changelog_get_usable_buffer (changelog_local_t *local);

void
changelog_set_usable_record_and_length (changelog_local_t *local,
                                        size_t len, int xr);
void
changelog_local_cleanup (xlator_t *xl, changelog_local_t *local);
changelog_local_t *
changelog_local_init (xlator_t *this, inode_t *inode, uuid_t gfid,
                      int xtra_records, gf_boolean_t update_flag);
int
changelog_start_next_change (xlator_t *this,
                             changelog_priv_t *priv,
                             unsigned long ts, gf_boolean_t finale);
int
changelog_open_journal (xlator_t *this, changelog_priv_t *priv);
int
changelog_fill_rollover_data (changelog_log_data_t *cld, gf_boolean_t is_last);
int
changelog_inject_single_event (xlator_t *this,
                               changelog_priv_t *priv,
                               changelog_log_data_t *cld);
size_t
changelog_entry_length ();
int
changelog_write (int fd, char *buffer, size_t len);
int
changelog_write_change (changelog_priv_t *priv, char *buffer, size_t len);
int
changelog_handle_change (xlator_t *this,
                         changelog_priv_t *priv, changelog_log_data_t *cld);
void
changelog_update (xlator_t *this, changelog_priv_t *priv,
                  changelog_local_t *local, changelog_log_type type);
void *
changelog_rollover (void *data);
void *
changelog_fsync_thread (void *data);
int
changelog_forget (xlator_t *this, inode_t *inode);
int
htime_update (xlator_t *this, changelog_priv_t *priv,
              unsigned long ts, char * buffer);
int
htime_open (xlator_t *this, changelog_priv_t *priv, unsigned long ts);
int
htime_create (xlator_t *this, changelog_priv_t *priv, unsigned long ts);

/* Geo-Rep snapshot dependency changes */
void
changelog_color_fop_and_inc_cnt (xlator_t *this, changelog_priv_t *priv,
                                                 changelog_local_t *local);
void
changelog_inc_fop_cnt (xlator_t *this, changelog_priv_t *priv,
                                       changelog_local_t *local);
void
changelog_dec_fop_cnt (xlator_t *this, changelog_priv_t *priv,
                                       changelog_local_t *local);
int
changelog_barrier_notify (changelog_priv_t *priv, char* buf);
void
changelog_barrier_cleanup (xlator_t *this, changelog_priv_t *priv,
                                                struct list_head *queue);
void
changelog_drain_white_fops (xlator_t *this, changelog_priv_t *priv);
void
changelog_drain_black_fops (xlator_t *this, changelog_priv_t *priv);

/* Crash consistency of changelog wrt snapshot */
int
changelog_snap_logging_stop ( xlator_t *this, changelog_priv_t *priv);
int
changelog_snap_logging_start ( xlator_t *this, changelog_priv_t *priv);
int
changelog_snap_open ( xlator_t *this, changelog_priv_t *priv);
int
changelog_snap_handle_ascii_change (xlator_t *this,
                changelog_log_data_t *cld);
int
changelog_snap_write_change (changelog_priv_t *priv, char *buffer, size_t len);

/* Changelog barrier routines */
void __chlog_barrier_enqueue (xlator_t *this, call_stub_t *stub);
void __chlog_barrier_disable (xlator_t *this, struct list_head *queue);
void chlog_barrier_dequeue_all (xlator_t *this, struct list_head *queue);
call_stub_t *__chlog_barrier_dequeue (xlator_t *this, struct list_head *queue);
int __chlog_barrier_enable (xlator_t *this, changelog_priv_t *priv);

int32_t
changelog_fill_entry_buf (call_frame_t *frame, xlator_t *this,
                          loc_t *loc, changelog_local_t **local);

/* event selection routines */
void changelog_select_event (xlator_t *,
                                    changelog_ev_selector_t *, unsigned int);
void changelog_deselect_event (xlator_t *,
                                      changelog_ev_selector_t *, unsigned int);
int changelog_init_event_selection (xlator_t *,
                                           changelog_ev_selector_t *);
int changelog_cleanup_event_selection (xlator_t *,
                                              changelog_ev_selector_t *);
int changelog_ev_selected (xlator_t *,
                                  changelog_ev_selector_t *, unsigned int);
void
changelog_dispatch_event (xlator_t *, changelog_priv_t *, changelog_event_t *);

changelog_inode_ctx_t *
__changelog_inode_ctx_get (xlator_t *, inode_t *, unsigned long **,
                           unsigned long *, changelog_log_type);
int
resolve_pargfid_to_path (xlator_t *this, const uuid_t gfid, char **path,
                         char *bname);

/* macros */

#define CHANGELOG_STACK_UNWIND(fop, frame, params ...) do {             \
                changelog_local_t *__local = NULL;                      \
                xlator_t          *__xl    = NULL;                      \
                if (frame) {                                            \
                        __local      = frame->local;                    \
                        __xl         = frame->this;                     \
                        frame->local = NULL;                            \
                }                                                       \
                STACK_UNWIND_STRICT (fop, frame, params);               \
                if (__local && __local->prev_entry)                     \
                        changelog_local_cleanup (__xl,                  \
                                                 __local->prev_entry);  \
                changelog_local_cleanup (__xl, __local);                \
        } while (0)

#define CHANGELOG_IOBUF_REF(iobuf) do {         \
                if (iobuf)                      \
                        iobuf_ref (iobuf);      \
        } while (0)

#define CHANGELOG_IOBUF_UNREF(iobuf) do {       \
                if (iobuf)                      \
                        iobuf_unref (iobuf);    \
        } while (0)

#define CHANGELOG_FILL_BUFFER(buffer, off, val, len) do {       \
                memcpy (buffer + off, val, len);                \
                off += len;                                     \
        } while (0)

#define SLICE_VERSION_UPDATE(slice) do {                \
                int i = 0;                              \
                for (; i < CHANGELOG_MAX_TYPE; i++) {   \
                        slice->changelog_version[i]++;  \
                }                                       \
        } while (0)

#define CHANGELOG_FILL_UINT32(co, number, converter, xlen) do { \
                co->co_convert = converter;                     \
                co->co_free = NULL;                             \
                co->co_type = CHANGELOG_OPT_REC_UINT32;         \
                co->co_uint32 = number;                         \
                xlen += sizeof (unsigned int);                 \
        } while (0)

#define CHANGLOG_FILL_FOP_NUMBER(co, fop, converter, xlen) do { \
                co->co_convert = converter;                     \
                co->co_free = NULL;                             \
                co->co_type = CHANGELOG_OPT_REC_FOP;            \
                co->co_fop = fop;                               \
                xlen += sizeof (fop);                           \
        } while (0)

#define CHANGELOG_FILL_ENTRY(co, pargfid, bname,                        \
                             converter, freefn, xlen, label)            \
        do {                                                            \
                co->co_convert = converter;                             \
                co->co_free = freefn;                                   \
                co->co_type = CHANGELOG_OPT_REC_ENTRY;                  \
                gf_uuid_copy (co->co_entry.cef_uuid, pargfid);          \
                co->co_entry.cef_bname = gf_strdup(bname);              \
                if (!co->co_entry.cef_bname)                            \
                        goto label;                                     \
                xlen += (UUID_CANONICAL_FORM_LEN + strlen (bname));     \
        } while (0)

#define CHANGELOG_FILL_ENTRY_DIR_PATH(co, pargfid, bname, converter,        \
                                      del_freefn, xlen, label, capture_del) \
        do {                                                                \
                co->co_convert = converter;                                 \
                co->co_free = del_freefn;                                   \
                co->co_type = CHANGELOG_OPT_REC_ENTRY;                      \
                gf_uuid_copy (co->co_entry.cef_uuid, pargfid);              \
                co->co_entry.cef_bname = gf_strdup(bname);                  \
                if (!co->co_entry.cef_bname)                                \
                        goto label;                                         \
                xlen += (UUID_CANONICAL_FORM_LEN + strlen (bname));         \
                if (!capture_del || resolve_pargfid_to_path (this, pargfid, \
                    &(co->co_entry.cef_path), co->co_entry.cef_bname)) {    \
                        co->co_entry.cef_path = gf_strdup ("\0");           \
                        xlen += 1;                                          \
                } else {                                                    \
                        xlen += (strlen (co->co_entry.cef_path));           \
                }                                                           \
        } while (0)

#define CHANGELOG_INIT(this, local, inode, gfid, xrec)                  \
        local = changelog_local_init (this, inode, gfid, xrec, _gf_false)

#define CHANGELOG_INIT_NOCHECK(this, local, inode, gfid, xrec)          \
        local = changelog_local_init (this, inode, gfid, xrec, _gf_true)

#define CHANGELOG_NOT_ACTIVE_THEN_GOTO(frame, priv, label) do {      \
                if (!priv->active)                                   \
                        goto label;                                  \
                /* ignore rebalance process's activity. */           \
                if ((frame->root->pid == GF_CLIENT_PID_DEFRAG) ||    \
                    (frame->root->pid == GF_CLIENT_PID_TIER_DEFRAG)) \
                        goto label;                                  \
        } while (0)

/* If it is a METADATA entry and fop num being GF_FOP_NULL, don't
 * log in the changelog as it is of no use. And also if it is
 * logged, since slicing version checking is done for metadata
 * entries, the subsequent entries with valid fop num which falls
 * to same changelog will be missed. Hence check for boundary
 * condition.
 */
#define CHANGELOG_OP_BOUNDARY_CHECK(frame, label) do {          \
                if (frame->root->op <= GF_FOP_NULL ||           \
                    frame->root->op >= GF_FOP_MAXVALUE)         \
                        goto label;                             \
        } while (0)

/**
 * ignore internal fops for all clients except AFR self-heal daemon
 */
#define CHANGELOG_IF_INTERNAL_FOP_THEN_GOTO(frame, dict, label) do {    \
                if ((frame->root->pid != GF_CLIENT_PID_SELF_HEALD)      \
                    && dict                                             \
                    && dict_get (dict, GLUSTERFS_INTERNAL_FOP_KEY))     \
                        goto label;                                     \
        } while (0)

#define CHANGELOG_COND_GOTO(priv, cond, label) do {                    \
                if (!priv->active || cond)                             \
                        goto label;                                    \
        } while (0)

/* Begin: Geo-Rep snapshot dependency changes */

#define DICT_ERROR         -1
#define BARRIER_OFF         0
#define BARRIER_ON          1
#define DICT_DEFAULT        2

#define CHANGELOG_NOT_ON_THEN_GOTO(priv, ret, label) do {                      \
                if (!priv->active) {                                           \
                        gf_msg (this->name, GF_LOG_WARNING, 0,                 \
                                CHANGELOG_MSG_NOT_ACTIVE,                      \
                                "Changelog is not active, return success");    \
                        ret = 0;                                               \
                        goto label;                                            \
                }                                                              \
        } while (0)

/* Log pthread error and goto label */
#define CHANGELOG_PTHREAD_ERROR_HANDLE_0(ret, label) do {                      \
                if (ret) {                                                     \
                        gf_msg (this->name, GF_LOG_ERROR,                      \
                                0, CHANGELOG_MSG_PTHREAD_ERROR,                \
                                "pthread error: Error: %d", ret);              \
                        ret = -1;                                              \
                        goto label;                                            \
                }                                                              \
        } while (0);

/* Log pthread error, set flag and goto label */
#define CHANGELOG_PTHREAD_ERROR_HANDLE_1(ret, label, flag) do {                \
                if (ret) {                                                     \
                        gf_msg (this->name, GF_LOG_ERROR, 0,                   \
                                CHANGELOG_MSG_PTHREAD_ERROR,                   \
                                "pthread error: Error: %d", ret);              \
                        ret = -1;                                              \
                        flag = _gf_true;                                       \
                        goto label;                                            \
                }                                                              \
        } while (0)
/* End: Geo-Rep snapshot dependency changes */

#endif /* _CHANGELOG_HELPERS_H */
