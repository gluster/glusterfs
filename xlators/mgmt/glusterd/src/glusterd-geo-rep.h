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
#define GSYNC_CONF_TEMPLATE GEOREP"/gsyncd_template.conf"
#endif

/* <slave host>::<slave volume> */
#define SLAVE_URL_INFO_MAX (_POSIX_HOST_NAME_MAX + GD_VOLUME_NAME_MAX + 3)

/* slave info format:
 * <master host uuid>:ssh://{<slave_user>@}<slave host>::<slave volume> \
 * :<slave volume uuid> */
#define VOLINFO_SLAVE_URL_MAX (LOGIN_NAME_MAX + (2*GF_UUID_BUF_SIZE) \
                                 + SLAVE_URL_INFO_MAX + 10)

typedef struct glusterd_gsync_status_temp {
        dict_t *rsp_dict;
        glusterd_volinfo_t *volinfo;
        char *node;
} glusterd_gsync_status_temp_t;

typedef struct gsync_status_param {
        int is_active;
        glusterd_volinfo_t *volinfo;
} gsync_status_param_t;

int
gsync_status (char *master, char *slave, char *conf_path,
              int *status, gf_boolean_t *is_template_in_use);

void
glusterd_check_geo_rep_configured (glusterd_volinfo_t *volinfo,
                                   gf_boolean_t *flag);
int
_get_slave_status (dict_t *dict, char *key, data_t *value, void *data);
int
glusterd_check_geo_rep_running (gsync_status_param_t *param, char **op_errstr);
#endif

