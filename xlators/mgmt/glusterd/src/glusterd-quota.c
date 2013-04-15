/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
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
#include "cli1-xdr.h"
#include "xdr-generic.h"
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "run.h"

#include <sys/wait.h>

int
__glusterd_handle_quota (rpcsvc_request_t *req)
{
        int32_t                         ret = -1;
        gf_cli_req                      cli_req = {{0,}};
        dict_t                         *dict = NULL;
        glusterd_op_t                   cli_op = GD_OP_QUOTA;
        char                            operation[256] = {0, };
        char                           *volname = NULL;
        int32_t                         type = 0;
        char                            msg[2048] = {0,};
        xlator_t                       *this = NULL;

        GF_ASSERT (req);
        this = THIS;
        GF_ASSERT (this);

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
                        gf_log (this->name, GF_LOG_ERROR, "failed to "
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
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume name, "
                        "while handling quota command");
                goto out;
        }

        ret = dict_get_int32 (dict, "type", &type);
        if (ret) {
                snprintf (msg, sizeof (msg), "Unable to get type of command");
                gf_log (this->name, GF_LOG_ERROR, "Unable to get type of cmd, "
                        "while handling quota command");
               goto out;
        }

        switch (type) {
        case GF_QUOTA_OPTION_TYPE_ENABLE:
                strncpy (operation, "enable", sizeof (operation));
                break;

        case GF_QUOTA_OPTION_TYPE_DISABLE:
                strncpy (operation, "disable", sizeof (operation));
                break;

        case GF_QUOTA_OPTION_TYPE_LIMIT_USAGE:
                strncpy (operation, "limit-usage", sizeof (operation));
                break;

        case GF_QUOTA_OPTION_TYPE_REMOVE:
                strncpy (operation, "remove", sizeof (operation));
                break;
        }
        ret = glusterd_op_begin (req, GD_OP_QUOTA, dict, msg, sizeof (msg));

out:
        glusterd_friend_sm ();
        glusterd_op_sm ();

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
                gf_log ("", GF_LOG_ERROR, "failed to get the quota status");
                ret = -1;
                goto out;
        }

        if (flag == _gf_false) {
                gf_log ("", GF_LOG_ERROR, "first enable the quota translator");
                ret = -1;
                goto out;
        }
        ret = 0;
out:
        return ret;
}

/* At the end of the function, the variable found will be set
 * to true if the path to be removed was present in the limit-list,
 * else will be false.
 */
int32_t
_glusterd_quota_remove_limits (char **quota_limits, char *path,
                               gf_boolean_t *found)
{
        int      ret      = 0;
        int      i        = 0;
        int      size     = 0;
        int      len      = 0;
        int      pathlen  = 0;
        int      skiplen  = 0;
        int      flag     = 0;
        char    *limits   = NULL;
        char    *qlimits  = NULL;

        if (found != NULL)
                *found = _gf_false;

        if (*quota_limits == NULL)
                return -1;

        qlimits = *quota_limits;

        pathlen = strlen (path);

        len = strlen (qlimits);

        limits = GF_CALLOC (len + 1, sizeof (char), gf_gld_mt_char);
        if (!limits)
                return -1;

        while (i < len) {
                if (!memcmp ((void *) &qlimits [i], (void *)path, pathlen))
                        if (qlimits [i + pathlen] == ':') {
                                flag = 1;
                                if (found != NULL)
                                        *found = _gf_true;
                        }

                while (qlimits [i + size] != ',' &&
                       qlimits [i + size] != '\0')
                        size++;

                if (!flag) {
                        memcpy ((void *) &limits [i], (void *) &qlimits [i], size + 1);
                } else {
                        skiplen = size + 1;
                        size = len - i - size;
                        memcpy ((void *) &limits [i], (void *) &qlimits [i + skiplen], size);
                        break;
                }

                i += size + 1;
                size = 0;
        }

        if (!flag) {
                ret = 1;
        } else {
                len = strlen (limits);

                if (len == 0) {
                        GF_FREE (qlimits);

                        *quota_limits = NULL;

                        goto out;
                }

                if (limits[len - 1] == ',') {
                        limits[len - 1] = '\0';
                        len --;
                }

                GF_FREE (qlimits);

                qlimits = GF_CALLOC (len + 1, sizeof (char), gf_gld_mt_char);

                if (!qlimits) {
                        ret = -1;
                        goto out;
                }

                memcpy ((void *) qlimits, (void *) limits, len + 1);

                *quota_limits = qlimits;

                ret = 0;
        }

out:
        GF_FREE (limits);

        return ret;
}

int32_t
glusterd_quota_initiate_fs_crawl (glusterd_conf_t *priv, char *volname)
{
        pid_t                      pid;
        int32_t                    ret              = 0;
        int                        status           = 0;
        char                       mountdir[]       = "/tmp/mntXXXXXX";
        runner_t                   runner           = {0};

        if (mkdtemp (mountdir) == NULL) {
                gf_log ("glusterd", GF_LOG_DEBUG,
                        "failed to create a temporary mount directory");
                ret = -1;
                goto out;
        }

        runinit (&runner);
        runner_add_args (&runner, SBIN_DIR"/glusterfs",
                         "-s", "localhost",
                         "--volfile-id", volname,
                         "-l", DEFAULT_LOG_FILE_DIRECTORY"/quota-crawl.log",
                         mountdir, NULL);

        ret = runner_run_nowait (&runner);
        if (ret == -1) {
                gf_log ("glusterd", GF_LOG_ERROR, "Failed to start fs-crawl");
                goto out;
        }

        if ((pid = fork ()) < 0) {
                gf_log ("glusterd", GF_LOG_WARNING, "fork from parent failed");
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
                        gf_log ("glusterd", GF_LOG_WARNING, "chdir %s failed, "
                                "reason: %s", mountdir, strerror (errno));
                        exit (EXIT_FAILURE);
                }
                runinit (&runner);
                runner_add_args (&runner, "/usr/bin/find", "find", ".", NULL);
                if (runner_start (&runner) == -1)
                        _exit (EXIT_FAILURE);

#ifndef GF_LINUX_HOST_OS
                runner_end (&runner); /* blocks in waitpid */
                runcmd ("umount", mountdir, NULL);
#else
                runcmd ("umount", "-l", mountdir, NULL);
#endif
                rmdir (mountdir);
                _exit (EXIT_SUCCESS);
        }
        ret = (waitpid (pid, &status, 0) == pid &&
               WIFEXITED (status) && WEXITSTATUS (status) == EXIT_SUCCESS) ? 0 : -1;

out:
        return ret;
}

char *
glusterd_quota_get_limit_value (char *quota_limits, char *path)
{
        int32_t i, j, k, l, len;
        int32_t pat_len, diff;
        char   *ret_str = NULL;

        len = strlen (quota_limits);
        pat_len = strlen (path);
        i = 0;
        j = 0;

        while (i < len) {
                j = i;
                k = 0;
                while (path [k] == quota_limits [j]) {
                        j++;
                        k++;
                }

                l = j;

                while (quota_limits [j] != ',' &&
                       quota_limits [j] != '\0')
                        j++;

                if (quota_limits [l] == ':' && pat_len == (l - i)) {
                        diff = j - i;
                        ret_str = GF_CALLOC (diff + 1, sizeof (char),
                                             gf_gld_mt_char);

                        strncpy (ret_str, &quota_limits [i], diff);

                        break;
                }
                i = ++j; //skip ','
        }

        return ret_str;
}

char*
_glusterd_quota_get_limit_usages (glusterd_volinfo_t *volinfo,
                                  char *path, char **op_errstr)
{
        int32_t  ret          = 0;
        char    *quota_limits = NULL;
        char    *ret_str      = NULL;

        if (volinfo == NULL)
                return NULL;

        ret = glusterd_volinfo_get (volinfo, VKEY_FEATURES_LIMIT_USAGE,
                                    &quota_limits);
        if (ret)
                return NULL;
        if (quota_limits == NULL) {
                ret_str = NULL;
                *op_errstr = gf_strdup ("Limit not set on any directory");
        } else if (path == NULL)
                ret_str = gf_strdup (quota_limits);
        else
                ret_str = glusterd_quota_get_limit_value (quota_limits, path);

        return ret_str;
}

int32_t
glusterd_quota_get_limit_usages (glusterd_conf_t *priv,
                                 glusterd_volinfo_t *volinfo,
                                 char *volname,
                                 dict_t *dict,
                                 char **op_errstr)
{
        int32_t i               = 0;
        int32_t ret             = 0;
        int32_t count           = 0;
        char    *path           = NULL;
        dict_t  *ctx            = NULL;
        char    cmd_str [1024]  = {0, };
        char   *ret_str         = NULL;

        ctx = glusterd_op_get_ctx ();
        if (ctx == NULL)
                return 0;

        ret = dict_get_int32 (dict, "count", &count);
        if (ret < 0)
                goto out;

        if (count == 0) {
                ret_str = _glusterd_quota_get_limit_usages (volinfo, NULL,
                                                            op_errstr);
        } else {
                i = 0;
                while (count--) {
                        snprintf (cmd_str, 1024, "path%d", i++);

                        ret = dict_get_str (dict, cmd_str, &path);
                        if (ret < 0)
                                goto out;

                        ret_str = _glusterd_quota_get_limit_usages (volinfo, path, op_errstr);
                }
        }

        if (ret_str) {
                ret = dict_set_dynstr (ctx, "limit_list", ret_str);
        }
out:
        return ret;
}

int32_t
glusterd_quota_enable (glusterd_volinfo_t *volinfo, char **op_errstr,
                       gf_boolean_t *crawl)
{
        int32_t         ret     = -1;
        char            *quota_status = NULL;

        GF_VALIDATE_OR_GOTO ("glusterd", volinfo, out);
        GF_VALIDATE_OR_GOTO ("glusterd", crawl, out);
        GF_VALIDATE_OR_GOTO ("glusterd", op_errstr, out);

        if (glusterd_is_volume_started (volinfo) == 0) {
                *op_errstr = gf_strdup ("Volume is stopped, start volume "
                                        "to enable quota.");
                goto out;
        }

        ret = glusterd_check_if_quota_trans_enabled (volinfo);
        if (ret == 0) {
                *op_errstr = gf_strdup ("Quota is already enabled");
                goto out;
        }

        quota_status = gf_strdup ("on");
        if (!quota_status) {
                gf_log ("", GF_LOG_ERROR, "memory allocation failed");
                *op_errstr = gf_strdup ("Enabling quota has been unsuccessful");
                goto out;
        }

        ret = dict_set_dynstr (volinfo->dict, VKEY_FEATURES_QUOTA, quota_status);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "dict set failed");
                *op_errstr = gf_strdup ("Enabling quota has been unsuccessful");
                goto out;
        }

        *op_errstr = gf_strdup ("Enabling quota has been successful");

        *crawl = _gf_true;

        ret = 0;
out:
        return ret;
}

int32_t
glusterd_quota_disable (glusterd_volinfo_t *volinfo, char **op_errstr)
{
        int32_t  ret            = -1;
        char    *quota_status   = NULL, *quota_limits = NULL;

        GF_VALIDATE_OR_GOTO ("glusterd", volinfo, out);
        GF_VALIDATE_OR_GOTO ("glusterd", op_errstr, out);

        ret = glusterd_check_if_quota_trans_enabled (volinfo);
        if (ret == -1) {
                *op_errstr = gf_strdup ("Quota is already disabled");
                goto out;
        }

        quota_status = gf_strdup ("off");
        if (!quota_status) {
                gf_log ("", GF_LOG_ERROR, "memory allocation failed");
                *op_errstr = gf_strdup ("Disabling quota has been unsuccessful");
                goto out;
        }

        ret = dict_set_dynstr (volinfo->dict, VKEY_FEATURES_QUOTA, quota_status);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "dict set failed");
                *op_errstr = gf_strdup ("Disabling quota has been unsuccessful");
                goto out;
        }

        *op_errstr = gf_strdup ("Disabling quota has been successful");

        ret = glusterd_volinfo_get (volinfo, VKEY_FEATURES_LIMIT_USAGE,
                                    &quota_limits);
        if (ret) {
                gf_log ("", GF_LOG_WARNING, "failed to get the quota limits");
        } else {
                GF_FREE (quota_limits);
        }

        dict_del (volinfo->dict, VKEY_FEATURES_LIMIT_USAGE);

out:
        return ret;
}

int32_t
glusterd_quota_limit_usage (glusterd_volinfo_t *volinfo, dict_t *dict, char **op_errstr)
{
        int32_t          ret    = -1;
        char            *path   = NULL;
        char            *limit  = NULL;
        char            *value  = NULL;
        char             msg [1024] = {0,};
        char            *quota_limits = NULL;

        GF_VALIDATE_OR_GOTO ("glusterd", dict, out);
        GF_VALIDATE_OR_GOTO ("glusterd", volinfo, out);
        GF_VALIDATE_OR_GOTO ("glusterd", op_errstr, out);

        ret = glusterd_check_if_quota_trans_enabled (volinfo);
        if (ret == -1) {
                *op_errstr = gf_strdup ("Quota is disabled, please enable "
                                        "quota");
                goto out;
        }

        ret = glusterd_volinfo_get (volinfo, VKEY_FEATURES_LIMIT_USAGE,
                                    &quota_limits);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "failed to get the quota limits");
                *op_errstr = gf_strdup ("failed to set limit");
                goto out;
        }

        ret = dict_get_str (dict, "path", &path);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to fetch quota limits" );
                *op_errstr = gf_strdup ("failed to set limit");
                goto out;
        }

        ret = dict_get_str (dict, "limit", &limit);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to fetch quota limits" );
                *op_errstr = gf_strdup ("failed to set limit");
                goto out;
        }

        if (quota_limits) {
                ret = _glusterd_quota_remove_limits (&quota_limits, path, NULL);
                if (ret == -1) {
                        gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                        *op_errstr = gf_strdup ("failed to set limit");
                        goto out;
                }
        }

        if (quota_limits == NULL) {
                ret = gf_asprintf (&value, "%s:%s", path, limit);
                if (ret == -1) {
                        gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                        *op_errstr = gf_strdup ("failed to set limit");
                        goto out;
                }
        } else {
                ret = gf_asprintf (&value, "%s,%s:%s",
                                   quota_limits, path, limit);
                if (ret == -1) {
                        gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                        *op_errstr = gf_strdup ("failed to set limit");
                        goto out;
                }

                GF_FREE (quota_limits);
        }

        quota_limits = value;

        ret = dict_set_str (volinfo->dict, VKEY_FEATURES_LIMIT_USAGE,
                            quota_limits);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set quota limits" );
                *op_errstr = gf_strdup ("failed to set limit");
                goto out;
        }
        snprintf (msg, 1024, "limit set on %s", path);
        *op_errstr = gf_strdup (msg);

        ret = 0;
out:
        return ret;
}

int32_t
glusterd_quota_remove_limits (glusterd_volinfo_t *volinfo, dict_t *dict, char **op_errstr)
{
        int32_t         ret                   = -1;
        char            str [PATH_MAX + 1024] = {0,};
        char            *quota_limits         = NULL;
        char            *path                 = NULL;
        gf_boolean_t     flag                 = _gf_false;

        GF_VALIDATE_OR_GOTO ("glusterd", dict, out);
        GF_VALIDATE_OR_GOTO ("glusterd", volinfo, out);
        GF_VALIDATE_OR_GOTO ("glusterd", op_errstr, out);

        ret = glusterd_check_if_quota_trans_enabled (volinfo);
        if (ret == -1) {
                *op_errstr = gf_strdup ("Quota is disabled, please enable quota");
                goto out;
        }

        ret = glusterd_volinfo_get (volinfo, VKEY_FEATURES_LIMIT_USAGE,
                                    &quota_limits);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "failed to get the quota limits");
                goto out;
        }

        ret = dict_get_str (dict, "path", &path);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to fetch quota limits" );
                goto out;
        }

        ret = _glusterd_quota_remove_limits (&quota_limits, path, &flag);
        if (ret == -1) {
                if (flag == _gf_true)
                        snprintf (str, sizeof (str), "Removing limit on %s has "
                                  "been unsuccessful", path);
                else
                        snprintf (str, sizeof (str), "%s has no limit set", path);
                *op_errstr = gf_strdup (str);
                goto out;
        } else {
                if (flag == _gf_true)
                        snprintf (str, sizeof (str), "Removed quota limit on "
                                  "%s", path);
                else
                        snprintf (str, sizeof (str), "no limit set on %s",
                                  path);
                *op_errstr = gf_strdup (str);
        }

        if (quota_limits) {
                ret = dict_set_str (volinfo->dict, VKEY_FEATURES_LIMIT_USAGE,
                                    quota_limits);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Unable to set quota limits" );
                        goto out;
                }
        } else {
               dict_del (volinfo->dict, VKEY_FEATURES_LIMIT_USAGE);
        }

        ret = 0;

out:
        return ret;
}


int
glusterd_op_quota (dict_t *dict, char **op_errstr)
{
        glusterd_volinfo_t     *volinfo      = NULL;
        int32_t                 ret          = -1;
        char                   *volname      = NULL;
        dict_t                 *ctx          = NULL;
        int                     type         = -1;
        gf_boolean_t            start_crawl  = _gf_false;
        glusterd_conf_t        *priv         = NULL;

        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        priv = THIS->private;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name " );
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                goto out;
        }

        ret = dict_get_int32 (dict, "type", &type);

        if (type == GF_QUOTA_OPTION_TYPE_ENABLE) {
                ret = glusterd_quota_enable (volinfo, op_errstr, &start_crawl);
                if (ret < 0)
                        goto out;

                goto create_vol;
        }

        if (type == GF_QUOTA_OPTION_TYPE_DISABLE) {
                ret = glusterd_quota_disable (volinfo, op_errstr);
                if (ret < 0)
                        goto out;

                goto create_vol;
        }

        if (type == GF_QUOTA_OPTION_TYPE_LIMIT_USAGE) {
                ret = glusterd_quota_limit_usage (volinfo, dict, op_errstr);
                if (ret < 0)
                        goto out;

                goto create_vol;
        }

        if (type == GF_QUOTA_OPTION_TYPE_REMOVE) {
                ret = glusterd_quota_remove_limits (volinfo, dict, op_errstr);
                if (ret < 0)
                        goto out;

                goto create_vol;
        }

        if (type == GF_QUOTA_OPTION_TYPE_LIST) {
                ret = glusterd_check_if_quota_trans_enabled (volinfo);
                if (ret == -1) {
                        *op_errstr = gf_strdup ("cannot list the limits, "
                                                "quota is disabled");
                        goto out;
                }

                ret = glusterd_quota_get_limit_usages (priv, volinfo, volname,
                                                       dict, op_errstr);

                goto out;
        }
create_vol:
        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to re-create volfile for"
                                          " 'quota'");
                ret = -1;
                goto out;
        }

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status)
                ret = glusterd_check_generate_start_nfs ();

        ret = 0;

out:
        ctx = glusterd_op_get_ctx ();
        if (ctx && start_crawl == _gf_true)
                glusterd_quota_initiate_fs_crawl (priv, volname);

        if (ctx && *op_errstr) {
                ret = dict_set_dynstr (ctx, "errstr", *op_errstr);
                if (ret) {
                        GF_FREE (*op_errstr);
                        gf_log ("", GF_LOG_DEBUG,
                                "failed to set error message in ctx");
                }
                *op_errstr = NULL;
        }

        return ret;
}

int
glusterd_op_stage_quota (dict_t *dict, char **op_errstr)
{
        int                ret           = 0;
        char              *volname       = NULL;
        gf_boolean_t       exists        = _gf_false;
        int                type          = 0;
        dict_t            *ctx           = NULL;

        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        if (!exists) {
                gf_log ("", GF_LOG_ERROR, "Volume with name: %s "
                                "does not exist",
                                volname);
                *op_errstr = gf_strdup ("Invalid volume name");
                ret = -1;
                goto out;
        }

        ret = dict_get_int32 (dict, "type", &type);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get 'type' for quota op");
                *op_errstr = gf_strdup ("Volume quota failed, internal error "
                                        ", unable to get type of operation");
                goto out;
        }


        ctx = glusterd_op_get_ctx();
        if (ctx && (type == GF_QUOTA_OPTION_TYPE_ENABLE
                    || type == GF_QUOTA_OPTION_TYPE_LIST)) {
                /* Fuse mount req. only for enable & list-usage options*/
                if (!glusterd_is_fuse_available ()) {
                        gf_log ("glusterd", GF_LOG_ERROR, "Unable to open /dev/"
                                "fuse (%s), quota command failed",
                                strerror (errno));
                        *op_errstr = gf_strdup ("Fuse unavailable");
                        ret = -1;
                        goto out;
                }
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);

        return ret;
}
