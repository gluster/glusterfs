/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
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
static int
fuse_resolve_path_simple (fuse_state_t *state);

static int
component_count (const char *path)
{
        int         count = 0;
        const char *trav = NULL;

        for (trav = path; *trav; trav++) {
                if (*trav == '/')
                        count++;
        }

        return count + 2;
}


static int
prepare_components (fuse_state_t *state)
{
        xlator_t               *active_xl  = NULL;
        fuse_resolve_t           *resolve    = NULL;
        char                   *resolved   = NULL;
        struct fuse_resolve_comp *components = NULL;
        char                   *trav       = NULL;
        int                     count      = 0;
        int                     i          = 0;

        resolve = state->resolve_now;

        resolved = gf_strdup (resolve->path);
        resolve->resolved = resolved;

        count = component_count (resolve->path);
        components = GF_CALLOC (sizeof (*components), count, 0); //TODO
        if (!components)
                goto out;
        resolve->components = components;

        active_xl = fuse_active_subvol (state->this);

        components[0].basename = "";
        components[0].ino      = 1;
        components[0].gen      = 0;
        components[0].inode    = inode_ref (active_xl->itable->root);

        i = 1;
        for (trav = resolved; *trav; trav++) {
                if (*trav == '/') {
                        components[i].basename = trav + 1;
                        *trav = 0;
                        i++;
                }
        }
out:
        return 0;
}


static int
fuse_resolve_loc_touchup (fuse_state_t *state)
{
        fuse_resolve_t *resolve = NULL;
        loc_t        *loc     = NULL;
        char         *path    = NULL;
        int           ret     = 0;

        resolve = state->resolve_now;
        loc     = state->loc_now;

        if (!loc->path) {
                if (loc->parent) {
                        ret = inode_path (loc->parent, resolve->bname, &path);
                } else if (loc->inode) {
                        ret = inode_path (loc->inode, NULL, &path);
                }
                if (ret)
                        gf_log ("", GF_LOG_TRACE,
                                "return value inode_path %d", ret);

                if (!path)
                        path = gf_strdup (resolve->path);

                loc->path = path;
        }

        loc->name = strrchr (loc->path, '/');
        if (loc->name)
                loc->name++;

        if (!loc->parent && loc->inode) {
                loc->parent = inode_parent (loc->inode, 0, NULL);
        }

        return 0;
}

static int
fuse_resolve_newfd_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        fuse_state_t *state      = NULL;
        fuse_resolve_t *resolve    = NULL;
        fd_t         *old_fd     = NULL;
        fd_t         *tmp_fd     = NULL;
        uint64_t      tmp_fd_ctx = 0;
        int           ret        = 0;

        state = frame->root->state;
        resolve = state->resolve_now;

        STACK_DESTROY (frame->root);

        if (op_ret == -1) {
                resolve->op_ret   = -1;
                resolve->op_errno = op_errno;
                goto out;
        }

        old_fd = resolve->fd;

        state->fd = fd_ref (fd);

        fd_bind (fd);

        resolve->fd = NULL;
        ret = fd_ctx_del (old_fd, state->this, &tmp_fd_ctx);
        if (!ret) {
                tmp_fd = (fd_t *)(long)tmp_fd_ctx;
                fd_unref (tmp_fd);
        }
        ret = fd_ctx_set (old_fd, state->this, (uint64_t)(long)fd);
        if (ret)
                gf_log ("resolve", GF_LOG_WARNING,
                        "failed to set the fd ctx with resolved fd");
out:
        fuse_resolve_all (state);
        return 0;
}

static void
fuse_resolve_new_fd (fuse_state_t *state)
{
        fuse_resolve_t *resolve = NULL;
        fd_t         *new_fd  = NULL;
        fd_t         *fd      = NULL;

        resolve = state->resolve_now;
        fd = resolve->fd;

        new_fd = fd_create (state->loc.inode, state->finh->pid);
        new_fd->flags = (fd->flags & ~O_TRUNC);

        gf_log ("resolve", GF_LOG_DEBUG,
                "%"PRIu64": OPEN %s", state->finh->unique,
                state->loc.path);

        FUSE_FOP (state, fuse_resolve_newfd_cbk, GF_FOP_OPEN,
                  open, &state->loc, new_fd->flags, new_fd, 0);
}

static int
fuse_resolve_deep_continue (fuse_state_t *state)
{
        fuse_resolve_t     *resolve = NULL;
        int               ret = 0;

        resolve = state->resolve_now;

        resolve->op_ret   = 0;
        resolve->op_errno = 0;

        if (resolve->path)
                ret = fuse_resolve_path_simple (state);
        if (ret)
                gf_log ("resolve", GF_LOG_TRACE,
                        "return value of resolve_*_simple %d", ret);

        fuse_resolve_loc_touchup (state);

        /* This function is called by either fd resolve or inode resolve */
        if (!resolve->fd)
                fuse_resolve_all (state);
        else
                fuse_resolve_new_fd (state);

        return 0;
}


static int
fuse_resolve_deep_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int op_ret, int op_errno, inode_t *inode, struct iatt *buf,
                  dict_t *xattr, struct iatt *postparent)
{
        xlator_t               *active_xl = NULL;
        fuse_state_t           *state      = NULL;
        fuse_resolve_t           *resolve    = NULL;
        struct fuse_resolve_comp *components = NULL;
        inode_t                *link_inode = NULL;
        int                     i          = 0;

        state = frame->root->state;
        resolve = state->resolve_now;
        components = resolve->components;

        i = (long) cookie;

        STACK_DESTROY (frame->root);

        if (op_ret == -1) {
                goto get_out_of_here;
        }

        if (i != 0) {
                inode_ref (inode);
                /* no linking for root inode */
                link_inode = inode_link (inode, resolve->deep_loc.parent,
                                         resolve->deep_loc.name, buf);
                components[i].inode  = inode_ref (link_inode);
                link_inode = NULL;
        }
        inode_ref (resolve->deep_loc.parent);
        inode_ref (inode);
        loc_wipe (&resolve->deep_loc);
        i++; /* next component */

        if (!components[i].basename) {
                /* all components of the path are resolved */
                goto get_out_of_here;
        }

        /* join the current component with the path resolved until now */
        *(components[i].basename - 1) = '/';

        active_xl = fuse_active_subvol (state->this);

        resolve->deep_loc.path   = gf_strdup (resolve->resolved);
        resolve->deep_loc.parent = inode_ref (components[i-1].inode);
        resolve->deep_loc.inode  = inode_new (active_xl->itable);
        resolve->deep_loc.name   = components[i].basename;

        FUSE_FOP_COOKIE (state, active_xl, fuse_resolve_deep_cbk, (void *)(long)i,
                         GF_FOP_LOOKUP, lookup, &resolve->deep_loc, NULL);
        return 0;

get_out_of_here:
        fuse_resolve_deep_continue (state);
        return 0;
}


static int
fuse_resolve_path_deep (fuse_state_t *state)
{
        xlator_t               *active_xl  = NULL;
        fuse_resolve_t           *resolve    = NULL;
        struct fuse_resolve_comp *components = NULL;
        inode_t                *inode      = NULL;
        long                    i          = 0;

        resolve = state->resolve_now;

        prepare_components (state);

        components = resolve->components;

        /* start from the root */
        active_xl = fuse_active_subvol (state->this);
        resolve->deep_loc.inode = inode_ref (active_xl->itable->root);
        resolve->deep_loc.path  = gf_strdup ("/");
        resolve->deep_loc.name  = "";

        for (i = 1; components[i].basename; i++) {
                *(components[i].basename - 1) = '/';
                inode = inode_grep (active_xl->itable, components[i-1].inode,
                                    components[i].basename);
                if (!inode)
                        break;
                components[i].inode = inode_ref (inode);
        }

        if (!components[i].basename)
                goto resolved;

        resolve->deep_loc.path   = gf_strdup (resolve->resolved);
        resolve->deep_loc.parent = inode_ref (components[i-1].inode);
        resolve->deep_loc.inode  = inode_new (active_xl->itable);
        resolve->deep_loc.name   = components[i].basename;

        FUSE_FOP_COOKIE (state, active_xl, fuse_resolve_deep_cbk, (void *)(long)i,
                         GF_FOP_LOOKUP, lookup, &resolve->deep_loc, NULL);

        return 0;
resolved:
        fuse_resolve_deep_continue (state);
        return 0;
}


static int
fuse_resolve_path_simple (fuse_state_t *state)
{
        fuse_resolve_t           *resolve    = NULL;
        struct fuse_resolve_comp *components = NULL;
        int                     ret        = -1;
        int                     par_idx    = 0;
        int                     ino_idx    = 0;
        int                     i          = 0;

        resolve = state->resolve_now;
        components = resolve->components;

        if (!components) {
                resolve->op_ret   = -1;
                resolve->op_errno = ENOENT;
                goto out;
        }

        for (i = 0; components[i].basename; i++) {
                par_idx = ino_idx;
                ino_idx = i;
        }

        if (!components[par_idx].inode) {
                resolve->op_ret    = -1;
                resolve->op_errno  = ENOENT;
                goto out;
        }

        if (!components[ino_idx].inode &&
            (resolve->type == RESOLVE_MUST || resolve->type == RESOLVE_EXACT)) {
                resolve->op_ret    = -1;
                resolve->op_errno  = ENOENT;
                goto out;
        }

        if (components[ino_idx].inode && resolve->type == RESOLVE_NOT) {
                resolve->op_ret    = -1;
                resolve->op_errno  = EEXIST;
                goto out;
        }

        if (components[ino_idx].inode)
                state->loc_now->inode  = inode_ref (components[ino_idx].inode);
        state->loc_now->parent = inode_ref (components[par_idx].inode);

        ret = 0;

out:
        return ret;
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
        xlator_t     *active_xl = NULL;
        xlator_t     *this      = NULL;
        fuse_resolve_t *resolve   = NULL;
        inode_t      *parent    = NULL;
        inode_t      *inode     = NULL;
        int           ret       = 0;

        this  = state->this;
        resolve = state->resolve_now;

        active_xl = fuse_active_subvol (state->this);

        parent = inode_find (active_xl->itable, resolve->pargfid);
        if (!parent) {
                /* simple resolution is indecisive. need to perform
                   deep resolution */
                resolve->op_ret   = -1;
                resolve->op_errno = ENOENT;
                ret = 1;
                goto out;
        }

        /* expected @parent was found from the inode cache */
        state->loc_now->parent = inode_ref (parent);

        inode = inode_grep (active_xl->itable, parent, resolve->bname);
        if (!inode) {
                resolve->op_ret   = -1;
                resolve->op_errno = ENOENT;
                ret = 1;
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
fuse_resolve_entry (fuse_state_t *state)
{
        int    ret = 0;
        loc_t *loc = NULL;

        loc  = state->loc_now;

        ret = fuse_resolve_entry_simple (state);
        if (ret > 0) {
                loc_wipe (loc);
                fuse_resolve_path_deep (state);
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
        xlator_t     *active_xl = NULL;
        fuse_resolve_t *resolve   = NULL;
        inode_t      *inode     = NULL;
        int           ret       = 0;

        resolve = state->resolve_now;
        active_xl = fuse_active_subvol (state->this);

        inode = inode_find (active_xl->itable, resolve->gfid);
        if (!inode) {
                resolve->op_ret   = -1;
                resolve->op_errno = ENOENT;
                ret = 1;
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
fuse_resolve_inode (fuse_state_t *state)
{
        int                 ret = 0;
        loc_t              *loc = NULL;

        loc  = state->loc_now;

        ret = fuse_resolve_inode_simple (state);

        if (ret > 0) {
                loc_wipe (loc);
                fuse_resolve_path_deep (state);
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

        fuse_resolve_path_deep (state);

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

        } else if (resolve->path) {

                fuse_resolve_path_deep (state);

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
        if (inode_xl == active_xl)
                goto resume;

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

        fuse_resolve_all (state);

        return 0;
resume:
        fn (state);

        return 0;
}
