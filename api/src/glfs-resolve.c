/*
  Copyright (c) 2012-2018 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <limits.h>

#include <glusterfs/glusterfs.h>
#include <glusterfs/logging.h>
#include <glusterfs/stack.h>
#include <glusterfs/gf-event.h>
#include "glfs-mem-types.h"
#include <glusterfs/common-utils.h>
#include <glusterfs/syncop.h>
#include <glusterfs/call-stub.h>
#include "gfapi-messages.h"
#include <glusterfs/inode.h>
#include "glfs-internal.h"

#define graphid_str(subvol)                                                    \
    (uuid_utoa((unsigned char *)subvol->graph->graph_uuid))
int
glfs_first_lookup_safe(xlator_t *subvol)
{
    loc_t loc = {
        0,
    };
    int ret = -1;

    loc.inode = subvol->itable->root;
    memset(loc.gfid, 0, 16);
    loc.gfid[15] = 1;
    loc.path = "/";
    loc.name = "";

    ret = syncop_lookup(subvol, &loc, 0, 0, 0, 0);
    DECODE_SYNCOP_ERR(ret);

    gf_msg_debug(subvol->name, 0, "first lookup complete %d", ret);

    return ret;
}

int
__glfs_first_lookup(struct glfs *fs, xlator_t *subvol)
{
    int ret = -1;

    fs->migration_in_progress = 1;
    pthread_mutex_unlock(&fs->mutex);
    {
        ret = glfs_first_lookup_safe(subvol);
    }
    pthread_mutex_lock(&fs->mutex);
    fs->migration_in_progress = 0;
    pthread_cond_broadcast(&fs->cond);

    /* wake up other waiting tasks */
    __GLFS_SYNCTASK_WAKE(fs);

    return ret;
}

/**
 * We have to check if need_lookup flag is set in both old and the new inodes.
 * If its set in oldinode, then directly go ahead and do an explicit lookup.
 * But if its not set in the oldinode, then check if the newinode is linked
 * via readdirp. If so an explicit lookup is needed on the new inode, so that
 * below xlators can set their respective contexts.
 */
inode_t *
glfs_refresh_inode_safe(xlator_t *subvol, inode_t *oldinode,
                        gf_boolean_t need_lookup)
{
    loc_t loc = {
        0,
    };
    int ret = -1;
    struct iatt iatt = {
        0,
    };
    inode_t *newinode = NULL;
    gf_boolean_t lookup_needed = _gf_false;
    uint64_t ctx_value = LOOKUP_NOT_NEEDED;

    if (!oldinode)
        return NULL;

    if (!need_lookup && oldinode->table->xl == subvol)
        return inode_ref(oldinode);

    newinode = inode_find(subvol->itable, oldinode->gfid);
    if (!need_lookup && newinode) {
        lookup_needed = inode_needs_lookup(newinode, THIS);
        if (!lookup_needed)
            return newinode;
    }

    gf_uuid_copy(loc.gfid, oldinode->gfid);
    if (!newinode)
        loc.inode = inode_new(subvol->itable);
    else
        loc.inode = newinode;

    if (!loc.inode)
        return NULL;

    ret = syncop_lookup(subvol, &loc, &iatt, 0, 0, 0);
    DECODE_SYNCOP_ERR(ret);

    if (ret) {
        gf_smsg(subvol->name, GF_LOG_WARNING, errno,
                API_MSG_INODE_REFRESH_FAILED, "gfid=%s",
                uuid_utoa(oldinode->gfid), "err=%s", strerror(errno), NULL);
        loc_wipe(&loc);
        return NULL;
    }

    newinode = inode_link(loc.inode, 0, 0, &iatt);
    if (newinode) {
        if (newinode == loc.inode)
            inode_ctx_set(newinode, THIS, &ctx_value);
        inode_lookup(newinode);
    } else {
        gf_smsg(subvol->name, GF_LOG_WARNING, errno, API_MSG_INODE_LINK_FAILED,
                "gfid=%s", uuid_utoa((unsigned char *)&iatt.ia_gfid), NULL);
    }

    loc_wipe(&loc);

    return newinode;
}

inode_t *
__glfs_refresh_inode(struct glfs *fs, xlator_t *subvol, inode_t *inode,
                     gf_boolean_t need_lookup)
{
    inode_t *newinode = NULL;

    fs->migration_in_progress = 1;
    pthread_mutex_unlock(&fs->mutex);
    {
        newinode = glfs_refresh_inode_safe(subvol, inode, need_lookup);
    }
    pthread_mutex_lock(&fs->mutex);
    fs->migration_in_progress = 0;
    pthread_cond_broadcast(&fs->cond);

    /* wake up other waiting tasks */
    __GLFS_SYNCTASK_WAKE(fs);

    return newinode;
}

GFAPI_SYMVER_PRIVATE_DEFAULT(glfs_loc_touchup, 3.4.0)
int
priv_glfs_loc_touchup(loc_t *loc)
{
    int ret = 0;

    ret = loc_touchup(loc, loc->name);
    if (ret < 0) {
        errno = -ret;
        ret = -1;
    }

    return ret;
}

int
glfs_resolve_symlink(struct glfs *fs, xlator_t *subvol, inode_t *inode,
                     char **lpath)
{
    loc_t loc = {
        0,
    };
    char *path = NULL;
    char *rpath = NULL;
    int ret = -1;

    loc.inode = inode_ref(inode);
    gf_uuid_copy(loc.gfid, inode->gfid);
    ret = inode_path(inode, NULL, &rpath);
    if (ret < 0)
        goto out;
    loc.path = rpath;

    ret = syncop_readlink(subvol, &loc, &path, 4096, NULL, NULL);
    DECODE_SYNCOP_ERR(ret);

    if (ret < 0)
        goto out;

    if (lpath)
        *lpath = path;
out:
    loc_wipe(&loc);
    return ret;
}

int
glfs_resolve_base(struct glfs *fs, xlator_t *subvol, inode_t *inode,
                  struct iatt *iatt)
{
    loc_t loc = {
        0,
    };
    int ret = -1;
    char *path = NULL;

    loc.inode = inode_ref(inode);
    gf_uuid_copy(loc.gfid, inode->gfid);

    ret = inode_path(loc.inode, NULL, &path);
    loc.path = path;
    if (ret < 0)
        goto out;

    ret = syncop_lookup(subvol, &loc, iatt, NULL, NULL, NULL);
    DECODE_SYNCOP_ERR(ret);
out:
    loc_wipe(&loc);

    return ret;
}
/*
 * This function can be used to call named lookup on root.
 * If you use glfs_resolve_base, that will be a nameless lookup.
 */
static int
glfs_resolve_root(struct glfs *fs, xlator_t *subvol, inode_t *inode,
                  struct iatt *iatt)
{
    loc_t loc = {
        0,
    };
    int ret = -1;
    char *path = NULL;

    loc.inode = inode_ref(inode);

    ret = inode_path(loc.inode, ".", &path);
    loc.path = path;
    loc.name = ".";
    /* Having a value in loc.name will help to bypass md-cache check for
     * nameless lookup.
     * TODO: Re-visit on nameless lookup and md-cache.
     * Github issue : https://github.com/gluster/glusterfs/issues/232
     */
    loc.parent = inode_ref(inode);
    if (ret < 0)
        goto out;

    ret = syncop_lookup(subvol, &loc, iatt, NULL, NULL, NULL);
    DECODE_SYNCOP_ERR(ret);
out:
    loc_wipe(&loc);

    return ret;
}

inode_t *
glfs_resolve_component(struct glfs *fs, xlator_t *subvol, inode_t *parent,
                       const char *component, struct iatt *iatt,
                       int force_lookup)
{
    loc_t loc = {
        0,
    };
    inode_t *inode = NULL;
    inode_t *temp_parent = NULL;
    int reval = 0;
    int ret = -1;
    int glret = -1;
    struct iatt ciatt = {
        0,
    };
    uuid_t gfid;
    dict_t *xattr_req = NULL;
    dict_t *xattr_rsp = NULL;
    uint64_t ctx_value = LOOKUP_NOT_NEEDED;

    loc.parent = inode_ref(parent);
    gf_uuid_copy(loc.pargfid, parent->gfid);

    if (__is_root_gfid(parent->gfid) &&
        ((strcmp(component, ".") == 0) || (strcmp(component, "..") == 0) ||
         (strcmp(component, "") == 0))) {
        if (!force_lookup) {
            inode = inode_ref(parent);
        } else {
            ret = glfs_resolve_root(fs, subvol, parent, &ciatt);
            if (!ret)
                inode = inode_ref(parent);
        }
        goto found;
    }
    /* *
     * if the component name is either "." or "..", it will try to
     * resolve that if inode has a proper parent (named lookup).
     *
     * Below condition works like this
     *
     * Example 1 :
     * Path /out_dir/dir/in_dir/.
     *     In put values :
     *         parent = in_dir
     *         component : "."
     *
     *      Out put values:
     *         parent : dir
     *         component : "in_dir"
     *
     * Example 2 :
     * Path /out_dir/dir/in_dir/..
     *     In put values :
     *         parent = in_dir
     *         component : ".."
     *
     *     Out put values:
     *         parent : output_dir
     *         component : "dir"
     *
     * In case of nameless lookup, both "." and ".." retained
     */

    if (strcmp(component, ".") == 0) {
        loc.inode = inode_ref(parent);
        temp_parent = inode_parent(loc.inode, 0, 0);
        if (temp_parent) {
            inode_unref(loc.parent);
            loc.parent = temp_parent;
            gf_uuid_copy(loc.pargfid, temp_parent->gfid);
            inode_find_directory_name(loc.inode, &loc.name);
        }

    } else if (strcmp(component, "..") == 0) {
        loc.inode = inode_parent(parent, 0, 0);
        if (loc.inode) {
            temp_parent = inode_parent(loc.inode, 0, 0);
            if (temp_parent) {
                inode_unref(loc.parent);
                loc.parent = temp_parent;
                gf_uuid_copy(loc.pargfid, temp_parent->gfid);
                inode_find_directory_name(loc.inode, &loc.name);
            } else if (__is_root_gfid(loc.inode->gfid)) {
                inode_unref(loc.parent);
                loc.parent = inode_ref(loc.inode);
                gf_uuid_copy(loc.pargfid, loc.inode->gfid);
                loc.name = ".";
            } else {
                inode_unref(loc.inode);
                loc.inode = NULL;
            }
        }
    } else
        loc.inode = inode_grep(parent->table, parent, component);

    if (!loc.name)
        loc.name = component;

    if (loc.inode) {
        gf_uuid_copy(loc.gfid, loc.inode->gfid);
        reval = 1;

        if (!(force_lookup || inode_needs_lookup(loc.inode, THIS))) {
            inode = inode_ref(loc.inode);
            goto found;
        }
    } else {
        gf_uuid_generate(gfid);
        loc.inode = inode_new(parent->table);
        if (!loc.inode) {
            errno = ENOMEM;
            goto out;
        }

        xattr_req = dict_new();
        if (!xattr_req) {
            errno = ENOMEM;
            goto out;
        }

        ret = dict_set_gfuuid(xattr_req, "gfid-req", gfid, true);
        if (ret) {
            errno = ENOMEM;
            goto out;
        }
        ret = dict_set_int32(xattr_req, GF_NAMESPACE_KEY, 1);
    }

    glret = priv_glfs_loc_touchup(&loc);
    if (glret < 0) {
        ret = -1;
        goto out;
    }

    ret = syncop_lookup(subvol, &loc, &ciatt, NULL, xattr_req, &xattr_rsp);
    if (ret && reval) {
        /*
         * A stale mapping might exist for a dentry/inode that has been
         * removed from another client.
         */
        if (-ret == ENOENT) {
            inode_unlink(loc.inode, loc.parent, loc.name);
            if (!inode_has_dentry(loc.inode))
                inode_forget(loc.inode, 0);
        }

        inode_unref(loc.inode);
        gf_uuid_clear(loc.gfid);
        loc.inode = inode_new(parent->table);
        if (!loc.inode) {
            errno = ENOMEM;
            goto out;
        }

        xattr_req = dict_new();
        if (!xattr_req) {
            errno = ENOMEM;
            goto out;
        }

        gf_uuid_generate(gfid);

        ret = dict_set_gfuuid(xattr_req, "gfid-req", gfid, true);
        if (ret) {
            errno = ENOMEM;
            goto out;
        }

        ret = dict_set_int32(xattr_req, GF_NAMESPACE_KEY, 1);
        ret = syncop_lookup(subvol, &loc, &ciatt, NULL, xattr_req, &xattr_rsp);
    }
    DECODE_SYNCOP_ERR(ret);
    if (ret)
        goto out;

    inode = inode_link(loc.inode, loc.parent, component, &ciatt);

    if (!inode) {
        gf_smsg(subvol->name, GF_LOG_WARNING, errno, API_MSG_INODE_LINK_FAILED,
                "gfid=%s", uuid_utoa((unsigned char *)&ciatt.ia_gfid), NULL);
        goto out;
    } else if (inode == loc.inode)
        inode_ctx_set(inode, THIS, &ctx_value);
found:
    if (inode) {
        ciatt.ia_type = inode->ia_type;
        inode_lookup(inode);
        if (xattr_rsp && dict_get_sizen(xattr_rsp, GF_NAMESPACE_KEY)) {
            /* This inode onwards we will set namespace */
            inode_set_namespace_inode(inode, inode);
        }
    }
    if (iatt)
        *iatt = ciatt;
out:
    if (xattr_req)
        dict_unref(xattr_req);
    loc_wipe(&loc);

    return inode;
}

GFAPI_SYMVER_PRIVATE_DEFAULT(glfs_resolve_at, 3.4.0)
int
priv_glfs_resolve_at(struct glfs *fs, xlator_t *subvol, inode_t *at,
                     const char *origpath, loc_t *loc, struct iatt *iatt,
                     int follow, int reval)
{
    inode_t *inode = NULL;
    inode_t *parent = NULL;
    char *saveptr = NULL;
    char *path = NULL;
    char *component = NULL;
    char *next_component = NULL;
    int ret = -1;
    struct iatt ciatt = {
        0,
    };

    DECLARE_OLD_THIS;
    __GLFS_ENTRY_VALIDATE_FS(fs, invalid_fs);

    if (origpath[0] == '\0') {
        errno = EINVAL;
        goto invalid_fs;
    }

    parent = NULL;
    if (at && origpath[0] != '/') {
        /* A relative resolution of a path which starts with '/'
           is equal to an absolute path resolution.
        */
        inode = inode_ref(at);
    } else {
        inode = inode_ref(subvol->itable->root);

        if (strcmp(origpath, "/") == 0)
            glfs_resolve_root(fs, subvol, inode, &ciatt);
    }

    path = gf_strdup(origpath);
    if (!path)
        goto invalid_fs;

    for (component = strtok_r(path, "/", &saveptr); component;
         component = next_component) {
        next_component = strtok_r(NULL, "/", &saveptr);

        if (parent)
            inode_unref(parent);
        parent = inode;
        inode = glfs_resolve_component(fs, subvol, parent, component, &ciatt,
                                       /* force hard lookup on the last
                                          component, as the caller
                                          wants proper iatt filled
                                       */
                                       (reval || (!next_component && iatt)));
        if (!inode) {
            ret = -1;
            break;
        }

        if (IA_ISLNK(ciatt.ia_type) && (next_component || follow)) {
            /* If the component is not the last piece,
               then following it is necessary even if
               not requested by the caller
            */
            char *lpath = NULL;
            loc_t sym_loc = {
                0,
            };

            if (follow > GLFS_SYMLINK_MAX_FOLLOW) {
                errno = ELOOP;
                ret = -1;
                if (inode) {
                    inode_unref(inode);
                    inode = NULL;
                }
                break;
            }

            ret = glfs_resolve_symlink(fs, subvol, inode, &lpath);
            inode_unref(inode);
            inode = NULL;
            if (ret < 0)
                break;

            ret = priv_glfs_resolve_at(fs, subvol, parent, lpath, &sym_loc,
                                       /* followed iatt becomes the
                                          component iatt
                                       */
                                       &ciatt,
                                       /* always recurisvely follow while
                                          following symlink
                                       */
                                       follow + 1, reval);
            if (ret == 0)
                inode = inode_ref(sym_loc.inode);
            loc_wipe(&sym_loc);
            GF_FREE(lpath);
        }

        if (!next_component)
            break;

        if (!IA_ISDIR(ciatt.ia_type)) {
            /* next_component exists and this component is
               not a directory
            */
            inode_unref(inode);
            inode = NULL;
            ret = -1;
            errno = ENOTDIR;
            break;
        }
    }

    if (parent && next_component)
        /* resolution failed mid-way */
        goto out;

    /* At this point, all components up to the last parent directory
       have been resolved successfully (@parent). Resolution of basename
       might have failed (@inode) if at all.
    */

    loc->parent = parent;
    if (parent) {
        gf_uuid_copy(loc->pargfid, parent->gfid);
        loc->name = component;
    }

    loc->inode = inode;
    if (inode) {
        gf_uuid_copy(loc->gfid, inode->gfid);
        if (iatt)
            *iatt = ciatt;
        ret = 0;
    }

    if (priv_glfs_loc_touchup(loc) < 0) {
        ret = -1;
    }
out:
    GF_FREE(path);
    __GLFS_EXIT_FS;

    /* do NOT loc_wipe here as only last component might be missing */
invalid_fs:
    return ret;
}

int
glfs_resolve_path(struct glfs *fs, xlator_t *subvol, const char *origpath,
                  loc_t *loc, struct iatt *iatt, int follow, int reval)
{
    int ret = -1;
    inode_t *cwd = NULL;

    if (origpath[0] == '/')
        return priv_glfs_resolve_at(fs, subvol, NULL, origpath, loc, iatt,
                                    follow, reval);

    cwd = glfs_cwd_get(fs);
    if (NULL == cwd) {
        gf_smsg(subvol->name, GF_LOG_WARNING, EIO, API_MSG_GET_CWD_FAILED,
                NULL);
        errno = EIO;
        goto out;
    }

    ret = priv_glfs_resolve_at(fs, subvol, cwd, origpath, loc, iatt, follow,
                               reval);
    if (cwd)
        inode_unref(cwd);

out:
    return ret;
}

GFAPI_SYMVER_PRIVATE_DEFAULT(glfs_resolve, 3.7.0)
int
priv_glfs_resolve(struct glfs *fs, xlator_t *subvol, const char *origpath,
                  loc_t *loc, struct iatt *iatt, int reval)
{
    int ret = -1;

    ret = glfs_resolve_path(fs, subvol, origpath, loc, iatt, 1, reval);

    return ret;
}

int
glfs_lresolve(struct glfs *fs, xlator_t *subvol, const char *origpath,
              loc_t *loc, struct iatt *iatt, int reval)
{
    int ret = -1;

    ret = glfs_resolve_path(fs, subvol, origpath, loc, iatt, 0, reval);

    return ret;
}

int
glfs_migrate_fd_locks_safe(struct glfs *fs, xlator_t *oldsubvol, fd_t *oldfd,
                           xlator_t *newsubvol, fd_t *newfd)
{
    dict_t *lockinfo = NULL;
    int ret = 0;
    char uuid1[64];

    if (!oldfd->lk_ctx || fd_lk_ctx_empty(oldfd->lk_ctx))
        return 0;

    newfd->lk_ctx = fd_lk_ctx_ref(oldfd->lk_ctx);

    ret = syncop_fgetxattr(oldsubvol, oldfd, &lockinfo, GF_XATTR_LOCKINFO_KEY,
                           NULL, NULL);
    DECODE_SYNCOP_ERR(ret);
    if (ret < 0) {
        gf_smsg(fs->volname, GF_LOG_WARNING, errno, API_MSG_FGETXATTR_FAILED,
                "gfid=%s", uuid_utoa_r(oldfd->inode->gfid, uuid1), "err=%s",
                strerror(errno), "subvol=%s", graphid_str(oldsubvol), "id=%d",
                oldsubvol->graph->id, NULL);
        goto out;
    }

    if (!dict_get(lockinfo, GF_XATTR_LOCKINFO_KEY)) {
        gf_smsg(fs->volname, GF_LOG_WARNING, 0, API_MSG_LOCKINFO_KEY_MISSING,
                "gfid=%s", uuid_utoa_r(oldfd->inode->gfid, uuid1), "subvol=%s",
                graphid_str(oldsubvol), "id=%d", oldsubvol->graph->id, NULL);
        goto out;
    }

    ret = syncop_fsetxattr(newsubvol, newfd, lockinfo, 0, NULL, NULL);
    DECODE_SYNCOP_ERR(ret);
    if (ret < 0) {
        gf_smsg(fs->volname, GF_LOG_WARNING, 0, API_MSG_FSETXATTR_FAILED,
                "gfid=%s", uuid_utoa_r(newfd->inode->gfid, uuid1), "err=%s",
                strerror(errno), "subvol=%s", graphid_str(newsubvol), "id=%d",
                newsubvol->graph->id, NULL);
        goto out;
    }
out:
    if (lockinfo)
        dict_unref(lockinfo);
    return ret;
}

fd_t *
glfs_migrate_fd_safe(struct glfs *fs, xlator_t *newsubvol, fd_t *oldfd)
{
    fd_t *newfd = NULL;
    inode_t *oldinode = NULL;
    inode_t *newinode = NULL;
    xlator_t *oldsubvol = NULL;
    int ret = -1;
    loc_t loc = {
        0,
    };
    char uuid1[64];
    dict_t *xdata = NULL;

    oldinode = oldfd->inode;
    oldsubvol = oldinode->table->xl;

    if (oldsubvol == newsubvol)
        return fd_ref(oldfd);

    if (!oldsubvol->switched) {
        xdata = dict_new();
        if (!xdata || dict_set_int8(xdata, "last-fsync", 1)) {
            gf_smsg(fs->volname, GF_LOG_WARNING, ENOMEM, API_MSG_FSYNC_FAILED,
                    "err=%s", "last-fsync set failed", "gfid=%s",
                    uuid_utoa_r(oldfd->inode->gfid, uuid1), "subvol=%s",
                    graphid_str(oldsubvol), "id=%d", oldsubvol->graph->id,
                    NULL);
        }

        ret = syncop_fsync(oldsubvol, oldfd, 0, NULL, NULL, xdata, NULL);
        DECODE_SYNCOP_ERR(ret);
        if (ret) {
            gf_smsg(fs->volname, GF_LOG_WARNING, errno, API_MSG_FSYNC_FAILED,
                    "err=%s", strerror(errno), "gfid=%s",
                    uuid_utoa_r(oldfd->inode->gfid, uuid1), "subvol=%s",
                    graphid_str(oldsubvol), "id=%d", oldsubvol->graph->id,
                    NULL);
        }
    }

    newinode = glfs_refresh_inode_safe(newsubvol, oldinode, _gf_false);
    if (!newinode) {
        gf_smsg(fs->volname, GF_LOG_WARNING, errno,
                API_MSG_INODE_REFRESH_FAILED, "gfid=%s",
                uuid_utoa_r(oldinode->gfid, uuid1), "err=%s", strerror(errno),
                "subvol=%s", graphid_str(newsubvol), "id=%d",
                newsubvol->graph->id, NULL);
        goto out;
    }

    newfd = fd_create(newinode, getpid());
    if (!newfd) {
        gf_smsg(fs->volname, GF_LOG_WARNING, errno,
                API_MSG_FDCREATE_FAILED_ON_GRAPH, "gfid=%s",
                uuid_utoa_r(newinode->gfid, uuid1), "err=%s", strerror(errno),
                "subvol=%s", graphid_str(newsubvol), "id=%d",
                newsubvol->graph->id, NULL);
        goto out;
    }

    loc.inode = inode_ref(newinode);

    ret = inode_path(oldfd->inode, NULL, (char **)&loc.path);
    if (ret < 0) {
        gf_smsg(fs->volname, GF_LOG_INFO, 0, API_MSG_INODE_PATH_FAILED, NULL);
        goto out;
    }

    gf_uuid_copy(loc.gfid, oldinode->gfid);

    if (IA_ISDIR(oldinode->ia_type))
        ret = syncop_opendir(newsubvol, &loc, newfd, NULL, NULL);
    else
        ret = syncop_open(newsubvol, &loc,
                          oldfd->flags & ~(O_TRUNC | O_EXCL | O_CREAT), newfd,
                          NULL, NULL);
    DECODE_SYNCOP_ERR(ret);
    loc_wipe(&loc);

    if (ret) {
        gf_smsg(fs->volname, GF_LOG_WARNING, errno, API_MSG_SYNCOP_OPEN_FAILED,
                "type=%s", IA_ISDIR(oldinode->ia_type) ? "dir" : "", "gfid=%s",
                uuid_utoa_r(newinode->gfid, uuid1), "err=%s", strerror(errno),
                "subvol=%s", graphid_str(newsubvol), "id=%d",
                newsubvol->graph->id, NULL);
        goto out;
    }

    ret = glfs_migrate_fd_locks_safe(fs, oldsubvol, oldfd, newsubvol, newfd);

    if (ret) {
        gf_smsg(fs->volname, GF_LOG_WARNING, errno, API_MSG_LOCK_MIGRATE_FAILED,
                "gfid=%s", uuid_utoa_r(newinode->gfid, uuid1), "err=%s",
                strerror(errno), "subvol=%s", graphid_str(newsubvol), "id=%d",
                newsubvol->graph->id, NULL);
        goto out;
    }

    newfd->flags = oldfd->flags;
    fd_bind(newfd);
out:
    if (newinode)
        inode_unref(newinode);

    if (ret) {
        fd_unref(newfd);
        newfd = NULL;
    }

    if (xdata)
        dict_unref(xdata);

    return newfd;
}

fd_t *
__glfs_migrate_fd(struct glfs *fs, xlator_t *newsubvol, struct glfs_fd *glfd)
{
    fd_t *oldfd = NULL;
    fd_t *newfd = NULL;

    oldfd = glfd->fd;

    fs->migration_in_progress = 1;
    pthread_mutex_unlock(&fs->mutex);
    {
        newfd = glfs_migrate_fd_safe(fs, newsubvol, oldfd);
    }
    pthread_mutex_lock(&fs->mutex);
    fs->migration_in_progress = 0;
    pthread_cond_broadcast(&fs->cond);

    /* wake up other waiting tasks */
    __GLFS_SYNCTASK_WAKE(fs);

    return newfd;
}

fd_t *
__glfs_resolve_fd(struct glfs *fs, xlator_t *subvol, struct glfs_fd *glfd)
{
    fd_t *fd = NULL;

    if (glfd->fd->inode->table->xl == subvol)
        return fd_ref(glfd->fd);

    fd = __glfs_migrate_fd(fs, subvol, glfd);
    if (!fd)
        return NULL;

    if (subvol == fs->active_subvol) {
        fd_unref(glfd->fd);
        glfd->fd = fd_ref(fd);
    }

    return fd;
}

fd_t *
glfs_resolve_fd(struct glfs *fs, xlator_t *subvol, struct glfs_fd *glfd)
{
    fd_t *fd = NULL;

    glfs_lock(fs, _gf_true);
    {
        fd = __glfs_resolve_fd(fs, subvol, glfd);
    }
    glfs_unlock(fs);

    return fd;
}

void
__glfs_migrate_openfds(struct glfs *fs, xlator_t *subvol)
{
    struct glfs_fd *glfd = NULL;
    fd_t *fd = NULL;

    list_for_each_entry(glfd, &fs->openfds, openfds)
    {
        if (gf_uuid_is_null(glfd->fd->inode->gfid)) {
            gf_smsg(fs->volname, GF_LOG_INFO, 0, API_MSG_OPENFD_SKIPPED,
                    "glfd=%p", glfd, "glfd->fd=%p", glfd->fd, "subvol=%s",
                    graphid_str(subvol), "id=%d", subvol->graph->id, NULL);
            /* create in progress, defer */
            continue;
        }

        fd = __glfs_migrate_fd(fs, subvol, glfd);
        if (fd) {
            fd_unref(glfd->fd);
            glfd->fd = fd;
        }
    }
}

/* Note that though it appears that this function executes under fs->mutex,
 * it is not fully executed under fs->mutex. i.e. there are functions like
 * __glfs_first_lookup, __glfs_refresh_inode, __glfs_migrate_openfds which
 * unlocks fs->mutex before sending any network fop, and reacquire fs->mutex
 * once the fop is complete. Hence the variable read from fs at the start of the
 * function need not have the same value by the end of the function.
 */
xlator_t *
__glfs_active_subvol(struct glfs *fs)
{
    xlator_t *new_subvol = NULL;
    int ret = -1;
    inode_t *new_cwd = NULL;

    if (!fs->next_subvol)
        return fs->active_subvol;

    new_subvol = fs->mip_subvol = fs->next_subvol;
    fs->next_subvol = NULL;

    ret = __glfs_first_lookup(fs, new_subvol);
    if (ret) {
        gf_smsg(fs->volname, GF_LOG_INFO, errno,
                API_MSG_FIRST_LOOKUP_GRAPH_FAILED, "subvol=%s",
                graphid_str(new_subvol), "id=%d", new_subvol->graph->id,
                "err=%s", strerror(errno), NULL);
        return NULL;
    }

    if (fs->cwd) {
        new_cwd = __glfs_refresh_inode(fs, new_subvol, fs->cwd, _gf_false);

        if (!new_cwd) {
            char buf1[64];
            gf_smsg(fs->volname, GF_LOG_INFO, errno,
                    API_MSG_CWD_GRAPH_REF_FAILED, "buf=%s",
                    uuid_utoa_r(fs->cwd->gfid, buf1), "subvol=%s",
                    graphid_str(new_subvol), "id=%d", new_subvol->graph->id,
                    "err=%s", strerror(errno), NULL);
            return NULL;
        }
    }

    __glfs_migrate_openfds(fs, new_subvol);
    /* TODO: Migrate the fds and inodes which have leases to the new graph
     * (issue #350)*/

    /* switching @active_subvol and @cwd
       should be atomic
    */
    fs->old_subvol = fs->active_subvol;
    fs->active_subvol = fs->mip_subvol;
    fs->mip_subvol = NULL;

    if (new_cwd) {
        __glfs_cwd_set(fs, new_cwd);
        inode_unref(new_cwd);
    }

    gf_smsg(fs->volname, GF_LOG_INFO, 0, API_MSG_SWITCHED_GRAPH, "subvol=%s",
            graphid_str(new_subvol), "id=%d", new_subvol->graph->id, NULL);

    return new_subvol;
}

GFAPI_SYMVER_PRIVATE_DEFAULT(glfs_subvol_done, 3.4.0)
void
priv_glfs_subvol_done(struct glfs *fs, xlator_t *subvol)
{
    int ref = 0;
    xlator_t *active_subvol = NULL;

    if (!subvol)
        return;

    /* For decrementing subvol->wind ref count we need not check/wait for
     * migration-in-progress flag.
     * Also glfs_subvol_done is called in call-back path therefore waiting
     * for migration-in-progress flag can lead to dead-lock.
     */
    glfs_lock(fs, _gf_false);
    {
        ref = (--subvol->winds);
        active_subvol = fs->active_subvol;
    }
    glfs_unlock(fs);

    if (ref == 0) {
        assert(subvol != active_subvol);
        xlator_notify(subvol, GF_EVENT_PARENT_DOWN, subvol, NULL);
    }
}

GFAPI_SYMVER_PRIVATE_DEFAULT(glfs_active_subvol, 3.4.0)
xlator_t *
priv_glfs_active_subvol(struct glfs *fs)
{
    xlator_t *subvol = NULL;
    xlator_t *old_subvol = NULL;

    glfs_lock(fs, _gf_true);
    {
        subvol = __glfs_active_subvol(fs);

        if (subvol)
            subvol->winds++;

        if (fs->old_subvol) {
            old_subvol = fs->old_subvol;
            fs->old_subvol = NULL;
            old_subvol->switched = 1;
        }
    }
    glfs_unlock(fs);

    if (old_subvol)
        priv_glfs_subvol_done(fs, old_subvol);

    return subvol;
}

int
__glfs_cwd_set(struct glfs *fs, inode_t *inode)
{
    if (inode->table->xl != fs->active_subvol) {
        inode = __glfs_refresh_inode(fs, fs->active_subvol, inode, _gf_false);
        if (!inode)
            return -1;
    } else {
        inode_ref(inode);
    }

    if (fs->cwd)
        inode_unref(fs->cwd);

    fs->cwd = inode;

    return 0;
}

int
glfs_cwd_set(struct glfs *fs, inode_t *inode)
{
    int ret = 0;

    glfs_lock(fs, _gf_true);
    {
        ret = __glfs_cwd_set(fs, inode);
    }
    glfs_unlock(fs);

    return ret;
}

inode_t *
__glfs_cwd_get(struct glfs *fs)
{
    inode_t *cwd = NULL;

    if (!fs->cwd)
        return NULL;

    if (fs->cwd->table->xl == fs->active_subvol) {
        cwd = inode_ref(fs->cwd);
        return cwd;
    }

    cwd = __glfs_refresh_inode(fs, fs->active_subvol, fs->cwd, _gf_false);

    return cwd;
}

inode_t *
glfs_cwd_get(struct glfs *fs)
{
    inode_t *cwd = NULL;

    glfs_lock(fs, _gf_true);
    {
        cwd = __glfs_cwd_get(fs);
    }
    glfs_unlock(fs);

    return cwd;
}

inode_t *
__glfs_resolve_inode(struct glfs *fs, xlator_t *subvol,
                     struct glfs_object *object)
{
    inode_t *inode = NULL;
    gf_boolean_t lookup_needed = _gf_false;

    lookup_needed = inode_needs_lookup(object->inode, THIS);

    if (!lookup_needed && object->inode->table->xl == subvol)
        return inode_ref(object->inode);

    inode = __glfs_refresh_inode(fs, fs->active_subvol, object->inode,
                                 lookup_needed);
    if (!inode)
        return NULL;

    if (subvol == fs->active_subvol) {
        inode_unref(object->inode);
        object->inode = inode_ref(inode);
    }

    return inode;
}

inode_t *
glfs_resolve_inode(struct glfs *fs, xlator_t *subvol,
                   struct glfs_object *object)
{
    inode_t *inode = NULL;

    glfs_lock(fs, _gf_true);
    {
        inode = __glfs_resolve_inode(fs, subvol, object);
    }
    glfs_unlock(fs);

    return inode;
}

int
glfs_create_object(loc_t *loc, struct glfs_object **retobject)
{
    struct glfs_object *object = NULL;

    object = GF_CALLOC(1, sizeof(struct glfs_object), glfs_mt_glfs_object_t);
    if (object == NULL) {
        errno = ENOMEM;
        return -1;
    }

    object->inode = loc->inode;
    gf_uuid_copy(object->gfid, object->inode->gfid);

    /* we hold the reference */
    loc->inode = NULL;

    *retobject = object;

    return 0;
}

struct glfs_object *
glfs_h_resolve_symlink(struct glfs *fs, struct glfs_object *object)
{
    xlator_t *subvol = NULL;
    loc_t sym_loc = {
        0,
    };
    struct iatt iatt = {
        0,
    };
    char *lpath = NULL;
    int ret = 0;
    struct glfs_object *target_object = NULL;

    subvol = glfs_active_subvol(fs);
    if (!subvol) {
        ret = -1;
        errno = EIO;
        goto out;
    }

    ret = glfs_resolve_symlink(fs, subvol, object->inode, &lpath);
    if (ret < 0)
        goto out;

    ret = glfs_resolve_at(fs, subvol, NULL, lpath, &sym_loc, &iatt,
                          /* always recurisvely follow while
                            following symlink
                          */
                          1, 0);
    if (ret == 0)
        ret = glfs_create_object(&sym_loc, &target_object);

out:
    loc_wipe(&sym_loc);
    GF_FREE(lpath);
    return target_object;
}
