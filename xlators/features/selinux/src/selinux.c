/*
   Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "xlator.h"

#include "selinux.h"
#include "selinux-messages.h"
#include "selinux-mem-types.h"
#include "compat-errno.h"

static int
selinux_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno, dict_t *dict, dict_t *xdata)
{
        int ret = 0;
        char *name = cookie;

        if (op_errno == 0 && dict && name && (!strcmp(name, SELINUX_GLUSTER_XATTR))) {
                ret = dict_rename_key (dict, SELINUX_GLUSTER_XATTR,
                                       SELINUX_XATTR);
                if (ret < 0)
                        gf_msg (this->name, GF_LOG_ERROR, op_errno,
                                SL_MSG_SELINUX_GLUSTER_XATTR_MISSING,
                                "getxattr failed for %s", SELINUX_XATTR);

        }

        STACK_UNWIND_STRICT (fgetxattr, frame, op_ret, op_errno,
                             dict, xdata);
        return ret;
}


static int
selinux_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                   const char *name, dict_t *xdata)
{
        selinux_priv_t  *priv           = NULL;
        int32_t         op_ret          = -1;
        int32_t         op_errno        = EINVAL;
        char            *xattr_name     = (char *) name;

        priv = this->private;

        GF_VALIDATE_OR_GOTO ("selinux", priv, err);

       /* name can be NULL for listxattr calls */
       if (!priv->selinux_enabled || !name)
                goto off;

        if (strcmp (name, SELINUX_XATTR) == 0)
                xattr_name = SELINUX_GLUSTER_XATTR;

off:
        STACK_WIND_COOKIE (frame, selinux_fgetxattr_cbk, xattr_name,
                           FIRST_CHILD(this),
                           FIRST_CHILD(this)->fops->fgetxattr, fd, xattr_name,
                           xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (fgetxattr, frame, op_ret, op_errno, NULL, xdata);

        return 0;
}

static int
selinux_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, dict_t *dict, dict_t *xdata)
{
        int   ret   = 0;
        char  *name = cookie;

        if (op_errno == 0 && dict && name && (!strcmp(name, SELINUX_GLUSTER_XATTR))) {
                ret = dict_rename_key (dict, SELINUX_GLUSTER_XATTR,
                                       SELINUX_XATTR);
                if (ret < 0)
                        gf_msg (this->name, GF_LOG_ERROR, op_errno,
                                SL_MSG_SELINUX_GLUSTER_XATTR_MISSING,
                                "getxattr failed for %s", SELINUX_XATTR);

        }

        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict, xdata);

        return 0;
}


static int
selinux_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                  const char *name, dict_t *xdata)
{
        selinux_priv_t  *priv           = NULL;
        int32_t         op_ret          = -1;
        int32_t         op_errno        = EINVAL;
        char            *xattr_name     = (char *) name;

        priv = this->private;

        GF_VALIDATE_OR_GOTO ("selinux", priv, err);

        /* name can be NULL for listxattr calls */
        if (!priv->selinux_enabled || !name)
                goto off;

        if (strcmp (name, SELINUX_XATTR) == 0)
                xattr_name = SELINUX_GLUSTER_XATTR;

off:
        STACK_WIND_COOKIE (frame, selinux_getxattr_cbk, xattr_name,
                           FIRST_CHILD(this),
                           FIRST_CHILD(this)->fops->getxattr, loc, xattr_name,
                           xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, NULL, xdata);
        return 0;
}

static int
selinux_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno, dict_t *xdata)
{
        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno, xdata);
        return 0;

}


static int
selinux_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
                   int flags, dict_t *xdata)
{
        selinux_priv_t  *priv           = NULL;
        int32_t         op_ret          = -1;
        int32_t         op_errno        = EINVAL;
        int32_t         ret             = -1;

        priv = this->private;

        GF_VALIDATE_OR_GOTO ("selinux", priv, err);

        if (!priv->selinux_enabled && !dict)
                goto off;

        ret = dict_rename_key (dict, SELINUX_XATTR, SELINUX_GLUSTER_XATTR);
        if (ret < 0 && ret != -ENODATA)
                goto err;

off:
        STACK_WIND (frame, selinux_fsetxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetxattr, fd, dict, flags,
                    xdata);


        return 0;
err:
        STACK_UNWIND_STRICT (fsetxattr, frame, op_ret, op_errno, xdata);
        return 0;

}

static int
selinux_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int op_ret, int op_errno, dict_t *xdata)
{
        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno, xdata);
        return 0;
}


static int
selinux_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                  dict_t *dict, int flags, dict_t *xdata)
{
        selinux_priv_t  *priv           = NULL;
        int32_t         op_ret          = -1;
        int32_t         op_errno        = EINVAL;
        int32_t         ret             = -1;

        priv = this->private;

        GF_VALIDATE_OR_GOTO ("selinux", priv, err);

        if (!priv->selinux_enabled && !dict)
                goto off;

        ret = dict_rename_key (dict, SELINUX_XATTR, SELINUX_GLUSTER_XATTR);
        if (ret < 0 && ret != -ENODATA)
                goto err;

off:
        STACK_WIND (frame, selinux_setxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr, loc, dict, flags,
                    xdata);
        return 0;
err:
        STACK_UNWIND_STRICT (setxattr, frame, op_ret, op_errno, xdata);
        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int      ret = -1;

        GF_VALIDATE_OR_GOTO("selinux", this, out);

        ret = xlator_mem_acct_init (this, gf_selinux_mt_end + 1);

        if (ret != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        SL_MSG_MEM_ACCT_INIT_FAILED,
                        "Memory accounting init failed");
                return ret;
        }
out:
        return ret;
}

int32_t
init (xlator_t *this)
{
        int32_t         ret        = -1;
        selinux_priv_t  *priv      = NULL;

        GF_VALIDATE_OR_GOTO ("selinux", this, out);

        if (!this->children || this->children->next) {
                gf_msg (this->name, GF_LOG_WARNING, 0, SL_MSG_INVALID_VOLFILE,
                        "Error: SELinux (%s) not configured with exactly one "
                        "child", this->name);
                return -1;
        }

        if (this->parents == NULL) {
                gf_msg (this->name, GF_LOG_WARNING, 0, SL_MSG_INVALID_VOLFILE,
                        "Dangling volume. Please check the volfile");
        }

        priv = GF_CALLOC (1, sizeof (*priv), gf_selinux_mt_selinux_priv_t);
        if (!priv) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                ret = ENOMEM;
                goto out;
        }

        GF_OPTION_INIT ("selinux", priv->selinux_enabled, bool, out);

        this->local_pool = mem_pool_new (selinux_priv_t, 64);
        if (!this->local_pool) {
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM, SL_MSG_ENOMEM,
                        "Failed to create local_t's memory pool");
                goto out;
        }

        this->private = (void *)priv;
        ret = 0;
out:
        if (ret) {
                if (priv) {
                         GF_FREE (priv);
                }
                mem_pool_destroy (this->local_pool);
        }
        return ret;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        int32_t         ret        = -1;
        selinux_priv_t *priv       = NULL;

        priv = this->private;

        GF_OPTION_RECONF ("selinux", priv->selinux_enabled, options,
                          bool, out);

        ret = 0;
out:
        return ret;

}

void
fini (xlator_t *this)
{
        selinux_priv_t *priv       = NULL;

        priv = this->private;
        GF_FREE (priv);

        mem_pool_destroy (this->local_pool);

        return;
}

struct xlator_fops fops = {
        .getxattr       = selinux_getxattr,
        .fgetxattr      = selinux_fgetxattr,
        .setxattr       = selinux_setxattr,
        .fsetxattr      = selinux_fsetxattr,
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
        { .key                  = { "selinux" },
          .type                 = GF_OPTION_TYPE_BOOL,
          .default_value        = "on",
          .description          = "Enable/disable selinux translator",
          .op_version           = {GD_OP_VERSION_3_11_0},
          .flags                = OPT_FLAG_SETTABLE,
          .tags                 = {"security" , "linux"},
        },
        { .key                  = { NULL }, }
};
