/*
   Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _POSIX_GFID_PATH_H
#define _POSIX_GFID_PATH_H

#include "xlator.h"
#include "common-utils.h"

#define MAX_GFID2PATH_LINK_SUP 500

int32_t
posix_set_gfid2path_xattr (xlator_t *, const char *, uuid_t,
                           const char *);
int32_t
posix_remove_gfid2path_xattr (xlator_t *, const char *, uuid_t,
                              const char *);
gf_boolean_t
posix_is_gfid2path_xattr (const char *name);
#endif /* _POSIX_GFID_PATH_H */
