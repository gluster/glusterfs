/*
  Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

/* TODO: add gf_log to all the cases returning errors */

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

/**
 * xlators/features/path-translator:
 *    This translator converts the path it gets into user specified targets.
 */

#include <sys/types.h>
#include <regex.h>
#include <time.h>
#include <errno.h>
#include "glusterfs.h"
#include "xlator.h"

typedef struct path_private
{
	int32_t this_len;
	int32_t start_off;
	int32_t end_off;
	char *this;
	char *that;
	char *path;
	regex_t *preg;
} path_private_t;

static char *
name_this_to_that (xlator_t *xl, const char *path, const char *name)
{
	path_private_t *priv = xl->private;
	char priv_path[ZR_PATH_MAX] = {0,};
	char *tmp_name = NULL;
	int32_t path_len = strlen (path);
	int32_t name_len = strlen (name) - ZR_FILE_CONTENT_STRLEN;
	int32_t total_len = path_len + name_len;
	int32_t i = 0, j = 0;

	if (path_len >= priv->end_off)
		return (char *)name;

	if (priv->end_off && (total_len > priv->end_off)) {
		j = priv->start_off;
		tmp_name = calloc (1, (total_len + ZR_FILE_CONTENT_STRLEN));
		ERR_ABORT (tmp_name);

		/* Get the complete path for the file first */
		strcpy (tmp_name, path);
		strcat (tmp_name, name + ZR_FILE_CONTENT_STRLEN);

		strncpy (priv_path, tmp_name, priv->start_off);
		for (i = priv->start_off; i < priv->end_off; i++) {
			if (tmp_name[i] == '/')
				continue;
			priv_path[j++] = tmp_name[i];
		}
		memcpy ((priv_path + j), 
			(tmp_name + priv->end_off), 
			(total_len - priv->end_off));
		priv_path[(total_len - (priv->end_off - j))] = '\0';

		strcpy (tmp_name, ZR_FILE_CONTENT_STR);
		strcat (tmp_name, priv_path);

		return tmp_name;
	}

	return (char *)name;
}

/* This function should return 
 *  NULL - 
 *  converted path - if path match
 *  same path - if it doesn't match
 */
static char *
path_this_to_that (xlator_t *xl, const char *path)
{
	path_private_t *priv = xl->private;
	char *priv_path = NULL;
	int32_t path_len = strlen (path);
	int32_t i = 0, j = 0;

	if (priv->end_off && (path_len > priv->start_off)) {
		priv_path = calloc (1, path_len);
		ERR_ABORT (priv_path);

		if (priv->start_off && (path_len > priv->start_off))
			memcpy (priv_path, path, priv->start_off);
		if (path_len > priv->end_off) {
			j = priv->start_off;
			for (i = priv->start_off; i < priv->end_off; i++) {
				if (path[i] == '/')
					continue;
				priv_path[j++] = path[i];
			}
			memcpy ((priv_path + j), 
				(path + priv->end_off), 
				(path_len - priv->end_off));
			priv_path[(path_len - (priv->end_off - j))] = '\0';
		}
		return priv_path;
	}
	return (char *)path;
}

int32_t 
path_create_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 fd_t *fd,
		 inode_t *inode,
		 struct stat *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf);
	return 0;
}

int32_t 
path_open_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       fd_t *fd)
{
	STACK_UNWIND (frame, op_ret, op_errno, fd);
	return 0;
}

int32_t 
path_getdents_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   dir_entry_t *entries,
		   int32_t count)
{
	STACK_UNWIND (frame, op_ret, op_errno, entries, count);
	return 0;
}

int32_t 
path_readdir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  gf_dirent_t *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}


int32_t 
path_readlink_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   const char *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t 
path_lookup_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *buf,
		 dict_t *xattr)
{
	STACK_UNWIND (frame, op_ret, op_errno, inode, buf, xattr);
	return 0;
}


int32_t 
path_symlink_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  inode_t *inode,
		  struct stat *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
	return 0;
}

int32_t 
path_mknod_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		inode_t *inode,
		struct stat *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
	return 0;
}
  

int32_t 
path_mkdir_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		inode_t *inode,
		struct stat *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
	return 0;
}
  
int32_t 
path_link_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       inode_t *inode,
	       struct stat *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
	return 0;
}

int32_t 
path_opendir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  fd_t *fd)
{
	STACK_UNWIND (frame, op_ret, op_errno, fd);
	return 0;
}


int32_t 
path_common_buf_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t 
path_common_dict_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      dict_t *dict)
{
	STACK_UNWIND (frame, op_ret, op_errno, dict);
	return 0;
}

int32_t 
path_common_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

/* */
int32_t 
path_lookup (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     int32_t need_xattr)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame, path_lookup_cbk,
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->lookup, 
		    loc, need_xattr);

	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}

int32_t 
path_stat (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame, 
		    path_common_buf_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->stat, 
		    loc);
  
	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}

int32_t 
path_readlink (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       size_t size)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame, 
		    path_readlink_cbk,
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->readlink, 
		    loc, 
		    size);
  
	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}

int32_t 
path_mknod (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    mode_t mode,
	    dev_t dev)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame, 
		    path_mknod_cbk,
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->mknod, 
		    loc, 
		    mode, 
		    dev);

	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}

int32_t 
path_mkdir (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    mode_t mode)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame, 
		    path_mkdir_cbk,
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->mkdir, 
		    loc, 
		    mode);

	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}

int32_t 
path_unlink (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame, 
		    path_common_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->unlink, 
		    loc);

	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}

int32_t 
path_rmdir (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame, 
		    path_common_cbk,
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->rmdir, 
		    loc);
  
	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}

int32_t 
path_symlink (call_frame_t *frame,
	      xlator_t *this,
	      const char *linkpath,
	      loc_t *loc)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame, 
		    path_symlink_cbk,
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->symlink, 
		    linkpath,
		    loc);
  
	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}

int32_t 
path_rename (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *oldloc,
	     loc_t *newloc)
{  
	char *oldloc_path = (char *)oldloc->path;
	char *tmp_oldloc_path = NULL;

	char *newloc_path = (char *)newloc->path;
	char *tmp_newloc_path = NULL;
	
	if (!(tmp_oldloc_path = path_this_to_that (this, oldloc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	oldloc->path = tmp_oldloc_path;

	if (!(tmp_newloc_path = path_this_to_that (this, newloc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	newloc->path = tmp_newloc_path;

	STACK_WIND (frame, 
		    path_common_buf_cbk,
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->rename, 
		    oldloc,
		    newloc);
  
	oldloc->path = oldloc_path;	
	if (tmp_oldloc_path != oldloc_path)
		FREE (tmp_oldloc_path);

	newloc->path = newloc_path;	
	if (tmp_newloc_path != newloc_path)
		FREE (tmp_newloc_path);

	return 0;
}

int32_t 
path_link (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *oldloc,
	   loc_t *newloc)
{
	char *oldloc_path = (char *)oldloc->path;
	char *tmp_oldloc_path = NULL;

	char *newloc_path = (char *)newloc->path;
	char *tmp_newloc_path = NULL;
	
	if (!(tmp_oldloc_path = path_this_to_that (this, oldloc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	oldloc->path = tmp_oldloc_path;

	if (!(tmp_newloc_path = path_this_to_that (this, newloc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	newloc->path = tmp_newloc_path;

	STACK_WIND (frame, 
		    path_link_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->link, 
		    oldloc, 
		    newloc);

	oldloc->path = oldloc_path;	
	if (tmp_oldloc_path != oldloc_path)
		FREE (tmp_oldloc_path);

	newloc->path = newloc_path;	
	if (tmp_newloc_path != newloc_path)
		FREE (tmp_newloc_path);

	return 0;
}

int32_t 
path_chmod (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    mode_t mode)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame, 
		    path_common_buf_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->chmod, 
		    loc, 
		    mode);
  
	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}

int32_t 
path_chown (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    uid_t uid,
	    gid_t gid)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame, 
		    path_common_buf_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->chown, 
		    loc, 
		    uid,
		    gid);

	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}

int32_t 
path_truncate (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       off_t offset)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame, 
		    path_common_buf_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->truncate, 
		    loc, 
		    offset);
  
	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}

int32_t 
path_utimens (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      struct timespec tv[2])
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame, 
		    path_common_buf_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->utimens, 
		    loc, 
		    tv);

	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}

int32_t 
path_open (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc,
	   int32_t flags,
	   fd_t *fd)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame, 
		    path_open_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->open, 
		    loc, 
		    flags,
		    fd);

	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}

int32_t 
path_create (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     int32_t flags,
	     mode_t mode,
	     fd_t *fd)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame, 
		    path_create_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->create, 
		    loc, 
		    flags,
		    mode,
		    fd);

	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}

int32_t 
path_setxattr (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       dict_t *dict,
	       int32_t flags)
{
	char *tmp_name = NULL;
	data_pair_t *trav = dict->members_list;
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	if (ZR_FILE_CONTENT_REQUEST(trav->key)) {
		tmp_name = name_this_to_that (this, loc->path, trav->key);
		if (tmp_name != trav->key) {
			trav->key = tmp_name;
		} else {
			tmp_name = NULL;
		}
	}

	STACK_WIND (frame, 
		    path_common_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->setxattr, 
		    loc, 
		    dict,
		    flags);

	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	if (tmp_name)
		FREE (tmp_name);

	return 0;
}

int32_t 
path_getxattr (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       const char *name)
{
	char *tmp_name = (char *)name;
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	if (ZR_FILE_CONTENT_REQUEST(name)) {
		tmp_name = name_this_to_that (this, loc->path, name);
	}

	STACK_WIND (frame, 
		    path_common_dict_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->getxattr,
		    loc, 
		    tmp_name);

	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	if (tmp_name != name)
		FREE (tmp_name);

	return 0;
}

int32_t 
path_removexattr (call_frame_t *frame,
		  xlator_t *this,
		  loc_t *loc,
		  const char *name)
{
	char *tmp_name = (char *)name;
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	if (ZR_FILE_CONTENT_REQUEST(name)) {
		tmp_name = name_this_to_that (this, loc->path, name);
	}

	STACK_WIND (frame, 
		    path_common_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->removexattr, 
		    loc, 
		    tmp_name);

	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	if (tmp_name != name)
		FREE (tmp_name);

	return 0;
}

int32_t 
path_opendir (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      fd_t *fd)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame, 
		    path_opendir_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->opendir, 
		    loc, 
		    fd);

	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}

int32_t 
path_access (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     int32_t mask)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame, 
		    path_common_cbk, 
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->access, 
		    loc, 
		    mask);

	loc->path = loc_path;
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}

int32_t
path_checksum_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   uint8_t *fchecksum,
		   uint8_t *dchecksum)
{
	STACK_UNWIND (frame, op_ret, op_errno, fchecksum, dchecksum);
	return 0;
}

int32_t
path_checksum (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       int32_t flag)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame,
		    path_checksum_cbk,
		    FIRST_CHILD(this), 
		    FIRST_CHILD(this)->fops->checksum, 
		    loc, 
		    flag);

	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}


int32_t
path_entrylk (call_frame_t *frame, xlator_t *this,
	      loc_t *loc, const char *basename,
	      entrylk_cmd cmd, entrylk_type type)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame, path_common_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->entrylk,
		    loc, basename, cmd, type);

	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}

int32_t
path_inodelk (call_frame_t *frame, xlator_t *this,
		 loc_t *loc, int32_t cmd, struct flock *lock)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame,
		    path_common_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->inodelk,
		    loc, cmd, lock);

	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}


int32_t
path_xattrop (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      gf_xattrop_flags_t flags,
	      dict_t *dict)
{
	char *loc_path = (char *)loc->path;
	char *tmp_path = NULL;
	
	if (!(tmp_path = path_this_to_that (this, loc->path))) {
		STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
		return 0;
	}
	loc->path = tmp_path;

	STACK_WIND (frame,
		    path_common_dict_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->xattrop,
		    loc,
		    flags,
		    dict);

	loc->path = loc_path;	
	if (tmp_path != loc_path)
		FREE (tmp_path);

	return 0;
}


int32_t 
init (xlator_t *this)
{
	dict_t *options = this->options;
	path_private_t *priv = NULL;

	if (!this->children) {
		gf_log (this->name, GF_LOG_ERROR, 
			"path translator requires atleast one subvolume");
		return -1;
	}
    
	if (this->children->next) {
		gf_log (this->name, GF_LOG_ERROR, 
			"path translator does not support more than "
			"one sub-volume");
		return -1;
	}
  
	priv = calloc (1, sizeof (*priv));
	ERR_ABORT (priv);
	if (dict_get (options, "start-offset")) {
		priv->start_off = data_to_int32 (dict_get (options, 
							   "start-offset"));
	}
	if (dict_get (options, "end-offset")) {
		priv->end_off = data_to_int32 (dict_get (options, 
							 "end-offset"));
	}

	if (dict_get (options, "regex")) {
		int32_t ret = 0;
		priv->preg = calloc (1, sizeof (regex_t));
		ERR_ABORT (priv->preg);
		ret = regcomp (priv->preg, 
			       data_to_str (dict_get (options, "regex")), 
			       REG_EXTENDED);
		if (ret) {
			gf_log (this->name, GF_LOG_ERROR, 
				"Failed to compile the 'option regex'");
			FREE (priv);
			return -1;
		}
		if (dict_get (options, "replace-with")) {
			priv->that = data_to_str (dict_get (options, 
							    "replace-with"));
		} else {
			priv->that = "";
		}
	}

	this->private = priv;
	return 0;
}

void
fini (xlator_t *this)
{
	return;
}

struct xlator_fops fops = {
	.stat        = path_stat,
	.readlink    = path_readlink,
	.mknod       = path_mknod,
	.mkdir       = path_mkdir,
	.unlink      = path_unlink,
	.rmdir       = path_rmdir,
	.symlink     = path_symlink,
	.rename      = path_rename,
	.link        = path_link,
	.chmod       = path_chmod,
	.chown       = path_chown,
	.truncate    = path_truncate,
	.utimens     = path_utimens,
	.open        = path_open,
	.setxattr    = path_setxattr,
	.getxattr    = path_getxattr,
	.removexattr = path_removexattr,
	.opendir     = path_opendir,
	.access      = path_access,
	.create      = path_create,
	.lookup      = path_lookup,
	.checksum    = path_checksum,
	.xattrop     = path_xattrop,
	.entrylk     = path_entrylk,
	.inodelk     = path_inodelk,
};


struct xlator_mops mops = {
};


struct xlator_cbks cbks = {
};

struct xlator_options options[] = { 
	{ "start-offset", GF_OPTION_TYPE_INT, 0, 0, 4096 },
	{ "end-offset", GF_OPTION_TYPE_INT, 0, 1, 4096 },
	{ "regex", GF_OPTION_TYPE_STR, 0, },
	{ "replace-with", GF_OPTION_TYPE_STR, 0, },
	{ NULL, 0, },
};
