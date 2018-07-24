/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#define __XOPEN_SOURCE 500

#include <stdint.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <ftw.h>
#include <sys/stat.h>
#include <signal.h>
#include <aio.h>

#ifdef HAVE_SYS_ACL_H
#ifdef HAVE_ACL_LIBACL_H /* for acl_to_any_text() */
#include <acl/libacl.h>
#else /* FreeBSD and others */
#include <sys/acl.h>
#endif
#endif

#ifndef GF_BSD_HOST_OS
#include <alloca.h>
#endif /* GF_BSD_HOST_OS */

#include <fnmatch.h>
#include "glusterfs.h"
#include "checksum.h"
#include "dict.h"
#include "logging.h"
#include "posix.h"
#include "posix-messages.h"
#include "posix-metadata.h"
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
#include "glusterfs-acl.h"
#include "posix-gfid-path.h"
#include "events.h"
#include "glusterfsd.h"
#include <sys/types.h>

char *marker_xattrs[] = {"trusted.glusterfs.quota.*",
                         "trusted.glusterfs.*.xtime",
                         NULL};

char *marker_contri_key = "trusted.*.*.contri";

static char* posix_ignore_xattrs[] = {
        "gfid-req",
        GLUSTERFS_ENTRYLK_COUNT,
        GLUSTERFS_INODELK_COUNT,
        GLUSTERFS_POSIXLK_COUNT,
        GLUSTERFS_PARENT_ENTRYLK,
        GF_GFIDLESS_LOOKUP,
        GLUSTERFS_INODELK_DOM_COUNT,
        GLUSTERFS_INTERNAL_FOP_KEY,
        NULL
};

static char* list_xattr_ignore_xattrs[] = {
        GF_SELINUX_XATTR_KEY,
        GF_XATTR_VOL_ID_KEY,
        GFID_XATTR_KEY,
        NULL
};

gf_boolean_t
posix_special_xattr (char **pattern, char *key)
{
        int          i    = 0;
        gf_boolean_t flag = _gf_false;

        GF_VALIDATE_OR_GOTO ("posix", pattern, out);
        GF_VALIDATE_OR_GOTO ("posix", key, out);

        for (i = 0; pattern[i]; i++) {
                if (!fnmatch (pattern[i], key, 0)) {
                        flag = _gf_true;
                        break;
                }
        }
out:
        return flag;
}

int
posix_handle_mdata_xattr (call_frame_t *frame, const char *name, int *op_errno)
{
        int                i                 = 0;
        int                ret               = 0;
        int                pid               = 1;
        static const char * const internal_xattr[]  = { GF_XATTR_MDATA_KEY,
                                                        NULL
                                                      };
        if (frame && frame->root) {
                pid = frame->root->pid;
        }

        if (!name || pid < GF_CLIENT_PID_MAX) {
                /* No need to do anything here */
                ret = 0;
                goto out;
        }

        for (i = 0; internal_xattr[i]; i++) {
                if (fnmatch (internal_xattr[i] , name, FNM_PERIOD) == 0) {
                        ret = -1;
                        if (op_errno) {
                                *op_errno = ENOATTR;
                        }

                        gf_msg_debug ("posix", ENOATTR,
                                      "Ignoring the key %s as an internal "
                                      "xattrs.", name);
                        goto out;
                }
        }

        ret = 0;
out:
        return ret;
}

int
posix_handle_georep_xattrs (call_frame_t *frame, const char *name,
                            int *op_errno, gf_boolean_t is_getxattr)
{

        int                i                = 0;
        int                ret              = 0;
        int                pid              = 1;
        gf_boolean_t       filter_xattr     = _gf_true;
        static const char *georep_xattr[]   = { "*.glusterfs.*.stime",
                                                "*.glusterfs.*.xtime",
                                                "*.glusterfs.*.entry_stime",
                                                "*.glusterfs.volume-mark.*",
                                                NULL
                                              };
        if (frame && frame->root) {
                pid = frame->root->pid;
        }

        if (!name) {
                /* No need to do anything here */
                ret = 0;
                goto out;
        }

        if (pid == GF_CLIENT_PID_GSYNCD && is_getxattr) {
                filter_xattr = _gf_false;

                /* getxattr from gsyncd process should return all the
                 * internal xattr. In other cases ignore such xattrs
                 */
        }

        for (i = 0; filter_xattr && georep_xattr[i]; i++) {
                if (fnmatch (georep_xattr[i] , name, FNM_PERIOD) == 0) {
                        ret = -1;
                        if (op_errno)
                               *op_errno = ENOATTR;

                        gf_msg_debug ("posix", ENOATTR,
                                      "Ignoring the key %s as an internal "
                                      "xattrs.", name);
                        goto out;
                }
        }

        ret = 0;
out:
        return ret;
}


int32_t
posix_set_mode_in_dict (dict_t *in_dict, dict_t *out_dict,
                        struct iatt *in_stbuf)
{
        int ret             = -1;
        mode_t mode         = 0;

        if ((!in_dict) || (!in_stbuf) || (!out_dict)) {
                goto out;
        }

        /* We need this only for files */
        if (!(IA_ISREG (in_stbuf->ia_type))) {
                ret = 0;
                goto out;
        }

        /* Nobody asked for this */
        if (!dict_get (in_dict, DHT_MODE_IN_XDATA_KEY)) {
                ret = 0;
                goto out;
        }
        mode = st_mode_from_ia (in_stbuf->ia_prot, in_stbuf->ia_type);

        ret = dict_set_int32 (out_dict, DHT_MODE_IN_XDATA_KEY, mode);

out:
        return ret;
}


static gf_boolean_t
posix_xattr_ignorable (char *key)
{
        return gf_get_index_by_elem (posix_ignore_xattrs, key) >= 0;
}

static int
_posix_xattr_get_set_from_backend (posix_xattr_filler_t *filler, char *key)
{
        ssize_t  xattr_size = -1;
        int      ret        = 0;
        char     *value     = NULL;
        char     val_buf[256] = {0};
        gf_boolean_t have_val   = _gf_false;

        if (!gf_is_valid_xattr_namespace (key)) {
                ret = -1;
                goto out;
        }

        /* Most of the gluster internal xattrs don't exceed 256 bytes. So try
         * getxattr with ~256 bytes. If it gives ERANGE then go the old way
         * of getxattr with NULL buf to find the length and then getxattr with
         * allocated buf to fill the data. This way we reduce lot of getxattrs.
         */
        if (filler->real_path)
                xattr_size = sys_lgetxattr (filler->real_path, key, val_buf,
                                            sizeof (val_buf) - 1);
        else
                xattr_size = sys_fgetxattr (filler->fdnum, key, val_buf,
                                            sizeof (val_buf) - 1);

        if (xattr_size >= 0) {
                have_val = _gf_true;
        } else if (xattr_size == -1 && errno != ERANGE) {
                ret = -1;
                goto out;
        }

        if (have_val) {
                /*No need to do getxattr*/
        } else if (filler->real_path) {
                xattr_size = sys_lgetxattr (filler->real_path, key, NULL, 0);
        } else {
                xattr_size = sys_fgetxattr (filler->fdnum, key, NULL, 0);
        }

        if (xattr_size != -1) {
                value = GF_CALLOC (1, xattr_size + 1, gf_posix_mt_char);
                if (!value)
                        goto out;

                if (have_val) {
                        memcpy (value, val_buf, xattr_size);
                } else if (filler->real_path) {
                        xattr_size = sys_lgetxattr (filler->real_path, key,
                                                    value, xattr_size);
                } else {
                        xattr_size = sys_fgetxattr (filler->fdnum, key, value,
                                                    xattr_size);
                }
                if (xattr_size == -1) {
                        if (filler->real_path)
                                gf_msg (filler->this->name, GF_LOG_WARNING, 0,
                                        P_MSG_XATTR_FAILED,
                                        "getxattr failed. path: %s, key: %s",
                                        filler->real_path, key);
                        else
                                gf_msg (filler->this->name, GF_LOG_WARNING, 0,
                                        P_MSG_XATTR_FAILED,
                                        "getxattr failed. gfid: %s, key: %s",
                                        uuid_utoa (filler->fd->inode->gfid),
                                        key);
                        GF_FREE (value);
                        goto out;
                }

                value[xattr_size] = '\0';
                ret = dict_set_bin (filler->xattr, key, value, xattr_size);
                if (ret < 0) {
                        if (filler->real_path)
                                gf_msg_debug (filler->this->name, 0,
                                        "dict set failed. path: %s, key: %s",
                                        filler->real_path, key);
                        else
                                gf_msg_debug (filler->this->name, 0,
                                        "dict set failed. gfid: %s, key: %s",
                                        uuid_utoa (filler->fd->inode->gfid),
                                        key);
                        GF_FREE (value);
                        goto out;
                }
        }
        ret = 0;
out:
        return ret;
}

static int gf_posix_xattr_enotsup_log;

static int
_posix_get_marker_all_contributions (posix_xattr_filler_t *filler)
{
        ssize_t  size = -1, remaining_size = -1, list_offset = 0;
        int      ret  = -1;
        char    *list = NULL, key[4096] = {0, };

        if (filler->real_path)
                size = sys_llistxattr (filler->real_path, NULL, 0);
        else
                size = sys_flistxattr (filler->fdnum, NULL, 0);
        if (size == -1) {
                if ((errno == ENOTSUP) || (errno == ENOSYS)) {
                        GF_LOG_OCCASIONALLY (gf_posix_xattr_enotsup_log,
                                             THIS->name, GF_LOG_WARNING,
                                             "Extended attributes not "
                                             "supported (try remounting brick"
                                             " with 'user_xattr' flag)");
                } else {
                        if (filler->real_path)
                                gf_msg (THIS->name, GF_LOG_WARNING, errno,
                                        P_MSG_XATTR_FAILED,
                                        "listxattr failed on %s",
                                        filler->real_path);
                        else
                                gf_msg (THIS->name, GF_LOG_WARNING, errno,
                                        P_MSG_XATTR_FAILED,
                                        "listxattr failed on %s",
                                        uuid_utoa (filler->fd->inode->gfid));
                }
                goto out;
        }

        if (size == 0) {
                ret = 0;
                goto out;
        }

        list = alloca (size);
        if (!list) {
                goto out;
        }

        if (filler->real_path)
                size = sys_llistxattr (filler->real_path, list, size);
        else
                size = sys_flistxattr (filler->fdnum, list, size);
        if (size <= 0) {
                ret = size;
                goto out;
        }

        remaining_size = size;
        list_offset = 0;

        while (remaining_size > 0) {
                strcpy (key, list + list_offset);
                if (fnmatch (marker_contri_key, key, 0) == 0) {
                        ret = _posix_xattr_get_set_from_backend (filler, key);
                }

                remaining_size -= strlen (key) + 1;
                list_offset += strlen (key) + 1;
        }

        ret = 0;

out:
        return ret;
}

static int
_posix_get_marker_quota_contributions (posix_xattr_filler_t *filler, char *key)
{
        char *saveptr = NULL, *token = NULL, *tmp_key = NULL;
        char *ptr     = NULL;
        int   i       = 0, ret = 0;

        tmp_key = ptr = gf_strdup (key);
        for (i = 0; i < 4; i++) {
                token = strtok_r (tmp_key, ".", &saveptr);
                tmp_key = NULL;
        }

        if (strncmp (token, "contri", strlen ("contri")) == 0) {
                ret = _posix_get_marker_all_contributions (filler);
        } else {
                ret = _posix_xattr_get_set_from_backend (filler, key);
        }

        GF_FREE (ptr);

        return ret;
}

static inode_t *
_get_filler_inode (posix_xattr_filler_t *filler)
{
        if (filler->fd)
                return filler->fd->inode;
        else if (filler->loc && filler->loc->inode)
                return filler->loc->inode;
        else
                return NULL;
}

static int
_posix_xattr_get_set (dict_t *xattr_req, char *key, data_t *data,
                      void *xattrargs)
{
        posix_xattr_filler_t *filler = xattrargs;
        int       ret      = -1;
        char     *databuf  = NULL;
        int       _fd      = -1;
        ssize_t  req_size  = 0;
        int32_t  list_offset = 0;
        ssize_t  remaining_size = 0;
        char     *xattr    = NULL;
        inode_t  *inode    = NULL;

        if (posix_xattr_ignorable (key))
                goto out;
        /* should size be put into the data_t ? */
        if (!strcmp (key, GF_CONTENT_KEY)
            && IA_ISREG (filler->stbuf->ia_type)) {
                if (!filler->real_path)
                        goto out;

                /* file content request */
                req_size = data_to_uint64 (data);
                if (req_size >= filler->stbuf->ia_size) {
                        _fd = open (filler->real_path, O_RDONLY);
                        if (_fd == -1) {
                                gf_msg (filler->this->name, GF_LOG_ERROR, errno,
                                        P_MSG_XDATA_GETXATTR,
                                        "Opening file %s failed",
                                        filler->real_path);
                                goto err;
                        }

                        /*
                         * There could be a situation where the ia_size is
                         * zero. GF_CALLOC will return a pointer to the
                         * memory initialized by gf_mem_set_acct_info.
                         * This function adds a header and a footer to
                         * the allocated memory.  The returned pointer
                         * points to the memory just after the header, but
                         * when size is zero, there is no space for user
                         * data. The memory can be freed by calling GF_FREE.
                         */
                        databuf = GF_CALLOC (1, filler->stbuf->ia_size,
                                             gf_posix_mt_char);
                        if (!databuf) {
                                goto err;
                        }

                        ret = sys_read (_fd, databuf, filler->stbuf->ia_size);
                        if (ret == -1) {
                                gf_msg (filler->this->name, GF_LOG_ERROR, errno,
                                         P_MSG_XDATA_GETXATTR,
                                        "Read on file %s failed",
                                        filler->real_path);
                                goto err;
                        }

                        ret = sys_close (_fd);
                        _fd = -1;
                        if (ret == -1) {
                                gf_msg (filler->this->name, GF_LOG_ERROR, errno,
                                        P_MSG_XDATA_GETXATTR,
                                        "Close on file %s failed",
                                        filler->real_path);
                                goto err;
                        }

                        ret = dict_set_bin (filler->xattr, key,
                                            databuf, filler->stbuf->ia_size);
                        if (ret < 0) {
                                gf_msg (filler->this->name, GF_LOG_ERROR, 0,
                                        P_MSG_XDATA_GETXATTR,
                                        "failed to set dict value. key: %s,"
                                        "path: %s",
                                        key, filler->real_path);
                                goto err;
                        }

                        /* To avoid double free in cleanup below */
                        databuf = NULL;
                err:
                        if (_fd != -1)
                                sys_close (_fd);
                        GF_FREE (databuf);
                }
        } else if (!strcmp (key, GLUSTERFS_OPEN_FD_COUNT)) {
                inode = _get_filler_inode (filler);
                if (!inode || gf_uuid_is_null (inode->gfid))
                                goto out;
                ret = dict_set_uint32 (filler->xattr, key, inode->fd_count);
                if (ret < 0) {
                        gf_msg (filler->this->name, GF_LOG_WARNING, 0,
                                P_MSG_DICT_SET_FAILED,
                                "Failed to set dictionary value for %s", key);
                }
        } else if (!strcmp (key, GLUSTERFS_ACTIVE_FD_COUNT)) {
                inode = _get_filler_inode (filler);
                if (!inode || gf_uuid_is_null (inode->gfid))
                                goto out;
                ret = dict_set_uint32 (filler->xattr, key,
                                       inode->active_fd_count);
                if (ret < 0) {
                        gf_msg (filler->this->name, GF_LOG_WARNING, 0,
                                P_MSG_DICT_SET_FAILED,
                                "Failed to set dictionary value for %s", key);
                }
        } else if (!strcmp (key, GET_ANCESTRY_PATH_KEY)) {
                /* As of now, the only consumers of POSIX_ANCESTRY_PATH attempt
                 * fetching it via path-based fops. Hence, leaving it as it is
                 * for now.
                 */
                if (!filler->real_path)
                        goto out;
                char *path = NULL;
                ret = posix_get_ancestry (filler->this, filler->loc->inode,
                                          NULL, &path, POSIX_ANCESTRY_PATH,
                                          &filler->op_errno, xattr_req);
                if (ret < 0) {
                        goto out;
                }

                ret = dict_set_dynstr (filler->xattr, GET_ANCESTRY_PATH_KEY,
                                       path);
                if (ret < 0) {
                        GF_FREE (path);
                        goto out;
                }

        } else if (fnmatch (marker_contri_key, key, 0) == 0) {
                ret = _posix_get_marker_quota_contributions (filler, key);
        } else if (strcmp(key, GF_REQUEST_LINK_COUNT_XDATA) == 0) {
                ret = dict_set (filler->xattr,
                                GF_REQUEST_LINK_COUNT_XDATA, data);
        } else if (strcmp (key, GF_GET_SIZE) == 0) {
                if (filler->stbuf && IA_ISREG (filler->stbuf->ia_type)) {
                        ret = dict_set_uint64 (filler->xattr, GF_GET_SIZE,
                                               filler->stbuf->ia_size);
                }
        } else {
                remaining_size = filler->list_size;
                while (remaining_size > 0) {
                        xattr = filler->list + list_offset;
                        if (fnmatch (key, xattr, 0) == 0)
                                ret = _posix_xattr_get_set_from_backend (filler,
                                                                         xattr);
                        remaining_size -= strlen (xattr) + 1;
                        list_offset += strlen (xattr) + 1;
                }
        }
out:
        return 0;
}

int
posix_fill_gfid_path (xlator_t *this, const char *path, struct iatt *iatt)
{
        int ret = 0;
        ssize_t size = 0;

        if (!iatt)
                return 0;

        size = sys_lgetxattr (path, GFID_XATTR_KEY, iatt->ia_gfid, 16);
        /* Return value of getxattr */
        if ((size == 16) || (size == -1))
                ret = 0;
        else
                ret = size;

        return ret;
}


int
posix_fill_gfid_fd (xlator_t *this, int fd, struct iatt *iatt)
{
        int ret = 0;
        ssize_t size = 0;

        if (!iatt)
                return 0;

        size = sys_fgetxattr (fd, GFID_XATTR_KEY, iatt->ia_gfid, 16);
        /* Return value of getxattr */
        if ((size == 16) || (size == -1))
                ret = 0;
        else
                ret = size;

        return ret;
}

void
posix_fill_ino_from_gfid (xlator_t *this, struct iatt *buf)
{
        /* consider least significant 8 bytes of value out of gfid */
        if (gf_uuid_is_null (buf->ia_gfid)) {
                buf->ia_ino = -1;
                goto out;
        }
        buf->ia_ino = gfid_to_ino (buf->ia_gfid);
        buf->ia_flags |= IATT_INO;
out:
        return;
}

int
posix_fdstat (xlator_t *this, inode_t *inode, int fd, struct iatt *stbuf_p)
{
        int                    ret     = 0;
        struct stat            fstatbuf = {0, };
        struct iatt            stbuf = {0, };
        struct posix_private  *priv = NULL;

        priv = this->private;

        ret = sys_fstat (fd, &fstatbuf);
        if (ret == -1)
                goto out;

        if (fstatbuf.st_nlink && !S_ISDIR (fstatbuf.st_mode))
                fstatbuf.st_nlink--;

        iatt_from_stat (&stbuf, &fstatbuf);

        if (inode && priv->ctime) {
                ret = posix_get_mdata_xattr (this, NULL, fd, inode, &stbuf);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_GETMDATA_FAILED,
                                "posix get mdata failed on gfid: %s",
                                uuid_utoa(inode->gfid));
                        goto out;
                }
        }
        ret = posix_fill_gfid_fd (this, fd, &stbuf);
        stbuf.ia_flags |= IATT_GFID;

        posix_fill_ino_from_gfid (this, &stbuf);

        if (stbuf_p)
                *stbuf_p = stbuf;

out:
        return ret;
}


/* The inode here is expected to update posix_mdata stored on disk.
 * Don't use it as a general purpose inode and don't expect it to
 * be always exists
 */
int
posix_istat (xlator_t *this, inode_t *inode, uuid_t gfid, const char *basename,
             struct iatt *buf_p)
{
        char        *real_path = NULL;
        struct stat  lstatbuf = {0, };
        struct iatt  stbuf = {0, };
        int          ret = 0;
        struct posix_private *priv = NULL;

        priv = this->private;

        MAKE_HANDLE_PATH (real_path, this, gfid, basename);
        if (!real_path) {
                gf_msg (this->name, GF_LOG_ERROR, ESTALE,
                        P_MSG_HANDLE_PATH_CREATE,
                        "Failed to create handle path for %s/%s",
                        uuid_utoa (gfid), basename ? basename : "");
                errno = ESTALE;
                ret = -1;
                goto out;
        }

        ret = sys_lstat (real_path, &lstatbuf);

        if (ret != 0) {
                if (ret == -1) {
                        if (errno != ENOENT && errno != ELOOP)
                                gf_msg (this->name, GF_LOG_WARNING, errno,
                                        P_MSG_LSTAT_FAILED,
                                        "lstat failed on %s",
                                        real_path);
                } else {
                        // may be some backend filesystem issue
                        gf_msg (this->name, GF_LOG_ERROR, 0, P_MSG_LSTAT_FAILED,
                                "lstat failed on %s and return value is %d "
                                "instead of -1. Please see dmesg output to "
                                "check whether the failure is due to backend "
                                "filesystem issue", real_path, ret);
                        ret = -1;
                }
                goto out;
        }

        if ((lstatbuf.st_ino == priv->handledir.st_ino) &&
            (lstatbuf.st_dev == priv->handledir.st_dev)) {
                errno = ENOENT;
                return -1;
        }

        if (!S_ISDIR (lstatbuf.st_mode))
                lstatbuf.st_nlink --;

        iatt_from_stat (&stbuf, &lstatbuf);

        if (inode && priv->ctime) {
                ret = posix_get_mdata_xattr (this, real_path, -1, inode,
                                             &stbuf);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_GETMDATA_FAILED,
                                "posix get mdata failed on %s", real_path);
                        goto out;
                }
        }

        if (basename)
                posix_fill_gfid_path (this, real_path, &stbuf);
        else
                gf_uuid_copy (stbuf.ia_gfid, gfid);
        stbuf.ia_flags |= IATT_GFID;

        posix_fill_ino_from_gfid (this, &stbuf);

        if (buf_p)
                *buf_p = stbuf;
out:
        return ret;
}



int
posix_pstat (xlator_t *this, inode_t *inode, uuid_t gfid, const char *path,
             struct iatt *buf_p, gf_boolean_t inode_locked)
{
        struct stat  lstatbuf = {0, };
        struct iatt  stbuf = {0, };
        int          ret = 0;
        int          op_errno = 0;
        struct posix_private *priv = NULL;


        priv = this->private;

        if (gfid && !gf_uuid_is_null (gfid))
                gf_uuid_copy (stbuf.ia_gfid, gfid);
        else
                posix_fill_gfid_path (this, path, &stbuf);
        stbuf.ia_flags |= IATT_GFID;

        ret = sys_lstat (path, &lstatbuf);
        if (ret == -1) {
                if (errno != ENOENT) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_LSTAT_FAILED, "lstat failed on %s", path);
                        errno = op_errno;/*gf_msg could have changed errno*/
                }
                goto out;
        }

        if ((lstatbuf.st_ino == priv->handledir.st_ino) &&
            (lstatbuf.st_dev == priv->handledir.st_dev)) {
                errno = ENOENT;
                return -1;
        }

        if (!S_ISDIR (lstatbuf.st_mode))
                lstatbuf.st_nlink --;

        iatt_from_stat (&stbuf, &lstatbuf);

        if (inode && priv->ctime) {
                if (!inode_locked) {
                        ret = posix_get_mdata_xattr (this, path, -1, inode, &stbuf);
                } else {
                        ret = __posix_get_mdata_xattr (this, path, -1, inode, &stbuf);
                }
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_GETMDATA_FAILED,
                                "posix get mdata failed on gfid: %s",
                                uuid_utoa(inode->gfid));
                        goto out;
                }
        }

        posix_fill_ino_from_gfid (this, &stbuf);

        if (buf_p)
                *buf_p = stbuf;
out:
        return ret;
}


static void
_get_list_xattr (posix_xattr_filler_t *filler)
{
        ssize_t               size                  = 0;

        if ((!filler) && (!filler->real_path) && (filler->fdnum < 0))
                goto out;

        if (filler->real_path)
                size = sys_llistxattr (filler->real_path, NULL, 0);
        else
                size = sys_flistxattr (filler->fdnum, NULL, 0);

        if (size <= 0)
                goto out;

        filler->list = GF_CALLOC (1, size, gf_posix_mt_char);
        if (!filler->list)
                goto out;

        if (filler->real_path)
                size = sys_llistxattr (filler->real_path, filler->list, size);
        else
                size = sys_flistxattr (filler->fdnum, filler->list, size);

        filler->list_size = size;
out:
        return;
}


static void
_handle_list_xattr (dict_t *xattr_req, const char *real_path, int fdnum,
                    posix_xattr_filler_t *filler)
{
        int32_t               list_offset           = 0;
        ssize_t               remaining_size        = 0;
        char                  *key                  = NULL;

        list_offset = 0;
        remaining_size = filler->list_size;
        while (remaining_size > 0) {
                key = filler->list + list_offset;

                if (gf_get_index_by_elem (list_xattr_ignore_xattrs, key) >= 0)
                        goto next;

                if (posix_special_xattr (marker_xattrs, key))
                        goto next;

                if (posix_handle_georep_xattrs (NULL, key, NULL, _gf_false))
                        goto next;

                if (posix_is_gfid2path_xattr (key))
                        goto next;

                if (dict_get (filler->xattr, key))
                        goto next;

                (void) _posix_xattr_get_set_from_backend (filler, key);
next:
                remaining_size -= strlen (key) + 1;
                list_offset += strlen (key) + 1;

        } /* while (remaining_size > 0) */
        return;
}

dict_t *
posix_xattr_fill (xlator_t *this, const char *real_path, loc_t *loc, fd_t *fd,
                  int fdnum, dict_t *xattr_req, struct iatt *buf)
{
        dict_t     *xattr             = NULL;
        posix_xattr_filler_t filler   = {0, };
        gf_boolean_t    list          = _gf_false;

        if (dict_get (xattr_req, "list-xattr")) {
                dict_del (xattr_req, "list-xattr");
                list = _gf_true;
        }

        xattr = dict_new ();
        if (!xattr) {
                goto out;
        }

        filler.this      = this;
        filler.real_path = real_path;
        filler.xattr     = xattr;
        filler.stbuf     = buf;
        filler.loc       = loc;
        filler.fd        = fd;
        filler.fdnum     = fdnum;

        _get_list_xattr (&filler);
        dict_foreach (xattr_req, _posix_xattr_get_set, &filler);
        if (list)
                _handle_list_xattr (xattr_req, real_path, fdnum, &filler);

        GF_FREE (filler.list);
out:
        return xattr;
}

void
posix_gfid_unset (xlator_t *this, dict_t *xdata)
{
        uuid_t uuid = {0, };
        int    ret  = 0;

        if (xdata == NULL)
                goto out;

        ret = dict_get_gfuuid (xdata, "gfid-req", &uuid);
        if (ret) {
                goto out;
        }

        posix_handle_unset (this, uuid, NULL);
out:
        return;
}

int
posix_gfid_set (xlator_t *this, const char *path, loc_t *loc, dict_t *xattr_req)
{
        uuid_t       uuid_req;
        uuid_t       uuid_curr;
        int          ret = 0;
        ssize_t      size = 0;
        struct stat  stat = {0, };


        if (!xattr_req)
                goto out;

        if (sys_lstat (path, &stat) != 0)
                goto out;

        size = sys_lgetxattr (path, GFID_XATTR_KEY, uuid_curr, 16);
        if (size == 16) {
                ret = 0;
                goto verify_handle;
        }

        ret = dict_get_gfuuid (xattr_req, "gfid-req", &uuid_req);
        if (ret) {
                gf_msg_debug (this->name, 0,
                        "failed to get the gfid from dict for %s",
                        loc->path);
                goto out;
        }
        if (gf_uuid_is_null (uuid_req)) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL, P_MSG_NULL_GFID,
                        "gfid is null for %s", loc ? loc->path : "");
                ret = -1;
                goto out;
        }

        ret = sys_lsetxattr (path, GFID_XATTR_KEY, uuid_req, 16, XATTR_CREATE);
        if (ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING, errno, P_MSG_GFID_FAILED,
                        "setting GFID on %s failed ", path);
                goto out;
        }
        gf_uuid_copy (uuid_curr, uuid_req);

verify_handle:
        if (!S_ISDIR (stat.st_mode))
                ret = posix_handle_hard (this, path, uuid_curr, &stat);
        else
                ret = posix_handle_soft (this, path, loc, uuid_curr, &stat);

out:
        return ret;
}


#ifdef HAVE_SYS_ACL_H
int
posix_pacl_set (const char *path, const char *key, const char *acl_s)
{
        int ret = -1;
        acl_t acl = NULL;
        acl_type_t type = 0;

        type = gf_posix_acl_get_type (key);

        acl = acl_from_text (acl_s);
        ret = acl_set_file (path, type, acl);
        if (ret)
                /* posix_handle_pair expects ret to be the errno */
                ret = -errno;

        acl_free (acl);

        return ret;
}

int
posix_pacl_get (const char *path, const char *key, char **acl_s)
{
        int ret = -1;
        acl_t acl = NULL;
        acl_type_t type = 0;
        char *acl_tmp = NULL;

        type = gf_posix_acl_get_type (key);
        if (!type)
                return -1;

        acl = acl_get_file (path, type);
        if (!acl)
                return -1;

#ifdef HAVE_ACL_LIBACL_H
        acl_tmp = acl_to_any_text (acl, NULL, ',',
                                   TEXT_ABBREVIATE | TEXT_NUMERIC_IDS);
#else /* FreeBSD and the like */
        acl_tmp = acl_to_text_np (acl, NULL, ACL_TEXT_NUMERIC_IDS);
#endif
        if (!acl_tmp)
                goto free_acl;

        *acl_s = gf_strdup (acl_tmp);
        if (*acl_s)
                ret = 0;

        acl_free (acl_tmp);
free_acl:
        acl_free (acl);

        return ret;
}
#else /* !HAVE_SYS_ACL_H (NetBSD) */
int
posix_pacl_set (const char *path, const char *key, const char *acl_s)
{
        errno = ENOTSUP;
        return -1;
}

int
posix_pacl_get (const char *path, const char *key, char **acl_s)
{
        errno = ENOTSUP;
        return -1;
}
#endif


#ifdef GF_DARWIN_HOST_OS
static
void posix_dump_buffer (xlator_t *this, const char *real_path, const char *key,
                        data_t *value, int flags)
{
        char buffer[3*value->len+1];
        int index = 0;
        buffer[0] = 0;
        gf_loglevel_t log_level = gf_log_get_loglevel ();
        if (log_level == GF_LOG_TRACE) {
                char *data = (char *) value->data;
                for (index = 0; index < value->len; index++)
                        sprintf(buffer+3*index, " %02x", data[index]);
        }
        gf_msg_debug (this->name, 0,
                "Dump %s: key:%s flags: %u length:%u data:%s ",
                real_path, key, flags, value->len,
                (log_level == GF_LOG_TRACE ? buffer : "<skipped in DEBUG>"));
}
#endif

int
posix_handle_pair (xlator_t *this, const char *real_path,
                   char *key, data_t *value, int flags, struct iatt *stbuf)
{
        int sys_ret = -1;
        int ret     = 0;
#ifdef GF_DARWIN_HOST_OS
        const int error_code = EINVAL;
#else
        const int error_code = EEXIST;
#endif

        if (XATTR_IS_PATHINFO (key)) {
                ret = -EACCES;
                goto out;
        } else if (posix_is_gfid2path_xattr (key)) {
                ret = -ENOTSUP;
                goto out;
        } else if (GF_POSIX_ACL_REQUEST (key)) {
                if (stbuf && IS_DHT_LINKFILE_MODE (stbuf))
                        goto out;
                ret = posix_pacl_set (real_path, key, value->data);
        } else if (!strncmp(key, POSIX_ACL_ACCESS_XATTR, strlen(key))
                   && stbuf && IS_DHT_LINKFILE_MODE (stbuf)) {
                goto out;
        } else if (!strncmp(key, GF_INTERNAL_CTX_KEY, strlen(key))) {
                /* ignore this key value pair */
                ret = 0;
                goto out;
        } else {
                sys_ret = sys_lsetxattr (real_path, key, value->data,
                                         value->len, flags);
#ifdef GF_DARWIN_HOST_OS
                posix_dump_buffer(this, real_path, key, value, flags);
#endif
                if (sys_ret < 0) {
                        ret = -errno;
                        if (errno == ENOENT) {
                                if (!posix_special_xattr (marker_xattrs,
                                                          key)) {
                                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                                P_MSG_XATTR_FAILED,
                                                "setxattr on %s failed",
                                                real_path);
                                }
                        } else {
                                if (errno == error_code) {
                                        gf_msg_debug (this->name, 0,
                                                      "%s: key:%s"
                                                      "flags: %u length:%d",
                                                      real_path, key, flags,
                                                      value->len);
                                } else {
                                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                                P_MSG_XATTR_FAILED, "%s: key:%s"
                                                "flags: %u length:%d",
                                                real_path, key, flags,
                                                value->len);
                                }
                        }

                        goto out;
                }
        }
out:
        return ret;
}

int
posix_fhandle_pair (call_frame_t *frame, xlator_t *this, int fd, char *key,
                    data_t *value, int flags, struct iatt *stbuf, fd_t *_fd)
{
        int sys_ret = -1;
        int ret     = 0;

        if (XATTR_IS_PATHINFO (key)) {
                ret = -EACCES;
                goto out;
        } else if (posix_is_gfid2path_xattr (key)) {
                ret = -ENOTSUP;
                goto out;
        } else if (!strncmp(key, POSIX_ACL_ACCESS_XATTR, strlen(key))
                   && stbuf && IS_DHT_LINKFILE_MODE (stbuf)) {
                goto out;
        }

        sys_ret = sys_fsetxattr (fd, key, value->data,
                                 value->len, flags);

        if (sys_ret < 0) {
                ret = -errno;
                if (errno == ENOENT) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_XATTR_FAILED, "fsetxattr on fd=%d"
                                " failed", fd);
                } else {

#ifdef GF_DARWIN_HOST_OS
                        if (errno == EINVAL) {
                                gf_msg_debug (this->name, 0, "fd=%d: key:%s "
                                              "error:%s", fd, key,
                                              strerror (errno));
                        } else {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        P_MSG_XATTR_FAILED, "fd=%d: key:%s",
                                        fd, key);
                        }

#else /* ! DARWIN */
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_XATTR_FAILED, "fd=%d: key:%s",
                                fd, key);
#endif /* DARWIN */
                }

                goto out;
        } else if (_fd) {
                posix_set_ctime (frame, this, NULL, fd, _fd->inode, NULL);
        }

out:
        return ret;
}

static void
del_stale_dir_handle (xlator_t *this, uuid_t gfid)
{
        char    newpath[PATH_MAX] = {0, };
        uuid_t       gfid_curr = {0, };
        ssize_t      size = -1;
        gf_boolean_t stale = _gf_false;
        char         *hpath = NULL;
        struct stat  stbuf = {0, };
        struct iatt  iabuf = {0, };

        MAKE_HANDLE_GFID_PATH (hpath, this, gfid, NULL);

        /* check that it is valid directory handle */
        size = sys_lstat (hpath, &stbuf);
        if (size < 0) {
                gf_msg_debug (this->name, 0, "%s: Handle stat failed: "
                        "%s", hpath, strerror (errno));
                goto out;
        }

        iatt_from_stat (&iabuf, &stbuf);
        if (iabuf.ia_nlink != 1 || !IA_ISLNK (iabuf.ia_type)) {
                gf_msg_debug (this->name, 0, "%s: Handle nlink %d %d",
                        hpath, iabuf.ia_nlink, IA_ISLNK (iabuf.ia_type));
                goto out;
        }

        size = posix_handle_path (this, gfid, NULL, newpath, sizeof (newpath));
        if (size <= 0) {
                if (errno == ENOENT) {
                        gf_msg_debug (this->name, 0, "%s: %s", newpath,
                                strerror (ENOENT));
                        stale = _gf_true;
                }
                goto out;
        }

        size = sys_lgetxattr (newpath, GFID_XATTR_KEY, gfid_curr, 16);
        if (size < 0 && errno == ENOENT) {
                gf_msg_debug (this->name, 0, "%s: %s", newpath,
                        strerror (ENOENT));
                stale = _gf_true;
        } else if (size == 16 && gf_uuid_compare (gfid, gfid_curr)) {
                gf_msg_debug (this->name, 0, "%s: mismatching gfid: %s, "
                              "at %s", hpath, uuid_utoa (gfid_curr), newpath);
                stale = _gf_true;
        }

out:
        if (stale) {
                size = sys_unlink (hpath);
                if (size < 0 && errno != ENOENT)
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_STALE_HANDLE_REMOVE_FAILED, "%s: Failed"
                                "to remove handle to %s", hpath, newpath);
        } else if (size == 16) {
                gf_msg_debug (this->name, 0, "%s: Fresh handle for "
                        "%s with gfid %s", hpath, newpath,
                        uuid_utoa (gfid_curr));
        }
        return;
}

static int
janitor_walker (const char *fpath, const struct stat *sb,
                int typeflag, struct FTW *ftwbuf)
{
        struct iatt  stbuf        = {0, };
        xlator_t     *this = NULL;

        this = THIS;
        /* posix_mdata_t is not filled, no time or size attributes
         * are being used, so fine.
         */
        posix_pstat (this, NULL, NULL, fpath, &stbuf, _gf_false);
        switch (sb->st_mode & S_IFMT) {
        case S_IFREG:
        case S_IFBLK:
        case S_IFLNK:
        case S_IFCHR:
        case S_IFIFO:
        case S_IFSOCK:
                gf_msg_trace (THIS->name, 0,
                        "unlinking %s", fpath);
                sys_unlink (fpath);
                if (stbuf.ia_nlink == 1)
                        posix_handle_unset (this, stbuf.ia_gfid, NULL);
                break;

        case S_IFDIR:
                if (ftwbuf->level) { /* don't remove top level dir */
                        gf_msg_debug (THIS->name, 0,
                                "removing directory %s", fpath);

                        sys_rmdir (fpath);
                        del_stale_dir_handle (this, stbuf.ia_gfid);
                }
                break;
        }

        return 0;   /* 0 = FTW_CONTINUE */
}


static struct posix_fd *
janitor_get_next_fd (xlator_t *this)
{
        struct posix_private *priv = NULL;
        struct posix_fd *pfd = NULL;

        struct timespec timeout;

        priv = this->private;

        pthread_mutex_lock (&priv->janitor_lock);
        {
                if (list_empty (&priv->janitor_fds)) {
                        time (&timeout.tv_sec);
                        timeout.tv_sec += priv->janitor_sleep_duration;
                        timeout.tv_nsec = 0;

                        pthread_cond_timedwait (&priv->janitor_cond,
                                                &priv->janitor_lock,
                                                &timeout);
                        goto unlock;
                }

                pfd = list_entry (priv->janitor_fds.next, struct posix_fd,
                                  list);

                list_del (priv->janitor_fds.next);
        }
unlock:
        pthread_mutex_unlock (&priv->janitor_lock);

        return pfd;
}


static void *
posix_janitor_thread_proc (void *data)
{
        xlator_t *            this = NULL;
        struct posix_private *priv = NULL;
        struct posix_fd *pfd;

        time_t now;

        this = data;
        priv = this->private;

        THIS = this;

        while (1) {

                time (&now);
                if ((now - priv->last_landfill_check) > priv->janitor_sleep_duration) {
                        if (priv->disable_landfill_purge) {
                                gf_msg_debug (this->name, 0,
                                              "Janitor would have "
                                              "cleaned out %s, but purge"
                                              "is disabled.",
                                              priv->trash_path);
                        } else {
                                gf_msg_trace (this->name, 0,
                                              "janitor cleaning out %s",
                                              priv->trash_path);

                                nftw (priv->trash_path,
                                      janitor_walker,
                                      32,
                                      FTW_DEPTH | FTW_PHYS);
                        }
                        priv->last_landfill_check = now;
                }

                pfd = janitor_get_next_fd (this);
                if (pfd) {
                        if (pfd->dir == NULL) {
                                gf_msg_trace (this->name, 0,
                                        "janitor: closing file fd=%d", pfd->fd);
                                sys_close (pfd->fd);
                        } else {
                                gf_msg_debug (this->name, 0, "janitor: closing"
                                              " dir fd=%p", pfd->dir);
                                sys_closedir (pfd->dir);
                        }

                        GF_FREE (pfd);
                }
        }

        return NULL;
}


void
posix_spawn_janitor_thread (xlator_t *this)
{
        struct posix_private *priv = NULL;
        int ret = 0;

        priv = this->private;

        LOCK (&priv->lock);
        {
                if (!priv->janitor_present) {
                        ret = gf_thread_create (&priv->janitor, NULL,
                                                posix_janitor_thread_proc,
                                                this, "posixjan");

                        if (ret < 0) {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        P_MSG_THREAD_FAILED, "spawning janitor "
                                        "thread failed");
                                goto unlock;
                        }

                        priv->janitor_present = _gf_true;
                }
        }
unlock:
        UNLOCK (&priv->lock);
}

static int
is_fresh_file (int64_t ctime_sec)
{
        struct timeval tv;

        gettimeofday (&tv, NULL);

        if ((ctime_sec >= (tv.tv_sec - 1))
            && (ctime_sec <= tv.tv_sec))
                return 1;

        return 0;
}


int
posix_gfid_heal (xlator_t *this, const char *path, loc_t *loc, dict_t *xattr_req)
{
        /* The purpose of this function is to prevent a race
           where an inode creation FOP (like mkdir/mknod/create etc)
           races with lookup in the following way:

                   {create thread}       |    {lookup thread}
                                         |
                                         t0
                      mkdir ("name")     |
                                         t1
                                         |     posix_gfid_set ("name", 2);
                                         t2
             posix_gfid_set ("name", 1); |
                                         t3
                      lstat ("name");    |     lstat ("name");

          In the above case mkdir FOP would have resulted with GFID 2 while
          it should have been GFID 1. It matters in the case where GFID would
          have gotten set to 1 on other subvolumes of replciate/distribute

          The "solution" here is that, if we detect lookup is attempting to
          set a GFID on a file which is created very recently, but does not
          yet have a GFID (i.e, between t1 and t2), then "fake" it as though
          posix_gfid_heal was called at t0 instead.
        */

        uuid_t       uuid_curr;
        int          ret = 0;
        struct stat  stat = {0, };
        struct iatt  stbuf  = {0, };
        struct posix_private *priv = NULL;

        priv = this->private;

        if (!xattr_req)
                return 0;

        if (loc->inode && priv->ctime) {
                if (sys_lstat (path, &stat) != 0) {
                        return -errno;
                }
                /* stbuf is only to compare ctime, don't use it to access
                 * other fields as they are zero. */
                ret = posix_get_mdata_xattr (this, path, -1, loc->inode,
                                             &stbuf);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                P_MSG_GETMDATA_FAILED,
                                "posix get mdata failed on gfid: %s",
                                uuid_utoa(loc->inode->gfid));
                        return -ENOENT;
                }
                ret = sys_lgetxattr (path, GFID_XATTR_KEY, uuid_curr, 16);
                if (ret != 16) {
                        if (is_fresh_file (stbuf.ia_ctime)) {
                                gf_msg (this->name, GF_LOG_ERROR, ENOENT,
                                        P_MSG_FRESHFILE,
                                        "Fresh file: %s", path);
                                return -ENOENT;
                        }
                }
        } else {
                if (sys_lstat (path, &stat) != 0) {
                        return -errno;
                }
                ret = sys_lgetxattr (path, GFID_XATTR_KEY, uuid_curr, 16);
                if (ret != 16) {
                        if (is_fresh_file (stat.st_ctime)) {
                                gf_msg (this->name, GF_LOG_ERROR, ENOENT,
                                        P_MSG_FRESHFILE,
                                        "Fresh file: %s", path);
                                return -ENOENT;
                        }
                }
        }

        posix_gfid_set (this, path, loc, xattr_req);
        return 0;
}


int
posix_acl_xattr_set (xlator_t *this, const char *path, dict_t *xattr_req)
{
        int          ret = 0;
        data_t      *data = NULL;
        struct stat  stat = {0, };

        if (!xattr_req)
                goto out;

        if (sys_lstat (path, &stat) != 0)
                goto out;

        data = dict_get (xattr_req, POSIX_ACL_ACCESS_XATTR);
        if (data) {
                ret = sys_lsetxattr (path, POSIX_ACL_ACCESS_XATTR,
                                     data->data, data->len, 0);
#ifdef __FreeBSD__
                if (ret != -1) {
                        ret = 0;
                }
#endif /* __FreeBSD__ */
                if (ret != 0)
                        goto out;
        }

        data = dict_get (xattr_req, POSIX_ACL_DEFAULT_XATTR);
        if (data) {
                ret = sys_lsetxattr (path, POSIX_ACL_DEFAULT_XATTR,
                                     data->data, data->len, 0);
#ifdef __FreeBSD__
                if (ret != -1) {
                        ret = 0;
                }
#endif /* __FreeBSD__ */
                if (ret != 0)
                        goto out;
        }

out:
        return ret;
}

static int
_handle_entry_create_keyvalue_pair (dict_t *d, char *k, data_t *v,
                                    void *tmp)
{
        int                   ret = -1;
        posix_xattr_filler_t *filler = NULL;

        filler = tmp;

        if (!strcmp (GFID_XATTR_KEY, k) ||
            !strcmp ("gfid-req", k) ||
            !strcmp (POSIX_ACL_DEFAULT_XATTR, k) ||
            !strcmp (POSIX_ACL_ACCESS_XATTR, k) ||
            posix_xattr_ignorable (k)) {
                return 0;
        }

        ret = posix_handle_pair (filler->this, filler->real_path, k, v,
                                 XATTR_CREATE, filler->stbuf);
        if (ret < 0) {
                errno = -ret;
                return -1;
        }
        return 0;
}

int
posix_entry_create_xattr_set (xlator_t *this, const char *path,
                             dict_t *dict)
{
        int ret = -1;

        posix_xattr_filler_t filler = {0,};

        if (!dict)
                goto out;

        filler.this = this;
        filler.real_path = path;
        filler.stbuf = NULL;

        ret = dict_foreach (dict, _handle_entry_create_keyvalue_pair, &filler);

out:
        return ret;
}

static int
__posix_fd_ctx_get (fd_t *fd, xlator_t *this, struct posix_fd **pfd_p,
                    int *op_errno_p)
{
        uint64_t          tmp_pfd = 0;
        struct posix_fd  *pfd = NULL;
        int               ret = -1;
        char             *real_path = NULL;
        char             *unlink_path = NULL;
        int               _fd = -1;
        int               op_errno = 0;
        DIR              *dir = NULL;

        struct posix_private    *priv      = NULL;

        priv = this->private;

        ret = __fd_ctx_get (fd, this, &tmp_pfd);
        if (ret == 0) {
                pfd = (void *)(long) tmp_pfd;
                goto out;
        }
        if (!fd_is_anonymous(fd)) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        P_MSG_READ_FAILED,
                        "Failed to get fd context for a non-anonymous fd, "
                        "file: %s, gfid: %s", real_path,
                        uuid_utoa (fd->inode->gfid));
                op_errno = EINVAL;
                goto out;
        }

        MAKE_HANDLE_PATH (real_path, this, fd->inode->gfid, NULL);
        if (!real_path) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        P_MSG_READ_FAILED,
                        "Failed to create handle path (%s)",
                        uuid_utoa (fd->inode->gfid));
                ret = -1;
                op_errno = EINVAL;
                goto out;
        }
        pfd = GF_CALLOC (1, sizeof (*pfd), gf_posix_mt_posix_fd);
        if (!pfd) {
                op_errno = ENOMEM;
                goto out;
        }
        pfd->fd = -1;

        if (fd->inode->ia_type == IA_IFDIR) {
                dir = sys_opendir (real_path);
                if (!dir) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, op_errno,
                                P_MSG_READ_FAILED,
                                "Failed to get anonymous fd for "
                                "real_path: %s.", real_path);
                        GF_FREE (pfd);
                        pfd = NULL;
                        goto out;
                }
                _fd = dirfd (dir);
        }

        /* Using fd->flags in case we choose to have anonymous
         * fds with different flags some day. As of today it
         * would be GF_ANON_FD_FLAGS and nothing else.
         */
        if (fd->inode->ia_type == IA_IFREG) {
                _fd = open (real_path, fd->flags);
                if ((_fd == -1) && (errno == ENOENT)) {
                        POSIX_GET_FILE_UNLINK_PATH (priv->base_path,
                                                    fd->inode->gfid,
                                                    unlink_path);
                        _fd = open (unlink_path, fd->flags);
                }
                if (_fd == -1) {
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, op_errno,
                                P_MSG_READ_FAILED,
                                "Failed to get anonymous fd for "
                                "real_path: %s.", real_path);
                        GF_FREE (pfd);
                        pfd = NULL;
                        goto out;
                }
        }

        pfd->fd = _fd;
        pfd->dir = dir;
        pfd->flags = fd->flags;

        ret = __fd_ctx_set (fd, this, (uint64_t) (long) pfd);
        if (ret != 0) {
                op_errno = ENOMEM;
                if (_fd != -1)
                        sys_close (_fd);
                if (dir)
                        sys_closedir (dir);
                GF_FREE (pfd);
                pfd = NULL;
                goto out;
        }

        ret = 0;
out:
        if (ret < 0 && op_errno_p)
                *op_errno_p = op_errno;

        if (pfd_p)
                *pfd_p = pfd;
        return ret;
}


int
posix_fd_ctx_get (fd_t *fd, xlator_t *this, struct posix_fd **pfd,
                  int *op_errno)
{
        int   ret;

        LOCK (&fd->inode->lock);
        {
                ret = __posix_fd_ctx_get (fd, this, pfd, op_errno);
        }
        UNLOCK (&fd->inode->lock);

        return ret;
}

int
posix_fs_health_check (xlator_t *this)
{
        struct  posix_private *priv     = NULL;
        int     ret                     = -1;
        char    *subvol_path            = NULL;
        char    timestamp[256]          = {0,};
        int     fd                      = -1;
        int     timelen                 = -1;
        time_t  time_sec                = {0,};
        char    buff[256]               = {0};
        char    file_path[PATH_MAX]     = {0};
        char    *op                     = NULL;
        int     op_errno                = 0;
        int     cnt                     = 0;
        int     timeout                 = 0;
        struct  aiocb aiocb;

        GF_VALIDATE_OR_GOTO (this->name, this, out);
        priv = this->private;
        GF_VALIDATE_OR_GOTO ("posix-helpers", priv, out);

        subvol_path = priv->base_path;
        timeout = priv->health_check_timeout;
        snprintf (file_path, sizeof (file_path)-1, "%s/%s/health_check",
                  subvol_path, GF_HIDDEN_PATH);

        time_sec = time (NULL);
        gf_time_fmt (timestamp, sizeof timestamp, time_sec, gf_timefmt_FT);
        timelen = strlen (timestamp);

        fd = open (file_path, O_CREAT|O_WRONLY|O_TRUNC, 0644);
        if (fd == -1) {
                op_errno = errno;
                op = "open_for_write";
                goto out;
        }
        memset(&aiocb, 0, sizeof(struct aiocb));
        aiocb.aio_fildes = fd;
        aiocb.aio_buf = timestamp;
        aiocb.aio_nbytes = timelen;
        aiocb.aio_sigevent.sigev_notify = SIGEV_NONE;
        if (aio_write(&aiocb) == -1) {
                op_errno = errno;
                op  = "aio_write";
                goto out;
        }

        /* Wait until write completion */
        while ((aio_error (&aiocb) == EINPROGRESS) && (++cnt <= timeout))
                sleep (1);

        ret = aio_error (&aiocb);
        if (ret != 0) {
                op_errno = errno;
                op  = "aio_write_error";
                ret = -1;
                goto out;
        }

        ret = aio_return (&aiocb);
        if (ret != timelen) {
                op_errno = errno;
                op  = "aio_write_buf";
                ret = -1;
                goto out;
        }

        sys_close (fd);

        fd = open (file_path, O_RDONLY);
        if (fd == -1) {
                op_errno = errno;
                op = "open_for_read";
                goto out;
        }

        memset(&aiocb, 0, sizeof(struct aiocb));
        aiocb.aio_fildes = fd;
        aiocb.aio_buf = buff;
        aiocb.aio_nbytes = sizeof(buff);
        if (aio_read(&aiocb) == -1) {
                op_errno = errno;
                op  = "aio_read";
                goto out;
        }
        cnt = 0;
        /* Wait until read completion */
        while ((aio_error (&aiocb) == EINPROGRESS) && (++cnt <= timeout))
                sleep (1);

        ret = aio_error (&aiocb);
        if (ret != 0) {
                op_errno = errno;
                op  = "aio_read_error";
                ret = -1;
                goto out;
        }

        ret = aio_return (&aiocb);
        if (ret != timelen) {
                op_errno = errno;
                op  = "aio_read_buf";
                ret = -1;
                goto out;
        }

        if (memcmp (timestamp, buff, ret)) {
                op_errno = EUCLEAN;
                op  = "aio_read_cmp_buf";
                ret = -1;
                goto out;
        }
        ret = 0;
out:
        if (fd != -1) {
                sys_close (fd);
        }
        if (ret && file_path[0]) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        P_MSG_HEALTHCHECK_FAILED,
                        "%s() on %s returned", op, file_path);
                gf_event (EVENT_POSIX_HEALTH_CHECK_FAILED,
                          "op=%s;path=%s;error=%s;brick=%s:%s timeout is %d",
                          op, file_path, strerror (op_errno), priv->hostname,
                          priv->base_path, timeout);
        }
        return ret;

}

static void *
posix_health_check_thread_proc (void *data)
{
        xlator_t             *this               = NULL;
        struct posix_private *priv               = NULL;
        uint32_t              interval           = 0;
        int                   ret                = -1;
        xlator_t             *top                = NULL;
        xlator_t             *victim             = NULL;
        xlator_list_t       **trav_p             = NULL;
        int                   count              = 0;
        gf_boolean_t          victim_found       = _gf_false;
        glusterfs_ctx_t      *ctx                = NULL;

        this = data;
        priv = this->private;
        ctx  = THIS->ctx;

        /* prevent races when the interval is updated */
        interval = priv->health_check_interval;
        if (interval == 0)
                goto out;

        gf_msg_debug (this->name, 0, "health-check thread started, "
                "interval = %d seconds", interval);
        while (1) {
                /* aborting sleep() is a request to exit this thread, sleep()
                 * will normally not return when cancelled */
                ret = sleep (interval);
                if (ret > 0)
                        break;
                /* prevent thread errors while doing the health-check(s) */
                pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);

                /* Do the health-check.*/
                ret = posix_fs_health_check (this);
                if (ret < 0 && priv->health_check_active)
                        goto abort;
                if (!priv->health_check_active)
                        goto out;
                pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
        }

out:
        gf_msg_debug (this->name, 0, "health-check thread exiting");

        LOCK (&priv->lock);
        {
                priv->health_check_active = _gf_false;
        }
        UNLOCK (&priv->lock);

        return NULL;

abort:
        /* health-check failed */
        gf_msg (this->name, GF_LOG_EMERG, 0, P_MSG_HEALTHCHECK_FAILED,
                "health-check failed, going down");

        xlator_notify (this->parents->xlator, GF_EVENT_CHILD_DOWN, this);

        /* Below code is use to ensure if brick multiplexing is enabled if
           count is more than 1 it means brick mux has enabled
        */
        if (this->ctx->active) {
                top = this->ctx->active->first;
                LOCK (&ctx->volfile_lock);
                        for (trav_p = &top->children; *trav_p;
                                               trav_p = &(*trav_p)->next) {
                                count++;
                        }
                UNLOCK (&ctx->volfile_lock);
        }

        if (count == 1) {
                gf_msg (this->name, GF_LOG_EMERG, 0, P_MSG_HEALTHCHECK_FAILED,
                        "still alive! -> SIGTERM");
                ret = sleep (30);

                /* Need to kill the process only while brick mux has not enabled
                */
                if (ret == 0)
                        kill (getpid(), SIGTERM);

                ret = sleep (30);
                gf_msg (this->name, GF_LOG_EMERG, 0, P_MSG_HEALTHCHECK_FAILED,
                        "still alive! -> SIGKILL");
                if (ret == 0)
                        kill (getpid(), SIGKILL);

        } else {
                LOCK (&ctx->volfile_lock);
                        for (trav_p = &top->children; *trav_p;
                             trav_p = &(*trav_p)->next) {
                                victim = (*trav_p)->xlator;
                                if (victim &&
                                    strcmp (victim->name, priv->base_path) == 0) {
                                        victim_found = _gf_true;
                                        break;
                                }
                        }
                UNLOCK (&ctx->volfile_lock);
                if (victim_found) {
                        gf_log (THIS->name, GF_LOG_INFO, "detaching not-only "
                                " child %s", priv->base_path);
                        top->notify (top, GF_EVENT_CLEANUP, victim);
                }
        }

        return NULL;
}

void
posix_spawn_health_check_thread (xlator_t *xl)
{
        struct posix_private *priv               = NULL;
        int                   ret                = -1;

        priv = xl->private;

        LOCK (&priv->lock);
        {
                /* cancel the running thread  */
                if (priv->health_check_active == _gf_true) {
                        pthread_cancel (priv->health_check);
                        priv->health_check_active = _gf_false;
                }

                /* prevent scheduling a check in a tight loop */
                if (priv->health_check_interval == 0)
                        goto unlock;

                ret = gf_thread_create (&priv->health_check, NULL,
                                        posix_health_check_thread_proc,
                                        xl, "posixhc");
                if (ret < 0) {
                        priv->health_check_interval = 0;
                        priv->health_check_active = _gf_false;
                        gf_msg (xl->name, GF_LOG_ERROR, errno,
                                P_MSG_HEALTHCHECK_FAILED,
                                "unable to setup health-check thread");
                        goto unlock;
                }

                /* run the thread detached, resources will be freed on exit */
                pthread_detach (priv->health_check);
                priv->health_check_active = _gf_true;
        }
unlock:
        UNLOCK (&priv->lock);
}

void
posix_disk_space_check (xlator_t *this)
{
        struct  posix_private *priv     = NULL;
        char    *subvol_path            = NULL;
        int     op_ret                  = 0;
        int     percent                 = 0;
        struct statvfs buf              = {0};
        uint64_t totsz                  = 0;
        uint64_t freesz                 = 0;

        GF_VALIDATE_OR_GOTO (this->name, this, out);
        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        subvol_path = priv->base_path;
        percent = priv->disk_reserve;

        op_ret = sys_statvfs (subvol_path, &buf);

        if (op_ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_STATVFS_FAILED,
                        "statvfs failed on %s", subvol_path);
                goto out;
        }
        totsz = (buf.f_blocks * buf.f_bsize);
        freesz = (buf.f_bfree * buf.f_bsize);

        if (freesz <= ((totsz * percent) / 100)) {
                priv->disk_space_full = 1;
        } else {
                priv->disk_space_full = 0;
        }
out:
        return;
}


static void *
posix_disk_space_check_thread_proc (void *data)
{
        xlator_t             *this               = NULL;
        struct posix_private *priv               = NULL;
        uint32_t              interval           = 0;
        int                   ret                = -1;

        this = data;
        priv = this->private;

        interval = 5;
        gf_msg_debug (this->name, 0, "disk-space thread started, "
                      "interval = %d seconds", interval);
        while (1) {
                /* aborting sleep() is a request to exit this thread, sleep()
                 * will normally not return when cancelled */
                ret = sleep (interval);
                if (ret > 0)
                        break;
                /* prevent thread errors while doing the health-check(s) */
                pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, NULL);

                /* Do the disk-check.*/
                posix_disk_space_check (this);
                if (!priv->disk_space_check_active)
                        goto out;
                pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
        }

out:
        gf_msg_debug (this->name, 0, "disk space check thread exiting");
        LOCK (&priv->lock);
        {
                priv->disk_space_check_active = _gf_false;
        }
        UNLOCK (&priv->lock);


        return NULL;
}

void
posix_spawn_disk_space_check_thread (xlator_t *xl)
{
        struct posix_private *priv               = NULL;
        int                   ret                = -1;

        priv = xl->private;

        LOCK (&priv->lock);
        {
                /* cancel the running thread  */
                if (priv->disk_space_check_active == _gf_true) {
                        pthread_cancel (priv->disk_space_check);
                        priv->disk_space_check_active = _gf_false;
                }

                ret = gf_thread_create_detached (&priv->disk_space_check,
                                                 posix_disk_space_check_thread_proc,
                                                 xl, "posix_reserve");
                if (ret < 0) {
                        priv->disk_space_check_active = _gf_false;
                        gf_msg (xl->name, GF_LOG_ERROR, errno,
                                P_MSG_DISK_SPACE_CHECK_FAILED,
                                "unable to setup disk space check thread");
                        goto unlock;
                }

                priv->disk_space_check_active = _gf_true;
        }
unlock:
        UNLOCK (&priv->lock);
}

int
posix_fsyncer_pick (xlator_t *this, struct list_head *head)
{
        struct posix_private *priv = NULL;
        int count = 0;

        priv = this->private;
        pthread_mutex_lock (&priv->fsync_mutex);
        {
                while (list_empty (&priv->fsyncs))
                        pthread_cond_wait (&priv->fsync_cond,
                                           &priv->fsync_mutex);

                count = priv->fsync_queue_count;
                priv->fsync_queue_count = 0;
                list_splice_init (&priv->fsyncs, head);
        }
        pthread_mutex_unlock (&priv->fsync_mutex);

        return count;
}


void
posix_fsyncer_process (xlator_t *this, call_stub_t *stub, gf_boolean_t do_fsync)
{
        struct posix_fd *pfd = NULL;
        int ret = -1;
        int op_errno = 0;

        ret = posix_fd_ctx_get (stub->args.fd, this, &pfd, &op_errno);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, op_errno,
                        P_MSG_GET_FDCTX_FAILED,
                        "could not get fdctx for fd(%s)",
                        uuid_utoa (stub->args.fd->inode->gfid));
                call_unwind_error (stub, -1, op_errno);
                return;
        }

        if (do_fsync) {
                if (stub->args.datasync)
                        ret = sys_fdatasync (pfd->fd);
                else
                        ret = sys_fsync (pfd->fd);
        } else {
                ret = 0;
        }

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno, P_MSG_FSTAT_FAILED,
                        "could not fstat fd(%s)",
                        uuid_utoa (stub->args.fd->inode->gfid));
                call_unwind_error (stub, -1, errno);
                return;
        }

        call_unwind_error (stub, 0, 0);
}


static void
posix_fsyncer_syncfs (xlator_t *this, struct list_head *head)
{
        call_stub_t *stub = NULL;
        struct posix_fd *pfd = NULL;
        int ret = -1;

        stub = list_entry (head->prev, call_stub_t, list);
        ret = posix_fd_ctx_get (stub->args.fd, this, &pfd, NULL);
        if (ret)
                return;

#ifdef GF_LINUX_HOST_OS
        /* syncfs() is not "declared" in RHEL's glibc even though
           the kernel has support.
        */
#include <sys/syscall.h>
#include <unistd.h>
#ifdef SYS_syncfs
        syscall (SYS_syncfs, pfd->fd);
#else
        sync();
#endif
#else
        sync();
#endif
}


void *
posix_fsyncer (void *d)
{
        xlator_t *this = d;
        struct posix_private *priv = NULL;
        call_stub_t *stub = NULL;
        call_stub_t *tmp = NULL;
        struct list_head list;
        int count = 0;
        gf_boolean_t do_fsync = _gf_true;

        priv = this->private;

        for (;;) {
                INIT_LIST_HEAD (&list);

                count = posix_fsyncer_pick (this, &list);

                usleep (priv->batch_fsync_delay_usec);

                gf_msg_debug (this->name, 0,
                        "picked %d fsyncs", count);

                switch (priv->batch_fsync_mode) {
                case BATCH_NONE:
                case BATCH_REVERSE_FSYNC:
                        break;
                case BATCH_SYNCFS:
                case BATCH_SYNCFS_SINGLE_FSYNC:
                case BATCH_SYNCFS_REVERSE_FSYNC:
                        posix_fsyncer_syncfs (this, &list);
                        break;
                }

                if (priv->batch_fsync_mode == BATCH_SYNCFS)
                        do_fsync = _gf_false;
                else
                        do_fsync = _gf_true;

                list_for_each_entry_safe_reverse (stub, tmp, &list, list) {
                        list_del_init (&stub->list);

                        posix_fsyncer_process (this, stub, do_fsync);

                        if (priv->batch_fsync_mode == BATCH_SYNCFS_SINGLE_FSYNC)
                                do_fsync = _gf_false;
                }
        }
}

/**
 * TODO: move fd/inode interfaces into a single routine..
 */
static int32_t
posix_fetch_signature_xattr (char *real_path,
                             const char *key, dict_t *xattr, size_t *xsize)
{
        int32_t ret = 0;
        char    *memptr    = NULL;
        ssize_t  xattrsize = 0;
        char     val_buf[2048] = {0,};
        gf_boolean_t have_val  = _gf_false;

        xattrsize = sys_lgetxattr (real_path, key, val_buf,
                                                           sizeof(val_buf) - 1);
        if (xattrsize >= 0) {
                have_val = _gf_true;
        } else {
                if (errno == ERANGE)
                        xattrsize = sys_lgetxattr (real_path, key, NULL, 0);
                if ((errno == ENOATTR) || (errno == ENODATA))
                        return 0;
                if (xattrsize == -1)
                        goto error_return;
        }
        memptr = GF_CALLOC (xattrsize + 1, sizeof (char), gf_posix_mt_char);
        if (!memptr)
                goto error_return;
        if (have_val) {
                memcpy (memptr, val_buf, xattrsize);
        } else {
                ret = sys_lgetxattr (real_path, key, memptr, xattrsize);
                if (ret == -1)
                        goto freemem;
        }
        ret = dict_set_dynptr (xattr, (char *)key, memptr, xattrsize);
        if (ret)
                goto freemem;

        if (xsize)
                *xsize = xattrsize;

        return 0;

 freemem:
        GF_FREE (memptr);
 error_return:
        return -1;
}

static int32_t
posix_fd_fetch_signature_xattr (int fd,
                                const char *key, dict_t *xattr, size_t *xsize)
{
        int32_t ret = 0;
        char    *memptr    = NULL;
        ssize_t  xattrsize = 0;

        xattrsize = sys_fgetxattr (fd, key, NULL, 0);
        if ((xattrsize == -1) && ((errno == ENOATTR) || (errno == ENODATA)))
                return 0;
        if (xattrsize == -1)
                goto error_return;

        memptr = GF_CALLOC (xattrsize + 1, sizeof (char), gf_posix_mt_char);
        if (!memptr)
                goto error_return;
        ret = sys_fgetxattr (fd, key, memptr, xattrsize);
        if (ret == -1)
                goto freemem;

        ret = dict_set_dynptr (xattr, (char *)key, memptr, xattrsize);
        if (ret)
                goto freemem;

        if (xsize)
                *xsize = xattrsize;

        return 0;

 freemem:
        GF_FREE (memptr);
 error_return:
        return -1;
}

/**
 * Fetch on-disk ongoing version and object signature extended attribute.
 * Be generous to absence of xattrs (just *absence*, other errors are
 * propagated up to the invoker), higher layer (br-stub) takes care of
 * interpreting the xattrs for anomalies.
 */
int32_t
posix_get_objectsignature (char *real_path, dict_t *xattr)
{
        int32_t ret = 0;
        size_t signsize = 0;

        ret = posix_fetch_signature_xattr
                (real_path, BITROT_CURRENT_VERSION_KEY, xattr, NULL);
        if (ret)
                goto error_return;

        ret = posix_fetch_signature_xattr
                (real_path, BITROT_SIGNING_VERSION_KEY, xattr, &signsize);
        if (ret)
                goto delkey1;

        ret = dict_set_uint32
                  (xattr, BITROT_SIGNING_XATTR_SIZE_KEY, (uint32_t) signsize);
        if (ret)
                goto delkey2;

        return 0;

 delkey2:
        dict_del (xattr, BITROT_SIGNING_VERSION_KEY);
 delkey1:
        dict_del (xattr, BITROT_CURRENT_VERSION_KEY);
 error_return:
        return -EINVAL;
}

int32_t
posix_fdget_objectsignature (int fd, dict_t *xattr)
{
        int32_t ret = 0;
        size_t signsize = 0;

        ret = posix_fd_fetch_signature_xattr
                (fd, BITROT_CURRENT_VERSION_KEY, xattr, NULL);
        if (ret)
                goto error_return;

        ret = posix_fd_fetch_signature_xattr
                (fd, BITROT_SIGNING_VERSION_KEY, xattr, &signsize);
        if (ret)
                goto delkey1;

        ret = dict_set_uint32
                  (xattr, BITROT_SIGNING_XATTR_SIZE_KEY, (uint32_t) signsize);
        if (ret)
                goto delkey2;

        return 0;

 delkey2:
        dict_del (xattr, BITROT_SIGNING_VERSION_KEY);
 delkey1:
        dict_del (xattr, BITROT_CURRENT_VERSION_KEY);
 error_return:
        return -EINVAL;
}

/*
 * posix_resolve_dirgfid_to_path:
 *       It converts given dirgfid to path by doing recursive readlinks at the
 *  backend. If bname is given, it suffixes bname to dir path to form the
 *  complete path else it doesn't. It allocates memory for the path and is
 *  caller's responsibility to free the same. If bname is NULL and pargfid
 *  is ROOT, then it returns "/"
 **/

int32_t
posix_resolve_dirgfid_to_path (const uuid_t dirgfid, const char *brick_path,
                               const char *bname, char **path)
{
        char             *linkname                  = NULL;
        char             *dir_handle                = NULL;
        char             *pgfidstr                  = NULL;
        char             *saveptr                   = NULL;
        ssize_t           len                       = 0;
        int               ret                       = 0;
        uuid_t            tmp_gfid                  = {0, };
        uuid_t            pargfid                   = {0, };
        char              gpath[PATH_MAX]           = {0,};
        char              result[PATH_MAX]          = {0,};
        char              result1[PATH_MAX]         = {0,};
        char             *dir_name                  = NULL;
        char              pre_dir_name[PATH_MAX]    = {0,};
        xlator_t         *this                      = NULL;

        this = THIS;
        GF_ASSERT (this);

        gf_uuid_copy (pargfid, dirgfid);
        if (!path || gf_uuid_is_null (pargfid)) {
                ret = -1;
                goto out;
        }

        if (__is_root_gfid (pargfid)) {
                if (bname) {
                        snprintf (result, PATH_MAX, "/%s", bname);
                        *path = gf_strdup (result);
                } else {
                        *path = gf_strdup ("/");
                }
                return ret;
        }

        dir_handle = alloca (PATH_MAX);
        linkname   = alloca (PATH_MAX);
        (void) snprintf (gpath, PATH_MAX, "%s/.glusterfs/", brick_path);

        while (!(__is_root_gfid (pargfid))) {
                snprintf (dir_handle, PATH_MAX, "%s/%02x/%02x/%s", gpath,
                          pargfid[0], pargfid[1], uuid_utoa (pargfid));

                len = sys_readlink (dir_handle, linkname, PATH_MAX);
                if (len < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                P_MSG_READLINK_FAILED,
                                "could not read the "
                                "link from the gfid handle %s", dir_handle);
                        ret = -1;
                        goto out;
                }

                linkname[len] = '\0';

                pgfidstr = strtok_r (linkname + strlen("../../00/00/"), "/",
                                     &saveptr);
                dir_name = strtok_r (NULL, "/", &saveptr);

                if (strlen(pre_dir_name) != 0) { /* Remove '/' at the end */
                        snprintf (result, PATH_MAX, "%s/%s", dir_name,
                                  pre_dir_name);
                } else {
                        snprintf (result, PATH_MAX, "%s", dir_name);
                }

                strncpy (pre_dir_name, result, sizeof(pre_dir_name));

                gf_uuid_parse (pgfidstr, tmp_gfid);
                gf_uuid_copy (pargfid, tmp_gfid);
        }

        if (bname) {
                snprintf (result1, PATH_MAX, "/%s/%s", result, bname);
        } else {
                snprintf (result1, PATH_MAX, "/%s", result);
        }

        *path = gf_strdup (result1);

out:
        return ret;
}

posix_inode_ctx_t *
__posix_inode_ctx_get (inode_t *inode, xlator_t *this)
{
        int                 ret      = -1;
        uint64_t            ctx_uint = 0;
        posix_inode_ctx_t  *ctx_p    = NULL;

        ret = __inode_ctx_get (inode, this, &ctx_uint);
        if (ret == 0) {
                return (posix_inode_ctx_t *)ctx_uint;
        }

        ctx_p = GF_CALLOC (1, sizeof (*ctx_p), gf_posix_mt_inode_ctx_t);
        if (!ctx_p)
                return NULL;

        pthread_mutex_init (&ctx_p->xattrop_lock, NULL);
        pthread_mutex_init (&ctx_p->write_atomic_lock, NULL);
        pthread_mutex_init (&ctx_p->pgfid_lock, NULL);

        ret = __inode_ctx_set (inode, this, (uint64_t *)&ctx_p);
        if (ret < 0) {
                pthread_mutex_destroy (&ctx_p->xattrop_lock);
                pthread_mutex_destroy (&ctx_p->write_atomic_lock);
                pthread_mutex_destroy (&ctx_p->pgfid_lock);
                GF_FREE (ctx_p);
                return NULL;
        }

        return ctx_p;
}

int
__posix_inode_ctx_set_unlink_flag (inode_t *inode, xlator_t *this, uint64_t ctx)
{
        posix_inode_ctx_t  *ctx_p    = NULL;

        ctx_p = __posix_inode_ctx_get (inode, this);
        if (ctx_p == NULL)
                return -1;

        ctx_p->unlink_flag = ctx;

        return 0;
}

int
posix_inode_ctx_set_unlink_flag (inode_t *inode, xlator_t *this, uint64_t ctx)
{
        int             ret = -1;

        LOCK(&inode->lock);
        {
                ret = __posix_inode_ctx_set_unlink_flag (inode, this, ctx);
        }
        UNLOCK(&inode->lock);

        return ret;
}

int
__posix_inode_ctx_get_all (inode_t *inode, xlator_t *this,
                           posix_inode_ctx_t **ctx)
{
        posix_inode_ctx_t  *ctx_p    = NULL;

        ctx_p = __posix_inode_ctx_get (inode, this);
        if (ctx_p == NULL)
                return -1;

        *ctx = ctx_p;

        return 0;
}

int
posix_inode_ctx_get_all (inode_t *inode, xlator_t *this,
                         posix_inode_ctx_t **ctx)
{
        int ret = 0;

        LOCK(&inode->lock);
        {
                ret = __posix_inode_ctx_get_all (inode, this, ctx);
        }
        UNLOCK(&inode->lock);

        return ret;
}

gf_boolean_t
posix_is_bulk_removexattr (char *name, dict_t *xdata)
{
        if (name && (strlen (name) == 0) && xdata)
                return _gf_true;
        return _gf_false;
}

int32_t
posix_set_iatt_in_dict (dict_t *dict, struct iatt *in_stbuf)
{
        int ret             = -1;
        struct iatt *stbuf  = NULL;
        int32_t len         = sizeof(struct iatt);

        if (!dict || !in_stbuf)
                return ret;

        stbuf = GF_CALLOC (1, len, gf_common_mt_char);
        if (!stbuf)
                return ret;

        memcpy (stbuf, in_stbuf, len);

        ret = dict_set_iatt (dict, DHT_IATT_IN_XDATA_KEY, stbuf, false);
        if (ret)
                GF_FREE (stbuf);

        return ret;
}

mode_t
posix_override_umask (mode_t mode, mode_t mode_bit)
{
        gf_msg_debug ("posix", 0, "The value of mode is %u", mode);
        mode = mode >> 9; /* 3x3 (bits for each octal digit)*/
        mode = (mode << 9) | mode_bit;
        gf_msg_debug ("posix", 0, "The value of mode is %u", mode);
        return mode;
}

int
posix_check_internal_writes (xlator_t *this, fd_t *fd, int sysfd,
                             dict_t *xdata)
{
        int          ret = 0;
        size_t       xattrsize = 0;
        data_t      *val = NULL;

        if (!xdata)
                return 0;

        LOCK (&fd->inode->lock);
        {
                val = dict_get (xdata, GF_PROTECT_FROM_EXTERNAL_WRITES);
                if (val) {
                        ret = sys_fsetxattr (sysfd,
                                             GF_PROTECT_FROM_EXTERNAL_WRITES,
                                             val->data, val->len, 0);
                        if (ret == -1) {
                                gf_msg (this->name, GF_LOG_ERROR,
                                        P_MSG_XATTR_FAILED,
                                        errno, "setxattr failed key %s",
                                        GF_PROTECT_FROM_EXTERNAL_WRITES);
                        }

                        goto out;
                }

                if (dict_get (xdata, GF_AVOID_OVERWRITE)) {
                        xattrsize = sys_fgetxattr (sysfd,
                                                   GF_PROTECT_FROM_EXTERNAL_WRITES,
                                                   NULL, 0);
                        if ((xattrsize == -1) && ((errno == ENOATTR) ||
                            (errno == ENODATA))) {
                                ret = 0;
                        } else {
                                ret = -1;
                        }
                }
        }
out:
        UNLOCK (&fd->inode->lock);
        return ret;
}

gf_cs_obj_state
posix_cs_heal_state (xlator_t *this, const char *realpath, int *fd,
                     struct iatt *buf)
{
        gf_boolean_t remote = _gf_false;
        gf_boolean_t downloading = _gf_false;
        int          ret = 0;
        gf_cs_obj_state state = GF_CS_ERROR;
        size_t       xattrsize = 0;

        if (!buf) {
                ret = -1;
                goto out;
        }

        if (fd) {
                xattrsize = sys_fgetxattr (*fd, GF_CS_OBJECT_REMOTE, NULL, 0);
                if ((xattrsize == -1) && ((errno == ENOATTR)  ||
                    (errno == ENODATA))) {
                        remote = _gf_false;
                } else  if (xattrsize == -1) {
                        ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR, 0, errno, "fgetxattr"
                                " failed");
                        state = GF_CS_ERROR;
                        goto out;
                } else {
                        remote = _gf_true;
                }

                xattrsize = sys_fgetxattr (*fd, GF_CS_OBJECT_DOWNLOADING, NULL,
                                           0);
                if ((xattrsize == -1) && ((errno == ENOATTR)  ||
                    (errno == ENODATA))) {
                        downloading = _gf_false;
                } else if (xattrsize == -1) {
                        ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR, 0, errno, "fgetxattr"
                                " failed");
                        state = GF_CS_ERROR;
                        goto out;
                } else {
                        downloading = _gf_true;
                }
        } else {
                xattrsize = sys_lgetxattr (realpath, GF_CS_OBJECT_REMOTE, NULL,
                                           0);
                if ((xattrsize == -1) && ((errno == ENOATTR) ||
                    (errno == ENODATA))) {
                        remote = _gf_false;
                } else  if (xattrsize == -1) {
                        ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR, 0, errno, "getxattr"
                                " failed");
                        state = GF_CS_ERROR;
                        goto out;
                } else {
                        remote = _gf_true;
                }

                xattrsize = sys_lgetxattr (realpath, GF_CS_OBJECT_DOWNLOADING,
                                           NULL, 0);
                if ((xattrsize == -1) && ((errno == ENOATTR)  ||
                    (errno == ENODATA))) {
                        downloading = _gf_false;
                } else if (xattrsize == -1) {
                        ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR, 0, errno, "getxattr"
                                " failed");
                        state = GF_CS_ERROR;
                        goto out;
                } else {
                        downloading = _gf_true;
                }
        }

        if (remote && downloading) {
                if (fd) {
                        ret = sys_fremovexattr (*fd, GF_CS_OBJECT_DOWNLOADING);
                } else {
                        ret = sys_lremovexattr (realpath, GF_CS_OBJECT_DOWNLOADING);
                }

                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, errno,
                                "failed to remove xattr, repair failed");
                        state = GF_CS_ERROR;
                        goto out;
                }

                if (buf->ia_size) {
                        if (fd) {
                                ret = sys_ftruncate (*fd, 0);
                        } else {
                                ret = sys_truncate (realpath, 0);
                        }

                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0, errno,
                                        "truncate failed. File is in inconsistent"
                                        " state");
                                state = GF_CS_ERROR;
                                goto out;
                        }
                }

                state = GF_CS_REMOTE;
                goto out;

        } else if (remote) {
                if (buf->ia_size) {
                        if (fd) {
                                ret = sys_ftruncate (*fd, 0);
                        } else {
                                ret = sys_truncate (realpath, 0);
                        }
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0, errno,
                                        "truncate failed. File is in inconsistent"
                                        " state");
                                state = GF_CS_ERROR;
                                goto out;
                        }
                }

                state = GF_CS_REMOTE;
                goto out;
        } else if (downloading) {
                if (buf->ia_size) {
                        if (fd) {
                                ret = sys_fremovexattr (*fd, GF_CS_OBJECT_DOWNLOADING);
                        } else {
                                ret = sys_lremovexattr (realpath, GF_CS_OBJECT_DOWNLOADING);
                        }

                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0, errno,
                                        "failed to remove xattr, repair failed");
                                state = GF_CS_ERROR;
                                goto out;
                        }

                        state = GF_CS_LOCAL;
                        goto out;
                }
        }

        state = GF_CS_LOCAL;
out:
        gf_msg_debug (this->name, 0, "heal state returned %d", state);
        return state;
}

gf_cs_obj_state
posix_cs_check_status (xlator_t *this, const char *realpath, int *fd,
                       struct iatt *buf)
{
        gf_boolean_t remote = _gf_false;
        gf_boolean_t downloading = _gf_false;
        int          ret = 0;
        gf_cs_obj_state state = GF_CS_LOCAL;
        size_t       xattrsize = 0;
        int          op_errno = 0;

        if (fd) {
                xattrsize = sys_fgetxattr (*fd, GF_CS_OBJECT_REMOTE, NULL, 0);
                if ((xattrsize == -1) && ((errno == ENOATTR)  ||
                    (errno == ENODATA))) {
                        remote = _gf_false;
                } else  if (xattrsize == -1) {
                        ret = -1;
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, 0, 0, "getxattr "
                                "failed err %d", errno);
                        goto out;
                } else {
                        remote = _gf_true;
                }

                xattrsize = sys_fgetxattr (*fd, GF_CS_OBJECT_DOWNLOADING, NULL,
                                           0);
                if ((xattrsize == -1) && ((errno == ENOATTR)  ||
                    (errno == ENODATA))) {
                        downloading = _gf_false;
                } else if (xattrsize == -1) {
                        ret = -1;
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, 0, 0, "getxattr "
                                "failed err : %d", errno);

                        goto out;
                } else {
                        downloading = _gf_true;
                }

        }

        if (realpath) {
                xattrsize = sys_lgetxattr (realpath, GF_CS_OBJECT_REMOTE, NULL,
                                           0);
                if ((xattrsize == -1) && ((errno == ENOATTR)  ||
                    (errno == ENODATA))) {
                        remote = _gf_false;
                } else  if (xattrsize == -1) {
                        ret = -1;
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, 0, 0, "getxattr "
                                "failed err : %d", errno);
                        goto out;
                } else {
                        remote = _gf_true;
                }

                xattrsize = sys_lgetxattr (realpath, GF_CS_OBJECT_DOWNLOADING,
                                           NULL, 0);
                if ((xattrsize == -1) && ((errno == ENOATTR)  ||
                    (errno == ENODATA))) {
                        downloading = _gf_false;
                } else if (xattrsize == -1) {
                        ret = -1;
                        op_errno = errno;
                        gf_msg (this->name, GF_LOG_ERROR, 0, 0, "getxattr "
                                "failed err : %d", errno);
                        goto out;
                } else {
                        downloading = _gf_true;
                }
        }

out:
        if (ret) {
                gf_msg ("POSIX", GF_LOG_ERROR, 0, op_errno, "getxattr failed "
                        "with %d", op_errno);
                state = GF_CS_ERROR;
                return state;
        }

        if ((remote && downloading) || (remote && buf && buf->ia_size)) {
                state = GF_CS_REPAIR;
                gf_msg_debug (this->name, 0, "status is REPAIR");
                return state;
        }

        if (remote)
                state = GF_CS_REMOTE;
        else if (downloading)
                state = GF_CS_DOWNLOADING;
        else
                state = GF_CS_LOCAL;

        gf_msg_debug (this->name, 0, "state returned is %d", state);
        return state;

}

int
posix_cs_set_state (xlator_t *this, dict_t **rsp, gf_cs_obj_state state,
                    char const *path, int *fd)
{
        int     ret = 0;
        char    *value = NULL;
        size_t  xattrsize = 0;

        if (!(*rsp)) {
                *rsp = dict_new ();
                if (!(*rsp)) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, ENOMEM, "failed to"
                                " create dict");
                        ret = -1;
                        goto out;
                }
        }

        ret = dict_set_uint64 (*rsp, GF_CS_OBJECT_STATUS, state);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, ENOMEM, "failed to set "
                        "dict");
                ret = -1;
                goto out;
        }

        if (fd) {
                xattrsize = sys_fgetxattr (*fd, GF_CS_OBJECT_REMOTE, NULL, 0);
                if (xattrsize != -1) {
                        value = GF_CALLOC (1, xattrsize + 1, gf_posix_mt_char);
                        if (!value) {
                                gf_msg (this->name, GF_LOG_ERROR, 0, 0,
                                        "no memory for value");
                                ret = -1;
                                goto out;
                        }
                        /* TODO: Add check for ENODATA */
                        xattrsize = sys_fgetxattr (*fd, GF_CS_OBJECT_REMOTE,
                                                   value, xattrsize + 1);
                        if (xattrsize == -1) {
                                gf_msg (this->name, GF_LOG_ERROR, 0, errno,
                                        " getxattr failed for key %s",
                                        GF_CS_OBJECT_REMOTE);
                                goto out;
                        } else {
                                value[xattrsize] = '\0';
                        }
                } else {
                        gf_msg (this->name, GF_LOG_ERROR, 0, errno,
                                " getxattr failed for key %s",
                                GF_CS_OBJECT_REMOTE);
                        goto out;
                }
        } else {
                xattrsize = sys_lgetxattr (path, GF_CS_OBJECT_REMOTE, NULL, 0);
                if (xattrsize != -1) {
                        value = GF_CALLOC (1, xattrsize + 1, gf_posix_mt_char);
                        if (!value) {
                                ret = -1;
                                goto out;
                        }

                        xattrsize = sys_lgetxattr (path, GF_CS_OBJECT_REMOTE,
                                                   value, xattrsize + 1);
                        if (xattrsize == -1) {
                                gf_msg (this->name, GF_LOG_ERROR, 0, errno,
                                        " getxattr failed for key %s",
                                        GF_CS_OBJECT_REMOTE);
                                goto out;
                        } else {
                                value[xattrsize] = '\0';
                        }
                } else {
                        gf_msg (this->name, GF_LOG_ERROR, 0, errno,
                                " getxattr failed for key %s",
                                GF_CS_OBJECT_REMOTE);
                        goto out;
                }
        }

        if (ret == 0) {
                ret = dict_set_str (*rsp, GF_CS_OBJECT_REMOTE, value);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, 0, "failed to set"
                                "value");
                }
        }

out:
        return ret;
}


/* This function checks the status of the file and updates the xattr response.
 * Also it repairs the state of the file which could have been resulted from a
 * crash or transient failures.
 */
int
posix_cs_maintenance (xlator_t *this, fd_t *fd, loc_t *loc, int *pfd,
                   struct iatt *buf, const char *realpath, dict_t *xattr_req,
                   dict_t **xattr_rsp, gf_boolean_t ignore_failure)
{
        gf_cs_obj_state    state = GF_CS_ERROR;
        int                ret   = 0;

        if (!(dict_get (xattr_req, GF_CS_OBJECT_STATUS) ||
              dict_get (xattr_req, GF_CS_OBJECT_REPAIR)))
                return 0;


        if (fd) {
                LOCK (&fd->inode->lock);
                if (dict_get (xattr_req, GF_CS_OBJECT_STATUS)) {
                        state = posix_cs_check_status (this, NULL, pfd, buf);
                        gf_msg_debug (this->name, 0, "state : %d", state);
                        ret = posix_cs_set_state (this, xattr_rsp,
                                                  state, NULL, pfd);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0, 0,
                                        "posix_cs_set_state failed");
                        }

                        if (ignore_failure) {
                                ret = 0;
                                goto unlock;
                        } else {
                                if (state != GF_CS_LOCAL || ret != 0) {
                                        ret = -1;
                                        goto unlock;
                                }
                        }
                }

                if (dict_get (xattr_req, GF_CS_OBJECT_REPAIR)) {
                        state = posix_cs_check_status (this, NULL, pfd,
                                                       buf);
                        gf_msg_debug (this->name, 0, "state : %d", state);

                        if (state == GF_CS_REPAIR) {
                                state = posix_cs_heal_state (this, NULL,
                                                             pfd, buf);

                                if (state == GF_CS_ERROR) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0, 0,
                                                "repair check failed");
                                }
                        }

                        ret = posix_cs_set_state (this, xattr_rsp,
                                                  state, NULL, pfd);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0, 0,
                                        "posix_cs_set_state failed");
                                if (ignore_failure)
                                        ret = 0;
                                else
                                        ret = -1;
                                goto unlock;
                        }
                }
        } else {
                if (!loc->inode) {
                        ret = 0;
                        goto unlock;
                }

                LOCK (&loc->inode->lock);
                if (dict_get (xattr_req, GF_CS_OBJECT_STATUS)) {
                        state = posix_cs_check_status (this, realpath, NULL,
                                                       buf);
                        gf_msg_debug (this->name, 0, "state : %d", state);
                        ret = posix_cs_set_state (this, xattr_rsp, state,
                                                  realpath, NULL);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0, 0,
                                        "posix_cs_set_state failed");
                        }

                        if (ignore_failure) {
                                ret = 0;
                                goto unlock;
                        } else {
                                if (state != GF_CS_LOCAL || ret != 0) {
                                        ret = -1;
                                        goto unlock;
                                }
                        }
                }

                if (dict_get (xattr_req, GF_CS_OBJECT_REPAIR)) {
                        state = posix_cs_check_status (this, realpath, NULL,
                                                       buf);
                        gf_msg_debug (this->name, 0, "state : %d", state);

                        if (state == GF_CS_REPAIR) {
                                state = posix_cs_heal_state (this, realpath,
                                                             NULL, buf);

                                if (state == GF_CS_ERROR) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0, 0,
                                                "repair check failed");
                                }
                        }

                        ret = posix_cs_set_state (this, xattr_rsp,
                                                  state, realpath, NULL);
                        if (ret) {
                                 gf_msg (this->name, GF_LOG_ERROR, 0, 0,
                                                "posix_cs_set_state failed");
                                if (ignore_failure)
                                        ret = 0;
                                else
                                        ret = -1;
                                goto unlock;
                        }
                }
        }

unlock:
        if (fd)
                UNLOCK (&fd->inode->lock);
        else
                UNLOCK (&loc->inode->lock);

        return ret;
}
