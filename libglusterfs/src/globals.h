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

xlator_t **__glusterfs_this_location ();
xlator_t *glusterfs_this_get ();
int glusterfs_this_set (xlator_t *);

/* task */
void *synctask_get ();
int synctask_set (glusterfs_ctx_t *, void *);

/* uuid_buf */
char *glusterfs_uuid_buf_get(glusterfs_ctx_t *);
/* lkowner_buf */
char *glusterfs_lkowner_buf_get(glusterfs_ctx_t *);

/* init */
int glusterfs_globals_init (glusterfs_ctx_t *ctx);

extern const char *gf_fop_list[];

#endif /* !_GLOBALS_H */
