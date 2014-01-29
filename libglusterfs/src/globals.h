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

#define GD_OP_VERSION_KEY     "operating-version"
#define GD_MIN_OP_VERSION_KEY "minimum-operating-version"
#define GD_MAX_OP_VERSION_KEY "maximum-operating-version"

/* Gluster versions - OP-VERSION mapping
 *
 * 3.3.0                - 1
 * 3.4.0                - 2
 * 3.5.0                - 3
 * 3.next (3.6?)        - 4
 *
 * TODO: Change above comment once gluster version is finalised
 * TODO: Finalize the op-version ranges
 */
#define GD_OP_VERSION_MIN  1 /* MIN is the fresh start op-version, mostly
                                should not change */
#define GD_OP_VERSION_MAX  4 /* MAX VERSION is the maximum count in VME table,
                                should keep changing with introduction of newer
                                versions */

#include "xlator.h"

/* THIS */
#define THIS (*__glusterfs_this_location())

xlator_t **__glusterfs_this_location ();
xlator_t *glusterfs_this_get ();
int glusterfs_this_set (xlator_t *);

/* syncopctx */
void *syncopctx_getctx ();
int syncopctx_setctx (void *ctx);

/* task */
void *synctask_get ();
int synctask_set (void *);

/* uuid_buf */
char *glusterfs_uuid_buf_get();
/* lkowner_buf */
char *glusterfs_lkowner_buf_get();

/* init */
int glusterfs_globals_init (glusterfs_ctx_t *ctx);

extern const char *gf_fop_list[];

#endif /* !_GLOBALS_H */
