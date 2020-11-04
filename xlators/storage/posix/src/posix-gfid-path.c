/*
   Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <stdint.h>

#include <glusterfs/compat-errno.h>
#include <glusterfs/syscall.h>
#include <glusterfs/logging.h>
#include "posix-messages.h"
#include "posix-mem-types.h"
#include "posix-gfid-path.h"
#include "posix.h"

gf_boolean_t
posix_is_gfid2path_xattr(const char *name)
{
    if (name && strncmp(GFID2PATH_XATTR_KEY_PREFIX, name,
                        GFID2PATH_XATTR_KEY_PREFIX_LENGTH) == 0)
        return _gf_true;

    return _gf_false;
}

static int gf_posix_xattr_enotsup_log;

int32_t
posix_get_gfid2path(xlator_t *this, inode_t *inode, const char *real_path,
                    int *op_errno, dict_t *dict)
{
    int ret = 0;
    char *path = NULL;
    ssize_t size = 0;
    char *list = NULL;
    int32_t list_offset = 0;
    int32_t i = 0;
    int32_t j = 0;
    char *paths[MAX_GFID2PATH_LINK_SUP] = {
        NULL,
    };
    char *value = NULL;
    size_t remaining_size = 0;
    size_t bytes = 0;
    char keybuffer[4096] = {
        0,
    };

    uuid_t pargfid = {
        0,
    };
    gf_boolean_t have_val = _gf_false;
    struct posix_private *priv = NULL;
    char pargfid_str[UUID_CANONICAL_FORM_LEN + 1] = {
        0,
    };
    gf_boolean_t found = _gf_false;
    int len;

    priv = this->private;

    if (IA_ISDIR(inode->ia_type)) {
        ret = posix_resolve_dirgfid_to_path(inode->gfid, priv->base_path, NULL,
                                            &path);
        if (ret < 0) {
            ret = -1;
            goto err;
        }
        ret = dict_set_dynstr(dict, GFID2PATH_VIRT_XATTR_KEY, path);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_WARNING, -ret, P_MSG_DICT_SET_FAILED,
                   "could not set "
                   "value for key (%s)",
                   GFID2PATH_VIRT_XATTR_KEY);
            goto err;
        }
        found = _gf_true;
    } else {
        char value_buf[8192] = {
            0,
        };
        char xattr_value[8192] = {
            0,
        };
        have_val = _gf_false;
        size = sys_llistxattr(real_path, value_buf, sizeof(value_buf) - 1);
        if (size > 0) {
            have_val = _gf_true;
        } else {
            if (errno == ERANGE) {
                gf_msg(this->name, GF_LOG_DEBUG, errno, P_MSG_XATTR_FAILED,
                       "listxattr failed due to overflow of"
                       " buffer on %s ",
                       real_path);
                size = sys_llistxattr(real_path, NULL, 0);
            }
            if (size == -1) {
                *op_errno = errno;
                if ((errno == ENOTSUP) || (errno == ENOSYS)) {
                    GF_LOG_OCCASIONALLY(gf_posix_xattr_enotsup_log, this->name,
                                        GF_LOG_WARNING,
                                        "Extended attributes not "
                                        "supported (try remounting"
                                        " brick with 'user_xattr' "
                                        "flag)");
                } else {
                    gf_msg(this->name, GF_LOG_ERROR, errno, P_MSG_XATTR_FAILED,
                           "listxattr failed on %s", real_path);
                }
                goto err;
            }
            if (size == 0)
                goto done;
        }

        list = GF_MALLOC(size, gf_posix_mt_char);
        if (!list) {
            *op_errno = errno;
            goto err;
        }
        if (have_val) {
            memcpy(list, value_buf, size);
        } else {
            size = sys_llistxattr(real_path, list, size);
            if (size < 0) {
                ret = -1;
                *op_errno = errno;
                goto err;
            }
        }
        remaining_size = size;
        list_offset = 0;
        while (remaining_size > 0) {
            len = snprintf(keybuffer, sizeof(keybuffer), "%s",
                           list + list_offset);

            if (!posix_is_gfid2path_xattr(keybuffer)) {
                goto ignore;
            }

            found = _gf_true;
            size = sys_lgetxattr(real_path, keybuffer, xattr_value,
                                 sizeof(xattr_value) - 1);
            if (size == -1) {
                ret = -1;
                *op_errno = errno;
                gf_msg(this->name, GF_LOG_ERROR, errno, P_MSG_XATTR_FAILED,
                       "getxattr failed on"
                       " %s: key = %s ",
                       real_path, keybuffer);
                break;
            }

            /* Parse pargfid from xattr value*/
            strncpy(pargfid_str, xattr_value, 36);
            pargfid_str[36] = '\0';
            gf_uuid_parse(pargfid_str, pargfid);

            /* Convert pargfid to path */
            ret = posix_resolve_dirgfid_to_path(pargfid, priv->base_path,
                                                &xattr_value[37], &paths[i]);
            i++;

        ignore:
            remaining_size -= (len + 1);
            list_offset += (len + 1);
        } /* while (remaining_size > 0) */

        /* gfid2path xattr is absent in the list of xattrs */
        if (!found) {
            ret = -1;
            /*
             * ENODATA because xattr is not present in the
             * list of xattrs. Thus the consumer should
             * face error instead of a success and a empty
             * string in the dict for the key.
             */
            *op_errno = ENODATA;
            goto err;
        }

        /*
         * gfid2path xattr is found in list of xattrs, but getxattr
         * on the 1st gfid2path xattr itself failed and the while
         * loop above broke. So there is nothing in the value. So
         * it would be better not to send "" as the value for any
         * key, as it is not true.
         */
        if (found && !i)
            goto err; /* both errno and ret are set before beak */

        /* Calculate memory to be allocated */
        for (j = 0; j < i; j++) {
            bytes += strlen(paths[j]);
            if (j < i - 1)
                bytes += strlen(priv->gfid2path_sep);
        }
        value = GF_CALLOC(bytes + 1, sizeof(char), gf_posix_mt_char);
        if (!value) {
            ret = -1;
            *op_errno = errno;
            goto err;
        }

        for (j = 0; j < i; j++) {
            strcat(value, paths[j]);
            if (j != i - 1)
                strcat(value, priv->gfid2path_sep);
        }
        value[bytes] = '\0';

        ret = dict_set_dynptr(dict, GFID2PATH_VIRT_XATTR_KEY, value, bytes);
        if (ret < 0) {
            *op_errno = -ret;
            gf_msg(this->name, GF_LOG_ERROR, *op_errno, P_MSG_DICT_SET_FAILED,
                   "dict set operation "
                   "on %s for the key %s failed.",
                   real_path, GFID2PATH_VIRT_XATTR_KEY);
            GF_FREE(value);
            goto err;
        }
    }

done:
    for (j = 0; j < i; j++) {
        if (paths[j])
            GF_FREE(paths[j]);
    }
    ret = 0;
    GF_FREE(list);
    return ret;
err:
    if (path)
        GF_FREE(path);
    for (j = 0; j < i; j++) {
        if (paths[j])
            GF_FREE(paths[j]);
    }
    GF_FREE(list);
    return ret;
}
