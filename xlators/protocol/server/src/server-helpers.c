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

#include "server.h"
#include "server-helpers.h"

#include <fnmatch.h>

int
server_decode_groups (call_frame_t *frame, rpcsvc_request_t *req)
{
        int     i = 0;

        if ((!frame) || (!req))
                return 0;

        frame->root->ngrps = req->auxgidcount;
        if (frame->root->ngrps == 0)
                return 0;

        if (frame->root->ngrps > GF_REQUEST_MAXGROUPS)
                return -1;

        for (; i < frame->root->ngrps; ++i)
                frame->root->groups[i] = req->auxgids[i];

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

        if (loc->path)
                GF_FREE ((void *)loc->path);
}


void
server_resolve_wipe (server_resolve_t *resolve)
{
        struct resolve_comp *comp = NULL;
        int                  i = 0;

        if (resolve->path)
                GF_FREE ((void *)resolve->path);

        if (resolve->bname)
                GF_FREE ((void *)resolve->bname);

        if (resolve->resolved)
                GF_FREE ((void *)resolve->resolved);

        loc_wipe (&resolve->deep_loc);

        comp = resolve->components;
        if (comp) {
                for (i = 0; comp[i].basename; i++) {
                        if (comp[i].inode)
                                inode_unref (comp[i].inode);
                }
                GF_FREE ((void *)resolve->components);
        }
}


void
free_state (server_state_t *state)
{
        if (state->conn) {
                //xprt_svc_unref (state->conn);
                state->conn = NULL;
        }

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

        if (state->volume)
                GF_FREE ((void *)state->volume);

        if (state->name)
                GF_FREE ((void *)state->name);

        server_loc_wipe (&state->loc);
        server_loc_wipe (&state->loc2);

        server_resolve_wipe (&state->resolve);
        server_resolve_wipe (&state->resolve2);

        GF_FREE (state);
}


int
gf_add_locker (struct _lock_table *table, const char *volume,
               loc_t *loc, fd_t *fd, pid_t pid, uint64_t owner,
               glusterfs_fop_t type)
{
        int32_t         ret = -1;
        struct _locker *new = NULL;

        new = GF_CALLOC (1, sizeof (struct _locker), gf_server_mt_locker_t);
        if (new == NULL) {
                gf_log ("server", GF_LOG_ERROR,
                        "failed to allocate memory for \'struct _locker\'");
                goto out;
        }
        INIT_LIST_HEAD (&new->lockers);

        new->volume = gf_strdup (volume);

        if (fd == NULL) {
                loc_copy (&new->loc, loc);
        } else {
                new->fd = fd_ref (fd);
        }

        new->pid   = pid;
        new->owner = owner;

        LOCK (&table->lock);
        {
                if (type == GF_FOP_ENTRYLK)
                        list_add_tail (&new->lockers, &table->entrylk_lockers);
                else
                        list_add_tail (&new->lockers, &table->inodelk_lockers);
        }
        UNLOCK (&table->lock);
out:
        return ret;
}


int
gf_del_locker (struct _lock_table *table, const char *volume,
               loc_t *loc, fd_t *fd, uint64_t owner, glusterfs_fop_t type)
{
        struct _locker    *locker = NULL;
        struct _locker    *tmp = NULL;
        int32_t            ret = 0;
        struct list_head  *head = NULL;
        struct list_head   del;

        INIT_LIST_HEAD (&del);

        LOCK (&table->lock);
        {
                if (type == GF_FOP_ENTRYLK) {
                        head = &table->entrylk_lockers;
                } else {
                        head = &table->inodelk_lockers;
                }

                list_for_each_entry_safe (locker, tmp, head, lockers) {
                        if (locker->fd && fd &&
                            (locker->fd == fd) && (locker->owner == owner)
                            && !strcmp (locker->volume, volume)) {
                                list_move_tail (&locker->lockers, &del);
                        } else if (locker->loc.inode &&
                                   loc &&
                                   (locker->loc.inode == loc->inode) &&
                                   (locker->owner == owner)
                                   && !strcmp (locker->volume, volume)) {
                                list_move_tail (&locker->lockers, &del);
                        }
                }
        }
        UNLOCK (&table->lock);

        tmp = NULL;
        locker = NULL;

        list_for_each_entry_safe (locker, tmp, &del, lockers) {
                list_del_init (&locker->lockers);
                if (locker->fd)
                        fd_unref (locker->fd);
                else
                        loc_wipe (&locker->loc);

                GF_FREE (locker->volume);
                GF_FREE (locker);
        }

        return ret;
}

static struct _lock_table *
gf_lock_table_new (void)
{
        struct _lock_table *new = NULL;

        new = GF_CALLOC (1, sizeof (struct _lock_table), gf_server_mt_lock_table_t);
        if (new == NULL) {
                gf_log ("server-protocol", GF_LOG_CRITICAL,
                        "failed to allocate memory for new lock table");
                goto out;
        }
        INIT_LIST_HEAD (&new->entrylk_lockers);
        INIT_LIST_HEAD (&new->inodelk_lockers);
        LOCK_INIT (&new->lock);
out:
        return new;
}

static int
server_nop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno)
{
        server_state_t *state = NULL;

        state = CALL_STATE(frame);

        if (state)
                free_state (state);
        STACK_DESTROY (frame->root);
        return 0;
}

int
do_lock_table_cleanup (xlator_t *this, server_connection_t *conn,
                       call_frame_t *frame, struct _lock_table *ltable)
{
        struct list_head  inodelk_lockers, entrylk_lockers;
        call_frame_t     *tmp_frame = NULL;
        struct gf_flock      flock = {0, };
        xlator_t         *bound_xl = NULL;
        struct _locker   *locker = NULL, *tmp = NULL;
        int               ret = -1;
        char             *path = NULL;

        bound_xl = conn->bound_xl;
        INIT_LIST_HEAD (&inodelk_lockers);
        INIT_LIST_HEAD (&entrylk_lockers);

        LOCK (&ltable->lock);
        {
                list_splice_init (&ltable->inodelk_lockers,
                                  &inodelk_lockers);

                list_splice_init (&ltable->entrylk_lockers, &entrylk_lockers);
        }
        UNLOCK (&ltable->lock);

        GF_FREE (ltable);

        flock.l_type  = F_UNLCK;
        flock.l_start = 0;
        flock.l_len   = 0;
        list_for_each_entry_safe (locker,
                                  tmp, &inodelk_lockers, lockers) {
                tmp_frame = copy_frame (frame);
                if (tmp_frame == NULL) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "out of memory");
                        goto out;
                }
                /*
                   lock owner = 0 is a special case that tells posix-locks
                   to release all locks from this transport
                */
                tmp_frame->root->pid      = 0;
                tmp_frame->root->lk_owner = 0;
                tmp_frame->root->trans    = conn;

                if (locker->fd) {
                        GF_ASSERT (locker->fd->inode);

                        ret = inode_path (locker->fd->inode, NULL, &path);

                        if (ret > 0) {
                                gf_log (this->name, GF_LOG_INFO, "finodelk "
                                        "released on %s", path);
                                GF_FREE (path);
                        } else {

                                gf_log (this->name, GF_LOG_INFO, "finodelk "
                                        "released on ino %"PRId64" with gfid %s",
                                        locker->fd->inode->ino,
                                        uuid_utoa (locker->fd->inode->gfid));
                        }

                        STACK_WIND (tmp_frame, server_nop_cbk, bound_xl,
                                    bound_xl->fops->finodelk,
                                    locker->volume,
                                    locker->fd, F_SETLK, &flock);
                        fd_unref (locker->fd);
                } else {
                        gf_log (this->name, GF_LOG_INFO, "inodelk released "
                                "on %s", locker->loc.path);

                        STACK_WIND (tmp_frame, server_nop_cbk, bound_xl,
                                    bound_xl->fops->inodelk,
                                    locker->volume,
                                    &(locker->loc), F_SETLK, &flock);
                        loc_wipe (&locker->loc);
                }

                GF_FREE (locker->volume);

                list_del_init (&locker->lockers);
                GF_FREE (locker);
        }

        tmp = NULL;
        locker = NULL;
        list_for_each_entry_safe (locker, tmp, &entrylk_lockers, lockers) {
                tmp_frame = copy_frame (frame);

                tmp_frame->root->lk_owner = 0;
                tmp_frame->root->pid      = 0;
                tmp_frame->root->trans    = conn;

                if (locker->fd) {
                        GF_ASSERT (locker->fd->inode);

                        ret = inode_path (locker->fd->inode, NULL, &path);

                        if (ret > 0) {
                                gf_log (this->name, GF_LOG_INFO, "fentrylk "
                                        "released on %s", path);
                                GF_FREE (path);
                        }  else {

                                gf_log (this->name, GF_LOG_INFO, "fentrylk "
                                        "released on ino %lu", locker->fd->inode->ino);
                        }

                        STACK_WIND (tmp_frame, server_nop_cbk, bound_xl,
                                    bound_xl->fops->fentrylk,
                                    locker->volume,
                                    locker->fd, NULL,
                                    ENTRYLK_UNLOCK, ENTRYLK_WRLCK);
                        fd_unref (locker->fd);
                } else {
                        gf_log (this->name, GF_LOG_INFO, "entrylk released "
                                "on %s", locker->loc.path);

                        STACK_WIND (tmp_frame, server_nop_cbk, bound_xl,
                                    bound_xl->fops->entrylk,
                                    locker->volume,
                                    &(locker->loc), NULL,
                                    ENTRYLK_UNLOCK, ENTRYLK_WRLCK);
                        loc_wipe (&locker->loc);
                }

                GF_FREE (locker->volume);

                list_del_init (&locker->lockers);
                GF_FREE (locker);
        }
        ret = 0;

out:
        return ret;
}


static int
server_connection_cleanup_flush_cbk (call_frame_t *frame, void *cookie,
                                     xlator_t *this, int32_t op_ret,
                                     int32_t op_errno)
{
        fd_t *fd = NULL;

        fd = frame->local;

        fd_unref (fd);
        frame->local = NULL;

        STACK_DESTROY (frame->root);
        return 0;
}


int
do_fd_cleanup (xlator_t *this, server_connection_t *conn, call_frame_t *frame,
               fdentry_t *fdentries, int fd_count)
{
        fd_t               *fd = NULL;
        int                 i = 0, ret = -1;
        call_frame_t       *tmp_frame = NULL;
        xlator_t           *bound_xl = NULL;
        char               *path     = NULL;

        bound_xl = conn->bound_xl;
        for (i = 0;i < fd_count; i++) {
                fd = fdentries[i].fd;

                if (fd != NULL) {
                        tmp_frame = copy_frame (frame);
                        if (tmp_frame == NULL) {
                                gf_log (this->name, GF_LOG_ERROR,
                                        "out of memory");
                                goto out;
                        }

                        GF_ASSERT (fd->inode);

                        ret = inode_path (fd->inode, NULL, &path);

                        if (ret > 0) {
                                gf_log (this->name, GF_LOG_INFO, "fd cleanup on "
                                        "%s", path);
                                GF_FREE (path);
                        }  else {

                                gf_log (this->name, GF_LOG_INFO, "fd cleanup on "
                                        "ino %"PRId64" with gfid %s",
                                        fd->inode->ino,
                                        uuid_utoa (fd->inode->gfid));
                        }

                        tmp_frame->local = fd;

                        tmp_frame->root->pid = 0;
                        tmp_frame->root->trans = conn;
                        tmp_frame->root->lk_owner = 0;
                        STACK_WIND (tmp_frame,
                                    server_connection_cleanup_flush_cbk,
                                    bound_xl, bound_xl->fops->flush, fd);
                }
        }

        GF_FREE (fdentries);
        ret = 0;

out:
        return ret;
}

int
do_connection_cleanup (xlator_t *this, server_connection_t *conn,
                       struct _lock_table *ltable, fdentry_t *fdentries, int fd_count)
{
        int             ret = 0;
        int             saved_ret = 0;
        call_frame_t   *frame = NULL;
        server_state_t *state = NULL;

        frame = create_frame (this, this->ctx->pool);
        if (frame == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto out;
        }

        saved_ret = do_lock_table_cleanup (this, conn, frame, ltable);

        if (fdentries != NULL) {
                ret = do_fd_cleanup (this, conn, frame, fdentries, fd_count);
        }

        state = CALL_STATE (frame);
        if (state)
                GF_FREE (state);

        STACK_DESTROY (frame->root);

        if (saved_ret || ret) {
                ret = -1;
        }

out:
        return ret;
}


int
server_connection_cleanup (xlator_t *this, server_connection_t *conn)
{
        char                do_cleanup = 0;
        struct _lock_table *ltable = NULL;
        fdentry_t          *fdentries = NULL;
        uint32_t            fd_count = 0;
        int                 ret = 0;

        if (conn == NULL) {
                goto out;
        }

        pthread_mutex_lock (&conn->lock);
        {
                conn->active_transports--;
                if (conn->active_transports == 0) {
                        if (conn->ltable) {
                                ltable = conn->ltable;
                                conn->ltable = gf_lock_table_new ();
                        }

                        if (conn->fdtable) {
                                fdentries = gf_fd_fdtable_get_all_fds (conn->fdtable,
                                                                       &fd_count);
                        }
                        do_cleanup = 1;
                }
        }
        pthread_mutex_unlock (&conn->lock);

        if (do_cleanup && conn->bound_xl)
                ret = do_connection_cleanup (this, conn, ltable, fdentries, fd_count);

out:
        return ret;
}


int
server_connection_destroy (xlator_t *this, server_connection_t *conn)
{
        call_frame_t       *frame = NULL, *tmp_frame = NULL;
        xlator_t           *bound_xl = NULL;
        int32_t             ret = -1;
        server_state_t     *state = NULL;
        struct list_head    inodelk_lockers;
        struct list_head    entrylk_lockers;
        struct _lock_table *ltable = NULL;
        struct _locker     *locker = NULL, *tmp = NULL;
        struct gf_flock        flock = {0,};
        fd_t               *fd = NULL;
        int32_t             i = 0;
        fdentry_t          *fdentries = NULL;
        uint32_t             fd_count = 0;
        char               *path      = NULL;

        if (conn == NULL) {
                ret = 0;
                goto out;
        }

        bound_xl = (xlator_t *) (conn->bound_xl);

        if (bound_xl) {
                /* trans will have ref_count = 1 after this call, but its
                   ok since this function is called in
                   GF_EVENT_TRANSPORT_CLEANUP */
                frame = create_frame (this, this->ctx->pool);

                pthread_mutex_lock (&(conn->lock));
                {
                        if (conn->ltable) {
                                ltable = conn->ltable;
                                conn->ltable = NULL;
                        }
                }
                pthread_mutex_unlock (&conn->lock);

                INIT_LIST_HEAD (&inodelk_lockers);
                INIT_LIST_HEAD (&entrylk_lockers);

                if (ltable) {
                        LOCK (&ltable->lock);
                        {
                                list_splice_init (&ltable->inodelk_lockers,
                                                  &inodelk_lockers);

                                list_splice_init (&ltable->entrylk_lockers, &entrylk_lockers);
                        }
                        UNLOCK (&ltable->lock);
                        GF_FREE (ltable);
                }

                flock.l_type  = F_UNLCK;
                flock.l_start = 0;
                flock.l_len   = 0;
                list_for_each_entry_safe (locker,
                                          tmp, &inodelk_lockers, lockers) {
                        tmp_frame = copy_frame (frame);
                        /*
                           lock_owner = 0 is a special case that tells posix-locks
                           to release all locks from this transport
                        */
                        tmp_frame->root->lk_owner = 0;
                        tmp_frame->root->trans = conn;

                        if (locker->fd) {
                                GF_ASSERT (locker->fd->inode);

                                ret = inode_path (locker->fd->inode, NULL, &path);

                                if (ret > 0) {
                                        gf_log (this->name, GF_LOG_INFO, "finodelk "
                                                "released on %s", path);
                                        GF_FREE (path);
                                } else {

                                        gf_log (this->name, GF_LOG_INFO, "finodelk "
                                                "released on ino %"PRId64 "with gfid %s",
                                                locker->fd->inode->ino,
                                                uuid_utoa (locker->fd->inode->gfid));
                                }

                                STACK_WIND (tmp_frame, server_nop_cbk, bound_xl,
                                            bound_xl->fops->finodelk,
                                            locker->volume,
                                            locker->fd, F_SETLK, &flock);
                                fd_unref (locker->fd);
                        } else {
                                gf_log (this->name, GF_LOG_INFO, "inodelk "
                                        "released on %s", locker->loc.path);

                                STACK_WIND (tmp_frame, server_nop_cbk, bound_xl,
                                            bound_xl->fops->inodelk,
                                            locker->volume,
                                            &(locker->loc), F_SETLK, &flock);
                                loc_wipe (&locker->loc);
                        }

                        GF_FREE (locker->volume);

                        list_del_init (&locker->lockers);
                        GF_FREE (locker);
                }

                tmp = NULL;
                locker = NULL;
                list_for_each_entry_safe (locker, tmp, &entrylk_lockers, lockers) {
                        tmp_frame = copy_frame (frame);

                        tmp_frame->root->lk_owner = 0;
                        tmp_frame->root->trans = conn;

                        if (locker->fd) {
                                GF_ASSERT (locker->fd->inode);

                                ret = inode_path (locker->fd->inode, NULL, &path);

                                if (ret > 0) {
                                        gf_log (this->name, GF_LOG_INFO, "fentrylk "
                                                "released on %s", path);

                                        GF_FREE (path);
                                } else {

                                        gf_log (this->name, GF_LOG_INFO, "fentrylk "
                                                "released on ino %"PRId64" and gfid= %s",
                                                locker->fd->inode->ino,
                                                uuid_utoa (locker->fd->inode->gfid));
                                }

                                STACK_WIND (tmp_frame, server_nop_cbk, bound_xl,
                                            bound_xl->fops->fentrylk,
                                            locker->volume,
                                            locker->fd, NULL,
                                            ENTRYLK_UNLOCK, ENTRYLK_WRLCK);
                                fd_unref (locker->fd);
                        } else {
                                gf_log (this->name, GF_LOG_INFO, "entrylk "
                                        "released on %s", locker->loc.path);

                                STACK_WIND (tmp_frame, server_nop_cbk, bound_xl,
                                            bound_xl->fops->entrylk,
                                            locker->volume,
                                            &(locker->loc), NULL,
                                            ENTRYLK_UNLOCK, ENTRYLK_WRLCK);
                                loc_wipe (&locker->loc);
                        }

                        GF_FREE (locker->volume);

                        list_del_init (&locker->lockers);
                        GF_FREE (locker);
                }

                pthread_mutex_lock (&(conn->lock));
                {
                        if (conn->fdtable) {
                                fdentries = gf_fd_fdtable_get_all_fds (conn->fdtable,
                                                                       &fd_count);
                                gf_fd_fdtable_destroy (conn->fdtable);
                                conn->fdtable = NULL;
                        }
                }
                pthread_mutex_unlock (&conn->lock);

                if (fdentries != NULL) {
                        for (i = 0; i < fd_count; i++) {
                                fd = fdentries[i].fd;
                                if (fd != NULL) {
                                        tmp_frame = copy_frame (frame);
                                        tmp_frame->local = fd;

                                        STACK_WIND (tmp_frame,
                                                    server_connection_cleanup_flush_cbk,
                                                    bound_xl,
                                                    bound_xl->fops->flush,
                                                    fd);
                                }
                        }
                        GF_FREE (fdentries);
                }
        }

        if (frame) {
                state = CALL_STATE (frame);
                if (state)
                        GF_FREE (state);
                STACK_DESTROY (frame->root);
        }

        gf_log (this->name, GF_LOG_INFO, "destroyed connection of %s",
                conn->id);

        GF_FREE (conn->id);
        GF_FREE (conn);

out:
        return ret;
}


server_connection_t *
server_connection_get (xlator_t *this, const char *id)
{
	server_connection_t *conn = NULL;
	server_connection_t *trav = NULL;
	server_conf_t       *conf = NULL;

        conf = this->private;

        pthread_mutex_lock (&conf->mutex);
        {
                list_for_each_entry (trav, &conf->conns, list) {
                        if (!strcmp (id, trav->id)) {
                                conn = trav;
                                break;
                        }
                }

                if (!conn) {
                        conn = (void *) GF_CALLOC (1, sizeof (*conn),
                                                   gf_server_mt_conn_t);
                        if (!conn)
                                goto unlock;

                        conn->id = gf_strdup (id);
                        conn->fdtable = gf_fd_fdtable_alloc ();
                        conn->ltable  = gf_lock_table_new ();
                        conn->this    = this;
                        pthread_mutex_init (&conn->lock, NULL);

			list_add (&conn->list, &conf->conns);
		}

                conn->ref++;
                conn->active_transports++;
	}
unlock:
	pthread_mutex_unlock (&conf->mutex);

        return conn;
}


void
server_connection_put (xlator_t *this, server_connection_t *conn)
{
        server_conf_t       *conf = NULL;
        server_connection_t *todel = NULL;

        if (conn == NULL) {
                goto out;
        }

        conf = this->private;

        if (conf == NULL) {
                goto out;
        }

        pthread_mutex_lock (&conf->mutex);
        {
                conn->ref--;

                if (!conn->ref) {
                        list_del_init (&conn->list);
                        todel = conn;
                }
        }
        pthread_mutex_unlock (&conf->mutex);

        if (todel) {
                server_connection_destroy (this, todel);
        }

out:
        return;
}

static call_frame_t *
server_alloc_frame (rpcsvc_request_t *req)
{
        call_frame_t         *frame = NULL;
        server_state_t       *state = NULL;
        server_connection_t  *conn  = NULL;

        GF_VALIDATE_OR_GOTO("server", req, out);
        GF_VALIDATE_OR_GOTO("server", req->trans, out);
        GF_VALIDATE_OR_GOTO("server", req->svc, out);
        GF_VALIDATE_OR_GOTO("server", req->svc->ctx, out);

        conn = (server_connection_t *)req->trans->xl_private;
        if (!conn)
                goto out;

        frame = create_frame (conn->this, req->svc->ctx->pool);
        GF_VALIDATE_OR_GOTO("server", frame, out);

        state = GF_CALLOC (1, sizeof (*state), gf_server_mt_state_t);
        GF_VALIDATE_OR_GOTO("server", state, out);

        if (conn->bound_xl)
                state->itable = conn->bound_xl->itable;

        state->xprt  = rpc_transport_ref (req->trans);
        state->conn  = conn;

        state->resolve.fd_no = -1;
        state->resolve2.fd_no = -1;

        frame->root->state = state;        /* which socket */
        frame->root->unique = 0;           /* which call */

        frame->this = conn->this;
out:
        return frame;
}



call_frame_t *
get_frame_from_request (rpcsvc_request_t *req)
{
        call_frame_t *frame = NULL;

        frame = server_alloc_frame (req);
        if (!frame)
                goto out;

        frame->root->op       = req->procnum;
        frame->root->type     = req->type;

        frame->root->unique   = req->xid;

        frame->root->uid      = req->uid;
        frame->root->gid      = req->gid;
        frame->root->pid      = req->pid;
        frame->root->trans    = req->trans->xl_private;
        frame->root->lk_owner = req->lk_owner;

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

        ret = dict_get_int32 (this->options, "inode-lru-limit",
                              &conf->inode_lru_limit);
        if (ret < 0) {
                conf->inode_lru_limit = 1024;
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

server_connection_t *
get_server_conn_state (xlator_t *this, rpc_transport_t *xprt)
{
        return (server_connection_t *)xprt->xl_private;
}

server_connection_t *
create_server_conn_state (xlator_t *this, rpc_transport_t *xprt)
{
        server_connection_t *conn = NULL;
        int                  ret = -1;

        conn = GF_CALLOC (1, sizeof (*conn), gf_server_mt_conn_t);
        if (!conn)
                goto out;

        pthread_mutex_init (&conn->lock, NULL);

        conn->fdtable = gf_fd_fdtable_alloc ();
        if (!conn->fdtable)
                goto out;

        conn->ltable  = gf_lock_table_new ();
        if (!conn->ltable)
                goto out;

        conn->this = this;

        xprt->xl_private = conn;

        ret = 0;
out:
        if (ret)
                destroy_server_conn_state (conn);

        return conn;
}

void
destroy_server_conn_state (server_connection_t *conn)
{
        if (!conn) {
                return;
        }

        if (conn->ltable) {
                /* TODO */
                //FREE (conn->ltable);
                ;
        }

        if (conn->fdtable)
                gf_fd_fdtable_destroy (conn->fdtable);

        pthread_mutex_destroy (&conn->lock);

        GF_FREE (conn);

        return;
}


void
print_caller (char *str, int size, call_frame_t *frame)
{
        server_state_t  *state = NULL;

        state = CALL_STATE (frame);

        snprintf (str, size,
                  " Callid=%"PRId64", Client=%s",
                  frame->root->unique,
                  state->xprt->peerinfo.identifier);

        return;
}


void
server_print_resolve (char *str, int size, server_resolve_t *resolve)
{
        int filled = 0;

        if (!resolve) {
                snprintf (str, size, "<nul>");
                return;
        }

        filled += snprintf (str + filled, size - filled,
                            " Resolve={");
        if (resolve->fd_no != -1)
                filled += snprintf (str + filled, size - filled,
                                    "fd=%"PRId64",", (uint64_t) resolve->fd_no);
        if (resolve->ino)
                filled += snprintf (str + filled, size - filled,
                                    "ino=%"PRIu64",", (uint64_t) resolve->ino);
        if (resolve->par)
                filled += snprintf (str + filled, size - filled,
                                    "par=%"PRIu64",", (uint64_t) resolve->par);
        if (resolve->gen)
                filled += snprintf (str + filled, size - filled,
                                    "gen=%"PRIu64",", (uint64_t) resolve->gen);
        if (resolve->bname)
                filled += snprintf (str + filled, size - filled,
                                    "bname=%s,", resolve->bname);
        if (resolve->path)
                filled += snprintf (str + filled, size - filled,
                                    "path=%s", resolve->path);

        snprintf (str + filled, size - filled, "}");
}


void
server_print_loc (char *str, int size, loc_t *loc)
{
        int filled = 0;

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
}


void
server_print_params (char *str, int size, server_state_t *state)
{
        int filled = 0;

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

        snprintf (str + filled, size - filled,
                  "bound_xl=%s}", state->conn->bound_xl->name);
}

int
server_resolve_is_empty (server_resolve_t *resolve)
{
        if (resolve->fd_no != -1)
                return 0;

        if (resolve->ino != 0)
                return 0;

        if (resolve->gen != 0)
                return 0;

        if (resolve->par != 0)
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

        this = frame->this;
        conf = this->private;

        if (!conf || !conf->trace)
                return;

        state = CALL_STATE (frame);

        print_caller (caller, 256, frame);

        switch (frame->root->type) {
        case GF_OP_TYPE_FOP:
                op = gf_fop_list[frame->root->op];
                break;
        case GF_OP_TYPE_MGMT:
                op = gf_mgmt_list[frame->root->op];
                break;
        default:
                op = "";
        }

        fdstr[0] = '\0';
        if (state->fd)
                snprintf (fdstr, 32, " fd=%p", state->fd);

        gf_log (this->name, GF_LOG_NORMAL,
                "%s%s => (%d, %d)%s",
                op, caller, op_ret, op_errno, fdstr);
}


void
server_print_request (call_frame_t *frame)
{
        server_conf_t   *conf = NULL;
        xlator_t        *this = NULL;
        server_state_t  *state = NULL;
        char             resolve_vars[256];
        char             resolve2_vars[256];
        char             loc_vars[256];
        char             loc2_vars[256];
        char             other_vars[512];
        char             caller[512];
        char            *op = "UNKNOWN";

        this = frame->this;
        conf = this->private;

        if (!conf || !conf->trace)
                return;

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
                op = gf_fop_list[frame->root->op];
                break;
        case GF_OP_TYPE_MGMT:
                op = gf_mgmt_list[frame->root->op];
                break;
        default:
                op = "";
                break;
        }

        gf_log (this->name, GF_LOG_NORMAL,
                "%s%s%s%s%s%s%s",
                op, caller,
                resolve_vars, loc_vars, resolve2_vars, loc2_vars, other_vars);
}

int
serialize_rsp_direntp (gf_dirent_t *entries, gfs3_readdirp_rsp *rsp)
{
	gf_dirent_t         *entry = NULL;
        gfs3_dirplist       *trav = NULL;
        gfs3_dirplist       *prev = NULL;
	int                  ret = -1;

	list_for_each_entry (entry, &entries->list, list) {
                trav = GF_CALLOC (1, sizeof (*trav), gf_server_mt_dirent_rsp_t);
                if (!trav)
                        goto out;

                trav->d_ino  = entry->d_ino;
                trav->d_off  = entry->d_off;
                trav->d_len  = entry->d_len;
                trav->d_type = entry->d_type;
                //trav->name   = memdup (entry->d_name, entry->d_len + 1);
                trav->name   = entry->d_name;

                gf_stat_from_iatt (&trav->stat, &entry->d_stat);

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
serialize_rsp_dirent (gf_dirent_t *entries, gfs3_readdir_rsp *rsp)
{
	gf_dirent_t         *entry = NULL;
        gfs3_dirlist        *trav = NULL;
        gfs3_dirlist        *prev = NULL;
	int                  ret = -1;

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
        gfs3_dirlist *prev = NULL;
        gfs3_dirlist *trav = NULL;

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
                list_for_each_entry (xprt, &conf->xprt_list, list) {
                        gf_log ("mount-point-list", GF_LOG_INFO,
                                "%s", xprt->peerinfo.identifier);
                }
        }

        /* Add more options/keys here */

        return 0;
}

int
gf_server_check_setxattr_cmd (call_frame_t *frame, dict_t *dict)
{

        data_pair_t      *pair = NULL;
        server_conf_t    *conf = NULL;
        rpc_transport_t  *xprt = NULL;
        uint64_t          total_read = 0;
        uint64_t          total_write = 0;

        conf = frame->this->private;
        if (!conf)
                return 0;

        for (pair = dict->members_list; pair; pair = pair->next) {
                /* this exact key is used in 'io-stats' too.
                 * But this is better place for this information dump.
                 */
                if (fnmatch ("*io*stat*dump", pair->key, 0) == 0) {
                        list_for_each_entry (xprt, &conf->xprt_list, list) {
                                total_read  += xprt->total_bytes_read;
                                total_write += xprt->total_bytes_write;
                        }
                        gf_log ("stats", GF_LOG_INFO,
                                "total-read %"PRIu64", total-write %"PRIu64,
                                total_read, total_write);
                }
        }

        return 0;
}
