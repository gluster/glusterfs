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


typedef int32_t (*xlator_specf_unwind_t) (void *getxattr, call_frame_t *frame,
                                         int op_ret, int op_errno, dict_t *dict);


struct volume_mark {
        uint8_t major;
        uint8_t minor;
        uint8_t uuid[16];
        uint8_t retval;
        uint32_t sec;
        uint32_t usec;
}__attribute__ ((__packed__));

struct marker_str {
        struct volume_mark    *volmark;
        data_t                *data;

        uint32_t               host_timebuf[2];
        uint32_t               net_timebuf[2];
        int32_t                call_count;
        unsigned               has_xtime:1;
        int32_t                enoent_count;
        int32_t                enotconn_count;
        int32_t                enodata_count;
        int32_t                noxtime_count;

        int                    esomerr;

        xlator_specf_unwind_t  xl_specf_unwind;
        void                  *xl_local;
        char                  *vol_uuid;
};

static inline gf_boolean_t
marker_has_volinfo (struct marker_str *marker)
{
       if (marker->volmark)
                return _gf_true;
       else
                return _gf_false;
}

int32_t
cluster_markerxtime_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, dict_t *dict);

int32_t
cluster_markeruuid_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, dict_t *dict);

int32_t
cluster_getmarkerattr (call_frame_t *frame,xlator_t *this, loc_t *loc,
                       const char *name, void *xl_local,
                       xlator_specf_unwind_t xl_specf_getxattr_unwind,
                       xlator_t **sub_volumes, int count, int type,
                       char *vol_uuid);

int
match_uuid_local (const char *name, char *uuid);




#endif /* !_LIBXLATOR_H */
