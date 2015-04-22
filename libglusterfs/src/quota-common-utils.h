/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _QUOTA_COMMON_UTILS_H
#define _QUOTA_COMMON_UTILS_H

#include "iatt.h"

struct _quota_limits {
        int64_t hl;
        int64_t sl;
} __attribute__ ((__packed__));
typedef struct _quota_limits quota_limits_t;

struct _quota_meta {
        int64_t size;
        int64_t file_count;
        int64_t dir_count;
} __attribute__ ((__packed__));
typedef struct _quota_meta quota_meta_t;

int32_t
quota_data_to_meta (data_t *data, char *key, quota_meta_t *meta);

int32_t
quota_dict_get_meta (dict_t *dict, char *key, quota_meta_t *meta);

int32_t
quota_dict_set_meta (dict_t *dict, char *key, const quota_meta_t *meta,
                     ia_type_t ia_type);

#endif /* _QUOTA_COMMON_UTILS_H */
