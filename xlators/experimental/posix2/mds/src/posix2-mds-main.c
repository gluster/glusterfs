/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
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

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "statedump.h"

#include "posix2-common.h"
#include "posix2-common-fops.h"

int32_t
posix2_mds_init (xlator_t *this)
{
        return posix2_common_init (this);
}

void
posix2_mds_fini (xlator_t *this)
{
        return posix2_common_fini (this);
}

class_methods_t class_methods = {
        .init           = posix2_mds_init,
        .fini           = posix2_mds_fini,
};

struct xlator_fops fops = {
        .lookup         = posix2_common_lookup,
        .stat           = posix2_common_stat,
        .fstat          = posix2_common_fstat,
        .truncate       = posix2_common_truncate,
        .ftruncate      = posix2_common_ftruncate,
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
        .readv          = posix2_common_readv,
        .writev         = posix2_common_writev,
        .flush          = posix2_common_flush,
        .fsync          = posix2_common_fsync,
        .opendir        = posix2_common_opendir,
        .readdir        = posix2_common_readdir,
        .readdirp       = posix2_common_readdirp,
        .fsyncdir       = posix2_common_fsyncdir,
        .statfs         = posix2_common_statfs,
        .setxattr       = posix2_common_setxattr,
        .getxattr       = posix2_common_getxattr,
        .fsetxattr      = posix2_common_fsetxattr,
        .fgetxattr      = posix2_common_fgetxattr,
        .removexattr    = posix2_common_removexattr,
        .fremovexattr   = posix2_common_fremovexattr,
        .lk             = posix2_common_lk,
        .inodelk        = posix2_common_inodelk,
        .finodelk       = posix2_common_finodelk,
        .entrylk        = posix2_common_entrylk,
        .fentrylk       = posix2_common_fentrylk,
        .rchecksum      = posix2_common_rchecksum,
        .xattrop        = posix2_common_xattrop,
        .fxattrop       = posix2_common_fxattrop,
        .setattr        = posix2_common_setattr,
        .fsetattr       = posix2_common_fsetattr,
        .fallocate      = posix2_common_fallocate,
        .discard        = posix2_common_discard,
        .zerofill       = posix2_common_zerofill,
        .ipc            = posix2_common_ipc,
        .seek           = posix2_common_seek,
        .lease          = posix2_common_lease,
/*        .compound       = posix2_common_compound, */
        .getactivelk    = posix2_common_getactivelk,
        .setactivelk    = posix2_common_setactivelk,
};

struct xlator_cbks cbks = {
};

/*
struct xlator_dumpops dumpops = {
};
*/
