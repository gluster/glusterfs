/*
   Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _POSIX2_ENTRY_OPS_H
#define _POSIX2_ENTRY_OPS_H

#include "xlator.h"

int32_t
posix2_lookup (call_frame_t *, xlator_t *, loc_t *, dict_t *);

int32_t
posix2_create (call_frame_t *, xlator_t *, loc_t *, int32_t,
               mode_t, mode_t, fd_t *, dict_t *);

#endif /* _POSIX2_ENTRY_OPS_H */
