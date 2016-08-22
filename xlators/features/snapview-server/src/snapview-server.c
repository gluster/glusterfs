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
#include "compat-errno.h"

#include "xlator.h"
#include "rpc-clnt.h"
#include "xdr-generic.h"
#include "protocol-common.h"
#include "syscall.h"
#include <pthread.h>


int32_t
svs_lookup_entry_point (xlator_t *this, loc_t *loc, inode_t *parent,
                        struct iatt *buf, struct iatt *postparent,
                        int32_t *op_errno)
{
        uuid_t         gfid;
        svs_inode_t   *inode_ctx = NULL;
        int            op_ret    = -1;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);
        GF_VALIDATE_OR_GOTO (this->name, buf, out);
        GF_VALIDATE_OR_GOTO (this->name, postparent, out);

        if (gf_uuid_is_null (loc->inode->gfid)) {
                gf_uuid_generate (gfid);
                svs_iatt_fill (gfid, buf);

                /* Here the inode context of the entry point directory
                   is filled with just the type of the inode and the gfid
                   of the parent from where the entry point was entered.
                   The glfs object and the fs instance will be NULL.
                */
                if (parent)
                        svs_iatt_fill (parent->gfid, postparent);
                else {
                        svs_iatt_fill (buf->ia_gfid, postparent);
                }

                inode_ctx = svs_inode_ctx_get_or_new (this, loc->inode);
                if (!inode_ctx) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to "
                                "allocate inode context for entry point "
                                "directory");
                        op_ret = -1;
                        *op_errno = ENOMEM;
                        goto out;
                }
                gf_uuid_copy (inode_ctx->pargfid, loc->pargfid);
                memcpy (&inode_ctx->buf, buf, sizeof (*buf));
                inode_ctx->type = SNAP_VIEW_ENTRY_POINT_INODE;
        } else {
                if (inode_ctx) {
                        memcpy (buf, &inode_ctx->buf, sizeof (*buf));
                        svs_iatt_fill (inode_ctx->pargfid, postparent);
                } else {
                        svs_iatt_fill (loc->inode->gfid, buf);
                        if (parent)
                                svs_iatt_fill (parent->gfid,
                                               postparent);
                        else {
                                svs_iatt_fill (loc->inode->gfid,
                                               postparent);
                        }
                }
        }

        op_ret = 0;

out:
        return op_ret;
}

/* When lookup comes from client and the protocol/server tries to resolve
   the pargfid via just sending the gfid as part of lookup, if the inode
   for the parent gfid is not found. But since that gfid has not yet been
   looked  up yet, inode will not be having inode context and parent is not
   there (as it is the parent of the entry that is being resolved). So
   without parent and inode context, svs cannot know which snapshot
   to look into. In such cases, the amguity is handled by looking
   into the latest snapshot. If the directory is there in the latest
   snapshot, lookup is successful, otherwise it is a failure. So for
   any directory created after taking the latest snapshot, entry into
   snapshot world is denied. i.e you have to be part of snapshot world
   to enter it. If the gfid is not found there, then unwind with
   ESTALE
   This gets executed mainly in the situation where the snapshot entry
   point is entered from a non-root directory and that non-root directory's
   inode (or gfid) is not yet looked up. And in each case when a gfid has to
   be looked up (without any inode contex and parent context present), last
   snapshot is referred and a random gfid is not generated.
*/
int32_t
svs_lookup_gfid (xlator_t *this, loc_t *loc, struct iatt *buf,
                 struct iatt *postparent, int32_t *op_errno)
{
        int32_t         op_ret                          = -1;
        unsigned char   handle_obj[GFAPI_HANDLE_LENGTH] = {0, };
        glfs_t         *fs                              = NULL;
        glfs_object_t  *object                          = NULL;
        struct stat     statbuf                         = {0, };
        svs_inode_t    *inode_ctx                       = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);
        GF_VALIDATE_OR_GOTO (this->name, buf, out);
        GF_VALIDATE_OR_GOTO (this->name, postparent, out);

        if (gf_uuid_is_null (loc->gfid) && gf_uuid_is_null (loc->inode->gfid)) {
                gf_log (this->name, GF_LOG_ERROR, "gfid is NULL");
                goto out;
        }

        if (!gf_uuid_is_null (loc->inode->gfid))
                memcpy (handle_obj, loc->inode->gfid,
                        GFAPI_HANDLE_LENGTH);
        else
                memcpy (handle_obj, loc->gfid,
                        GFAPI_HANDLE_LENGTH);

        fs = svs_get_latest_snapshot (this);
        if (!fs) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the latest "
                        "snapshot");
                op_ret = -1;
                *op_errno = EINVAL;
                goto out;
        }


        object = glfs_h_create_from_handle (fs, handle_obj, GFAPI_HANDLE_LENGTH,
                                            &statbuf);
        if (!object) {
                gf_log (this->name, GF_LOG_ERROR, "failed to do lookup and get "
                        "the handle on the snapshot %s (path: %s, gfid: %s)",
                        loc->name, loc->path, uuid_utoa (loc->gfid));
                op_ret = -1;
                *op_errno = ESTALE;
                goto out;
        }

        inode_ctx = svs_inode_ctx_get_or_new (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate inode "
                        "context");
                op_ret = -1;
                *op_errno = ENOMEM;
                goto out;
        }

        iatt_from_stat (buf, &statbuf);
        if (!gf_uuid_is_null (loc->gfid))
                gf_uuid_copy (buf->ia_gfid, loc->gfid);
        else
                gf_uuid_copy (buf->ia_gfid, loc->inode->gfid);

        inode_ctx->type = SNAP_VIEW_VIRTUAL_INODE;
        inode_ctx->fs = fs;
        inode_ctx->object = object;
        memcpy (&inode_ctx->buf, buf, sizeof (*buf));
        svs_iatt_fill (buf->ia_gfid, postparent);

        op_ret = 0;

out:
        return op_ret;
}

/* If the parent is an entry point inode, then create the handle for the
   snapshot on which lookup came. i.e in reality lookup came on
   the directory from which the entry point directory was entered, but
   lookup is into the past. So create the handle for it by doing
   the name-less lookup on the gfid (which can be obtained from
   parent's context
*/
int32_t
svs_lookup_snapshot (xlator_t *this, loc_t *loc, struct iatt *buf,
                     struct iatt *postparent, inode_t *parent,
                     svs_inode_t *parent_ctx, int32_t *op_errno)
{
        int32_t         op_ret                          = -1;
        unsigned char   handle_obj[GFAPI_HANDLE_LENGTH] = {0, };
        glfs_t         *fs                              = NULL;
        glfs_object_t  *object                          = NULL;
        struct stat     statbuf                         = {0, };
        svs_inode_t    *inode_ctx                       = NULL;
        uuid_t          gfid;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);
        GF_VALIDATE_OR_GOTO (this->name, buf, out);
        GF_VALIDATE_OR_GOTO (this->name, postparent, out);
        GF_VALIDATE_OR_GOTO (this->name, parent_ctx, out);
        GF_VALIDATE_OR_GOTO (this->name, parent, out);

        fs = svs_initialise_snapshot_volume (this, loc->name, op_errno);
        if (!fs) {
                gf_log (this->name, GF_LOG_DEBUG, "failed to "
                        "create the fs instance for snap %s",
                        loc->name);
                *op_errno = ENOENT;
                op_ret = -1;
                goto out;
        }

        memcpy (handle_obj, parent_ctx->pargfid,
                GFAPI_HANDLE_LENGTH);
        object = glfs_h_create_from_handle (fs, handle_obj, GFAPI_HANDLE_LENGTH,
                                            &statbuf);
        if (!object) {
                gf_log (this->name, GF_LOG_DEBUG, "failed to do lookup and "
                        "get the handle on the snapshot %s", loc->name);
                op_ret = -1;
                *op_errno = errno;
                goto out;
        }

        inode_ctx = svs_inode_ctx_get_or_new (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "failed to "
                        "allocate inode context");
                op_ret = -1;
                *op_errno = ENOMEM;
                goto out;
        }

        if (gf_uuid_is_null (loc->gfid) &&
            gf_uuid_is_null (loc->inode->gfid))
                gf_uuid_generate (gfid);
        else {
                if (!gf_uuid_is_null (loc->inode->gfid))
                        gf_uuid_copy (gfid, loc->inode->gfid);
                else
                        gf_uuid_copy (gfid, loc->gfid);
        }
        iatt_from_stat (buf, &statbuf);
        gf_uuid_copy (buf->ia_gfid, gfid);
        svs_fill_ino_from_gfid (buf);
        inode_ctx->type = SNAP_VIEW_SNAPSHOT_INODE;
        inode_ctx->fs = fs;
        inode_ctx->object = object;
        memcpy (&inode_ctx->buf, buf, sizeof (*buf));
        svs_iatt_fill (parent->gfid, postparent);

        SVS_STRDUP (inode_ctx->snapname, loc->name);
        if (!inode_ctx->snapname) {
                op_ret = -1;
                *op_errno = ENOMEM;
                goto out;
        }
        op_ret = 0;

out:
        if (op_ret) {
                if (object)
                        glfs_h_close (object);

                if (inode_ctx)
                        inode_ctx->object = NULL;
        }

        return op_ret;
}

/* Both parent and entry are from snapshot world */
int32_t
svs_lookup_entry (xlator_t *this, loc_t *loc, struct iatt *buf,
                  struct iatt *postparent, inode_t *parent,
                  svs_inode_t *parent_ctx, int32_t *op_errno)
{
        int32_t         op_ret                          = -1;
        glfs_t         *fs                              = NULL;
        glfs_object_t  *object                          = NULL;
        struct stat     statbuf                         = {0, };
        svs_inode_t    *inode_ctx                       = NULL;
        glfs_object_t  *parent_object                   = NULL;
        uuid_t          gfid                            = {0, };

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);
        GF_VALIDATE_OR_GOTO (this->name, buf, out);
        GF_VALIDATE_OR_GOTO (this->name, postparent, out);
        GF_VALIDATE_OR_GOTO (this->name, parent_ctx, out);
        GF_VALIDATE_OR_GOTO (this->name, parent, out);

        parent_object = parent_ctx->object;
        fs = parent_ctx->fs;

        object = glfs_h_lookupat (fs, parent_object, loc->name,
                                  &statbuf, 0);
        if (!object) {
                gf_log (this->name, GF_LOG_DEBUG, "failed to do lookup and "
                        "get the handle for entry %s (path: %s)", loc->name,
                        loc->path);
                op_ret = -1;
                *op_errno = errno;
                goto out;
        }

        if (gf_uuid_is_null(object->gfid)) {
                gf_log (this->name, GF_LOG_DEBUG, "gfid from glfs handle is "
                        "NULL for entry %s (path: %s)", loc->name, loc->path);
                op_ret = -1;
                *op_errno = errno;
                goto out;
        }

        inode_ctx = svs_inode_ctx_get_or_new (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "failed to "
                        "allocate inode context");
                op_ret = -1;
                *op_errno = ENOMEM;
                goto out;
        }

        if (gf_uuid_is_null (loc->gfid) &&
            gf_uuid_is_null (loc->inode->gfid))
                svs_uuid_generate (gfid, parent_ctx->snapname, object->gfid);
        else {
                if (!gf_uuid_is_null (loc->inode->gfid))
                        gf_uuid_copy (gfid, loc->inode->gfid);
                else
                        gf_uuid_copy (gfid, loc->gfid);
        }

        iatt_from_stat (buf, &statbuf);
        gf_uuid_copy (buf->ia_gfid, gfid);
        svs_fill_ino_from_gfid (buf);
        inode_ctx->type = SNAP_VIEW_VIRTUAL_INODE;
        inode_ctx->fs = fs;
        inode_ctx->object = object;
        memcpy (&inode_ctx->buf, buf, sizeof (*buf));
        svs_iatt_fill (parent->gfid, postparent);

        if (IA_ISDIR (buf->ia_type)) {
                SVS_STRDUP (inode_ctx->snapname, parent_ctx->snapname);
                if (!inode_ctx->snapname) {
                        op_ret = -1;
                        *op_errno = ENOMEM;
                        goto out;
                }
        }

        op_ret = 0;

out:
        if (op_ret) {
                if (object)
                        glfs_h_close (object);

                if (inode_ctx)
                        inode_ctx->object = NULL;
        }

        return op_ret;
}

/* inode context is there means lookup has come on an object which was
   built either as part of lookup or as part of readdirp. But in readdirp
   we would not have got the handle to access the object in the gfapi
   world.
   So if inode context contains glfs_t instance for the right
   gfapi world and glfs_object_t handle for accessing it in the gfapi
   world, then unwind with success as the snapshots as of now are
   read-only.
   If the above condition is not met, then send lookup call again to
   the gfapi world. It can happen only if both parent context and
   the name of the entry are present.

   If parent is an entry point to snapshot world:
   * parent is needed for getting the gfid on which lookup has to be done
     (the gfid present in the inode is a virtual gfid) in the snapshot
     world.
   * name is required to get the right glfs_t instance on which lookup
     has to be done

   If parent is a directory from snapshot world:
   * parent context is needed to get the glfs_t instance and to get the
     handle to parent directory in the snapshot world.
   * name is needed to do the lookup on the right entry in the snapshot
     world
*/
int32_t
svs_revalidate (xlator_t *this, loc_t *loc, inode_t *parent,
                svs_inode_t *inode_ctx, svs_inode_t *parent_ctx,
                struct iatt *buf, struct iatt *postparent, int32_t *op_errno)
{
        int32_t         op_ret                          = -1;
        int             ret                             = -1;
        char            tmp_uuid[64]                    = {0, };
        glfs_t         *fs                              = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, buf, out);
        GF_VALIDATE_OR_GOTO (this->name, postparent, out);
        GF_VALIDATE_OR_GOTO (this->name, inode_ctx, out);

        if (inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                svs_iatt_fill (loc->inode->gfid, buf);
                if (parent)
                        svs_iatt_fill (parent->gfid,
                                       postparent);
                else
                        svs_iatt_fill (loc->inode->gfid, postparent);
                op_ret = 0;
                goto out;
        } else {
                /* Though fs and object are present in the inode context, its
                 * better to check if fs is valid or not before doing anything.
                 * Its for the protection from the following operations.
                 * 1) Create a file on the glusterfs mount point
                 * 2) Create a snapshot (say "snap1")
                 * 3) Access the contents of the snapshot
                 * 4) Delete the file from the mount point
                 * 5) Delete the snapshot "snap1"
                 * 6) Create a new snapshot "snap1"
                 *
                 * Now accessing the new snapshot "snap1" gives problems.
                 * Because the inode and dentry created for snap1 would not be
                 * deleted upon the deletion of the snapshot (as deletion of
                 * snapshot is a gluster cli operation, not a fop). So next time
                 * upon creation of a new snap with same name, the previous
                 * inode and dentry itself will be used. But the inode context
                 * contains old information about the glfs_t instance and the
                 * handle in the gfapi world. Thus the glfs_t instance should
                 * be checked before accessing. If its wrong, then right
                 * instance should be obtained by doing the lookup.
                 */
                if (inode_ctx->fs && inode_ctx->object) {
                        fs = inode_ctx->fs;
                        SVS_CHECK_VALID_SNAPSHOT_HANDLE(fs, this);
                        if (fs) {
                                memcpy (buf, &inode_ctx->buf, sizeof (*buf));
                                if (parent)
                                        svs_iatt_fill (parent->gfid,
                                                       postparent);
                                else
                                        svs_iatt_fill (buf->ia_gfid,
                                                       postparent);
                                op_ret = 0;
                                goto out;
                        } else {
                                inode_ctx->fs = NULL;
                                inode_ctx->object = NULL;
                                ret = svs_get_handle (this, loc, inode_ctx,
                                                      op_errno);
                                if (ret) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "failed to get the handle for "
                                                "%s (gfid %s)", loc->path,
                                                uuid_utoa_r (loc->inode->gfid,
                                                             tmp_uuid));
                                        op_ret = -1;
                                        goto out;
                                }
                        }
                }

                /* To send the lookup to gfapi world, both the name of the
                   entry as well as the parent context is needed.
                */
                if (!loc->name || !parent_ctx) {
                        *op_errno = ESTALE;
                        gf_log (this->name, GF_LOG_ERROR, "%s is NULL",
                                loc->name?"parent context":"loc->name");
                        goto out;
                }

                if (parent_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE)
                        op_ret = svs_lookup_snapshot (this, loc, buf,
                                                      postparent, parent,
                                                      parent_ctx, op_errno);
                else
                        op_ret = svs_lookup_entry (this, loc, buf, postparent,
                                                   parent, parent_ctx,
                                                   op_errno);

                goto out;
        }

out:
        return op_ret;
}

int32_t
svs_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        struct iatt    buf                            = {0, };
        int32_t        op_ret                         = -1;
        int32_t        op_errno                       = EINVAL;
        struct iatt    postparent                     = {0,};
        svs_inode_t   *inode_ctx                      = NULL;
        svs_inode_t   *parent_ctx                     = NULL;
        int32_t        ret                            = -1;
        inode_t       *parent                         = NULL;
        snap_dirent_t *dirent                         = NULL;
        gf_boolean_t   entry_point_key                = _gf_false;
        gf_boolean_t   entry_point                    = _gf_false;

        GF_VALIDATE_OR_GOTO ("svs", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        /* For lookups sent on inodes (i.e not parent inode + basename, but
           direct inode itself which usually is a nameless lookup or revalidate
           on the inode), loc->name will not be there. Get it from path if
           it is there.
           This is the difference between nameless lookup and revalidate lookup
           on an inode:
           nameless lookup: loc->path contains gfid and strrchr on it fails
           revalidate lookup: loc->path contains the entry name of the inode
                              and strrchr gives the name of the entry from path
        */
        if (loc->path) {
                if (!loc->name || (loc->name && !strcmp (loc->name, ""))) {
                        loc->name = strrchr (loc->path, '/');
                        if (loc->name)
                                loc->name++;
                }
        }

        if (loc->parent)
                parent = inode_ref (loc->parent);
        else {
                parent = inode_find (loc->inode->table, loc->pargfid);
                if (!parent)
                        parent = inode_parent (loc->inode, NULL, NULL);
        }
        if (parent)
                parent_ctx = svs_inode_ctx_get (this, parent);

        inode_ctx = svs_inode_ctx_get (this, loc->inode);

        /* Initialize latest snapshot, which is used for nameless lookups */
        dirent = svs_get_latest_snap_entry (this);

        if (dirent && !dirent->fs) {
                svs_initialise_snapshot_volume (this, dirent->name, NULL);
        }

        if (xdata && !inode_ctx) {
                ret = dict_get_str_boolean (xdata, "entry-point", _gf_false);
                if (ret == -1) {
                        gf_log (this->name, GF_LOG_DEBUG, "failed to get the "
                                "entry point info");
                        entry_point_key = _gf_false;
                } else {
                        entry_point_key = ret;
                }

                if (loc->name && strlen (loc->name)) {
                        /* lookup can come with the entry-point set in the dict
                        * for the parent directory of the entry-point as well.
                        * So consider entry_point only for named lookup
                        */
                        entry_point = entry_point_key;
                }
        }

        if (inode_ctx && inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                /* entry-point may not be set in the dictonary.
                 * This can happen if snap-view client is restarted where
                 * inode-ctx not available and a nameless lookup has come
                 */
                entry_point = _gf_true;
        }

        /* lookup is on the entry point to the snapshot world */
        if (entry_point) {
                op_ret = svs_lookup_entry_point (this, loc, parent, &buf,
                                                 &postparent, &op_errno);
                goto out;
        }

        /* revalidate */
        if (inode_ctx) {
                op_ret = svs_revalidate (this, loc, parent, inode_ctx,
                                         parent_ctx, &buf, &postparent,
                                         &op_errno);
                goto out;
        }

        /* This can happen when entry point directory is entered from non-root
           directory. (ex: if /mnt/glusterfs is the mount point, then entry
           point (say .snaps) is entered from /mnt/glusterfs/dir/.snaps). Also
           it can happen when client sends a nameless lookup on just a gfid and
           the server does not have the inode in the inode table.
        */
        if (!inode_ctx && !parent_ctx) {
                if (gf_uuid_is_null (loc->gfid) &&
                    gf_uuid_is_null (loc->inode->gfid)) {
                        gf_log (this->name, GF_LOG_DEBUG, "gfid is NULL, "
                                "either the lookup came on missing entry or "
                                "the entry is stale");
                        op_ret = -1;
                        op_errno = ESTALE;
                        goto out;
                }

                if (!entry_point_key) {
                        /* This can happen when there is no inode_ctx available.
                        * snapview-server might have restarted or
                        * graph change might have happened
                        */
                        op_ret = -1;
                        op_errno = ESTALE;
                        goto out;
                }

                /* lookup is on the parent directory of entry-point.
                 * this would have already looked up by snap-view client
                 * so return success
                 */
                if (!gf_uuid_is_null (loc->gfid))
                        gf_uuid_copy (buf.ia_gfid, loc->gfid);
                else
                        gf_uuid_copy (buf.ia_gfid, loc->inode->gfid);

                svs_iatt_fill (buf.ia_gfid, &buf);
                svs_iatt_fill (buf.ia_gfid, &postparent);

                op_ret = 0;
                goto out;
        }

        if (parent_ctx) {
                if (parent_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE)
                        op_ret = svs_lookup_snapshot (this, loc, &buf,
                                                      &postparent, parent,
                                                      parent_ctx, &op_errno);
                else
                        op_ret = svs_lookup_entry (this, loc, &buf,
                                                   &postparent, parent,
                                                   parent_ctx, &op_errno);
                goto out;
        }

out:
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno,
                             loc?loc->inode:NULL, &buf, xdata, &postparent);

        if (parent)
                inode_unref (parent);

        return 0;
}

int32_t
svs_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd,
             dict_t *xdata)
{
        svs_inode_t   *inode_ctx  = NULL;
        int32_t        op_ret     = -1;
        int32_t        op_errno   = EINVAL;
        svs_fd_t      *svs_fd     = NULL;
        glfs_fd_t     *glfd       = NULL;
        glfs_t        *fs         = NULL;
        glfs_object_t *object     = NULL;

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        inode_ctx = svs_inode_ctx_get (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "inode context not found "
                        "for the inode %s", uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        /* Fake success is sent if the opendir is on the entry point directory
           or the inode is SNAP_VIEW_ENTRY_POINT_INODE
        */
        if  (inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                op_ret = 0;
                op_errno = 0;
                goto out;
        }
        else {

                SVS_GET_INODE_CTX_INFO(inode_ctx, fs, object, this, loc, op_ret,
                                       op_errno, out);

                glfd = glfs_h_opendir (fs, object);
                if (!glfd) {
                        op_ret = -1;
                        op_errno = errno;
                        gf_log (this->name, GF_LOG_ERROR, "opendir on %s "
                                "failed (gfid: %s)", loc->name,
                                uuid_utoa (loc->inode->gfid));
                        goto out;
                }
                svs_fd = svs_fd_ctx_get_or_new (this, fd);
                if (!svs_fd) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to allocate "
                                "fd context %s (gfid: %s)", loc->name,
                                uuid_utoa (fd->inode->gfid));
                        op_ret = -1;
                        op_errno = ENOMEM;
                        glfs_closedir (glfd);
                        goto out;
                }
                svs_fd->fd = glfd;

                op_ret = 0;
                op_errno = 0;
        }

out:
        STACK_UNWIND_STRICT (opendir, frame, op_ret, op_errno, fd, NULL);

        return 0;
}

/*
 * This function adds the xattr keys present in the list (@list) to the dict.
 * But the list contains only the names of the xattrs (and no value, as
 * the gfapi functions for the listxattr operations would return only the
 * names of the xattrs in the buffer provided by the caller, though they had
 * got the values of those xattrs from posix) as described in the man page of
 * listxattr. But before unwinding snapview-server has to put those names
 * back into the dict. But to get the values for those xattrs it has to do the
 * getxattr operation on each xattr which might turn out to be a costly
 * operation. So for each of the xattrs present in the list, a 0 byte value
 * ("") is set into the dict before unwinding. This can be treated as an
 * indicator to other xlators which want to cache the xattrs (as of now,
 * md-cache which caches acl and selinux related xattrs) to not to cache the
 * values of the xattrs present in the dict.
 */
int32_t
svs_add_xattrs_to_dict (xlator_t *this, dict_t *dict, char *list, ssize_t size)
{
        char           keybuffer[4096]  = {0,};
        size_t         remaining_size   = 0;
        int32_t        list_offset      = 0;
        int32_t        ret              = -1;

        GF_VALIDATE_OR_GOTO ("snapview-daemon", this, out);
        GF_VALIDATE_OR_GOTO (this->name, dict, out);
        GF_VALIDATE_OR_GOTO (this->name, list, out);

        remaining_size = size;
        list_offset = 0;
        while (remaining_size > 0) {
                strncpy (keybuffer, list + list_offset, sizeof (keybuffer) - 1);
#ifdef GF_DARWIN_HOST_OS
                /* The protocol expect namespace for now */
                char *newkey = NULL;
                gf_add_prefix (XATTR_USER_PREFIX, keybuffer, &newkey);
                strcpy (keybuffer, newkey);
                GF_FREE (newkey);
#endif
                ret = dict_set_str (dict, keybuffer, "");
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "dict set operation "
                                "for the key %s failed.", keybuffer);
                        goto out;
                }

                remaining_size -= strlen (keybuffer) + 1;
                list_offset += strlen (keybuffer) + 1;
        } /* while (remaining_size > 0) */

        ret = 0;

out:
        return ret;
}

int32_t
svs_getxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, const char *name,
              dict_t *xdata)
{
        svs_inode_t   *inode_ctx        = NULL;
        int32_t        op_ret           = -1;
        int32_t        op_errno         = EINVAL;
        glfs_t        *fs               = NULL;
        glfs_object_t *object           = NULL;
        char          *value            = 0;
        ssize_t        size             = 0;
        dict_t        *dict             = NULL;

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO ("snap-view-daemon", frame, out);
        GF_VALIDATE_OR_GOTO ("snap-view-daemon", loc, out);
        GF_VALIDATE_OR_GOTO ("snap-view-daemon", loc->inode, out);

        inode_ctx = svs_inode_ctx_get (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "inode context not found "
                        "for the inode %s", uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        /* ENODATA is sent if the getxattr is on entry point directory
           or the inode is SNAP_VIEW_ENTRY_POINT_INODE. Entry point is
           a virtual directory on which setxattr operations are not
           allowed. If getxattr has to be faked as success, then a value
           for the name of the xattr has to be sent which we dont have.
        */
        if (inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                op_ret = -1;
                op_errno = ENODATA;
                goto out;
        }
        else {

                SVS_GET_INODE_CTX_INFO(inode_ctx, fs, object, this, loc, op_ret,
                                       op_errno, out);

                dict = dict_new ();
                if (!dict) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to "
                                "allocate dict");
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto out;
                }

                size = glfs_h_getxattrs (fs, object, name, NULL, 0);
                if (size == -1) {
                        gf_log (this->name,
                                errno == ENODATA?GF_LOG_DEBUG:GF_LOG_ERROR,
                                "getxattr on %s failed (key: %s) with %s",
                                loc->path, name, strerror(errno));
                        op_ret = -1;
                        op_errno = errno;
                        goto out;
                }
                value = GF_CALLOC (size + 1, sizeof (char),
                                   gf_common_mt_char);
                if (!value) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to "
                                "allocate memory for getxattr on %s "
                                "(key: %s)", loc->name, name);
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto out;
                }

                size = glfs_h_getxattrs (fs, object, name, value, size);
                if (size == -1) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to "
                                "get the xattr %s for entry %s", name,
                                loc->name);
                        op_ret = -1;
                        op_errno = errno;
                        goto out;
                }
                value[size] = '\0';

                if (name) {
                        op_ret = dict_set_dynptr (dict, (char *)name, value,
                                                  size);
                        if (op_ret < 0) {
                                op_errno = -op_ret;
                                gf_log (this->name, GF_LOG_ERROR, "dict set "
                                        "operation for %s for the key %s "
                                        "failed.", loc->path, name);
                                GF_FREE (value);
                                value = NULL;
                                goto out;
                        }
                } else {
                        op_ret = svs_add_xattrs_to_dict (this, dict, value,
                                                         size);
                        if (op_ret == -1) {
                                gf_log (this->name, GF_LOG_ERROR, "failed to "
                                        "add the xattrs from the list to dict");
                                op_errno = ENOMEM;
                                goto out;
                        }
                        GF_FREE (value);
                }
        }

out:
        if (op_ret)
                GF_FREE (value);

        STACK_UNWIND_STRICT (getxattr, frame, op_ret, op_errno, dict, NULL);

        if (dict)
                dict_unref (dict);

        return 0;
}

int32_t
svs_fgetxattr (call_frame_t *frame, xlator_t *this, fd_t *fd, const char *name,
               dict_t *xdata)
{
        svs_inode_t *inode_ctx  = NULL;
        int32_t      op_ret     = -1;
        int32_t      op_errno   = EINVAL;
        char        *value      = 0;
        ssize_t      size       = 0;
        dict_t      *dict       = NULL;
        svs_fd_t    *sfd        = NULL;
        glfs_fd_t   *glfd       = NULL;

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO ("snap-view-daemon", frame, out);
        GF_VALIDATE_OR_GOTO ("snap-view-daemon", fd, out);
        GF_VALIDATE_OR_GOTO ("snap-view-daemon", fd->inode, out);

        inode_ctx = svs_inode_ctx_get (this, fd->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "inode context not found "
                        "for the inode %s", uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        sfd = svs_fd_ctx_get_or_new (this, fd);
        if (!sfd) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the fd "
                        "context for %s", uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = EBADFD;
                goto out;
        }

        glfd = sfd->fd;
        /* EINVAL is sent if the getxattr is on entry point directory
           or the inode is SNAP_VIEW_ENTRY_POINT_INODE. Entry point is
           a virtual directory on which setxattr operations are not
           allowed. If getxattr has to be faked as success, then a value
           for the name of the xattr has to be sent which we dont have.
        */
        if (inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }
        else {
                dict = dict_new ();
                if (!dict) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to "
                                "allocate  dict");
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto out;
                }

                if (name) {
                        size = glfs_fgetxattr (glfd, name, NULL, 0);
                        if (size == -1) {
                                gf_log (this->name, GF_LOG_ERROR, "getxattr on "
                                        "%s failed (key: %s)",
                                        uuid_utoa (fd->inode->gfid), name);
                                op_ret = -1;
                                op_errno = errno;
                                goto out;
                        }
                        value = GF_CALLOC (size + 1, sizeof (char),
                                           gf_common_mt_char);
                        if (!value) {
                                gf_log (this->name, GF_LOG_ERROR, "failed to "
                                        "allocate memory for getxattr on %s "
                                        "(key: %s)",
                                        uuid_utoa (fd->inode->gfid), name);
                                op_ret = -1;
                                op_errno = ENOMEM;
                                goto out;
                        }

                        size = glfs_fgetxattr (glfd, name, value, size);
                        if (size == -1) {
                                gf_log (this->name, GF_LOG_ERROR, "failed to "
                                        "get the xattr %s for inode %s", name,
                                        uuid_utoa (fd->inode->gfid));
                                op_ret = -1;
                                op_errno = errno;
                                goto out;
                        }
                        value[size] = '\0';

                        op_ret = dict_set_dynptr (dict, (char *)name, value,
                                                  size);
                        if (op_ret < 0) {
                                op_errno = -op_ret;
                                gf_log (this->name, GF_LOG_ERROR, "dict set "
                                        "operation for gfid %s for the key %s "
                                        "failed.",
                                        uuid_utoa (fd->inode->gfid), name);
                                GF_FREE (value);
                                goto out;
                        }
                } else {
                        size = glfs_flistxattr (glfd, NULL, 0);
                        if (size == -1) {
                                gf_log (this->name, GF_LOG_ERROR, "listxattr "
                                        "on %s failed",
                                        uuid_utoa (fd->inode->gfid));
                                goto out;
                        }

                        value = GF_CALLOC (size + 1, sizeof (char),
                                           gf_common_mt_char);
                        if (!value) {
                                op_ret = -1;
                                op_errno = ENOMEM;
                                gf_log (this->name, GF_LOG_ERROR, "failed to "
                                        "allocate buffer for xattr list (%s)",
                                        uuid_utoa (fd->inode->gfid));
                                goto out;
                        }

                        size = glfs_flistxattr (glfd, value, size);
                        if (size == -1) {
                                op_ret = -1;
                                op_errno = errno;
                                gf_log (this->name, GF_LOG_ERROR, "listxattr "
                                        "on %s failed",
                                        uuid_utoa (fd->inode->gfid));
                                goto out;
                        }

                        op_ret = svs_add_xattrs_to_dict (this, dict, value,
                                                         size);
                        if (op_ret == -1) {
                                gf_log (this->name, GF_LOG_ERROR, "failed to "
                                        "add the xattrs from the list to dict");
                                op_errno = ENOMEM;
                                goto out;
                        }
                        GF_FREE (value);
                }

                op_ret = 0;
                op_errno = 0;
        }

out:
        if (op_ret)
                GF_FREE (value);

        STACK_UNWIND_STRICT (fgetxattr, frame, op_ret, op_errno, dict, NULL);

        if (dict)
                dict_unref (dict);

        return 0;
}

int32_t
svs_releasedir (xlator_t *this, fd_t *fd)
{
        svs_fd_t *sfd      = NULL;
        uint64_t          tmp_pfd  = 0;
        int               ret      = 0;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        ret = fd_ctx_del (fd, this, &tmp_pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd from fd=%p is NULL", fd);
                goto out;
        }

        sfd = (svs_fd_t *)(long)tmp_pfd;
        if (sfd->fd) {
                ret = glfs_closedir (sfd->fd);
                if (ret)
                        gf_log (this->name, GF_LOG_WARNING, "failed to close "
                                "the glfd for directory %s",
                                uuid_utoa (fd->inode->gfid));
        }

        GF_FREE (sfd);

out:
        return 0;
}

int32_t
svs_flush (call_frame_t *frame, xlator_t *this,
           fd_t *fd, dict_t *xdata)
{
        int32_t          op_ret         = -1;
        int32_t          op_errno       = 0;
        int              ret            = -1;
        uint64_t         value          = 0;
        svs_inode_t     *inode_ctx      = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        inode_ctx = svs_inode_ctx_get (this, fd->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "inode context not found for"
                        " the inode %s", uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        ret = fd_ctx_get (fd, this, &value);
        if (ret < 0 && inode_ctx->type != SNAP_VIEW_ENTRY_POINT_INODE) {
                op_errno = EINVAL;
                gf_log (this->name, GF_LOG_WARNING,
                        "pfd is NULL on fd=%p", fd);
                goto out;
        }

        op_ret = 0;

out:
        STACK_UNWIND_STRICT (flush, frame, op_ret, op_errno, NULL);

        return 0;
}

int32_t
svs_release (xlator_t *this, fd_t *fd)
{
        svs_fd_t *sfd      = NULL;
        uint64_t          tmp_pfd  = 0;
        int               ret      = 0;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);

        ret = fd_ctx_del (fd, this, &tmp_pfd);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "pfd from fd=%p is NULL", fd);
                goto out;
        }

        sfd = (svs_fd_t *)(long)tmp_pfd;
        if (sfd->fd) {
                ret = glfs_close (sfd->fd);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to close "
                                "the glfd for %s",
                                uuid_utoa (fd->inode->gfid));
                }
        }

        GF_FREE (sfd);
out:
        return 0;
}

int32_t
svs_forget  (xlator_t *this, inode_t *inode)
{
        int       ret = -1;
        uint64_t  value = 0;
        svs_inode_t  *inode_ctx = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        ret = inode_ctx_del (inode, this, &value);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "failed to delte the inode "
                        "context of %s", uuid_utoa (inode->gfid));
                goto out;
        }

        inode_ctx = (svs_inode_t *)value;
        if (!inode_ctx)
                goto out;

        if (inode_ctx->snapname)
                GF_FREE (inode_ctx->snapname);

        GF_FREE (inode_ctx);

out:
        return 0;
}

int
svs_fill_readdir (xlator_t *this, gf_dirent_t *entries, size_t size, off_t off)
{
        gf_dirent_t             *entry          = NULL;
        svs_private_t           *priv           = NULL;
        int                     i               = 0;
        snap_dirent_t           *dirents        = NULL;
        int                     this_size       = 0;
        int                     filled_size     = 0;
        int                     count           = 0;

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO ("snap-view-daemon", entries, out);

        priv = this->private;
        GF_ASSERT (priv);

        /* create the dir entries */
        LOCK (&priv->snaplist_lock);
        {
                dirents = priv->dirents;

                for (i = off; i < priv->num_snaps; ) {
                        this_size = sizeof (gf_dirent_t) +
                                strlen (dirents[i].name) + 1;
                        if (this_size + filled_size > size )
                                goto unlock;

                        entry = gf_dirent_for_name (dirents[i].name);
                        if (!entry) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "failed to allocate dentry for %s",
                                        dirents[i].name);
                                goto unlock;
                        }

                        entry->d_off = i + 1;
                        /*
                         * readdir on the entry-point directory to the snapshot
                         * world, will return elements in the list of the
                         * snapshots as the directory entries. Since the entries
                         * returned are virtual entries which does not exist
                         * physically on the disk, pseudo inode numbers are
                         * generated.
                         */
                        entry->d_ino = i + 2*42;
                        entry->d_type = DT_DIR;
                        list_add_tail (&entry->list, &entries->list);
                        ++i;
                        count++;
                        filled_size += this_size;
                }
        }
unlock:
        UNLOCK (&priv->snaplist_lock);

out:
        return count;
}

int32_t
svs_glfs_readdir (xlator_t *this, glfs_fd_t *glfd, gf_dirent_t *entries,
                  int32_t *op_errno, struct iatt *buf, gf_boolean_t readdirplus,
                  size_t size)
{
        int              filled_size    = 0;
        int              this_size      = 0;
        int32_t          ret            = -1;
        int32_t          count          = 0;
        gf_dirent_t     *entry          = NULL;
        struct   dirent *dirents        = NULL;
        struct   dirent  de             = {0, };
        struct stat      statbuf        = {0, };
        off_t            in_case        = -1;

        GF_VALIDATE_OR_GOTO ("svs", this, out);
        GF_VALIDATE_OR_GOTO (this->name, glfd, out);
        GF_VALIDATE_OR_GOTO (this->name, entries, out);

        while (filled_size < size) {
                in_case = glfs_telldir (glfd);
                if (in_case == -1) {
                        gf_log (this->name, GF_LOG_ERROR, "telldir failed");
                        break;
                }

                if (readdirplus)
                        ret = glfs_readdirplus_r (glfd, &statbuf, &de,
                                                  &dirents);
                else
                        ret = glfs_readdir_r (glfd, &de, &dirents);

                if (ret == 0 && dirents != NULL) {
                        if (readdirplus)
                                this_size = max (sizeof (gf_dirent_t),
                                                 sizeof (gfs3_dirplist))
                                        + strlen (de.d_name) + 1;
                        else
                                this_size = sizeof (gf_dirent_t)
                                        + strlen (de.d_name) + 1;

                        if (this_size + filled_size > size) {
                                glfs_seekdir (glfd, in_case);
                                break;
                        }

                        entry = gf_dirent_for_name (de.d_name);
                        if (!entry) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "could not create gf_dirent "
                                        "for entry %s: (%s)",
                                        entry->d_name,
                                        strerror (errno));
                                break;
                        }
                        entry->d_off = glfs_telldir (glfd);
                        entry->d_ino = de.d_ino;
                        entry->d_type = de.d_type;
                        if (readdirplus) {
                                iatt_from_stat (buf, &statbuf);
                                entry->d_stat = *buf;
                        }
                        list_add_tail (&entry->list, &entries->list);

                        filled_size += this_size;
                        count++;
                } else if (ret == 0 && dirents == NULL) {
                        *op_errno = ENOENT;
                        break;
                } else if (ret != 0) {
                        *op_errno = errno;
                        break;
                }
                dirents = NULL;
                ret = -1;
        }

out:
        return count;
}

/* readdirp can be of 2 types.
   1) It can come on entry point directory where the list of snapshots
      is sent as dirents. In this case, the iatt structure is filled
      on the fly if the inode is not found for the entry or the inode
      context is NULL. Other wise if inode is found and inode context
      is there the iatt structure saved in the context is used.
   2) It can be on a directory in one of the snapshots. In this case,
      the readdirp call would have sent us a iatt structure. So the same
      structure is used with the exception that the gfid and the inode
      numbers will be newly generated and filled in.
*/
void
svs_readdirp_fill (xlator_t *this, inode_t *parent, svs_inode_t *parent_ctx,
                   gf_dirent_t *entry)
{
        inode_t                *inode          = NULL;
        uuid_t                  random_gfid    = {0,};
        struct  iatt            buf            = {0, };
        svs_inode_t            *inode_ctx      = NULL;

        GF_VALIDATE_OR_GOTO ("snapview-server", this, out);
        GF_VALIDATE_OR_GOTO (this->name, parent, out);
        GF_VALIDATE_OR_GOTO (this->name, parent_ctx, out);
        GF_VALIDATE_OR_GOTO (this->name, entry, out);

        if (!strcmp (entry->d_name, ".") || !strcmp (entry->d_name, ".."))
                goto out;

        inode = inode_grep (parent->table, parent, entry->d_name);
        if (inode) {
                entry->inode = inode;
                inode_ctx = svs_inode_ctx_get (this, inode);
                if (!inode_ctx) {
                        gf_uuid_copy (buf.ia_gfid, inode->gfid);
                        svs_iatt_fill (inode->gfid, &buf);
                        buf.ia_type = inode->ia_type;
                } else {
                        buf = inode_ctx->buf;
                }

                entry->d_ino = buf.ia_ino;

                if (parent_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE)
                        entry->d_stat = buf;
                else {
                        entry->d_stat.ia_ino = buf.ia_ino;
                        gf_uuid_copy (entry->d_stat.ia_gfid, buf.ia_gfid);
                }
        } else {

                if (parent_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                        inode = inode_new (parent->table);
                        entry->inode = inode;

                        /* If inode context allocation fails, then do not send
                         * the inode for that particular entry as part of
                         * readdirp response. Fuse and protocol/server will link
                         * the inodes in readdirp only if the entry contains
                         * inode in it.
                         */
                        inode_ctx = svs_inode_ctx_get_or_new (this, inode);
                        if (!inode_ctx) {
                                gf_log (this->name, GF_LOG_ERROR, "failed to "
                                        "allocate inode context for %s",
                                        entry->d_name);
                                inode_unref (entry->inode);
                                entry->inode = NULL;
                                goto out;
                        }

                        /* Generate virtual gfid for SNAPSHOT dir and
                         * update the statbuf
                         */
                        gf_uuid_generate (random_gfid);
                        gf_uuid_copy (buf.ia_gfid, random_gfid);
                        svs_fill_ino_from_gfid (&buf);
                        buf.ia_type = IA_IFDIR;
                        entry->d_ino = buf.ia_ino;
                        entry->d_stat = buf;
                        inode_ctx->buf = buf;
                        inode_ctx->type = SNAP_VIEW_SNAPSHOT_INODE;
                } else {
                        /* For files under snapshot world do not set
                         * entry->inode and reset statbuf (except ia_ino),
                         * so that FUSE/Kernel will send an explicit lookup.
                         * entry->d_stat contains the statbuf information
                         * of original file, so for NFS not to cache this
                         * information and to send explicit lookup, it is
                         * required to reset the statbuf.
                         * Virtual gfid for these files will be generated in the
                         * first lookup.
                         */
                        buf.ia_ino = entry->d_ino;
                        entry->d_stat = buf;
                }
        }

out:
        return;
}

/* In readdirp, though new inode is created along with the generation of
   new gfid, the inode context created will not contain the glfs_t instance
   for the filesystem it belongs to and the handle for it in the gfapi
   world. (handle is obtained only by doing the lookup call on the entry
   and doing lookup on each entry received as part of readdir call is a
   costly operation. So the fs and handle is NULL in the inode context
   and is filled in when lookup comes on that object.
*/
int32_t
svs_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t off, dict_t *dict)
{
        gf_dirent_t             entries;
        gf_dirent_t            *entry                           = NULL;
        struct  iatt            buf                             = {0, };
        int                     count                           = 0;
        int                     op_ret                          = -1;
        int                     op_errno                        = EINVAL;
        svs_inode_t            *parent_ctx                      = NULL;
        svs_fd_t               *svs_fd                          = NULL;

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, frame, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, unwind);

        INIT_LIST_HEAD (&entries.list);

        parent_ctx = svs_inode_ctx_get (this, fd->inode);
        if (!parent_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the inode "
                        "context for %s", uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }

        if (parent_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                LOCK (&fd->lock);
                {
                        count = svs_fill_readdir (this, &entries, size, off);
                }
                UNLOCK (&fd->lock);

                op_ret = count;

                list_for_each_entry (entry, &entries.list, list) {
                        svs_readdirp_fill (this, fd->inode, parent_ctx, entry);
                }

                goto unwind;
        } else {
                svs_fd = svs_fd_ctx_get_or_new (this, fd);
                if (!svs_fd) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to get the "
                                "fd context %s", uuid_utoa (fd->inode->gfid));
                        op_ret = -1;
                        op_errno = EBADFD;
                        goto unwind;
                }

                glfs_seekdir (svs_fd->fd, off);

                LOCK (&fd->lock);
                {
                        count = svs_glfs_readdir (this, svs_fd->fd, &entries,
                                                  &op_errno, &buf, _gf_true,
                                                  size);
                }
                UNLOCK (&fd->lock);

                op_ret = count;

                list_for_each_entry (entry, &entries.list, list) {
                        svs_readdirp_fill (this, fd->inode, parent_ctx, entry);
                }

                goto unwind;
        }

unwind:
        STACK_UNWIND_STRICT (readdirp, frame, op_ret, op_errno, &entries, dict);

        gf_dirent_free (&entries);

        return 0;
}

int32_t
svs_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t off, dict_t *xdata)
{
        gf_dirent_t    entries   = {{{0, }, }, };
        int            count     = 0;
        svs_inode_t   *inode_ctx = NULL;
        int            op_errno  = EINVAL;
        int            op_ret    = -1;
        svs_fd_t      *svs_fd    = NULL;
        glfs_fd_t     *glfd      = NULL;

        INIT_LIST_HEAD (&entries.list);

        GF_VALIDATE_OR_GOTO ("snap-view-server", this, unwind);
        GF_VALIDATE_OR_GOTO (this->name, frame, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd, unwind);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, unwind);

        inode_ctx = svs_inode_ctx_get (this, fd->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "inode context not found in "
                        "the inode %s", uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto unwind;
        }

        if (inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                LOCK (&fd->lock);
                {
                        count = svs_fill_readdir (this, &entries, size, off);
                }
                UNLOCK (&fd->lock);
        } else {
                svs_fd = svs_fd_ctx_get_or_new (this, fd);
                if (!svs_fd) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to get the "
                                "fd context %s", uuid_utoa (fd->inode->gfid));
                        op_ret = -1;
                        op_errno = EBADFD;
                        goto unwind;
                }

                glfd = svs_fd->fd;

                LOCK (&fd->lock);
                {
                        count = svs_glfs_readdir (this, glfd, &entries,
                                                  &op_errno, NULL, _gf_false,
                                                  size);
                }
                UNLOCK (&fd->lock);
        }

        op_ret = count;

unwind:
        STACK_UNWIND_STRICT (readdir, frame, op_ret, op_errno, &entries, xdata);

        gf_dirent_free (&entries);

        return 0;
}

/*
 * This function is mainly helpful for NFS. Till now NFS server was not linking
 * the inodes in readdirp, which caused problems when below operations were
 * performed.
 *
 * 1) ls -l in one of the snaopshots (snapview-server would generate gfids for
 *    each entry on the fly and link the inodes associated with those entries)
 * 2) NFS server upon getting readdirp reply would not link the inodes of the
 *    entries. But it used to generate filehandles for each entry and associate
 *    the gfid of that entry with the filehandle and send it as part of the
 *    reply to nfs client.
 * 3) NFS client would send the filehandle of one of those entries when some
 *    activity is done on it.
 * 4) NFS server would not be able to find the inode for the gfid present in the
 *    filehandle (as the inode was not linked) and would go for hard resolution
 *    by sending a lookup on the gfid by creating a new inode.
 * 5) snapview-client will not able to identify whether the inode is a real
 *    inode existing in the main volume or a virtual inode existing in the
 *    snapshots as there would not be any inode context.
 * 6) Since the gfid upon which lookup is sent is a virtual gfid which is not
 *    present in the disk, lookup would fail and the application would get an
 *    error.
 *
 * The above problem is fixed by the below commit which makes snapview server
 * more compatible with nfs server (1dea949cb60c3814c9206df6ba8dddec8d471a94).
 * But now because NFS server does inode linking in readdirp has introduced
 * the below issue.
 * In readdirp though snapview-server allocates inode contexts it does not
 * actually perform lookup on each entry it obtained in readdirp (as doing
 * a lookup via gfapi over the network for each entry would be costly).
 *
 * Till now it was not a problem with NFS server, as NFS was sending a lookup on
 * the gfid it got from NFS client, for which it was not able to find the right
 * inode. So snapview-server was able to get the fs instance (glfs_t) of the
 * snapshot volume to which the entry belongs to, and the handle for the entry
 * from the corresponding snapshot volume and fill those informations in the
 * inode context.
 *
 * But now, since NFS server is able to find the inode from the inode table for
 * the gfid it got from the NFS client, it wont send lookup. Rather it directly
 * sends the fop it received from the client. Now this causes problems for
 * snapview-server. Because for each fop snapview-server assumes that lookup has
 * been performed on that entry and the entry's inode context contains the
 * pointers for the fs instance and the handle to the entry in that fs. When NFS
 * server sends the fop and snapview-server finds that the fs instance and the
 * handle within the inode context are NULL it unwinds with EINVAL.
 *
 * So to handle this, if fs instance or handle within the inode context are
 * NULL, then do a lookup based on parent inode context's fs instance. And
 * unwind the results obtained as part of lookup
 */

int32_t
svs_get_handle (xlator_t *this, loc_t *loc, svs_inode_t *inode_ctx,
                int32_t *op_errno)
{
        svs_inode_t   *parent_ctx   = NULL;
        int            ret          = -1;
        inode_t       *parent       = NULL;
        struct iatt    postparent   = {0, };
        struct iatt    buf          = {0, };
        char           uuid1[64];

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        if (loc->path) {
                if (!loc->name || (loc->name && !strcmp (loc->name, ""))) {
                        loc->name = strrchr (loc->path, '/');
                        if (loc->name)
                                loc->name++;
                }
        }

        if (loc->parent)
                parent = inode_ref (loc->parent);
        else {
                parent = inode_find (loc->inode->table, loc->pargfid);
                if (!parent)
                        parent = inode_parent (loc->inode, NULL, NULL);
        }

        if (parent)
                parent_ctx = svs_inode_ctx_get (this, parent);

        if (!parent_ctx) {
                gf_log (this->name, GF_LOG_WARNING, "failed to get the parent "
                        "context for %s (%s)", loc->path,
                        uuid_utoa_r (loc->inode->gfid, uuid1));
                *op_errno = EINVAL;
                goto out;
        }

        if (parent_ctx) {
                if (parent_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE)
                        ret = svs_lookup_snapshot (this, loc, &buf,
                                                   &postparent, parent,
                                                   parent_ctx, op_errno);
                else
                        ret = svs_lookup_entry (this, loc, &buf,
                                                &postparent, parent,
                                                parent_ctx, op_errno);
        }

out:
        if (parent)
                inode_unref (parent);

        return ret;
}

int32_t
svs_stat (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        struct iatt    buf          = {0, };
        int32_t        op_errno     = EINVAL;
        int32_t        op_ret       = -1;
        svs_inode_t   *inode_ctx    = NULL;
        glfs_t        *fs           = NULL;
        glfs_object_t *object       = NULL;
        struct stat    stat         = {0, };
        int            ret          = -1;

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        /* Instead of doing the check of whether it is a entry point directory
           or not by checking the name of the entry and then deciding what
           to do, just check the inode context and decide what to be done.
        */

        inode_ctx = svs_inode_ctx_get (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "inode context not found for"
                        " %s", uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                svs_iatt_fill (loc->inode->gfid, &buf);
                op_ret = 0;
        }
        else {

                SVS_GET_INODE_CTX_INFO(inode_ctx, fs, object, this, loc, op_ret,
                                       op_errno, out);

                ret = glfs_h_stat (fs, object, &stat);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "glfs_h_stat on %s "
                                "(gfid: %s) failed", loc->name,
                                uuid_utoa (loc->inode->gfid));
                        op_ret = -1;
                        op_errno = errno;
                        goto out;
                }

                iatt_from_stat (&buf, &stat);
                gf_uuid_copy (buf.ia_gfid, loc->inode->gfid);
                svs_fill_ino_from_gfid (&buf);
                op_ret = ret;
        }

out:
        STACK_UNWIND_STRICT (stat, frame, op_ret, op_errno, &buf, xdata);
        return 0;
}

int32_t
svs_fstat (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        struct iatt    buf         = {0, };
        int32_t        op_errno    = EINVAL;
        int32_t        op_ret      = -1;
        svs_inode_t   *inode_ctx   = NULL;
        struct stat    stat        = {0, };
        int            ret         = -1;
        glfs_fd_t     *glfd        = NULL;
        svs_fd_t      *sfd         = NULL;

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, out);

        /* Instead of doing the check of whether it is a entry point directory
           or not by checking the name of the entry and then deciding what
           to do, just check the inode context and decide what to be done.
        */

        inode_ctx = svs_inode_ctx_get (this, fd->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "inode context not found for"
                        " the inode %s", uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                svs_iatt_fill (fd->inode->gfid, &buf);
                op_ret = 0;
        }
        else {
                sfd = svs_fd_ctx_get_or_new (this, fd);
                if (!sfd) {
                        gf_log (this->name, GF_LOG_ERROR, "failed to get the "
                                "fd context for %s",
                                uuid_utoa (fd->inode->gfid));
                        op_ret = -1;
                        op_errno = EBADFD;
                        goto out;
                }

                glfd = sfd->fd;
                ret = glfs_fstat (glfd, &stat);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "glfs_fstat on "
                                "gfid: %s failed", uuid_utoa (fd->inode->gfid));
                        op_ret = -1;
                        op_errno = errno;
                        goto out;
                }

                iatt_from_stat (&buf, &stat);
                gf_uuid_copy (buf.ia_gfid, fd->inode->gfid);
                svs_fill_ino_from_gfid (&buf);
                op_ret = ret;
        }

out:
        STACK_UNWIND_STRICT (fstat, frame, op_ret, op_errno, &buf, xdata);
        return 0;
}

int32_t
svs_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        struct statvfs buf          = {0, };
        int32_t        op_errno     = EINVAL;
        int32_t        op_ret       = -1;
        svs_inode_t   *inode_ctx    = NULL;
        glfs_t        *fs           = NULL;
        glfs_object_t *object       = NULL;
        int            ret          = -1;

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        /* Instead of doing the check of whether it is a entry point directory
           or not by checking the name of the entry and then deciding what
           to do, just check the inode context and decide what to be done.
        */
        inode_ctx = svs_inode_ctx_get (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "inode context not found for"
                        " %s", uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        SVS_GET_INODE_CTX_INFO(inode_ctx, fs, object, this, loc, op_ret,
                               op_errno, out);

        ret = glfs_h_statfs (fs, object, &buf);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "glfs_h_statvfs on %s "
                        "(gfid: %s) failed", loc->name,
                        uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = errno;
                goto out;
        }
        op_ret = ret;

out:
        STACK_UNWIND_STRICT (statfs, frame, op_ret, op_errno, &buf, xdata);
        return 0;
}


int32_t
svs_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
          fd_t *fd, dict_t *xdata)
{
        svs_inode_t   *inode_ctx = NULL;
        svs_fd_t      *sfd       = NULL;
        int32_t        op_ret    = -1;
        int32_t        op_errno  = EINVAL;
        glfs_fd_t     *glfd      = NULL;
        glfs_t        *fs        = NULL;
        glfs_object_t *object    = NULL;


        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        inode_ctx = svs_inode_ctx_get (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "inode context for %s "
                        "(gfid: %s) not found", loc->name,
                        uuid_utoa (loc->inode->gfid));
                goto out;
        }

        if (inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE)
                GF_ASSERT (0); // on entry point it should always be opendir

        SVS_GET_INODE_CTX_INFO(inode_ctx, fs, object, this, loc, op_ret,
                               op_errno, out);

        glfd = glfs_h_open (fs, object, flags);
        if (!glfd) {
                gf_log (this->name, GF_LOG_ERROR, "glfs_h_open on %s failed "
                        "(gfid: %s)", loc->name, uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = errno;
                goto out;
        }

        sfd = svs_fd_ctx_get_or_new (this, fd);
        if (!sfd) {
                gf_log (this->name, GF_LOG_ERROR, "failed to allocate fd "
                        "context for %s (gfid: %s)", loc->name,
                        uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = ENOMEM;
                glfs_close (glfd);
                goto out;
        }
        sfd->fd = glfd;

        op_ret = 0;

out:
        STACK_UNWIND_STRICT (open, frame, op_ret, op_errno, fd, NULL);
        return 0;
}

int32_t
svs_readv (call_frame_t *frame, xlator_t *this,
           fd_t *fd, size_t size, off_t offset, uint32_t flags, dict_t *xdata)
{
        int32_t                op_ret     = -1;
        int32_t                op_errno   = 0;
        svs_private_t         *priv       = NULL;
        struct iobuf          *iobuf      = NULL;
        struct iobref         *iobref     = NULL;
        struct iovec           vec        = {0,};
        svs_fd_t              *sfd        = NULL;
        int                    ret        = -1;
        struct stat            fstatbuf   = {0, };
        glfs_fd_t             *glfd       = NULL;
        struct iatt            stbuf      = {0, };

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, fd, out);
        GF_VALIDATE_OR_GOTO (this->name, fd->inode, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);

        sfd = svs_fd_ctx_get_or_new (this, fd);
        if (!sfd) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get the fd "
                        "context for %s", uuid_utoa (fd->inode->gfid));
                op_ret = -1;
                op_errno = EBADFD;
                goto out;
        }

        glfd = sfd->fd;

        iobuf = iobuf_get2 (this->ctx->iobuf_pool, size);
        if (!iobuf) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        ret = glfs_pread (glfd, iobuf->ptr, size, offset, 0);
        if (ret < 0) {
                op_ret = -1;
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "glfs_read failed (%s)",
                        strerror (op_errno));
                goto out;
        }

        vec.iov_base = iobuf->ptr;
        vec.iov_len  = ret;

        iobref = iobref_new ();

        iobref_add (iobref, iobuf);

        ret = glfs_fstat (glfd, &fstatbuf);
        if (ret) {
                op_ret = -1;
                op_errno = errno;
                gf_log (this->name, GF_LOG_ERROR, "glfs_fstat failed after "
                        "readv on %s", uuid_utoa (fd->inode->gfid));
                goto out;
        }

        iatt_from_stat (&stbuf, &fstatbuf);
        gf_uuid_copy (stbuf.ia_gfid, fd->inode->gfid);
        svs_fill_ino_from_gfid (&stbuf);

        /* Hack to notify higher layers of EOF. */
        if (!stbuf.ia_size || (offset + vec.iov_len) >= stbuf.ia_size)
                op_errno = ENOENT;

        op_ret = vec.iov_len;

out:

        STACK_UNWIND_STRICT (readv, frame, op_ret, op_errno,
                             &vec, 1, &stbuf, iobref, NULL);

        if (iobref)
                iobref_unref (iobref);
        if (iobuf)
                iobuf_unref (iobuf);

        return 0;
}

int32_t
svs_readlink (call_frame_t *frame, xlator_t *this,
              loc_t *loc, size_t size, dict_t *xdata)
{
        svs_inode_t     *inode_ctx = NULL;
        glfs_t          *fs        = NULL;
        glfs_object_t   *object    = NULL;
        int              op_ret    = -1;
        int              op_errno  = EINVAL;
        char            *buf       = NULL;
        struct iatt      stbuf     = {0, };
        int              ret       = -1;
        struct stat      stat      = {0, };

        GF_VALIDATE_OR_GOTO ("snap-view-daemon", this, out);
        GF_VALIDATE_OR_GOTO (this->name, frame, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        inode_ctx = svs_inode_ctx_get (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "failed to get inode context "
                        "for %s (gfid: %s)", loc->name,
                        uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        SVS_GET_INODE_CTX_INFO(inode_ctx, fs, object, this, loc, op_ret,
                               op_errno, out);

        ret = glfs_h_stat (fs, object, &stat);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "glfs_h_stat on %s "
                        "(gfid: %s) failed", loc->name,
                        uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = errno;
                goto out;
        }

        iatt_from_stat (&stbuf, &stat);
        gf_uuid_copy (stbuf.ia_gfid, loc->inode->gfid);
        svs_fill_ino_from_gfid (&stbuf);

        buf = alloca (size + 1);
        op_ret = glfs_h_readlink (fs, object, buf, size);
        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "readlink on %s failed "
                        "(gfid: %s)", loc->name, uuid_utoa (loc->inode->gfid));
                op_errno = errno;
                goto out;
        }

        buf[op_ret] = 0;

out:
        STACK_UNWIND_STRICT (readlink, frame, op_ret, op_errno, buf, &stbuf,
                             NULL);

        return 0;
}

int32_t
svs_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int mask,
            dict_t *xdata)
{
        int             ret          = -1;
        int32_t         op_ret       = -1;
        int32_t         op_errno     = EINVAL;
        glfs_t         *fs           = NULL;
        glfs_object_t  *object       = NULL;
        svs_inode_t    *inode_ctx    = NULL;
        gf_boolean_t    is_fuse_call = 0;
        int             mode         = 0;

        GF_VALIDATE_OR_GOTO ("svs", this, out);
        GF_VALIDATE_OR_GOTO (this->name, this->private, out);
        GF_VALIDATE_OR_GOTO (this->name, loc, out);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, out);

        inode_ctx = svs_inode_ctx_get (this, loc->inode);
        if (!inode_ctx) {
                gf_log (this->name, GF_LOG_ERROR, "inode context not found for"
                        " %s", uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        is_fuse_call = __is_fuse_call (frame);

        /*
         * For entry-point directory, set read and execute bits. But not write
         * permissions.
         */
        if (inode_ctx->type == SNAP_VIEW_ENTRY_POINT_INODE) {
                if (is_fuse_call) {
                        op_ret = 0;
                        op_errno = 0;
                } else {
                        op_ret = 0;
                        mode |= POSIX_ACL_READ;
                        mode |= POSIX_ACL_EXECUTE;
                        op_errno = mode;
                }
                goto out;
        }


        SVS_GET_INODE_CTX_INFO(inode_ctx, fs, object, this, loc, op_ret,
                               op_errno, out);

        /* The actual posix_acl xlator does acl checks differently for
           fuse and nfs. So set frame->root->pid as fspid of the syncop
           if the call came from nfs
        */
        if (!is_fuse_call) {
                syncopctx_setfspid (&frame->root->pid);
                syncopctx_setfsuid (&frame->root->uid);
                syncopctx_setfsgid (&frame->root->gid);
                syncopctx_setfsgroups (frame->root->ngrps,
                                       frame->root->groups);
        }

        ret = glfs_h_access (fs, object, mask);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "failed to access %s "
                        "(gfid: %s)", loc->path, uuid_utoa (loc->inode->gfid));
                op_ret = -1;
                op_errno = errno;
                goto out;
        }

        op_ret = 0;
        op_errno = ret;

out:

        STACK_UNWIND_STRICT (access, frame, op_ret, op_errno, NULL);
        return 0;
}


int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_svs_mt_end + 1);

        if (ret != 0) {
                gf_log (this->name, GF_LOG_WARNING, "Memory accounting"
                        " init failed");
                return ret;
        }

        return ret;
}

int32_t
init (xlator_t *this)
{
        svs_private_t   *priv           = NULL;
        int             ret             = -1;

        /* This can be the top of graph in certain cases */
        if (!this->parents) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "dangling volume. check volfile ");
        }

        priv = GF_CALLOC (1, sizeof (*priv), gf_svs_mt_priv_t);
        if (!priv)
                goto out;

        this->private = priv;

        GF_OPTION_INIT ("volname", priv->volname, str, out);
        LOCK_INIT (&priv->snaplist_lock);

        LOCK (&priv->snaplist_lock);
        {
                priv->num_snaps = 0;
        }
        UNLOCK (&priv->snaplist_lock);

        /* What to do here upon failure? should init be failed or succeed? */
        /* If succeeded, then dynamic management of snapshots will not */
        /* happen.*/
        ret = svs_mgmt_init (this);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "failed to initiate the "
                        "mgmt rpc callback for svs. Dymamic management of the"
                        "snapshots will not happen");
                goto out;
        }

        /* get the list of snaps first to return to client xlator */
        ret = svs_get_snapshot_list (this);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Error initializing snaplist infrastructure");
                ret = -1;
                goto out;
        }

        ret = 0;

out:
        if (ret && priv) {
                LOCK_DESTROY (&priv->snaplist_lock);
                GF_FREE (priv->dirents);
                GF_FREE (priv);
        }

        return ret;
}

void
fini (xlator_t *this)
{
        svs_private_t   *priv   = NULL;
        glusterfs_ctx_t *ctx    = NULL;
        int             ret     = 0;

        GF_ASSERT (this);
        priv = this->private;
        this->private = NULL;
        ctx = this->ctx;
        if (!ctx)
                gf_log (this->name, GF_LOG_ERROR,
                        "Invalid ctx found");

        if (priv) {
                ret = LOCK_DESTROY (&priv->snaplist_lock);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Could not destroy mutex snaplist_lock");
                }

                if (priv->dirents) {
                        GF_FREE (priv->dirents);
                }

                if (priv->rpc) {
                        /* cleanup the saved-frames before last unref */
                        rpc_clnt_connection_cleanup (&priv->rpc->conn);
                        rpc_clnt_unref (priv->rpc);
                }

                GF_FREE (priv);
        }

        return;
}

struct xlator_fops fops = {
        .lookup     = svs_lookup,
        .stat       = svs_stat,
        .statfs     = svs_statfs,
        .opendir    = svs_opendir,
        .readdirp   = svs_readdirp,
        .readdir    = svs_readdir,
        .open       = svs_open,
        .readv      = svs_readv,
        .flush      = svs_flush,
        .fstat      = svs_fstat,
        .getxattr   = svs_getxattr,
        .access     = svs_access,
        .readlink   = svs_readlink,
        /* entry fops */
};

struct xlator_cbks cbks = {
        .release  = svs_release,
        .releasedir = svs_releasedir,
        .forget     = svs_forget,
};

struct volume_options options[] = {
        { .key  = {"volname"},
          .type = GF_OPTION_TYPE_STR,
        },
        { .key  = {NULL} },
};
