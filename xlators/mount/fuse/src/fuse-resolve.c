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

#include "fuse-bridge.h"

static int
fuse_resolve_all (fuse_state_t *state);

int fuse_resolve_continue (fuse_state_t *state);
int fuse_resolve_entry_simple (fuse_state_t *state);
int fuse_resolve_inode_simple (fuse_state_t *state);


static int
fuse_resolve_loc_touchup (fuse_state_t *state)
{
        fuse_resolve_t *resolve = NULL;
        loc_t          *loc     = NULL;
        char           *path    = NULL;
        int             ret     = 0;

        resolve = state->resolve_now;
        loc     = state->loc_now;

        if (!loc->path) {
                if (loc->parent && resolve->bname) {
                        ret = inode_path (loc->parent, resolve->bname, &path);
                } else if (loc->inode) {
                        ret = inode_path (loc->inode, NULL, &path);
                }
                if (ret)
                        gf_log (THIS->name, GF_LOG_TRACE,
                                "return value inode_path %d", ret);
                loc->path = path;
        }

        return 0;
}


int
fuse_resolve_gfid_entry_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int op_ret, int op_errno, inode_t *inode,
                             struct iatt *buf, dict_t *xattr,
                             struct iatt *postparent)
{
        fuse_state_t   *state      = NULL;
        fuse_resolve_t *resolve    = NULL;
        inode_t        *link_inode = NULL;
        loc_t          *resolve_loc   = NULL;

        state = frame->root->state;
        resolve = state->resolve_now;
        resolve_loc = &resolve->resolve_loc;

        STACK_DESTROY (frame->root);

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

        fuse_resolve_continue (state);
        return 0;
}


int
fuse_resolve_gfid_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno, inode_t *inode, struct iatt *buf,
                       dict_t *xattr, struct iatt *postparent)
{
        fuse_state_t   *state      = NULL;
        fuse_resolve_t *resolve    = NULL;
        inode_t        *link_inode = NULL;
        loc_t          *resolve_loc   = NULL;

        state = frame->root->state;
        resolve = state->resolve_now;
        resolve_loc = &resolve->resolve_loc;

        STACK_DESTROY (frame->root);

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

        FUSE_FOP (state, fuse_resolve_gfid_entry_cbk, GF_FOP_LOOKUP,
                  lookup, &resolve->resolve_loc, NULL);

        return 0;
out:
        fuse_resolve_continue (state);
        return 0;
}


int
fuse_resolve_gfid (fuse_state_t *state)
{
        fuse_resolve_t *resolve  = NULL;
        loc_t          *resolve_loc = NULL;
        int             ret      = 0;

        resolve = state->resolve_now;
        resolve_loc = &resolve->resolve_loc;

        if (!uuid_is_null (resolve->pargfid)) {
                uuid_copy (resolve_loc->gfid, resolve->pargfid);
                resolve_loc->inode = inode_new (state->itable);
                ret = inode_path (resolve_loc->inode, NULL,
                                  (char **)&resolve_loc->path);
        } else if (!uuid_is_null (resolve->gfid)) {
                uuid_copy (resolve_loc->gfid, resolve->gfid);
                resolve_loc->inode = inode_new (state->itable);
                ret = inode_path (resolve_loc->inode, NULL,
                                  (char **)&resolve_loc->path);
        }
        if (ret <= 0) {
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to get the path from inode %s",
                        uuid_utoa (resolve->gfid));
        }

        FUSE_FOP (state, fuse_resolve_gfid_cbk, GF_FOP_LOOKUP,
                  lookup, &resolve->resolve_loc, NULL);

        return 0;
}


int
fuse_resolve_continue (fuse_state_t *state)
{
        fuse_resolve_t     *resolve = NULL;
        int                   ret = 0;

        resolve = state->resolve_now;

        resolve->op_ret   = 0;
        resolve->op_errno = 0;

        /* TODO: should we handle 'fd' here ? */
        if (!uuid_is_null (resolve->pargfid))
                ret = fuse_resolve_entry_simple (state);
        else if (!uuid_is_null (resolve->gfid))
                ret = fuse_resolve_inode_simple (state);
        if (ret)
                gf_log (THIS->name, GF_LOG_DEBUG,
                        "return value of resolve_*_simple %d", ret);

        fuse_resolve_loc_touchup (state);

        fuse_resolve_all (state);

        return 0;
}


/*
  Check if the requirements are fulfilled by entries in the inode cache itself
  Return value:
  <= 0 - simple resolution was decisive and complete (either success or failure)
  > 0  - indecisive, need to perform deep resolution
*/

int
fuse_resolve_entry_simple (fuse_state_t *state)
{
        fuse_resolve_t *resolve   = NULL;
        inode_t      *parent    = NULL;
        inode_t      *inode     = NULL;
        int           ret       = 0;

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
        if (state->loc_now->parent) {
                inode_unref (state->loc_now->parent);
        }

        state->loc_now->parent = inode_ref (parent);

        inode = inode_grep (state->itable, parent, resolve->bname);
        if (!inode) {
                resolve->op_ret   = -1;
                resolve->op_errno = ENOENT;
                ret = 1;
                goto out;
        }

        ret = 0;

        if (state->loc_now->inode) {
                inode_unref (state->loc_now->inode);
                state->loc_now->inode = NULL;
        }

        state->loc_now->inode  = inode_ref (inode);
        uuid_copy (state->loc_now->gfid, resolve->gfid);

out:
        if (parent)
                inode_unref (parent);

        if (inode)
                inode_unref (inode);

        return ret;
}


int
fuse_resolve_entry (fuse_state_t *state)
{
        int    ret = 0;
        loc_t *loc = NULL;

        loc  = state->loc_now;

        ret = fuse_resolve_entry_simple (state);
        if (ret > 0) {
                loc_wipe (loc);
                fuse_resolve_gfid (state);
                return 0;
        }

        if (ret == 0)
                fuse_resolve_loc_touchup (state);

        fuse_resolve_all (state);

        return 0;
}


int
fuse_resolve_inode_simple (fuse_state_t *state)
{
        fuse_resolve_t *resolve   = NULL;
        inode_t      *inode     = NULL;
        int           ret       = 0;

        resolve = state->resolve_now;

        inode = inode_find (state->itable, resolve->gfid);
        if (!inode) {
                resolve->op_ret   = -1;
                resolve->op_errno = ENOENT;
                ret = 1;
                goto out;
        }

        ret = 0;

        if (state->loc_now->inode) {
                inode_unref (state->loc_now->inode);
        }

        state->loc_now->inode = inode_ref (inode);
        uuid_copy (state->loc_now->gfid, resolve->gfid);

out:
        if (inode)
                inode_unref (inode);

        return ret;
}


int
fuse_resolve_inode (fuse_state_t *state)
{
        int                 ret = 0;
        loc_t              *loc = NULL;

        loc  = state->loc_now;

        ret = fuse_resolve_inode_simple (state);

        if (ret > 0) {
                loc_wipe (loc);
                fuse_resolve_gfid (state);
                return 0;
        }

        if (ret == 0)
                fuse_resolve_loc_touchup (state);

        fuse_resolve_all (state);

        return 0;
}

static int
fuse_resolve_fd (fuse_state_t *state)
{
        fuse_resolve_t  *resolve    = NULL;
        fd_t          *fd         = NULL;
        int            ret        = 0;
        uint64_t       tmp_fd_ctx = 0;
        char          *path       = NULL;
        char          *name       = NULL;

        resolve = state->resolve_now;

        fd = resolve->fd;

        ret = fd_ctx_get (fd, state->this, &tmp_fd_ctx);
        if (!ret) {
                state->fd = (fd_t *)(long)tmp_fd_ctx;
                fd_ref (state->fd);
                fuse_resolve_all (state);
                goto out;
        }

        ret = inode_path (fd->inode, 0, &path);
        if (ret <= 0)
                gf_log ("", GF_LOG_WARNING,
                        "failed to do inode-path on fd %d %s", ret, path);

        name = strrchr (path, '/');
        if (name)
                name++;

        resolve->path = path;
        resolve->bname = gf_strdup (name);

        state->loc_now     = &state->loc;

out:
        return 0;
}


static int
fuse_resolve (fuse_state_t *state)
 {
        fuse_resolve_t   *resolve = NULL;

        resolve = state->resolve_now;

        if (resolve->fd) {

                fuse_resolve_fd (state);

        } else if (!uuid_is_null (resolve->pargfid)) {

                fuse_resolve_entry (state);

        } else if (!uuid_is_null (resolve->gfid)) {

                fuse_resolve_inode (state);

        } else {

                resolve->op_ret = 0;
                resolve->op_errno = EINVAL;

                fuse_resolve_all (state);
        }

        return 0;
}


static int
fuse_resolve_done (fuse_state_t *state)
{
        fuse_resume_fn_t fn = NULL;

        if (state->resolve.op_ret || state->resolve2.op_ret) {
                send_fuse_err (state->this, state->finh,
                               state->resolve.op_errno);
                free_fuse_state (state);
                goto out;
        }
        fn = state->resume_fn;
        if (fn)
                fn (state);

out:
        return 0;
}


/*
 * This function is called multiple times, once per resolving one location/fd.
 * state->resolve_now is used to decide which location/fd is to be resolved now
 */
static int
fuse_resolve_all (fuse_state_t *state)
{
        if (state->resolve_now == NULL) {

                state->resolve_now = &state->resolve;
                state->loc_now     = &state->loc;

                fuse_resolve (state);

        } else if (state->resolve_now == &state->resolve) {

                state->resolve_now = &state->resolve2;
                state->loc_now     = &state->loc2;

                fuse_resolve (state);

        } else if (state->resolve_now == &state->resolve2) {

                fuse_resolve_done (state);

        } else {
                gf_log ("fuse-resolve", GF_LOG_ERROR,
                        "Invalid pointer for state->resolve_now");
        }

        return 0;
}


int
fuse_gfid_set (fuse_state_t *state)
{
        int   ret = 0;

        if (uuid_is_null (state->gfid))
                goto out;

        if (!state->dict)
                state->dict = dict_new ();

        if (!state->dict) {
                ret = -1;
                goto out;
        }

        ret = dict_set_static_bin (state->dict, "gfid-req",
                                   state->gfid, sizeof (state->gfid));
out:
        return ret;
}


int
fuse_resolve_and_resume (fuse_state_t *state, fuse_resume_fn_t fn)
{
        xlator_t *inode_xl = NULL;
        xlator_t *active_xl = NULL;

        fuse_gfid_set (state);

        state->resume_fn = fn;

        active_xl = fuse_active_subvol (state->this);
        inode_xl = fuse_state_subvol (state);
        if (!inode_xl && state->loc.parent)
                inode_xl = state->loc.parent->table->xl;

        /* If inode or fd is already in new graph, goto resume */
        if (inode_xl == active_xl) {
                /* Lets move to resume if there is no other inode to check */
                if (!(state->loc2.parent || state->loc2.inode))
                        goto resume;

                inode_xl = NULL;
                /* We have to make sure both inodes we are
                   working on are in same inode table */
                if (state->loc2.inode)
                        inode_xl = state->loc2.inode->table->xl;
                if (!inode_xl && state->loc2.parent)
                        inode_xl = state->loc2.parent->table->xl;

                if (inode_xl == active_xl)
                        goto resume;
        }


        /* If the resolve is for 'fd' and its open with 'write' flag
           set, don't switch to new graph yet */

        /* TODO: fix it later */
        /* if (state->fd && ((state->fd->flags & O_RDWR) ||
                          (state->fd->flags & O_WRONLY)))
        */
        if (state->fd)
                goto resume;

        /*
        if (state->fd) {
                state->resolve.fd = state->fd;
                state->fd = NULL; // TODO: we may need a 'fd_unref()' here, not very sure'
        }
        */

        /* now we have to resolve the inode to 'itable' */
        state->itable = active_xl->itable;

        fuse_resolve_all (state);

        return 0;
resume:
        fn (state);

        return 0;
}
