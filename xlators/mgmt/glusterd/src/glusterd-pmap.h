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
#include "compat-uuid.h"

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "call-stub.h"
#include "fd.h"
#include "byte-order.h"
#include "glusterd.h"
#include "rpcsvc.h"


struct pmap_port_status {
        gf_pmap_port_type_t type;
        char  *brickname;
        void  *xprt;
};

struct pmap_registry {
        int     base_port;
        int     last_alloc;
        struct  pmap_port_status ports[65536];
};

int pmap_assign_port (xlator_t *this, int port, const char *path);
int pmap_mark_port_leased (xlator_t *this, int port);
int pmap_registry_alloc (xlator_t *this);
int pmap_registry_bind (xlator_t *this, int port, const char *brickname,
                        gf_pmap_port_type_t type, void *xprt);
int pmap_registry_extend (xlator_t *this, int port, const char *brickname);
int pmap_registry_remove (xlator_t *this, int port, const char *brickname,
                          gf_pmap_port_type_t type, void *xprt);
int pmap_registry_search (xlator_t *this, const char *brickname,
                          gf_pmap_port_type_t type, gf_boolean_t destroy);
struct pmap_registry *pmap_registry_get (xlator_t *this);

#endif
