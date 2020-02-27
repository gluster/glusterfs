/*
  Copyright (c) 2010-2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "server.h"
#include "server-helpers.h"
#include <glusterfs/gidcache.h>
#include "server-messages.h"
#include <glusterfs/syscall.h>
#include <glusterfs/defaults.h>
#include <glusterfs/default-args.h>
#include "server-common.h"

#include <fnmatch.h>
#include <pwd.h>

/* based on nfs_fix_aux_groups() */
int
gid_resolve(server_conf_t *conf, call_stack_t *root)
{
    int ret = 0;
    struct passwd mypw;
    char mystrs[1024];
    struct passwd *result;
    gid_t *mygroups = NULL;
    gid_list_t gl;
    int ngroups;
    const gid_list_t *agl;

    agl = gid_cache_lookup(&conf->gid_cache, root->uid, 0, 0);
    if (agl) {
        root->ngrps = agl->gl_count;

        if (root->ngrps > 0) {
            ret = call_stack_alloc_groups(root, agl->gl_count);
            if (ret == 0) {
                memcpy(root->groups, agl->gl_list,
                       sizeof(gid_t) * agl->gl_count);
            }
        }

        gid_cache_release(&conf->gid_cache, agl);

        return ret;
    }

    ret = getpwuid_r(root->uid, &mypw, mystrs, sizeof(mystrs), &result);
    if (ret != 0) {
        gf_smsg("gid-cache", GF_LOG_ERROR, errno, PS_MSG_GET_UID_FAILED,
                "uid=%u", root->uid, NULL);
        return -1;
    }

    if (!result) {
        gf_smsg("gid-cache", GF_LOG_ERROR, 0, PS_MSG_UID_NOT_FOUND, "uid=%u",
                root->uid, NULL);
        return -1;
    }

    gf_msg_trace("gid-cache", 0, "mapped %u => %s", root->uid, result->pw_name);

    ngroups = gf_getgrouplist(result->pw_name, root->gid, &mygroups);
    if (ngroups == -1) {
        gf_smsg("gid-cache", GF_LOG_ERROR, 0, PS_MSG_MAPPING_ERROR,
                "pw_name=%s", result->pw_name, "root->ngtps=%d", root->ngrps,
                NULL);
        return -1;
    }
    root->ngrps = (uint16_t)ngroups;

    /* setup a full gid_list_t to add it to the gid_cache */
    gl.gl_id = root->uid;
    gl.gl_uid = root->uid;
    gl.gl_gid = root->gid;
    gl.gl_count = root->ngrps;

    gl.gl_list = GF_MALLOC(root->ngrps * sizeof(gid_t), gf_common_mt_groups_t);
    if (gl.gl_list)
        memcpy(gl.gl_list, mygroups, sizeof(gid_t) * root->ngrps);
    else {
        GF_FREE(mygroups);
        return -1;
    }

    if (root->ngrps > 0) {
        call_stack_set_groups(root, root->ngrps, &mygroups);
    }

    if (gid_cache_add(&conf->gid_cache, &gl) != 1)
        GF_FREE(gl.gl_list);

    return ret;
}

int
server_resolve_groups(call_frame_t *frame, rpcsvc_request_t *req)
{
    xlator_t *this = NULL;
    server_conf_t *conf = NULL;

    GF_VALIDATE_OR_GOTO("server", frame, out);
    GF_VALIDATE_OR_GOTO("server", req, out);

    this = req->trans->xl;
    conf = this->private;

    return gid_resolve(conf, frame->root);
out:
    return -1;
}

int
server_decode_groups(call_frame_t *frame, rpcsvc_request_t *req)
{
    int i = 0;

    GF_VALIDATE_OR_GOTO("server", frame, out);
    GF_VALIDATE_OR_GOTO("server", req, out);

    if (call_stack_alloc_groups(frame->root, req->auxgidcount) != 0)
        return -1;

    frame->root->ngrps = req->auxgidcount;
    if (frame->root->ngrps == 0)
        return 0;

    /* ngrps cannot be bigger than USHRT_MAX(65535) */
    if (frame->root->ngrps > GF_MAX_AUX_GROUPS)
        return -1;

    for (; i < frame->root->ngrps; ++i)
        frame->root->groups[i] = req->auxgids[i];
out:
    return 0;
}

void
server_loc_wipe(loc_t *loc)
{
    if (loc->parent) {
        inode_unref(loc->parent);
        loc->parent = NULL;
    }

    if (loc->inode) {
        inode_unref(loc->inode);
        loc->inode = NULL;
    }

    GF_FREE((void *)loc->path);
}

void
server_resolve_wipe(server_resolve_t *resolve)
{
    GF_FREE((void *)resolve->path);

    GF_FREE((void *)resolve->bname);

    loc_wipe(&resolve->resolve_loc);
}

void
free_state(server_state_t *state)
{
    if (state->fd) {
        fd_unref(state->fd);
        state->fd = NULL;
    }

    if (state->params) {
        dict_unref(state->params);
        state->params = NULL;
    }

    if (state->iobref) {
        iobref_unref(state->iobref);
        state->iobref = NULL;
    }

    if (state->iobuf) {
        iobuf_unref(state->iobuf);
        state->iobuf = NULL;
    }

    if (state->dict) {
        dict_unref(state->dict);
        state->dict = NULL;
    }

    if (state->xdata) {
        dict_unref(state->xdata);
        state->xdata = NULL;
    }

    GF_FREE((void *)state->volume);

    GF_FREE((void *)state->name);

    server_loc_wipe(&state->loc);
    server_loc_wipe(&state->loc2);

    server_resolve_wipe(&state->resolve);
    server_resolve_wipe(&state->resolve2);

    /* Call rpc_trnasport_unref to avoid crashes at last after free
       all resources because of server_rpc_notify (for transport destroy)
       call's xlator_mem_cleanup if all xprt are destroyed that internally
       call's inode_table_destroy.
    */
    if (state->xprt) {
        rpc_transport_unref(state->xprt);
        state->xprt = NULL;
    }

    GF_FREE(state);
}

static int
server_connection_cleanup_flush_cbk(call_frame_t *frame, void *cookie,
                                    xlator_t *this, int32_t op_ret,
                                    int32_t op_errno, dict_t *xdata)
{
    int32_t ret = -1;
    fd_t *fd = NULL;
    client_t *client = NULL;
    uint64_t fd_cnt = 0;
    xlator_t *victim = NULL;
    server_conf_t *conf = NULL;
    xlator_t *serv_xl = NULL;
    rpc_transport_t *xprt = NULL;
    rpc_transport_t *xp_next = NULL;
    int32_t detach = (long)cookie;
    gf_boolean_t xprt_found = _gf_false;

    GF_VALIDATE_OR_GOTO("server", this, out);
    GF_VALIDATE_OR_GOTO("server", frame, out);

    fd = frame->local;
    client = frame->root->client;
    serv_xl = frame->this;
    conf = serv_xl->private;

    fd_unref(fd);
    frame->local = NULL;

    if (client)
        victim = client->bound_xl;

    if (victim) {
        fd_cnt = GF_ATOMIC_DEC(client->fd_cnt);
        if (!fd_cnt && conf && detach) {
            pthread_mutex_lock(&conf->mutex);
            {
                list_for_each_entry_safe(xprt, xp_next, &conf->xprt_list, list)
                {
                    if (!xprt->xl_private)
                        continue;
                    if (xprt->xl_private == client) {
                        xprt_found = _gf_true;
                        break;
                    }
                }
            }
            pthread_mutex_unlock(&conf->mutex);
            if (xprt_found) {
                rpc_transport_unref(xprt);
            }
        }
    }

    gf_client_unref(client);
    STACK_DESTROY(frame->root);

    ret = 0;
out:
    return ret;
}

static int
do_fd_cleanup(xlator_t *this, client_t *client, fdentry_t *fdentries,
              int fd_count, int32_t detach)
{
    fd_t *fd = NULL;
    int i = 0, ret = -1;
    call_frame_t *tmp_frame = NULL;
    xlator_t *bound_xl = NULL;
    char *path = NULL;

    GF_VALIDATE_OR_GOTO("server", this, out);
    GF_VALIDATE_OR_GOTO("server", fdentries, out);

    bound_xl = client->bound_xl;

    for (i = 0; i < fd_count; i++) {
        fd = fdentries[i].fd;

        if (fd != NULL) {
            tmp_frame = create_frame(this, this->ctx->pool);
            if (tmp_frame == NULL) {
                goto out;
            }

            tmp_frame->root->type = GF_OP_TYPE_FOP;
            GF_ASSERT(fd->inode);

            ret = inode_path(fd->inode, NULL, &path);

            if (ret > 0) {
                gf_smsg(this->name, GF_LOG_INFO, 0, PS_MSG_FD_CLEANUP,
                        "path=%s", path, NULL);
                GF_FREE(path);
            } else {
                gf_smsg(this->name, GF_LOG_INFO, 0, PS_MSG_FD_CLEANUP,
                        "inode-gfid=%s", uuid_utoa(fd->inode->gfid), NULL);
            }

            tmp_frame->local = fd;
            tmp_frame->root->pid = 0;
            gf_client_ref(client);
            tmp_frame->root->client = client;
            memset(&tmp_frame->root->lk_owner, 0, sizeof(gf_lkowner_t));

            STACK_WIND_COOKIE(tmp_frame, server_connection_cleanup_flush_cbk,
                              (void *)(long)detach, bound_xl,
                              bound_xl->fops->flush, fd, NULL);
        }
    }

    GF_FREE(fdentries);
    ret = 0;

out:
    return ret;
}

int
server_connection_cleanup(xlator_t *this, client_t *client, int32_t flags,
                          gf_boolean_t *fd_exist)
{
    server_ctx_t *serv_ctx = NULL;
    fdentry_t *fdentries = NULL;
    uint32_t fd_count = 0;
    int cd_ret = 0;
    int ret = 0;
    xlator_t *bound_xl = NULL;
    int i = 0;
    fd_t *fd = NULL;
    uint64_t fd_cnt = 0;
    int32_t detach = 0;

    GF_VALIDATE_OR_GOTO("server", this, out);
    GF_VALIDATE_OR_GOTO(this->name, client, out);
    GF_VALIDATE_OR_GOTO(this->name, flags, out);

    serv_ctx = server_ctx_get(client, client->this);

    if (serv_ctx == NULL) {
        gf_smsg(this->name, GF_LOG_INFO, 0, PS_MSG_SERVER_CTX_GET_FAILED, NULL);
        goto out;
    }

    LOCK(&serv_ctx->fdtable_lock);
    {
        if (serv_ctx->fdtable && (flags & POSIX_LOCKS))
            fdentries = gf_fd_fdtable_get_all_fds(serv_ctx->fdtable, &fd_count);
    }
    UNLOCK(&serv_ctx->fdtable_lock);

    if (client->bound_xl == NULL)
        goto out;

    if (flags & INTERNAL_LOCKS) {
        cd_ret = gf_client_disconnect(client);
    }

    if (fdentries != NULL) {
        /* Loop to configure fd_count on victim brick */
        bound_xl = client->bound_xl;
        if (bound_xl) {
            for (i = 0; i < fd_count; i++) {
                fd = fdentries[i].fd;
                if (!fd)
                    continue;
                fd_cnt++;
            }
            if (fd_cnt) {
                if (fd_exist)
                    (*fd_exist) = _gf_true;
                GF_ATOMIC_ADD(client->fd_cnt, fd_cnt);
            }
        }

        /* If fd_exist is not NULL it means function is invoke
           by server_rpc_notify at the time of getting DISCONNECT
           notification
        */
        if (fd_exist)
            detach = 1;

        gf_msg_debug(this->name, 0,
                     "Performing cleanup on %d "
                     "fdentries",
                     fd_count);
        ret = do_fd_cleanup(this, client, fdentries, fd_count, detach);
    } else
        gf_smsg(this->name, GF_LOG_INFO, 0, PS_MSG_FDENTRY_NULL, NULL);

    if (cd_ret || ret)
        ret = -1;

out:
    return ret;
}

static call_frame_t *
server_alloc_frame(rpcsvc_request_t *req)
{
    call_frame_t *frame = NULL;
    server_state_t *state = NULL;
    client_t *client = NULL;

    GF_VALIDATE_OR_GOTO("server", req, out);
    GF_VALIDATE_OR_GOTO("server", req->trans, out);
    GF_VALIDATE_OR_GOTO("server", req->svc, out);
    GF_VALIDATE_OR_GOTO("server", req->svc->ctx, out);

    client = req->trans->xl_private;
    GF_VALIDATE_OR_GOTO("server", client, out);

    frame = create_frame(client->this, req->svc->ctx->pool);
    if (!frame)
        goto out;

    frame->root->type = GF_OP_TYPE_FOP;
    state = GF_CALLOC(1, sizeof(*state), gf_server_mt_state_t);
    if (!state)
        goto out;

    if (client->bound_xl)
        state->itable = client->bound_xl->itable;

    state->xprt = rpc_transport_ref(req->trans);
    state->resolve.fd_no = -1;
    state->resolve2.fd_no = -1;

    frame->root->client = client;
    frame->root->state = state; /* which socket */

    frame->this = client->this;
out:
    return frame;
}

call_frame_t *
get_frame_from_request(rpcsvc_request_t *req)
{
    call_frame_t *frame = NULL;
    client_t *client = NULL;
    client_t *tmp_client = NULL;
    xlator_t *this = NULL;
    server_conf_t *priv = NULL;
    clienttable_t *clienttable = NULL;
    unsigned int i = 0;
    rpc_transport_t *trans = NULL;
    server_state_t *state = NULL;

    GF_VALIDATE_OR_GOTO("server", req, out);

    frame = server_alloc_frame(req);
    if (!frame)
        goto out;

    frame->root->op = req->procnum;

    client = req->trans->xl_private;
    this = req->trans->xl;
    priv = this->private;
    clienttable = this->ctx->clienttable;

    for (i = 0; i < clienttable->max_clients; i++) {
        tmp_client = clienttable->cliententries[i].client;
        if (client == tmp_client) {
            /* for non trusted clients username and password
               would not have been set. So for non trusted clients
               (i.e clients not from the same machine as the brick,
               and clients from outside the storage pool)
               do the root-squashing and all-squashing.
               TODO: If any client within the storage pool (i.e
               mounting within a machine from the pool but using
               other machine's ip/hostname from the same pool)
               is present treat it as a trusted client
            */
            if (!client->auth.username && req->pid != NFS_PID) {
                RPC_AUTH_ROOT_SQUASH(req);
                RPC_AUTH_ALL_SQUASH(req);
            }

            /* Problem: If we just check whether the client is
               trusted client and do not do root squashing and
               all squashing for them, then for smb clients and
               UFO clients root squashing and all squashing will
               never happen as they use the fuse mounts done within
               the trusted pool (i.e they are trusted clients).
               Solution: To fix it, do root squashing and all squashing
               for trusted clients also. If one wants to have a client
               within the storage pool for which root-squashing does
               not happen, then the client has to be mounted with
               --no-root-squash option. But for defrag client and
               gsyncd client do not do root-squashing and all-squashing.
            */
            if (client->auth.username &&
                req->pid != GF_CLIENT_PID_NO_ROOT_SQUASH &&
                req->pid != GF_CLIENT_PID_GSYNCD &&
                req->pid != GF_CLIENT_PID_DEFRAG &&
                req->pid != GF_CLIENT_PID_SELF_HEALD &&
                req->pid != GF_CLIENT_PID_QUOTA_MOUNT) {
                RPC_AUTH_ROOT_SQUASH(req);
                RPC_AUTH_ALL_SQUASH(req);
            }

            /* For nfs clients the server processes will be running
               within the trusted storage pool machines. So if we
               do not do root-squashing and all-squashing for nfs
               servers, thinking that its a trusted client, then
               root-squashing and all-squashing won't work for nfs
               clients.
            */
            if (req->pid == NFS_PID) {
                RPC_AUTH_ROOT_SQUASH(req);
                RPC_AUTH_ALL_SQUASH(req);
            }
        }
    }

    /* Add a ref for this fop */
    if (client)
        gf_client_ref(client);

    frame->root->uid = req->uid;
    frame->root->gid = req->gid;
    frame->root->pid = req->pid;
    frame->root->client = client;
    frame->root->lk_owner = req->lk_owner;

    if (priv->server_manage_gids)
        server_resolve_groups(frame, req);
    else
        server_decode_groups(frame, req);
    trans = req->trans;
    if (trans) {
        memcpy(&frame->root->identifier, trans->peerinfo.identifier,
               sizeof(trans->peerinfo.identifier));
    }

    /* more fields, for the clients which are 3.x series this will be 0 */
    frame->root->flags = req->flags;
    frame->root->ctime = req->ctime;

    frame->local = req;

    state = CALL_STATE(frame);
    state->client = client;
out:
    return frame;
}

int
server_build_config(xlator_t *this, server_conf_t *conf)
{
    data_t *data = NULL;
    int ret = -1;
    struct stat buf = {
        0,
    };

    GF_VALIDATE_OR_GOTO("server", this, out);
    GF_VALIDATE_OR_GOTO("server", conf, out);

    ret = dict_get_int32(this->options, "inode-lru-limit",
                         &conf->inode_lru_limit);
    if (ret < 0) {
        conf->inode_lru_limit = 16384;
    }

    conf->verify_volfile = 1;
    data = dict_get(this->options, "verify-volfile-checksum");
    if (data) {
        ret = gf_string2boolean(data->data, &conf->verify_volfile);
        if (ret != 0) {
            gf_smsg(this->name, GF_LOG_WARNING, EINVAL, PS_MSG_WRONG_VALUE,
                    NULL);
        }
    }

    data = dict_get(this->options, "trace");
    if (data) {
        ret = gf_string2boolean(data->data, &conf->trace);
        if (ret != 0) {
            gf_smsg(this->name, GF_LOG_WARNING, EINVAL, PS_MSG_INVALID_ENTRY,
                    NULL);
        }
    }

    /* TODO: build_rpc_config (); */
    ret = dict_get_int32(this->options, "limits.transaction-size",
                         &conf->rpc_conf.max_block_size);
    if (ret < 0) {
        gf_msg_trace(this->name, 0,
                     "defaulting limits.transaction-"
                     "size to %d",
                     DEFAULT_BLOCK_SIZE);
        conf->rpc_conf.max_block_size = DEFAULT_BLOCK_SIZE;
    }

    data = dict_get(this->options, "config-directory");
    if (data) {
        /* Check whether the specified directory exists,
           or directory specified is non standard */
        ret = sys_stat(data->data, &buf);
        if ((ret != 0) || !S_ISDIR(buf.st_mode)) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, PS_MSG_DIR_NOT_FOUND,
                    "data=%s", data->data, NULL);
            ret = -1;
            goto out;
        }
        /* Make sure that conf-dir doesn't contain ".." in path */
        if ((gf_strstr(data->data, "/", "..")) == -1) {
            ret = -1;
            gf_smsg(this->name, GF_LOG_ERROR, 0, PS_MSG_CONF_DIR_INVALID,
                    "data=%s", data->data, NULL);
            goto out;
        }

        conf->conf_dir = gf_strdup(data->data);
    }
    ret = 0;
out:
    return ret;
}

void
print_caller(char *str, int size, call_frame_t *frame)
{
    server_state_t *state = NULL;

    GF_VALIDATE_OR_GOTO("server", str, out);
    GF_VALIDATE_OR_GOTO("server", frame, out);

    state = CALL_STATE(frame);

    snprintf(str, size, " Callid=%" PRId64 ", Client=%s", frame->root->unique,
             state->xprt->peerinfo.identifier);

out:
    return;
}

void
server_print_resolve(char *str, int size, server_resolve_t *resolve)
{
    int filled = 0;

    GF_VALIDATE_OR_GOTO("server", str, out);

    if (!resolve) {
        snprintf(str, size, "<nul>");
        return;
    }

    filled += snprintf(str + filled, size - filled, " Resolve={");
    if (resolve->fd_no != -1)
        filled += snprintf(str + filled, size - filled, "fd=%" PRId64 ",",
                           (uint64_t)resolve->fd_no);
    if (resolve->bname)
        filled += snprintf(str + filled, size - filled, "bname=%s,",
                           resolve->bname);
    if (resolve->path)
        filled += snprintf(str + filled, size - filled, "path=%s",
                           resolve->path);

    snprintf(str + filled, size - filled, "}");
out:
    return;
}

void
server_print_loc(char *str, int size, loc_t *loc)
{
    int filled = 0;

    GF_VALIDATE_OR_GOTO("server", str, out);

    if (!loc) {
        snprintf(str, size, "<nul>");
        return;
    }

    filled += snprintf(str + filled, size - filled, " Loc={");

    if (loc->path)
        filled += snprintf(str + filled, size - filled, "path=%s,", loc->path);
    if (loc->inode)
        filled += snprintf(str + filled, size - filled, "inode=%p,",
                           loc->inode);
    if (loc->parent)
        filled += snprintf(str + filled, size - filled, "parent=%p",
                           loc->parent);

    snprintf(str + filled, size - filled, "}");
out:
    return;
}

void
server_print_params(char *str, int size, server_state_t *state)
{
    int filled = 0;

    GF_VALIDATE_OR_GOTO("server", str, out);

    filled += snprintf(str + filled, size - filled, " Params={");

    if (state->fd)
        filled += snprintf(str + filled, size - filled, "fd=%p,", state->fd);
    if (state->valid)
        filled += snprintf(str + filled, size - filled, "valid=%d,",
                           state->valid);
    if (state->flags)
        filled += snprintf(str + filled, size - filled, "flags=%d,",
                           state->flags);
    if (state->wbflags)
        filled += snprintf(str + filled, size - filled, "wbflags=%d,",
                           state->wbflags);
    if (state->size)
        filled += snprintf(str + filled, size - filled, "size=%zu,",
                           state->size);
    if (state->offset)
        filled += snprintf(str + filled, size - filled, "offset=%" PRId64 ",",
                           state->offset);
    if (state->cmd)
        filled += snprintf(str + filled, size - filled, "cmd=%d,", state->cmd);
    if (state->type)
        filled += snprintf(str + filled, size - filled, "type=%d,",
                           state->type);
    if (state->name)
        filled += snprintf(str + filled, size - filled, "name=%s,",
                           state->name);
    if (state->mask)
        filled += snprintf(str + filled, size - filled, "mask=%d,",
                           state->mask);
    if (state->volume)
        filled += snprintf(str + filled, size - filled, "volume=%s,",
                           state->volume);

/* FIXME
        snprintf (str + filled, size - filled,
                  "bound_xl=%s}", state->client->bound_xl->name);
*/
out:
    return;
}

int
server_resolve_is_empty(server_resolve_t *resolve)
{
    if (resolve->fd_no != -1)
        return 0;

    if (resolve->path != 0)
        return 0;

    if (resolve->bname != 0)
        return 0;

    return 1;
}

void
server_print_reply(call_frame_t *frame, int op_ret, int op_errno)
{
    server_conf_t *conf = NULL;
    server_state_t *state = NULL;
    xlator_t *this = NULL;
    char caller[512];
    char fdstr[32];
    char *op = "UNKNOWN";

    GF_VALIDATE_OR_GOTO("server", frame, out);

    this = frame->this;
    conf = this->private;

    GF_VALIDATE_OR_GOTO("server", conf, out);
    GF_VALIDATE_OR_GOTO("server", conf->trace, out);

    state = CALL_STATE(frame);

    print_caller(caller, 256, frame);

    switch (frame->root->type) {
        case GF_OP_TYPE_FOP:
            op = (char *)gf_fop_list[frame->root->op];
            break;
        default:
            op = "";
    }

    fdstr[0] = '\0';
    if (state->fd)
        snprintf(fdstr, 32, " fd=%p", state->fd);

    gf_smsg(this->name, GF_LOG_INFO, op_errno, PS_MSG_SERVER_MSG, "op=%s", op,
            "caller=%s", caller, "op_ret=%d", op_ret, "op_errno=%d", op_errno,
            "fdstr=%s", fdstr, NULL);
out:
    return;
}

void
server_print_request(call_frame_t *frame)
{
    server_conf_t *conf = NULL;
    xlator_t *this = NULL;
    server_state_t *state = NULL;
    char *op = "UNKNOWN";
    char resolve_vars[256];
    char resolve2_vars[256];
    char loc_vars[256];
    char loc2_vars[256];
    char other_vars[512];
    char caller[512];

    GF_VALIDATE_OR_GOTO("server", frame, out);

    this = frame->this;
    conf = this->private;

    GF_VALIDATE_OR_GOTO("server", conf, out);

    if (!conf->trace)
        goto out;

    state = CALL_STATE(frame);

    memset(resolve_vars, '\0', 256);
    memset(resolve2_vars, '\0', 256);
    memset(loc_vars, '\0', 256);
    memset(loc2_vars, '\0', 256);
    memset(other_vars, '\0', 256);

    print_caller(caller, 256, frame);

    if (!server_resolve_is_empty(&state->resolve)) {
        server_print_resolve(resolve_vars, 256, &state->resolve);
        server_print_loc(loc_vars, 256, &state->loc);
    }

    if (!server_resolve_is_empty(&state->resolve2)) {
        server_print_resolve(resolve2_vars, 256, &state->resolve2);
        server_print_loc(loc2_vars, 256, &state->loc2);
    }

    server_print_params(other_vars, 512, state);

    switch (frame->root->type) {
        case GF_OP_TYPE_FOP:
            op = (char *)gf_fop_list[frame->root->op];
            break;
        default:
            op = "";
            break;
    }

    gf_smsg(this->name, GF_LOG_INFO, 0, PS_MSG_SERVER_MSG, "op=%s", op,
            "caller=%s", caller, "resolve_vars=%s", resolve_vars, "loc_vars=%s",
            loc_vars, "resolve2_vars=%s", resolve2_vars, "loc2_vars=%s",
            loc2_vars, "other_vars=%s", other_vars, NULL);
out:
    return;
}

int
serialize_rsp_direntp(gf_dirent_t *entries, gfs3_readdirp_rsp *rsp)
{
    gf_dirent_t *entry = NULL;
    gfs3_dirplist *trav = NULL;
    gfs3_dirplist *prev = NULL;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("server", entries, out);
    GF_VALIDATE_OR_GOTO("server", rsp, out);

    list_for_each_entry(entry, &entries->list, list)
    {
        trav = GF_CALLOC(1, sizeof(*trav), gf_server_mt_dirent_rsp_t);
        if (!trav)
            goto out;

        trav->d_ino = entry->d_ino;
        trav->d_off = entry->d_off;
        trav->d_len = entry->d_len;
        trav->d_type = entry->d_type;
        trav->name = entry->d_name;

        gf_stat_from_iatt(&trav->stat, &entry->d_stat);

        /* if 'dict' is present, pack it */
        if (entry->dict) {
            ret = dict_allocate_and_serialize(entry->dict,
                                              (char **)&trav->dict.dict_val,
                                              &trav->dict.dict_len);
            if (ret != 0) {
                gf_smsg(THIS->name, GF_LOG_ERROR, 0, PS_MSG_DICT_SERIALIZE_FAIL,
                        NULL);
                errno = -ret;
                trav->dict.dict_len = 0;
                goto out;
            }
        }

        if (prev)
            prev->nextentry = trav;
        else
            rsp->reply = trav;

        prev = trav;
        trav = NULL;
    }

    ret = 0;
out:
    GF_FREE(trav);

    return ret;
}

int
serialize_rsp_direntp_v2(gf_dirent_t *entries, gfx_readdirp_rsp *rsp)
{
    gf_dirent_t *entry = NULL;
    gfx_dirplist *trav = NULL;
    gfx_dirplist *prev = NULL;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("server", entries, out);
    GF_VALIDATE_OR_GOTO("server", rsp, out);

    list_for_each_entry(entry, &entries->list, list)
    {
        trav = GF_CALLOC(1, sizeof(*trav), gf_server_mt_dirent_rsp_t);
        if (!trav)
            goto out;

        trav->d_ino = entry->d_ino;
        trav->d_off = entry->d_off;
        trav->d_len = entry->d_len;
        trav->d_type = entry->d_type;
        trav->name = entry->d_name;

        gfx_stat_from_iattx(&trav->stat, &entry->d_stat);
        dict_to_xdr(entry->dict, &trav->dict);

        if (prev)
            prev->nextentry = trav;
        else
            rsp->reply = trav;

        prev = trav;
        trav = NULL;
    }

    ret = 0;
out:
    GF_FREE(trav);

    return ret;
}

int
serialize_rsp_dirent(gf_dirent_t *entries, gfs3_readdir_rsp *rsp)
{
    gf_dirent_t *entry = NULL;
    gfs3_dirlist *trav = NULL;
    gfs3_dirlist *prev = NULL;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("server", rsp, out);
    GF_VALIDATE_OR_GOTO("server", entries, out);

    list_for_each_entry(entry, &entries->list, list)
    {
        trav = GF_CALLOC(1, sizeof(*trav), gf_server_mt_dirent_rsp_t);
        if (!trav)
            goto out;
        trav->d_ino = entry->d_ino;
        trav->d_off = entry->d_off;
        trav->d_len = entry->d_len;
        trav->d_type = entry->d_type;
        trav->name = entry->d_name;
        if (prev)
            prev->nextentry = trav;
        else
            rsp->reply = trav;

        prev = trav;
    }

    ret = 0;
out:
    return ret;
}

int
serialize_rsp_dirent_v2(gf_dirent_t *entries, gfx_readdir_rsp *rsp)
{
    gf_dirent_t *entry = NULL;
    gfx_dirlist *trav = NULL;
    gfx_dirlist *prev = NULL;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("server", rsp, out);
    GF_VALIDATE_OR_GOTO("server", entries, out);

    list_for_each_entry(entry, &entries->list, list)
    {
        trav = GF_CALLOC(1, sizeof(*trav), gf_server_mt_dirent_rsp_t);
        if (!trav)
            goto out;
        trav->d_ino = entry->d_ino;
        trav->d_off = entry->d_off;
        trav->d_len = entry->d_len;
        trav->d_type = entry->d_type;
        trav->name = entry->d_name;
        if (prev)
            prev->nextentry = trav;
        else
            rsp->reply = trav;

        prev = trav;
    }

    ret = 0;
out:
    return ret;
}

int
readdir_rsp_cleanup(gfs3_readdir_rsp *rsp)
{
    gfs3_dirlist *prev = NULL;
    gfs3_dirlist *trav = NULL;

    trav = rsp->reply;
    prev = trav;
    while (trav) {
        trav = trav->nextentry;
        GF_FREE(prev);
        prev = trav;
    }

    return 0;
}

int
readdirp_rsp_cleanup(gfs3_readdirp_rsp *rsp)
{
    gfs3_dirplist *prev = NULL;
    gfs3_dirplist *trav = NULL;

    trav = rsp->reply;
    prev = trav;
    while (trav) {
        trav = trav->nextentry;
        GF_FREE(prev->dict.dict_val);
        GF_FREE(prev);
        prev = trav;
    }

    return 0;
}

int
readdir_rsp_cleanup_v2(gfx_readdir_rsp *rsp)
{
    gfx_dirlist *prev = NULL;
    gfx_dirlist *trav = NULL;

    trav = rsp->reply;
    prev = trav;
    while (trav) {
        trav = trav->nextentry;
        GF_FREE(prev);
        prev = trav;
    }

    return 0;
}

int
readdirp_rsp_cleanup_v2(gfx_readdirp_rsp *rsp)
{
    gfx_dirplist *prev = NULL;
    gfx_dirplist *trav = NULL;

    trav = rsp->reply;
    prev = trav;
    while (trav) {
        trav = trav->nextentry;
        GF_FREE(prev->dict.pairs.pairs_val);
        GF_FREE(prev);
        prev = trav;
    }

    return 0;
}

static int
common_rsp_locklist(lock_migration_info_t *locklist, gfs3_locklist **reply)
{
    lock_migration_info_t *tmp = NULL;
    gfs3_locklist *trav = NULL;
    gfs3_locklist *prev = NULL;
    int ret = -1;

    GF_VALIDATE_OR_GOTO("server", locklist, out);

    list_for_each_entry(tmp, &locklist->list, list)
    {
        trav = GF_CALLOC(1, sizeof(*trav), gf_server_mt_lock_mig_t);
        if (!trav)
            goto out;

        switch (tmp->flock.l_type) {
            case F_RDLCK:
                tmp->flock.l_type = GF_LK_F_RDLCK;
                break;
            case F_WRLCK:
                tmp->flock.l_type = GF_LK_F_WRLCK;
                break;
            case F_UNLCK:
                tmp->flock.l_type = GF_LK_F_UNLCK;
                break;

            default:
                gf_smsg(THIS->name, GF_LOG_ERROR, 0, PS_MSG_LOCK_ERROR,
                        "lock_type=%" PRId32, tmp->flock.l_type, NULL);
                break;
        }

        gf_proto_flock_from_flock(&trav->flock, &tmp->flock);

        trav->lk_flags = tmp->lk_flags;

        trav->client_uid = tmp->client_uid;

        if (prev)
            prev->nextentry = trav;
        else
            *reply = trav;

        prev = trav;
        trav = NULL;
    }

    ret = 0;
out:
    GF_FREE(trav);
    return ret;
}

int
serialize_rsp_locklist(lock_migration_info_t *locklist,
                       gfs3_getactivelk_rsp *rsp)
{
    int ret = 0;

    GF_VALIDATE_OR_GOTO("server", rsp, out);
    ret = common_rsp_locklist(locklist, &rsp->reply);
out:
    return ret;
}
int
serialize_rsp_locklist_v2(lock_migration_info_t *locklist,
                          gfx_getactivelk_rsp *rsp)
{
    int ret = 0;

    GF_VALIDATE_OR_GOTO("server", rsp, out);
    ret = common_rsp_locklist(locklist, &rsp->reply);
out:
    return ret;
}

int
getactivelkinfo_rsp_cleanup(gfs3_getactivelk_rsp *rsp)
{
    gfs3_locklist *prev = NULL;
    gfs3_locklist *trav = NULL;

    trav = rsp->reply;
    prev = trav;

    while (trav) {
        trav = trav->nextentry;
        GF_FREE(prev);
        prev = trav;
    }

    return 0;
}
int
getactivelkinfo_rsp_cleanup_v2(gfx_getactivelk_rsp *rsp)
{
    gfs3_locklist *prev = NULL;
    gfs3_locklist *trav = NULL;

    trav = rsp->reply;
    prev = trav;

    while (trav) {
        trav = trav->nextentry;
        GF_FREE(prev);
        prev = trav;
    }

    return 0;
}

int
gf_server_check_getxattr_cmd(call_frame_t *frame, const char *key)
{
    server_conf_t *conf = NULL;
    rpc_transport_t *xprt = NULL;

    conf = frame->this->private;
    if (!conf)
        return 0;

    if (fnmatch("*list*mount*point*", key, 0) == 0) {
        /* list all the client protocol connecting to this process */
        pthread_mutex_lock(&conf->mutex);
        {
            list_for_each_entry(xprt, &conf->xprt_list, list)
            {
                gf_smsg("mount-point-list", GF_LOG_INFO, 0,
                        PS_MSG_MOUNT_PT_FAIL, "identifier=%s",
                        xprt->peerinfo.identifier, NULL);
            }
        }
        pthread_mutex_unlock(&conf->mutex);
    }

    /* Add more options/keys here */

    return 0;
}

int
gf_server_check_setxattr_cmd(call_frame_t *frame, dict_t *dict)
{
    server_conf_t *conf = NULL;
    rpc_transport_t *xprt = NULL;
    uint64_t total_read = 0;
    uint64_t total_write = 0;

    conf = frame->this->private;
    if (!conf || !dict)
        return 0;

    if (dict_foreach_fnmatch(dict, "*io*stat*dump", dict_null_foreach_fn,
                             NULL) > 0) {
        list_for_each_entry(xprt, &conf->xprt_list, list)
        {
            total_read += xprt->total_bytes_read;
            total_write += xprt->total_bytes_write;
        }
        gf_smsg("stats", GF_LOG_INFO, 0, PS_MSG_RW_STAT, "total-read=%" PRIu64,
                total_read, "total-write=%" PRIu64, total_write, NULL);
    }

    return 0;
}

server_ctx_t *
server_ctx_get(client_t *client, xlator_t *xlator)
{
    void *tmp = NULL;
    server_ctx_t *ctx = NULL;
    server_ctx_t *setted_ctx = NULL;

    client_ctx_get(client, xlator, &tmp);

    ctx = tmp;

    if (ctx != NULL)
        goto out;

    ctx = GF_CALLOC(1, sizeof(server_ctx_t), gf_server_mt_server_conf_t);

    if (ctx == NULL)
        goto out;

    ctx->fdtable = gf_fd_fdtable_alloc();

    if (ctx->fdtable == NULL) {
        GF_FREE(ctx);
        ctx = NULL;
        goto out;
    }

    LOCK_INIT(&ctx->fdtable_lock);

    setted_ctx = client_ctx_set(client, xlator, ctx);
    if (ctx != setted_ctx) {
        LOCK_DESTROY(&ctx->fdtable_lock);
        GF_FREE(ctx->fdtable);
        GF_FREE(ctx);
        ctx = setted_ctx;
    }

out:
    return ctx;
}

int
auth_set_username_passwd(dict_t *input_params, dict_t *config_params,
                         client_t *client)
{
    int ret = 0;
    data_t *allow_user = NULL;
    data_t *passwd_data = NULL;
    char *username = NULL;
    char *password = NULL;
    char *brick_name = NULL;
    char *searchstr = NULL;
    char *username_str = NULL;
    char *tmp = NULL;
    char *username_cpy = NULL;

    ret = dict_get_str(input_params, "username", &username);
    if (ret) {
        gf_msg_debug("auth/login", 0,
                     "username not found, returning "
                     "DONT-CARE");
        /* For non trusted clients username and password
           will not be there. So don't reject the client.
        */
        ret = 0;
        goto out;
    }

    ret = dict_get_str(input_params, "password", &password);
    if (ret) {
        gf_smsg("auth/login", GF_LOG_WARNING, 0, PS_MSG_PASSWORD_NOT_FOUND,
                NULL);
        goto out;
    }

    ret = dict_get_str(input_params, "remote-subvolume", &brick_name);
    if (ret) {
        gf_smsg("auth/login", GF_LOG_ERROR, 0,
                PS_MSG_REMOTE_SUBVOL_NOT_SPECIFIED, NULL);
        ret = -1;
        goto out;
    }

    ret = gf_asprintf(&searchstr, "auth.login.%s.allow", brick_name);
    if (-1 == ret) {
        ret = 0;
        goto out;
    }

    allow_user = dict_get(config_params, searchstr);
    GF_FREE(searchstr);

    if (allow_user) {
        username_cpy = gf_strdup(allow_user->data);
        if (!username_cpy)
            goto out;

        username_str = strtok_r(username_cpy, " ,", &tmp);

        while (username_str) {
            if (!fnmatch(username_str, username, 0)) {
                ret = gf_asprintf(&searchstr, "auth.login.%s.password",
                                  username);
                if (-1 == ret)
                    goto out;

                passwd_data = dict_get(config_params, searchstr);
                GF_FREE(searchstr);

                if (!passwd_data) {
                    gf_smsg("auth/login", GF_LOG_ERROR, 0, PS_MSG_LOGIN_ERROR,
                            NULL);
                    ret = -1;
                    goto out;
                }

                ret = strcmp(data_to_str(passwd_data), password);
                if (!ret) {
                    client->auth.username = gf_strdup(username);
                    client->auth.passwd = gf_strdup(password);
                } else {
                    gf_smsg("auth/login", GF_LOG_ERROR, 0, PS_MSG_LOGIN_ERROR,
                            "username=%s", username, NULL);
                }
                break;
            }
            username_str = strtok_r(NULL, " ,", &tmp);
        }
    }

out:
    GF_FREE(username_cpy);

    return ret;
}

inode_t *
server_inode_new(inode_table_t *itable, uuid_t gfid)
{
    if (__is_root_gfid(gfid))
        return itable->root;
    else
        return inode_new(itable);
}

int
unserialize_req_locklist(gfs3_setactivelk_req *req, lock_migration_info_t *lmi)
{
    struct gfs3_locklist *trav = NULL;
    lock_migration_info_t *temp = NULL;
    int ret = -1;

    trav = req->request;

    INIT_LIST_HEAD(&lmi->list);

    while (trav) {
        temp = GF_CALLOC(1, sizeof(*lmi), gf_common_mt_lock_mig);
        if (temp == NULL) {
            gf_smsg(THIS->name, GF_LOG_ERROR, 0, PS_MSG_NO_MEM, NULL);
            goto out;
        }

        INIT_LIST_HEAD(&temp->list);

        gf_proto_flock_to_flock(&trav->flock, &temp->flock);

        temp->lk_flags = trav->lk_flags;

        temp->client_uid = gf_strdup(trav->client_uid);

        list_add_tail(&temp->list, &lmi->list);

        trav = trav->nextentry;
    }

    ret = 0;
out:
    return ret;
}

int
unserialize_req_locklist_v2(gfx_setactivelk_req *req,
                            lock_migration_info_t *lmi)
{
    struct gfs3_locklist *trav = NULL;
    lock_migration_info_t *temp = NULL;
    int ret = -1;

    trav = req->request;

    INIT_LIST_HEAD(&lmi->list);

    while (trav) {
        temp = GF_CALLOC(1, sizeof(*lmi), gf_common_mt_lock_mig);
        if (temp == NULL) {
            gf_smsg(THIS->name, GF_LOG_ERROR, 0, PS_MSG_NO_MEM, NULL);
            goto out;
        }

        INIT_LIST_HEAD(&temp->list);

        gf_proto_flock_to_flock(&trav->flock, &temp->flock);

        temp->lk_flags = trav->lk_flags;

        temp->client_uid = gf_strdup(trav->client_uid);

        list_add_tail(&temp->list, &lmi->list);

        trav = trav->nextentry;
    }

    ret = 0;
out:
    return ret;
}
