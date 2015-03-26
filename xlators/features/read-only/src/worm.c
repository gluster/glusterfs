/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "defaults.h"
#include "read-only-common.h"
#include "read-only-mem-types.h"
#include "read-only.h"

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        ret = xlator_mem_acct_init (this, gf_read_only_mt_end + 1);
        if (ret)
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting "
                        "initialization failed.");

        return ret;
}

static int32_t
worm_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
               int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd, xdata);
        return 0;
}

int32_t
worm_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           fd_t *fd, dict_t *xdata)
{
        if (is_readonly_or_worm_enabled (this) &&
            ((((flags & O_ACCMODE) == O_WRONLY) ||
              ((flags & O_ACCMODE) == O_RDWR)) &&
              !(flags & O_APPEND))) {
                STACK_UNWIND_STRICT (open, frame, -1, EROFS, NULL, NULL);
                return 0;
        }

        STACK_WIND (frame, worm_open_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open, loc, flags, fd, xdata);
        return 0;
}

int32_t
init (xlator_t *this)
{
        int                     ret     = -1;
        read_only_priv_t       *priv    = NULL;

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "translator not configured with exactly one child");
                return -1;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile ");
        }

        priv = GF_CALLOC (1, sizeof (*priv), gf_read_only_mt_priv_t);
        if (!priv)
                goto out;

        GF_OPTION_INIT ("worm", priv->readonly_or_worm_enabled, bool, out);

        this->private = priv;
        ret = 0;
out:
        return ret;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        read_only_priv_t  *priv                     = NULL;
        int                ret                      = -1;
        gf_boolean_t       readonly_or_worm_enabled = _gf_false;

        priv = this->private;
        GF_ASSERT (priv);

        GF_OPTION_RECONF ("worm", readonly_or_worm_enabled, options, bool, out);

        priv->readonly_or_worm_enabled = readonly_or_worm_enabled;
        ret = 0;
out:
        gf_log (this->name, GF_LOG_DEBUG, "returning %d", ret);
        return ret;
}

void
fini (xlator_t *this)
{
        read_only_priv_t         *priv    = NULL;

        priv = this->private;
        if (!priv)
                return;

        this->private = NULL;
        GF_FREE (priv);

        return;
}

struct xlator_fops fops = {
        .open        = worm_open,

        .unlink      = ro_unlink,
        .rmdir       = ro_rmdir,
        .rename      = ro_rename,
        .truncate    = ro_truncate,
        .removexattr = ro_removexattr,
        .fsyncdir    = ro_fsyncdir,
        .xattrop     = ro_xattrop,
        .inodelk     = ro_inodelk,
        .finodelk    = ro_finodelk,
        .entrylk     = ro_entrylk,
        .fentrylk    = ro_fentrylk,
        .lk          = ro_lk,
};

struct xlator_cbks cbks;

struct volume_options options[] = {
        { .key  = {"worm"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "When \"on\", makes a volume get write once read many "
                         " feature. It is turned \"off\" by default."
        },
};

