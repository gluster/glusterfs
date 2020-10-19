/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _GLUSTERD_PMAP_H_
#define _GLUSTERD_PMAP_H_

#include <pthread.h>
#include <glusterfs/compat-uuid.h>

#include <glusterfs/glusterfs.h>
#include <glusterfs/xlator.h>
#include <glusterfs/logging.h>
#include <glusterfs/call-stub.h>
#include <glusterfs/byte-order.h>
#include "rpcsvc.h"

struct pmap_registry {
    int base_port;
    int max_port;
};

int
pmap_port_alloc(xlator_t *this);
struct pmap_registry *
pmap_registry_get(xlator_t *this);

#endif
