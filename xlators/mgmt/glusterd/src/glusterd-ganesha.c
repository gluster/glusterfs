/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/



#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "common-utils.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-nfs-svc.h"
#define MAXBUF 1024
#define DELIM "=\""

int
glusterd_check_ganesha_cmd (char *key, char *value, char **errstr, dict_t *dict)
{
        int                ret = 0;
        gf_boolean_t       b   = _gf_false;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (key);
        GF_ASSERT (value);

        if ((strcmp (key, "ganesha.enable") == 0) ||
           (strcmp (key, "features.ganesha") == 0)) {
                ret = gf_string2boolean (value, &b);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to parse bool"
                                "string");
                        goto out;
                }
                if ((strcmp (value, "on")) && (strcmp (value, "off"))) {
                        gf_log (this->name, GF_LOG_ERROR, "Invalid value"
                                "for volume set command. Use on/off only");
                        ret = -1;
                        goto out;
                }
                ret = glusterd_handle_ganesha_op (dict, errstr, key, value);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Handling NFS-Ganesha op"
                                "failed.");
                }
        }
out:
        return ret;
}

int
glusterd_op_stage_set_ganesha (dict_t *dict, char **op_errstr)
{
        int                             ret                     = -1;
        char                            *volname                = NULL;
        int                             exists                  = 0;
        char                            *key                    = NULL;
        char                            *value                  = NULL;
        char                            str[100]                = {0, } ;
        int                             dict_count              = 0;
        int                             flags                   = 0;
        char                            errstr[2048]            = {0, } ;
        glusterd_volinfo_t              *volinfo                = NULL;
        glusterd_conf_t                 *priv                   = NULL;
        xlator_t                        *this                   = NULL;

        GF_ASSERT (dict);
        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "key", &key);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                       "invalid key");
                goto out;
        }

        ret = dict_get_str (dict, "value", &value);
        if (ret) {
               gf_log (this->name, GF_LOG_ERROR,
                        "invalid key,value pair in 'global vol set'");
                goto out;
        }
out:

        if (ret) {
                if (!(*op_errstr)) {
                        *op_errstr = gf_strdup ("Error, Validation Failed");
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Error, Cannot Validate option :%s %s",
                                key, value);
                } else {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "Error, Cannot Validate option");
                }
        }
        return ret;
}

int
glusterd_op_set_ganesha (dict_t *dict, char **errstr)
{
        int                                      ret = 0;
        int                                      flags = 0;
        glusterd_volinfo_t                      *volinfo = NULL;
        char                                    *volname = NULL;
        xlator_t                                *this = NULL;
        glusterd_conf_t                         *priv = NULL;
        char                                    *key = NULL;
        char                                    *value = NULL;
        char                                     str[50] = {0, };
        int32_t                                  dict_count = 0;
        dict_t                                  *vol_opts = NULL;
        int count                                = 0;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (dict);

        priv = this->private;
        GF_ASSERT (priv);


        ret = dict_get_str (dict, "key", &key);
        if (ret) {
               gf_log (this->name, GF_LOG_ERROR,
                       "Couldn't get key in global option set");
                goto out;
       }

        ret = dict_get_str (dict, "value", &value);
        if (ret) {
               gf_log (this->name, GF_LOG_ERROR,
                        "Couldn't get value in global option set");
                goto out;
        }

        ret = glusterd_handle_ganesha_op (dict, errstr, key, value);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                "Initial NFS-Ganesha set up failed");
                ret = -1;
                goto out;
        }
        ret = dict_set_str(priv->opts, "features.ganesha", value);
        if (ret) {
                gf_log (this->name, GF_LOG_WARNING, "Failed to set"
                        " features.ganesha in dict.");
                goto out;
        }

        /* To do : Lock the global options file before writing */
        /* into this file. Bug ID : 1200254    */

        ret = glusterd_store_options (this, priv->opts);
        if (ret) {
             gf_log (this->name, GF_LOG_ERROR,
                                "Failed to store options");
                        goto out;
        }

out:
       gf_log (this->name, GF_LOG_DEBUG, "returning %d", ret);
       return ret;
}

/* Following 2 functions parse GANESHA_HA_CONF
 * The sample file looks like below,
 * HA_NAME="ganesha-ha-360"
 * HA_VOL_NAME="ha-state"
 * HA_VOL_MNT="/mount-point"
 * HA_VOL_SERVER="server1"
 * HA_CLUSTER_NODES="server1,server2"
 * VIP_rhs_1="10.x.x.x"
 * VIP_rhs_2="10.x.x.x." */

gf_boolean_t
is_ganesha_host (void)
{
        char    *host_from_file          = NULL;
        FILE    *fp;
        char    line[MAXBUF];
        gf_boolean_t    ret              = _gf_false;
        int     i                        = 1;
        xlator_t        *this            = NULL;

        this = THIS;

        fp = fopen (GANESHA_HA_CONF, "r");

        if (fp == NULL) {
                gf_log (this->name, GF_LOG_INFO, "couldn't open the file %s",
                        GANESHA_HA_CONF);
                return _gf_false;
        }

         while (fgets (line, sizeof(line), fp) != NULL) {
                /* Read GANESHA_HA_CONFIG till we find the HA VOL server */
                host_from_file = strstr ((char *)line, "HA_VOL_SERVER");
                if (host_from_file != NULL) {
                        host_from_file = strstr (host_from_file, DELIM);
                        host_from_file = host_from_file + strlen(DELIM);
                        i = strlen(host_from_file);
                        host_from_file[i - 2] = '\0';
                        break;
                }
        }

        ret = gf_is_local_addr (host_from_file);
        if (ret) {
                gf_log (this->name, GF_LOG_INFO, "ganesha host found "
                        "Hostname is %s", host_from_file);
        }

        fclose (fp);
        return ret;
}

/* Check if the localhost is listed as one of nfs-ganesha nodes */
gf_boolean_t
check_host_list (void)
{

        char    *host_from_file          = NULL;
        glusterd_conf_t     *priv        = NULL;
        char    *hostname                = NULL;
        FILE    *fp;
        char    line[MAXBUF];
        int     ret                      = _gf_false;
        int     i                        = 1;
        xlator_t        *this            = NULL;

        this = THIS;
        priv =  THIS->private;
        GF_ASSERT (priv);

        fp = fopen (GANESHA_HA_CONF, "r");

        if (fp == NULL) {
                gf_log (this->name, GF_LOG_INFO, "couldn't open the file %s",
                        GANESHA_HA_CONF);
                return 0;
        }

       while (fgets (line, sizeof(line), fp) != NULL) {
        /* Read GANESHA_HA_CONFIG till we find the list of HA_CLUSTER_NODES */
                hostname = strstr ((char *)line, "HA_CLUSTER_NODES");
                if (hostname != NULL) {
                        hostname = strstr (hostname, DELIM);
                        hostname = hostname + strlen(DELIM);
                        i = strlen (hostname);
                        hostname[i - 2] = '\0';
                        break;
                }
        }

        /* Hostname is a comma separated list now */
        hostname = strtok (hostname, ",");
        while (hostname != NULL) {
                ret = gf_is_local_addr (hostname);
                if (ret) {
                        gf_log (this->name, GF_LOG_INFO, "ganesha host found "
                                "Hostname is %s", hostname);
                        break;
                }
                hostname = strtok (NULL, ",");
        }

        fclose (fp);
        return ret;

}

int
create_export_config (char *volname, char **op_errstr)
{
        runner_t                runner                     = {0,};
        int                     ret                        = -1;

        GF_ASSERT(volname);
        runinit (&runner);
        runner_add_args (&runner, "sh",
                        GANESHA_PREFIX"/create-export-ganesha.sh",
                        CONFDIR, volname, NULL);
        ret = runner_run(&runner);

        if (ret)
                gf_asprintf (op_errstr, "Failed to create"
                            " NFS-Ganesha export config file.");

        return ret;
}

int
ganesha_manage_export (dict_t *dict, char *value, char **op_errstr)
{
        runner_t                 runner = {0,};
        int                      ret = -1;
        char                     str[1024];
        glusterd_volinfo_t      *volinfo = NULL;
        char                    *volname = NULL;
        xlator_t                *this    = NULL;
        int                     i = 1;

        runinit (&runner);
        this =  THIS;

        GF_ASSERT (value);
        GF_ASSERT (dict);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        FMTSTR_CHECK_VOL_EXISTS, volname);
                goto out;
        }
        /* Todo : check if global option is enabled, proceed only then */

        /* Create the export file only when ganesha.enable "on" is executed */
         if (strcmp (value, "on") == 0) {
                ret  =  create_export_config (volname, op_errstr);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Failed to create"
                                "export file for NFS-Ganesha\n");
                        goto out;
                }
        }

        if (check_host_list()) {
                runner_add_args (&runner, "sh", GANESHA_PREFIX"/dbus-send.sh",
                         CONFDIR, value, volname, NULL);
                ret = runner_run (&runner);
                if (ret)
                        gf_asprintf(op_errstr, "Dynamic export"
                                    " addition/deletion failed."
                                    "Please see log file for details");
        }
out:
        return ret;
}

int
tear_down_cluster(void)
{
        int     ret     =       0;
        runner_t runner =       {0,};

        if (is_ganesha_host()) {
                runinit (&runner);
                runner_add_args (&runner, "sh", CONFDIR,
                                GANESHA_PREFIX"/ganesha-ha.sh", "teardown",
                                CONFDIR, NULL);
                ret = runner_run(&runner);
        }
        return ret;
}


int
setup_cluster(void)
{
        int ret         = 0;
        runner_t runner = {0,};

        if (is_ganesha_host()) {
                runinit (&runner);
                runner_add_args (&runner, "sh", GANESHA_PREFIX"/ganesha-ha.sh",
                                 "setup", CONFDIR,  NULL);
                ret =  runner_run (&runner);
        }
        return ret;
}


int
stop_ganesha (char **op_errstr)
{
        runner_t                runner                     = {0,};
        int                     ret                        = 1;

        ret = tear_down_cluster();
        if (ret == -1) {
                gf_asprintf (op_errstr, "Cleanup of NFS-Ganesha"
                             "HA config failed.");
                goto out;
        }

        if (check_host_list ()) {
                runinit (&runner);
                runner_add_args (&runner, "service", " nfs-ganesha", "stop", NULL);
                ret =  runner_run (&runner);
        }
out:
        return ret;
}

int
start_ganesha (char **op_errstr)
{

        runner_t                runner                     = {0,};
        int                     ret                        = -1;
        char                    key[1024]                  = {0,};
        char                    *hostname                  = NULL;
        dict_t *vol_opts                                   = NULL;
        glusterd_volinfo_t *volinfo                        = NULL;
        int count                                          = 0;
        char *volname                                      = NULL;
        glusterd_conf_t *priv                              = NULL;

        priv =  THIS->private;
        GF_ASSERT (priv);

        cds_list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                vol_opts = volinfo->dict;
                /* Gluster-nfs has to be disabled across the trusted pool */
                /* before attempting to start nfs-ganesha */
                ret = dict_set_str (vol_opts, "nfs.disable", "on");
                if (ret)
                        goto out;
        }

        ret = priv->nfs_svc.stop (&(priv->nfs_svc), SIGKILL);
        if (ret) {
                ret = -1;
                gf_asprintf (op_errstr, "Gluster-NFS service could"
                             "not be stopped, exiting.");
                goto out;
        }

        if (check_host_list()) {
                runinit(&runner);
                runner_add_args (&runner, "service",
                                 "nfs-ganesha", "start", NULL);

                ret =  runner_run (&runner);
                if (ret) {
                        gf_asprintf (op_errstr, "NFS-Ganesha failed to start."
                        "Please see log file for details");
                        goto out;
                }

                ret = setup_cluster();
                if (ret == -1) {
                        gf_asprintf (op_errstr, "Failed to set up HA "
                                     "config for NFS-Ganesha."
                                     "Please check the log file for details");
                        goto out;
                }
        }

out:
        return ret;
}

int
glusterd_handle_ganesha_op (dict_t *dict, char **op_errstr,
                            char *key, char *value)
{

        int32_t                 ret          = -1;
        char                   *volname      = NULL;
        xlator_t               *this         = NULL;
        static int             export_id     = 1;
        glusterd_volinfo_t     *volinfo      = NULL;
        char *option                         = NULL;

        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);
        GF_ASSERT (key);
        GF_ASSERT (value);

        /* TODO: enable only if global option is set */
        /* BUG ID : 1200265 */

        if (strcmp (key, "ganesha.enable") == 0) {
                ret =  ganesha_manage_export(dict, value, op_errstr);
                if (ret < 0)
                        goto out;
        }

        if (strcmp (key, "features.ganesha") == 0) {
                if (strcmp (value, "enable") == 0) {
                        ret = start_ganesha(op_errstr);
                        if (ret < 0)
                                goto out;
                }

        else if (strcmp (value, "disable") == 0) {
                ret = stop_ganesha (op_errstr);
                if (ret < 0)
                        goto out;
                }
        }

out:
        return ret;
}

