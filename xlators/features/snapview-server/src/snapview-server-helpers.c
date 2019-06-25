/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "snapview-server.h"
#include "snapview-server-mem-types.h"

#include <glusterfs/xlator.h>
#include "rpc-clnt.h"
#include "xdr-generic.h"
#include "protocol-common.h"
#include <pthread.h>

int
__svs_inode_ctx_set(xlator_t *this, inode_t *inode, svs_inode_t *svs_inode)
{
    uint64_t value = 0;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("snapview-server", this, out);
    GF_VALIDATE_OR_GOTO(this->name, inode, out);
    GF_VALIDATE_OR_GOTO(this->name, svs_inode, out);

    value = (uint64_t)(long)svs_inode;

    ret = __inode_ctx_set(inode, this, &value);

out:
    return ret;
}

svs_inode_t *
__svs_inode_ctx_get(xlator_t *this, inode_t *inode)
{
    svs_inode_t *svs_inode = NULL;
    uint64_t value = 0;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("snapview-server", this, out);
    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    ret = __inode_ctx_get(inode, this, &value);
    if (ret)
        goto out;

    svs_inode = (svs_inode_t *)((long)value);

out:
    return svs_inode;
}

svs_inode_t *
svs_inode_ctx_get(xlator_t *this, inode_t *inode)
{
    svs_inode_t *svs_inode = NULL;

    GF_VALIDATE_OR_GOTO("snapview-server", this, out);
    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    LOCK(&inode->lock);
    {
        svs_inode = __svs_inode_ctx_get(this, inode);
    }
    UNLOCK(&inode->lock);

out:
    return svs_inode;
}

int32_t
svs_inode_ctx_set(xlator_t *this, inode_t *inode, svs_inode_t *svs_inode)
{
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO("snapview-server", this, out);
    GF_VALIDATE_OR_GOTO(this->name, inode, out);
    GF_VALIDATE_OR_GOTO(this->name, svs_inode, out);

    LOCK(&inode->lock);
    {
        ret = __svs_inode_ctx_set(this, inode, svs_inode);
    }
    UNLOCK(&inode->lock);

out:
    return ret;
}

svs_inode_t *
svs_inode_new(void)
{
    svs_inode_t *svs_inode = NULL;

    svs_inode = GF_CALLOC(1, sizeof(*svs_inode), gf_svs_mt_svs_inode_t);

    return svs_inode;
}

svs_inode_t *
svs_inode_ctx_get_or_new(xlator_t *this, inode_t *inode)
{
    svs_inode_t *svs_inode = NULL;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("snapview-server", this, out);
    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    LOCK(&inode->lock);
    {
        svs_inode = __svs_inode_ctx_get(this, inode);
        if (!svs_inode) {
            svs_inode = svs_inode_new();
            if (svs_inode) {
                ret = __svs_inode_ctx_set(this, inode, svs_inode);
                if (ret) {
                    GF_FREE(svs_inode);
                    svs_inode = NULL;
                }
            }
        }
    }
    UNLOCK(&inode->lock);

out:
    return svs_inode;
}

svs_fd_t *
svs_fd_new(void)
{
    svs_fd_t *svs_fd = NULL;

    svs_fd = GF_CALLOC(1, sizeof(*svs_fd), gf_svs_mt_svs_fd_t);

    return svs_fd;
}

int
__svs_fd_ctx_set(xlator_t *this, fd_t *fd, svs_fd_t *svs_fd)
{
    uint64_t value = 0;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("snapview-server", this, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);
    GF_VALIDATE_OR_GOTO(this->name, svs_fd, out);

    value = (uint64_t)(long)svs_fd;

    ret = __fd_ctx_set(fd, this, value);

out:
    return ret;
}

svs_fd_t *
__svs_fd_ctx_get(xlator_t *this, fd_t *fd)
{
    svs_fd_t *svs_fd = NULL;
    uint64_t value = 0;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("snapview-server", this, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);

    ret = __fd_ctx_get(fd, this, &value);
    if (ret)
        return NULL;

    svs_fd = (svs_fd_t *)((long)value);

out:
    return svs_fd;
}

svs_fd_t *
svs_fd_ctx_get(xlator_t *this, fd_t *fd)
{
    svs_fd_t *svs_fd = NULL;

    GF_VALIDATE_OR_GOTO("snapview-server", this, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);

    LOCK(&fd->lock);
    {
        svs_fd = __svs_fd_ctx_get(this, fd);
    }
    UNLOCK(&fd->lock);

out:
    return svs_fd;
}

int32_t
svs_fd_ctx_set(xlator_t *this, fd_t *fd, svs_fd_t *svs_fd)
{
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO("snapview-server", this, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);
    GF_VALIDATE_OR_GOTO(this->name, svs_fd, out);

    LOCK(&fd->lock);
    {
        ret = __svs_fd_ctx_set(this, fd, svs_fd);
    }
    UNLOCK(&fd->lock);

out:
    return ret;
}

svs_fd_t *
__svs_fd_ctx_get_or_new(xlator_t *this, fd_t *fd)
{
    svs_fd_t *svs_fd = NULL;
    int ret = -1;
    glfs_t *fs = NULL;
    glfs_object_t *object = NULL;
    svs_inode_t *inode_ctx = NULL;
    glfs_fd_t *glfd = NULL;
    inode_t *inode = NULL;

    GF_VALIDATE_OR_GOTO("snapview-server", this, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);

    inode = fd->inode;
    svs_fd = __svs_fd_ctx_get(this, fd);
    if (svs_fd) {
        ret = 0;
        goto out;
    }

    svs_fd = svs_fd_new();
    if (!svs_fd) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SVS_MSG_NEW_FD_CTX_FAILED,
               "failed to allocate new fd "
               "context for gfid %s",
               uuid_utoa(inode->gfid));
        goto out;
    }

    if (fd_is_anonymous(fd)) {
        inode_ctx = svs_inode_ctx_get(this, inode);
        if (!inode_ctx) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   SVS_MSG_GET_INODE_CONTEXT_FAILED,
                   "failed to get inode "
                   "context for %s",
                   uuid_utoa(inode->gfid));
            goto out;
        }

        fs = inode_ctx->fs;
        object = inode_ctx->object;

        if (inode->ia_type == IA_IFDIR) {
            glfd = glfs_h_opendir(fs, object);
            if (!glfd) {
                gf_msg(this->name, GF_LOG_ERROR, errno, SVS_MSG_OPENDIR_FAILED,
                       "failed to "
                       "open the directory %s",
                       uuid_utoa(inode->gfid));
                goto out;
            }
        }

        if (inode->ia_type == IA_IFREG) {
            glfd = glfs_h_open(fs, object, O_RDONLY | O_LARGEFILE);
            if (!glfd) {
                gf_msg(this->name, GF_LOG_ERROR, errno, SVS_MSG_OPEN_FAILED,
                       "failed to "
                       "open the file %s",
                       uuid_utoa(inode->gfid));
                goto out;
            }
        }

        svs_fd->fd = glfd;
    }

    ret = __svs_fd_ctx_set(this, fd, svs_fd);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SVS_MSG_SET_FD_CONTEXT_FAILED,
               "failed to set fd context "
               "for gfid %s",
               uuid_utoa(inode->gfid));
        if (svs_fd->fd) {
            if (inode->ia_type == IA_IFDIR) {
                ret = glfs_closedir(svs_fd->fd);
                if (ret)
                    gf_msg(this->name, GF_LOG_ERROR, errno,
                           SVS_MSG_CLOSEDIR_FAILED,
                           "failed to close the fd for %s",
                           uuid_utoa(inode->gfid));
            }
            if (inode->ia_type == IA_IFREG) {
                ret = glfs_close(svs_fd->fd);
                if (ret)
                    gf_msg(this->name, GF_LOG_ERROR, 0, SVS_MSG_CLOSE_FAILED,
                           "failed to close the fd for %s",
                           uuid_utoa(inode->gfid));
            }
        }
        ret = -1;
    }

out:
    if (ret) {
        GF_FREE(svs_fd);
        svs_fd = NULL;
    }

    return svs_fd;
}

svs_fd_t *
svs_fd_ctx_get_or_new(xlator_t *this, fd_t *fd)
{
    svs_fd_t *svs_fd = NULL;

    GF_VALIDATE_OR_GOTO("snapview-server", this, out);
    GF_VALIDATE_OR_GOTO(this->name, fd, out);

    LOCK(&fd->lock);
    {
        svs_fd = __svs_fd_ctx_get_or_new(this, fd);
    }
    UNLOCK(&fd->lock);

out:
    return svs_fd;
}

int
svs_uuid_generate(xlator_t *this, uuid_t gfid, char *snapname,
                  uuid_t origin_gfid)
{
    char ino_string[NAME_MAX + 32] = "";
    uuid_t tmp = {
        0,
    };
    int ret = -1;

    GF_VALIDATE_OR_GOTO("snapview-server", this, out);
    GF_VALIDATE_OR_GOTO(this->name, snapname, out);

    (void)snprintf(ino_string, sizeof(ino_string), "%s%s", snapname,
                   uuid_utoa(origin_gfid));

    if (gf_gfid_generate_from_xxh64(tmp, ino_string)) {
        gf_msg(this->name, GF_LOG_WARNING, 0, SVS_MSG_GFID_GEN_FAILED,
               "failed to generate "
               "gfid for object with actual gfid of %s "
               "(snapname: %s, key: %s)",
               uuid_utoa(origin_gfid), snapname, ino_string);
        goto out;
    }

    gf_uuid_copy(gfid, tmp);

    ret = 0;

    gf_msg_debug(this->name, 0, "gfid generated is %s ", uuid_utoa(gfid));

out:
    return ret;
}

void
svs_fill_ino_from_gfid(struct iatt *buf)
{
    xlator_t *this = NULL;

    this = THIS;

    GF_VALIDATE_OR_GOTO("snapview-server", this, out);
    GF_VALIDATE_OR_GOTO(this->name, buf, out);

    /* consider least significant 8 bytes of value out of gfid */
    if (gf_uuid_is_null(buf->ia_gfid)) {
        buf->ia_ino = -1;
        goto out;
    }

    buf->ia_ino = gfid_to_ino(buf->ia_gfid);
out:
    return;
}

void
svs_iatt_fill(uuid_t gfid, struct iatt *buf)
{
    struct timeval tv = {
        0,
    };
    xlator_t *this = NULL;

    this = THIS;

    GF_VALIDATE_OR_GOTO("snapview-server", this, out);
    GF_VALIDATE_OR_GOTO(this->name, buf, out);

    buf->ia_type = IA_IFDIR;
    buf->ia_uid = 0;
    buf->ia_gid = 0;
    buf->ia_size = 0;
    buf->ia_nlink = 2;
    buf->ia_blocks = 8;
    buf->ia_size = 4096;

    gf_uuid_copy(buf->ia_gfid, gfid);
    svs_fill_ino_from_gfid(buf);

    buf->ia_prot = ia_prot_from_st_mode(0755);

    gettimeofday(&tv, 0);

    buf->ia_mtime = buf->ia_atime = buf->ia_ctime = tv.tv_sec;
    buf->ia_mtime_nsec = buf->ia_atime_nsec = buf->ia_ctime_nsec = (tv.tv_usec *
                                                                    1000);

out:
    return;
}

/* priv->snaplist_lock should be held before calling this function */
snap_dirent_t *
__svs_get_snap_dirent(xlator_t *this, const char *name)
{
    svs_private_t *private = NULL;
    int i = 0;
    snap_dirent_t *dirents = NULL;
    snap_dirent_t *tmp_dirent = NULL;
    snap_dirent_t *dirent = NULL;

   private
    = this->private;

    dirents = private->dirents;
    if (!dirents) {
        goto out;
    }

    tmp_dirent = dirents;
    for (i = 0; i < private->num_snaps; i++) {
        if (!strcmp(tmp_dirent->name, name)) {
            dirent = tmp_dirent;
            break;
        }
        tmp_dirent++;
    }

out:
    return dirent;
}

glfs_t *
__svs_initialise_snapshot_volume(xlator_t *this, const char *name,
                                 int32_t *op_errno)
{
    svs_private_t *priv = NULL;
    int32_t ret = -1;
    int32_t local_errno = ESTALE;
    snap_dirent_t *dirent = NULL;
    char volname[PATH_MAX] = {
        0,
    };
    glfs_t *fs = NULL;
    int loglevel = GF_LOG_INFO;
    char logfile[PATH_MAX] = {
        0,
    };
    char *volfile_server = NULL;

    GF_VALIDATE_OR_GOTO("snapview-server", this, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);
    GF_VALIDATE_OR_GOTO(this->name, name, out);

    priv = this->private;

    dirent = __svs_get_snap_dirent(this, name);
    if (!dirent) {
        gf_msg_debug(this->name, 0,
                     "snap entry for "
                     "name %s not found",
                     name);
        local_errno = ENOENT;
        goto out;
    }

    if (dirent->fs) {
        ret = 0;
        fs = dirent->fs;
        goto out;
    }

    snprintf(volname, sizeof(volname), "/snaps/%s/%s/%s", dirent->name,
             dirent->snap_volname, dirent->snap_volname);

    fs = glfs_new(volname);
    if (!fs) {
        local_errno = ENOMEM;
        gf_msg(this->name, GF_LOG_ERROR, local_errno, SVS_MSG_GLFS_NEW_FAILED,
               "glfs instance for snap volume %s "
               "failed",
               dirent->name);
        goto out;
    }

    /*
     * Before, localhost was used as the volfile server. But, with that
     * method, accessing snapshots started giving ENOENT error if a
     * specific bind address is mentioned in the glusterd volume file.
     * Check the bug https://bugzilla.redhat.com/show_bug.cgi?id=1725211.
     * So, the new method is tried below, where, snapview-server first
     * uses the volfile server used by the snapd (obtained from the
     * command line arguments saved in the global context of the process).
     * If the volfile server in global context is NULL, then localhost
     * is tried (like before).
     */
    if (this->ctx->cmd_args.volfile_server) {
        volfile_server = gf_strdup(this->ctx->cmd_args.volfile_server);
        if (!volfile_server) {
            gf_msg(this->name, GF_LOG_WARNING, ENOMEM,
                   SVS_MSG_VOLFILE_SERVER_GET_FAIL,
                   "failed to copy volfile server %s. ",
                   this->ctx->cmd_args.volfile_server);
            ret = -1;
            goto out;
        }
    } else {
        gf_msg(this->name, GF_LOG_WARNING, ENOMEM,
               SVS_MSG_VOLFILE_SERVER_GET_FAIL,
               "volfile server is NULL in cmd args. "
               "Trying with localhost");
        volfile_server = gf_strdup("localhost");
        if (!volfile_server) {
            gf_msg(this->name, GF_LOG_WARNING, ENOMEM,
                   SVS_MSG_VOLFILE_SERVER_GET_FAIL,
                   "failed to copy volfile server localhost.");
            ret = -1;
            goto out;
        }
    }

    ret = glfs_set_volfile_server(fs, "tcp", volfile_server, 24007);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, local_errno,
               SVS_MSG_SET_VOLFILE_SERVR_FAILED,
               "setting the "
               "volfile server %s for snap volume %s "
               "failed",
               volfile_server, dirent->name);
        goto out;
    }

    snprintf(logfile, sizeof(logfile),
             DEFAULT_SVD_LOG_FILE_DIRECTORY "/snaps/%s/%s-%s.log",
             priv->volname, name, dirent->uuid);

    ret = glfs_set_logging(fs, logfile, loglevel);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, local_errno,
               SVS_MSG_SET_LOGGING_FAILED,
               "failed to set the "
               "log file path");
        goto out;
    }

    ret = glfs_init(fs);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, local_errno, SVS_MSG_GLFS_INIT_FAILED,
               "initing the "
               "fs for %s failed",
               dirent->name);
        goto out;
    }

    ret = 0;

out:
    if (ret) {
        if (op_errno)
            *op_errno = local_errno;

        if (fs)
            glfs_fini(fs);
        fs = NULL;
    }

    if (fs) {
        dirent->fs = fs;
    }

    GF_FREE(volfile_server);
    return fs;
}

glfs_t *
svs_initialise_snapshot_volume(xlator_t *this, const char *name,
                               int32_t *op_errno)
{
    glfs_t *fs = NULL;
    svs_private_t *priv = NULL;

    GF_VALIDATE_OR_GOTO("snapview-server", this, out);
    GF_VALIDATE_OR_GOTO(this->name, this->private, out);
    GF_VALIDATE_OR_GOTO(this->name, name, out);

    priv = this->private;

    LOCK(&priv->snaplist_lock);
    {
        fs = __svs_initialise_snapshot_volume(this, name, op_errno);
    }
    UNLOCK(&priv->snaplist_lock);

out:

    return fs;
}

snap_dirent_t *
svs_get_latest_snap_entry(xlator_t *this)
{
    svs_private_t *priv = NULL;
    snap_dirent_t *dirents = NULL;
    snap_dirent_t *dirent = NULL;

    GF_VALIDATE_OR_GOTO("svs", this, out);

    priv = this->private;

    LOCK(&priv->snaplist_lock);
    {
        dirents = priv->dirents;
        if (!dirents) {
            goto unlock;
        }
        if (priv->num_snaps)
            dirent = &dirents[priv->num_snaps - 1];
    }
unlock:
    UNLOCK(&priv->snaplist_lock);

out:
    return dirent;
}

glfs_t *
svs_get_latest_snapshot(xlator_t *this)
{
    glfs_t *fs = NULL;
    snap_dirent_t *dirent = NULL;
    svs_private_t *priv = NULL;

    GF_VALIDATE_OR_GOTO("svs", this, out);
    priv = this->private;

    dirent = svs_get_latest_snap_entry(this);

    if (dirent) {
        LOCK(&priv->snaplist_lock);
        {
            fs = dirent->fs;
        }
        UNLOCK(&priv->snaplist_lock);
    }

out:
    return fs;
}

glfs_t *
svs_inode_ctx_glfs_mapping(xlator_t *this, svs_inode_t *inode_ctx)
{
    glfs_t *fs = NULL;

    GF_VALIDATE_OR_GOTO("svs", this, out);
    GF_VALIDATE_OR_GOTO(this->name, inode_ctx, out);

    fs = inode_ctx->fs;

    SVS_CHECK_VALID_SNAPSHOT_HANDLE(fs, this);

out:
    return fs;
}

glfs_t *
svs_inode_glfs_mapping(xlator_t *this, inode_t *inode)
{
    svs_inode_t *inode_ctx = NULL;
    glfs_t *fs = NULL;

    inode_ctx = svs_inode_ctx_get(this, inode);
    if (!inode_ctx) {
        gf_msg(this->name, GF_LOG_ERROR, 0, SVS_MSG_GET_INODE_CONTEXT_FAILED,
               "inode context not found for"
               " the inode %s",
               uuid_utoa(inode->gfid));
        goto out;
    }

    fs = svs_inode_ctx_glfs_mapping(this, inode_ctx);

out:
    return fs;
}
