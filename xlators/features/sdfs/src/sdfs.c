/*
   Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <libgen.h>
#include "sdfs.h"

static int
sdfs_frame_return (call_frame_t *frame)
{
        sdfs_local_t *local             = NULL;

        if (!frame)
                return -1;

        local = frame->local;

        return GF_ATOMIC_DEC (local->call_cnt);
}

static void
sdfs_lock_free (sdfs_entry_lock_t *entrylk)
{
        if (entrylk == NULL)
                goto out;

        loc_wipe (&entrylk->parent_loc);
        GF_FREE (entrylk->basename);

out:
        return;
}

static void
sdfs_lock_array_free (sdfs_lock_t *lock)
{
        sdfs_entry_lock_t       *entrylk = NULL;
        int                      i       = 0;

        if (lock == NULL)
                goto out;

        for (i = 0; i < lock->lock_count; i++) {
                entrylk = &lock->entrylk[i];
                sdfs_lock_free (entrylk);
        }

out:
        return;
}

static void
sdfs_local_cleanup (sdfs_local_t *local)
{
        if (!local)
                return;

        loc_wipe (&local->loc);
        loc_wipe (&local->parent_loc);

        if (local->stub) {
                call_stub_destroy (local->stub);
                local->stub = NULL;
        }

        sdfs_lock_array_free (local->lock);
        GF_FREE (local->lock);

        mem_put (local);
}

static int
sdfs_build_parent_loc (loc_t *parent, loc_t *child)
{
        int     ret     = -1;
        char    *path   = NULL;

        if (!child->parent) {
                goto out;
        }
        parent->inode = inode_ref (child->parent);
        path = gf_strdup (child->path);
        if (!path) {
                ret = -ENOMEM;
                goto out;
        }

        parent->path = dirname(path);
        if (!parent->path) {
                goto out;
        }

        gf_uuid_copy (parent->gfid, child->pargfid);
        return 0;

out:
        GF_FREE (path);
        return ret;
}

static sdfs_local_t *
sdfs_local_init (call_frame_t *frame, xlator_t *this)
{
        sdfs_local_t *local = NULL;

        local = mem_get0 (this->local_pool);
        if (!local)
                goto out;

        frame->local = local;
out:
        return local;
}

static int
sdfs_get_new_frame (call_frame_t *frame, loc_t *loc, call_frame_t **new_frame)
{
        int           ret       = -1;
        sdfs_local_t *local     = NULL;
        client_t     *client    = NULL;

        *new_frame = copy_frame (frame);
        if (!*new_frame) {
                goto err;
        }

        client = frame->root->client;
        gf_client_ref (client);
        (*new_frame)->root->client = client;
        local = sdfs_local_init (*new_frame, THIS);
        if (!local) {
                goto err;
        }

        local->main_frame = frame;

        ret = sdfs_build_parent_loc (&local->parent_loc, loc);
        if (ret) {
                goto err;
        }

        ret = loc_copy (&local->loc, loc);
        if (ret == -1) {
                goto err;
        }

        ret = 0;
err:
        if (ret == -1) {
                SDFS_STACK_DESTROY (frame);
        }
        return ret;
}

int
sdfs_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        sdfs_local_t    *local          = NULL;
        call_stub_t     *stub           = NULL;

        local = frame->local;

        local->op_ret = op_ret;
        local->op_errno = op_errno;

        if (local->stub) {
                stub = local->stub;
                local->stub = NULL;
                call_resume (stub);
        } else {
                if (op_ret < 0)
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                SDFS_MSG_ENTRYLK_ERROR,
                                "Unlocking entry lock failed for %s",
                                local->loc.name);

                SDFS_STACK_DESTROY (frame);
        }

        return 0;
}

int
sdfs_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, inode_t *inode,
                struct iatt *stbuf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        sdfs_local_t *local = NULL;

        local = frame->local;

        STACK_UNWIND_STRICT (mkdir, local->main_frame, op_ret, op_errno, inode,
                             stbuf, preparent, postparent, xdata);

        local->main_frame = NULL;
        STACK_WIND (frame, sdfs_entrylk_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->entrylk,
                    this->name, &local->parent_loc, local->loc.name,
                    ENTRYLK_UNLOCK, ENTRYLK_WRLCK, xdata);
        return 0;
}

int
sdfs_mkdir_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                   mode_t mode, mode_t umask, dict_t *xdata)
{
        sdfs_local_t    *local                  = NULL;
        char             gfid[GF_UUID_BUF_SIZE] = {0};
        int              op_errno               = -1;

        local = frame->local;

        gf_uuid_unparse(loc->pargfid, gfid);

        if (local->op_ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        SDFS_MSG_ENTRYLK_ERROR,
                        "Acquiring entry lock failed for directory %s "
                        "with parent gfid %s", local->loc.name, gfid);
                op_errno = local->op_errno;
                goto err;
        }

        STACK_WIND (frame, sdfs_mkdir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mkdir, loc,
                    mode, umask, xdata);

        return 0;
err:
        STACK_UNWIND_STRICT (mkdir, local->main_frame, -1, op_errno,
                             NULL, NULL, NULL, NULL, NULL);

        local->main_frame = NULL;
        SDFS_STACK_DESTROY (frame);
        return 0;
}

int
sdfs_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
            mode_t umask, dict_t *xdata)
{
        sdfs_local_t    *local      = NULL;
        call_frame_t    *new_frame  = NULL;
        call_stub_t     *stub       = NULL;
        int              op_errno   = 0;

        if (-1 == sdfs_get_new_frame (frame, loc, &new_frame)) {
                op_errno = ENOMEM;
                goto err;
        }

        stub = fop_mkdir_stub (new_frame, sdfs_mkdir_helper, loc, mode,
                               umask, xdata);
        if (!stub) {
                op_errno = ENOMEM;
                goto err;
        }

        local = new_frame->local;
        local->stub = stub;

        STACK_WIND (new_frame, sdfs_entrylk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->entrylk,
                    this->name, &local->parent_loc, local->loc.name,
                    ENTRYLK_LOCK, ENTRYLK_WRLCK, xdata);

        return 0;
err:
        STACK_UNWIND_STRICT (mkdir, frame, -1, op_errno, NULL, NULL,
                             NULL, NULL, NULL);

        if (new_frame)
                SDFS_STACK_DESTROY (new_frame);

        return 0;
}

int
sdfs_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        sdfs_local_t *local = NULL;

        local = frame->local;

        STACK_UNWIND_STRICT (rmdir, local->main_frame, op_ret, op_errno,
                             preparent, postparent, xdata);

        local->main_frame = NULL;
        STACK_WIND (frame, sdfs_entrylk_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->entrylk,
                    this->name, &local->parent_loc, local->loc.name,
                    ENTRYLK_UNLOCK, ENTRYLK_WRLCK, xdata);
        return 0;
}

int
sdfs_rmdir_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                   int flags, dict_t *xdata)
{
        sdfs_local_t    *local                  = NULL;
        char             gfid[GF_UUID_BUF_SIZE] = {0};

        local = frame->local;

        gf_uuid_unparse(loc->pargfid, gfid);

        if (local->op_ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        SDFS_MSG_ENTRYLK_ERROR,
                        "Acquiring entry lock failed for directory %s "
                        "with parent gfid %s", local->loc.name, gfid);
                goto err;
        }

        STACK_WIND (frame, sdfs_rmdir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rmdir, loc,
                    flags, xdata);

        return 0;
err:
        STACK_UNWIND_STRICT (rmdir, local->main_frame, -1, local->op_errno,
                             NULL, NULL, NULL);

        local->main_frame = NULL;
        SDFS_STACK_DESTROY (frame);
        return 0;
}

int
sdfs_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags,
            dict_t *xdata)
{
        sdfs_local_t    *local      = NULL;
        call_frame_t    *new_frame  = NULL;
        call_stub_t     *stub       = NULL;
        int              op_errno   = 0;

        if (-1 == sdfs_get_new_frame (frame, loc, &new_frame)) {
                op_errno = ENOMEM;
                goto err;
        }

        stub = fop_rmdir_stub (new_frame, sdfs_rmdir_helper, loc, flags, xdata);
        if (!stub) {
                op_errno = ENOMEM;
                goto err;
        }

        local = new_frame->local;
        local->stub = stub;

        STACK_WIND (new_frame, sdfs_entrylk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->entrylk,
                    this->name, &local->parent_loc, local->loc.name,
                    ENTRYLK_LOCK, ENTRYLK_WRLCK, xdata);

        return 0;
err:
        STACK_UNWIND_STRICT (rmdir, frame, -1, op_errno, NULL, NULL,
                             NULL);

        if (new_frame)
                SDFS_STACK_DESTROY (new_frame);

        return 0;
}

int
sdfs_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno,
                 fd_t *fd, inode_t *inode, struct iatt *stbuf,
                 struct iatt *preparent, struct iatt *postparent,
                 dict_t *xdata)
{
        sdfs_local_t *local = NULL;

        local = frame->local;

        STACK_UNWIND_STRICT (create, local->main_frame, op_ret, op_errno, fd,
                             inode, stbuf, preparent, postparent, xdata);

        local->main_frame = NULL;
        STACK_WIND (frame, sdfs_entrylk_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->entrylk,
                    this->name, &local->parent_loc, local->loc.name,
                    ENTRYLK_UNLOCK, ENTRYLK_WRLCK, xdata);
        return 0;
}

int
sdfs_create_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    int32_t flags, mode_t mode, mode_t umask, fd_t *fd,
                    dict_t *xdata)
{
        sdfs_local_t    *local                  = NULL;
        char             gfid[GF_UUID_BUF_SIZE] = {0};

        local = frame->local;

        gf_uuid_unparse(loc->pargfid, gfid);

        if (local->op_ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        SDFS_MSG_ENTRYLK_ERROR,
                        "Acquiring entry lock failed for directory %s "
                        "with parent gfid %s", local->loc.name, gfid);
                goto err;
        }

        STACK_WIND (frame, sdfs_create_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create, loc, flags,
                    mode, umask, fd, xdata);

        return 0;
err:
        STACK_UNWIND_STRICT (create, local->main_frame, -1, local->op_errno,
                             NULL, NULL, NULL, NULL, NULL, NULL);

        local->main_frame = NULL;
        SDFS_STACK_DESTROY (frame);
        return 0;
}

int
sdfs_create (call_frame_t *frame, xlator_t *this, loc_t *loc,
             int32_t flags, mode_t mode, mode_t umask,
             fd_t *fd, dict_t *xdata)
{
        sdfs_local_t    *local      = NULL;
        call_frame_t    *new_frame  = NULL;
        call_stub_t     *stub       = NULL;
        int              op_errno   = 0;

        if (-1 == sdfs_get_new_frame (frame, loc, &new_frame)) {
                op_errno = ENOMEM;
                goto err;
        }

        stub = fop_create_stub (new_frame, sdfs_create_helper, loc,
                                flags, mode, umask, fd, xdata);
        if (!stub) {
                op_errno = ENOMEM;
                goto err;
        }

        local = new_frame->local;
        local->stub = stub;

        STACK_WIND (new_frame, sdfs_entrylk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->entrylk,
                    this->name, &local->parent_loc, local->loc.name,
                    ENTRYLK_LOCK, ENTRYLK_WRLCK, xdata);

        return 0;
err:
        STACK_UNWIND_STRICT (create, frame, -1, op_errno, NULL, NULL,
                             NULL, NULL, NULL, NULL);

        if (new_frame)
                SDFS_STACK_DESTROY (new_frame);

        return 0;
}

int
sdfs_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                 struct iatt *postparent, dict_t *xdata)
{
        sdfs_local_t *local = NULL;

        local = frame->local;

        STACK_UNWIND_STRICT (unlink, local->main_frame, op_ret, op_errno,
                             preparent, postparent, xdata);

        local->main_frame = NULL;
        STACK_WIND (frame, sdfs_entrylk_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->entrylk,
                    this->name, &local->parent_loc, local->loc.name,
                    ENTRYLK_UNLOCK, ENTRYLK_WRLCK, xdata);
        return 0;
}

int
sdfs_unlink_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    int flags, dict_t *xdata)
{
        sdfs_local_t    *local                  = NULL;
        char             gfid[GF_UUID_BUF_SIZE] = {0};

        local = frame->local;

        gf_uuid_unparse(loc->pargfid, gfid);

        if (local->op_ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        SDFS_MSG_ENTRYLK_ERROR,
                        "Acquiring entry lock failed for directory %s "
                        "with parent gfid %s", local->loc.name, gfid);
                goto err;
        }

        STACK_WIND (frame, sdfs_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink, loc, flags, xdata);

        return 0;
err:
        STACK_UNWIND_STRICT (unlink, local->main_frame, -1, local->op_errno,
                             NULL, NULL, NULL);

        local->main_frame = NULL;
        SDFS_STACK_DESTROY (frame);
        return 0;
}

int
sdfs_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc,
             int flags, dict_t *xdata)
{
        sdfs_local_t    *local      = NULL;
        call_frame_t    *new_frame  = NULL;
        call_stub_t     *stub       = NULL;
        int              op_errno   = 0;

        if (-1 == sdfs_get_new_frame (frame, loc, &new_frame)) {
                op_errno = ENOMEM;
                goto err;
        }

        stub = fop_unlink_stub (new_frame, sdfs_unlink_helper, loc,
                                flags, xdata);
        if (!stub) {
                op_errno = ENOMEM;
                goto err;
        }

        local = new_frame->local;
        local->stub = stub;

        STACK_WIND (new_frame, sdfs_entrylk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->entrylk,
                    this->name, &local->parent_loc, local->loc.name,
                    ENTRYLK_LOCK, ENTRYLK_WRLCK, xdata);

        return 0;
err:
        STACK_UNWIND_STRICT (unlink, frame, -1, op_errno, NULL, NULL,
                             NULL);

        if (new_frame)
                SDFS_STACK_DESTROY (new_frame);

        return 0;
}

int
sdfs_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct iatt *stbuf, struct iatt *preparent,
                  struct iatt *postparent, dict_t *xdata)
{
        sdfs_local_t *local = NULL;

        local = frame->local;

        STACK_UNWIND_STRICT (link, local->main_frame, op_ret, op_errno, inode,
                             stbuf, preparent, postparent, xdata);

        local->main_frame = NULL;
        STACK_WIND (frame, sdfs_entrylk_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->entrylk,
                    this->name, &local->parent_loc, local->loc.name,
                    ENTRYLK_UNLOCK, ENTRYLK_WRLCK, xdata);
        return 0;
}

int
sdfs_symlink_helper (call_frame_t *frame, xlator_t *this,
                     const char *linkname, loc_t *loc, mode_t umask,
                     dict_t *xdata)
{
        sdfs_local_t    *local                  = NULL;
        char             gfid[GF_UUID_BUF_SIZE] = {0};

        local = frame->local;

        gf_uuid_unparse(loc->pargfid, gfid);

        if (local->op_ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        SDFS_MSG_ENTRYLK_ERROR,
                        "Acquiring entry lock failed for directory %s "
                        "with parent gfid %s", local->loc.name, gfid);
                goto err;
        }

        STACK_WIND (frame, sdfs_symlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->symlink, linkname, loc,
                    umask, xdata);

        return 0;
err:
        STACK_UNWIND_STRICT (link, local->main_frame, -1, local->op_errno,
                             NULL, NULL, NULL, NULL, NULL);

        local->main_frame = NULL;
        SDFS_STACK_DESTROY (frame);
        return 0;
}

int
sdfs_symlink (call_frame_t *frame, xlator_t *this, const char *linkname,
              loc_t *loc, mode_t umask, dict_t *xdata)
{
        sdfs_local_t    *local      = NULL;
        call_frame_t    *new_frame  = NULL;
        call_stub_t     *stub       = NULL;
        int              op_errno   = 0;

        if (-1 == sdfs_get_new_frame (frame, loc, &new_frame)) {
                op_errno = ENOMEM;
                goto err;
        }

        stub = fop_symlink_stub (new_frame, sdfs_symlink_helper, linkname, loc,
                                 umask, xdata);
        if (!stub) {
                op_errno = ENOMEM;
                goto err;
        }

        local = new_frame->local;
        local->stub = stub;

        STACK_WIND (new_frame, sdfs_entrylk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->entrylk,
                    this->name, &local->parent_loc, local->loc.name,
                    ENTRYLK_LOCK, ENTRYLK_WRLCK, xdata);

        return 0;
err:
        STACK_UNWIND_STRICT (link, frame, -1, op_errno, NULL, NULL,
                             NULL, NULL, NULL);

        if (new_frame)
                SDFS_STACK_DESTROY (new_frame);

        return 0;
}

int
sdfs_common_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        sdfs_local_t    *local          = NULL;
        int              this_call_cnt  = 0;
        int              lk_index       = 0;
        sdfs_lock_t     *locks          = NULL;
        call_stub_t     *stub           = NULL;

        local = frame->local;
        locks = local->lock;
        lk_index = (long) cookie;

        if (op_ret < 0) {
                local->op_ret = op_ret;
                local->op_errno = op_errno;
        } else {
                locks->entrylk->locked[lk_index] = _gf_true;
        }

        this_call_cnt = sdfs_frame_return (frame);
        if (this_call_cnt > 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "As there are more callcnt (%d) returning without WIND",
                        this_call_cnt);
                return 0;
        }

        if (local->stub) {
                stub = local->stub;
                local->stub = NULL;
                call_resume (stub);
        } else {
                if (local->op_ret < 0)
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                SDFS_MSG_ENTRYLK_ERROR,
                                "unlocking entry lock failed ");
                SDFS_STACK_DESTROY (frame);
        }

        return 0;
}

int
sdfs_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno,
               inode_t *inode, struct iatt *stbuf, struct iatt *preparent,
               struct iatt *postparent, dict_t *xdata)
{
        sdfs_local_t             *local          = NULL;
        sdfs_lock_t              *lock           = NULL;
        int                      i              = 0;
        int lock_count = 0;

        local = frame->local;
        lock  = local->lock;

        STACK_UNWIND_STRICT (link, local->main_frame, op_ret, op_errno, inode,
                             stbuf, preparent, postparent, xdata);

        local->main_frame = NULL;
        lock_count = lock->lock_count;
        for (i = 0; i < lock_count; i++) {
                STACK_WIND_COOKIE (frame, sdfs_common_entrylk_cbk,
                                   (void *)(long) i,
                                   FIRST_CHILD (this),
                                   FIRST_CHILD(this)->fops->entrylk,
                                   this->name, &lock->entrylk[i].parent_loc,
                                   lock->entrylk[i].basename,
                                   ENTRYLK_UNLOCK, ENTRYLK_WRLCK, xdata);
        }

        return 0;
}

int
sdfs_link_helper (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                  loc_t *newloc, dict_t *xdata)
{
        sdfs_local_t            *local                  = NULL;
        sdfs_lock_t             *locks                  = NULL;
        gf_boolean_t             stack_destroy          = _gf_true;
        int                      lock_count             = 0;
        int                      i                      = 0;

        local = frame->local;
        locks = local->lock;

        if (local->op_ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        SDFS_MSG_ENTRYLK_ERROR,
                        "Acquiring entry lock failed");
                goto err;
        }

        STACK_WIND (frame, sdfs_link_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->link, oldloc, newloc,
                    xdata);

        return 0;
err:
        STACK_UNWIND_STRICT (link, local->main_frame, -1, local->op_errno,
                             NULL, NULL, NULL, NULL, NULL);

        local->main_frame = NULL;
        for (i = 0; i < locks->lock_count && locks->entrylk->locked[i]; i++) {
                lock_count++;
        }
        GF_ATOMIC_INIT (local->call_cnt, lock_count);

        for (i = 0; i < lock_count; i++) {
                if (!locks->entrylk->locked[i]) {
                        lock_count++;
                        continue;
                }

                stack_destroy = _gf_false;
                STACK_WIND (frame, sdfs_common_entrylk_cbk,
                            FIRST_CHILD (this),
                            FIRST_CHILD(this)->fops->entrylk,
                            this->name, &locks->entrylk[i].parent_loc,
                            locks->entrylk[i].basename,
                            ENTRYLK_UNLOCK, ENTRYLK_WRLCK, xdata);
        }

        if (stack_destroy)
                SDFS_STACK_DESTROY (frame);

        return 0;
}

static int
sdfs_init_entry_lock (sdfs_entry_lock_t *lock, loc_t *loc)
{
        int ret = 0;

        ret = sdfs_build_parent_loc (&lock->parent_loc, loc);
        if (ret)
                return -1;

        lock->basename = gf_strdup (loc->name);
        if (!lock->basename)
                return -1;

        return 0;
}

int
sdfs_entry_lock_cmp (const void *l1, const void *l2)
{
        const sdfs_entry_lock_t        *r1      = l1;
        const sdfs_entry_lock_t        *r2      = l2;
        int                             ret     = 0;
        uuid_t                          gfid1   = {0};
        uuid_t                          gfid2   = {0};

        loc_gfid ((loc_t *)&r1->parent_loc, gfid1);
        loc_gfid ((loc_t *)&r2->parent_loc, gfid2);
        ret = gf_uuid_compare (gfid1, gfid2);
        /*Entrylks with NULL basename are the 'smallest'*/
        if (ret == 0) {
                if (!r1->basename)
                        return -1;
                if (!r2->basename)
                        return 1;
                ret = strcmp (r1->basename, r2->basename);
        }

        if (ret <= 0)
                return -1;
        else
                return 1;
}

int
sdfs_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
           loc_t *newloc, dict_t *xdata)
{
        sdfs_local_t            *local      = NULL;
        call_frame_t            *new_frame  = NULL;
        call_stub_t             *stub       = NULL;
        sdfs_lock_t             *lock       = NULL;
        client_t                *client     = NULL;
        int                      ret        = 0;
        int                      op_errno   = 0;

        new_frame = copy_frame (frame);
        if (!new_frame) {
                op_errno = ENOMEM;
                goto err;
        }

        gf_client_ref (client);
        new_frame->root->client = client;
        local = sdfs_local_init (new_frame, this);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->main_frame = frame;

        lock = GF_CALLOC (1, sizeof (*lock), gf_common_mt_char);
        if (!lock)
                goto err;

        local->lock = lock;

        ret = sdfs_init_entry_lock (&lock->entrylk[0], newloc);
        if (ret)
                goto err;

        ++lock->lock_count;

        local->lock = lock;
        GF_ATOMIC_INIT (local->call_cnt, lock->lock_count);

        loc_copy (&local->loc, newloc);
        if (ret == -1) {
                op_errno = ENOMEM;
                goto err;
        }

        stub = fop_link_stub (new_frame, sdfs_link_helper, oldloc,
                              newloc, xdata);
        if (!stub) {
                op_errno = ENOMEM;
                goto err;
        }

        local->stub = stub;

        STACK_WIND_COOKIE (new_frame, sdfs_common_entrylk_cbk,
                           0, FIRST_CHILD (this),
                           FIRST_CHILD(this)->fops->entrylk,
                           this->name, &lock->entrylk[0].parent_loc,
                           lock->entrylk[0].basename, ENTRYLK_LOCK,
                           ENTRYLK_WRLCK, xdata);

        return 0;
err:

        STACK_UNWIND_STRICT (link, frame, -1, op_errno, NULL, NULL,
                             NULL, NULL, NULL);

        if (new_frame)
                SDFS_STACK_DESTROY (new_frame);

        return 0;
}

int
sdfs_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, inode_t *inode,
                struct iatt *stbuf, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        sdfs_local_t *local = NULL;

        local = frame->local;

        STACK_UNWIND_STRICT (mknod, local->main_frame, op_ret, op_errno, inode,
                             stbuf, preparent, postparent, xdata);

        local->main_frame = NULL;
        STACK_WIND (frame, sdfs_entrylk_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->entrylk,
                    this->name, &local->parent_loc, local->loc.name,
                    ENTRYLK_UNLOCK, ENTRYLK_WRLCK, xdata);
        return 0;
}

int
sdfs_mknod_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                   mode_t mode, dev_t rdev, mode_t umask, dict_t *xdata)
{
        sdfs_local_t    *local                   = NULL;
        char             gfid[GF_UUID_BUF_SIZE]  = {0};

        local = frame->local;

        gf_uuid_unparse(loc->pargfid, gfid);

        if (local->op_ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        SDFS_MSG_ENTRYLK_ERROR,
                        "Acquiring entry lock failed for directory %s "
                        "with parent gfid %s", local->loc.name, gfid);
                goto err;
        }

        STACK_WIND (frame, sdfs_mknod_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mknod, loc, mode, rdev,
                    umask, xdata);

        return 0;
err:
        STACK_UNWIND_STRICT (mknod, local->main_frame, -1, local->op_errno,
                             NULL, NULL, NULL, NULL, NULL);

        local->main_frame = NULL;
        SDFS_STACK_DESTROY (frame);
        return 0;
}

int
sdfs_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
            dev_t rdev, mode_t umask, dict_t *xdata)
{
        sdfs_local_t            *local      = NULL;
        call_frame_t            *new_frame  = NULL;
        call_stub_t             *stub       = NULL;
        int                      op_errno   = 0;

        if (-1 == sdfs_get_new_frame (frame, loc, &new_frame)) {
                op_errno = ENOMEM;
                goto err;
        }

        stub = fop_mknod_stub (new_frame, sdfs_mknod_helper, loc, mode,
                               rdev, umask, xdata);
        if (!stub) {
                op_errno = ENOMEM;
                goto err;
        }

        local = new_frame->local;
        local->stub = stub;

        STACK_WIND (new_frame, sdfs_entrylk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->entrylk,
                    this->name, &local->parent_loc, local->loc.name,
                    ENTRYLK_LOCK, ENTRYLK_WRLCK, xdata);

        return 0;
err:
        STACK_UNWIND_STRICT (mknod, frame, -1, op_errno, NULL, NULL,
                             NULL, NULL, NULL);

        if (new_frame)
                SDFS_STACK_DESTROY (new_frame);

        return 0;
}

int
sdfs_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct iatt *stbuf,
                 struct iatt *preoldparent, struct iatt *postoldparent,
                 struct iatt *prenewparent, struct iatt *postnewparent,
                 dict_t *xdata)
{
        sdfs_local_t    *local = NULL;
        sdfs_lock_t     *lock  = NULL;
        int              i     = 0;
        int              call_cnt = 0;

        local = frame->local;
        lock = local->lock;
        GF_ATOMIC_INIT (local->call_cnt, lock->lock_count);

        STACK_UNWIND_STRICT (rename, local->main_frame, op_ret, op_errno, stbuf,
                             preoldparent, postoldparent, prenewparent,
                             postnewparent, xdata);

        local->main_frame = NULL;
        call_cnt = GF_ATOMIC_GET (local->call_cnt);

        for (i = 0; i < call_cnt; i++) {
                STACK_WIND_COOKIE (frame, sdfs_common_entrylk_cbk,
                                   (void *)(long) i,
                                   FIRST_CHILD (this),
                                   FIRST_CHILD(this)->fops->entrylk,
                                   this->name, &lock->entrylk[i].parent_loc,
                                   lock->entrylk[i].basename,
                                   ENTRYLK_UNLOCK, ENTRYLK_WRLCK, xdata);
        }

        return 0;
}

int
sdfs_rename_helper (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                    loc_t *newloc, dict_t *xdata)
{
        sdfs_local_t            *local                  = NULL;
        sdfs_lock_t             *lock                   = NULL;
        gf_boolean_t             stack_destroy          = _gf_true;
        int                      lock_count             = 0;
        int                      i                      = 0;

        local = frame->local;
        lock = local->lock;

        if (local->op_ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        SDFS_MSG_ENTRYLK_ERROR,
                        "Acquiring entry lock failed ");
               goto err;
        }

        STACK_WIND (frame, sdfs_rename_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename, oldloc, newloc,
                    xdata);

        return 0;

err:
        STACK_UNWIND_STRICT (rename, local->main_frame, -1, local->op_errno,
                             NULL, NULL, NULL, NULL, NULL, NULL);

        local->main_frame = NULL;
        for (i = 0; i < lock->lock_count && lock->entrylk->locked[i]; i++) {
                lock_count++;
        }
        GF_ATOMIC_INIT (local->call_cnt, lock_count);

        for (i = 0; i < lock_count; i++) {
                if (!lock->entrylk->locked[i]) {
                        lock_count++;
                        continue;
                }
                stack_destroy = _gf_false;
                STACK_WIND (frame, sdfs_common_entrylk_cbk,
                            FIRST_CHILD (this),
                            FIRST_CHILD(this)->fops->entrylk,
                            this->name, &lock->entrylk[i].parent_loc,
                            lock->entrylk[i].basename,
                            ENTRYLK_UNLOCK, ENTRYLK_WRLCK, xdata);
        }

        if (stack_destroy)
                SDFS_STACK_DESTROY (frame);

        return 0;
}

int
sdfs_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
             loc_t *newloc, dict_t *xdata)
{
        sdfs_local_t    *local      = NULL;
        sdfs_lock_t     *lock       = NULL;
        call_frame_t    *new_frame  = NULL;
        call_stub_t     *stub       = NULL;
        client_t        *client     = NULL;
        int              ret        = 0;
        int              op_errno   = -1;
        int              i          = 0;
        int              call_cnt   = 0;

        new_frame = copy_frame (frame);
        if (!new_frame) {
                op_errno = ENOMEM;
                goto err;
        }

        gf_client_ref (client);
        new_frame->root->client = client;
        local = sdfs_local_init (new_frame, this);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->main_frame = frame;

        lock = GF_CALLOC (1, sizeof (*lock), gf_common_mt_char);
        if (!lock)
                goto err;

        local->lock = lock;

        ret = sdfs_init_entry_lock (&lock->entrylk[0], oldloc);
        if (ret)
                goto err;
        lock->entrylk->locked[0] = _gf_false;

        ++lock->lock_count;

        ret = sdfs_init_entry_lock (&lock->entrylk[1], newloc);
        if (ret)
                goto err;
        lock->entrylk->locked[1] = _gf_false;

        ++lock->lock_count;

        qsort (lock->entrylk, lock->lock_count, sizeof (*lock->entrylk),
               sdfs_entry_lock_cmp);

        local->lock = lock;
        GF_ATOMIC_INIT (local->call_cnt, lock->lock_count);

        stub = fop_rename_stub (new_frame, sdfs_rename_helper, oldloc,
                                newloc, xdata);
        if (!stub) {
                op_errno = ENOMEM;
                goto err;
        }

        local->stub = stub;
        call_cnt = GF_ATOMIC_GET (local->call_cnt);
        for (i = 0; i < call_cnt; i++) {
                STACK_WIND_COOKIE (new_frame, sdfs_common_entrylk_cbk,
                                   (void *)(long) i,
                                   FIRST_CHILD (this),
                                   FIRST_CHILD(this)->fops->entrylk,
                                   this->name, &lock->entrylk[i].parent_loc,
                                   lock->entrylk[i].basename,
                                   ENTRYLK_LOCK, ENTRYLK_WRLCK, xdata);
        }

        return 0;
err:

        STACK_UNWIND_STRICT (rename, frame, -1, op_errno, NULL, NULL,
                             NULL, NULL, NULL, NULL);

        if (new_frame)
                SDFS_STACK_DESTROY (new_frame);

        return 0;
}

int
sdfs_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, inode_t *inode,
                 struct iatt *stbuf,  dict_t *xdata,
                 struct iatt *postparent)
{
        sdfs_local_t *local = NULL;

        local = frame->local;

        if (!local->loc.parent) {
                sdfs_local_cleanup (local);
                frame->local = NULL;
                STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno,
                                     inode, stbuf, xdata, postparent);
                return 0;
        }

        STACK_UNWIND_STRICT (lookup, local->main_frame, op_ret, op_errno, inode,
                             stbuf, xdata, postparent);

        local->main_frame = NULL;
        STACK_WIND (frame, sdfs_entrylk_cbk, FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->entrylk,
                    this->name, &local->parent_loc, local->loc.name,
                    ENTRYLK_UNLOCK, ENTRYLK_RDLCK, xdata);
        return 0;
}

int
sdfs_lookup_helper (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    dict_t *xdata)
{
        sdfs_local_t    *local                  = NULL;
        char             gfid[GF_UUID_BUF_SIZE] = {0};

        local = frame->local;

        gf_uuid_unparse(loc->pargfid, gfid);

        if (local->op_ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        SDFS_MSG_ENTRYLK_ERROR,
                        "Acquiring entry lock failed for directory %s "
                        "with parent gfid %s", local->loc.name, gfid);
                goto err;
        }

        STACK_WIND (frame, sdfs_lookup_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->lookup, loc, xdata);

        return 0;
err:
        STACK_UNWIND_STRICT (lookup, local->main_frame, -1, local->op_errno,
                             NULL, NULL, NULL, NULL);
        local->main_frame = NULL;

        SDFS_STACK_DESTROY (frame);
        return 0;
}

int
sdfs_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc,
             dict_t *xdata)
{
        sdfs_local_t            *local      = NULL;
        call_frame_t            *new_frame  = NULL;
        call_stub_t             *stub       = NULL;
        int                      op_errno   = 0;

        if (!loc->parent) {
                local = sdfs_local_init (frame, this);
                if (!local) {
                        op_errno = ENOMEM;
                        goto err;
                }

                STACK_WIND_TAIL(frame, FIRST_CHILD(this),
                                FIRST_CHILD(this)->fops->lookup,
                                loc, xdata);
                return 0;
        }

        if (-1 == sdfs_get_new_frame (frame, loc, &new_frame)) {
                op_errno = ENOMEM;
                goto err;
        }

        stub = fop_lookup_stub (new_frame, sdfs_lookup_helper, loc,
                                xdata);
        if (!stub) {
                op_errno = ENOMEM;
                goto err;
        }

        local = new_frame->local;
        local->stub = stub;

        STACK_WIND (new_frame, sdfs_entrylk_cbk,
                    FIRST_CHILD (this),
                    FIRST_CHILD(this)->fops->entrylk,
                    this->name, &local->parent_loc, local->loc.name,
                    ENTRYLK_LOCK, ENTRYLK_RDLCK, xdata);

        return 0;

err:
        STACK_UNWIND_STRICT (lookup, frame, -1, op_errno, NULL, NULL,
                             NULL, NULL);

        if (new_frame)
                SDFS_STACK_DESTROY (new_frame);

        return 0;
}

int
init (xlator_t *this)
{
        int ret = -1;

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "'dentry-fop-serializer' not configured with exactly one child");
                goto out;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile ");
        }

        this->local_pool = mem_pool_new (sdfs_local_t, 512);
        if (!this->local_pool) {
                goto out;
        }

        ret = 0;

out:
        return ret;
}

int
fini (xlator_t *this)
{
        mem_pool_destroy (this->local_pool);

        return 0;
}


struct xlator_fops fops = {
        .mkdir      = sdfs_mkdir,
        .rmdir      = sdfs_rmdir,
        .create     = sdfs_create,
        .unlink     = sdfs_unlink,
        .symlink    = sdfs_symlink,
        .link       = sdfs_link,
        .mknod      = sdfs_mknod,
        .rename     = sdfs_rename,
        .lookup     = sdfs_lookup,
};

struct xlator_cbks cbks;

