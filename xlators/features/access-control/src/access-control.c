/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "access-control.h"
#include "xlator.h"
#include "call-stub.h"
#include "defaults.h"
#include "iatt.h"

/* Careful, this function erases the stub from frame->local. Dont call this if
 * a subsequent callback requires retaining access to the stub. This should be
 * called at the end of all access-control related operations, i.e. once the
 * frame will be handed off to the actual fop and the next callback that will
 * be called is the default callback. IOW, the function where call_resume is
 * called.
 * NOTE: this is required because FRAME_DESTROY tries to free frame->local if
 * it finds it to be non-NULL.
 */
call_stub_t *
__get_frame_stub (call_frame_t *fr)
{
        call_stub_t     *st = NULL;

        if (!fr)
                return NULL;

        st = fr->local;
        fr->local = NULL;

        return st;
}

int
ac_test_owner_access (struct iatt *ia, uid_t uid, int accesstest)
{
        int     ret = -1;

        if (!ia)
                return -1;

        /* First test permissions using the uid. */
        if (ia->ia_uid != uid) {
                ret = -1;
                goto out;
        }

        /* At this point we know, the uid matches that of the stat structure, so
         * if the caller does not care, we should return success.
         */
        if (ac_test_dontcare (accesstest)) {
                ret = 0;
                goto out;
        }

        if (ac_test_read (accesstest))
                ret = IA_PROT_RUSR (ia->ia_prot);

        if (ac_test_write (accesstest))
                ret = IA_PROT_WUSR (ia->ia_prot);

        if (ac_test_exec (accesstest))
                ret = IA_PROT_XUSR (ia->ia_prot);

        /* For failed access test for owner, we need to return EACCES */
        if (!ret)
                ret = -1;
        else
                ret = 0;
out:
        return ret;
}


int
ac_test_group_access (struct iatt *ia, gid_t gid, gid_t *auxgids, int auxcount,
                      int accesstest)
{
        int     ret = -1;
        int     testgid = -1;
        int     x = 0;

        if (!ia)
                return -1;
        /* First, determine which gid to test against. This will be determined
         * by first checking which of the gids given to us match the gid in the
         * stat. If none match, then we go to checking with others as the user.
         */

        /* If we are only given the primary gid. Dont depend on @auxgids
         * being NULL since I know users of this function can pass statically
         * allocated arrays which cant be NULL and yet contain no valid gids.
         */

        if ((ia->ia_gid != gid) && (auxcount == 0)) {
                ret = -1;
                goto out;
        }

        if (ia->ia_gid == gid)
                testgid = gid;
        else {
                for (; x < auxcount; ++x) {
                        if (ia->ia_gid == auxgids[x]) {
                                testgid = ia->ia_gid;
                                break;
                        }
                }
        }

        /* None of the gids match with the gid in the stat. */
        if (testgid == -1) {
                ret = -1;
                goto out;
        }

        /* At this point, at least one gid matches that in the stat, now we must
         * check whether the caller is interested in the access check at all.
         */
        if (ac_test_dontcare (accesstest)) {
                ret = 0;
                goto out;
        }

        if (ac_test_read (accesstest))
                ret = IA_PROT_RGRP (ia->ia_prot);

        if (ac_test_write (accesstest))
                ret = IA_PROT_WGRP (ia->ia_prot);

        if (ac_test_exec (accesstest))
                ret = IA_PROT_XGRP (ia->ia_prot);

        if (!ret)
                ret = -1;
        else
                ret = 0;

out:
        return ret;
}


int
ac_test_other_access (struct iatt *ia, int accesstest)
{
        int     ret = 0;

        if (!ia)
                return -1;

        if (ac_test_read (accesstest))
                ret = IA_PROT_ROTH (ia->ia_prot);

        if (ac_test_write (accesstest))
                ret = IA_PROT_WOTH (ia->ia_prot);

        if (ac_test_exec (accesstest))
                ret = IA_PROT_XOTH (ia->ia_prot);

        if (!ret)
                ret = -1;
        else
                ret = 0;

        return ret;
}


/* Returns -1 on a failed access test with @operrno set to the relevant error
 * number.
 */
int
ac_test_access (struct iatt *ia, uid_t uid, gid_t gid, gid_t *auxgids,
                int auxcount, int accesstest, int testwho, int *operrno)
{
        int             ret = -1;

        if ((!ia) || (!operrno))
                return -1;

        if ((uid == 0) && (gid == 0)) {
                gf_log (ACTRL, GF_LOG_TRACE, "Root has access");
                return 0;
        }

        if (ac_test_owner (testwho)) {
                gf_log (ACTRL, GF_LOG_TRACE, "Testing owner access");
                ret = ac_test_owner_access (ia, uid, accesstest);
        }

        if (ret == 0) {
                gf_log (ACTRL, GF_LOG_TRACE, "Owner has access");
                goto out;
        }

        if (ac_test_group (testwho)) {
                gf_log (ACTRL, GF_LOG_TRACE, "Testing group access");
                ret = ac_test_group_access (ia, gid, auxgids, auxcount,
                                            accesstest);
        }

        if (ret == 0) {
                gf_log (ACTRL, GF_LOG_TRACE, "Group has access");
                goto out;
        }

        if (ac_test_other (testwho)) {
                gf_log (ACTRL, GF_LOG_TRACE, "Testing other access");
                ret = ac_test_other_access (ia, accesstest);
        }

        if (ret == 0)
                gf_log (ACTRL, GF_LOG_TRACE, "Other has access");
out:
        if (ret == -1) {
                gf_log (ACTRL, GF_LOG_TRACE, "No access allowed");
                *operrno = EPERM;
        }

        return ret;
}


int
ac_loc_fill (loc_t *loc, inode_t *inode, inode_t *parent, char *path)
{
        int     ret = -EFAULT;

        if (!loc)
                return ret;

        if (inode) {
                loc->inode = inode_ref (inode);
                loc->ino = inode->ino;
        }

        if (parent)
                loc->parent = inode_ref (parent);

        loc->path = gf_strdup (path);
        if (!loc->path) {
                gf_log (ACTRL, GF_LOG_ERROR, "strdup failed");
                goto loc_wipe;
        }

        loc->name = strrchr (loc->path, '/');
        if (loc->name)
                loc->name++;
        else
                goto loc_wipe;

        ret = 0;
loc_wipe:
        if (ret < 0)
                loc_wipe (loc);

        return ret;
}


int
ac_inode_loc_fill (inode_t *inode, loc_t *loc)
{
        char            *resolvedpath = NULL;
        inode_t         *parent = NULL;
        int             ret = -EFAULT;

        if ((!inode) || (!loc))
                return ret;

        if ((inode) && (inode->ino == 1))
                goto ignore_parent;

        parent = inode_parent (inode, 0, NULL);
        if (!parent)
                goto err;

ignore_parent:
        ret = inode_path (inode, NULL, &resolvedpath);
        if (ret < 0)
                goto err;

        ret = ac_loc_fill (loc, inode, parent, resolvedpath);
        if (ret < 0)
                goto err;

err:
        if (parent)
                inode_unref (parent);

        if (resolvedpath)
                GF_FREE (resolvedpath);

        return ret;
}


int
ac_parent_loc_fill (loc_t *parentloc, loc_t *childloc)
{
        if ((!parentloc) || (!childloc))
                return -1;

        return ac_inode_loc_fill (childloc->parent, parentloc);
}


int32_t
ac_truncate_resume (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    off_t offset)
{
        STACK_WIND (frame, default_truncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate, loc, offset);
        return 0;
}


int32_t
ac_truncate_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;

        stub = __get_frame_stub (frame);
        if (op_ret == -1)
                goto out;

        op_ret = ac_test_access (buf, frame->root->uid, frame->root->gid,
                                 frame->root->groups, frame->root->ngrps,
                                 ACCTEST_WRITE, ACCTEST_ANY, &op_errno);
        if (op_ret == -1)
                goto out;

        call_resume (stub);
out:
        if (op_ret < 0) {
                STACK_UNWIND_STRICT (truncate, frame, -1, op_errno, NULL, NULL);
                if (stub)
                        call_stub_destroy (stub);
        }

        return 0;
}


int32_t
ac_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset)
{
        call_stub_t     *stub = NULL;
        int             ret = -EFAULT;

        if (__is_fuse_call (frame)) {
                ac_truncate_resume (frame, this, loc, offset);
                return 0;
        }
        stub = fop_truncate_stub (frame, ac_truncate_resume, loc, offset);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create call stub: "
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        frame->local = stub;
        STACK_WIND (frame, ac_truncate_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, loc);

        ret = 0;
out:
        if (ret < 0)
                STACK_UNWIND_STRICT (truncate, frame, -1, -ret, NULL, NULL);

        return 0;
}


int32_t
ac_access_resume (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask)
{
        STACK_WIND (frame, default_access_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->access, loc, mask);
        return 0;
}


int32_t
ac_access_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;
        int32_t         mask = 0;
        int             acctest = 0;

        stub = __get_frame_stub (frame);
        mask = stub->args.access.mask;

        /* If mask requests test for file existence then do not
         * return a failure with ENOENT, instead return a failed
         * access test.
         */
        if (op_ret == -1) {
                if (mask & F_OK)
                        op_errno = EACCES;
                else
                        op_errno = errno;

                goto out;
        }

        if (R_OK & mask)
                acctest |= ACCTEST_READ;
        else if (W_OK & mask)
                acctest |= ACCTEST_WRITE;
        else if (X_OK & mask)
                acctest |= ACCTEST_EXEC;
        else
                acctest = 0;

        op_ret = ac_test_access (buf, frame->root->uid, frame->root->gid,
                                 frame->root->groups, frame->root->ngrps,
                                 acctest, ACCTEST_ANY, &op_errno);
        if (op_ret == -1)
                goto out;

        call_resume (stub);
out:
        if (op_ret < 0) {
                STACK_UNWIND_STRICT (access, frame, -1, op_errno);
                if (stub)
                        call_stub_destroy (stub);
        }

        return 0;
}


int32_t
ac_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask)
{
        call_stub_t     *stub = NULL;
        int             ret = -EFAULT;

        if (__is_fuse_call (frame)) {
                ac_access_resume (frame, this, loc, mask);
                return 0;
        }
        stub = fop_access_stub (frame, ac_access_resume, loc, mask);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create call stub: "
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        frame->local = stub;
        STACK_WIND (frame, ac_access_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, loc);
        ret = 0;

out:
        if (ret < 0)
                STACK_UNWIND_STRICT (access, frame, -1, -ret);

        return 0;
}


int32_t
ac_readlink_resume (call_frame_t *frame, xlator_t *this, loc_t *loc,
                    size_t size)
{
        STACK_WIND (frame, default_readlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readlink, loc, size);
        return 0;
}


int32_t
ac_readlink_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;

        stub = __get_frame_stub (frame);
        if (op_ret == -1)
                goto out;

        op_ret = ac_test_access (buf, frame->root->uid, frame->root->gid,
                                 frame->root->groups, frame->root->ngrps,
                                 ACCTEST_READ, ACCTEST_ANY, &op_errno);
        if (op_ret == -1)
                goto out;

        call_resume (stub);
out:
        if (op_ret < 0) {
                STACK_UNWIND_STRICT (readlink, frame, -1, op_errno, NULL, NULL);
                if (stub)
                        call_stub_destroy (stub);
        }

        return 0;
}


int32_t
ac_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size)
{
        call_stub_t     *stub = NULL;
        int             ret = -EFAULT;

        if (__is_fuse_call (frame)) {
                ac_readlink_resume (frame, this, loc, size);
                return 0;
        }
        stub = fop_readlink_stub (frame, ac_readlink_resume, loc, size);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create call stub: "
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        frame->local = stub;
        STACK_WIND (frame, ac_readlink_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, loc);
        ret = 0;

out:
        if (ret < 0)
                STACK_UNWIND_STRICT (readlink, frame, -1, -ret, NULL, NULL);

        return 0;
}


int
ac_mknod_resume (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
                 dev_t rdev, dict_t *params)
{
        STACK_WIND (frame, default_mknod_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mknod, loc, mode, rdev, params);
        return 0;
}


int32_t
ac_mknod_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;

        stub = __get_frame_stub (frame);
        if (op_ret == -1)
                goto out;

        op_ret = ac_test_access (buf, frame->root->uid, frame->root->gid,
                                 frame->root->groups, frame->root->ngrps,
                                 ACCTEST_WRITE, ACCTEST_ANY, &op_errno);
        if (op_ret == -1) {
                op_errno = EACCES;
                goto out;
        }

        call_resume (stub);
out:
        if (op_ret < 0) {
                STACK_UNWIND_STRICT (mknod, frame, -1, op_errno, NULL, NULL,
                                     NULL, NULL);
                if (stub)
                        call_stub_destroy (stub);
        }

        return 0;
}


int
ac_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          dev_t rdev, dict_t *params)
{
        call_stub_t     *stub = NULL;
        int             ret = -EFAULT;
        loc_t           parentloc = {0, };

        if (__is_fuse_call (frame)) {
                ac_mknod_resume (frame, this, loc, mode, rdev, params);
                return 0;
        }
        stub = fop_mknod_stub (frame, ac_mknod_resume, loc, mode, rdev, params);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create call stub: "
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        frame->local = stub;
        ret = ac_parent_loc_fill (&parentloc, loc);
        if (ret < 0)
                goto out;

        STACK_WIND (frame, ac_mknod_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, &parentloc);
        loc_wipe (&parentloc);
        ret = 0;

out:
        if (ret < 0) {
                /* Erase any stored frame before unwinding. */
                stub = __get_frame_stub (frame);
                if (stub)
                        call_stub_destroy (stub);
                STACK_UNWIND_STRICT (mknod, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL);
        }

        return 0;
}


int
ac_mkdir_resume (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
                 dict_t *params)
{
        STACK_WIND (frame, default_mkdir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mkdir, loc, mode, params);
        return 0;
}


int32_t
ac_mkdir_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;

        stub = __get_frame_stub (frame);
        if (op_ret == -1)
                goto out;

        op_ret = ac_test_access (buf, frame->root->uid, frame->root->gid,
                                 frame->root->groups, frame->root->ngrps,
                                 ACCTEST_WRITE, ACCTEST_ANY, &op_errno);
        if (op_ret == -1) {
                /* On a failed  write test on parent dir, we need to return
                 * EACCES, not EPERM that is returned by default by
                 * ac_test_access.
                 */
                op_errno = EACCES;
                goto out;
        }

        call_resume (stub);
out:
        if (op_ret < 0) {
                STACK_UNWIND_STRICT (mkdir, frame, -1, op_errno, NULL, NULL,
                                     NULL, NULL);
                if (stub)
                        call_stub_destroy (stub);
        }

        return 0;
}


int
ac_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          dict_t *params)
{
        call_stub_t     *stub = NULL;
        int             ret = -EFAULT;
        loc_t           parentloc = {0, };

        if (__is_fuse_call (frame)) {
                ac_mkdir_resume (frame, this, loc, mode, params);
                return 0;
        }
        stub = fop_mkdir_stub (frame, ac_mkdir_resume, loc, mode, params);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create call stub: "
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        frame->local = stub;
        ret = ac_parent_loc_fill (&parentloc, loc);
        if (ret < 0)
                goto out;

        STACK_WIND (frame, ac_mkdir_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, &parentloc);
        loc_wipe (&parentloc);
        ret = 0;

out:
        if (ret < 0) {
                /* Erase the stored stub before unwinding. */
                stub = __get_frame_stub (frame);
                if (stub)
                        call_stub_destroy (stub);
                STACK_UNWIND_STRICT (mkdir, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL);
        }

        return 0;
}


int32_t
ac_unlink_resume (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        STACK_WIND (frame, default_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink, loc);
        return 0;
}


int32_t
ac_unlink_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;

        stub = __get_frame_stub (frame);
        if (op_ret == -1)
                goto out;

        op_ret = ac_test_access (buf, frame->root->uid, frame->root->gid,
                                 frame->root->groups, frame->root->ngrps,
                                 ACCTEST_WRITE, ACCTEST_ANY, &op_errno);
        if (op_ret == -1) {
                op_errno = EACCES;
                goto out;
        }

        call_resume (stub);
out:
        if (op_ret < 0) {
                STACK_UNWIND_STRICT (unlink, frame, -1, op_errno, NULL, NULL);
                if (stub)
                        call_stub_destroy (stub);
        }

        return 0;
}


int32_t
ac_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        call_stub_t     *stub = NULL;
        int             ret = -EFAULT;
        loc_t           parentloc = {0, };

        if (__is_fuse_call (frame)) {
                ac_unlink_resume (frame, this, loc);
                return 0;
        }
        stub = fop_unlink_stub (frame, ac_unlink_resume, loc);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create call stub: "
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        frame->local = stub;
        ret = ac_parent_loc_fill (&parentloc, loc);
        if (ret < 0)
                goto out;

        STACK_WIND (frame, ac_unlink_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, &parentloc);
        loc_wipe (&parentloc);
        ret = 0;

out:
        if (ret < 0) {
                /* Erase the stored stub before unwinding. */
                stub = __get_frame_stub (frame);
                if (stub)
                        call_stub_destroy (stub);
                STACK_UNWIND_STRICT (unlink, frame, -1, -ret, NULL, NULL);
        }

        return 0;
}


int
ac_rmdir_resume (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags)
{
        STACK_WIND (frame, default_rmdir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rmdir, loc, flags);
        return 0;
}


int32_t
ac_rmdir_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;

        stub = __get_frame_stub (frame);
        if (op_ret == -1)
                goto out;

        op_ret = ac_test_access (buf, frame->root->uid, frame->root->gid,
                                 frame->root->groups, frame->root->ngrps,
                                 ACCTEST_WRITE, ACCTEST_ANY, &op_errno);
        if (op_ret == -1) {
                op_errno = EACCES;
                goto out;
        }

        call_resume (stub);
out:
        if (op_ret < 0) {
                STACK_UNWIND_STRICT (rmdir, frame, -1, op_errno, NULL, NULL);
                if (stub)
                        call_stub_destroy (stub);
        }

        return 0;
}


int
ac_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int flags)
{
        call_stub_t     *stub = NULL;
        int             ret = -EFAULT;
        loc_t           parentloc = {0, };

        if (__is_fuse_call (frame)) {
                ac_rmdir_resume (frame, this, loc, flags);
                return 0;
        }
        stub = fop_rmdir_stub (frame, ac_rmdir_resume, loc, flags);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create call stub: "
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        frame->local = stub;
        ret = ac_parent_loc_fill (&parentloc, loc);
        if (ret < 0)
                goto out;

        STACK_WIND (frame, ac_rmdir_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, &parentloc);
        loc_wipe (&parentloc);
        ret = 0;

out:
        if (ret < 0) {
                /* Erase the stored stub before unwinding. */
                stub = __get_frame_stub (frame);
                if (stub)
                        call_stub_destroy (stub);
                STACK_UNWIND_STRICT (rmdir, frame, -1, -ret, NULL, NULL);
        }

        return 0;
}


int32_t
ac_symlink_resume (call_frame_t *frame, xlator_t *this, const char *linkname,
                   loc_t *loc, dict_t *params)
{
        STACK_WIND (frame, default_symlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->symlink, linkname, loc, params);
        return 0;
}


int32_t
ac_symlink_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;

        stub = __get_frame_stub (frame);
        if (op_ret == -1)
                goto out;

        op_ret = ac_test_access (buf, frame->root->uid, frame->root->gid,
                                 frame->root->groups, frame->root->ngrps,
                                 ACCTEST_WRITE, ACCTEST_ANY, &op_errno);
        if (op_ret == -1) {
                op_errno = EACCES;
                goto out;
        }

        call_resume (stub);
out:
        if (op_ret < 0) {
                STACK_UNWIND_STRICT (symlink, frame, -1, op_errno, NULL, NULL,
                                     NULL, NULL);
                if (stub)
                        call_stub_destroy (stub);
        }

        return 0;
}


int
ac_symlink (call_frame_t *frame, xlator_t *this, const char *linkname,
            loc_t *loc, dict_t *params)
{
        call_stub_t     *stub = NULL;
        int             ret = -EFAULT;
        loc_t           parentloc = {0, };

        if (__is_fuse_call (frame)) {
                ac_symlink_resume (frame, this, linkname, loc, params);
                return 0;
        }
        stub = fop_symlink_stub (frame, ac_symlink_resume, linkname, loc,
                                 params);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create call stub: "
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        frame->local = stub;
        ret = ac_parent_loc_fill (&parentloc, loc);
        if (ret < 0)
                goto out;

        STACK_WIND (frame, ac_symlink_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, &parentloc);
        loc_wipe (&parentloc);
        ret = 0;

out:
        if (ret < 0) {
                /* Erase the stored stub before unwinding. */
                stub = __get_frame_stub (frame);
                if (stub)
                        call_stub_destroy (stub);
                STACK_UNWIND_STRICT (symlink, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL);
        }

        return 0;
}


int32_t
ac_rename_resume (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                  loc_t *newloc)
{
        STACK_WIND (frame, default_rename_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename, oldloc, newloc);
        return 0;
}


int32_t
ac_rename_dst_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;

        stub = __get_frame_stub (frame);
        if (op_ret == -1)
                goto out;

        op_ret = ac_test_access (buf, frame->root->uid,
                                 frame->root->gid, frame->root->groups,
                                 frame->root->ngrps, ACCTEST_WRITE,
                                 ACCTEST_ANY, &op_errno);
        if (op_ret == -1) {
                op_errno = EACCES;
               goto out;
        }

        call_resume (stub);
out:
        if (op_ret < 0) {
                STACK_UNWIND_STRICT (rename, frame, -1, op_errno, NULL, NULL,
                                     NULL, NULL, NULL);
                if (stub)
                        call_stub_destroy (stub);
        }

        return 0;
}


int32_t
ac_rename_src_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;
        loc_t           parentloc = {0, };

        stub = frame->local;
        if (op_ret == -1)
                goto out;

        op_ret = ac_test_access (buf, frame->root->uid,
                                 frame->root->gid, frame->root->groups,
                                 frame->root->ngrps, ACCTEST_WRITE,
                                 ACCTEST_ANY, &op_errno);
        if (op_ret == -1) {
                op_errno = EACCES;
                goto out;
        }

        op_ret = ac_parent_loc_fill (&parentloc, &stub->args.rename.new);
        if (op_ret < 0) {
                op_errno = -EFAULT;
                goto out;
        }

        STACK_WIND (frame, ac_rename_dst_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, &parentloc);
        loc_wipe (&parentloc);

out:
        if (op_ret < 0) {
                /* Erase the stored stub before unwinding. */
                stub = __get_frame_stub (frame);
                if (stub)
                        call_stub_destroy (stub);
                STACK_UNWIND_STRICT (rename, frame, -1, op_errno, NULL, NULL,
                                     NULL, NULL, NULL);
        }

        return 0;
}


int32_t
ac_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc)
{
        call_stub_t     *stub = NULL;
        int             ret = -EFAULT;
        loc_t           parentloc = {0, };

        if (__is_fuse_call (frame)) {
                ac_rename_resume (frame, this, oldloc, newloc);
                return 0;
        }
        stub = fop_rename_stub (frame, ac_rename_resume, oldloc, newloc);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create call stub: "
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        frame->local = stub;
        ret = ac_parent_loc_fill (&parentloc, oldloc);
        if (ret < 0)
                goto out;

        STACK_WIND (frame, ac_rename_src_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, &parentloc);
        loc_wipe (&parentloc);
        ret = 0;

out:
        if (ret < 0) {
                /* Erase the stored stub before unwinding. */
                stub = __get_frame_stub (frame);
                if (stub)
                        call_stub_destroy (stub);
                STACK_UNWIND_STRICT (rename, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL, NULL);
        }

        return 0;
}


int32_t
ac_link_resume (call_frame_t *frame, xlator_t *this, loc_t *oldloc,
                loc_t *newloc)
{
        STACK_WIND (frame, default_link_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->link, oldloc, newloc);
        return 0;
}


int32_t
ac_link_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;

        stub = __get_frame_stub (frame);
        if (op_ret == -1)
                goto out;

        op_ret = ac_test_access (buf, frame->root->uid, frame->root->gid,
                                 frame->root->groups, frame->root->ngrps,
                                 ACCTEST_WRITE, ACCTEST_ANY, &op_errno);
        if (op_ret == -1) {
                /* By default ac_test_access sets the op_errno to EPERM
                 * but in the case of link, we need to return EACCES to meet
                 * posix requirements when a write permission is not available
                 * for the new directory.
                 */
                op_errno = EACCES;
                goto out;
        }

        call_resume (stub);
out:
        if (op_ret < 0) {
                STACK_UNWIND_STRICT (link, frame, -1, op_errno, NULL, NULL,
                                     NULL, NULL);
                if (stub)
                        call_stub_destroy (stub);
        }

        return 0;
}


int32_t
ac_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc)
{
        call_stub_t     *stub = NULL;
        int             ret = -EFAULT;
        loc_t           parentloc = {0, };

        if (__is_fuse_call (frame)) {
                ac_link_resume (frame, this, oldloc, newloc);
                return 0;
        }
        stub = fop_link_stub (frame, ac_link_resume, oldloc, newloc);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create call stub: "
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        frame->local = stub;
        ret = ac_parent_loc_fill (&parentloc, newloc);
        if (ret < 0)
                goto out;

        STACK_WIND (frame, ac_link_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, &parentloc);
        loc_wipe (&parentloc);
        ret = 0;

out:
        if (ret < 0) {
                /* Erase the stored stub before unwinding. */
                stub = __get_frame_stub (frame);
                if (stub)
                        call_stub_destroy (stub);
                STACK_UNWIND_STRICT (link, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL);
        }

        return 0;
}


int32_t
ac_create_resume (call_frame_t *frame, xlator_t *this, loc_t *loc,
                  int32_t flags, mode_t mode, fd_t *fd, dict_t *params)
{
        STACK_WIND (frame, default_create_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create, loc, flags, mode,
                    fd, params);
        return 0;
}


int32_t
ac_create_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;

        stub = __get_frame_stub (frame);
        if (op_ret == -1)
                goto out;

        op_ret = ac_test_access (buf, frame->root->uid, frame->root->gid,
                                 frame->root->groups, frame->root->ngrps,
                                 ACCTEST_WRITE, ACCTEST_ANY, &op_errno);
         if (op_ret == -1) {
                op_errno = EACCES;
                goto out;
         }

        call_resume (stub);
out:
        if (op_ret < 0) {
                STACK_UNWIND_STRICT (create, frame, -1, op_errno, NULL, NULL,
                                     NULL, NULL, NULL);
                if (stub)
                        call_stub_destroy (stub);
        }

        return 0;
}


int32_t
ac_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           mode_t mode, fd_t *fd, dict_t *params)
{
        call_stub_t     *stub = NULL;
        int             ret = -EFAULT;
        loc_t           parentloc = {0, };

        if (__is_fuse_call (frame)) {
                ac_create_resume (frame, this, loc, flags, mode, fd, params);
                return 0;
        }
        stub = fop_create_stub (frame, ac_create_resume, loc, flags, mode,
                                fd, params);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create call stub: "
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        frame->local = stub;
        ret = ac_parent_loc_fill (&parentloc, loc);
        if (ret < 0)
                goto out;

        STACK_WIND (frame, ac_create_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, &parentloc);
        loc_wipe (&parentloc);
        ret = 0;

out:
        if (ret < 0) {
                /* Erase the stored stub before unwinding. */
                stub = __get_frame_stub (frame);
                if (stub)
                        call_stub_destroy (stub);
                STACK_UNWIND_STRICT (create, frame, -1, -ret, NULL, NULL, NULL,
                                     NULL, NULL);
        }

        return 0;
}


int32_t
ac_open_resume (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
                fd_t *fd, int32_t wbflags)
{
        STACK_WIND (frame, default_open_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open, loc, flags, fd, wbflags);
        return 0;
}


int32_t
ac_open_create_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;

        stub = __get_frame_stub (frame);
        if (op_ret == -1)
                goto out;

        op_ret = ac_test_access (buf, frame->root->uid, frame->root->gid,
                                 frame->root->groups, frame->root->ngrps,
                                 ACCTEST_WRITE, ACCTEST_ANY, &op_errno);
         if (op_ret == -1) {
                op_errno = EACCES;
                goto out;
         }

        call_resume (stub);
out:
        if (op_ret < 0) {
                STACK_UNWIND_STRICT (open, frame, -1, op_errno, NULL);
                if (stub)
                        call_stub_destroy (stub);
        }

        return 0;
}


int
ac_open_create (call_stub_t *stub)
{
        int             ret = -EFAULT;
        loc_t           parentloc = {0, };
        xlator_t        *this = NULL;

        if (!stub)
                return ret;

        ret = ac_parent_loc_fill (&parentloc, &stub->args.open.loc);
        if (ret < 0)
                goto out;

        this = stub->frame->this;
        STACK_WIND (stub->frame, ac_open_create_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, &parentloc);
        loc_wipe (&parentloc);
        ret = 0;

out:
        return ret;
}


int32_t
ac_open_only_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;
        int             acctest = 0;
        int32_t         flags = 0;

        stub = __get_frame_stub (frame);
        if (op_ret == -1)
                goto out;

        flags = stub->args.open.flags;
        /* The permissions we test for depend on how the open needs to be
         * performed. */
        if ((flags & O_ACCMODE) == O_RDONLY)
                acctest = ACCTEST_READ;
        else if (((flags & O_ACCMODE) == O_RDWR) ||
                 ((flags & O_ACCMODE) == O_WRONLY))
                acctest = ACCTEST_WRITE;

        op_ret = ac_test_access (buf, frame->root->uid, frame->root->gid,
                                 frame->root->groups, frame->root->ngrps,
                                 acctest, ACCTEST_ANY, &op_errno);
        if (op_ret == -1)
                goto out;

        call_resume (stub);
out:
        if (op_ret < 0) {
                STACK_UNWIND_STRICT (open, frame, -1, op_errno, NULL);
                if (stub)
                        call_stub_destroy (stub);
        }

        return 0;
}


int
ac_open_only (call_stub_t *stub)
{
        int             ret = -EFAULT;
        xlator_t        *this = NULL;

        if (!stub)
                return ret;

        this = stub->frame->this;
        STACK_WIND (stub->frame, ac_open_only_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, &stub->args.open.loc);
        return 0;
}

int32_t
ac_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, int32_t wbflags)
{
        call_stub_t     *stub = NULL;
        int             ret = -EFAULT;

        if (__is_fuse_call (frame)) {
                ret = ac_open_resume (frame, this, loc, flags, fd, wbflags);
                return 0;
        }

        stub = fop_open_stub (frame, ac_open_resume, loc, flags, fd, wbflags);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create call stub: "
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        frame->local = stub;
        /* If we are not supposed to create the file then there is no need to
         * check the parent dir permissions. */
        if (flags & O_CREAT)
                ret = ac_open_create (stub);
        else
                ret = ac_open_only (stub);

out:
        if (ret < 0) {
                /* Erase the stored stub before unwinding. */
                stub = __get_frame_stub (frame);
                if (stub)
                        call_stub_destroy (stub);
                STACK_UNWIND_STRICT (open, frame, -1, -ret, NULL);
        }

        return 0;
}


int32_t
ac_readv_resume (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                 off_t offset)
{
        STACK_WIND (frame, default_readv_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readv, fd, size, offset);
        return 0;
}


int32_t
ac_readv_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;

        stub = __get_frame_stub (frame);
        if (op_ret == -1)
                goto out;

        op_ret = ac_test_access (buf, frame->root->uid, frame->root->gid,
                                 frame->root->groups, frame->root->ngrps,
                                 ACCTEST_READ, ACCTEST_ANY, &op_errno);
        if (op_ret == -1) {
                op_errno = EACCES;
                goto out;
        }

        call_resume (stub);
out:
        if (op_ret < 0) {
                STACK_UNWIND_STRICT (readv, frame, -1, op_errno, NULL, 0, NULL,
                                     NULL);
                if (stub)
                        call_stub_destroy (stub);
        }

        return 0;
}


int32_t
ac_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset)
{
        call_stub_t     *stub = NULL;
        int             ret = -EFAULT;

        if (__is_fuse_call (frame)) {
                ret = ac_readv_resume (frame, this, fd, size, offset);
                return 0;
        }

        stub = fop_readv_stub (frame, ac_readv_resume, fd, size, offset);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create call stub: "
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        frame->local = stub;
        STACK_WIND (frame, ac_readv_fstat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fstat, fd);
        ret = 0;

out:
        if (ret < 0)
                STACK_UNWIND_STRICT (readv, frame, -1, -ret, NULL, 0, NULL,
                                     NULL);

        return 0;
}


int32_t
ac_writev_resume (call_frame_t *frame, xlator_t *this, fd_t *fd,
                  struct iovec *vector, int32_t count, off_t offset,
                  struct iobref *iobref)
{
        STACK_WIND (frame, default_writev_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->writev, fd, vector, count, offset,
                    iobref);
        return 0;
}


int32_t
ac_writev_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;

        stub = __get_frame_stub (frame);
        if (op_ret == -1)
                goto out;

        op_ret = ac_test_access (buf, frame->root->uid, frame->root->gid,
                                 frame->root->groups, frame->root->ngrps,
                                 ACCTEST_WRITE, ACCTEST_ANY, &op_errno);
        if (op_ret == -1) {
                op_errno = EACCES;
                goto out;
        }

        call_resume (stub);
out:
        if (op_ret < 0) {
                STACK_UNWIND_STRICT (writev, frame, -1, op_errno, NULL, NULL);
                if (stub)
                        call_stub_destroy (stub);
        }

        return 0;
}


int32_t
ac_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t offset, struct iobref *iobref)
{
        call_stub_t     *stub = NULL;
        int             ret = -EFAULT;

        if (__is_fuse_call (frame)) {
                ret = ac_writev_resume (frame, this, fd, vector, count,
                                        offset, iobref);
                return 0;
        }

        stub = fop_writev_stub (frame, ac_writev_resume, fd, vector, count,
                                offset, iobref);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create call stub: "
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        frame->local = stub;
        STACK_WIND (frame, ac_writev_fstat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fstat, fd);
        ret = 0;

out:
        if (ret < 0)
                STACK_UNWIND_STRICT (writev, frame, -1, -ret, NULL, NULL);

        return 0;
}


int32_t
ac_opendir_resume (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
        STACK_WIND (frame, default_opendir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->opendir, loc, fd);
        return 0;
}


int32_t
ac_opendir_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;

        stub = __get_frame_stub (frame);
        if (op_ret == -1)
                goto out;

        op_ret = ac_test_access (buf, frame->root->uid, frame->root->gid,
                                 frame->root->groups, frame->root->ngrps,
                                 ACCTEST_READ, ACCTEST_ANY, &op_errno);
        if (op_ret == -1)
                goto out;

        call_resume (stub);
out:
        if (op_ret < 0) {
                STACK_UNWIND_STRICT (opendir, frame, -1, op_errno, NULL);
                if (stub)
                        call_stub_destroy (stub);
        }

        return 0;
}

int32_t
ac_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
        call_stub_t     *stub = NULL;
        int             ret = -EFAULT;

        if (__is_fuse_call (frame)) {
                ret = ac_opendir_resume (frame, this, loc, fd);
                return 0;
        }

        stub = fop_opendir_stub (frame, ac_opendir_resume, loc, fd);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create call stub: "
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        frame->local = stub;
        STACK_WIND (frame, ac_opendir_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, loc);
        ret = 0;

out:
        if (ret < 0)
                STACK_UNWIND_STRICT (opendir, frame, -1, -ret, NULL);

        return 0;
}


int32_t
ac_setattr_resume (call_frame_t *frame, xlator_t *this, loc_t *loc,
                   struct iatt *buf, int32_t valid)
{
        STACK_WIND (frame, default_setattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setattr, loc, buf, valid);
        return 0;
}


int32_t
ac_setattr_stat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;
        int32_t         valid = 0;
        struct iatt     *setbuf = NULL;

        stub = __get_frame_stub (frame);
        if (op_ret == -1)
                goto out;

        op_ret = ac_test_access (buf, frame->root->uid, frame->root->gid,
                                 frame->root->groups, frame->root->ngrps,
                                 ACCTEST_DONTCARE, ACCTEST_OWNER,
                                 &op_errno);
        if (op_ret == -1)
                goto out;

        valid = stub->args.setattr.valid;
        setbuf = &stub->args.setattr.stbuf;
        if (gf_attr_uid_set (valid) || gf_attr_gid_set (valid)) {
                /* chown returns EPERM if the operation would change the
                 * ownership, but the effective user ID is not the
                 * super-user and the process is not an owner of the file.
                 * Ref: posix-testsuite/chown/07.t
                 */
                if ((frame->root->uid != 0) && (gf_attr_uid_set (valid))) {
                        if (buf->ia_uid != setbuf->ia_uid) {
                                op_ret = -1;
                                op_errno = EPERM;
                                goto out;
                        }
                }

                /* non-super-user can modify file group if he is owner of a
                 * file and gid he is setting is in his groups list.
                 * Ref: posix-testsuite/chown/00.t
                 */
                if ((frame->root->uid != 0) && (gf_attr_gid_set (valid))) {
                        if (frame->root->uid != buf->ia_uid) {
                                op_ret = -1;
                                op_errno = EPERM;
                                goto out;
                        }

                        op_ret = ac_test_access (setbuf, 0, frame->root->gid,
                                                 frame->root->groups,
                                                 frame->root->ngrps,
                                                 ACCTEST_DONTCARE,
                                                 ACCTEST_GROUP, &op_errno);
                        if (op_ret == -1)
                                goto out;
                }
        }

        call_resume (stub);
out:
        if (op_ret < 0) {
                STACK_UNWIND_STRICT (setattr, frame, -1, op_errno, NULL, NULL);
                if (stub)
                        call_stub_destroy (stub);
        }

        return 0;
}

int32_t
ac_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc, struct iatt *buf,
            int32_t valid)
{
        call_stub_t     *stub = NULL;
        int             ret = -EFAULT;

        if (__is_fuse_call (frame)) {
                ret = ac_setattr_resume (frame, this, loc, buf, valid);
                return 0;
        }

        stub = fop_setattr_stub (frame, ac_setattr_resume, loc, buf, valid);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create call stub: "
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        frame->local = stub;
        STACK_WIND (frame, ac_setattr_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, loc);
        ret = 0;

out:
        if (ret < 0)
                STACK_UNWIND_STRICT (setattr, frame, -1, -ret, NULL, NULL);

        return 0;
}


int32_t
ac_fsetattr_resume (call_frame_t *frame, xlator_t *this, fd_t *fd,
                    struct iatt *buf, int32_t valid)
{
        STACK_WIND (frame, default_fsetattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsetattr, fd, buf, valid);
        return 0;
}


int32_t
ac_fsetattr_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        call_stub_t     *stub = NULL;
        int32_t         valid = 0;
        struct iatt     *setbuf = NULL;

        stub = __get_frame_stub (frame);
        if (op_ret == -1)
                goto out;

        op_ret = ac_test_access (buf, frame->root->uid, frame->root->gid,
                                 frame->root->groups, frame->root->ngrps,
                                 ACCTEST_DONTCARE, ACCTEST_OWNER,
                                 &op_errno);
        if (op_ret == -1)
                goto out;

        valid = stub->args.fsetattr.valid;
        setbuf = &stub->args.fsetattr.stbuf;
        if (gf_attr_uid_set (valid) && gf_attr_gid_set (valid)) {
                /* chown returns EPERM if the operation would change the
                 * ownership, but the effective user ID is not the
                 * super-user and the process is not an owner of the file.
                 * Ref: posix-testsuite/chown/07.t
                 */
                if ((frame->root->uid != 0) && (gf_attr_uid_set (valid))) {
                        if (buf->ia_uid != setbuf->ia_uid) {
                                op_ret = -1;
                                op_errno = EPERM;
                                goto out;
                        }
                }

                /* non-super-user can modify file group if he is owner of a
                 * file and gid he is setting is in his groups list.
                 * Ref: posix-testsuite/chown/00.t
                 */
                if ((frame->root->uid != 0) && (gf_attr_gid_set (valid))) {
                        if (frame->root->uid != buf->ia_uid) {
                                op_ret = -1;
                                op_errno = EPERM;
                                goto out;
                        }

                        op_ret = ac_test_access (buf, 0, frame->root->gid,
                                                 frame->root->groups,
                                                 frame->root->ngrps,
                                                 ACCTEST_DONTCARE,
                                                 ACCTEST_GROUP, &op_errno);
                        if (op_ret == -1)
                                goto out;
                }
        }

        call_resume (stub);
out:
        if (op_ret < 0) {
                STACK_UNWIND_STRICT (fsetattr, frame, -1, op_errno, NULL, NULL);
                if (stub)
                        call_stub_destroy (stub);
        }

        return 0;
}


int32_t
ac_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iatt *buf,
             int32_t valid)
{
        call_stub_t     *stub = NULL;
        int             ret = -EFAULT;

        if (__is_fuse_call (frame)) {
                ret = ac_fsetattr_resume (frame, this, fd, buf, valid);
                return 0;
        }

        stub = fop_fsetattr_stub (frame, ac_fsetattr_resume, fd, buf, valid);
        if (!stub) {
                gf_log (this->name, GF_LOG_ERROR, "cannot create call stub: "
                        "(out of memory)");
                ret = -ENOMEM;
                goto out;
        }

        frame->local = stub;
        STACK_WIND (frame, ac_fsetattr_fstat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fstat, fd);
        ret = 0;

out:
        if (ret < 0)
                STACK_UNWIND_STRICT (fsetattr, frame, -1, -ret, NULL, NULL);

        return 0;
}


struct xlator_fops fops = {
        .truncate       = ac_truncate,
        .access         = ac_access,
        .readlink       = ac_readlink,
        .mknod          = ac_mknod,
        .mkdir          = ac_mkdir,
        .unlink         = ac_unlink,
        .rmdir          = ac_rmdir,
        .symlink        = ac_symlink,
        .rename         = ac_rename,
        .link           = ac_link,
        .create         = ac_create,
        .open           = ac_open,
/*
 * Allow Writes and Reads to proceed without permission checks because:
 * a. We expect that when the fds are opened, thats when the perm checks happen
 * depending on the read/write mode used.
 *
 * b. In case of nfs clients, we expect the nfs clients to perform the checks
 * based on getattr/access nfs requests.
 *
 * Keep these functions around in case we ever run into a nfs client that
 * depends on nfs server to perform these checks. Till then, just remove the
 * references from here instead.
        .readv          = ac_readv,
        .writev         = ac_writev,
*/
        .opendir        = ac_opendir,
        .setattr        = ac_setattr,
        .fsetattr       = ac_fsetattr,
};

int
init (xlator_t *this)
{
        int ret = -1;

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "FATAL: access-control not configured with "
                        "exactly one  child");
                goto out;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_WARNING,
                        "dangling volume. check volfile ");
        }

        ret = 0;
out:
        return ret;
}

void
fini (xlator_t *this)
{
        return;
}

struct xlator_cbks cbks = {
};
