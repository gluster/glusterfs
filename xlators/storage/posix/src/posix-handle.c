/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#ifdef GF_LINUX_HOST_OS
#include <alloca.h>
#endif

#include "posix-handle.h"
#include "posix.h"
#include <glusterfs/syscall.h>
#include "posix-messages.h"
#include "posix-metadata.h"

#include <glusterfs/compat-errno.h>

int
posix_handle_mkdir_hashes(xlator_t *this, int dfd, uuid_t gfid);

inode_t *
posix_resolve(xlator_t *this, inode_table_t *itable, inode_t *parent,
              char *bname, struct iatt *iabuf)
{
    inode_t *inode = NULL;
    int ret = -1;

    ret = posix_istat(this, NULL, parent->gfid, bname, iabuf);
    if (ret < 0) {
        gf_log(this->name, GF_LOG_WARNING,
               "gfid: %s, bname: %s "
               "failed",
               uuid_utoa(parent->gfid), bname);
        goto out;
    }

    if (__is_root_gfid(iabuf->ia_gfid) && !strcmp(bname, "/")) {
        inode = itable->root;
    } else {
        inode = inode_find(itable, iabuf->ia_gfid);
        if (inode == NULL) {
            inode = inode_new(itable);
            gf_uuid_copy(inode->gfid, iabuf->ia_gfid);
        }
    }

    /* posix_istat wouldn't have fetched posix_mdata_t i.e.,
     * time attributes as inode is passed as NULL, hence get
     * here once you got the inode
     */
    ret = posix_get_mdata_xattr(this, NULL, -1, inode, iabuf);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_GETMDATA_FAILED,
               "posix get mdata failed on gfid:%s", uuid_utoa(inode->gfid));
        goto out;
    }

    /* Linking an inode here, can cause a race in posix_acl.
       Parent inode gets linked here, but before
       it reaches posix_acl_readdirp_cbk, create/lookup can
       come on a leaf-inode, as parent-inode-ctx not yet updated
       in posix_acl_readdirp_cbk, create and lookup can fail
       with EACCESS. So do the inode linking in the quota xlator

    if (__is_root_gfid (iabuf->ia_gfid) && !strcmp (bname, "/"))
            linked_inode = itable->root;
    else
            linked_inode = inode_link (inode, parent, bname, iabuf);

    inode_unref (inode);*/

out:
    return inode;
}

int
posix_make_ancestral_node(const char *priv_base_path, char *path, int pathsize,
                          gf_dirent_t *head, char *dir_name, struct iatt *iabuf,
                          inode_t *inode, int type, dict_t *xdata)
{
    gf_dirent_t *entry = NULL;
    char real_path[PATH_MAX + 1] =
        {
            0,
        },
                              len = 0;
    loc_t loc = {
        0,
    };
    int ret = -1;

    len = strlen(path) + strlen(dir_name) + 1;
    if (len > pathsize) {
        goto out;
    }

    strcat(path, dir_name);
    if (*dir_name != '/')
        strcat(path, "/");

    if (type & POSIX_ANCESTRY_DENTRY) {
        entry = gf_dirent_for_name(dir_name);
        if (!entry)
            goto out;

        entry->d_stat = *iabuf;
        entry->inode = inode_ref(inode);

        list_add_tail(&entry->list, &head->list);
        snprintf(real_path, sizeof(real_path), "%s/%s", priv_base_path, path);
        loc.inode = inode_ref(inode);
        gf_uuid_copy(loc.gfid, inode->gfid);

        entry->dict = posix_xattr_fill(THIS, real_path, &loc, NULL, -1, xdata,
                                       iabuf);
        loc_wipe(&loc);
    }

    ret = 0;

out:
    return ret;
}

int
posix_make_ancestryfromgfid(xlator_t *this, char *path, int pathsize,
                            gf_dirent_t *head, int type, uuid_t gfid,
                            const size_t handle_size,
                            const char *priv_base_path, inode_table_t *itable,
                            inode_t **parent, dict_t *xdata, int32_t *op_errno)
{
    char *linkname = NULL; /* "../../<gfid[0]>/<gfid[1]/"
                            "<gfidstr>/<NAME_MAX>" */
    char *dir_handle = NULL;
    char *pgfidstr = NULL;
    char *saveptr = NULL;
    ssize_t len = 0;
    inode_t *inode = NULL;
    struct iatt iabuf = {
        0,
    };
    int ret = -1;
    uuid_t tmp_gfid = {
        0,
    };
    char *dir_stack[PATH_MAX / 2 + 1]; /* Since PATH_MAX/2 also gives
                                        an upper bound on depth of
                                        directories tree */
    uuid_t gfid_stack[PATH_MAX / 2 + 1];

    char *dir_name = NULL;
    char *saved_dir = NULL;
    int top = -1;

    if (!path || !parent || !priv_base_path || gf_uuid_is_null(gfid)) {
        *op_errno = EINVAL;
        goto out;
    }

    dir_handle = alloca(handle_size);
    linkname = alloca(PATH_MAX);
    gf_uuid_copy(tmp_gfid, gfid);

    while (top < PATH_MAX / 2) {
        gf_uuid_copy(gfid_stack[++top], tmp_gfid);
        if (__is_root_gfid(tmp_gfid)) {
            *parent = inode_ref(itable->root);

            saved_dir = alloca(sizeof("/"));
            strcpy(saved_dir, "/");
            dir_stack[top] = saved_dir;
            break;
        } else {
            snprintf(dir_handle, handle_size, "%s/%s/%02x/%02x/%s",
                     priv_base_path, GF_HIDDEN_PATH, tmp_gfid[0], tmp_gfid[1],
                     uuid_utoa(tmp_gfid));

            len = sys_readlink(dir_handle, linkname, PATH_MAX);
            if (len < 0) {
                *op_errno = errno;
                gf_msg(this->name,
                       (errno == ENOENT || errno == ESTALE) ? GF_LOG_DEBUG
                                                            : GF_LOG_ERROR,
                       errno, P_MSG_READLINK_FAILED,
                       "could not read"
                       " the link from the gfid handle %s ",
                       dir_handle);
                ret = -1;
                goto out;
            }

            linkname[len] = '\0';

            pgfidstr = strtok_r(linkname + SLEN("../../00/00/"), "/", &saveptr);
            dir_name = strtok_r(NULL, "/", &saveptr);
            saved_dir = alloca(strlen(dir_name) + 1);
            gf_uuid_parse(pgfidstr, tmp_gfid);
            strcpy(saved_dir, dir_name);
            dir_stack[top] = saved_dir;
        }
    }
    if (top == PATH_MAX / 2) {
        gf_msg(this->name, GF_LOG_ERROR, P_MSG_ANCESTORY_FAILED, 0,
               "build ancestry failed due to "
               "deep directory hierarchy, depth: %d.",
               top);
        *op_errno = EINVAL;
        ret = -1;
        goto out;
    }

    while (top >= 0) {
        if (!*parent) {
            /* There's no real "root" cause for how we end up here,
             * so for now let's log this and bail out to prevent
             * crashes.
             */
            gf_msg(this->name, GF_LOG_WARNING, P_MSG_INODE_RESOLVE_FAILED, 0,
                   "OOPS: *parent is null (path: %s), bailing!", path);
            goto out;
        }

        memset(&iabuf, 0, sizeof(iabuf));
        inode = posix_resolve(this, itable, *parent, dir_stack[top], &iabuf);
        if (inode == NULL) {
            gf_msg(this->name, GF_LOG_ERROR, P_MSG_INODE_RESOLVE_FAILED, 0,
                   "posix resolve on the inode %s failed",
                   uuid_utoa(gfid_stack[top]));
            *op_errno = ESTALE;
            ret = -1;
            goto out;
        }

        ret = posix_make_ancestral_node(priv_base_path, path, pathsize, head,
                                        dir_stack[top], &iabuf, inode, type,
                                        xdata);
        if (ret < 0) {
            *op_errno = ENOMEM;
            goto out;
        }

        inode_unref(*parent);
        *parent = inode;
        top--;
    }
out:
    return ret;
}

int
posix_handle_relpath(xlator_t *this, uuid_t gfid, const char *basename,
                     char *buf, size_t buflen)
{
    char *uuid_str = NULL;
    int len = 0;

    len = POSIX_GFID_HANDLE_RELSIZE;

    if (basename) {
        len += (strlen(basename) + 1);
    }

    if (buflen < len || !buf)
        return len;

    uuid_str = uuid_utoa(gfid);

    if (basename) {
        len = snprintf(buf, buflen, "../../%02x/%02x/%s/%s", gfid[0], gfid[1],
                       uuid_str, basename);
    } else {
        len = snprintf(buf, buflen, "../../%02x/%02x/%s", gfid[0], gfid[1],
                       uuid_str);
    }

    return len;
}

/*
  TODO: explain how this pump fixes ELOOP
*/
gf_boolean_t
posix_is_malformed_link(xlator_t *this, char *base_str, char *linkname,
                        size_t len)
{
    if ((len == 8) && strcmp(linkname, "../../..")) /*for root*/
        goto err;

    if (len < 50 || len >= 512)
        goto err;

    if (memcmp(linkname, "../../", 6) != 0)
        goto err;

    if ((linkname[2] != '/') || (linkname[5] != '/') || (linkname[8] != '/') ||
        (linkname[11] != '/') || (linkname[48] != '/')) {
        goto err;
    }

    if ((linkname[20] != '-') || (linkname[25] != '-') ||
        (linkname[30] != '-') || (linkname[35] != '-')) {
        goto err;
    }

    return _gf_false;

err:
    gf_log_callingfn(this->name, GF_LOG_ERROR,
                     "malformed internal link "
                     "%s for %s",
                     linkname, base_str);
    return _gf_true;
}

int
posix_handle_pump(xlator_t *this, char *buf, int len, int maxlen,
                  char *base_str, int base_len, int pfx_len)
{
    char linkname[512] = {
        0,
    }; /* "../../<gfid>/<NAME_MAX>" */
    int ret = 0;
    int blen = 0;
    int link_len = 0;
    char tmpstr[POSIX_GFID_HASH2_LEN] = {
        0,
    };
    char d2[3] = {
        0,
    };
    int index = 0;
    int dirfd = 0;
    struct posix_private *priv = this->private;

    strncpy(tmpstr, (base_str + pfx_len + 3), 40);
    strncpy(d2, (base_str + pfx_len), 2);
    index = strtoul(d2, NULL, 16);
    dirfd = priv->arrdfd[index];

    /* is a directory's symlink-handle */
    ret = readlinkat(dirfd, tmpstr, linkname, 512);
    if (ret == -1) {
        gf_msg(this->name, GF_LOG_ERROR, errno, P_MSG_READLINK_FAILED,
               "internal readlink failed on %s ", base_str);
        goto err;
    }

    if (ret < 512)
        linkname[ret] = 0;

    link_len = ret;

    if ((ret == 8) && memcmp(linkname, "../../..", 8) == 0) {
        if (strcmp(base_str, buf) == 0) {
            strcpy(buf + pfx_len, "..");
        }
        goto out;
    }

    if (posix_is_malformed_link(this, base_str, linkname, ret))
        goto err;

    blen = link_len - 48;

    if (len + blen >= maxlen) {
        gf_msg(this->name, GF_LOG_ERROR, 0, P_MSG_HANDLEPATH_FAILED,
               "Unable to form handle path for %s (maxlen = %d)", buf, maxlen);
        goto err;
    }

    memmove(buf + base_len + blen, buf + base_len,
            (strlen(buf) - base_len) + 1);

    strncpy(base_str + pfx_len, linkname + 6, base_len - pfx_len);

    strncpy(buf + pfx_len, linkname + 6, link_len - 6);
out:
    return len + blen;
err:
    return -1;
}

/*
  posix_handle_path differs from posix_handle_gfid_path in the way that the
  path filled in @buf by posix_handle_path will return type IA_IFDIR when
  an lstat() is performed on it, whereas posix_handle_gfid_path returns path
  to the handle symlink (typically used for the purpose of unlinking it).

  posix_handle_path also guarantees immunity to ELOOP on the path returned by it
*/

int
posix_handle_path(xlator_t *this, uuid_t gfid, const char *basename, char *ubuf,
                  size_t size)
{
    struct posix_private *priv = NULL;
    char *uuid_str = NULL;
    int len = 0;
    int ret = -1;
    struct stat stat;
    char *base_str = NULL;
    int base_len = 0;
    int pfx_len;
    int maxlen;
    char *buf;
    int index = 0;
    int dfd = 0;
    char newstr[POSIX_GFID_HASH2_LEN] = {
        0,
    };

    priv = this->private;

    uuid_str = uuid_utoa(gfid);

    if (ubuf) {
        buf = ubuf;
        maxlen = size;
    } else {
        maxlen = PATH_MAX;
        buf = alloca(maxlen);
    }

    index = gfid[0];
    dfd = priv->arrdfd[index];

    base_len = (priv->base_path_length + SLEN(GF_HIDDEN_PATH) + 45);
    base_str = alloca(base_len + 1);
    base_len = snprintf(base_str, base_len + 1, "%s/%s/%02x/%02x/%s",
                        priv->base_path, GF_HIDDEN_PATH, gfid[0], gfid[1],
                        uuid_str);
    pfx_len = priv->base_path_length + 1 + SLEN(GF_HIDDEN_PATH) + 1;

    if (basename) {
        len = snprintf(buf, maxlen, "%s/%s", base_str, basename);
    } else {
        len = snprintf(buf, maxlen, "%s", base_str);
    }

    snprintf(newstr, sizeof(newstr), "%02x/%s", gfid[1], uuid_str);
    ret = sys_fstatat(dfd, newstr, &stat, AT_SYMLINK_NOFOLLOW);

    if (!(ret == 0 && S_ISLNK(stat.st_mode) && stat.st_nlink == 1))
        goto out;

    do {
        errno = 0;
        ret = posix_handle_pump(this, buf, len, maxlen, base_str, base_len,
                                pfx_len);
        len = ret;

        if (ret == -1)
            break;
        ret = sys_lstat(buf, &stat);
    } while ((ret == -1) && errno == ELOOP);

out:
    return len + 1;
}

int
posix_handle_gfid_path(xlator_t *this, uuid_t gfid, char *buf, size_t buflen)
{
    struct posix_private *priv = NULL;
    char *uuid_str = NULL;
    int len = 0;

    priv = this->private;

    len = POSIX_GFID_HANDLE_SIZE(priv->base_path_length);

    len += 256; /* worst-case for directory's symlink-handle expansion */

    if ((buflen < len) || !buf)
        return len;

    uuid_str = uuid_utoa(gfid);

    if (__is_root_gfid(gfid)) {
        len = snprintf(buf, buflen, "%s", priv->base_path);
    } else {
        len = snprintf(buf, buflen, "%s/%s/%02x/%02x/%s", priv->base_path,
                       GF_HIDDEN_PATH, gfid[0], gfid[1], uuid_str);
    }

    return len;
}

int
posix_handle_init(xlator_t *this)
{
    struct posix_private *priv = NULL;
    char *handle_pfx = NULL;
    int ret = 0;
    struct stat stbuf;
    struct stat rootbuf;
    struct stat exportbuf;
    char *rootstr = NULL;
    static uuid_t gfid = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    int dfd = 0;

    priv = this->private;

    ret = sys_stat(priv->base_path, &exportbuf);
    if (ret || !S_ISDIR(exportbuf.st_mode)) {
        gf_msg(this->name, GF_LOG_ERROR, 0, P_MSG_HANDLE_CREATE,
               "Not a directory: %s", priv->base_path);
        return -1;
    }

    handle_pfx = alloca(priv->base_path_length + 1 + SLEN(GF_HIDDEN_PATH) + 1);

    sprintf(handle_pfx, "%s/%s", priv->base_path, GF_HIDDEN_PATH);

    ret = sys_stat(handle_pfx, &stbuf);
    switch (ret) {
        case -1:
            if (errno == ENOENT) {
                ret = sys_mkdir(handle_pfx, 0600);
                if (ret != 0) {
                    gf_msg(this->name, GF_LOG_ERROR, errno, P_MSG_HANDLE_CREATE,
                           "Creating directory %s failed", handle_pfx);
                    return -1;
                }
            } else {
                gf_msg(this->name, GF_LOG_ERROR, errno, P_MSG_HANDLE_CREATE,
                       "Checking for %s failed", handle_pfx);
                return -1;
            }
            break;
        case 0:
            if (!S_ISDIR(stbuf.st_mode)) {
                gf_msg(this->name, GF_LOG_ERROR, 0, P_MSG_HANDLE_CREATE,
                       "Not a directory: %s", handle_pfx);
                return -1;
            }
            break;
        default:
            break;
    }

    ret = sys_stat(handle_pfx, &priv->handledir);

    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, errno, P_MSG_HANDLE_CREATE,
               "stat for %s failed", handle_pfx);
        return -1;
    }

    MAKE_HANDLE_ABSPATH_FD(rootstr, this, gfid, dfd);
    ret = sys_fstatat(dfd, rootstr, &rootbuf, 0);
    switch (ret) {
        case -1:
            if (errno != ENOENT) {
                gf_msg(this->name, GF_LOG_ERROR, errno, P_MSG_HANDLE_CREATE,
                       "%s", priv->base_path);
                return -1;
            }
            ret = posix_handle_mkdir_hashes(this, dfd, gfid);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_HANDLE_CREATE,
                       "mkdir %s failed", rootstr);
                return -1;
            }

            ret = sys_symlinkat("../../..", dfd, rootstr);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, errno, P_MSG_HANDLE_CREATE,
                       "symlink %s creation failed", rootstr);
                return -1;
            }
            break;
        case 0:
            if ((exportbuf.st_ino == rootbuf.st_ino) &&
                (exportbuf.st_dev == rootbuf.st_dev))
                return 0;

            gf_msg(this->name, GF_LOG_ERROR, 0, P_MSG_HANDLE_CREATE,
                   "Different dirs %s (%lld/%lld) != %s (%lld/%lld)",
                   priv->base_path, (long long)exportbuf.st_ino,
                   (long long)exportbuf.st_dev, rootstr,
                   (long long)rootbuf.st_ino, (long long)rootbuf.st_dev);
            return -1;

            break;
    }

    return 0;
}

gf_boolean_t
posix_does_old_trash_exists(char *old_trash)
{
    uuid_t gfid = {0};
    gf_boolean_t exists = _gf_false;
    struct stat stbuf = {0};
    int ret = 0;

    ret = sys_lstat(old_trash, &stbuf);
    if ((ret == 0) && S_ISDIR(stbuf.st_mode)) {
        ret = sys_lgetxattr(old_trash, "trusted.gfid", gfid, 16);
        if ((ret < 0) && (errno == ENODATA || errno == ENOATTR))
            exists = _gf_true;
    }
    return exists;
}

int
posix_handle_new_trash_init(xlator_t *this, char *trash)
{
    int ret = 0;
    struct stat stbuf = {0};

    ret = sys_lstat(trash, &stbuf);
    switch (ret) {
        case -1:
            if (errno == ENOENT) {
                ret = sys_mkdir(trash, 0755);
                if (ret != 0) {
                    gf_msg(this->name, GF_LOG_ERROR, errno,
                           P_MSG_HANDLE_TRASH_CREATE,
                           "Creating directory %s failed", trash);
                }
            } else {
                gf_msg(this->name, GF_LOG_ERROR, errno,
                       P_MSG_HANDLE_TRASH_CREATE, "Checking for %s failed",
                       trash);
            }
            break;
        case 0:
            if (!S_ISDIR(stbuf.st_mode)) {
                gf_msg(this->name, GF_LOG_ERROR, errno,
                       P_MSG_HANDLE_TRASH_CREATE, "Not a directory: %s", trash);
                ret = -1;
            }
            break;
        default:
            break;
    }
    return ret;
}

int
posix_mv_old_trash_into_new_trash(xlator_t *this, char *old, char *new)
{
    char dest_old[PATH_MAX] = {0};
    int ret = 0;
    uuid_t dest_name = {0};

    if (!posix_does_old_trash_exists(old))
        goto out;
    gf_uuid_generate(dest_name);
    snprintf(dest_old, sizeof(dest_old), "%s/%s", new, uuid_utoa(dest_name));
    ret = sys_rename(old, dest_old);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, errno, P_MSG_HANDLE_TRASH_CREATE,
               "Not able to move %s -> %s ", old, dest_old);
    }
out:
    return ret;
}

int
posix_handle_trash_init(xlator_t *this)
{
    int ret = -1;
    struct posix_private *priv = NULL;
    char old_trash[PATH_MAX] = {0};

    priv = this->private;

    priv->trash_path = GF_MALLOC(priv->base_path_length + SLEN("/") +
                                     SLEN(GF_HIDDEN_PATH) + SLEN("/") +
                                     SLEN(TRASH_DIR) + 1,
                                 gf_posix_mt_trash_path);

    if (!priv->trash_path)
        goto out;

    snprintf(
        priv->trash_path,
        priv->base_path_length + SLEN(GF_HIDDEN_PATH) + SLEN(TRASH_DIR) + 3,
        "%s/%s/%s", priv->base_path, GF_HIDDEN_PATH, TRASH_DIR);

    ret = posix_handle_new_trash_init(this, priv->trash_path);
    if (ret)
        goto out;
    snprintf(old_trash, sizeof(old_trash), "%s/.landfill", priv->base_path);
    ret = posix_mv_old_trash_into_new_trash(this, old_trash, priv->trash_path);
out:
    return ret;
}

int
posix_handle_mkdir_hashes(xlator_t *this, int dirfd, uuid_t gfid)
{
    int ret = -1;
    char d2[3] = {
        0,
    };

    snprintf(d2, sizeof(d2), "%02x", gfid[1]);
    ret = sys_mkdirat(dirfd, d2, 0700);
    if (ret == -1 && errno != EEXIST) {
        gf_msg(this->name, GF_LOG_ERROR, errno, P_MSG_HANDLE_CREATE,
               "error mkdir hash-2 %s ", uuid_utoa(gfid));
        return -1;
    }

    return 0;
}

int
posix_handle_hard(xlator_t *this, const char *oldpath, uuid_t gfid,
                  struct stat *oldbuf)
{
    struct stat newbuf;
    struct stat hashbuf;
    int ret = -1;
    gf_boolean_t link_exists = _gf_false;
    char d2[3] = {
        0,
    };
    int dfd = -1;
    char *newstr = NULL;

    MAKE_HANDLE_ABSPATH_FD(newstr, this, gfid, dfd);
    ret = sys_fstatat(dfd, newstr, &newbuf, AT_SYMLINK_NOFOLLOW);

    if (ret == -1 && errno != ENOENT) {
        gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_HANDLE_CREATE, "%s",
               uuid_utoa(gfid));
        return -1;
    }

    if (ret == -1 && errno == ENOENT) {
        snprintf(d2, sizeof(d2), "%02x", gfid[1]);
        ret = sys_fstatat(dfd, d2, &hashbuf, 0);
        if (ret) {
            ret = posix_handle_mkdir_hashes(this, dfd, gfid);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_HANDLE_CREATE,
                       "mkdir %s failed ", uuid_utoa(gfid));
                return -1;
            }
        }
        ret = sys_linkat(AT_FDCWD, oldpath, dfd, newstr);

        if (ret) {
            if (errno != EEXIST) {
                gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_HANDLE_CREATE,
                       "link %s -> %s"
                       "failed ",
                       oldpath, newstr);
                return -1;
            } else {
                link_exists = _gf_true;
            }
        }
        ret = sys_fstatat(dfd, newstr, &newbuf, AT_SYMLINK_NOFOLLOW);

        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_HANDLE_CREATE,
                   "lstat on %s failed", uuid_utoa(gfid));
            return -1;
        }
        if ((link_exists) && (!S_ISREG(newbuf.st_mode))) {
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, P_MSG_HANDLE_CREATE,
                   "%s - Expected regular file", uuid_utoa(gfid));
            return -1;
        }
    }

    if (newbuf.st_ino != oldbuf->st_ino || newbuf.st_dev != oldbuf->st_dev) {
        gf_msg(this->name, GF_LOG_WARNING, 0, P_MSG_HANDLE_CREATE,
               "mismatching ino/dev between file %s (%lld/%lld) "
               "and handle %s (%lld/%lld)",
               oldpath, (long long)oldbuf->st_ino, (long long)oldbuf->st_dev,
               uuid_utoa(gfid), (long long)newbuf.st_ino,
               (long long)newbuf.st_dev);
        ret = -1;
    }

    return ret;
}

int
posix_handle_soft(xlator_t *this, const char *real_path, loc_t *loc,
                  uuid_t gfid, struct stat *oldbuf)
{
    char *oldpath = NULL;
    char *newpath = NULL;
    struct stat newbuf;
    struct stat hashbuf;
    int ret = -1;
    char d2[3] = {
        0,
    };
    int dfd = -1;
    char *newstr = NULL;

    MAKE_HANDLE_ABSPATH(newpath, this, gfid);
    MAKE_HANDLE_ABSPATH_FD(newstr, this, gfid, dfd);
    MAKE_HANDLE_RELPATH(oldpath, this, loc->pargfid, loc->name);

    ret = sys_fstatat(dfd, newstr, &newbuf, AT_SYMLINK_NOFOLLOW);

    if (ret == -1 && errno != ENOENT) {
        gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_HANDLE_CREATE, "%s",
               newstr);
        return -1;
    }

    if (ret == -1 && errno == ENOENT) {
        if (posix_is_malformed_link(this, newpath, oldpath, strlen(oldpath))) {
            GF_ASSERT(!"Malformed link");
            errno = EINVAL;
            return -1;
        }

        snprintf(d2, sizeof(d2), "%02x", gfid[1]);
        ret = sys_fstatat(dfd, d2, &hashbuf, 0);

        if (ret) {
            ret = posix_handle_mkdir_hashes(this, dfd, gfid);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_HANDLE_CREATE,
                       "mkdir %s failed ", newstr);
                return -1;
            }
        }
        ret = sys_symlinkat(oldpath, dfd, newstr);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_HANDLE_CREATE,
                   "symlink %s -> %s failed", oldpath, newstr);
            return -1;
        }

        ret = sys_fstatat(dfd, newstr, &newbuf, AT_SYMLINK_NOFOLLOW);

        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_HANDLE_CREATE,
                   "stat on %s failed ", newstr);
            return -1;
        }
    }

    ret = sys_stat(real_path, &newbuf);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_HANDLE_CREATE,
               "stat on %s failed ", real_path);
        return -1;
    }

    if (!oldbuf)
        return ret;

    if (newbuf.st_ino != oldbuf->st_ino || newbuf.st_dev != oldbuf->st_dev) {
        gf_msg(this->name, GF_LOG_WARNING, 0, P_MSG_HANDLE_CREATE,
               "mismatching ino/dev between file %s (%lld/%lld) "
               "and handle %s (%lld/%lld)",
               oldpath, (long long)oldbuf->st_ino, (long long)oldbuf->st_dev,
               newpath, (long long)newbuf.st_ino, (long long)newbuf.st_dev);
        ret = -1;
    }

    return ret;
}

int
posix_handle_unset_gfid(xlator_t *this, uuid_t gfid)
{
    int ret = 0;
    struct stat stat;
    int index = 0;
    int dfd = 0;
    char newstr[POSIX_GFID_HASH2_LEN] = {
        0,
    };
    struct posix_private *priv = this->private;

    index = gfid[0];
    dfd = priv->arrdfd[index];

    snprintf(newstr, sizeof(newstr), "%02x/%s", gfid[1], uuid_utoa(gfid));
    ret = sys_fstatat(dfd, newstr, &stat, AT_SYMLINK_NOFOLLOW);

    if (ret == -1) {
        if (errno != ENOENT) {
            gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_HANDLE_DELETE, "%s",
                   newstr);
        }
        goto out;
    }

    ret = sys_unlinkat(dfd, newstr);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_HANDLE_DELETE,
               "unlink %s is failed", newstr);
    }

out:
    return ret;
}

int
posix_handle_unset(xlator_t *this, uuid_t gfid, const char *basename)
{
    int ret;
    struct iatt stat;
    char *path = NULL;

    if (!basename) {
        ret = posix_handle_unset_gfid(this, gfid);
        return ret;
    }

    MAKE_HANDLE_PATH(path, this, gfid, basename);
    if (!path) {
        gf_msg(this->name, GF_LOG_WARNING, 0, P_MSG_HANDLE_DELETE,
               "Failed to create handle path for %s (%s)", basename,
               uuid_utoa(gfid));
        return -1;
    }

    /* stat is being used only for gfid, so passing a NULL inode
     * doesn't fetch time attributes which is fine
     */
    ret = posix_istat(this, NULL, gfid, basename, &stat);
    if (ret == -1) {
        gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_HANDLE_DELETE, "%s",
               path);
        return -1;
    }

    ret = posix_handle_unset_gfid(this, stat.ia_gfid);

    return ret;
}

int
posix_create_link_if_gfid_exists(xlator_t *this, uuid_t gfid, char *real_path,
                                 inode_table_t *itable)
{
    int ret = -1;
    char *newpath = NULL;
    char *unlink_path = NULL;
    uint64_t ctx_int = 0;
    inode_t *inode = NULL;
    struct stat stbuf = {
        0,
    };
    struct posix_private *priv = NULL;
    posix_inode_ctx_t *ctx = NULL;

    priv = this->private;

    MAKE_HANDLE_PATH(newpath, this, gfid, NULL);
    if (!newpath) {
        gf_msg(this->name, GF_LOG_WARNING, 0, P_MSG_HANDLE_CREATE,
               "Failed to create handle path (%s)", uuid_utoa(gfid));
        return ret;
    }

    ret = sys_lstat(newpath, &stbuf);
    if (!ret) {
        ret = sys_link(newpath, real_path);
    } else {
        inode = inode_find(itable, gfid);
        if (!inode)
            return -1;

        LOCK(&inode->lock);
        {
            ret = __posix_inode_ctx_get_all(inode, this, &ctx);
            if (ret)
                goto unlock;

            if (ctx->unlink_flag != GF_UNLINK_TRUE) {
                ret = -1;
                goto unlock;
            }

            POSIX_GET_FILE_UNLINK_PATH(priv->base_path, gfid, unlink_path);
            ret = sys_link(unlink_path, real_path);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_HANDLE_CREATE,
                       "Failed to link "
                       "%s with %s",
                       real_path, unlink_path);
                goto unlock;
            }
            ret = sys_rename(unlink_path, newpath);
            if (ret) {
                gf_msg(this->name, GF_LOG_WARNING, errno, P_MSG_HANDLE_CREATE,
                       "Failed to link "
                       "%s with %s",
                       real_path, unlink_path);
                goto unlock;
            }
            ctx_int = GF_UNLINK_FALSE;
            ret = __posix_inode_ctx_set_unlink_flag(inode, this, ctx_int);
        }
    unlock:
        UNLOCK(&inode->lock);

        inode_unref(inode);
    }

    return ret;
}
