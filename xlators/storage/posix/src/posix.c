/*
   Copyright (c) 2006-2017 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#define __XOPEN_SOURCE 500

/* for SEEK_HOLE and SEEK_DATA */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <glusterfs/xlator.h>
#include "posix.h"

int32_t
mem_acct_init(xlator_t *this);

extern struct volume_options posix_options[];

struct xlator_dumpops dumpops = {
    .priv = posix_priv,
    .inode = posix_inode,
};

struct xlator_fops fops = {
    .lookup = posix_lookup,
    .stat = posix_stat,
    .opendir = posix_opendir,
    .readdir = posix_readdir,
    .readdirp = posix_readdirp,
    .readlink = posix_readlink,
    .mknod = posix_mknod,
    .mkdir = posix_mkdir,
    .unlink = posix_unlink,
    .rmdir = posix_rmdir,
    .symlink = posix_symlink,
    .rename = posix_rename,
    .link = posix_link,
    .truncate = posix_truncate,
    .create = posix_create,
    .open = posix_open,
    .readv = posix_readv,
    .writev = posix_writev,
    .statfs = posix_statfs,
    .flush = posix_flush,
    .fsync = posix_fsync,
    .setxattr = posix_setxattr,
    .fsetxattr = posix_fsetxattr,
    .getxattr = posix_getxattr,
    .fgetxattr = posix_fgetxattr,
    .removexattr = posix_removexattr,
    .fremovexattr = posix_fremovexattr,
    .fsyncdir = posix_fsyncdir,
    .access = posix_access,
    .ftruncate = posix_ftruncate,
    .fstat = posix_fstat,
    .lk = posix_lk,
    .inodelk = posix_inodelk,
    .finodelk = posix_finodelk,
    .entrylk = posix_entrylk,
    .fentrylk = posix_fentrylk,
    .rchecksum = posix_rchecksum,
    .xattrop = posix_xattrop,
    .fxattrop = posix_fxattrop,
    .setattr = posix_setattr,
    .fsetattr = posix_fsetattr,
    .fallocate = posix_glfallocate,
    .discard = posix_discard,
    .zerofill = posix_zerofill,
    .ipc = posix_ipc,
    .seek = posix_seek,
    .lease = posix_lease,
    .put = posix_put,
    .copy_file_range = posix_copy_file_range,
};

struct xlator_cbks cbks = {
    .release = posix_release,
    .releasedir = posix_releasedir,
    .forget = posix_forget,
};

xlator_api_t xlator_api = {
    .init = posix_init,
    .fini = posix_fini,
    .notify = posix_notify,
    .reconfigure = posix_reconfigure,
    .mem_acct_init = mem_acct_init,
    .op_version = {1}, /* Present from the initial version */
    .dumpops = &dumpops,
    .fops = &fops,
    .cbks = &cbks,
    .options = posix_options,
    .identifier = "posix",
    .category = GF_MAINTAINED,
};
