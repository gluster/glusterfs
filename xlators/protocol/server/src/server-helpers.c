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


/* server_loc_fill - derive a loc_t for a given inode number
 *
 * NOTE: make sure that @loc is empty, because any pointers it holds with reference will
 *       be leaked after returning from here.
 */
int32_t
server_loc_fill (loc_t *loc,
  		 server_state_t *state,
  		 ino_t ino,
  		 ino_t par,
  		 const char *name,
  		 const char *path)
{
  	inode_t *inode = NULL;
  	inode_t *parent = NULL;
  	int32_t ret = -1;

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

  	if (path) {
  		loc->path = strdup (path);
  		loc->name = strrchr (loc->path, '/');
  		if (loc->name)
  			(loc->name)++;
  	}
	
  	{
  		char *tmp_path = NULL;
  		size_t n = 0;

  		if (name && parent) {
  			n = inode_path (parent, name, NULL, 0) + 1;
  			tmp_path = calloc (1, n);
  			inode_path (parent, name, tmp_path, n);
  		} else if (inode){
  			n = inode_path (inode, NULL, NULL, 0) + 1;
  			tmp_path = calloc (1, n);
  			inode_path (inode, NULL, tmp_path, n);
  		}

  		if (tmp_path && (strncmp (tmp_path, path, n))) {
  			gf_log (state->bound_xl->name,
  				GF_LOG_ERROR,
  				"paths differ for inode(%"PRId64"): "
				"path (%s) from dentry tree is %s",
  				ino, path, tmp_path);
  		}
		
		if (tmp_path)
			free (tmp_path);
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

#ifdef HAVE_TV_NSEC
	uint32_t atime_nsec = stbuf->st_atim.tv_nsec;
	uint32_t mtime_nsec = stbuf->st_mtim.tv_nsec;
	uint32_t ctime_nsec = stbuf->st_ctim.tv_nsec;
#else
	uint32_t atime_nsec = 0;
	uint32_t mtime_nsec = 0;
	uint32_t ctime_nsec = 0;
#endif

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

	FREE (state);
}


call_frame_t *
server_copy_frame (call_frame_t *frame)
{
	call_frame_t *new_frame = NULL;
	server_state_t *state = NULL, *new_state = NULL;

	state = frame->root->state;

	new_frame = copy_frame (frame);

	new_state = calloc (1, sizeof (server_state_t));

	new_frame->root->frames.op   = frame->op;
	new_frame->root->frames.type = frame->type;
	new_frame->root->trans       = state->trans;
	new_frame->root->state       = new_state;

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

	new = calloc (1, sizeof (struct _locker));
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

	buffer = calloc (1, len);
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
