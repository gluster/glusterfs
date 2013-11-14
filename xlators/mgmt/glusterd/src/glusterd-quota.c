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
#include "syscall.h"
#include "byte-order.h"
#include "compat-errno.h"

#include <sys/wait.h>


const char *gd_quota_op_list[GF_QUOTA_OPTION_TYPE_DEFAULT_SOFT_LIMIT+1] = {
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
};

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

        if ((conf->op_version == GD_OP_VERSION_MIN) &&
            (type > GF_QUOTA_OPTION_TYPE_VERSION)) {
                snprintf (msg, sizeof (msg), "Cannot execute command. The "
                         "cluster is operating at version %d. Quota command %s "
                         "is unavailable in this version", conf->op_version,
                         gd_quota_op_list[type]);
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
                gf_log ("", GF_LOG_ERROR, "failed to get the quota status");
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

/* At the end of the function, the variable @found will be set
 * to true if the path to be removed was present in the limit-list,
 * else will be false.
 *
 * In addition, the function does the following things:
 *
 * a. places the path to be removed, if found, in @removed_path,
 * b. places the new limit list formed after removing @path's entry, in
 *    @new_list. If @path is not found, the input limit string @quota_limits is
 *    dup'd as is and placed in @new_list.
 */
int32_t
_glusterd_quota_remove_limits (char *quota_limits, char *path,
                               gf_boolean_t *found, char **new_list,
                               char **removed_path)
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
        char    *rp       = NULL;

        if (found != NULL)
                *found = _gf_false;

        if (quota_limits == NULL)
                return -1;

        qlimits = quota_limits;

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
                        if (removed_path) {
                                rp = GF_CALLOC (skiplen, sizeof (char), gf_gld_mt_char);
                                if (!rp) {
                                        ret = -1;
                                        goto out;
                                }
                                strncpy (rp, &qlimits[i], skiplen - 1);
                                *removed_path = rp;
                        }
                        memcpy ((void *) &limits [i], (void *) &qlimits [i + skiplen], size);
                        break;
                }

                i += size + 1;
                size = 0;
        }

        len = strlen (limits);
        if (len == 0)
                goto out;

        if (limits[len - 1] == ',') {
                limits[len - 1] = '\0';
                len --;
        }

        *new_list = GF_CALLOC (len + 1, sizeof (char), gf_gld_mt_char);
        if (!*new_list) {
                ret = -1;
                goto out;
        }

        memcpy ((void *) *new_list, (void *) limits, len + 1);
        ret = 0;
out:
        GF_FREE (limits);
        if (ret != -1)
                ret = flag ? 0 : 1;

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
			 "--use-readdirp=no",
                         "-l", DEFAULT_LOG_FILE_DIRECTORY"/quota-crawl.log",
                         mountdir, NULL);

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
                gf_log (this->name, GF_LOG_ERROR, "Failed to set default "
                        "soft-limit into dict");
                goto out;
        }
        ret = 0;

out:
        return ret;
}

int32_t
glusterd_quota_enable (glusterd_volinfo_t *volinfo, char **op_errstr,
                       gf_boolean_t *crawl)
{
        int32_t         ret     = -1;
        char            *quota_status = NULL;
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

        quota_status = gf_strdup ("on");
        if (!quota_status) {
                gf_log (this->name, GF_LOG_ERROR, "memory allocation failed");
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr (volinfo->dict, VKEY_FEATURES_QUOTA,
                               quota_status);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "dict set failed");
                goto out;
        }

        *crawl = _gf_true;

        ret = 0;
out:
        if (ret && op_errstr && !*op_errstr)
                gf_asprintf (op_errstr, "Enabling quota on volume %s has been "
                             "unsuccessful", volinfo->volname);
        return ret;
}

int32_t
glusterd_quota_disable (glusterd_volinfo_t *volinfo, char **op_errstr)
{
        int32_t    ret            = -1;
        int        i              =  0;
        char      *quota_status   = NULL;
        char      *value          = NULL;
        xlator_t  *this           = NULL;
        glusterd_conf_t *conf     = NULL;
        char *quota_options[]     = {"features.soft-timeout",
                                     "features.hard-timeout",
                                     "features.alert-time",
                                     "features.default-soft-limit", NULL};

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

        quota_status = gf_strdup ("off");
        if (!quota_status) {
                gf_log (this->name, GF_LOG_ERROR, "memory allocation failed");
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr (volinfo->dict, VKEY_FEATURES_QUOTA, quota_status);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "dict set failed");
                goto out;
        }

        for (i = 0; quota_options [i]; i++) {
                ret = glusterd_volinfo_get (volinfo, quota_options[i], &value);
                if (ret) {
                        gf_log (this->name, GF_LOG_INFO, "failed to get option"
                                                         " %s",
                                                         quota_options[i]);
                } else {
                dict_del (volinfo->dict, quota_options[i]);
                }
        }

        //Remove aux mount of the volume on every node in the cluster
        ret = glusterd_remove_auxiliary_mount (volinfo->volname);
        if (ret)
                goto out;

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
                          char *soft_limit, char **op_errstr)
{
        int               ret                = -1;
        xlator_t         *this               = NULL;
        char              abspath[PATH_MAX]  = {0,};
        glusterd_conf_t  *priv               = NULL;
        double           soft_lim            = 0;

        typedef struct quota_limits {
                int64_t hl;
                int64_t sl;
        } __attribute__ ((__packed__)) quota_limits_t;

	quota_limits_t existing_limit = {0,};
	quota_limits_t new_limit = {0,};

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        snprintf (abspath, sizeof (abspath)-1, "/tmp/%s%s", volname, path);

        ret = gf_lstat_dir (abspath, NULL);
        if (ret) {
                gf_asprintf (op_errstr, "Failed to find the directory %s. "
                             "Reason : %s", abspath, strerror (errno));
                goto out;
        }

        if (!soft_limit) {
                ret = sys_lgetxattr (abspath,
                                     "trusted.glusterfs.quota.limit-set",
                                     (void *)&existing_limit,
                                     sizeof (existing_limit));
                if (ret < 0) {
                        switch (errno) {
                        case ENOATTR:
                                existing_limit.sl = -1;
                            break;
                        default:
                                gf_asprintf (op_errstr, "Failed to get the xattr "
                                             "'trusted.glusterfs.quota.limit-set' from "
                                             "%s. Reason : %s", abspath,
                                             strerror (errno));
                                goto out;
                        }
                } else {
                        existing_limit.hl = ntoh64 (existing_limit.hl);
                        existing_limit.sl = ntoh64 (existing_limit.sl);
                }
                new_limit.sl = existing_limit.sl;

        } else {
                ret = gf_string2percent (soft_limit, &soft_lim);
                if (ret)
                        goto out;
                new_limit.sl = soft_lim;
        }

        new_limit.sl = hton64 (new_limit.sl);

        ret = gf_string2bytesize (hard_limit, (uint64_t*)&new_limit.hl);
        if (ret)
                goto out;

        new_limit.hl = hton64 (new_limit.hl);

        ret = sys_lsetxattr (abspath, "trusted.glusterfs.quota.limit-set",
                             (char *)(void *)&new_limit, sizeof (new_limit), 0);
        if (ret) {
                gf_asprintf (op_errstr, "setxattr of "
                             "'trusted.glusterfs.quota.limit-set' failed on %s."
                             " Reason : %s", abspath, strerror (errno));
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

static int
glusterd_store_quota_config (glusterd_volinfo_t *volinfo, char *path,
                             char *gfid_str, int opcode, char **op_errstr)
{
        int                ret                   = -1;
        int                count                 = 0;
        xlator_t          *this                  = NULL;
        glusterd_conf_t   *conf                  = NULL;
        unsigned char      buf[16]               = {0,};
        int                fd                    = -1;
        int                conf_fd               = -1;
        size_t             entry_sz              = 16;
        uuid_t             gfid                  = {0,};
        gf_boolean_t       found                 = _gf_false;
        gf_boolean_t       modified              = _gf_false;


        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        uuid_parse (gfid_str, gfid);

        glusterd_store_create_quota_conf_sh_on_absence (volinfo);

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


        ret = glusterd_store_quota_conf_skip_header (this, conf_fd);
        if (ret) {
                goto out;
        }

        ret = glusterd_store_quota_conf_stamp_header (this, fd);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to add header to tmp "
                        "file.");
                goto out;
        }
        //gfid is stored as 16 bytes of 'raw' data
        entry_sz = 16;
        for (;;) {
                ret = read (conf_fd, (void*)&buf, entry_sz) ;
                if (ret <= 0) {
                        //Finished reading all entries in the conf file
                        break;
                }
                if (ret != 16) {
                        //This should never happen. We must have a multiple of
                        //entry_sz bytes in our configuration file.
                        gf_log (this->name, GF_LOG_CRITICAL, "Quota "
                                "configuration store may be corrupt.");
                        ret = -1;
                        goto out;
                }
                count++;
                if (uuid_compare (gfid, buf)) {
                        /*If the gfids don't match, write @buf into tmp file. */
                        ret = write (fd, (void*) buf, entry_sz);
                        if (ret == -1) {
                                gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                        "write %s into quota configuration.",
                                        uuid_utoa (buf));
                                goto out;
                        }
                } else {
                        /*If a match is found, write @buf into tmp file for
                         * limit-usage only.
                         */
                        if (opcode == GF_QUOTA_OPTION_TYPE_LIMIT_USAGE) {
                                ret = write (fd, (void *) buf, entry_sz);
                                if (ret == -1) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "Failed to write %s into quota "
                                                "configuration.",
                                                uuid_utoa (buf));
                                        goto out;
                                }
                        }
                        found = _gf_true;
                }
        }

        switch (opcode) {
                case GF_QUOTA_OPTION_TYPE_LIMIT_USAGE:
                        /*
                         * count = 0 implies that the conf file is empty.
                         * In this case, we directly go ahead and write gfid_str
                         * into the tmp file.
                         * If count is non-zero and found is false, limit is
                         * being set on a gfid for the first time. So
                         * append gfid_str to the end of the file.
                         */
                        if ((count == 0) ||
                            ((count > 0) && (found == _gf_false))) {
                                memcpy (buf, gfid, 16);
                                ret = write (fd, (void *) buf, entry_sz);
                                if (ret == -1) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "Failed to write %s into quota "
                                                "configuration.",
                                                uuid_utoa (buf));
                                        goto out;
                                }
                                modified = _gf_true;
                        }

                        break;

                case GF_QUOTA_OPTION_TYPE_REMOVE:
                        /*
                         * count = 0 is not a valid scenario and must be treated
                         * as error.
                         * If count is non-zero and found is false, then it is
                         * an error.
                         * If count is non-zero and found is true, take no
                         * action, by virtue of which the gfid is as good as
                         * deleted from the store.
                         */
                        if (count == 0) {
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
                close (conf_fd);
        }

        if (fd != -1) {
                close (fd);
        }

        if (ret && (fd > 0)) {
                gf_store_unlink_tmppath (volinfo->quota_conf_shandle);
        } else if (!ret) {
                ret = gf_store_rename_tmppath (volinfo->quota_conf_shandle);
                if (modified) {
                        ret = glusterd_compute_cksum (volinfo, _gf_true);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                        "compute cksum for quota conf file");
                                goto out;
                        }

                        ret = glusterd_store_save_quota_version_and_cksum
                                                                      (volinfo);
                        if (ret) {
                                gf_log (this->name, GF_LOG_ERROR, "Failed to "
                                        "store quota version and cksum");
                                goto out;
                        }
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
                gf_log (this->name, GF_LOG_ERROR, "Unable to fetch path");
                goto out;
        }
        ret = gf_canonicalize_path (path);
        if (ret)
                goto out;

        ret = dict_get_str (dict, "hard-limit", &hard_limit);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to fetch hard limit");
                goto out;
        }

        if (dict_get (dict, "soft-limit")) {
                ret = dict_get_str (dict, "soft-limit", &soft_limit);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Unable to fetch "
                                "soft limit");
                        goto out;
                }
        }

        if (is_origin_glusterd ()) {
                ret = glusterd_set_quota_limit (volinfo->volname, path,
                                                hard_limit, soft_limit,
                                                op_errstr);
                if (ret)
                        goto out;
        }

        ret = dict_get_str (dict, "gfid", &gfid_str);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get gfid of path "
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
glusterd_remove_quota_limit (char *volname, char *path, char **op_errstr)
{
        int               ret                = -1;
        xlator_t         *this               = NULL;
        char              abspath[PATH_MAX]  = {0,};
        glusterd_conf_t  *priv               = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        snprintf (abspath, sizeof (abspath)-1, "/tmp/%s%s", volname, path);

        ret = gf_lstat_dir (abspath, NULL);
        if (ret) {
                gf_asprintf (op_errstr, "Failed to find the directory %s. "
                             "Reason : %s", abspath, strerror (errno));
                goto out;
        }

        ret = sys_lremovexattr (abspath, "trusted.glusterfs.quota.limit-set");
        if (ret) {
                gf_asprintf (op_errstr, "removexattr failed on %s. Reason : %s",
                             abspath, strerror (errno));
                goto out;
        }
        ret = 0;

out:
        return ret;
}

int32_t
glusterd_quota_remove_limits (glusterd_volinfo_t *volinfo, dict_t *dict,
                              int opcode, char **op_errstr)
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
                gf_log (this->name, GF_LOG_ERROR, "Unable to fetch path");
                goto out;
        }

        ret = gf_canonicalize_path (path);
        if (ret)
                goto out;

        if (is_origin_glusterd ()) {
                ret = glusterd_remove_quota_limit (volinfo->volname, path,
                                                   op_errstr);
                if (ret)
                        goto out;
        }

        ret = dict_get_str (dict, "gfid", &gfid_str);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to get gfid of path "
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
                gf_log (this->name, GF_LOG_ERROR, "Option value absent.");
                return -1;
        }

        option = gf_strdup (value);
        ret = dict_set_dynstr (volinfo->dict, key, option);
        if(ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set option %s",
                        key);
                return -1;
        }

        return 0;
}

static int
glusterd_quotad_op (int opcode)
{
        int ret = -1;

        switch (opcode) {
                case GF_QUOTA_OPTION_TYPE_ENABLE:
                case GF_QUOTA_OPTION_TYPE_DISABLE:

                        if (glusterd_all_volumes_with_quota_stopped ())
                                ret = glusterd_quotad_stop ();
                        else
                                ret = glusterd_check_generate_start_quotad ();
                        break;

                case GF_QUOTA_OPTION_TYPE_DEFAULT_SOFT_LIMIT:
                case GF_QUOTA_OPTION_TYPE_HARD_TIMEOUT:
                case GF_QUOTA_OPTION_TYPE_SOFT_TIMEOUT:
                case GF_QUOTA_OPTION_TYPE_ALERT_TIME:

                        ret = glusterd_reconfigure_quotad ();
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
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume name");
                goto out;
        }

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_asprintf (op_errstr, FMTSTR_CHECK_VOL_EXISTS, volname);
                goto out;
        }

        ret = dict_get_int32 (dict, "type", &type);

        if ((priv->op_version == GD_OP_VERSION_MIN) &&
            (type > GF_QUOTA_OPTION_TYPE_VERSION)) {
                gf_asprintf (op_errstr, "Volume quota failed. The cluster is "
                                        "operating at version %d. Quota command"
                                        " %s is unavailable in this version.",
                                        priv->op_version,
                                        gd_quota_op_list[type]);
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

                case GF_QUOTA_OPTION_TYPE_DISABLE:
                        ret = glusterd_quota_disable (volinfo, op_errstr);
                        if (ret < 0)
                                goto out;

                        break;

                case GF_QUOTA_OPTION_TYPE_LIMIT_USAGE:
                        ret = glusterd_quota_limit_usage (volinfo, dict, type,
                                                          op_errstr);
                        goto out;

                case GF_QUOTA_OPTION_TYPE_REMOVE:
                        ret = glusterd_quota_remove_limits (volinfo, dict, type,
                                                            op_errstr);
                        goto out;

                case GF_QUOTA_OPTION_TYPE_LIST:
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

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to re-create "
                                                  "volfiles");
                ret = -1;
                goto out;
        }

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                if (priv->op_version == GD_OP_VERSION_MIN)
                        ret = glusterd_check_generate_start_nfs ();
        }

        if (rsp_dict && start_crawl == _gf_true)
                glusterd_quota_initiate_fs_crawl (priv, volname);

        if (priv->op_version > GD_OP_VERSION_MIN) {
                ret = glusterd_quotad_op (type);
                if (ret)
                        goto out;
        }
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
                gf_log (this->name, GF_LOG_ERROR, "Failed to get path");
                goto out;
        }

        list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                ret = glusterd_resolve_brick (brickinfo);
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, FMTSTR_RESOLVE_BRICK,
                                brickinfo->hostname, brickinfo->path);
                        goto out;
                }

                if (uuid_compare (brickinfo->uuid, MY_UUID))
                        continue;

                if (brickinfo->vg[0])
                        continue;

                snprintf (backend_path, sizeof (backend_path), "%s%s",
                          brickinfo->path, path);

                ret = gf_lstat_dir (backend_path, NULL);
                if (ret) {
                        gf_log (this->name, GF_LOG_INFO, "Failed to find "
                                "directory %s. Reason : %s", backend_path,
                                strerror (errno));
                        ret = 0;
                        continue;
                }
                ret = sys_lgetxattr (backend_path, GFID_XATTR_KEY, gfid, 16);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_INFO, "Failed to get "
                                "extended attribute %s for directory %s. "
                                "Reason : %s", GFID_XATTR_KEY, backend_path,
                                     strerror (errno));
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
                        gf_log (this->name, GF_LOG_ERROR, "Failed to place "
                                "gfid of %s in dict", backend_path);
                        GF_FREE (gfid_str);
                        goto out;
                }
                count++;
        }

        ret = dict_set_int32 (rsp_dict, "count", count);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Failed to set count");
                goto out;
        }

        ret = 0;
out:
        return ret;
}

int
glusterd_op_stage_quota (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        int                 ret           = 0;
        int                 type          = 0;
        int                 i             = 0;
        char               *volname       = NULL;
        char               *value         = NULL;
        gf_boolean_t        exists        = _gf_false;
        dict_t             *ctx           = NULL;
        dict_t             *tmp_dict      = NULL;
        xlator_t           *this          = NULL;
        glusterd_conf_t    *priv          = NULL;
        glusterd_volinfo_t *volinfo       = NULL;

        struct {
                int opcode;
                char *key;
        } optable[] = {
                {GF_QUOTA_OPTION_TYPE_ALERT_TIME,
                                                "features.alert-time"},
                {GF_QUOTA_OPTION_TYPE_SOFT_TIMEOUT, "features.soft-timeout"},
                {GF_QUOTA_OPTION_TYPE_HARD_TIMEOUT, "features.hard-timeout"},
                {GF_QUOTA_OPTION_TYPE_NONE, NULL}
        };

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        tmp_dict = dict_new ();
        if (!tmp_dict)
                goto out;

        ret = dict_get_str (dict, "volname", &volname);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Unable to get volume name");
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

        if ((priv->op_version == GD_OP_VERSION_MIN) &&
            (type > GF_QUOTA_OPTION_TYPE_VERSION)) {
                gf_asprintf (op_errstr, "Volume quota failed. The cluster is "
                                        "operating at version %d. Quota command"
                                        " %s is unavailable in this version.",
                                         priv->op_version,
                                         gd_quota_op_list[type]);
                ret = -1;
                goto out;
        }

         ctx = glusterd_op_get_ctx();
         if (ctx && (type == GF_QUOTA_OPTION_TYPE_ENABLE
                     || type == GF_QUOTA_OPTION_TYPE_LIST)) {
                 /* Fuse mount req. only for enable & list-usage options*/
                 if (!glusterd_is_fuse_available ()) {
                         *op_errstr = gf_strdup ("Fuse unavailable");
                         ret = -1;
                         goto out;
                 }
         }

        switch (type) {
                case GF_QUOTA_OPTION_TYPE_LIMIT_USAGE:
                case GF_QUOTA_OPTION_TYPE_REMOVE:
                        ret = glusterd_get_gfid_from_brick (dict, volinfo,
                                                            rsp_dict,
                                                            op_errstr);
                        if (ret)
                                goto out;
                        break;

                case GF_QUOTA_OPTION_TYPE_ALERT_TIME:
                case GF_QUOTA_OPTION_TYPE_SOFT_TIMEOUT:
                case GF_QUOTA_OPTION_TYPE_HARD_TIMEOUT:
                        ret = dict_get_str (dict, "value", &value);
                        if (ret)
                                goto out;

                        for (i = 0; optable[i].key; i++) {
                                if (type == optable[i].opcode)
                                        break;
                        }
                        ret = dict_set_str (tmp_dict, optable[i].key, value);
                        if (ret)
                                goto out;

                        ret = glusterd_validate_reconfopts (volinfo, tmp_dict,
                                                                    op_errstr);
                        if (ret)
                                goto out;
                        break;

                default:
                        ret = 0;
        }

        ret = 0;

 out:
        if (tmp_dict)
                dict_unref (tmp_dict);
        if (ret && op_errstr && *op_errstr)
                gf_log (this->name, GF_LOG_ERROR, "%s", *op_errstr);
        gf_log (this->name, GF_LOG_DEBUG, "Returning %d", ret);

         return ret;
}
