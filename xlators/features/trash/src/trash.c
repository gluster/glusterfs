/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 


#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"

#include <libgen.h>

struct trash_struct {
  char origpath[4096];
  char mkdir_origpath[4096];
  char mkdir_path[4096];
  char newpath[4096];
  char oldpath[4096]; // used only in case of rename
};

/* Unlink related */
static int32_t 
trash_mkdir_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf);


static int32_t 
trash_newpath_mkdir_cbk (call_frame_t *frame,
			 call_frame_t *prev_frame,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct stat *buf);

static int32_t 
trash_target_unlink_cbk (call_frame_t *frame,
			 call_frame_t *prev_frame,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct stat *buf);

static int32_t 
trash_rename_cbk (call_frame_t *frame,
		  call_frame_t *prev_frame,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
  struct trash_struct *local = frame->local;
  if (op_ret == -1 && op_errno == ENOENT) {
    /* check for the errno, if its ENOENT create directory and call rename later */
    STACK_WIND (frame,
		trash_mkdir_cbk,
		this->children->xlator,
		this->children->xlator->fops->mkdir,
		local->mkdir_path,
		0);
  } else if (op_ret == -1 && op_errno == ENOTDIR) {
    char *temppath = strdup (local->newpath);
    STACK_WIND (frame,
		trash_target_unlink_cbk,
		this->children->xlator,
		this->children->xlator->fops->unlink,
		dirname (temppath));
    free (temppath);
  } else if (op_ret == -1 && op_errno == EISDIR) {
    gf_log ("trash", 
	    GF_LOG_WARNING, 
	    "Target exists as a directory, cannot keep the copy, deleting");
    STACK_WIND (frame,
		trash_rename_cbk,
		this->children->xlator,
		this->children->xlator->fops->unlink,
		local->origpath);
  } else {
    /* */
    STACK_UNWIND (frame, 0, op_errno);
  }
  
  return 0;
}

static int32_t 
trash_target_unlink_cbk (call_frame_t *frame,
			 call_frame_t *prev_frame,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct stat *buf) 
{
  struct trash_struct *local = frame->local;
  STACK_WIND (frame,
	      trash_rename_cbk,
	      this->children->xlator,
	      this->children->xlator->fops->rename,
	      local->origpath,
	      local->newpath);
  return 0;
}

static int32_t 
trash_chmod_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  struct trash_struct *local = frame->local;
  if (op_ret == -1) {
    gf_log ("trash", GF_LOG_ERROR, "trash_chmod_cbk failed for %s", local->mkdir_path);
    STACK_UNWIND (frame, op_ret, op_errno);
    return 0;
  }
  
  strcpy (local->mkdir_path, local->mkdir_origpath);
  STACK_WIND (frame,
	      trash_rename_cbk,
	      this->children->xlator,
	      this->children->xlator->fops->rename,
	      local->origpath,
	      local->newpath);
  
  return 0;
}

static int32_t 
trash_mkdir_cbk (call_frame_t *frame,
		 call_frame_t *prev_frame,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
  struct trash_struct *local = frame->local;

  if (op_ret == -1 && op_errno != EEXIST && op_errno != ENOENT) {
    STACK_UNWIND (frame, op_ret, op_errno);
    return 0;
  }

  if (op_ret == -1 && op_errno == EEXIST) {
    strcpy (local->mkdir_path, local->mkdir_origpath);
    STACK_WIND (frame,
		trash_rename_cbk,
		this->children->xlator,
		this->children->xlator->fops->rename,
		local->origpath,
		local->newpath);
  } else if (op_ret == -1 && op_errno == ENOENT) {
    char *temppath = strdup (local->mkdir_path);
    strcpy (local->mkdir_path, dirname (temppath));
    free (temppath);
    STACK_WIND (frame,
		trash_mkdir_cbk,
		this->children->xlator,
		this->children->xlator->fops->mkdir,
		local->mkdir_path,
		0);
  } else {
    STACK_WIND (frame,
		trash_chmod_cbk,
		this->children->xlator,
		this->children->xlator->fops->chmod,
		local->mkdir_path,
		0777);
  }
  return 0;
}

static int32_t
trash_unlink (call_frame_t *frame,
	      xlator_t *this,
	      const char *path)
{
  struct trash_struct *local = calloc (1, sizeof (struct trash_struct));

  frame->local = local;
  strcpy (local->mkdir_path, "/.trash");
  strcpy (local->mkdir_origpath, "/.trash");
  strcpy (local->newpath, "/.trash");
  strcat (local->newpath, path);
  strcpy (local->origpath, path);
  char *temppath = strdup (local->origpath);
  strcat (local->mkdir_origpath, dirname (temppath));
  strcat (local->mkdir_path, dirname (temppath));
  free (temppath);  // rename the path to newpath */ 
  STACK_WIND (frame,
	      trash_rename_cbk,
	      this->children->xlator,
	      this->children->xlator->fops->rename,
	      local->origpath,
	      local->newpath);

  return 0;
}

/* Rename */
static int32_t 
trash_rename_rename_cbk (call_frame_t *frame,
			  call_frame_t *prev_frame,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno)
{
  STACK_UNWIND (frame, op_ret, op_errno);
  return 0;
}

static int32_t 
trash_newpath_rename_cbk (call_frame_t *frame,
			  call_frame_t *prev_frame,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno)
{
  struct trash_struct *local = frame->local;
  if (op_ret == -1 && op_errno == ENOENT) {
    /* check for the errno, if its ENOENT create directory and call rename later */
    STACK_WIND (frame,
		trash_newpath_mkdir_cbk,
		this->children->xlator,
		this->children->xlator->fops->mkdir,
		local->mkdir_path,
		0);
  } else if (op_ret == -1 && op_errno == ENOTDIR) {
    char *temppath = strdup (local->newpath);
    STACK_WIND (frame,
		trash_target_unlink_cbk,
		this->children->xlator,
		this->children->xlator->fops->unlink,
		dirname (temppath));
    free (temppath);
  } else if (op_ret == -1 && op_errno == EISDIR) {
    gf_log ("trash", 
	    GF_LOG_WARNING, 
	    "Target exists as a directory, cannot keep the copy, deleting");
    STACK_WIND (frame,
		trash_rename_rename_cbk,
		this->children->xlator,
		this->children->xlator->fops->rename,
		local->oldpath,
		local->origpath);
  } else {
    STACK_WIND (frame,
		trash_rename_rename_cbk,
		this->children->xlator,
		this->children->xlator->fops->rename,
		local->oldpath,
		local->origpath);
  }
  
  return 0;
}


static int32_t 
trash_newpath_chmod_cbk (call_frame_t *frame,
			 call_frame_t *prev_frame,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct stat *buf)
{
  struct trash_struct *local = frame->local;
  if (op_ret == -1) {
    gf_log ("trash", GF_LOG_ERROR, "trash_chmod_cbk failed for %s", local->mkdir_path);
    STACK_UNWIND (frame, op_ret, op_errno);
    return 0;
  }
  
  strcpy (local->mkdir_path, local->mkdir_origpath);
  STACK_WIND (frame,
	      trash_newpath_rename_cbk,
	      this->children->xlator,
	      this->children->xlator->fops->rename,
	      local->origpath,
	      local->newpath);
  
  return 0;
}

static int32_t 
trash_newpath_mkdir_cbk (call_frame_t *frame,
			 call_frame_t *prev_frame,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct stat *buf)
{
  struct trash_struct *local = frame->local;

  if (op_ret == -1 && op_errno != EEXIST && op_errno != ENOENT) {
    STACK_UNWIND (frame, op_ret, op_errno);
    return 0;
  }

  if (op_ret == -1 && op_errno == EEXIST) {
    strcpy (local->mkdir_path, local->mkdir_origpath);
    STACK_WIND (frame,
		trash_newpath_rename_cbk,
		this->children->xlator,
		this->children->xlator->fops->rename,
		local->origpath,
		local->newpath);
  } else if (op_ret == -1 && op_errno == ENOENT) {
    char *temppath = strdup (local->mkdir_path);
    strcpy (local->mkdir_path, dirname (temppath));
    free (temppath);
    STACK_WIND (frame,
		trash_newpath_mkdir_cbk,
		this->children->xlator,
		this->children->xlator->fops->mkdir,
		local->mkdir_path,
		0);
  } else {
    STACK_WIND (frame,
		trash_newpath_chmod_cbk,
		this->children->xlator,
		this->children->xlator->fops->chmod,
		local->mkdir_path,
		0777);
  }
  return 0;
}

static int32_t 
trash_getattr_cbk (call_frame_t *frame,
		   call_frame_t *prev_frame,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *stbuf)
{
  struct trash_struct *local = frame->local;
  
  if (op_ret != 0) {
    /* Call rename, as the newpath doesn't exist */
    STACK_WIND (frame,
		trash_rename_rename_cbk,
		this->children->xlator,
		this->children->xlator->fops->rename,
		local->oldpath,
		local->origpath);
  } else {
    /* Target file/dir exists, so send newpath to ".trash" */
    STACK_WIND (frame,
		trash_newpath_rename_cbk,
		this->children->xlator,
		this->children->xlator->fops->rename,
		local->origpath,
		local->newpath);
  }

  return 0;
}

static int32_t 
trash_rename (call_frame_t *frame,
	      xlator_t *this,
	      const char *oldpath,
	      const char *newpath)
{
  struct trash_struct *local = calloc (1, sizeof (struct trash_struct));

  frame->local = local;
  strcpy (local->mkdir_path, "/.trash");
  strcpy (local->mkdir_origpath, "/.trash");
  strcpy (local->newpath, "/.trash");
  strcat (local->newpath, newpath);
  strcpy (local->origpath, newpath);
  strcpy (local->oldpath, oldpath);
  
  char *temppath = strdup (local->origpath);
  strcat (local->mkdir_origpath, dirname (temppath));
  strcat (local->mkdir_path, dirname (temppath));
  free (temppath);  

  /* rename the oldpath to newpath */
  STACK_WIND (frame,
	      trash_getattr_cbk,
	      this->children->xlator,
	      this->children->xlator->fops->getattr,
	      local->origpath);
  
  return 0;
}

int32_t 
init (xlator_t *this)
{
  /* Create .trashcan directory in init */
  if (!this->children || this->children->next) {
    gf_log ("feature/trash",
	    GF_LOG_ERROR,
	    "FATAL: xlator (%s) not configured with exactly one child",
	    this->name);
    return -1;
  }
    
  return 0;
}

void
fini (xlator_t *xl)
{
  return;
}


struct xlator_fops fops = {
  .unlink = trash_unlink,
  .rename = trash_rename,
};

struct xlator_mops mops = {

};
