/*
   Copyright (c) 2006, 2007 Z RESEARCH, Inc. <http://www.zresearch.com>
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


#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"

#include <libgen.h>

struct trash_struct {
  inode_t *inode;
  char origpath[4096];
  char newpath[4096];
  char oldpath[4096]; // used only in case of rename
};
typedef struct trash_struct trash_local_t;

struct trash_priv {
  char trash_dir[4096];
};
typedef struct trash_priv trash_private_t;

/**
 * trash_common_unwind_cbk - 
 */
static int32_t 
trash_common_unwind_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

/**
 * trash_common_unwind_buf_cbk - 
 */
static int32_t 
trash_common_unwind_buf_cbk (call_frame_t *frame,
			     void *cookie,
			     xlator_t *this,
			     int32_t op_ret,
			     int32_t op_errno,
			     struct stat *buf)
{
  STACK_UNWIND (frame, op_ret, op_errno, buf);
  return 0;
}

/**
 * trash_unlink_rename_cbk - 
 */
static int32_t 
trash_unlink_rename_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct stat *buf)
{
  trash_local_t *local = frame->local;
  if (op_ret == -1 && op_errno == ENOENT) {
    /* check for the errno, if its ENOENT create directory and call 
     * rename later 
     */
  } else if (op_ret == -1 && op_errno == ENOTDIR) {
    gf_log (this->name, 
	    GF_LOG_WARNING, 
	    "Target exists as a directory, cannot keep the copy, deleting");
    loc_t tmp_loc = {
      .inode = local->inode,
      .path = local->origpath,
      .ino = local->inode->ino
    };
    STACK_WIND (frame,
		trash_common_unwind_cbk,
		this->children->xlator,
		this->children->xlator->fops->unlink,
		&tmp_loc);
  } else if (op_ret == -1 && op_errno == EISDIR) {
    gf_log (this->name, 
	    GF_LOG_WARNING, 
	    "Target exists as a directory, cannot keep the copy, deleting");
    loc_t tmp_loc = {
      .inode = local->inode,
      .path = local->origpath,
      .ino = local->inode->ino
    };
    STACK_WIND (frame,
		trash_common_unwind_cbk,
		this->children->xlator,
		this->children->xlator->fops->unlink,
		&tmp_loc);
  } else {
    /* */
    STACK_UNWIND (frame, 0, op_errno);
  }
  
  return 0;
}


/**
 * trash_unlink - 
 */
static int32_t
trash_unlink (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
  trash_private_t *priv = this->private;

  if (strncmp (loc->path, priv->trash_dir, strlen(priv->trash_dir)) == 0) {
    /* Trying to rename from the trash can dir, do the actual thing */
    STACK_WIND (frame,
		trash_common_unwind_cbk,
		this->children->xlator,
		this->children->xlator->fops->unlink,
		loc);
  } else {
    trash_local_t *local = calloc (1, sizeof (trash_local_t));
    if (!local) {
      STACK_UNWIND (frame, -1, ENOMEM);
      return 0;
    }
    frame->local = local;
    local->inode = loc->inode;
    strcpy (local->origpath, loc->path);
    strcpy (local->newpath, priv->trash_dir);
    strcat (local->newpath, loc->path);
    {
      loc_t new_loc = {
	.inode = NULL,
	.ino = 0,
	.path = local->newpath
      };
      STACK_WIND (frame,
		  trash_unlink_rename_cbk,
		  this->children->xlator,
		  this->children->xlator->fops->rename,
		  loc,
		  &new_loc);
    }
  }
  return 0;
}


/**
 * trash_rename_lookup_cbk - 
 */
static int32_t 
trash_rename_lookup_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 inode_t *inode,
			 struct stat *buf)
{
  trash_local_t *local = frame->local;

  if (op_ret == -1) {
    loc_t oldloc = {
      .inode = local->inode,
      .ino = local->inode->ino,
      .path = local->oldpath
    };
    loc_t newloc = {
      .inode = NULL,
      .ino = 0,
      .path = local->origpath
    };
    STACK_WIND (frame,
		trash_common_unwind_buf_cbk,
		this->children->xlator,
		this->children->xlator->fops->rename,
		&oldloc,
		&newloc);
  }
  return 0;
}


/**
 * trash_rename - 
 */
static int32_t 
trash_rename (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *oldloc,
	      loc_t *newloc)
{
  trash_private_t *priv = this->private;
  if (strncmp (oldloc->path, priv->trash_dir, strlen(priv->trash_dir)) == 0) {
    /* Trying to rename from the trash can dir, do the actual thing */
    STACK_WIND (frame,
		trash_common_unwind_buf_cbk,
		this->children->xlator,
		this->children->xlator->fops->rename,
		oldloc,
		newloc);
  } else {
    /* Trying to rename a regular file from GlusterFS */
    trash_local_t *local = calloc (1, sizeof (trash_local_t));
    if (!local) {
      STACK_UNWIND (frame, -1, ENOMEM, NULL);
      return 0;
    }
    frame->local = local;
    local->inode = oldloc->inode;
    strcpy (local->newpath, priv->trash_dir);
    strcat (local->newpath, newloc->path);
    strcpy (local->origpath, newloc->path);
    strcpy (local->oldpath, oldloc->path);
    
    /* Send a lookup call on newloc, to ensure we are not overwriting */
    STACK_WIND (frame,
		trash_rename_lookup_cbk,
		this->children->xlator,
		this->children->xlator->fops->lookup,
		newloc);
  }
  return 0;
}

/**
 * trash_init -
 */
int32_t 
init (xlator_t *this)
{
  data_t *trash_dir = NULL;
  trash_private_t *_priv = NULL;

  /* Create .trashcan directory in init */
  if (!this->children || this->children->next) {
    gf_log (this->name,
	    GF_LOG_ERROR,
	    "Not configured with exactly one child. exiting",
	    this->name);
    return -1;
  }

  _priv = calloc (1, sizeof (*_priv));
    
  trash_dir = dict_get (this->options, "trash-dir");
  if (!trash_dir) {
    gf_log (this->name, 
	    GF_LOG_WARNING,
	    "No option specified for trash-dir, using \"/.trash/\"");
    strcpy (_priv->trash_dir, "/.trash");
  } else {
    strcpy (_priv->trash_dir, trash_dir->data);
  }

  this->private = (void *)_priv;
  return 0;
}

/**
 * trash_init -
 */
void
fini (xlator_t *this)
{
  trash_private_t *priv = this->private;
  free (priv);
  return;
}


struct xlator_fops fops = {
  .unlink = trash_unlink,
  .rename = trash_rename,
};

struct xlator_mops mops = {

};
