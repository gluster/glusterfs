/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef QUOTAD_HELPERS_H
#define QUOTAD_HELPERS_H

#include "rpcsvc.h"
#include "quota.h"
#include "quotad-aggregator.h"

void
quotad_aggregator_free_state (quotad_aggregator_state_t *state);

call_frame_t *
quotad_aggregator_get_frame_from_req (rpcsvc_request_t *req);

#endif
