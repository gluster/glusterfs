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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"

#define GF_SQUASH_NOBODY_UID         65534
#define GF_SQUASH_NOBODY_GID         65534
#define GF_SQUASH_ROOT_UID           0
#define GF_SQUASH_ROOT_GID           0

#define GF_MAXIMUM_SQUASHING_ALLOWED 32

/*
  option root-squashing on (off by default)
  option uidmap <uid=newuid>;...
  option gidmap <gid=newgid>;...

*/

/* Calls which return at this level */

struct gf_squash {
	int num_uid_entries;
	int num_gid_entries;
	int input_uid[GF_MAXIMUM_SQUASHING_ALLOWED];
	int output_uid[GF_MAXIMUM_SQUASHING_ALLOWED];
	int input_gid[GF_MAXIMUM_SQUASHING_ALLOWED];
	int output_gid[GF_MAXIMUM_SQUASHING_ALLOWED];
};

/* update_frame: The main logic of the whole translator.
   Return values:
   0: no change
   1: only uid changed 
   2: only gid changed
   3: both uid/gid changed
*/
static int32_t
update_frame (call_frame_t *frame,
	      struct gf_squash *squash)
{
	int32_t idx = 0;
	int32_t ret = 0;

	for (idx = 0; idx < squash->num_uid_entries; idx++) {
		if (frame->root->uid == squash->input_uid[idx]) {
			frame->root->uid = squash->output_uid[idx];
			ret = 1;
			break;
		}
	}
	
	for (idx = 0; idx < squash->num_gid_entries; idx++) {
		if (frame->root->gid == squash->input_gid[idx]) {
			frame->root->gid = squash->output_gid[idx];
			if (ret == 0) 
				ret = 2;
			else 
				ret = 3;
			break;
		}
	}

	return ret;
}

/* if 'root' don't change the uid/gid */
static int32_t
update_stat (struct stat *stbuf,
	     struct gf_squash *squash)
{
	int32_t idx = 0;
	for (idx = 0; idx < squash->num_uid_entries; idx++) {
		if (stbuf->st_uid == GF_SQUASH_ROOT_UID)
			continue;
		if (stbuf->st_uid == squash->input_uid[idx]) {
			stbuf->st_uid = squash->output_uid[idx];
			break;
		}
	}
	
	for (idx = 0; idx < squash->num_gid_entries; idx++) {
		if (stbuf->st_gid == GF_SQUASH_ROOT_GID)
			continue;
		if (stbuf->st_gid == squash->input_gid[idx]) {
			stbuf->st_gid = squash->output_gid[idx];
			break;
		}
	}

	return 0;
}

static int32_t 
squash_lookup_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
		   struct stat *buf,
		   dict_t *dict)
{
	update_stat (buf, this->private);
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      inode,
		      buf,
		      dict);
	return 0;
}

int32_t 
squash_lookup (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       int32_t need_xattr)
{
	STACK_WIND (frame,
		    squash_lookup_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->lookup,
		    loc,
		    need_xattr);
	return 0;
}


static int32_t
squash_stat_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct stat *buf)
{
	update_stat (buf, this->private);
	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t
squash_stat (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{
	STACK_WIND (frame,
		    squash_stat_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->stat,
		    loc);
	return 0;
}

static int32_t
squash_chmod_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
	update_stat (buf, this->private);
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t
squash_chmod (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      mode_t mode)
{
/*TODO */
	int32_t ret = 0;
	ret = update_frame (frame, this->private);
	if (ret) {
		switch (ret) {
		case 1:
			if (loc->inode->st_mode & S_IWGRP)
				break;
		case 3:
			if (loc->inode->st_mode & S_IWOTH)
				break;
			gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
			STACK_UNWIND (frame, -1, EPERM, NULL);
			return 0;
		}			
	}
	STACK_WIND (frame,
		    squash_chmod_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->chmod,
		    loc,
		    mode);
	return 0;
}


static int32_t
squash_fchmod_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *buf)
{
	update_stat (buf, this->private);
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t 
squash_fchmod (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       mode_t mode)
{
	STACK_WIND (frame,
		    squash_fchmod_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fchmod,
		    fd,
		    mode);
	return 0;
}

static int32_t
squash_chown_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
	update_stat (buf, this->private);
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t
squash_chown (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      uid_t uid,
	      gid_t gid)
{
/*TODO */
	int32_t ret = 0;
	ret = update_frame (frame, this->private);
	if (ret) {
		switch (ret) {
		case 1:
			if (loc->inode->st_mode & S_IWGRP)
				break;
		case 3:
			if (loc->inode->st_mode & S_IWOTH)
				break;
			gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
			STACK_UNWIND (frame, -1, EPERM, NULL);
			return 0;
		}			
	}
	STACK_WIND (frame,	      
		    squash_chown_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->chown,
		    loc,
		    uid,
		    gid);
	return 0;
}

static int32_t
squash_fchown_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *buf)
{
	update_stat (buf, this->private);
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t 
squash_fchown (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       uid_t uid,
	       gid_t gid)
{
	STACK_WIND (frame,	      
		    squash_fchown_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fchown,
		    fd,
		    uid,
		    gid);
	return 0;
}

static int32_t
squash_truncate_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
	update_stat (buf, this->private);
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t
squash_truncate (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 off_t offset)
{
/*TODO */
	int32_t ret = 0;
	ret = update_frame (frame, this->private);
	if (ret) {
		switch (ret) {
		case 1:
			if (loc->inode->st_mode & S_IWGRP)
				break;
		case 3:
			if (loc->inode->st_mode & S_IWOTH)
				break;
			gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
			STACK_UNWIND (frame, -1, EPERM, NULL);
			return 0;
		}			
	}
	STACK_WIND (frame,
		    squash_truncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->truncate,
		    loc,
		    offset);
	return 0;
}

static int32_t
squash_ftruncate_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct stat *buf)
{
	update_stat (buf, this->private);
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t
squash_ftruncate (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  off_t offset)
{
	STACK_WIND (frame,
		    squash_ftruncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->ftruncate,
		    fd,
		    offset);
	return 0;
}

int32_t 
squash_utimens_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
	update_stat (buf, this->private);
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}


int32_t 
squash_utimens (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		struct timespec tv[2])
{
	/* TODO */
	int32_t ret = 0;
	ret = update_frame (frame, this->private);
	if (ret) {
		switch (ret) {
		case 1:
			if (loc->inode->st_mode & S_IWGRP)
				break;
		case 3:
			if (loc->inode->st_mode & S_IWOTH)
				break;
			gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
			STACK_UNWIND (frame, -1, EPERM, NULL);
			return 0;
		}			
	}
	STACK_WIND (frame,
		    squash_utimens_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->utimens,
		    loc,
		    tv);
	return 0;
}

static int32_t
squash_readlink_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     const char *path)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      path);
	return 0;
}

int32_t
squash_readlink (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 size_t size)
{
	/*TODO */
	int32_t ret = 0;
	ret = update_frame (frame, this->private);
	if (ret) {
		switch (ret) {
		case 1:
			if (loc->inode->st_mode & S_IRGRP)
				break;
		case 3:
			if (loc->inode->st_mode & S_IROTH)
				break;
			gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
			STACK_UNWIND (frame, -1, EPERM, NULL);
			return 0;
		}			
	}
	STACK_WIND (frame,
		    squash_readlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readlink,
		    loc,
		    size);
	return 0;
}


static int32_t
squash_mknod_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  inode_t *inode,
		  struct stat *buf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      inode,
		      buf);
	return 0;
}

int32_t
squash_mknod (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      mode_t mode,
	      dev_t rdev)
{
	/* TODO: Parent directory permission matters */
	update_frame (frame, this->private);
	STACK_WIND (frame,
		    squash_mknod_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mknod,
		    loc, mode, rdev);
	return 0;
}

static int32_t
squash_mkdir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  inode_t *inode,
		  struct stat *buf)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      inode,
		      buf);
	return 0;
}

int32_t
squash_mkdir (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      mode_t mode)
{
	/* TODO: Parent directory permission matters */
	update_frame (frame, this->private);
	STACK_WIND (frame,
		    squash_mkdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mkdir,
		    loc, mode);
	return 0;
}

static int32_t
squash_unlink_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t
squash_unlink (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc)
{
	int32_t ret = 0;
	ret = update_frame (frame, this->private);
	if (ret) {
		switch (ret) {
		case 1:
			if (loc->inode->st_mode & S_IWGRP)
				break;
		case 3:
			if (loc->inode->st_mode & S_IWOTH)
				break;
			gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
			STACK_UNWIND (frame, -1, EPERM);
			return 0;
		}			
	}
	STACK_WIND (frame,
		    squash_unlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->unlink,
		    loc);
	return 0;
}

static int32_t
squash_rmdir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}

int32_t
squash_rmdir (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
	int32_t ret = 0;
	ret = update_frame (frame, this->private);
	if (ret) {
		switch (ret) {
		case 1:
			if (loc->inode->st_mode & S_IWGRP)
				break;
		case 3:
			if (loc->inode->st_mode & S_IWOTH)
				break;
			gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
			STACK_UNWIND (frame, -1, EPERM);
			return 0;
		}			
	}
	STACK_WIND (frame,
		    squash_rmdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rmdir,
		    loc);
	return 0;
}

static int32_t
squash_symlink_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct stat *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, inode,	buf);
	return 0;
}

int32_t
squash_symlink (call_frame_t *frame,
		xlator_t *this,
		const char *linkpath,
		loc_t *loc)
{
	/* TODO: parent directory permissions matters */
	update_frame (frame, this->private);
	STACK_WIND (frame,
		    squash_symlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->symlink,
		    linkpath, loc);
	return 0;
}


static int32_t
squash_rename_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *buf)
{
	update_stat (buf, this->private);
	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t
squash_rename (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *oldloc,
	       loc_t *newloc)
{
	/* TODO: logically parent directory permissions matters */
	int32_t ret = 0;
	ret = update_frame (frame, this->private);
	if (ret) {
		switch (ret) {
		case 1:
			if (oldloc->inode->st_mode & S_IWGRP)
				break;
		case 3:
			if (oldloc->inode->st_mode & S_IWOTH)
				break;
			gf_log (this->name, GF_LOG_DEBUG, 
				"%s->%s: returning permission denied", 
				oldloc->path, newloc->path);
			STACK_UNWIND (frame, -1, EPERM, NULL);
			return 0;
		}			
	}
	STACK_WIND (frame,
		    squash_rename_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rename,
		    oldloc, newloc);
	return 0;
}


static int32_t
squash_link_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, inode,	buf);
	return 0;
}

int32_t
squash_link (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     const char *newname)
{
	update_frame (frame, this->private);
	STACK_WIND (frame,
		    squash_link_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->link,
		    loc, newname);
	return 0;
}


static int32_t
squash_create_cbk (call_frame_t *frame,
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
squash_create (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       int32_t flags,
	       mode_t mode, fd_t *fd)
{
	/* TODO: Parent directory permission */
	update_frame (frame, this->private);
	STACK_WIND (frame, squash_create_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->create,
		    loc, flags, mode, fd);
	return 0;
}

static int32_t
squash_open_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 fd_t *fd)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      fd);
	return 0;
}

int32_t
squash_open (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     int32_t flags, 
	     fd_t *fd)
{
	int32_t ret = 0;
	ret = update_frame (frame, this->private);
	if (ret) {
		switch (ret) {
		case 1:
			if (loc->inode->st_mode & S_IWGRP)
				break;
			if (!((flags & O_WRONLY) || (flags & O_RDWR)) 
			    && (loc->inode->st_mode & S_IRGRP))
				break;
		case 3:
			if (loc->inode->st_mode & S_IWOTH)
				break;
			if (!((flags & O_WRONLY) || (flags & O_RDWR))
			    && (loc->inode->st_mode & S_IROTH))
				break;
			gf_log (this->name, GF_LOG_DEBUG, 
				"%s: returning permission denied (mode: 0%o, flag=0%o)", 
				loc->path, loc->inode->st_mode, flags);
			STACK_UNWIND (frame, -1, EPERM, fd);
			return 0;
		}			
	}
	STACK_WIND (frame,
		    squash_open_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->open,
		    loc, flags, fd);
	return 0;
}

static int32_t
squash_readv_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct iovec *vector,
		  int32_t count,
		  struct stat *stbuf)
{
	update_stat (stbuf, this->private);
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      vector,
		      count,
		      stbuf);
	return 0;
}

int32_t
squash_readv (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      size_t size,
	      off_t offset)
{
	STACK_WIND (frame,
		    squash_readv_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readv,
		    fd,
		    size,
		    offset);
	return 0;
}


static int32_t
squash_writev_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *stbuf)
{
	update_stat (stbuf, this->private);
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      stbuf);
	return 0;
}

int32_t
squash_writev (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       struct iovec *vector,
	       int32_t count,
	       off_t off)
{
	STACK_WIND (frame,
		    squash_writev_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->writev,
		    fd,
		    vector,
		    count,
		    off);
	return 0;
}

static int32_t
squash_fstat_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
	update_stat (buf, this->private);
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t
squash_fstat (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
	STACK_WIND (frame,
		    squash_fstat_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fstat,
		    fd);
	return 0;
}

static int32_t
squash_opendir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    fd_t *fd)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      fd);
	return 0;
}

int32_t
squash_opendir (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc, fd_t *fd)
{
/*TODO*/
	int32_t ret = 0;
	ret = update_frame (frame, this->private);
	if (ret) {
		switch (ret) {
		case 1:
			if (loc->inode->st_mode & S_IWGRP)
				break;
			if (loc->inode->st_mode & S_IRGRP)
				break;
		case 3:
			if (loc->inode->st_mode & S_IWOTH)
				break;
			if (loc->inode->st_mode & S_IROTH)
				break;
			gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
			STACK_UNWIND (frame, -1, EPERM, fd);
			return 0;
		}			
	}
	STACK_WIND (frame,
		    squash_opendir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->opendir,
		    loc, fd);
	return 0;
}


static int32_t
squash_setxattr_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno);
	return 0;
}

int32_t
squash_setxattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 dict_t *dict,
		 int32_t flags)
{
/*TODO */
	int32_t ret = 0;
	ret = update_frame (frame, this->private);
	if (ret) {
		switch (ret) {
		case 1:
			if (loc->inode->st_mode & S_IWGRP)
				break;
		case 3:
			if (loc->inode->st_mode & S_IWOTH)
				break;
			gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
			STACK_UNWIND (frame, -1, EPERM);
			return 0;
		}			
	}
	STACK_WIND (frame,
		    squash_setxattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->setxattr,
		    loc,
		    dict,
		    flags);
	return 0;
}

static int32_t
squash_getxattr_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     dict_t *dict)
{
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      dict);
	return 0;
}

int32_t
squash_getxattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 const char *name)
{
	int32_t ret = 0;
	ret = update_frame (frame, this->private);
	if (ret) {
		switch (ret) {
		case 1:
			if (loc->inode->st_mode & S_IRGRP)
				break;
		case 3:
			if (loc->inode->st_mode & S_IROTH)
				break;
			gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
			STACK_UNWIND (frame, -1, EPERM, NULL);
			return 0;
		}			
	}
	STACK_WIND (frame,
		    squash_getxattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->getxattr,
		    loc,
		    name);
	return 0;
}

static int32_t
squash_removexattr_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t
squash_removexattr (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc,
		    const char *name)
{
	int32_t ret = 0;
	ret = update_frame (frame, this->private);
	if (ret) {
		switch (ret) {
		case 1:
			if (loc->inode->st_mode & S_IWGRP)
				break;
		case 3:
			if (loc->inode->st_mode & S_IWOTH)
				break;
			gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
			STACK_UNWIND (frame, -1, EPERM);
			return 0;
		}			
	}
	STACK_WIND (frame,
		    squash_removexattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->removexattr,
		    loc,
		    name);
	return 0;
}

int32_t 
init (xlator_t *this)
{
	char *value = NULL;
	char *tmp_str = NULL;
	char *tmp_str1 = NULL;
	char *dup_str = NULL;
	char *input_value_str = NULL;
	char *output_value_str = NULL;
	int32_t input_value = 0;
	int32_t output_value = 0;
	data_t *option_data = NULL;
	struct gf_squash *squash = NULL;

	if (!this->children || this->children->next) {
		gf_log (this->name,
			GF_LOG_ERROR,
			"translator not configured with exactly one child",
			this->name);
		return -1;
	}
	
	squash = calloc (sizeof (*squash), 1);
	ERR_ABORT (squash);
	
	if (dict_get (this->options, "root-squashing")) {
		value = data_to_str (dict_get (this->options, "root-squashing"));
		if ((strcasecmp (value, "enable") == 0) ||
		    (strcasecmp (value, "on") == 0)) {
			squash->num_uid_entries = 1;
			squash->num_gid_entries = 1;
			squash->input_uid[0] = GF_SQUASH_ROOT_UID; /* root */
			squash->input_gid[0] = GF_SQUASH_ROOT_GID; /* root */
			squash->output_uid[0] = GF_SQUASH_NOBODY_UID;
			squash->output_gid[0] = GF_SQUASH_NOBODY_GID;
		}
	}

	if (dict_get (this->options, "uidmap")) {
		option_data = dict_get (this->options, "uidmap");
		value = strtok_r (option_data->data, ",", &tmp_str);
		while (value) {
			dup_str = strdup (value);
			input_value_str = strtok_r (dup_str, "=", &tmp_str1);
			if (input_value_str) {
				if (gf_string2int (input_value_str, &input_value) != 0) {
					gf_log (this->name, GF_LOG_ERROR, 
						"invalid number format \"%s\"", 
						input_value_str);
					return -1;
				}
				output_value_str = strtok_r (NULL, "=", &tmp_str1);
				if (output_value_str) {
					if (gf_string2int (output_value_str, &output_value) != 0) {
						gf_log (this->name, GF_LOG_ERROR, 
							"invalid number format \"%s\"", 
							output_value_str);
						return -1;
					}
				} else {
					gf_log (this->name, GF_LOG_ERROR, 
						"mapping string not valid");
					return -1;
				}
			} else {
				gf_log (this->name, GF_LOG_ERROR, 
					"mapping string not valid");
				return -1;
			}
			squash->input_uid[squash->num_uid_entries] = input_value;
			squash->output_uid[squash->num_uid_entries] = output_value;
			gf_log (this->name, 
				GF_LOG_DEBUG, 
				"pair %d: input uid '%d' will be changed to uid '%d'", 
				squash->num_uid_entries, input_value, output_value);

			squash->num_uid_entries++;
			if (squash->num_uid_entries == GF_MAXIMUM_SQUASHING_ALLOWED)
				break;
			value = strtok_r (NULL, ",", &tmp_str);
		}
		
	}

	if (dict_get (this->options, "gidmap")) {
		option_data = dict_get (this->options, "gidmap");
		value = strtok_r (option_data->data, ",", &tmp_str);
		while (value) {
			dup_str = strdup (value);
			input_value_str = strtok_r (dup_str, "=", &tmp_str1);
			if (input_value_str) {
				if (gf_string2int (input_value_str, &input_value) != 0) {
					gf_log (this->name, GF_LOG_ERROR, 
						"invalid number format \"%s\"", 
						input_value_str);
					return -1;
				}
				output_value_str = strtok_r (NULL, "=", &tmp_str1);
				if (output_value_str) {
					if (gf_string2int (output_value_str, &output_value) != 0) {
						gf_log (this->name, GF_LOG_ERROR, 
							"invalid number format \"%s\"", 
							output_value_str);
						return -1;
					}
				} else {
					gf_log (this->name, GF_LOG_ERROR, 
						"mapping string not valid");
					return -1;
				}
			} else {
				gf_log (this->name, GF_LOG_ERROR, 
					"mapping string not valid");
				return -1;
			}

			squash->input_gid[squash->num_gid_entries] = input_value;
			squash->output_gid[squash->num_gid_entries] = output_value;

			gf_log (this->name, 
				GF_LOG_DEBUG, 
				"pair %d: input gid '%d' will be changed to gid '%d'", 
				squash->num_gid_entries, input_value, output_value);

			squash->num_gid_entries++;
			if (squash->num_gid_entries == GF_MAXIMUM_SQUASHING_ALLOWED)
				break;
			value = strtok_r (NULL, ",", &tmp_str);
		}
	}

	this->private = squash;
	return 0;
}


void
fini (xlator_t *this)
{
	struct gf_squash *squash = this->private;

	FREE (squash);

	return;
}


struct xlator_fops fops = {
	.lookup      = squash_lookup,
	.stat        = squash_stat,
	.fstat       = squash_fstat,
	.chmod       = squash_chmod,
	.fchmod      = squash_fchmod,
	.readlink    = squash_readlink,
	.mknod       = squash_mknod,
	.mkdir       = squash_mkdir,
	.unlink      = squash_unlink,
	.rmdir       = squash_rmdir,
	.symlink     = squash_symlink,
	.rename      = squash_rename,
	.link        = squash_link,
	.chown       = squash_chown,
	.fchown      = squash_fchown,
	.truncate    = squash_truncate,
	.ftruncate   = squash_ftruncate,
	.create      = squash_create,
	.open        = squash_open,
	.readv       = squash_readv,
	.writev      = squash_writev,
	.setxattr    = squash_setxattr,
	.getxattr    = squash_getxattr,
	.removexattr = squash_removexattr,
	.opendir     = squash_opendir,
	.utimens     = squash_utimens,
};

struct xlator_mops mops = {

};
