/*
   Copyright (c) 2006-2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _GLUSTERD_H_
#define _GLUSTERD_H_

#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <libgen.h>

#include <glusterfs/compat-uuid.h>

#include "rpc-clnt.h"
#include <glusterfs/glusterfs.h>
#include <glusterfs/xlator.h>
#include <glusterfs/logging.h>
#include <glusterfs/call-stub.h>
#include "glusterd-mem-types.h"
#include "rpcsvc.h"
#include "glusterd1-xdr.h"
#include "protocol-common.h"
#include "cli1-xdr.h"
#include <glusterfs/syncop.h>
#include <glusterfs/store.h>
#include <glusterfs/events.h>

#define GLUSTERD_DEFAULT_SPECDIR "/var/lib/glusterd/vols"

typedef struct {
    struct _volfile_ctx *volfile;
    pthread_mutex_t mutex;
    uuid_t uuid;
    rpcsvc_t *rpc;
    pthread_mutex_t xprt_lock;
    struct list_head xprt_list;
    gf_timer_t *timer;

    xlator_t *xl; /* Should be set to 'THIS' before creating thread */
    dict_t *opts;
    char workdir[PATH_MAX];
} glusterd_conf_t;

#define GLUSTERD_DEFAULT_PORT 24007

int
glusterd_fetchspec_notify(xlator_t *this);

#endif
