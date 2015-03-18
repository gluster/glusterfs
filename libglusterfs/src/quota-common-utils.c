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

int32_t
quota_dict_get_meta (dict_t *dict, char *key, quota_meta_t *meta)
{
        int32_t        ret      = -1;
        data_t        *data     = NULL;
        quota_meta_t  *value    = NULL;
        int64_t       *size     = NULL;

        if (!dict || !key || !meta)
                goto out;

        data = dict_get (dict, key);
        if (!data || !data->data)
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
                gf_log_callingfn ("quota", GF_LOG_DEBUG, "Object quota xattrs "
                                  "missing: len = %d", data->len);
                ret = -2;
                goto out;
        }

        ret = 0;
out:

        return ret;
}

int32_t
quota_dict_set_meta (dict_t *dict, char *key, const quota_meta_t *meta,
                     ia_type_t ia_type)
{
        int32_t         ret      = -1;
        quota_meta_t   *value    = NULL;

        value = GF_CALLOC (1, sizeof (quota_meta_t), gf_common_quota_meta_t);
        if (value == NULL) {
                gf_log_callingfn ("quota", GF_LOG_ERROR,
                                  "Memory allocation failed");
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
                gf_log_callingfn ("quota", GF_LOG_ERROR, "dict set failed");
                GF_FREE (value);
        }

out:
        return ret;
}

