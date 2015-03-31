/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __GF_CHANGELOG_JOURNAL_H
#define __GF_CHANGELOG_JOURNAL_H

#include <unistd.h>
#include <pthread.h>

#include "changelog.h"

enum api_conn {
        JNL_API_CONNECTED,
        JNL_API_CONN_INPROGESS,
        JNL_API_DISCONNECTED,
};

typedef struct gf_changelog_entry {
        char path[PATH_MAX];

        struct list_head list;
} gf_changelog_entry_t;

typedef struct gf_changelog_processor {
        pthread_mutex_t  lock;     /* protects ->entries */
        pthread_cond_t   cond;     /* waiter during empty list */
        gf_boolean_t     waiting;

        pthread_t processor;       /* thread-id of journal processing thread */

        struct list_head entries;
} gf_changelog_processor_t;

typedef struct gf_changelog_journal {
        DIR *jnl_dir;                       /* 'processing' directory stream */

        int jnl_fd;                         /* fd to the tracker file */

        char jnl_brickpath[PATH_MAX];       /* brick path for this end-point */

        gf_changelog_processor_t *jnl_proc;

        char *jnl_working_dir;              /* scratch directory */

        char jnl_current_dir[PATH_MAX];
        char jnl_processed_dir[PATH_MAX];
        char jnl_processing_dir[PATH_MAX];

        char rfc3986[256];                  /* RFC 3986 string encoding */

        struct gf_changelog_journal *hist_jnl;
        int hist_done;                      /* holds 0 done scanning,
                                               1 keep scanning and -1 error */

        pthread_spinlock_t lock;
        int connected;
        xlator_t *this;
} gf_changelog_journal_t;

#define JNL_SET_API_STATE(jnl, state)  (jnl->connected = state)
#define JNL_IS_API_DISCONNECTED(jnl)  (jnl->connected == JNL_API_DISCONNECTED)

/* History API */
typedef struct gf_changelog_history_data {
        int           len;

        int           htime_fd;

        /* parallelism count */
        int           n_parallel;

        /* history from, to indexes */
        unsigned long from;
        unsigned long to;
        xlator_t      *this;
} gf_changelog_history_data_t;

typedef struct gf_changelog_consume_data {
        /** set of inputs */

        /* fd to read from */
        int             fd;

        /* from @offset */
        off_t           offset;

        xlator_t       *this;

        gf_changelog_journal_t *jnl;

        /** set of outputs */

        /* return value */
        int retval;

        /* journal processed */
        char changelog[PATH_MAX];
} gf_changelog_consume_data_t;

/* event handler */
CALLBACK gf_changelog_handle_journal;

/* init, connect & disconnect handler */
INIT gf_changelog_journal_init;
FINI gf_changelog_journal_fini;
CONNECT gf_changelog_journal_connect;
DISCONNECT gf_changelog_journal_disconnect;

#endif
