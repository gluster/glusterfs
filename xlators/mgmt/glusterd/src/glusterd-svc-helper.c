/*
   Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <signal.h>

#include <glusterfs/globals.h>
#include <glusterfs/run.h>
#include "glusterd.h"
#include <glusterfs/glusterfs.h>
#include "glusterd-utils.h"
#include "glusterd-svc-mgmt.h"
#include "glusterd-shd-svc.h"
#include "glusterd-quotad-svc.h"
#include "glusterd-nfs-svc.h"
#include "glusterd-bitd-svc.h"
#include "glusterd-tierd-svc.h"
#include "glusterd-tierd-svc-helper.h"
#include "glusterd-shd-svc-helper.h"
#include "glusterd-scrub-svc.h"
#include "glusterd-svc-helper.h"
#include <glusterfs/syscall.h>
#include "glusterd-snapshot-utils.h"

int
glusterd_svcs_reconfigure(glusterd_volinfo_t *volinfo)
{
    int ret = 0;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    char *svc_name = NULL;

    GF_ASSERT(this);

    conf = this->private;
    GF_ASSERT(conf);

    svc_name = "nfs";
    ret = glusterd_nfssvc_reconfigure();
    if (ret)
        goto out;

    svc_name = "self-heald";
    if (volinfo) {
        ret = glusterd_shdsvc_reconfigure(volinfo);
        if (ret)
            goto out;
    }

    if (conf->op_version == GD_OP_VERSION_MIN)
        goto out;

    svc_name = "quotad";
    ret = glusterd_quotadsvc_reconfigure();
    if (ret)
        goto out;

    svc_name = "bitd";
    ret = glusterd_bitdsvc_reconfigure();
    if (ret)
        goto out;

    svc_name = "scrubber";
    ret = glusterd_scrubsvc_reconfigure();
out:
    if (ret && svc_name)
        gf_event(EVENT_SVC_RECONFIGURE_FAILED, "svc_name=%s", svc_name);
    return ret;
}

int
glusterd_svcs_stop(glusterd_volinfo_t *volinfo)
{
    int ret = 0;
    xlator_t *this = NULL;
    glusterd_conf_t *priv = NULL;

    this = THIS;
    GF_ASSERT(this);

    priv = this->private;
    GF_ASSERT(priv);

    ret = glusterd_svc_stop(&(priv->nfs_svc), SIGKILL);
    if (ret)
        goto out;

    ret = glusterd_svc_stop(&(priv->quotad_svc), SIGTERM);
    if (ret)
        goto out;

    if (volinfo) {
        ret = glusterd_svc_stop(&(volinfo->shd.svc), PROC_START_NO_WAIT);
        if (ret)
            goto out;
    }

    ret = glusterd_svc_stop(&(priv->bitd_svc), SIGTERM);
    if (ret)
        goto out;

    ret = glusterd_svc_stop(&(priv->scrub_svc), SIGTERM);
out:
    return ret;
}

int
glusterd_svcs_manager(glusterd_volinfo_t *volinfo)
{
    int ret = 0;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;

    GF_ASSERT(this);

    conf = this->private;
    GF_ASSERT(conf);

    if (volinfo && volinfo->is_snap_volume)
        return 0;

    ret = conf->nfs_svc.manager(&(conf->nfs_svc), NULL, PROC_START_NO_WAIT);
    if (ret)
        goto out;

    if (conf->op_version == GD_OP_VERSION_MIN)
        goto out;

    ret = conf->quotad_svc.manager(&(conf->quotad_svc), volinfo,
                                   PROC_START_NO_WAIT);
    if (ret == -EINVAL)
        ret = 0;
    if (ret)
        goto out;

    ret = conf->bitd_svc.manager(&(conf->bitd_svc), NULL, PROC_START_NO_WAIT);
    if (ret == -EINVAL)
        ret = 0;
    if (ret)
        goto out;

    if (volinfo) {
        ret = volinfo->shd.svc.manager(&(volinfo->shd.svc), volinfo,
                                       PROC_START_NO_WAIT);
        if (ret == -EINVAL)
            ret = 0;
        if (ret)
            goto out;
    }

    ret = conf->scrub_svc.manager(&(conf->scrub_svc), NULL, PROC_START_NO_WAIT);
    if (ret == -EINVAL)
        ret = 0;
out:
    return ret;
}

int
glusterd_svc_check_volfile_identical(char *svc_name,
                                     glusterd_graph_builder_t builder,
                                     gf_boolean_t *identical)
{
    char orgvol[PATH_MAX] = {
        0,
    };
    char *tmpvol = NULL;
    glusterd_conf_t *conf = NULL;
    xlator_t *this = NULL;
    int ret = -1;
    int need_unlink = 0;
    int tmp_fd = -1;

    this = THIS;

    GF_ASSERT(this);
    GF_ASSERT(identical);
    conf = this->private;

    glusterd_svc_build_volfile_path(svc_name, conf->workdir, orgvol,
                                    sizeof(orgvol));

    ret = gf_asprintf(&tmpvol, "/tmp/g%s-XXXXXX", svc_name);
    if (ret < 0) {
        goto out;
    }

    /* coverity[secure_temp] mkstemp uses 0600 as the mode and is safe */
    tmp_fd = mkstemp(tmpvol);
    if (tmp_fd < 0) {
        gf_msg(this->name, GF_LOG_WARNING, errno, GD_MSG_FILE_OP_FAILED,
               "Unable to create temp file"
               " %s:(%s)",
               tmpvol, strerror(errno));
        ret = -1;
        goto out;
    }

    need_unlink = 1;

    ret = glusterd_create_global_volfile(builder, tmpvol, NULL);
    if (ret)
        goto out;

    ret = glusterd_check_files_identical(orgvol, tmpvol, identical);
out:
    if (need_unlink)
        sys_unlink(tmpvol);

    if (tmpvol != NULL)
        GF_FREE(tmpvol);

    if (tmp_fd >= 0)
        sys_close(tmp_fd);

    return ret;
}

int
glusterd_svc_check_topology_identical(char *svc_name,
                                      glusterd_graph_builder_t builder,
                                      gf_boolean_t *identical)
{
    char orgvol[PATH_MAX] = {
        0,
    };
    char *tmpvol = NULL;
    glusterd_conf_t *conf = NULL;
    xlator_t *this = THIS;
    int ret = -1;
    int tmpclean = 0;
    int tmpfd = -1;

    if ((!identical) || (!this) || (!this->private))
        goto out;

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    /* Fetch the original volfile */
    glusterd_svc_build_volfile_path(svc_name, conf->workdir, orgvol,
                                    sizeof(orgvol));

    /* Create the temporary volfile */
    ret = gf_asprintf(&tmpvol, "/tmp/g%s-XXXXXX", svc_name);
    if (ret < 0) {
        goto out;
    }

    /* coverity[secure_temp] mkstemp uses 0600 as the mode and is safe */
    tmpfd = mkstemp(tmpvol);
    if (tmpfd < 0) {
        gf_msg(this->name, GF_LOG_WARNING, errno, GD_MSG_FILE_OP_FAILED,
               "Unable to create temp file"
               " %s:(%s)",
               tmpvol, strerror(errno));
        ret = -1;
        goto out;
    }

    tmpclean = 1; /* SET the flag to unlink() tmpfile */

    ret = glusterd_create_global_volfile(builder, tmpvol, NULL);
    if (ret)
        goto out;

    /* Compare the topology of volfiles */
    ret = glusterd_check_topology_identical(orgvol, tmpvol, identical);
out:
    if (tmpfd >= 0)
        sys_close(tmpfd);
    if (tmpclean)
        sys_unlink(tmpvol);
    if (tmpvol != NULL)
        GF_FREE(tmpvol);
    return ret;
}

int
glusterd_volume_svc_check_volfile_identical(
    char *svc_name, dict_t *mode_dict, glusterd_volinfo_t *volinfo,
    glusterd_vol_graph_builder_t builder, gf_boolean_t *identical)
{
    char orgvol[PATH_MAX] = {
        0,
    };
    char *tmpvol = NULL;
    xlator_t *this = NULL;
    int ret = -1;
    int need_unlink = 0;
    int tmp_fd = -1;

    this = THIS;

    GF_VALIDATE_OR_GOTO(this->name, this, out);
    GF_VALIDATE_OR_GOTO(this->name, identical, out);

    /* This builds volfile for volume level dameons */
    glusterd_volume_svc_build_volfile_path(svc_name, volinfo, orgvol,
                                           sizeof(orgvol));

    ret = gf_asprintf(&tmpvol, "/tmp/g%s-XXXXXX", svc_name);
    if (ret < 0) {
        goto out;
    }

    /* coverity[secure_temp] mkstemp uses 0600 as the mode and is safe */
    tmp_fd = mkstemp(tmpvol);
    if (tmp_fd < 0) {
        gf_msg(this->name, GF_LOG_WARNING, errno, GD_MSG_FILE_OP_FAILED,
               "Unable to create temp file"
               " %s:(%s)",
               tmpvol, strerror(errno));
        ret = -1;
        goto out;
    }

    need_unlink = 1;

    ret = builder(volinfo, tmpvol, mode_dict);
    if (ret)
        goto out;

    ret = glusterd_check_files_identical(orgvol, tmpvol, identical);
out:
    if (need_unlink)
        sys_unlink(tmpvol);

    if (tmpvol != NULL)
        GF_FREE(tmpvol);

    if (tmp_fd >= 0)
        sys_close(tmp_fd);

    return ret;
}

int
glusterd_volume_svc_check_topology_identical(
    char *svc_name, dict_t *mode_dict, glusterd_volinfo_t *volinfo,
    glusterd_vol_graph_builder_t builder, gf_boolean_t *identical)
{
    char orgvol[PATH_MAX] = {
        0,
    };
    char *tmpvol = NULL;
    glusterd_conf_t *conf = NULL;
    xlator_t *this = THIS;
    int ret = -1;
    int tmpclean = 0;
    int tmpfd = -1;

    if ((!identical) || (!this) || (!this->private))
        goto out;

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    /* This builds volfile for volume level dameons */
    glusterd_volume_svc_build_volfile_path(svc_name, volinfo, orgvol,
                                           sizeof(orgvol));
    /* Create the temporary volfile */
    ret = gf_asprintf(&tmpvol, "/tmp/g%s-XXXXXX", svc_name);
    if (ret < 0) {
        goto out;
    }

    /* coverity[secure_temp] mkstemp uses 0600 as the mode and is safe */
    tmpfd = mkstemp(tmpvol);
    if (tmpfd < 0) {
        gf_msg(this->name, GF_LOG_WARNING, errno, GD_MSG_FILE_OP_FAILED,
               "Unable to create temp file"
               " %s:(%s)",
               tmpvol, strerror(errno));
        ret = -1;
        goto out;
    }

    tmpclean = 1; /* SET the flag to unlink() tmpfile */

    ret = builder(volinfo, tmpvol, mode_dict);
    if (ret)
        goto out;

    /* Compare the topology of volfiles */
    ret = glusterd_check_topology_identical(orgvol, tmpvol, identical);
out:
    if (tmpfd >= 0)
        sys_close(tmpfd);
    if (tmpclean)
        sys_unlink(tmpvol);
    if (tmpvol != NULL)
        GF_FREE(tmpvol);
    return ret;
}

void *
__gf_find_compatible_svc(gd_node_type daemon)
{
    glusterd_svc_proc_t *svc_proc = NULL;
    glusterd_svc_proc_t *return_proc = NULL;
    glusterd_svc_t *parent_svc = NULL;
    struct cds_list_head *svc_procs = NULL;
    glusterd_conf_t *conf = NULL;
    int pid = -1;

    conf = THIS->private;
    GF_VALIDATE_OR_GOTO("glusterd", conf, out);

    switch (daemon) {
        case GD_NODE_SHD: {
            svc_procs = &conf->shd_procs;
            if (!svc_procs)
                goto out;
        } break;
        default:
            /* Add support for other client daemons here */
            goto out;
    }

    cds_list_for_each_entry(svc_proc, svc_procs, svc_proc_list)
    {
        parent_svc = cds_list_entry(svc_proc->svcs.next, glusterd_svc_t,
                                    mux_svc);
        if (!return_proc)
            return_proc = svc_proc;

        /* If there is an  already running shd daemons, select it. Otehrwise
         * select the first one.
         */
        if (parent_svc && gf_is_service_running(parent_svc->proc.pidfile, &pid))
            return (void *)svc_proc;
        /*
         * Logic to select one process goes here. Currently there is only one
         * shd_proc. So selecting the first one;
         */
    }
out:
    return return_proc;
}

glusterd_svc_proc_t *
glusterd_svcprocess_new()
{
    glusterd_svc_proc_t *new_svcprocess = NULL;

    new_svcprocess = GF_CALLOC(1, sizeof(*new_svcprocess),
                               gf_gld_mt_glusterd_svc_proc_t);

    if (!new_svcprocess)
        return NULL;

    CDS_INIT_LIST_HEAD(&new_svcprocess->svc_proc_list);
    CDS_INIT_LIST_HEAD(&new_svcprocess->svcs);
    new_svcprocess->notify = glusterd_muxsvc_common_rpc_notify;
    return new_svcprocess;
}

int
glusterd_shd_svc_mux_init(glusterd_volinfo_t *volinfo, glusterd_svc_t *svc)
{
    int ret = -1;
    glusterd_svc_proc_t *mux_proc = NULL;
    glusterd_conn_t *mux_conn = NULL;
    glusterd_conf_t *conf = NULL;
    glusterd_svc_t *parent_svc = NULL;
    int pid = -1;

    GF_VALIDATE_OR_GOTO("glusterd", svc, out);
    GF_VALIDATE_OR_GOTO("glusterd", volinfo, out);
    conf = THIS->private;
    GF_VALIDATE_OR_GOTO("glusterd", conf, out);
    GF_VALIDATE_OR_GOTO("glusterd", svc, out);

    pthread_mutex_lock(&conf->attach_lock);
    {
        if (!svc->inited) {
            if (gf_is_service_running(svc->proc.pidfile, &pid)) {
                /* Just connect is required, but we don't know what happens
                 * during the disconnect. So better to reattach.
                 */
                mux_proc = __gf_find_compatible_svc_from_pid(GD_NODE_SHD, pid);
            }

            if (!mux_proc) {
                if (pid != -1 && sys_access(svc->proc.pidfile, R_OK) == 0) {
                    /* stale pid file, unlink it. */
                    kill(pid, SIGTERM);
                    sys_unlink(svc->proc.pidfile);
                }
                mux_proc = __gf_find_compatible_svc(GD_NODE_SHD);
            }
            if (mux_proc) {
                /* Take first entry from the process */
                parent_svc = cds_list_entry(mux_proc->svcs.next, glusterd_svc_t,
                                            mux_svc);
                sys_link(parent_svc->proc.pidfile, svc->proc.pidfile);
                mux_conn = &parent_svc->conn;
                if (volinfo)
                    volinfo->shd.attached = _gf_true;
            } else {
                mux_proc = glusterd_svcprocess_new();
                if (!mux_proc) {
                    ret = -1;
                    goto unlock;
                }
                cds_list_add_tail(&mux_proc->svc_proc_list, &conf->shd_procs);
            }
            svc->svc_proc = mux_proc;
            cds_list_del_init(&svc->mux_svc);
            cds_list_add_tail(&svc->mux_svc, &mux_proc->svcs);
            ret = glusterd_shdsvc_init(volinfo, mux_conn, mux_proc);
            if (ret) {
                pthread_mutex_unlock(&conf->attach_lock);
                gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_FAILED_INIT_SHDSVC,
                       "Failed to init shd "
                       "service");
                goto out;
            }
            gf_msg_debug(THIS->name, 0, "shd service initialized");
            svc->inited = _gf_true;
        }
        ret = 0;
    }
unlock:
    pthread_mutex_unlock(&conf->attach_lock);
out:
    return ret;
}

void *
__gf_find_compatible_svc_from_pid(gd_node_type daemon, pid_t pid)
{
    glusterd_svc_proc_t *svc_proc = NULL;
    struct cds_list_head *svc_procs = NULL;
    glusterd_svc_t *svc = NULL;
    pid_t mux_pid = -1;
    glusterd_conf_t *conf = NULL;

    conf = THIS->private;
    if (!conf)
        return NULL;

    switch (daemon) {
        case GD_NODE_SHD: {
            svc_procs = &conf->shd_procs;
            if (!svc_procs)
                return NULL;
        } break;
        default:
            /* Add support for other client daemons here */
            return NULL;
    }

    cds_list_for_each_entry(svc_proc, svc_procs, svc_proc_list)
    {
        cds_list_for_each_entry(svc, &svc_proc->svcs, mux_svc)
        {
            if (gf_is_service_running(svc->proc.pidfile, &mux_pid)) {
                if (mux_pid == pid) {
                    /*TODO
                     * inefficient loop, but at the moment, there is only
                     * one shd.
                     */
                    return svc_proc;
                }
            }
        }
    }
    return NULL;
}

static int32_t
my_callback(struct rpc_req *req, struct iovec *iov, int count, void *v_frame)
{
    call_frame_t *frame = v_frame;
    xlator_t *this = NULL;
    glusterd_conf_t *conf = NULL;

    GF_VALIDATE_OR_GOTO("glusterd", frame, out);
    this = frame->this;
    GF_VALIDATE_OR_GOTO("glusterd", this, out);
    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    GF_ATOMIC_DEC(conf->blockers);

    STACK_DESTROY(frame->root);
out:
    return 0;
}

static int32_t
glusterd_svc_attach_cbk(struct rpc_req *req, struct iovec *iov, int count,
                        void *v_frame)
{
    call_frame_t *frame = v_frame;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_shdsvc_t *shd = NULL;
    glusterd_svc_t *svc = frame->cookie;
    glusterd_svc_t *parent_svc = NULL;
    glusterd_svc_proc_t *mux_proc = NULL;
    glusterd_conf_t *conf = NULL;
    int *flag = (int *)frame->local;
    xlator_t *this = THIS;
    int pid = -1;
    int ret = -1;
    gf_getspec_rsp rsp = {
        0,
    };

    GF_VALIDATE_OR_GOTO("glusterd", this, out);
    conf = this->private;
    GF_VALIDATE_OR_GOTO("glusterd", conf, out);
    GF_VALIDATE_OR_GOTO("glusterd", frame, out);
    GF_VALIDATE_OR_GOTO("glusterd", svc, out);

    frame->local = NULL;
    frame->cookie = NULL;

    if (!strcmp(svc->name, "glustershd")) {
        /* Get volinfo->shd from svc object */
        shd = cds_list_entry(svc, glusterd_shdsvc_t, svc);
        if (!shd) {
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_SHD_OBJ_GET_FAIL,
                   "Failed to get shd object "
                   "from shd service");
            goto out;
        }

        /* Get volinfo from shd */
        volinfo = cds_list_entry(shd, glusterd_volinfo_t, shd);
        if (!volinfo) {
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOLINFO_GET_FAIL,
                   "Failed to get volinfo from "
                   "from shd");
            goto out;
        }
    }

    if (!iov) {
        gf_msg(frame->this->name, GF_LOG_ERROR, 0, GD_MSG_REQ_DECODE_FAIL,
               "iov is NULL");
        ret = -1;
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_getspec_rsp);
    if (ret < 0) {
        gf_msg(frame->this->name, GF_LOG_ERROR, 0, GD_MSG_REQ_DECODE_FAIL,
               "XDR decoding error");
        ret = -1;
        goto out;
    }

    if (rsp.op_ret == 0) {
        pthread_mutex_lock(&conf->attach_lock);
        {
            if (!strcmp(svc->name, "glustershd")) {
                mux_proc = svc->svc_proc;
                if (mux_proc &&
                    !gf_is_service_running(svc->proc.pidfile, &pid)) {
                    /*
                     * When svc's are restarting, there is a chance that the
                     * attached svc might not have updated it's pid. Because
                     * it was at connection stage. So in that case, we need
                     * to retry the pid file copy.
                     */
                    parent_svc = cds_list_entry(mux_proc->svcs.next,
                                                glusterd_svc_t, mux_svc);
                    if (parent_svc)
                        sys_link(parent_svc->proc.pidfile, svc->proc.pidfile);
                }
            }
            svc->online = _gf_true;
        }
        pthread_mutex_unlock(&conf->attach_lock);
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_SVC_ATTACH_FAIL,
               "svc %s of volume %s attached successfully to pid %d", svc->name,
               volinfo->volname, glusterd_proc_get_pid(&svc->proc));
    } else {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SVC_ATTACH_FAIL,
               "svc %s of volume %s failed to "
               "attach to pid %d. Starting a new process",
               svc->name, volinfo->volname, glusterd_proc_get_pid(&svc->proc));
        if (!strcmp(svc->name, "glustershd")) {
            glusterd_recover_shd_attach_failure(volinfo, svc, *flag);
        }
    }
out:
    if (flag) {
        GF_FREE(flag);
    }
    GF_ATOMIC_DEC(conf->blockers);
    STACK_DESTROY(frame->root);
    return 0;
}

extern size_t
build_volfile_path(char *volume_id, char *path, size_t path_len,
                   char *trusted_str);

int
__glusterd_send_svc_configure_req(glusterd_svc_t *svc, int flags,
                                  struct rpc_clnt *rpc, char *volfile_id,
                                  int op)
{
    int ret = -1;
    struct iobuf *iobuf = NULL;
    struct iobref *iobref = NULL;
    struct iovec iov = {
        0,
    };
    char path[PATH_MAX] = {
        '\0',
    };
    struct stat stbuf = {
        0,
    };
    int32_t spec_fd = -1;
    size_t file_len = -1;
    char *volfile_content = NULL;
    ssize_t req_size = 0;
    call_frame_t *frame = NULL;
    gd1_mgmt_brick_op_req brick_req;
    void *req = &brick_req;
    void *errlbl = &&err;
    struct rpc_clnt_connection *conn;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = THIS->private;
    extern struct rpc_clnt_program gd_brick_prog;
    fop_cbk_fn_t cbkfn = my_callback;

    if (!rpc) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_PARAM_NULL,
               "called with null rpc");
        return -1;
    }

    conn = &rpc->conn;
    if (!conn->connected || conn->disconnected) {
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_CONNECT_RETURNED,
               "not connected yet");
        return -1;
    }

    brick_req.op = op;
    brick_req.name = volfile_id;
    brick_req.input.input_val = NULL;
    brick_req.input.input_len = 0;

    frame = create_frame(this, this->ctx->pool);
    if (!frame) {
        goto *errlbl;
    }

    if (op == GLUSTERD_SVC_ATTACH) {
        (void)build_volfile_path(volfile_id, path, sizeof(path), NULL);

        ret = sys_stat(path, &stbuf);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SVC_ATTACH_FAIL,
                   "Unable to stat %s (%s)", path, strerror(errno));
            ret = -EINVAL;
            goto *errlbl;
        }

        file_len = stbuf.st_size;
        volfile_content = GF_MALLOC(file_len + 1, gf_common_mt_char);
        if (!volfile_content) {
            ret = -ENOMEM;
            goto *errlbl;
        }
        spec_fd = open(path, O_RDONLY);
        if (spec_fd < 0) {
            gf_msg(THIS->name, GF_LOG_WARNING, 0, GD_MSG_SVC_ATTACH_FAIL,
                   "failed to read volfile %s", path);
            ret = -EIO;
            goto *errlbl;
        }
        ret = sys_read(spec_fd, volfile_content, file_len);
        if (ret == file_len) {
            brick_req.input.input_val = volfile_content;
            brick_req.input.input_len = file_len;
        } else {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SVC_ATTACH_FAIL,
                   "read failed on path %s. File size=%" GF_PRI_SIZET
                   "read size=%d",
                   path, file_len, ret);
            ret = -EIO;
            goto *errlbl;
        }

        frame->cookie = svc;
        frame->local = GF_CALLOC(1, sizeof(int), gf_gld_mt_int);
        *((int *)frame->local) = flags;
        cbkfn = glusterd_svc_attach_cbk;
    }

    req_size = xdr_sizeof((xdrproc_t)xdr_gd1_mgmt_brick_op_req, req);
    iobuf = iobuf_get2(rpc->ctx->iobuf_pool, req_size);
    if (!iobuf) {
        goto *errlbl;
    }
    errlbl = &&maybe_free_iobuf;

    iov.iov_base = iobuf->ptr;
    iov.iov_len = iobuf_pagesize(iobuf);

    iobref = iobref_new();
    if (!iobref) {
        goto *errlbl;
    }
    errlbl = &&free_iobref;

    iobref_add(iobref, iobuf);
    /*
     * Drop our reference to the iobuf.  The iobref should already have
     * one after iobref_add, so when we unref that we'll free the iobuf as
     * well.  This allows us to pass just the iobref as frame->local.
     */
    iobuf_unref(iobuf);
    /* Set the pointer to null so we don't free it on a later error. */
    iobuf = NULL;

    /* Create the xdr payload */
    ret = xdr_serialize_generic(iov, req, (xdrproc_t)xdr_gd1_mgmt_brick_op_req);
    if (ret == -1) {
        goto *errlbl;
    }
    iov.iov_len = ret;

    /* Send the msg */
    GF_ATOMIC_INC(conf->blockers);
    ret = rpc_clnt_submit(rpc, &gd_brick_prog, op, cbkfn, &iov, 1, NULL, 0,
                          iobref, frame, NULL, 0, NULL, 0, NULL);
    GF_FREE(volfile_content);
    if (spec_fd >= 0)
        sys_close(spec_fd);
    return ret;

free_iobref:
    iobref_unref(iobref);
maybe_free_iobuf:
    if (iobuf) {
        iobuf_unref(iobuf);
    }
err:
    GF_FREE(volfile_content);
    if (spec_fd >= 0)
        sys_close(spec_fd);
    if (frame)
        STACK_DESTROY(frame->root);
    return -1;
}

int
glusterd_attach_svc(glusterd_svc_t *svc, glusterd_volinfo_t *volinfo, int flags)
{
    glusterd_conf_t *conf = THIS->private;
    int ret = -1;
    int tries;
    rpc_clnt_t *rpc = NULL;

    GF_VALIDATE_OR_GOTO("glusterd", conf, out);
    GF_VALIDATE_OR_GOTO("glusterd", svc, out);
    GF_VALIDATE_OR_GOTO("glusterd", volinfo, out);

    gf_msg("glusterd", GF_LOG_INFO, 0, GD_MSG_ATTACH_INFO,
           "adding svc %s (volume=%s) to existing "
           "process with pid %d",
           svc->name, volinfo->volname, glusterd_proc_get_pid(&svc->proc));

    rpc = rpc_clnt_ref(svc->conn.rpc);
    for (tries = 15; tries > 0; --tries) {
        if (rpc) {
            pthread_mutex_lock(&conf->attach_lock);
            {
                ret = __glusterd_send_svc_configure_req(
                    svc, flags, rpc, svc->proc.volfileid, GLUSTERD_SVC_ATTACH);
            }
            pthread_mutex_unlock(&conf->attach_lock);
            if (!ret) {
                volinfo->shd.attached = _gf_true;
                goto out;
            }
        }
        /*
         * It might not actually be safe to manipulate the lock
         * like this, but if we don't then the connection can
         * never actually complete and retries are useless.
         * Unfortunately, all of the alternatives (e.g. doing
         * all of this in a separate thread) are much more
         * complicated and risky.
         * TBD: see if there's a better way
         */
        synclock_unlock(&conf->big_lock);
        sleep(1);
        synclock_lock(&conf->big_lock);
    }
    ret = -1;
    gf_msg("glusterd", GF_LOG_WARNING, 0, GD_MSG_SVC_ATTACH_FAIL,
           "attach failed for %s(volume=%s)", svc->name, volinfo->volname);
out:
    if (rpc)
        rpc_clnt_unref(rpc);
    return ret;
}

int
glusterd_detach_svc(glusterd_svc_t *svc, glusterd_volinfo_t *volinfo, int sig)
{
    glusterd_conf_t *conf = THIS->private;
    int ret = -1;
    int tries;
    rpc_clnt_t *rpc = NULL;

    GF_VALIDATE_OR_GOTO(THIS->name, conf, out);
    GF_VALIDATE_OR_GOTO(THIS->name, svc, out);
    GF_VALIDATE_OR_GOTO(THIS->name, volinfo, out);

    gf_msg(THIS->name, GF_LOG_INFO, 0, GD_MSG_DETACH_INFO,
           "removing svc %s (volume=%s) from existing "
           "process with pid %d",
           svc->name, volinfo->volname, glusterd_proc_get_pid(&svc->proc));

    rpc = rpc_clnt_ref(svc->conn.rpc);
    for (tries = 15; tries > 0; --tries) {
        if (rpc) {
            /*For detach there is no flags, and we are not using sig.*/
            pthread_mutex_lock(&conf->attach_lock);
            {
                ret = __glusterd_send_svc_configure_req(svc, 0, svc->conn.rpc,
                                                        svc->proc.volfileid,
                                                        GLUSTERD_SVC_DETACH);
            }
            pthread_mutex_unlock(&conf->attach_lock);
            if (!ret) {
                goto out;
            }
        }
        /*
         * It might not actually be safe to manipulate the lock
         * like this, but if we don't then the connection can
         * never actually complete and retries are useless.
         * Unfortunately, all of the alternatives (e.g. doing
         * all of this in a separate thread) are much more
         * complicated and risky.
         * TBD: see if there's a better way
         */
        synclock_unlock(&conf->big_lock);
        sleep(1);
        synclock_lock(&conf->big_lock);
    }
    ret = -1;
    gf_msg("glusterd", GF_LOG_WARNING, 0, GD_MSG_SVC_DETACH_FAIL,
           "detach failed for %s(volume=%s)", svc->name, volinfo->volname);
out:
    if (rpc)
        rpc_clnt_unref(rpc);
    return ret;
}
