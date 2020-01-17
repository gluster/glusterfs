/*
  Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "snapview-client.h"
#include <glusterfs/inode.h>
#include <glusterfs/byte-order.h>

static void
svc_local_free(svc_local_t *local)
{
    if (local) {
        loc_wipe(&local->loc);
        if (local->fd)
            fd_unref(local->fd);
        if (local->xdata)
            dict_unref(local->xdata);
        mem_put(local);
    }
}

static xlator_t *
svc_get_subvolume(xlator_t *this, int inode_type)
{
    xlator_t *subvolume = NULL;

    GF_VALIDATE_OR_GOTO("snapview-client", this, out);

    if (inode_type == VIRTUAL_INODE)
        subvolume = SECOND_CHILD(this);
    else
        subvolume = FIRST_CHILD(this);

out:
    return subvolume;
}

static int32_t
__svc_inode_ctx_set(xlator_t *this, inode_t *inode, int inode_type)
{
    uint64_t value = 0;
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO("snapview-client", this, out);
    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    value = inode_type;

    ret = __inode_ctx_set(inode, this, &value);

out:
    return ret;
}

static int
__svc_inode_ctx_get(xlator_t *this, inode_t *inode, int *inode_type)
{
    uint64_t value = 0;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("snapview-client", this, out);
    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    ret = __inode_ctx_get(inode, this, &value);
    if (ret < 0)
        goto out;

    *inode_type = (int)(value);

out:
    return ret;
}

static int
svc_inode_ctx_get(xlator_t *this, inode_t *inode, int *inode_type)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO("snapview-client", this, out);
    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    LOCK(&inode->lock);
    {
        ret = __svc_inode_ctx_get(this, inode, inode_type);
    }
    UNLOCK(&inode->lock);

out:
    return ret;
}

static int32_t
svc_inode_ctx_set(xlator_t *this, inode_t *inode, int inode_type)
{
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO("snapview-client", this, out);
    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    LOCK(&inode->lock);
    {
        ret = __svc_inode_ctx_set(this, inode, inode_type);
    }
    UNLOCK(&inode->lock);

out:
    return ret;
}

static svc_fd_t *
svc_fd_new(void)
{
    svc_fd_t *svc_fd = NULL;

    svc_fd = GF_CALLOC(1, sizeof(*svc_fd), gf_svc_mt_svc_fd_t);

    return svc_fd;
}

static svc_fd_t *
__svc_fd_ctx_get(xlator_t *this, fd_t *fd)
{
    svc_fd_t *svc_fd = NULL;
    uint64_t value = 0;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("snapview-client", this, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);

    ret = __fd_ctx_get(fd, this, &value);
    if (ret)
        return NULL;

    svc_fd = (svc_fd_t *)((long)value);

out:
    return svc_fd;
}

static svc_fd_t *
svc_fd_ctx_get(xlator_t *this, fd_t *fd)
{
    svc_fd_t *svc_fd = NULL;

    GF_VALIDATE_OR_GOTO("snapview-client", this, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);

    LOCK(&fd->lock);
    {
        svc_fd = __svc_fd_ctx_get(this, fd);
    }
    UNLOCK(&fd->lock);

out:
    return svc_fd;
}

static int
__svc_fd_ctx_set(xlator_t *this, fd_t *fd, svc_fd_t *svc_fd)
{
    uint64_t value = 0;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("snapview-client", this, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);
    GF_VALIDATE_OR_GOTO(this->name, svc_fd, out);

    value = (uint64_t)(long)svc_fd;

    ret = __fd_ctx_set(fd, this, value);

out:
    return ret;
}

static svc_fd_t *
__svc_fd_ctx_get_or_new(xlator_t *this, fd_t *fd)
{
    svc_fd_t *svc_fd = NULL;
    int ret = -1;
    inode_t *inode = NULL;

    GF_VALIDATE_OR_GOTO("snapview-client", this, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);

    inode = fd->inode;
    svc_fd = __svc_fd_ctx_get(this, fd);
    if (svc_fd) {
        ret = 0;
        goto out;
    }

    svc_fd = svc_fd_new();
    if (!svc_fd) {
        gf_smsg(this->name, GF_LOG_ERROR, ENOMEM, SVC_MSG_ALLOC_FD_FAILED,
                "gfid=%s", uuid_utoa(inode->gfid), NULL);
        goto out;
    }

    ret = __svc_fd_ctx_set(this, fd, svc_fd);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_SET_FD_CONTEXT_FAILED,
                "gfid=%s", uuid_utoa(inode->gfid), NULL);
        ret = -1;
    }

out:
    if (ret) {
        GF_FREE(svc_fd);
        svc_fd = NULL;
    }

    return svc_fd;
}

static svc_fd_t *
svc_fd_ctx_get_or_new(xlator_t *this, fd_t *fd)
{
    svc_fd_t *svc_fd = NULL;

    GF_VALIDATE_OR_GOTO("snapview-client", this, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);

    LOCK(&fd->lock);
    {
        svc_fd = __svc_fd_ctx_get_or_new(this, fd);
    }
    UNLOCK(&fd->lock);

out:
    return svc_fd;
}

/**
 * @this: xlator
 * @entry_point: pointer to the buffer provided by consumer
 *
 * This function is mainly for copying the entry point name
 * (stored as string in priv->path) to a buffer point to by
 * @entry_point within the lock. It is for the consumer to
 * allocate the memory for the buffer.
 *
 * This function is called by all the functions (or fops)
 * who need to use priv->path for avoiding the race.
 * For example, either in lookup or in any other fop,
 * while priv->path is being accessed, a reconfigure can
 * happen to change priv->path. This ensures that, a lock
 * is taken before accessing priv->path.
 **/
int
gf_svc_get_entry_point(xlator_t *this, char *entry_point, size_t dest_size)
{
    int ret = -1;
    svc_private_t *priv = NULL;

    GF_VALIDATE_OR_GOTO("snapview-client", this, out);
    GF_VALIDATE_OR_GOTO(this->name, entry_point, out);

    priv = this->private;

    LOCK(&priv->lock);
    {
        if (dest_size <= strlen(priv->path)) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_STR_LEN,
                    "dest-size=%zu", dest_size, "priv-path-len=%zu",
                    strlen(priv->path), "path=%s", priv->path, NULL);
        } else {
            snprintf(entry_point, dest_size, "%s", priv->path);
            ret = 0;
        }
    }
    UNLOCK(&priv->lock);

out:
    return ret;
}

static int32_t
gf_svc_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *buf, dict_t *xdata, struct iatt *postparent)
{
    svc_local_t *local = NULL;
    xlator_t *subvolume = NULL;
    gf_boolean_t do_unwind = _gf_true;
    int inode_type = -1;
    int ret = -1;

    local = frame->local;
    subvolume = local->subvolume;
    if (!subvolume) {
        gf_msg_callingfn(this->name, GF_LOG_ERROR, 0, SVC_MSG_SUBVOLUME_NULL,
                         "path: %s gfid: %s ", local->loc.path,
                         inode ? uuid_utoa(inode->gfid) : "");
        GF_ASSERT(0);
    }

    /* There is a possibility that, the client process just came online
       and does not have the inode on which the lookup came. In that case,
       the fresh inode created from fuse for the lookup fop, won't have
       the inode context set without which svc cannot decide where to
       STACK_WIND to. So by default it decides to send the fop to the
       regular subvolume (i.e first child of the xlator). If lookup fails
       on the regular volume, then there is a possibility that the lookup
       is happening on a virtual inode (i.e history data residing in snaps).
       So if lookup fails with ENOENT and the inode context is not there,
       then send the lookup to the 2nd child of svc.

       If there are any changes in volfile/client-restarted then inode-ctx
       is lost. In this case if nameless lookup fails with ESTALE,
       then send the lookup to the 2nd child of svc.
    */
    if (op_ret) {
        if (subvolume == FIRST_CHILD(this)) {
            gf_smsg(this->name,
                    (op_errno == ENOENT || op_errno == ESTALE) ? GF_LOG_DEBUG
                                                               : GF_LOG_ERROR,
                    op_errno, SVC_MSG_NORMAL_GRAPH_LOOKUP_FAIL, "error=%s",
                    strerror(op_errno), NULL);
        } else {
            gf_smsg(this->name,
                    (op_errno == ENOENT || op_errno == ESTALE) ? GF_LOG_DEBUG
                                                               : GF_LOG_ERROR,
                    op_errno, SVC_MSG_SNAPVIEW_GRAPH_LOOKUP_FAIL, "error=%s",
                    strerror(op_errno), NULL);
            goto out;
        }

        if ((op_errno == ENOENT || op_errno == ESTALE) &&
            !gf_uuid_is_null(local->loc.gfid)) {
            if (inode != NULL)
                ret = svc_inode_ctx_get(this, inode, &inode_type);

            if (ret < 0 || inode == NULL) {
                gf_msg_debug(this->name, 0,
                             "Lookup on normal graph failed. "
                             " Sending lookup to snapview-server");
                subvolume = SECOND_CHILD(this);
                local->subvolume = subvolume;
                STACK_WIND(frame, gf_svc_lookup_cbk, subvolume,
                           subvolume->fops->lookup, &local->loc, xdata);
                do_unwind = _gf_false;
            }
        }

        goto out;
    }

    if (subvolume == FIRST_CHILD(this))
        inode_type = NORMAL_INODE;
    else
        inode_type = VIRTUAL_INODE;

    ret = svc_inode_ctx_set(this, inode, inode_type);
    if (ret)
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_SET_INODE_CONTEXT_FAILED,
                "gfid=%s", uuid_utoa(inode->gfid), NULL);

out:
    if (do_unwind) {
        SVC_STACK_UNWIND(lookup, frame, op_ret, op_errno, inode, buf, xdata,
                         postparent);
    }

    return 0;
}

static int32_t
gf_svc_lookup(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    int32_t ret = -1;
    svc_local_t *local = NULL;
    xlator_t *subvolume = NULL;
    int op_ret = -1;
    int op_errno = EINVAL;
    inode_t *parent = NULL;
    dict_t *new_xdata = NULL;
    int inode_type = -1;
    int parent_type = -1;
    gf_boolean_t wind = _gf_false;
    char entry_point[NAME_MAX + 1] = {
        0,
    };

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->inode, out);

    ret = svc_inode_ctx_get(this, loc->inode, &inode_type);
    if (!__is_root_gfid(loc->gfid)) {
        if (loc->parent) {
            parent = inode_ref(loc->parent);
            ret = svc_inode_ctx_get(this, loc->parent, &parent_type);
        } else {
            parent = inode_parent(loc->inode, loc->pargfid, NULL);
            if (parent)
                ret = svc_inode_ctx_get(this, parent, &parent_type);
        }
    }

    local = mem_get0(this->local_pool);
    if (!local) {
        op_ret = -1;
        op_errno = ENOMEM;
        gf_smsg(this->name, GF_LOG_ERROR, op_errno, SVC_MSG_NO_MEMORY, NULL);
        goto out;
    }

    frame->local = local;
    loc_copy(&local->loc, loc);

    if (__is_root_gfid(loc->inode->gfid)) {
        subvolume = FIRST_CHILD(this);
        GF_ASSERT(subvolume);
        local->subvolume = subvolume;
        wind = _gf_true;
        goto out;
    }

    /* nfs sends nameless lookups directly using the gfid. In that case
       loc->name will be NULL. So check if loc->name is NULL. If so, then
       try to get the subvolume using inode context. But if the inode has
       not been looked up yet, then send the lookup call to the first
       subvolume.
    */

    if (!loc->name) {
        if (gf_uuid_is_null(loc->inode->gfid)) {
            subvolume = FIRST_CHILD(this);
            local->subvolume = subvolume;
            wind = _gf_true;
            goto out;
        } else {
            if (inode_type >= 0)
                subvolume = svc_get_subvolume(this, inode_type);
            else
                subvolume = FIRST_CHILD(this);
            local->subvolume = subvolume;
            wind = _gf_true;
            goto out;
        }
    }

    if (gf_svc_get_entry_point(this, entry_point, sizeof(entry_point))) {
        gf_smsg(this->name, GF_LOG_WARNING, op_errno,
                SVC_MSG_COPY_ENTRY_POINT_FAILED, NULL);
        goto out;
    }

    if (strcmp(loc->name, entry_point)) {
        if (parent_type == VIRTUAL_INODE) {
            subvolume = SECOND_CHILD(this);
        } else {
            /*
             * Either parent type is normal graph, or the parent
             * type is uncertain.
             */
            subvolume = FIRST_CHILD(this);
        }
        local->subvolume = subvolume;
    } else {
        subvolume = SECOND_CHILD(this);
        local->subvolume = subvolume;
        if (parent_type == NORMAL_INODE) {
            /* Indication of whether the lookup is happening on the
               entry point or not, to the snapview-server.
            */
            SVC_ENTRY_POINT_SET(this, xdata, op_ret, op_errno, new_xdata, ret,
                                out);
        }
    }

    wind = _gf_true;

out:
    if (wind)
        STACK_WIND(frame, gf_svc_lookup_cbk, subvolume, subvolume->fops->lookup,
                   loc, xdata);
    else
        SVC_STACK_UNWIND(lookup, frame, op_ret, op_errno, NULL, NULL, NULL,
                         NULL);
    if (new_xdata)
        dict_unref(new_xdata);

    if (parent)
        inode_unref(parent);

    return 0;
}

static int32_t
gf_svc_statfs(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    xlator_t *subvolume = NULL;
    int32_t ret = -1;
    int inode_type = -1;
    int32_t op_ret = -1;
    int32_t op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;
    svc_private_t *priv = NULL;
    const char *path = NULL;
    int path_len = -1;
    int snap_len = -1;
    loc_t root_loc = {
        0,
    };
    loc_t *temp_loc = NULL;
    char entry_point[NAME_MAX + 1] = {
        0,
    };

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->inode, out);

    priv = this->private;
    SVC_GET_SUBVOL_FROM_CTX(this, op_ret, op_errno, inode_type, ret, loc->inode,
                            subvolume, out);
    path_len = strlen(loc->path);
    snap_len = strlen(priv->path);
    temp_loc = loc;

    if (path_len >= snap_len && inode_type == VIRTUAL_INODE) {
        path = &loc->path[path_len - snap_len];
        if (gf_svc_get_entry_point(this, entry_point, sizeof(entry_point))) {
            gf_smsg(this->name, GF_LOG_WARNING, op_errno,
                    SVC_MSG_COPY_ENTRY_POINT_FAILED, NULL);
            goto out;
        }

        if (!strcmp(path, entry_point)) {
            /*
             * statfs call for virtual snap directory.
             * Sent the fops to parent volume by removing
             * virtual directory from path
             */
            subvolume = FIRST_CHILD(this);
            root_loc.path = gf_strdup("/");
            gf_uuid_clear(root_loc.gfid);
            root_loc.gfid[15] = 1;
            root_loc.inode = inode_ref(loc->inode->table->root);
            temp_loc = &root_loc;
        }
    }

    STACK_WIND_TAIL(frame, subvolume, subvolume->fops->statfs, temp_loc, xdata);
    if (temp_loc == &root_loc)
        loc_wipe(temp_loc);

    wind = _gf_true;
out:
    if (!wind)
        SVC_STACK_UNWIND(statfs, frame, op_ret, op_errno, NULL, NULL);
    return 0;
}

static int32_t
gf_svc_stat_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *buf,
                dict_t *xdata)
{
    /* TODO: FIX ME
     * Consider a testcase:
     * #mount -t nfs host1:/vol1 /mnt
     * #ls /mnt
     * #ls /mnt/.snaps (As expected this fails)
     * #gluster volume set vol1 features.uss enable
     * Now `ls /mnt/.snaps` should work, but fails with No such file or
     * directory. This is because NFS client (gNFS) caches the list of files
     * in a directory. This cache is updated if there are any changes in the
     * directory attributes. So, one way to solve this problem is to change
     * 'ctime' attribute when USS is enabled as below.
     *
     * if (op_ret == 0 && IA_ISDIR(buf->ia_type))
     *     buf->ia_ctime_nsec++;
     *
     * But this is not the ideal solution as applications see the unexpected
     * ctime change causing failures.
     */

    SVC_STACK_UNWIND(stat, frame, op_ret, op_errno, buf, xdata);
    return 0;
}

/* should all the fops be handled like lookup is supposed to be
   handled? i.e just based on inode type decide where the call should
   be sent and in the call back update the contexts.
*/
static int32_t
gf_svc_stat(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
    int32_t ret = -1;
    int inode_type = -1;
    xlator_t *subvolume = NULL;
    int32_t op_ret = -1;
    int32_t op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->inode, out);

    SVC_GET_SUBVOL_FROM_CTX(this, op_ret, op_errno, inode_type, ret, loc->inode,
                            subvolume, out);

    STACK_WIND(frame, gf_svc_stat_cbk, subvolume, subvolume->fops->stat, loc,
               xdata);

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(stat, frame, op_ret, op_errno, NULL, NULL);
    return 0;
}

static int32_t
gf_svc_fstat(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    int32_t ret = -1;
    int inode_type = -1;
    xlator_t *subvolume = NULL;
    int32_t op_ret = -1;
    int32_t op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);
    GF_VALIDATE_OR_GOTO(this->name, fd->inode, out);

    SVC_GET_SUBVOL_FROM_CTX(this, op_ret, op_errno, inode_type, ret, fd->inode,
                            subvolume, out);

    STACK_WIND_TAIL(frame, subvolume, subvolume->fops->fstat, fd, xdata);

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(fstat, frame, op_ret, op_errno, NULL, NULL);

    return ret;
}

static int32_t
gf_svc_opendir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
    svc_fd_t *svc_fd = NULL;
    svc_local_t *local = NULL;
    svc_private_t *priv = NULL;
    gf_boolean_t special_dir = _gf_false;
    char path[PATH_MAX] = {
        0,
    };

    GF_VALIDATE_OR_GOTO("snapview-client", this, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

    if (op_ret)
        goto out;

    priv = this->private;
    local = frame->local;

    if (local->subvolume == FIRST_CHILD(this) && priv->special_dir &&
        strcmp(priv->special_dir, "")) {
        if (!__is_root_gfid(fd->inode->gfid))
            snprintf(path, sizeof(path), "%s/.", priv->special_dir);
        else
            snprintf(path, sizeof(path), "/.");

        if (!strcmp(local->loc.path, priv->special_dir) ||
            !strcmp(local->loc.path, path)) {
            gf_msg_debug(this->name, 0,
                         "got opendir on special directory"
                         " %s (gfid: %s)",
                         path, uuid_utoa(fd->inode->gfid));
            special_dir = _gf_true;
        }
    }

    if (special_dir) {
        svc_fd = svc_fd_ctx_get_or_new(this, fd);
        if (!svc_fd) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_GET_FD_CONTEXT_FAILED,
                    "gfid=%s", uuid_utoa(fd->inode->gfid), NULL);
            goto out;
        }

        svc_fd->last_offset = -1;
        svc_fd->special_dir = special_dir;
    }

out:
    STACK_UNWIND_STRICT(opendir, frame, op_ret, op_errno, fd, xdata);

    return 0;
}

/* If the inode represents a directory which is actually
   present in a snapshot, then opendir on that directory
   should be sent to the snap-view-server which opens
   the directory in the corresponding graph.
   In fact any opendir call on a virtual directory
   should be sent to svs. Because if it fakes success
   here, then later when readdir on that fd comes, there
   will not be any corresponding fd opened on svs and
   svc has to do things that open-behind is doing.
*/
static int32_t
gf_svc_opendir(call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
               dict_t *xdata)
{
    int32_t ret = -1;
    int inode_type = -1;
    xlator_t *subvolume = NULL;
    int op_ret = -1;
    int op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;
    svc_local_t *local = NULL;

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->inode, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);

    local = mem_get0(this->local_pool);
    if (!local) {
        op_errno = ENOMEM;
        gf_smsg(this->name, GF_LOG_ERROR, op_errno, SVC_MSG_NO_MEMORY,
                "path=%s", loc->path, "gfid=%s", uuid_utoa(fd->inode->gfid),
                NULL);
        goto out;
    }
    loc_copy(&local->loc, loc);
    frame->local = local;

    SVC_GET_SUBVOL_FROM_CTX(this, op_ret, op_errno, inode_type, ret, loc->inode,
                            subvolume, out);
    local->subvolume = subvolume;

    STACK_WIND(frame, gf_svc_opendir_cbk, subvolume, subvolume->fops->opendir,
               loc, fd, xdata);

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(opendir, frame, op_ret, op_errno, NULL, NULL);

    return 0;
}

static int32_t
gf_svc_setattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
               struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
    int32_t ret = -1;
    int inode_type = -1;
    int op_ret = -1;
    int op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->inode, out);

    ret = svc_inode_ctx_get(this, loc->inode, &inode_type);
    if (ret < 0) {
        op_ret = -1;
        op_errno = EINVAL;
        gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                SVC_MSG_GET_INODE_CONTEXT_FAILED, "path=%s", loc->path,
                "gfid= %s", uuid_utoa(loc->inode->gfid), NULL);
        goto out;
    }

    if (inode_type == NORMAL_INODE) {
        STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                        FIRST_CHILD(this)->fops->setattr, loc, stbuf, valid,
                        xdata);
    } else {
        op_ret = -1;
        op_errno = EROFS;
        goto out;
    }

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(setattr, frame, op_ret, op_errno, NULL, NULL, NULL);
    return 0;
}

/* XXX: This function is currently not used. Remove "#if 0" when required */
#if 0
static int32_t
gf_svc_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        int32_t      ret        = -1;
        int          inode_type = -1;
        int          op_ret     = -1;
        int          op_errno   = EINVAL;
        gf_boolean_t wind       = _gf_false;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, out);

        ret = svc_inode_ctx_get (this, fd->inode, &inode_type);
        if (ret < 0) {
                op_ret = -1;
                op_errno = EINVAL;
                gf_msg (this->name, GF_LOG_ERROR, op_errno,
                        SVC_MSG_GET_INODE_CONTEXT_FAILED, "failed to "
                        "get the inode context for %s",
                        uuid_utoa (fd->inode->gfid));
                goto out;
        }

        if (inode_type == NORMAL_INODE) {
                STACK_WIND_TAIL (frame, FIRST_CHILD (this),
                                 FIRST_CHILD (this)->fops->fsetattr, fd, stbuf,
                                 valid, xdata);
        } else {
                op_ret = -1;
                op_errno = EROFS;
                goto out;
        }

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (fsetattr, frame, op_ret, op_errno,
                                  NULL, NULL, NULL);
        return 0;
}
#endif /* gf_svc_fsetattr() is not used */

static int32_t
gf_svc_getxattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name, dict_t *xdata)
{
    int32_t ret = -1;
    int inode_type = -1;
    xlator_t *subvolume = NULL;
    int op_ret = -1;
    int op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;
    svc_private_t *priv = NULL;
    char attrname[PATH_MAX] = "";
    char attrval[64] = "";
    dict_t *dict = NULL;
    char entry_point[NAME_MAX + 1] = {
        0,
    };

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->inode, out);
    priv = this->private;
    GF_VALIDATE_OR_GOTO(this->name, priv, out);

    /*
     * Samba sends this special key for case insensitive
     * filename check. This request comes with a parent
     * path and with a special key GF_XATTR_GET_REAL_FILENAME_KEY.
     * e.g. "glusterfs.get_real_filename:.snaps".
     * If the name variable matches this key then we have
     * to send back .snaps as the real filename.
     */
    if (!name)
        goto stack_wind;

    sscanf(name, "%[^:]:%[^@]", attrname, attrval);
    strcat(attrname, ":");

    if (!strcmp(attrname, GF_XATTR_GET_REAL_FILENAME_KEY)) {
        if (gf_svc_get_entry_point(this, entry_point, sizeof(entry_point))) {
            gf_smsg(this->name, GF_LOG_WARNING, op_errno,
                    SVC_MSG_COPY_ENTRY_POINT_FAILED, NULL);
            goto out;
        }

        if (!strcasecmp(attrval, entry_point)) {
            dict = dict_new();
            if (NULL == dict) {
                op_errno = ENOMEM;
                goto out;
            }

            ret = dict_set_dynstr_with_alloc(dict, (char *)name, entry_point);

            if (ret) {
                op_errno = ENOMEM;
                goto out;
            }

            op_errno = 0;
            op_ret = strlen(entry_point) + 1;
            /* We should return from here */
            goto out;
        }
    }
stack_wind:
    SVC_GET_SUBVOL_FROM_CTX(this, op_ret, op_errno, inode_type, ret, loc->inode,
                            subvolume, out);

    STACK_WIND_TAIL(frame, subvolume, subvolume->fops->getxattr, loc, name,
                    xdata);

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(getxattr, frame, op_ret, op_errno, dict, NULL);

    if (dict)
        dict_unref(dict);

    return 0;
}

/* XXX: This function is currently not used. Mark it '#if 0' when required */
#if 0
static int32_t
gf_svc_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                  const char *name, dict_t *xdata)
{
        int32_t       ret        = -1;
        int           inode_type = -1;
        xlator_t     *subvolume  = NULL;
        gf_boolean_t  wind       = _gf_false;
        int           op_ret     = -1;
        int           op_errno   = EINVAL;

        GF_VALIDATE_OR_GOTO ("svc", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, out);

        SVC_GET_SUBVOL_FROM_CTX (this, op_ret, op_errno, inode_type, ret,
                                 fd->inode, subvolume, out);

        STACK_WIND_TAIL (frame, subvolume,
                         subvolume->fops->fgetxattr, fd, name, xdata);

        wind = _gf_true;

out:
        if (!wind)
                SVC_STACK_UNWIND (fgetxattr, frame, op_ret, op_errno,
                                  NULL, NULL);
        return 0;
}
#endif /* gf_svc_fgetxattr() is not used */

static int32_t
gf_svc_setxattr(call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
                int32_t flags, dict_t *xdata)
{
    int32_t ret = -1;
    int inode_type = -1;
    int op_ret = -1;
    int op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->inode, out);

    ret = svc_inode_ctx_get(this, loc->inode, &inode_type);
    if (ret < 0) {
        op_ret = -1;
        op_errno = EINVAL;
        gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                SVC_MSG_GET_INODE_CONTEXT_FAILED, "name=%s", loc->name,
                "gfid=%s", uuid_utoa(loc->inode->gfid), NULL);
        goto out;
    }

    if (inode_type == NORMAL_INODE) {
        STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                        FIRST_CHILD(this)->fops->setxattr, loc, dict, flags,
                        xdata);
    } else {
        op_ret = -1;
        op_errno = EROFS;
        goto out;
    }

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(setxattr, frame, op_ret, op_errno, NULL);

    return 0;
}

static int32_t
gf_svc_fsetxattr(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
                 int32_t flags, dict_t *xdata)
{
    int32_t ret = -1;
    int inode_type = -1;
    int op_ret = -1;
    int op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);
    GF_VALIDATE_OR_GOTO(this->name, fd->inode, out);

    ret = svc_inode_ctx_get(this, fd->inode, &inode_type);
    if (ret < 0) {
        op_ret = -1;
        op_errno = EINVAL;
        gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                SVC_MSG_GET_INODE_CONTEXT_FAILED, "gfid=%s",
                uuid_utoa(fd->inode->gfid), NULL);
        goto out;
    }

    if (inode_type == NORMAL_INODE) {
        STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                        FIRST_CHILD(this)->fops->fsetxattr, fd, dict, flags,
                        xdata);
    } else {
        op_ret = -1;
        op_errno = EROFS;
        goto out;
    }

    wind = _gf_true;

out:
    if (!wind)
        STACK_UNWIND_STRICT(fsetxattr, frame, op_ret, op_errno, NULL);

    return 0;
}

static int32_t
gf_svc_rmdir(call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
             dict_t *xdata)
{
    int inode_type = -1;
    int ret = -1;
    int op_ret = -1;
    int op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->inode, out);

    ret = svc_inode_ctx_get(this, loc->inode, &inode_type);
    if (ret < 0) {
        op_ret = -1;
        op_errno = EINVAL;
        gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                SVC_MSG_GET_INODE_CONTEXT_FAILED, "name=%s", loc->name,
                "gfid=%s", uuid_utoa(loc->inode->gfid), NULL);
        goto out;
    }

    if (inode_type == NORMAL_INODE) {
        STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                        FIRST_CHILD(this)->fops->rmdir, loc, flags, xdata);
    } else {
        op_ret = -1;
        op_errno = EROFS;
        goto out;
    }

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(rmdir, frame, op_ret, op_errno, NULL, NULL, NULL);
    return 0;
}

static int32_t
gf_svc_mkdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *buf, struct iatt *preparent,
                 struct iatt *postparent, dict_t *xdata)
{
    int inode_type = -1;
    int ret = -1;

    if (op_ret < 0)
        goto out;

    inode_type = NORMAL_INODE;
    ret = svc_inode_ctx_set(this, inode, inode_type);
    if (ret)
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_SET_INODE_CONTEXT_FAILED,
                NULL);

out:
    SVC_STACK_UNWIND(mkdir, frame, op_ret, op_errno, inode, buf, preparent,
                     postparent, xdata);
    return 0;
}

static int32_t
gf_svc_mkdir(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
             mode_t umask, dict_t *xdata)
{
    int parent_type = -1;
    int ret = -1;
    int op_ret = -1;
    int op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;
    char entry_point[NAME_MAX + 1] = {
        0,
    };

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->inode, out);

    ret = svc_inode_ctx_get(this, loc->parent, &parent_type);
    if (ret < 0) {
        op_ret = -1;
        op_errno = EINVAL;
        gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                SVC_MSG_GET_INODE_CONTEXT_FAILED, "gfid=%s",
                uuid_utoa(loc->parent->gfid), NULL);
        goto out;
    }

    if (gf_svc_get_entry_point(this, entry_point, sizeof(entry_point))) {
        gf_smsg(this->name, GF_LOG_WARNING, op_errno,
                SVC_MSG_COPY_ENTRY_POINT_FAILED, NULL);
        goto out;
    }

    if (strcmp(loc->name, entry_point) && parent_type == NORMAL_INODE) {
        STACK_WIND(frame, gf_svc_mkdir_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->mkdir, loc, mode, umask, xdata);
    } else {
        op_ret = -1;
        op_errno = EROFS;
        goto out;
    }

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(mkdir, frame, op_ret, op_errno, NULL, NULL, NULL, NULL,
                         NULL);
    return 0;
}

static int32_t
gf_svc_mknod_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *buf, struct iatt *preparent,
                 struct iatt *postparent, dict_t *xdata)
{
    int inode_type = -1;
    int ret = -1;

    if (op_ret < 0)
        goto out;

    inode_type = NORMAL_INODE;
    ret = svc_inode_ctx_set(this, inode, inode_type);
    if (ret)
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_SET_INODE_CONTEXT_FAILED,
                NULL);

out:
    SVC_STACK_UNWIND(mknod, frame, op_ret, op_errno, inode, buf, preparent,
                     postparent, xdata);
    return 0;
}

static int32_t
gf_svc_mknod(call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
             dev_t rdev, mode_t umask, dict_t *xdata)
{
    int parent_type = -1;
    int ret = -1;
    int op_ret = -1;
    int op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;
    char entry_point[NAME_MAX + 1] = {
        0,
    };

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->inode, out);

    ret = svc_inode_ctx_get(this, loc->parent, &parent_type);
    if (ret < 0) {
        op_ret = -1;
        op_errno = EINVAL;
        gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                SVC_MSG_GET_INODE_CONTEXT_FAILED, "gfid=%s",
                uuid_utoa(loc->parent->gfid), NULL);
        goto out;
    }

    if (gf_svc_get_entry_point(this, entry_point, sizeof(entry_point))) {
        gf_smsg(this->name, GF_LOG_WARNING, op_errno,
                SVC_MSG_COPY_ENTRY_POINT_FAILED, NULL);
        goto out;
    }

    if (strcmp(loc->name, entry_point) && parent_type == NORMAL_INODE) {
        STACK_WIND(frame, gf_svc_mknod_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->mknod, loc, mode, rdev, umask,
                   xdata);
    } else {
        op_ret = -1;
        op_errno = EROFS;
        goto out;
    }

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(mknod, frame, op_ret, op_errno, NULL, NULL, NULL, NULL,
                         NULL);
    return 0;
}

/* If the flags of the open call contain O_WRONLY or O_RDWR and the inode is
   a virtual inode, then unwind the call back with EROFS. Otherwise simply
   STACK_WIND the call to the first child of svc xlator.
*/
static int32_t
gf_svc_open(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
            fd_t *fd, dict_t *xdata)
{
    xlator_t *subvolume = NULL;
    int inode_type = -1;
    int op_ret = -1;
    int op_errno = EINVAL;
    int ret = -1;
    gf_boolean_t wind = _gf_false;

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->inode, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);

    /* Another way is to STACK_WIND to normal subvolume, if inode
       type is not there in the context. If the file actually resides
       in snapshots, then ENOENT would be returned. Needs more analysis.
    */
    SVC_GET_SUBVOL_FROM_CTX(this, op_ret, op_errno, inode_type, ret, loc->inode,
                            subvolume, out);

    if (((flags & O_ACCMODE) == O_WRONLY) || ((flags & O_ACCMODE) == O_RDWR)) {
        if (subvolume != FIRST_CHILD(this)) {
            op_ret = -1;
            op_errno = EINVAL;
            goto out;
        }
    }

    STACK_WIND_TAIL(frame, subvolume, subvolume->fops->open, loc, flags, fd,
                    xdata);

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(open, frame, op_ret, op_errno, NULL, NULL);
    return 0;
}

static int32_t
gf_svc_create_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
                  struct iatt *stbuf, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
    int inode_type = -1;
    int ret = -1;

    if (op_ret < 0)
        goto out;

    inode_type = NORMAL_INODE;
    ret = svc_inode_ctx_set(this, inode, inode_type);
    if (ret)
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_SET_INODE_CONTEXT_FAILED,
                NULL);

out:
    SVC_STACK_UNWIND(create, frame, op_ret, op_errno, fd, inode, stbuf,
                     preparent, postparent, xdata);

    return 0;
}

static int32_t
gf_svc_create(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
              mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
    int parent_type = -1;
    int ret = -1;
    int op_ret = -1;
    int op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;
    char entry_point[NAME_MAX + 1] = {
        0,
    };

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->inode, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);

    ret = svc_inode_ctx_get(this, loc->parent, &parent_type);
    if (ret < 0) {
        op_ret = -1;
        op_errno = EINVAL;
        gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                SVC_MSG_GET_INODE_CONTEXT_FAILED, "gfid=%s",
                uuid_utoa(loc->parent->gfid), NULL);
        goto out;
    }

    if (gf_svc_get_entry_point(this, entry_point, sizeof(entry_point))) {
        gf_smsg(this->name, GF_LOG_WARNING, op_errno,
                SVC_MSG_COPY_ENTRY_POINT_FAILED, NULL);
        goto out;
    }

    if (strcmp(loc->name, entry_point) && parent_type == NORMAL_INODE) {
        STACK_WIND(frame, gf_svc_create_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->create, loc, flags, mode, umask, fd,
                   xdata);
    } else {
        op_ret = -1;
        op_errno = EROFS;
        goto out;
    }

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(create, frame, op_ret, op_errno, NULL, NULL, NULL,
                         NULL, NULL, NULL);
    return 0;
}

static int32_t
gf_svc_symlink_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, inode_t *inode,
                   struct iatt *buf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
    int inode_type = -1;
    int ret = -1;

    if (op_ret < 0)
        goto out;

    inode_type = NORMAL_INODE;
    ret = svc_inode_ctx_set(this, inode, inode_type);
    if (ret)
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_SET_INODE_CONTEXT_FAILED,
                NULL);

out:
    SVC_STACK_UNWIND(symlink, frame, op_ret, op_errno, inode, buf, preparent,
                     postparent, xdata);

    return 0;
}

static int32_t
gf_svc_symlink(call_frame_t *frame, xlator_t *this, const char *linkpath,
               loc_t *loc, mode_t umask, dict_t *xdata)
{
    int parent_type = -1;
    int op_ret = -1;
    int op_errno = EINVAL;
    int ret = -1;
    gf_boolean_t wind = _gf_false;
    char entry_point[NAME_MAX + 1] = {
        0,
    };

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->inode, out);

    ret = svc_inode_ctx_get(this, loc->parent, &parent_type);
    if (ret < 0) {
        op_ret = -1;
        op_errno = EINVAL;
        gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                SVC_MSG_GET_INODE_CONTEXT_FAILED, "gfid=%s",
                uuid_utoa(loc->parent->gfid), NULL);
        goto out;
    }

    if (gf_svc_get_entry_point(this, entry_point, sizeof(entry_point))) {
        gf_smsg(this->name, GF_LOG_WARNING, op_errno,
                SVC_MSG_COPY_ENTRY_POINT_FAILED, NULL);
        goto out;
    }

    if (strcmp(loc->name, entry_point) && parent_type == NORMAL_INODE) {
        STACK_WIND(frame, gf_svc_symlink_cbk, FIRST_CHILD(this),
                   FIRST_CHILD(this)->fops->symlink, linkpath, loc, umask,
                   xdata);
    } else {
        op_ret = -1;
        op_errno = EROFS;
        goto out;
    }

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(symlink, frame, op_ret, op_errno, NULL, NULL, NULL,
                         NULL, NULL);
    return 0;
}

static int32_t
gf_svc_unlink(call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
              dict_t *xdata)
{
    int inode_type = -1;
    int op_ret = -1;
    int op_errno = EINVAL;
    int ret = -1;
    gf_boolean_t wind = _gf_false;

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->inode, out);

    ret = svc_inode_ctx_get(this, loc->inode, &inode_type);
    if (ret < 0) {
        op_ret = -1;
        op_errno = EINVAL;
        gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                SVC_MSG_GET_INODE_CONTEXT_FAILED, "gfid=%s",
                uuid_utoa(loc->parent->gfid), NULL);
        goto out;
    }

    if (inode_type == NORMAL_INODE) {
        STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                        FIRST_CHILD(this)->fops->unlink, loc, flags, xdata);
    } else {
        op_ret = -1;
        op_errno = EROFS;
        goto out;
    }

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(unlink, frame, op_ret, op_errno, NULL, NULL, NULL);
    return 0;
}

static int32_t
gf_svc_readv(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset, uint32_t flags, dict_t *xdata)
{
    int inode_type = -1;
    xlator_t *subvolume = NULL;
    int ret = -1;
    int op_ret = -1;
    int op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);
    GF_VALIDATE_OR_GOTO(this->name, fd->inode, out);

    SVC_GET_SUBVOL_FROM_CTX(this, op_ret, op_errno, inode_type, ret, fd->inode,
                            subvolume, out);

    STACK_WIND_TAIL(frame, subvolume, subvolume->fops->readv, fd, size, offset,
                    flags, xdata);

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(readv, frame, op_ret, op_errno, NULL, 0, NULL, NULL,
                         NULL);
    return 0;
}

static int32_t
gf_svc_readlink(call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
                dict_t *xdata)
{
    int inode_type = -1;
    xlator_t *subvolume = NULL;
    int ret = -1;
    int op_ret = -1;
    int op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->inode, out);

    SVC_GET_SUBVOL_FROM_CTX(this, op_ret, op_errno, inode_type, ret, loc->inode,
                            subvolume, out);

    STACK_WIND_TAIL(frame, subvolume, subvolume->fops->readlink, loc, size,
                    xdata);

    wind = _gf_true;

out:
    if (!wind)
        STACK_UNWIND_STRICT(readlink, frame, op_ret, op_errno, NULL, NULL,
                            NULL);
    return 0;
}

static int32_t
gf_svc_access(call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
              dict_t *xdata)
{
    int ret = -1;
    int inode_type = -1;
    xlator_t *subvolume = NULL;
    int op_ret = -1;
    int op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->inode, out);

    SVC_GET_SUBVOL_FROM_CTX(this, op_ret, op_errno, inode_type, ret, loc->inode,
                            subvolume, out);

    STACK_WIND_TAIL(frame, subvolume, subvolume->fops->access, loc, mask,
                    xdata);

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(access, frame, op_ret, op_errno, NULL);

    return 0;
}

int32_t
gf_svc_readdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                   dict_t *xdata)
{
    gf_dirent_t *entry = NULL;
    gf_dirent_t *tmpentry = NULL;
    svc_local_t *local = NULL;
    char entry_point[NAME_MAX + 1] = {
        0,
    };

    if (op_ret < 0)
        goto out;

    local = frame->local;

    /* If .snaps pre-exists, then it should not be listed
     * in the NORMAL INODE directory when USS is enabled,
     * so filter the .snaps entry if exists.
     * However it is OK to list .snaps in VIRTUAL world
     */
    if (local->subvolume != FIRST_CHILD(this))
        goto out;

    /*
     * Better to goto out if getting the entry point
     * fails. We might end up sending the directory
     * entry for the snapview entry point in the readdir
     * response. But, the intention is to avoid the race
     * condition where priv->path is being changed in
     * reconfigure while this is accessing it.
     */
    if (gf_svc_get_entry_point(this, entry_point, sizeof(entry_point))) {
        gf_smsg(this->name, GF_LOG_WARNING, op_errno,
                SVC_MSG_COPY_ENTRY_POINT_FAILED, NULL);
        goto out;
    }

    list_for_each_entry_safe(entry, tmpentry, &entries->list, list)
    {
        if (strcmp(entry_point, entry->d_name) == 0)
            gf_dirent_entry_free(entry);
    }

out:
    SVC_STACK_UNWIND(readdir, frame, op_ret, op_errno, entries, xdata);
    return 0;
}

static int32_t
gf_svc_readdir(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
               off_t off, dict_t *xdata)
{
    int inode_type = -1;
    xlator_t *subvolume = NULL;
    svc_local_t *local = NULL;
    int ret = -1;
    int op_ret = -1;
    int op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;
    svc_fd_t *svc_fd = NULL;
    gf_dirent_t entries;

    INIT_LIST_HEAD(&entries);

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);
    GF_VALIDATE_OR_GOTO(this->name, fd->inode, out);

    svc_fd = svc_fd_ctx_get_or_new(this, fd);
    if (!svc_fd)
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_GET_FD_CONTEXT_FAILED,
                "gfid=%s", uuid_utoa(fd->inode->gfid), NULL);
    else {
        if (svc_fd->entry_point_handled && off == svc_fd->last_offset) {
            op_ret = 0;
            op_errno = ENOENT;
            goto out;
        }
    }

    SVC_GET_SUBVOL_FROM_CTX(this, op_ret, op_errno, inode_type, ret, fd->inode,
                            subvolume, out);

    local = mem_get0(this->local_pool);
    if (!local) {
        gf_smsg(this->name, GF_LOG_ERROR, op_errno, SVC_MSG_NO_MEMORY,
                "inode-gfid=%s", uuid_utoa(fd->inode->gfid), NULL);
        goto out;
    }
    local->subvolume = subvolume;
    frame->local = local;

    STACK_WIND(frame, gf_svc_readdir_cbk, subvolume, subvolume->fops->readdir,
               fd, size, off, xdata);

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(readdir, frame, op_ret, op_errno, &entries, NULL);

    gf_dirent_free(&entries);

    return 0;
}

/*
 * This lookup if mainly for supporting USS for windows.
 * Since the dentry for the entry-point directory is not sent in
 * the readdir response, from windows explorer, there is no way
 * to access the snapshots. If the explicit path of the entry-point
 * directory is mentioned in the address bar, then windows sends
 * readdir on the parent directory and compares if the entry point
 * directory's name is there in readdir response. If it is not there
 * then access to snapshot world is denied. And windows users cannot
 * access snapshots via samba.
 * So, to handle this a new option called special-directory is created,
 * which if set, snapview-client will send the entry-point's dentry
 * in readdirp o/p for the special directory, so that it will be
 * visible from windows explorer.
 * But to send that virtual entry, the following mechanism is used.
 * 1) Check if readdir from posix is over.
 * 2) If so, then send a lookup on entry point directory to snap daemon
 * (this is needed because in readdirp inodes are linked, so we need to
 * maintain 1:1 mapping between inodes (gfids) from snapview server to
 * snapview client).
 * 3) Once successful lookup response received, send a new entry to
 * windows.
 */

static int32_t
gf_svc_readdirp_lookup_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, inode_t *inode,
                           struct iatt *buf, dict_t *xdata,
                           struct iatt *postparent)
{
    gf_dirent_t entries;
    gf_dirent_t *entry = NULL;
    svc_fd_t *svc_fd = NULL;
    svc_local_t *local = NULL;
    int inode_type = -1;
    int ret = -1;
    char entry_point[NAME_MAX + 1] = {
        0,
    };

    GF_VALIDATE_OR_GOTO("snapview-client", this, out);

    INIT_LIST_HEAD(&entries.list);

    local = frame->local;

    if (op_ret) {
        if (op_errno == ESTALE && !local->revalidate) {
            local->revalidate = 1;
            ret = gf_svc_special_dir_revalidate_lookup(frame, this, xdata);

            if (!ret)
                return 0;
        }
        op_ret = 0;
        op_errno = ENOENT;
        goto out;
    }

    svc_fd = svc_fd_ctx_get(this, local->fd);
    if (!svc_fd) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_GET_FD_CONTEXT_FAILED,
                "gfid=%s", uuid_utoa(local->fd->inode->gfid), NULL);
        op_ret = 0;
        op_errno = ENOENT;
        goto out;
    }

    if (gf_svc_get_entry_point(this, entry_point, sizeof(entry_point))) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, SVC_MSG_COPY_ENTRY_POINT_FAILED,
                NULL);
        op_ret = 0;
        op_errno = ENOENT;
        goto out;
    }

    entry = gf_dirent_for_name(entry_point);
    if (!entry) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_NO_MEMORY,
                "entry-point=%s", entry_point, NULL);
        op_ret = 0;
        op_errno = ENOMEM;
        goto out;
    }

    entry->inode = inode_ref(inode);
    entry->d_off = svc_fd->last_offset + 22;
    entry->d_ino = buf->ia_ino;
    entry->d_type = DT_DIR;
    entry->d_stat = *buf;
    inode_type = VIRTUAL_INODE;
    ret = svc_inode_ctx_set(this, entry->inode, inode_type);
    if (ret)
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_SET_INODE_CONTEXT_FAILED,
                "entry-name=%s", entry->d_name, NULL);

    list_add_tail(&entry->list, &entries.list);
    op_ret = 1;
    svc_fd->last_offset = entry->d_off;
    svc_fd->entry_point_handled = _gf_true;

out:
    SVC_STACK_UNWIND(readdirp, frame, op_ret, op_errno, &entries,
                     local ? local->xdata : NULL);

    gf_dirent_free(&entries);

    return 0;
}

int
gf_svc_special_dir_revalidate_lookup(call_frame_t *frame, xlator_t *this,
                                     dict_t *xdata)
{
    svc_local_t *local = NULL;
    loc_t *loc = NULL;
    dict_t *tmp_xdata = NULL;
    char *path = NULL;
    int ret = -1;
    char entry_point[NAME_MAX + 1] = {
        0,
    };

    GF_VALIDATE_OR_GOTO("snapview-client", this, out);

    local = frame->local;
    loc = &local->loc;

    if (local->xdata) {
        dict_unref(local->xdata);
        local->xdata = NULL;
    }

    if (xdata)
        local->xdata = dict_ref(xdata);

    inode_unref(loc->inode);
    loc->inode = inode_new(loc->parent->table);
    if (!loc->inode) {
        gf_smsg(this->name, GF_LOG_ERROR, ENOMEM, SVC_MSG_ALLOC_INODE_FAILED,
                NULL);
        goto out;
    }

    if (gf_svc_get_entry_point(this, entry_point, sizeof(entry_point))) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, SVC_MSG_COPY_ENTRY_POINT_FAILED,
                NULL);
        goto out;
    }

    gf_uuid_copy(local->loc.gfid, loc->inode->gfid);
    ret = inode_path(loc->parent, entry_point, &path);
    if (ret < 0)
        goto out;

    if (loc->path)
        GF_FREE((char *)loc->path);

    loc->path = gf_strdup(path);
    if (loc->path) {
        if (!loc->name || (loc->name && !strcmp(loc->name, ""))) {
            loc->name = strrchr(loc->path, '/');
            if (loc->name)
                loc->name++;
        }
    } else
        loc->path = NULL;

    tmp_xdata = dict_new();
    if (!tmp_xdata) {
        ret = -1;
        goto out;
    }

    ret = dict_set_str(tmp_xdata, "entry-point", "true");
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_DICT_SET_FAILED, NULL);
        goto out;
    }

    STACK_WIND(frame, gf_svc_readdirp_lookup_cbk, SECOND_CHILD(this),
               SECOND_CHILD(this)->fops->lookup, loc, tmp_xdata);
out:
    if (tmp_xdata)
        dict_unref(tmp_xdata);

    GF_FREE(path);
    return ret;
}

static gf_boolean_t
gf_svc_readdir_on_special_dir(call_frame_t *frame, void *cookie, xlator_t *this,
                              int32_t op_ret, int32_t op_errno,
                              gf_dirent_t *entries, dict_t *xdata)
{
    svc_local_t *local = NULL;
    svc_private_t *private = NULL;
    inode_t *inode = NULL;
    fd_t *fd = NULL;
    char *path = NULL;
    loc_t *loc = NULL;
    dict_t *tmp_xdata = NULL;
    int ret = -1;
    gf_boolean_t unwind = _gf_true;
    svc_fd_t *svc_fd = NULL;
    char entry_point[NAME_MAX + 1] = {
        0,
    };

    GF_VALIDATE_OR_GOTO("snapview-client", this, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);

   private
    = this->private;
    local = frame->local;

    loc = &local->loc;
    fd = local->fd;
    svc_fd = svc_fd_ctx_get(this, fd);
    if (!svc_fd) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_GET_FD_CONTEXT_FAILED,
                "gfid=%s", uuid_utoa(fd->inode->gfid), NULL);
        goto out;
    }

    /*
     * check if its end of readdir operation from posix, if special_dir
     * option is set, if readdir is done on special directory and if
     * readdirp is from normal regular graph.
     */

    if (!private->show_entry_point)
        goto out;

    if (op_ret == 0 && op_errno == ENOENT && private->special_dir &&
        strcmp(private->special_dir, "") && svc_fd->special_dir &&
        local->subvolume == FIRST_CHILD(this)) {
        if (gf_svc_get_entry_point(this, entry_point, sizeof(entry_point))) {
            gf_smsg(this->name, GF_LOG_WARNING, 0,
                    SVC_MSG_GET_FD_CONTEXT_FAILED, NULL);
            goto out;
        }

        inode = inode_grep(fd->inode->table, fd->inode, entry_point);
        if (!inode) {
            inode = inode_new(fd->inode->table);
            if (!inode) {
                gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_ALLOC_INODE_FAILED,
                        NULL);
                goto out;
            }
        }

        gf_uuid_copy(local->loc.pargfid, fd->inode->gfid);
        gf_uuid_copy(local->loc.gfid, inode->gfid);
        if (gf_uuid_is_null(inode->gfid))
            ret = inode_path(fd->inode, entry_point, &path);
        else
            ret = inode_path(inode, NULL, &path);

        if (ret < 0)
            goto out;
        loc->path = gf_strdup(path);
        if (loc->path) {
            if (!loc->name || (loc->name && !strcmp(loc->name, ""))) {
                loc->name = strrchr(loc->path, '/');
                if (loc->name)
                    loc->name++;
            }
        }

        loc->inode = inode;
        loc->parent = inode_ref(fd->inode);
        tmp_xdata = dict_new();
        if (!tmp_xdata)
            goto out;
        ret = dict_set_str(tmp_xdata, "entry-point", "true");
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_DICT_SET_FAILED, NULL);
            goto out;
        }

        local->cookie = cookie;
        if (local->xdata) {
            dict_unref(local->xdata);
            local->xdata = NULL;
        }
        if (xdata)
            local->xdata = dict_ref(xdata);

        STACK_WIND(frame, gf_svc_readdirp_lookup_cbk, SECOND_CHILD(this),
                   SECOND_CHILD(this)->fops->lookup, loc, tmp_xdata);
        unwind = _gf_false;
    }

out:
    if (tmp_xdata)
        dict_unref(tmp_xdata);

    GF_FREE(path);
    return unwind;
}

static int32_t
gf_svc_readdirp_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                    dict_t *xdata)
{
    gf_dirent_t *entry = NULL;
    gf_dirent_t *tmpentry = NULL;
    svc_local_t *local = NULL;
    int inode_type = -1;
    int ret = -1;
    svc_fd_t *svc_fd = NULL;
    gf_boolean_t unwind = _gf_true;
    char entry_point[NAME_MAX + 1] = {
        0,
    };

    if (op_ret < 0)
        goto out;

    GF_VALIDATE_OR_GOTO("snapview-client", this, out);

    local = frame->local;

    svc_fd = svc_fd_ctx_get(this, local->fd);
    if (!svc_fd) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, SVC_MSG_GET_FD_CONTEXT_FAILED,
                "gfid=%s", uuid_utoa(local->fd->inode->gfid), NULL);
    }

    if (local->subvolume == FIRST_CHILD(this))
        inode_type = NORMAL_INODE;
    else
        inode_type = VIRTUAL_INODE;

    /*
     * Better to goto out and return whatever is there in the
     * readdirp response (even if the readdir response contains
     * a directory entry for the snapshot entry point). Otherwise
     * if we ignore the error, then there is a chance of race
     * condition where, priv->path is changed in reconfigure
     */
    if (gf_svc_get_entry_point(this, entry_point, sizeof(entry_point))) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, SVC_MSG_COPY_ENTRY_POINT_FAILED,
                NULL);
        goto out;
    }

    list_for_each_entry_safe(entry, tmpentry, &entries->list, list)
    {
        /* If .snaps pre-exists, then it should not be listed
         * in the NORMAL INODE directory when USS is enabled,
         * so filter the .snaps entry if exists.
         * However it is OK to list .snaps in VIRTUAL world
         */
        if (inode_type == NORMAL_INODE && !strcmp(entry_point, entry->d_name)) {
            gf_dirent_entry_free(entry);
            continue;
        }

        if (!entry->inode)
            continue;

        ret = svc_inode_ctx_set(this, entry->inode, inode_type);
        if (ret)
            gf_smsg(this->name, GF_LOG_ERROR, 0,
                    SVC_MSG_SET_INODE_CONTEXT_FAILED, NULL);
        if (svc_fd)
            svc_fd->last_offset = entry->d_off;
    }

    unwind = gf_svc_readdir_on_special_dir(frame, cookie, this, op_ret,
                                           op_errno, entries, xdata);

out:
    if (unwind)
        SVC_STACK_UNWIND(readdirp, frame, op_ret, op_errno, entries, xdata);

    return 0;
}

static int32_t
gf_svc_readdirp(call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                off_t off, dict_t *xdata)
{
    int inode_type = -1;
    xlator_t *subvolume = NULL;
    svc_local_t *local = NULL;
    int ret = -1;
    int op_ret = -1;
    int op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;
    svc_fd_t *svc_fd = NULL;
    gf_dirent_t entries;

    INIT_LIST_HEAD(&entries.list);

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);
    GF_VALIDATE_OR_GOTO(this->name, fd->inode, out);

    local = mem_get0(this->local_pool);
    if (!local) {
        op_errno = ENOMEM;
        gf_smsg(this->name, GF_LOG_ERROR, op_errno, SVC_MSG_NO_MEMORY, NULL);
        goto out;
    }

    /*
     * This is mainly for samba shares (or windows clients). As part of
     * readdirp on the directory used as samba share, the entry point
     * directory would have been added at the end. So when a new readdirp
     * request comes, we have to check if the entry point has been handled
     * or not in readdirp. That information and the offset used for it
     * is remembered in fd context. If it has been handled, then simply
     * unwind indication end of readdir operation.
     */
    svc_fd = svc_fd_ctx_get_or_new(this, fd);
    if (!svc_fd)
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_GET_FD_CONTEXT_FAILED,
                "gfid=%s", uuid_utoa(fd->inode->gfid), NULL);
    else {
        if (svc_fd->entry_point_handled && off == svc_fd->last_offset) {
            op_ret = 0;
            op_errno = ENOENT;
            goto out;
        }
    }

    SVC_GET_SUBVOL_FROM_CTX(this, op_ret, op_errno, inode_type, ret, fd->inode,
                            subvolume, out);

    local->subvolume = subvolume;
    local->fd = fd_ref(fd);
    frame->local = local;

    STACK_WIND(frame, gf_svc_readdirp_cbk, subvolume, subvolume->fops->readdirp,
               fd, size, off, xdata);

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(readdirp, frame, op_ret, op_errno, &entries, NULL);

    gf_dirent_free(&entries);

    return 0;
}

/* Renaming the entries from or to snapshots is not allowed as the snapshots
   are read-only.
*/
static int32_t
gf_svc_rename(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
              dict_t *xdata)
{
    int src_inode_type = -1;
    int dst_inode_type = -1;
    int dst_parent_type = -1;
    int32_t op_ret = -1;
    int32_t op_errno = 0;
    int32_t ret = -1;
    gf_boolean_t wind = _gf_false;

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, oldloc, out);
    GF_VALIDATE_OR_GOTO(this->name, oldloc->inode, out);
    GF_VALIDATE_OR_GOTO(this->name, newloc, out);

    ret = svc_inode_ctx_get(this, oldloc->inode, &src_inode_type);
    if (ret < 0) {
        op_ret = -1;
        op_errno = EINVAL;
        gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                SVC_MSG_GET_INODE_CONTEXT_FAILED, "gfid=%s",
                uuid_utoa(oldloc->inode->gfid), NULL);
        goto out;
    }

    if (src_inode_type == VIRTUAL_INODE) {
        op_ret = -1;
        op_errno = EROFS;
        gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                SVC_MSG_RENAME_SNAPSHOT_ENTRY, "name=%s", oldloc->name, NULL);
        goto out;
    }

    if (newloc->inode) {
        ret = svc_inode_ctx_get(this, newloc->inode, &dst_inode_type);
        if (!ret && dst_inode_type == VIRTUAL_INODE) {
            op_ret = -1;
            op_errno = EROFS;
            gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                    SVC_MSG_RENAME_SNAPSHOT_ENTRY, "oldloc-name=%s",
                    oldloc->name, "newloc-name=%s", newloc->name, NULL);
            goto out;
        }
    }

    if (dst_inode_type < 0) {
        ret = svc_inode_ctx_get(this, newloc->parent, &dst_parent_type);
        if (!ret && dst_parent_type == VIRTUAL_INODE) {
            op_ret = -1;
            op_errno = EROFS;
            gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                    SVC_MSG_RENAME_SNAPSHOT_ENTRY, "oldloc-name=%s",
                    oldloc->name, "newloc-name=%s", newloc->name, NULL);
            goto out;
        }
    }

    STACK_WIND_TAIL(frame, FIRST_CHILD(this), FIRST_CHILD(this)->fops->rename,
                    oldloc, newloc, xdata);

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(rename, frame, op_ret, op_errno, NULL, NULL, NULL,
                         NULL, NULL, NULL);
    return 0;
}

/* Creating hardlinks for the files from the snapshot is not allowed as it
   will be equivalent of creating hardlinks across different filesystems.
   And so is vice versa.
*/
static int32_t
gf_svc_link(call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
            dict_t *xdata)
{
    int src_inode_type = -1;
    int dst_parent_type = -1;
    int32_t op_ret = -1;
    int32_t op_errno = 0;
    int32_t ret = -1;
    gf_boolean_t wind = _gf_false;

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, oldloc, out);
    GF_VALIDATE_OR_GOTO(this->name, oldloc->inode, out);
    GF_VALIDATE_OR_GOTO(this->name, newloc, out);

    ret = svc_inode_ctx_get(this, oldloc->inode, &src_inode_type);
    if (!ret && src_inode_type == VIRTUAL_INODE) {
        op_ret = -1;
        op_errno = EROFS;
        gf_smsg(this->name, GF_LOG_ERROR, op_errno, SVC_MSG_LINK_SNAPSHOT_ENTRY,
                "oldloc-name=%s", oldloc->name, NULL);
        goto out;
    }

    ret = svc_inode_ctx_get(this, newloc->parent, &dst_parent_type);
    if (!ret && dst_parent_type == VIRTUAL_INODE) {
        op_ret = -1;
        op_errno = EROFS;
        gf_smsg(this->name, GF_LOG_ERROR, op_errno, SVC_MSG_LINK_SNAPSHOT_ENTRY,
                "oldloc-name=%s", oldloc->name, "newloc-name=%s", newloc->name,
                NULL);
        goto out;
    }

    STACK_WIND_TAIL(frame, FIRST_CHILD(this), FIRST_CHILD(this)->fops->link,
                    oldloc, newloc, xdata);

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(link, frame, op_ret, op_errno, NULL, NULL, NULL, NULL,
                         NULL);
    return 0;
}

static int32_t
gf_svc_removexattr(call_frame_t *frame, xlator_t *this, loc_t *loc,
                   const char *name, dict_t *xdata)
{
    int ret = -1;
    int inode_type = -1;
    int op_ret = -1;
    int op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, loc, out);
    GF_VALIDATE_OR_GOTO(this->name, loc->inode, out);

    ret = svc_inode_ctx_get(this, loc->inode, &inode_type);
    if (ret < 0) {
        op_ret = -1;
        op_errno = EINVAL;
        gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                SVC_MSG_GET_INODE_CONTEXT_FAILED, "path=%s", loc->path,
                "gfid=%s", uuid_utoa(loc->inode->gfid), NULL);
        goto out;
    }

    if (inode_type == NORMAL_INODE) {
        STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                        FIRST_CHILD(this)->fops->removexattr, loc, name, xdata);
    } else {
        op_ret = -1;
        op_errno = EROFS;
        goto out;
    }

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(removexattr, frame, op_ret, op_errno, NULL);

    return 0;
}

static int
gf_svc_fsync(call_frame_t *frame, xlator_t *this, fd_t *fd, int datasync,
             dict_t *xdata)
{
    int inode_type = -1;
    int ret = -1;
    int op_ret = -1;
    int op_errno = EINVAL;
    gf_boolean_t wind = _gf_false;

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);
    GF_VALIDATE_OR_GOTO(this->name, fd->inode, out);

    ret = svc_inode_ctx_get(this, fd->inode, &inode_type);
    if (ret < 0) {
        op_ret = -1;
        op_errno = EINVAL;
        gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                SVC_MSG_GET_INODE_CONTEXT_FAILED, "gfid=%s",
                uuid_utoa(fd->inode->gfid), NULL);
        goto out;
    }

    if (inode_type == NORMAL_INODE) {
        STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                        FIRST_CHILD(this)->fops->fsync, fd, datasync, xdata);
    } else {
        op_ret = -1;
        op_errno = EROFS;
        goto out;
    }

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(fsync, frame, op_ret, op_errno, NULL, NULL, NULL);

    return 0;
}

static int32_t
gf_svc_flush(call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
    int32_t op_ret = -1;
    int32_t op_errno = 0;
    int ret = -1;
    int inode_type = -1;
    xlator_t *subvolume = NULL;
    gf_boolean_t wind = _gf_false;

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, frame, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);
    GF_VALIDATE_OR_GOTO(this->name, fd->inode, out);

    SVC_GET_SUBVOL_FROM_CTX(this, op_ret, op_errno, inode_type, ret, fd->inode,
                            subvolume, out);

    STACK_WIND_TAIL(frame, subvolume, subvolume->fops->flush, fd, xdata);

    wind = _gf_true;

out:
    if (!wind)
        SVC_STACK_UNWIND(flush, frame, op_ret, op_errno, NULL);

    return 0;
}

static int32_t
gf_svc_releasedir(xlator_t *this, fd_t *fd)
{
    svc_fd_t *sfd = NULL;
    uint64_t tmp_pfd = 0;
    int ret = 0;

    GF_VALIDATE_OR_GOTO("snapview-client", this, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);

    ret = fd_ctx_del(fd, this, &tmp_pfd);
    if (ret < 0) {
        gf_msg_debug(this->name, 0, "pfd from fd=%p is NULL", fd);
        goto out;
    }

    GF_FREE(sfd);

out:
    return 0;
}

static int32_t
gf_svc_forget(xlator_t *this, inode_t *inode)
{
    int ret = -1;
    uint64_t value = 0;

    GF_VALIDATE_OR_GOTO("svc", this, out);
    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    ret = inode_ctx_del(inode, this, &value);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0,
                SVC_MSG_DELETE_INODE_CONTEXT_FAILED, "gfid=%s",
                uuid_utoa(inode->gfid), NULL);
        goto out;
    }

out:
    return 0;
}

static int
gf_svc_priv_destroy(xlator_t *this, svc_private_t *priv)
{
    int ret = -1;

    if (!priv) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, SVC_MSG_NULL_PRIV, NULL);
        goto out;
    }

    GF_FREE(priv->path);
    GF_FREE(priv->special_dir);

    LOCK_DESTROY(&priv->lock);

    GF_FREE(priv);

    if (this->local_pool) {
        mem_pool_destroy(this->local_pool);
        this->local_pool = NULL;
    }

    ret = 0;

out:
    return ret;
}

/**
 * ** NOTE **:
 * =============
 * The option "snapdir-entry-path" is NOT reconfigurable.
 * That option as of now is only for the consumption of
 * samba, where, it needs to tell glusterfs about the
 * directory that is shared with windows client for the
 * access. Now, in windows-explorer (GUI) interface, for
 * the directory shared, the entry point to the snapshot
 * world (snapshot-directory option) should be visible,
 * atleast as a hidden entry. For that to happen, glusterfs
 * has to send that entry in the readdir response coming on
 * the directory used as the smb share. Therefore, samba,
 * while initializing the gluster volume (via gfapi) sets
 * the xlator option "snapdir-entry-path" to the directory
 * which is to be shared with windows (check the file
 * vfs_glusterfs.c from samba source code). So to avoid
 * problems with smb access, not allowing snapdir-entry-path
 * option to be configurable. That option is for those
 * consumers who know what they are doing.
 **/
int
reconfigure(xlator_t *this, dict_t *options)
{
    svc_private_t *priv = NULL;
    char *path = NULL;
    gf_boolean_t show_entry_point = _gf_false;
    char *tmp = NULL;

    priv = this->private;

    GF_OPTION_RECONF("snapshot-directory", path, options, str, out);
    if (!path || (strlen(path) > NAME_MAX) || path[0] != '.') {
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_INVALID_ENTRY_POINT,
                "path=%s", path, NULL);
        goto out;
    }

    GF_OPTION_RECONF("show-snapshot-directory", show_entry_point, options, bool,
                     out);

    /*
     * The assumption now is that priv->path is an allocated memory (either
     * in init or in a previous reconfigure).
     * So, the intention here is to preserve the older contents of the option
     * until the new option's value has been completely stored in the priv.
     * So, do this.
     *  - Store the pointer of priv->path in a temporary pointer.
     *  - Allocate new memory for the new value of the option that is just
     *    obtained from the above call to GF_OPTION_RECONF.
     *  - If the above allocation fails, again set the pointer from priv
     *    to the address stored in tmp. i.e. the previous value.
     *  - If the allocation succeeds, then free the tmp pointer.
     * WARNING: Before changing the allocation and freeing logic of
     *          priv->path, always check the init function to see how
     *          priv->path is set. Take decisions accordingly. As of now,
     *          the assumption is that, the string elements of private
     *          structure of snapview-client are allocated (either in
     *          init or here in reconfugure).
     */
    LOCK(&priv->lock);
    {
        tmp = priv->path;
        priv->path = NULL;
        priv->path = gf_strdup(path);
        if (!priv->path) {
            gf_log(this->name, GF_LOG_ERROR,
                   "failed to reconfigure snapshot-directory option to %s",
                   path);
            priv->path = tmp;
        } else {
            GF_FREE(tmp);
            tmp = NULL;
        }

        priv->show_entry_point = show_entry_point;
    }
    UNLOCK(&priv->lock);

out:
    return 0;
}

int32_t
mem_acct_init(xlator_t *this)
{
    int32_t ret = -1;

    if (!this)
        return ret;

    ret = xlator_mem_acct_init(this, gf_svc_mt_end + 1);

    if (ret != 0) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, SVC_MSG_MEM_ACNT_FAILED, NULL);
    }

    return ret;
}

int32_t
init(xlator_t *this)
{
    svc_private_t *private = NULL;
    int ret = -1;
    int children = 0;
    xlator_list_t *xl = NULL;
    char *path = NULL;
    char *special_dir = NULL;

    if (!this->children) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_NO_CHILD_FOR_XLATOR, NULL);
        goto out;
    }

    xl = this->children;
    while (xl) {
        children++;
        xl = xl->next;
    }

    if (children != 2) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_XLATOR_CHILDREN_WRONG,
                "subvol-num=%d", children, NULL);
        goto out;
    }

    /* This can be the top of graph in certain cases */
    if (!this->parents) {
        gf_msg_debug(this->name, 0,
                     "dangling volume. Check "
                     "volfile");
    }

   private
    = GF_CALLOC(1, sizeof(*private), gf_svc_mt_svc_private_t);
    if (!private)
        goto out;

    LOCK_INIT(&private->lock);

    GF_OPTION_INIT("snapshot-directory", path, str, out);
    if (!path || (strlen(path) > NAME_MAX) || path[0] != '.') {
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_INVALID_ENTRY_POINT,
                "path=%s", path, NULL);
        goto out;
    }

   private
    ->path = gf_strdup(path);
    if (!private->path) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_NO_MEMORY,
                "entry-point-path=%s", path, NULL);
        goto out;
    }

    GF_OPTION_INIT("snapdir-entry-path", special_dir, str, out);
    if (!special_dir || strstr(special_dir, path)) {
        if (special_dir)
            gf_smsg(this->name, GF_LOG_ERROR, 0,
                    SVC_MSG_ENTRY_POINT_SPECIAL_DIR, "path=%s", path,
                    "special-dir=%s", special_dir);
        else
            gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_NULL_SPECIAL_DIR,
                    NULL);
        goto out;
    }

   private
    ->special_dir = gf_strdup(special_dir);
    if (!private->special_dir) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_NO_MEMORY,
                "special-directory=%s", special_dir, NULL);
        goto out;
    }

    GF_OPTION_INIT("show-snapshot-directory", private->show_entry_point, bool,
                   out);

    this->local_pool = mem_pool_new(svc_local_t, 128);
    if (!this->local_pool) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, SVC_MSG_MEM_POOL_GET_FAILED, NULL);
        goto out;
    }

    this->private = private;

    ret = 0;

out:
    if (ret)
        (void)gf_svc_priv_destroy(this, private);

    return ret;
}

void
fini(xlator_t *this)
{
    svc_private_t *priv = NULL;

    if (!this)
        return;

    priv = this->private;
    if (!priv)
        return;

    /*
     * Just log the failure and go ahead to
     * set this->priv to NULL.
     */
    if (gf_svc_priv_destroy(this, priv))
        gf_smsg(this->name, GF_LOG_WARNING, 0, SVC_MSG_PRIV_DESTROY_FAILED,
                NULL);

    this->private = NULL;

    return;
}

int
notify(xlator_t *this, int event, void *data, ...)
{
    xlator_t *subvol = NULL;
    int ret = 0;

    subvol = data;

    /* As there are two subvolumes in snapview-client, there is
     * a possibility that the regular subvolume is still down and
     * snapd subvolume come up first. So if we don't handle this situation
     * CHILD_UP event will be propagated upwards to fuse when
     * regular subvolume is still down.
     * This can cause data unavailable for the application.
     * So for now send notifications up only for regular subvolume.
     *
     * TODO: In future if required we may need to handle
     * notifications from virtual subvolume
     */
    if (subvol != SECOND_CHILD(this))
        ret = default_notify(this, event, data);

    return ret;
}

struct xlator_fops fops = {
    .lookup = gf_svc_lookup,
    .opendir = gf_svc_opendir,
    .stat = gf_svc_stat,
    .fstat = gf_svc_fstat,
    .statfs = gf_svc_statfs,
    .rmdir = gf_svc_rmdir,
    .rename = gf_svc_rename,
    .mkdir = gf_svc_mkdir,
    .open = gf_svc_open,
    .unlink = gf_svc_unlink,
    .setattr = gf_svc_setattr,
    .getxattr = gf_svc_getxattr,
    .setxattr = gf_svc_setxattr,
    .fsetxattr = gf_svc_fsetxattr,
    .readv = gf_svc_readv,
    .readdir = gf_svc_readdir,
    .readdirp = gf_svc_readdirp,
    .create = gf_svc_create,
    .readlink = gf_svc_readlink,
    .mknod = gf_svc_mknod,
    .symlink = gf_svc_symlink,
    .flush = gf_svc_flush,
    .link = gf_svc_link,
    .access = gf_svc_access,
    .removexattr = gf_svc_removexattr,
    .fsync = gf_svc_fsync,
};

struct xlator_cbks cbks = {
    .forget = gf_svc_forget,
    .releasedir = gf_svc_releasedir,
};

struct volume_options options[] = {
    {
        .key = {"snapshot-directory"},
        .type = GF_OPTION_TYPE_STR,
        .default_value = ".snaps",
    },
    {
        .key = {"snapdir-entry-path"},
        .type = GF_OPTION_TYPE_STR,
        .description = "An option to set the path of a directory on which "
                       "when readdir comes, dentry for the snapshot-directory"
                       " should be created and added in the readdir response",
        .default_value = "",
    },
    {
        .key = {"show-snapshot-directory"},
        .type = GF_OPTION_TYPE_BOOL,
        .description = "If this option is set, and the option "
                       "\"snapdir-entry-path\" is set (which is set by samba "
                       "vfs plugin for glusterfs, then send the entry point "
                       "when readdir comes on the snapdir-entry-path",
        .default_value = "off",
    },
    {.key = {NULL}},
};

xlator_api_t xlator_api = {
    .init = init,
    .fini = fini,
    .notify = notify,
    .reconfigure = reconfigure,
    .mem_acct_init = mem_acct_init,
    .op_version = {1},
    .fops = &fops,
    .cbks = &cbks,
    .options = options,
    .identifier = "snapview-client",
    .category = GF_MAINTAINED,
};
