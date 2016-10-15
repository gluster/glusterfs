/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _TIER_H_
#define _TIER_H_


/******************************************************************************/
/* This is from dht-rebalancer.c as we dont have dht-rebalancer.h */
#include "dht-common.h"
#include "xlator.h"
#include <signal.h>
#include <fnmatch.h>
#include <signal.h>

/*
 * Size of timer wheel. We would not promote or demote less
 * frequently than this number.
 */
#define TIMER_SECS 3600

#include "gfdb_data_store.h"
#include <ctype.h>
#include <sys/stat.h>

#define PROMOTION_QFILE "promotequeryfile"
#define DEMOTION_QFILE "demotequeryfile"

#define TIER_HASHED_SUBVOL   conf->subvolumes[0]
#define TIER_UNHASHED_SUBVOL   conf->subvolumes[1]

#define GET_QFILE_PATH(is_promotion)\
        (is_promotion) ? promotion_qfile : demotion_qfile

typedef struct tier_qfile_array {
        int             *fd_array;
        ssize_t         array_size;
        ssize_t         next_index;
        /* Indicate the number of exhuasted FDs*/
        ssize_t         exhausted_count;
} tier_qfile_array_t;


typedef struct _query_cbk_args {
        xlator_t                *this;
        gf_defrag_info_t        *defrag;
        /* This is write */
        int                     query_fd;
        int                     is_promotion;
        int                     is_compaction;
        /* This is for read */
        tier_qfile_array_t       *qfile_array;
} query_cbk_args_t;

int
gf_run_tier(xlator_t *this, gf_defrag_info_t *defrag);

typedef struct gfdb_brick_info {
        gfdb_time_t             *time_stamp;
        gf_boolean_t            _gfdb_promote;
        query_cbk_args_t        *_query_cbk_args;
} gfdb_brick_info_t;

typedef struct brick_list {
        xlator_t          *xlator;
        char              *brick_db_path;
        char              brick_name[NAME_MAX];
        char              qfile_path[PATH_MAX];
        struct list_head  list;
} tier_brick_list_t;

typedef struct _dm_thread_args {
        xlator_t                *this;
        gf_defrag_info_t        *defrag;
        struct list_head        *brick_list;
        int                     freq_time;
        int                     return_value;
        int                     is_promotion;
        int                     is_compaction;
        gf_boolean_t            is_hot_tier;
} migration_args_t;

typedef enum tier_watermark_op_ {
        TIER_WM_NONE = 0,
        TIER_WM_LOW,
        TIER_WM_HI,
        TIER_WM_MID
} tier_watermark_op_t;

#define DEFAULT_PROMOTE_FREQ_SEC       120
#define DEFAULT_DEMOTE_FREQ_SEC        120
#define DEFAULT_HOT_COMPACT_FREQ_SEC   604800
#define DEFAULT_COLD_COMPACT_FREQ_SEC  604800
#define DEFAULT_DEMOTE_DEGRADED        1
#define DEFAULT_WRITE_FREQ_SEC         0
#define DEFAULT_READ_FREQ_SEC          0
#define DEFAULT_WM_LOW                 75
#define DEFAULT_WM_HI                  90
#define DEFAULT_TIER_MODE              TIER_MODE_TEST
#define DEFAULT_COMP_MODE              _gf_true
#define DEFAULT_TIER_MAX_MIGRATE_MB    1000
#define DEFAULT_TIER_MAX_MIGRATE_FILES 5000
#define DEFAULT_TIER_QUERY_LIMIT       100

#endif
