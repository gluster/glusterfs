/*
  Copyright (c) 2007-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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
 * unify-self-heal.c : 
 *   This file implements few functions which enables 'unify' translator 
 *  to be consistent in its behaviour when 
 *     > a node fails, 
 *     > a node gets added, 
 *     > a failed node comes back
 *     > a new namespace server is added (ie, an fresh namespace server).
 * 
 *  This functionality of 'unify' will enable glusterfs to support storage
 *  system failure, and maintain consistancy. This works both ways, ie, when
 *  an entry (either file or directory) is found on namespace server, and not
 *  on storage nodes, its created in storage nodes and vica-versa.
 * 
 *  The two fops, where it can be implemented are 'getdents ()' and 'lookup ()'
 *
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
#include "common-utils.h"

int32_t
unify_sh_getdents_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       dir_entry_t *entry,
		       int32_t count);

int32_t
unify_sh_ns_getdents_cbk (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  dir_entry_t *entry,
			  int32_t count);

int32_t
unify_bgsh_getdents_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 dir_entry_t *entry,
			 int32_t count);

int32_t
unify_bgsh_ns_getdents_cbk (call_frame_t *frame,
			    void *cookie,
			    xlator_t *this,
			    int32_t op_ret,
			    int32_t op_errno,
			    dir_entry_t *entry,
			    int32_t count);

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

	if (local->sh_struct) {
		if (local->sh_struct->offset_list)
			FREE (local->sh_struct->offset_list);

		if (local->sh_struct->entry_list)
			FREE (local->sh_struct->entry_list);

		if (local->sh_struct->count_list)
			FREE (local->sh_struct->count_list);

		FREE (local->sh_struct);
	}

	loc_wipe (&local->loc1);
	loc_wipe (&local->loc2);
}

int32_t 
unify_sh_setdents_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno)
{
	int32_t callcnt = -1;
	unify_local_t *local = frame->local;
	inode_t *inode = NULL;
	dict_t *tmp_dict = NULL;
	dir_entry_t *prev, *entry, *trav;

	LOCK (&frame->lock);
	{
		/* if local->call_count == 0, that means, setdents on 
		 * storagenodes is still pending.
		 */
		if (local->call_count)
			callcnt = --local->call_count;
	}
	UNLOCK (&frame->lock);

	if (callcnt == 0) {
		if (local->sh_struct->entry_list[0]) {
			prev = entry = local->sh_struct->entry_list[0];
			if (!entry)
				return 0;
			trav = entry->next;
			while (trav) {
				prev->next = trav->next;
				FREE (trav->name);
				if (S_ISLNK (trav->buf.st_mode))
					FREE (trav->link);
				FREE (trav);
				trav = prev->next;
			}
			FREE (entry);
		}

		if (!local->flags) {
			if (local->sh_struct->count_list[0] >= 
			    UNIFY_SELF_HEAL_GETDENTS_COUNT) {
				/* count == size, that means, there are more entries
				   to read from */
				//local->call_count = 0;
				local->sh_struct->offset_list[0] += 
					UNIFY_SELF_HEAL_GETDENTS_COUNT;
				STACK_WIND (frame,
					    unify_sh_ns_getdents_cbk,
					    NS(this),
					    NS(this)->fops->getdents,
					    local->fd,
					    UNIFY_SELF_HEAL_GETDENTS_COUNT,
					    local->sh_struct->offset_list[0],
					    GF_GET_DIR_ONLY);
			}		
		} else {
			inode = local->loc1.inode;
			fd_unref (local->fd);
			tmp_dict = local->dict;

			unify_local_wipe (local);
			
			STACK_UNWIND (frame, local->op_ret, local->op_errno, 
				      inode, &local->stbuf, local->dict);
			if (tmp_dict)
				dict_unref (tmp_dict);
		}
	}
  
	return 0;
}


int32_t
unify_sh_ns_getdents_cbk (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  dir_entry_t *entry,
			  int32_t count)
{
	unify_local_t *local = frame->local;
	unify_private_t *priv = this->private;
	long index = 0;
	unsigned long final = 0;
	dir_entry_t *tmp = CALLOC (1, sizeof (dir_entry_t));
	
	local->sh_struct->entry_list[0] = tmp;
	local->sh_struct->count_list[0] = count;
	if (entry) {
		tmp->next = entry->next;
		entry->next = NULL;
	}

	if ((count < UNIFY_SELF_HEAL_GETDENTS_COUNT) || !entry) {
		final = 1;
	}

	LOCK (&frame->lock);
	{
		/* local->call_count will be '0' till now. make it 1 so, it 
		   can be UNWIND'ed for the last call. */
		local->call_count = priv->child_count;
		if (final)
			local->flags = 1;
	}
	UNLOCK (&frame->lock);

	for (index = 0; index < priv->child_count; index++) 
	{
		STACK_WIND_COOKIE (frame,
				   unify_sh_setdents_cbk, 
				   (void *)index,
				   priv->xl_array[index],
				   priv->xl_array[index]->fops->setdents,
				   local->fd, GF_SET_DIR_ONLY,
				   local->sh_struct->entry_list[0], count);
	}

	return 0;
}

int32_t 
unify_sh_ns_setdents_cbk (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno)
{
	int32_t callcnt = -1;
	unify_local_t *local = frame->local;
	unify_private_t *priv = this->private;
	long index = (long)cookie;
	dir_entry_t *prev, *entry, *trav;

	LOCK (&frame->lock);
	{
		if (local->sh_struct->entry_list[index]) {
			prev = entry = local->sh_struct->entry_list[index];
			trav = entry->next;
			while (trav) {
				prev->next = trav->next;
				FREE (trav->name);
				if (S_ISLNK (trav->buf.st_mode))
					FREE (trav->link);
				FREE (trav);
				trav = prev->next;
			}
			FREE (entry);
		}
	}
	UNLOCK (&frame->lock);

	if (local->sh_struct->count_list[index] < 
	    UNIFY_SELF_HEAL_GETDENTS_COUNT) {
		LOCK (&frame->lock);
		{
			callcnt = --local->call_count;
		}
		UNLOCK (&frame->lock);
	} else {
		/* count == size, that means, there are more entries 
		   to read from */
		local->sh_struct->offset_list[index] += 
			UNIFY_SELF_HEAL_GETDENTS_COUNT;
		STACK_WIND_COOKIE (frame,
				   unify_sh_getdents_cbk,
				   cookie,
				   priv->xl_array[index],
				   priv->xl_array[index]->fops->getdents,
				   local->fd,
				   UNIFY_SELF_HEAL_GETDENTS_COUNT,
				   local->sh_struct->offset_list[index],
				   GF_GET_ALL);
    
		gf_log (this->name, GF_LOG_DEBUG, 
			"readdir on (%s) with offset %"PRId64"", 
			priv->xl_array[index]->name, 
			local->sh_struct->offset_list[index]);
	}

	if (!callcnt) {
		/* All storage nodes have done unified setdents on NS node.
		 * Now, do getdents from NS and do setdents on storage nodes.
		 */
    
		/* sh_struct->offset_list is no longer required for
		   storage nodes now */
		local->sh_struct->offset_list[0] = 0; /* reset */

		STACK_WIND (frame,
			    unify_sh_ns_getdents_cbk,
			    NS(this),
			    NS(this)->fops->getdents,
			    local->fd,
			    UNIFY_SELF_HEAL_GETDENTS_COUNT,
			    0, /* In this call, do send '0' as offset */
			    GF_GET_DIR_ONLY);
	}

	return 0;
}


/**
 * unify_sh_getdents_cbk -
 */
int32_t
unify_sh_getdents_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       dir_entry_t *entry,
		       int32_t count)
{
	int32_t callcnt = -1;
	unify_local_t *local = frame->local;
	unify_private_t *priv = this->private;
	long index = (long)cookie;
	dir_entry_t *tmp = NULL; 

	if (op_ret >= 0 && count > 0) {
		/* There is some dentry found, just send the dentry to NS */
		tmp = CALLOC (1, sizeof (dir_entry_t));
		local->sh_struct->entry_list[index] = tmp;
		local->sh_struct->count_list[index] = count;
		if (entry) {
			tmp->next = entry->next;
			entry->next = NULL;
		}
		STACK_WIND_COOKIE (frame,
				   unify_sh_ns_setdents_cbk,
				   cookie,
				   NS(this),
				   NS(this)->fops->setdents,
				   local->fd,
				   GF_SET_IF_NOT_PRESENT,
				   local->sh_struct->entry_list[index],
				   count);
		return 0;
	}
  
	if (count < UNIFY_SELF_HEAL_GETDENTS_COUNT) {
		LOCK (&frame->lock);
		{
			callcnt = --local->call_count;
		}
		UNLOCK (&frame->lock);
	} else {
		/* count == size, that means, there are more entries 
		   to read from */
		local->sh_struct->offset_list[index] += 
			UNIFY_SELF_HEAL_GETDENTS_COUNT;
		STACK_WIND_COOKIE (frame,
				   unify_sh_getdents_cbk,
				   cookie,
				   priv->xl_array[index],
				   priv->xl_array[index]->fops->getdents,
				   local->fd,
				   UNIFY_SELF_HEAL_GETDENTS_COUNT,
				   local->sh_struct->offset_list[index],
				   GF_GET_ALL);
    
		gf_log (this->name, GF_LOG_DEBUG, 
			"readdir on (%s) with offset %"PRId64"", 
			priv->xl_array[index]->name, 
			local->sh_struct->offset_list[index]);
	}

	if (!callcnt) {
		/* All storage nodes have done unified setdents on NS node.
		 * Now, do getdents from NS and do setdents on storage nodes.
		 */
    
		/* sh_struct->offset_list is no longer required for
		   storage nodes now */
		local->sh_struct->offset_list[0] = 0; /* reset */

		STACK_WIND (frame,
			    unify_sh_ns_getdents_cbk,
			    NS(this),
			    NS(this)->fops->getdents,
			    local->fd,
			    UNIFY_SELF_HEAL_GETDENTS_COUNT,
			    0, /* In this call, do send '0' as offset */
			    GF_GET_DIR_ONLY);
	}

	return 0;
}

/**
 * unify_sh_opendir_cbk -
 *
 * @cookie: 
 */
int32_t 
unify_sh_opendir_cbk (call_frame_t *frame,
		      void *cookie,
		      xlator_t *this,
		      int32_t op_ret,
		      int32_t op_errno,
		      fd_t *fd)
{
	int32_t callcnt = 0;
	unify_local_t *local = frame->local;
	unify_private_t *priv = this->private;
	int16_t index = 0;
	inode_t *inode = NULL;
	dict_t *tmp_dict = NULL;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
    
		if (op_ret >= 0) {
			local->op_ret = op_ret;
		} else {
			gf_log (this->name, GF_LOG_WARNING, "failed");
			local->failed = 1;
		}
	}
	UNLOCK (&frame->lock);
  
	if (!callcnt) {
		local->call_count = priv->child_count + 1;
    
		if (!local->failed) {
			/* send getdents() namespace after finishing
			   storage nodes */
			local->call_count--; 
      
			fd_bind (fd);

			if (local->call_count) {
				/* Used as the offset index. This list keeps
				 * track of offset sent to each node during
				 * STACK_WIND.
				 */
				local->sh_struct->offset_list = 
					calloc (priv->child_count, 
						sizeof (off_t));
				ERR_ABORT (local->sh_struct->offset_list);
	
				local->sh_struct->entry_list = 
					calloc (priv->child_count, 
						sizeof (dir_entry_t *));
				ERR_ABORT (local->sh_struct->entry_list);

				local->sh_struct->count_list = 
					calloc (priv->child_count, 
						sizeof (int));
				ERR_ABORT (local->sh_struct->count_list);

				/* Send getdents on all the fds */
				for (index = 0; 
				     index < priv->child_count; index++) {
					STACK_WIND_COOKIE (frame,
							   unify_sh_getdents_cbk,
							   (void *)(long)index,
							   priv->xl_array[index],
							   priv->xl_array[index]->fops->getdents,
							   local->fd,
							   UNIFY_SELF_HEAL_GETDENTS_COUNT,
							   0, /* In this call, do send '0' as offset */
							   GF_GET_ALL);
				}

				/* did stack wind, so no need to unwind here */
				return 0;
			} /* (local->call_count) */
		} /* (!local->failed) */

		/* Opendir failed on one node. */ 
		inode = local->loc1.inode;
		fd_unref (local->fd);
		tmp_dict = local->dict;

		unify_local_wipe (local);
		/* Only 'self-heal' failed, lookup() was successful. */
		local->op_ret = 0;

		/* This is lookup_cbk ()'s UNWIND. */
		STACK_UNWIND (frame, local->op_ret, local->op_errno, inode,
			      &local->stbuf, local->dict);
		if (tmp_dict)
			dict_unref (tmp_dict);
	}

	return 0;
}

/**
 * gf_sh_checksum_cbk - 
 * 
 * @frame: frame used in lookup. get a copy of it, and use that copy.
 * @this: pointer to unify xlator.
 * @inode: pointer to inode, for which the consistency check is required.
 *
 */
int32_t 
unify_sh_checksum_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       uint8_t *file_checksum,
		       uint8_t *dir_checksum)
{
	unify_local_t *local = frame->local;
	unify_private_t *priv = this->private;
	int16_t index = 0;
	int32_t callcnt = 0;
	inode_t *inode = NULL;
	dict_t *tmp_dict = NULL;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
		if (op_ret >= 0) {
			if (NS(this) == (xlator_t *)cookie) {
				memcpy (local->sh_struct->ns_file_checksum, 
					file_checksum, ZR_FILENAME_MAX);
				memcpy (local->sh_struct->ns_dir_checksum, 
					dir_checksum, ZR_FILENAME_MAX);
			} else {
				if (local->entry_count == 0) {
					/* Initialize the dir_checksum to be 
					 * used for comparision with other
					 * storage nodes. Should be done for
					 * the first successful call *only*. 
					 */
                                        /* Using 'entry_count' as a flag */
					local->entry_count = 1;
					memcpy (local->sh_struct->dir_checksum,
						dir_checksum, ZR_FILENAME_MAX);
				}

				/* Reply from the storage nodes */
				for (index = 0; 
				     index < ZR_FILENAME_MAX; index++) {
					/* Files should be present in
					   only one node */
					local->sh_struct->file_checksum[index] ^= file_checksum[index];
	  
					/* directory structure should be
					   same accross */
					if (local->sh_struct->dir_checksum[index] != dir_checksum[index])
						local->failed = 1;
				}
			}
		} 
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		for (index = 0; index < ZR_FILENAME_MAX ; index++) {
			if (local->sh_struct->file_checksum[index] != 
			    local->sh_struct->ns_file_checksum[index]) {
				local->failed = 1;
				break;
			}
			if (local->sh_struct->dir_checksum[index] != 
			    local->sh_struct->ns_dir_checksum[index]) {
				local->failed = 1;
				break;
			}
		}
	
		if (local->failed) {
			/* Log it, it should be a rare event */
			gf_log (this->name, GF_LOG_WARNING, 
				"Self-heal triggered on directory %s", 
				local->loc1.path);

			/* Any self heal will be done at directory level */
			local->call_count = 0;
			local->op_ret = -1;
			local->failed = 0;
      
			local->fd = fd_create (local->loc1.inode, 
					       frame->root->pid);

			local->call_count = priv->child_count + 1;
	
			for (index = 0; 
			     index < (priv->child_count + 1); index++) {
				STACK_WIND_COOKIE (frame,
						   unify_sh_opendir_cbk,
						   priv->xl_array[index]->name,
						   priv->xl_array[index],
						   priv->xl_array[index]->fops->opendir,
						   &local->loc1,
						   local->fd);
			}
			/* opendir can be done on the directory */
			return 0;
		}

		/* no mismatch */
		inode = local->loc1.inode;
		tmp_dict = local->dict;

		unify_local_wipe (local);

		/* This is lookup_cbk ()'s UNWIND. */
		STACK_UNWIND (frame,
			      local->op_ret,
			      local->op_errno,
			      inode,
			      &local->stbuf,
			      local->dict);
		if (tmp_dict)
			dict_unref (tmp_dict);
	}

	return 0;
}

/* Foreground self-heal part over */

/* Background self-heal part */

int32_t 
unify_bgsh_setdents_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno)
{
	int32_t callcnt = -1;
	unify_local_t *local = frame->local;
	dir_entry_t *prev, *entry, *trav;

	LOCK (&frame->lock);
	{
		/* if local->call_count == 0, that means, setdents 
		   on storagenodes is still pending. */
		if (local->call_count)
			callcnt = --local->call_count;
	}
	UNLOCK (&frame->lock);


	if (callcnt == 0) {
		if (local->sh_struct->entry_list[0]) {
			prev = entry = local->sh_struct->entry_list[0];
			trav = entry->next;
			while (trav) {
				prev->next = trav->next;
				FREE (trav->name);
				if (S_ISLNK (trav->buf.st_mode))
					FREE (trav->link);
				FREE (trav);
				trav = prev->next;
			}
			FREE (entry);
		}

		if (!local->flags) {
			if (local->sh_struct->count_list[0] >= 
			    UNIFY_SELF_HEAL_GETDENTS_COUNT) {
				/* count == size, that means, there are more
				   entries to read from */
				//local->call_count = 0;
				local->sh_struct->offset_list[0] += 
					UNIFY_SELF_HEAL_GETDENTS_COUNT;
				STACK_WIND (frame,
					    unify_bgsh_ns_getdents_cbk,
					    NS(this),
					    NS(this)->fops->getdents,
					    local->fd,
					    UNIFY_SELF_HEAL_GETDENTS_COUNT,
					    local->sh_struct->offset_list[0],
					    GF_GET_DIR_ONLY);
			}		
		} else {
			fd_unref (local->fd);
			unify_local_wipe (local);
			STACK_DESTROY (frame->root);
		}
	}

	return 0;
}


int32_t
unify_bgsh_ns_getdents_cbk (call_frame_t *frame,
			    void *cookie,
			    xlator_t *this,
			    int32_t op_ret,
			    int32_t op_errno,
			    dir_entry_t *entry,
			    int32_t count)
{
	unify_local_t *local = frame->local;
	unify_private_t *priv = this->private;
	long index = 0;
	unsigned long final = 0;
	dir_entry_t *tmp = CALLOC (1, sizeof (dir_entry_t));

	local->sh_struct->entry_list[0] = tmp;
	local->sh_struct->count_list[0] = count;
	if (entry) {
		tmp->next = entry->next;
		entry->next = NULL;
	}

	if ((count < UNIFY_SELF_HEAL_GETDENTS_COUNT) || !entry) {
		final = 1;
	}

	LOCK (&frame->lock);
	{
		/* local->call_count will be '0' till now. make it 1 so, 
		   it can be UNWIND'ed for the last call. */
		local->call_count = priv->child_count;
		if (final)
			local->flags = 1;
	}
	UNLOCK (&frame->lock);

	for (index = 0; index < priv->child_count; index++) 
	{
		STACK_WIND_COOKIE (frame,
				   unify_bgsh_setdents_cbk, 
				   (void *)index,
				   priv->xl_array[index],
				   priv->xl_array[index]->fops->setdents,
				   local->fd, GF_SET_DIR_ONLY,
				   local->sh_struct->entry_list[0], count);
	}

	return 0;
}

int32_t 
unify_bgsh_ns_setdents_cbk (call_frame_t *frame,
			    void *cookie,
			    xlator_t *this,
			    int32_t op_ret,
			    int32_t op_errno)
{
	int32_t callcnt = -1;
	unify_local_t *local = frame->local;
	unify_private_t *priv = this->private;
	long index = (long)cookie;
	dir_entry_t *prev, *entry, *trav;

	if (local->sh_struct->entry_list[index]) {
		prev = entry = local->sh_struct->entry_list[index];
		if (!entry)
			return 0;
		trav = entry->next;
		while (trav) {
			prev->next = trav->next;
			FREE (trav->name);
			if (S_ISLNK (trav->buf.st_mode))
				FREE (trav->link);
			FREE (trav);
			trav = prev->next;
		}
		FREE (entry);
	}

	if (local->sh_struct->count_list[index] < 
	    UNIFY_SELF_HEAL_GETDENTS_COUNT) {
		LOCK (&frame->lock);
		{
			callcnt = --local->call_count;
		}
		UNLOCK (&frame->lock);
	} else {
		/* count == size, that means, there are more entries 
		   to read from */
		local->sh_struct->offset_list[index] += 
			UNIFY_SELF_HEAL_GETDENTS_COUNT;
		STACK_WIND_COOKIE (frame,
				   unify_bgsh_getdents_cbk,
				   cookie,
				   priv->xl_array[index],
				   priv->xl_array[index]->fops->getdents,
				   local->fd,
				   UNIFY_SELF_HEAL_GETDENTS_COUNT,
				   local->sh_struct->offset_list[index],
				   GF_GET_ALL);
    
		gf_log (this->name, GF_LOG_DEBUG, 
			"readdir on (%s) with offset %"PRId64"", 
			priv->xl_array[index]->name, 
			local->sh_struct->offset_list[index]);
	}

	if (!callcnt) {
		/* All storage nodes have done unified setdents on NS node.
		 * Now, do getdents from NS and do setdents on storage nodes.
		 */
    
		/* sh_struct->offset_list is no longer required for
		   storage nodes now */
		local->sh_struct->offset_list[0] = 0; /* reset */

		STACK_WIND (frame,
			    unify_bgsh_ns_getdents_cbk,
			    NS(this),
			    NS(this)->fops->getdents,
			    local->fd,
			    UNIFY_SELF_HEAL_GETDENTS_COUNT,
			    0, /* In this call, do send '0' as offset */
			    GF_GET_DIR_ONLY);
	}

	return 0;
}


/**
 * unify_bgsh_getdents_cbk -
 */
int32_t
unify_bgsh_getdents_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 dir_entry_t *entry,
			 int32_t count)
{
	int32_t callcnt = -1;
	unify_local_t *local = frame->local;
	unify_private_t *priv = this->private;
	long index = (long)cookie;
	dir_entry_t *tmp = NULL; 

	if (op_ret >= 0 && count > 0) {
		/* There is some dentry found, just send the dentry to NS */
		tmp = CALLOC (1, sizeof (dir_entry_t));
		local->sh_struct->entry_list[index] = tmp;
		local->sh_struct->count_list[index] = count;
		if (entry) {
			tmp->next = entry->next;
			entry->next = NULL;
		}
		STACK_WIND_COOKIE (frame,
				   unify_bgsh_ns_setdents_cbk,
				   cookie,
				   NS(this),
				   NS(this)->fops->setdents,
				   local->fd,
				   GF_SET_IF_NOT_PRESENT,
				   local->sh_struct->entry_list[index],
				   count);
		return 0;
	}
  
	if (count < UNIFY_SELF_HEAL_GETDENTS_COUNT) {
		LOCK (&frame->lock);
		{
			callcnt = --local->call_count;
		}
		UNLOCK (&frame->lock);
	} else {
		/* count == size, that means, there are more entries to read from */
		local->sh_struct->offset_list[index] += 
			UNIFY_SELF_HEAL_GETDENTS_COUNT;

		STACK_WIND_COOKIE (frame,
				   unify_bgsh_getdents_cbk,
				   cookie,
				   priv->xl_array[index],
				   priv->xl_array[index]->fops->getdents,
				   local->fd,
				   UNIFY_SELF_HEAL_GETDENTS_COUNT,
				   local->sh_struct->offset_list[index],
				   GF_GET_ALL);
    
		gf_log (this->name, GF_LOG_DEBUG, 
			"readdir on (%s) with offset %"PRId64"", 
			priv->xl_array[index]->name, 
			local->sh_struct->offset_list[index]);
	}

	if (!callcnt) {
		/* All storage nodes have done unified setdents on NS node.
		 * Now, do getdents from NS and do setdents on storage nodes.
		 */
    
		/* sh_struct->offset_list is no longer required for 
		   storage nodes now */
		local->sh_struct->offset_list[0] = 0; /* reset */

		STACK_WIND (frame,
			    unify_bgsh_ns_getdents_cbk,
			    NS(this),
			    NS(this)->fops->getdents,
			    local->fd,
			    UNIFY_SELF_HEAL_GETDENTS_COUNT,
			    0, /* In this call, do send '0' as offset */
			    GF_GET_DIR_ONLY);
	}

	return 0;
}

/**
 * unify_bgsh_opendir_cbk -
 *
 * @cookie: 
 */
int32_t 
unify_bgsh_opendir_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			fd_t *fd)
{
	unify_local_t *local = frame->local;
	unify_private_t *priv = this->private;
	int32_t callcnt = 0;
	int16_t index = 0;

	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
    
		if (op_ret >= 0) {
			local->op_ret = op_ret;
		} else {
			local->failed = 1;
		}
	}
	UNLOCK (&frame->lock);
  
	if (!callcnt) {
		local->call_count = priv->child_count + 1;
    
		if (!local->failed) {
			/* send getdents() namespace after finishing 
			   storage nodes */
			local->call_count--; 
			callcnt = local->call_count;
      
			fd_bind (fd);

			if (local->call_count) {
				/* Used as the offset index. This list keeps 
				   track of offset sent to each node during 
				   STACK_WIND. */
				local->sh_struct->offset_list = 
					calloc (priv->child_count, 
						sizeof (off_t));
				ERR_ABORT (local->sh_struct->offset_list);
	
				local->sh_struct->entry_list = 
					calloc (priv->child_count, 
						sizeof (dir_entry_t *));
				ERR_ABORT (local->sh_struct->entry_list);

				local->sh_struct->count_list = 
					calloc (priv->child_count, 
						sizeof (int));
				ERR_ABORT (local->sh_struct->count_list);

				/* Send getdents on all the fds */
				for (index = 0; 
				     index < priv->child_count; index++) {
					STACK_WIND_COOKIE (frame,
							   unify_bgsh_getdents_cbk,
							   (void *)(long)index,
							   priv->xl_array[index],
							   priv->xl_array[index]->fops->getdents,
							   local->fd,
							   UNIFY_SELF_HEAL_GETDENTS_COUNT,
							   0, /* In this call, do send '0' as offset */
							   GF_GET_ALL);
				}
				/* did a stack wind, so no need to unwind here */
				return 0;
			} /* (local->call_count) */
		} /* (!local->failed) */

		/* Opendir failed on one node. 	 */
		fd_unref (local->fd);
		
		unify_local_wipe (local);
		STACK_DESTROY (frame->root);
	}

	return 0;
}

/**
 * gf_bgsh_checksum_cbk - 
 * 
 * @frame: frame used in lookup. get a copy of it, and use that copy.
 * @this: pointer to unify xlator.
 * @inode: pointer to inode, for which the consistency check is required.
 *
 */
int32_t 
unify_bgsh_checksum_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 uint8_t *file_checksum,
			 uint8_t *dir_checksum)
{
	unify_local_t *local = frame->local;
	unify_private_t *priv = this->private;
	int16_t index = 0;
	int32_t callcnt = 0;
  
	LOCK (&frame->lock);
	{
		callcnt = --local->call_count;
		if (op_ret >= 0) {
			if (NS(this) == (xlator_t *)cookie) {
				memcpy (local->sh_struct->ns_file_checksum, 
					file_checksum, ZR_FILENAME_MAX);
				memcpy (local->sh_struct->ns_dir_checksum, 
					dir_checksum, ZR_FILENAME_MAX);
			} else {
				if (local->entry_count == 0) {
					/* Initialize the dir_checksum to be 
					 * used for comparision with other 
					 * storage nodes. Should be done for
					 * the first successful call *only*. 
					 */
					/* Using 'entry_count' as a flag */
					local->entry_count = 1; 
					memcpy (local->sh_struct->dir_checksum,
						dir_checksum, ZR_FILENAME_MAX);
				}

				/* Reply from the storage nodes */
				for (index = 0; 
				     index < ZR_FILENAME_MAX; index++) {
					/* Files should be present in only 
					   one node */
					local->sh_struct->file_checksum[index] ^= file_checksum[index];
	  
					/* directory structure should be same 
					   accross */
					if (local->sh_struct->dir_checksum[index] != dir_checksum[index])
						local->failed = 1;
				}
			}
		} 
	}
	UNLOCK (&frame->lock);

	if (!callcnt) {
		for (index = 0; index < ZR_FILENAME_MAX ; index++) {
			if (local->sh_struct->file_checksum[index] != 
			    local->sh_struct->ns_file_checksum[index]) {
				local->failed = 1;
				break;
			}
			if (local->sh_struct->dir_checksum[index] != 
			    local->sh_struct->ns_dir_checksum[index]) {
				local->failed = 1;
				break;
			}
		}
	
		if (local->failed) {
			/* Log it, it should be a rare event */
			gf_log (this->name, GF_LOG_WARNING, 
				"Self-heal triggered on directory %s", 
				local->loc1.path);

			/* Any self heal will be done at the directory level */
			local->op_ret = -1;
			local->failed = 0;
      
			local->fd = fd_create (local->loc1.inode, 
					       frame->root->pid);
			local->call_count = priv->child_count + 1;
	
			for (index = 0; 
			     index < (priv->child_count + 1); index++) {
				STACK_WIND_COOKIE (frame,
						   unify_bgsh_opendir_cbk,
						   priv->xl_array[index]->name,
						   priv->xl_array[index],
						   priv->xl_array[index]->fops->opendir,
						   &local->loc1,
						   local->fd);
			}
      
			/* opendir can be done on the directory */
			return 0;
		}

		/* no mismatch */
		unify_local_wipe (local);
		STACK_DESTROY (frame->root);
	}

	return 0;
}

/* Background self-heal part over */




/**
 * zr_unify_self_heal - 
 * 
 * @frame: frame used in lookup. get a copy of it, and use that copy.
 * @this: pointer to unify xlator.
 * @inode: pointer to inode, for which the consistency check is required.
 *
 */
int32_t 
zr_unify_self_heal (call_frame_t *frame,
		    xlator_t *this,
		    unify_local_t *local)
{
	unify_private_t *priv = this->private;
	call_frame_t *bg_frame = NULL;
	unify_local_t *bg_local = NULL;
	inode_t *tmp_inode = NULL;
	dict_t *tmp_dict = NULL;
	int16_t index = 0;
  
	if (local->inode_generation < priv->inode_generation) {
		/* Any self heal will be done at the directory level */
		/* Update the inode's generation to the current generation
		   value. */
		local->inode_generation = priv->inode_generation;
		inode_ctx_put (local->loc1.inode, this, 
			  (uint64_t)(long)local->inode_generation);

		if (priv->self_heal == ZR_UNIFY_FG_SELF_HEAL) {
			local->op_ret = 0;
			local->failed = 0;
			local->call_count = priv->child_count + 1;
			local->sh_struct = 
				calloc (1, sizeof (struct unify_self_heal_struct));
      
			/* +1 is for NS */
			for (index = 0; 
			     index < (priv->child_count + 1); index++) {
				STACK_WIND_COOKIE (frame,
						   unify_sh_checksum_cbk,
						   priv->xl_array[index],
						   priv->xl_array[index],
						   priv->xl_array[index]->fops->checksum,
						   &local->loc1,
						   0);
			}

			/* Self-heal in foreground, hence no need 
			   to UNWIND here */
			return 0;
		}

		/* Self Heal done in background */
		bg_frame = copy_frame (frame);
		INIT_LOCAL (bg_frame, bg_local);
		loc_copy (&bg_local->loc1, &local->loc1);
		bg_local->op_ret = 0;
		bg_local->failed = 0;
		bg_local->call_count = priv->child_count + 1;
		bg_local->sh_struct = 
			calloc (1, sizeof (struct unify_self_heal_struct));
    
		/* +1 is for NS */
		for (index = 0; index < (priv->child_count + 1); index++) {
			STACK_WIND_COOKIE (bg_frame,
					   unify_bgsh_checksum_cbk,
					   priv->xl_array[index],
					   priv->xl_array[index],
					   priv->xl_array[index]->fops->checksum,
					   &bg_local->loc1,
					   0);
		}
	}

	/* generation number matches, self heal already done or
	 * self heal done in background: just do STACK_UNWIND 
	 */
	tmp_inode = local->loc1.inode;
	tmp_dict = local->dict;

	unify_local_wipe (local);

	/* This is lookup_cbk ()'s UNWIND. */
	STACK_UNWIND (frame,
		      local->op_ret,
		      local->op_errno,
		      tmp_inode,
		      &local->stbuf,
		      local->dict);

	if (tmp_dict)
		dict_unref (tmp_dict);

	return 0;
}

