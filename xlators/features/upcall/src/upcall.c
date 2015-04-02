/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "compat.h"
#include "xlator.h"
#include "inode.h"
#include "logging.h"
#include "common-utils.h"

#include "statedump.h"
#include "syncop.h"

#include "upcall.h"
#include "upcall-mem-types.h"
#include "glusterfs3-xdr.h"
#include "protocol-common.h"
#include "defaults.h"

int
up_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
{
        client_t         *client        = NULL;
        uint32_t         flags          = 0;
        upcall_local_t   *local         = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        client = frame->root->client;
        local = frame->local;

        if ((op_ret < 0) || !local) {
                goto out;
        }
        flags = UP_IDEMPOTENT_FLAGS;
        CACHE_INVALIDATE (frame, this, client, local->inode, flags);

out:
        UPCALL_STACK_UNWIND (open, frame, op_ret, op_errno, fd, xdata);

        return 0;
}


int
up_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, fd->inode);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_open_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->open,
                    loc, flags, fd, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (open, frame, -1, op_errno, NULL, NULL);

        return 0;
}

int
up_writev_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, struct iatt *prebuf,
                struct iatt *postbuf, dict_t *xdata)
{
        client_t         *client        = NULL;
        uint32_t         flags          = 0;
        upcall_local_t   *local         = NULL;

        client = frame->root->client;
        local = frame->local;

        if ((op_ret < 0) || !local) {
                goto out;
        }
        flags = UP_WRITE_FLAGS;
        CACHE_INVALIDATE (frame, this, client, local->inode, flags);

out:
        UPCALL_STACK_UNWIND (writev, frame, op_ret, op_errno,
                             prebuf, postbuf, xdata);

        return 0;
}


int
up_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
            struct iovec *vector, int count, off_t off, uint32_t flags,
            struct iobref *iobref, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, fd->inode);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_writev_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->writev,
                    fd, vector, count, off, flags, iobref, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (writev, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}


int
up_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int op_ret, int op_errno,
               struct iovec *vector, int count, struct iatt *stbuf,
               struct iobref *iobref, dict_t *xdata)
{
        client_t         *client        = NULL;
        uint32_t         flags          = 0;
        upcall_local_t   *local         = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        client = frame->root->client;
        local = frame->local;

        if ((op_ret < 0) || !local) {
                goto out;
        }
        flags = UP_IDEMPOTENT_FLAGS;
        CACHE_INVALIDATE (frame, this, client, local->inode, flags);

out:
        UPCALL_STACK_UNWIND (readv, frame, op_ret, op_errno, vector,
                             count, stbuf, iobref, xdata);

        return 0;
}

int
up_readv (call_frame_t *frame, xlator_t *this,
          fd_t *fd, size_t size, off_t offset,
          uint32_t flags, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, fd->inode);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_readv_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->readv,
                    fd, size, offset, flags, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (readv, frame, -1, op_errno, NULL, 0,
                             NULL, NULL, NULL);

        return 0;
}

int32_t
up_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
                dict_t *xdata)
{
        client_t         *client        = NULL;
        uint32_t         flags          = 0;
        upcall_local_t   *local         = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        client = frame->root->client;
        local = frame->local;

        if ((op_ret < 0) || !local) {
                goto out;
        }
        flags = UP_IDEMPOTENT_FLAGS;
        CACHE_INVALIDATE (frame, this, client, local->inode, flags);

out:
        UPCALL_STACK_UNWIND (lk, frame, op_ret, op_errno, lock, xdata);

        return 0;
}

int
up_lk (call_frame_t *frame, xlator_t *this,
       fd_t *fd, int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, fd->inode);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_lk_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lk,
                    fd, cmd, flock, xdata);
        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (lk, frame, -1, op_errno, NULL, NULL);

        return 0;
}

int
up_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno, struct iatt *prebuf,
                  struct iatt *postbuf, dict_t *xdata)
{
        client_t         *client        = NULL;
        uint32_t         flags          = 0;
        upcall_local_t   *local         = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        client = frame->root->client;
        local = frame->local;

        if ((op_ret < 0) || !local) {
                goto out;
        }
        flags = UP_WRITE_FLAGS;
        CACHE_INVALIDATE (frame, this, client, local->inode, flags);

out:
        UPCALL_STACK_UNWIND (truncate, frame, op_ret, op_errno,
                             prebuf, postbuf, xdata);

        return 0;
}

int
up_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
              dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, loc->inode);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_truncate_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->truncate,
                    loc, offset, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (truncate, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

int
up_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int op_ret, int op_errno, struct iatt *statpre,
                 struct iatt *statpost, dict_t *xdata)
{
        client_t         *client        = NULL;
        uint32_t         flags          = 0;
        upcall_local_t   *local         = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        client = frame->root->client;
        local = frame->local;

        if ((op_ret < 0) || !local) {
                goto out;
        }
        /* XXX: setattr -> UP_SIZE or UP_OWN or UP_MODE or UP_TIMES
         * or INODE_UPDATE (or UP_PERM esp incase of ACLs -> INODE_INVALIDATE)
         * Need to check what attr is changed and accordingly pass UP_FLAGS.
         * Bug1200271.
         */
        flags = UP_ATTR_FLAGS;
        CACHE_INVALIDATE (frame, this, client, local->inode, flags);

out:
        UPCALL_STACK_UNWIND (setattr, frame, op_ret, op_errno,
                             statpre, statpost, xdata);

        return 0;
}

int
up_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
             struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, loc->inode);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_setattr_cbk,
                           FIRST_CHILD(this),
                           FIRST_CHILD(this)->fops->setattr,
                            loc, stbuf, valid, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (setattr, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

int
up_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *stbuf,
                   struct iatt *preoldparent, struct iatt *postoldparent,
                   struct iatt *prenewparent, struct iatt *postnewparent,
                   dict_t *xdata)
{
        client_t         *client        = NULL;
        uint32_t         flags          = 0;
        upcall_local_t   *local         = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        client = frame->root->client;
        local = frame->local;

        if ((op_ret < 0) || !local) {
                goto out;
        }
        flags = UP_RENAME_FLAGS;
        CACHE_INVALIDATE (frame, this, client, local->inode, flags);

        /* Need to invalidate old and new parent entries as well */
        flags = UP_PARENT_DENTRY_FLAGS;
        CACHE_INVALIDATE_DIR (frame, this, client, local->inode, flags);

        /* XXX: notify oldparent as well */
/*        if (gf_uuid_compare (preoldparent->ia_gfid, prenewparent->ia_gfid))
                CACHE_INVALIDATE (frame, this, client, prenewparent->ia_gfid, flags);*/

out:
        UPCALL_STACK_UNWIND (rename, frame, op_ret, op_errno,
                             stbuf, preoldparent, postoldparent,
                             prenewparent, postnewparent, xdata);

        return 0;
}

int
up_rename (call_frame_t *frame, xlator_t *this,
          loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, oldloc->inode);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_rename_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->rename,
                    oldloc, newloc, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (rename, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL, NULL, NULL);

        return 0;
}

int
up_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        client_t         *client        = NULL;
        uint32_t         flags          = 0;
        upcall_local_t   *local         = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        client = frame->root->client;
        local = frame->local;

        if ((op_ret < 0) || !local) {
                goto out;
        }
        flags = UP_NLINK_FLAGS;
        CACHE_INVALIDATE (frame, this, client, local->inode, flags);

        flags = UP_PARENT_DENTRY_FLAGS;
        /* invalidate parent's entry too */
        CACHE_INVALIDATE_DIR (frame, this, client, local->inode, flags);

out:
        UPCALL_STACK_UNWIND (unlink, frame, op_ret, op_errno,
                             preparent, postparent, xdata);

        return 0;
}

int
up_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
              dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, loc->inode);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_unlink_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->unlink,
                    loc, xflag, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (unlink, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

int
up_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, inode_t *inode, struct iatt *stbuf,
                struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        client_t         *client        = NULL;
        uint32_t         flags          = 0;
        upcall_local_t   *local         = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        client = frame->root->client;
        local = frame->local;

        if ((op_ret < 0) || !local) {
                goto out;
        }
        flags = UP_NLINK_FLAGS;
        CACHE_INVALIDATE (frame, this, client, local->inode, flags);

        /* do we need to update parent as well?? */
out:
        UPCALL_STACK_UNWIND (link, frame, op_ret, op_errno,
                             inode, stbuf, preparent, postparent, xdata);

        return 0;
}

int
up_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
         loc_t *newloc, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, oldloc->inode);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_link_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->link,
                    oldloc, newloc, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (link, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL, NULL);

        return 0;
}

int
up_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        client_t         *client        = NULL;
        uint32_t         flags          = 0;
        upcall_local_t   *local         = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        client = frame->root->client;
        local = frame->local;

        if ((op_ret < 0) || !local) {
                goto out;
        }
        flags = UP_NLINK_FLAGS;
        CACHE_INVALIDATE (frame, this, client, local->inode, flags);

        /* invalidate parent's entry too */
        flags = UP_PARENT_DENTRY_FLAGS;
        CACHE_INVALIDATE_DIR (frame, this, client, local->inode, flags);

out:
        UPCALL_STACK_UNWIND (rmdir, frame, op_ret, op_errno,
                             preparent, postparent, xdata);

        return 0;
}

int
up_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
              dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, loc->inode);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_rmdir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->rmdir,
                    loc, flags, xdata);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (rmdir, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

int
up_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, inode_t *inode,
                struct iatt *stbuf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        client_t         *client        = NULL;
        uint32_t         flags          = 0;
        upcall_local_t   *local         = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        client = frame->root->client;
        local = frame->local;

        if ((op_ret < 0) || !local) {
                goto out;
        }
        flags = UP_NLINK_FLAGS;
        CACHE_INVALIDATE (frame, this, client, local->inode, flags);

        /* invalidate parent's entry too */
        flags = UP_PARENT_DENTRY_FLAGS;
        CACHE_INVALIDATE_DIR (frame, this, client, local->inode, flags);

out:
        UPCALL_STACK_UNWIND (mkdir, frame, op_ret, op_errno,
                             inode, stbuf, preparent, postparent, xdata);

        return 0;
}

int
up_mkdir (call_frame_t *frame, xlator_t *this,
          loc_t *loc, mode_t mode, mode_t umask, dict_t *params)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, loc->inode);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_mkdir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->mkdir,
                    loc, mode, umask, params);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (mkdir, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL, NULL);

        return 0;
}

int
up_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, fd_t *fd, inode_t *inode,
                struct iatt *stbuf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        client_t         *client        = NULL;
        uint32_t         flags          = 0;
        upcall_local_t   *local         = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        client = frame->root->client;
        local = frame->local;

        if ((op_ret < 0) || !local) {
                goto out;
        }

        /* As its a new file create, no need of sending notification */
        /* However invalidate parent's entry */
        flags = UP_PARENT_DENTRY_FLAGS;
        CACHE_INVALIDATE_DIR (frame, this, client, local->inode, flags);

out:
        UPCALL_STACK_UNWIND (create, frame, op_ret, op_errno, fd,
                             inode, stbuf, preparent, postparent, xdata);

        return 0;
}

int
up_create (call_frame_t *frame, xlator_t *this,
          loc_t *loc, int32_t flags, mode_t mode,
          mode_t umask, fd_t *fd, dict_t *params)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, loc->inode);

        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_create_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->create,
                    loc, flags, mode, umask, fd, params);

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        UPCALL_STACK_UNWIND (create, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL, NULL, NULL);

        return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_upcall_mt_end + 1);

        if (ret != 0) {
                gf_msg ("upcall", GF_LOG_WARNING, 0,
                        UPCALL_MSG_NO_MEMORY,
                        "Memory allocation failed");
                return ret;
        }

        return ret;
}

void
upcall_local_wipe (xlator_t *this, upcall_local_t *local)
{
        if (local) {
                inode_unref (local->inode);
                mem_put (local);
        }
}

upcall_local_t *
upcall_local_init (call_frame_t *frame, xlator_t *this, inode_t *inode)
{
        upcall_local_t *local = NULL;

        local = mem_get0 (THIS->local_pool);

        if (!local)
                goto out;

        local->inode = inode_ref (inode);

        /* Shall we get inode_ctx and store it here itself? */
        local->upcall_inode_ctx = upcall_inode_ctx_get (inode, this);

        frame->local = local;

out:
        return local;
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        upcall_private_t *priv                   = NULL;
        int              ret                    = -1;

        priv = this->private;
        GF_ASSERT (priv);

        GF_OPTION_RECONF ("cache-invalidation", priv->cache_invalidation_enabled,
                          options, bool, out);
        GF_OPTION_RECONF ("cache-invalidation-timeout", priv->cache_invalidation_timeout,
                          options, int32, out);

        ret = 0;
out:
        return ret;
}

int
init (xlator_t *this)
{
        int                       ret        = -1;
        upcall_private_t         *priv       = NULL;

        priv = GF_CALLOC (1, sizeof (*priv),
                          gf_upcall_mt_private_t);
        if (!priv) {
                gf_msg ("upcall", GF_LOG_WARNING, 0,
                        UPCALL_MSG_NO_MEMORY,
                        "Memory allocation failed");
                goto out;
        }

        GF_OPTION_INIT ("cache-invalidation", priv->cache_invalidation_enabled,
                        bool, out);
        GF_OPTION_INIT ("cache-invalidation-timeout",
                        priv->cache_invalidation_timeout, int32, out);

        this->private = priv;
        this->local_pool = mem_pool_new (upcall_local_t, 512);
        ret = 0;

out:
        if (ret) {
                GF_FREE (priv);
        }

        return ret;
}

int
fini (xlator_t *this)
{
        upcall_private_t *priv = NULL;

        priv = this->private;
        if (!priv) {
                return 0;
        }
        this->private = NULL;

        GF_FREE (priv);

        return 0;
}

int
upcall_forget (xlator_t *this, inode_t *inode)
{
        upcall_cleanup_inode_ctx (this, inode);
        return 0;
}

int
upcall_release (xlator_t *this, fd_t *fd)
{
        return 0;
}

int
notify (xlator_t *this, int32_t event, void *data, ...)
{
        int                 ret              = -1;
        int32_t             val              = 0;
        notify_event_data_t *notify_event    = NULL;
        struct gf_upcall    up_req           = {0,};
        upcall_client_t     *up_client_entry = NULL;

        switch (event) {
        case GF_EVENT_UPCALL:
        {
                gf_log (this->name, GF_LOG_DEBUG, "Upcall Notify event = %d",
                        event);

                notify_event = (notify_event_data_t *) data;
                up_client_entry = notify_event->client_entry;

                if (!up_client_entry) {
                        goto out;
                }

                up_req.client_uid = up_client_entry->client_uid;

                memcpy (up_req.gfid, notify_event->gfid, 16);
                gf_log (this->name, GF_LOG_DEBUG,
                        "Sending notify to the client- %s, gfid - %s",
                        up_client_entry->client_uid, up_req.gfid);

                switch (notify_event->event_type) {
                case CACHE_INVALIDATION:
                        GF_ASSERT (notify_event->extra);
                        up_req.flags = notify_event->invalidate_flags;
                        up_req.expire_time_attr = up_client_entry->expire_time_attr;
                        break;
                default:
                        goto out;
                }

                up_req.event_type = notify_event->event_type;

                ret = default_notify (this, event, &up_req);

                /*
                 * notify may fail as the client could have been
                 * dis(re)connected. Cleanup the client entry.
                 */
                if (ret < 0)
                        __upcall_cleanup_client_entry (up_client_entry);
        }
        break;
        default:
                default_notify (this, event, data);
        break;
        }
        ret = 0;

out:
        return ret;
}

struct xlator_fops fops = {
        .open        = up_open,
        .readv       = up_readv,
        .writev      = up_writev,
        .truncate    = up_truncate,
        .lk          = up_lk,
        .setattr     = up_setattr,
        .rename      = up_rename,
        .unlink      = up_unlink, /* invalidate both file and parent dir */
        .rmdir       = up_rmdir, /* same as above */
        .link        = up_link, /* invalidate both file and parent dir */
        .create      = up_create, /* update only direntry */
        .mkdir       = up_mkdir, /* update only dirent */
#ifdef WIP
        .ftruncate   = up_ftruncate, /* reqd? */
        .getattr     = up_getattr, /* ?? */
        .getxattr    = up_getxattr, /* ?? */
        .access      = up_access,
        .lookup      = up_lookup,
        .symlink     = up_symlink, /* invalidate both file and parent dir maybe */
        .readlink    = up_readlink, /* Needed? readlink same as read? */
        .readdirp    = up_readdirp,
        .readdir     = up_readdir,
/*  other fops to be considered - Bug1200271
 *   lookup, stat, opendir, readdir, readdirp, readlink, mknod, statfs, flush,
 *   fsync, mknod, fsyncdir, setxattr, removexattr, rchecksum, fallocate, discard,
 *   zerofill, (also variants of above similar to fsetattr)
 */
#endif
};

struct xlator_cbks cbks = {
        .forget = upcall_forget,
        .release = upcall_release,
};

struct volume_options options[] = {
        { .key  = {"cache-invalidation"},
          .type = GF_OPTION_TYPE_BOOL,
          .default_value = "off",
          .description = "When \"on\", sends cache-invalidation"
                         " notifications."
        },
        { .key  = {"cache-invalidation-timeout"},
          .type = GF_OPTION_TYPE_INT,
          .default_value = CACHE_INVALIDATION_TIMEOUT,
          .description = "After 'timeout' seconds since the time"
                         " client accessed any file, cache-invalidation"
                         " notifications are no longer sent to that client."
        },
        { .key = {NULL} },
};
