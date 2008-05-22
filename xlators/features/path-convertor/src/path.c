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
  char *tmp_name = NULL;
  int32_t path_len = strlen (path);
  int32_t name_len = strlen (name) - GF_FILE_CONTENT_STRING_LEN;
  int32_t total_len = path_len + name_len;

  if (priv->end_off && (total_len > priv->end_off)) {
    int32_t i,j = priv->start_off;
    char priv_path[4096] = {0,};
    tmp_name = calloc (1, (total_len + GF_FILE_CONTENT_STRING_LEN));
    ERR_ABORT (tmp_name);

    /* Get the complete path for the file first */
    strcpy (tmp_name, path);
    strcat (tmp_name, name + GF_FILE_CONTENT_STRING_LEN);

    //priv->path = calloc (1, name_len + path_len);
    for (i = priv->start_off; i < priv->end_off; i++) {
      if (tmp_name[i] == '/')
	continue;
      priv_path[j++] = tmp_name[i];
    }
    memcpy ((priv_path + j), 
	    (tmp_name + priv->end_off), 
	    (total_len - priv->end_off));
    priv->path[(total_len - (priv->end_off - j))] = '\0';

    strcpy (tmp_name, GF_FILE_CONTENT_STRING);
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
  int32_t path_len = strlen (path);
#if 0
  /* */
  if (priv->this_len) {
    if (path_len < priv->this_len)
      return NULL;
    if (path[priv->this_len] == '/')
      return (char *)path + priv->this_len;
    return NULL;
  }

  if (priv->preg) {
    int32_t ret = 0;
    regmatch_t match;
    ret = regexec (priv->preg, path, 0, &match, REG_NOTBOL);
    if (!ret) {
      char *priv_path = calloc (1, path_len);
      ERR_ABORT (priv_path);

      strncpy (priv_path, (char *)path, match.rm_so);
      strncat (priv_path, priv->that, strlen (priv->that));
      strncat (priv_path, ((char *)path+match.rm_eo), (path_len - match.rm_eo));
      return priv_path;
    }
    return (char *)path;
  }
#endif

  if (priv->end_off && (path_len > priv->start_off)) {
    char *priv_path = calloc (1, path_len);
    ERR_ABORT (priv_path);

    if (priv->start_off && (path_len > priv->start_off))
      memcpy (priv_path, path, priv->start_off);
    if (path_len > priv->end_off) {
      int32_t i,j = priv->start_off;
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
path_stat_cbk (call_frame_t *frame,
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
path_chown_cbk (call_frame_t *frame,
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
path_chmod_cbk (call_frame_t *frame,
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
path_unlink_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t 
path_rename_cbk (call_frame_t *frame,
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
path_rmdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t 
path_truncate_cbk (call_frame_t *frame,
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
path_utimens_cbk (call_frame_t *frame,
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
path_setxattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t 
path_getxattr_cbk (call_frame_t *frame,
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
path_removexattr_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t 
path_closedir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t 
path_access_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

int32_t 
path_setdents_cbk (call_frame_t *frame,
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
  loc_t tmp_loc = {0,};

  

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;

  STACK_WIND (frame, path_lookup_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->lookup, 
	      &tmp_loc, need_xattr);

  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);

  return 0;
}

int32_t 
path_stat (call_frame_t *frame,
	   xlator_t *this,
	   loc_t *loc)
{
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_stat_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->stat, 
	      loc);
  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);
  
  return 0;
}

int32_t 
path_readlink (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       size_t size)
{
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_readlink_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->readlink, 
	      &tmp_loc, 
	      size);
  
  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);

  return 0;
}

int32_t 
path_mknod (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    mode_t mode,
	    dev_t dev)
{
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_mknod_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->mknod, 
	      &tmp_loc, 
	      mode, 
	      dev);
  
  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);

  return 0;
}

int32_t 
path_mkdir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode)
{
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_mkdir_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->mkdir, 
	      &tmp_loc, 
	      mode);
  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);

  return 0;
}

int32_t 
path_unlink (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_unlink_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->unlink, 
	      loc);
  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);

  return 0;
}

int32_t 
path_rmdir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_rmdir_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->rmdir, 
	      loc);
  
  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);

  return 0;
}

int32_t 
path_symlink (call_frame_t *frame,
	       xlator_t *this,
	       const char *linkpath,
	       loc_t *loc)
{
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_symlink_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->symlink, 
	      linkpath,
	      loc);
  
  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);

  return 0;
}

int32_t 
path_rename (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *oldloc,
	      loc_t *newloc)
{  
  loc_t tmp_oldloc = {0,};
  loc_t tmp_newloc = {0,};

  if (!(tmp_oldloc.path = path_this_to_that (this, oldloc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL);
    return 0;
  }
  if (!(tmp_newloc.path = path_this_to_that (this, newloc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL);
    return 0;
  }
  tmp_oldloc.inode = oldloc->inode;
  tmp_newloc.inode = newloc->inode;
  STACK_WIND (frame, 
	      path_rename_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->rename, 
	      &tmp_oldloc,
	      &tmp_newloc);
  
  if (tmp_oldloc.path != oldloc->path)
    FREE (tmp_oldloc.path);

  if (tmp_newloc.path != newloc->path)
    FREE (tmp_newloc.path);

  return 0;
}

int32_t 
path_link (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    const char *newpath)
{
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_link_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->link, 
	      &tmp_loc, 
	      newpath);
  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);

  return 0;
}

int32_t 
path_chmod (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode)
{
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_chmod_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->chmod, 
	      &tmp_loc, 
	      mode);
  
  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);

  return 0;
}

int32_t 
path_chown (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     uid_t uid,
	     gid_t gid)
{
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_chown_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->chown, 
	      &tmp_loc, 
	      uid,
	      gid);

  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);

  return 0;
}

int32_t 
path_truncate (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		off_t offset)
{
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_truncate_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->truncate, 
	      &tmp_loc, 
	      offset);
  
  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);

  return 0;
}

int32_t 
path_utimens (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       struct timespec tv[2])
{
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_utimens_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->utimens, 
	      &tmp_loc, 
	      tv);

  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);

  return 0;
}

int32_t 
path_open (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t flags,
	    fd_t *fd)
{
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_open_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->open, 
	      &tmp_loc, 
	      flags,
	      fd);
  //if (tmp_loc.path != loc->path)
  //  FREE (tmp_loc.path);

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
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL, NULL, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_create_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->create, 
	      &tmp_loc, 
	      flags,
	      mode,
	      fd);
  //if (tmp_loc.path != loc->path)
  //  FREE (tmp_loc.path);

  return 0;
}

int32_t 
path_setxattr (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       dict_t *dict,
	       int32_t flags)
{
  loc_t tmp_loc = {0,};
  char *tmp_name = NULL;
  data_pair_t *trav = dict->members_list;

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  if (GF_FILE_CONTENT_REQUEST(trav->key)) {
    tmp_name = name_this_to_that (this, loc->path, trav->key);
    if (tmp_name != trav->key) {
      trav->key = tmp_name;
    } else {
      tmp_name = NULL;
    }
  }
  STACK_WIND (frame, 
	      path_setxattr_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->setxattr, 
	      &tmp_loc, 
	      dict,
	      flags);
  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);

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
  loc_t tmp_loc = {0,};
  char *tmp_name = (char *)name;
  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  if (GF_FILE_CONTENT_REQUEST(name)) {
    tmp_name = name_this_to_that (this, loc->path, name);
  }
  STACK_WIND (frame, 
	      path_getxattr_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->getxattr,
	      &tmp_loc, 
	      tmp_name);
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
  loc_t tmp_loc = {0,};
  char *tmp_name = (char *)name;
  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  if (GF_FILE_CONTENT_REQUEST(name)) {
    tmp_name = name_this_to_that (this, loc->path, name);
  }
  STACK_WIND (frame, 
	      path_removexattr_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->removexattr, 
	      &tmp_loc, 
	      tmp_name);

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
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_opendir_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->opendir, 
	      &tmp_loc, 
	      fd);
  //if (tmp_loc.path != loc->path)
  //  FREE (tmp_loc.path);

  return 0;
}

#if 0
int32_t 
path_getdents (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       size_t size,
	       off_t offset,
	       int32_t flag)
{
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_getdents_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->getdents, 
	      fd,
	      size, 
	      offset, 
	      flag);
  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);

  return 0;
}


int32_t 
path_readdir (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       size_t size,
	       off_t offset)
{
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_readdir_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->readdir,
	      fd,
	      size, 
	      offset);

  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);

  return 0;
}

int32_t 
path_closedir (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd)
{  
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_closedir_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->closedir, 
	      fd);
  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);

  return 0;
}
#endif /* if 0 */

int32_t 
path_access (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t mask)
{
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_access_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->access, 
	      &tmp_loc, 
	      mask);
  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);

  return 0;
}

#if 0
int32_t 
path_setdents (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		int32_t flags,
		dir_entry_t *entries,
		int32_t count)
{
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame, 
	      path_setdents_cbk, 
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->fops->setdents, 
	      fd,
	      flags,
	      entries,
	      count);
  if (tmp_loc.path != loc->path)
    FREE (tmp_loc.path);

  return 0;
}
#endif /* if 0 */

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
	    "path translator does not support more than one sub-volume");
    return -1;
  }
  
  priv = calloc (1, sizeof (*priv));
  ERR_ABORT (priv);
  if (dict_get (options, "start-offset")) {
    priv->start_off = data_to_int32 (dict_get (options, "start-offset"));
  }
  if (dict_get (options, "end-offset")) {
    priv->end_off = data_to_int32 (dict_get (options, "end-offset"));
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
      priv->that = data_to_str (dict_get (options, "replace-with"));
    } else {
      priv->that = "";
    }
  }

  /* Set this translator's inode table pointer to child node's pointer. */
  this->itable = FIRST_CHILD (this)->itable;
  this->private = priv;
  return 0;
}

void
fini (xlator_t *this)
{
  /* Free up the dictionary options */
  dict_destroy (FIRST_CHILD(this)->options);

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
  //  .readdir     = path_readdir, 
  //.closedir    = path_closedir,
  .access      = path_access,
  .create      = path_create,
  .lookup      = path_lookup,
  //.setdents    = path_setdents,
  //.getdents    = path_getdents,
};

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
  loc_t tmp_loc = {0,};

  if (!(tmp_loc.path = path_this_to_that (this, loc->path))) {
    STACK_UNWIND (frame, -1, ENOENT, NULL, NULL);
    return 0;
  }
  tmp_loc.inode = loc->inode;
  STACK_WIND (frame,
	      path_checksum_cbk,
	      FIRST_CHILD(this), 
	      FIRST_CHILD(this)->mops->checksum, 
	      &tmp_loc, 
	      flag);

  return 0;
}


static int32_t
path_lock_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}


int32_t
path_lock (call_frame_t *frame,
	   xlator_t *this,
	   const char *path)
{
  char *tmp_path = path_this_to_that (this, path);
  if (!tmp_path) {
    STACK_UNWIND (frame, -1, ENOENT);
    return 0;
  }
  STACK_WIND (frame,
	      path_lock_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->mops->lock,
	      tmp_path);
  return 0;
}


static int32_t
path_unlock_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}


int32_t
path_unlock (call_frame_t *frame,
	     xlator_t *this,
	     const char *path)
{
  char *tmp_path = path_this_to_that (this, path);
  if (!tmp_path) {
    STACK_UNWIND (frame, -1, ENOENT);
    return 0;
  }
  STACK_WIND (frame,
	      path_unlock_cbk,
	      FIRST_CHILD(this),
	      FIRST_CHILD(this)->mops->unlock,
	      tmp_path);
  return 0;
}


struct xlator_mops mops = {
  .lock = path_lock,
  .unlock = path_unlock,
  .checksum = path_checksum,
};
