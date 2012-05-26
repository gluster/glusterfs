/*
  Copyright (c) 2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
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

#include <signal.h>

static char *gsync_reserved_opts[] = {
        "gluster-command-dir",
        "pid-file",
        "state-file",
        "session-owner",
        NULL
};

int
glusterd_handle_gsync_set (rpcsvc_request_t *req)
{
        int32_t                 ret     = 0;
        dict_t                  *dict   = NULL;
        gf_cli_req              cli_req = {{0},};
        glusterd_op_t           cli_op = GD_OP_GSYNC_SET;
        char                    *master = NULL;
        char                    *slave = NULL;
        char                    operation[256] = {0,};
        int                     type = 0;
        glusterd_conf_t         *priv   = NULL;
        char                    *host_uuid = NULL;

        GF_ASSERT (req);
        GF_ASSERT (THIS);
        GF_ASSERT (THIS->private);

        priv = THIS->private;

        if (!xdr_to_generic (req->msg[0], &cli_req,
                             (xdrproc_t)xdr_gf_cli_req)) {
                req->rpc_err = GARBAGE_ARGS;
                goto out;
        }

        if (cli_req.dict.dict_len) {
                dict = dict_new ();
                if (!dict)
                        goto out;

                ret = dict_unserialize (cli_req.dict.dict_val,
                                        cli_req.dict.dict_len,
                                        &dict);
                if (ret < 0) {
                        gf_log ("glusterd", GF_LOG_ERROR, "failed to "
                                "unserialize req-buffer to dictionary");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }

                host_uuid = gf_strdup (uuid_utoa(priv->uuid));
                if (host_uuid == NULL) {
                        gf_log ("glusterd", GF_LOG_ERROR, "failed to get"
                                "the uuid of the host machine");
                        ret = -1;
                        goto out;
                }
                ret = dict_set_dynstr (dict, "host-uuid", host_uuid);
                if (ret)
                        goto out;

        }

        ret = dict_get_str (dict, "master", &master);
        if (ret < 0) {
                gf_log ("", GF_LOG_INFO, "master not found, while handling"
                         GEOREP" options");
                master = "(No Master)";
        }

        ret = dict_get_str (dict, "slave", &slave);
        if (ret < 0) {
                gf_log ("", GF_LOG_INFO, "slave not not found, while"
                        "handling "GEOREP" options");
                slave = "(No Slave)";
        }

        ret = dict_get_int32 (dict, "type", &type);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "command type not found, while"
                        "handling "GEOREP" options");
                goto out;
        }

        switch (type) {

        case GF_GSYNC_OPTION_TYPE_START:
                strncpy (operation, "start", sizeof (operation));
                break;

        case GF_GSYNC_OPTION_TYPE_STOP:
                strncpy (operation, "stop", sizeof (operation));
                break;

        case GF_GSYNC_OPTION_TYPE_CONFIG:
                strncpy (operation, "config", sizeof (operation));
                break;

        case GF_GSYNC_OPTION_TYPE_STATUS:
                strncpy (operation, "status", sizeof (operation));
                break;
        case GF_GSYNC_OPTION_TYPE_ROTATE:
                strncpy (operation, "rotate", sizeof(operation));
                break;
        }

        gf_cmd_log ("volume "GEOREP, " %s command on %s,%s", operation, master,
                    slave);
        ret = glusterd_op_begin (req, GD_OP_GSYNC_SET, dict);
        gf_cmd_log ("volume "GEOREP, " %s command on %s,%s %s  ", operation,
                    master, slave, (ret != 0)? "FAILED" : "SUCCEEDED");

out:
        glusterd_friend_sm ();
        glusterd_op_sm ();

        if (ret) {
                if (dict)
                        dict_unref (dict);
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     NULL, "operation failed");
        }
        return ret;
}


/*****
 *
 * glusterd_urltransform* internal API
 *
 *****/

static void
glusterd_urltransform_init (runner_t *runner, const char *transname)
{
        runinit (runner);
        runner_add_arg (runner, GSYNCD_PREFIX"/gsyncd");
        runner_argprintf (runner, "--%s-url", transname);
}

static void
glusterd_urltransform_add (runner_t *runner, const char *url)
{
        runner_add_arg (runner, url);
}

static void
_glusterd_urltransform_add_iter (dict_t *dict, char *key, data_t *value, void *data)
{
        runner_t *runner = (runner_t *)data;
        char *slave = NULL;

        slave = strchr (value->data, ':');
        GF_ASSERT (slave);
        slave++;
        runner_add_arg (runner, slave);
}

static void
glusterd_urltransform_free (char **linearr, unsigned n)
{
        int i = 0;

        for (; i < n; i++)
                GF_FREE (linearr[i]);

        GF_FREE (linearr);
}

static int
glusterd_urltransform (runner_t *runner, char ***linearrp)
{
        char **linearr = NULL;
        char *line = NULL;
        unsigned arr_len = 32;
        unsigned arr_idx = 0;
        gf_boolean_t error = _gf_false;

        linearr = GF_CALLOC (arr_len, sizeof (char *), gf_gld_mt_linearr);
        if (!linearr) {
                error = _gf_true;
                goto out;
        }

        runner_redir (runner, STDOUT_FILENO, RUN_PIPE);
        if (runner_start (runner) != 0) {
                gf_log ("", GF_LOG_ERROR, "spawning child failed");

                error = _gf_true;
                goto out;
        }

        arr_idx = 0;
        for (;;) {
                line = GF_MALLOC (1024, gf_gld_mt_linebuf);
                if (!line) {
                        error = _gf_true;
                        goto out;
                }

                if (fgets (line, 1024, runner_chio (runner, STDOUT_FILENO)) ==
                    NULL)
                        break;

                if (line[strlen (line) - 1] != '\n') {
                        GF_FREE (line);
                        error = _gf_true;
                        goto out;
                }
                line[strlen (line) - 1] = '\0';

                if (arr_idx == arr_len) {
                        arr_len <<= 1;
                        linearr = GF_REALLOC (linearr, arr_len);
                        if (!linearr) {
                                GF_FREE (line);
                                error = _gf_true;
                                goto out;
                        }
                }
                linearr[arr_idx] = line;

                arr_idx++;
        }

 out:

        /* XXX chpid field is not exported by run API
         * but runner_end() does not abort the invoked
         * process (ie. it might block in waitpid(2))
         * so we resort to a manual kill a the private field
         */
        if (error && runner->chpid > 0)
                kill (runner->chpid, SIGKILL);

        if (runner_end (runner) != 0)
                error = _gf_true;

        if (error) {
                gf_log ("", GF_LOG_ERROR, "reading data from child failed");
                glusterd_urltransform_free (linearr, arr_idx);
                return -1;
        }

        *linearrp = linearr;
        return arr_idx;
}

static int
glusterd_urltransform_single (const char *url, const char *transname,
                              char ***linearrp)
{
        runner_t runner = {0,};

        glusterd_urltransform_init (&runner, transname);
        glusterd_urltransform_add (&runner, url);
        return glusterd_urltransform (&runner, linearrp);
}


struct dictidxmark {
        unsigned isrch;
        unsigned ithis;
        char *ikey;
};

static void
_dict_mark_atindex (dict_t *dict, char *key, data_t *value, void *data)
{
        struct dictidxmark *dim = data;

        if (dim->isrch == dim->ithis)
                dim->ikey = key;

        dim->ithis++;
}

static char *
dict_get_by_index (dict_t *dict, unsigned i)
{
        struct dictidxmark dim = {0,};

        dim.isrch = i;
        dict_foreach (dict, _dict_mark_atindex, &dim);

        return dim.ikey;
}

static int
glusterd_get_slave (glusterd_volinfo_t *vol, const char *slaveurl, char **slavekey)
{
        runner_t runner = {0,};
        int n = 0;
        int i = 0;
        char **linearr = NULL;

        glusterd_urltransform_init (&runner, "canonicalize");
        dict_foreach (vol->gsync_slaves, _glusterd_urltransform_add_iter, &runner);
        glusterd_urltransform_add (&runner, slaveurl);

        n = glusterd_urltransform (&runner, &linearr);
        if (n == -1)
                return -2;

        for (i = 0; i < n - 1; i++) {
                if (strcmp (linearr[i], linearr[n - 1]) == 0)
                        break;
        }
        glusterd_urltransform_free (linearr, i);

        if (i < n - 1)
                *slavekey = dict_get_by_index (vol->gsync_slaves, i);
        else
                i = -1;

        return i;
}


static int
glusterd_query_extutil (char *resbuf, runner_t *runner)
{
        char               *ptr = NULL;
        int                 ret = 0;

        runner_redir (runner, STDOUT_FILENO, RUN_PIPE);
        if (runner_start (runner) != 0) {
                gf_log ("", GF_LOG_ERROR, "spawning child failed");

                return -1;
        }

        ptr = fgets(resbuf, PATH_MAX, runner_chio (runner, STDOUT_FILENO));
        if (ptr)
                resbuf[strlen(resbuf)-1] = '\0'; //strip off \n

        ret = runner_end (runner);
        if (ret)
                gf_log ("", GF_LOG_ERROR, "reading data from child failed");

        return ret ? -1 : 0;
}

int
glusterd_gsync_get_param_file (char *prmfile, const char *param, char *master,
                               char *slave, char *gl_workdir)
{
        runner_t            runner = {0,};

        runinit (&runner);
        runner_add_args  (&runner, GSYNCD_PREFIX"/gsyncd", "-c", NULL);
        runner_argprintf (&runner, "%s/"GSYNC_CONF, gl_workdir);
        runner_argprintf (&runner, ":%s", master);
        runner_add_args  (&runner, slave, "--config-get", NULL);
        runner_argprintf (&runner, "%s-file", param);

        return glusterd_query_extutil (prmfile, &runner);
}

int
glusterd_gsync_get_session_owner (char *master, char *slave, char *session_owner,
                                  char *gl_workdir)
{
        runner_t runner = {0,};

        runinit(&runner);
        runner_add_args  (&runner, GSYNCD_PREFIX"/gsyncd", "-c", NULL);
        runner_argprintf (&runner, "%s/"GSYNC_CONF, gl_workdir);
        runner_argprintf (&runner, ":%s", master);
        runner_add_args  (&runner, slave, "--config-get", "session-owner",
                          NULL);

        return glusterd_query_extutil (session_owner, &runner);
}

/* check whether @slave is local or remote. normalized
 * urls starting with ssh are considered to be remote
 * @returns
 *    1 if slave is remote
 *    0 is slave is local
 */
int
glusterd_gsync_slave_is_remote (char *slave)
{
        int   ret     = 0;
        char *ssh_pos = NULL;

        ssh_pos = strstr(slave, "ssh://");
        if ( ssh_pos && ((ssh_pos - slave) == 0) )
                ret = 1;

        return ret;
}

int
glusterd_gsync_get_slave_log_file (char *master, char *slave, char *log_file)
{
        int              ret        = -1;
        runner_t         runner     = {0,};
        char uuid_str[64]           = {0,};
        glusterd_conf_t *priv       = NULL;
        char            *gl_workdir = NULL;

        GF_ASSERT(THIS);
        GF_ASSERT(THIS->private);

        priv = THIS->private;

        GF_VALIDATE_OR_GOTO("gsyncd", master, out);
        GF_VALIDATE_OR_GOTO("gsyncd", slave, out);

        gl_workdir = priv->workdir;

        /* get the session owner for the master-slave session */
        ret = glusterd_gsync_get_session_owner (master, slave, uuid_str,
                                                gl_workdir);
        if (ret)
                goto out;

        /* get the log file for the slave */
        runinit(&runner);
        runner_add_args  (&runner, GSYNCD_PREFIX"/gsyncd", "-c", NULL);
        runner_argprintf (&runner, "%s/"GSYNC_CONF, gl_workdir);
        runner_argprintf (&runner, "--session-owner=%s", uuid_str);
        runner_add_args  (&runner, slave, "--config-get", "log-file", NULL);

        ret = glusterd_query_extutil (log_file, &runner);

 out:
        return ret;
}

static int
gsyncd_getpidfile (char *master, char *slave, char *pidfile)
{
        int                ret             = -1;
        glusterd_conf_t    *priv  = NULL;

        GF_ASSERT (THIS);
        GF_ASSERT (THIS->private);

        priv = THIS->private;

        GF_VALIDATE_OR_GOTO ("gsync", master, out);
        GF_VALIDATE_OR_GOTO ("gsync", slave, out);

        ret = glusterd_gsync_get_param_file (pidfile, "pid", master,
                                              slave, priv->workdir);
        if (ret == -1) {
                ret = -2;
                gf_log ("", GF_LOG_WARNING, "failed to create the pidfile string");
                goto out;
        }

        ret = open (pidfile, O_RDWR);

 out:
        return ret;
}

static int
glusterd_gsyncd_getlogfile (char *master, char *slave, char *log_file)
{
        int                ret             = -1;
        glusterd_conf_t    *priv  = NULL;

        GF_ASSERT (THIS);
        GF_ASSERT (THIS->private);

        priv = THIS->private;

        GF_VALIDATE_OR_GOTO ("gsync", master, out);
        GF_VALIDATE_OR_GOTO ("gsync", slave, out);

        ret = glusterd_gsync_get_param_file (log_file, "log", master,
                                              slave, priv->workdir);
        if (ret == -1) {
                ret = -2;
                gf_log ("", GF_LOG_WARNING, "failed to gsyncd logfile");
                goto out;
        }

 out:
        return ret;
}

static int
gsync_status_byfd (int fd)
{
        GF_ASSERT (fd >= -1);

        if (lockf (fd, F_TEST, 0) == -1 &&
            (errno == EAGAIN || errno == EACCES))
                /* gsyncd keeps the pidfile locked */
                return 0;

        return -1;
}

/* status: return 0 when gsync is running
 * return -1 when not running
 */
int
gsync_status (char *master, char *slave, int *status)
{
        char pidfile[PATH_MAX] = {0,};
        int  fd                = -1;

        fd = gsyncd_getpidfile (master, slave, pidfile);
        if (fd == -2)
                return -1;

        *status = gsync_status_byfd (fd);

        sys_close (fd);

        return 0;
}


int32_t
glusterd_gsync_volinfo_dict_set (glusterd_volinfo_t *volinfo,
                                 char *key, char *value)
{
        int32_t  ret            = -1;
        char    *gsync_status   = NULL;

        gsync_status = gf_strdup (value);
        if (!gsync_status) {
                gf_log ("", GF_LOG_ERROR, "Unable to allocate memory");
                goto out;
        }

        ret = dict_set_dynstr (volinfo->dict, key, gsync_status);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to set dict");
                goto out;
        }

        ret = 0;
out:
        return 0;
}

int
gsync_verify_config_options (dict_t *dict, char **op_errstr)
{
        char  **resopt    = NULL;
        int     i         = 0;
        char   *subop     = NULL;
        char   *slave     = NULL;
        char   *op_name   = NULL;
        char   *op_value  = NULL;
        char   *t         = NULL;
        gf_boolean_t banned = _gf_true;

        if (dict_get_str (dict, "subop", &subop) != 0) {
                gf_log ("", GF_LOG_WARNING, "missing subop");
                *op_errstr = gf_strdup ("Invalid config request");
                return -1;
        }

        if (dict_get_str (dict, "slave", &slave) != 0) {
                gf_log ("", GF_LOG_WARNING, GEOREP" CONFIG: no slave given");
                *op_errstr = gf_strdup ("Slave required");
                return -1;
        }

        if (strcmp (subop, "get-all") == 0)
                return 0;

        if (dict_get_str (dict, "op_name", &op_name) != 0) {
                gf_log ("", GF_LOG_WARNING, "option name missing");
                *op_errstr = gf_strdup ("Option name missing");
                return -1;
        }

        if (runcmd (GSYNCD_PREFIX"/gsyncd", "--config-check", op_name, NULL)) {
                gf_log ("", GF_LOG_WARNING, "Invalid option %s", op_name);
                *op_errstr = gf_strdup ("Invalid option");

                return -1;
        }

        if (strcmp (subop, "get") == 0)
                return 0;

        t = strtail (subop, "set");
        if (!t)
                t = strtail (subop, "del");
        if (!t || (t[0] && strcmp (t, "-glob") != 0)) {
                gf_log ("", GF_LOG_WARNING, "unknown subop %s", subop);
                *op_errstr = gf_strdup ("Invalid config request");
                return -1;
        }

        if (strtail (subop, "set") &&
            dict_get_str (dict, "op_value", &op_value) != 0) {
                gf_log ("", GF_LOG_WARNING, "missing value for set");
                *op_errstr = gf_strdup ("missing value");
        }

        /* match option name against reserved options, modulo -/_
         * difference
         */
        for (resopt = gsync_reserved_opts; *resopt; resopt++) {
                banned = _gf_true;
                for (i = 0; (*resopt)[i] && op_name[i]; i++) {
                        if ((*resopt)[i] == op_name[i] ||
                            ((*resopt)[i] == '-' && op_name[i] == '_'))
                                continue;
                        banned = _gf_false;
                }
                if (banned) {
                        gf_log ("", GF_LOG_WARNING, "Reserved option %s", op_name);
                        *op_errstr = gf_strdup ("Reserved option");

                        return -1;
                        break;
                }
        }

        return 0;
}

static void
_get_status_mst_slv (dict_t *this, char *key, data_t *value, void *data)
{
        glusterd_gsync_status_temp_t  *param = NULL;
        char                          *slave = NULL;
        int                           ret = 0;

        param = (glusterd_gsync_status_temp_t *)data;

        GF_ASSERT (param);
        GF_ASSERT (param->volinfo);

        slave = strchr(value->data, ':');
        if (slave)
                slave ++;
        else
                return;

        ret = glusterd_get_gsync_status_mst_slv(param->volinfo,
                                                slave, param->rsp_dict);

}


static void
_get_max_gsync_slave_num (dict_t *this, char *key, data_t *value, void *data)
{
        int  tmp_slvnum = 0;
        int *slvnum = (int *)data;

        sscanf (key, "slave%d", &tmp_slvnum);
        if (tmp_slvnum > *slvnum)
                *slvnum = tmp_slvnum;
}

static int
glusterd_remove_slave_in_info (glusterd_volinfo_t *volinfo, char *slave,
                               char **op_errstr)
{
        int   ret = 0;
        char *slavekey = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (slave);

        ret = glusterd_get_slave (volinfo, slave, &slavekey);
        if (ret < 0) {
                ret++;
                goto out;
        }

        dict_del (volinfo->gsync_slaves, slavekey);

        ret = glusterd_store_volinfo (volinfo,
                                      GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
                 *op_errstr = gf_strdup ("Failed to store the Volume"
                                         "information");
                goto out;
        }
 out:
        gf_log ("", GF_LOG_DEBUG, "returning %d", ret);
        return ret;

}

static int
glusterd_gsync_get_uuid (char *slave, glusterd_volinfo_t *vol,
                         uuid_t uuid)
{
        int   ret = 0;
        char *slavekey = NULL;
        char *slaveentry = NULL;
        char *t = NULL;

        GF_ASSERT (vol);
        GF_ASSERT (slave);

        ret = glusterd_get_slave (vol, slave, &slavekey);
        if (ret < 0) {
                /* XXX colliding cases of failure and non-extant
                 * slave... now just doing this as callers of this
                 * function can make sense only of -1 and 0 as retvals;
                 * getting at the proper semanticals will involve
                 * fixing callers as well.
                 */
                ret = -1;
                goto out;
        }

        ret = dict_get_str (vol->gsync_slaves, slavekey, &slaveentry);
        GF_ASSERT (ret == 0);

        t = strchr (slaveentry, ':');
        GF_ASSERT (t);
        *t = '\0';
        ret = uuid_parse (slaveentry, uuid);
        *t = ':';

 out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int
glusterd_check_gsync_running_local (char *master, char *slave,
                                    gf_boolean_t *is_run)
{
        int                 ret    = -1;
        int                 ret_status = 0;

        GF_ASSERT (master);
        GF_ASSERT (slave);
        GF_ASSERT (is_run);

        *is_run = _gf_false;
        ret = gsync_status (master, slave, &ret_status);
        if (ret == 0 && ret_status == 0) {
                *is_run = _gf_true;
        } else if (ret == -1) {
                gf_log ("", GF_LOG_WARNING, GEOREP" validation "
                        " failed");
                goto out;
        }
        ret = 0;
 out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}

static int
glusterd_store_slave_in_info (glusterd_volinfo_t *volinfo, char *slave,
                              char *host_uuid, char **op_errstr)
{
        int    ret = 0;
        int    maxslv = 0;
        char **linearr = NULL;
        char  *value = NULL;
        char  *slavekey = NULL;
        char  *slaveentry = NULL;
        char   key[512] = {0, };
        char  *t = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (slave);
        GF_ASSERT (host_uuid);

        ret = glusterd_get_slave (volinfo, slave, &slavekey);
        switch (ret) {
        case -2:
                ret = -1;
                goto out;
        case -1:
                                break;
        default:
                GF_ASSERT (ret > 0);
                ret = dict_get_str (volinfo->gsync_slaves, slavekey, &slaveentry);
                GF_ASSERT (ret == 0);

                /* same-name + same-uuid slave entries should have been filtered
                 * out in glusterd_op_verify_gsync_start_options(), so we can
                 * assert an uuid mismatch
                 */
                t = strtail (slaveentry, host_uuid);
                GF_ASSERT (!t || *t != ':');

                gf_log ("", GF_LOG_ERROR, GEOREP" has already been invoked for "
                                          "the %s (master) and %s (slave) "
                                          "from a different machine",
                                           volinfo->volname, slave);
                *op_errstr = gf_strdup (GEOREP" already running in an an"
                                        "another machine");
                ret = -1;
                goto out;
        }

        ret = glusterd_urltransform_single (slave, "normalize", &linearr);
        if (ret == -1)
                goto out;

        ret = gf_asprintf (&value,  "%s:%s", host_uuid, linearr[0]);
        glusterd_urltransform_free (linearr, 1);
        if (ret  == -1)
                goto out;

        dict_foreach (volinfo->gsync_slaves, _get_max_gsync_slave_num, &maxslv);
        snprintf (key, 512, "slave%d", maxslv + 1);
        ret = dict_set_dynstr (volinfo->gsync_slaves, key, value);
        if (ret)
                goto out;

        ret = glusterd_store_volinfo (volinfo,
                                      GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
                *op_errstr = gf_strdup ("Failed to store the Volume "
                                        "information");
                goto out;
        }
        ret = 0;
 out:
        return ret;
}


static int
glusterd_op_verify_gsync_start_options (glusterd_volinfo_t *volinfo,
                                        char *slave, char **op_errstr)
{
        int                     ret = -1;
        gf_boolean_t            is_running = _gf_false;
        char                    msg[2048] = {0};
        uuid_t                  uuid = {0};
        glusterd_conf_t         *priv = NULL;
        xlator_t                *this = NULL;

        this = THIS;

        GF_ASSERT (volinfo);
        GF_ASSERT (slave);
        GF_ASSERT (op_errstr);
        GF_ASSERT (this && this->private);

        priv  = this->private;

        if (GLUSTERD_STATUS_STARTED != volinfo->status) {
                snprintf (msg, sizeof (msg), "Volume %s needs to be started "
                          "before "GEOREP" start", volinfo->volname);
                goto out;
        }
        /*Check if the gsync is already started in cmd. inited host
         * If so initiate add it into the glusterd's priv*/
        ret = glusterd_gsync_get_uuid (slave, volinfo, uuid);
        if ((ret == 0) && (uuid_compare (priv->uuid, uuid) == 0)) {
                ret = glusterd_check_gsync_running_local (volinfo->volname,
                                                          slave, &is_running);
                if (ret) {
                        snprintf (msg, sizeof (msg), GEOREP" start option "
                                  "validation failed ");
                        goto out;
                }
                if (_gf_true == is_running) {
                        snprintf (msg, sizeof (msg), GEOREP " session between"
                                  " %s & %s already started", volinfo->volname,
                                  slave);
                        ret = -1;
                        goto out;
                }
        }
        ret = 0;
out:
        if (ret && (msg[0] != '\0')) {
                *op_errstr = gf_strdup (msg);
        }
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_check_gsync_running (glusterd_volinfo_t *volinfo, gf_boolean_t *flag)
{

        GF_ASSERT (volinfo);
        GF_ASSERT (flag);

        if (volinfo->gsync_slaves->count)
                *flag = _gf_true;
        else
                *flag = _gf_false;

        return 0;
}

static int
glusterd_op_verify_gsync_running (glusterd_volinfo_t *volinfo,
                                  char *slave, char **op_errstr)
{
        int                     ret = -1;
        char                    msg[2048] = {0};
        uuid_t                  uuid = {0};

        GF_ASSERT (THIS && THIS->private);
        GF_ASSERT (volinfo);
        GF_ASSERT (slave);
        GF_ASSERT (op_errstr);

        if (GLUSTERD_STATUS_STARTED != volinfo->status) {
                snprintf (msg, sizeof (msg), "Volume %s needs to be started "
                          "before "GEOREP" start", volinfo->volname);

                goto out;
        }
        ret = glusterd_gsync_get_uuid (slave, volinfo, uuid);
        if (ret == -1) {
                snprintf (msg, sizeof (msg), GEOREP" session between %s & %s"
                          " not active", volinfo->volname, slave);
                goto out;
        }

        ret = 0;
out:
        if (ret && (msg[0] != '\0')) {
                *op_errstr = gf_strdup (msg);
        }
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

static int
glusterd_verify_gsync_status_opts (dict_t *dict, char **op_errstr)
{
        char               *slave  = NULL;
        char               *volname = NULL;
        char               errmsg[PATH_MAX] = {0, };
        gf_boolean_t       exists = _gf_false;
        glusterd_volinfo_t *volinfo = NULL;
        int                ret = 0;

        ret = dict_get_str (dict, "master", &volname);
        if (ret < 0) {
                ret = 0;
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        ret = glusterd_volinfo_find (volname, &volinfo);
        if ((ret) || (!exists)) {
                gf_log ("", GF_LOG_WARNING, "volume name does not exist");
                snprintf (errmsg, sizeof(errmsg), "Volume name %s does not"
                          " exist", volname);
                *op_errstr = gf_strdup (errmsg);
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "slave", &slave);
        if (ret < 0) {
                ret = 0;
                goto out;
        }

 out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;

}


int
glusterd_op_gsync_args_get (dict_t *dict, char **op_errstr,
                            char **master, char **slave)
{

        int             ret = -1;
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        if (master) {
                ret = dict_get_str (dict, "master", master);
                if (ret < 0) {
                        gf_log ("", GF_LOG_WARNING, "master not found");
                        *op_errstr = gf_strdup ("master not found");
                        goto out;
                }
        }

        if (slave) {
                ret = dict_get_str (dict, "slave", slave);
                if (ret < 0) {
                        gf_log ("", GF_LOG_WARNING, "slave not found");
                        *op_errstr = gf_strdup ("slave not found");
                        goto out;
                }
        }


        ret = 0;
out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_op_stage_gsync_set (dict_t *dict, char **op_errstr)
{
        int                     ret     = 0;
        int                     type    = 0;
        char                    *volname = NULL;
        char                    *slave   = NULL;
        gf_boolean_t            exists   = _gf_false;
        glusterd_volinfo_t      *volinfo = NULL;
        char                    errmsg[PATH_MAX] = {0,};
        dict_t                  *ctx = NULL;


        ret = dict_get_int32 (dict, "type", &type);
        if (ret < 0) {
                gf_log ("", GF_LOG_WARNING, "command type not found");
                *op_errstr = gf_strdup ("command unsuccessful");
                goto out;
        }

        switch (type) {
        case GF_GSYNC_OPTION_TYPE_STATUS:
                ret = glusterd_verify_gsync_status_opts (dict, op_errstr);

                goto out;
        case GF_GSYNC_OPTION_TYPE_CONFIG:
                ret = gsync_verify_config_options (dict, op_errstr);

                goto out;

        case GF_GSYNC_OPTION_TYPE_ROTATE:
                /* checks same as status mode */
                ret = glusterd_verify_gsync_status_opts(dict, op_errstr);
                goto out;
        }

        ret = glusterd_op_gsync_args_get (dict, op_errstr, &volname, &slave);
        if (ret)
                goto out;

        exists = glusterd_check_volume_exists (volname);
        ret = glusterd_volinfo_find (volname, &volinfo);
        if ((ret) || (!exists)) {
                gf_log ("", GF_LOG_WARNING, "volume name does not exist");
                snprintf (errmsg, sizeof(errmsg), "Volume name %s does not"
                          " exist", volname);
                *op_errstr = gf_strdup (errmsg);
                ret = -1;
                goto out;
        }

        switch (type) {
        case GF_GSYNC_OPTION_TYPE_START:
                ret = glusterd_op_verify_gsync_start_options (volinfo, slave,
                                                              op_errstr);
                if (ret)
                        goto out;
                ctx = glusterd_op_get_ctx();
                if (ctx) {
                        /*gsyncd does a fuse mount to start the geo-rep session*/
                        if (!glusterd_is_fuse_available ()) {
                                gf_log ("glusterd", GF_LOG_ERROR, "Unable to open"
                                        " /dev/fuse (%s), geo-replication start"
                                        " failed", strerror (errno));
                                snprintf (errmsg, sizeof(errmsg),
                                          "fuse unvailable");
                                *op_errstr = gf_strdup (errmsg);
                                ret = -1;
                                goto out;
                        }
                }
                break;

        case GF_GSYNC_OPTION_TYPE_STOP:
                ret = glusterd_op_verify_gsync_running (volinfo, slave,
                                                        op_errstr);
                break;
        }

out:
        return ret;
}

int
stop_gsync (char *master, char *slave, char **msg)
{
        int32_t         ret     = 0;
        int             pfd     = -1;
        pid_t           pid     = 0;
        char            pidfile[PATH_MAX] = {0,};
        char            buf [1024] = {0,};
        int             i       = 0;

        GF_ASSERT (THIS);
        GF_ASSERT (THIS->private);

        pfd = gsyncd_getpidfile (master, slave, pidfile);
        if (pfd == -2) {
                gf_log ("", GF_LOG_ERROR, GEOREP" stop validation "
                        " failed for %s & %s", master, slave);
                ret = -1;
                goto out;
        }
        if (gsync_status_byfd (pfd) == -1) {
                gf_log ("", GF_LOG_ERROR, "gsyncd b/w %s & %s is not"
                        " running", master, slave);
                if (msg)
                        *msg = gf_strdup ("Warning: "GEOREP" session was "
                                          "defunct at stop time");
                /* monitor gsyncd already dead */
                goto out;
        }

        if (pfd < 0)
                goto out;

        ret = read (pfd, buf, 1024);
        if (ret > 0) {
                pid = strtol (buf, NULL, 10);
                ret = kill (-pid, SIGTERM);
                if (ret) {
                        gf_log ("", GF_LOG_WARNING,
                                "failed to kill gsyncd");
                        goto out;
                }
                for (i = 0; i < 20; i++) {
                        if (gsync_status_byfd (pfd) == -1) {
                                /* monitor gsyncd is dead but worker may
                                 * still be alive, give some more time
                                 * before SIGKILL (hack)
                                 */
                                usleep (50000);
                                break;
                        }
                        usleep (50000);
                }
                kill (-pid, SIGKILL);
                unlink (pidfile);
        }
        ret = 0;

out:
        sys_close (pfd);
        return ret;
}

int
glusterd_check_restart_gsync_session (glusterd_volinfo_t *volinfo, char *slave,
                                      dict_t *resp_dict);

int
glusterd_gsync_configure (glusterd_volinfo_t *volinfo, char *slave,
                          dict_t *dict, dict_t *resp_dict, char **op_errstr)
{
        int32_t         ret     = -1;
        char            *op_name = NULL;
        char            *op_value = NULL;
        runner_t        runner    = {0,};
        glusterd_conf_t *priv   = NULL;
        char            *subop  = NULL;
        char            *master = NULL;

        GF_ASSERT (slave);
        GF_ASSERT (op_errstr);
        GF_ASSERT (dict);
        GF_ASSERT (resp_dict);

        ret = dict_get_str (dict, "subop", &subop);
        if (ret != 0)
                goto out;

        if (strcmp (subop, "get") == 0 || strcmp (subop, "get-all") == 0) {
                /* deferred to cli */
                gf_log ("", GF_LOG_DEBUG, "Returning 0");
                return 0;
        }

        ret = dict_get_str (dict, "op_name", &op_name);
        if (ret != 0)
                goto out;

        if (strtail (subop, "set")) {
                ret = dict_get_str (dict, "op_value", &op_value);
                if (ret != 0)
                        goto out;
        }

        if (THIS)
                priv = THIS->private;
        if (priv == NULL) {
                gf_log ("", GF_LOG_ERROR, "priv of glusterd not present");
                *op_errstr = gf_strdup ("glusterd defunct");
                goto out;
        }

        master = "";
        runinit (&runner);
        runner_add_args (&runner, GSYNCD_PREFIX"/gsyncd", "-c", NULL);
        runner_argprintf (&runner, "%s/"GSYNC_CONF, priv->workdir);
        if (volinfo) {
                master = volinfo->volname;
                runner_argprintf (&runner, ":%s", master);
        }
        runner_add_arg (&runner, slave);
        runner_argprintf (&runner, "--config-%s", subop);
        runner_add_arg (&runner, op_name);
        if (op_value)
                runner_add_arg (&runner, op_value);
        ret = runner_run (&runner);
        if (ret) {
                gf_log ("", GF_LOG_WARNING, "gsyncd failed to "
                        "%s %s option for %s %s peers",
                        subop, op_name, master, slave);

                gf_asprintf (op_errstr, GEOREP" config-%s failed for %s %s",
                             subop, master, slave);

                goto out;
        }
        ret = 0;
        gf_asprintf (op_errstr, "config-%s successful", subop);

out:
        if (!ret && volinfo) {
                ret = glusterd_check_restart_gsync_session (volinfo, slave,
                                                            resp_dict);
                if (ret)
                        *op_errstr = gf_strdup ("internal error");
        }

        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_gsync_read_frm_status (char *path, char *buf, size_t blen)
{
        int                 ret = 0;
        int           status_fd = -1;
        char                 *p = NULL;

        GF_ASSERT (path);
        GF_ASSERT (buf);
        status_fd = open (path, O_RDONLY);
        if (status_fd == -1) {
                gf_log ("", GF_LOG_ERROR, "Unable to read gsyncd status"
                        " file");
                return -1;
        }
        ret = read (status_fd, buf, blen - 1);
        if (ret > 0) {
                p = buf + strlen (buf) - 1;
                while (isspace (*p))
                        *p-- = '\0';
                ret = 0;
        } else if (ret < 0)
                gf_log ("", GF_LOG_ERROR, "Status file of gsyncd is corrupt");

        close (status_fd);
        return ret;
}

int
glusterd_read_status_file (char *master, char *slave,
                           dict_t *dict)
{
        glusterd_conf_t *priv = NULL;
        int              ret = 0;
        char             statefile[PATH_MAX] = {0, };
        char             buf[1024] = {0, };
        char             mst[1024] = {0, };
        char             slv[1024] = {0, };
        char             sts[1024] = {0, };
        char            *bufp = NULL;
        int              gsync_count = 0;
        int              status = 0;

        GF_ASSERT (THIS);
        GF_ASSERT (THIS->private);

        priv = THIS->private;
        ret = glusterd_gsync_get_param_file (statefile, "state", master,
                                             slave, priv->workdir);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to get the name of status"
                        "file for %s(master), %s(slave)", master, slave);
                goto out;

        }

        ret = gsync_status (master, slave, &status);
        if (ret == 0 && status == -1) {
                strncpy (buf, "defunct", sizeof (buf));
                goto done;
        } else if (ret == -1)
                goto out;

        ret = glusterd_gsync_read_frm_status (statefile, buf, sizeof (buf));
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to read the status"
                        "file for %s(master), %s(slave)", master, slave);
                strncpy (buf, "defunct", sizeof (buf));
                goto done;
        }

 done:
        ret = dict_get_int32 (dict, "gsync-count", &gsync_count);

        if (ret)
                gsync_count = 1;
        else
                gsync_count++;

        snprintf (mst, sizeof (mst), "master%d", gsync_count);
        master = gf_strdup (master);
        if (!master)
                goto out;
        ret = dict_set_dynstr (dict, mst, master);
        if (ret) {
                GF_FREE (master);
                goto out;
        }

        snprintf (slv, sizeof (slv), "slave%d", gsync_count);
        slave = gf_strdup (slave);
        if (!slave)
                goto out;
        ret = dict_set_dynstr (dict, slv, slave);
        if (ret) {
                GF_FREE (slave);
                goto out;
        }

        snprintf (sts, sizeof (slv), "status%d", gsync_count);
        bufp = gf_strdup (buf);
        if (!bufp)
                goto out;
        ret = dict_set_dynstr (dict, sts, bufp);
        if (ret) {
                GF_FREE (bufp);
                goto out;
        }
        ret = dict_set_int32 (dict, "gsync-count", gsync_count);
        if (ret)
                goto out;

        ret = 0;
 out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d ", ret);
        return ret;
}

int
glusterd_check_restart_gsync_session (glusterd_volinfo_t *volinfo, char *slave,
                                      dict_t *resp_dict)
{

        int                    ret = 0;
        uuid_t                 uuid = {0, };
        glusterd_conf_t        *priv = NULL;
        char                   *status_msg = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (slave);
        GF_ASSERT (THIS);
        GF_ASSERT (THIS->private);

        priv = THIS->private;

        if (glusterd_gsync_get_uuid (slave, volinfo, uuid))
                /* session does not exist, nothing to do */
                goto out;
        if (uuid_compare (priv->uuid, uuid) == 0) {
                ret = stop_gsync (volinfo->volname, slave, &status_msg);
                if (ret == 0 && status_msg)
                        ret = dict_set_str (resp_dict, "gsync-status",
                                            status_msg);
                if (ret == 0)
                        ret = glusterd_start_gsync (volinfo, slave,
                                                    uuid_utoa(priv->uuid), NULL);
        }

 out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int32_t
glusterd_marker_create_volfile (glusterd_volinfo_t *volinfo)
{
        int32_t          ret     = 0;

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret) {
                gf_log ("", GF_LOG_ERROR, "Unable to create volfile"
                        " for setting of marker while '"GEOREP" start'");
                ret = -1;
                goto out;
        }

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status)
                ret = glusterd_nodesvcs_handle_graph_change (volinfo);
        ret = 0;
out:
        return ret;
}

int
glusterd_set_marker_gsync (glusterd_volinfo_t *volinfo)
{
        int                      ret     = -1;
        int                      marker_set = _gf_false;
        char                    *gsync_status = NULL;

        GF_ASSERT (THIS);
        GF_ASSERT (THIS->private);

        marker_set = glusterd_volinfo_get_boolean (volinfo, VKEY_MARKER_XTIME);
        if (marker_set == -1) {
                gf_log ("", GF_LOG_ERROR, "failed to get the marker status");
                ret = -1;
                goto out;
        }

        if (marker_set == _gf_false) {
                gsync_status = gf_strdup ("on");
                if (gsync_status == NULL) {
                        ret = -1;
                        goto out;
                }

                ret = glusterd_gsync_volinfo_dict_set (volinfo,
                                                       VKEY_MARKER_XTIME, gsync_status);
                if (ret < 0)
                        goto out;

                ret = glusterd_marker_create_volfile (volinfo);
                if (ret) {
                        gf_log ("", GF_LOG_ERROR, "Setting dict failed");
                        goto out;
                }
        }
        ret = 0;

out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}




int
glusterd_get_gsync_status_mst_slv( glusterd_volinfo_t *volinfo,
                                   char *slave, dict_t *rsp_dict)
{
        uuid_t             uuid = {0, };
        glusterd_conf_t    *priv = NULL;
        int                ret = 0;

        GF_ASSERT (volinfo);
        GF_ASSERT (slave);
        GF_ASSERT (THIS);
        GF_ASSERT (THIS->private);

        priv = THIS->private;

        ret = glusterd_gsync_get_uuid (slave, volinfo, uuid);
        if ((ret == 0) && (uuid_compare (priv->uuid, uuid) != 0))
                goto out;

        if (ret) {
                ret = 0;
                gf_log ("", GF_LOG_INFO, "geo-replication status %s %s :"
                        "session is not active", volinfo->volname, slave);
                goto out;
        }

        ret = glusterd_read_status_file (volinfo->volname, slave, rsp_dict);
 out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

static int
glusterd_get_gsync_status_mst (glusterd_volinfo_t *volinfo, dict_t *rsp_dict)
{
        glusterd_gsync_status_temp_t  param = {0, };

        GF_ASSERT (volinfo);

        param.rsp_dict = rsp_dict;
        param.volinfo = volinfo;
        dict_foreach (volinfo->gsync_slaves, _get_status_mst_slv, &param);

        return 0;
}

static int
glusterd_get_gsync_status_all ( dict_t *rsp_dict)
{

        int32_t                 ret = 0;
        glusterd_conf_t         *priv = NULL;
        glusterd_volinfo_t      *volinfo = NULL;

        GF_ASSERT (THIS);
        priv = THIS->private;

        GF_ASSERT (priv);

        list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                ret = glusterd_get_gsync_status_mst (volinfo, rsp_dict);
                if (ret)
                        goto out;
        }

out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;

}

static int
glusterd_get_gsync_status (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        char               *slave  = NULL;
        char               *volname = NULL;
        char               errmsg[PATH_MAX] = {0, };
        gf_boolean_t       exists = _gf_false;
        glusterd_volinfo_t *volinfo = NULL;
        int                ret = 0;


        ret = dict_get_str (dict, "master", &volname);
        if (ret < 0){
                ret = glusterd_get_gsync_status_all (rsp_dict);
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        ret = glusterd_volinfo_find (volname, &volinfo);
        if ((ret) || (!exists)) {
                gf_log ("", GF_LOG_WARNING, "volume name does not exist");
                snprintf (errmsg, sizeof(errmsg), "Volume name %s does not"
                          " exist", volname);
                *op_errstr = gf_strdup (errmsg);
                ret = -1;
                goto out;
        }


        ret = dict_get_str (dict, "slave", &slave);
        if (ret < 0) {
                ret = glusterd_get_gsync_status_mst (volinfo, rsp_dict);
                goto out;
        }

        ret = glusterd_get_gsync_status_mst_slv (volinfo, slave, rsp_dict);

 out:
        gf_log ("", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

int
glusterd_send_sigstop (pid_t pid)
{
        int ret = 0;
        ret = kill (pid, SIGSTOP);
        if (ret)
                gf_log ("", GF_LOG_ERROR, GEOREP"failed to send SIGSTOP signal");
        return ret;
}

int
glusterd_send_sigcont (pid_t pid)
{
        int ret = 0;
        ret = kill (pid, SIGCONT);
        if (ret)
                gf_log ("", GF_LOG_ERROR, GEOREP"failed to send SIGCONT signal");
        return ret;
}

/*
 * Log rotations flow is something like this:
 * - Send SIGSTOP to process group (this will stop monitor/worker process
 *   and also the slave if it's local)
 * - Rotate log file for monitor/worker
 * - Rotate log file for slave if it's local
 * - Send SIGCONT to the process group. Monitor wakes up, kills the worker
 *   (this is done in the SIGCONT handler), which results in the termination
 *   of the slave (local/remote). After returning from signal handler,
 *   monitor detects absence of worker and starts it again, which in-turn
 *   starts the slave.
 */
int
glusterd_send_log_rotate_signal (pid_t pid, char *logfile1, char *logfile2)
{
        int         ret         = 0;
        char rlogfile[PATH_MAX] = {0,};
        time_t      rottime     = 0;

        ret = glusterd_send_sigstop (-pid);
        rottime = time (NULL);

        snprintf (rlogfile, sizeof (rlogfile), "%s.%"PRIu64, logfile1,
                  (uint64_t) rottime);
        ret = rename (logfile1, rlogfile);
        if (ret)
                gf_log ("", GF_LOG_ERROR, "rename failed for geo-rep log file");

        if (!*logfile2) {
                gf_log ("", GF_LOG_DEBUG, "Slave is not local,"
                        " skipping rotation");
                ret = 0;
                goto out;
        }

        (void) snprintf (rlogfile, sizeof (rlogfile), "%s.%"PRIu64, logfile2,
                         (uint64_t) rottime);
        ret = rename (logfile2, rlogfile);
        if (ret)
                gf_log ("", GF_LOG_ERROR, "rename failed for geo-rep slave"
                        " log file");

 out:
        ret = glusterd_send_sigcont (-pid);

        return ret;
}

int
glusterd_get_pid_from_file (char *master, char *slave, pid_t *pid)
{
        int ret                = -1;
        int pfd                = 0;
        char pidfile[PATH_MAX] = {0,};
        char buff[1024]        = {0,};

        pfd = gsyncd_getpidfile (master, slave, pidfile);
        if (pfd == -2) {
                gf_log ("", GF_LOG_ERROR, GEOREP" log-rotate validation "
                        " failed for %s & %s", master, slave);
                goto out;
        }
        if (gsync_status_byfd (pfd) == -1) {
                gf_log ("", GF_LOG_ERROR, "gsyncd b/w %s & %s is not"
                        " running", master, slave);
                goto out;
        }

        if (pfd < 0)
                goto out;

        ret = read (pfd, buff, 1024);
        if (ret < 0) {
                gf_log ("", GF_LOG_ERROR, GEOREP" cannot read pid from pid-file");
                goto out;
        }


        *pid = strtol (buff, NULL, 10);
        ret = 0;

out:
        sys_close(pfd);
        return ret;
}

int
glusterd_do_gsync_log_rotate (char *master, char *slave, uuid_t *uuid, char **op_errstr)
{
        int              ret     = 0;
        glusterd_conf_t *priv    = NULL;
        pid_t            pid     = 0;
        char log_file1[PATH_MAX] = {0,};
        char log_file2[PATH_MAX] = {0,};

        GF_ASSERT (THIS);
        GF_ASSERT (THIS->private);

        priv = THIS->private;

        ret = glusterd_get_pid_from_file (master, slave, &pid);
        if (ret)
                goto out;

        /* log file */
        ret = glusterd_gsyncd_getlogfile (master, slave, log_file1);
        if (ret)
                goto out;

        /* check if slave is local or remote */
        ret = glusterd_gsync_slave_is_remote (slave);
        if (ret)
                goto do_rotate;

        /* slave log file - slave is local and it's log can be rotated */
        ret = glusterd_gsync_get_slave_log_file (master, slave, log_file2);
        if (ret)
                goto out;

 do_rotate:
        ret = glusterd_send_log_rotate_signal (pid, log_file1, log_file2);

 out:
        if (ret && op_errstr)
                *op_errstr = gf_strdup("Error rotating log file");
        return ret;
}

int
glusterd_do_gsync_log_rotation_mst_slv (glusterd_volinfo_t *volinfo, char *slave,
                                        char **op_errstr)
{
        uuid_t           uuid = {0, };
        glusterd_conf_t *priv = NULL;
        int              ret  = 0;
        char             errmsg[1024] = {0,};
        xlator_t        *this    = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (slave);
        GF_ASSERT (THIS);
        this = THIS;
        GF_ASSERT (this->private);
        priv = this->private;

        ret = glusterd_gsync_get_uuid (slave, volinfo, uuid);
        if ((ret == 0) && (uuid_compare (priv->uuid, uuid) != 0))
                goto out;

        if (ret) {
                snprintf(errmsg, sizeof(errmsg), "geo-replication session b/w %s %s not active",
                         volinfo->volname, slave);
                gf_log (this->name, GF_LOG_WARNING, "%s", errmsg);
                if (op_errstr)
                        *op_errstr = gf_strdup(errmsg);
                goto out;
        }

        ret = glusterd_do_gsync_log_rotate (volinfo->volname, slave, &uuid, op_errstr);

 out:
        gf_log (this->name, GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

static void
_iterate_log_rotate_mst_slv (dict_t *this, char *key, data_t *value, void *data)
{
        glusterd_gsync_status_temp_t  *param = NULL;
        char                          *slave = NULL;

        param = (glusterd_gsync_status_temp_t *) data;

        GF_ASSERT (param);
        GF_ASSERT (param->volinfo);

        slave = strchr (value->data, ':');
        if (slave)
                slave++;
        else {
                gf_log ("", GF_LOG_ERROR, "geo-replication log-rotate: slave (%s) "
                        "not conforming to format", slave);
                return;
        }

        (void) glusterd_do_gsync_log_rotation_mst_slv (param->volinfo, slave, NULL);
}

int
glusterd_do_gsync_log_rotation_mst (glusterd_volinfo_t *volinfo)
{
        glusterd_gsync_status_temp_t  param = {0, };

        GF_ASSERT (volinfo);

        param.volinfo = volinfo;
        dict_foreach (volinfo->gsync_slaves, _iterate_log_rotate_mst_slv, &param);
        return 0;
}

static int
glusterd_rotate_gsync_all ()
{
        int32_t             ret     = 0;
        glusterd_conf_t    *priv    = NULL;
        glusterd_volinfo_t *volinfo = NULL;

        GF_ASSERT (THIS);
        priv = THIS->private;

        GF_ASSERT (priv);

        list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                ret = glusterd_do_gsync_log_rotation_mst (volinfo);
                if (ret)
                        goto out;
        }

 out:
        gf_log ("", GF_LOG_DEBUG, "Returning with %d", ret);
        return ret;
}

static int
glusterd_rotate_gsync_logs (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        char                *slave   = NULL;
        char                *volname = NULL;
        char errmsg[1024]            = {0,};
        gf_boolean_t         exists  = _gf_false;
        glusterd_volinfo_t  *volinfo = NULL;
        char               **linearr = NULL;
        int                  ret     = 0;

        ret = dict_get_str (dict, "master", &volname);
        if (ret < 0) {
                ret = glusterd_rotate_gsync_all ();
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        ret = glusterd_volinfo_find (volname, &volinfo);
        if ((ret) || (!exists)) {
                snprintf (errmsg, sizeof(errmsg), "Volume %s does not"
                          " exist", volname);
                gf_log ("", GF_LOG_WARNING, "%s", errmsg);
                *op_errstr = gf_strdup (errmsg);
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "slave", &slave);
        if (ret < 0) {
                ret = glusterd_do_gsync_log_rotation_mst (volinfo);
                goto out;
        }

        /* for the given slave use the normalized url */
        ret = glusterd_urltransform_single (slave, "normalize", &linearr);
        if (ret == -1)
                goto out;

        ret = glusterd_do_gsync_log_rotation_mst_slv (volinfo, linearr[0],
                                                      op_errstr);
        if (ret)
                gf_log ("gsyncd", GF_LOG_ERROR, "gsyncd log-rotate failed for"
                        " %s & %s", volname, slave);

        glusterd_urltransform_free (linearr, 1);
 out:
        return ret;
}


int
glusterd_op_gsync_set (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        int32_t             ret     = -1;
        int32_t             type    = -1;
        dict_t             *ctx    = NULL;
        dict_t             *resp_dict = NULL;
        char               *host_uuid = NULL;
        char               *slave  = NULL;
        char               *volname = NULL;
        glusterd_volinfo_t *volinfo = NULL;
        glusterd_conf_t    *priv = NULL;
        char               *status_msg = NULL;
        uuid_t              uuid = {0, };

        GF_ASSERT (THIS);
        GF_ASSERT (THIS->private);
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        priv = THIS->private;

        ret = dict_get_int32 (dict, "type", &type);
        if (ret < 0)
                goto out;

        ret = dict_get_str (dict, "host-uuid", &host_uuid);
        if (ret < 0)
                goto out;

        ctx = glusterd_op_get_ctx ();
        resp_dict = ctx ? ctx : rsp_dict;
        GF_ASSERT (resp_dict);

        if (type == GF_GSYNC_OPTION_TYPE_STATUS) {
                ret = glusterd_get_gsync_status (dict, op_errstr, resp_dict);
                goto out;
        }

        if (type == GF_GSYNC_OPTION_TYPE_ROTATE) {
                ret = glusterd_rotate_gsync_logs (dict, op_errstr, resp_dict);
                goto out;

        }

        ret = dict_get_str (dict, "slave", &slave);
        if (ret < 0)
                goto out;

        if (dict_get_str (dict, "master", &volname) == 0) {
                ret = glusterd_volinfo_find (volname, &volinfo);
                if (ret) {
                        gf_log ("", GF_LOG_WARNING, "Volinfo for %s (master) not found",
                                volname);
                        goto out;
                }
        }

        if (type == GF_GSYNC_OPTION_TYPE_CONFIG) {
                ret = glusterd_gsync_configure (volinfo, slave, dict, resp_dict,
                                                op_errstr);
                goto out;
        }

        if (!volinfo) {
                ret = -1;
                goto out;
        }

        if (type == GF_GSYNC_OPTION_TYPE_START) {

                ret = glusterd_set_marker_gsync (volinfo);
                if (ret != 0) {
                        gf_log ("", GF_LOG_WARNING, "marker start failed");
                        *op_errstr = gf_strdup ("failed to initialize indexing");
                        ret = -1;
                        goto out;
                }
                ret = glusterd_store_slave_in_info(volinfo, slave,
                                                   host_uuid, op_errstr);
                if (ret)
                        goto out;

                ret = glusterd_start_gsync (volinfo, slave, host_uuid,
                                            op_errstr);
        }

        if (type == GF_GSYNC_OPTION_TYPE_STOP) {

                ret = glusterd_gsync_get_uuid (slave, volinfo, uuid);
                if (ret) {
                        gf_log ("", GF_LOG_WARNING, GEOREP" is not set up for"
                                "%s(master) and %s(slave)", volname, slave);
                        *op_errstr = strdup (GEOREP" is not set up");
                        goto out;
                }

                ret = glusterd_remove_slave_in_info(volinfo, slave, op_errstr);
                if (ret)
                        goto out;

                if (uuid_compare (priv->uuid, uuid) != 0) {
                        goto out;
                }

                ret = stop_gsync (volname, slave, &status_msg);
                if (ret == 0 && status_msg)
                        ret = dict_set_str (resp_dict, "gsync-status",
                                            status_msg);
                if (ret != 0)
                        *op_errstr = gf_strdup ("internal error");
        }

out:
        gf_log ("", GF_LOG_DEBUG,"Returning %d", ret);
        return ret;
}
