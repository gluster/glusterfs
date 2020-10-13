/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "dht-common.h"
#include "dht-lock.h"
#include "glusterfs/compat-errno.h"  // for ENODATA on BSD

static void
dht_free_fd_ctx(dht_fd_ctx_t *fd_ctx)
{
    GF_FREE(fd_ctx);
}

int32_t
dht_fd_ctx_destroy(xlator_t *this, fd_t *fd)
{
    dht_fd_ctx_t *fd_ctx = NULL;
    uint64_t value = 0;
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);

    ret = fd_ctx_del(fd, this, &value);
    if (ret) {
        goto out;
    }

    fd_ctx = (dht_fd_ctx_t *)(uintptr_t)value;
    if (fd_ctx) {
        GF_REF_PUT(fd_ctx);
    }
out:
    return ret;
}

static int
__dht_fd_ctx_set(xlator_t *this, fd_t *fd, xlator_t *dst)
{
    dht_fd_ctx_t *fd_ctx = NULL;
    uint64_t value = 0;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);

    fd_ctx = GF_CALLOC(1, sizeof(*fd_ctx), gf_dht_mt_fd_ctx_t);

    if (!fd_ctx) {
        goto out;
    }

    fd_ctx->opened_on_dst = (uint64_t)(uintptr_t)dst;
    GF_REF_INIT(fd_ctx, dht_free_fd_ctx);

    value = (uint64_t)(uintptr_t)fd_ctx;

    ret = __fd_ctx_set(fd, this, value);
    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_FD_CTX_SET_FAILED,
                "fd=0x%p", fd, NULL);
        GF_REF_PUT(fd_ctx);
    }
out:
    return ret;
}

int
dht_fd_ctx_set(xlator_t *this, fd_t *fd, xlator_t *dst)
{
    dht_fd_ctx_t *fd_ctx = NULL;
    uint64_t value = 0;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);

    LOCK(&fd->lock);
    {
        ret = __fd_ctx_get(fd, this, &value);
        if (ret && value) {
            fd_ctx = (dht_fd_ctx_t *)(uintptr_t)value;
            if (fd_ctx->opened_on_dst == (uint64_t)(uintptr_t)dst) {
                /* This could happen due to racing
                 * check_progress tasks*/
                goto unlock;
            } else {
                /* This would be a big problem*/
                /* Overwrite and hope for the best*/
                fd_ctx->opened_on_dst = (uint64_t)(uintptr_t)dst;
                UNLOCK(&fd->lock);
                gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_INVALID_VALUE,
                        NULL);

                goto out;
            }
        }
        ret = __dht_fd_ctx_set(this, fd, dst);
    }
unlock:
    UNLOCK(&fd->lock);
out:
    return ret;
}

static dht_fd_ctx_t *
dht_fd_ctx_get(xlator_t *this, fd_t *fd)
{
    dht_fd_ctx_t *fd_ctx = NULL;
    int ret = -1;
    uint64_t tmp_val = 0;

    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);

    LOCK(&fd->lock);
    {
        ret = __fd_ctx_get(fd, this, &tmp_val);
        if ((ret < 0) || (tmp_val == 0)) {
            goto unlock;
        }

        fd_ctx = (dht_fd_ctx_t *)(uintptr_t)tmp_val;
        GF_REF_GET(fd_ctx);
    }
unlock:
    UNLOCK(&fd->lock);

out:
    return fd_ctx;
}

gf_boolean_t
dht_fd_open_on_dst(xlator_t *this, fd_t *fd, xlator_t *dst)
{
    dht_fd_ctx_t *fd_ctx = NULL;
    gf_boolean_t opened = _gf_false;

    fd_ctx = dht_fd_ctx_get(this, fd);

    if (fd_ctx) {
        if (fd_ctx->opened_on_dst == (uint64_t)(uintptr_t)dst) {
            opened = _gf_true;
        }
        GF_REF_PUT(fd_ctx);
    }

    return opened;
}

void
dht_free_mig_info(void *data)
{
    dht_migrate_info_t *miginfo = NULL;

    miginfo = data;
    GF_FREE(miginfo);

    return;
}

static int
dht_inode_ctx_set_mig_info(xlator_t *this, inode_t *inode, xlator_t *src_subvol,
                           xlator_t *dst_subvol)
{
    dht_migrate_info_t *miginfo = NULL;
    uint64_t value = 0;
    int ret = -1;

    miginfo = GF_CALLOC(1, sizeof(*miginfo), gf_dht_mt_miginfo_t);
    if (miginfo == NULL)
        goto out;

    miginfo->src_subvol = src_subvol;
    miginfo->dst_subvol = dst_subvol;
    GF_REF_INIT(miginfo, dht_free_mig_info);

    value = (uint64_t)(uintptr_t)miginfo;

    ret = inode_ctx_set1(inode, this, &value);
    if (ret < 0) {
        GF_REF_PUT(miginfo);
    }

out:
    return ret;
}

int
dht_inode_ctx_get_mig_info(xlator_t *this, inode_t *inode,
                           xlator_t **src_subvol, xlator_t **dst_subvol)
{
    int ret = -1;
    uint64_t tmp_miginfo = 0;
    dht_migrate_info_t *miginfo = NULL;

    LOCK(&inode->lock);
    {
        ret = __inode_ctx_get1(inode, this, &tmp_miginfo);
        if ((ret < 0) || (tmp_miginfo == 0)) {
            UNLOCK(&inode->lock);
            goto out;
        }

        miginfo = (dht_migrate_info_t *)(uintptr_t)tmp_miginfo;
        GF_REF_GET(miginfo);
    }
    UNLOCK(&inode->lock);

    if (src_subvol)
        *src_subvol = miginfo->src_subvol;

    if (dst_subvol)
        *dst_subvol = miginfo->dst_subvol;

    GF_REF_PUT(miginfo);

out:
    return ret;
}

gf_boolean_t
dht_mig_info_is_invalid(xlator_t *current, xlator_t *src_subvol,
                        xlator_t *dst_subvol)
{
    /* Not set
     */
    if (!src_subvol || !dst_subvol)
        return _gf_true;

    /* Invalid scenarios:
     * The src_subvol does not match the subvol on which the current op was sent
     * so the cached subvol has changed between the last mig_info_set and now.
     * src_subvol == dst_subvol. The file was migrated without any FOP detecting
     * a P2 so the old dst is now the current subvol.
     *
     * There is still one scenario where the info could be outdated - if
     * file has undergone multiple migrations and ends up on the same src_subvol
     * on which the mig_info was first set.
     */
    if ((current == dst_subvol) || (current != src_subvol))
        return _gf_true;

    return _gf_false;
}

/* Used to check if fd fops have the fd opened on the cached subvol
 * This is required when:
 * 1. an fd is opened on FILE1 on subvol1
 * 2. the file is migrated to subvol2
 * 3. a lookup updates the cached subvol in the inode_ctx to subvol2
 * 4. a write comes on the fd
 * The write is sent to subvol2 on an fd which has been opened only on fd1
 * Since the migration phase checks don't kick in, the fop fails with EBADF
 *
 */

int
dht_check_and_open_fd_on_subvol_complete(int ret, call_frame_t *frame,
                                         void *data)
{
    glusterfs_fop_t fop = 0;
    dht_local_t *local = NULL;
    xlator_t *subvol = NULL;
    xlator_t *this = NULL;
    fd_t *fd = NULL;
    int op_errno = -1;

    local = frame->local;
    this = frame->this;
    fop = local->fop;
    subvol = local->cached_subvol;
    fd = local->fd;

    if (ret) {
        op_errno = local->op_errno;
        goto handle_err;
    }

    switch (fop) {
        case GF_FOP_WRITE:
            STACK_WIND_COOKIE(frame, dht_writev_cbk, subvol, subvol,
                              subvol->fops->writev, fd, local->rebalance.vector,
                              local->rebalance.count, local->rebalance.offset,
                              local->rebalance.flags, local->rebalance.iobref,
                              local->xattr_req);
            break;

        case GF_FOP_FLUSH:
            STACK_WIND(frame, dht_flush_cbk, subvol, subvol->fops->flush, fd,
                       local->xattr_req);
            break;

        case GF_FOP_FSETATTR:
            STACK_WIND_COOKIE(frame, dht_file_setattr_cbk, subvol, subvol,
                              subvol->fops->fsetattr, fd,
                              &local->rebalance.stbuf, local->rebalance.flags,
                              local->xattr_req);
            break;

        case GF_FOP_ZEROFILL:
            STACK_WIND_COOKIE(frame, dht_zerofill_cbk, subvol, subvol,
                              subvol->fops->zerofill, fd,
                              local->rebalance.offset, local->rebalance.size,
                              local->xattr_req);

            break;

        case GF_FOP_DISCARD:
            STACK_WIND_COOKIE(frame, dht_discard_cbk, subvol, subvol,
                              subvol->fops->discard, local->fd,
                              local->rebalance.offset, local->rebalance.size,
                              local->xattr_req);
            break;

        case GF_FOP_FALLOCATE:
            STACK_WIND_COOKIE(frame, dht_fallocate_cbk, subvol, subvol,
                              subvol->fops->fallocate, fd,
                              local->rebalance.flags, local->rebalance.offset,
                              local->rebalance.size, local->xattr_req);
            break;

        case GF_FOP_FTRUNCATE:
            STACK_WIND_COOKIE(frame, dht_truncate_cbk, subvol, subvol,
                              subvol->fops->ftruncate, fd,
                              local->rebalance.offset, local->xattr_req);
            break;

        case GF_FOP_FSYNC:
            STACK_WIND_COOKIE(frame, dht_fsync_cbk, subvol, subvol,
                              subvol->fops->fsync, local->fd,
                              local->rebalance.flags, local->xattr_req);
            break;

        case GF_FOP_READ:
            STACK_WIND(frame, dht_readv_cbk, subvol, subvol->fops->readv,
                       local->fd, local->rebalance.size,
                       local->rebalance.offset, local->rebalance.flags,
                       local->xattr_req);
            break;

        case GF_FOP_FSTAT:
            STACK_WIND_COOKIE(frame, dht_file_attr_cbk, subvol, subvol,
                              subvol->fops->fstat, fd, local->xattr_req);
            break;

        case GF_FOP_FSETXATTR:
            STACK_WIND_COOKIE(frame, dht_file_setxattr_cbk, subvol, subvol,
                              subvol->fops->fsetxattr, local->fd,
                              local->rebalance.xattr, local->rebalance.flags,
                              local->xattr_req);
            break;

        case GF_FOP_FREMOVEXATTR:
            STACK_WIND_COOKIE(frame, dht_file_removexattr_cbk, subvol, subvol,
                              subvol->fops->fremovexattr, local->fd, local->key,
                              local->xattr_req);

            break;

        case GF_FOP_FXATTROP:
            STACK_WIND(frame, dht_common_xattrop_cbk, subvol,
                       subvol->fops->fxattrop, local->fd,
                       local->rebalance.flags, local->rebalance.xattr,
                       local->xattr_req);
            break;

        case GF_FOP_FGETXATTR:
            STACK_WIND(frame, dht_getxattr_cbk, subvol, subvol->fops->fgetxattr,
                       local->fd, local->key, NULL);
            break;

        case GF_FOP_FINODELK:
            STACK_WIND(frame, dht_finodelk_cbk, subvol, subvol->fops->finodelk,
                       local->key, local->fd, local->rebalance.lock_cmd,
                       &local->rebalance.flock, local->xattr_req);
            break;
        default:
            gf_smsg(this->name, GF_LOG_ERROR, 0, DHT_MSG_UNKNOWN_FOP, "fd=%p",
                    fd, "gfid=%s", uuid_utoa(fd->inode->gfid), "name=%s",
                    subvol->name, NULL);
            break;
    }

    goto out;

    /* Could not open the fd on the dst. Unwind */

handle_err:

    switch (fop) {
        case GF_FOP_WRITE:
            DHT_STACK_UNWIND(writev, frame, -1, op_errno, NULL, NULL, NULL);
            break;

        case GF_FOP_FLUSH:
            DHT_STACK_UNWIND(flush, frame, -1, op_errno, NULL);
            break;

        case GF_FOP_FSETATTR:
            DHT_STACK_UNWIND(fsetattr, frame, -1, op_errno, NULL, NULL, NULL);
            break;

        case GF_FOP_ZEROFILL:
            DHT_STACK_UNWIND(zerofill, frame, -1, op_errno, NULL, NULL, NULL);
            break;

        case GF_FOP_DISCARD:
            DHT_STACK_UNWIND(discard, frame, -1, op_errno, NULL, NULL, NULL);
            break;

        case GF_FOP_FALLOCATE:
            DHT_STACK_UNWIND(fallocate, frame, -1, op_errno, NULL, NULL, NULL);
            break;

        case GF_FOP_FTRUNCATE:
            DHT_STACK_UNWIND(ftruncate, frame, -1, op_errno, NULL, NULL, NULL);
            break;

        case GF_FOP_FSYNC:
            DHT_STACK_UNWIND(fsync, frame, -1, op_errno, NULL, NULL, NULL);
            break;

        case GF_FOP_READ:
            DHT_STACK_UNWIND(readv, frame, -1, op_errno, NULL, 0, NULL, NULL,
                             NULL);
            break;

        case GF_FOP_FSTAT:
            DHT_STACK_UNWIND(fstat, frame, -1, op_errno, NULL, NULL);
            break;

        case GF_FOP_FSETXATTR:
            DHT_STACK_UNWIND(fsetxattr, frame, -1, op_errno, NULL);
            break;

        case GF_FOP_FREMOVEXATTR:
            DHT_STACK_UNWIND(fremovexattr, frame, -1, op_errno, NULL);
            break;

        case GF_FOP_FXATTROP:
            DHT_STACK_UNWIND(fxattrop, frame, -1, op_errno, NULL, NULL);
            break;

        case GF_FOP_FGETXATTR:
            DHT_STACK_UNWIND(fgetxattr, frame, -1, op_errno, NULL, NULL);
            break;

        case GF_FOP_FINODELK:
            DHT_STACK_UNWIND(finodelk, frame, -1, op_errno, NULL);
            break;

        default:
            gf_smsg(this->name, GF_LOG_ERROR, 0, DHT_MSG_UNKNOWN_FOP, "fd=%p",
                    fd, "gfid=%s", uuid_utoa(fd->inode->gfid), "name=%s",
                    subvol->name, NULL);
            break;
    }

out:

    return 0;
}

/* Check once again if the fd has been opened on the cached subvol.
 * If not, open and update the fd_ctx.
 */

int
dht_check_and_open_fd_on_subvol_task(void *data)
{
    loc_t loc = {
        0,
    };
    int ret = -1;
    call_frame_t *frame = NULL;
    dht_local_t *local = NULL;
    fd_t *fd = NULL;
    xlator_t *this = NULL;
    xlator_t *subvol = NULL;

    frame = data;
    local = frame->local;
    this = THIS;
    fd = local->fd;
    subvol = local->cached_subvol;

    local->fd_checked = _gf_true;

    if (fd_is_anonymous(fd) || dht_fd_open_on_dst(this, fd, subvol)) {
        ret = 0;
        goto out;
    }

    gf_msg_debug(this->name, 0, "Opening fd (%p, flags=0%o) on file %s @ %s",
                 fd, fd->flags, uuid_utoa(fd->inode->gfid), subvol->name);

    loc.inode = inode_ref(fd->inode);
    gf_uuid_copy(loc.gfid, fd->inode->gfid);

    /* Open this on the dst subvol */

    SYNCTASK_SETID(0, 0);

    ret = syncop_open(subvol, &loc, (fd->flags & ~(O_CREAT | O_EXCL | O_TRUNC)),
                      fd, NULL, NULL);

    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, DHT_MSG_OPEN_FD_ON_DST_FAILED,
                "fd=%p", fd, "flags=0%o", fd->flags, "gfid=%s",
                uuid_utoa(fd->inode->gfid), "name=%s", subvol->name, NULL);
        /* This can happen if the cached subvol was updated in the
         * inode_ctx and the fd was opened on the new cached suvol
         * after this fop was wound on the old cached subvol.
         * As we do not close the fd on the old subvol (a leak)
         * don't treat ENOENT as an error and allow the phase1/phase2
         * checks to handle it.
         */

        if ((-ret != ENOENT) && (-ret != ESTALE)) {
            local->op_errno = -ret;
            ret = -1;
        } else {
            ret = 0;
        }

        local->op_errno = -ret;
        ret = -1;

    } else {
        dht_fd_ctx_set(this, fd, subvol);
    }

    SYNCTASK_SETID(frame->root->uid, frame->root->gid);
out:
    loc_wipe(&loc);

    return ret;
}

int
dht_check_and_open_fd_on_subvol(xlator_t *this, call_frame_t *frame)
{
    int ret = -1;
    dht_local_t *local = NULL;

    /*
            if (dht_fd_open_on_dst (this, fd, subvol))
                    goto out;
    */
    local = frame->local;

    ret = synctask_new(this->ctx->env, dht_check_and_open_fd_on_subvol_task,
                       dht_check_and_open_fd_on_subvol_complete, frame, frame);

    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, DHT_MSG_SYNCTASK_CREATE_FAILED,
                "to-check-and-open fd=%p", local->fd, NULL);
    }

    return ret;
}

int
dht_frame_return(call_frame_t *frame)
{
    dht_local_t *local = NULL;
    int this_call_cnt = -1;

    if (!frame)
        return -1;

    local = frame->local;

    LOCK(&frame->lock);
    {
        this_call_cnt = --local->call_cnt;
    }
    UNLOCK(&frame->lock);

    return this_call_cnt;
}

/*
 * Use this function to specify which subvol you want the file created
 * on - this need not be the hashed subvol.
 * Format: <filename>@<this->name>:<subvol-name>
 * Eg: file-1@vol1-dht:vol1-client-0
 *     where vol1 is a pure distribute volume
 *     will create file-1 on vol1-client-0
 */

int
dht_filter_loc_subvol_key(xlator_t *this, loc_t *loc, loc_t *new_loc,
                          xlator_t **subvol)
{
    char *new_name = NULL;
    char *new_path = NULL;
    xlator_list_t *trav = NULL;
    char key[1024] = {
        0,
    };
    int ret = 0; /* not found */
    int keylen = 0;
    int name_len = 0;
    int path_len = 0;

    /* Why do other tasks if first required 'char' itself is not there */
    if (!new_loc || !loc || !loc->name || !strchr(loc->name, '@')) {
        /* Skip the GF_FREE checks here */
        return ret;
    }

    trav = this->children;
    while (trav) {
        keylen = snprintf(key, sizeof(key), "*@%s:%s", this->name,
                          trav->xlator->name);
        /* Ignore '*' */
        keylen = keylen - 1;
        if (fnmatch(key, loc->name, FNM_NOESCAPE) == 0) {
            name_len = strlen(loc->name) - keylen;
            new_name = GF_MALLOC(name_len + 1, gf_common_mt_char);
            if (!new_name)
                goto out;
            if (fnmatch(key, loc->path, FNM_NOESCAPE) == 0) {
                path_len = strlen(loc->path) - keylen;
                new_path = GF_MALLOC(path_len + 1, gf_common_mt_char);
                if (!new_path)
                    goto out;
                snprintf(new_path, path_len + 1, "%s", loc->path);
            }
            snprintf(new_name, name_len + 1, "%s", loc->name);

            if (new_loc) {
                new_loc->path = ((new_path) ? new_path : gf_strdup(loc->path));
                new_loc->name = new_name;
                new_loc->inode = inode_ref(loc->inode);
                new_loc->parent = inode_ref(loc->parent);
            }
            *subvol = trav->xlator;
            ret = 1; /* success */
            goto out;
        }
        trav = trav->next;
    }
out:
    if (!ret) {
        /* !success */
        GF_FREE(new_path);
        GF_FREE(new_name);
    }
    return ret;
}

static xlator_t *
dht_get_subvol_from_id(xlator_t *this, int client_id)
{
    xlator_t *xl = NULL;
    dht_conf_t *conf = NULL;
    char *sid = NULL;
    int32_t ret = -1;

    conf = this->private;

    ret = gf_asprintf(&sid, "%d", client_id);
    if (ret == -1) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, DHT_MSG_ASPRINTF_FAILED, NULL);
        goto out;
    }

    if (dict_get_ptr(conf->leaf_to_subvol, sid, (void **)&xl))
        xl = NULL;

    GF_FREE(sid);

out:
    return xl;
}

int
dht_deitransform(xlator_t *this, uint64_t y, xlator_t **subvol_p)
{
    int client_id = 0;
    xlator_t *subvol = 0;
    dht_conf_t *conf = NULL;

    if (!this->private)
        return -1;

    conf = this->private;

    client_id = gf_deitransform(this, y);

    subvol = dht_get_subvol_from_id(this, client_id);

    if (!subvol)
        subvol = conf->subvolumes[0];

    if (subvol_p)
        *subvol_p = subvol;

    return 0;
}

void
dht_local_wipe(xlator_t *this, dht_local_t *local)
{
    int i = 0;

    if (!local)
        return;

    loc_wipe(&local->loc);
    loc_wipe(&local->loc2);
    loc_wipe(&local->loc2_copy);

    if (local->xattr)
        dict_unref(local->xattr);

    if (local->inode)
        inode_unref(local->inode);

    if (local->layout) {
        dht_layout_unref(this, local->layout);
        local->layout = NULL;
    }

    loc_wipe(&local->linkfile.loc);

    if (local->linkfile.xattr)
        dict_unref(local->linkfile.xattr);

    if (local->linkfile.inode)
        inode_unref(local->linkfile.inode);

    if (local->fd) {
        fd_unref(local->fd);
        local->fd = NULL;
    }

    if (local->params) {
        dict_unref(local->params);
        local->params = NULL;
    }

    if (local->xattr_req)
        dict_unref(local->xattr_req);
    if (local->mds_xattr)
        dict_unref(local->mds_xattr);
    if (local->xdata)
        dict_unref(local->xdata);

    if (local->selfheal.layout) {
        dht_layout_unref(this, local->selfheal.layout);
        local->selfheal.layout = NULL;
    }

    if (local->selfheal.refreshed_layout) {
        dht_layout_unref(this, local->selfheal.refreshed_layout);
        local->selfheal.refreshed_layout = NULL;
    }

    for (i = 0; i < 2; i++) {
        dht_lock_array_free(local->lock[i].ns.parent_layout.locks,
                            local->lock[i].ns.parent_layout.lk_count);

        GF_FREE(local->lock[i].ns.parent_layout.locks);

        dht_lock_array_free(local->lock[i].ns.directory_ns.locks,
                            local->lock[i].ns.directory_ns.lk_count);
        GF_FREE(local->lock[i].ns.directory_ns.locks);
    }

    GF_FREE(local->key);

    if (local->rebalance.xdata)
        dict_unref(local->rebalance.xdata);

    if (local->rebalance.xattr)
        dict_unref(local->rebalance.xattr);

    if (local->rebalance.dict)
        dict_unref(local->rebalance.dict);

    GF_FREE(local->rebalance.vector);

    if (local->rebalance.iobref)
        iobref_unref(local->rebalance.iobref);

    if (local->stub) {
        call_stub_destroy(local->stub);
        local->stub = NULL;
    }

    if (local->ret_cache)
        GF_FREE(local->ret_cache);

    mem_put(local);
}

dht_local_t *
dht_local_init(call_frame_t *frame, loc_t *loc, fd_t *fd, glusterfs_fop_t fop)
{
    dht_local_t *local = NULL;
    inode_t *inode = NULL;
    int ret = 0;

    local = mem_get0(THIS->local_pool);
    if (!local)
        goto out;

    if (loc) {
        ret = loc_copy(&local->loc, loc);
        if (ret)
            goto out;

        inode = loc->inode;
    }

    if (fd) {
        local->fd = fd_ref(fd);
        if (!inode)
            inode = fd->inode;
    }

    local->op_ret = -1;
    local->op_errno = EUCLEAN;
    local->fop = fop;

    if (inode) {
        local->layout = dht_layout_get(frame->this, inode);
        local->cached_subvol = dht_subvol_get_cached(frame->this, inode);
    }

    frame->local = local;

out:
    if (ret) {
        if (local)
            mem_put(local);
        local = NULL;
    }
    return local;
}

xlator_t *
dht_first_up_subvol(xlator_t *this)
{
    dht_conf_t *conf = NULL;
    xlator_t *child = NULL;
    int i = 0;
    time_t time = 0;

    conf = this->private;
    if (!conf)
        goto out;

    LOCK(&conf->subvolume_lock);
    {
        for (i = 0; i < conf->subvolume_cnt; i++) {
            if (conf->subvol_up_time[i]) {
                if (!time) {
                    time = conf->subvol_up_time[i];
                    child = conf->subvolumes[i];
                } else if (time > conf->subvol_up_time[i]) {
                    time = conf->subvol_up_time[i];
                    child = conf->subvolumes[i];
                }
            }
        }
    }
    UNLOCK(&conf->subvolume_lock);

out:
    return child;
}

xlator_t *
dht_last_up_subvol(xlator_t *this)
{
    dht_conf_t *conf = NULL;
    xlator_t *child = NULL;
    int i = 0;

    conf = this->private;
    if (!conf)
        goto out;

    LOCK(&conf->subvolume_lock);
    {
        for (i = conf->subvolume_cnt - 1; i >= 0; i--) {
            if (conf->subvolume_status[i]) {
                child = conf->subvolumes[i];
                break;
            }
        }
    }
    UNLOCK(&conf->subvolume_lock);

out:
    return child;
}

xlator_t *
dht_subvol_get_hashed(xlator_t *this, loc_t *loc)
{
    dht_layout_t *layout = NULL;
    xlator_t *subvol = NULL;
    dht_conf_t *conf = NULL;
    dht_methods_t *methods = NULL;

    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    methods = &(conf->methods);

    if (__is_root_gfid(loc->gfid)) {
        subvol = dht_first_up_subvol(this);
        goto out;
    }

    GF_VALIDATE_OR_GOTO(this->name, loc->parent, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->name, out);

    layout = dht_layout_get(this, loc->parent);

    if (!layout) {
        gf_msg_debug(this->name, 0, "Missing layout. path=%s, parent gfid =%s",
                     loc->path, uuid_utoa(loc->parent->gfid));
        goto out;
    }

    subvol = methods->layout_search(this, layout, loc->name);

    if (!subvol) {
        gf_msg_debug(this->name, 0, "No hashed subvolume for path=%s",
                     loc->path);
        goto out;
    }

out:
    if (layout) {
        dht_layout_unref(this, layout);
    }

    return subvol;
}

/* Get the layout of the root dir.
 * This method does not unref the layout - this needs to be done externally */
xlator_t *
dht_subvol_get_hashed_root_layout(xlator_t *this, loc_t *loc,
                                  dht_layout_t *root_layout)
{
    xlator_t *subvol = NULL;
    dht_conf_t *conf = NULL;
    dht_methods_t *methods = NULL;

    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    methods = &(conf->methods);

    if (__is_root_gfid(loc->gfid)) {
        subvol = dht_first_up_subvol(this);
        goto out;
    }

    GF_VALIDATE_OR_GOTO(this->name, loc->parent, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->name, out);

    subvol = methods->layout_search(this, root_layout, loc->name);

    if (!subvol) {
        gf_msg_debug(this->name, 0, "No hashed subvolume for path=%s",
                     loc->path);
        goto out;
    }

out:
    return subvol;
}

xlator_t *
dht_subvol_get_cached(xlator_t *this, inode_t *inode)
{
    dht_layout_t *layout = NULL;
    xlator_t *subvol = NULL;

    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    layout = dht_layout_get(this, inode);

    if (!layout) {
        goto out;
    }

    subvol = layout->list[0].xlator;

out:
    if (layout) {
        dht_layout_unref(this, layout);
    }

    return subvol;
}

xlator_t *
dht_subvol_next(xlator_t *this, xlator_t *prev)
{
    dht_conf_t *conf = NULL;
    int i = 0;
    xlator_t *next = NULL;

    conf = this->private;
    if (!conf)
        goto out;

    for (i = 0; i < conf->subvolume_cnt; i++) {
        if (conf->subvolumes[i] == prev) {
            if ((i + 1) < conf->subvolume_cnt)
                next = conf->subvolumes[i + 1];
            break;
        }
    }

out:
    return next;
}

/* This func wraps around, if prev is actually the last subvol.
 */
xlator_t *
dht_subvol_next_available(xlator_t *this, xlator_t *prev)
{
    dht_conf_t *conf = NULL;
    int i = 0;
    xlator_t *next = NULL;

    conf = this->private;
    if (!conf)
        goto out;

    for (i = 0; i < conf->subvolume_cnt; i++) {
        if (conf->subvolumes[i] == prev) {
            /* if prev is last in conf->subvolumes, then wrap
             * around.
             */
            if ((i + 1) < conf->subvolume_cnt) {
                next = conf->subvolumes[i + 1];
            } else {
                next = conf->subvolumes[0];
            }
            break;
        }
    }

out:
    return next;
}
int
dht_subvol_cnt(xlator_t *this, xlator_t *subvol)
{
    int i = 0;
    int ret = -1;
    dht_conf_t *conf = NULL;

    conf = this->private;
    if (!conf)
        goto out;

    for (i = 0; i < conf->subvolume_cnt; i++) {
        if (subvol == conf->subvolumes[i]) {
            ret = i;
            break;
        }
    }

out:
    return ret;
}

#define set_if_greater(a, b)                                                   \
    do {                                                                       \
        if ((a) < (b))                                                         \
            (a) = (b);                                                         \
    } while (0)

#define set_if_greater_time(a, an, b, bn)                                      \
    do {                                                                       \
        if (((a) < (b)) || (((a) == (b)) && ((an) < (bn)))) {                  \
            (a) = (b);                                                         \
            (an) = (bn);                                                       \
        }                                                                      \
    } while (0)

int
dht_iatt_merge(xlator_t *this, struct iatt *to, struct iatt *from)
{
    if (!from || !to)
        return 0;

    to->ia_dev = from->ia_dev;

    gf_uuid_copy(to->ia_gfid, from->ia_gfid);

    to->ia_ino = from->ia_ino;
    to->ia_prot = from->ia_prot;
    to->ia_type = from->ia_type;
    to->ia_nlink = from->ia_nlink;
    to->ia_rdev = from->ia_rdev;
    to->ia_size += from->ia_size;
    to->ia_blksize = from->ia_blksize;
    to->ia_blocks += from->ia_blocks;

    if (IA_ISDIR(from->ia_type)) {
        to->ia_blocks = DHT_DIR_STAT_BLOCKS;
        to->ia_size = DHT_DIR_STAT_SIZE;
    }
    set_if_greater(to->ia_uid, from->ia_uid);
    set_if_greater(to->ia_gid, from->ia_gid);

    set_if_greater_time(to->ia_atime, to->ia_atime_nsec, from->ia_atime,
                        from->ia_atime_nsec);
    set_if_greater_time(to->ia_mtime, to->ia_mtime_nsec, from->ia_mtime,
                        from->ia_mtime_nsec);
    set_if_greater_time(to->ia_ctime, to->ia_ctime_nsec, from->ia_ctime,
                        from->ia_ctime_nsec);

    return 0;
}

int
dht_build_child_loc(xlator_t *this, loc_t *child, loc_t *parent, char *name)
{
    if (!child) {
        goto err;
    }

    if (strcmp(parent->path, "/") == 0)
        gf_asprintf((char **)&child->path, "/%s", name);
    else
        gf_asprintf((char **)&child->path, "%s/%s", parent->path, name);

    if (!child->path) {
        goto err;
    }

    child->name = strrchr(child->path, '/');
    if (child->name)
        child->name++;

    child->parent = inode_ref(parent->inode);
    child->inode = inode_new(parent->inode->table);

    if (!child->inode) {
        goto err;
    }

    return 0;
err:
    if (child) {
        loc_wipe(child);
    }
    return -1;
}

int
dht_init_local_subvolumes(xlator_t *this, dht_conf_t *conf)
{
    xlator_list_t *subvols = NULL;
    int cnt = 0;

    if (!conf)
        return -1;

    for (subvols = this->children; subvols; subvols = subvols->next)
        cnt++;

    conf->local_subvols = GF_CALLOC(cnt, sizeof(xlator_t *),
                                    gf_dht_mt_xlator_t);

    /* FIX FIX : do this dynamically*/
    conf->local_nodeuuids = GF_CALLOC(cnt, sizeof(subvol_nodeuuids_info_t),
                                      gf_dht_nodeuuids_t);

    if (!conf->local_subvols || !conf->local_nodeuuids) {
        return -1;
    }

    conf->local_subvols_cnt = 0;

    return 0;
}

int
dht_init_subvolumes(xlator_t *this, dht_conf_t *conf)
{
    xlator_list_t *subvols = NULL;
    int cnt = 0;

    if (!conf)
        return -1;

    for (subvols = this->children; subvols; subvols = subvols->next)
        cnt++;

    conf->subvolumes = GF_CALLOC(cnt, sizeof(xlator_t *), gf_dht_mt_xlator_t);
    if (!conf->subvolumes) {
        return -1;
    }
    conf->subvolume_cnt = cnt;
    /* Doesn't make sense to do any dht layer tasks
       if the subvol count is 1. Set it as pass_through */
    if (cnt == 1)
        this->pass_through = _gf_true;

    conf->local_subvols_cnt = 0;

    dht_set_subvol_range(this);

    cnt = 0;
    for (subvols = this->children; subvols; subvols = subvols->next)
        conf->subvolumes[cnt++] = subvols->xlator;

    conf->subvolume_status = GF_CALLOC(cnt, sizeof(char), gf_dht_mt_char);
    if (!conf->subvolume_status) {
        return -1;
    }

    conf->last_event = GF_CALLOC(cnt, sizeof(int), gf_dht_mt_char);
    if (!conf->last_event) {
        return -1;
    }

    conf->subvol_up_time = GF_CALLOC(cnt, sizeof(time_t),
                                     gf_dht_mt_subvol_time);
    if (!conf->subvol_up_time) {
        return -1;
    }

    conf->du_stats = GF_CALLOC(conf->subvolume_cnt, sizeof(dht_du_t),
                               gf_dht_mt_dht_du_t);
    if (!conf->du_stats) {
        return -1;
    }

    conf->decommissioned_bricks = GF_CALLOC(cnt, sizeof(xlator_t *),
                                            gf_dht_mt_xlator_t);
    if (!conf->decommissioned_bricks) {
        return -1;
    }

    return 0;
}

/*
 op_ret values :
  0 : Success.
 -1 : Failure.
  1 : File is being migrated but not by this DHT layer.
*/

static int
dht_migration_complete_check_done(int op_ret, call_frame_t *frame, void *data)
{
    dht_local_t *local = NULL;
    xlator_t *subvol = NULL;

    local = frame->local;

    if (op_ret != 0)
        goto out;

    if (local->cached_subvol == NULL) {
        local->op_errno = EINVAL;
        goto out;
    }

    subvol = local->cached_subvol;

out:
    local->rebalance.target_op_fn(THIS, subvol, frame, op_ret);

    return 0;
}

int
dht_migration_complete_check_task(void *data)
{
    int ret = -1;
    xlator_t *src_node = NULL;
    xlator_t *dst_node = NULL, *linkto_target = NULL;
    dht_local_t *local = NULL;
    dict_t *dict = NULL;
    struct iatt stbuf = {
        0,
    };
    xlator_t *this = NULL;
    call_frame_t *frame = NULL;
    loc_t tmp_loc = {
        0,
    };
    char *path = NULL;
    dht_conf_t *conf = NULL;
    inode_t *inode = NULL;
    fd_t *iter_fd = NULL;
    fd_t *tmp = NULL;
    uint64_t tmp_miginfo = 0;
    dht_migrate_info_t *miginfo = NULL;
    gf_boolean_t skip_open = _gf_false;
    int open_failed = 0;

    this = THIS;
    frame = data;
    local = frame->local;
    conf = this->private;

    src_node = local->cached_subvol;

    if (!local->loc.inode && !local->fd) {
        local->op_errno = EINVAL;
        goto out;
    }

    inode = (!local->fd) ? local->loc.inode : local->fd->inode;

    /* getxattr on cached_subvol for 'linkto' value. Do path based getxattr
     * as root:root. If a fd is already open, access check won't be done*/

    if (!local->loc.inode) {
        ret = syncop_fgetxattr(src_node, local->fd, &dict,
                               conf->link_xattr_name, NULL, NULL);
    } else {
        SYNCTASK_SETID(0, 0);
        ret = syncop_getxattr(src_node, &local->loc, &dict,
                              conf->link_xattr_name, NULL, NULL);
        SYNCTASK_SETID(frame->root->uid, frame->root->gid);
    }

    /*
     * Each DHT xlator layer has its own name for the linkto xattr.
     * If the file mode bits indicate the the file is being migrated but
     * this layer's linkto xattr is not set, it means that another
     * DHT layer is migrating the file. In this case, return 1 so
     * the mode bits can be passed on to the higher layer for appropriate
     * action.
     */
    if (-ret == ENODATA) {
        /* This DHT translator is not migrating this file */

        ret = inode_ctx_reset1(inode, this, &tmp_miginfo);
        if (tmp_miginfo) {
            /* This can be a problem if the file was
             * migrated by two different layers. Raise
             * a warning here.
             */
            gf_smsg(
                this->name, GF_LOG_WARNING, 0, DHT_MSG_HAS_MIGINFO, "tmp=%s",
                tmp_loc.path ? tmp_loc.path : uuid_utoa(tmp_loc.gfid), NULL);

            miginfo = (void *)(uintptr_t)tmp_miginfo;
            GF_REF_PUT(miginfo);
        }
        ret = 1;
        goto out;
    }

    if (!ret)
        linkto_target = dht_linkfile_subvol(this, NULL, NULL, dict);

    if (local->loc.inode) {
        loc_copy(&tmp_loc, &local->loc);
    } else {
        tmp_loc.inode = inode_ref(inode);
        gf_uuid_copy(tmp_loc.gfid, inode->gfid);
    }

    ret = syncop_lookup(this, &tmp_loc, &stbuf, 0, 0, 0);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, DHT_MSG_FILE_LOOKUP_FAILED,
                "tmp=%s", tmp_loc.path ? tmp_loc.path : uuid_utoa(tmp_loc.gfid),
                "name=%s", this->name, NULL);
        local->op_errno = -ret;
        ret = -1;
        goto out;
    }

    dst_node = dht_subvol_get_cached(this, tmp_loc.inode);
    if (linkto_target && dst_node != linkto_target) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_INVALID_LINKFILE,
                "linkto_target_name=%s", linkto_target->name, "dst_name=%s",
                dst_node->name, NULL);
    }

    if (gf_uuid_compare(stbuf.ia_gfid, tmp_loc.inode->gfid)) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, DHT_MSG_GFID_MISMATCH, "tmp=%s",
                tmp_loc.path ? tmp_loc.path : uuid_utoa(tmp_loc.gfid),
                "dst_name=%s", dst_node->name, NULL);
        ret = -1;
        local->op_errno = EIO;
        goto out;
    }

    /* update local. A layout is set in inode-ctx in lookup already */

    dht_layout_unref(this, local->layout);

    local->layout = dht_layout_get(frame->this, inode);
    local->cached_subvol = dst_node;

    ret = 0;

    /* once we detect the migration complete, the inode-ctx2 is no more
       required.. delete the ctx and also, it means, open() already
       done on all the fd of inode */
    ret = inode_ctx_reset1(inode, this, &tmp_miginfo);
    if (tmp_miginfo) {
        miginfo = (void *)(uintptr_t)tmp_miginfo;
        GF_REF_PUT(miginfo);
        goto out;
    }

    /* perform 'open()' on all the fd's present on the inode */
    if (tmp_loc.path == NULL) {
        inode_path(inode, NULL, &path);
        if (path)
            tmp_loc.path = path;
    }

    LOCK(&inode->lock);

    if (list_empty(&inode->fd_list))
        goto unlock;

    /* perform open as root:root. There is window between linkfile
     * creation(root:root) and setattr with the correct uid/gid
     */
    SYNCTASK_SETID(0, 0);

    /* It's possible that we are the last user of iter_fd after each
     * iteration. In this case the fd_unref() of iter_fd at the end of
     * the loop will cause the destruction of the fd. So we need to
     * iterate the list safely because iter_fd cannot be trusted.
     */
    iter_fd = list_entry((&inode->fd_list)->next, typeof(*iter_fd), inode_list);
    while (&iter_fd->inode_list != (&inode->fd_list)) {
        if (fd_is_anonymous(iter_fd) ||
            (dht_fd_open_on_dst(this, iter_fd, dst_node))) {
            if (!tmp) {
                iter_fd = list_entry(iter_fd->inode_list.next, typeof(*iter_fd),
                                     inode_list);
                continue;
            }
            skip_open = _gf_true;
        }
        /* We need to release the inode->lock before calling
         * syncop_open() to avoid possible deadlocks. However this
         * can cause the iter_fd to be released by other threads.
         * To avoid this, we take a reference before releasing the
         * lock.
         */
        fd_ref(iter_fd);

        UNLOCK(&inode->lock);

        if (tmp) {
            fd_unref(tmp);
            tmp = NULL;
        }
        if (skip_open)
            goto next;

        /* flags for open are stripped down to allow following the
         * new location of the file, otherwise we can get EEXIST or
         * truncate the file again as rebalance is moving the data */
        ret = syncop_open(dst_node, &tmp_loc,
                          (iter_fd->flags & ~(O_CREAT | O_EXCL | O_TRUNC)),
                          iter_fd, NULL, NULL);
        if (ret < 0) {
            gf_smsg(this->name, GF_LOG_ERROR, -ret,
                    DHT_MSG_OPEN_FD_ON_DST_FAILED, "id=%p", iter_fd,
                    "flags=0%o", iter_fd->flags, "path=%s", path, "name=%s",
                    dst_node->name, NULL);

            open_failed = 1;
            local->op_errno = -ret;
            ret = -1;
        } else {
            dht_fd_ctx_set(this, iter_fd, dst_node);
        }

    next:
        LOCK(&inode->lock);
        skip_open = _gf_false;
        tmp = iter_fd;
        iter_fd = list_entry(tmp->inode_list.next, typeof(*tmp), inode_list);
    }

    SYNCTASK_SETID(frame->root->uid, frame->root->gid);

    if (open_failed) {
        ret = -1;
        goto unlock;
    }
    ret = 0;

unlock:
    UNLOCK(&inode->lock);
    if (tmp) {
        fd_unref(tmp);
        tmp = NULL;
    }

out:
    if (dict) {
        dict_unref(dict);
    }

    loc_wipe(&tmp_loc);

    return ret;
}

int
dht_rebalance_complete_check(xlator_t *this, call_frame_t *frame)
{
    int ret = -1;

    ret = synctask_new(this->ctx->env, dht_migration_complete_check_task,
                       dht_migration_complete_check_done, frame, frame);
    return ret;
}

/* During 'in-progress' state, both nodes should have the file */
/*
 op_ret values :
  0 : Success
 -1 : Failure.
  1 : File is being migrated but not by this DHT layer.
*/
static int
dht_inprogress_check_done(int op_ret, call_frame_t *frame, void *data)
{
    dht_local_t *local = NULL;
    xlator_t *dst_subvol = NULL, *src_subvol = NULL;
    inode_t *inode = NULL;

    local = frame->local;

    if (op_ret != 0)
        goto out;

    inode = local->loc.inode ? local->loc.inode : local->fd->inode;

    dht_inode_ctx_get_mig_info(THIS, inode, &src_subvol, &dst_subvol);
    if (dht_mig_info_is_invalid(local->cached_subvol, src_subvol, dst_subvol)) {
        dst_subvol = dht_subvol_get_cached(THIS, inode);
        if (!dst_subvol) {
            local->op_errno = EINVAL;
            goto out;
        }
    }

out:
    local->rebalance.target_op_fn(THIS, dst_subvol, frame, op_ret);

    return 0;
}

static int
dht_rebalance_inprogress_task(void *data)
{
    int ret = -1;
    xlator_t *src_node = NULL;
    xlator_t *dst_node = NULL;
    dht_local_t *local = NULL;
    dict_t *dict = NULL;
    call_frame_t *frame = NULL;
    xlator_t *this = NULL;
    char *path = NULL;
    struct iatt stbuf = {
        0,
    };
    loc_t tmp_loc = {
        0,
    };
    dht_conf_t *conf = NULL;
    inode_t *inode = NULL;
    fd_t *iter_fd = NULL;
    fd_t *tmp = NULL;
    int open_failed = 0;
    uint64_t tmp_miginfo = 0;
    dht_migrate_info_t *miginfo = NULL;
    gf_boolean_t skip_open = _gf_false;

    this = THIS;
    frame = data;
    local = frame->local;
    conf = this->private;

    src_node = local->cached_subvol;

    if (!local->loc.inode && !local->fd)
        goto out;

    inode = (!local->fd) ? local->loc.inode : local->fd->inode;

    /* getxattr on cached_subvol for 'linkto' value. Do path based getxattr
     * as root:root. If a fd is already open, access check won't be done*/
    if (local->loc.inode) {
        SYNCTASK_SETID(0, 0);
        ret = syncop_getxattr(src_node, &local->loc, &dict,
                              conf->link_xattr_name, NULL, NULL);
        SYNCTASK_SETID(frame->root->uid, frame->root->gid);
    } else {
        ret = syncop_fgetxattr(src_node, local->fd, &dict,
                               conf->link_xattr_name, NULL, NULL);
    }

    /*
     * Each DHT xlator layer has its own name for the linkto xattr.
     * If the file mode bits indicate the the file is being migrated but
     * this layer's linkto xattr is not present, it means that another
     * DHT layer is migrating the file. In this case, return 1 so
     * the mode bits can be passed on to the higher layer for appropriate
     * action.
     */

    if (-ret == ENODATA) {
        /* This DHT layer is not migrating this file */
        ret = inode_ctx_reset1(inode, this, &tmp_miginfo);
        if (tmp_miginfo) {
            /* This can be a problem if the file was
             * migrated by two different layers. Raise
             * a warning here.
             */
            gf_smsg(
                this->name, GF_LOG_WARNING, 0, DHT_MSG_HAS_MIGINFO, "tmp=%s",
                tmp_loc.path ? tmp_loc.path : uuid_utoa(tmp_loc.gfid), NULL);
            miginfo = (void *)(uintptr_t)tmp_miginfo;
            GF_REF_PUT(miginfo);
        }
        ret = 1;
        goto out;
    }

    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, DHT_MSG_GET_XATTR_FAILED,
                "path=%s", local->loc.path, NULL);
        ret = -1;
        goto out;
    }

    dst_node = dht_linkfile_subvol(this, NULL, NULL, dict);
    if (!dst_node) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, DHT_MSG_GET_XATTR_FAILED,
                "path=%s", local->loc.path, NULL);
        ret = -1;
        goto out;
    }

    local->rebalance.target_node = dst_node;

    if (local->loc.inode) {
        loc_copy(&tmp_loc, &local->loc);
    } else {
        tmp_loc.inode = inode_ref(inode);
        gf_uuid_copy(tmp_loc.gfid, inode->gfid);
    }

    /* lookup on dst */
    ret = syncop_lookup(dst_node, &tmp_loc, &stbuf, NULL, NULL, NULL);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, DHT_MSG_FILE_LOOKUP_FAILED,
                "tmp=%s", tmp_loc.path ? tmp_loc.path : uuid_utoa(tmp_loc.gfid),
                "name=%s", dst_node->name, NULL);
        ret = -1;
        goto out;
    }

    if (gf_uuid_compare(stbuf.ia_gfid, tmp_loc.inode->gfid)) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, DHT_MSG_GFID_MISMATCH, "tmp=%s",
                tmp_loc.path ? tmp_loc.path : uuid_utoa(tmp_loc.gfid),
                "name=%s", dst_node->name, NULL);
        ret = -1;
        goto out;
    }
    ret = 0;

    if (tmp_loc.path == NULL) {
        inode_path(inode, NULL, &path);
        if (path)
            tmp_loc.path = path;
    }

    LOCK(&inode->lock);

    if (list_empty(&inode->fd_list))
        goto unlock;

    /* perform open as root:root. There is window between linkfile
     * creation(root:root) and setattr with the correct uid/gid
     */
    SYNCTASK_SETID(0, 0);

    /* It's possible that we are the last user of iter_fd after each
     * iteration. In this case the fd_unref() of iter_fd at the end of
     * the loop will cause the destruction of the fd. So we need to
     * iterate the list safely because iter_fd cannot be trusted.
     */
    iter_fd = list_entry((&inode->fd_list)->next, typeof(*iter_fd), inode_list);
    while (&iter_fd->inode_list != (&inode->fd_list)) {
        /* We need to release the inode->lock before calling
         * syncop_open() to avoid possible deadlocks. However this
         * can cause the iter_fd to be released by other threads.
         * To avoid this, we take a reference before releasing the
         * lock.
         */

        if (fd_is_anonymous(iter_fd) ||
            (dht_fd_open_on_dst(this, iter_fd, dst_node))) {
            if (!tmp) {
                iter_fd = list_entry(iter_fd->inode_list.next, typeof(*iter_fd),
                                     inode_list);
                continue;
            }
            skip_open = _gf_true;
        }

        /* Yes, this is ugly but there isn't a cleaner way to do this
         * the fd_ref is an atomic increment so not too bad. We want to
         * reduce the number of inode locks and unlocks.
         */

        fd_ref(iter_fd);
        UNLOCK(&inode->lock);

        if (tmp) {
            fd_unref(tmp);
            tmp = NULL;
        }
        if (skip_open)
            goto next;

        /* flags for open are stripped down to allow following the
         * new location of the file, otherwise we can get EEXIST or
         * truncate the file again as rebalance is moving the data */
        ret = syncop_open(dst_node, &tmp_loc,
                          (iter_fd->flags & ~(O_CREAT | O_EXCL | O_TRUNC)),
                          iter_fd, NULL, NULL);
        if (ret < 0) {
            gf_smsg(this->name, GF_LOG_ERROR, -ret,
                    DHT_MSG_OPEN_FD_ON_DST_FAILED, "fd=%p", iter_fd,
                    "flags=0%o", iter_fd->flags, "path=%s", path, "name=%s",
                    dst_node->name, NULL);
            ret = -1;
            open_failed = 1;
        } else {
            /* Potential fd leak if this fails here as it will be
               reopened at the next Phase1/2 check */
            dht_fd_ctx_set(this, iter_fd, dst_node);
        }

    next:
        LOCK(&inode->lock);
        skip_open = _gf_false;
        tmp = iter_fd;
        iter_fd = list_entry(tmp->inode_list.next, typeof(*tmp), inode_list);
    }

    SYNCTASK_SETID(frame->root->uid, frame->root->gid);

unlock:
    UNLOCK(&inode->lock);

    if (tmp) {
        fd_unref(tmp);
        tmp = NULL;
    }
    if (open_failed) {
        ret = -1;
        goto out;
    }

    ret = dht_inode_ctx_set_mig_info(this, inode, src_node, dst_node);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, DHT_MSG_SET_INODE_CTX_FAILED,
                "path=%s", local->loc.path, "name=%s", dst_node->name, NULL);
        goto out;
    }

    ret = 0;
out:
    if (dict) {
        dict_unref(dict);
    }

    loc_wipe(&tmp_loc);
    return ret;
}

int
dht_rebalance_in_progress_check(xlator_t *this, call_frame_t *frame)
{
    int ret = -1;

    ret = synctask_new(this->ctx->env, dht_rebalance_inprogress_task,
                       dht_inprogress_check_done, frame, frame);
    return ret;
}

int
dht_inode_ctx_layout_set(inode_t *inode, xlator_t *this,
                         dht_layout_t *layout_int)
{
    dht_inode_ctx_t *ctx = NULL;
    int ret = -1;

    ret = dht_inode_ctx_get(inode, this, &ctx);
    if (!ret && ctx) {
        ctx->layout = layout_int;
    } else {
        ctx = GF_CALLOC(1, sizeof(*ctx), gf_dht_mt_inode_ctx_t);
        if (!ctx)
            return ret;
        ctx->layout = layout_int;
    }

    ret = dht_inode_ctx_set(inode, this, ctx);

    return ret;
}

void
dht_inode_ctx_time_set(inode_t *inode, xlator_t *this, struct iatt *stat)
{
    dht_inode_ctx_t *ctx = NULL;
    dht_stat_time_t *time = 0;
    int ret = -1;

    ret = dht_inode_ctx_get(inode, this, &ctx);

    if (ret)
        return;

    time = &ctx->time;

    time->mtime = stat->ia_mtime;
    time->mtime_nsec = stat->ia_mtime_nsec;

    time->ctime = stat->ia_ctime;
    time->ctime_nsec = stat->ia_ctime_nsec;

    time->atime = stat->ia_atime;
    time->atime_nsec = stat->ia_atime_nsec;

    return;
}

int
dht_inode_ctx_time_update(inode_t *inode, xlator_t *this, struct iatt *stat,
                          int32_t post)
{
    dht_inode_ctx_t *ctx = NULL;
    dht_stat_time_t *time = 0;
    int ret = -1;

    GF_VALIDATE_OR_GOTO(this->name, stat, out);
    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    ret = dht_inode_ctx_get(inode, this, &ctx);

    if (ret) {
        ctx = GF_CALLOC(1, sizeof(*ctx), gf_dht_mt_inode_ctx_t);
        if (!ctx)
            return -1;
    }

    time = &ctx->time;

    LOCK(&inode->lock);
    {
        DHT_UPDATE_TIME(time->mtime, time->mtime_nsec, stat->ia_mtime,
                        stat->ia_mtime_nsec, post);
        DHT_UPDATE_TIME(time->ctime, time->ctime_nsec, stat->ia_ctime,
                        stat->ia_ctime_nsec, post);
        DHT_UPDATE_TIME(time->atime, time->atime_nsec, stat->ia_atime,
                        stat->ia_atime_nsec, post);
    }
    UNLOCK(&inode->lock);

    ret = dht_inode_ctx_set(inode, this, ctx);
out:
    return 0;
}

int
dht_inode_ctx_get(inode_t *inode, xlator_t *this, dht_inode_ctx_t **ctx)
{
    int ret = -1;
    uint64_t ctx_int = 0;

    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    ret = inode_ctx_get(inode, this, &ctx_int);

    if (ret)
        return ret;

    if (ctx)
        *ctx = (dht_inode_ctx_t *)(uintptr_t)ctx_int;
out:
    return ret;
}

int
dht_inode_ctx_set(inode_t *inode, xlator_t *this, dht_inode_ctx_t *ctx)
{
    int ret = -1;
    uint64_t ctx_int = 0;

    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO(this->name, inode, out);
    GF_VALIDATE_OR_GOTO(this->name, ctx, out);

    ctx_int = (long)ctx;
    ret = inode_ctx_set(inode, this, &ctx_int);
out:
    return ret;
}

int
dht_subvol_status(dht_conf_t *conf, xlator_t *subvol)
{
    int i;

    for (i = 0; i < conf->subvolume_cnt; i++) {
        if (conf->subvolumes[i] == subvol) {
            return conf->subvolume_status[i];
        }
    }
    return 0;
}

inode_t *
dht_heal_path(xlator_t *this, char *path, inode_table_t *itable)
{
    int ret = -1;
    struct iatt iatt = {
        0,
    };
    inode_t *linked_inode = NULL;
    loc_t loc = {
        0,
    };
    char *bname = NULL;
    char *save_ptr = NULL;
    static uuid_t gfid = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    char *tmp_path = NULL;

    tmp_path = gf_strdup(path);
    if (!tmp_path) {
        goto out;
    }

    gf_uuid_copy(loc.pargfid, gfid);
    loc.parent = inode_ref(itable->root);

    bname = strtok_r(tmp_path, "/", &save_ptr);

    /* sending a lookup on parent directory,
     * Eg:  if  path is like /a/b/c/d/e/f/g/
     * then we will send a lookup on a first and then b,c,d,etc
     */

    while (bname) {
        linked_inode = NULL;
        loc.inode = inode_grep(itable, loc.parent, bname);
        if (loc.inode == NULL) {
            loc.inode = inode_new(itable);
            if (loc.inode == NULL) {
                ret = -ENOMEM;
                goto out;
            }
        } else {
            /*
             * Inode is already populated in the inode table.
             * Which means we already looked up the inode and
             * linked with a dentry. So that we will skip
             * lookup on this entry, and proceed to next.
             */
            linked_inode = loc.inode;
            bname = strtok_r(NULL, "/", &save_ptr);
            if (!bname) {
                goto out;
            }
            inode_unref(loc.parent);
            loc.parent = loc.inode;
            gf_uuid_copy(loc.pargfid, loc.inode->gfid);
            loc.inode = NULL;
            continue;
        }

        loc.name = bname;
        ret = loc_path(&loc, bname);

        ret = syncop_lookup(this, &loc, &iatt, NULL, NULL, NULL);
        if (ret) {
            gf_smsg(this->name, GF_LOG_INFO, -ret, DHT_MSG_DIR_SELFHEAL_FAILED,
                    "path=%s", path, "subvolume=%s", this->name, "bname=%s",
                    bname, NULL);
            goto out;
        }

        linked_inode = inode_link(loc.inode, loc.parent, bname, &iatt);
        if (!linked_inode)
            goto out;

        loc_wipe(&loc);
        gf_uuid_copy(loc.pargfid, linked_inode->gfid);
        loc.inode = NULL;

        bname = strtok_r(NULL, "/", &save_ptr);
        if (bname)
            loc.parent = linked_inode;
    }
out:
    inode_ref(linked_inode);
    loc_wipe(&loc);
    GF_FREE(tmp_path);

    return linked_inode;
}

int
dht_heal_full_path(void *data)
{
    call_frame_t *heal_frame = data;
    dht_local_t *local = NULL;
    loc_t loc = {
        0,
    };
    dict_t *dict = NULL;
    char *path = NULL;
    int ret = -1;
    xlator_t *source = NULL;
    xlator_t *this = NULL;
    inode_table_t *itable = NULL;
    inode_t *inode = NULL;
    inode_t *tmp_inode = NULL;

    GF_VALIDATE_OR_GOTO("DHT", heal_frame, out);

    local = heal_frame->local;
    this = heal_frame->this;
    source = heal_frame->cookie;
    heal_frame->cookie = NULL;
    gf_uuid_copy(loc.gfid, local->gfid);

    if (local->loc.inode)
        loc.inode = inode_ref(local->loc.inode);
    else
        goto out;

    itable = loc.inode->table;
    ret = syncop_getxattr(source, &loc, &dict, GET_ANCESTRY_PATH_KEY, NULL,
                          NULL);
    if (ret) {
        gf_smsg(this->name, GF_LOG_INFO, -ret, DHT_MSG_DIR_HEAL_ABORT,
                "subvol=%s", source->name, NULL);
        goto out;
    }

    ret = dict_get_str(dict, GET_ANCESTRY_PATH_KEY, &path);
    if (path) {
        inode = dht_heal_path(this, path, itable);
        if (inode && inode != local->inode) {
            /*
             * if inode returned by heal function is different
             * from what we passed, which means a racing thread
             * already linked a different inode for dentry.
             * So we will update our local->inode, so that we can
             * retrurn proper inode.
             */
            tmp_inode = local->inode;
            local->inode = inode;
            inode_unref(tmp_inode);
            tmp_inode = NULL;
        } else {
            inode_unref(inode);
        }
    }

out:
    loc_wipe(&loc);
    if (dict)
        dict_unref(dict);
    return 0;
}

int
dht_heal_full_path_done(int op_ret, call_frame_t *heal_frame, void *data)
{
    call_frame_t *main_frame = NULL;
    dht_local_t *local = NULL;
    xlator_t *this = NULL;
    int ret = -1;
    int op_errno = 0;

    local = heal_frame->local;
    main_frame = local->main_frame;
    local->main_frame = NULL;
    this = heal_frame->this;

    dht_set_fixed_dir_stat(&local->postparent);
    if (local->need_xattr_heal) {
        local->need_xattr_heal = 0;
        ret = dht_dir_xattr_heal(this, local, &op_errno);
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                    DHT_MSG_DIR_XATTR_HEAL_FAILED, "path=%s", local->loc.path,
                    NULL);
        }
    }

    DHT_STACK_UNWIND(lookup, main_frame, 0, 0, local->inode, &local->stbuf,
                     local->xattr, &local->postparent);

    DHT_STACK_DESTROY(heal_frame);
    return 0;
}

/* This function must be called inside an inode lock */
int
__dht_lock_subvol_set(inode_t *inode, xlator_t *this, xlator_t *lock_subvol)
{
    dht_inode_ctx_t *ctx = NULL;
    int ret = -1;
    uint64_t value = 0;

    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    ret = __inode_ctx_get0(inode, this, &value);
    if (ret || !value) {
        return -1;
    }

    ctx = (dht_inode_ctx_t *)(uintptr_t)value;
    ctx->lock_subvol = lock_subvol;
out:
    return ret;
}

xlator_t *
dht_get_lock_subvolume(xlator_t *this, struct gf_flock *lock,
                       dht_local_t *local)
{
    xlator_t *subvol = NULL;
    inode_t *inode = NULL;
    int32_t ret = -1;
    uint64_t value = 0;
    xlator_t *cached_subvol = NULL;
    dht_inode_ctx_t *ctx = NULL;
    char gfid[GF_UUID_BUF_SIZE] = {0};

    GF_VALIDATE_OR_GOTO(this->name, lock, out);
    GF_VALIDATE_OR_GOTO(this->name, local, out);

    cached_subvol = local->cached_subvol;

    if (local->loc.inode || local->fd) {
        inode = local->loc.inode ? local->loc.inode : local->fd->inode;
    }

    if (!inode)
        goto out;

    if (!(IA_ISDIR(inode->ia_type) || IA_ISINVAL(inode->ia_type))) {
        /*
         * We may get non-linked inode for directories as part
         * of the selfheal code path. So checking  for IA_INVAL
         * type also. This will only happen for directory.
         */
        subvol = local->cached_subvol;
        goto out;
    }

    if (lock->l_type != F_UNLCK) {
        /*
         * inode purging might happen on NFS between a lk
         * and unlk. Due to this lk and unlk might be sent
         * to different subvols.
         * So during a lock request, taking a ref on inode
         * to prevent inode purging. inode unref will happen
         * in unlock cbk code path.
         */
        inode_ref(inode);
    }

    LOCK(&inode->lock);
    ret = __inode_ctx_get0(inode, this, &value);
    if (!ret && value) {
        ctx = (dht_inode_ctx_t *)(uintptr_t)value;
        subvol = ctx->lock_subvol;
    }
    if (!subvol && lock->l_type != F_UNLCK && cached_subvol) {
        ret = __dht_lock_subvol_set(inode, this, cached_subvol);
        if (ret) {
            gf_uuid_unparse(inode->gfid, gfid);
            UNLOCK(&inode->lock);
            gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_SET_INODE_CTX_FAILED,
                    "lock_subvol gfid=%s", gfid, NULL);
            goto post_unlock;
        }
        subvol = cached_subvol;
    }
    UNLOCK(&inode->lock);
post_unlock:
    if (!subvol && inode && lock->l_type != F_UNLCK) {
        inode_unref(inode);
    }
out:
    return subvol;
}

int
dht_lk_inode_unref(call_frame_t *frame, int32_t op_ret)
{
    int ret = -1;
    dht_local_t *local = NULL;
    inode_t *inode = NULL;
    xlator_t *this = NULL;
    char gfid[GF_UUID_BUF_SIZE] = {0};

    local = frame->local;
    this = frame->this;

    if (local->loc.inode || local->fd) {
        inode = local->loc.inode ? local->loc.inode : local->fd->inode;
    }
    if (!inode) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_LOCK_INODE_UNREF_FAILED,
                NULL);
        goto out;
    }

    if (!(IA_ISDIR(inode->ia_type) || IA_ISINVAL(inode->ia_type))) {
        ret = 0;
        goto out;
    }

    switch (local->lock_type) {
        case F_RDLCK:
        case F_WRLCK:
            if (op_ret) {
                gf_uuid_unparse(inode->gfid, gfid);
                gf_msg_debug(this->name, 0, "lock request failed for gfid %s",
                             gfid);
                inode_unref(inode);
                goto out;
            }
            break;

        case F_UNLCK:
            if (!op_ret) {
                inode_unref(inode);
            } else {
                gf_uuid_unparse(inode->gfid, gfid);
                gf_smsg(this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_LOCK_INODE_UNREF_FAILED, "gfid=%s", gfid, NULL);
                goto out;
            }
        default:
            break;
    }
    ret = 0;
out:
    return ret;
}

/* Code to update custom extended attributes from src dict to dst dict
 */
void
dht_dir_set_heal_xattr(xlator_t *this, dht_local_t *local, dict_t *dst,
                       dict_t *src, int *uret, int *uflag)
{
    int ret = -1;
    data_t *keyval = NULL;
    int luret = -1;
    int luflag = -1;
    int i = 0;
    char **xattrs_to_heal;

    if (!src || !dst) {
        gf_smsg(this->name, GF_LOG_WARNING, EINVAL, DHT_MSG_DST_NULL_SET_FAILED,
                "path=%s", local->loc.path, NULL);
        return;
    }
    /* Check if any user xattr present in src dict and set
       it to dst dict
    */
    luret = dict_foreach_fnmatch(src, "user.*", dht_set_user_xattr, dst);
    /* Check if any other custom xattr present in src dict
       and set it to dst dict, here index start from 1 because
       user xattr already checked in previous statement
    */

    xattrs_to_heal = get_xattrs_to_heal();

    for (i = 1; xattrs_to_heal[i]; i++) {
        keyval = dict_get(src, xattrs_to_heal[i]);
        if (keyval) {
            luflag = 1;
            ret = dict_set(dst, xattrs_to_heal[i], keyval);
            if (ret)
                gf_smsg(this->name, GF_LOG_WARNING, ENOMEM,
                        DHT_MSG_DICT_SET_FAILED, "key=%s", xattrs_to_heal[i],
                        "path=%s", local->loc.path, NULL);
            keyval = NULL;
        }
    }
    if (uret)
        (*uret) = luret;
    if (uflag)
        (*uflag) = luflag;
}
