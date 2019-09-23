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

struct pmap_port_status {
    char *brickname;
    void *xprt;
    gf_pmap_port_type_t type;
};

struct pmap_registry {
    struct pmap_port_status ports[GF_PORT_MAX + 1];
    int base_port;
    int max_port;
    int last_alloc;
};

int
pmap_assign_port(xlator_t *this, int port, const char *path);
int
pmap_mark_port_leased(xlator_t *this, int port);
int
pmap_registry_alloc(xlator_t *this);
int
pmap_registry_bind(xlator_t *this, int port, const char *brickname,
                   gf_pmap_port_type_t type, void *xprt);
int
pmap_registry_extend(xlator_t *this, int port, const char *brickname);
int
pmap_registry_remove(xlator_t *this, int port, const char *brickname,
                     gf_pmap_port_type_t type, void *xprt,
                     gf_boolean_t brick_disconnect);
int
pmap_registry_search(xlator_t *this, const char *brickname,
                     gf_pmap_port_type_t type, gf_boolean_t destroy);
struct pmap_registry *
pmap_registry_get(xlator_t *this);

#endif
