/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __BIT_ROT_SCRUB_H__
#define __BIT_ROT_SCRUB_H__

#include "xlator.h"
#include "bit-rot.h"

void *br_fsscanner (void *);

int32_t br_fsscan_schedule (xlator_t *);
int32_t br_fsscan_reschedule (xlator_t *);
int32_t br_fsscan_activate (xlator_t *);
int32_t br_fsscan_deactivate (xlator_t *);
int32_t br_fsscan_ondemand (xlator_t *);

int32_t br_scrubber_handle_options (xlator_t *, br_private_t *, dict_t *);

int32_t
br_scrubber_monitor_init (xlator_t *, br_private_t *);

int32_t br_scrubber_init (xlator_t *, br_private_t *);

int32_t br_collect_bad_objects_from_children (xlator_t *this, dict_t *dict);

void
br_child_set_scrub_state (br_child_t *, gf_boolean_t);

#endif /* __BIT_ROT_SCRUB_H__ */
