/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GLOBALS_H
#define _GLOBALS_H

#define GF_DEFAULT_BASE_PORT 24007

#include "xlator.h"

/* THIS */
#define THIS (*__glusterfs_this_location())

#define GF_UUID_BUF_SIZE 50

xlator_t **__glusterfs_this_location ();
xlator_t *glusterfs_this_get ();
int glusterfs_this_set (xlator_t *);

/* task */
void *synctask_get ();
int synctask_set (void *);

/* uuid_buf */
char *glusterfs_uuid_buf_get();
char *glusterfs_lkowner_buf_get();

/* init */
int glusterfs_globals_init (void);

#endif /* !_GLOBALS_H */
