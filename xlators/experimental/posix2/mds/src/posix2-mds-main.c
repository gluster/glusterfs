/*
  Copyright (c) 2016-2017 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* File: posix2-mds-main.c
 * This file contains the xlator loading functions, FOP entry points
 * and options.
 */

#include "xlator.h"

#include "posix2-common-fops.h"
#include "posix2-entry-ops.h"
#include "posix.h"

class_methods_t class_methods = {
        .init           = posix_init,
        .fini           = posix_fini,
        .reconfigure    = posix_reconfigure,
        .notify         = posix_notify
};

struct xlator_dumpops dumpops = {
        .priv    = posix_priv,
        .inode   = posix_inode,
};

struct xlator_fops fops = {
        .lookup         = posix2_lookup,
        .stat           = posix_stat,
        .fstat          = posix_fstat,
        .truncate       = posix_truncate,
        .ftruncate      = posix2_common_ftruncate,
        .access         = posix_access,
        .readlink       = posix_readlink,
        .mknod          = posix2_common_mknod,
        .mkdir          = posix2_common_mkdir,
        .unlink         = posix2_common_unlink,
        .rmdir          = posix2_common_rmdir,
        .symlink        = posix2_common_symlink,
        .rename         = posix2_common_rename,
        .link           = posix2_common_link,
        .create         = posix2_create,
        .open           = posix_open,
        .readv          = posix2_common_readv,
        .writev         = posix2_common_writev,
        .flush          = posix2_common_flush,
        .fsync          = posix_fsync,
        .opendir        = posix_opendir,
        .readdir        = posix_readdir,
        .readdirp       = posix_readdirp,
        .fsyncdir       = posix_fsyncdir,
        .statfs         = posix_statfs,
        .setxattr       = posix_setxattr,
        .getxattr       = posix_getxattr,
        .fsetxattr      = posix_fsetxattr,
        .fgetxattr      = posix_fgetxattr,
        .removexattr    = posix_removexattr,
        .fremovexattr   = posix_fremovexattr,
        .lk             = posix_lk,
        .inodelk        = posix_inodelk,
        .finodelk       = posix_finodelk,
        .entrylk        = posix_entrylk,
        .fentrylk       = posix_fentrylk,
        .rchecksum      = posix2_common_rchecksum,
        .xattrop        = posix_xattrop,
        .fxattrop       = posix_fxattrop,
        .setattr        = posix_setattr,
        .fsetattr       = posix_fsetattr,
        .fallocate      = posix2_common_fallocate,
        .discard        = posix2_common_discard,
        .zerofill       = posix2_common_zerofill,
        .ipc            = posix_ipc,
#ifdef HAVE_SEEK_HOLE
        .seek           = posix_seek,
#endif
        .lease          = posix_lease,
/*        .compound       = posix2_common_compound, */
/*        .getactivelk    = posix2_common_getactivelk,*/
/*        .setactivelk    = posix2_common_setactivelk,*/
};

struct xlator_cbks cbks = {
        .release     = posix_release,
        .releasedir  = posix_releasedir,
        .forget      = posix_forget
};
