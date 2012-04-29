/*
  Copyright (c) 2006-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _GLUSTERD_VOLGEN_H_
#define _GLUSTERD_VOLGEN_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterd.h"

/* volopt map key name definitions */

#define VKEY_DIAG_CNT_FOP_HITS    "diagnostics.count-fop-hits"
#define VKEY_DIAG_LAT_MEASUREMENT "diagnostics.latency-measurement"
#define VKEY_FEATURES_LIMIT_USAGE "features.limit-usage"
#define VKEY_MARKER_XTIME         GEOREP".indexing"
#define VKEY_FEATURES_QUOTA       "features.quota"
#define VKEY_PERF_STAT_PREFETCH   "performance.stat-prefetch"

typedef enum {
        GF_CLIENT_TRUSTED,
        GF_CLIENT_OTHER
} glusterd_client_type_t;

#define COMPLETE_OPTION(key, completion, ret)                           \
        do {                                                            \
                if (!strchr (key, '.')) {                               \
                        ret = option_complete (key, &completion);       \
                        if (ret) {                                      \
                                gf_log ("", GF_LOG_ERROR, "Out of memory"); \
                                return _gf_false;                       \
                        }                                               \
                                                                        \
                        if (!completion) {                              \
                                gf_log ("", GF_LOG_ERROR, "option %s does not" \
                                        "exist", key);                  \
                                return _gf_false;                       \
                        }                                               \
                }                                                       \
                                                                        \
                if (completion)                                         \
                        GF_FREE (completion);                           \
        } while (0);

typedef enum gd_volopt_flags_ {
        OPT_FLAG_NONE,
        OPT_FLAG_FORCE = 1,
} gd_volopt_flags_t;

int glusterd_create_rb_volfiles (glusterd_volinfo_t *volinfo,
                                 glusterd_brickinfo_t *brickinfo);

int glusterd_create_volfiles_and_notify_services (glusterd_volinfo_t *volinfo);

void glusterd_get_nfs_filepath (char *filename);

void glusterd_get_shd_filepath (char *filename);

int glusterd_create_nfs_volfile ();
int glusterd_create_shd_volfile ();

int glusterd_delete_volfile (glusterd_volinfo_t *volinfo,
                             glusterd_brickinfo_t *brickinfo);

int glusterd_volinfo_get (glusterd_volinfo_t *volinfo, char *key, char **value);
int glusterd_volinfo_get_boolean (glusterd_volinfo_t *volinfo, char *key);

int glusterd_validate_globalopts (glusterd_volinfo_t *volinfo, dict_t *val_dict, char **op_errstr);

int glusterd_validate_localopts (dict_t *val_dict, char **op_errstr);
gf_boolean_t glusterd_check_globaloption (char *key);
gf_boolean_t
glusterd_check_voloption_flags (char *key, int32_t flags);
gf_boolean_t
glusterd_is_valid_volfpath (char *volname, char *brick);
int generate_brick_volfiles (glusterd_volinfo_t *volinfo);
int glusterd_get_volopt_content (gf_boolean_t xml_out);
char*
glusterd_get_trans_type_rb (gf_transport_type ttype);
int
glusterd_check_nfs_volfile_identical (gf_boolean_t *identical);
#endif
