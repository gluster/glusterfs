/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
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
#define VKEY_FEATURES_SOFT_LIMIT  "features.soft-limit"
#define VKEY_MARKER_XTIME         GEOREP".indexing"
#define VKEY_MARKER_XTIME_FORCE   GEOREP".ignore-pid-check"
#define VKEY_CHANGELOG            "changelog.changelog"
#define VKEY_FEATURES_QUOTA       "features.quota"

#define AUTH_ALLOW_MAP_KEY "auth.allow"
#define AUTH_REJECT_MAP_KEY "auth.reject"
#define NFS_DISABLE_MAP_KEY "nfs.disable"
#define AUTH_ALLOW_OPT_KEY "auth.addr.*.allow"
#define AUTH_REJECT_OPT_KEY "auth.addr.*.reject"
#define NFS_DISABLE_OPT_KEY "nfs.*.disable"


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
        OPT_FLAG_FORCE = 0x01,      // option needs force to be reset
        OPT_FLAG_XLATOR_OPT = 0x02, // option enables/disables xlators
        OPT_FLAG_CLIENT_OPT = 0x04, // option affects clients
} gd_volopt_flags_t;

typedef enum {
        GF_XLATOR_POSIX = 0,
        GF_XLATOR_ACL,
        GF_XLATOR_LOCKS,
        GF_XLATOR_IOT,
        GF_XLATOR_INDEX,
        GF_XLATOR_MARKER,
        GF_XLATOR_IO_STATS,
        GF_XLATOR_BD,
        GF_XLATOR_NONE,
} glusterd_server_xlator_t;

/* As of now debug xlators can be loaded only below fuse in the client
 * graph via cli. More xlators can be added below when the cli option
 * for adding debug xlators anywhere in the client graph has to be made
 * available.
 */
typedef enum {
        GF_CLNT_XLATOR_FUSE = 0,
        GF_CLNT_XLATOR_NONE,
} glusterd_client_xlator_t;

typedef enum  { DOC, NO_DOC, GLOBAL_DOC, GLOBAL_NO_DOC } option_type_t;

typedef int (*vme_option_validation) (dict_t *dict, char *key, char *value,
                                      char **op_errstr);

struct volopt_map_entry {
        char *key;
        char *voltype;
        char *option;
        char *value;
        option_type_t type;
        uint32_t flags;
        uint32_t op_version;
        char *description;
        vme_option_validation validate_fn;
        /* If client_option is true, the option affects clients.
         * this is used to calculate client-op-version of volumes
         */
        //gf_boolean_t client_option;
};

int glusterd_create_rb_volfiles (glusterd_volinfo_t *volinfo,
                                 glusterd_brickinfo_t *brickinfo);

int glusterd_create_volfiles_and_notify_services (glusterd_volinfo_t *volinfo);

void glusterd_get_nfs_filepath (char *filename);

void glusterd_get_shd_filepath (char *filename);

int glusterd_create_nfs_volfile ();
int glusterd_create_shd_volfile ();
int glusterd_create_quotad_volfile ();

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
int generate_client_volfiles (glusterd_volinfo_t *volinfo,
                              glusterd_client_type_t client_type);
int glusterd_get_volopt_content (dict_t *dict, gf_boolean_t xml_out);
char*
glusterd_get_trans_type_rb (gf_transport_type ttype);
int
glusterd_check_nfs_volfile_identical (gf_boolean_t *identical);
int
glusterd_check_nfs_topology_identical (gf_boolean_t *identical);

uint32_t
glusterd_get_op_version_for_key (char *key);

gf_boolean_t
gd_is_client_option (char *key);

gf_boolean_t
gd_is_xlator_option (char *key);

gf_boolean_t
gd_is_boolean_option (char *key);

#endif
