/*
 *   Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
 *   This file is part of GlusterFS.
 *
 *   This file is licensed to you under your choice of the GNU Lesser
 *   General Public License, version 3 or any later version (LGPLv3 or
 *   later), or the GNU General Public License, version 2 (GPLv2), in all
 *   cases as published by the Free Software Foundation.
 */

#include <glusterfs/glusterfs.h>
#include <glusterfs/xlator.h>
#include <glusterfs/defaults.h>
#include "cloudsync.h"
#include "cloudsync-common.h"
#include <glusterfs/call-stub.h>
#include "cloudsync-autogen-fops.h"

#include <string.h>
#include <dlfcn.h>

static void
cs_cleanup_private(cs_private_t *priv)
{
    if (priv) {
        if (priv->stores) {
            priv->stores->fini(priv->stores->config);
            GF_FREE(priv->stores);
        }

        pthread_spin_destroy(&priv->lock);
        GF_FREE(priv);
    }

    return;
}

static struct cs_plugin plugins[] = {
    {.name = "cloudsyncs3",
     .library = "cloudsyncs3.so",
     .description = "cloudsync s3 store."},
#if defined(__linux__)
    {.name = "cvlt",
     .library = "cloudsynccvlt.so",
     .description = "Commvault content store."},
#endif
    {.name = NULL},
};

int
cs_init(xlator_t *this)
{
    cs_private_t *priv = NULL;
    gf_boolean_t per_vol = _gf_false;
    int ret = 0;
    char *libpath = NULL;
    store_methods_t *store_methods = NULL;
    void *handle = NULL;
    char *temp_str = NULL;
    int index = 0;
    char *libname = NULL;

    priv = GF_CALLOC(1, sizeof(*priv), gf_cs_mt_cs_private_t);
    if (!priv) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "insufficient memory");
        goto out;
    }

    priv->this = this;

    this->local_pool = mem_pool_new(cs_local_t, 512);
    if (!this->local_pool) {
        gf_msg(this->name, GF_LOG_ERROR, 0, ENOMEM, "initialisation failed.");
        ret = -1;
        goto out;
    }

    this->private = priv;

    GF_OPTION_INIT("cloudsync-remote-read", priv->remote_read, bool, out);

    /* temp workaround. Should be configurable through glusterd*/
    per_vol = _gf_true;

    if (per_vol) {
        if (dict_get_str_sizen(this->options, "cloudsync-storetype",
                               &temp_str) == 0) {
            for (index = 0; plugins[index].name; index++) {
                if (!strcmp(temp_str, plugins[index].name)) {
                    libname = plugins[index].library;
                    break;
                }
            }
        } else {
            ret = 0;
        }

        if (!libname) {
            gf_msg(this->name, GF_LOG_WARNING, 0, 0, "no plugin enabled");
            ret = 0;
            goto out;
        }

        ret = gf_asprintf(&libpath, "%s/%s", CS_PLUGINDIR, libname);
        if (ret == -1) {
            goto out;
        }

        handle = dlopen(libpath, RTLD_NOW);
        if (!handle) {
            gf_msg(this->name, GF_LOG_WARNING, 0, 0,
                   "could not "
                   "load the required library. %s",
                   dlerror());
            ret = 0;
            goto out;
        } else {
            gf_msg(this->name, GF_LOG_INFO, 0, 0,
                   "loading library:%s successful", libname);
        }

        priv->stores = GF_CALLOC(1, sizeof(struct cs_remote_stores),
                                 gf_cs_mt_cs_remote_stores_t);
        if (!priv->stores) {
            gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                   "Could not "
                   "allocate memory for priv->stores");
            ret = -1;
            goto out;
        }

        (void)dlerror(); /* clear out previous error string */

        /* load library methods */
        store_methods = (store_methods_t *)dlsym(handle, "store_ops");
        if (!store_methods) {
            gf_msg(this->name, GF_LOG_ERROR, 0, 0, "null store_methods %s",
                   dlerror());
            ret = -1;
            goto out;
        }

        (void)dlerror();

        if (priv->remote_read) {
            priv->stores->rdfop = store_methods->fop_remote_read;
            if (!priv->stores->rdfop) {
                gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                       "failed to get"
                       " read fop %s",
                       dlerror());
                ret = -1;
                goto out;
            }
        }

        priv->stores->dlfop = store_methods->fop_download;
        if (!priv->stores->dlfop) {
            gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                   "failed to get"
                   " download fop %s",
                   dlerror());
            ret = -1;
            goto out;
        }

        (void)dlerror();
        priv->stores->init = store_methods->fop_init;
        if (!priv->stores->init) {
            gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                   "failed to get"
                   " init fop %s",
                   dlerror());
            ret = -1;
            goto out;
        }

        (void)dlerror();
        priv->stores->reconfigure = store_methods->fop_reconfigure;
        if (!priv->stores->reconfigure) {
            gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                   "failed to get"
                   " reconfigure fop %s",
                   dlerror());
            ret = -1;
            goto out;
        }

        priv->stores->handle = handle;

        priv->stores->config = (void *)((priv->stores->init)(this));
        if (!priv->stores->config) {
            gf_msg(this->name, GF_LOG_ERROR, 0, 0, "null config");
            ret = -1;
            goto out;
        }
    }

    ret = 0;

out:
    if (ret == -1) {
        if (this->local_pool) {
            mem_pool_destroy(this->local_pool);
            this->local_pool = NULL;
        }

        cs_cleanup_private(priv);

        if (handle) {
            dlclose(handle);
        }
    }

    GF_FREE(libpath);

    return ret;
}

int
cs_forget(xlator_t *this, inode_t *inode)
{
    uint64_t ctx_int = 0;
    cs_inode_ctx_t *ctx = NULL;

    inode_ctx_del(inode, this, &ctx_int);
    if (!ctx_int)
        return 0;

    ctx = (cs_inode_ctx_t *)(uintptr_t)ctx_int;

    GF_FREE(ctx);
    return 0;
}

void
cs_fini(xlator_t *this)
{
    cs_private_t *priv = NULL;
    priv = this->private;

    cs_cleanup_private(priv);
}

int
cs_reconfigure(xlator_t *this, dict_t *options)
{
    cs_private_t *priv = NULL;
    int ret = 0;

    priv = this->private;
    if (!priv) {
        ret = -1;
        goto out;
    }

    GF_OPTION_RECONF("cloudsync-remote-read", priv->remote_read, options, bool,
                     out);

    /* needed only for per volume configuration*/
    ret = priv->stores->reconfigure(this, options);

out:
    return ret;
}

int32_t
cs_mem_acct_init(xlator_t *this)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO("cloudsync", this, out);

    ret = xlator_mem_acct_init(this, gf_cs_mt_end);

    if (ret != 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "Memory accounting init failed");
        return ret;
    }
out:
    return ret;
}

int32_t
cs_readdirp(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
            off_t off, dict_t *xdata)
{
    int ret = 0;
    int op_errno = ENOMEM;

    if (!xdata) {
        xdata = dict_new();
        if (!xdata) {
            gf_msg(this->name, GF_LOG_ERROR, 0, ENOMEM,
                   "failed to create "
                   "dict");
            goto err;
        }
    }

    ret = dict_set_uint32(xdata, GF_CS_OBJECT_STATUS, 1);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "dict_set failed key:"
               " %s",
               GF_CS_OBJECT_STATUS);
        goto err;
    }

    STACK_WIND(frame, default_readdirp_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readdirp, fd, size, off, xdata);
    return 0;
err:
    STACK_UNWIND_STRICT(readdirp, frame, -1, op_errno, NULL, NULL);
    return 0;
}

int32_t
cs_truncate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                struct iatt *postbuf, dict_t *xdata)
{
    cs_local_t *local = NULL;
    int ret = 0;
    uint64_t val = 0;

    local = frame->local;

    local->call_cnt++;

    if (op_ret == -1) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "truncate failed");
        ret = dict_get_uint64(xdata, GF_CS_OBJECT_STATUS, &val);
        if (ret == 0) {
            if (val == GF_CS_ERROR) {
                gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                       "could not get file state, unwinding");
                op_ret = -1;
                op_errno = EIO;
                goto unwind;
            } else {
                __cs_inode_ctx_update(this, local->loc.inode, val);
                gf_msg(this->name, GF_LOG_INFO, 0, 0, " state = %" PRIu64, val);

                if (local->call_cnt == 1 &&
                    (val == GF_CS_REMOTE || val == GF_CS_DOWNLOADING)) {
                    gf_msg(this->name, GF_LOG_WARNING, 0, 0,
                           "will repair and download "
                           "the file, current state : %" PRIu64,
                           val);
                    goto repair;
                } else {
                    gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                           "second truncate, Unwinding");
                    goto unwind;
                }
            }
        } else {
            gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                   "file state "
                   "could not be figured, unwinding");
            goto unwind;
        }
    } else {
        /* successful write => file is local */
        __cs_inode_ctx_update(this, local->loc.inode, GF_CS_LOCAL);
        gf_msg(this->name, GF_LOG_INFO, 0, 0,
               "state : GF_CS_LOCAL"
               ", truncate successful");

        goto unwind;
    }

repair:
    ret = locate_and_execute(frame);
    if (ret) {
        goto unwind;
    }

    return 0;

unwind:
    CS_STACK_UNWIND(truncate, frame, op_ret, op_errno, prebuf, postbuf, xdata);
    return 0;
}

int32_t
cs_truncate(call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
            dict_t *xdata)
{
    cs_local_t *local = NULL;
    int ret = 0;
    cs_inode_ctx_t *ctx = NULL;
    gf_cs_obj_state state = -1;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(loc, err);

    local = cs_local_init(this, frame, loc, NULL, GF_FOP_TRUNCATE);
    if (!local) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "local init failed");
        goto err;
    }

    __cs_inode_ctx_get(this, loc->inode, &ctx);

    if (ctx)
        state = __cs_get_file_state(loc->inode, ctx);
    else
        state = GF_CS_LOCAL;

    local->xattr_req = xdata ? dict_ref(xdata) : (xdata = dict_new());

    ret = dict_set_uint32(local->xattr_req, GF_CS_OBJECT_STATUS, 1);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "dict_set failed key:"
               " %s",
               GF_CS_OBJECT_STATUS);
        goto err;
    }

    local->stub = fop_truncate_stub(frame, cs_resume_truncate, loc, offset,
                                    xdata);
    if (!local->stub) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "insufficient memory");
        goto err;
    }

    if (state == GF_CS_LOCAL) {
        STACK_WIND(frame, cs_truncate_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->truncate, loc, offset, xdata);

    } else {
        local->call_cnt++;
        ret = locate_and_execute(frame);
        if (ret) {
            goto err;
        }
    }

    return 0;
err:
    CS_STACK_UNWIND(truncate, frame, -1, ENOMEM, NULL, NULL, NULL);
    return 0;
}

int32_t
cs_statfs_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct statvfs *buf, dict_t *xdata)
{
    STACK_UNWIND_STRICT(statfs, frame, op_ret, op_errno, buf, xdata);
    return 0;
}

int32_t
cs_statfs(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    STACK_WIND(frame, cs_statfs_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->statfs, loc, xdata);
    return 0;
}

int32_t
cs_getxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
    STACK_UNWIND_STRICT(getxattr, frame, op_ret, op_errno, dict, xdata);
    return 0;
}

int32_t
cs_getxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, const char *name,
            dict_t *xattr_req)
{
    STACK_WIND(frame, cs_getxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->getxattr, loc, name, xattr_req);
    return 0;
}

int32_t
cs_setxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    cs_local_t *local = NULL;

    local = frame->local;

    if (local->locked)
        cs_inodelk_unlock(frame);

    CS_STACK_UNWIND(setxattr, frame, op_ret, op_errno, xdata);

    return 0;
}

int32_t
cs_setxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
            int32_t flags, dict_t *xdata)
{
    data_t *tmp = NULL;
    cs_local_t *local = NULL;
    int ret = 0;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);

    local = cs_local_init(this, frame, loc, NULL, GF_FOP_SETXATTR);
    if (!local) {
        ret = -1;
        goto err;
    }

    local->xattr_req = xdata ? dict_ref(xdata) : (xdata = dict_new());

    tmp = dict_get_sizen(dict, GF_CS_OBJECT_UPLOAD_COMPLETE);
    if (tmp) {
        /* Value of key should be the atime */
        local->stub = fop_setxattr_stub(frame, cs_resume_setxattr, loc, dict,
                                        flags, xdata);

        if (!local->stub)
            goto err;

        ret = locate_and_execute(frame);
        if (ret) {
            goto err;
        }

        return 0;
    }

    STACK_WIND(frame, cs_setxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->setxattr, loc, dict, flags, xdata);
    return 0;
err:
    CS_STACK_UNWIND(setxattr, frame, -1, errno, NULL);
    return 0;
}

int32_t
cs_fgetxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *dict, dict_t *xdata)
{
    STACK_UNWIND_STRICT(fgetxattr, frame, op_ret, op_errno, dict, xdata);
    return 0;
}

int32_t
cs_fgetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name,
             dict_t *xdata)
{
    STACK_WIND(frame, cs_fgetxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fgetxattr, fd, name, xdata);
    return 0;
}

int32_t
cs_fsetxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    STACK_UNWIND_STRICT(fsetxattr, frame, op_ret, op_errno, xdata);
    return 0;
}

int32_t
cs_fsetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
             int32_t flags, dict_t *xdata)
{
    STACK_WIND(frame, cs_fsetxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fsetxattr, fd, dict, flags, xdata);
    return 0;
}

int32_t
cs_unlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iatt *preparent, struct iatt *postparent,
              dict_t *xdata)
{
    STACK_UNWIND_STRICT(unlink, frame, op_ret, op_errno, preparent, postparent,
                        xdata);
    return 0;
}

int32_t
cs_unlink(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          dict_t *xattr_req)
{
    cs_local_t *local = NULL;
    int ret = 0;

    local = cs_local_init(this, frame, loc, NULL, GF_FOP_UNLINK);
    if (!local)
        goto err;

    local->xattr_req = xattr_req ? dict_ref(xattr_req) : dict_new();

    ret = dict_set_uint32(local->xattr_req, GF_CS_OBJECT_STATUS, 1);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "dict_set failed key:"
               " %s",
               GF_CS_OBJECT_STATUS);
        goto err;
    }
    STACK_WIND(frame, cs_unlink_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->unlink, loc, flags, local->xattr_req);
    return 0;
err:
    CS_STACK_UNWIND(unlink, frame, -1, errno, NULL, NULL, NULL);
    return 0;
}

int32_t
cs_open_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
            int32_t op_errno, fd_t *fd, dict_t *xdata)
{
    int ret = 0;
    uint64_t val = 0;

    if (op_ret == 0) {
        ret = dict_get_uint64(xdata, GF_CS_OBJECT_STATUS, &val);
        if (!ret) {
            ret = __cs_inode_ctx_update(this, fd->inode, val);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, 0, "ctx update failed");
            }
        }
    } else {
        cs_inode_ctx_reset(this, fd->inode);
    }

    CS_STACK_UNWIND(open, frame, op_ret, op_errno, fd, xdata);
    return 0;
}

int32_t
cs_open(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
        fd_t *fd, dict_t *xattr_req)
{
    cs_local_t *local = NULL;
    int ret = 0;

    local = cs_local_init(this, frame, NULL, fd, GF_FOP_OPEN);
    if (!local)
        goto err;

    local->xattr_req = xattr_req ? dict_ref(xattr_req) : dict_new();

    ret = dict_set_uint32(local->xattr_req, GF_CS_OBJECT_STATUS, 1);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "dict_set failed key:"
               " %s",
               GF_CS_OBJECT_STATUS);
        goto err;
    }

    STACK_WIND(frame, cs_open_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->open, loc, flags, fd, local->xattr_req);
    return 0;
err:
    CS_STACK_UNWIND(open, frame, -1, errno, NULL, NULL);
    return 0;
}

int32_t
cs_fstat_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, struct iatt *buf, dict_t *xdata)
{
    int ret = 0;
    uint64_t val = 0;
    fd_t *fd = NULL;
    cs_local_t *local = NULL;

    local = frame->local;

    fd = local->fd;

    if (op_ret == 0) {
        ret = dict_get_uint64(xdata, GF_CS_OBJECT_STATUS, &val);
        if (!ret) {
            gf_msg_debug(this->name, 0, "state %" PRIu64, val);
            ret = __cs_inode_ctx_update(this, fd->inode, val);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, 0, "ctx update failed");
            }
        }
    } else {
        cs_inode_ctx_reset(this, fd->inode);
    }

    CS_STACK_UNWIND(fstat, frame, op_ret, op_errno, buf, xdata);

    return 0;
}

int32_t
cs_fstat(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xattr_req)
{
    cs_local_t *local = NULL;
    int ret = 0;

    local = cs_local_init(this, frame, NULL, fd, GF_FOP_FSTAT);
    if (!local)
        goto err;

    if (fd->inode->ia_type == IA_IFDIR)
        goto wind;

    local->xattr_req = xattr_req ? dict_ref(xattr_req) : dict_new();

    ret = dict_set_uint32(local->xattr_req, GF_CS_OBJECT_STATUS, 1);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "dict_set failed key:"
               " %s",
               GF_CS_OBJECT_STATUS);
        goto err;
    }

wind:
    STACK_WIND(frame, cs_fstat_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fstat, fd, local->xattr_req);
    return 0;
err:
    CS_STACK_UNWIND(fstat, frame, -1, errno, NULL, NULL);
    return 0;
}

cs_local_t *
cs_local_init(xlator_t *this, call_frame_t *frame, loc_t *loc, fd_t *fd,
              glusterfs_fop_t fop)
{
    cs_local_t *local = NULL;
    int ret = 0;

    local = mem_get0(this->local_pool);
    if (!local)
        goto out;

    if (loc) {
        ret = loc_copy(&local->loc, loc);
        if (ret)
            goto out;
    }

    if (fd) {
        local->fd = fd_ref(fd);
    }

    local->op_ret = -1;
    local->op_errno = EUCLEAN;
    local->fop = fop;
    local->dloffset = 0;
    frame->local = local;
    local->locked = _gf_false;
    local->call_cnt = 0;
out:
    if (ret) {
        if (local)
            mem_put(local);
        local = NULL;
    }

    return local;
}

call_frame_t *
cs_lock_frame(call_frame_t *parent_frame)
{
    call_frame_t *lock_frame = NULL;

    lock_frame = copy_frame(parent_frame);

    if (lock_frame == NULL)
        goto out;

    set_lk_owner_from_ptr(&lock_frame->root->lk_owner, parent_frame->root);

out:
    return lock_frame;
}

void
cs_lock_wipe(call_frame_t *lock_frame)
{
    CS_STACK_DESTROY(lock_frame);
}

int32_t
cs_inodelk_unlock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    cs_lock_wipe(frame);

    return 0;
}

int
cs_inodelk_unlock(call_frame_t *main_frame)
{
    xlator_t *this = NULL;
    struct gf_flock flock = {
        0,
    };
    call_frame_t *lock_frame = NULL;
    cs_local_t *lock_local = NULL;
    cs_local_t *main_local = NULL;
    int ret = 0;

    this = main_frame->this;
    main_local = main_frame->local;

    lock_frame = cs_lock_frame(main_frame);
    if (!lock_frame)
        goto out;

    lock_local = cs_local_init(this, lock_frame, NULL, NULL, 0);
    if (!lock_local)
        goto out;

    ret = cs_build_loc(&lock_local->loc, main_frame);
    if (ret) {
        goto out;
    }

    flock.l_type = F_UNLCK;

    main_local->locked = _gf_false;

    STACK_WIND(lock_frame, cs_inodelk_unlock_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->inodelk, CS_LOCK_DOMAIN,
               &lock_local->loc, F_SETLKW, &flock, NULL);

    return 0;

out:
    gf_msg(this->name, GF_LOG_ERROR, 0, 0,
           "Stale lock would be found on"
           " server");

    if (lock_frame)
        cs_lock_wipe(lock_frame);

    return 0;
}

int
cs_download_task(void *arg)
{
    call_frame_t *frame = NULL;
    xlator_t *this = NULL;
    cs_private_t *priv = NULL;
    int ret = -1;
    char *sign_req = NULL;
    fd_t *fd = NULL;
    cs_local_t *local = NULL;
    dict_t *dict = NULL;

    frame = (call_frame_t *)arg;

    this = frame->this;

    priv = this->private;

    if (!priv->stores) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "No remote store "
               "plugins found");
        ret = -1;
        goto out;
    }

    local = frame->local;

    if (local->fd)
        fd = fd_anonymous(local->fd->inode);
    else
        fd = fd_anonymous(local->loc.inode);

    if (!fd) {
        gf_msg("CS", GF_LOG_ERROR, 0, 0, "fd creation failed");
        ret = -1;
        goto out;
    }

    local->dlfd = fd;
    local->dloffset = 0;

    dict = dict_new();
    if (!dict) {
        gf_msg(this->name, GF_LOG_ERROR, 0, ENOMEM,
               "failed to create "
               "dict");
        ret = -1;
        goto out;
    }

    ret = dict_set_uint32(dict, GF_CS_OBJECT_DOWNLOADING, 1);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "dict_set failed");
        ret = -1;
        goto out;
    }

    ret = syncop_fsetxattr(this, local->fd, dict, 0, NULL, NULL);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "fsetxattr failed "
               "key %s",
               GF_CS_OBJECT_DOWNLOADING);
        ret = -1;
        goto out;
    }
    /*this calling method is for per volume setting */
    ret = priv->stores->dlfop(frame, priv->stores->config);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "download failed"
               ", remotepath: %s",
               local->remotepath);

        /*using dlfd as it is anonymous and have RDWR flag*/
        ret = syncop_ftruncate(FIRST_CHILD(this), local->dlfd, 0, NULL, NULL,
                               NULL, NULL);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, -ret, "ftruncate failed");
        } else {
            gf_msg_debug(this->name, 0, "ftruncate succeed");
        }

        ret = -1;
        goto out;
    } else {
        gf_msg(this->name, GF_LOG_INFO, 0, 0,
               "download success, path"
               " : %s",
               local->remotepath);

        ret = syncop_fremovexattr(this, local->fd, GF_CS_OBJECT_REMOTE, NULL,
                                  NULL);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, -ret,
                   "removexattr failed, remotexattr");
            ret = -1;
            goto out;
        } else {
            gf_msg_debug(this->name, 0,
                         "fremovexattr success, "
                         "path : %s",
                         local->remotepath);
        }

        ret = syncop_fremovexattr(this, local->fd, GF_CS_OBJECT_DOWNLOADING,
                                  NULL, NULL);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, -ret,
                   "removexattr failed, downloading xattr, path %s",
                   local->remotepath);
            ret = -1;
            goto out;
        } else {
            gf_msg_debug(this->name, 0,
                         "fremovexattr success"
                         " path  %s",
                         local->remotepath);
        }
    }

out:
    GF_FREE(sign_req);

    if (dict)
        dict_unref(dict);

    if (fd) {
        fd_unref(fd);
        local->dlfd = NULL;
    }

    return ret;
}

int
cs_download(call_frame_t *frame)
{
    int ret = 0;
    cs_local_t *local = NULL;
    xlator_t *this = NULL;

    local = frame->local;
    this = frame->this;

    if (!local->remotepath) {
        ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "remote path not"
               " available. Check posix logs to resolve");
        goto out;
    }

    ret = cs_download_task((void *)frame);
out:
    return ret;
}

int
cs_set_xattr_req(call_frame_t *frame)
{
    cs_local_t *local = NULL;
    GF_UNUSED int ret = 0;

    local = frame->local;

    /* When remote reads are performed (i.e. reads on remote store),
     * there needs to be a way to associate a file on gluster volume
     * with its correspnding file on the remote store. In order to do
     * that, a unique key can be maintained as an xattr
     * (GF_CS_XATTR_ARCHIVE_UUID)on the stub file on gluster bricks.
     * This xattr should be provided to the plugin to
     * perform the read fop on the correct file. This assumes that the file
     * hierarchy and name need not be the same on remote store as that of
     * the gluster volume.
     */
    ret = dict_set_sizen_str_sizen(local->xattr_req, GF_CS_XATTR_ARCHIVE_UUID,
                                   "1");

    return 0;
}

int
cs_update_xattrs(call_frame_t *frame, dict_t *xdata)
{
    cs_local_t *local = NULL;
    xlator_t *this = NULL;
    int size = -1;
    GF_UNUSED int ret = 0;

    local = frame->local;
    this = frame->this;

    local->xattrinfo.lxattr = GF_CALLOC(1, sizeof(cs_loc_xattr_t),
                                        gf_cs_mt_cs_lxattr_t);
    if (!local->xattrinfo.lxattr) {
        local->op_ret = -1;
        local->op_errno = ENOMEM;
        goto err;
    }

    gf_uuid_copy(local->xattrinfo.lxattr->gfid, local->loc.gfid);

    if (local->remotepath) {
        local->xattrinfo.lxattr->file_path = gf_strdup(local->remotepath);
        if (!local->xattrinfo.lxattr->file_path) {
            local->op_ret = -1;
            local->op_errno = ENOMEM;
            goto err;
        }
    }

    ret = dict_get_gfuuid(xdata, GF_CS_XATTR_ARCHIVE_UUID,
                          &(local->xattrinfo.lxattr->uuid));

    if (ret) {
        gf_uuid_clear(local->xattrinfo.lxattr->uuid);
    }
    size = strlen(this->name) - strlen("-cloudsync") + 1;
    local->xattrinfo.lxattr->volname = GF_CALLOC(1, size, gf_common_mt_char);
    if (!local->xattrinfo.lxattr->volname) {
        local->op_ret = -1;
        local->op_errno = ENOMEM;
        goto err;
    }
    strncpy(local->xattrinfo.lxattr->volname, this->name, size - 1);
    local->xattrinfo.lxattr->volname[size - 1] = '\0';

    return 0;
err:
    cs_xattrinfo_wipe(local);
    return -1;
}

int
cs_serve_readv(call_frame_t *frame, off_t offset, size_t size, uint32_t flags)
{
    xlator_t *this = NULL;
    cs_private_t *priv = NULL;
    int ret = -1;
    fd_t *fd = NULL;
    cs_local_t *local = NULL;

    local = frame->local;
    this = frame->this;
    priv = this->private;

    if (!local->remotepath) {
        ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "remote path not"
               " available. Check posix logs to resolve");
        goto out;
    }

    if (!priv->stores) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "No remote store "
               "plugins found");
        ret = -1;
        goto out;
    }

    if (local->fd) {
        fd = fd_anonymous(local->fd->inode);
    } else {
        fd = fd_anonymous(local->loc.inode);
    }

    local->xattrinfo.size = size;
    local->xattrinfo.offset = offset;
    local->xattrinfo.flags = flags;

    if (!fd) {
        gf_msg("CS", GF_LOG_ERROR, 0, 0, "fd creation failed");
        ret = -1;
        goto out;
    }

    local->dlfd = fd;
    local->dloffset = offset;

    /*this calling method is for per volume setting */
    ret = priv->stores->rdfop(frame, priv->stores->config);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "read failed"
               ", remotepath: %s",
               local->remotepath);
        ret = -1;
        goto out;
    } else {
        gf_msg(this->name, GF_LOG_INFO, 0, 0,
               "read success, path"
               " : %s",
               local->remotepath);
    }

out:
    if (fd) {
        fd_unref(fd);
        local->dlfd = NULL;
    }
    return ret;
}

int32_t
cs_readv_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
             int32_t op_errno, struct iovec *vector, int32_t count,
             struct iatt *stbuf, struct iobref *iobref, dict_t *xdata)
{
    cs_local_t *local = NULL;
    int ret = 0;
    uint64_t val = 0;
    fd_t *fd = NULL;

    local = frame->local;
    fd = local->fd;

    local->call_cnt++;

    if (op_ret == -1) {
        ret = dict_get_uint64(xdata, GF_CS_OBJECT_STATUS, &val);
        if (ret == 0) {
            if (val == GF_CS_ERROR) {
                gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                       "could not get file state, unwinding");
                op_ret = -1;
                op_errno = EIO;
                goto unwind;
            } else {
                __cs_inode_ctx_update(this, fd->inode, val);
                gf_msg(this->name, GF_LOG_INFO, 0, 0, " state = %" PRIu64, val);

                if (local->call_cnt == 1 &&
                    (val == GF_CS_REMOTE || val == GF_CS_DOWNLOADING)) {
                    gf_msg(this->name, GF_LOG_INFO, 0, 0,
                           " will read from remote : %" PRIu64, val);
                    goto repair;
                } else {
                    gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                           "second readv, Unwinding");
                    goto unwind;
                }
            }
        } else {
            gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                   "file state "
                   "could not be figured, unwinding");
            goto unwind;
        }
    } else {
        /* successful readv => file is local */
        __cs_inode_ctx_update(this, fd->inode, GF_CS_LOCAL);
        gf_msg(this->name, GF_LOG_INFO, 0, 0,
               "state : GF_CS_LOCAL"
               ", readv successful");

        goto unwind;
    }

repair:
    ret = locate_and_execute(frame);
    if (ret) {
        goto unwind;
    }

    return 0;

unwind:
    CS_STACK_UNWIND(readv, frame, op_ret, op_errno, vector, count, stbuf,
                    iobref, xdata);

    return 0;
}

int32_t
cs_resume_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                off_t offset, uint32_t flags, dict_t *xdata)
{
    int ret = 0;

    ret = cs_resume_postprocess(this, frame, fd->inode);
    if (ret) {
        goto unwind;
    }

    cs_inodelk_unlock(frame);

    STACK_WIND(frame, cs_readv_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readv, fd, size, offset, flags, xdata);

    return 0;

unwind:
    cs_inodelk_unlock(frame);

    cs_common_cbk(frame);

    return 0;
}

int32_t
cs_resume_remote_readv(call_frame_t *frame, xlator_t *this, fd_t *fd,
                       size_t size, off_t offset, uint32_t flags, dict_t *xdata)
{
    int ret = 0;
    cs_local_t *local = NULL;
    gf_cs_obj_state state = -1;
    cs_inode_ctx_t *ctx = NULL;

    cs_inodelk_unlock(frame);

    local = frame->local;
    if (!local) {
        ret = -1;
        goto unwind;
    }

    __cs_inode_ctx_get(this, fd->inode, &ctx);

    state = __cs_get_file_state(fd->inode, ctx);
    if (state == GF_CS_ERROR) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "status is GF_CS_ERROR."
               " Aborting readv");
        local->op_ret = -1;
        local->op_errno = EREMOTE;
        ret = -1;
        goto unwind;
    }

    /* Serve readv from remote store only if it is remote. */
    gf_msg_debug(this->name, 0, "status of file %s is %d",
                 local->remotepath ? local->remotepath : "", state);

    /* We will reach this condition if local inode ctx had REMOTE
     * state when the control was in cs_readv but after stat
     * we got an updated state saying that the file is LOCAL.
     */
    if (state == GF_CS_LOCAL) {
        STACK_WIND(frame, cs_readv_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->readv, fd, size, offset, flags,
                   xdata);
    } else if (state == GF_CS_REMOTE) {
        ret = cs_resume_remote_readv_postprocess(this, frame, fd->inode, offset,
                                                 size, flags);
        /* Failed to submit the remote readv fop to plugin */
        if (ret) {
            local->op_ret = -1;
            local->op_errno = EREMOTE;
            goto unwind;
        }
        /* When the file is in any other intermediate state,
         * we should not perform remote reads.
         */
    } else {
        local->op_ret = -1;
        local->op_errno = EINVAL;
        goto unwind;
    }

    return 0;

unwind:
    cs_common_cbk(frame);

    return 0;
}

int32_t
cs_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
         off_t offset, uint32_t flags, dict_t *xdata)
{
    int op_errno = ENOMEM;
    cs_local_t *local = NULL;
    int ret = 0;
    cs_inode_ctx_t *ctx = NULL;
    gf_cs_obj_state state = -1;
    cs_private_t *priv = NULL;

    VALIDATE_OR_GOTO(frame, err);
    VALIDATE_OR_GOTO(this, err);
    VALIDATE_OR_GOTO(fd, err);

    priv = this->private;

    local = cs_local_init(this, frame, NULL, fd, GF_FOP_READ);
    if (!local) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "local init failed");
        goto err;
    }

    __cs_inode_ctx_get(this, fd->inode, &ctx);

    if (ctx)
        state = __cs_get_file_state(fd->inode, ctx);
    else
        state = GF_CS_LOCAL;

    local->xattr_req = xdata ? dict_ref(xdata) : (xdata = dict_new());

    ret = dict_set_uint32(local->xattr_req, GF_CS_OBJECT_STATUS, 1);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "dict_set failed key:"
               " %s",
               GF_CS_OBJECT_STATUS);
        goto err;
    }

    if (priv->remote_read) {
        local->stub = fop_readv_stub(frame, cs_resume_remote_readv, fd, size,
                                     offset, flags, xdata);
    } else {
        local->stub = fop_readv_stub(frame, cs_resume_readv, fd, size, offset,
                                     flags, xdata);
    }
    if (!local->stub) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "insufficient memory");
        goto err;
    }

    if (state == GF_CS_LOCAL) {
        STACK_WIND(frame, cs_readv_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->readv, fd, size, offset, flags,
                   xdata);
    } else {
        local->call_cnt++;
        ret = locate_and_execute(frame);
        if (ret) {
            goto err;
        }
    }

    return 0;

err:
    CS_STACK_UNWIND(readv, frame, -1, op_errno, NULL, -1, NULL, NULL, NULL);

    return 0;
}

int
cs_resume_remote_readv_postprocess(xlator_t *this, call_frame_t *frame,
                                   inode_t *inode, off_t offset, size_t size,
                                   uint32_t flags)
{
    int ret = 0;

    ret = cs_serve_readv(frame, offset, size, flags);

    return ret;
}

int
cs_stat_check_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                  int op_errno, struct iatt *stbuf, dict_t *xdata)
{
    cs_local_t *local = NULL;
    call_stub_t *stub = NULL;
    char *filepath = NULL;
    int ret = 0;
    inode_t *inode = NULL;
    uint64_t val = 0;

    local = frame->local;

    if (op_ret == -1) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;
        gf_msg(this->name, GF_LOG_ERROR, 0, op_errno, "stat check failed");
        goto err;
    } else {
        if (local->fd)
            inode = local->fd->inode;
        else
            inode = local->loc.inode;

        if (!inode) {
            local->op_ret = -1;
            local->op_errno = EINVAL;
            gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                   "null inode "
                   "returned");
            goto err;
        }

        ret = dict_get_uint64(xdata, GF_CS_OBJECT_STATUS, &val);
        if (ret == 0) {
            if (val == GF_CS_ERROR) {
                cs_inode_ctx_reset(this, inode);
                local->op_ret = -1;
                local->op_errno = EIO;
                gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                       "status = GF_CS_ERROR. failed to get "
                       " file state");
                goto err;
            } else {
                ret = __cs_inode_ctx_update(this, inode, val);
                gf_msg_debug(this->name, 0, "status : %" PRIu64, val);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, 0, "ctx update failed");
                    local->op_ret = -1;
                    local->op_errno = ENOMEM;
                    goto err;
                }
            }
        } else {
            gf_msg_debug(this->name, 0, "status not found in dict");
            local->op_ret = -1;
            local->op_errno = ENOMEM;
            goto err;
        }

        ret = dict_get_str_sizen(xdata, GF_CS_OBJECT_REMOTE, &filepath);
        if (filepath) {
            gf_msg_debug(this->name, 0, "filepath returned %s", filepath);
            local->remotepath = gf_strdup(filepath);
            if (!local->remotepath) {
                local->op_ret = -1;
                local->op_errno = ENOMEM;
                goto err;
            }
        } else {
            gf_msg_debug(this->name, 0, "NULL filepath");
        }

        ret = cs_update_xattrs(frame, xdata);
        if (ret)
            goto err;

        local->op_ret = 0;
        local->xattr_rsp = dict_ref(xdata);
        memcpy(&local->stbuf, stbuf, sizeof(struct iatt));
    }

    stub = local->stub;
    local->stub = NULL;
    call_resume(stub);

    return 0;
err:
    cs_inodelk_unlock(frame);

    cs_common_cbk(frame);

    return 0;
}

int
cs_do_stat_check(call_frame_t *main_frame)
{
    cs_local_t *local = NULL;
    xlator_t *this = NULL;
    int ret = 0;

    local = main_frame->local;
    this = main_frame->this;

    ret = dict_set_uint32(local->xattr_req, GF_CS_OBJECT_REPAIR, 256);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "dict_set failed");
        goto err;
    }

    cs_set_xattr_req(main_frame);

    if (local->fd) {
        STACK_WIND(main_frame, cs_stat_check_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->fstat, local->fd, local->xattr_req);
    } else {
        STACK_WIND(main_frame, cs_stat_check_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->stat, &local->loc,
                   local->xattr_req);
    }

    return 0;

err:
    cs_inodelk_unlock(main_frame);

    cs_common_cbk(main_frame);

    return 0;
}

void
cs_common_cbk(call_frame_t *frame)
{
    glusterfs_fop_t fop = -1;
    cs_local_t *local = NULL;

    local = frame->local;

    fop = local->fop;

    /*Note: Only the failure case needs to be handled here. Since for
     * successful stat check the fop will resume anyway. The unwind can
     * happen from the fop_cbk and each cbk can unlock the inodelk in case
     * a lock was taken before. The lock status can be stored in frame */

    /* for failure case  */

    /*TODO: add other fops*/
    switch (fop) {
        case GF_FOP_WRITE:
            CS_STACK_UNWIND(writev, frame, local->op_ret, local->op_errno, NULL,
                            NULL, NULL);
            break;

        case GF_FOP_SETXATTR:
            CS_STACK_UNWIND(setxattr, frame, local->op_ret, local->op_errno,
                            NULL);
            break;
        case GF_FOP_READ:
            CS_STACK_UNWIND(readv, frame, local->op_ret, local->op_errno, NULL,
                            0, NULL, NULL, NULL);
            break;
        case GF_FOP_FTRUNCATE:
            CS_STACK_UNWIND(ftruncate, frame, local->op_ret, local->op_errno,
                            NULL, NULL, NULL);
            break;

        case GF_FOP_TRUNCATE:
            CS_STACK_UNWIND(truncate, frame, local->op_ret, local->op_errno,
                            NULL, NULL, NULL);
            break;
        default:
            break;
    }

    return;
}

int
cs_blocking_inodelk_cbk(call_frame_t *lock_frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    cs_local_t *main_local = NULL;
    call_frame_t *main_frame = NULL;
    cs_local_t *lock_local = NULL;

    lock_local = lock_frame->local;

    main_frame = lock_local->main_frame;
    main_local = main_frame->local;

    if (op_ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "inodelk failed");
        main_local->op_errno = op_errno;
        main_local->op_ret = op_ret;
        goto err;
    }

    main_local->locked = _gf_true;

    cs_lock_wipe(lock_frame);

    cs_do_stat_check(main_frame);

    return 0;
err:
    cs_common_cbk(main_frame);

    cs_lock_wipe(lock_frame);

    return 0;
}

int
cs_build_loc(loc_t *loc, call_frame_t *frame)
{
    cs_local_t *local = NULL;
    int ret = -1;

    local = frame->local;

    if (local->fd) {
        loc->inode = inode_ref(local->fd->inode);
        if (loc->inode) {
            gf_uuid_copy(loc->gfid, loc->inode->gfid);
            ret = 0;
            goto out;
        } else {
            ret = -1;
            goto out;
        }
    } else {
        loc->inode = inode_ref(local->loc.inode);
        if (loc->inode) {
            gf_uuid_copy(loc->gfid, loc->inode->gfid);
            ret = 0;
            goto out;
        } else {
            ret = -1;
            goto out;
        }
    }
out:
    return ret;
}

int
cs_blocking_inodelk(call_frame_t *parent_frame)
{
    call_frame_t *lock_frame = NULL;
    cs_local_t *lock_local = NULL;
    xlator_t *this = NULL;
    struct gf_flock flock = {
        0,
    };
    int ret = 0;

    this = parent_frame->this;

    lock_frame = cs_lock_frame(parent_frame);
    if (!lock_frame) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "insuffcient memory");
        goto err;
    }

    lock_local = cs_local_init(this, lock_frame, NULL, NULL, 0);
    if (!lock_local) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "local init failed");
        goto err;
    }

    lock_local->main_frame = parent_frame;

    flock.l_type = F_WRLCK;

    ret = cs_build_loc(&lock_local->loc, parent_frame);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "build_loc failed");
        goto err;
    }

    STACK_WIND(lock_frame, cs_blocking_inodelk_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->inodelk, CS_LOCK_DOMAIN,
               &lock_local->loc, F_SETLKW, &flock, NULL);

    return 0;
err:
    if (lock_frame)
        cs_lock_wipe(lock_frame);

    return -1;
}

int
locate_and_execute(call_frame_t *frame)
{
    int ret = 0;

    ret = cs_blocking_inodelk(frame);

    if (ret)
        return -1;
    else
        return 0;
}

int32_t
cs_resume_truncate(call_frame_t *frame, xlator_t *this, loc_t *loc,
                   off_t offset, dict_t *xattr_req)
{
    cs_local_t *local = NULL;
    int ret = 0;

    local = frame->local;

    ret = cs_resume_postprocess(this, frame, loc->inode);
    if (ret) {
        goto unwind;
    }

    cs_inodelk_unlock(frame);

    STACK_WIND(frame, cs_truncate_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->truncate, loc, offset,
               local->xattr_req);

    return 0;

unwind:
    cs_inodelk_unlock(frame);

    cs_common_cbk(frame);

    return 0;
}

int32_t
cs_resume_setxattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                   dict_t *dict, int32_t flags, dict_t *xdata)
{
    cs_local_t *local = NULL;
    cs_inode_ctx_t *ctx = NULL;
    gf_cs_obj_state state = GF_CS_ERROR;

    local = frame->local;

    __cs_inode_ctx_get(this, loc->inode, &ctx);

    state = __cs_get_file_state(loc->inode, ctx);

    if (state == GF_CS_ERROR) {
        /* file is already remote */
        local->op_ret = -1;
        local->op_errno = EINVAL;
        gf_msg(this->name, GF_LOG_WARNING, 0, 0,
               "file %s , could not figure file state", loc->path);
        goto unwind;
    }

    if (state == GF_CS_REMOTE) {
        /* file is already remote */
        local->op_ret = -1;
        local->op_errno = EINVAL;
        gf_msg(this->name, GF_LOG_WARNING, 0, EINVAL,
               "file %s is already remote", loc->path);
        goto unwind;
    }

    if (state == GF_CS_DOWNLOADING) {
        gf_msg(this->name, GF_LOG_WARNING, 0, 0,
               " file is in downloading state.");
        local->op_ret = -1;
        local->op_errno = EINVAL;
        goto unwind;
    }

    STACK_WIND(frame, cs_setxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->setxattr, loc, dict, flags,
               local->xattr_req);

    return 0;
unwind:
    cs_inodelk_unlock(frame);

    cs_common_cbk(frame);

    return 0;
}

gf_cs_obj_state
__cs_get_file_state(inode_t *inode, cs_inode_ctx_t *ctx)
{
    gf_cs_obj_state state = -1;

    if (!ctx)
        return GF_CS_ERROR;

    LOCK(&inode->lock);
    {
        state = ctx->state;
    }
    UNLOCK(&inode->lock);

    return state;
}

void
__cs_inode_ctx_get(xlator_t *this, inode_t *inode, cs_inode_ctx_t **ctx)
{
    uint64_t ctxint = 0;
    int ret = 0;

    LOCK(&inode->lock);
    {
        ret = __inode_ctx_get(inode, this, &ctxint);
    }
    UNLOCK(&inode->lock);

    if (ret)
        *ctx = NULL;
    else
        *ctx = (cs_inode_ctx_t *)(uintptr_t)ctxint;

    return;
}

int
__cs_inode_ctx_update(xlator_t *this, inode_t *inode, uint64_t val)
{
    cs_inode_ctx_t *ctx = NULL;
    uint64_t ctxint = 0;
    int ret = 0;

    LOCK(&inode->lock);
    {
        ret = __inode_ctx_get(inode, this, &ctxint);
        if (ret) {
            ctx = GF_CALLOC(1, sizeof(*ctx), gf_cs_mt_cs_inode_ctx_t);
            if (!ctx) {
                gf_msg(this->name, GF_LOG_ERROR, 0, 0, "ctx allocation failed");
                ret = -1;
                goto out;
            }

            ctx->state = val;

            ctxint = (uint64_t)(uintptr_t)ctx;

            ret = __inode_ctx_set(inode, this, &ctxint);
            if (ret) {
                GF_FREE(ctx);
                goto out;
            }
        } else {
            ctx = (cs_inode_ctx_t *)(uintptr_t)ctxint;

            ctx->state = val;
        }
    }

out:
    UNLOCK(&inode->lock);

    return ret;
}

int
cs_inode_ctx_reset(xlator_t *this, inode_t *inode)
{
    cs_inode_ctx_t *ctx = NULL;
    uint64_t ctxint = 0;

    inode_ctx_del(inode, this, &ctxint);
    if (!ctxint) {
        return 0;
    }

    ctx = (cs_inode_ctx_t *)(uintptr_t)ctxint;

    GF_FREE(ctx);
    return 0;
}

int
cs_resume_postprocess(xlator_t *this, call_frame_t *frame, inode_t *inode)
{
    cs_local_t *local = NULL;
    gf_cs_obj_state state = -1;
    cs_inode_ctx_t *ctx = NULL;
    int ret = 0;

    local = frame->local;
    if (!local) {
        ret = -1;
        goto out;
    }

    __cs_inode_ctx_get(this, inode, &ctx);

    state = __cs_get_file_state(inode, ctx);
    if (state == GF_CS_ERROR) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "status is GF_CS_ERROR."
               " Aborting write");
        local->op_ret = -1;
        local->op_errno = EREMOTE;
        ret = -1;
        goto out;
    }

    if (state == GF_CS_REMOTE || state == GF_CS_DOWNLOADING) {
        gf_msg_debug(this->name, 0, "status is %d", state);
        ret = cs_download(frame);
        if (ret == 0) {
            gf_msg_debug(this->name, 0, "Winding for Final Write");
        } else {
            gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                   " download failed, unwinding writev");
            local->op_ret = -1;
            local->op_errno = EREMOTE;
            ret = -1;
        }
    }
out:
    return ret;
}

int32_t
cs_fdctx_to_dict(xlator_t *this, fd_t *fd, dict_t *dict)
{
    return 0;
}

int32_t
cs_inode(xlator_t *this)
{
    return 0;
}

int32_t
cs_inode_to_dict(xlator_t *this, dict_t *dict)
{
    return 0;
}

int32_t
cs_history(xlator_t *this)
{
    return 0;
}

int32_t
cs_fd(xlator_t *this)
{
    return 0;
}

int32_t
cs_fd_to_dict(xlator_t *this, dict_t *dict)
{
    return 0;
}

int32_t
cs_fdctx(xlator_t *this, fd_t *fd)
{
    return 0;
}

int32_t
cs_inodectx(xlator_t *this, inode_t *ino)
{
    return 0;
}

int32_t
cs_inodectx_to_dict(xlator_t *this, inode_t *ino, dict_t *dict)
{
    return 0;
}

int32_t
cs_priv_to_dict(xlator_t *this, dict_t *dict, char *brickname)
{
    return 0;
}

int32_t
cs_priv(xlator_t *this)
{
    return 0;
}

int
cs_notify(xlator_t *this, int event, void *data, ...)
{
    return default_notify(this, event, data);
}

struct xlator_fops cs_fops = {
    .stat = cs_stat,
    .readdirp = cs_readdirp,
    .truncate = cs_truncate,
    .seek = cs_seek,
    .statfs = cs_statfs,
    .fallocate = cs_fallocate,
    .discard = cs_discard,
    .getxattr = cs_getxattr,
    .writev = cs_writev,
    .setxattr = cs_setxattr,
    .fgetxattr = cs_fgetxattr,
    .lookup = cs_lookup,
    .fsetxattr = cs_fsetxattr,
    .readv = cs_readv,
    .ftruncate = cs_ftruncate,
    .rchecksum = cs_rchecksum,
    .unlink = cs_unlink,
    .open = cs_open,
    .fstat = cs_fstat,
    .zerofill = cs_zerofill,
};

struct xlator_cbks cs_cbks = {
    .forget = cs_forget,
};

struct xlator_dumpops cs_dumpops = {
    .fdctx_to_dict = cs_fdctx_to_dict,
    .inode = cs_inode,
    .inode_to_dict = cs_inode_to_dict,
    .history = cs_history,
    .fd = cs_fd,
    .fd_to_dict = cs_fd_to_dict,
    .fdctx = cs_fdctx,
    .inodectx = cs_inodectx,
    .inodectx_to_dict = cs_inodectx_to_dict,
    .priv_to_dict = cs_priv_to_dict,
    .priv = cs_priv,
};

struct volume_options cs_options[] = {
    {.key = {"cloudsync-storetype"},
     .type = GF_OPTION_TYPE_STR,
     .description = "Defines which remote store is enabled"},
    {.key = {"cloudsync-remote-read"},
     .type = GF_OPTION_TYPE_BOOL,
     .description = "Defines a remote read fop when on"},
    {.key = {"cloudsync-store-id"},
     .type = GF_OPTION_TYPE_STR,
     .description = "Defines a volume wide store id"},
    {.key = {"cloudsync-product-id"},
     .type = GF_OPTION_TYPE_STR,
     .description = "Defines a volume wide product id"},
    {.key = {NULL}},
};

xlator_api_t xlator_api = {
    .init = cs_init,
    .fini = cs_fini,
    .notify = cs_notify,
    .reconfigure = cs_reconfigure,
    .mem_acct_init = cs_mem_acct_init,
    .dumpops = &cs_dumpops,
    .fops = &cs_fops,
    .cbks = &cs_cbks,
    .options = cs_options,
    .identifier = "cloudsync",
    .category = GF_TECH_PREVIEW,
};
