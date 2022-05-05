/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#ifndef _GLUSTERD_GEO_REP_H_
#define _GLUSTERD_GEO_REP_H_

#ifndef GSYNC_CONF_TEMPLATE
#define GSYNC_CONF_TEMPLATE GEOREP "/gsyncd_template.conf"
#endif

#define GLUSTERD_COMMON_PEM_PUB_FILE "/geo-replication/common_secret.pem.pub"
#define GLUSTERD_CREATE_HOOK_SCRIPT                                            \
    "/hooks/1/gsync-create/post/"                                              \
    "S56glusterd-geo-rep-create-post.sh"

/* <secondary host>::<secondary volume> */
#define SECONDARY_URL_INFO_MAX (_POSIX_HOST_NAME_MAX + GD_VOLUME_NAME_MAX + 3)

/* secondary info format:
 * <primary host uuid>:ssh://{<secondary_user>@}<secondary host>::<secondary
 * volume> \
 * :<secondary volume uuid> */
#define VOLINFO_SECONDARY_URL_MAX                                              \
    (LOGIN_NAME_MAX + (2 * GF_UUID_BUF_SIZE) + SECONDARY_URL_INFO_MAX + 10)

struct gsync_config_opt_vals_ {
    char *op_name;
    char *values[GEO_CONF_MAX_OPT_VALS];
    int no_of_pos_vals;
    gf_boolean_t case_sensitive;
};

typedef struct glusterd_gsync_status_temp {
    dict_t *rsp_dict;
    glusterd_volinfo_t *volinfo;
    char *node;
} glusterd_gsync_status_temp_t;

typedef struct gsync_status_param {
    glusterd_volinfo_t *volinfo;
    int is_active;
} gsync_status_param_t;

int
gsync_status(char *primary, char *secondary, char *conf_path, int *status,
             gf_boolean_t *is_template_in_use);

void
glusterd_check_geo_rep_configured(glusterd_volinfo_t *volinfo,
                                  gf_boolean_t *flag);
int
_get_secondary_status(dict_t *dict, char *key, data_t *value, void *data);
int
glusterd_check_geo_rep_running(gsync_status_param_t *param, char **op_errstr);

int
glusterd_get_gsync_status_mst(glusterd_volinfo_t *volinfo, dict_t *rsp_dict,
                              char *node);
#endif
