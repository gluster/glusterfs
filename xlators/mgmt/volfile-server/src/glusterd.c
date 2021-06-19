/*
   Copyright (c) 2021 Kadalu.IO <https://kadalu.io>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <time.h>
#include <grp.h>
#include <sys/uio.h>
#include <sys/resource.h>

#include <libgen.h>
#include <glusterfs/compat-uuid.h>

#include "glusterd.h"
#include "glusterd-messages.h"
#include "rpcsvc.h"
#include "fnmatch.h"
#include <glusterfs/xlator.h>
#include <glusterfs/call-stub.h>
#include <glusterfs/defaults.h>
#include <glusterfs/list.h>
#include <glusterfs/dict.h>
#include <glusterfs/options.h>
#include <glusterfs/compat.h>
#include <glusterfs/compat-errno.h>
#include <glusterfs/syscall.h>
#include <glusterfs/common-utils.h>
#include <glusterfs/run.h>
#include "rpc-common-xdr.h"

#include <glusterfs/syncop.h>

extern struct rpcsvc_program gluster_handshake_prog;
rpcsvc_cbk_program_t glusterd_cbk_prog = {
    .progname = "Gluster Callback",
    .prognum = GLUSTER_CBK_PROGRAM,
    .progver = GLUSTER_CBK_VERSION,
};

struct rpcsvc_program *gd_inet_programs[] = {&gluster_handshake_prog};
int gd_inet_programs_count = (sizeof(gd_inet_programs) /
                              sizeof(gd_inet_programs[0]));

int
glusterd_fetchspec_notify(xlator_t *this)
{
    int ret = -1;
    glusterd_conf_t *priv = NULL;
    rpc_transport_t *trans = NULL;

    priv = this->private;

    pthread_mutex_lock(&priv->xprt_lock);
    {
        list_for_each_entry(trans, &priv->xprt_list, list)
        {
            rpcsvc_callback_submit(priv->rpc, trans, &glusterd_cbk_prog,
                                   GF_CBK_FETCHSPEC, NULL, 0, NULL);
        }
    }
    pthread_mutex_unlock(&priv->xprt_lock);

    ret = 0;

    return ret;
}

int
glusterd_fetchsnap_notify(xlator_t *this)
{
    int ret = -1;
    glusterd_conf_t *priv = NULL;
    rpc_transport_t *trans = NULL;

    priv = this->private;

    /*
     * TODO: As of now, the identification of the rpc clients in the
     * handshake protocol is not there. So among so many glusterfs processes
     * registered with glusterd, it is hard to identify one particular
     * process (in this particular case, the snap daemon). So the callback
     * notification is sent to all the transports from the transport list.
     * Only those processes which have a rpc client registered for this
     * callback will respond to the notification. Once the identification
     * of the rpc clients becomes possible, the below section can be changed
     * to send callback notification to only those rpc clients, which have
     * registered.
     */
    pthread_mutex_lock(&priv->xprt_lock);
    {
        list_for_each_entry(trans, &priv->xprt_list, list)
        {
            rpcsvc_callback_submit(priv->rpc, trans, &glusterd_cbk_prog,
                                   GF_CBK_GET_SNAPS, NULL, 0, NULL);
        }
    }
    pthread_mutex_unlock(&priv->xprt_lock);

    ret = 0;

    return ret;
}

int32_t
mem_acct_init(xlator_t *this)
{
    int ret = -1;

    if (!this)
        return ret;

    ret = xlator_mem_acct_init(this, gf_gld_mt_end);

    if (ret != 0) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Memory accounting init failed");
        return ret;
    }

    return ret;
}

int
glusterd_rpcsvc_notify(rpcsvc_t *rpc, void *xl, rpcsvc_event_t event,
                       void *data)
{
    xlator_t *this = NULL;
    rpc_transport_t *xprt = NULL;
    glusterd_conf_t *priv = NULL;

    if (!xl || !data) {
        gf_msg("glusterd", GF_LOG_WARNING, 0, GD_MSG_NO_INIT,
               "Calling rpc_notify without initializing");
        goto out;
    }

    this = xl;
    xprt = data;

    priv = this->private;

    switch (event) {
        case RPCSVC_EVENT_ACCEPT: {
            pthread_mutex_lock(&priv->xprt_lock);
            list_add_tail(&xprt->list, &priv->xprt_list);
            pthread_mutex_unlock(&priv->xprt_lock);
            break;
        }
        case RPCSVC_EVENT_DISCONNECT: {
            /* A DISCONNECT event could come without an ACCEPT event
             * happening for this transport. This happens when the server is
             * expecting encrypted connections by the client tries to
             * connect unecnrypted
             */
            if (list_empty(&xprt->list))
                break;

            pthread_mutex_lock(&priv->xprt_lock);
            list_del(&xprt->list);
            pthread_mutex_unlock(&priv->xprt_lock);
            break;
        }

        default:
            break;
    }

out:
    return 0;
}

void
glusterd_stop_listener(xlator_t *this)
{
    glusterd_conf_t *conf = NULL;
    rpcsvc_listener_t *listener = NULL;
    rpcsvc_listener_t *next = NULL;
    int i = 0;

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    for (i = 0; i < gd_inet_programs_count; i++) {
        rpcsvc_program_unregister(conf->rpc, gd_inet_programs[i]);
    }

    list_for_each_entry_safe(listener, next, &conf->rpc->listeners, list)
    {
        rpcsvc_listener_destroy(listener);
    }

    (void)rpcsvc_unregister_notify(conf->rpc, glusterd_rpcsvc_notify, this);

out:

    return;
}

/*
 * init - called during glusterd initialization
 *
 * @this:
 *
 */
int
init(xlator_t *this)
{
    int32_t ret, len = -1;
    rpcsvc_t *rpc = NULL;
    glusterd_conf_t *conf = NULL;
    data_t *dir_data = NULL;
    struct stat buf = {
        0,
    };
    char workdir[PATH_MAX] = {
        0,
    };

    dir_data = dict_get(this->options, "volspec-directory");

    if (!dir_data) {
        // Use default working dir
        len = snprintf(workdir, PATH_MAX, "%s", GLUSTERD_DEFAULT_SPECDIR);
    } else {
        len = snprintf(workdir, PATH_MAX, "%s", dir_data->data);
    }
    if (len < 0 || len >= PATH_MAX)
        exit(2);

    ret = sys_stat(workdir, &buf);
    if ((ret != 0) && (ENOENT != errno)) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
               "stat fails on %s, exiting. (errno = %d)", workdir, errno);
        exit(1);
    }

    if ((!ret) && (!S_ISDIR(buf.st_mode))) {
        gf_msg(this->name, GF_LOG_CRITICAL, ENOENT, GD_MSG_DIR_NOT_FOUND,
               "Provided working area %s is not a directory, exiting", workdir);
        exit(1);
    }

    if ((-1 == ret) && (ENOENT == errno)) {
        ret = mkdir_p(workdir, 0755, _gf_true);

        if (-1 == ret) {
            gf_msg(this->name, GF_LOG_CRITICAL, errno, GD_MSG_CREATE_DIR_FAILED,
                   "Unable to create directory %s ,errno = %d", workdir, errno);
            exit(1);
        }
    }

    setenv("GLUSTERD_SPECDIR", workdir, 1);
    gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_CURR_WORK_DIR_INFO,
           "Using %s as specfile directory", workdir);

    /* TODO: rpc options can be handled here */
    rpc = rpcsvc_init(this, this->ctx, this->options, 64);
    if (rpc == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RPC_INIT_FAIL,
               "failed to init rpc");
        goto out;
    }

    ret = rpcsvc_register_notify(rpc, glusterd_rpcsvc_notify, this);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RPCSVC_REG_NOTIFY_RETURNED,
               "rpcsvc_register_notify returned %d", ret);
        goto out;
    }

    ret = rpcsvc_create_listeners(rpc, this->options, this->name);
    if (ret < 1) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RPC_LISTENER_CREATE_FAIL,
               "creation of listener failed");
        ret = -1;
        goto out;
    }

    ret = rpcsvc_program_register(rpc, &gluster_handshake_prog, _gf_false);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RPC_LISTENER_CREATE_FAIL,
               "adding programs failed");
        rpcsvc_program_unregister(rpc, &gluster_handshake_prog);
        goto out;
    }

    conf = GF_CALLOC(1, sizeof(glusterd_conf_t), gf_gld_mt_glusterd_conf_t);

    pthread_mutex_init(&conf->mutex, NULL);
    conf->rpc = rpc;
    this->private = conf;

    /* conf->workdir and conf->rundir are smaller than PATH_MAX; gcc's
     * snprintf checking will throw an error here if sprintf is used.
     * Dueling gcc-8 and coverity, now coverity isn't smart enough to
     * detect that these strncpy calls are safe. And for extra fun,
     * the annotations don't do anything. */
    if (strlen(workdir) >= sizeof(conf->workdir)) {
        ret = -1;
        goto out;
    }
    /* coverity[BUFFER_SIZE_WARNING] */
    (void)strncpy(conf->workdir, workdir, sizeof(conf->workdir));

    pthread_mutex_init(&conf->xprt_lock, NULL);
    INIT_LIST_HEAD(&conf->xprt_list);

    ret = 0;
out:
    if (ret < 0) {
        if (this->private != NULL) {
            GF_FREE(this->private);
            this->private = NULL;
        }
    }

    return ret;
}

/*
 * fini - finish function for glusterd, called before
 *        unloading gluster.
 *
 * @this:
 *
 */
void
fini(xlator_t *this)
{
    if (!this || !this->private)
        goto out;

    glusterd_stop_listener(this);
out:
    return;
}

/*
 * notify - notify function for glusterd
 * @this:
 * @trans:
 * @event:
 *
 */
int
notify(xlator_t *this, int32_t event, void *data, ...)
{
    int ret = 0;

    switch (event) {
        case GF_EVENT_POLLIN:
            break;

        case GF_EVENT_POLLERR:
            break;

        case GF_EVENT_CLEANUP:
            break;

        default:
            default_notify(this, event, data);
            break;
    }

    return ret;
}

struct xlator_fops fops;

struct xlator_cbks cbks;

struct volume_options options[] = {
    {
        .key = {"volspec-directory"},
        .type = GF_OPTION_TYPE_PATH,
    },
    {.key = {NULL}},
};

xlator_api_t xlator_api = {
    .init = init,
    .fini = fini,
    .mem_acct_init = mem_acct_init,
    .op_version = {1}, /* Present from the initial version */
    .fops = &fops,
    .cbks = &cbks,
    .options = options,
    .identifier = "volfile-server",
    .category = GF_MAINTAINED,
};
