/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __BIT_ROT_SSM_H__
#define __BIT_ROT_SSM_H__

#include "xlator.h"

typedef enum br_scrub_state {
        BR_SCRUB_STATE_INACTIVE = 0,
        BR_SCRUB_STATE_PENDING,
        BR_SCRUB_STATE_ACTIVE,
        BR_SCRUB_STATE_PAUSED,
        BR_SCRUB_STATE_IPAUSED,
        BR_SCRUB_STATE_STALLED,
        BR_SCRUB_MAXSTATES,
} br_scrub_state_t;

typedef enum br_scrub_event {
        BR_SCRUB_EVENT_SCHEDULE = 0,
        BR_SCRUB_EVENT_PAUSE,
        BR_SCRUB_EVENT_ONDEMAND,
        BR_SCRUB_MAXEVENTS,
} br_scrub_event_t;

struct br_monitor;

int32_t br_scrub_state_machine (xlator_t *, gf_boolean_t);

#endif /* __BIT_ROT_SSM_H__ */
