/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "glusterd-conn-mgmt.h"
#include "glusterd-svc-mgmt.h"

#define _LGPL_SOURCE
#include <urcu/rculist.h>

glusterd_svc_t *
glusterd_conn_get_svc_object (glusterd_conn_t *conn)
{
        return cds_list_entry (conn, glusterd_svc_t, conn);
}
