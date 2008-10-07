#include "glusterfs.h"
#include "xlator.h"
#include "server-protocol.h"
#include <libgen.h>

/* SERVER_DENTRY_STATE_PREPARE - prepare a fresh state for use
 *
 * @state    - an empty state
 * @loc      - loc_t which needs to resolved
 * @parent   - most immediate parent of @loc available in dentry cache
 * @resolved - component of @loc->path which has been resolved
 *             through dentry cache
 */
#define SERVER_DENTRY_STATE_PREPARE(state,loc,parent,resolved) do {	\
		size_t pathlen = 0;					\
		size_t resolvedlen = 0;					\
		char *path = NULL;					\
		pathlen   = strlen (loc->path) + 1;			\
		path = calloc (1, pathlen);				\
		new_state->loc.parent = inode_ref (parent);		\
		new_state->loc.inode  = inode_new (new_state->itable);	\
		if (resolved) {						\
			resolvedlen = strlen (resolved);		\
			strncpy (path, resolved, resolvedlen);		\
			new_state->resolved = memdup (path, pathlen);	\
		} else {						\
			strncpy (path, loc->path, pathlen);		\
		}							\
		new_state->loc.path = path;				\
		new_state->loc.name = strrchr (path, '/');		\
		if (new_state->loc.name)				\
			new_state->loc.name++;				\
		new_state->path = strdup (loc->path);			\
	}while (0);

/* SERVER_DENTRY_UPDATE_STATE - update a server_state_t, to prepare state
 *                              for new lookup
 *
 * @state - state to be updated.
 */
#define SERVER_DENTRY_UPDATE_STATE(state) do {				\
		char *path = NULL;					\
		size_t pathlen = 0;					\
		strcpy (state->resolved, state->loc.path);		\
		pathlen = strlen (state->loc.path);			\
		if (!strcmp (state->resolved, state->path)) {		\
			free (state->resolved);				\
			state->resolved = NULL;				\
			goto resume;					\
		}							\
									\
		path = (char *)(state->loc.path + pathlen);		\
		path[0] = '/';						\
		strcpy_till (path + 1,					\
			     state->path + pathlen + 1, '/');		\
		state->loc.name = strrchr (state->loc.path, '/');	\
		if (state->loc.name)					\
			state->loc.name++;				\
		inode_unref (state->loc.parent);			\
		state->loc.parent = inode_ref (state->loc.inode);	\
		inode_unref (state->loc.inode);				\
		state->loc.inode = inode_new (state->itable);		\
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

static inline __attribute__((always_inline))
call_frame_t *
server_copy_frame (call_frame_t *frame)
{
	call_frame_t *new_frame = NULL;
	server_state_t *state = NULL, *new_state = NULL;
	call_ctx_t *_call = NULL;

	state = frame->root->state;

	new_frame = copy_frame (frame);

	_call = new_frame->root;

	new_state = calloc (1, sizeof (server_state_t));

	_call->frames.op   = frame->op;
	_call->frames.type = frame->type;
	_call->trans       = state->trans;
	_call->state       = new_state;

	new_state->bound_xl = state->bound_xl;
	new_state->trans    = transport_ref (state->trans);
	new_state->itable   = state->itable;

	return new_frame;
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
	state = STATE (frame);
	
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

/* __do_resolve_path - resolve @loc->path into @loc->inode and @loc->parent. also
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

	state = (server_state_t *) (stub->frame->root->state);
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
		new_state = STATE (new_frame);

		SERVER_DENTRY_STATE_PREPARE(new_state, loc, parent, resolved);
		
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
	__do_path_resolve (stub, loc);

	return 0;
}
