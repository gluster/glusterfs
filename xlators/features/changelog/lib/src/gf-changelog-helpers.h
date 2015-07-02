/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _GF_CHANGELOG_HELPERS_H
#define _GF_CHANGELOG_HELPERS_H

#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include "locking.h"

#include <xlator.h>

#include "changelog.h"

#include "changelog-rpc-common.h"
#include "gf-changelog-journal.h"

#define GF_CHANGELOG_TRACKER  "tracker"

#define GF_CHANGELOG_CURRENT_DIR    ".current"
#define GF_CHANGELOG_PROCESSED_DIR  ".processed"
#define GF_CHANGELOG_PROCESSING_DIR ".processing"
#define GF_CHANGELOG_HISTORY_DIR    ".history"
#define TIMESTAMP_LENGTH 10

#ifndef MAXLINE
#define MAXLINE 4096
#endif

#define GF_CHANGELOG_FILL_BUFFER(ptr, ascii, off, len) do {     \
                memcpy (ascii + off, ptr, len);                 \
                off += len;                                     \
        } while (0)

typedef struct read_line {
        int rl_cnt;
        char *rl_bufptr;
        char rl_buf[MAXLINE];
} read_line_t;

struct gf_changelog;
struct gf_event;

/**
 * Event list for ordered event notification
 *
 * ->next_seq holds the next _expected_ sequence number.
 */
struct gf_event_list {
        pthread_mutex_t lock;               /* protects this structure */
        pthread_cond_t  cond;

        pthread_t invoker;

        unsigned long next_seq;             /* next sequence number expected:
                                               zero during bootstrap */

        struct gf_changelog *entry;         /* backpointer to it's brick
                                               encapsulator (entry) */
        struct list_head events;            /* list of events */
};

/**
 * include a refcount if it's of use by additional layers
 */
struct gf_event {
        int count;

        unsigned long seq;

        struct list_head list;

        struct iovec iov[0];
};
#define GF_EVENT_CALLOC_SIZE(cnt, len)                                  \
        (sizeof (struct gf_event) + (cnt * sizeof (struct iovec)) + len)

/**
 * assign the base address of the IO vector to the correct memory
o * area and set it's addressable length.
 */
#define GF_EVENT_ASSIGN_IOVEC(vec, event, len, pos)                     \
        do {                                                            \
                vec->iov_base = ((char *)event) +                       \
                        sizeof (struct gf_event) +                      \
                        (event->count * sizeof (struct iovec)) + pos;   \
                vec->iov_len = len;                                     \
                pos += len;                                             \
        } while (0)

typedef enum gf_changelog_conn_state {
        GF_CHANGELOG_CONN_STATE_PENDING = 0,
        GF_CHANGELOG_CONN_STATE_ACCEPTED,
        GF_CHANGELOG_CONN_STATE_DISCONNECTED,
} gf_changelog_conn_state_t;

/**
 * An instance of this structure is allocated for each brick for which
 * notifications are streamed.
 */
typedef struct gf_changelog {
        gf_lock_t statelock;
        gf_changelog_conn_state_t connstate;

        xlator_t *this;

        struct list_head list;              /* list of instances */

        char brick[PATH_MAX];               /* brick path for this end-point */

        changelog_rpc_t grpc;               /* rpc{-clnt,svc} for this brick */
#define RPC_PROBER(ent)  ent->grpc.rpc
#define RPC_REBORP(ent)  ent->grpc.svc
#define RPC_SOCK(ent)    ent->grpc.sock

        unsigned int notify;                /* notification flag(s) */

        FINI       *fini;                   /* destructor callback */
        CALLBACK   *callback;               /* event callback dispatcher */
        CONNECT    *connected;              /* connect callback */
        DISCONNECT *disconnected;           /* disconnection callback */

        void *ptr;                          /* owner specific private data */
        xlator_t *invokerxl;                /* consumers _this_, if valid,
                                               assigned to THIS before cbk is
                                               invoked */

        gf_boolean_t ordered;

        void (*queueevent) (struct gf_event_list *, struct gf_event *);
        void (*pickevent) (struct gf_event_list *, struct gf_event **);

        struct gf_event_list event;
} gf_changelog_t;

static inline int
gf_changelog_filter_check (gf_changelog_t *entry, changelog_event_t *event)
{
        if (event->ev_type & entry->notify)
                return 1;
        return 0;
}

#define GF_NEED_ORDERED_EVENTS(ent)  (ent->ordered == _gf_true)

/** private structure */
typedef struct gf_private {
        pthread_mutex_t lock;            /* protects ->connections, cleanups */
        pthread_cond_t  cond;

        void *api;                       /* pointer for API access */

        pthread_t poller;                /* event poller thread */
        pthread_t connectionjanitor;     /* connection cleaner */

        struct list_head connections;    /* list of connections */
        struct list_head cleanups;       /* list of connection to be
                                            cleaned up */
} gf_private_t;

#define GF_CHANGELOG_GET_API_PTR(this) (((gf_private_t *) this->private)->api)

/**
 * upcall: invoke callback with _correct_ THIS
 */
#define GF_CHANGELOG_INVOKE_CBK(this, cbk, brick, args ...)             \
        do {                                                            \
                xlator_t *old_this = NULL;                              \
                xlator_t *invokerxl = NULL;                             \
                                                                        \
                invokerxl = entry->invokerxl;                           \
                old_this = this;                                        \
                                                                        \
                if (invokerxl) {                                        \
                        THIS = invokerxl;                               \
                }                                                       \
                                                                        \
                cbk (invokerxl, brick, args);                           \
                THIS = old_this;                                        \
                                                                        \
        } while (0)

#define SAVE_THIS(xl)                           \
        do {                                    \
                old_this = xl;                  \
                THIS = master;                  \
        } while (0)

#define RESTORE_THIS()                          \
        do {                                    \
                if (old_this)                   \
                        THIS = old_this;        \
        } while (0)

/** APIs and the rest */

void *
gf_changelog_process (void *data);

ssize_t
gf_changelog_read_path (int fd, char *buffer, size_t bufsize);

void
gf_rfc3986_encode (unsigned char *s, char *enc, char *estr);

size_t
gf_changelog_write (int fd, char *buffer, size_t len);

ssize_t
gf_readline (int fd, void *vptr, size_t maxlen);

int
gf_ftruncate (int fd, off_t length);

off_t
gf_lseek (int fd, off_t offset, int whence);

int
gf_changelog_consume (xlator_t *this,
                      gf_changelog_journal_t *jnl,
                      char *from_path, gf_boolean_t no_publish);
int
gf_changelog_publish (xlator_t *this,
                      gf_changelog_journal_t *jnl, char *from_path);
int
gf_thread_cleanup (xlator_t *this, pthread_t thread);
void *
gf_changelog_callback_invoker (void *arg);

int
gf_cleanup_event (xlator_t *, struct gf_event_list *);

/* (un)ordered event queueing */
void
queue_ordered_event (struct gf_event_list *, struct gf_event *);

void
queue_unordered_event (struct gf_event_list *, struct gf_event *);

/* (un)ordered event picking */
void
pick_event_ordered (struct gf_event_list *, struct gf_event **);

void
pick_event_unordered (struct gf_event_list *, struct gf_event **);

/* connection janitor thread */
void *
gf_changelog_connection_janitor (void *);

#endif
