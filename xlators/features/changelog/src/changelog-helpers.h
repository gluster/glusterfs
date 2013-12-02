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

#include "changelog-misc.h"

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
} changelog_rollover_t;

typedef struct changelog_fsync {
        /* fsync() thread */
        pthread_t fsync_th;

        xlator_t *this;
} changelog_fsync_t;

# define CHANGELOG_MAX_CLIENTS  5
typedef struct changelog_notify {
        /* reader end of the pipe */
        int rfd;

        /* notifier thread */
        pthread_t notify_th;

        /* unique socket path */
        char sockpath[PATH_MAX];

        int socket_fd;

        /**
         * simple array of accept()'ed fds. Not scalable at all
         * for large number of clients, but it's okay as we have
         * a ahrd limit in this version (@CHANGELOG_MAX_CLIENTS).
         */
        int client_fd[CHANGELOG_MAX_CLIENTS];

        xlator_t *this;
} changelog_notify_t;

struct changelog_priv {
        gf_boolean_t active;

        /* to generate unique socket file per brick */
        char *changelog_brick;

        /* logging directory */
        char *changelog_dir;

        /* one file for all changelog types */
        int changelog_fd;

        gf_lock_t lock;

        /* writen end of the pipe */
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

        /* context of the notifier thread */
        changelog_notify_t cn;

        /* operation mode */
        changelog_mode_t op_mode;

        /* bootstrap routine for 'current' logger */
        struct changelog_bootstrap *cb;

        /* encoder mode */
        changelog_encoder_t encode_mode;

        /* encoder */
        struct changelog_encoder *ce;
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

void
changelog_thread_cleanup (xlator_t *this, pthread_t thr_id);
inline void *
changelog_get_usable_buffer (changelog_local_t *local);
inline void
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
changelog_open (xlator_t *this, changelog_priv_t *priv);
int
changelog_fill_rollover_data (changelog_log_data_t *cld, gf_boolean_t is_last);
int
changelog_inject_single_event (xlator_t *this,
                               changelog_priv_t *priv,
                               changelog_log_data_t *cld);
inline size_t
changelog_entry_length ();
inline int
changelog_write (int fd, char *buffer, size_t len);
int
changelog_write_change (changelog_priv_t *priv, char *buffer, size_t len);
inline int
changelog_handle_change (xlator_t *this,
                         changelog_priv_t *priv, changelog_log_data_t *cld);
inline void
changelog_update (xlator_t *this, changelog_priv_t *priv,
                  changelog_local_t *local, changelog_log_type type);
void *
changelog_rollover (void *data);
void *
changelog_fsync_thread (void *data);
int
changelog_forget (xlator_t *this, inode_t *inode);

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
                changelog_local_cleanup (__xl, __local);                \
                if (__local && __local->prev_entry)                     \
                        changelog_local_cleanup (__xl,                  \
                                                 __local->prev_entry);  \
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
                uuid_copy (co->co_entry.cef_uuid, pargfid);             \
                co->co_entry.cef_bname = gf_strdup(bname);              \
                if (!co->co_entry.cef_bname)                            \
                        goto label;                                     \
                xlen += (UUID_CANONICAL_FORM_LEN + strlen (bname));     \
        } while (0)

#define CHANGELOG_INIT(this, local, inode, gfid, xrec)                  \
        local = changelog_local_init (this, inode, gfid, xrec, _gf_false)

#define CHANGELOG_INIT_NOCHECK(this, local, inode, gfid, xrec)          \
        local = changelog_local_init (this, inode, gfid, xrec, _gf_true)

#define CHANGELOG_NOT_ACTIVE_THEN_GOTO(frame, priv, label) do { \
                if (!priv->active)                              \
                        goto label;                             \
                /* ignore rebalance process's activity. */      \
                if (frame->root->pid == GF_CLIENT_PID_DEFRAG)   \
                        goto label;                             \
        } while (0)

/* ignore internal fops */
#define CHANGELOG_IF_INTERNAL_FOP_THEN_GOTO(dict, label) do {           \
                if (dict && dict_get (dict, GLUSTERFS_INTERNAL_FOP_KEY)) \
                        goto label;                                     \
        } while (0)

#define CHANGELOG_COND_GOTO(priv, cond, label) do {                    \
                if (!priv->active || cond)                             \
                        goto label;                                    \
        } while (0)

#endif /* _CHANGELOG_HELPERS_H */
