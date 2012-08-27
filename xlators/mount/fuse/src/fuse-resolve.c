/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
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

#include "fuse-bridge.h"

static int
fuse_resolve_all (fuse_state_t *state);

int fuse_resolve_continue (fuse_state_t *state);
int fuse_resolve_entry_simple (fuse_state_t *state);
int fuse_resolve_inode_simple (fuse_state_t *state);
int fuse_migrate_fd (xlator_t *this, fd_t *fd, xlator_t *old_subvol,
                     xlator_t *new_subvol);

fuse_fd_ctx_t *
fuse_fd_ctx_get (xlator_t *this, fd_t *fd);

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
			uuid_copy (loc->pargfid, loc->parent->gfid);
			loc->name = resolve->bname;
                } else if (loc->inode) {
                        ret = inode_path (loc->inode, NULL, &path);
			uuid_copy (loc->gfid, loc->inode->gfid);
                }
                if (ret)
                        gf_log (THIS->name, GF_LOG_TRACE,
                                "return value inode_path %d", ret);
                loc->path = path;
        }

        return 0;
}


int
fuse_resolve_entry_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
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
                gf_log (this->name, (op_errno == ENOENT)
                        ? GF_LOG_DEBUG : GF_LOG_WARNING,
                        "%s/%s: failed to resolve (%s)",
                        uuid_utoa (resolve_loc->pargfid), resolve_loc->name,
                        strerror (op_errno));
                resolve->op_ret = -1;
                resolve->op_errno = op_errno;
                goto out;
        }

        link_inode = inode_link (inode, resolve_loc->parent,
                                 resolve_loc->name, buf);

	state->loc_now->inode = link_inode;

out:
        loc_wipe (resolve_loc);

        fuse_resolve_continue (state);
        return 0;
}


int
fuse_resolve_entry (fuse_state_t *state)
{
	fuse_resolve_t   *resolve = NULL;
	loc_t            *resolve_loc = NULL;

	resolve = state->resolve_now;
	resolve_loc = &resolve->resolve_loc;

	resolve_loc->parent = inode_ref (state->loc_now->parent);
	uuid_copy (resolve_loc->pargfid, state->loc_now->pargfid);
        resolve_loc->name = resolve->bname;
        resolve_loc->inode = inode_new (state->itable);

        inode_path (resolve_loc->parent, resolve_loc->name,
                    (char **) &resolve_loc->path);

        FUSE_FOP (state, fuse_resolve_entry_cbk, GF_FOP_LOOKUP,
                  lookup, resolve_loc, NULL);

	return 0;
}


int
fuse_resolve_gfid_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno, inode_t *inode,
                       struct iatt *buf, dict_t *xattr, struct iatt *postparent)
{
        fuse_state_t   *state      = NULL;
        fuse_resolve_t *resolve    = NULL;
        inode_t        *link_inode = NULL;
        loc_t          *loc_now   = NULL;

        state = frame->root->state;
        resolve = state->resolve_now;
	loc_now = state->loc_now;

        STACK_DESTROY (frame->root);

        if (op_ret == -1) {
                gf_log (this->name, (op_errno == ENOENT)
                        ? GF_LOG_DEBUG : GF_LOG_WARNING,
                        "%s: failed to resolve (%s)",
                        uuid_utoa (resolve->resolve_loc.gfid),
			strerror (op_errno));
                loc_wipe (&resolve->resolve_loc);

                /* resolve->op_ret can have 3 values: 0, -1, -2.
                 * 0 : resolution was successful.
                 * -1: parent inode could not be resolved.
                 * -2: entry (inode corresponding to path) could not be resolved
                 */

                if (uuid_is_null (resolve->gfid)) {
                        resolve->op_ret = -1;
                } else {
                        resolve->op_ret = -2;
                }

                resolve->op_errno = op_errno;
                goto out;
        }

        loc_wipe (&resolve->resolve_loc);

        link_inode = inode_link (inode, NULL, NULL, buf);

        if (!link_inode)
                goto out;

	if (!uuid_is_null (resolve->gfid)) {
		loc_now->inode = link_inode;
		goto out;
	}

	loc_now->parent = link_inode;
        uuid_copy (loc_now->pargfid, link_inode->gfid);

	fuse_resolve_entry (state);

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
        } else if (!uuid_is_null (resolve->gfid)) {
                uuid_copy (resolve_loc->gfid, resolve->gfid);
        }

	resolve_loc->inode = inode_new (state->itable);
	ret = loc_path (resolve_loc, NULL);

        if (ret <= 0) {
                gf_log (THIS->name, GF_LOG_WARNING,
                        "failed to get the path for inode %s",
                        uuid_utoa (resolve->gfid));
        }

        FUSE_FOP (state, fuse_resolve_gfid_cbk, GF_FOP_LOOKUP,
                  lookup, resolve_loc, NULL);

        return 0;
}


/*
 * Return value:
 * 0 - resolved parent and entry (as necessary)
 * -1 - resolved parent but not entry (though necessary)
 * 1 - resolved neither parent nor entry
 */

int
fuse_resolve_parent_simple (fuse_state_t *state)
{
        fuse_resolve_t *resolve   = NULL;
	loc_t          *loc       = NULL;
        inode_t        *parent    = NULL;
        inode_t        *inode     = NULL;

        resolve = state->resolve_now;
	loc = state->loc_now;

	loc->name = resolve->bname;

	parent = resolve->parhint;
	if (parent->table == state->itable) {
		/* no graph switches since */
		loc->parent = inode_ref (parent);
		loc->inode = inode_grep (state->itable, parent, loc->name);

                /* nodeid for root is 1 and we blindly take the latest graph's
                 * table->root as the parhint and because of this there is
                 * ambiguity whether the entry should have existed or not, and
                 * we took the conservative approach of assuming entry should
                 * have been there even though it need not have (bug #804592).
                 */
                if ((loc->inode == NULL)
                    && __is_root_gfid (parent->gfid)) {
                        /* non decisive result - entry missing */
                        return -1;
                }

		/* decisive result - resolution success */
		return 0;
	}

        parent = inode_find (state->itable, resolve->pargfid);
	if (!parent) {
		/* non decisive result - parent missing */
		return 1;
	}

	loc->parent = parent;
        uuid_copy (loc->pargfid, resolve->pargfid);

	inode = inode_grep (state->itable, parent, loc->name);
	if (inode) {
		loc->inode = inode;
		/* decisive result - resolution success */
		return 0;
	}

	/* non decisive result - entry missing */
        return -1;
}


int
fuse_resolve_parent (fuse_state_t *state)
{
        int    ret = 0;

        ret = fuse_resolve_parent_simple (state);
        if (ret > 0) {
                fuse_resolve_gfid (state);
                return 0;
        }

	if (ret < 0) {
		fuse_resolve_entry (state);
		return 0;
	}

        fuse_resolve_continue (state);

        return 0;
}


int
fuse_resolve_inode_simple (fuse_state_t *state)
{
        fuse_resolve_t *resolve   = NULL;
	loc_t          *loc = NULL;
        inode_t        *inode     = NULL;

        resolve = state->resolve_now;
	loc = state->loc_now;

	inode = resolve->hint;
	if (inode->table == state->itable) {
		inode_ref (inode);
		goto found;
	}

        inode = inode_find (state->itable, resolve->gfid);
        if (inode)
		goto found;

        return 1;
found:
	loc->inode = inode;
	return 0;
}


int
fuse_resolve_inode (fuse_state_t *state)
{
        int                 ret = 0;

        ret = fuse_resolve_inode_simple (state);

        if (ret > 0) {
                fuse_resolve_gfid (state);
                return 0;
        }

        fuse_resolve_continue (state);

        return 0;
}


int
fuse_migrate_fd_task (void *data)
{
        int                     ret   = -1;
        fuse_fd_ctx_t          *fdctx = NULL;
        fuse_state_t           *state = NULL;

        state = data;
        if (state == NULL) {
                goto out;
        }

        ret = fuse_migrate_fd (state->this, state->fd,
                               state->fd->inode->table->xl,
                               state->active_subvol);

        fdctx = fuse_fd_ctx_check_n_create (state->this, state->fd);
        if (fdctx != NULL) {
                if (ret < 0) {
                        fdctx->migration_failed = 1;
                } else {
                        fdctx->migration_failed = 0;
                }
        }

        ret = 0;

out:
        return ret;
}


static inline int
fuse_migrate_fd_error (xlator_t *this, fd_t *fd)
{
        fuse_fd_ctx_t *fdctx = NULL;
        char           error = 0;

        fdctx = fuse_fd_ctx_get (this, fd);
        if (fdctx != NULL) {
                if (fdctx->migration_failed) {
                        error = 1;
                }
        }

        return error;
}


static int
fuse_resolve_fd (fuse_state_t *state)
{
        fuse_resolve_t         *resolve            = NULL;
	fd_t                   *fd                 = NULL;
	xlator_t               *active_subvol      = NULL;
        int                     ret                = 0;
        char                    fd_migration_error = 0;

        resolve = state->resolve_now;

        fd = resolve->fd;
	active_subvol = fd->inode->table->xl;

        fd_migration_error = fuse_migrate_fd_error (state->this, fd);
        if (fd_migration_error) {
                resolve->op_ret = -1;
                resolve->op_errno = EBADF;
        } else if (state->active_subvol != active_subvol) {
                ret = synctask_new (state->this->ctx->env, fuse_migrate_fd_task,
                                    NULL, NULL, state);

                fd_migration_error = fuse_migrate_fd_error (state->this, fd);

                if ((ret == -1) || fd_migration_error
                    || (state->active_subvol != fd->inode->table->xl)) {
                        if (ret == -1) {
                                gf_log (state->this->name, GF_LOG_WARNING,
                                        "starting sync-task to migrate fd (%p)"
                                        " failed", fd);
                        } else {
                                gf_log (state->this->name, GF_LOG_WARNING,
                                        "fd migration of fd (%p) failed", fd);
                        }

                        resolve->op_ret = -1;
                        resolve->op_errno = EBADF;
                } else {
                        gf_log (state->this->name, GF_LOG_DEBUG,
                                "fd (%p) migrated successfully in resolver",
                                fd);
                }
        }

        if ((resolve->op_ret == -1) && (resolve->op_errno == EBADF)) {
                gf_log ("fuse-resolve", GF_LOG_WARNING, "migration of fd (%p) "
                        "did not complete, failing fop with EBADF", fd);
        }

	/* state->active_subvol = active_subvol; */

        fuse_resolve_continue (state);

        return 0;
}


int
fuse_gfid_set (fuse_state_t *state)
{
        int   ret = 0;

        if (uuid_is_null (state->gfid))
                goto out;

        if (!state->xdata)
                state->xdata = dict_new ();

        if (!state->xdata) {
                ret = -1;
                goto out;
        }

        ret = dict_set_static_bin (state->xdata, "gfid-req",
                                   state->gfid, sizeof (state->gfid));
out:
        return ret;
}


int
fuse_resolve_entry_init (fuse_state_t *state, fuse_resolve_t *resolve,
			 ino_t par, char *name)
{
	inode_t       *parent = NULL;

	parent = fuse_ino_to_inode (par, state->this);
	uuid_copy (resolve->pargfid, parent->gfid);
	resolve->parhint = parent;
	resolve->bname = gf_strdup (name);

	return 0;
}


int
fuse_resolve_inode_init (fuse_state_t *state, fuse_resolve_t *resolve,
			 ino_t ino)
{
	inode_t       *inode = NULL;

	inode = fuse_ino_to_inode (ino, state->this);
	uuid_copy (resolve->gfid, inode->gfid);
	resolve->hint = inode;

	return 0;
}


int
fuse_resolve_fd_init (fuse_state_t *state, fuse_resolve_t *resolve,
		      fd_t *fd)
{
	resolve->fd = fd_ref (fd);

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

                fuse_resolve_parent (state);

        } else if (!uuid_is_null (resolve->gfid)) {

                fuse_resolve_inode (state);

        } else {
                fuse_resolve_all (state);
        }

        return 0;
}


static int
fuse_resolve_done (fuse_state_t *state)
{
        fuse_resume_fn_t fn = NULL;

        fn = state->resume_fn;

	fn (state);

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
fuse_resolve_continue (fuse_state_t *state)
{
        fuse_resolve_loc_touchup (state);

        fuse_resolve_all (state);

        return 0;
}


int
fuse_resolve_and_resume (fuse_state_t *state, fuse_resume_fn_t fn)
{
        fuse_gfid_set (state);

        state->resume_fn = fn;

        fuse_resolve_all (state);

        return 0;
}
