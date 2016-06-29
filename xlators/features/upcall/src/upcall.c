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

int32_t
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
        flags = UP_UPDATE_CLIENT;
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 NULL, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (open, frame, op_ret, op_errno, fd, xdata);

        return 0;
}


int32_t
up_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, fd->inode, NULL);
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
        UPCALL_STACK_UNWIND (open, frame, -1, op_errno, NULL, NULL);

        return 0;
}

int32_t
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
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 postbuf, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (writev, frame, op_ret, op_errno,
                             prebuf, postbuf, xdata);

        return 0;
}


int32_t
up_writev (call_frame_t *frame, xlator_t *this, fd_t *fd,
           struct iovec *vector, int count, off_t off, uint32_t flags,
           struct iobref *iobref, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, fd->inode, NULL);
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
        UPCALL_STACK_UNWIND (writev, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}


int32_t
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
        flags = UP_UPDATE_CLIENT;
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 stbuf, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (readv, frame, op_ret, op_errno, vector,
                             count, stbuf, iobref, xdata);

        return 0;
}

int32_t
up_readv (call_frame_t *frame, xlator_t *this,
          fd_t *fd, size_t size, off_t offset,
          uint32_t flags, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, fd->inode, NULL);
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
        flags = UP_UPDATE_CLIENT;
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 NULL, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (lk, frame, op_ret, op_errno, lock, xdata);

        return 0;
}

int32_t
up_lk (call_frame_t *frame, xlator_t *this,
       fd_t *fd, int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, fd->inode, NULL);
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
        UPCALL_STACK_UNWIND (lk, frame, -1, op_errno, NULL, NULL);

        return 0;
}

int32_t
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
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 postbuf, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (truncate, frame, op_ret, op_errno,
                             prebuf, postbuf, xdata);

        return 0;
}

int32_t
up_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
             dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, loc->inode, NULL);
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
        UPCALL_STACK_UNWIND (truncate, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

int32_t
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
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 statpost, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (setattr, frame, op_ret, op_errno,
                             statpre, statpost, xdata);

        return 0;
}

int32_t
up_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
            struct iatt *stbuf, int32_t valid, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, loc->inode, NULL);
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
        UPCALL_STACK_UNWIND (setattr, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

int32_t
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
        flags = (UP_RENAME_FLAGS | UP_PARENT_DENTRY_FLAGS);
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 stbuf, postnewparent, postoldparent, NULL);

out:
        UPCALL_STACK_UNWIND (rename, frame, op_ret, op_errno,
                             stbuf, preoldparent, postoldparent,
                             prenewparent, postnewparent, xdata);

        return 0;
}

int32_t
up_rename (call_frame_t *frame, xlator_t *this,
          loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, oldloc->inode, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        /* copy oldloc */
        loc_copy (&local->rename_oldloc, oldloc);
out:
        STACK_WIND (frame, up_rename_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->rename,
                    oldloc, newloc, xdata);

        return 0;

err:
        UPCALL_STACK_UNWIND (rename, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL, NULL, NULL);

        return 0;
}

int32_t
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
        flags = (UP_NLINK_FLAGS | UP_PARENT_DENTRY_FLAGS);
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 NULL, postparent, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (unlink, frame, op_ret, op_errno,
                             preparent, postparent, xdata);

        return 0;
}

int32_t
up_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
           dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, loc->inode, NULL);
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
        UPCALL_STACK_UNWIND (unlink, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

int32_t
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
        flags = (UP_NLINK_FLAGS | UP_PARENT_DENTRY_FLAGS);
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 stbuf, postparent, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (link, frame, op_ret, op_errno,
                             inode, stbuf, preparent, postparent, xdata);

        return 0;
}

int32_t
up_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
         loc_t *newloc, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, oldloc->inode, NULL);
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
        UPCALL_STACK_UNWIND (link, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL, NULL);

        return 0;
}

int32_t
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

        flags = (UP_NLINK_FLAGS | UP_PARENT_DENTRY_FLAGS);
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 NULL, postparent, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (rmdir, frame, op_ret, op_errno,
                             preparent, postparent, xdata);

        return 0;
}

int32_t
up_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
          dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, loc->inode, NULL);
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
        UPCALL_STACK_UNWIND (rmdir, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

int32_t
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

        /* invalidate parent's entry too */
        flags = UP_TIMES;
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 postparent, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (mkdir, frame, op_ret, op_errno,
                             inode, stbuf, preparent, postparent, xdata);

        return 0;
}

int32_t
up_mkdir (call_frame_t *frame, xlator_t *this,
          loc_t *loc, mode_t mode, mode_t umask, dict_t *params)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, loc->parent, NULL);
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
        UPCALL_STACK_UNWIND (mkdir, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL, NULL);

        return 0;
}

int32_t
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
        flags = UP_TIMES;
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 postparent, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (create, frame, op_ret, op_errno, fd,
                             inode, stbuf, preparent, postparent, xdata);

        return 0;
}

int32_t
up_create (call_frame_t *frame, xlator_t *this,
           loc_t *loc, int32_t flags, mode_t mode,
           mode_t umask, fd_t *fd, dict_t *params)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, loc->parent, NULL);

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
        UPCALL_STACK_UNWIND (create, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL, NULL, NULL);

        return 0;
}

int32_t
up_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int op_ret, int op_errno,
               inode_t *inode, struct iatt *stbuf, dict_t *xattr,
               struct iatt *postparent)
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
        flags = UP_UPDATE_CLIENT;
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 stbuf, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (lookup, frame, op_ret, op_errno, inode, stbuf,
                             xattr, postparent);

        return 0;
}

int32_t
up_lookup (call_frame_t *frame, xlator_t *this,
           loc_t *loc, dict_t *xattr_req)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, loc->inode, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_lookup_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->lookup,
                    loc, xattr_req);

        return 0;

err:
        UPCALL_STACK_UNWIND (lookup, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL);

        return 0;
}

int32_t
up_stat_cbk (call_frame_t *frame, void *cookie,
             xlator_t *this, int32_t op_ret, int32_t op_errno,
             struct iatt *buf, dict_t *xdata)
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
        flags = UP_UPDATE_CLIENT;
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 buf, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (stat, frame, op_ret, op_errno, buf,
                             xdata);

        return 0;
}

int32_t
up_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, loc->inode, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_stat_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->stat,
                    loc, xdata);

        return 0;

err:
        UPCALL_STACK_UNWIND (stat, frame, -1, op_errno, NULL, NULL);

        return 0;
}

int32_t
up_fstat (call_frame_t *frame, xlator_t *this,
          fd_t *fd, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, fd->inode, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_stat_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fstat,
                    fd, xdata);

        return 0;

err:
        UPCALL_STACK_UNWIND (fstat, frame, -1, op_errno, NULL, NULL);

        return 0;
}

int32_t
up_ftruncate (call_frame_t *frame, xlator_t *this,
              fd_t *fd, off_t offset, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, fd->inode, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_truncate_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->ftruncate,
                    fd, offset, xdata);

        return 0;

err:
        UPCALL_STACK_UNWIND (ftruncate, frame, -1, op_errno, NULL,
                             NULL, NULL);

        return 0;
}

int32_t
up_access_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int op_ret, int op_errno, dict_t *xdata)
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
        flags = UP_UPDATE_CLIENT;
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 NULL, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (access, frame, op_ret, op_errno, xdata);

        return 0;
}

int32_t
up_access (call_frame_t *frame, xlator_t *this,
           loc_t *loc, int32_t mask, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, loc->inode, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_access_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->access,
                    loc, mask, xdata);

        return 0;

err:
        UPCALL_STACK_UNWIND (access, frame, -1, op_errno, NULL);

        return 0;
}

int32_t
up_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int op_ret, int op_errno, const char *path,
                 struct iatt *stbuf, dict_t *xdata)
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
        flags = UP_UPDATE_CLIENT;
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 stbuf, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (readlink, frame, op_ret, op_errno, path, stbuf,
                             xdata);

        return 0;
}

int32_t
up_readlink (call_frame_t *frame, xlator_t *this,
             loc_t *loc, size_t size, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, loc->inode, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_readlink_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->readlink,
                    loc, size, xdata);

        return 0;

err:
        UPCALL_STACK_UNWIND (readlink, frame, -1, op_errno, NULL,
                             NULL, NULL);

        return 0;
}

int32_t
up_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int32_t op_ret, int32_t op_errno, inode_t *inode,
              struct iatt *buf, struct iatt *preparent,
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

        /* invalidate parent's entry too */
        flags = UP_TIMES;
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 postparent, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (mknod, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);

        return 0;
}

int32_t
up_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc,
          mode_t mode, dev_t rdev, mode_t umask, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, loc->parent, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_mknod_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->mknod,
                    loc, mode, rdev, umask, xdata);

        return 0;

err:
        UPCALL_STACK_UNWIND (mknod, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL, NULL);

        return 0;
}

int32_t
up_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, inode_t *inode,
                struct iatt *buf, struct iatt *preparent,
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

        /* invalidate parent's entry too */
        flags = UP_TIMES;
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 postparent, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (symlink, frame, op_ret, op_errno, inode, buf,
                             preparent, postparent, xdata);

        return 0;
}

int32_t
up_symlink (call_frame_t   *frame, xlator_t *this,
            const char *linkpath, loc_t *loc, mode_t umask,
            dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, loc->parent, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_symlink_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->symlink,
                    linkpath, loc, umask, xdata);

        return 0;

err:
        UPCALL_STACK_UNWIND (symlink, frame, -1, op_errno, NULL,
                             NULL, NULL, NULL, NULL);

        return 0;
}

int32_t
up_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, fd_t *fd,
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
        flags = UP_UPDATE_CLIENT;
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 NULL, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (opendir, frame, op_ret, op_errno, fd, xdata);

        return 0;
}

int32_t
up_opendir (call_frame_t *frame, xlator_t *this,
            loc_t *loc, fd_t *fd, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, loc->inode, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_opendir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->opendir,
                    loc, fd, xdata);

        return 0;

err:
        UPCALL_STACK_UNWIND (opendir, frame, -1, op_errno, NULL, NULL);

        return 0;
}

int32_t
up_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct statvfs *buf,
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
        flags = UP_UPDATE_CLIENT;
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 NULL, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (statfs, frame, op_ret, op_errno, buf, xdata);

        return 0;
}

int32_t
up_statfs (call_frame_t *frame, xlator_t *this,
           loc_t *loc, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, loc->inode, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_statfs_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->statfs,
                    loc, xdata);

        return 0;

err:
        UPCALL_STACK_UNWIND (statfs, frame, -1, op_errno, NULL, NULL);

        return 0;
}

int32_t
up_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
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
        flags = UP_UPDATE_CLIENT;
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 NULL, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (readdir, frame, op_ret, op_errno, entries, xdata);

        return 0;
}

int32_t
up_readdir (call_frame_t  *frame, xlator_t *this,
            fd_t *fd, size_t size, off_t off, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, fd->inode, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_readdir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->readdir,
                    fd, size, off, xdata);

        return 0;

err:
        UPCALL_STACK_UNWIND (readdir, frame, -1, op_errno, NULL, NULL);

        return 0;
}

int32_t
up_readdirp (call_frame_t *frame, xlator_t *this,
             fd_t *fd, size_t size, off_t off, dict_t *dict)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, fd->inode, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_readdir_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->readdirp,
                    fd, size, off, dict);

        return 0;

err:
        UPCALL_STACK_UNWIND (readdirp, frame, -1, op_errno, NULL, NULL);

        return 0;
}

int32_t
up_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
             struct iatt  *stbuf, int32_t valid, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, fd->inode, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_setattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fsetattr,
                    fd, stbuf, valid, xdata);

        return 0;

err:
        UPCALL_STACK_UNWIND (fsetattr, frame, -1, op_errno, NULL,
                             NULL, NULL);

        return 0;
}

int32_t
up_fallocate_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *pre,
                 struct iatt *post, dict_t *xdata)
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
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 post, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (fallocate, frame, op_ret, op_errno, pre,
                             post, xdata);

        return 0;
}

int32_t
up_fallocate(call_frame_t *frame, xlator_t *this, fd_t *fd,
             int32_t mode, off_t offset, size_t len, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, fd->inode, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_fallocate_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fallocate,
                    fd, mode, offset, len, xdata);

        return 0;

err:
        UPCALL_STACK_UNWIND (fallocate, frame, -1, op_errno, NULL,
                             NULL, NULL);

        return 0;
}

int32_t
up_discard_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *pre,
               struct iatt *post, dict_t *xdata)
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
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 post, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (discard, frame, op_ret, op_errno, pre,
                             post, xdata);

        return 0;
}

int32_t
up_discard(call_frame_t *frame, xlator_t *this, fd_t *fd,
           off_t offset, size_t len, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, fd->inode, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_discard_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->discard,
                    fd, offset, len, xdata);

        return 0;

err:
        UPCALL_STACK_UNWIND (discard, frame, -1, op_errno, NULL,
                             NULL, NULL);

        return 0;
}

int32_t
up_zerofill_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *pre,
                struct iatt *post, dict_t *xdata)
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
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 post, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (zerofill, frame, op_ret, op_errno, pre,
                             post, xdata);

        return 0;
}

int
up_zerofill(call_frame_t *frame, xlator_t *this, fd_t *fd,
            off_t offset, off_t len, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, fd->inode, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_zerofill_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->zerofill,
                    fd, offset, len, xdata);

        return 0;

err:
        UPCALL_STACK_UNWIND (zerofill, frame, -1, op_errno, NULL,
                             NULL, NULL);

        return 0;
}


int32_t
up_seek_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
             int op_errno, off_t offset, dict_t *xdata)
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
        flags = UP_UPDATE_CLIENT;
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 NULL, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (seek, frame, op_ret, op_errno, offset, xdata);

        return 0;
}


int32_t
up_seek (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
         gf_seek_what_t what, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, fd->inode, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_seek_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->seek, fd, offset, what, xdata);

        return 0;

err:
        UPCALL_STACK_UNWIND (seek, frame, -1, op_errno, 0, NULL);

        return 0;
}


int32_t
up_setxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        client_t         *client        = NULL;
        uint32_t         flags          = 0;
        upcall_local_t   *local         = NULL;
        int              ret            = 0;
        struct iatt      stbuf          = {0, };

        EXIT_IF_UPCALL_OFF (this, out);

        client = frame->root->client;
        local = frame->local;

        if ((op_ret < 0) || !local) {
                goto out;
        }

        flags = UP_XATTR;
        /* Remove the virtual xattrs from the dict */
        ret = dict_foreach (local->xattr, up_filter_virtual_xattr, NULL);
        if (ret < 0) {
                op_ret = ret;
                goto out;
        }

        ret = syncop_stat (FIRST_CHILD(frame->this), &local->loc, &stbuf,
                           NULL, NULL);
        if (ret == 0)
                flags |= UP_TIMES;

        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 &stbuf, NULL, NULL, local->xattr);

out:
        UPCALL_STACK_UNWIND (setxattr, frame, op_ret, op_errno, xdata);

        return 0;
}


int32_t
up_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
             int32_t flags, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;
        dict_t           *xattr          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        xattr = dict_copy_with_ref (dict, NULL);
        if (!xattr) {
                op_errno = ENOMEM;
                goto err;
        }

        local = upcall_local_init (frame, this, loc, NULL, loc->inode, xattr);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_setxattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr, loc, dict, flags,
                    xdata);

        return 0;

err:
        UPCALL_STACK_UNWIND (setxattr, frame, -1, op_errno, NULL);

        return 0;
}


int32_t
up_fsetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        client_t         *client        = NULL;
        uint32_t         flags          = 0;
        upcall_local_t   *local         = NULL;
        int              ret            = 0;
        struct iatt      stbuf          = {0,};

        EXIT_IF_UPCALL_OFF (this, out);

        client = frame->root->client;
        local = frame->local;

        if ((op_ret < 0) || !local) {
                goto out;
        }

        flags = UP_XATTR;
        /* Remove the virtual xattrs from the dict */
        ret = dict_foreach (local->xattr, up_filter_virtual_xattr, NULL);
        if (ret < 0) {
                op_ret = ret;
                goto out;
        }

        ret = syncop_fstat (FIRST_CHILD(frame->this), local->fd, &stbuf, NULL,
                            NULL);
        if (ret == 0)
                flags |= UP_TIMES;

        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 &stbuf, NULL, NULL, local->xattr);

out:
        UPCALL_STACK_UNWIND (fsetxattr, frame, op_ret, op_errno, xdata);

        return 0;
}


int32_t
up_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
              int32_t flags, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;
        dict_t           *xattr          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        xattr = dict_copy_with_ref (dict, NULL);
        if (!xattr) {
                op_errno = ENOMEM;
                goto err;
        }

        local = upcall_local_init (frame, this, NULL, fd, fd->inode, xattr);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_fsetxattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fsetxattr,
                    fd, dict, flags, xdata);

        return 0;

err:
        UPCALL_STACK_UNWIND (fsetxattr, frame, -1, op_errno, NULL);

        return 0;
}


int32_t
up_fremovexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        client_t         *client        = NULL;
        uint32_t         flags          = 0;
        upcall_local_t   *local         = NULL;
        struct iatt      stbuf          = {0,};
        int              ret            = 0;

        EXIT_IF_UPCALL_OFF (this, out);

        client = frame->root->client;
        local = frame->local;

        if ((op_ret < 0) || !local) {
                goto out;
        }
        flags = UP_XATTR_RM;

        ret = syncop_fstat (FIRST_CHILD(frame->this), local->fd, &stbuf, NULL,
                            NULL);
        if (ret == 0)
                flags |= UP_TIMES;

        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 &stbuf, NULL, NULL, local->xattr);

out:
        UPCALL_STACK_UNWIND (fremovexattr, frame, op_ret, op_errno,
                             xdata);
        return 0;
}


int32_t
up_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 const char *name, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;
        dict_t           *xattr          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        xattr = dict_for_key_value (name, "", 1);
        if (!xattr) {
                op_errno = ENOMEM;
                goto err;
        }

        local = upcall_local_init (frame, this, NULL, fd, fd->inode, xattr);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_fremovexattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fremovexattr,
                    fd, name, xdata);
        return 0;

err:
        UPCALL_STACK_UNWIND (fremovexattr, frame, -1, op_errno, NULL);

        return 0;
}


int32_t
up_removexattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        client_t         *client        = NULL;
        uint32_t         flags          = 0;
        upcall_local_t   *local         = NULL;
        struct iatt      stbuf          = {0,};
        int              ret            = 0;

        EXIT_IF_UPCALL_OFF (this, out);

        client = frame->root->client;
        local = frame->local;

        if ((op_ret < 0) || !local) {
                goto out;
        }
        flags = UP_XATTR_RM;

        ret = syncop_stat (FIRST_CHILD(frame->this), &local->loc, &stbuf, NULL,
                           NULL);
        if (ret == 0)
                flags |= UP_TIMES;

        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 &stbuf, NULL, NULL, local->xattr);

out:
        UPCALL_STACK_UNWIND (removexattr, frame, op_ret, op_errno,
                             xdata);
        return 0;
}


int32_t
up_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;
        dict_t           *xattr          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        xattr = dict_for_key_value (name, "", 1);
        if (!xattr) {
                op_errno = ENOMEM;
                goto err;
        }

        local = upcall_local_init (frame, this, loc, NULL, loc->inode, xattr);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_removexattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->removexattr,
                    loc, name, xdata);
        return 0;

err:
        UPCALL_STACK_UNWIND (removexattr, frame, -1, op_errno, NULL);

        return 0;
}


int32_t
up_fgetxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *dict,
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

        flags = UP_UPDATE_CLIENT;
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 NULL, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (fgetxattr, frame, op_ret, op_errno,
                             dict, xdata);
        return 0;
}


int32_t
up_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
              const char *name, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, fd->inode, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_fgetxattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->fgetxattr,
                    fd, name, xdata);
        return 0;
err:
        UPCALL_STACK_UNWIND (fgetxattr, frame, -1, op_errno,
                             NULL, NULL);
        return 0;
}


int32_t
up_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *dict,
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

        flags = UP_UPDATE_CLIENT;
        upcall_cache_invalidate (frame, this, client, local->inode, flags,
                                 NULL, NULL, NULL, NULL);

out:
        UPCALL_STACK_UNWIND (getxattr, frame, op_ret, op_errno,
                             dict, xdata);
        return 0;
}

int32_t
up_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
             const char *name, dict_t *xdata)
{
        int32_t          op_errno        = -1;
        upcall_local_t   *local          = NULL;

        EXIT_IF_UPCALL_OFF (this, out);

        local = upcall_local_init (frame, this, NULL, NULL, loc->inode, NULL);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

out:
        STACK_WIND (frame, up_getxattr_cbk,
                    FIRST_CHILD(this), FIRST_CHILD(this)->fops->getxattr,
                    loc, name, xdata);
        return 0;
err:
        UPCALL_STACK_UNWIND (getxattr, frame, -1, op_errno,
                             NULL, NULL);
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
                if (local->xattr) {
                        /* There will be 2 refs at this point, hence dict_destroy:
                         * 1. taken by dict_copy_with_ref
                         * 2. taken by upcall_local_init ()
                         */
                        dict_destroy (local->xattr);
                }
                loc_wipe (&local->rename_oldloc);
                loc_wipe (&local->loc);
                if (local->fd)
                        fd_unref (local->fd);
                mem_put (local);
        }
}

upcall_local_t *
upcall_local_init (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
                   inode_t *inode, dict_t *xattr)
{
        upcall_local_t *local = NULL;

        local = mem_get0 (THIS->local_pool);

        if (!local)
                goto out;

        local->inode = inode_ref (inode);
        if (xattr)
                local->xattr = dict_ref (xattr);

        /* Shall we get inode_ctx and store it here itself? */
        local->upcall_inode_ctx = upcall_inode_ctx_get (inode, this);

        if (loc)
                loc_copy (&local->loc, loc);
        if (fd)
                local->fd = fd_ref (fd);

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

        if (priv->cache_invalidation_enabled &&
            !priv->reaper_init_done) {
                ret = upcall_reaper_thread_init (this);

                if (ret) {
                        gf_msg ("upcall", GF_LOG_WARNING, 0,
                                UPCALL_MSG_INTERNAL_ERROR,
                                "reaper_thread creation failed (%s)."
                                " Disabling cache_invalidation",
                                strerror(errno));
                }
                priv->reaper_init_done = 1;
        }

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

        LOCK_INIT (&priv->inode_ctx_lk);
        INIT_LIST_HEAD (&priv->inode_ctx_list);

        this->private = priv;
        priv->fini = 0;
        priv->reaper_init_done = 0;

        this->local_pool = mem_pool_new (upcall_local_t, 512);
        ret = 0;

        if (priv->cache_invalidation_enabled) {
                ret = upcall_reaper_thread_init (this);

                if (ret) {
                        gf_msg ("upcall", GF_LOG_WARNING, 0,
                                UPCALL_MSG_INTERNAL_ERROR,
                                "reaper_thread creation failed (%s)."
                                " Disabling cache_invalidation",
                                strerror(errno));
                }
                priv->reaper_init_done = 1;
        }
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

        priv->fini = 1;

        pthread_join (priv->reaper_thr, NULL);

        LOCK_DESTROY (&priv->inode_ctx_lk);

        /* Do we need to cleanup the inode_ctxs? IMO not required
         * as inode_forget would have been done on all the inodes
         * before calling xlator_fini */
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
        struct gf_upcall    *up_req          = NULL;

        switch (event) {
        case GF_EVENT_UPCALL:
        {
                gf_log (this->name, GF_LOG_DEBUG, "Upcall Notify event = %d",
                        event);

                up_req = (struct gf_upcall *) data;

                GF_VALIDATE_OR_GOTO(this->name, up_req, out);

                ret = default_notify (this, event, up_req);

                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                UPCALL_MSG_NOTIFY_FAILED,
                                "Failed to notify cache invalidation"
                                " to client(%s)",
                                up_req->client_uid);
                        goto out;
                }
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
        /* fops which change only "ATIME" do not result
         * in any cache invalidation. Hence upcall
         * notifications are not sent in this case.
         * But however, we need to store/update the
         * client info in the upcall state to be able
         * to notify them incase of any changes done
         * to the data.
         *
         * Below such fops do not trigger upcall
         * notifications but will add/update
         * clients info in the upcall inode ctx.*/
        .lookup      = up_lookup,
        .open        = up_open,
        .statfs      = up_statfs,
        .opendir     = up_opendir,
        .readdir     = up_readdir,
        .readdirp    = up_readdirp,
        .stat        = up_stat,
        .fstat       = up_fstat,
        .access      = up_access,
        .readlink    = up_readlink,
        .readv       = up_readv,
        .lk          = up_lk,
        .seek        = up_seek,

        /* fops doing  write */
        .truncate    = up_truncate,
        .ftruncate   = up_ftruncate,
        .writev      = up_writev,
        .zerofill    = up_zerofill,
        .fallocate   = up_fallocate,
        .discard     = up_discard,

        /* fops changing attributes */
        .fsetattr    = up_fsetattr,
        .setattr     = up_setattr,

        /* fops affecting parent dirent */
        .mknod       = up_mknod,
        .create      = up_create,
        .symlink     = up_symlink,
        .mkdir       = up_mkdir,

        /* fops affecting both file and parent
         * cache entries */
        .unlink      = up_unlink,
        .link        = up_link,
        .rmdir       = up_rmdir,
        .rename      = up_rename,

        .setxattr    = up_setxattr,
        .fsetxattr   = up_fsetxattr,
        .getxattr    = up_getxattr,
        .fgetxattr   = up_fgetxattr,
        .fremovexattr = up_fremovexattr,
        .removexattr = up_removexattr,

#ifdef NOT_SUPPORTED
        /* internal lk fops */
        .inodelk     = up_inodelk,
        .finodelk    = up_finodelk,
        .entrylk     = up_entrylk,
        .fentrylk    = up_fentrylk,

        /* Below fops follow 'WRITE' which
         * would have already sent upcall
         * notifications */
        .flush       = up_flush,
        .fsync       = up_fsync,
        .fsyncdir    = up_fsyncdir,

        .xattrop     = up_xattrop,
        .fxattrop    = up_fxattrop,
#endif
};

struct xlator_cbks cbks = {
        .forget  = upcall_forget,
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
