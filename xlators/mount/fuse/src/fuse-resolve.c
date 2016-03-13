/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
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

        resolve = state->resolve_now;
        loc     = state->loc_now;

        loc_touchup (loc, resolve->bname);
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
        uint64_t        ctx_value  = LOOKUP_NOT_NEEDED;

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
        if (link_inode == inode)
                inode_ctx_set (link_inode, this, &ctx_value);
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
	gf_uuid_copy (resolve_loc->pargfid, state->loc_now->pargfid);
        resolve_loc->name = resolve->bname;

        resolve_loc->inode = inode_grep (state->itable, resolve->parhint,
                                         resolve->bname);
        if (!resolve_loc->inode) {
                resolve_loc->inode = inode_new (state->itable);
        }
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
        loc_t          *loc_now    = NULL;
        inode_t        *tmp_inode  = NULL;
        uint64_t        ctx_value  = LOOKUP_NOT_NEEDED;

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

                if (gf_uuid_is_null (resolve->gfid)) {
                        resolve->op_ret = -1;
                } else {
                        resolve->op_ret = -2;
                }

                resolve->op_errno = op_errno;
                goto out;
        }

        link_inode = inode_link (inode, NULL, NULL, buf);
        if (link_inode == inode)
                inode_ctx_set (link_inode, this, &ctx_value);

        loc_wipe (&resolve->resolve_loc);

        if (!link_inode)
                goto out;

	if (!gf_uuid_is_null (resolve->gfid)) {
		loc_now->inode = link_inode;
		goto out;
	}

	loc_now->parent = link_inode;
        gf_uuid_copy (loc_now->pargfid, link_inode->gfid);

        tmp_inode = inode_grep (state->itable, link_inode, resolve->bname);
        if (tmp_inode && (!inode_needs_lookup (tmp_inode, THIS))) {
                loc_now->inode = tmp_inode;
                goto out;
        }

        inode_unref (tmp_inode);
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

        if (!gf_uuid_is_null (resolve->pargfid)) {
                gf_uuid_copy (resolve_loc->gfid, resolve->pargfid);
        } else if (!gf_uuid_is_null (resolve->gfid)) {
                gf_uuid_copy (resolve_loc->gfid, resolve->gfid);
        }

	/* inode may already exist in case we are looking up an inode which was
	   linked through readdirplus */
	resolve_loc->inode = inode_find (state->itable, resolve_loc->gfid);
	if (!resolve_loc->inode)
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
        xlator_t       *this      = NULL;

        resolve = state->resolve_now;
	loc = state->loc_now;
        this = state->this;

	loc->name = resolve->bname;

	parent = resolve->parhint;
	if (parent->table == state->itable) {
		if (inode_needs_lookup (parent, THIS))
			return 1;

		/* no graph switches since */
		loc->parent = inode_ref (parent);
		gf_uuid_copy (loc->pargfid, parent->gfid);
		loc->inode = inode_grep (state->itable, parent, loc->name);

                /* nodeid for root is 1 and we blindly take the latest graph's
                 * table->root as the parhint and because of this there is
                 * ambiguity whether the entry should have existed or not, and
                 * we took the conservative approach of assuming entry should
                 * have been there even though it need not have (bug #804592).
                 */

                if (loc->inode && inode_needs_lookup (loc->inode, THIS)) {
                        inode_unref (loc->inode);
                        loc->inode = NULL;
                        return -1;
                }

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
	if (inode_needs_lookup (parent, THIS)) {
		inode_unref (parent);
		return 1;
	}

	loc->parent = parent;
        gf_uuid_copy (loc->pargfid, resolve->pargfid);

	inode = inode_grep (state->itable, parent, loc->name);
	if (inode && !inode_needs_lookup (inode, this)) {
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
	if (inode->table == state->itable)
		inode_ref (inode);
	else
		inode = inode_find (state->itable, resolve->gfid);

        if (inode) {
		if (!inode_needs_lookup (inode, THIS))
			goto found;
		/* inode was linked through readdirplus */
		inode_unref (inode);
	}

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
        int            ret        = -1;
        fuse_state_t  *state      = NULL;
        fd_t          *basefd     = NULL, *oldfd = NULL;
        fuse_fd_ctx_t *basefd_ctx = NULL;
        xlator_t      *old_subvol = NULL;

        state = data;
        if (state == NULL) {
                goto out;
        }

        basefd = state->fd;

        basefd_ctx = fuse_fd_ctx_get (state->this, basefd);
        if (!basefd_ctx)
                goto out;

        LOCK (&basefd->lock);
        {
                oldfd = basefd_ctx->activefd ? basefd_ctx->activefd : basefd;
                fd_ref (oldfd);
        }
        UNLOCK (&basefd->lock);

        old_subvol = oldfd->inode->table->xl;

        ret = fuse_migrate_fd (state->this, basefd, old_subvol,
                               state->active_subvol);

        LOCK (&basefd->lock);
        {
                if (ret < 0) {
                        basefd_ctx->migration_failed = 1;
                } else {
                        basefd_ctx->migration_failed = 0;
                }
        }
        UNLOCK (&basefd->lock);

        ret = 0;

out:
        if (oldfd)
                fd_unref (oldfd);

        return ret;
}


static int
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

#define FUSE_FD_GET_ACTIVE_FD(activefd, basefd)                 \
        do {                                                    \
                LOCK (&basefd->lock);                           \
                {                                               \
                        activefd = basefd_ctx->activefd ?       \
                                basefd_ctx->activefd : basefd;  \
                        if (activefd != basefd) {               \
                                fd_ref (activefd);              \
                        }                                       \
                }                                               \
                UNLOCK (&basefd->lock);                         \
                                                                \
                if (activefd == basefd) {                       \
                        fd_ref (activefd);                      \
                }                                               \
        } while (0);


static int
fuse_resolve_fd (fuse_state_t *state)
{
        fuse_resolve_t *resolve            = NULL;
	fd_t           *basefd             = NULL, *activefd = NULL;
	xlator_t       *active_subvol      = NULL, *this = NULL;
        int             ret                = 0;
        char            fd_migration_error = 0;
        fuse_fd_ctx_t  *basefd_ctx         = NULL;

        resolve = state->resolve_now;

        this = state->this;

        basefd = resolve->fd;
        basefd_ctx = fuse_fd_ctx_get (this, basefd);
        if (basefd_ctx == NULL) {
                gf_log (state->this->name, GF_LOG_WARNING,
                        "fdctx is NULL for basefd (ptr:%p inode-gfid:%s), "
                        "resolver erroring out with errno EINVAL",
                        basefd, uuid_utoa (basefd->inode->gfid));
                resolve->op_ret = -1;
                resolve->op_errno = EINVAL;
                goto resolve_continue;
        }

        FUSE_FD_GET_ACTIVE_FD (activefd, basefd);

        active_subvol = activefd->inode->table->xl;

        fd_migration_error = fuse_migrate_fd_error (state->this, basefd);
        if (fd_migration_error) {
                resolve->op_ret = -1;
                resolve->op_errno = EBADF;
        } else if (state->active_subvol != active_subvol) {
                ret = synctask_new (state->this->ctx->env, fuse_migrate_fd_task,
                                    NULL, NULL, state);

                fd_migration_error = fuse_migrate_fd_error (state->this,
                                                            basefd);
                fd_unref (activefd);

                FUSE_FD_GET_ACTIVE_FD (activefd, basefd);
                active_subvol = activefd->inode->table->xl;

                if ((ret == -1) || fd_migration_error
                    || (state->active_subvol != active_subvol)) {
                        if (ret == -1) {
                                gf_log (state->this->name, GF_LOG_WARNING,
                                        "starting sync-task to migrate "
                                        "basefd (ptr:%p inode-gfid:%s) failed "
                                        "(old-subvolume:%s-%d "
                                        "new-subvolume:%s-%d)",
                                        basefd,
                                        uuid_utoa (basefd->inode->gfid),
                                        active_subvol->name,
                                        active_subvol->graph->id,
                                        state->active_subvol->name,
                                        state->active_subvol->graph->id);
                        } else {
                                gf_log (state->this->name, GF_LOG_WARNING,
                                        "fd migration of basefd "
                                        "(ptr:%p inode-gfid:%s) failed "
                                        "(old-subvolume:%s-%d "
                                        "new-subvolume:%s-%d)",
                                        basefd,
                                        uuid_utoa (basefd->inode->gfid),
                                        active_subvol->name,
                                        active_subvol->graph->id,
                                        state->active_subvol->name,
                                        state->active_subvol->graph->id);
                        }

                        resolve->op_ret = -1;
                        resolve->op_errno = EBADF;
                } else {
                        gf_log (state->this->name, GF_LOG_DEBUG,
                                "basefd (ptr:%p inode-gfid:%s) migrated "
                                "successfully in resolver "
                                "(old-subvolume:%s-%d new-subvolume:%s-%d)",
                                basefd, uuid_utoa (basefd->inode->gfid),
                                active_subvol->name, active_subvol->graph->id,
                                state->active_subvol->name,
                                state->active_subvol->graph->id);
                }
        }

        if ((resolve->op_ret == -1) && (resolve->op_errno == EBADF)) {
                gf_log ("fuse-resolve", GF_LOG_WARNING,
                        "migration of basefd (ptr:%p inode-gfid:%s) "
                        "did not complete, failing fop with EBADF "
                        "(old-subvolume:%s-%d new-subvolume:%s-%d)", basefd,
                        uuid_utoa (basefd->inode->gfid),
                        active_subvol->name, active_subvol->graph->id,
                        state->active_subvol->name,
                        state->active_subvol->graph->id);
        }

        if (activefd != basefd) {
                state->fd = fd_ref (activefd);
                fd_unref (basefd);
        }

	/* state->active_subvol = active_subvol; */

resolve_continue:
        if (activefd != NULL) {
                fd_unref (activefd);
        }

        fuse_resolve_continue (state);

        return 0;
}


int
fuse_gfid_set (fuse_state_t *state)
{
        int   ret = 0;

        if (gf_uuid_is_null (state->gfid))
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
	gf_uuid_copy (resolve->pargfid, parent->gfid);
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
	gf_uuid_copy (resolve->gfid, inode->gfid);
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

        } else if (!gf_uuid_is_null (resolve->pargfid)) {

                fuse_resolve_parent (state);

        } else if (!gf_uuid_is_null (resolve->gfid)) {

                fuse_resolve_inode (state);

        } else {
                fuse_resolve_all (state);
        }

        return 0;
}

static int
fuse_resolve_done (fuse_state_t *state)
{
        fuse_fop_resume (state);
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
