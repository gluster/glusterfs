/*
   Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef __SELINUX_H__
#define __SELINUX_H__

#include "common-utils.h"

#define SELINUX_XATTR "security.selinux"
#define SELINUX_GLUSTER_XATTR "trusted.glusterfs.selinux"

struct selinux_priv {
        gf_boolean_t    selinux_enabled;
};

typedef struct selinux_priv selinux_priv_t;

#endif
