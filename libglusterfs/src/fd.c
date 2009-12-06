/*
  Copyright (c) 2007-2009 Gluster, Inc. <http://www.gluster.com>
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
#include "statedump.h"


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
        uint32_t result = 1;

        if (nr < 0) {
                gf_log ("server-protocol/fd",
                                GF_LOG_ERROR,
                                "Negative number passed");
                return -1;
        }

        while (result <= nr)
                result *= 2;

        return result;
}

static int
gf_fd_chain_fd_entries (fdentry_t *entries, uint32_t startidx,
                        uint32_t endcount)
{
        uint32_t        i = 0;

        if (!entries)
                return -1;

        /* Chain only till the second to last entry because we want to
         * ensure that the last entry has GF_FDTABLE_END.
         */
        for (i = startidx; i < (endcount - 1); i++)
                entries[i].next_free = i + 1;

        /* i has already been incremented upto the last entry. */
        entries[i].next_free = GF_FDTABLE_END;

        return 0;
}


static uint32_t 
gf_fd_fdtable_expand (fdtable_t *fdtable, uint32_t nr)
{
	fdentry_t *oldfds = NULL;
	uint32_t oldmax_fds = -1;
  
	if (fdtable == NULL || nr < 0)
	{
		gf_log ("fd", GF_LOG_ERROR, "invalid argument");
		return EINVAL;
	}
  
	nr /= (1024 / sizeof (fdentry_t));
	nr = gf_roundup_power_of_two (nr + 1);
	nr *= (1024 / sizeof (fdentry_t));

	oldfds = fdtable->fdentries;
	oldmax_fds = fdtable->max_fds;

	fdtable->fdentries = CALLOC (nr, sizeof (fdentry_t));
	ERR_ABORT (fdtable->fdentries);
	fdtable->max_fds = nr; 

	if (oldfds) {
		uint32_t cpy = oldmax_fds * sizeof (fdentry_t);
		memcpy (fdtable->fdentries, oldfds, cpy);
	}

        gf_fd_chain_fd_entries (fdtable->fdentries, oldmax_fds,
                                fdtable->max_fds);

        /* Now that expansion is done, we must update the fd list
         * head pointer so that the fd allocation functions can continue
         * using the expanded table.
         */
        fdtable->first_free = oldmax_fds;
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

fdentry_t *
__gf_fd_fdtable_get_all_fds (fdtable_t *fdtable, uint32_t *count)
{
        fdentry_t       *fdentries = NULL;

        if (count == NULL) {
                goto out;
        }

        fdentries = fdtable->fdentries;
        fdtable->fdentries = calloc (fdtable->max_fds, sizeof (fdentry_t));
        gf_fd_chain_fd_entries (fdtable->fdentries, 0, fdtable->max_fds);
        *count = fdtable->max_fds;

out:
        return fdentries;
}

fdentry_t *
gf_fd_fdtable_get_all_fds (fdtable_t *fdtable, uint32_t *count)
{
        fdentry_t       *entries = NULL;
        if (fdtable) {
                pthread_mutex_lock (&fdtable->lock);
                {
                        entries = __gf_fd_fdtable_get_all_fds (fdtable, count);
                }
                pthread_mutex_unlock (&fdtable->lock);
        }

        return entries;
}

void 
gf_fd_fdtable_destroy (fdtable_t *fdtable)
{
        struct list_head  list = {0, };
        fd_t             *fd = NULL;
        fdentry_t        *fdentries = NULL;
        uint32_t          fd_count = 0;
        int32_t           i = 0; 

        INIT_LIST_HEAD (&list);

	if (!fdtable)
                return;

	pthread_mutex_lock (&fdtable->lock);
	{
                fdentries = __gf_fd_fdtable_get_all_fds (fdtable, &fd_count);
		FREE (fdtable->fdentries);
	}
	pthread_mutex_unlock (&fdtable->lock);

        if (fdentries != NULL) {
                for (i = 0; i < fd_count; i++) {
                        fd = fdentries[i].fd;
                        if (fd != NULL) {
                                fd_unref (fd);
                        }
                }

                FREE (fdentries);
		pthread_mutex_destroy (&fdtable->lock);
		FREE (fdtable);
	}
}

int32_t 
gf_fd_unused_get (fdtable_t *fdtable, fd_t *fdptr)
{
	int32_t         fd = -1;
        fdentry_t       *fde = NULL;
	int             error;
        int             alloc_attempts = 0;
  
	if (fdtable == NULL || fdptr == NULL)
	{
		gf_log ("fd", GF_LOG_ERROR, "invalid argument");
		return EINVAL;
	}
  
	pthread_mutex_lock (&fdtable->lock);
	{
fd_alloc_try_again:
                if (fdtable->first_free != GF_FDTABLE_END) {
                        fde = &fdtable->fdentries[fdtable->first_free];
                        fd = fdtable->first_free;
                        fdtable->first_free = fde->next_free;
                        fde->next_free = GF_FDENTRY_ALLOCATED;
                        fde->fd = fdptr;
		} else {
                        /* If this is true, there is something
                         * seriously wrong with our data structures.
                         */
                        if (alloc_attempts >= 2) {
                                gf_log ("server-protocol.c", GF_LOG_ERROR,
                                        "Multiple attempts to expand fd table"
                                        " have failed.");
                                goto out;
                        }
                        error = gf_fd_fdtable_expand (fdtable,
                                                      fdtable->max_fds + 1);
			if (error) {
				gf_log ("server-protocol.c",
					GF_LOG_ERROR,
					"Cannot expand fdtable:%s", strerror (error));
                                goto out;
			}
                        ++alloc_attempts;
                        /* At this point, the table stands expanded
                         * with the first_free referring to the first
                         * free entry in the new set of fdentries that
                         * have just been allocated. That means, the
                         * above logic should just work.
                         */
                        goto fd_alloc_try_again;
		}
	}
out:
	pthread_mutex_unlock (&fdtable->lock);

	return fd;
}


inline void 
gf_fd_put (fdtable_t *fdtable, int32_t fd)
{
	fd_t *fdptr = NULL;
        fdentry_t *fde = NULL;

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
                fde = &fdtable->fdentries[fd];
                /* If the entry is not allocated, put operation must return
                 * without doing anything.
                 * This has the potential of masking out any bugs in a user of
                 * fd that ends up calling gf_fd_put twice for the same fd or
                 * for an unallocated fd, but thats a price we have to pay for
                 * ensuring sanity of our fd-table.
                 */
                if (fde->next_free != GF_FDENTRY_ALLOCATED)
                        goto unlock_out;
                fdptr = fde->fd;
                fde->fd = NULL;
                fde->next_free = fdtable->first_free;
                fdtable->first_free = fd;
	}
unlock_out:
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
		fdptr = fdtable->fdentries[fd].fd;
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
        xlator_t    *xl = NULL;
	int i = 0;
        xlator_t    *old_THIS = NULL;

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
		for (i = 0; i < fd->inode->table->xl->ctx->xl_count; i++) {
			if (fd->_ctx[i].key) {
				xl = (xlator_t *)(long)fd->_ctx[i].key;
                                old_THIS = THIS;
                                THIS = xl;
				if (xl->cbks->releasedir)
					xl->cbks->releasedir (xl, fd);
                                THIS = old_THIS;
			}
		}
        } else {
		for (i = 0; i < fd->inode->table->xl->ctx->xl_count; i++) {
			if (fd->_ctx[i].key) {
				xl = (xlator_t *)(long)fd->_ctx[i].key;
                                old_THIS = THIS;
                                THIS = xl;
				if (xl->cbks->release)
					xl->cbks->release (xl, fd);
                                THIS = old_THIS;
			}
		}
        }
        
        LOCK_DESTROY (&fd->lock);

	FREE (fd->_ctx);
        inode_unref (fd->inode);
        fd->inode = (inode_t *)0xaaaaaaaa;
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
__fd_ctx_set (fd_t *fd, xlator_t *xlator, uint64_t value)
{
	int index = 0;
        int ret = 0;
        int set_idx = -1;

	if (!fd || !xlator)
		return -1;
        
        for (index = 0; index < xlator->ctx->xl_count; index++) {
                if (!fd->_ctx[index].key) {
                        if (set_idx == -1)
                                set_idx = index;
                        /* dont break, to check if key already exists
                           further on */
                }
                if (fd->_ctx[index].key == (uint64_t)(long) xlator) {
                        set_idx = index;
                        break;
                }
        }
	
        if (set_idx == -1) {
                ret = -1;
                goto out;
        }
        
        fd->_ctx[set_idx].key   = (uint64_t)(long) xlator;
        fd->_ctx[set_idx].value = value;

out:
	return ret;
}


int
fd_ctx_set (fd_t *fd, xlator_t *xlator, uint64_t value)
{
        int ret = 0;

	if (!fd || !xlator)
		return -1;
        
        LOCK (&fd->lock);
        {
                ret = __fd_ctx_set (fd, xlator, value);
        }
        UNLOCK (&fd->lock);

        return ret;
}


int 
__fd_ctx_get (fd_t *fd, xlator_t *xlator, uint64_t *value)
{
	int index = 0;
        int ret = 0;

	if (!fd || !xlator)
		return -1;
        
        for (index = 0; index < xlator->ctx->xl_count; index++) {
                if (fd->_ctx[index].key == (uint64_t)(long)xlator)
                        break;
        }
        
        if (index == xlator->ctx->xl_count) {
                ret = -1;
                goto out;
        }

        if (value) 
                *value = fd->_ctx[index].value;
        
out:
	return ret;
}


int 
fd_ctx_get (fd_t *fd, xlator_t *xlator, uint64_t *value)
{
        int ret = 0;

	if (!fd || !xlator)
		return -1;

        LOCK (&fd->lock);
        {
                ret = __fd_ctx_get (fd, xlator, value);
        }
        UNLOCK (&fd->lock);

        return ret;
}


int 
__fd_ctx_del (fd_t *fd, xlator_t *xlator, uint64_t *value)
{
	int index = 0;
        int ret = 0;

	if (!fd || !xlator)
		return -1;
        
        for (index = 0; index < xlator->ctx->xl_count; index++) {
                if (fd->_ctx[index].key == (uint64_t)(long)xlator)
                        break;
        }
        
        if (index == xlator->ctx->xl_count) {
                ret = -1;
                goto out;
        }
        
        if (value) 
                *value = fd->_ctx[index].value;		
        
        fd->_ctx[index].key   = 0;
        fd->_ctx[index].value = 0;

out:
	return ret;
}


int 
fd_ctx_del (fd_t *fd, xlator_t *xlator, uint64_t *value)
{
        int ret = 0;

	if (!fd || !xlator)
		return -1;
        
        LOCK (&fd->lock);
        {
                ret = __fd_ctx_del (fd, xlator, value);
        }
        UNLOCK (&fd->lock);

        return ret;
}


void
fd_dump (fd_t *fd, char *prefix)
{
        char        key[GF_DUMP_MAX_BUF_LEN];

        if (!fd)
                return;
    
        memset(key, 0, sizeof(key));
        gf_proc_dump_build_key(key, prefix, "pid");
        gf_proc_dump_write(key, "%d", fd->pid);
        gf_proc_dump_build_key(key, prefix, "refcount");
        gf_proc_dump_write(key, "%d", fd->refcount);
        gf_proc_dump_build_key(key, prefix, "flags");
        gf_proc_dump_write(key, "%d", fd->flags);
        if (fd->inode) {
                gf_proc_dump_build_key(key, prefix, "inode");
                gf_proc_dump_write(key, "%ld", fd->inode->ino);
        }
}


void
fdentry_dump (fdentry_t *fdentry, char *prefix)
{
        if (!fdentry)
                return;

        if (GF_FDENTRY_ALLOCATED != fdentry->next_free)
                return;

        if (fdentry->fd) 
                fd_dump(fdentry->fd, prefix);
}

void
fdtable_dump (fdtable_t *fdtable, char *prefix)
{
        char    key[GF_DUMP_MAX_BUF_LEN];
        int     i = 0;
        int     ret = -1;

        if (!fdtable)
                return;
    
	ret = pthread_mutex_trylock (&fdtable->lock);

        if (ret) {
		gf_log ("fd", GF_LOG_WARNING, "Unable to acquire lock");
                return;
        }

        memset(key, 0, sizeof(key));
        gf_proc_dump_build_key(key, prefix, "refcount");
        gf_proc_dump_write(key, "%d", fdtable->refcount);
        gf_proc_dump_build_key(key, prefix, "maxfds");
        gf_proc_dump_write(key, "%d", fdtable->max_fds);
        gf_proc_dump_build_key(key, prefix, "first_free");
        gf_proc_dump_write(key, "%d", fdtable->first_free);

        for ( i = 0 ; i < fdtable->max_fds; i++) {
                if (GF_FDENTRY_ALLOCATED == 
                                fdtable->fdentries[i].next_free) {
                        gf_proc_dump_build_key(key, prefix, "fdentry[%d]", i);
                        gf_proc_dump_add_section(key);
                        fdentry_dump(&fdtable->fdentries[i], key);
                }
        }

        pthread_mutex_unlock(&fdtable->lock);
}

