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
#include "glusterd-geo-rep.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "glusterd-svc-helper.h"
#include "run.h"
#include "syscall.h"
#include "glusterd-messages.h"

#include <signal.h>

static int
dict_get_param (dict_t *dict, char *key, char **param);

struct gsync_config_opt_vals_ gsync_confopt_vals[] = {
        {.op_name        = "change_detector",
         .no_of_pos_vals = 2,
         .case_sensitive = _gf_true,
         .values         = {"xsync", "changelog"},
        },
        {.op_name        = "special_sync_mode",
         .no_of_pos_vals = 2,
         .case_sensitive = _gf_true,
         .values         = {"partial", "recover"}
        },
        {.op_name        = "log-level",
         .no_of_pos_vals = 5,
         .case_sensitive = _gf_false,
         .values         = {"critical", "error", "warning", "info", "debug"}
        },
        {.op_name        = "use-tarssh",
         .no_of_pos_vals = 6,
         .case_sensitive = _gf_false,
         .values         = {"true", "false", "0", "1", "yes", "no"}
        },
        {.op_name        = "ignore_deletes",
         .no_of_pos_vals = 6,
         .case_sensitive = _gf_false,
         .values         = {"true", "false", "0", "1", "yes", "no"}
        },
        {.op_name        = "use_meta_volume",
         .no_of_pos_vals = 6,
         .case_sensitive = _gf_false,
         .values         = {"true", "false", "0", "1", "yes", "no"}
        },
        {.op_name        = "use-meta-volume",
         .no_of_pos_vals = 6,
         .case_sensitive = _gf_false,
         .values         = {"true", "false", "0", "1", "yes", "no"}
        },
        {.op_name = NULL,
        },
};

static char *gsync_reserved_opts[] = {
        "gluster-command-dir",
        "pid-file",
        "state-file",
        "session-owner",
        "state-socket-unencoded",
        "socketdir",
        "local-id",
        "local-path",
        "slave-id",
        NULL
};

static char *gsync_no_restart_opts[] = {
        "checkpoint",
        "log_rsync_performance",
        "log-rsync-performance",
        NULL
};

int
__glusterd_handle_sys_exec (rpcsvc_request_t *req)
{
        int32_t                 ret     = 0;
        dict_t                  *dict   = NULL;
        gf_cli_req              cli_req = {{0},};
        glusterd_op_t           cli_op = GD_OP_SYS_EXEC;
        glusterd_conf_t         *priv   = NULL;
        char                    *host_uuid = NULL;
        char                    err_str[2048] = {0,};
        xlator_t                *this = NULL;

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = xdr_to_generic (req->msg[0], &cli_req,
                              (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL, "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the command");
                        goto out;
               } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }

                host_uuid = gf_strdup (uuid_utoa(MY_UUID));
                if (host_uuid == NULL) {
                        snprintf (err_str, sizeof (err_str), "Failed to get "
                                  "the uuid of local glusterd");
                        ret = -1;
                        goto out;
                }

                ret = dict_set_dynstr (dict, "host-uuid", host_uuid);
                if (ret)
                        goto out;
        }

        ret = glusterd_op_begin_synctask (req, cli_op, dict);

out:
        if (ret) {
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, err_str);
        }
        return ret;
}

int
__glusterd_handle_copy_file (rpcsvc_request_t *req)
{
        int32_t                 ret     = 0;
        dict_t                  *dict   = NULL;
        gf_cli_req              cli_req = {{0},};
        glusterd_op_t           cli_op = GD_OP_COPY_FILE;
        glusterd_conf_t         *priv   = NULL;
        char                    *host_uuid = NULL;
        char                    err_str[2048] = {0,};
        xlator_t                *this = NULL;

        GF_ASSERT (req);

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        ret = xdr_to_generic (req->msg[0], &cli_req,
                              (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL, "failed to"
                                "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the command");
                        goto out;
               } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }

                host_uuid = gf_strdup (uuid_utoa(MY_UUID));
                if (host_uuid == NULL) {
                        snprintf (err_str, sizeof (err_str), "Failed to get "
                                  "the uuid of local glusterd");
                        ret = -1;
                        goto out;
                }

                ret = dict_set_dynstr (dict, "host-uuid", host_uuid);
                if (ret)
                        goto out;
        }

        ret = glusterd_op_begin_synctask (req, cli_op, dict);

out:
        if (ret) {
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, err_str);
        }
        return ret;
}

int
__glusterd_handle_gsync_set (rpcsvc_request_t *req)
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
        char                    err_str[2048] = {0,};
        xlator_t                *this = NULL;

        GF_ASSERT (req);

       this = THIS;
       GF_ASSERT (this);
       priv = this->private;
       GF_ASSERT (priv);

        ret = xdr_to_generic (req->msg[0], &cli_req,
                              (xdrproc_t)xdr_gf_cli_req);
        if (ret < 0) {
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
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_UNSERIALIZE_FAIL, "failed to "
                                "unserialize req-buffer to dictionary");
                        snprintf (err_str, sizeof (err_str), "Unable to decode "
                                  "the command");
                        goto out;
                } else {
                        dict->extra_stdfree = cli_req.dict.dict_val;
                }

                host_uuid = gf_strdup (uuid_utoa(MY_UUID));
                if (host_uuid == NULL) {
                        snprintf (err_str, sizeof (err_str), "Failed to get "
                                  "the uuid of local glusterd");
                        ret = -1;
                        goto out;
                }
                ret = dict_set_dynstr (dict, "host-uuid", host_uuid);
                if (ret)
                        goto out;

        }

        ret = dict_get_str (dict, "master", &master);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
                        "master not found, while handling "GEOREP" options");
                master = "(No Master)";
        }

        ret = dict_get_str (dict, "slave", &slave);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
                         "slave not found, while handling "GEOREP" options");
                slave = "(No Slave)";
        }

        ret = dict_get_int32 (dict, "type", &type);
        if (ret < 0) {
                snprintf (err_str, sizeof (err_str), "Command type not found "
                          "while handling "GEOREP" options");
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "%s", err_str);
                goto out;
        }

        switch (type) {
        case GF_GSYNC_OPTION_TYPE_CREATE:
                strncpy (operation, "create", sizeof (operation));
                cli_op = GD_OP_GSYNC_CREATE;
                break;

        case GF_GSYNC_OPTION_TYPE_START:
                strncpy (operation, "start", sizeof (operation));
                break;

        case GF_GSYNC_OPTION_TYPE_STOP:
                strncpy (operation, "stop", sizeof (operation));
                break;

        case GF_GSYNC_OPTION_TYPE_PAUSE:
                strncpy (operation, "pause", sizeof (operation));
                break;

        case GF_GSYNC_OPTION_TYPE_RESUME:
                strncpy (operation, "resume", sizeof (operation));
                break;

        case GF_GSYNC_OPTION_TYPE_CONFIG:
                strncpy (operation, "config", sizeof (operation));
                break;

        case GF_GSYNC_OPTION_TYPE_STATUS:
                strncpy (operation, "status", sizeof (operation));
                break;
        }

        ret = glusterd_op_begin_synctask (req, cli_op, dict);

out:
        if (ret) {
                if (err_str[0] == '\0')
                        snprintf (err_str, sizeof (err_str),
                                  "Operation failed");
                ret = glusterd_op_send_cli_response (cli_op, ret, 0, req,
                                                     dict, err_str);
        }
        return ret;
}

int
glusterd_handle_sys_exec (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_sys_exec);
}

int
glusterd_handle_copy_file (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_copy_file);
}

int
glusterd_handle_gsync_set (rpcsvc_request_t *req)
{
        return glusterd_big_locked_handler (req, __glusterd_handle_gsync_set);
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

/* Helper routine to terminate just before slave_voluuid */
static int32_t
parse_slave_url (char *slv_url, char **slave)
{
        char      *tmp  = NULL;
        xlator_t  *this = NULL;
        int32_t    ret  = -1;

        this = THIS;

        /* slave format:
         * master_node_uuid:ssh://slave_host::slave_vol:slave_voluuid */
        *slave = strchr (slv_url, ':');
        if (!(*slave)) {
                goto out;
        }
        (*slave)++;

        /* To terminate at : before slave volume uuid */
        tmp = strstr (*slave, "::");
        if (!tmp) {
                goto out;
        }
        tmp += 2;
        tmp = strchr (tmp, ':');
        if (!tmp)
                gf_msg_debug (this->name, 0, "old slave: %s!", *slave);
        else
                *tmp = '\0';

        ret = 0;
        gf_msg_debug (this->name, 0, "parsed slave: %s!", *slave);
out:
        return ret;
}

static int
_glusterd_urltransform_add_iter (dict_t *dict, char *key, data_t *value, void *data)
{
        runner_t *runner = (runner_t *)data;
        char slv_url[VOLINFO_SLAVE_URL_MAX] = {0};
        char *slave          = NULL;
        xlator_t  *this      = NULL;
        int32_t  ret         = -1;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);

        gf_msg_debug (this->name, 0, "value->data %s", value->data);

        strncpy (slv_url, value->data, sizeof(slv_url));
        ret = parse_slave_url (slv_url, &slave);
        if (ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SLAVE_VOL_PARSE_FAIL,
                        "Error in parsing slave: %s!", value->data);
                goto out;
        }

        runner_add_arg (runner, slave);
        ret = 0;
out:
        return ret;
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
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        linearr = GF_CALLOC (arr_len, sizeof (char *), gf_gld_mt_linearr);
        if (!linearr) {
                error = _gf_true;
                goto out;
        }

        runner_redir (runner, STDOUT_FILENO, RUN_PIPE);
        if (runner_start (runner) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SPAWNING_CHILD_FAILED,
                        "spawning child failed");

                error = _gf_true;
                goto out;
        }

        arr_idx = 0;
        for (;;) {
                size_t len;
                line = GF_MALLOC (1024, gf_gld_mt_linebuf);
                if (!line) {
                        error = _gf_true;
                        goto out;
                }

                if (fgets (line, 1024, runner_chio (runner, STDOUT_FILENO)) ==
                    NULL)
                        break;

                len = strlen (line);
                if (len == 0 || line[len - 1] != '\n') {
                        GF_FREE (line);
                        error = _gf_true;
                        goto out;
                }
                line[len - 1] = '\0';

                if (arr_idx == arr_len) {
                        void *p = linearr;
                        arr_len <<= 1;
                        p = GF_REALLOC (linearr, arr_len);
                        if (!p) {
                                GF_FREE (line);
                                error = _gf_true;
                                goto out;
                        }
                        linearr = p;
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
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_READ_CHILD_DATA_FAILED,
                        "reading data from child failed");
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


struct slave_vol_config {
       char      old_slvhost[_POSIX_HOST_NAME_MAX+1];
       char      old_slvuser[LOGIN_NAME_MAX];
       unsigned  old_slvidx;
       char      slave_voluuid[GF_UUID_BUF_SIZE];
};

static int
_dict_mark_atindex (dict_t *dict, char *key, data_t *value, void *data)
{
        struct dictidxmark *dim = data;

        if (dim->isrch == dim->ithis)
                dim->ikey = key;

        dim->ithis++;
        return 0;
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
        int32_t ret = 0;

        glusterd_urltransform_init (&runner, "canonicalize");
        ret = dict_foreach (vol->gsync_slaves, _glusterd_urltransform_add_iter,
                                               &runner);
        if (ret < 0)
                return -2;

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
glusterd_query_extutil_generic (char *resbuf, size_t blen, runner_t *runner, void *data,
                                int (*fcbk)(char *resbuf, size_t blen, FILE *fp, void *data))
{
        int                 ret = 0;
        xlator_t            *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        runner_redir (runner, STDOUT_FILENO, RUN_PIPE);
        if (runner_start (runner) != 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SPAWNING_CHILD_FAILED,
                        "spawning child failed");

                return -1;
        }

        ret = fcbk (resbuf, blen, runner_chio (runner, STDOUT_FILENO), data);

        ret |= runner_end (runner);
        if (ret)
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_READ_CHILD_DATA_FAILED,
                        "reading data from child failed");

        return ret ? -1 : 0;
}

static int
_fcbk_singleline(char *resbuf, size_t blen, FILE *fp, void *data)
{
        char *ptr = NULL;

        errno = 0;
        ptr = fgets (resbuf, blen, fp);
        if (ptr) {
                size_t len = strlen(resbuf);
                if (len && resbuf[len-1] == '\n')
                        resbuf[len-1] = '\0'; //strip off \n
        }

        return errno ? -1 : 0;
}

static int
glusterd_query_extutil (char *resbuf, runner_t *runner)
{
        return glusterd_query_extutil_generic (resbuf, PATH_MAX, runner, NULL,
                                               _fcbk_singleline);
}

static int
glusterd_get_slave_voluuid (char *slave_host, char *slave_vol, char *vol_uuid)
{
        runner_t          runner = {0,};
        glusterd_conf_t   *priv  = NULL;
        xlator_t          *this  = NULL;
        int ret                  = -1;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);

        priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        runinit (&runner);
        runner_add_arg  (&runner, GSYNCD_PREFIX"/gsyncd");
        runner_add_arg  (&runner, "--slavevoluuid-get");
        runner_argprintf (&runner, "%s::%s", slave_host, slave_vol);

        synclock_unlock (&priv->big_lock);
        ret = glusterd_query_extutil (vol_uuid, &runner);
        synclock_lock (&priv->big_lock);

out:
        return ret;
}


static int
_fcbk_conftodict (char *resbuf, size_t blen, FILE *fp, void *data)
{
        char   *ptr = NULL;
        dict_t *dict = data;
        char   *v = NULL;

        for (;;) {
                errno = 0;
                ptr = fgets (resbuf, blen, fp);
                if (!ptr)
                        break;
                v = resbuf + strlen(resbuf) - 1;
                while (isspace (*v))
                        /* strip trailing space */
                        *v-- = '\0';
                if (v == resbuf)
                        /* skip empty line */
                        continue;
                v = strchr (resbuf, ':');
                if (!v)
                        return -1;
                *v++ = '\0';
                while (isspace (*v))
                        v++;
                v = gf_strdup (v);
                if (!v)
                        return -1;
                if (dict_set_dynstr (dict, resbuf, v) != 0) {
                        GF_FREE (v);
                        return -1;
                }
        }

        return errno ? -1 : 0;
}

static int
glusterd_gsync_get_config (char *master, char *slave, char *conf_path, dict_t *dict)
{
        /* key + value, where value must be able to accommodate a path */
        char resbuf[256 + PATH_MAX] = {0,};
        runner_t             runner = {0,};

        runinit (&runner);
        runner_add_args  (&runner, GSYNCD_PREFIX"/gsyncd", "-c", NULL);
        runner_argprintf (&runner, "%s", conf_path);
        runner_argprintf (&runner, "--iprefix=%s", DATADIR);
        runner_argprintf (&runner, ":%s", master);
        runner_add_args  (&runner, slave, "--config-get-all", NULL);

        return glusterd_query_extutil_generic (resbuf, sizeof (resbuf),
                                               &runner, dict, _fcbk_conftodict);
}

static int
_fcbk_statustostruct (char *resbuf, size_t blen, FILE *fp,
                      void *data)
{
        char                  *ptr = NULL;
        char                    *v = NULL;
        char                    *k = NULL;
        gf_gsync_status_t *sts_val = NULL;

        sts_val = (gf_gsync_status_t *)data;

        for (;;) {
                errno = 0;
                ptr = fgets (resbuf, blen, fp);
                if (!ptr)
                        break;

                v = resbuf + strlen(resbuf) - 1;
                while (isspace (*v))
                        /* strip trailing space */
                        *v-- = '\0';
                if (v == resbuf)
                        /* skip empty line */
                        continue;
                v = strchr (resbuf, ':');
                if (!v)
                        return -1;
                *v++ = '\0';
                while (isspace (*v))
                        v++;
                v = gf_strdup (v);
                if (!v)
                        return -1;

                k = gf_strdup (resbuf);
                if (!k) {
                        GF_FREE (v);
                        return -1;
                }

                if (strcmp (k, "worker_status") == 0) {
                        memcpy (sts_val->worker_status, v,
                                strlen(v));
                        sts_val->worker_status[strlen(v)] = '\0';
                } else if (strcmp (k, "slave_node") == 0) {
                        memcpy (sts_val->slave_node, v,
                                strlen(v));
                        sts_val->slave_node[strlen(v)] = '\0';
                } else if (strcmp (k, "crawl_status") == 0) {
                        memcpy (sts_val->crawl_status, v,
                                strlen(v));
                        sts_val->crawl_status[strlen(v)] = '\0';
                } else if (strcmp (k, "last_synced") == 0) {
                        memcpy (sts_val->last_synced, v,
                                strlen(v));
                        sts_val->last_synced[strlen(v)] = '\0';
                } else if (strcmp (k, "last_synced_utc") == 0) {
                        memcpy (sts_val->last_synced_utc, v,
                                strlen(v));
                        sts_val->last_synced_utc[strlen(v)] = '\0';
                } else if (strcmp (k, "entry") == 0) {
                        memcpy (sts_val->entry, v,
                                strlen(v));
                        sts_val->entry[strlen(v)] = '\0';
                } else if (strcmp (k, "data") == 0) {
                        memcpy (sts_val->data, v,
                                strlen(v));
                        sts_val->data[strlen(v)] = '\0';
                } else if (strcmp (k, "meta") == 0) {
                        memcpy (sts_val->meta, v,
                                strlen(v));
                        sts_val->meta[strlen(v)] = '\0';
                } else if (strcmp (k, "failures") == 0) {
                        memcpy (sts_val->failures, v,
                                strlen(v));
                        sts_val->failures[strlen(v)] = '\0';
                } else if (strcmp (k, "checkpoint_time") == 0) {
                        memcpy (sts_val->checkpoint_time, v,
                                strlen(v));
                        sts_val->checkpoint_time[strlen(v)] = '\0';
                } else if (strcmp (k, "checkpoint_time_utc") == 0) {
                        memcpy (sts_val->checkpoint_time_utc, v,
                                strlen(v));
                        sts_val->checkpoint_time_utc[strlen(v)] = '\0';
                } else if (strcmp (k, "checkpoint_completed") == 0) {
                        memcpy (sts_val->checkpoint_completed, v,
                                strlen(v));
                        sts_val->checkpoint_completed[strlen(v)] = '\0';
                } else if (strcmp (k, "checkpoint_completion_time") == 0) {
                        memcpy (sts_val->checkpoint_completion_time, v,
                                strlen(v));
                        sts_val->checkpoint_completion_time[strlen(v)] = '\0';
                } else if (strcmp (k, "checkpoint_completion_time_utc") == 0) {
                        memcpy (sts_val->checkpoint_completion_time_utc, v,
                                strlen(v));
                        sts_val->checkpoint_completion_time_utc[strlen(v)] =
                                '\0';
                }
                GF_FREE(v);
                GF_FREE(k);
        }

        return errno ? -1 : 0;
}


static int
glusterd_gsync_get_status (char *master, char *slave, char *conf_path,
                           char *brick_path, gf_gsync_status_t *sts_val)
{
        /* key + value, where value must be able to accommodate a path */
        char resbuf[256 + PATH_MAX] = {0,};
        runner_t             runner = {0,};

        runinit (&runner);
        runner_add_args  (&runner, GSYNCD_PREFIX"/gsyncd", "-c", NULL);
        runner_argprintf (&runner, "%s", conf_path);
        runner_argprintf (&runner, "--iprefix=%s", DATADIR);
        runner_argprintf (&runner, ":%s", master);
        runner_add_args  (&runner, slave, "--status-get", NULL);
        runner_add_args  (&runner, "--path", brick_path, NULL);

        return glusterd_query_extutil_generic (resbuf, sizeof (resbuf),
                                               &runner, sts_val,
                                               _fcbk_statustostruct);
}

static int
glusterd_gsync_get_param_file (char *prmfile, const char *param, char *master,
                               char *slave, char *conf_path)
{
        runner_t            runner = {0,};

        runinit (&runner);
        runner_add_args  (&runner, GSYNCD_PREFIX"/gsyncd", "-c", NULL);
        runner_argprintf (&runner, "%s", conf_path);
        runner_argprintf (&runner, "--iprefix=%s", DATADIR);
        runner_argprintf (&runner, ":%s", master);
        runner_add_args  (&runner, slave, "--config-get", NULL);
        runner_argprintf (&runner, "%s-file", param);

        return glusterd_query_extutil (prmfile, &runner);
}

static int
gsyncd_getpidfile (char *master, char *slave, char *pidfile,
                   char *conf_path, gf_boolean_t *is_template_in_use)
{
        char                temp_conf_path[PATH_MAX] = "";
        char               *working_conf_path        = NULL;
        glusterd_conf_t    *priv                     = NULL;
        int                 ret                      = -1;
        struct stat         stbuf                    = {0,};
        xlator_t            *this                    = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (this->private);
        GF_ASSERT (conf_path);

        priv = this->private;

        GF_VALIDATE_OR_GOTO ("gsync", master, out);
        GF_VALIDATE_OR_GOTO ("gsync", slave, out);

        snprintf (temp_conf_path, sizeof(temp_conf_path) - 1,
                  "%s/"GSYNC_CONF_TEMPLATE, priv->workdir);

        ret = sys_lstat (conf_path, &stbuf);
        if (!ret) {
                gf_msg_debug (this->name, 0, "Using passed config template(%s).",
                        conf_path);
                working_conf_path = conf_path;
        } else {
                gf_msg (this->name, GF_LOG_WARNING, ENOENT,
                        GD_MSG_FILE_OP_FAILED,
                        "Config file (%s) missing. Looking for template "
                        "config file (%s)", conf_path, temp_conf_path);
                ret = sys_lstat (temp_conf_path, &stbuf);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, ENOENT,
                                GD_MSG_FILE_OP_FAILED,
                                "Template config file (%s) missing.",
                                temp_conf_path);
                        goto out;
                }
                gf_msg (this->name, GF_LOG_INFO, 0, GD_MSG_DEFAULT_TEMP_CONFIG,
                        "Using default config template(%s).",
                        temp_conf_path);
                working_conf_path = temp_conf_path;
                *is_template_in_use = _gf_true;
        }

fetch_data:

        ret = glusterd_gsync_get_param_file (pidfile, "pid", master,
                                             slave, working_conf_path);
        if ((ret == -1) || strlen(pidfile) == 0) {
                if (*is_template_in_use == _gf_false) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_PIDFILE_CREATE_FAILED,
                                "failed to create the pidfile string. "
                                "Trying default config template");
                        working_conf_path = temp_conf_path;
                        *is_template_in_use = _gf_true;
                        goto fetch_data;
                } else {
                        ret = -2;
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_PIDFILE_CREATE_FAILED, "failed to "
                                "create the pidfile string from template "
                                "config");
                        goto out;
                }
        }

        gf_msg_debug (this->name, 0, "pidfile = %s", pidfile);

        ret = open (pidfile, O_RDWR);
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
gsync_status (char *master, char *slave, char *conf_path,
              int *status, gf_boolean_t *is_template_in_use)
{
        char pidfile[PATH_MAX] = {0,};
        int  fd                = -1;

        fd = gsyncd_getpidfile (master, slave, pidfile,
                                conf_path, is_template_in_use);
        if (fd == -2)
                return -1;

        *status = gsync_status_byfd (fd);

        sys_close (fd);

        return 0;
}


static int32_t
glusterd_gsync_volinfo_dict_set (glusterd_volinfo_t *volinfo,
                                 char *key, char *value)
{
        int32_t  ret            = -1;
        char    *gsync_status   = NULL;
        xlator_t *this          = NULL;

        this = THIS;
        GF_ASSERT (this);

        gsync_status = gf_strdup (value);
        if (!gsync_status) {
                gf_msg (this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                        "Unable to allocate memory");
                goto out;
        }

        ret = dict_set_dynstr (volinfo->dict, key, gsync_status);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                        "Unable to set dict");
                goto out;
        }

        ret = 0;
out:
        return ret;
}

static int
glusterd_verify_gsyncd_spawn (char *master, char *slave)
{
        int                 ret = 0;
        runner_t            runner = {0,};
        xlator_t            *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        runinit (&runner);
        runner_add_args (&runner, GSYNCD_PREFIX"/gsyncd",
                         "--verify", "spawning", NULL);
        runner_argprintf (&runner, ":%s", master);
        runner_add_args  (&runner, slave, NULL);
        runner_redir (&runner, STDOUT_FILENO, RUN_PIPE);
        ret = runner_start (&runner);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SPAWNING_CHILD_FAILED,
                        "spawning child failed");
                ret = -1;
                goto out;
        }

        if (runner_end (&runner) != 0)
                ret = -1;

out:
        gf_msg_debug (this->name, 0, "returning %d", ret);
        return ret;
}

static int
gsync_verify_config_options (dict_t *dict, char **op_errstr, char *volname)
{
        char                         **resopt           = NULL;
        int                            i                = 0;
        int                            ret              = -1;
        char                          *subop            = NULL;
        char                          *slave            = NULL;
        char                          *op_name          = NULL;
        char                          *op_value         = NULL;
        char                          *t                = NULL;
        char                           errmsg[PATH_MAX] = "";
        gf_boolean_t                   banned           = _gf_true;
        gf_boolean_t                   op_match         = _gf_true;
        gf_boolean_t                   val_match        = _gf_true;
        struct gsync_config_opt_vals_ *conf_vals        = NULL;
        xlator_t                       *this            = NULL;

        this = THIS;
        GF_ASSERT (this);

        if (dict_get_str (dict, "subop", &subop) != 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
                        "missing subop");
                *op_errstr = gf_strdup ("Invalid config request");
                return -1;
        }

        if (dict_get_str (dict, "slave", &slave) != 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
                        GEOREP" CONFIG: no slave given");
                *op_errstr = gf_strdup ("Slave required");
                return -1;
        }

        if (strcmp (subop, "get-all") == 0)
                return 0;

        if (dict_get_str (dict, "op_name", &op_name) != 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
                        "option name missing");
                *op_errstr = gf_strdup ("Option name missing");
                return -1;
        }

        if (runcmd (GSYNCD_PREFIX"/gsyncd", "--config-check", op_name, NULL)) {
                ret = glusterd_verify_gsyncd_spawn (volname, slave);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_GSYNCD_SPAWN_FAILED, "Unable to spawn "
                                "gsyncd");
                        return 0;
                }

                gf_msg (this->name, GF_LOG_WARNING, EINVAL,
                        GD_MSG_INVALID_ENTRY,
                        "Invalid option %s", op_name);
                *op_errstr = gf_strdup ("Invalid option");

                return -1;
        }

        if (strcmp (subop, "get") == 0)
                return 0;

        t = strtail (subop, "set");
        if (!t)
                t = strtail (subop, "del");
        if (!t || (t[0] && strcmp (t, "-glob") != 0)) {
                gf_msg (this->name, GF_LOG_WARNING, 0, GD_MSG_SUBOP_NOT_FOUND,
                        "unknown subop %s", subop);
                *op_errstr = gf_strdup ("Invalid config request");
                return -1;
        }

        if (strtail (subop, "set") &&
            dict_get_str (dict, "op_value", &op_value) != 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
                        "missing value for set");
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
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_RESERVED_OPTION,
                                "Reserved option %s", op_name);
                        *op_errstr = gf_strdup ("Reserved option");

                        return -1;
                        break;
                }
        }

        /* Check options in gsync_confopt_vals for invalid values */
        for (conf_vals = gsync_confopt_vals; conf_vals->op_name; conf_vals++) {
                op_match = _gf_true;
                for (i = 0; conf_vals->op_name[i] && op_name[i]; i++) {
                        if (conf_vals->op_name[i] == op_name[i] ||
                            (conf_vals->op_name[i] == '_' && op_name[i] == '-'))
                                continue;
                        op_match = _gf_false;
                }

                if (op_match) {
                        if (!op_value)
                                goto out;
                        val_match = _gf_false;
                        for (i = 0; i < conf_vals->no_of_pos_vals; i++) {
                                if(conf_vals->case_sensitive){
                                        if (!strcmp (conf_vals->values[i], op_value))
                                                val_match = _gf_true;
                                } else {
                                        if (!strcasecmp (conf_vals->values[i], op_value))
                                                val_match = _gf_true;
                                }
                        }

                        if (!val_match) {
                                ret = snprintf (errmsg, sizeof(errmsg) - 1,
                                                "Invalid value(%s) for"
                                                " option %s", op_value,
                                                op_name);
                                errmsg[ret] = '\0';

                                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                        GD_MSG_INVALID_ENTRY, "%s", errmsg);
                                *op_errstr = gf_strdup (errmsg);
                                return -1;
                        }
                }
        }
out:
        return 0;
}

static int
glusterd_get_gsync_status_mst_slv (glusterd_volinfo_t *volinfo,
                                   char *slave, char *conf_path,
                                   dict_t *rsp_dict, char *node);

static int
_get_status_mst_slv (dict_t *dict, char *key, data_t *value, void *data)
{
        glusterd_gsync_status_temp_t *param                = NULL;
        char                         *slave                = NULL;
        char                         *slave_buf            = NULL;
        char                         *slave_url            = NULL;
        char                         *slave_vol            = NULL;
        char                         *slave_host           = NULL;
        char                         *errmsg               = NULL;
        char                           conf_path[PATH_MAX] = "";
        int                           ret                  = -1;
        glusterd_conf_t              *priv                 = NULL;
        xlator_t                     *this                 = NULL;
        char                         slv_url[VOLINFO_SLAVE_URL_MAX] = {0};

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);

        param = (glusterd_gsync_status_temp_t *)data;

        GF_VALIDATE_OR_GOTO (this->name, param, out);
        GF_VALIDATE_OR_GOTO (this->name, param->volinfo, out);

        if (this)
                priv = this->private;
        GF_VALIDATE_OR_GOTO (this->name, priv, out);

        strncpy (slv_url, value->data, sizeof(slv_url));
        ret = parse_slave_url (slv_url, &slave);
        if (ret == -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SLAVE_VOL_PARSE_FAIL,
                        "Error in parsing slave: %s!", value->data);
                goto out;
        }

        ret = glusterd_get_slave_info (slave, &slave_url,
                                       &slave_host, &slave_vol, &errmsg);
        if (ret) {
                if (errmsg)
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SLAVEINFO_FETCH_ERROR,
                                "Unable to fetch slave details. Error: %s",
                                errmsg);
                else
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SLAVEINFO_FETCH_ERROR,
                                "Unable to fetch slave details.");
                ret = -1;
                goto out;
        }

        ret = snprintf (conf_path, sizeof(conf_path) - 1,
                        "%s/"GEOREP"/%s_%s_%s/gsyncd.conf",
                        priv->workdir, param->volinfo->volname,
                        slave_host, slave_vol);
        conf_path[ret] = '\0';

        ret = glusterd_get_gsync_status_mst_slv(param->volinfo,
                                                slave, conf_path,
                                                param->rsp_dict,
                                                param->node);
out:

        if (errmsg)
                GF_FREE (errmsg);

        if (slave_buf)
                GF_FREE(slave_buf);

        gf_msg_debug (this->name, 0, "Returning %d.", ret);
        return ret;
}


static int
_get_max_gsync_slave_num (dict_t *dict, char *key, data_t *value, void *data)
{
        int  tmp_slvnum = 0;
        int *slvnum = (int *)data;

        sscanf (key, "slave%d", &tmp_slvnum);
        if (tmp_slvnum > *slvnum)
                *slvnum = tmp_slvnum;

        return 0;
}

static int
_get_slave_idx_slave_voluuid (dict_t *dict, char *key, data_t *value,
                              void *data)
{
        char  *slave_info                    = NULL;
        xlator_t  *this                      = NULL;
        struct slave_vol_config *slave_cfg   = NULL;
        int  i                               = 0;
        int  ret                             = -1;
        unsigned  tmp_slvnum                 = 0;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);

        slave_cfg = data;

        if (value)
                slave_info = value->data;

        if (!(slave_info) || strlen (slave_info) == 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_INVALID_SLAVE,
                        "Invalid slave in dict");
                ret = -2;
                goto out;
        }

        /* slave format:
         * master_node_uuid:ssh://slave_host::slave_vol:slave_voluuid */
        while (i++ < 5) {
                slave_info = strchr (slave_info, ':');
                if (slave_info)
                        slave_info++;
                else {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SLAVE_VOL_PARSE_FAIL,
                                "slave_info becomes NULL!");
                        ret = -2;
                        goto out;
                }
        }
        if (strcmp (slave_info, slave_cfg->slave_voluuid) == 0) {
                gf_msg_debug (this->name, 0, "Same slave volume "
                              "already present %s",
                              slave_cfg->slave_voluuid);
                ret = -1;

                sscanf (key, "slave%d", &tmp_slvnum);
                slave_cfg->old_slvidx = tmp_slvnum;

                gf_msg_debug (this->name, 0, "and "
                              "its index is: %d", tmp_slvnum);
                goto out;
        }

        ret = 0;
out:
        return ret;
}

static int
glusterd_remove_slave_in_info (glusterd_volinfo_t *volinfo, char *slave,
                               char **op_errstr)
{
        int   zero_slave_entries = _gf_true;
        int   ret = 0;
        char *slavekey = NULL;
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (volinfo);
        GF_ASSERT (slave);

        do {
                ret = glusterd_get_slave (volinfo, slave, &slavekey);
                if (ret < 0 && zero_slave_entries) {
                        ret++;
                        goto out;
                }
                zero_slave_entries = _gf_false;
                dict_del (volinfo->gsync_slaves, slavekey);
        } while (ret >= 0);

        ret = glusterd_store_volinfo (volinfo,
                                      GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
                 *op_errstr = gf_strdup ("Failed to store the Volume"
                                         "information");
                goto out;
        }
 out:
        gf_msg_debug (this->name, 0, "returning %d", ret);
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
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

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
        ret = gf_uuid_parse (slaveentry, uuid);
        *t = ':';

 out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

static int
update_slave_voluuid (dict_t *dict, char *key, data_t *value, void *data)
{
        char                         *slave                = NULL;
        char                         *slave_url            = NULL;
        char                         *slave_vol            = NULL;
        char                         *slave_host           = NULL;
        char                         *errmsg               = NULL;
        xlator_t                     *this                 = NULL;
        int                          ret                   = -1;
        char                         slv_url[VOLINFO_SLAVE_URL_MAX] = {0};
        char                         slave_voluuid[GF_UUID_BUF_SIZE] = {0};
        char                         *slave_info           = NULL;
        char                         *new_value            = NULL;
        char                         *same_key             = NULL;
        int                          cnt                   = 0;
        gf_boolean_t                 *voluuid_updated      = NULL;

        this = THIS;

        voluuid_updated = data;
        slave_info = value->data;
        gf_msg_debug (this->name, 0, "slave_info: %s!", slave_info);

        /* old slave format:
         * master_node_uuid:ssh://slave_host::slave_vol
         * New slave format:
         * master_node_uuid:ssh://slave_host::slave_vol:slave_voluuid */
        while (slave_info) {
                slave_info = strchr (slave_info, ':');
                if (slave_info)
                        cnt++;
                else
                        break;

                slave_info++;
        }

        gf_msg_debug (this->name, 0, "cnt: %d", cnt);
        /* check whether old slave format and update vol uuid if old format.
         * With volume uuid, number of ':' is 5 and is 4 without.
         */
        if (cnt == 4) {
                strncpy (slv_url, value->data, sizeof(slv_url));

                ret = parse_slave_url (slv_url, &slave);
                if (ret == -1) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SLAVE_VOL_PARSE_FAIL,
                                "Error in parsing slave: %s!", value->data);
                        goto out;
                }

                ret = glusterd_get_slave_info (slave, &slave_url,
                                       &slave_host, &slave_vol, &errmsg);
                if (ret) {
                        if (errmsg)
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_SLAVEINFO_FETCH_ERROR,
                                        "Unable to fetch slave details. Error: %s",
                                        errmsg);
                        else
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_SLAVEINFO_FETCH_ERROR,
                                        "Unable to fetch slave details.");
                        ret = -1;
                        goto out;
                }

                ret = glusterd_get_slave_voluuid (slave_host, slave_vol,
                                                  slave_voluuid);
                if ((ret) || (strlen(slave_voluuid) == 0)) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_REMOTE_VOL_UUID_FAIL,
                                "Unable to get remote volume uuid"
                                "slavehost:%s slavevol:%s",
                                 slave_host, slave_vol);
                        /* Avoiding failure due to remote vol uuid fetch */
                        ret = 0;
                        goto out;
                }
                ret = gf_asprintf (&new_value,  "%s:%s",
                                    value->data, slave_voluuid);
                ret = gf_asprintf (&same_key, "%s", key);

                /* delete old key and add new value */
                dict_del (dict, key);

                /* set new value for the same key*/
                ret = dict_set_dynstr (dict, same_key, new_value);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_REMOTE_VOL_UUID_FAIL,
                                "Error in setting dict value"
                                "new_value :%s", new_value);
                        goto out;
                }
                *voluuid_updated = _gf_true;
        }

        ret = 0;
out:
        if (errmsg)
                GF_FREE (errmsg);

        gf_msg_debug (this->name, 0, "Returning %d.", ret);
        return ret;
}

static int
glusterd_update_slave_voluuid_slaveinfo (glusterd_volinfo_t *volinfo)
{
        int   ret = -1;
        xlator_t *this = NULL;
        gf_boolean_t voluuid_updated = _gf_false;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);
        GF_VALIDATE_OR_GOTO (this->name, volinfo, out);

        ret = dict_foreach (volinfo->gsync_slaves, update_slave_voluuid,
                            &voluuid_updated);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REMOTE_VOL_UUID_FAIL, "Error in updating"
                        "volinfo");
                goto out;
        }

        if (_gf_true == voluuid_updated) {
                ret = glusterd_store_volinfo (volinfo,
                                          GLUSTERD_VOLINFO_VER_AC_INCREMENT);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_VOLINFO_STORE_FAIL, "Error in storing"
                                "volinfo");
                        goto out;
                }
        }

        ret = 0;
out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_check_gsync_running_local (char *master, char *slave,
                                    char *conf_path,
                                    gf_boolean_t *is_run)
{
        int                 ret    = -1;
        int                 ret_status = 0;
        gf_boolean_t        is_template_in_use = _gf_false;
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (master);
        GF_ASSERT (slave);
        GF_ASSERT (is_run);

        *is_run = _gf_false;
        ret = gsync_status (master, slave, conf_path,
                            &ret_status, &is_template_in_use);
        if (ret == 0 && ret_status == 0)
                *is_run = _gf_true;
        else if (ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING, 0, GD_MSG_VALIDATE_FAILED,
                        GEOREP" validation failed");
                goto out;
        }
        ret = 0;
 out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;

}

static int
glusterd_store_slave_in_info (glusterd_volinfo_t *volinfo, char *slave,
                              char *host_uuid, char *slave_voluuid,
                              char **op_errstr, gf_boolean_t is_force)
{
        int    ret = 0;
        int    maxslv = 0;
        char **linearr = NULL;
        char  *value = NULL;
        char  *slavekey = NULL;
        char  *slaveentry = NULL;
        char   key[512] = {0, };
        char  *t = NULL;
        xlator_t *this = NULL;
        struct slave_vol_config slave1  = {{0},};

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (volinfo);
        GF_ASSERT (slave);
        GF_ASSERT (host_uuid);
        GF_VALIDATE_OR_GOTO (this->name, slave_voluuid, out);

        ret = glusterd_get_slave (volinfo, slave, &slavekey);
        switch (ret) {
        case -2:
                ret = -1;
                goto out;
        case -1:
                                break;
        default:
                if (!is_force)
                        GF_ASSERT (ret > 0);
                ret = dict_get_str (volinfo->gsync_slaves, slavekey, &slaveentry);
                GF_ASSERT (ret == 0);

                /* same-name + same-uuid slave entries should have been filtered
                 * out in glusterd_op_verify_gsync_start_options(), so we can
                 * assert an uuid mismatch
                 */
                t = strtail (slaveentry, host_uuid);
                if (!is_force)
                        GF_ASSERT (!t || *t != ':');

                if (is_force) {
                        gf_msg_debug (this->name, 0, GEOREP" has already "
                                      "been invoked for the %s (master) and "
                                      "%s (slave). Allowing without saving "
                                      "info again due to force command.",
                                      volinfo->volname, slave);
                        ret = 0;
                        goto out;
                }

                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_INVOKE_ERROR,
                        GEOREP" has already been invoked for "
                         "the %s (master) and %s (slave) from a different "
                         "machine", volinfo->volname, slave);
                *op_errstr = gf_strdup (GEOREP" already running in "
                                        "another machine");
                ret = -1;
                goto out;
        }

        ret = glusterd_urltransform_single (slave, "normalize", &linearr);
        if (ret == -1)
                goto out;

        ret = gf_asprintf (&value,  "%s:%s:%s", host_uuid,
                                    linearr[0], slave_voluuid);

        glusterd_urltransform_free (linearr, 1);
        if (ret  == -1)
                goto out;

        /* Given the slave volume uuid, check and get any existing slave */
        strncpy (slave1.slave_voluuid, slave_voluuid, GF_UUID_BUF_SIZE);
        ret = dict_foreach (volinfo->gsync_slaves,
                            _get_slave_idx_slave_voluuid, &slave1);

        if (ret == 0) { /* New slave */
                dict_foreach (volinfo->gsync_slaves, _get_max_gsync_slave_num,
                              &maxslv);
                snprintf (key, 512, "slave%d", maxslv + 1);

                ret = dict_set_dynstr (volinfo->gsync_slaves, key, value);
                if (ret) {
                        GF_FREE (value);
                        goto out;
                }
        } else if (ret == -1) { /* Existing slave */
                snprintf (key, 512, "slave%d", slave1.old_slvidx);

                /* Delete present slave info(with old hostname) */
                dict_del (volinfo->gsync_slaves, key);

                gf_msg_debug (this->name, 0, "Replacing key:%s with new value"
                              ":%s", key, value);

                /* Add new slave's value, with the same slave index */
                ret = dict_set_dynstr (volinfo->gsync_slaves, key, value);
                if (ret) {
                        GF_FREE (value);
                        goto out;
                }
        } else {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REMOTE_VOL_UUID_FAIL,
                        "_get_slave_idx_slave_voluuid failed!");
                GF_FREE (value);
                ret = -1;
                goto out;
        }

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
                                        char *slave, char *conf_path,
                                        char *statefile, char **op_errstr,
                                        gf_boolean_t is_force)
{
        int                     ret = -1;
        int                     ret_status = 0;
        gf_boolean_t            is_template_in_use = _gf_false;
        char                    msg[2048] = {0};
        uuid_t                  uuid = {0};
        xlator_t               *this = NULL;
        struct stat             stbuf = {0,};
        char                    statefiledir[PATH_MAX] = {0,};
        char                    *statedir = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (volinfo);
        GF_ASSERT (slave);
        GF_ASSERT (op_errstr);
        GF_ASSERT (conf_path);
        GF_ASSERT (this && this->private);

        if (GLUSTERD_STATUS_STARTED != volinfo->status) {
                snprintf (msg, sizeof (msg), "Volume %s needs to be started "
                          "before "GEOREP" start", volinfo->volname);
                goto out;
        }

        /* check session directory as statefile may not present
         * during upgrade */
        strncpy (statefiledir, statefile, sizeof(statefiledir));
        statedir = dirname (statefiledir);

        ret = sys_lstat (statedir, &stbuf);
        if (ret) {
                snprintf (msg, sizeof (msg), "Session between %s and %s has"
                          " not been created. Please create session and retry.",
                          volinfo->volname, slave);
                gf_msg (this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
                        "%s statefile: %s", msg, statefile);
                *op_errstr = gf_strdup (msg);
                goto out;
        }

        /* Check if the gsync slave info is stored. If not
         * session has not been created */
        ret = glusterd_gsync_get_uuid (slave, volinfo, uuid);
        if (ret) {
                snprintf (msg, sizeof (msg), "Session between %s and %s has"
                          " not been created. Please create session and retry.",
                          volinfo->volname, slave);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SESSION_CREATE_ERROR,
                        "%s", msg);
                goto out;
        }

        /*Check if the gsync is already started in cmd. inited host
         * If so initiate add it into the glusterd's priv*/
        ret = gsync_status (volinfo->volname, slave, conf_path,
                            &ret_status, &is_template_in_use);
        if (ret == 0) {
                if ((ret_status == 0) && !is_force) {
                        snprintf (msg, sizeof (msg), GEOREP " session between"
                                  " %s & %s already started", volinfo->volname,
                                  slave);
                        ret = -1;
                        goto out;
                }
        } else if (ret == -1) {
                snprintf (msg, sizeof (msg), GEOREP" start option "
                          "validation failed ");
                goto out;
        }

        if (is_template_in_use == _gf_true) {
                snprintf (msg, sizeof (msg), GEOREP" start "
                          "failed : pid-file entry missing "
                          "in config file.");
                ret = -1;
                goto out;
        }

        ret = glusterd_verify_gsyncd_spawn (volinfo->volname, slave);
        if (ret && !is_force) {
                snprintf (msg, sizeof (msg), "Unable to spawn gsyncd");
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_GSYNCD_SPAWN_FAILED,
                        "%s", msg);
        }
out:
        if (ret && (msg[0] != '\0')) {
                *op_errstr = gf_strdup (msg);
        }
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

void
glusterd_check_geo_rep_configured (glusterd_volinfo_t *volinfo,
                                   gf_boolean_t *flag)
{

        GF_ASSERT (volinfo);
        GF_ASSERT (flag);

        if (volinfo->gsync_slaves->count)
                *flag = _gf_true;
        else
                *flag = _gf_false;

        return;
}

/*
 * is_geo_rep_active:
 *      This function reads the state_file and sets is_active to 1 if the
 *      monitor status is neither "Stopped" or "Created"
 *
 * RETURN VALUE:
 *       0: On successful read of state_file.
 *      -1: error.
 */

static int
is_geo_rep_active (glusterd_volinfo_t *volinfo, char *slave,
                   char *conf_path, int *is_active)
{
        dict_t                 *confd                      = NULL;
        char                   *statefile                  = NULL;
        char                   *master                     = NULL;
        char                    monitor_status[PATH_MAX]   = "";
        int                     ret                        = -1;
        xlator_t               *this                       = NULL;

        this = THIS;
        GF_ASSERT (this);

        master = volinfo->volname;

        confd = dict_new ();
        if (!confd) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_CREATE_FAIL,
                        "Not able to create dict.");
                goto out;
        }

        ret = glusterd_gsync_get_config (master, slave, conf_path,
                                         confd);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GET_CONFIG_INFO_FAILED,
                        "Unable to get configuration data "
                        "for %s(master), %s(slave)", master, slave);
                ret = -1;
                goto out;
        }

        ret = dict_get_param (confd, "state_file", &statefile);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Unable to get state_file's name "
                        "for %s(master), %s(slave). Please check gsync "
                        "config file.", master, slave);
                ret = -1;
                goto out;
        }

        ret = glusterd_gsync_read_frm_status (statefile, monitor_status,
                                              sizeof (monitor_status));
        if (ret <= 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STAT_FILE_READ_FAILED,
                        "Unable to read the status file for %s(master), "
                        "%s(slave)", master, slave);
                strncpy (monitor_status, "defunct", sizeof (monitor_status));
        }

        if ((!strcmp(monitor_status, "Stopped")) ||
            (!strcmp(monitor_status, "Created"))) {
                *is_active = 0;
        } else {
                *is_active = 1;
        }
        ret = 0;
out:
        if (confd)
                dict_unref (confd);
        return ret;
}

/*
 * _get_slave_status:
 *      Called for each slave in the volume from dict_foreach.
 *      It calls is_geo_rep_active to get the monitor status.
 *
 * RETURN VALUE:
 *      0: On successful read of state_file from is_geo_rep_active.
 *         When it is found geo-rep is already active from previous calls.
 *         When there is no slave.
 *     -1: On error.
 */

int
_get_slave_status (dict_t *dict, char *key, data_t *value, void *data)
{
        gsync_status_param_t *param                        = NULL;
        char                 *slave                        = NULL;
        char                 *slave_url                    = NULL;
        char                 *slave_vol                    = NULL;
        char                 *slave_host                   = NULL;
        char                 *errmsg                       = NULL;
        char                           conf_path[PATH_MAX] = "";
        int                   ret                          = -1;
        glusterd_conf_t      *priv                         = NULL;
        xlator_t             *this                         = NULL;

        param = (gsync_status_param_t *)data;

        GF_ASSERT (param);
        GF_ASSERT (param->volinfo);
        if (param->is_active) {
                ret = 0;
                goto out;
        }

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        if (priv == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLUSTERD_PRIV_NOT_FOUND,
                        "priv of glusterd not present");
                goto out;
        }

        slave = strchr (value->data, ':');
        if (!slave) {
                ret = 0;
                goto out;
        }
        slave++;

        ret = glusterd_get_slave_info (slave, &slave_url,
                                       &slave_host, &slave_vol, &errmsg);
        if (ret) {
                if (errmsg)
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SLAVEINFO_FETCH_ERROR, "Unable to fetch"
                                " slave details. Error: %s", errmsg);
                else
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SLAVEINFO_FETCH_ERROR,
                                "Unable to fetch slave details.");
                ret = -1;
                goto out;
        }

        ret = snprintf (conf_path, sizeof(conf_path) - 1,
                        "%s/"GEOREP"/%s_%s_%s/gsyncd.conf",
                        priv->workdir, param->volinfo->volname,
                        slave_host, slave_vol);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_CONF_PATH_ASSIGN_FAILED,
                        "Unable to assign conf_path.");
                ret = -1;
                goto out;
        }
        conf_path[ret] = '\0';

        ret = is_geo_rep_active (param->volinfo,slave, conf_path,
                                 &param->is_active);
out:
        GF_FREE(errmsg);
        return ret;
}

/* glusterd_check_geo_rep_running:
 *          Checks if any geo-rep session is running for the volume.
 *
 *    RETURN VALUE:
 *          Sets param.active to true if any geo-rep session is active.
 *    This function sets op_errstr during some error and when any geo-rep
 *    session is active. It is caller's responsibility to free op_errstr
 *    in above cases.
 */

int
glusterd_check_geo_rep_running (gsync_status_param_t *param, char **op_errstr)
{
        char                    msg[2048]   = {0,};
        gf_boolean_t            enabled     = _gf_false;
        int                     ret         = 0;
        xlator_t               *this        = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (param);
        GF_ASSERT (param->volinfo);
        GF_ASSERT (op_errstr);

        glusterd_check_geo_rep_configured (param->volinfo, &enabled);

        if (enabled) {
                ret = dict_foreach (param->volinfo->gsync_slaves,
                                    _get_slave_status, param);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SLAVEINFO_FETCH_ERROR,
                                "_get_slave_satus failed");
                        snprintf (msg, sizeof(msg), GEOREP" Unable to"
                                  " get the status of active "GEOREP""
                                  " session for the volume '%s'.\n"
                                  " Please check the log file for"
                                  " more info.", param->volinfo->volname);
                        *op_errstr = gf_strdup (msg);
                        ret = -1;
                        goto out;
                }

                if (param->is_active) {
                        snprintf (msg, sizeof(msg), GEOREP" sessions"
                                  " are active for the volume %s.\nStop"
                                  " "GEOREP " sessions involved in this"
                                  " volume. Use 'volume "GEOREP
                                  " status' command for more info.",
                                  param->volinfo->volname);
                        *op_errstr = gf_strdup (msg);
                        goto out;
                 }
         }
 out:
        return ret;
}

static int
glusterd_op_verify_gsync_running (glusterd_volinfo_t *volinfo,
                                  char *slave, char *conf_path,
                                  char **op_errstr)
{
        int        pfd               = -1;
        int        ret               = -1;
        char       msg[2048]         = {0};
        char       pidfile[PATH_MAX] = {0,};
        gf_boolean_t is_template_in_use = _gf_false;
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (THIS && THIS->private);
        GF_ASSERT (volinfo);
        GF_ASSERT (slave);
        GF_ASSERT (conf_path);
        GF_ASSERT (op_errstr);

        if (GLUSTERD_STATUS_STARTED != volinfo->status) {
                snprintf (msg, sizeof (msg), "Volume %s needs to be started "
                          "before "GEOREP" start", volinfo->volname);

                goto out;
        }

        pfd = gsyncd_getpidfile (volinfo->volname, slave, pidfile,
                                 conf_path, &is_template_in_use);
        if (pfd == -2) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_VALIDATE_FAILED,
                        GEOREP" stop validation failed for %s & %s",
                        volinfo->volname, slave);
                ret = -1;
                goto out;
        }
        if (gsync_status_byfd (pfd) == -1) {
                snprintf (msg, sizeof (msg), GEOREP" session b/w %s & %s is "
                          "not running on this node.", volinfo->volname,
                          slave);
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_SESSION_INACTIVE,
                        "%s", msg);
                ret = -1;
                /* monitor gsyncd already dead */
                goto out;
        }

        if (is_template_in_use) {
                snprintf (msg, sizeof (msg), "pid-file entry missing in "
                          "the config file(%s).", conf_path);
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_PIDFILE_NOT_FOUND,
                        "%s", msg);
                ret = -1;
                goto out;
        }

        if (pfd < 0)
                goto out;

        ret = 0;
out:
        if (ret && (msg[0] != '\0')) {
                *op_errstr = gf_strdup (msg);
        }
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

static int
glusterd_verify_gsync_status_opts (dict_t *dict, char **op_errstr)
{
        char               *slave           = NULL;
        char               *volname         = NULL;
        char               errmsg[PATH_MAX] = {0, };
        gf_boolean_t        exists          = _gf_false;
        glusterd_volinfo_t *volinfo         = NULL;
        int                 ret             = 0;
        char               *conf_path       = NULL;
        char               *slave_url       = NULL;
        char               *slave_host      = NULL;
        char               *slave_vol       = NULL;
        glusterd_conf_t    *priv            = NULL;
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        if (THIS)
                priv = THIS->private;
        if (priv == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLUSTERD_PRIV_NOT_FOUND,
                        "priv of glusterd not present");
                *op_errstr = gf_strdup ("glusterd defunct");
                goto out;
        }

        ret = dict_get_str (dict, "master", &volname);
        if (ret < 0) {
                ret = 0;
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        ret = glusterd_volinfo_find (volname, &volinfo);
        if ((ret) || (!exists)) {
                gf_msg (this->name, GF_LOG_WARNING, 0, GD_MSG_VOL_NOT_FOUND,
                        "volume name does not exist");
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

        ret = glusterd_get_slave_details_confpath (volinfo, dict, &slave_url,
                                                   &slave_host, &slave_vol,
                                                   &conf_path, op_errstr);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SLAVEINFO_FETCH_ERROR,
                        "Unable to fetch slave  or confpath details.");
                ret = -1;
                goto out;
        }

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}


int
glusterd_op_gsync_args_get (dict_t *dict, char **op_errstr,
                            char **master, char **slave, char **host_uuid)
{

        int             ret = -1;
        xlator_t        *this = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        if (master) {
                ret = dict_get_str (dict, "master", master);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_DICT_GET_FAILED, "master not found");
                        *op_errstr = gf_strdup ("master not found");
                        goto out;
                }
        }

        if (slave) {
                ret = dict_get_str (dict, "slave", slave);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_DICT_GET_FAILED, "slave not found");
                        *op_errstr = gf_strdup ("slave not found");
                        goto out;
                }
        }

        if (host_uuid) {
                ret = dict_get_str (dict, "host-uuid", host_uuid);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_DICT_GET_FAILED, "host_uuid not found");
                        *op_errstr = gf_strdup ("host_uuid not found");
                        goto out;
                }
        }

        ret = 0;
out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_op_stage_sys_exec (dict_t *dict, char **op_errstr)
{
        char             errmsg[PATH_MAX]          = "";
        char            *command                   = NULL;
        char             command_path[PATH_MAX]    = "";
        struct stat      st                        = {0,};
        int              ret                       = -1;
        glusterd_conf_t *conf                      = NULL;
        xlator_t        *this                      = NULL;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        if (conf->op_version < 2) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_UNSUPPORTED_VERSION,
                        "Op Version not supported.");
                snprintf (errmsg, sizeof(errmsg), "One or more nodes do not"
                          " support the required op version.");
                *op_errstr = gf_strdup (errmsg);
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "command", &command);
        if (ret) {
                strcpy (errmsg, "internal error");
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Unable to get command from dict");
                goto out;
        }

        /* enforce local occurrence of the command */
        if (strchr (command, '/')) {
                strcpy (errmsg, "invalid command name");
                ret = -1;
                goto out;
        }

        sprintf (command_path, GSYNCD_PREFIX"/peer_%s", command);
        /* check if it's executable */
        ret = sys_access (command_path, X_OK);
        if (!ret)
                /* check if it's a regular file */
                ret = sys_stat (command_path, &st);
        if (!ret && !S_ISREG (st.st_mode))
                ret = -1;

out:
        if (ret) {
                if (errmsg[0] == '\0') {
                        if (command)
                                snprintf (errmsg, sizeof (errmsg),
                                          "gsync peer_%s command not found.",
                                          command);
                        else
                                snprintf (errmsg, sizeof (errmsg), "%s",
                                          "gsync peer command was not "
                                          "specified");
                }
                *op_errstr = gf_strdup (errmsg);
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_PEER_CMD_ERROR,
                        "%s", errmsg);
        }

        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_op_stage_copy_file (dict_t *dict, char **op_errstr)
{
        char             abs_filename[PATH_MAX] = "";
        char             errmsg[PATH_MAX]       = "";
        char            *filename               = NULL;
        char            *host_uuid              = NULL;
        char             uuid_str [64]          = {0};
        int              ret                    = -1;
        glusterd_conf_t *priv                   = NULL;
        struct stat      stbuf                  = {0,};
        xlator_t         *this                  = NULL;
        char             workdir[PATH_MAX]      = {0,};
        char             realpath_filename[PATH_MAX] = {0,};
        char             realpath_workdir[PATH_MAX]  = {0,};

        this = THIS;
        GF_ASSERT (this);

        if (THIS)
                priv = THIS->private;
        if (priv == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLUSTERD_PRIV_NOT_FOUND,
                        "priv of glusterd not present");
                *op_errstr = gf_strdup ("glusterd defunct");
                goto out;
        }

        if (priv->op_version < 2) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_UNSUPPORTED_VERSION,
                        "Op Version not supported.");
                snprintf (errmsg, sizeof(errmsg), "One or more nodes do not"
                          " support the required op version.");
                *op_errstr = gf_strdup (errmsg);
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "host-uuid", &host_uuid);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Unable to fetch host-uuid from dict.");
                goto out;
        }

        uuid_utoa_r (MY_UUID, uuid_str);
        if (!strcmp (uuid_str, host_uuid)) {
                ret = dict_get_str (dict, "source", &filename);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Unable to fetch filename from dict.");
                        *op_errstr = gf_strdup ("command unsuccessful");
                        goto out;
                }
                snprintf (abs_filename, sizeof(abs_filename),
                          "%s/%s", priv->workdir, filename);

                if (!realpath (priv->workdir, realpath_workdir)) {
                        snprintf (errmsg, sizeof (errmsg), "Failed to get "
                                  "realpath of %s: %s", priv->workdir,
                                  strerror (errno));
                        *op_errstr = gf_strdup (errmsg);
                        ret = -1;
                        goto out;
                }

                if (!realpath (abs_filename, realpath_filename)) {
                        snprintf (errmsg, sizeof (errmsg), "Failed to get "
                                  "realpath of %s: %s", filename,
                                  strerror (errno));
                        *op_errstr = gf_strdup (errmsg);
                        ret = -1;
                        goto out;
                }

                /* Add Trailing slash to workdir, without slash strncmp
                   will succeed for /var/lib/glusterd_bad */
                snprintf (workdir, sizeof(workdir), "%s/", realpath_workdir);

                /* Protect against file copy outside $workdir */
                if (strncmp (workdir, realpath_filename, strlen (workdir))) {
                        snprintf (errmsg, sizeof (errmsg), "Source file"
                                  " is outside of %s directory", priv->workdir);
                        *op_errstr = gf_strdup (errmsg);
                        ret = -1;
                        goto out;
                }

                ret = sys_lstat (abs_filename, &stbuf);
                if (ret) {
                        snprintf (errmsg, sizeof (errmsg), "Source file"
                                  " does not exist in %s", priv->workdir);
                        *op_errstr = gf_strdup (errmsg);
                        goto out;
                }

                if (!S_ISREG(stbuf.st_mode)) {
                        snprintf (errmsg, sizeof (errmsg), "Source file"
                                 " is not a regular file.");
                        *op_errstr = gf_strdup (errmsg);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SRC_FILE_ERROR,
                                "%s", errmsg);
                        ret = -1;
                        goto out;
                }
        }

        ret = 0;
out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_get_statefile_name (glusterd_volinfo_t *volinfo, char *slave,
                             char *conf_path, char **statefile,
                             gf_boolean_t *is_template_in_use)
{
        char            *master                   = NULL;
        char            *buf                      = NULL;
        char            *working_conf_path        = NULL;
        char             temp_conf_path[PATH_MAX] = "";
        dict_t          *confd                    = NULL;
        glusterd_conf_t *priv                     = NULL;
        int              ret                      = -1;
        struct stat      stbuf                    = {0,};
        xlator_t        *this                     = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (this->private);
        GF_ASSERT (volinfo);
        GF_ASSERT (conf_path);
        GF_ASSERT (is_template_in_use);

        master = volinfo->volname;

        confd = dict_new ();
        if (!confd) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_CREATE_FAIL,
                        "Unable to create new dict");
                goto out;
        }

        priv = THIS->private;

        snprintf (temp_conf_path, sizeof(temp_conf_path) - 1,
                  "%s/"GSYNC_CONF_TEMPLATE, priv->workdir);

        ret = sys_lstat (conf_path, &stbuf);
        if (!ret) {
                gf_msg (this->name, GF_LOG_INFO, 0, GD_MSG_CONFIG_INFO,
                        "Using passed config template(%s).",
                        conf_path);
                working_conf_path = conf_path;
        } else {
                gf_msg (this->name, GF_LOG_WARNING, ENOENT,
                        GD_MSG_FILE_OP_FAILED,
                        "Config file (%s) missing. Looking for template config"
                        " file (%s)", conf_path, temp_conf_path);
                ret = sys_lstat (temp_conf_path, &stbuf);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, ENOENT,
                                GD_MSG_FILE_OP_FAILED, "Template "
                                "config file (%s) missing.", temp_conf_path);
                        goto out;
                }
                gf_msg (this->name, GF_LOG_INFO, 0, GD_MSG_DEFAULT_TEMP_CONFIG,
                        "Using default config template(%s).", temp_conf_path);
                working_conf_path = temp_conf_path;
                *is_template_in_use = _gf_true;
        }

fetch_data:
        ret = glusterd_gsync_get_config (master, slave, working_conf_path,
                                         confd);
        if (ret) {
                if (*is_template_in_use == _gf_false) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_GET_CONFIG_INFO_FAILED,
                                "Unable to get configuration data "
                                "for %s(master), %s(slave). "
                                "Trying template config.",
                                master, slave);
                        working_conf_path = temp_conf_path;
                        *is_template_in_use = _gf_true;
                        goto fetch_data;
                } else {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_GET_CONFIG_INFO_FAILED,
                                "Unable to get configuration data "
                                "for %s(master), %s(slave) from "
                                "template config",
                                master, slave);
                        goto out;
                }
        }

        ret = dict_get_param (confd, "state_file", &buf);
        if (ret) {
                if (*is_template_in_use == _gf_false) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Unable to get state_file's name. "
                                "Trying template config.");
                        working_conf_path = temp_conf_path;
                        *is_template_in_use = _gf_true;
                        goto fetch_data;
                } else {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_GET_STATEFILE_NAME_FAILED,
                                "Unable to get state_file's "
                                "name from template.");
                        goto out;
                }
        }

        ret = 0;
 out:
        if (buf) {
                *statefile = gf_strdup(buf);
                if (!*statefile)
                        ret = -1;
        }

        if (confd)
                dict_unref (confd);

        gf_msg_debug (this->name, 0, "Returning %d ", ret);
        return ret;
}

int
glusterd_create_status_file (char *master, char *slave, char *slave_host,
                             char *slave_vol, char *status)
{
        int                ret    = -1;
        runner_t           runner = {0,};
        glusterd_conf_t   *priv   = NULL;
        xlator_t          *this   = NULL;

        this = THIS;
        GF_ASSERT (this);

        if (THIS)
                priv = THIS->private;
        if (priv == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLUSTERD_PRIV_NOT_FOUND,
                        "priv of glusterd not present");
                goto out;
        }

        if (!status) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_STATUS_NULL,
                        "Status Empty");
                goto out;
        }
        gf_msg_debug (this->name, 0, "slave = %s", slave);

        runinit (&runner);
        runner_add_args (&runner, GSYNCD_PREFIX"/gsyncd", "--create",
                         status, "-c", NULL);
        runner_argprintf (&runner, "%s/"GEOREP"/%s_%s_%s/gsyncd.conf",
                          priv->workdir, master, slave_host, slave_vol);
        runner_argprintf (&runner, "--iprefix=%s", DATADIR);
        runner_argprintf (&runner, ":%s", master);
        runner_add_args  (&runner, slave, NULL);
        synclock_unlock (&priv->big_lock);
        ret = runner_run (&runner);
        synclock_lock (&priv->big_lock);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STATUSFILE_CREATE_FAILED,
                        "Creating status file failed.");
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        gf_msg_debug (this->name, 0, "returning %d", ret);
        return ret;
}

static int
glusterd_verify_slave (char *volname, char *slave_url, char *slave_vol,
                       int ssh_port, char **op_errstr,
                       gf_boolean_t *is_force_blocker)
{
        int32_t          ret                     = -1;
        runner_t         runner                  = {0,};
        char             log_file_path[PATH_MAX] = "";
        char             buf[PATH_MAX]           = "";
        char            *tmp                     = NULL;
        char            *slave_url_buf           = NULL;
        char            *save_ptr                = NULL;
        char            *slave_user              = NULL;
        char            *slave_ip                = NULL;
        glusterd_conf_t *priv                    = NULL;
        xlator_t        *this                    = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (volname);
        GF_ASSERT (slave_url);
        GF_ASSERT (slave_vol);

        /* Fetch the slave_user and slave_ip from the slave_url.
         * If the slave_user is not present. Use "root"
         */
        if (strstr(slave_url, "@")) {
                slave_url_buf = gf_strdup (slave_url);
                if (!slave_url_buf)
                        goto out;

                slave_user = strtok_r (slave_url_buf, "@", &save_ptr);
                slave_ip = strtok_r (NULL, "@", &save_ptr);
        } else {
                slave_user = "root";
                slave_ip = slave_url;
        }

        if (!slave_user || !slave_ip) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_SLAVE_URL_INVALID,
                        "Invalid slave url.");
                goto out;
        }

        snprintf (log_file_path, sizeof(log_file_path),
                  DEFAULT_LOG_FILE_DIRECTORY"/create_verify_log");

        runinit (&runner);
        runner_add_args  (&runner, GSYNCD_PREFIX"/gverify.sh", NULL);
        runner_argprintf (&runner, "%s", volname);
        runner_argprintf (&runner, "%s", slave_user);
        runner_argprintf (&runner, "%s", slave_ip);
        runner_argprintf (&runner, "%s", slave_vol);
        runner_argprintf (&runner, "%d", ssh_port);
        runner_argprintf (&runner, "%s", log_file_path);
        gf_msg_debug (this->name, 0, "gverify Args = %s %s %s %s %s %s %s",
                      runner.argv[0], runner.argv[1], runner.argv[2],
                      runner.argv[3], runner.argv[4], runner.argv[5],
                      runner.argv[6]);
        runner_redir (&runner, STDOUT_FILENO, RUN_PIPE);
        synclock_unlock (&priv->big_lock);
        ret = runner_run (&runner);
        synclock_lock (&priv->big_lock);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_SLAVE,
                        "Not a valid slave");
                ret = glusterd_gsync_read_frm_status (log_file_path,
                                                      buf, sizeof(buf));
                if (ret <= 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_READ_ERROR,
                                "Unable to read from %s", log_file_path);
                        goto out;
                }

                /* Tokenize the error message from gverify.sh to figure out
                 * if the error is a force blocker or not. */
                tmp = strtok_r (buf, "|", &save_ptr);
                if (!strcmp (tmp, "FORCE_BLOCKER"))
                        *is_force_blocker = 1;
                else {
                        /* No FORCE_BLOCKER flag present so all that is
                         * present is the error message. */
                        *is_force_blocker = 0;
                        if (tmp)
                                *op_errstr = gf_strdup (tmp);
                        ret = -1;
                        goto out;
                }

                /* Copy rest of the error message to op_errstr */
                tmp = strtok_r (NULL, "|", &save_ptr);
                if (tmp)
                        *op_errstr = gf_strdup (tmp);
                ret = -1;
                goto out;
        }
        ret = 0;
out:
        GF_FREE (slave_url_buf);
        sys_unlink (log_file_path);
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

/** @slave_ip remains unmodified */
int
glusterd_geo_rep_parse_slave (char *slave_url,
                              char **hostname, char **op_errstr)
{
        int   ret             = -1;
        char *tmp             = NULL;
        char *save_ptr        = NULL;
        char *host            = NULL;
        char errmsg[PATH_MAX] = "";
        char *saved_url       = NULL;
        xlator_t *this        = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (slave_url);
        GF_ASSERT (*slave_url);

        saved_url = gf_strdup (slave_url);
        if (!saved_url)
                goto out;

        /* Checking if hostname has user specified */
        host = strstr (saved_url, "@");
        if (!host) { /* no user specified */
                if (hostname) {
                        *hostname = gf_strdup (saved_url);
                        if (!*hostname)
                                goto out;
                }

                ret = 0;
                goto out;
        } else {
                /* Moving the host past the '@' and checking if the
                 * actual hostname also has '@' */
                host++;
                if (strstr (host, "@")) {
                        gf_msg_debug (this->name, 0, "host = %s", host);
                        ret = snprintf (errmsg, sizeof(errmsg) - 1,
                                        "Invalid Hostname (%s).", host);
                        errmsg[ret] = '\0';
                        gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                                GD_MSG_INVALID_ENTRY, "%s", errmsg);
                        ret = -1;
                        if (op_errstr)
                                *op_errstr = gf_strdup (errmsg);
                        goto out;
                }

                ret = -1;

                /**
                 * preliminary check for valid slave format.
                 */
                tmp = strtok_r (saved_url, "@", &save_ptr);
                tmp = strtok_r (NULL, "@", &save_ptr);
                if (!tmp)
                        goto out;
                if (hostname) {
                        *hostname = gf_strdup (tmp);
                        if (!*hostname)
                                goto out;
                }
        }

        ret = 0;
out:
        GF_FREE (saved_url);
        if (ret)
                if (hostname)
                        GF_FREE (*hostname);
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

/* Return -1 only if there is a match in volume uuid */
static int
get_slavehost_from_voluuid (dict_t *dict, char *key, data_t *value, void *data)
{
        char *slave_info                    = NULL;
        char *tmp                           = NULL;
        char *slave_host                    = NULL;
        xlator_t  *this                     = NULL;
        struct slave_vol_config *slave_vol  = NULL;
        int i                               = 0;
        int ret                             = -1;

        this = THIS;
        GF_VALIDATE_OR_GOTO ("glusterd", this, out);

        slave_vol = data;
        slave_info = value->data;

        gf_msg_debug (this->name, 0, "slave_info:%s !", slave_info);

        if (!(slave_info) || strlen (slave_info) == 0) {
                /* no slaves present, peace  */
                ret = 0;
                goto out;
        }

        /* slave format:
         * master_node_uuid:ssh://slave_host::slave_vol:slave_voluuid */
        while (i++ < 5) {
                slave_info = strchr (slave_info, ':');
                if (slave_info)
                        slave_info++;
                else
                        break;
        }

        if (!(slave_info) || strlen(slave_info) == 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SLAVE_VOL_PARSE_FAIL,
                        "slave_info format is wrong!");
                ret = -2;
                goto out;
        } else {
                if (strcmp (slave_info, slave_vol->slave_voluuid) == 0) {
                        ret = -1;

                        /* get corresponding slave host for reference*/
                        slave_host = value->data;
                        slave_host = strstr (slave_host, "://");
                        if (slave_host)
                                slave_host += 3;

                        /* To go past username in non-root geo-rep session */
                        tmp = strchr (slave_host, '@');
                        if (tmp) {
                                if ((tmp - slave_host) >= LOGIN_NAME_MAX) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_SLAVE_VOL_PARSE_FAIL,
                                                "Invalid slave user length in %s",
                                                slave_host);
                                        ret = -2;
                                        goto out;
                                }
                                strncpy (slave_vol->old_slvuser, slave_host,
                                                 (tmp - slave_host));
                                slave_vol->old_slvuser[(tmp - slave_host) + 1]
                                                        = '\0';
                                slave_host = tmp + 1;
                        } else
                                strcpy (slave_vol->old_slvuser, "root");

                        tmp = strchr (slave_host, ':');
                        if (!tmp) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_SLAVE_VOL_PARSE_FAIL,
                                        "Invalid slave_host!");
                                ret = -2;
                                goto out;
                        }

                        strncpy (slave_vol->old_slvhost, slave_host,
                                                 (tmp - slave_host));
                        slave_vol->old_slvhost[(tmp - slave_host) + 1] = '\0';

                        goto out;
                }
        }

        ret = 0;
out:
        return ret;
}

/* Given slave host and slave volume, check whether slave volume uuid
 * already present.
 * If slave volume uuid is present, get corresponding slave host
 * for reference */
static int
glusterd_get_slavehost_from_voluuid (glusterd_volinfo_t *volinfo,
                      char *slave_host, char *slave_vol,
                      struct slave_vol_config *slave1)
{
        int                ret   = -1;
        xlator_t           *this = NULL;

        this = THIS;

        GF_VALIDATE_OR_GOTO (this->name, volinfo, out);

        ret = dict_foreach (volinfo->gsync_slaves, get_slavehost_from_voluuid,
                                                   slave1);
out:
        return ret;
}

int
glusterd_op_stage_gsync_create (dict_t *dict, char **op_errstr)
{
        char               *down_peerstr              = NULL;
        char               *slave                     = NULL;
        char               *volname                   = NULL;
        char               *host_uuid                 = NULL;
        char               *statefile                 = NULL;
        char               *slave_url                 = NULL;
        char               *slave_host                = NULL;
        char               *slave_vol                 = NULL;
        char               *conf_path                 = NULL;
        char                errmsg[PATH_MAX]          = "";
        char                common_pem_file[PATH_MAX] = "";
        char                hook_script[PATH_MAX]     = "";
        char                uuid_str [64]             = "";
        int                 ret                       = -1;
        int                 is_pem_push               = -1;
        int                 ssh_port                  = 22;
        gf_boolean_t        is_force                  = -1;
        gf_boolean_t        is_no_verify              = -1;
        gf_boolean_t        is_force_blocker          = -1;
        gf_boolean_t        exists                    = _gf_false;
        gf_boolean_t        is_template_in_use        = _gf_false;
        glusterd_conf_t    *conf                      = NULL;
        glusterd_volinfo_t *volinfo                   = NULL;
        struct stat         stbuf                     = {0,};
        xlator_t           *this                      = NULL;
        struct slave_vol_config  slave1                     = {{0},};
        char                     old_slave_url[SLAVE_URL_INFO_MAX] = {0};
        char                     old_confpath[PATH_MAX]     = {0};
        gf_boolean_t             is_running                 = _gf_false;
        char                     *statedir                  = NULL;
        char                     statefiledir[PATH_MAX]     = {0,};
        gf_boolean_t             is_different_slavehost     = _gf_false;
        gf_boolean_t             is_different_username      = _gf_false;
        char                     *slave_user                = NULL;
        char                     *save_ptr                  = NULL;
        char                     *slave_url_buf             = NULL;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        ret = glusterd_op_gsync_args_get (dict, op_errstr, &volname,
                                          &slave, &host_uuid);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_ARG_FETCH_ERROR,
                        "Unable to fetch arguments");
                gf_msg_debug (this->name, 0, "Returning %d", ret);
                return -1;
        }

        if (conf->op_version < 2) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_UNSUPPORTED_VERSION,
                        "Op Version not supported.");
                snprintf (errmsg, sizeof(errmsg), "One or more nodes do not"
                          " support the required op version.");
                *op_errstr = gf_strdup (errmsg);
                ret = -1;
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        ret = glusterd_volinfo_find (volname, &volinfo);
        if ((ret) || (!exists)) {
                gf_msg (this->name, GF_LOG_WARNING, 0, GD_MSG_VOL_NOT_FOUND,
                        "volume name does not exist");
                snprintf (errmsg, sizeof(errmsg), "Volume name %s does not"
                          " exist", volname);
                *op_errstr = gf_strdup (errmsg);
                gf_msg_debug (this->name, 0, "Returning %d", ret);
                return -1;
        }

        ret = glusterd_get_slave_details_confpath (volinfo, dict, &slave_url,
                                                   &slave_host, &slave_vol,
                                                   &conf_path, op_errstr);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SLAVEINFO_FETCH_ERROR,
                        "Unable to fetch slave or confpath details.");
                ret = -1;
                goto out;
        }

        is_force = dict_get_str_boolean (dict, "force", _gf_false);

        uuid_utoa_r (MY_UUID, uuid_str);
        if (!strcmp (uuid_str, host_uuid)) {
                ret = glusterd_are_vol_all_peers_up (volinfo,
                                                     &conf->peers,
                                                     &down_peerstr);
                if ((ret == _gf_false) && !is_force) {
                        snprintf (errmsg, sizeof (errmsg), "Peer %s,"
                                  " which is a part of %s volume, is"
                                  " down. Please bring up the peer and"
                                  " retry.", down_peerstr,
                                  volinfo->volname);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_PEER_DISCONNECTED,
                                "%s", errmsg);
                        *op_errstr = gf_strdup (errmsg);
                        GF_FREE (down_peerstr);
                        down_peerstr = NULL;
                        gf_msg_debug (this->name, 0, "Returning %d", ret);
                        return -1;
                } else if (ret == _gf_false) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                GD_MSG_PEER_DISCONNECTED,
                                "Peer %s, which is a part of %s volume, is"
                                " down. Force creating geo-rep session."
                                " On bringing up the peer, re-run"
                                " \"gluster system:: execute"
                                " gsec_create\" and \"gluster volume"
                                " geo-replication %s %s create push-pem"
                                " force\"", down_peerstr, volinfo->volname,
                                volinfo->volname, slave);
                         GF_FREE (down_peerstr);
                         down_peerstr = NULL;
                }

                ret = dict_get_int32 (dict, "ssh_port", &ssh_port);
                if (ret < 0 && ret != -ENOENT) {
                        snprintf (errmsg, sizeof (errmsg),
                                  "Fetching ssh_port failed while "
                                  "handling "GEOREP" options");
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "%s", errmsg);
                        goto out;
                }

                is_no_verify = dict_get_str_boolean (dict, "no_verify", _gf_false);

                if (!is_no_verify) {
                        /* Checking if slave host is pingable, has proper passwordless
                        * ssh login setup, slave volume is created, slave vol is empty,
                        * and if it has enough memory and bypass in case of force if
                        * the error is not a force blocker */
                        ret = glusterd_verify_slave (volname, slave_url, slave_vol,
                                                     ssh_port, op_errstr,
                                                     &is_force_blocker);
                        if (ret) {
                                if (is_force && !is_force_blocker) {
                                        gf_msg (this->name, GF_LOG_INFO, 0,
                                                GD_MSG_INVALID_SLAVE,
                                                "%s is not a valid slave "
                                                "volume. Error: %s. Force "
                                                "creating geo-rep"
                                                " session.", slave,
                                                *op_errstr);
                                } else {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_INVALID_SLAVE,
                                                "%s is not a valid slave "
                                                "volume. Error: %s",
                                                slave, *op_errstr);
                                        ret = -1;

                                        goto out;
                                }
                        }
                }

                ret = dict_get_int32 (dict, "push_pem", &is_pem_push);
                if (!ret && is_pem_push) {
                        ret = snprintf (common_pem_file,
                                        sizeof(common_pem_file) - 1,
                                        "%s"GLUSTERD_COMMON_PEM_PUB_FILE,
                                        conf->workdir);
                        common_pem_file[ret] = '\0';

                        ret = snprintf (hook_script, sizeof(hook_script) - 1,
                                        "%s"GLUSTERD_CREATE_HOOK_SCRIPT,
                                        conf->workdir);
                        hook_script[ret] = '\0';

                        ret = sys_lstat (common_pem_file, &stbuf);
                        if (ret) {
                                snprintf (errmsg, sizeof (errmsg), "%s"
                                          " required for push-pem is"
                                          " not present. Please run"
                                          " \"gluster system:: execute"
                                          " gsec_create\"", common_pem_file);
                                gf_msg (this->name, GF_LOG_ERROR, ENOENT,
                                        GD_MSG_FILE_OP_FAILED,
                                        "%s", errmsg);
                                *op_errstr = gf_strdup (errmsg);
                                ret = -1;
                                goto out;
                        }

                        ret = sys_lstat (hook_script, &stbuf);
                        if (ret) {
                                snprintf (errmsg, sizeof (errmsg),
                                          "The hook-script (%s) required "
                                          "for push-pem is not present. "
                                          "Please install the hook-script "
                                          "and retry", hook_script);
                                gf_msg (this->name, GF_LOG_ERROR, ENOENT,
                                        GD_MSG_FILE_OP_FAILED, "%s", errmsg);
                                *op_errstr = gf_strdup (errmsg);
                                ret = -1;
                                goto out;
                        }

                        if (!S_ISREG(stbuf.st_mode)) {
                                snprintf (errmsg, sizeof (errmsg), "%s"
                                          " required for push-pem is"
                                          " not a regular file. Please run"
                                          " \"gluster system:: execute"
                                          " gsec_create\"", common_pem_file);
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_REG_FILE_MISSING, "%s", errmsg);
                                ret = -1;
                                goto out;
                        }
                }
        }

        ret = glusterd_get_statefile_name (volinfo, slave,
                                           conf_path, &statefile,
                                           &is_template_in_use);
        if (ret) {
                if (!strstr(slave, "::"))
                        snprintf (errmsg, sizeof (errmsg),
                                  "%s is not a valid slave url.", slave);
                else
                        snprintf (errmsg, sizeof (errmsg), "Please check gsync "
                                  "config file. Unable to get statefile's name");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STATEFILE_NAME_NOT_FOUND,
                        "%s", errmsg);
                ret = -1;
                goto out;
        }

        ret = dict_set_str (dict, "statefile", statefile);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                        "Unable to store statefile path");
                goto out;
        }

        strncpy (statefiledir, statefile, sizeof(statefiledir));
        statedir = dirname (statefiledir);

        ret = sys_lstat (statedir, &stbuf);
        if (!ret && !is_force) {
                snprintf (errmsg, sizeof (errmsg), "Session between %s"
                          " and %s is already created.",
                          volinfo->volname, slave);
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SESSION_ALREADY_EXIST,
                        "%s", errmsg);
                ret = -1;
                goto out;
        } else if (!ret)
                gf_msg (this->name, GF_LOG_INFO, 0, GD_MSG_FORCE_CREATE_SESSION,
                        "Session between %s and %s is already created. Force"
                        " creating again.", volinfo->volname, slave);

        ret = glusterd_get_slave_voluuid (slave_host, slave_vol,
                                          slave1.slave_voluuid);
        if ((ret) || (strlen(slave1.slave_voluuid) == 0)) {
                snprintf (errmsg, sizeof (errmsg),
                          "Unable to get remote volume uuid.");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_REMOTE_VOL_UUID_FAIL, "%s", errmsg);
                ret = -1;
                goto out;
        }

        ret = dict_set_dynstr_with_alloc (dict, "slave_voluuid",
                                          slave1.slave_voluuid);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                        "Unable to set slave volume uuid in the dict");
                goto out;
        }

        /* Check whether session is already created using slave volume uuid */
        ret = glusterd_get_slavehost_from_voluuid (volinfo, slave_host,
                                              slave_vol, &slave1);
        if (ret == -1) {
                if (!is_force) {
                        snprintf (errmsg, sizeof (errmsg), "Session between %s"
                                  " and %s:%s is already created! Cannot create "
                                  "with new slave:%s again!",
                                  volinfo->volname, slave1.old_slvhost,
                                  slave_vol, slave_host);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                 GD_MSG_FORCE_CREATE_SESSION, "Session between"
                                  " %s and %s:%s is already created! "
                                  "Cannot create with new slave:%s again!",
                                  volinfo->volname, slave1.old_slvhost,
                                  slave_vol, slave_host);
                        goto out;
                }

                /* Now, check whether session is already started.If so, warn!*/
                is_different_slavehost =
                        (strcmp (slave_host, slave1.old_slvhost) != 0)
                                                     ? _gf_true : _gf_false;

                if (strstr (slave_url, "@")) {
                        slave_url_buf = gf_strdup (slave_url);
                        if (!slave_url_buf) {
                                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                        GD_MSG_NO_MEMORY,
                                        "Unable to allocate memory");
                                ret = -1;
                                goto out;
                        }
                        slave_user = strtok_r (slave_url_buf, "@", &save_ptr);
                } else
                        slave_user = "root";
                is_different_username =
                        (strcmp (slave_user, slave1.old_slvuser) != 0)
                                                   ? _gf_true : _gf_false;

                /* Do the check, only if different slave host/slave user */
                if (is_different_slavehost || is_different_username) {
                        (void) snprintf (old_confpath, sizeof(old_confpath) - 1,
                                         "%s/"GEOREP"/%s_%s_%s/gsyncd.conf",
                                         conf->workdir, volinfo->volname,
                                         slave1.old_slvhost, slave_vol);

                        /* construct old slave url with (old) slave host */
                        (void) snprintf (old_slave_url,
                                         sizeof(old_slave_url) - 1,
                                         "%s::%s", slave1.old_slvhost,
                                         slave_vol);

                        ret = glusterd_check_gsync_running_local (volinfo->volname,
                                 old_slave_url, old_confpath, &is_running);
                        if (_gf_true == is_running) {
                                (void) snprintf (errmsg, sizeof(errmsg), "Geo"
                                    "-replication session between %s and %s"
                                    " is still active. Please stop the "
                                    "session and retry.",
                                    volinfo->volname, old_slave_url);
                                ret = -1;
                                goto out;
                        }
                }

                ret = dict_set_dynstr_with_alloc (dict, "old_slavehost",
                                                  slave1.old_slvhost);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Unable to set old_slavehost in the dict");
                        goto out;
                }

                ret = dict_set_int32 (dict, "existing_session", _gf_true);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Unable to set existing_session in the dict");
                        goto out;
                }
        } else if (ret == -2) {
                snprintf (errmsg, sizeof (errmsg), "get_slavehost_from_voluuid"
                          " failed for %s::%s. Please check the glusterd logs.",
                          slave_host, slave_vol);
                gf_msg (this->name, GF_LOG_INFO, 0, GD_MSG_FORCE_CREATE_SESSION,
                        "get_slavehost_from_voluuid failed %s %s!!",
                         slave_host, slave_vol);
                goto out;
        }

        ret = glusterd_verify_gsyncd_spawn (volinfo->volname, slave);
        if (ret) {
                snprintf (errmsg, sizeof (errmsg), "Unable to spawn gsyncd.");
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_GSYNCD_SPAWN_FAILED,
                        "%s", errmsg);
                goto out;
        }

        ret = 0;
out:

        if (ret && errmsg[0] != '\0')
                *op_errstr = gf_strdup (errmsg);
        if (slave_url_buf)
                GF_FREE (slave_url_buf);

        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

/* pre-condition check for geo-rep pause/resume.
 * Return: 0 on success
 *        -1 on any check failed.
 */
static int
gd_pause_resume_validation (int type, glusterd_volinfo_t *volinfo,
                            char *slave, char *statefile, char **op_errstr)
{
        int        ret                        = 0;
        char       errmsg[PATH_MAX]           = {0,};
        char       monitor_status[NAME_MAX]   = {0,};

        GF_ASSERT (volinfo);
        GF_ASSERT (slave);
        GF_ASSERT (statefile);
        GF_ASSERT (op_errstr);

        ret = glusterd_gsync_read_frm_status (statefile, monitor_status,
                                               sizeof (monitor_status));
        if (ret <= 0) {
                snprintf (errmsg, sizeof(errmsg), "Pause check Failed:"
                          " Geo-rep session is not setup");
                ret = -1;
                goto out;
        }

        if ( type == GF_GSYNC_OPTION_TYPE_PAUSE &&
             strstr (monitor_status, "Paused")) {
                snprintf (errmsg, sizeof(errmsg), "Geo-replication"
                          " session between %s and %s already Paused.",
                          volinfo->volname, slave);
                ret = -1;
                goto out;
        }
        if ( type == GF_GSYNC_OPTION_TYPE_RESUME &&
             !strstr (monitor_status, "Paused")) {
                snprintf (errmsg, sizeof(errmsg), "Geo-replication"
                          " session between %s and %s is not Paused.",
                          volinfo->volname, slave);
                ret = -1;
                goto out;
        }
        ret = 0;
out:
        if (ret && (errmsg[0] != '\0')) {
                *op_errstr = gf_strdup (errmsg);
        }
        return ret;
}

int
glusterd_op_stage_gsync_set (dict_t *dict, char **op_errstr)
{
        int                 ret                   = 0;
        int                 type                  = 0;
        char               *volname               = NULL;
        char               *slave                 = NULL;
        char               *slave_url             = NULL;
        char               *slave_host            = NULL;
        char               *slave_vol             = NULL;
        char               *down_peerstr          = NULL;
        char               *statefile             = NULL;
        char               statefiledir[PATH_MAX] = {0,};
        char               *statedir              = NULL;
        char               *path_list             = NULL;
        char               *conf_path             = NULL;
        gf_boolean_t        exists                = _gf_false;
        glusterd_volinfo_t *volinfo               = NULL;
        char                    errmsg[PATH_MAX]  = {0,};
        dict_t             *ctx                   = NULL;
        gf_boolean_t        is_force              = 0;
        gf_boolean_t        is_running            = _gf_false;
        gf_boolean_t        is_template_in_use    = _gf_false;
        uuid_t              uuid                  = {0};
        char                    uuid_str [64]     = {0};
        char               *host_uuid             = NULL;
        xlator_t           *this                  = NULL;
        glusterd_conf_t    *conf                  = NULL;
        struct stat         stbuf                 = {0,};

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);

        ret = dict_get_int32 (dict, "type", &type);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
                        "command type not found");
                *op_errstr = gf_strdup ("command unsuccessful");
                goto out;
        }

        if (type == GF_GSYNC_OPTION_TYPE_STATUS) {
                ret = glusterd_verify_gsync_status_opts (dict, op_errstr);
                goto out;
        }

        ret = glusterd_op_gsync_args_get (dict, op_errstr,
                                          &volname, &slave, &host_uuid);
        if (ret)
                goto out;

        uuid_utoa_r (MY_UUID, uuid_str);

        if (conf->op_version < 2) {
                snprintf (errmsg, sizeof(errmsg), "One or more nodes do not"
                          " support the required op version.");
                ret = -1;
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        ret = glusterd_volinfo_find (volname, &volinfo);
        if ((ret) || (!exists)) {
                snprintf (errmsg, sizeof(errmsg), "Volume name %s does not"
                          " exist", volname);
                ret = -1;
                goto out;
        }

        ret = glusterd_get_slave_details_confpath (volinfo, dict, &slave_url,
                                                   &slave_host, &slave_vol,
                                                   &conf_path, op_errstr);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SLAVEINFO_FETCH_ERROR,
                        "Unable to fetch slave or confpath details.");
                ret = -1;
                goto out;
        }

        is_force = dict_get_str_boolean (dict, "force", _gf_false);

        ret = glusterd_get_statefile_name (volinfo, slave,
                                           conf_path, &statefile,
                                           &is_template_in_use);
        if (ret) {
                if (!strstr(slave, "::")) {
                        snprintf (errmsg, sizeof(errmsg),
                                  "%s is not a valid slave url.", slave);
                        ret = -1;
                        goto out;
                } else {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SLAVE_URL_INVALID,
                                "state_file entry missing in config file (%s)",
                                conf_path);

                        if ((type == GF_GSYNC_OPTION_TYPE_STOP) && is_force) {
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        GD_MSG_STOP_FORCE, "Allowing stop "
                                        "force to bypass missing statefile "
                                        "entry in config file (%s), and "
                                        "template file", conf_path);
                                ret = 0;
                        } else
                                goto out;
                }
        } else {
                ret = dict_set_str (dict, "statefile", statefile);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "Unable to store statefile path");
                        goto out;
                }
        }

        /* Allowing stop force to bypass the statefile check
         * as this command acts as a fail safe method to stop geo-rep
         * session. */
        if (!((type == GF_GSYNC_OPTION_TYPE_STOP) && is_force)) {

                /* check session directory as statefile may not present
                 * during upgrade */
                strncpy (statefiledir, statefile, sizeof(statefiledir));
                statedir = dirname (statefiledir);

                ret = sys_lstat (statedir, &stbuf);
                if (ret) {
                        snprintf (errmsg, sizeof(errmsg), "Geo-replication"
                                  " session between %s and %s does not exist.",
                                  volinfo->volname, slave);
                        gf_msg (this->name, GF_LOG_ERROR, ENOENT,
                                GD_MSG_FILE_OP_FAILED,
                                "%s. statefile = %s", errmsg, statefile);
                        ret = -1;
                        goto out;
                }
        }

        /* Check if all peers that are a part of the volume are up or not */
        if ((type == GF_GSYNC_OPTION_TYPE_DELETE) ||
            ((type == GF_GSYNC_OPTION_TYPE_STOP) && !is_force) ||
            (type == GF_GSYNC_OPTION_TYPE_PAUSE) ||
            (type == GF_GSYNC_OPTION_TYPE_RESUME)) {
                if (!strcmp (uuid_str, host_uuid)) {
                        ret = glusterd_are_vol_all_peers_up (volinfo,
                                                             &conf->peers,
                                                             &down_peerstr);
                        if (ret == _gf_false) {
                                snprintf (errmsg, sizeof (errmsg), "Peer %s,"
                                          " which is a part of %s volume, is"
                                          " down. Please bring up the peer and"
                                          " retry.", down_peerstr,
                                          volinfo->volname);
                                ret = -1;
                                GF_FREE (down_peerstr);
                                down_peerstr = NULL;
                                goto out;
                        }
                }
        }

        switch (type) {
        case GF_GSYNC_OPTION_TYPE_START:
                if (is_template_in_use) {
                        snprintf (errmsg, sizeof(errmsg), "state-file entry "
                                  "missing in the config file(%s).",
                                  conf_path);
                        ret = -1;
                        goto out;
                }

                ret = glusterd_op_verify_gsync_start_options (volinfo, slave,
                                                              conf_path,
                                                              statefile,
                                                              op_errstr, is_force);
                if (ret)
                        goto out;
                ctx = glusterd_op_get_ctx();
                if (ctx) {
                        /* gsyncd does a fuse mount to start
                         * the geo-rep session */
                        if (!glusterd_is_fuse_available ()) {
                                gf_msg ("glusterd", GF_LOG_ERROR, errno,
                                        GD_MSG_GEO_REP_START_FAILED, "Unable "
                                        "to open /dev/fuse (%s), "
                                        "geo-replication start failed",
                                        strerror (errno));
                                snprintf (errmsg, sizeof(errmsg),
                                          "fuse unvailable");
                                ret = -1;
                                goto out;
                        }
                }
                break;

        case GF_GSYNC_OPTION_TYPE_STOP:
                if (!is_force) {
                        if (is_template_in_use) {
                                snprintf (errmsg, sizeof(errmsg),
                                          "state-file entry missing in "
                                          "the config file(%s).", conf_path);
                                ret = -1;
                                goto out;
                        }

                        ret = glusterd_op_verify_gsync_running (volinfo, slave,
                                                                conf_path,
                                                                op_errstr);
                        if (ret) {
                                ret = glusterd_get_local_brickpaths (volinfo,
                                                                     &path_list);
                                if (path_list)
                                        ret = -1;
                        }
                }
                break;

        case GF_GSYNC_OPTION_TYPE_PAUSE:
        case GF_GSYNC_OPTION_TYPE_RESUME:
                if (is_template_in_use) {
                        snprintf (errmsg, sizeof(errmsg),
                                  "state-file entry missing in "
                                  "the config file(%s).", conf_path);
                        ret = -1;
                        goto out;
                }

                ret = glusterd_op_verify_gsync_running (volinfo, slave,
                                                        conf_path, op_errstr);
                if (ret) {
                        ret = glusterd_get_local_brickpaths (volinfo,
                                                             &path_list);
                        if (path_list) {
                                ret = -1;
                                goto out;
                        }
                }

                if (!is_force) {
                        ret = gd_pause_resume_validation (type, volinfo, slave,
                                                          statefile, op_errstr);
                        if (ret) {
                                ret = glusterd_get_local_brickpaths (volinfo,
                                                                    &path_list);
                                if (path_list) {
                                        ret = -1;
                                        goto out;
                                }
                        }
                }
                break;

        case GF_GSYNC_OPTION_TYPE_CONFIG:
                if (is_template_in_use) {
                        snprintf (errmsg, sizeof(errmsg), "state-file entry "
                                  "missing in the config file(%s).",
                                  conf_path);
                        ret = -1;
                        goto out;
                }

                ret = gsync_verify_config_options (dict, op_errstr, volname);
                goto out;
                break;

        case GF_GSYNC_OPTION_TYPE_DELETE:
                /* Check if the gsync session is still running
                 * If so ask the user to stop geo-replication first.*/
                if (is_template_in_use) {
                        snprintf (errmsg, sizeof(errmsg), "state-file entry "
                                  "missing in the config file(%s).",
                                  conf_path);
                        ret = -1;
                        goto out;
                }

                ret = glusterd_gsync_get_uuid (slave, volinfo, uuid);
                if (ret) {
                        snprintf (errmsg, sizeof(errmsg), "Geo-replication"
                                  " session between %s and %s does not exist.",
                                  volinfo->volname, slave);
                        ret = -1;
                        goto out;
                } else {
                        ret = glusterd_check_gsync_running_local (volinfo->volname,
                                                                  slave, conf_path,
                                                                  &is_running);
                        if (_gf_true == is_running) {
                                snprintf (errmsg, sizeof (errmsg), GEOREP
                                          " session between %s & %s is "
                                          "still active. Please stop the "
                                          "session and retry.",
                                          volinfo->volname, slave);
                                ret = -1;
                                goto out;
                        }
                }

                ret = glusterd_verify_gsyncd_spawn (volinfo->volname, slave);
                if (ret) {
                        snprintf (errmsg, sizeof (errmsg),
                                  "Unable to spawn gsyncd");
                }

                break;
        }

out:

        if (path_list)
                GF_FREE (path_list);

        if (ret && errmsg[0] != '\0') {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_GSYNCD_ERROR,
                        "%s", errmsg);
                *op_errstr = gf_strdup (errmsg);
        }

        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

static int
gd_pause_or_resume_gsync (dict_t *dict, char *master, char *slave,
                          char *slave_host, char *slave_vol, char *conf_path,
                          char **op_errstr, gf_boolean_t is_pause)
{
        int32_t         ret                      = 0;
        int             pfd                      = -1;
        pid_t           pid                      = 0;
        char            pidfile[PATH_MAX]        = {0,};
        char            errmsg[PATH_MAX]         = "";
        char            buf [1024]               = {0,};
        gf_boolean_t    is_template_in_use       = _gf_false;
        char            monitor_status[NAME_MAX] = {0,};
        char            *statefile               = NULL;
        xlator_t        *this                    = NULL;

        this = THIS;
        GF_ASSERT (this);
        GF_ASSERT (dict);
        GF_ASSERT (master);
        GF_ASSERT (slave);
        GF_ASSERT (slave_host);
        GF_ASSERT (slave_vol);
        GF_ASSERT (conf_path);

        pfd = gsyncd_getpidfile (master, slave, pidfile,
                                 conf_path, &is_template_in_use);
        if (pfd == -2) {
                snprintf (errmsg, sizeof(errmsg),
                          "pid-file entry mising in config file and "
                          "template config file.");
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_PIDFILE_NOT_FOUND,
                        "%s", errmsg);
                *op_errstr = gf_strdup (errmsg);
                ret = -1;
                goto out;
        }

        if (gsync_status_byfd (pfd) == -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_GSYNCD_ERROR,
                        "gsyncd b/w %s & %s is not running", master, slave);
                /* monitor gsyncd already dead */
                goto out;
        }

        if (pfd < 0)
                goto out;

        /* Prepare to update status file*/
        ret = dict_get_str (dict, "statefile", &statefile);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Pause/Resume Failed: Unable to fetch statefile path");
                goto out;
        }
        ret = glusterd_gsync_read_frm_status (statefile, monitor_status,
                                              sizeof (monitor_status));
        if (ret <= 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STAT_FILE_READ_FAILED, "Pause/Resume Failed: "
                        "Unable to read status file for %s(master)"
                        " %s(slave)", master, slave);
                goto out;
        }

        ret = sys_read (pfd, buf, 1024);
        if (ret > 0) {
                pid = strtol (buf, NULL, 10);
                if (is_pause) {
                        ret = kill (-pid, SIGSTOP);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        GD_MSG_PID_KILL_FAIL, "Failed"
                                        " to pause gsyncd. Error: %s",
                                        strerror (errno));
                                goto out;
                        }
                        /*On pause force, if status is already paused
                          do not update status again*/
                        if (strstr (monitor_status, "Paused"))
                                goto out;

                        ret = glusterd_create_status_file ( master, slave,
                                                     slave_host, slave_vol,
                                                     "Paused");
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_UPDATE_STATEFILE_FAILED,
                                        "Unable  to update state_file."
                                        " Error : %s", strerror (errno));
                                /* If status cannot be updated resume back */
                                if (kill (-pid, SIGCONT)) {
                                        snprintf (errmsg, sizeof(errmsg),
                                                  "Pause successful but could "
                                                  "not update status file. "
                                                  "Please use 'resume force' to"
                                                  " resume back and retry pause"
                                                  " to reflect in status");
                                        gf_msg (this->name, GF_LOG_ERROR,
                                                errno,
                                                GD_MSG_PID_KILL_FAIL,
                                                "Resume back Failed. Error:"
                                                "%s", strerror (errno));
                                        *op_errstr = gf_strdup (errmsg);
                                }
                                goto out;
                        }
                } else {
                        ret = glusterd_create_status_file (master, slave,
                                                           slave_host,
                                                           slave_vol,
                                                           "Started");
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_UPDATE_STATEFILE_FAILED,
                                        "Resume Failed: Unable to update "
                                        "state_file. Error : %s",
                                        strerror (errno));
                                goto out;
                        }
                        ret = kill (-pid, SIGCONT);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        GD_MSG_PID_KILL_FAIL,
                                        "Resumed Failed: Unable to send"
                                        " SIGCONT. Error: %s",
                                         strerror (errno));
                                /* Process can't be resumed, update status
                                 * back to paused. */
                                ret = glusterd_create_status_file (master,
                                                                   slave,
                                                                   slave_host,
                                                                   slave_vol,
                                                                monitor_status);
                                if (ret) {
                                        snprintf (errmsg, sizeof(errmsg),
                                                  "Resume failed!!! Status "
                                                  "inconsistent. Please use "
                                                  "'resume force' to resume and"
                                                  " reach consistent state");
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_STATUS_UPDATE_FAILED,
                                                "Updating status back to paused"
                                                " Failed. Error: %s",
                                                strerror (errno));
                                        *op_errstr = gf_strdup (errmsg);
                                }
                                goto out;
                        }
                }
        }
        ret = 0;

out:
        sys_close (pfd);
        return ret;
}

static int
stop_gsync (char *master, char *slave, char **msg,
            char *conf_path, char **op_errstr,
            gf_boolean_t is_force)
{
        int32_t         ret     = 0;
        int             pfd     = -1;
        pid_t           pid     = 0;
        char            pidfile[PATH_MAX] = {0,};
        char            errmsg[PATH_MAX] = "";
        char            buf[1024] = {0,};
        int             i       = 0;
        gf_boolean_t    is_template_in_use = _gf_false;
        xlator_t        *this   = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (this->private);

        pfd = gsyncd_getpidfile (master, slave, pidfile,
                                 conf_path, &is_template_in_use);
        if (pfd == -2) {
                snprintf (errmsg, sizeof(errmsg) - 1,
                          "pid-file entry mising in config file and "
                          "template config file.");
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_PIDFILE_NOT_FOUND,
                        "%s", errmsg);
                *op_errstr = gf_strdup (errmsg);
                ret = -1;
                goto out;
        }
        if (gsync_status_byfd (pfd) == -1 && !is_force) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_GSYNCD_ERROR,
                        "gsyncd b/w %s & %s is not running", master,
                        slave);
                /* monitor gsyncd already dead */
                goto out;
        }

        if (pfd < 0)
                goto out;

        ret = sys_read (pfd, buf, 1024);
        if (ret > 0) {
                pid = strtol (buf, NULL, 10);
                ret = kill (-pid, SIGTERM);
                if (ret && !is_force) {
                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                GD_MSG_PID_KILL_FAIL,
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
                sys_unlink (pidfile);
        }
        ret = 0;

out:
        sys_close (pfd);

        return ret;
}

/*
 * glusterd_gsync_op_already_set:
 *      This funcion checks whether the op_value is same as in the
 *      gsyncd.conf file.
 *
 * RETURN VALUE:
 *      0 : op_value matches the conf file.
 *      1 : op_value does not matches the conf file or op_param not
 *          found in conf file.
 *     -1 : error
 */

int
glusterd_gsync_op_already_set (char* master, char* slave, char* conf_path,
                                             char* op_name, char* op_value)
{
        dict_t                 *confd                       = NULL;
        char                   *op_val_buf                  = NULL;
        int32_t                 op_val_conf                 = 0;
        int32_t                 op_val_cli                  = 0;
        int32_t                 ret                         = -1;
        gf_boolean_t            is_bool                     = _gf_true;
        xlator_t                *this                       = NULL;

        this = THIS;
        GF_ASSERT (this);

        confd = dict_new ();
        if (!confd) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_CREATE_FAIL,
                        "Not able to create dict.");
                return -1;
        }

        ret = glusterd_gsync_get_config (master, slave, conf_path,
                                         confd);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GET_CONFIG_INFO_FAILED,
                        "Unable to get configuration data for %s(master), "
                        "%s(slave)", master, slave);
                goto out;
        }

        ret = dict_get_param (confd, op_name, &op_val_buf);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Unable to get op_value for %s(master), %s(slave). "
                        "Please check gsync config file.", master, slave);
                ret = 1;
                goto out;
        }

        gf_msg_debug (this->name, 0, "val_cli:%s  val_conf:%s", op_value,
                      op_val_buf);

        if (!strcmp(op_val_buf,"true") || !strcmp(op_val_buf,"1")
            || !strcmp(op_val_buf,"yes")) {
                op_val_conf = 1;
        } else if(!strcmp(op_val_buf,"false") || !strcmp(op_val_buf,"0")
                || !strcmp(op_val_buf,"no")) {
                op_val_conf = 0;
        } else {
                is_bool = _gf_false;
        }

        if (is_bool) {
                if (!strcmp(op_value,"true") || !strcmp(op_value,"1")
                    || !strcmp(op_value,"yes")) {
                        op_val_cli = 1;
                } else {
                        op_val_cli = 0;
                }

                if ( op_val_cli == op_val_conf ) {
                        ret = 0;
                        goto out;
                }
        } else {
                if (!strcmp(op_val_buf,op_value)) {
                        ret = 0;
                        goto out;
                }
        }

        ret = 1;

out:
        dict_unref(confd);
        return ret;
}

static int
glusterd_gsync_configure (glusterd_volinfo_t *volinfo, char *slave,
                          char *path_list, dict_t *dict,
                          dict_t *resp_dict, char **op_errstr)
{
        int32_t         ret     = -1;
        char            *op_name = NULL;
        char            *op_value = NULL;
        runner_t        runner    = {0,};
        glusterd_conf_t *priv   = NULL;
        char            *subop  = NULL;
        char            *master = NULL;
        char            *conf_path = NULL;
        char            *slave_host = NULL;
        char            *slave_vol = NULL;
        struct stat      stbuf     = {0, };
        gf_boolean_t     restart_required = _gf_true;
        char           **resopt    = NULL;
        gf_boolean_t     op_already_set = _gf_false;
        xlator_t        *this   = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (slave);
        GF_ASSERT (op_errstr);
        GF_ASSERT (dict);
        GF_ASSERT (resp_dict);

        ret = dict_get_str (dict, "subop", &subop);
        if (ret != 0)
                goto out;

        if (strcmp (subop, "get") == 0 || strcmp (subop, "get-all") == 0) {
                /* deferred to cli */
                gf_msg_debug (this->name, 0, "Returning 0");
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

        priv = THIS->private;
        if (priv == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLUSTERD_PRIV_NOT_FOUND,
                        "priv of glusterd not present");
                *op_errstr = gf_strdup ("glusterd defunct");
                goto out;
        }

        ret = dict_get_str (dict, "conf_path", &conf_path);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Unable to fetch conf file path.");
                goto out;
        }

        master = "";
        runinit (&runner);
        runner_add_args (&runner, GSYNCD_PREFIX"/gsyncd", "-c", NULL);
        runner_argprintf (&runner, "%s", conf_path);
        runner_argprintf (&runner, "--iprefix=%s", DATADIR);
        if (volinfo) {
                master = volinfo->volname;
                runner_argprintf (&runner, ":%s", master);
        }
        runner_add_arg (&runner, slave);
        runner_argprintf (&runner, "--config-%s", subop);
        runner_add_arg (&runner, op_name);
        if (op_value)
                runner_add_arg (&runner, op_value);

        if ( strcmp(op_name,"checkpoint") != 0 && strtail (subop, "set")) {
                ret = glusterd_gsync_op_already_set(master,slave,conf_path,
                                                           op_name,op_value);
                if (ret == -1) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_GSYNCD_OP_SET_FAILED,
                                "glusterd_gsync_op_already_set failed.");
                        gf_asprintf (op_errstr, GEOREP" config-%s failed for "
                                     "%s %s", subop, master, slave);
                        goto out;
                }
                if (ret == 0) {
                        gf_msg_debug (this->name, 0, "op_value is already set");
                        op_already_set = _gf_true;
                        goto out;
                }
        }

        synclock_unlock (&priv->big_lock);
        ret = runner_run (&runner);
        synclock_lock (&priv->big_lock);
        if (ret) {
                gf_msg (this->name, GF_LOG_WARNING, 0, GD_MSG_GSYNCD_ERROR,
                        "gsyncd failed to %s %s option for "
                        "%s %s peers", subop, op_name, master,
                        slave);

                gf_asprintf (op_errstr, GEOREP" config-%s failed for %s %s",
                             subop, master, slave);

                goto out;
        }

        if ((!strcmp (op_name, "state_file")) && (op_value)) {

                ret = sys_lstat (op_value, &stbuf);
                if (ret) {
                        ret = dict_get_str (dict, "slave_host", &slave_host);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_GET_FAILED,
                                        "Unable to fetch slave host.");
                                goto out;
                        }

                        ret = dict_get_str (dict, "slave_vol", &slave_vol);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_GET_FAILED,
                                        "Unable to fetch slave volume name.");
                                goto out;
                        }

                        ret = glusterd_create_status_file (volinfo->volname,
                                                           slave, slave_host,
                                                           slave_vol,
                                                           "Switching Status "
                                                           "File");
                        if (ret || sys_lstat (op_value, &stbuf)) {
                                gf_msg (this->name, GF_LOG_ERROR, errno,
                                        GD_MSG_FILE_OP_FAILED, "Unable to "
                                        "create %s. Error : %s", op_value,
                                        strerror (errno));
                                ret = -1;
                                goto out;
                        }
                }
        }

        ret = 0;
        gf_asprintf (op_errstr, "config-%s successful", subop);

out:
        if (!ret && volinfo && !op_already_set) {
            for (resopt = gsync_no_restart_opts; *resopt; resopt++) {
                restart_required = _gf_true;
                if (!strcmp ((*resopt), op_name)){
                    restart_required = _gf_false;
                    break;
                }
            }

            if (restart_required) {
                ret = glusterd_check_restart_gsync_session (volinfo, slave,
                                                            resp_dict, path_list,
                                                            conf_path, 0);
                if (ret)
                    *op_errstr = gf_strdup ("internal error");
            }
        }

        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_gsync_read_frm_status (char *path, char *buf, size_t blen)
{
        int                 ret = 0;
        int           status_fd = -1;
        xlator_t          *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (path);
        GF_ASSERT (buf);
        status_fd = open (path, O_RDONLY);
        if (status_fd == -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_FILE_OP_FAILED,
                        "Unable to read gsyncd status file %s", path);
                return -1;
        }
        ret = sys_read (status_fd, buf, blen - 1);
        if (ret > 0) {
                size_t len = strnlen (buf, ret);
                /* Ensure there is a NUL byte and that it's not the first.  */
                if (len == 0 || len == blen - 1) {
                        ret = -1;
                } else {
                        char *p = buf + len - 1;
                        while (isspace (*p))
                                *p-- = '\0';
                }
        } else if (ret == 0)
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_GSYNCD_ERROR,
                        "Status file of gsyncd is empty");
        else /* ret < 0 */
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_GSYNCD_ERROR,
                        "Status file of gsyncd is corrupt");

        sys_close (status_fd);
        return ret;
}

static int
dict_get_param (dict_t *dict, char *key, char **param)
{
        char  *dk = NULL;
        char   *s = NULL;
        char    x = '\0';
        int   ret = 0;

        if (dict_get_str (dict, key, param) == 0)
                return 0;

        dk = gf_strdup (key);
        if (!dk)
                return -1;

        s = strpbrk (dk, "-_");
        if (!s) {
                ret = -1;
                goto out;
        }
        x = (*s == '-') ? '_' : '-';
        *s++ = x;
        while ((s = strpbrk (s, "-_")))
                *s++ = x;

        ret = dict_get_str (dict, dk, param);
out:
        GF_FREE (dk);
        return ret;
}

int
glusterd_fetch_values_from_config (char *master, char *slave,
                                   char *confpath, dict_t *confd,
                                   char **statefile,
                                   char **georep_session_wrkng_dir,
                                   char **socketfile)
{
        int                     ret                        = 0;
        xlator_t                *this                      = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = glusterd_gsync_get_config (master, slave, confpath,
                                         confd);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GET_CONFIG_INFO_FAILED,
                        "Unable to get configuration data for %s(master), "
                        "%s(slave)", master, slave);
                goto out;
        }

        if (statefile) {
                ret = dict_get_param (confd, "state_file", statefile);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Unable to get state_file's name "
                                "for %s(master), %s(slave). "
                                "Please check gsync config file.",
                                master, slave);
                        goto out;
                }
        }

        if (georep_session_wrkng_dir) {
                ret = dict_get_param (confd, "georep_session_working_dir",
                                      georep_session_wrkng_dir);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Unable to get geo-rep session's "
                                "working directory name for %s(master), "
                                "%s(slave). Please check gsync config file.",
                                master, slave);
                        goto out;
                }
        }

        if (socketfile) {
                ret = dict_get_param (confd, "state_socket_unencoded",
                                      socketfile);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED,
                                "Unable to get socket file's name "
                                "for %s(master), %s(slave). "
                                "Please check gsync config file.",
                                master, slave);
                        goto out;
                }
        }

        ret = 0;
out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_read_status_file (glusterd_volinfo_t *volinfo, char *slave,
                           char *conf_path, dict_t *dict, char *node)
{
        char                    temp_conf_path[PATH_MAX]   = "";
        char                   *working_conf_path          = NULL;
        char                   *georep_session_wrkng_dir   = NULL;
        char                   *master                     = NULL;
        char                    sts_val_name[1024]         = "";
        char                    monitor_status[NAME_MAX]   = "";
        char                   *statefile                  = NULL;
        char                   *socketfile                 = NULL;
        dict_t                 *confd                      = NULL;
        char                   *slavekey                   = NULL;
        char                   *slaveentry                 = NULL;
        char                   *slaveuser                  = NULL;
        char                   *saveptr                    = NULL;
        char                   *temp                       = NULL;
        char                   *temp_inp                   = NULL;
        char                   *brick_host_uuid            = NULL;
        int                     brick_host_uuid_length     = 0;
        int                     gsync_count                = 0;
        int                     ret                        = 0;
        glusterd_brickinfo_t   *brickinfo                  = NULL;
        gf_gsync_status_t      *sts_val                    = NULL;
        gf_boolean_t            is_template_in_use         = _gf_false;
        glusterd_conf_t        *priv                       = NULL;
        struct stat             stbuf                      = {0,};
        xlator_t               *this                       = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (this->private);
        GF_ASSERT (volinfo);
        GF_ASSERT (conf_path);

        master = volinfo->volname;

        confd = dict_new ();
        if (!confd) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_CREATE_FAIL,
                        "Not able to create dict.");
                return -1;
        }

        priv = THIS->private;

        snprintf (temp_conf_path, sizeof(temp_conf_path) - 1,
                  "%s/"GSYNC_CONF_TEMPLATE, priv->workdir);

        ret = sys_lstat (conf_path, &stbuf);
        if (!ret) {
                gf_msg (this->name, GF_LOG_INFO, 0, GD_MSG_CONFIG_INFO,
                        "Using passed config template(%s).",
                        conf_path);
                working_conf_path = conf_path;
        } else {
                gf_msg (this->name, GF_LOG_WARNING, ENOENT,
                        GD_MSG_FILE_OP_FAILED,
                        "Config file (%s) missing. Looking for template "
                        "config file (%s)", conf_path, temp_conf_path);
                ret = sys_lstat (temp_conf_path, &stbuf);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, ENOENT,
                                GD_MSG_FILE_OP_FAILED, "Template "
                                "config file (%s) missing.", temp_conf_path);
                        goto out;
                }
                gf_msg (this->name, GF_LOG_INFO, 0, GD_MSG_DEFAULT_TEMP_CONFIG,
                        "Using default config template(%s).", temp_conf_path);
                working_conf_path = temp_conf_path;
                is_template_in_use = _gf_true;
        }

fetch_data:
        ret = glusterd_fetch_values_from_config (master, slave,
                                                 working_conf_path,
                                                 confd,
                                                 &statefile,
                                                 &georep_session_wrkng_dir,
                                                 &socketfile);
        if (ret) {
                if (is_template_in_use == _gf_false) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_FETCH_CONFIG_VAL_FAILED,
                                "Unable to fetch config values "
                                "for %s(master), %s(slave). "
                                "Trying default config template",
                                master, slave);
                        working_conf_path = temp_conf_path;
                        is_template_in_use = _gf_true;
                        goto fetch_data;
                } else {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_FETCH_CONFIG_VAL_FAILED, "Unable to "
                                "fetch config values for %s(master), "
                                "%s(slave)", master, slave);
                        goto out;
                }
        }

        ret = glusterd_gsync_read_frm_status (statefile, monitor_status,
                                              sizeof (monitor_status));
        if (ret <= 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_STAT_FILE_READ_FAILED,
                        "Unable to read the status file for %s(master), "
                        "%s(slave) statefile: %s", master, slave,
                         statefile);
                strncpy (monitor_status, "defunct", sizeof (monitor_status));
        }

        ret = dict_get_int32 (dict, "gsync-count", &gsync_count);
        if (ret)
                gsync_count = 0;

        cds_list_for_each_entry (brickinfo, &volinfo->bricks, brick_list) {
                if (gf_uuid_compare (brickinfo->uuid, MY_UUID))
                        continue;

                sts_val = GF_CALLOC (1, sizeof(gf_gsync_status_t),
                                     gf_common_mt_gsync_status_t);
                if (!sts_val) {
                        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                GD_MSG_NO_MEMORY,
                                "Out Of Memory");
                        goto out;
                }

                /* Slave Key */
                ret = glusterd_get_slave (volinfo, slave, &slavekey);
                if (ret < 0) {
                        GF_FREE (sts_val);
                        goto out;
                }
                memcpy (sts_val->slavekey, slavekey, strlen(slavekey));
                sts_val->slavekey[strlen(slavekey)] = '\0';

                /* Master Volume */
                memcpy (sts_val->master, master, strlen(master));
                sts_val->master[strlen(master)] = '\0';

                /* Master Brick Node */
                memcpy (sts_val->node, brickinfo->hostname,
                        strlen(brickinfo->hostname));
                sts_val->node[strlen(brickinfo->hostname)] = '\0';

                /* Master Brick Path */
                memcpy (sts_val->brick, brickinfo->path,
                        strlen(brickinfo->path));
                sts_val->brick[strlen(brickinfo->path)] = '\0';

                /* Brick Host UUID */
                brick_host_uuid = uuid_utoa(brickinfo->uuid);
                brick_host_uuid_length = strlen (brick_host_uuid);
                memcpy (sts_val->brick_host_uuid, brick_host_uuid,
                        brick_host_uuid_length);
                sts_val->brick_host_uuid[brick_host_uuid_length] = '\0';

                /* Slave */
                memcpy (sts_val->slave, slave, strlen(slave));
                sts_val->slave[strlen(slave)] = '\0';

                snprintf (sts_val->slave_node,
                          sizeof(sts_val->slave_node), "N/A");

                snprintf (sts_val->worker_status,
                          sizeof(sts_val->worker_status), "N/A");

                snprintf (sts_val->crawl_status,
                          sizeof(sts_val->crawl_status), "N/A");

                snprintf (sts_val->last_synced,
                          sizeof(sts_val->last_synced), "N/A");

                snprintf (sts_val->last_synced_utc,
                          sizeof(sts_val->last_synced_utc), "N/A");

                snprintf (sts_val->entry, sizeof(sts_val->entry), "N/A");

                snprintf (sts_val->data, sizeof(sts_val->data), "N/A");

                snprintf (sts_val->meta, sizeof(sts_val->meta), "N/A");

                snprintf (sts_val->failures, sizeof(sts_val->failures), "N/A");

                snprintf (sts_val->checkpoint_time,
                          sizeof(sts_val->checkpoint_time), "N/A");

                snprintf (sts_val->checkpoint_time_utc,
                          sizeof(sts_val->checkpoint_time_utc), "N/A");

                snprintf (sts_val->checkpoint_completed,
                          sizeof(sts_val->checkpoint_completed), "N/A");

                snprintf (sts_val->checkpoint_completion_time,
                          sizeof(sts_val->checkpoint_completion_time),
                          "N/A");

                snprintf (sts_val->checkpoint_completion_time_utc,
                          sizeof(sts_val->checkpoint_completion_time_utc),
                          "N/A");

                /* Get all the other values from Gsyncd */
                ret = glusterd_gsync_get_status (master, slave, conf_path,
                                                 brickinfo->path, sts_val);

                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_GET_STATUS_DATA_FAIL,
                                "Unable to get status data "
                                "for %s(master), %s(slave), %s(brick)",
                                master, slave, brickinfo->path);
                        ret = -1;
                        goto out;
                }

                if (is_template_in_use) {
                        snprintf (sts_val->worker_status,
                                  sizeof(sts_val->worker_status),
                                  "Config Corrupted");
                }

                ret = dict_get_str (volinfo->gsync_slaves, slavekey,
                                    &slaveentry);
                if (ret < 0) {
                        GF_FREE (sts_val);
                        goto out;
                }


                memcpy (sts_val->session_slave, slaveentry,
                        strlen(slaveentry));
                sts_val->session_slave[strlen(slaveentry)] = '\0';

                temp_inp = gf_strdup(slaveentry);
                if (!temp_inp)
                        goto out;

                if (strstr(temp_inp, "@") == NULL) {
                        slaveuser = "root";
                } else {
                        temp = strtok_r(temp_inp, "//", &saveptr);
                        temp = strtok_r(NULL, "/", &saveptr);
                        slaveuser = strtok_r(temp, "@", &saveptr);
                }
                memcpy (sts_val->slave_user, slaveuser,
                        strlen(slaveuser));
                sts_val->slave_user[strlen(slaveuser)] = '\0';

                snprintf (sts_val_name, sizeof (sts_val_name),
                          "status_value%d", gsync_count);
                ret = dict_set_bin (dict, sts_val_name, sts_val,
                                    sizeof(gf_gsync_status_t));
                if (ret) {
                        GF_FREE (sts_val);
                        goto out;
                }

                gsync_count++;
                sts_val = NULL;
        }

        ret = dict_set_int32 (dict, "gsync-count", gsync_count);
        if (ret)
                goto out;

out:
        GF_FREE (temp_inp);
        dict_unref (confd);

        return 0;
}

int
glusterd_check_restart_gsync_session (glusterd_volinfo_t *volinfo, char *slave,
                                      dict_t *resp_dict, char *path_list,
                                      char *conf_path, gf_boolean_t is_force)
{

        int                    ret              = 0;
        glusterd_conf_t        *priv            = NULL;
        char                   *status_msg      = NULL;
        gf_boolean_t           is_running       = _gf_false;
        char                   *op_errstr       = NULL;
        char                   *key             = NULL;
        xlator_t               *this            = NULL;

        GF_ASSERT (volinfo);
        GF_ASSERT (slave);
        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);

        key  = slave;

        ret = glusterd_check_gsync_running_local (volinfo->volname,
                                                  slave, conf_path,
                                                  &is_running);
        if (!ret && (_gf_true != is_running))
                /* gsynd not running, nothing to do */
                goto out;

        ret = stop_gsync (volinfo->volname, slave, &status_msg,
                          conf_path, &op_errstr,
                          is_force);
        if (ret == 0 && status_msg)
                ret = dict_set_str (resp_dict, "gsync-status",
                                    status_msg);
        if (ret == 0) {
                dict_del (volinfo->gsync_active_slaves, key);
                ret = glusterd_start_gsync (volinfo, slave, path_list,
                                            conf_path, uuid_utoa(MY_UUID),
                                            NULL, _gf_false);
                if (!ret) {
                        /* Add slave to the dict indicating geo-rep session is
                         * running.*/
                        ret = dict_set_dynstr_with_alloc (
                                              volinfo->gsync_active_slaves,
                                              key, "running");
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED, "Unable to set"
                                        " key:%s value:running in dict. But "
                                        "the config succeeded.", key);
                              goto out;
                        }
                }
        }

 out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

static int32_t
glusterd_marker_changelog_create_volfile (glusterd_volinfo_t *volinfo)
{
        int32_t          ret     = 0;
        xlator_t         *this   = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = glusterd_create_volfiles_and_notify_services (volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_VOLFILE_CREATE_FAIL,
                        "Unable to create volfile for setting of marker "
                        "while '"GEOREP" start'");
                ret = -1;
                goto out;
        }

        ret = glusterd_store_volinfo (volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret)
                goto out;

        if (GLUSTERD_STATUS_STARTED == volinfo->status) {
                ret = glusterd_svcs_manager (volinfo);
                goto out;
        }
        ret = 0;
out:
        return ret;
}

static int
glusterd_set_gsync_knob (glusterd_volinfo_t *volinfo, char *key, int *vc)
{
        int   ret          = -1;
        int   conf_enabled = _gf_false;
        xlator_t *this     = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (this->private);

        conf_enabled = glusterd_volinfo_get_boolean (volinfo, key);
        if (conf_enabled == -1) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_GET_KEY_FAILED,
                        "failed to get key %s from volinfo", key);
                goto out;
        }

        ret = 0;
        if (conf_enabled == _gf_false) {
                *vc = 1;
                ret = glusterd_gsync_volinfo_dict_set (volinfo,
                                                       key, "on");
        }

 out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

static int
glusterd_set_gsync_confs (glusterd_volinfo_t *volinfo)
{
        int ret             = -1;
        int volfile_changed = 0;

        ret = glusterd_set_gsync_knob (volinfo,
                                       VKEY_MARKER_XTIME, &volfile_changed);
        if (ret)
                goto out;

        /**
         * enable ignore-pid-check blindly as it could be needed for
         * cascading setups.
         */
        ret = glusterd_set_gsync_knob (volinfo, VKEY_MARKER_XTIME_FORCE,
                                       &volfile_changed);
        if (ret)
                goto out;

        ret = glusterd_set_gsync_knob (volinfo,
                                       VKEY_CHANGELOG, &volfile_changed);
        if (ret)
                goto out;

        if (volfile_changed)
                ret = glusterd_marker_changelog_create_volfile (volinfo);

 out:
        return ret;
}

static int
glusterd_get_gsync_status_mst_slv (glusterd_volinfo_t *volinfo,
                                   char *slave, char *conf_path,
                                   dict_t *rsp_dict, char *node)
{
        char              *statefile = NULL;
        uuid_t             uuid = {0, };
        int                ret = 0;
        gf_boolean_t       is_template_in_use = _gf_false;
        struct stat        stbuf = {0, };
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (volinfo);
        GF_ASSERT (slave);
        GF_ASSERT (this->private);

        ret = glusterd_gsync_get_uuid (slave, volinfo, uuid);
        if (ret) {
                gf_msg (this->name, GF_LOG_INFO, 0, GD_MSG_SESSION_INACTIVE,
                        "geo-replication status %s %s : session is not "
                        "active", volinfo->volname, slave);

                ret = glusterd_get_statefile_name (volinfo, slave,
                                                   conf_path, &statefile,
                                                   &is_template_in_use);
                if (ret) {
                        if (!strstr(slave, "::"))
                                gf_msg (this->name, GF_LOG_INFO, 0,
                                        GD_MSG_SLAVE_URL_INVALID,
                                        "%s is not a valid slave url.", slave);
                        else
                                gf_msg (this->name, GF_LOG_INFO, 0,
                                        GD_MSG_GET_STATEFILE_NAME_FAILED,
                                        "Unable to get statefile's name");
                        ret = 0;
                        goto out;
                }

                ret = sys_lstat (statefile, &stbuf);
                if (ret) {
                        gf_msg (this->name, GF_LOG_INFO, ENOENT,
                                GD_MSG_FILE_OP_FAILED,
                                "%s statefile not present.", statefile);
                        ret = 0;
                        goto out;
                }
        }

        ret = glusterd_read_status_file (volinfo, slave, conf_path,
                                         rsp_dict, node);
out:
        if (statefile)
                GF_FREE (statefile);

        gf_msg_debug (this->name, 0, "Returning with %d", ret);
        return ret;
}

static int
glusterd_get_gsync_status_mst (glusterd_volinfo_t *volinfo, dict_t *rsp_dict,
                               char *node)
{
        glusterd_gsync_status_temp_t  param = {0, };

        GF_ASSERT (volinfo);

        param.rsp_dict = rsp_dict;
        param.volinfo = volinfo;
        param.node = node;
        dict_foreach (volinfo->gsync_slaves, _get_status_mst_slv, &param);

        return 0;
}

static int
glusterd_get_gsync_status_all (dict_t *rsp_dict, char *node)
{

        int32_t                 ret = 0;
        glusterd_conf_t         *priv = NULL;
        glusterd_volinfo_t      *volinfo = NULL;
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;

        GF_ASSERT (priv);

        cds_list_for_each_entry (volinfo, &priv->volumes, vol_list) {
                ret = glusterd_get_gsync_status_mst (volinfo, rsp_dict, node);
                if (ret)
                        goto out;
        }

out:
        gf_msg_debug (this->name, 0, "Returning with %d", ret);
        return ret;

}

static int
glusterd_get_gsync_status (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        char               *slave  = NULL;
        char               *volname = NULL;
        char               *conf_path = NULL;
        char               errmsg[PATH_MAX] = {0, };
        gf_boolean_t       exists = _gf_false;
        glusterd_volinfo_t *volinfo = NULL;
        int                ret = 0;
        char my_hostname[256] = {0,};
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = gethostname(my_hostname, 256);
        if (ret) {
                /* stick to N/A */
                (void) strcpy (my_hostname, "N/A");
        }

        ret = dict_get_str (dict, "master", &volname);
        if (ret < 0){
                ret = glusterd_get_gsync_status_all (rsp_dict, my_hostname);
                goto out;
        }

        exists = glusterd_check_volume_exists (volname);
        ret = glusterd_volinfo_find (volname, &volinfo);
        if ((ret) || (!exists)) {
                gf_msg (this->name, GF_LOG_WARNING, 0, GD_MSG_VOL_NOT_FOUND,
                        "volume name does not exist");
                snprintf (errmsg, sizeof(errmsg), "Volume name %s does not"
                          " exist", volname);
                *op_errstr = gf_strdup (errmsg);
                ret = -1;
                goto out;
        }


        ret = dict_get_str (dict, "slave", &slave);
        if (ret < 0) {
                ret = glusterd_get_gsync_status_mst (volinfo,
                                                     rsp_dict, my_hostname);
                goto out;
        }

        ret = dict_get_str (dict, "conf_path", &conf_path);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Unable to fetch conf file path.");
                goto out;
        }

        ret = glusterd_get_gsync_status_mst_slv (volinfo, slave, conf_path,
                                                 rsp_dict, my_hostname);

 out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

static int
glusterd_gsync_delete (glusterd_volinfo_t *volinfo, char *slave,
                       char *slave_host, char *slave_vol, char *path_list,
                       dict_t *dict, dict_t *resp_dict, char **op_errstr)
{
        int32_t         ret     = -1;
        runner_t        runner    = {0,};
        glusterd_conf_t *priv   = NULL;
        char            *master = NULL;
        char            *gl_workdir = NULL;
        char             geo_rep_dir[PATH_MAX] = "";
        char            *conf_path = NULL;
        xlator_t *this = NULL;
        uint32_t        reset_sync_time = _gf_false;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (slave);
        GF_ASSERT (slave_host);
        GF_ASSERT (slave_vol);
        GF_ASSERT (op_errstr);
        GF_ASSERT (dict);
        GF_ASSERT (resp_dict);

        if (THIS)
                priv = THIS->private;
        if (priv == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLUSTERD_PRIV_NOT_FOUND,
                        "priv of glusterd not present");
                *op_errstr = gf_strdup ("glusterd defunct");
                goto out;
        }

        ret = dict_get_str (dict, "conf_path", &conf_path);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Unable to fetch conf file path.");
                goto out;
        }

        gl_workdir = priv->workdir;
        master = "";
        runinit (&runner);
        runner_add_args  (&runner, GSYNCD_PREFIX"/gsyncd",
                          "--delete", "-c", NULL);
        runner_argprintf (&runner, "%s", conf_path);
        runner_argprintf (&runner, "--iprefix=%s", DATADIR);

        runner_argprintf (&runner, "--path-list=%s", path_list);

        ret = dict_get_uint32 (dict, "reset-sync-time", &reset_sync_time);
        if (!ret && reset_sync_time) {
                runner_add_args  (&runner, "--reset-sync-time", NULL);
        }

        if (volinfo) {
                master = volinfo->volname;
                runner_argprintf (&runner, ":%s", master);
        }
        runner_add_arg (&runner, slave);
        runner_redir (&runner, STDOUT_FILENO, RUN_PIPE);
        synclock_unlock (&priv->big_lock);
        ret = runner_run (&runner);
        synclock_lock (&priv->big_lock);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_SESSION_DEL_FAILED,
                        "gsyncd failed to delete session info for %s and "
                        "%s peers", master, slave);

                gf_asprintf (op_errstr, "gsyncd failed to "
                             "delete session info for %s and %s peers",
                             master, slave);

                goto out;
        }

        ret = snprintf (geo_rep_dir, sizeof(geo_rep_dir) - 1,
                        "%s/"GEOREP"/%s_%s_%s", gl_workdir,
                        volinfo->volname, slave_host, slave_vol);
        geo_rep_dir[ret] = '\0';

        ret = sys_rmdir (geo_rep_dir);
        if (ret) {
                if (errno == ENOENT)
                        gf_msg_debug (this->name, 0, "Geo Rep Dir(%s) Not Present.",
                                      geo_rep_dir);
                else {
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_DIR_OP_FAILED,
                                "Unable to delete Geo Rep Dir(%s). Error: %s",
                                geo_rep_dir, strerror (errno));
                        goto out;
                }
        }

        ret = 0;

        gf_asprintf (op_errstr, "delete successful");

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_op_sys_exec (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        char                   buf[PATH_MAX]          = "";
        char                   cmd_arg_name[PATH_MAX] = "";
        char                   output_name[PATH_MAX]  = "";
        char                   errmsg[PATH_MAX]       = "";
        char                  *ptr                    = NULL;
        char                  *bufp                   = NULL;
        char                  *command                = NULL;
        char                 **cmd_args               = NULL;
        int                    ret                    = -1;
        int                    i                      = -1;
        int                    cmd_args_count         = 0;
        int                    output_count           = 0;
        glusterd_conf_t       *priv                   = NULL;
        runner_t               runner                 = {0,};
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);
        GF_ASSERT (rsp_dict);

        if (THIS)
                priv = THIS->private;
        if (priv == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLUSTERD_PRIV_NOT_FOUND,
                        "priv of glusterd not present");
                *op_errstr = gf_strdup ("glusterd defunct");
                goto out;
        }

        ret = dict_get_str (dict, "command", &command);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Unable to get command from dict");
                goto out;
        }

        ret = dict_get_int32 (dict, "cmd_args_count", &cmd_args_count);
        if (ret)
                gf_msg (this->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
                        "No cmd_args_count");

        if (cmd_args_count) {
                cmd_args = GF_CALLOC (cmd_args_count, sizeof (char*),
                                      gf_common_mt_char);
                if (!cmd_args) {
                        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                GD_MSG_NO_MEMORY,
                                "Unable to calloc. Errno = %s",
                                strerror(errno));
                        goto out;
                }

                for (i=1; i <= cmd_args_count; i++) {
                        memset (cmd_arg_name, '\0', sizeof(cmd_arg_name));
                        snprintf (cmd_arg_name, sizeof(cmd_arg_name),
                                  "cmd_arg_%d", i);
                        ret = dict_get_str (dict, cmd_arg_name, &cmd_args[i-1]);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_GET_FAILED, "Unable to get"
                                        " %s in dict", cmd_arg_name);
                                goto out;
                        }
                }
        }

        runinit (&runner);
        runner_argprintf (&runner, GSYNCD_PREFIX"/peer_%s", command);
        for (i=0; i < cmd_args_count; i++)
                runner_add_arg (&runner, cmd_args[i]);
        runner_redir (&runner, STDOUT_FILENO, RUN_PIPE);
        synclock_unlock (&priv->big_lock);
        ret = runner_start (&runner);
        if (ret == -1) {
                snprintf (errmsg, sizeof (errmsg), "Unable to "
                          "execute command. Error : %s",
                          strerror (errno));
                *op_errstr = gf_strdup (errmsg);
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_CMD_EXEC_FAIL, "%s",
                        errmsg);
                ret = -1;
                synclock_lock (&priv->big_lock);
                goto out;
        }

        do {
                ptr = fgets(buf, sizeof(buf), runner_chio (&runner, STDOUT_FILENO));
                if (ptr) {
                        ret = dict_get_int32 (rsp_dict, "output_count", &output_count);
                        if (ret)
                                output_count = 1;
                        else
                                output_count++;
                        memset (output_name, '\0', sizeof (output_name));
                        snprintf (output_name, sizeof (output_name),
                                  "output_%d", output_count);
                        if (buf[strlen(buf) - 1] == '\n')
                                buf[strlen(buf) - 1] = '\0';
                        bufp = gf_strdup (buf);
                        if (!bufp)
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_STRDUP_FAILED,
                                        "gf_strdup failed.");
                        ret = dict_set_dynstr (rsp_dict, output_name, bufp);
                        if (ret) {
                                GF_FREE (bufp);
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED, "output set "
                                        "failed.");
                        }
                        ret = dict_set_int32 (rsp_dict, "output_count", output_count);
                        if (ret)
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED, "output_count "
                                        "set failed.");
                }
        } while (ptr);

        ret = runner_end (&runner);
        if (ret) {
                snprintf (errmsg, sizeof (errmsg), "Unable to "
                          "end. Error : %s",
                          strerror (errno));
                *op_errstr = gf_strdup (errmsg);
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_UNABLE_TO_END, "%s",
                        errmsg);
                ret = -1;
                synclock_lock (&priv->big_lock);
                goto out;
        }
        synclock_lock (&priv->big_lock);

        ret = 0;
out:
        if (cmd_args) {
                GF_FREE (cmd_args);
                cmd_args = NULL;
        }

        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_op_copy_file (dict_t *dict, char **op_errstr)
{
        char             abs_filename[PATH_MAX] = "";
        char             errmsg[PATH_MAX]       = "";
        char            *filename               = NULL;
        char            *host_uuid              = NULL;
        char             uuid_str [64]          = {0};
        char            *contents               = NULL;
        char             buf[1024]              = "";
        int              ret                    = -1;
        int              fd                     = -1;
        int              bytes_writen           = 0;
        int              bytes_read             = 0;
        int              contents_size          = -1;
        int              file_mode              = -1;
        glusterd_conf_t *priv                   = NULL;
        struct stat      stbuf                  = {0,};
        gf_boolean_t     free_contents          = _gf_true;
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        if (THIS)
                priv = THIS->private;
        if (priv == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_GLUSTERD_PRIV_NOT_FOUND,
                        "priv of glusterd not present");
                *op_errstr = gf_strdup ("glusterd defunct");
                goto out;
        }

        ret = dict_get_str (dict, "host-uuid", &host_uuid);
        if (ret < 0)
                goto out;

        ret = dict_get_str (dict, "source", &filename);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Unable to fetch filename from dict.");
               *op_errstr = gf_strdup ("command unsuccessful");
               goto out;
        }
        snprintf (abs_filename, sizeof(abs_filename),
                  "%s/%s", priv->workdir, filename);

        uuid_utoa_r (MY_UUID, uuid_str);
        if (!strcmp (uuid_str, host_uuid)) {
                ret = sys_lstat (abs_filename, &stbuf);
                if (ret) {
                        snprintf (errmsg, sizeof (errmsg), "Source file"
                                 " does not exist in %s", priv->workdir);
                        *op_errstr = gf_strdup (errmsg);
                        gf_msg (this->name, GF_LOG_ERROR, ENOENT,
                                GD_MSG_FILE_OP_FAILED, "%s", errmsg);
                        goto out;
                }

                contents = GF_CALLOC(1, stbuf.st_size+1, gf_common_mt_char);
                if (!contents) {
                        snprintf (errmsg, sizeof (errmsg),
                                  "Unable to allocate memory");
                        *op_errstr = gf_strdup (errmsg);
                        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                GD_MSG_NO_MEMORY,
                                "%s", errmsg);
                        ret = -1;
                        goto out;
                }

                fd = open (abs_filename, O_RDONLY);
                if (fd < 0) {
                        snprintf (errmsg, sizeof (errmsg), "Unable to open %s",
                                  abs_filename);
                        *op_errstr = gf_strdup (errmsg);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_FILE_OP_FAILED,
                                "%s", errmsg);
                        ret = -1;
                        goto out;
                }

                do {
                        ret = sys_read (fd, buf, sizeof(buf));
                        if (ret > 0) {
                                memcpy (contents+bytes_read, buf, ret);
                                bytes_read += ret;
                                memset (buf, '\0', sizeof(buf));
                        }
                } while (ret > 0);

                if (bytes_read != stbuf.st_size) {
                        snprintf (errmsg, sizeof (errmsg), "Unable to read all "
                                  "the data from %s", abs_filename);
                        *op_errstr = gf_strdup (errmsg);
                        gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_READ_ERROR,
                                "%s", errmsg);
                        ret = -1;
                        goto out;
                }

                ret = dict_set_int32 (dict, "contents_size", stbuf.st_size);
                if (ret) {
                        snprintf (errmsg, sizeof (errmsg), "Unable to set"
                                  " contents size in dict.");
                        *op_errstr = gf_strdup (errmsg);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED, "%s", errmsg);
                        goto out;
                }

                ret = dict_set_int32 (dict, "file_mode",
                                      (int32_t)stbuf.st_mode);
                if (ret) {
                        snprintf (errmsg, sizeof (errmsg), "Unable to set"
                                  " file mode in dict.");
                        *op_errstr = gf_strdup (errmsg);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED,
                                "%s", errmsg);
                        goto out;
                }

                ret = dict_set_bin (dict, "common_pem_contents",
                                    contents, stbuf.st_size);
                if (ret) {
                        snprintf (errmsg, sizeof (errmsg), "Unable to set"
                                  " pem contents in dict.");
                        *op_errstr = gf_strdup (errmsg);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED, "%s", errmsg);
                        goto out;
                }
                free_contents = _gf_false;
        } else {
                free_contents = _gf_false;
                ret = dict_get_bin (dict, "common_pem_contents",
                                    (void **) &contents);
                if (ret) {
                        snprintf (errmsg, sizeof (errmsg), "Unable to get"
                                  " pem contents in dict.");
                        *op_errstr = gf_strdup (errmsg);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "%s", errmsg);
                        goto out;
                }
                ret = dict_get_int32 (dict, "contents_size", &contents_size);
                if (ret) {
                        snprintf (errmsg, sizeof (errmsg), "Unable to set"
                                  " contents size in dict.");
                        *op_errstr = gf_strdup (errmsg);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "%s", errmsg);
                        goto out;
                }

                ret = dict_get_int32 (dict, "file_mode", &file_mode);
                if (ret) {
                        snprintf (errmsg, sizeof (errmsg), "Unable to get"
                                  " file mode in dict.");
                        *op_errstr = gf_strdup (errmsg);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_GET_FAILED, "%s", errmsg);
                        goto out;
                }

                fd = open (abs_filename, O_WRONLY | O_TRUNC | O_CREAT, 0600);
                if (fd < 0) {
                        snprintf (errmsg, sizeof (errmsg), "Unable to open %s",
                                  abs_filename);
                        *op_errstr = gf_strdup (errmsg);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_FILE_OP_FAILED, "%s", errmsg);
                        ret = -1;
                        goto out;
                }

                bytes_writen = sys_write (fd, contents, contents_size);

                if (bytes_writen != contents_size) {
                        snprintf (errmsg, sizeof (errmsg), "Failed to write"
                                  " to %s", abs_filename);
                        *op_errstr = gf_strdup (errmsg);
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_FILE_OP_FAILED, "%s", errmsg);
                        ret = -1;
                        goto out;
                }

                sys_fchmod (fd, file_mode);
        }

        ret = 0;
out:
        if (fd != -1)
                sys_close (fd);

        if (free_contents)
                GF_FREE(contents);

        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_op_gsync_set (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        int32_t             ret                 = -1;
        int32_t             type                = -1;
        char               *host_uuid           = NULL;
        char               *slave               = NULL;
        char               *slave_url           = NULL;
        char               *slave_vol           = NULL;
        char               *slave_host          = NULL;
        char               *volname             = NULL;
        char               *path_list           = NULL;
        glusterd_volinfo_t *volinfo             = NULL;
        glusterd_conf_t    *priv                = NULL;
        gf_boolean_t        is_force            = _gf_false;
        char               *status_msg          = NULL;
        gf_boolean_t        is_running          = _gf_false;
        char               *conf_path           = NULL;
        char               *key                 = NULL;
        xlator_t           *this                = NULL;

        this = THIS;
        GF_ASSERT (this);
        priv = this->private;
        GF_ASSERT (priv);
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);
        GF_ASSERT (rsp_dict);

        ret = dict_get_int32 (dict, "type", &type);
        if (ret < 0)
                goto out;

        ret = dict_get_str (dict, "host-uuid", &host_uuid);
        if (ret < 0)
                goto out;

        if (type == GF_GSYNC_OPTION_TYPE_STATUS) {
                ret = glusterd_get_gsync_status (dict, op_errstr, rsp_dict);
                goto out;
        }

        ret = dict_get_str (dict, "slave", &slave);
        if (ret < 0)
                goto out;

        key = slave;

        ret = dict_get_str (dict, "slave_url", &slave_url);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Unable to fetch slave url.");
                goto out;
        }

        ret = dict_get_str (dict, "slave_host", &slave_host);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Unable to fetch slave hostname.");
                goto out;
        }

        ret = dict_get_str (dict, "slave_vol", &slave_vol);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Unable to fetch slave volume name.");
                goto out;
        }

        ret = dict_get_str (dict, "conf_path", &conf_path);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Unable to fetch conf file path.");
                goto out;
        }

        if (dict_get_str (dict, "master", &volname) == 0) {
                ret = glusterd_volinfo_find (volname, &volinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_DICT_GET_FAILED, "Volinfo for"
                                " %s (master) not found", volname);
                        goto out;
                }

                ret = glusterd_get_local_brickpaths (volinfo, &path_list);
        }

        if (type == GF_GSYNC_OPTION_TYPE_CONFIG) {
                ret = glusterd_gsync_configure (volinfo, slave, path_list,
                                                dict, rsp_dict, op_errstr);
                if (!ret) {
                        ret = dict_set_str (rsp_dict, "conf_path", conf_path);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED,
                                        "Unable to store conf_file_path.");
                                goto out;
                        }
                }
                goto out;
        }

        if (type == GF_GSYNC_OPTION_TYPE_DELETE) {
                ret = glusterd_remove_slave_in_info(volinfo, slave, op_errstr);
                if (ret && !is_force && path_list)
                        goto out;

                ret = glusterd_gsync_delete (volinfo, slave, slave_host,
                                             slave_vol, path_list, dict,
                                             rsp_dict, op_errstr);
                goto out;
        }

        if (!volinfo) {
                ret = -1;
                goto out;
        }

        is_force = dict_get_str_boolean (dict, "force", _gf_false);

        if (type == GF_GSYNC_OPTION_TYPE_START) {
                /* Add slave to the dict indicating geo-rep session is running*/
                ret = dict_set_dynstr_with_alloc (volinfo->gsync_active_slaves,
                                                  key, "running");
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_DICT_SET_FAILED, "Unable to set key:%s"
                                " value:running in the dict", key);
                        goto out;
                }

                /* If slave volume uuid is not present in gsync_slaves
                 * update it*/
                ret = glusterd_update_slave_voluuid_slaveinfo (volinfo);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_REMOTE_VOL_UUID_FAIL, "Error in updating"
                                " slave volume uuid for old slave info");
                        goto out;
                }

                ret = glusterd_start_gsync (volinfo, slave, path_list,
                                            conf_path, host_uuid, op_errstr,
                                            _gf_false);

                /* Delete added slave in the dict if start fails*/
                if (ret)
                        dict_del (volinfo->gsync_active_slaves, key);
       }

        if (type == GF_GSYNC_OPTION_TYPE_STOP ||
            type == GF_GSYNC_OPTION_TYPE_PAUSE ||
            type == GF_GSYNC_OPTION_TYPE_RESUME) {
                ret = glusterd_check_gsync_running_local (volinfo->volname,
                                                          slave, conf_path,
                                                          &is_running);
                if (!ret && !is_force && path_list &&
                    (_gf_true != is_running)) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                GD_MSG_GSYNCD_OP_SET_FAILED, GEOREP" is not "
                                "set up for %s(master) and %s(slave)",
                                volname, slave);
                        *op_errstr = strdup (GEOREP" is not set up");
                        goto out;
                }

                if (type == GF_GSYNC_OPTION_TYPE_PAUSE) {
                        ret = gd_pause_or_resume_gsync (dict, volname, slave,
                                                        slave_host, slave_vol,
                                                        conf_path, op_errstr,
                                                        _gf_true);
                        if (ret)
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_PAUSE_FAILED,
                                        GEOREP" Pause Failed");
                        else
                                dict_del (volinfo->gsync_active_slaves, key);

                } else if (type == GF_GSYNC_OPTION_TYPE_RESUME) {

                        /* Add slave to the dict indicating geo-rep session is
                         * running*/
                        ret = dict_set_dynstr_with_alloc (
                                              volinfo->gsync_active_slaves,
                                              key, "running");
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_DICT_SET_FAILED, "Unable to set "
                                        "key:%s value:running in dict", key);
                              goto out;
                        }

                        ret = gd_pause_or_resume_gsync (dict, volname, slave,
                                                        slave_host, slave_vol,
                                                        conf_path, op_errstr,
                                                        _gf_false);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        GD_MSG_RESUME_FAILED,
                                        GEOREP" Resume Failed");
                                dict_del (volinfo->gsync_active_slaves, key);
                        }
                } else {

                        ret = stop_gsync (volname, slave, &status_msg,
                                          conf_path, op_errstr, is_force);

                        if (ret == 0 && status_msg)
                                ret = dict_set_str (rsp_dict, "gsync-status",
                                                    status_msg);
                        if (!ret) {
                                ret = glusterd_create_status_file (
                                                           volinfo->volname,
                                                           slave, slave_host,
                                                           slave_vol,"Stopped");
                                if (ret) {
                                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                                GD_MSG_UPDATE_STATEFILE_FAILED,
                                                "Unable to update state_file. "
                                                "Error : %s", strerror (errno));
                                }
                                dict_del (volinfo->gsync_active_slaves, key);
                        }
                }
        }

out:
        if (path_list) {
                GF_FREE (path_list);
                path_list = NULL;
        }

        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_get_slave_details_confpath (glusterd_volinfo_t *volinfo,
                                     dict_t *dict, char **slave_url,
                                     char **slave_host, char **slave_vol,
                                     char **conf_path, char **op_errstr)
{
        int              ret                  = -1;
        char               confpath[PATH_MAX] = "";
        glusterd_conf_t *priv                 = NULL;
        char            *slave                = NULL;
        xlator_t *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);

        ret = dict_get_str (dict, "slave", &slave);
        if (ret || !slave) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "Unable to fetch slave from dict");
                ret = -1;
                goto out;
        }

        ret = glusterd_get_slave_info (slave, slave_url,
                                       slave_host, slave_vol, op_errstr);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SLAVEINFO_FETCH_ERROR,
                        "Unable to fetch slave details.");
                ret = -1;
                goto out;
        }

        ret = dict_set_str (dict, "slave_url", *slave_url);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                        "Unable to store slave IP.");
                goto out;
        }

        ret = dict_set_str (dict, "slave_host", *slave_host);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                        "Unable to store slave hostname");
                goto out;
        }

        ret = dict_set_str (dict, "slave_vol", *slave_vol);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                        "Unable to store slave volume name.");
                goto out;
        }

        ret = snprintf (confpath, sizeof(confpath) - 1,
                        "%s/"GEOREP"/%s_%s_%s/gsyncd.conf",
                        priv->workdir, volinfo->volname,
                        *slave_host, *slave_vol);
        confpath[ret] = '\0';
        *conf_path = gf_strdup (confpath);
        if (!(*conf_path)) {
                gf_msg (this->name, GF_LOG_ERROR, errno, GD_MSG_STRDUP_FAILED,
                        "Unable to gf_strdup. Error: %s", strerror (errno));
                ret = -1;
                goto out;
        }

        ret = dict_set_str (dict, "conf_path", *conf_path);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                        "Unable to store conf_path");
                goto out;
        }

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;

}

int
glusterd_get_slave_info (char *slave,
                         char **slave_url, char **hostname,
                         char **slave_vol, char **op_errstr)
{
        char     *tmp              = NULL;
        char     *save_ptr         = NULL;
        char    **linearr          = NULL;
        int32_t   ret              = -1;
        char      errmsg[PATH_MAX] = "";
        xlator_t *this             = NULL;

        this = THIS;
        GF_ASSERT (this);

        ret = glusterd_urltransform_single (slave, "normalize",
                                            &linearr);
        if (ret == -1) {
                ret = snprintf (errmsg, sizeof(errmsg) - 1,
                                "Invalid Url: %s", slave);
                errmsg[ret] = '\0';
                *op_errstr = gf_strdup (errmsg);
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_NORMALIZE_URL_FAIL,
                        "Failed to normalize url");
                goto out;
        }

        tmp = strtok_r (linearr[0], "/", &save_ptr);
        tmp = strtok_r (NULL, "/", &save_ptr);
        slave = strtok_r (tmp, ":", &save_ptr);
        if (slave) {
                ret = glusterd_geo_rep_parse_slave (slave, hostname, op_errstr);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_SLAVE_URL_INVALID,
                                "Invalid slave url: %s", *op_errstr);
                        goto out;
                }
                gf_msg_debug (this->name, 0, "Hostname : %s", *hostname);

                *slave_url = gf_strdup (slave);
                if (!*slave_url) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_STRDUP_FAILED,
                                "Failed to gf_strdup");
                        ret = -1;
                        goto out;
                }
                gf_msg_debug (this->name, 0, "Slave URL : %s", *slave_url);
                ret = 0;
        } else {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INVALID_ENTRY, "Invalid slave name");
                goto out;
        }

        slave = strtok_r (NULL, ":", &save_ptr);
        if (slave) {
                *slave_vol = gf_strdup (slave);
                if (!*slave_vol) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                GD_MSG_STRDUP_FAILED,
                                "Failed to gf_strdup");
                        ret = -1;
                        GF_FREE (*slave_url);
                        goto out;
                }
                gf_msg_debug (this->name, 0, "Slave Vol : %s", *slave_vol);
                ret = 0;
        } else {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        GD_MSG_INVALID_ENTRY, "Invalid slave name");
                goto out;
        }

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

static void
runinit_gsyncd_setrx (runner_t *runner, char *conf_path)
{
        runinit (runner);
        runner_add_args (runner, GSYNCD_PREFIX"/gsyncd", "-c", NULL);
        runner_argprintf (runner, "%s", conf_path);
        runner_add_arg (runner, "--config-set-rx");
}

static int
glusterd_check_gsync_present (int *valid_state)
{
        char                buff[PATH_MAX] = {0, };
        runner_t            runner = {0,};
        char               *ptr = NULL;
        int                 ret = 0;
        xlator_t           *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        runinit (&runner);
        runner_add_args (&runner, GSYNCD_PREFIX"/gsyncd", "--version", NULL);
        runner_redir (&runner, STDOUT_FILENO, RUN_PIPE);
        ret = runner_start (&runner);
        if (ret == -1) {
                if (errno == ENOENT) {
                        gf_msg ("glusterd", GF_LOG_INFO, ENOENT,
                                GD_MSG_MODULE_NOT_INSTALLED, GEOREP" module "
                                "not installed in the system");
                        *valid_state = 0;
                }
                else {
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_MODULE_ERROR,
                                GEOREP" module not working as desired");
                        *valid_state = -1;
                }
                goto out;
        }

        ptr = fgets(buff, sizeof(buff), runner_chio (&runner, STDOUT_FILENO));
        if (ptr) {
                if (!strstr (buff, "gsyncd")) {
                        ret = -1;
                        gf_msg ("glusterd", GF_LOG_ERROR, 0,
                                GD_MSG_MODULE_ERROR,
                                 GEOREP" module not working as desired");
                        *valid_state = -1;
                        goto out;
                }
        } else {
                ret = -1;
                gf_msg ("glusterd", GF_LOG_ERROR, 0, GD_MSG_MODULE_ERROR,
                        GEOREP" module not working as desired");
                *valid_state = -1;
                goto out;
        }

        ret = 0;
 out:

        runner_end (&runner);

        gf_msg_debug ("glusterd", 0, "Returning %d", ret);
        return ret;

}

static int
create_conf_file (glusterd_conf_t *conf, char *conf_path)
#define RUN_GSYNCD_CMD do {                                                          \
        ret = runner_run_reuse (&runner);                                            \
        if (ret == -1) {                                                             \
                runner_log (&runner, "glusterd", GF_LOG_ERROR, "command failed");    \
                runner_end (&runner);                                                \
                goto out;                                                            \
        }                                                                            \
        runner_end (&runner);                                                        \
} while (0)
{
        int ret = 0;
        runner_t runner = {0,};
        char georepdir[PATH_MAX] = {0,};
        int valid_state = 0;

        valid_state = -1;
        ret = glusterd_check_gsync_present (&valid_state);
        if (-1 == ret) {
                ret = valid_state;
                goto out;
        }

        ret = snprintf (georepdir, sizeof(georepdir) - 1, "%s/"GEOREP,
                        conf->workdir);
        georepdir[ret] = '\0';

        /************
         * master pre-configuration
         ************/

        /* remote-gsyncd */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_args (&runner, "remote-gsyncd", GSYNCD_PREFIX"/gsyncd", ".", ".", NULL);
        RUN_GSYNCD_CMD;

        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_args (&runner, "remote-gsyncd", "/nonexistent/gsyncd",
                         ".", "^ssh:", NULL);
        RUN_GSYNCD_CMD;

        /* gluster-command-dir */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_args (&runner, "gluster-command-dir", SBIN_DIR"/",
                         ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* gluster-params */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_args (&runner, "gluster-params",
                         "aux-gfid-mount acl",
                         ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* ssh-command */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_arg (&runner, "ssh-command");
        runner_argprintf (&runner,
                          "ssh -oPasswordAuthentication=no "
                           "-oStrictHostKeyChecking=no "
                           "-i %s/secret.pem", georepdir);
        runner_add_args (&runner, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* ssh-command tar */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_arg (&runner, "ssh-command-tar");
        runner_argprintf (&runner,
                          "ssh -oPasswordAuthentication=no "
                           "-oStrictHostKeyChecking=no "
                           "-i %s/tar_ssh.pem", georepdir);
        runner_add_args (&runner, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* pid-file */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_arg (&runner, "pid-file");
        runner_argprintf (&runner, "%s/${mastervol}_${remotehost}_${slavevol}/monitor.pid", georepdir);
        runner_add_args (&runner, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* geo-rep-working-dir */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_arg (&runner, "georep-session-working-dir");
        runner_argprintf (&runner, "%s/${mastervol}_${remotehost}_${slavevol}/", georepdir);
        runner_add_args (&runner, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* state-file */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_arg (&runner, "state-file");
        runner_argprintf (&runner, "%s/${mastervol}_${remotehost}_${slavevol}/monitor.status", georepdir);
        runner_add_args (&runner, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* state-detail-file */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_arg (&runner, "state-detail-file");
        runner_argprintf (&runner, "%s/${mastervol}_${remotehost}_${slavevol}/${eSlave}-detail.status", georepdir);
        runner_add_args (&runner, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* state-socket */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_arg (&runner, "state-socket-unencoded");
        runner_argprintf (&runner, "%s/${mastervol}_${remotehost}_${slavevol}/${eSlave}.socket", georepdir);
        runner_add_args (&runner, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* socketdir */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_args (&runner, "socketdir", GLUSTERD_SOCK_DIR, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* log-file */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_args (&runner,
                         "log-file",
                         DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"/${mastervol}/${eSlave}.log",
                         ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* changelog-log-file */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_args (&runner,
                         "changelog-log-file",
                         DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"/${mastervol}/${eSlave}${local_id}-changes.log",
                         ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* gluster-log-file */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_args (&runner,
                         "gluster-log-file",
                         DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"/${mastervol}/${eSlave}${local_id}.gluster.log",
                         ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* ignore-deletes */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_args (&runner, "ignore-deletes", "false", ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* special-sync-mode */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_args (&runner, "special-sync-mode", "partial", ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /* change-detector == changelog */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_args(&runner, "change-detector", "changelog", ".", ".", NULL);
        RUN_GSYNCD_CMD;

        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_arg(&runner, "working-dir");
        runner_argprintf(&runner, "%s/${mastervol}/${eSlave}",
                         DEFAULT_GLUSTERFSD_MISC_DIRETORY);
        runner_add_args (&runner, ".", ".", NULL);
        RUN_GSYNCD_CMD;

        /************
         * slave pre-configuration
         ************/

        /* gluster-command-dir */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_args (&runner, "gluster-command-dir", SBIN_DIR"/",
                         ".", NULL);
        RUN_GSYNCD_CMD;

        /* gluster-params */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_args (&runner, "gluster-params",
                         "aux-gfid-mount acl",
                         ".", NULL);
        RUN_GSYNCD_CMD;

        /* log-file */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_args (&runner,
                         "log-file",
                         DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves/${session_owner}:${local_node}${local_id}.${slavevol}.log",
                         ".", NULL);
        RUN_GSYNCD_CMD;

        /* MountBroker log-file */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_args (&runner,
                         "log-file-mbr",
                         DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves/mbr/${session_owner}:${local_node}${local_id}.${slavevol}.log",
                         ".", NULL);
        RUN_GSYNCD_CMD;

        /* gluster-log-file */
        runinit_gsyncd_setrx (&runner, conf_path);
        runner_add_args (&runner,
                         "gluster-log-file",
                         DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"-slaves/${session_owner}:${local_node}${local_id}.${slavevol}.gluster.log",
                         ".", NULL);
        RUN_GSYNCD_CMD;

 out:
        return ret ? -1 : 0;
}

static int
glusterd_create_essential_dir_files (glusterd_volinfo_t *volinfo, dict_t *dict,
                                     char *slave, char *slave_host,
                                     char *slave_vol, char **op_errstr)
{
        int                ret              = -1;
        char              *conf_path        = NULL;
        char              *statefile        = NULL;
        char               buf[PATH_MAX]    = "";
        char               errmsg[PATH_MAX] = "";
        glusterd_conf_t   *conf             = NULL;
        struct stat        stbuf            = {0,};
        xlator_t          *this = NULL;

        this = THIS;
        GF_ASSERT (this);

        conf = this->private;

        ret = dict_get_str (dict, "conf_path", &conf_path);
        if (ret) {
                snprintf (errmsg, sizeof (errmsg),
                          "Unable to fetch conf file path.");
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "%s", errmsg);
                goto out;
        }

        ret = dict_get_str (dict, "statefile", &statefile);
        if (ret) {
                snprintf (errmsg, sizeof (errmsg),
                          "Unable to fetch statefile path.");
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "%s", errmsg);
                goto out;
        }

        ret = snprintf (buf, sizeof(buf) - 1, "%s/"GEOREP"/%s_%s_%s",
                        conf->workdir, volinfo->volname, slave_host, slave_vol);
        buf[ret] = '\0';
        ret = mkdir_p (buf, 0777, _gf_true);
        if (ret) {
                snprintf (errmsg, sizeof (errmsg), "Unable to create %s"
                          ". Error : %s", buf, strerror (errno));
                *op_errstr = gf_strdup (errmsg);
                gf_msg (this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
                        "%s", errmsg);
                goto out;
        }

        ret = snprintf (buf, PATH_MAX, DEFAULT_LOG_FILE_DIRECTORY"/"GEOREP"/%s",
                        volinfo->volname);
        buf[ret] = '\0';
        ret = mkdir_p (buf, 0777, _gf_true);
        if (ret) {
                snprintf (errmsg, sizeof (errmsg), "Unable to create %s"
                          ". Error : %s", buf, strerror (errno));
                *op_errstr = gf_strdup (errmsg);
                gf_msg (this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
                        "%s", errmsg);
                goto out;
        }

        ret = sys_lstat (conf_path, &stbuf);
        if (!ret) {
                gf_msg_debug (this->name, 0, "Session already running."
                              " Not creating config file again.");
        } else {
                ret = create_conf_file (conf, conf_path);
                if (ret || sys_lstat (conf_path, &stbuf)) {
                        snprintf (errmsg, sizeof (errmsg), "Failed to create"
                                  " config file(%s).", conf_path);
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_FILE_OP_FAILED, "%s", errmsg);
                        goto out;
                }
        }

        ret = sys_lstat (statefile, &stbuf);
        if (!ret) {
                gf_msg_debug (this->name, 0, "Session already running."
                              " Not creating status file again.");
                goto out;
        } else {
                ret = glusterd_create_status_file (volinfo->volname, slave,
                                                   slave_host, slave_vol,
                                                   "Created");
                if (ret || sys_lstat (statefile, &stbuf)) {
                        snprintf (errmsg, sizeof (errmsg), "Unable to create %s"
                                  ". Error : %s", statefile, strerror (errno));
                        *op_errstr = gf_strdup (errmsg);
                        gf_msg (this->name, GF_LOG_ERROR, errno,
                                GD_MSG_FILE_OP_FAILED, "%s", errmsg);
                        ret = -1;
                        goto out;
                }
        }

out:
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}

int
glusterd_op_gsync_create (dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
        char                common_pem_file[PATH_MAX] = "";
        char                errmsg[PATH_MAX]  = {0,};
        char                hooks_args[PATH_MAX]      = "";
        char                uuid_str [64]             = "";
        char               *host_uuid                 = NULL;
        char               *slave_url                 = NULL;
        char               *slave_url_buf             = NULL;
        char               *slave_user                = NULL;
        char               *slave_ip                  = NULL;
        char               *save_ptr                  = NULL;
        char               *slave_host                = NULL;
        char               *slave_vol                 = NULL;
        char               *arg_buf                   = NULL;
        char               *volname                   = NULL;
        char               *slave                     = NULL;
        int32_t             ret                       = -1;
        int32_t             is_pem_push               = -1;
        int32_t             ssh_port                  = 22;
        gf_boolean_t        is_force                  = -1;
        glusterd_conf_t    *conf                      = NULL;
        glusterd_volinfo_t *volinfo                   = NULL;
        xlator_t           *this                      = NULL;
        char                old_working_dir[PATH_MAX] = {0};
        char                new_working_dir[PATH_MAX] = {0};
        char               *slave_voluuid             = NULL;
        char               *old_slavehost             = NULL;
        gf_boolean_t        is_existing_session       = _gf_false;

        this = THIS;
        GF_ASSERT (this);
        conf = this->private;
        GF_ASSERT (conf);
        GF_ASSERT (dict);
        GF_ASSERT (op_errstr);

        ret = glusterd_op_gsync_args_get (dict, op_errstr,
                                          &volname, &slave, &host_uuid);
        if (ret)
                goto out;

        snprintf (common_pem_file, sizeof(common_pem_file),
                  "%s"GLUSTERD_COMMON_PEM_PUB_FILE, conf->workdir);

        ret = glusterd_volinfo_find (volname, &volinfo);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
                        "Volinfo for %s (master) not found", volname);
                goto out;
        }

        ret = dict_get_str (dict, "slave_vol", &slave_vol);
        if (ret) {
                snprintf (errmsg, sizeof (errmsg),
                          "Unable to fetch slave volume name.");
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "%s", errmsg);
                goto out;
        }

        ret = dict_get_str (dict, "slave_url", &slave_url);
        if (ret) {
                snprintf (errmsg, sizeof (errmsg),
                          "Unable to fetch slave IP.");
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "%s", errmsg);
                ret = -1;
                goto out;
        }

        /* Fetch the slave_user and slave_ip from the slave_url.
         * If the slave_user is not present. Use "root"
         */
        if (strstr(slave_url, "@")) {
                slave_url_buf = gf_strdup (slave_url);
                if (!slave_url_buf) {
                        ret = -1;
                        goto out;
                }
                slave_user = strtok_r (slave_url, "@", &save_ptr);
                slave_ip = strtok_r (NULL, "@", &save_ptr);
        } else {
                slave_user = "root";
                slave_ip = slave_url;
        }

        if (!slave_user || !slave_ip) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_SLAVE_URL_INVALID,
                        "Invalid slave url.");
                ret = -1;
                goto out;
        }

        ret = dict_get_str (dict, "slave_host", &slave_host);
        if (ret) {
                snprintf (errmsg, sizeof (errmsg),
                          "Unable to fetch slave host");
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "%s", errmsg);
                ret = -1;
                goto out;
        }

        ret = dict_get_int32 (dict, "ssh_port", &ssh_port);
        if (ret < 0 && ret != -ENOENT) {
                snprintf (errmsg, sizeof (errmsg), "Fetching ssh_port failed");
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                        "%s", errmsg);
                ret = -1;
                goto out;
        }

        is_force = dict_get_str_boolean (dict, "force", _gf_false);

        uuid_utoa_r (MY_UUID, uuid_str);
        if (!strcmp (uuid_str, host_uuid)) {
                ret = dict_get_int32 (dict, "push_pem", &is_pem_push);
                if (!ret && is_pem_push) {
                        gf_msg_debug (this->name, 0, "Trying to setup"
                                      " pem files in slave");
                        is_pem_push = 1;
                } else
                        is_pem_push = 0;

                snprintf(hooks_args, sizeof(hooks_args),
                         "is_push_pem=%d,pub_file=%s,slave_user=%s,slave_ip=%s,"
                         "slave_vol=%s,ssh_port=%d", is_pem_push,
                         common_pem_file, slave_user, slave_ip, slave_vol,
                         ssh_port);
        } else
                snprintf(hooks_args, sizeof(hooks_args),
                         "This argument will stop the hooks script");

        arg_buf = gf_strdup (hooks_args);
        if (!arg_buf) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_STRDUP_FAILED,
                        "Failed to gf_strdup");
                if (is_force) {
                        ret = 0;
                        goto create_essentials;
                }
                ret = -1;
                goto out;
        }

        ret = dict_set_str (dict, "hooks_args", arg_buf);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                        "Failed to set hooks_args in dict.");
                if (is_force) {
                        ret = 0;
                        goto create_essentials;
                }
                goto out;
        }

create_essentials:
        /* Fetch slave volume uuid, to get stored in volume info. */
        ret = dict_get_str (dict, "slave_voluuid", &slave_voluuid);
        if (ret) {
                snprintf (errmsg, sizeof (errmsg),
                          "Unable to fetch slave volume uuid from dict");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                         GD_MSG_DICT_GET_FAILED,
                         "%s", errmsg);
                ret = -1;
                goto out;
        }

        is_existing_session = dict_get_str_boolean (dict, "existing_session",
                                                    _gf_false);
        if (is_existing_session) {
                ret = dict_get_str (dict, "old_slavehost", &old_slavehost);
                if (ret) {
                        snprintf (errmsg, sizeof (errmsg),
                                  "Unable to fetch old_slavehost");
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                 GD_MSG_DICT_GET_FAILED,
                                 "%s", errmsg);
                        ret = -1;
                        goto out;
                }

                /* Rename existing geo-rep session with new Slave Host */
                ret = snprintf (old_working_dir,
                         sizeof (old_working_dir) - 1,
                         "%s/"GEOREP"/%s_%s_%s", conf->workdir,
                         volinfo->volname, old_slavehost,
                         slave_vol);

                ret = snprintf (new_working_dir,
                         sizeof (new_working_dir) - 1,
                         "%s/"GEOREP"/%s_%s_%s", conf->workdir,
                         volinfo->volname, slave_host, slave_vol);

                ret = sys_rename (old_working_dir, new_working_dir);
                if (!ret) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                GD_MSG_FORCE_CREATE_SESSION,
                                "rename of old working dir %s to "
                                "new working dir %s is done! ",
                                 old_working_dir, new_working_dir);
                } else {
                        if (errno == ENOENT) {
                                /* log error, but proceed with directory
                                 * creation below */
                                gf_msg_debug (this->name, 0,
                                              "old_working_dir(%s) "
                                               "not present.",
                                               old_working_dir);
                        } else {
                                snprintf (errmsg, sizeof (errmsg),
                                          "rename of old working dir %s to "
                                          "new working dir %s failed! Error: %s",
                                          old_working_dir, new_working_dir,
                                          strerror (errno));
                                gf_msg (this->name,  GF_LOG_INFO, 0,
                                        GD_MSG_FORCE_CREATE_SESSION,
                                        "rename of old working dir %s to "
                                        "new working dir %s failed! Error: %s!",
                                        old_working_dir, new_working_dir,
                                        strerror (errno));

                                ret = -1;
                                goto out;
                        }
                }
        }

        ret = glusterd_create_essential_dir_files (volinfo, dict, slave,
                                                   slave_host, slave_vol,
                                                   op_errstr);
        if (ret)
                goto out;

        ret = glusterd_store_slave_in_info (volinfo, slave,
                                            host_uuid, slave_voluuid,
                                            op_errstr, is_force);
        if (ret) {
                snprintf (errmsg, sizeof (errmsg), "Unable to store"
                          " slave info.");
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        GD_MSG_SLAVEINFO_STORE_ERROR,
                        "%s", errmsg);
                goto out;
        }

        /* Enable marker and changelog */
        ret = glusterd_set_gsync_confs (volinfo);
        if (ret != 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        GD_MSG_MARKER_START_FAIL, "marker/changelog"
                        " start failed");
                snprintf (errmsg, sizeof (errmsg),
                          "Index initialization failed");

                ret = -1;
                goto out;
        }

out:
        if (ret && errmsg[0] != '\0') {
                gf_msg (this->name, GF_LOG_ERROR, 0, GD_MSG_GSYNCD_ERROR,
                        "%s", errmsg);
                *op_errstr = gf_strdup (errmsg);
        }

        GF_FREE (slave_url_buf);
        gf_msg_debug (this->name, 0, "Returning %d", ret);
        return ret;
}
