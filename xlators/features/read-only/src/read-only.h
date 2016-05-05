/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __READONLY_H__
#define __READONLY_H__

#include "read-only-mem-types.h"
#include "xlator.h"


typedef struct {
        uint8_t worm : 1;
        uint8_t retain : 1;
        uint8_t legal_hold :1;
        uint8_t ret_mode : 1;
        uint64_t ret_period;
        uint64_t auto_commit_period;
} worm_reten_state_t;


typedef struct {
        gf_boolean_t          readonly_or_worm_enabled;
        gf_boolean_t          worm_file;
        uint64_t              reten_period;
        uint64_t              com_period;
        char                  *reten_mode;
        time_t                start_time;
} read_only_priv_t;

#endif
