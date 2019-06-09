/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "dht-common.h"

struct xlator_fops dht_pt_fops = {
    /* we need to keep mkdir to make sure we
       have layout on new directory */
    .mkdir = dht_pt_mkdir,
    .getxattr = dht_pt_getxattr,
    .fgetxattr = dht_pt_fgetxattr,

    /* required to trace fop properly in changelog */
    .rename = dht_pt_rename,

    /* FIXME: commenting the '.lookup()' below made some of
       the failing tests to pass. I would remove the below
       line, but keeping it here as a reminder for people
       to check for issues if they find concerns with DHT
       pass-through logic  */
    /*
      .lookup = dht_lookup,
      .readdir = dht_readdir,
      .readdirp = dht_readdirp,
    */
    /* Keeping above as commented, mainly to support the
       usecase of a gluster volume getting to 1x(anytype),
       due to remove-brick (shrinking) exercise. In that case,
       we would need above fops to be available, so we can
       handle the case of dangling linkto files (if any) */
};

struct xlator_fops fops = {
    .ipc = dht_ipc,
    .lookup = dht_lookup,
    .mknod = dht_mknod,
    .create = dht_create,

    .open = dht_open,
    .statfs = dht_statfs,
    .opendir = dht_opendir,
    .readdir = dht_readdir,
    .readdirp = dht_readdirp,
    .fsyncdir = dht_fsyncdir,
    .symlink = dht_symlink,
    .unlink = dht_unlink,
    .link = dht_link,
    .mkdir = dht_mkdir,
    .rmdir = dht_rmdir,
    .rename = dht_rename,
    .entrylk = dht_entrylk,
    .fentrylk = dht_fentrylk,

    /* Inode read operations */
    .stat = dht_stat,
    .fstat = dht_fstat,
    .access = dht_access,
    .readlink = dht_readlink,
    .getxattr = dht_getxattr,
    .fgetxattr = dht_fgetxattr,
    .readv = dht_readv,
    .flush = dht_flush,
    .fsync = dht_fsync,
    .inodelk = dht_inodelk,
    .finodelk = dht_finodelk,
    .lk = dht_lk,
    .lease = dht_lease,

    /* Inode write operations */
    .fremovexattr = dht_fremovexattr,
    .removexattr = dht_removexattr,
    .setxattr = dht_setxattr,
    .fsetxattr = dht_fsetxattr,
    .truncate = dht_truncate,
    .ftruncate = dht_ftruncate,
    .writev = dht_writev,
    .xattrop = dht_xattrop,
    .fxattrop = dht_fxattrop,
    .setattr = dht_setattr,
    .fsetattr = dht_fsetattr,
    .fallocate = dht_fallocate,
    .discard = dht_discard,
    .zerofill = dht_zerofill,
};

struct xlator_dumpops dumpops = {
    .priv = dht_priv_dump,
    .inodectx = dht_inodectx_dump,
};

struct xlator_cbks cbks = {
    .release = dht_release,
    //      .releasedir = dht_releasedir,
    .forget = dht_forget,
};

extern int32_t
mem_acct_init(xlator_t *this);

extern struct volume_options dht_options[];

xlator_api_t xlator_api = {
    .init = dht_init,
    .fini = dht_fini,
    .notify = dht_notify,
    .reconfigure = dht_reconfigure,
    .mem_acct_init = mem_acct_init,
    .op_version = {1}, /* Present from the initial version */
    .dumpops = &dumpops,
    .fops = &fops,
    .cbks = &cbks,
    .options = dht_options,
    .identifier = "distribute",
    .pass_through_fops = &dht_pt_fops,
    .category = GF_MAINTAINED,
};
