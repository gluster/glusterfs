/*
   Copyright (c) 2011-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "glusterd-op-sm.h"
#include "glusterd-geo-rep.h"
#include "glusterd-store.h"
#include "glusterd-utils.h"
#include "glusterd-volgen.h"
#include "glusterd-svc-helper.h"
#include <glusterfs/run.h>
#include <glusterfs/syscall.h>
#include "glusterd-messages.h"

#include <signal.h>

static int
dict_get_param(dict_t *dict, char *key, char **param);

struct gsync_config_opt_vals_ gsync_confopt_vals[] = {
    {
        .op_name = "change_detector",
        .no_of_pos_vals = 2,
        .case_sensitive = _gf_true,
        .values = {"xsync", "changelog"},
    },
    {.op_name = "special_sync_mode",
     .no_of_pos_vals = 2,
     .case_sensitive = _gf_true,
     .values = {"partial", "recover"}},
    {.op_name = "log-level",
     .no_of_pos_vals = 5,
     .case_sensitive = _gf_false,
     .values = {"critical", "error", "warning", "info", "debug"}},
    {.op_name = "use-tarssh",
     .no_of_pos_vals = 6,
     .case_sensitive = _gf_false,
     .values = {"true", "false", "0", "1", "yes", "no"}},
    {.op_name = "ignore_deletes",
     .no_of_pos_vals = 6,
     .case_sensitive = _gf_false,
     .values = {"true", "false", "0", "1", "yes", "no"}},
    {.op_name = "use_meta_volume",
     .no_of_pos_vals = 6,
     .case_sensitive = _gf_false,
     .values = {"true", "false", "0", "1", "yes", "no"}},
    {.op_name = "use-meta-volume",
     .no_of_pos_vals = 6,
     .case_sensitive = _gf_false,
     .values = {"true", "false", "0", "1", "yes", "no"}},
    {
        .op_name = NULL,
    },
};

static char *gsync_reserved_opts[] = {
    "gluster-command",        "pid-file",  "state-file", "session-owner",
    "state-socket-unencoded", "socketdir", "local-id",   "local-path",
    "secondary-id",           NULL};

static char *gsync_no_restart_opts[] = {"checkpoint", "log_rsync_performance",
                                        "log-rsync-performance", NULL};

void
set_gsyncd_inet6_arg(runner_t *runner)
{
    char *af;
    int ret;

    ret = dict_get_str(THIS->options, "transport.address-family", &af);
    if (ret == 0)
        runner_argprintf(runner, "--%s", af);
}

int
__glusterd_handle_sys_exec(rpcsvc_request_t *req)
{
    int32_t ret = 0;
    dict_t *dict = NULL;
    gf_cli_req cli_req = {
        {0},
    };
    glusterd_op_t cli_op = GD_OP_SYS_EXEC;
    glusterd_conf_t *priv = NULL;
    char *host_uuid = NULL;
    char err_str[64] = {
        0,
    };
    xlator_t *this = THIS;

    GF_ASSERT(req);

    priv = this->private;
    GF_ASSERT(priv);

    ret = xdr_to_generic(req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
    if (ret < 0) {
        req->rpc_err = GARBAGE_ARGS;
        snprintf(err_str, sizeof(err_str), "Garbage args received");
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_GARBAGE_ARGS, NULL);
        goto out;
    }

    if (cli_req.dict.dict_len) {
        dict = dict_new();
        if (!dict) {
            gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL,
                    NULL);
            goto out;
        }

        ret = dict_unserialize(cli_req.dict.dict_val, cli_req.dict.dict_len,
                               &dict);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_UNSERIALIZE_FAIL,
                   "failed to "
                   "unserialize req-buffer to dictionary");
            snprintf(err_str, sizeof(err_str),
                     "Unable to decode "
                     "the command");
            goto out;
        } else {
            dict->extra_stdfree = cli_req.dict.dict_val;
        }

        host_uuid = gf_strdup(uuid_utoa(MY_UUID));
        if (host_uuid == NULL) {
            snprintf(err_str, sizeof(err_str),
                     "Failed to get "
                     "the uuid of local glusterd");
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_UUID_GET_FAIL,
                    NULL);
            ret = -1;
            goto out;
        }

        ret = dict_set_dynstr(dict, "host-uuid", host_uuid);
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                    "Key=host-uuid", NULL);
            goto out;
        }
    }

    ret = glusterd_op_begin_synctask(req, cli_op, dict);

out:
    if (ret) {
        if (err_str[0] == '\0')
            snprintf(err_str, sizeof(err_str), "Operation failed");
        ret = glusterd_op_send_cli_response(cli_op, ret, 0, req, dict, err_str);
    }
    return ret;
}

int
__glusterd_handle_copy_file(rpcsvc_request_t *req)
{
    int32_t ret = 0;
    dict_t *dict = NULL;
    gf_cli_req cli_req = {
        {0},
    };
    glusterd_op_t cli_op = GD_OP_COPY_FILE;
    glusterd_conf_t *priv = NULL;
    char *host_uuid = NULL;
    char err_str[64] = {
        0,
    };
    xlator_t *this = THIS;

    GF_ASSERT(req);

    priv = this->private;
    GF_ASSERT(priv);

    ret = xdr_to_generic(req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
    if (ret < 0) {
        req->rpc_err = GARBAGE_ARGS;
        snprintf(err_str, sizeof(err_str), "Garbage args received");
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_GARBAGE_ARGS, NULL);
        goto out;
    }

    if (cli_req.dict.dict_len) {
        dict = dict_new();
        if (!dict) {
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL,
                    NULL);
            goto out;
        }

        ret = dict_unserialize(cli_req.dict.dict_val, cli_req.dict.dict_len,
                               &dict);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_UNSERIALIZE_FAIL,
                   "failed to"
                   "unserialize req-buffer to dictionary");
            snprintf(err_str, sizeof(err_str),
                     "Unable to decode "
                     "the command");
            goto out;
        } else {
            dict->extra_stdfree = cli_req.dict.dict_val;
        }

        host_uuid = gf_strdup(uuid_utoa(MY_UUID));
        if (host_uuid == NULL) {
            snprintf(err_str, sizeof(err_str),
                     "Failed to get "
                     "the uuid of local glusterd");
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_UUID_GET_FAIL,
                    NULL);
            ret = -1;
            goto out;
        }

        ret = dict_set_dynstr(dict, "host-uuid", host_uuid);
        if (ret)
            goto out;
    }

    ret = glusterd_op_begin_synctask(req, cli_op, dict);

out:
    if (ret) {
        if (err_str[0] == '\0')
            snprintf(err_str, sizeof(err_str), "Operation failed");
        ret = glusterd_op_send_cli_response(cli_op, ret, 0, req, dict, err_str);
    }
    return ret;
}

int
__glusterd_handle_gsync_set(rpcsvc_request_t *req)
{
    int32_t ret = 0;
    dict_t *dict = NULL;
    gf_cli_req cli_req = {
        {0},
    };
    glusterd_op_t cli_op = GD_OP_GSYNC_SET;
    char *primary = NULL;
    char *secondary = NULL;
    char operation[64] = {
        0,
    };
    int type = 0;
    glusterd_conf_t *priv = NULL;
    char *host_uuid = NULL;
    char err_str[64] = {
        0,
    };
    xlator_t *this = THIS;

    GF_ASSERT(req);

    priv = this->private;
    GF_ASSERT(priv);

    ret = xdr_to_generic(req->msg[0], &cli_req, (xdrproc_t)xdr_gf_cli_req);
    if (ret < 0) {
        req->rpc_err = GARBAGE_ARGS;
        snprintf(err_str, sizeof(err_str), "Garbage args received");
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_GARBAGE_ARGS, NULL);
        goto out;
    }

    if (cli_req.dict.dict_len) {
        dict = dict_new();
        if (!dict) {
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL,
                    NULL);
            goto out;
        }

        ret = dict_unserialize(cli_req.dict.dict_val, cli_req.dict.dict_len,
                               &dict);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_UNSERIALIZE_FAIL,
                   "failed to "
                   "unserialize req-buffer to dictionary");
            snprintf(err_str, sizeof(err_str),
                     "Unable to decode "
                     "the command");
            goto out;
        } else {
            dict->extra_stdfree = cli_req.dict.dict_val;
        }

        host_uuid = gf_strdup(uuid_utoa(MY_UUID));
        if (host_uuid == NULL) {
            snprintf(err_str, sizeof(err_str),
                     "Failed to get "
                     "the uuid of local glusterd");
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_UUID_GET_FAIL,
                    NULL);
            ret = -1;
            goto out;
        }
        ret = dict_set_dynstr(dict, "host-uuid", host_uuid);
        if (ret)
            goto out;
    }

    ret = dict_get_str(dict, "primary", &primary);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
               "primary not found, while handling " GEOREP " options");
        primary = "(No Primary)";
    }

    ret = dict_get_str(dict, "secondary", &secondary);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
               "secondary not found, while handling " GEOREP " options");
        secondary = "(No Secondary)";
    }

    ret = dict_get_int32(dict, "type", &type);
    if (ret < 0) {
        snprintf(err_str, sizeof(err_str),
                 "Command type not found "
                 "while handling " GEOREP " options");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               err_str);
        goto out;
    }

    switch (type) {
        case GF_GSYNC_OPTION_TYPE_CREATE:
            snprintf(operation, sizeof(operation), "create");
            cli_op = GD_OP_GSYNC_CREATE;
            break;

        case GF_GSYNC_OPTION_TYPE_START:
            snprintf(operation, sizeof(operation), "start");
            break;

        case GF_GSYNC_OPTION_TYPE_STOP:
            snprintf(operation, sizeof(operation), "stop");
            break;

        case GF_GSYNC_OPTION_TYPE_PAUSE:
            snprintf(operation, sizeof(operation), "pause");
            break;

        case GF_GSYNC_OPTION_TYPE_RESUME:
            snprintf(operation, sizeof(operation), "resume");
            break;

        case GF_GSYNC_OPTION_TYPE_CONFIG:
            snprintf(operation, sizeof(operation), "config");
            break;

        case GF_GSYNC_OPTION_TYPE_STATUS:
            snprintf(operation, sizeof(operation), "status");
            break;
    }

    ret = glusterd_op_begin_synctask(req, cli_op, dict);

out:
    if (ret) {
        if (err_str[0] == '\0')
            snprintf(err_str, sizeof(err_str), "Operation failed");
        ret = glusterd_op_send_cli_response(cli_op, ret, 0, req, dict, err_str);
    }
    return ret;
}

int
glusterd_handle_sys_exec(rpcsvc_request_t *req)
{
    return glusterd_big_locked_handler(req, __glusterd_handle_sys_exec);
}

int
glusterd_handle_copy_file(rpcsvc_request_t *req)
{
    return glusterd_big_locked_handler(req, __glusterd_handle_copy_file);
}

int
glusterd_handle_gsync_set(rpcsvc_request_t *req)
{
    return glusterd_big_locked_handler(req, __glusterd_handle_gsync_set);
}

/*****
 *
 * glusterd_urltransform* internal API
 *
 *****/

static void
glusterd_urltransform_init(runner_t *runner, const char *transname)
{
    runinit(runner);
    runner_add_arg(runner, GSYNCD_PREFIX "/gsyncd");
    set_gsyncd_inet6_arg(runner);
    runner_argprintf(runner, "--%s-url", transname);
}

static void
glusterd_urltransform_add(runner_t *runner, const char *url)
{
    runner_add_arg(runner, url);
}

/* Helper routine to terminate just before secondary_voluuid */
static int32_t
parse_secondary_url(char *sec_url, char **secondary)
{
    char *tmp = NULL;
    xlator_t *this = THIS;
    int32_t ret = -1;

    this = THIS;

    /* secondary format:
     * primary_node_uuid:ssh://secondary_host::secondary_vol:secondary_voluuid
     */
    *secondary = strchr(sec_url, ':');
    if (!(*secondary)) {
        goto out;
    }
    (*secondary)++;

    /* To terminate at : before secondary volume uuid */
    tmp = strstr(*secondary, "::");
    if (!tmp) {
        goto out;
    }
    tmp += 2;
    tmp = strchr(tmp, ':');
    if (!tmp)
        gf_msg_debug(this->name, 0, "old secondary: %s!", *secondary);
    else
        *tmp = '\0';

    ret = 0;
    gf_msg_debug(this->name, 0, "parsed secondary: %s!", *secondary);
out:
    return ret;
}

static int
_glusterd_urltransform_add_iter(dict_t *dict, char *key, data_t *value,
                                void *data)
{
    runner_t *runner = (runner_t *)data;
    char sec_url[VOLINFO_SECONDARY_URL_MAX] = {0};
    char *secondary = NULL;
    xlator_t *this = THIS;
    int32_t ret = -1;

    gf_msg_debug(this->name, 0, "value->data %s", value->data);

    if (snprintf(sec_url, sizeof(sec_url), "%s", value->data) >=
        sizeof(sec_url)) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SECONDARY_VOL_PARSE_FAIL,
               "Error in copying secondary: %s!", value->data);
        goto out;
    }

    ret = parse_secondary_url(sec_url, &secondary);
    if (ret == -1) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SECONDARY_VOL_PARSE_FAIL,
               "Error in parsing secondary: %s!", value->data);
        goto out;
    }

    runner_add_arg(runner, secondary);
    ret = 0;
out:
    return ret;
}

static void
glusterd_urltransform_free(char **linearr, unsigned n)
{
    int i = 0;

    for (; i < n; i++)
        GF_FREE(linearr[i]);

    GF_FREE(linearr);
}

static int
glusterd_urltransform(runner_t *runner, char ***linearrp)
{
    char **linearr = NULL;
    char *line = NULL;
    unsigned arr_len = 32;
    unsigned arr_idx = 0;
    gf_boolean_t error = _gf_false;
    xlator_t *this = THIS;

    linearr = GF_CALLOC(arr_len, sizeof(char *), gf_gld_mt_linearr);
    if (!linearr) {
        error = _gf_true;
        goto out;
    }

    runner_redir(runner, STDOUT_FILENO, RUN_PIPE);
    if (runner_start(runner) != 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SPAWNING_CHILD_FAILED,
               "spawning child failed");

        error = _gf_true;
        goto out;
    }

    arr_idx = 0;
    for (;;) {
        size_t len;
        line = GF_MALLOC(1024, gf_gld_mt_linebuf);
        if (!line) {
            error = _gf_true;
            goto out;
        }

        if (fgets(line, 1024, runner_chio(runner, STDOUT_FILENO)) == NULL) {
            GF_FREE(line);
            break;
        }

        len = strlen(line);
        if (len == 0 || line[len - 1] != '\n') {
            GF_FREE(line);
            error = _gf_true;
            goto out;
        }
        line[len - 1] = '\0';

        if (arr_idx == arr_len) {
            void *p = linearr;
            arr_len <<= 1;
            p = GF_REALLOC(linearr, arr_len);
            if (!p) {
                GF_FREE(line);
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
        kill(runner->chpid, SIGKILL);

    if (runner_end(runner) != 0)
        error = _gf_true;

    if (error) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_READ_CHILD_DATA_FAILED,
               "reading data from child failed");
        glusterd_urltransform_free(linearr, arr_idx);
        return -1;
    }

    *linearrp = linearr;
    return arr_idx;
}

static int
glusterd_urltransform_single(const char *url, const char *transname,
                             char ***linearrp)
{
    runner_t runner = {
        0,
    };

    glusterd_urltransform_init(&runner, transname);
    glusterd_urltransform_add(&runner, url);
    return glusterd_urltransform(&runner, linearrp);
}

struct dictidxmark {
    unsigned isrch;
    unsigned ithis;
    char *ikey;
};

struct secondary_vol_config {
    char old_sechost[_POSIX_HOST_NAME_MAX + 1];
    char old_secuser[LOGIN_NAME_MAX];
    unsigned old_slvidx;
    char secondary_voluuid[UUID_CANONICAL_FORM_LEN + 1];
};

static int
_dict_mark_atindex(dict_t *dict, char *key, data_t *value, void *data)
{
    struct dictidxmark *dim = data;

    if (dim->isrch == dim->ithis)
        dim->ikey = key;

    dim->ithis++;
    return 0;
}

static char *
dict_get_by_index(dict_t *dict, unsigned i)
{
    struct dictidxmark dim = {
        0,
    };

    dim.isrch = i;
    dict_foreach(dict, _dict_mark_atindex, &dim);

    return dim.ikey;
}

static int
glusterd_get_secondary(glusterd_volinfo_t *vol, const char *secondaryurl,
                       char **secondarykey)
{
    runner_t runner = {
        0,
    };
    int n = 0;
    int i = 0;
    char **linearr = NULL;
    int32_t ret = 0;

    glusterd_urltransform_init(&runner, "canonicalize");
    ret = dict_foreach(vol->gsync_secondaries, _glusterd_urltransform_add_iter,
                       &runner);
    if (ret < 0)
        return -2;

    glusterd_urltransform_add(&runner, secondaryurl);

    n = glusterd_urltransform(&runner, &linearr);
    if (n == -1)
        return -2;

    for (i = 0; i < n - 1; i++) {
        if (strcmp(linearr[i], linearr[n - 1]) == 0)
            break;
    }
    glusterd_urltransform_free(linearr, n);

    if (i < n - 1)
        *secondarykey = dict_get_by_index(vol->gsync_secondaries, i);
    else
        i = -1;

    return i;
}

static int
glusterd_query_extutil_generic(char *resbuf, size_t blen, runner_t *runner,
                               void *data,
                               int (*fcbk)(char *resbuf, size_t blen, FILE *fp,
                                           void *data))
{
    int ret = 0;
    xlator_t *this = THIS;

    runner_redir(runner, STDOUT_FILENO, RUN_PIPE);
    if (runner_start(runner) != 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SPAWNING_CHILD_FAILED,
               "spawning child failed");

        return -1;
    }

    ret = fcbk(resbuf, blen, runner_chio(runner, STDOUT_FILENO), data);

    ret |= runner_end(runner);
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_READ_CHILD_DATA_FAILED,
               "reading data from child failed");

    return ret ? -1 : 0;
}

static int
_fcbk_singleline(char *resbuf, size_t blen, FILE *fp, void *data)
{
    char *ptr = NULL;

    errno = 0;
    ptr = fgets(resbuf, blen, fp);
    if (ptr) {
        size_t len = strlen(resbuf);
        if (len && resbuf[len - 1] == '\n')
            resbuf[len - 1] = '\0';  // strip off \n
    }

    return errno ? -1 : 0;
}

static int
glusterd_query_extutil(char *resbuf, runner_t *runner)
{
    return glusterd_query_extutil_generic(resbuf, PATH_MAX, runner, NULL,
                                          _fcbk_singleline);
}

static int
glusterd_get_secondary_voluuid(char *secondary_host, char *secondary_vol,
                               char *vol_uuid)
{
    runner_t runner = {
        0,
    };
    glusterd_conf_t *priv = NULL;
    xlator_t *this = THIS;
    int ret = -1;

    priv = this->private;
    GF_VALIDATE_OR_GOTO(this->name, priv, out);

    runinit(&runner);
    runner_add_arg(&runner, GSYNCD_PREFIX "/gsyncd");
    set_gsyncd_inet6_arg(&runner);
    runner_add_arg(&runner, "--secondaryvoluuid-get");
    runner_argprintf(&runner, "%s::%s", secondary_host, secondary_vol);

    synclock_unlock(&priv->big_lock);
    ret = glusterd_query_extutil(vol_uuid, &runner);
    synclock_lock(&priv->big_lock);

out:
    return ret;
}

static int
_fcbk_conftodict(char *resbuf, size_t blen, FILE *fp, void *data)
{
    char *ptr = NULL;
    dict_t *dict = data;
    char *v = NULL;

    for (;;) {
        errno = 0;
        ptr = fgets(resbuf, blen - 2, fp);
        if (!ptr)
            break;
        v = resbuf + strlen(resbuf) - 1;
        while (isspace(*v))
            /* strip trailing space */
            *v-- = '\0';
        if (v == resbuf)
            /* skip empty line */
            continue;
        v = strchr(resbuf, ':');
        if (!v)
            return -1;
        *v++ = '\0';
        while (isspace(*v))
            v++;
        v = gf_strdup(v);
        if (!v)
            return -1;
        if (dict_set_dynstr(dict, resbuf, v) != 0) {
            GF_FREE(v);
            return -1;
        }
    }

    return errno ? -1 : 0;
}

static int
glusterd_gsync_get_config(char *primary, char *secondary, char *conf_path,
                          dict_t *dict)
{
    /* key + value, where value must be able to accommodate a path */
    char resbuf[256 + PATH_MAX] = {
        0,
    };
    runner_t runner = {
        0,
    };

    runinit(&runner);
    runner_add_args(&runner, GSYNCD_PREFIX "/gsyncd", "-c", NULL);
    runner_argprintf(&runner, "%s", conf_path);
    set_gsyncd_inet6_arg(&runner);
    runner_argprintf(&runner, "--iprefix=%s", DATADIR);
    runner_argprintf(&runner, ":%s", primary);
    runner_add_args(&runner, secondary, "--config-get-all", NULL);

    return glusterd_query_extutil_generic(resbuf, sizeof(resbuf), &runner, dict,
                                          _fcbk_conftodict);
}

static int
_fcbk_statustostruct(char *resbuf, size_t blen, FILE *fp, void *data)
{
    char *ptr = NULL;
    char *v = NULL;
    char *k = NULL;
    gf_gsync_status_t *sts_val = NULL;
    size_t len = 0;

    sts_val = (gf_gsync_status_t *)data;

    for (;;) {
        errno = 0;
        ptr = fgets(resbuf, blen - 2, fp);
        if (!ptr)
            break;

        v = resbuf + strlen(resbuf) - 1;
        while (isspace(*v))
            /* strip trailing space */
            *v-- = '\0';
        if (v == resbuf)
            /* skip empty line */
            continue;
        v = strchr(resbuf, ':');
        if (!v)
            return -1;
        *v++ = '\0';
        while (isspace(*v))
            v++;
        v = gf_strdup(v);
        if (!v)
            return -1;

        k = gf_strdup(resbuf);
        if (!k) {
            GF_FREE(v);
            return -1;
        }

        if (strcmp(k, "worker_status") == 0) {
            len = min(strlen(v), (sizeof(sts_val->worker_status) - 1));
            memcpy(sts_val->worker_status, v, len);
            sts_val->worker_status[len] = '\0';
        } else if (strcmp(k, "secondary_node") == 0) {
            len = min(strlen(v), (sizeof(sts_val->secondary_node) - 1));
            memcpy(sts_val->secondary_node, v, len);
            sts_val->secondary_node[len] = '\0';
        } else if (strcmp(k, "crawl_status") == 0) {
            len = min(strlen(v), (sizeof(sts_val->crawl_status) - 1));
            memcpy(sts_val->crawl_status, v, len);
            sts_val->crawl_status[len] = '\0';
        } else if (strcmp(k, "last_synced") == 0) {
            len = min(strlen(v), (sizeof(sts_val->last_synced) - 1));
            memcpy(sts_val->last_synced, v, len);
            sts_val->last_synced[len] = '\0';
        } else if (strcmp(k, "last_synced_utc") == 0) {
            len = min(strlen(v), (sizeof(sts_val->last_synced_utc) - 1));
            memcpy(sts_val->last_synced_utc, v, len);
            sts_val->last_synced_utc[len] = '\0';
        } else if (strcmp(k, "entry") == 0) {
            len = min(strlen(v), (sizeof(sts_val->entry) - 1));
            memcpy(sts_val->entry, v, len);
            sts_val->entry[len] = '\0';
        } else if (strcmp(k, "data") == 0) {
            len = min(strlen(v), (sizeof(sts_val->data) - 1));
            memcpy(sts_val->data, v, len);
            sts_val->data[len] = '\0';
        } else if (strcmp(k, "meta") == 0) {
            len = min(strlen(v), (sizeof(sts_val->meta) - 1));
            memcpy(sts_val->meta, v, len);
            sts_val->meta[len] = '\0';
        } else if (strcmp(k, "failures") == 0) {
            len = min(strlen(v), (sizeof(sts_val->failures) - 1));
            memcpy(sts_val->failures, v, len);
            sts_val->failures[len] = '\0';
        } else if (strcmp(k, "checkpoint_time") == 0) {
            len = min(strlen(v), (sizeof(sts_val->checkpoint_time) - 1));
            memcpy(sts_val->checkpoint_time, v, len);
            sts_val->checkpoint_time[len] = '\0';
        } else if (strcmp(k, "checkpoint_time_utc") == 0) {
            len = min(strlen(v), (sizeof(sts_val->checkpoint_time_utc) - 1));
            memcpy(sts_val->checkpoint_time_utc, v, len);
            sts_val->checkpoint_time_utc[len] = '\0';
        } else if (strcmp(k, "checkpoint_completed") == 0) {
            len = min(strlen(v), (sizeof(sts_val->checkpoint_completed) - 1));
            memcpy(sts_val->checkpoint_completed, v, len);
            sts_val->checkpoint_completed[len] = '\0';
        } else if (strcmp(k, "checkpoint_completion_time") == 0) {
            len = min(strlen(v),
                      (sizeof(sts_val->checkpoint_completion_time) - 1));
            memcpy(sts_val->checkpoint_completion_time, v, len);
            sts_val->checkpoint_completion_time[len] = '\0';
        } else if (strcmp(k, "checkpoint_completion_time_utc") == 0) {
            len = min(strlen(v),
                      (sizeof(sts_val->checkpoint_completion_time_utc) - 1));
            memcpy(sts_val->checkpoint_completion_time_utc, v, len);
            sts_val->checkpoint_completion_time_utc[len] = '\0';
        }
        GF_FREE(v);
        GF_FREE(k);
    }

    return errno ? -1 : 0;
}

static int
glusterd_gsync_get_status(char *primary, char *secondary, char *conf_path,
                          char *brick_path, gf_gsync_status_t *sts_val)
{
    /* key + value, where value must be able to accommodate a path */
    char resbuf[256 + PATH_MAX] = {
        0,
    };
    runner_t runner = {
        0,
    };

    runinit(&runner);
    runner_add_args(&runner, GSYNCD_PREFIX "/gsyncd", "-c", NULL);
    runner_argprintf(&runner, "%s", conf_path);
    set_gsyncd_inet6_arg(&runner);
    runner_argprintf(&runner, "--iprefix=%s", DATADIR);
    runner_argprintf(&runner, ":%s", primary);
    runner_add_args(&runner, secondary, "--status-get", NULL);
    runner_add_args(&runner, "--path", brick_path, NULL);

    return glusterd_query_extutil_generic(resbuf, sizeof(resbuf), &runner,
                                          sts_val, _fcbk_statustostruct);
}

static int
glusterd_gsync_get_param_file(char *prmfile, const char *param, char *primary,
                              char *secondary, char *conf_path)
{
    runner_t runner = {
        0,
    };

    runinit(&runner);
    runner_add_args(&runner, GSYNCD_PREFIX "/gsyncd", "-c", NULL);
    runner_argprintf(&runner, "%s", conf_path);
    set_gsyncd_inet6_arg(&runner);
    runner_argprintf(&runner, "--iprefix=%s", DATADIR);
    runner_argprintf(&runner, ":%s", primary);
    runner_add_args(&runner, secondary, "--config-get", NULL);
    runner_argprintf(&runner, "%s-file", param);

    return glusterd_query_extutil(prmfile, &runner);
}

static int
gsyncd_getpidfile(char *primary, char *secondary, char *pidfile,
                  char *conf_path, gf_boolean_t *is_template_in_use)
{
    char temp_conf_path[PATH_MAX] = "";
    char *working_conf_path = NULL;
    glusterd_conf_t *priv = NULL;
    int ret = -1;
    struct stat stbuf = {
        0,
    };
    xlator_t *this = THIS;
    int32_t len = 0;

    GF_ASSERT(this->private);
    GF_ASSERT(conf_path);

    priv = this->private;

    GF_VALIDATE_OR_GOTO("gsync", primary, out);
    GF_VALIDATE_OR_GOTO("gsync", secondary, out);

    len = snprintf(temp_conf_path, sizeof(temp_conf_path),
                   "%s/" GSYNC_CONF_TEMPLATE, priv->workdir);
    if ((len < 0) || (len >= sizeof(temp_conf_path))) {
        goto out;
    }

    ret = sys_lstat(conf_path, &stbuf);
    if (!ret) {
        gf_msg_debug(this->name, 0, "Using passed config template(%s).",
                     conf_path);
        working_conf_path = conf_path;
    } else {
        gf_msg(this->name, GF_LOG_WARNING, ENOENT, GD_MSG_FILE_OP_FAILED,
               "Config file (%s) missing. Looking for template "
               "config file (%s)",
               conf_path, temp_conf_path);
        ret = sys_lstat(temp_conf_path, &stbuf);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, ENOENT, GD_MSG_FILE_OP_FAILED,
                   "Template config file (%s) missing.", temp_conf_path);
            goto out;
        }
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_DEFAULT_TEMP_CONFIG,
               "Using default config template(%s).", temp_conf_path);
        working_conf_path = temp_conf_path;
        *is_template_in_use = _gf_true;
    }

fetch_data:

    ret = glusterd_gsync_get_param_file(pidfile, "pid", primary, secondary,
                                        working_conf_path);
    if ((ret == -1) || strlen(pidfile) == 0) {
        if (*is_template_in_use == _gf_false) {
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_PIDFILE_CREATE_FAILED,
                   "failed to create the pidfile string. "
                   "Trying default config template");
            working_conf_path = temp_conf_path;
            *is_template_in_use = _gf_true;
            goto fetch_data;
        } else {
            ret = -2;
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_PIDFILE_CREATE_FAILED,
                   "failed to "
                   "create the pidfile string from template "
                   "config");
            goto out;
        }
    }

    gf_msg_debug(this->name, 0, "pidfile = %s", pidfile);

    ret = open(pidfile, O_RDWR);
out:
    return ret;
}

static int
gsync_status_byfd(int fd)
{
    GF_ASSERT(fd >= -1);

    if (lockf(fd, F_TEST, 0) == -1 && (errno == EAGAIN || errno == EACCES))
        /* gsyncd keeps the pidfile locked */
        return 0;

    return -1;
}

/* status: return 0 when gsync is running
 * return -1 when not running
 */
int
gsync_status(char *primary, char *secondary, char *conf_path, int *status,
             gf_boolean_t *is_template_in_use)
{
    char pidfile[PATH_MAX] = {
        0,
    };
    int fd = -1;

    fd = gsyncd_getpidfile(primary, secondary, pidfile, conf_path,
                           is_template_in_use);
    if (fd == -2)
        return -1;

    *status = gsync_status_byfd(fd);

    sys_close(fd);

    return 0;
}

static int32_t
glusterd_gsync_volinfo_dict_set(glusterd_volinfo_t *volinfo, char *key,
                                char *value)
{
    int32_t ret = -1;
    char *gsync_status = NULL;
    xlator_t *this = THIS;

    gsync_status = gf_strdup(value);
    if (!gsync_status) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Unable to allocate memory");
        goto out;
    }

    ret = dict_set_dynstr(volinfo->dict, key, gsync_status);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to set dict");
        goto out;
    }

    ret = 0;
out:
    return ret;
}

static int
glusterd_verify_gsyncd_spawn(char *primary, char *secondary)
{
    int ret = 0;
    runner_t runner = {
        0,
    };
    xlator_t *this = THIS;

    runinit(&runner);
    runner_add_args(&runner, GSYNCD_PREFIX "/gsyncd", "--verify", "spawning",
                    NULL);
    runner_argprintf(&runner, ":%s", primary);
    runner_add_args(&runner, secondary, NULL);
    runner_redir(&runner, STDOUT_FILENO, RUN_PIPE);
    ret = runner_start(&runner);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SPAWNING_CHILD_FAILED,
               "spawning child failed");
        ret = -1;
        goto out;
    }

    if (runner_end(&runner) != 0)
        ret = -1;

out:
    gf_msg_debug(this->name, 0, "returning %d", ret);
    return ret;
}

static int
gsync_verify_config_options(dict_t *dict, char **op_errstr, char *volname)
{
    char **resopt = NULL;
    int i = 0;
    int ret = -1;
    char *subop = NULL;
    char *secondary = NULL;
    char *op_name = NULL;
    char *op_value = NULL;
    char *t = NULL;
    char errmsg[PATH_MAX] = "";
    gf_boolean_t banned = _gf_true;
    gf_boolean_t op_match = _gf_true;
    gf_boolean_t val_match = _gf_true;
    struct gsync_config_opt_vals_ *conf_vals = NULL;
    xlator_t *this = THIS;

    if (dict_get_str(dict, "subop", &subop) != 0) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
               "missing subop");
        *op_errstr = gf_strdup("Invalid config request");
        return -1;
    }

    if (dict_get_str(dict, "secondary", &secondary) != 0) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
               GEOREP " CONFIG: no secondary given");
        *op_errstr = gf_strdup("Secondary required");
        return -1;
    }

    if (strcmp(subop, "get-all") == 0)
        return 0;

    if (dict_get_str(dict, "op_name", &op_name) != 0) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
               "option name missing");
        *op_errstr = gf_strdup("Option name missing");
        return -1;
    }

    if (runcmd(GSYNCD_PREFIX "/gsyncd", "--config-check", op_name, NULL)) {
        ret = glusterd_verify_gsyncd_spawn(volname, secondary);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GSYNCD_SPAWN_FAILED,
                   "Unable to spawn "
                   "gsyncd");
            return 0;
        }

        gf_msg(this->name, GF_LOG_WARNING, EINVAL, GD_MSG_INVALID_ENTRY,
               "Invalid option %s", op_name);
        *op_errstr = gf_strdup("Invalid option");

        return -1;
    }

    if (strcmp(subop, "get") == 0)
        return 0;

    t = strtail(subop, "set");
    if (!t)
        t = strtail(subop, "del");
    if (!t || (t[0] && strcmp(t, "-glob") != 0)) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_SUBOP_NOT_FOUND,
               "unknown subop %s", subop);
        *op_errstr = gf_strdup("Invalid config request");
        return -1;
    }

    if (strtail(subop, "set") &&
        dict_get_str(dict, "op_value", &op_value) != 0) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
               "missing value for set");
        *op_errstr = gf_strdup("missing value");
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

        if (op_name[i] != '\0')
            banned = _gf_false;

        if (banned) {
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_RESERVED_OPTION,
                   "Reserved option %s", op_name);
            *op_errstr = gf_strdup("Reserved option");

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
                if (conf_vals->case_sensitive) {
                    if (!strcmp(conf_vals->values[i], op_value))
                        val_match = _gf_true;
                } else {
                    if (!strcasecmp(conf_vals->values[i], op_value))
                        val_match = _gf_true;
                }
            }

            if (!val_match) {
                ret = snprintf(errmsg, sizeof(errmsg) - 1,
                               "Invalid value(%s) for"
                               " option %s",
                               op_value, op_name);
                errmsg[ret] = '\0';

                gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
                       "%s", errmsg);
                *op_errstr = gf_strdup(errmsg);
                return -1;
            }
        }
    }
out:
    return 0;
}

static int
glusterd_get_gsync_status_mst_slv(glusterd_volinfo_t *volinfo, char *secondary,
                                  char *conf_path, dict_t *rsp_dict,
                                  char *node);

static int
_get_status_mst_slv(dict_t *dict, char *key, data_t *value, void *data)
{
    glusterd_gsync_status_temp_t *param = NULL;
    char *secondary = NULL;
    char *secondary_buf = NULL;
    char *secondary_url = NULL;
    char *secondary_vol = NULL;
    char *secondary_host = NULL;
    char *errmsg = NULL;
    char conf_path[PATH_MAX] = "";
    int ret = -1;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = THIS;
    char sec_url[VOLINFO_SECONDARY_URL_MAX] = {0};

    param = (glusterd_gsync_status_temp_t *)data;

    GF_VALIDATE_OR_GOTO(this->name, param, out);
    GF_VALIDATE_OR_GOTO(this->name, param->volinfo, out);

    priv = this->private;
    GF_VALIDATE_OR_GOTO(this->name, priv, out);

    if (snprintf(sec_url, sizeof(sec_url), "%s", value->data) >=
        sizeof(sec_url)) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SECONDARY_VOL_PARSE_FAIL,
               "Error in copying secondary: %s!", value->data);
        goto out;
    }

    ret = parse_secondary_url(sec_url, &secondary);
    if (ret == -1) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SECONDARY_VOL_PARSE_FAIL,
               "Error in parsing secondary: %s!", value->data);
        goto out;
    }

    ret = glusterd_get_secondary_info(secondary, &secondary_url,
                                      &secondary_host, &secondary_vol, &errmsg);
    if (ret) {
        if (errmsg)
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   GD_MSG_SECONDARYINFO_FETCH_ERROR,
                   "Unable to fetch secondary details. Error: %s", errmsg);
        else
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   GD_MSG_SECONDARYINFO_FETCH_ERROR,
                   "Unable to fetch secondary details.");
        ret = -1;
        goto out;
    }

    ret = snprintf(conf_path, sizeof(conf_path) - 1,
                   "%s/" GEOREP "/%s_%s_%s/gsyncd.conf", priv->workdir,
                   param->volinfo->volname, secondary_host, secondary_vol);
    conf_path[ret] = '\0';

    ret = glusterd_get_gsync_status_mst_slv(
        param->volinfo, secondary, conf_path, param->rsp_dict, param->node);
out:

    if (errmsg)
        GF_FREE(errmsg);

    if (secondary_buf)
        GF_FREE(secondary_buf);

    if (secondary_vol)
        GF_FREE(secondary_vol);

    if (secondary_url)
        GF_FREE(secondary_url);

    if (secondary_host)
        GF_FREE(secondary_host);

    gf_msg_debug(this->name, 0, "Returning %d.", ret);
    return ret;
}

static int
_get_max_gsync_secondary_num(dict_t *dict, char *key, data_t *value, void *data)
{
    int tmp_secnum = 0;
    int *secnum = (int *)data;

    sscanf(key, "secondary%d", &tmp_secnum);
    if (tmp_secnum > *secnum)
        *secnum = tmp_secnum;

    return 0;
}

static int
_get_secondary_idx_secondary_voluuid(dict_t *dict, char *key, data_t *value,
                                     void *data)
{
    char *secondary_info = NULL;
    xlator_t *this = THIS;
    struct secondary_vol_config *sec_cfg = NULL;

    int i = 0;
    int ret = -1;
    unsigned tmp_secnum = 0;

    sec_cfg = data;

    if (value)
        secondary_info = value->data;

    if (!(secondary_info) || strlen(secondary_info) == 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_SECONDARY,
               "Invalid secondary in dict");
        ret = -2;
        goto out;
    }

    /* secondary format:
     * primary_node_uuid:ssh://secondary_host::secondary_vol:secondary_voluuid
     */
    while (i++ < 5) {
        secondary_info = strchr(secondary_info, ':');
        if (secondary_info)
            secondary_info++;
        else {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SECONDARY_VOL_PARSE_FAIL,
                   "secondary_info becomes NULL!");
            ret = -2;
            goto out;
        }
    }
    if (strcmp(secondary_info, sec_cfg->secondary_voluuid) == 0) {
        gf_msg_debug(this->name, 0,
                     "Same secondary volume "
                     "already present %s",
                     sec_cfg->secondary_voluuid);
        ret = -1;

        sscanf(key, "secondary%d", &tmp_secnum);
        sec_cfg->old_slvidx = tmp_secnum;

        gf_msg_debug(this->name, 0,
                     "and "
                     "its index is: %d",
                     tmp_secnum);
        goto out;
    }

    ret = 0;
out:
    return ret;
}

static int
glusterd_remove_secondary_in_info(glusterd_volinfo_t *volinfo, char *secondary,
                                  char **op_errstr)
{
    int zero_secondary_entries = _gf_true;
    int ret = 0;
    char *secondarykey = NULL;

    GF_ASSERT(volinfo);
    GF_ASSERT(secondary);

    do {
        ret = glusterd_get_secondary(volinfo, secondary, &secondarykey);
        if (ret < 0 && zero_secondary_entries) {
            ret++;
            goto out;
        }
        zero_secondary_entries = _gf_false;
        dict_del(volinfo->gsync_secondaries, secondarykey);
    } while (ret >= 0);

    ret = glusterd_store_volinfo(volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
    if (ret) {
        *op_errstr = gf_strdup(
            "Failed to store the Volume"
            "information");
        goto out;
    }
out:
    gf_msg_debug(THIS->name, 0, "returning %d", ret);
    return ret;
}

static int
glusterd_gsync_get_uuid(char *secondary, glusterd_volinfo_t *vol, uuid_t uuid)
{
    int ret = 0;
    char *secondarykey = NULL;
    char *secondaryentry = NULL;
    char *t = NULL;

    GF_ASSERT(vol);
    GF_ASSERT(secondary);

    ret = glusterd_get_secondary(vol, secondary, &secondarykey);
    if (ret < 0) {
        /* XXX colliding cases of failure and non-extant
         * secondary... now just doing this as callers of this
         * function can make sense only of -1 and 0 as retvals;
         * getting at the proper semanticals will involve
         * fixing callers as well.
         */
        ret = -1;
        goto out;
    }

    ret = dict_get_str(vol->gsync_secondaries, secondarykey, &secondaryentry);
    GF_ASSERT(ret == 0);

    t = strchr(secondaryentry, ':');
    GF_ASSERT(t);
    *t = '\0';
    ret = gf_uuid_parse(secondaryentry, uuid);
    *t = ':';

out:
    gf_msg_debug(THIS->name, 0, "Returning %d", ret);
    return ret;
}

static int
update_secondary_voluuid(dict_t *dict, char *key, data_t *value, void *data)
{
    char *secondary = NULL;
    char *secondary_url = NULL;
    char *secondary_vol = NULL;
    char *secondary_host = NULL;
    char *errmsg = NULL;
    xlator_t *this = THIS;
    int ret = -1;
    char sec_url[VOLINFO_SECONDARY_URL_MAX] = {0};
    char secondary_voluuid[GF_UUID_BUF_SIZE] = {0};
    char *secondary_info = NULL;
    char *new_value = NULL;
    char *same_key = NULL;
    int cnt = 0;
    gf_boolean_t *voluuid_updated = NULL;

    voluuid_updated = data;
    secondary_info = value->data;
    gf_msg_debug(this->name, 0, "secondary_info: %s!", secondary_info);

    /* old secondary format:
     * primary_node_uuid:ssh://secondary_host::secondary_vol
     * New secondary format:
     * primary_node_uuid:ssh://secondary_host::secondary_vol:secondary_voluuid
     */
    while (secondary_info) {
        secondary_info = strchr(secondary_info, ':');
        if (secondary_info)
            cnt++;
        else
            break;

        secondary_info++;
    }

    gf_msg_debug(this->name, 0, "cnt: %d", cnt);
    /* check whether old secondary format and update vol uuid if old format.
     * With volume uuid, number of ':' is 5 and is 4 without.
     */
    if (cnt == 4) {
        if (snprintf(sec_url, sizeof(sec_url), "%s", value->data) >=
            sizeof(sec_url)) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SECONDARY_VOL_PARSE_FAIL,
                   "Error in copying secondary: %s!", value->data);
            goto out;
        }

        ret = parse_secondary_url(sec_url, &secondary);
        if (ret == -1) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SECONDARY_VOL_PARSE_FAIL,
                   "Error in parsing secondary: %s!", value->data);
            goto out;
        }

        ret = glusterd_get_secondary_info(secondary, &secondary_url,
                                          &secondary_host, &secondary_vol,
                                          &errmsg);
        if (ret) {
            if (errmsg)
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_SECONDARYINFO_FETCH_ERROR,
                       "Unable to fetch secondary details. Error: %s", errmsg);
            else
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_SECONDARYINFO_FETCH_ERROR,
                       "Unable to fetch secondary details.");
            ret = -1;
            goto out;
        }

        ret = glusterd_get_secondary_voluuid(secondary_host, secondary_vol,
                                             secondary_voluuid);
        if ((ret) || (strlen(secondary_voluuid) == 0)) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_REMOTE_VOL_UUID_FAIL,
                   "Unable to get remote volume uuid"
                   "secondaryhost:%s secondaryvol:%s",
                   secondary_host, secondary_vol);
            /* Avoiding failure due to remote vol uuid fetch */
            ret = 0;
            goto out;
        }
        ret = gf_asprintf(&new_value, "%s:%s", value->data, secondary_voluuid);
        ret = gf_asprintf(&same_key, "%s", key);

        /* delete old key and add new value */
        dict_del(dict, key);

        /* set new value for the same key*/
        ret = dict_set_dynstr(dict, same_key, new_value);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_REMOTE_VOL_UUID_FAIL,
                   "Error in setting dict value"
                   "new_value :%s",
                   new_value);
            goto out;
        }
        *voluuid_updated = _gf_true;
    }

    ret = 0;
out:
    if (errmsg)
        GF_FREE(errmsg);

    if (secondary_url)
        GF_FREE(secondary_url);

    if (secondary_vol)
        GF_FREE(secondary_vol);

    if (secondary_host)
        GF_FREE(secondary_host);

    if (same_key)
        GF_FREE(same_key);
    gf_msg_debug(this->name, 0, "Returning %d.", ret);
    return ret;
}

static int
glusterd_update_secondary_voluuid_secondaryinfo(glusterd_volinfo_t *volinfo)
{
    int ret = -1;
    xlator_t *this = THIS;
    gf_boolean_t voluuid_updated = _gf_false;

    GF_VALIDATE_OR_GOTO(this->name, volinfo, out);

    ret = dict_foreach(volinfo->gsync_secondaries, update_secondary_voluuid,
                       &voluuid_updated);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_REMOTE_VOL_UUID_FAIL,
               "Error in updating"
               "volinfo");
        goto out;
    }

    if (_gf_true == voluuid_updated) {
        ret = glusterd_store_volinfo(volinfo,
                                     GLUSTERD_VOLINFO_VER_AC_INCREMENT);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_STORE_FAIL,
                   "Error in storing"
                   "volinfo");
            goto out;
        }
    }

    ret = 0;
out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_check_gsync_running_local(char *primary, char *secondary,
                                   char *conf_path, gf_boolean_t *is_run)
{
    int ret = -1;
    int ret_status = 0;
    gf_boolean_t is_template_in_use = _gf_false;
    xlator_t *this = THIS;

    GF_ASSERT(primary);
    GF_ASSERT(secondary);
    GF_ASSERT(is_run);

    *is_run = _gf_false;
    ret = gsync_status(primary, secondary, conf_path, &ret_status,
                       &is_template_in_use);
    if (ret == 0 && ret_status == 0)
        *is_run = _gf_true;
    else if (ret == -1) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_VALIDATE_FAILED,
               GEOREP " validation failed");
        goto out;
    }
    ret = 0;
out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

static int
glusterd_store_secondary_in_info(glusterd_volinfo_t *volinfo, char *secondary,
                                 char *host_uuid, char *secondary_voluuid,
                                 char **op_errstr, gf_boolean_t is_force)
{
    int ret = 0;
    int maxslv = 0;
    char **linearr = NULL;
    char *value = NULL;
    char *secondarykey = NULL;
    char *secondaryentry = NULL;
    char key[32] = {
        0,
    };
    int keylen;
    char *t = NULL;
    xlator_t *this = THIS;
    struct secondary_vol_config secondary1 = {
        {0},
    };

    GF_ASSERT(volinfo);
    GF_ASSERT(secondary);
    GF_ASSERT(host_uuid);
    GF_VALIDATE_OR_GOTO(this->name, secondary_voluuid, out);

    ret = glusterd_get_secondary(volinfo, secondary, &secondarykey);
    switch (ret) {
        case -2:
            ret = -1;
            goto out;
        case -1:
            break;
        default:
            if (!is_force)
                GF_ASSERT(ret > 0);
            ret = dict_get_str(volinfo->gsync_secondaries, secondarykey,
                               &secondaryentry);
            GF_ASSERT(ret == 0);

            /* same-name + same-uuid secondary entries should have been filtered
             * out in glusterd_op_verify_gsync_start_options(), so we can
             * assert an uuid mismatch
             */
            t = strtail(secondaryentry, host_uuid);
            if (!is_force)
                GF_ASSERT(!t || *t != ':');

            if (is_force) {
                gf_msg_debug(this->name, 0,
                             GEOREP
                             " has already "
                             "been invoked for the %s (primary) and "
                             "%s (secondary). Allowing without saving "
                             "info again due to force command.",
                             volinfo->volname, secondary);
                ret = 0;
                goto out;
            }

            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVOKE_ERROR,
                   GEOREP
                   " has already been invoked for "
                   "the %s (primary) and %s (secondary) from a different "
                   "machine",
                   volinfo->volname, secondary);
            *op_errstr = gf_strdup(GEOREP
                                   " already running in "
                                   "another machine");
            ret = -1;
            goto out;
    }

    ret = glusterd_urltransform_single(secondary, "normalize", &linearr);
    if (ret == -1)
        goto out;

    ret = gf_asprintf(&value, "%s:%s:%s", host_uuid, linearr[0],
                      secondary_voluuid);

    glusterd_urltransform_free(linearr, 1);
    if (ret == -1)
        goto out;

    /* Given the secondary volume uuid, check and get any existing secondary */
    memcpy(secondary1.secondary_voluuid, secondary_voluuid,
           UUID_CANONICAL_FORM_LEN);
    ret = dict_foreach(volinfo->gsync_secondaries,
                       _get_secondary_idx_secondary_voluuid, &secondary1);

    if (ret == 0) { /* New secondary */
        dict_foreach(volinfo->gsync_secondaries, _get_max_gsync_secondary_num,
                     &maxslv);
        keylen = snprintf(key, sizeof(key), "secondary%d", maxslv + 1);

        ret = dict_set_dynstrn(volinfo->gsync_secondaries, key, keylen, value);
        if (ret) {
            GF_FREE(value);
            goto out;
        }
    } else if (ret == -1) { /* Existing secondary */
        keylen = snprintf(key, sizeof(key), "secondary%d",
                          secondary1.old_slvidx);

        gf_msg_debug(this->name, 0,
                     "Replacing key:%s with new value"
                     ":%s",
                     key, value);

        /* Add new secondary's value, with the same secondary index */
        ret = dict_set_dynstrn(volinfo->gsync_secondaries, key, keylen, value);
        if (ret) {
            GF_FREE(value);
            goto out;
        }
    } else {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_REMOTE_VOL_UUID_FAIL,
               "_get_secondary_idx_secondary_voluuid failed!");
        GF_FREE(value);
        ret = -1;
        goto out;
    }

    ret = glusterd_store_volinfo(volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
    if (ret) {
        *op_errstr = gf_strdup(
            "Failed to store the Volume "
            "information");
        goto out;
    }
    ret = 0;
out:
    return ret;
}

static int
glusterd_op_verify_gsync_start_options(glusterd_volinfo_t *volinfo,
                                       char *secondary, char *conf_path,
                                       char *statefile, char **op_errstr,
                                       gf_boolean_t is_force)
{
    int ret = -1;
    int ret_status = 0;
    gf_boolean_t is_template_in_use = _gf_false;
    char msg[2048] = {0};
    uuid_t uuid = {0};
    xlator_t *this = THIS;
    struct stat stbuf = {
        0,
    };
    char statefiledir[PATH_MAX] = {
        0,
    };
    char *statedir = NULL;

    GF_ASSERT(volinfo);
    GF_ASSERT(secondary);
    GF_ASSERT(op_errstr);
    GF_ASSERT(conf_path);
    GF_ASSERT(this->private);

    if (GLUSTERD_STATUS_STARTED != volinfo->status) {
        snprintf(msg, sizeof(msg),
                 "Volume %s needs to be started "
                 "before " GEOREP " start",
                 volinfo->volname);
        goto out;
    }

    /* check session directory as statefile may not present
     * during upgrade */
    if (snprintf(statefiledir, sizeof(statefiledir), "%s", statefile) >=
        sizeof(statefiledir)) {
        snprintf(msg, sizeof(msg), "statefiledir truncated");
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED, "%s",
               msg);
        *op_errstr = gf_strdup(msg);
        goto out;
    }
    statedir = dirname(statefiledir);

    ret = sys_lstat(statedir, &stbuf);
    if (ret) {
        snprintf(msg, sizeof(msg),
                 "Session between %s and %s has"
                 " not been created. Please create session and retry.",
                 volinfo->volname, secondary);
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
               "%s statefile: %s", msg, statefile);
        *op_errstr = gf_strdup(msg);
        goto out;
    }

    /* Check if the gsync secondary info is stored. If not
     * session has not been created */
    ret = glusterd_gsync_get_uuid(secondary, volinfo, uuid);
    if (ret) {
        snprintf(msg, sizeof(msg),
                 "Session between %s and %s has"
                 " not been created. Please create session and retry.",
                 volinfo->volname, secondary);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SESSION_CREATE_ERROR, "%s",
               msg);
        goto out;
    }

    /*Check if the gsync is already started in cmd. inited host
     * If so initiate add it into the glusterd's priv*/
    ret = gsync_status(volinfo->volname, secondary, conf_path, &ret_status,
                       &is_template_in_use);
    if (ret == 0) {
        if ((ret_status == 0) && !is_force) {
            snprintf(msg, sizeof(msg),
                     GEOREP
                     " session between"
                     " %s & %s already started",
                     volinfo->volname, secondary);
            ret = -1;
            goto out;
        }
    } else if (ret == -1) {
        snprintf(msg, sizeof(msg),
                 GEOREP
                 " start option "
                 "validation failed ");
        goto out;
    }

    if (is_template_in_use == _gf_true) {
        snprintf(msg, sizeof(msg),
                 GEOREP
                 " start "
                 "failed : pid-file entry missing "
                 "in config file.");
        ret = -1;
        goto out;
    }

    ret = glusterd_verify_gsyncd_spawn(volinfo->volname, secondary);
    if (ret && !is_force) {
        snprintf(msg, sizeof(msg), "Unable to spawn gsyncd");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GSYNCD_SPAWN_FAILED, "%s",
               msg);
    }
out:
    if (ret && (msg[0] != '\0')) {
        *op_errstr = gf_strdup(msg);
    }
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

void
glusterd_check_geo_rep_configured(glusterd_volinfo_t *volinfo,
                                  gf_boolean_t *flag)
{
    GF_ASSERT(volinfo);
    GF_ASSERT(flag);

    if (volinfo->gsync_secondaries->count)
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
is_geo_rep_active(glusterd_volinfo_t *volinfo, char *secondary, char *conf_path,
                  int *is_active)
{
    dict_t *confd = NULL;
    char *statefile = NULL;
    char *primary = NULL;
    char monitor_status[PATH_MAX] = "";
    int ret = -1;
    xlator_t *this = THIS;

    primary = volinfo->volname;

    confd = dict_new();
    if (!confd) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_CREATE_FAIL,
               "Not able to create dict.");
        goto out;
    }

    ret = glusterd_gsync_get_config(primary, secondary, conf_path, confd);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GET_CONFIG_INFO_FAILED,
               "Unable to get configuration data "
               "for %s(primary), %s(secondary)",
               primary, secondary);
        ret = -1;
        goto out;
    }

    ret = dict_get_param(confd, "state_file", &statefile);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get state_file's name "
               "for %s(primary), %s(secondary). Please check gsync "
               "config file.",
               primary, secondary);
        ret = -1;
        goto out;
    }

    ret = glusterd_gsync_read_frm_status(statefile, monitor_status,
                                         sizeof(monitor_status));
    if (ret <= 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_STAT_FILE_READ_FAILED,
               "Unable to read the status file for %s(primary), "
               "%s(secondary)",
               primary, secondary);
        snprintf(monitor_status, sizeof(monitor_status), "defunct");
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
        dict_unref(confd);
    return ret;
}

/*
 * _get_secondary_status:
 *      Called for each secondary in the volume from dict_foreach.
 *      It calls is_geo_rep_active to get the monitor status.
 *
 * RETURN VALUE:
 *      0: On successful read of state_file from is_geo_rep_active.
 *         When it is found geo-rep is already active from previous calls.
 *         When there is no secondary.
 *     -1: On error.
 */

int
_get_secondary_status(dict_t *dict, char *key, data_t *value, void *data)
{
    gsync_status_param_t *param = NULL;
    char *secondary = NULL;
    char *secondary_url = NULL;
    char *secondary_vol = NULL;
    char *secondary_host = NULL;
    char *errmsg = NULL;
    char conf_path[PATH_MAX] = "";
    int ret = -1;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = THIS;

    param = (gsync_status_param_t *)data;

    GF_ASSERT(param);
    GF_ASSERT(param->volinfo);
    if (param->is_active) {
        ret = 0;
        goto out;
    }

    priv = this->private;
    if (priv == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GLUSTERD_PRIV_NOT_FOUND,
               "priv of glusterd not present");
        goto out;
    }

    secondary = strchr(value->data, ':');
    if (!secondary) {
        ret = 0;
        goto out;
    }
    secondary++;

    ret = glusterd_get_secondary_info(secondary, &secondary_url,
                                      &secondary_host, &secondary_vol, &errmsg);
    if (ret) {
        if (errmsg)
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   GD_MSG_SECONDARYINFO_FETCH_ERROR,
                   "Unable to fetch"
                   " secondary details. Error: %s",
                   errmsg);
        else
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   GD_MSG_SECONDARYINFO_FETCH_ERROR,
                   "Unable to fetch secondary details.");
        ret = -1;
        goto out;
    }

    ret = snprintf(conf_path, sizeof(conf_path) - 1,
                   "%s/" GEOREP "/%s_%s_%s/gsyncd.conf", priv->workdir,
                   param->volinfo->volname, secondary_host, secondary_vol);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_CONF_PATH_ASSIGN_FAILED,
               "Unable to assign conf_path.");
        ret = -1;
        goto out;
    }
    conf_path[ret] = '\0';

    ret = is_geo_rep_active(param->volinfo, secondary, conf_path,
                            &param->is_active);
out:
    if (errmsg)
        GF_FREE(errmsg);

    if (secondary_vol)
        GF_FREE(secondary_vol);

    if (secondary_url)
        GF_FREE(secondary_url);
    if (secondary_host)
        GF_FREE(secondary_host);

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
glusterd_check_geo_rep_running(gsync_status_param_t *param, char **op_errstr)
{
    char msg[2048] = {
        0,
    };
    gf_boolean_t enabled = _gf_false;
    int ret = 0;

    GF_ASSERT(param);
    GF_ASSERT(param->volinfo);
    GF_ASSERT(op_errstr);

    glusterd_check_geo_rep_configured(param->volinfo, &enabled);

    if (enabled) {
        ret = dict_foreach(param->volinfo->gsync_secondaries,
                           _get_secondary_status, param);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0,
                   GD_MSG_SECONDARYINFO_FETCH_ERROR,
                   "_get_secondary_satus failed");
            snprintf(msg, sizeof(msg),
                     GEOREP
                     " Unable to"
                     " get the status of active " GEOREP
                     ""
                     " session for the volume '%s'.\n"
                     " Please check the log file for"
                     " more info.",
                     param->volinfo->volname);
            *op_errstr = gf_strdup(msg);
            ret = -1;
            goto out;
        }

        if (param->is_active) {
            snprintf(msg, sizeof(msg),
                     GEOREP
                     " sessions"
                     " are active for the volume %s.\nStop"
                     " " GEOREP
                     " sessions involved in this"
                     " volume. Use 'volume " GEOREP
                     " status' command for more info.",
                     param->volinfo->volname);
            *op_errstr = gf_strdup(msg);
            goto out;
        }
    }
out:
    return ret;
}

static int
glusterd_op_verify_gsync_running(glusterd_volinfo_t *volinfo, char *secondary,
                                 char *conf_path, char **op_errstr)
{
    int pfd = -1;
    int ret = -1;
    char msg[2048] = {0};
    char pidfile[PATH_MAX] = {
        0,
    };
    gf_boolean_t is_template_in_use = _gf_false;
    xlator_t *this = THIS;

    GF_ASSERT(volinfo);
    GF_ASSERT(secondary);
    GF_ASSERT(conf_path);
    GF_ASSERT(op_errstr);

    if (GLUSTERD_STATUS_STARTED != volinfo->status) {
        snprintf(msg, sizeof(msg),
                 "Volume %s needs to be started "
                 "before " GEOREP " start",
                 volinfo->volname);
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_GEO_REP_START_FAILED,
                "Volume is not in a started state, Volname=%s",
                volinfo->volname, NULL);

        goto out;
    }

    pfd = gsyncd_getpidfile(volinfo->volname, secondary, pidfile, conf_path,
                            &is_template_in_use);
    if (pfd == -2) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VALIDATE_FAILED,
               GEOREP " stop validation failed for %s & %s", volinfo->volname,
               secondary);
        ret = -1;
        goto out;
    }
    if (gsync_status_byfd(pfd) == -1) {
        snprintf(msg, sizeof(msg),
                 GEOREP
                 " session b/w %s & %s is "
                 "not running on this node.",
                 volinfo->volname, secondary);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SESSION_INACTIVE, "%s", msg);
        ret = -1;
        /* monitor gsyncd already dead */
        goto out;
    }

    if (is_template_in_use) {
        snprintf(msg, sizeof(msg),
                 "pid-file entry missing in "
                 "the config file(%s).",
                 conf_path);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_PIDFILE_NOT_FOUND, "%s",
               msg);
        ret = -1;
        goto out;
    }

    if (pfd < 0)
        goto out;

    ret = 0;
out:
    if (ret && (msg[0] != '\0')) {
        *op_errstr = gf_strdup(msg);
    }
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

static int
glusterd_verify_gsync_status_opts(dict_t *dict, char **op_errstr)
{
    char *secondary = NULL;
    char *volname = NULL;
    char errmsg[PATH_MAX] = {
        0,
    };
    glusterd_volinfo_t *volinfo = NULL;
    int ret = 0;
    char *conf_path = NULL;
    char *secondary_url = NULL;
    char *secondary_host = NULL;
    char *secondary_vol = NULL;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = THIS;

    priv = this->private;
    if (priv == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GLUSTERD_PRIV_NOT_FOUND,
               "priv of glusterd not present");
        *op_errstr = gf_strdup("glusterd defunct");
        goto out;
    }

    ret = dict_get_str(dict, "primary", &volname);
    if (ret < 0) {
        ret = 0;
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_VOL_NOT_FOUND,
               "volume name does not exist");
        snprintf(errmsg, sizeof(errmsg),
                 "Volume name %s does not"
                 " exist",
                 volname);
        *op_errstr = gf_strdup(errmsg);
        goto out;
    }

    ret = dict_get_str(dict, "secondary", &secondary);
    if (ret < 0) {
        ret = 0;
        goto out;
    }

    ret = glusterd_get_secondary_details_confpath(
        volinfo, dict, &secondary_url, &secondary_host, &secondary_vol,
        &conf_path, op_errstr);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SECONDARYINFO_FETCH_ERROR,
               "Unable to fetch secondary or confpath details.");
        ret = -1;
        goto out;
    }

out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_op_gsync_args_get(dict_t *dict, char **op_errstr, char **primary,
                           char **secondary, char **host_uuid)
{
    int ret = -1;
    xlator_t *this = THIS;

    GF_ASSERT(dict);
    GF_ASSERT(op_errstr);

    if (primary) {
        ret = dict_get_str(dict, "primary", primary);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
                   "primary not found");
            *op_errstr = gf_strdup("primary not found");
            goto out;
        }
    }

    if (secondary) {
        ret = dict_get_str(dict, "secondary", secondary);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
                   "secondary not found");
            *op_errstr = gf_strdup("secondary not found");
            goto out;
        }
    }

    if (host_uuid) {
        ret = dict_get_str(dict, "host-uuid", host_uuid);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
                   "host_uuid not found");
            *op_errstr = gf_strdup("host_uuid not found");
            goto out;
        }
    }

    ret = 0;
out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_op_stage_sys_exec(dict_t *dict, char **op_errstr)
{
    char errmsg[PATH_MAX] = "";
    char *command = NULL;
    char command_path[PATH_MAX] = "";
    struct stat st = {
        0,
    };
    int ret = -1;
    glusterd_conf_t *conf = NULL;
    xlator_t *this = THIS;

    conf = this->private;
    GF_ASSERT(conf);

    if (conf->op_version < 2) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_UNSUPPORTED_VERSION,
               "Op Version not supported.");
        snprintf(errmsg, sizeof(errmsg),
                 "One or more nodes do not"
                 " support the required op version.");
        *op_errstr = gf_strdup(errmsg);
        ret = -1;
        goto out;
    }

    ret = dict_get_str(dict, "command", &command);
    if (ret) {
        strcpy(errmsg, "internal error");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get command from dict");
        goto out;
    }

    /* enforce local occurrence of the command */
    if (strchr(command, '/')) {
        strcpy(errmsg, "invalid command name");
        ret = -1;
        goto out;
    }

    sprintf(command_path, GSYNCD_PREFIX "/peer_%s", command);
    /* check if it's executable */
    ret = sys_access(command_path, X_OK);
    if (!ret)
        /* check if it's a regular file */
        ret = sys_stat(command_path, &st);
    if (!ret && !S_ISREG(st.st_mode))
        ret = -1;

out:
    if (ret) {
        if (errmsg[0] == '\0') {
            if (command)
                snprintf(errmsg, sizeof(errmsg),
                         "gsync peer_%s command not found.", command);
            else
                snprintf(errmsg, sizeof(errmsg), "%s",
                         "gsync peer command was not "
                         "specified");
        }
        *op_errstr = gf_strdup(errmsg);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_PEER_CMD_ERROR, "%s",
               errmsg);
    }

    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_op_stage_copy_file(dict_t *dict, char **op_errstr)
{
    char abs_filename[PATH_MAX] = "";
    char errmsg[PATH_MAX] = "";
    char *filename = NULL;
    char *host_uuid = NULL;
    char uuid_str[64] = {0};
    int ret = -1;
    glusterd_conf_t *priv = NULL;
    struct stat stbuf = {
        0,
    };
    xlator_t *this = THIS;
    char workdir[PATH_MAX] = {
        0,
    };
    char realpath_filename[PATH_MAX] = {
        0,
    };
    char realpath_workdir[PATH_MAX] = {
        0,
    };
    int32_t len = 0;

    priv = this->private;
    if (priv == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GLUSTERD_PRIV_NOT_FOUND,
               "priv of glusterd not present");
        *op_errstr = gf_strdup("glusterd defunct");
        goto out;
    }

    if (priv->op_version < 2) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_UNSUPPORTED_VERSION,
               "Op Version not supported.");
        snprintf(errmsg, sizeof(errmsg),
                 "One or more nodes do not"
                 " support the required op version.");
        *op_errstr = gf_strdup(errmsg);
        ret = -1;
        goto out;
    }

    ret = dict_get_str(dict, "host-uuid", &host_uuid);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch host-uuid from dict.");
        goto out;
    }

    uuid_utoa_r(MY_UUID, uuid_str);
    if (!strcmp(uuid_str, host_uuid)) {
        ret = dict_get_str(dict, "source", &filename);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to fetch filename from dict.");
            *op_errstr = gf_strdup("command unsuccessful");
            goto out;
        }
        len = snprintf(abs_filename, sizeof(abs_filename), "%s/%s",
                       priv->workdir, filename);
        if ((len < 0) || (len >= sizeof(abs_filename))) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_COPY_FAIL, NULL);
            ret = -1;
            goto out;
        }

        if (!realpath(priv->workdir, realpath_workdir)) {
            len = snprintf(errmsg, sizeof(errmsg),
                           "Failed to "
                           "get realpath of %s: %s",
                           priv->workdir, strerror(errno));
            if (len < 0) {
                strcpy(errmsg, "<error>");
            }
            gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_REALPATH_GET_FAIL,
                    "Realpath=%s, Reason=%s", priv->workdir, strerror(errno),
                    NULL);
            *op_errstr = gf_strdup(errmsg);
            ret = -1;
            goto out;
        }

        if (!realpath(abs_filename, realpath_filename)) {
            snprintf(errmsg, sizeof(errmsg),
                     "Failed to get "
                     "realpath of %s: %s",
                     filename, strerror(errno));
            gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_REALPATH_GET_FAIL,
                    "Filename=%s, Reason=%s", filename, strerror(errno), NULL);
            *op_errstr = gf_strdup(errmsg);
            ret = -1;
            goto out;
        }

        /* Add Trailing slash to workdir, without slash strncmp
           will succeed for /var/lib/glusterd_bad */
        len = snprintf(workdir, sizeof(workdir), "%s/", realpath_workdir);
        if ((len < 0) || (len >= sizeof(workdir))) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_COPY_FAIL, NULL);
            ret = -1;
            goto out;
        }

        /* Protect against file copy outside $workdir */
        if (strncmp(workdir, realpath_filename, strlen(workdir))) {
            len = snprintf(errmsg, sizeof(errmsg),
                           "Source file"
                           " is outside of %s directory",
                           priv->workdir);
            if (len < 0) {
                strcpy(errmsg, "<error>");
            }
            gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_SRC_FILE_ERROR, errmsg,
                    NULL);
            *op_errstr = gf_strdup(errmsg);
            ret = -1;
            goto out;
        }

        ret = sys_lstat(abs_filename, &stbuf);
        if (ret) {
            len = snprintf(errmsg, sizeof(errmsg),
                           "Source file"
                           " does not exist in %s",
                           priv->workdir);
            if (len < 0) {
                strcpy(errmsg, "<error>");
            }
            gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_SRC_FILE_ERROR, errmsg,
                    NULL);
            *op_errstr = gf_strdup(errmsg);
            goto out;
        }

        if (!S_ISREG(stbuf.st_mode)) {
            snprintf(errmsg, sizeof(errmsg),
                     "Source file"
                     " is not a regular file.");
            gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_SRC_FILE_ERROR, errmsg,
                    NULL);
            *op_errstr = gf_strdup(errmsg);
            ret = -1;
            goto out;
        }
    }

    ret = 0;
out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_get_statefile_name(glusterd_volinfo_t *volinfo, char *secondary,
                            char *conf_path, char **statefile,
                            gf_boolean_t *is_template_in_use)
{
    char *primary = NULL;
    char *buf = NULL;
    char *working_conf_path = NULL;
    char temp_conf_path[PATH_MAX] = "";
    dict_t *confd = NULL;
    glusterd_conf_t *priv = NULL;
    int ret = -1;
    struct stat stbuf = {
        0,
    };
    xlator_t *this = THIS;
    int32_t len = 0;

    GF_ASSERT(this->private);
    GF_ASSERT(volinfo);
    GF_ASSERT(conf_path);
    GF_ASSERT(is_template_in_use);

    primary = volinfo->volname;

    confd = dict_new();
    if (!confd) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_CREATE_FAIL,
               "Unable to create new dict");
        goto out;
    }

    priv = THIS->private;

    len = snprintf(temp_conf_path, sizeof(temp_conf_path),
                   "%s/" GSYNC_CONF_TEMPLATE, priv->workdir);
    if ((len < 0) || (len >= sizeof(temp_conf_path))) {
        goto out;
    }

    ret = sys_lstat(conf_path, &stbuf);
    if (!ret) {
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_CONFIG_INFO,
               "Using passed config template(%s).", conf_path);
        working_conf_path = conf_path;
    } else {
        gf_msg(this->name, GF_LOG_WARNING, ENOENT, GD_MSG_FILE_OP_FAILED,
               "Config file (%s) missing. Looking for template config"
               " file (%s)",
               conf_path, temp_conf_path);
        ret = sys_lstat(temp_conf_path, &stbuf);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, ENOENT, GD_MSG_FILE_OP_FAILED,
                   "Template "
                   "config file (%s) missing.",
                   temp_conf_path);
            goto out;
        }
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_DEFAULT_TEMP_CONFIG,
               "Using default config template(%s).", temp_conf_path);
        working_conf_path = temp_conf_path;
        *is_template_in_use = _gf_true;
    }

fetch_data:
    ret = glusterd_gsync_get_config(primary, secondary, working_conf_path,
                                    confd);
    if (ret) {
        if (*is_template_in_use == _gf_false) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GET_CONFIG_INFO_FAILED,
                   "Unable to get configuration data "
                   "for %s(primary), %s(secondary). "
                   "Trying template config.",
                   primary, secondary);
            working_conf_path = temp_conf_path;
            *is_template_in_use = _gf_true;
            goto fetch_data;
        } else {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GET_CONFIG_INFO_FAILED,
                   "Unable to get configuration data "
                   "for %s(primary), %s(secondary) from "
                   "template config",
                   primary, secondary);
            goto out;
        }
    }

    ret = dict_get_param(confd, "state_file", &buf);
    if (ret) {
        if (*is_template_in_use == _gf_false) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to get state_file's name. "
                   "Trying template config.");
            working_conf_path = temp_conf_path;
            *is_template_in_use = _gf_true;
            goto fetch_data;
        } else {
            gf_msg(this->name, GF_LOG_ERROR, 0,
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
        dict_unref(confd);

    gf_msg_debug(this->name, 0, "Returning %d ", ret);
    return ret;
}

int
glusterd_create_status_file(char *primary, char *secondary,
                            char *secondary_host, char *secondary_vol,
                            char *status)
{
    int ret = -1;
    runner_t runner = {
        0,
    };
    glusterd_conf_t *priv = NULL;
    xlator_t *this = THIS;

    priv = this->private;
    if (priv == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GLUSTERD_PRIV_NOT_FOUND,
               "priv of glusterd not present");
        goto out;
    }

    if (!status) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_STATUS_NULL, "Status Empty");
        goto out;
    }
    gf_msg_debug(this->name, 0, "secondary = %s", secondary);

    runinit(&runner);
    runner_add_args(&runner, GSYNCD_PREFIX "/gsyncd", "--create", status, "-c",
                    NULL);
    runner_argprintf(&runner, "%s/" GEOREP "/%s_%s_%s/gsyncd.conf",
                     priv->workdir, primary, secondary_host, secondary_vol);
    runner_argprintf(&runner, "--iprefix=%s", DATADIR);
    runner_argprintf(&runner, ":%s", primary);
    runner_add_args(&runner, secondary, NULL);
    synclock_unlock(&priv->big_lock);
    ret = runner_run(&runner);
    synclock_lock(&priv->big_lock);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_STATUSFILE_CREATE_FAILED,
               "Creating status file failed.");
        ret = -1;
        goto out;
    }

    ret = 0;
out:
    gf_msg_debug(this->name, 0, "returning %d", ret);
    return ret;
}

static int
glusterd_verify_secondary(char *volname, char *secondary_url,
                          char *secondary_vol, int ssh_port, char **op_errstr,
                          gf_boolean_t *is_force_blocker)
{
    int32_t ret = -1;
    runner_t runner = {
        0,
    };
    char log_file_path[PATH_MAX] = "";
    char buf[PATH_MAX] = "";
    char *tmp = NULL;
    char *secondary_url_buf = NULL;
    char *save_ptr = NULL;
    char *secondary_user = NULL;
    char *secondary_ip = NULL;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = THIS;
    char *af = NULL;

    priv = this->private;
    GF_ASSERT(priv);
    GF_ASSERT(volname);
    GF_ASSERT(secondary_url);
    GF_ASSERT(secondary_vol);

    /* Fetch the secondary_user and secondary_ip from the secondary_url.
     * If the secondary_user is not present. Use "root"
     */
    if (strstr(secondary_url, "@")) {
        secondary_url_buf = gf_strdup(secondary_url);
        if (!secondary_url_buf) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_STRDUP_FAILED,
                    "Secondary_url=%s", secondary_url, NULL);
            goto out;
        }

        secondary_user = strtok_r(secondary_url_buf, "@", &save_ptr);
        secondary_ip = strtok_r(NULL, "@", &save_ptr);
    } else {
        secondary_user = "root";
        secondary_ip = secondary_url;
    }

    if (!secondary_user || !secondary_ip) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SECONDARY_URL_INVALID,
               "Invalid secondary url.");
        goto out;
    }

    snprintf(log_file_path, sizeof(log_file_path), "%s/create_verify_log",
             priv->logdir);

    runinit(&runner);
    runner_add_args(&runner, GSYNCD_PREFIX "/gverify.sh", NULL);
    runner_argprintf(&runner, "%s", volname);
    runner_argprintf(&runner, "%s", secondary_user);
    runner_argprintf(&runner, "%s", secondary_ip);
    runner_argprintf(&runner, "%s", secondary_vol);
    runner_argprintf(&runner, "%d", ssh_port);
    runner_argprintf(&runner, "%s", log_file_path);
    ret = dict_get_str(this->options, "transport.address-family", &af);
    if (ret)
        af = "-";

    runner_argprintf(&runner, "%s", af);

    gf_msg_debug(this->name, 0, "gverify Args = %s %s %s %s %s %s %s %s",
                 runner.argv[0], runner.argv[1], runner.argv[2], runner.argv[3],
                 runner.argv[4], runner.argv[5], runner.argv[6],
                 runner.argv[7]);
    runner_redir(&runner, STDOUT_FILENO, RUN_PIPE);
    synclock_unlock(&priv->big_lock);
    ret = runner_run(&runner);
    synclock_lock(&priv->big_lock);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INVALID_SECONDARY,
               "Not a valid secondary");
        ret = glusterd_gsync_read_frm_status(log_file_path, buf, sizeof(buf));
        if (ret <= 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_READ_ERROR,
                   "Unable to read from %s", log_file_path);
            goto out;
        }

        /* Tokenize the error message from gverify.sh to figure out
         * if the error is a force blocker or not. */
        tmp = strtok_r(buf, "|", &save_ptr);
        if (!tmp) {
            ret = -1;
            goto out;
        }
        if (!strcmp(tmp, "FORCE_BLOCKER"))
            *is_force_blocker = 1;
        else {
            /* No FORCE_BLOCKER flag present so all that is
             * present is the error message. */
            *is_force_blocker = 0;
            *op_errstr = gf_strdup(tmp);
            ret = -1;
            goto out;
        }

        /* Copy rest of the error message to op_errstr */
        tmp = strtok_r(NULL, "|", &save_ptr);
        if (tmp)
            *op_errstr = gf_strdup(tmp);
        ret = -1;
        goto out;
    }
    ret = 0;
out:
    GF_FREE(secondary_url_buf);
    sys_unlink(log_file_path);
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

/** @secondary_ip remains unmodified */
int
glusterd_geo_rep_parse_secondary(char *secondary_url, char **hostname,
                                 char **op_errstr)
{
    int ret = -1;
    char *tmp = NULL;
    char *save_ptr = NULL;
    char *host = NULL;
    char errmsg[PATH_MAX] = "";
    char *saved_url = NULL;
    xlator_t *this = THIS;

    GF_ASSERT(secondary_url);
    GF_ASSERT(*secondary_url);

    saved_url = gf_strdup(secondary_url);
    if (!saved_url)
        goto out;

    /* Checking if hostname has user specified */
    host = strstr(saved_url, "@");
    if (!host) { /* no user specified */
        if (hostname) {
            *hostname = gf_strdup(saved_url);
            if (!*hostname)
                goto out;
        }

        ret = 0;
        goto out;
    } else {
        /* Moving the host past the '@' and checking if the
         * actual hostname also has '@' */
        host++;
        if (strstr(host, "@")) {
            gf_msg_debug(this->name, 0, "host = %s", host);
            ret = snprintf(errmsg, sizeof(errmsg) - 1, "Invalid Hostname (%s).",
                           host);
            errmsg[ret] = '\0';
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY, "%s",
                   errmsg);
            ret = -1;
            if (op_errstr)
                *op_errstr = gf_strdup(errmsg);
            goto out;
        }

        ret = -1;

        /**
         * preliminary check for valid secondary format.
         */
        tmp = strtok_r(saved_url, "@", &save_ptr);
        tmp = strtok_r(NULL, "@", &save_ptr);
        if (!tmp)
            goto out;
        if (hostname) {
            *hostname = gf_strdup(tmp);
            if (!*hostname)
                goto out;
        }
    }

    ret = 0;
out:
    GF_FREE(saved_url);
    if (ret)
        if (hostname)
            GF_FREE(*hostname);
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

/* Return -1 only if there is a match in volume uuid */
static int
get_secondaryhost_from_voluuid(dict_t *dict, char *key, data_t *value,
                               void *data)
{
    char *secondary_info = NULL;
    char *tmp = NULL;
    char *secondary_host = NULL;
    xlator_t *this = THIS;
    struct secondary_vol_config *secondary_vol = NULL;
    int i = 0;
    int ret = -1;

    secondary_vol = data;
    secondary_info = value->data;

    gf_msg_debug(this->name, 0, "secondary_info:%s !", secondary_info);

    if (!(secondary_info) || strlen(secondary_info) == 0) {
        /* no secondaries present, peace  */
        ret = 0;
        goto out;
    }

    /* secondary format:
     * primary_node_uuid:ssh://secondary_host::secondary_vol:secondary_voluuid
     */
    while (i++ < 5) {
        secondary_info = strchr(secondary_info, ':');
        if (secondary_info)
            secondary_info++;
        else
            break;
    }

    if (!(secondary_info) || strlen(secondary_info) == 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SECONDARY_VOL_PARSE_FAIL,
               "secondary_info format is wrong!");
        ret = -2;
        goto out;
    } else {
        if (strcmp(secondary_info, secondary_vol->secondary_voluuid) == 0) {
            ret = -1;

            /* get corresponding secondary host for reference*/
            secondary_host = value->data;
            secondary_host = strstr(secondary_host, "://");
            if (secondary_host) {
                secondary_host += 3;
            } else {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_SECONDARY_VOL_PARSE_FAIL,
                       "Invalid secondary_host format!");
                ret = -2;
                goto out;
            }
            /* To go past username in non-root geo-rep session */
            tmp = strchr(secondary_host, '@');
            if (tmp) {
                if ((tmp - secondary_host) >= LOGIN_NAME_MAX) {
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_SECONDARY_VOL_PARSE_FAIL,
                           "Invalid secondary user length in %s",
                           secondary_host);
                    ret = -2;
                    goto out;
                }
                strncpy(secondary_vol->old_secuser, secondary_host,
                        (tmp - secondary_host));
                secondary_vol->old_secuser[(tmp - secondary_host) + 1] = '\0';
                secondary_host = tmp + 1;
            } else
                strcpy(secondary_vol->old_secuser, "root");

            tmp = strchr(secondary_host, ':');
            if (!tmp) {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_SECONDARY_VOL_PARSE_FAIL,
                       "Invalid secondary_host!");
                ret = -2;
                goto out;
            }

            strncpy(secondary_vol->old_sechost, secondary_host,
                    (tmp - secondary_host));
            secondary_vol->old_sechost[(tmp - secondary_host) + 1] = '\0';

            goto out;
        }
    }

    ret = 0;
out:
    return ret;
}

/* Given secondary host and secondary volume, check whether secondary volume
 * uuid already present. If secondary volume uuid is present, get corresponding
 * secondary host for reference */
static int
glusterd_get_secondaryhost_from_voluuid(glusterd_volinfo_t *volinfo,
                                        char *secondary_host,
                                        char *secondary_vol,
                                        struct secondary_vol_config *secondary1)
{
    int ret = -1;

    GF_VALIDATE_OR_GOTO(THIS->name, volinfo, out);

    ret = dict_foreach(volinfo->gsync_secondaries,
                       get_secondaryhost_from_voluuid, secondary1);
out:
    return ret;
}

int
glusterd_op_stage_gsync_create(dict_t *dict, char **op_errstr)
{
    char *down_peerstr = NULL;
    char *secondary = NULL;
    char *volname = NULL;
    char *host_uuid = NULL;
    char *statefile = NULL;
    char *secondary_url = NULL;
    char *secondary_host = NULL;
    char *secondary_vol = NULL;
    char *conf_path = NULL;
    char errmsg[PATH_MAX] = "";
    char common_pem_file[PATH_MAX] = "";
    char hook_script[PATH_MAX] = "";
    char uuid_str[64] = "";
    int ret = -1;
    int is_pem_push = -1;
    int ssh_port = 22;
    gf_boolean_t is_force = -1;
    gf_boolean_t is_no_verify = -1;
    gf_boolean_t is_force_blocker = -1;
    gf_boolean_t is_template_in_use = _gf_false;
    glusterd_conf_t *conf = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    struct stat stbuf = {
        0,
    };
    xlator_t *this = THIS;
    struct secondary_vol_config secondary1 = {
        {0},
    };
    char old_secondary_url[SECONDARY_URL_INFO_MAX] = {0};
    char old_confpath[PATH_MAX] = {0};
    gf_boolean_t is_running = _gf_false;
    char *statedir = NULL;
    char statefiledir[PATH_MAX] = {
        0,
    };
    gf_boolean_t is_different_secondaryhost = _gf_false;
    gf_boolean_t is_different_username = _gf_false;
    char *secondary_user = NULL;
    char *save_ptr = NULL;
    char *secondary_url_buf = NULL;
    int32_t len = 0;

    conf = this->private;
    GF_ASSERT(conf);

    ret = glusterd_op_gsync_args_get(dict, op_errstr, &volname, &secondary,
                                     &host_uuid);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_ARG_FETCH_ERROR,
               "Unable to fetch arguments");
        gf_msg_debug(this->name, 0, "Returning %d", ret);
        return -1;
    }

    if (conf->op_version < 2) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_UNSUPPORTED_VERSION,
               "Op Version not supported.");
        snprintf(errmsg, sizeof(errmsg),
                 "One or more nodes do not"
                 " support the required op version.");
        *op_errstr = gf_strdup(errmsg);
        ret = -1;
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_VOL_NOT_FOUND,
               "volume name does not exist");
        snprintf(errmsg, sizeof(errmsg),
                 "Volume name %s does not"
                 " exist",
                 volname);
        goto out;
    }

    ret = glusterd_get_secondary_details_confpath(
        volinfo, dict, &secondary_url, &secondary_host, &secondary_vol,
        &conf_path, op_errstr);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SECONDARYINFO_FETCH_ERROR,
               "Unable to fetch secondary or confpath details.");
        ret = -1;
        goto out;
    }

    is_force = dict_get_str_boolean(dict, "force", _gf_false);

    uuid_utoa_r(MY_UUID, uuid_str);
    if (!strcmp(uuid_str, host_uuid)) {
        ret = glusterd_are_vol_all_peers_up(volinfo, &conf->peers,
                                            &down_peerstr);
        if ((ret == _gf_false) && !is_force) {
            snprintf(errmsg, sizeof(errmsg),
                     "Peer %s,"
                     " which is a part of %s volume, is"
                     " down. Please bring up the peer and"
                     " retry.",
                     down_peerstr, volinfo->volname);
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_PEER_DISCONNECTED, "%s",
                   errmsg);
            *op_errstr = gf_strdup(errmsg);
            GF_FREE(down_peerstr);
            down_peerstr = NULL;
            gf_msg_debug(this->name, 0, "Returning %d", ret);
            return -1;
        } else if (ret == _gf_false) {
            gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_PEER_DISCONNECTED,
                   "Peer %s, which is a part of %s volume, is"
                   " down. Force creating geo-rep session."
                   " On bringing up the peer, re-run"
                   " \"gluster system:: execute"
                   " gsec_create\" and \"gluster volume"
                   " geo-replication %s %s create push-pem"
                   " force\"",
                   down_peerstr, volinfo->volname, volinfo->volname, secondary);
            GF_FREE(down_peerstr);
            down_peerstr = NULL;
        }

        ret = dict_get_int32(dict, "ssh_port", &ssh_port);
        if (ret < 0 && ret != -ENOENT) {
            snprintf(errmsg, sizeof(errmsg),
                     "Fetching ssh_port failed while "
                     "handling " GEOREP " options");
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
                   errmsg);
            goto out;
        }

        is_no_verify = dict_get_str_boolean(dict, "no_verify", _gf_false);

        if (!is_no_verify) {
            /* Checking if secondary host is pingable, has proper passwordless
             * ssh login setup, secondary volume is created, secondary vol is
             * empty, and if it has enough memory and bypass in case of force if
             * the error is not a force blocker */
            ret = glusterd_verify_secondary(volname, secondary_url,
                                            secondary_vol, ssh_port, op_errstr,
                                            &is_force_blocker);
            if (ret) {
                if (is_force && !is_force_blocker) {
                    gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_INVALID_SECONDARY,
                           "%s is not a valid secondary "
                           "volume. Error: %s. Force "
                           "creating geo-rep"
                           " session.",
                           secondary, *op_errstr);
                } else {
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_INVALID_SECONDARY,
                           "%s is not a valid secondary "
                           "volume. Error: %s",
                           secondary, *op_errstr);
                    ret = -1;

                    goto out;
                }
            }
        }

        ret = dict_get_int32(dict, "push_pem", &is_pem_push);
        if (!ret && is_pem_push) {
            ret = snprintf(common_pem_file, sizeof(common_pem_file),
                           "%s" GLUSTERD_COMMON_PEM_PUB_FILE, conf->workdir);
            if ((ret < 0) || (ret >= sizeof(common_pem_file))) {
                ret = -1;
                goto out;
            }

            ret = snprintf(hook_script, sizeof(hook_script),
                           "%s" GLUSTERD_CREATE_HOOK_SCRIPT, conf->workdir);
            if ((ret < 0) || (ret >= sizeof(hook_script))) {
                ret = -1;
                goto out;
            }

            ret = sys_lstat(common_pem_file, &stbuf);
            if (ret) {
                len = snprintf(errmsg, sizeof(errmsg),
                               "%s"
                               " required for push-pem is"
                               " not present. Please run"
                               " \"gluster system:: execute"
                               " gsec_create\"",
                               common_pem_file);
                if (len < 0) {
                    strcpy(errmsg, "<error>");
                }
                gf_msg(this->name, GF_LOG_ERROR, ENOENT, GD_MSG_FILE_OP_FAILED,
                       "%s", errmsg);
                *op_errstr = gf_strdup(errmsg);
                ret = -1;
                goto out;
            }

            ret = sys_lstat(hook_script, &stbuf);
            if (ret) {
                len = snprintf(errmsg, sizeof(errmsg),
                               "The hook-script (%s) "
                               "required for push-pem is not "
                               "present. Please install the "
                               "hook-script and retry",
                               hook_script);
                if (len < 0) {
                    strcpy(errmsg, "<error>");
                }
                gf_msg(this->name, GF_LOG_ERROR, ENOENT, GD_MSG_FILE_OP_FAILED,
                       "%s", errmsg);
                *op_errstr = gf_strdup(errmsg);
                ret = -1;
                goto out;
            }

            if (!S_ISREG(stbuf.st_mode)) {
                len = snprintf(errmsg, sizeof(errmsg),
                               "%s"
                               " required for push-pem is"
                               " not a regular file. Please"
                               " run \"gluster system:: "
                               "execute gsec_create\"",
                               common_pem_file);
                if (len < 0) {
                    strcpy(errmsg, "<error>");
                }
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_REG_FILE_MISSING,
                       "%s", errmsg);
                ret = -1;
                goto out;
            }
        }
    }

    ret = glusterd_get_statefile_name(volinfo, secondary, conf_path, &statefile,
                                      &is_template_in_use);
    if (ret) {
        if (!strstr(secondary, "::"))
            snprintf(errmsg, sizeof(errmsg), "%s is not a valid secondary url.",
                     secondary);
        else
            snprintf(errmsg, sizeof(errmsg),
                     "Please check gsync "
                     "config file. Unable to get statefile's name");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_STATEFILE_NAME_NOT_FOUND,
               "%s", errmsg);
        ret = -1;
        goto out;
    }

    ret = dict_set_str(dict, "statefile", statefile);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to store statefile path");
        goto out;
    }

    if (snprintf(statefiledir, sizeof(statefiledir), "%s", statefile) >=
        sizeof(statefiledir)) {
        snprintf(errmsg, sizeof(errmsg), "Failed copying statefiledir");
        goto out;
    }
    statedir = dirname(statefiledir);

    ret = sys_lstat(statedir, &stbuf);
    if (!ret && !is_force) {
        snprintf(errmsg, sizeof(errmsg),
                 "Session between %s"
                 " and %s is already created.",
                 volinfo->volname, secondary);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SESSION_ALREADY_EXIST, "%s",
               errmsg);
        ret = -1;
        goto out;
    } else if (!ret)
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_FORCE_CREATE_SESSION,
               "Session between %s and %s is already created. Force"
               " creating again.",
               volinfo->volname, secondary);

    ret = glusterd_get_secondary_voluuid(secondary_host, secondary_vol,
                                         secondary1.secondary_voluuid);
    if ((ret) || (strlen(secondary1.secondary_voluuid) == 0)) {
        snprintf(errmsg, sizeof(errmsg), "Unable to get remote volume uuid.");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_REMOTE_VOL_UUID_FAIL, "%s",
               errmsg);
        ret = -1;
        goto out;
    }

    ret = dict_set_dynstr_with_alloc(dict, "secondary_voluuid",
                                     secondary1.secondary_voluuid);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to set secondary volume uuid in the dict");
        goto out;
    }

    /* Check whether session is already created using secondary volume uuid */
    ret = glusterd_get_secondaryhost_from_voluuid(volinfo, secondary_host,
                                                  secondary_vol, &secondary1);
    if (ret == -1) {
        if (!is_force) {
            snprintf(errmsg, sizeof(errmsg),
                     "Session between %s"
                     " and %s:%s is already created! Cannot create "
                     "with new secondary:%s again!",
                     volinfo->volname, secondary1.old_sechost, secondary_vol,
                     secondary_host);
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_FORCE_CREATE_SESSION,
                   "Session between"
                   " %s and %s:%s is already created! "
                   "Cannot create with new secondary:%s again!",
                   volinfo->volname, secondary1.old_sechost, secondary_vol,
                   secondary_host);
            goto out;
        }

        /* There is a remote possibility that secondary_host can be NULL when
           control reaches here. Add a check so we wouldn't crash in next
           line */
        if (!secondary_host)
            goto out;

        /* Now, check whether session is already started.If so, warn!*/
        is_different_secondaryhost = (strcmp(secondary_host,
                                             secondary1.old_sechost) != 0)
                                         ? _gf_true
                                         : _gf_false;

        if (strstr(secondary_url, "@")) {
            secondary_url_buf = gf_strdup(secondary_url);
            if (!secondary_url_buf) {
                gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                       "Unable to allocate memory");
                ret = -1;
                goto out;
            }
            secondary_user = strtok_r(secondary_url_buf, "@", &save_ptr);
        } else
            secondary_user = "root";
        is_different_username = (strcmp(secondary_user,
                                        secondary1.old_secuser) != 0)
                                    ? _gf_true
                                    : _gf_false;

        /* Do the check, only if different secondary host/secondary user */
        if (is_different_secondaryhost || is_different_username) {
            len = snprintf(old_confpath, sizeof(old_confpath),
                           "%s/" GEOREP "/%s_%s_%s/gsyncd.conf", conf->workdir,
                           volinfo->volname, secondary1.old_sechost,
                           secondary_vol);
            if ((len < 0) || (len >= sizeof(old_confpath))) {
                ret = -1;
                goto out;
            }

            /* construct old secondary url with (old) secondary host */
            len = snprintf(old_secondary_url, sizeof(old_secondary_url),
                           "%s::%s", secondary1.old_sechost, secondary_vol);
            if ((len < 0) || (len >= sizeof(old_secondary_url))) {
                ret = -1;
                goto out;
            }

            ret = glusterd_check_gsync_running_local(
                volinfo->volname, old_secondary_url, old_confpath, &is_running);
            if (_gf_true == is_running) {
                (void)snprintf(errmsg, sizeof(errmsg),
                               "Geo"
                               "-replication session between %s and %s"
                               " is still active. Please stop the "
                               "session and retry.",
                               volinfo->volname, old_secondary_url);
                ret = -1;
                goto out;
            }
        }

        ret = dict_set_dynstr_with_alloc(dict, "old_secondaryhost",
                                         secondary1.old_sechost);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Unable to set old_secondaryhost in the dict");
            goto out;
        }

        ret = dict_set_int32(dict, "existing_session", _gf_true);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Unable to set existing_session in the dict");
            goto out;
        }
    } else if (ret == -2) {
        snprintf(errmsg, sizeof(errmsg),
                 "get_secondaryhost_from_voluuid"
                 " failed for %s::%s. Please check the glusterd logs.",
                 secondary_host, secondary_vol);
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_FORCE_CREATE_SESSION,
               "get_secondaryhost_from_voluuid failed %s %s!!", secondary_host,
               secondary_vol);
        goto out;
    }

    ret = glusterd_verify_gsyncd_spawn(volinfo->volname, secondary);
    if (ret) {
        snprintf(errmsg, sizeof(errmsg), "Unable to spawn gsyncd.");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GSYNCD_SPAWN_FAILED, "%s",
               errmsg);
        goto out;
    }

    ret = 0;
out:

    if (ret && errmsg[0] != '\0')
        *op_errstr = gf_strdup(errmsg);

    if (secondary_url_buf)
        GF_FREE(secondary_url_buf);

    return ret;
}

/* pre-condition check for geo-rep pause/resume.
 * Return: 0 on success
 *        -1 on any check failed.
 */
static int
gd_pause_resume_validation(int type, glusterd_volinfo_t *volinfo,
                           char *secondary, char *statefile, char **op_errstr)
{
    int ret = 0;
    char errmsg[PATH_MAX] = {
        0,
    };
    char monitor_status[NAME_MAX] = {
        0,
    };

    GF_ASSERT(volinfo);
    GF_ASSERT(secondary);
    GF_ASSERT(statefile);
    GF_ASSERT(op_errstr);

    ret = glusterd_gsync_read_frm_status(statefile, monitor_status,
                                         sizeof(monitor_status));
    if (ret <= 0) {
        snprintf(errmsg, sizeof(errmsg),
                 "Pause check Failed:"
                 " Geo-rep session is not setup");
        ret = -1;
        goto out;
    }

    if (type == GF_GSYNC_OPTION_TYPE_PAUSE &&
        strstr(monitor_status, "Paused")) {
        snprintf(errmsg, sizeof(errmsg),
                 "Geo-replication"
                 " session between %s and %s already Paused.",
                 volinfo->volname, secondary);
        ret = -1;
        goto out;
    }
    if (type == GF_GSYNC_OPTION_TYPE_RESUME &&
        !strstr(monitor_status, "Paused")) {
        snprintf(errmsg, sizeof(errmsg),
                 "Geo-replication"
                 " session between %s and %s is not Paused.",
                 volinfo->volname, secondary);
        ret = -1;
        goto out;
    }
    ret = 0;
out:
    if (ret && (errmsg[0] != '\0')) {
        *op_errstr = gf_strdup(errmsg);
    }
    return ret;
}

int
glusterd_op_stage_gsync_set(dict_t *dict, char **op_errstr)
{
    int ret = 0;
    int type = 0;
    char *volname = NULL;
    char *secondary = NULL;
    char *secondary_url = NULL;
    char *secondary_host = NULL;
    char *secondary_vol = NULL;
    char *down_peerstr = NULL;
    char *statefile = NULL;
    char statefiledir[PATH_MAX] = {
        0,
    };
    char *statedir = NULL;
    char *path_list = NULL;
    char *conf_path = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    char errmsg[PATH_MAX] = {
        0,
    };
    dict_t *ctx = NULL;
    gf_boolean_t is_force = 0;
    gf_boolean_t is_running = _gf_false;
    gf_boolean_t is_template_in_use = _gf_false;
    uuid_t uuid = {0};
    char uuid_str[64] = {0};
    char *host_uuid = NULL;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    struct stat stbuf = {
        0,
    };

    conf = this->private;
    GF_ASSERT(conf);

    ret = dict_get_int32(dict, "type", &type);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
               "command type not found");
        *op_errstr = gf_strdup("command unsuccessful");
        goto out;
    }

    if (type == GF_GSYNC_OPTION_TYPE_STATUS) {
        ret = glusterd_verify_gsync_status_opts(dict, op_errstr);
        goto out;
    }

    ret = glusterd_op_gsync_args_get(dict, op_errstr, &volname, &secondary,
                                     &host_uuid);
    if (ret)
        goto out;

    uuid_utoa_r(MY_UUID, uuid_str);

    if (conf->op_version < 2) {
        snprintf(errmsg, sizeof(errmsg),
                 "One or more nodes do not"
                 " support the required op version.");
        ret = -1;
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(errmsg, sizeof(errmsg),
                 "Volume name %s does not"
                 " exist",
                 volname);
        goto out;
    }

    ret = glusterd_get_secondary_details_confpath(
        volinfo, dict, &secondary_url, &secondary_host, &secondary_vol,
        &conf_path, op_errstr);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SECONDARYINFO_FETCH_ERROR,
               "Unable to fetch secondary or confpath details.");
        ret = -1;
        goto out;
    }

    is_force = dict_get_str_boolean(dict, "force", _gf_false);

    ret = glusterd_get_statefile_name(volinfo, secondary, conf_path, &statefile,
                                      &is_template_in_use);
    if (ret) {
        if (!strstr(secondary, "::")) {
            snprintf(errmsg, sizeof(errmsg), "%s is not a valid secondary url.",
                     secondary);
            ret = -1;
            goto out;
        } else {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SECONDARY_URL_INVALID,
                   "state_file entry missing in config file (%s)", conf_path);

            if ((type == GF_GSYNC_OPTION_TYPE_STOP) && is_force) {
                gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_STOP_FORCE,
                       "Allowing stop "
                       "force to bypass missing statefile "
                       "entry in config file (%s), and "
                       "template file",
                       conf_path);
                ret = 0;
            } else
                goto out;
        }
    } else {
        ret = dict_set_str(dict, "statefile", statefile);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
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
        if (snprintf(statefiledir, sizeof(statefiledir), "%s", statefile) >=
            sizeof(statefiledir)) {
            snprintf(errmsg, sizeof(errmsg), "Failed copying statefiledir");
            ret = -1;
            goto out;
        }
        statedir = dirname(statefiledir);

        ret = sys_lstat(statedir, &stbuf);
        if (ret) {
            snprintf(errmsg, sizeof(errmsg),
                     "Geo-replication"
                     " session between %s and %s does not exist.",
                     volinfo->volname, secondary);
            gf_msg(this->name, GF_LOG_ERROR, ENOENT, GD_MSG_FILE_OP_FAILED,
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
        if (!strcmp(uuid_str, host_uuid)) {
            ret = glusterd_are_vol_all_peers_up(volinfo, &conf->peers,
                                                &down_peerstr);
            if (ret == _gf_false) {
                snprintf(errmsg, sizeof(errmsg),
                         "Peer %s,"
                         " which is a part of %s volume, is"
                         " down. Please bring up the peer and"
                         " retry.",
                         down_peerstr, volinfo->volname);
                ret = -1;
                GF_FREE(down_peerstr);
                down_peerstr = NULL;
                goto out;
            }
        }
    }

    switch (type) {
        case GF_GSYNC_OPTION_TYPE_START:
            if (is_template_in_use) {
                snprintf(errmsg, sizeof(errmsg),
                         "state-file entry "
                         "missing in the config file(%s).",
                         conf_path);
                ret = -1;
                goto out;
            }

            ret = glusterd_op_verify_gsync_start_options(
                volinfo, secondary, conf_path, statefile, op_errstr, is_force);
            if (ret)
                goto out;
            ctx = glusterd_op_get_ctx();
            if (ctx) {
                /* gsyncd does a fuse mount to start
                 * the geo-rep session */
                if (!glusterd_is_fuse_available()) {
                    gf_msg("glusterd", GF_LOG_ERROR, errno,
                           GD_MSG_GEO_REP_START_FAILED,
                           "Unable "
                           "to open /dev/fuse (%s), "
                           "geo-replication start failed",
                           strerror(errno));
                    snprintf(errmsg, sizeof(errmsg), "fuse unavailable");
                    ret = -1;
                    goto out;
                }
            }
            break;

        case GF_GSYNC_OPTION_TYPE_STOP:
            if (!is_force) {
                if (is_template_in_use) {
                    snprintf(errmsg, sizeof(errmsg),
                             "state-file entry missing in "
                             "the config file(%s).",
                             conf_path);
                    ret = -1;
                    goto out;
                }

                ret = glusterd_op_verify_gsync_running(volinfo, secondary,
                                                       conf_path, op_errstr);
                if (ret) {
                    ret = glusterd_get_local_brickpaths(volinfo, &path_list);
                    if (!path_list && ret == -1)
                        goto out;
                }

                /* Check for geo-rep session is active or not for
                 * configured user.*/
                ret = glusterd_gsync_get_uuid(secondary, volinfo, uuid);
                if (ret) {
                    snprintf(errmsg, sizeof(errmsg),
                             "Geo-replication session between %s "
                             "and %s does not exist.",
                             volinfo->volname, secondary);
                    ret = -1;
                    goto out;
                }
            }
            break;

        case GF_GSYNC_OPTION_TYPE_PAUSE:
        case GF_GSYNC_OPTION_TYPE_RESUME:
            if (is_template_in_use) {
                snprintf(errmsg, sizeof(errmsg),
                         "state-file entry missing in "
                         "the config file(%s).",
                         conf_path);
                ret = -1;
                goto out;
            }

            ret = glusterd_op_verify_gsync_running(volinfo, secondary,
                                                   conf_path, op_errstr);
            if (ret) {
                ret = glusterd_get_local_brickpaths(volinfo, &path_list);
                if (!path_list && ret == -1)
                    goto out;
            }

            /* Check for geo-rep session is active or not
             * for configured user.*/
            ret = glusterd_gsync_get_uuid(secondary, volinfo, uuid);
            if (ret) {
                snprintf(errmsg, sizeof(errmsg),
                         "Geo-replication"
                         " session between %s and %s does not exist.",
                         volinfo->volname, secondary);
                ret = -1;
                goto out;
            }

            if (!is_force) {
                ret = gd_pause_resume_validation(type, volinfo, secondary,
                                                 statefile, op_errstr);
                if (ret) {
                    ret = glusterd_get_local_brickpaths(volinfo, &path_list);
                    if (!path_list && ret == -1)
                        goto out;
                }
            }
            break;

        case GF_GSYNC_OPTION_TYPE_CONFIG:
            if (is_template_in_use) {
                snprintf(errmsg, sizeof(errmsg),
                         "state-file entry "
                         "missing in the config file(%s).",
                         conf_path);
                ret = -1;
                goto out;
            }

            ret = gsync_verify_config_options(dict, op_errstr, volname);
            goto out;
            break;

        case GF_GSYNC_OPTION_TYPE_DELETE:
            /* Check if the gsync session is still running
             * If so ask the user to stop geo-replication first.*/
            if (is_template_in_use) {
                snprintf(errmsg, sizeof(errmsg),
                         "state-file entry "
                         "missing in the config file(%s).",
                         conf_path);
                ret = -1;
                goto out;
            }

            ret = glusterd_gsync_get_uuid(secondary, volinfo, uuid);
            if (ret) {
                snprintf(errmsg, sizeof(errmsg),
                         "Geo-replication"
                         " session between %s and %s does not exist.",
                         volinfo->volname, secondary);
                ret = -1;
                goto out;
            } else {
                ret = glusterd_check_gsync_running_local(
                    volinfo->volname, secondary, conf_path, &is_running);
                if (_gf_true == is_running) {
                    snprintf(errmsg, sizeof(errmsg),
                             GEOREP
                             " session between %s & %s is "
                             "still active. Please stop the "
                             "session and retry.",
                             volinfo->volname, secondary);
                    ret = -1;
                    goto out;
                }
            }

            ret = glusterd_verify_gsyncd_spawn(volinfo->volname, secondary);
            if (ret) {
                snprintf(errmsg, sizeof(errmsg), "Unable to spawn gsyncd");
            }

            break;
    }

out:

    if (path_list)
        GF_FREE(path_list);

    if (ret && errmsg[0] != '\0') {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GSYNCD_ERROR, "%s", errmsg);
        *op_errstr = gf_strdup(errmsg);
    }

    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

static int
gd_pause_or_resume_gsync(dict_t *dict, char *primary, char *secondary,
                         char *secondary_host, char *secondary_vol,
                         char *conf_path, char **op_errstr,
                         gf_boolean_t is_pause)
{
    int32_t ret = 0;
    int pfd = -1;
    long pid = 0;
    char pidfile[PATH_MAX] = {
        0,
    };
    char errmsg[PATH_MAX] = "";
    char buf[4096] = {
        0,
    };
    gf_boolean_t is_template_in_use = _gf_false;
    char monitor_status[NAME_MAX] = {
        0,
    };
    char *statefile = NULL;
    xlator_t *this = THIS;

    GF_ASSERT(dict);
    GF_ASSERT(primary);
    GF_ASSERT(secondary);
    GF_ASSERT(secondary_host);
    GF_ASSERT(secondary_vol);
    GF_ASSERT(conf_path);

    pfd = gsyncd_getpidfile(primary, secondary, pidfile, conf_path,
                            &is_template_in_use);
    if (pfd == -2) {
        snprintf(errmsg, sizeof(errmsg),
                 "pid-file entry mising in config file and "
                 "template config file.");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_PIDFILE_NOT_FOUND, "%s",
               errmsg);
        *op_errstr = gf_strdup(errmsg);
        ret = -1;
        goto out;
    }

    if (gsync_status_byfd(pfd) == -1) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GSYNCD_ERROR,
               "gsyncd b/w %s & %s is not running", primary, secondary);
        /* monitor gsyncd already dead */
        goto out;
    }

    if (pfd < 0)
        goto out;

    /* Prepare to update status file*/
    ret = dict_get_str(dict, "statefile", &statefile);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Pause/Resume Failed: Unable to fetch statefile path");
        goto out;
    }
    ret = glusterd_gsync_read_frm_status(statefile, monitor_status,
                                         sizeof(monitor_status));
    if (ret <= 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_STAT_FILE_READ_FAILED,
               "Pause/Resume Failed: "
               "Unable to read status file for %s(primary)"
               " %s(primary)",
               primary, secondary);
        goto out;
    }

    ret = sys_read(pfd, buf, sizeof(buf) - 1);
    if (ret > 0) {
        buf[ret] = '\0';
        pid = strtol(buf, NULL, 10);
        if (is_pause) {
            ret = kill(-pid, SIGSTOP);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_PID_KILL_FAIL,
                       "Failed"
                       " to pause gsyncd. Error: %s",
                       strerror(errno));
                goto out;
            }
            /*On pause force, if status is already paused
              do not update status again*/
            if (strstr(monitor_status, "Paused"))
                goto out;

            ret = glusterd_create_status_file(
                primary, secondary, secondary_host, secondary_vol, "Paused");
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_UPDATE_STATEFILE_FAILED,
                       "Unable  to update state_file."
                       " Error : %s",
                       strerror(errno));
                /* If status cannot be updated resume back */
                if (kill(-pid, SIGCONT)) {
                    snprintf(errmsg, sizeof(errmsg),
                             "Pause successful but could "
                             "not update status file. "
                             "Please use 'resume force' to"
                             " resume back and retry pause"
                             " to reflect in status");
                    gf_msg(this->name, GF_LOG_ERROR, errno,
                           GD_MSG_PID_KILL_FAIL,
                           "Resume back Failed. Error:"
                           "%s",
                           strerror(errno));
                    *op_errstr = gf_strdup(errmsg);
                }
                goto out;
            }
        } else {
            ret = glusterd_create_status_file(
                primary, secondary, secondary_host, secondary_vol, "Started");
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_UPDATE_STATEFILE_FAILED,
                       "Resume Failed: Unable to update "
                       "state_file. Error : %s",
                       strerror(errno));
                goto out;
            }
            ret = kill(-pid, SIGCONT);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_PID_KILL_FAIL,
                       "Resumed Failed: Unable to send"
                       " SIGCONT. Error: %s",
                       strerror(errno));
                /* Process can't be resumed, update status
                 * back to paused. */
                ret = glusterd_create_status_file(primary, secondary,
                                                  secondary_host, secondary_vol,
                                                  monitor_status);
                if (ret) {
                    snprintf(errmsg, sizeof(errmsg),
                             "Resume failed!!! Status "
                             "inconsistent. Please use "
                             "'resume force' to resume and"
                             " reach consistent state");
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_STATUS_UPDATE_FAILED,
                           "Updating status back to paused"
                           " Failed. Error: %s",
                           strerror(errno));
                    *op_errstr = gf_strdup(errmsg);
                }
                goto out;
            }
        }
    }
    ret = 0;

out:
    sys_close(pfd);
    /* coverity[INTEGER_OVERFLOW] */
    return ret;
}

static int
stop_gsync(char *primary, char *secondary, char **msg, char *conf_path,
           char **op_errstr, gf_boolean_t is_force)
{
    int32_t ret = 0;
    int pfd = -1;
    long pid = 0;
    char pidfile[PATH_MAX] = {
        0,
    };
    char errmsg[PATH_MAX] = "";
    char buf[4096] = {
        0,
    };
    int i = 0;
    gf_boolean_t is_template_in_use = _gf_false;
    xlator_t *this = THIS;

    pfd = gsyncd_getpidfile(primary, secondary, pidfile, conf_path,
                            &is_template_in_use);
    if (pfd == -2) {
        snprintf(errmsg, sizeof(errmsg) - 1,
                 "pid-file entry mising in config file and "
                 "template config file.");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_PIDFILE_NOT_FOUND, "%s",
               errmsg);
        *op_errstr = gf_strdup(errmsg);
        ret = -1;
        goto out;
    }
    if (gsync_status_byfd(pfd) == -1 && !is_force) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GSYNCD_ERROR,
               "gsyncd b/w %s & %s is not running", primary, secondary);
        /* monitor gsyncd already dead */
        goto out;
    }

    if (pfd < 0)
        goto out;

    ret = sys_read(pfd, buf, sizeof(buf) - 1);
    if (ret > 0) {
        buf[ret] = '\0';
        pid = strtol(buf, NULL, 10);
        ret = kill(-pid, SIGTERM);
        if (ret && !is_force) {
            gf_msg(this->name, GF_LOG_WARNING, errno, GD_MSG_PID_KILL_FAIL,
                   "failed to kill gsyncd");
            goto out;
        }
        for (i = 0; i < 20; i++) {
            if (gsync_status_byfd(pfd) == -1) {
                /* monitor gsyncd is dead but worker may
                 * still be alive, give some more time
                 * before SIGKILL (hack)
                 */
                gf_nanosleep(50000 * GF_US_IN_NS);
                break;
            }
            gf_nanosleep(50000 * GF_US_IN_NS);
        }
        kill(-pid, SIGKILL);
        sys_unlink(pidfile);
    }
    ret = 0;

out:
    sys_close(pfd);
    /* coverity[INTEGER_OVERFLOW] */
    return ret;
}

/*
 * glusterd_gsync_op_already_set:
 *      This function checks whether the op_value is same as in the
 *      gsyncd.conf file.
 *
 * RETURN VALUE:
 *      0 : op_value matches the conf file.
 *      1 : op_value does not matches the conf file or op_param not
 *          found in conf file.
 *     -1 : error
 */

int
glusterd_gsync_op_already_set(char *primary, char *secondary, char *conf_path,
                              char *op_name, char *op_value)
{
    dict_t *confd = NULL;
    char *op_val_buf = NULL;
    int32_t op_val_conf = 0;
    int32_t op_val_cli = 0;
    int32_t ret = -1;
    gf_boolean_t is_bool = _gf_true;
    xlator_t *this = THIS;

    confd = dict_new();
    if (!confd) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_CREATE_FAIL,
               "Not able to create dict.");
        return -1;
    }

    ret = glusterd_gsync_get_config(primary, secondary, conf_path, confd);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GET_CONFIG_INFO_FAILED,
               "Unable to get configuration data for %s(primary), "
               "%s(secondary)",
               primary, secondary);
        goto out;
    }

    ret = dict_get_param(confd, op_name, &op_val_buf);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get op_value for %s(primary), %s(secondary). "
               "Please check gsync config file.",
               primary, secondary);
        ret = 1;
        goto out;
    }

    gf_msg_debug(this->name, 0, "val_cli:%s  val_conf:%s", op_value,
                 op_val_buf);

    if (!strcmp(op_val_buf, "true") || !strcmp(op_val_buf, "1") ||
        !strcmp(op_val_buf, "yes")) {
        op_val_conf = 1;
    } else if (!strcmp(op_val_buf, "false") || !strcmp(op_val_buf, "0") ||
               !strcmp(op_val_buf, "no")) {
        op_val_conf = 0;
    } else {
        is_bool = _gf_false;
    }

    if (is_bool) {
        if (op_value && (!strcmp(op_value, "true") || !strcmp(op_value, "1") ||
                         !strcmp(op_value, "yes"))) {
            op_val_cli = 1;
        } else {
            op_val_cli = 0;
        }

        if (op_val_cli == op_val_conf) {
            ret = 0;
            goto out;
        }
    } else {
        if (op_value && !strcmp(op_val_buf, op_value)) {
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
glusterd_gsync_configure(glusterd_volinfo_t *volinfo, char *secondary,
                         char *path_list, dict_t *dict, dict_t *resp_dict,
                         char **op_errstr)
{
    int32_t ret = -1;
    char *op_name = NULL;
    char *op_value = NULL;
    runner_t runner = {
        0,
    };
    glusterd_conf_t *priv = NULL;
    char *subop = NULL;
    char *primary = NULL;
    char *conf_path = NULL;
    char *secondary_host = NULL;
    char *secondary_vol = NULL;
    struct stat stbuf = {
        0,
    };
    gf_boolean_t restart_required = _gf_true;
    char **resopt = NULL;
    gf_boolean_t op_already_set = _gf_false;
    xlator_t *this = THIS;

    GF_ASSERT(secondary);
    GF_ASSERT(op_errstr);
    GF_ASSERT(dict);
    GF_ASSERT(resp_dict);

    ret = dict_get_str(dict, "subop", &subop);
    if (ret != 0)
        goto out;

    if (strcmp(subop, "get") == 0 || strcmp(subop, "get-all") == 0) {
        /* deferred to cli */
        gf_msg_debug(this->name, 0, "Returning 0");
        return 0;
    }

    ret = dict_get_str(dict, "op_name", &op_name);
    if (ret != 0)
        goto out;

    if (strtail(subop, "set")) {
        ret = dict_get_str(dict, "op_value", &op_value);
        if (ret != 0)
            goto out;
    }

    priv = THIS->private;
    if (priv == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GLUSTERD_PRIV_NOT_FOUND,
               "priv of glusterd not present");
        *op_errstr = gf_strdup("glusterd defunct");
        goto out;
    }

    ret = dict_get_str(dict, "conf_path", &conf_path);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch conf file path.");
        goto out;
    }

    primary = "";
    runinit(&runner);
    runner_add_args(&runner, GSYNCD_PREFIX "/gsyncd", "-c", NULL);
    runner_argprintf(&runner, "%s", conf_path);
    runner_argprintf(&runner, "--iprefix=%s", DATADIR);
    if (volinfo) {
        primary = volinfo->volname;
        runner_argprintf(&runner, ":%s", primary);
    }
    runner_add_arg(&runner, secondary);
    runner_argprintf(&runner, "--config-%s", subop);
    runner_add_arg(&runner, op_name);
    if (op_value) {
        runner_argprintf(&runner, "--value=%s", op_value);
    }

    if (strcmp(op_name, "checkpoint") != 0 && strtail(subop, "set")) {
        ret = glusterd_gsync_op_already_set(primary, secondary, conf_path,
                                            op_name, op_value);
        if (ret == -1) {
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_GSYNCD_OP_SET_FAILED,
                   "glusterd_gsync_op_already_set failed.");
            gf_asprintf(op_errstr,
                        GEOREP
                        " config-%s failed for "
                        "%s %s",
                        subop, primary, secondary);
            goto out;
        }
        if (ret == 0) {
            gf_msg_debug(this->name, 0, "op_value is already set");
            op_already_set = _gf_true;
            goto out;
        }
    }

    synclock_unlock(&priv->big_lock);
    ret = runner_run(&runner);
    synclock_lock(&priv->big_lock);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_GSYNCD_ERROR,
               "gsyncd failed to %s %s option for "
               "%s %s peers",
               subop, op_name, primary, secondary);

        gf_asprintf(op_errstr, GEOREP " config-%s failed for %s %s", subop,
                    primary, secondary);

        goto out;
    }

    if ((!strcmp(op_name, "state_file")) && (op_value)) {
        ret = sys_lstat(op_value, &stbuf);
        if (ret) {
            ret = dict_get_str(dict, "secondary_host", &secondary_host);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Unable to fetch secondary host.");
                goto out;
            }

            ret = dict_get_str(dict, "secondary_vol", &secondary_vol);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Unable to fetch secondary volume name.");
                goto out;
            }

            ret = glusterd_create_status_file(volinfo->volname, secondary,
                                              secondary_host, secondary_vol,
                                              "Switching Status "
                                              "File");
            if (ret || sys_lstat(op_value, &stbuf)) {
                gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
                       "Unable to "
                       "create %s. Error : %s",
                       op_value, strerror(errno));
                ret = -1;
                goto out;
            }
        }
    }

    ret = 0;
    gf_asprintf(op_errstr, "config-%s successful", subop);

out:
    if (!ret && volinfo && !op_already_set) {
        for (resopt = gsync_no_restart_opts; *resopt; resopt++) {
            restart_required = _gf_true;
            if (!strcmp((*resopt), op_name)) {
                restart_required = _gf_false;
                break;
            }
        }

        if (restart_required) {
            ret = glusterd_check_restart_gsync_session(
                volinfo, secondary, resp_dict, path_list, conf_path, 0);
            if (ret)
                *op_errstr = gf_strdup("internal error");
        }
    }

    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_gsync_read_frm_status(char *path, char *buf, size_t blen)
{
    int ret = 0;
    int status_fd = -1;
    xlator_t *this = THIS;

    GF_ASSERT(path);
    GF_ASSERT(buf);
    status_fd = open(path, O_RDONLY);
    if (status_fd == -1) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_FILE_OP_FAILED,
               "Unable to read gsyncd status file %s", path);
        return -1;
    }
    ret = sys_read(status_fd, buf, blen - 1);

    if (ret > 0) {
        buf[ret] = '\0';
        size_t len = strnlen(buf, ret);
        /* Ensure there is a NUL byte and that it's not the first.  */
        if (len == 0 || len == blen - 1) {
            ret = -1;
        } else {
            char *p = buf + len - 1;
            while (isspace(*p))
                *p-- = '\0';
        }
    } else if (ret == 0)
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GSYNCD_ERROR,
               "Status file of gsyncd is empty");
    else /* ret < 0 */
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GSYNCD_ERROR,
               "Status file of gsyncd is corrupt");

    sys_close(status_fd);
    return ret;
}

static int
dict_get_param(dict_t *dict, char *key, char **param)
{
    char *dk = NULL;
    char *s = NULL;
    char x = '\0';
    int ret = 0;

    if (dict_get_str(dict, key, param) == 0)
        return 0;

    dk = gf_strdup(key);
    if (!dk)
        return -1;

    s = strpbrk(dk, "-_");
    if (!s) {
        ret = -1;
        goto out;
    }
    x = (*s == '-') ? '_' : '-';
    *s++ = x;
    while ((s = strpbrk(s, "-_")))
        *s++ = x;

    ret = dict_get_str(dict, dk, param);
out:
    GF_FREE(dk);
    return ret;
}

int
glusterd_fetch_values_from_config(char *primary, char *secondary,
                                  char *confpath, dict_t *confd,
                                  char **statefile,
                                  char **georep_session_wrkng_dir,
                                  char **socketfile)
{
    int ret = 0;
    xlator_t *this = THIS;

    ret = glusterd_gsync_get_config(primary, secondary, confpath, confd);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GET_CONFIG_INFO_FAILED,
               "Unable to get configuration data for %s(primary), "
               "%s(secondary)",
               primary, secondary);
        goto out;
    }

    if (statefile) {
        ret = dict_get_param(confd, "state_file", statefile);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to get state_file's name "
                   "for %s(primary), %s(secondary). "
                   "Please check gsync config file.",
                   primary, secondary);
            goto out;
        }
    }

    if (georep_session_wrkng_dir) {
        ret = dict_get_param(confd, "georep_session_working_dir",
                             georep_session_wrkng_dir);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to get geo-rep session's "
                   "working directory name for %s(primary), "
                   "%s(secondary). Please check gsync config file.",
                   primary, secondary);
            goto out;
        }
    }

    if (socketfile) {
        ret = dict_get_param(confd, "state_socket_unencoded", socketfile);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to get socket file's name "
                   "for %s(primary), %s(secondary). "
                   "Please check gsync config file.",
                   primary, secondary);
            goto out;
        }
    }

    ret = 0;
out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_read_status_file(glusterd_volinfo_t *volinfo, char *secondary,
                          char *conf_path, dict_t *dict, char *node)
{
    char temp_conf_path[PATH_MAX] = "";
    char *working_conf_path = NULL;
    char *georep_session_wrkng_dir = NULL;
    char *primary = NULL;
    char sts_val_name[1024] = "";
    char monitor_status[NAME_MAX] = "";
    char *statefile = NULL;
    char *socketfile = NULL;
    dict_t *confd = NULL;
    char *secondarykey = NULL;
    char *secondaryentry = NULL;
    char *secondaryuser = NULL;
    char *saveptr = NULL;
    char *temp = NULL;
    char *temp_inp = NULL;
    char *brick_host_uuid = NULL;
    int brick_host_uuid_length = 0;
    int gsync_count = 0;
    int ret = 0;
    glusterd_brickinfo_t *brickinfo = NULL;
    gf_gsync_status_t *sts_val = NULL;
    gf_boolean_t is_template_in_use = _gf_false;
    glusterd_conf_t *priv = NULL;
    struct stat stbuf = {
        0,
    };
    xlator_t *this = THIS;
    int32_t len = 0;

    priv = this->private;
    GF_ASSERT(priv);
    GF_ASSERT(volinfo);
    GF_ASSERT(conf_path);

    primary = volinfo->volname;

    confd = dict_new();
    if (!confd) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_CREATE_FAIL,
               "Not able to create dict.");
        ret = -1;
        goto out;
    }

    len = snprintf(temp_conf_path, sizeof(temp_conf_path),
                   "%s/" GSYNC_CONF_TEMPLATE, priv->workdir);
    if ((len < 0) || (len >= sizeof(temp_conf_path))) {
        ret = -1;
        goto out;
    }

    ret = sys_lstat(conf_path, &stbuf);
    if (!ret) {
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_CONFIG_INFO,
               "Using passed config template(%s).", conf_path);
        working_conf_path = conf_path;
    } else {
        gf_msg(this->name, GF_LOG_WARNING, ENOENT, GD_MSG_FILE_OP_FAILED,
               "Config file (%s) missing. Looking for template "
               "config file (%s)",
               conf_path, temp_conf_path);
        ret = sys_lstat(temp_conf_path, &stbuf);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, ENOENT, GD_MSG_FILE_OP_FAILED,
                   "Template "
                   "config file (%s) missing.",
                   temp_conf_path);
            goto out;
        }
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_DEFAULT_TEMP_CONFIG,
               "Using default config template(%s).", temp_conf_path);
        working_conf_path = temp_conf_path;
        is_template_in_use = _gf_true;
    }

fetch_data:
    ret = glusterd_fetch_values_from_config(
        primary, secondary, working_conf_path, confd, &statefile,
        &georep_session_wrkng_dir, &socketfile);
    if (ret) {
        if (is_template_in_use == _gf_false) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_FETCH_CONFIG_VAL_FAILED,
                   "Unable to fetch config values "
                   "for %s(primary), %s(secondary). "
                   "Trying default config template",
                   primary, secondary);
            working_conf_path = temp_conf_path;
            is_template_in_use = _gf_true;
            goto fetch_data;
        } else {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_FETCH_CONFIG_VAL_FAILED,
                   "Unable to "
                   "fetch config values for %s(primary), "
                   "%s(secondary)",
                   primary, secondary);
            goto out;
        }
    }

    ret = glusterd_gsync_read_frm_status(statefile, monitor_status,
                                         sizeof(monitor_status));
    if (ret <= 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_STAT_FILE_READ_FAILED,
               "Unable to read the status file for %s(primary), "
               "%s(secondary) statefile: %s",
               primary, secondary, statefile);
        snprintf(monitor_status, sizeof(monitor_status), "defunct");
    }

    ret = dict_get_int32(dict, "gsync-count", &gsync_count);
    if (ret)
        gsync_count = 0;

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        if (gf_uuid_compare(brickinfo->uuid, MY_UUID))
            continue;

        sts_val = GF_CALLOC(1, sizeof(gf_gsync_status_t),
                            gf_common_mt_gsync_status_t);
        if (!sts_val) {
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                   "Out Of Memory");
            goto out;
        }

        /* Secondary Key */
        ret = glusterd_get_secondary(volinfo, secondary, &secondarykey);
        if (ret < 0) {
            GF_FREE(sts_val);
            goto out;
        }
        memcpy(sts_val->secondarykey, secondarykey, strlen(secondarykey));
        sts_val->secondarykey[strlen(secondarykey)] = '\0';

        /* Primary Volume */
        memcpy(sts_val->primary, primary, strlen(primary));
        sts_val->primary[strlen(primary)] = '\0';

        /* Primary Brick Node */
        memcpy(sts_val->node, brickinfo->hostname, strlen(brickinfo->hostname));
        sts_val->node[strlen(brickinfo->hostname)] = '\0';

        /* Primary Brick Path */
        memcpy(sts_val->brick, brickinfo->path, strlen(brickinfo->path));
        sts_val->brick[strlen(brickinfo->path)] = '\0';

        /* Brick Host UUID */
        brick_host_uuid = uuid_utoa(brickinfo->uuid);
        brick_host_uuid_length = strlen(brick_host_uuid);
        memcpy(sts_val->brick_host_uuid, brick_host_uuid,
               brick_host_uuid_length);
        sts_val->brick_host_uuid[brick_host_uuid_length] = '\0';

        /* Secondary */
        memcpy(sts_val->secondary, secondary, strlen(secondary));
        sts_val->secondary[strlen(secondary)] = '\0';

        snprintf(sts_val->secondary_node, sizeof(sts_val->secondary_node),
                 "N/A");

        snprintf(sts_val->worker_status, sizeof(sts_val->worker_status), "N/A");

        snprintf(sts_val->crawl_status, sizeof(sts_val->crawl_status), "N/A");

        snprintf(sts_val->last_synced, sizeof(sts_val->last_synced), "N/A");

        snprintf(sts_val->last_synced_utc, sizeof(sts_val->last_synced_utc),
                 "N/A");

        snprintf(sts_val->entry, sizeof(sts_val->entry), "N/A");

        snprintf(sts_val->data, sizeof(sts_val->data), "N/A");

        snprintf(sts_val->meta, sizeof(sts_val->meta), "N/A");

        snprintf(sts_val->failures, sizeof(sts_val->failures), "N/A");

        snprintf(sts_val->checkpoint_time, sizeof(sts_val->checkpoint_time),
                 "N/A");

        snprintf(sts_val->checkpoint_time_utc,
                 sizeof(sts_val->checkpoint_time_utc), "N/A");

        snprintf(sts_val->checkpoint_completed,
                 sizeof(sts_val->checkpoint_completed), "N/A");

        snprintf(sts_val->checkpoint_completion_time,
                 sizeof(sts_val->checkpoint_completion_time), "N/A");

        snprintf(sts_val->checkpoint_completion_time_utc,
                 sizeof(sts_val->checkpoint_completion_time_utc), "N/A");

        /* Get all the other values from Gsyncd */
        ret = glusterd_gsync_get_status(primary, secondary, conf_path,
                                        brickinfo->path, sts_val);

        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GET_STATUS_DATA_FAIL,
                   "Unable to get status data "
                   "for %s(primary), %s(secondary), %s(brick)",
                   primary, secondary, brickinfo->path);
            ret = -1;
            goto out;
        }

        if (is_template_in_use) {
            snprintf(sts_val->worker_status, sizeof(sts_val->worker_status),
                     "Config Corrupted");
        }

        ret = dict_get_str(volinfo->gsync_secondaries, secondarykey,
                           &secondaryentry);
        if (ret < 0) {
            GF_FREE(sts_val);
            goto out;
        }

        memcpy(sts_val->session_secondary, secondaryentry,
               strlen(secondaryentry));
        sts_val->session_secondary[strlen(secondaryentry)] = '\0';

        temp_inp = gf_strdup(secondaryentry);
        if (!temp_inp)
            goto out;

        if (strstr(temp_inp, "@") == NULL) {
            secondaryuser = "root";
        } else {
            temp = strtok_r(temp_inp, "//", &saveptr);
            temp = strtok_r(NULL, "/", &saveptr);
            secondaryuser = strtok_r(temp, "@", &saveptr);
        }
        memcpy(sts_val->secondary_user, secondaryuser, strlen(secondaryuser));
        sts_val->secondary_user[strlen(secondaryuser)] = '\0';

        snprintf(sts_val_name, sizeof(sts_val_name), "status_value%d",
                 gsync_count);
        ret = dict_set_bin(dict, sts_val_name, sts_val,
                           sizeof(gf_gsync_status_t));
        if (ret) {
            GF_FREE(sts_val);
            goto out;
        }

        gsync_count++;
        sts_val = NULL;
    }

    ret = dict_set_int32(dict, "gsync-count", gsync_count);

out:
    GF_FREE(temp_inp);
    if (confd)
        dict_unref(confd);

    return ret;
}

int
glusterd_check_restart_gsync_session(glusterd_volinfo_t *volinfo,
                                     char *secondary, dict_t *resp_dict,
                                     char *path_list, char *conf_path,
                                     gf_boolean_t is_force)
{
    int ret = 0;
    char *status_msg = NULL;
    gf_boolean_t is_running = _gf_false;
    char *op_errstr = NULL;
    char *key = NULL;
    xlator_t *this = THIS;

    GF_ASSERT(volinfo);
    GF_ASSERT(secondary);

    key = secondary;

    ret = glusterd_check_gsync_running_local(volinfo->volname, secondary,
                                             conf_path, &is_running);
    if (!ret && (_gf_true != is_running))
        /* gsynd not running, nothing to do */
        goto out;

    ret = stop_gsync(volinfo->volname, secondary, &status_msg, conf_path,
                     &op_errstr, is_force);
    if (ret == 0 && status_msg)
        ret = dict_set_str(resp_dict, "gsync-status", status_msg);
    if (ret == 0) {
        dict_del(volinfo->gsync_active_secondaries, key);
        ret = glusterd_start_gsync(volinfo, secondary, path_list, conf_path,
                                   uuid_utoa(MY_UUID), NULL, _gf_false);
        if (!ret) {
            /* Add secondary to the dict indicating geo-rep session is
             * running.*/
            ret = dict_set_dynstr_with_alloc(volinfo->gsync_active_secondaries,
                                             key, "running");
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Unable to set"
                       " key:%s value:running in dict. But "
                       "the config succeeded.",
                       key);
                goto out;
            }
        }
    }

out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    if (op_errstr)
        GF_FREE(op_errstr);
    return ret;
}

static int32_t
glusterd_marker_changelog_create_volfile(glusterd_volinfo_t *volinfo)
{
    int32_t ret = 0;

    ret = glusterd_create_volfiles_and_notify_services(volinfo);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_VOLFILE_CREATE_FAIL,
               "Unable to create volfile for setting of marker "
               "while '" GEOREP " start'");
        ret = -1;
        goto out;
    }

    ret = glusterd_store_volinfo(volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
    if (ret)
        goto out;

    if (GLUSTERD_STATUS_STARTED == volinfo->status) {
        ret = glusterd_svcs_manager(volinfo);
        goto out;
    }
    ret = 0;
out:
    return ret;
}

static int
glusterd_set_gsync_knob(glusterd_volinfo_t *volinfo, char *key, int *vc)
{
    int ret = -1;
    int conf_enabled = _gf_false;
    xlator_t *this = THIS;

    conf_enabled = glusterd_volinfo_get_boolean(volinfo, key);
    if (conf_enabled == -1) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GET_KEY_FAILED,
               "failed to get key %s from volinfo", key);
        goto out;
    }

    ret = 0;
    if (conf_enabled == _gf_false) {
        *vc = 1;
        ret = glusterd_gsync_volinfo_dict_set(volinfo, key, "on");
    }

out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

static int
glusterd_set_gsync_confs(glusterd_volinfo_t *volinfo)
{
    int ret = -1;
    int volfile_changed = 0;

    ret = glusterd_set_gsync_knob(volinfo, VKEY_MARKER_XTIME, &volfile_changed);
    if (ret)
        goto out;

    /**
     * enable ignore-pid-check blindly as it could be needed for
     * cascading setups.
     */
    ret = glusterd_set_gsync_knob(volinfo, VKEY_MARKER_XTIME_FORCE,
                                  &volfile_changed);
    if (ret)
        goto out;

    ret = glusterd_set_gsync_knob(volinfo, VKEY_CHANGELOG, &volfile_changed);
    if (ret)
        goto out;

    if (volfile_changed)
        ret = glusterd_marker_changelog_create_volfile(volinfo);

out:
    return ret;
}

static int
glusterd_get_gsync_status_mst_slv(glusterd_volinfo_t *volinfo, char *secondary,
                                  char *conf_path, dict_t *rsp_dict, char *node)
{
    char *statefile = NULL;
    uuid_t uuid = {
        0,
    };
    int ret = 0;
    gf_boolean_t is_template_in_use = _gf_false;
    struct stat stbuf = {
        0,
    };
    xlator_t *this = THIS;

    GF_ASSERT(volinfo);
    GF_ASSERT(secondary);

    ret = glusterd_gsync_get_uuid(secondary, volinfo, uuid);
    if (ret) {
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_SESSION_INACTIVE,
               "geo-replication status %s %s : session is not "
               "active",
               volinfo->volname, secondary);

        ret = glusterd_get_statefile_name(volinfo, secondary, conf_path,
                                          &statefile, &is_template_in_use);
        if (ret) {
            if (!strstr(secondary, "::"))
                gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_SECONDARY_URL_INVALID,
                       "%s is not a valid secondary url.", secondary);
            else
                gf_msg(this->name, GF_LOG_INFO, 0,
                       GD_MSG_GET_STATEFILE_NAME_FAILED,
                       "Unable to get statefile's name");
            ret = 0;
            goto out;
        }

        ret = sys_lstat(statefile, &stbuf);
        if (ret) {
            gf_msg(this->name, GF_LOG_INFO, ENOENT, GD_MSG_FILE_OP_FAILED,
                   "%s statefile not present.", statefile);
            ret = 0;
            goto out;
        }
    }

    ret = glusterd_read_status_file(volinfo, secondary, conf_path, rsp_dict,
                                    node);
out:
    if (statefile)
        GF_FREE(statefile);

    gf_msg_debug(this->name, 0, "Returning with %d", ret);
    return ret;
}

int
glusterd_get_gsync_status_mst(glusterd_volinfo_t *volinfo, dict_t *rsp_dict,
                              char *node)
{
    glusterd_gsync_status_temp_t param = {
        0,
    };

    GF_ASSERT(volinfo);

    param.rsp_dict = rsp_dict;
    param.volinfo = volinfo;
    param.node = node;
    dict_foreach(volinfo->gsync_secondaries, _get_status_mst_slv, &param);

    return 0;
}

static int
glusterd_get_gsync_status_all(dict_t *rsp_dict, char *node)
{
    int32_t ret = 0;
    glusterd_conf_t *priv = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = THIS;

    priv = this->private;

    GF_ASSERT(priv);

    cds_list_for_each_entry(volinfo, &priv->volumes, vol_list)
    {
        ret = glusterd_get_gsync_status_mst(volinfo, rsp_dict, node);
        if (ret)
            goto out;
    }

out:
    gf_msg_debug(this->name, 0, "Returning with %d", ret);
    return ret;
}

static int
glusterd_get_gsync_status(dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
    char *secondary = NULL;
    char *volname = NULL;
    char *conf_path = NULL;
    char errmsg[PATH_MAX] = {
        0,
    };
    glusterd_volinfo_t *volinfo = NULL;
    int ret = 0;
    char *my_hostname = gf_gethostname();
    xlator_t *this = THIS;

    ret = dict_get_str(dict, "primary", &volname);
    if (ret < 0) {
        ret = glusterd_get_gsync_status_all(rsp_dict, my_hostname);
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_VOL_NOT_FOUND,
               "volume name does not exist");
        snprintf(errmsg, sizeof(errmsg),
                 "Volume name %s does not"
                 " exist",
                 volname);
        *op_errstr = gf_strdup(errmsg);
        goto out;
    }

    ret = dict_get_str(dict, "secondary", &secondary);
    if (ret < 0) {
        ret = glusterd_get_gsync_status_mst(volinfo, rsp_dict, my_hostname);
        goto out;
    }

    ret = dict_get_str(dict, "conf_path", &conf_path);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch conf file path.");
        goto out;
    }

    ret = glusterd_get_gsync_status_mst_slv(volinfo, secondary, conf_path,
                                            rsp_dict, my_hostname);

out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

static int
glusterd_gsync_delete(glusterd_volinfo_t *volinfo, char *secondary,
                      char *secondary_host, char *secondary_vol,
                      char *path_list, dict_t *dict, dict_t *resp_dict,
                      char **op_errstr)
{
    int32_t ret = -1;
    runner_t runner = {
        0,
    };
    glusterd_conf_t *priv = NULL;
    char *primary = NULL;
    char *gl_workdir = NULL;
    char geo_rep_dir[PATH_MAX] = "";
    char *conf_path = NULL;
    xlator_t *this = THIS;
    uint32_t reset_sync_time = _gf_false;

    GF_ASSERT(secondary);
    GF_ASSERT(secondary_host);
    GF_ASSERT(secondary_vol);
    GF_ASSERT(op_errstr);
    GF_ASSERT(dict);
    GF_ASSERT(resp_dict);

    priv = this->private;
    if (priv == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GLUSTERD_PRIV_NOT_FOUND,
               "priv of glusterd not present");
        *op_errstr = gf_strdup("glusterd defunct");
        goto out;
    }

    ret = dict_get_str(dict, "conf_path", &conf_path);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch conf file path.");
        goto out;
    }

    gl_workdir = priv->workdir;
    primary = "";
    runinit(&runner);
    runner_add_args(&runner, GSYNCD_PREFIX "/gsyncd", "--delete", "-c", NULL);
    runner_argprintf(&runner, "%s", conf_path);
    runner_argprintf(&runner, "--iprefix=%s", DATADIR);

    runner_argprintf(&runner, "--path-list=%s", path_list);

    ret = dict_get_uint32(dict, "reset-sync-time", &reset_sync_time);
    if (!ret && reset_sync_time) {
        runner_add_args(&runner, "--reset-sync-time", NULL);
    }

    if (volinfo) {
        primary = volinfo->volname;
        runner_argprintf(&runner, ":%s", primary);
    }
    runner_add_arg(&runner, secondary);
    runner_redir(&runner, STDOUT_FILENO, RUN_PIPE);
    synclock_unlock(&priv->big_lock);
    ret = runner_run(&runner);
    synclock_lock(&priv->big_lock);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SESSION_DEL_FAILED,
               "gsyncd failed to delete session info for %s and "
               "%s peers",
               primary, secondary);

        gf_asprintf(op_errstr,
                    "gsyncd failed to "
                    "delete session info for %s and %s peers",
                    primary, secondary);

        goto out;
    }

    ret = snprintf(geo_rep_dir, sizeof(geo_rep_dir) - 1,
                   "%s/" GEOREP "/%s_%s_%s", gl_workdir, volinfo->volname,
                   secondary_host, secondary_vol);
    geo_rep_dir[ret] = '\0';

    ret = sys_rmdir(geo_rep_dir);
    if (ret) {
        if (errno == ENOENT)
            gf_msg_debug(this->name, 0, "Geo Rep Dir(%s) Not Present.",
                         geo_rep_dir);
        else {
            gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
                   "Unable to delete Geo Rep Dir(%s). Error: %s", geo_rep_dir,
                   strerror(errno));
            goto out;
        }
    }

    ret = 0;

    gf_asprintf(op_errstr, "delete successful");

out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_op_sys_exec(dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
    char buf[PATH_MAX] = "";
    char cmd_arg_name[PATH_MAX] = "";
    char output_name[PATH_MAX] = "";
    char errmsg[PATH_MAX] = "";
    char *ptr = NULL;
    char *bufp = NULL;
    char *command = NULL;
    char **cmd_args = NULL;
    int ret = -1;
    int i = -1;
    int cmd_args_count = 0;
    int output_count = 0;
    glusterd_conf_t *priv = NULL;
    runner_t runner = {
        0,
    };
    xlator_t *this = THIS;

    GF_ASSERT(dict);
    GF_ASSERT(op_errstr);
    GF_ASSERT(rsp_dict);

    priv = this->private;
    if (priv == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GLUSTERD_PRIV_NOT_FOUND,
               "priv of glusterd not present");
        *op_errstr = gf_strdup("glusterd defunct");
        goto out;
    }

    ret = dict_get_str(dict, "command", &command);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get command from dict");
        goto out;
    }

    ret = dict_get_int32(dict, "cmd_args_count", &cmd_args_count);
    if (ret)
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
               "No cmd_args_count");

    if (cmd_args_count) {
        cmd_args = GF_CALLOC(cmd_args_count, sizeof(char *), gf_common_mt_char);
        if (!cmd_args) {
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                   "Unable to calloc. Errno = %s", strerror(errno));
            goto out;
        }

        for (i = 1; i <= cmd_args_count; i++) {
            snprintf(cmd_arg_name, sizeof(cmd_arg_name), "cmd_arg_%d", i);
            ret = dict_get_str(dict, cmd_arg_name, &cmd_args[i - 1]);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Unable to get"
                       " %s in dict",
                       cmd_arg_name);
                goto out;
            }
        }
    }

    runinit(&runner);
    runner_argprintf(&runner, GSYNCD_PREFIX "/peer_%s", command);
    for (i = 0; i < cmd_args_count; i++)
        runner_add_arg(&runner, cmd_args[i]);
    runner_redir(&runner, STDOUT_FILENO, RUN_PIPE);
    synclock_unlock(&priv->big_lock);
    ret = runner_start(&runner);
    if (ret == -1) {
        snprintf(errmsg, sizeof(errmsg),
                 "Unable to "
                 "execute command. Error : %s",
                 strerror(errno));
        *op_errstr = gf_strdup(errmsg);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_CMD_EXEC_FAIL, "%s", errmsg);
        ret = -1;
        synclock_lock(&priv->big_lock);
        goto out;
    }

    do {
        ptr = fgets(buf, sizeof(buf), runner_chio(&runner, STDOUT_FILENO));
        if (ptr) {
            ret = dict_get_int32(rsp_dict, "output_count", &output_count);
            if (ret)
                output_count = 1;
            else
                output_count++;
            snprintf(output_name, sizeof(output_name), "output_%d",
                     output_count);
            if (buf[strlen(buf) - 1] == '\n')
                buf[strlen(buf) - 1] = '\0';
            bufp = gf_strdup(buf);
            if (!bufp)
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_STRDUP_FAILED,
                       "gf_strdup failed.");
            ret = dict_set_dynstr(rsp_dict, output_name, bufp);
            if (ret) {
                GF_FREE(bufp);
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "output set "
                       "failed.");
            }
            ret = dict_set_int32(rsp_dict, "output_count", output_count);
            if (ret)
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "output_count "
                       "set failed.");
        }
    } while (ptr);

    ret = runner_end(&runner);
    if (ret) {
        snprintf(errmsg, sizeof(errmsg),
                 "Unable to "
                 "end. Error : %s",
                 strerror(errno));
        *op_errstr = gf_strdup(errmsg);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_UNABLE_TO_END, "%s", errmsg);
        ret = -1;
        synclock_lock(&priv->big_lock);
        goto out;
    }
    synclock_lock(&priv->big_lock);

    ret = 0;
out:
    if (cmd_args) {
        GF_FREE(cmd_args);
        cmd_args = NULL;
    }

    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_op_copy_file(dict_t *dict, char **op_errstr)
{
    char abs_filename[PATH_MAX] = "";
    char errmsg[PATH_MAX] = "";
    char *filename = NULL;
    char *host_uuid = NULL;
    char uuid_str[64] = {0};
    char *contents = NULL;
    char buf[4096] = "";
    int ret = -1;
    int fd = -1;
    int bytes_writen = 0;
    int bytes_read = 0;
    int contents_size = -1;
    int file_mode = -1;
    glusterd_conf_t *priv = NULL;
    struct stat stbuf = {
        0,
    };
    gf_boolean_t free_contents = _gf_true;
    xlator_t *this = THIS;
    int32_t len = 0;

    priv = this->private;
    if (priv == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GLUSTERD_PRIV_NOT_FOUND,
               "priv of glusterd not present");
        *op_errstr = gf_strdup("glusterd defunct");
        goto out;
    }

    ret = dict_get_str(dict, "host-uuid", &host_uuid);
    if (ret < 0)
        goto out;

    ret = dict_get_str(dict, "source", &filename);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch filename from dict.");
        *op_errstr = gf_strdup("command unsuccessful");
        goto out;
    }
    len = snprintf(abs_filename, sizeof(abs_filename), "%s/%s", priv->workdir,
                   filename);
    if ((len < 0) || (len >= sizeof(abs_filename))) {
        ret = -1;
        goto out;
    }

    uuid_utoa_r(MY_UUID, uuid_str);
    if (!strcmp(uuid_str, host_uuid)) {
        ret = sys_lstat(abs_filename, &stbuf);
        if (ret) {
            len = snprintf(errmsg, sizeof(errmsg),
                           "Source file "
                           "does not exist in %s",
                           priv->workdir);
            if (len < 0) {
                strcpy(errmsg, "<error>");
            }
            *op_errstr = gf_strdup(errmsg);
            gf_msg(this->name, GF_LOG_ERROR, ENOENT, GD_MSG_FILE_OP_FAILED,
                   "%s", errmsg);
            goto out;
        }

        contents = GF_CALLOC(1, stbuf.st_size + 1, gf_common_mt_char);
        if (!contents) {
            snprintf(errmsg, sizeof(errmsg), "Unable to allocate memory");
            *op_errstr = gf_strdup(errmsg);
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY, "%s",
                   errmsg);
            ret = -1;
            goto out;
        }

        fd = open(abs_filename, O_RDONLY);
        if (fd < 0) {
            len = snprintf(errmsg, sizeof(errmsg), "Unable to open %s",
                           abs_filename);
            if (len < 0) {
                strcpy(errmsg, "<error>");
            }
            *op_errstr = gf_strdup(errmsg);
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_FILE_OP_FAILED, "%s",
                   errmsg);
            ret = -1;
            goto out;
        }

        do {
            ret = sys_read(fd, buf, sizeof(buf) - 1);
            if (ret > 0) {
                buf[ret] = '\0';
                memcpy(contents + bytes_read, buf, ret);
                bytes_read += ret;
            }
        } while (ret > 0);

        if (bytes_read != stbuf.st_size) {
            len = snprintf(errmsg, sizeof(errmsg),
                           "Unable to read all the data from %s", abs_filename);
            if (len < 0) {
                strcpy(errmsg, "<error>");
            }
            *op_errstr = gf_strdup(errmsg);
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_READ_ERROR, "%s",
                   errmsg);
            ret = -1;
            goto out;
        }

        ret = dict_set_int32(dict, "contents_size", stbuf.st_size);
        if (ret) {
            snprintf(errmsg, sizeof(errmsg),
                     "Unable to set"
                     " contents size in dict.");
            *op_errstr = gf_strdup(errmsg);
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED, "%s",
                   errmsg);
            goto out;
        }

        ret = dict_set_int32(dict, "file_mode", (int32_t)stbuf.st_mode);
        if (ret) {
            snprintf(errmsg, sizeof(errmsg),
                     "Unable to set"
                     " file mode in dict.");
            *op_errstr = gf_strdup(errmsg);
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED, "%s",
                   errmsg);
            goto out;
        }

        ret = dict_set_bin(dict, "common_pem_contents", contents,
                           stbuf.st_size);
        if (ret) {
            snprintf(errmsg, sizeof(errmsg),
                     "Unable to set"
                     " pem contents in dict.");
            *op_errstr = gf_strdup(errmsg);
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED, "%s",
                   errmsg);
            goto out;
        }
        free_contents = _gf_false;
    } else {
        free_contents = _gf_false;
        ret = dict_get_bin(dict, "common_pem_contents", (void **)&contents);
        if (ret) {
            snprintf(errmsg, sizeof(errmsg),
                     "Unable to get"
                     " pem contents in dict.");
            *op_errstr = gf_strdup(errmsg);
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
                   errmsg);
            goto out;
        }
        ret = dict_get_int32(dict, "contents_size", &contents_size);
        if (ret) {
            snprintf(errmsg, sizeof(errmsg),
                     "Unable to set"
                     " contents size in dict.");
            *op_errstr = gf_strdup(errmsg);
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
                   errmsg);
            goto out;
        }

        ret = dict_get_int32(dict, "file_mode", &file_mode);
        if (ret) {
            snprintf(errmsg, sizeof(errmsg),
                     "Unable to get"
                     " file mode in dict.");
            *op_errstr = gf_strdup(errmsg);
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
                   errmsg);
            goto out;
        }

        fd = open(abs_filename, O_WRONLY | O_TRUNC | O_CREAT, 0600);
        if (fd < 0) {
            len = snprintf(errmsg, sizeof(errmsg), "Unable to open %s",
                           abs_filename);
            if (len < 0) {
                strcpy(errmsg, "<error>");
            }
            *op_errstr = gf_strdup(errmsg);
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_FILE_OP_FAILED, "%s",
                   errmsg);
            ret = -1;
            goto out;
        }

        bytes_writen = sys_write(fd, contents, contents_size);

        if (bytes_writen != contents_size) {
            len = snprintf(errmsg, sizeof(errmsg), "Failed to write to %s",
                           abs_filename);
            if (len < 0) {
                strcpy(errmsg, "<error>");
            }
            *op_errstr = gf_strdup(errmsg);
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_FILE_OP_FAILED, "%s",
                   errmsg);
            ret = -1;
            goto out;
        }

        sys_fchmod(fd, file_mode);
    }

    ret = 0;
out:
    if (fd != -1)
        sys_close(fd);

    if (free_contents)
        GF_FREE(contents);

    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_op_gsync_set(dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
    int32_t ret = -1;
    int32_t type = -1;
    char *host_uuid = NULL;
    char *secondary = NULL;
    char *secondary_url = NULL;
    char *secondary_vol = NULL;
    char *secondary_host = NULL;
    char *volname = NULL;
    char *path_list = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_conf_t *priv = NULL;
    gf_boolean_t is_force = _gf_false;
    char *status_msg = NULL;
    gf_boolean_t is_running = _gf_false;
    char *conf_path = NULL;
    char *key = NULL;
    xlator_t *this = THIS;

    priv = this->private;
    GF_ASSERT(priv);
    GF_ASSERT(dict);
    GF_ASSERT(op_errstr);
    GF_ASSERT(rsp_dict);

    ret = dict_get_int32(dict, "type", &type);
    if (ret < 0)
        goto out;

    ret = dict_get_str(dict, "host-uuid", &host_uuid);
    if (ret < 0)
        goto out;

    if (type == GF_GSYNC_OPTION_TYPE_STATUS) {
        ret = glusterd_get_gsync_status(dict, op_errstr, rsp_dict);
        goto out;
    }

    ret = dict_get_str(dict, "secondary", &secondary);
    if (ret < 0)
        goto out;

    key = secondary;

    ret = dict_get_str(dict, "secondary_url", &secondary_url);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch secondary url.");
        goto out;
    }

    ret = dict_get_str(dict, "secondary_host", &secondary_host);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch secondary hostname.");
        goto out;
    }

    ret = dict_get_str(dict, "secondary_vol", &secondary_vol);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch secondary volume name.");
        goto out;
    }

    ret = dict_get_str(dict, "conf_path", &conf_path);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch conf file path.");
        goto out;
    }

    if (dict_get_str(dict, "primary", &volname) == 0) {
        ret = glusterd_volinfo_find(volname, &volinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_DICT_GET_FAILED,
                   "Volinfo for"
                   " %s (primary) not found",
                   volname);
            goto out;
        }

        ret = glusterd_get_local_brickpaths(volinfo, &path_list);
        if (!path_list && ret == -1)
            goto out;
    }

    if (type == GF_GSYNC_OPTION_TYPE_CONFIG) {
        ret = glusterd_gsync_configure(volinfo, secondary, path_list, dict,
                                       rsp_dict, op_errstr);
        if (!ret) {
            ret = dict_set_str(rsp_dict, "conf_path", conf_path);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Unable to store conf_file_path.");
                goto out;
            }
        }
        goto out;
    }

    if (type == GF_GSYNC_OPTION_TYPE_DELETE) {
        ret = glusterd_remove_secondary_in_info(volinfo, secondary, op_errstr);
        if (ret && !is_force && path_list)
            goto out;

        ret = glusterd_gsync_delete(volinfo, secondary, secondary_host,
                                    secondary_vol, path_list, dict, rsp_dict,
                                    op_errstr);
        goto out;
    }

    if (!volinfo) {
        ret = -1;
        goto out;
    }

    is_force = dict_get_str_boolean(dict, "force", _gf_false);

    if (type == GF_GSYNC_OPTION_TYPE_START) {
        /* Add secondary to the dict indicating geo-rep session is running*/
        ret = dict_set_dynstr_with_alloc(volinfo->gsync_active_secondaries, key,
                                         "running");
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Unable to set key:%s"
                   " value:running in the dict",
                   key);
            goto out;
        }

        /* If secondary volume uuid is not present in gsync_secondaries
         * update it*/
        ret = glusterd_update_secondary_voluuid_secondaryinfo(volinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_REMOTE_VOL_UUID_FAIL,
                   "Error in updating"
                   " secondary volume uuid for old secondary info");
            goto out;
        }

        ret = glusterd_start_gsync(volinfo, secondary, path_list, conf_path,
                                   host_uuid, op_errstr, _gf_false);

        /* Delete added secondary in the dict if start fails*/
        if (ret)
            dict_del(volinfo->gsync_active_secondaries, key);
    }

    if (type == GF_GSYNC_OPTION_TYPE_STOP ||
        type == GF_GSYNC_OPTION_TYPE_PAUSE ||
        type == GF_GSYNC_OPTION_TYPE_RESUME) {
        ret = glusterd_check_gsync_running_local(volinfo->volname, secondary,
                                                 conf_path, &is_running);
        if (!ret && !is_force && path_list && (_gf_true != is_running)) {
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_GSYNCD_OP_SET_FAILED,
                   GEOREP
                   " is not "
                   "set up for %s(primary) and %s(secondary)",
                   volname, secondary);
            *op_errstr = gf_strdup(GEOREP " is not set up");
            goto out;
        }

        if (type == GF_GSYNC_OPTION_TYPE_PAUSE) {
            ret = gd_pause_or_resume_gsync(dict, volname, secondary,
                                           secondary_host, secondary_vol,
                                           conf_path, op_errstr, _gf_true);
            if (ret)
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_PAUSE_FAILED,
                       GEOREP " Pause Failed");
            else
                dict_del(volinfo->gsync_active_secondaries, key);

        } else if (type == GF_GSYNC_OPTION_TYPE_RESUME) {
            /* Add secondary to the dict indicating geo-rep session is
             * running*/
            ret = dict_set_dynstr_with_alloc(volinfo->gsync_active_secondaries,
                                             key, "running");
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Unable to set "
                       "key:%s value:running in dict",
                       key);
                goto out;
            }

            ret = gd_pause_or_resume_gsync(dict, volname, secondary,
                                           secondary_host, secondary_vol,
                                           conf_path, op_errstr, _gf_false);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RESUME_FAILED,
                       GEOREP " Resume Failed");
                dict_del(volinfo->gsync_active_secondaries, key);
            }
        } else {
            ret = stop_gsync(volname, secondary, &status_msg, conf_path,
                             op_errstr, is_force);

            if (ret == 0 && status_msg)
                ret = dict_set_str(rsp_dict, "gsync-status", status_msg);
            if (!ret) {
                ret = glusterd_create_status_file(volinfo->volname, secondary,
                                                  secondary_host, secondary_vol,
                                                  "Stopped");
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_UPDATE_STATEFILE_FAILED,
                           "Unable to update state_file. "
                           "Error : %s",
                           strerror(errno));
                }
                dict_del(volinfo->gsync_active_secondaries, key);
            }
        }
    }

out:
    if (path_list) {
        GF_FREE(path_list);
        path_list = NULL;
    }

    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_get_secondary_details_confpath(glusterd_volinfo_t *volinfo,
                                        dict_t *dict, char **secondary_url,
                                        char **secondary_host,
                                        char **secondary_vol, char **conf_path,
                                        char **op_errstr)
{
    int ret = -1;
    char confpath[PATH_MAX] = "";
    glusterd_conf_t *priv = NULL;
    char *secondary = NULL;
    xlator_t *this = THIS;

    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_get_str(dict, "secondary", &secondary);
    if (ret || !secondary) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to fetch secondary from dict");
        ret = -1;
        goto out;
    }

    ret = glusterd_get_secondary_info(secondary, secondary_url, secondary_host,
                                      secondary_vol, op_errstr);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SECONDARYINFO_FETCH_ERROR,
               "Unable to fetch secondary details.");
        ret = -1;
        goto out;
    }

    ret = dict_set_str(dict, "secondary_url", *secondary_url);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to store secondary IP.");
        goto out;
    }

    ret = dict_set_str(dict, "secondary_host", *secondary_host);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to store secondary hostname");
        goto out;
    }

    ret = dict_set_str(dict, "secondary_vol", *secondary_vol);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to store secondary volume name.");
        goto out;
    }

    ret = snprintf(confpath, sizeof(confpath) - 1,
                   "%s/" GEOREP "/%s_%s_%s/gsyncd.conf", priv->workdir,
                   volinfo->volname, *secondary_host, *secondary_vol);
    confpath[ret] = '\0';
    *conf_path = gf_strdup(confpath);
    if (!(*conf_path)) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_STRDUP_FAILED,
               "Unable to gf_strdup. Error: %s", strerror(errno));
        ret = -1;
        goto out;
    }

    ret = dict_set_str(dict, "conf_path", *conf_path);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to store conf_path");
        goto out;
    }

out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_get_secondary_info(char *secondary, char **secondary_url,
                            char **hostname, char **secondary_vol,
                            char **op_errstr)
{
    char *tmp = NULL;
    char *save_ptr = NULL;
    char **linearr = NULL;
    int32_t ret = -1;
    char errmsg[PATH_MAX] = "";
    xlator_t *this = THIS;

    ret = glusterd_urltransform_single(secondary, "normalize", &linearr);
    if ((ret == -1) || (linearr[0] == NULL)) {
        ret = snprintf(errmsg, sizeof(errmsg) - 1, "Invalid Url: %s",
                       secondary);
        errmsg[ret] = '\0';
        *op_errstr = gf_strdup(errmsg);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_NORMALIZE_URL_FAIL,
               "Failed to normalize url");
        goto out;
    }

    tmp = strtok_r(linearr[0], "/", &save_ptr);
    tmp = strtok_r(NULL, "/", &save_ptr);
    secondary = NULL;
    if (tmp != NULL) {
        secondary = strtok_r(tmp, ":", &save_ptr);
    }
    if (secondary) {
        ret = glusterd_geo_rep_parse_secondary(secondary, hostname, op_errstr);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SECONDARY_URL_INVALID,
                   "Invalid secondary url: %s", *op_errstr);
            goto out;
        }
        gf_msg_debug(this->name, 0, "Hostname : %s", *hostname);

        *secondary_url = gf_strdup(secondary);
        if (!*secondary_url) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_STRDUP_FAILED,
                   "Failed to gf_strdup");
            ret = -1;
            goto out;
        }
        gf_msg_debug(this->name, 0, "Secondary URL : %s", *secondary_url);
        ret = 0;
    } else {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "Invalid secondary name");
        goto out;
    }

    secondary = strtok_r(NULL, ":", &save_ptr);
    if (secondary) {
        *secondary_vol = gf_strdup(secondary);
        if (!*secondary_vol) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_STRDUP_FAILED,
                   "Failed to gf_strdup");
            ret = -1;
            GF_FREE(*secondary_url);
            goto out;
        }
        gf_msg_debug(this->name, 0, "Secondary Vol : %s", *secondary_vol);
        ret = 0;
    } else {
        gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_INVALID_ENTRY,
               "Invalid secondary name");
        goto out;
    }

out:
    if (linearr)
        glusterd_urltransform_free(linearr, 1);
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

static void
runinit_gsyncd_setrx(runner_t *runner, char *conf_path)
{
    runinit(runner);
    runner_add_args(runner, GSYNCD_PREFIX "/gsyncd", "-c", NULL);
    runner_argprintf(runner, "%s", conf_path);
    runner_add_arg(runner, "--config-set-rx");
}

static int
glusterd_check_gsync_present(int *valid_state)
{
    char buff[PATH_MAX] = {
        0,
    };
    runner_t runner = {
        0,
    };
    char *ptr = NULL;
    int ret = 0;

    runinit(&runner);
    runner_add_args(&runner, GSYNCD_PREFIX "/gsyncd", "--version", NULL);
    runner_redir(&runner, STDOUT_FILENO, RUN_PIPE);
    ret = runner_start(&runner);
    if (ret == -1) {
        if (errno == ENOENT) {
            gf_msg("glusterd", GF_LOG_INFO, ENOENT, GD_MSG_MODULE_NOT_INSTALLED,
                   GEOREP
                   " module "
                   "not installed in the system");
            *valid_state = 0;
        } else {
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_MODULE_ERROR,
                   GEOREP " module not working as desired");
            *valid_state = -1;
        }
        goto out;
    }

    ptr = fgets(buff, sizeof(buff), runner_chio(&runner, STDOUT_FILENO));
    if (ptr) {
        if (!strstr(buff, "gsyncd")) {
            ret = -1;
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_MODULE_ERROR,
                   GEOREP " module not working as desired");
            *valid_state = -1;
            goto out;
        }
    } else {
        ret = -1;
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_MODULE_ERROR,
               GEOREP " module not working as desired");
        *valid_state = -1;
        goto out;
    }

    ret = 0;
out:

    runner_end(&runner);

    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

static int
create_conf_file(glusterd_conf_t *conf, char *conf_path)
#define RUN_GSYNCD_CMD                                                         \
    do {                                                                       \
        ret = runner_run_reuse(&runner);                                       \
        if (ret == -1) {                                                       \
            runner_log(&runner, "glusterd", GF_LOG_ERROR, "command failed");   \
            runner_end(&runner);                                               \
            goto out;                                                          \
        }                                                                      \
        runner_end(&runner);                                                   \
    } while (0)
{
    int ret = 0;
    runner_t runner = {
        0,
    };
    char georepdir[PATH_MAX] = {
        0,
    };
    int valid_state = 0;

    valid_state = -1;
    ret = glusterd_check_gsync_present(&valid_state);
    if (-1 == ret) {
        ret = valid_state;
        goto out;
    }

    ret = snprintf(georepdir, sizeof(georepdir) - 1, "%s/" GEOREP,
                   conf->workdir);
    georepdir[ret] = '\0';

    /************
     * primary pre-configuration
     ************/

    /* remote-gsyncd */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_args(&runner, "remote-gsyncd", GSYNCD_PREFIX "/gsyncd", ".", ".",
                    NULL);
    RUN_GSYNCD_CMD;

    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_args(&runner, "remote-gsyncd", "/nonexistent/gsyncd", ".",
                    "^ssh:", NULL);
    RUN_GSYNCD_CMD;

    /* gluster-command-dir */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_args(&runner, "gluster-command-dir", SBIN_DIR "/", ".", ".",
                    NULL);
    RUN_GSYNCD_CMD;

    /* gluster-params */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_args(&runner, "gluster-params", "aux-gfid-mount acl", ".", ".",
                    NULL);
    RUN_GSYNCD_CMD;

    /* ssh-command */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_arg(&runner, "ssh-command");
    runner_argprintf(&runner,
                     "ssh -oPasswordAuthentication=no "
                     "-oStrictHostKeyChecking=no "
                     "-i %s/secret.pem",
                     georepdir);
    runner_add_args(&runner, ".", ".", NULL);
    RUN_GSYNCD_CMD;

    /* ssh-command tar */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_arg(&runner, "ssh-command-tar");
    runner_argprintf(&runner,
                     "ssh -oPasswordAuthentication=no "
                     "-oStrictHostKeyChecking=no "
                     "-i %s/tar_ssh.pem",
                     georepdir);
    runner_add_args(&runner, ".", ".", NULL);
    RUN_GSYNCD_CMD;

    /* pid-file */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_arg(&runner, "pid-file");
    runner_argprintf(
        &runner, "%s/${primaryvol}_${remotehost}_${secondaryvol}/monitor.pid",
        georepdir);
    runner_add_args(&runner, ".", ".", NULL);
    RUN_GSYNCD_CMD;

    /* geo-rep-working-dir */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_arg(&runner, "georep-session-working-dir");
    runner_argprintf(&runner, "%s/${primaryvol}_${remotehost}_${secondaryvol}/",
                     georepdir);
    runner_add_args(&runner, ".", ".", NULL);
    RUN_GSYNCD_CMD;

    /* state-file */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_arg(&runner, "state-file");
    runner_argprintf(
        &runner,
        "%s/${primaryvol}_${remotehost}_${secondaryvol}/monitor.status",
        georepdir);
    runner_add_args(&runner, ".", ".", NULL);
    RUN_GSYNCD_CMD;

    /* state-detail-file */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_arg(&runner, "state-detail-file");
    runner_argprintf(&runner,
                     "%s/${primaryvol}_${remotehost}_${secondaryvol}/"
                     "${eSecondary}-detail.status",
                     georepdir);
    runner_add_args(&runner, ".", ".", NULL);
    RUN_GSYNCD_CMD;

    /* state-socket */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_arg(&runner, "state-socket-unencoded");
    runner_argprintf(
        &runner,
        "%s/${primaryvol}_${remotehost}_${secondaryvol}/${eSecondary}.socket",
        georepdir);
    runner_add_args(&runner, ".", ".", NULL);
    RUN_GSYNCD_CMD;

    /* socketdir */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_args(&runner, "socketdir", GLUSTERD_SOCK_DIR, ".", ".", NULL);
    RUN_GSYNCD_CMD;

    /* log-file */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_arg(&runner, "log-file");
    runner_argprintf(&runner, "%s/%s/${primaryvol}/${eSecondary}.log",
                     conf->logdir, GEOREP);
    runner_add_args(&runner, ".", ".", NULL);
    RUN_GSYNCD_CMD;

    /* changelog-log-file */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_arg(&runner, "changelog-log-file");
    runner_argprintf(&runner,
                     "%s/%s/${primaryvol}/${eSecondary}${local_id}-changes.log",
                     conf->logdir, GEOREP);
    runner_add_args(&runner, ".", ".", NULL);
    RUN_GSYNCD_CMD;

    /* gluster-log-file */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_arg(&runner, "gluster-log-file");
    runner_argprintf(&runner,
                     "%s/%s/${primaryvol}/${eSecondary}${local_id}.gluster.log",
                     conf->logdir, GEOREP);
    runner_add_args(&runner, ".", ".", NULL);
    RUN_GSYNCD_CMD;

    /* ignore-deletes */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_args(&runner, "ignore-deletes", "false", ".", ".", NULL);
    RUN_GSYNCD_CMD;

    /* special-sync-mode */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_args(&runner, "special-sync-mode", "partial", ".", ".", NULL);
    RUN_GSYNCD_CMD;

    /* change-detector == changelog */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_args(&runner, "change-detector", "changelog", ".", ".", NULL);
    RUN_GSYNCD_CMD;

    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_arg(&runner, "working-dir");
    runner_argprintf(&runner, "%s/${primaryvol}/${eSecondary}",
                     DEFAULT_GLUSTERFSD_MISC_DIRETORY);
    runner_add_args(&runner, ".", ".", NULL);
    RUN_GSYNCD_CMD;

    /************
     * secondary pre-configuration
     ************/

    /* secondary-gluster-command-dir */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_args(&runner, "secondary-gluster-command-dir", SBIN_DIR "/", ".",
                    NULL);
    RUN_GSYNCD_CMD;

    /* gluster-params */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_args(&runner, "gluster-params", "aux-gfid-mount acl", ".", NULL);
    RUN_GSYNCD_CMD;

    /* log-file */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_arg(&runner, "log-file");
    runner_argprintf(
        &runner,
        "%s/%s-secondaries/"
        "${session_owner}:${local_node}${local_id}.${secondaryvol}."
        "log",
        conf->logdir, GEOREP);
    runner_add_args(&runner, ".", ".", NULL);
    RUN_GSYNCD_CMD;

    /* MountBroker log-file */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_arg(&runner, "log-file-mbr");
    runner_argprintf(
        &runner,
        "%s/%s-secondaries/mbr/"
        "${session_owner}:${local_node}${local_id}.${secondaryvol}."
        "log",
        conf->logdir, GEOREP);
    runner_add_args(&runner, ".", ".", NULL);
    RUN_GSYNCD_CMD;

    /* gluster-log-file */
    runinit_gsyncd_setrx(&runner, conf_path);
    runner_add_arg(&runner, "gluster-log-file");
    runner_argprintf(
        &runner,
        "%s/%s-secondaries/"
        "${session_owner}:${local_node}${local_id}.${secondaryvol}."
        "gluster.log",
        conf->logdir, GEOREP);
    runner_add_args(&runner, ".", ".", NULL);
    RUN_GSYNCD_CMD;

out:
    return ret ? -1 : 0;
}

static int
glusterd_create_essential_dir_files(glusterd_volinfo_t *volinfo, dict_t *dict,
                                    char *secondary, char *secondary_host,
                                    char *secondary_vol, char **op_errstr)
{
    int ret = -1;
    char *conf_path = NULL;
    char *statefile = NULL;
    char buf[PATH_MAX] = "";
    char errmsg[PATH_MAX] = "";
    glusterd_conf_t *conf = NULL;
    struct stat stbuf = {
        0,
    };
    xlator_t *this = THIS;
    int32_t len = 0;

    conf = this->private;

    ret = dict_get_str(dict, "conf_path", &conf_path);
    if (ret) {
        snprintf(errmsg, sizeof(errmsg), "Unable to fetch conf file path.");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               errmsg);
        goto out;
    }

    ret = dict_get_str(dict, "statefile", &statefile);
    if (ret) {
        snprintf(errmsg, sizeof(errmsg), "Unable to fetch statefile path.");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               errmsg);
        goto out;
    }

    ret = snprintf(buf, sizeof(buf), "%s/" GEOREP "/%s_%s_%s", conf->workdir,
                   volinfo->volname, secondary_host, secondary_vol);
    if ((ret < 0) || (ret >= sizeof(buf))) {
        ret = -1;
        goto out;
    }
    ret = mkdir_p(buf, 0755, _gf_true);
    if (ret) {
        len = snprintf(errmsg, sizeof(errmsg),
                       "Unable to create %s"
                       ". Error : %s",
                       buf, strerror(errno));
        if (len < 0) {
            strcpy(errmsg, "<error>");
        }
        *op_errstr = gf_strdup(errmsg);
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED, "%s",
               errmsg);
        goto out;
    }

    ret = snprintf(buf, PATH_MAX, "%s/" GEOREP "/%s", conf->logdir,
                   volinfo->volname);
    if ((ret < 0) || (ret >= PATH_MAX)) {
        ret = -1;
        goto out;
    }
    ret = mkdir_p(buf, 0755, _gf_true);
    if (ret) {
        len = snprintf(errmsg, sizeof(errmsg),
                       "Unable to create %s"
                       ". Error : %s",
                       buf, strerror(errno));
        if (len < 0) {
            strcpy(errmsg, "<error>");
        }
        *op_errstr = gf_strdup(errmsg);
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED, "%s",
               errmsg);
        goto out;
    }

    ret = sys_lstat(conf_path, &stbuf);
    if (!ret) {
        gf_msg_debug(this->name, 0,
                     "Session already running."
                     " Not creating config file again.");
    } else {
        ret = create_conf_file(conf, conf_path);
        if (ret || sys_lstat(conf_path, &stbuf)) {
            snprintf(errmsg, sizeof(errmsg),
                     "Failed to create"
                     " config file(%s).",
                     conf_path);
            gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED, "%s",
                   errmsg);
            goto out;
        }
    }

    ret = sys_lstat(statefile, &stbuf);
    if (!ret) {
        gf_msg_debug(this->name, 0,
                     "Session already running."
                     " Not creating status file again.");
        goto out;
    } else {
        ret = glusterd_create_status_file(volinfo->volname, secondary,
                                          secondary_host, secondary_vol,
                                          "Created");
        if (ret || sys_lstat(statefile, &stbuf)) {
            snprintf(errmsg, sizeof(errmsg),
                     "Unable to create %s"
                     ". Error : %s",
                     statefile, strerror(errno));
            *op_errstr = gf_strdup(errmsg);
            gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED, "%s",
                   errmsg);
            ret = -1;
            goto out;
        }
    }

out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_op_gsync_create(dict_t *dict, char **op_errstr, dict_t *rsp_dict)
{
    char common_pem_file[PATH_MAX] = "";
    char errmsg[PATH_MAX] = {
        0,
    };
    char hooks_args[PATH_MAX] = "";
    char uuid_str[64] = "";
    char *host_uuid = NULL;
    char *secondary_url = NULL;
    char *secondary_url_buf = NULL;
    char *secondary_user = NULL;
    char *secondary_ip = NULL;
    char *save_ptr = NULL;
    char *secondary_host = NULL;
    char *secondary_vol = NULL;
    char *arg_buf = NULL;
    char *volname = NULL;
    char *secondary = NULL;
    int32_t ret = -1;
    int32_t is_pem_push = -1;
    int32_t ssh_port = 22;
    gf_boolean_t is_force = -1;
    glusterd_conf_t *conf = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = THIS;
    char old_working_dir[PATH_MAX] = {0};
    char new_working_dir[PATH_MAX] = {0};
    char *secondary_voluuid = NULL;
    char *old_secondaryhost = NULL;
    gf_boolean_t is_existing_session = _gf_false;
    int32_t len = 0;

    conf = this->private;
    GF_ASSERT(conf);
    GF_ASSERT(dict);
    GF_ASSERT(op_errstr);

    ret = glusterd_op_gsync_args_get(dict, op_errstr, &volname, &secondary,
                                     &host_uuid);
    if (ret)
        goto out;

    len = snprintf(common_pem_file, sizeof(common_pem_file),
                   "%s" GLUSTERD_COMMON_PEM_PUB_FILE, conf->workdir);
    if ((len < 0) || (len >= sizeof(common_pem_file))) {
        ret = -1;
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
               "Volinfo for %s (primary) not found", volname);
        goto out;
    }

    ret = dict_get_str(dict, "secondary_vol", &secondary_vol);
    if (ret) {
        snprintf(errmsg, sizeof(errmsg),
                 "Unable to fetch secondary volume name.");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               errmsg);
        goto out;
    }

    ret = dict_get_str(dict, "secondary_url", &secondary_url);
    if (ret) {
        snprintf(errmsg, sizeof(errmsg), "Unable to fetch secondary IP.");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               errmsg);
        ret = -1;
        goto out;
    }

    /* Fetch the secondary_user and secondary_ip from the secondary_url.
     * If the secondary_user is not present. Use "root"
     */
    if (strstr(secondary_url, "@")) {
        secondary_url_buf = gf_strdup(secondary_url);
        if (!secondary_url_buf) {
            ret = -1;
            goto out;
        }
        secondary_user = strtok_r(secondary_url, "@", &save_ptr);
        secondary_ip = strtok_r(NULL, "@", &save_ptr);
    } else {
        secondary_user = "root";
        secondary_ip = secondary_url;
    }

    if (!secondary_user || !secondary_ip) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SECONDARY_URL_INVALID,
               "Invalid secondary url.");
        ret = -1;
        goto out;
    }

    ret = dict_get_str(dict, "secondary_host", &secondary_host);
    if (ret) {
        snprintf(errmsg, sizeof(errmsg), "Unable to fetch secondary host");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               errmsg);
        ret = -1;
        goto out;
    }

    ret = dict_get_int32(dict, "ssh_port", &ssh_port);
    if (ret < 0 && ret != -ENOENT) {
        snprintf(errmsg, sizeof(errmsg), "Fetching ssh_port failed");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               errmsg);
        ret = -1;
        goto out;
    }

    is_force = dict_get_str_boolean(dict, "force", _gf_false);

    uuid_utoa_r(MY_UUID, uuid_str);
    if (!strcmp(uuid_str, host_uuid)) {
        ret = dict_get_int32(dict, "push_pem", &is_pem_push);
        if (!ret && is_pem_push) {
            gf_msg_debug(this->name, 0,
                         "Trying to setup"
                         " pem files in secondary");
            is_pem_push = 1;
        } else
            is_pem_push = 0;

        len = snprintf(hooks_args, sizeof(hooks_args),
                       "is_push_pem=%d,pub_file=%s,secondary_user=%s,"
                       "secondary_ip=%s,secondary_vol=%s,ssh_port=%d",
                       is_pem_push, common_pem_file, secondary_user,
                       secondary_ip, secondary_vol, ssh_port);
        if ((len < 0) || (len >= sizeof(hooks_args))) {
            ret = -1;
            goto out;
        }
    } else
        snprintf(hooks_args, sizeof(hooks_args),
                 "This argument will stop the hooks script");

    arg_buf = gf_strdup(hooks_args);
    if (!arg_buf) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_STRDUP_FAILED,
               "Failed to gf_strdup");
        if (is_force) {
            ret = 0;
            goto create_essentials;
        }
        ret = -1;
        goto out;
    }

    ret = dict_set_str(dict, "hooks_args", arg_buf);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set hooks_args in dict.");
        if (is_force) {
            ret = 0;
            goto create_essentials;
        }
        goto out;
    }

create_essentials:
    /* Fetch secondary volume uuid, to get stored in volume info. */
    ret = dict_get_str(dict, "secondary_voluuid", &secondary_voluuid);
    if (ret) {
        snprintf(errmsg, sizeof(errmsg),
                 "Unable to fetch secondary volume uuid from dict");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
               errmsg);
        ret = -1;
        goto out;
    }

    is_existing_session = dict_get_str_boolean(dict, "existing_session",
                                               _gf_false);
    if (is_existing_session) {
        ret = dict_get_str(dict, "old_secondaryhost", &old_secondaryhost);
        if (ret) {
            snprintf(errmsg, sizeof(errmsg),
                     "Unable to fetch old_secondaryhost");
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "%s",
                   errmsg);
            ret = -1;
            goto out;
        }

        /* Rename existing geo-rep session with new Secondary Host */
        ret = snprintf(old_working_dir, sizeof(old_working_dir) - 1,
                       "%s/" GEOREP "/%s_%s_%s", conf->workdir,
                       volinfo->volname, old_secondaryhost, secondary_vol);

        ret = snprintf(new_working_dir, sizeof(new_working_dir) - 1,
                       "%s/" GEOREP "/%s_%s_%s", conf->workdir,
                       volinfo->volname, secondary_host, secondary_vol);

        ret = sys_rename(old_working_dir, new_working_dir);
        if (!ret) {
            gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_FORCE_CREATE_SESSION,
                   "rename of old working dir %s to "
                   "new working dir %s is done! ",
                   old_working_dir, new_working_dir);
        } else {
            if (errno == ENOENT) {
                /* log error, but proceed with directory
                 * creation below */
                gf_msg_debug(this->name, 0,
                             "old_working_dir(%s) "
                             "not present.",
                             old_working_dir);
            } else {
                len = snprintf(errmsg, sizeof(errmsg),
                               "rename of old working dir %s "
                               "to new working dir %s "
                               "failed! Error: %s",
                               old_working_dir, new_working_dir,
                               strerror(errno));
                if (len < 0) {
                    strcpy(errmsg, "<error>");
                }
                gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_FORCE_CREATE_SESSION,
                       "rename of old working dir %s to "
                       "new working dir %s failed! Error: %s!",
                       old_working_dir, new_working_dir, strerror(errno));

                ret = -1;
                goto out;
            }
        }
    }

    ret = glusterd_create_essential_dir_files(
        volinfo, dict, secondary, secondary_host, secondary_vol, op_errstr);
    if (ret)
        goto out;

    ret = glusterd_store_secondary_in_info(
        volinfo, secondary, host_uuid, secondary_voluuid, op_errstr, is_force);
    if (ret) {
        snprintf(errmsg, sizeof(errmsg),
                 "Unable to store"
                 " secondary info.");
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SECONDARYINFO_STORE_ERROR,
               "%s", errmsg);
        goto out;
    }

    /* Enable marker and changelog */
    ret = glusterd_set_gsync_confs(volinfo);
    if (ret != 0) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_MARKER_START_FAIL,
               "marker/changelog"
               " start failed");
        snprintf(errmsg, sizeof(errmsg), "Index initialization failed");

        ret = -1;
        goto out;
    }

out:
    if (ret && errmsg[0] != '\0') {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GSYNCD_ERROR, "%s", errmsg);
        *op_errstr = gf_strdup(errmsg);
    }

    GF_FREE(secondary_url_buf);
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}
