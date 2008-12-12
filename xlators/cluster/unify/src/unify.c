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

/**
 * xlators/cluster/unify:
 *     - This xlator is one of the main translator in GlusterFS, which
 *   actually does the clustering work of the file system. One need to 
 *   understand that, unify assumes file to be existing in only one of 
 *   the child node, and directories to be present on all the nodes. 
 *
 * NOTE:
 *   Now, unify has support for global namespace, which is used to keep a 
 * global view of fs's namespace tree. The stat for directories are taken
 * just from the namespace, where as for files, just 'st_ino' is taken from
 * Namespace node, and other stat info is taken from the actual storage node.
 * Also Namespace node helps to keep consistant inode for files across 
 * glusterfs (re-)mounts.
 */

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "unify.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"
#include "defaults.h"
#include "common-utils.h"

#define CHILDDOWN ENOTCONN

#include <sys/xattr.h>
#include <signal.h>
#include <libgen.h>
#include "compat-errno.h"
#include "compat.h"

#define UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR(_loc) do { \
  if (!(_loc && _loc->inode && _loc->inode->ctx)) {        \
    TRAP_ON (!(_loc && _loc->inode && _loc->inode->ctx));  \
    STACK_UNWIND (frame, -1, EINVAL, NULL, NULL, NULL);    \
    return 0;                                              \
  }                                                        \
} while(0)


#define UNIFY_CHECK_FD_CTX_AND_UNWIND_ON_ERR(_fd) do { \
  if (!(_fd && _fd->ctx &&                             \
	dict_get (_fd->ctx, this->name))) {            \
    TRAP_ON (!(_fd && _fd->ctx &&                      \
	          dict_get (_fd->ctx, this->name)));   \
    STACK_UNWIND (frame, -1, EBADFD, NULL, NULL);      \
    return 0;                                          \
  }                                                    \
} while(0)

#define UNIFY_CHECK_FD_AND_UNWIND_ON_ERR(_fd) do { \
  if (!(_fd && _fd->ctx)) {                        \
    TRAP_ON (!(_fd && _fd->ctx));                  \
    STACK_UNWIND (frame, -1, EBADFD, NULL, NULL);  \
    return 0;                                      \
  }                                                \
} while(0)

/**
 * unify_local_wipe - free all the extra allocation of local->* here.
 */
static void 
unify_local_wipe (unify_local_t *local)
{
	/* Free the strdup'd variables in the local structure */
	if (local->name) {
		FREE (local->name);
	}
	loc_wipe (&local->loc1);
	loc_wipe (&local->loc2);
}



/*
 * unify_normalize_stats -
 */
void
unify_normalize_stats (struct statvfs *buf,
		       unsigned long bsize,
		       unsigned long frsize)
{
	double factor;

	if (buf->f_bsize != bsize) {
		factor = ((double) buf->f_bsize) / bsize;
		buf->f_bsize  = bsize;
		buf->f_bfree  = (fsblkcnt_t) (factor * buf->f_bfree);
		buf->f_bavail = (fsblkcnt_t) (factor * buf->f_bavail);
	}
  
	if (buf->f_frsize != frsize) {
		factor = ((double) buf->f_frsize) / frsize;
		buf->f_frsize = frsize;
		buf->f_blocks = (fsblkcnt_t) (factor * buf->f_blocks);
	}
}

/**
 * unify_statfs_cbk -
 */
int32_t
unify_statfs_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct statvfs *stbuf)
{
	int32_t callcnt = 0;
	struct statvfs *dict_buf = NULL;
	unsigned long bsize;
	unsigned long frsize;
	unify_local_t *local = (unify_local_t *)frame->local;
	call_frame_t *prev_frame = cookie;

	LOCK (&frame->lock);
	{
		if (op_ret >= 0) {
			/* when a call is successfull, add it to local->dict */
			dict_buf = &local->statvfs_buf;

			if (dict_buf->f_bsize != 0) {
				bsize  = max (dict_buf->f_bsize, 
					      stbuf->f_bsize);

				frsize = max (dict_buf->f_frsize, 
					      stbuf->f_frsize);
				unify_normalize_stats(dict_buf, bsize, frsize);
				unify_normalize_stats(stbuf, bsize, frsize);
			} else {
				dict_buf->f_bsize   = stbuf->f_bsize;
				dict_buf->f_frsize  = stbuf->f_frsize;
			}
      
			dict_buf->f_blocks += stbuf->f_blocks;
			dict_buf->f_bfree  += stbuf->f_bfree;
			dict_buf->f_bavail += stbuf->f_bavail;
			dict_buf->f_files  += stbuf->f_files;
			dict_buf->f_ffree  += stbuf->f_ffree;
			dict_buf->f_favail += stbuf->f_favail;
			dict_buf->f_fsid    = stbuf->f_fsid;
			dict_buf->f_flag    = stbuf->f_flag;
			dict_buf->f_namemax = stbuf->f_namemax;
			local->op_ret = op_ret;
		} else {
			/* fop on storage node has failed due to some error */
			if (op_errno != ENOTCONN) {
				gf_log (this->name, GF_LOG_ERROR, 
					"child(%s): %s", 
					prev_frame->this->name, 
					strerror (op_errno));
			}
			local->op_errno = op_errno;
		}
		callcnt = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		STACK_UNWIND (frame, local->op_ret, local->op_errno, 
			      &local->statvfs_buf);
	}

	return 0;
}

/**
 * unify_statfs -
 */
int32_t
unify_statfs (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
	unify_local_t *local = NULL;
	xlator_list_t *trav = this->children;

	INIT_LOCAL (frame, local);
	local->call_count = ((unify_private_t *)this->private)->child_count;

	while(trav) {
		STACK_WIND (frame,
			    unify_statfs_cbk,
			    trav->xlator,
			    trav->xlator->fops->statfs,
			    loc);
		trav = trav->next;
	}

	return 0;
}

/**
 * unify_buf_cbk - 
 */
int32_t
unify_buf_cbk (call_frame_t *frame,
	       void *cookie,
	       xlator_t *this,
	       int32_t op_ret,
	       int32_t op_errno,
	       struct stat *buf)
{
	int32_t callcnt = 0;
	unify_private_t *priv = this->private;
	unify_local_t *local = frame->local;
	call_frame_t *prev_frame = cookie;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
    
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"child(%s): path(%s): %s", 
				prev_frame->this->name, 
				(local->loc1.path)?local->loc1.path:"", 
				strerror (op_errno));

			local->op_errno = op_errno;
			if ((op_errno == ENOENT) && priv->optimist) 
				local->op_ret = 0;
		}

		if (op_ret >= 0) {
			local->op_ret = 0;

			if (NS (this) == prev_frame->this) {
				local->st_ino = buf->st_ino;
				/* If the entry is directory, get the stat
				   from NS node */
				if (S_ISDIR (buf->st_mode) || 
				    !local->stbuf.st_blksize) {
					local->stbuf = *buf;
				}
			}

			if ((!S_ISDIR (buf->st_mode)) && 
			    (NS (this) != prev_frame->this)) {
				/* If file, take the stat info from Storage 
				   node. */
				local->stbuf = *buf;
			}
		}
	}
	UNLOCK (&frame->lock);
    
	if (!callcnt) {
		/* If the inode number is not filled, operation should
		   fail */
		if (!local->st_ino)
			local->op_ret = -1;

		local->stbuf.st_ino = local->st_ino;
		unify_local_wipe (local);
		STACK_UNWIND (frame, local->op_ret, local->op_errno, 
			      &local->stbuf);
	}

	return 0;
}

#define check_if_dht_linkfile(s) ((s->st_mode & ~S_IFMT) == S_ISVTX)

/**
 * unify_lookup_cbk - 
 */
int32_t 
unify_lookup_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  inode_t *inode,
		  struct stat *buf,
		  dict_t *dict)
{
	int ret = 0;
	int32_t callcnt = 0;
	unify_private_t *priv = this->private;
	unify_local_t *local = frame->local;
	inode_t *tmp_inode = NULL;
	dict_t *local_dict = NULL;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
 
		if (op_ret == -1) {
			if ((op_errno != ENOTCONN) && (op_errno != ENOENT)) {
				gf_log (this->name, GF_LOG_ERROR,
					"child(%s): path(%s): %s", 
					priv->xl_array[(long)cookie]->name, 
					local->loc1.path, strerror (op_errno));
				local->op_errno = op_errno;
				local->failed = 1;

			} else if (local->revalidate && 
				   !(priv->optimist && (op_errno == ENOENT))) {

				gf_log (this->name, 
					(op_errno == ENOTCONN) ? 
					GF_LOG_DEBUG:GF_LOG_ERROR,
					"child(%s): path(%s): %s", 
					priv->xl_array[(long)cookie]->name, 
					local->loc1.path, strerror (op_errno));
				local->op_errno = op_errno;
				local->failed = 1;
			}
		}

		if (op_ret == 0) {
			local->op_ret = 0; 
			
			if (check_if_dht_linkfile(buf)) {
				gf_log (this->name, GF_LOG_CRITICAL,
					"file %s may be DHT link file on %s, "
					"make sure the backend is not shared "
					"between unify and DHT", 
					local->loc1.path, 
					priv->xl_array[(long)cookie]->name);
			}

			if (local->stbuf.st_mode && local->stbuf.st_blksize) {
				/* make sure we already have a stbuf
				   stored in local->stbuf */
				if (S_ISDIR (local->stbuf.st_mode) && 
				    !S_ISDIR (buf->st_mode)) {
					gf_log (this->name, GF_LOG_CRITICAL, 
						"[CRITICAL] '%s' is directory "
						"on namespace, non-directory "
						"on node '%s', returning EIO",
						local->loc1.path, 
						priv->xl_array[(long)cookie]->name);
					local->return_eio = 1;
				}
				if (!S_ISDIR (local->stbuf.st_mode) && 
				    S_ISDIR (buf->st_mode)) {
					gf_log (this->name, GF_LOG_CRITICAL, 
						"[CRITICAL] '%s' is directory "
						"on node '%s', non-directory "
						"on namespace, returning EIO",
						local->loc1.path, 
						priv->xl_array[(long)cookie]->name);
					local->return_eio = 1;
				}
			}
	
			if (!local->revalidate && !S_ISDIR (buf->st_mode)) {
				/* This is the first time lookup on file*/
				if (!local->list) {
					/* list is not allocated, allocate 
					   the max possible range */
					local->list = calloc (1, 2 * (priv->child_count + 2));
					if (!local->list) {
						gf_log (this->name, 
							GF_LOG_CRITICAL, 
							"Not enough memory");
						STACK_UNWIND (frame, -1, 
							      ENOMEM, inode, 
							      NULL, NULL);
						return 0;
					}
				}
				/* update the index of the list */
				local->list [local->index++] = 
					(int16_t)(long)cookie;
			}
      
			if ((!local->dict) && dict &&
			    (priv->xl_array[(long)cookie] != NS(this)))	{
				local->dict = dict_ref (dict);
			}

			/* index of NS node is == total child count */
			if (priv->child_count == (int16_t)(long)cookie) {
				/* Take the inode number from namespace */
				local->st_ino = buf->st_ino;
				if (S_ISDIR (buf->st_mode) || 
				    !(local->stbuf.st_blksize)) {
					local->stbuf = *buf;
				}
			} else if (!S_ISDIR (buf->st_mode)) {
				/* If file, then get the stat from 
				   storage node */
				local->stbuf = *buf;
			}

			if (local->st_nlink < buf->st_nlink) {
				local->st_nlink = buf->st_nlink;
			}
		}
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		local_dict = local->dict;
		if (local->return_eio) {
			gf_log (this->name, GF_LOG_CRITICAL, 
				"[CRITICAL] Unable to fix the path (%s) with "
				"self-heal, try manual verification. "
				"returning EIO.", local->loc1.path);
			unify_local_wipe (local);
			STACK_UNWIND (frame, -1, EIO, inode, NULL, NULL);
			if (local_dict)	{
				dict_unref (local_dict);
			}
			return 0;
		}

		if (!local->stbuf.st_blksize) {
			/* Inode not present */
			local->op_ret = -1;
		} else {
			if (!local->revalidate && 
			    !S_ISDIR (local->stbuf.st_mode)) { 
				/* If its a file, big array is useless, 
				   allocate the smaller one */
				int16_t *list = NULL;
				list = calloc (1, 2 * (local->index + 1));
				ERR_ABORT (list);
				memcpy (list, local->list, 2 * local->index);
				/* Make the end of the list as -1 */
				FREE (local->list);
				local->list = list;
				local->list [local->index] = -1;
				/* Update the inode->ctx with proper array */
				/* TODO: log on failure */
				ret = dict_set_ptr (local->loc1.inode->ctx, 
						    this->name, local->list);
			}

			if (S_ISDIR(local->loc1.inode->st_mode)) {
				/* lookup is done for directory */
				if (local->failed && priv->self_heal) {
					/* Triggering self-heal */
                                        /* means, self-heal required for this 
					   inode */
					local->inode_generation = 0; 
					priv->inode_generation++;
				}
			} else {
				local->stbuf.st_ino = local->st_ino;
			}
	  
			local->stbuf.st_nlink = local->st_nlink;
		}
		if (local->op_ret == -1) {
			if (!local->revalidate && local->list)
				FREE (local->list);
		}

		if ((local->op_ret >= 0) && local->failed && 
		    local->revalidate) {
			/* Done revalidate, but it failed */
			if (op_errno != ENOTCONN) {
				gf_log (this->name, GF_LOG_ERROR, 
					"Revalidate failed for path(%s): %s", 
					local->loc1.path, strerror (op_errno));
			}
			local->op_ret = -1;
		}

		if ((priv->self_heal && !priv->optimist) && 
		    (!local->revalidate && (local->op_ret == 0) && 
		     S_ISDIR(local->stbuf.st_mode))) {
			/* Let the self heal be done here */
			zr_unify_self_heal (frame, this, local);
			local_dict = NULL;
		} else {
			/* either no self heal, or op_ret == -1 (failure) */
			tmp_inode = local->loc1.inode;
			unify_local_wipe (local);
			STACK_UNWIND (frame, local->op_ret, local->op_errno, 
				      tmp_inode, &local->stbuf, local->dict);
		}
		if (local_dict) {
			dict_unref (local_dict);
		}
	}
  
	return 0;
}

/**
 * unify_lookup - 
 */
int32_t 
unify_lookup (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t need_xattr)
{
	unify_local_t *local = NULL;
	unify_private_t *priv = this->private;
	int16_t *list = NULL;
	long index = 0;

	if (!(loc && loc->inode && loc->inode->ctx)) {
		gf_log (this->name, GF_LOG_ERROR, 
			"%s: Argument not right", loc?loc->path:"(null)");
		STACK_UNWIND (frame, -1, EINVAL, NULL, NULL, NULL);
		return 0;
	}

	/* Initialization */
	INIT_LOCAL (frame, local);
	loc_copy (&local->loc1, loc);
	if (local->loc1.path == NULL) {
		gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
		STACK_UNWIND (frame, -1, ENOMEM, loc->inode, NULL, NULL);
		return 0;
	}

	if (dict_get (loc->inode->ctx, this->name) && 
	    loc->inode->st_mode && 
	    !S_ISDIR (loc->inode->st_mode)) {
		/* check if revalidate or fresh lookup */
		local->list = data_to_ptr (dict_get (loc->inode->ctx, 
						     this->name));
	}

	if (local->list) {
		list = local->list;
		for (index = 0; list[index] != -1; index++);
		if (index != 2) {
			if (index < 2) {
				gf_log (this->name, GF_LOG_ERROR,
					"returning ESTALE for %s: file "
					"count is %ld", loc->path, index);
				/* Print where all the file is present */
				for (index = 0; 
				     local->list[index] != -1; index++) {
					gf_log (this->name, GF_LOG_ERROR, 
						"%s: found on %s", loc->path, 
						priv->xl_array[list[index]]->name);
				}
				unify_local_wipe (local);
				STACK_UNWIND (frame, -1, ESTALE, 
					      NULL, NULL, NULL);
				return 0;  
			} else {
				/* There are more than 2 presences */
				/* Just log and continue */
				gf_log (this->name, GF_LOG_ERROR,
					"%s: file count is %ld", 
					loc->path, index);
				/* Print where all the file is present */
				for (index = 0; 
				     local->list[index] != -1; index++) {
					gf_log (this->name, GF_LOG_ERROR, 
						"%s: found on %s", loc->path, 
						priv->xl_array[list[index]]->name);
				}
			}
		}
      
		/* is revalidate */
		local->revalidate = 1;
      
		for (index = 0; list[index] != -1; index++)
			local->call_count++;
      
		for (index = 0; list[index] != -1; index++) {
			char need_break = (list[index+1] == -1);
			STACK_WIND_COOKIE (frame,
					   unify_lookup_cbk,
					   (void *)(long)list[index], //cookie
					   priv->xl_array [list[index]],
					   priv->xl_array [list[index]]->fops->lookup,
					   loc,
					   need_xattr);
			if (need_break)
				break;
		}
	} else {
		if (loc->inode->st_mode) {
			if (dict_get (loc->inode->ctx, this->name)) {
				local->inode_generation = 
					data_to_int64 (dict_get (
							       loc->inode->ctx,
							       this->name));
			}
		}
		/* This is first call, there is no list */
		/* call count should be all child + 1 namespace */
		local->call_count = priv->child_count + 1;
      
		for (index = 0; index <= priv->child_count; index++) {
			STACK_WIND_COOKIE (frame,
					   unify_lookup_cbk,
					   (void *)index, //cookie
					   priv->xl_array[index],
					   priv->xl_array[index]->fops->lookup,
					   loc,
					   need_xattr);
		}
	}

	return 0;
}

/**
 * unify_stat - if directory, get the stat directly from NameSpace child.
 *     if file, check for a hint and send it only there (also to NS).
 *     if its a fresh stat, then do it on all the nodes.
 *
 * NOTE: for all the call, sending cookie as xlator pointer, which will be 
 *       used in cbk.
 */
int32_t
unify_stat (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc)
{
	unify_local_t *local = NULL;
	unify_private_t *priv = this->private;
	int16_t index = 0;
	int16_t *list = NULL;

	UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	/* Initialization */
	INIT_LOCAL (frame, local);
	loc_copy (&local->loc1, loc);
	if (local->loc1.path == NULL) {
		gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
		STACK_UNWIND (frame, -1, ENOMEM, NULL);
		return 0;
	}

	if (S_ISDIR (loc->inode->st_mode)) {
		/* Directory */
		local->call_count = 1;
		STACK_WIND (frame, unify_buf_cbk, NS(this),
			    NS(this)->fops->stat, loc);
	} else {
		/* File */
		list = data_to_ptr (dict_get (loc->inode->ctx, this->name));
    
		for (index = 0; list[index] != -1; index++)
			local->call_count++;
    
		for (index = 0; list[index] != -1; index++) {
			char need_break = (list[index+1] == -1);
			STACK_WIND (frame,
				    unify_buf_cbk,
				    priv->xl_array[list[index]],
				    priv->xl_array[list[index]]->fops->stat,
				    loc);
			if (need_break)
				break;
		}
	}

	return 0;
}

/**
 * unify_access_cbk -
 */
int32_t
unify_access_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


/**
 * unify_access - Send request to only namespace, which has all the 
 *      attributes set for the file.
 */
int32_t
unify_access (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t mask)
{
	UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	STACK_WIND (frame,
		    unify_access_cbk,
		    NS(this),
		    NS(this)->fops->access,
		    loc,
		    mask);

	return 0;
}

int32_t
unify_mkdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *buf)
{
	int32_t callcnt = 0;
	unify_private_t *priv = this->private;
	unify_local_t *local = frame->local;
	inode_t *tmp_inode = NULL;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
  
		if ((op_ret == -1) && !(priv->optimist && 
					(op_errno == ENOENT || 
					 op_errno == EEXIST))) {
			/* TODO: Decrement the inode_generation of 
			 * this->inode's parent inode, hence the missing 
			 * directory is created properly by self-heal. 
			 * Currently, there is no way to get the parent 
			 * inode directly.
			 */
			gf_log (this->name, GF_LOG_ERROR,
				"child(%s): path(%s): %s", 
				priv->xl_array[(long)cookie]->name, 
				local->loc1.path, strerror (op_errno));
			if (op_errno != EEXIST)
				local->failed = 1;
			local->op_errno = op_errno;
		}
  
		if (op_ret >= 0)
			local->op_ret = 0;

	}
	UNLOCK (&frame->lock);
  
	if (!callcnt) {
		if (!local->failed) {
			dict_set (local->loc1.inode->ctx, this->name, 
				  data_from_int64 (priv->inode_generation));
		}
		
		tmp_inode = local->loc1.inode;
		unify_local_wipe (local);

		STACK_UNWIND (frame, local->op_ret, local->op_errno, 
			      tmp_inode, &local->stbuf);
	}

	return 0;
}

/**
 * unify_ns_mkdir_cbk -
 */
int32_t
unify_ns_mkdir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct stat *buf)
{
	unify_private_t *priv = this->private;
	unify_local_t *local = frame->local;
	long index = 0;

	if (op_ret == -1) {
		/* No need to send mkdir request to other servers, 
		 * as namespace action failed 
		 */
		gf_log (this->name, GF_LOG_ERROR,
			"namespace: path(%s): %s", 
			local->name, strerror (op_errno));
		unify_local_wipe (local);
		STACK_UNWIND (frame, op_ret, op_errno, inode, NULL);
		return 0;
	}
  
	/* Create one inode for this entry */
	local->op_ret = 0;
	local->stbuf = *buf;

	local->call_count = priv->child_count;

	/* Send mkdir request to all the nodes now */
	for (index = 0; index < priv->child_count; index++) {
		STACK_WIND_COOKIE (frame,
				   unify_mkdir_cbk,
				   (void *)index, //cookie
				   priv->xl_array[index],
				   priv->xl_array[index]->fops->mkdir,
				   &local->loc1,
				   local->mode);
	}
  
	return 0;
}


/**
 * unify_mkdir -
 */
int32_t
unify_mkdir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode)
{
	unify_local_t *local = NULL;

	/* Initialization */
	INIT_LOCAL (frame, local);
	local->mode = mode;

	loc_copy (&local->loc1, loc);

	if (local->loc1.path == NULL) {
		gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
		STACK_UNWIND (frame, -1, ENOMEM, NULL, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    unify_ns_mkdir_cbk,
		    NS(this),
		    NS(this)->fops->mkdir,
		    loc,
		    mode);
	return 0;
}

/**
 * unify_rmdir_cbk -
 */
int32_t
unify_rmdir_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
	int32_t callcnt = 0;
	unify_private_t *priv = this->private;
	unify_local_t *local = frame->local;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
		if (op_ret == 0 || (priv->optimist && (op_errno == ENOENT)))
			local->op_ret = 0;
		if (op_ret == -1)
			local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		unify_local_wipe (local);
		STACK_UNWIND (frame, local->op_ret, local->op_errno);
	}

	return 0;
}

/**
 * unify_ns_rmdir_cbk -
 */
int32_t
unify_ns_rmdir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
	int16_t index = 0;
	unify_private_t *priv = this->private;
	unify_local_t *local = frame->local;
  
	if (op_ret == -1) {
		/* No need to send rmdir request to other servers, 
		 * as namespace action failed 
		 */
		gf_log (this->name, 
			((op_errno != ENOTEMPTY) ? 
			 GF_LOG_ERROR : GF_LOG_DEBUG),
			"namespace: path(%s): %s", 
			local->loc1.path, strerror (op_errno));
		unify_local_wipe (local);
		STACK_UNWIND (frame, op_ret, op_errno);
		return 0;
	}

	local->call_count = priv->child_count;

	for (index = 0; index < priv->child_count; index++) {
		STACK_WIND (frame,
			    unify_rmdir_cbk,
			    priv->xl_array[index],
			    priv->xl_array[index]->fops->rmdir,
			    &local->loc1);
	}

	return 0;
}

/**
 * unify_rmdir -
 */
int32_t
unify_rmdir (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc)
{
	unify_local_t *local = NULL;

	UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	/* Initialization */
	INIT_LOCAL (frame, local);

	loc_copy (&local->loc1, loc);
	if (local->loc1.path == NULL) {
		gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
		STACK_UNWIND (frame, -1, ENOMEM);
		return 0;
	}

	STACK_WIND (frame,
		    unify_ns_rmdir_cbk,
		    NS(this),
		    NS(this)->fops->rmdir,
		    loc);

	return 0;
}

/**
 * unify_open_cbk -
 */
int32_t
unify_open_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		fd_t *fd)
{
	int32_t callcnt = 0;
	unify_local_t *local = frame->local;

	LOCK (&frame->lock);
	{
		if (op_ret >= 0) {
			local->op_ret = op_ret;
			if (NS(this) != (xlator_t *)cookie) {
				/* Store child node's ptr, used in 
				   all the f*** / FileIO calls */
				dict_set (fd->ctx, this->name,
					  data_from_static_ptr (cookie));
			}
		}
		if (op_ret == -1) {
			local->op_errno = op_errno;
			local->failed = 1;
		}
		callcnt = --local->call_count;
	}
	UNLOCK (&frame->lock);
  
	if (!callcnt) {
		if ((local->failed == 1) && (local->op_ret >= 0)) {
			local->call_count = 1;
			/* return -1 to user */
			local->op_ret = -1;
			//local->op_errno = EIO; 
      
			if (dict_get (local->fd->ctx, this->name)) {
				gf_log (this->name, GF_LOG_ERROR, 
					"Open success on child node, "
					"failed on namespace");
			} else {
				gf_log (this->name, GF_LOG_ERROR, 
					"Open success on namespace, "
					"failed on child node");
			}
		}

		unify_local_wipe (local);
		STACK_UNWIND (frame, local->op_ret, 
			      local->op_errno, local->fd);
	}

	return 0;
}

#ifdef GF_DARWIN_HOST_OS
/**
 * unify_create_lookup_cbk - 
 */
int32_t 
unify_open_lookup_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       inode_t *inode,
		       struct stat *buf,
		       dict_t *dict)
{
	int32_t callcnt = 0;
	int16_t index = 0;
	unify_private_t *priv = this->private;
	unify_local_t *local = frame->local;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
		if ((op_ret == -1) && (op_errno != ENOENT)) {
			gf_log (this->name, GF_LOG_ERROR,
				"child(%s): path(%s): %s", 
				priv->xl_array[(long)cookie]->name, 
				local->loc1.path, strerror (op_errno));
			local->op_errno = op_errno;
		}
    
		if (op_ret >= 0) {
			local->op_ret = op_ret; 
			local->index++;
			if (NS(this) == priv->xl_array[(long)cookie]) {
				local->list[0] = (int16_t)(long)cookie;
			} else {
				local->list[1] = (int16_t)(long)cookie;
			}
			if (S_ISDIR (buf->st_mode))
				local->failed = 1;
		}
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		int16_t file_list[3] = {0,};
		local->op_ret = -1;

		file_list[0] = local->list[0];
		file_list[1] = local->list[1];
		file_list[2] = -1;

		if (local->index != 2) {
			/* Lookup failed, can't do open */
			gf_log (this->name, GF_LOG_ERROR,
				"%s: present on %d nodes", 
				local->name, local->index);

			if (local->index < 2) {
				unify_local_wipe (local);
				gf_log (this->name, GF_LOG_ERROR,
					"returning as file found on less "
					"than 2 nodes");
				STACK_UNWIND (frame, local->op_ret, 
					      local->op_errno, local->fd);
				return 0;
			}
		}

		if (local->failed) {
			/* Open on directory, return EISDIR */
			unify_local_wipe (local);
			STACK_UNWIND (frame, -1, EISDIR, local->fd);
			return 0;
		}

		/* Everything is perfect :) */    
		local->call_count = 2;
    
		for (index = 0; file_list[index] != -1; index++) {
			char need_break = (file_list[index+1] == -1);
			STACK_WIND_COOKIE (frame,
					   unify_open_cbk,
					   priv->xl_array[file_list[index]],
					   priv->xl_array[file_list[index]],
					   priv->xl_array[file_list[index]]->fops->open,
					   &local->loc1,
					   local->flags,
					   local->fd);
			if (need_break)
				break;
		}
	}

	return 0;
}


int32_t
unify_open_readlink_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 const char *path)
{
	int16_t index = 0;
	unify_private_t *priv = this->private;
	unify_local_t *local = frame->local;

	if (op_ret == -1) {
		STACK_UNWIND (frame, -1, ENOENT);
		return 0;
	}

	if (path[0] == '/') {
		local->name = strdup (path);
		ERR_ABORT (local->name);
	} else {
		char *tmp_str = strdup (local->loc1.path);
		char *tmp_base = dirname (tmp_str);
		local->name = calloc (1, ZR_PATH_MAX);
		strcpy (local->name, tmp_base);
		strncat (local->name, "/", 1);
		strcat (local->name, path);
		FREE (tmp_str);
	}
  
	local->list = calloc (1, sizeof (int16_t) * 3);
	ERR_ABORT (local->list);
	local->call_count = priv->child_count + 1;
	local->op_ret = -1;
	for (index = 0; index <= priv->child_count; index++) {
		/* Send the lookup to all the nodes including namespace */
		STACK_WIND_COOKIE (frame,
				   unify_open_lookup_cbk,
				   (void *)(long)index,
				   priv->xl_array[index],
				   priv->xl_array[index]->fops->lookup,
				   &local->loc1,
				   0);
	}

	return 0;
}
#endif /* GF_DARWIN_HOST_OS */

/**
 * unify_open - 
 */
int32_t
unify_open (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *loc,
	    int32_t flags,
	    fd_t *fd)
{
	unify_private_t *priv = this->private;
	unify_local_t *local = NULL;
	int16_t *list = NULL;
	int16_t index = 0;
	int16_t file_list[3] = {0,};

	UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	/* Init */
	INIT_LOCAL (frame, local);
	loc_copy (&local->loc1, loc);
	local->fd    = fd;
	local->flags = flags;
	list = data_to_ptr (dict_get (loc->inode->ctx, this->name));
	local->list = list;
	file_list[0] = priv->child_count; /* Thats namespace */
	file_list[2] = -1;
	for (index = 0; list[index] != -1; index++) {
		local->call_count++;
		if (list[index] != priv->child_count)
			file_list[1] = list[index];
	}

	if (local->call_count != 2) {
		/* If the lookup was done for file */
		gf_log (this->name, GF_LOG_ERROR,
			"%s: entry_count is %d",
			loc->path, local->call_count);
		for (index = 0; local->list[index] != -1; index++)
			gf_log (this->name, GF_LOG_ERROR, "%s: found on %s",
				loc->path, priv->xl_array[list[index]]->name);

		if (local->call_count < 2) {
			gf_log (this->name, GF_LOG_ERROR,
				"returning EIO as file found on onlyone node");
			STACK_UNWIND (frame, -1, EIO, fd);
			return 0;
		}
	}

#ifdef GF_DARWIN_HOST_OS
	/* Handle symlink here */
	if (S_ISLNK (loc->inode->st_mode)) {
		/* Callcount doesn't matter here */
		STACK_WIND (frame,
			    unify_open_readlink_cbk,
			    NS(this),
			    NS(this)->fops->readlink,
			    loc, ZR_PATH_MAX);
		return 0;
	}
#endif /* GF_DARWIN_HOST_OS */

	local->call_count = 2;
	for (index = 0; file_list[index] != -1; index++) {
		char need_break = (file_list[index+1] == -1);
		STACK_WIND_COOKIE (frame,
				   unify_open_cbk,
				   priv->xl_array[file_list[index]], //cookie
				   priv->xl_array[file_list[index]],
				   priv->xl_array[file_list[index]]->fops->open,
				   loc,
				   flags,
				   fd);
		if (need_break)
			break;
	}

	return 0;
}


int32_t 
unify_create_unlink_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno)
{
	unify_local_t *local = frame->local;
	inode_t *inode = local->loc1.inode;

	unify_local_wipe (local);

	STACK_UNWIND (frame, local->op_ret, local->op_errno, local->fd, 
		      inode, &local->stbuf);
  
	return 0;
}

/**
 * unify_create_open_cbk -
 */
int32_t
unify_create_open_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       fd_t *fd)
{
	int ret = 0;
	int32_t callcnt = 0;
	unify_local_t *local = frame->local;
	inode_t *inode = NULL;
	xlator_t *child = NULL;
	LOCK (&frame->lock);
	{
		if (op_ret >= 0) {
			local->op_ret = op_ret;
			if (NS(this) != (xlator_t *)cookie) {
				/* Store child node's ptr, used in all 
				   the f*** / FileIO calls */
				/* TODO: log on failure */
				ret = dict_set_static_ptr (fd->ctx, 
							   this->name, 
							   cookie);
			}
		} else {
			gf_log (this->name, GF_LOG_ERROR,
				"child(%s): path(%s): %s", 
				((xlator_t *)cookie)->name,
				local->loc1.path, strerror (op_errno));
			local->op_errno = op_errno;
			local->failed = 1;
		}
		callcnt = --local->call_count;
	}
	UNLOCK (&frame->lock);
  
	if (!callcnt) {
		if (local->failed == 1 && (local->op_ret >= 0)) {
			local->call_count = 1;
			/* return -1 to user */
			local->op_ret = -1;
			local->op_errno = EIO;
			local->fd = fd;
			local->call_count = 1;
			if (dict_get (local->fd->ctx, this->name)) {
				child = data_to_ptr (dict_get (local->fd->ctx,
							       this->name));

				gf_log (this->name, GF_LOG_ERROR, 
					"Create success on child node, "
					"failed on namespace");

				STACK_WIND (frame,
					    unify_create_unlink_cbk,
					    child,
					    child->fops->unlink,
					    &local->loc1);
			} else {
				gf_log (this->name, GF_LOG_ERROR, 
					"Create success on namespace, "
					"failed on child node");

				STACK_WIND (frame,
					    unify_create_unlink_cbk,
					    NS(this),
					    NS(this)->fops->unlink,
					    &local->loc1);
			}
			return 0;
		}
		inode = local->loc1.inode;
		unify_local_wipe (local);
		STACK_UNWIND (frame, local->op_ret, local->op_errno, fd,
			      inode, &local->stbuf);
	}
	return 0;
}

/**
 * unify_create_lookup_cbk - 
 */
int32_t 
unify_create_lookup_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 inode_t *inode,
			 struct stat *buf,
			 dict_t *dict)
{
	int ret = 0;
	int32_t callcnt = 0;
	int16_t index = 0;
	unify_private_t *priv = this->private;
	unify_local_t *local = frame->local;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"child(%s): path(%s): %s", 
				priv->xl_array[(long)cookie]->name, 
				local->loc1.path, strerror (op_errno));
			local->op_errno = op_errno;
			local->failed = 1;
		}

		if (op_ret >= 0) {
			local->op_ret = op_ret; 
			local->list[local->index++] = (int16_t)(long)cookie;
			if (NS(this) == priv->xl_array[(long)cookie]) {
				local->st_ino = buf->st_ino;
			} else {
				local->stbuf = *buf;
			}
		}
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		int16_t *list = local->list;
		int16_t file_list[3] = {0,};
		local->op_ret = -1;

		local->list [local->index] = -1;
		file_list[0] = list[0];
		file_list[1] = list[1];
		file_list[2] = -1;

		local->stbuf.st_ino = local->st_ino;
		/* TODO: log on failure */
		ret = dict_set_ptr (local->loc1.inode->ctx, 
				    this->name, local->list);

		if (local->index != 2) {
			/* Lookup failed, can't do open */
			gf_log (this->name, GF_LOG_ERROR,
				"%s: present on %d nodes", 
				local->loc1.path, local->index);
			file_list[0] = priv->child_count;
			for (index = 0; list[index] != -1; index++) {
				gf_log (this->name, GF_LOG_ERROR, 
					"%s: found on %s", local->loc1.path, 
					priv->xl_array[list[index]]->name);
				if (list[index] != priv->child_count)
					file_list[1] = list[index];
			}

			if (local->index < 2) {
				unify_local_wipe (local);
				gf_log (this->name, GF_LOG_ERROR,
					"returning EIO as file found on "
					"only one node");
				STACK_UNWIND (frame, -1, EIO, 
					      local->fd, inode, NULL);
				return 0;
			}
		}
		/* Everything is perfect :) */    
		local->call_count = 2;
    
		for (index = 0; file_list[index] != -1; index++) {
			char need_break = (file_list[index+1] == -1);
			STACK_WIND_COOKIE (frame,
					   unify_create_open_cbk,
					   priv->xl_array[file_list[index]],
					   priv->xl_array[file_list[index]],
					   priv->xl_array[file_list[index]]->fops->open,
					   &local->loc1,
					   local->flags,
					   local->fd);
			if (need_break)
				break;
		}
	}

	return 0;
}


/**
 * unify_create_cbk -
 */
int32_t
unify_create_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  fd_t *fd,
		  inode_t *inode,
		  struct stat *buf)
{
	int ret = 0;
	unify_local_t *local = frame->local;
	call_frame_t *prev_frame = cookie;
	inode_t *tmp_inode = NULL;

	if (op_ret == -1) {
		/* send unlink () on Namespace */
		local->op_errno = op_errno;
		local->op_ret = -1;
		local->call_count = 1;
		gf_log (this->name, GF_LOG_ERROR,
			"create failed on %s (file %s, error %s), "
			"sending unlink to namespace", 
			prev_frame->this->name, 
			local->loc1.path, strerror (op_errno));

		STACK_WIND (frame,
			    unify_create_unlink_cbk,
			    NS(this),
			    NS(this)->fops->unlink,
			    &local->loc1);

		return 0;
	}

	if (op_ret >= 0) {
		local->op_ret = op_ret;
		local->stbuf = *buf;
		/* Just inode number should be from NS node */
		local->stbuf.st_ino = local->st_ino;

		/* TODO: log on failure */
		ret = dict_set_static_ptr (fd->ctx, this->name, 
					   prev_frame->this);
	}
  
	tmp_inode = local->loc1.inode;
	unify_local_wipe (local);
	STACK_UNWIND (frame, local->op_ret, local->op_errno, local->fd, 
		      tmp_inode, &local->stbuf);

	return 0;
}

/**
 * unify_ns_create_cbk -
 * 
 */
int32_t
unify_ns_create_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     fd_t *fd,
		     inode_t *inode,
		     struct stat *buf)
{
	struct sched_ops *sched_ops = NULL;
	xlator_t *sched_xl = NULL;
	unify_local_t *local = frame->local;
	unify_private_t *priv = this->private;
	int16_t *list = NULL;
	int16_t index = 0;

	if (op_ret == -1) {
		/* No need to send create request to other servers, as 
		   namespace action failed. Handle exclusive create here. */
		if ((op_errno != EEXIST) || 
		    ((op_errno == EEXIST) && 
		     ((local->flags & O_EXCL) == O_EXCL))) {
			/* If its just a create call without O_EXCL, 
			   don't do this */
			gf_log (this->name, GF_LOG_ERROR,
				"namespace: path(%s): %s", 
				local->loc1.path, strerror (op_errno));
			unify_local_wipe (local);
			STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf);
			return 0;
		}
	}
  
	if (op_ret >= 0) {
		/* Get the inode number from the NS node */
		local->st_ino = buf->st_ino;
  
		local->op_ret = -1;

		/* Start the mapping list */
		list = calloc (1, sizeof (int16_t) * 3);
		ERR_ABORT (list);
		dict_set (inode->ctx, this->name, data_from_ptr (list));
		list[0] = priv->child_count;
		list[2] = -1;

		/* This means, file doesn't exist anywhere in the Filesystem */
		sched_ops = priv->sched_ops;

		/* Send create request to the scheduled node now */
		sched_xl = sched_ops->schedule (this, local->loc1.path);
		if (sched_xl == NULL)
		{
			/* send unlink () on Namespace */
			local->op_errno = ENOTCONN;
			local->op_ret = -1;
			local->call_count = 1;
			gf_log (this->name, GF_LOG_ERROR,
				"no node online to schedule create:(file %s) "
				"sending unlink to namespace", 
				(local->loc1.path)?local->loc1.path:"");

			STACK_WIND (frame,
				    unify_create_unlink_cbk,
				    NS(this),
				    NS(this)->fops->unlink,
				    &local->loc1);
	
			return 0;
		}

		for (index = 0; index < priv->child_count; index++)
			if (sched_xl == priv->xl_array[index])
				break;
		list[1] = index;

		STACK_WIND (frame, unify_create_cbk,
			    sched_xl, sched_xl->fops->create,
			    &local->loc1, local->flags, local->mode, fd);
	} else {
		/* File already exists, and there is no O_EXCL flag */

		gf_log (this->name, GF_LOG_DEBUG, 
			"File(%s) already exists on namespace, sending "
			"open instead", local->loc1.path);

		local->list = calloc (1, sizeof (int16_t) * 3);
		ERR_ABORT (local->list);
		local->call_count = priv->child_count + 1;
		local->op_ret = -1;
		for (index = 0; index <= priv->child_count; index++) {
			/* Send lookup() to all nodes including namespace */
			STACK_WIND_COOKIE (frame,
					   unify_create_lookup_cbk,
					   (void *)(long)index,
					   priv->xl_array[index],
					   priv->xl_array[index]->fops->lookup,
					   &local->loc1,
					   0);
		}
	}
	return 0;
}

/**
 * unify_create - create a file in global namespace first, so other 
 *    clients can see them. Create the file in storage nodes in background.
 */
int32_t
unify_create (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc,
	      int32_t flags,
	      mode_t mode,
	      fd_t *fd)
{
	unify_local_t *local = NULL;
  
	/* Initialization */
	INIT_LOCAL (frame, local);
	local->mode = mode;
	local->flags = flags;
	local->fd = fd;

	loc_copy (&local->loc1, loc);
	if (local->loc1.path == NULL) {
		gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
		STACK_UNWIND (frame, -1, ENOMEM, fd, loc->inode, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    unify_ns_create_cbk,
		    NS(this),
		    NS(this)->fops->create,
		    loc,
		    flags | O_EXCL,
		    mode,
		    fd);
  
	return 0;
}


/**
 * unify_opendir_cbk - 
 */
int32_t
unify_opendir_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   fd_t *fd)
{
	STACK_UNWIND (frame, op_ret, op_errno, fd);

	return 0;
}

/** 
 * unify_opendir -
 */
int32_t
unify_opendir (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       fd_t *fd)
{
	UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	STACK_WIND (frame, unify_opendir_cbk,
		    NS(this), NS(this)->fops->opendir, loc, fd);

	return 0;
}


/**
 * unify_chmod - 
 */
int32_t
unify_chmod (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode)
{
	unify_local_t *local = NULL;
	unify_private_t *priv = this->private;
	int32_t index = 0;
	int32_t callcnt = 0;
  
	UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	/* Initialization */
	INIT_LOCAL (frame, local);

	if (S_ISDIR (loc->inode->st_mode)) {
		local->call_count = priv->child_count + 1;
      
		for (index = 0; index < (priv->child_count + 1); index++) {
			STACK_WIND (frame,
				    unify_buf_cbk,
				    priv->xl_array[index],
				    priv->xl_array[index]->fops->chmod,
				    loc, mode);
		}    
	} else {
		local->list = data_to_ptr (dict_get (loc->inode->ctx, 
						     this->name));
      
		for (index = 0; local->list[index] != -1; index++) {
			local->call_count++;
			callcnt++;
		}
      
		for (index = 0; local->list[index] != -1; index++) {
			STACK_WIND (frame,
				    unify_buf_cbk,
				    priv->xl_array[local->list[index]],
				    priv->xl_array[local->list[index]]->fops->chmod,
				    loc,
				    mode);
			if (!--callcnt)
				break;
		}
	}

	return 0;
}

/**
 * unify_chown - 
 */
int32_t
unify_chown (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     uid_t uid,
	     gid_t gid)
{
	unify_local_t *local = NULL;
	unify_private_t *priv = this->private;
	int32_t index = 0;
	int32_t callcnt = 0;
  
	UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	/* Initialization */
	INIT_LOCAL (frame, local);

	if (S_ISDIR (loc->inode->st_mode)) {
		local->call_count = priv->child_count + 1;
      
		for (index = 0; index < (priv->child_count + 1); index++) {
			STACK_WIND (frame,
				    unify_buf_cbk,
				    priv->xl_array[index],
				    priv->xl_array[index]->fops->chown,
				    loc, uid, gid);
		}    
	} else {
		local->list = data_to_ptr (dict_get (loc->inode->ctx, 
						     this->name));
      
		for (index = 0; local->list[index] != -1; index++) {
			local->call_count++;
			callcnt++;
		}
      
		for (index = 0; local->list[index] != -1; index++) {
			STACK_WIND (frame,
				    unify_buf_cbk,
				    priv->xl_array[local->list[index]],
				    priv->xl_array[local->list[index]]->fops->chown,
				    loc, uid, gid);
			if (!--callcnt)
				break;
		}
	}

	return 0;
}


/**
 * unify_buf_cbk - 
 */
int32_t
unify_truncate_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    struct stat *buf)
{
	int32_t callcnt = 0;
	unify_private_t *priv = this->private;
	unify_local_t *local = frame->local;
	call_frame_t *prev_frame = cookie;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
    
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_ERROR,
				"child(%s): path(%s): %s", 
				prev_frame->this->name, 
				(local->loc1.path)?local->loc1.path:"", 
				strerror (op_errno));
			local->op_errno = op_errno;
			if (!((op_errno == ENOENT) && priv->optimist))
				local->op_ret = -1;
		}

		if (op_ret >= 0) {
			if (NS (this) == prev_frame->this) {
				local->st_ino = buf->st_ino;
				/* If the entry is directory, get the 
				   stat from NS node */
				if (S_ISDIR (buf->st_mode) || 
				    !local->stbuf.st_blksize) {
					local->stbuf = *buf;
				}
			}

			if ((!S_ISDIR (buf->st_mode)) && 
			    (NS (this) != prev_frame->this)) {
				/* If file, take the stat info from 
				   Storage node. */
				local->stbuf = *buf;
			}
		}
	}
	UNLOCK (&frame->lock);
    
	if (!callcnt) {
		local->stbuf.st_ino = local->st_ino;
		unify_local_wipe (local);
		STACK_UNWIND (frame, local->op_ret, local->op_errno, 
			      &local->stbuf);
	}

	return 0;
}

/**
 * unify_truncate - 
 */
int32_t
unify_truncate (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		off_t offset)
{
	unify_local_t *local = NULL;
	unify_private_t *priv = this->private;
	int32_t index = 0;
	int32_t callcnt = 0;
  
	UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	/* Initialization */
	INIT_LOCAL (frame, local);

	if (S_ISDIR (loc->inode->st_mode)) {
		local->call_count = 1;
      
		STACK_WIND (frame,
			    unify_buf_cbk,
			    NS(this),
			    NS(this)->fops->stat,
			    loc);
	} else {
		local->op_ret = 0;
		local->list = data_to_ptr (dict_get (loc->inode->ctx, 
						     this->name));
      
		for (index = 0; local->list[index] != -1; index++) {
			local->call_count++;
			callcnt++;
		}
      
		/* Don't send truncate to NS node */
		STACK_WIND (frame, unify_truncate_cbk, NS(this),
			    NS(this)->fops->stat, loc);
		callcnt--;

		for (index = 0; local->list[index] != -1; index++) {
			if (NS(this) != priv->xl_array[local->list[index]]) {
				STACK_WIND (frame,
					    unify_truncate_cbk,
					    priv->xl_array[local->list[index]],
					    priv->xl_array[local->list[index]]->fops->truncate,
					    loc,
					    offset);
				if (!--callcnt)
					break;
			}
		}
	}

	return 0;
}

/**
 * unify_utimens - 
 */
int32_t 
unify_utimens (call_frame_t *frame,
	       xlator_t *this,
	       loc_t *loc,
	       struct timespec tv[2])
{
	unify_local_t *local = NULL;
	unify_private_t *priv = this->private;
	int32_t index = 0;
	int32_t callcnt = 0;
  
	UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	/* Initialization */
	INIT_LOCAL (frame, local);

	if (S_ISDIR (loc->inode->st_mode)) {
		local->call_count = priv->child_count + 1;
      
		for (index = 0; index < (priv->child_count + 1); index++) {
			STACK_WIND (frame,
				    unify_buf_cbk,
				    priv->xl_array[index],
				    priv->xl_array[index]->fops->utimens,
				    loc, tv);
		}
	} else {
		local->list = data_to_ptr (dict_get (loc->inode->ctx, 
						     this->name));
      
		for (index = 0; local->list[index] != -1; index++) {
			local->call_count++;
			callcnt++;
		}
      
		for (index = 0; local->list[index] != -1; index++) {
			STACK_WIND (frame,
				    unify_buf_cbk,
				    priv->xl_array[local->list[index]],
				    priv->xl_array[local->list[index]]->fops->utimens,
				    loc,
				    tv);
			if (!--callcnt)
				break;
		}
	}
  
	return 0;
}

/**
 * unify_readlink_cbk - 
 */
int32_t
unify_readlink_cbk (call_frame_t *frame,
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
 * unify_readlink - Read the link only from the storage node.
 */
int32_t
unify_readlink (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		size_t size)
{
	unify_private_t *priv = this->private;
	int32_t entry_count = 0;
	int16_t *list = NULL;
	int16_t index = 0;
  
	UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

	for (index = 0; list[index] != -1; index++)
		entry_count++;

	if (entry_count >= 2) {
		for (index = 0; list[index] != -1; index++) {
			if (priv->xl_array[list[index]] != NS(this)) {
				STACK_WIND (frame,
					    unify_readlink_cbk,
					    priv->xl_array[list[index]],
					    priv->xl_array[list[index]]->fops->readlink,
					    loc,
					    size);
				break;
			}
		}
	} else {
		gf_log (this->name, GF_LOG_ERROR, 
			"returning ENOENT, no softlink files found "
			"on storage node");
		STACK_UNWIND (frame, -1, ENOENT, NULL);
	}

	return 0;
}


/**
 * unify_unlink_cbk - 
 */
int32_t
unify_unlink_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno)
{
	int32_t callcnt = 0;
	unify_private_t *priv = this->private;
	unify_local_t *local = frame->local;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
		if (op_ret == 0  || ((op_errno == ENOENT) && priv->optimist))
			local->op_ret = 0;
		if (op_ret == -1)
			local->op_errno = op_errno;
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		unify_local_wipe (local);
		STACK_UNWIND (frame, local->op_ret, local->op_errno);
	}

	return 0;
}


/**
 * unify_unlink - 
 */
int32_t
unify_unlink (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *loc)
{
	unify_private_t *priv = this->private;
	unify_local_t *local = NULL;
	int16_t *list = NULL;
	int16_t index = 0;

	UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	/* Initialization */
	INIT_LOCAL (frame, local);
	loc_copy (&local->loc1, loc);

	list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

	for (index = 0; list[index] != -1; index++)
		local->call_count++;

	if (local->call_count) {
		for (index = 0; list[index] != -1; index++) {
			char need_break = (list[index+1] == -1);
			STACK_WIND (frame,
				    unify_unlink_cbk,
				    priv->xl_array[list[index]],
				    priv->xl_array[list[index]]->fops->unlink,
				    loc);
			if (need_break)
				break;
		}
	} else {
		gf_log (this->name, GF_LOG_ERROR,
			"%s: returning ENOENT", loc->path);
		STACK_UNWIND (frame, -1, ENOENT);
	}

	return 0;
}


/**
 * unify_readv_cbk - 
 */
int32_t
unify_readv_cbk (call_frame_t *frame,
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
 * unify_readv - 
 */
int32_t
unify_readv (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     size_t size,
	     off_t offset)
{
	xlator_t *child = NULL;

	UNIFY_CHECK_FD_CTX_AND_UNWIND_ON_ERR (fd);

	child = data_to_ptr (dict_get (fd->ctx, this->name));

	STACK_WIND (frame,
		    unify_readv_cbk,
		    child,
		    child->fops->readv,
		    fd,
		    size,
		    offset);


	return 0;
}

/**
 * unify_writev_cbk - 
 */
int32_t
unify_writev_cbk (call_frame_t *frame,
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
 * unify_writev - 
 */
int32_t
unify_writev (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      struct iovec *vector,
	      int32_t count,
	      off_t off)
{
	xlator_t *child = NULL;

	UNIFY_CHECK_FD_CTX_AND_UNWIND_ON_ERR (fd);

	child = data_to_ptr (dict_get (fd->ctx, this->name));

	STACK_WIND (frame,
		    unify_writev_cbk,
		    child,
		    child->fops->writev,
		    fd,
		    vector,
		    count,
		    off);

	return 0;
}

/**
 * unify_ftruncate -
 */
int32_t
unify_ftruncate (call_frame_t *frame,
		 xlator_t *this,
		 fd_t *fd,
		 off_t offset)
{
	xlator_t *child = NULL;
	unify_local_t *local = NULL;

	UNIFY_CHECK_FD_CTX_AND_UNWIND_ON_ERR(fd);

	/* Initialization */
	INIT_LOCAL (frame, local);
	local->op_ret = 0;

	child = data_to_ptr (dict_get (fd->ctx, this->name));
	local->call_count = 2;
  
	STACK_WIND (frame, unify_truncate_cbk, 
		    child, child->fops->ftruncate,
		    fd, offset);
  
	STACK_WIND (frame, unify_truncate_cbk, 
		    NS(this), NS(this)->fops->fstat,
		    fd);
  
	return 0;
}


/**
 * unify_fchmod - 
 */
int32_t 
unify_fchmod (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      mode_t mode)
{
	unify_local_t *local = NULL;
	xlator_t *child = NULL;	

	UNIFY_CHECK_FD_AND_UNWIND_ON_ERR(fd);

	/* Initialization */
	INIT_LOCAL (frame, local);

	if (dict_get (fd->ctx, this->name)) {
		/* If its set, then its file */
		child = data_to_ptr (dict_get (fd->ctx, this->name));
		local->call_count = 2;

		STACK_WIND (frame, unify_buf_cbk, child, 
			    child->fops->fchmod, fd, mode);

		STACK_WIND (frame, unify_buf_cbk, NS(this),	
			    NS(this)->fops->fchmod,	fd, mode);

	} else {
		/* this is an directory */
		local->call_count = 1;
    
		STACK_WIND (frame, unify_buf_cbk,
			    NS(this), NS(this)->fops->fchmod, fd, mode);
	}

	return 0;
}

/**
 * unify_fchown - 
 */
int32_t 
unify_fchown (call_frame_t *frame,
	      xlator_t *this,
	      fd_t *fd,
	      uid_t uid,
	      gid_t gid)
{
	unify_local_t *local = NULL;
	xlator_t *child = NULL;

	UNIFY_CHECK_FD_AND_UNWIND_ON_ERR(fd);

	/* Initialization */
	INIT_LOCAL (frame, local);

	if (dict_get (fd->ctx, this->name)) {
		/* If its set, then its file */
		child = data_to_ptr (dict_get (fd->ctx, this->name));
		local->call_count = 2;

		STACK_WIND (frame, unify_buf_cbk, child,
			    child->fops->fchown, fd, uid, gid);

		STACK_WIND (frame, unify_buf_cbk, NS(this),
			    NS(this)->fops->fchown,	fd, uid, gid);
	} else {
		local->call_count = 1;
    
		STACK_WIND (frame, unify_buf_cbk,
			    NS(this), NS(this)->fops->fchown,
			    fd, uid, gid);
	}
  
	return 0;
}

/**
 * unify_flush_cbk - 
 */
int32_t
unify_flush_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

/**
 * unify_flush -
 */
int32_t
unify_flush (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd)
{
	xlator_t *child = NULL;

	UNIFY_CHECK_FD_CTX_AND_UNWIND_ON_ERR(fd);

	child = data_to_ptr (dict_get (fd->ctx, this->name));

	STACK_WIND (frame, unify_flush_cbk, child, 
		    child->fops->flush, fd);

	return 0;
}


/**
 * unify_fsync_cbk - 
 */
int32_t
unify_fsync_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno)
{
	STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}

/**
 * unify_fsync - 
 */
int32_t
unify_fsync (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd,
	     int32_t flags)
{
	xlator_t *child = NULL;

	UNIFY_CHECK_FD_CTX_AND_UNWIND_ON_ERR(fd);

	child = data_to_ptr (dict_get (fd->ctx, this->name));

	STACK_WIND (frame, unify_fsync_cbk, child,
		    child->fops->fsync, fd, flags);

	return 0;
}

/**
 * unify_fstat - Send fstat FOP to Namespace only if its directory, and to 
 *     both namespace and the storage node if its a file.
 */
int32_t
unify_fstat (call_frame_t *frame,
	     xlator_t *this,
	     fd_t *fd)
{
	unify_local_t *local = NULL;
	xlator_t *child = NULL;

	UNIFY_CHECK_FD_AND_UNWIND_ON_ERR(fd);

	INIT_LOCAL (frame, local);

	if (dict_get (fd->ctx, this->name)) {
		/* If its set, then its file */
		child = data_to_ptr (dict_get (fd->ctx, this->name));
		local->call_count = 2;

		STACK_WIND (frame, unify_buf_cbk, child,
			    child->fops->fstat, fd);

		STACK_WIND (frame, unify_buf_cbk, NS(this),
			    NS(this)->fops->fstat, fd);

	} else {
		/* this is an directory */
		local->call_count = 1;
		STACK_WIND (frame, unify_buf_cbk, NS(this),
			    NS(this)->fops->fstat, fd);
	}

	return 0;
}

/**
 * unify_getdents_cbk - 
 */
int32_t
unify_getdents_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    dir_entry_t *entry,
		    int32_t count)
{
	STACK_UNWIND (frame, op_ret, op_errno, entry, count);
	return 0;
}

/**
 * unify_getdents - send the FOP request to all the nodes.
 */
int32_t
unify_getdents (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		size_t size,
		off_t offset,
		int32_t flag)
{
	UNIFY_CHECK_FD_AND_UNWIND_ON_ERR (fd);

	STACK_WIND (frame, unify_getdents_cbk, NS(this),
		    NS(this)->fops->getdents, fd, size, offset, flag);

	return 0;
}


/**
 * unify_readdir_cbk - 
 */
int32_t
unify_readdir_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   gf_dirent_t *buf)
{
	STACK_UNWIND (frame, op_ret, op_errno, buf);

	return 0;
}

/**
 * unify_readdir - send the FOP request to all the nodes.
 */
int32_t
unify_readdir (call_frame_t *frame,
	       xlator_t *this,
	       fd_t *fd,
	       size_t size,
	       off_t offset)
{
	UNIFY_CHECK_FD_AND_UNWIND_ON_ERR (fd);

	STACK_WIND (frame, unify_readdir_cbk, NS(this),
		    NS(this)->fops->readdir, fd, size, offset);

	return 0;
}


/**
 * unify_fsyncdir_cbk - 
 */
int32_t
unify_fsyncdir_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
	STACK_UNWIND (frame, op_ret, op_errno);

	return 0;
}

/**
 * unify_fsyncdir -
 */
int32_t
unify_fsyncdir (call_frame_t *frame,
		xlator_t *this,
		fd_t *fd,
		int32_t flags)
{
	UNIFY_CHECK_FD_AND_UNWIND_ON_ERR (fd);

	STACK_WIND (frame, unify_fsyncdir_cbk,
		    NS(this), NS(this)->fops->fsyncdir, fd, flags);

	return 0;
}

/**
 * unify_lk_cbk - UNWIND frame with the proper return arguments.
 */
int32_t
unify_lk_cbk (call_frame_t *frame,
	      void *cookie,
	      xlator_t *this,
	      int32_t op_ret,
	      int32_t op_errno,
	      struct flock *lock)
{
	STACK_UNWIND (frame, op_ret, op_errno, lock);
	return 0;
}

/**
 * unify_lk - Send it to all the storage nodes, (should be 1) which has file.
 */
int32_t
unify_lk (call_frame_t *frame,
	  xlator_t *this,
	  fd_t *fd,
	  int32_t cmd,
	  struct flock *lock)
{
	xlator_t *child = NULL;

	UNIFY_CHECK_FD_CTX_AND_UNWIND_ON_ERR (fd);

	child = data_to_ptr (dict_get (fd->ctx, this->name));

	STACK_WIND (frame, unify_lk_cbk, child,
		    child->fops->lk, fd, cmd, lock);

	return 0;
}

#include <attr/xattr.h>

int32_t
unify_setxattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno);

static int32_t
unify_setxattr_file_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno)
{
	unify_private_t *private = this->private;
	unify_local_t *local = frame->local;
	xlator_t *sched_xl = NULL;
	struct sched_ops *sched_ops = NULL;

	if (op_ret == -1) {
		if (!ENOTSUP)
			gf_log (this->name, GF_LOG_ERROR,
				"setxattr with XATTR_CREATE on ns: "
				"path(%s) key(%s): %s",
				local->loc1.path, local->name, 
				strerror (op_errno));
		unify_local_wipe (local);
		STACK_UNWIND (frame, op_ret, op_errno);
		return 0;
	} 

	LOCK (&frame->lock);
	{
		local->failed = 0;
		local->op_ret = 0;
		local->op_errno = 0;
		local->call_count = 1;
	}
	UNLOCK (&frame->lock);

	/* schedule XATTR_CREATE on one of the child node */
	sched_ops = private->sched_ops;
    
	/* Send create request to the scheduled node now */
	sched_xl = sched_ops->schedule (this, local->name); 
	if (!sched_xl) {
		STACK_UNWIND (frame, -1, ENOTCONN);
		return 0;
	}

	STACK_WIND (frame,
		    unify_setxattr_cbk,
		    sched_xl,
		    sched_xl->fops->setxattr,
		    &local->loc1,
		    local->dict,
		    local->flags);
	return 0;
}

/**
 * unify_setxattr_cbk - When all the child nodes return, UNWIND frame.
 */
int32_t
unify_setxattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno)
{
	int32_t callcnt = 0;
	unify_local_t *local = frame->local;
	call_frame_t *prev_frame = cookie;
	dict_t *dict = NULL;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
    
		if (op_ret == -1) {
			gf_log (this->name, (((op_errno == ENOENT) || 
					      (op_errno == ENOTSUP))? 
					     GF_LOG_DEBUG : GF_LOG_ERROR), 
				"child(%s): path(%s): %s", 
				prev_frame->this->name, 
				(local->loc1.path)?local->loc1.path:"", 
				strerror (op_errno));
			if (local->failed == -1) {
				local->failed = 1;
			}
			local->op_errno = op_errno;
		} else {
			local->failed = 0;
			local->op_ret = op_ret;
		}
	}
	UNLOCK (&frame->lock);
  
	if (!callcnt) {
		if (local->failed && local->name && 
		    ZR_FILE_CONTENT_REQUEST(local->name)) {      
			dict = get_new_dict ();
			dict_set (dict, local->dict->members_list->key, 
				  data_from_dynptr(NULL, 0));
			dict_ref (dict);

			local->call_count = 1;

			STACK_WIND (frame,
				    unify_setxattr_file_cbk,
				    NS(this),
				    NS(this)->fops->setxattr,
				    &local->loc1,
				    dict,
				    XATTR_CREATE);

			dict_unref (dict);
			return 0;
		}
    
		unify_local_wipe (local);
		STACK_UNWIND (frame, local->op_ret, local->op_errno);
	}

	return 0;
}



#include <attr/xattr.h>
/**
 * unify_sexattr - This function should be sent to all the storage nodes, 
 *       which contains the file, (excluding namespace).
 */
int32_t
unify_setxattr (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		dict_t *dict,
		int32_t flags)
{
	unify_private_t *priv = this->private;
	unify_local_t *local = NULL;
	int16_t *list = NULL;
	int16_t index = 0;
	int32_t call_count = 0;
	data_pair_t *trav = dict->members_list;

	UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	/* Initialization */
	INIT_LOCAL (frame, local);
	local->failed = -1;
	loc_copy (&local->loc1, loc);

	if (S_ISDIR (loc->inode->st_mode)) {

		if (ZR_FILE_CONTENT_REQUEST(trav->key)) {
			/* direct the storage xlators to change file 
			   content only if file exists */
			local->flags = flags;
			local->dict = dict;
			local->name = strdup (trav->key);
			flags |= XATTR_REPLACE;
		}

		local->call_count = priv->child_count;
		for (index = 0; index < priv->child_count; index++) {
			STACK_WIND (frame,
				    unify_setxattr_cbk,
				    priv->xl_array[index],
				    priv->xl_array[index]->fops->setxattr,
				    loc, dict, flags);
		}
		return 0;
	}

	list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

	for (index = 0; list[index] != -1; index++) {
		if (NS(this) != priv->xl_array[list[index]]) {
			local->call_count++;
			call_count++;
		}
	}
  
	if (local->call_count) {
		for (index = 0; list[index] != -1; index++) {
			if (priv->xl_array[list[index]] != NS(this)) {
				STACK_WIND (frame,
					    unify_setxattr_cbk,
					    priv->xl_array[list[index]],
					    priv->xl_array[list[index]]->fops->setxattr,
					    loc,
					    dict,
					    flags);
				if (!--call_count)
					break;
			}
		}
		return 0;
	}

	/* No entry in storage nodes */
	gf_log (this->name, GF_LOG_DEBUG, 
		"returning ENOENT, file not found on storage node.");
	STACK_UNWIND (frame, -1, ENOENT);

	return 0;
}


/**
 * unify_getxattr_cbk - This function is called from only one child, so, no
 *     need of any lock or anything else, just send it to above layer 
 */
int32_t
unify_getxattr_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    dict_t *value)
{
	int32_t callcnt = 0;
	dict_t *local_value = NULL;
	unify_local_t *local = frame->local;
	call_frame_t *prev_frame = cookie;
  
	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
    
		if (op_ret == -1) {
			local->op_errno = op_errno;
			gf_log (this->name, 
				(((op_errno == ENOENT) || 
				  (op_errno == ENODATA) || 
				  (op_errno == ENOTSUP)) ? 
				 GF_LOG_DEBUG : GF_LOG_ERROR), 
				"child(%s): path(%s): %s", 
				prev_frame->this->name, 
				(local->loc1.path)?local->loc1.path:"", 
				strerror (op_errno));
		} else {
			if (!local->dict)
				local->dict = dict_ref (value);
			local->op_ret = op_ret;
		}
	}
	UNLOCK (&frame->lock);
  
	if (!callcnt) {
		local_value = local->dict;
		local->dict = NULL;
      
		STACK_UNWIND (frame, local->op_ret, local->op_errno, 
			      local_value);
      
		if (local_value)
			dict_unref (local_value);
	} 

	return 0;
}


/** 
 * unify_getxattr - This FOP is sent to only the storage node.
 */
int32_t
unify_getxattr (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		const char *name)
{
	unify_private_t *priv = this->private;
	int16_t *list = NULL;
	int16_t index = 0;
	int16_t count = 0;
	unify_local_t *local = NULL;

	UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);
	INIT_LOCAL (frame, local);

	if (S_ISDIR (loc->inode->st_mode)) {
		local->call_count = priv->child_count;
		for (index = 0; index < priv->child_count; index++)
			STACK_WIND (frame,
				    unify_getxattr_cbk,
				    priv->xl_array[index],
				    priv->xl_array[index]->fops->getxattr,
				    loc,
				    name);
		return 0;
	}

	list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

	for (index = 0; list[index] != -1; index++) {
		if (NS(this) != priv->xl_array[list[index]]) {
			local->call_count++;
			count++;
		}
	}

	if (count) {
		for (index = 0; list[index] != -1; index++) {
			if (priv->xl_array[list[index]] != NS(this)) {
				STACK_WIND (frame,
					    unify_getxattr_cbk,
					    priv->xl_array[list[index]],
					    priv->xl_array[list[index]]->fops->getxattr,
					    loc,
					    name);
				if (!--count)
					break;
			}
		}
	} else {
		dict_t *tmp_dict = get_new_dict ();
		gf_log (this->name, GF_LOG_DEBUG, 
			"%s: returning ENODATA, no file found on storage node",
			loc->path);
		STACK_UNWIND (frame, -1, ENODATA, tmp_dict);
		dict_destroy (tmp_dict);
	}

	return 0;
}

/**
 * unify_removexattr_cbk - Wait till all the child node returns the call
 *      and then UNWIND to above layer.
 */
int32_t
unify_removexattr_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
	int32_t callcnt = 0;
	unify_local_t *local = frame->local;
	call_frame_t *prev_frame = cookie;

	LOCK (&frame->lock);
	{ 
		callcnt = --local->call_count;
		if (op_ret == -1) {
			local->op_errno = op_errno;
			if (op_errno != ENOTSUP)
				gf_log (this->name, GF_LOG_ERROR, 
					"child(%s): path(%s): %s", 
					prev_frame->this->name, 
					local->loc1.path, strerror (op_errno));
		} else {
			local->op_ret = op_ret;
		}
	}
	UNLOCK (&frame->lock);  

	if (!callcnt) {
		STACK_UNWIND (frame, local->op_ret, local->op_errno);
	}

	return 0;
}

/**
 * unify_removexattr - Send it to all the child nodes which has the files.
 */
int32_t
unify_removexattr (call_frame_t *frame,
		   xlator_t *this,
		   loc_t *loc,
		   const char *name)
{
	unify_private_t *priv = this->private;
	unify_local_t *local = NULL;
	int16_t *list = NULL;
	int16_t index = 0;
	int32_t call_count = 0;

	UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (loc);

	/* Initialization */
	INIT_LOCAL (frame, local);

	if (S_ISDIR (loc->inode->st_mode)) {
		local->call_count = priv->child_count;
		for (index = 0; index < priv->child_count; index++)
			STACK_WIND (frame, 		    
				    unify_removexattr_cbk,
				    priv->xl_array[index],
				    priv->xl_array[index]->fops->removexattr,
				    loc,
				    name);

		return 0;
	}

	list = data_to_ptr (dict_get (loc->inode->ctx, this->name));

	for (index = 0; list[index] != -1; index++) {
		if (NS(this) != priv->xl_array[list[index]]) {
			local->call_count++;
			call_count++;
		}
	}

	if (local->call_count) {
		for (index = 0; list[index] != -1; index++) {
			if (priv->xl_array[list[index]] != NS(this)) {
				STACK_WIND (frame,
					    unify_removexattr_cbk,
					    priv->xl_array[list[index]],
					    priv->xl_array[list[index]]->fops->removexattr,
					    loc,
					    name);
				if (!--call_count)
					break;
			}
		}
		return 0;
	} 

	gf_log (this->name, GF_LOG_DEBUG, 
		"%s: returning ENOENT, not found on storage node.", loc->path);
	STACK_UNWIND (frame, -1, ENOENT);

	return 0;
}


int32_t 
unify_mknod_unlink_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
	unify_local_t *local = frame->local;

	if (op_ret == -1)
		gf_log (this->name, GF_LOG_ERROR, 
			"%s: %s", local->loc1.path, strerror (op_errno));
  
	unify_local_wipe (local);
	/* No log required here as this -1 is for mknod call */
	STACK_UNWIND (frame, -1, local->op_errno, NULL, NULL);
	return 0;
}

/**
 * unify_mknod_cbk - 
 */
int32_t
unify_mknod_cbk (call_frame_t *frame,
		 void *cookie,
		 xlator_t *this,
		 int32_t op_ret,
		 int32_t op_errno,
		 inode_t *inode,
		 struct stat *buf)
{
	unify_local_t *local = frame->local;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_ERROR, 
			"mknod failed on storage node, sending unlink to "
			"namespace");
		local->op_errno = op_errno;
		STACK_WIND (frame,
			    unify_mknod_unlink_cbk,
			    NS(this),
			    NS(this)->fops->unlink,
			    &local->loc1);
		return 0;
	}
  
	local->stbuf = *buf;
	local->stbuf.st_ino = local->st_ino;
	unify_local_wipe (local);
	STACK_UNWIND (frame, op_ret, op_errno, inode, &local->stbuf);
	return 0;
}

/**
 * unify_ns_mknod_cbk - 
 */
int32_t
unify_ns_mknod_cbk (call_frame_t *frame,
		    void *cookie,
		    xlator_t *this,
		    int32_t op_ret,
		    int32_t op_errno,
		    inode_t *inode,
		    struct stat *buf)
{
	struct sched_ops *sched_ops = NULL;
	xlator_t *sched_xl = NULL;
	unify_local_t *local = frame->local;
	unify_private_t *priv = this->private;
	int16_t *list = NULL;
	int16_t index = 0;
	call_frame_t *prev_frame = cookie;

	if (op_ret == -1) {
		/* No need to send mknod request to other servers, 
		 * as namespace action failed 
		 */
		gf_log (this->name, GF_LOG_ERROR, 
			"child(%s): path(%s): %s", 
			prev_frame->this->name, local->loc1.path, 
			strerror (op_errno));
		unify_local_wipe (local);
		STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
		return 0;
	}
  
	/* Create one inode for this entry */
	local->op_ret = 0;
	local->stbuf = *buf;
	local->st_ino = buf->st_ino;

	list = calloc (1, sizeof (int16_t) * 3);
	ERR_ABORT (list);
	list[0] = priv->child_count;
	list[2] = -1;
	dict_set (inode->ctx, this->name, data_from_ptr (list));

	sched_ops = priv->sched_ops;

	/* Send mknod request to scheduled node now */
	sched_xl = sched_ops->schedule (this, local->loc1.path); 
	if (!sched_xl) {
		gf_log (this->name, GF_LOG_ERROR, 
			"mknod failed on storage node, no node online "
			"at the moment, sending unlink to NS");
		local->op_errno = ENOTCONN;
		STACK_WIND (frame,
			    unify_mknod_unlink_cbk,
			    NS(this),
			    NS(this)->fops->unlink,
			    &local->loc1);
      
		return 0;
	}

	for (index = 0; index < priv->child_count; index++)
		if (sched_xl == priv->xl_array[index])
			break;
	list[1] = index;
  
	STACK_WIND (frame,  unify_mknod_cbk,
		    sched_xl,  sched_xl->fops->mknod,
		    &local->loc1, local->mode, local->dev);

	return 0;
}

/**
 * unify_mknod - Create a device on namespace first, and later create on 
 *       the storage node.
 */
int32_t
unify_mknod (call_frame_t *frame,
	     xlator_t *this,
	     loc_t *loc,
	     mode_t mode,
	     dev_t rdev)
{
	unify_local_t *local = NULL;
  
	/* Initialization */
	INIT_LOCAL (frame, local);
	local->mode = mode;
	local->dev = rdev;
	loc_copy (&local->loc1, loc);
	if (local->loc1.path == NULL) {
		gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
		STACK_UNWIND (frame, -1, ENOMEM, loc->inode, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    unify_ns_mknod_cbk,
		    NS(this),
		    NS(this)->fops->mknod,
		    loc,
		    mode,
		    rdev);

	return 0;
}

int32_t 
unify_symlink_unlink_cbk (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno)
{
	unify_local_t *local = frame->local;
	if (op_ret == -1)
		gf_log (this->name, GF_LOG_ERROR, 
			"%s: %s", local->loc1.path, strerror (op_errno));

	unify_local_wipe (local);
	STACK_UNWIND (frame, -1, local->op_errno, NULL, NULL);
	return 0;
}

/**
 * unify_symlink_cbk - 
 */
int32_t
unify_symlink_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
		   struct stat *buf)
{
	unify_local_t *local = frame->local;

	if (op_ret == -1) {
		/* Symlink on storage node failed, hence send unlink 
		   to the NS node */
		local->op_errno = op_errno;
		gf_log (this->name, GF_LOG_ERROR, 
			"symlink on storage node failed, sending unlink "
			"to namespace");

		STACK_WIND (frame,
			    unify_symlink_unlink_cbk,
			    NS(this),
			    NS(this)->fops->unlink,
			    &local->loc1);
    
		return 0;
	}
  
	local->stbuf = *buf;
	local->stbuf.st_ino = local->st_ino;
	unify_local_wipe (local);
	STACK_UNWIND (frame, op_ret, op_errno, inode, &local->stbuf);

	return 0;
}

/**
 * unify_ns_symlink_cbk - 
 */
int32_t
unify_ns_symlink_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      inode_t *inode,
		      struct stat *buf)
{

	struct sched_ops *sched_ops = NULL;
	xlator_t *sched_xl = NULL;
	int16_t *list = NULL;
	unify_local_t *local = frame->local;
	unify_private_t *priv = this->private;
	int16_t index = 0;

	if (op_ret == -1) {
		/* No need to send symlink request to other servers, 
		 * as namespace action failed 
		 */
		gf_log (this->name, GF_LOG_ERROR, 
			"namespace: path(%s): %s", 
			local->loc1.path, strerror (op_errno));
		unify_local_wipe (local);
		STACK_UNWIND (frame, op_ret, op_errno, NULL, buf);
		return 0;
	}
  
	/* Create one inode for this entry */
	local->op_ret = 0;
	local->st_ino = buf->st_ino;
  
	/* Start the mapping list */

	list = calloc (1, sizeof (int16_t) * 3);
	ERR_ABORT (list);
	list[0] = priv->child_count; //namespace's index
	list[2] = -1;
	dict_set (inode->ctx, this->name, data_from_ptr (list));

	sched_ops = priv->sched_ops;

	/* Send symlink request to all the nodes now */
	sched_xl = sched_ops->schedule (this, local->loc1.path); 
	if (!sched_xl) {
		/* Symlink on storage node failed, hence send unlink 
		   to the NS node */
		local->op_errno = ENOTCONN;
		gf_log (this->name, GF_LOG_ERROR, 
			"symlink on storage node failed, no node online, "
			"sending unlink to namespace");
      
		STACK_WIND (frame,
			    unify_symlink_unlink_cbk,
			    NS(this),
			    NS(this)->fops->unlink,
			    &local->loc1);
      
		return 0;
	}

	for (index = 0; index < priv->child_count; index++)
		if (sched_xl == priv->xl_array[index])
			break;
	list[1] = index;
	
	STACK_WIND (frame,
		    unify_symlink_cbk,
		    sched_xl,
		    sched_xl->fops->symlink,
		    local->name,
		    &local->loc1);

	return 0;
}

/**
 * unify_symlink - 
 */
int32_t
unify_symlink (call_frame_t *frame,
	       xlator_t *this,
	       const char *linkpath,
	       loc_t *loc)
{
	unify_local_t *local = NULL;
  
	/* Initialization */
	INIT_LOCAL (frame, local);
	loc_copy (&local->loc1, loc);
	local->name = strdup (linkpath);

	if ((local->name == NULL) || 
	    (local->loc1.path == NULL)) {
		gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
		STACK_UNWIND (frame, -1, ENOMEM, loc->inode, NULL);
		return 0;
	}

	STACK_WIND (frame,
		    unify_ns_symlink_cbk,
		    NS(this),
		    NS(this)->fops->symlink,
		    linkpath,
		    loc);

	return 0;
}


int32_t 
unify_rename_unlink_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno)
{
	int32_t callcnt = 0;
	unify_local_t *local = frame->local;
	call_frame_t *prev_frame = cookie;
  
	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_ERROR, 
			"child(%s): path(%s -> %s): %s", 
			prev_frame->this->name, 
			local->loc1.path, local->loc2.path, 
			strerror (op_errno));
      
	}
	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		local->stbuf.st_ino = local->st_ino;
		unify_local_wipe (local);
		STACK_UNWIND (frame, local->op_ret, local->op_errno, 
			      &local->stbuf);
	}
	return 0;
}

int32_t 
unify_ns_rename_undo_cbk (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  struct stat *buf)
{
	unify_local_t *local = frame->local;

	if (op_ret == -1) {
		gf_log (this->name, GF_LOG_ERROR, 
			"namespace: path(%s -> %s): %s", 
			local->loc1.path, local->loc2.path, 
			strerror (op_errno));
	}

	local->stbuf.st_ino = local->st_ino;
	unify_local_wipe (local);
	STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
	return 0;
}

int32_t 
unify_rename_cbk (call_frame_t *frame,
		  void *cookie,
		  xlator_t *this,
		  int32_t op_ret,
		  int32_t op_errno,
		  struct stat *buf)
{
	int32_t index = 0;
	int32_t callcnt = 0;
	int16_t *list = NULL;
	unify_private_t *priv = this->private;
	unify_local_t *local = frame->local;
	call_frame_t *prev_frame = cookie;
  
	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
		if (op_ret >= 0) {
			if (!S_ISDIR (buf->st_mode))
				local->stbuf = *buf;
			local->op_ret = op_ret;
		} else {
			gf_log (this->name, GF_LOG_ERROR, 
				"child(%s): path(%s -> %s): %s", 
				prev_frame->this->name, 
				local->loc1.path, local->loc2.path, 
				strerror (op_errno));
			local->op_errno = op_errno;
		}
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		local->stbuf.st_ino = local->st_ino;
		if (S_ISDIR (local->loc1.inode->st_mode)) {
			unify_local_wipe (local);
			STACK_UNWIND (frame, local->op_ret, local->op_errno, &local->stbuf);
			return 0;
		}

		if (local->op_ret == -1) {
			/* TODO: check this logic */

			/* Rename failed in storage node, successful on NS, 
			 * hence, rename back the entries in NS */
			/* NOTE: this will be done only if the destination 
			 * doesn't exists, if  the destination exists, the 
			 * job of correcting NS is left to self-heal
			 */
			if (!local->index) {
				loc_t tmp_oldloc = {
                                        /* its actual 'newloc->path' */
					.path = local->loc2.path, 
					.inode = local->loc1.inode,
					.parent = local->loc2.parent
				};
	
				loc_t tmp_newloc = {
					/* Actual 'oldloc->path' */
					.path = local->loc1.path,
					.parent = local->loc1.parent
				};

				gf_log (this->name, GF_LOG_ERROR, 
					"rename succussful on namespace, on "
					"stroage node failed, reverting back");

				STACK_WIND (frame,
					    unify_ns_rename_undo_cbk,
					    NS(this),
					    NS(this)->fops->rename,
					    &tmp_oldloc,
					    &tmp_newloc);
				return 0;
			}
		} else {
			/* Rename successful on storage nodes */

			int32_t idx = 0;
			int16_t *tmp_list = NULL;
			if (local->loc2.inode && local->loc2.inode->ctx) {
				list = data_to_ptr (
					dict_get (local->loc2.inode->ctx, 
						  this->name));
			}

			if (list) {				
				for (index = 0; list[index] != -1; index++);
				tmp_list = calloc (1, index * 2);
				memcpy (tmp_list, list, index * 2);

				for (index = 0; list[index] != -1; index++) {
					/* TODO: Check this logic. */
					/* If the destination file exists in 
					 * the same storage node where we sent
					 * 'rename' call, no need to send 
					 * unlink 
					 */
					for (idx = 0; 
					     local->list[idx] != -1; idx++) {
						if (tmp_list[index] == local->list[idx]) {
							tmp_list[index] = priv->child_count;
							continue;
						}
					}
	  
					if (NS(this) != priv->xl_array[tmp_list[index]]) {
						local->call_count++;
						callcnt++;
					}
				}

				if (local->call_count) {
					if (callcnt > 1)
						gf_log (this->name, 
							GF_LOG_ERROR, 
							"%s->%s: more (%d) "
							"subvolumes have the "
							"newloc entry", 
							local->loc1.path, 
							local->loc2.path, 
							callcnt);

					for (index=0; 
					     tmp_list[index] != -1; index++) {
						if (NS(this) != priv->xl_array[tmp_list[index]]) {		    
							STACK_WIND (frame,
								    unify_rename_unlink_cbk,
								    priv->xl_array[tmp_list[index]],
								    priv->xl_array[tmp_list[index]]->fops->unlink,
								    &local->loc2);
							if (!--callcnt)
								break;
						}
					}

					FREE (tmp_list);
					return 0;
				}
				if (tmp_list)
					FREE (tmp_list);
			}
		}
    
		/* Need not send 'unlink' to storage node */
		unify_local_wipe (local);
		STACK_UNWIND (frame, local->op_ret, 
			      local->op_errno, &local->stbuf);
	}

	return 0;
}

int32_t 
unify_ns_rename_cbk (call_frame_t *frame,
		     void *cookie,
		     xlator_t *this,
		     int32_t op_ret,
		     int32_t op_errno,
		     struct stat *buf)
{
	int32_t index = 0;
	int32_t callcnt = 0;
	int16_t *list = NULL;
	unify_private_t *priv = this->private;
	unify_local_t *local = frame->local;

	if (op_ret == -1) {
		/* Free local->new_inode */
		gf_log (this->name, GF_LOG_ERROR, 
			"namespace: path(%s -> %s): %s", 
			local->loc1.path, local->loc2.path, 
			strerror (op_errno));

		unify_local_wipe (local);
		STACK_UNWIND (frame, op_ret, op_errno, buf);
		return 0;
	}

	local->stbuf = *buf;
	local->st_ino = buf->st_ino;

	/* Everything is fine. */
	if (S_ISDIR (buf->st_mode)) {
		local->call_count = priv->child_count;
		for (index=0; index < priv->child_count; index++) {
			STACK_WIND (frame,
				    unify_rename_cbk,
				    priv->xl_array[index],
				    priv->xl_array[index]->fops->rename,
				    &local->loc1,
				    &local->loc2);
		}

		return 0;
	}

	local->call_count = 0;  
	/* send rename */
	list = local->list;
	for (index=0; list[index] != -1; index++) {
		if (NS(this) != priv->xl_array[list[index]]) {
			local->call_count++;
			callcnt++;
		}
	}

	if (local->call_count) {
		for (index=0; list[index] != -1; index++) {
			if (NS(this) != priv->xl_array[list[index]]) {
				STACK_WIND (frame,
					    unify_rename_cbk,
					    priv->xl_array[list[index]],
					    priv->xl_array[list[index]]->fops->rename,
					    &local->loc1,
					    &local->loc2);
				if (!--callcnt)
					break;
			}
		}
	} else {
		/* file doesn't seem to be present in storage nodes */
		gf_log (this->name, GF_LOG_CRITICAL,
			"CRITICAL: source file not in storage node, "
			"rename successful on namespace :O");
		unify_local_wipe (local);
		STACK_UNWIND (frame, -1, EIO, NULL);
	}
	return 0;
}


/**
 * unify_rename - One of the tricky function. The deadliest of all :O
 */
int32_t
unify_rename (call_frame_t *frame,
	      xlator_t *this,
	      loc_t *oldloc,
	      loc_t *newloc)
{
	unify_local_t *local = NULL;

	/* Initialization */
	INIT_LOCAL (frame, local);
	loc_copy (&local->loc1, oldloc);
	loc_copy (&local->loc2, newloc);

	if ((local->loc1.path == NULL) || 
	    (local->loc2.path == NULL)) {
		gf_log (this->name, GF_LOG_CRITICAL, "Not enough memory :O");
		STACK_UNWIND (frame, -1, ENOMEM, NULL);
		return 0;
	}
  
	local->list = data_to_ptr (dict_get (oldloc->inode->ctx, this->name));
	STACK_WIND (frame,
		    unify_ns_rename_cbk,
		    NS(this),
		    NS(this)->fops->rename,
		    oldloc,
		    newloc);
	return 0;
}

/**
 * unify_link_cbk -
 */
int32_t
unify_link_cbk (call_frame_t *frame,
		void *cookie,
		xlator_t *this,
		int32_t op_ret,
		int32_t op_errno,
		inode_t *inode,
		struct stat *buf)
{
	unify_local_t *local = frame->local;

	if (op_ret >= 0) 
		local->stbuf = *buf;
	local->stbuf.st_ino = local->st_ino;

	unify_local_wipe (local);
	STACK_UNWIND (frame, op_ret, op_errno, inode, &local->stbuf);

	return 0;
}

/**
 * unify_ns_link_cbk - 
 */
int32_t
unify_ns_link_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno,
		   inode_t *inode,
		   struct stat *buf)
{
	unify_private_t *priv = this->private;
	unify_local_t *local = frame->local;
	int16_t *list = local->list;
	int16_t index = 0;

	if (op_ret == -1) {
		/* No need to send link request to other servers, 
		 * as namespace action failed 
		 */
		gf_log (this->name, GF_LOG_ERROR, 
			"namespace: path(%s -> %s): %s", 
			local->loc1.path, local->loc2.path, 
			strerror (op_errno));
		unify_local_wipe (local);
		STACK_UNWIND (frame, op_ret, op_errno, inode, buf);
		return 0;
	}

	/* Update inode for this entry */
	local->op_ret = 0;
	local->st_ino = buf->st_ino;

	/* Send link request to the node now */
	for (index = 0; list[index] != -1; index++) {
		char need_break = (list[index+1] == -1);
		if (priv->xl_array[list[index]] != NS (this)) {
			STACK_WIND (frame,
				    unify_link_cbk,
				    priv->xl_array[list[index]],
				    priv->xl_array[list[index]]->fops->link,
				    &local->loc1,
				    &local->loc2);
		}
		if (need_break)
			break;
	}

	return 0;
}

/**
 * unify_link - 
 */
int32_t
unify_link (call_frame_t *frame,
	    xlator_t *this,
	    loc_t *oldloc,
	    loc_t *newloc)
{
	unify_local_t *local = NULL;

	UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (oldloc);
	UNIFY_CHECK_INODE_CTX_AND_UNWIND_ON_ERR (newloc);

	/* Initialization */
	INIT_LOCAL (frame, local);

	loc_copy (&local->loc1, oldloc);
	loc_copy (&local->loc2, newloc);

	local->list = data_to_ptr (dict_get (oldloc->inode->ctx, this->name));

	STACK_WIND (frame,
		    unify_ns_link_cbk,
		    NS(this),
		    NS(this)->fops->link,
		    oldloc,
		    newloc);

	return 0;
}


/**
 * unify_checksum_cbk - 
 */
int32_t
unify_checksum_cbk (call_frame_t *frame,
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

/**
 * unify_checksum - 
 */
int32_t
unify_checksum (call_frame_t *frame,
		xlator_t *this,
		loc_t *loc,
		int32_t flag)
{
	STACK_WIND (frame,
		    unify_checksum_cbk,
		    NS(this),
		    NS(this)->fops->checksum,
		    loc,
		    flag);

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
	unify_private_t *priv = this->private;
	struct sched_ops *sched = NULL;

	if (!priv) {
		return 0;
	}

	sched = priv->sched_ops;    
	if (!sched) {
		gf_log (this->name, GF_LOG_CRITICAL, "No scheduler :O");
		raise (SIGTERM);
		return 0;
	}
	if (priv->namespace == data) {
		if (event == GF_EVENT_CHILD_UP) {
			sched->notify (this, event, data);
		}
		return 0;
	}

	switch (event)
	{
	case GF_EVENT_CHILD_UP:
	{
		/* Call scheduler's update () to enable it for scheduling */
		sched->notify (this, event, data);
	
		LOCK (&priv->lock);
		{
			/* Increment the inode's generation, which is 
			   used for self_heal */
			++priv->inode_generation;
			++priv->num_child_up;
		}
		UNLOCK (&priv->lock);

		if (!priv->is_up) {
			default_notify (this, event, data);
			priv->is_up = 1;
		}
	}
	break;
	case GF_EVENT_CHILD_DOWN:
	{
		/* Call scheduler's update () to disable the child node 
		 * for scheduling
		 */
		sched->notify (this, event, data);
		LOCK (&priv->lock);
		{
			--priv->num_child_up;
		}
		UNLOCK (&priv->lock);

		if (priv->num_child_up == 0) {
			/* Send CHILD_DOWN to upper layer */
			default_notify (this, event, data);
			priv->is_up = 0;
		}
	}
	break;

	default:
	{
		default_notify (this, event, data);
	}
	break;
	}

	return 0;
}

/** 
 * init - This function is called first in the xlator, while initializing.
 *   All the config file options are checked and appropriate flags are set.
 *
 * @this - 
 */
int32_t 
init (xlator_t *this)
{
	int32_t ret = 0;
	int32_t count = 0;
	unify_private_t *_private = NULL; 
	xlator_list_t *trav = NULL;
	xlator_t *ns_xl = NULL;
	data_t *scheduler = NULL;
	data_t *data = NULL;

	/* Check for number of child nodes, if there is no child nodes, exit */
	if (!this->children) {
		gf_log (this->name, GF_LOG_ERROR,
			"No child nodes specified. check \"subvolumes \" "
			"option in volfile");
		return -1;
	}
  
	/* Check for 'scheduler' in volume */
	scheduler = dict_get (this->options, "scheduler");
	if (!scheduler) {
		gf_log (this->name, GF_LOG_ERROR, 
			"\"option scheduler <x>\" is missing in volfile");
		return -1;
	}

	/* Setting "option namespace <node>" */
	data = dict_get (this->options, "namespace");
	if(!data) {
		gf_log (this->name, GF_LOG_CRITICAL, 
			"namespace option not specified, Exiting");
		return -1;
	}
	/* Search namespace in the child node, if found, exit */
	trav = this->children;
	while (trav) {
		if (strcmp (trav->xlator->name, data->data) == 0)
			break;
		trav = trav->next;
	}
	if (trav) {
		gf_log (this->name, GF_LOG_CRITICAL, 
			"namespace node used as a subvolume, Exiting");
		return -1;
	}
	
	/* Search for the namespace node, if found, continue */
	ns_xl = this->next;
	while (ns_xl) {
		if (strcmp (ns_xl->name, data->data) == 0)
			break;
		ns_xl = ns_xl->next;
	}
	if (!ns_xl) {
		gf_log (this->name, GF_LOG_CRITICAL, 
			"namespace node not found in volfile, Exiting");
		return -1;
	}
	
	gf_log (this->name, GF_LOG_DEBUG, 
		"namespace node specified as %s", data->data);
	
	_private = calloc (1, sizeof (*_private));
	ERR_ABORT (_private);
	_private->sched_ops = get_scheduler (scheduler->data);
	if (!_private->sched_ops) {
		gf_log (this->name, GF_LOG_CRITICAL, 
			"Error while loading scheduler. Exiting");
		FREE (_private);
		return -1;
	}
	_private->namespace = ns_xl;
	
	/* update _private structure */
	{
		count = 0;
		trav = this->children;
		/* Get the number of child count */
		while (trav) {
			count++;
			trav = trav->next;
		}
		
		gf_log (this->name, GF_LOG_DEBUG, 
			"Child node count is %d", count);    

		_private->child_count = count;
		if (count == 1) {
			/* TODO: Should I error out here? */
			gf_log (this->name, GF_LOG_CRITICAL, 
				"WARNING: You have defined only one "
				"\"subvolumes\" for unify volume. It may not "
				"be the desired config, review your volume "
				"volfile. If this is how you are testing it,"
				" you may hit some performance penalty");
		}
		
		_private->xl_array = calloc (1, 
					     sizeof (xlator_t) * (count + 1));
		ERR_ABORT (_private->xl_array);
		
		count = 0;
		trav = this->children;
		while (trav) {
			_private->xl_array[count++] = trav->xlator;
			trav = trav->next;
		}
		_private->xl_array[count] = _private->namespace;
		
		/* self-heal part, start with generation '1' */
		_private->inode_generation = 1; 
                /* Because, Foreground part is tested well */
		_private->self_heal = ZR_UNIFY_FG_SELF_HEAL; 
		data = dict_get (this->options, "self-heal");
		if (data) {
			if (strcasecmp (data->data, "off") == 0) 
				_private->self_heal = ZR_UNIFY_SELF_HEAL_OFF;

			if (strcasecmp (data->data, "foreground") == 0)
				_private->self_heal = ZR_UNIFY_FG_SELF_HEAL;

			if (strcasecmp (data->data, "background") == 0)
				_private->self_heal = ZR_UNIFY_BG_SELF_HEAL;
		}
    
		/* optimist - ask bulde for more about it */
		data = dict_get (this->options, "optimist");
		if (data) {
			if (gf_string2boolean (data->data, 
					       &_private->optimist) == -1) {
				gf_log (this->name, GF_LOG_ERROR, 
					"optimist excepts only boolean "
					"options");
			}
		}

		LOCK_INIT (&_private->lock);
	}

	/* Now that everything is fine. */
	this->private = (void *)_private;
	{
		/* Initialize scheduler, if everything else is successful */
		ret = _private->sched_ops->init (this); 
		if (ret == -1) {
			gf_log (this->name, GF_LOG_CRITICAL,
				"Initializing scheduler failed, Exiting");
			FREE (_private);
			return -1;
		}

		ret = 0;

		/* Initialize the namespace volume */
		if (!ns_xl->ready)
			ret = xlator_tree_init (ns_xl);
		if (!ret) {
			/* This section is required because some fops may look 
			 * for 'xl->parent' variable 
			 */
			xlator_list_t *xlparent = NULL;
			xlator_list_t *parent = NULL;

			xlparent = calloc (1, sizeof (*xlparent));
			xlparent->xlator = this;
			if (!ns_xl->parents) {
				ns_xl->parents = xlparent;
			} else {
				parent = ns_xl->parents;
				while (parent->next)
					parent = parent->next;
				parent->next = xlparent;
			}
		} else {
			gf_log (this->name, GF_LOG_CRITICAL, 
				"initializing namespace node failed, Exiting");
			FREE (_private);
			return -1;
		}
	}

	/* Tell namespace node that init is done */
	ns_xl->notify (ns_xl, GF_EVENT_PARENT_UP, this);

	return 0;
}

/** 
 * fini  - Free all the allocated memory 
 */
void
fini (xlator_t *this)
{
	unify_private_t *priv = this->private;
	priv->sched_ops->fini (this);
	this->private = NULL;
	LOCK_DESTROY (&priv->lock);
	FREE (priv->xl_array);
	FREE (priv);
	return;
}


struct xlator_fops fops = {
	.stat        = unify_stat,
	.chmod       = unify_chmod,
	.readlink    = unify_readlink,
	.mknod       = unify_mknod,
	.mkdir       = unify_mkdir,
	.unlink      = unify_unlink,
	.rmdir       = unify_rmdir,
	.symlink     = unify_symlink,
	.rename      = unify_rename,
	.link        = unify_link,
	.chown       = unify_chown,
	.truncate    = unify_truncate,
	.create      = unify_create,
	.open        = unify_open,
	.readv       = unify_readv,
	.writev      = unify_writev,
	.statfs      = unify_statfs,
	.flush       = unify_flush,
	.fsync       = unify_fsync,
	.setxattr    = unify_setxattr,
	.getxattr    = unify_getxattr,
	.removexattr = unify_removexattr,
	.opendir     = unify_opendir,
	.readdir     = unify_readdir,
	.fsyncdir    = unify_fsyncdir,
	.access      = unify_access,
	.ftruncate   = unify_ftruncate,
	.fstat       = unify_fstat,
	.lk          = unify_lk,
	.fchown      = unify_fchown,
	.fchmod      = unify_fchmod,
	.utimens     = unify_utimens,
	.lookup      = unify_lookup,
	.getdents    = unify_getdents,
	.checksum    = unify_checksum,
};

struct xlator_mops mops = {
};

struct xlator_cbks cbks = {
};

struct xlator_options options[] = {
	{ "namespace", GF_OPTION_TYPE_XLATOR, 0, 0, 0 },
	{ "scheduler", GF_OPTION_TYPE_STR, 0, 0, 0, 
	  "alu|rr|random|nufa|switch" },
	{ "alu.<scheduler-specific-option>", GF_OPTION_TYPE_ANY, 4, 0, 0 },
	{ "rr.<scheduler-specific-option>", GF_OPTION_TYPE_ANY, 3, 0, 0 },
	{ "random.<scheduler-specific-option>", GF_OPTION_TYPE_ANY, 7, 0, 0 },
	{ "switch.<scheduler-specific-option>", GF_OPTION_TYPE_ANY, 7, 0, 0 },
	{ "nufa.<scheduler-specific-option>", GF_OPTION_TYPE_ANY, 5, 0, 0 },
	{ "self-heal", GF_OPTION_TYPE_STR, 0, 0, 0, 
	  "foreground|background|off" },
	{ "optimist", GF_OPTION_TYPE_BOOL, 0, 0, 0 },
	{ NULL, 0, 0, 0, 0 },
};
