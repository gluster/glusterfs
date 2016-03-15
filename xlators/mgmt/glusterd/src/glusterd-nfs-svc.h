/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _GLUSTERD_NFS_SVC_H_
#define _GLUSTERD_NFS_SVC_H_

#include "glusterd-svc-mgmt.h"

void
glusterd_nfssvc_build (glusterd_svc_t *svc);

int
glusterd_nfssvc_init (glusterd_svc_t *svc);

int
glusterd_nfssvc_reconfigure ();

#endif
