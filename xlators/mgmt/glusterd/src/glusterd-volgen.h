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

#if (HAVE_LIB_XML)
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>
#endif

#include "glusterd.h"
#include "glusterd-messages.h"

/* volopt map key name definitions */

#define VKEY_DIAG_CNT_FOP_HITS    "diagnostics.count-fop-hits"
#define VKEY_DIAG_LAT_MEASUREMENT "diagnostics.latency-measurement"
#define VKEY_FEATURES_LIMIT_USAGE "features.limit-usage"
#define VKEY_FEATURES_SOFT_LIMIT  "features.soft-limit"
#define VKEY_MARKER_XTIME         GEOREP".indexing"
#define VKEY_MARKER_XTIME_FORCE   GEOREP".ignore-pid-check"
#define VKEY_CHANGELOG            "changelog.changelog"
#define VKEY_FEATURES_QUOTA       "features.quota"
#define VKEY_FEATURES_INODE_QUOTA "features.inode-quota"
#define VKEY_FEATURES_TRASH       "features.trash"
#define VKEY_FEATURES_BITROT      "features.bitrot"
#define VKEY_FEATURES_SCRUB       "features.scrub"
#define VKEY_PARALLEL_READDIR     "performance.parallel-readdir"
#define VKEY_READDIR_AHEAD        "performance.readdir-ahead"
#define VKEY_RDA_CACHE_LIMIT      "performance.rda-cache-limit"
#define VKEY_RDA_REQUEST_SIZE     "performance.rda-request-size"

#define AUTH_ALLOW_MAP_KEY "auth.allow"
#define AUTH_REJECT_MAP_KEY "auth.reject"
#define NFS_DISABLE_MAP_KEY "nfs.disable"
#define AUTH_ALLOW_OPT_KEY "auth.addr.*.allow"
#define AUTH_REJECT_OPT_KEY "auth.addr.*.reject"
#define NFS_DISABLE_OPT_KEY "nfs.*.disable"

#define SSL_OWN_CERT_OPT    "ssl.own-cert"
#define SSL_PRIVATE_KEY_OPT "ssl.private-key"
#define SSL_CA_LIST_OPT     "ssl.ca-list"
#define SSL_CRL_PATH_OPT    "ssl.crl-path"
#define SSL_CERT_DEPTH_OPT  "ssl.certificate-depth"
#define SSL_CIPHER_LIST_OPT "ssl.cipher-list"
#define SSL_DH_PARAM_OPT    "ssl.dh-param"
#define SSL_EC_CURVE_OPT    "ssl.ec-curve"


typedef enum {
        GF_CLIENT_TRUSTED,
        GF_CLIENT_OTHER
} glusterd_client_type_t;

/* It indicates the type of volfile that the graph is built for */
typedef enum {
        GF_REBALANCED = 1,
        GF_QUOTAD,
        GF_SNAPD,
} glusterd_graph_type_t;

struct volgen_graph {
        char **errstr;
        glusterfs_graph_t graph;
        glusterd_graph_type_t type;
};
typedef struct volgen_graph volgen_graph_t;

typedef int (*glusterd_graph_builder_t) (volgen_graph_t *graph,
                                         dict_t *mod_dict);

#define COMPLETE_OPTION(key, completion, ret)                           \
        do {                                                            \
                if (!strchr (key, '.')) {                               \
                        ret = option_complete (key, &completion);       \
                        if (ret) {                                      \
                                gf_msg ("", GF_LOG_ERROR, ENOMEM,       \
                                        GD_MSG_NO_MEMORY, "Out of memory"); \
                                return _gf_false;                       \
                        }                                               \
                                                                        \
                        if (!completion) {                              \
                                gf_msg ("", GF_LOG_ERROR, 0,            \
                                        GD_MSG_INVALID_ENTRY,           \
                                        "option %s does not"            \
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
        OPT_FLAG_NEVER_RESET = 0x08, /* option which should not be reset */
} gd_volopt_flags_t;

typedef enum {
        GF_XLATOR_POSIX = 0,
        GF_XLATOR_ACL,
        GF_XLATOR_LOCKS,
        GF_XLATOR_LEASES,
        GF_XLATOR_UPCALL,
        GF_XLATOR_IOT,
        GF_XLATOR_INDEX,
        GF_XLATOR_MARKER,
        GF_XLATOR_IO_STATS,
        GF_XLATOR_BD,
        GF_XLATOR_SERVER,
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

typedef int (*vme_option_validation) (glusterd_volinfo_t *volinfo, dict_t *dict,
                                      char *key, char *value, char **op_errstr);

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

typedef
int (*brick_xlator_builder) (volgen_graph_t *graph,
                             glusterd_volinfo_t *volinfo, dict_t *set_dict,
                             glusterd_brickinfo_t *brickinfo);

struct volgen_brick_xlator {
        /* function that builds a xlator */
        brick_xlator_builder builder;
        /* debug key for a xlator that
         * gets used for adding debug translators like trace, error-gen
         * before this xlator */
        char *dbg_key;
};
typedef struct volgen_brick_xlator volgen_brick_xlator_t;

int
glusterd_snapdsvc_create_volfile (glusterd_volinfo_t *volinfo);

int
glusterd_snapdsvc_generate_volfile (volgen_graph_t *graph,
                                    glusterd_volinfo_t *volinfo);

int
glusterd_create_global_volfile (glusterd_graph_builder_t builder,
                                char *filepath, dict_t  *mod_dict);

int
glusterd_create_rb_volfiles (glusterd_volinfo_t *volinfo,
                                 glusterd_brickinfo_t *brickinfo);

int
glusterd_create_volfiles (glusterd_volinfo_t *volinfo);

int
glusterd_create_volfiles_and_notify_services (glusterd_volinfo_t *volinfo);

int
glusterd_generate_client_per_brick_volfile (glusterd_volinfo_t *volinfo);

void
glusterd_get_nfs_filepath (char *filename);

void
glusterd_get_shd_filepath (char *filename);

int
build_shd_graph (volgen_graph_t *graph, dict_t *mod_dict);

int
build_nfs_graph (volgen_graph_t *graph, dict_t *mod_dict);

int
build_quotad_graph (volgen_graph_t *graph, dict_t *mod_dict);

int
build_rebalance_volfile (glusterd_volinfo_t *volinfo, char *filepath,
                         dict_t *mod_dict);

int
build_bitd_graph (volgen_graph_t *graph, dict_t *mod_dict);

int
build_scrub_graph (volgen_graph_t *graph, dict_t *mod_dict);

int
glusterd_delete_volfile (glusterd_volinfo_t *volinfo,
                             glusterd_brickinfo_t *brickinfo);
int
glusterd_delete_snap_volfile (glusterd_volinfo_t *volinfo,
                              glusterd_volinfo_t *snap_volinfo,
                              glusterd_brickinfo_t *brickinfo);

int
glusterd_volinfo_get (glusterd_volinfo_t *volinfo, char *key, char **value);

int
glusterd_volinfo_get_boolean (glusterd_volinfo_t *volinfo, char *key);

int
glusterd_validate_globalopts (glusterd_volinfo_t *volinfo, dict_t *val_dict,
                              char **op_errstr);

int
glusterd_validate_localopts (dict_t *val_dict, char **op_errstr);

gf_boolean_t
glusterd_check_globaloption (char *key);

gf_boolean_t
glusterd_check_voloption_flags (char *key, int32_t flags);

gf_boolean_t
glusterd_is_valid_volfpath (char *volname, char *brick);

int
generate_brick_volfiles (glusterd_volinfo_t *volinfo);

int
generate_snap_brick_volfiles (glusterd_volinfo_t *volinfo,
                                  glusterd_volinfo_t *snap_volinfo);
int
generate_client_volfiles (glusterd_volinfo_t *volinfo,
                              glusterd_client_type_t client_type);
int
generate_snap_client_volfiles (glusterd_volinfo_t *actual_volinfo,
                               glusterd_volinfo_t *snap_volinfo,
                               glusterd_client_type_t client_type,
                               gf_boolean_t vol_restore);

int
_get_xlator_opt_key_from_vme ( struct volopt_map_entry *vme, char **key);

void
_free_xlator_opt_key (char *key);


#if (HAVE_LIB_XML)
int
init_sethelp_xml_doc (xmlTextWriterPtr *writer, xmlBufferPtr  *buf);

int
xml_add_volset_element (xmlTextWriterPtr writer, const char *name,
                        const char *def_val, const char *dscrpt);
int
end_sethelp_xml_doc (xmlTextWriterPtr writer);
#endif /* HAVE_LIB_XML */

char*
glusterd_get_trans_type_rb (gf_transport_type ttype);

uint32_t
glusterd_get_op_version_for_key (char *key);

gf_boolean_t
gd_is_client_option (char *key);

gf_boolean_t
gd_is_xlator_option (char *key);

gf_boolean_t
gd_is_boolean_option (char *key);


char*
volgen_get_shd_key (int type);

int
glusterd_volopt_validate (glusterd_volinfo_t *volinfo, dict_t *dict, char *key,
                          char *value, char **op_errstr);
gf_boolean_t
gd_is_self_heal_enabled (glusterd_volinfo_t *volinfo, dict_t *dict);

int
generate_dummy_client_volfiles (glusterd_volinfo_t *volinfo);

#endif
