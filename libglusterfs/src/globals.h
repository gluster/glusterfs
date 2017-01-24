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

#define GD_OP_VERSION_KEY     "operating-version"
#define GD_MIN_OP_VERSION_KEY "minimum-operating-version"
#define GD_MAX_OP_VERSION_KEY "maximum-operating-version"

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
#define GD_OP_VERSION_MIN  1 /* MIN is the fresh start op-version, mostly
                                should not change */
#define GD_OP_VERSION_MAX  GD_OP_VERSION_3_10_0 /* MAX VERSION is the maximum
                                                  count in VME table, should
                                                  keep changing with
                                                  introduction of newer
                                                  versions */

#define GD_OP_VERSION_3_6_0    30600 /* Op-Version for GlusterFS 3.6.0 */

#define GD_OP_VERSION_3_7_0    30700 /* Op-version for GlusterFS 3.7.0 */

#define GD_OP_VERSION_3_7_1    30701 /* Op-version for GlusterFS 3.7.1 */

#define GD_OP_VERSION_3_7_2    30702 /* Op-version for GlusterFS 3.7.2 */

#define GD_OP_VERSION_3_7_3    30703 /* Op-version for GlusterFS 3.7.3 */

#define GD_OP_VERSION_3_7_4    30704 /* Op-version for GlusterFS 3.7.4 */

#define GD_OP_VERSION_3_7_5    30705 /* Op-version for GlusterFS 3.7.5 */

#define GD_OP_VERSION_3_7_6    30706 /* Op-version for GlusterFS 3.7.6 */

#define GD_OP_VERSION_3_7_7    30707 /* Op-version for GlusterFS 3.7.7 */

#define GD_OP_VERSION_3_7_10   30710 /* Op-version for GlusterFS 3.7.10 */

#define GD_OP_VERSION_3_7_12   30712 /* Op-version for GlusterFS 3.7.12 */

#define GD_OP_VERSION_3_8_0    30800 /* Op-version for GlusterFS 3.8.0 */

#define GD_OP_VERSION_3_8_3    30803 /* Op-version for GlusterFS 3.8.3 */

#define GD_OP_VERSION_3_8_4    30804 /* Op-version for GlusterFS 3.8.4 */

#define GD_OP_VERSION_3_9_0    30900 /* Op-version for GlusterFS 3.9.0 */

#define GD_OP_VERSION_3_9_1    30901 /* Op-version for GlusterFS 3.9.1 */

#define GD_OP_VERSION_3_10_0   31000 /* Op-version for GlusterFS 3.10.0 */

#define GD_OP_VER_PERSISTENT_AFR_XATTRS GD_OP_VERSION_3_6_0

#include "xlator.h"

/* THIS */
#define THIS (*__glusterfs_this_location())
#define DECLARE_OLD_THIS        xlator_t *old_THIS = THIS

/*
 * a more comprehensive feature test is shown at
 * http://lists.iptel.org/pipermail/semsdev/2010-October/005075.html
 * this is sufficient for RHEL5 i386 builds
 */
#if (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 1)) && !defined(__i386__)
# define INCREMENT_ATOMIC(lk, op) __sync_add_and_fetch(&op, 1)
# define DECREMENT_ATOMIC(lk, op) __sync_sub_and_fetch(&op, 1)
#else
/* These are only here for old gcc, e.g. on RHEL5 i386.
 * We're not ever going to use this in an if stmt,
 * but let's be pedantically correct for style points */
# define INCREMENT_ATOMIC(lk, op) do { LOCK (&lk); ++op; UNLOCK (&lk); } while (0)
/* this is a gcc 'statement expression', it works with llvm/clang too */
# define DECREMENT_ATOMIC(lk, op) ({ LOCK (&lk); --op; UNLOCK (&lk); op; })
#endif

xlator_t **__glusterfs_this_location (void);
xlator_t *glusterfs_this_get (void);
int glusterfs_this_set (xlator_t *);

/* syncopctx */
void *syncopctx_getctx (void);
int syncopctx_setctx (void *ctx);

/* task */
void *synctask_get (void);
int synctask_set (void *);

/* uuid_buf */
char *glusterfs_uuid_buf_get (void);
/* lkowner_buf */
char *glusterfs_lkowner_buf_get (void);
/* leaseid buf */
char *glusterfs_leaseid_buf_get (void);

/* init */
int glusterfs_globals_init (glusterfs_ctx_t *ctx);

extern const char *gf_fop_list[];
extern const char *gf_upcall_list[];

/* mem acct enable/disable */
int gf_global_mem_acct_enable_get (void);
int gf_global_mem_acct_enable_set (int val);
#endif /* !_GLOBALS_H */
