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


int
server_resolve_all (call_frame_t *frame);
int
resolve_entry_simple (call_frame_t *frame);
int
resolve_inode_simple (call_frame_t *frame);
int
resolve_continue (call_frame_t *frame);
int
resolve_anonfd_simple (call_frame_t *frame);

int
resolve_loc_touchup (call_frame_t *frame)
{
        server_state_t       *state = NULL;
        server_resolve_t     *resolve = NULL;
        loc_t                *loc = NULL;
        char                 *path = NULL;
        int                   ret = 0;

        state = CALL_STATE (frame);

        resolve = state->resolve_now;
        loc     = state->loc_now;

        if (!loc->path) {
                if (loc->parent && resolve->bname) {
                        ret = inode_path (loc->parent, resolve->bname, &path);
                } else if (loc->inode) {
                        ret = inode_path (loc->inode, NULL, &path);
                }
                if (ret)
                        gf_log (frame->this->name, GF_LOG_TRACE,
                                "return value inode_path %d", ret);
                loc->path = path;
        }

        return 0;
}


int
resolve_gfid_entry_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, inode_t *inode,
                        struct iatt *buf, dict_t *xdata,
                        struct iatt *postparent)
{
        server_state_t       *state = NULL;
        server_resolve_t     *resolve = NULL;
        inode_t              *link_inode = NULL;
        loc_t                *resolve_loc = NULL;

        state = CALL_STATE (frame);
        resolve = state->resolve_now;
        resolve_loc = &resolve->resolve_loc;

        if (op_ret == -1) {
                gf_log (this->name, ((op_errno == ENOENT) ? GF_LOG_DEBUG :
                                     GF_LOG_WARNING),
                        "%s/%s: failed to resolve (%s)",
                        uuid_utoa (resolve_loc->pargfid), resolve_loc->name,
                        strerror (op_errno));
                goto out;
        }

        link_inode = inode_link (inode, resolve_loc->parent,
                                 resolve_loc->name, buf);

        if (!link_inode)
                goto out;

        inode_lookup (link_inode);

        inode_unref (link_inode);

out:
        loc_wipe (resolve_loc);

        resolve_continue (frame);
        return 0;
}


int
resolve_gfid_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno, inode_t *inode, struct iatt *buf,
                  dict_t *xdata, struct iatt *postparent)
{
        server_state_t       *state = NULL;
        server_resolve_t     *resolve = NULL;
        inode_t              *link_inode = NULL;
        loc_t                *resolve_loc = NULL;

        state = CALL_STATE (frame);
        resolve = state->resolve_now;
        resolve_loc = &resolve->resolve_loc;

        if (op_ret == -1) {
                gf_log (this->name, ((op_errno == ENOENT) ? GF_LOG_DEBUG :
                                     GF_LOG_WARNING),
                        "%s: failed to resolve (%s)",
                        uuid_utoa (resolve_loc->gfid), strerror (op_errno));
                loc_wipe (&resolve->resolve_loc);
                goto out;
        }

        loc_wipe (resolve_loc);

        link_inode = inode_link (inode, NULL, NULL, buf);

        if (!link_inode)
                goto out;

        inode_lookup (link_inode);

        if (uuid_is_null (resolve->pargfid)) {
                inode_unref (link_inode);
                goto out;
        }

        resolve_loc->parent = link_inode;
        uuid_copy (resolve_loc->pargfid, resolve_loc->parent->gfid);

        resolve_loc->name = resolve->bname;

        resolve_loc->inode = inode_new (state->itable);
        inode_path (resolve_loc->parent, resolve_loc->name,
                    (char **) &resolve_loc->path);

        STACK_WIND (frame, resolve_gfid_entry_cbk,
                    frame->root->client->bound_xl,
                    frame->root->client->bound_xl->fops->lookup,
                    &resolve->resolve_loc, NULL);
        return 0;
out:
        resolve_continue (frame);
        return 0;
}


int
resolve_gfid (call_frame_t *frame)
{
        server_state_t       *state = NULL;
        xlator_t             *this = NULL;
        server_resolve_t     *resolve = NULL;
        loc_t                *resolve_loc = NULL;
        int                   ret = 0;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;
        resolve_loc = &resolve->resolve_loc;

        if (!uuid_is_null (resolve->pargfid))
                uuid_copy (resolve_loc->gfid, resolve->pargfid);
        else if (!uuid_is_null (resolve->gfid))
                uuid_copy (resolve_loc->gfid, resolve->gfid);

        resolve_loc->inode = inode_new (state->itable);
        ret = loc_path (resolve_loc, NULL);

        STACK_WIND (frame, resolve_gfid_cbk,
                    frame->root->client->bound_xl,
                    frame->root->client->bound_xl->fops->lookup,
                    &resolve->resolve_loc, NULL);
        return 0;
}

int
resolve_continue (call_frame_t *frame)
{
        server_state_t       *state = NULL;
        xlator_t             *this = NULL;
        server_resolve_t     *resolve = NULL;
        int                   ret = 0;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;

        resolve->op_ret   = 0;
        resolve->op_errno = 0;

        if (resolve->fd_no != -1) {
                ret = resolve_anonfd_simple (frame);
                goto out;
        } else if (!uuid_is_null (resolve->pargfid))
                ret = resolve_entry_simple (frame);
        else if (!uuid_is_null (resolve->gfid))
                ret = resolve_inode_simple (frame);
        if (ret)
                gf_log (this->name, GF_LOG_DEBUG,
                        "return value of resolve_*_simple %d", ret);

        resolve_loc_touchup (frame);
out:
        server_resolve_all (frame);

        return 0;
}


/*
  Check if the requirements are fulfilled by entries in the inode cache itself
  Return value:
  <= 0 - simple resolution was decisive and complete (either success or failure)
  > 0  - indecisive, need to perform deep resolution
*/

int
resolve_entry_simple (call_frame_t *frame)
{
        server_state_t     *state = NULL;
        xlator_t           *this = NULL;
        server_resolve_t   *resolve = NULL;
        inode_t            *parent = NULL;
        inode_t            *inode = NULL;
        int                 ret = 0;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;

        parent = inode_find (state->itable, resolve->pargfid);
        if (!parent) {
                /* simple resolution is indecisive. need to perform
                   deep resolution */
                resolve->op_ret   = -1;
                resolve->op_errno = ESTALE;
                ret = 1;
                goto out;
        }

        /* expected @parent was found from the inode cache */
        uuid_copy (state->loc_now->pargfid, resolve->pargfid);
        state->loc_now->parent = inode_ref (parent);
        state->loc_now->name = resolve->bname;

        inode = inode_grep (state->itable, parent, resolve->bname);
        if (!inode) {
                switch (resolve->type) {
                case RESOLVE_DONTCARE:
                case RESOLVE_NOT:
                        ret = 0;
                        break;
                case RESOLVE_MAY:
                        ret = 1;
                        break;
                default:
                        resolve->op_ret   = -1;
                        resolve->op_errno = ENOENT;
                        ret = 1;
                        break;
                }

                goto out;
        }

        if (resolve->type == RESOLVE_NOT) {
                gf_log (this->name, GF_LOG_DEBUG, "inode (pointer: %p gfid:%s"
                        " found for path (%s) while type is RESOLVE_NOT",
                        inode, uuid_utoa (inode->gfid), resolve->path);
                resolve->op_ret   = -1;
                resolve->op_errno = EEXIST;
                ret = -1;
                goto out;
        }

        ret = 0;

        state->loc_now->inode  = inode_ref (inode);

out:
        if (parent)
                inode_unref (parent);

        if (inode)
                inode_unref (inode);

        return ret;
}


int
server_resolve_entry (call_frame_t *frame)
{
        server_state_t     *state = NULL;
        int                 ret = 0;
        loc_t              *loc = NULL;

        state = CALL_STATE (frame);
        loc  = state->loc_now;

        ret = resolve_entry_simple (frame);

        if (ret > 0) {
                loc_wipe (loc);
                resolve_gfid (frame);
                return 0;
        }

        if (ret == 0)
                resolve_loc_touchup (frame);

        server_resolve_all (frame);

        return 0;
}


int
resolve_inode_simple (call_frame_t *frame)
{
        server_state_t     *state = NULL;
        server_resolve_t   *resolve = NULL;
        inode_t            *inode = NULL;
        int                 ret = 0;

        state = CALL_STATE (frame);
        resolve = state->resolve_now;
        inode = inode_find (state->itable, resolve->gfid);

        if (!inode) {
                resolve->op_ret   = -1;
                resolve->op_errno = ESTALE;
                ret = 1;
                goto out;
        }

        ret = 0;

        state->loc_now->inode = inode_ref (inode);
        uuid_copy (state->loc_now->gfid, resolve->gfid);

out:
        if (inode)
                inode_unref (inode);

        return ret;
}


int
server_resolve_inode (call_frame_t *frame)
{
        server_state_t     *state = NULL;
        int                 ret = 0;
        loc_t              *loc = NULL;

        state = CALL_STATE (frame);
        loc  = state->loc_now;

        ret = resolve_inode_simple (frame);

        if (ret > 0) {
                loc_wipe (loc);
                resolve_gfid (frame);
                return 0;
        }

        if (ret == 0)
                resolve_loc_touchup (frame);

        server_resolve_all (frame);

        return 0;
}


int
resolve_anonfd_simple (call_frame_t *frame)
{
        server_state_t     *state = NULL;
        server_resolve_t   *resolve = NULL;
        inode_t            *inode = NULL;
        int                 ret = 0;

        state = CALL_STATE (frame);
        resolve = state->resolve_now;

        inode = inode_find (state->itable, resolve->gfid);

        if (!inode) {
                resolve->op_ret   = -1;
                resolve->op_errno = ENOENT;
                ret = 1;
                goto out;
        }

        ret = 0;

        state->fd = fd_anonymous (inode);
out:
        if (inode)
                inode_unref (inode);

        if (ret != 0)
                gf_log ("server", GF_LOG_WARNING, "inode for the gfid (%s) is "
                        "not found. anonymous fd creation failed",
                        uuid_utoa (resolve->gfid));
        return ret;
}


int
server_resolve_anonfd (call_frame_t *frame)
{
        server_state_t     *state = NULL;
        int                 ret = 0;
        loc_t              *loc = NULL;

        state = CALL_STATE (frame);
        loc  = state->loc_now;

        ret = resolve_anonfd_simple (frame);

        if (ret > 0) {
                loc_wipe (loc);
                resolve_gfid (frame);
                return 0;
        }

        server_resolve_all (frame);

        return 0;

}


int
server_resolve_fd (call_frame_t *frame)
{
        server_ctx_t         *serv_ctx = NULL;
        server_state_t       *state    = NULL;
        client_t             *client   = NULL;
        server_resolve_t     *resolve  = NULL;
        uint64_t              fd_no    = -1;

        state = CALL_STATE (frame);
        resolve = state->resolve_now;

        fd_no = resolve->fd_no;

        if (fd_no == GF_ANON_FD_NO) {
                server_resolve_anonfd (frame);
                return 0;
        }

        client = frame->root->client;

        serv_ctx = server_ctx_get (client, client->this);

        if (serv_ctx == NULL) {
                gf_log ("", GF_LOG_INFO, "server_ctx_get() failed");
                resolve->op_ret   = -1;
                resolve->op_errno = ENOMEM;
                return 0;
        }

        state->fd = gf_fd_fdptr_get (serv_ctx->fdtable, fd_no);

        if (!state->fd) {
                gf_log ("", GF_LOG_INFO, "fd not found in context");
                resolve->op_ret   = -1;
                resolve->op_errno = EBADF;
        }

        server_resolve_all (frame);

        return 0;
}


int
server_resolve (call_frame_t *frame)
{
        server_state_t     *state = NULL;
        server_resolve_t   *resolve = NULL;

        state = CALL_STATE (frame);
        resolve = state->resolve_now;

        if (resolve->fd_no != -1) {

                server_resolve_fd (frame);

        } else if (!uuid_is_null (resolve->pargfid)) {

                server_resolve_entry (frame);

        } else if (!uuid_is_null (resolve->gfid)) {

                server_resolve_inode (frame);

        } else {
                if (resolve == &state->resolve)
                        gf_log (frame->this->name, GF_LOG_WARNING,
                                "no resolution type for %s (%s)",
                                resolve->path, gf_fop_list[frame->root->op]);

                resolve->op_ret = -1;
                resolve->op_errno = EINVAL;

                server_resolve_all (frame);
        }

        return 0;
}


int
server_resolve_done (call_frame_t *frame)
{
        server_state_t    *state = NULL;

        state = CALL_STATE (frame);

        server_print_request (frame);

        state->resume_fn (frame, frame->root->client->bound_xl);

        return 0;
}


/*
 * This function is called multiple times, once per resolving one location/fd.
 * state->resolve_now is used to decide which location/fd is to be resolved now
 */
int
server_resolve_all (call_frame_t *frame)
{
        server_state_t    *state = NULL;
        xlator_t          *this = NULL;

        this  = frame->this;
        state = CALL_STATE (frame);

        if (state->resolve_now == NULL) {

                state->resolve_now = &state->resolve;
                state->loc_now     = &state->loc;

                server_resolve (frame);

        } else if (state->resolve_now == &state->resolve) {

                state->resolve_now = &state->resolve2;
                state->loc_now     = &state->loc2;

                server_resolve (frame);

        } else if (state->resolve_now == &state->resolve2) {

                server_resolve_done (frame);

        } else {
                gf_log (this->name, GF_LOG_ERROR,
                        "Invalid pointer for state->resolve_now");
        }

        return 0;
}


int
resolve_and_resume (call_frame_t *frame, server_resume_fn_t fn)
{
        server_state_t    *state = NULL;

        state = CALL_STATE (frame);
        state->resume_fn = fn;

        server_resolve_all (frame);

        return 0;
}
