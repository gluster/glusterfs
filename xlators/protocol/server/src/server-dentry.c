#include "glusterfs.h"
#include "xlator.h"
#include "server-protocol.h"
#include "server-helpers.h"
#include <libgen.h>

/* SERVER_DENTRY_STATE_PREPARE - prepare a fresh state for use
 *
 * @state    - an empty state
 * @loc      - loc_t which needs to resolved
 * @parent   - most immediate parent of @loc available in dentry cache
 * @resolved - component of @loc->path which has been resolved
 *             through dentry cache
 */
#define SERVER_DENTRY_STATE_PREPARE(_state,_loc,_parent,_resolved) do {	\
		size_t pathlen = 0;					\
		size_t resolvedlen = 0;					\
		char *path = NULL;					\
		int pad = 0;						\
		pathlen   = strlen (_loc->path) + 1;			\
		path = CALLOC (1, pathlen);				\
		_state->loc.parent = inode_ref (_parent);		\
		_state->loc.inode  = inode_new (_state->itable);	\
		if (_resolved) {					\
			resolvedlen = strlen (_resolved);		\
			strncpy (path, _resolved, resolvedlen);		\
			_state->resolved = memdup (path, pathlen);	\
			if (resolvedlen == 1) /* only root resolved */	\
				pad = 0;				\
			else {						\
				pad = 1;				\
				path[resolvedlen] = '/';		\
			}						\
			strcpy_till (path + resolvedlen + pad, loc->path + resolvedlen + pad, '/'); \
		} else {						\
			strncpy (path, _loc->path, pathlen);		\
		}							\
		_state->loc.path = path;				\
		_state->loc.name = strrchr (path, '/');			\
		if (_state->loc.name)					\
			_state->loc.name++;				\
		_state->path = strdup (_loc->path);			\
	}while (0);

/* SERVER_DENTRY_UPDATE_STATE - update a server_state_t, to prepare state
 *                              for new lookup
 *
 * @state - state to be updated.
 */
#define SERVER_DENTRY_UPDATE_STATE(_state) do {				\
		char *path = NULL;					\
		size_t pathlen = 0;					\
		strcpy (_state->resolved, _state->loc.path);		\
		pathlen = strlen (_state->loc.path);			\
		if (!strcmp (_state->resolved, _state->path)) {		\
			free (_state->resolved);			\
			_state->resolved = NULL;			\
			goto resume;					\
		}							\
									\
		path = (char *)(_state->loc.path + pathlen);		\
		path[0] = '/';						\
		strcpy_till (path + 1,					\
			     _state->path + pathlen + 1, '/');		\
		_state->loc.name = strrchr (_state->loc.path, '/');	\
		if (_state->loc.name)					\
			_state->loc.name++;				\
		inode_unref (_state->loc.parent);			\
		_state->loc.parent = inode_ref (_state->loc.inode);	\
		inode_unref (_state->loc.inode);			\
		_state->loc.inode = inode_new (_state->itable);		\
	}while (0);

/* NOTE: should be used only for a state which was created by __do_path_resolve
 *       using any other state will result in double free corruption.
 */
#define SERVER_STATE_CLEANUP(_state) do {	\
		if (_state->resolved)		\
			free (_state->resolved);	\
		if (_state->path)		\
			free (_state->path);	\
		server_loc_wipe (&_state->loc);	\
		free_state (_state);		\
	} while (0);

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

/* __server_path_to_parenti - derive parent inode for @path. if immediate parent is
 *                            not available in the dentry cache, return nearest
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
__server_path_to_parenti (inode_table_t *itable,
                          const char *path,
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


        pathlen = STRLEN_0 (path);
        resolved_till = CALLOC (1, pathlen);

        GF_VALIDATE_OR_GOTO("server-dentry", resolved_till, out);
        pathdup = strdup (path);
        GF_VALIDATE_OR_GOTO("server-dentry", pathdup, out);

        parent = inode_ref (itable->root);
        curr = NULL;

        component = strtok_r (pathdup, "/", &strtokptr);

        while (component) {
                curr = inode_search (itable, parent->ino, component);
                if (!curr) {
                        /* if current component was the last component
                           set it to NULL                                                           
			*/
                        component = strtok_r (NULL, "/", &strtokptr);
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

        free (pathdup);

        if (component) {
                *reslv = resolved_till;
        } else {
                free (resolved_till);
        }
out:
        return parent;
}


/* __do_path_resolve_cbk -
 *
 * @frame -
 * @cookie -
 * @this -
 * @op_ret -
 * @op_errno -
 * @inode -
 * @stbuf -
 * @dict -
 *
 */
static int32_t
__do_path_resolve_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       inode_t *inode,
		       struct stat *stbuf,
		       dict_t *dict,
                       struct stat *postparent)
{
	server_state_t *state = NULL;
	call_stub_t *stub = NULL;
	inode_t *parent = NULL;

	stub = frame->local;
	state = CALL_STATE(frame);
	
	parent = state->loc.parent;

	if (op_ret == -1) {
		if (strcmp (state->path, state->loc.path))
			parent = NULL;
		
		server_stub_resume (stub, op_ret, op_errno, NULL, parent);
		goto cleanup;
	} else {
		if (inode->ino == 0) {
			gf_log (BOUND_XL(frame)->name, GF_LOG_DEBUG,
				"looked up for %s (%"PRId64"/%s)",
				state->loc.path, state->loc.parent->ino, state->loc.name);
			inode_link (inode, state->loc.parent, state->loc.name, stbuf);
			inode_lookup (inode);
		}

		if (state->resolved) {
			SERVER_DENTRY_UPDATE_STATE(state);

			gf_log (BOUND_XL(frame)->name, GF_LOG_DEBUG,
				"looking up for %s (%"PRId64"/%s)",
				state->loc.path, state->loc.parent->ino, state->loc.name);

			STACK_WIND (frame,
				    __do_path_resolve_cbk,
				    BOUND_XL(frame),
				    BOUND_XL(frame)->fops->lookup,
				    &(state->loc),
				    0);

			goto out;
		}
	resume:
		/* we are done, call stub_resume() to do rest of the job */
		server_stub_resume (stub, op_ret, op_errno, inode, parent);
	cleanup:
		SERVER_STATE_CLEANUP(state);
		/* stub will be freed by stub_resume, leave no traces */
		frame->local = NULL;
		STACK_DESTROY (frame->root);
	}
out:
	return 0;
}

/* __do_path_resolve - resolve @loc->path into @loc->inode and @loc->parent. also
 *                     update the dentry cache
 *
 * @stub - call stub to resume after resolving @loc->path
 * @loc  - loc to resolve before resuming @stub.
 *
 * return - return value of __do_path_resolve doesn't matter to the caller, if @stub
 *          is not NULL.
 */
static int32_t
__do_path_resolve (call_stub_t *stub,
		   const loc_t *loc)
{
	int32_t         ret = -1;
	char           *resolved  = NULL;
	call_frame_t   *new_frame = NULL;
	server_state_t *state = NULL, *new_state = NULL;
	inode_t        *parent = NULL;
	
	state = CALL_STATE(stub->frame);
	parent = loc->parent;
	if (parent) {
		inode_ref (parent);
		gf_log (BOUND_XL(stub->frame)->name, GF_LOG_DEBUG,
			"loc->parent(%"PRId64") already present. sending lookup "
			"for %"PRId64"/%s", parent->ino, parent->ino, loc->name);
		resolved = strdup (loc->path);
		resolved = dirname (resolved);
	} else {
		parent = __server_path_to_parenti (state->itable, loc->path, &resolved);
	}

	if (parent == NULL) {
		/* fire in the bush.. run! run!! run!!! */
		gf_log ("server",
			GF_LOG_CRITICAL,
			"failed to get parent inode number");
		goto panic;
	}		

	if (resolved) {
		gf_log (BOUND_XL(stub->frame)->name,
			GF_LOG_DEBUG,
			"resolved path(%s) till %"PRId64"(%s). "
			"sending lookup for remaining path",
			loc->path, parent->ino, resolved);
	}
	
	{
		new_frame = server_copy_frame (stub->frame);
		new_state = CALL_STATE(new_frame);

		SERVER_DENTRY_STATE_PREPARE(new_state, loc, parent, resolved);
		
		if (parent)
			inode_unref (parent); /* __server_path_to_parenti()'s  inode_ref */
		free (resolved);
		/* now interpret state as:
		 * state->path - compelete pathname to resolve
		 * state->resolved - pathname resolved from dentry cache
		 */
		new_frame->local = stub;
		STACK_WIND (new_frame,
			    __do_path_resolve_cbk,
			    BOUND_XL(new_frame),
			    BOUND_XL(new_frame)->fops->lookup,
			    &(new_state->loc),
			    0);
		goto out;
	}
panic:
	server_stub_resume (stub, -1, ENOENT, NULL, NULL);	
out:
	return ret;
}


/*
 * do_path_lookup - transform a pathname into inode, with the compelete
 *                  dentry tree upto inode built.
 *
 * @stub - call stub to resume after completing pathname to inode transform
 * @loc  - location. valid fields that do_path_lookup() uses in @loc are
 *         @loc->path - pathname
 *         @loc->ino  - inode number
 *
 * return - do_path_lookup returns only after complete dentry tree is built
 *          upto @loc->path.
 */
int32_t
do_path_lookup (call_stub_t *stub,
		const loc_t *loc)
{
	char       *pathname  = NULL;
	char       *directory = NULL;
	inode_t    *inode = NULL;
	inode_t    *parent = NULL;
	server_state_t *state = NULL;
	
	state = CALL_STATE(stub->frame);

	inode = inode_from_path (state->itable, loc->path);
	pathname  = strdup (loc->path);
	directory = dirname (pathname);
	parent = inode_from_path (state->itable, directory);
	
	if (inode && parent) {
		gf_log (BOUND_XL(stub->frame)->name,
			GF_LOG_DEBUG,
			"resolved path(%s) to %"PRId64"/%"PRId64"(%s)",
			loc->path, parent->ino, inode->ino, loc->name);
		server_stub_resume (stub, 0, 0, inode, parent);
		inode_unref (inode);
		inode_unref (parent);
	} else {
		gf_log (BOUND_XL(stub->frame)->name,
			GF_LOG_DEBUG,
			"resolved path(%s) to %p(%"PRId64")/%p(%"PRId64")",
			loc->path, parent, (parent ? parent->ino : 0), 
			inode, (inode ? inode->ino : 0));
		if (parent) {
			inode_unref (parent);
		} else if (inode) {
			inode_unref (inode);
			gf_log (BOUND_XL(stub->frame)->name,
				GF_LOG_ERROR,
				"undesired behaviour. inode(%"PRId64") for %s "
				"exists without parent (%s)", 
				inode->ino, loc->path, directory);
		}
		__do_path_resolve (stub, loc);
	}
	
	if (pathname)
		free (pathname);

	return 0;
}
