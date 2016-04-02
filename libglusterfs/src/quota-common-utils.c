/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/


#include "dict.h"
#include "logging.h"
#include "byte-order.h"
#include "quota-common-utils.h"
#include "common-utils.h"
#include "libglusterfs-messages.h"

gf_boolean_t
quota_meta_is_null (const quota_meta_t *meta)
{
        if (meta->size == 0 &&
            meta->file_count == 0 &&
            meta->dir_count == 0)
                return _gf_true;

        return _gf_false;
}

int32_t
quota_data_to_meta (data_t *data, char *key, quota_meta_t *meta)
{
        int32_t        ret      = -1;
        quota_meta_t  *value    = NULL;
        int64_t       *size     = NULL;

        if (!data || !key || !meta)
                goto out;

        if (data->len > sizeof (int64_t)) {
                value = (quota_meta_t *) data->data;
                meta->size = ntoh64 (value->size);
                meta->file_count = ntoh64 (value->file_count);
                if (data->len > (sizeof (int64_t)) * 2)
                        meta->dir_count  = ntoh64 (value->dir_count);
                else
                        meta->dir_count = 0;
        } else {
                size = (int64_t *) data->data;
                meta->size = ntoh64 (*size);
                meta->file_count = 0;
                meta->dir_count = 0;
                /* This can happen during software upgrade.
                 * Older version of glusterfs will not have inode count.
                 * Return failure, this will be healed as part of lookup
                 */
                gf_msg_callingfn ("quota", GF_LOG_DEBUG, 0,
                                  LG_MSG_QUOTA_XATTRS_MISSING, "Object quota "
                                  "xattrs missing: len = %d", data->len);
                ret = -2;
                goto out;
        }

        ret = 0;
out:

        return ret;
}

int32_t
quota_dict_get_inode_meta (dict_t *dict, char *key, quota_meta_t *meta)
{
        int32_t        ret      = -1;
        data_t        *data     = NULL;

        if (!dict || !key || !meta)
                goto out;

        data = dict_get (dict, key);
        if (!data || !data->data)
                goto out;

        ret = quota_data_to_meta (data, key, meta);

out:

        return ret;
}

int32_t
quota_dict_get_meta (dict_t *dict, char *key, quota_meta_t *meta)
{
        int32_t        ret      = -1;

        ret = quota_dict_get_inode_meta (dict, key, meta);
        if (ret == -2)
                ret = 0;

        return ret;
}

int32_t
quota_dict_set_meta (dict_t *dict, char *key, const quota_meta_t *meta,
                     ia_type_t ia_type)
{
        int32_t         ret      = -ENOMEM;
        quota_meta_t   *value    = NULL;

        value = GF_CALLOC (1, sizeof (quota_meta_t), gf_common_quota_meta_t);
        if (value == NULL) {
                goto out;
        }

        value->size = hton64 (meta->size);
        value->file_count = hton64 (meta->file_count);
        value->dir_count = hton64 (meta->dir_count);

        if (ia_type == IA_IFDIR) {
                ret = dict_set_bin (dict, key, value, sizeof (*value));
        } else {
                /* For a file we don't need to store dir_count in the
                 * quota size xattr, so we set the len of the data in the dict
                 * as 128bits, so when the posix xattrop reads the dict, it only
                 * performs operations on size and file_count
                 */
                ret = dict_set_bin (dict, key, value,
                                    sizeof (*value) - sizeof (int64_t));
        }

        if (ret < 0) {
                gf_msg_callingfn ("quota", GF_LOG_ERROR, 0,
                                  LG_MSG_DICT_SET_FAILED, "dict set failed");
                GF_FREE (value);
        }

out:
        return ret;
}

int32_t
quota_conf_read_header (int fd, char *buf)
{
        int    header_len      = 0;
        int    ret             = 0;

        header_len = strlen (QUOTA_CONF_HEADER);

        ret = gf_nread (fd, buf, header_len);
        if (ret <= 0) {
                goto out;
        } else if (ret > 0 && ret != header_len) {
                ret = -1;
                goto out;
        }

        buf[header_len-1] = 0;

out:
        if (ret < 0)
                gf_msg_callingfn ("quota", GF_LOG_ERROR, 0,
                                  LG_MSG_QUOTA_CONF_ERROR, "failed to read "
                                  "header from a quota conf");

        return ret;
}

int32_t
quota_conf_read_version (int fd, float *version)
{
        int    ret             = 0;
        char   buf[PATH_MAX]   = "";
        char  *tail            = NULL;
        float  value           = 0.0f;

        ret = quota_conf_read_header (fd, buf);
        if (ret == 0) {
                /* quota.conf is empty */
                value = GF_QUOTA_CONF_VERSION;
                goto out;
        } else if (ret < 0) {
                goto out;
        }

        value = strtof ((buf + strlen(buf) - 3), &tail);
        if (tail[0] != '\0') {
                ret = -1;
                gf_msg_callingfn ("quota", GF_LOG_ERROR, 0,
                                  LG_MSG_QUOTA_CONF_ERROR, "invalid quota conf"
                                  " version");
                goto out;
        }

        ret = 0;

out:
        if (ret >= 0)
                *version = value;
        else
                gf_msg_callingfn ("quota", GF_LOG_ERROR, 0,
                                  LG_MSG_QUOTA_CONF_ERROR, "failed to "
                                  "read version from a quota conf header");

        return ret;
}

int32_t
quota_conf_read_gfid (int fd, void *buf, char *type, float version)
{
        int           ret         = 0;

        ret = gf_nread (fd, buf, 16);
        if (ret <= 0)
                goto out;

        if (ret != 16) {
                ret = -1;
                goto out;
        }

        if (version >= 1.2f) {
                ret = gf_nread (fd, type, 1);
                if (ret != 1) {
                        ret = -1;
                        goto out;
                }
                ret = 17;
        } else {
                *type = GF_QUOTA_CONF_TYPE_USAGE;
        }

out:
        if (ret < 0)
                gf_msg_callingfn ("quota", GF_LOG_ERROR, 0,
                                  LG_MSG_QUOTA_CONF_ERROR, "failed to "
                                  "read gfid from a quota conf");

        return ret;
}

int32_t
quota_conf_skip_header (int fd)
{
        return gf_skip_header_section (fd, strlen (QUOTA_CONF_HEADER));
}

