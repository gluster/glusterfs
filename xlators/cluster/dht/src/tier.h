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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif


/******************************************************************************/
/* This is from dht-rebalancer.c as we dont have dht-rebalancer.h */
#include "dht-common.h"
#include "xlator.h"
#include <signal.h>
#include <fnmatch.h>
#include <signal.h>

#define DEFAULT_PROMOTE_FREQ_SEC 120
#define DEFAULT_DEMOTE_FREQ_SEC  120

/*
 * Size of timer wheel. We would not promote or demote lesd
 * frequently than this number.
 */
#define TIMER_SECS 3600

#include "gfdb_data_store.h"
#include <ctype.h>
#include <sys/stat.h>

#define DEMOTION_QFILE "/var/run/gluster/demotequeryfile"
#define PROMOTION_QFILE "/var/run/gluster/promotequeryfile"

#define GET_QFILE_PATH(is_promotion)\
        (is_promotion) ? PROMOTION_QFILE : DEMOTION_QFILE

typedef struct _query_cbk_args {
        xlator_t *this;
        gf_defrag_info_t *defrag;
        FILE *queryFILE;
} query_cbk_args_t;

int
gf_run_tier(xlator_t *this, gf_defrag_info_t *defrag);

typedef struct _gfdb_brick_dict_info {
        gfdb_time_t           *time_stamp;
        gf_boolean_t            _gfdb_promote;
        query_cbk_args_t       *_query_cbk_args;
} _gfdb_brick_dict_info_t;

typedef struct _dm_thread_args {
        xlator_t                *this;
        gf_defrag_info_t        *defrag;
        dict_t                  *brick_list;
        int                     freq_time;
        int                     return_value;
} promotion_args_t, demotion_args_t;

#endif
