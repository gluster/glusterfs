/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "read-only-common.h"
#include "read-only-mem-types.h"
#include "read-only.h"

int32_t
mem_acct_init(xlator_t *this)
{
    int ret = -1;

    ret = xlator_mem_acct_init(this, gf_read_only_mt_end);
    if (ret)
        gf_log(this->name, GF_LOG_ERROR,
               "Memory accounting "
               "initialization failed.");

    return ret;
}

int32_t
init(xlator_t *this)
{
    int ret = -1;
    read_only_priv_t *priv = NULL;

    if (!this->children || this->children->next) {
        gf_log(this->name, GF_LOG_ERROR,
               "translator not configured with exactly one child");
        return -1;
    }

    if (!this->parents) {
        gf_log(this->name, GF_LOG_WARNING, "dangling volume. check volfile ");
    }

    priv = GF_CALLOC(1, sizeof(*priv), gf_read_only_mt_priv_t);
    if (!priv)
        goto out;

    this->private = priv;

    GF_OPTION_INIT("read-only", priv->readonly_or_worm_enabled, bool, out);

    ret = 0;
out:
    return ret;
}

int
reconfigure(xlator_t *this, dict_t *options)
{
    read_only_priv_t *priv = NULL;
    int ret = -1;
    gf_boolean_t readonly_or_worm_enabled = _gf_false;

    priv = this->private;
    GF_ASSERT(priv);

    GF_OPTION_RECONF("read-only", readonly_or_worm_enabled, options, bool, out);
    priv->readonly_or_worm_enabled = readonly_or_worm_enabled;
    ret = 0;
out:
    gf_log(this->name, GF_LOG_DEBUG, "returning %d", ret);
    return ret;
}

void
fini(xlator_t *this)
{
    read_only_priv_t *priv = NULL;

    priv = this->private;
    if (!priv)
        return;

    this->private = NULL;
    GF_FREE(priv);

    return;
}

struct xlator_fops fops = {
    .mknod = ro_mknod,
    .mkdir = ro_mkdir,
    .unlink = ro_unlink,
    .rmdir = ro_rmdir,
    .symlink = ro_symlink,
    .rename = ro_rename,
    .link = ro_link,
    .truncate = ro_truncate,
    .open = ro_open,
    .writev = ro_writev,
    .setxattr = ro_setxattr,
    .fsetxattr = ro_fsetxattr,
    .removexattr = ro_removexattr,
    .fsyncdir = ro_fsyncdir,
    .ftruncate = ro_ftruncate,
    .create = ro_create,
    .setattr = ro_setattr,
    .fsetattr = ro_fsetattr,
    .xattrop = ro_xattrop,
    .fxattrop = ro_fxattrop,
    .inodelk = ro_inodelk,
    .finodelk = ro_finodelk,
    .entrylk = ro_entrylk,
    .fentrylk = ro_fentrylk,
    .lk = ro_lk,
    .fallocate = ro_fallocate,
};

struct xlator_cbks cbks = {};

struct volume_options options[] = {
    {.key = {"read-only"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "off",
     /*.validate_fn = validate_boolean,*/
     .op_version = {1},
     .flags = OPT_FLAG_SETTABLE,
     .description = "When \"on\", makes a volume read-only. It is turned "
                    "\"off\" by default."},
    {.key = {NULL}},
};

xlator_api_t xlator_api = {
    .init = init,
    .fini = fini,
    .reconfigure = reconfigure,
    .mem_acct_init = mem_acct_init,
    .op_version = {1}, /* Present from the initial version */
    .fops = &fops,
    .cbks = &cbks,
    .options = options,
    .identifier = "read-only",
    .category = GF_TECH_PREVIEW,
};
