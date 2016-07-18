/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include "statedump.h"
#include "dht-common.h"

class_methods_t class_methods = {
        .init           = dht_init,
        .fini           = dht_fini,
        .reconfigure    = dht_reconfigure,
        .notify         = dht_notify
};

struct xlator_fops fops = {
        .ipc         = dht_ipc,
        .lookup      = dht_lookup,
        .mknod       = dht_mknod,
        .create      = dht_create,

        .open        = dht_open,
        .statfs      = dht_statfs,
        .opendir     = dht_opendir,
        .readdir     = dht_readdir,
        .readdirp    = dht_readdirp,
        .fsyncdir    = dht_fsyncdir,
        .symlink     = dht_symlink,
        .unlink      = dht_unlink,
        .link        = dht_link,
        .mkdir       = dht_mkdir,
        .rmdir       = dht_rmdir,
        .rename      = dht_rename,
        .entrylk     = dht_entrylk,
        .fentrylk    = dht_fentrylk,

        /* Inode read operations */
        .stat        = dht_stat,
        .fstat       = dht_fstat,
        .access      = dht_access,
        .readlink    = dht_readlink,
        .getxattr    = dht_getxattr,
        .fgetxattr    = dht_fgetxattr,
        .readv       = dht_readv,
        .flush       = dht_flush,
        .fsync       = dht_fsync,
        .inodelk     = dht_inodelk,
        .finodelk    = dht_finodelk,
        .lk          = dht_lk,
        .lease       = dht_lease,

        /* Inode write operations */
        .fremovexattr = dht_fremovexattr,
        .removexattr = dht_removexattr,
        .setxattr    = dht_setxattr,
        .fsetxattr   = dht_fsetxattr,
        .truncate    = dht_truncate,
        .ftruncate   = dht_ftruncate,
        .writev      = dht_writev,
        .xattrop     = dht_xattrop,
        .fxattrop    = dht_fxattrop,
        .setattr     = dht_setattr,
        .fsetattr    = dht_fsetattr,
	.fallocate   = dht_fallocate,
	.discard     = dht_discard,
        .zerofill    = dht_zerofill,
};

struct xlator_dumpops dumpops = {
        .priv = dht_priv_dump,
        .inodectx = dht_inodectx_dump,
};


struct xlator_cbks cbks = {
        .release    = dht_release,
//      .releasedir = dht_releasedir,
        .forget     = dht_forget
};
