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

#include <glusterfs/compat-uuid.h>

#include <urcu/list.h>
#include <glusterfs/xlator.h>

struct pmap_ports {
    struct cds_list_head port_list;
    char *brickname;
    void *xprt;
    int port;
};

struct pmap_registry {
    struct cds_list_head ports;
    int base_port;
    int max_port;
};

int
pmap_port_alloc(xlator_t *this);
struct pmap_registry *
pmap_registry_get(xlator_t *this);
int
pmap_add_port_to_list(xlator_t *this, int port, char *brickname, void *xprt);
int
pmap_port_new(xlator_t *this, int port, char *brickname, void *xprt,
              struct pmap_ports **new_port);
int
pmap_port_remove(xlator_t *this, int port, char *brickname, void *xprt,
                 gf_boolean_t brick_disconnect);
int
pmap_registry_search(xlator_t *this, char *brickname, gf_boolean_t destroy);
int
port_brick_bind(xlator_t *this, int port, char *brickname, void *xprt,
                gf_boolean_t attach_req);
int
pmap_registry_search_by_xprt(xlator_t *this, void *xprt);
int
pmap_assign_port(xlator_t *this, int old_port, char *path);

#endif
