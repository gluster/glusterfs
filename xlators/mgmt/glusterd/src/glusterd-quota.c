/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "common-utils.h"
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-nfs-svc.h"
#include "glusterd-quotad-svc.h"
#include "glusterd-volgen.h"
#include "glusterd-messages.h"
#include "run.h"
#include "syscall.h"
#include "byte-order.h"
#include "compat-errno.h"
#include "quota-common-utils.h"
#include "glusterd-quota.h"

#include <sys/wait.h>
#include <dlfcn.h>

#ifndef _PATH_SETFATTR
# ifdef GF_LINUX_HOST_OS
#  define _PATH_SETFATTR "/usr/bin/setfattr"
# endif
# ifdef __NetBSD__
#  define _PATH_SETFATTR "/usr/pkg/bin/setfattr"
# endif
#endif

/* Any negative pid to make it special client */
#define QUOTA_CRAWL_PID "-100"

const char *gd_quota_op_list[GF_QUOTA_OPTION_TYPE_MAX + 1] = {
        [GF_QUOTA_OPTION_TYPE_NONE]               = "none",
        [GF_QUOTA_OPTION_TYPE_ENABLE]             = "enable",
        [GF_QUOTA_OPTION_TYPE_DISABLE]            = "disable",
        [GF_QUOTA_OPTION_TYPE_LIMIT_USAGE]        = "limit-usage",
        [GF_QUOTA_OPTION_TYPE_REMOVE]             = "remove",
        [GF_QUOTA_OPTION_TYPE_LIST]               = "list",
        [GF_QUOTA_OPTION_TYPE_VERSION]            = "version",
        [GF_QUOTA_OPTION_TYPE_ALERT_TIME]         = "alert-time",
        [GF_QUOTA_OPTION_TYPE_SOFT_TIMEOUT]       = "soft-timeout",
        [GF_QUOTA_OPTION_TYPE_HARD_TIMEOUT]       = "hard-timeout",
        [GF_QUOTA_OPTION_TYPE_DEFAULT_SOFT_LIMIT] = "default-soft-limit",
        [GF_QUOTA_OPTION_TYPE_LIMIT_OBJECTS]      = "limit-objects",
        [GF_QUOTA_OPTION_TYPE_LIST_OBJECTS]       = "list-objects",
        [GF_QUOTA_OPTION_TYPE_REMOVE_OBJECTS]     = "remove-objects",
        [GF_QUOTA_OPTION_TYPE_ENABLE_OBJECTS]     = "enable-objects",
        [GF_QUOTA_OPTION_TYPE_UPGRADE]            = "upgrade",
        [GF_QUOTA_OPTION_TYPE_MAX]                = NULL
};


gf_boolean_t
glusterd_is_quota_supported (int32_t type, char **op_errstr)
{
        xlator_t           *this        = NULL;
        glusterd_conf_t    *conf        = NULL;
        gf_boolean_t        supported   = _gf_false;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        if ((conf->op_version == GD_OP_VERSION_MIN) &&
            (type > GF_QUOTA_OPTION_TYPE_VERSION))
                goto out;

        if ((conf->op_version < GD_OP_VERSION_3_7_0) &&
            (type > GF_QUOTA_OPTION_TYPE_VERSION_OBJECTS))
                goto out;

        /* Quota Operations that change quota.conf shouldn't
         * be allowed as the quota.conf format changes in 3.7
         */
        if ((conf->op_version < GD_OP_VERSION_3_7_0) &&
            (type == GF_QUOTA_OPTION_TYPE_ENABLE ||
             type == GF_QUOTA_OPTION_TYPE_LIMIT_USAGE ||
             type == GF_QUOTA_OPTION_TYPE_REMOVE))
                goto out;

        /* Quota xattr version implemented in 3.7.6
         * quota-version is incremented when quota is enabled
         * Quota enable and disable performance enhancement has been done
         * in version 3.7.12.
         * so don't allow enabling/disabling quota in heterogeneous
         * cluster during upgrade
         */
        if (type == GF_QUOTA_OPTION_TYPE_ENABLE ||
            type == GF_QUOTA_OPTION_TYPE_ENABLE_OBJECTS ||
            type == GF_QUOTA_OPTION_TYPE_DISABLE) {
                if (conf->op_version < GD_OP_VERSION_3_7_12)
                        goto out;
        }

        supported = _gf_true;

out:
        if (!supported && op_errstr != NULL && conf)
                gf_asprintf (op_errstr, "Volume quota failed. The cluster is "
                             "operating at version %d. Quota command"
                             " %s is unavailable in this version.",
                             conf->op_version, gd_quota_op_list[type]);

        return supported;
}

int
__glusterd_handle_quota (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                         *dict = NULL;
        glusterd_op_t                   cli_op = GD_OP_QUOTA;
        char                           *volname = NULL;
        int32_t                         type = 0;
        char                            msg[2048] = {0,};
        xlator_t                       *this = NULL;
        glusterd_conf_t                *conf = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        ret = xdr_to_generic (req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
                //failed to decode msg;
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                /* Unserialize the dictionary */
                dict  = dict_new ();

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL, "failed to "
                                    "unserialize req-buffer to dictionary");
                        snprintf (msg, sizeof (msg), "Unable to decode the "
                                  "command");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }
        }

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                snprintf (msg, sizeof (msg), "Unable to get volume name");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume name, "
                        "while handling quota command");
                goto out;
        }

        ret = dict_get_int32 (dict, "type", &type);
        if (ret) {
                snprintf (msg, sizeof (msg), "Unable to get type of command");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get type of cmd, "
                        "while handling quota command");
                goto out;
        }

        if (!glusterd_is_quota_supported (type, NULL)) {
                snprintf (msg, sizeof (msg), "Volume quota failed. The cluster "
                          "is operating at version %d. Quota command"
                          " %s is unavailable in this version.",
                          conf->op_version, gd_quota_op_list[type]);
                ret = -1;
                goto out;
        }

        ret = glusterd_op_begin_synctask (req, GD_OP_QUOTA, dict);

out:
        if (ret) {
                if (msg[0] == '\0')
                        snprintf (msg, sizeof (msg), "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, msg);
        }

        return ret;
}

int
glusterd_handle_quota (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_quota);
}

int32_t
glusterd_check_if_quota_trans_enabled (glusterd_volinfo_t *volinfo)
{
        int32_t  ret           = 0;
        int      flag          = _gf_false;

        flag = glusterd_volinfo_get_boolean (volinfo, VKEY_FEATURES_QUOTA);
        if (flag == -1) {
                gf_msg ("glusterd", GF_LOG_ERROR, 0,
                        GD_MSG_QUOTA_GET_STAT_FAIL,
                        "failed to get the quota status");
                ret = -1;
                goto out;
        }

        if (flag == _gf_false) {
                ret = -1;
                goto out;
        }
        ret = 0;
out:
        return ret;
}

int32_t
_glusterd_quota_initiate_fs_crawl (glusterd_conf_t *priv,
                                  glusterd_volinfo_t *volinfo,
                                  glusterd_brickinfo_t *brick, int type,
                                  char *pid_dir)
{
        pid_t                      pid;
        int32_t                    ret                 = -1;
        int                        status              = 0;
        char                       mountdir[PATH_MAX]  = {0,};
        char                       logfile[PATH_MAX]   = {0,};
        char                       brickpath[PATH_MAX] = {0,};
        char                       vol_id[PATH_MAX]    = {0,};
        char                       pidfile[PATH_MAX]   = {0,};
        runner_t                   runner              = {0};
        char                      *volfileserver       = NULL;
        FILE                      *pidfp               = NULL;

        GF_VALIDATE_OR_GOTO ("glusterd", THIS, out);

        GLUSTERD_GET_TMP_PATH (mountdir, "/");
        ret = sys_mkdir (mountdir, 0777);
        if (ret && errno != EEXIST) {
                gf_msg (THIS->name, GF_LOG_WARNING, errno,
                        GD_MSG_MOUNT_REQ_FAIL, "failed to create temporary "
                        "directory %s", mountdir);
                ret = -1;
                goto out;
        }

        strcat (mountdir, "mntXXXXXX");
        if (mkdtemp (mountdir) == NULL) {
                gf_msg (THIS->name, GF_LOG_WARNING, errno,
                        GD_MSG_MOUNT_REQ_FAIL, "failed to create a temporary "
                        "mount directory: %s", mountdir);
                ret = -1;
                goto out;
        }

        GLUSTERD_REMOVE_SLASH_FROM_PATH (brick->path, brickpath);
        snprintf (logfile, sizeof (logfile),
                  DEFAULT_QUOTA_CRAWL_LOG_DIRECTORY"/%s.log",
                  brickpath);

        if (dict_get_str (THIS->options, "transport.socket.bind-address",
                          &volfileserver) != 0)
                volfileserver = "localhost";

        snprintf (vol_id, sizeof (vol_id), "client_per_brick/%s.%s.%s.%s.vol",
                  volinfo->volname, "client", brick->hostname, brickpath);

        runinit (&runner);

        if (type == GF_QUOTA_OPTION_TYPE_ENABLE ||
            type == GF_QUOTA_OPTION_TYPE_ENABLE_OBJECTS)
                runner_add_args (&runner, SBIN_DIR"/glusterfs",
                                 "-s", volfileserver,
                                 "--volfile-id", vol_id,
                                 "--use-readdirp=yes",
                                 "--client-pid", QUOTA_CRAWL_PID,
                                 "-l", logfile, mountdir, NULL);
        else
                runner_add_args (&runner, SBIN_DIR"/glusterfs",
                                 "-s", volfileserver,
                                 "--volfile-id", vol_id,
                                 "--use-readdirp=no",
                                 "--client-pid", QUOTA_CRAWL_PID,
                                 "-l", logfile, mountdir, NULL);

        synclock_unlock (&priv->big_lock);
        ret = runner_run_reuse (&runner);
        synclock_lock (&priv->big_lock);
        if (ret == -1) {
                runner_log (&runner, "glusterd", GF_LOG_DEBUG, "command failed");
                runner_end (&runner);
                goto out;
        }
        runner_end (&runner);

        if ((pid = fork ()) < 0) {
                gf_msg (THIS->name, GF_LOG_WARNING, 0,
                        GD_MSG_FORK_FAIL, "fork from parent failed");
                ret = -1;
                goto out;
        } else if (pid == 0) {//first child
                /* fork one more to not hold back main process on
                 * blocking call below
                 */
                pid = fork ();
                if (pid)
                        _exit (pid > 0 ? EXIT_SUCCESS : EXIT_FAILURE);

                ret = chdir (mountdir);
                if (ret == -1) {
                        gf_msg (THIS->name, GF_LOG_WARNING, errno,
                                GD_MSG_DIR_OP_FAILED, "chdir %s failed",
                                mountdir);
                        exit (EXIT_FAILURE);
                }
                runinit (&runner);

                if (type == GF_QUOTA_OPTION_TYPE_ENABLE ||
                    type == GF_QUOTA_OPTION_TYPE_ENABLE_OBJECTS)
                        runner_add_args (&runner, "/usr/bin/find", ".", NULL);

                else if (type == GF_QUOTA_OPTION_TYPE_DISABLE) {

#if defined(GF_DARWIN_HOST_OS)
                        runner_add_args (&runner, "/usr/bin/find", ".",
                                         "-exec", "/usr/bin/xattr", "-w",
                                         VIRTUAL_QUOTA_XATTR_CLEANUP_KEY, "1",
                                         "{}", "\\", ";", NULL);
#elif defined(__FreeBSD__)
                        runner_add_args (&runner, "/usr/bin/find", ".",
                                         "-exec", "/usr/sbin/setextattr",
                                         EXTATTR_NAMESPACE_USER,
                                         VIRTUAL_QUOTA_XATTR_CLEANUP_KEY, "1",
                                         "{}", "\\", ";", NULL);
#else
                        runner_add_args (&runner, "/usr/bin/find", ".",
                                         "-exec", _PATH_SETFATTR, "-n",
                                         VIRTUAL_QUOTA_XATTR_CLEANUP_KEY, "-v",
                                         "1", "{}", "\\", ";", NULL);
#endif

                }

                if (runner_start (&runner) == -1) {
                        gf_umount_lazy ("glusterd", mountdir, 1);
                        _exit (EXIT_FAILURE);
                }

                snprintf (pidfile, sizeof (pidfile), "%s/%s.pid", pid_dir,
                          brickpath);
                pidfp = fopen (pidfile, "w");
                if (pidfp) {
                        fprintf (pidfp, "%d\n", runner.chpid);
                        fflush (pidfp);
                        fclose (pidfp);
                }

#ifndef GF_LINUX_HOST_OS
                runner_end (&runner); /* blocks in waitpid */
#endif
                gf_umount_lazy ("glusterd", mountdir, 1);

                _exit (EXIT_SUCCESS);
        }
        ret = (waitpid (pid, &status, 0) == pid &&
               WIFEXITED (status) && WEXITSTATUS (status) == EXIT_SUCCESS) ? 0 : -1;

out:
        return ret;
}

void
glusterd_stop_all_quota_crawl_service (glusterd_conf_t *priv,
                                       glusterd_volinfo_t *volinfo, int type)
{
        DIR                       *dir                = NULL;
        struct dirent             *entry              = NULL;
        struct dirent              scratch[2]         = {{0,},};
        char                       pid_dir[PATH_MAX]  = {0,};
        char                       pidfile[PATH_MAX]  = {0,};

        GLUSTERD_GET_QUOTA_CRAWL_PIDDIR (pid_dir, volinfo, type);

        dir = sys_opendir (pid_dir);
        if (dir == NULL)
                return;

        GF_FOR_EACH_ENTRY_IN_DIR (entry, dir, scratch);
        while (entry) {
                snprintf (pidfile, sizeof (pidfile), "%s/%s",
                          pid_dir, entry->d_name);

                glusterd_service_stop_nolock ("quota_crawl", pidfile, SIGKILL,
                                              _gf_true);
                sys_unlink (pidfile);

                GF_FOR_EACH_ENTRY_IN_DIR (entry, dir, scratch);
        }
        sys_closedir (dir);
}

int32_t
glusterd_quota_initiate_fs_crawl (glusterd_conf_t *priv,
                                  glusterd_volinfo_t *volinfo, int type)
{
        int32_t                    ret                = -1;
        glusterd_brickinfo_t      *brick              = NULL;
        char                       pid_dir[PATH_MAX]  = {0, };

        GF_VALIDATE_OR_GOTO ("glusterd", THIS, out);

        ret = glusterd_generate_client_per_brick_volfile (volinfo);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLUSTERD_OP_FAILED,
                        "failed to generate client volume file");
                goto out;
        }

        ret = mkdir_p (DEFAULT_QUOTA_CRAWL_LOG_DIRECTORY, 0777, _gf_true);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_GLUSTERD_OP_FAILED,
                        "failed to create dir %s: %s",
                        DEFAULT_QUOTA_CRAWL_LOG_DIRECTORY, strerror (errno));
                goto out;
        }

        GLUSTERD_GET_QUOTA_CRAWL_PIDDIR (pid_dir, volinfo, type);
        ret = mkdir_p (pid_dir, 0777, _gf_true);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_ERROR, errno,
                        GD_MSG_GLUSTERD_OP_FAILED,
                        "failed to create dir %s: %s",
                        pid_dir, strerror (errno));
                goto out;
        }

        /* When quota enable is performed, stop alreday running enable crawl
         * process and start fresh crawl process. let disable process continue
         * if running to cleanup the older xattrs
         * When quota disable is performed, stop both enable/disable crawl
         * process and start fresh crawl process to cleanup the xattrs
         */
        glusterd_stop_all_quota_crawl_service (priv, volinfo,
                                               GF_QUOTA_OPTION_TYPE_ENABLE);
        if (type == GF_QUOTA_OPTION_TYPE_DISABLE)
                glusterd_stop_all_quota_crawl_service (priv, volinfo,
                                               GF_QUOTA_OPTION_TYPE_DISABLE);

        cds_list_for_each_entry (brick, &volinfo->bricks, brick_list) {
                if (gf_uuid_compare (brick->uuid, MY_UUID))
                        continue;

                ret = _glusterd_quota_initiate_fs_crawl (priv, volinfo, brick,
                                                         type, pid_dir);

                if (ret)
                        goto out;
        }

        ret = 0;
out:
        return ret;
}

int32_t
glusterd_quota_get_default_soft_limit (glusterd_volinfo_t *volinfo,
                                       dict_t *rsp_dict)
{
        int32_t            ret             = 0;
        xlator_t          *this            = NULL;
        glusterd_conf_t   *conf            = NULL;
        char              *default_limit   = NULL;
        char              *val             = NULL;

        if (rsp_dict == NULL)
                return -1;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        ret = glusterd_volinfo_get (volinfo, "features.default-soft-limit",
                                    &default_limit);
        if (default_limit)
                val = gf_strdup (default_limit);
        else
                val = gf_strdup ("80%");

        ret = dict_set_dynstr (rsp_dict, "default-soft-limit", val);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "Failed to set default "
                        "soft-limit into dict");
                goto out;
        }
        ret = 0;

out:
        return ret;
}

int32_t
glusterd_inode_quota_enable (glusterd_volinfo_t *volinfo, char **op_errstr,
                             gf_boolean_t *crawl)
{
        int32_t         ret     = -1;
        xlator_t        *this   = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_VALIDATE_OR_GOTO (this->name, volinfo, out);
        GF_VALIDATE_OR_GOTO (this->name, crawl, out);
        GF_VALIDATE_OR_GOTO (this->name, op_errstr, out);

        if (glusterd_is_volume_started (volinfo) == 0) {
                *op_errstr = gf_strdup ("Volume is stopped, start volume "
                                        "to enable inode quota.");
                ret = -1;
                goto out;
        }

        ret = glusterd_check_if_quota_trans_enabled (volinfo);
        if (ret != 0) {
                *op_errstr = gf_strdup ("Quota is disabled. Enabling quota "
                                        "will enable inode quota");
                ret = -1;
                goto out;
        }

        if (glusterd_is_volume_inode_quota_enabled (volinfo)) {
                *op_errstr = gf_strdup ("Inode Quota is already enabled");
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr_with_alloc (volinfo->dict,
                                          VKEY_FEATURES_INODE_QUOTA, "on");
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED,
                        "dict set failed");
                goto out;
        }

        *crawl = _gf_true;

        ret = glusterd_store_quota_config (volinfo, NULL, NULL,
                                           GF_QUOTA_OPTION_TYPE_ENABLE_OBJECTS,
                                           op_errstr);

        ret = 0;
out:
        if (ret && op_errstr && !*op_errstr)
                gf_asprintf (op_errstr, "Enabling inode quota on volume %s has "
                             "been unsuccessful", volinfo->volname);
        return ret;
}

int32_t
glusterd_quota_enable (glusterd_volinfo_t *volinfo, char **op_errstr,
                       gf_boolean_t *crawl)
{
        int32_t         ret     = -1;
        xlator_t        *this         = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_VALIDATE_OR_GOTO (this->name, volinfo, out);
        GF_VALIDATE_OR_GOTO (this->name, crawl, out);
        GF_VALIDATE_OR_GOTO (this->name, op_errstr, out);

        if (glusterd_is_volume_started (volinfo) == 0) {
                *op_errstr = gf_strdup ("Volume is stopped, start volume "
                                        "to enable quota.");
                ret = -1;
                goto out;
        }

        ret = glusterd_check_if_quota_trans_enabled (volinfo);
        if (ret == 0) {
                *op_errstr = gf_strdup ("Quota is already enabled");
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr_with_alloc (volinfo->dict, VKEY_FEATURES_QUOTA,
                                          "on");
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED, "dict set failed");
                goto out;
        }

        ret = dict_set_dynstr_with_alloc (volinfo->dict,
                                          VKEY_FEATURES_INODE_QUOTA, "on");
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "dict set failed");
                goto out;
        }

        ret = dict_set_dynstr_with_alloc (volinfo->dict,
                                          "features.quota-deem-statfs",
                                          "on");
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED, "setting quota-deem-statfs"
                        "in volinfo failed");
                goto out;
        }

        *crawl = _gf_true;

        ret = glusterd_store_quota_config (volinfo, NULL, NULL,
                                           GF_QUOTA_OPTION_TYPE_ENABLE,
                                           op_errstr);

        ret = 0;
out:
        if (ret && op_errstr && !*op_errstr)
                gf_asprintf (op_errstr, "Enabling quota on volume %s has been "
                             "unsuccessful", volinfo->volname);
        return ret;
}

int32_t
glusterd_quota_disable (glusterd_volinfo_t *volinfo, char **op_errstr,
                        gf_boolean_t *crawl)
{
        int32_t    ret            = -1;
        int        i              =  0;
        char      *value          = NULL;
        xlator_t  *this           = NULL;
        glusterd_conf_t *conf     = NULL;
        char *quota_options[]     = {"features.soft-timeout",
                                     "features.hard-timeout",
                                     "features.alert-time",
                                     "features.default-soft-limit",
                                     "features.quota-deem-statfs",
                                     "features.quota-timeout", NULL};

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        GF_VALIDATE_OR_GOTO (this->name, volinfo, out);
        GF_VALIDATE_OR_GOTO (this->name, op_errstr, out);

        ret = glusterd_check_if_quota_trans_enabled (volinfo);
        if (ret == -1) {
                *op_errstr = gf_strdup ("Quota is already disabled");
                goto out;
        }

        ret = dict_set_dynstr_with_alloc (volinfo->dict, VKEY_FEATURES_QUOTA,
                                          "off");
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_DICT_SET_FAILED, "dict set failed");
                goto out;
        }

        ret = dict_set_dynstr_with_alloc (volinfo->dict,
                                          VKEY_FEATURES_INODE_QUOTA, "off");
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "dict set failed");
                goto out;
        }

        for (i = 0; quota_options [i]; i++) {
                ret = glusterd_volinfo_get (volinfo, quota_options[i], &value);
                if (ret) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                GD_MSG_VOLINFO_GET_FAIL, "failed to get option"
                                " %s", quota_options[i]);
                } else {
                dict_del (volinfo->dict, quota_options[i]);
                }
        }

        //Remove aux mount of the volume on every node in the cluster
        ret = glusterd_remove_auxiliary_mount (volinfo->volname);
        if (ret)
                goto out;

        *crawl = _gf_true;

        (void) glusterd_clean_up_quota_store (volinfo);

        ret = 0;
out:
        if (ret && op_errstr && !*op_errstr)
                gf_asprintf (op_errstr, "Disabling quota on volume %s has been "
                             "unsuccessful", volinfo->volname);
        return ret;
}

static int
glusterd_set_quota_limit (char *volname, char *path, char *hard_limit,
                          char *soft_limit, char *key, char **op_errstr)
{
        int               ret                = -1;
        xlator_t         *this               = NULL;
        char              abspath[PATH_MAX]  = {0,};
        glusterd_conf_t  *priv               = NULL;
	quota_limits_t    existing_limit     = {0,};
	quota_limits_t    new_limit          = {0,};
        double            soft_limit_double  = 0;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        GLUSTERD_GET_QUOTA_AUX_MOUNT_PATH (abspath, volname, path);
        ret = gf_lstat_dir (abspath, NULL);
        if (ret) {
                gf_asprintf (op_errstr, "Failed to find the directory %s. "
                             "Reason : %s", abspath, strerror (errno));
                goto out;
        }

        if (!soft_limit) {
                ret = sys_lgetxattr (abspath, key, (void *)&existing_limit,
                                     sizeof (existing_limit));
                if (ret < 0) {
                        switch (errno) {
#if defined(ENOATTR) && (ENOATTR != ENODATA)
                        case ENODATA: /* FALLTHROUGH */
#endif
                        case ENOATTR:
                                existing_limit.sl = -1;
                            break;
                        default:
                                gf_asprintf (op_errstr, "Failed to get the "
                                             "xattr %s from %s. Reason : %s",
                                             key, abspath, strerror (errno));
                                goto out;
                        }
                } else {
                        existing_limit.hl = ntoh64 (existing_limit.hl);
                        existing_limit.sl = ntoh64 (existing_limit.sl);
                }
                new_limit.sl = existing_limit.sl;

        } else {
                ret = gf_string2percent (soft_limit, &soft_limit_double);
                if (ret)
                        goto out;
                new_limit.sl = soft_limit_double;
        }

        new_limit.sl = hton64 (new_limit.sl);

        ret = gf_string2bytesize_int64 (hard_limit, &new_limit.hl);
        if (ret)
                goto out;

        new_limit.hl = hton64 (new_limit.hl);

        ret = sys_lsetxattr (abspath, key, (char *)(void *)&new_limit,
                             sizeof (new_limit), 0);
        if (ret == -1) {
                gf_asprintf (op_errstr, "setxattr of %s failed on %s."
                             " Reason : %s", key, abspath, strerror (errno));
                goto out;
        }
        ret = 0;

out:
        return ret;
}

static int
glusterd_update_quota_conf_version (glusterd_volinfo_t *volinfo)
{
        volinfo->quota_conf_version++;
        return 0;
}

/*The function glusterd_find_gfid_match () does the following:
 * Given a buffer of gfids, the number of bytes read and the key gfid that needs
 * to be found, the function compares 16 bytes at a time from @buf against
 * @gfid.
 *
 * What happens when the match is found:
 * i. If the function was called as part of 'limit-usage' operation, the call
 *    returns with write_byte_count = bytes_read
 *ii. If the function as called as part of 'quota remove' operation, @buf
 *    is modified in memory such that the match is deleted from the buffer, and
 *    also @write_byte_count is set to original buf size minus the sixteen bytes
 *    that was deleted as part of 'remove'.
 *
 * What happens when the match is not found in the current buffer:
 * The function returns with write_byte_count = bytes_read, which means to say
 * that the caller of this function must write the entire buffer to the tmp file
 * and continue the search.
 */
static gf_boolean_t
glusterd_find_gfid_match_3_6 (uuid_t gfid, unsigned char *buf,
                              size_t bytes_read, int opcode,
                              size_t *write_byte_count)
{
        int           gfid_index  = 0;
        int           shift_count = 0;
        unsigned char tmp_buf[17] = {0,};

        /* This function if for backward compatibility */

        while (gfid_index != bytes_read) {
                memcpy ((void *)tmp_buf, (void *)&buf[gfid_index], 16);
                if (!gf_uuid_compare (gfid, tmp_buf)) {
                        if (opcode == GF_QUOTA_OPTION_TYPE_REMOVE) {
                                shift_count = bytes_read - (gfid_index + 16);
                                memmove ((void *)&buf[gfid_index],
                                         (void *)&buf[gfid_index+16],
                                         shift_count);
                                *write_byte_count = bytes_read - 16;
                        } else {
                                *write_byte_count = bytes_read;
                        }
                        return _gf_true;
                } else {
                        gfid_index += 16;
                }
        }
        if (gfid_index == bytes_read)
                *write_byte_count = bytes_read;

        return _gf_false;
}

static gf_boolean_t
glusterd_find_gfid_match (uuid_t gfid, char gfid_type, unsigned char *buf,
                          size_t bytes_read, int opcode,
                          size_t *write_byte_count)
{
        int                 gfid_index  = 0;
        int                 shift_count = 0;
        unsigned char       tmp_buf[17] = {0,};
        char                type        = 0;
        xlator_t           *this        = NULL;
        glusterd_conf_t    *conf        = NULL;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);

        conf = this->private;
        GF_VALIDATE_OR_GOTO (this->name, conf, out);

        if (conf->op_version < GD_OP_VERSION_3_7_0)
                return glusterd_find_gfid_match_3_6 (gfid, buf, bytes_read,
                                                     opcode, write_byte_count);

        while (gfid_index != bytes_read) {
                memcpy ((void *)tmp_buf, (void *)&buf[gfid_index], 16);
                type = buf[gfid_index + 16];

                if (!gf_uuid_compare (gfid, tmp_buf) && type == gfid_type) {
                        if (opcode == GF_QUOTA_OPTION_TYPE_REMOVE ||
                            opcode == GF_QUOTA_OPTION_TYPE_REMOVE_OBJECTS) {
                                shift_count = bytes_read - (gfid_index + 17);
                                memmove ((void *)&buf[gfid_index],
                                         (void *)&buf[gfid_index + 17],
                                         shift_count);
                                *write_byte_count = bytes_read - 17;
                        } else {
                                *write_byte_count = bytes_read;
                        }
                        return _gf_true;
                } else {
                        gfid_index += 17;
                }
        }
        if (gfid_index == bytes_read)
                *write_byte_count = bytes_read;

out:

        return _gf_false;
}

/* The function glusterd_copy_to_tmp_file() reads the "remaining" bytes from
 * the source fd and writes them to destination fd, at the rate of 128K bytes
 * of read+write at a time.
 */

static int
glusterd_copy_to_tmp_file (int src_fd, int dst_fd)
{
        int            ret         = 0;
        size_t         entry_sz    = 131072;
        ssize_t        bytes_read  = 0;
        unsigned char  buf[131072] = {0,};
        xlator_t      *this        = NULL;

        this = THIS;
        GF_ASSERT (this);

        while ((bytes_read = sys_read (src_fd, (void *)&buf, entry_sz)) > 0) {
                if (bytes_read % 16 != 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_QUOTA_CONF_CORRUPT, "quota.conf "
                                "corrupted");
                        ret = -1;
                        goto out;
                }
                ret = sys_write (dst_fd, (void *) buf, bytes_read);
                if (ret == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_QUOTA_CONF_WRITE_FAIL,
                                "write into quota.conf failed.");
                        goto out;
                }
        }
        ret = 0;

out:
        return ret;
}

int
glusterd_store_quota_conf_upgrade (glusterd_volinfo_t *volinfo)
{
        int                ret                   = -1;
        int                fd                    = -1;
        int                conf_fd               = -1;
        unsigned char      gfid[17]              = {0,};
        xlator_t          *this                  = NULL;
        char               type                  = 0;

        this = THIS;
        GF_ASSERT (this);

        fd = gf_store_mkstemp (volinfo->quota_conf_shandle);
        if (fd < 0) {
                ret = -1;
                goto out;
        }

        conf_fd = open (volinfo->quota_conf_shandle->path, O_RDONLY);
        if (conf_fd == -1) {
                ret = -1;
                goto out;
        }

        ret = quota_conf_skip_header (conf_fd);
        if (ret)
                goto out;

        ret = glusterd_quota_conf_write_header (fd);
        if (ret)
                goto out;

        while (1) {
                ret = quota_conf_read_gfid (conf_fd, gfid, &type, 1.1);
                if (ret == 0)
                        break;
                else if (ret < 0)
                        goto out;

                ret = glusterd_quota_conf_write_gfid (fd, gfid,
                                             GF_QUOTA_CONF_TYPE_USAGE);
                if (ret < 0)
                        goto out;
        }

out:
        if (conf_fd != -1)
                sys_close (conf_fd);

        if (ret && (fd > 0)) {
                gf_store_unlink_tmppath (volinfo->quota_conf_shandle);
        } else if (!ret) {
                ret = gf_store_rename_tmppath (volinfo->quota_conf_shandle);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_FILE_OP_FAILED,
                                "Failed to rename "
                                "quota conf file");
                        return ret;
                }

                ret = glusterd_compute_cksum (volinfo, _gf_true);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_CKSUM_COMPUTE_FAIL, "Failed to "
                                "compute cksum for quota conf file");
                        return ret;
                }

                ret = glusterd_store_save_quota_version_and_cksum (volinfo);
                if (ret)
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_QUOTA_CKSUM_VER_STORE_FAIL, "Failed to "
                                "store quota version and cksum");
        }

        return ret;
}

int
glusterd_store_quota_config (glusterd_volinfo_t *volinfo, char *path,
                             char *gfid_str, int opcode, char **op_errstr)
{
        int                ret                   = -1;
        int                fd                    = -1;
        int                conf_fd               = -1;
        ssize_t            bytes_read            = 0;
        size_t             bytes_to_write        = 0;
        unsigned char      buf[131072]           = {0,};
        uuid_t             gfid                  = {0,};
        xlator_t          *this                  = NULL;
        gf_boolean_t       found                 = _gf_false;
        gf_boolean_t       modified              = _gf_false;
        gf_boolean_t       is_file_empty         = _gf_false;
        gf_boolean_t       is_first_read         = _gf_true;
        glusterd_conf_t   *conf                  = NULL;
        float              version               = 0.0f;
        char               type                  = 0;
        int                quota_conf_line_sz    = 16;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        glusterd_store_create_quota_conf_sh_on_absence (volinfo);

        conf_fd = open (volinfo->quota_conf_shandle->path, O_RDONLY);
        if (conf_fd == -1) {
                ret = -1;
                goto out;
        }

        ret = quota_conf_read_version (conf_fd, &version);
        if (ret)
                goto out;

        if (version < 1.2f && conf->op_version >= GD_OP_VERSION_3_7_0) {
                /* Upgrade quota.conf file to newer format */
                sys_close (conf_fd);
                ret = glusterd_store_quota_conf_upgrade(volinfo);
                if (ret)
                        goto out;

                conf_fd = open (volinfo->quota_conf_shandle->path, O_RDONLY);
                if (conf_fd == -1) {
                        ret = -1;
                        goto out;
                }

                ret = quota_conf_skip_header (conf_fd);
                if (ret)
                        goto out;
        }

        /* If op-ver is gt 3.7, then quota.conf will be upgraded, and 17 bytes
         * storted in the new format. 16 bytes uuid and
         * 1 byte type (usage/object)
         */
        if (conf->op_version >= GD_OP_VERSION_3_7_0)
                quota_conf_line_sz++;

        fd = gf_store_mkstemp (volinfo->quota_conf_shandle);
        if (fd < 0) {
                ret = -1;
                goto out;
        }

        ret = glusterd_quota_conf_write_header (fd);
        if (ret)
                goto out;


        /* Just create empty quota.conf file if create */
        if (GF_QUOTA_OPTION_TYPE_ENABLE == opcode ||
            GF_QUOTA_OPTION_TYPE_ENABLE_OBJECTS == opcode ||
            GF_QUOTA_OPTION_TYPE_UPGRADE == opcode) {
                /* Opcode will be GF_QUOTA_OPTION_TYPE_UPGRADE when there is
                 * an upgrade from 3.6 to 3.7. Just upgrade the quota.conf
                 * file even during an op-version bumpup and exit.
                 */
                modified = _gf_true;
                goto out;
        }

        /* Check if gfid_str is given for opts other than ENABLE */
        if (!gfid_str) {
                ret = -1;
                goto out;
        }
        gf_uuid_parse (gfid_str, gfid);

        if (opcode > GF_QUOTA_OPTION_TYPE_VERSION_OBJECTS)
                type = GF_QUOTA_CONF_TYPE_OBJECTS;
        else
                type = GF_QUOTA_CONF_TYPE_USAGE;

        for (;;) {
                bytes_read = sys_read (conf_fd, (void *)&buf, sizeof (buf));
                if (bytes_read <= 0) {
                        /*The flag @is_first_read is TRUE when the loop is
                         * entered, and is set to false if the first read
                         * reads non-zero bytes of data. The flag is used to
                         * detect if quota.conf is an empty file, but for the
                         * header. This is done to log appropriate error message
                         * when 'quota remove' is attempted when there are no
                         * limits set on the given volume.
                         */
                        if (is_first_read)
                                is_file_empty = _gf_true;
                        break;
                }
                if ((bytes_read % quota_conf_line_sz) != 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_QUOTA_CONF_CORRUPT, "quota.conf "
                                "corrupted");
                        ret = -1;
                        goto out;
                }
                found = glusterd_find_gfid_match (gfid, type, buf, bytes_read,
                                                  opcode, &bytes_to_write);

                ret = sys_write (fd, (void *) buf, bytes_to_write);
                if (ret == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_QUOTA_CONF_WRITE_FAIL,
                                "write into quota.conf failed.");
                        goto out;
                }

                /*If the match is found in this iteration, copy the rest of
                 * quota.conf into quota.conf.tmp and break.
                 * Else continue with the search.
                 */
                if (found) {
                        ret = glusterd_copy_to_tmp_file (conf_fd, fd);
                        if (ret)
                                goto out;
                        break;
                }
                is_first_read = _gf_false;
        }

        switch (opcode) {
        case GF_QUOTA_OPTION_TYPE_LIMIT_USAGE:
                if (!found) {
                        ret = glusterd_quota_conf_write_gfid (fd, gfid,
                                                     GF_QUOTA_CONF_TYPE_USAGE);
                        if (ret == -1) {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        GD_MSG_QUOTA_CONF_WRITE_FAIL,
                                        "write into quota.conf failed. ");
                                goto out;
                        }
                        modified = _gf_true;
                }
                break;
        case GF_QUOTA_OPTION_TYPE_LIMIT_OBJECTS:
                if (!found) {
                        ret = glusterd_quota_conf_write_gfid (fd, gfid,
                                                   GF_QUOTA_CONF_TYPE_OBJECTS);
                        if (ret == -1) {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        GD_MSG_QUOTA_CONF_WRITE_FAIL,
                                        "write into quota.conf failed. ");
                                goto out;
                        }
                        modified = _gf_true;
                }
                break;

        case GF_QUOTA_OPTION_TYPE_REMOVE:
        case GF_QUOTA_OPTION_TYPE_REMOVE_OBJECTS:
                if (is_file_empty) {
                        gf_asprintf (op_errstr, "Cannot remove limit on"
                                     " %s. The quota configuration file"
                                     " for volume %s is empty.", path,
                                     volinfo->volname);
                        ret = -1;
                        goto out;
                } else {
                        if (!found) {
                                gf_asprintf (op_errstr, "Error. gfid %s"
                                             " for path %s not found in"
                                             " store", gfid_str, path);
                                ret = -1;
                                goto out;
                        } else {
                                modified = _gf_true;
                        }
                }
                break;

        default:
                ret = 0;
                break;
        }

        if (modified)
                glusterd_update_quota_conf_version (volinfo);

        ret = 0;
out:
        if (conf_fd != -1) {
                sys_close (conf_fd);
        }

        if (ret && (fd > 0)) {
                gf_store_unlink_tmppath (volinfo->quota_conf_shandle);
        } else if (!ret) {
                ret = gf_store_rename_tmppath (volinfo->quota_conf_shandle);
                if (modified) {
                        ret = glusterd_compute_cksum (volinfo, _gf_true);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_CKSUM_COMPUTE_FAIL, "Failed to "
                                        "compute cksum for quota conf file");
                                return ret;
                        }

                        ret = glusterd_store_save_quota_version_and_cksum
                                                                      (volinfo);
                        if (ret)
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_VERS_CKSUM_STORE_FAIL,
                                        "Failed to "
                                        "store quota version and cksum");
                }
        }

        return ret;
}

int32_t
glusterd_quota_limit_usage (glusterd_volinfo_t *volinfo, dict_t *dict,
                            int opcode, char **op_errstr)
{
        int32_t          ret                = -1;
        char            *path               = NULL;
        char            *hard_limit         = NULL;
        char            *soft_limit         = NULL;
        char            *gfid_str           = NULL;
        xlator_t        *this               = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_VALIDATE_OR_GOTO (this->name, dict, out);
        GF_VALIDATE_OR_GOTO (this->name, volinfo, out);
        GF_VALIDATE_OR_GOTO (this->name, op_errstr, out);

        ret = glusterd_check_if_quota_trans_enabled (volinfo);
        if (ret == -1) {
                *op_errstr = gf_strdup ("Quota is disabled, please enable "
                                        "quota");
                goto out;
        }

        ret = dict_get_str (dict, "path", &path);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to fetch path");
                goto out;
        }
        ret = gf_canonicalize_path (path);
        if (ret)
                goto out;

        ret = dict_get_str (dict, "hard-limit", &hard_limit);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to fetch hard limit");
                goto out;
        }

        if (dict_get (dict, "soft-limit")) {
                ret = dict_get_str (dict, "soft-limit", &soft_limit);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "Unable to fetch "
                                "soft limit");
                        goto out;
                }
        }

        if (is_origin_glusterd (dict)) {
                if (opcode == GF_QUOTA_OPTION_TYPE_LIMIT_USAGE) {
                        ret = glusterd_set_quota_limit (volinfo->volname, path,
                                                        hard_limit, soft_limit,
                                                        QUOTA_LIMIT_KEY,
                                                        op_errstr);
                } else {
                        ret = glusterd_set_quota_limit (volinfo->volname, path,
                                                        hard_limit, soft_limit,
                                                        QUOTA_LIMIT_OBJECTS_KEY,
                                                        op_errstr);
                }
                if (ret)
                        goto out;
        }

        ret = dict_get_str (dict, "gfid", &gfid_str);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Failed to get gfid of path "
                        "%s", path);
                goto out;
        }

        ret = glusterd_store_quota_config (volinfo, path, gfid_str, opcode,
                                           op_errstr);
        if (ret)
                goto out;

        ret = 0;
out:

        if (ret && op_errstr && !*op_errstr)
                gf_asprintf (op_errstr, "Failed to set hard limit on path %s "
                             "for volume %s", path, volinfo->volname);
        return ret;
}

static int
glusterd_remove_quota_limit (char *volname, char *path, char **op_errstr,
                             int type)
{
        int               ret                = -1;
        xlator_t         *this               = NULL;
        char              abspath[PATH_MAX]  = {0,};
        glusterd_conf_t  *priv               = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        GLUSTERD_GET_QUOTA_AUX_MOUNT_PATH (abspath, volname, path);
        ret = gf_lstat_dir (abspath, NULL);
        if (ret) {
                gf_asprintf (op_errstr, "Failed to find the directory %s. "
                             "Reason : %s", abspath, strerror (errno));
                goto out;
        }

        if (type == GF_QUOTA_OPTION_TYPE_REMOVE) {
                ret = sys_lremovexattr (abspath, QUOTA_LIMIT_KEY);
                if (ret) {
                        gf_asprintf (op_errstr, "removexattr failed on %s. "
                                     "Reason : %s", abspath, strerror (errno));
                        goto out;
                }
        }

        if (type == GF_QUOTA_OPTION_TYPE_REMOVE_OBJECTS) {
                ret = sys_lremovexattr (abspath, QUOTA_LIMIT_OBJECTS_KEY);
                if (ret) {
                        gf_asprintf (op_errstr, "removexattr failed on %s. "
                                     "Reason : %s", abspath, strerror (errno));
                        goto out;
                }
        }
        ret = 0;

out:
        return ret;
}

int32_t
glusterd_quota_remove_limits (glusterd_volinfo_t *volinfo, dict_t *dict,
                              int opcode, char **op_errstr, int type)
{
        int32_t         ret                   = -1;
        char            *path                 = NULL;
        char            *gfid_str             = NULL;
        xlator_t        *this                 = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_VALIDATE_OR_GOTO (this->name, dict, out);
        GF_VALIDATE_OR_GOTO (this->name, volinfo, out);
        GF_VALIDATE_OR_GOTO (this->name, op_errstr, out);

        ret = glusterd_check_if_quota_trans_enabled (volinfo);
        if (ret == -1) {
                *op_errstr = gf_strdup ("Quota is disabled, please enable "
                                        "quota");
                goto out;
        }

        ret = dict_get_str (dict, "path", &path);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to fetch path");
                goto out;
        }

        ret = gf_canonicalize_path (path);
        if (ret)
                goto out;

        if (is_origin_glusterd (dict)) {
                ret = glusterd_remove_quota_limit (volinfo->volname, path,
                                                   op_errstr, type);
                if (ret)
                        goto out;
        }

        ret = dict_get_str (dict, "gfid", &gfid_str);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Failed to get gfid of path "
                        "%s", path);
                goto out;
        }

        ret = glusterd_store_quota_config (volinfo, path, gfid_str, opcode,
                                           op_errstr);
        if (ret)
                goto out;


        ret = 0;

out:
        return ret;
}

int
glusterd_set_quota_option (glusterd_volinfo_t *volinfo, dict_t *dict,
                           char *key, char **op_errstr)
{
        int        ret    = 0;
        char      *value  = NULL;
        xlator_t  *this   = NULL;
        char      *option = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = glusterd_check_if_quota_trans_enabled (volinfo);
        if (ret == -1) {
                gf_asprintf (op_errstr, "Cannot set %s. Quota on volume %s is "
                                        "disabled", key, volinfo->volname);
                return -1;
        }

        ret = dict_get_str (dict, "value", &value);
        if(ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Option value absent.");
                return -1;
        }

        option = gf_strdup (value);
        ret = dict_set_dynstr (volinfo->dict, key, option);
        if(ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Failed to set option %s",
                        key);
                return -1;
        }

        return 0;
}

static int
glusterd_quotad_op (int opcode)
{
        int              ret  = -1;
        xlator_t        *this = NULL;
        glusterd_conf_t *priv = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        switch (opcode) {
                case GF_QUOTA_OPTION_TYPE_ENABLE:
                case GF_QUOTA_OPTION_TYPE_DISABLE:

                        if (glusterd_all_volumes_with_quota_stopped ())
                                ret = glusterd_svc_stop (&(priv->quotad_svc),
                                                         SIGTERM);
                        else
                                ret = priv->quotad_svc.manager
                                                (&(priv->quotad_svc), NULL,
                                                 PROC_START);
                        break;

                default:
                        ret = 0;
                        break;
        }
        return ret;
}

int
glusterd_op_quota (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        glusterd_volinfo_t     *volinfo      = NULL;
        int32_t                 ret          = -1;
        char                   *volname      = NULL;
        int                     type         = -1;
        gf_boolean_t            start_crawl  = _gf_false;
        glusterd_conf_t        *priv         = NULL;
        xlator_t               *this         = NULL;

        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_asprintf (op_errstr, FMTSTR_CHECK_VOL_EXISTS, volname);
                goto out;
        }

        ret = dict_get_int32 (dict, "type", &type);

        if (!glusterd_is_quota_supported (type, op_errstr)) {
                ret = -1;
                goto out;
        }

        switch (type) {
                case GF_QUOTA_OPTION_TYPE_ENABLE:
                        ret = glusterd_quota_enable (volinfo, op_errstr,
                                                     &start_crawl);
                        if (ret < 0)
                                goto out;
                        break;

                case GF_QUOTA_OPTION_TYPE_ENABLE_OBJECTS:
                        ret = glusterd_inode_quota_enable (volinfo, op_errstr,
                                                           &start_crawl);
                        if (ret < 0)
                                goto out;
                        break;

                case GF_QUOTA_OPTION_TYPE_DISABLE:
                        ret = glusterd_quota_disable (volinfo, op_errstr,
                                                      &start_crawl);
                        if (ret < 0)
                                goto out;

                        break;

                case GF_QUOTA_OPTION_TYPE_LIMIT_USAGE:
                case GF_QUOTA_OPTION_TYPE_LIMIT_OBJECTS:
                        ret = glusterd_quota_limit_usage (volinfo, dict, type,
                                                          op_errstr);
                        goto out;

                case GF_QUOTA_OPTION_TYPE_REMOVE:
                case GF_QUOTA_OPTION_TYPE_REMOVE_OBJECTS:
                        ret = glusterd_quota_remove_limits (volinfo, dict, type,
                                                            op_errstr, type);
                        goto out;

                case GF_QUOTA_OPTION_TYPE_LIST:
                case GF_QUOTA_OPTION_TYPE_LIST_OBJECTS:
                        ret = glusterd_check_if_quota_trans_enabled (volinfo);
                        if (ret == -1) {
                                *op_errstr = gf_strdup ("Cannot list limits, "
                                                        "quota is disabled");
                                goto out;
                        }
                        ret = glusterd_quota_get_default_soft_limit (volinfo,
                                                               rsp_dict);
                        goto out;

                case GF_QUOTA_OPTION_TYPE_SOFT_TIMEOUT:
                        ret = glusterd_set_quota_option (volinfo, dict,
                                                        "features.soft-timeout",
                                                         op_errstr);
                        if (ret)
                                goto out;
                        break;

                case GF_QUOTA_OPTION_TYPE_HARD_TIMEOUT:
                        ret = glusterd_set_quota_option (volinfo, dict,
                                                        "features.hard-timeout",
                                                         op_errstr);
                        if (ret)
                                goto out;
                        break;

                case GF_QUOTA_OPTION_TYPE_ALERT_TIME:
                        ret = glusterd_set_quota_option (volinfo, dict,
                                                         "features.alert-time",
                                                         op_errstr);
                        if (ret)
                                goto out;
                        break;

                case GF_QUOTA_OPTION_TYPE_DEFAULT_SOFT_LIMIT:
                        ret = glusterd_set_quota_option (volinfo, dict,
                                                  "features.default-soft-limit",
                                                  op_errstr);
                        if (ret)
                                goto out;
                        break;

                default:
                        gf_asprintf (op_errstr, "Quota command failed. Invalid "
                                     "opcode");
                        ret = -1;
                        goto out;
        }

        if (priv->op_version > GD_OP_VERSION_MIN) {
                ret = glusterd_quotad_op (type);
                if (ret)
                        goto out;
        }


        if (GF_QUOTA_OPTION_TYPE_ENABLE == type)
                volinfo->quota_xattr_version++;
        ret = glusterd_store_volinfo (volinfo,
                                      GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
                if (GF_QUOTA_OPTION_TYPE_ENABLE == type)
                        volinfo->quota_xattr_version--;
                goto out;
        }

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_VOLFILE_CREATE_FAIL, "Unable to re-create "
                                                  "volfiles");
                if (GF_QUOTA_OPTION_TYPE_ENABLE == type) {
                        /* rollback volinfo */
                        volinfo->quota_xattr_version--;
                        ret = glusterd_store_volinfo (volinfo,
                                      GLUSTERD_VOLINFO_VER_AC_INCREMENT);
                }

                ret = -1;
                goto out;
        }

        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                if (priv->op_version == GD_OP_VERSION_MIN)
                        ret = priv->nfs_svc.manager (&(priv->nfs_svc), NULL, 0);
        }

        if (rsp_dict && start_crawl == _gf_true)
                glusterd_quota_initiate_fs_crawl (priv, volinfo, type);

        ret = 0;
out:
        return ret;
}

/*
 * glusterd_get_gfid_from_brick() fetches the 'trusted.gfid' attribute of @path
 * from each brick in the backend and places the same in the rsp_dict with the
 * keys being gfid0, gfid1, gfid2 and so on. The absence of @path in the backend
 * is not treated as error.
 */
static int
glusterd_get_gfid_from_brick (dict_t *dict, glusterd_volinfo_t *volinfo,
                              dict_t *rsp_dict, char **op_errstr)
{
        int                    ret                    = -1;
        int                    count                  = 0;
        char                  *path                   = NULL;
        char                   backend_path[PATH_MAX] = {0,};
        xlator_t              *this                   = NULL;
        glusterd_conf_t       *priv                   = NULL;
        glusterd_brickinfo_t  *brickinfo              = NULL;
        char                   key[256]               = {0,};
        char                  *gfid_str               = NULL;
        uuid_t                 gfid;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "path", &path);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Failed to get path");
                goto out;
        }

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                ret = glusterd_resolve_brick (brickinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_RESOLVE_BRICK_FAIL, FMTSTR_RESOLVE_BRICK,
                                brickinfo->hostname, brickinfo->path);
                        goto out;
                }

                if (gf_uuid_compare (brickinfo->uuid, MY_UUID))
                        continue;

                if (brickinfo->vg[0])
                        continue;

                snprintf (backend_path, sizeof (backend_path), "%s%s",
                          brickinfo->path, path);

                ret = gf_lstat_dir (backend_path, NULL);
                if (ret) {
                        gf_msg (this->name, GF_LOG_INFO, errno,
                                GD_MSG_DIR_OP_FAILED, "Failed to find "
                                "directory %s.", backend_path);
                        ret = 0;
                        continue;
                }
                ret = sys_lgetxattr (backend_path, GFID_XATTR_KEY, gfid, 16);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_INFO, errno,
                                GD_MSG_SETXATTR_FAIL, "Failed to get "
                                "extended attribute %s for directory %s. ",
                                GFID_XATTR_KEY, backend_path);
                        ret = 0;
                        continue;
                }
                snprintf (key, sizeof (key), "gfid%d", count);

                gfid_str = gf_strdup (uuid_utoa (gfid));
                if (!gfid_str) {
                        ret = -1;
                        goto out;
                }

                ret = dict_set_dynstr (rsp_dict, key, gfid_str);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED, "Failed to place "
                                "gfid of %s in dict", backend_path);
                        GF_FREE (gfid_str);
                        goto out;
                }
                count++;
        }

        ret = dict_set_int32 (rsp_dict, "count", count);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_SET_FAILED, "Failed to set count");
                goto out;
        }

        ret = 0;
out:
        return ret;
}

static int
_glusterd_validate_quota_opts (dict_t *dict, int type, char **errstr)
{
        int                     ret = -1;
        xlator_t                *this = THIS;
        void                    *quota_xl = NULL;
        volume_opt_list_t       opt_list = {{0},};
        volume_option_t         *opt = NULL;
        char                    *key = NULL;
        char                    *value = NULL;

        GF_ASSERT (dict);
        GF_ASSERT (this);

        ret = xlator_volopt_dynload ("features/quota", &quota_xl, &opt_list);
        if (ret)
                goto out;

        switch (type) {
        case GF_QUOTA_OPTION_TYPE_SOFT_TIMEOUT:
        case GF_QUOTA_OPTION_TYPE_HARD_TIMEOUT:
        case GF_QUOTA_OPTION_TYPE_ALERT_TIME:
        case GF_QUOTA_OPTION_TYPE_DEFAULT_SOFT_LIMIT:
                key = (char *)gd_quota_op_list[type];
                break;
        default:
                ret = -1;
                goto out;
        }

        opt = xlator_volume_option_get_list (&opt_list, key);
        if (!opt) {
                ret = -1;
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_UNKNOWN_KEY, "Unknown option: %s", key);
                goto out;
        }
        ret = dict_get_str (dict, "value", &value);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Value not found for key %s",
                        key);
                goto out;
        }

        ret = xlator_option_validate (this, key, value, opt, errstr);

out:
        if (quota_xl) {
                dlclose (quota_xl);
                quota_xl = NULL;
        }
        return ret;
}

static int
glusterd_create_quota_auxiliary_mount (xlator_t *this, char *volname)
{
        int                ret                     = -1;
        char               mountdir[PATH_MAX]      = {0,};
        char               pidfile_path[PATH_MAX]  = {0,};
        char               logfile[PATH_MAX]       = {0,};
        char               qpid[16]                = {0,};
        char              *volfileserver           = NULL;
        glusterd_conf_t   *priv                    = NULL;
        struct stat        buf                     = {0,};

        GF_VALIDATE_OR_GOTO ("glusterd", this, out);
        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        GLUSTERFS_GET_AUX_MOUNT_PIDFILE (pidfile_path, volname);

        if (gf_is_service_running (pidfile_path, NULL)) {
                gf_msg_debug (this->name, 0, "Aux mount of volume %s is running"
                              " already", volname);
                ret = 0;
                goto out;
        }

        if (glusterd_is_fuse_available () == _gf_false) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_MOUNT_REQ_FAIL, "Fuse unavailable");
                ret = -1;
                goto out;
        }

        GLUSTERD_GET_QUOTA_AUX_MOUNT_PATH (mountdir, volname, "/");
        ret = sys_mkdir (mountdir, 0777);
        if (ret && errno != EEXIST) {
                gf_msg (this->name, GF_LOG_ERROR, errno,
                        GD_MSG_MOUNT_REQ_FAIL, "Failed to create auxiliary "
                        "mount directory %s", mountdir);
                goto out;
        }
        snprintf (logfile, PATH_MAX-1, "%s/quota-mount-%s.log",
                  DEFAULT_LOG_FILE_DIRECTORY, volname);
        snprintf(qpid, 15, "%d", GF_CLIENT_PID_QUOTA_MOUNT);

        if (dict_get_str (this->options, "transport.socket.bind-address",
                          &volfileserver) != 0)
                volfileserver = "localhost";

        synclock_unlock (&priv->big_lock);
        ret = runcmd (SBIN_DIR"/glusterfs",
                      "--volfile-server", volfileserver,
                      "--volfile-id", volname,
                      "-l", logfile,
                      "-p", pidfile_path,
                      "--client-pid", qpid,
                      mountdir,
                      NULL);
        if (ret == 0) {
                /* Block here till mount process is ready to accept FOPs.
                 * Else, if glusterd acquires biglock below before
                 * mount process is ready, then glusterd and mount process
                 * can get into a deadlock situation.
                 */
                ret = sys_stat (mountdir, &buf);
                if (ret < 0)
                        ret = -errno;
        } else {
                ret = -errno;
        }

        synclock_lock (&priv->big_lock);

        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, -ret,
                        GD_MSG_MOUNT_REQ_FAIL, "Failed to mount glusterfs "
                        "client. Please check the log file %s for more details",
                        logfile);
                ret = -1;
                goto out;
        }

        ret = 0;

out:
        return ret;
}

int
glusterd_op_stage_quota (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        int                 ret            = 0;
        char               *volname        = NULL;
        gf_boolean_t        exists         = _gf_false;
        int                 type           = 0;
        xlator_t           *this           = NULL;
        glusterd_conf_t    *priv           = NULL;
        glusterd_volinfo_t *volinfo        = NULL;
        char               *hard_limit_str = NULL;
        int64_t             hard_limit     = 0;
        gf_boolean_t        get_gfid       = _gf_false;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_DICT_GET_FAILED, "Unable to get volume name");
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        if (!exists) {
                gf_asprintf (op_errstr, FMTSTR_CHECK_VOL_EXISTS, volname);
                ret = -1;
                goto out;
        }
        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_asprintf (op_errstr, FMTSTR_CHECK_VOL_EXISTS, volname);
                goto out;
        }

        if (!glusterd_is_volume_started (volinfo)) {
                *op_errstr = gf_strdup ("Volume is stopped, start volume "
                                        "before executing quota command.");
                ret = -1;
                goto out;
        }

        ret = dict_get_int32 (dict, "type", &type);
        if (ret) {
                *op_errstr = gf_strdup ("Volume quota failed, internal error, "
                                        "unable to get type of operation");
                goto out;
        }

        if ((!glusterd_is_volume_quota_enabled (volinfo)) &&
            (type != GF_QUOTA_OPTION_TYPE_ENABLE)) {
                *op_errstr = gf_strdup ("Quota is disabled, please enable "
                                        "quota");
                ret = -1;
                goto out;
        }

        if (type > GF_QUOTA_OPTION_TYPE_VERSION_OBJECTS) {
                if (!glusterd_is_volume_inode_quota_enabled (volinfo) &&
                    type != GF_QUOTA_OPTION_TYPE_ENABLE_OBJECTS) {
                        *op_errstr = gf_strdup ("Inode Quota is disabled, "
                                                "please enable inode quota");
                        ret = -1;
                        goto out;
                }
        }

        if (!glusterd_is_quota_supported (type, op_errstr)) {
                ret = -1;
                goto out;
        }

        if ((GF_QUOTA_OPTION_TYPE_ENABLE != type) &&
            (glusterd_check_if_quota_trans_enabled (volinfo) != 0)) {
                ret = -1;
                gf_asprintf (op_errstr, "Quota is not enabled on volume %s",
                             volname);
                goto out;
        }

        switch (type) {
        case GF_QUOTA_OPTION_TYPE_LIST:
        case GF_QUOTA_OPTION_TYPE_LIST_OBJECTS:
        case GF_QUOTA_OPTION_TYPE_LIMIT_USAGE:
        case GF_QUOTA_OPTION_TYPE_LIMIT_OBJECTS:
        case GF_QUOTA_OPTION_TYPE_REMOVE:
        case GF_QUOTA_OPTION_TYPE_REMOVE_OBJECTS:
                /* Quota auxiliary mount is needed by CLI
                 * for list command and need by glusterd for
                 * setting/removing limit
                 */
                if (is_origin_glusterd (dict)) {
                        ret = glusterd_create_quota_auxiliary_mount (this,
                                                                     volname);
                        if (ret) {
                                *op_errstr = gf_strdup ("Failed to start aux "
                                                        "mount");
                                goto out;
                        }
                }
                break;
        }

        switch (type) {
        case GF_QUOTA_OPTION_TYPE_LIMIT_USAGE:
                ret = dict_get_str (dict, "hard-limit", &hard_limit_str);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Failed to get hard-limit from dict");
                        goto out;
                }
                ret = gf_string2bytesize_int64 (hard_limit_str, &hard_limit);
                if (ret) {
                        if (errno == ERANGE || hard_limit < 0)
                                gf_asprintf (op_errstr, "Hard-limit "
                                        "value out of range (0 - %"PRId64
                                        "): %s", hard_limit_str);
                        else
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        GD_MSG_CONVERSION_FAILED,
                                        "Failed to convert hard-limit "
                                        "string to value");
                        goto out;
                }
                get_gfid = _gf_true;
                break;
        case GF_QUOTA_OPTION_TYPE_LIMIT_OBJECTS:
                get_gfid = _gf_true;
                break;

        case GF_QUOTA_OPTION_TYPE_REMOVE:
        case GF_QUOTA_OPTION_TYPE_REMOVE_OBJECTS:
                get_gfid = _gf_true;
                break;

        case GF_QUOTA_OPTION_TYPE_SOFT_TIMEOUT:
        case GF_QUOTA_OPTION_TYPE_HARD_TIMEOUT:
        case GF_QUOTA_OPTION_TYPE_ALERT_TIME:
        case GF_QUOTA_OPTION_TYPE_DEFAULT_SOFT_LIMIT:
                ret = _glusterd_validate_quota_opts (dict, type, op_errstr);
                if (ret)
                        goto out;
                break;

        default:
                break;
        }

        if (get_gfid == _gf_true) {
                ret = glusterd_get_gfid_from_brick (dict, volinfo, rsp_dict,
                                                    op_errstr);
                if (ret)
                        goto out;
        }

        ret = 0;

 out:
        if (ret && op_errstr && *op_errstr)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_OP_STAGE_QUOTA_FAIL, "%s", *op_errstr);
        gf_msg_debug (this->name, 0, "Returning %d", ret);

         return ret;
}
