/*
 * Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
 * This file is part of GlusterFS.
 *
 * This file is licensed to you under your choice of the GNU Lesser
 * General Public License, version 3 or any later version (LGPLv3 or
 * later), or the GNU General Public License, version 2 (GPLv2), in all
 * cases as published by the Free Software Foundation.
 *
 * xlators/features/namespace:
 *      This translator tags each request with a namespace hash,
 *      which then can be used in later translators to track and
 *      throttle fops per namespace.
 */

#include <sys/types.h>

#include "defaults.h"
#include "glusterfs.h"
#include "hashfn.h"
#include "logging.h"
#include "namespace.h"

/* Return codes for common path parsing functions. */
enum _path_parse_result {
        PATH_PARSE_RESULT_NO_PATH = 0,
        PATH_PARSE_RESULT_FOUND   = 1,
        PATH_PARSE_RESULT_IS_GFID = 2,
};

typedef enum _path_parse_result path_parse_result_t;

/* Clean up an ns_local struct. Wipe a loc (its inode is ref'd, so we're good.)
 */
static inline void
ns_local_cleanup (ns_local_t *local)
{
        if (!local) {
                return;
        }

        loc_wipe (&local->loc);
        GF_FREE (local);
}

/* Create a new ns_local. We ref the inode, fake a new loc struct, and stash
 * the stub given to us. */
static inline ns_local_t *
ns_local_new (call_stub_t *stub, inode_t *inode)
{
        ns_local_t *local = NULL;
        loc_t       loc   = {0, };

        if (!stub || !inode) {
                goto out;
        }

        local = GF_CALLOC (1, sizeof (ns_local_t), 0);
        if (local == NULL) {
                goto out;
        }

        /* Set up a fake loc_t struct to give to the getxattr call. */
        gf_uuid_copy (loc.gfid, inode->gfid);
        loc.inode = inode_ref (inode);

        /* If for some reason inode_ref() fails, then just give up. */
        if (!loc.inode) {
                GF_FREE (local);
                goto out;
        }

        local->stub = stub;
        local->loc = loc;

out:
        return local;
}

/* Try parsing a path string. If the path string is a GFID, then return
 * with PATH_PARSE_RESULT_IS_GFID. If we have no namespace (i.e. '/') then
 * return PATH_PARSE_RESULT_NO_PATH and set the hash to 1. Otherwise, hash the
 * namespace and store it in the info struct. */
static path_parse_result_t
parse_path (ns_info_t *info, const char *path)
{
        int         len      = 0;
        const char *ns_begin = path;
        const char *ns_end   = NULL;

        if (!path || strlen (path) == 0) {
                return PATH_PARSE_RESULT_NO_PATH;
        }

        if (path[0] == '<') {
                return PATH_PARSE_RESULT_IS_GFID;
        }

        /* Right now we only want the top-level directory, so
         * skip the initial '/' and read until the next '/'. */
        while (*ns_begin == '/') {
                ns_begin++;
        }

        /* ns_end will point to the next '/' or NULL if there is no delimiting
         * '/' (i.e. "/directory" or the top level "/") */
        ns_end = strchr (ns_begin, '/');
        len = ns_end ? (ns_end - ns_begin) : strlen (ns_begin);

        if (len != 0) {
                info->hash = SuperFastHash (ns_begin, len);
        } else {
                /* If our substring is empty, then we can hash '/' instead.
                 * '/' is used in the namespace config for the top-level
                 * namespace. */
                info->hash = SuperFastHash ("/", 1);
        }

        info->found = _gf_true;
        return PATH_PARSE_RESULT_FOUND;
}

/* Cache namespace info stored in the stack (info) into the inode. */
static int
ns_inode_ctx_put (inode_t *inode, xlator_t *this, ns_info_t *info)
{
        ns_info_t *cached_ns_info = NULL;
        uint64_t   ns_as_64       = 0;
        int        ret            = -1;

        if (!inode || !this) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Need a valid inode and xlator to cache ns_info.");
                ret = -1;
                goto out;
        }

        cached_ns_info = GF_CALLOC (1, sizeof (ns_info_t), 0);

        /* If we've run out of memory, then return ENOMEM. */
        if (cached_ns_info == NULL) {
                gf_log (this->name, GF_LOG_WARNING,
                        "No memory to cache ns_info.");
                ret = -(ENOMEM);
                goto out;
        }

        *cached_ns_info = *info;
        ns_as_64 = (uint64_t)cached_ns_info;

        ret = inode_ctx_put (inode, this, ns_as_64);

        if (ret) {
                goto out;
        }

        ret = 0;
out:
        if (ret && cached_ns_info) {
                GF_FREE (cached_ns_info);
        }

        return ret;
}

/* Retrieve namespace info cached in the inode into the stack for use in later
 * translators. */
static int
ns_inode_ctx_get (inode_t *inode, xlator_t *this, ns_info_t *info)
{
        ns_info_t *cached_ns_info = NULL;
        uint64_t   ns_as_64       = 0;
        int        ret            = -1;

        if (!inode) {
                ret = -ENOENT;
                goto out;
        }

        ret = inode_ctx_get (inode, this, &ns_as_64);

        if (!ret) {
                cached_ns_info = (ns_info_t *)ns_as_64;
                *info = *cached_ns_info;
        }

out:
        return ret;
}

/* This callback is the top of the unwind path of our attempt to get the path
 * manually from the posix translator. We'll try to parse the path returned
 * if it exists, then cache the hash if possible. Then just return to the
 * default stub that we provide in the local, since there's nothing else to do
 * once we've gotten the namespace hash. */
int32_t
get_path_resume_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *dict,
                     dict_t *xdata)
{
        path_parse_result_t  ret          = PATH_PARSE_RESULT_NO_PATH;
        call_frame_t        *resume_frame = NULL;
        ns_local_t          *local        = NULL;
        call_stub_t         *stub         = NULL;
        ns_info_t           *info         = NULL;
        char                *path         = NULL;

        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        local = frame->local;

        GF_VALIDATE_OR_GOTO (this->name, local, out);
        stub = local->stub;

        GF_VALIDATE_OR_GOTO (this->name, stub, out);
        /* Get the ns_info from the frame that we will eventually resume,
         * not the frame that we're going to destroy (frame). */
        resume_frame = stub->frame;

        GF_VALIDATE_OR_GOTO (this->name, resume_frame, out);
        GF_VALIDATE_OR_GOTO (this->name, resume_frame->root, out);
        info = &resume_frame->root->ns_info;

        GF_VALIDATE_OR_GOTO (this->name, dict, out);

        /* If we get a value back for the GET_ANCESTRY_PATH_KEY, then we
         * try to access it and parse it like a path. */
        if (!op_ret && !dict_get_str (dict, GET_ANCESTRY_PATH_KEY, &path)) {
                gf_log (this->name, GF_LOG_DEBUG, "G>P %s retrieved path %s",
                        uuid_utoa (local->loc.gfid), path);
                /* Now let's parse a path, finally. */
                ret = parse_path (info, path);
        }

        if (ret == PATH_PARSE_RESULT_FOUND) {
                /* If we finally found namespace, then stash it. */
                ns_inode_ctx_put (local->loc.inode, this, info);

                gf_log (this->name, GF_LOG_DEBUG,
                        "G>P %s %10u namespace found %s",
                        uuid_utoa (local->loc.inode->gfid), info->hash, path);
        } else if (ret == PATH_PARSE_RESULT_NO_PATH) {
                gf_log (this->name, GF_LOG_WARNING, "G>P %s has no path",
                        uuid_utoa (local->loc.inode->gfid));
        } else if (ret == PATH_PARSE_RESULT_IS_GFID) {
                gf_log (this->name, GF_LOG_WARNING,
                        "G>P %s winding failed, still have gfid",
                        uuid_utoa (local->loc.inode->gfid));
        }

out:
        /* Make sure to clean up local finally. */

        if (frame) {
                frame->local = NULL;
                STACK_DESTROY (frame->root);
        }

        if (local) {
                ns_local_cleanup (local);
        }

        if (stub) {
                call_resume (stub);
        }

        return 0;
}

/* This function tries first to set a namespace based on the information that
 * it can retrieve from an `loc_t`. This includes first looking for a cached
 * namespace in the inode, then trying to parse the path string in the `loc_t`
 * struct. If this fails, then it will try to call inode_path. */
static path_parse_result_t
set_ns_from_loc (const char *fn, call_frame_t *frame, xlator_t *this,
                 loc_t *loc)
{
        path_parse_result_t ret = PATH_PARSE_RESULT_NO_PATH;
        ns_private_t *priv      = (ns_private_t *)this->private;
        ns_info_t *info         = &frame->root->ns_info;
        char *path              = NULL;

        info->hash = 0;
        info->found = _gf_false;

        if (!priv->tag_namespaces) {
                return ret;
        }

        /* This is our first pass at trying to get a path. Try getting
         * from the inode context, then from the loc's path itself. */
        if (!loc || !loc->path || !loc->inode) {
                ret = PATH_PARSE_RESULT_NO_PATH;
        } else if (!ns_inode_ctx_get (loc->inode, this, info)) {
                ret = PATH_PARSE_RESULT_FOUND;
        } else {
                ret = parse_path (info, loc->path);
                gf_log (this->name, GF_LOG_DEBUG, "%s: LOC retrieved path %s",
                        fn, loc->path);

                if (ret == PATH_PARSE_RESULT_FOUND) {
                        ns_inode_ctx_put (loc->inode, this, info);
                }
        }

        /* Keep trying by calling inode_path next, making sure to copy
        the loc's gfid into its inode if necessary. */
        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                if (gf_uuid_is_null (loc->inode->gfid)) {
                        gf_uuid_copy (loc->inode->gfid, loc->gfid);
                }

                if (inode_path (loc->inode, NULL, &path) >= 0 && path) {
                        ret = parse_path (info, loc->path);
                        gf_log (this->name, GF_LOG_DEBUG,
                                "%s: LOC retrieved path %s", fn, path);

                        if (ret == PATH_PARSE_RESULT_FOUND) {
                                ns_inode_ctx_put (loc->inode, this, info);
                        }
                }

                if (path) {
                        GF_FREE (path);
                }
        }

        /* Report our status, and if we have a GFID, we'll eventually try a
         * GET_ANCESTRY_PATH_KEY wind when we return from this function. */
        if (ret == PATH_PARSE_RESULT_FOUND) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%s: LOC %s %10u namespace found for %s", fn,
                        uuid_utoa (loc->inode->gfid), info->hash, loc->path);
        } else if (ret == PATH_PARSE_RESULT_NO_PATH) {
                gf_log (this->name, GF_LOG_WARNING, "%s: LOC has no path", fn);
        } else if (ret == PATH_PARSE_RESULT_IS_GFID) {
                /* Make sure to copy the inode's gfid for the eventual wind. */
                if (gf_uuid_is_null (loc->inode->gfid)) {
                        gf_uuid_copy (loc->inode->gfid, loc->gfid);
                }

                gf_log (this->name, GF_LOG_DEBUG,
                        "%s: LOC %s winding, looking for path", fn,
                        uuid_utoa (loc->inode->gfid));
        }

        return ret;
}

/* This function tries first to set a namespace based on the information that
 * it can retrieve from an `fd_t`. This includes first looking for a cached
 * namespace in the inode, then trying to call inode_path manually. */
static path_parse_result_t
set_ns_from_fd (const char *fn, call_frame_t *frame, xlator_t *this, fd_t *fd)
{
        path_parse_result_t  ret  = PATH_PARSE_RESULT_NO_PATH;
        ns_private_t        *priv = (ns_private_t *)this->private;
        ns_info_t           *info = &frame->root->ns_info;
        char                *path = NULL;

        info->hash = 0;
        info->found = _gf_false;

        if (!priv->tag_namespaces) {
                return ret;
        }

        /* This is our first pass at trying to get a path. Try getting
         * from the inode context, then inode_path. */
        if (!fd || !fd->inode) {
                ret = PATH_PARSE_RESULT_NO_PATH;
        } else if (!ns_inode_ctx_get (fd->inode, this, info)) {
                ret = PATH_PARSE_RESULT_FOUND;
        } else if (inode_path (fd->inode, NULL, &path) >= 0 && path) {
                ret = parse_path (info, path);
                gf_log (this->name, GF_LOG_DEBUG, "%s: FD  retrieved path %s",
                        fn, path);

                if (ret == PATH_PARSE_RESULT_FOUND) {
                        ns_inode_ctx_put (fd->inode, this, info);
                }
        }

        if (path) {
                GF_FREE (path);
        }

        /* Report our status, and if we have a GFID, we'll eventually try a
         * GET_ANCESTRY_PATH_KEY wind when we return from this function. */
        if (ret == PATH_PARSE_RESULT_FOUND) {
                gf_log (this->name, GF_LOG_DEBUG, "%s: FD  %s %10u namespace found",
                        fn, uuid_utoa (fd->inode->gfid), info->hash);
        } else if (ret == PATH_PARSE_RESULT_NO_PATH) {
                gf_log (this->name, GF_LOG_WARNING, "%s: FD  has no path", fn);
        } else if (ret == PATH_PARSE_RESULT_IS_GFID) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%s: FD  %s winding, looking for path",
                        fn, uuid_utoa (fd->inode->gfid));
        }

        return ret;
}

/* This macro does the work of winding down a call of `getxattr` in the case
 * that we have to retrieve the path manually. It assumes that there is a label
 * called `wind` and the existence of several basic variables (frame, this),
 * but otherwise is general enough for any fop (fd- or loc-based.) */
#define GET_ANCESTRY_PATH_WIND(fop, inode, args...)                            \
        do {                                                                   \
                ns_info_t *info         = &frame->root->ns_info;               \
                call_frame_t *new_frame = NULL;                                \
                ns_local_t *local       = NULL;                                \
                call_stub_t *stub       = NULL;                                \
                                                                               \
                gf_log (this->name, GF_LOG_DEBUG,                              \
                        "    %s winding, looking for path",                    \
                        uuid_utoa (inode->gfid));                              \
                                                                               \
                new_frame = create_frame (this, this->ctx->pool);              \
                if (!new_frame) {                                              \
                        gf_log (this->name, GF_LOG_ERROR,                      \
                                "Cannot allocate new call frame.");            \
                        goto wind;                                             \
                }                                                              \
                                                                               \
                stub = fop_##fop##_stub (frame, default_##fop, args);          \
                if (!stub) {                                                   \
                        gf_log (this->name, GF_LOG_ERROR,                      \
                                "Cannot allocate function stub.");             \
                        goto wind;                                             \
                }                                                              \
                                                                               \
                new_frame->root->uid = 0;                                      \
                new_frame->root->gid = 0;                                      \
                /* Put a phony "not found" NS info into this call. */          \
                new_frame->root->ns_info = *info;                              \
                                                                               \
                local = ns_local_new (stub, inode);                            \
                if (!local) {                                                  \
                        gf_log (this->name, GF_LOG_ERROR,                      \
                                "Cannot allocate function local.");            \
                        goto wind;                                             \
                }                                                              \
                                                                               \
                new_frame->local = local;                                      \
                /* After allocating a new frame, a call stub (to               \
                 * resume our current fop), and a local variables              \
                 * struct (for our loc to getxattr and our resume              \
                 * stub), call getxattr and unwind to get_path_resume_cbk.     \
                 */                                                            \
                STACK_WIND (new_frame, get_path_resume_cbk,                    \
                            FIRST_CHILD (this),                                \
                            FIRST_CHILD (this)->fops->getxattr, &local->loc,   \
                            GET_ANCESTRY_PATH_KEY, NULL);                      \
        } while (0)

int32_t
ns_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflags,
          dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (rmdir, loc->inode, loc, xflags, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_rmdir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->rmdir, loc, xflags, xdata);
        return 0;
}

int32_t
ns_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflags,
           dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (unlink, loc->inode, loc, xflags, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_unlink_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->unlink, loc, xflags, xdata);
        return 0;
}

int32_t
ns_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
           dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, newloc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (rename, newloc->inode, oldloc, newloc,
                                        xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_rename_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->rename, oldloc, newloc, xdata);
        return 0;
}

int32_t
ns_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc,
         dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, newloc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (link, newloc->inode, oldloc, newloc,
                                        xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_link_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->link, oldloc, newloc, xdata);
        return 0;
}

int32_t
ns_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          mode_t umask, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (mkdir, loc->inode, loc, mode, umask,
                                        xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_mkdir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->mkdir, loc, mode, umask, xdata);
        return 0;
}

int32_t
ns_symlink (call_frame_t *frame, xlator_t *this, const char *linkname,
            loc_t *loc, mode_t umask, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (symlink, loc->inode, linkname, loc,
                                        umask, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_symlink_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->symlink, linkname, loc, umask,
                    xdata);
        return 0;
}

int32_t
ns_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          dev_t dev, mode_t umask, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (mknod, loc->inode, loc, mode, dev,
                                        umask, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_mknod_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->mknod, loc, mode, dev, umask,
                    xdata);
        return 0;
}

int32_t
ns_create (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
           mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);
        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (create, loc->inode, loc, flags, mode,
                                        umask, fd, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_create_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->create, loc, flags, mode, umask,
                    fd, xdata);
        return 0;
}

int32_t
ns_fsetattr (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iatt *stbuf,
             int32_t valid, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (fsetattr, fd->inode, fd, stbuf, valid,
                                        xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_fsetattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsetattr, fd, stbuf, valid,
                    xdata);
        return 0;
}

int32_t
ns_setattr (call_frame_t *frame, xlator_t *this, loc_t *loc, struct iatt *stbuf,
            int32_t valid, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (setattr, loc->inode, loc, stbuf, valid,
                                        xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_setattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setattr, loc, stbuf, valid,
                    xdata);
        return 0;
}

int32_t
ns_fremovexattr (call_frame_t *frame, xlator_t *this, fd_t *fd,
                 const char *name, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (fremovexattr, fd->inode, fd, name,
                                        xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_fremovexattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fremovexattr, fd, name, xdata);
        return 0;
}

int32_t
ns_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (removexattr, loc->inode, loc, name,
                                        xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_removexattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->removexattr, loc, name, xdata);
        return 0;
}

int32_t
ns_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
             int32_t flags, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (setxattr, loc->inode, loc, dict, flags,
                                        xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_setxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->setxattr, loc, dict, flags,
                    xdata);
        return 0;
}

int32_t
ns_fsetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *dict,
              int32_t flags, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (fsetxattr, fd->inode, fd, dict, flags,
                                        xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_fsetxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsetxattr, fd, dict, flags,
                    xdata);
        return 0;
}

int32_t
ns_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset,
             dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (truncate, loc->inode, loc, offset,
                                        xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_truncate_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->truncate, loc, offset, xdata);
        return 0;
}

int32_t
ns_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (ftruncate, fd->inode, fd, offset,
                                        xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_ftruncate_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->ftruncate, fd, offset, xdata);
        return 0;
}

int32_t
ns_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t offset, uint32_t flags, struct iobref *iobref,
           dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (writev, fd->inode, fd, vector, count,
                                        offset, flags, iobref, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_writev_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->writev, fd, vector, count, offset,
                    flags, iobref, xdata);
        return 0;
}

int32_t
ns_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (lookup, loc->inode, loc, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_lookup_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->lookup, loc, xdata);
        return 0;
}

int32_t
ns_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (stat, loc->inode, loc, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_stat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->stat, loc, xdata);
        return 0;
}

int32_t
ns_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (fstat, fd->inode, fd, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_fstat_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fstat, fd, xdata);
        return 0;
}

int32_t
ns_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size,
             dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (readlink, loc->inode, loc, size, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_readlink_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readlink, loc, size, xdata);
        return 0;
}

int32_t
ns_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask,
           dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (access, loc->inode, loc, mask, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_access_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->access, loc, mask, xdata);
        return 0;
}

int32_t
ns_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (open, fd->inode, loc, flags, fd, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_open_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->open, loc, flags, fd, xdata);
        return 0;
}

int32_t
ns_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset, uint32_t flags, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (readv, fd->inode, fd, size, offset,
                                        flags, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_readv_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readv, fd, size, offset, flags,
                    xdata);
        return 0;
}

int32_t
ns_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (flush, fd->inode, fd, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_flush_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->flush, fd, xdata);
        return 0;
}

int32_t
ns_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
          dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (fsync, fd->inode, fd, datasync, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_fsync_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsync, fd, datasync, xdata);
        return 0;
}

int32_t
ns_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
            dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (opendir, loc->inode, loc, fd, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_opendir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->opendir, loc, fd, xdata);
        return 0;
}

int32_t
ns_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
             dict_t *xdata)

{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (fsyncdir, fd->inode, fd, datasync,
                                        xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_fsyncdir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fsyncdir, fd, datasync, xdata);
        return 0;
}

int32_t
ns_rchecksum (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
              int32_t len, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (rchecksum, fd->inode, fd, offset, len,
                                        xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_rchecksum_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->rchecksum, fd, offset, len,
                    xdata);
        return 0;
}

int32_t
ns_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (statfs, loc->inode, loc, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_statfs_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->statfs, loc, xdata);
        return 0;
}

int32_t
ns_inodelk (call_frame_t *frame, xlator_t *this, const char *volume, loc_t *loc,
            int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (inodelk, loc->inode, volume, loc, cmd,
                                        flock, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_inodelk_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->inodelk, volume, loc, cmd, flock,
                    xdata);
        return 0;
}

int32_t
ns_finodelk (call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
             int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (finodelk, fd->inode, volume, fd, cmd,
                                        flock, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_finodelk_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->finodelk, volume, fd, cmd, flock,
                    xdata);
        return 0;
}

int32_t
ns_entrylk (call_frame_t *frame, xlator_t *this, const char *volume, loc_t *loc,
            const char *basename, entrylk_cmd cmd, entrylk_type type,
            dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (entrylk, loc->inode, volume, loc,
                                        basename, cmd, type, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_entrylk_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->entrylk, volume, loc, basename,
                    cmd, type, xdata);
        return 0;
}

int32_t
ns_fentrylk (call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
             const char *basename, entrylk_cmd cmd, entrylk_type type,
             dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (fentrylk, fd->inode, volume, fd,
                                        basename, cmd, type, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_fentrylk_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fentrylk, volume, fd, basename,
                    cmd, type, xdata);
        return 0;
}

int32_t
ns_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name,
              dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (fgetxattr, fd->inode, fd, name, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_fgetxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fgetxattr, fd, name, xdata);
        return 0;
}

int32_t
ns_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, const char *name,
             dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (getxattr, loc->inode, loc, name, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_getxattr_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->getxattr, loc, name, xdata);
        return 0;
}

int32_t
ns_lk (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t cmd,
       struct gf_flock *flock, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (lk, fd->inode, fd, cmd, flock, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_lk_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->lk, fd, cmd, flock, xdata);
        return 0;
}

int32_t
ns_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
            off_t offset, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (readdir, fd->inode, fd, size, offset,
                                        xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_readdir_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readdir, fd, size, offset, xdata);

        return 0;
}

int32_t
ns_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset, dict_t *dict)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (readdirp, fd->inode, fd, size, offset,
                                        dict);
                return 0;
        }
wind:
        STACK_WIND (frame, default_readdirp_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->readdirp, fd, size, offset, dict);
        return 0;
}

int32_t
ns_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
            gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_loc (__FUNCTION__, frame, this, loc);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (xattrop, loc->inode, loc, flags, dict,
                                        xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_xattrop_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->xattrop, loc, flags, dict, xdata);

        return 0;
}

int32_t
ns_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
             gf_xattrop_flags_t flags, dict_t *dict, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (fxattrop, fd->inode, fd, flags, dict,
                                        xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_fxattrop_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fxattrop, fd, flags, dict, xdata);

        return 0;
}


int32_t
ns_getspec (call_frame_t *frame, xlator_t *this, const char *key, int32_t flag)
{
        STACK_WIND (frame, default_getspec_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->getspec, key, flag);
        return 0;
}

int32_t
ns_fallocate (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t keep_size,
              off_t offset, size_t len, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (fallocate, fd->inode, fd, keep_size,
                                        offset, len, xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_fallocate_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->fallocate, fd, keep_size, offset,
                    len, xdata);
        return 0;
}

int32_t
ns_discard (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
            size_t len, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (discard, fd->inode, fd, offset, len,
                                        xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_discard_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->discard, fd, offset, len, xdata);
        return 0;
}

int32_t
ns_zerofill (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset,
             off_t len, dict_t *xdata)
{
        path_parse_result_t ret = set_ns_from_fd (__FUNCTION__, frame, this, fd);

        if (ret == PATH_PARSE_RESULT_IS_GFID) {
                GET_ANCESTRY_PATH_WIND (zerofill, fd->inode, fd, offset, len,
                                        xdata);
                return 0;
        }
wind:
        STACK_WIND (frame, default_zerofill_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->zerofill, fd, offset, len, xdata);
        return 0;
}

int
ns_forget (xlator_t *this, inode_t *inode)
{
        uint64_t ns_as_64 = 0;
        ns_info_t *info   = NULL;

        inode_ctx_del (inode, this, &ns_as_64);

        if (!ns_as_64) {
                return 0;
        }

        info = (ns_info_t *)ns_as_64;
        GF_FREE (info);

        return 0;
}

int32_t
init (xlator_t *this)
{
        int32_t ret        = -1;
        ns_private_t *priv = NULL;

        GF_VALIDATE_OR_GOTO (GF_NAMESPACE, this, out);

        if (!this->children || this->children->next) {
                gf_log (this->name, GF_LOG_ERROR,
                        "translator needs a single subvolume.");
                goto out;
        }

        if (!this->parents) {
                gf_log (this->name, GF_LOG_ERROR,
                        "dangling volume. please check volfile.");
                goto out;
        }

        priv = GF_CALLOC (1, sizeof (ns_private_t), 0);

        if (!priv) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Can't allocate ns_priv structure.");
                goto out;
        }

        GF_OPTION_INIT ("tag-namespaces", priv->tag_namespaces, bool, out);

        gf_log (this->name, GF_LOG_INFO, "Namespace xlator loaded");
        this->private = priv;
        ret           = 0;

out:
        if (ret) {
                GF_FREE (priv);
        }

        return ret;
}

void
fini (xlator_t *this)
{
        GF_FREE (this->private);
}

int
reconfigure (xlator_t *this, dict_t *options)
{
        int ret            = -1;
        ns_private_t *priv = NULL;

        GF_VALIDATE_OR_GOTO (this->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, options, out);

        priv = (ns_private_t *)this->private;

        GF_OPTION_RECONF ("tag-namespaces", priv->tag_namespaces, options, bool,
                          out);

        ret = 0;
out:
        return ret;
}

struct xlator_fops fops = {
        .lookup       = ns_lookup,
        .stat         = ns_stat,
        .fstat        = ns_fstat,
        .truncate     = ns_truncate,
        .ftruncate    = ns_ftruncate,
        .access       = ns_access,
        .readlink     = ns_readlink,
        .mknod        = ns_mknod,
        .mkdir        = ns_mkdir,
        .unlink       = ns_unlink,
        .rmdir        = ns_rmdir,
        .symlink      = ns_symlink,
        .rename       = ns_rename,
        .link         = ns_link,
        .create       = ns_create,
        .open         = ns_open,
        .readv        = ns_readv,
        .writev       = ns_writev,
        .flush        = ns_flush,
        .fsync        = ns_fsync,
        .opendir      = ns_opendir,
        .readdir      = ns_readdir,
        .readdirp     = ns_readdirp,
        .fsyncdir     = ns_fsyncdir,
        .statfs       = ns_statfs,
        .setxattr     = ns_setxattr,
        .getxattr     = ns_getxattr,
        .fsetxattr    = ns_fsetxattr,
        .fgetxattr    = ns_fgetxattr,
        .removexattr  = ns_removexattr,
        .fremovexattr = ns_fremovexattr,
        .lk           = ns_lk,
        .inodelk      = ns_inodelk,
        .finodelk     = ns_finodelk,
        .entrylk      = ns_entrylk,
        .fentrylk     = ns_fentrylk,
        .rchecksum    = ns_rchecksum,
        .xattrop      = ns_xattrop,
        .fxattrop     = ns_fxattrop,
        .setattr      = ns_setattr,
        .fsetattr     = ns_fsetattr,
        .getspec      = ns_getspec,
        .fallocate    = ns_fallocate,
        .discard      = ns_discard,
        .zerofill     = ns_zerofill,
};

struct xlator_cbks cbks = {
        .forget = ns_forget,
};

struct xlator_dumpops dumpops;

struct volume_options options[] = {
        {
        .key           = { "tag-namespaces" },
        .type          = GF_OPTION_TYPE_BOOL,
        .default_value = "off",
        .description   = "This option enables this translator's functionality "
                       "that tags every fop with a namespace hash for later "
                       "throttling, stats collection, logging, etc.",
        .op_version    = {GD_OP_VERSION_4_1_0},
        .tags          = {"namespace"},
        .flags         = OPT_FLAG_SETTABLE | OPT_FLAG_CLIENT_OPT | OPT_FLAG_DOC,
        },
        {.key = { NULL } },
};
