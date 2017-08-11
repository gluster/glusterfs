/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* File: posix2-ds-main.c
 * This file contains the xlator loading functions, FOP entry points
 * and options.
 * The entire functionality including comments is TODO.
 */

#include "xlator.h"

#include "posix2-common-fops.h"
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
        .lookup         = posix2_common_lookup,
        .stat           = posix2_common_stat,
        .fstat          = posix2_common_fstat,
        .truncate       = posix2_common_truncate,
        .ftruncate      = posix_ftruncate,
        .access         = posix2_common_access,
        .readlink       = posix2_common_readlink,
        .mknod          = posix2_common_mknod,
        .mkdir          = posix2_common_mkdir,
        .unlink         = posix2_common_unlink,
        .rmdir          = posix2_common_rmdir,
        .symlink        = posix2_common_symlink,
        .rename         = posix2_common_rename,
        .link           = posix2_common_link,
        .create         = posix2_common_create,
        .open           = posix2_common_open,
        .readv          = posix_readv,
        .writev         = posix_writev,
        .flush          = posix_flush,
        .fsync          = posix_fsync,
        .opendir        = posix2_common_opendir,
        .readdir        = posix2_common_readdir,
        .readdirp       = posix2_common_readdirp,
        .fsyncdir       = posix2_common_fsyncdir,
        .statfs         = posix_statfs,
        .setxattr       = posix2_common_setxattr,
        .getxattr       = posix2_common_getxattr,
        .fsetxattr      = posix2_common_fsetxattr,
        .fgetxattr      = posix2_common_fgetxattr,
        .removexattr    = posix2_common_removexattr,
        .fremovexattr   = posix2_common_fremovexattr,
        .lk             = posix_lk,
        .inodelk        = posix_inodelk,
        .finodelk       = posix_finodelk,
        .entrylk        = posix_entrylk,
        .fentrylk       = posix_fentrylk,
        .rchecksum      = posix_rchecksum,
        .xattrop        = posix2_common_xattrop,
        .fxattrop       = posix2_common_fxattrop,
        .setattr        = posix2_common_setattr,
        .fsetattr       = posix2_common_fsetattr,
        .fallocate      = posix_glfallocate,
        .discard        = posix_discard,
        .zerofill       = posix_zerofill,
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
