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
#include "server-messages.h"

static void
server_resolve_all(call_frame_t *frame, server_state_t *state);
static int
resolve_entry_simple(call_frame_t *frame, server_state_t *state,
                     server_resolve_t *resolve);
static int
resolve_inode_simple(call_frame_t *frame, server_state_t *state,
                     server_resolve_t *resolve);
static void
resolve_continue(call_frame_t *frame, server_state_t *state,
                 server_resolve_t *resolve);
static int
resolve_anonfd_simple(call_frame_t *frame, server_state_t *state,
                      server_resolve_t *resolve);

static int
resolve_name_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                 int op_errno, inode_t *inode, struct iatt *buf, dict_t *xdata,
                 struct iatt *postparent)
{
    server_state_t *state = NULL;
    server_resolve_t *resolve = NULL;
    inode_t *link_inode = NULL;
    loc_t *resolve_loc = NULL;

    state = CALL_STATE(frame);
    resolve = state->resolve_now;
    resolve_loc = &resolve->resolve_loc;

    if (op_ret == -1) {
        if (op_errno == ENOENT) {
            gf_msg_debug(this->name, op_errno, "%s/%s: failed to resolve",
                         uuid_utoa(resolve_loc->pargfid), resolve_loc->name);
            if (resolve->type == RESOLVE_NOT) {
                do {
                    inode = inode_grep(state->itable, resolve_loc->parent,
                                       resolve->bname);

                    if (inode) {
                        gf_msg_debug(this->name, 0,
                                     "%s/%s: "
                                     "removing stale dentry",
                                     uuid_utoa(resolve_loc->pargfid),
                                     resolve->bname);
                        inode_unlink(inode, resolve_loc->parent,
                                     resolve->bname);
                    }
                } while (inode);
            }
        } else {
            gf_msg(this->name, GF_LOG_WARNING, op_errno,
                   PS_MSG_GFID_RESOLVE_FAILED,
                   "%s/%s: failed to "
                   "resolve (%s)",
                   uuid_utoa(resolve_loc->pargfid), resolve_loc->name,
                   strerror(op_errno));
        }
        goto out;
    }

    link_inode = inode_link(inode, resolve_loc->parent, resolve_loc->name, buf);

    if (!link_inode)
        goto out;

    inode_lookup(link_inode);

    inode_unref(link_inode);

out:
    loc_wipe(resolve_loc);

    resolve_continue(frame, state, resolve);
    return 0;
}

static int
resolve_name(call_frame_t *frame, inode_t *parent, server_state_t *state,
             server_resolve_t *resolve)
{
    loc_t *resolve_loc = NULL;
    dict_t *dict = NULL;

    resolve_loc = &resolve->resolve_loc;
    resolve_loc->parent = inode_ref(parent);
    gf_uuid_copy(resolve_loc->pargfid, resolve_loc->parent->gfid);

    resolve_loc->name = resolve->bname;

    resolve_loc->inode = server_inode_new(state->itable, resolve_loc->gfid);

    inode_path(resolve_loc->parent, resolve_loc->name,
               (char **)&resolve_loc->path);

    if (state->xdata) {
        dict = dict_copy_with_ref(state->xdata, NULL);
        if (!dict)
            gf_msg(frame->this->name, GF_LOG_ERROR, ENOMEM, PS_MSG_NO_MEMORY,
                   "BUG: dict allocation failed (pargfid: %s, name: %s), "
                   "still continuing",
                   uuid_utoa(resolve_loc->gfid), resolve_loc->name);
    }

    STACK_WIND(frame, resolve_name_cbk, frame->root->client->bound_xl,
               frame->root->client->bound_xl->fops->lookup,
               &resolve->resolve_loc, dict);
    if (dict)
        dict_unref(dict);
    return 0;
}

static int
resolve_gfid_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                 int op_errno, inode_t *inode, struct iatt *buf, dict_t *xdata,
                 struct iatt *postparent)
{
    server_state_t *state = NULL;
    server_resolve_t *resolve = NULL;
    inode_t *link_inode = NULL;
    loc_t *resolve_loc = NULL;

    state = CALL_STATE(frame);
    resolve = state->resolve_now;
    resolve_loc = &resolve->resolve_loc;

    if (op_ret == -1) {
        if (op_errno == ENOENT) {
            gf_msg_debug(this->name, op_errno, "%s: failed to resolve",
                         uuid_utoa(resolve_loc->gfid));
        } else {
            gf_msg(this->name, GF_LOG_WARNING, op_errno,
                   PS_MSG_GFID_RESOLVE_FAILED, "%s: failed to resolve (%s)",
                   uuid_utoa(resolve_loc->gfid), strerror(op_errno));
        }
        loc_wipe(&resolve->resolve_loc);
        goto out;
    }

    link_inode = inode_link(inode, NULL, NULL, buf);

    if (!link_inode) {
        loc_wipe(resolve_loc);
        goto out;
    }

    inode_lookup(link_inode);

    /* wipe the loc only after the inode has been linked to the inode
       table. Otherwise before inode gets linked to the inode table,
       inode would have been unrefed (this might have been destroyed
       if refcount becomes 0, and put back to mempool). So once the
       inode gets destroyed, inode_link is a redundant operation. But
       without knowing that the destroyed inode's pointer is saved in
       the resolved_loc as parent (while constructing loc for resolving
       the entry) and the inode_new call for resolving the entry will
       return the same pointer to the inode as the parent (because in
       reality the inode is a free inode present in cold list of the
       inode mem-pool).
    */
    loc_wipe(resolve_loc);

    if (gf_uuid_is_null(resolve->pargfid)) {
        inode_unref(link_inode);
        goto out;
    }

    resolve_name(frame, link_inode, state, resolve);
    inode_unref(link_inode);

    return 0;
out:
    resolve_continue(frame, state, resolve);
    return 0;
}

static void
resolve_gfid(call_frame_t *frame, server_state_t *state,
             server_resolve_t *resolve)
{
    loc_t *resolve_loc = NULL;
    dict_t *xdata = NULL;

    resolve_loc = &resolve->resolve_loc;

    if (!gf_uuid_is_null(resolve->pargfid))
        gf_uuid_copy(resolve_loc->gfid, resolve->pargfid);
    else if (!gf_uuid_is_null(resolve->gfid))
        gf_uuid_copy(resolve_loc->gfid, resolve->gfid);

    resolve_loc->inode = server_inode_new(state->itable, resolve_loc->gfid);
    (void)loc_path(resolve_loc, NULL);

    if (state->xdata) {
        xdata = dict_copy_with_ref(state->xdata, NULL);
        if (!xdata)
            gf_msg(frame->this->name, GF_LOG_ERROR, ENOMEM, PS_MSG_NO_MEMORY,
                   "BUG: dict allocation failed (gfid: %s), "
                   "still continuing",
                   uuid_utoa(resolve_loc->gfid));
    }

    STACK_WIND(frame, resolve_gfid_cbk, frame->root->client->bound_xl,
               frame->root->client->bound_xl->fops->lookup,
               &resolve->resolve_loc, xdata);

    if (xdata)
        dict_unref(xdata);
}

static void
resolve_continue(call_frame_t *frame, server_state_t *state,
                 server_resolve_t *resolve)
{
    int ret = 0;

    resolve->op_ret = 0;
    resolve->op_errno = 0;

    if (resolve->fd_no != -1) {
        ret = resolve_anonfd_simple(frame, state, resolve);
        goto out;
    } else if (!gf_uuid_is_null(resolve->pargfid))
        ret = resolve_entry_simple(frame, state, resolve);
    else if (!gf_uuid_is_null(resolve->gfid))
        ret = resolve_inode_simple(frame, state, resolve);
    if (ret)
        gf_msg_debug(frame->this->name, 0,
                     "return value of resolve_*_"
                     "simple %d",
                     ret);

    loc_touchup(state->loc_now, resolve->bname);
out:
    server_resolve_all(frame, state);
}

/*
  Check if the requirements are fulfilled by entries in the inode cache itself
  Return value:
  <= 0 - simple resolution was decisive and complete (either success or failure)
  > 0  - indecisive, need to perform deep resolution
*/

static int
resolve_entry_simple(call_frame_t *frame, server_state_t *state,
                     server_resolve_t *resolve)
{
    inode_t *parent = NULL;
    inode_t *inode = NULL;
    int ret = 0;

    parent = inode_find(state->itable, resolve->pargfid);
    if (!parent) {
        /* simple resolution is indecisive. need to perform
           deep resolution */
        resolve->op_ret = -1;
        resolve->op_errno = ESTALE;
        ret = 1;
        goto out;
    }

    if (parent->ia_type != IA_IFDIR) {
        /* Parent type should be 'directory', and nothing else */
        gf_msg(frame->this->name, GF_LOG_ERROR, EPERM,
               PS_MSG_GFID_RESOLVE_FAILED, "%s: parent type not directory (%d)",
               uuid_utoa(parent->gfid), parent->ia_type);
        resolve->op_ret = -1;
        resolve->op_errno = EPERM;
        ret = 1;
        goto out;
    }

    /* expected @parent was found from the inode cache */
    gf_uuid_copy(state->loc_now->pargfid, resolve->pargfid);
    state->loc_now->parent = inode_ref(parent);
    if (strchr(resolve->bname, '/')) {
        /* basename should be a string (without '/') in a directory,
           it can't span multiple levels. This can also lead to
           resolving outside the parent's tree, which is not allowed */
        gf_msg(frame->this->name, GF_LOG_ERROR, EPERM,
               PS_MSG_GFID_RESOLVE_FAILED,
               "%s: basename sent by client not allowed", resolve->bname);
        resolve->op_ret = -1;
        resolve->op_errno = EPERM;
        ret = 1;
        goto out;
    }

    state->loc_now->name = resolve->bname;

    inode = inode_grep(state->itable, parent, resolve->bname);
    if (!inode) {
        switch (resolve->type) {
            case RESOLVE_DONTCARE:
            case RESOLVE_NOT:
                ret = 0;
                break;
            case RESOLVE_MAY:
                ret = 2;
                break;
            default:
                resolve->op_ret = -1;
                resolve->op_errno = ENOENT;
                ret = 2;
                break;
        }

        goto out;
    }

    if (resolve->type == RESOLVE_NOT) {
        gf_msg_debug(frame->this->name, 0,
                     "inode (pointer: %p gfid:%s found"
                     " for path (%s) while type is RESOLVE_NOT. "
                     "Performing lookup on backend to rule out any "
                     "possible stale dentries in inode table",
                     inode, uuid_utoa(inode->gfid), resolve->path);
        resolve->op_ret = -1;
        resolve->op_errno = EEXIST;
        ret = 1;
        goto out;
    }

    ret = 0;

    state->loc_now->inode = inode_ref(inode);

out:
    if (parent)
        inode_unref(parent);

    if (inode)
        inode_unref(inode);

    return ret;
}

static void
server_resolve_entry(call_frame_t *frame, server_state_t *state,
                     server_resolve_t *resolve)
{
    int ret = 0;
    inode_t *parent = NULL;

    ret = resolve_entry_simple(frame, state, resolve);

    if (ret > 0) {
        if (ret == 2) {
            parent = inode_ref(state->loc_now->parent);
        }
        /*Wipe state->loc_now before calling resolve_xxxx()*/
        loc_wipe(state->loc_now);
        if (parent) {
            resolve_name(frame, parent, state, resolve);
            inode_unref(parent);
        } else {
            resolve_gfid(frame, state, resolve);
        }
    } else {
        loc_touchup(state->loc_now, resolve->bname);
        server_resolve_all(frame, state);
    }
}

static int
resolve_inode_simple(call_frame_t *frame, server_state_t *state,
                     server_resolve_t *resolve)
{
    inode_t *inode = NULL;
    int ret = 0;

    inode = inode_find(state->itable, resolve->gfid);

    if (!inode) {
        if (resolve->type == RESOLVE_DONTCARE) {
            gf_uuid_copy(state->loc_now->gfid, resolve->gfid);
        } else {
            resolve->op_ret = -1;
            resolve->op_errno = ESTALE;
            ret = 1;
        }
        goto out;
    }

    state->loc_now->inode = inode_ref(inode);
    gf_uuid_copy(state->loc_now->gfid, resolve->gfid);

out:
    if (inode)
        inode_unref(inode);

    return ret;
}

static void
server_resolve_inode(call_frame_t *frame, server_state_t *state,
                     server_resolve_t *resolve)
{
    int ret = 0;
    loc_t *loc = NULL;

    ret = resolve_inode_simple(frame, state, resolve);

    if (ret > 0) {
        loc = state->loc_now;
        loc_wipe(loc);
        resolve_gfid(frame, state, resolve);
    } else {
        loc_touchup(state->loc_now, resolve->bname);
        server_resolve_all(frame, state);
    }
}

static int
resolve_anonfd_simple(call_frame_t *frame, server_state_t *state,
                      server_resolve_t *resolve)
{
    inode_t *inode = NULL;
    int ret = 0;

    inode = inode_find(state->itable, resolve->gfid);

    if (!inode) {
        resolve->op_ret = -1;
        resolve->op_errno = ENOENT;
        ret = 1;
        gf_msg_debug("server", 0,
                     "inode for the gfid"
                     "(%s) is not found. anonymous fd creation failed",
                     uuid_utoa(resolve->gfid));
        goto out;
    }

    if (frame->root->op == GF_FOP_READ || frame->root->op == GF_FOP_WRITE)
        state->fd = fd_anonymous_with_flags(inode, state->flags);
    else
        state->fd = fd_anonymous(inode);
out:
    if (inode)
        inode_unref(inode);

    return ret;
}

static void
server_resolve_anonfd(call_frame_t *frame, server_state_t *state,
                      server_resolve_t *resolve)
{
    int ret = 0;
    loc_t *loc = NULL;

    ret = resolve_anonfd_simple(frame, state, resolve);

    if (ret > 0) {
        loc = state->loc_now;
        loc_wipe(loc);
        resolve_gfid(frame, state, resolve);
    } else {
        server_resolve_all(frame, state);
    }
}

static void
server_resolve_fd(call_frame_t *frame, server_state_t *state,
                  server_resolve_t *resolve)
{
    server_ctx_t *serv_ctx = NULL;
    client_t *client = NULL;
    uint64_t fd_no;

    fd_no = resolve->fd_no;

    if (fd_no == GF_ANON_FD_NO) {
        server_resolve_anonfd(frame, state, resolve);
        return;
    }

    client = frame->root->client;

    serv_ctx = server_ctx_get(client, client->this);

    if (serv_ctx == NULL) {
        gf_msg("", GF_LOG_INFO, ENOMEM, PS_MSG_NO_MEMORY,
               "server_ctx_get() failed");
        resolve->op_ret = -1;
        resolve->op_errno = ENOMEM;
        return;
    }

    /*
     * With copy_file_range, there will be 2 fds to resolve.
     * This same function is called to resolve both the source
     * fd and the destination fd. As of now, this function does
     * not have any mechanism to distinguish between the 2 fds
     * being resolved except for checking the value of state->fd.
     * The assumption is that, if source fd the one which is
     * being resolved here, then state->fd would be NULL. If it
     * is not NULL, then it is the destination fd which is being
     * resolved.
     * This  method (provided the above assumption is true) is
     * to achieve the ability to distinguish between 2 fds with
     * minimum changes being done to this function. If this way
     * is not correct, then more changes might be needed.
     */
    if (!state->fd) {
        state->fd = gf_fd_fdptr_get(serv_ctx->fdtable, fd_no);
        if (!state->fd) {
            gf_msg("", GF_LOG_INFO, EBADF, PS_MSG_FD_NOT_FOUND,
                   "fd not "
                   "found in context");
            resolve->op_ret = -1;
            resolve->op_errno = EBADF;
        }
    } else {
        state->fd_out = gf_fd_fdptr_get(serv_ctx->fdtable, fd_no);
        if (!state->fd_out) {
            gf_msg("", GF_LOG_INFO, EBADF, PS_MSG_FD_NOT_FOUND,
                   "fd not "
                   "found in context");
            resolve->op_ret = -1;
            resolve->op_errno = EBADF;
        }
    }

    server_resolve_all(frame, state);
}

static void
server_resolve(call_frame_t *frame, server_state_t *state)
{
    server_resolve_t *resolve = NULL;

    resolve = state->resolve_now;

    if (resolve->fd_no != -1) {
        server_resolve_fd(frame, state, resolve);

    } else if (!gf_uuid_is_null(resolve->pargfid)) {
        server_resolve_entry(frame, state, resolve);

    } else if (!gf_uuid_is_null(resolve->gfid)) {
        server_resolve_inode(frame, state, resolve);

    } else {
        if (resolve == &state->resolve)
            gf_msg(frame->this->name, GF_LOG_WARNING, 0, PS_MSG_INVALID_ENTRY,
                   "no resolution type for %s (%s)", resolve->path,
                   gf_fop_list[frame->root->op]);

        resolve->op_ret = -1;
        resolve->op_errno = EINVAL;

        server_resolve_all(frame, state);
    }
}

static void
server_resolve_done(call_frame_t *frame, server_state_t *state)
{
    server_print_request(frame);

    state->resume_fn(frame, frame->root->client->bound_xl);
}

/*
 * This function is called multiple times, once per resolving one location/fd.
 * state->resolve_now is used to decide which location/fd is to be resolved now
 */
static void
server_resolve_all(call_frame_t *frame, server_state_t *state)
{
    if (state->resolve_now == NULL) {
        state->resolve_now = &state->resolve;
        state->loc_now = &state->loc;

        server_resolve(frame, state);

    } else if (state->resolve_now == &state->resolve) {
        state->resolve_now = &state->resolve2;
        state->loc_now = &state->loc2;

        server_resolve(frame, state);

    } else if (state->resolve_now == &state->resolve2) {
        server_resolve_done(frame, state);

    } else {
        gf_msg(frame->this->name, GF_LOG_ERROR, EINVAL, PS_MSG_INVALID_ENTRY,
               "Invalid pointer for "
               "state->resolve_now");
    }
}

void
resolve_and_resume(call_frame_t *frame, server_resume_fn_t fn)
{
    server_state_t *state = NULL;

    state = CALL_STATE(frame);
    state->resume_fn = fn;

    server_resolve_all(frame, state);
}
