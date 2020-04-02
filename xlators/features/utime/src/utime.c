/*
  Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "utime.h"
#include "utime-helpers.h"
#include "utime-messages.h"
#include "utime-mem-types.h"
#include <glusterfs/call-stub.h>

int32_t
gf_utime_invalidate(xlator_t *this, inode_t *inode)
{
    return 0;
}

int32_t
gf_utime_forget(xlator_t *this, inode_t *inode)
{
    return 0;
}

int32_t
gf_utime_client_destroy(xlator_t *this, client_t *client)
{
    return 0;
}

void
gf_utime_ictxmerge(xlator_t *this, fd_t *fd, inode_t *inode,
                   inode_t *linked_inode)
{
    return;
}

int32_t
gf_utime_release(xlator_t *this, fd_t *fd)
{
    return 0;
}

int32_t
gf_utime_releasedir(xlator_t *this, fd_t *fd)
{
    return 0;
}

int32_t
gf_utime_client_disconnect(xlator_t *this, client_t *client)
{
    return 0;
}

int32_t
gf_utime_fdctx_to_dict(xlator_t *this, fd_t *fd, dict_t *dict)
{
    return 0;
}

int32_t
gf_utime_inode(xlator_t *this)
{
    return 0;
}

int32_t
gf_utime_inode_to_dict(xlator_t *this, dict_t *dict)
{
    return 0;
}

int32_t
gf_utime_history(xlator_t *this)
{
    return 0;
}

int32_t
gf_utime_fd(xlator_t *this)
{
    return 0;
}

int32_t
gf_utime_fd_to_dict(xlator_t *this, dict_t *dict)
{
    return 0;
}

int32_t
gf_utime_fdctx(xlator_t *this, fd_t *fd)
{
    return 0;
}

int32_t
gf_utime_inodectx(xlator_t *this, inode_t *ino)
{
    return 0;
}

int32_t
gf_utime_inodectx_to_dict(xlator_t *this, inode_t *ino, dict_t *dict)
{
    return 0;
}

int32_t
gf_utime_priv_to_dict(xlator_t *this, dict_t *dict, char *brickname)
{
    return 0;
}

int32_t
gf_utime_priv(xlator_t *this)
{
    return 0;
}

int32_t
mem_acct_init(xlator_t *this)
{
    if (xlator_mem_acct_init(this, utime_mt_end + 1) != 0) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, UTIME_MSG_NO_MEMORY,
               "Memory accounting initialization failed.");
        return -1;
    }
    return 0;
}

int32_t
gf_utime_set_mdata_setxattr_cbk(call_frame_t *frame, void *cookie,
                                xlator_t *this, int op_ret, int op_errno,
                                dict_t *xdata)
{
    call_stub_t *stub = frame->local;
    /* Don't fail lookup if mdata setxattr fails */
    if (op_ret) {
        gf_msg(this->name, GF_LOG_ERROR, op_errno, UTIME_MSG_SET_MDATA_FAILED,
               "dict set of key for set-ctime-mdata failed");
    }
    frame->local = NULL;
    call_resume(stub);
    STACK_DESTROY(frame->root);
    return 0;
}

int32_t
gf_utime_set_mdata_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                              int32_t op_ret, int32_t op_errno, inode_t *inode,
                              struct iatt *stbuf, dict_t *xdata,
                              struct iatt *postparent)
{
    dict_t *dict = NULL;
    struct mdata_iatt *mdata = NULL;
    int ret = 0;
    loc_t loc = {
        0,
    };
    call_frame_t *new_frame = NULL;

    if (!op_ret && dict_get(xdata, GF_XATTR_MDATA_KEY) == NULL) {
        dict = dict_new();
        if (!dict) {
            op_errno = ENOMEM;
            goto err;
        }
        mdata = GF_MALLOC(sizeof(struct mdata_iatt), gf_common_mt_char);
        if (mdata == NULL) {
            op_errno = ENOMEM;
            goto err;
        }
        iatt_to_mdata(mdata, stbuf);
        ret = dict_set_mdata(dict, CTIME_MDATA_XDATA_KEY, mdata, _gf_false);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_WARNING, ENOMEM, UTIME_MSG_NO_MEMORY,
                   "dict set of key for set-ctime-mdata failed");
            goto err;
        }
        new_frame = copy_frame(frame);
        if (!new_frame) {
            op_errno = ENOMEM;
            goto stub_err;
        }

        new_frame->local = fop_lookup_cbk_stub(frame, default_lookup_cbk,
                                               op_ret, op_errno, inode, stbuf,
                                               xdata, postparent);
        if (!new_frame->local) {
            gf_msg(this->name, GF_LOG_WARNING, ENOMEM, UTIME_MSG_NO_MEMORY,
                   "lookup_cbk stub allocation failed");
            op_errno = ENOMEM;
            STACK_DESTROY(new_frame->root);
            goto stub_err;
        }

        loc.inode = inode_ref(inode);
        gf_uuid_copy(loc.gfid, stbuf->ia_gfid);

        new_frame->root->uid = 0;
        new_frame->root->gid = 0;
        new_frame->root->pid = GF_CLIENT_PID_SET_UTIME;
        STACK_WIND(new_frame, gf_utime_set_mdata_setxattr_cbk,
                   FIRST_CHILD(this), FIRST_CHILD(this)->fops->setxattr, &loc,
                   dict, 0, NULL);

        dict_unref(dict);
        inode_unref(loc.inode);
        return 0;
    }

    STACK_UNWIND_STRICT(lookup, frame, op_ret, op_errno, inode, stbuf, xdata,
                        postparent);
    return 0;

err:
    if (mdata) {
        GF_FREE(mdata);
    }
stub_err:
    if (dict) {
        dict_unref(dict);
    }
    STACK_UNWIND_STRICT(lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);
    return 0;
}

int
gf_utime_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    int op_errno = EINVAL;
    int ret = -1;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(loc, err);
    VALIDATE_OR_GOTO(loc->inode, err);

    xdata = xdata ? dict_ref(xdata) : dict_new();
    if (!xdata) {
        op_errno = ENOMEM;
        goto err;
    }

    ret = dict_set_int8(xdata, GF_XATTR_MDATA_KEY, 1);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, -ret, UTIME_MSG_DICT_SET_FAILED,
               "%s: Unable to set dict value for %s", loc->path,
               GF_XATTR_MDATA_KEY);
        op_errno = -ret;
        goto free_dict;
    }

    STACK_WIND(frame, gf_utime_set_mdata_lookup_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->lookup, loc, xdata);
    dict_unref(xdata);
    return 0;

free_dict:
    dict_unref(xdata);
err:
    STACK_UNWIND_STRICT(lookup, frame, ret, op_errno, NULL, NULL, NULL, NULL);
    return 0;
}

int32_t
init(xlator_t *this)
{
    utime_priv_t *utime = NULL;

    utime = GF_MALLOC(sizeof(*utime), utime_mt_utime_t);
    if (utime == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, UTIME_MSG_NO_MEMORY,
               "Failed to allocate private memory.");
        return -1;
    }
    memset(utime, 0, sizeof(*utime));

    this->private = utime;
    GF_OPTION_INIT("noatime", utime->noatime, bool, err);

    return 0;
err:
    return -1;
}

void
fini(xlator_t *this)
{
    utime_priv_t *utime = NULL;

    utime = this->private;
    GF_FREE(utime);
    return;
}

int32_t
reconfigure(xlator_t *this, dict_t *options)
{
    utime_priv_t *utime = this->private;

    GF_OPTION_RECONF("noatime", utime->noatime, options, bool, err);

    return 0;
err:
    return -1;
}

int
notify(xlator_t *this, int event, void *data, ...)
{
    return default_notify(this, event, data);
}

struct xlator_fops fops = {
    .rename = gf_utime_rename,
    .mknod = gf_utime_mknod,
    .readv = gf_utime_readv,
    .fremovexattr = gf_utime_fremovexattr,
    .open = gf_utime_open,
    .create = gf_utime_create,
    .mkdir = gf_utime_mkdir,
    .writev = gf_utime_writev,
    .rmdir = gf_utime_rmdir,
    .fallocate = gf_utime_fallocate,
    .truncate = gf_utime_truncate,
    .symlink = gf_utime_symlink,
    .zerofill = gf_utime_zerofill,
    .link = gf_utime_link,
    .ftruncate = gf_utime_ftruncate,
    .unlink = gf_utime_unlink,
    .setattr = gf_utime_setattr,
    .fsetattr = gf_utime_fsetattr,
    .opendir = gf_utime_opendir,
    .removexattr = gf_utime_removexattr,
    .lookup = gf_utime_lookup,
};
struct xlator_cbks cbks = {
    .invalidate = gf_utime_invalidate,
    .forget = gf_utime_forget,
    .client_destroy = gf_utime_client_destroy,
    .ictxmerge = gf_utime_ictxmerge,
    .release = gf_utime_release,
    .releasedir = gf_utime_releasedir,
    .client_disconnect = gf_utime_client_disconnect,
};
struct xlator_dumpops dumpops = {
    .fdctx_to_dict = gf_utime_fdctx_to_dict,
    .inode = gf_utime_inode,
    .inode_to_dict = gf_utime_inode_to_dict,
    .history = gf_utime_history,
    .fd = gf_utime_fd,
    .fd_to_dict = gf_utime_fd_to_dict,
    .fdctx = gf_utime_fdctx,
    .inodectx = gf_utime_inodectx,
    .inodectx_to_dict = gf_utime_inodectx_to_dict,
    .priv_to_dict = gf_utime_priv_to_dict,
    .priv = gf_utime_priv,
};

struct volume_options options[] = {
    {.key = {"noatime"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "on",
     .op_version = {GD_OP_VERSION_5_0},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
     .tags = {"ctime"},
     .description = "Enable/Disable atime updation when ctime feature is "
                    "enabled. When noatime is on, atime is not updated with "
                    "ctime feature enabled and vice versa."},
    {.key = {NULL}}};

xlator_api_t xlator_api = {
    .init = init,
    .fini = fini,
    .notify = notify,
    .reconfigure = reconfigure,
    .mem_acct_init = mem_acct_init,
    .op_version = {GD_OP_VERSION_5_0},
    .dumpops = &dumpops,
    .fops = &fops,
    .cbks = &cbks,
    .options = options,
    .identifier = "utime",
    .category = GF_MAINTAINED,
};
