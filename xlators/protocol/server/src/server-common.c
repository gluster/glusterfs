#include "server.h"
#include "defaults.h"
#include "rpc-common-xdr.h"
#include "glusterfs3-xdr.h"
#include "glusterfs3.h"
#include "compat-errno.h"
#include "server-messages.h"
#include "defaults.h"
#include "fd.h"
#include "xdr-nfs3.h"

void
server_post_stat (gfs3_stat_rsp *rsp, struct iatt *stbuf)
{
        gf_stat_from_iatt (&rsp->stat, stbuf);
}

void
server_post_readlink (gfs3_readlink_rsp *rsp, struct iatt *stbuf,
                      const char *buf)
{
        gf_stat_from_iatt (&rsp->buf, stbuf);
        rsp->path = (char *)buf;
}

void
server_post_mknod (server_state_t *state, gfs3_mknod_rsp *rsp,
                   struct iatt *stbuf, struct iatt *preparent,
                   struct iatt *postparent, inode_t *inode)
{
        inode_t             *link_inode = NULL;

        gf_stat_from_iatt (&rsp->stat, stbuf);
        gf_stat_from_iatt (&rsp->preparent, preparent);
        gf_stat_from_iatt (&rsp->postparent, postparent);

        link_inode = inode_link (inode, state->loc.parent,
                                 state->loc.name, stbuf);
        inode_lookup (link_inode);
        inode_unref  (link_inode);
}

void
server_post_mkdir (server_state_t *state, gfs3_mkdir_rsp *rsp,
                   inode_t *inode,
                   struct iatt *stbuf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        inode_t             *link_inode = NULL;

        gf_stat_from_iatt (&rsp->stat, stbuf);
        gf_stat_from_iatt (&rsp->preparent, preparent);
        gf_stat_from_iatt (&rsp->postparent, postparent);

        link_inode = inode_link (inode, state->loc.parent,
                                 state->loc.name, stbuf);
        inode_lookup (link_inode);
        inode_unref (link_inode);

}

void
server_post_unlink (server_state_t *state, gfs3_unlink_rsp *rsp,
                    struct iatt *preparent, struct iatt *postparent)
{
        inode_unlink (state->loc.inode, state->loc.parent,
                      state->loc.name);

        forget_inode_if_no_dentry (state->loc.inode);

        gf_stat_from_iatt (&rsp->preparent, preparent);
        gf_stat_from_iatt (&rsp->postparent, postparent);

}

void
server_post_rmdir (server_state_t *state, gfs3_rmdir_rsp *rsp,
                    struct iatt *preparent, struct iatt *postparent)
{
        inode_unlink (state->loc.inode, state->loc.parent,
                      state->loc.name);
        /* parent should not be found for directories after
         * inode_unlink, since directories cannot have
         * hardlinks.
         */
        forget_inode_if_no_dentry (state->loc.inode);

        gf_stat_from_iatt (&rsp->preparent, preparent);
        gf_stat_from_iatt (&rsp->postparent, postparent);
}

void
server_post_symlink (server_state_t *state, gfs3_symlink_rsp *rsp,
                   inode_t *inode,
                   struct iatt *stbuf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        inode_t             *link_inode = NULL;

        gf_stat_from_iatt (&rsp->stat, stbuf);
        gf_stat_from_iatt (&rsp->preparent, preparent);
        gf_stat_from_iatt (&rsp->postparent, postparent);

        link_inode = inode_link (inode, state->loc.parent,
                                 state->loc.name, stbuf);
        inode_lookup (link_inode);
        inode_unref (link_inode);

}

void
server_post_link (server_state_t *state, gfs3_link_rsp *rsp,
                   inode_t *inode,
                   struct iatt *stbuf, struct iatt *preparent,
                   struct iatt *postparent, dict_t *xdata)
{
        inode_t             *link_inode = NULL;

        gf_stat_from_iatt (&rsp->stat, stbuf);
        gf_stat_from_iatt (&rsp->preparent, preparent);
        gf_stat_from_iatt (&rsp->postparent, postparent);

        link_inode = inode_link (inode, state->loc2.parent,
                                 state->loc2.name, stbuf);
        inode_lookup (link_inode);
        inode_unref (link_inode);

}

void
server_post_truncate (gfs3_truncate_rsp *rsp, struct iatt *prebuf,
                      struct iatt *postbuf)
{
        gf_stat_from_iatt (&rsp->prestat, prebuf);
        gf_stat_from_iatt (&rsp->poststat, postbuf);
}

void
server_post_writev (gfs3_write_rsp *rsp, struct iatt *prebuf,
                      struct iatt *postbuf)
{
        gf_stat_from_iatt (&rsp->prestat, prebuf);
        gf_stat_from_iatt (&rsp->poststat, postbuf);
}

void
server_post_statfs (gfs3_statfs_rsp *rsp, struct statvfs *stbuf)
{
        gf_statfs_from_statfs (&rsp->statfs, stbuf);
}

void
server_post_fsync (gfs3_fsync_rsp *rsp, struct iatt *prebuf,
                   struct iatt *postbuf)
{
        gf_stat_from_iatt (&rsp->prestat, prebuf);
        gf_stat_from_iatt (&rsp->poststat, postbuf);
}

void
server_post_ftruncate (gfs3_ftruncate_rsp *rsp, struct iatt *prebuf,
                      struct iatt *postbuf)
{
        gf_stat_from_iatt (&rsp->prestat, prebuf);
        gf_stat_from_iatt (&rsp->poststat, postbuf);
}

void
server_post_fstat (gfs3_fstat_rsp *rsp, struct iatt *stbuf)
{
        gf_stat_from_iatt (&rsp->stat, stbuf);
}

void
server_post_lk (xlator_t *this, gfs3_lk_rsp *rsp, struct gf_flock *lock)
{
        switch (lock->l_type) {
        case F_RDLCK:
                lock->l_type = GF_LK_F_RDLCK;
                break;
        case F_WRLCK:
                lock->l_type = GF_LK_F_WRLCK;
                break;
        case F_UNLCK:
                lock->l_type = GF_LK_F_UNLCK;
                break;
        default:
                gf_msg (this->name, GF_LOG_ERROR, 0, PS_MSG_LOCK_ERROR,
                        "Unknown lock type: %"PRId32"!", lock->l_type);
                break;
        }

        gf_proto_flock_from_flock (&rsp->flock, lock);
}

int
server_post_readdir (gfs3_readdir_rsp *rsp, gf_dirent_t *entries)
{
                int     ret = 0;

                ret = serialize_rsp_dirent (entries, rsp);

        return ret;
}
void
server_post_zerofill (gfs3_zerofill_rsp *rsp, struct iatt *statpre,
                      struct iatt *statpost)
{
        gf_stat_from_iatt (&rsp->statpre, statpre);
        gf_stat_from_iatt (&rsp->statpost, statpost);
}

void
server_post_discard (gfs3_discard_rsp *rsp, struct iatt *statpre,
                      struct iatt *statpost)
{
        gf_stat_from_iatt (&rsp->statpre, statpre);
        gf_stat_from_iatt (&rsp->statpost, statpost);
}

void
server_post_fallocate (gfs3_fallocate_rsp *rsp, struct iatt *statpre,
                       struct iatt *statpost)
{
        gf_stat_from_iatt (&rsp->statpre, statpre);
        gf_stat_from_iatt (&rsp->statpost, statpost);
}

void
server_post_seek (gfs3_seek_rsp *rsp, off_t offset)
{
        rsp->offset = offset;
}

int
server_post_readdirp (gfs3_readdirp_rsp *rsp, gf_dirent_t *entries)
{
                int     ret = 0;

                ret = serialize_rsp_direntp (entries, rsp);

        return ret;
}

void
server_post_fsetattr (gfs3_fsetattr_rsp *rsp, struct iatt *statpre,
                       struct iatt *statpost)
{
        gf_stat_from_iatt (&rsp->statpre, statpre);
        gf_stat_from_iatt (&rsp->statpost, statpost);
}

void
server_post_setattr (gfs3_setattr_rsp *rsp, struct iatt *statpre,
                     struct iatt *statpost)
{
        gf_stat_from_iatt (&rsp->statpre, statpre);
        gf_stat_from_iatt (&rsp->statpost, statpost);
}

void
server_post_rchecksum (gfs3_rchecksum_rsp *rsp, uint32_t weak_checksum,
                       uint8_t *strong_checksum)
{
        rsp->weak_checksum = weak_checksum;

        rsp->strong_checksum.strong_checksum_val = (char *)strong_checksum;
        rsp->strong_checksum.strong_checksum_len = MD5_DIGEST_LENGTH;

}

void
server_post_rename (call_frame_t *frame,
                    server_state_t *state, gfs3_rename_rsp *rsp,
                    struct iatt *stbuf,
                    struct iatt *preoldparent,
                    struct iatt *postoldparent,
                    struct iatt *prenewparent,
                    struct iatt *postnewparent)
{
        inode_t             *tmp_inode  = NULL;

        stbuf->ia_type = state->loc.inode->ia_type;

        /* TODO: log gfid of the inodes */
        gf_msg_trace (frame->root->client->bound_xl->name, 0, "%"PRId64": "
                      "RENAME_CBK %s ==> %s", frame->root->unique,
                      state->loc.name, state->loc2.name);

        /* Before renaming the inode, we have to get the inode for the
         * destination entry (i.e. inode with state->loc2.parent as
         * parent and state->loc2.name as name). If it exists, then
         * unlink that inode, and send forget on that inode if the
         * unlinked entry is the last entry. In case of fuse client
         * the fuse kernel module itself sends the forget on the
         * unlinked inode.
         */
        tmp_inode = inode_grep (state->loc.inode->table,
                                state->loc2.parent, state->loc2.name);
        if (tmp_inode) {
                inode_unlink (tmp_inode, state->loc2.parent,
                              state->loc2.name);
                forget_inode_if_no_dentry (tmp_inode);
                inode_unref (tmp_inode);
        }

        inode_rename (state->itable,
                      state->loc.parent, state->loc.name,
                      state->loc2.parent, state->loc2.name,
                      state->loc.inode, stbuf);
        gf_stat_from_iatt (&rsp->stat, stbuf);

        gf_stat_from_iatt (&rsp->preoldparent, preoldparent);
        gf_stat_from_iatt (&rsp->postoldparent, postoldparent);

        gf_stat_from_iatt (&rsp->prenewparent, prenewparent);
        gf_stat_from_iatt (&rsp->postnewparent, postnewparent);

}

int
server_post_open (call_frame_t *frame, xlator_t *this,
                  gfs3_open_rsp *rsp, fd_t *fd)
{
        server_ctx_t        *serv_ctx = NULL;
        uint64_t             fd_no    = 0;

        serv_ctx = server_ctx_get (frame->root->client, this);
        if (serv_ctx == NULL) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        PS_MSG_SERVER_CTX_GET_FAILED, "server_ctx_get() "
                        "failed");
                return -1;
        }

        fd_bind (fd);
        fd_no = gf_fd_unused_get (serv_ctx->fdtable, fd);
        fd_ref (fd);
        rsp->fd = fd_no;

        return 0;
}

void
server_post_readv (gfs3_read_rsp *rsp, struct iatt *stbuf, int op_ret)
{
        gf_stat_from_iatt (&rsp->stat, stbuf);
        rsp->size = op_ret;
}

int
server_post_opendir (call_frame_t *frame, xlator_t *this,
                     gfs3_opendir_rsp *rsp, fd_t *fd)
{
        server_ctx_t        *serv_ctx = NULL;
        uint64_t             fd_no    = 0;

        serv_ctx = server_ctx_get (frame->root->client, this);
        if (serv_ctx == NULL) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        PS_MSG_SERVER_CTX_GET_FAILED, "server_ctx_get() "
                        "failed");
                return -1;
        }

        fd_bind (fd);
        fd_no = gf_fd_unused_get (serv_ctx->fdtable, fd);
        fd_ref (fd);
        rsp->fd = fd_no;

        return 0;
}

int
server_post_create (call_frame_t *frame, gfs3_create_rsp *rsp,
                    server_state_t *state,
                    xlator_t *this, fd_t *fd, inode_t *inode,
                    struct iatt *stbuf, struct iatt *preparent,
                    struct iatt *postparent)
{
        server_ctx_t        *serv_ctx   = NULL;
        inode_t             *link_inode = NULL;
        uint64_t             fd_no      = 0;
        int                  op_errno   = 0;

        link_inode = inode_link (inode, state->loc.parent,
                                 state->loc.name, stbuf);

        if (!link_inode) {
                op_errno = ENOENT;
                goto out;
        }

        if (link_inode != inode) {
                /*
                  VERY racy code (if used anywhere else)
                  -- don't do this without understanding
                */

                inode_ctx_merge (fd, fd->inode, link_inode);
                inode_unref (fd->inode);
                fd->inode = inode_ref (link_inode);
        }

        inode_lookup (link_inode);
        inode_unref (link_inode);

        serv_ctx = server_ctx_get (frame->root->client, this);
        if (serv_ctx == NULL) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        PS_MSG_SERVER_CTX_GET_FAILED, "server_ctx_get() "
                        "failed");
                goto out;
        }

        fd_bind (fd);
        fd_no = gf_fd_unused_get (serv_ctx->fdtable, fd);
        fd_ref (fd);

        if ((fd_no > UINT64_MAX) || (fd == 0)) {
                op_errno = errno;
        }

        rsp->fd = fd_no;
        gf_stat_from_iatt (&rsp->stat, stbuf);
        gf_stat_from_iatt (&rsp->preparent, preparent);
        gf_stat_from_iatt (&rsp->postparent, postparent);

        return 0;
out:
        return -op_errno;
}

/*TODO: Handle revalidate path */
void
server_post_lookup (gfs3_lookup_rsp *rsp, call_frame_t *frame,
                    server_state_t *state,
                    inode_t *inode, struct iatt *stbuf,
                    struct iatt *postparent)
{
        inode_t             *root_inode = NULL;
        inode_t             *link_inode = NULL;
        uuid_t               rootgfid   = {0,};

        root_inode = frame->root->client->bound_xl->itable->root;

        if (inode == root_inode) {
                /* we just looked up root ("/") */
                stbuf->ia_ino = 1;
                rootgfid[15]  = 1;
                gf_uuid_copy (stbuf->ia_gfid, rootgfid);
                if (inode->ia_type == 0)
                        inode->ia_type = stbuf->ia_type;
        }

        gf_stat_from_iatt (&rsp->stat, stbuf);

        if (!__is_root_gfid (inode->gfid)) {
                link_inode = inode_link (inode, state->loc.parent,
                                         state->loc.name, stbuf);
                if (link_inode) {
                        inode_lookup (link_inode);
                        inode_unref (link_inode);
                }
        }
}

void
server_post_lease (gfs3_lease_rsp *rsp, struct gf_lease *lease)
{
        gf_proto_lease_from_lease (&rsp->lease, lease);
}
