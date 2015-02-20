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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterd-svc-mgmt.h"

int
glusterd_nfssvc_init (glusterd_svc_t *svc);

int
glusterd_nfssvc_manager (glusterd_svc_t *svc, void *data, int flags);

int
glusterd_nfssvc_start (glusterd_svc_t *svc, int flags);

int
glusterd_nfssvc_stop (glusterd_svc_t *svc, int sig);

int
glusterd_nfssvc_reconfigure ();

#endif
