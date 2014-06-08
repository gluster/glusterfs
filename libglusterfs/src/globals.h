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
 * 3.5.1                - 30501
 * 3.next (3.6?)        - 30600
 *
 * Starting with Gluster v3.6 and v3.5.1, the op-version will be multi-digit
 * integer values based on the Glusterfs version, instead of a simply
 * incrementing integer value. The op-version for a given X.Y.Z release will be
 * an integer XYZ, with Y and Z 2 digit always 2 digits wide and padded with 0
 * when needed. This should allow for some gaps between two Y releases for
 * backports of features in Z releases.
 */
#define GD_OP_VERSION_MIN  1 /* MIN is the fresh start op-version, mostly
                                should not change */
#define GD_OP_VERSION_MAX  30501 /* MAX VERSION is the maximum count in VME
                                    table, should keep changing with
                                    introduction of newer versions */

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
