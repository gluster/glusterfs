/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
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
resolve_deep_continue (call_frame_t *frame);
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
                        struct iatt *buf, dict_t *xattr, struct iatt *postparent)
{
        server_state_t       *state = NULL;
        server_resolve_t     *resolve = NULL;
        inode_t              *link_inode = NULL;
        loc_t                *deep_loc = NULL;

        state = CALL_STATE (frame);
        resolve = state->resolve_now;
        deep_loc = &resolve->deep_loc;

        if (op_ret == -1) {
                gf_log (this->name, ((op_errno == ENOENT) ? GF_LOG_DEBUG :
                                     GF_LOG_WARNING),
                        "%s/%s: failed to resolve (%s)",
                        uuid_utoa (deep_loc->pargfid), deep_loc->name,
                        strerror (op_errno));
                goto out;
        }

        link_inode = inode_link (inode, deep_loc->parent, deep_loc->name, buf);

        if (!link_inode)
                goto out;

        inode_lookup (link_inode);

        inode_unref (link_inode);

out:
        loc_wipe (deep_loc);

        resolve_deep_continue (frame);
        return 0;
}


int
resolve_gfid_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno, inode_t *inode, struct iatt *buf,
                  dict_t *xattr, struct iatt *postparent)
{
        server_state_t       *state = NULL;
        server_resolve_t     *resolve = NULL;
        inode_t              *link_inode = NULL;
        loc_t                *deep_loc = NULL;

        state = CALL_STATE (frame);
        resolve = state->resolve_now;
        deep_loc = &resolve->deep_loc;

        if (op_ret == -1) {
                gf_log (this->name, ((op_errno == ENOENT) ? GF_LOG_DEBUG :
                                     GF_LOG_WARNING),
                        "%s: failed to resolve (%s)",
                        uuid_utoa (deep_loc->gfid), strerror (op_errno));
                loc_wipe (&resolve->deep_loc);
                goto out;
        }

        loc_wipe (deep_loc);

        link_inode = inode_link (inode, NULL, NULL, buf);

        if (!link_inode)
                goto out;

        inode_lookup (link_inode);

        if (uuid_is_null (resolve->pargfid)) {
                inode_unref (link_inode);
                goto out;
        }

        deep_loc->parent = link_inode;
        uuid_copy (deep_loc->pargfid, deep_loc->parent->gfid);

        deep_loc->name = resolve->bname;

        deep_loc->inode = inode_new (state->itable);
        inode_path (deep_loc->parent, deep_loc->name, (char **) &deep_loc->path);

        STACK_WIND (frame, resolve_gfid_entry_cbk,
                    BOUND_XL (frame), BOUND_XL (frame)->fops->lookup,
                    &resolve->deep_loc, NULL);
        return 0;
out:
        resolve_deep_continue (frame);
        return 0;
}


int
resolve_gfid (call_frame_t *frame)
{
        server_state_t       *state = NULL;
        xlator_t             *this = NULL;
        server_resolve_t     *resolve = NULL;
        loc_t                *deep_loc = NULL;
        int                   ret = 0;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;
        deep_loc = &resolve->deep_loc;

        if (!uuid_is_null (resolve->pargfid)) {
                uuid_copy (deep_loc->gfid, resolve->pargfid);
                deep_loc->inode = inode_new (state->itable);
                ret = inode_path (deep_loc->inode, NULL, (char **)&deep_loc->path);
        } else if (!uuid_is_null (resolve->gfid)) {
                uuid_copy (deep_loc->gfid, resolve->gfid);
                deep_loc->inode = inode_new (state->itable);
                ret = inode_path (deep_loc->inode, NULL, (char **)&deep_loc->path);
        }

        STACK_WIND (frame, resolve_gfid_cbk,
                    BOUND_XL (frame), BOUND_XL (frame)->fops->lookup,
                    &resolve->deep_loc, NULL);
        return 0;
}


int
resolve_deep_continue (call_frame_t *frame)
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
                resolve->op_errno = ENOENT;
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
                resolve->op_errno = ENOENT;
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
        server_state_t       *state = NULL;
        server_resolve_t     *resolve = NULL;
        server_connection_t  *conn = NULL;
        uint64_t              fd_no = -1;

        state = CALL_STATE (frame);
        resolve = state->resolve_now;
        conn  = SERVER_CONNECTION (frame);

        fd_no = resolve->fd_no;

        if (fd_no == -2) {
                server_resolve_anonfd (frame);
                return 0;
        }

        state->fd = gf_fd_fdptr_get (conn->fdtable, fd_no);

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
        xlator_t          *bound_xl = NULL;

        state = CALL_STATE (frame);
        bound_xl = BOUND_XL (frame);

        server_print_request (frame);

        state->resume_fn (frame, bound_xl);

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
