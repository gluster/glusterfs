/*
   Copyright (c) 2006-2017 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#define __XOPEN_SOURCE 500

/* for SEEK_HOLE and SEEK_DATA */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <openssl/md5.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <ftw.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/uio.h>
#include <unistd.h>
#include <ftw.h>

#ifndef GF_BSD_HOST_OS
#include <alloca.h>
#endif /* GF_BSD_HOST_OS */

#ifdef HAVE_LINKAT
#include <fcntl.h>
#endif /* HAVE_LINKAT */

#include "glusterfs.h"
#include "checksum.h"
#include "dict.h"
#include "logging.h"
#include "posix.h"
#include "posix-handle.h"
#include "xlator.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat-errno.h"
#include "compat.h"
#include "byte-order.h"
#include "syscall.h"
#include "statedump.h"
#include "locking.h"
#include "timer.h"
#include "glusterfs3-xdr.h"
#include "hashfn.h"
#include "posix-aio.h"
#include "glusterfs-acl.h"
#include "posix-messages.h"
#include "posix-metadata.h"
#include "events.h"
#include "posix-gfid-path.h"
#include "compat-uuid.h"
#include "syncop.h"

extern char *marker_xattrs[];
#define ALIGN_SIZE 4096

#undef HAVE_SET_FSID
#ifdef HAVE_SET_FSID

#define DECLARE_OLD_FS_ID_VAR uid_t old_fsuid; gid_t old_fsgid;

#define SET_FS_ID(uid, gid) do {                \
                old_fsuid = setfsuid (uid);     \
                old_fsgid = setfsgid (gid);     \
        } while (0)

#define SET_TO_OLD_FS_ID() do {                 \
                setfsuid (old_fsuid);           \
                setfsgid (old_fsgid);           \
        } while (0)

#else

#define DECLARE_OLD_FS_ID_VAR
#define SET_FS_ID(uid, gid)
#define SET_TO_OLD_FS_ID()

#endif

gf_boolean_t
posix_symlinks_match (xlator_t *this, loc_t *loc, uuid_t gfid)
{
        struct posix_private *priv = NULL;
        char linkname_actual[PATH_MAX] = {0,};
        char linkname_expected[PATH_MAX] = {0};
        char *dir_handle = NULL;
        ssize_t len = 0;
        size_t handle_size = 0;
        gf_boolean_t ret = _gf_false;

        priv = this->private;
        handle_size = POSIX_GFID_HANDLE_SIZE(priv->base_path_length);
        dir_handle = alloca0 (handle_size);

        snprintf (linkname_expected, PATH_MAX, "../../%02x/%02x/%s/%s",
                  loc->pargfid[0], loc->pargfid[1], uuid_utoa (loc->pargfid),
                  loc->name);

        MAKE_HANDLE_GFID_PATH (dir_handle, this, gfid, NULL);
        len = sys_readlink (dir_handle, linkname_actual, PATH_MAX);
        if (len < 0 || len == PATH_MAX) {
                if (len == PATH_MAX) {
                        errno = EINVAL;
                }

                if (errno != ENOENT) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_LSTAT_FAILED, "readlink[%s] failed",
                                dir_handle);
                }
                goto out;
        }
        linkname_actual[len] = '\0';

        if (!strcmp (linkname_actual, linkname_expected))
                ret = _gf_true;

out:
        return ret;
}

dict_t*
posix_dict_set_nlink (dict_t *req, dict_t *res, int32_t nlink)
{
        int   ret  =  -1;

        if (req == NULL || !dict_get (req, GF_REQUEST_LINK_COUNT_XDATA))
                goto out;

        if (res == NULL)
                res = dict_new ();
        if (res == NULL)
                goto out;

        ret = dict_set_uint32 (res, GF_RESPONSE_LINK_COUNT_XDATA, nlink);
        if (ret == -1)
                gf_msg ("posix", GF_LOG_WARNING, 0, P_MSG_SET_XDATA_FAIL,
                        "Failed to set GF_RESPONSE_LINK_COUNT_XDATA");
out:
        return res;
}

/* Regular fops */

int32_t
posix_lookup (call_frame_t *frame, xlator_t *this,
              loc_t *loc, dict_t *xdata)
{
        struct iatt buf                = {0, };
        int32_t     op_ret             = -1;
        int32_t     entry_ret          = 0;
        int32_t     op_errno           = 0;
        dict_t *    xattr              = NULL;
        char *      real_path          = NULL;
        char *      par_path           = NULL;
        char        *gfid_path         = NULL;
        uuid_t      gfid               = {0};
        struct iatt postparent         = {0,};
        struct stat statbuf            = {0};
        int32_t     gfidless           = 0;
        char        *pgfid_xattr_key   = NULL;
        int32_t     nlink_samepgfid    = 0;
        struct  posix_private *priv    = NULL;
        posix_inode_ctx_t *ctx         = NULL;
        int         ret                = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;

        /* The Hidden directory should be for housekeeping purpose and it
           should not get any gfid on it */
        if (__is_root_gfid (loc->pargfid) && loc->name
            && (strcmp (loc->name, GF_HIDDEN_PATH) == 0)) {
                gf_msg (this->name, GF_LOG_WARNING, EPERM,
                        P_MSG_LOOKUP_NOT_PERMITTED, "Lookup issued on %s,"
                        " which is not permitted", GF_HIDDEN_PATH);
                op_errno = EPERM;
                op_ret = -1;
                goto out;
        }

        op_ret = dict_get_int32 (xdata, GF_GFIDLESS_LOOKUP, &gfidless);
        op_ret = -1;
        if (gf_uuid_is_null (loc->pargfid) || (loc->name == NULL)) {
                /* nameless lookup */
                MAKE_INODE_HANDLE (real_path, this, loc, &buf);
        } else {
                MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, &buf);

                if (gf_uuid_is_null (loc->inode->gfid)) {
                        op_ret = posix_gfid_heal (this, real_path, loc, xdata);
                        if (op_ret < 0) {
                                op_errno = -op_ret;
                                op_ret = -1;
                                goto out;
                        }
                        MAKE_ENTRY_HANDLE (real_path, par_path, this,
                                           loc, &buf);
                }
        }

        op_errno = errno;

        if (op_ret == -1) {
                if (op_errno != ENOENT) {
                        gf_msg (this->name, GF_LOG_WARNING, op_errno,
                                P_MSG_LSTAT_FAILED,
                                "lstat on %s failed",
                                real_path ? real_path : "null");
                }
                entry_ret = -1;
                if (loc_is_nameless(loc)) {
                        if (!op_errno)
                                op_errno = ESTALE;
                        loc_gfid (loc, gfid);
                        MAKE_HANDLE_ABSPATH (gfid_path, this, gfid);
                        ret = sys_stat(gfid_path, &statbuf);
                        if (ret == 0 && ((statbuf.st_mode & S_IFMT) == S_IFDIR))
                                /*Don't unset if it was a symlink to a dir.*/
                                goto parent;
                        ret = sys_lstat(gfid_path, &statbuf);
                        if (ret == 0 && statbuf.st_nlink == 1) {
                                gf_msg (this->name, GF_LOG_WARNING, op_errno,
                                        P_MSG_HANDLE_DELETE, "Found stale gfid "
                                        "handle %s, removing it.", gfid_path);
                                posix_handle_unset (this, gfid, NULL);
                        }
                }
                goto parent;
        }

        if (xdata && (op_ret == 0)) {
                xattr = posix_xattr_fill (this, real_path, loc, NULL, -1, xdata,
                                          &buf);

                posix_cs_maintenance (this, NULL, loc, NULL, &buf, real_path,
                                   xdata, &xattr, _gf_true);

                if (dict_get (xdata, GF_CLEAN_WRITE_PROTECTION)) {
                        ret = sys_lremovexattr (real_path,
                                                GF_PROTECT_FROM_EXTERNAL_WRITES);
                        if (ret == -1 && (errno != ENODATA && errno != ENOATTR))
                                gf_msg (this->name, GF_LOG_ERROR,
                                        P_MSG_XATTR_NOT_REMOVED, errno,
                                        "removexattr failed. key %s path %s",
                                        GF_PROTECT_FROM_EXTERNAL_WRITES,
                                        loc->path);
                }
        }

        if (priv->update_pgfid_nlinks) {
                if (!gf_uuid_is_null (loc->pargfid) && !IA_ISDIR (buf.ia_type)) {
                        MAKE_PGFID_XATTR_KEY (pgfid_xattr_key,
                                              PGFID_XATTR_KEY_PREFIX,
                                              loc->pargfid);

                        op_ret = posix_inode_ctx_get_all (loc->inode, this,
                                                          &ctx);
                        if (op_ret < 0) {
                                op_errno = ENOMEM;
                                goto out;
                        }

                        pthread_mutex_lock (&ctx->pgfid_lock);
                        {
                                SET_PGFID_XATTR_IF_ABSENT (real_path,
                                                           pgfid_xattr_key,
                                                           nlink_samepgfid,
                                                           XATTR_CREATE, op_ret,
                                                           this, unlock);
                        }
unlock:
                        pthread_mutex_unlock (&ctx->pgfid_lock);
                }
        }

parent:
        if (par_path) {
                op_ret = posix_pstat (this, loc->parent, loc->pargfid,
                                      par_path, &postparent, _gf_false);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_LSTAT_FAILED, "post-operation lstat on"
                                " parent %s failed", par_path);
                        if (op_errno == ENOENT)
                                /* If parent directory is missing in a lookup,
                                   errno should be ESTALE (bad handle) and not
                                   ENOENT (missing entry)
                                */
                                op_errno = ESTALE;
                        goto out;
                }
        }

        op_ret = entry_ret;
out:
        if (!op_ret && !gfidless && gf_uuid_is_null (buf.ia_gfid)) {
                gf_msg (this->name, GF_LOG_ERROR, ENODATA, P_MSG_NULL_GFID,
                        "buf->ia_gfid is null for "
                        "%s", (real_path) ? real_path: "");
                op_ret = -1;
                op_errno = ENODATA;
        }

        if (op_ret == 0)
                op_errno = 0;
        STACK_UNWIND_STRICT (lookup, frame, op_ret, op_errno,
                             (loc)?loc->inode:NULL, &buf, xattr, &postparent);

        if (xattr)
                dict_unref (xattr);

        return 0;
}

int
posix_mknod (call_frame_t *frame, xlator_t *this,
             loc_t *loc, mode_t mode, dev_t dev, mode_t umask, dict_t *xdata)
{
        int                   tmp_fd          = 0;
        int32_t               op_ret          = -1;
        int32_t               op_errno        = 0;
        char                 *real_path       = 0;
        char                 *par_path        = 0;
        struct iatt           stbuf           = { 0, };
        struct posix_private *priv            = NULL;
        gid_t                 gid             = 0;
        struct iatt           preparent       = {0,};
        struct iatt           postparent      = {0,};
        uuid_t                uuid_req        = {0,};
        int32_t               nlink_samepgfid = 0;
        char                 *pgfid_xattr_key = NULL;
        gf_boolean_t          entry_created   = _gf_false, gfid_set = _gf_false;
        gf_boolean_t          linked          = _gf_false;
        gf_loglevel_t         level           = GF_LOG_NONE;
        mode_t                mode_bit        = 0;
        posix_inode_ctx_t     *ctx                = NULL;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);
        GFID_NULL_CHECK_AND_GOTO (frame, this, loc, xdata, op_ret, op_errno,
                                  out);
        MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, NULL);

        mode_bit = (priv->create_mask & mode) | priv->force_create_mode;
        mode = posix_override_umask (mode, mode_bit);

        gid = frame->root->gid;

        SET_FS_ID (frame->root->uid, gid);
        DISK_SPACE_CHECK_AND_GOTO (frame, priv, xdata, op_ret, op_errno, out);
        if (!real_path || !par_path) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }


        op_ret = posix_pstat (this, loc->parent, loc->pargfid, par_path,
                              &preparent, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "pre-operation lstat on parent of %s failed",
                        real_path);
                goto out;
        }

        if (preparent.ia_prot.sgid) {
                gid = preparent.ia_gid;
        }

        /* Check if the 'gfid' already exists, because this mknod may be an
           internal call from distribute for creating 'linkfile', and that
           linkfile may be for a hardlinked file */
        if (dict_get (xdata, GLUSTERFS_INTERNAL_FOP_KEY)) {
                dict_del (xdata, GLUSTERFS_INTERNAL_FOP_KEY);
                op_ret = dict_get_gfuuid (xdata, "gfid-req", &uuid_req);
                if (op_ret) {
                        gf_msg_debug (this->name, 0, "failed to get the gfid from "
                                "dict for %s", loc->path);
                        goto real_op;
                }
                op_ret = posix_create_link_if_gfid_exists (this, uuid_req,
                                                           real_path,
                                                           loc->inode->table);
                if (!op_ret) {
                        linked = _gf_true;
                        goto post_op;
                }
        }

real_op:
#ifdef __NetBSD__
        if (S_ISFIFO(mode))
                op_ret = mkfifo (real_path, mode);
        else
#endif /* __NetBSD__ */
        op_ret = sys_mknod (real_path, mode, dev);

        if (op_ret == -1) {
                op_errno = errno;
                if ((op_errno == EINVAL) && S_ISREG (mode)) {
                        /* Over Darwin, mknod with (S_IFREG|mode)
                           doesn't work */
                        tmp_fd = sys_creat (real_path, mode);
                        if (tmp_fd == -1) {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        P_MSG_CREATE_FAILED, "create failed on"
                                        "%s", real_path);
                                goto out;
                        }
                        sys_close (tmp_fd);
                } else {
                        if (op_errno == EEXIST)
                                level = GF_LOG_DEBUG;
                        else
                                level = GF_LOG_ERROR;
                        gf_msg (this->name, level, errno, P_MSG_MKNOD_FAILED,
                                "mknod on %s failed", real_path);
                        goto out;
                }
        }

        entry_created = _gf_true;

#ifndef HAVE_SET_FSID
        op_ret = sys_lchown (real_path, frame->root->uid, gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LCHOWN_FAILED,
                        "lchown on %s failed", real_path);
                goto out;
        }
#endif

post_op:
        op_ret = posix_acl_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_ACL_FAILED,
                        "setting ACLs on %s failed", real_path);
        }

        if (priv->update_pgfid_nlinks) {
                MAKE_PGFID_XATTR_KEY (pgfid_xattr_key, PGFID_XATTR_KEY_PREFIX,
                                      loc->pargfid);
                op_ret = posix_inode_ctx_get_all (loc->inode, this, &ctx);
                if (op_ret < 0) {
                        op_errno = ENOMEM;
                        goto out;
                }

                pthread_mutex_lock (&ctx->pgfid_lock);
                {
                        LINK_MODIFY_PGFID_XATTR (real_path, pgfid_xattr_key,
                                                 nlink_samepgfid, 0, op_ret,
                                                 this, unlock);
                }
unlock:
                pthread_mutex_unlock (&ctx->pgfid_lock);
        }

        if (priv->gfid2path) {
                posix_set_gfid2path_xattr (this, real_path, loc->pargfid,
                                           loc->name);
        }

        op_ret = posix_entry_create_xattr_set (this, real_path, xdata);
        if (op_ret) {
                if (errno != EEXIST)
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_XATTR_FAILED,
                                "setting xattrs on %s failed", real_path);
                else
                        gf_msg_debug (this->name, 0,
                                      "setting xattrs on %s failed", real_path);
        }

        if (!linked) {
                op_ret = posix_gfid_set (this, real_path, loc, xdata);
                if (op_ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_GFID_FAILED,
                                "setting gfid on %s failed", real_path);
                } else {
                        gfid_set = _gf_true;
                }
        }

        op_ret = posix_pstat (this, loc->inode, NULL, real_path, &stbuf, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_MKNOD_FAILED,
                        "mknod on %s failed", real_path);
                goto out;
        }

        posix_set_ctime (frame, this, real_path, -1, loc->inode, &stbuf);

        op_ret = posix_pstat (this, loc->parent, loc->pargfid, par_path,
                              &postparent, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "post-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        posix_set_parent_ctime (frame, this, par_path, -1, loc->parent,
                                &postparent);

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        if (op_ret < 0) {
                if (entry_created) {
                        if (S_ISREG (mode))
                                sys_unlink (real_path);
                        else
                                sys_rmdir (real_path);
                }

                if (gfid_set)
                        posix_gfid_unset (this, xdata);
        }

        STACK_UNWIND_STRICT (mknod, frame, op_ret, op_errno,
                             (loc)?loc->inode:NULL, &stbuf, &preparent,
                             &postparent, NULL);

        return 0;
}

int
posix_mkdir (call_frame_t *frame, xlator_t *this,
             loc_t *loc, mode_t mode, mode_t umask, dict_t *xdata)
{
        int32_t               op_ret          = -1;
        int32_t               op_errno        = 0;
        char                 *real_path       = NULL, *gfid_path = NULL;
        char                 *par_path        = NULL, *xattr_name = NULL;
        struct iatt           stbuf           = {0, };
        struct posix_private *priv            = NULL;
        gid_t                 gid             = 0;
        struct iatt           preparent       = {0,};
        struct iatt           postparent      = {0,};
        gf_boolean_t          entry_created   = _gf_false, gfid_set = _gf_false;
        uuid_t                uuid_req        = {0,};
        ssize_t               size            = 0;
        dict_t               *xdata_rsp       = NULL;
        void                 *disk_xattr      = NULL;
        data_t               *arg_data        = NULL;
        char          pgfid[GF_UUID_BUF_SIZE] = {0};
        char                 value_buf[4096]  = {0,};
        gf_boolean_t         have_val         = _gf_false;
        mode_t               mode_bit         = 0;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        /* The Hidden directory should be for housekeeping purpose and it
           should not get created from a user request */
        if (__is_root_gfid (loc->pargfid) &&
            (strcmp (loc->name, GF_HIDDEN_PATH) == 0)) {
                gf_msg (this->name, GF_LOG_WARNING, EPERM,
                        P_MSG_MKDIR_NOT_PERMITTED, "mkdir issued on %s, which"
                        "is not permitted", GF_HIDDEN_PATH);
                op_errno = EPERM;
                op_ret = -1;
                goto out;
        }

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);
        GFID_NULL_CHECK_AND_GOTO (frame, this, loc, xdata, op_ret, op_errno,
                                  out);
        DISK_SPACE_CHECK_AND_GOTO (frame, priv, xdata, op_ret, op_errno, out);

        MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, NULL);
        if (!real_path || !par_path) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        if (loc->parent)
                gf_uuid_unparse (loc->parent->gfid, pgfid);
        else
                gf_uuid_unparse (loc->pargfid, pgfid);

        gid = frame->root->gid;

        op_ret = posix_pstat (this, loc->inode, NULL, real_path, &stbuf, _gf_false);

        SET_FS_ID (frame->root->uid, gid);

        mode_bit = (priv->create_directory_mask & mode)
                   | priv->force_directory_mode;
        mode = posix_override_umask (mode, mode_bit);

        if (xdata) {
                op_ret = dict_get_gfuuid (xdata, "gfid-req", &uuid_req);
                if (!op_ret && !gf_uuid_compare (stbuf.ia_gfid, uuid_req)) {
                        op_ret = -1;
                        op_errno = EEXIST;
                        goto out;
                }
        }

        if (!gf_uuid_is_null (uuid_req)) {
                op_ret = posix_istat (this, loc->inode, uuid_req, NULL, &stbuf);
                if ((op_ret == 0) && IA_ISDIR (stbuf.ia_type)) {
                        size = posix_handle_path (this, uuid_req, NULL, NULL,
                                                  0);
                        if (size > 0)
                                gfid_path = alloca (size);

                        if (gfid_path)
                                posix_handle_path (this, uuid_req, NULL,
                                                   gfid_path, size);

                        if (frame->root->pid != GF_CLIENT_PID_SELF_HEALD) {
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        P_MSG_DIR_OF_SAME_ID, "mkdir (%s): "
                                        "gfid (%s) is already associated with "
                                        "directory (%s). Hence, both "
                                        "directories will share same gfid and "
                                        "this can lead to inconsistencies.",
                                        loc->path, uuid_utoa (uuid_req),
                                        gfid_path ? gfid_path : "<NULL>");

                                gf_event (EVENT_POSIX_SAME_GFID, "gfid=%s;"
                                          "path=%s;newpath=%s;brick=%s:%s",
                                          uuid_utoa (uuid_req),
                                          gfid_path ? gfid_path : "<NULL>",
                                          loc->path, priv->hostname,
                                          priv->base_path);
                        }
                        if (!posix_symlinks_match (this, loc, uuid_req))
                                /* For afr selfheal of dir renames, we need to
                                 * remove the old symlink in order for
                                 * posix_gfid_set to set the symlink to the
                                 * new dir.*/
                                posix_handle_unset (this, stbuf.ia_gfid, NULL);
                }
        } else if (frame->root->pid != GF_SERVER_PID_TRASH) {
                op_ret = -1;
                op_errno = EPERM;
                gf_msg_callingfn (this->name, GF_LOG_WARNING, op_errno,
                        P_MSG_NULL_GFID, "mkdir (%s): is issued without "
                        "gfid-req %p", loc->path, xdata);
                goto out;
        }

        op_ret = posix_pstat (this, loc->parent, loc->pargfid, par_path,
                              &preparent, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "pre-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        if (preparent.ia_prot.sgid) {
                gid = preparent.ia_gid;
                mode |= S_ISGID;
        }

        op_ret = dict_get_str (xdata, GF_PREOP_PARENT_KEY, &xattr_name);
        if (xattr_name != NULL) {
                arg_data = dict_get (xdata, xattr_name);
                if (arg_data) {
                        size = sys_lgetxattr (par_path, xattr_name, value_buf,
                                              sizeof(value_buf) - 1);
                        if (size >= 0) {
                                have_val = _gf_true;
                        } else {
                                if (errno == ERANGE) {
                                        gf_msg (this->name, GF_LOG_INFO, errno,
                                                P_MSG_PREOP_CHECK_FAILED,
                                                "mkdir (%s/%s): getxattr on key "
                                                "(%s) path (%s) failed due to "
                                                " buffer overflow", pgfid,
                                                loc->name, xattr_name,
                                                par_path);
                                        size = sys_lgetxattr (par_path,
                                                              xattr_name, NULL,
                                                              0);
                                }
                                if (size < 0) {
                                        op_ret = -1;
                                        op_errno = errno;
                                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                                P_MSG_PREOP_CHECK_FAILED,
                                                "mkdir (%s/%s): getxattr on key (%s)"
                                                " path (%s) failed ", pgfid,
                                                loc->name, xattr_name,
                                                par_path);
                                        goto out;
                                }
                        }
                        disk_xattr = alloca (size);
                        if (disk_xattr == NULL) {
                                op_ret = -1;
                                op_errno = errno;
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        P_MSG_PREOP_CHECK_FAILED,
                                        "mkdir (%s/%s): alloca failed during"
                                        " preop of mkdir (%s)", pgfid,
                                        loc->name, real_path);
                                goto out;
                        }
                        if (have_val) {
                                memcpy (disk_xattr, value_buf, size);
                        } else {
                                size = sys_lgetxattr (par_path, xattr_name,
                                                      disk_xattr, size);
                                if (size < 0) {
                                        op_errno = errno;
                                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                                P_MSG_PREOP_CHECK_FAILED,
                                                "mkdir (%s/%s): getxattr on "
                                                " key (%s) path (%s) failed "
                                                "(%s)", pgfid, loc->name,
                                                xattr_name, par_path,
                                                strerror (errno));
                                        goto out;
                               }
                        }
                        if ((arg_data->len != size)
                            || (memcmp (arg_data->data, disk_xattr, size))) {
                                gf_msg (this->name, GF_LOG_INFO, EIO,
                                        P_MSG_PREOP_CHECK_FAILED,
                                        "mkdir (%s/%s): failing preop of "
                                        "mkdir (%s) as on-disk"
                                        " xattr value differs from argument "
                                        "value for key %s", pgfid, loc->name,
                                        real_path, xattr_name);
                                op_ret = -1;
                                op_errno = EIO;

                                xdata_rsp = dict_new ();
                                if (xdata_rsp == NULL) {
                                        gf_msg (this->name, GF_LOG_ERROR,
                                                ENOMEM,
                                                P_MSG_PREOP_CHECK_FAILED,
                                                "mkdir (%s/%s):  "
                                                "dict allocation failed", pgfid,
                                                loc->name);
                                        op_errno = ENOMEM;
                                        goto out;
                                }

                                op_errno = dict_set_int8 (xdata_rsp,
                                                          GF_PREOP_CHECK_FAILED, 1);
                                goto out;
                        }

                        dict_del (xdata, xattr_name);
                }

                dict_del (xdata, GF_PREOP_PARENT_KEY);
        }

        op_ret = sys_mkdir (real_path, mode);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_MKDIR_FAILED,
                        "mkdir of %s failed", real_path);
                goto out;
        }

        entry_created = _gf_true;

#ifndef HAVE_SET_FSID
        op_ret = sys_chown (real_path, frame->root->uid, gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_CHOWN_FAILED,
                        "chown on %s failed", real_path);
                goto out;
        }
#endif
        op_ret = posix_acl_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_ACL_FAILED,
                        "setting ACLs on %s failed ", real_path);
        }

        op_ret = posix_entry_create_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_XATTR_FAILED,
                        "setting xattrs on %s failed", real_path);
        }

        op_ret = posix_gfid_set (this, real_path, loc, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_GFID_FAILED,
                        "setting gfid on %s failed", real_path);
        } else {
                gfid_set = _gf_true;
        }

        op_ret = posix_pstat (this, loc->inode, NULL, real_path, &stbuf, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "lstat on %s failed", real_path);
                goto out;
        }

        posix_set_ctime (frame, this, real_path, -1, loc->inode, &stbuf);

        op_ret = posix_pstat (this, loc->parent, loc->pargfid, par_path,
                              &postparent, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "post-operation lstat on parent of %s failed",
                        real_path);
                goto out;
        }

        posix_set_parent_ctime (frame, this, par_path, -1, loc->parent,
                                &postparent);

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        if (op_ret < 0) {
                if (entry_created)
                        sys_rmdir (real_path);

                if (gfid_set)
                        posix_gfid_unset (this, xdata);
        }

        STACK_UNWIND_STRICT (mkdir, frame, op_ret, op_errno,
                             (loc)?loc->inode:NULL, &stbuf, &preparent,
                             &postparent, xdata_rsp);

        if (xdata_rsp)
                dict_unref (xdata_rsp);

        return 0;
}

int
posix_add_unlink_to_ctx (inode_t *inode, xlator_t *this, char *unlink_path)
{
        uint64_t ctx = GF_UNLINK_FALSE;
        int ret = 0;

        if (!unlink_path) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                        P_MSG_UNLINK_FAILED,
                        "Creation of unlink entry failed for gfid: %s",
                        unlink_path);
                ret = -1;
                goto out;
        }

        ctx = GF_UNLINK_TRUE;
        ret = posix_inode_ctx_set_unlink_flag (inode, this, ctx);
        if (ret < 0) {
                goto out;
        }

out:
        return ret;
}

int32_t
posix_move_gfid_to_unlink (xlator_t *this, uuid_t gfid, loc_t *loc)
{
        char *unlink_path = NULL;
        char *gfid_path = NULL;
        int ret = 0;
        struct posix_private    *priv_posix = NULL;

        priv_posix = (struct posix_private *) this->private;

        MAKE_HANDLE_GFID_PATH (gfid_path, this, gfid, NULL);

        POSIX_GET_FILE_UNLINK_PATH (priv_posix->base_path,
                                    loc->inode->gfid, unlink_path);
        if (!unlink_path) {
                ret = -1;
                goto out;
        }
        gf_msg_debug (this->name, 0,
                      "Moving gfid: %s to unlink_path : %s",
                      gfid_path, unlink_path);
        ret = sys_rename (gfid_path, unlink_path);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        P_MSG_UNLINK_FAILED,
                        "Creation of unlink entry failed for gfid: %s",
                        unlink_path);
                goto out;
        }
        ret = posix_add_unlink_to_ctx (loc->inode, this, unlink_path);
        if (ret < 0)
                goto out;

out:
        return ret;
}

int32_t
posix_unlink_gfid_handle_and_entry (call_frame_t *frame, xlator_t *this,
                                    const char *real_path, struct iatt *stbuf,
                                    int32_t *op_errno, loc_t *loc,
                                    gf_boolean_t get_link_count,
                                    dict_t *rsp_dict)
{
        int32_t                ret      = 0;
        struct iatt            prebuf   = {0,};
        gf_boolean_t           locked   = _gf_false;
        gf_boolean_t           update_ctime = _gf_false;

        /*  Unlink the gfid_handle_first */
        if (stbuf && stbuf->ia_nlink == 1) {

                LOCK (&loc->inode->lock);

                if (loc->inode->fd_count == 0) {
                        UNLOCK (&loc->inode->lock);
                        ret = posix_handle_unset (this, stbuf->ia_gfid, NULL);
                } else {
                        UNLOCK (&loc->inode->lock);
                        ret = posix_move_gfid_to_unlink (this, stbuf->ia_gfid,
                                                         loc);
                }
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_UNLINK_FAILED, "unlink of gfid handle "
                                "failed for path:%s with gfid %s",
                                real_path, uuid_utoa (stbuf->ia_gfid));
                }
        } else {
                update_ctime = _gf_true;
        }

        if (get_link_count) {
                LOCK (&loc->inode->lock);
                locked = _gf_true;
                /* Since this stat is to get link count and not for time
                 * attributes, intentionally passing inode as NULL
                 */
                ret = posix_pstat (this, NULL, loc->gfid, real_path,
                                   &prebuf, _gf_true);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_LSTAT_FAILED, "lstat on %s failed",
                                real_path);
                        goto err;
                }
        }

        /* Unlink the actual file */
        ret = sys_unlink (real_path);
        if (ret == -1) {
                if (op_errno)
                        *op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_UNLINK_FAILED,
                        "unlink of %s failed", real_path);
                goto err;
        }

        if (locked) {
                UNLOCK (&loc->inode->lock);
                locked = _gf_false;
        }

        if (update_ctime) {
                posix_set_ctime (frame, this, NULL, -1, loc->inode, stbuf);
        }

        ret = dict_set_uint32 (rsp_dict, GET_LINK_COUNT, prebuf.ia_nlink);
        if (ret)
                gf_msg (this->name, GF_LOG_WARNING, 0, P_MSG_SET_XDATA_FAIL,
                        "failed to set "GET_LINK_COUNT" for %s", real_path);

        return 0;

err:
        if (locked) {
                UNLOCK (&loc->inode->lock);
                locked = _gf_false;
        }
        return -1;
}

gf_boolean_t
posix_skip_non_linkto_unlink (dict_t *xdata, loc_t *loc, char *key,
                              const char *linkto_xattr, struct iatt *stbuf,
                              const char *real_path)
{
        gf_boolean_t     skip_unlink            = _gf_false;
        gf_boolean_t     is_dht_linkto_file     = _gf_false;
        int              unlink_if_linkto       = 0;
        ssize_t          xattr_size             = -1;
        int              op_ret                 = -1;

        op_ret = dict_get_int32 (xdata, key,
                                 &unlink_if_linkto);

        if (!op_ret && unlink_if_linkto) {

                is_dht_linkto_file =  IS_DHT_LINKFILE_MODE (stbuf);
                if (!is_dht_linkto_file)
                        return _gf_true;

                LOCK (&loc->inode->lock);

                xattr_size = sys_lgetxattr (real_path, linkto_xattr, NULL, 0);

                if (xattr_size <= 0)
                        skip_unlink = _gf_true;

                UNLOCK (&loc->inode->lock);

                gf_msg ("posix", GF_LOG_INFO, 0, P_MSG_XATTR_STATUS,
                        "linkto_xattr status: %"PRIu32" for %s", skip_unlink,
                        real_path);
        }
        return skip_unlink;

}

int32_t
posix_unlink (call_frame_t *frame, xlator_t *this,
              loc_t *loc, int xflag, dict_t *xdata)
{
        int32_t                op_ret             = -1;
        int32_t                op_errno           = 0;
        char                   *real_path         = NULL;
        char                   *par_path          = NULL;
        int32_t                fd                 = -1;
        struct iatt            stbuf              = {0,};
        struct iatt            postbuf            = {0,};
        struct posix_private  *priv               = NULL;
        struct iatt            preparent          = {0,};
        struct iatt            postparent         = {0,};
        char                  *pgfid_xattr_key    = NULL;
        int32_t                nlink_samepgfid    = 0;
        int32_t                check_open_fd      = 0;
        int32_t                skip_unlink        = 0;
        int32_t                fdstat_requested   = 0;
        dict_t                *unwind_dict        = NULL;
        void                  *uuid               = NULL;
        char                   uuid_str[GF_UUID_BUF_SIZE] = {0};
        char                   gfid_str[GF_UUID_BUF_SIZE] = {0};
        gf_boolean_t           get_link_count     = _gf_false;
        posix_inode_ctx_t     *ctx                = NULL;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, &stbuf);
        if (!real_path || !par_path) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        op_ret = posix_pstat (this, loc->parent, loc->pargfid, par_path,
                              &preparent, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "pre-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        priv = this->private;

        op_ret = dict_get_ptr (xdata, TIER_LINKFILE_GFID, &uuid);

        if (!op_ret && gf_uuid_compare (uuid, stbuf.ia_gfid)) {
                op_errno = ENOENT;
                op_ret = -1;
                gf_uuid_unparse (uuid, uuid_str);
                gf_uuid_unparse (stbuf.ia_gfid, gfid_str);
                gf_msg_debug (this->name, op_errno, "Mismatch in gfid for path "
                              "%s. Aborting the unlink. loc->gfid = %s, "
                              "stbuf->ia_gfid = %s", real_path,
                              uuid_str, gfid_str);
                goto out;
        }

        op_ret = dict_get_int32 (xdata, DHT_SKIP_OPEN_FD_UNLINK,
                                 &check_open_fd);

        if (!op_ret && check_open_fd) {

                LOCK (&loc->inode->lock);

                if (loc->inode->fd_count) {
                        skip_unlink = 1;
                }

                UNLOCK (&loc->inode->lock);

                gf_msg (this->name, GF_LOG_INFO, 0, P_MSG_KEY_STATUS_INFO,
                        "open-fd-key-status: %"PRIu32" for %s", skip_unlink,
                        real_path);

                if (skip_unlink) {
                        op_ret = -1;
                        op_errno = EBUSY;
                        goto out;
                }
        }
        /*
         * If either of the function return true, skip_unlink.
         * If first first function itself return true,
         * we don't need to call second function, skip unlink.
         */
        skip_unlink = posix_skip_non_linkto_unlink (xdata, loc,
                                                    DHT_SKIP_NON_LINKTO_UNLINK,
                                                    DHT_LINKTO, &stbuf,
                                                    real_path);
        skip_unlink = skip_unlink || posix_skip_non_linkto_unlink (xdata, loc,
                                                    TIER_SKIP_NON_LINKTO_UNLINK,
                                                    TIER_LINKTO, &stbuf,
                                                    real_path);
        if (skip_unlink) {
                op_ret = -1;
                op_errno = EBUSY;
                goto out;
        }

        if (IA_ISREG (loc->inode->ia_type) &&
            xdata && dict_get (xdata, DHT_IATT_IN_XDATA_KEY)) {
                fdstat_requested = 1;
        }

        if (fdstat_requested ||
            (priv->background_unlink && IA_ISREG (loc->inode->ia_type))) {
                fd = sys_open (real_path, O_RDONLY, 0);
                if (fd == -1) {
                        op_ret = -1;
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_OPEN_FAILED,
                                "open of %s failed", real_path);
                        goto out;
                }
        }

        if (priv->update_pgfid_nlinks && (stbuf.ia_nlink > 1)) {
                MAKE_PGFID_XATTR_KEY (pgfid_xattr_key, PGFID_XATTR_KEY_PREFIX,
                                      loc->pargfid);
                op_ret = posix_inode_ctx_get_all (loc->inode, this, &ctx);
                if (op_ret < 0) {
                        op_errno = ENOMEM;
                        goto out;
                }
                pthread_mutex_lock (&ctx->pgfid_lock);
                {
                        UNLINK_MODIFY_PGFID_XATTR (real_path, pgfid_xattr_key,
                                                   nlink_samepgfid, 0, op_ret,
                                                   this, unlock);
                }
        unlock:
                pthread_mutex_unlock (&ctx->pgfid_lock);

                if (op_ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                P_MSG_XATTR_FAILED, "modification of "
                                "parent gfid xattr failed (path:%s gfid:%s)",
                                real_path, uuid_utoa (loc->inode->gfid));
                        if (op_errno != ENOATTR)
                            /* Allow unlink if pgfid xattr is not set. */
                            goto out;
                }
        }

        if (priv->gfid2path && (stbuf.ia_nlink > 1)) {
                op_ret = posix_remove_gfid2path_xattr (this, real_path,
                                                       loc->pargfid,
                                                       loc->name);
                if (op_ret < 0) {
                        /* Allow unlink if pgfid xattr is not set. */
                        if (errno != ENOATTR)
                                goto out;
                }
        }

        unwind_dict = dict_new ();
        if (!unwind_dict) {
                op_errno = -ENOMEM;
                op_ret = -1;
                goto out;
        }

        if (xdata && dict_get (xdata, GET_LINK_COUNT))
                get_link_count = _gf_true;
        op_ret =  posix_unlink_gfid_handle_and_entry (frame, this, real_path,
                                                      &stbuf, &op_errno, loc,
                                                      get_link_count,
                                                      unwind_dict);
        if (op_ret == -1) {
                goto out;
        }

        if (fdstat_requested) {
                op_ret = posix_fdstat (this, loc->inode, fd, &postbuf);
                if (op_ret == -1) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_FSTAT_FAILED, "post operation "
                                "fstat failed on fd=%d", fd);
                        goto out;
                }
                op_ret = posix_set_iatt_in_dict (unwind_dict, &postbuf);
        }

        op_ret = posix_pstat (this, loc->parent, loc->pargfid, par_path,
                              &postparent, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "post-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        posix_set_parent_ctime (frame, this, par_path, -1, loc->parent,
                                &postparent);

        unwind_dict = posix_dict_set_nlink (xdata, unwind_dict, stbuf.ia_nlink);
        op_ret = 0;
out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (unlink, frame, op_ret, op_errno,
                             &preparent, &postparent, unwind_dict);

        if (fd != -1) {
                sys_close (fd);
        }

        /* unref unwind_dict*/
        if (unwind_dict) {
                dict_unref (unwind_dict);
        }

        return 0;
}


int
posix_rmdir (call_frame_t *frame, xlator_t *this,
             loc_t *loc, int flags, dict_t *xdata)
{
        int32_t               op_ret     = -1;
        int32_t               op_errno   = 0;
        char                 *real_path  = NULL;
        char                 *par_path   = NULL;
        char                 *gfid_str   = NULL;
        struct iatt           preparent  = {0,};
        struct iatt           postparent = {0,};
        struct iatt           stbuf      = {0,};
        struct posix_private *priv       = NULL;
        char                  tmp_path[PATH_MAX] = {0,};

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (loc, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);

        /* The Hidden directory should be for housekeeping purpose and it
           should not get deleted from inside process */
        if (__is_root_gfid (loc->pargfid) &&
            (strcmp (loc->name, GF_HIDDEN_PATH) == 0)) {
                gf_msg (this->name, GF_LOG_WARNING, EPERM,
                        P_MSG_RMDIR_NOT_PERMITTED, "rmdir issued on %s, which"
                        "is not permitted", GF_HIDDEN_PATH);
                op_errno = EPERM;
                op_ret = -1;
                goto out;
        }

        priv = this->private;

        MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, &stbuf);
        if (!real_path || !par_path) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        op_ret = posix_pstat (this, loc->parent, loc->pargfid, par_path,
                              &preparent, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "pre-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        if (flags) {
                gfid_str = uuid_utoa (stbuf.ia_gfid);

                op_ret = sys_mkdir (priv->trash_path, 0755);
                if (errno != EEXIST && op_ret == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_MKDIR_FAILED,
                                "mkdir of %s failed", priv->trash_path);
                } else {
                        (void) snprintf (tmp_path, sizeof(tmp_path), "%s/%s",
                                         priv->trash_path, gfid_str);
                        gf_msg_debug (this->name, 0,
                                      "Moving %s to %s", real_path, tmp_path);
                        op_ret = sys_rename (real_path, tmp_path);
                        pthread_cond_signal (&priv->janitor_cond);
                }
        } else {
                op_ret = sys_rmdir (real_path);
        }
        op_errno = errno;

        if (op_ret == 0) {
                if (posix_symlinks_match (this, loc, stbuf.ia_gfid))
                        posix_handle_unset (this, stbuf.ia_gfid, NULL);
        }

        if (op_errno == EEXIST)
                /* Solaris sets errno = EEXIST instead of ENOTEMPTY */
                op_errno = ENOTEMPTY;

        /* No need to log a common error as ENOTEMPTY */
        if (op_ret == -1 && op_errno != ENOTEMPTY) {
                gf_msg (this->name, GF_LOG_ERROR, op_errno, P_MSG_RMDIR_FAILED,
                        "rmdir of %s failed", real_path);
        }

        if (op_ret == -1) {
                if (op_errno == ENOTEMPTY) {
                        gf_msg_debug (this->name, 0, "%s on %s failed", (flags)
                                      ? "rename" : "rmdir", real_path);
                } else {
                        gf_msg (this->name, GF_LOG_ERROR, op_errno,
                                P_MSG_DIR_OPERATION_FAILED, "%s on %s failed",
                                (flags) ? "rename" : "rmdir", real_path);
                }
                goto out;
        }

        op_ret = posix_pstat (this, loc->parent, loc->pargfid, par_path,
                              &postparent, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "post-operation lstat on parent of %s failed",
                        par_path);
                goto out;
        }

        posix_set_parent_ctime (frame, this, par_path, -1, loc->parent,
                                &postparent);

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (rmdir, frame, op_ret, op_errno,
                             &preparent, &postparent, NULL);

        return 0;
}


int
posix_symlink (call_frame_t *frame, xlator_t *this,
               const char *linkname, loc_t *loc, mode_t umask, dict_t *xdata)
{
        int32_t               op_ret          = -1;
        int32_t               op_errno        = 0;
        char *                real_path       = 0;
        char *                par_path        = 0;
        struct iatt           stbuf           = { 0, };
        struct posix_private *priv            = NULL;
        gid_t                 gid             = 0;
        struct iatt           preparent       = {0,};
        struct iatt           postparent      = {0,};
        char                 *pgfid_xattr_key = NULL;
        int32_t               nlink_samepgfid = 0;
        gf_boolean_t          entry_created   = _gf_false, gfid_set = _gf_false;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (linkname, out);
        VALIDATE_OR_GOTO (loc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);
        GFID_NULL_CHECK_AND_GOTO (frame, this, loc, xdata, op_ret, op_errno,
                                  out);
        DISK_SPACE_CHECK_AND_GOTO (frame, priv, xdata, op_ret, op_errno, out);

        MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, &stbuf);

        gid = frame->root->gid;
        if (!real_path || !par_path) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        SET_FS_ID (frame->root->uid, gid);

        op_ret = posix_pstat (this, loc->parent, loc->pargfid, par_path,
                              &preparent, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "pre-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        if (preparent.ia_prot.sgid) {
                gid = preparent.ia_gid;
        }

        op_ret = sys_symlink (linkname, real_path);

        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_SYMLINK_FAILED,
                        "symlink of %s --> %s failed",
                        real_path, linkname);
                goto out;
        }

        entry_created = _gf_true;

        posix_set_ctime (frame, this, real_path, -1, loc->inode, &stbuf);

#ifndef HAVE_SET_FSID
        op_ret = sys_lchown (real_path, frame->root->uid, gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LCHOWN_FAILED,
                        "lchown failed on %s", real_path);
                goto out;
        }
#endif
        op_ret = posix_acl_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_ACL_FAILED,
                        "setting ACLs on %s failed", real_path);
        }

        if (priv->update_pgfid_nlinks) {
                MAKE_PGFID_XATTR_KEY (pgfid_xattr_key, PGFID_XATTR_KEY_PREFIX,
                                      loc->pargfid);
                nlink_samepgfid = 1;
                SET_PGFID_XATTR (real_path, pgfid_xattr_key, nlink_samepgfid,
                                 XATTR_CREATE, op_ret, this, ignore);
        }

        if (priv->gfid2path) {
                posix_set_gfid2path_xattr (this, real_path, loc->pargfid,
                                           loc->name);
        }

ignore:
        op_ret = posix_entry_create_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_XATTR_FAILED,
                        "setting xattrs on %s failed ", real_path);
        }

        op_ret = posix_gfid_set (this, real_path, loc, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_GFID_FAILED,
                        "setting gfid on %s failed", real_path);
        } else {
                gfid_set = _gf_true;
        }

        op_ret = posix_pstat (this, loc->inode, NULL, real_path, &stbuf, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "lstat failed on %s", real_path);
                goto out;
        }

        op_ret = posix_pstat (this, loc->parent, loc->pargfid, par_path,
                              &postparent, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "post-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        posix_set_parent_ctime (frame, this, par_path, -1, loc->parent,
                                &postparent);

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        if (op_ret < 0) {
                if (entry_created)
                        sys_unlink (real_path);

                if (gfid_set)
                        posix_gfid_unset (this, xdata);
        }

        STACK_UNWIND_STRICT (symlink, frame, op_ret, op_errno,
                             (loc)?loc->inode:NULL, &stbuf, &preparent,
                             &postparent, NULL);

        return 0;
}


int
posix_rename (call_frame_t *frame, xlator_t *this,
              loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        int32_t               op_ret          = -1;
        int32_t               op_errno        = 0;
        char                 *real_oldpath    = NULL;
        char                 *real_newpath    = NULL;
        char                 *par_oldpath     = NULL;
        char                 *par_newpath     = NULL;
        struct iatt           stbuf           = {0, };
        struct posix_private *priv            = NULL;
        char                  was_present     = 1;
        struct iatt           preoldparent    = {0, };
        struct iatt           postoldparent   = {0, };
        struct iatt           prenewparent    = {0, };
        struct iatt           postnewparent   = {0, };
        char                  olddirid[64];
        char                  newdirid[64];
        uuid_t                victim          = {0};
        int                   was_dir         = 0;
        int                   nlink           = 0;
        char                 *pgfid_xattr_key = NULL;
        int32_t               nlink_samepgfid = 0;
        char                 *gfid_path            = NULL;
        dict_t               *unwind_dict     = NULL;
        gf_boolean_t          locked          = _gf_false;
        gf_boolean_t          get_link_count  = _gf_false;
        posix_inode_ctx_t    *ctx_old         = NULL;
        posix_inode_ctx_t    *ctx_new         = NULL;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (oldloc, out);
        VALIDATE_OR_GOTO (newloc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);
        DISK_SPACE_CHECK_AND_GOTO (frame, priv, xdata, op_ret, op_errno, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_ENTRY_HANDLE (real_oldpath, par_oldpath, this, oldloc, NULL);
        if (!real_oldpath || !par_oldpath) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        MAKE_ENTRY_HANDLE (real_newpath, par_newpath, this, newloc, &stbuf);
        if (!real_newpath || !par_newpath) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        unwind_dict = dict_new ();
        if (!unwind_dict) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        op_ret = posix_pstat (this, oldloc->parent, oldloc->pargfid,
                              par_oldpath, &preoldparent, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "pre-operation lstat on parent %s failed",
                        par_oldpath);
                goto out;
        }

        op_ret = posix_pstat (this, newloc->parent, newloc->pargfid,
                              par_newpath, &prenewparent, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "pre-operation lstat on parent of %s failed",
                        par_newpath);
                goto out;
        }

        op_ret = posix_pstat (this, newloc->inode, NULL, real_newpath, &stbuf,
                              _gf_false);
        if ((op_ret == -1) && (errno == ENOENT)){
                was_present = 0;
        } else {
                gf_uuid_copy (victim, stbuf.ia_gfid);
                if (IA_ISDIR (stbuf.ia_type))
                        was_dir = 1;
                nlink = stbuf.ia_nlink;
        }

        if (was_present && IA_ISDIR(stbuf.ia_type) && !newloc->inode) {
                gf_msg (this->name, GF_LOG_WARNING, EEXIST, P_MSG_DIR_FOUND,
                        "found directory at %s while expecting ENOENT",
                        real_newpath);
                op_ret = -1;
                op_errno = EEXIST;
                goto out;
        }

        if (was_present && IA_ISDIR(stbuf.ia_type) &&
            gf_uuid_compare (newloc->inode->gfid, stbuf.ia_gfid)) {
                gf_msg (this->name, GF_LOG_WARNING, EEXIST, P_MSG_DIR_FOUND,
                        "found directory %s at %s while renaming %s",
                        uuid_utoa_r (newloc->inode->gfid, olddirid),
                        real_newpath,
                        uuid_utoa_r (stbuf.ia_gfid, newdirid));
                op_ret = -1;
                op_errno = EEXIST;
                goto out;
        }

        op_ret = posix_inode_ctx_get_all (oldloc->inode, this, &ctx_old);
        if (op_ret < 0) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }

        if (newloc->inode) {
                op_ret = posix_inode_ctx_get_all (newloc->inode, this, &ctx_new);
                if (op_ret < 0) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto out;
                }
        }

        if (IA_ISDIR (oldloc->inode->ia_type))
                posix_handle_unset (this, oldloc->inode->gfid, NULL);

        pthread_mutex_lock (&ctx_old->pgfid_lock);
        {
                if (!IA_ISDIR (oldloc->inode->ia_type)
                    && priv->update_pgfid_nlinks) {
                        MAKE_PGFID_XATTR_KEY (pgfid_xattr_key,
                                              PGFID_XATTR_KEY_PREFIX,
                                              oldloc->pargfid);
                        UNLINK_MODIFY_PGFID_XATTR (real_oldpath,
                                                   pgfid_xattr_key,
                                                   nlink_samepgfid, 0,
                                                   op_ret,
                                                   this, unlock);
                }

                if ((xdata) && (dict_get (xdata, GET_LINK_COUNT))
                    && (real_newpath) && (was_present)) {
                        pthread_mutex_lock (&ctx_new->pgfid_lock);
                        locked = _gf_true;
                        get_link_count = _gf_true;
                        op_ret = posix_pstat (this, newloc->inode, newloc->gfid,
                                              real_newpath, &stbuf, _gf_false);
                        if ((op_ret == -1) && (errno != ENOENT)) {
                                op_errno = errno;
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        P_MSG_LSTAT_FAILED,
                                        "lstat on %s failed", real_newpath);
                                goto unlock;
                        }
                }

                op_ret = sys_rename (real_oldpath, real_newpath);
                if (op_ret == -1) {
                        op_errno = errno;
                        if (op_errno == ENOTEMPTY) {
                                gf_msg_debug (this->name, 0, "rename of %s to"
                                              " %s failed: %s", real_oldpath,
                                              real_newpath,
                                              strerror (op_errno));
                        } else {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        P_MSG_RENAME_FAILED,
                                        "rename of %s to %s failed",
                                        real_oldpath, real_newpath);
                       }

                        if (priv->update_pgfid_nlinks
                            && !IA_ISDIR (oldloc->inode->ia_type)) {
                                LINK_MODIFY_PGFID_XATTR (real_oldpath,
                                                         pgfid_xattr_key,
                                                         nlink_samepgfid, 0,
                                                         op_ret,
                                                         this, unlock);
                        }

                        goto unlock;
                }

                if (locked) {
                        pthread_mutex_unlock (&ctx_new->pgfid_lock);
                        locked = _gf_false;
                }

                if ((get_link_count) &&
                    (dict_set_uint32 (unwind_dict, GET_LINK_COUNT,
                                      stbuf.ia_nlink)))
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                P_MSG_SET_XDATA_FAIL, "failed to set "
                                GET_LINK_COUNT" for %s", real_newpath);

                if (!IA_ISDIR (oldloc->inode->ia_type)
                    && priv->update_pgfid_nlinks) {
                        MAKE_PGFID_XATTR_KEY (pgfid_xattr_key,
                                              PGFID_XATTR_KEY_PREFIX,
                                              newloc->pargfid);
                        LINK_MODIFY_PGFID_XATTR (real_newpath,
                                                 pgfid_xattr_key,
                                                 nlink_samepgfid, 0,
                                                 op_ret,
                                                 this, unlock);
                }

                if (!IA_ISDIR (oldloc->inode->ia_type) && priv->gfid2path) {
                        MAKE_HANDLE_ABSPATH (gfid_path, this,
                                             oldloc->inode->gfid);

                        posix_remove_gfid2path_xattr (this, gfid_path,
                                                      oldloc->pargfid,
                                                      oldloc->name);
                        posix_set_gfid2path_xattr (this, gfid_path,
                                                   newloc->pargfid,
                                                   newloc->name);
                }
        }

unlock:
        if (locked) {
                pthread_mutex_unlock (&ctx_new->pgfid_lock);
                locked = _gf_false;
        }
        pthread_mutex_unlock (&ctx_old->pgfid_lock);

        if (op_ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0, P_MSG_XATTR_FAILED,
                        "modification of "
                        "parent gfid xattr failed (gfid:%s)",
                        uuid_utoa (oldloc->inode->gfid));
                goto out;
        }

        if (was_dir)
                posix_handle_unset (this, victim, NULL);

        if (was_present && !was_dir && nlink == 1)
                posix_handle_unset (this, victim, NULL);

        if (IA_ISDIR (oldloc->inode->ia_type)) {
                posix_handle_soft (this, real_newpath, newloc,
                                   oldloc->inode->gfid, NULL);
        }

        op_ret = posix_pstat (this, newloc->inode, NULL, real_newpath, &stbuf,
                              _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "lstat on %s failed", real_newpath);
                goto out;
        }

        /* Since the same inode is later used and dst inode is not present,
         * update ctime on source inode. It can't use old path because it
         * doesn't exist and xattr has to be stored on disk */
        posix_set_ctime (frame, this, real_newpath, -1, oldloc->inode, &stbuf);

        op_ret = posix_pstat (this, oldloc->parent, oldloc->pargfid,
                              par_oldpath, &postoldparent, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "post-operation lstat on parent %s failed",
                        par_oldpath);
                goto out;
        }

        posix_set_parent_ctime (frame, this, par_oldpath, -1, oldloc->parent,
                                &postoldparent);

        op_ret = posix_pstat (this, newloc->parent, newloc->pargfid,
                              par_newpath, &postnewparent, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "post-operation lstat on parent %s failed",
                        par_newpath);
                goto out;
        }

        posix_set_parent_ctime (frame, this, par_newpath, -1, newloc->parent,
                                &postnewparent);

        if (was_present)
                unwind_dict = posix_dict_set_nlink (xdata, unwind_dict, nlink);
        op_ret = 0;
out:

        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (rename, frame, op_ret, op_errno, &stbuf,
                             &preoldparent, &postoldparent,
                             &prenewparent, &postnewparent, unwind_dict);

        if (unwind_dict)
                dict_unref (unwind_dict);

        return 0;
}


int
posix_link (call_frame_t *frame, xlator_t *this,
            loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        int32_t               op_ret          = -1;
        int32_t               op_errno        = 0;
        char                 *real_oldpath    = 0;
        char                 *real_newpath    = 0;
        char                 *par_newpath     = 0;
        struct iatt           stbuf           = {0, };
        struct posix_private *priv            = NULL;
        struct iatt           preparent       = {0,};
        struct iatt           postparent      = {0,};
        int32_t               nlink_samepgfid = 0;
        char                 *pgfid_xattr_key = NULL;
        gf_boolean_t          entry_created   = _gf_false;
        posix_inode_ctx_t    *ctx             = NULL;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (oldloc, out);
        VALIDATE_OR_GOTO (newloc, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);
        DISK_SPACE_CHECK_AND_GOTO (frame, priv, xdata, op_ret, op_errno, out);

        SET_FS_ID (frame->root->uid, frame->root->gid);
        MAKE_INODE_HANDLE (real_oldpath, this, oldloc, &stbuf);
        if (!real_oldpath) {
                op_errno = errno;
                goto out;
        }

        if (priv->max_hardlinks && stbuf.ia_nlink >= priv->max_hardlinks) {
                op_ret = -1;
                op_errno = EMLINK;
                gf_log (this->name, GF_LOG_ERROR,
                        "hardlink failed: %s exceeds max link count (%u/%u).",
                        real_oldpath, stbuf.ia_nlink, priv->max_hardlinks);
                goto out;
        }

        MAKE_ENTRY_HANDLE (real_newpath, par_newpath, this, newloc, &stbuf);
        if (!real_newpath || !par_newpath) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        op_ret = posix_pstat (this, newloc->parent, newloc->pargfid,
                              par_newpath, &preparent, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "lstat failed: %s", par_newpath);
                goto out;
        }


        op_ret = sys_link (real_oldpath, real_newpath);

        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LINK_FAILED,
                        "link %s to %s failed",
                        real_oldpath, real_newpath);
                goto out;
        }

        entry_created = _gf_true;

        op_ret = posix_pstat (this, newloc->inode, NULL, real_newpath, &stbuf,
                              _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "lstat on %s failed", real_newpath);
                goto out;
        }

        posix_set_ctime (frame, this, real_newpath, -1, newloc->inode, &stbuf);

        op_ret = posix_pstat (this, newloc->parent, newloc->pargfid,
                              par_newpath, &postparent, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "lstat failed: %s", par_newpath);
                goto out;
        }

        posix_set_parent_ctime (frame, this, par_newpath, -1, newloc->parent,
                                &postparent);

        if (priv->update_pgfid_nlinks) {
                MAKE_PGFID_XATTR_KEY (pgfid_xattr_key, PGFID_XATTR_KEY_PREFIX,
                                      newloc->pargfid);

                op_ret = posix_inode_ctx_get_all (newloc->inode, this, &ctx);
                if (op_ret < 0) {
                        op_errno = ENOMEM;
                        goto out;
                }

                pthread_mutex_lock (&ctx->pgfid_lock);
                {
                        LINK_MODIFY_PGFID_XATTR (real_newpath, pgfid_xattr_key,
                                                 nlink_samepgfid, 0, op_ret,
                                                 this, unlock);
                }
        unlock:
                pthread_mutex_unlock (&ctx->pgfid_lock);

                if (op_ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                P_MSG_XATTR_FAILED, "modification of "
                                "parent gfid xattr failed (path:%s gfid:%s)",
                                real_newpath, uuid_utoa (newloc->inode->gfid));
                        goto out;
                }
        }

        if (priv->gfid2path) {
                if (stbuf.ia_nlink <= MAX_GFID2PATH_LINK_SUP) {
                        op_ret = posix_set_gfid2path_xattr (this, real_newpath,
                                                            newloc->pargfid,
                                                            newloc->name);
                        if (op_ret) {
                                op_errno = errno;
                                goto out;
                        }
                 } else {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                P_MSG_XATTR_NOTSUP, "Link count exceeded. "
                                "gfid2path xattr not set (path:%s gfid:%s)",
                                real_newpath, uuid_utoa (newloc->inode->gfid));
                 }
        }

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        STACK_UNWIND_STRICT (link, frame, op_ret, op_errno,
                             (oldloc)?oldloc->inode:NULL, &stbuf, &preparent,
                             &postparent, NULL);

        if (op_ret < 0) {
                if (entry_created)
                        sys_unlink (real_newpath);
        }

        return 0;
}

int
posix_create (call_frame_t *frame, xlator_t *this,
              loc_t *loc, int32_t flags, mode_t mode,
              mode_t umask, fd_t *fd, dict_t *xdata)
{
        int32_t                op_ret          = -1;
        int32_t                op_errno        = 0;
        int32_t                _fd             = -1;
        int                    _flags          = 0;
        char *                 real_path       = NULL;
        char *                 par_path        = NULL;
        struct iatt            stbuf           = {0, };
        struct posix_fd *      pfd             = NULL;
        struct posix_private * priv            = NULL;
        char                   was_present     = 1;

        gid_t                  gid             = 0;
        struct iatt            preparent       = {0,};
        struct iatt            postparent      = {0,};

        int                    nlink_samepgfid = 0;
        char *                 pgfid_xattr_key = NULL;
        gf_boolean_t           entry_created   = _gf_false, gfid_set = _gf_false;
        mode_t                 mode_bit        = 0;

        DECLARE_OLD_FS_ID_VAR;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (loc, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;
        VALIDATE_OR_GOTO (priv, out);
        GFID_NULL_CHECK_AND_GOTO (frame, this, loc, xdata, op_ret, op_errno,
                                  out);
        DISK_SPACE_CHECK_AND_GOTO (frame, priv, xdata, op_ret, op_errno, out);

        MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, &stbuf);

        gid = frame->root->gid;

        SET_FS_ID (frame->root->uid, gid);
        if (!real_path || !par_path) {
                op_ret = -1;
                op_errno = ESTALE;
                goto out;
        }

        op_ret = posix_pstat (this, loc->parent, loc->pargfid, par_path,
                              &preparent, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "pre-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        if (preparent.ia_prot.sgid) {
                gid = preparent.ia_gid;
        }

        if (!flags) {
                _flags = O_CREAT | O_RDWR | O_EXCL;
        }
        else {
                _flags = flags | O_CREAT;
        }

        op_ret = posix_pstat (this, loc->inode, NULL, real_path, &stbuf,
                              _gf_false);
        if ((op_ret == -1) && (errno == ENOENT)) {
                was_present = 0;
        }

        if (priv->o_direct)
                _flags |= O_DIRECT;

        mode_bit = (priv->create_mask & mode) | priv->force_create_mode;
        mode = posix_override_umask (mode, mode_bit);
        _fd = sys_open (real_path, _flags, mode);

        if (_fd == -1) {
                op_errno = errno;
                op_ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_OPEN_FAILED,
                        "open on %s failed", real_path);
                goto out;
        }

        if ((_flags & O_CREAT) && (_flags & O_EXCL)) {
                entry_created = _gf_true;
        }


        if (was_present)
                goto fill_stat;

#ifndef HAVE_SET_FSID
        op_ret = sys_chown (real_path, frame->root->uid, gid);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_CHOWN_FAILED,
                        "chown on %s failed", real_path);
        }
#endif
        op_ret = posix_acl_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_ACL_FAILED,
                        "setting ACLs on %s failed", real_path);
        }

        if (priv->update_pgfid_nlinks) {
                MAKE_PGFID_XATTR_KEY (pgfid_xattr_key, PGFID_XATTR_KEY_PREFIX,
                                      loc->pargfid);
                nlink_samepgfid = 1;
                SET_PGFID_XATTR (real_path, pgfid_xattr_key, nlink_samepgfid,
                                 XATTR_CREATE, op_ret, this, ignore);
        }

        if (priv->gfid2path) {
                posix_set_gfid2path_xattr (this, real_path, loc->pargfid,
                                            loc->name);
        }
ignore:
        op_ret = posix_entry_create_xattr_set (this, real_path, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_XATTR_FAILED,
                        "setting xattrs on %s failed ", real_path);
        }

fill_stat:
        op_ret = posix_gfid_set (this, real_path, loc, xdata);
        if (op_ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_GFID_FAILED,
                        "setting gfid on %s failed", real_path);
        } else {
                gfid_set = _gf_true;
        }

        op_ret = posix_fdstat (this, loc->inode, _fd, &stbuf);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_FSTAT_FAILED,
                        "fstat on %d failed", _fd);
                goto out;
        }

        posix_set_ctime (frame, this, real_path, -1, loc->inode, &stbuf);

        op_ret = posix_pstat (this, loc->parent, loc->pargfid, par_path,
                              &postparent, _gf_false);
        if (op_ret == -1) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "post-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        posix_set_parent_ctime (frame, this, par_path, -1, loc->parent,
                                &postparent);

        op_ret = -1;
        pfd = GF_CALLOC (1, sizeof (*pfd), gf_posix_mt_posix_fd);
        if (!pfd) {
                op_errno = errno;
                goto out;
        }

        pfd->flags = flags;
        pfd->fd    = _fd;

        op_ret = fd_ctx_set (fd, this, (uint64_t)(long)pfd);
        if (op_ret)
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        P_MSG_FD_PATH_SETTING_FAILED,
                        "failed to set the fd context path=%s fd=%p",
                        real_path, fd);

        LOCK (&priv->lock);
        {
                priv->nr_files++;
        }
        UNLOCK (&priv->lock);

        op_ret = 0;

out:
        SET_TO_OLD_FS_ID ();

        if (op_ret < 0) {
                if (_fd != -1)
                        sys_close (_fd);

                if (entry_created)
                        sys_unlink (real_path);

                if (gfid_set)
                        posix_gfid_unset (this, xdata);
        }

        STACK_UNWIND_STRICT (create, frame, op_ret, op_errno,
                             fd, (loc)?loc->inode:NULL, &stbuf, &preparent,
                             &postparent, xdata);

        return 0;
}

/* TODO: Ensure atomocity of put, and rollback in case of failure
 * One of the ways, is to perform put in the hidden directory
 * and rename it to the specified location, if the put was successful
 */
int32_t
posix_put (call_frame_t *frame, xlator_t *this, loc_t *loc,
           mode_t mode, mode_t umask, uint32_t flags,
           struct iovec *vector, int32_t count, off_t offset,
           struct iobref *iobref, dict_t *xattr, dict_t *xdata)
{
        int32_t               op_ret       = -1;
        int32_t               op_errno     = 0;
        fd_t                 *fd           = NULL;
        char                 *real_path    = NULL;
        char                 *par_path     = NULL;
        struct iatt           stbuf        = {0, };
        struct iatt           preparent    = {0,};
        struct iatt           postparent   = {0,};

        MAKE_ENTRY_HANDLE (real_path, par_path, this, loc, &stbuf);

        op_ret = posix_pstat (this, loc->parent, loc->pargfid, par_path,
                              &preparent, _gf_false);
        if (op_ret < 0) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "pre-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }
        fd = fd_create (loc->inode, getpid());
        if (!fd) {
                op_ret = -1;
                op_errno = ENOMEM;
                goto out;
        }
        fd->flags = flags;

        /* No xlators are expected below posix, but we cannot still call sys_create()
         * directly here, as posix_create does many other things like chmod, setxattr
         * etc. along with sys_create(). But we cannot also directly call posix_create()
         * as it calls STACK_UNWIND. Hence using syncop()
         */
        op_ret = syncop_create (this, loc, flags, mode, fd, &stbuf, xdata, NULL);
        if (op_ret < 0) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_CREATE_FAILED,
                        "create of %s failed", loc->path);
                goto out;
        }

        op_ret = posix_pstat (this, loc->parent, loc->pargfid, par_path,
                              &postparent, _gf_false);
        if (op_ret < 0) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "post-operation lstat on parent %s failed",
                        par_path);
                goto out;
        }

        op_ret = syncop_writev (this, fd, vector, count, offset, iobref,
                                flags, xdata, NULL);
        if (op_ret < 0) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_WRITE_FAILED,
                        "write on file %s failed", loc->path);
                goto out;
        }

        op_ret = syncop_fsetxattr (this, fd, xattr, flags, xdata, NULL);
        if (op_ret < 0) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_XATTR_FAILED,
                        "setxattr on file %s failed", loc->path);
                goto out;
        }

        op_ret = syncop_flush (this, fd, xdata, NULL);
        if (op_ret < 0) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_CLOSE_FAILED,
                        "setxattr on file %s failed", loc->path);
                goto out;
        }

        op_ret = posix_pstat (this, loc->inode, loc->gfid, real_path, &stbuf, _gf_false);
        if (op_ret < 0) {
                op_errno = errno;
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_LSTAT_FAILED,
                        "post-operation lstat on %s failed", real_path);
                goto out;
        }
out:
        STACK_UNWIND_STRICT (put, frame, op_ret, op_errno, loc->inode, &stbuf,
                             &preparent, &postparent, NULL);

        return 0;
}
