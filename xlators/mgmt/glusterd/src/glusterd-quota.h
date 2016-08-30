/*
   Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _GLUSTERD_QUOTA_
#define _GLUSTERD_QUOTA_

int
glusterd_store_quota_config (glusterd_volinfo_t *volinfo, char *path,
                             char *gfid_str, int opcode, char **op_errstr);

#endif
