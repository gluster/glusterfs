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
#define SERVER_DENTRY_STATE_PREPARE(_state,loc,parent,resolved) do {	\
		size_t pathlen = 0;					\
		size_t resolvedlen = 0;					\
		char *path = NULL;					\
		int pad = 0;						\
		pathlen   = strlen (loc->path) + 1;			\
		path = calloc (1, pathlen);				\
		_state->loc.parent = inode_ref (parent);		\
		_state->loc.inode  = inode_new (_state->itable);	\
		if (resolved) {						\
			resolvedlen = strlen (resolved);		\
			strncpy (path, resolved, resolvedlen);		\
			_state->resolved = memdup (path, pathlen);	\
			if (resolvedlen == 1) /* only root resolved */	\
				pad = 0;				\
			else {						\
				pad = 1;				\
				path[resolvedlen] = '/';		\
			}						\
			strcpy_till (path + resolvedlen + pad, loc->path + resolvedlen + pad, '/'); \
		} else {						\
			strncpy (path, loc->path, pathlen);		\
		}							\
		_state->loc.path = path;				\
		_state->loc.name = strrchr (path, '/');			\
		if (_state->loc.name)					\
			_state->loc.name++;				\
		_state->path = strdup (loc->path);			\
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
#define SERVER_STATE_CLEANUP(state) do {	\
		if (state->resolved)		\
			free (state->resolved);	\
		if (state->path)		\
			free (state->path);	\
		server_loc_wipe (&state->loc);	\
		free_state (state);		\
	} while (0);

/* strcpy_till - copy @dname to @dest, until 'delim' is encountered in @dest
 * @dest - destination string
 * @dname - source string
 * @delim - delimiter character
 *
 * return - NULL is returned if '0' is encountered in @dname, otherwise returns
 *          a pointer to remaining string begining in @dest.
 */
static inline __attribute__((always_inline))
char *
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

static inline __attribute__((always_inline))
int
str_rerase_till (char *str,
		 char delim)
{
	char *strend = NULL;
	size_t idx = strlen (str);

	strend = str;
	while ((strend[idx] != '/') && idx) {
		strend[idx] = 0;
		idx--;
	}

	return idx;
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
	char *resolved = NULL;
	size_t pathlen = 0;

	char *dname = NULL;

	inode_t *inode = NULL;
	inode_t *parent = NULL;

	size_t copied = 0;
	size_t r_len = 0;

	pathlen = strlen (path);
	resolved = calloc (1, 1 + pathlen);
	inode = itable->root;
	parent = inode;
	dname = (char *)path;

	while (dname && inode) {
		copied = strlen (resolved);
		resolved[copied] = '/';
		copied++;
		while (*dname == '/')
			dname++;
		dname = strcpy_till (resolved + copied, dname, '/');

		inode = inode_from_path (itable, resolved);
		if (inode) {
			if (parent)
				inode_unref (parent);
			parent = inode;
		} else {
			str_rerase_till (resolved, '/');
		}
	}

	if (dname) {
		r_len = strlen (resolved);
		if ((r_len > 1) && (resolved[(r_len - 1)] == '/'))
			resolved[(r_len - 1)] = '\0';
		*reslv = resolved;
	} else {
		free (resolved);
	}

	return parent;
}


/* __do_resolve_path_cbk -
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
		       dict_t *dict)
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
			inode_link (inode, state->loc.parent, state->loc.name, stbuf);
			inode_lookup (inode);
		}

		if (state->resolved) {
			SERVER_DENTRY_UPDATE_STATE(state);
			STACK_WIND (frame,
				    __do_path_resolve_cbk,
				    BOUND_XL (frame),
				    BOUND_XL (frame)->fops->lookup,
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

	parent = __server_path_to_parenti (state->itable, loc->path, &resolved);
	if (parent == NULL) {
		/* fire in the bush.. run! run!! run!!! */
		gf_log ("server",
			GF_LOG_CRITICAL,
			"failed to get parent inode number");
		goto panic;
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
	}
panic:
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
	if (pathname)
		free (pathname);
	
	if (inode && parent) {
		server_stub_resume (stub, 0, 0, inode, parent);
		inode_unref (inode);
		inode_unref (parent);
	} else {
		if (parent)
			inode_unref (parent);
		__do_path_resolve (stub, loc);
	}

	return 0;
}
