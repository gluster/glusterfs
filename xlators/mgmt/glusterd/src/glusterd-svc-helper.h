/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERD_SVC_HELPER_H_
#define _GLUSTERD_SVC_HELPER_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterd.h"
#include "glusterd-svc-mgmt.h"

int
glusterd_svcs_reconfigure (glusterd_volinfo_t *volinfo);

int
glusterd_svcs_stop ();

int
glusterd_svcs_manager (glusterd_volinfo_t *volinfo);
#endif
