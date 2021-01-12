/*
   Copyright (c) 2008-2012, 2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <glusterfs/xlator.h>
#include <glusterfs/defaults.h>
#include "read-only-common.h"
#include "read-only-mem-types.h"
#include "read-only.h"
#include <glusterfs/syncop.h>
#include "worm-helper.h"

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

static int32_t
worm_open(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          fd_t *fd, dict_t *xdata)
{
    if (is_readonly_or_worm_enabled(frame, this) &&
        (flags & (O_WRONLY | O_RDWR | O_APPEND | O_TRUNC))) {
        STACK_UNWIND_STRICT(open, frame, -1, EROFS, NULL, NULL);
        return 0;
    }

    STACK_WIND_TAIL(frame, FIRST_CHILD(this), FIRST_CHILD(this)->fops->open,
                    loc, flags, fd, xdata);
    return 0;
}

static int32_t
worm_link(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
          dict_t *xdata)
{
    int op_errno = EROFS;
    read_only_priv_t *priv = NULL;

    priv = this->private;
    GF_ASSERT(priv);
    if (is_readonly_or_worm_enabled(frame, this))
        goto out;
    if (!priv->worm_file || (frame->root->pid < 0)) {
        op_errno = 0;
        goto out;
    }

    gf_uuid_copy(oldloc->gfid, oldloc->inode->gfid);
    if (is_wormfile(this, _gf_false, oldloc)) {
        op_errno = 0;
        goto out;
    }
    op_errno = gf_worm_state_transition(this, _gf_false, oldloc, GF_FOP_LINK);

out:
    if (op_errno) {
        if (op_errno < 0)
            op_errno = EROFS;
        STACK_UNWIND_STRICT(link, frame, -1, op_errno, NULL, NULL, NULL, NULL,
                            NULL);
    } else
        STACK_WIND_TAIL(frame, FIRST_CHILD(this), FIRST_CHILD(this)->fops->link,
                        oldloc, newloc, xdata);
    return 0;
}

static int32_t
worm_unlink(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
            dict_t *xdata)
{
    int op_errno = EROFS;
    read_only_priv_t *priv = NULL;

    priv = this->private;
    GF_ASSERT(priv);
    if (is_readonly_or_worm_enabled(frame, this)) {
        goto out;
    }
    if (!priv->worm_file || (frame->root->pid < 0)) {
        op_errno = 0;
        goto out;
    }

    gf_uuid_copy(loc->gfid, loc->inode->gfid);
    if (is_wormfile(this, _gf_false, loc)) {
        op_errno = 0;
        goto out;
    }
    op_errno = gf_worm_state_transition(this, _gf_false, loc, GF_FOP_UNLINK);
out:
    if (op_errno) {
        if (op_errno < 0)
            op_errno = EROFS;
        STACK_UNWIND_STRICT(unlink, frame, -1, op_errno, NULL, NULL, NULL);
    } else
        STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                        FIRST_CHILD(this)->fops->unlink, loc, flags, xdata);
    return 0;
}

static int32_t
worm_rename(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
            dict_t *xdata)
{
    int op_errno = EROFS;
    read_only_priv_t *priv = NULL;

    priv = this->private;
    GF_ASSERT(priv);
    if (is_readonly_or_worm_enabled(frame, this))
        goto out;
    if (!priv->worm_file || (frame->root->pid < 0)) {
        op_errno = 0;
        goto out;
    }

    gf_uuid_copy(oldloc->gfid, oldloc->inode->gfid);
    if (is_wormfile(this, _gf_false, oldloc)) {
        op_errno = 0;
        goto check_newloc;
    }
    op_errno = gf_worm_state_transition(this, _gf_false, oldloc, GF_FOP_RENAME);

    if (op_errno == 0) {
    check_newloc:
        if (newloc->inode != NULL) {
            gf_uuid_copy(newloc->gfid, newloc->inode->gfid);
            if (is_wormfile(this, _gf_false, newloc)) {
                op_errno = 0;
                goto out;
            }
            op_errno = gf_worm_state_transition(this, _gf_false, newloc,
                                                GF_FOP_RENAME);
        }
    }

out:
    if (op_errno) {
        if (op_errno < 0)
            op_errno = EROFS;
        STACK_UNWIND_STRICT(rename, frame, -1, op_errno, NULL, NULL, NULL, NULL,
                            NULL, NULL);
    } else
        STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                        FIRST_CHILD(this)->fops->rename, oldloc, newloc, xdata);
    return 0;
}

static int32_t
worm_truncate(call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
              dict_t *xdata)
{
    int op_errno = EROFS;
    read_only_priv_t *priv = NULL;

    priv = this->private;
    GF_ASSERT(priv);
    if (is_readonly_or_worm_enabled(frame, this))
        goto out;
    if (!priv->worm_file || (frame->root->pid < 0)) {
        op_errno = 0;
        goto out;
    }

    if (is_wormfile(this, _gf_false, loc)) {
        op_errno = 0;
        goto out;
    }
    op_errno = gf_worm_state_transition(this, _gf_false, loc, GF_FOP_TRUNCATE);

out:
    if (op_errno) {
        if (op_errno < 0)
            op_errno = EROFS;
        STACK_UNWIND_STRICT(truncate, frame, -1, op_errno, NULL, NULL, NULL);
    } else
        STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                        FIRST_CHILD(this)->fops->truncate, loc, offset, xdata);
    return 0;
}

static int32_t
worm_ftruncate(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
               dict_t *xdata)
{
    int op_errno = EROFS;
    read_only_priv_t *priv = NULL;

    priv = this->private;
    GF_ASSERT(priv);
    if (is_readonly_or_worm_enabled(frame, this))
        goto out;
    if (!priv->worm_file || (frame->root->pid < 0)) {
        op_errno = 0;
        goto out;
    }

    if (is_wormfile(this, _gf_true, fd)) {
        op_errno = 0;
        goto out;
    }
    op_errno = gf_worm_state_transition(this, _gf_true, fd, GF_FOP_FTRUNCATE);

out:
    if (op_errno) {
        if (op_errno < 0)
            op_errno = EROFS;
        STACK_UNWIND_STRICT(ftruncate, frame, -1, op_errno, NULL, NULL, NULL);
    } else
        STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                        FIRST_CHILD(this)->fops->ftruncate, fd, offset, xdata);
    return 0;
}

static int32_t
worm_setattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
             struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
    gf_boolean_t rd_only = _gf_false;
    worm_reten_state_t reten_state = {
        0,
    };
    struct iatt stpre = {
        0,
    };
    read_only_priv_t *priv = NULL;
    int op_errno = EROFS;
    int ret = -1;

    priv = this->private;
    GF_ASSERT(priv);
    if (!priv->worm_file) {
        op_errno = 0;
        goto out;
    }

    if (is_wormfile(this, _gf_false, loc)) {
        op_errno = 0;
        goto out;
    }
    if (valid & GF_SET_ATTR_MODE) {
        rd_only = gf_worm_write_disabled(stbuf);
        if (!rd_only) {
            op_errno = 0;
            goto out;
        }

        ret = worm_set_state(this, _gf_false, loc, &reten_state, stbuf);
        if (ret) {
            gf_log(this->name, GF_LOG_ERROR, "Error setting worm state");
            goto out;
        }
    } else if (valid & GF_SET_ATTR_ATIME) {
        ret = worm_get_state(this, _gf_false, loc, &reten_state);
        if (ret) {
            op_errno = 0;
            goto out;
        }
        if (reten_state.retain) {
            ret = syncop_stat(this, loc, &stpre, NULL, NULL);
            if (ret)
                goto out;
            if (reten_state.ret_mode == 0) {
                if (stbuf->ia_atime < stpre.ia_mtime) {
                    gf_log(this->name, GF_LOG_ERROR,
                           "Cannot set atime less than "
                           "the mtime for a WORM-Retained "
                           "file");
                    goto out;
                }
            } else {
                if (stbuf->ia_atime < stpre.ia_atime) {
                    gf_log(this->name, GF_LOG_ERROR,
                           "Cannot decrease the atime of a"
                           " WORM-Retained file in "
                           "Enterprise mode");
                    goto out;
                }
            }
            reten_state.ret_period = reten_state.ret_period + stbuf->ia_atime -
                                     stpre.ia_atime;
            ret = gf_worm_set_xattr(this, &reten_state, _gf_false, loc);
            if (ret) {
                goto out;
            }
            stbuf->ia_mtime = stpre.ia_mtime;
        }
    }
    op_errno = 0;

out:
    if (op_errno)
        STACK_UNWIND_STRICT(setattr, frame, -1, EROFS, NULL, NULL, NULL);
    else
        STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                        FIRST_CHILD(this)->fops->setattr, loc, stbuf, valid,
                        xdata);
    return 0;
}

static int32_t
worm_fsetattr(call_frame_t *frame, xlator_t *this, fd_t *fd, struct iatt *stbuf,
              int32_t valid, dict_t *xdata)
{
    gf_boolean_t rd_only = _gf_false;
    worm_reten_state_t reten_state = {
        0,
    };
    struct iatt stpre = {
        0,
    };
    read_only_priv_t *priv = NULL;
    int op_errno = EROFS;
    int ret = -1;

    priv = this->private;
    GF_ASSERT(priv);
    if (!priv->worm_file) {
        op_errno = 0;
        goto out;
    }

    if (is_wormfile(this, _gf_true, fd)) {
        op_errno = 0;
        goto out;
    }
    if (valid & GF_SET_ATTR_MODE) {
        rd_only = gf_worm_write_disabled(stbuf);
        if (!rd_only) {
            op_errno = 0;
            goto out;
        }

        ret = worm_set_state(this, _gf_true, fd, &reten_state, stbuf);
        if (ret) {
            gf_log(this->name, GF_LOG_ERROR, "Error setting worm state");
            goto out;
        }
    } else if (valid & GF_SET_ATTR_ATIME) {
        ret = worm_get_state(this, _gf_true, fd, &reten_state);
        if (ret) {
            op_errno = 0;
            goto out;
        }
        if (reten_state.retain) {
            ret = syncop_fstat(this, fd, &stpre, NULL, NULL);
            if (ret)
                goto out;
            if (reten_state.ret_mode == 0) {
                if (stbuf->ia_atime < stpre.ia_mtime) {
                    gf_log(this->name, GF_LOG_ERROR,
                           "Cannot set atime less than "
                           "the mtime for a WORM-Retained "
                           "file");
                    goto out;
                }
            } else {
                if (stbuf->ia_atime < stpre.ia_atime) {
                    gf_log(this->name, GF_LOG_ERROR,
                           "Cannot decrease the atime of a"
                           " WORM-Retained file in "
                           "Enterprise mode");
                    goto out;
                }
            }
            reten_state.ret_period = reten_state.ret_period + stbuf->ia_atime -
                                     stpre.ia_atime;
            ret = gf_worm_set_xattr(this, &reten_state, _gf_true, fd);
            if (ret) {
                goto out;
            }

            stbuf->ia_mtime = stpre.ia_mtime;
        }
    }
    op_errno = 0;

out:
    if (op_errno)
        STACK_UNWIND_STRICT(fsetattr, frame, -1, op_errno, NULL, NULL, NULL);
    else
        STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                        FIRST_CHILD(this)->fops->fsetattr, fd, stbuf, valid,
                        xdata);
    return 0;
}

static int32_t
worm_writev(call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
            int32_t count, off_t offset, uint32_t flags, struct iobref *iobref,
            dict_t *xdata)
{
    read_only_priv_t *priv = NULL;
    int op_errno = EROFS;

    priv = this->private;
    GF_ASSERT(priv);
    if (!priv->worm_file || (frame->root->pid < 0)) {
        op_errno = 0;
        goto out;
    }
    if (is_wormfile(this, _gf_true, fd)) {
        op_errno = 0;
        goto out;
    }
    op_errno = gf_worm_state_transition(this, _gf_true, fd, GF_FOP_WRITE);

out:
    if (op_errno) {
        if (op_errno < 0)
            op_errno = EROFS;
        STACK_UNWIND_STRICT(writev, frame, -1, op_errno, NULL, NULL, NULL);
    } else
        STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                        FIRST_CHILD(this)->fops->writev, fd, vector, count,
                        offset, flags, iobref, xdata);
    return 0;
}

static int32_t
worm_create_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                struct iatt *buf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
    int ret = 0;
    read_only_priv_t *priv = NULL;
    // In case of an error exit because fd can be NULL and this would
    // cause an segfault when performing fsetxattr . We explicitly
    // unwind to avoid future problems
    if (op_ret < 0) {
        goto out;
    }

    priv = this->private;
    GF_ASSERT(priv);
    if (priv->worm_file) {
        ret = fd_ctx_set(fd, this, 1);
        if (ret) {
            gf_log(this->name, GF_LOG_ERROR,
                   "Failed to set the fd ctx "
                   "for gfid:%s . Worm feature may not work for the gfid",
                   uuid_utoa(inode->gfid));
        }
        ret = worm_init_state(this, _gf_true, fd);
        if (ret) {
            gf_log(this->name, GF_LOG_ERROR, "Error initializing state");
        }
    }

out:
    STACK_UNWIND_STRICT(create, frame, op_ret, op_errno, fd, inode, buf,
                        preparent, postparent, xdata);
    return ret;
}

static int32_t
worm_create(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
            mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
    STACK_WIND(frame, worm_create_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->create, loc, flags, mode, umask, fd,
               xdata);
    return 0;
}

static void
set_reten_mode(read_only_priv_t *priv, char *reten_mode)
{
    if (strcmp(reten_mode, "relax") == 0)
        priv->reten_mode = 0;
    else
        priv->reten_mode = 1;
}

int32_t
init(xlator_t *this)
{
    int ret = -1;
    read_only_priv_t *priv = NULL;
    char *reten_mode = NULL;

    if (!this->children || this->children->next) {
        gf_log(this->name, GF_LOG_ERROR,
               "translator not configured with exactly one child");
        return -1;
    }

    if (!this->parents) {
        gf_log(this->name, GF_LOG_WARNING, "dangling volume. check volfile ");
    }

    this->local_pool = mem_pool_new(read_only_priv_t, 64);
    if (!this->local_pool) {
        ret = -1;
        gf_log(this->name, GF_LOG_ERROR,
               "failed to create read_only_priv_t's memory pool");
        goto out;
    }

    priv = mem_get0(this->local_pool);
    if (!priv) {
        gf_log(this->name, GF_LOG_ERROR, "Error allocating priv");
        goto out;
    }

    this->private = priv;

    GF_OPTION_INIT("worm", priv->readonly_or_worm_enabled, bool, out);
    GF_OPTION_INIT("worm-file-level", priv->worm_file, bool, out);
    GF_OPTION_INIT("default-retention-period", priv->reten_period, int64, out);
    GF_OPTION_INIT("auto-commit-period", priv->com_period, int64, out);
    GF_OPTION_INIT("retention-mode", reten_mode, str, out);
    set_reten_mode(priv, reten_mode);
    GF_OPTION_INIT("worm-files-deletable", priv->worm_files_deletable, bool,
                   out);

    ret = 0;
out:
    return ret;
}

int
reconfigure(xlator_t *this, dict_t *options)
{
    read_only_priv_t *priv = NULL;
    char *reten_mode = NULL;
    int ret = -1;

    priv = this->private;
    GF_ASSERT(priv);

    GF_OPTION_RECONF("worm", priv->readonly_or_worm_enabled, options, bool,
                     out);
    GF_OPTION_RECONF("worm-file-level", priv->worm_file, options, bool, out);
    GF_OPTION_RECONF("default-retention-period", priv->reten_period, options,
                     int64, out);
    GF_OPTION_RECONF("retention-mode", reten_mode, options, str, out);
    set_reten_mode(priv, reten_mode);
    GF_OPTION_RECONF("auto-commit-period", priv->com_period, options, int64,
                     out);
    GF_OPTION_RECONF("worm-files-deletable", priv->worm_files_deletable,
                     options, bool, out);
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
        goto out;
    mem_put(priv);
    this->private = NULL;
    mem_pool_destroy(this->local_pool);
    this->local_pool = NULL;
out:
    return;
}

struct xlator_fops fops = {
    .open = worm_open,
    .writev = worm_writev,
    .setattr = worm_setattr,
    .fsetattr = worm_fsetattr,
    .rename = worm_rename,
    .link = worm_link,
    .unlink = worm_unlink,
    .truncate = worm_truncate,
    .ftruncate = worm_ftruncate,
    .create = worm_create,

    .rmdir = ro_rmdir,
    .removexattr = ro_removexattr,
    .fsyncdir = ro_fsyncdir,
    .xattrop = ro_xattrop,
    .inodelk = ro_inodelk,
    .finodelk = ro_finodelk,
    .entrylk = ro_entrylk,
    .fentrylk = ro_fentrylk,
    .lk = ro_lk,
};

int32_t
worm_release(xlator_t *this, fd_t *fd)
{
    dict_t *dict = NULL;
    int ret = -1;
    dict = dict_new();
    uint64_t value = 0;
    loc_t loc = {
        0,
    };
    read_only_priv_t *priv = NULL;
    priv = this->private;

    if (priv->worm_file) {
        if (!dict) {
            gf_log(this->name, GF_LOG_ERROR, "Error creating the dict");
            goto out;
        }

        ret = fd_ctx_get(fd, this, &value);
        if (ret) {
            gf_log(this->name, GF_LOG_DEBUG, "Failed to get the fd ctx");
        }
        if (!value) {
            goto out;
        }

        ret = dict_set_int8(dict, "trusted.worm_file", 1);
        if (ret) {
            gf_log(this->name, GF_LOG_ERROR,
                   "Error in setting "
                   "the dict");
            goto out;
        }

        loc.inode = inode_ref(fd->inode);
        gf_uuid_copy(loc.gfid, fd->inode->gfid);
        ret = syncop_setxattr(this, &loc, dict, 0, NULL, NULL);
        if (ret) {
            gf_log(this->name, GF_LOG_ERROR, "Error setting xattr");
            goto out;
        }

        gf_worm_state_transition(this, _gf_false, &loc, GF_FOP_WRITE);
    }

out:
    loc_wipe(&loc);
    if (dict)
        dict_unref(dict);
    return 0;
}

struct xlator_cbks cbks = {
    .release = worm_release,
};

struct volume_options options[] = {
    {.key = {"worm"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "off",
     /*.validate_fn = validate_boolean,*/
     .op_version = {2},
     .flags = OPT_FLAG_SETTABLE,
     .description = "When \"on\", makes a volume get write once read many "
                    " feature. It is turned \"off\" by default."},
    {.key = {"worm-file-level"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "off",
     /*.validate_fn = validate_boolean,*/
     .op_version = {GD_OP_VERSION_3_8_0},
     .flags = OPT_FLAG_SETTABLE,
     .description = "When \"on\", activates the file level worm. "
                    "It is turned \"off\" by default."},
    {.key = {"worm-files-deletable"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "on",
     /*.validate_fn = validate_boolean,*/
     .op_version = {GD_OP_VERSION_3_13_0},
     .flags = OPT_FLAG_SETTABLE,
     .description = "When \"off\", doesn't allow the Worm files"
                    "to be deleted. It is turned \"on\" by default."},
    {.key = {"default-retention-period"},
     .type = GF_OPTION_TYPE_TIME,
     .default_value = "120",
     /*.validate_fn = validate_worm_period,*/
     .op_version = {GD_OP_VERSION_3_8_0},
     .flags = OPT_FLAG_SETTABLE,
     .description = "The default retention period for the files."},
    {.key = {"retention-mode"},
     .type = GF_OPTION_TYPE_STR,
     .default_value = "relax",
     /*.validate_fn = validate_reten_mode,*/
     .op_version = {GD_OP_VERSION_3_8_0},
     .flags = OPT_FLAG_SETTABLE,
     .description = "The mode of retention (relax/enterprise). "
                    "It is relax by default."},
    {.key = {"auto-commit-period"},
     .type = GF_OPTION_TYPE_TIME,
     .default_value = "180",
     /*.validate_fn = validate_worm_period,*/
     .op_version = {GD_OP_VERSION_3_8_0},
     .flags = OPT_FLAG_SETTABLE,
     .description = "Auto commit period for the files."},
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
    .identifier = "worm",
    .category = GF_TECH_PREVIEW,
};
