/*
  Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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


/* server_loc_fill - derive a loc_t for a given inode number
 *
 * NOTE: make sure that @loc is empty, because any pointers it holds with reference will
 *       be leaked after returning from here.
 */
int
server_loc_fill (loc_t *loc, server_state_t *state,
  		 ino_t ino, ino_t par,
  		 const char *name, const char *path)
{
  	inode_t *inode = NULL;
  	inode_t *parent = NULL;
  	int32_t  ret = -1;
	char    *dentry_path = NULL;


  	GF_VALIDATE_OR_GOTO ("server", loc, out);
  	GF_VALIDATE_OR_GOTO ("server", state, out);
  	GF_VALIDATE_OR_GOTO ("server", path, out);

  	/* anything beyond this point is success */
  	ret = 0;
	loc->ino = ino;
  	inode = loc->inode;
  	if (inode == NULL) {
  		if (ino)
  			inode = inode_search (state->itable, ino, NULL);

  		if ((inode == NULL) &&
  		    (par && name))
  			inode = inode_search (state->itable, par, name);

  		loc->inode = inode;
  		if (inode)
  			loc->ino = inode->ino;
  	}

  	parent = loc->parent;
	if (parent == NULL) {
		if (inode)
			parent = inode_parent (inode, par, name);
		else
			parent = inode_search (state->itable, par, NULL);
		loc->parent = parent;
	}

	if (name && parent) {
		ret = inode_path (parent, name, &dentry_path);
		if (ret < 0) {
			gf_log (state->bound_xl->name, GF_LOG_DEBUG,
				"failed to build path for %"PRId64"/%s: %s",
				parent->ino, name, strerror (-ret));
		}
	} else if (inode) {
		ret = inode_path (inode, NULL, &dentry_path);
		if (ret < 0) {
			gf_log (state->bound_xl->name, GF_LOG_DEBUG,
				"failed to build path for %"PRId64": %s",
				inode->ino, strerror (-ret));

			inode_unref (loc->inode);
			loc->inode = NULL;
		}
	}

	if (dentry_path) {
		if (strcmp (dentry_path, path)) {
			gf_log (state->bound_xl->name, GF_LOG_DEBUG,
				"paths differ for inode(%"PRId64"): "
				"client path = %s. dentry path = %s",
				ino, path, dentry_path);
		}

		loc->path = dentry_path;
		loc->name = strrchr (loc->path, '/');
		if (loc->name)
			loc->name++;
	} else {
		loc->path = strdup (path);
		loc->name = strrchr (loc->path, '/');
		if (loc->name)
			loc->name++;
	}

out:
  	return ret;
}

/*
 * stat_to_str - convert struct stat to a ASCII string
 * @stbuf: struct stat pointer
 *
 * not for external reference
 */
char *
stat_to_str (struct stat *stbuf)
{
	char *tmp_buf = NULL;

	uint64_t dev = stbuf->st_dev;
	uint64_t ino = stbuf->st_ino;
	uint32_t mode = stbuf->st_mode;
	uint32_t nlink = stbuf->st_nlink;
	uint32_t uid = stbuf->st_uid;
	uint32_t gid = stbuf->st_gid;
	uint64_t rdev = stbuf->st_rdev;
	uint64_t size = stbuf->st_size;
	uint32_t blksize = stbuf->st_blksize;
	uint64_t blocks = stbuf->st_blocks;
	uint32_t atime = stbuf->st_atime;
	uint32_t mtime = stbuf->st_mtime;
	uint32_t ctime = stbuf->st_ctime;

	uint32_t atime_nsec = ST_ATIM_NSEC(stbuf);
	uint32_t mtime_nsec = ST_MTIM_NSEC(stbuf);
	uint32_t ctime_nsec = ST_CTIM_NSEC(stbuf);


	asprintf (&tmp_buf,
		  GF_STAT_PRINT_FMT_STR,
		  dev,
		  ino,
		  mode,
		  nlink,
		  uid,
		  gid,
		  rdev,
		  size,
		  blksize,
		  blocks,
		  atime,
		  atime_nsec,
		  mtime,
		  mtime_nsec,
		  ctime,
		  ctime_nsec);

	return tmp_buf;
}


void
server_loc_wipe (loc_t *loc)
{
	if (loc->parent)
		inode_unref (loc->parent);
	if (loc->inode)
		inode_unref (loc->inode);
	if (loc->path)
		free ((char *)loc->path);
}

void
free_state (server_state_t *state)
{
	transport_t *trans = NULL;	

	trans    = state->trans;

	if (state->fd)
		fd_unref (state->fd);

	transport_unref (trans);
	
	if (state->xattr_req)
		dict_unref (state->xattr_req);

	FREE (state);
}


call_frame_t *
server_copy_frame (call_frame_t *frame)
{
	call_frame_t *new_frame = NULL;
	server_state_t *state = NULL, *new_state = NULL;

	state = frame->root->state;

	new_frame = copy_frame (frame);

	new_state = CALLOC (1, sizeof (server_state_t));

	new_frame->root->op    = frame->root->op;
	new_frame->root->type  = frame->root->type;
	new_frame->root->trans = state->trans;
	new_frame->root->state = new_state;

	new_state->bound_xl = state->bound_xl;
	new_state->trans    = transport_ref (state->trans);
	new_state->itable   = state->itable;

	return new_frame;
}

int32_t
gf_add_locker (struct _lock_table *table,
	       loc_t *loc,
	       fd_t *fd,
	       pid_t pid)
{
	int32_t ret = -1;
	struct _locker *new = NULL;
	uint8_t dir = 0;

	new = CALLOC (1, sizeof (struct _locker));
	if (new == NULL) {
		gf_log ("server", GF_LOG_ERROR,
			"failed to allocate memory for \'struct _locker\'");
		goto out;
	}
	INIT_LIST_HEAD (&new->lockers);

	if (fd == NULL) {
		loc_copy (&new->loc, loc);
		dir = S_ISDIR (new->loc.inode->st_mode);
	} else {
		new->fd = fd_ref (fd);
		dir = S_ISDIR (fd->inode->st_mode);
	}

	new->pid = pid;

	LOCK (&table->lock);
	{
		if (dir)
			list_add_tail (&new->lockers, &table->dir_lockers);
		else
			list_add_tail (&new->lockers, &table->file_lockers);
	}
	UNLOCK (&table->lock);
out:
	return ret;
}

int32_t
gf_del_locker (struct _lock_table *table,
	       loc_t *loc,
	       fd_t *fd,
	       pid_t pid)
{
	struct _locker *locker = NULL, *tmp = NULL;
	int32_t ret = 0;
	uint8_t dir = 0;
	struct list_head *head = NULL;
	struct list_head del;

	INIT_LIST_HEAD (&del);

	if (fd) {
		dir = S_ISDIR (fd->inode->st_mode);
	} else {
		dir = S_ISDIR (loc->inode->st_mode);
	}

	LOCK (&table->lock);
	{
		if (dir) {
			head = &table->dir_lockers;
		} else {
			head = &table->file_lockers;
		}

		list_for_each_entry_safe (locker, tmp, head, lockers) {
			if (locker->fd &&
			    fd &&
			    (locker->fd == fd) && (locker->pid == pid)) {
				list_move_tail (&locker->lockers, &del);
			} else if (locker->loc.inode && 
				   loc &&
				   (locker->loc.inode == loc->inode) &&
				   (locker->pid == pid)) {
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

		free (locker);
	}

	return ret;
}

int32_t
gf_direntry_to_bin (dir_entry_t *head,
		    char **bufferp)
{
	dir_entry_t *trav = NULL;
	uint32_t len = 0;
	uint32_t this_len = 0;
	char *buffer = NULL;
	size_t buflen = -1;
	char *ptr = NULL;
	char *tmp_buf = NULL;

	trav = head->next;
	while (trav) {
		len += strlen (trav->name);
		len += 1;
		len += strlen (trav->link);
		len += 1; /* for '\n' */
		len += 256; // max possible for statbuf;
		trav = trav->next;
	}

	buffer = CALLOC (1, len);
	if (buffer == NULL) {
		gf_log ("server", GF_LOG_ERROR,
			"failed to allocate memory for buffer");
		goto out;
	}

	ptr = buffer;
	trav = head->next;
	while (trav) {
		tmp_buf = stat_to_str (&trav->buf);
		/* tmp_buf will have \n before \0 */

		this_len = sprintf (ptr, "%s/%s%s\n",
				    trav->name, tmp_buf,
				    trav->link);

		FREE (tmp_buf);
		trav = trav->next;
		ptr += this_len;
	}
	if (bufferp)
		*bufferp = buffer;
	buflen = strlen (buffer);
	
out:
	return buflen;
}


static struct _lock_table *
gf_lock_table_new (void)
{
	struct _lock_table *new = NULL;

	new = CALLOC (1, sizeof (struct _lock_table));
	if (new == NULL) {
		gf_log ("server-protocol", GF_LOG_CRITICAL,
			"failed to allocate memory for new lock table");
		goto out;
	}
	INIT_LIST_HEAD (&new->dir_lockers);
	INIT_LIST_HEAD (&new->file_lockers);
	LOCK_INIT (&new->lock);
out:
	return new;
}


int
server_connection_destroy (xlator_t *this, server_connection_t *conn)
{

	call_frame_t      *frame = NULL, *tmp_frame = NULL;
	xlator_t          *bound_xl = NULL;
	int32_t            ret = -1;
	server_state_t    *state = NULL;
	struct list_head   file_lockers;
	struct list_head   dir_lockers;
	struct _lock_table *ltable = NULL;
	struct _locker     *locker = NULL, *tmp = NULL;
	struct flock        flock = {0,};


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

		INIT_LIST_HEAD (&file_lockers);
		INIT_LIST_HEAD (&dir_lockers);

		LOCK (&ltable->lock);
		{
			list_splice_init (&ltable->file_lockers, 
					  &file_lockers);

			list_splice_init (&ltable->dir_lockers, &dir_lockers);
		}
		UNLOCK (&ltable->lock);
		free (ltable);

		flock.l_type  = F_UNLCK;
		flock.l_start = 0;
		flock.l_len   = 0;
		list_for_each_entry_safe (locker, 
					  tmp, &file_lockers, lockers) {
			tmp_frame = copy_frame (frame);
			/* 
			   pid = 0 is a special case that tells posix-locks
			   to release all locks from this transport
			*/
			tmp_frame->root->pid = 0;
			tmp_frame->root->trans = conn;

			if (locker->fd) {
				STACK_WIND (tmp_frame, server_nop_cbk,
					    bound_xl,
					    bound_xl->fops->finodelk,
					    locker->fd, F_SETLK, &flock);
				fd_unref (locker->fd);
			} else {
				STACK_WIND (tmp_frame, server_nop_cbk,
					    bound_xl,
					    bound_xl->fops->inodelk,
					    &(locker->loc), F_SETLK, &flock);
				loc_wipe (&locker->loc);
			}

			list_del_init (&locker->lockers);
			free (locker);
		}

		tmp = NULL;
		locker = NULL;
		list_for_each_entry_safe (locker, tmp, &dir_lockers, lockers) {
			tmp_frame = copy_frame (frame);

			tmp_frame->root->pid = 0;
			tmp_frame->root->trans = conn;

			if (locker->fd) {
				STACK_WIND (tmp_frame, server_nop_cbk,
					    bound_xl,
					    bound_xl->fops->fentrylk,
					    locker->fd, NULL, 
					    ENTRYLK_UNLOCK, ENTRYLK_WRLCK);
				fd_unref (locker->fd);
			} else {
				STACK_WIND (tmp_frame, server_nop_cbk,
					    bound_xl,
					    bound_xl->fops->entrylk,
					    &(locker->loc), NULL, 
					    ENTRYLK_UNLOCK, ENTRYLK_WRLCK);
				loc_wipe (&locker->loc);
			}

			list_del_init (&locker->lockers);
			free (locker);
		}

		state = CALL_STATE (frame);
		if (state)
			free (state);
		STACK_DESTROY (frame->root);

		pthread_mutex_lock (&(conn->lock));
		{
			if (conn->fdtable) {
				gf_fd_fdtable_destroy (conn->fdtable);
				conn->fdtable = NULL;
			}
		}
		pthread_mutex_unlock (&conn->lock);

	}

	gf_log (this->name, GF_LOG_INFO, "destroyed connection of %s",
		conn->id);

	FREE (conn->id);
	FREE (conn);

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
			conn = (void *) CALLOC (1, sizeof (*conn));

			conn->id = strdup (id);
			conn->fdtable = gf_fd_fdtable_alloc ();
			conn->ltable  = gf_lock_table_new ();

			pthread_mutex_init (&conn->lock, NULL);

			list_add (&conn->list, &conf->conns);
		}

		conn->ref++;
	}
	pthread_mutex_unlock (&conf->mutex);

	return conn;
}


void
server_connection_put (xlator_t *this, server_connection_t *conn)
{
	server_conf_t       *conf = NULL;
	server_connection_t *todel = NULL;

	conf = this->private;

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

	return;
}
