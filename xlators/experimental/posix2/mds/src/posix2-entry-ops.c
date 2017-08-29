/*
   Copyright (c) 2015-2017 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

/**
 * ZeroFile store directory operations
 */

#include "syscall.h"
#include "posix2-helpers.h"
#include "common-utils.h"
#include "posix.h"

static int32_t
posix2_named_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        int32_t      ret      = 0;
        int32_t      op_ret   = 0;
        int32_t      op_errno = 0;
        int          parlen   = 0;
        char        *parpath  = NULL;
        struct iatt  buf      = {0,};
        struct iatt  postbuf  = {0,};
        struct posix_private *priv = NULL;

        priv = this->private;

        parlen = posix2_handle_length (priv->base_path_length);
        parpath = alloca (parlen);

        errno = EINVAL;

        /* make parent handle */
        parlen = posix2_make_handle (loc->pargfid, priv->base_path, parpath,
                                     parlen);
        if (parlen <= 0)
                goto unwind_err;

        /* lookup entry */
        ret = posix2_handle_entry (this, parpath, loc->name, &buf);
        if (ret) {
                if (errno != EREMOTE)
                        goto unwind_err;
                op_ret = -1;
                op_errno = errno;
        }

        ret = posix2_istat_path (this, loc->pargfid, parpath, &postbuf,
                                 _gf_true);
        if (ret)
                goto unwind_err;

        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno,
                             loc->inode, &buf, NULL, &postbuf);
        return 0;

 unwind_err:
        STACK_UNWIND_STRICT (lookup, frame, -1, errno, NULL, NULL, NULL, NULL);
        return 0;
}

/**
 * Create the root inode. There's no need to link name as it's the beasty
 * root ("/").
 */
static int32_t
posix2_create_inode0x1 (xlator_t *this, char *entry, uuid_t rootgfid,
                        struct iatt *stbuf)
{
        int32_t ret = 0;
        mode_t mode = (0700 | S_IFDIR);

        ret = posix2_create_inode (this, entry, 0, mode);
        if (ret)
                goto error_return;

        /* we just created it, but still.. */
        ret = posix2_istat_path (this, rootgfid, entry, stbuf, _gf_true);
        /**
         * If lookup() fails now, we return without doing doing any cleanups as
         * we haven't left anything half-baked.
         */
        if (ret)
                goto error_return;
        return 0;

 error_return:
        return -1;
}

/* TODO: Enable with aux GFID support added
static int32_t
posix2_handle_auxlookup (call_frame_t *frame,
                         xlator_t *this, loc_t *loc, uuid_t auxgfid)
{
        struct iatt auxbuf = {0,};

        gf_uuid_copy (auxbuf.ia_gfid, auxgfid);

        auxbuf.ia_type = IA_IFDIR;
        auxbuf.ia_nlink = 1;
        auxbuf.ia_uid = 0;
        auxbuf.ia_gid = 0;

        posix2_fill_ino_from_gfid (this, &auxbuf);

        STACK_UNWIND_STRICT (lookup, frame,
                             0, 0, loc->inode, &auxbuf, NULL, NULL);
        return 0;
}*/

static int32_t
posix2_nameless_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        int32_t      ret      = 0;
        int          entrylen = 0;
        char        *entry    = NULL;
        uuid_t       tgtuuid  = {0,};
        struct iatt  buf      = {0,};
        struct iatt  pbuf     = {0,};
        struct posix_private *priv = NULL;

        priv = this->private;

        entrylen = posix2_handle_length (priv->base_path_length);
        entry = alloca (entrylen);

        if (!gf_uuid_is_null (loc->gfid))
                gf_uuid_copy (tgtuuid, loc->gfid);
        else
                gf_uuid_copy (tgtuuid, loc->inode->gfid);

        /* TODO: Enable with aux GFID support added
        if (__is_auxilary_gfid (tgtuuid))
                return posix2_handle_auxlookup (frame, this, loc, tgtuuid); */

        errno = EINVAL;
        entrylen = posix2_make_handle (tgtuuid, priv->base_path, entry,
                                       entrylen);
        if (entrylen <= 0)
                goto unwind_err;

        ret = posix2_istat_path (this, tgtuuid, entry, &buf, _gf_false);
        if (ret < 0) {
                if (errno != ENOENT)
                        goto unwind_err;
                if ((errno == ENOENT) && __is_root_gfid (tgtuuid))
                        ret = posix2_create_inode0x1 (this, entry, tgtuuid,
                                                      &buf);
                if (ret) {
                        if (errno == ENOENT)
                                errno = ESTALE;
                        goto unwind_err;
                }
        }

        /* TODO: Things above the stack expect valid pbuf pointers, even when
        data in it is filled incorrectly, continue the behaviour and revisit
        later. see syncop_lookup_cbk dereferencing parent iatt without checks */
        STACK_UNWIND_STRICT (lookup, frame, 0, 0, loc->inode, &buf,
                             NULL, &pbuf);
        return 0;

 unwind_err:
        STACK_UNWIND_STRICT (lookup, frame, -1,
                             errno, NULL, NULL, NULL, NULL);
        return 0;
}

int32_t
posix2_lookup (call_frame_t *frame,
               xlator_t *this, loc_t *loc, dict_t *xdata)
{
        int32_t ret = 0;

        if (posix2_lookup_is_nameless (loc))
                ret = posix2_nameless_lookup (frame, this, loc);
        else
                ret = posix2_named_lookup (frame, this, loc);

        return ret;
}

static int32_t
posix2_open_and_save (xlator_t *this, fd_t *fd, char *entry, int32_t flags)
{
        int openfd = -1;
        int32_t ret = 0;

        openfd = open (entry, flags);
        if (openfd < 0)
                goto error_return;
        ret = posix2_save_openfd (this, fd, openfd, flags);
        if (ret)
                goto closefd;
        return 0;

 closefd:
        sys_close (openfd);
 error_return:
        return -1;
}

int32_t
posix2_open_inode (xlator_t *this,
                   char *export, uuid_t gfid, fd_t *fd, int32_t flags)
{
        int32_t  ret      = 0;
        int      entrylen = 0;
        char    *entry    = NULL;

        entrylen = posix2_handle_length (strlen(export));
        entry = alloca (entrylen);

        errno = EINVAL;
        entrylen = posix2_make_handle (gfid, export, entry, entrylen);
        if (entrylen <= 0)
                goto error_return;

        ret = posix2_open_and_save (this, fd, entry, flags & ~O_CREAT);
        if (ret)
                goto error_return;
        return 0;

 error_return:
        return -1;
}

static int32_t
posix2_create_namei (xlator_t *this, char *parpath,
                     loc_t *loc, fd_t *fd, int32_t flags,
                     mode_t mode, dict_t *xdata, struct iatt *stbuf)
{
        int32_t         ret      = 0;
        int             entrylen = 0;
        char           *entry    = NULL;
        char           *export   = NULL;
        void           *uuidreq  = NULL;
        uuid_t          gfid     = {0,};
        struct posix_private *priv = NULL;

        priv = this->private;
        export = priv->base_path;

        ret = dict_get_ptr (xdata, "gfid-req", &uuidreq);
        if (ret) {
                errno = EINVAL;
                goto error_return;
        }
        gf_uuid_copy (gfid, uuidreq);

        entrylen = posix2_handle_length (priv->base_path_length);
        entry = alloca (entrylen);

        /* create inode */
        entrylen = posix2_make_handle (gfid, export, entry, entrylen);
        if (entrylen <= 0) {
                errno = EINVAL;
                goto error_return;
        }
        ret = posix2_create_inode (this, entry, flags, mode);
        if (ret)
                goto error_return;

        /* link name to inode */
        /* TODO: a crash here would leave behind an orphan inode? is that RIOs
        responsibility to clean? I would assume not, as this is an atomic
        operation for RIO, it will send *a* FOP (create) downward, and hence
        has no transaction to remember. I would assume we hence need to add
        reclaim of 'such' orphan inodes in POSIX2 */
        ret = posix2_link_inode (this, parpath, loc->name, gfid);
        if (ret)
                goto purge_inode;

        ret = posix2_istat_path (this, gfid, entry, stbuf, _gf_false);
        if (ret)
                goto purge_entry;

        ret = posix2_open_and_save (this, fd, entry, flags);
        if (ret)
                goto purge_entry;

        return 0;

 purge_entry:
        /* TODO: entry purging */
 purge_inode:
        /* TODO: inode purging */
 error_return:
        return -1;
}

/**
 * Create an inode for a given object and acquire an active reference
 * on it. This routine serializes multiple creat() calls for a given
 * parent inode. Parallel creates for the name entry converge at the
 * correct inode and acquire an fd reference.
 */
int32_t
posix2_do_namei (xlator_t *this,
                 char *parpath, loc_t *loc, fd_t *fd,
                 int32_t flags, mode_t mode, dict_t *xdata, struct iatt *stbuf)
{
        int32_t         ret    = 0;
        char           *export = NULL;
        inode_t        *parent = NULL;
        struct posix_private *priv = NULL;

        priv = this->private;
        export = priv->base_path;
        parent = loc->parent;

        /* TODO: Need to check if we need a more granular lock for this */
        LOCK (&parent->lock);
        {
                ret = posix2_handle_entry (this, parpath, loc->name, stbuf);
                if (ret < 0) {
                        if (errno != ENOENT)
                                goto unblock;
                        /**
                         * EREMOTE is handled by rio as a special case by
                         * looking up the remote object in another MDS. So,
                         * we're left with only two cases:
                         *  - missing entry and inode (ENOENT)
                         *  - valid entry and inode
                         */
                }

                if (ret)
                        ret = posix2_create_namei (this, parpath, loc, fd,
                                                   flags, mode, xdata, stbuf);
                else
                        ret = posix2_open_inode (this, export, stbuf->ia_gfid,
                                                 fd, flags);
        }
 unblock:
        UNLOCK (&parent->lock);

        return ret;
}

int32_t
posix2_create (call_frame_t *frame,
               xlator_t *this, loc_t *loc, int32_t flags,
               mode_t mode, mode_t umask, fd_t *fd, dict_t *xdata)
{
        int32_t ret = 0;
        int parlen = 0, retlen;
        char *parpath = NULL;
        struct iatt buf = {0,};
        struct iatt prebuf = {0,};
        struct iatt postbuf = {0,};
        struct posix_private *priv = NULL;

        priv = this->private;

        parlen = posix2_handle_length (priv->base_path_length);
        parpath = alloca (parlen);

        errno = EINVAL;
        /* parent handle */
        retlen = posix2_make_handle (loc->pargfid, priv->base_path, parpath,
                                     parlen);
        if (parlen != retlen) {
                errno = EINVAL;
                goto unwind_err;
        }

        /* parent prebuf */
        ret = posix2_istat_path (this, loc->pargfid, parpath,
                                 &prebuf, _gf_true);
        if (ret)
                goto unwind_err;

        ret = posix2_do_namei (this, parpath, loc, fd, flags, mode,
                               xdata, &buf);
        if (ret)
                goto unwind_err;

        /* parent postbuf */
        ret = posix2_istat_path (this, loc->pargfid, parpath,
                                 &postbuf, _gf_true);
        if (ret)
                goto unwind_err;

        STACK_UNWIND_STRICT (create, frame, 0, 0,
                             fd, loc->inode, &buf, &prebuf, &postbuf, xdata);
        return 0;

 unwind_err:
        STACK_UNWIND_STRICT (create, frame,
                             -1, errno, fd, loc->inode, NULL, NULL, NULL, NULL);
        return 0;
}

int32_t
zfstore_namelink (call_frame_t *frame,
                  xlator_t *this, loc_t *loc, dict_t *xdata)
{
        int32_t         ret     = 0;
        uuid_t          gfid    = {0,};
        void           *uuidreq = NULL;
        void           *parpath = NULL;
        int             parlen  = 0;
        struct iatt     prebuf  = {0,};
        struct iatt     postbuf = {0,};
        struct posix_private *priv = NULL;

        priv = this->private;

        errno = EINVAL;
        ret = dict_get_ptr (xdata, "gfid-req", &uuidreq);
        if (ret)
                goto unwind_err;
        gf_uuid_copy (gfid, uuidreq);

        parlen = posix2_handle_length (priv->base_path_length);
        parpath = alloca (parlen);

        /* parent handle */
        parlen = posix2_make_handle (loc->pargfid, priv->base_path,
                                     parpath, parlen);
        if (parlen <= 0)
                goto unwind_err;

        /* parent prebuf */
        ret = posix2_istat_path (this, loc->pargfid, parpath,
                                 &prebuf, _gf_true);
        if (ret)
                goto unwind_err;

        ret = posix2_link_inode (this, parpath, loc->name, gfid);
        if (ret)
                goto unwind_err;

        /* parent postbuf */
        ret = posix2_istat_path (this, loc->pargfid, parpath,
                                 &postbuf, _gf_true);
        if (ret)
                goto unwind_err;

        /* TODO: uncomment when namelink is added as a FOP
        STACK_UNWIND_STRICT (namelink, frame, 0, 0, &prebuf, &postbuf, NULL);
        */
        return 0;

 unwind_err:
        /* TODO: uncomment when namelink is added as a FOP
        STACK_UNWIND_STRICT (namelink, frame, -1, errno, NULL, NULL, NULL);
        */
        return 0;
}

int32_t
posix2_icreate (call_frame_t *frame,
                xlator_t *this, loc_t *loc, mode_t mode, dict_t *xdata)
{
        int32_t         ret      = 0;
        uuid_t          gfid     = {0,};
        void           *uuidreq  = NULL;
        int             entrylen = 0;
        char           *entry    = NULL;
        struct iatt     stbuf    = {0,};
        struct posix_private *priv = NULL;

        priv = this->private;

        errno = EINVAL;
        ret = dict_get_ptr (xdata, "gfid-req", &uuidreq);
        if (ret)
                goto unwind_err;
        gf_uuid_copy (gfid, uuidreq);

        entrylen = posix2_handle_length (priv->base_path_length);
        entry = alloca (entrylen);

        entrylen = posix2_make_handle (gfid, priv->base_path, entry, entrylen);
        if (entrylen <= 0)
                goto unwind_err;

        ret = posix2_create_inode (this, entry, 0, mode);
        if (ret)
                goto unwind_err;

        ret = posix2_istat_path (this, gfid, entry, &stbuf, _gf_false);
        if (ret)
                goto purge_entry;

        /* TODO: uncomment when icreate is added as a FOP
        STACK_UNWIND_STRICT (icreate, frame, 0, 0, loc->inode, &stbuf, NULL);
        */
        return 0;

 purge_entry:
 unwind_err:
        /* TODO: uncomment when icreate is added as a FOP
        STACK_UNWIND_STRICT (icreate, frame, -1, errno, NULL, NULL, NULL);
        */
        return 0;
}
