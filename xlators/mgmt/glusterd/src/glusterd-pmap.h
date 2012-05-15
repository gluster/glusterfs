/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _GLUSTERD_PMAP_H_
#define _GLUSTERD_PMAP_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <pthread.h>
#include "uuid.h"

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "call-stub.h"
#include "fd.h"
#include "byte-order.h"
#include "glusterd.h"
#include "rpcsvc.h"


#define GF_IANA_PRIV_PORTS_START 49152 /* RFC 6335 */

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

int pmap_registry_alloc (xlator_t *this);
int pmap_registry_bind (xlator_t *this, int port, const char *brickname,
                        gf_pmap_port_type_t type, void *xprt);
int pmap_registry_remove (xlator_t *this, int port, const char *brickname,
                          gf_pmap_port_type_t type, void *xprt);
int pmap_registry_search (xlator_t *this, const char *brickname,
                          gf_pmap_port_type_t type);
struct pmap_registry *pmap_registry_get (xlator_t *this);

#endif
