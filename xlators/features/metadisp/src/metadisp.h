/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef GF_METADISP_H_
#define GF_METADISP_H_

#include <glusterfs/glusterfs.h>
#include <glusterfs/logging.h>
#include <glusterfs/dict.h>
#include <glusterfs/xlator.h>
#include <glusterfs/defaults.h>

#define METADATA_CHILD(_this) FIRST_CHILD(_this)
#define DATA_CHILD(_this) SECOND_CHILD(_this)

int32_t
build_backend_loc(uuid_t gfid, loc_t *src_loc, loc_t *dst_loc);

#define METADISP_TRACE(_args...) gf_log("metadisp", GF_LOG_INFO, _args)

#define METADISP_FILTER_ROOT(_op, _args...)                                    \
    if (strcmp(loc->path, "/") == 0) {                                         \
        STACK_WIND(frame, default_##_op##_cbk, METADATA_CHILD(this),           \
                   METADATA_CHILD(this)->fops->_op, _args);                    \
        return 0;                                                              \
    }

#define METADISP_FILTER_ROOT_BY_GFID(_op, _gfid, _args...)                     \
    if (__is_root_gfid(_gfid)) {                                               \
        STACK_WIND(frame, default_##_op##_cbk, METADATA_CHILD(this),           \
                   METADATA_CHILD(this)->fops->_op, _args);                    \
        return 0;                                                              \
    }

#define RESOLVE_GFID_REQ(_dict, _dest, _lbl)                                   \
    VALIDATE_OR_GOTO(dict_get_ptr(_dict, "gfid-req", (void **)&_dest) == 0,    \
                     _lbl)

#endif /* __TEMPLATE_H__ */
