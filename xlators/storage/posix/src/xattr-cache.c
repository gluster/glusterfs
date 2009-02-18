/*
  Copyright (c) 2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include "byte-order.h"

#include "xattr-cache.h"
#include "posix.h"
#include "compat-errno.h"

static int
__hgetxattr (xattr_cache_handle_t *handle, xlator_t *this, 
	     const char *key, void *value, size_t len)
{
	char *            real_path = NULL;
	struct posix_fd * pfd = NULL;
	uint64_t          tmp_pfd = 0;
	int op_ret = -1;
	int ret    = -1;
	int _fd    = -1;

	if (handle->loc.path) {
		MAKE_REAL_PATH (real_path, this, handle->loc.path);
		op_ret = lgetxattr (real_path, key, value, len);

		if (op_ret == -1)
			op_ret = -errno;
	} else {
		ret = fd_ctx_get (handle->fd, this, &tmp_pfd);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to get pfd from fd=%p",
                                handle->fd);
                        op_ret = -EBADFD;
			goto out;
                }
		pfd = (struct posix_fd *)(long)tmp_pfd;
                _fd = pfd->fd;

		op_ret = fgetxattr (_fd, key, value, len);
		if (op_ret == -1)
			op_ret = -errno;
	}

out:
	return op_ret;
}


static int
__hsetxattr (xattr_cache_handle_t *handle, xlator_t *this,
	     const char *key, void *value, size_t len, int flags)
{
	char *            real_path = NULL;
	struct posix_fd * pfd = NULL;
	uint64_t          tmp_pfd = 0;
	int op_ret = -1;
	int ret    = -1;
	int _fd    = -1;

	if (handle->loc.path) {
		MAKE_REAL_PATH (real_path, this, handle->loc.path);

		op_ret = lsetxattr (real_path, key, value, len, flags);
		if (op_ret == -1)
			op_ret = -errno;
	} else {
		ret = fd_ctx_get (handle->fd, this, &tmp_pfd);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to get pfd from fd=%p",
                                handle->fd);

			op_ret = -EBADFD;
			goto out;
                }
		pfd = (struct posix_fd *)(long)tmp_pfd;
		
                _fd = pfd->fd;

		op_ret = fsetxattr (_fd, key, value, len, flags);
		if (op_ret == -1)
			op_ret = -errno;
	}

out:
	return op_ret;
}


static xattr_cache_entry_t *
__cache_lookup (xattr_cache_t *cache, inode_t *inode, char *key)
{
	int i = 0;

	for (i = 0; i < cache->size; i++) {
		if ((cache->entries[i]->inode == inode)
		    && (!strcmp (cache->entries[i]->key, key))) {
			cache->entries[i]->nraccess++;
			return cache->entries[i];
		}
	}

	return NULL;
}


static xattr_cache_entry_t *
__cache_least_used_entry (xattr_cache_t *cache)
{
	xattr_cache_entry_t *lue = cache->entries[0];
	int i;

	for (i = 0; i < cache->size; i++) {
		if (cache->entries[i]->nraccess < lue->nraccess)
			lue = cache->entries[i];
	}

	lue->nraccess++;
	return lue;
}


static inode_t *
__inode_for_handle (xattr_cache_handle_t *handle)
{
	inode_t *inode = NULL;

	if (handle->loc.path)
		inode = handle->loc.inode;
	else if (handle->fd)
		inode = handle->fd->inode;

	return inode;
}


static void
__free_handle (xattr_cache_handle_t *handle)
{
	if (handle->loc.path)
		loc_wipe (&handle->loc);
	
	FREE (handle);
}


static xattr_cache_handle_t *
__copy_handle (xattr_cache_handle_t *handle)
{
	xattr_cache_handle_t *hnew = calloc (1, sizeof (xattr_cache_handle_t));
	
	if (handle->loc.path)
		loc_copy (&hnew->loc, &handle->loc);
	else
		hnew->fd = handle->fd;

	return hnew;
}


static int
__cache_populate_entry (xattr_cache_entry_t *entry, xlator_t *this,
			xattr_cache_handle_t *handle, char *key, size_t len)
{
	int op_ret = -1;

	entry->array = calloc (1, len);
	if (!entry->array) {
		op_ret = -ENOMEM;
		goto out;
	}

	op_ret = __hgetxattr (handle, this, key, entry->array, len);

	entry->key      = strdup (key);
	entry->inode    = __inode_for_handle (handle);
	entry->handle   = __copy_handle (handle);
	entry->len      = len;
	entry->nraccess = 1;

out:
	return op_ret;
}


static int
__cache_flush_entry (xattr_cache_entry_t *entry, xlator_t *this)
{
	int ret = -1;

	if (entry->dirty) {
		ret = __hsetxattr (entry->handle, this, 
				   entry->key, entry->array, entry->len, 0);
	}

	entry->len      = 0;
	entry->nraccess = 0;
	entry->dirty    = 0;
	entry->inode    = NULL;

	if (entry->key) {
		FREE (entry->key);
		entry->key = NULL;
	}
	
	if (entry->array) {
		FREE (entry->array);
		entry->array = NULL;
	}

	if (entry->handle) {
		__free_handle (entry->handle);
		entry->handle = NULL;
	}

	return 0;
}


static void
__print_array (char *str, xlator_t *this, int32_t *array, size_t len)
{
	char *ptr = NULL;
	char *buf = NULL;

	int i, count = -1;

	count = len / sizeof (int32_t);

	/* 10 digits per entry + 1 space + '[' and ']' */
	buf = malloc (count * 11 + 8);

	ptr = buf;
	ptr += sprintf (ptr, "[ ");
	for (i = 0; i < count; i++)
		ptr += sprintf (ptr, "%d ", ntoh32 (array[i]));
	ptr += sprintf (ptr, "]");

	gf_log (this->name, GF_LOG_DEBUG,
		"%s%s", str, buf);

	FREE (buf);
}


int 
posix_xattr_cache_read (xlator_t *this, xattr_cache_handle_t *handle, 
			char *key, int32_t *array, size_t len)
{
	xattr_cache_entry_t *entry  = NULL;
	xattr_cache_entry_t *purgee = NULL;

	xattr_cache_t *cache = NULL;
	inode_t *inode = NULL;

	int op_ret = -1;

	inode = __inode_for_handle (handle);
	
	if (!inode) {
		gf_log (this->name, GF_LOG_DEBUG,
			"handle has no inode!");
		goto out;
	}

	cache = ((struct posix_private *) (this->private))->xattr_cache;

	pthread_mutex_lock (&cache->lock);
	{
		entry = __cache_lookup (cache, inode, key);

		if (entry) {
			if (handle->loc.path)
				gf_log (this->name, GF_LOG_DEBUG,
					"cache hit for %s", handle->loc.path);
			else if (handle->fd)
				gf_log (this->name, GF_LOG_DEBUG,
					"cache hit for fd=%p", handle->fd);
		}

		if (!entry) {
			purgee = __cache_least_used_entry (cache);

			if (purgee->handle && purgee->handle->loc.path)
				gf_log (this->name, GF_LOG_DEBUG,
					"flushing and purging entry for %s",
					purgee->handle->loc.path);
			else if (purgee->handle && purgee->handle->fd)
				gf_log (this->name, GF_LOG_DEBUG,
					"flushing and purging entry for fd=%p", 
					purgee->handle->fd);
			__cache_flush_entry (purgee, this);

			if (handle->loc.path)
				gf_log (this->name, GF_LOG_DEBUG,
					"populating entry for %s",
					handle->loc.path);
			else if (handle->fd)
				gf_log (this->name, GF_LOG_DEBUG,
					"populating entry for fd=%p", 
					handle->fd);
			__cache_populate_entry (purgee, this, handle, key, len);

			entry = purgee;
		}

		memcpy (array, entry->array, len);

		__print_array ("read array: ", this, array, len);
	}
	pthread_mutex_unlock (&cache->lock);

	op_ret = 0;
out:
	return op_ret;
}


int posix_xattr_cache_write (xlator_t *this, xattr_cache_handle_t *handle, 
			     char *key, int32_t *array, size_t len)
{
	xattr_cache_t       * cache = NULL;
	xattr_cache_entry_t * entry = NULL;

	inode_t *inode = NULL;

	int op_ret = -1;

	inode = __inode_for_handle (handle);
	
	if (!inode) {
		gf_log (this->name, GF_LOG_DEBUG,
			"handle has no inode!");
		goto out;
	}

	cache = ((struct posix_private *) (this->private))->xattr_cache;
	
	pthread_mutex_lock (&cache->lock);
	{
		entry = __cache_lookup (cache, inode, key);

		if (entry) {
			entry->dirty = 1;
			memcpy (entry->array, array, len);
		} else {
			/*
			 * This case shouldn't usually happen, since the
			 * entry should have been brought into the cache
			 * by the previous read (xattrop always does a read &
			 * write).
			 *
			 * If we've reached here, it means things are happening
			 * very quickly and the entry was flushed after read
			 * but before this write. In that case, let's just
			 * write this to disk
			 */
			 
			op_ret = __hsetxattr (handle, this, key, array,
					      len, 0);
		}

		__print_array ("wrote array: ", this, array, len);
	}
	pthread_mutex_unlock (&cache->lock);

	op_ret = 0;
out:
	return op_ret;
}


int posix_xattr_cache_flush (xlator_t *this, xattr_cache_handle_t *handle)
{
	xattr_cache_t       *cache = NULL;
	xattr_cache_entry_t *entry = NULL;

	int i;
	inode_t *inode = NULL;

	int op_ret = -1;

	inode = __inode_for_handle (handle);
	if (!inode) {
		gf_log (this->name, GF_LOG_DEBUG,
			"handle has no inode!");
		op_ret = -EINVAL;
		goto out;
	}

	cache = ((struct posix_private *) (this->private))->xattr_cache;

	pthread_mutex_lock (&cache->lock);
	{
		for (i = 0; i < cache->size; i++) {
			entry = cache->entries[i];

			if (entry->inode == inode) {
				if (entry->handle->loc.path)
					gf_log (this->name, GF_LOG_DEBUG,
						"force flushing entry for %s",
						entry->handle->loc.path);
				
				else if (cache->entries[i]->handle->fd)
					gf_log (this->name, GF_LOG_DEBUG,
						"force flushing entry for fd=%p", 
						entry->handle->fd);
				
				__cache_flush_entry (entry, this);
			}
		}
	}
	pthread_mutex_unlock (&cache->lock);

	op_ret = 0;
out:
	return op_ret;
}


int
posix_xattr_cache_flush_all (xlator_t *this)
{
	xattr_cache_t       *cache = NULL;
	xattr_cache_entry_t *entry = NULL;

	int i;
	int op_ret = 0;

	cache = ((struct posix_private *) (this->private))->xattr_cache;

	pthread_mutex_lock (&cache->lock);
	{
		gf_log (this->name, GF_LOG_DEBUG,
			"flushing entire xattr cache: ");

		for (i = 0; i < cache->size; i++) {
			entry = cache->entries[i];

			if (!entry || !entry->handle)
				continue;

			if (entry->handle->loc.path)
				gf_log (this->name, GF_LOG_DEBUG,
					"  force flushing entry for %s",
					entry->handle->loc.path);
			
			else if (cache->entries[i]->handle->fd)
				gf_log (this->name, GF_LOG_DEBUG,
					"  force flushing entry for fd=%p", 
					entry->handle->fd);
			
			__cache_flush_entry (entry, this);
		}
	}
	pthread_mutex_unlock (&cache->lock);

	return op_ret;
}


xattr_cache_t *
posix_xattr_cache_init (size_t size)
{
	int i = 0;
	xattr_cache_t * cache = NULL;
	int op_ret = -1;

	cache = CALLOC (1, sizeof (xattr_cache_t));
	if (!cache) {
		goto out;
	}

	cache->entries = CALLOC (size, sizeof (xattr_cache_entry_t *));
	if (!cache->entries)
		goto out;

	cache->size = size;

	for (i = 0; i < size; i++) {
		cache->entries[i] = calloc (1, sizeof (xattr_cache_entry_t));
		if (!cache->entries[i])
			goto out;
	}

	pthread_mutex_init (&cache->lock, NULL);

	op_ret = 0;
out:
	if (op_ret == -1) {
		if (cache) {
			if (cache->entries) {
				for (i = 0; i < size; i++)
					if (cache->entries[i])
						FREE (cache->entries[i]);

				FREE (cache->entries);
			}
			
			FREE (cache);
		}
	}

	return cache;
}
