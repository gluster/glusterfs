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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "filter-mem-types.h"

#define GF_FILTER_NOBODY_UID         65534
#define GF_FILTER_NOBODY_GID         65534
#define GF_FILTER_ROOT_UID           0
#define GF_FILTER_ROOT_GID           0

#define GF_MAXIMUM_FILTERING_ALLOWED 32

/*
  option root-filtering on (off by default)
  option translate-uid <uid-range=newuid,uid=newuid>
  option translate-gid <gid-range=newgid,gid=newgid>
  option read-only <yes|true>
  option fixed-uid <uid>
  option fixed-gid <gid>
  option filter-uid <uid-range,uid>
  option filter-gid <gid-range,gid> // not supported yet

*/

struct gf_filter {
	/* Flags */
	gf_boolean_t complete_read_only;
	char fixed_uid_set;
	char fixed_gid_set;
	char partial_filter;

	/* Options */
	/* Mapping/Filtering/Translate whatever you want to call */
	int translate_num_uid_entries;
	int translate_num_gid_entries;
	int translate_input_uid[GF_MAXIMUM_FILTERING_ALLOWED][2];
	int translate_output_uid[GF_MAXIMUM_FILTERING_ALLOWED];
	int translate_input_gid[GF_MAXIMUM_FILTERING_ALLOWED][2];
	int translate_output_gid[GF_MAXIMUM_FILTERING_ALLOWED];

	/* Fixed uid/gid */
	int fixed_uid;
	int fixed_gid;

	/* Filter */
	int filter_num_uid_entries;
	int filter_num_gid_entries;
	int filter_input_uid[GF_MAXIMUM_FILTERING_ALLOWED][2];
	int filter_input_gid[GF_MAXIMUM_FILTERING_ALLOWED][2];
	
};

/* update_frame: The main logic of the whole translator.
   Return values:
   0: no change
   // TRANSLATE
   1: only uid changed 
   2: only gid changed
   3: both uid/gid changed
   // FILTER
   4: uid in filter range
   5: gid in filter range  // not supported yet
   6: complete fs is readonly
*/

#define GF_FILTER_NO_CHANGE    0
#define	GF_FILTER_MAP_UID      1
#define GF_FILTER_MAP_GID      2
#define GF_FILTER_MAP_BOTH     3
#define GF_FILTER_FILTER_UID   4
#define GF_FILTER_FILTER_GID   5
#define GF_FILTER_RO_FS        6

static int32_t
update_frame (call_frame_t *frame,
	      inode_t *inode,
	      struct gf_filter *filter)
{
	uid_t    uid = 0;
	int32_t  idx = 0;
	int32_t  ret = 0;
	int32_t  dictret = 0;
	uint64_t tmp_uid = 0;
	
	for (idx = 0; idx < filter->translate_num_uid_entries; idx++) {
		if ((frame->root->uid >=filter->translate_input_uid[idx][0]) &&
		    (frame->root->uid <=filter->translate_input_uid[idx][1])) {
			dictret = inode_ctx_get (inode, frame->this, &tmp_uid);
			uid = (uid_t)tmp_uid;
			if (dictret == 0) {
				if (frame->root->uid != uid)
					ret = GF_FILTER_MAP_UID;
			} else {
				ret = GF_FILTER_MAP_UID;
			}
			break;
		}
	}
	
	for (idx = 0; idx < filter->translate_num_gid_entries; idx++) {
		if ((frame->root->gid >=filter->translate_input_gid[idx][0]) &&
		    (frame->root->gid <=filter->translate_input_gid[idx][1])) {
			if (ret == GF_FILTER_NO_CHANGE) 
				ret = GF_FILTER_MAP_GID;
			else 
				ret = GF_FILTER_MAP_BOTH;
			break;
		}
	}


	if (filter->complete_read_only)
		return GF_FILTER_RO_FS;
	
	if (filter->partial_filter) {
		dictret = inode_ctx_get (inode, frame->this, &tmp_uid);
		uid = (uid_t)tmp_uid;
		if (dictret != -1) {
			for (idx = 0; idx < filter->filter_num_uid_entries; 
			     idx++) {
				if ((uid >=filter->filter_input_uid[idx][0]) &&
				    (uid <=filter->filter_input_uid[idx][1])) {
					return GF_FILTER_FILTER_UID;
				}
			}
		}
	}

	return ret;
}

/* if 'root' don't change the uid/gid */
static int32_t
update_stat (struct iatt *stbuf,
	     struct gf_filter *filter)
{
	int32_t idx = 0;
	for (idx = 0; idx < filter->translate_num_uid_entries; idx++) {
		if (stbuf->ia_uid == GF_FILTER_ROOT_UID)
			continue;
		if ((stbuf->ia_uid >= filter->translate_input_uid[idx][0]) &&
		    (stbuf->ia_uid <= filter->translate_input_uid[idx][1])) {
			stbuf->ia_uid = filter->translate_output_uid[idx];
			break;
		}
	}
	
	for (idx = 0; idx < filter->translate_num_gid_entries; idx++) {
		if (stbuf->ia_gid == GF_FILTER_ROOT_GID)
			continue;
		if ((stbuf->ia_gid >= filter->translate_input_gid[idx][0]) &&
		    (stbuf->ia_gid <= filter->translate_input_gid[idx][1])) {
			stbuf->ia_gid = filter->translate_output_gid[idx];
			break;
		}
	}

	if (filter->fixed_uid_set) {
		stbuf->ia_uid = filter->fixed_uid;
	}

	if (filter->fixed_gid_set) {
		stbuf->ia_gid = filter->fixed_gid;
	}
	
	return 0;
}

static int32_t 
filter_lookup_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
		   struct iatt *buf,
		   dict_t *dict,
                   struct iatt *postparent)
{
	int ret = 0;
	if (op_ret >= 0) {
		update_stat (buf, this->private);
		ret = inode_ctx_put (inode, this, (uint64_t)(long)buf->ia_uid);
		if (ret == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"couldn't set context");
		}

		update_stat (postparent, this->private);
	}
	STACK_UNWIND (frame, op_ret, op_errno, inode, buf, dict, postparent);
	return 0;
}

int32_t 
filter_lookup (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       dict_t *xattr_req)
{
	STACK_WIND (frame,
		    filter_lookup_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->lookup,
		    loc,
		    xattr_req);
	return 0;
}


static int32_t
filter_stat_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 struct iatt *buf)
{
	if (op_ret >= 0) {
		update_stat (buf, this->private);
	}
	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t
filter_stat (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{
	STACK_WIND (frame,
		    filter_stat_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->stat,
		    loc);
	return 0;
}

static int32_t
filter_setattr_cbk (call_frame_t *frame,
                    void *cookie,
                    xlator_t *this,
                    int32_t op_ret,
                    int32_t op_errno,
                    struct iatt *preop,
                    struct iatt *postop)
{
	if (op_ret >= 0) {
		update_stat (preop, this->private);
		update_stat (postop, this->private);
	}
	STACK_UNWIND (frame, op_ret, op_errno, preop, postop);
	return 0;
}

int32_t
filter_setattr (call_frame_t *frame,
                xlator_t *this,
                loc_t *loc,
                struct iatt *stbuf,
                int32_t valid)
{
	int32_t ret = 0;
	ret = update_frame (frame, loc->inode, this->private);
	switch (ret) {
	case GF_FILTER_MAP_UID:
		if (loc->inode->st_mode & S_IWGRP)
			break;
	case GF_FILTER_MAP_BOTH:
		if (loc->inode->st_mode & S_IWOTH)
			break;
		gf_log (this->name, GF_LOG_DEBUG,
                        "%s: returning permission denied", loc->path);
		STACK_UNWIND (frame, -1, EPERM, NULL, NULL, NULL);
		return 0;

	case GF_FILTER_FILTER_UID:
	case GF_FILTER_FILTER_GID:
	case GF_FILTER_RO_FS:
		STACK_UNWIND (frame, -1, EROFS, NULL, NULL);
		return 0;
	default:
		break;
	}

	STACK_WIND (frame,
		    filter_setattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->setattr,
		    loc,
		    stbuf, valid);
	return 0;
}

static int32_t
filter_fsetattr_cbk (call_frame_t *frame,
                     void *cookie,
                     xlator_t *this,
                     int32_t op_ret,
                     int32_t op_errno,
                     struct iatt *preop,
                     struct iatt *postop)
{
	if (op_ret >= 0) {
                update_stat (preop, this->private);
		update_stat (postop, this->private);
	}
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      preop, postop);
	return 0;
}

int32_t
filter_fsetattr (call_frame_t *frame,
                 xlator_t *this,
                 fd_t *fd,
                 struct iatt *stbuf,
                 int32_t valid)
{
	STACK_WIND (frame,
		    filter_fsetattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fsetattr,
		    fd,
		    stbuf, valid);
	return 0;
}


static int32_t
filter_truncate_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct iatt *prebuf,
                     struct iatt *postbuf)
{
	if (op_ret >= 0) {
		update_stat (prebuf, this->private);
		update_stat (postbuf, this->private);
	}
	STACK_UNWIND (frame, op_ret, op_errno, prebuf, postbuf);
	return 0;
}

int32_t
filter_truncate (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 off_t offset)
{
	int32_t ret = 0;
	ret = update_frame (frame, loc->inode, this->private);
	switch (ret) {
	case GF_FILTER_MAP_UID:
		if (loc->inode->st_mode & S_IWGRP)
			break;
	case GF_FILTER_MAP_BOTH:
		if (loc->inode->st_mode & S_IWOTH)
			break;
		gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
		STACK_UNWIND (frame, -1, EPERM, NULL, NULL);
		return 0;
		
	case GF_FILTER_FILTER_UID:
	case GF_FILTER_FILTER_GID:
	case GF_FILTER_RO_FS:
		STACK_UNWIND (frame, -1, EROFS, NULL, NULL);
		return 0;
	}			

	STACK_WIND (frame,
		    filter_truncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->truncate,
		    loc,
		    offset);
	return 0;
}

static int32_t
filter_ftruncate_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      struct iatt *prebuf,
                      struct iatt *postbuf)
{
	if (op_ret >= 0) {
		update_stat (prebuf, this->private);
		update_stat (postbuf, this->private);
	}
	STACK_UNWIND (frame, op_ret, op_errno, prebuf, postbuf);
	return 0;
}

int32_t
filter_ftruncate (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  off_t offset)
{
	STACK_WIND (frame,
		    filter_ftruncate_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->ftruncate,
		    fd,
		    offset);
	return 0;
}


static int32_t
filter_readlink_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     const char *path,
                     struct iatt *sbuf)
{
        if (op_ret >= 0)
                update_stat (sbuf, this->private);

	STACK_UNWIND (frame, op_ret, op_errno, path, sbuf);
	return 0;
}

int32_t
filter_readlink (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 size_t size)
{
	int32_t ret = 0;
	ret = update_frame (frame, loc->inode, this->private);
	switch (ret) {
	case GF_FILTER_MAP_UID:
		if (loc->inode->st_mode & S_IRGRP)
			break;
	case GF_FILTER_MAP_BOTH:
		if (loc->inode->st_mode & S_IROTH)
			break;
		gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
		STACK_UNWIND (frame, -1, EPERM, NULL);
		return 0;
	}			
	STACK_WIND (frame,
		    filter_readlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readlink,
		    loc,
		    size);
	return 0;
}


static int32_t
filter_mknod_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  inode_t *inode,
                  struct iatt *buf,
                  struct iatt *preparent,
                  struct iatt *postparent)
{
	int ret = 0;

	if (op_ret >= 0) {
		update_stat (buf, this->private);
		ret = inode_ctx_put (inode, this, (uint64_t)(long)buf->ia_uid);
		if (ret == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"couldn't set context");
		}

		update_stat (preparent, this->private);
		update_stat (postparent, this->private);
	}
	STACK_UNWIND (frame, op_ret, op_errno, inode, buf,
                      preparent, postparent);
	return 0;
}

int32_t
filter_mknod (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      mode_t mode,
	      dev_t rdev)
{
	int ret = 0;
	inode_t *parent = loc->parent;
	ret = update_frame (frame, loc->inode, this->private);
	switch (ret) {		
	case GF_FILTER_MAP_UID:
		if (parent->st_mode & S_IWGRP)
			break;
	case GF_FILTER_MAP_BOTH:
		if (parent->st_mode & S_IWOTH)
			break;
		gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
		STACK_UNWIND (frame, -1, EPERM, NULL, NULL,
                              NULL, NULL);
		return 0;

	case GF_FILTER_FILTER_UID:
	case GF_FILTER_FILTER_GID:
	case GF_FILTER_RO_FS:
		STACK_UNWIND (frame, -1, EROFS, NULL, NULL,
                              NULL, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    filter_mknod_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mknod,
		    loc, mode, rdev);
	return 0;
}

static int32_t
filter_mkdir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  inode_t *inode,
                  struct iatt *buf,
                  struct iatt *preparent,
                  struct iatt *postparent)
{
	int ret = 0;
	if (op_ret >= 0) {
		update_stat (buf, this->private);
		ret = inode_ctx_put (inode, this, (uint64_t)(long)buf->ia_uid);
		if (ret == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"couldn't set context");
		}

		update_stat (preparent, this->private);
		update_stat (postparent, this->private);
	}
	STACK_UNWIND (frame, op_ret, op_errno, inode, buf,
                      preparent, postparent);
	return 0;
}

int32_t
filter_mkdir (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      mode_t mode)
{
	int ret = 0;
	inode_t *parent = loc->parent;
	ret = update_frame (frame, loc->inode, this->private);
	switch (ret) {		
	case GF_FILTER_MAP_UID:
		if (parent->st_mode & S_IWGRP)
			break;
	case GF_FILTER_MAP_BOTH:
		if (parent->st_mode & S_IWOTH)
			break;
		gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
		STACK_UNWIND (frame, -1, EPERM, NULL, NULL,
                              NULL, NULL);
		return 0;

	case GF_FILTER_FILTER_UID:
	case GF_FILTER_FILTER_GID:
	case GF_FILTER_RO_FS:
		STACK_UNWIND (frame, -1, EROFS, NULL, NULL,
                              NULL, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    filter_mkdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mkdir,
		    loc, mode);
	return 0;
}

static int32_t
filter_unlink_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
                   struct iatt *preparent,
                   struct iatt *postparent)
{
        if (op_ret >= 0) {
		update_stat (preparent, this->private);
		update_stat (postparent, this->private);
        }

	STACK_UNWIND (frame, op_ret, op_errno, preparent, postparent);
	return 0;
}

int32_t
filter_unlink (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc)
{
	int32_t ret = 0;
	inode_t *parent = loc->parent;
	if (!parent)
		parent = inode_parent (loc->inode, 0, NULL);
	ret = update_frame (frame, loc->inode, this->private);
	switch (ret) {
	case GF_FILTER_MAP_UID:
		if (parent->st_mode & S_IWGRP)
			break;
		if (loc->inode->st_mode & S_IWGRP)
			break;
	case GF_FILTER_MAP_BOTH:
		if (parent->st_mode & S_IWOTH)
			break;
		if (loc->inode->st_mode & S_IWOTH)
			break;
		gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
		STACK_UNWIND (frame, -1, EPERM, NULL, NULL);
		return 0;
	case GF_FILTER_FILTER_UID:
	case GF_FILTER_FILTER_GID:
	case GF_FILTER_RO_FS:
		STACK_UNWIND (frame, -1, EROFS, NULL, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    filter_unlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->unlink,
		    loc);
	return 0;
}

static int32_t
filter_rmdir_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
                  struct iatt *preparent,
                  struct iatt *postparent)
{
        if (op_ret >= 0) {
		update_stat (preparent, this->private);
		update_stat (postparent, this->private);
        }

	STACK_UNWIND (frame, op_ret, op_errno, preparent, postparent);
	return 0;
}

int32_t
filter_rmdir (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
	int32_t ret = 0;
	inode_t *parent = loc->parent;
	if (!parent)
		parent = inode_parent (loc->inode, 0, NULL);
	ret = update_frame (frame, loc->inode, this->private);
	switch (ret) {
	case GF_FILTER_MAP_UID:
		if (parent->st_mode & S_IWGRP)
			break;
		if (loc->inode->st_mode & S_IWGRP)
			break;
	case GF_FILTER_MAP_BOTH:
		if (parent->st_mode & S_IWOTH)
			break;
		if (loc->inode->st_mode & S_IWOTH)
			break;
		gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
		STACK_UNWIND (frame, -1, EPERM, NULL, NULL);
		return 0;
	case GF_FILTER_FILTER_UID:
	case GF_FILTER_FILTER_GID:
	case GF_FILTER_RO_FS:
                STACK_UNWIND (frame, -1, EROFS, NULL, NULL);
                return 0;
	}			
	STACK_WIND (frame,
		    filter_rmdir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rmdir,
		    loc);
	return 0;
}

static int32_t
filter_symlink_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
                    struct iatt *buf,
                    struct iatt *preparent,
                    struct iatt *postparent)
{
	int ret = 0;
	if (op_ret >= 0) {
		update_stat (buf, this->private);
		ret = inode_ctx_put (inode, this, (uint64_t)(long)buf->ia_uid);
		if (ret == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"couldn't set context");
		}

		update_stat (preparent, this->private);
		update_stat (postparent, this->private);
	}
	STACK_UNWIND (frame, op_ret, op_errno, inode, buf,
                      preparent, postparent);
	return 0;
}

int32_t
filter_symlink (call_frame_t *frame,
		xlator_t *this,
		const char *linkpath,
		loc_t *loc)
{
	int ret = 0;
	inode_t *parent = loc->parent;
	ret = update_frame (frame, loc->inode, this->private);
	switch (ret) {		
	case GF_FILTER_MAP_UID:
		if (parent->st_mode & S_IWGRP)
			break;
	case GF_FILTER_MAP_BOTH:
		if (parent->st_mode & S_IWOTH)
			break;
		gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
		STACK_UNWIND (frame, -1, EPERM, NULL, NULL,
                              NULL, NULL);
		return 0;

	case GF_FILTER_FILTER_UID:
	case GF_FILTER_FILTER_GID:
	case GF_FILTER_RO_FS:
		STACK_UNWIND (frame, -1, EROFS, NULL, NULL,
                              NULL, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    filter_symlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->symlink,
		    linkpath, loc);
	return 0;
}


static int32_t
filter_rename_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct iatt *buf,
                   struct iatt *preoldparent,
                   struct iatt *postoldparent,
                   struct iatt *prenewparent,
                   struct iatt *postnewparent)
{
	if (op_ret >= 0) {
		update_stat (buf, this->private);

		update_stat (preoldparent, this->private);
		update_stat (postoldparent, this->private);

		update_stat (prenewparent, this->private);
		update_stat (postnewparent, this->private);
	}

	STACK_UNWIND (frame, op_ret, op_errno, buf,
                      preoldparent, postoldparent,
                      prenewparent, postnewparent);
	return 0;
}

int32_t
filter_rename (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *oldloc,
	       loc_t *newloc)
{
	int32_t ret = 0;
	inode_t *parent = oldloc->parent;
	if (!parent)
		parent = inode_parent (oldloc->inode, 0, NULL);
	ret = update_frame (frame, oldloc->inode, this->private);
	switch (ret) {		
	case GF_FILTER_MAP_UID:
		if (parent->st_mode & S_IWGRP)
			break;
		if (oldloc->inode->st_mode & S_IWGRP)
			break;
	case GF_FILTER_MAP_BOTH:
		if (parent->st_mode & S_IWOTH)
			break;
		if (oldloc->inode->st_mode & S_IWOTH)
			break;
		gf_log (this->name, GF_LOG_DEBUG, 
			"%s -> %s: returning permission denied", oldloc->path, newloc->path);
		STACK_UNWIND (frame, -1, EPERM, NULL,
                              NULL, NULL,
                              NULL, NULL);
		return 0;

	case GF_FILTER_FILTER_UID:
	case GF_FILTER_FILTER_GID:
	case GF_FILTER_RO_FS:
		STACK_UNWIND (frame, -1, EROFS, NULL,
                              NULL, NULL,
                              NULL, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    filter_rename_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->rename,
		    oldloc, newloc);
	return 0;
}


static int32_t
filter_link_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
                 struct iatt *buf,
                 struct iatt *preparent,
                 struct iatt *postparent)
{
	int ret = 0;
	if (op_ret >= 0) {
		update_stat (buf, this->private);
		ret = inode_ctx_put (inode, this, (uint64_t)(long)buf->ia_uid);
		if (ret == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"couldn't set context");
		}

		update_stat (preparent, this->private);
		update_stat (postparent, this->private);
	}
	STACK_UNWIND (frame, op_ret, op_errno, inode, buf,
                      preparent, postparent);
	return 0;
}

int32_t
filter_link (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *oldloc,
	     loc_t *newloc)
{
	int ret = 0;
	ret = update_frame (frame, oldloc->inode, this->private);
	switch (ret) {
	case GF_FILTER_FILTER_UID:
	case GF_FILTER_FILTER_GID:
	case GF_FILTER_RO_FS:
		STACK_UNWIND (frame, -1, EROFS, NULL, NULL,
                              NULL, NULL);
		return 0;
	}
	STACK_WIND (frame,
		    filter_link_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->link,
		    oldloc, newloc);
	return 0;
}


static int32_t
filter_create_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   fd_t *fd,
		   inode_t *inode,
		   struct iatt *buf,
                   struct iatt *preparent,
                   struct iatt *postparent)
{
	int ret = 0;
	if (op_ret >= 0) {
		update_stat (buf, this->private);
		ret = inode_ctx_put (inode, this, (uint64_t)(long)buf->ia_uid);
		if (ret == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"couldn't set context");
		}
		update_stat (preparent, this->private);
		update_stat (postparent, this->private);
	}
	STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf,
                      preparent, postparent);
	return 0;
}

int32_t
filter_create (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       int32_t flags,
	       mode_t mode, fd_t *fd)
{
	int ret = 0;
	inode_t *parent = loc->parent;
	ret = update_frame (frame, loc->inode, this->private);
	switch (ret) {		
	case GF_FILTER_MAP_UID:
		if (parent->st_mode & S_IWGRP)
			break;
	case GF_FILTER_MAP_BOTH:
		if (parent->st_mode & S_IWOTH)
			break;
		gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
		STACK_UNWIND (frame, -1, EPERM, NULL, NULL, NULL,
                              NULL, NULL);
		return 0;

	case GF_FILTER_FILTER_UID:
	case GF_FILTER_FILTER_GID:
	case GF_FILTER_RO_FS:
		STACK_UNWIND (frame, -1, EROFS, NULL, NULL, NULL,
                              NULL, NULL);
		return 0;
	}
	STACK_WIND (frame, filter_create_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->create,
		    loc, flags, mode, fd);
	return 0;
}

static int32_t
filter_open_cbk (call_frame_t *frame,
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
filter_open (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     int32_t flags, 
	     fd_t *fd,
             int32_t wbflags)
{
	int32_t ret = 0;
	ret = update_frame (frame, loc->inode, this->private);
	switch (ret) {
	case GF_FILTER_MAP_UID:
		if (loc->inode->st_mode & S_IWGRP)
			break;
		if (!(((flags & O_ACCMODE) == O_WRONLY)
                      || ((flags & O_ACCMODE) == O_RDWR))
		    && (loc->inode->st_mode & S_IRGRP))
			break;
	case GF_FILTER_MAP_BOTH:
		if (loc->inode->st_mode & S_IWOTH)
				break;
		if (!(((flags & O_ACCMODE) == O_WRONLY)
                      || ((flags & O_ACCMODE) == O_RDWR))
		    && (loc->inode->st_mode & S_IROTH))
			break;
		gf_log (this->name, GF_LOG_DEBUG, 
			"%s: returning permission denied (mode: 0%o, flag=0%o)", 
			loc->path, loc->inode->st_mode, flags);
		STACK_UNWIND (frame, -1, EPERM, fd);
		return 0;
	case GF_FILTER_FILTER_UID:
	case GF_FILTER_FILTER_GID:
	case GF_FILTER_RO_FS:
		if (!(((flags & O_ACCMODE) == O_WRONLY)
                      || ((flags & O_ACCMODE) == O_RDWR)))
			break;
		STACK_UNWIND (frame, -1, EROFS, NULL);
		return 0;
		
	}
	STACK_WIND (frame,
		    filter_open_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->open,
		    loc, flags, fd, wbflags);
	return 0;
}

static int32_t
filter_readv_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct iovec *vector,
		  int32_t count,
		  struct iatt *stbuf,
                  struct iobref *iobref)
{
	if (op_ret >= 0) {
		update_stat (stbuf, this->private);
	}
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      vector,
		      count,
		      stbuf,
                      iobref);
	return 0;
}

int32_t
filter_readv (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      size_t size,
	      off_t offset)
{
	STACK_WIND (frame,
		    filter_readv_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readv,
		    fd,
		    size,
		    offset);
	return 0;
}


static int32_t
filter_writev_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
                   struct iatt *prebuf,
		   struct iatt *postbuf)
{
	if (op_ret >= 0) {
		update_stat (prebuf, this->private);
		update_stat (postbuf, this->private);
	}
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      prebuf,
                      postbuf);
	return 0;
}

int32_t
filter_writev (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       struct iovec *vector,
	       int32_t count,
	       off_t off,
               struct iobref *iobref)
{
	int32_t ret = 0;
	ret = update_frame (frame, fd->inode, this->private);
	switch (ret) {
	case GF_FILTER_FILTER_UID:
	case GF_FILTER_FILTER_GID:
	case GF_FILTER_RO_FS:
		STACK_UNWIND (frame, -1, EROFS, NULL, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    filter_writev_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->writev,
		    fd,
		    vector,
		    count,
		    off,
                    iobref);
	return 0;
}

static int32_t
filter_fstat_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct iatt *buf)
{
	if (op_ret >= 0) {
		update_stat (buf, this->private);
	}
	STACK_UNWIND (frame,
		      op_ret,
		      op_errno,
		      buf);
	return 0;
}

int32_t
filter_fstat (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
	STACK_WIND (frame,
		    filter_fstat_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->fstat,
		    fd);
	return 0;
}

static int32_t
filter_opendir_cbk (call_frame_t *frame,
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
filter_opendir (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc, fd_t *fd)
{
	int32_t ret = 0;
	ret = update_frame (frame, loc->inode, this->private);
	switch (ret) {
	case GF_FILTER_MAP_UID:
		if (loc->inode->st_mode & S_IWGRP)
			break;
		if (loc->inode->st_mode & S_IRGRP)
			break;
	case GF_FILTER_MAP_BOTH:
		if (loc->inode->st_mode & S_IWOTH)
			break;
		if (loc->inode->st_mode & S_IROTH)
			break;
		gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
		STACK_UNWIND (frame, -1, EPERM, fd);
		return 0;
	}			
	STACK_WIND (frame,
		    filter_opendir_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->opendir,
		    loc, fd);
	return 0;
}


static int32_t
filter_setxattr_cbk (call_frame_t *frame,
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
filter_setxattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 dict_t *dict,
		 int32_t flags)
{

	int32_t ret = 0;
	ret = update_frame (frame, loc->inode, this->private);
	switch (ret) {
	case GF_FILTER_MAP_UID:
		if (loc->inode->st_mode & S_IWGRP)
			break;
	case GF_FILTER_MAP_BOTH:
		if (loc->inode->st_mode & S_IWOTH)
			break;
		gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
		STACK_UNWIND (frame, -1, EPERM);
		return 0;
	case GF_FILTER_FILTER_UID:
	case GF_FILTER_FILTER_GID:
	case GF_FILTER_RO_FS:
		STACK_UNWIND (frame, -1, EROFS);
		return 0;
	}			

	STACK_WIND (frame,
		    filter_setxattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->setxattr,
		    loc,
		    dict,
		    flags);
	return 0;
}

static int32_t
filter_getxattr_cbk (call_frame_t *frame,
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
filter_getxattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 const char *name)
{
	int32_t ret = 0;
	ret = update_frame (frame, loc->inode, this->private);
	switch (ret) {
	case GF_FILTER_MAP_UID:
		if (loc->inode->st_mode & S_IRGRP)
			break;
	case GF_FILTER_MAP_BOTH:
		if (loc->inode->st_mode & S_IROTH)
			break;
		gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
		STACK_UNWIND (frame, -1, EPERM, NULL);
		return 0;
	}			

	STACK_WIND (frame,
		    filter_getxattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->getxattr,
		    loc,
		    name);
	return 0;
}

static int32_t
filter_removexattr_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

int32_t
filter_removexattr (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc,
		    const char *name)
{
	int32_t ret = 0;
	ret = update_frame (frame, loc->inode, this->private);
	switch (ret) {
	case GF_FILTER_MAP_UID:
		if (loc->inode->st_mode & S_IWGRP)
			break;
	case GF_FILTER_MAP_BOTH:
		if (loc->inode->st_mode & S_IWOTH)
			break;
		gf_log (this->name, GF_LOG_DEBUG, "%s: returning permission denied", loc->path);
		STACK_UNWIND (frame, -1, EPERM);
		return 0;
	case GF_FILTER_FILTER_UID:
	case GF_FILTER_FILTER_GID:
	case GF_FILTER_RO_FS:
		STACK_UNWIND (frame, -1, EROFS);
		return 0;
	}			

	STACK_WIND (frame,
		    filter_removexattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->removexattr,
		    loc,
		    name);
	return 0;
}

int32_t
mem_acct_init (xlator_t *this)
{
        int     ret = -1;

        if (!this)
                return ret;

        ret = xlator_mem_acct_init (this, gf_filter_mt_end + 1);
        
        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR, "Memory accounting init"
                        "failed");
                return ret;
        }

        return ret;
}

int32_t 
init (xlator_t *this)
{
	char *value = NULL;
	char *tmp_str = NULL;
	char *tmp_str1 = NULL;
	char *tmp_str2 = NULL;
	char *dup_str = NULL;
	char *input_value_str1 = NULL;
	char *input_value_str2 = NULL;
	char *output_value_str = NULL;
	int32_t input_value = 0;
	int32_t output_value = 0;
	data_t *option_data = NULL;
	struct gf_filter *filter = NULL;
	gf_boolean_t tmp_bool = 0;

	if (!this->children || this->children->next) {
		gf_log (this->name,
			GF_LOG_ERROR,
			"translator not configured with exactly one child");
		return -1;
	}
	
	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

	filter = GF_CALLOC (sizeof (*filter), 1, gf_filter_mt_gf_filter);
	ERR_ABORT (filter);
	
	if (dict_get (this->options, "read-only")) {
		value = data_to_str (dict_get (this->options, "read-only"));
		if (gf_string2boolean (value, &filter->complete_read_only) == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"wrong value provided for 'read-only'");
			return -1;
		}
	}

	if (dict_get (this->options, "root-squashing")) {
		value = data_to_str (dict_get (this->options, "root-squashing"));
		if (gf_string2boolean (value, &tmp_bool) == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"wrong value provided for 'root-squashing'");
			return -1;
		}
		if (tmp_bool) {
			filter->translate_num_uid_entries = 1;
			filter->translate_num_gid_entries = 1;
			filter->translate_input_uid[0][0] = GF_FILTER_ROOT_UID; /* root */
			filter->translate_input_uid[0][1] = GF_FILTER_ROOT_UID; /* root */
			filter->translate_input_gid[0][0] = GF_FILTER_ROOT_GID; /* root */
			filter->translate_input_gid[0][1] = GF_FILTER_ROOT_GID; /* root */
			filter->translate_output_uid[0] = GF_FILTER_NOBODY_UID;
			filter->translate_output_gid[0] = GF_FILTER_NOBODY_GID;
		}
	}

	if (dict_get (this->options, "translate-uid")) {
		option_data = dict_get (this->options, "translate-uid");
		value = strtok_r (option_data->data, ",", &tmp_str);
		while (value) {
			dup_str = gf_strdup (value);
			input_value_str1 = strtok_r (dup_str, "=", &tmp_str1);
			if (input_value_str1) {
				/* Check for n-m */
				char *temp_string = gf_strdup (input_value_str1);
				input_value_str2 = strtok_r (temp_string, "-", &tmp_str2);
				if (gf_string2int (input_value_str2, &input_value) != 0) {
					gf_log (this->name, GF_LOG_ERROR, 
						"invalid number format \"%s\"", 
						input_value_str2);
					return -1;
				}
				filter->translate_input_uid[filter->translate_num_uid_entries][0] = input_value;
				input_value_str2 = strtok_r (NULL, "-", &tmp_str2);
				if (input_value_str2) {
					if (gf_string2int (input_value_str2, &input_value) != 0) {
						gf_log (this->name, GF_LOG_ERROR, 
							"invalid number format \"%s\"", 
							input_value_str2);
						return -1;
					}
				}
				filter->translate_input_uid[filter->translate_num_uid_entries][1] = input_value;
				GF_FREE (temp_string);
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
			filter->translate_output_uid[filter->translate_num_uid_entries]   = output_value;
			gf_log (this->name, 
				GF_LOG_DEBUG, 
				"pair %d: input uid '%d' will be changed to uid '%d'", 
				filter->translate_num_uid_entries, input_value, output_value);

			filter->translate_num_uid_entries++;
			if (filter->translate_num_uid_entries == GF_MAXIMUM_FILTERING_ALLOWED)
				break;
			value = strtok_r (NULL, ",", &tmp_str);
			GF_FREE (dup_str);
		}
	}

	tmp_str1 = NULL;
	tmp_str2 = NULL;
	tmp_str  = NULL;

	if (dict_get (this->options, "translate-gid")) {
		option_data = dict_get (this->options, "translate-gid");
		value = strtok_r (option_data->data, ",", &tmp_str);
		while (value) {
			dup_str = gf_strdup (value);
			input_value_str1 = strtok_r (dup_str, "=", &tmp_str1);
			if (input_value_str1) {
				/* Check for n-m */
				char *temp_string = gf_strdup (input_value_str1);
				input_value_str2 = strtok_r (temp_string, "-", &tmp_str2);
				if (gf_string2int (input_value_str2, &input_value) != 0) {
					gf_log (this->name, GF_LOG_ERROR, 
						"invalid number format \"%s\"", 
						input_value_str2);
					return -1;
				}
				filter->translate_input_gid[filter->translate_num_gid_entries][0] = input_value;
				input_value_str2 = strtok_r (NULL, "-", &tmp_str2);
				if (input_value_str2) {
					if (gf_string2int (input_value_str2, &input_value) != 0) {
						gf_log (this->name, GF_LOG_ERROR, 
							"invalid number format \"%s\"", 
							input_value_str2);
						return -1;
					}
				}
				filter->translate_input_gid[filter->translate_num_gid_entries][1] = input_value;
				GF_FREE (temp_string);
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
						"translate-gid value not valid");
					return -1;
				}
			} else {
				gf_log (this->name, GF_LOG_ERROR, 
					"translate-gid value not valid");
				return -1;
			}
			
			filter->translate_output_gid[filter->translate_num_gid_entries] = output_value;
			
			gf_log (this->name, GF_LOG_DEBUG, 
				"pair %d: input gid '%d' will be changed to gid '%d'", 
				filter->translate_num_gid_entries, input_value, output_value);
			
			filter->translate_num_gid_entries++;
			if (filter->translate_num_gid_entries == GF_MAXIMUM_FILTERING_ALLOWED)
				break;
			value = strtok_r (NULL, ",", &tmp_str);
			GF_FREE (dup_str);
		}
	}

	tmp_str  = NULL;
	tmp_str1 = NULL;

	if (dict_get (this->options, "filter-uid")) {
		option_data = dict_get (this->options, "filter-uid");
		value = strtok_r (option_data->data, ",", &tmp_str);
		while (value) {
			dup_str = gf_strdup (value);
			/* Check for n-m */
			input_value_str1 = strtok_r (dup_str, "-", &tmp_str1);
			if (gf_string2int (input_value_str1, &input_value) != 0) {
				gf_log (this->name, GF_LOG_ERROR, 
					"invalid number format \"%s\"", 
					input_value_str1);
				return -1;
			}
			filter->filter_input_uid[filter->filter_num_uid_entries][0] = input_value;
			input_value_str1 = strtok_r (NULL, "-", &tmp_str1);
			if (input_value_str1) {
				if (gf_string2int (input_value_str1, &input_value) != 0) {
					gf_log (this->name, GF_LOG_ERROR, 
						"invalid number format \"%s\"", 
						input_value_str1);
					return -1;
				}
			}
			filter->filter_input_uid[filter->filter_num_uid_entries][1] = input_value;

			gf_log (this->name, 
				GF_LOG_DEBUG, 
				"filter [%d]: input uid(s) '%s' will be filtered", 
				filter->filter_num_uid_entries, dup_str);
			
			filter->filter_num_uid_entries++;
			if (filter->filter_num_uid_entries == GF_MAXIMUM_FILTERING_ALLOWED)
				break;
			value = strtok_r (NULL, ",", &tmp_str);
			GF_FREE (dup_str);
		}
		filter->partial_filter = 1;
	}

	tmp_str  = NULL;
	tmp_str1 = NULL;

	if (dict_get (this->options, "filter-gid")) {
		option_data = dict_get (this->options, "filter-gid");
		value = strtok_r (option_data->data, ",", &tmp_str);
		while (value) {
			dup_str = gf_strdup (value);
			/* Check for n-m */
			input_value_str1 = strtok_r (dup_str, "-", &tmp_str1);
			if (gf_string2int (input_value_str1, &input_value) != 0) {
				gf_log (this->name, GF_LOG_ERROR, 
					"invalid number format \"%s\"", 
					input_value_str1);
				return -1;
			}
			filter->filter_input_gid[filter->filter_num_gid_entries][0] = input_value;
			input_value_str1 = strtok_r (NULL, "-", &tmp_str1);
			if (input_value_str1) {
				if (gf_string2int (input_value_str1, &input_value) != 0) {
					gf_log (this->name, GF_LOG_ERROR, 
						"invalid number format \"%s\"", 
						input_value_str1);
					return -1;
				}
			}
			filter->filter_input_gid[filter->filter_num_gid_entries][1] = input_value;

			gf_log (this->name, 
				GF_LOG_DEBUG, 
				"filter [%d]: input gid(s) '%s' will be filtered", 
				filter->filter_num_gid_entries, dup_str);
			
			filter->filter_num_gid_entries++;
			if (filter->filter_num_gid_entries == GF_MAXIMUM_FILTERING_ALLOWED)
				break;
			value = strtok_r (NULL, ",", &tmp_str);
			GF_FREE (dup_str);
		}
		gf_log (this->name, GF_LOG_ERROR, "this option is not supported currently.. exiting");
		return -1;
		filter->partial_filter = 1;
	}

	if (dict_get (this->options, "fixed-uid")) {
		option_data = dict_get (this->options, "fixed-uid");
		if (gf_string2int (option_data->data, &input_value) != 0) {
			gf_log (this->name, GF_LOG_ERROR, 
				"invalid number format \"%s\"", 
				option_data->data);
			return -1;
		}
		filter->fixed_uid = input_value;
		filter->fixed_uid_set = 1;
	}

	if (dict_get (this->options, "fixed-gid")) {
		option_data = dict_get (this->options, "fixed-gid");
		if (gf_string2int (option_data->data, &input_value) != 0) {
			gf_log (this->name, GF_LOG_ERROR, 
				"invalid number format \"%s\"", 
				option_data->data);
			return -1;
		}
		filter->fixed_gid = input_value;
		filter->fixed_gid_set = 1;
	}

	this->private = filter;
	return 0;
}


void
fini (xlator_t *this)
{
	struct gf_filter *filter = this->private;

	GF_FREE (filter);

	return;
}


struct xlator_fops fops = {
	.lookup      = filter_lookup,
	.stat        = filter_stat,
	.fstat       = filter_fstat,
	.readlink    = filter_readlink,
	.mknod       = filter_mknod,
	.mkdir       = filter_mkdir,
	.unlink      = filter_unlink,
	.rmdir       = filter_rmdir,
	.symlink     = filter_symlink,
	.rename      = filter_rename,
	.link        = filter_link,
	.truncate    = filter_truncate,
	.ftruncate   = filter_ftruncate,
	.create      = filter_create,
	.open        = filter_open,
	.readv       = filter_readv,
	.writev      = filter_writev,
	.setxattr    = filter_setxattr,
	.getxattr    = filter_getxattr,
	.removexattr = filter_removexattr,
	.opendir     = filter_opendir,
        .setattr     = filter_setattr,
        .fsetattr    = filter_fsetattr,
};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
	{ .key  = { "root-squashing" }, 
	  .type = GF_OPTION_TYPE_BOOL 
	},
	{ .key  = { "read-only" }, 
	  .type = GF_OPTION_TYPE_BOOL
	},
	{ .key  = { "fixed-uid" },  
	  .type = GF_OPTION_TYPE_INT
	},
	{ .key  = { "fixed-gid" },  
	  .type = GF_OPTION_TYPE_INT
	},
	{ .key  = { "translate-uid" },  
	  .type = GF_OPTION_TYPE_ANY 
	},
	{ .key  = { "translate-gid" },  
	  .type = GF_OPTION_TYPE_ANY
	},
	{ .key  = { "filter-uid" },  
	  .type = GF_OPTION_TYPE_ANY 
	},
	{ .key  = { "filter-gid" },  
	  .type = GF_OPTION_TYPE_ANY 
	},
	{ .key = {NULL} },
};
