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
#define GF_DEFAULT_VOLFILE_TRANSPORT "tcp"

#define GF_GLOBAL_XLATOR_NAME "global"
#define GD_OP_VERSION_KEY "operating-version"
#define GD_MIN_OP_VERSION_KEY "minimum-operating-version"
#define GD_MAX_OP_VERSION_KEY "maximum-operating-version"

#define GF_PROTECT_FROM_EXTERNAL_WRITES "trusted.glusterfs.protect.writes"
#define GF_AVOID_OVERWRITE "glusterfs.avoid.overwrite"
#define GF_CLEAN_WRITE_PROTECTION "glusterfs.clean.writexattr"

/* Gluster versions - OP-VERSION mapping
 *
 * 3.3.x                - 1
 * 3.4.x                - 2
 * 3.5.0                - 3
 * 3.5.1                - 30501
 * 3.6.0                - 30600
 * 3.7.0                - 30700
 * 3.7.1                - 30701
 * 3.7.2                - 30702
 *
 * Starting with Gluster v3.6, the op-version will be multi-digit integer values
 * based on the Glusterfs version, instead of a simply incrementing integer
 * value. The op-version for a given X.Y.Z release will be an integer XYZ, with
 * Y and Z 2 digit always 2 digits wide and padded with 0 when needed. This
 * should allow for some gaps between two Y releases for backports of features
 * in Z releases.
 */

#define GD_OP_VERSION3(_a, _b, _c) \
    GD_OP_VERSION_##_a##_##_b##_##_c = (((_a) * 100 + (_b)) * 100 + (_c))

#define GD_OP_VERSION2(_a, _b) \
    GD_OP_VERSION_##_a##_##_b = (((_a) * 100 + (_b)) * 100)

#define GD_OP_VERSION_END \
        GD_OP_VERSION_NEXT, GD_OP_VERSION_MAX = GD_OP_VERSION_NEXT - 1

enum {
    GD_OP_VERSION_MIN = 1,
    GD_OP_VERSION3( 3,  6,  0),
    GD_OP_VERSION3( 3,  7,  0),
    GD_OP_VERSION3( 3,  7,  1),
    GD_OP_VERSION3( 3,  7,  2),
    GD_OP_VERSION3( 3,  7,  3),
    GD_OP_VERSION3( 3,  7,  4),
    GD_OP_VERSION3( 3,  7,  5),
    GD_OP_VERSION3( 3,  7,  6),
    GD_OP_VERSION3( 3,  7,  7),
    GD_OP_VERSION3( 3,  7, 10),
    GD_OP_VERSION3( 3,  7, 12),
    GD_OP_VERSION3( 3,  8,  0),
    GD_OP_VERSION3( 3,  8,  3),
    GD_OP_VERSION3( 3,  8,  4),
    GD_OP_VERSION3( 3,  9,  0),
    GD_OP_VERSION3( 3,  9,  1),
    GD_OP_VERSION3( 3, 10,  0),
    GD_OP_VERSION3( 3, 10,  1),
    GD_OP_VERSION3( 3, 10,  2),
    GD_OP_VERSION3( 3, 11,  0),
    GD_OP_VERSION3( 3, 11,  1),
    GD_OP_VERSION3( 3, 12,  0),
    GD_OP_VERSION3( 3, 12,  2),
    GD_OP_VERSION3( 3, 12,  3),
    GD_OP_VERSION3( 3, 13,  0),
    GD_OP_VERSION3( 3, 13,  1),
    GD_OP_VERSION3( 3, 13,  2),
    GD_OP_VERSION3( 4,  0,  0),
    GD_OP_VERSION3( 4,  1,  0),
    GD_OP_VERSION2( 5,  0),
    GD_OP_VERSION2( 5,  4),
    GD_OP_VERSION2( 6,  0),
    GD_OP_VERSION2( 7,  0),
    GD_OP_VERSION2( 7,  1),
    GD_OP_VERSION2( 7,  2),
    GD_OP_VERSION2( 7,  3),
    GD_OP_VERSION2( 8,  0),
    GD_OP_VERSION2( 9,  0),
    GD_OP_VERSION2(10,  0),
    GD_OP_VERSION2(11,  0),

/* NOTE: Add new versions above this line. */
    GD_OP_VERSION_END
};

#include "glusterfs/xlator.h"
#include "glusterfs/options.h"

/* THIS */
#define THIS (*__glusterfs_this_location())
#define DECLARE_OLD_THIS xlator_t *old_THIS = THIS

xlator_t **
__glusterfs_this_location(void);

extern xlator_t global_xlator;
extern struct volume_options global_xl_options[];

/* syncopctx */
void *
syncopctx_getctx(void);

/* task */
void *
synctask_get(void);
void
synctask_set(void *);

/* uuid_buf */
char *
glusterfs_uuid_buf_get(void);
/* lkowner_buf */
char *
glusterfs_lkowner_buf_get(void);
/* leaseid buf */
char *
glusterfs_leaseid_buf_get(void);
char *
glusterfs_leaseid_exist(void);

/* init */
int
glusterfs_globals_init(glusterfs_ctx_t *ctx);

void
gf_thread_needs_cleanup(void);

struct tvec_base *
glusterfs_ctx_tw_get(glusterfs_ctx_t *ctx);
void
glusterfs_ctx_tw_put(glusterfs_ctx_t *ctx);

extern const char *gf_fop_list[];
extern const char *gf_upcall_list[];

/* mem acct enable/disable */
int
gf_global_mem_acct_enable_get(void);
int
gf_global_mem_acct_enable_set(int val);

extern glusterfs_ctx_t *global_ctx;
#endif /* !_GLOBALS_H */
