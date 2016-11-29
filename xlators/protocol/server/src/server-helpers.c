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
#include "gidcache.h"
#include "server-messages.h"
#include "syscall.h"
#include "defaults.h"
#include "default-args.h"
#include "server-common.h"

#include <fnmatch.h>
#include <pwd.h>
#include <grp.h>
#include "compound-fop-utils.h"

/* based on nfs_fix_aux_groups() */
int
gid_resolve (server_conf_t *conf, call_stack_t *root)
{
        int               ret = 0;
        struct passwd     mypw;
        char              mystrs[1024];
        struct passwd    *result;
        gid_t             mygroups[GF_MAX_AUX_GROUPS];
        gid_list_t        gl;
        const gid_list_t *agl;
        int               ngroups, i;

        agl = gid_cache_lookup (&conf->gid_cache, root->uid, 0, 0);
        if (agl) {
                root->ngrps = agl->gl_count;
                goto fill_groups;
        }

        ret = getpwuid_r (root->uid, &mypw, mystrs, sizeof(mystrs), &result);
        if (ret != 0) {
                gf_msg ("gid-cache", GF_LOG_ERROR, errno,
                        PS_MSG_GET_UID_FAILED, "getpwuid_r(%u) failed",
                        root->uid);
                return -1;
        }

        if (!result) {
                gf_msg ("gid-cache", GF_LOG_ERROR, 0, PS_MSG_UID_NOT_FOUND,
                        "getpwuid_r(%u) found nothing", root->uid);
                return -1;
        }

        gf_msg_trace ("gid-cache", 0, "mapped %u => %s", root->uid,
                      result->pw_name);

        ngroups = GF_MAX_AUX_GROUPS;
        ret = getgrouplist (result->pw_name, root->gid, mygroups, &ngroups);
        if (ret == -1) {
                gf_msg ("gid-cache", GF_LOG_ERROR, 0, PS_MSG_MAPPING_ERROR,
                        "could not map %s to group list (%d gids)",
                        result->pw_name, root->ngrps);
                return -1;
        }
        root->ngrps = (uint16_t) ngroups;

fill_groups:
        if (agl) {
                /* the gl is not complete, we only use gl.gl_list later on */
                gl.gl_list = agl->gl_list;
        } else {
                /* setup a full gid_list_t to add it to the gid_cache */
                gl.gl_id = root->uid;
                gl.gl_uid = root->uid;
                gl.gl_gid = root->gid;
                gl.gl_count = root->ngrps;

                gl.gl_list = GF_MALLOC (root->ngrps * sizeof(gid_t),
                                        gf_common_mt_groups_t);
                if (gl.gl_list)
                        memcpy (gl.gl_list, mygroups,
                                sizeof(gid_t) * root->ngrps);
                else
                        return -1;
        }

        if (root->ngrps == 0) {
                ret = 0;
                goto out;
        }

        if (call_stack_alloc_groups (root, root->ngrps) != 0) {
                ret = -1;
                goto out;
        }

        /* finally fill the groups from the */
        for (i = 0; i < root->ngrps; ++i)
                root->groups[i] = gl.gl_list[i];

out:
        if (agl) {
                gid_cache_release (&conf->gid_cache, agl);
        } else {
                if (gid_cache_add (&conf->gid_cache, &gl) != 1)
                        GF_FREE (gl.gl_list);
        }

        return ret;
}

int
server_resolve_groups (call_frame_t *frame, rpcsvc_request_t *req)
{
        xlator_t      *this = NULL;
        server_conf_t *conf = NULL;

        GF_VALIDATE_OR_GOTO ("server", frame, out);
        GF_VALIDATE_OR_GOTO ("server", req, out);

        this = req->trans->xl;
        conf = this->private;

        return gid_resolve (conf, frame->root);
out:
        return -1;
}

int
server_decode_groups (call_frame_t *frame, rpcsvc_request_t *req)
{
        int     i = 0;

        GF_VALIDATE_OR_GOTO ("server", frame, out);
        GF_VALIDATE_OR_GOTO ("server", req, out);

        if (call_stack_alloc_groups (frame->root, req->auxgidcount) != 0)
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
server_loc_wipe (loc_t *loc)
{
        if (loc->parent) {
                inode_unref (loc->parent);
                loc->parent = NULL;
        }

        if (loc->inode) {
                inode_unref (loc->inode);
                loc->inode = NULL;
        }

        GF_FREE ((void *)loc->path);
}


void
server_resolve_wipe (server_resolve_t *resolve)
{
        GF_FREE ((void *)resolve->path);

        GF_FREE ((void *)resolve->bname);

        loc_wipe (&resolve->resolve_loc);
}


void
free_state (server_state_t *state)
{
        if (state->xprt) {
                rpc_transport_unref (state->xprt);
                state->xprt = NULL;
        }
        if (state->fd) {
                fd_unref (state->fd);
                state->fd = NULL;
        }

        if (state->params) {
                dict_unref (state->params);
                state->params = NULL;
        }

        if (state->iobref) {
                iobref_unref (state->iobref);
                state->iobref = NULL;
        }

        if (state->iobuf) {
                iobuf_unref (state->iobuf);
                state->iobuf = NULL;
        }

        if (state->dict) {
                dict_unref (state->dict);
                state->dict = NULL;
        }

        if (state->xdata) {
                dict_unref (state->xdata);
                state->xdata = NULL;
        }

        GF_FREE ((void *)state->volume);

        GF_FREE ((void *)state->name);

        server_loc_wipe (&state->loc);
        server_loc_wipe (&state->loc2);

        server_resolve_wipe (&state->resolve);
        server_resolve_wipe (&state->resolve2);

        compound_args_cleanup (state->args);

        GF_FREE (state);
}


static int
server_connection_cleanup_flush_cbk (call_frame_t *frame, void *cookie,
                                     xlator_t *this, int32_t op_ret,
                                     int32_t op_errno, dict_t *xdata)
{
        int32_t    ret    = -1;
        fd_t      *fd     = NULL;
        client_t  *client = NULL;

        GF_VALIDATE_OR_GOTO ("server", this, out);
        GF_VALIDATE_OR_GOTO ("server", frame, out);

        fd = frame->local;
        client = frame->root->client;

        fd_unref (fd);
        frame->local = NULL;

        gf_client_unref (client);
        STACK_DESTROY (frame->root);

        ret = 0;
out:
        return ret;
}


static int
do_fd_cleanup (xlator_t *this, client_t* client, fdentry_t *fdentries, int fd_count)
{
        fd_t               *fd = NULL;
        int                 i = 0, ret = -1;
        call_frame_t       *tmp_frame = NULL;
        xlator_t           *bound_xl = NULL;
        char               *path     = NULL;

        GF_VALIDATE_OR_GOTO ("server", this, out);
        GF_VALIDATE_OR_GOTO ("server", fdentries, out);

        bound_xl = client->bound_xl;
        for (i = 0;i < fd_count; i++) {
                fd = fdentries[i].fd;

                if (fd != NULL) {
                        tmp_frame = create_frame (this, this->ctx->pool);
                        if (tmp_frame == NULL) {
                                goto out;
                        }

                        GF_ASSERT (fd->inode);

                        ret = inode_path (fd->inode, NULL, &path);

                        if (ret > 0) {
                                gf_msg (this->name, GF_LOG_INFO, 0,
                                        PS_MSG_FD_CLEANUP,
                                        "fd cleanup on %s", path);
                                GF_FREE (path);
                        }  else {

                                gf_msg (this->name, GF_LOG_INFO, 0,
                                        PS_MSG_FD_CLEANUP,
                                        "fd cleanup on inode with gfid %s",
                                        uuid_utoa (fd->inode->gfid));
                        }

                        tmp_frame->local = fd;
                        tmp_frame->root->pid = 0;
                        gf_client_ref (client);
                        tmp_frame->root->client = client;
                        memset (&tmp_frame->root->lk_owner, 0,
                                sizeof (gf_lkowner_t));

                        STACK_WIND (tmp_frame,
                                    server_connection_cleanup_flush_cbk,
                                    bound_xl, bound_xl->fops->flush, fd, NULL);
                }
        }

        GF_FREE (fdentries);
        ret = 0;

out:
        return ret;
}


int
server_connection_cleanup (xlator_t *this, client_t *client,
                           int32_t flags)
{
        server_ctx_t        *serv_ctx  = NULL;
        fdentry_t           *fdentries = NULL;
        uint32_t             fd_count  = 0;
        int                  cd_ret    = 0;
        int                  ret       = 0;

        GF_VALIDATE_OR_GOTO (this->name, this, out);
        GF_VALIDATE_OR_GOTO (this->name, client, out);
        GF_VALIDATE_OR_GOTO (this->name, flags, out);

        serv_ctx = server_ctx_get (client, client->this);

        if (serv_ctx == NULL) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        PS_MSG_SERVER_CTX_GET_FAILED, "server_ctx_get() "
                        "failed");
                goto out;
        }

        LOCK (&serv_ctx->fdtable_lock);
        {
                if (serv_ctx->fdtable && (flags & POSIX_LOCKS))
                        fdentries = gf_fd_fdtable_get_all_fds (serv_ctx->fdtable,
                                                               &fd_count);
        }
        UNLOCK (&serv_ctx->fdtable_lock);

        if (client->bound_xl == NULL)
                goto out;

        if (flags & INTERNAL_LOCKS) {
                cd_ret = gf_client_disconnect (client);
        }

        if (fdentries != NULL) {
                gf_msg_debug (this->name, 0, "Performing cleanup on %d "
                              "fdentries", fd_count);
                ret = do_fd_cleanup (this, client, fdentries, fd_count);
        }
        else
                gf_msg (this->name, GF_LOG_INFO, 0, PS_MSG_FDENTRY_NULL,
                        "no fdentries to clean");

        if (cd_ret || ret)
                ret = -1;

out:
        return ret;
}


static call_frame_t *
server_alloc_frame (rpcsvc_request_t *req)
{
        call_frame_t    *frame  = NULL;
        server_state_t  *state  = NULL;
        client_t        *client = NULL;

        GF_VALIDATE_OR_GOTO ("server", req, out);
        GF_VALIDATE_OR_GOTO ("server", req->trans, out);
        GF_VALIDATE_OR_GOTO ("server", req->svc, out);
        GF_VALIDATE_OR_GOTO ("server", req->svc->ctx, out);

        client = req->trans->xl_private;
        GF_VALIDATE_OR_GOTO ("server", client, out);

        frame = create_frame (client->this, req->svc->ctx->pool);
        if (!frame)
                goto out;

        state = GF_CALLOC (1, sizeof (*state), gf_server_mt_state_t);
        if (!state)
                goto out;

        if (client->bound_xl)
                state->itable = client->bound_xl->itable;

        state->xprt  = rpc_transport_ref (req->trans);
        state->resolve.fd_no = -1;
        state->resolve2.fd_no = -1;

        frame->root->client  = client;
        frame->root->state = state;        /* which socket */
        frame->root->unique = 0;           /* which call */

        frame->this = client->this;
out:
        return frame;
}


call_frame_t *
get_frame_from_request (rpcsvc_request_t *req)
{
        call_frame_t  *frame = NULL;
        client_t      *client = NULL;
        client_t      *tmp_client = NULL;
        xlator_t  *this = NULL;
        server_conf_t *priv = NULL;
        clienttable_t *clienttable = NULL;
        unsigned int   i           = 0;
        rpc_transport_t *trans = NULL;

        GF_VALIDATE_OR_GOTO ("server", req, out);

        client = req->trans->xl_private;

        frame = server_alloc_frame (req);
        if (!frame)
                goto out;

        frame->root->op       = req->procnum;

        frame->root->unique   = req->xid;

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
                           do the root-squashing.
                           TODO: If any client within the storage pool (i.e
                           mounting within a machine from the pool but using
                           other machine's ip/hostname from the same pool)
                           is present treat it as a trusted client
                        */
                        if (!client->auth.username && req->pid != NFS_PID)
                                RPC_AUTH_ROOT_SQUASH (req);

                        /* Problem: If we just check whether the client is
                           trusted client and do not do root squashing for
                           them, then for smb clients and UFO clients root
                           squashing will never happen as they use the fuse
                           mounts done within the trusted pool (i.e they are
                           trusted clients).
                           Solution: To fix it, do root squashing for trusted
                           clients also. If one wants to have a client within
                           the storage pool for which root-squashing does not
                           happen, then the client has to be mounted with
                           --no-root-squash option. But for defrag client and
                           gsyncd client do not do root-squashing.
                        */
                        if (client->auth.username &&
                            req->pid != GF_CLIENT_PID_NO_ROOT_SQUASH &&
                            req->pid != GF_CLIENT_PID_GSYNCD &&
                            req->pid != GF_CLIENT_PID_DEFRAG &&
                            req->pid != GF_CLIENT_PID_SELF_HEALD &&
                            req->pid != GF_CLIENT_PID_QUOTA_MOUNT)
                                RPC_AUTH_ROOT_SQUASH (req);

                        /* For nfs clients the server processes will be running
                           within the trusted storage pool machines. So if we
                           do not do root-squashing for nfs servers, thinking
                           that its a trusted client, then root-squashing wont
                           work for nfs clients.
                        */
                        if (req->pid == NFS_PID)
                                RPC_AUTH_ROOT_SQUASH (req);
                }
        }

        frame->root->uid      = req->uid;
        frame->root->gid      = req->gid;
        frame->root->pid      = req->pid;
        gf_client_ref (client);
        frame->root->client   = client;
        frame->root->lk_owner = req->lk_owner;

        if (priv->server_manage_gids)
            server_resolve_groups (frame, req);
        else
            server_decode_groups (frame, req);
        trans = req->trans;
        if (trans) {
                memcpy (&frame->root->identifier, trans->peerinfo.identifier,
                        sizeof (trans->peerinfo.identifier));
        }


        frame->local = req;
out:
        return frame;
}


int
server_build_config (xlator_t *this, server_conf_t *conf)
{
        data_t     *data = NULL;
        int         ret = -1;
        struct stat buf = {0,};

        GF_VALIDATE_OR_GOTO ("server", this, out);
        GF_VALIDATE_OR_GOTO ("server", conf, out);

        ret = dict_get_int32 (this->options, "inode-lru-limit",
                              &conf->inode_lru_limit);
        if (ret < 0) {
                conf->inode_lru_limit = 16384;
        }

        conf->verify_volfile = 1;
        data = dict_get (this->options, "verify-volfile-checksum");
        if (data) {
                ret = gf_string2boolean(data->data, &conf->verify_volfile);
                if (ret != 0) {
                        gf_msg (this->name, GF_LOG_WARNING, EINVAL,
                                PS_MSG_INVALID_ENTRY, "wrong value for '"
                                "verify-volfile-checksum', Neglecting option");
                }
        }

        data = dict_get (this->options, "trace");
        if (data) {
                ret = gf_string2boolean (data->data, &conf->trace);
                if (ret != 0) {
                        gf_msg (this->name, GF_LOG_WARNING, EINVAL,
                                PS_MSG_INVALID_ENTRY, "'trace' takes on only "
                                "boolean values. Neglecting option");
                }
        }

        /* TODO: build_rpc_config (); */
        ret = dict_get_int32 (this->options, "limits.transaction-size",
                              &conf->rpc_conf.max_block_size);
        if (ret < 0) {
                gf_msg_trace (this->name, 0, "defaulting limits.transaction-"
                              "size to %d", DEFAULT_BLOCK_SIZE);
                conf->rpc_conf.max_block_size = DEFAULT_BLOCK_SIZE;
        }

        data = dict_get (this->options, "config-directory");
        if (data) {
                /* Check whether the specified directory exists,
                   or directory specified is non standard */
                ret = sys_stat (data->data, &buf);
                if ((ret != 0) || !S_ISDIR (buf.st_mode)) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                PS_MSG_DIR_NOT_FOUND, "Directory '%s' doesn't "
                                "exist, exiting.", data->data);
                        ret = -1;
                        goto out;
                }
                /* Make sure that conf-dir doesn't contain ".." in path */
                if ((gf_strstr (data->data, "/", "..")) == -1) {
                        ret = -1;
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                PS_MSG_CONF_DIR_INVALID,
                                "%s: invalid conf_dir", data->data);
                        goto out;
                }

                conf->conf_dir = gf_strdup (data->data);
        }
        ret = 0;
out:
        return ret;
}


void
print_caller (char *str, int size, call_frame_t *frame)
{
        server_state_t  *state = NULL;

        GF_VALIDATE_OR_GOTO ("server", str, out);
        GF_VALIDATE_OR_GOTO ("server", frame, out);

        state = CALL_STATE (frame);

        snprintf (str, size,
                  " Callid=%"PRId64", Client=%s",
                  frame->root->unique,
                  state->xprt->peerinfo.identifier);

out:
        return;
}


void
server_print_resolve (char *str, int size, server_resolve_t *resolve)
{
        int filled = 0;

        GF_VALIDATE_OR_GOTO ("server", str, out);

        if (!resolve) {
                snprintf (str, size, "<nul>");
                return;
        }

        filled += snprintf (str + filled, size - filled,
                            " Resolve={");
        if (resolve->fd_no != -1)
                filled += snprintf (str + filled, size - filled,
                                    "fd=%"PRId64",", (uint64_t) resolve->fd_no);
        if (resolve->bname)
                filled += snprintf (str + filled, size - filled,
                                    "bname=%s,", resolve->bname);
        if (resolve->path)
                filled += snprintf (str + filled, size - filled,
                                    "path=%s", resolve->path);

        snprintf (str + filled, size - filled, "}");
out:
        return;
}


void
server_print_loc (char *str, int size, loc_t *loc)
{
        int filled = 0;

        GF_VALIDATE_OR_GOTO ("server", str, out);

        if (!loc) {
                snprintf (str, size, "<nul>");
                return;
        }

        filled += snprintf (str + filled, size - filled,
                            " Loc={");

        if (loc->path)
                filled += snprintf (str + filled, size - filled,
                                    "path=%s,", loc->path);
        if (loc->inode)
                filled += snprintf (str + filled, size - filled,
                                    "inode=%p,", loc->inode);
        if (loc->parent)
                filled += snprintf (str + filled, size - filled,
                                    "parent=%p", loc->parent);

        snprintf (str + filled, size - filled, "}");
out:
        return;
}


void
server_print_params (char *str, int size, server_state_t *state)
{
        int filled = 0;

        GF_VALIDATE_OR_GOTO ("server", str, out);

        filled += snprintf (str + filled, size - filled,
                            " Params={");

        if (state->fd)
                filled += snprintf (str + filled, size - filled,
                                    "fd=%p,", state->fd);
        if (state->valid)
                filled += snprintf (str + filled, size - filled,
                                    "valid=%d,", state->valid);
        if (state->flags)
                filled += snprintf (str + filled, size - filled,
                                    "flags=%d,", state->flags);
        if (state->wbflags)
                filled += snprintf (str + filled, size - filled,
                                    "wbflags=%d,", state->wbflags);
        if (state->size)
                filled += snprintf (str + filled, size - filled,
                                    "size=%zu,", state->size);
        if (state->offset)
                filled += snprintf (str + filled, size - filled,
                                    "offset=%"PRId64",", state->offset);
        if (state->cmd)
                filled += snprintf (str + filled, size - filled,
                                    "cmd=%d,", state->cmd);
        if (state->type)
                filled += snprintf (str + filled, size - filled,
                                    "type=%d,", state->type);
        if (state->name)
                filled += snprintf (str + filled, size - filled,
                                    "name=%s,", state->name);
        if (state->mask)
                filled += snprintf (str + filled, size - filled,
                                    "mask=%d,", state->mask);
        if (state->volume)
                filled += snprintf (str + filled, size - filled,
                                    "volume=%s,", state->volume);

/* FIXME
        snprintf (str + filled, size - filled,
                  "bound_xl=%s}", state->client->bound_xl->name);
*/
out:
        return;
}


int
server_resolve_is_empty (server_resolve_t *resolve)
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
server_print_reply (call_frame_t *frame, int op_ret, int op_errno)
{
        server_conf_t   *conf = NULL;
        server_state_t  *state = NULL;
        xlator_t        *this = NULL;
        char             caller[512];
        char             fdstr[32];
        char            *op = "UNKNOWN";

        GF_VALIDATE_OR_GOTO ("server", frame, out);

        this = frame->this;
        conf = this->private;

        GF_VALIDATE_OR_GOTO ("server", conf, out);
        GF_VALIDATE_OR_GOTO ("server", conf->trace, out);

        state = CALL_STATE (frame);

        print_caller (caller, 256, frame);

        switch (frame->root->type) {
        case GF_OP_TYPE_FOP:
                op = (char *)gf_fop_list[frame->root->op];
                break;
        default:
                op = "";
        }

        fdstr[0] = '\0';
        if (state->fd)
                snprintf (fdstr, 32, " fd=%p", state->fd);

        gf_msg (this->name, GF_LOG_INFO, op_errno, PS_MSG_SERVER_MSG,
                "%s%s => (%d, %d)%s", op, caller, op_ret, op_errno, fdstr);
out:
        return;
}


void
server_print_request (call_frame_t *frame)
{
        server_conf_t   *conf  = NULL;
        xlator_t        *this  = NULL;
        server_state_t  *state = NULL;
        char            *op    = "UNKNOWN";
        char             resolve_vars[256];
        char             resolve2_vars[256];
        char             loc_vars[256];
        char             loc2_vars[256];
        char             other_vars[512];
        char             caller[512];

        GF_VALIDATE_OR_GOTO ("server", frame, out);

        this = frame->this;
        conf = this->private;

        GF_VALIDATE_OR_GOTO ("server", conf, out);

        if (!conf->trace)
                goto out;

        state = CALL_STATE (frame);

        memset (resolve_vars, '\0', 256);
        memset (resolve2_vars, '\0', 256);
        memset (loc_vars, '\0', 256);
        memset (loc2_vars, '\0', 256);
        memset (other_vars, '\0', 256);

        print_caller (caller, 256, frame);

        if (!server_resolve_is_empty (&state->resolve)) {
                server_print_resolve (resolve_vars, 256, &state->resolve);
                server_print_loc (loc_vars, 256, &state->loc);
        }

        if (!server_resolve_is_empty (&state->resolve2)) {
                server_print_resolve (resolve2_vars, 256, &state->resolve2);
                server_print_loc (loc2_vars, 256, &state->loc2);
        }

        server_print_params (other_vars, 512, state);

        switch (frame->root->type) {
        case GF_OP_TYPE_FOP:
                op = (char *)gf_fop_list[frame->root->op];
                break;
        default:
                op = "";
                break;
        }

        gf_msg (this->name, GF_LOG_INFO, 0, PS_MSG_SERVER_MSG,
                "%s%s%s%s%s%s%s", op, caller,
                resolve_vars, loc_vars, resolve2_vars, loc2_vars, other_vars);
out:
        return;
}


int
serialize_rsp_direntp (gf_dirent_t *entries, gfs3_readdirp_rsp *rsp)
{
        gf_dirent_t         *entry = NULL;
        gfs3_dirplist       *trav  = NULL;
        gfs3_dirplist       *prev  = NULL;
        int                  ret   = -1;

        GF_VALIDATE_OR_GOTO ("server", entries, out);
        GF_VALIDATE_OR_GOTO ("server", rsp, out);

        list_for_each_entry (entry, &entries->list, list) {
                trav = GF_CALLOC (1, sizeof (*trav), gf_server_mt_dirent_rsp_t);
                if (!trav)
                        goto out;

                trav->d_ino  = entry->d_ino;
                trav->d_off  = entry->d_off;
                trav->d_len  = entry->d_len;
                trav->d_type = entry->d_type;
                trav->name   = entry->d_name;

                gf_stat_from_iatt (&trav->stat, &entry->d_stat);

                /* if 'dict' is present, pack it */
                if (entry->dict) {
                        trav->dict.dict_len = dict_serialized_length (entry->dict);
                        if (trav->dict.dict_len > UINT_MAX) {
                                gf_msg (THIS->name, GF_LOG_ERROR, EINVAL,
                                        PS_MSG_INVALID_ENTRY, "failed to get "
                                        "serialized length of reply dict");
                                errno = EINVAL;
                                trav->dict.dict_len = 0;
                                goto out;
                        }

                        trav->dict.dict_val = GF_CALLOC (1, trav->dict.dict_len,
                                                         gf_server_mt_rsp_buf_t);
                        if (!trav->dict.dict_val) {
                                errno = ENOMEM;
                                trav->dict.dict_len = 0;
                                goto out;
                        }

                        ret = dict_serialize (entry->dict, trav->dict.dict_val);
                        if (ret < 0) {
                                gf_msg (THIS->name, GF_LOG_ERROR, 0,
                                        PS_MSG_DICT_SERIALIZE_FAIL,
                                        "failed to serialize reply dict");
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
        GF_FREE (trav);

        return ret;
}


int
serialize_rsp_dirent (gf_dirent_t *entries, gfs3_readdir_rsp *rsp)
{
        gf_dirent_t   *entry = NULL;
        gfs3_dirlist  *trav  = NULL;
        gfs3_dirlist  *prev  = NULL;
        int           ret    = -1;

        GF_VALIDATE_OR_GOTO ("server", entries, out);
        GF_VALIDATE_OR_GOTO ("server", rsp, out);

        list_for_each_entry (entry, &entries->list, list) {
                trav = GF_CALLOC (1, sizeof (*trav), gf_server_mt_dirent_rsp_t);
                if (!trav)
                        goto out;
                trav->d_ino  = entry->d_ino;
                trav->d_off  = entry->d_off;
                trav->d_len  = entry->d_len;
                trav->d_type = entry->d_type;
                trav->name   = entry->d_name;
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
readdir_rsp_cleanup (gfs3_readdir_rsp *rsp)
{
        gfs3_dirlist  *prev = NULL;
        gfs3_dirlist  *trav = NULL;

        trav = rsp->reply;
        prev = trav;
        while (trav) {
                trav = trav->nextentry;
                GF_FREE (prev);
                prev = trav;
        }

        return 0;
}


int
readdirp_rsp_cleanup (gfs3_readdirp_rsp *rsp)
{
        gfs3_dirplist *prev = NULL;
        gfs3_dirplist *trav = NULL;

        trav = rsp->reply;
        prev = trav;
        while (trav) {
                trav = trav->nextentry;
                GF_FREE (prev->dict.dict_val);
                GF_FREE (prev);
                prev = trav;
        }

        return 0;
}

int
serialize_rsp_locklist (lock_migration_info_t *locklist,
                               gfs3_getactivelk_rsp *rsp)
{
        lock_migration_info_t   *tmp    = NULL;
        gfs3_locklist           *trav   = NULL;
        gfs3_locklist           *prev   = NULL;
        int                     ret     = -1;

        GF_VALIDATE_OR_GOTO ("server", locklist, out);
        GF_VALIDATE_OR_GOTO ("server", rsp, out);

        list_for_each_entry (tmp, &locklist->list, list) {
                trav = GF_CALLOC (1, sizeof (*trav), gf_server_mt_lock_mig_t);
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
                        gf_msg (THIS->name, GF_LOG_ERROR, 0, PS_MSG_LOCK_ERROR,
                                "Unknown lock type: %"PRId32"!",
                                tmp->flock.l_type);
                        break;
                }

                gf_proto_flock_from_flock (&trav->flock, &tmp->flock);

                trav->lk_flags = tmp->lk_flags;

                trav->client_uid = tmp->client_uid;

                if (prev)
                        prev->nextentry = trav;
                else
                        rsp->reply = trav;

                prev = trav;
                trav = NULL;
        }

        ret = 0;
out:
        GF_FREE (trav);
        return ret;
}

int
getactivelkinfo_rsp_cleanup (gfs3_getactivelk_rsp  *rsp)
{
        gfs3_locklist  *prev = NULL;
        gfs3_locklist  *trav = NULL;

        trav = rsp->reply;
        prev = trav;

        while (trav) {
                trav = trav->nextentry;
                GF_FREE (prev);
                prev = trav;
        }

        return 0;
}

int
gf_server_check_getxattr_cmd (call_frame_t *frame, const char *key)
{

        server_conf_t    *conf = NULL;
        rpc_transport_t  *xprt = NULL;

        conf = frame->this->private;
        if (!conf)
                return 0;

        if (fnmatch ("*list*mount*point*", key, 0) == 0) {
                /* list all the client protocol connecting to this process */
                pthread_mutex_lock (&conf->mutex);
                {
                        list_for_each_entry (xprt, &conf->xprt_list, list) {
                                gf_msg ("mount-point-list", GF_LOG_INFO, 0,
                                        PS_MSG_MOUNT_PT_FAIL,
                                        "%s", xprt->peerinfo.identifier);
                        }
                }
                pthread_mutex_unlock (&conf->mutex);
        }

        /* Add more options/keys here */

        return 0;
}


int
gf_server_check_setxattr_cmd (call_frame_t *frame, dict_t *dict)
{

        server_conf_t    *conf        = NULL;
        rpc_transport_t  *xprt        = NULL;
        uint64_t          total_read  = 0;
        uint64_t          total_write = 0;

        conf = frame->this->private;
        if (!conf || !dict)
                return 0;

        if (dict_foreach_fnmatch (dict, "*io*stat*dump",
                                  dict_null_foreach_fn, NULL ) > 0) {
                list_for_each_entry (xprt, &conf->xprt_list, list) {
                        total_read  += xprt->total_bytes_read;
                        total_write += xprt->total_bytes_write;
                }
                gf_msg ("stats", GF_LOG_INFO, 0, PS_MSG_RW_STAT,
                        "total-read %"PRIu64", total-write %"PRIu64,
                        total_read, total_write);
        }

        return 0;
}


gf_boolean_t
server_cancel_grace_timer (xlator_t *this, client_t *client)
{
        server_ctx_t  *serv_ctx  = NULL;
        gf_timer_t    *timer     = NULL;
        gf_boolean_t   cancelled = _gf_false;

        if (!this || !client) {
                gf_msg (THIS->name, GF_LOG_ERROR, EINVAL, PS_MSG_INVALID_ENTRY,
                        "Invalid arguments to cancel connection timer");
                return cancelled;
        }

        serv_ctx = server_ctx_get (client, client->this);

        if (serv_ctx == NULL) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        PS_MSG_SERVER_CTX_GET_FAILED,
                        "server_ctx_get() failed");
                goto out;
        }

        LOCK (&serv_ctx->fdtable_lock);
        {
                if (serv_ctx->grace_timer) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                        PS_MSG_GRACE_TIMER_CANCELLED,
                                        "Cancelling the grace timer");
                        timer = serv_ctx->grace_timer;
                        serv_ctx->grace_timer = NULL;
                }
        }
        UNLOCK (&serv_ctx->fdtable_lock);

        if (timer) {
                gf_timer_call_cancel (this->ctx, timer);
                cancelled = _gf_true;
        }
out:
        return cancelled;
}

server_ctx_t*
server_ctx_get (client_t *client, xlator_t *xlator)
{
        void *tmp = NULL;
        server_ctx_t *ctx = NULL;

        client_ctx_get (client, xlator, &tmp);

        ctx = tmp;

        if (ctx != NULL)
                goto out;

        ctx = GF_CALLOC (1, sizeof (server_ctx_t), gf_server_mt_server_conf_t);

        if (ctx == NULL)
                goto out;

     /* ctx->lk_version = 0; redundant */
        ctx->fdtable = gf_fd_fdtable_alloc ();

        if (ctx->fdtable == NULL) {
                GF_FREE (ctx);
                ctx = NULL;
                goto out;
        }

        LOCK_INIT (&ctx->fdtable_lock);

        if (client_ctx_set (client, xlator, ctx) != 0) {
              LOCK_DESTROY (&ctx->fdtable_lock);
              GF_FREE (ctx->fdtable);
              GF_FREE (ctx);
              ctx = NULL;
        }

out:
        return ctx;
}

int
auth_set_username_passwd (dict_t *input_params, dict_t *config_params,
                          client_t *client)
{
        int      ret           = 0;
        data_t  *allow_user    = NULL;
        data_t  *passwd_data   = NULL;
        char    *username      = NULL;
        char    *password      = NULL;
        char    *brick_name    = NULL;
        char    *searchstr     = NULL;
        char    *username_str  = NULL;
        char    *tmp           = NULL;
        char    *username_cpy  = NULL;

        ret = dict_get_str (input_params, "username", &username);
        if (ret) {
                gf_msg_debug ("auth/login", 0, "username not found, returning "
                              "DONT-CARE");
                /* For non trusted clients username and password
                   will not be there. So dont reject the client.
                */
                ret = 0;
                goto out;
        }

        ret = dict_get_str (input_params, "password", &password);
        if (ret) {
                gf_msg ("auth/login", GF_LOG_WARNING, 0,
                        PS_MSG_DICT_GET_FAILED,
                        "password not found, returning DONT-CARE");
                goto out;
        }

        ret = dict_get_str (input_params, "remote-subvolume", &brick_name);
        if (ret) {
                gf_msg ("auth/login", GF_LOG_ERROR, 0, PS_MSG_DICT_GET_FAILED,
                        "remote-subvolume not specified");
                ret = -1;
                goto out;
        }

        ret = gf_asprintf (&searchstr, "auth.login.%s.allow", brick_name);
        if (-1 == ret) {
                ret = 0;
                goto out;
        }

        allow_user = dict_get (config_params, searchstr);
        GF_FREE (searchstr);

        if (allow_user) {
                username_cpy = gf_strdup (allow_user->data);
                if (!username_cpy)
                        goto out;

                username_str = strtok_r (username_cpy, " ,", &tmp);

                while (username_str) {
                        if (!fnmatch (username_str, username, 0)) {
                                ret = gf_asprintf (&searchstr,
                                                   "auth.login.%s.password",
                                                   username);
                                if (-1 == ret)
                                        goto out;

                                passwd_data = dict_get (config_params,
                                                        searchstr);
                                GF_FREE (searchstr);

                                if (!passwd_data) {
                                        gf_msg ("auth/login", GF_LOG_ERROR, 0,
                                                PS_MSG_LOGIN_ERROR, "wrong "
                                                "username/password "
                                                "combination");
                                        ret = -1;
                                        goto out;
                                }

                                ret = !((strcmp (data_to_str (passwd_data),
                                                    password))?0: -1);
                                if (!ret) {
                                        client->auth.username =
                                                gf_strdup (username);
                                        client->auth.passwd =
                                                gf_strdup (password);
                                }
                                if (ret == -1)
                                        gf_msg ("auth/login", GF_LOG_ERROR, 0,
                                                PS_MSG_LOGIN_ERROR, "wrong "
                                                "password for user %s",
                                                username);
                                break;
                        }
                        username_str = strtok_r (NULL, " ,", &tmp);
                }
        }

out:
        GF_FREE (username_cpy);

        return ret;
}

inode_t *
server_inode_new (inode_table_t *itable, uuid_t gfid) {
        if (__is_root_gfid (gfid))
                return itable->root;
        else
                return inode_new (itable);
}

int
unserialize_req_locklist (gfs3_setactivelk_req *req,
                          lock_migration_info_t *lmi)
{
        struct gfs3_locklist            *trav      = NULL;
        lock_migration_info_t           *temp    = NULL;
        int                             ret       = -1;

        trav = req->request;

        INIT_LIST_HEAD (&lmi->list);

        while (trav) {
                temp = GF_CALLOC (1, sizeof (*lmi), gf_common_mt_lock_mig);
                if (temp == NULL) {
                        gf_msg (THIS->name, GF_LOG_ERROR, 0, 0, "No memory");
                        goto out;
                }

                INIT_LIST_HEAD (&temp->list);

                gf_proto_flock_to_flock (&trav->flock, &temp->flock);

                temp->lk_flags = trav->lk_flags;

                temp->client_uid =  gf_strdup (trav->client_uid);

                list_add_tail (&temp->list, &lmi->list);

                trav = trav->nextentry;
        }

        ret = 0;
out:
        return ret;
}

int
server_populate_compound_request (gfs3_compound_req *req, call_frame_t *frame,
                                  default_args_t *this_args,
                                  int index)
{
        int                     op_errno    = 0;
        int                     ret         = -1;
        dict_t                 *xdata       = NULL;
        dict_t                 *xattr       = NULL;
        struct iovec req_iovec[MAX_IOVEC]   = { {0,} };
        compound_req            *this_req   = NULL;
        server_state_t          *state      = CALL_STATE (frame);

        this_req = &req->compound_req_array.compound_req_array_val[index];

        switch (this_req->fop_enum) {
        case GF_FOP_STAT:
        {
                gfs3_stat_req *args = NULL;

                args = &this_req->compound_req_u.compound_stat_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_stat_store (this_args, &state->loc, xdata);
                break;
        }
        case GF_FOP_READLINK:
        {
                gfs3_readlink_req *args = NULL;

                args = &this_req->compound_req_u.compound_readlink_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_readlink_store (this_args, &state->loc, args->size, xdata);
                break;
        }
        case GF_FOP_MKNOD:
        {
                gfs3_mknod_req *args = NULL;

                args = &this_req->compound_req_u.compound_mknod_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_mknod_store (this_args, &state->loc, args->mode, args->dev,
                                  args->umask, xdata);
                break;
        }
        case GF_FOP_MKDIR:
        {
                gfs3_mkdir_req *args = NULL;

                args = &this_req->compound_req_u.compound_mkdir_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_mkdir_store (this_args, &state->loc, args->mode,
                                  args->umask, xdata);
                break;
        }
        case GF_FOP_UNLINK:
        {
                gfs3_unlink_req *args = NULL;

                args = &this_req->compound_req_u.compound_unlink_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_unlink_store (this_args, &state->loc, args->xflags, xdata);
                break;
        }
        case GF_FOP_RMDIR:
        {
                gfs3_rmdir_req *args = NULL;

                args = &this_req->compound_req_u.compound_rmdir_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_rmdir_store (this_args, &state->loc, args->xflags, xdata);
                break;
        }
        case GF_FOP_SYMLINK:
        {
                gfs3_symlink_req *args = NULL;

                args = &this_req->compound_req_u.compound_symlink_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_symlink_store (this_args, args->linkname, &state->loc,
                                    args->umask, xdata);

                this_args->loc.inode = inode_new (state->itable);

                break;
        }
        case GF_FOP_RENAME:
        {
                gfs3_rename_req *args = NULL;

                args = &this_req->compound_req_u.compound_rename_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);

                args_rename_store (this_args, &state->loc, &state->loc2, xdata);
                break;
        }
        case GF_FOP_LINK:
        {
                gfs3_link_req *args = NULL;

                args = &this_req->compound_req_u.compound_link_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_link_store (this_args, &state->loc, &state->loc2, xdata);

                this_args->loc2.inode = inode_ref (this_args->loc.inode);

                break;
        }
        case GF_FOP_TRUNCATE:
        {
                gfs3_truncate_req *args = NULL;

                args = &this_req->compound_req_u.compound_truncate_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_truncate_store (this_args, &state->loc, args->offset,
                                     xdata);
                break;
        }
        case GF_FOP_OPEN:
        {
                gfs3_open_req *args = NULL;

                args = &this_req->compound_req_u.compound_open_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_open_store (this_args, &state->loc, args->flags, state->fd,
                                 xdata);

                this_args->fd = fd_create (this_args->loc.inode,
                                           frame->root->pid);
                this_args->fd->flags = this_args->flags;

                break;
        }
        case GF_FOP_READ:
        {
                gfs3_read_req *args = NULL;

                args = &this_req->compound_req_u.compound_read_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_readv_store (this_args, state->fd, args->size,
                                  args->offset, args->flag, xdata);
                break;
        }
        case GF_FOP_WRITE:
        {
                gfs3_write_req *args = NULL;

                args = &this_req->compound_req_u.compound_write_req;

                /*TODO : What happens when payload count is more than one? */
                req_iovec[0].iov_base = state->payload_vector[0].iov_base +
                                        state->write_length;
                req_iovec[0].iov_len  = args->size;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                /* The way writev fop works :
                 * xdr args of write along with other args contains
                 * write length not count. But when the call is wound to posix,
                 * this length is not used. It is taken from the request
                 * write vector that is passed down. Posix needs the vector
                 * count to determine the amount of write to be done.
                 * This count for writes that come as part of compound fops
                 * will be 1. The vectors are merged into one under
                 * GF_FOP_WRITE section of client_handle_fop_requirements()
                 * in protocol client.
                 */
                args_writev_store (this_args, state->fd, req_iovec, 1,
                                   args->offset, args->flag, state->iobref,
                                   xdata);
                state->write_length += req_iovec[0].iov_len;
                break;
        }
        case GF_FOP_STATFS:
        {
                gfs3_statfs_req *args = NULL;

                args = &this_req->compound_req_u.compound_statfs_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_statfs_store (this_args, &state->loc, xdata);
                break;
        }
        case GF_FOP_FLUSH:
        {
                gfs3_flush_req *args = NULL;

                args = &this_req->compound_req_u.compound_flush_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_flush_store (this_args, state->fd, xdata);
                break;
        }
        case GF_FOP_FSYNC:
        {
                gfs3_fsync_req *args = NULL;

                args = &this_req->compound_req_u.compound_fsync_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_fsync_store (this_args, state->fd, args->data, xdata);
                break;
        }
        case GF_FOP_SETXATTR:
        {
                gfs3_setxattr_req *args = NULL;

                args = &this_req->compound_req_u.compound_setxattr_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xattr, args->dict.dict_val,
                                              args->dict.dict_len, ret,
                                              op_errno, out);
                args_setxattr_store (this_args, &state->loc, xattr, args->flags,
                                     xdata);
                break;
        }
        case GF_FOP_GETXATTR:
        {
                gfs3_getxattr_req *args = NULL;

                args = &this_req->compound_req_u.compound_getxattr_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                gf_server_check_getxattr_cmd (frame, args->name);

                args_getxattr_store (this_args, &state->loc, args->name, xdata);
                break;
        }
        case GF_FOP_REMOVEXATTR:
        {
                gfs3_removexattr_req *args = NULL;

                args = &this_req->compound_req_u.compound_removexattr_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_removexattr_store (this_args, &state->loc, args->name,
                                        xdata);
                break;
        }
        case GF_FOP_OPENDIR:
        {
                gfs3_opendir_req *args = NULL;

                args = &this_req->compound_req_u.compound_opendir_req;

                this_args->fd = fd_create (this_args->loc.inode,
                                           frame->root->pid);
                if (!this_args->fd) {
                        gf_msg ("server", GF_LOG_ERROR, 0,
                                PS_MSG_FD_CREATE_FAILED,
                                "could not create the fd");
                        goto out;
                }
                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata,
                                              args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_opendir_store (this_args, &state->loc, state->fd, xdata);
                break;
        }
        case GF_FOP_FSYNCDIR:
        {
                gfs3_fsyncdir_req *args = NULL;

                args = &this_req->compound_req_u.compound_fsyncdir_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_fsyncdir_store (this_args, state->fd, args->data, xdata);
                break;
        }
        case GF_FOP_ACCESS:
        {
                gfs3_access_req *args = NULL;

                args = &this_req->compound_req_u.compound_access_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_access_store (this_args, &state->loc, args->mask, xdata);
                break;
        }
        case GF_FOP_CREATE:
        {
                gfs3_create_req *args = NULL;

                args = &this_req->compound_req_u.compound_create_req;

                state->loc.inode = inode_new (state->itable);

                state->fd = fd_create (state->loc.inode, frame->root->pid);
                if (!state->fd) {
                        gf_msg ("server", GF_LOG_ERROR, 0,
                                PS_MSG_FD_CREATE_FAILED,
                                "fd creation for the inode %s failed",
                                state->loc.inode ?
                                uuid_utoa (state->loc.inode->gfid):NULL);
                        goto out;
                }
                state->fd->flags = state->flags;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_create_store (this_args, &state->loc, args->flags,
                                   args->mode, args->umask, state->fd, xdata);
                break;
        }
        case GF_FOP_FTRUNCATE:
        {
                gfs3_ftruncate_req *args = NULL;

                args = &this_req->compound_req_u.compound_ftruncate_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_ftruncate_store (this_args, state->fd, args->offset,
                                      xdata);
                break;
        }
        case GF_FOP_FSTAT:
        {
                gfs3_fstat_req *args = NULL;

                args = &this_req->compound_req_u.compound_fstat_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_fstat_store (this_args, state->fd, xdata);
                break;
        }
        case GF_FOP_LK:
        {
                gfs3_lk_req *args = NULL;

                args = &this_req->compound_req_u.compound_lk_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);

                switch (args->cmd) {
                case GF_LK_GETLK:
                        this_args->cmd = F_GETLK;
                        break;
                case GF_LK_SETLK:
                        this_args->cmd = F_SETLK;
                        break;
                case GF_LK_SETLKW:
                        this_args->cmd = F_SETLKW;
                        break;
                case GF_LK_RESLK_LCK:
                        this_args->cmd = F_RESLK_LCK;
                        break;
                case GF_LK_RESLK_LCKW:
                        this_args->cmd = F_RESLK_LCKW;
                        break;
                case GF_LK_RESLK_UNLCK:
                        this_args->cmd = F_RESLK_UNLCK;
                        break;
                case GF_LK_GETLK_FD:
                        this_args->cmd = F_GETLK_FD;
                        break;
                }

                gf_proto_flock_to_flock (&args->flock, &this_args->lock);

                switch (args->type) {
                case GF_LK_F_RDLCK:
                        this_args->lock.l_type = F_RDLCK;
                        break;
                case GF_LK_F_WRLCK:
                        this_args->lock.l_type = F_WRLCK;
                        break;
                case GF_LK_F_UNLCK:
                        this_args->lock.l_type = F_UNLCK;
                        break;
                default:
                        gf_msg (frame->root->client->bound_xl->name,
                                GF_LOG_ERROR,
                                0, PS_MSG_LOCK_ERROR, "fd - %"PRId64" (%s):"
                                " Unknown "
                                "lock type: %"PRId32"!", state->resolve.fd_no,
                                uuid_utoa (state->fd->inode->gfid),
                                args->type);
                        break;
                }
                args_lk_store (this_args, state->fd, this_args->cmd,
                               &this_args->lock, xdata);
                break;
        }
        case GF_FOP_LOOKUP:
        {
                gfs3_lookup_req *args = NULL;

                args = &this_req->compound_req_u.compound_lookup_req;

                if (this_args->loc.inode)
                        this_args->loc.inode = server_inode_new (state->itable,
                                                             state->loc.gfid);
                else
                        state->is_revalidate = 1;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_lookup_store (this_args, &state->loc, xdata);
                break;
        }
        case GF_FOP_READDIR:
        {
                gfs3_readdir_req *args = NULL;

                args = &this_req->compound_req_u.compound_readdir_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_readdir_store (this_args, state->fd, args->size,
                                    args->offset, xdata);
                break;
        }
        case GF_FOP_INODELK:
        {
                gfs3_inodelk_req *args = NULL;

                args = &this_req->compound_req_u.compound_inodelk_req;

                switch (args->cmd) {
                case GF_LK_GETLK:
                        this_args->cmd = F_GETLK;
                        break;
                case GF_LK_SETLK:
                        this_args->cmd = F_SETLK;
                        break;
                case GF_LK_SETLKW:
                        this_args->cmd = F_SETLKW;
                        break;
                }

                gf_proto_flock_to_flock (&args->flock, &this_args->lock);

                switch (args->type) {
                case GF_LK_F_RDLCK:
                        this_args->lock.l_type = F_RDLCK;
                        break;
                case GF_LK_F_WRLCK:
                        this_args->lock.l_type = F_WRLCK;
                        break;
                case GF_LK_F_UNLCK:
                        this_args->lock.l_type = F_UNLCK;
                        break;
                }

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_inodelk_store (this_args, args->volume, &state->loc,
                                    this_args->cmd, &this_args->lock, xdata);
                break;
        }
        case GF_FOP_FINODELK:
        {
                gfs3_finodelk_req *args = NULL;

                args = &this_req->compound_req_u.compound_finodelk_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);

                switch (args->cmd) {
                case GF_LK_GETLK:
                        this_args->cmd = F_GETLK;
                        break;
                case GF_LK_SETLK:
                        this_args->cmd = F_SETLK;
                        break;
                case GF_LK_SETLKW:
                        this_args->cmd = F_SETLKW;
                        break;
                }

                gf_proto_flock_to_flock (&args->flock, &this_args->lock);

                switch (args->type) {
                case GF_LK_F_RDLCK:
                        this_args->lock.l_type = F_RDLCK;
                        break;
                case GF_LK_F_WRLCK:
                        this_args->lock.l_type = F_WRLCK;
                        break;
                case GF_LK_F_UNLCK:
                        this_args->lock.l_type = F_UNLCK;
                        break;
                }
                args_finodelk_store (this_args, args->volume, state->fd,
                                     this_args->cmd, &this_args->lock, xdata);
                        break;
        }
        case GF_FOP_ENTRYLK:
        {
                gfs3_entrylk_req *args = NULL;

                args = &this_req->compound_req_u.compound_entrylk_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_entrylk_store (this_args, args->volume, &state->loc,
                                    args->name, args->cmd, args->type, xdata);
                break;
        }
        case GF_FOP_FENTRYLK:
        {
                gfs3_fentrylk_req *args = NULL;

                args = &this_req->compound_req_u.compound_fentrylk_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_fentrylk_store (this_args, args->volume, state->fd,
                                     args->name, args->cmd, args->type, xdata);
                break;
        }
        case GF_FOP_XATTROP:
        {
                gfs3_xattrop_req *args = NULL;

                args = &this_req->compound_req_u.compound_xattrop_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xattr, (args->dict.dict_val),
                                              (args->dict.dict_len), ret,
                                               op_errno, out);
                args_xattrop_store (this_args, &state->loc, args->flags,
                                    xattr, xdata);
                break;
        }
        case GF_FOP_FXATTROP:
        {
                gfs3_fxattrop_req *args = NULL;

                args = &this_req->compound_req_u.compound_fxattrop_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xattr, (args->dict.dict_val),
                                              (args->dict.dict_len), ret,
                                              op_errno, out);

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);

                args_fxattrop_store (this_args, state->fd, args->flags, xattr,
                                     xdata);
                break;
        }
        case GF_FOP_FGETXATTR:
        {
                gfs3_fgetxattr_req *args = NULL;

                args = &this_req->compound_req_u.compound_fgetxattr_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);

                args_fgetxattr_store (this_args, state->fd, args->name, xdata);
                break;
        }
        case GF_FOP_FSETXATTR:
        {
                gfs3_fsetxattr_req *args = NULL;

                args = &this_req->compound_req_u.compound_fsetxattr_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xattr, (args->dict.dict_val),
                                              (args->dict.dict_len), ret,
                                              op_errno, out);

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);

                args_fsetxattr_store (this_args, state->fd, xattr, args->flags,
                                      xdata);
                break;
        }
        case GF_FOP_RCHECKSUM:
        {
                gfs3_rchecksum_req *args = NULL;

                args = &this_req->compound_req_u.compound_rchecksum_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);

                args_rchecksum_store (this_args, state->fd, args->offset,
                                      args->len, xdata);
                break;
        }
        case GF_FOP_SETATTR:
        {
                gfs3_setattr_req *args = NULL;

                args = &this_req->compound_req_u.compound_setattr_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);

                gf_stat_to_iatt (&args->stbuf, &this_args->stat);

                args_setattr_store (this_args, &state->loc, &this_args->stat,
                                    args->valid, xdata);
                break;
        }
        case GF_FOP_FSETATTR:
        {
                gfs3_fsetattr_req *args = NULL;

                args = &this_req->compound_req_u.compound_fsetattr_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);

                gf_stat_to_iatt (&args->stbuf, &this_args->stat);

                args_fsetattr_store (this_args, state->fd, &this_args->stat,
                                     args->valid, xdata);
                break;
        }
        case GF_FOP_READDIRP:
        {
                gfs3_readdirp_req *args = NULL;

                args = &this_req->compound_req_u.compound_readdirp_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xattr, (args->dict.dict_val),
                                              (args->dict.dict_len), ret,
                                              op_errno, out);

                args_readdirp_store (this_args, state->fd, args->size,
                                     args->offset, xattr);
                break;
        }
        case GF_FOP_FREMOVEXATTR:
        {
                gfs3_fremovexattr_req *args = NULL;

                args = &this_req->compound_req_u.compound_fremovexattr_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);

                args_fremovexattr_store (this_args, state->fd, args->name,
                                         xdata);
                break;
        }
	case GF_FOP_FALLOCATE:
        {
                gfs3_fallocate_req *args = NULL;

                args = &this_req->compound_req_u.compound_fallocate_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_fallocate_store (this_args, state->fd, args->flags,
                                      args->offset, args->size, xdata);
                break;
        }
	case GF_FOP_DISCARD:
        {
                gfs3_discard_req *args = NULL;

                args = &this_req->compound_req_u.compound_discard_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);

                args_discard_store (this_args, state->fd, args->offset,
                                    args->size, xdata);
                break;
        }
        case GF_FOP_ZEROFILL:
        {
                gfs3_zerofill_req *args = NULL;

                args = &this_req->compound_req_u.compound_zerofill_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_zerofill_store (this_args, state->fd, args->offset,
                                     args->size, xdata);
                break;
        }
        case GF_FOP_SEEK:
        {
                gfs3_seek_req *args = NULL;

                args = &this_req->compound_req_u.compound_seek_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);
                args_seek_store (this_args, state->fd, args->offset, args->what,
                                 xdata);
                break;
        }
        case GF_FOP_LEASE:
        {
                gfs3_lease_req *args = NULL;

                args = &this_req->compound_req_u.compound_lease_req;

                GF_PROTOCOL_DICT_UNSERIALIZE (frame->root->client->bound_xl,
                                              xdata, args->xdata.xdata_val,
                                              args->xdata.xdata_len, ret,
                                              op_errno, out);

                gf_proto_lease_to_lease (&args->lease, &state->lease);

                args_lease_store (this_args, &state->loc, &state->lease, xdata);
                break;
        }
        default:
                return ENOTSUP;
        }
out:
        if (xattr)
                dict_unref (xattr);
        if (xdata)
                dict_unref (xdata);
        return op_errno;
}

int
server_populate_compound_response (xlator_t *this, gfs3_compound_rsp *rsp,
                                   call_frame_t *frame,
                                   compound_args_cbk_t *args_cbk, int index)
{
        int                     op_errno    = EINVAL;
        default_args_cbk_t      *this_args_cbk = NULL;
        compound_rsp            *this_rsp   = NULL;
        server_state_t          *state      = NULL;
        int                     ret         = 0;

        state = CALL_STATE (frame);
        this_rsp = &rsp->compound_rsp_array.compound_rsp_array_val[index];

        this_args_cbk = &args_cbk->rsp_list[index];
        this_rsp->fop_enum = args_cbk->enum_list[index];

        switch (this_rsp->fop_enum) {
        case GF_FOP_STAT:
        {
                gfs3_stat_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_stat_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                if (!this_args_cbk->op_ret) {
                        server_post_stat (rsp_args,
                                          &this_args_cbk->stat);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_READLINK:
        {
                gfs3_readlink_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_readlink_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                if (this_args_cbk->op_ret >= 0) {
                        server_post_readlink (rsp_args, &this_args_cbk->stat,
                                              this_args_cbk->buf);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                if (!rsp_args->path)
                        rsp_args->path = "";
                break;
        }
        case GF_FOP_MKNOD:
        {
                gfs3_mknod_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_mknod_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                if (!this_args_cbk->op_ret) {
                        server_post_mknod (state, rsp_args,
                                           &this_args_cbk->stat,
                                           &this_args_cbk->preparent,
                                           &this_args_cbk->postparent,
                                           this_args_cbk->inode);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_MKDIR:
        {
                gfs3_mkdir_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_mkdir_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        server_post_mkdir (state, rsp_args,
                                           this_args_cbk->inode,
                                           &this_args_cbk->stat,
                                           &this_args_cbk->preparent,
                                           &this_args_cbk->postparent,
                                           this_args_cbk->xdata);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_UNLINK:
        {
                gfs3_unlink_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_unlink_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                if (!this_args_cbk->op_ret) {
                        server_post_unlink (state, rsp_args,
                                            &this_args_cbk->preparent,
                                            &this_args_cbk->postparent);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_RMDIR:
        {
                gfs3_rmdir_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_rmdir_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                if (!this_args_cbk->op_ret) {
                        server_post_rmdir (state, rsp_args,
                                            &this_args_cbk->preparent,
                                            &this_args_cbk->postparent);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_SYMLINK:
        {
                gfs3_symlink_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_symlink_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        server_post_symlink (state, rsp_args,
                                             this_args_cbk->inode,
                                             &this_args_cbk->stat,
                                             &this_args_cbk->preparent,
                                             &this_args_cbk->postparent,
                                             this_args_cbk->xdata);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_RENAME:
        {
                gfs3_rename_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_rename_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        server_post_rename (frame, state, rsp_args,
                                            &this_args_cbk->stat,
                                            &this_args_cbk->preparent,
                                            &this_args_cbk->postparent,
                                            &this_args_cbk->preparent2,
                                            &this_args_cbk->postparent2);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_LINK:
        {
                gfs3_link_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_link_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        server_post_link (state, rsp_args,
                                          this_args_cbk->inode,
                                          &this_args_cbk->stat,
                                          &this_args_cbk->preparent,
                                          &this_args_cbk->postparent,
                                          this_args_cbk->xdata);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_TRUNCATE:
        {
                gfs3_truncate_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_truncate_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        server_post_truncate (rsp_args,
                                              &this_args_cbk->prestat,
                                              &this_args_cbk->poststat);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_OPEN:
        {
                gfs3_open_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_open_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        server_post_open (frame, this, rsp_args,
                                          this_args_cbk->fd);

                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno = gf_errno_to_error
                                     (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_READ:
        {
                gfs3_read_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_read_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (this_args_cbk->op_ret >= 0) {
                        server_post_readv (rsp_args, &this_args_cbk->stat,
                                           this_args_cbk->op_ret);

                        if (!state->rsp_iobref) {
                                state->rsp_iobref = this_args_cbk->iobref;
                                state->rsp_count = 0;
                        }
                        iobref_merge (state->rsp_iobref,
                                      this_args_cbk->iobref);
                        memcpy (&state->rsp_vector[state->rsp_count],
                                this_args_cbk->vector,
                                (this_args_cbk->count *
                                 sizeof(state->rsp_vector[0])));
                        state->rsp_count += this_args_cbk->count;
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno = gf_errno_to_error
                                     (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_WRITE:
        {
                gfs3_write_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_write_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (this_args_cbk->op_ret >= 0) {
                        server_post_writev (rsp_args,
                                              &this_args_cbk->prestat,
                                              &this_args_cbk->poststat);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_STATFS:
        {
                gfs3_statfs_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_statfs_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                if (!this_args_cbk->op_ret) {
                        server_post_statfs (rsp_args,
                                            &this_args_cbk->statvfs);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_FLUSH:
        {
                gf_common_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_flush_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_FSYNC:
        {
                gfs3_fsync_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_fsync_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        server_post_fsync (rsp_args,
                                            &this_args_cbk->prestat,
                                            &this_args_cbk->poststat);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_SETXATTR:
        {
                gf_common_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_setxattr_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_GETXATTR:
        {
                gfs3_getxattr_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_getxattr_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (-1 != this_args_cbk->op_ret) {
                        GF_PROTOCOL_DICT_SERIALIZE (this,
                                                    this_args_cbk->xattr,
                                                    &rsp_args->dict.dict_val,
                                                    rsp_args->dict.dict_len,
                                                    rsp_args->op_errno, out);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_REMOVEXATTR:
        {
                gf_common_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_removexattr_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_OPENDIR:
        {
                gfs3_opendir_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_opendir_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        server_post_opendir (frame, this, rsp_args,
                                          this_args_cbk->fd);

                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno = gf_errno_to_error
                                     (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_FSYNCDIR:
        {
                gf_common_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_fsyncdir_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_ACCESS:
        {
                gf_common_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_access_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_CREATE:
        {
                gfs3_create_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_create_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);

                if (!this_args_cbk->op_ret) {
                        rsp_args->op_ret = server_post_create (frame,
                                                rsp_args, state, this,
                                                this_args_cbk->fd,
                                                this_args_cbk->inode,
                                                &this_args_cbk->stat,
                                                &this_args_cbk->preparent,
                                                &this_args_cbk->postparent);
                        if (rsp_args->op_ret) {
                                rsp_args->op_errno = -rsp_args->op_ret;
                                rsp_args->op_ret = -1;
                        }
                }
                break;
        }
        case GF_FOP_FTRUNCATE:
        {
                gfs3_ftruncate_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_ftruncate_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        server_post_ftruncate (rsp_args,
                                              &this_args_cbk->prestat,
                                              &this_args_cbk->poststat);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_FSTAT:
        {
                gfs3_fstat_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_fstat_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                if (!this_args_cbk->op_ret) {
                        server_post_fstat (rsp_args,
                                          &this_args_cbk->stat);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_LK:
        {
                gfs3_lk_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_lk_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        server_post_lk (this, rsp_args, &this_args_cbk->lock);
                }

                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_LOOKUP:
        {
                gfs3_lookup_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_lookup_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        server_post_lookup (rsp_args, frame, state,
                                            this_args_cbk->inode,
                                            &this_args_cbk->stat,
                                            &this_args_cbk->postparent);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_READDIR:
        {
                gfs3_readdir_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_readdir_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);

                if (this_args_cbk->op_ret > 0) {
                        ret = server_post_readdir (rsp_args,
                                                   &this_args_cbk->entries);
                        if (ret < 0) {
                                rsp_args->op_ret = ret;
                                rsp_args->op_errno = ENOMEM;
                        }
                }
                break;
        }
        case GF_FOP_INODELK:
        {
                gf_common_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_inodelk_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_FINODELK:
        {
                gf_common_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_finodelk_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_ENTRYLK:
        {
                gf_common_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_entrylk_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_FENTRYLK:
        {
                gf_common_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_fentrylk_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_XATTROP:
        {
                gfs3_xattrop_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_xattrop_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        GF_PROTOCOL_DICT_SERIALIZE (this,
                                                    this_args_cbk->xattr,
                                                    &rsp_args->dict.dict_val,
                                                    rsp_args->dict.dict_len,
                                                    rsp_args->op_errno, out);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_FXATTROP:
        {
                gfs3_fxattrop_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_fxattrop_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        GF_PROTOCOL_DICT_SERIALIZE (this,
                                                    this_args_cbk->xattr,
                                                    &rsp_args->dict.dict_val,
                                                    rsp_args->dict.dict_len,
                                                    rsp_args->op_errno, out);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_FGETXATTR:
        {
                gfs3_fgetxattr_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_fgetxattr_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (-1 != this_args_cbk->op_ret) {
                        GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xattr,
                                                    &rsp_args->dict.dict_val,
                                                    rsp_args->dict.dict_len,
                                                    rsp_args->op_errno, out);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_FSETXATTR:
        {
                gf_common_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_setxattr_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_RCHECKSUM:
        {
                gfs3_rchecksum_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_rchecksum_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        server_post_rchecksum (rsp_args,
                                               this_args_cbk->weak_checksum,
                                               this_args_cbk->strong_checksum);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_SETATTR:
        {
                gfs3_setattr_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_setattr_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        server_post_setattr (rsp_args,
                                             &this_args_cbk->prestat,
                                             &this_args_cbk->poststat);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_FSETATTR:
        {
                gfs3_fsetattr_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_fsetattr_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        server_post_fsetattr (rsp_args, &this_args_cbk->prestat,
                                              &this_args_cbk->poststat);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_READDIRP:
        {
                gfs3_readdirp_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_readdirp_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (this_args_cbk->op_ret > 0) {
                        ret = server_post_readdirp (rsp_args,
                                                   &this_args_cbk->entries);
                        if (ret < 0) {
                                rsp_args->op_ret = ret;
                                rsp_args->op_errno = ENOMEM;
                                goto out;
                        }
                        gf_link_inodes_from_dirent (this, state->fd->inode,
                                                    &this_args_cbk->entries);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_FREMOVEXATTR:
        {
                gf_common_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_fremovexattr_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
	case GF_FOP_FALLOCATE:
        {
                gfs3_fallocate_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_fallocate_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        server_post_fallocate (rsp_args,
                                               &this_args_cbk->prestat,
                                               &this_args_cbk->poststat);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
	case GF_FOP_DISCARD:
        {
                gfs3_discard_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_discard_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        server_post_discard (rsp_args,
                                             &this_args_cbk->prestat,
                                             &this_args_cbk->poststat);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_ZEROFILL:
        {
                gfs3_zerofill_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_zerofill_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        server_post_zerofill (rsp_args,
                                              &this_args_cbk->prestat,
                                              &this_args_cbk->poststat);
                }
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_SEEK:
        {
                gfs3_seek_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_seek_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);
                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        case GF_FOP_LEASE:
        {
                gfs3_lease_rsp *rsp_args = NULL;

                rsp_args = &this_rsp->compound_rsp_u.compound_lease_rsp;

                GF_PROTOCOL_DICT_SERIALIZE (this, this_args_cbk->xdata,
                                            &rsp_args->xdata.xdata_val,
                                            rsp_args->xdata.xdata_len,
                                            rsp_args->op_errno, out);

                if (!this_args_cbk->op_ret) {
                        server_post_lease (rsp_args, &this_args_cbk->lease);
                }

                rsp_args->op_ret = this_args_cbk->op_ret;
                rsp_args->op_errno  = gf_errno_to_error
                                      (this_args_cbk->op_errno);
                break;
        }
        default:
                return ENOTSUP;
        }
        op_errno = 0;
out:
        return op_errno;
}
/* This works only when the compound fop acts on one loc/inode/gfid.
 * If compound fops on more than one inode is required, multiple
 * resolve and resumes will have to be done. This will have to change.
 * Right now, multiple unlinks, rmdirs etc is are not supported.
 * This can be added for future enhancements.
 */
int
server_get_compound_resolve (server_state_t *state, gfs3_compound_req *req)
{
        int           i     = 0;
        compound_req *array = &req->compound_req_array.compound_req_array_val[i];

        switch (array->fop_enum) {
        case GF_FOP_STAT:
        {
                gfs3_stat_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_stat_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                break;
        }
        case GF_FOP_READLINK:
        {
                gfs3_readlink_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_readlink_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                break;
        }
        case GF_FOP_MKNOD:
        {
                gfs3_mknod_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_mknod_req;

                state->resolve.type    = RESOLVE_NOT;
                memcpy (state->resolve.pargfid, this_req.pargfid, 16);
                state->resolve.bname = gf_strdup
                                       (this_req.bname);
                break;
        }
        case GF_FOP_MKDIR:
        {
                gfs3_mkdir_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_mkdir_req;

                state->resolve.type    = RESOLVE_NOT;
                memcpy (state->resolve.pargfid, this_req.pargfid, 16);
                state->resolve.bname = gf_strdup
                                       (this_req.bname);
                break;
        }
        case GF_FOP_UNLINK:
        {
                gfs3_unlink_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_unlink_req;

                state->resolve.type    = RESOLVE_MUST;
                memcpy (state->resolve.pargfid, this_req.pargfid, 16);
                state->resolve.bname = gf_strdup
                                       (this_req.bname);
                break;
        }
        case GF_FOP_RMDIR:
        {
                gfs3_rmdir_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_rmdir_req;

                state->resolve.type    = RESOLVE_MUST;
                memcpy (state->resolve.pargfid, this_req.pargfid, 16);
                state->resolve.bname = gf_strdup
                                       (this_req.bname);
                break;
        }
        case GF_FOP_SYMLINK:
        {
                gfs3_symlink_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_symlink_req;

                state->resolve.type   = RESOLVE_NOT;
                memcpy (state->resolve.pargfid, this_req.pargfid, 16);
                state->resolve.bname = gf_strdup
                                       (this_req.bname);
                break;
        }
        case GF_FOP_RENAME:
        {
                gfs3_rename_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_rename_req;

                state->resolve.type   = RESOLVE_MUST;
                state->resolve.bname = gf_strdup
                                       (this_req.oldbname);
                memcpy (state->resolve.pargfid, this_req.oldgfid, 16);

                state->resolve2.type  = RESOLVE_MAY;
                state->resolve2.bname = gf_strdup
                                       (this_req.newbname);
                memcpy (state->resolve2.pargfid, this_req.newgfid, 16);
                break;
        }
        case GF_FOP_LINK:
        {
                gfs3_link_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_link_req;

                state->resolve.type    = RESOLVE_MUST;
                memcpy (state->resolve.gfid, this_req.oldgfid, 16);

                state->resolve2.type   = RESOLVE_NOT;
                state->resolve2.bname = gf_strdup
                                       (this_req.newbname);
                memcpy (state->resolve2.pargfid, this_req.newgfid, 16);
                break;
        }
        case GF_FOP_TRUNCATE:
        {
                gfs3_truncate_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_truncate_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                break;
        }
        case GF_FOP_OPEN:
        {
                gfs3_open_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_open_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                break;
        }
        case GF_FOP_READ:
        {
                gfs3_read_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_read_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                break;
        }
        case GF_FOP_WRITE:
        {
                gfs3_write_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_write_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                break;
        }
        case GF_FOP_STATFS:
        {
                gfs3_statfs_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_statfs_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                break;
        }
        case GF_FOP_FLUSH:
        {
                gfs3_flush_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_flush_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_FSYNC:
        {
                gfs3_fsync_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_fsync_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_SETXATTR:
        {
                gfs3_setxattr_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_setxattr_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                break;
        }
        case GF_FOP_GETXATTR:
        {
                gfs3_getxattr_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_getxattr_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                break;
        }
        case GF_FOP_REMOVEXATTR:
        {
                gfs3_removexattr_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_removexattr_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                break;
        }
        case GF_FOP_OPENDIR:
        {
                gfs3_opendir_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_opendir_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                break;
        }
        case GF_FOP_FSYNCDIR:
        {
                gfs3_fsyncdir_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_fsyncdir_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_ACCESS:
        {
                gfs3_access_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_access_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                break;
        }
        case GF_FOP_CREATE:
        {
                gfs3_create_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_create_req;

                state->flags = gf_flags_to_flags (this_req.flags);
                if (state->flags & O_EXCL) {
                        state->resolve.type = RESOLVE_NOT;
                } else {
                        state->resolve.type = RESOLVE_DONTCARE;
                }

                memcpy (state->resolve.pargfid, this_req.pargfid, 16);
                state->resolve.bname = gf_strdup
                                       (this_req.bname);
                break;
        }
        case GF_FOP_FTRUNCATE:
        {
                gfs3_ftruncate_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_ftruncate_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_FSTAT:
        {
                gfs3_fstat_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_fstat_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_LK:
        {
                gfs3_lk_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_lk_req;

                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_LOOKUP:
        {
                gfs3_lookup_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_lookup_req;
                state->resolve.type   = RESOLVE_DONTCARE;

                if (this_req.bname && strcmp (this_req.bname, "")) {
                        memcpy (state->resolve.pargfid, this_req.pargfid, 16);
                        state->resolve.bname = gf_strdup
                                               (this_req.bname);
                } else {
                        memcpy (state->resolve.gfid, this_req.gfid, 16);
                }
                break;
        }
        case GF_FOP_READDIR:
        {
                gfs3_readdir_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_readdir_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_INODELK:
        {
                gfs3_inodelk_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_inodelk_req;

                state->resolve.type  = RESOLVE_EXACT;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                break;
        }
        case GF_FOP_FINODELK:
        {
                gfs3_finodelk_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_finodelk_req;

                state->resolve.type  = RESOLVE_EXACT;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_ENTRYLK:
        {
                gfs3_entrylk_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_entrylk_req;

                state->resolve.type  = RESOLVE_EXACT;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                break;
        }
        case GF_FOP_FENTRYLK:
        {
                gfs3_fentrylk_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_fentrylk_req;

                state->resolve.type  = RESOLVE_EXACT;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_XATTROP:
        {
                gfs3_xattrop_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_xattrop_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                break;
        }
        case GF_FOP_FXATTROP:
        {
                gfs3_fxattrop_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_fxattrop_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_FGETXATTR:
        {
                gfs3_fgetxattr_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_fgetxattr_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_FSETXATTR:
        {
                gfs3_fsetxattr_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_fsetxattr_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_RCHECKSUM:
        {
                gfs3_rchecksum_req this_req = {0,};

                this_req = array[i].compound_req_u.compound_rchecksum_req;

                state->resolve.type  = RESOLVE_MAY;
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_SETATTR:
        {
                gfs3_setattr_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_setattr_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                break;
        }
        case GF_FOP_FSETATTR:
        {
                gfs3_fsetattr_req this_req = {0,};

                this_req = array[i].compound_req_u.compound_fsetattr_req;

                state->resolve.type  = RESOLVE_MUST;
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_READDIRP:
        {
                gfs3_readdirp_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_readdirp_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_FREMOVEXATTR:
        {
                gfs3_fremovexattr_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_fremovexattr_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_FALLOCATE:
        {
                gfs3_fallocate_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_fallocate_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_DISCARD:
        {
                gfs3_discard_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_discard_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_ZEROFILL:
        {
                gfs3_zerofill_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_zerofill_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_SEEK:
        {
                gfs3_seek_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_seek_req;

                state->resolve.type  = RESOLVE_MUST;
                memcpy (state->resolve.gfid,
                        this_req.gfid, 16);
                state->resolve.fd_no = this_req.fd;
                break;
        }
        case GF_FOP_LEASE:
        {
                gfs3_lease_req this_req = { {0,} };

                this_req = array[i].compound_req_u.compound_lease_req;

                state->resolve.type = RESOLVE_MUST;
                memcpy (state->resolve.gfid, this_req.gfid, 16);
                break;
        }
        default:
                return ENOTSUP;
        }
        return 0;
}

void
server_compound_rsp_cleanup (gfs3_compound_rsp *rsp, compound_args_cbk_t *args)
{
        int                     i, len          = 0;
        compound_rsp            *this_rsp       = NULL;

        if (!rsp->compound_rsp_array.compound_rsp_array_val)
                return;

        len = rsp->compound_rsp_array.compound_rsp_array_len;

        for (i = 0; i < len; i++) {
                this_rsp = &rsp->compound_rsp_array.compound_rsp_array_val[i];
                switch (args->enum_list[i]) {
                case GF_FOP_STAT:
                        SERVER_FOP_RSP_CLEANUP (rsp, stat, i);
                        break;
                case GF_FOP_MKNOD:
                        SERVER_FOP_RSP_CLEANUP (rsp, mknod, i);
                        break;
                case GF_FOP_MKDIR:
                        SERVER_FOP_RSP_CLEANUP (rsp, mkdir, i);
                        break;
                case GF_FOP_UNLINK:
                        SERVER_FOP_RSP_CLEANUP (rsp, unlink, i);
                        break;
                case GF_FOP_RMDIR:
                        SERVER_FOP_RSP_CLEANUP (rsp, rmdir, i);
                        break;
                case GF_FOP_SYMLINK:
                        SERVER_FOP_RSP_CLEANUP (rsp, symlink, i);
                        break;
                case GF_FOP_RENAME:
                        SERVER_FOP_RSP_CLEANUP (rsp, rename, i);
                        break;
                case GF_FOP_LINK:
                        SERVER_FOP_RSP_CLEANUP (rsp, link, i);
                        break;
                case GF_FOP_TRUNCATE:
                        SERVER_FOP_RSP_CLEANUP (rsp, truncate, i);
                        break;
                case GF_FOP_OPEN:
                        SERVER_FOP_RSP_CLEANUP (rsp, open, i);
                        break;
                case GF_FOP_READ:
                        SERVER_FOP_RSP_CLEANUP (rsp, read, i);
                        break;
                case GF_FOP_WRITE:
                        SERVER_FOP_RSP_CLEANUP (rsp, write, i);
                        break;
                case GF_FOP_STATFS:
                        SERVER_FOP_RSP_CLEANUP (rsp, statfs, i);
                        break;
                case GF_FOP_FSYNC:
                        SERVER_FOP_RSP_CLEANUP (rsp, fsync, i);
                        break;
                case GF_FOP_OPENDIR:
                        SERVER_FOP_RSP_CLEANUP (rsp, opendir, i);
                        break;
                case GF_FOP_CREATE:
                        SERVER_FOP_RSP_CLEANUP (rsp, create, i);
                        break;
                case GF_FOP_FTRUNCATE:
                        SERVER_FOP_RSP_CLEANUP (rsp, ftruncate, i);
                        break;
                case GF_FOP_FSTAT:
                        SERVER_FOP_RSP_CLEANUP (rsp, fstat, i);
                        break;
                case GF_FOP_LK:
                        SERVER_FOP_RSP_CLEANUP (rsp, lk, i);
                        break;
                case GF_FOP_LOOKUP:
                        SERVER_FOP_RSP_CLEANUP (rsp, lookup, i);
                        break;
                case GF_FOP_SETATTR:
                        SERVER_FOP_RSP_CLEANUP (rsp, setattr, i);
                        break;
                case GF_FOP_FSETATTR:
                        SERVER_FOP_RSP_CLEANUP (rsp, fsetattr, i);
                        break;
                case GF_FOP_FALLOCATE:
                        SERVER_FOP_RSP_CLEANUP (rsp, fallocate, i);
                        break;
                case GF_FOP_DISCARD:
                        SERVER_FOP_RSP_CLEANUP (rsp, discard, i);
                        break;
                case GF_FOP_ZEROFILL:
                        SERVER_FOP_RSP_CLEANUP (rsp, zerofill, i);
                        break;
                case GF_FOP_IPC:
                        SERVER_FOP_RSP_CLEANUP (rsp, ipc, i);
                        break;
                case GF_FOP_SEEK:
                        SERVER_FOP_RSP_CLEANUP (rsp, seek, i);
                        break;
                case GF_FOP_LEASE:
                        SERVER_FOP_RSP_CLEANUP (rsp, lease, i);
                        break;
                /* fops that use gf_common_rsp */
                case GF_FOP_FLUSH:
                        SERVER_COMMON_RSP_CLEANUP (rsp, flush, i);
                        break;
                case GF_FOP_SETXATTR:
                        SERVER_COMMON_RSP_CLEANUP (rsp, setxattr, i);
                        break;
                case GF_FOP_REMOVEXATTR:
                        SERVER_COMMON_RSP_CLEANUP (rsp, removexattr, i);
                        break;
                case GF_FOP_FSETXATTR:
                        SERVER_COMMON_RSP_CLEANUP (rsp, fsetxattr, i);
                        break;
                case GF_FOP_FREMOVEXATTR:
                        SERVER_COMMON_RSP_CLEANUP (rsp, fremovexattr, i);
                        break;
                case GF_FOP_FSYNCDIR:
                        SERVER_COMMON_RSP_CLEANUP (rsp, fsyncdir, i);
                        break;
                case GF_FOP_ACCESS:
                        SERVER_COMMON_RSP_CLEANUP (rsp, access, i);
                        break;
                case GF_FOP_INODELK:
                        SERVER_COMMON_RSP_CLEANUP (rsp, inodelk, i);
                        break;
                case GF_FOP_FINODELK:
                        SERVER_COMMON_RSP_CLEANUP (rsp, finodelk, i);
                        break;
                case GF_FOP_ENTRYLK:
                        SERVER_COMMON_RSP_CLEANUP (rsp, entrylk, i);
                        break;
                case GF_FOP_FENTRYLK:
                        SERVER_COMMON_RSP_CLEANUP (rsp, fentrylk, i);
                        break;
                case GF_FOP_READLINK:
                        SERVER_FOP_RSP_CLEANUP (rsp, readlink, i);
                        break;
                case GF_FOP_RCHECKSUM:
                        SERVER_FOP_RSP_CLEANUP (rsp, rchecksum, i);
                        break;
                /* fops that need extra cleanup */
                case GF_FOP_XATTROP:
                {
                        gfs3_xattrop_rsp *tmp_rsp = &CPD_RSP_FIELD(this_rsp,
                                                    xattrop);
                        SERVER_FOP_RSP_CLEANUP (rsp, xattrop, i);
                        GF_FREE (tmp_rsp->dict.dict_val);
                        break;
                }
                case GF_FOP_FXATTROP:
                {
                        gfs3_fxattrop_rsp *tmp_rsp = &CPD_RSP_FIELD(this_rsp,
                                                     fxattrop);
                        SERVER_FOP_RSP_CLEANUP (rsp, fxattrop, i);
                        GF_FREE (tmp_rsp->dict.dict_val);
                        break;
                }
                case GF_FOP_READDIR:
                {
                        gfs3_readdir_rsp *tmp_rsp = &CPD_RSP_FIELD(this_rsp,
                                                    readdir);
                        SERVER_FOP_RSP_CLEANUP (rsp, readdir, i);
                        readdir_rsp_cleanup (tmp_rsp);
                        break;
                }
                case GF_FOP_READDIRP:
                {
                        gfs3_readdirp_rsp *tmp_rsp = &CPD_RSP_FIELD(this_rsp,
                                                     readdirp);
                        SERVER_FOP_RSP_CLEANUP (rsp, readdir, i);
                        readdirp_rsp_cleanup (tmp_rsp);
                        break;
                }
                case GF_FOP_GETXATTR:
                {
                        gfs3_getxattr_rsp *tmp_rsp = &CPD_RSP_FIELD(this_rsp,
                                                     getxattr);
                        SERVER_FOP_RSP_CLEANUP (rsp, getxattr, i);
                        GF_FREE (tmp_rsp->dict.dict_val);
                        break;
                }
                case GF_FOP_FGETXATTR:
                {
                        gfs3_fgetxattr_rsp *tmp_rsp = &CPD_RSP_FIELD(this_rsp,
                                                      fgetxattr);
                        SERVER_FOP_RSP_CLEANUP (rsp, fgetxattr, i);
                        GF_FREE (tmp_rsp->dict.dict_val);
                        break;
                }
                default:
                        break;
                }
        }
        GF_FREE (rsp->compound_rsp_array.compound_rsp_array_val);
        return;
}

void
server_compound_req_cleanup (gfs3_compound_req *req, int len)
{
        int             i        = 0;
        compound_req   *curr_req = NULL;


        if (!req->compound_req_array.compound_req_array_val)
                return;

        for (i = 0; i < len; i++) {
                curr_req = &req->compound_req_array.compound_req_array_val[i];

                switch (curr_req->fop_enum) {
                case GF_FOP_STAT:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, stat);
                        break;
                case GF_FOP_READLINK:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, readlink);
                        break;
                case GF_FOP_MKNOD:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, mknod);
                        break;
                case GF_FOP_MKDIR:
                {
                        gfs3_mkdir_req *args = &CPD_REQ_FIELD (curr_req, mkdir);

                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, mkdir);
                        free (args->bname);
                        break;
                }
                case GF_FOP_UNLINK:
                {
                        gfs3_unlink_req *args = &CPD_REQ_FIELD (curr_req,
                                                unlink);

                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, unlink);
                        free (args->bname);
                        break;
                }
                case GF_FOP_RMDIR:
                {
                        gfs3_rmdir_req *args = &CPD_REQ_FIELD (curr_req,
                                               rmdir);

                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, rmdir);
                        free (args->bname);
                        break;
                }
                case GF_FOP_SYMLINK:
                {
                        gfs3_symlink_req *args = &CPD_REQ_FIELD (curr_req,
                                                 symlink);

                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, symlink);
                        free (args->bname);
                        free (args->linkname);
                        break;
                }
                case GF_FOP_RENAME:
                {
                        gfs3_rename_req *args = &CPD_REQ_FIELD (curr_req,
                                                rename);

                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, rename);
                        free (args->oldbname);
                        free (args->newbname);
                        break;
                }
                case GF_FOP_LINK:
                {
                        gfs3_link_req *args = &CPD_REQ_FIELD (curr_req,
                                              link);

                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, link);
                        free (args->newbname);
                        break;
                }
                case GF_FOP_TRUNCATE:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, truncate);
                        break;
                case GF_FOP_OPEN:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, open);
                        break;
                case GF_FOP_READ:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, read);
                        break;
                case GF_FOP_WRITE:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, write);
                        break;
                case GF_FOP_STATFS:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, statfs);
                        break;
                case GF_FOP_FLUSH:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, flush);
                        break;
                case GF_FOP_FSYNC:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, fsync);
                        break;
                case GF_FOP_SETXATTR:
                {
                        gfs3_setxattr_req *args = &CPD_REQ_FIELD (curr_req,
                                                  setxattr);

                        free (args->dict.dict_val);
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, setxattr);
                        break;
                }
                case GF_FOP_GETXATTR:
                {
                        gfs3_getxattr_req *args = &CPD_REQ_FIELD (curr_req,
                                                  getxattr);

                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, getxattr);
                        free (args->name);
                        break;
                }
                case GF_FOP_REMOVEXATTR:
                {
                        gfs3_removexattr_req *args = &CPD_REQ_FIELD (curr_req,
                                                     removexattr);

                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, removexattr);
                        free (args->name);
                        break;
                }
                case GF_FOP_OPENDIR:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, opendir);
                        break;
                case GF_FOP_FSYNCDIR:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, fsyncdir);
                        break;
                case GF_FOP_ACCESS:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, access);
                        break;
                case GF_FOP_CREATE:
                {
                        gfs3_create_req *args = &CPD_REQ_FIELD (curr_req,
                                                create);

                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, create);
                        free (args->bname);
                        break;
                }
                case GF_FOP_FTRUNCATE:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, ftruncate);
                        break;
                case GF_FOP_FSTAT:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, fstat);
                        break;
                case GF_FOP_LK:
                {
                        gfs3_lk_req *args = &CPD_REQ_FIELD (curr_req, lk);

                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, lk);
                        free (args->flock.lk_owner.lk_owner_val);
                        break;
                }
                case GF_FOP_LOOKUP:
                {
                        gfs3_lookup_req *args = &CPD_REQ_FIELD (curr_req,
                                                                lookup);

                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, lookup);
                        free (args->bname);
                        break;
                }
                case GF_FOP_READDIR:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, readdir);
                        break;
                case GF_FOP_INODELK:
                {
                        gfs3_inodelk_req *args = &CPD_REQ_FIELD (curr_req,
                                                                 inodelk);

                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, inodelk);
                        free (args->volume);
                        free (args->flock.lk_owner.lk_owner_val);
                        break;
                }
                case GF_FOP_FINODELK:
                {
                        gfs3_finodelk_req *args = &CPD_REQ_FIELD (curr_req,
                                                                  finodelk);

                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, finodelk);
                        free (args->volume);
                        free (args->flock.lk_owner.lk_owner_val);
                        break;
                }
                case GF_FOP_ENTRYLK:
                {
                        gfs3_entrylk_req *args = &CPD_REQ_FIELD (curr_req,
                                                                 entrylk);

                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, entrylk);
                        free (args->volume);
                        free (args->name);
                        break;
                }
                case GF_FOP_FENTRYLK:
                {
                        gfs3_fentrylk_req *args = &CPD_REQ_FIELD (curr_req,
                                                                  fentrylk);

                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, fentrylk);
                        free (args->volume);
                        free (args->name);
                        break;
                }
                case GF_FOP_XATTROP:
                {
                        gfs3_xattrop_req *args = &CPD_REQ_FIELD (curr_req,
                                                                 xattrop);

                        free (args->dict.dict_val);
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, xattrop);
                        break;
                }
                case GF_FOP_FXATTROP:
                {
                        gfs3_fxattrop_req *args = &CPD_REQ_FIELD (curr_req,
                                                                  fxattrop);

                        free (args->dict.dict_val);
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, fxattrop);
                        break;
                }
                case GF_FOP_FGETXATTR:
                {
                        gfs3_fgetxattr_req *args = &CPD_REQ_FIELD (curr_req,
                                                   fgetxattr);

                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, fgetxattr);
                        free (args->name);
                        break;
                }
                case GF_FOP_FSETXATTR:
                {
                        gfs3_fsetxattr_req *args = &CPD_REQ_FIELD(curr_req,
                                                   fsetxattr);

                        free (args->dict.dict_val);
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, fsetxattr);
                        break;
                }
                case GF_FOP_RCHECKSUM:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, rchecksum);
                        break;
                case GF_FOP_SETATTR:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, setattr);
                        break;
                case GF_FOP_FSETATTR:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, fsetattr);
                        break;
                case GF_FOP_READDIRP:
                {
                        gfs3_readdirp_req *args = &CPD_REQ_FIELD (curr_req,
                                                  readdirp);

                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, fremovexattr);
                        free (args->dict.dict_val);
                        break;
                }
                case GF_FOP_FREMOVEXATTR:
                {
                        gfs3_fremovexattr_req *args = &CPD_REQ_FIELD(curr_req,
                                                      fremovexattr);

                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, fremovexattr);
                        free (args->name);
                        break;
                }
                case GF_FOP_FALLOCATE:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, fallocate);
                        break;
                case GF_FOP_DISCARD:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, discard);
                        break;
                case GF_FOP_ZEROFILL:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, zerofill);
                        break;
                case GF_FOP_IPC:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, ipc);
                        break;
                case GF_FOP_SEEK:
                        SERVER_COMPOUND_FOP_CLEANUP (curr_req, seek);
                        break;
                default:
                        break;
                }
        }

        return;
}
