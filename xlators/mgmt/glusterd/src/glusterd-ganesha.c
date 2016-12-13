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

/*
 * Check if the cluster is a ganesha cluster or not *
 */
gf_boolean_t
glusterd_is_ganesha_cluster () {
        int                ret      = -1;
        glusterd_conf_t   *priv     = NULL;
        xlator_t          *this     = NULL;
        gf_boolean_t       ret_bool = _gf_false;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("ganesha", this, out);
        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        ret = dict_get_str_boolean (priv->opts,
                                    GLUSTERD_STORE_KEY_GANESHA_GLOBAL,
                                    _gf_false);
        if (ret == _gf_true) {
                ret_bool = _gf_true;
                gf_msg_debug (this->name, 0,
                              "nfs-ganesha is enabled for the cluster");
        } else
                gf_msg_debug (this->name, 0,
                              "nfs-ganesha is disabled for the cluster");

out:
        return ret_bool;

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

/* *
 * The below function is called as part of commit phase for volume set option
 * "ganesha.enable". If the value is "on", it creates export configuration file
 * and then export the volume via dbus command. Incase of "off", the volume
 * will be already unexported during stage phase, so it will remove the conf
 * file from shared storage
 */
int
glusterd_check_ganesha_cmd (char *key, char *value, char **errstr, dict_t *dict)
{
        int                ret = 0;
        char               *volname = NULL;

        GF_ASSERT (key);
        GF_ASSERT (value);
        GF_ASSERT (dict);

        if ((strcmp (key, "ganesha.enable") == 0)) {
                if ((strcmp (value, "on")) && (strcmp (value, "off"))) {
                        gf_asprintf (errstr, "Invalid value"
                                " for volume set command. Use on/off only.");
                        ret = -1;
                        goto out;
                }
                if (strcmp (value, "on") == 0) {
                        ret = glusterd_handle_ganesha_op (dict, errstr, key,
                                                        value);

                 } else if (is_origin_glusterd (dict)) {
                        ret = dict_get_str (dict, "volname", &volname);
                        if (ret) {
                                gf_msg ("glusterd-ganesha", GF_LOG_ERROR, errno,
                                GD_MSG_DICT_GET_FAILED,
                                "Unable to get volume name");
                                goto out;
                        }
                        ret = manage_export_config (volname, "off", errstr);
                 }
        }
out:
        if (ret) {
                gf_msg ("glusterd-ganesha", GF_LOG_ERROR, 0,
                        GD_MSG_NFS_GNS_OP_HANDLE_FAIL,
                        "Handling NFS-Ganesha"
                        " op failed.");
        }
        return ret;
}

int
glusterd_op_stage_set_ganesha (dict_t *dict, char **op_errstr)
{
        int                             ret                     = -1;
        int                             value                   = -1;
        gf_boolean_t                    option                  = _gf_false;
        char                            *str                    = NULL;
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
        } else {
                ret =  stop_ganesha (op_errstr);
                if (ret)
                        gf_msg_debug (THIS->name, 0, "Could not stop "
                                                "NFS-Ganesha.");
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

/* Following function parse GANESHA_HA_CONF
 * The sample file looks like below,
 * HA_NAME="ganesha-ha-360"
 * HA_VOL_NAME="ha-state"
 * HA_CLUSTER_NODES="server1,server2"
 * VIP_rhs_1="10.x.x.x"
 * VIP_rhs_2="10.x.x.x." */

/* Check if the localhost is listed as one of nfs-ganesha nodes */
gf_boolean_t
check_host_list (void)
{

        glusterd_conf_t     *priv        = NULL;
        char    *hostname, *hostlist;
        gf_boolean_t    ret              = _gf_false;
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
manage_export_config (char *volname, char *value, char **op_errstr)
{
        runner_t                runner                     = {0,};
        int                     ret                        = -1;

        GF_ASSERT(volname);
        runinit (&runner);
        runner_add_args (&runner, "sh",
                        GANESHA_PREFIX"/create-export-ganesha.sh",
                        CONFDIR, value, volname, NULL);
        ret = runner_run(&runner);

        if (ret)
                gf_asprintf (op_errstr, "Failed to create"
                            " NFS-Ganesha export config file.");

        return ret;
}

/* Exports and unexports a particular volume via NFS-Ganesha */
int
ganesha_manage_export (dict_t *dict, char *value, char **op_errstr)
{
        runner_t                 runner = {0,};
        int                      ret = -1;
        glusterd_volinfo_t      *volinfo = NULL;
        dict_t                  *vol_opts = NULL;
        char                    *volname = NULL;
        xlator_t                *this    = NULL;
        glusterd_conf_t         *priv    = NULL;
        gf_boolean_t             option  = _gf_false;

        runinit (&runner);
        this =  THIS;
        GF_ASSERT (this);
        priv = this->private;

        GF_ASSERT (value);
        GF_ASSERT (dict);
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_GET_FAILED,
                        "Unable to get volume name");
                goto out;
        }
        ret = gf_string2boolean (value, &option);
        if (ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INVALID_ENTRY, "invalid value.");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_VOL_NOT_FOUND,
                        FMTSTR_CHECK_VOL_EXISTS, volname);
                goto out;
        }

        ret = glusterd_check_ganesha_export (volinfo);
        if (ret && option) {
                gf_asprintf (op_errstr, "ganesha.enable "
                             "is already 'on'.");
                ret = -1;
                goto out;

        }  else if (!option && !ret) {
                gf_asprintf (op_errstr, "ganesha.enable "
                                    "is already 'off'.");
                ret = -1;
                goto out;
        }

        /* Check if global option is enabled, proceed only then */
        ret = dict_get_str_boolean (priv->opts,
                            GLUSTERD_STORE_KEY_GANESHA_GLOBAL, _gf_false);
        if (ret == -1) {
                gf_msg_debug (this->name, 0, "Failed to get "
                        "global option dict.");
                gf_asprintf (op_errstr, "The option "
                             "nfs-ganesha should be "
                             "enabled before setting ganesha.enable.");
                goto out;
        }
        if (!ret) {
                gf_asprintf (op_errstr, "The option "
                             "nfs-ganesha should be "
                             "enabled before setting ganesha.enable.");
                ret = -1;
                goto out;
        }

        /* *
         * Create the export file from the node where ganesha.enable "on"
         * is executed
         * */
         if (option) {
                ret  = manage_export_config (volname, "on", op_errstr);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_EXPORT_FILE_CREATE_FAIL,
                                "Failed to create"
                                "export file for NFS-Ganesha\n");
                        goto out;
                }
        }

        if (check_host_list()) {
                runner_add_args (&runner, "sh", GANESHA_PREFIX"/dbus-send.sh",
                         CONFDIR, value, volname, NULL);
                ret = runner_run (&runner);
                if (ret) {
                        gf_asprintf(op_errstr, "Dynamic export"
                                    " addition/deletion failed."
                                    " Please see log file for details");
                        goto out;
                }
        }

        vol_opts = volinfo->dict;
        ret = dict_set_dynstr_with_alloc (vol_opts,
                                 "features.cache-invalidation", value);
        if (ret)
                gf_asprintf (op_errstr, "Cache-invalidation could not"
                                        " be set to %s.", value);
        ret = glusterd_store_volinfo (volinfo,
                        GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                gf_asprintf (op_errstr, "failed to store volinfo for %s"
                             , volinfo->volname);

out:
        return ret;
}

int
tear_down_cluster(gf_boolean_t run_teardown)
{
        int     ret                     = 0;
        runner_t runner                 = {0,};
        struct stat     st              = {0,};
        DIR            *dir             = NULL;
        struct dirent  *entry           = NULL;
        struct dirent   scratch[2]      = {{0,},};
        char            path[PATH_MAX]  = {0,};

        if (run_teardown) {
                runinit (&runner);
                runner_add_args (&runner, "sh",
                                GANESHA_PREFIX"/ganesha-ha.sh", "teardown",
                                CONFDIR, NULL);
                ret = runner_run(&runner);
                /* *
                 * Remove all the entries in CONFDIR expect ganesha.conf and
                 * ganesha-ha.conf
                 */
                dir = sys_opendir (CONFDIR);
                if (!dir) {
                        gf_msg_debug (THIS->name, 0, "Failed to open directory %s. "
                                      "Reason : %s", CONFDIR, strerror (errno));
                        ret = 0;
                        goto out;
                }

                GF_FOR_EACH_ENTRY_IN_DIR (entry, dir, scratch);
                while (entry) {
                        snprintf (path, PATH_MAX, "%s/%s", CONFDIR, entry->d_name);
                        ret = sys_lstat (path, &st);
                        if (ret == -1) {
                                gf_msg_debug (THIS->name, 0, "Failed to stat entry %s :"
                                              " %s", path, strerror (errno));
                                goto out;
                        }

                        if (strcmp(entry->d_name, "ganesha.conf") == 0 ||
                            strcmp(entry->d_name, "ganesha-ha.conf") == 0)
                                gf_msg_debug (THIS->name, 0, " %s is not required"
                                                " to remove", path);
                        else if (S_ISDIR (st.st_mode))
                                ret = recursive_rmdir (path);
                        else
                                ret = sys_unlink (path);

                        if (ret) {
                                gf_msg_debug (THIS->name, 0, " Failed to remove %s. "
                                      "Reason : %s", path, strerror (errno));
                        }

                        gf_msg_debug (THIS->name, 0, "%s %s", ret ?
                                      "Failed to remove" : "Removed", entry->d_name);
                        GF_FOR_EACH_ENTRY_IN_DIR (entry, dir, scratch);
                }

                ret = sys_closedir (dir);
                if (ret) {
                        gf_msg_debug (THIS->name, 0, "Failed to close dir %s. Reason :"
                                      " %s", CONFDIR, strerror (errno));
                }
        }

out:
        return ret;
}


int
setup_cluster(gf_boolean_t run_setup)
{
        int ret         = 0;
        runner_t runner = {0,};

        if (run_setup) {
                runinit (&runner);
                runner_add_args (&runner, "sh", GANESHA_PREFIX"/ganesha-ha.sh",
                                 "setup", CONFDIR,  NULL);
                ret =  runner_run (&runner);
        }
        return ret;
}


static int
teardown (gf_boolean_t run_teardown, char **op_errstr)
{
        runner_t                runner                     = {0,};
        int                     ret                        = 1;
        glusterd_volinfo_t      *volinfo                   = NULL;
        glusterd_conf_t         *priv                      = NULL;
        dict_t                  *vol_opts                  = NULL;

        priv = THIS->private;

        ret = tear_down_cluster (run_teardown);
        if (ret == -1) {
                gf_asprintf (op_errstr, "Cleanup of NFS-Ganesha"
                             " HA config failed.");
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

        int ret                 = 0;
        runner_t runner         = {0,};

        runinit (&runner);
        runner_add_args (&runner, "sh", GANESHA_PREFIX"/ganesha-ha.sh",
                         "--setup-ganesha-conf-files", CONFDIR, "no", NULL);
        ret =  runner_run (&runner);
        if (ret) {
                gf_asprintf (op_errstr, "removal of symlink ganesha.conf "
                             "in /etc/ganesha failed");
        }

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
        dict_t *vol_opts                                   = NULL;
        glusterd_volinfo_t *volinfo                        = NULL;
        glusterd_conf_t *priv                              = NULL;
        runner_t runner                                    = {0,};

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

        runinit (&runner);
        runner_add_args (&runner, "sh", GANESHA_PREFIX"/ganesha-ha.sh",
                         "--setup-ganesha-conf-files", CONFDIR, "yes", NULL);
        ret =  runner_run (&runner);
        if (ret) {
                gf_asprintf (op_errstr, "creation of symlink ganesha.conf "
                             "in /etc/ganesha failed");
                goto out;
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
pre_setup (gf_boolean_t run_setup, char **op_errstr)
{
        int    ret = 0;

        ret = check_host_list();

        if (ret) {
                ret = setup_cluster(run_setup);
                if (ret == -1)
                        gf_asprintf (op_errstr, "Failed to set up HA "
                                     "config for NFS-Ganesha. "
                                     "Please check the log file for details");
        }

        return ret;
}

int
glusterd_handle_ganesha_op (dict_t *dict, char **op_errstr,
                            char *key, char *value)
{

        int32_t                 ret          = -1;
        gf_boolean_t           option        = _gf_false;

        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);
        GF_ASSERT (key);
        GF_ASSERT (value);


        if (strcmp (key, "ganesha.enable") == 0) {
                ret =  ganesha_manage_export (dict, value, op_errstr);
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
                /* *
                 * The set up/teardown of pcs cluster should be performed only
                 * once. This will done on the node in which the cli command
                 * 'gluster nfs-ganesha <enable/disable>' got executed. So that
                 * node should part of ganesha HA cluster
                 */
                if (option) {
                        ret = pre_setup (is_origin_glusterd (dict), op_errstr);
                        if (ret < 0)
                                goto out;
                } else {
                        ret = teardown (is_origin_glusterd (dict), op_errstr);
                        if (ret < 0)
                                goto out;
                }
        }

out:
        return ret;
}

