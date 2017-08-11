/*
   Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _POSIX2_HELPERS_H
#define _POSIX2_HELPERS_H

#include "xlator.h"

int
posix2_handle_length (int32_t);

int
posix2_make_handle (uuid_t, char *, char *, size_t);

int
posix2_istat_path (xlator_t *, uuid_t , const char *,
                   struct iatt *, gf_boolean_t);

int32_t
posix2_save_openfd (xlator_t *, fd_t *, int, int32_t);

int32_t
posix2_create_inode (xlator_t *, char *, int32_t, mode_t);

int32_t
posix2_link_inode (xlator_t *, char *, const char *, uuid_t);

int32_t
posix2_handle_entry (xlator_t *, char *, const char *, struct iatt *);

int32_t
posix2_lookup_is_nameless (loc_t *);

void
posix2_fill_ino_from_gfid (xlator_t *, struct iatt *);

#endif /* !_POSIX2_HELPERS_H */
