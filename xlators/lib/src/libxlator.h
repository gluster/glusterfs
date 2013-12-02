/*
   Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _LIBXLATOR_H
#define _LIBXLATOR_H


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "logging.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat.h"
#include "compat-errno.h"


#define MARKER_XATTR_PREFIX "trusted.glusterfs"
#define XTIME               "xtime"
#define VOLUME_MARK         "volume-mark"
#define GF_XATTR_MARKER_KEY MARKER_XATTR_PREFIX "." VOLUME_MARK
#define UUID_SIZE 36
#define MARKER_UUID_TYPE    1
#define MARKER_XTIME_TYPE   2
#define GF_XATTR_QUOTA_SIZE_KEY "trusted.glusterfs.quota.size"
#define GF_XATTR_QUOTA_LIMIT_LIST "trusted.limit.list"


typedef int32_t (*xlator_specf_unwind_t) (call_frame_t *frame,
                                          int op_ret, int op_errno,
                                          dict_t *dict, dict_t *xdata);


struct volume_mark {
        uint8_t major;
        uint8_t minor;
        uint8_t uuid[16];
        uint8_t retval;
        uint32_t sec;
        uint32_t usec;
}__attribute__ ((__packed__));


/*
 * The enumerated type here
 * is used to index two kind
 * of integer arrays:
 * - gauges
 * - counters

 * A counter is used internally,
 * in getxattr callbacks, to count
 * the results, categorized as
 * the enum names suggest. So values
 * in the counter are always non-negative.

 * Gauges are part of the API.
 * The caller passes one to the
 * top-level aggregator function,
 * cluster_getmarkerattr(). The gauge
 * defines an evaluation policy for the
 * counter. That is, at the
 * end of the aggregation process
 * the gauge is matched against the
 * counter, and the policy
 * represented by the gauge decides
 * whether to return with success or failure,
 * and in latter case, what particular failure
 * case (errno).

 * The rules are the following: for some index i,
 * - if gauge[i] == 0, no requirement is set
 *   against counter[i];
 * - if gauge[i] > 0, counter[i] >= gauge[i]
 *   is required;
 * - if gauge[i] < 0, counter[i] < |gauge[i]|
 *   is required.

 * If the requirement is not met, then i is mapped
 * to the respective errno (MCNT_ENOENT -> ENOENT),
 * or in lack of that, EINVAL.

 * Cf. evaluate_marker_results() and marker_idx_errno_map[]
 * in libxlator.c

 * We provide two default gauges, one inteded for xtime
 * aggregation, other for volume mark aggregation. The
 * policies they represent agree with the hard-coded
 * one prior to gauges. Cf. marker_xtime_default_gauge
 * and marker_uuid_default_gauge in libxlator.c
 */

typedef enum {
        MCNT_FOUND,
        MCNT_NOTFOUND,
        MCNT_ENODATA,
        MCNT_ENOTCONN,
        MCNT_ENOENT,
        MCNT_EOTHER,
        MCNT_MAX
} marker_result_idx_t;

extern int marker_xtime_default_gauge[];
extern int marker_uuid_default_gauge[];

struct marker_str {
        struct volume_mark    *volmark;
        data_t                *data;

        uint32_t               host_timebuf[2];
        uint32_t               net_timebuf[2];
        int32_t                call_count;
        int                    gauge[MCNT_MAX];
        int                    count[MCNT_MAX];

        xlator_specf_unwind_t  xl_specf_unwind;
        void                  *xl_local;
        char                  *vol_uuid;
        uint8_t                retval;
};

typedef struct marker_str xl_marker_local_t;

int32_t
cluster_markerxtime_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, dict_t *dict, dict_t *xdata);

int32_t
cluster_markeruuid_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, dict_t *dict, dict_t *xdata);

int32_t
cluster_getmarkerattr (call_frame_t *frame,xlator_t *this, loc_t *loc,
                       const char *name, void *xl_local,
                       xlator_specf_unwind_t xl_specf_getxattr_unwind,
                       xlator_t **sub_volumes, int count, int type,
                       int *gauge, char *vol_uuid);

int
match_uuid_local (const char *name, char *uuid);

int
gf_get_min_stime (xlator_t *this, dict_t *dst, char *key, data_t *value);

int
gf_get_max_stime (xlator_t *this, dict_t *dst, char *key, data_t *value);

#endif /* !_LIBXLATOR_H */
