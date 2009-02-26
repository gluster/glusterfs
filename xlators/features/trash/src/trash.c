/*
  Copyright (c) 2006-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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


#include "glusterfs.h"
#include "logging.h"
#include "dict.h"
#include "xlator.h"
#include "defaults.h"

#include <libgen.h>

/* TODO: currently it can work only above posix, no other translators 
 *       between them. Not a good thing. Try making more reliable methods.
 */

struct trash_struct {
	inode_t *inode;
	loc_t loc1;
	loc_t loc2;
	char origpath[ZR_PATH_MAX];
	char newpath[ZR_PATH_MAX];
	char oldpath[ZR_PATH_MAX]; // used only in case of rename
};
typedef struct trash_struct trash_local_t;

struct trash_priv {
	char trash_dir[ZR_PATH_MAX];
};
typedef struct trash_priv trash_private_t;

int32_t
trash_unlink_rename_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct stat *buf);
int32_t
trash_rename_rename_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct stat *buf);

/**
 * trash_common_unwind_cbk -
 */
int32_t
trash_common_unwind_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno)
{
	trash_local_t *local = frame->local;

	if (local->loc1.path)
		loc_wipe (&local->loc1);
	
	if (local->loc2.path)
		loc_wipe (&local->loc2);

	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

/**
 * trash_common_unwind_buf_cbk -
 */
int32_t
trash_common_unwind_buf_cbk (call_frame_t *frame,
			     void *cookie,
			     xlator_t *this,
			     int32_t op_ret,
			     int32_t op_errno,
			     struct stat *buf)
{
	trash_local_t *local = frame->local;

	if (local->loc1.path)
		loc_wipe (&local->loc1);
	
	if (local->loc2.path)
		loc_wipe (&local->loc2);

	STACK_UNWIND (frame, op_ret, op_errno, buf);
	return 0;
}

int32_t
trash_mkdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *stbuf)
{
	trash_local_t *local = frame->local;
	char *tmp_str = strdup (local->newpath);
	int32_t count = 0;
	char *tmp_path = NULL;
	char *tmp_dirname = NULL;

	if (op_ret == -1 && op_errno == ENOENT) {
		tmp_dirname = strchr (tmp_str, '/');
		while (tmp_dirname) {
			count = tmp_dirname - tmp_str;
			if (count == 0)
				count = 1;
			tmp_path = CALLOC (1, count + 1);
			ERR_ABORT (tmp_path);
			memcpy (tmp_path, local->newpath, count);
			loc_t tmp_loc = {
				.inode = NULL,
				.path = tmp_path,
			};

			/* TODO:create the directory with proper permissions */
			STACK_WIND_COOKIE (frame,
					   trash_mkdir_cbk,
					   tmp_path,
					   this->children->xlator,
					   this->children->xlator->fops->mkdir,
					   &tmp_loc,
					   0777);
			tmp_dirname = strchr (tmp_str + count + 1, '/');
		}
		free (cookie);
		free (tmp_str);
		return 0;
	}
	char *dir_name = dirname (tmp_str);
	if (strcmp((char*)cookie, dir_name) == 0) {
		loc_t new_loc = {
			.inode = NULL,
			.path = local->newpath
		};
		STACK_WIND (frame,
			    trash_unlink_rename_cbk,
			    this->children->xlator,
			    this->children->xlator->fops->rename,
			    &local->loc2,
			    &new_loc);

	}
	free (cookie); /* strdup (dir_name) was sent here :) */
	free (tmp_str);
	return 0;
}

/**
 * trash_unlink_rename_cbk -
 */
int32_t
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
		char *tmp_str = strdup (local->newpath);
		char *dir_name = dirname (tmp_str);
		loc_t tmp_loc = {
			.inode = NULL,
			.path = dir_name,
		};
		/* TODO: create the directory with proper permissions */
		STACK_WIND_COOKIE (frame,
				   trash_mkdir_cbk,
				   strdup (dir_name),
				   this->children->xlator,
				   this->children->xlator->fops->mkdir,
				   &tmp_loc,
				   0777);
		free (tmp_str);
	} else if (op_ret == -1 && op_errno == ENOTDIR) {
		gf_log (this->name, GF_LOG_WARNING,
			"Target exists, cannot keep the copy, deleting");
		STACK_WIND (frame,
			    trash_common_unwind_cbk,
			    this->children->xlator,
			    this->children->xlator->fops->unlink,
			    &local->loc2);
	} else if (op_ret == -1 && op_errno == EISDIR) {
		gf_log (this->name, GF_LOG_WARNING,
			"Target exists as a directory, cannot keep the copy, "
			"deleting");
		STACK_WIND (frame,
			    trash_common_unwind_cbk,
			    this->children->xlator,
			    this->children->xlator->fops->unlink,
			    &local->loc2);
	} else {
		/* */
		STACK_UNWIND (frame, 0, op_errno);
	}

	return 0;
}


/**
 * trash_unlink -
 */
int32_t
trash_unlink (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
	trash_private_t *priv = this->private;
	trash_local_t *local = NULL;
	time_t       utime = 0;
	struct tm   *tm = NULL;
	char         timestr[256];

	if (strncmp (loc->path, priv->trash_dir, 
		     strlen(priv->trash_dir)) == 0) {
		/* Trying to rename from the trash can dir, do the
		   actual unlink */
		STACK_WIND (frame,
			    trash_common_unwind_cbk,
			    this->children->xlator,
			    this->children->xlator->fops->unlink,
			    loc);
	} else {
		local = CALLOC (1, sizeof (trash_local_t));
		if (!local) {
			STACK_UNWIND (frame, -1, ENOMEM);
			return 0;
		}
		frame->local = local;
		
		loc_copy (&local->loc2, loc);

		strcpy (local->newpath, priv->trash_dir);
		strcat (local->newpath, loc->path);

		utime = time (NULL);
		tm    = localtime (&utime);
		strftime (timestr, 256, ".%Y%m%d%H%M%S", tm); 
		strcat (local->newpath, timestr);

		{
			loc_t new_loc = {
				.inode = NULL,
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

/* */
int32_t
trash_rename_mkdir_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			inode_t *inode,
			struct stat *stbuf)
{
	trash_local_t *local = frame->local;
	char *tmp_str = strdup (local->newpath);

	if (op_ret == -1 && op_errno == ENOENT) {
		int32_t count = 0;
		char *tmp_path = NULL;
		char *tmp_dirname = strchr (tmp_str, '/');

		while (tmp_dirname) {
			count = tmp_dirname - tmp_str;
			if (count == 0)
				count = 1;
			tmp_path = CALLOC (1, count + 2);
			ERR_ABORT (tmp_path);
			memcpy (tmp_path, local->newpath, count);
			loc_t tmp_loc = {
				.inode = NULL,
				.path = tmp_path,
			};

			/* TODO:create the directory with proper permissions */
			STACK_WIND_COOKIE (frame,
					   trash_rename_mkdir_cbk,
					   tmp_path,
					   this->children->xlator,
					   this->children->xlator->fops->mkdir,
					   &tmp_loc,
					   0777);
			tmp_dirname = strchr (tmp_str + count + 1, '/');
		}
		free (cookie);
		free (tmp_str);
		return 0;
	}
	char *dir_name = dirname (tmp_str);
	if (strcmp((char*)cookie, dir_name) == 0) {
		loc_t new_loc = {
			.inode = NULL,
			.path = local->newpath
		};
		STACK_WIND (frame,
			    trash_rename_rename_cbk,
			    this->children->xlator,
			    this->children->xlator->fops->rename,
			    &local->loc2,
			    &new_loc);

	}
	free (cookie); /* strdup (dir_name) was sent here :) */
	free (tmp_str);
	return 0;
}


/**
 * trash_unlink_rename_cbk -
 */
int32_t
trash_rename_rename_cbk (call_frame_t *frame,
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
		char *tmp_str = strdup (local->newpath);
		char *dir_name = dirname (tmp_str);
		loc_t tmp_loc = {
			.inode = NULL,
			.path = dir_name,
		};
		/* TODO: create the directory with proper permissions */
		STACK_WIND_COOKIE (frame,
				   trash_rename_mkdir_cbk,
				   strdup (dir_name),
				   this->children->xlator,
				   this->children->xlator->fops->mkdir,
				   &tmp_loc,
				   0777);
		free (tmp_str);
		return 0;
	} else if (op_ret == -1 && op_errno == ENOTDIR) {
		gf_log (this->name, GF_LOG_WARNING,
			"Target exists, cannot keep the dest entry %s, "
			"renaming",
			local->loc2.path);
	} else if (op_ret == -1 && op_errno == EISDIR) {
		gf_log (this->name, GF_LOG_WARNING,
			"Target exists as a directory, cannot keep the "
			"copy %s, renaming",
			local->loc2.path);
	}
	loc_t new_loc = {
		.inode = NULL,
		.parent = local->loc2.parent,
		.path = local->loc2.path,
	};
	STACK_WIND (frame,
		    trash_common_unwind_buf_cbk,
		    this->children->xlator,
		    this->children->xlator->fops->rename,
		    &local->loc1,
		    &new_loc);

	return 0;
}

/**
 * trash_rename_lookup_cbk -
 */
int32_t
trash_rename_lookup_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 inode_t *inode,
			 struct stat *buf,
			 dict_t *xattr)
{
	trash_local_t *local = frame->local;

	if (op_ret == -1) {
		STACK_WIND (frame,
			    trash_common_unwind_buf_cbk,
			    this->children->xlator,
			    this->children->xlator->fops->rename,
			    &local->loc1,
			    &local->loc2);
		return 0;
	}

	loc_t oldloc = {
		.parent = local->loc2.parent,
		.inode = inode,
		.path = local->loc2.path,
	};
	loc_t newloc = {
		.inode = NULL,
		.path = local->newpath
	};
	STACK_WIND (frame,
		    trash_rename_rename_cbk,
		    this->children->xlator,
		    this->children->xlator->fops->rename,
		    &oldloc,
		    &newloc);

	return 0;
}


/**
 * trash_rename -
 */
int32_t
trash_rename (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *oldloc,
	      loc_t *newloc)
{
	trash_private_t *priv = this->private;
	trash_local_t *local = NULL;
	time_t       utime = 0;
	struct tm   *tm = NULL;
	char         timestr[256];

	if (strncmp (oldloc->path, priv->trash_dir, 
		     strlen(priv->trash_dir)) == 0) {
		/* Trying to rename from the trash can dir, 
		   do the actual rename */
		STACK_WIND (frame,
			    trash_common_unwind_buf_cbk,
			    this->children->xlator,
			    this->children->xlator->fops->rename,
			    oldloc,
			    newloc);
	} else {
		/* Trying to rename a regular file from GlusterFS */
		local = CALLOC (1, sizeof (trash_local_t));
		if (!local) {
			STACK_UNWIND (frame, -1, ENOMEM, NULL);
			return 0;
		}
		frame->local = local;
		loc_copy (&local->loc1, oldloc);
		loc_copy (&local->loc2, newloc);

		strcpy (local->newpath, priv->trash_dir);
		strcat (local->newpath, newloc->path);

		utime = time (NULL);
		tm    = localtime (&utime);
		strftime (timestr, 256, ".%Y%m%d%H%M%S", tm); 
		strcat (local->newpath, timestr);

		/* Send a lookup call on newloc, to ensure we are not 
		   overwriting */
		STACK_WIND (frame,
			    trash_rename_lookup_cbk,
			    this->children->xlator,
			    this->children->xlator->fops->lookup,
			    newloc,
			    0);
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
	xlator_list_t *trav = NULL;
	trash_private_t *_priv = NULL;

	/* Create .trashcan directory in init */
	if (!this->children || this->children->next) {
		gf_log (this->name, GF_LOG_ERROR,
			"not configured with exactly one child. exiting");
		return -1;
	}

	if (!this->parents) {
		gf_log (this->name, GF_LOG_WARNING,
			"dangling volume. check volfile ");
	}

	trav = this->children;
	while (trav->xlator->children)
		trav = trav->xlator->children;

	if (strncmp ("storage/", trav->xlator->type, 8))
	{
		gf_log (this->name, GF_LOG_ERROR,
			"'trash' translator not loaded over storage "
			"translator, not a supported setup");
		return -1;
	}

	_priv = CALLOC (1, sizeof (*_priv));
	ERR_ABORT (_priv);

	trash_dir = dict_get (this->options, "trash-dir");
	if (!trash_dir) {
		gf_log (this->name, GF_LOG_WARNING,
			"no option specified for 'trash-dir', "
			"using \"/.trashcan/\"");
		strcpy (_priv->trash_dir, "/.trashcan");
	} else {
		/* Need a path with '/' as the first char, if not 
		   given, append it */
		if (trash_dir->data[0] == '/') {
			strcpy (_priv->trash_dir, trash_dir->data);
		} else {
			strcpy (_priv->trash_dir, "/");
			strcat (_priv->trash_dir, trash_dir->data);
		}
	}

	this->private = (void *)_priv;
	return 0;
}

void
fini (xlator_t *this)
{
	trash_private_t *priv = this->private;
	FREE (priv);
	return;
}


struct xlator_fops fops = {
	.unlink = trash_unlink,
	.rename = trash_rename,
};

struct xlator_mops mops = {

};

struct xlator_cbks cbks = {
};

struct volume_options options[] = {
	{ .key  = { "trash-dir" }, 
	  .type = GF_OPTION_TYPE_PATH 
	},
	{ .key  = {NULL} },
};
