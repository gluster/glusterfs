/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include <inttypes.h>

#if defined(GF_LINUX_HOST_OS)
#include <mntent.h>
#else
#include "mntent_compat.h"
#endif
#include <dlfcn.h>
#if (HAVE_LIB_XML)
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>
#endif

#include <glusterfs/compat.h>
#include <glusterfs/dict.h>
#include <glusterfs/logging.h>
#include "glusterd-messages.h"
#include <glusterfs/compat.h>
#include <glusterfs/run.h>
#include <glusterfs/compat-errno.h>
#include <glusterfs/syscall.h>
#include "glusterd.h"
#include "glusterd-op-sm.h"
#include "glusterd-geo-rep.h"
#include "glusterd-utils.h"
#include "glusterd-store.h"
#include <glusterfs/glusterfs-acl.h>
#include "glusterd-mgmt.h"
#include "glusterd-messages.h"
#include "glusterd-snapshot-utils.h"
#include "glusterd-svc-helper.h"
#include "glusterd-server-quorum.h"
#include <glusterfs/quota-common-utils.h>
#include "glusterd-shd-svc-helper.h"
#include "protocol-utils.h"

#include <inttypes.h>
#include <signal.h>
#include <sys/types.h>
#include <rpc/pmap_clnt.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/statvfs.h>
#include <ifaddrs.h>
#include <netdb.h>

#ifdef GF_SOLARIS_HOST_OS
#include <sys/sockio.h>
#endif

#ifdef __FreeBSD__
#include <sys/sysctl.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <libprocstat.h>
#include <libutil.h>
#endif

#define NFS_PROGRAM 100003
#define NFSV3_VERSION 3

#define MOUNT_PROGRAM 100005
#define MOUNTV3_VERSION 3
#define MOUNTV1_VERSION 1

#define NLM_PROGRAM 100021
#define NLMV4_VERSION 4
#define NLMV1_VERSION 1

#ifdef BUILD_GNFS
#define GLUSTERD_GET_NFS_PIDFILE(pidfile, priv)                                \
    do {                                                                       \
        int32_t _nfs_pid_len;                                                  \
        _nfs_pid_len = snprintf(pidfile, PATH_MAX, "%s/nfs/nfs.pid",           \
                                priv->rundir);                                 \
        if ((_nfs_pid_len < 0) || (_nfs_pid_len >= PATH_MAX)) {                \
            pidfile[0] = 0;                                                    \
        }                                                                      \
    } while (0)
#endif

#define GLUSTERD_GET_QUOTAD_PIDFILE(pidfile, priv)                             \
    do {                                                                       \
        int32_t _quotad_pid_len;                                               \
        _quotad_pid_len = snprintf(pidfile, PATH_MAX, "%s/quotad/quotad.pid",  \
                                   priv->rundir);                              \
        if ((_quotad_pid_len < 0) || (_quotad_pid_len >= PATH_MAX)) {          \
            pidfile[0] = 0;                                                    \
        }                                                                      \
    } while (0)

static int
glusterd_brick_terminate(glusterd_volinfo_t *volinfo,
                         glusterd_brickinfo_t *brickinfo, char *options,
                         int option_cnt, char **op_errstr);

static int
glusterd_restart_gsyncds(glusterd_conf_t *conf);

static int32_t
glusterd_delete_all_bricks(glusterd_volinfo_t *volinfo);

static int
glusterd_check_and_set_brick_xattr(char *host, char *path, uuid_t uuid,
                                   char **op_errstr, gf_boolean_t is_force);

static int
glusterd_brick_proc_for_port(int port, glusterd_brick_proc_t **brickprocess);

static int
glusterd_restart_bricks(void *opaque);

static int
glusterd_get_volopt_content(dict_t *ctx, gf_boolean_t xml_out);

int
glusterd_defrag_ref(glusterd_defrag_info_t *defrag)
{
    int refcnt = 0;

    if (!defrag)
        goto out;

    LOCK(&defrag->lock);
    {
        refcnt = ++defrag->refcnt;
    }
    UNLOCK(&defrag->lock);

out:
    return refcnt;
}

int
glusterd_defrag_unref(glusterd_defrag_info_t *defrag)
{
    int refcnt = -1;

    if (!defrag)
        goto out;

    LOCK(&defrag->lock);
    {
        refcnt = --defrag->refcnt;
    }
    UNLOCK(&defrag->lock);
    if (refcnt <= 0)
        GF_FREE(defrag);
out:
    return refcnt;
}

gf_boolean_t
is_brick_mx_enabled(void)
{
    char *value = NULL;
    int ret = 0;
    gf_boolean_t enabled = _gf_false;
    glusterd_conf_t *priv = NULL;

    priv = THIS->private;

    ret = dict_get_str(priv->opts, GLUSTERD_BRICK_MULTIPLEX_KEY, &value);
    if (!ret)
        ret = gf_string2boolean(value, &enabled);

/* GF_ENABLE_BRICKMUX set as a compile time build option, if the
   option is set and brick_mux key is not configured then consider
   brick_mux option is enabled
*/
#if defined(GF_ENABLE_BRICKMUX)
    if (ret) {
        ret = _gf_false;
        enabled = _gf_true;
    }
#endif

    return ret ? _gf_false : enabled;
}

static gf_boolean_t
is_brick_graceful_cleanup_enabled(dict_t *opts)
{
    char *value = NULL;
    int ret = 0;
    gf_boolean_t enabled = _gf_false;

    ret = dict_get_str(opts, GLUSTER_BRICK_GRACEFUL_CLEANUP, &value);
    if (!ret)
        ret = gf_string2boolean(value, &enabled);

    return ret ? _gf_false : enabled;
}

static gf_boolean_t
gd_has_address(struct list_head *hostnames_list_head, const char *hostname)
{
    glusterd_hostname_t *hostname_obj = NULL;

    list_for_each_entry(hostname_obj, hostnames_list_head, hostname_list)
    {
        if (strcmp(hostname_obj->hostname, hostname) == 0) {
            return _gf_true;
        }
    }

    return _gf_false;
}

static int
glusterd_hostname_new(xlator_t *this, const char *hostname,
                      glusterd_hostname_t **name)
{
    glusterd_hostname_t *hostname_obj = NULL;
    int32_t ret = -1;

    GF_ASSERT(hostname);
    GF_ASSERT(name);

    hostname_obj = GF_MALLOC(sizeof(*hostname_obj), gf_gld_mt_hostname_t);

    if (!hostname_obj) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY, NULL);
        goto out;
    }

    hostname_obj->hostname = gf_strdup(hostname);
    CDS_INIT_LIST_HEAD(&hostname_obj->hostname_list);

    *name = hostname_obj;
    ret = 0;

out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

gf_boolean_t
glusterd_gf_is_local_addr(char *hostname)
{
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    glusterd_hostname_t *hostname_obj = NULL;
    gf_boolean_t found = _gf_false;
    int ret = 1;

    priv = this->private;

    if (gd_has_address(&priv->hostnames, hostname)) {
        found = _gf_true;
        goto out;
    }

    if (gd_has_address(&priv->remote_hostnames, hostname)) {
        found = _gf_false;
        goto out;
    }

    ret = glusterd_hostname_new(this, hostname, &hostname_obj);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY, NULL);
        goto out;
    }

    if (gf_is_local_addr(hostname)) {
        found = _gf_true;
        list_add_tail(&hostname_obj->hostname_list, &priv->hostnames);
    } else {
        found = _gf_false;
        list_add_tail(&hostname_obj->hostname_list, &priv->remote_hostnames);
    }

out:
    return found;
}

static int
get_mux_limit_per_process(int *mux_limit)
{
    char *value = NULL;
    int ret = -1;
    int max_bricks_per_proc = 0;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;

    priv = this->private;
    GF_VALIDATE_OR_GOTO(this->name, priv, out);

    if (!is_brick_mx_enabled()) {
        max_bricks_per_proc = 1;
        ret = 0;
        goto out;
    }

    ret = dict_get_str(priv->opts, GLUSTERD_BRICKMUX_LIMIT_KEY, &value);
    if (ret) {
        value = GLUSTERD_BRICKMUX_LIMIT_DFLT_VALUE;
    }
    ret = gf_string2int(value, &max_bricks_per_proc);
    if (ret)
        goto out;

out:
    *mux_limit = max_bricks_per_proc;

    gf_msg_debug("glusterd", 0, "Mux limit set to %d bricks per process",
                 *mux_limit);

    return ret;
}

static int
get_gd_vol_thread_limit(int *thread_limit)
{
    char *value = NULL;
    int ret = -1;
    int vol_per_thread_limit = 0;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;

    priv = this->private;
    GF_VALIDATE_OR_GOTO(this->name, priv, out);

    if (!is_brick_mx_enabled()) {
        vol_per_thread_limit = 1;
        ret = 0;
        goto out;
    }

    ret = dict_get_str(priv->opts, GLUSTERD_VOL_CNT_PER_THRD, &value);
    if (ret) {
        value = GLUSTERD_VOL_CNT_PER_THRD_DEFAULT_VALUE;
    }
    ret = gf_string2int(value, &vol_per_thread_limit);
    if (ret)
        goto out;

out:
    *thread_limit = vol_per_thread_limit;

    gf_msg_debug("glusterd", 0,
                 "Per Thread volume limit set to %d glusterd to populate dict "
                 "data parallel",
                 *thread_limit);

    return ret;
}

extern struct volopt_map_entry glusterd_volopt_map[];
extern glusterd_all_vol_opts valid_all_vol_opts[];

static glusterd_lock_t lock;

int32_t
glusterd_get_lock_owner(uuid_t *uuid)
{
    gf_uuid_copy(*uuid, lock.owner);
    return 0;
}

static int32_t
glusterd_set_lock_owner(uuid_t owner)
{
    gf_uuid_copy(lock.owner, owner);
    // TODO: set timestamp
    return 0;
}

static int32_t
glusterd_unset_lock_owner(uuid_t owner)
{
    gf_uuid_clear(lock.owner);
    // TODO: set timestamp
    return 0;
}

gf_boolean_t
glusterd_is_fuse_available(void)
{
    int fd = 0;

#ifdef __NetBSD__
    fd = open("/dev/puffs", O_RDWR);
#else
    fd = open("/dev/fuse", O_RDWR);
#endif

    if (fd > -1 && !sys_close(fd))
        return _gf_true;
    else
        return _gf_false;
}

int32_t
glusterd_lock(uuid_t uuid)
{
    uuid_t owner;
    char new_owner_str[50] = "";
    char owner_str[50] = "";
    int ret = -1;
    xlator_t *this = THIS;

    GF_ASSERT(uuid);

    glusterd_get_lock_owner(&owner);

    if (!gf_uuid_is_null(owner)) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GLUSTERD_LOCK_FAIL,
               "Unable to get lock"
               " for uuid: %s, lock held by: %s",
               uuid_utoa_r(uuid, new_owner_str), uuid_utoa_r(owner, owner_str));
        goto out;
    }

    ret = glusterd_set_lock_owner(uuid);

    if (!ret) {
        gf_msg_debug(this->name, 0,
                     "Cluster lock held by"
                     " %s",
                     uuid_utoa(uuid));
    }

out:
    return ret;
}

int32_t
glusterd_unlock(uuid_t uuid)
{
    uuid_t owner;
    char new_owner_str[50] = "";
    char owner_str[50] = "";
    int32_t ret = -1;

    GF_ASSERT(uuid);

    glusterd_get_lock_owner(&owner);

    if (gf_uuid_is_null(owner)) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_GLUSTERD_LOCK_FAIL,
               "Cluster lock not held!");
        goto out;
    }

    ret = gf_uuid_compare(uuid, owner);

    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_GLUSTERD_LOCK_FAIL,
               "Cluster lock held by %s ,"
               "unlock req from %s!",
               uuid_utoa_r(owner, owner_str), uuid_utoa_r(uuid, new_owner_str));
        goto out;
    }

    ret = glusterd_unset_lock_owner(uuid);

    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_GLUSTERD_UNLOCK_FAIL,
               "Unable to clear cluster "
               "lock");
        goto out;
    }

    ret = 0;

out:
    return ret;
}

int
glusterd_get_uuid(uuid_t *uuid)
{
    gf_uuid_copy(*uuid, MY_UUID);

    return 0;
}

int
glusterd_submit_request(struct rpc_clnt *rpc, void *req, call_frame_t *frame,
                        rpc_clnt_prog_t *prog, int procnum,
                        struct iobref *iobref, xlator_t *this,
                        fop_cbk_fn_t cbkfn, xdrproc_t xdrproc)
{
    char new_iobref = 0;
    int ret = -1;
    int count = 0;
    ssize_t req_size = 0;
    struct iobuf *iobuf = NULL;
    struct iovec iov = {
        0,
    };

    GF_ASSERT(rpc);

    if (req) {
        req_size = xdr_sizeof(xdrproc, req);
        iobuf = iobuf_get2(this->ctx->iobuf_pool, req_size);
        if (!iobuf) {
            goto out;
        };

        if (!iobref) {
            iobref = iobref_new();
            if (!iobref) {
                gf_smsg("glusterd", GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                        NULL);
                goto out;
            }

            new_iobref = 1;
        }

        iobref_add(iobref, iobuf);

        iov.iov_base = iobuf->ptr;
        iov.iov_len = iobuf_pagesize(iobuf);

        /* Create the xdr payload */
        ret = xdr_serialize_generic(iov, req, xdrproc);
        if (ret == -1) {
            goto out;
        }
        iov.iov_len = ret;
        count = 1;
    }

    /* Send the msg */
    rpc_clnt_submit(rpc, prog, procnum, cbkfn, &iov, count, NULL, 0, iobref,
                    frame, NULL, 0, NULL, 0, NULL);

    /* Unconditionally set ret to 0 here. This is to guard against a double
     * STACK_DESTROY in case of a failure in rpc_clnt_submit AFTER the
     * request is sent over the wire: once in the callback function of the
     * request and once in the error codepath of some of the callers of
     * glusterd_submit_request().
     */
    ret = 0;
out:
    if (new_iobref) {
        iobref_unref(iobref);
    }

    iobuf_unref(iobuf);

    return ret;
}

static struct iobuf *
glusterd_serialize_reply(rpcsvc_request_t *req, void *arg, struct iovec *outmsg,
                         xdrproc_t xdrproc)
{
    struct iobuf *iob = NULL;
    ssize_t retlen = -1;
    ssize_t rsp_size = 0;

    /* First, get the io buffer into which the reply in arg will
     * be serialized.
     */
    rsp_size = xdr_sizeof(xdrproc, arg);
    iob = iobuf_get2(req->svc->ctx->iobuf_pool, rsp_size);
    if (!iob) {
        gf_msg("glusterd", GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Failed to get iobuf");
        goto ret;
    }

    iobuf_to_iovec(iob, outmsg);
    /* Use the given serializer to translate the give C structure in arg
     * to XDR format which will be written into the buffer in outmsg.
     */
    /* retlen is used to received the error since size_t is unsigned and we
     * need -1 for error notification during encoding.
     */
    retlen = xdr_serialize_generic(*outmsg, arg, xdrproc);
    if (retlen == -1) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_ENCODE_FAIL,
               "Failed to encode message");
        goto ret;
    }

    outmsg->iov_len = retlen;
ret:
    if (retlen == -1) {
        iobuf_unref(iob);
        iob = NULL;
    }

    return iob;
}

int
glusterd_submit_reply(rpcsvc_request_t *req, void *arg, struct iovec *payload,
                      int payloadcount, struct iobref *iobref,
                      xdrproc_t xdrproc)
{
    struct iobuf *iob = NULL;
    int ret = -1;
    struct iovec rsp = {
        0,
    };
    char new_iobref = 0;

    if (!req) {
        GF_ASSERT(req);
        goto out;
    }

    if (!iobref) {
        iobref = iobref_new();
        if (!iobref) {
            gf_msg("glusterd", GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                   "out of memory");
            goto out;
        }

        new_iobref = 1;
    }

    iob = glusterd_serialize_reply(req, arg, &rsp, xdrproc);
    if (!iob) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_SERIALIZE_MSG_FAIL,
               "Failed to serialize reply");
    } else {
        iobref_add(iobref, iob);
    }

    ret = rpcsvc_submit_generic(req, &rsp, 1, payload, payloadcount, iobref);

    if (ret == -1) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_REPLY_SUBMIT_FAIL,
               "Reply submission failed");
        goto out;
    }

    ret = 0;
out:

    if (new_iobref) {
        iobref_unref(iobref);
    }

    if (iob)
        iobuf_unref(iob);
    return ret;
}

int32_t
glusterd_volinfo_new(glusterd_volinfo_t **volinfo)
{
    glusterd_volinfo_t *new_volinfo = NULL;
    int32_t ret = -1;

    GF_ASSERT(volinfo);

    new_volinfo = GF_CALLOC(1, sizeof(*new_volinfo),
                            gf_gld_mt_glusterd_volinfo_t);

    if (!new_volinfo)
        goto out;

    LOCK_INIT(&new_volinfo->lock);
    CDS_INIT_LIST_HEAD(&new_volinfo->vol_list);
    CDS_INIT_LIST_HEAD(&new_volinfo->snapvol_list);
    CDS_INIT_LIST_HEAD(&new_volinfo->bricks);
    CDS_INIT_LIST_HEAD(&new_volinfo->ta_bricks);
    CDS_INIT_LIST_HEAD(&new_volinfo->snap_volumes);

    new_volinfo->dict = dict_new();
    if (!new_volinfo->dict) {
        gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        GF_FREE(new_volinfo);

        goto out;
    }

    new_volinfo->gsync_secondaries = dict_new();
    if (!new_volinfo->gsync_secondaries) {
        gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        dict_unref(new_volinfo->dict);
        GF_FREE(new_volinfo);
        goto out;
    }

    new_volinfo->gsync_active_secondaries = dict_new();
    if (!new_volinfo->gsync_active_secondaries) {
        gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        dict_unref(new_volinfo->dict);
        dict_unref(new_volinfo->gsync_secondaries);
        GF_FREE(new_volinfo);
        goto out;
    }

    snprintf(new_volinfo->parent_volname, GD_VOLUME_NAME_MAX, "N/A");

    new_volinfo->snap_max_hard_limit = GLUSTERD_SNAPS_MAX_HARD_LIMIT;

    new_volinfo->xl = THIS;

    glusterd_snapdsvc_build(&new_volinfo->snapd.svc);
    glusterd_gfproxydsvc_build(&new_volinfo->gfproxyd.svc);
    glusterd_shdsvc_build(&new_volinfo->shd.svc);

    pthread_mutex_init(&new_volinfo->store_volinfo_lock, NULL);
    GF_ATOMIC_INIT(new_volinfo->refcnt, 0);

    *volinfo = glusterd_volinfo_ref(new_volinfo);

    ret = 0;

out:
    gf_msg_debug(THIS->name, 0, "Returning %d", ret);
    return ret;
}

static void
glusterd_auth_cleanup(glusterd_volinfo_t *volinfo)
{
    GF_ASSERT(volinfo);

    GF_FREE(volinfo->auth.username);

    GF_FREE(volinfo->auth.password);
}

char *
glusterd_auth_get_username(glusterd_volinfo_t *volinfo)
{
    GF_ASSERT(volinfo);

    return volinfo->auth.username;
}

char *
glusterd_auth_get_password(glusterd_volinfo_t *volinfo)
{
    GF_ASSERT(volinfo);

    return volinfo->auth.password;
}

int32_t
glusterd_auth_set_username(glusterd_volinfo_t *volinfo, char *username)
{
    GF_ASSERT(volinfo);
    GF_ASSERT(username);

    volinfo->auth.username = gf_strdup(username);
    return 0;
}

int32_t
glusterd_auth_set_password(glusterd_volinfo_t *volinfo, char *password)
{
    GF_ASSERT(volinfo);
    GF_ASSERT(password);

    volinfo->auth.password = gf_strdup(password);
    return 0;
}

int32_t
glusterd_brickinfo_delete(glusterd_brickinfo_t *brickinfo)
{
    int32_t ret = -1;

    GF_ASSERT(brickinfo);

    cds_list_del_init(&brickinfo->brick_list);

    (void)gf_store_handle_destroy(brickinfo->shandle);

    GF_FREE(brickinfo->logfile);
    GF_FREE(brickinfo);

    ret = 0;

    return ret;
}

static int32_t
glusterd_volume_brickinfos_delete(glusterd_volinfo_t *volinfo)
{
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_brickinfo_t *tmp = NULL;
    int32_t ret = 0;

    GF_ASSERT(volinfo);

    cds_list_for_each_entry_safe(brickinfo, tmp, &volinfo->bricks, brick_list)
    {
        ret = glusterd_brickinfo_delete(brickinfo);
        if (ret)
            goto out;
    }

out:
    gf_msg_debug(THIS->name, 0, "Returning %d", ret);
    return ret;
}

static int32_t
glusterd_volinfo_delete(glusterd_volinfo_t *volinfo)
{
    int32_t ret = -1;

    GF_ASSERT(volinfo);
    /* This is expected to be called from glusterd_volinfo_unref() only. */
    GF_ASSERT(GF_ATOMIC_GET(volinfo->refcnt) == 0);

    cds_list_del_init(&volinfo->vol_list);
    cds_list_del_init(&volinfo->snapvol_list);

    ret = glusterd_volume_brickinfos_delete(volinfo);
    if (ret)
        goto out;
    if (volinfo->dict)
        dict_unref(volinfo->dict);
    if (volinfo->gsync_secondaries)
        dict_unref(volinfo->gsync_secondaries);
    if (volinfo->gsync_active_secondaries)
        dict_unref(volinfo->gsync_active_secondaries);
    GF_FREE(volinfo->logdir);
    if (volinfo->rebal.dict)
        dict_unref(volinfo->rebal.dict);

    /* Destroy the connection object for per volume svc daemons */
    glusterd_conn_term(&volinfo->snapd.svc.conn);
    glusterd_conn_term(&volinfo->gfproxyd.svc.conn);

    gf_store_handle_destroy(volinfo->quota_conf_shandle);
    gf_store_handle_destroy(volinfo->shandle);
    gf_store_handle_destroy(volinfo->node_state_shandle);
    gf_store_handle_destroy(volinfo->snapd.handle);

    glusterd_auth_cleanup(volinfo);
    glusterd_shd_svcproc_cleanup(&volinfo->shd);

    pthread_mutex_destroy(&volinfo->store_volinfo_lock);
    LOCK_DESTROY(&volinfo->lock);

    GF_FREE(volinfo);
    ret = 0;
out:
    gf_msg_debug(THIS->name, 0, "Returning %d", ret);
    return ret;
}

glusterd_volinfo_t *
glusterd_volinfo_unref(glusterd_volinfo_t *volinfo)
{
    long refcnt = -1;
    glusterd_conf_t *conf = THIS->private;

    pthread_mutex_lock(&conf->volume_lock);
    {
        refcnt = GF_ATOMIC_DEC(volinfo->refcnt);
    }
    pthread_mutex_unlock(&conf->volume_lock);
    if (!refcnt) {
        glusterd_volinfo_delete(volinfo);
        return NULL;
    }

    return volinfo;
}

glusterd_volinfo_t *
glusterd_volinfo_ref(glusterd_volinfo_t *volinfo)
{
    GF_ATOMIC_INC(volinfo->refcnt);
    return volinfo;
}

static int
glusterd_volinfo_remove(glusterd_volinfo_t *volinfo)
{
    cds_list_del_init(&volinfo->vol_list);
    glusterd_volinfo_unref(volinfo);
    return 0;
}

static int32_t
glusterd_brickprocess_new(glusterd_brick_proc_t **brickprocess)
{
    glusterd_brick_proc_t *new_brickprocess = NULL;
    int32_t ret = -1;

    GF_VALIDATE_OR_GOTO(THIS->name, brickprocess, out);

    new_brickprocess = GF_CALLOC(1, sizeof(*new_brickprocess),
                                 gf_gld_mt_glusterd_brick_proc_t);

    if (!new_brickprocess)
        goto out;

    CDS_INIT_LIST_HEAD(&new_brickprocess->bricks);
    CDS_INIT_LIST_HEAD(&new_brickprocess->brick_proc_list);

    new_brickprocess->brick_count = 0;
    *brickprocess = new_brickprocess;

    ret = 0;

out:
    gf_msg_debug(THIS->name, 0, "Returning %d", ret);
    return ret;
}

int32_t
glusterd_brickinfo_new(glusterd_brickinfo_t **brickinfo)
{
    glusterd_brickinfo_t *new_brickinfo = NULL;
    int32_t ret = -1;

    GF_ASSERT(brickinfo);

    new_brickinfo = GF_CALLOC(1, sizeof(*new_brickinfo),
                              gf_gld_mt_glusterd_brickinfo_t);

    if (!new_brickinfo)
        goto out;

    CDS_INIT_LIST_HEAD(&new_brickinfo->brick_list);
    CDS_INIT_LIST_HEAD(&new_brickinfo->mux_bricks);
    pthread_mutex_init(&new_brickinfo->restart_mutex, NULL);
    *brickinfo = new_brickinfo;

    ret = 0;

out:
    gf_msg_debug(THIS->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_get_next_available_brickid(glusterd_volinfo_t *volinfo)
{
    glusterd_brickinfo_t *brickinfo = NULL;
    char *token = NULL;
    int brickid = 0;
    int max_brickid = -1;
    int ret = -1;

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        token = strrchr(brickinfo->brick_id, '-');
        ret = gf_string2int32(++token, &brickid);
        if (ret < 0) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_ID_GEN_FAILED,
                   "Unable to generate brick ID");
            return ret;
        }
        if (brickid > max_brickid)
            max_brickid = brickid;
    }

    return max_brickid + 1;
}

int32_t
glusterd_resolve_brick(glusterd_brickinfo_t *brickinfo)
{
    int32_t ret = -1;

    GF_ASSERT(brickinfo);
    if (!gf_uuid_compare(brickinfo->uuid, MY_UUID) ||
        (glusterd_peerinfo_find_by_uuid(brickinfo->uuid) != NULL)) {
        ret = 0;
        goto out;
    }

    ret = glusterd_hostname_to_uuid(brickinfo->hostname, brickinfo->uuid);

out:
    gf_msg_debug(THIS->name, 0, "Returning %d", ret);
    return ret;
}

int32_t
glusterd_get_brick_mount_dir(char *brickpath, char *hostname, char *mount_dir)
{
    char *mnt_pt = NULL;
    char *brick_dir = NULL;
    int32_t ret = -1;
    uuid_t brick_uuid = {
        0,
    };
    xlator_t *this = THIS;

    GF_ASSERT(brickpath);
    GF_ASSERT(hostname);
    GF_ASSERT(mount_dir);

    ret = glusterd_hostname_to_uuid(hostname, brick_uuid);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_HOSTNAME_TO_UUID_FAIL,
               "Failed to convert hostname %s to uuid", hostname);
        goto out;
    }

    if (!gf_uuid_compare(brick_uuid, MY_UUID)) {
        ret = glusterd_get_brick_root(brickpath, &mnt_pt);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, 0,
                   GD_MSG_BRICKPATH_ROOT_GET_FAIL,
                   "Could not get the root of the brick path %s", brickpath);
            goto out;
        }

        if (strncmp(brickpath, mnt_pt, strlen(mnt_pt))) {
            gf_msg(this->name, GF_LOG_WARNING, 0,
                   GD_MSG_BRKPATH_MNTPNT_MISMATCH, "brick: %s brick mount: %s",
                   brickpath, mnt_pt);
            ret = -1;
            goto out;
        }

        brick_dir = &brickpath[strlen(mnt_pt)];
        if (brick_dir[0] == '/')
            brick_dir++;

        snprintf(mount_dir, VALID_GLUSTERD_PATHMAX, "/%s", brick_dir);
    }

out:
    if (mnt_pt)
        GF_FREE(mnt_pt);

    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

static char *
get_path_name(char *word, char **path)
{
    char *delimiter = NULL;
    delimiter = strchr(word, '/');
    if (!delimiter)
        return NULL;
    *path = delimiter;
    return *path;
}

int32_t
glusterd_brickinfo_new_from_brick(char *brick, glusterd_brickinfo_t **brickinfo,
                                  gf_boolean_t construct_real_path,
                                  char **op_errstr)
{
    char *hostname = NULL;
    char *path = NULL;
    char *tmp_host = NULL;
    char *tmp_path = NULL;
    int32_t ret = -1;
    glusterd_brickinfo_t *new_brickinfo = NULL;
    xlator_t *this = THIS;
    char abspath[PATH_MAX] = "";

    GF_ASSERT(brick);
    GF_ASSERT(brickinfo);

    tmp_host = gf_strdup(brick);
    if (tmp_host && !get_host_name(tmp_host, &hostname))
        goto out;
    tmp_path = gf_strdup(brick);
    if (tmp_path && !get_path_name(tmp_path, &path))
        goto out;

    GF_ASSERT(hostname);
    GF_ASSERT(path);

    ret = glusterd_brickinfo_new(&new_brickinfo);
    if (ret)
        goto out;

    ret = gf_canonicalize_path(path);
    if (ret)
        goto out;
    ret = snprintf(new_brickinfo->hostname, sizeof(new_brickinfo->hostname),
                   "%s", hostname);
    if (ret < 0 || ret >= sizeof(new_brickinfo->hostname)) {
        ret = -1;
        goto out;
    }
    ret = snprintf(new_brickinfo->path, sizeof(new_brickinfo->path), "%s",
                   path);
    if (ret < 0 || ret >= sizeof(new_brickinfo->path)) {
        ret = -1;
        goto out;
    }

    if (construct_real_path) {
        ret = glusterd_hostname_to_uuid(new_brickinfo->hostname,
                                        new_brickinfo->uuid);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_HOSTNAME_TO_UUID_FAIL,
                   "Failed to convert hostname %s to uuid", hostname);
            if (op_errstr)
                gf_asprintf(op_errstr,
                            "Host %s is not in "
                            "\'Peer in Cluster\' state",
                            new_brickinfo->hostname);
            goto out;
        }
    }

    if (construct_real_path && !gf_uuid_compare(new_brickinfo->uuid, MY_UUID) &&
        new_brickinfo->real_path[0] == '\0') {
        if (!realpath(new_brickinfo->path, abspath)) {
            /* ENOENT indicates that brick path has not been created
             * which is a valid scenario */
            if (errno != ENOENT) {
                gf_msg(this->name, GF_LOG_CRITICAL, errno,
                       GD_MSG_BRICKINFO_CREATE_FAIL,
                       "realpath"
                       " () failed for brick %s. The "
                       "underlying filesystem may be in bad "
                       "state. Error - %s",
                       new_brickinfo->path, strerror(errno));
                ret = -1;
                goto out;
            }
        }
        if (strlen(abspath) >= sizeof(new_brickinfo->real_path)) {
            ret = -1;
            goto out;
        }
        (void)strncpy(new_brickinfo->real_path, abspath,
                      sizeof(new_brickinfo->real_path));
    }

    *brickinfo = new_brickinfo;

    ret = 0;
out:
    GF_FREE(tmp_host);
    if (tmp_host)
        GF_FREE(tmp_path);

    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

static gf_boolean_t
_is_prefix(char *str1, char *str2)
{
    GF_ASSERT(str1);
    GF_ASSERT(str2);

    int i = 0;
    int len1 = 0;
    int len2 = 0;
    int small_len = 0;
    char *bigger = NULL;
    gf_boolean_t prefix = _gf_true;

    len1 = strlen(str1);
    len2 = strlen(str2);
    small_len = min(len1, len2);

    /*
     * If either one (not both) of the strings are 0-length, they are not
     * prefixes of each other.
     */
    if ((small_len == 0) && (len1 != len2)) {
        return _gf_false;
    }

    for (i = 0; i < small_len; i++) {
        if (str1[i] != str2[i]) {
            prefix = _gf_false;
            break;
        }
    }

    if (len1 < len2)
        bigger = str2;

    else if (len1 > len2)
        bigger = str1;

    else
        return prefix;

    if (bigger[small_len] != '/')
        prefix = _gf_false;

    return prefix;
}

/* Checks if @path is available in the peer identified by @uuid
 * 'availability' is determined by querying current state of volumes
 * in the cluster. */
static gf_boolean_t
glusterd_is_brickpath_available(uuid_t uuid, char *path)
{
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_conf_t *priv = NULL;
    gf_boolean_t available = _gf_false;
    char tmp_path[PATH_MAX] = "";

    priv = THIS->private;

    if (snprintf(tmp_path, PATH_MAX, "%s", path) >= PATH_MAX)
        goto out;
    /* path may not yet exist */
    if (!realpath(path, tmp_path)) {
        if (errno != ENOENT) {
            gf_msg(THIS->name, GF_LOG_CRITICAL, errno,
                   GD_MSG_BRICKINFO_CREATE_FAIL,
                   "realpath"
                   " () failed for brick %s. The "
                   "underlying filesystem may be in bad "
                   "state. Error - %s",
                   path, strerror(errno));
            goto out;
        }
        /* When realpath(3) fails, tmp_path is undefined. */
        (void)snprintf(tmp_path, sizeof(tmp_path), "%s", path);
    }

    cds_list_for_each_entry(volinfo, &priv->volumes, vol_list)
    {
        cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
        {
            if (gf_uuid_compare(uuid, brickinfo->uuid))
                continue;
            if (_is_prefix(brickinfo->real_path, tmp_path)) {
                gf_msg(THIS->name, GF_LOG_CRITICAL, 0,
                       GD_MSG_BRICKINFO_CREATE_FAIL,
                       "_is_prefix call failed for brick %s "
                       "against brick %s",
                       tmp_path, brickinfo->real_path);
                goto out;
            }
        }
    }
    available = _gf_true;
out:
    return available;
}

int
glusterd_validate_and_create_brickpath(glusterd_brickinfo_t *brickinfo,
                                       uuid_t volume_id, char *volname,
                                       char **op_errstr, gf_boolean_t is_force,
                                       gf_boolean_t ignore_partition)
{
    int ret = -1;
    struct mntent *mnt_ent = NULL;
    struct mntent save_entry = {
        0,
    };
    struct stat brick_st = {
        0,
    };
    struct stat root_st = {
        0,
    };
    char msg[2048] = "";
    char buff[PATH_MAX] = "";
    gf_boolean_t is_created = _gf_false;
    char glusterfs_dir_path[PATH_MAX] = "";
    int32_t len = 0;

    ret = sys_mkdir(brickinfo->path, 0755);
    if (ret) {
        if (errno != EEXIST) {
            len = snprintf(msg, sizeof(msg),
                           "Failed to create "
                           "brick directory for brick %s:%s. "
                           "Reason : %s ",
                           brickinfo->hostname, brickinfo->path,
                           strerror(errno));
            gf_smsg(
                "glusterd", GF_LOG_ERROR, errno, GD_MSG_CREATE_BRICK_DIR_FAILED,
                "Brick_hostname=%s, Brick_path=%s, Reason=%s",
                brickinfo->hostname, brickinfo->path, strerror(errno), NULL);
            goto out;
        }
    } else {
        is_created = _gf_true;
    }

    ret = sys_lstat(brickinfo->path, &brick_st);
    if (ret) {
        len = snprintf(msg, sizeof(msg),
                       "lstat failed on %s. "
                       "Reason : %s",
                       brickinfo->path, strerror(errno));
        gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_LSTAT_FAIL,
                "Failed on Brick_path=%s, Reason=%s", brickinfo->path,
                strerror(errno), NULL);
        goto out;
    }

    if ((!is_created) && (!S_ISDIR(brick_st.st_mode))) {
        len = snprintf(msg, sizeof(msg),
                       "The provided path %s "
                       "which is already present, is not a directory",
                       brickinfo->path);
        gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_DIR_OP_FAILED,
                "Brick_path=%s", brickinfo->path, NULL);
        ret = -1;
        goto out;
    }

    ret = sys_lstat("/", &root_st);
    if (ret) {
        len = snprintf(msg, sizeof(msg),
                       "lstat failed on /. "
                       "Reason : %s",
                       strerror(errno));
        gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_LSTAT_FAIL,
                "Failed on /, Reason=%s", strerror(errno), NULL);
        goto out;
    }

    if (strncmp(volname, GLUSTER_SHARED_STORAGE,
                SLEN(GLUSTER_SHARED_STORAGE)) &&
        sizeof(GLUSTERD_DEFAULT_WORKDIR) <= (strlen(brickinfo->path) + 1) &&
        !strncmp(brickinfo->path, GLUSTERD_DEFAULT_WORKDIR,
                 (sizeof(GLUSTERD_DEFAULT_WORKDIR) - 1))) {
        len = snprintf(msg, sizeof(msg),
                       "Brick isn't allowed to be "
                       "created inside glusterd's working directory.");
        gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_BRICK_CREATION_FAIL,
                NULL);
        ret = -1;
        goto out;
    }

    if (!is_force) {
        mnt_ent = glusterd_get_mnt_entry_info(brickinfo->path, buff,
                                              sizeof(buff), &save_entry);
        if (mnt_ent) {
            len = snprintf(msg, sizeof(msg),
                           "The brick %s:%s "
                           "is a mount point. Please create a "
                           "sub-directory under the mount point "
                           "and use that as the brick directory. "
                           "Or use 'force' at the end of the "
                           "command if you want to override this "
                           "behavior.",
                           brickinfo->hostname, brickinfo->path);
            gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_BRICK_CREATE_MNTPNT,
                    "Use 'force' at the end of the command if you want to "
                    "override this behavior, Brick_hostname=%s, Brick_path=%s",
                    brickinfo->hostname, brickinfo->path, NULL);
            ret = -1;
            goto out;
        } else if (brick_st.st_dev == root_st.st_dev) {
            len = snprintf(msg, sizeof(msg),
                           "The brick %s:%s "
                           "is being created in the root "
                           "partition. It is recommended that "
                           "you don't use the system's root "
                           "partition for storage backend. Or "
                           "use 'force' at the end of the "
                           "command if you want to override this "
                           "behavior.",
                           brickinfo->hostname, brickinfo->path);
            gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_BRICK_CREATE_ROOT,
                    "Use 'force' at the end of the command if you want to "
                    "override this behavior, Brick_hostname=%s, Brick_path=%s",
                    brickinfo->hostname, brickinfo->path, NULL);

            /* If --wignore-partition flag is used, ignore warnings
             * related to bricks being on root partition when 'force'
             * is not used */
            if ((len < 0) || (len >= sizeof(msg)) || !ignore_partition) {
                ret = -1;
                goto out;
            }
        }
    }

    ret = glusterd_check_and_set_brick_xattr(
        brickinfo->hostname, brickinfo->path, volume_id, op_errstr, is_force);
    if (ret)
        goto out;

    /* create .glusterfs directory */
    len = snprintf(glusterfs_dir_path, sizeof(glusterfs_dir_path), "%s/%s",
                   brickinfo->path, ".glusterfs");
    if ((len < 0) || (len >= sizeof(glusterfs_dir_path))) {
        ret = -1;
        goto out;
    }

    ret = sys_mkdir(glusterfs_dir_path, 0600);
    if (ret && (errno != EEXIST)) {
        len = snprintf(msg, sizeof(msg),
                       "Failed to create "
                       ".glusterfs directory for brick %s:%s. "
                       "Reason : %s ",
                       brickinfo->hostname, brickinfo->path, strerror(errno));
        gf_smsg("glusterd", GF_LOG_ERROR, errno,
                GD_MSG_CREATE_GLUSTER_DIR_FAILED,
                "Brick_hostname=%s, Brick_path=%s, Reason=%s",
                brickinfo->hostname, brickinfo->path, strerror(errno), NULL);
        goto out;
    }

    ret = 0;

out:
    if (len < 0) {
        ret = -1;
    }
    if (ret && is_created) {
        (void)recursive_rmdir(brickinfo->path);
    }
    if (ret && !*op_errstr && msg[0] != '\0')
        *op_errstr = gf_strdup(msg);

    return ret;
}

static int32_t
glusterd_volume_brickinfo_get(uuid_t uuid, char *hostname, char *path,
                              glusterd_volinfo_t *volinfo,
                              glusterd_brickinfo_t **brickinfo)
{
    glusterd_brickinfo_t *brickiter = NULL;
    uuid_t peer_uuid = {0};
    int32_t ret = -1;
    xlator_t *this = THIS;

    if (uuid) {
        gf_uuid_copy(peer_uuid, uuid);
    } else {
        ret = glusterd_hostname_to_uuid(hostname, peer_uuid);
        if (ret)
            goto out;
    }
    ret = -1;
    cds_list_for_each_entry(brickiter, &volinfo->bricks, brick_list)
    {
        if ((gf_uuid_is_null(brickiter->uuid)) &&
            (glusterd_resolve_brick(brickiter) != 0))
            goto out;
        if (gf_uuid_compare(peer_uuid, brickiter->uuid))
            continue;

        if (strcmp(brickiter->path, path) == 0) {
            gf_msg_debug(this->name, 0, LOGSTR_FOUND_BRICK, brickiter->hostname,
                         brickiter->path, volinfo->volname);
            ret = 0;
            if (brickinfo)
                *brickinfo = brickiter;
            break;
        }
    }

out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

static int32_t
glusterd_volume_ta_brickinfo_get(uuid_t uuid, char *hostname, char *path,
                                 glusterd_volinfo_t *volinfo,
                                 glusterd_brickinfo_t **ta_brickinfo)
{
    glusterd_brickinfo_t *ta_brickiter = NULL;
    int32_t ret = -1;
    xlator_t *this = THIS;

    ret = -1;

    cds_list_for_each_entry(ta_brickiter, &volinfo->ta_bricks, brick_list)
    {
        if (strcmp(ta_brickiter->path, path) == 0 &&
            strcmp(ta_brickiter->hostname, hostname) == 0) {
            gf_msg_debug(this->name, 0, LOGSTR_FOUND_BRICK,
                         ta_brickiter->hostname, ta_brickiter->path,
                         volinfo->volname);
            ret = 0;
            if (ta_brickinfo)
                *ta_brickinfo = ta_brickiter;
            break;
        }
    }

    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int32_t
glusterd_volume_brickinfo_get_by_brick(char *brick, glusterd_volinfo_t *volinfo,
                                       glusterd_brickinfo_t **brickinfo,
                                       gf_boolean_t construct_real_path)
{
    int32_t ret = -1;
    glusterd_brickinfo_t *tmp_brickinfo = NULL;

    GF_ASSERT(brick);
    GF_ASSERT(volinfo);

    ret = glusterd_brickinfo_new_from_brick(brick, &tmp_brickinfo,
                                            construct_real_path, NULL);
    if (ret)
        goto out;

    ret = glusterd_volume_brickinfo_get(
        NULL, tmp_brickinfo->hostname, tmp_brickinfo->path, volinfo, brickinfo);
    (void)glusterd_brickinfo_delete(tmp_brickinfo);
out:
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

gf_boolean_t
glusterd_is_brick_decommissioned(glusterd_volinfo_t *volinfo, char *hostname,
                                 char *path)
{
    gf_boolean_t decommissioned = _gf_false;
    glusterd_brickinfo_t *brickinfo = NULL;
    int ret = -1;

    ret = glusterd_volume_brickinfo_get(NULL, hostname, path, volinfo,
                                        &brickinfo);
    if (ret)
        goto out;
    decommissioned = brickinfo->decommissioned;
out:
    return decommissioned;
}

int32_t
glusterd_volinfo_find(const char *volname, glusterd_volinfo_t **volinfo)
{
    glusterd_volinfo_t *tmp_volinfo = NULL;
    int32_t ret = -1;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;

    GF_ASSERT(volname);

    priv = this->private;
    GF_ASSERT(priv);

    cds_list_for_each_entry(tmp_volinfo, &priv->volumes, vol_list)
    {
        if (!strcmp(tmp_volinfo->volname, volname)) {
            gf_msg_debug(this->name, 0, "Volume %s found", volname);
            ret = 0;
            *volinfo = tmp_volinfo;
            break;
        }
    }

    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

static int32_t
glusterd_service_stop(const char *service, char *pidfile, int sig,
                      gf_boolean_t force_kill)
{
    int32_t ret = -1;
    pid_t pid = -1;
    xlator_t *this = THIS;

    if (!gf_is_service_running(pidfile, &pid)) {
        ret = 0;
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_ALREADY_STOPPED,
               "%s already stopped", service);
        goto out;
    }
    gf_msg_debug(this->name, 0,
                 "Stopping gluster %s running in pid: "
                 "%d",
                 service, pid);

    ret = kill(pid, sig);
    if (ret) {
        switch (errno) {
            case ESRCH:
                gf_msg_debug(this->name, 0, "%s is already stopped", service);
                ret = 0;
                goto out;
            default:
                gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_SVC_KILL_FAIL,
                       "Unable to kill %s "
                       "service, reason:%s",
                       service, strerror(errno));
        }
    }
    if (!force_kill)
        goto out;

    sleep(1);
    if (gf_is_service_running(pidfile, &pid)) {
        ret = kill(pid, SIGKILL);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_PID_KILL_FAIL,
                   "Unable to kill pid:%d, "
                   "reason:%s",
                   pid, strerror(errno));
            goto out;
        }
    }

    ret = 0;
out:
    return ret;
}

void
glusterd_set_socket_filepath(char *sock_filepath, char *sockpath, size_t len)
{
    char xxh64[GF_XXH64_DIGEST_LENGTH * 2 + 1] = {
        0,
    };

    gf_xxh64_wrapper((unsigned char *)sock_filepath, strlen(sock_filepath),
                     GF_XXHSUM64_DEFAULT_SEED, xxh64);
    snprintf(sockpath, len, "%s/%s.socket", GLUSTERD_SOCK_DIR, xxh64);
}

static void
glusterd_set_brick_socket_filepath(glusterd_volinfo_t *volinfo,
                                   glusterd_brickinfo_t *brickinfo,
                                   char *sockpath, size_t len)
{
    char volume_dir[PATH_MAX] = "";
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    int expected_file_len = 0;
    char export_path[PATH_MAX] = "";
    char sock_filepath[PATH_MAX] = "";
    int32_t slen = 0;

    expected_file_len = SLEN(GLUSTERD_SOCK_DIR) + SLEN("/") +
                        SHA256_DIGEST_LENGTH * 2 + SLEN(".socket") + 1;
    GF_ASSERT(len >= expected_file_len);

    priv = this->private;

    GLUSTERD_GET_VOLUME_PID_DIR(volume_dir, volinfo, priv);
    GLUSTERD_REMOVE_SLASH_FROM_PATH(brickinfo->path, export_path);
    slen = snprintf(sock_filepath, PATH_MAX, "%s/run/%s-%s", volume_dir,
                    brickinfo->hostname, export_path);
    if (slen < 0) {
        sock_filepath[0] = 0;
    }
    glusterd_set_socket_filepath(sock_filepath, sockpath, len);
}

/* connection happens only if it is not already connected,
 * reconnections are taken care by rpc-layer
 */
static int32_t
glusterd_brick_connect(glusterd_volinfo_t *volinfo,
                       glusterd_brickinfo_t *brickinfo, char *socketpath)
{
    int ret = 0;
    char volume_id_str[64] = "";
    char *brickid = NULL;
    dict_t *options = NULL;
    struct rpc_clnt *rpc = NULL;

    GF_ASSERT(volinfo);
    GF_ASSERT(brickinfo);
    GF_ASSERT(socketpath);

    if (brickinfo->rpc == NULL) {
        /* Setting frame-timeout to 10mins (600seconds).
         * Unix domain sockets ensures that the connection is reliable.
         * The default timeout of 30mins used for unreliable network
         * connections is too long for unix domain socket connections.
         */
        options = dict_new();
        if (!options) {
            gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL,
                    NULL);
            goto out;
        }

        ret = rpc_transport_unix_options_build(options, socketpath, 600);
        if (ret)
            goto out;

        uuid_utoa_r(volinfo->volume_id, volume_id_str);
        ret = gf_asprintf(&brickid, "%s:%s:%s", volume_id_str,
                          brickinfo->hostname, brickinfo->path);
        if (ret < 0)
            goto out;

        ret = glusterd_rpc_create(&rpc, options, glusterd_brick_rpc_notify,
                                  brickid, _gf_false);
        if (ret) {
            GF_FREE(brickid);
            goto out;
        }
        brickinfo->rpc = rpc;
    }
out:
    if (options)
        dict_unref(options);
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

static int
_mk_rundir_p(glusterd_volinfo_t *volinfo)
{
    char rundir[PATH_MAX] = "";
    glusterd_conf_t *priv = NULL;
    xlator_t *this = THIS;
    int ret = -1;

    priv = this->private;
    GLUSTERD_GET_VOLUME_PID_DIR(rundir, volinfo, priv);
    ret = mkdir_p(rundir, 0755, _gf_true);
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_CREATE_DIR_FAILED,
               "Failed to create rundir");
    return ret;
}

static int32_t
glusterd_volume_start_glusterfs(glusterd_volinfo_t *volinfo,
                                glusterd_brickinfo_t *brickinfo,
                                gf_boolean_t wait)
{
    int32_t ret = -1;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    char pidfile[PATH_MAX + 1] = "";
    char volfile[PATH_MAX] = "";
    runner_t runner = {
        0,
    };
    char exp_path[PATH_MAX] = "";
    char logfile[PATH_MAX] = "";
    int port = 0;
    int rdma_port = 0;
    char *bind_address = NULL;
    char *localtime_logging = NULL;
    char socketpath[PATH_MAX] = "";
    char glusterd_uuid[1024] = "";
    char valgrind_logfile[PATH_MAX] = "";
    char rdma_brick_path[PATH_MAX] = "";
    struct rpc_clnt *rpc = NULL;
    rpc_clnt_connection_t *conn = NULL;
    int pid = -1;
    int32_t len = 0;
    glusterd_brick_proc_t *brick_proc = NULL;
    char *inet_family = NULL;
    char *global_threading = NULL;
    bool threading = false;
    glusterd_volinfo_t *parent_volinfo = NULL;
    char xlator_option_volume_id[4096] = "";

    GF_ASSERT(volinfo);
    GF_ASSERT(brickinfo);

    priv = this->private;
    GF_ASSERT(priv);

    if (brickinfo->snap_status == -1) {
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_SNAPSHOT_PENDING,
               "Snapshot is pending on %s:%s. "
               "Hence not starting the brick",
               brickinfo->hostname, brickinfo->path);
        ret = 0;
        goto out;
    }

    GLUSTERD_GET_BRICK_PIDFILE(pidfile, volinfo, brickinfo, priv);
    if (gf_is_service_running(pidfile, &pid)) {
        goto connect;
    }

    /*
     * There are all sorts of races in the start/stop code that could leave
     * a UNIX-domain socket or RPC-client object associated with a
     * long-dead incarnation of this brick, while the new incarnation is
     * listening on a new socket at the same path and wondering why we
     * haven't shown up.  To avoid the whole mess and be on the safe side,
     * we just blow away anything that might have been left over, and start
     * over again.
     */
    glusterd_set_brick_socket_filepath(volinfo, brickinfo, socketpath,
                                       sizeof(socketpath));
    gf_unlink(socketpath);
    rpc = brickinfo->rpc;
    if (rpc) {
        brickinfo->rpc = NULL;
        conn = &rpc->conn;
        pthread_mutex_lock(&conn->lock);
        if (conn->reconnect) {
            (void)gf_timer_call_cancel(rpc->ctx, conn->reconnect);
            conn->reconnect = NULL;
        }
        pthread_mutex_unlock(&conn->lock);
        rpc_clnt_unref(rpc);
    }

    port = pmap_assign_port(this, brickinfo->port, brickinfo->path);
    if (!port) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_PORTS_EXHAUSTED,
               "All the ports in the range are exhausted, can't start "
               "brick %s for volume %s",
               brickinfo->path, volinfo->volname);
        ret = -1;
        goto out;
    }

    /* Build the exp_path, before starting the glusterfsd even in
       valgrind mode. Otherwise all the glusterfsd processes start
       writing the valgrind log to the same file.
    */
    GLUSTERD_REMOVE_SLASH_FROM_PATH(brickinfo->path, exp_path);

retry:
    runinit(&runner);

    if (this->ctx->cmd_args.vgtool != _gf_none) {
        /* Run bricks with valgrind. */
        if (volinfo->logdir) {
            len = snprintf(valgrind_logfile, PATH_MAX, "%s/valgrind-%s-%s.log",
                           volinfo->logdir, volinfo->volname, exp_path);
        } else {
            len = snprintf(valgrind_logfile, PATH_MAX,
                           "%s/bricks/valgrind-%s-%s.log", priv->logdir,
                           volinfo->volname, exp_path);
        }
        if ((len < 0) || (len >= PATH_MAX)) {
            ret = -1;
            goto out;
        }

        if (this->ctx->cmd_args.vgtool == _gf_memcheck)
            runner_add_args(&runner, "valgrind", "--leak-check=full",
                            "--trace-children=yes", "--track-origins=yes",
                            NULL);
        else
            runner_add_args(&runner, "valgrind", "--tool=drd", NULL);

        runner_argprintf(&runner, "--log-file=%s", valgrind_logfile);
    }

    if (volinfo->is_snap_volume) {
        len = snprintf(volfile, PATH_MAX, "/%s/%s/%s/%s.%s.%s",
                       GLUSTERD_VOL_SNAP_DIR_PREFIX,
                       volinfo->snapshot->snapname, volinfo->volname,
                       volinfo->volname, brickinfo->hostname, exp_path);
    } else {
        len = snprintf(volfile, PATH_MAX, "%s.%s.%s", volinfo->volname,
                       brickinfo->hostname, exp_path);
    }
    if ((len < 0) || (len >= PATH_MAX)) {
        ret = -1;
        goto out;
    }

    if (volinfo->logdir) {
        len = snprintf(logfile, PATH_MAX, "%s/%s.log", volinfo->logdir,
                       exp_path);
    } else {
        len = snprintf(logfile, PATH_MAX, "%s/bricks/%s.log", priv->logdir,
                       exp_path);
    }
    if ((len < 0) || (len >= PATH_MAX)) {
        ret = -1;
        goto out;
    }

    if (!brickinfo->logfile)
        brickinfo->logfile = gf_strdup(logfile);

    (void)snprintf(glusterd_uuid, 1024, "*-posix.glusterd-uuid=%s",
                   uuid_utoa(MY_UUID));
    runner_add_args(&runner, SBIN_DIR "/glusterfsd", "-s", brickinfo->hostname,
                    "--volfile-id", volfile, "-p", pidfile, "-S", socketpath,
                    "--brick-name", brickinfo->path, "-l", brickinfo->logfile,
                    "--xlator-option", glusterd_uuid, "--process-name", "brick",
                    NULL);

    if (dict_get_str(priv->opts, GLUSTERD_LOCALTIME_LOGGING_KEY,
                     &localtime_logging) == 0) {
        if (strcmp(localtime_logging, "enable") == 0)
            runner_add_arg(&runner, "--localtime-logging");
    }

    if (volinfo->is_snap_volume) {
        /* If the Brick process belongs to a Snapshot brick then set
           the parent Volume ID itself as Volume ID. This is to avoid
           setting xattr when a Snapshot is created. Setting Xattr fails
           in a few file system where Snapshots are not allowed
           to mount as rw */
        ret = glusterd_volinfo_find(volinfo->parent_volname, &parent_volinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
                   "Parent Volume (%s) does not exist ",
                   volinfo->parent_volname);
            goto out;
        }
        (void)snprintf(xlator_option_volume_id, sizeof(xlator_option_volume_id),
                       "*-posix.volume-id=%s",
                       uuid_utoa(parent_volinfo->volume_id));
        runner_add_args(&runner, "--xlator-option", xlator_option_volume_id,
                        NULL);

        (void)snprintf(xlator_option_volume_id, sizeof(xlator_option_volume_id),
                       "%s.volume-id=%s", brickinfo->path,
                       uuid_utoa(parent_volinfo->volume_id));
        runner_add_args(&runner, "--xlator-option", xlator_option_volume_id,
                        NULL);

        /* Snapshot Bricks are read only, disable the posix health check */
        runner_add_args(&runner, "--xlator-option",
                        "*-posix.health-check-interval=0", NULL);

        /* Snapshot Bricks are read only, disable the changelog xlator */
        runner_add_args(&runner, "--xlator-option", "*-changelog.changelog=off",
                        NULL);
    }

    runner_add_arg(&runner, "--brick-port");
    if (volinfo->transport_type != GF_TRANSPORT_BOTH_TCP_RDMA) {
        runner_argprintf(&runner, "%d", port);
    } else {
        len = snprintf(rdma_brick_path, sizeof(rdma_brick_path), "%s.rdma",
                       brickinfo->path);
        if ((len < 0) || (len >= sizeof(rdma_brick_path))) {
            ret = -1;
            goto out;
        }
        rdma_port = pmap_assign_port(this, brickinfo->rdma_port,
                                     rdma_brick_path);
        if (!rdma_port) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_PORTS_EXHAUSTED,
                   "All rdma ports in the "
                   "range are exhausted, can't start brick %s for "
                   "volume %s",
                   rdma_brick_path, volinfo->volname);
            ret = -1;
            goto out;
        }
        runner_argprintf(&runner, "%d,%d", port, rdma_port);
        runner_add_arg(&runner, "--xlator-option");
        runner_argprintf(&runner, "%s-server.transport.rdma.listen-port=%d",
                         volinfo->volname, rdma_port);
    }

    if (dict_get_str(volinfo->dict, VKEY_CONFIG_GLOBAL_THREADING,
                     &global_threading) == 0) {
        if ((gf_string2boolean(global_threading, &threading) == 0) &&
            threading) {
            runner_add_arg(&runner, "--global-threading");
        }
    }

    if (this->ctx->cmd_args.io_engine != NULL) {
        runner_add_args(&runner, "--io-engine", this->ctx->cmd_args.io_engine,
                        NULL);
    }

    if (this->ctx->cmd_args.logger == gf_logger_syslog) {
        runner_argprintf(&runner, "--logger=syslog");
    }

    runner_add_arg(&runner, "--xlator-option");
    runner_argprintf(&runner, "%s-server.listen-port=%d", volinfo->volname,
                     port);

    if (dict_get_str(this->options, "transport.socket.bind-address",
                     &bind_address) == 0) {
        runner_add_arg(&runner, "--xlator-option");
        runner_argprintf(&runner, "transport.socket.bind-address=%s",
                         bind_address);
    }

    if (volinfo->transport_type == GF_TRANSPORT_RDMA)
        runner_argprintf(&runner, "--volfile-server-transport=rdma");
    else if (volinfo->transport_type == GF_TRANSPORT_BOTH_TCP_RDMA)
        runner_argprintf(&runner, "--volfile-server-transport=socket,rdma");

    ret = dict_get_str(this->options, "transport.address-family", &inet_family);
    if (!ret) {
        runner_add_arg(&runner, "--xlator-option");
        runner_argprintf(&runner, "transport.address-family=%s", inet_family);
    }

    if (volinfo->memory_accounting)
        runner_add_arg(&runner, "--mem-accounting");

    if (is_brick_mx_enabled())
        runner_add_arg(&runner, "--brick-mux");

    runner_log(&runner, "", GF_LOG_DEBUG, "Starting GlusterFS");

    brickinfo->port = port;
    brickinfo->rdma_port = rdma_port;
    brickinfo->status = GF_BRICK_STARTING;
    brickinfo->port_registered = _gf_false;

    if (wait) {
        synclock_unlock(&priv->big_lock);
        ret = runner_run(&runner);
        synclock_lock(&priv->big_lock);

        if (ret == -EADDRINUSE) {
            /* retry after getting a new port */
            gf_msg(this->name, GF_LOG_WARNING, -ret,
                   GD_MSG_SRC_BRICK_PORT_UNAVAIL,
                   "Port %d is used by other process", port);

            port = pmap_port_alloc(this);
            if (!port) {
                gf_msg(this->name, GF_LOG_CRITICAL, 0, GD_MSG_NO_FREE_PORTS,
                       "Couldn't allocate a port");
                ret = -1;
                goto out;
            }
            gf_msg(this->name, GF_LOG_NOTICE, 0, GD_MSG_RETRY_WITH_NEW_PORT,
                   "Retrying to start brick %s with new port %d",
                   brickinfo->path, port);
            goto retry;
        }
    } else {
        ret = runner_run_nowait(&runner);
    }

    if (ret) {
        brickinfo->port = 0;
        brickinfo->rdma_port = 0;
        goto out;
    }

    ret = glusterd_brickprocess_new(&brick_proc);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICKPROC_NEW_FAILED,
               "Failed to create "
               "new brick process instance");
        goto out;
    }

    brick_proc->port = brickinfo->port;
    cds_list_add_tail(&brick_proc->brick_proc_list, &priv->brick_procs);
    brickinfo->brick_proc = brick_proc;
    cds_list_add_tail(&brickinfo->mux_bricks, &brick_proc->bricks);
    brickinfo->brick_proc = brick_proc;
    brick_proc->brick_count++;

connect:
    ret = glusterd_brick_connect(volinfo, brickinfo, socketpath);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_DISCONNECTED,
               "Failed to connect to brick %s:%s on %s", brickinfo->hostname,
               brickinfo->path, socketpath);
        goto out;
    }

out:
    if (ret)
        brickinfo->status = GF_BRICK_STOPPED;
    return ret;
}

static int32_t
glusterd_brick_disconnect(glusterd_brickinfo_t *brickinfo)
{
    rpc_clnt_t *rpc = NULL;
    glusterd_conf_t *priv = THIS->private;

    GF_ASSERT(brickinfo);

    if (!brickinfo) {
        gf_msg_callingfn("glusterd", GF_LOG_WARNING, EINVAL,
                         GD_MSG_BRICK_NOT_FOUND, "!brickinfo");
        return -1;
    }

    rpc = brickinfo->rpc;
    brickinfo->rpc = NULL;

    if (rpc) {
        glusterd_rpc_clnt_unref(priv, rpc);
    }

    return 0;
}

static gf_boolean_t
unsafe_option(dict_t *this, char *key, data_t *value, void *arg)
{
    /*
     * Certain options are safe because they're already being handled other
     * ways, such as being copied down to the bricks (all auth options) or
     * being made irrelevant (event-threads).  All others are suspect and
     * must be checked in the next function.
     */
    if (fnmatch("*auth*", key, 0) == 0) {
        return _gf_false;
    }

    if (fnmatch("*event-threads", key, 0) == 0) {
        return _gf_false;
    }

    if (fnmatch("*diagnostics.brick-log*", key, 0) == 0) {
        return _gf_false;
    }

    if (fnmatch("*diagnostics.client-log*", key, 0) == 0) {
        return _gf_false;
    }
    if (fnmatch("user.*", key, 0) == 0) {
        return _gf_false;
    }

    return _gf_true;
}

static int
opts_mismatch(dict_t *dict1, char *key, data_t *value1, void *dict2)
{
    data_t *value2 = dict_get(dict2, key);
    int32_t min_len;

    /*
     * If the option is only present on one, we can either look at the
     * default or assume a mismatch.  Looking at the default is pretty
     * hard, because that's part of a structure within each translator and
     * there's no dlopen interface to get at it, so we assume a mismatch.
     * If the user really wants them to match (and for their bricks to be
     * multiplexed, they can always reset the option).
     */
    if (!value2) {
        gf_log(THIS->name, GF_LOG_DEBUG, "missing option %s", key);
        return -1;
    }

    min_len = MIN(value1->len, value2->len);
    if (strncmp(value1->data, value2->data, min_len) != 0) {
        gf_log(THIS->name, GF_LOG_DEBUG, "option mismatch, %s, %s != %s", key,
               value1->data, value2->data);
        return -1;
    }

    return 0;
}

static int
glusterd_brickprocess_delete(glusterd_brick_proc_t *brick_proc)
{
    cds_list_del_init(&brick_proc->brick_proc_list);
    cds_list_del_init(&brick_proc->bricks);

    GF_FREE(brick_proc);

    return 0;
}

int
glusterd_brick_process_remove_brick(glusterd_brickinfo_t *brickinfo,
                                    int *last_brick)
{
    int ret = -1;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    glusterd_brick_proc_t *brick_proc = NULL;

    priv = this->private;
    GF_VALIDATE_OR_GOTO(this->name, priv, out);
    GF_VALIDATE_OR_GOTO(this->name, brickinfo, out);

    brick_proc = brickinfo->brick_proc;
    if (!brick_proc) {
        if (brickinfo->status != GF_BRICK_STARTED) {
            /* this function will be called from gluster_pmap_signout and
             * glusterd_volume_stop_glusterfs. So it is possible to have
             * brick_proc set as null.
             */
            ret = 0;
        }
        goto out;
    }

    GF_VALIDATE_OR_GOTO(this->name, (brick_proc->brick_count > 0), out);

    cds_list_del_init(&brickinfo->mux_bricks);
    brick_proc->brick_count--;

    /* If all bricks have been removed, delete the brick process */
    if (brick_proc->brick_count == 0) {
        if (last_brick != NULL)
            *last_brick = 1;
        ret = glusterd_brickprocess_delete(brick_proc);
        if (ret)
            goto out;
    }
    brickinfo->brick_proc = NULL;
    ret = 0;
out:
    return ret;
}

static int
glusterd_brick_process_add_brick(glusterd_brickinfo_t *brickinfo,
                                 glusterd_brickinfo_t *parent_brickinfo)
{
    int ret = -1;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    glusterd_brick_proc_t *brick_proc = NULL;

    priv = this->private;
    GF_VALIDATE_OR_GOTO(this->name, priv, out);
    GF_VALIDATE_OR_GOTO(this->name, brickinfo, out);

    if (!parent_brickinfo) {
        ret = glusterd_brick_proc_for_port(brickinfo->port, &brick_proc);
        if (ret) {
            ret = glusterd_brickprocess_new(&brick_proc);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICKPROC_NEW_FAILED,
                       "Failed to create "
                       "new brick process instance");
                goto out;
            }

            brick_proc->port = brickinfo->port;

            cds_list_add_tail(&brick_proc->brick_proc_list, &priv->brick_procs);
        }
    } else {
        ret = 0;
        brick_proc = parent_brickinfo->brick_proc;
    }

    cds_list_add_tail(&brickinfo->mux_bricks, &brick_proc->bricks);
    brickinfo->brick_proc = brick_proc;
    brick_proc->brick_count++;
out:
    return ret;
}

/* ret = 0 only when you get a brick process associated with the port
 * ret = -1 otherwise
 */
static int
glusterd_brick_proc_for_port(int port, glusterd_brick_proc_t **brickprocess)
{
    int ret = -1;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    glusterd_brick_proc_t *brick_proc = NULL;

    priv = this->private;
    GF_VALIDATE_OR_GOTO(this->name, priv, out);

    cds_list_for_each_entry(brick_proc, &priv->brick_procs, brick_proc_list)
    {
        if (brick_proc->port == port) {
            *brickprocess = brick_proc;
            ret = 0;
            break;
        }
    }
out:
    return ret;
}

int32_t
glusterd_volume_stop_glusterfs(glusterd_volinfo_t *volinfo,
                               glusterd_brickinfo_t *brickinfo,
                               gf_boolean_t del_brick)
{
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    int ret = -1;
    char *op_errstr = NULL;
    char pidfile[PATH_MAX] = "";
    int last_brick = -1;
    gf_boolean_t graceful_enabled = _gf_false;

    GF_ASSERT(volinfo);
    GF_ASSERT(brickinfo);

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    ret = glusterd_brick_process_remove_brick(brickinfo, &last_brick);
    if (ret) {
        gf_msg_debug(this->name, 0,
                     "Couldn't remove brick from"
                     " brick process");
        goto out;
    }

    if (del_brick)
        cds_list_del_init(&brickinfo->brick_list);

    if (GLUSTERD_STATUS_STARTED == volinfo->status) {
        /*
         * In a post-multiplexing world, even if we're not actually
         * doing any multiplexing, just dropping the RPC connection
         * isn't enough.  There might be many such connections during
         * the brick daemon's lifetime, even if we only consider the
         * management RPC port (because tests etc. might be manually
         * attaching and detaching bricks).  Therefore, we have to send
         * an actual signal instead.
         */
        graceful_enabled = is_brick_graceful_cleanup_enabled(conf->opts);
        if ((is_brick_mx_enabled() && last_brick != 1) || graceful_enabled) {
            ret = send_attach_req(this, brickinfo->rpc, brickinfo->path, NULL,
                                  NULL, GLUSTERD_BRICK_TERMINATE,
                                  graceful_enabled);
            if (ret && brickinfo->status == GF_BRICK_STARTED) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_STOP_FAIL,
                       "Failed to send"
                       " detach request for brick %s",
                       brickinfo->path);
                goto out;
            }
            gf_log(this->name, GF_LOG_INFO,
                   "Detach request for "
                   "brick %s:%s is sent successfully with graceful cleanup %d",
                   brickinfo->hostname, brickinfo->path, graceful_enabled);

        } else {
            gf_msg_debug(this->name, 0,
                         "About to stop glusterfsd"
                         " for brick %s:%s",
                         brickinfo->hostname, brickinfo->path);
            ret = glusterd_brick_terminate(volinfo, brickinfo, NULL, 0,
                                           &op_errstr);
            if (ret && brickinfo->status == GF_BRICK_STARTED) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_STOP_FAIL,
                       "Failed to kill"
                       " the brick %s",
                       brickinfo->path);
                goto out;
            }

            if (op_errstr) {
                GF_FREE(op_errstr);
            }
            if (is_brick_mx_enabled()) {
                /* In case of brick multiplexing we need to make
                 * sure the port is cleaned up from here as the
                 * RPC connection may not have been originated
                 * for the same brick instance
                 */
                pmap_port_remove(this, brickinfo->port, brickinfo->path, NULL,
                                 _gf_true);
            }
        }

        (void)glusterd_brick_disconnect(brickinfo);
        ret = 0;
    }

    GLUSTERD_GET_BRICK_PIDFILE(pidfile, volinfo, brickinfo, conf);
    gf_msg_debug(this->name, 0, "Unlinking pidfile %s", pidfile);
    (void)sys_unlink(pidfile);

    brickinfo->status = GF_BRICK_STOPPED;
    brickinfo->start_triggered = _gf_false;
    brickinfo->brick_proc = NULL;
    if (del_brick)
        glusterd_delete_brick(volinfo, brickinfo);
out:
    return ret;
}

/* Free LINE[0..N-1] and then the LINE buffer.  */
static void
free_lines(char **line, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        GF_FREE(line[i]);
    GF_FREE(line);
}

static char **
glusterd_readin_file(const char *filepath, int *line_count)
{
    int ret = -1;
    int n = 8;
    int counter = 0;
    char buffer[PATH_MAX + 256] = "";
    char **lines = NULL;
    FILE *fp = NULL;
    void *p;

    fp = fopen(filepath, "r");
    if (!fp)
        goto out;

    lines = GF_CALLOC(1, n * sizeof(*lines), gf_gld_mt_charptr);
    if (!lines)
        goto out;

    for (counter = 0; fgets(buffer, sizeof(buffer), fp); counter++) {
        if (counter == n - 1) {
            n *= 2;
            p = GF_REALLOC(lines, n * sizeof(char *));
            if (!p) {
                free_lines(lines, n / 2);
                lines = NULL;
                goto out;
            }
            lines = p;
        }

        lines[counter] = gf_strdup(buffer);
    }

    lines[counter] = NULL;
    /* Reduce allocation to minimal size.  */
    p = GF_REALLOC(lines, (counter + 1) * sizeof(char *));
    if (!p) {
        /* coverity[TAINTED_SCALAR] */
        free_lines(lines, counter);
        lines = NULL;
        goto out;
    }
    lines = p;

    *line_count = counter;
    ret = 0;

out:
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_READIN_FILE_FAILED, "%s",
               strerror(errno));
    if (fp)
        fclose(fp);

    return lines;
}

int
glusterd_compare_lines(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int
glusterd_sort_and_redirect(const char *src_filepath, int dest_fd)
{
    int ret = -1;
    int line_count = 0;
    int counter = 0;
    char **lines = NULL;

    if (!src_filepath || dest_fd < 0)
        goto out;

    lines = glusterd_readin_file(src_filepath, &line_count);
    if (!lines)
        goto out;

    qsort(lines, line_count, sizeof(*lines), glusterd_compare_lines);

    for (counter = 0; lines[counter]; counter++) {
        ret = sys_write(dest_fd, lines[counter], strlen(lines[counter]));
        if (ret < 0)
            goto out;

        GF_FREE(lines[counter]);
    }

    ret = 0;
out:
    GF_FREE(lines);

    return ret;
}

static void
compute_checksum(char *buf, const ssize_t size, uint32_t *checksum)
{
    int ret = -1;
    char *checksum_buf = NULL;

    checksum_buf = (char *)(checksum);

    if (!(*checksum)) {
        checksum_buf[0] = 0xba;
        checksum_buf[1] = 0xbe;
        checksum_buf[2] = 0xb0;
        checksum_buf[3] = 0x0b;
    }

    for (ret = 0; ret < (size - 4); ret += 4) {
        checksum_buf[0] ^= (buf[ret]);
        checksum_buf[1] ^= (buf[ret + 1] << 1);
        checksum_buf[2] ^= (buf[ret + 2] << 2);
        checksum_buf[3] ^= (buf[ret + 3] << 3);
    }

    for (ret = 0; ret <= (size % 4); ret++) {
        checksum_buf[ret] ^= (buf[(size - 4) + ret] << ret);
    }

    return;
}

#define GF_CHECKSUM_BUF_SIZE 1024

static int
get_checksum_for_file(int fd, uint32_t *checksum, int op_version)
{
    int ret = -1;
    char buf[GF_CHECKSUM_BUF_SIZE] = {
        0,
    };

    /* goto first place */
    sys_lseek(fd, 0L, SEEK_SET);
    do {
        ret = sys_read(fd, &buf, GF_CHECKSUM_BUF_SIZE);
        if (ret > 0) {
            if (op_version < GD_OP_VERSION_5_4)
                compute_checksum(buf, GF_CHECKSUM_BUF_SIZE, checksum);
            else
                compute_checksum(buf, ret, checksum);
        }
    } while (ret > 0);

    /* set it back */
    sys_lseek(fd, 0L, SEEK_SET);

    return ret;
}

static int
get_checksum_for_path(char *path, uint32_t *checksum, int op_version)
{
    int ret = -1;
    int fd = -1;

    GF_ASSERT(path);
    GF_ASSERT(checksum);

    fd = open(path, O_RDWR);

    if (fd == -1) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, LG_MSG_PATH_OPEN_FAILED,
                "path=%s", path, NULL);
        goto out;
    }

    ret = get_checksum_for_file(fd, checksum, op_version);

out:
    if (fd != -1)
        sys_close(fd);

    return ret;
}

static int
glusterd_volume_compute_cksum(glusterd_volinfo_t *volinfo, char *cksum_path,
                              char *filepath, gf_boolean_t is_quota_conf,
                              uint32_t *cs)
{
    int32_t ret = -1;
    uint32_t cksum = 0;
    int fd = -1;
    int sort_fd = 0;
    char sort_filepath[PATH_MAX] = "";
    char buf[32];
    gf_boolean_t unlink_sortfile = _gf_false;
    glusterd_conf_t *priv = THIS->private;
    mode_t orig_umask = 0;

    GF_ASSERT(volinfo);
    GF_ASSERT(priv);

    fd = open(cksum_path, O_RDWR | O_APPEND | O_CREAT | O_TRUNC, 0600);
    if (-1 == fd) {
        gf_msg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
               "Unable to open %s,"
               " errno: %d",
               cksum_path, errno);
        ret = -1;
        goto out;
    }

    if (!is_quota_conf) {
        snprintf(sort_filepath, sizeof(sort_filepath), "/tmp/%s.XXXXXX",
                 volinfo->volname);

        orig_umask = umask(S_IRWXG | S_IRWXO);
        sort_fd = mkstemp(sort_filepath);
        umask(orig_umask);
        if (-1 == sort_fd) {
            gf_msg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
                   "Could not generate "
                   "temp file, reason: %s for volume: %s",
                   strerror(errno), volinfo->volname);
            goto out;
        } else {
            unlink_sortfile = _gf_true;
        }

        /* sort the info file, result in sort_filepath */

        ret = glusterd_sort_and_redirect(filepath, sort_fd);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_FILE_OP_FAILED,
                   "sorting info file "
                   "failed");
            goto out;
        }

        ret = sys_close(sort_fd);
        if (ret)
            goto out;

        ret = get_checksum_for_path(sort_filepath, &cksum, priv->op_version);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_CKSUM_GET_FAIL,
                   "unable to get "
                   "checksum for path: %s",
                   sort_filepath);
            goto out;
        }

        ret = snprintf(buf, sizeof(buf), "info=%u\n", cksum);
        ret = sys_write(fd, buf, ret);
        if (ret <= 0) {
            ret = -1;
            goto out;
        }
    } else if (priv->op_version < GD_OP_VERSION_7_0) {
        ret = get_checksum_for_path(filepath, &cksum, priv->op_version);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_CKSUM_GET_FAIL,
                   "unable to get "
                   "checksum for path: %s",
                   filepath);
            goto out;
        }
    }

    ret = get_checksum_for_file(fd, &cksum, priv->op_version);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_CKSUM_GET_FAIL,
               "unable to get checksum for path: %s", filepath);
        goto out;
    }

    *cs = cksum;

out:
    if (fd != -1)
        sys_close(fd);
    if (unlink_sortfile)
        sys_unlink(sort_filepath);
    gf_msg_debug("glusterd", 0, "Returning with %d", ret);

    return ret;
}

int
glusterd_compute_cksum(glusterd_volinfo_t *volinfo, gf_boolean_t is_quota_conf)
{
    int ret = -1;
    uint32_t cs = 0;
    char cksum_path[PATH_MAX] = "";
    char path[PATH_MAX] = "";
    char filepath[PATH_MAX] = "";
    glusterd_conf_t *conf = NULL;
    int32_t len1 = 0;
    int32_t len2 = 0;

    conf = THIS->private;
    GF_ASSERT(conf);

    GLUSTERD_GET_VOLUME_DIR(path, volinfo, conf);

    if (is_quota_conf) {
        len1 = snprintf(cksum_path, sizeof(cksum_path), "%s/%s", path,
                        GLUSTERD_VOL_QUOTA_CKSUM_FILE);
        len2 = snprintf(filepath, sizeof(filepath), "%s/%s", path,
                        GLUSTERD_VOLUME_QUOTA_CONFIG);
    } else {
        len1 = snprintf(cksum_path, sizeof(cksum_path), "%s/%s", path,
                        GLUSTERD_CKSUM_FILE);
        len2 = snprintf(filepath, sizeof(filepath), "%s/%s", path,
                        GLUSTERD_VOLUME_INFO_FILE);
    }
    if ((len1 < 0) || (len2 < 0) || (len1 >= sizeof(cksum_path)) ||
        (len2 >= sizeof(filepath))) {
        goto out;
    }

    ret = glusterd_volume_compute_cksum(volinfo, cksum_path, filepath,
                                        is_quota_conf, &cs);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_CKSUM_COMPUTE_FAIL,
               "Failed to compute checksum "
               "for volume %s",
               volinfo->volname);
        goto out;
    }

    if (is_quota_conf)
        volinfo->quota_conf_cksum = cs;
    else
        volinfo->cksum = cs;

    ret = 0;
out:
    return ret;
}

static int
_add_dict_to_prdict(dict_t *this, char *key, data_t *value, void *data)
{
    glusterd_dict_ctx_t *ctx = data;
    char optkey[64]; /* optkey are usually quite small */
    int ret = -1;

    ret = snprintf(optkey, sizeof(optkey), "%s.%s%d", ctx->prefix,
                   ctx->key_name, ctx->opt_count);
    if (ret < 0 || ret >= sizeof(optkey))
        return -1;
    ret = dict_set_strn(ctx->dict, optkey, ret, key);
    if (ret)
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "option add for %s%d %s", ctx->key_name, ctx->opt_count, key);
    ret = snprintf(optkey, sizeof(optkey), "%s.%s%d", ctx->prefix,
                   ctx->val_name, ctx->opt_count);
    if (ret < 0 || ret >= sizeof(optkey))
        return -1;
    ret = dict_set_strn(ctx->dict, optkey, ret, value->data);
    if (ret)
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "option add for %s%d %s", ctx->val_name, ctx->opt_count,
               value->data);
    ctx->opt_count++;

    return ret;
}

/* The prefix represents the type of volume to be added.
 * It will be "volume" for normal volumes, and snap# like
 * snap1, snap2, for snapshot volumes
 */
int32_t
glusterd_add_volume_to_dict(glusterd_volinfo_t *volinfo, dict_t *dict,
                            int32_t count, char *prefix)
{
    int32_t ret = -1;
    char pfx[32] = ""; /* prefix should be quite small */
    char key[64] = "";
    int keylen;
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_brickinfo_t *ta_brickinfo = NULL;
    int32_t i = 1;
    char *volume_id_str = NULL;
    char *str = NULL;
    glusterd_dict_ctx_t ctx = {0};
    char *rebalance_id_str = NULL;
    char *rb_id_str = NULL;
    xlator_t *this = THIS;

    GF_ASSERT(dict);
    GF_ASSERT(volinfo);
    GF_ASSERT(prefix);

    ret = snprintf(pfx, sizeof(pfx), "%s%d", prefix, count);
    if (ret < 0 || ret >= sizeof(pfx)) {
        ret = -1;
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.name", pfx);
    ret = dict_set_strn(dict, key, keylen, volinfo->volname);
    if (ret)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.type", pfx);
    ret = dict_set_int32n(dict, key, keylen, volinfo->type);
    if (ret)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.brick_count", pfx);
    ret = dict_set_int32n(dict, key, keylen, volinfo->brick_count);
    if (ret)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.version", pfx);
    ret = dict_set_int32n(dict, key, keylen, volinfo->version);
    if (ret)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.status", pfx);
    ret = dict_set_int32n(dict, key, keylen, volinfo->status);
    if (ret)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.sub_count", pfx);
    ret = dict_set_int32n(dict, key, keylen, volinfo->sub_count);
    if (ret)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.subvol_count", pfx);
    ret = dict_set_int32n(dict, key, keylen, volinfo->subvol_count);
    if (ret)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.replica_count", pfx);
    ret = dict_set_int32n(dict, key, keylen, volinfo->replica_count);
    if (ret)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.arbiter_count", pfx);
    ret = dict_set_int32n(dict, key, keylen, volinfo->arbiter_count);
    if (ret)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.thin_arbiter_count", pfx);
    ret = dict_set_int32n(dict, key, keylen, volinfo->thin_arbiter_count);
    if (ret)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.disperse_count", pfx);
    ret = dict_set_int32n(dict, key, keylen, volinfo->disperse_count);
    if (ret)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.redundancy_count", pfx);
    ret = dict_set_int32n(dict, key, keylen, volinfo->redundancy_count);
    if (ret)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.dist_count", pfx);
    ret = dict_set_int32n(dict, key, keylen, volinfo->dist_leaf_count);
    if (ret)
        goto out;

    snprintf(key, sizeof(key), "%s.ckusm", pfx);
    ret = dict_set_int64(dict, key, volinfo->cksum);
    if (ret)
        goto out;

    snprintf(key, sizeof(key), "%s.transport_type", pfx);
    ret = dict_set_uint32(dict, key, volinfo->transport_type);
    if (ret)
        goto out;

    snprintf(key, sizeof(key), "%s.stage_deleted", pfx);
    ret = dict_set_uint32(dict, key, (uint32_t)volinfo->stage_deleted);
    if (ret)
        goto out;

    ret = gd_add_vol_snap_details_to_dict(dict, pfx, volinfo);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "vol snap details", NULL);
        goto out;
    }

    volume_id_str = gf_strdup(uuid_utoa(volinfo->volume_id));
    if (!volume_id_str) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_STRDUP_FAILED,
                "volume id=%s", volinfo->volume_id, NULL);
        ret = -1;
        goto out;
    }
    keylen = snprintf(key, sizeof(key), "%s.volume_id", pfx);
    ret = dict_set_dynstrn(dict, key, keylen, volume_id_str);
    if (ret)
        goto out;
    volume_id_str = NULL;

    keylen = snprintf(key, sizeof(key), "%s.username", pfx);
    str = glusterd_auth_get_username(volinfo);
    if (str) {
        ret = dict_set_dynstrn(dict, key, keylen, gf_strdup(str));
        if (ret)
            goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.password", pfx);
    str = glusterd_auth_get_password(volinfo);
    if (str) {
        ret = dict_set_dynstrn(dict, key, keylen, gf_strdup(str));
        if (ret)
            goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.rebalance", pfx);
    ret = dict_set_int32n(dict, key, keylen, volinfo->rebal.defrag_cmd);
    if (ret)
        goto out;

    rebalance_id_str = gf_strdup(uuid_utoa(volinfo->rebal.rebalance_id));
    if (!rebalance_id_str) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_STRDUP_FAILED,
                "rebalance_id=%s", volinfo->rebal.rebalance_id, NULL);
        ret = -1;
        goto out;
    }
    keylen = snprintf(key, sizeof(key), "%s.rebalance-id", pfx);
    ret = dict_set_dynstrn(dict, key, keylen, rebalance_id_str);
    if (ret)
        goto out;
    rebalance_id_str = NULL;

    snprintf(key, sizeof(key), "%s.rebalance-op", pfx);
    ret = dict_set_uint32(dict, key, volinfo->rebal.op);
    if (ret)
        goto out;

    if (volinfo->rebal.dict) {
        ctx.dict = dict;
        ctx.prefix = pfx;
        ctx.opt_count = 1;
        ctx.key_name = "rebal-dict-key";
        ctx.val_name = "rebal-dict-value";

        dict_foreach(volinfo->rebal.dict, _add_dict_to_prdict, &ctx);
        ctx.opt_count--;
        keylen = snprintf(key, sizeof(key), "volume%d.rebal-dict-count", count);
        ret = dict_set_int32n(dict, key, keylen, ctx.opt_count);
        if (ret)
            goto out;
    }

    ctx.dict = dict;
    ctx.prefix = pfx;
    ctx.opt_count = 1;
    ctx.key_name = "key";
    ctx.val_name = "value";
    GF_ASSERT(volinfo->dict);

    dict_foreach(volinfo->dict, _add_dict_to_prdict, &ctx);
    ctx.opt_count--;
    keylen = snprintf(key, sizeof(key), "%s.opt-count", pfx);
    ret = dict_set_int32n(dict, key, keylen, ctx.opt_count);
    if (ret)
        goto out;

    ctx.dict = dict;
    ctx.prefix = pfx;
    ctx.opt_count = 1;
    ctx.key_name = "secondary-num";
    ctx.val_name = "secondary-val";
    GF_ASSERT(volinfo->gsync_secondaries);

    dict_foreach(volinfo->gsync_secondaries, _add_dict_to_prdict, &ctx);
    ctx.opt_count--;

    keylen = snprintf(key, sizeof(key), "%s.gsync-count", pfx);
    ret = dict_set_int32n(dict, key, keylen, ctx.opt_count);
    if (ret)
        goto out;

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        keylen = snprintf(key, sizeof(key), "%s.brick%d.hostname", pfx, i);
        ret = dict_set_strn(dict, key, keylen, brickinfo->hostname);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "%s.brick%d.path", pfx, i);
        ret = dict_set_strn(dict, key, keylen, brickinfo->path);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "%s.brick%d.decommissioned", pfx,
                          i);
        ret = dict_set_int32n(dict, key, keylen, brickinfo->decommissioned);
        if (ret)
            goto out;

        keylen = snprintf(key, sizeof(key), "%s.brick%d.brick_id", pfx, i);
        ret = dict_set_strn(dict, key, keylen, brickinfo->brick_id);
        if (ret)
            goto out;

        snprintf(key, sizeof(key), "%s.brick%d.uuid", pfx, i);
        ret = dict_set_dynstr_with_alloc(dict, key, uuid_utoa(brickinfo->uuid));
        if (ret)
            goto out;

        snprintf(key, sizeof(key), "%s.brick%d", pfx, i);
        ret = gd_add_brick_snap_details_to_dict(dict, key, brickinfo);
        if (ret)
            goto out;

        i++;
    }

    i = 1;
    if (volinfo->thin_arbiter_count == 1) {
        cds_list_for_each_entry(ta_brickinfo, &volinfo->ta_bricks, brick_list)
        {
            keylen = snprintf(key, sizeof(key), "%s.ta-brick%d.hostname", pfx,
                              i);
            ret = dict_set_strn(dict, key, keylen, ta_brickinfo->hostname);
            if (ret)
                goto out;

            keylen = snprintf(key, sizeof(key), "%s.ta-brick%d.path", pfx, i);
            ret = dict_set_strn(dict, key, keylen, ta_brickinfo->path);
            if (ret)
                goto out;

            keylen = snprintf(key, sizeof(key), "%s.ta-brick%d.decommissioned",
                              pfx, i);
            ret = dict_set_int32n(dict, key, keylen,
                                  ta_brickinfo->decommissioned);
            if (ret)
                goto out;

            keylen = snprintf(key, sizeof(key), "%s.ta-brick%d.brick_id", pfx,
                              i);
            ret = dict_set_strn(dict, key, keylen, ta_brickinfo->brick_id);
            if (ret)
                goto out;

            snprintf(key, sizeof(key), "%s.ta-brick%d.uuid", pfx, i);
            ret = dict_set_dynstr_with_alloc(dict, key,
                                             uuid_utoa(ta_brickinfo->uuid));
            if (ret)
                goto out;

            i++;
        }
    }

    /* Add volume op-versions to dict. This prevents volume inconsistencies
     * in the cluster
     */
    keylen = snprintf(key, sizeof(key), "%s.op-version", pfx);
    ret = dict_set_int32n(dict, key, keylen, volinfo->op_version);
    if (ret)
        goto out;
    keylen = snprintf(key, sizeof(key), "%s.client-op-version", pfx);
    ret = dict_set_int32n(dict, key, keylen, volinfo->client_op_version);
    if (ret)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.quota-xattr-version", pfx);
    ret = dict_set_int32n(dict, key, keylen, volinfo->quota_xattr_version);
out:
    GF_FREE(volume_id_str);
    GF_FREE(rebalance_id_str);
    GF_FREE(rb_id_str);

    if (key[0] != '\0' && ret != 0)
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "Key=%s", key, NULL);
    gf_msg_debug(this->name, 0, "Returning with %d", ret);
    return ret;
}

/* The prefix represents the type of volume to be added.
 * It will be "volume" for normal volumes, and snap# like
 * snap1, snap2, for snapshot volumes
 */
int
glusterd_vol_add_quota_conf_to_dict(glusterd_volinfo_t *volinfo, dict_t *load,
                                    int vol_idx, char *prefix)
{
    int fd = -1;
    unsigned char buf[16] = "";
    char key[64];
    char key_prefix[32];
    int gfid_idx = 0;
    int ret = -1;
    xlator_t *this = THIS;
    char type = 0;
    float version = 0.0f;

    GF_ASSERT(prefix);

    ret = glusterd_store_create_quota_conf_sh_on_absence(volinfo);
    if (ret)
        goto out;

    fd = open(volinfo->quota_conf_shandle->path, O_RDONLY);
    if (fd == -1) {
        ret = -1;
        goto out;
    }

    ret = quota_conf_read_version(fd, &version);
    if (ret)
        goto out;

    ret = snprintf(key_prefix, sizeof(key_prefix), "%s%d", prefix, vol_idx);
    if (ret < 0 || ret >= sizeof(key_prefix)) {
        ret = -1;
        goto out;
    }
    for (gfid_idx = 0;; gfid_idx++) {
        ret = quota_conf_read_gfid(fd, buf, &type, version);
        if (ret == 0) {
            break;
        } else if (ret < 0) {
            gf_msg(this->name, GF_LOG_CRITICAL, 0, GD_MSG_QUOTA_CONF_CORRUPT,
                   "Quota "
                   "configuration store may be corrupt.");
            goto out;
        }

        snprintf(key, sizeof(key) - 1, "%s.gfid%d", key_prefix, gfid_idx);
        ret = dict_set_dynstr_with_alloc(load, key, uuid_utoa(buf));
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                    "Key=%s", key, NULL);
            goto out;
        }

        snprintf(key, sizeof(key) - 1, "%s.gfid-type%d", key_prefix, gfid_idx);
        ret = dict_set_int8(load, key, type);
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                    "Key=%s", key, NULL);
            goto out;
        }
    }

    ret = snprintf(key, sizeof(key), "%s.gfid-count", key_prefix);
    ret = dict_set_int32n(load, key, ret, gfid_idx);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "Key=%s", key, NULL);
        goto out;
    }

    snprintf(key, sizeof(key), "%s.quota-cksum", key_prefix);
    ret = dict_set_uint32(load, key, volinfo->quota_conf_cksum);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "Key=%s", key, NULL);
        goto out;
    }

    snprintf(key, sizeof(key), "%s.quota-version", key_prefix);
    ret = dict_set_uint32(load, key, volinfo->quota_conf_version);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "Key=%s", key, NULL);
        goto out;
    }

    ret = 0;
out:
    if (fd != -1)
        sys_close(fd);
    return ret;
}

static void *
glusterd_add_bulk_volumes_create_thread(void *data)
{
    int32_t ret = -1;
    glusterd_conf_t *priv = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    int32_t count = 0;
    xlator_t *this = NULL;
    glusterd_add_dict_args_t *arg = NULL;
    dict_t *dict = NULL;
    int start = 0;
    int end = 0;

    GF_ASSERT(data);

    arg = data;
    dict = arg->voldict;
    start = arg->start;
    end = arg->end;
    this = arg->this;
    THIS = arg->this;
    priv = this->private;
    GF_ASSERT(priv);

    cds_list_for_each_entry(volinfo, &priv->volumes, vol_list)
    {
        count++;

        /* Skip volumes if index count is less than start
           index to handle volume for specific thread
        */
        if (count < start)
            continue;

        /* No need to process volume if index count is greater
           than end index
        */
        if (count > end)
            break;

        ret = glusterd_add_volume_to_dict(volinfo, dict, count, "volume");
        if (ret)
            goto out;
        if (!dict_get_sizen(volinfo->dict, VKEY_FEATURES_QUOTA))
            continue;
        ret = glusterd_vol_add_quota_conf_to_dict(volinfo, dict, count,
                                                  "volume");
        if (ret)
            goto out;
    }

out:
    GF_ATOMIC_DEC(priv->thread_count);
    free(arg);
    return NULL;
}

static int
glusterd_dict_searialize(dict_t *dict_arr[], unsigned int count,
                         unsigned int totcount, char *buf)
{
    int i = 0;
    uint32_t keylen = 0;
    uint32_t netword = 0;
    data_pair_t *pair = NULL;
    unsigned int dict_count = 0;
    int ret = 0;

    netword = htobe32(totcount);
    memcpy(buf, &netword, sizeof(netword));
    buf += DICT_HDR_LEN;

    for (i = 0; i < count; i++) {
        if (dict_arr[i]) {
            dict_count = dict_arr[i]->count;
            pair = dict_arr[i]->members_list;
            while (dict_count) {
                if (!pair) {
                    gf_msg("glusterd", GF_LOG_ERROR, 0,
                           LG_MSG_PAIRS_LESS_THAN_COUNT,
                           "less than count data pairs found!");
                    ret = -1;
                    goto out;
                }

                keylen = strlen(pair->key);
                netword = htobe32(keylen);
                memcpy(buf, &netword, sizeof(netword));
                buf += DICT_DATA_HDR_KEY_LEN;
                if (!pair->value) {
                    gf_msg("glusterd", GF_LOG_ERROR, 0, LG_MSG_NULL_PTR,
                           "pair->value is null!");
                    ret = -1;
                    goto out;
                }

                netword = htobe32(pair->value->len);
                memcpy(buf, &netword, sizeof(netword));
                buf += DICT_DATA_HDR_VAL_LEN;

                memcpy(buf, pair->key, keylen);
                buf += keylen;
                *buf++ = '\0';

                if (pair->value->data) {
                    memcpy(buf, pair->value->data, pair->value->len);
                    buf += pair->value->len;
                }

                pair = pair->next;
                dict_count--;
            }
        }
    }

out:
    for (i = 0; i < count; i++) {
        if (dict_arr[i])
            dict_unref(dict_arr[i]);
    }
    return ret;
}

static int
glusterd_dict_arr_serialize(dict_t *dict_arr[], unsigned int count, char **buf,
                            u_int *length)
{
    ssize_t len = 0;
    int i = 0;
    unsigned int totcount = 0;
    int ret = 0;

    for (i = 0; i < count; i++) {
        if (dict_arr[i]) {
            LOCK(&dict_arr[i]->lock);
            {
                len += dict_serialized_length_lk(dict_arr[i]);
                totcount += dict_arr[i]->count;
            }
            UNLOCK(&dict_arr[i]->lock);
        }
    }

    // Subtract HDR_LEN except one dictionary
    len = len - ((count - 1) * DICT_HDR_LEN);

    *buf = GF_MALLOC(len, gf_common_mt_char);
    if (*buf == NULL) {
        ret = -ENOMEM;
        goto out;
    }

    if (length != NULL) {
        *length = len;
    }

    ret = glusterd_dict_searialize(dict_arr, count, totcount, *buf);

out:
    return ret;
}

int32_t
glusterd_add_volumes_to_export_dict(dict_t *peer_data, char **buf,
                                    u_int *length)
{
    int32_t ret = -1;
    dict_t *dict_arr[128] = {
        0,
    };
    glusterd_conf_t *priv = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    int32_t count = 0;
    glusterd_dict_ctx_t ctx = {0};
    xlator_t *this = THIS;
    unsigned int totthread = 0;
    int volcnt = 0;
    int start = 1;
    int endindex = 0;
    int vol_per_thread_limit = 0;
    glusterd_add_dict_args_t *arg = NULL;
    pthread_t th_id = {
        0,
    };
    int th_ret = 0;
    int i = 0;

    priv = this->private;
    GF_ASSERT(priv);

    /* Count the total number of volumes */
    cds_list_for_each_entry(volinfo, &priv->volumes, vol_list) volcnt++;

    get_gd_vol_thread_limit(&vol_per_thread_limit);

    if ((vol_per_thread_limit == 1) || (vol_per_thread_limit == 0) ||
        (vol_per_thread_limit > 100)) {
        totthread = 0;
    } else {
        totthread = volcnt / vol_per_thread_limit;
        if (totthread) {
            endindex = volcnt % vol_per_thread_limit;
            if (endindex)
                totthread++;
        }
    }

    if (totthread == 0) {
        cds_list_for_each_entry(volinfo, &priv->volumes, vol_list)
        {
            count++;
            ret = glusterd_add_volume_to_dict(volinfo, peer_data, count,
                                              "volume");
            if (ret)
                goto out;

            if (!dict_get_sizen(volinfo->dict, VKEY_FEATURES_QUOTA))
                continue;

            ret = glusterd_vol_add_quota_conf_to_dict(volinfo, peer_data, count,
                                                      "volume");
            if (ret)
                goto out;
        }
    } else {
        for (i = 0; i < totthread; i++) {
            arg = calloc(1, sizeof(*arg));
            dict_arr[i] = dict_new();
            arg->this = this;
            arg->voldict = dict_arr[i];
            arg->start = start;
            if ((i + 1) != totthread) {
                arg->end = ((i + 1) * vol_per_thread_limit);
            } else {
                arg->end = (((i + 1) * vol_per_thread_limit) + endindex);
            }
            th_ret = gf_thread_create_detached(
                &th_id, glusterd_add_bulk_volumes_create_thread, arg,
                "bulkvoldict");
            if (th_ret) {
                gf_log(this->name, GF_LOG_ERROR,
                       "glusterd_add_bulk_volume %s"
                       " thread creation failed",
                       "bulkvoldict");
                free(arg);
                goto out;
            }

            start = start + vol_per_thread_limit;
            GF_ATOMIC_INC(priv->thread_count);
            gf_log(this->name, GF_LOG_INFO,
                   "Create thread %d to populate dict data for volume"
                   " start index is %d end index is %d",
                   (i + 1), arg->start, arg->end);
        }
        while (GF_ATOMIC_GET(priv->thread_count)) {
            sleep(1);
        }

        gf_log(this->name, GF_LOG_INFO,
               "Finished dictionary population in all threads");
    }

    ret = dict_set_int32_sizen(peer_data, "count", volcnt);
    if (ret)
        goto out;

    ctx.dict = peer_data;
    ctx.prefix = "global";
    ctx.opt_count = 1;
    ctx.key_name = "key";
    ctx.val_name = "val";
    dict_foreach(priv->opts, _add_dict_to_prdict, &ctx);
    ctx.opt_count--;
    ret = dict_set_int32_sizen(peer_data, "global-opt-count", ctx.opt_count);
    if (ret)
        goto out;

    if (totthread) {
        gf_log(this->name, GF_LOG_INFO,
               "Merged multiple dictionaries into a single one");
        dict_arr[totthread++] = dict_ref(peer_data);
        ret = glusterd_dict_arr_serialize(dict_arr, totthread, buf, length);
        gf_log(this->name, GF_LOG_INFO, "Serialize dictionary data returned %d",
               ret);
    }

out:

    gf_msg_trace(this->name, 0, "Returning %d", ret);
    return ret;
}

static int32_t
glusterd_compare_friend_volume(dict_t *peer_data,
                               glusterd_friend_synctask_args_t *arg,
                               int32_t count, int32_t *status, char *hostname)
{
    int32_t ret = -1;
    char key[64] = "";
    char key_prefix[32];
    int keylen;
    glusterd_volinfo_t *volinfo = NULL;
    char *volname = NULL;
    uint32_t cksum = 0;
    uint32_t quota_cksum = 0;
    uint32_t quota_version = 0;
    uint32_t stage_deleted = 0;
    int32_t version = 0;
    xlator_t *this = THIS;

    GF_ASSERT(peer_data);
    GF_ASSERT(arg);
    GF_ASSERT(status);

    snprintf(key_prefix, sizeof(key_prefix), "volume%d", count);
    keylen = snprintf(key, sizeof(key), "%s.name", key_prefix);
    ret = dict_get_strn(arg->peer_ver_data, key, keylen, &volname);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                "Key=%s is NULL in peer_ver_data", key, NULL);
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        snprintf(key, sizeof(key), "%s.stage_deleted", key_prefix);
        ret = dict_get_uint32(peer_data, key, &stage_deleted);
        /* stage_deleted = 1 means the volume is still in the process of
         * deleting a volume, so we shouldn't be trying to create a
         * fresh volume here which would lead to a stale entry
         */
        if (!ret && stage_deleted == 0)
            *status = GLUSTERD_VOL_COMP_UPDATE_REQ;
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.version", key_prefix);
    ret = dict_get_int32n(arg->peer_ver_data, key, keylen, &version);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                "Key=%s is NULL in peer_ver_data", key, NULL);
        goto out;
    }

    if (version > volinfo->version) {
        // Mismatch detected
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_VOL_VERS_MISMATCH,
               "Version of volume %s differ. local version = %d, "
               "remote version = %d on peer %s",
               volinfo->volname, volinfo->version, version, hostname);
        GF_ATOMIC_INIT(volinfo->volpeerupdate, 1);
        *status = GLUSTERD_VOL_COMP_UPDATE_REQ;
        goto out;
    } else if (version < volinfo->version) {
        *status = GLUSTERD_VOL_COMP_SCS;
        goto out;
    }

    // Now, versions are same, compare cksums.
    //
    snprintf(key, sizeof(key), "%s.ckusm", key_prefix);
    ret = dict_get_uint32(arg->peer_ver_data, key, &cksum);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                "Key=%s is NULL in peer_ver_data", key, NULL);
        goto out;
    }

    if (cksum != volinfo->cksum) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_CKSUM_VERS_MISMATCH,
               "Version of Cksums %s differ. local cksum = %u, remote "
               "cksum = %u on peer %s",
               volinfo->volname, volinfo->cksum, cksum, hostname);
        *status = GLUSTERD_VOL_COMP_RJT;
        goto out;
    }

    if (!dict_get_sizen(volinfo->dict, VKEY_FEATURES_QUOTA))
        goto skip_quota;

    snprintf(key, sizeof(key), "%s.quota-version", key_prefix);
    ret = dict_get_uint32(arg->peer_ver_data, key, &quota_version);
    if (ret) {
        gf_msg_debug(this->name, 0,
                     "quota-version key absent for"
                     " volume %s in peer %s's response",
                     volinfo->volname, hostname);
    } else {
        if (quota_version > volinfo->quota_conf_version) {
            // Mismatch detected
            gf_msg(this->name, GF_LOG_INFO, 0,
                   GD_MSG_QUOTA_CONFIG_VERS_MISMATCH,
                   "Quota configuration versions of volume %s "
                   "differ. local version = %d, remote version = "
                   "%d on peer %s",
                   volinfo->volname, volinfo->quota_conf_version, quota_version,
                   hostname);
            GF_ATOMIC_INIT(volinfo->volpeerupdate, 1);
            *status = GLUSTERD_VOL_COMP_UPDATE_REQ;
            goto out;
        } else if (quota_version < volinfo->quota_conf_version) {
            *status = GLUSTERD_VOL_COMP_SCS;
            goto out;
        }
    }

    // Now, versions are same, compare cksums.
    //
    snprintf(key, sizeof(key), "%s.quota-cksum", key_prefix);
    ret = dict_get_uint32(arg->peer_ver_data, key, &quota_cksum);
    if (ret) {
        gf_msg_debug(this->name, 0,
                     "quota checksum absent for "
                     "volume %s in peer %s's response",
                     volinfo->volname, hostname);
    } else {
        if (quota_cksum != volinfo->quota_conf_cksum) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   GD_MSG_QUOTA_CONFIG_CKSUM_MISMATCH,
                   "Cksums of "
                   "quota configuration of volume %s differ. local"
                   " cksum = %u, remote  cksum = %u on peer %s",
                   volinfo->volname, volinfo->quota_conf_cksum, quota_cksum,
                   hostname);
            *status = GLUSTERD_VOL_COMP_RJT;
            goto out;
        }
    }

skip_quota:
    *status = GLUSTERD_VOL_COMP_SCS;

out:
    if (*status == GLUSTERD_VOL_COMP_UPDATE_REQ) {
        /*Set the status to ensure volume is updated on the peer
         */
        arg->status_arr[(count / 64)] ^= 1UL << (count % 64);
    }

    if (*status == GLUSTERD_VOL_COMP_RJT) {
        gf_event(EVENT_COMPARE_FRIEND_VOLUME_FAILED, "volume=%s",
                 volinfo->volname);
    }
    gf_msg_debug(this->name, 0, "Returning with ret: %d, status: %d", ret,
                 *status);
    return ret;
}

static int32_t
import_prdict_dict(dict_t *peer_data, dict_t *dst_dict, char *key_prefix,
                   char *value_prefix, int opt_count, char *prefix)
{
    char key[512] = "";
    int keylen;
    int32_t ret = 0;
    int i = 1;
    char *opt_key = NULL;
    char *opt_val = NULL;
    char *dup_opt_val = NULL;
    char msg[2048] = "";

    while (i <= opt_count) {
        keylen = snprintf(key, sizeof(key), "%s.%s%d", prefix, key_prefix, i);
        ret = dict_get_strn(peer_data, key, keylen, &opt_key);
        if (ret) {
            snprintf(msg, sizeof(msg),
                     "Volume dict key not "
                     "specified");
            goto out;
        }

        keylen = snprintf(key, sizeof(key), "%s.%s%d", prefix, value_prefix, i);
        ret = dict_get_strn(peer_data, key, keylen, &opt_val);
        if (ret) {
            snprintf(msg, sizeof(msg),
                     "Volume dict value not "
                     "specified");
            goto out;
        }
        dup_opt_val = gf_strdup(opt_val);
        if (!dup_opt_val) {
            ret = -1;
            goto out;
        }
        ret = dict_set_dynstr(dst_dict, opt_key, dup_opt_val);
        if (ret) {
            snprintf(msg, sizeof(msg),
                     "Volume set %s %s "
                     "unsuccessful",
                     opt_key, dup_opt_val);
            goto out;
        }
        i++;
    }

out:
    if (msg[0])
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_IMPORT_PRDICT_DICT, "%s",
               msg);
    gf_msg_debug("glusterd", 0, "Returning with %d", ret);
    return ret;
}

static int
glusterd_restart_rebalance(glusterd_conf_t *conf)
{
    glusterd_volinfo_t *volinfo = NULL;
    int ret = 0;

    cds_list_for_each_entry(volinfo, &conf->volumes, vol_list)
    {
        glusterd_restart_rebalance_for_volume(volinfo);
    }
    return ret;
}

int
glusterd_spawn_daemons(void *opaque)
{
    glusterd_conf_t *conf = THIS->private;
    int ret = -1;

    /* glusterd_restart_brick() will take the sync_lock. */
    glusterd_restart_bricks(NULL);
    glusterd_restart_gsyncds(conf);
    glusterd_restart_rebalance(conf);
    ret = glusterd_snapdsvc_restart();
    ret = glusterd_gfproxydsvc_restart();
    ret = glusterd_shdsvc_restart();
    return ret;
}

static int32_t
glusterd_import_friend_volume_opts(dict_t *peer_data, int count,
                                   glusterd_volinfo_t *volinfo, char *prefix)
{
    char key[64];
    int keylen;
    int32_t ret = -1;
    int opt_count = 0;
    char msg[2048] = "";
    char volume_prefix[32];

    GF_ASSERT(peer_data);
    GF_ASSERT(volinfo);

    snprintf(volume_prefix, sizeof(volume_prefix), "%s%d", prefix, count);

    keylen = snprintf(key, sizeof(key), "%s.opt-count", volume_prefix);
    ret = dict_get_int32n(peer_data, key, keylen, &opt_count);
    if (ret) {
        snprintf(msg, sizeof(msg),
                 "Volume option count not "
                 "specified for %s",
                 volinfo->volname);
        goto out;
    }

    ret = import_prdict_dict(peer_data, volinfo->dict, "key", "value",
                             opt_count, volume_prefix);
    if (ret) {
        snprintf(msg, sizeof(msg),
                 "Unable to import options dict "
                 "specified for %s",
                 volinfo->volname);
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.gsync-count", volume_prefix);
    ret = dict_get_int32n(peer_data, key, keylen, &opt_count);
    if (ret) {
        snprintf(msg, sizeof(msg),
                 "Gsync count not "
                 "specified for %s",
                 volinfo->volname);
        goto out;
    }

    ret = import_prdict_dict(peer_data, volinfo->gsync_secondaries,
                             "secondary-num", "secondary-val", opt_count,
                             volume_prefix);
    if (ret) {
        snprintf(msg, sizeof(msg),
                 "Unable to import gsync sessions "
                 "specified for %s",
                 volinfo->volname);
        goto out;
    }

out:
    if (msg[0])
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOL_OPTS_IMPORT_FAIL, "%s",
               msg);
    gf_msg_debug("glusterd", 0, "Returning with %d", ret);
    return ret;
}

static int32_t
glusterd_import_new_ta_brick(dict_t *peer_data, int32_t vol_count,
                             int32_t brick_count,
                             glusterd_brickinfo_t **ta_brickinfo, char *prefix)
{
    char key[128];
    char key_prefix[64];
    int keylen;
    int ret = -1;
    char *hostname = NULL;
    char *path = NULL;
    char *brick_id = NULL;
    int decommissioned = 0;
    glusterd_brickinfo_t *new_ta_brickinfo = NULL;
    char msg[256] = "";
    char *brick_uuid_str = NULL;

    GF_ASSERT(peer_data);
    GF_ASSERT(vol_count >= 0);
    GF_ASSERT(ta_brickinfo);
    GF_ASSERT(prefix);

    ret = snprintf(key_prefix, sizeof(key_prefix), "%s%d.ta-brick%d", prefix,
                   vol_count, brick_count);

    if (ret < 0 || ret >= sizeof(key_prefix)) {
        ret = -1;
        snprintf(msg, sizeof(msg), "key_prefix too long");
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.hostname", key_prefix);
    ret = dict_get_strn(peer_data, key, keylen, &hostname);
    if (ret) {
        snprintf(msg, sizeof(msg), "%s missing in payload", key);
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.path", key_prefix);
    ret = dict_get_strn(peer_data, key, keylen, &path);
    if (ret) {
        snprintf(msg, sizeof(msg), "%s missing in payload", key);
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.brick_id", key_prefix);
    ret = dict_get_strn(peer_data, key, keylen, &brick_id);

    keylen = snprintf(key, sizeof(key), "%s.decommissioned", key_prefix);
    ret = dict_get_int32n(peer_data, key, keylen, &decommissioned);
    if (ret) {
        /* For backward compatibility */
        ret = 0;
    }

    ret = glusterd_brickinfo_new(&new_ta_brickinfo);
    if (ret)
        goto out;

    ret = snprintf(new_ta_brickinfo->path, sizeof(new_ta_brickinfo->path), "%s",
                   path);
    if (ret < 0 || ret >= sizeof(new_ta_brickinfo->path)) {
        ret = -1;
        goto out;
    }
    ret = snprintf(new_ta_brickinfo->hostname,
                   sizeof(new_ta_brickinfo->hostname), "%s", hostname);
    if (ret < 0 || ret >= sizeof(new_ta_brickinfo->hostname)) {
        ret = -1;
        goto out;
    }
    new_ta_brickinfo->decommissioned = decommissioned;
    if (brick_id)
        (void)snprintf(new_ta_brickinfo->brick_id,
                       sizeof(new_ta_brickinfo->brick_id), "%s", brick_id);
    keylen = snprintf(key, sizeof(key), "%s.uuid", key_prefix);
    ret = dict_get_strn(peer_data, key, keylen, &brick_uuid_str);
    if (ret)
        goto out;
    gf_uuid_parse(brick_uuid_str, new_ta_brickinfo->uuid);

    *ta_brickinfo = new_ta_brickinfo;

out:
    if (msg[0]) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_BRICK_IMPORT_FAIL, "%s",
               msg);
        gf_event(EVENT_IMPORT_BRICK_FAILED, "peer=%s;ta-brick=%s",
                 new_ta_brickinfo->hostname, new_ta_brickinfo->path);
    }
    gf_msg_debug("glusterd", 0, "Returning with %d", ret);
    return ret;
}

/* The prefix represents the type of volume to be added.
 * It will be "volume" for normal volumes, and snap# like
 * snap1, snap2, for snapshot volumes
 */
static int32_t
glusterd_import_new_brick(dict_t *peer_data, int32_t vol_count,
                          int32_t brick_count, glusterd_brickinfo_t **brickinfo,
                          char *prefix)
{
    char key[128];
    char key_prefix[64];
    int keylen;
    int ret = -1;
    char *hostname = NULL;
    char *path = NULL;
    char *brick_id = NULL;
    int decommissioned = 0;
    glusterd_brickinfo_t *new_brickinfo = NULL;
    char msg[256] = "";
    char *brick_uuid_str = NULL;

    GF_ASSERT(peer_data);
    GF_ASSERT(vol_count >= 0);
    GF_ASSERT(brickinfo);
    GF_ASSERT(prefix);

    ret = snprintf(key_prefix, sizeof(key_prefix), "%s%d.brick%d", prefix,
                   vol_count, brick_count);
    if (ret < 0 || ret >= sizeof(key_prefix)) {
        ret = -1;
        snprintf(msg, sizeof(msg), "key_prefix too long");
        goto out;
    }
    keylen = snprintf(key, sizeof(key), "%s.hostname", key_prefix);
    ret = dict_get_strn(peer_data, key, keylen, &hostname);
    if (ret) {
        snprintf(msg, sizeof(msg), "%s missing in payload", key);
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.path", key_prefix);
    ret = dict_get_strn(peer_data, key, keylen, &path);
    if (ret) {
        snprintf(msg, sizeof(msg), "%s missing in payload", key);
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.brick_id", key_prefix);
    ret = dict_get_strn(peer_data, key, keylen, &brick_id);

    keylen = snprintf(key, sizeof(key), "%s.decommissioned", key_prefix);
    ret = dict_get_int32n(peer_data, key, keylen, &decommissioned);
    if (ret) {
        /* For backward compatibility */
        ret = 0;
    }

    ret = glusterd_brickinfo_new(&new_brickinfo);
    if (ret)
        goto out;

    ret = snprintf(new_brickinfo->path, sizeof(new_brickinfo->path), "%s",
                   path);
    if (ret < 0 || ret >= sizeof(new_brickinfo->path)) {
        ret = -1;
        goto out;
    }
    ret = snprintf(new_brickinfo->hostname, sizeof(new_brickinfo->hostname),
                   "%s", hostname);
    if (ret < 0 || ret >= sizeof(new_brickinfo->hostname)) {
        ret = -1;
        goto out;
    }
    new_brickinfo->decommissioned = decommissioned;
    if (brick_id)
        (void)snprintf(new_brickinfo->brick_id, sizeof(new_brickinfo->brick_id),
                       "%s", brick_id);

    ret = gd_import_new_brick_snap_details(peer_data, key_prefix,
                                           new_brickinfo);
    if (ret)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.uuid", key_prefix);
    ret = dict_get_strn(peer_data, key, keylen, &brick_uuid_str);
    if (ret)
        goto out;
    gf_uuid_parse(brick_uuid_str, new_brickinfo->uuid);

    *brickinfo = new_brickinfo;
out:
    if (msg[0]) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_BRICK_IMPORT_FAIL, "%s",
               msg);
        if (new_brickinfo)
            gf_event(EVENT_IMPORT_BRICK_FAILED, "peer=%s;brick=%s",
                     new_brickinfo->hostname, new_brickinfo->path);
    }
    gf_msg_debug("glusterd", 0, "Returning with %d", ret);
    return ret;
}

/* The prefix represents the type of volume to be added.
 * It will be "volume" for normal volumes, and snap# like
 * snap1, snap2, for snapshot volumes
 */
static int32_t
glusterd_import_bricks(dict_t *peer_data, int32_t vol_count,
                       glusterd_volinfo_t *new_volinfo, char *prefix)
{
    int ret = -1;
    int brick_count = 1;
    int ta_brick_count = 1;
    int brickid = 0;
    glusterd_brickinfo_t *new_brickinfo = NULL;
    glusterd_brickinfo_t *new_ta_brickinfo = NULL;

    GF_ASSERT(peer_data);
    GF_ASSERT(vol_count >= 0);
    GF_ASSERT(new_volinfo);
    GF_ASSERT(prefix);
    while (brick_count <= new_volinfo->brick_count) {
        ret = glusterd_import_new_brick(peer_data, vol_count, brick_count,
                                        &new_brickinfo, prefix);
        if (ret)
            goto out;
        if (new_brickinfo->brick_id[0] == '\0')
            /*We were probed from a peer having op-version
             less than GD_OP_VER_PERSISTENT_AFR_XATTRS*/
            GLUSTERD_ASSIGN_BRICKID_TO_BRICKINFO(new_brickinfo, new_volinfo,
                                                 brickid++);
        cds_list_add_tail(&new_brickinfo->brick_list, &new_volinfo->bricks);
        brick_count++;
    }

    if (new_volinfo->thin_arbiter_count == 1) {
        while (ta_brick_count <= new_volinfo->subvol_count) {
            ret = glusterd_import_new_ta_brick(peer_data, vol_count,
                                               ta_brick_count,
                                               &new_ta_brickinfo, prefix);
            if (ret)
                goto out;
            cds_list_add_tail(&new_ta_brickinfo->brick_list,
                              &new_volinfo->ta_bricks);
            ta_brick_count++;
        }
    }
    ret = 0;
out:
    gf_msg_debug("glusterd", 0, "Returning with %d", ret);
    return ret;
}

/* The prefix represents the type of volume to be added.
 * It will be "volume" for normal volumes, and snap# like
 * snap1, snap2, for snapshot volumes
 */
int
glusterd_import_quota_conf(dict_t *peer_data, int vol_idx,
                           glusterd_volinfo_t *new_volinfo, char *prefix)
{
    int gfid_idx = 0;
    int gfid_count = 0;
    int ret = -1;
    int fd = -1;
    char key[128];
    char key_prefix[64];
    int keylen;
    char *gfid_str = NULL;
    uuid_t gfid = {
        0,
    };
    xlator_t *this = THIS;
    int8_t gfid_type = 0;

    GF_ASSERT(peer_data);
    GF_ASSERT(prefix);

    if (!glusterd_is_volume_quota_enabled(new_volinfo)) {
        (void)glusterd_clean_up_quota_store(new_volinfo);
        return 0;
    }

    ret = glusterd_store_create_quota_conf_sh_on_absence(new_volinfo);
    if (ret)
        goto out;

    fd = gf_store_mkstemp(new_volinfo->quota_conf_shandle);
    if (fd < 0) {
        ret = -1;
        goto out;
    }

    ret = snprintf(key_prefix, sizeof(key_prefix), "%s%d", prefix, vol_idx);
    if (ret < 0 || ret >= sizeof(key_prefix)) {
        ret = -1;
        gf_msg_debug(this->name, 0, "Failed to set key_prefix for quota conf");
        goto out;
    }
    snprintf(key, sizeof(key), "%s.quota-cksum", key_prefix);
    ret = dict_get_uint32(peer_data, key, &new_volinfo->quota_conf_cksum);
    if (ret)
        gf_msg_debug(this->name, 0, "Failed to get quota cksum");

    snprintf(key, sizeof(key), "%s.quota-version", key_prefix);
    ret = dict_get_uint32(peer_data, key, &new_volinfo->quota_conf_version);
    if (ret)
        gf_msg_debug(this->name, 0,
                     "Failed to get quota "
                     "version");

    keylen = snprintf(key, sizeof(key), "%s.gfid-count", key_prefix);
    ret = dict_get_int32n(peer_data, key, keylen, &gfid_count);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                "Key=%s", key, NULL);
        goto out;
    }

    ret = glusterd_quota_conf_write_header(fd);
    if (ret)
        goto out;

    for (gfid_idx = 0; gfid_idx < gfid_count; gfid_idx++) {
        keylen = snprintf(key, sizeof(key) - 1, "%s.gfid%d", key_prefix,
                          gfid_idx);
        ret = dict_get_strn(peer_data, key, keylen, &gfid_str);
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                    "Key=%s", key, NULL);
            goto out;
        }

        snprintf(key, sizeof(key) - 1, "%s.gfid-type%d", key_prefix, gfid_idx);
        ret = dict_get_int8(peer_data, key, &gfid_type);
        if (ret)
            gfid_type = GF_QUOTA_CONF_TYPE_USAGE;

        gf_uuid_parse(gfid_str, gfid);
        ret = glusterd_quota_conf_write_gfid(fd, gfid, (char)gfid_type);
        if (ret < 0) {
            gf_msg(this->name, GF_LOG_CRITICAL, errno,
                   GD_MSG_QUOTA_CONF_WRITE_FAIL,
                   "Unable to write "
                   "gfid %s into quota.conf for %s",
                   gfid_str, new_volinfo->volname);
            ret = -1;
            goto out;
        }
    }

    ret = gf_store_rename_tmppath(new_volinfo->quota_conf_shandle);

    ret = 0;

out:
    if (!ret) {
        ret = glusterd_compute_cksum(new_volinfo, _gf_true);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_CKSUM_COMPUTE_FAIL,
                   "Failed to compute checksum");
            goto clear_quota_conf;
        }

        ret = glusterd_store_save_quota_version_and_cksum(new_volinfo);
        if (ret)
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   GD_MSG_QUOTA_CKSUM_VER_STORE_FAIL,
                   "Failed to save quota version and checksum");
    }

clear_quota_conf:
    if (ret && (fd > 0)) {
        gf_store_unlink_tmppath(new_volinfo->quota_conf_shandle);
        (void)gf_store_handle_destroy(new_volinfo->quota_conf_shandle);
        new_volinfo->quota_conf_shandle = NULL;
    }

    return ret;
}

static int
gd_import_friend_volume_rebal_dict(dict_t *dict, int count,
                                   glusterd_volinfo_t *volinfo)
{
    int ret = -1;
    char key[64] = "";
    int dict_count = 0;
    char key_prefix[32];

    GF_ASSERT(dict);
    GF_ASSERT(volinfo);
    xlator_t *this = THIS;

    snprintf(key_prefix, sizeof(key_prefix), "volume%d", count);
    ret = snprintf(key, sizeof(key), "%s.rebal-dict-count", key_prefix);
    ret = dict_get_int32n(dict, key, ret, &dict_count);
    if (ret) {
        /* Older peers will not have this dict */
        gf_smsg(this->name, GF_LOG_INFO, -ret, GD_MSG_DICT_GET_FAILED, "Key=%s",
                key, NULL);
        ret = 0;
        goto out;
    }

    volinfo->rebal.dict = dict_new();
    if (!volinfo->rebal.dict) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        ret = -1;
        goto out;
    }

    ret = import_prdict_dict(dict, volinfo->rebal.dict, "rebal-dict-key",
                             "rebal-dict-value", dict_count, key_prefix);
out:
    if (ret && volinfo->rebal.dict)
        dict_unref(volinfo->rebal.dict);
    gf_msg_debug(this->name, 0, "Returning with %d", ret);
    return ret;
}

/* The prefix represents the type of volume to be added.
 * It will be "volume" for normal volumes, and snap# like
 * snap1, snap2, for snapshot volumes
 */
int32_t
glusterd_import_volinfo(dict_t *peer_data, int count,
                        glusterd_volinfo_t **volinfo, char *prefix)
{
    int ret = -1;
    char key[64] = "";
    char key_prefix[32];
    int keylen;
    char *parent_volname = NULL;
    char *volname = NULL;
    glusterd_volinfo_t *new_volinfo = NULL;
    char *volume_id_str = NULL;
    char msg[2048] = "";
    char *str = NULL;
    char *rebalance_id_str = NULL;
    int op_version = 0;
    int client_op_version = 0;
    uint32_t stage_deleted = 0;
    xlator_t *this = THIS;

    GF_ASSERT(peer_data);
    GF_ASSERT(volinfo);
    GF_ASSERT(prefix);

    ret = snprintf(key_prefix, sizeof(key_prefix), "%s%d", prefix, count);
    if (ret < 0 || ret >= sizeof(key_prefix)) {
        ret = -1;
        snprintf(msg, sizeof(msg), "key_prefix too big");
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.name", key_prefix);
    ret = dict_get_strn(peer_data, key, keylen, &volname);
    if (ret) {
        snprintf(msg, sizeof(msg), "%s missing in payload", key);
        goto out;
    }

    snprintf(key, sizeof(key), "%s.stage_deleted", key_prefix);
    ret = dict_get_uint32(peer_data, key, &stage_deleted);
    /* stage_deleted = 1 means the volume is still in the process of
     * deleting a volume, so we shouldn't be trying to create a
     * fresh volume here which would lead to a stale entry
     */
    if (stage_deleted) {
        goto out;
    }

    ret = glusterd_volinfo_new(&new_volinfo);
    if (ret)
        goto out;
    ret = snprintf(new_volinfo->volname, sizeof(new_volinfo->volname), "%s",
                   volname);
    if (ret < 0 || ret >= sizeof(new_volinfo->volname)) {
        ret = -1;
        goto out;
    }
    keylen = snprintf(key, sizeof(key), "%s.type", key_prefix);
    ret = dict_get_int32n(peer_data, key, keylen, &new_volinfo->type);
    if (ret) {
        snprintf(msg, sizeof(msg), "%s missing in payload for %s", key,
                 volname);
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.parent_volname", key_prefix);
    ret = dict_get_strn(peer_data, key, keylen, &parent_volname);
    if (!ret) {
        ret = snprintf(new_volinfo->parent_volname,
                       sizeof(new_volinfo->parent_volname), "%s",
                       parent_volname);
        if (ret < 0 || ret >= sizeof(new_volinfo->volname)) {
            ret = -1;
            goto out;
        }
    }
    keylen = snprintf(key, sizeof(key), "%s.brick_count", key_prefix);
    ret = dict_get_int32n(peer_data, key, keylen, &new_volinfo->brick_count);
    if (ret) {
        snprintf(msg, sizeof(msg), "%s missing in payload for %s", key,
                 volname);
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.version", key_prefix);
    ret = dict_get_int32n(peer_data, key, keylen, &new_volinfo->version);
    if (ret) {
        snprintf(msg, sizeof(msg), "%s missing in payload for %s", key,
                 volname);
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.status", key_prefix);
    ret = dict_get_int32n(peer_data, key, keylen,
                          (int32_t *)&new_volinfo->status);
    if (ret) {
        snprintf(msg, sizeof(msg), "%s missing in payload for %s", key,
                 volname);
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.sub_count", key_prefix);
    ret = dict_get_int32n(peer_data, key, keylen, &new_volinfo->sub_count);
    if (ret) {
        snprintf(msg, sizeof(msg), "%s missing in payload for %s", key,
                 volname);
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.subvol_count", key_prefix);
    ret = dict_get_int32n(peer_data, key, keylen, &new_volinfo->subvol_count);
    if (ret) {
        snprintf(msg, sizeof(msg), "%s missing in payload for %s", key,
                 volname);
        goto out;
    }

    /* not having a 'replica_count' key is not a error
       (as peer may be of old version) */
    keylen = snprintf(key, sizeof(key), "%s.replica_count", key_prefix);
    ret = dict_get_int32n(peer_data, key, keylen, &new_volinfo->replica_count);
    if (ret)
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
               "peer is possibly old version");

    /* not having a 'arbiter_count' key is not a error
       (as peer may be of old version) */
    keylen = snprintf(key, sizeof(key), "%s.arbiter_count", key_prefix);
    ret = dict_get_int32n(peer_data, key, keylen, &new_volinfo->arbiter_count);
    if (ret)
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
               "peer is possibly old version");

    /* not having a 'thin_arbiter_count' key is not a error
       (as peer may be of old version) */
    keylen = snprintf(key, sizeof(key), "%s.thin_arbiter_count", key_prefix);
    ret = dict_get_int32n(peer_data, key, keylen,
                          &new_volinfo->thin_arbiter_count);
    if (ret)
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
               "peer is possibly old version");

    /* not having a 'disperse_count' key is not a error
       (as peer may be of old version) */
    keylen = snprintf(key, sizeof(key), "%s.disperse_count", key_prefix);
    ret = dict_get_int32n(peer_data, key, keylen, &new_volinfo->disperse_count);
    if (ret)
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
               "peer is possibly old version");

    /* not having a 'redundancy_count' key is not a error
       (as peer may be of old version) */
    keylen = snprintf(key, sizeof(key), "%s.redundancy_count", key_prefix);
    ret = dict_get_int32n(peer_data, key, keylen,
                          &new_volinfo->redundancy_count);
    if (ret)
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
               "peer is possibly old version");

    /* not having a 'dist_count' key is not a error
       (as peer may be of old version) */
    keylen = snprintf(key, sizeof(key), "%s.dist_count", key_prefix);
    ret = dict_get_int32n(peer_data, key, keylen,
                          &new_volinfo->dist_leaf_count);
    if (ret)
        gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
               "peer is possibly old version");

    new_volinfo->subvol_count = new_volinfo->brick_count /
                                glusterd_get_dist_leaf_count(new_volinfo);
    snprintf(key, sizeof(key), "%s.ckusm", key_prefix);
    ret = dict_get_uint32(peer_data, key, &new_volinfo->cksum);
    if (ret) {
        snprintf(msg, sizeof(msg), "%s missing in payload for %s", key,
                 volname);
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.volume_id", key_prefix);
    ret = dict_get_strn(peer_data, key, keylen, &volume_id_str);
    if (ret) {
        snprintf(msg, sizeof(msg), "%s missing in payload for %s", key,
                 volname);
        goto out;
    }

    gf_uuid_parse(volume_id_str, new_volinfo->volume_id);

    keylen = snprintf(key, sizeof(key), "%s.username", key_prefix);
    ret = dict_get_strn(peer_data, key, keylen, &str);
    if (!ret) {
        ret = glusterd_auth_set_username(new_volinfo, str);
        if (ret)
            goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.password", key_prefix);
    ret = dict_get_strn(peer_data, key, keylen, &str);
    if (!ret) {
        ret = glusterd_auth_set_password(new_volinfo, str);
        if (ret)
            goto out;
    }

    snprintf(key, sizeof(key), "%s.transport_type", key_prefix);
    ret = dict_get_uint32(peer_data, key, &new_volinfo->transport_type);
    if (ret) {
        snprintf(msg, sizeof(msg), "%s missing in payload for %s", key,
                 volname);
        goto out;
    }

    snprintf(key, sizeof(key), "%s.rebalance", key_prefix);
    ret = dict_get_uint32(peer_data, key, &new_volinfo->rebal.defrag_cmd);
    if (ret) {
        snprintf(msg, sizeof(msg), "%s missing in payload for %s", key,
                 volname);
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.rebalance-id", key_prefix);
    ret = dict_get_strn(peer_data, key, keylen, &rebalance_id_str);
    if (ret) {
        /* This is not present in older glusterfs versions,
         * so don't error out
         */
        ret = 0;
    } else {
        gf_uuid_parse(rebalance_id_str, new_volinfo->rebal.rebalance_id);
    }

    snprintf(key, sizeof(key), "%s.rebalance-op", key_prefix);
    /* This is not present in older glusterfs versions,
     * so don't error out
     */
    ret = dict_get_uint32(peer_data, key, (uint32_t *)&new_volinfo->rebal.op);

    ret = gd_import_friend_volume_rebal_dict(peer_data, count, new_volinfo);
    if (ret) {
        snprintf(msg, sizeof(msg),
                 "Failed to import rebalance dict "
                 "for volume.");
        goto out;
    }

    ret = gd_import_volume_snap_details(peer_data, new_volinfo, key_prefix,
                                        volname);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_SNAP_DETAILS_IMPORT_FAIL,
               "Failed to import snapshot "
               "details for volume %s",
               volname);
        goto out;
    }

    ret = glusterd_import_friend_volume_opts(peer_data, count, new_volinfo,
                                             prefix);
    if (ret)
        goto out;

    /* Import the volume's op-versions if available else set it to 1.
     * Not having op-versions implies this informtation was obtained from a
     * op-version 1 friend (gluster-3.3), ergo the cluster is at op-version
     * 1 and all volumes are at op-versions 1.
     *
     * Either both the volume op-versions should be absent or both should be
     * present. Only one being present is a failure
     */
    keylen = snprintf(key, sizeof(key), "%s.op-version", key_prefix);
    ret = dict_get_int32n(peer_data, key, keylen, &op_version);
    if (ret)
        ret = 0;
    keylen = snprintf(key, sizeof(key), "%s.client-op-version", key_prefix);
    ret = dict_get_int32n(peer_data, key, keylen, &client_op_version);
    if (ret)
        ret = 0;

    if (op_version && client_op_version) {
        new_volinfo->op_version = op_version;
        new_volinfo->client_op_version = client_op_version;
    } else if (((op_version == 0) && (client_op_version != 0)) ||
               ((op_version != 0) && (client_op_version == 0))) {
        ret = -1;
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Only one volume op-version found");
        goto out;
    } else {
        new_volinfo->op_version = 1;
        new_volinfo->client_op_version = 1;
    }

    keylen = snprintf(key, sizeof(key), "%s.quota-xattr-version", key_prefix);
    /*This is not present in older glusterfs versions, so ignore ret value*/
    ret = dict_get_int32n(peer_data, key, keylen,
                          &new_volinfo->quota_xattr_version);

    ret = glusterd_import_bricks(peer_data, count, new_volinfo, prefix);
    if (ret)
        goto out;

    *volinfo = new_volinfo;
out:
    if (msg[0]) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOLINFO_IMPORT_FAIL, "%s",
               msg);
        gf_event(EVENT_IMPORT_VOLUME_FAILED, "volume=%s", new_volinfo->volname);
    }
    gf_msg_debug("glusterd", 0, "Returning with %d", ret);
    return ret;
}

static int32_t
glusterd_volume_disconnect_all_bricks(glusterd_volinfo_t *volinfo)
{
    int ret = 0;
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_brick_proc_t *brick_proc = NULL;
    int brick_count = 0;

    GF_ASSERT(volinfo);

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        if (glusterd_is_brick_started(brickinfo)) {
            /* If brick multiplexing is enabled then we can't
             * blindly set brickinfo->rpc to NULL as it might impact
             * the other attached bricks.
             */
            ret = glusterd_brick_proc_for_port(brickinfo->port, &brick_proc);
            if (!ret) {
                brick_count = brick_proc->brick_count;
            }
            if (!is_brick_mx_enabled() || brick_count == 0) {
                ret = glusterd_brick_disconnect(brickinfo);
                if (ret) {
                    gf_msg("glusterd", GF_LOG_ERROR, 0,
                           GD_MSD_BRICK_DISCONNECT_FAIL,
                           "Failed to "
                           "disconnect %s:%s",
                           brickinfo->hostname, brickinfo->path);
                    break;
                }
            }
        }
    }

    return ret;
}

static int32_t
glusterd_volinfo_copy_brickinfo(glusterd_volinfo_t *old_volinfo,
                                glusterd_volinfo_t *new_volinfo)
{
    glusterd_brickinfo_t *new_brickinfo = NULL;
    glusterd_brickinfo_t *old_brickinfo = NULL;
    glusterd_brickinfo_t *new_ta_brickinfo = NULL;
    glusterd_brickinfo_t *old_ta_brickinfo = NULL;
    int ret = 0;
    xlator_t *this = THIS;
    char abspath[PATH_MAX] = "";

    GF_ASSERT(new_volinfo);
    GF_ASSERT(old_volinfo);

    cds_list_for_each_entry(new_brickinfo, &new_volinfo->bricks, brick_list)
    {
        ret = glusterd_volume_brickinfo_get(
            new_brickinfo->uuid, new_brickinfo->hostname, new_brickinfo->path,
            old_volinfo, &old_brickinfo);
        if (ret == 0) {
            new_brickinfo->port = old_brickinfo->port;

            if (old_brickinfo->real_path[0] == '\0') {
                if (!realpath(new_brickinfo->path, abspath)) {
                    /* Here an ENOENT should also be a
                     * failure as the brick is expected to
                     * be in existence
                     */
                    gf_msg(this->name, GF_LOG_CRITICAL, errno,
                           GD_MSG_BRICKINFO_CREATE_FAIL,
                           "realpath () failed for brick "
                           "%s. The underlying filesystem "
                           "may be in bad state",
                           new_brickinfo->path);
                    ret = -1;
                    goto out;
                }
                if (strlen(abspath) >= sizeof(new_brickinfo->real_path)) {
                    ret = -1;
                    goto out;
                }
                (void)strncpy(new_brickinfo->real_path, abspath,
                              sizeof(new_brickinfo->real_path));
            } else {
                (void)strncpy(new_brickinfo->real_path,
                              old_brickinfo->real_path,
                              sizeof(new_brickinfo->real_path));
            }
        }
    }
    if (new_volinfo->thin_arbiter_count == 1) {
        cds_list_for_each_entry(new_ta_brickinfo, &new_volinfo->ta_bricks,
                                brick_list)
        {
            ret = glusterd_volume_ta_brickinfo_get(
                new_ta_brickinfo->uuid, new_ta_brickinfo->hostname,
                new_ta_brickinfo->path, old_volinfo, &old_ta_brickinfo);
            if (ret == 0) {
                new_ta_brickinfo->port = old_ta_brickinfo->port;

                if (old_ta_brickinfo->real_path[0] == '\0') {
                    if (!realpath(new_ta_brickinfo->path, abspath)) {
                        /* Here an ENOENT should also be a
                         * failure as the brick is expected to
                         * be in existence
                         */
                        gf_msg(this->name, GF_LOG_CRITICAL, errno,
                               GD_MSG_BRICKINFO_CREATE_FAIL,
                               "realpath () failed for brick "
                               "%s. The underlying filesystem "
                               "may be in bad state",
                               new_brickinfo->path);
                        ret = -1;
                        goto out;
                    }
                    if (strlen(abspath) >=
                        sizeof(new_ta_brickinfo->real_path)) {
                        ret = -1;
                        goto out;
                    }
                    (void)strncpy(new_ta_brickinfo->real_path, abspath,
                                  sizeof(new_ta_brickinfo->real_path));
                } else {
                    (void)strncpy(new_ta_brickinfo->real_path,
                                  old_ta_brickinfo->real_path,
                                  sizeof(new_ta_brickinfo->real_path));
                }
            }
        }
    }
    ret = 0;

out:
    return ret;
}

static int32_t
glusterd_volinfo_stop_stale_bricks(glusterd_volinfo_t *new_volinfo,
                                   glusterd_volinfo_t *old_volinfo)
{
    glusterd_brickinfo_t *new_brickinfo = NULL;
    glusterd_brickinfo_t *old_brickinfo = NULL;

    int ret = 0;
    GF_ASSERT(new_volinfo);
    GF_ASSERT(old_volinfo);
    if (_gf_false == glusterd_is_volume_started(old_volinfo))
        goto out;
    cds_list_for_each_entry(old_brickinfo, &old_volinfo->bricks, brick_list)
    {
        ret = glusterd_volume_brickinfo_get(
            old_brickinfo->uuid, old_brickinfo->hostname, old_brickinfo->path,
            new_volinfo, &new_brickinfo);
        /* If the brick is stale, i.e it's not a part of the new volume
         * or if it's part of the new volume and is pending a snap or if it's
         * brick multiplexing enabled, then stop the brick process
         */
        if (ret || (new_brickinfo->snap_status == -1) ||
            GF_ATOMIC_GET(old_volinfo->volpeerupdate)) {
            /*TODO: may need to switch to 'atomic' flavour of
             * brick_stop, once we make peer rpc program also
             * synctask enabled*/
            ret = glusterd_brick_stop(old_volinfo, old_brickinfo, _gf_false);
            if (ret)
                gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_BRICK_STOP_FAIL,
                       "Failed to stop"
                       " brick %s:%s",
                       old_brickinfo->hostname, old_brickinfo->path);
        }
    }
    ret = 0;
out:
    gf_msg_debug("glusterd", 0, "Returning with %d", ret);
    return ret;
}

static int32_t
glusterd_delete_stale_volume(glusterd_volinfo_t *stale_volinfo,
                             glusterd_volinfo_t *valid_volinfo)
{
    int32_t ret = -1;
    glusterd_volinfo_t *temp_volinfo = NULL;
    glusterd_volinfo_t *voliter = NULL;
    glusterd_svc_t *svc = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    int32_t brick_count = -1;

    GF_ASSERT(stale_volinfo);
    GF_ASSERT(valid_volinfo);

    /* Copy snap_volumes list from stale_volinfo to valid_volinfo */
    valid_volinfo->snap_count = 0;
    cds_list_for_each_entry_safe(voliter, temp_volinfo,
                                 &stale_volinfo->snap_volumes, snapvol_list)
    {
        cds_list_add_tail(&voliter->snapvol_list, &valid_volinfo->snap_volumes);
        valid_volinfo->snap_count++;
    }

    if ((!gf_uuid_is_null(stale_volinfo->restored_from_snap)) &&
        (gf_uuid_compare(stale_volinfo->restored_from_snap,
                         valid_volinfo->restored_from_snap))) {
        cds_list_for_each_entry(brickinfo, &stale_volinfo->bricks, brick_list)
        {
            brick_count++;
            if (gf_uuid_compare(brickinfo->uuid, MY_UUID))
                continue;

            ret = glusterd_snapshot_remove(NULL, stale_volinfo, brickinfo,
                                           brick_count);
            if (ret) {
                gf_msg(THIS->name, GF_LOG_WARNING, 0, GD_MSG_SNAP_REMOVE_FAIL,
                       "Failed to remove snapshot for "
                       "restored volume %s",
                       stale_volinfo->volname);
            }
        }
    }

    /* If stale volume is in started state, stop the stale bricks if the new
     * volume is started else, stop all bricks.
     * We don't want brick_rpc_notify to access already deleted brickinfo,
     * so disconnect all bricks from stale_volinfo (unconditionally), since
     * they are being deleted subsequently.
     */
    if (glusterd_is_volume_started(stale_volinfo)) {
        if (glusterd_is_volume_started(valid_volinfo)) {
            (void)glusterd_volinfo_stop_stale_bricks(valid_volinfo,
                                                     stale_volinfo);

        } else {
            (void)glusterd_stop_bricks(stale_volinfo);
        }

        (void)glusterd_volume_disconnect_all_bricks(stale_volinfo);
    }
    /* Delete all the bricks and stores and vol files. They will be created
     * again by the valid_volinfo. Volume store delete should not be
     * performed because some of the bricks could still be running,
     * keeping pid files under run directory
     */
    (void)glusterd_delete_all_bricks(stale_volinfo);
    if (stale_volinfo->shandle) {
        sys_unlink(stale_volinfo->shandle->path);
        (void)gf_store_handle_destroy(stale_volinfo->shandle);
        stale_volinfo->shandle = NULL;
    }

    /* Marking volume as stopped, so that svc manager stops snapd
     * and we are deleting the volume.
     */
    stale_volinfo->status = GLUSTERD_STATUS_STOPPED;

    if (!stale_volinfo->is_snap_volume) {
        svc = &(stale_volinfo->snapd.svc);
        (void)svc->manager(svc, stale_volinfo, PROC_START_NO_WAIT);
    }
    svc = &(stale_volinfo->shd.svc);
    (void)svc->manager(svc, stale_volinfo, PROC_START_NO_WAIT);

    (void)glusterd_volinfo_remove(stale_volinfo);

    return 0;
}

/* Stops the rebalance process of the given volume
 */
static int
gd_stop_rebalance_process(glusterd_volinfo_t *volinfo)
{
    int ret = -1;
    glusterd_conf_t *conf = NULL;
    char pidfile[PATH_MAX] = "";

    GF_ASSERT(volinfo);

    conf = THIS->private;
    GF_ASSERT(conf);

    GLUSTERD_GET_DEFRAG_PID_FILE(pidfile, volinfo, conf);
    ret = glusterd_service_stop("rebalance", pidfile, SIGTERM, _gf_true);

    return ret;
}

/* This function updates the rebalance information of the new volinfo using the
 * information from the old volinfo.
 */
static int
gd_check_and_update_rebalance_info(glusterd_volinfo_t *old_volinfo,
                                   glusterd_volinfo_t *new_volinfo)
{
    int ret = -1;
    glusterd_rebalance_t *old = NULL;
    glusterd_rebalance_t *new = NULL;

    GF_ASSERT(old_volinfo);
    GF_ASSERT(new_volinfo);

    old = &(old_volinfo->rebal);
    new = &(new_volinfo->rebal);

    // Disconnect from rebalance process
    if (glusterd_defrag_rpc_get(old->defrag)) {
        rpc_transport_disconnect(old->defrag->rpc->conn.trans, _gf_false);
        glusterd_defrag_rpc_put(old->defrag);
    }

    if (!gf_uuid_is_null(old->rebalance_id) &&
        gf_uuid_compare(old->rebalance_id, new->rebalance_id)) {
        (void)gd_stop_rebalance_process(old_volinfo);
        goto out;
    }

    /* If the tasks match, copy the status and other information of the
     * rebalance process from old_volinfo to new_volinfo
     */
    new->defrag_status = old->defrag_status;
    new->rebalance_files = old->rebalance_files;
    new->rebalance_data = old->rebalance_data;
    new->lookedup_files = old->lookedup_files;
    new->skipped_files = old->skipped_files;
    new->rebalance_failures = old->rebalance_failures;
    new->rebalance_time = old->rebalance_time;

    /* glusterd_rebalance_t.{op, id, defrag_cmd} are copied during volume
     * import a new defrag object should come to life with rebalance being
     * restarted
     */
out:
    return ret;
}

static int32_t
glusterd_import_friend_volume(dict_t *peer_data, int count,
                              glusterd_friend_synctask_args_t *arg)
{
    int32_t ret = -1;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = THIS;
    glusterd_volinfo_t *old_volinfo = NULL;
    glusterd_volinfo_t *new_volinfo = NULL;
    glusterd_svc_t *svc = NULL;
    int32_t update = 0;
    char key[64] = "";

    GF_ASSERT(peer_data);

    priv = this->private;
    GF_ASSERT(priv);

    if (arg) {
        /*Check if the volume options are updated on the other peers
         */
        update = (1UL & (arg->status_arr[(count / 64)] >> (count % 64)));
    } else {
        ret = snprintf(key, sizeof(key), "volume%d.update", count);
        ret = dict_get_int32n(peer_data, key, ret, &update);
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                    "Key=%s", key, NULL);
            goto out;
        }
    }

    if (!update) {
        /* if update is 0 that means the volume is not imported */
        gf_log(this->name, GF_LOG_DEBUG,
               "The volume%d does"
               " not have any peer change",
               count);
        ret = 0;
        goto out;
    }

    ret = glusterd_import_volinfo(peer_data, count, &new_volinfo, "volume");
    if (ret)
        goto out;

    if (!new_volinfo) {
        gf_msg_debug(this->name, 0, "Not importing snap volume");
        goto out;
    }

    ret = glusterd_volinfo_find(new_volinfo->volname, &old_volinfo);
    if (0 == ret) {
        if (new_volinfo->version <= old_volinfo->version) {
            /* When this condition is true, it already means that
             * the other synctask thread of import volume has
             * already up to date volume, so just ignore this volume
             * now
             */
            goto out;
        }
        /* Ref count the old_volinfo such that deleting it doesn't crash
         * if its been already in use by other thread
         */
        glusterd_volinfo_ref(old_volinfo);
        (void)gd_check_and_update_rebalance_info(old_volinfo, new_volinfo);

        /* Copy brick ports & real_path from the old volinfo always.
         * The old_volinfo will be cleaned up and this information
         * could be lost
         */
        (void)glusterd_volinfo_copy_brickinfo(old_volinfo, new_volinfo);

        (void)glusterd_delete_stale_volume(old_volinfo, new_volinfo);
        glusterd_volinfo_unref(old_volinfo);
    }

    ret = glusterd_store_volinfo(new_volinfo, GLUSTERD_VOLINFO_VER_AC_NONE);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_STORE_FAIL,
               "Failed to store "
               "volinfo for volume %s",
               new_volinfo->volname);
        goto out;
    }

    ret = glusterd_create_volfiles(new_volinfo);
    if (ret)
        goto out;

    glusterd_list_add_order(&new_volinfo->vol_list, &priv->volumes,
                            glusterd_compare_volume_name);

    if (glusterd_is_volume_started(new_volinfo)) {
        (void)glusterd_start_bricks(new_volinfo);
        if (glusterd_is_snapd_enabled(new_volinfo)) {
            svc = &(new_volinfo->snapd.svc);
            if (svc->manager(svc, new_volinfo, PROC_START_NO_WAIT)) {
                gf_event(EVENT_SVC_MANAGER_FAILED, "svc_name=%s", svc->name);
            }
        }
        svc = &(new_volinfo->shd.svc);
        if (svc->manager(svc, new_volinfo, PROC_START_NO_WAIT)) {
            gf_event(EVENT_SVC_MANAGER_FAILED, "svc_name=%s", svc->name);
        }
    }

    ret = glusterd_import_quota_conf(peer_data, count, new_volinfo, "volume");
    if (ret) {
        gf_event(EVENT_IMPORT_QUOTA_CONF_FAILED, "volume=%s",
                 new_volinfo->volname);
        goto out;
    }

    ret = glusterd_fetchspec_notify(this);
out:
    gf_msg_debug("glusterd", 0, "Returning with ret: %d", ret);
    return ret;
}

static int32_t
glusterd_import_friend_volumes_synctask(void *opaque)
{
    int32_t ret = -1;
    int32_t count = 0;
    int i = 1;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    dict_t *peer_data = NULL;
    glusterd_friend_synctask_args_t *arg = NULL;
    uint64_t bm = 0;
    uint64_t mask = 0;

    conf = this->private;
    GF_ASSERT(conf);

    arg = opaque;
    if (!arg)
        goto out;

    peer_data = arg->peer_data;
    ret = dict_get_int32(peer_data, "count", &count);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                "Key=count", NULL);
        goto out;
    }

    synclock_lock(&conf->big_lock);

    /* We need to ensure that importing a volume shouldn't race with an
     * other thread where as part of restarting glusterd, bricks are
     * restarted (refer glusterd_restart_bricks ())
     */
    while (conf->restart_bricks) {
        synccond_wait(&conf->cond_restart_bricks, &conf->big_lock);
    }
    conf->restart_bricks = _gf_true;

    while (i <= count) {
        bm = arg->status_arr[i / 64];
        while (bm != 0) {
            /* mask will contain the lowest bit set from bm. */
            mask = bm & (-bm);
            bm ^= mask;
            ret = glusterd_import_friend_volume(peer_data, i + ffsll(mask) - 2,
                                                arg);
            if (ret < 0) {
                break;
            }
        }
        i += 64;
    }
    if (i > count) {
        glusterd_svcs_manager(NULL);
    }
    conf->restart_bricks = _gf_false;
    synccond_broadcast(&conf->cond_restart_bricks);
out:
    if (arg) {
        dict_unref(arg->peer_data);
        dict_unref(arg->peer_ver_data);
        GF_FREE(arg);
    }

    gf_msg_debug("glusterd", 0, "Returning with %d", ret);
    return ret;
}

static int32_t
glusterd_import_friend_volumes(dict_t *peer_data)
{
    int32_t ret = -1;
    int32_t count = 0;
    int i = 1;

    GF_ASSERT(peer_data);

    ret = dict_get_int32(peer_data, "count", &count);
    if (ret) {
        gf_smsg("glusterd", GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                "Key=count", NULL);
        goto out;
    }

    while (i <= count) {
        ret = glusterd_import_friend_volume(peer_data, i, NULL);
        if (ret)
            goto out;
        i++;
    }

out:
    gf_msg_debug("glusterd", 0, "Returning with %d", ret);
    return ret;
}

static int
glusterd_get_global_server_quorum_ratio(dict_t *opts, double *quorum)
{
    int ret = -1;
    char *quorum_str = NULL;

    ret = dict_get_str(opts, GLUSTERD_QUORUM_RATIO_KEY, &quorum_str);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_DEBUG, -ret, GD_MSG_DICT_GET_FAILED,
                "Key=%s", GLUSTERD_QUORUM_RATIO_KEY, NULL);
    } else {
        ret = gf_string2percent(quorum_str, quorum);
    }
    return ret;
}

static int
glusterd_get_global_opt_version(dict_t *opts, uint32_t *version)
{
    int ret = -1;
    char *version_str = NULL;

    ret = dict_get_str(opts, GLUSTERD_GLOBAL_OPT_VERSION, &version_str);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                "Key=%s", GLUSTERD_GLOBAL_OPT_VERSION, NULL);
    } else {
        ret = gf_string2uint(version_str, version);
    }
    return ret;
}

int
glusterd_get_next_global_opt_version_str(dict_t *opts, char **version_str)
{
    int ret = -1;
    char version_string[64] = "";
    uint32_t version = 0;

    ret = glusterd_get_global_opt_version(opts, &version);
    if (ret)
        goto out;
    version++;
    snprintf(version_string, sizeof(version_string), "%" PRIu32, version);
    *version_str = gf_strdup(version_string);
    if (*version_str)
        ret = 0;
out:
    return ret;
}

static int32_t
glusterd_import_global_opts(dict_t *friend_data)
{
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    int ret = -1;
    dict_t *import_options = NULL;
    int count = 0;
    uint32_t local_version = 0;
    uint32_t remote_version = 0;
    double old_quorum = 0.0;
    double new_quorum = 0.0;

    conf = this->private;

    ret = dict_get_int32(friend_data, "global-opt-count", &count);
    if (ret) {
        // old version peer
        gf_smsg(this->name, GF_LOG_INFO, -ret, GD_MSG_DICT_GET_FAILED,
                "Key=global-opt-count", NULL);
        ret = 0;
        goto out;
    }

    import_options = dict_new();
    if (!import_options) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        goto out;
    }
    ret = import_prdict_dict(friend_data, import_options, "key", "val", count,
                             "global");
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GLOBAL_OPT_IMPORT_FAIL,
               "Failed to import"
               " global options");
        goto out;
    }

    /* Not handling ret since server-quorum-ratio might not yet be set */
    ret = glusterd_get_global_server_quorum_ratio(conf->opts, &old_quorum);
    ret = glusterd_get_global_server_quorum_ratio(import_options, &new_quorum);

    ret = glusterd_get_global_opt_version(conf->opts, &local_version);
    if (ret)
        goto out;
    ret = glusterd_get_global_opt_version(import_options, &remote_version);
    if (ret)
        goto out;

    if (remote_version > local_version) {
        ret = glusterd_store_options(this, import_options);
        if (ret)
            goto out;
        dict_unref(conf->opts);
        conf->opts = dict_ref(import_options);

        /* If server quorum ratio has changed, restart bricks to
         * recompute if quorum is met. If quorum is not met bricks are
         * not started and those already running are stopped
         */
        if (old_quorum != new_quorum) {
            glusterd_launch_synctask(glusterd_restart_bricks, NULL);
        }
    }

    ret = 0;
out:
    if (import_options)
        dict_unref(import_options);
    return ret;
}

int32_t
glusterd_compare_friend_data(dict_t *peer_data, dict_t *cmp, int32_t *status,
                             char *hostname)
{
    int32_t ret = -1;
    int32_t count = 0;
    int i = 1;
    gf_boolean_t update = _gf_false;
    xlator_t *this = THIS;
    glusterd_friend_synctask_args_t *arg = NULL;

    GF_ASSERT(peer_data);
    GF_ASSERT(status);

    ret = glusterd_import_global_opts(peer_data);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GLOBAL_OPT_IMPORT_FAIL,
               "Importing global "
               "options failed");
        goto out;
    }

    ret = dict_get_int32(peer_data, "count", &count);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                "Key=count", NULL);
        goto out;
    }

    arg = GF_CALLOC(1, sizeof(*arg) + sizeof(uint64_t) * (count / 64),
                    gf_common_mt_char);
    if (!arg) {
        ret = -1;
        gf_msg("glusterd", GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Out Of Memory");
        goto out;
    }
    arg->peer_data = dict_ref(peer_data);
    arg->peer_ver_data = dict_ref(cmp);
    while (i <= count) {
        ret = glusterd_compare_friend_volume(peer_data, arg, i, status,
                                             hostname);
        if (ret)
            goto out;

        if (GLUSTERD_VOL_COMP_RJT == *status) {
            ret = 0;
            update = _gf_false;
            goto out;
        }
        if (GLUSTERD_VOL_COMP_UPDATE_REQ == *status) {
            update = _gf_true;
        }
        i++;
    }

    if (update) {
        /* Launch the import friend volume as a separate synctask as it
         * has to trigger start bricks where we may need to wait for the
         * first brick to come up before attaching the subsequent bricks
         * in case brick multiplexing is enabled
         */
        ret = glusterd_launch_synctask(glusterd_import_friend_volumes_synctask,
                                       arg);
    }

out:
    if ((ret || !update) && arg) {
        dict_unref(arg->peer_data);
        dict_unref(arg->peer_ver_data);
        GF_FREE(arg);
    }
    gf_msg_debug(this->name, 0, "Returning with ret: %d, status: %d", ret,
                 *status);
    return ret;
}

struct rpc_clnt *
glusterd_defrag_rpc_get(glusterd_defrag_info_t *defrag)
{
    struct rpc_clnt *rpc = NULL;

    if (!defrag)
        return NULL;

    LOCK(&defrag->lock);
    {
        rpc = rpc_clnt_ref(defrag->rpc);
    }
    UNLOCK(&defrag->lock);
    return rpc;
}

struct rpc_clnt *
glusterd_defrag_rpc_put(glusterd_defrag_info_t *defrag)
{
    struct rpc_clnt *rpc = NULL;

    if (!defrag)
        return NULL;

    LOCK(&defrag->lock);
    {
        rpc = rpc_clnt_unref(defrag->rpc);
        defrag->rpc = rpc;
    }
    UNLOCK(&defrag->lock);
    return rpc;
}

struct rpc_clnt *
glusterd_pending_node_get_rpc(glusterd_pending_node_t *pending_node)
{
    struct rpc_clnt *rpc = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_svc_t *svc = NULL;

    GF_VALIDATE_OR_GOTO(THIS->name, pending_node, out);
    GF_VALIDATE_OR_GOTO(THIS->name, pending_node->node, out);

    if (pending_node->type == GD_NODE_BRICK) {
        brickinfo = pending_node->node;
        rpc = brickinfo->rpc;

    } else if (pending_node->type == GD_NODE_SHD ||
               pending_node->type == GD_NODE_NFS ||
               pending_node->type == GD_NODE_QUOTAD ||
               pending_node->type == GD_NODE_SCRUB) {
        svc = pending_node->node;
        rpc = svc->conn.rpc;
    } else if (pending_node->type == GD_NODE_REBALANCE) {
        volinfo = pending_node->node;
        rpc = glusterd_defrag_rpc_get(volinfo->rebal.defrag);

    } else if (pending_node->type == GD_NODE_SNAPD) {
        volinfo = pending_node->node;
        rpc = volinfo->snapd.svc.conn.rpc;
    } else {
        GF_ASSERT(0);
    }

out:
    return rpc;
}

void
glusterd_pending_node_put_rpc(glusterd_pending_node_t *pending_node)
{
    glusterd_volinfo_t *volinfo = NULL;

    switch (pending_node->type) {
        case GD_NODE_REBALANCE:
            volinfo = pending_node->node;
            glusterd_defrag_rpc_put(volinfo->rebal.defrag);
            break;

        default:
            break;
    }
}

#ifdef BUILD_GNFS
void
glusterd_nfs_pmap_deregister(void)
{
    if (pmap_unset(MOUNT_PROGRAM, MOUNTV3_VERSION))
        gf_msg("glusterd", GF_LOG_INFO, 0, GD_MSG_DEREGISTER_SUCCESS,
               "De-registered MOUNTV3 successfully");
    else
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_PMAP_UNSET_FAIL,
               "De-register MOUNTV3 is unsuccessful");

    if (pmap_unset(MOUNT_PROGRAM, MOUNTV1_VERSION))
        gf_msg("glusterd", GF_LOG_INFO, 0, GD_MSG_DEREGISTER_SUCCESS,
               "De-registered MOUNTV1 successfully");
    else
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_PMAP_UNSET_FAIL,
               "De-register MOUNTV1 is unsuccessful");

    if (pmap_unset(NFS_PROGRAM, NFSV3_VERSION))
        gf_msg("glusterd", GF_LOG_INFO, 0, GD_MSG_DEREGISTER_SUCCESS,
               "De-registered NFSV3 successfully");
    else
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_PMAP_UNSET_FAIL,
               "De-register NFSV3 is unsuccessful");

    if (pmap_unset(NLM_PROGRAM, NLMV4_VERSION))
        gf_msg("glusterd", GF_LOG_INFO, 0, GD_MSG_DEREGISTER_SUCCESS,
               "De-registered NLM v4 successfully");
    else
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_PMAP_UNSET_FAIL,
               "De-registration of NLM v4 failed");

    if (pmap_unset(NLM_PROGRAM, NLMV1_VERSION))
        gf_msg("glusterd", GF_LOG_INFO, 0, GD_MSG_DEREGISTER_SUCCESS,
               "De-registered NLM v1 successfully");
    else
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_PMAP_UNSET_FAIL,
               "De-registration of NLM v1 failed");

    if (pmap_unset(ACL_PROGRAM, ACLV3_VERSION))
        gf_msg("glusterd", GF_LOG_INFO, 0, GD_MSG_DEREGISTER_SUCCESS,
               "De-registered ACL v3 successfully");
    else
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_PMAP_UNSET_FAIL,
               "De-registration of ACL v3 failed");
}

#endif  /// #ifdef BUILD_GNFS

int
glusterd_remote_hostname_get(rpcsvc_request_t *req, char *remote_host, int len)
{
    GF_ASSERT(req);
    GF_ASSERT(remote_host);
    GF_ASSERT(req->trans);

    char *name = NULL;
    char *hostname = NULL;
    char *tmp_host = NULL;
    char *canon = NULL;
    int ret = 0;

    name = req->trans->peerinfo.identifier;
    tmp_host = gf_strdup(name);
    if (tmp_host)
        get_host_name(tmp_host, &hostname);

    GF_ASSERT(hostname);
    if (!hostname) {
        memset(remote_host, 0, len);
        ret = -1;
        goto out;
    }

    if ((gf_get_hostname_from_ip(hostname, &canon) == 0) && canon) {
        GF_FREE(tmp_host);
        tmp_host = hostname = canon;
    }

    (void)snprintf(remote_host, len, "%s", hostname);

out:
    GF_FREE(tmp_host);
    return ret;
}

gf_boolean_t
glusterd_are_all_volumes_stopped(void)
{
    glusterd_conf_t *priv = NULL;
    glusterd_volinfo_t *voliter = NULL;

    priv = THIS->private;
    GF_ASSERT(priv);

    cds_list_for_each_entry(voliter, &priv->volumes, vol_list)
    {
        if (voliter->status == GLUSTERD_STATUS_STARTED)
            return _gf_false;
    }

    return _gf_true;
}

gf_boolean_t
glusterd_all_volumes_with_quota_stopped(void)
{
    glusterd_conf_t *priv = NULL;
    glusterd_volinfo_t *voliter = NULL;

    priv = THIS->private;
    GF_ASSERT(priv);

    cds_list_for_each_entry(voliter, &priv->volumes, vol_list)
    {
        if (!glusterd_is_volume_quota_enabled(voliter))
            continue;
        if (voliter->status == GLUSTERD_STATUS_STARTED)
            return _gf_false;
    }

    return _gf_true;
}

static int32_t
my_callback(struct rpc_req *req, struct iovec *iov, int count, void *v_frame)
{
    call_frame_t *frame = v_frame;
    glusterd_conf_t *conf = frame->this->private;

    if (GF_ATOMIC_DEC(conf->blockers) == 0) {
        synccond_broadcast(&conf->cond_blockers);
    }

    STACK_DESTROY(frame->root);
    return 0;
}

static int32_t
attach_brick_callback(struct rpc_req *req, struct iovec *iov, int count,
                      void *v_frame)
{
    call_frame_t *frame = v_frame;
    glusterd_conf_t *conf = frame->this->private;
    glusterd_brickinfo_t *brickinfo = frame->local;
    glusterd_brickinfo_t *other_brick = frame->cookie;
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = THIS;
    int ret = -1;
    char pidfile1[PATH_MAX] = "";
    char pidfile2[PATH_MAX] = "";
    gf_getspec_rsp rsp = {
        0,
    };
    int last_brick = -1;

    frame->local = NULL;
    frame->cookie = NULL;

    if (!iov) {
        gf_log(frame->this->name, GF_LOG_ERROR, "iov is NULL");
        ret = -1;
        goto out;
    }

    ret = xdr_to_generic(*iov, &rsp, (xdrproc_t)xdr_gf_getspec_rsp);
    if (ret < 0) {
        gf_log(frame->this->name, GF_LOG_ERROR, "XDR decoding error");
        ret = -1;
        goto out;
    }

    ret = glusterd_get_volinfo_from_brick(other_brick->path, &volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_GET_FAIL,
               "Failed to get volinfo"
               " from brick(%s) so  pidfile copying/unlink will fail",
               other_brick->path);
        goto out;
    }
    GLUSTERD_GET_BRICK_PIDFILE(pidfile1, volinfo, other_brick, conf);
    volinfo = NULL;

    ret = glusterd_get_volinfo_from_brick(brickinfo->path, &volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_GET_FAIL,
               "Failed to get volinfo"
               " from brick(%s) so  pidfile copying/unlink will fail",
               brickinfo->path);
        goto out;
    }
    GLUSTERD_GET_BRICK_PIDFILE(pidfile2, volinfo, brickinfo, conf);

    if (rsp.op_ret == 0) {
        brickinfo->port_registered = _gf_true;

        /* PID file is copied once brick has attached
           successfully
        */
        ret = glusterd_copy_file(pidfile1, pidfile2);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                   "Could not copy file %s to %s", pidfile1, pidfile2);
            goto out;
        }

        brickinfo->status = GF_BRICK_STARTED;
        brickinfo->rpc = rpc_clnt_ref(other_brick->rpc);
        gf_log(this->name, GF_LOG_INFO, "brick %s is attached successfully",
               brickinfo->path);
    } else {
        gf_log(this->name, GF_LOG_INFO,
               "attach_brick failed pidfile"
               " is %s for brick_path %s",
               pidfile2, brickinfo->path);
        brickinfo->port = 0;
        brickinfo->status = GF_BRICK_STOPPED;
        ret = glusterd_brick_process_remove_brick(brickinfo, &last_brick);
        if (ret)
            gf_msg_debug(this->name, 0,
                         "Couldn't remove brick from"
                         " brick process");
        LOCK(&volinfo->lock);
        ret = glusterd_store_volinfo(volinfo, GLUSTERD_VOLINFO_VER_AC_NONE);
        UNLOCK(&volinfo->lock);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_SET_FAIL,
                   "Failed to store volinfo of "
                   "%s volume",
                   volinfo->volname);
            goto out;
        }
    }
out:
    if (GF_ATOMIC_DEC(conf->blockers) == 0) {
        synccond_broadcast(&conf->cond_blockers);
    }
    STACK_DESTROY(frame->root);
    return 0;
}

int
send_attach_req(xlator_t *this, struct rpc_clnt *rpc, char *path,
                glusterd_brickinfo_t *brickinfo,
                glusterd_brickinfo_t *other_brick, int op,
                gf_boolean_t graceful_cleanup)
{
    int ret = -1;
    struct iobuf *iobuf = NULL;
    struct iobref *iobref = NULL;
    struct iovec iov = {
        0,
    };
    ssize_t req_size = 0;
    call_frame_t *frame = NULL;
    gd1_mgmt_brick_op_req brick_req;
    void *req = &brick_req;
    struct rpc_clnt_connection *conn;
    glusterd_conf_t *conf = this->private;
    extern struct rpc_clnt_program gd_brick_prog;
    fop_cbk_fn_t cbkfn = my_callback;
    dict_t *dict = NULL;

    if (!rpc) {
        gf_log(this->name, GF_LOG_ERROR, "called with null rpc");
        return -1;
    }

    conn = &rpc->conn;
    if (rpc_clnt_connection_status(conn) != RPC_STATUS_CONNECTED) {
        gf_log(this->name, GF_LOG_INFO, "not connected yet");
        return -1;
    }

    brick_req.op = op;
    brick_req.name = path;
    brick_req.input.input_val = NULL;
    brick_req.input.input_len = 0;
    brick_req.dict.dict_val = NULL;
    brick_req.dict.dict_len = 0;

    /* Set graceful_cleanup key in dicitonary to check the key value at the time
       of handling a detach rpc request at brick side
    */
    if (graceful_cleanup) {
        dict = dict_new();
        if (!dict) {
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL,
                    NULL);
            ret = -ENOMEM;
            goto err;
        }
        ret = dict_set_str_sizen(dict, GLUSTER_BRICK_GRACEFUL_CLEANUP,
                                 "enable");
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                   "Unable to set cluster.brick-graceful-cleanup key");
            goto err;
        }
        ret = dict_allocate_and_serialize(dict, &brick_req.dict.dict_val,
                                          &brick_req.dict.dict_len);
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, errno,
                    GD_MSG_DICT_ALLOC_AND_SERL_LENGTH_GET_FAIL, NULL);
            goto err;
        }
    }

    req_size = xdr_sizeof((xdrproc_t)xdr_gd1_mgmt_brick_op_req, req);
    iobuf = iobuf_get2(rpc->ctx->iobuf_pool, req_size);
    if (!iobuf) {
        goto err;
    }

    iov.iov_base = iobuf->ptr;
    iov.iov_len = iobuf_pagesize(iobuf);

    iobref = iobref_new();
    if (!iobref) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_NO_MEMORY, NULL);
        goto free_iobref;
    }

    frame = create_frame(this, this->ctx->pool);
    if (!frame) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_FRAME_CREATE_FAIL,
                NULL);
        goto free_iobref;
    }

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
        goto free_iobref;
    }

    iov.iov_len = ret;

    if (op == GLUSTERD_BRICK_ATTACH) {
        frame->local = brickinfo;
        frame->cookie = other_brick;
        cbkfn = attach_brick_callback;
    }
    /* Send the msg */
    GF_ATOMIC_INC(conf->blockers);
    ret = rpc_clnt_submit(rpc, &gd_brick_prog, op, cbkfn, &iov, 1, NULL, 0,
                          iobref, frame, NULL, 0, NULL, 0, NULL);

free_iobref:
    if (iobref)
        iobref_unref(iobref);
    if (iobuf) {
        iobuf_unref(iobuf);
    }
err:
    if (dict)
        dict_unref(dict);
    if (brick_req.dict.dict_val)
        GF_FREE(brick_req.dict.dict_val);
    return ret;
}

extern size_t
build_volfile_path(char *volume_id, char *path, size_t path_len,
                   char *trusted_str, dict_t *dict);

static int
attach_brick(xlator_t *this, glusterd_brickinfo_t *brickinfo,
             glusterd_brickinfo_t *other_brick, glusterd_volinfo_t *volinfo,
             glusterd_volinfo_t *other_vol)
{
    glusterd_conf_t *conf = this->private;
    char unslashed[PATH_MAX] = {
        '\0',
    };
    char full_id[PATH_MAX] = {
        '\0',
    };
    char path[PATH_MAX] = {
        '\0',
    };
    int ret = -1;
    int tries;
    rpc_clnt_t *rpc;
    int32_t len;

    gf_log(this->name, GF_LOG_INFO, "add brick %s to existing process for %s",
           brickinfo->path, other_brick->path);

    GLUSTERD_REMOVE_SLASH_FROM_PATH(brickinfo->path, unslashed);

    if (volinfo->is_snap_volume) {
        len = snprintf(full_id, sizeof(full_id), "/%s/%s/%s/%s.%s.%s",
                       GLUSTERD_VOL_SNAP_DIR_PREFIX,
                       volinfo->snapshot->snapname, volinfo->volname,
                       volinfo->volname, brickinfo->hostname, unslashed);
    } else {
        len = snprintf(full_id, sizeof(full_id), "%s.%s.%s", volinfo->volname,
                       brickinfo->hostname, unslashed);
    }
    if ((len < 0) || (len >= sizeof(full_id))) {
        goto out;
    }

    (void)build_volfile_path(full_id, path, sizeof(path), NULL, NULL);

    for (tries = 15; tries > 0; --tries) {
        rpc = rpc_clnt_ref(other_brick->rpc);
        if (rpc) {
            ret = send_attach_req(this, rpc, path, brickinfo, other_brick,
                                  GLUSTERD_BRICK_ATTACH, _gf_false);
            rpc_clnt_unref(rpc);
            if (!ret) {
                ret = port_brick_bind(this, other_brick->port, brickinfo->path,
                                      NULL, true);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, errno,
                           GD_PMAP_PORT_BIND_FAILED,
                           "adding brick to process failed");
                    goto out;
                }
                brickinfo->port = other_brick->port;
                ret = glusterd_brick_process_add_brick(brickinfo, other_brick);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_BRICKPROC_ADD_BRICK_FAILED,
                           "Adding brick %s:%s to brick "
                           "process failed",
                           brickinfo->hostname, brickinfo->path);
                    return ret;
                }
                return 0;
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
        synctask_sleep(1);
        synclock_lock(&conf->big_lock);
    }

out:
    gf_log(this->name, GF_LOG_WARNING, "attach failed for %s", brickinfo->path);
    return ret;
}

/* This name was just getting too long, hence the abbreviations. */
static glusterd_brickinfo_t *
find_compat_brick_in_vol(glusterd_conf_t *conf,
                         glusterd_volinfo_t *srch_vol, /* volume to search */
                         glusterd_volinfo_t *comp_vol, /* volume to compare */
                         glusterd_brickinfo_t *brickinfo)
{
    xlator_t *this = THIS;
    glusterd_brickinfo_t *other_brick = NULL;
    glusterd_brick_proc_t *brick_proc = NULL;
    char pidfile2[PATH_MAX] = "";
    int32_t pid2 = -1;
    int16_t retries = 15;
    int mux_limit = -1;
    int ret = -1;
    gf_boolean_t brick_status = _gf_false;
    gf_boolean_t is_shared_storage = _gf_false;

    /*
     * If comp_vol is provided, we have to check *volume* compatibility
     * before we can check *brick* compatibility.
     */
    if (comp_vol) {
        /*
         * We should not attach bricks of a normal volume to bricks
         * of shared storage volume.
         */
        if (!strcmp(srch_vol->volname, GLUSTER_SHARED_STORAGE))
            is_shared_storage = _gf_true;

        if (!strcmp(comp_vol->volname, GLUSTER_SHARED_STORAGE)) {
            if (!is_shared_storage)
                return NULL;
        } else if (is_shared_storage)
            return NULL;

        /*
         * It's kind of a shame that we have to do this check in both
         * directions, but an option might only exist on one of the two
         * dictionaries and dict_foreach_match will only find that one.
         */

        gf_log(this->name, GF_LOG_DEBUG, "comparing options for %s and %s",
               comp_vol->volname, srch_vol->volname);

        if (dict_foreach_match(comp_vol->dict, unsafe_option, NULL,
                               opts_mismatch, srch_vol->dict) < 0) {
            gf_log(this->name, GF_LOG_DEBUG, "failure forward");
            return NULL;
        }

        if (dict_foreach_match(srch_vol->dict, unsafe_option, NULL,
                               opts_mismatch, comp_vol->dict) < 0) {
            gf_log(this->name, GF_LOG_DEBUG, "failure backward");
            return NULL;
        }

        gf_log(this->name, GF_LOG_DEBUG, "all options match");
    }

    ret = get_mux_limit_per_process(&mux_limit);
    if (ret) {
        gf_msg_debug(this->name, 0,
                     "Retrieving brick mux "
                     "limit failed. Returning NULL");
        return NULL;
    }

    cds_list_for_each_entry(other_brick, &srch_vol->bricks, brick_list)
    {
        if (other_brick == brickinfo) {
            continue;
        }
        if (gf_uuid_compare(brickinfo->uuid, other_brick->uuid)) {
            continue;
        }
        if (other_brick->status != GF_BRICK_STARTED &&
            other_brick->status != GF_BRICK_STARTING) {
            continue;
        }

        ret = glusterd_brick_proc_for_port(other_brick->port, &brick_proc);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Couldn't get brick "
                         "process corresponding to brick %s:%s",
                         other_brick->hostname, other_brick->path);
            continue;
        }

        if (mux_limit != 0) {
            if (brick_proc->brick_count >= mux_limit)
                continue;
        } else {
            /* This means that the "cluster.max-bricks-per-process"
             * options hasn't yet been explicitly set. Continue
             * as if there's no limit set
             */
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_NO_MUX_LIMIT,
                   "cluster.max-bricks-per-process options isn't "
                   "set. Continuing with no limit set for "
                   "brick multiplexing.");
        }
        /* The first brick process might take some time to finish its
         * handshake with glusterd and prepare the graph. We can't
         * afford to send attach_req for other bricks till that time.
         * brick process sends PMAP_SIGNIN event after processing the
         * volfile and hence it's safe to assume that if glusterd has
         * received a pmap signin request for the same brick, we are
         * good for subsequent attach requests.
         */
        retries = 15;
        while (retries > 0) {
            if (other_brick->port_registered) {
                GLUSTERD_GET_BRICK_PIDFILE(pidfile2, srch_vol, other_brick,
                                           conf);
                if (sys_access(pidfile2, F_OK) == 0 &&
                    gf_is_service_running(pidfile2, &pid2)) {
                    gf_msg_debug(this->name, 0,
                                 "brick %s is running as a pid %d ",
                                 other_brick->path, pid2);
                    brick_status = _gf_true;
                    break;
                }
            }

            synclock_unlock(&conf->big_lock);
            gf_msg_debug(this->name, 0,
                         "brick %s is still"
                         " starting, waiting for 2 seconds ",
                         other_brick->path);
            synctask_sleep(2);
            synclock_lock(&conf->big_lock);
            retries--;
        }

        if (!brick_status) {
            gf_log(this->name, GF_LOG_INFO,
                   "brick has not come up so cleaning up dead brick %s:%s",
                   other_brick->hostname, other_brick->path);
            other_brick->status = GF_BRICK_STOPPED;
            if (pidfile2[0])
                sys_unlink(pidfile2);
            continue;
        }
        return other_brick;
    }

    return NULL;
}

static glusterd_brickinfo_t *
find_compatible_brick(glusterd_conf_t *conf, glusterd_volinfo_t *volinfo,
                      glusterd_brickinfo_t *brickinfo,
                      glusterd_volinfo_t **other_vol_p)
{
    glusterd_brickinfo_t *other_brick = NULL;
    glusterd_volinfo_t *other_vol = NULL;
    glusterd_snap_t *snap = NULL;

    /* Just return NULL here if multiplexing is disabled. */
    if (!is_brick_mx_enabled()) {
        return NULL;
    }

    other_brick = find_compat_brick_in_vol(conf, volinfo, NULL, brickinfo);
    if (other_brick) {
        *other_vol_p = volinfo;
        return other_brick;
    }

    /*
     * This check is necessary because changes to a volume's
     * transport options aren't propagated to snapshots.  Such a
     * change might break compatibility between the two, but we
     * have no way to "evict" a brick from the process it's
     * currently in.  If we keep it separate from the start, we
     * avoid the problem.  Note that snapshot bricks can still be
     * colocated with one another, even if they're for different
     * volumes, because the only thing likely to differ is their
     * auth options and those are not a factor in determining
     * compatibility.
     *
     * The very same immutability of snapshot bricks' transport
     * options, which can make them incompatible with their parent
     * volumes, ensures that once-compatible snapshot bricks will
     * remain compatible.  However, the same is not true for bricks
     * belonging to two non-snapshot volumes.  In that case, a
     * change to one might break compatibility and require them to
     * be separated, which is not yet done.
     *
     * TBD: address the option-change issue for non-snapshot bricks
     */
    if (!volinfo->is_snap_volume) {
        cds_list_for_each_entry(other_vol, &conf->volumes, vol_list)
        {
            if (other_vol == volinfo) {
                continue;
            }
            other_brick = find_compat_brick_in_vol(conf, other_vol, volinfo,
                                                   brickinfo);
            if (other_brick) {
                *other_vol_p = other_vol;
                return other_brick;
            }
        }
    } else {
        cds_list_for_each_entry(snap, &conf->snapshots, snap_list)
        {
            cds_list_for_each_entry(other_vol, &snap->volumes, vol_list)
            {
                if (other_vol == volinfo) {
                    continue;
                }
                other_brick = find_compat_brick_in_vol(conf, other_vol, volinfo,
                                                       brickinfo);
                if (other_brick) {
                    *other_vol_p = other_vol;
                    return other_brick;
                }
            }
        }
    }

    return NULL;
}

/* Below function is use to populate sockpath based on passed pid
   value as a argument after check the value from proc and also
   check if passed pid is match with running  glusterfs process
*/

static int
glusterd_get_sock_from_brick_pid(int pid, char *sockpath, size_t len)
{
    char buf[1024] = "";
    char cmdline[2048] = "";
    xlator_t *this = THIS;
    int fd = -1;
    int i = 0, j = 0;
    char *ptr = NULL;
    char *brptr = NULL;
    char tmpsockpath[PATH_MAX] = "";
    size_t blen = 0;
    int ret = -1;

#ifdef __FreeBSD__
    blen = sizeof(buf);
    int mib[4];

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_ARGS;
    mib[3] = pid;

    if (sys_sysctl(mib, 4, buf, &blen, NULL, blen) != 0) {
        gf_log(this->name, GF_LOG_ERROR, "brick process %d is not running",
               pid);
        return ret;
    }
#else
    char fname[128] = "";
    snprintf(fname, sizeof(fname), "/proc/%d/cmdline", pid);

    if (sys_access(fname, R_OK) != 0) {
        gf_log(this->name, GF_LOG_ERROR, "brick process %d is not running",
               pid);
        return ret;
    }

    fd = open(fname, O_RDONLY);
    if (fd != -1) {
        blen = (int)sys_read(fd, buf, 1024);
    } else {
        gf_log(this->name, GF_LOG_ERROR, "open failed %s to open a file %s",
               strerror(errno), fname);
        return ret;
    }
#endif

    /* convert cmdline to single string */
    for (i = 0, j = 0; i < blen; i++) {
        if (buf[i] == '\0')
            cmdline[j++] = ' ';
        else if (buf[i] < 32 || buf[i] > 126) /* remove control char */
            continue;
        else if (buf[i] == '"' || buf[i] == '\\') {
            cmdline[j++] = '\\';
            cmdline[j++] = buf[i];
        } else {
            cmdline[j++] = buf[i];
        }
    }
    cmdline[j] = '\0';
    if (fd)
        sys_close(fd);
    if (!strstr(cmdline, "glusterfs"))
        return ret;

    ptr = strstr(cmdline, "-S ");
    if (!ptr)
        return ret;
    ptr = strchr(ptr, '/');
    if (!ptr)
        return ret;
    brptr = strstr(ptr, "--brick-name");
    if (!brptr)
        return ret;
    i = 0;

    while (ptr < brptr) {
        if (*ptr != 32)
            tmpsockpath[i++] = *ptr;
        ptr++;
    }

    if (tmpsockpath[0]) {
        strncpy(sockpath, tmpsockpath, i);
        /*
         * Condition to check if the brick socket file is present
         * in the stated path or not. This helps in preventing
         * constant re-connect triggered in the RPC layer and also
         * a log message would help out the user.
         */
        ret = sys_access(sockpath, F_OK);
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_FILE_NOT_FOUND,
                    "%s not found", sockpath, NULL);
        }
    }

    return ret;
}

char *
search_brick_path_from_proc(pid_t brick_pid, char *brickpath)
{
    char *brick_path = NULL;
#ifdef __FreeBSD__
    struct filestat *fst;
    struct procstat *ps;
    struct kinfo_proc *kp;
    struct filestat_list *head;

    ps = procstat_open_sysctl();
    if (ps == NULL)
        goto out;

    kp = kinfo_getproc(brick_pid);
    if (kp == NULL)
        goto out;

    head = procstat_getfiles(ps, (void *)kp, 0);
    if (head == NULL)
        goto out;

    STAILQ_FOREACH(fst, head, next)
    {
        if (fst->fs_fd < 0)
            continue;

        if (!strcmp(fst->fs_path, brickpath)) {
            brick_path = gf_strdup(fst->fs_path);
            break;
        }
    }

out:
    if (head != NULL)
        procstat_freefiles(ps, head);
    if (kp != NULL)
        free(kp);
    procstat_close(ps);
#else
    struct dirent *dp = NULL;
    DIR *dirp = NULL;
    size_t len = 0;
    int fd = -1;
    char path[PATH_MAX] = "";
    struct dirent scratch[2] = {
        {
            0,
        },
    };

    if (!brickpath)
        goto out;

    len = sprintf(path, "/proc/%d/fd/", brick_pid);
    if (len >= (sizeof(path) - 2))
        goto out;

    dirp = sys_opendir(path);
    if (!dirp)
        goto out;

    fd = dirfd(dirp);
    if (fd < 0)
        goto out;

    while ((dp = sys_readdir(dirp, scratch))) {
        if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
            continue;

        /* check for non numerical descriptors */
        if (!strtol(dp->d_name, (char **)NULL, 10))
            continue;

        len = readlinkat(fd, dp->d_name, path, sizeof(path) - 1);
        /* TODO: handle len == -1 -> error condition in readlinkat */
        if (len > 1) {
            path[len] = '\0';
            if (!strcmp(path, brickpath)) {
                brick_path = gf_strdup(path);
                break;
            }
        }
    }
out:
    if (dirp)
        sys_closedir(dirp);
#endif
    return brick_path;
}

int
glusterd_brick_start(glusterd_volinfo_t *volinfo,
                     glusterd_brickinfo_t *brickinfo, gf_boolean_t wait,
                     gf_boolean_t only_connect)
{
    int ret = -1;
    xlator_t *this = THIS;
    glusterd_brickinfo_t *other_brick;
    glusterd_conf_t *conf = NULL;
    int32_t pid = -1;
    char pidfile[PATH_MAX] = "";
    char socketpath[PATH_MAX] = "";
    char *brickpath = NULL;
    glusterd_volinfo_t *other_vol;
    gf_boolean_t is_service_running = _gf_false;
    uuid_t volid = {
        0,
    };
    ssize_t size = -1;

    conf = this->private;

    if ((!brickinfo) || (!volinfo)) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    if (gf_uuid_is_null(brickinfo->uuid)) {
        ret = glusterd_resolve_brick(brickinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RESOLVE_BRICK_FAIL,
                   FMTSTR_RESOLVE_BRICK, brickinfo->hostname, brickinfo->path);
            gf_event(EVENT_BRICKPATH_RESOLVE_FAILED,
                     "peer=%s;volume=%s;brick=%s", brickinfo->hostname,
                     volinfo->volname, brickinfo->path);
            goto out;
        }
    }

    if (gf_uuid_compare(brickinfo->uuid, MY_UUID)) {
        ret = 0;
        goto out;
    }

    /* If a trigger to start the brick is already initiated then no need for
     * a reattempt as it's an overkill. With glusterd_brick_start ()
     * function being used in multiple places, when glusterd restarts we see
     * three different triggers for an attempt to start the brick process
     * due to the quorum handling code in glusterd_friend_sm.
     */
    if (brickinfo->status == GF_BRICK_STARTING || brickinfo->start_triggered ||
        GF_ATOMIC_GET(volinfo->volpeerupdate)) {
        gf_msg_debug(this->name, 0,
                     "brick %s is already in starting "
                     "phase",
                     brickinfo->path);
        ret = 0;
        goto out;
    }
    if (!only_connect)
        brickinfo->start_triggered = _gf_true;

    GLUSTERD_GET_BRICK_PIDFILE(pidfile, volinfo, brickinfo, conf);

    /* Compare volume-id xattr is helpful to ensure the existence of a
       brick_root path before the start/attach a brick
    */
    size = sys_lgetxattr(brickinfo->path, GF_XATTR_VOL_ID_KEY, volid, 16);
    if (size != 16) {
        gf_log(this->name, GF_LOG_ERROR,
               "Missing %s extended attribute on brick root (%s),"
               " brick is deemed not to be a part of the volume (%s) ",
               GF_XATTR_VOL_ID_KEY, brickinfo->path, volinfo->volname);
        goto out;
    }

    /* If it is Snapshot brick, allow starting the brick process even if
       on disk Volume id is different than the one in Snap Volume info */
    if (gf_uuid_compare(volinfo->volume_id, volid) &&
        !volinfo->is_snap_volume) {
        gf_log(this->name, GF_LOG_ERROR,
               "Mismatching %s extended attribute on brick root (%s),"
               " brick is deemed not to be a part of the volume (%s)",
               GF_XATTR_VOL_ID_KEY, brickinfo->path, volinfo->volname);
        goto out;
    }

    is_service_running = gf_is_service_running(pidfile, &pid);
    if (is_service_running) {
        if (is_brick_mx_enabled()) {
            brickpath = search_brick_path_from_proc(pid, brickinfo->path);
            if (!brickpath) {
                if (only_connect)
                    return 0;
                gf_log(this->name, GF_LOG_INFO,
                       "Either pid %d is not running or brick"
                       " path %s is not consumed so cleanup pidfile",
                       pid, brickinfo->path);
                /* brick isn't running,so unlink stale pidfile
                 * if any.
                 */
                if (sys_access(pidfile, R_OK) == 0) {
                    sys_unlink(pidfile);
                }
                goto run;
            }
            GF_FREE(brickpath);
            ret = glusterd_get_sock_from_brick_pid(pid, socketpath,
                                                   sizeof(socketpath));
            if (ret) {
                if (only_connect)
                    return 0;
                gf_log(this->name, GF_LOG_INFO,
                       "Either pid %d is not running or does "
                       "not match with any running brick "
                       "processes",
                       pid);
                /* Fetch unix socket is failed so unlink pidfile */
                if (sys_access(pidfile, R_OK) == 0) {
                    sys_unlink(pidfile);
                }
                goto run;
            }
        }
        if (brickinfo->status != GF_BRICK_STARTING &&
            brickinfo->status != GF_BRICK_STARTED) {
            gf_log(this->name, GF_LOG_INFO,
                   "discovered already-running brick %s", brickinfo->path);
            (void)port_brick_bind(this, brickinfo->port, brickinfo->path, NULL,
                                  false);
            brickinfo->port_registered = _gf_true;
            /*
             * This will unfortunately result in a separate RPC
             * connection per brick, even though they're all in
             * the same process.  It works, but it would be nicer
             * if we could find a pre-existing connection to that
             * same port (on another brick) and re-use that.
             * TBD: re-use RPC connection across bricks
             */
            if (!is_brick_mx_enabled()) {
                glusterd_set_brick_socket_filepath(
                    volinfo, brickinfo, socketpath, sizeof(socketpath));
                /*
                 * Condition to check if the brick socket file is present
                 * in the stated path or not. This helps in preventing
                 * constant re-connect triggered in the RPC layer and also
                 * a log message would help out the user.
                 */
                ret = sys_access(socketpath, F_OK);
                if (ret) {
                    gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_FILE_NOT_FOUND,
                            "%s not found", socketpath, NULL);
                    goto out;
                }
            }

            gf_log(this->name, GF_LOG_DEBUG,
                   "Using %s as sockfile for brick %s of volume %s ",
                   socketpath, brickinfo->path, volinfo->volname);

            (void)glusterd_brick_connect(volinfo, brickinfo, socketpath);

            ret = glusterd_brick_process_add_brick(brickinfo, NULL);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_BRICKPROC_ADD_BRICK_FAILED,
                       "Adding brick %s:%s to brick process "
                       "failed.",
                       brickinfo->hostname, brickinfo->path);
                goto out;
            }
            /* We need to set the status back to STARTING so that
             * while the other (re)start brick requests come in for
             * other bricks, this brick can be considered as
             * compatible.
             */
            brickinfo->status = GF_BRICK_STARTING;
        }
        return 0;
    }
    if (only_connect)
        return 0;

run:
    ret = _mk_rundir_p(volinfo);
    if (ret)
        goto out;

    other_brick = find_compatible_brick(conf, volinfo, brickinfo, &other_vol);
    if (other_brick) {
        /* mark the brick to starting as send_attach_req might take few
         * iterations to successfully attach the brick and we don't want
         * to get into a state where another needless trigger to start
         * the brick is processed
         */
        brickinfo->status = GF_BRICK_STARTING;
        ret = attach_brick(this, brickinfo, other_brick, volinfo, other_vol);
        if (ret == 0) {
            goto out;
        }
        /* Attach_brick is failed so unlink pidfile */
        if (sys_access(pidfile, R_OK) == 0) {
            sys_unlink(pidfile);
        }
    }

    /*
     * This hack is necessary because our brick-process management is a
     * total nightmare.  We expect a brick process's socket and pid files
     * to be ready *immediately* after we start it.  Ditto for it calling
     * back to bind its port.  Unfortunately, none of that is realistic.
     * Any process takes non-zero time to start up.  This has *always* been
     * racy and unsafe; it just became more visible with multiplexing.
     *
     * The right fix would be to do all of this setup *in the parent*,
     * which would include (among other things) getting the PID back from
     * the "runner" code.  That's all prohibitively difficult and risky.
     * To work around the more immediate problems, we create a stub pidfile
     * here to let gf_is_service_running know that we expect the process to
     * be there shortly, and then it gets filled in with a real PID when
     * the process does finish starting up.
     *
     * TBD: pray for GlusterD 2 to be ready soon.
     */
    gf_log(this->name, GF_LOG_INFO,
           "starting a fresh brick process for "
           "brick %s",
           brickinfo->path);
    ret = glusterd_volume_start_glusterfs(volinfo, brickinfo, wait);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_DISCONNECTED,
               "Unable to start brick %s:%s", brickinfo->hostname,
               brickinfo->path);
        gf_event(EVENT_BRICK_START_FAILED, "peer=%s;volume=%s;brick=%s",
                 brickinfo->hostname, volinfo->volname, brickinfo->path);
        goto out;
    }

out:
    if (ret && brickinfo) {
        brickinfo->start_triggered = _gf_false;
    }
    gf_msg_debug(this->name, 0, "returning %d ", ret);
    return ret;
}

static int
glusterd_restart_bricks(void *opaque)
{
    int ret = 0;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_snap_t *snap = NULL;
    gf_boolean_t start_svcs = _gf_false;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    int active_count = 0;
    int quorum_count = 0;
    gf_boolean_t node_quorum = _gf_false;

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, return_block);

    synclock_lock(&conf->big_lock);

    /* We need to ensure that restarting the bricks during glusterd restart
     * shouldn't race with the import volume thread (refer
     * glusterd_compare_friend_data ())
     */
    while (conf->restart_bricks) {
        synccond_wait(&conf->cond_restart_bricks, &conf->big_lock);
    }
    conf->restart_bricks = _gf_true;

    GF_ATOMIC_INC(conf->blockers);
    ret = glusterd_get_quorum_cluster_counts(this, &active_count,
                                             &quorum_count);
    if (ret)
        goto out;

    if (does_quorum_meet(active_count, quorum_count))
        node_quorum = _gf_true;

    cds_list_for_each_entry(volinfo, &conf->volumes, vol_list)
    {
        if (volinfo->status != GLUSTERD_STATUS_STARTED) {
            continue;
        }
        gf_msg_debug(this->name, 0, "starting the volume %s", volinfo->volname);

        /* Check the quorum, if quorum is not met, don't start the
           bricks. Stop bricks in case they are running.
        */
        ret = check_quorum_for_brick_start(volinfo, node_quorum);
        if (ret == 0) {
            gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_SERVER_QUORUM_NOT_MET,
                   "Skipping brick "
                   "restart for volume %s as quorum is not met",
                   volinfo->volname);
            (void)glusterd_stop_bricks(volinfo);
            continue;
        } else if (ret == 2 && conf->restart_done == _gf_true) {
            /* If glusterd has been restarted and quorum is not
             * applicable then do not restart the bricks as this
             * might start bricks brought down purposely, say for
             * maintenance
             */
            continue;
        } else {
            start_svcs = _gf_true;
            cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
            {
                if (!brickinfo->start_triggered) {
                    pthread_mutex_lock(&brickinfo->restart_mutex);
                    {
                        glusterd_brick_start(volinfo, brickinfo, _gf_true,
                                             _gf_false);
                    }
                    pthread_mutex_unlock(&brickinfo->restart_mutex);
                }
            }
            ret = glusterd_store_volinfo(volinfo, GLUSTERD_VOLINFO_VER_AC_NONE);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_STORE_FAIL,
                       "Failed to "
                       "write volinfo for volume %s",
                       volinfo->volname);
                goto out;
            }
        }
    }

    cds_list_for_each_entry(snap, &conf->snapshots, snap_list)
    {
        cds_list_for_each_entry(volinfo, &snap->volumes, vol_list)
        {
            if (volinfo->status != GLUSTERD_STATUS_STARTED)
                continue;
            /* Check the quorum, if quorum is not met, don't start
             * the bricks
             */
            ret = check_quorum_for_brick_start(volinfo, node_quorum);
            if (ret == 0) {
                gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_SERVER_QUORUM_NOT_MET,
                       "Skipping"
                       " brick restart for volume %s as "
                       "quorum is not met",
                       volinfo->volname);
                continue;
            }
            start_svcs = _gf_true;
            gf_msg_debug(this->name, 0,
                         "starting the snap "
                         "volume %s",
                         volinfo->volname);
            cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
            {
                if (!brickinfo->start_triggered) {
                    pthread_mutex_lock(&brickinfo->restart_mutex);
                    {
                        /* coverity[SLEEP] */
                        glusterd_brick_start(volinfo, brickinfo, _gf_true,
                                             _gf_false);
                    }
                    pthread_mutex_unlock(&brickinfo->restart_mutex);
                }
            }
            ret = glusterd_store_volinfo(volinfo, GLUSTERD_VOLINFO_VER_AC_NONE);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_STORE_FAIL,
                       "Failed to "
                       "write volinfo for volume %s",
                       volinfo->volname);
                goto out;
            }
        }
    }
    if (start_svcs == _gf_true) {
        glusterd_svcs_manager(NULL);
    }

    ret = 0;

out:
    conf->restart_done = _gf_true;
    conf->restart_bricks = _gf_false;
    if (GF_ATOMIC_DEC(conf->blockers) == 0) {
        synccond_broadcast(&conf->cond_blockers);
    }
    synccond_broadcast(&conf->cond_restart_bricks);

return_block:
    return ret;
}

static int
_local_gsyncd_start(dict_t *this, char *key, data_t *value, void *data)
{
    char *path_list = NULL;
    char *secondary = NULL;
    char *secondary_url = NULL;
    char *secondary_vol = NULL;
    char *secondary_host = NULL;
    char *statefile = NULL;
    char buf[1024] = "faulty";
    int ret = 0;
    int op_ret = 0;
    int ret_status = 0;
    char uuid_str[64] = "";
    glusterd_volinfo_t *volinfo = NULL;
    char confpath[PATH_MAX] = "";
    char *op_errstr = NULL;
    glusterd_conf_t *priv = NULL;
    gf_boolean_t is_template_in_use = _gf_false;
    gf_boolean_t is_paused = _gf_false;
    char key1[1024] = "";
    xlator_t *this1 = THIS;

    priv = this1->private;
    GF_ASSERT(priv);
    GF_ASSERT(data);

    volinfo = data;
    secondary = strchr(value->data, ':');
    if (secondary)
        secondary++;
    else
        return 0;

    (void)snprintf(uuid_str, sizeof(uuid_str), "%s", (char *)value->data);

    /* Getting Local Brickpaths */
    ret = glusterd_get_local_brickpaths(volinfo, &path_list);

    /*Generating the conf file path needed by gsyncd */
    ret = glusterd_get_secondary_info(
        secondary, &secondary_url, &secondary_host, &secondary_vol, &op_errstr);
    if (ret) {
        gf_msg(this1->name, GF_LOG_ERROR, 0, GD_MSG_SECONDARYINFO_FETCH_ERROR,
               "Unable to fetch secondary details.");
        ret = -1;
        goto out;
    }

    ret = snprintf(confpath, sizeof(confpath) - 1,
                   "%s/" GEOREP "/%s_%s_%s/gsyncd.conf", priv->workdir,
                   volinfo->volname, secondary_host, secondary_vol);
    confpath[ret] = '\0';

    /* Fetching the last status of the node */
    ret = glusterd_get_statefile_name(volinfo, secondary, confpath, &statefile,
                                      &is_template_in_use);
    if (ret) {
        if (!strstr(secondary, "::"))
            gf_msg(this1->name, GF_LOG_INFO, 0, GD_MSG_SECONDARY_URL_INVALID,
                   "%s is not a valid secondary url.", secondary);
        else
            gf_msg(this1->name, GF_LOG_INFO, 0,
                   GD_MSG_GET_STATEFILE_NAME_FAILED,
                   "Unable to get"
                   " statefile's name");
        goto out;
    }

    /* If state-file entry is missing from the config file,
     * do not start gsyncd on restart */
    if (is_template_in_use) {
        gf_msg(this1->name, GF_LOG_INFO, 0, GD_MSG_NO_STATEFILE_ENTRY,
               "state-file entry is missing in config file."
               "Not Restarting");
        goto out;
    }

    is_template_in_use = _gf_false;

    ret = gsync_status(volinfo->volname, secondary, confpath, &ret_status,
                       &is_template_in_use);
    if (ret == -1) {
        gf_msg(this1->name, GF_LOG_INFO, 0, GD_MSG_GSYNC_VALIDATION_FAIL,
               GEOREP " start option validation failed ");
        ret = 0;
        goto out;
    }

    if (is_template_in_use == _gf_true) {
        gf_msg(this1->name, GF_LOG_INFO, 0, GD_MSG_PIDFILE_NOT_FOUND,
               "pid-file entry is missing in config file."
               "Not Restarting");
        ret = 0;
        goto out;
    }

    ret = glusterd_gsync_read_frm_status(statefile, buf, sizeof(buf));
    if (ret <= 0) {
        gf_msg(this1->name, GF_LOG_ERROR, 0, GD_MSG_STAT_FILE_READ_FAILED,
               "Unable to read the status");
        goto out;
    }

    /* Form key1 which is "<user@><secondary_host>::<secondaryvol>" */
    snprintf(key1, sizeof(key1), "%s::%s", secondary_url, secondary_vol);

    /* Looks for the last status, to find if the session was running
     * when the node went down. If the session was just created or
     * stopped, do not restart the geo-rep session */
    if ((!strcmp(buf, "Created")) || (!strcmp(buf, "Stopped"))) {
        gf_msg(this1->name, GF_LOG_INFO, 0, GD_MSG_GEO_REP_START_FAILED,
               "Geo-Rep Session was not started between "
               "%s and %s::%s. Not Restarting",
               volinfo->volname, secondary_url, secondary_vol);
        goto out;
    } else if (strstr(buf, "Paused")) {
        is_paused = _gf_true;
    } else if ((!strcmp(buf, "Config Corrupted"))) {
        gf_msg(this1->name, GF_LOG_INFO, 0, GD_MSG_RECOVERING_CORRUPT_CONF,
               "Recovering from a corrupted config. "
               "Not Restarting. Use start (force) to "
               "start the session between %s and %s::%s.",
               volinfo->volname, secondary_url, secondary_vol);
        goto out;
    }

    if (is_paused) {
        glusterd_start_gsync(volinfo, secondary, path_list, confpath, uuid_str,
                             NULL, _gf_true);
    } else {
        /* Add secondary to the dict indicating geo-rep session is running*/
        ret = dict_set_dynstr_with_alloc(volinfo->gsync_active_secondaries,
                                         key1, "running");
        if (ret) {
            gf_msg(this1->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Unable to set key:%s"
                   " value:running in the dict",
                   key1);
            goto out;
        }
        ret = glusterd_start_gsync(volinfo, secondary, path_list, confpath,
                                   uuid_str, NULL, _gf_false);
        if (ret)
            dict_del(volinfo->gsync_active_secondaries, key1);
    }

out:
    if (statefile)
        GF_FREE(statefile);
    if (secondary_url)
        GF_FREE(secondary_url);

    if (is_template_in_use) {
        op_ret = glusterd_create_status_file(volinfo->volname, secondary,
                                             secondary_host, secondary_vol,
                                             "Config Corrupted");
        if (op_ret) {
            gf_msg(this1->name, GF_LOG_ERROR, 0,
                   GD_MSG_STATUSFILE_CREATE_FAILED,
                   "Unable to create status file"
                   ". Error : %s",
                   strerror(errno));
            ret = op_ret;
        }
    }
    if (secondary_vol)
        GF_FREE(secondary_vol);
    GF_FREE(path_list);
    GF_FREE(op_errstr);

    return ret;
}

static int
glusterd_volume_restart_gsyncds(glusterd_volinfo_t *volinfo)
{
    GF_ASSERT(volinfo);

    dict_foreach(volinfo->gsync_secondaries, _local_gsyncd_start, volinfo);
    return 0;
}

static int
glusterd_restart_gsyncds(glusterd_conf_t *conf)
{
    glusterd_volinfo_t *volinfo = NULL;
    int ret = 0;

    cds_list_for_each_entry(volinfo, &conf->volumes, vol_list)
    {
        glusterd_volume_restart_gsyncds(volinfo);
    }
    return ret;
}

int
glusterd_get_dist_leaf_count(glusterd_volinfo_t *volinfo)
{
    if (volinfo->type == GF_CLUSTER_TYPE_DISPERSE)
        return volinfo->disperse_count;
    else if (volinfo->type == GF_CLUSTER_TYPE_REPLICATE)
        return volinfo->replica_count;

    return 1;
}

static glusterd_brickinfo_t *
glusterd_get_brickinfo_by_position(glusterd_volinfo_t *volinfo, uint32_t pos)
{
    glusterd_brickinfo_t *tmpbrkinfo = NULL;

    cds_list_for_each_entry(tmpbrkinfo, &volinfo->bricks, brick_list)
    {
        if (pos == 0)
            return tmpbrkinfo;
        pos--;
    }
    return NULL;
}

void
glusterd_set_brick_status(glusterd_brickinfo_t *brickinfo,
                          gf_brick_status_t status)
{
    GF_ASSERT(brickinfo);
    brickinfo->status = status;
    if (GF_BRICK_STARTED == status) {
        gf_msg_debug("glusterd", 0,
                     "Setting brick %s:%s status "
                     "to started",
                     brickinfo->hostname, brickinfo->path);
    } else {
        gf_msg_debug("glusterd", 0,
                     "Setting brick %s:%s status "
                     "to stopped",
                     brickinfo->hostname, brickinfo->path);
    }
}

gf_boolean_t
glusterd_is_brick_started(glusterd_brickinfo_t *brickinfo)
{
    GF_ASSERT(brickinfo);
    return (brickinfo->status == GF_BRICK_STARTED);
}

int
glusterd_get_brick_root(char *path, char **mount_point)
{
    char *ptr = NULL;
    char *mnt_pt = NULL;
    struct stat brickstat = {0};
    struct stat buf = {0};
    xlator_t *this = THIS;

    if (!path) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto err;
    }
    mnt_pt = gf_strdup(path);
    if (!mnt_pt) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto err;
    }
    if (sys_stat(mnt_pt, &brickstat))
        goto err;

    while ((ptr = strrchr(mnt_pt, '/')) && ptr != mnt_pt) {
        *ptr = '\0';
        if (sys_stat(mnt_pt, &buf)) {
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
                    "Error in stat=%s", strerror(errno), NULL);
            goto err;
        }

        if (brickstat.st_dev != buf.st_dev) {
            *ptr = '/';
            break;
        }
    }

    if (ptr == mnt_pt) {
        if (sys_stat("/", &buf)) {
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
                    "Error in stat=%s", strerror(errno), NULL);
            goto err;
        }
        if (brickstat.st_dev == buf.st_dev)
            strcpy(mnt_pt, "/");
    }

    *mount_point = mnt_pt;
    return 0;

err:
    GF_FREE(mnt_pt);
    return -1;
}

static char *
glusterd_parse_inode_size(char *stream, char *pattern)
{
    char *needle = NULL;
    char *trail = NULL;

    needle = strstr(stream, pattern);
    if (!needle)
        goto out;

    needle = nwstrtail(needle, pattern);

    trail = needle;
    while (trail && isdigit(*trail))
        trail++;
    if (trail)
        *trail = '\0';

out:
    return needle;
}

static struct fs_info {
    char *fs_type_name;
    char *fs_tool_name;
    char *fs_tool_arg;
    char *fs_tool_pattern;
    char *fs_tool_pkg;
} glusterd_fs[] = {{"xfs", "xfs_info", NULL, "isize=", "xfsprogs"},
                   {"ext3", "tune2fs", "-l", "Inode size:", "e2fsprogs"},
                   {"ext4", "tune2fs", "-l", "Inode size:", "e2fsprogs"},
                   {"btrfs", NULL, NULL, NULL, NULL},
                   {"zfs", NULL, NULL, NULL, NULL},
                   {NULL, NULL, NULL, NULL, NULL}};

static int
glusterd_add_inode_size_to_dict(dict_t *dict, int count)
{
    int ret = -1;
    char key[64];
    char buffer[4096] = "";
    char *device = NULL;
    char *fs_name = NULL;
    char *cur_word = NULL;
    char *trail = NULL;
    runner_t runner = {
        0,
    };
    struct fs_info *fs = NULL;
    static dict_t *cached_fs = NULL;
    xlator_t *this = THIS;

    ret = snprintf(key, sizeof(key), "brick%d.device", count);
    ret = dict_get_strn(dict, key, ret, &device);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                "Key=%s", key, NULL);
        goto out;
    }

    if (cached_fs) {
        if (dict_get_str(cached_fs, device, &cur_word) == 0) {
            goto cached;
        }
    } else {
        cached_fs = dict_new();
    }

    ret = snprintf(key, sizeof(key), "brick%d.fs_name", count);
    ret = dict_get_strn(dict, key, ret, &fs_name);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                "Key=%s", key, NULL);
        goto out;
    }

    runinit(&runner);
    runner_redir(&runner, STDOUT_FILENO, RUN_PIPE);

    for (fs = glusterd_fs; fs->fs_type_name; fs++) {
        if (strcmp(fs_name, fs->fs_type_name) == 0) {
            if (!fs->fs_tool_name) {
                /* dynamic inodes */
                gf_smsg(this->name, GF_LOG_INFO, 0, GD_MSG_INODE_SIZE_GET_FAIL,
                        "The brick on device uses dynamic inode sizes",
                        "Device=%s (%s)", device, fs_name, NULL);
                cur_word = "N/A";
                goto cached;
            }
            runner_add_arg(&runner, fs->fs_tool_name);
            break;
        }
    }

    if (runner.argv[0]) {
        if (fs->fs_tool_arg)
            runner_add_arg(&runner, fs->fs_tool_arg);
        runner_add_arg(&runner, device);
    } else {
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_INODE_SIZE_GET_FAIL,
                "Could not find tool to get inode size for device", "Tool=%s",
                fs->fs_tool_name, "Device=%s (%s)", device, fs_name,
                "Missing package=%s ?", fs->fs_tool_pkg, NULL);
        goto out;
    }

    ret = runner_start(&runner);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_CMD_EXEC_FAIL,
                "Failed to execute \"%s\"", fs->fs_tool_name, NULL);
        /*
         * Runner_start might return an error after the child has
         * been forked, e.g. if the program isn't there.  In that
         * case, we still need to call runner_end to reap the
         * child and free resources.  Fortunately, that seems to
         * be harmless for other kinds of failures.
         */
        (void)runner_end(&runner);
        goto out;
    }

    for (;;) {
        if (fgets(buffer, sizeof(buffer),
                  runner_chio(&runner, STDOUT_FILENO)) == NULL)
            break;
        trail = strrchr(buffer, '\n');
        if (trail)
            *trail = '\0';

        cur_word = glusterd_parse_inode_size(buffer, fs->fs_tool_pattern);

        if (cur_word)
            break;
    }

    ret = runner_end(&runner);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_CMD_EXEC_FAIL,
                "Tool exited with non-zero exit status", "Tool=%s",
                fs->fs_tool_name, NULL);

        goto out;
    }
    if (!cur_word) {
        ret = -1;
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_INODE_SIZE_GET_FAIL,
                "Using Tool=%s", fs->fs_tool_name, NULL);
        goto out;
    }

    if (dict_set_dynstr_with_alloc(cached_fs, device, cur_word)) {
        /* not fatal if not entered into the cache */
        gf_msg_debug(this->name, 0, "failed to cache fs inode size for %s",
                     device);
    }

cached:
    snprintf(key, sizeof(key), "brick%d.inode_size", count);

    ret = dict_set_dynstr_with_alloc(dict, key, cur_word);

out:
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_INODE_SIZE_GET_FAIL, NULL);
    return ret;
}

struct mntent *
glusterd_get_mnt_entry_info(char *mnt_pt, char *buff, int buflen,
                            struct mntent *entry_ptr)
{
    struct mntent *entry = NULL;
    FILE *mtab = NULL;
    char abspath[PATH_MAX] = "";

    GF_ASSERT(mnt_pt);
    GF_ASSERT(buff);
    GF_ASSERT(entry_ptr);

    mtab = setmntent(_PATH_MOUNTED, "r");
    if (!mtab)
        goto out;

    if (!realpath(mnt_pt, abspath)) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_MNTENTRY_GET_FAIL,
               "realpath () failed for path %s", mnt_pt);
        goto out;
    }

    entry = getmntent_r(mtab, entry_ptr, buff, buflen);

    while (1) {
        if (!entry)
            goto out;

        if (!strcmp(entry->mnt_dir, abspath) &&
            strcmp(entry->mnt_type, "rootfs"))
            break;
        entry = getmntent_r(mtab, entry_ptr, buff, buflen);
    }

out:
    if (NULL != mtab) {
        endmntent(mtab);
    }
    return entry;
}

static int
glusterd_add_brick_mount_details(glusterd_brickinfo_t *brickinfo, dict_t *dict,
                                 int count)
{
    int ret = -1;
    char key[64] = "";
    char buff[PATH_MAX] = "";
    char base_key[32] = "";
    struct mntent save_entry = {0};
    char *mnt_pt = NULL;
    struct mntent *entry = NULL;
    xlator_t *this = THIS;

    snprintf(base_key, sizeof(base_key), "brick%d", count);

    ret = glusterd_get_brick_root(brickinfo->path, &mnt_pt);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_BRICKPATH_ROOT_GET_FAIL,
                NULL);
        goto out;
    }

    entry = glusterd_get_mnt_entry_info(mnt_pt, buff, sizeof(buff),
                                        &save_entry);
    if (!entry) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_GET_MNT_ENTRY_INFO_FAIL,
                NULL);
        ret = -1;
        goto out;
    }

    /* get device file */
    snprintf(key, sizeof(key), "%s.device", base_key);

    ret = dict_set_dynstr_with_alloc(dict, key, entry->mnt_fsname);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "Key=%s", key, NULL);
        goto out;
    }

    /* fs type */
    snprintf(key, sizeof(key), "%s.fs_name", base_key);

    ret = dict_set_dynstr_with_alloc(dict, key, entry->mnt_type);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "Key=%s", key, NULL);
        goto out;
    }

    /* mount options */
    snprintf(key, sizeof(key), "%s.mnt_options", base_key);

    ret = dict_set_dynstr_with_alloc(dict, key, entry->mnt_opts);

out:
    if (mnt_pt)
        GF_FREE(mnt_pt);

    return ret;
}

char *
glusterd_get_brick_mount_device(char *brick_path)
{
    int ret = -1;
    char *mnt_pt = NULL;
    char *device = NULL;
    char buff[PATH_MAX] = "";
    struct mntent *entry = NULL;
    struct mntent save_entry = {
        0,
    };

    GF_ASSERT(brick_path);

    ret = glusterd_get_brick_root(brick_path, &mnt_pt);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_BRICKPATH_ROOT_GET_FAIL,
               "Failed to get mount point "
               "for %s brick",
               brick_path);
        goto out;
    }

    entry = glusterd_get_mnt_entry_info(mnt_pt, buff, sizeof(buff),
                                        &save_entry);
    if (NULL == entry) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_MNTENTRY_GET_FAIL,
               "Failed to get mnt entry "
               "for %s mount path",
               mnt_pt);
        goto out;
    }

    /* get the fs_name/device */
    device = gf_strdup(entry->mnt_fsname);

out:
    if (mnt_pt)
        GF_FREE(mnt_pt);

    return device;
}

int
glusterd_add_brick_detail_to_dict(glusterd_volinfo_t *volinfo,
                                  glusterd_brickinfo_t *brickinfo, dict_t *dict,
                                  int count)
{
    int ret = -1;
    uint64_t memtotal = 0;
    uint64_t memfree = 0;
    uint64_t inodes_total = 0;
    uint64_t inodes_free = 0;
    uint64_t block_size = 0;
    char key[64];
    char base_key[32];
    struct statvfs brickstat = {0};
    xlator_t *this = THIS;

    GF_ASSERT(volinfo);
    GF_ASSERT(brickinfo);
    GF_ASSERT(dict);

    snprintf(base_key, sizeof(base_key), "brick%d", count);

    ret = sys_statvfs(brickinfo->path, &brickstat);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
               "statfs error: %s ", strerror(errno));
        goto out;
    }

    /* file system block size */
    block_size = brickstat.f_bsize;
    snprintf(key, sizeof(key), "%s.block_size", base_key);
    ret = dict_set_uint64(dict, key, block_size);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "Key=%s", key, NULL);
        goto out;
    }

    /* free space in brick */
    memfree = brickstat.f_bfree * brickstat.f_bsize;
    snprintf(key, sizeof(key), "%s.free", base_key);
    ret = dict_set_uint64(dict, key, memfree);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "Key=%s", key, NULL);
        goto out;
    }

    /* total space of brick */
    memtotal = brickstat.f_blocks * brickstat.f_bsize;
    snprintf(key, sizeof(key), "%s.total", base_key);
    ret = dict_set_uint64(dict, key, memtotal);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "Key=%s", key, NULL);
        goto out;
    }

    /* inodes: total and free counts only for ext2/3/4 and xfs */
    inodes_total = brickstat.f_files;
    if (inodes_total) {
        snprintf(key, sizeof(key), "%s.total_inodes", base_key);
        ret = dict_set_uint64(dict, key, inodes_total);
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                    "Key=%s", key, NULL);
            goto out;
        }
    }

    inodes_free = brickstat.f_ffree;
    if (inodes_free) {
        snprintf(key, sizeof(key), "%s.free_inodes", base_key);
        ret = dict_set_uint64(dict, key, inodes_free);
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                    "Key=%s", key, NULL);
            goto out;
        }
    }

    ret = glusterd_add_brick_mount_details(brickinfo, dict, count);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_ADD_BRICK_MNT_INFO_FAIL,
                NULL);
        goto out;
    }

    ret = glusterd_add_inode_size_to_dict(dict, count);
out:
    if (ret)
        gf_msg_debug(this->name, errno, "Error adding brick detail to dict");
    return ret;
}

int32_t
glusterd_add_brick_to_dict(glusterd_volinfo_t *volinfo,
                           glusterd_brickinfo_t *brickinfo, dict_t *dict,
                           int32_t count)
{
    int ret = -1;
    int32_t pid = -1;
    char key[64];
    int keylen;
    char base_key[32];
    char pidfile[PATH_MAX] = "";
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    gf_boolean_t brick_online = _gf_false;
    char *brickpath = NULL;

    GF_ASSERT(volinfo);
    GF_ASSERT(brickinfo);
    GF_ASSERT(dict);

    priv = this->private;

    snprintf(base_key, sizeof(base_key), "brick%d", count);
    keylen = snprintf(key, sizeof(key), "%s.hostname", base_key);

    ret = dict_set_strn(dict, key, keylen, brickinfo->hostname);
    if (ret)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.path", base_key);
    ret = dict_set_strn(dict, key, keylen, brickinfo->path);
    if (ret)
        goto out;

    /* add peer uuid */
    snprintf(key, sizeof(key), "%s.peerid", base_key);
    ret = dict_set_dynstr_with_alloc(dict, key, uuid_utoa(brickinfo->uuid));
    if (ret) {
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "%s.port", base_key);
    ret = dict_set_int32n(
        dict, key, keylen,
        (volinfo->transport_type == GF_TRANSPORT_RDMA) ? 0 : brickinfo->port);
    if (ret)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.rdma_port", base_key);
    if (volinfo->transport_type == GF_TRANSPORT_RDMA) {
        ret = dict_set_int32n(dict, key, keylen, brickinfo->port);
    } else if (volinfo->transport_type == GF_TRANSPORT_BOTH_TCP_RDMA) {
        ret = dict_set_int32n(dict, key, keylen, brickinfo->rdma_port);
    } else
        ret = dict_set_int32n(dict, key, keylen, 0);

    if (ret)
        goto out;

    GLUSTERD_GET_BRICK_PIDFILE(pidfile, volinfo, brickinfo, priv);

    if (glusterd_is_brick_started(brickinfo)) {
        if (gf_is_service_running(pidfile, &pid) &&
            brickinfo->port_registered) {
            if (!is_brick_mx_enabled()) {
                brick_online = _gf_true;
            } else {
                brickpath = search_brick_path_from_proc(pid, brickinfo->path);
                if (!brickpath) {
                    gf_log(this->name, GF_LOG_INFO,
                           "brick path %s is not consumed", brickinfo->path);
                    brick_online = _gf_false;
                } else {
                    brick_online = _gf_true;
                    GF_FREE(brickpath);
                }
            }
        } else {
            pid = -1;
        }
    }

    keylen = snprintf(key, sizeof(key), "%s.pid", base_key);
    ret = dict_set_int32n(dict, key, keylen, pid);
    if (ret)
        goto out;

    keylen = snprintf(key, sizeof(key), "%s.status", base_key);
    ret = dict_set_int32n(dict, key, keylen, brick_online);

out:
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "Key=%s", key, NULL);
        gf_msg_debug(this->name, 0, "Returning %d", ret);
    }

    return ret;
}

int
glusterd_brick_stop(glusterd_volinfo_t *volinfo,
                    glusterd_brickinfo_t *brickinfo, gf_boolean_t del_brick)
{
    int ret = -1;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;

    conf = this->private;
    GF_ASSERT(conf);

    if ((!brickinfo) || (!volinfo)) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    if (gf_uuid_is_null(brickinfo->uuid)) {
        ret = glusterd_resolve_brick(brickinfo);
        if (ret) {
            gf_event(EVENT_BRICKPATH_RESOLVE_FAILED,
                     "peer=%s;volume=%s;brick=%s", brickinfo->hostname,
                     volinfo->volname, brickinfo->path);
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_RESOLVE_BRICK_FAIL,
                   FMTSTR_RESOLVE_BRICK, brickinfo->hostname, brickinfo->path);
            goto out;
        }
    }

    if (gf_uuid_compare(brickinfo->uuid, MY_UUID)) {
        ret = 0;
        if (del_brick)
            glusterd_delete_brick(volinfo, brickinfo);
        goto out;
    }

    ret = glusterd_volume_stop_glusterfs(volinfo, brickinfo, del_brick);
    if (ret) {
        gf_msg(this->name, GF_LOG_CRITICAL, 0, GD_MSG_BRICK_STOP_FAIL,
               "Unable to stop"
               " brick: %s:%s",
               brickinfo->hostname, brickinfo->path);
        goto out;
    }

out:
    gf_msg_debug(this->name, 0, "returning %d ", ret);
    return ret;
}

int
glusterd_is_defrag_on(glusterd_volinfo_t *volinfo)
{
    return (volinfo->rebal.defrag != NULL);
}

int
glusterd_new_brick_validate(char *brick, glusterd_brickinfo_t *brickinfo,
                            char *op_errstr, size_t len, char *op)
{
    glusterd_brickinfo_t *newbrickinfo = NULL;
    int ret = -1;
    gf_boolean_t is_allocated = _gf_false;
    glusterd_peerinfo_t *peerinfo = NULL;
    xlator_t *this = THIS;

    GF_ASSERT(brick);
    GF_ASSERT(op_errstr);

    if (!brickinfo) {
        ret = glusterd_brickinfo_new_from_brick(brick, &newbrickinfo, _gf_true,
                                                NULL);
        if (ret)
            goto out;
        is_allocated = _gf_true;
    } else {
        newbrickinfo = brickinfo;
    }

    ret = glusterd_resolve_brick(newbrickinfo);
    if (ret) {
        snprintf(op_errstr, len,
                 "Host %s is not in \'Peer "
                 "in Cluster\' state",
                 newbrickinfo->hostname);
        goto out;
    }

    if (!gf_uuid_compare(MY_UUID, newbrickinfo->uuid)) {
        /* brick is local */
        if (!glusterd_is_brickpath_available(newbrickinfo->uuid,
                                             newbrickinfo->path)) {
            snprintf(op_errstr, len,
                     "Brick: %s not available."
                     " Brick may be containing or be contained "
                     "by an existing brick.",
                     brick);
            if (op && (!strcmp(op, "GF_RESET_OP_COMMIT") ||
                       !strcmp(op, "GF_RESET_OP_COMMIT_FORCE")))
                ret = 1;
            else
                ret = -1;
            goto out;
        }

    } else {
        peerinfo = glusterd_peerinfo_find_by_uuid(newbrickinfo->uuid);
        if (peerinfo == NULL) {
            ret = -1;
            snprintf(op_errstr, len, "Failed to find host %s",
                     newbrickinfo->hostname);
            goto out;
        }

        if ((!peerinfo->connected)) {
            snprintf(op_errstr, len, "Host %s not connected",
                     newbrickinfo->hostname);
            ret = -1;
            goto out;
        }

        if (peerinfo->state != GD_FRIEND_STATE_BEFRIENDED) {
            snprintf(op_errstr, len,
                     "Host %s is not in \'Peer "
                     "in Cluster\' state",
                     newbrickinfo->hostname);
            ret = -1;
            goto out;
        }
    }

    ret = 0;
out:
    if (is_allocated)
        glusterd_brickinfo_delete(newbrickinfo);
    if (op_errstr[0] != '\0')
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_VALIDATE_FAIL, "%s",
               op_errstr);
    gf_msg_debug(this->name, 0, "returning %d ", ret);
    return ret;
}

/*path needs to be absolute; works only on gfid, volume-id*/
static int
glusterd_is_uuid_present(char *path, char *xattr, gf_boolean_t *present)
{
    GF_ASSERT(path);
    GF_ASSERT(xattr);
    GF_ASSERT(present);

    int ret = -1;
    uuid_t uid = {
        0,
    };

    if (!path || !xattr || !present)
        goto out;

    ret = sys_lgetxattr(path, xattr, &uid, 16);

    if (ret >= 0) {
        *present = _gf_true;
        ret = 0;
        goto out;
    }

    switch (errno) {
#if defined(ENODATA)
        case ENODATA: /* FALLTHROUGH */
#endif
#if defined(ENOATTR) && (ENOATTR != ENODATA)
        case ENOATTR: /* FALLTHROUGH */
#endif
        case ENOTSUP:
            *present = _gf_false;
            ret = 0;
            break;
        default:
            break;
    }
out:
    return ret;
}

/*path needs to be absolute*/
static int
glusterd_is_path_in_use(char *path, gf_boolean_t *in_use, char **op_errstr)
{
    int i = 0;
    int ret = -1;
    gf_boolean_t used = _gf_false;
    char dir[PATH_MAX] = "";
    char *curdir = NULL;
    char msg[2048] = "";
    char *keys[3] = {GFID_XATTR_KEY, GF_XATTR_VOL_ID_KEY, NULL};

    GF_ASSERT(path);
    if (!path)
        goto out;

    if (snprintf(dir, PATH_MAX, "%s", path) >= PATH_MAX)
        goto out;

    curdir = dir;
    do {
        for (i = 0; !used && keys[i]; i++) {
            ret = glusterd_is_uuid_present(curdir, keys[i], &used);
            if (ret)
                goto out;
        }

        if (used)
            break;

        curdir = dirname(curdir);
        if (!strcmp(curdir, "."))
            goto out;

    } while (strcmp(curdir, "/"));

    if (!strcmp(curdir, "/")) {
        for (i = 0; !used && keys[i]; i++) {
            ret = glusterd_is_uuid_present(curdir, keys[i], &used);
            if (ret)
                goto out;
        }
    }

    ret = 0;
    *in_use = used;
out:
    if (ret) {
        snprintf(msg, sizeof(msg),
                 "Failed to get extended "
                 "attribute %s, reason: %s",
                 keys[i], strerror(errno));
    }

    if (*in_use) {
        if (path && curdir && !strcmp(path, curdir)) {
            int z = snprintf(msg, sizeof(msg),
                             "%s is already part of a "
                             "volume",
                             path);
            if (z < 0 || z >= sizeof(msg)) {
                snprintf(msg, sizeof(msg), "path too big");
                ret = -1;
            }
        } else {
            snprintf(msg, sizeof(msg),
                     "parent directory %s is "
                     "already part of a volume",
                     curdir);
        }
    }

    if (strlen(msg)) {
        gf_msg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_PATH_ALREADY_PART_OF_VOL,
               "%s", msg);
        *op_errstr = gf_strdup(msg);
    }

    return ret;
}

static int
glusterd_check_and_set_brick_xattr(char *host, char *path, uuid_t uuid,
                                   char **op_errstr, gf_boolean_t is_force)
{
    int ret = -1;
    char msg[2048] = "";
    gf_boolean_t in_use = _gf_false;
    int flags = 0;
    xlator_t *this = THIS;

    /* Check for xattr support in backend fs */
    ret = sys_lsetxattr(path, "trusted.glusterfs.test", "working", 8, 0);
    if (ret == -1) {
        ret = snprintf(msg, sizeof(msg),
                       "Glusterfs is not"
                       " supported on brick: %s:%s.\nSetting"
                       " extended attributes failed, reason:"
                       " %s.",
                       host, path, strerror(errno));
        if (ret < 0 || ret >= sizeof(msg)) {
            snprintf(msg, sizeof(msg), "path too big");
            ret = -1;
            goto out;
        }
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_SET_XATTR_BRICK_FAIL,
                "Host=%s, Path=%s", host, path, NULL);
        goto out;

    } else {
        ret = sys_lremovexattr(path, "trusted.glusterfs.test");
        if (ret) {
            snprintf(msg, sizeof(msg),
                     "Removing test extended"
                     " attribute failed, reason: %s",
                     strerror(errno));
            gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_REMOVE_XATTR_FAIL,
                    NULL);
            goto out;
        }
    }

    ret = glusterd_is_path_in_use(path, &in_use, op_errstr);
    if (ret)
        goto out;

    if (in_use && !is_force) {
        ret = -1;
        goto out;
    }

    if (!is_force)
        flags = XATTR_CREATE;

    ret = sys_lsetxattr(path, GF_XATTR_VOL_ID_KEY, uuid, 16, flags);
    if (ret == -1) {
        snprintf(msg, sizeof(msg),
                 "Failed to set extended "
                 "attributes %s, reason: %s",
                 GF_XATTR_VOL_ID_KEY, strerror(errno));
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_SET_XATTR_FAIL,
                "Attriutes=%s", GF_XATTR_VOL_ID_KEY, NULL);
        goto out;
    }

    ret = 0;
out:
    if (strlen(msg)) {
        if (*op_errstr)
            GF_FREE(*op_errstr);
        *op_errstr = gf_strdup(msg);
    }

    return ret;
}

int
glusterd_sm_tr_log_init(glusterd_sm_tr_log_t *log, char *(*state_name_get)(int),
                        char *(*event_name_get)(int), size_t size)
{
    glusterd_sm_transition_t *transitions = NULL;
    int ret = -1;

    GF_ASSERT(size > 0);
    GF_ASSERT(log && state_name_get && event_name_get);

    if (!log || !state_name_get || !event_name_get || (size <= 0))
        goto out;

    transitions = GF_CALLOC(size, sizeof(*transitions), gf_gld_mt_sm_tr_log_t);
    if (!transitions)
        goto out;

    log->transitions = transitions;
    log->size = size;
    log->state_name_get = state_name_get;
    log->event_name_get = event_name_get;
    ret = 0;

out:
    gf_msg_debug("glusterd", 0, "returning %d", ret);
    return ret;
}

void
glusterd_sm_tr_log_delete(glusterd_sm_tr_log_t *log)
{
    if (!log)
        return;
    GF_FREE(log->transitions);
    return;
}

int
glusterd_sm_tr_log_transition_add(glusterd_sm_tr_log_t *log, int old_state,
                                  int new_state, int event)
{
    glusterd_sm_transition_t *transitions = NULL;
    int ret = -1;
    int next = 0;
    xlator_t *this = THIS;

    GF_ASSERT(log);
    if (!log)
        goto out;

    transitions = log->transitions;
    if (!transitions)
        goto out;

    if (log->count)
        next = (log->current + 1) % log->size;
    else
        next = 0;

    transitions[next].old_state = old_state;
    transitions[next].new_state = new_state;
    transitions[next].event = event;
    transitions[next].time = gf_time();

    log->current = next;
    if (log->count < log->size)
        log->count++;
    ret = 0;
    gf_msg_debug(this->name, 0,
                 "Transitioning from '%s' to '%s' "
                 "due to event '%s'",
                 log->state_name_get(old_state), log->state_name_get(new_state),
                 log->event_name_get(event));
out:
    gf_msg_debug(this->name, 0, "returning %d", ret);
    return ret;
}

int32_t
glusterd_delete_volume(glusterd_volinfo_t *volinfo)
{
    int ret = -1;
    GF_ASSERT(volinfo);

    ret = glusterd_store_delete_volume(volinfo);

    if (ret)
        goto out;

    glusterd_volinfo_remove(volinfo);
out:
    gf_msg_debug(THIS->name, 0, "returning %d", ret);
    return ret;
}

int32_t
glusterd_delete_brick(glusterd_volinfo_t *volinfo,
                      glusterd_brickinfo_t *brickinfo)
{
    int ret = 0;
    char voldir[PATH_MAX] = "";
    glusterd_conf_t *priv = THIS->private;
    GF_ASSERT(volinfo);
    GF_ASSERT(brickinfo);

    GLUSTERD_GET_VOLUME_DIR(voldir, volinfo, priv);

    glusterd_delete_volfile(volinfo, brickinfo);
    glusterd_store_delete_brick(brickinfo, voldir);
    glusterd_brickinfo_delete(brickinfo);
    volinfo->brick_count--;
    return ret;
}

static int32_t
glusterd_delete_all_bricks(glusterd_volinfo_t *volinfo)
{
    int ret = 0;
    glusterd_brickinfo_t *brickinfo = NULL;
    glusterd_brickinfo_t *tmp = NULL;

    GF_ASSERT(volinfo);

    cds_list_for_each_entry_safe(brickinfo, tmp, &volinfo->bricks, brick_list)
    {
        ret = glusterd_delete_brick(volinfo, brickinfo);
    }
    return ret;
}

int
glusterd_get_local_brickpaths(glusterd_volinfo_t *volinfo, char **pathlist)
{
    char **path_tokens = NULL;
    char *tmp_path_list = NULL;
    char path[PATH_MAX] = "";
    int32_t count = 0;
    int32_t pathlen = 0;
    int32_t total_len = 0;
    int32_t ret = 0;
    int i = 0;
    glusterd_brickinfo_t *brickinfo = NULL;
    unsigned char *uuid;

    if ((!volinfo) || (!pathlist)) {
        gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_INVALID_ARGUMENT, NULL);
        goto out;
    }

    path_tokens = GF_CALLOC(sizeof(char *), volinfo->brick_count,
                            gf_gld_mt_charptr);
    if (!path_tokens) {
        gf_msg("glusterd", GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Could not allocate memory.");
        ret = -1;
        goto out;
    }

    uuid = MY_UUID;
    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        if (gf_uuid_compare(brickinfo->uuid, uuid))
            continue;

        pathlen = snprintf(path, sizeof(path), "--path=%s ", brickinfo->path);
        if (pathlen < sizeof(path))
            path[pathlen] = '\0';
        else
            path[sizeof(path) - 1] = '\0';
        path_tokens[count] = gf_strdup(path);
        if (!path_tokens[count]) {
            gf_msg("glusterd", GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                   "Could not allocate memory.");
            ret = -1;
            goto out;
        }
        count++;
        total_len += pathlen;
    }

    tmp_path_list = GF_CALLOC(sizeof(char), total_len + 1, gf_gld_mt_char);
    if (!tmp_path_list) {
        gf_msg("glusterd", GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "Could not allocate memory.");
        ret = -1;
        goto out;
    }

    for (i = 0; i < count; i++)
        strcat(tmp_path_list, path_tokens[i]);

    if (count)
        *pathlist = tmp_path_list;

    ret = count;
out:
    if (path_tokens) {
        for (i = 0; i < count; i++) {
            GF_FREE(path_tokens[i]);
        }
    }

    GF_FREE(path_tokens);
    path_tokens = NULL;

    if (ret == 0) {
        gf_msg_debug("glusterd", 0, "No Local Bricks Present.");
        GF_FREE(tmp_path_list);
        tmp_path_list = NULL;
    }

    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

int
glusterd_start_gsync(glusterd_volinfo_t *primary_vol, char *secondary,
                     char *path_list, char *conf_path, char *glusterd_uuid_str,
                     char **op_errstr, gf_boolean_t is_pause)
{
    int32_t ret = 0;
    int32_t status = 0;
    char uuid_str[64] = "";
    runner_t runner = {
        0,
    };
    glusterd_conf_t *priv = NULL;
    int errcode = 0;
    gf_boolean_t is_template_in_use = _gf_false;

    priv = THIS->private;
    GF_ASSERT(priv);

    uuid_utoa_r(MY_UUID, uuid_str);

    if (!path_list) {
        ret = 0;
        gf_msg_debug("glusterd", 0,
                     "No Bricks in this node."
                     " Not starting gsyncd.");
        goto out;
    }

    ret = gsync_status(primary_vol->volname, secondary, conf_path, &status,
                       &is_template_in_use);
    if (status == 0)
        goto out;

    if (is_template_in_use == _gf_true) {
        gf_asprintf(op_errstr,
                    GEOREP
                    " start failed for %s %s : "
                    "pid-file entry missing in config file",
                    primary_vol->volname, secondary);
        ret = -1;
        goto out;
    }

    uuid_utoa_r(primary_vol->volume_id, uuid_str);
    runinit(&runner);
    runner_add_args(&runner, GSYNCD_PREFIX "/gsyncd", path_list, "-c", NULL);
    runner_argprintf(&runner, "%s", conf_path);
    runner_argprintf(&runner, ":%s", primary_vol->volname);
    runner_add_args(&runner, secondary, "--config-set", "session-owner", NULL);
    runner_argprintf(&runner, "--value=%s", uuid_str);
    synclock_unlock(&priv->big_lock);
    ret = runner_run(&runner);
    synclock_lock(&priv->big_lock);
    if (ret == -1) {
        errcode = -1;
        goto out;
    }

    runinit(&runner);
    runner_add_args(&runner, GSYNCD_PREFIX "/gsyncd", path_list, "--monitor",
                    "-c", NULL);
    runner_argprintf(&runner, "%s", conf_path);
    runner_argprintf(&runner, "--iprefix=%s", DATADIR);
    runner_argprintf(&runner, ":%s", primary_vol->volname);
    runner_argprintf(&runner, "--glusterd-uuid=%s", uuid_utoa(priv->uuid));
    runner_add_arg(&runner, secondary);
    if (is_pause)
        runner_add_arg(&runner, "--pause-on-start");
    synclock_unlock(&priv->big_lock);
    ret = runner_run(&runner);
    synclock_lock(&priv->big_lock);
    if (ret == -1) {
        gf_asprintf(op_errstr, GEOREP " start failed for %s %s",
                    primary_vol->volname, secondary);
        goto out;
    }

    ret = 0;

out:
    if ((ret != 0) && errcode == -1) {
        if (op_errstr)
            *op_errstr = gf_strdup(
                "internal error, cannot start "
                "the " GEOREP " session");
    }

    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

int32_t
glusterd_recreate_volfiles(glusterd_conf_t *conf)
{
    glusterd_volinfo_t *volinfo = NULL;
    int ret = 0;
    int op_ret = 0;

    GF_ASSERT(conf);

    cds_list_for_each_entry(volinfo, &conf->volumes, vol_list)
    {
        ret = glusterd_create_volfiles(volinfo);
        if (ret) {
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_VOLFILE_CREATE_FAIL,
                   "Failed to "
                   "regenerate volfile(s) for %s",
                   volinfo->volname);
            op_ret = ret;
        }
    }
    return op_ret;
}

static inline int
glusterd_is_replica_volume(int type)
{
    if (type == GF_CLUSTER_TYPE_REPLICATE)
        return 1;
    return 0;
}
gf_boolean_t
glusterd_is_volume_replicate(glusterd_volinfo_t *volinfo)
{
    return glusterd_is_replica_volume((volinfo->type));
}

static gf_boolean_t
glusterd_is_shd_compatible_type(int type)
{
    switch (type) {
        case GF_CLUSTER_TYPE_REPLICATE:
        case GF_CLUSTER_TYPE_DISPERSE:
            return _gf_true;
    }
    return _gf_false;
}

gf_boolean_t
glusterd_is_shd_compatible_volume(glusterd_volinfo_t *volinfo)
{
    return glusterd_is_shd_compatible_type(volinfo->type);
}

static int
glusterd_set_dump_options(char *dumpoptions_path, char *options, int option_cnt)
{
    int ret = 0;
    char *dup_options = NULL;
    char *option = NULL;
    char *tmpptr = NULL;
    FILE *fp = NULL;
    int nfs_cnt = 0;
    glusterd_conf_t *priv = NULL;

    priv = THIS->private;
    GF_ASSERT(priv);

    if (0 == option_cnt || (option_cnt == 1 && (!strcmp(options, "nfs ")))) {
        ret = 0;
        goto out;
    }

    fp = fopen(dumpoptions_path, "w");
    if (!fp) {
        ret = -1;
        goto out;
    }
    dup_options = gf_strdup(options);

    if (!dup_options) {
        goto out;
    }
    gf_msg("glusterd", GF_LOG_INFO, 0, GD_MSG_STATEDUMP_OPTS_RCVD,
           "Received following statedump options: %s", dup_options);
    option = strtok_r(dup_options, " ", &tmpptr);
    while (option) {
        if (!strcmp(option, priv->nfs_svc.name)) {
            if (nfs_cnt > 0) {
                sys_unlink(dumpoptions_path);
                ret = 0;
                goto out;
            }
            nfs_cnt++;
            option = strtok_r(NULL, " ", &tmpptr);
            continue;
        }
        fprintf(fp, "%s=yes\n", option);
        option = strtok_r(NULL, " ", &tmpptr);
    }

out:
    if (fp)
        fclose(fp);
    GF_FREE(dup_options);
    return ret;
}

static int
glusterd_brick_signal(glusterd_volinfo_t *volinfo,
                      glusterd_brickinfo_t *brickinfo, char *options,
                      int option_cnt, char **op_errstr, int sig)
{
    int ret = -1;
    glusterd_conf_t *conf = NULL;
    char pidfile_path[PATH_MAX] = "";
    char dumpoptions_path[PATH_MAX] = "";
    FILE *pidfile = NULL;
    pid_t pid = -1;

    conf = THIS->private;
    GF_ASSERT(conf);

    if (gf_uuid_is_null(brickinfo->uuid)) {
        ret = glusterd_resolve_brick(brickinfo);
        if (ret) {
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_RESOLVE_BRICK_FAIL,
                   "Cannot resolve brick %s:%s", brickinfo->hostname,
                   brickinfo->path);
            goto out;
        }
    }

    if (gf_uuid_compare(brickinfo->uuid, MY_UUID)) {
        ret = 0;
        goto out;
    }

    GLUSTERD_GET_BRICK_PIDFILE(pidfile_path, volinfo, brickinfo, conf);

    /* TBD: use gf_is_service_running instead of almost-identical code? */
    pidfile = fopen(pidfile_path, "r");
    if (!pidfile) {
        gf_msg("glusterd", GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
               "Unable to open pidfile: %s", pidfile_path);
        ret = -1;
        goto out;
    }

    ret = fscanf(pidfile, "%d", &pid);
    if (ret <= 0) {
        gf_msg("glusterd", GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
               "Unable to get pid of brick process");
        ret = -1;
        goto out;
    }

    if (pid == 0) {
        gf_msg("glusterd", GF_LOG_WARNING, 0, GD_MSG_NO_SIG_TO_PID_ZERO,
               "refusing to send signal %d to pid zero", sig);
        goto out;
    }

    if (sig == SIGUSR1) {
        snprintf(dumpoptions_path, sizeof(dumpoptions_path),
                 DEFAULT_VAR_RUN_DIRECTORY "/glusterdump.%d.options", pid);
        ret = glusterd_set_dump_options(dumpoptions_path, options, option_cnt);
        if (ret < 0) {
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_BRK_STATEDUMP_FAIL,
                   "error while parsing the statedump "
                   "options");
            ret = -1;
            goto out;
        }
    }

    gf_msg("glusterd", GF_LOG_INFO, 0, GD_MSG_STATEDUMP_INFO,
           "sending signal %d to brick with pid %d", sig, pid);

    kill(pid, sig);

    sleep(1);
    sys_unlink(dumpoptions_path);
    ret = 0;
out:
    if (pidfile)
        fclose(pidfile);
    return ret;
}

int
glusterd_brick_statedump(glusterd_volinfo_t *volinfo,
                         glusterd_brickinfo_t *brickinfo, char *options,
                         int option_cnt, char **op_errstr)
{
    return glusterd_brick_signal(volinfo, brickinfo, options, option_cnt,
                                 op_errstr, SIGUSR1);
}

static int
glusterd_brick_terminate(glusterd_volinfo_t *volinfo,
                         glusterd_brickinfo_t *brickinfo, char *options,
                         int option_cnt, char **op_errstr)
{
    return glusterd_brick_signal(volinfo, brickinfo, options, option_cnt,
                                 op_errstr, SIGTERM);
}

#ifdef BUILD_GNFS
int
glusterd_nfs_statedump(char *options, int option_cnt, char **op_errstr)
{
    int ret = -1;
    glusterd_conf_t *conf = NULL;
    char pidfile_path[PATH_MAX] = "";
    FILE *pidfile = NULL;
    pid_t pid = -1;
    char dumpoptions_path[PATH_MAX] = "";
    char *option = NULL;
    char *tmpptr = NULL;
    char *dup_options = NULL;
    char msg[256] = "";

    conf = THIS->private;
    GF_ASSERT(conf);

    dup_options = gf_strdup(options);

    if (!dup_options) {
        goto out;
    }
    option = strtok_r(dup_options, " ", &tmpptr);
    if (strcmp(option, conf->nfs_svc.name)) {
        snprintf(msg, sizeof(msg),
                 "for nfs statedump, options should"
                 " be after the key nfs");
        gf_smsg("glusterd", GF_LOG_ERROR, errno, GD_MSG_INVALID_ENTRY,
                "Options misplaced", NULL);
        *op_errstr = gf_strdup(msg);
        ret = -1;
        goto out;
    }

    GLUSTERD_GET_NFS_PIDFILE(pidfile_path, conf);

    pidfile = fopen(pidfile_path, "r");
    if (!pidfile) {
        gf_msg("glusterd", GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
               "Unable to open pidfile: %s", pidfile_path);
        ret = -1;
        goto out;
    }

    ret = fscanf(pidfile, "%d", &pid);
    if (ret <= 0) {
        gf_msg("glusterd", GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
               "Unable to get pid of brick process");
        ret = -1;
        goto out;
    }

    snprintf(dumpoptions_path, sizeof(dumpoptions_path),
             DEFAULT_VAR_RUN_DIRECTORY "/glusterdump.%d.options", pid);
    ret = glusterd_set_dump_options(dumpoptions_path, options, option_cnt);
    if (ret < 0) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_BRK_STATEDUMP_FAIL,
               "error while parsing the statedump "
               "options");
        ret = -1;
        goto out;
    }

    gf_msg("glusterd", GF_LOG_INFO, 0, GD_MSG_STATEDUMP_INFO,
           "Performing statedump on nfs server with "
           "pid %d",
           pid);

    kill(pid, SIGUSR1);

    sleep(1);
    /* coverity[TAINTED_STRING] */
    sys_unlink(dumpoptions_path);
    ret = 0;
out:
    if (pidfile)
        fclose(pidfile);
    GF_FREE(dup_options);
    return ret;
}
#endif

int
glusterd_quotad_statedump(char *options, int option_cnt, char **op_errstr)
{
    int ret = -1;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    char pidfile_path[PATH_MAX] = "";
    FILE *pidfile = NULL;
    pid_t pid = -1;
    char dumpoptions_path[PATH_MAX] = "";
    char *option = NULL;
    char *tmpptr = NULL;
    char *dup_options = NULL;
    char msg[256] = "";

    conf = this->private;
    GF_ASSERT(conf);

    dup_options = gf_strdup(options);
    if (!dup_options) {
        goto out;
    }
    option = strtok_r(dup_options, " ", &tmpptr);
    if (strcmp(option, conf->quotad_svc.name)) {
        snprintf(msg, sizeof(msg),
                 "for quotad statedump, options "
                 "should be after the key 'quotad'");
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_INVALID_ENTRY,
                "Options misplaced", NULL);
        *op_errstr = gf_strdup(msg);
        ret = -1;
        goto out;
    }

    GLUSTERD_GET_QUOTAD_PIDFILE(pidfile_path, conf);

    pidfile = fopen(pidfile_path, "r");
    if (!pidfile) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
               "Unable to open pidfile: %s", pidfile_path);
        ret = -1;
        goto out;
    }

    ret = fscanf(pidfile, "%d", &pid);
    if (ret <= 0) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
               "Unable to get pid of quotad "
               "process");
        ret = -1;
        goto out;
    }

    snprintf(dumpoptions_path, sizeof(dumpoptions_path),
             DEFAULT_VAR_RUN_DIRECTORY "/glusterdump.%d.options", pid);
    ret = glusterd_set_dump_options(dumpoptions_path, options, option_cnt);
    if (ret < 0) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRK_STATEDUMP_FAIL,
               "error while parsing "
               "statedump options");
        ret = -1;
        goto out;
    }

    gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_STATEDUMP_INFO,
           "Performing statedump on quotad with "
           "pid %d",
           pid);

    kill(pid, SIGUSR1);

    sleep(1);

    /* coverity[TAINTED_STRING] */
    sys_unlink(dumpoptions_path);
    ret = 0;
out:
    if (pidfile)
        fclose(pidfile);
    GF_FREE(dup_options);
    return ret;
}

/* Checks if the given peer contains bricks belonging to the given volume.
 * Returns,
 *   2 - if peer contains all the bricks
 *   1 - if peer contains at least 1 brick
 *   0 - if peer contains no bricks
 */
int
glusterd_friend_contains_vol_bricks(glusterd_volinfo_t *volinfo,
                                    uuid_t friend_uuid)
{
    int ret = 0;
    glusterd_brickinfo_t *brickinfo = NULL;
    int count = 0;

    GF_ASSERT(volinfo);

    cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        if (!gf_uuid_compare(brickinfo->uuid, friend_uuid)) {
            count++;
        }
    }

    if (count) {
        if (count == volinfo->brick_count)
            ret = 2;
        else
            ret = 1;
    }
    gf_msg_debug(THIS->name, 0, "Returning %d", ret);
    return ret;
}

/* Cleanup the stale volumes left behind in the cluster. The volumes which are
 * contained completely within the detached peer are stale with respect to the
 * cluster.
 */
int
glusterd_friend_remove_cleanup_vols(uuid_t uuid)
{
    int ret = -1;
    glusterd_conf_t *priv = NULL;
    glusterd_svc_t *svc = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_volinfo_t *tmp_volinfo = NULL;

    xlator_t *this = THIS;
    priv = this->private;
    GF_ASSERT(priv);

    cds_list_for_each_entry_safe(volinfo, tmp_volinfo, &priv->volumes, vol_list)
    {
        if (!glusterd_friend_contains_vol_bricks(volinfo, MY_UUID)) {
            /*Stop snapd daemon service if snapd daemon is running*/
            if (!volinfo->is_snap_volume) {
                svc = &(volinfo->snapd.svc);
                ret = svc->stop(svc, SIGTERM);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SVC_STOP_FAIL,
                           "Failed "
                           "to stop snapd daemon service");
                }
            }

            if (glusterd_is_shd_compatible_volume(volinfo)) {
                /*
                 * Sending stop request for all volumes. So it is fine
                 * to send stop for mux shd
                 */
                svc = &(volinfo->shd.svc);
                ret = svc->stop(svc, SIGTERM);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SVC_STOP_FAIL,
                           "Failed "
                           "to stop shd daemon service");
                }
            }
        }

        if (glusterd_friend_contains_vol_bricks(volinfo, uuid) == 2) {
            gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_STALE_VOL_DELETE_INFO,
                   "Deleting stale volume %s", volinfo->volname);
            ret = glusterd_delete_volume(volinfo);
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_STALE_VOL_REMOVE_FAIL,
                       "Error deleting stale volume");
                goto out;
            }
        }
    }

    /* Reconfigure all daemon services upon peer detach */
    ret = glusterd_svcs_reconfigure(NULL);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_SVC_STOP_FAIL,
               "Failed to reconfigure all daemon services.");
    }
    ret = 0;
out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_get_trusted_client_filepath(char *filepath,
                                     glusterd_volinfo_t *volinfo,
                                     gf_transport_type type)
{
    int ret = 0;
    char path[PATH_MAX] = "";
    int32_t len = 0;
    glusterd_conf_t *priv = NULL;

    priv = THIS->private;

    GLUSTERD_GET_VOLUME_DIR(path, volinfo, priv);

    switch (type) {
        case GF_TRANSPORT_TCP:
            len = snprintf(filepath, PATH_MAX, "%s/trusted-%s.tcp-fuse.vol",
                           path, volinfo->volname);
            break;

        case GF_TRANSPORT_RDMA:
            len = snprintf(filepath, PATH_MAX, "%s/trusted-%s.rdma-fuse.vol",
                           path, volinfo->volname);
            break;
        default:
            ret = -1;
            break;
    }
    if ((len < 0) || (len >= PATH_MAX)) {
        ret = -1;
    }

    return ret;
}

int
glusterd_get_dummy_client_filepath(char *filepath, glusterd_volinfo_t *volinfo,
                                   gf_transport_type type)
{
    char *suffix;
    int32_t ret;

    if ((type == GF_TRANSPORT_TCP) || (type == GF_TRANSPORT_BOTH_TCP_RDMA)) {
        suffix = "tcp-fuse";
    } else if (type == GF_TRANSPORT_RDMA) {
        suffix = "rdma-fuse";
    } else {
        return -1;
    }

    ret = snprintf(filepath, PATH_MAX, "/tmp/%s.%s.%d.vol", volinfo->volname,
                   suffix, getpid());
    if ((ret < 0) || (ret >= PATH_MAX)) {
        return -1;
    }

    return 0;
}

static int
glusterd_volume_defrag_restart(glusterd_volinfo_t *volinfo, char *op_errstr,
                               size_t len, int cmd, defrag_cbk_fn_t cbk)
{
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    char pidfile[PATH_MAX] = "";
    int ret = -1;
    pid_t pid = 0;
    int refcnt = 0;

    priv = this->private;
    if (!priv)
        return ret;

    /* Don't start the rebalance process if the stautus is already
     * completed, stopped or failed. If the status is started, check if
     * there is an existing process already and connect to it. If not, then
     * start the rebalance process
     */

    switch (volinfo->rebal.defrag_status) {
        case GF_DEFRAG_STATUS_COMPLETE:
        case GF_DEFRAG_STATUS_STOPPED:
        case GF_DEFRAG_STATUS_FAILED:
            break;
        case GF_DEFRAG_STATUS_STARTED:
            GLUSTERD_GET_DEFRAG_PID_FILE(pidfile, volinfo, priv);
            if (gf_is_service_running(pidfile, &pid)) {
                ret = glusterd_rebalance_defrag_init(volinfo, cbk);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, 0,
                           GD_MSG_REBALANCE_START_FAIL,
                           "Failed to initialize  defrag."
                           "Not starting rebalance process for "
                           "%s.",
                           volinfo->volname);
                    gf_event(EVENT_REBALANCE_START_FAILED, "volume=%s",
                             volinfo->volname);
                    goto out;
                }
                refcnt = glusterd_defrag_ref(volinfo->rebal.defrag);
                /* If refcnt value is 1 it means either defrag object is
                   poulated by glusterd_rebalance_defrag_init or previous
                   rpc creation was failed.If it is not 1 it means it(defrag)
                   was populated at the time of start a rebalance daemon.
                   We need to create a rpc object only while a previous
                   rpc connection was not established successfully at the
                   time of restart a rebalance daemon by
                   glusterd_handle_defrag_start otherwise rebalance cli
                   does not show correct status after just reboot a node and try
                   to print the rebalance status because defrag object has been
                   destroyed during handling of rpc disconnect.
                */
                if (refcnt == 1) {
                    ret = glusterd_rebalance_rpc_create(volinfo);
                } else {
                    ret = 0;
                    glusterd_defrag_unref(volinfo->rebal.defrag);
                }
                break;
            }
        case GF_DEFRAG_STATUS_NOT_STARTED:
            ret = glusterd_handle_defrag_start(volinfo, op_errstr, len, cmd,
                                               cbk, volinfo->rebal.op);
            if (ret) {
                volinfo->rebal.defrag_status = GF_DEFRAG_STATUS_FAILED;
                gf_event(EVENT_REBALANCE_START_FAILED, "volume=%s",
                         volinfo->volname);
            }
            break;
        default:
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_REBALANCE_START_FAIL,
                   "Unknown defrag status (%d)."
                   "Not starting rebalance process for %s.",
                   volinfo->rebal.defrag_status, volinfo->volname);
            break;
    }
out:
    return ret;
}

int
glusterd_restart_rebalance_for_volume(glusterd_volinfo_t *volinfo)
{
    int ret = -1;
    char op_errstr[PATH_MAX] = "";

    if (!gd_should_i_start_rebalance(volinfo)) {
        /* Store the rebalance-id and rebalance command even if
         * the peer isn't starting a rebalance process. On peers
         * where a rebalance process is started,
         * glusterd_handle_defrag_start performs the storing.
         *
         * Storing this is needed for having 'volume status'
         * work correctly.
         */
        volinfo->rebal.defrag_status = GF_DEFRAG_STATUS_NOT_STARTED;
        return 0;
    }
    if (!volinfo->rebal.defrag_cmd) {
        volinfo->rebal.defrag_status = GF_DEFRAG_STATUS_FAILED;
        return -1;
    }

    ret = glusterd_volume_defrag_restart(volinfo, op_errstr, PATH_MAX,
                                         volinfo->rebal.defrag_cmd,
                                         volinfo->rebal.op == GD_OP_REMOVE_BRICK
                                             ? glusterd_remove_brick_migrate_cbk
                                             : NULL);
    if (!ret) {
        /* If remove brick is started then ensure that on a glusterd
         * restart decommission_is_in_progress is set to avoid remove
         * brick commit to happen when rebalance is not completed.
         */
        if (volinfo->rebal.op == GD_OP_REMOVE_BRICK &&
            volinfo->rebal.defrag_status == GF_DEFRAG_STATUS_STARTED) {
            volinfo->decommission_in_progress = 1;
        }
    }
    return ret;
}

void
glusterd_volinfo_reset_defrag_stats(glusterd_volinfo_t *volinfo)
{
    glusterd_rebalance_t *rebal = NULL;
    GF_ASSERT(volinfo);

    rebal = &volinfo->rebal;
    rebal->rebalance_files = 0;
    rebal->rebalance_data = 0;
    rebal->lookedup_files = 0;
    rebal->rebalance_failures = 0;
    rebal->rebalance_time = 0;
    rebal->skipped_files = 0;
}

gf_boolean_t
glusterd_is_local_brick(glusterd_volinfo_t *volinfo,
                        glusterd_brickinfo_t *brickinfo)
{
    gf_boolean_t local = _gf_false;
    int ret = 0;

    if (gf_uuid_is_null(brickinfo->uuid)) {
        ret = glusterd_resolve_brick(brickinfo);
        if (ret)
            goto out;
    }
    local = !gf_uuid_compare(brickinfo->uuid, MY_UUID);
out:
    return local;
}
int
glusterd_validate_volume_id(dict_t *op_dict, glusterd_volinfo_t *volinfo)
{
    int ret = -1;
    char *volid_str = NULL;
    uuid_t vol_uid = {
        0,
    };
    xlator_t *this = THIS;

    ret = dict_get_str(op_dict, "vol-id", &volid_str);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get volume id for "
               "volume %s",
               volinfo->volname);
        goto out;
    }
    ret = gf_uuid_parse(volid_str, vol_uid);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_UUID_PARSE_FAIL,
               "Failed to parse volume id "
               "for volume %s",
               volinfo->volname);
        goto out;
    }

    if (gf_uuid_compare(vol_uid, volinfo->volume_id)) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_ID_MISMATCH,
               "Volume ids of volume %s - %s"
               " and %s - are different. Possibly a split brain among "
               "peers.",
               volinfo->volname, volid_str, uuid_utoa(volinfo->volume_id));
        ret = -1;
        goto out;
    }

out:
    return ret;
}

int
glusterd_defrag_volume_status_update(glusterd_volinfo_t *volinfo,
                                     dict_t *rsp_dict, int32_t cmd)
{
    int ret = 0;
    int ret2 = 0;
    uint64_t files = 0;
    uint64_t size = 0;
    uint64_t lookup = 0;
    gf_defrag_status_t status = GF_DEFRAG_STATUS_NOT_STARTED;
    uint64_t failures = 0;
    uint64_t skipped = 0;
    xlator_t *this = THIS;
    double run_time = 0;
    uint64_t promoted = 0;
    uint64_t demoted = 0;
    time_t time_left = 0;

    ret = dict_get_uint64(rsp_dict, "files", &files);
    if (ret)
        gf_msg_trace(this->name, 0, "failed to get file count");

    ret = dict_get_uint64(rsp_dict, "size", &size);
    if (ret)
        gf_msg_trace(this->name, 0, "failed to get size of xfer");

    ret = dict_get_uint64(rsp_dict, "lookups", &lookup);
    if (ret)
        gf_msg_trace(this->name, 0, "failed to get lookedup file count");

    ret = dict_get_int32(rsp_dict, "status", (int32_t *)&status);
    if (ret)
        gf_msg_trace(this->name, 0, "failed to get status");

    ret = dict_get_uint64(rsp_dict, "failures", &failures);
    if (ret)
        gf_msg_trace(this->name, 0, "failed to get failure count");

    ret = dict_get_uint64(rsp_dict, "skipped", &skipped);
    if (ret)
        gf_msg_trace(this->name, 0, "failed to get skipped count");

    ret = dict_get_uint64(rsp_dict, "promoted", &promoted);
    if (ret)
        gf_msg_trace(this->name, 0, "failed to get promoted count");

    ret = dict_get_uint64(rsp_dict, "demoted", &demoted);
    if (ret)
        gf_msg_trace(this->name, 0, "failed to get demoted count");

    ret = dict_get_double(rsp_dict, "run-time", &run_time);
    if (ret)
        gf_msg_trace(this->name, 0, "failed to get run-time");

    ret2 = dict_get_time(rsp_dict, "time-left", &time_left);
    if (ret2)
        gf_msg_trace(this->name, 0, "failed to get time left");

    if (files)
        volinfo->rebal.rebalance_files = files;
    if (size)
        volinfo->rebal.rebalance_data = size;
    if (lookup)
        volinfo->rebal.lookedup_files = lookup;
    if (status)
        volinfo->rebal.defrag_status = status;
    if (failures)
        volinfo->rebal.rebalance_failures = failures;
    if (skipped)
        volinfo->rebal.skipped_files = skipped;
    if (run_time)
        volinfo->rebal.rebalance_time = run_time;
    if (!ret2)
        volinfo->rebal.time_left = time_left;

    return ret;
}

int
glusterd_check_topology_identical(const char *filename1, const char *filename2,
                                  gf_boolean_t *identical)
{
    int ret = -1; /* FAILURE */
    xlator_t *this = THIS;
    FILE *fp1 = NULL;
    FILE *fp2 = NULL;
    glusterfs_graph_t *grph1 = NULL;
    glusterfs_graph_t *grph2 = NULL;

    /* Sanitize the inputs */
    GF_VALIDATE_OR_GOTO(this->name, filename1, out);
    GF_VALIDATE_OR_GOTO(this->name, filename2, out);
    GF_VALIDATE_OR_GOTO(this->name, identical, out);

    /* fopen() the volfile1 to create the graph */
    fp1 = fopen(filename1, "r");
    if (fp1 == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
               "fopen() on file: %s failed "
               "(%s)",
               filename1, strerror(errno));
        goto out;
    }

    /* fopen() the volfile2 to create the graph */
    fp2 = fopen(filename2, "r");
    if (fp2 == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
               "fopen() on file: %s failed "
               "(%s)",
               filename2, strerror(errno));
        goto out;
    }

    /* create the graph for filename1 */
    grph1 = glusterfs_graph_construct(fp1);
    if (grph1 == NULL)
        goto out;

    /* create the graph for filename2 */
    grph2 = glusterfs_graph_construct(fp2);
    if (grph2 == NULL)
        goto out;

    /* compare the graph topology */
    *identical = is_graph_topology_equal(grph1, grph2);
    ret = 0; /* SUCCESS */
out:
    if (fp1)
        fclose(fp1);
    if (fp2)
        fclose(fp2);
    if (grph1)
        glusterfs_graph_destroy(grph1);
    if (grph2)
        glusterfs_graph_destroy(grph2);

    gf_msg_debug(this->name, 0, "Returning with %d", ret);
    return ret;
}

int
glusterd_check_files_identical(char *filename1, char *filename2,
                               gf_boolean_t *identical)
{
    int ret = -1;
    struct stat buf1 = {
        0,
    };
    struct stat buf2 = {
        0,
    };
    uint32_t cksum1 = 0;
    uint32_t cksum2 = 0;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;

    GF_ASSERT(filename1);
    GF_ASSERT(filename2);
    GF_ASSERT(identical);

    priv = this->private;
    GF_VALIDATE_OR_GOTO(this->name, priv, out);

    ret = sys_stat(filename1, &buf1);

    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
               "stat on file: %s failed "
               "(%s)",
               filename1, strerror(errno));
        goto out;
    }

    ret = sys_stat(filename2, &buf2);

    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, errno, GD_MSG_FILE_OP_FAILED,
               "stat on file: %s failed "
               "(%s)",
               filename2, strerror(errno));
        goto out;
    }

    if (buf1.st_size != buf2.st_size) {
        *identical = _gf_false;
        goto out;
    }

    ret = get_checksum_for_path(filename1, &cksum1, priv->op_version);
    if (ret)
        goto out;

    ret = get_checksum_for_path(filename2, &cksum2, priv->op_version);
    if (ret)
        goto out;

    if (cksum1 != cksum2)
        *identical = _gf_false;
    else
        *identical = _gf_true;

out:
    gf_msg_debug(this->name, 0, "Returning with %d", ret);
    return ret;
}

int
glusterd_volset_help(dict_t *dict, char **op_errstr)
{
    int ret = -1;
    gf_boolean_t xml_out = _gf_false;
#if (!HAVE_LIB_XML)
    xlator_t *this = THIS;
#endif

    if (!dict) {
        if (!(dict = glusterd_op_get_ctx())) {
            ret = 0;
            goto out;
        }
    }

    if (dict_get_sizen(dict, "help")) {
        xml_out = _gf_false;
    } else if (dict_get_sizen(dict, "help-xml")) {
        xml_out = _gf_true;
#if (HAVE_LIB_XML)
        ret = 0;
#else
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_MODULE_NOT_INSTALLED,
               "libxml not present in the system");
        if (op_errstr)
            *op_errstr = gf_strdup(
                "Error: xml libraries not "
                "present to produce "
                "xml-output");
        goto out;
#endif

    } else {
        goto out;
    }

    ret = glusterd_get_volopt_content(dict, xml_out);
    if (ret && op_errstr)
        *op_errstr = gf_strdup("Failed to get volume options help");
out:

    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

int
glusterd_to_cli(rpcsvc_request_t *req, gf_cli_rsp *arg, struct iovec *payload,
                int payloadcount, struct iobref *iobref, xdrproc_t xdrproc,
                dict_t *dict)
{
    int ret = -1;
    char *cmd = NULL;
    int op_ret = 0;
    char *op_errstr = NULL;

    op_ret = arg->op_ret;
    op_errstr = arg->op_errstr;

    ret = dict_get_str(dict, "cmd-str", &cmd);
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get command "
               "string");

    if (cmd) {
        if (op_ret)
            gf_cmd_log("", "%s : FAILED %s %s", cmd, (op_errstr) ? ":" : " ",
                       (op_errstr) ? op_errstr : " ");
        else
            gf_cmd_log("", "%s : SUCCESS", cmd);
    }

    glusterd_submit_reply(req, arg, payload, payloadcount, iobref,
                          (xdrproc_t)xdrproc);

    if (dict) {
        dict_unref(dict);
    }
    return ret;
}

int32_t
glusterd_aggr_brick_mount_dirs(dict_t *aggr, dict_t *rsp_dict)
{
    char key[64] = "";
    int keylen;
    char *brick_mount_dir = NULL;
    int32_t brick_count = -1;
    int32_t ret = -1;
    int32_t i = -1;
    xlator_t *this = THIS;

    GF_ASSERT(aggr);
    GF_ASSERT(rsp_dict);

    ret = dict_get_int32(rsp_dict, "brick_count", &brick_count);
    if (ret) {
        gf_msg_debug(this->name, 0, "No brick_count present");
        ret = 0;
        goto out;
    }

    for (i = 1; i <= brick_count; i++) {
        brick_mount_dir = NULL;
        keylen = snprintf(key, sizeof(key), "brick%d.mount_dir", i);
        ret = dict_get_strn(rsp_dict, key, keylen, &brick_mount_dir);
        if (ret) {
            /* Coz the info will come from a different node */
            gf_msg_debug(this->name, 0, "%s not present", key);
            continue;
        }

        ret = dict_set_dynstr_with_alloc(aggr, key, brick_mount_dir);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set %s", key);
            goto out;
        }
    }

    ret = 0;
out:
    gf_msg_trace(this->name, 0, "Returning %d ", ret);
    return ret;
}

int32_t
glusterd_rb_use_rsp_dict(dict_t *aggr, dict_t *rsp_dict)
{
    int32_t src_port = 0;
    int32_t dst_port = 0;
    int ret = 0;
    dict_t *ctx = NULL;

    if (aggr) {
        ctx = aggr;

    } else {
        ctx = glusterd_op_get_ctx();
        if (!ctx) {
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_OPCTX_GET_FAIL,
                   "Operation Context is not present");
            GF_ASSERT(0);
        }
    }

    if (rsp_dict) {
        ret = dict_get_int32(rsp_dict, "src-brick-port", &src_port);
        if (ret == 0) {
            gf_msg_debug("glusterd", 0, "src-brick-port=%d found", src_port);
        }

        ret = dict_get_int32(rsp_dict, "dst-brick-port", &dst_port);
        if (ret == 0) {
            gf_msg_debug("glusterd", 0, "dst-brick-port=%d found", dst_port);
        }

        ret = glusterd_aggr_brick_mount_dirs(ctx, rsp_dict);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_MOUNDIRS_AGGR_FAIL,
                   "Failed to "
                   "aggregate brick mount dirs");
            goto out;
        }
    }

    if (src_port) {
        ret = dict_set_int32_sizen(ctx, "src-brick-port", src_port);
        if (ret) {
            gf_msg_debug("glusterd", 0, "Could not set src-brick");
            goto out;
        }
    }

    if (dst_port) {
        ret = dict_set_int32_sizen(ctx, "dst-brick-port", dst_port);
        if (ret) {
            gf_msg_debug("glusterd", 0, "Could not set dst-brick");
            goto out;
        }
    }

out:
    return ret;
}

int32_t
glusterd_sync_use_rsp_dict(dict_t *aggr, dict_t *rsp_dict)
{
    int ret = 0;

    GF_ASSERT(rsp_dict);

    if (!rsp_dict) {
        gf_smsg(THIS->name, GF_LOG_ERROR, errno, GD_MSG_DICT_CREATE_FAIL, NULL);
        goto out;
    }

    ret = glusterd_import_friend_volumes(rsp_dict);
out:
    return ret;
}

static int
_profile_volume_add_friend_rsp(dict_t *this, char *key, data_t *value,
                               void *data)
{
    char new_key[264] = "";
    int new_key_len;
    glusterd_pr_brick_rsp_conv_t *rsp_ctx = NULL;
    data_t *new_value = NULL;
    int brick_count = 0;
    char brick_key[256] = "";

    if (strcmp(key, "count") == 0)
        return 0;
    sscanf(key, "%d%s", &brick_count, brick_key);
    rsp_ctx = data;
    new_value = data_copy(value);
    GF_ASSERT(new_value);
    new_key_len = snprintf(new_key, sizeof(new_key), "%d%s",
                           rsp_ctx->count + brick_count, brick_key);
    dict_setn(rsp_ctx->dict, new_key, new_key_len, new_value);
    return 0;
}

int
glusterd_profile_volume_use_rsp_dict(dict_t *aggr, dict_t *rsp_dict)
{
    int ret = 0;
    glusterd_pr_brick_rsp_conv_t rsp_ctx = {0};
    int32_t brick_count = 0;
    int32_t count = 0;
    dict_t *ctx_dict = NULL;

    GF_ASSERT(rsp_dict);

    ret = dict_get_int32(rsp_dict, "count", &brick_count);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
                "Key=count", NULL);
        ret = 0;  // no bricks in the rsp
        goto out;
    }
    if (aggr) {
        ctx_dict = aggr;

    } else {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_OPCTX_GET_FAIL,
               "Operation Context is not present");
        ret = -1;
        goto out;
    }

    ret = dict_get_int32(ctx_dict, "count", &count);
    rsp_ctx.count = count;
    rsp_ctx.dict = ctx_dict;
    dict_foreach(rsp_dict, _profile_volume_add_friend_rsp, &rsp_ctx);
    ret = dict_set_int32_sizen(ctx_dict, "count", count + brick_count);
out:
    return ret;
}

static int
glusterd_volume_status_add_peer_rsp(dict_t *this, char *key, data_t *value,
                                    void *data)
{
    glusterd_status_rsp_conv_t *rsp_ctx = NULL;
    data_t *new_value = NULL;
    char brick_key[1024] = "";
    char new_key[1024] = "";
    int32_t index = 0;
    int32_t ret = -1;
    int32_t len = 0;

    /* Skip the following keys, they are already present in the ctx_dict */
    /* Also, skip all the task related pairs. They will be added to the
     * ctx_dict later
     */
    if (!strcmp(key, "count") || !strcmp(key, "cmd") ||
        !strcmp(key, "brick-index-max") || !strcmp(key, "other-count") ||
        !strncmp(key, "task", 4))
        return 0;

    rsp_ctx = data;
    new_value = data_copy(value);
    GF_ASSERT(new_value);

    sscanf(key, "brick%d.%s", &index, brick_key);

    if (index > rsp_ctx->brick_index_max) {
        len = snprintf(new_key, sizeof(new_key), "brick%d.%s",
                       index + rsp_ctx->other_count, brick_key);
    } else {
        len = snprintf(new_key, sizeof(new_key), "%s", key);
    }
    if (len < 0 || len >= sizeof(new_key))
        goto out;

    ret = dict_setn(rsp_ctx->dict, new_key, len, new_value);
out:
    if (ret) {
        data_unref(new_value);
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Unable to set key: %s in dict", key);
    }

    return 0;
}

static int
glusterd_volume_status_copy_tasks_to_ctx_dict(dict_t *this, char *key,
                                              data_t *value, void *data)
{
    int ret = 0;
    dict_t *ctx_dict = NULL;
    data_t *new_value = NULL;

    if (strncmp(key, "task", 4))
        return 0;

    ctx_dict = data;
    GF_ASSERT(ctx_dict);

    new_value = data_copy(value);
    GF_ASSERT(new_value);

    ret = dict_set(ctx_dict, key, new_value);

    return ret;
}

static int
glusterd_volume_status_aggregate_tasks_status(dict_t *ctx_dict,
                                              dict_t *rsp_dict)
{
    int ret = -1;
    int local_count = 0;
    int remote_count = 0;
    int i = 0;
    int j = 0;
    char key[128] = "";
    int keylen;
    char *task_type = NULL;
    int local_status = 0;
    int remote_status = 0;
    char *local_task_id = NULL;
    char *remote_task_id = NULL;

    GF_ASSERT(ctx_dict);
    GF_ASSERT(rsp_dict);

    ret = dict_get_int32(rsp_dict, "tasks", &remote_count);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get remote task count");
        goto out;
    }
    /* Local count will not be present when this is called for the first
     * time with the origins rsp_dict
     */
    ret = dict_get_int32(ctx_dict, "tasks", &local_count);
    if (ret) {
        ret = dict_foreach(
            rsp_dict, glusterd_volume_status_copy_tasks_to_ctx_dict, ctx_dict);
        if (ret)
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to copy tasks"
                   "to ctx_dict.");
        goto out;
    }

    if (local_count != remote_count) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_TASKS_COUNT_MISMATCH,
               "Local tasks count (%d) and "
               "remote tasks count (%d) do not match. Not aggregating "
               "tasks status.",
               local_count, remote_count);
        ret = -1;
        goto out;
    }

    /* Update the tasks statuses. For every remote tasks, search for the
     * local task, and update the local task status based on the remote
     * status.
     */
    for (i = 0; i < remote_count; i++) {
        keylen = snprintf(key, sizeof(key), "task%d.type", i);
        ret = dict_get_strn(rsp_dict, key, keylen, &task_type);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to get task typpe from rsp dict");
            goto out;
        }

        /* Skip replace-brick status as it is going to be the same on
         * all peers. rb_status is set by the replace brick commit
         * function on all peers based on the replace brick command.
         * We return the value of rb_status as the status for a
         * replace-brick task in a 'volume status' command.
         */
        if (!strcmp(task_type, "Replace brick"))
            continue;

        keylen = snprintf(key, sizeof(key), "task%d.status", i);
        ret = dict_get_int32n(rsp_dict, key, keylen, &remote_status);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to get task status from rsp dict");
            goto out;
        }
        keylen = snprintf(key, sizeof(key), "task%d.id", i);
        ret = dict_get_strn(rsp_dict, key, keylen, &remote_task_id);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Failed to get task id from rsp dict");
            goto out;
        }
        for (j = 0; j < local_count; j++) {
            keylen = snprintf(key, sizeof(key), "task%d.id", j);
            ret = dict_get_strn(ctx_dict, key, keylen, &local_task_id);
            if (ret) {
                gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Failed to get local task-id");
                goto out;
            }

            if (strncmp(remote_task_id, local_task_id,
                        strlen(remote_task_id))) {
                /* Quit if a matching local task is not found */
                if (j == (local_count - 1)) {
                    gf_msg(THIS->name, GF_LOG_ERROR, 0,
                           GD_MSG_TASKS_COUNT_MISMATCH,
                           "Could not find matching local "
                           "task for task %s",
                           remote_task_id);
                    goto out;
                }
                continue;
            }

            keylen = snprintf(key, sizeof(key), "task%d.status", j);
            ret = dict_get_int32n(ctx_dict, key, keylen, &local_status);
            if (ret) {
                gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                       "Failed to get local task status");
                goto out;
            }

            /* Rebalance has 5 states,
             * NOT_STARTED, STARTED, STOPPED, COMPLETE, FAILED
             * The precedence used to determine the aggregate status
             * is as below,
             * STARTED > FAILED > STOPPED > COMPLETE > NOT_STARTED
             */
            /* TODO: Move this to a common place utilities that both
             * CLI and glusterd need.
             * Till then if the below algorithm is changed, change
             * it in cli_xml_output_vol_rebalance_status in
             * cli-xml-output.c
             */
            ret = 0;
            int rank[] = {[GF_DEFRAG_STATUS_STARTED] = 1,
                          [GF_DEFRAG_STATUS_FAILED] = 2,
                          [GF_DEFRAG_STATUS_STOPPED] = 3,
                          [GF_DEFRAG_STATUS_COMPLETE] = 4,
                          [GF_DEFRAG_STATUS_NOT_STARTED] = 5};
            if (rank[remote_status] <= rank[local_status])
                ret = dict_set_int32n(ctx_dict, key, keylen, remote_status);
            if (ret) {
                gf_msg(THIS->name, GF_LOG_ERROR, 0,
                       GD_MSG_TASK_STATUS_UPDATE_FAIL,
                       "Failed to "
                       "update task status");
                goto out;
            }
            break;
        }
    }

out:
    return ret;
}

gf_boolean_t
glusterd_status_has_tasks(int cmd)
{
    if (((cmd & GF_CLI_STATUS_MASK) == GF_CLI_STATUS_NONE) &&
        (cmd & GF_CLI_STATUS_VOL))
        return _gf_true;
    return _gf_false;
}

int
glusterd_volume_status_copy_to_op_ctx_dict(dict_t *aggr, dict_t *rsp_dict)
{
    int ret = 0;
    glusterd_status_rsp_conv_t rsp_ctx = {0};
    int32_t cmd = GF_CLI_STATUS_NONE;
    int32_t node_count = 0;
    int32_t other_count = 0;
    int32_t brick_index_max = -1;
    int32_t hot_brick_count = -1;
    int32_t type = -1;
    int32_t rsp_node_count = 0;
    int32_t rsp_other_count = 0;
    int vol_count = -1;
    int i = 0;
    dict_t *ctx_dict = NULL;
    char key[64] = "";
    int keylen;
    char *volname = NULL;
    glusterd_volinfo_t *volinfo = NULL;

    GF_ASSERT(rsp_dict);

    if (aggr) {
        ctx_dict = aggr;

    } else {
        ctx_dict = glusterd_op_get_ctx();
    }

    ret = dict_get_int32(ctx_dict, "cmd", &cmd);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED, "Key=cmd",
                NULL);
        goto out;
    }

    if (cmd & GF_CLI_STATUS_ALL && is_origin_glusterd(ctx_dict)) {
        ret = dict_get_int32(rsp_dict, "vol_count", &vol_count);
        if (ret == 0) {
            ret = dict_set_int32_sizen(ctx_dict, "vol_count", vol_count);
            if (ret) {
                gf_smsg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                        "Key=vol_count", NULL);
                goto out;
            }

            for (i = 0; i < vol_count; i++) {
                keylen = snprintf(key, sizeof(key), "vol%d", i);
                ret = dict_get_strn(rsp_dict, key, keylen, &volname);
                if (ret) {
                    gf_smsg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                            "Key=%s", key, NULL);
                    goto out;
                }

                ret = dict_set_strn(ctx_dict, key, keylen, volname);
                if (ret) {
                    gf_smsg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                            "Key=%s", key, NULL);
                    goto out;
                }
            }
        } else {
            /* Ignore the error as still the aggregation applies in
             * case its a task sub command */
            ret = 0;
        }
    }

    if ((cmd & GF_CLI_STATUS_TASKS) != 0)
        goto aggregate_tasks;

    ret = dict_get_int32(rsp_dict, "count", &rsp_node_count);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED, "Key=count",
                NULL);
        ret = 0;  // no bricks in the rsp
        goto out;
    }

    ret = dict_get_int32(rsp_dict, "other-count", &rsp_other_count);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                "Key=other-count", NULL);
        goto out;
    }

    ret = dict_get_int32(ctx_dict, "count", &node_count);
    ret = dict_get_int32(ctx_dict, "other-count", &other_count);
    if (!dict_get_sizen(ctx_dict, "brick-index-max")) {
        ret = dict_get_int32(rsp_dict, "brick-index-max", &brick_index_max);
        if (ret) {
            gf_smsg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                    "Key=brick-index-max", NULL);
            goto out;
        }
        ret = dict_set_int32_sizen(ctx_dict, "brick-index-max",
                                   brick_index_max);
        if (ret) {
            gf_smsg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                    "Key=brick-index-max", NULL);
            goto out;
        }

    } else {
        ret = dict_get_int32(ctx_dict, "brick-index-max", &brick_index_max);
        if (ret) {
            gf_smsg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                    "Key=brick-index-max", NULL);
            goto out;
        }
    }

    rsp_ctx.count = node_count;
    rsp_ctx.brick_index_max = brick_index_max;
    rsp_ctx.other_count = other_count;
    rsp_ctx.dict = ctx_dict;

    dict_foreach(rsp_dict, glusterd_volume_status_add_peer_rsp, &rsp_ctx);

    ret = dict_set_int32_sizen(ctx_dict, "count", node_count + rsp_node_count);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                "Key=count", NULL);
        goto out;
    }

    ret = dict_set_int32_sizen(ctx_dict, "other-count",
                               other_count + rsp_other_count);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                "Key=other-count", NULL);
        goto out;
    }

    ret = dict_get_str(ctx_dict, "volname", &volname);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                "Key=volname", NULL);
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_VOLINFO_GET_FAIL,
                "Volume=%s", volname, NULL);
        goto out;
    }

    ret = dict_set_int32_sizen(ctx_dict, "hot_brick_count", hot_brick_count);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "Key=hot_brick_count", NULL);
        goto out;
    }

    ret = dict_set_int32_sizen(ctx_dict, "type", type);
    if (ret) {
        gf_smsg(THIS->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                "Key=type", NULL);
        goto out;
    }

aggregate_tasks:
    /* Tasks are only present for a normal status command for a volume or
     * for an explicit tasks status command for a volume
     */
    if (!(cmd & GF_CLI_STATUS_ALL) &&
        (((cmd & GF_CLI_STATUS_TASKS) != 0) || glusterd_status_has_tasks(cmd)))
        ret = glusterd_volume_status_aggregate_tasks_status(ctx_dict, rsp_dict);

out:
    return ret;
}

static int
glusterd_bitrot_volume_node_rsp(dict_t *aggr, dict_t *rsp_dict)
{
    int ret = -1;
    uint64_t value = 0;
    char key[64] = "";
    int keylen;
    char buf[1024] = "";
    int32_t i = 0;
    int32_t j = 0;
    char *last_scrub_time = NULL;
    char *scrub_time = NULL;
    char *volname = NULL;
    char *scrub_freq = NULL;
    char *scrub_state = NULL;
    char *scrub_impact = NULL;
    char *bad_gfid_str = NULL;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    int8_t scrub_running = 0;

    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_set_str_sizen(aggr, "bitrot_log_file",
                             priv->bitd_svc.proc.logfile);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set bitrot log file location");
        goto out;
    }

    ret = dict_set_str_sizen(aggr, "scrub_log_file",
                             priv->scrub_svc.proc.logfile);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set scrubber log file location");
        goto out;
    }

    ret = dict_get_str(aggr, "volname", &volname);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume name");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
               "Unable to find volinfo for volume: %s", volname);
        goto out;
    }

    ret = dict_get_int32(aggr, "count", &i);
    i++;

    ret = dict_set_int32_sizen(aggr, "count", i);
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set count");

    snprintf(buf, sizeof(buf), "%s", uuid_utoa(MY_UUID));

    snprintf(key, sizeof(key), "node-uuid-%d", i);
    ret = dict_set_dynstr_with_alloc(aggr, key, buf);
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "failed to set node-uuid");

    ret = dict_get_str(volinfo->dict, "features.scrub-freq", &scrub_freq);
    if (!ret) {
        ret = dict_set_str_sizen(aggr, "features.scrub-freq", scrub_freq);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "scrub-frequency value to dictionary");
        }
    } else {
        /* By Default scrub-frequency is bi-weekly. So when user
         * enable bitrot then scrub-frequency value will not be
         * present in volinfo->dict. Setting by-default value of
         * scrub-frequency explicitly for presenting it to scrub
         * status.
         */
        ret = dict_set_dynstr_with_alloc(aggr, "features.scrub-freq",
                                         "biweekly");
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "scrub-frequency value to dictionary");
        }
    }

    ret = dict_get_str(volinfo->dict, "features.scrub-throttle", &scrub_impact);
    if (!ret) {
        ret = dict_set_str_sizen(aggr, "features.scrub-throttle", scrub_impact);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "scrub-throttle value to dictionary");
        }
    } else {
        /* By Default scrub-throttle is lazy. So when user
         * enable bitrot then scrub-throttle value will not be
         * present in volinfo->dict. Setting by-default value of
         * scrub-throttle explicitly for presenting it to
         * scrub status.
         */
        ret = dict_set_dynstr_with_alloc(aggr, "features.scrub-throttle",
                                         "lazy");
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "scrub-throttle value to dictionary");
        }
    }

    ret = dict_get_str(volinfo->dict, "features.scrub", &scrub_state);
    if (!ret) {
        ret = dict_set_str_sizen(aggr, "features.scrub", scrub_state);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "scrub state value to dictionary");
        }
    }

    ret = dict_get_int8(rsp_dict, "scrub-running", &scrub_running);
    if (!ret) {
        snprintf(key, sizeof(key), "scrub-running-%d", i);
        ret = dict_set_uint64(aggr, key, scrub_running);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "scrub-running value");
        }
    }

    ret = dict_get_uint64(rsp_dict, "scrubbed-files", &value);
    if (!ret) {
        snprintf(key, sizeof(key), "scrubbed-files-%d", i);
        ret = dict_set_uint64(aggr, key, value);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "scrubbed-file value");
        }
    }

    ret = dict_get_uint64(rsp_dict, "unsigned-files", &value);
    if (!ret) {
        snprintf(key, sizeof(key), "unsigned-files-%d", i);
        ret = dict_set_uint64(aggr, key, value);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "unsigned-file value");
        }
    }

    ret = dict_get_str(rsp_dict, "last-scrub-time", &last_scrub_time);
    if (!ret) {
        keylen = snprintf(key, sizeof(key), "last-scrub-time-%d", i);

        scrub_time = gf_strdup(last_scrub_time);
        ret = dict_set_dynstrn(aggr, key, keylen, scrub_time);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "last scrub time value");
        }
    }

    ret = dict_get_uint64(rsp_dict, "scrub-duration", &value);
    if (!ret) {
        snprintf(key, sizeof(key), "scrub-duration-%d", i);
        ret = dict_set_uint64(aggr, key, value);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set "
                         "scrubbed-duration value");
        }
    }

    ret = dict_get_uint64(rsp_dict, "total-count", &value);
    if (!ret) {
        snprintf(key, sizeof(key), "error-count-%d", i);
        ret = dict_set_uint64(aggr, key, value);
        if (ret) {
            gf_msg_debug(this->name, 0,
                         "Failed to set error "
                         "count value");
        }

        /* Storing all the bad files in the dictionary */
        for (j = 0; j < value; j++) {
            keylen = snprintf(key, sizeof(key), "quarantine-%d", j);
            ret = dict_get_strn(rsp_dict, key, keylen, &bad_gfid_str);
            if (!ret) {
                snprintf(key, sizeof(key), "quarantine-%d-%d", j, i);
                ret = dict_set_dynstr_with_alloc(aggr, key, bad_gfid_str);
                if (ret) {
                    gf_msg_debug(this->name, 0,
                                 "Failed to"
                                 "bad file gfid ");
                }
            }
        }
    }

    ret = 0;
out:
    return ret;
}

int
glusterd_volume_rebalance_use_rsp_dict(dict_t *aggr, dict_t *rsp_dict)
{
    char key[64] = "";
    int keylen;
    char *node_uuid = NULL;
    char *node_uuid_str = NULL;
    char *volname = NULL;
    dict_t *ctx_dict = NULL;
    double elapsed_time = 0;
    glusterd_conf_t *conf = NULL;
    glusterd_peerinfo_t *peerinfo = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    int ret = 0;
    int32_t index = 0;
    int32_t count = 0;
    int32_t current_index = 1;
    int32_t value32 = 0;
    uint64_t value = 0;
    char *peer_uuid_str = NULL;
    xlator_t *this = THIS;

    GF_ASSERT(rsp_dict);
    conf = this->private;

    if (conf->op_version < GD_OP_VERSION_6_0)
        current_index = 2;
    if (aggr) {
        ctx_dict = aggr;

    } else {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_OPCTX_GET_FAIL,
               "Operation Context is not present");
        goto out;
    }

    ret = dict_get_str(ctx_dict, "volname", &volname);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume name");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);

    if (ret)
        goto out;

    ret = dict_get_int32(rsp_dict, "count", &index);
    if (ret)
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "failed to get index from rsp dict");

    keylen = snprintf(key, sizeof(key), "node-uuid-%d", index);
    ret = dict_get_strn(rsp_dict, key, keylen, &node_uuid);
    if (!ret) {
        node_uuid_str = gf_strdup(node_uuid);

        /* Finding the index of the node-uuid in the peer-list */
        RCU_READ_LOCK;
        cds_list_for_each_entry_rcu(peerinfo, &conf->peers, uuid_list)
        {
            peer_uuid_str = gd_peer_uuid_str(peerinfo);
            if (strcmp(peer_uuid_str, node_uuid_str) == 0)
                break;

            current_index++;
        }
        RCU_READ_UNLOCK;

        /* Setting the largest index value as the total count. */
        ret = dict_get_int32(ctx_dict, "count", &count);
        if (count < current_index) {
            ret = dict_set_int32_sizen(ctx_dict, "count", current_index);
            if (ret)
                gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                       "Failed to set count");
        }

        /* Setting the same index for the node, as is in the peerlist.*/
        keylen = snprintf(key, sizeof(key), "node-uuid-%d", current_index);
        ret = dict_set_dynstrn(ctx_dict, key, keylen, node_uuid_str);
        if (ret) {
            gf_msg_debug(this->name, 0, "failed to set node-uuid");
        }
    }

    snprintf(key, sizeof(key), "files-%d", index);
    ret = dict_get_uint64(rsp_dict, key, &value);
    if (!ret) {
        snprintf(key, sizeof(key), "files-%d", current_index);
        ret = dict_set_uint64(ctx_dict, key, value);
        if (ret) {
            gf_msg_debug(this->name, 0, "failed to set the file count");
        }
    }

    snprintf(key, sizeof(key), "size-%d", index);
    ret = dict_get_uint64(rsp_dict, key, &value);
    if (!ret) {
        snprintf(key, sizeof(key), "size-%d", current_index);
        ret = dict_set_uint64(ctx_dict, key, value);
        if (ret) {
            gf_msg_debug(this->name, 0, "failed to set the size of migration");
        }
    }

    snprintf(key, sizeof(key), "lookups-%d", index);
    ret = dict_get_uint64(rsp_dict, key, &value);
    if (!ret) {
        snprintf(key, sizeof(key), "lookups-%d", current_index);
        ret = dict_set_uint64(ctx_dict, key, value);
        if (ret) {
            gf_msg_debug(this->name, 0, "failed to set looked up file count");
        }
    }

    keylen = snprintf(key, sizeof(key), "status-%d", index);
    ret = dict_get_int32n(rsp_dict, key, keylen, &value32);
    if (!ret) {
        keylen = snprintf(key, sizeof(key), "status-%d", current_index);
        ret = dict_set_int32n(ctx_dict, key, keylen, value32);
        if (ret) {
            gf_msg_debug(this->name, 0, "failed to set status");
        }
    }

    snprintf(key, sizeof(key), "failures-%d", index);
    ret = dict_get_uint64(rsp_dict, key, &value);
    if (!ret) {
        snprintf(key, sizeof(key), "failures-%d", current_index);
        ret = dict_set_uint64(ctx_dict, key, value);
        if (ret) {
            gf_msg_debug(this->name, 0, "failed to set failure count");
        }
    }

    snprintf(key, sizeof(key), "skipped-%d", index);
    ret = dict_get_uint64(rsp_dict, key, &value);
    if (!ret) {
        snprintf(key, sizeof(key), "skipped-%d", current_index);
        ret = dict_set_uint64(ctx_dict, key, value);
        if (ret) {
            gf_msg_debug(this->name, 0, "failed to set skipped count");
        }
    }
    snprintf(key, sizeof(key), "run-time-%d", index);
    ret = dict_get_double(rsp_dict, key, &elapsed_time);
    if (!ret) {
        snprintf(key, sizeof(key), "run-time-%d", current_index);
        ret = dict_set_double(ctx_dict, key, elapsed_time);
        if (ret) {
            gf_msg_debug(this->name, 0, "failed to set run-time");
        }
    }

    snprintf(key, sizeof(key), "time-left-%d", index);
    ret = dict_get_uint64(rsp_dict, key, &value);
    if (!ret) {
        snprintf(key, sizeof(key), "time-left-%d", current_index);
        ret = dict_set_uint64(ctx_dict, key, value);
        if (ret) {
            gf_msg_debug(this->name, 0, "failed to set time-left");
        }
    }
    snprintf(key, sizeof(key), "demoted-%d", index);
    ret = dict_get_uint64(rsp_dict, key, &value);
    if (!ret) {
        snprintf(key, sizeof(key), "demoted-%d", current_index);
        ret = dict_set_uint64(ctx_dict, key, value);
        if (ret) {
            gf_msg_debug(this->name, 0, "failed to set demoted count");
        }
    }
    snprintf(key, sizeof(key), "promoted-%d", index);
    ret = dict_get_uint64(rsp_dict, key, &value);
    if (!ret) {
        snprintf(key, sizeof(key), "promoted-%d", current_index);
        ret = dict_set_uint64(ctx_dict, key, value);
        if (ret) {
            gf_msg_debug(this->name, 0, "failed to set promoted count");
        }
    }

    ret = 0;

out:
    return ret;
}

static int
_profile_volume_add_brick_rsp(dict_t *this, char *key, data_t *value,
                              void *data)
{
    char new_key[256] = "";
    int keylen;
    glusterd_pr_brick_rsp_conv_t *rsp_ctx = NULL;
    data_t *new_value = NULL;

    rsp_ctx = data;
    new_value = data_copy(value);
    GF_ASSERT(new_value);
    keylen = snprintf(new_key, sizeof(new_key), "%d-%s", rsp_ctx->count, key);
    dict_setn(rsp_ctx->dict, new_key, keylen, new_value);
    return 0;
}

// input-key: <replica-id>:<child-id>-*
// output-key: <brick-id>-*
int
_heal_volume_add_shd_rsp(dict_t *this, char *key, data_t *value, void *data)
{
    char new_key[256] = "";
    char int_str[16] = "";
    data_t *new_value = NULL;
    char *rxl_end = NULL;
    int rxl_end_len;
    char *rxl_child_end = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    int rxl_id = 0;
    int rxl_child_id = 0;
    int brick_id = 0;
    int int_len = 0;
    int ret = 0;
    glusterd_heal_rsp_conv_t *rsp_ctx = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;

    rsp_ctx = data;
    rxl_end = strchr(key, '-');
    if (!rxl_end)
        goto out;

    rxl_child_end = strchr(rxl_end + 1, '-');
    if (!rxl_child_end)
        goto out;

    rxl_end_len = strlen(rxl_end);
    int_len = strlen(key) - rxl_end_len;
    (void)memcpy(int_str, key, int_len);
    int_str[int_len] = '\0';

    ret = gf_string2int(int_str, &rxl_id);
    if (ret)
        goto out;

    int_len = rxl_end_len - strlen(rxl_child_end) - 1;
    (void)memcpy(int_str, rxl_end + 1, int_len);
    int_str[int_len] = '\0';

    ret = gf_string2int(int_str, &rxl_child_id);
    if (ret)
        goto out;

    volinfo = rsp_ctx->volinfo;
    brick_id = rxl_id * volinfo->replica_count + rxl_child_id;

    if (!strcmp(rxl_child_end, "-status")) {
        brickinfo = glusterd_get_brickinfo_by_position(volinfo, brick_id);
        if (!brickinfo)
            goto out;
        if (!glusterd_is_local_brick(volinfo, brickinfo))
            goto out;
    }
    new_value = data_copy(value);
    int_len = snprintf(new_key, sizeof(new_key), "%d%s", brick_id,
                       rxl_child_end);
    dict_setn(rsp_ctx->dict, new_key, int_len, new_value);

out:
    return 0;
}

int
_heal_volume_add_shd_rsp_of_statistics(dict_t *this, char *key, data_t *value,
                                       void *data)
{
    char new_key[256] = "";
    char int_str[16] = "";
    char key_begin_string[128] = "";
    data_t *new_value = NULL;
    char *rxl_end = NULL;
    int rxl_end_len;
    char *rxl_child_end = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    char *key_begin_str = NULL;
    int key_begin_strlen;
    int rxl_id = 0;
    int rxl_child_id = 0;
    int brick_id = 0;
    int int_len = 0;
    int ret = 0;
    glusterd_heal_rsp_conv_t *rsp_ctx = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;

    rsp_ctx = data;
    key_begin_str = strchr(key, '-');
    if (!key_begin_str)
        goto out;

    rxl_end = strchr(key_begin_str + 1, '-');
    if (!rxl_end)
        goto out;

    rxl_child_end = strchr(rxl_end + 1, '-');
    if (!rxl_child_end)
        goto out;

    key_begin_strlen = strlen(key_begin_str);
    int_len = strlen(key) - key_begin_strlen;

    (void)memcpy(key_begin_string, key, int_len);
    key_begin_string[int_len] = '\0';

    rxl_end_len = strlen(rxl_end);
    int_len = key_begin_strlen - rxl_end_len - 1;
    (void)memcpy(int_str, key_begin_str + 1, int_len);
    int_str[int_len] = '\0';
    ret = gf_string2int(int_str, &rxl_id);
    if (ret)
        goto out;

    int_len = rxl_end_len - strlen(rxl_child_end) - 1;
    (void)memcpy(int_str, rxl_end + 1, int_len);
    int_str[int_len] = '\0';
    ret = gf_string2int(int_str, &rxl_child_id);
    if (ret)
        goto out;

    volinfo = rsp_ctx->volinfo;
    brick_id = rxl_id * volinfo->replica_count + rxl_child_id;

    brickinfo = glusterd_get_brickinfo_by_position(volinfo, brick_id);
    if (!brickinfo)
        goto out;
    if (!glusterd_is_local_brick(volinfo, brickinfo))
        goto out;

    new_value = data_copy(value);
    int_len = snprintf(new_key, sizeof(new_key), "%s-%d%s", key_begin_string,
                       brick_id, rxl_child_end);
    dict_setn(rsp_ctx->dict, new_key, int_len, new_value);

out:
    return 0;
}

int
glusterd_heal_volume_brick_rsp(dict_t *req_dict, dict_t *rsp_dict,
                               dict_t *op_ctx, char **op_errstr)
{
    int ret = 0;
    glusterd_heal_rsp_conv_t rsp_ctx = {0};
    char *volname = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    int heal_op = -1;

    GF_ASSERT(rsp_dict);
    GF_ASSERT(op_ctx);
    GF_ASSERT(op_errstr);

    ret = dict_get_str(req_dict, "volname", &volname);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume name");
        goto out;
    }

    ret = dict_get_int32(req_dict, "heal-op", &heal_op);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get heal_op");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);

    if (ret)
        goto out;

    rsp_ctx.dict = op_ctx;
    rsp_ctx.volinfo = volinfo;
    rsp_ctx.this = THIS;
    if (heal_op == GF_SHD_OP_STATISTICS)
        dict_foreach(rsp_dict, _heal_volume_add_shd_rsp_of_statistics,
                     &rsp_ctx);
    else
        dict_foreach(rsp_dict, _heal_volume_add_shd_rsp, &rsp_ctx);

out:
    return ret;
}

int
_status_volume_add_brick_rsp(dict_t *this, char *key, data_t *value, void *data)
{
    char new_key[256] = "";
    int keylen;
    data_t *new_value = 0;
    glusterd_pr_brick_rsp_conv_t *rsp_ctx = NULL;

    rsp_ctx = data;
    new_value = data_copy(value);
    keylen = snprintf(new_key, sizeof(new_key), "brick%d.%s", rsp_ctx->count,
                      key);
    dict_setn(rsp_ctx->dict, new_key, keylen, new_value);

    return 0;
}

int
glusterd_status_volume_brick_rsp(dict_t *rsp_dict, dict_t *op_ctx,
                                 char **op_errstr)
{
    int ret = 0;
    glusterd_pr_brick_rsp_conv_t rsp_ctx = {0};
    int32_t count = 0;
    int index = 0;

    GF_ASSERT(rsp_dict);
    GF_ASSERT(op_ctx);
    GF_ASSERT(op_errstr);

    ret = dict_get_int32(op_ctx, "count", &count);
    if (ret) {
        count = 0;
    } else {
        count++;
    }
    ret = dict_get_int32(rsp_dict, "index", &index);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Couldn't get node index");
        goto out;
    }
    dict_del_sizen(rsp_dict, "index");

    rsp_ctx.count = index;
    rsp_ctx.dict = op_ctx;
    dict_foreach(rsp_dict, _status_volume_add_brick_rsp, &rsp_ctx);
    ret = dict_set_int32_sizen(op_ctx, "count", count);

out:
    return ret;
}

int
glusterd_status_volume_client_list(dict_t *rsp_dict, dict_t *op_ctx,
                                   char **op_errstr)
{
    int ret = 0;
    char *process = 0;
    int32_t count = 0;
    int32_t fuse_count = 0;
    int32_t gfapi_count = 0;
    int32_t rebalance_count = 0;
    int32_t glustershd_count = 0;
    int32_t quotad_count = 0;
    int32_t snapd_count = 0;
    int32_t client_count = 0;
    int i = 0;
    char key[64] = "";

    GF_ASSERT(rsp_dict);
    GF_ASSERT(op_ctx);
    GF_ASSERT(op_errstr);

    ret = dict_get_int32(rsp_dict, "clientcount", &client_count);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
               "Couldn't get node index");
    }
    ret = dict_set_int32_sizen(op_ctx, "client-count", client_count);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Couldn't get node index");
        goto out;
    }
    for (i = 0; i < client_count; i++) {
        count = 0;
        ret = snprintf(key, sizeof(key), "client%d.name", i);
        ret = dict_get_strn(rsp_dict, key, ret, &process);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
                   "Couldn't get client name");
            goto out;
        }
        ret = dict_add_dynstr_with_alloc(op_ctx, key, process);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_INFO, 0, GD_MSG_DICT_SET_FAILED,
                   "Couldn't set client name");
        }
        if (!strncmp(process, "fuse", 4)) {
            ret = dict_get_int32(op_ctx, "fuse-count", &count);
            if (ret) {
                gf_msg(THIS->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
                       "Couldn't get fuse-count");
            }
            fuse_count++;
            continue;
        } else if (!strncmp(process, "gfapi", 5)) {
            ret = dict_get_int32(op_ctx, "gfapi-count", &count);
            if (ret) {
                gf_msg(THIS->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
                       "Couldn't get gfapi-count");
            }
            gfapi_count++;
            continue;

        } else if (!strcmp(process, "rebalance")) {
            ret = dict_get_int32(op_ctx, "rebalance-count", &count);
            if (ret) {
                gf_msg(THIS->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
                       "Couldn't get rebalance-count");
            }
            rebalance_count++;
            continue;
        } else if (!strcmp(process, "glustershd")) {
            ret = dict_get_int32(op_ctx, "glustershd-count", &count);
            if (ret) {
                gf_msg(THIS->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
                       "Couldn't get glustershd-count");
            }
            glustershd_count++;
            continue;
        } else if (!strcmp(process, "quotad")) {
            ret = dict_get_int32(op_ctx, "quotad-count", &count);
            if (ret) {
                gf_msg(THIS->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
                       "Couldn't get quotad-count");
            }
            quotad_count++;
            continue;
        } else if (!strcmp(process, "snapd")) {
            ret = dict_get_int32(op_ctx, "snapd-count", &count);
            if (ret) {
                gf_msg(THIS->name, GF_LOG_INFO, 0, GD_MSG_DICT_GET_FAILED,
                       "Couldn't get snapd-count");
            }
            snapd_count++;
        }
    }

    if (fuse_count) {
        ret = dict_set_int32_sizen(op_ctx, "fuse-count", fuse_count);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Couldn't set fuse-count");
            goto out;
        }
    }
    if (gfapi_count) {
        ret = dict_set_int32_sizen(op_ctx, "gfapi-count", gfapi_count);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Couldn't set gfapi-count");
            goto out;
        }
    }
    if (rebalance_count) {
        ret = dict_set_int32_sizen(op_ctx, "rebalance-count", rebalance_count);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Couldn't set rebalance-count");
            goto out;
        }
    }
    if (glustershd_count) {
        ret = dict_set_int32_sizen(op_ctx, "glustershd-count",
                                   glustershd_count);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Couldn't set glustershd-count");
            goto out;
        }
    }
    if (quotad_count) {
        ret = dict_set_int32_sizen(op_ctx, "quotad-count", quotad_count);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Couldn't set quotad-count");
            goto out;
        }
    }
    if (snapd_count) {
        ret = dict_set_int32_sizen(op_ctx, "snapd-count", snapd_count);
        if (ret) {
            gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Couldn't set snapd-count");
            goto out;
        }
    }

out:
    return ret;
}

int
glusterd_rebalance_rsp(dict_t *op_ctx, glusterd_rebalance_t *index, int32_t i)
{
    int ret = 0;
    char key[64] = "";
    int keylen;
    xlator_t *this = THIS;

    snprintf(key, sizeof(key), "files-%d", i);
    ret = dict_set_uint64(op_ctx, key, index->rebalance_files);
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "failed to set file count");

    snprintf(key, sizeof(key), "size-%d", i);
    ret = dict_set_uint64(op_ctx, key, index->rebalance_data);
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "failed to set size of xfer");

    snprintf(key, sizeof(key), "lookups-%d", i);
    ret = dict_set_uint64(op_ctx, key, index->lookedup_files);
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "failed to set lookedup file count");

    keylen = snprintf(key, sizeof(key), "status-%d", i);
    ret = dict_set_int32n(op_ctx, key, keylen, index->defrag_status);
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "failed to set status");

    snprintf(key, sizeof(key), "failures-%d", i);
    ret = dict_set_uint64(op_ctx, key, index->rebalance_failures);
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "failed to set failure count");

    snprintf(key, sizeof(key), "skipped-%d", i);
    ret = dict_set_uint64(op_ctx, key, index->skipped_files);
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "failed to set skipped count");

    snprintf(key, sizeof(key), "run-time-%d", i);
    ret = dict_set_double(op_ctx, key, index->rebalance_time);
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "failed to set run-time");

    return ret;
}

int
glusterd_defrag_volume_node_rsp(dict_t *req_dict, dict_t *rsp_dict,
                                dict_t *op_ctx)
{
    int ret = 0;
    char *volname = NULL;
    glusterd_volinfo_t *volinfo = NULL;
    char key[64] = "";
    int keylen;
    int32_t i = 0;
    char buf[64] = "";
    char *node_str = NULL;
    int32_t cmd = 0;

    GF_ASSERT(req_dict);

    ret = dict_get_str(req_dict, "volname", &volname);
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume name");
        goto out;
    }

    ret = glusterd_volinfo_find(volname, &volinfo);

    ret = dict_get_int32(req_dict, "rebalance-command", &cmd);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_GET_FAILED,
               "Unable to get the cmd");
        goto out;
    }

    if (rsp_dict) {
        ret = glusterd_defrag_volume_status_update(volinfo, rsp_dict, cmd);
    }

    if (!op_ctx) {
        dict_copy(rsp_dict, op_ctx);
        goto out;
    }

    ret = dict_get_int32(op_ctx, "count", &i);
    i++;

    ret = dict_set_int32_sizen(op_ctx, "count", i);
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set count");

    snprintf(buf, sizeof(buf), "%s", uuid_utoa(MY_UUID));
    node_str = gf_strdup(buf);

    keylen = snprintf(key, sizeof(key), "node-uuid-%d", i);
    ret = dict_set_dynstrn(op_ctx, key, keylen, node_str);
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "failed to set node-uuid");

    glusterd_rebalance_rsp(op_ctx, &volinfo->rebal, i);

    snprintf(key, sizeof(key), "time-left-%d", i);
    ret = dict_set_time(op_ctx, key, volinfo->rebal.time_left);
    if (ret)
        gf_msg(THIS->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
               "failed to set time left");

out:
    return ret;
}

static int
glusterd_profile_volume_brick_rsp(void *pending_entry, dict_t *rsp_dict,
                                  dict_t *op_ctx, char **op_errstr,
                                  gd_node_type type)
{
    int ret = 0;
    glusterd_pr_brick_rsp_conv_t rsp_ctx = {0};
    int32_t count = 0;
    char brick[PATH_MAX + 1024] = "";
    char key[64] = "";
    int keylen;
    char *full_brick = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;

    GF_ASSERT(rsp_dict);
    GF_ASSERT(op_ctx);
    GF_ASSERT(op_errstr);
    GF_ASSERT(pending_entry);

    ret = dict_get_int32(op_ctx, "count", &count);
    if (ret) {
        count = 1;
    } else {
        count++;
    }
    if (type == GD_NODE_BRICK) {
        brickinfo = pending_entry;
        snprintf(brick, sizeof(brick), "%s:%s", brickinfo->hostname,
                 brickinfo->path);
    } else if (type == GD_NODE_NFS) {
        snprintf(brick, sizeof(brick), "%s", uuid_utoa(MY_UUID));
    }
    full_brick = gf_strdup(brick);
    GF_ASSERT(full_brick);
    keylen = snprintf(key, sizeof(key), "%d-brick", count);
    ret = dict_set_dynstrn(op_ctx, key, keylen, full_brick);

    rsp_ctx.count = count;
    rsp_ctx.dict = op_ctx;
    dict_foreach(rsp_dict, _profile_volume_add_brick_rsp, &rsp_ctx);
    ret = dict_set_int32_sizen(op_ctx, "count", count);
    return ret;
}

int32_t
glusterd_handle_node_rsp(dict_t *req_dict, void *pending_entry,
                         glusterd_op_t op, dict_t *rsp_dict, dict_t *op_ctx,
                         char **op_errstr, gd_node_type type)
{
    int ret = 0;
    int32_t cmd = GF_OP_CMD_NONE;

    GF_ASSERT(op_errstr);

    switch (op) {
        case GD_OP_PROFILE_VOLUME:
            ret = glusterd_profile_volume_brick_rsp(pending_entry, rsp_dict,
                                                    op_ctx, op_errstr, type);
            break;
        case GD_OP_STATUS_VOLUME:
            ret = dict_get_int32(req_dict, "cmd", &cmd);
            if (!ret && (cmd & GF_CLI_STATUS_CLIENT_LIST)) {
                ret = glusterd_status_volume_client_list(rsp_dict, op_ctx,
                                                         op_errstr);
            } else
                ret = glusterd_status_volume_brick_rsp(rsp_dict, op_ctx,
                                                       op_errstr);
            break;
        case GD_OP_DEFRAG_BRICK_VOLUME:
            glusterd_defrag_volume_node_rsp(req_dict, rsp_dict, op_ctx);
            break;

        case GD_OP_HEAL_VOLUME:
            ret = glusterd_heal_volume_brick_rsp(req_dict, rsp_dict, op_ctx,
                                                 op_errstr);
            break;
        case GD_OP_SCRUB_STATUS:
            ret = glusterd_bitrot_volume_node_rsp(op_ctx, rsp_dict);

            break;
        default:
            break;
    }

    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

int32_t
glusterd_set_originator_uuid(dict_t *dict)
{
    int ret = -1;
    uuid_t *originator_uuid = NULL;

    GF_ASSERT(dict);

    originator_uuid = GF_MALLOC(sizeof(uuid_t), gf_common_mt_uuid_t);
    if (!originator_uuid) {
        ret = -1;
        goto out;
    }

    gf_uuid_copy(*originator_uuid, MY_UUID);
    ret = dict_set_bin(dict, "originator_uuid", originator_uuid,
                       sizeof(uuid_t));
    if (ret) {
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set originator_uuid.");
        goto out;
    }

out:
    if (ret && originator_uuid)
        GF_FREE(originator_uuid);

    return ret;
}

/* Should be used only when an operation is in progress, as that is the only
 * time a lock_owner is set
 */
gf_boolean_t
is_origin_glusterd(dict_t *dict)
{
    gf_boolean_t ret = _gf_false;
    uuid_t lock_owner = {
        0,
    };
    uuid_t *originator_uuid = NULL;

    GF_ASSERT(dict);

    ret = dict_get_bin(dict, "originator_uuid", (void **)&originator_uuid);
    if (ret) {
        /* If not originator_uuid has been set, then the command
         * has been originated from a glusterd running on older version
         * Hence fetching the lock owner */
        ret = glusterd_get_lock_owner(&lock_owner);
        if (ret) {
            ret = _gf_false;
            goto out;
        }
        ret = !gf_uuid_compare(MY_UUID, lock_owner);
    } else
        ret = !gf_uuid_compare(MY_UUID, *originator_uuid);

out:
    return ret;
}

int
glusterd_generate_and_set_task_id(dict_t *dict, char *key, const int keylen)
{
    int ret = -1;
    uuid_t task_id = {
        0,
    };
    char *uuid_str = NULL;
    xlator_t *this = THIS;

    GF_ASSERT(dict);

    gf_uuid_generate(task_id);
    uuid_str = gf_strdup(uuid_utoa(task_id));
    if (!uuid_str) {
        ret = -1;
        goto out;
    }

    ret = dict_set_dynstrn(dict, key, keylen, uuid_str);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set %s in dict", key);
        goto out;
    }
    gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_TASK_ID_INFO,
           "Generated task-id %s for key %s", uuid_str, key);

out:
    if (ret)
        GF_FREE(uuid_str);
    return ret;
}

int
glusterd_copy_uuid_to_dict(uuid_t uuid, dict_t *dict, char *key,
                           const int keylen)
{
    int ret = -1;
    char tmp_str[40] = "";
    char *task_id_str = NULL;

    GF_ASSERT(dict);
    GF_ASSERT(key);

    gf_uuid_unparse(uuid, tmp_str);
    task_id_str = gf_strdup(tmp_str);
    if (!task_id_str)
        return -1;

    ret = dict_set_dynstrn(dict, key, keylen, task_id_str);
    if (ret) {
        GF_FREE(task_id_str);
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Error setting uuid in dict with key %s", key);
    }

    return 0;
}

static int
_update_volume_op_versions(dict_t *this, char *key, data_t *value, void *data)
{
    int op_version = 0;
    glusterd_volinfo_t *ctx = NULL;
    gf_boolean_t enabled = _gf_true;
    int ret = -1;
    struct volopt_map_entry *vmep = NULL;

    GF_ASSERT(data);
    ctx = data;

    vmep = gd_get_vmep(key);
    op_version = glusterd_get_op_version_from_vmep(vmep);

    if (gd_is_xlator_option(vmep) || gd_is_boolean_option(vmep)) {
        ret = gf_string2boolean(value->data, &enabled);
        if (ret)
            return 0;

        if (!enabled)
            return 0;
    }

    if (op_version > ctx->op_version)
        ctx->op_version = op_version;

    if (gd_is_client_option(vmep) && (op_version > ctx->client_op_version))
        ctx->client_op_version = op_version;

    return 0;
}

void
gd_update_volume_op_versions(glusterd_volinfo_t *volinfo)
{
    glusterd_conf_t *conf = NULL;
    gf_boolean_t ob_enabled = _gf_false;

    GF_ASSERT(volinfo);

    conf = THIS->private;
    GF_ASSERT(conf);

    /* Reset op-versions to minimum */
    volinfo->op_version = 1;
    volinfo->client_op_version = 1;

    dict_foreach(volinfo->dict, _update_volume_op_versions, volinfo);

    /* Special case for open-behind
     * If cluster op-version >= 2 and open-behind hasn't been explicitly
     * disabled, volume op-versions must be updated to account for it
     */

    /* TODO: Remove once we have a general way to update automatically
     * enabled features
     */
    if (conf->op_version >= 2) {
        ob_enabled = dict_get_str_boolean(volinfo->dict,
                                          "performance.open-behind", _gf_true);
        if (ob_enabled) {
            if (volinfo->op_version < 2)
                volinfo->op_version = 2;
            if (volinfo->client_op_version < 2)
                volinfo->client_op_version = 2;
        }
    }

    if (volinfo->type == GF_CLUSTER_TYPE_DISPERSE) {
        if (volinfo->op_version < GD_OP_VERSION_3_6_0)
            volinfo->op_version = GD_OP_VERSION_3_6_0;
        if (volinfo->client_op_version < GD_OP_VERSION_3_6_0)
            volinfo->client_op_version = GD_OP_VERSION_3_6_0;
    }

    return;
}

/* A task is committed/completed once the task-id for it is cleared */
gf_boolean_t
gd_is_remove_brick_committed(glusterd_volinfo_t *volinfo)
{
    GF_ASSERT(volinfo);

    if ((GD_OP_REMOVE_BRICK == volinfo->rebal.op) &&
        !gf_uuid_is_null(volinfo->rebal.rebalance_id))
        return _gf_false;

    return _gf_true;
}

/* Tells if rebalance needs to be started for the given volume on the peer
 *
 * Rebalance should be started on a peer only if an involved brick is present on
 * the peer.
 *
 * For a normal rebalance, if any one brick of the given volume is present on
 * the peer, the rebalance process should be started.
 *
 * For a rebalance as part of a remove-brick operation, the rebalance process
 * should be started only if one of the bricks being removed is present on the
 * peer
 */
gf_boolean_t
gd_should_i_start_rebalance(glusterd_volinfo_t *volinfo)
{
    gf_boolean_t retval = _gf_false;
    int ret = -1;
    glusterd_brickinfo_t *brick = NULL;
    int count = 0;
    int i = 0;
    char key[64] = "";
    int keylen;
    char *brickname = NULL;
    unsigned char *uuid = MY_UUID;

    switch (volinfo->rebal.op) {
        case GD_OP_REBALANCE:
            cds_list_for_each_entry(brick, &volinfo->bricks, brick_list)
            {
                if (gf_uuid_compare(uuid, brick->uuid) == 0) {
                    retval = _gf_true;
                    break;
                }
            }
            break;
        case GD_OP_REMOVE_BRICK:
            ret = dict_get_int32(volinfo->rebal.dict, "count", &count);
            if (ret) {
                goto out;
            }
            for (i = 1; i <= count; i++) {
                keylen = snprintf(key, sizeof(key), "brick%d", i);
                ret = dict_get_strn(volinfo->rebal.dict, key, keylen,
                                    &brickname);
                if (ret)
                    goto out;
                ret = glusterd_volume_brickinfo_get_by_brick(brickname, volinfo,
                                                             &brick, _gf_false);
                if (ret)
                    goto out;
                if (gf_uuid_compare(uuid, brick->uuid) == 0) {
                    retval = _gf_true;
                    break;
                }
            }
            break;
        default:
            break;
    }

out:
    return retval;
}

int
glusterd_is_volume_quota_enabled(glusterd_volinfo_t *volinfo)
{
    return (glusterd_volinfo_get_boolean(volinfo, VKEY_FEATURES_QUOTA));
}

int
glusterd_is_bitrot_enabled(glusterd_volinfo_t *volinfo)
{
    return glusterd_volinfo_get_boolean(volinfo, VKEY_FEATURES_BITROT);
}

void
glusterd_clean_up_quota_store(glusterd_volinfo_t *volinfo)
{
    char voldir[PATH_MAX] = "";
    char quota_confpath[PATH_MAX] = "";
    char cksum_path[PATH_MAX] = "";
    glusterd_conf_t *conf = NULL;
    int32_t len = 0;

    conf = THIS->private;
    GF_ASSERT(conf);

    GLUSTERD_GET_VOLUME_DIR(voldir, volinfo, conf);

    len = snprintf(quota_confpath, sizeof(quota_confpath), "%s/%s", voldir,
                   GLUSTERD_VOLUME_QUOTA_CONFIG);
    if ((len < 0) || (len >= sizeof(quota_confpath))) {
        quota_confpath[0] = 0;
    }
    len = snprintf(cksum_path, sizeof(cksum_path), "%s/%s", voldir,
                   GLUSTERD_VOL_QUOTA_CKSUM_FILE);
    if ((len < 0) || (len >= sizeof(cksum_path))) {
        cksum_path[0] = 0;
    }

    sys_unlink(quota_confpath);
    sys_unlink(cksum_path);

    gf_store_handle_destroy(volinfo->quota_conf_shandle);
    volinfo->quota_conf_shandle = NULL;
    volinfo->quota_conf_version = 0;
}

rpc_clnt_t *
glusterd_rpc_clnt_unref(glusterd_conf_t *conf, rpc_clnt_t *rpc)
{
    rpc_clnt_t *ret = NULL;

    GF_ASSERT(conf);
    GF_ASSERT(rpc);
    synclock_unlock(&conf->big_lock);
    (void)rpc_clnt_reconnect_cleanup(&rpc->conn);
    ret = rpc_clnt_unref(rpc);
    synclock_lock(&conf->big_lock);

    return ret;
}

int32_t
glusterd_compare_volume_name(struct cds_list_head *list1,
                             struct cds_list_head *list2)
{
    glusterd_volinfo_t *volinfo1 = NULL;
    glusterd_volinfo_t *volinfo2 = NULL;

    volinfo1 = cds_list_entry(list1, glusterd_volinfo_t, vol_list);
    volinfo2 = cds_list_entry(list2, glusterd_volinfo_t, vol_list);
    return strcmp(volinfo1->volname, volinfo2->volname);
}

static int
gd_default_synctask_cbk(int ret, call_frame_t *frame, void *opaque)
{
    glusterd_conf_t *priv = THIS->private;
    synclock_unlock(&priv->big_lock);
    return ret;
}

int
glusterd_launch_synctask(synctask_fn_t fn, void *opaque)
{
    xlator_t *this = THIS;
    int ret = -1;

    /* synclock_lock must be called from within synctask, @fn must call it
     * before it starts with its work*/
    ret = synctask_new(this->ctx->env, fn, gd_default_synctask_cbk, NULL,
                       opaque);
    if (ret)
        gf_msg(this->name, GF_LOG_CRITICAL, 0, GD_MSG_SPAWN_SVCS_FAIL,
               "Failed to spawn bricks"
               " and other volume related services");

    return ret;
}

/*
 * glusterd_enable_default_options enable certain options by default on the
 * given volume based on the cluster op-version. This is called only during
 * volume create or during volume reset
 *
 * @volinfo - volume on which to enable the default options
 * @option  - option to be set to default. If NULL, all possible options will be
 *            set to default
 *
 * Returns 0 on success and -1 on failure. If @option is given, but doesn't
 * match any of the options that could be set, it is a success.
 */
/*
 * TODO: Make this able to parse the volume-set table to set options
 * Currently, the check and set for any option which wants to make use of this
 * 'framework' needs to be done here manually. This would mean more work for the
 * developer. This little extra work can be avoided if we make it possible to
 * parse the volume-set table to get the options which could be set and their
 * default values
 */
int
glusterd_enable_default_options(glusterd_volinfo_t *volinfo, char *option)
{
    int ret = 0;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
#ifdef IPV6_DEFAULT
    char *addr_family = "inet6";
#else
    char *addr_family = "inet";
#endif

    GF_VALIDATE_OR_GOTO(this->name, volinfo, out);

    conf = this->private;
    GF_ASSERT(conf);

    if (conf->op_version >= GD_OP_VERSION_3_8_0) {
        /* nfs.disable needs to be enabled for new volumes with
         * >= gluster version 3.7 (for now) 3.8 later
         */
        if (!option || !strcmp(NFS_DISABLE_MAP_KEY, option)) {
            ret = dict_set_dynstr_with_alloc(volinfo->dict, NFS_DISABLE_MAP_KEY,
                                             "on");
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                       "Failed to set option '" NFS_DISABLE_MAP_KEY
                       "' on volume "
                       "%s",
                       volinfo->volname);
                goto out;
            }
        }
    }

    /* Set needed volume options in volinfo->dict
     * For ex.,
     *
     * if (!option || !strcmp("someoption", option) {
     *      ret = dict_set_str(volinfo->dict, "someoption", "on");
     *      ...
     * }
     * */

    /* Option 'features.quota-deem-statfs' should not be turned off
     * with 'gluster volume reset <VOLNAME>', since quota features
     * can be reset only with 'gluster volume quota <VOLNAME>
     * disable'.
     */

    if (!option || !strcmp("features.quota-deem-statfs", option)) {
        if (glusterd_is_volume_quota_enabled(volinfo)) {
            ret = dict_set_dynstr_with_alloc(
                volinfo->dict, "features.quota-deem-statfs", "on");
            if (ret) {
                gf_msg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                       "Failed to set option "
                       "'features.quota-deem-statfs' "
                       "on volume %s",
                       volinfo->volname);
                goto out;
            }
        }
    }

    if (conf->op_version >= GD_OP_VERSION_3_9_0) {
        if (!option || !strcmp("transport.address-family", option)) {
            if (volinfo->transport_type == GF_TRANSPORT_TCP) {
                ret = dict_set_dynstr_with_alloc(
                    volinfo->dict, "transport.address-family", addr_family);
                if (ret) {
                    gf_msg(this->name, GF_LOG_ERROR, -ret,
                           GD_MSG_DICT_SET_FAILED,
                           "failed to set transport."
                           "address-family on %s",
                           volinfo->volname);
                    goto out;
                }
            }
        }
    }

    if (conf->op_version >= GD_OP_VERSION_7_0) {
        ret = dict_set_dynstr_with_alloc(volinfo->dict,
                                         "storage.fips-mode-rchecksum", "on");
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                   "Failed to set option 'storage.fips-mode-rchecksum' "
                   "on volume %s",
                   volinfo->volname);
            goto out;
        }
    }

    if ((conf->op_version >= GD_OP_VERSION_9_0) &&
        (volinfo->status == GLUSTERD_STATUS_NONE) &&
        (volinfo->type == GF_CLUSTER_TYPE_REPLICATE)) {
        ret = dict_set_dynstr_with_alloc(volinfo->dict,
                                         "cluster.granular-entry-heal", "on");
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                   "Failed to set option 'cluster.granular-entry-heal' "
                   "on volume %s",
                   volinfo->volname);
            goto out;
        }
    }
out:
    return ret;
}

void
glusterd_get_gfproxy_client_volfile(glusterd_volinfo_t *volinfo, char *path,
                                    int path_len)
{
    char workdir[PATH_MAX] = "";
    glusterd_conf_t *priv = THIS->private;

    GLUSTERD_GET_VOLUME_DIR(workdir, volinfo, priv);

    switch (volinfo->transport_type) {
        case GF_TRANSPORT_TCP:
        case GF_TRANSPORT_BOTH_TCP_RDMA:
            snprintf(path, path_len, "%s/trusted-%s.tcp-gfproxy-fuse.vol",
                     workdir, volinfo->volname);
            break;

        case GF_TRANSPORT_RDMA:
            snprintf(path, path_len, "%s/trusted-%s.rdma-gfproxy-fuse.vol",
                     workdir, volinfo->volname);
            break;
        default:
            break;
    }
}

void
glusterd_get_rebalance_volfile(glusterd_volinfo_t *volinfo, char *path,
                               int path_len)
{
    char workdir[PATH_MAX] = "";
    glusterd_conf_t *priv = THIS->private;

    GLUSTERD_GET_VOLUME_DIR(workdir, volinfo, priv);

    snprintf(path, path_len, "%s/%s-rebalance.vol", workdir, volinfo->volname);
}

/* This function will update the backend file-system
 * type and the mount options in origin and snap brickinfo.
 * This will be later used to perform file-system specific operation
 * during LVM snapshot.
 *
 * @param brick_path       brickpath for which fstype to be found
 * @param brickinfo        brickinfo of snap/origin volume
 * @return 0 on success and -1 on failure
 */
int
glusterd_update_mntopts(char *brick_path, glusterd_brickinfo_t *brickinfo)
{
    int32_t ret = -1;
    char *mnt_pt = NULL;
    char buff[PATH_MAX] = "";
    struct mntent *entry = NULL;
    struct mntent save_entry = {
        0,
    };

    GF_ASSERT(brick_path);
    GF_ASSERT(brickinfo);

    ret = glusterd_get_brick_root(brick_path, &mnt_pt);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_BRICKPATH_ROOT_GET_FAIL,
               "getting the root "
               "of the brick (%s) failed ",
               brick_path);
        goto out;
    }

    entry = glusterd_get_mnt_entry_info(mnt_pt, buff, sizeof(buff),
                                        &save_entry);
    if (!entry) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_MNTENTRY_GET_FAIL,
               "getting the mount entry for "
               "the brick (%s) failed",
               brick_path);
        ret = -1;
        goto out;
    }

    if (snprintf(brickinfo->fstype, sizeof(brickinfo->fstype), "%s",
                 entry->mnt_type) >= sizeof(brickinfo->fstype)) {
        ret = -1;
        goto out;
    }
    (void)snprintf(brickinfo->mnt_opts, sizeof(brickinfo->mnt_opts), "%s",
                   entry->mnt_opts);

    gf_strncpy(brickinfo->mnt_opts, entry->mnt_opts,
               sizeof(brickinfo->mnt_opts));

    ret = 0;
out:
    if (mnt_pt)
        GF_FREE(mnt_pt);
    return ret;
}

static int
glusterd_get_value_for_vme_entry(struct volopt_map_entry *vme, char **def_val)
{
    int ret = -1;
    char *key = NULL;
    xlator_t *this = THIS;
    char *local_def_val = NULL;
    void *dl_handle = NULL;
    volume_opt_list_t vol_opt_handle = {
        {0},
    };

    CDS_INIT_LIST_HEAD(&vol_opt_handle.list);

    if (_get_xlator_opt_key_from_vme(vme, &key)) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_GET_KEY_FAILED,
               "Failed to get %s key from "
               "volume option entry",
               vme->key);
        goto out;
    }

    ret = xlator_volopt_dynload(vme->voltype, &dl_handle, &vol_opt_handle);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_XLATOR_VOLOPT_DYNLOAD_ERROR,
               "xlator_volopt_dynload error "
               "(%d)",
               ret);
        ret = -2;
        goto cont;
    }

    ret = xlator_option_info_list(&vol_opt_handle, key, &local_def_val, NULL);
    if (ret) {
        /* Swallow error if option was not found. */
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_GET_KEY_FAILED,
               "Failed to get option for %s "
               "key",
               key);
        ret = -2;
        goto cont;
    }
    if (!local_def_val)
        local_def_val = "(null)";

    *def_val = gf_strdup(local_def_val);

cont:
    if (dl_handle) {
        dlclose(dl_handle);
        dl_handle = NULL;
        vol_opt_handle.given_opt = NULL;
    }
    if (key) {
        _free_xlator_opt_key(key);
        key = NULL;
    }

    if (ret)
        goto out;

out:
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

int
glusterd_get_global_max_op_version(rpcsvc_request_t *req, dict_t *ctx,
                                   int count)
{
    int ret = -1;
    char *def_val = NULL;
    char dict_key[50] = "";
    int keylen;

    ret = glusterd_mgmt_v3_initiate_all_phases(req, GD_OP_MAX_OPVERSION, ctx);

    ret = dict_get_str(ctx, "max-opversion", &def_val);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get max-opversion value from"
               " dictionary");
        goto out;
    }

    keylen = sprintf(dict_key, "key%d", count);
    ret = dict_set_nstrn(ctx, dict_key, keylen, GLUSTERD_MAX_OP_VERSION_KEY,
                         SLEN(GLUSTERD_MAX_OP_VERSION_KEY));
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set %s in "
               "dictionary",
               GLUSTERD_MAX_OP_VERSION_KEY);
        goto out;
    }

    sprintf(dict_key, "value%d", count);
    ret = dict_set_dynstr_with_alloc(ctx, dict_key, def_val);
    if (ret) {
        gf_msg(THIS->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set %s for key %s in dictionary", def_val,
               GLUSTERD_MAX_OP_VERSION_KEY);
        goto out;
    }

out:
    return ret;
}

int
glusterd_get_global_options_for_all_vols(rpcsvc_request_t *req, dict_t *ctx,
                                         char **op_errstr)
{
    int ret = -1;
    int count = 0;
    gf_boolean_t all_opts = _gf_false;
    gf_boolean_t key_found = _gf_false;
    glusterd_conf_t *priv = NULL;
    xlator_t *this = THIS;
    char *key = NULL;
    char *key_fixed = NULL;
    char dict_key[50] = "";
    char *def_val = NULL;
    char err_str[PATH_MAX] = "";
    char *allvolopt = NULL;
    int32_t i = 0;
    gf_boolean_t exists = _gf_false;
    gf_boolean_t need_free = _gf_false;

    priv = this->private;
    GF_VALIDATE_OR_GOTO(this->name, priv, out);

    GF_VALIDATE_OR_GOTO(this->name, ctx, out);

    ret = dict_get_str(ctx, "key", &key);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Failed to get option key from dictionary");
        goto out;
    }

    if (strcasecmp(key, "all") == 0)
        all_opts = _gf_true;
    else {
        exists = glusterd_check_option_exists(key, &key_fixed);
        if (!exists) {
            snprintf(err_str, sizeof(err_str),
                     "Option "
                     "with name: %s does not exist",
                     key);
            gf_msg(this->name, GF_LOG_ERROR, EINVAL, GD_MSG_UNKNOWN_KEY, "%s",
                   err_str);
            if (key_fixed)
                snprintf(err_str, sizeof(err_str), "Did you mean %s?",
                         key_fixed);
            ret = -1;
            goto out;
        }
        if (key_fixed)
            key = key_fixed;
    }
    /* coverity[CONSTANT_EXPRESSION_RESULT] */
    ALL_VOLUME_OPTION_CHECK("all", _gf_true, key, ret, op_errstr, out);

    for (i = 0; valid_all_vol_opts[i].option; i++) {
        allvolopt = valid_all_vol_opts[i].option;

        if (!all_opts && strcmp(key, allvolopt) != 0)
            continue;

        /* Found global option */
        if (strcmp(allvolopt, GLUSTERD_MAX_OP_VERSION_KEY) == 0) {
            count++;
            ret = glusterd_get_global_max_op_version(req, ctx, count);
            if (ret)
                goto out;
            else
                continue;
        }

        ret = dict_get_str(priv->opts, allvolopt, &def_val);

        /* If global option isn't set explicitly */

        if (!def_val) {
            if (!strcmp(allvolopt, GLUSTERD_GLOBAL_OP_VERSION_KEY)) {
                gf_asprintf(&def_val, "%d", priv->op_version);
                need_free = _gf_true;
            } else {
                gf_asprintf(&def_val, "%s (DEFAULT)",
                            valid_all_vol_opts[i].dflt_val);
                need_free = _gf_true;
            }
        }

        count++;
        ret = sprintf(dict_key, "key%d", count);
        ret = dict_set_strn(ctx, dict_key, ret, allvolopt);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set %s in dictionary", allvolopt);
            goto out;
        }

        sprintf(dict_key, "value%d", count);
        ret = dict_set_dynstr_with_alloc(ctx, dict_key, def_val);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to set %s for key %s in dictionary", def_val,
                   allvolopt);
            goto out;
        }

        if (need_free) {
            GF_FREE(def_val);
            need_free = _gf_false;
        }
        def_val = NULL;
        allvolopt = NULL;

        if (!all_opts)
            break;
    }

    ret = dict_set_int32_sizen(ctx, "count", count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set count in dictionary");
    }

out:
    if (ret && !all_opts && !key_found) {
        if (err_str[0] == 0)
            snprintf(err_str, sizeof(err_str), "option %s does not exist", key);
        if (*op_errstr == NULL)
            *op_errstr = gf_strdup(err_str);
    }

    if (ret && need_free) {
        GF_FREE(def_val);
    }
    GF_FREE(key_fixed);
    gf_msg_debug(THIS->name, 0, "Returning %d", ret);

    return ret;
}

static char *
glusterd_get_option_value(glusterd_volinfo_t *volinfo, char *key)
{
    char *value = NULL;

    if (!glusterd_is_volume_replicate(volinfo))
        goto ret;

    if (!strcmp(key, "performance.client-io-threads")) {
        value = "off";
    } else if (!strcmp(key, "cluster.quorum-type")) {
        if (volinfo->replica_count % 2) {
            value = "auto";
        }
    }
ret:
    return value;
}

int
glusterd_get_default_val_for_volopt(dict_t *ctx, gf_boolean_t all_opts,
                                    char *input_key, char *orig_key,
                                    glusterd_volinfo_t *volinfo,
                                    char **op_errstr)
{
    struct volopt_map_entry *vme = NULL;
    int ret = -1;
    int count = 0;
    xlator_t *this = THIS;
    char *def_val = NULL;
    char *def_val_str = NULL;
    char dict_key[50] = "";
    int keylen;
    gf_boolean_t key_found = _gf_false;
    gf_boolean_t get_value_vme = _gf_false;
    glusterd_conf_t *priv = NULL;
    dict_t *vol_dict = NULL;

    priv = this->private;
    GF_VALIDATE_OR_GOTO(this->name, priv, out);

    vol_dict = volinfo->dict;
    GF_VALIDATE_OR_GOTO(this->name, vol_dict, out);

    /* Check whether key is passed for a single option */
    if (!all_opts && !input_key) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_KEY_NULL, "Key is NULL");
        goto out;
    }

    for (vme = &glusterd_volopt_map[0]; vme->key; vme++) {
        if (!all_opts && strcmp(vme->key, input_key))
            continue;
        key_found = _gf_true;
        get_value_vme = _gf_false;
        /* First look for the key in the priv->opts for global option
         * and then into vol_dict, if its not present then look for
         * translator default value */
        keylen = strlen(vme->key);
        ret = dict_get_strn(priv->opts, vme->key, keylen, &def_val);
        if (!def_val) {
            ret = dict_get_strn(vol_dict, vme->key, keylen, &def_val);
            if (ret == -ENOENT)
                def_val = glusterd_get_option_value(volinfo, vme->key);
            if (!def_val) {
                if (vme->value) {
                    def_val = vme->value;
                } else {
                    ret = glusterd_get_value_for_vme_entry(vme, &def_val);
                    get_value_vme = _gf_true;
                    if (!all_opts && ret)
                        goto out;
                    else if (ret == -2)
                        continue;
                }
            }
        }
        count++;
        keylen = sprintf(dict_key, "key%d", count);
        ret = dict_set_strn(ctx, dict_key, keylen, vme->key);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to "
                   "set %s in dictionary",
                   vme->key);
            goto out;
        }
        sprintf(dict_key, "value%d", count);
        if (get_value_vme) {  // the value was never changed  - DEFAULT is used
            gf_asprintf(&def_val_str, "%s (DEFAULT)", def_val);
            ret = dict_set_dynstr_with_alloc(ctx, dict_key, def_val_str);
            GF_FREE(def_val_str);
            def_val_str = NULL;
        } else
            ret = dict_set_dynstr_with_alloc(ctx, dict_key, def_val);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
                   "Failed to "
                   "set %s for key %s in dictionary",
                   def_val, vme->key);
            goto out;
        }
        if (get_value_vme)
            GF_FREE(def_val);

        def_val = NULL;
        if (!all_opts)
            break;
    }
    if (!all_opts && !key_found)
        goto out;

    ret = dict_set_int32_sizen(ctx, "count", count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_SET_FAILED,
               "Failed to set count "
               "in dictionary");
    }

out:
    if (ret && !all_opts && !key_found) {
        char err_str[PATH_MAX];
        snprintf(err_str, sizeof(err_str), "option %s does not exist",
                 orig_key);
        *op_errstr = gf_strdup(err_str);
    }
    if (def_val)
        GF_FREE(def_val);
    gf_msg_debug(this->name, 0, "Returning %d", ret);
    return ret;
}

static int
glusterd_get_volopt_content(dict_t *ctx, gf_boolean_t xml_out)
{
    void *dl_handle = NULL;
    volume_opt_list_t vol_opt_handle = {
        {0},
    };
    char *key = NULL;
    struct volopt_map_entry *vme = NULL;
    int ret = -1;
    char *def_val = NULL;
    char *descr = NULL;
    char *output = NULL;
    size_t size = 0;
    size_t used = 0;
    gf_boolean_t free_descr = _gf_false;
#if (HAVE_LIB_XML)
    xmlTextWriterPtr writer = NULL;
    xmlBufferPtr buf = NULL;

    if (xml_out) {
        ret = init_sethelp_xml_doc(&writer, &buf);
        if (ret) /*logging done in init_xml_lib*/
            goto out;
    }
#endif

    if (!xml_out) {
        size = 65536;
        output = GF_MALLOC(size, gf_common_mt_char);
        if (output == NULL) {
            ret = -1;
            goto out;
        }
    }

    CDS_INIT_LIST_HEAD(&vol_opt_handle.list);

    for (vme = &glusterd_volopt_map[0]; vme->key; vme++) {
        if ((vme->type == NO_DOC) || (vme->type == GLOBAL_NO_DOC))
            continue;

        free_descr = _gf_false;

        if (vme->description) {
            descr = vme->description;
            def_val = vme->value;
        } else {
            if (_get_xlator_opt_key_from_vme(vme, &key)) {
                gf_msg_debug("glusterd", 0,
                             "Failed to "
                             "get %s key from volume option entry",
                             vme->key);
                goto out; /*Some error while getting key*/
            }

            ret = xlator_volopt_dynload(vme->voltype, &dl_handle,
                                        &vol_opt_handle);

            if (ret) {
                gf_msg_debug("glusterd", 0, "xlator_volopt_dynload error(%d)",
                             ret);
                ret = 0;
                goto cont;
            }

            ret = xlator_option_info_list(&vol_opt_handle, key, &def_val,
                                          &descr);
            if (ret) { /* Swallow error if option was not found. */
                gf_msg_debug("glusterd", 0, "Failed to get option for %s key",
                             key);
                ret = 0;
                goto cont;
            }
            free_descr = _gf_true;
        }

        if (xml_out) {
#if (HAVE_LIB_XML)
            if (xml_add_volset_element(writer, vme->key, def_val, descr)) {
                ret = -1;
                goto cont;
            }
#else
            gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_MODULE_NOT_INSTALLED,
                   "Libxml not present");
#endif
        } else {
            void *tmp;
            int len;

            do {
                len = snprintf(output + used, size - used,
                               "Option: %s\nDefault Value: %s\n"
                               "Description: %s\n\n",
                               vme->key, def_val, descr);
                if (len < 0) {
                    ret = -1;
                    goto cont;
                }
                if (used + len < size) {
                    used += len;
                    break;
                }

                size += (len + 65536) & ~65535;
                tmp = GF_REALLOC(output, size);
                if (tmp == NULL) {
                    ret = -1;
                    goto cont;
                }
                output = tmp;
            } while (1);
        }
    cont:
        if (free_descr)
            GF_FREE(descr);
        if (dl_handle) {
            dlclose(dl_handle);
            dl_handle = NULL;
            vol_opt_handle.given_opt = NULL;
        }
        if (key) {
            _free_xlator_opt_key(key);
            key = NULL;
        }
        if (ret)
            goto out;
    }

#if (HAVE_LIB_XML)
    if ((xml_out) && (ret = end_sethelp_xml_doc(writer)))
        goto out;
#else
    if (xml_out)
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_MODULE_NOT_INSTALLED,
               "Libxml not present");
#endif

    if (xml_out) {
#if (HAVE_LIB_XML)
        output = gf_strdup((char *)buf->content);
        if (NULL == output) {
            ret = -1;
            goto out;
        }
#else
        gf_msg("glusterd", GF_LOG_ERROR, 0, GD_MSG_MODULE_NOT_INSTALLED,
               "Libxml not present");
#endif
    }

    ret = dict_set_dynstr_sizen(ctx, "help-str", output);
    if (ret >= 0) {
        output = NULL;
    }
out:
    GF_FREE(output);
    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

int
glusterd_check_client_op_version_support(char *volname, uint32_t op_version,
                                         char **op_errstr)
{
    int ret = 0;
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    rpc_transport_t *xprt = NULL;

    priv = this->private;
    GF_ASSERT(priv);

    pthread_mutex_lock(&priv->xprt_lock);
    list_for_each_entry(xprt, &priv->xprt_list, list)
    {
        if ((!strcmp(volname, xprt->peerinfo.volname)) &&
            ((op_version > xprt->peerinfo.max_op_version) ||
             (op_version < xprt->peerinfo.min_op_version))) {
            ret = -1;
            break;
        }
    }
    pthread_mutex_unlock(&priv->xprt_lock);

    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_UNSUPPORTED_VERSION,
               "Client %s is running with min_op_version as %d and "
               "max_op_version as %d and don't support the required "
               "op-version %d",
               xprt->peerinfo.identifier, xprt->peerinfo.min_op_version,
               xprt->peerinfo.max_op_version, op_version);
        if (op_errstr)
            ret = gf_asprintf(op_errstr,
                              "One of the client %s is "
                              "running with op-version %d and "
                              "doesn't support the required "
                              "op-version %d. This client needs to"
                              " be upgraded or disconnected "
                              "before running this command again",
                              xprt->peerinfo.identifier,
                              xprt->peerinfo.max_op_version, op_version);

        return -1;
    }
    return 0;
}

gf_boolean_t
glusterd_is_volume_started(glusterd_volinfo_t *volinfo)
{
    GF_ASSERT(volinfo);
    return (volinfo->status == GLUSTERD_STATUS_STARTED);
}

/* This function will insert the element to the list in a order.
   Order will be based on the compare function provided as a input.
   If element to be inserted in ascending order compare should return:
    0: if both the arguments are equal
   >0: if first argument is greater than second argument
   <0: if first argument is less than second argument */
void
glusterd_list_add_order(struct cds_list_head *new, struct cds_list_head *head,
                        int (*compare)(struct cds_list_head *,
                                       struct cds_list_head *))
{
    struct cds_list_head *pos = NULL;

    cds_list_for_each_rcu(pos, head)
    {
        if (compare(new, pos) <= 0)
            break;
    }

    cds_list_add_rcu(new, rcu_dereference(pos->prev));
}

int
glusterd_handle_replicate_brick_ops(glusterd_volinfo_t *volinfo,
                                    glusterd_brickinfo_t *brickinfo,
                                    glusterd_op_t op)
{
    int32_t ret = -1;
    char tmpmount[] = "/tmp/mntXXXXXX";
    char logfile[PATH_MAX] = "";
    int dirty[3] = {
        0,
    };
    runner_t runner = {0};
    glusterd_conf_t *priv = NULL;
    char *pid = NULL;
    char vpath[PATH_MAX] = "";
    char *volfileserver = NULL;

    xlator_t *this = THIS;
    priv = this->private;
    GF_VALIDATE_OR_GOTO(this->name, priv, out);

    dirty[2] = htobe32(1);

    ret = sys_lsetxattr(brickinfo->path, GF_AFR_DIRTY, dirty, sizeof(dirty), 0);
    if (ret == -1) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_SET_XATTR_FAIL,
                "Attribute=%s", GF_AFR_DIRTY, "Reason=%s", strerror(errno),
                NULL);
        goto out;
    }

    if (mkdtemp(tmpmount) == NULL) {
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_CREATE_DIR_FAILED,
                NULL);
        ret = -1;
        goto out;
    }

    ret = gf_asprintf(&pid, "%d", GF_CLIENT_PID_ADD_REPLICA_MOUNT);
    if (ret < 0)
        goto out;

    switch (op) {
        case GD_OP_REPLACE_BRICK:
            if (dict_get_str(this->options, "transport.socket.bind-address",
                             &volfileserver) != 0)
                volfileserver = "localhost";

            snprintf(logfile, sizeof(logfile), "%s/%s-replace-brick-mount.log",
                     priv->logdir, volinfo->volname);
            if (!*logfile) {
                ret = -1;
                goto out;
            }
            runinit(&runner);
            runner_add_args(&runner, SBIN_DIR "/glusterfs", "-s", volfileserver,
                            "--volfile-id", volinfo->volname, "--client-pid",
                            pid, "-l", logfile, tmpmount, NULL);
            break;

        case GD_OP_ADD_BRICK:
            snprintf(logfile, sizeof(logfile), "%s/%s-add-brick-mount.log",
                     priv->logdir, volinfo->volname);
            if (!*logfile) {
                ret = -1;
                goto out;
            }
            ret = glusterd_get_dummy_client_filepath(vpath, volinfo,
                                                     volinfo->transport_type);
            if (ret) {
                gf_log("", GF_LOG_ERROR,
                       "Failed to get "
                       "volfile path");
                goto out;
            }
            runinit(&runner);
            runner_add_args(&runner, SBIN_DIR "/glusterfs", "--volfile", vpath,
                            "--client-pid", pid, "-l", logfile, tmpmount, NULL);
            break;
        default:
            break;
    }
    synclock_unlock(&priv->big_lock);
    ret = runner_run(&runner);

    if (ret) {
        gf_log(this->name, GF_LOG_ERROR,
               "mount command"
               " failed.");
        goto lock;
    }
    ret = sys_lsetxattr(
        tmpmount,
        (op == GD_OP_REPLACE_BRICK) ? GF_AFR_REPLACE_BRICK : GF_AFR_ADD_BRICK,
        brickinfo->brick_id, sizeof(brickinfo->brick_id), 0);
    if (ret == -1)
        gf_smsg(this->name, GF_LOG_ERROR, errno, GD_MSG_SET_XATTR_FAIL,
                "Attribute=%s, Reason=%s",
                (op == GD_OP_REPLACE_BRICK) ? GF_AFR_REPLACE_BRICK
                                            : GF_AFR_ADD_BRICK,
                strerror(errno), NULL);
    gf_umount_lazy(this->name, tmpmount, 1);
lock:
    synclock_lock(&priv->big_lock);
out:
    if (pid)
        GF_FREE(pid);
    gf_msg_debug(this->name, 0, "Returning with ret");
    return ret;
}

void
assign_brick_groups(glusterd_volinfo_t *volinfo)
{
    glusterd_brickinfo_t *brickinfo = NULL;
    uint16_t group_num = 0;
    int in_group = 0;

    list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
    {
        brickinfo->group = group_num;
        if (++in_group >= volinfo->replica_count) {
            in_group = 0;
            ++group_num;
        }
    }
}

glusterd_brickinfo_t *
get_last_brick_of_brick_group(glusterd_volinfo_t *volinfo,
                              glusterd_brickinfo_t *brickinfo)
{
    glusterd_brickinfo_t *next = NULL;
    glusterd_brickinfo_t *last = NULL;

    last = brickinfo;
    for (;;) {
        next = list_next(last, &volinfo->bricks, glusterd_brickinfo_t,
                         brick_list);
        if (!next || (next->group != brickinfo->group)) {
            break;
        }
        last = next;
    }

    return last;
}

int
glusterd_get_rb_dst_brickinfo(glusterd_volinfo_t *volinfo,
                              glusterd_brickinfo_t **brickinfo)
{
    int32_t ret = -1;

    if (!volinfo || !brickinfo)
        goto out;

    *brickinfo = volinfo->rep_brick.dst_brick;

    ret = 0;

out:
    return ret;
}

int
rb_update_dstbrick_port(glusterd_brickinfo_t *dst_brickinfo, dict_t *rsp_dict,
                        dict_t *req_dict)
{
    int ret = 0;
    int dict_ret = 0;
    int dst_port = 0;

    dict_ret = dict_get_int32(req_dict, "dst-brick-port", &dst_port);
    if (!dict_ret)
        dst_brickinfo->port = dst_port;

    if (glusterd_gf_is_local_addr(dst_brickinfo->hostname)) {
        gf_msg("glusterd", GF_LOG_INFO, 0, GD_MSG_BRK_PORT_NO_ADD_INDO,
               "adding dst-brick port no %d", dst_port);

        if (rsp_dict) {
            ret = dict_set_int32_sizen(rsp_dict, "dst-brick-port",
                                       dst_brickinfo->port);
            if (ret) {
                gf_msg_debug("glusterd", 0,
                             "Could not set dst-brick port no in rsp dict");
                goto out;
            }
        }

        if (req_dict && !dict_ret) {
            ret = dict_set_int32_sizen(req_dict, "dst-brick-port",
                                       dst_brickinfo->port);
            if (ret) {
                gf_msg_debug("glusterd", 0, "Could not set dst-brick port no");
                goto out;
            }
        }
    }
out:
    return ret;
}

int
glusterd_brick_op_prerequisites(dict_t *dict, char **op, glusterd_op_t *gd_op,
                                char **volname, glusterd_volinfo_t **volinfo,
                                char **src_brick,
                                glusterd_brickinfo_t **src_brickinfo,
                                char *pidfile, char **op_errstr,
                                dict_t *rsp_dict)
{
    int ret = 0;
    char msg[2048] = "";
    gsync_status_param_t param = {
        0,
    };
    xlator_t *this = THIS;
    glusterd_conf_t *priv = NULL;
    glusterd_volinfo_t *v = NULL;
    glusterd_brickinfo_t *b = NULL;

    priv = this->private;
    GF_ASSERT(priv);

    ret = dict_get_str(dict, "operation", op);
    if (ret) {
        gf_msg_debug(this->name, 0, "dict get on operation type failed");
        goto out;
    }

    *gd_op = gd_cli_to_gd_op(*op);
    if (*gd_op < 0)
        goto out;

    ret = dict_get_str(dict, "volname", volname);

    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get volume name");
        goto out;
    }

    ret = glusterd_volinfo_find(*volname, volinfo);
    if (ret) {
        snprintf(msg, sizeof(msg), "volume: %s does not exist", *volname);
        *op_errstr = gf_strdup(msg);
        goto out;
    }

    if (GLUSTERD_STATUS_STARTED != (*volinfo)->status) {
        ret = -1;
        snprintf(msg, sizeof(msg), "volume: %s is not started", *volname);
        *op_errstr = gf_strdup(msg);
        goto out;
    }

    /* If geo-rep is configured, for this volume, it should be stopped. */
    param.volinfo = *volinfo;
    ret = glusterd_check_geo_rep_running(&param, op_errstr);
    if (ret || param.is_active) {
        ret = -1;
        goto out;
    }

    if (glusterd_is_defrag_on(*volinfo)) {
        snprintf(msg, sizeof(msg),
                 "Volume name %s rebalance is in "
                 "progress. Please retry after completion",
                 *volname);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_OIP_RETRY_LATER, "%s", msg);
        *op_errstr = gf_strdup(msg);
        ret = -1;
        goto out;
    }

    if (dict) {
        if (!glusterd_is_fuse_available()) {
            gf_msg(this->name, GF_LOG_ERROR, 0,
                   (*gd_op == GD_OP_REPLACE_BRICK)
                       ? GD_MSG_RB_CMD_FAIL
                       : GD_MSG_RESET_BRICK_CMD_FAIL,
                   "Unable to open /dev/"
                   "fuse (%s), %s command failed",
                   strerror(errno), gd_rb_op_to_str(*op));
            snprintf(msg, sizeof(msg),
                     "Fuse unavailable\n "
                     "%s failed",
                     gd_rb_op_to_str(*op));
            *op_errstr = gf_strdup(msg);
            ret = -1;
            goto out;
        }
    }

    ret = dict_get_str(dict, "src-brick", src_brick);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get src brick");
        goto out;
    }

    gf_msg_debug(this->name, 0, "src brick=%s", *src_brick);

    ret = glusterd_volume_brickinfo_get_by_brick(*src_brick, *volinfo,
                                                 src_brickinfo, _gf_false);
    if (ret) {
        snprintf(msg, sizeof(msg),
                 "brick: %s does not exist in "
                 "volume: %s",
                 *src_brick, *volname);
        gf_smsg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRICK_NOT_FOUND,
                "Brick=%s, Volume=%s", *src_brick, *volname, NULL);
        *op_errstr = gf_strdup(msg);
        goto out;
    }

    if (glusterd_gf_is_local_addr((*src_brickinfo)->hostname)) {
        gf_msg_debug(this->name, 0, "I AM THE SOURCE HOST");
        if ((*src_brickinfo)->port && rsp_dict) {
            ret = dict_set_int32_sizen(rsp_dict, "src-brick-port",
                                       (*src_brickinfo)->port);
            if (ret) {
                gf_msg_debug(this->name, 0, "Could not set src-brick-port=%d",
                             (*src_brickinfo)->port);
            }
        }

        v = *volinfo;
        b = *src_brickinfo;
        GLUSTERD_GET_BRICK_PIDFILE(pidfile, v, b, priv);
    }

    ret = 0;
out:
    return ret;
}

int
glusterd_get_dst_brick_info(char **dst_brick, char *volname, char **op_errstr,
                            glusterd_brickinfo_t **dst_brickinfo, char **host,
                            dict_t *dict, char **dup_dstbrick)
{
    char *path = NULL;
    char *c = NULL;
    char msg[2048] = "";
    xlator_t *this = THIS;
    int ret = 0;

    ret = dict_get_str(dict, "dst-brick", dst_brick);

    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
               "Unable to get dest brick.");
        goto out;
    }

    gf_msg_debug(this->name, 0, "dst brick=%s", *dst_brick);

    if (!glusterd_store_is_valid_brickpath(volname, *dst_brick) ||
        !glusterd_is_valid_volfpath(volname, *dst_brick)) {
        snprintf(msg, sizeof(msg),
                 "brick path %s is too "
                 "long.",
                 *dst_brick);
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BRKPATH_TOO_LONG, "%s", msg);
        *op_errstr = gf_strdup(msg);

        ret = -1;
        goto out;
    }

    *dup_dstbrick = gf_strdup(*dst_brick);
    if (!*dup_dstbrick) {
        ret = -1;
        goto out;
    }

    /*
     * IPv4 address contains '.' and ipv6 addresses contains ':'
     * So finding the last occurrence of ':' to
     * mark the start of brick path
     */
    c = strrchr(*dup_dstbrick, ':');
    if (c != NULL) {
        c[0] = '\0';
        *host = *dup_dstbrick;
        path = c++;
    }

    if (!host || !path) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BAD_FORMAT,
               "dst brick %s is not of "
               "form <HOSTNAME>:<export-dir>",
               *dst_brick);
        ret = -1;
        goto out;
    }

    ret = glusterd_brickinfo_new_from_brick(*dst_brick, dst_brickinfo, _gf_true,
                                            NULL);
    if (ret)
        goto out;

    ret = 0;
out:
    return ret;
}

int
glusterd_get_volinfo_from_brick(char *brick, glusterd_volinfo_t **volinfo)
{
    int ret = -1;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    glusterd_volinfo_t *voliter = NULL;
    glusterd_brickinfo_t *brickiter = NULL;
    glusterd_snap_t *snap = NULL;

    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);

    /* First check for normal volumes */
    cds_list_for_each_entry(voliter, &conf->volumes, vol_list)
    {
        cds_list_for_each_entry(brickiter, &voliter->bricks, brick_list)
        {
            if (gf_uuid_compare(brickiter->uuid, conf->uuid))
                continue;
            if (!strcmp(brickiter->path, brick)) {
                *volinfo = voliter;
                return 0;
            }
        }
    }
    /* In case normal volume is not found, check for snapshot volumes */
    cds_list_for_each_entry(snap, &conf->snapshots, snap_list)
    {
        cds_list_for_each_entry(voliter, &snap->volumes, vol_list)
        {
            cds_list_for_each_entry(brickiter, &voliter->bricks, brick_list)
            {
                if (gf_uuid_compare(brickiter->uuid, conf->uuid))
                    continue;
                if (!strcmp(brickiter->path, brick)) {
                    *volinfo = voliter;
                    return 0;
                }
            }
        }
    }

out:
    return ret;
}

glusterd_op_t
gd_cli_to_gd_op(char *cli_op)
{
    if (!strcmp(cli_op, "GF_RESET_OP_START") ||
        !strcmp(cli_op, "GF_RESET_OP_COMMIT") ||
        !strcmp(cli_op, "GF_RESET_OP_COMMIT_FORCE")) {
        return GD_OP_RESET_BRICK;
    }

    if (!strcmp(cli_op, "GF_REPLACE_OP_COMMIT_FORCE"))
        return GD_OP_REPLACE_BRICK;

    return -1;
}

char *
gd_rb_op_to_str(char *op)
{
    if (!strcmp(op, "GF_RESET_OP_START"))
        return "reset-brick start";
    if (!strcmp(op, "GF_RESET_OP_COMMIT"))
        return "reset-brick commit";
    if (!strcmp(op, "GF_RESET_OP_COMMIT_FORCE"))
        return "reset-brick commit force";
    if (!strcmp(op, "GF_REPLACE_OP_COMMIT_FORCE"))
        return "replace-brick commit force";
    return NULL;
}

gf_boolean_t
glusterd_is_profile_on(glusterd_volinfo_t *volinfo)
{
    int ret = -1;
    gf_boolean_t is_latency_on = _gf_false;
    gf_boolean_t is_fd_stats_on = _gf_false;

    GF_ASSERT(volinfo);

    ret = glusterd_volinfo_get_boolean(volinfo, VKEY_DIAG_CNT_FOP_HITS);
    if (ret != -1)
        is_fd_stats_on = ret;
    ret = glusterd_volinfo_get_boolean(volinfo, VKEY_DIAG_LAT_MEASUREMENT);
    if (ret != -1)
        is_latency_on = ret;
    if ((_gf_true == is_latency_on) && (_gf_true == is_fd_stats_on))
        return _gf_true;
    return _gf_false;
}

static gf_ai_compare_t
glusterd_compare_addrinfo(struct addrinfo *first, struct addrinfo *next)
{
    int ret = -1;
    struct addrinfo *tmp1 = NULL;
    struct addrinfo *tmp2 = NULL;
    char firstip[NI_MAXHOST] = {0.};
    char nextip[NI_MAXHOST] = {
        0,
    };

    for (tmp1 = first; tmp1 != NULL; tmp1 = tmp1->ai_next) {
        ret = getnameinfo(tmp1->ai_addr, tmp1->ai_addrlen, firstip, NI_MAXHOST,
                          NULL, 0, NI_NUMERICHOST);
        if (ret)
            return GF_AI_COMPARE_ERROR;
        for (tmp2 = next; tmp2 != NULL; tmp2 = tmp2->ai_next) {
            ret = getnameinfo(tmp2->ai_addr, tmp2->ai_addrlen, nextip,
                              NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
            if (ret)
                return GF_AI_COMPARE_ERROR;
            if (!strcmp(firstip, nextip)) {
                return GF_AI_COMPARE_MATCH;
            }
        }
    }
    return GF_AI_COMPARE_NO_MATCH;
}

/* Check for non optimal brick order for Replicate/Disperse:
 * Checks if bricks belonging to a replicate or disperse
 * volume are present on the same server
 */
int32_t
glusterd_check_brick_order(dict_t *dict, char *err_str, int32_t type,
                           char **volname, char **brick_list,
                           int32_t *brick_count, int32_t sub_count, int flag)
{
    int ret = -1;
    int i = 0;
    int j = 0;
    int k = 0;
    xlator_t *this = THIS;
    addrinfo_list_t *ai_list = NULL;
    addrinfo_list_t *ai_list_tmp1 = NULL;
    addrinfo_list_t *ai_list_tmp2 = NULL;
    char *brick = NULL;
    char *brick_list_dup = NULL;
    char *brick_list_ptr = NULL;
    char *tmpptr = NULL;
    struct addrinfo *ai_info = NULL;
    char brick_addr[128] = {
        0,
    };
    int addrlen = 0;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_brickinfo_t *brickinfo = NULL;
    addrinfo_list_t *pre_list = NULL;
    addrinfo_list_t *pre_list_tmp1 = NULL;
    addrinfo_list_t *pre_list_tmp2 = NULL;
    int count = 0;

    const char failed_string[2048] =
        "Failed to perform brick order "
        "check. Use 'force' at the end of the command"
        " if you want to override this behavior. ";
    const char found_string[2048] =
        "Multiple bricks of a %s "
        "volume are present on the same server. This "
        "setup is not optimal. Bricks should be on "
        "different nodes to have best fault tolerant "
        "configuration. Use 'force' at the end of the "
        "command if you want to override this "
        "behavior. ";

    ai_list = MALLOC(sizeof(addrinfo_list_t));
    if (ai_list == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "failed to allocate memory");
        goto out;
    }
    ai_list->info = NULL;
    CDS_INIT_LIST_HEAD(&ai_list->list);

    pre_list = MALLOC(sizeof(addrinfo_list_t));
    if (pre_list == NULL) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
               "failed to allocate memory");
        goto out;
    }
    pre_list->info = NULL;
    CDS_INIT_LIST_HEAD(&pre_list->list);

    if (!(*volname)) {
        ret = dict_get_str(dict, "volname", &(*volname));
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Unable to get volume name");
            goto out;
        }
    }

    /* This flag is set to test hostnames with existing bricks in the
     * volume when replica count is changed and brick(s) are added
     * to same replica set.
     */
    if (flag) {
        ret = glusterd_volinfo_find(*volname, &volinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
                   "Unable to find volume: %s", *volname);
            goto out;
        }
        cds_list_for_each_entry(brickinfo, &volinfo->bricks, brick_list)
        {
            ret = getaddrinfo(brickinfo->hostname, NULL, NULL, &ai_info);
            if (ret != 0) {
                gf_msg(this->name, GF_LOG_ERROR, 0,
                       GD_MSG_HOSTNAME_RESOLVE_FAIL,
                       "unable to resolve host name for addr %s",
                       brickinfo->hostname);
                goto out;
            }
            pre_list_tmp1 = MALLOC(sizeof(addrinfo_list_t));
            if (pre_list_tmp1 == NULL) {
                gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                       "failed to allocate "
                       "memory");
                freeaddrinfo(ai_info);
                goto out;
            }
            pre_list_tmp1->info = ai_info;
            cds_list_add_tail(&pre_list_tmp1->list, &pre_list->list);
            pre_list_tmp1 = NULL;
            count++;
        }
    }

    if (!(*brick_list)) {
        ret = dict_get_str(dict, "bricks", &(*brick_list));
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Bricks check : Could not "
                   "retrieve bricks list");
            goto out;
        }
    }

    if (!(*brick_count)) {
        ret = dict_get_int32(dict, "count", &(*brick_count));
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_DICT_GET_FAILED,
                   "Bricks check : Could not "
                   "retrieve brick count");
            goto out;
        }
    }

    brick_list_dup = brick_list_ptr = gf_strdup(*brick_list);
    /* Resolve hostnames and get addrinfo */
    while (i < *brick_count) {
        ++i;
        brick = strtok_r(brick_list_dup, " \n", &tmpptr);
        brick_list_dup = tmpptr;
        if (brick == NULL)
            goto check_failed;
        tmpptr = strrchr(brick, ':');
        if (tmpptr == NULL)
            goto check_failed;
        addrlen = strlen(brick) - strlen(tmpptr);
        strncpy(brick_addr, brick, sizeof(brick_addr));
        brick_addr[addrlen] = '\0';
        ret = getaddrinfo(brick_addr, NULL, NULL, &ai_info);
        if (ret != 0) {
            ret = 0;
            gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_HOSTNAME_RESOLVE_FAIL,
                   "unable to resolve host name for addr %s", brick_addr);
            goto out;
        }
        ai_list_tmp1 = MALLOC(sizeof(addrinfo_list_t));
        if (ai_list_tmp1 == NULL) {
            ret = 0;
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, GD_MSG_NO_MEMORY,
                   "failed to allocate "
                   "memory");
            freeaddrinfo(ai_info);
            goto out;
        }
        ai_list_tmp1->info = ai_info;
        cds_list_add_tail(&ai_list_tmp1->list, &ai_list->list);
        ai_list_tmp1 = NULL;
    }

    i = 0;
    ai_list_tmp1 = cds_list_entry(ai_list->list.next, addrinfo_list_t, list);

    if (*brick_count < sub_count) {
        sub_count = *brick_count;
    }
    /* Check for bad brick order for brick list provided in add-brick command
     * List1 = [hn1, hn2, hn3, hn4] -> Passed brick order check
     * List2 = [hn1, hn2, hn1]  -> Failed brick order check
     * Comparison to check all hostnames in List are unique or not for replica
     * and disperse subvol set.
     * */
    while (i < *brick_count) {
        ++i;
        ai_info = ai_list_tmp1->info;
        ai_list_tmp1 = cds_list_entry(ai_list_tmp1->list.next, addrinfo_list_t,
                                      list);
        if (0 == i % sub_count) {
            j = 0;
            continue;
        }
        ai_list_tmp2 = ai_list_tmp1;
        k = j;
        while (k < sub_count - 1) {
            ++k;
            ret = glusterd_compare_addrinfo(ai_info, ai_list_tmp2->info);
            if (GF_AI_COMPARE_ERROR == ret)
                goto check_failed;
            if (GF_AI_COMPARE_MATCH == ret)
                goto found_bad_brick_order;
            ai_list_tmp2 = cds_list_entry(ai_list_tmp2->list.next,
                                          addrinfo_list_t, list);
        }
        ++j;
    }

    /* Check for bad brick order for brick list present already in the volume
     * with the bricks provided in add-brick command.
     * pre_list = [pn1, pn2, pn3]
     * ai_list = [pn4, pn1]
     * Failed as pn1 is present in new_list for replica subvols
     */
    ai_list_tmp2 = NULL;
    if (flag) {
        i = 0;
        ai_list_tmp2 = cds_list_first_entry(ai_list->list.next, addrinfo_list_t,
                                            list);
        while (i < count) {
            cds_list_for_each_entry(pre_list_tmp1, &pre_list->list, list)
            {
                ret = glusterd_compare_addrinfo(ai_list_tmp2->info,
                                                pre_list_tmp1->info);
                if (GF_AI_COMPARE_ERROR == ret)
                    goto check_failed;
                if (GF_AI_COMPARE_MATCH == ret)
                    goto found_bad_brick_order;
            }
            ai_list_tmp2 = cds_list_entry(ai_list_tmp2->list.next,
                                          addrinfo_list_t, list);
            ++i;
        }
    }

    gf_msg_debug(this->name, 0, "Brick order okay");
    ret = 0;
    goto out;

check_failed:
    gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_BAD_BRKORDER_CHECK_FAIL,
           "Failed bad brick order check");
    snprintf(err_str, sizeof(failed_string), failed_string);
    ret = -1;
    goto out;

found_bad_brick_order:
    gf_msg(this->name, GF_LOG_INFO, 0, GD_MSG_BAD_BRKORDER,
           "Bad brick order found");
    if (type == GF_CLUSTER_TYPE_DISPERSE) {
        snprintf(err_str, sizeof(found_string), found_string, "disperse");
    } else {
        snprintf(err_str, sizeof(found_string), found_string, "replicate");
    }

    ret = -1;
out:
    GF_FREE(brick_list_ptr);
    if (pre_list != NULL) {
        cds_list_for_each_entry_safe(pre_list_tmp1, pre_list_tmp2,
                                     &pre_list->list, list)
        {
            if (pre_list_tmp1->info)
                freeaddrinfo(pre_list_tmp1->info);
            FREE(pre_list_tmp1);
        }
        FREE(pre_list);
    }

    if (ai_list != NULL) {
        cds_list_for_each_entry_safe(ai_list_tmp1, ai_list_tmp2, &ai_list->list,
                                     list)
        {
            if (ai_list_tmp1->info)
                freeaddrinfo(ai_list_tmp1->info);
            FREE(ai_list_tmp1);
        }
        FREE(ai_list);
    }

    gf_msg_debug("glusterd", 0, "Returning %d", ret);
    return ret;
}

static gf_boolean_t
search_peer_in_auth_list(char *peer_hostname, char *auth_allow_list)
{
    if (strstr(auth_allow_list, peer_hostname)) {
        return _gf_true;
    }

    return _gf_false;
}

/* glusterd_add_peers_to_auth_list() adds peers into auth.allow list
 * if auth.allow list is not empty. This is called for add-brick and
 * replica brick operations to avoid failing the temporary mount. New
 * volfiles will be generated and clients are notified reg new volfiles.
 */
void
glusterd_add_peers_to_auth_list(char *volname)
{
    int ret = 0;
    glusterd_volinfo_t *volinfo = NULL;
    glusterd_peerinfo_t *peerinfo = NULL;
    xlator_t *this = THIS;
    glusterd_conf_t *conf = NULL;
    int32_t len = 0;
    char *ptr = NULL;
    char *auth_allow_list = NULL;
    char *new_auth_allow_list = NULL;

    conf = this->private;
    GF_ASSERT(conf);

    GF_VALIDATE_OR_GOTO(this->name, volname, out);

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
               "Unable to find volume: %s", volname);
        goto out;
    }

    ret = dict_get_str(volinfo->dict, "auth.allow", &auth_allow_list);
    if (ret) {
        gf_msg(this->name, GF_LOG_INFO, -ret, GD_MSG_DICT_GET_FAILED,
               "auth allow list is not set");
        goto out;
    }

    /* Here calculates length of the new buffer.
     * It is prepared for the worst-case scenario where every peeers
     * were not in the original auth.allow list and needed to be added
     */
    len = strlen(auth_allow_list) + 1;
    cds_list_for_each_entry_rcu(peerinfo, &conf->peers, uuid_list)
    {
        /* Each additional peer needs a comma and the name of it */
        len += 1 + strlen(peerinfo->hostname);
    }

    new_auth_allow_list = GF_CALLOC(1, len, gf_common_mt_char);

    strncat(new_auth_allow_list, auth_allow_list, len);
    ptr = new_auth_allow_list + strlen(auth_allow_list);

    cds_list_for_each_entry_rcu(peerinfo, &conf->peers, uuid_list)
    {
        ret = search_peer_in_auth_list(peerinfo->hostname, new_auth_allow_list);
        if (!ret) {
            gf_log(this->name, GF_LOG_DEBUG,
                   "peer %s not found in auth.allow list", peerinfo->hostname);
            ptr += snprintf(ptr, len - (ptr - new_auth_allow_list), ",%s",
                            peerinfo->hostname);
        }
    }
    if (strcmp(new_auth_allow_list, auth_allow_list) != 0) {
        /* In case, new_auth_allow_list is not same as auth_allow_list,
         * we need to update the volinfo->dict with new_auth_allow_list.
         * we delete the auth_allow_list and replace it with
         * new_auth_allow_list. for reverting the changes in post commit, we
         * keep the copy of auth_allow_list as old_auth_allow_list in
         * volinfo->dict.
         */
        ret = dict_set_dynstr_with_alloc(volinfo->dict, "old.auth.allow",
                                         auth_allow_list);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                   "Unable to set old.auth.allow list");
            goto out;
        }
        ret = dict_set_dynstr(volinfo->dict, "auth.allow", new_auth_allow_list);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
                   "Unable to set new auth.allow list");
            goto out;
        }
        new_auth_allow_list = NULL;
        ret = glusterd_create_volfiles_and_notify_services(volinfo);
        if (ret) {
            gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_VOLFILE_CREATE_FAIL,
                   "failed to create volfiles");
            goto out;
        }
    }
out:
    GF_FREE(new_auth_allow_list);
    return;
}

int
glusterd_replace_old_auth_allow_list(char *volname)
{
    int ret = 0;
    glusterd_volinfo_t *volinfo = NULL;
    xlator_t *this = THIS;
    char *old_auth_allow_list = NULL;

    GF_VALIDATE_OR_GOTO(this->name, volname, out);

    ret = glusterd_volinfo_find(volname, &volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_VOL_NOT_FOUND,
               "Unable to find volume: %s", volname);
        goto out;
    }

    ret = dict_get_str(volinfo->dict, "old.auth.allow", &old_auth_allow_list);
    if (ret) {
        gf_msg(this->name, GF_LOG_INFO, -ret, GD_MSG_DICT_GET_FAILED,
               "old auth allow list is not set, no need to replace the list");
        ret = 0;
        goto out;
    }

    ret = dict_set_dynstr_with_alloc(volinfo->dict, "auth.allow",
                                     old_auth_allow_list);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, GD_MSG_DICT_SET_FAILED,
               "Unable to replace auth.allow list");
        goto out;
    }

    dict_del_sizen(volinfo->dict, "old.auth.allow");

    ret = glusterd_create_volfiles_and_notify_services(volinfo);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_VOLFILE_CREATE_FAIL,
               "failed to create volfiles");
        goto out;
    }
    ret = glusterd_store_volinfo(volinfo, GLUSTERD_VOLINFO_VER_AC_INCREMENT);
    if (ret) {
        gf_msg(this->name, GF_LOG_WARNING, 0, GD_MSG_VOLINFO_STORE_FAIL,
               "failed to store volinfo");
        goto out;
    }
out:
    return ret;
}

/* This function will update the file-system label of the
 * backend snapshot brick.
 *
 * @param brick_path     brick path of the snap volume
 * @param fstype         brick fstype of the snap volume
 * @param device_path    brick device path of the snap volume
 *
 * @return 0 on success and -1 on failure
 */
int
glusterd_update_fs_label(char *brick_path, char *fstype, char *device_path)
{
    int32_t ret = -1;
    char msg[PATH_MAX] = "";
    char label[NAME_MAX] = "";
    uuid_t uuid = {
        0,
    };
    runner_t runner = {
        0,
    };
    xlator_t *this = THIS;
    int32_t len = 0;

    GF_ASSERT(brick_path);
    GF_ASSERT(fstype);

    /* Generate a new UUID */
    gf_uuid_generate(uuid);

    GLUSTERD_GET_UUID_NOHYPHEN(label, uuid);

    runinit(&runner);

    /* Call the file-system specific tools to update the file-system
     * label. Currently we are only supporting xfs and ext2/ext3/ext4
     * file-system.
     */
    if (0 == strcmp(fstype, "xfs")) {
        /* XFS label is of size 12. Therefore we should truncate the
         * label to 12 bytes*/
        label[12] = '\0';
        len = snprintf(msg, sizeof(msg),
                       "Changing filesystem label "
                       "of %s brick to %s",
                       brick_path, label);
        if (len < 0) {
            strcpy(msg, "<error>");
        }
        /* Run the run xfs_admin tool to change the label
         * of the file-system */
        runner_add_args(&runner, "xfs_admin", "-L", label, device_path, NULL);
    } else if (0 == strcmp(fstype, "ext4") || 0 == strcmp(fstype, "ext3") ||
               0 == strcmp(fstype, "ext2")) {
        /* Ext2/Ext3/Ext4 label is of size 16. Therefore we should
         * truncate the label to 16 bytes*/
        label[16] = '\0';
        len = snprintf(msg, sizeof(msg),
                       "Changing filesystem label "
                       "of %s brick to %s",
                       brick_path, label);
        if (len < 0) {
            strcpy(msg, "<error>");
        }
        /* For ext2/ext3/ext4 run tune2fs to change the
         * file-system label */
        runner_add_args(&runner, "tune2fs", "-L", label, device_path, NULL);
    } else {
        gf_msg(this->name, GF_LOG_WARNING, EOPNOTSUPP, GD_MSG_OP_UNSUPPORTED,
               "Changing file-system "
               "label of %s file-system is not supported as of now",
               fstype);
        runner_end(&runner);
        ret = -1;
        goto out;
    }

    runner_log(&runner, this->name, GF_LOG_DEBUG, msg);
    ret = runner_run(&runner);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, 0, GD_MSG_FS_LABEL_UPDATE_FAIL,
               "Failed to change "
               "filesystem label of %s brick to %s",
               brick_path, label);
        goto out;
    }

    ret = 0;
out:
    return ret;
}
