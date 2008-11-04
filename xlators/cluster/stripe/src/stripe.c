/*
  Copyright (c) 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

/**
 * xlators/cluster/stripe:
 *    Stripe translator, stripes the data accross its child nodes, 
 *    as per the options given in the spec file. The striping works 
 *    fairly simple. It writes files at different offset as per 
 *    calculation. So, 'ls -l' output at the real posix level will 
 *    show file size bigger than the actual size. But when one does 
 *    'df' or 'du <file>', real size of the file on the server is shown.
 *
 * WARNING:
 *    Stripe translator can't regenerate data if a child node gets disconnected.
 *    So, no 'self-heal' for stripe. Hence the advice, use stripe only when its 
 *    very much necessary, or else, use it in combination with AFR, to have a 
 *    backup copy. 
 */

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "logging.h"
#include "defaults.h"
#include "compat.h"
#include "compat-errno.h"
#include <fnmatch.h>
#include <signal.h>

#define STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR(_loc) do { \
  if (!(_loc && _loc->inode)) {                             \
	  TRAP_ON (!(_loc && _loc->inode))                  \
    STACK_UNWIND (frame, -1, EINVAL, NULL, NULL, NULL);     \
    return 0;                                               \
  }                                                         \
} while(0)

struct stripe_local;

/**
 * Private structure for stripe translator 
 */
struct stripe_private {
	xlator_t **xl_array;
	uint64_t   block_size;
	gf_lock_t  lock;
	uint8_t    nodes_down;
	int8_t     first_child_down;
	int8_t     child_count;
	int8_t     state[256];       /* Current state of the child node, 0 for down, 1 for up */
	gf_boolean_t  xattr_supported;  /* 0 for no, 1 for yes, default yes */
	uint8_t       xattr_supported_option_given; /* 0 of no, 1 for yes */
};

/**
 * Used to keep info about the replies received from fops->readv calls 
 */
struct readv_replies {
	struct iovec *vector;
	int32_t       count; //count of vector
	int32_t       op_ret;   //op_ret of readv
	int32_t       op_errno;
	struct stat   stbuf; /* 'stbuf' is also a part of reply */
};

/**
 * Local structure to be passed with all the frames in case of STACK_WIND
 */
struct stripe_local {
	struct stripe_local *next;
	call_frame_t        *orig_frame; //

	/* Used by _cbk functions */
 	struct stat          stbuf;
	struct readv_replies *replies;
	struct statvfs       statvfs_buf;
	dir_entry_t         *entry;
	struct xlator_stats  stats;

	int8_t               revalidate;
	int8_t               failed;
	int8_t               unwind;

	int32_t              node_index;
	int32_t              call_count;
	int32_t              wind_count; // used instead of child_cound in case of read and write */
	int32_t              op_ret;
	int32_t              op_errno; 
	int32_t              count;
	int32_t              flags;
	char                *name;
	inode_t             *inode;

	loc_t                loc;
	loc_t                loc2;

	/* For File I/O fops */
	dict_t              *dict;

	/* General usage */
	off_t                offset;
	off_t                stripe_size;

	int8_t              *list;
	struct flock         lock;
	fd_t                *fd;
	void                *value;
};

typedef struct stripe_local   stripe_local_t;
typedef struct stripe_private stripe_private_t;

/*
 * stripe_common_cbk -
 */
int32_t
stripe_common_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

/**
 * stripe_stack_unwind_cbk -  This function is used for all the _cbk without 
 *     any extra arguments (other than the minimum given)
 * This is called from functions like fsync,unlink,rmdir etc.
 *
 */
int32_t 
stripe_stack_unwind_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno)
{
	int32_t         callcnt = 0;
	stripe_local_t *local   = frame->local;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;

		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_WARNING, "%s returned %s",
				((call_frame_t *)cookie)->this->name, strerror (op_errno));			
			local->op_errno = op_errno;
			if (op_errno == ENOTCONN) 
				local->failed = 1;
		}
		if (op_ret >= 0) 
			local->op_ret = op_ret;
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		if (local->failed)
			local->op_ret = -1;

		if (local->loc.path)
			loc_wipe (&local->loc);
		if (local->loc2.path)
			loc_wipe (&local->loc2);

		STACK_UNWIND (frame, local->op_ret, local->op_errno);
	}
	return 0;
}

int32_t 
stripe_common_buf_cbk (call_frame_t *frame,
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
 * stripe_stack_unwind_buf_cbk -  This function is used for all the _cbk with 
 *    'struct stat *buf' as extra argument (other than minimum)
 * This is called from functions like, chmod,fchmod,chown,fchown,truncate,ftruncate,
 *   utimens etc.
 *
 * @cookie - this argument should be always 'xlator_t *' of child node 
 */
int32_t 
stripe_stack_unwind_buf_cbk (call_frame_t *frame,
			     void *cookie,
			     xlator_t *this,
			     int32_t op_ret,
			     int32_t op_errno,
			     struct stat *buf)
{
	int32_t callcnt = 0;
	stripe_local_t *local = frame->local;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;

		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_WARNING, "%s returned error %s",
				((call_frame_t *)cookie)->this->name, strerror (op_errno));
			local->op_errno = op_errno;
			if (op_errno == ENOTCONN)
				local->failed = 1;
		}
    
		if (op_ret == 0) {
			local->op_ret = 0;
			if (local->stbuf.st_blksize == 0) {
				local->stbuf = *buf;
				/* Because st_blocks gets added again */
				local->stbuf.st_blocks = 0;
			}

			if (FIRST_CHILD(this) == ((call_frame_t *)cookie)->this) {
				/* Always, pass the inode number of first child to the above layer */
				local->stbuf.st_ino = buf->st_ino;
				local->stbuf.st_mtime = buf->st_mtime;
			}

			local->stbuf.st_blocks += buf->st_blocks;
			if (local->stbuf.st_size < buf->st_size)
				local->stbuf.st_size = buf->st_size;
			if (local->stbuf.st_blksize != buf->st_blksize) {
				/* TODO: add to blocks in terms of original block size */
			}
		}
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		if (local->failed)
			local->op_ret = -1;

		if (local->loc.path)
			loc_wipe (&local->loc);
		if (local->loc2.path)
			loc_wipe (&local->loc2);

		STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
	}

	return 0;
}

/* In case of symlink, mknod, the file is created on just first node */
int32_t 
stripe_common_inode_cbk (call_frame_t *frame,
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

/**
 * stripe_stack_unwind_inode_cbk - This is called by the function like, 
 *                   link (), symlink (), mkdir (), mknod () 
 *           This creates a inode for new inode. It keeps a list of all 
 *           the inodes received from the child nodes. It is used while 
 *           forwarding any fops to child nodes.
 *
 */
int32_t 
stripe_stack_unwind_inode_cbk (call_frame_t *frame,
			       void *cookie,
			       xlator_t *this,
			       int32_t op_ret,
			       int32_t op_errno,
			       inode_t *inode,
			       struct stat *buf)
{
	int32_t callcnt = 0;
	stripe_local_t *local = frame->local;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
    
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_WARNING, "%s returned error %s",
				((call_frame_t *)cookie)->this->name, strerror (op_errno));
			local->op_errno = op_errno;
			if (op_errno == ENOTCONN)
				local->failed = 1;
		}
 
		if (op_ret >= 0) {
			local->op_ret = 0;

			if (local->stbuf.st_blksize == 0) {
				local->inode = inode;
				local->stbuf = *buf;
				/* Because st_blocks gets added again */
				local->stbuf.st_blocks = 0;
			}
			if (FIRST_CHILD(this) == ((call_frame_t *)cookie)->this) {
				local->stbuf.st_ino = buf->st_ino;
				local->stbuf.st_mtime = buf->st_mtime;
			}

			local->stbuf.st_blocks += buf->st_blocks;
			if (local->stbuf.st_size < buf->st_size)
				local->stbuf.st_size = buf->st_size;
			if (local->stbuf.st_blksize != buf->st_blksize) {
				/* TODO: add to blocks in terms of original block size */
			}
		}
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		if (local->failed)
			local->op_ret = -1;

		STACK_UNWIND (frame, local->op_ret, local->op_errno, 
			      local->inode, &local->stbuf);
	}

	return 0;
}

int32_t 
stripe_stack_unwind_inode_lookup_cbk (call_frame_t *frame,
				      void *cookie,
				      xlator_t *this,
				      int32_t op_ret,
				      int32_t op_errno,
				      inode_t *inode,
				      struct stat *buf,
				      dict_t *dict)
{
	int32_t callcnt = 0;
	dict_t *tmp_dict = NULL;
	stripe_local_t *local = frame->local;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
    
		if (op_ret == -1) {
			if (op_errno != ENOENT)
				gf_log (this->name, GF_LOG_WARNING, "%s returned error %s",
					((call_frame_t *)cookie)->this->name, strerror (op_errno));
			local->op_errno = op_errno;
			if (op_errno == ENOTCONN)
				local->failed = 1;
		}
 
		if (op_ret >= 0) {
			local->op_ret = 0;

			if (local->stbuf.st_blksize == 0) {
				local->inode = inode;
				local->stbuf = *buf;
				/* Because st_blocks gets added again */
				local->stbuf.st_blocks = 0;
			}
			if (FIRST_CHILD(this) == ((call_frame_t *)cookie)->this) {
				local->stbuf.st_ino = buf->st_ino;
				local->stbuf.st_mtime = buf->st_mtime;
				if (local->dict)
					dict_unref (local->dict);
				local->dict = dict_ref (dict);
			} else {
				if (!local->dict)
					local->dict = dict_ref (dict);
			}
			local->stbuf.st_blocks += buf->st_blocks;
			if (local->stbuf.st_size < buf->st_size)
				local->stbuf.st_size = buf->st_size;
			if (local->stbuf.st_blksize != buf->st_blksize) {
				/* TODO: add to blocks in terms of original block size */
			}
		}
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		if (local->failed)
			local->op_ret = -1;

		tmp_dict = local->dict;
		STACK_UNWIND (frame, local->op_ret, local->op_errno, 
			      local->inode, &local->stbuf, local->dict);
		if (tmp_dict)
			dict_unref (tmp_dict);
	}

	return 0;
}


/**
 * stripe_lookup -
 */
int32_t 
stripe_lookup (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       int32_t need_xattr)
{
	stripe_local_t *local = NULL;
	xlator_list_t *trav = NULL;
	stripe_private_t *priv = this->private;
	char send_lookup_to_all = 0;

	if (!(loc && loc->inode)) {
		gf_log (this->name, GF_LOG_ERROR, "wrong argument, returning EINVAL");
		STACK_UNWIND (frame, -1, EINVAL, NULL, NULL, NULL);
		return 0;
	}

	/* Initialization */
	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	local->op_ret = -1;
	frame->local = local;

	if ((!loc->inode->st_mode) || S_ISDIR (loc->inode->st_mode) || S_ISREG (loc->inode->st_mode))
		send_lookup_to_all = 1;

	if (send_lookup_to_all) {
		/* Everytime in stripe lookup, all child nodes should be looked up */
		local->call_count = priv->child_count;
		trav = this->children;
		while (trav) {
			STACK_WIND (frame,
				    stripe_stack_unwind_inode_lookup_cbk,
				    trav->xlator,
				    trav->xlator->fops->lookup,
				    loc, need_xattr);
			trav = trav->next;
		}
	} else {
		local->call_count = 1;
		
		STACK_WIND (frame,
			    stripe_stack_unwind_inode_lookup_cbk,
			    FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->lookup,
			    loc, need_xattr);
	}
  
	return 0;
}

/**
 * stripe_stat -
 */
int32_t
stripe_stat (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{ 
	int send_lookup_to_all = 0;
	xlator_list_t *trav = NULL;
	stripe_local_t *local = NULL;
	stripe_private_t *priv = this->private;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	if (S_ISDIR (loc->inode->st_mode) || S_ISREG (loc->inode->st_mode))
		send_lookup_to_all = 1;

	if (!send_lookup_to_all) {
		STACK_WIND (frame,
			    stripe_common_buf_cbk,
			    FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->stat,
			    loc);
	} else {
		/* Initialization */
		local = calloc (1, sizeof (stripe_local_t));
		ERR_ABORT (local);
		local->op_ret = -1;
		frame->local = local;
		local->inode = loc->inode;
		local->call_count = priv->child_count;
    
		trav = this->children;
		while (trav) {
			STACK_WIND (frame,
				    stripe_stack_unwind_buf_cbk,
				    trav->xlator,
				    trav->xlator->fops->stat,
				    loc);
			trav = trav->next;
		}
	}
	return 0;
}


/**
 * stripe_chmod -
 */
int32_t
stripe_chmod (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      mode_t mode)
{
	int send_fop_to_all = 0;
	xlator_list_t *trav = NULL;
	stripe_local_t *local = NULL;
	stripe_private_t *priv = this->private;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	if (priv->first_child_down) {
		gf_log (this->name, GF_LOG_WARNING, "First node down, returning ENOTCONN");
		STACK_UNWIND (frame, -1, ENOTCONN, NULL);
		return 0;
	}

	if (S_ISDIR (loc->inode->st_mode) || S_ISREG (loc->inode->st_mode))
		send_fop_to_all = 1;

	if (!send_fop_to_all) {
		STACK_WIND (frame,
			    stripe_common_buf_cbk,
			    FIRST_CHILD(this),
			    FIRST_CHILD(this)->fops->chmod,
			    loc, mode);
	} else {
		/* Initialization */
		local = calloc (1, sizeof (stripe_local_t));
		ERR_ABORT (local);
		local->op_ret = -1;
		frame->local = local;
		local->inode = loc->inode;
		local->call_count = priv->child_count;

		trav = this->children;
		while (trav) {
			STACK_WIND (frame,
				    stripe_stack_unwind_buf_cbk,
				    trav->xlator,
				    trav->xlator->fops->chmod,
				    loc, mode);
			trav = trav->next;
		}
	}
	return 0;
}


/**
 * stripe_chown - 
 */
int32_t
stripe_chown (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      uid_t uid,
	      gid_t gid)
{
	int send_fop_to_all = 0;
	xlator_list_t *trav = NULL;
	stripe_local_t *local = NULL;
	stripe_private_t *priv = this->private;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	if (priv->first_child_down) {
		gf_log (this->name, GF_LOG_WARNING, "First node down, returning ENOTCONN");
		STACK_UNWIND (frame, -1, ENOTCONN, NULL);
		return 0;
	}

	if (S_ISDIR (loc->inode->st_mode) || S_ISREG (loc->inode->st_mode))
		send_fop_to_all = 1;

	trav = this->children;
	if (!send_fop_to_all) {
		STACK_WIND (frame,
			    stripe_common_buf_cbk,
			    trav->xlator,
			    trav->xlator->fops->chown,
			    loc, uid, gid);
	} else {
		/* Initialization */
		local = calloc (1, sizeof (stripe_local_t));
		ERR_ABORT (local);
		local->op_ret = -1;
		frame->local = local;
		local->inode = loc->inode;
		local->call_count = priv->child_count;

		trav = this->children;
		while (trav) {
			STACK_WIND (frame,
				    stripe_stack_unwind_buf_cbk,
				    trav->xlator,
				    trav->xlator->fops->chown,
				    loc, uid, gid);
			trav = trav->next;
		}
	}

	return 0;
}


/**
 * stripe_statfs_cbk - 
 */
int32_t
stripe_statfs_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct statvfs *stbuf)
{
	stripe_local_t *local = (stripe_local_t *)frame->local;
	int32_t callcnt;
	LOCK(&frame->lock);
	{
		callcnt = --local->call_count;

		if (op_ret != 0 && op_errno != ENOTCONN) {
			local->op_errno = op_errno;
		}
		if (op_ret == 0) {
      			struct statvfs *dict_buf = &local->statvfs_buf;
			dict_buf->f_bsize   = stbuf->f_bsize;
			dict_buf->f_frsize  = stbuf->f_frsize;
			dict_buf->f_blocks += stbuf->f_blocks;
			dict_buf->f_bfree  += stbuf->f_bfree;
			dict_buf->f_bavail += stbuf->f_bavail;
			dict_buf->f_files  += stbuf->f_files;
			dict_buf->f_ffree  += stbuf->f_ffree;
			dict_buf->f_favail += stbuf->f_favail;
			dict_buf->f_fsid    = stbuf->f_fsid;
			dict_buf->f_flag    = stbuf->f_flag;
			dict_buf->f_namemax = stbuf->f_namemax;
			local->op_ret = 0;
		}
	}
	UNLOCK (&frame->lock);
  
	if (!callcnt) {
		STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->statvfs_buf);
	}
  
	return 0;
}


/**
 * stripe_statfs - 
 */
int32_t
stripe_statfs (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc)
{
	stripe_local_t *local = NULL;
	xlator_list_t *trav = this->children;

	/* Initialization */
	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	local->op_ret = -1;
	local->op_errno = ENOTCONN;
	frame->local = local;

	local->call_count = ((stripe_private_t *)this->private)->child_count;
	while (trav) {
		STACK_WIND (frame,
			    stripe_statfs_cbk,
			    trav->xlator,
			    trav->xlator->fops->statfs,
			    loc);
		trav = trav->next;
	}

	return 0;
}


/**
 * stripe_truncate - 
 */
int32_t
stripe_truncate (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 off_t offset)
{
	int send_fop_to_all = 0;
	stripe_local_t *local = NULL;
	stripe_private_t *priv = this->private;
	xlator_list_t *trav = this->children;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	if (priv->first_child_down) {
		gf_log (this->name, GF_LOG_WARNING, "First node down, returning ENOTCONN");
		STACK_UNWIND (frame, -1, ENOTCONN, NULL);
		return 0;
	}

	if (S_ISDIR (loc->inode->st_mode) || S_ISREG (loc->inode->st_mode))
		send_fop_to_all = 1;

	if (!send_fop_to_all) {
		STACK_WIND (frame,
			    stripe_common_buf_cbk,
			    trav->xlator,
			    trav->xlator->fops->truncate,
			    loc,
			    offset);
	} else {
		/* Initialization */
		local = calloc (1, sizeof (stripe_local_t));
		ERR_ABORT (local);
		local->op_ret = -1;
		frame->local = local;
		local->inode = loc->inode;
		local->call_count = priv->child_count;
    
		while (trav) {
			STACK_WIND (frame,
				    stripe_stack_unwind_buf_cbk,
				    trav->xlator,
				    trav->xlator->fops->truncate,
				    loc,
				    offset);
			trav = trav->next;
		}
	}

	return 0;
}


/**
 * stripe_utimens - 
 */
int32_t 
stripe_utimens (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		struct timespec tv[2])
{
	int send_fop_to_all = 0;
	stripe_local_t *local = NULL;
	stripe_private_t *priv = this->private;
	xlator_list_t *trav = this->children;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	if (priv->first_child_down) {
		gf_log (this->name, GF_LOG_WARNING, "First node down, returning ENOTCONN");
		STACK_UNWIND (frame, -1, ENOTCONN, NULL);
		return 0;
	}

	if (S_ISDIR (loc->inode->st_mode) || S_ISREG (loc->inode->st_mode))
		send_fop_to_all = 1;

	if (!send_fop_to_all) {
		STACK_WIND (frame,
			    stripe_common_buf_cbk,
			    trav->xlator,
			    trav->xlator->fops->utimens,
			    loc, tv);
	} else {
		/* Initialization */
		local = calloc (1, sizeof (stripe_local_t));
		ERR_ABORT (local);
		local->op_ret = -1;
		frame->local = local;
		local->inode = loc->inode;
		local->call_count = priv->child_count;
    
		while (trav) {
			STACK_WIND (frame,
				    stripe_stack_unwind_buf_cbk,
				    trav->xlator,
				    trav->xlator->fops->utimens,
				    loc, tv);
			trav = trav->next;
		}
	}
	return 0;
}


int32_t 
stripe_first_rename_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct stat *buf)
{
	stripe_local_t *local = frame->local;
	xlator_list_t *trav = this->children;

	if (op_ret == -1) 
	{
		STACK_UNWIND (frame, op_ret, op_errno, buf);
		return 0;
	}

	local->op_ret = 0;
	local->stbuf = *buf;
	local->call_count--;
	trav = trav->next; /* Skip first child */

	while (trav) {
		STACK_WIND (frame,
			    stripe_stack_unwind_buf_cbk,
			    trav->xlator,
			    trav->xlator->fops->rename,
			    &local->loc, &local->loc2);
		trav = trav->next;
	}

	return 0;
}
/**
 * stripe_rename - 
 */
int32_t
stripe_rename (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *oldloc,
	       loc_t *newloc)
{
	stripe_private_t *priv = this->private;
	stripe_local_t *local = NULL;
	xlator_list_t *trav = this->children;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (oldloc);

	if (priv->first_child_down) {
		gf_log (this->name, GF_LOG_WARNING, "First node down, returning ENOTCONN");
		STACK_UNWIND (frame, -1, EIO, NULL);
		return 0;
	}

	/* Initialization */
	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	local->op_ret = -1;
	local->inode = oldloc->inode;
	loc_copy (&local->loc, oldloc);
	loc_copy (&local->loc2, newloc);

	local->call_count = priv->child_count;
  
	frame->local = local;

	STACK_WIND (frame,
		    stripe_first_rename_cbk,
		    trav->xlator,
		    trav->xlator->fops->rename,
		    oldloc, newloc);

	return 0;
}


/**
 * stripe_access - 
 */
int32_t
stripe_access (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       int32_t mask)
{
	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	STACK_WIND (frame,
		    stripe_common_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->access,
		    loc, mask);

	return 0;
}


/**
 * stripe_readlink_cbk - 
 */
int32_t 
stripe_readlink_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     const char *path)
{
	STACK_UNWIND (frame, op_ret, op_errno, path);

	return 0;
}


/**
 * stripe_readlink - 
 */
int32_t
stripe_readlink (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 size_t size)
{
	stripe_private_t *priv = this->private;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	if (priv->first_child_down) {
		gf_log (this->name, GF_LOG_WARNING, "First node down, returning ENOTCONN");
		STACK_UNWIND (frame, -1, ENOTCONN, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    stripe_readlink_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->readlink,
		    loc, size);

	return 0;
}


/**
 * stripe_unlink - 
 */
int32_t
stripe_unlink (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc)
{
	int send_fop_to_all = 0;
	stripe_local_t *local = NULL;
	stripe_private_t *priv = this->private;
	xlator_list_t *trav = this->children;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	if (priv->first_child_down) {
		gf_log (this->name, GF_LOG_WARNING, "First node down, returning EIO");
		STACK_UNWIND (frame, -1, EIO);
		return 0;
	}
 
	if (S_ISDIR (loc->inode->st_mode) || S_ISREG (loc->inode->st_mode))
		send_fop_to_all = 1;

	if (!send_fop_to_all) {
		STACK_WIND (frame,
			    stripe_common_cbk,
			    trav->xlator,
			    trav->xlator->fops->unlink,
			    loc);
	} else {
		/* Initialization */
		local = calloc (1, sizeof (stripe_local_t));
		ERR_ABORT (local);
		local->op_ret = -1;
		frame->local = local;
		local->call_count = priv->child_count;
    
		while (trav) {
			STACK_WIND (frame,
				    stripe_stack_unwind_cbk,
				    trav->xlator,
				    trav->xlator->fops->unlink,
				    loc);
			trav = trav->next;
		}
	}

	return 0;
}


int32_t 
stripe_first_rmdir_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
	xlator_list_t *trav = this->children;
	stripe_local_t *local = frame->local;

	if (op_ret == -1) 
	{
		STACK_UNWIND (frame, op_ret, op_errno);
		return 0;
	}

	local->call_count--; /* First child successful */
	trav = trav->next; /* Skip first child */

	while (trav) {
		STACK_WIND (frame,
			    stripe_stack_unwind_cbk,
			    trav->xlator,
			    trav->xlator->fops->rmdir,
			    &local->loc);
		trav = trav->next;
	}

	return 0;
}

/**
 * stripe_rmdir - 
 */
int32_t
stripe_rmdir (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
	stripe_local_t *local = NULL;
	xlator_list_t *trav = this->children;
	stripe_private_t *priv = this->private;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	if (priv->first_child_down) {
		gf_log (this->name, GF_LOG_WARNING, "First node down, returning EIO");
		STACK_UNWIND (frame, -1, EIO);
		return 0;
	}

	/* Initialization */
	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	local->op_ret = -1;
	frame->local = local;
	local->inode = loc->inode;
	loc_copy (&local->loc, loc);
	local->call_count = priv->child_count;
  
	STACK_WIND (frame,
		    stripe_first_rmdir_cbk,
		    trav->xlator,
		    trav->xlator->fops->rmdir,
		    loc);

	return 0;
}


/**
 * stripe_setxattr - 
 */
int32_t
stripe_setxattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 dict_t *dict,
		 int32_t flags)
{
	stripe_private_t *priv = this->private;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	if (priv->first_child_down) {
		gf_log (this->name, GF_LOG_WARNING, "First node down, returning ENOTCONN");
		STACK_UNWIND (frame, -1, ENOTCONN);
		return 0;
	}

	STACK_WIND (frame,
		    stripe_common_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->setxattr,
		    loc, dict, flags);

	return 0;
}


int32_t 
stripe_mknod_ifreg_fail_unlink_cbk (call_frame_t *frame,
				    void *cookie,
				    xlator_t *this,
				    int32_t op_ret,
				    int32_t op_errno)
{
	int32_t callcnt = 0;
	stripe_local_t *local = frame->local;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		loc_wipe (&local->loc);
		STACK_UNWIND (frame, local->op_ret, local->op_errno, 
			      local->inode, &local->stbuf);
	}

	return 0;
}


/**
 */
int32_t
stripe_mknod_ifreg_setxattr_cbk (call_frame_t *frame,
				 void *cookie,
				 xlator_t *this,
				 int32_t op_ret,
				 int32_t op_errno)
{
	int32_t callcnt = 0;
	stripe_local_t *local = frame->local;
	stripe_private_t *priv = this->private;
	xlator_list_t *trav = this->children;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
    
		if ((op_ret == -1) && (op_errno == ENOTSUP)) {
			if (!priv->xattr_supported_option_given) {
				priv->xattr_supported = 0;
				op_ret = 0;
				gf_log (this->name, GF_LOG_CRITICAL, 
					"seems like extended attribute not supported, "
					"falling back to no extended attribute mode");
			}
		}

		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_WARNING, "%s returned error %s",
				((call_frame_t *)cookie)->this->name, strerror (op_errno));
			local->op_ret = -1;
			local->op_errno = op_errno;
		}
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		if (local->op_ret == -1) {
			local->call_count = priv->child_count;
			while (trav) {
				STACK_WIND (frame,
					    stripe_mknod_ifreg_fail_unlink_cbk,
					    trav->xlator,
					    trav->xlator->fops->unlink,
					    &local->loc);
				trav = trav->next;
			}
			return 0;
		}

		loc_wipe (&local->loc);
		STACK_UNWIND (frame, local->op_ret, local->op_errno, 
			      local->inode, &local->stbuf);
	}
	return 0;
}

/**
 */
int32_t
stripe_mknod_ifreg_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			inode_t *inode,
			struct stat *buf)
{
	int32_t callcnt = 0;
	stripe_local_t *local = frame->local;
	stripe_private_t *priv = this->private;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
    
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_WARNING, "%s returned error %s",
				((call_frame_t *)cookie)->this->name, strerror (op_errno));
			local->failed = 1;
			local->op_errno = op_errno;
		}
    
		if (op_ret >= 0) {
			local->op_ret = op_ret;
			/* Get the mapping in inode private */
			/* Get the stat buf right */
			if (local->stbuf.st_blksize == 0) {
				local->stbuf = *buf;
				/* Because st_blocks gets added again */
				local->stbuf.st_blocks = 0;
			}

			/* Always, pass the inode number of first child to the above layer */
			if (FIRST_CHILD(this) == ((call_frame_t *)cookie)->this)
				local->stbuf.st_ino = buf->st_ino;
      
			local->stbuf.st_blocks += buf->st_blocks;
			if (local->stbuf.st_size < buf->st_size)
				local->stbuf.st_size = buf->st_size;
			if (local->stbuf.st_blksize != buf->st_blksize) {
				/* TODO: add to blocks in terms of original block size */
			}
		}
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		if (local->failed) 
			local->op_ret = -1;

		if ((local->op_ret != -1) && priv->xattr_supported) {
			/* Send a setxattr request to nodes where the files are created */
			int32_t index = 0;
			char size_key[256] = {0,};
			char index_key[256] = {0,};
			char count_key[256] = {0,};
			xlator_list_t *trav = this->children;
			dict_t *dict = get_new_dict ();

			sprintf (size_key, "trusted.%s.stripe-size", this->name);
			sprintf (count_key, "trusted.%s.stripe-count", this->name);
			sprintf (index_key, "trusted.%s.stripe-index", this->name);

			dict_set (dict, size_key, data_from_int64 (local->stripe_size));
			dict_set (dict, count_key, data_from_int32 (local->call_count));

			local->call_count = priv->child_count;
	
			dict_ref (dict);

			while (trav) {
				dict_set (dict, index_key, data_from_int32 (index));

				STACK_WIND (frame,
					    stripe_mknod_ifreg_setxattr_cbk,
					    trav->xlator,
					    trav->xlator->fops->setxattr,
					    &local->loc, dict, 0);
	
				index++;
				trav = trav->next;
			}
			dict_unref (dict);
		} else {
			/* Create itself has failed.. so return without setxattring */
			loc_wipe (&local->loc);
			STACK_UNWIND (frame, local->op_ret, local->op_errno, 
				      local->inode, &local->stbuf);
		}
	}
  
	return 0;
}


/**
 * stripe_mknod - 
 */
int32_t
stripe_mknod (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      mode_t mode,
	      dev_t rdev)
{
	stripe_private_t *priv = this->private;
  
	if (priv->first_child_down) {
		gf_log (this->name, GF_LOG_WARNING, "First node down, returning EIO");
		STACK_UNWIND (frame, -1, EIO, NULL, NULL);
		return 0;
	}

	if (S_ISREG(mode)) {
		/* NOTE: on older kernels (older than 2.6.9), creat() fops is sent as 
		   mknod() + open(). Hence handling S_IFREG files is necessary */

		stripe_local_t *local = NULL;
		xlator_list_t *trav = NULL;
		
		if (priv->nodes_down) {
			gf_log (this->name, GF_LOG_WARNING, "Some node down, returning EIO");
			STACK_UNWIND (frame, -1, EIO, loc->inode, NULL);
			return 0;
		}
		
		/* Initialization */
		local = calloc (1, sizeof (stripe_local_t));
		ERR_ABORT (local);
		local->op_ret = -1;
		local->op_errno = ENOTCONN;
		local->stripe_size = priv->block_size;
		frame->local = local;
		local->inode = loc->inode;
		loc_copy (&local->loc, loc);

		/* Everytime in stripe lookup, all child nodes should be looked up */
		local->call_count = ((stripe_private_t *)this->private)->child_count;
		
		trav = this->children;
		while (trav) {
			STACK_WIND (frame,
				    stripe_mknod_ifreg_cbk,
				    trav->xlator,
				    trav->xlator->fops->mknod,
				    loc, mode, rdev);
			trav = trav->next;
		}

		/* This case is handled, no need to continue further. */
		return 0; 
	}


	STACK_WIND (frame,
		    stripe_common_inode_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->mknod,
		    loc, mode, rdev);

	return 0;
}


/**
 * stripe_mkdir - 
 */
int32_t
stripe_mkdir (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      mode_t mode)
{
	stripe_private_t *priv = this->private;
	stripe_local_t *local = NULL;
	xlator_list_t *trav = NULL;
  
	if (priv->first_child_down) {
		gf_log (this->name, GF_LOG_WARNING, "First node down, returning EIO");
		STACK_UNWIND (frame, -1, EIO, NULL, NULL);
		return 0;
	}

	/* Initialization */
	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	local->op_ret = -1;
	local->call_count = priv->child_count;
	frame->local = local;

	/* Everytime in stripe lookup, all child nodes should be looked up */
	trav = this->children;
	while (trav) {
		STACK_WIND (frame,
			    stripe_stack_unwind_inode_cbk,
			    trav->xlator,
			    trav->xlator->fops->mkdir,
			    loc, mode);
		trav = trav->next;
	}

	return 0;
}


/**
 * stripe_symlink - 
 */
int32_t
stripe_symlink (call_frame_t *frame,
		xlator_t *this,
		const char *linkpath,
		loc_t *loc)
{
	stripe_private_t *priv = this->private;
  
	if (priv->first_child_down) {
		gf_log (this->name, GF_LOG_WARNING, "First node down, returning EIO");
		STACK_UNWIND (frame, -1, EIO, NULL, NULL);
		return 0;
	}

	/* send symlink to only first node */
	STACK_WIND (frame,
		    stripe_common_inode_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->symlink,
		    linkpath, loc);

	return 0;
}

/**
 * stripe_link -
 */
int32_t
stripe_link (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *oldloc,
	     loc_t *newloc)
{
	int send_fop_to_all = 0;
	stripe_private_t *priv = this->private;
	stripe_local_t *local = NULL;
	xlator_list_t *trav = this->children;
  
	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (oldloc);
	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (newloc);

	if (priv->first_child_down) {
		gf_log (this->name, GF_LOG_WARNING, "First node down, returning EIO");
		STACK_UNWIND (frame, -1, EIO, NULL, NULL);
		return 0;
	}


	if (S_ISREG (oldloc->inode->st_mode))
		send_fop_to_all = 1;

	if (!send_fop_to_all) {
		STACK_WIND (frame,
			    stripe_common_inode_cbk,
			    trav->xlator,
			    trav->xlator->fops->link,
			    oldloc, newloc);
	} else {
		/* Initialization */
		local = calloc (1, sizeof (stripe_local_t));
		ERR_ABORT (local);
		local->op_ret = -1;
		frame->local = local;
		local->call_count = priv->child_count;

		/* Everytime in stripe lookup, all child nodes should be looked up */
		while (trav) {
			STACK_WIND (frame,
				    stripe_stack_unwind_inode_cbk,
				    trav->xlator,
				    trav->xlator->fops->link,
				    oldloc, newloc);
			trav = trav->next;
		}
	}

	return 0;
}

int32_t 
stripe_create_fail_unlink_cbk (call_frame_t *frame,
			       void *cookie,
			       xlator_t *this,
			       int32_t op_ret,
			       int32_t op_errno)
{
	int32_t callcnt = 0;
	fd_t *lfd = NULL;
	stripe_local_t *local = frame->local;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		lfd = local->fd;
		loc_wipe (&local->loc);
		STACK_UNWIND (frame, local->op_ret, local->op_errno, 
			      local->fd, local->inode, &local->stbuf);
		fd_unref (lfd);
	}
	return 0;
}


/**
 * stripe_create_setxattr_cbk - 
 */
int32_t
stripe_create_setxattr_cbk (call_frame_t *frame,
			    void *cookie,
			    xlator_t *this,
			    int32_t op_ret,
			    int32_t op_errno)
{
	fd_t *lfd = NULL;
	int32_t callcnt = 0;
	stripe_local_t *local = frame->local;
	stripe_private_t *priv = this->private;
	xlator_list_t *trav = this->children;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
    
		if ((op_ret == -1) && (op_errno == ENOTSUP)) {
			if (!priv->xattr_supported_option_given) {
				priv->xattr_supported = 0;
				op_ret = 0;
				gf_log (this->name, GF_LOG_CRITICAL, 
					"seems like extended attribute not supported, "
					"falling back to no extended attribute mode");
			}
		}

		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_WARNING, "%s returned error %s",
				((call_frame_t *)cookie)->this->name, strerror (op_errno));
			local->op_ret = -1;
			local->op_errno = op_errno;
		}
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		if (local->op_ret == -1) {
			local->call_count = priv->child_count;
			while (trav) {
				STACK_WIND (frame,
					    stripe_create_fail_unlink_cbk,
					    trav->xlator,
					    trav->xlator->fops->unlink,
					    &local->loc);
				trav = trav->next;
			}
	
			return 0;
		}

		lfd = local->fd;
		loc_wipe (&local->loc);
		STACK_UNWIND (frame, local->op_ret, local->op_errno,
			      local->fd, local->inode, &local->stbuf);
		fd_unref (lfd);
	}

	return 0;
}

/**
 * stripe_create_cbk - 
 */
int32_t
stripe_create_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   fd_t *fd,
		   inode_t *inode,
		   struct stat *buf)
{
	int32_t callcnt = 0;
	stripe_local_t *local = frame->local;
	stripe_private_t *priv = this->private;
	fd_t *lfd = NULL;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
    
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_WARNING, "%s returned error %s",
				((call_frame_t *)cookie)->this->name, strerror (op_errno));
			local->failed = 1;
			local->op_errno = op_errno;
		}
    
		if (op_ret >= 0) {
			local->op_ret = op_ret;
			/* Get the mapping in inode private */
			/* Get the stat buf right */
			if (local->stbuf.st_blksize == 0) {
				local->stbuf = *buf;
				/* Because st_blocks gets added again */
				local->stbuf.st_blocks = 0;
			}
      
			/* Always, pass the inode number of first child to the above layer */
			if (FIRST_CHILD(this) == ((call_frame_t *)cookie)->this)
				local->stbuf.st_ino = buf->st_ino;
      
			local->stbuf.st_blocks += buf->st_blocks;
			if (local->stbuf.st_size < buf->st_size)
				local->stbuf.st_size = buf->st_size;
			if (local->stbuf.st_blksize != buf->st_blksize) {
				/* TODO: add to blocks in terms of original block size */
			}
		}
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		if (local->failed)
			local->op_ret = -1;

		if (local->op_ret >= 0) {
			dict_set (local->fd->ctx,  this->name, 
				  data_from_uint64 (local->stripe_size));
		}

		if ((local->op_ret != -1) && local->stripe_size && priv->xattr_supported) {
			/* Send a setxattr request to nodes where the files are created */
			int32_t index = 0;
			char size_key[256] = {0,};
			char index_key[256] = {0,};
			char count_key[256] = {0,};
			xlator_list_t *trav = this->children;
			dict_t *dict = get_new_dict ();

			sprintf (size_key, "trusted.%s.stripe-size", this->name);
			sprintf (count_key, "trusted.%s.stripe-count", this->name);
			sprintf (index_key, "trusted.%s.stripe-index", this->name);

			dict_set (dict, size_key, data_from_int64 (local->stripe_size));
			dict_set (dict, count_key, data_from_int32 (local->call_count));

			local->call_count = priv->child_count;
	
			dict_ref (dict);
			while (trav) {
				dict_set (dict, index_key, data_from_int32 (index));
	
				STACK_WIND (frame,
					    stripe_create_setxattr_cbk,
					    trav->xlator,
					    trav->xlator->fops->setxattr,
					    &local->loc,
					    dict,
					    0);
	
				index++;
				trav = trav->next;
			}
			dict_unref (dict);
		} else {
			/* Create itself has failed.. so return without setxattring */
			lfd = local->fd;
			loc_wipe (&local->loc);
			STACK_UNWIND (frame, local->op_ret, local->op_errno, 
				      local->fd, local->inode, &local->stbuf);
      
			fd_unref (lfd);
		}
	}
  
	return 0;
}


/**
 * stripe_create - If a block-size is specified for the 'name', create the 
 *    file in all the child nodes. If not, create it in only first child.
 *
 * @name- complete path of the file to be created.
 */
int32_t
stripe_create (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       int32_t flags,
	       mode_t mode,
	       fd_t *fd)
{
	stripe_private_t *priv = this->private;
	stripe_local_t *local = NULL;
	xlator_list_t *trav = NULL;

	/* files created in O_APPEND mode does not allow lseek() on fd */
	flags &= ~O_APPEND;

	if (priv->first_child_down || priv->nodes_down) {
		gf_log (this->name, GF_LOG_WARNING, "First node down, returning EIO");
		STACK_UNWIND (frame, -1, EIO, fd, loc->inode, NULL);
		return 0;
	}

	/* Initialization */
	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	local->op_ret = -1;
	local->op_errno = ENOTCONN;
	local->stripe_size = priv->block_size;
	frame->local = local;
	local->inode = loc->inode;
	loc_copy (&local->loc, loc);
	local->fd = fd_ref (fd);

	local->call_count = ((stripe_private_t *)this->private)->child_count;
	
	trav = this->children;
	while (trav) {
		STACK_WIND (frame,
			    stripe_create_cbk,
			    trav->xlator,
			    trav->xlator->fops->create,
			    loc, flags, mode, fd);
		trav = trav->next;
	}
       
	return 0;
}

/**
 * stripe_open_cbk - 
 */
int32_t
stripe_open_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 fd_t *fd)
{
	int32_t callcnt = 0;
	stripe_local_t *local = frame->local;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;

		if (op_ret == -1) {
			local->failed = 1;
			gf_log (this->name, GF_LOG_WARNING, "%s returned error %s",
				((call_frame_t *)cookie)->this->name, strerror (op_errno));
			local->op_ret = -1;
			local->op_errno = op_errno;
		}
    
		if (op_ret >= 0)
			local->op_ret = op_ret;
	}
	UNLOCK (&frame->lock);
  
	if (!callcnt) {
		if (local->failed)
			local->op_ret = -1;

		if (local->op_ret >= 0) {
			dict_set (local->fd->ctx,  this->name, 
				  data_from_uint64 (local->stripe_size));
		}
		loc_wipe (&local->loc);
		STACK_UNWIND (frame, local->op_ret, local->op_errno, fd);
	}

	return 0;
}


/**
 * stripe_getxattr_cbk - 
 */
int32_t
stripe_open_getxattr_cbk (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  dict_t *dict)
{
	int32_t callcnt = 0;
	stripe_local_t *local = frame->local;
	xlator_list_t *trav = this->children;
	stripe_private_t *priv = this->private;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;

		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_WARNING, "%s returned error %s",
				((call_frame_t *)cookie)->this->name, strerror (op_errno));
			local->op_ret = -1;
			local->op_errno = op_errno;
			if (op_errno == ENOTCONN)
				local->failed = 1;
			if (op_errno == ENOTSUP) {
				if (!priv->xattr_supported_option_given) {
					priv->xattr_supported = 0;
					gf_log (this->name, GF_LOG_CRITICAL, 
						"seems like extended attribute not supported, "
						"falling back to no extended attribute mode");
				}
			}
		}
	}
	UNLOCK (&frame->lock);
  
	if (!callcnt) {
		if (!local->failed && (local->op_ret != -1)) {
			/* If getxattr doesn't fails, call open */
			char size_key[256] = {0,};
			data_t *stripe_size_data = NULL;

			sprintf (size_key, "trusted.%s.stripe-size", this->name);
			stripe_size_data = dict_get (dict, size_key);

			if (stripe_size_data) {
				local->stripe_size = data_to_int64 (stripe_size_data);
				if (local->stripe_size != priv->block_size) {
					gf_log (this->name, GF_LOG_WARNING,
						"file(%s) is having different block-size", local->loc.path);
				}
			} else {
				/* if the file was created using earlier versions of stripe */
				gf_log (this->name, GF_LOG_CRITICAL,
					"[CRITICAL] Seems like file(%s) created using earlier version",
					local->loc.path);
			}
		}
    
		local->call_count = priv->child_count;

		while (trav) {
			STACK_WIND (frame,
				    stripe_open_cbk,
				    trav->xlator,
				    trav->xlator->fops->open,
				    &local->loc, local->flags, local->fd);
			trav = trav->next;
		}
	}

	return 0;
}

/**
 * stripe_open - 
 */
int32_t
stripe_open (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     int32_t flags,
	     fd_t *fd)
{
	stripe_local_t *local = NULL;
	stripe_private_t *priv = this->private;
	xlator_list_t *trav = this->children;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);
  
	/* files opened in O_APPEND mode does not allow lseek() on fd */
	flags &= ~O_APPEND;

	if (priv->first_child_down) {
		gf_log (this->name, GF_LOG_WARNING, "First node down, returning ENOTCONN");
		STACK_UNWIND (frame, -1, ENOTCONN, NULL);
		return 0;
	}

	/* Initialization */
	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	local->fd = fd;
	frame->local = local;
	local->inode = loc->inode;
	loc_copy (&local->loc, loc);

	/* Striped files */
	local->flags = flags;
	local->call_count = priv->child_count;

	if (priv->xattr_supported) {
		while (trav) {
			STACK_WIND (frame,
				    stripe_open_getxattr_cbk,
				    trav->xlator,
				    trav->xlator->fops->getxattr,
				    loc, NULL);
			trav = trav->next;
		}
	} else {
		local->stripe_size = priv->block_size;
		while (trav) {
			STACK_WIND (frame,
				    stripe_open_cbk,
				    trav->xlator,
				    trav->xlator->fops->open,
				    &local->loc, local->flags, local->fd);
			trav = trav->next;
		}
	}

	return 0;
}

/**
 * stripe_opendir_cbk - 
 */
int32_t
stripe_opendir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    fd_t *fd)
{
	int32_t callcnt = 0;
	stripe_local_t *local = frame->local;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;

		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_WARNING, "%s returned error %s",
				((call_frame_t *)cookie)->this->name, strerror (op_errno));
			local->op_ret = -1;
			local->failed = 1;
			local->op_errno = op_errno;
		}
    
		if (op_ret >= 0) 
			local->op_ret = op_ret;
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		STACK_UNWIND (frame, local->op_ret, local->op_errno, local->fd);
	}

	return 0;
}


/**
 * stripe_opendir - 
 */
int32_t
stripe_opendir (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		fd_t *fd)
{
	stripe_local_t *local = NULL;
	stripe_private_t *priv = this->private;
	xlator_list_t *trav = this->children;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	if (priv->first_child_down) {
		gf_log (this->name, GF_LOG_WARNING, "First node down, returning EIO");
		STACK_UNWIND (frame, -1, EIO, NULL);
		return 0;
	}

	/* Initialization */
	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	frame->local = local;
	local->inode = loc->inode;
	local->fd = fd;
	local->call_count = priv->child_count;

	while (trav) {
		STACK_WIND (frame,
			    stripe_opendir_cbk,
			    trav->xlator,
			    trav->xlator->fops->opendir,
			    loc, fd);
		trav = trav->next;
	}
  
	return 0;
}


/**
 * stripe_getxattr_cbk - 
 */
int32_t
stripe_getxattr_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     dict_t *value)
{
	STACK_UNWIND (frame, op_ret, op_errno, value);
	return 0;
}


/**
 * stripe_getxattr - 
 */
int32_t
stripe_getxattr (call_frame_t *frame,
		 xlator_t *this,
		 loc_t *loc,
		 const char *name)
{
	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	STACK_WIND (frame,
		    stripe_getxattr_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->getxattr,
		    loc, name);

	return 0;
}

/**
 * stripe_removexattr - 
 */
int32_t
stripe_removexattr (call_frame_t *frame,
		    xlator_t *this,
		    loc_t *loc,
		    const char *name)
{
	stripe_private_t *priv = this->private;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	if (priv->first_child_down) {
		gf_log (this->name, GF_LOG_WARNING, "First node down, returning ENOTCONN");
		STACK_UNWIND (frame, -1, ENOTCONN, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    stripe_common_cbk,
		    FIRST_CHILD(this),
		    FIRST_CHILD(this)->fops->removexattr,
		    loc, name);

	return 0;
}


/**
 * stripe_lk_cbk - 
 */
int32_t
stripe_lk_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct flock *lock)
{
	int32_t callcnt = 0;
	stripe_local_t *local = frame->local;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_WARNING, "%s returned error %s",
				((call_frame_t *)cookie)->this->name, strerror (op_errno));
			local->op_errno = op_errno;
			if (op_errno == ENOTCONN)
				local->failed = 1;
		}
		if (op_ret == 0 && local->op_ret == -1) {
			/* First successful call, copy the *lock */
			local->op_ret = 0;
			local->lock = *lock;
		}
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		if (local->failed)
			local->op_ret = -1;
		STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->lock);
	}
	return 0;
}


/**
 * stripe_lk - 
 */
int32_t
stripe_lk (call_frame_t *frame,
	   xlator_t *this,
	   fd_t *fd,
	   int32_t cmd,
	   struct flock *lock)
{
	stripe_local_t *local = NULL;
	xlator_list_t *trav = this->children;
	stripe_private_t *priv = this->private;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (fd);
	/* Initialization */
	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	local->op_ret = -1;
	frame->local = local;
  
	local->call_count = priv->child_count;
	
	while (trav) {
		STACK_WIND (frame,	      
			    stripe_lk_cbk,
			    trav->xlator,
			    trav->xlator->fops->lk,
			    fd, cmd, lock);
		trav = trav->next;
	}

	return 0;
}

/**
 * stripe_writedir - 
 */
int32_t
stripe_setdents (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 int32_t flags,
		 dir_entry_t *entries,
		 int32_t count)
{
	stripe_local_t *local = NULL;
	stripe_private_t *priv = this->private;
	xlator_list_t *trav = this->children;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (fd);

	/* Initialization */
	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	local->op_ret = -1;
	frame->local = local;
	local->call_count = priv->child_count;

	while (trav) {
		STACK_WIND (frame,	      
			    stripe_stack_unwind_cbk,
			    trav->xlator,
			    trav->xlator->fops->setdents,
			    fd, flags, entries, count);
		trav = trav->next;
	}

	return 0;
}


/**
 * stripe_flush - 
 */
int32_t
stripe_flush (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
	stripe_local_t *local = NULL;
	stripe_private_t *priv = this->private;
	xlator_list_t *trav = this->children;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (fd);

	/* Initialization */
	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	local->op_ret = -1;
	frame->local = local;
	local->call_count = priv->child_count;
	
	while (trav) {
		STACK_WIND (frame,	      
			    stripe_stack_unwind_cbk,
			    trav->xlator,
			    trav->xlator->fops->flush,
			    fd);
		trav = trav->next;
	}

	return 0;
}


/**
 * stripe_close - 
 */
int32_t
stripe_release (xlator_t *this,
		fd_t *fd)
{
	return 0;
}


/**
 * stripe_fsync - 
 */
int32_t
stripe_fsync (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      int32_t flags)
{
	stripe_local_t *local = NULL;
	stripe_private_t *priv = this->private;
	xlator_list_t *trav = this->children;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (fd);

	/* Initialization */
	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	local->op_ret = -1;
	frame->local = local;
	local->call_count = priv->child_count;
	
	while (trav) {
		STACK_WIND (frame,	      
			    stripe_stack_unwind_cbk,
			    trav->xlator,
			    trav->xlator->fops->fsync,
			    fd, flags);
		trav = trav->next;
	}

	return 0;
}


/**
 * stripe_fstat - 
 */
int32_t
stripe_fstat (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd)
{
	stripe_local_t *local = NULL;
	stripe_private_t *priv = this->private;
	xlator_list_t *trav = this->children;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (fd);

	/* Initialization */
	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	local->op_ret = -1;
	frame->local = local;
	local->inode = fd->inode;
	local->call_count = priv->child_count;
	
	while (trav) {
		STACK_WIND (frame,	      
			    stripe_stack_unwind_buf_cbk,
			    trav->xlator,
			    trav->xlator->fops->fstat,
			    fd);
		trav = trav->next;
	}

	return 0;
}


/**
 * stripe_fchmod - 
 */
int32_t 
stripe_fchmod (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       mode_t mode)
{
	stripe_local_t *local = NULL;
	stripe_private_t *priv = this->private;
	xlator_list_t *trav = this->children;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (fd);

	/* Initialization */
	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	local->op_ret = -1;
	frame->local = local;
	local->inode = fd->inode;
	local->call_count = priv->child_count;
	
	while (trav) {
		STACK_WIND (frame,	      
			    stripe_stack_unwind_buf_cbk,
			    trav->xlator,
			    trav->xlator->fops->fchmod,
			    fd, mode);
		trav = trav->next;
	}

	return 0;
}


/**
 * stripe_fchown - 
 */
int32_t 
stripe_fchown (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       uid_t uid,
	       gid_t gid)
{
	stripe_local_t *local = NULL;
	stripe_private_t *priv = this->private;
	xlator_list_t *trav = this->children;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (fd);

	/* Initialization */
	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	local->op_ret = -1;
	frame->local = local;
	local->inode = fd->inode;
	local->call_count = priv->child_count;
	
	while (trav) {
		STACK_WIND (frame,	      
			    stripe_stack_unwind_buf_cbk,
			    trav->xlator,
			    trav->xlator->fops->fchown,
			    fd, uid, gid);
		trav = trav->next;
	}

	return 0;
}


/**
 * stripe_ftruncate - 
 */
int32_t
stripe_ftruncate (call_frame_t *frame,
		  xlator_t *this,
		  fd_t *fd,
		  off_t offset)
{
	stripe_local_t *local = NULL;
	stripe_private_t *priv = this->private;
	xlator_list_t *trav = this->children;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (fd);

	/* Initialization */
	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	local->op_ret = -1;
	frame->local = local;
	local->inode = fd->inode;
	local->call_count = priv->child_count;
	
	while (trav) {
		STACK_WIND (frame,	      
			    stripe_stack_unwind_buf_cbk,
			    trav->xlator,
			    trav->xlator->fops->ftruncate,
			    fd, offset);
		trav = trav->next;
	}

	return 0;
}


/**
 * stripe_releasedir - 
 */
int32_t
stripe_releasedir (xlator_t *this,
		   fd_t *fd)
{
	return 0;
}


/**
 * stripe_fsyncdir - 
 */
int32_t
stripe_fsyncdir (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 int32_t flags)
{
	stripe_local_t *local = NULL;
	stripe_private_t *priv = this->private;
	xlator_list_t *trav = this->children;

	STRIPE_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (fd);

	/* Initialization */
	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	local->op_ret = -1;
	frame->local = local;
	local->call_count = priv->child_count;

	while (trav) {
		STACK_WIND (frame,	      
			    stripe_stack_unwind_cbk,
			    trav->xlator,
			    trav->xlator->fops->fsyncdir,
			    fd,
			    flags);
		trav = trav->next;
	}

	return 0;
}


/**
 * stripe_single_readv_cbk - This function is used as return fn, when the 
 *     file name doesn't match the pattern specified for striping.
 */
int32_t
stripe_single_readv_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct iovec *vector,
			 int32_t count,
			 struct stat *stbuf)
{
	STACK_UNWIND (frame, op_ret, op_errno, vector, count, stbuf);
	return 0;
}

/**
 * stripe_readv_cbk - get all the striped reads, and order it properly, send it
 *        to above layer after putting it in a single vector.
 */
int32_t
stripe_readv_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct iovec *vector,
		  int32_t count,
		  struct stat *stbuf)
{
	int32_t index = 0;
	int32_t callcnt = 0;
	call_frame_t *main_frame = NULL;
	stripe_local_t *main_local = NULL;
	stripe_local_t *local = frame->local;

	index = local->node_index;
	main_frame = local->orig_frame;
	main_local = main_frame->local;

	LOCK (&main_frame->lock);
	{
		main_local->replies[index].op_ret = op_ret;
		main_local->replies[index].op_errno = op_errno;
		if (op_ret >= 0) {
			main_local->replies[index].count  = count;
			main_local->replies[index].vector = iov_dup (vector, count);
			main_local->replies[index].stbuf = *stbuf;
			if (frame->root->rsp_refs)
				dict_copy (frame->root->rsp_refs, main_frame->root->rsp_refs);
		}
		callcnt = ++main_local->call_count;
	}
	UNLOCK(&main_frame->lock);

	if (callcnt == main_local->wind_count) {
		int32_t final_count = 0;
		struct iovec *final_vec = NULL;
		struct stat tmp_stbuf = {0,};
		dict_t *refs = main_frame->root->rsp_refs;

		op_ret = 0;
		memcpy (&tmp_stbuf, &main_local->replies[0].stbuf, sizeof (struct stat));
		for (index=0; index < main_local->wind_count; index++) {
			/* TODO: check whether each stripe returned 'expected'
			 * number of bytes 
			 */
			if (main_local->replies[index].op_ret == -1) {
				op_ret = -1;
				op_errno = main_local->replies[index].op_errno;
				break;
			}
			op_ret += main_local->replies[index].op_ret;
			final_count += main_local->replies[index].count;
			/* TODO: Do I need to send anything more in stbuf? */
			if (tmp_stbuf.st_size < main_local->replies[index].stbuf.st_size)
				tmp_stbuf.st_size = main_local->replies[index].stbuf.st_size;
		}

		if (op_ret != -1) {
			final_vec = calloc (final_count, sizeof (struct iovec));
			ERR_ABORT (final_vec);
			final_count = 0;

			for (index=0; index < main_local->wind_count; index++) {
				memcpy (final_vec + final_count,
					main_local->replies[index].vector,
					main_local->replies[index].count * sizeof (struct iovec));
				final_count += main_local->replies[index].count;

				free (main_local->replies[index].vector);
			}
		} else {
			final_vec = NULL;
			final_count = 0;
		}
		/* */
		FREE (main_local->replies);
		refs = main_frame->root->rsp_refs;
		STACK_UNWIND (main_frame, op_ret, op_errno, final_vec, final_count, &tmp_stbuf);

		dict_unref (refs);
		if (final_vec)
			free (final_vec);
	}

	STACK_DESTROY (frame->root);
	return 0;
}

/**
 * stripe_readv - 
 */
int32_t
stripe_readv (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      size_t size,
	      off_t offset)
{
	int32_t index = 0;
	int32_t num_stripe = 0;
	size_t frame_size = 0;
	off_t rounded_end = 0;
	off_t stripe_size = 0;
	off_t rounded_start = 0;
	off_t frame_offset = offset;
	stripe_local_t *local = NULL;
	call_frame_t *rframe = NULL;
	stripe_local_t *rlocal = NULL;
	xlator_list_t *trav = this->children;
	stripe_private_t *priv = this->private;

	//stripe_size = data_to_uint64 (dict_get (fd->ctx, this->name));
	stripe_size = priv->block_size;

	/* The file is stripe across the child nodes. Send the read request to the 
	 * child nodes appropriately after checking which region of the file is in
	 * which child node. Always '0-<stripe_size>' part of the file resides in
	 * the first child.
	 */
	rounded_start = floor (offset, stripe_size);
	rounded_end = roof (offset+size, stripe_size);
	num_stripe = (rounded_end - rounded_start) / stripe_size;
	
	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	local->wind_count = num_stripe;
	frame->local = local;
	frame->root->rsp_refs = dict_ref (get_new_dict ());
	
	/* This is where all the vectors should be copied. */
	local->replies = calloc (1, num_stripe * sizeof (struct readv_replies));
	ERR_ABORT (local->replies);
	
	for (index = 0;
	     index < ((offset / stripe_size) % priv->child_count);
	     index++) {
		trav = trav->next;
	}
    
	for (index = 0; index < num_stripe; index++) {
		rframe = copy_frame (frame);
		rlocal = calloc (1, sizeof (stripe_local_t));
		ERR_ABORT (rlocal);
		
		frame_size = min (roof (frame_offset+1, stripe_size),
				  (offset + size)) - frame_offset;
		
		rlocal->node_index = index;
		rlocal->orig_frame = frame;
		rframe->local = rlocal;
		STACK_WIND (rframe,
			    stripe_readv_cbk,
			    trav->xlator,
			    trav->xlator->fops->readv,
			    fd, frame_size, frame_offset);
      
		frame_offset += frame_size;

		trav = trav->next ? trav->next : this->children;
	}

	return 0;
}


/**
 * stripe_writev_cbk - 
 */
int32_t
stripe_writev_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   struct stat *stbuf)
{
	int32_t callcnt = 0;
	stripe_local_t *local = frame->local;
	LOCK(&frame->lock);
	{
		callcnt = ++local->call_count;
    
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_WARNING, "%s returned error %s",
				((call_frame_t *)cookie)->this->name, strerror (op_errno));
			local->op_errno = op_errno;
			local->op_ret = -1;
		}
		if (op_ret >= 0) {
			local->op_ret += op_ret;
			local->stbuf = *stbuf;
		}
	}
	UNLOCK (&frame->lock);

	if ((callcnt == local->wind_count) && local->unwind) {
		STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
	}
	return 0;
}


/**
 * stripe_single_writev_cbk - 
 */
int32_t
stripe_single_writev_cbk (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  struct stat *stbuf)
{
	STACK_UNWIND (frame, op_ret, op_errno, stbuf);
	return 0;
}
/**
 * stripe_writev - 
 */
int32_t
stripe_writev (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       struct iovec *vector,
	       int32_t count,
	       off_t offset)
{
	int32_t idx = 0;
	int32_t total_size = 0;
	int32_t offset_offset = 0;
	int32_t remaining_size = 0;
	int32_t tmp_count = count;
	off_t fill_size = 0;
	off_t stripe_size = 0;
	struct iovec *tmp_vec = vector;
	stripe_private_t *priv = this->private;
	stripe_local_t *local = NULL;
	xlator_list_t *trav = NULL;

	//stripe_size = data_to_uint64 (dict_get (fd->ctx, this->name));
	stripe_size = priv->block_size;

	/* File has to be stripped across the child nodes */
	for (idx = 0; idx< count; idx ++) {
		total_size += tmp_vec[idx].iov_len;
	}
	remaining_size = total_size;

	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	frame->local = local;
	local->stripe_size = stripe_size;

	while (1) {
		/* Send striped chunk of the vector to child nodes appropriately. */
		trav = this->children;
		
		idx = ((offset + offset_offset) / local->stripe_size) % priv->child_count;
		while (idx) {
			trav = trav->next;
			idx--;
		}
		fill_size = local->stripe_size - ((offset + offset_offset) % local->stripe_size);
		if (fill_size > remaining_size)
			fill_size = remaining_size;

		remaining_size -= fill_size;

		tmp_count = iov_subset (vector, count, offset_offset,
					offset_offset + fill_size, NULL);
		tmp_vec = calloc (tmp_count, sizeof (struct iovec));
		ERR_ABORT (tmp_vec);
		tmp_count = iov_subset (vector, count, offset_offset,
					offset_offset + fill_size, tmp_vec);
		
		local->wind_count++;
		if (remaining_size == 0)
			local->unwind = 1;

		STACK_WIND(frame,
			   stripe_writev_cbk,
			   trav->xlator,
			   trav->xlator->fops->writev,
			   fd, tmp_vec, tmp_count, offset + offset_offset);
		FREE (tmp_vec);
		offset_offset += fill_size;
		if (remaining_size == 0)
			break;
	}

	return 0;
}



/* Management operations */

/**
 * stripe_stats_cbk - Add all the fields received from different clients. 
 *    Once all the clients return, send stats to above layer.
 * 
 */
int32_t
stripe_stats_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct xlator_stats *stats)
{
	int32_t callcnt = 0;
	stripe_local_t *local = frame->local;

	LOCK(&frame->lock);
	{
		callcnt = --local->call_count;
    
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_WARNING, "%s returned error %s",
				((call_frame_t *)cookie)->this->name, strerror (op_errno));
			local->op_ret = -1;
			local->op_errno = op_errno;
		}
		if (op_ret == 0) {
			if (local->op_ret == -2) {
				/* This is to make sure this is the frist time */
				local->stats = *stats;
				local->op_ret = 0;
			} else {
				local->stats.nr_files += stats->nr_files;
				local->stats.free_disk += stats->free_disk;
				local->stats.disk_usage += stats->disk_usage;
				local->stats.nr_clients += stats->nr_clients;
			}
		}
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stats);
	}

	return 0;
}

/**
 * stripe_stats - 
 */
int32_t
stripe_stats (call_frame_t *frame,
	      xlator_t *this,
	      int32_t flags)
{
	stripe_local_t *local = NULL;
	xlator_list_t *trav = this->children;

	local = calloc (1, sizeof (stripe_local_t));
	ERR_ABORT (local);
	frame->local = local;
	local->op_ret = -2; /* to be used as a flag in _cbk */
	local->call_count = ((stripe_private_t*)this->private)->child_count;
	while (trav) {
		STACK_WIND (frame,
			    stripe_stats_cbk,
			    trav->xlator,
			    trav->xlator->mops->stats,
			    flags);
		trav = trav->next;
	}
	return 0;
}

/**
 * notify
 */
int32_t
notify (xlator_t *this,
        int32_t event,
        void *data,
        ...)
{
	stripe_private_t *priv = this->private;
	int down_client = 0;
	int i = 0;

	if (!priv)
		return 0;

	switch (event) 
	{
	case GF_EVENT_CHILD_UP:
	{
		/* get an index number to set */
		for (i = 0; i < priv->child_count; i++) {
			if (data == priv->xl_array[i])
				break;
		}
		priv->state[i] = 1;
		for (i = 0; i < priv->child_count; i++) {
			if (!priv->state[i])
				down_client++;
		}

		LOCK (&priv->lock);
		{
			priv->nodes_down = down_client;

			if (data == FIRST_CHILD (this)) {
				priv->first_child_down = 0;
				default_notify (this, event, data);
			}
		}
		UNLOCK (&priv->lock);
	}
	break;
	case GF_EVENT_CHILD_DOWN:
	{
		/* get an index number to set */
		for (i = 0; i < priv->child_count; i++) {
			if (data == priv->xl_array[i])
				break;
		}
		priv->state[i] = 0;
		for (i = 0; i < priv->child_count; i++) {
			if (!priv->state[i])
				down_client++;
		}

		LOCK (&priv->lock);
		{
			priv->nodes_down = down_client;

			if (data == FIRST_CHILD (this)) {
				priv->first_child_down = 1;
				default_notify (this, event, data);
			}
		}
		UNLOCK (&priv->lock);
	}
	break;
	case GF_EVENT_PARENT_UP:
		break;
	default:
	{
		/* */
		default_notify (this, event, data);
	}
	break;
	}

	return 0;
}
/**
 * init - This function is called when xlator-graph gets initialized. 
 *     The option given in spec files are parsed here.
 * @this - 
 */
int32_t
init (xlator_t *this)
{
	stripe_private_t *priv = NULL;
	xlator_list_t *trav = NULL;
	data_t *data = NULL;
	int32_t count = 0;
	int flag = 0;
  
	data = dict_get (this->options, "drop-hostname-from-subvolumes");
	if (data) {
		/* Well, drop the hostname from the subvolumes */
		char host_name[256];
		
		if (gethostname (host_name, 256) == -1) {
			gf_log (this->name, GF_LOG_ERROR, 
				"drop-hostname-from-subvolumes: gethostname(): %s", strerror (errno));
			return -1;
		} else {
			/* Check if the host name is proper subvolume of stripe */
			trav = this->children;
			if (strcmp (host_name, trav->xlator->name) == 0) {
				/* Well there is a volume with the name 'hostname()', hence neglect it */
				this->children = trav->next;
				gf_log (this->name, GF_LOG_DEBUG, 
					"drop-hostname-from-subvolumes: skipped volume '%s'", host_name);
				flag = 1;
			}
			while (trav && trav->next) {
				if (strcmp (host_name, (trav->next)->xlator->name) == 0) {
					/* Well there is a volume with the name 'hostname()', hence neglect it */
					/* Remove entry about this subvolume from the list */
					trav->next = (trav->next)->next;
					flag = 1;
				}
				trav = trav->next;
			}
	      
			if (!flag) {
				/* entry for 'local-volume-name' is wrong, not present in subvolumes */
				gf_log (this->name, GF_LOG_ERROR, 
					"requested to drop hostname from subvolumes, but no volume by name '%s'", 
					host_name);
				return -1;
			} 
		}
	}

	trav = this->children;
	while (trav) {
		count++;
		trav = trav->next;
	}

	if (!count) {
		gf_log (this->name, GF_LOG_ERROR,
			"stripe configured without \"subvolumes\" option. exiting");
		return -1;
	}
	priv = calloc (1, sizeof (stripe_private_t));
	ERR_ABORT (priv);
	priv->xl_array = calloc (1, count * sizeof (xlator_t *));
	ERR_ABORT (priv->xl_array);
	priv->child_count = count;
	LOCK_INIT (&priv->lock);

	trav = this->children;
	count = 0;
	while (trav) {
		priv->xl_array[count++] = trav->xlator;
		trav = trav->next;
	}

	if (count > 256) {
		gf_log (this->name, GF_LOG_ERROR,
			"maximum number of stripe subvolumes supported is 256");
		return -1;
	}

	/* option stripe-pattern *avi:1GB,*pdf:4096 */
	data = dict_get (this->options, "block-size");
	if (!data) {
		gf_log (this->name, GF_LOG_WARNING,
			"No stripe pattern specified. check \"option block-size <x>\" in spec file");
	} else {
		char *tmp_str = NULL;
		char *tmp_str1 = NULL;
		char *dup_str = NULL;
		char *stripe_str = NULL;
		char *pattern = NULL;
		char *num = NULL;
		/* Get the pattern for striping. "option block-size *avi:10MB" etc */
		stripe_str = strtok_r (data->data, ",", &tmp_str);
		while (stripe_str) {
			dup_str = strdup (stripe_str);
			pattern = strtok_r (dup_str, ":", &tmp_str1);
			num = strtok_r (NULL, ":", &tmp_str1);
			if (num) {
				if (gf_string2bytesize (num, &priv->block_size) != 0) {
					gf_log (this->name, GF_LOG_ERROR, 
						"invalid number format \"%s\"", 
						num);
					return -1;
				}
			} else {
				/* Possible that there is no pattern given */
				if (gf_string2bytesize (pattern, &priv->block_size) != 0) {
					priv->block_size = (128 * GF_UNIT_KB);
				}
			}
			stripe_str = strtok_r (NULL, ",", &tmp_str);
		}
	}

/* The below code will be used when its only size as option */
/*
	data = dict_get (this->options, "block-size");
	if (!data) {
		gf_log (this->name, GF_LOG_WARNING,
			"No block-size specified. check \"option block-size <x>\" in spec file, defaulting to 128KB");
		priv->block_size = (128 * GF_UNIT_KB);
	} else {
		if (gf_string2bytesize (data->data, &priv->block_size) != 0) {
			gf_log ("stripe", GF_LOG_ERROR, 
				"invalid number format \"%s\"", data->data);
			return -1;
		}
	}
*/

	priv->xattr_supported = 1;
	priv->xattr_supported_option_given = 0;
	data = dict_get (this->options, "extended-attribute-support");
	if (data) {
		if (gf_string2boolean (data->data, &priv->xattr_supported) == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"error setting hard check for extended attribute");
			//return -1;
		}
		priv->xattr_supported_option_given = 1;
	}
	
        
	gf_log (this->name, GF_LOG_DEBUG, "stripe block size %"PRIu64"", priv->block_size);

	/* notify related */
	priv->nodes_down = priv->child_count;
	this->private = priv;

	trav = this->children;
	while (trav) {
		trav->xlator->notify (trav->xlator, GF_EVENT_PARENT_UP, this);
		trav = trav->next;
	}

	return 0;
} 

/** 
 * fini -   Free all the private variables
 * @this - 
 */
void 
fini (xlator_t *this)
{
	stripe_private_t *priv = this->private;

	FREE (priv->xl_array);
	LOCK_DESTROY (&priv->lock);
	FREE (priv);
	return;
}


struct xlator_fops fops = {
	.stat        = stripe_stat,
	.unlink      = stripe_unlink,
	.symlink     = stripe_symlink,
	.rename      = stripe_rename,
	.link        = stripe_link,
	.chmod       = stripe_chmod,
	.chown       = stripe_chown,
	.truncate    = stripe_truncate,
	.utimens     = stripe_utimens,
	.create      = stripe_create,
	.open        = stripe_open,
	.readv       = stripe_readv,
	.writev      = stripe_writev,
	.statfs      = stripe_statfs,
	.flush       = stripe_flush,
	.fsync       = stripe_fsync,
	.setxattr    = stripe_setxattr,
	.getxattr    = stripe_getxattr,
	.removexattr = stripe_removexattr,
	.access      = stripe_access,
	.ftruncate   = stripe_ftruncate,
	.fstat       = stripe_fstat,
	.readlink    = stripe_readlink,
	.mkdir       = stripe_mkdir,
	.rmdir       = stripe_rmdir,
	.lk          = stripe_lk,
	.opendir     = stripe_opendir,
	.fsyncdir    = stripe_fsyncdir,
	.fchmod      = stripe_fchmod,
	.fchown      = stripe_fchown,
	.lookup      = stripe_lookup,
	.setdents    = stripe_setdents,
	.mknod       = stripe_mknod,
};

struct xlator_mops mops = {
	.stats  = stripe_stats,
};

struct xlator_cbks cbks = {
	.release    = stripe_release,
	.releasedir = stripe_releasedir
};


struct xlator_options options[] = {
	{ "block-size", GF_OPTION_TYPE_ANY, 0, },
	{ "drop-hostname-from-subvolumes", GF_OPTION_TYPE_BOOL, 0,  },
	{ "extended-attribute-support", GF_OPTION_TYPE_BOOL, 0,  },
	{ NULL, 0, },
};
