/*
 *   Copyright (c) 2021 Pavilion Data Systems, Inc. <http://www.pavilion.io>
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
#include <glusterfs/syscall.h>
#include <glusterfs/compat-errno.h>
#include "tier.h"
#include <glusterfs/call-stub.h>
#include "tier-autogen-fops.h"

#include <string.h>
#include <dlfcn.h>

void
tier_local_wipe(xlator_t *this, tier_local_t *local)
{
    if (!local)
        return;

    loc_wipe(&local->loc);

    if (local->fd) {
        fd_unref(local->fd);
        local->fd = NULL;
    }

    if (local->stub) {
        call_stub_destroy(local->stub);
        local->stub = NULL;
    }

    if (local->remotepath)
        GF_FREE(local->remotepath);

    if (local->inode)
        inode_unref(local->inode);

    GF_FREE(local);
}

/* fremovexattr() to remove all traces of a file's remote status */
static int
tier_clear_local_file_xattr(xlator_t *this, inode_t *inode)
{
    int ret = -1;
    loc_t tmp_loc = {
        0,
    };
    dict_t *tmp_xdata = dict_new();
    if (!tmp_xdata) {
        goto err;
    }
    ret = dict_set_sizen_str_sizen(tmp_xdata, GF_CS_OBJECT_REMOTE, "0");
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
               "%s: dict set of remote key failed", uuid_utoa(inode->gfid));
    }

    ret = dict_set_sizen_str_sizen(tmp_xdata, GF_CS_OBJECT_SIZE, "0");
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
               "%s: dict set of object size key failed",
               uuid_utoa(inode->gfid));
    }

    ret = dict_set_sizen_str_sizen(tmp_xdata, GF_CS_NUM_BLOCKS, "0");
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
               "%s: dict set of num_blocks key failed", uuid_utoa(inode->gfid));
    }

    uuid_copy(tmp_loc.gfid, inode->gfid);
    tmp_loc.inode = inode_ref(inode);
    ret = syncop_removexattr(this, &tmp_loc, "", tmp_xdata, NULL);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_DEBUG, -ret, 0,
               "%s: failed to mark file local (after truncate) with bulk xattr "
               "removal. Retrying with only one key(%s)",
               uuid_utoa(inode->gfid), GF_CS_OBJECT_REMOTE);

        ret = syncop_removexattr(this, &tmp_loc, GF_CS_OBJECT_REMOTE, tmp_xdata,
                                 NULL);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_ERROR, -ret, 0,
                   "%s: failed to mark the file as local (after truncate).",
                   uuid_utoa(inode->gfid));
        }
    } else {
        gf_msg(this->name, GF_LOG_DEBUG, 0, 0, "%s: marked file as local.",
               uuid_utoa(inode->gfid));
    }

    dict_unref(tmp_xdata);
    inode_unref(inode);

err:
    return ret;
}

void
tier_cleanup_private(tier_private_t *priv)
{
    if (priv) {
        if (priv->stores) {
            if (priv->stores->fini) {
                priv->stores->fini(priv->stores->config);
            }
            if (priv->stores->handle) {
                dlclose(priv->stores->handle);
            }
            GF_FREE(priv->stores);
            priv->stores = NULL;
        }

        GF_FREE(priv->tierdir);
        priv->tierdir = NULL;
    }

    return;
}

void
__tier_inode_ctx_get(xlator_t *this, inode_t *inode, tier_inode_ctx_t **ctx)
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
        *ctx = (tier_inode_ctx_t *)(uintptr_t)ctxint;

    return;
}

int
__tier_inode_ctx_update(xlator_t *this, inode_t *inode, uint64_t val,
                        uint64_t size)
{
    tier_inode_ctx_t *ctx = NULL;
    uint64_t ctxint = 0;
    int ret = 0;

    LOCK(&inode->lock);
    {
        ret = __inode_ctx_get(inode, this, &ctxint);
        if (!ctxint) {
            ctx = GF_CALLOC(1, sizeof(*ctx), gf_tier_mt_inode_ctx_t);
            if (!ctx) {
                gf_msg(this->name, GF_LOG_ERROR, ENOMEM, 0,
                       "ctx allocation failed");
                ret = -1;
                goto out;
            }

            GF_ATOMIC_INIT(ctx->readcnt, 0);
            ctx->state = val;
            ctx->ia_size = size;
            ctxint = (uint64_t)(uintptr_t)ctx;
            ret = __inode_ctx_set(inode, this, &ctxint);
            if (ret) {
                GF_FREE(ctx);
                goto out;
            }
        } else {
            ctx = (tier_inode_ctx_t *)(uintptr_t)ctxint;

            if (val)
                ctx->state = val;
            if (size)
                ctx->ia_size = size;
        }
    }

out:
    UNLOCK(&inode->lock);

    return ret;
}

int
__tier_inode_ctx_update_path(xlator_t *this, inode_t *inode, char *path)
{
    tier_inode_ctx_t *ctx = NULL;
    uint64_t ctxint = 0;
    int ret = 0;

    if (!path)
        return 0;

    LOCK(&inode->lock);
    {
        ret = __inode_ctx_get(inode, this, &ctxint);
        if (!ctxint) {
            ctx = GF_CALLOC(1, sizeof(*ctx), gf_tier_mt_inode_ctx_t);
            if (!ctx) {
                gf_msg(this->name, GF_LOG_ERROR, ENOMEM, 0,
                       "ctx allocation failed");
                ret = -1;
                goto out;
            }

            ctx->remote_path = gf_strdup(path);
            if (!ctx->remote_path) {
                GF_FREE(ctx);
                goto out;
            }

            ctxint = (uint64_t)(uintptr_t)ctx;

            ret = __inode_ctx_set(inode, this, &ctxint);
            if (ret) {
                GF_FREE(ctx->remote_path);
                GF_FREE(ctx);
                goto out;
            }
            goto out;
        }
        ctx = (tier_inode_ctx_t *)(uintptr_t)ctxint;

        if (ctx->remote_path) {
            GF_FREE(ctx->remote_path);
        }
        ctx->remote_path = gf_strdup(path);
    }

out:
    UNLOCK(&inode->lock);

    return ret;
}

tier_local_t *
tier_local_init(xlator_t *this, call_frame_t *frame, loc_t *loc, fd_t *fd,
                glusterfs_fop_t fop)
{
    tier_inode_ctx_t *ctx = NULL;
    tier_local_t *local = NULL;
    int ret = 0;

    local = GF_CALLOC(1, sizeof(tier_local_t), gf_common_mt_char);
    if (!local)
        goto out;

    if (loc) {
        ret = loc_copy(&local->loc, loc);
        if (ret)
            goto out;
        __tier_inode_ctx_get(this, loc->inode, &ctx);
        local->inode = inode_ref(loc->inode);
    }

    if (fd) {
        local->fd = fd_ref(fd);
        __tier_inode_ctx_get(this, fd->inode, &ctx);
        local->inode = inode_ref(fd->inode);
    }

    local->op_ret = -1;
    local->op_errno = EUCLEAN;
    local->fop = fop;
    frame->local = local;
    local->locked = _gf_false;
    local->call_cnt = 0;
    if (ctx) {
        local->ctx = ctx;
        local->state = ctx->state;
        local->remotepath = gf_strdup(ctx->remote_path);
        local->ia_size = ctx->ia_size;
    } else {
        /* For a newly created file, ctx will not be set, so set the state as
         * GF_TIER_LOCAL */
        local->state = GF_TIER_LOCAL;
    }
    ret = 0;
out:
    if (ret) {
        if (local) {
            if (local->inode)
                inode_unref(local->inode);
            GF_FREE(local);
        }
        local = NULL;
    }

    return local;
}

struct tier_plugin plugins[] = {
    {.name = "filesystem",
     .library = "filesystem.so",
     .description = "Filesystem as the remote store."},
    {.name = NULL},
};

int
locate_and_execute(call_frame_t *frame)
{
    tier_local_t *local = frame->local;
    if (local) {
        call_stub_t *stub = local->stub;
        local->stub = NULL;
        call_resume(stub);
        return 0;
    }
    return -1;
}

int
tier_inode_ctx_reset(xlator_t *this, inode_t *inode)
{
    tier_private_t *priv = this->private;
    tier_inode_ctx_t *ctx = NULL;
    uint64_t ctxint = 0;
    uint64_t ctxint1 = 0;

    inode_ctx_del2(inode, this, &ctxint, &ctxint1);
    if (!ctxint) {
        goto ctx1;
    }

    ctx = (tier_inode_ctx_t *)(uintptr_t)ctxint;

    GF_FREE(ctx->remote_path);
    GF_FREE(ctx);

ctx1:
    if (!ctxint1) {
        return 0;
    }

    if (priv->stores && priv->stores->forget) {
        priv->stores->forget((void *)ctxint1, priv->stores->config);
    }
    return 0;
}

void
trigger_download_of_pending_files(xlator_t *this)
{
    tier_private_t *priv = this->private;
    struct dirent *dirent;

    /* If we are here, tierdir is set */
    DIR *dir = sys_opendir(priv->tierdir);
    if (!dir) {
        gf_msg(this->name, GF_LOG_ERROR, errno, 0,
               "failed to opendir on tierdir");
        return;
    }

    while ((dirent = sys_readdir(dir, NULL)) != NULL) {
        if ((strcmp(dirent->d_name, ".") == 0) ||
            (strcmp(dirent->d_name, "..") == 0))
            continue;

        gf_msg(this->name, GF_LOG_INFO, errno, 0,
               "found in-migration file: %s\n", dirent->d_name);

        /* Adding a logic to start migration here is a good idea. But the
           challenge is that, this is not always 'Ready' state considering
           initialization would be still not complete in whole graph when
           its called. It is now handled in specific 'read' or 'write' fop,
           so no data loss nor delay would happen. Benefit of not handling
           it here is that, when the process restarts, we would have a lot
           of load otherwise, now we can choose to decide that from
           application layer */
        /* For now, a log here is good enough to indicate the pending
           migration, which would help in debug if there are any issues.
        */
    }

    sys_closedir(dir);
    return;
}

int
tier_download_task(void *arg)
{
    call_frame_t *frame = NULL;
    xlator_t *this = NULL;
    tier_private_t *priv = NULL;
    int ret = -1;
    tier_local_t *local = NULL;

    frame = (call_frame_t *)arg;

    this = frame->this;

    priv = this->private;

    if (!priv->stores || !priv->stores->config) {
        gf_msg(this->name, GF_LOG_ERROR, ENOENT, 0,
               "No remote store plugins found");
        ret = -1;
        goto out;
    }

    /*this calling method is for per volume setting */
    ret = priv->stores->dlfop(frame, priv->stores->config);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "download failed: remotepath: %s", local->remotepath);

        ret = -1;
        goto out;
    }
    /* if the above is success, it clears the remote xattrs too */
    /* Local state should be cleared */
    tier_inode_ctx_reset(this, local->inode);

out:
    return ret;
}

int
tier_download(call_frame_t *frame)
{
    int ret = 0;
    tier_local_t *local = NULL;
    xlator_t *this = NULL;

    local = frame->local;
    this = frame->this;

    if (!local->remotepath) {
        ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, 0,
               "remote path not available. Check posix logs to resolve");
        goto out;
    }

    ret = tier_download_task((void *)frame);
out:
    return ret;
}

int
tier_resume_postprocess(xlator_t *this, call_frame_t *frame, inode_t *inode)
{
    tier_local_t *local = NULL;
    int ret = 0;

    local = frame->local;
    if (!local) {
        ret = -1;
        goto out;
    }

    if (local->state == GF_TIER_ERROR) {
        gf_msg(this->name, GF_LOG_ERROR, EREMOTE, 0,
               "status is GF_TIER_ERROR. Aborting write");
        local->op_ret = -1;
        local->op_errno = EREMOTE;
        ret = -1;
        goto out;
    }

    if (local->state == GF_TIER_REMOTE) {
        gf_msg_debug(this->name, 0, "status is %d", local->state);
        ret = tier_download(frame);
        if (ret == 0) {
            gf_msg_debug(this->name, 0, "Winding for Final Write");
        } else {
            gf_msg(this->name, GF_LOG_ERROR, EREMOTE, 0,
                   " download failed, unwinding writev");
            local->op_ret = -1;
            local->op_errno = EREMOTE;
            ret = -1;
        }
    }
out:
    return ret;
}

int
tier_init(xlator_t *this, dict_t *options)
{
    struct tier_remote_stores *stores = NULL;
    store_methods_t *store_methods = NULL;
    tier_private_t *priv = NULL;
    char *libpath = NULL;
    void *handle = NULL;
    char *temp_str = NULL;
    char *bmdir = NULL;
    char *libname = NULL;
    int index = 0;
    int ret = 0;

    priv = this->private;

    if (this->pass_through) {
        return 0;
    }

    GF_OPTION_RECONF("tier-stub-size", priv->stub_size, options, size, out);

    /* Previously initialized */
    if (priv->stores && priv->stores->config) {
        ret = priv->stores->reconfigure(this, options);
        if (ret) {
            gf_msg(this->name, GF_LOG_INFO, 0, 0, "plugin reconfigure failed");
        }
        return 0;
    }

    GF_OPTION_RECONF("tier-bitmap-dir", bmdir, options, path, out);
    if (!priv->tierdir) {
        if (!bmdir) {
            gf_msg(this->name, GF_LOG_ERROR, ENOENT, 0,
                   "'tier-bitmap-dir' option not provided. Add it in volume");
            ret = -1;
            goto out;
        }

        ret = sys_mkdir(bmdir, 755);
        if (ret && errno != EEXIST) {
            gf_msg(this->name, GF_LOG_ERROR, errno, 0, "mkdir(%s) failed: %s",
                   bmdir, strerror(errno));
            ret = -1;
            goto out;
        }

        priv->tierdir = gf_strdup(bmdir);
        if (!priv->tierdir) {
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, 0,
                   "'bitmap' dir copy failed (%s)", bmdir);
            ret = -1;
            goto out;
        }
    }

    if (dict_get_str(options, "tier-storetype", &temp_str) == 0) {
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

    if (!strcmp(temp_str, "filesystem")) {
        /* filesystem_plugin = true; */
        priv->remote_read = true;
    }

    ret = gf_asprintf(&libpath, "%s/%s", TIER_PLUGINDIR, libname);
    if (ret == -1) {
        goto out;
    }

    handle = dlopen(libpath, RTLD_NOW);
    if (!handle) {
        gf_msg(this->name, GF_LOG_WARNING, errno, 0,
               "could not load the required library. %s", dlerror());
        ret = -1;
        goto out;
    }

    stores = GF_CALLOC(1, sizeof(struct tier_remote_stores),
                       gf_tier_mt_remote_stores_t);
    if (!stores) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, 0,
               "Could not allocate memory for priv->stores");
        ret = -1;
        goto out;
    }

    (void)dlerror(); /* clear out previous error string */

    /* load library methods */
    store_methods = (store_methods_t *)dlsym(handle, "store_ops");
    if (!store_methods) {
        gf_msg(this->name, GF_LOG_ERROR, errno, 0, "null store_methods %s",
               dlerror());
        ret = -1;
        goto out;
    }

    (void)dlerror();

    if (priv->remote_read) {
        stores->rdfop = store_methods->fop_remote_read;
        if (!stores->rdfop) {
            gf_msg(this->name, GF_LOG_ERROR, ENOSYS, 0,
                   "failed to get read fop %s", dlerror());
            ret = -1;
            goto out;
        }
    }

    /* delete method in plugin is optional */
    stores->deletefop = store_methods->fop_remote_delete;
    if (!stores->deletefop) {
        gf_msg_debug(this->name, ENOENT, "failed to get delete fop %s (skip)",
                     dlerror());
    }

    /* readblkfop method in plugin is optional, instead would use download
     */
    stores->readblkfop = store_methods->fop_remote_readblk;
    if (!stores->readblkfop) {
        gf_msg_debug(this->name, ENOENT, "failed to get readblk fop %s (skip)",
                     dlerror());
    }

    stores->sync = store_methods->fop_sync;
    if (!stores->sync) {
        gf_msg_debug(this->name, ENOENT, "failed to get sync fop %s (skip)",
                     dlerror());
    }
    stores->rmdir = store_methods->fop_rmdir;
    if (!stores->rmdir) {
        gf_msg_debug(this->name, ENOENT, "failed to get rmdir fop %s (skip)",
                     dlerror());
    }

    stores->get_value = store_methods->get_value;
    if (!stores->get_value) {
        gf_msg_debug(this->name, ENOENT,
                     "failed to get 'get_value' fop %s (skip)", dlerror());
    }

    stores->forget = store_methods->forget;
    if (!stores->forget) {
        gf_msg_debug(this->name, ENOENT, "failed to get 'forget' fop %s (skip)",
                     dlerror());
    }

    stores->open = store_methods->open;
    if (!stores->open) {
        gf_msg_debug(this->name, ENOENT, "failed to get 'open' fop %s (skip)",
                     dlerror());
    }
    stores->release = store_methods->release;
    if (!stores->release) {
        gf_msg_debug(this->name, ENOENT, "failed to get 'forget' fop %s (skip)",
                     dlerror());
    }

    stores->dlfop = store_methods->fop_download;
    if (!stores->dlfop) {
        gf_msg(this->name, GF_LOG_ERROR, ENOSYS, 0,
               "failed to get download fop %s", dlerror());
        ret = -1;
        goto out;
    }

    (void)dlerror();
    stores->init = store_methods->fop_init;
    if (!stores->init) {
        gf_msg(this->name, GF_LOG_ERROR, ENOSYS, 0, "failed to get init fop %s",
               dlerror());
        ret = -1;
        goto out;
    }

    (void)dlerror();
    stores->reconfigure = store_methods->fop_reconfigure;
    if (!stores->reconfigure) {
        gf_msg(this->name, GF_LOG_ERROR, ENOSYS, 0,
               "failed to get reconfigure fop %s", dlerror());
        ret = -1;
        goto out;
    }

    stores->handle = handle;

    stores->config = (void *)((stores->init)(this, options));
    if (!stores->config) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "plugin init failed");
        ret = -1;
        goto out;
    }

    priv->stores = stores;
    stores = NULL;

    ret = 0;
    gf_msg(this->name, GF_LOG_INFO, 0, 0, "plugin (%s) init succeeded",
           libname);

out:
    if (ret == -1) {
        tier_cleanup_private(priv);

        if (handle) {
            dlclose(handle);
        }
        if (stores) {
            if (stores->fini) {
                stores->fini(stores->config);
            }
            GF_FREE(stores);
        }
    }

    GF_FREE(libpath);

    return ret;
}

int
init(xlator_t *this)
{
    tier_private_t *priv = NULL;
    int ret = 0;

    priv = GF_CALLOC(1, sizeof(*priv), gf_tier_mt_private_t);
    if (!priv) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, 0, "insufficient memory");
        goto out;
    }

    this->private = priv;

    GF_OPTION_INIT("tier-remote-read", priv->remote_read, bool, out);

    GF_OPTION_INIT("pass-through", this->pass_through, bool, out);

    if (this->pass_through) {
        gf_msg(this->name, GF_LOG_INFO, 0, 0, "tier module is not enabled.");
        goto out;
    }

    ret = tier_init(this, this->options);

out:
    if (ret) {
        GF_FREE(priv);
    }
    return ret;
}

int32_t
tier_release(xlator_t *this, fd_t *fd)
{
    uint64_t ctx = 0;
    tier_private_t *priv = this->private;

    (void)fd_ctx_del(fd, this, &ctx);
    if (ctx && priv->stores && priv->stores->release) {
        priv->stores->release(fd->inode, fd, priv->stores->config);
    }
    return 0;
}

int
tier_forget(xlator_t *this, inode_t *inode)
{
    uint64_t ctx_int0 = 0;
    uint64_t ctx_int1 = 0;
    tier_inode_ctx_t *ctx = NULL;
    tier_private_t *priv = this->private;

    inode_ctx_del2(inode, this, &ctx_int0, &ctx_int1);
    if (!ctx_int0)
        goto check_ctx1;

    ctx = (tier_inode_ctx_t *)(uintptr_t)ctx_int0;

    GF_FREE(ctx->remote_path);
    GF_FREE(ctx);

check_ctx1:
    if (ctx_int1 && priv->stores && priv->stores->forget) {
        priv->stores->forget((void *)ctx_int1, priv->stores->config);
    }

    return 0;
}

void
tier_fini(xlator_t *this)
{
    tier_private_t *priv = this->private;

    this->private = NULL;
    tier_cleanup_private(priv);
    GF_FREE(priv);
}

int
tier_reconfigure(xlator_t *this, dict_t *options)
{
    tier_private_t *priv = NULL;
    int ret = 0;

    priv = this->private;
    if (!priv) {
        ret = -1;
        goto out;
    }

    GF_OPTION_RECONF("tier-remote-read", priv->remote_read, options, bool, out);

    GF_OPTION_RECONF("pass-through", this->pass_through, options, bool, out);
    if (this->pass_through) {
        goto out;
    }

    ret = tier_init(this, options);
    /* all logging will happen in tier_init() itself */
out:
    return ret;
}

int32_t
tier_mem_acct_init(xlator_t *this)
{
    int ret = xlator_mem_acct_init(this, gf_tier_mt_end + 1);

    if (ret != 0) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, 0,
               "Memory accounting init failed");
        return ret;
    }

    return ret;
}

int32_t
tier_ftruncate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                   struct iatt *postbuf, dict_t *xdata)
{
    int ret = -1;
    tier_private_t *priv = this->private;
    tier_local_t *local = frame->local;
    if (op_ret >= 0 && local->state != GF_TIER_LOCAL) {
        __tier_inode_ctx_update(this, local->inode, 0, postbuf->ia_size);
        /* Send delete to plugin only if offset is 0 */
        if ((local->offset == 0) && priv->stores->deletefop &&
            priv->stores->config) {
            /* let delete/unlink be atomic call on remote */
            ret = priv->stores->deletefop(frame, 0, priv->stores->config);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                       "remote delete failed, remotepath: %s",
                       local->remotepath);
                op_errno = -ret;
                goto out;
            }

            tier_clear_local_file_xattr(this, local->inode);
            /* Local state is cleared */
            tier_inode_ctx_reset(this, local->inode);
        }
    }
out:
    TIER_STACK_UNWIND(ftruncate, frame, op_ret, op_errno, prebuf, postbuf,
                      xdata);
    return 0;
}

int32_t
tier_ftruncate(call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
               dict_t *xdata)
{
    tier_local_t *local = NULL;
    int32_t op_errno = ENOMEM;

    local = tier_local_init(this, frame, NULL, fd, GF_FOP_FTRUNCATE);
    if (!local)
        goto err;

    local->offset = offset;

    STACK_WIND(frame, tier_ftruncate_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->ftruncate, fd, offset, xdata);

    return 0;
err:
    TIER_STACK_UNWIND(ftruncate, frame, -1, op_errno, NULL, NULL, NULL);
    return 0;
}

int32_t
tier_truncate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{
    int ret = -1;
    tier_private_t *priv = this->private;
    tier_local_t *local = frame->local;
    if (op_ret >= 0 && local->state != GF_TIER_LOCAL) {
        __tier_inode_ctx_update(this, local->inode, 0, postbuf->ia_size);
        /* Send delete to plugin only if offset is 0 */
        if ((local->offset == 0) && priv->stores->deletefop &&
            priv->stores->config) {
            /* let delete/unlink be atomic call on remote */
            ret = priv->stores->deletefop(frame, 0, priv->stores->config);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                       "remote delete failed, remotepath: %s",
                       local->remotepath);
                op_errno = -ret;
                goto out;
            }

            tier_clear_local_file_xattr(this, local->inode);
            /* Local state is cleared */
            tier_inode_ctx_reset(this, local->inode);
        }
    }
out:
    TIER_STACK_UNWIND(truncate, frame, op_ret, op_errno, prebuf, postbuf,
                      xdata);
    return 0;
}

int32_t
tier_truncate(call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
              dict_t *xdata)
{
    tier_local_t *local = NULL;
    int32_t op_errno = ENOMEM;

    local = tier_local_init(this, frame, loc, NULL, GF_FOP_TRUNCATE);
    if (!local)
        goto err;

    local->offset = offset;

    STACK_WIND(frame, tier_truncate_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->truncate, loc, offset, xdata);

    return 0;
err:
    TIER_STACK_UNWIND(truncate, frame, -1, op_errno, NULL, NULL, NULL);
    return 0;
}

int32_t
tier_setxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    tier_local_t *local = frame->local;

    if (op_ret >= 0 && local) {
        uint64_t ia_size = 0;
        int ret = 0;
        if (xdata) {
            ret = dict_get_uint64(xdata, GF_CS_OBJECT_SIZE, &ia_size);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, ENODATA, 0,
                       "%s: failed to get remote file size", local->remotepath);
            }
        }

        ret = __tier_inode_ctx_update(this, local->inode, GF_TIER_REMOTE,
                                      ia_size);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
                   "%s: failed to update the inode ctx", local->remotepath);
        }

        ret = __tier_inode_ctx_update_path(this, local->inode,
                                           local->remotepath);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
                   "%s: failed to update the path in inode ctx",
                   local->remotepath);
        }
    }

    TIER_STACK_UNWIND(setxattr, frame, op_ret, op_errno, xdata);

    return 0;
}

int32_t
tier_setxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
              int32_t flags, dict_t *xdata)
{
    char *received_value = NULL;
    int ret;
    int32_t op_errno = EINVAL;
    tier_local_t *local = NULL;
    tier_private_t *priv = this->private;

    ret = dict_get_str_sizen(dict, TIER_PROMOTE_FILE, &received_value);
    if (received_value) {
        /* 'received_value' can be anything. */
        local = tier_local_init(this, frame, loc, NULL, GF_FOP_SETXATTR);
        if (!local)
            goto unwind;

        if (local->state == GF_TIER_LOCAL)
            goto wind;

        /* Call downloadfop */
        if (!priv->stores || !priv->stores->config) {
            op_errno = ENOTSUP;
            goto unwind;
        }
        /*this calling method is for per volume setting */
        ret = priv->stores->dlfop(frame, priv->stores->config);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                   "download failed: remotepath: %s", local->remotepath);
            ret = -1;
            op_errno = EREMOTE;
        }

        /* DECIDE: for future, make this as a trigger, so the response can go
         * back immediately to user, and download can happen in another thread
         */
        /* let xdata be null for now, as we don't have anything to send */
        TIER_STACK_UNWIND(setxattr, frame, ret, op_errno, NULL);
        return 0;
    }

    ret = dict_get_str_sizen(dict, MARK_REMOTE_KEY, &received_value);
    if (received_value) {
        char *tmp_str;
        char *mtime;
        char *path = NULL;

        /* 'received_value' to be mtime:path */
        local = tier_local_init(this, frame, loc, NULL, GF_FOP_SETXATTR);
        if (!local)
            goto unwind;

        gf_tier_obj_state state = local->state;

        if (state == GF_TIER_ERROR) {
            /* file is already remote */
            gf_msg(this->name, GF_LOG_WARNING, 0, 0,
                   "path(%s) , could not figure file state", loc->path);
            goto unwind;
        }

        if (state == GF_TIER_REMOTE) {
            /* file is already remote */
            op_errno = EREMOTE;
            gf_msg(this->name, GF_LOG_WARNING, EREMOTE, 0,
                   "file(%s) is already remote", loc->path);
            goto unwind;
        }

        if (!priv->stores || !priv->stores->config) {
            op_errno = ENOTSUP;
            gf_msg(this->name, GF_LOG_WARNING, ENOTSUP, 0,
                   "no plugins ready to accept. Enable all options or do "
                   "a volume restart");
            goto unwind;
        }

        /* Parse the received string, which is in the format of
         * ${mtime}:${remote_path} */
        tmp_str = strdup(received_value);

        mtime = strtok_r(tmp_str, ":", &path);

        dict_del_sizen(dict, MARK_REMOTE_KEY);

        ret = dict_set_dynstr_sizen(dict, GF_CS_OBJECT_UPLOAD_COMPLETE,
                                    gf_strdup(mtime));
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
                   "%s: mtime set failed", loc->path);
        }

        if (path && path[0] != '\0') {
            ret = dict_set_dynstr_sizen(dict, GF_CS_OBJECT_REMOTE,
                                        gf_strdup(path));
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
                       "%s: remote path set failed", loc->path);
            }
        } else {
            path = (char *)loc->path;
        }
        if (local->remotepath) {
            GF_FREE(local->remotepath);
        }
        local->remotepath = gf_strdup(path);
        free(tmp_str);
    }

wind:
    STACK_WIND(frame, tier_setxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->setxattr, loc, dict, flags, xdata);
    return 0;

unwind:
    TIER_STACK_UNWIND(setxattr, frame, -1, op_errno, NULL);
    return 0;
}

int32_t
tier_fsetxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    tier_local_t *local = frame->local;

    if ((op_ret >= 0) && local) {
        uint64_t ia_size = 0;
        int ret = 0;
        if (xdata) {
            ret = dict_get_uint64(xdata, GF_CS_OBJECT_SIZE, &ia_size);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, ENODATA, 0,
                       "%s: failed to get remote size", local->remotepath);
            }
        }

        ret = __tier_inode_ctx_update(this, local->inode, GF_TIER_REMOTE,
                                      ia_size);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
                   "%s: failed to set inode_ctx", local->remotepath);
        }
        ret = __tier_inode_ctx_update_path(this, local->inode,
                                           local->remotepath);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
                   "%s: failed to set path in inode_ctx", local->remotepath);
        }
    }
    TIER_STACK_UNWIND(fsetxattr, frame, op_ret, op_errno, xdata);

    return 0;
}

int32_t
tier_fsetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
               int32_t flags, dict_t *xdata)
{
    char *received_value = NULL;
    int ret;
    int32_t op_errno = EINVAL;
    tier_private_t *priv = this->private;

    ret = dict_get_str_sizen(dict, MARK_REMOTE_KEY, &received_value);
    if (received_value) {
        char *tmp_str;
        char *mtime;
        char *path = NULL;
        gf_tier_obj_state state = GF_TIER_ERROR;

        tier_local_t *local = NULL;

        local = tier_local_init(this, frame, NULL, fd, GF_FOP_FSETXATTR);
        if (!local)
            goto unwind;

        state = local->state;

        if (state == GF_TIER_ERROR) {
            /* file is already remote */
            gf_msg(this->name, GF_LOG_WARNING, 0, 0,
                   "fd(%p) , could not figure file state", fd);
            goto unwind;
        }

        if (state == GF_TIER_REMOTE) {
            /* file is already remote */
            op_errno = EREMOTE;
            gf_msg(this->name, GF_LOG_WARNING, EREMOTE, 0,
                   "fd(%p) is already remote", fd);
            goto unwind;
        }

        if (!priv->stores || !priv->stores->config) {
            op_errno = ENOTSUP;
            gf_msg(this->name, GF_LOG_WARNING, ENOTSUP, 0,
                   "no plugins ready to accept. Enable all options or do "
                   "a volume restart");
            goto unwind;
        }

        /* Parse the received string, which is in the format of
         * ${mtime}:${remote_path} */
        tmp_str = strdup(received_value);

        mtime = strtok_r(tmp_str, ":", &path);

        dict_del_sizen(dict, MARK_REMOTE_KEY);

        ret = dict_set_dynstrn(dict, GF_CS_OBJECT_UPLOAD_COMPLETE,
                               SLEN(GF_CS_OBJECT_UPLOAD_COMPLETE),
                               gf_strdup(mtime));
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
                   "%s: failed to set mtime details in dict", path);
        }
        if (path && path[0] != '\0') {
            ret = dict_set_dynstrn(dict, GF_CS_OBJECT_REMOTE,
                                   SLEN(GF_CS_OBJECT_REMOTE), gf_strdup(path));
            if (ret < 0) {
                gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
                       "%s: failed to set remote path details in dict", path);
            }
            if (local->remotepath) {
                GF_FREE(local->remotepath);
            }
            local->remotepath = gf_strdup(path);
        } else {
            op_errno = ENOTSUP;
            /* If there is no proper path provided, it is not possible to track
             * the remote file. */
            free(tmp_str);
            goto unwind;
        }

        free(tmp_str);
    }

    STACK_WIND(frame, tier_fsetxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fsetxattr, fd, dict, flags, xdata);
    return 0;

unwind:
    TIER_STACK_UNWIND(fsetxattr, frame, -1, op_errno, NULL);
    return 0;
}

int32_t
tier_unlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
    tier_private_t *priv = this->private;
    tier_local_t *local = frame->local;
    uint32_t nlink = 0;
    int ret = 0;

    if ((op_ret >= 0) && (local->state != GF_TIER_LOCAL)) {
        ret = dict_get_uint32(xdata, GF_RESPONSE_LINK_COUNT_XDATA, &nlink);
        /* TODO: add more logs on failure */
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                   "failed to get nlink from dict, remotepath: %s",
                   local->remotepath);
        } else {
            gf_msg(this->name, GF_LOG_DEBUG, 0, 0, "nlink = %u, remotepath: %s",
                   nlink, local->remotepath);
        }
        /* Delete remote file only if the file is last one */
        if ((nlink == 1) && (priv->stores && priv->stores->config)) {
            /* let delete/unlink be atomic call on remote */
            ret = priv->stores->deletefop(frame, local->flags,
                                          priv->stores->config);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, 0,
                       "delete failed, remotepath: %s", local->remotepath);
                op_errno = -ret;
                op_ret = -1;
            }
        }
    }
    TIER_STACK_UNWIND(unlink, frame, op_ret, op_errno, preparent, postparent,
                      xdata);
    return 0;
}

int32_t
tier_unlink(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
            dict_t *xattr_req)
{
    tier_local_t *local = NULL;
    tier_private_t *priv = this->private;
    int32_t op_errno = ENOMEM;
    dict_t *xdata = NULL;
    int ret = 0;

    local = tier_local_init(this, frame, loc, NULL, GF_FOP_UNLINK);
    if (!local)
        goto err;
    if ((local->state != GF_TIER_LOCAL) && priv->stores &&
        priv->stores->deletefop) {
        local->flags = flags;
        xdata = xattr_req ? dict_ref(xattr_req) : dict_new();
        ret = dict_set_uint32(xdata, GF_REQUEST_LINK_COUNT_XDATA, 0);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
                   "%s: failed to set link count in xdata", loc->path);
        }

    } else {
        xdata = xattr_req ? dict_ref(xattr_req) : NULL;
    }

    /* unlink will continue after remote delete */
    STACK_WIND(frame, tier_unlink_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->unlink, loc, flags, xdata);

    if (xdata)
        dict_unref(xdata);
    return 0;

err:
    TIER_STACK_UNWIND(unlink, frame, -1, op_errno, NULL, NULL, NULL);
    return 0;
}

int32_t
tier_readv_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iovec *vector,
               int32_t count, struct iatt *stbuf, struct iobref *iobref,
               dict_t *xdata)
{
    TIER_STACK_UNWIND(readv, frame, op_ret, op_errno, vector, count, stbuf,
                      iobref, xdata);

    return 0;
}

int
tier_serve_readv(call_frame_t *frame, fd_t *fd, size_t size, off_t offset,
                 uint32_t flags, dict_t *xdata)
{
    int ret = -1;
    int op_errno = EINVAL;
    xlator_t *this = frame->this;
    tier_private_t *priv = this->private;
    tier_local_t *local = frame->local;
    struct iobuf *iobuf = NULL;

    if (!local->remotepath) {
        ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, 0,
               "remote path not available. Check posix logs to resolve");
        goto out;
    }

    if (!priv->stores) {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, 0,
               "No remote store plugins found %p %p", this, priv);
        ret = -1;
        goto out;
    }

    local->size = size;
    local->offset = offset;
    local->flags = flags;

    if (priv->stores && priv->stores->config && priv->stores->rdfop) {
        iobuf = iobuf_get_page_aligned(this->ctx->iobuf_pool, size, ALIGN_SIZE);
        if (!iobuf) {
            op_errno = ENOMEM;
            goto out;
        }

        size_t data_size = 0;

        /*this calling method is for per volume setting */
        gf_tier_obj_state state = priv->stores->rdfop(
            frame, iobuf->ptr, &data_size, priv->stores->config);
        if (state == GF_TIER_ERROR) {
            op_errno = EREMOTE;
            iobuf_unref(iobuf);
            gf_msg(this->name, GF_LOG_ERROR, EREMOTE, 0,
                   "read failed, remotepath: %s", local->remotepath);
            goto out;
        } else if (state == GF_TIER_REMOTE) {
            /* Data successfully returned from the remote path. Return the data
             */
            struct iovec vec = {.iov_base = iobuf->ptr, .iov_len = data_size};
            struct iatt stbuf = {
                0,
            };
            struct iobref *iobref = iobref_new();
            if (!iobref) {
                op_errno = ENOMEM;
                iobuf_unref(iobuf);
                goto out;
            }
            iobref_add(iobref, iobuf);

            GF_ATOMIC_INC(local->ctx->readcnt);
            TIER_STACK_UNWIND(readv, frame, data_size, 0, &vec, 1, &stbuf,
                              iobref, xdata);

            iobuf_unref(iobuf);
            iobref_unref(iobref);
            /* Re look at where the data_buf gets free */
            return 0;
        } /* GF_TIER_LOCAL */
        iobuf_unref(iobuf);
    } else {
        ret = tier_download(frame);
        if (ret < 0) {
            op_errno = EREMOTE;
            gf_msg(this->name, GF_LOG_WARNING, EREMOTE, 0,
                   "remote download fail, path : %s", local->remotepath);
            goto out;
        }
    }
    STACK_WIND(frame, tier_readv_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->readv, fd, size, offset, flags, xdata);
    return 0;
out:
    TIER_STACK_UNWIND(readv, frame, -1, op_errno, NULL, 0, NULL, NULL, NULL);

    return 0;
}

int32_t
tier_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
           off_t offset, uint32_t flags, dict_t *xdata)
{
    int ret = 0;
    int op_errno = EINVAL;
    tier_local_t *local = NULL;
    tier_private_t *priv = this->private;

    local = tier_local_init(this, frame, NULL, fd, GF_FOP_READ);
    if (!local) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, 0, "local init failed");
        op_errno = ENOMEM;
        goto err;
    }

    if (local->state == GF_TIER_LOCAL) {
        STACK_WIND(frame, tier_readv_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->readv, fd, size, offset, flags,
                   xdata);
        return 0;
    }

    /* There is enough data left on hot tier, even though file is remote. */
    if ((offset + size) < priv->stub_size) {
        STACK_WIND(frame, tier_readv_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->readv, fd, size, offset, flags,
                   xdata);
        return 0;
    }

    ret = tier_serve_readv(frame, fd, size, offset, flags, xdata);
    /* Failed to submit the remote readv fop to plugin */
    if (ret) {
        op_errno = EREMOTE;
        goto err;
    }

    return 0;

err:
    TIER_STACK_UNWIND(readv, frame, -1, op_errno, NULL, 0, NULL, NULL, NULL);

    return 0;
}

int32_t
tier_writev_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                struct iatt *postbuf, dict_t *xdata)
{
    TIER_STACK_UNWIND(writev, frame, op_ret, op_errno, prebuf, postbuf, xdata);

    return 0;
}

int
tier_perform_writev(call_frame_t *frame, fd_t *fd, struct iovec *vector,
                    int32_t count, off_t offset, uint32_t flags,
                    struct iobref *iobref, dict_t *xdata)
{
    xlator_t *this = NULL;
    tier_private_t *priv = NULL;
    int ret = -1;
    int op_errno = EINVAL;
    tier_local_t *local = NULL;

    local = frame->local;
    this = frame->this;
    priv = this->private;

    if (!local->remotepath) {
        ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "remote path not available. Check posix logs to resolve %p %d",
               local->inode, local->state);
        goto out;
    }

    if (!priv->stores) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "No remote store plugins found");
        ret = -1;
        goto out;
    }

    local->size = iov_length(vector, count);
    local->offset = offset;
    local->flags = flags;

    if (priv->stores && priv->stores->config && priv->stores->readblkfop) {
        gf_tier_obj_state state = priv->stores->readblkfop(
            frame, priv->stores->config);
        if (state != GF_TIER_LOCAL) {
            op_errno = EREMOTE;
            gf_msg(this->name, GF_LOG_INFO, 0, 0,
                   "remote read update fail, path : %s", local->remotepath);
            goto out;
        }
    } else {
        ret = tier_download(frame);
        if (ret < 0) {
            op_errno = EREMOTE;
            gf_msg(this->name, GF_LOG_INFO, 0, 0,
                   "remote download update fail, path : %s", local->remotepath);
            goto out;
        }
    }

    STACK_WIND(frame, tier_writev_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->writev, fd, vector, count, offset,
               flags, iobref, xdata);

    return 0;

out:
    TIER_STACK_UNWIND(writev, frame, -1, op_errno, NULL, NULL, NULL);
    return 0;
}

int32_t
tier_writev(call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
            int32_t count, off_t offset, uint32_t flags, struct iobref *iobref,
            dict_t *xdata)
{
    int op_errno = EINVAL;
    tier_local_t *local = NULL;
    int ret = 0;

    local = tier_local_init(this, frame, NULL, fd, GF_FOP_WRITE);
    if (!local) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "local init failed");
        op_errno = ENOMEM;
        goto err;
    }

    if (local->state == GF_TIER_LOCAL) {
        STACK_WIND(frame, tier_writev_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->writev, fd, vector, count, offset,
                   flags, iobref, xdata);
        return 0;
    }

    ret = tier_perform_writev(frame, fd, vector, count, offset, flags, iobref,
                              xdata);
    /* Failed to submit the remote readv fop to plugin */
    if (ret) {
        op_errno = EREMOTE;
        goto err;
    }

    return 0;

err:
    TIER_STACK_UNWIND(writev, frame, -1, op_errno, NULL, NULL, NULL);

    return 0;
}

void
tier_common_cbk(call_frame_t *frame)
{
    glusterfs_fop_t fop = -1;
    tier_local_t *local = NULL;

    local = frame->local;

    fop = local->fop;

    /* Note: Only the failure case needs to be handled here. Since for
     * successful stat check the fop will resume anyway. The unwind can
     * happen from the fop_cbk and each cbk can unlock the inodelk in case
     * a lock was taken before. The lock status can be stored in frame */
    /* for failure case  */

    switch (fop) {
        case GF_FOP_RCHECKSUM:
            TIER_STACK_UNWIND(rchecksum, frame, local->op_ret, local->op_errno,
                              0, NULL, NULL);
            break;

        case GF_FOP_FALLOCATE:
            TIER_STACK_UNWIND(fallocate, frame, local->op_ret, local->op_errno,
                              NULL, NULL, NULL);
            break;
        case GF_FOP_DISCARD:
            TIER_STACK_UNWIND(discard, frame, local->op_ret, local->op_errno,
                              NULL, NULL, NULL);
            break;
        case GF_FOP_ZEROFILL:
            TIER_STACK_UNWIND(zerofill, frame, local->op_ret, local->op_errno,
                              NULL, NULL, NULL);
            break;

        default:
            break;
    }

    return;
}

int32_t
tier_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, inode_t *inode,
                struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
    if (op_ret >= 0) {
        char *filepath = NULL;
        int ret = dict_get_str_sizen(xdata, GF_CS_OBJECT_REMOTE, &filepath);
        if (!ret && filepath) {
            __tier_inode_ctx_update(this, inode, GF_TIER_REMOTE, buf->ia_size);
            __tier_inode_ctx_update_path(this, inode, filepath);
        }
    }

    TIER_STACK_UNWIND(lookup, frame, op_ret, op_errno, inode, buf, xdata,
                      postparent);

    return 0;
}

int32_t
tier_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    int op_errno = EINVAL;
    int ret = 0;
    bool need_unref = false;

    if (loc->inode->ia_type == IA_IFDIR)
        goto wind;

    xdata = xdata ? dict_ref(xdata) : dict_new();
    if (!xdata) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "insufficient memory");
        op_errno = ENOMEM;
        goto err;
    }

    need_unref = true;
    ret = dict_set_str_sizen(xdata, GF_CS_OBJECT_REMOTE, "0");
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "dict_set failed key: %s",
               GF_CS_OBJECT_REMOTE);
        goto err;
    }

wind:
    STACK_WIND(frame, tier_lookup_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->lookup, loc, xdata);

    if (need_unref)
        dict_unref(xdata);

    return 0;
err:
    TIER_STACK_UNWIND(lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);
    if (need_unref)
        dict_unref(xdata);
    return 0;
}

static int
tier_fgetxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int op_ret, int op_errno, dict_t *dict, dict_t *xdata)
{
    /* we shouldn't pass the information of tier xattr keys to above layers */
    /* for example, rebalance would move all xattrs of the file to another
     * brick, and delete all here */
    dict_del_sizen(dict, GF_CS_OBJECT_REMOTE);
    dict_del_sizen(dict, GF_CS_NUM_BLOCKS);
    dict_del_sizen(dict, GF_CS_OBJECT_SIZE);
    STACK_UNWIND_STRICT(fgetxattr, frame, op_ret, op_errno, dict, xdata);
    return 0;
}

static int
tier_getxattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                  int op_errno, dict_t *dict, dict_t *xdata)
{
    /* we shouldn't pass the information of tier xattr keys to above layers */
    /* for example, rebalance would move all xattrs of the file to another
     * brick, and delete all here */
    dict_del_sizen(dict, GF_CS_OBJECT_REMOTE);
    dict_del_sizen(dict, GF_CS_NUM_BLOCKS);
    dict_del_sizen(dict, GF_CS_OBJECT_SIZE);
    STACK_UNWIND_STRICT(getxattr, frame, op_ret, op_errno, dict, xdata);
    return 0;
}

int32_t
tier_fgetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name,
               dict_t *xdata)
{
    int op_ret = -1;
    int op_errno = EINVAL;
    dict_t *dict = NULL;
    if (name && !strncmp(name, TIER_GET_READ_CNT, SLEN(TIER_GET_READ_CNT))) {
        tier_inode_ctx_t *ctx = NULL;

        __tier_inode_ctx_get(this, fd->inode, &ctx);
        if (!ctx) {
            goto unwind;
        }

        dict = dict_new();
        op_ret = dict_set_uint64(dict, TIER_GET_READ_CNT,
                                 GF_ATOMIC_GET(ctx->readcnt));

        if (op_ret < 0) {
            gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
                   "%s: failed to set read-count in dict",
                   uuid_utoa(fd->inode->gfid));
        }

    unwind:
        TIER_STACK_UNWIND(fgetxattr, frame, op_ret, op_errno, dict, xdata);
        if (dict)
            dict_unref(dict);
        return 0;
    }

    if (name && !strncmp(name, TIER_MIGRATED_BLOCK_CNT,
                         SLEN(TIER_MIGRATED_BLOCK_CNT))) {
        tier_private_t *priv = this->private;

        if (priv->stores && priv->stores->get_value) {
            dict = dict_new();

            op_ret = priv->stores->get_value(fd->inode, priv->stores->config,
                                             GF_TIER_WRITE_COUNT, dict);
            if (op_ret < 0) {
                op_errno = -op_ret;
                op_ret = -1;
            }
        }
        TIER_STACK_UNWIND(fgetxattr, frame, op_ret, op_errno, dict, xdata);
        if (dict)
            dict_unref(dict);
        return 0;
    }

    STACK_WIND(frame, tier_fgetxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fgetxattr, fd, name, xdata);
    return 0;
}

int32_t
tier_getxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, const char *name,
              dict_t *xdata)
{
    int op_ret = -1;
    int op_errno = EINVAL;
    dict_t *dict = NULL;
    if (name && !strcmp(name, TIER_GET_READ_CNT)) {
        tier_inode_ctx_t *ctx = NULL;

        __tier_inode_ctx_get(this, loc->inode, &ctx);
        if (!ctx) {
            goto unwind;
        }

        dict = dict_new();
        op_ret = dict_set_uint64(dict, TIER_GET_READ_CNT,
                                 GF_ATOMIC_GET(ctx->readcnt));

        if (op_ret < 0) {
            gf_msg(this->name, GF_LOG_WARNING, EINVAL, 0,
                   "%s: failed to set read-count in dict",
                   loc->path ? loc->path : uuid_utoa(loc->inode->gfid));
        }
    unwind:
        TIER_STACK_UNWIND(getxattr, frame, op_ret, op_errno, dict, xdata);
        if (dict)
            dict_unref(dict);
        return 0;
    }

    if (name && !strncmp(name, TIER_MIGRATED_BLOCK_CNT,
                         SLEN(TIER_MIGRATED_BLOCK_CNT))) {
        tier_private_t *priv = this->private;

        if (priv->stores && priv->stores->get_value) {
            dict = dict_new();
            op_ret = priv->stores->get_value(loc->inode, priv->stores->config,
                                             GF_TIER_WRITE_COUNT, dict);
            if (op_ret < 0) {
                op_errno = -op_ret;
                op_ret = -1;
            }
        }
        TIER_STACK_UNWIND(getxattr, frame, op_ret, op_errno, dict, xdata);
        if (dict)
            dict_unref(dict);
        return 0;
    }

    STACK_WIND(frame, tier_getxattr_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->getxattr, loc, name, xdata);
    return 0;
}

int
tier_flush_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    TIER_STACK_UNWIND(flush, frame, op_ret, op_errno, xdata);
    return 0;
}

/* Lets handle flush similar to fsync */
int
tier_flush(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    int ret = 0;
    int op_errno = EINVAL;
    tier_local_t *local = NULL;
    tier_private_t *priv = this->private;

    local = tier_local_init(this, frame, NULL, fd, GF_FOP_FLUSH);
    if (!local) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "local init failed");
        op_errno = ENOMEM;
        goto err;
    }

    if (local->state == GF_TIER_LOCAL) {
        STACK_WIND(frame, tier_flush_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->flush, fd, xdata);
        return 0;
    }

    if (!local->remotepath) {
        ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "remote path not available. Check posix logs to resolve %p %d",
               local->inode, local->state);
        goto err;
    }

    if (!priv->stores || !priv->stores->config) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "No remote store plugins found");
        ret = -1;
        goto err;
    }

    if (priv->stores->sync) {
        ret = priv->stores->sync(frame, priv->stores->config, fd);
        if (ret) {
            op_errno = EREMOTE;
            gf_msg(this->name, GF_LOG_WARNING, 0, 0,
                   "remote sync update fail, path : %s", local->remotepath);
            goto err;
        }
    }
    /* fsync is successful on bitmap file, good to do fsync on file */
    STACK_WIND(frame, tier_flush_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->flush, fd, xdata);

    return 0;
err:
    TIER_STACK_UNWIND(flush, frame, -1, op_errno, NULL);

    return 0;
}

int
tier_fsync_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf, dict_t *xdata)
{
    TIER_STACK_UNWIND(fsync, frame, op_ret, op_errno, prebuf, postbuf, xdata);
    return 0;
}

int
tier_fsync(call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
           dict_t *xdata)
{
    int ret = 0;
    int op_errno = EINVAL;
    tier_local_t *local = NULL;
    tier_private_t *priv = this->private;

    local = tier_local_init(this, frame, NULL, fd, GF_FOP_FSYNC);
    if (!local) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "local init failed");
        op_errno = ENOMEM;
        goto err;
    }

    if (local->state == GF_TIER_LOCAL) {
        STACK_WIND(frame, tier_fsync_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->fsync, fd, datasync, xdata);
        return 0;
    }

    if (!local->remotepath) {
        ret = -1;
        gf_msg(this->name, GF_LOG_ERROR, 0, 0,
               "remote path not available. Check posix logs to resolve %p %d",
               local->inode, local->state);
        goto err;
    }

    if (!priv->stores || !priv->stores->config) {
        gf_msg(this->name, GF_LOG_ERROR, 0, 0, "No remote store plugins found");
        ret = -1;
        goto err;
    }

    if (priv->stores->sync) {
        ret = priv->stores->sync(frame, priv->stores->config, fd);
        if (ret) {
            op_errno = EREMOTE;
            gf_msg(this->name, GF_LOG_WARNING, 0, 0,
                   "remote sync update fail, path : %s", local->remotepath);
            goto err;
        }
    }
    /* fsync is successful on bitmap file, good to do fsync on file */
    STACK_WIND(frame, tier_fsync_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->fsync, fd, datasync, xdata);

    return 0;
err:
    TIER_STACK_UNWIND(fsync, frame, -1, op_errno, NULL, NULL, NULL);

    return 0;
}

int
tier_rmdir(call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
           dict_t *xdata)
{
    tier_private_t *priv = this->private;

    if (!priv->stores || !priv->stores->config) {
        goto wind;
    }

    if (priv->stores->rmdir) {
        int ret = priv->stores->rmdir(frame, priv->stores->config, loc->path);
        if (ret) {
            /* ignore errors from rmdir */
            ret = 0;
        }
    }

wind:
    STACK_WIND(frame, default_rmdir_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->rmdir, loc, flags, xdata);

    return 0;
}

int32_t
tier_open_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, fd_t *fd, dict_t *xdata)
{
    tier_private_t *priv = this->private;
    if (op_ret >= 0) {
        tier_inode_ctx_t *ctx = NULL;
        __tier_inode_ctx_get(this, fd->inode, &ctx);
        if (ctx && priv->stores && priv->stores->open) {
            int ret = priv->stores->open(fd->inode, fd, ctx->remote_path,
                                         priv->stores->config);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, -ret, 0,
                       "remote open failed, remotepath: %s", ctx->remote_path);
                op_ret = -1;
                op_errno = -ret;
                goto out;
            }
            ret = fd_ctx_set(fd, this, 1);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, -ret, 0,
                       "fd_ctx_set failed, remotepath: %s", ctx->remote_path);
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
            }
        }
    }

out:
    STACK_UNWIND_STRICT(open, frame, op_ret, op_errno, fd, xdata);
    return 0;
}

int32_t
tier_open(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          fd_t *fd, dict_t *xdata)
{
    STACK_WIND(frame, tier_open_cbk, FIRST_CHILD(this),
               FIRST_CHILD(this)->fops->open, loc, flags, fd, xdata);
    return 0;
}

int
tier_notify(xlator_t *this, int32_t event, void *data, ...)
{
    tier_private_t *priv = this->private;

    if (GF_EVENT_PARENT_DOWN == event) {
        gf_log(this->name, GF_LOG_DEBUG,
               "Ideally, send notification and stop pending operations");
    }

    if (GF_EVENT_CHILD_UP == event) {
        if (!priv->stores || !priv->stores->config) {
            goto out;
        }
        gf_log(this->name, GF_LOG_DEBUG, "start the threads");
        trigger_download_of_pending_files(this);
    }

out:
    return default_notify(this, event, data);
}

struct xlator_fops tier_fops = {
    /* implement to update inode context */
    .lookup = tier_lookup,

    .setxattr = tier_setxattr,
    .fsetxattr = tier_fsetxattr,
    .getxattr = tier_getxattr,
    .fgetxattr = tier_fgetxattr,

    .zerofill = tier_zerofill,   /* generate */
    .fallocate = tier_fallocate, /* generate */
    .discard = tier_discard,     /* generate */

    /* not requried for pavilion as DHT doesn't use it, but need
       to pass it to plugin regardless if the file is remote */
    .rchecksum = tier_rchecksum, /* generate */

    .open = tier_open,
    .writev = tier_writev,
    .readv = tier_readv,
    .unlink = tier_unlink,
    .fsync = tier_fsync,
    .flush = tier_flush,

    /* only directory operation, required for keeping the states clean */
    .rmdir = tier_rmdir,

    .truncate = tier_truncate,
    .ftruncate = tier_ftruncate,
};

struct xlator_cbks tier_cbks = {
    .forget = tier_forget,
    .release = tier_release,
};

struct volume_options tier_options[] = {
    {.key = {"tier-storetype"},
     .type = GF_OPTION_TYPE_STR,
     .default_value = "filesystem",
     .description = "Defines which remote store is enabled"},
    {.key = {"tier-remote-read"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "true",
     .description = "Defines a remote read fop when on"},
    {.key = {"tier-cold-mountpoint"},
     .type = GF_OPTION_TYPE_STR,
     .description = "Define cold tier mountpoint, from which to copy the data"},
    {.key = {"tier-bitmap-dir"},
     .type = GF_OPTION_TYPE_PATH,
     .default_value = "{{ brick.path }}/.glusterfs/tier",
     .description = "Path inside .glusterfs to store bitmap files while "
                    "migrating from cold tier"},
    {.key = {"tier-cold-block-size"},
     .type = GF_OPTION_TYPE_SIZET,
     .min = 128 * GF_UNIT_KB,
     .default_value = "1048576",
     .max = 8 * GF_UNIT_MB,
     .description = "Blocksize to be used for bitmap"},
    {.key = {"pass-through"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "true",
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .tags = {"tier"},
     .description = "Enable/Disable tier translator"},
    {.key = {"tier-stub-size"},
     .type = GF_OPTION_TYPE_SIZET,
     .default_value = "0",
     .tags = {"tier"},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .description = "size of the stub on hot tier"},
    {.key = {"tier-plugin-migrate-thread"},
     .type = GF_OPTION_TYPE_BOOL,
     .default_value = "enable",
     .tags = {"tier"},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .description =
         "Automatically download files upon 'threshold-block-count' writes"},
    {.key = {"tier-threshold-block-count"},
     .type = GF_OPTION_TYPE_INT,
     .default_value = "20",
     .tags = {"tier"},
     .flags = OPT_FLAG_SETTABLE | OPT_FLAG_DOC,
     .description =
         "If migration thread is enabled, use this value to trigger migration"},
    {.key = {NULL}},
};

xlator_api_t xlator_api = {
    .init = init,
    .fini = tier_fini,
    .notify = tier_notify,
    .reconfigure = tier_reconfigure,
    .mem_acct_init = tier_mem_acct_init,
    .fops = &tier_fops,
    .cbks = &tier_cbks,
    .options = tier_options,
    .identifier = "tier",
    .category = GF_TECH_PREVIEW,
};
