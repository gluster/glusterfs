/*
  Copyright (c) 2008, 2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include "libglusterfsclient.h"
#include "libglusterfsclient-internals.h"
#include <libgen.h>

#define LIBGLUSTERFS_CLIENT_DENTRY_LOC_PREPARE(_new_loc, _loc, _parent, \
                                               _resolved) do {          \
                size_t pathlen = 0;                                     \
                size_t resolvedlen = 0;                                 \
                char *path = NULL;                                      \
                int pad = 0;                                            \
                pathlen   = strlen (_loc->path) + 1;                    \
                path = CALLOC (1, pathlen);                             \
                _new_loc.parent =  _parent;                             \
                resolvedlen = strlen (_resolved);                       \
                strncpy (path, _resolved, resolvedlen);                 \
                if (resolvedlen == 1) /* only root resolved */          \
                        pad = 0;                                        \
                else {                                                  \
                        pad = 1;                                        \
                        path[resolvedlen] = '/';                        \
                }                                                       \
                strcpy_till (path + resolvedlen + pad,                  \
                             loc->path + resolvedlen + pad, '/');       \
                _new_loc.path = path;                                   \
                _new_loc.name = strrchr (path, '/');                    \
                if (_new_loc.name)                                      \
                        _new_loc.name++;                                \
        }while (0);


/* strcpy_till - copy @dname to @dest, until 'delim' is encountered in @dest
 * @dest - destination string
 * @dname - source string
 * @delim - delimiter character
 *
 * return - NULL is returned if '0' is encountered in @dname, otherwise returns
 *          a pointer to remaining string begining in @dest.
 */
static char *
strcpy_till (char *dest, const char *dname, char delim)
{
        char *src = NULL;
        int idx = 0;
        char *ret = NULL;

        src = (char *)dname;
        while (src[idx] && (src[idx] != delim)) {
                dest[idx] = src[idx];
                idx++;
        }

        dest[idx] = 0;

        if (src[idx] == 0)
                ret = NULL;
        else
                ret = &(src[idx]);

        return ret;
}

/* __libgf_client_path_to_parenti - derive parent inode for @path. if immediate 
 *                            parent is not available in the dentry cache, return nearest
 *                            available parent inode and set @reslv to the path of
 *                            the returned directory.
 *
 * @itable - inode table
 * @path   - path whose parent has to be looked up.
 * @reslv  - if immediate parent is not available, reslv will be set to path of the
 *           resolved parent.
 *
 * return - should never return NULL. should at least return '/' inode.
 */
static inode_t *
__libgf_client_path_to_parenti (inode_table_t *itable,
                                const char *path,
                                time_t lookup_timeout,
                                char **reslv)
{
        char *resolved_till = NULL;
        char *strtokptr = NULL;
        char *component = NULL;
        char *next_component = NULL;
        char *pathdup = NULL;
        inode_t *curr = NULL;
        inode_t *parent = NULL;
        size_t pathlen = 0;
        time_t current, prev;
        libglusterfs_client_inode_ctx_t *inode_ctx = NULL;
        uint64_t ptr = 0;
        int32_t op_ret = 0;

        pathlen = STRLEN_0 (path);
        resolved_till = CALLOC (1, pathlen);

        GF_VALIDATE_OR_GOTO("libglusterfsclient-dentry", resolved_till, out);
        pathdup = strdup (path);
        GF_VALIDATE_OR_GOTO("libglusterfsclient-dentry", pathdup, out);

        parent = inode_ref (itable->root);
        curr = NULL;

        component = strtok_r (pathdup, "/", &strtokptr);

        while (component) {
                curr = inode_search (itable, parent->ino, component);
                if (!curr) {
                        break;
                }

                op_ret = inode_ctx_get (curr, itable->xl, &ptr);
                if (op_ret == -1) {
                        errno = EINVAL;
                        break;
                }

                inode_ctx = (libglusterfs_client_inode_ctx_t *)(long)ptr;
                memset (&current, 0, sizeof (current));
                current = time (NULL);

                pthread_mutex_lock (&inode_ctx->lock);
                {
                        prev = inode_ctx->previous_lookup_time;
                }
                pthread_mutex_unlock (&inode_ctx->lock);
    
                if ((prev < 0) 
                    || (lookup_timeout < (current - prev))) {
                        break;
                }

                /* It is OK to append the component even if it is the       
                   last component in the path, because, if 'next_component'
                   returns NULL, @parent will remain the same and
                   @resolved_till will not be sent back               
                */
                strcat (resolved_till, "/");
                strcat (resolved_till, component);

                next_component = strtok_r (NULL, "/", &strtokptr);

                if (next_component) {
                        inode_unref (parent);
                        parent = curr;
                        curr = NULL;
                } else {
                        /* will break */
                        inode_unref (curr);
                }

                component = next_component;
        }

        if (resolved_till[0] == '\0') {
                strcat (resolved_till, "/");
        }

        free (pathdup);
        
        if (reslv) {
                *reslv = resolved_till;
        } else {
                FREE (resolved_till);
        }

out:
        return parent;
}

static inline void
libgf_client_update_resolved (const char *path, char *resolved)
{
        int32_t pathlen = 0;
        char *tmp = NULL, *dest = NULL, *dname = NULL;
        char append_slash = 0;

        pathlen = strlen (resolved); 
        tmp = (char *)(resolved + pathlen);
        if (*((char *) (resolved + pathlen - 1)) != '/') {
                tmp[0] = '/';
                append_slash = 1;
        }

        if (append_slash) {
                dest = tmp + 1;
        } else {
                dest = tmp;
        }

        if (*((char *) path + pathlen) == '/') {
                dname = (char *) path + pathlen + 1;
        } else {
                dname = (char *) path + pathlen;
        }

        strcpy_till (dest, dname, '/');
}

/* __do_path_resolve - resolve @loc->path into @loc->inode and @loc->parent. also
 *                     update the dentry cache
 *
 * @loc   - loc to resolve. 
 * @ctx   - libglusterfsclient context
 *
 * return - 0 on success
 *         -1 on failure 
 *          
 */
static int32_t
__do_path_resolve (loc_t *loc, libglusterfs_client_ctx_t *ctx)
{
        int32_t         op_ret = -1;
        char           *resolved  = NULL;
        inode_t        *parent = NULL, *inode = NULL;
        dentry_t       *dentry = NULL;
        loc_t          new_loc = {0, };
	char           *pathname = NULL, *directory = NULL;
	char           *file = NULL;   
        time_t current, prev;
        libglusterfs_client_inode_ctx_t *inode_ctx = NULL;
        uint64_t ptr = 0;
        
        parent = loc->parent;
        if (parent) {
                inode_ref (parent);
                gf_log ("libglusterfsclient-dentry", GF_LOG_DEBUG,
                        "loc->parent(%"PRId64") already present. sending lookup "
                        "for %"PRId64"/%s", parent->ino, parent->ino,
                        loc->name);
                resolved = strdup (loc->path);
                resolved = dirname (resolved);
        } else {
                parent = __libgf_client_path_to_parenti (ctx->itable, loc->path,
                                                         ctx->lookup_timeout,
                                                         &resolved);
        }

        if (parent == NULL) {
                /* fire in the bush.. run! run!! run!!! */
                gf_log ("libglusterfsclient-dentry",
                        GF_LOG_CRITICAL,
                        "failed to get parent inode number");
                op_ret = -1;
                goto out;
        }               

        gf_log ("libglusterfsclient-dentry",
                GF_LOG_DEBUG,
                "resolved path(%s) till %"PRId64"(%s). "
                "sending lookup for remaining path",
                loc->path, parent->ino, resolved);

	pathname = strdup (loc->path);
	directory = dirname (pathname);
        pathname = NULL;

        while (strcmp (resolved, directory) != 0) 
        {
                dentry = NULL;

                LIBGLUSTERFS_CLIENT_DENTRY_LOC_PREPARE (new_loc, loc, parent,
                                                        resolved);

		if (pathname) {
			free (pathname);
			pathname = NULL;
		}

		pathname = strdup (resolved);
		file = basename (pathname);

                new_loc.inode = inode_search (ctx->itable, parent->ino, file);
                if (new_loc.inode) {
                        op_ret = inode_ctx_get (new_loc.inode, ctx->itable->xl,
                                                &ptr);
                        if (op_ret != -1) {
                                inode_ctx = (libglusterfs_client_inode_ctx_t *)(long)ptr;
                                memset (&current, 0, sizeof (current));
                                current = time (NULL);
                        
                                pthread_mutex_lock (&inode_ctx->lock);
                                {
                                        prev = inode_ctx->previous_lookup_time;
                                }
                                pthread_mutex_unlock (&inode_ctx->lock);
                
                                if ((prev >= 0) 
                                    && (ctx->lookup_timeout 
                                        >= (current - prev))) {
                                        dentry = dentry_search_for_inode (new_loc.inode,
                                                                          parent->ino,
                                                                          file);
                                }
                        }
                }
                
                if (dentry == NULL) {
                        op_ret = libgf_client_lookup (ctx, &new_loc, NULL, NULL,
                                                      0);
                        if (op_ret == -1) {
                                inode_ref (new_loc.parent);
                                libgf_client_loc_wipe (&new_loc);
                                goto out;
                        }
                }

                parent = inode_ref (new_loc.inode);
                libgf_client_loc_wipe (&new_loc);

                libgf_client_update_resolved (loc->path, resolved);
        }

	if (pathname) {
		free (pathname);
		pathname = NULL;
	} 

        pathname = strdup (loc->path);
        file = basename (pathname);

        inode = inode_search (ctx->itable, parent->ino, file);
        if (!inode) {
                libgf_client_loc_fill (&new_loc, ctx, 0, parent->ino,
                                       file);

                op_ret = libgf_client_lookup (ctx, &new_loc, NULL, NULL,
                                              0);
                if (op_ret == -1) {
                        /* parent is resolved, file referred by the 
                           path may not be present on the storage*/
                        if (strcmp (loc->path, "/") != 0) {
                                op_ret = 0;
                        }

                        libgf_client_loc_wipe (&new_loc);
                        goto out;
                }
                
                inode = inode_ref (new_loc.inode);
                libgf_client_loc_wipe (&new_loc);
        }

out:
        loc->inode = inode;
        loc->parent = parent;

        FREE (resolved);
	if (pathname) {
		FREE (pathname);
	}

	if (directory) {
		FREE (directory);
	}

        return op_ret;
}


/* resolves loc->path to loc->parent and loc->inode */
int32_t
libgf_client_path_lookup (loc_t *loc,
                          libglusterfs_client_ctx_t *ctx,
                          char lookup_basename)
{
        char       *pathname  = NULL;
        char       *directory = NULL;
        inode_t    *inode = NULL;
        inode_t    *parent = NULL;
        int32_t     op_ret = 0;
	loc_t       new_loc = {0, };

	/* workaround for xlators like dht which require lookup to be sent 
	   on / */

	libgf_client_loc_fill (&new_loc, ctx, 1, 0, "/");

	op_ret = libgf_client_lookup (ctx, &new_loc, NULL, NULL, NULL);
	if (op_ret == -1) {
		gf_log ("libglusterfsclient-dentry",
			GF_LOG_ERROR,
			"lookup of / failed");
		goto out;
	}
	libgf_client_loc_wipe (&new_loc);

        pathname  = strdup (loc->path);
        directory = dirname (pathname);
        parent = inode_from_path (ctx->itable, directory);

        if (parent != NULL) {
                loc->parent = parent;

                if (!lookup_basename) {
                        gf_log ("libglusterfsclient",
                                GF_LOG_DEBUG,
                                "resolved dirname(%s) to %"PRId64,
                                loc->path, parent->ino);
                        goto out;
                } else {
                        inode = inode_from_path (ctx->itable, loc->path);
                        if (inode != NULL) {
                                gf_log ("libglusterfsclient",
                                        GF_LOG_DEBUG,
                                        "resolved path(%s) to %"PRId64"/%"PRId64,
                                        loc->path, parent->ino, inode->ino);
                                loc->inode = inode;
                                goto out;
                        }
                }
        } else {
                gf_log ("libglusterfsclient",
                        GF_LOG_DEBUG,
                        "resolved path(%s) to %p(%"PRId64")/%p(%"PRId64")",
                        loc->path, parent, (parent ? parent->ino : 0), 
                        inode, (inode ? inode->ino : 0));
                if (parent) {
                        inode_unref (parent);
                } else if (inode) {
                        inode_unref (inode);
                        gf_log ("libglusterfsclient",
                                GF_LOG_ERROR,
                                "undesired behaviour. inode(%"PRId64") for %s "
                                "exists without parent (%s)", 
                                inode->ino, loc->path, directory);
                }
                op_ret = __do_path_resolve (loc, ctx);
        }

out:    
        if (pathname)
                free (pathname);

        return op_ret;
}
