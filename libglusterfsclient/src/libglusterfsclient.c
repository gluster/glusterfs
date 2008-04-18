#include <stdio.h>
#include <errno.h>
#include <libgen.h>

#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xlator.h>
#include <timer.h>
#include <time.h>
#include <poll.h>
#include <transport.h>
#include "libglusterfsclient.h"
#include "libglusterfsclient-internals.h"

#define GLUSTERFS_CTX_KEY "__libglusterfs-client-ctx"
#define GLUSTERFS_OFFSET_KEY "__libglusterfs-client-offset"
#define GLUSTERFS_LOOKUP_KEY "__libglusterfs-client-previous-lookup"
#define GLUSTERFS_STAT_CACHE_KEY "__libglusterfs-client-stat-cache"
#define GLUSTERFS_STAT_TIME_KEY "__libglusterfs-client-stat-time"

/* TODO: free members of stub if necessary */
typedef struct libglusterfs_client_async_local {
  void *cbk_data;
  union {
    struct {
      fd_t *fd;
      glusterfs_readv_cbk_t cbk;
    }readv_cbk;
    
    struct {
      fd_t *fd;
      glusterfs_writev_cbk_t cbk;
    }writev_cbk;

    struct {
      fd_t *fd;
    }close_cbk;
  }fop;
}libglusterfs_client_async_local_t;
    
static inline xlator_t *
libglusterfs_graph (xlator_t *graph);

void *poll_proc (void *ptr)
{
  glusterfs_ctx_t *ctx = ptr;

  while (!poll_iteration (ctx));

  return NULL;
}

int32_t
xlator_graph_init (xlator_t *xl)
{
  xlator_t *trav = xl;
  int32_t ret = -1;

  while (trav->prev)
    trav = trav->prev;

  while (trav) {
    if (!trav->ready) {
      ret = xlator_tree_init (trav);
      if (ret < 0)
	break;
    }
    trav = trav->next;
  }

  return ret;
}

static inode_t *
dummy_inode (inode_table_t *table)
{
  inode_t *dummy;

  dummy = calloc (1, sizeof (*dummy));

  dummy->table = table;

  INIT_LIST_HEAD (&dummy->list);
  INIT_LIST_HEAD (&dummy->inode_hash);
  INIT_LIST_HEAD (&dummy->fds);
  INIT_LIST_HEAD (&dummy->dentry.name_hash);
  INIT_LIST_HEAD (&dummy->dentry.inode_list);

  dummy->ref = 1;
  dummy->ctx = get_new_dict ();

  LOCK_INIT (&dummy->lock);
  return dummy;
}

static call_frame_t *
get_call_frame_for_req (libglusterfs_client_ctx_t *ctx, char d)
{
  call_pool_t *pool = ctx->gf_ctx.pool;
  call_ctx_t *cctx = NULL;
  
  cctx = calloc (1, sizeof (*cctx));
  cctx->frames.root = cctx;

  cctx->uid = geteuid ();
  cctx->gid = getegid ();
  cctx->pid = getpid ();
  cctx->unique = ctx->counter++;
  
  cctx->frames.this = ctx->gf_ctx.graph;
  
  if (d) {
    cctx->req_refs = dict_ref (get_new_dict ());
    /*
      TODO
      dict_set (cctx->req_refs, NULL, priv->buf);
      cctx->req_refs->is_locked = 1;
    */
  }

  cctx->pool = pool;
  LOCK (&pool->lock);
  list_add (&cctx->all_frames, &pool->all_frames);
  UNLOCK (&pool->lock);

  return &cctx->frames;
}

void 
libgf_client_fini (xlator_t *this)
{
  return;
}

int32_t
libgf_client_notify (xlator_t *this, int32_t event,
	void *data, ...)
{

  return 0;
}

int32_t 
libgf_client_init (xlator_t *this)
{
  return 0;
}

libglusterfs_handle_t 
glusterfs_init (glusterfs_init_ctx_t *init_ctx)
{
  libglusterfs_client_ctx_t *ctx = NULL;
  FILE *specfp = NULL;
  xlator_t *graph = NULL;
  call_pool_t *pool = NULL;
  struct rlimit lim;

  if (!init_ctx || !init_ctx->specfile) {
    /*    fprintf (stderr, "Invalid arguments\n"); */
    errno = EINVAL;
    return NULL;
  }

  /* TODO: maintain a ctx table so that user cannot compromise by passing arbitrary ctx */

  ctx = calloc (1, sizeof (*ctx));
  if (!ctx) {
    fprintf (stderr, "libglusterfs-client: out of memory, gf_init failed\n");
    errno = ENOMEM;
    return NULL;
  }

  ctx->lookup_timeout = init_ctx->lookup_timeout;
  ctx->stat_timeout = init_ctx->stat_timeout;

  pthread_mutex_init (&ctx->gf_ctx.lock, NULL);
  
  pool = ctx->gf_ctx.pool = calloc (1, sizeof (call_pool_t));
  LOCK_INIT (&pool->lock);
  INIT_LIST_HEAD (&pool->all_frames);

  lim.rlim_cur = RLIM_INFINITY;
  lim.rlim_max = RLIM_INFINITY;
  setrlimit (RLIMIT_CORE, &lim);
  setrlimit (RLIMIT_NOFILE, &lim);  

  if (init_ctx->logfile)
    ctx->gf_ctx.logfile = strdup (init_ctx->logfile);
  else
    asprintf (&ctx->gf_ctx.logfile, "/dev/stderr");

  if (!init_ctx->loglevel)
    ctx->gf_ctx.loglevel = GF_LOG_ERROR;
  else {
    if (!strncasecmp (init_ctx->loglevel, "DEBUG", strlen ("DEBUG"))) {
      ctx->gf_ctx.loglevel = GF_LOG_DEBUG;
    } else if (!strncasecmp (init_ctx->loglevel, "WARNING", strlen ("WARNING"))) {
      ctx->gf_ctx.loglevel = GF_LOG_WARNING;
    } else if (!strncasecmp (init_ctx->loglevel, "CRITICAL", strlen ("CRITICAL"))) {
      ctx->gf_ctx.loglevel = GF_LOG_CRITICAL;
    } else if (!strncasecmp (init_ctx->loglevel, "NONE", strlen ("NONE"))) {
      ctx->gf_ctx.loglevel = GF_LOG_NONE;
    } else if (!strncasecmp (init_ctx->loglevel, "ERROR", strlen ("ERROR"))) {
      ctx->gf_ctx.loglevel = GF_LOG_ERROR;
    } else {
      fprintf (stderr, "glusterfs: Unrecognized log-level \"%s\", possible values are \"DEBUG|WARNING|[ERROR]|CRITICAL|NONE\"\n", init_ctx->loglevel);
      freee (ctx);
      return NULL;
    }
  }
  
	      
  if (gf_log_init (ctx->gf_ctx.logfile) == -1) {
    fprintf (stderr,
	     "glusterfs: failed to open logfile \"%s\"\n",
	     ctx->gf_ctx.logfile);
    freee (ctx);
    return NULL;
  }

  gf_log_set_loglevel (ctx->gf_ctx.loglevel);

  /*  ctx->gf_ctx.specfile = strdup (specfile); */
  specfp = fopen (init_ctx->specfile, "r");

  if (!specfp) {
    fprintf (stderr,
	     "glusterfs: could not open specfile: %s\n", strerror (errno));
    freee (ctx);
    return NULL;
  }

  gf_timer_registry_init (&ctx->gf_ctx);

  graph = file_to_xlator_tree (&ctx->gf_ctx, specfp);
  graph = libglusterfs_graph (graph);

  ctx->itable = inode_table_new (0, graph);

  if (xlator_graph_init (graph) == -1) {
    gf_log ("glusterfs", GF_LOG_ERROR,
	    "Initializing graph failed");

    /*inode_table_destroy (ctx->itable) ? */
    freee (ctx);
    return NULL;
  }

  ctx->gf_ctx.graph = graph;
  pthread_create (&ctx->reply_thread, NULL, poll_proc, (void *)&ctx->gf_ctx);
  return ctx;
}

int 
glusterfs_fini (libglusterfs_client_ctx_t *ctx)
{
  freee (ctx->gf_ctx.logfile);
  /* freee (ctx->gf_ctx.specfile); */

  /* TODO complete cleanup of timer */
  ((gf_timer_registry_t *)ctx->gf_ctx.timer)->fin = 1;
  /*TODO 
   * destroy the reply thread 
   * destroy inode table
   * destroy the graph
   * free (ctx) 
   */

  return 0;
}

int32_t 
libgf_client_lookup_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 inode_t *inode,
			 struct stat *buf,
			 dict_t *dict)
{
  libgf_client_local_t *local = frame->local;

  local->reply_stub = fop_lookup_cbk_stub (frame, NULL, op_ret, op_errno, inode, buf, NULL);
  pthread_mutex_lock (&local->lock);
  {
    local->complete = 1;
    pthread_cond_broadcast (&local->reply_cond);
  }
  pthread_mutex_unlock (&local->lock);

  return 0;
}

int32_t
libgf_client_lookup (libglusterfs_client_ctx_t *ctx,
		     loc_t *loc,
		     struct stat *stbuf)
{
  call_stub_t  *stub = NULL;
  int32_t op_ret;
  /* Directory structure is flat. i.e., all the files and directories are immediate children of root directory */

  /*  if (loc->inode) 
    revalidate = 1;
  */
  loc->inode = dummy_inode (ctx->itable);
  LIBGF_CLIENT_FOP(ctx, stub, lookup, loc, 0);

  if (stub->args.lookup_cbk.op_ret == 0) {
    inode_t *libgf_inode = NULL;
    struct stat *buf = NULL;
    time_t current = 0;

    /* flat directory structure */
    inode_t *parent = inode_search (ctx->itable, 1, NULL);
    libgf_inode = inode_update (ctx->itable, parent, loc->path, &stub->args.lookup_cbk.buf);
    if (stub->args.lookup_cbk.inode->ctx != libgf_inode->ctx) {
      dict_t *swap = stub->args.lookup_cbk.inode->ctx;
      stub->args.lookup_cbk.inode->ctx = libgf_inode->ctx;
      libgf_inode->ctx = swap;
    }

    inode_unref (loc->inode);
    loc->inode = libgf_inode;

    current = time (NULL);
    dict_set (loc->inode->ctx, GLUSTERFS_LOOKUP_KEY, data_from_uint32 (current));
    dict_set (loc->inode->ctx, GLUSTERFS_STAT_TIME_KEY, data_from_uint32 (current));

    buf = calloc (1, sizeof (*buf));
    memcpy (buf, &stub->args.lookup_cbk.buf, sizeof (*buf));
    dict_set (loc->inode->ctx, GLUSTERFS_STAT_CACHE_KEY, data_from_dynptr (buf, sizeof (*buf)));

    if (stbuf)
      *stbuf = stub->args.lookup_cbk.buf; 

    inode_unref (parent);
  }
  else
    {
      /* TODO: write this later */
      /*
      inode_t *parent = inode_search (ctx->itable, 1, NULL);

      if (revalidate) 
	inode_unlink (ctx->itable, parent, loc->path);

      inode_unref (parent);
      loc->inode = NULL;
      */

      /*
      if (stbuf)
	*stbuf = 0; 
	*/
    }
  
  op_ret = stub->args.lookup_cbk.op_ret;
  errno = stub->args.lookup_cbk.op_errno;

  if (stub->args.lookup_cbk.dict)
    dict_unref (stub->args.lookup_cbk.dict);
  if (stub->args.lookup_cbk.inode && 
      (stub->args.lookup_cbk.op_ret != -1))
    inode_unref (stub->args.lookup_cbk.inode);

  freee (stub);

  return op_ret;
}


int32_t
libgf_client_getxattr_cbk (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno,
			   dict_t *dict)
{

  libgf_client_local_t *local = frame->local;

  local->reply_stub = fop_getxattr_cbk_stub (frame, NULL, op_ret, op_errno, dict);

  pthread_mutex_lock (&local->lock);
  {
    local->complete = 1;
    pthread_cond_broadcast (&local->reply_cond);
  }
  pthread_mutex_unlock (&local->lock);

  return 0;
}

size_t 
libgf_client_getxattr (libglusterfs_client_ctx_t *ctx, 
		       loc_t *loc,
		       const char *name,
		       void *value,
		       size_t size)
{
  call_stub_t  *stub = NULL;
  int32_t op_ret = 0;

  LIBGF_CLIENT_FOP (ctx, stub, getxattr, loc, name);

  op_ret = stub->args.getxattr_cbk.op_ret;
  errno = stub->args.getxattr_cbk.op_errno;

  if (op_ret >= 0) {
    /*
      gf_log ("LIBGF_CLIENT", GF_LOG_DEBUG,
      "%"PRId64": %s => %d", frame->root->unique,
      state->fuse_loc.loc.path, op_ret);
    */

    data_t *value_data = dict_get (stub->args.getxattr_cbk.dict, (char *)name);
    
    if (value_data) {
      int32_t copy_len = 0;
      op_ret = value_data->len; /* Don't return the value for '\0' */

      copy_len = size < value_data->len ? size : value_data->len;
      /*FIXME: where is this freed? */
      memcpy (value, value_data->data, copy_len);
    } else {
      errno = ENODATA;
      op_ret = -1;
    }
  }

  /* FIXME: where are the contents of stub like loc are freed? */
  if (stub->args.getxattr_cbk.dict)
    dict_unref (stub->args.getxattr_cbk.dict);

  freee (stub);
  return op_ret;
}

ssize_t 
glusterfs_getxattr (libglusterfs_client_ctx_t *ctx, 
		    const char *path, 
		    const char *name,
		    void *value, 
		    size_t size)
{
  int32_t op_ret = 0;
  char lookup_required = 1;
  loc_t loc;
  /*   list_head_t signal_handlers; */

  loc.path = strdup (path);
  loc.inode = inode_search (ctx->itable, 1, loc.path);

  if (loc.inode && loc.inode->ctx) {
    time_t current, prev;
 
    memset (&current, 0, sizeof (current));
    current = time (NULL);
    if (((int32_t)(prev = data_to_uint32(dict_get (loc.inode->ctx, GLUSTERFS_LOOKUP_KEY)))) >= 0 && ctx->lookup_timeout >= (current - prev)) {
      lookup_required = 0;
    } 
  }

  /*  LIBGF_INSTALL_SIGNAL_HANDLERS (signal_handlers); */
  if (lookup_required) {
    op_ret = libgf_client_lookup (ctx, &loc, NULL);
  }

  if (!op_ret)
    op_ret = libgf_client_getxattr (ctx, &loc, name, value, size);

  freee (loc.path);
  /*  LIBGF_RESTORE_SIGNAL_HANDLERS (signal_handlers); */
  return op_ret;
}

static int32_t
libgf_client_open_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       fd_t *fd)
{
  libgf_client_local_t *local = frame->local;

  local->reply_stub = fop_open_cbk_stub (frame, NULL, op_ret, op_errno, fd);

  pthread_mutex_lock (&local->lock);
  {
    local->complete = 1;
    pthread_cond_broadcast (&local->reply_cond);
  }
  pthread_mutex_unlock (&local->lock);

  return 0;
}


int 
libgf_client_open (libglusterfs_client_ctx_t *ctx, 
		   loc_t *loc, 
		   fd_t *fd, 
		   int flags)
{
  call_stub_t *stub = NULL;
  int32_t op_ret = 0;

  LIBGF_CLIENT_FOP (ctx, stub, open, loc, flags, fd);

  op_ret = stub->args.open_cbk.op_ret;
  errno = stub->args.open_cbk.op_errno;

  /* call-stub->args.c does'nt fd_ref (fd), is it not a bug? */
  /* unref the reference done while creating stub */
  /* fd_unref (stub->args.open_cbk.fd); */

  freee  (stub);
  return op_ret;
}

static int32_t
libgf_client_create_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 fd_t *fd,
			 inode_t *inode,
			 struct stat *buf)     
{
  libgf_client_local_t *local = frame->local;

  local->reply_stub = fop_create_cbk_stub (frame, NULL, op_ret, op_errno, fd, inode, buf);

  pthread_mutex_lock (&local->lock);
  {
    local->complete = 1;
    pthread_cond_broadcast (&local->reply_cond);
  }
  pthread_mutex_unlock (&local->lock);

  return 0;
}

int 
libgf_client_creat (libglusterfs_client_ctx_t *ctx,
		    loc_t *loc,
		    fd_t *fd,
		    int flags,
		    mode_t mode)
{
  call_stub_t *stub = NULL;
  int32_t op_ret = 0;

  loc->inode = dummy_inode (ctx->itable);

  LIBGF_CLIENT_FOP (ctx, stub, create, loc, flags, mode, fd);
  
  if (stub->args.create_cbk.op_ret == 0) {
    inode_t *libgf_inode = NULL;
    
    /* flat directory structure */
    inode_t *parent = inode_search (ctx->itable, 1, NULL);
    libgf_inode = inode_update (ctx->itable, parent, loc->path, &stub->args.lookup_cbk.buf);
    if (stub->args.create_cbk.inode->ctx != libgf_inode->ctx) {
      dict_t *swap = stub->args.create_cbk.inode->ctx;
      stub->args.create_cbk.inode->ctx = libgf_inode->ctx;
      libgf_inode->ctx = swap;
    }

    inode_unref (loc->inode);
    loc->inode = libgf_inode;

    /*
    if (stbuf)
      *stbuf = stub->args.lookup_cbk.buf; 
      */
    inode_unref (parent);
  }

  inode_unref (stub->args.create_cbk.inode);

  op_ret = stub->args.create_cbk.op_ret;
  errno = stub->args.create_cbk.op_errno;
  
  return op_ret;
}

int32_t
libgf_client_opendir_cbk (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  fd_t *fd)
{
  libgf_client_local_t *local = frame->local;

  local->reply_stub = fop_opendir_cbk_stub (frame, NULL, op_ret, op_errno, fd);

  pthread_mutex_lock (&local->lock);
  {
    local->complete = 1;
    pthread_cond_broadcast (&local->reply_cond);
  }
  pthread_mutex_unlock (&local->lock);

  return 0;
}

int 
libgf_client_opendir (libglusterfs_client_ctx_t *ctx,
		      loc_t *loc,
		      fd_t *fd)
{
  call_stub_t *stub = NULL;
  int32_t op_ret = 0;

  LIBGF_CLIENT_FOP (ctx, stub, opendir, loc, fd);

  op_ret = stub->args.opendir_cbk.op_ret;
  errno = stub->args.opendir_cbk.op_errno;
  return 0;
}

long 
glusterfs_open (libglusterfs_client_ctx_t *ctx, 
		const char *path, 
		int flags, 
		mode_t mode)
{
  loc_t loc;
  int32_t op_ret = 0;
  fd_t *fd = NULL;
  struct stat stbuf; 
  char lookup_required = 1;
  
  if (!ctx || !path) {
    errno = EINVAL;
    return -1;
  }

  loc.path = strdup (path);
  loc.inode = inode_search (ctx->itable, 1, loc.path);

  if (!(flags & O_APPEND) && loc.inode && loc.inode->ctx) {
    time_t current, prev;
 
    memset (&current, 0, sizeof (current));
    current = time (NULL);
    if (((int32_t)(prev = data_to_uint32(dict_get (loc.inode->ctx, GLUSTERFS_LOOKUP_KEY)))) >= 0 && ctx->lookup_timeout >= (current - prev)) {
      lookup_required = 0;
    } 
  }

  if (lookup_required) {
    op_ret = libgf_client_lookup (ctx, &loc, &stbuf);
    if (!op_ret && ((flags & O_CREAT) == O_CREAT) && ((flags & O_EXCL) == O_EXCL)) {
      errno = EEXIST;
      op_ret = -1;
    }
  }

  if (!op_ret || (op_ret == -1 && errno == ENOENT && ((flags & O_CREAT) == O_CREAT))) {
    fd = fd_create (loc.inode);

    if (!op_ret) {
      if (S_ISDIR (loc.inode->st_mode)) {
	/*FIXME: check for O_DIRECTORY before calling opendir */
	if ((flags & O_RDONLY) == O_RDONLY)
	  op_ret = libgf_client_opendir (ctx, &loc, fd);
	else {
	  op_ret = -1;
	  errno = EEXIST;
	}
      }
      else
	op_ret = libgf_client_open (ctx, &loc, fd, flags);
    }
    else
      op_ret = libgf_client_creat (ctx, &loc, fd, flags, mode);

    if (op_ret == -1) {
      fd_destroy (fd);
    } else {
      int offset = 0;
      
      op_ret = (int)fd;
      
      if ((flags & O_APPEND) == O_APPEND)
	offset = stbuf.st_size;
      
      dict_set (fd->ctx, GLUSTERFS_OFFSET_KEY, data_from_int32 (offset));
      dict_set (fd->ctx, GLUSTERFS_CTX_KEY, data_from_static_ptr (ctx));
    }
  }

  freee (loc.path);
  return op_ret;
}

long 
glusterfs_creat (libglusterfs_client_ctx_t *ctx, const char *path, mode_t mode)
{
  loc_t loc;
  int32_t op_ret = -1;
  fd_t *fd = NULL;

  if (!ctx || !path) {
    errno = EINVAL;
    return -1;
  }

  loc.path = strdup (path);
  loc.inode = NULL;

  /*TODO: send create only if file does not exist, otherwise send open */
  /*  libgf_client_lookup (ctx, &loc, NULL); */

  fd = fd_create (loc.inode);

  op_ret = libgf_client_creat (ctx, &loc, fd, O_CREAT|O_WRONLY|O_TRUNC, mode);

  if (op_ret == -1) {
    fd_destroy (fd);
  } else {
    int offset = 0;
      
    op_ret = (int32_t) fd;
    
    dict_set (fd->ctx, GLUSTERFS_OFFSET_KEY, data_from_int32 (offset));
    dict_set (fd->ctx, GLUSTERFS_CTX_KEY, data_from_static_ptr (ctx));
  }
  freee (loc.path);

  return op_ret;
}

int32_t
libgf_client_close_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno)
{
  libgf_client_local_t *local = frame->local;

  local->reply_stub = fop_close_cbk_stub (frame, NULL, op_ret, op_errno);

  pthread_mutex_lock (&local->lock);
  {
    local->complete = 1;
    pthread_cond_broadcast (&local->reply_cond);
  }
  pthread_mutex_unlock (&local->lock);

  return 0;
}

int 
libgf_client_close_async_cbk (call_frame_t *frame,
			      void *cookie,
			      xlator_t *this,
			      int32_t op_ret,
			      int32_t op_errno)
{
  libglusterfs_client_async_local_t *local = frame->local;

  if (!op_ret)
    fd_destroy (local->fop.close_cbk.fd);

  STACK_DESTROY (frame->root);
  return 0;
}

int 
libgf_client_close (libglusterfs_client_ctx_t *ctx, fd_t *fd)
{
  /*
  call_stub_t *stub;
  int32_t op_ret; */
  libglusterfs_client_async_local_t *local ;
  local = calloc (1, sizeof (*local));

  local->fop.close_cbk.fd = fd;
  LIBGF_CLIENT_FOP_ASYNC (ctx,
			  local,
			  libgf_client_close_async_cbk,
			  close,
			  fd);

  //  LIBGF_CLIENT_FOP (ctx, stub, close, fd);

  /* op_ret = stub->args.close_cbk.op_ret;
     errno = stub->args.close_cbk.op_errno; 

     freee (stub);*/

  return 0;//op_ret;
}

int32_t
libgf_client_flush_cbk (call_frame_t *frame,
		   void *cookie,
		   xlator_t *this,
		   int32_t op_ret,
		   int32_t op_errno)
{
  libgf_client_local_t *local = frame->local;

  local->reply_stub = fop_flush_cbk_stub (frame, NULL, op_ret, op_errno);

  pthread_mutex_lock (&local->lock);
  {
    local->complete = 1;
    pthread_cond_broadcast (&local->reply_cond);
  }
  pthread_mutex_unlock (&local->lock);

  return 0;
}

int 
libgf_client_flush (libglusterfs_client_ctx_t *ctx, fd_t *fd)
{
  call_stub_t *stub;
  int32_t op_ret;

  LIBGF_CLIENT_FOP (ctx, stub, flush, fd);

  op_ret = stub->args.flush_cbk.op_ret;
  errno = stub->args.flush_cbk.op_errno;

  freee (stub);

  return op_ret;
}

int
libgf_client_closedir_cbk (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno)
{
 libgf_client_local_t *local = frame->local;

  local->reply_stub = fop_closedir_cbk_stub (frame, NULL, op_ret, op_errno);

  pthread_mutex_lock (&local->lock);
  {
    local->complete = 1;
    pthread_cond_broadcast (&local->reply_cond);
  }
  pthread_mutex_unlock (&local->lock);

  return 0;
}

int
libgf_client_closedir (libglusterfs_client_ctx_t *ctx, fd_t *fd)
{
  /*  call_stub_t *stub;
      int32_t op_ret;*/
  libglusterfs_client_async_local_t *local ;
  local = calloc (1, sizeof (*local));

  local->fop.close_cbk.fd = fd;

  LIBGF_CLIENT_FOP_ASYNC (ctx,
			  local,
			  libgf_client_close_async_cbk,
			  close,
			  fd);

  /*
  LIBGF_CLIENT_FOP (ctx, stub, closedir, fd);

  op_ret = stub->args.closedir_cbk.op_ret;
  errno = stub->args.closedir_cbk.op_errno;

  freee (stub);
  */
  return 0;
}

int 
glusterfs_close (long fd)
{
  int32_t op_ret = 0;
  libglusterfs_client_ctx_t *ctx = NULL;

  if (!fd) {
    errno = EINVAL;
    return -1;
  }

  ctx = data_to_ptr (dict_get (((fd_t *)fd)->ctx, GLUSTERFS_CTX_KEY));

  /*  op_ret = libgf_client_flush (ctx, (fd_t *)fd);  */
  if (!op_ret) {
    if (S_ISDIR (((fd_t *) fd)->inode->st_mode))
      op_ret = libgf_client_closedir (ctx, (fd_t *)fd);
    else
      op_ret = libgf_client_close (ctx, (fd_t *)fd);
  }

  /* FIXME: user can compromise system by passing arbitrary fd, use an fdtable */
  /*  if (!op_ret)
      fd_destroy ((fd_t *)fd); */

  return op_ret;
}

int32_t
libgf_client_setxattr_cbk (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno)
{
  libgf_client_local_t *local = frame->local;

  local->reply_stub = fop_setxattr_cbk_stub (frame, NULL, op_ret, op_errno);

  pthread_mutex_lock (&local->lock);
  {
    local->complete = 1;
    pthread_cond_broadcast (&local->reply_cond);
  }
  pthread_mutex_unlock (&local->lock);

  return 0;
}

int
libgf_client_setxattr (libglusterfs_client_ctx_t *ctx, 
		       loc_t *loc,
		       const char *name,
		       const void *value,
		       size_t size,
		       int flags)
{
  call_stub_t  *stub = NULL;
  int32_t op_ret = 0;
  dict_t *dict;

  dict = get_new_dict ();

  dict_set (dict, (char *)name,
	    bin_to_data ((void *)value, size));
  dict_ref (dict);


  LIBGF_CLIENT_FOP (ctx, stub, setxattr, loc, dict, flags);

  op_ret = stub->args.setxattr_cbk.op_ret;
  errno = stub->args.setxattr_cbk.op_errno;

  dict_unref (dict);
  freee (stub);
  return op_ret;
}

int 
glusterfs_setxattr (libglusterfs_client_ctx_t *ctx, const char *path, const char *name,
		    const void *value, size_t size, int flags)
{
  int32_t op_ret = 0;
  loc_t loc;
  char lookup_required = 1;
  /*   list_head_t signal_handlers; */

  loc.path = strdup (path);
  loc.inode = inode_search (ctx->itable, 1, loc.path);

  if (loc.inode && loc.inode->ctx) {
    time_t current, prev;
 
    memset (&current, 0, sizeof (current));
    current = time (NULL);
    if (((int32_t)(prev = data_to_uint32(dict_get (loc.inode->ctx, GLUSTERFS_LOOKUP_KEY)))) >= 0 && ctx->lookup_timeout >= (current - prev)) {
      lookup_required = 0;
    } 
  }

  if (lookup_required) {
    /*  LIBGF_INSTALL_SIGNAL_HANDLERS (signal_handlers); */
    op_ret = libgf_client_lookup (ctx, &loc, NULL);
  }

  if (!op_ret)
    op_ret = libgf_client_setxattr (ctx, &loc, name, value, size, flags);

  freee (loc.path);
  /*  LIBGF_RESTORE_SIGNAL_HANDLERS (signal_handlers); */
  return op_ret;
}

int 
glusterfs_lsetxattr (libglusterfs_client_ctx_t *ctx, const char *path, const char *name,
		     const void *value, size_t size, int flags)
{
  return ENOSYS;
}

int32_t
libgf_client_fsetxattr_cbk (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno)
{
  libgf_client_local_t *local = frame->local;

  local->reply_stub = fop_setxattr_cbk_stub (frame, NULL, op_ret, op_errno);

  pthread_mutex_lock (&local->lock);
  {
    local->complete = 1;
    pthread_cond_broadcast (&local->reply_cond);
  }
  pthread_mutex_unlock (&local->lock);

  return 0;
}

int 
libgf_client_fsetxattr (libglusterfs_client_ctx_t *ctx, fd_t *fd, const char *name, 
			const void *value, size_t size, int flags)
{
  /*
  call_stub_t  *stub = NULL;
  int32_t op_ret = 0;
  dict_t *dict;

  dict = get_new_dict ();

  dict_set (dict, (char *)name,
	    bin_to_data ((void *)value, size));
  dict_ref (dict);

  LIBGF_CLIENT_FOP (ctx, stub, fsetxattr, fd, dict, flags);

  op_ret = stub->args.fsetxattr_cbk.op_ret;
  errno = stub->args.fsetxattr_cbk.op_errno;

  dict_unref (dict);
  freee (stub);
  return op_ret;
  */
  return 0;
}

int 
glusterfs_fsetxattr (long fd, const char *name,
		     const void *value, size_t size, int flags)
{
  fd_t *__fd = (fd_t *)fd;
  libglusterfs_client_ctx_t *ctx = NULL;

  ctx = data_to_ptr (dict_get (__fd->ctx, GLUSTERFS_CTX_KEY));

  return libgf_client_fsetxattr (ctx, __fd, name, value, size, flags);
}

ssize_t 
glusterfs_lgetxattr (libglusterfs_client_ctx_t *ctx, const char *path, const char *name,
		     void *value, size_t size)
{
  return ENOSYS;
}

int32_t
libgf_client_fgetxattr_cbk (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno,
			   dict_t *dict)
{
  /*
  libgf_client_local_t *local = frame->local;

  local->reply_stub = fop_fgetxattr_cbk_stub (frame, NULL, op_ret, op_errno, dict);

  pthread_mutex_lock (&local->lock);
  {
    local->complete = 1;
    pthread_cond_broadcast (&local->reply_cond);
  }
  pthread_mutex_unlock (&local->lock);
  */
  return 0;
}

ssize_t 
libgf_client_fgetxattr (libglusterfs_client_ctx_t *ctx, fd_t *fd, const char *name,
			void *value, size_t size)
{
#if 0
  call_stub_t  *stub = NULL;
  int32_t op_ret = 0;

  LIBGF_CLIENT_FOP (ctx, stub, fgetxattr, fd, name, value, size);

  op_ret = stub->args.fgetxattr_cbk.op_ret;
  errno = stub->args.fgetxattr_cbk.op_errno;

  if (op_ret >= 0) {
    /*
      gf_log ("LIBGF_CLIENT", GF_LOG_DEBUG,
      "%"PRId64": %s => %d", frame->root->unique,
      state->fuse_loc.loc.path, op_ret);
    */

    data_t *value_data = dict_get (stub->args.getxattr_cbk.dict, (char *)name);
    
    if (value_data) {
      int32_t copy_len = 0;
      op_ret = value_data->len; /* Don't return the value for '\0' */

      copy_len = size < value_data->len ? size : value_data->len;
      /*FIXME: where is this freed? */
      memcpy (value, value_data->data, copy_len);
    } else {
      errno = ENODATA;
      op_ret = -1;
    }
  }

  /* FIXME: where are the contents of stub like loc are freed? */
  if (stub->args.getxattr_cbk.dict)
    dict_unref (stub->args.getxattr_cbk.dict);

  freee (stub);
#endif
  return 0;
}

ssize_t 
glusterfs_fgetxattr (long fd, const char *name,
		     void *value, size_t size)
{
  libglusterfs_client_ctx_t *ctx;
  fd_t *__fd = (fd_t *)fd;

  ctx = data_to_ptr (dict_get (__fd->ctx, GLUSTERFS_CTX_KEY));

  return libgf_client_fgetxattr (ctx, __fd, name, value, size);
}

ssize_t 
glusterfs_listxattr (libglusterfs_client_ctx_t *ctx, const char *path, char *list,
		     size_t size)
{
  return ENOSYS;
}

ssize_t 
glusterfs_llistxattr (libglusterfs_client_ctx_t *ctx, const char *path, char *list,
	       size_t size)
{
  return ENOSYS;
}

ssize_t 
glusterfs_flistxattr (libglusterfs_client_ctx_t *ctx, int filedes, char *list,
		      size_t size)
{
  return ENOSYS;
}

int 
glusterfs_removexattr (libglusterfs_client_ctx_t *ctx, const char *path, const char *name)
{
  return ENOSYS;
}

int 
glusterfs_lremovexattr (libglusterfs_client_ctx_t *ctx, const char *path, const char *name)
{
  return ENOSYS;
}

int 
glusterfs_fremovexattr (libglusterfs_client_ctx_t *ctx, int filedes, const char *name)
{
  return ENOSYS;
}

int32_t
libgf_client_readv_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			struct iovec *vector,
			int32_t count,
			struct stat *stbuf)
{
  libgf_client_local_t *local = frame->local;

  local->reply_stub = fop_readv_cbk_stub (frame, NULL, op_ret, op_errno, vector, count, stbuf);

  pthread_mutex_lock (&local->lock);
  {
    local->complete = 1;
    pthread_cond_broadcast (&local->reply_cond);
  }
  pthread_mutex_unlock (&local->lock);

  return 0;
}

int 
libgf_client_read (libglusterfs_client_ctx_t *ctx, 
		   long fd, 
		   void *buf, 
		   size_t size, 
		   off_t offset)
{
  call_stub_t *stub;
  struct iovec *vector;
  int32_t op_ret = -1;
  int count = 0;

  LIBGF_CLIENT_FOP (ctx, stub, readv, ((fd_t *)fd), size, offset);

  op_ret = stub->args.readv_cbk.op_ret;
  errno = stub->args.readv_cbk.op_errno;
  count = stub->args.readv_cbk.count;
  vector = stub->args.readv_cbk.vector;
  if (op_ret > 0) {
    int i = 0;
    op_ret = 0;
    while (size && (i < count)) {
      int len = size < vector[i].iov_len ? size : vector[i].iov_len;
      memcpy (buf, vector[i++].iov_base, len);
      buf += len;
      size -= len;
      op_ret += len;
    }
  }

  free ((char *)stub->args.readv_cbk.vector);

  if (stub->frame->root->rsp_refs && stub->args.readv_cbk.op_ret >= 0)
    dict_unref (stub->frame->root->rsp_refs);

  freee (stub);
  return op_ret;
}

ssize_t 
glusterfs_read (long fd, void *buf, size_t nbytes)
{
  int32_t op_ret = -1, offset = 0;
  data_t *offset_data;
  libglusterfs_client_ctx_t *ctx = NULL;

  if (!fd) {
    errno = EINVAL;
    return -1;
  }

  offset_data =  dict_get (((fd_t *)fd)->ctx, GLUSTERFS_OFFSET_KEY);
  offset = data_to_int32 (offset_data);

  ctx = data_to_ptr (dict_get (((fd_t *) fd)->ctx, GLUSTERFS_CTX_KEY));

  op_ret = libgf_client_read (ctx, fd, buf, nbytes, offset);

  if (op_ret > 0) {
    offset += op_ret;
    dict_set (((fd_t *)fd)->ctx, GLUSTERFS_OFFSET_KEY, data_from_int32 (offset));
  }

  return op_ret;
}

int
libgf_client_writev_cbk (call_frame_t *frame,
			 void *cookie,
			 xlator_t *this,
			 int32_t op_ret,
			 int32_t op_errno,
			 struct stat *stbuf)
{
  libgf_client_local_t *local = frame->local;

  local->reply_stub = fop_writev_cbk_stub (frame, NULL, op_ret, op_errno, stbuf);

  pthread_mutex_lock (&local->lock);
  {
    local->complete = 1;
    pthread_cond_broadcast (&local->reply_cond);
  }
  pthread_mutex_unlock (&local->lock);
  return 0;
}

int
libgf_client_writev (libglusterfs_client_ctx_t *ctx, 
		     fd_t *fd, 
		     struct iovec *vector, 
		     int count, 
		     off_t offset)
{
  call_stub_t *stub = NULL;
  int op_ret = -1;
  
  LIBGF_CLIENT_FOP (ctx, stub, writev, fd, vector, count, offset);

  op_ret = stub->args.writev_cbk.op_ret;
  errno = stub->args.writev_cbk.op_errno;

  freee (stub);
  return op_ret;
}

ssize_t 
glusterfs_write (long fd, const void *buf, size_t n)
{
  int32_t op_ret = -1, offset = 0;
  struct iovec vector;
  data_t *offset_data;

  if (!fd) {
    errno = EINVAL;
    return -1;
  }

  offset_data =  dict_get (((fd_t *)fd)->ctx, GLUSTERFS_OFFSET_KEY);

  offset = data_to_int32 (offset_data);
  vector.iov_base = (void *)buf;
  vector.iov_len = n;

  op_ret = libgf_client_writev (data_to_ptr (dict_get (((fd_t *) fd)->ctx, GLUSTERFS_CTX_KEY)), 
				(fd_t *)fd, 
				&vector, 
				1, 
				offset);

  if (op_ret >= 0) {
    offset += op_ret;
    dict_set (((fd_t *)fd)->ctx, GLUSTERFS_OFFSET_KEY, data_from_int32 (offset));
  }

  return op_ret;
}

int32_t
libgf_client_readdir_cbk (call_frame_t *frame,
			  void *cookie,
			  xlator_t *this,
			  int32_t op_ret,
			  int32_t op_errno,
			  gf_dirent_t *entries)
{
  libgf_client_local_t *local = frame->local;

  local->reply_stub = fop_readdir_cbk_stub (frame, NULL, op_ret, op_errno, entries);

  pthread_mutex_lock (&local->lock);
  {
    local->complete = 1;
    pthread_cond_broadcast (&local->reply_cond);
  }
  pthread_mutex_unlock (&local->lock);
  return 0;
}

int 
libgf_client_readdir (libglusterfs_client_ctx_t *ctx, 
		      fd_t *fd, 
		      struct dirent *dirp, 
		      size_t size, 
		      off_t offset)
{  
  call_stub_t *stub = NULL;
  int op_ret = -1;
  
  LIBGF_CLIENT_FOP (ctx, stub, readdir, fd, size, offset);

  op_ret = stub->args.readdir_cbk.op_ret;
  errno = stub->args.readdir_cbk.op_errno;

  if (op_ret > 0) {
    gf_dirent_t *entry = stub->args.readdir_cbk.entries;

    dirp->d_ino = entry->d_ino;
    /*
      #ifdef GF_DARWIN_HOST_OS
      dirp->d_off = entry->d_seekoff;
      #endif
      #ifdef GF_LINUX_HOST_OS
      dirp->d_off = entry->d_off;
      #endif
    */

    dirp->d_off = entry->d_off;
    dirp->d_type = entry->d_type;
    dirp->d_reclen = entry->d_len;
    strncpy (dirp->d_name, entry->d_name, dirp->d_reclen);
    freee (stub->args.readdir_cbk.entries);

    /*
      asprintf (&key, "%s-offset", basename (__FILE__));
      dict_set (fd->ctx, key, data_from_int32 (dirp->d_off));
      freee (key);
    */
  }

  freee (stub);

  return op_ret;
}

int
glusterfs_readdir (long fd, struct dirent *dirp, int count)
{
  int op_ret = -1;
  libglusterfs_client_ctx_t *ctx = NULL;
  int32_t offset = 0;

  ctx = data_to_ptr (dict_get (((fd_t *)fd)->ctx, GLUSTERFS_CTX_KEY));
  offset = data_to_int32 (dict_get (((fd_t *)fd)->ctx, GLUSTERFS_OFFSET_KEY));

  op_ret = libgf_client_readdir (ctx, (fd_t *)fd, dirp, sizeof (*dirp), offset);

  if (op_ret > 0) {
    offset = dirp->d_off;
    dict_set (((fd_t *) fd)->ctx, GLUSTERFS_OFFSET_KEY, data_from_int32 (offset));
  }

  return op_ret;
}

static int32_t
libglusterfs_readv_async_cbk (call_frame_t *frame,
			   void *cookie,
			   xlator_t *this,
			   int32_t op_ret,
			   int32_t op_errno,
			   struct iovec *vector,
			   int32_t count,
			   struct stat *stbuf)
{
  glusterfs_read_buf_t *buf;
  libglusterfs_client_async_local_t *local = frame->local;
  fd_t *__fd = local->fop.readv_cbk.fd;
  glusterfs_readv_cbk_t readv_cbk = local->fop.readv_cbk.cbk;

  buf = calloc (1, sizeof (*buf));
  buf->vector = iov_dup (vector, count);
  buf->count = count;
  buf->op_ret = op_ret;
  buf->op_errno = op_errno;
  buf->ref = dict_ref (frame->root->rsp_refs);

  if (op_ret > 0) {
    int32_t offset = 0;

    offset = data_to_int32 (dict_get (__fd->ctx, GLUSTERFS_OFFSET_KEY));
    offset += op_ret;
    dict_set (__fd->ctx, GLUSTERFS_OFFSET_KEY, data_from_int32 (offset));
  }

  readv_cbk (buf, local->cbk_data); 
  STACK_DESTROY (frame->root);

  return 0;
}

void 
glusterfs_free (glusterfs_read_buf_t *buf)
{
  //iov_free (buf->vector, buf->count);
  freee (buf->vector);
  dict_unref ((dict_t *) buf->ref);
  freee (buf);
}

int 
glusterfs_read_async (long fd, 
		      size_t nbytes, 
		      off_t offset,
		      glusterfs_readv_cbk_t readv_cbk,
		      void *cbk_data)
{
  libglusterfs_client_ctx_t *ctx;
  fd_t *__fd = (fd_t *)fd;
  libglusterfs_client_async_local_t *local = NULL;

  local = calloc (1, sizeof (*local));
  local->fop.readv_cbk.fd = __fd;
  local->fop.readv_cbk.cbk = readv_cbk;
  local->cbk_data = cbk_data;


  ctx = data_to_ptr (dict_get (__fd->ctx, GLUSTERFS_CTX_KEY));

  if (offset < 0) {
    offset = data_to_int32 (dict_get (__fd->ctx, GLUSTERFS_OFFSET_KEY));
  }

  LIBGF_CLIENT_FOP_ASYNC (ctx,
			  local,
			  libglusterfs_readv_async_cbk,
			  readv,
			  __fd,
			  nbytes,
			  offset);
  return 0;
}

static int32_t
libglusterfs_writev_async_cbk (call_frame_t *frame,
			    void *cookie,
			    xlator_t *this,
			    int32_t op_ret,
			    int32_t op_errno,
			    struct stat *stbuf)
{
  libglusterfs_client_async_local_t *local = frame->local;
  fd_t *fd = NULL;
  glusterfs_writev_cbk_t writev_cbk;

  writev_cbk = local->fop.writev_cbk.cbk;
  fd = local->fop.writev_cbk.fd;

  if (op_ret > 0) {
    int32_t offset = 0;

    offset = data_to_int32 (dict_get (fd->ctx, GLUSTERFS_OFFSET_KEY));
    offset += op_ret;
    dict_set (fd->ctx, GLUSTERFS_OFFSET_KEY, data_from_int32 (offset));
  }

  writev_cbk (op_ret, op_errno, local->cbk_data);

  STACK_DESTROY (frame->root);
  return 0;
}

int32_t
glusterfs_write_async (long fd, 
		       void *buf, 
		       size_t nbytes, 
		       off_t offset,
		       glusterfs_writev_cbk_t writev_cbk,
		       void *cbk_data)
{
  fd_t *__fd = (fd_t *)fd;
  struct iovec vector;
  off_t __offset = offset;
  libglusterfs_client_ctx_t *ctx = NULL;
  libglusterfs_client_async_local_t *local = NULL;

  local = calloc (1, sizeof (*local));
  local->fop.writev_cbk.fd = __fd;
  local->fop.writev_cbk.cbk = writev_cbk;
  local->cbk_data = cbk_data;

  vector.iov_base = (void *)buf;
  vector.iov_len = nbytes;
  
  ctx = data_to_ptr (dict_get (__fd->ctx, GLUSTERFS_CTX_KEY));
 
  if (offset > 0) {
    __offset = data_to_int32 (dict_get (__fd->ctx, GLUSTERFS_OFFSET_KEY));
  }

  LIBGF_CLIENT_FOP_ASYNC (ctx,
			  local,
			  libglusterfs_writev_async_cbk,
			  writev,
			  __fd,
			  &vector,
			  1,
			  __offset);

  return 0;
}

off_t
glusterfs_lseek (long fd, off_t offset, int whence)
{
  fd_t *__fd = (fd_t *)fd;
  off_t __offset = 0;
  
  switch (whence)
    {
    case SEEK_SET:
      __offset = offset;
      break;

    case SEEK_CUR:
      __offset = data_to_int32 (dict_get (__fd->ctx, GLUSTERFS_OFFSET_KEY));
      __offset += offset;
      break;

    case SEEK_END:
      /* TODO: inode has stbuf struct commented out */
      /*__offset = fd->inode->stbuf.st_size + offset; */
      break;
    }
  dict_set (__fd->ctx, GLUSTERFS_OFFSET_KEY, data_from_int32 (__offset));
  
  /* TODO: check whether this is the one to be returned */
  return __offset;
}

int32_t
libgf_client_stat_cbk (call_frame_t *frame,
		       void *cookie,
		       xlator_t *this,
		       int32_t op_ret,
		       int32_t op_errno,
		       struct stat *buf)
{
  libgf_client_local_t *local = frame->local;

  local->reply_stub = fop_stat_cbk_stub (frame, 
					 NULL, 
					 op_ret, 
					 op_errno, 
					 buf);

  pthread_mutex_lock (&local->lock);
  {
    local->complete = 1;
    pthread_cond_broadcast (&local->reply_cond);
  }
  pthread_mutex_unlock (&local->lock);

  return 0;
}

int32_t 
libgf_client_stat (libglusterfs_client_ctx_t *ctx, 
		   loc_t *loc,
		   struct stat *stbuf)
{
  call_stub_t *stub = NULL;
  data_t *stbuf_data = NULL;
  struct stat *stbuf_cache = NULL;
  int32_t op_ret = 0;
  time_t prev, current;

  current = time (NULL);
  prev = data_to_uint32 (dict_get (loc->inode->ctx, GLUSTERFS_STAT_TIME_KEY));

  if ((current - prev) <= ctx->stat_timeout) {
    stbuf_cache = data_to_ptr (dict_get (loc->inode->ctx, GLUSTERFS_STAT_CACHE_KEY));
    *stbuf = *stbuf_cache;
    return 0;
  }
    
  LIBGF_CLIENT_FOP (ctx, stub, stat, loc);
 
  op_ret = stub->args.stat_cbk.op_ret;
  errno = stub->args.stat_cbk.op_errno;
  *stbuf = stub->args.stat_cbk.buf;

  stbuf_data = dict_get (loc->inode->ctx, GLUSTERFS_STAT_CACHE_KEY);
  if (stbuf_data)
    stbuf_cache = data_to_ptr (stbuf_data);

  if (!stbuf_cache) 
    stbuf_cache = calloc (1, sizeof (*stbuf_cache));

  memcpy (stbuf_cache, stbuf, sizeof (*stbuf_cache));
  dict_set (loc->inode->ctx, GLUSTERFS_STAT_CACHE_KEY, data_from_dynptr (stbuf_cache, sizeof (*stbuf_cache)));

  current = time (NULL);
  dict_set (loc->inode->ctx, GLUSTERFS_STAT_TIME_KEY, data_from_uint32 (current));

  freee (stub);
  return op_ret;
}

int32_t  
glusterfs_stat (libglusterfs_handle_t handle, 
		const char *path, 
		struct stat *buf)
{
  int32_t op_ret = 0;
  loc_t loc;
  char lookup_required = 1;
  libglusterfs_client_ctx_t *ctx = handle;

  loc.path = strdup (path);
  loc.inode = inode_search (ctx->itable, 1, loc.path);

  if (loc.inode && loc.inode->ctx) {
    time_t current, prev;
 
    memset (&current, 0, sizeof (current));
    current = time (NULL);
    if (((int32_t)(prev = data_to_uint32(dict_get (loc.inode->ctx, GLUSTERFS_LOOKUP_KEY)))) >= 0 && ctx->lookup_timeout >= (current - prev)) {
      lookup_required = 0;
    } 
  }

  if (lookup_required) {
    op_ret = libgf_client_lookup (ctx, &loc, NULL);
  }

  if (!op_ret)
    op_ret = libgf_client_stat (ctx, &loc, buf);

  freee (loc.path);
  return op_ret;
}

static int32_t
libgf_client_fstat_cbk (call_frame_t *frame,
			void *cookie,
			xlator_t *this,
			int32_t op_ret,
			int32_t op_errno,
			struct stat *buf)
{  
  libgf_client_local_t *local = frame->local;

  local->reply_stub = fop_fstat_cbk_stub (frame, 
					  NULL, 
					  op_ret, 
					  op_errno, 
					  buf);

  pthread_mutex_lock (&local->lock);
  {
    local->complete = 1;
    pthread_cond_broadcast (&local->reply_cond);
  }
  pthread_mutex_unlock (&local->lock);

  return 0;

}

int32_t
libgf_client_fstat (libglusterfs_client_ctx_t *ctx, fd_t *fd, struct stat *buf)
{
  call_stub_t *stub = NULL;
  int32_t op_ret = 0;
  fd_t *__fd = fd;
  time_t current, prev;
  struct stat *stbuf_cache = NULL;
  data_t *stbuf_data = NULL;

  current = time (NULL);
  prev = data_to_uint32 (dict_get (__fd->inode->ctx, GLUSTERFS_STAT_TIME_KEY));

  if ((current - prev) <= ctx->stat_timeout) {
    stbuf_cache = data_to_ptr (dict_get (__fd->inode->ctx, GLUSTERFS_STAT_CACHE_KEY));
    *buf = *stbuf_cache;
    return 0;
  }

  LIBGF_CLIENT_FOP (ctx, stub, fstat, __fd);
 
  op_ret = stub->args.fstat_cbk.op_ret;
  errno = stub->args.fstat_cbk.op_errno;
  *buf = stub->args.fstat_cbk.buf;

  stbuf_data = dict_get (fd->inode->ctx, GLUSTERFS_STAT_CACHE_KEY);
  if (stbuf_data)
    stbuf_cache = data_to_ptr (stbuf_data);

  if (!stbuf_cache) 
    stbuf_cache = calloc (1, sizeof (*stbuf_cache));

  memcpy (stbuf_cache, buf, sizeof (*stbuf_cache));
  dict_set (fd->inode->ctx, GLUSTERFS_STAT_CACHE_KEY, data_from_dynptr (stbuf_cache, sizeof (*stbuf_cache)));

  current = time (NULL);
  dict_set (fd->inode->ctx, GLUSTERFS_STAT_TIME_KEY, data_from_uint32 (current));

  freee (stub);
  return op_ret;
}

int32_t 
glusterfs_fstat (long fd, struct stat *buf) 
{
  libglusterfs_client_ctx_t *ctx;
  fd_t *__fd = (fd_t *)fd;


  ctx = data_to_ptr (dict_get (__fd->ctx, GLUSTERFS_CTX_KEY));

  return libgf_client_fstat (ctx, __fd, buf);
}
 
static struct xlator_fops libgf_client_fops = {
};

static struct xlator_mops libgf_client_mops = {
};

static inline xlator_t *
libglusterfs_graph (xlator_t *graph)
{
  xlator_t *top = calloc (1, sizeof (*top));
  xlator_list_t *xlchild;

  xlchild = calloc (1, sizeof(*xlchild));
  xlchild->xlator = graph;
  top->children = xlchild;
  top->ctx = graph->ctx;
  top->next = graph;
  top->name = strdup ("libglusterfsclient");
  graph->parent = top;
  asprintf (&top->type, "libglusterfsclient");
  
  /*
    if (!(xl->fops = dlsym (handle, "fops"))) {
    gf_log ("libglusterfs/xlator",
	    GF_LOG_ERROR,
	    "dlsym(fops) on %s",
	    dlerror ());
    exit (1);
  }
  if (!(xl->mops = dlsym (handle, "mops"))) {
    gf_log ("libglusterfs/xlator",
	    GF_LOG_ERROR,
	    "dlsym(mops) on %s",
	    dlerror ());
    exit (1);
  }

  if (!(xl->init = dlsym (handle, "init"))) {
    gf_log ("libglusterfs/xlator",
	    GF_LOG_ERROR,
	    "dlsym(init) on %s",
	    dlerror ());
    exit (1);
  }

  if (!(xl->fini = dlsym (handle, "fini"))) {
    gf_log ("libglusterfs/xlator",
	    GF_LOG_ERROR,
	    "dlsym(fini) on %s",
	    dlerror ());
    exit (1);
  }

  if (!(xl->notify = dlsym (handle, "notify"))) {
    gf_log ("libglusterfs/xlator",
	    GF_LOG_DEBUG,
	    "dlsym(notify) on %s -- neglecting",
	    dlerror ());
  }
  */

  /* FIXME: Do I need to do this? for the libglusterfsclient is never going to be used as an xlator */
  top->init = libgf_client_init;
  top->fops = &libgf_client_fops;
  top->mops = &libgf_client_mops;
  top->notify = libgf_client_notify;
  top->fini = libgf_client_fini;
  //  fill_defaults (top);

  return top;
}
