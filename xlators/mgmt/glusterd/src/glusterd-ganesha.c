/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/



#include "common-utils.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-nfs-svc.h"
#include "glusterd-volgen.h"
#include "glusterd-messages.h"
#include "syscall.h"

#include <ctype.h>

#define SHARED_STORAGE_MNT "/var/run/gluster/shared_storage/nfs-ganesha"

int start_ganesha (char **op_errstr);


typedef struct service_command {
        char *binary;
        char *service;
        int (*action) (struct service_command *, char *);
} service_command;

/* parsing_ganesha_ha_conf will allocate the returned string
 * to be freed (GF_FREE) by the caller
 * return NULL if error or not found */
static char*
parsing_ganesha_ha_conf(const char *key) {
#define MAX_LINE 1024
        char scratch[MAX_LINE * 2] = {0,};
        char *value = NULL, *pointer = NULL, *end_pointer = NULL;
        FILE *fp;
        struct stat st = {0,};

        fp = fopen (GANESHA_HA_CONF, "r");
        if (fp == NULL) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_FILE_OP_FAILED, "couldn't open the file %s",
                        GANESHA_HA_CONF);
                goto end_ret;
        }
        while ((pointer = fgets (scratch, MAX_LINE, fp)) != NULL) {
                /* Read config file until we get matching "^[[:space:]]*key" */
                if (*pointer == '#') {
                        continue;
                }
                while (isblank(*pointer)) {
                        pointer++;
                }
                if (strncmp (pointer, key, strlen (key))) {
                        continue;
                }
                pointer += strlen (key);
                /* key found : if we fail to parse, we'll return an error
                 * rather than trying next one
                 * - supposition : conf file is bash compatible : no space
                 *   around the '=' */
                if (*pointer != '=') {
                        gf_msg (THIS->name, GF_LOG_ERROR, errno,
                                GD_MSG_GET_CONFIG_INFO_FAILED,
                                "Parsing %s failed at key %s",
                                GANESHA_HA_CONF, key);
                        goto end_close;
                }
                pointer++;  /* jump the '=' */

                if (*pointer == '"' || *pointer == '\'') {
                        /* dont get the quote */
                        pointer++;
                }
                end_pointer = pointer;
                /* stop at the next closing quote or  blank/newline */
                do {
                        end_pointer++;
                } while (!(*end_pointer == '\'' || *end_pointer == '"' ||
                                isspace(*end_pointer) || *end_pointer == '\0'));
                *end_pointer = '\0';

                /* got it. copy it and return */
                value = gf_strdup (pointer);
                break;
        }

end_close:
        fclose(fp);
end_ret:
        return value;
}

static int
sc_systemctl_action (struct service_command *sc, char *command)
{
        runner_t        runner   = {0,};

        runinit (&runner);
        runner_add_args (&runner, sc->binary, command, sc->service, NULL);
        return runner_run (&runner);
}

static int
sc_service_action (struct service_command *sc, char *command)
{
        runner_t      runner = {0,};

        runinit (&runner);
        runner_add_args (&runner, sc->binary, sc->service, command, NULL);
        return runner_run (&runner);
}

static int
manage_service (char *action)
{
        struct stat stbuf       = {0,};
        int     i               = 0;
        int     ret             = 0;
        struct service_command sc_list[] = {
                { .binary  = "/usr/bin/systemctl",
                  .service = "nfs-ganesha",
                  .action  = sc_systemctl_action
                },
                { .binary  = "/sbin/invoke-rc.d",
                  .service = "nfs-ganesha",
                  .action  = sc_service_action
                },
                { .binary  = "/sbin/service",
                  .service = "nfs-ganesha",
                  .action  = sc_service_action
                },
                { .binary  = NULL
                }
        };

        while (sc_list[i].binary != NULL) {
                ret = sys_stat (sc_list[i].binary, &stbuf);
                if (ret == 0) {
                        gf_msg_debug (THIS->name, 0,
                                "%s found.", sc_list[i].binary);
                        if (strcmp (sc_list[i].binary, "/usr/bin/systemctl") == 0)
                                ret = sc_systemctl_action (&sc_list[i], action);
                        else
                                ret = sc_service_action (&sc_list[i], action);

                        return ret;
                }
                i++;
        }
        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                GD_MSG_UNRECOGNIZED_SVC_MNGR,
                "Could not %s NFS-Ganesha.Service manager for distro"
                " not recognized.", action);
        return ret;
}
/* Check if ganesha.enable is set to 'on', that checks if
 * a  particular volume is exported via NFS-Ganesha */
gf_boolean_t
glusterd_check_ganesha_export (glusterd_volinfo_t *volinfo) {

        char *value              = NULL;
        gf_boolean_t is_exported = _gf_false;
        int ret                 = 0;

        ret = glusterd_volinfo_get (volinfo, "ganesha.enable", &value);
        if ((ret == 0) && value) {
                if (strcmp (value, "on") == 0) {
                        gf_msg_debug (THIS->name, 0, "ganesha.enable set"
                                " to %s", value);
                        is_exported = _gf_true;
                }
        }
        return is_exported;
}


int
glusterd_check_ganesha_cmd (char *key, char *value, char **errstr, dict_t *dict)
{
        int                ret = 0;
        xlator_t                *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (key);
        GF_ASSERT (value);

        if ((strcmp (key, "ganesha.enable") == 0)) {
                if ((strcmp (value, "on")) && (strcmp (value, "off"))) {
                        gf_asprintf (errstr, "Invalid value"
                                " for volume set command. Use on/off only.");
                        ret = -1;
                        goto out;
                }
                ret = glusterd_handle_ganesha_op (dict, errstr, key, value);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_NFS_GNS_OP_HANDLE_FAIL,
                                "Handling NFS-Ganesha"
                                " op failed.");
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
        int                             value                   = -1;
        gf_boolean_t                    option                  = _gf_false;
        char                            *str                    = NULL;
        int                             dict_count              = 0;
        int                             flags                   = 0;
        glusterd_volinfo_t              *volinfo                = NULL;
        glusterd_conf_t                 *priv                   = NULL;
        xlator_t                        *this                   = NULL;

        GF_ASSERT (dict);
        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        value = dict_get_str_boolean (dict, "value", _gf_false);
        if (value == -1) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED,
                        "value not present.");
                goto out;
        }
        /* This dict_get will fail if the user had never set the key before */
        /*Ignoring the ret value and proceeding */
        ret = dict_get_str (priv->opts, GLUSTERD_STORE_KEY_GANESHA_GLOBAL, &str);
        if (ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        GD_MSG_DICT_GET_FAILED, "Global dict not present.");
                ret = 0;
                goto out;
        }
        /* Validity of the value is already checked */
        ret = gf_string2boolean (str, &option);
        /* Check if the feature is already enabled, fail in that case */
        if (value == option) {
                gf_asprintf (op_errstr, "nfs-ganesha is already %sd.", str);
                ret = -1;
                goto out;
        }

        if (value) {
                ret =  start_ganesha (op_errstr);
                if (ret) {
                        gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                GD_MSG_NFS_GNS_START_FAIL,
                                "Could not start NFS-Ganesha");

                }
        }

out:

        if (ret) {
                if (!(*op_errstr)) {
                        *op_errstr = gf_strdup ("Error, Validation Failed");
                        gf_msg_debug (this->name, 0,
                                "Error, Cannot Validate option :%s",
                                GLUSTERD_STORE_KEY_GANESHA_GLOBAL);
                } else {
                        gf_msg_debug (this->name, 0,
                                "Error, Cannot Validate option");
                }
        }
        return ret;
}

int
glusterd_op_set_ganesha (dict_t *dict, char **errstr)
{
        int                                      ret = 0;
        xlator_t                                *this = NULL;
        glusterd_conf_t                         *priv = NULL;
        char                                    *key = NULL;
        char                                    *value = NULL;
        dict_t                                  *vol_opts = NULL;
        char                                    *next_version =  NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (dict);

        priv = this->private;
        GF_ASSERT (priv);


        ret = dict_get_str (dict, "key", &key);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED,
                        "Couldn't get key in global option set");
                goto out;
       }

        ret = dict_get_str (dict, "value", &value);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED,
                        "Couldn't get value in global option set");
                goto out;
        }

        ret = glusterd_handle_ganesha_op (dict, errstr, key, value);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_NFS_GNS_SETUP_FAIL,
                        "Initial NFS-Ganesha set up failed");
                ret = -1;
                goto out;
        }
        ret = dict_set_dynstr_with_alloc (priv->opts,
                                   GLUSTERD_STORE_KEY_GANESHA_GLOBAL,
                                   value);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, errno,
                        GD_MSG_DICT_SET_FAILED, "Failed to set"
                        " nfs-ganesha in dict.");
                goto out;
        }
        ret = glusterd_get_next_global_opt_version_str (priv->opts,
                                                        &next_version);
        if (ret) {
                gf_msg_debug (THIS->name, 0, "Could not fetch "
                        " global op version");
                goto out;
        }
        ret = dict_set_str (priv->opts, GLUSTERD_GLOBAL_OPT_VERSION,
                            next_version);
        if (ret)
                goto out;

        ret = glusterd_store_options (this, priv->opts);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STORE_FAIL, "Failed to store options");
                goto out;
        }

out:
       gf_msg_debug (this->name, 0, "returning %d", ret);
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
        gf_boolean_t    ret              = _gf_false;
        xlator_t        *this            = NULL;

        this = THIS;

        host_from_file = parsing_ganesha_ha_conf ("HA_VOL_SERVER");
        if (host_from_file == NULL) {
                gf_msg (this->name, GF_LOG_INFO, errno,
                        GD_MSG_GET_CONFIG_INFO_FAILED,
                        "couldn't get HA_VOL_SERVER from file %s",
                        GANESHA_HA_CONF);
                return _gf_false;
        }

        ret = gf_is_local_addr (host_from_file);
        if (ret) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        GD_MSG_NFS_GNS_HOST_FOUND,
                        "ganesha host found "
                        "Hostname is %s", host_from_file);
        }

        GF_FREE (host_from_file);
        return ret;
}

/* Check if the localhost is listed as one of nfs-ganesha nodes */
gf_boolean_t
check_host_list (void)
{

        glusterd_conf_t     *priv        = NULL;
        char    *hostname, *hostlist;
        int     ret                      = _gf_false;
        xlator_t        *this            = NULL;

        this = THIS;
        priv =  THIS->private;
        GF_ASSERT (priv);

        hostlist = parsing_ganesha_ha_conf ("HA_CLUSTER_NODES");
        if (hostlist == NULL) {
                gf_msg (this->name, GF_LOG_INFO, errno,
                        GD_MSG_GET_CONFIG_INFO_FAILED,
                        "couldn't get HA_CLUSTER_NODES from file %s",
                        GANESHA_HA_CONF);
                return _gf_false;
        }

        /* Hostlist is a comma separated list now */
        hostname = strtok (hostlist, ",");
        while (hostname != NULL) {
                ret = gf_is_local_addr (hostname);
                if (ret) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                GD_MSG_NFS_GNS_HOST_FOUND,
                                "ganesha host found "
                                "Hostname is %s", hostname);
                        break;
                }
                hostname = strtok (NULL, ",");
        }

        GF_FREE (hostlist);
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

        if (ret && op_errstr)
                gf_asprintf (op_errstr, "Failed to create"
                            " NFS-Ganesha export config file.");

        return ret;
}

int
copy_export_config (char *volname, char **op_errstr)
{
        runner_t                runner                     = {0,};
        int                     ret                        = -1;

        GF_ASSERT(volname);
        runinit (&runner);
        runner_add_args (&runner, "sh",
                        GANESHA_PREFIX"/copy-export-ganesha.sh",
                        CONFDIR, volname, NULL);
        ret = runner_run(&runner);

        if (ret && op_errstr)
                gf_asprintf (op_errstr, "Failed to copy"
                            " NFS-Ganesha export config file.");

        return ret;
}
/* Exports and unexports a particular volume via NFS-Ganesha */
int
ganesha_manage_export (char *volname, char *value, char **op_errstr,
                       gf_boolean_t reboot)
{
        runner_t                 runner = {0,};
        int                      ret = -1;
        char                     str[1024];
        glusterd_volinfo_t      *volinfo = NULL;
        dict_t                  *vol_opts = NULL;
        xlator_t                *this    = NULL;
        glusterd_conf_t         *priv    = NULL;
        gf_boolean_t             option  = _gf_false;
        int                     i = 1;

        runinit (&runner);
        this =  THIS;
        GF_ASSERT (this);
        priv = this->private;

        GF_ASSERT (value);
        GF_ASSERT (priv);
        GF_VALIDATE_OR_GOTO (this->name, volname, out);


        ret = gf_string2boolean (value, &option);
        if (ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INVALID_ENTRY, "invalid value.");
                goto out;
        }

        /* *
         * Incase of reboot, following checks are already made before calling
         * ganesha_manage_export. So it will be reductant do it again
         */
        if (!reboot) {
                ret = glusterd_volinfo_find (volname, &volinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_VOL_NOT_FOUND,
                                FMTSTR_CHECK_VOL_EXISTS, volname);
                        goto out;
                }

                ret = glusterd_check_ganesha_export (volinfo);
                if (ret && option) {
                        if (op_errstr)
                                gf_asprintf (op_errstr, "ganesha.enable "
                                                     "is already 'on'.");
                        ret = -1;
                        goto out;

                }  else if (!option && !ret) {
                        if (op_errstr)
                                gf_asprintf (op_errstr, "ganesha.enable "
                                                    "is already 'off'.");
                        ret = -1;
                        goto out;
                }
        }

        ret = 0;

        /* *
         * Incase of restart, there is chance that global option turned off
         * with volume set command. Still we may need to clean up the
         * configuration files.
         * Otherwise check if global option is enabled, only then proceed
         * */
        if (!(reboot && !option)) {
                ret = dict_get_str_boolean (priv->opts,
                            GLUSTERD_STORE_KEY_GANESHA_GLOBAL, _gf_false);
                if (ret == -1) {
                        gf_msg_debug (this->name, 0, "Failed to get "
                                                "global option dict.");
                        if (op_errstr)
                                gf_asprintf (op_errstr, "The option "
                                             "nfs-ganesha should be "
                                             "enabled before setting "
                                             "ganesha.enable.");
                        goto out;
                }
                if (!ret) {
                        if (op_errstr)
                                gf_asprintf (op_errstr, "The option "
                                             "nfs-ganesha should be "
                                             "enabled before setting "
                                             "ganesha.enable.");
                        ret = -1;
                        goto out;
                }
        }
        /* Create the export file only when ganesha.enable "on" is executed */
         if (option) {
                if (reboot)
                       ret  =  copy_export_config (volname, op_errstr);
                else
                       ret  =  create_export_config (volname, op_errstr);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_EXPORT_FILE_CREATE_FAIL,
                                "Failed to create/copy "
                                "export file for NFS-Ganesha\n");
                        goto out;
                }
        }

        if (check_host_list()) {
                runner_add_args (&runner, "sh", GANESHA_PREFIX"/dbus-send.sh",
                         CONFDIR, value, volname, NULL);
                ret = runner_run (&runner);
                if (ret) {
                        if (op_errstr)
                                gf_asprintf(op_errstr, "Dynamic export"
                                            " addition/deletion failed."
                                            " Please see log file for details");
                        /* *
                         * Incase of reboot scenarios, we cannot guarantee
                         * nfs-ganesha to be running on that node, so that
                         * dynamic export may fail
                         */
                        if (reboot)
                                ret = 0;
                        else
                                goto out;
                }
        }


        /* *
         * cache-invalidation should be on when a volume is exported
         * and off when a volume is unexported. It is not required
         * for reboot scenarios, already it will be copied.
         * */
        if (!reboot) {
                vol_opts = volinfo->dict;
                ret = dict_set_dynstr_with_alloc (vol_opts,
                                         "features.cache-invalidation", value);
                if (ret && op_errstr)
                        gf_asprintf (op_errstr, "Cache-invalidation could not"
                                                " be set to %s.", value);
                ret = glusterd_store_volinfo (volinfo,
                                GLUSTERD_VOLINFO_VER_AC_INCREMENT);
                if (ret && op_errstr)
                        gf_asprintf (op_errstr, "failed to store volinfo for %s"
                                     , volinfo->volname);

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
                runner_add_args (&runner, "sh",
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


static int
teardown (char **op_errstr)
{
        runner_t                runner                     = {0,};
        int                     ret                        = 1;
        glusterd_volinfo_t      *volinfo                   = NULL;
        glusterd_conf_t         *priv                      = NULL;
        dict_t                  *vol_opts                  = NULL;

        priv = THIS->private;

        ret = tear_down_cluster();
        if (ret == -1) {
                gf_asprintf (op_errstr, "Cleanup of NFS-Ganesha"
                             " HA config failed.");
                goto out;
        }
        ret =  stop_ganesha (op_errstr);
        if (ret) {
                gf_asprintf (op_errstr, "Could not stop NFS-Ganesha.");
                goto out;
        }

        runinit (&runner);
        runner_add_args (&runner, "sh", GANESHA_PREFIX"/ganesha-ha.sh",
                                 "cleanup", CONFDIR,  NULL);
        ret = runner_run (&runner);
        if (ret)
                gf_msg_debug (THIS->name, 0, "Could not clean up"
                        " NFS-Ganesha related config");

        cds_list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                vol_opts = volinfo->dict;
                /* All the volumes exported via NFS-Ganesha will be
                unexported, hence setting the appropriate keys */
                ret = dict_set_str (vol_opts, "features.cache-invalidation",
                                    "off");
                if (ret)
                        gf_msg (THIS->name, GF_LOG_WARNING, errno,
                                GD_MSG_DICT_SET_FAILED,
                                "Could not set features.cache-invalidation "
                                "to off for %s", volinfo->volname);

                ret = dict_set_str (vol_opts, "ganesha.enable", "off");
                if (ret)
                        gf_msg (THIS->name, GF_LOG_WARNING, errno,
                                GD_MSG_DICT_SET_FAILED,
                                "Could not set ganesha.enable to off for %s",
                                volinfo->volname);

                ret = glusterd_store_volinfo (volinfo,
                                GLUSTERD_VOLINFO_VER_AC_INCREMENT);
                if (ret)
                        gf_msg (THIS->name, GF_LOG_WARNING, 0,
                                GD_MSG_VOLINFO_SET_FAIL,
                                "failed to store volinfo for %s",
                                volinfo->volname);
        }
out:
        return ret;
}

int
stop_ganesha (char **op_errstr) {

        int ret = 0;

        if (check_host_list ()) {
                ret = manage_service ("stop");
                if (ret)
                        gf_asprintf (op_errstr, "NFS-Ganesha service could not"
                                     "be stopped.");
        }
        return ret;

}

int
start_ganesha (char **op_errstr)
{
        int                     ret                        = -1;
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
                ret = dict_set_str (vol_opts, NFS_DISABLE_MAP_KEY, "on");
                if (ret)
                        goto out;

                ret = glusterd_store_volinfo (volinfo,
                                GLUSTERD_VOLINFO_VER_AC_INCREMENT);
                if (ret) {
                        *op_errstr = gf_strdup ("Failed to store the "
                                                "Volume information");
                        goto out;
                }
        }

        /* If the nfs svc is not initialized it means that the service is not
         * running, hence we can skip the process of stopping gluster-nfs
         * service
         */
        if (priv->nfs_svc.inited) {
                ret = priv->nfs_svc.stop (&(priv->nfs_svc), SIGKILL);
                if (ret) {
                        ret = -1;
                        gf_asprintf (op_errstr, "Gluster-NFS service could"
                                     "not be stopped, exiting.");
                        goto out;
                }
        }
        if (check_host_list()) {
                ret = manage_service ("start");
                if (ret)
                        gf_asprintf (op_errstr, "NFS-Ganesha failed to start."
                        "Please see log file for details");
        }

out:
        return ret;
}

static int
pre_setup (char **op_errstr)
{
        int    ret = 0;

        ret = sys_mkdir (SHARED_STORAGE_MNT, 0775);

        if ((-1 == ret) && (EEXIST != errno)) {
                gf_msg ("THIS->name", GF_LOG_ERROR, errno,
                        GD_MSG_CREATE_DIR_FAILED, "mkdir() failed on path %s,",
                        SHARED_STORAGE_MNT);
                goto out;
        }

        ret = check_host_list();

        if (ret) {
                ret = setup_cluster();
                if (ret == -1)
                        gf_asprintf (op_errstr, "Failed to set up HA "
                                     "config for NFS-Ganesha. "
                                     "Please check the log file for details");
        }

out:
        return ret;
}

int
glusterd_handle_ganesha_op (dict_t *dict, char **op_errstr,
                            char *key, char *value)
{

        int32_t                ret           = -1;
        char                   *volname      = NULL;
        gf_boolean_t           option        = _gf_false;

        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);
        GF_ASSERT (key);
        GF_ASSERT (value);


        if (strcmp (key, "ganesha.enable") == 0) {
                ret = dict_get_str (dict, "volname", &volname);
                if (ret) {
                        gf_msg (THIS->name, GF_LOG_ERROR, errno,
                                GD_MSG_DICT_GET_FAILED,
                                "Unable to get volume name");
                        goto out;
                }
                ret =  ganesha_manage_export (volname, value, op_errstr,
                                              _gf_false);
                if (ret < 0)
                        goto out;
        }

        /* It is possible that the key might not be set */
        ret =  gf_string2boolean (value, &option);
        if (ret == -1) {
                gf_asprintf (op_errstr, "Invalid value in key-value pair.");
                goto out;
        }

        if (strcmp (key, GLUSTERD_STORE_KEY_GANESHA_GLOBAL) == 0) {
                if (option) {
                        ret = pre_setup (op_errstr);
                        if (ret < 0)
                                goto out;
                } else {
                        ret = teardown (op_errstr);
                        if (ret < 0)
                                goto out;
                }
        }

out:
        return ret;
}

