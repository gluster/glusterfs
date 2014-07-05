/*
  Copyright (c) 2010-2013 Red Hat, Inc. <http://www.redhat.com>
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

#include "server.h"
#include "server-helpers.h"
#include "gidcache.h"

#include <fnmatch.h>
#include <pwd.h>
#include <grp.h>

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
                gf_log("gid-cache", GF_LOG_ERROR, "getpwuid_r(%u) failed",
                       root->uid);
                return -1;
        }

        if (!result) {
                gf_log ("gid-cache", GF_LOG_ERROR, "getpwuid_r(%u) found "
                        "nothing", root->uid);
                return -1;
        }

        gf_log ("gid-cache", GF_LOG_TRACE, "mapped %u => %s", root->uid,
                result->pw_name);

        ngroups = GF_MAX_AUX_GROUPS;
        ret = getgrouplist (result->pw_name, root->gid, mygroups, &ngroups);
        if (ret == -1) {
                gf_log ("gid-cache", GF_LOG_ERROR, "could not map %s to group "
                        "list (%d gids)", result->pw_name, root->ngrps);
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
                                gf_log (this->name, GF_LOG_INFO,
                                        "fd cleanup on %s", path);
                                GF_FREE (path);
                        }  else {

                                gf_log (this->name, GF_LOG_INFO,
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
                gf_log (this->name, GF_LOG_INFO, "server_ctx_get() failed");
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

        if (fdentries != NULL)
                ret = do_fd_cleanup (this, client, fdentries, fd_count);
        else
                gf_log (this->name, GF_LOG_INFO, "no fdentries to clean");

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
        server_conf_t *conf = NULL;

        GF_VALIDATE_OR_GOTO ("server", req, out);

        client = req->trans->xl_private;

        frame = server_alloc_frame (req);
        if (!frame)
                goto out;

        frame->root->op       = req->procnum;

        frame->root->unique   = req->xid;

        frame->root->uid      = req->uid;
        frame->root->gid      = req->gid;
        frame->root->pid      = req->pid;
        gf_client_ref (client);
        frame->root->client   = client;
        frame->root->lk_owner = req->lk_owner;

        conf = frame->this->private;
        if (conf->server_manage_gids)
            server_resolve_groups (frame, req);
        else
            server_decode_groups (frame, req);

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
                        gf_log (this->name, GF_LOG_WARNING,
                                "wrong value for 'verify-volfile-checksum', "
                                "Neglecting option");
                }
        }

        data = dict_get (this->options, "trace");
        if (data) {
                ret = gf_string2boolean (data->data, &conf->trace);
                if (ret != 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "'trace' takes on only boolean values. "
                                "Neglecting option");
                }
        }

        /* TODO: build_rpc_config (); */
        ret = dict_get_int32 (this->options, "limits.transaction-size",
                              &conf->rpc_conf.max_block_size);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "defaulting limits.transaction-size to %d",
                        DEFAULT_BLOCK_SIZE);
                conf->rpc_conf.max_block_size = DEFAULT_BLOCK_SIZE;
        }

        data = dict_get (this->options, "config-directory");
        if (data) {
                /* Check whether the specified directory exists,
                   or directory specified is non standard */
                ret = stat (data->data, &buf);
                if ((ret != 0) || !S_ISDIR (buf.st_mode)) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "Directory '%s' doesn't exist, exiting.",
                                data->data);
                        ret = -1;
                        goto out;
                }
                /* Make sure that conf-dir doesn't contain ".." in path */
                if ((gf_strstr (data->data, "/", "..")) == -1) {
                        ret = -1;
                        gf_log (this->name, GF_LOG_ERROR,
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

        gf_log (this->name, GF_LOG_INFO,
                "%s%s => (%d, %d)%s",
                op, caller, op_ret, op_errno, fdstr);
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

        gf_log (this->name, GF_LOG_INFO,
                "%s%s%s%s%s%s%s",
                op, caller,
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
                        if (trav->dict.dict_len < 0) {
                                gf_log (THIS->name, GF_LOG_ERROR,
                                        "failed to get serialized length "
                                        "of reply dict");
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
                                gf_log (THIS->name, GF_LOG_ERROR,
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
                                gf_log ("mount-point-list", GF_LOG_INFO,
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
                gf_log ("stats", GF_LOG_INFO,
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
                gf_log (THIS->name, GF_LOG_ERROR,
                        "Invalid arguments to cancel connection timer");
                return cancelled;
        }

        serv_ctx = server_ctx_get (client, client->this);

        if (serv_ctx == NULL) {
                gf_log (this->name, GF_LOG_INFO, "server_ctx_get() failed");
                goto out;
        }

        LOCK (&serv_ctx->fdtable_lock);
        {
                if (serv_ctx->grace_timer) {
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
