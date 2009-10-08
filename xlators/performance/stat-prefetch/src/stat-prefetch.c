/*
  Copyright (c) 2009-2010 Gluster, Inc. <http://www.gluster.com>
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

#include "stat-prefetch.h"
#include "locking.h"
#include "inode.h"
#include <libgen.h>

#define GF_SP_CACHE_BUCKETS 4096

inline uint32_t
sp_hashfn (void *data, int len)
{
        return gf_dm_hashfn ((const char *)data, len);
}
 
sp_cache_t *
sp_cache_init (void)
{
        sp_cache_t *cache = NULL;

        cache = CALLOC (1, sizeof (*cache));
        if (cache) {
                cache->table = rbthash_table_init (GF_SP_CACHE_BUCKETS,
                                                   sp_hashfn,
                                                   free);
                if (cache->table == NULL) {
                        FREE (cache);
                        cache = NULL;
                        goto out;
                }

                LOCK_INIT (&cache->lock);
        }

out:
        return cache;
}

        
void
sp_local_free (sp_local_t *local)
{
        if (local) {
                loc_wipe (&local->loc);
                FREE (local);
        }
}


int32_t
sp_cache_remove_entry (sp_cache_t *cache, char *name, char remove_all)
{
        int32_t          ret   = -1;
        rbthash_table_t *table = NULL;

        if ((cache == NULL) || ((name == NULL) && !remove_all)) {
                goto out;
        }

        LOCK (&cache->lock);
        {
                if (remove_all) {
                        table = cache->table;
                        cache->table = rbthash_table_init (GF_SP_CACHE_BUCKETS,
                                                           sp_hashfn,
                                                           free);
                        if (cache->table == NULL) {
                                cache->table = table;
                        } else {
                                rbthash_table_destroy (table);
                                ret = 0;
                        }
                } else {
                        rbthash_remove (cache->table, name, strlen (name));
                        ret = 0;
                }
        }
        UNLOCK (&cache->lock);

out:
        return ret;    
}


int32_t
sp_cache_get_entry (sp_cache_t *cache, char *name, gf_dirent_t *entry)
{
        int32_t      ret = -1;
        gf_dirent_t *tmp = NULL;

        if ((cache == NULL) || (name == NULL) || (entry == NULL)) {
                goto out;
        }

        LOCK (&cache->lock);
        {
                tmp = rbthash_get (cache->table, name, strlen (name));
                if (tmp != NULL) {
                        memcpy (entry, tmp, sizeof (*entry));
                        ret = 0;
                }
        }
        UNLOCK (&cache->lock);

out:
        return ret;
}
 

void
sp_cache_free (sp_cache_t *cache)
{
        sp_cache_remove_entry (cache, NULL, 1);
        rbthash_table_destroy (cache->table);
        FREE (cache);
}


sp_cache_t *
sp_get_cache_fd (xlator_t *this, fd_t *fd)
{
        sp_cache_t  *cache     = NULL;
        uint64_t     value     = 0;
        int32_t      ret       = -1;
        sp_fd_ctx_t *fd_ctx = NULL;

        if (fd == NULL) {
                goto out;
        }

        ret = fd_ctx_get (fd, this, &value);
        if (ret == -1) {
                goto out;
        }

        fd_ctx = (void *)(long) value;

        LOCK (&fd_ctx->lock);
        {
                cache = fd_ctx->cache;
        }
        UNLOCK (&fd_ctx->lock);
out:
        return cache;
}


void
sp_fd_ctx_free (sp_fd_ctx_t *fd_ctx)
{
        if (fd_ctx == NULL) {
                goto out;
        }

        if (fd_ctx->parent_inode) {
                inode_unref (fd_ctx->parent_inode);
                fd_ctx->parent_inode = NULL;
        }
                
        if (fd_ctx->name) {
                FREE (fd_ctx->name);
                fd_ctx->name = NULL;
        }

        if (fd_ctx->cache) {
                sp_cache_free (fd_ctx->cache);
        }

        FREE (fd_ctx);
out:
        return;
}
 

inline sp_fd_ctx_t *
sp_fd_ctx_init (void)
{
        sp_fd_ctx_t *fd_ctx = NULL;

        fd_ctx = CALLOC (1, sizeof (*fd_ctx));
        if (fd_ctx) {
                LOCK_INIT (&fd_ctx->lock);
        }

        return fd_ctx;
}


sp_fd_ctx_t *
sp_fd_ctx_new (xlator_t *this, inode_t *parent, char *name, sp_cache_t *cache)
{
        sp_fd_ctx_t *fd_ctx = NULL;

        fd_ctx = sp_fd_ctx_init ();
        if (fd_ctx == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto out;
        }

        if (parent) {
                fd_ctx->parent_inode = inode_ref (parent);
        }

        if (name) {
                fd_ctx->name = strdup (name);
                if (fd_ctx->name == NULL) {
                        sp_fd_ctx_free (fd_ctx);
                        fd_ctx = NULL;
                }
        }

        fd_ctx->cache = cache;

out:
        return fd_ctx;
}


sp_cache_t *
sp_del_cache_fd (xlator_t *this, fd_t *fd)
{
        sp_cache_t  *cache = NULL;
        uint64_t     value = 0;
        int32_t      ret   = -1;
        sp_fd_ctx_t *fd_ctx = NULL;

        if (fd == NULL) {
                goto out;
        }

        ret = fd_ctx_get (fd, this, &value);
        if (ret == -1) {
                goto out;
        }

        fd_ctx = (void *)(long) value;

        LOCK (&fd_ctx->lock);
        {
                cache = fd_ctx->cache;
                fd_ctx->cache = NULL;
        }
        UNLOCK (&fd_ctx->lock);

out:
        return cache;
}


sp_cache_t *
sp_get_cache_inode (xlator_t *this, inode_t *inode, int32_t pid)
{
        fd_t       *fd    = NULL;
        sp_cache_t *cache = NULL;

        if (inode == NULL) {
                goto out;
        }

        fd = fd_lookup (inode, pid);
        if (fd == NULL) {
                goto out;
        }

        cache = sp_get_cache_fd (this, fd);

        fd_unref (fd);
out:
        return cache;
}


inline int32_t
sp_put_cache (xlator_t *this, fd_t *fd, sp_cache_t *cache)
{
        sp_fd_ctx_t *fd_ctx = NULL;
        int32_t      ret    = -1; 
        uint64_t     value  = 0;

        ret = fd_ctx_get (fd, this, &value);
        if (!ret) {
                fd_ctx = (void *)(long)value;
        } else {
                fd_ctx = sp_fd_ctx_init ();
                if (fd_ctx == NULL) {
                        gf_log (this->name, GF_LOG_ERROR, "out of memory");
                        ret = -1;
                        goto out;
                }

                ret = fd_ctx_set (fd, this, (long)(void *)fd_ctx);
                if (ret == -1) {
                        sp_fd_ctx_free (fd_ctx); 
                        goto out;
                }
        }

        LOCK (&fd_ctx->lock);
        { 
                if (fd_ctx->cache) {
                        sp_cache_free (fd_ctx->cache);
                }

                fd_ctx->cache = cache;
        }
        UNLOCK (&fd_ctx->lock);

out:
        return ret;
}


int32_t
sp_cache_add_entries (sp_cache_t *cache, gf_dirent_t *entries)
{
        gf_dirent_t *entry           = NULL, *new = NULL;
        int32_t      ret             = -1;
        uint64_t     expected_offset = 0;
        
        LOCK (&cache->lock);
        {
                list_for_each_entry (entry, &entries->list, list) {
                        new = gf_dirent_for_name (entry->d_name);
                        if (new == NULL) {
                                goto unlock;
                        }

                        new->d_ino  = entry->d_ino;
                        new->d_off  = entry->d_off;
                        new->d_len  = entry->d_len;
                        new->d_type = entry->d_type;
                        new->d_stat = entry->d_stat;

                        ret = rbthash_insert (cache->table, new, new->d_name,
                                              strlen (new->d_name));
                        if (ret == -1) {
                                FREE (new);
                                continue;
                        }

                        expected_offset = new->d_off;
                }

                cache->expected_offset = expected_offset;

                ret = 0;
        }
unlock:
        UNLOCK (&cache->lock);

        return ret;
}


int32_t
sp_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, inode_t *inode,
               struct stat *buf, dict_t *dict, struct stat *postparent)
{
        struct stat *stbuf = NULL;
        int32_t      ret   = -1;
        uint64_t     value = 0;

        if (op_ret == -1) {
                goto out;
        }

        if (S_ISDIR (buf->st_mode)) {
                LOCK (&inode->lock);
                {
                        ret = __inode_ctx_get (inode, this, &value);
                        if (ret == 0) {
                                stbuf = (struct stat *)(long)value;
                        }

                        if (stbuf == NULL) {
                                stbuf = CALLOC (1, sizeof (*stbuf));
                                if (stbuf == NULL) {
                                        op_ret = -1;
                                        op_errno = ENOMEM;
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "out of memory");
                                        goto unlock;
                                }

                                ret = __inode_ctx_put (inode, this,
                                                       (long)stbuf);
                                if (ret == -1) {
                                        op_ret = -1;
                                
                                        /* FIXME: EINVAL is not correct */ 
                                        op_errno = EINVAL;
                                        FREE (stbuf);
                                        goto unlock;
                                }
                        }
                        
                        memcpy (stbuf, buf, sizeof (*stbuf));
                }
        unlock:
                UNLOCK (&inode->lock);
        }

out:
	SP_STACK_UNWIND (frame, op_ret, op_errno, inode, buf, dict);
        return 0;
}


int32_t
sp_lookup_behind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, inode_t *inode,
                      struct stat *buf, dict_t *dict, struct stat *postparent)
{
        sp_local_t *local = NULL;
        sp_cache_t *cache = NULL;

        local = frame->local;
        if (local == NULL) {
                goto out;
        }

        if ((op_ret == -1) && (op_errno = ENOENT)) {
                cache = sp_get_cache_inode (this, local->loc.parent,
                                            frame->root->pid);

                if (cache) {
                        sp_cache_remove_entry (cache, (char *)local->loc.name,
                                               0);
                }
        } 

out:
        SP_STACK_DESTROY (frame);
        return 0;
}


int32_t
sp_get_ancestors (char *path, char **parent, char **grand_parent)
{
        int32_t  ret = -1, i = 0;
        char    *cpy = NULL;

        if (!path || !parent || !grand_parent) {
                ret = 0;
                goto out;
        }

        for (i = 0; i < 2; i++) {
                if (!strcmp (path, "/")) {
                        break;
                }

                cpy = strdup (path);
                if (cpy == NULL) {
                        goto out;
                }

                path = dirname (cpy);
                switch (i)
                {
                case 0:
                        *parent = path;
                        break;
                case 1:
                        *grand_parent = path;
                        break;
                }
        }

        ret = 0;
out:
        return ret; 
}


int32_t
sp_cache_remove_parent_entry (call_frame_t *frame, xlator_t *this, char *path)
{
        char       *parent    = NULL, *grand_parent = NULL, *cpy = NULL;
        inode_t    *inode_gp  = NULL;
        sp_cache_t *cache_gp  = NULL;
        int32_t     ret       = -1;

        ret = sp_get_ancestors (path, &parent, &grand_parent);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto out;
        }

        if (grand_parent && strcmp (grand_parent, "/")) {
                inode_gp = inode_from_path (frame->root->frames.this->itable,
                                            grand_parent);
                if (inode_gp) {
                        cache_gp = sp_get_cache_inode (this, inode_gp,
                                                       frame->root->pid);
                        if (cache_gp) {
                                cpy = strdup (parent);
                                GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name,
                                                                cpy, out,
                                                                ENOMEM);
                                path = basename (cpy);
                                sp_cache_remove_entry (cache_gp, path, 0);
                                FREE (cpy);
                        }
                        inode_unref (inode_gp);
                }
        }

        ret = 0;
out:
        if (parent) {
                FREE (parent);
        }

        if (grand_parent) {
                FREE (grand_parent);
        }

        return ret;
}


int32_t
sp_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xattr_req)
{
        sp_local_t   *local      = NULL;
        gf_dirent_t   dirent;     
        int32_t       ret        = -1, op_ret = -1, op_errno = EINVAL; 
        sp_cache_t   *cache      = NULL;
        struct stat  *postparent = NULL, *buf = NULL;
        uint64_t      value      = 0; 
        call_frame_t *wind_frame = NULL;
        char          lookup_behind = 0;

        if (loc == NULL) {
                goto unwind;
        }

        if (xattr_req || (loc->parent == NULL) || (loc->name == NULL)) {
                goto wind;
        }

        memset (&dirent, 0, sizeof (dirent));
        cache = sp_get_cache_inode (this, loc->parent, frame->root->pid);
        if (cache) {
                ret = sp_cache_get_entry (cache, (char *)loc->name, &dirent);
                if (ret == 0) {
                        ret = inode_ctx_get (loc->parent, this, &value);
                        if (ret == 0) {
                                postparent = (void *)(long)value;
                                buf = &dirent.d_stat;
                                op_ret = 0;
                                op_errno = 0;
                                lookup_behind = 1;
                        } 
                } 
        } 

wind:        
        if (lookup_behind) {
                wind_frame = copy_frame (frame);
                if (wind_frame == NULL) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        gf_log (this->name, GF_LOG_ERROR, "out of memory");
                        goto unwind;
                } 

                local = CALLOC (1, sizeof (*local));
                if (local == NULL) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        STACK_DESTROY (wind_frame->root);
                        goto unwind;
                }
                
                loc_copy (&local->loc, loc);
                wind_frame->local = local;
                STACK_WIND (wind_frame, sp_lookup_behind_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, xattr_req);
        } else {
                STACK_WIND (frame, sp_lookup_cbk, FIRST_CHILD(this),
                            FIRST_CHILD(this)->fops->lookup, loc, xattr_req);
     
                return 0;
        }

unwind:
	SP_STACK_UNWIND (frame, op_ret, op_errno, loc->inode, buf, postparent,
                         NULL);
        return 0;

}


int32_t
sp_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, gf_dirent_t *entries)
{
        sp_local_t *local = NULL;
        sp_cache_t *cache = NULL;  
        fd_t       *fd    = NULL;
        int32_t     ret   = 0;

        local = frame->local;
        if (local == NULL) {
                goto out;
        }

        fd = local->fd;

        cache = sp_get_cache_fd (this, fd);
        if (cache == NULL) {
                cache = sp_cache_init ();
                if (cache == NULL) {
                        goto out;
                }

                ret = sp_put_cache (this, fd, cache);
                if (ret == -1) {
                        sp_cache_free (cache);
                        goto out;
                }
        }

        sp_cache_add_entries (cache, entries);

out:
	SP_STACK_UNWIND (frame, op_ret, op_errno, entries);
	return 0;
}


int32_t
sp_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
            off_t off)
{
        sp_cache_t *cache    = NULL;
        sp_local_t *local    = NULL;
        char       *path     = NULL;
        int32_t     ret      = -1;

        cache = sp_get_cache_fd (this, fd);
        if (cache) {
                if (off != cache->expected_offset) {
                        cache = sp_del_cache_fd (this, fd);
                        if (cache) {
                                sp_cache_free (cache);
                        }
                }
        }

        ret = inode_path (fd->inode, NULL, &path);
        if (ret == -1) {
                goto unwind;
        }
  
        ret = sp_cache_remove_parent_entry (frame, this, path);

        FREE (path);

        if (ret < 0) {
                errno = -ret;
                goto unwind;
        }

        local = CALLOC (1, sizeof (*local));
        if (local) {
                local->fd = fd;
                frame->local = local;
        }

	STACK_WIND (frame, sp_readdir_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readdirp, fd, size, off);

        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno, NULL);
        return 0;
}


int32_t
sp_truncate_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                 struct stat *postbuf)
{
	SP_STACK_UNWIND (frame, op_ret, op_errno, prebuf, postbuf);
	return 0;
}



int32_t
sp_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct stat *buf,
               struct stat *preoldparent, struct stat *postoldparent,
               struct stat *prenewparent, struct stat *postnewparent)
{
	SP_STACK_UNWIND (frame, op_ret, op_errno, buf, preoldparent,
                         postoldparent, prenewparent, postnewparent);
	return 0;
}


int32_t
sp_fd_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
           int32_t op_errno, fd_t *fd)
{
        sp_local_t  *local = NULL;
        sp_fd_ctx_t *fd_ctx = NULL;

        if (op_ret == -1) {
                goto out;
        }

        local = frame->local;
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, local, out, EINVAL);

        fd_ctx = sp_fd_ctx_new (this, local->loc.parent,
                                (char *)local->loc.name, NULL);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, fd_ctx, out, ENOMEM);

        op_ret = fd_ctx_set (fd, this, (long)(void *)fd_ctx);
        if (op_ret == -1) {
                sp_fd_ctx_free (fd_ctx);
                op_errno = ENOMEM;
        }

out:
        SP_STACK_UNWIND (frame, op_ret, op_errno, fd);
        return 0;
}


int32_t
sp_open (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flags,
         fd_t *fd, int wbflags)
{
        sp_local_t *local = NULL;
        int32_t     ret   = -1;

        local = CALLOC (1, sizeof (*local));
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, local, unwind, ENOMEM);

        frame->local = local;

        ret = loc_copy (&local->loc, loc);
        if (ret == -1) {
                goto unwind;
        }

	STACK_WIND (frame, sp_fd_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->open, loc, flags, fd, wbflags);
        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno, fd);
        return 0;
}


static int32_t
sp_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode,
               struct stat *buf, struct stat *preparent,
               struct stat *postparent)
{
        sp_local_t  *local = NULL;
        sp_fd_ctx_t *fd_ctx = NULL;

        if (op_ret == -1) {
                goto out;
        }

        local = frame->local;
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, local, out, EINVAL);

        fd_ctx = sp_fd_ctx_new (this, local->loc.parent,
                                (char *)local->loc.name, NULL);
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, fd_ctx, out, ENOMEM);

        op_ret = fd_ctx_set (fd, this, (long)(void *)fd_ctx);
        if (op_ret == -1) {
                sp_fd_ctx_free (fd_ctx);
                op_errno = ENOMEM;
        }

out:
        SP_STACK_UNWIND (frame, op_ret, op_errno, fd, inode, buf);
        return 0;
}


int32_t
sp_create (call_frame_t *frame,	xlator_t *this,	loc_t *loc, int32_t flags,
           mode_t mode, fd_t *fd)
{
        sp_local_t *local     = NULL;
        int32_t     ret       = -1;

        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->parent, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->path, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, unwind);

        ret = sp_cache_remove_parent_entry (frame, this, (char *)loc->path);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto unwind;
        }

        local = CALLOC (1, sizeof (*local));
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, local, unwind, ENOMEM);

        frame->local = local;

        ret = loc_copy (&local->loc, loc);
        if (ret == -1) {
                goto unwind;
        }

	STACK_WIND (frame, sp_create_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->create, loc, flags, mode, fd);

        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno, fd);
        return 0;
}


int32_t
sp_opendir (call_frame_t *frame, xlator_t *this, loc_t *loc, fd_t *fd)
{
        sp_local_t *local = NULL;
        int32_t     ret   = -1;

        local = CALLOC (1, sizeof (*local));
        GF_VALIDATE_OR_GOTO_WITH_ERROR (this->name, local, unwind, ENOMEM);

        frame->local = local;

        ret = loc_copy (&local->loc, loc);
        if (ret == -1) {
                goto unwind;
        }

	STACK_WIND (frame, sp_fd_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->opendir, loc, fd);
        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno, fd);
        return 0;
}


int32_t
sp_new_entry_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, inode_t *inode,
                  struct stat *buf, struct stat *preparent,
                  struct stat *postparent)
{
	STACK_UNWIND (frame, op_ret, op_errno, inode, buf, preparent,
                      postparent);
	return 0;
}


int32_t
sp_mkdir (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode)
{
        int32_t     ret = 0;

        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->parent, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->path, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, unwind);

        ret = sp_cache_remove_parent_entry (frame, this, (char *)loc->path);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto unwind;
        }

	STACK_WIND (frame, sp_new_entry_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mkdir, loc, mode);

        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno, loc->inode, NULL);
        return 0;
}


int32_t
sp_mknod (call_frame_t *frame, xlator_t *this, loc_t *loc, mode_t mode,
          dev_t rdev)
{
        int32_t     ret = 0;

        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->parent, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->path, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, unwind);

        ret = sp_cache_remove_parent_entry (frame, this, (char *)loc->path);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto unwind;
        }

	STACK_WIND (frame, sp_new_entry_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->mknod, loc, mode, rdev);

        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno, loc->inode, NULL);
        return 0;
}


int32_t
sp_symlink (call_frame_t *frame, xlator_t *this, const char *linkpath,
            loc_t *loc)
{
        int32_t     ret = 0;

        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->parent, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->path, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, unwind);

        ret = sp_cache_remove_parent_entry (frame, this, (char *)loc->path);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto unwind;
        }

	STACK_WIND (frame, sp_new_entry_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->symlink, linkpath, loc);

        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno, loc->inode, NULL);
        return 0;
}


int32_t
sp_link (call_frame_t *frame, xlator_t *this, loc_t *oldloc, loc_t *newloc)
{
        int32_t     ret = 0;

        GF_VALIDATE_OR_GOTO (this->name, newloc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, newloc->parent, unwind);
        GF_VALIDATE_OR_GOTO (this->name, newloc->path, unwind);
        GF_VALIDATE_OR_GOTO (this->name, newloc->name, unwind);
        GF_VALIDATE_OR_GOTO (this->name, newloc->inode, unwind);

        ret = sp_cache_remove_parent_entry (frame, this, (char *)newloc->path);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto unwind;
        }

	STACK_WIND (frame, sp_new_entry_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->link, oldloc, newloc);

        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno, oldloc->inode, NULL);
        return 0;
}


int32_t
sp_truncate (call_frame_t *frame, xlator_t *this, loc_t *loc, off_t offset)
{
        sp_cache_t *cache = NULL;

        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->parent, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, unwind);

        cache = sp_get_cache_inode (this, loc->parent, frame->root->pid);
        if (cache) {
                sp_cache_remove_entry (cache, (char *)loc->name, 0);
        }

	STACK_WIND (frame, sp_truncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->truncate, loc, offset);
        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno, NULL);
        return 0;
}


int32_t
sp_ftruncate (call_frame_t *frame, xlator_t *this, fd_t *fd, off_t offset)
{
        sp_fd_ctx_t *fd_ctx = NULL;
        sp_cache_t  *cache  = NULL;
        uint64_t     value  = 0;
        int32_t      ret    = 0; 
        inode_t     *parent = NULL;
        char        *name   = NULL; 

        ret = fd_ctx_get (fd, this, &value);
        if (ret == -1) {
                errno = EINVAL;
                goto unwind;
        }

        fd_ctx = (void *)(long)value;
        name   = fd_ctx->name;
        parent = fd_ctx->parent_inode;

        cache = sp_get_cache_inode (this, parent, frame->root->pid);
        if (cache) {
                sp_cache_remove_entry (cache, name, 0);
        }

	STACK_WIND (frame, sp_truncate_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->ftruncate, fd, offset);
        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno, NULL);
        return 0;
}


int32_t
sp_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno,
                struct stat *prestat, struct stat *poststat)
{
	SP_STACK_UNWIND (frame, op_ret, op_errno, poststat);
	return 0;
}


int
sp_setattr (call_frame_t *frame, xlator_t *this,
            loc_t *loc, struct stat *buf, int32_t valid)
{
        sp_cache_t *cache = NULL;

        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->parent, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, unwind);

        cache = sp_get_cache_inode (this, loc->parent, frame->root->pid);
        if (cache) {
                sp_cache_remove_entry (cache, (char *)loc->name, 0);
        }

	STACK_WIND (frame, sp_setattr_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setattr, loc, buf, valid);
        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno, NULL);
        return 0;
}


int32_t
sp_readlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, const char *path,
                 struct stat *buf)
{
	SP_STACK_UNWIND (frame, op_ret, op_errno, path, buf);
        return 0;
}


int32_t
sp_readlink (call_frame_t *frame, xlator_t *this, loc_t *loc, size_t size)
{
        sp_cache_t *cache = NULL;

        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->parent, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, unwind);

        cache = sp_get_cache_inode (this, loc->parent, frame->root->pid);
        if (cache) {
                sp_cache_remove_entry (cache, (char *)loc->name, 0);
        }

	STACK_WIND (frame, sp_readlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readlink, loc, size);
        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno, NULL);
        return 0;
}

int32_t
sp_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct stat *preparent,
               struct stat *postparent)
{
	SP_STACK_UNWIND (frame, op_ret, op_errno, preparent, postparent);
	return 0;
}



int32_t
sp_err_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
            int32_t op_ret, int32_t op_errno)
{
	SP_STACK_UNWIND (frame, op_ret, op_errno);
	return 0;
}


int32_t
sp_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        sp_cache_t *cache = NULL;
        int32_t     ret   = 0;

        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->parent, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, unwind);

        cache = sp_get_cache_inode (this, loc->parent, frame->root->pid);
        if (cache) {
                sp_cache_remove_entry (cache, (char *)loc->name, 0);
        }

        ret = sp_cache_remove_parent_entry (frame, this, (char *)loc->path);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto unwind;
        }

	STACK_WIND (frame, sp_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->unlink, loc);
        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno);
        return 0;
}


void
sp_remove_caches_from_all_fds_opened (xlator_t *this, inode_t *inode)
{
        fd_t       *fd    = NULL;
        sp_cache_t *cache = NULL;

        LOCK (&inode->lock);
        {
                list_for_each_entry (fd, &inode->fd_list, inode_list) {
                        cache = sp_get_cache_fd (this, fd);
                        if (cache) {
                                sp_cache_remove_entry (cache, NULL, 1);
                        }
                }
        }
        UNLOCK (&inode->lock);
}
 

int32_t
sp_rmdir (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        sp_cache_t *cache = NULL;
        int32_t     ret   = -1;

        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->path, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->inode, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->parent, unwind);

        sp_remove_caches_from_all_fds_opened (this, loc->inode);

        cache = sp_get_cache_inode (this, loc->parent, frame->root->pid);
        if (cache) {
                sp_cache_remove_entry (cache, (char *)loc->name, 0);
        }

        ret = sp_cache_remove_parent_entry (frame, this, (char *)loc->path);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto unwind;
        }

        STACK_WIND (frame, sp_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rmdir, loc);
        return 0;

unwind:
        STACK_UNWIND (frame, -1, errno);
        return 0;
}


int32_t
sp_readv_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int32_t op_ret,
              int32_t op_errno, struct iovec *vector, int32_t count,
              struct stat *stbuf, struct iobref *iobref)
{
	SP_STACK_UNWIND (frame, op_ret, op_errno, vector, count, stbuf, iobref);
	return 0;
}


int32_t
sp_readv (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
          off_t offset)
{
        sp_fd_ctx_t *fd_ctx = NULL;
        sp_cache_t  *cache  = NULL;
        uint64_t     value  = 0;
        int32_t      ret    = 0; 
        inode_t     *parent = NULL;
        char        *name   = NULL; 

        ret = fd_ctx_get (fd, this, &value);
        if (ret == -1) {
                errno = EINVAL;
                goto unwind;
        }

        fd_ctx = (void *)(long)value;
        name   = fd_ctx->name;
        parent = fd_ctx->parent_inode;

        cache = sp_get_cache_inode (this, parent, frame->root->pid);
        if (cache) {
                sp_cache_remove_entry (cache, name, 0);
        }

	STACK_WIND (frame, sp_readv_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->readv, fd, size, offset);
        return 0;

unwind:
	SP_STACK_UNWIND (frame, -1, errno, NULL, -1, NULL, NULL);
        return 0;
}


int32_t
sp_writev (call_frame_t *frame, xlator_t *this, fd_t *fd, struct iovec *vector,
           int32_t count, off_t off, struct iobref *iobref)
{
        sp_fd_ctx_t *fd_ctx = NULL;
        sp_cache_t  *cache  = NULL;
        uint64_t     value  = 0;
        int32_t      ret    = 0; 
        inode_t     *parent = NULL;
        char        *name   = NULL; 

        ret = fd_ctx_get (fd, this, &value);
        if (ret == -1) {
                errno = EINVAL;
                goto unwind;
        }

        fd_ctx = (void *)(long)value;
        name   = fd_ctx->name;
        parent = fd_ctx->parent_inode;

        cache = sp_get_cache_inode (this, parent, frame->root->pid);
        if (cache) {
                sp_cache_remove_entry (cache, name, 0);
        }

	STACK_WIND (frame, sp_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->writev, fd, vector, count, off,
                    iobref);
        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno, NULL);
        return 0;
}


int32_t
sp_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags)
{
        sp_fd_ctx_t *fd_ctx = NULL;
        sp_cache_t  *cache  = NULL;
        uint64_t     value  = 0;
        int32_t      ret    = 0; 
        inode_t     *parent = NULL;
        char        *name   = NULL; 

        ret = fd_ctx_get (fd, this, &value);
        if (ret == -1) {
                errno = EINVAL;
                goto unwind;
        }

        fd_ctx = (void *)(long)value;
        name   = fd_ctx->name;
        parent = fd_ctx->parent_inode;

        cache = sp_get_cache_inode (this, parent, frame->root->pid);
        if (cache) {
                sp_cache_remove_entry (cache, name, 0);
        }

	STACK_WIND (frame, sp_unlink_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fsync, fd, flags);
        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno);
        return 0;
}


int32_t
sp_rename (call_frame_t *frame, xlator_t *this, loc_t *oldloc,loc_t *newloc)
{
        sp_cache_t *cache = NULL;
        int32_t     ret   = -1;

        GF_VALIDATE_OR_GOTO (this->name, oldloc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, oldloc->path, unwind);
        GF_VALIDATE_OR_GOTO (this->name, oldloc->name, unwind);
        GF_VALIDATE_OR_GOTO (this->name, oldloc->parent, unwind);
        GF_VALIDATE_OR_GOTO (this->name, oldloc->inode, unwind);

        GF_VALIDATE_OR_GOTO (this->name, newloc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, newloc->path, unwind);

        cache = sp_get_cache_inode (this, oldloc->parent, frame->root->pid);
        if (cache) {
                sp_cache_remove_entry (cache, (char *)oldloc->name, 0);
        }

        cache = sp_get_cache_inode (this, newloc->parent, frame->root->pid);
        if (cache) {
                sp_cache_remove_entry (cache, (char *)newloc->name, 0);
        }

        ret = sp_cache_remove_parent_entry (frame, this, (char *)oldloc->path);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto unwind;
        }

        ret = sp_cache_remove_parent_entry (frame, this, (char *)newloc->path);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto unwind;
        }

        if (S_ISDIR (oldloc->inode->st_mode)) {
                sp_remove_caches_from_all_fds_opened (this, oldloc->inode);
        }

        STACK_WIND (frame, sp_rename_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->rename, oldloc, newloc);
        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno, NULL);
	return 0;
}


int32_t
sp_setxattr (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *dict,
             int32_t flags)
{
        sp_cache_t *cache = NULL;

        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->parent, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, unwind);

        cache = sp_get_cache_inode (this, loc->parent, frame->root->pid);
        if (cache) {
                sp_cache_remove_entry (cache, (char *)loc->name, 0);
        }

	STACK_WIND (frame, sp_err_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setxattr, loc, dict, flags);
        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno);
        return 0;
}


int32_t
sp_removexattr (call_frame_t *frame, xlator_t *this, loc_t *loc,
                const char *name)
{
        sp_cache_t *cache = NULL;

        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->parent, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, unwind);

        cache = sp_get_cache_inode (this, loc->parent, frame->root->pid);
        if (cache) {
                sp_cache_remove_entry (cache, (char *)loc->name, 0);
        }

	STACK_WIND (frame, sp_err_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->removexattr, loc, name);
        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno);
        return 0;
}


int32_t
sp_setdents (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t flags,
             dir_entry_t *entries, int32_t count)
{
        sp_fd_ctx_t *fd_ctx = NULL;
        sp_cache_t  *cache  = NULL;
        uint64_t     value  = 0;
        int32_t      ret    = 0; 
        inode_t     *parent = NULL;
        char        *name   = NULL; 
        dir_entry_t *trav   = NULL;

        ret = fd_ctx_get (fd, this, &value);
        if (ret == -1) {
                errno = EINVAL;
                goto unwind;
        }

        fd_ctx = (void *)(long)value;
        name   = fd_ctx->name;
        parent = fd_ctx->parent_inode;

        cache = sp_get_cache_inode (this, parent, frame->root->pid);
        if (cache) {
                sp_cache_remove_entry (cache, name, 0);
        }

        cache = sp_get_cache_fd (this, fd);
        if (cache) {
                for (trav = entries->next; trav; trav = trav->next) {
                        sp_cache_remove_entry (cache, trav->name, 0);
                }
        }

	STACK_WIND (frame, sp_err_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->setdents, fd, flags, entries,
                    count);
	return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno);
        return 0;
}


int32_t
sp_getdents_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dir_entry_t *entries,
                 int32_t count)
{
        dir_entry_t *trav  = NULL;
        sp_local_t  *local = NULL;
        sp_cache_t  *cache = NULL;
        
        if (op_ret == -1) {
                goto out;
        }

        local = frame->local;
        if ((local == NULL) || (local->fd == NULL)) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        cache = sp_get_cache_fd (this, local->fd);
        if (cache) {
                for (trav = entries->next; trav; trav = trav->next) {
                        if (S_ISLNK (trav->buf.st_mode)) {
                                sp_cache_remove_entry (cache, trav->name, 0);
                        }
                }
        }
        
out: 
	SP_STACK_UNWIND (frame, op_ret, op_errno, entries, count);
	return 0;
}


int32_t
sp_getdents (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset, int32_t flags)
{
        sp_fd_ctx_t *fd_ctx = NULL;
        sp_cache_t  *cache  = NULL;
        uint64_t     value  = 0;
        int32_t      ret    = 0; 
        inode_t     *parent = NULL;
        char        *name   = NULL; 
        sp_local_t  *local  = NULL;

        ret = fd_ctx_get (fd, this, &value);
        if (ret == -1) {
                errno = EINVAL;
                goto unwind;
        }

        fd_ctx = (void *)(long)value;
        name   = fd_ctx->name;
        parent = fd_ctx->parent_inode;

        cache = sp_get_cache_inode (this, parent, frame->root->pid);
        if (cache) {
                sp_cache_remove_entry (cache, name, 0);
        }

        local = CALLOC (1, sizeof (*local));
        if (local == NULL) {
                gf_log (this->name, GF_LOG_ERROR, "out of memory");
                goto unwind;
        }

        local->fd = fd;
        frame->local = local;

	STACK_WIND (frame, sp_getdents_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->getdents, fd, size, offset, flags);
	return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno, NULL, -1);
        return 0;
}


int32_t
sp_checksum_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, uint8_t *file_checksum,
                 uint8_t *dir_checksum)
{
	SP_STACK_UNWIND (frame, op_ret, op_errno, file_checksum, dir_checksum);
	return 0;
}


int32_t
sp_checksum (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t flag)
{
        sp_cache_t *cache = NULL;

        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->parent, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, unwind);

        cache = sp_get_cache_inode (this, loc->parent, frame->root->pid);
        if (cache) {
                sp_cache_remove_entry (cache, (char *)loc->name, 0);
        }

	STACK_WIND (frame, sp_checksum_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->checksum, loc, flag);
        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno, NULL);
        return 0;
}


int32_t
sp_xattrop_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, dict_t *dict)
{
	SP_STACK_UNWIND (frame, op_ret, op_errno, dict);
	return 0;
}


int32_t
sp_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
            gf_xattrop_flags_t flags, dict_t *dict)
{
        sp_cache_t *cache = NULL;

        GF_VALIDATE_OR_GOTO (this->name, loc, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->parent, unwind);
        GF_VALIDATE_OR_GOTO (this->name, loc->name, unwind);

        cache = sp_get_cache_inode (this, loc->parent, frame->root->pid);
        if (cache) {
                sp_cache_remove_entry (cache, (char *)loc->name, 0);
        }

	STACK_WIND (frame, sp_xattrop_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->xattrop, loc, flags, dict);
        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno, NULL);
        return 0;
}


int32_t
sp_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
             gf_xattrop_flags_t flags, dict_t *dict)
{
        sp_fd_ctx_t *fd_ctx = NULL;
        sp_cache_t  *cache  = NULL;
        uint64_t     value  = 0;
        int32_t      ret    = 0; 
        inode_t     *parent = NULL;
        char        *name   = NULL; 

        ret = fd_ctx_get (fd, this, &value);
        if (ret == -1) {
                errno = EINVAL;
                goto unwind;
        }

        fd_ctx = (void *)(long)value;
        name   = fd_ctx->name;
        parent = fd_ctx->parent_inode;

        cache = sp_get_cache_inode (this, parent, frame->root->pid);
        if (cache) {
                sp_cache_remove_entry (cache, name, 0);
        }

	STACK_WIND (frame, sp_xattrop_cbk, FIRST_CHILD(this),
                    FIRST_CHILD(this)->fops->fxattrop, fd, flags, dict);
        return 0;

unwind:
        SP_STACK_UNWIND (frame, -1, errno, NULL);
        return 0;
}


int32_t
sp_forget (xlator_t *this, inode_t *inode)
{
        struct stat *buf   = NULL;
        uint64_t     value = 0;

        inode_ctx_del (inode, this, &value);
        
        if (value) {
                buf = (void *)(long)value;
                FREE (buf);
        }
        
        return 0;
}


int32_t
sp_release (xlator_t *this, fd_t *fd)
{
        sp_fd_ctx_t *fd_ctx = NULL;
        uint64_t     value  = 0;
        int32_t      ret    = 0;

        ret = fd_ctx_del (fd, this, &value);
        if (!ret) {
                fd_ctx = (void *)(long) value;
                sp_fd_ctx_free (fd_ctx);      
        }

        return 0;
}



int32_t 
init (xlator_t *this)
{
        int32_t ret = -1;
        if (!this->children || this->children->next) {
                gf_log ("stat-prefetch",
                        GF_LOG_ERROR,
                        "FATAL: translator %s does not have exactly one child "
                        "node", this->name);
                goto out;
        }

        ret = 0;
out:
        return ret;
}

void
fini (xlator_t *this)
{
        return;
}


struct xlator_fops fops = {
        .lookup      = sp_lookup,
        .readdir     = sp_readdir,
        .readdirp    = sp_readdir,
        .open        = sp_open, 
        .create      = sp_create,
        .opendir     = sp_opendir,
        .mkdir       = sp_mkdir,
        .mknod       = sp_mknod,
        .symlink     = sp_symlink,
        .link        = sp_link,
        .truncate    = sp_truncate,
        .ftruncate   = sp_ftruncate,
        .readlink    = sp_readlink,
        .unlink      = sp_unlink,
        .rmdir       = sp_rmdir,
        .readv       = sp_readv,
        .writev      = sp_writev, 
        .fsync       = sp_fsync,
        .rename      = sp_rename,
        .setxattr    = sp_setxattr,
        .removexattr = sp_removexattr,
        .setdents    = sp_setdents,
        .getdents    = sp_getdents,
        .checksum    = sp_checksum,
        .xattrop     = sp_xattrop,
        .fxattrop    = sp_fxattrop,
        .setattr     = sp_setattr,
};

struct xlator_mops mops = {
};

struct xlator_cbks cbks = {
        .forget     = sp_forget,
        .release    = sp_release,
        .releasedir = sp_release
};
