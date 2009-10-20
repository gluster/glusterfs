/*
  Copyright (c) 2009 Gluster, Inc. <http://www.gluster.com>
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

#include "server-protocol.h"
#include "server-helpers.h"


struct resolve_comp {
        char      *basename;
        ino_t      ino;
        uint64_t   gen;
        inode_t   *inode;
};

int
server_resolve_all (call_frame_t *frame);

int
component_count (const char *path)
{
        int         count = 0;
        const char *trav = NULL;

        trav = path;

        for (trav = path; *trav; trav++) {
                if (*trav == '/')
                        count++;
        }

        return count + 2;
}


int
prepare_components (server_resolve_t *resolve)
{
//        char *path  = NULL;

        return 0;
}


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
                if (loc->parent) {
                        ret = inode_path (loc->parent, resolve->bname, &path);
                } else if (loc->inode) {
                        ret = inode_path (loc->inode, NULL, &path);
                }

                if (!path)
                        path = strdup (resolve->path);

                loc->path = path;
        }

        loc->name = strrchr (loc->path, '/');
        if (loc->name)
                loc->name++;

        if (!loc->parent) {
                loc->parent = inode_parent (loc->inode, 0, NULL);
        }

        return 0;
}



int
server_resolve_fd (call_frame_t *frame)
{
        server_state_t       *state = NULL;
        xlator_t             *this = NULL;
        server_resolve_t     *resolve = NULL;
        server_connection_t  *conn = NULL;
        uint64_t              fd_no = -1;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;
        conn  = SERVER_CONNECTION (frame);

        fd_no = resolve->fd_no;

        state->fd = gf_fd_fdptr_get (conn->fdtable, fd_no);

        if (!state->fd) {
                resolve->op_ret   = -1;
                resolve->op_errno = EBADF;
        }

        server_resolve_all (frame);

        return 0;
}


int
resolve_entry_deep (call_frame_t *frame)
{
        server_state_t     *state = NULL;
        xlator_t           *this = NULL;
        server_resolve_t   *resolve = NULL;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;

        gf_log (frame->this->name, GF_LOG_WARNING,
                "seeking deep resolution of %s", resolve->path);

        if (resolve->type == RESOLVE_MUST) {
                resolve->op_ret = -1;
                resolve->op_errno = ENOENT;
        }

        resolve_loc_touchup (frame);

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

        parent = inode_get (state->itable, resolve->par, 0);
        if (!parent) {
                /* simple resolution is indecisive. need to perform
                   deep resolution */
                ret = 1;
                goto out;
        }

        if (parent->ino != 1 && parent->generation != resolve->gen) {
                /* simple resolution is decisive - request was for a
                   stale handle */
                resolve->op_ret   = -1;
                resolve->op_errno = ESTALE;
                ret = -1;
                goto out;
        }

        /* expected @parent was found from the inode cache */
        state->loc_now->parent = inode_ref (parent);

        inode = inode_grep (state->itable, parent, resolve->bname);
        if (!inode) {
                switch (resolve->type) {
                case RESOLVE_DONTCARE:
                case RESOLVE_NOT:
                        ret = 0;
                        break;
                default:
                        ret = 1;
                        break;
                }

                goto out;
        }

        if (resolve->type == RESOLVE_NOT) {
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
        xlator_t           *this = NULL;
        server_resolve_t   *resolve = NULL;
        int                 ret = 0;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;

        ret = resolve_entry_simple (frame);

        if (ret > 0) {
                resolve_entry_deep (frame);
                return 0;
        }

        resolve_loc_touchup (frame);

        server_resolve_all (frame);

        return 0;
}


int
resolve_inode_deep (call_frame_t *frame)
{
        server_state_t     *state = NULL;
        xlator_t           *this = NULL;
        server_resolve_t   *resolve = NULL;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;

        gf_log (frame->this->name, GF_LOG_WARNING,
                "seeking deep resolution of %s", resolve->path);

        if (resolve->type == RESOLVE_MUST) {
                resolve->op_ret = -1;
                resolve->op_errno = ENOENT;
                goto out;
        }

        resolve_loc_touchup (frame);

out:
        server_resolve_all (frame);

        return 0;
}


int
resolve_inode_simple (call_frame_t *frame)
{
        server_state_t     *state = NULL;
        xlator_t           *this = NULL;
        server_resolve_t   *resolve = NULL;
        inode_t            *inode = NULL;
        int                 ret = 0;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;

        if (resolve->type == RESOLVE_EXACT) {
                inode = inode_get (state->itable, resolve->ino, resolve->gen);
        } else {
                inode = inode_get (state->itable, resolve->ino, 0);
        }

        if (!inode) {
                ret = 1;
                goto out;
        }

        if (inode->ino != 1 && inode->generation != resolve->gen) {
                resolve->op_ret      = -1;
                resolve->op_errno    = ESTALE;
                ret = -1;
                goto out;
        }

        ret = 0;

        state->loc_now->inode = inode_ref (inode);

out:
        if (inode)
                inode_unref (inode);

        return ret;
}


int
server_resolve_inode (call_frame_t *frame)
{
        server_state_t     *state = NULL;
        xlator_t           *this = NULL;
        server_resolve_t   *resolve = NULL;
        int                 ret = 0;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;

        ret = resolve_inode_simple (frame);

        if (ret > 0) {
                resolve_inode_deep (frame);
                return 0;
        }

        resolve_loc_touchup (frame);

        server_resolve_all (frame);

        return 0;
}


int
server_resolve (call_frame_t *frame)
{
        server_state_t     *state = NULL;
        xlator_t           *this = NULL;
        server_resolve_t   *resolve = NULL;

        state = CALL_STATE (frame);
        this  = frame->this;
        resolve = state->resolve_now;

        if (resolve->fd_no != -1) {

                server_resolve_fd (frame);

        } else if (resolve->par) {

                server_resolve_entry (frame);

        } else if (resolve->ino) {

                server_resolve_inode (frame);

        } else {
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

        gf_log (BOUND_XL (frame)->name, GF_LOG_DEBUG,
                "RESOLVE %s() on %s %s",
                gf_fop_list[frame->root->op],
                state->resolve.path, state->resolve2.path);

        server_resolve_all (frame);

        return 0;
}
