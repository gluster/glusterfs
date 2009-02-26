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

#include "fd.h"
#include "glusterfs.h"
#include "inode.h"
#include "dict.h"


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif


static uint32_t 
gf_fd_fdtable_expand (fdtable_t *fdtable, uint32_t nr);

static fd_t *
_fd_ref (fd_t *fd);

/* 
   Allocate in memory chunks of power of 2 starting from 1024B 
   Assumes fdtable->lock is held
*/
static inline uint32_t 
gf_roundup_power_of_two (uint32_t nr)
{
	uint32_t result = nr;

	if (nr < 0) {
		gf_log ("server-protocol/fd",
			GF_LOG_ERROR,
			"Negative number passed");
		return -1;
	}

	switch (nr) {
	case 0:
	case 1: 
		result = 1;
		break;

	default:
	{
		uint32_t cnt = 0, tmp = nr;
		uint8_t remainder = 0;
		while (tmp != 1){
			if (tmp % 2)
				remainder = 1;
			tmp /= 2;
			cnt++;
		}

		if (remainder)
			result = 0x1 << (cnt + 1);
		break;
	}
	}

	return result;
}

static uint32_t 
gf_fd_fdtable_expand (fdtable_t *fdtable, uint32_t nr)
{
	fd_t **oldfds = NULL;
	uint32_t oldmax_fds = -1;
  
	if (fdtable == NULL || nr < 0)
	{
		gf_log ("fd", GF_LOG_ERROR, "invalid argument");
		return EINVAL;
	}
  
	nr /= (1024 / sizeof (fd_t *));
	nr = gf_roundup_power_of_two (nr + 1);
	nr *= (1024 / sizeof (fd_t *));

	oldfds = fdtable->fds;
	oldmax_fds = fdtable->max_fds;

	fdtable->fds = CALLOC (nr, sizeof (fd_t *));
	ERR_ABORT (fdtable->fds);
	fdtable->max_fds = nr; 

	if (oldfds) {
		uint32_t cpy = oldmax_fds * sizeof (fd_t *);
		memcpy (fdtable->fds, oldfds, cpy);
	}

	FREE (oldfds);
	return 0;
}

fdtable_t *
gf_fd_fdtable_alloc (void) 
{
	fdtable_t *fdtable = NULL;

	fdtable = CALLOC (1, sizeof (*fdtable));
	if (!fdtable) 
		return NULL;

	pthread_mutex_init (&fdtable->lock, NULL);

	pthread_mutex_lock (&fdtable->lock);
	{
		gf_fd_fdtable_expand (fdtable, 0);
	}
	pthread_mutex_unlock (&fdtable->lock);

	return fdtable;
}

void 
gf_fd_fdtable_destroy (fdtable_t *fdtable)
{
				
	int32_t i = 0;

	if (fdtable) {
		pthread_mutex_lock (&fdtable->lock);
		{
			for (i=0; i < fdtable->max_fds; i++) {
				if (fdtable->fds[i]) {
					fd_t *fd = fdtable->fds[i];
						  
					fd_unref (fd);
				}
			}

			FREE (fdtable->fds);
		}
		pthread_mutex_unlock (&fdtable->lock);
		pthread_mutex_destroy (&fdtable->lock);
		FREE (fdtable);
	}
}

int32_t
gf_fd_unused_get2 (fdtable_t *fdtable, fd_t *fdptr, int32_t fd)
{
	int32_t ret = -1;
	
	if (fdtable == NULL || fdptr == NULL || fd < 0)
	{
		gf_log ("fd", GF_LOG_ERROR, "invalid argument");
		errno = EINVAL;
		return -1;
	}
 
	pthread_mutex_lock (&fdtable->lock);
	{
		while (fdtable->max_fds < fd) {
			int error = 0;
			error = gf_fd_fdtable_expand (fdtable, fdtable->max_fds + 1);
			if (error) 
			{
				gf_log ("fd.c",
					GF_LOG_ERROR,
					"Cannot expand fdtable:%s", strerror (error));
				goto err;
			}
		}
		
		if (!fdtable->fds[fd]) 
		{
			fdtable->fds[fd] = fdptr;
			fd_ref (fdptr);
			ret = fd;
		} 
		else 
		{
			gf_log ("fd.c",
				GF_LOG_ERROR,
				"Cannot allocate fd %d (slot not empty in fdtable)", fd);
		}
	}
err:
	pthread_mutex_unlock (&fdtable->lock);
	
	return ret;
}

  
int32_t 
gf_fd_unused_get (fdtable_t *fdtable, fd_t *fdptr)
{
	int32_t fd = -1, i = 0;
  
	if (fdtable == NULL || fdptr == NULL)
	{
		gf_log ("fd", GF_LOG_ERROR, "invalid argument");
		return EINVAL;
	}
  
	pthread_mutex_lock (&fdtable->lock);
	{
		for (i = 0; i<fdtable->max_fds; i++) 
		{
			if (!fdtable->fds[i])
				break;
		}

		if (i < fdtable->max_fds) {
			fdtable->fds[i] = fdptr;
			fd = i;
		} else {
			int32_t error;
			error = gf_fd_fdtable_expand (fdtable, fdtable->max_fds + 1);
			if (error) {
				gf_log ("server-protocol.c",
					GF_LOG_ERROR,
					"Cannot expand fdtable:%s", strerror (error));
			} else {
				fdtable->fds[i] = fdptr;
				fd = i;
			}
		}
	}
	pthread_mutex_unlock (&fdtable->lock);

	return fd;
}


inline void 
gf_fd_put (fdtable_t *fdtable, int32_t fd)
{
	fd_t *fdptr = NULL;
	if (fdtable == NULL || fd < 0)
	{
		gf_log ("fd", GF_LOG_ERROR, "invalid argument");
		return;
	}
  
	if (!(fd < fdtable->max_fds))
	{
		gf_log ("fd", GF_LOG_ERROR, "invalid argument");
		return;
	}

	pthread_mutex_lock (&fdtable->lock);
	{
		fdptr = fdtable->fds[fd];
		fdtable->fds[fd] = NULL;
	}
	pthread_mutex_unlock (&fdtable->lock);

	if (fdptr) {
		fd_unref (fdptr);
	}
}


fd_t *
gf_fd_fdptr_get (fdtable_t *fdtable, int64_t fd)
{
	fd_t *fdptr = NULL;
  
	if (fdtable == NULL || fd < 0)
	{
		gf_log ("fd", GF_LOG_ERROR, "invalid argument");
		errno = EINVAL;
		return NULL;
	}
  
	if (!(fd < fdtable->max_fds))
	{
		gf_log ("fd", GF_LOG_ERROR, "invalid argument");
		errno = EINVAL;
		return NULL;
	}

	pthread_mutex_lock (&fdtable->lock);
	{
		fdptr = fdtable->fds[fd];
		if (fdptr) {
			fd_ref (fdptr);
		}
	}
	pthread_mutex_unlock (&fdtable->lock);

	return fdptr;
}

fd_t *
_fd_ref (fd_t *fd)
{
	++fd->refcount;
	
	return fd;
}

fd_t *
fd_ref (fd_t *fd)
{
	fd_t *refed_fd = NULL;

	if (!fd) {
		gf_log ("fd", GF_LOG_ERROR, "@fd=%p", fd);
		return NULL;
	}

	LOCK (&fd->inode->lock);
	refed_fd = _fd_ref (fd);
	UNLOCK (&fd->inode->lock);
	
	return refed_fd;
}

fd_t *
_fd_unref (fd_t *fd)
{
	assert (fd->refcount);

	--fd->refcount;

	if (fd->refcount == 0){
		list_del_init (&fd->inode_list);
	}
	
	return fd;
}

static void
fd_destroy (fd_t *fd)
{
        data_pair_t *pair = NULL;
        xlator_t    *xl = NULL;
	int i = 0;

        if (fd == NULL){
                gf_log ("xlator", GF_LOG_ERROR, "invalid arugument");
                goto out;
        }
  
        if (fd->inode == NULL){
                gf_log ("xlator", GF_LOG_ERROR, "fd->inode is NULL");
                goto out;
        }
	if (!fd->_ctx)
		goto out;

        if (S_ISDIR (fd->inode->st_mode)) {
                for (pair = fd->ctx->members_list; pair; pair = pair->next) {
                        /* notify all xlators which have a context */
                        xl = xlator_search_by_name (fd->inode->table->xl, 
						    pair->key);
          
                        if (!xl) {
                                gf_log ("fd", GF_LOG_CRITICAL,
                                        "fd(%p)->ctx has invalid key(%s)",
                                        fd, pair->key);
                                continue;
                        }
                        if (xl->cbks->releasedir) {
                                xl->cbks->releasedir (xl, fd);
                        } else {
                                gf_log ("fd", GF_LOG_CRITICAL,
                                        "xlator(%s) in fd(%p) no RELEASE cbk",
                                        xl->name, fd);
                        }

                }
		for (i = 0; i < fd->inode->table->xl->ctx->xl_count; i++) {
			if (fd->_ctx[i].key) {
				xl = (xlator_t *)(long)fd->_ctx[i].key;
				if (xl->cbks->releasedir)
					xl->cbks->releasedir (xl, fd);
			}
		}
        } else {
                for (pair = fd->ctx->members_list; pair; pair = pair->next) {
                        /* notify all xlators which have a context */
                        xl = xlator_search_by_name (fd->inode->table->xl, 
						    pair->key);
          
                        if (!xl) {
                                gf_log ("fd", GF_LOG_CRITICAL,
                                        "fd(%p)->ctx has invalid key(%s)",
                                        fd, pair->key);
                                continue;
                        }
                        if (xl->cbks->release) {
                                xl->cbks->release (xl, fd);
                        } else {
                                gf_log ("fd", GF_LOG_CRITICAL,
                                        "xlator(%s) in fd(%p) no RELEASE cbk",
                                        xl->name, fd);
                        }
                }
		for (i = 0; i < fd->inode->table->xl->ctx->xl_count; i++) {
			if (fd->_ctx[i].key) {
				xl = (xlator_t *)(long)fd->_ctx[i].key;
				if (xl->cbks->release)
					xl->cbks->release (xl, fd);
			}
		}
        }
        
        LOCK_DESTROY (&fd->lock);

	FREE (fd->_ctx);
        inode_unref (fd->inode);
        fd->inode = (inode_t *)0xaaaaaaaa;
        dict_destroy (fd->ctx);
        FREE (fd);
        
out:
        return;
}

void
fd_unref (fd_t *fd)
{
        int32_t refcount = 0;

        if (!fd) {
                gf_log ("fd.c", GF_LOG_ERROR, "fd is NULL");
                return;
        }
        
        LOCK (&fd->inode->lock);
        {
                _fd_unref (fd);
                refcount = fd->refcount;
        }
        UNLOCK (&fd->inode->lock);
        
        if (refcount == 0) {
                fd_destroy (fd);
        }

        return ;
}

fd_t *
fd_bind (fd_t *fd)
{
        inode_t *inode = fd->inode;

        if (!fd) {
                gf_log ("fd.c", GF_LOG_ERROR, "fd is NULL");
                return NULL;
        }

        LOCK (&inode->lock);
        {
                list_add (&fd->inode_list, &inode->fd_list);
        }
        UNLOCK (&inode->lock);
        
        return fd;
}

fd_t *
fd_create (inode_t *inode, pid_t pid)
{
        fd_t *fd = NULL;
  
        if (inode == NULL) {
                gf_log ("fd", GF_LOG_ERROR, "invalid argument");
                return NULL;
        }
  
        fd = CALLOC (1, sizeof (fd_t));
        ERR_ABORT (fd);
  
	fd->_ctx = CALLOC (1, (sizeof (struct _fd_ctx) * 
			       inode->table->xl->ctx->xl_count));
        fd->ctx = get_new_dict ();
        fd->inode = inode_ref (inode);
        fd->pid = pid;
        INIT_LIST_HEAD (&fd->inode_list);
        
        LOCK_INIT (&fd->lock);

        LOCK (&inode->lock);
        fd = _fd_ref (fd);
        UNLOCK (&inode->lock);

        return fd;
}

fd_t *
fd_lookup (inode_t *inode, pid_t pid)
{
        fd_t *fd = NULL;
        fd_t *iter_fd = NULL;

        LOCK (&inode->lock);
        {
                if (list_empty (&inode->fd_list)) {
                        fd = NULL;
                } else {
                        list_for_each_entry (iter_fd, &inode->fd_list, inode_list) {
                                if (pid) {
                                        if (iter_fd->pid == pid) {
                                                fd = _fd_ref (iter_fd);
                                                break;
                                        }
                                } else {
                                        fd = _fd_ref (iter_fd);
                                        break;
                                }
                        }
                }
        }
        UNLOCK (&inode->lock);
        
        return fd;
}

uint8_t
fd_list_empty (inode_t *inode)
{
        uint8_t empty = 0; 

        LOCK (&inode->lock);
        {
                empty = list_empty (&inode->fd_list);
        }
        UNLOCK (&inode->lock);
        
        return empty;
}

int
fd_ctx_set (fd_t *fd, xlator_t *xlator, uint64_t value)
{
	int index = 0;
        int ret = 0;

	if (!fd || !xlator)
		return -1;
        
        LOCK (&fd->lock);
        {
                for (index = 0; index < xlator->ctx->xl_count; index++) {
                        if (!fd->_ctx[index].key || 
                            (fd->_ctx[index].key == (uint64_t)(long)xlator))
                                break;
                }
	
                if (index == xlator->ctx->xl_count) {
                        ret = -1;
                        goto unlock;
                }

                fd->_ctx[index].key   = (uint64_t)(long) xlator;
                fd->_ctx[index].value = value;
        }
unlock:
        UNLOCK (&fd->lock);

	return ret;
}

int 
fd_ctx_get (fd_t *fd, xlator_t *xlator, uint64_t *value)
{
	int index = 0;
        int ret = 0;

	if (!fd || !xlator)
		return -1;
        
        LOCK (&fd->lock);
        {
                for (index = 0; index < xlator->ctx->xl_count; index++) {
                        if (fd->_ctx[index].key == (uint64_t)(long)xlator)
                                break;
                }

                if (index == xlator->ctx->xl_count) {
                        ret = -1;
                        goto unlock;
                }

                if (value) 
                        *value = fd->_ctx[index].value;
        }
unlock:
        UNLOCK (&fd->lock);

	return ret;
}


int 
fd_ctx_del (fd_t *fd, xlator_t *xlator, uint64_t *value)
{
	int index = 0;
        int ret = 0;

	if (!fd || !xlator)
		return -1;
        
        LOCK (&fd->lock);
        {
                for (index = 0; index < xlator->ctx->xl_count; index++) {
                        if (fd->_ctx[index].key == (uint64_t)(long)xlator)
                                break;
                }

                if (index == xlator->ctx->xl_count) {
                        ret = -1;
                        goto unlock;
                }

                if (value) 
                        *value = fd->_ctx[index].value;		

                fd->_ctx[index].key   = 0;
                fd->_ctx[index].value = 0;
        }
unlock:
        UNLOCK (&fd->lock);

	return ret;
}
