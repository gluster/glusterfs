
#include "glusterfs.h"
#include "stat-prefetch.h"
#include "dict.h"
#include "xlator.h"

static int
getattr_getattr (struct xlator *xl,
		const char *path,
		struct stat *stbuf)
{
  struct getattr_private *priv = xl->private;
  
  int ret = 0;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  /* check if we have it in our list. If yes, return from here. */
  pthread_mutex_lock (&priv->mutex);
  {
    struct getattr_node *head = priv->head;
    struct getattr_node *prev = head, *next = head->next ;

    /* see if the cache is valid to this point in time */
    {
      struct timeval curr_tval;
      gettimeofday (&curr_tval, NULL);
      if (curr_tval.tv_sec > (priv->curr_tval.tv_sec 
			      + priv->timeout.tv_sec
			      + ((priv->curr_tval.tv_usec + priv->timeout.tv_usec)/1000000))){
	/* cache is invalid, free everything and coninue with regular getattr */
	gf_log ("stat-prefetch", LOG_DEBUG, "stat-prefetch.c->getattr: timeout, flushing cache");
	/* flush the getattr ahead buffer, if any exists */
	{
	  struct getattr_node *prev = NULL, *next = NULL;
	  prev = head->next;
	  while (prev){
	    next = prev->next;
	    free (prev->stbuf);
	    free (prev->pathname);
	    free (prev);
	    if (next)
	      prev = next->next;
	    else
	      prev = next;
	  }
	  /* reset the timer value */
	  head->next = NULL;
	  gettimeofday (&priv->curr_tval, NULL);
	}
      }
    }

    prev = head;
    next = head->next;
    while (next){
      if (!strcmp (next->pathname, path)){
	gf_log ("stat-prefetch", LOG_DEBUG, "stat-prefetch.c->getattr: %s found in cache\n", next->pathname);
	memcpy (stbuf, next->stbuf, sizeof (*stbuf));

      /* also remove the corresponding node from our list */
	prev->next = next->next;
	free (next->pathname);
	free (next->stbuf);
	free (next);
	pthread_mutex_unlock (&priv->mutex);
	return 0;
      }
      prev = next;
      next = next->next;
    }
  }
  pthread_mutex_unlock (&priv->mutex);

  gf_log ("stat-prefetch", LOG_DEBUG, "stat-prefetch.c->getattr: continuing with normal getattr for %s\n", path);
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->getattr (trav_xl, path, stbuf);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
getattr_readlink (struct xlator *xl,
		 const char *path,
		 char *dest,
		 size_t size)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->readlink (trav_xl, path, dest, size);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
getattr_mknod (struct xlator *xl,
	      const char *path,
	      mode_t mode,
	      dev_t dev,
	      uid_t uid,
	      gid_t gid)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->mknod (trav_xl, path, mode, dev, uid, gid);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
getattr_mkdir (struct xlator *xl,
	      const char *path,
	      mode_t mode,
	      uid_t uid,
	      gid_t gid)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->mkdir (trav_xl, path, mode, uid, gid);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
getattr_unlink (struct xlator *xl,
	       const char *path)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->unlink (trav_xl, path);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
getattr_rmdir (struct xlator *xl,
	      const char *path)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->rmdir (trav_xl, path);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}



static int
getattr_symlink (struct xlator *xl,
		const char *oldpath,
		const char *newpath,
		uid_t uid,
		gid_t gid)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->symlink (trav_xl, oldpath, newpath, uid, gid);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
getattr_rename (struct xlator *xl,
	       const char *oldpath,
	       const char *newpath,
	       uid_t uid,
	       gid_t gid)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->rename (trav_xl, oldpath, newpath, uid, gid);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
getattr_link (struct xlator *xl,
	     const char *oldpath,
	     const char *newpath,
	     uid_t uid,
	     gid_t gid)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->link (trav_xl, oldpath, newpath, uid, gid);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
getattr_chmod (struct xlator *xl,
	      const char *path,
	      mode_t mode)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->chmod (trav_xl, path, mode);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
getattr_chown (struct xlator *xl,
	      const char *path,
	      uid_t uid,
	      gid_t gid)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->chown (trav_xl, path, uid, gid);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
getattr_truncate (struct xlator *xl,
		 const char *path,
		 off_t offset)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->truncate (trav_xl, path, offset);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
getattr_utime (struct xlator *xl,
	      const char *path,
	      struct utimbuf *buf)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->utime (trav_xl, path, buf);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
getattr_open (struct xlator *xl,
	     const char *path,
	     int flags,
	     mode_t mode,
	     struct file_context *ctx)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *getattr_ctx = calloc (1, sizeof (struct file_context));
  getattr_ctx->volume = xl;
  getattr_ctx->next = ctx->next;
  ctx->next = getattr_ctx;

  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->open (trav_xl, path, flags, mode, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
getattr_read (struct xlator *xl,
	     const char *path,
	     char *buf,
	     size_t size,
	     off_t offset,
	     struct file_context *ctx)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);

  if (tmp == NULL) {
    return -1;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->read (trav_xl, path, buf, size, offset, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
getattr_write (struct xlator *xl,
	      const char *path,
	      const char *buf,
	      size_t size,
	      off_t offset,
	      struct file_context *ctx)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  
  if (tmp == NULL) {
    return -1;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->write (trav_xl, path, buf, size, offset, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
getattr_statfs (struct xlator *xl,
	       const char *path,
	       struct statvfs *buf)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->statfs (trav_xl, path, buf);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
getattr_flush (struct xlator *xl,
	      const char *path,
	      struct file_context *ctx)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);

  if (tmp == NULL) {
    return -1;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->flush (trav_xl, path, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
getattr_release (struct xlator *xl,
		const char *path,
		struct file_context *ctx)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  
  if (tmp == NULL) {
    return -1;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->release (trav_xl, path, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  RM_MY_CTX (ctx, tmp);
  free (tmp);

  return ret;
}

static int
getattr_fsync (struct xlator *xl,
	      const char *path,
	      int datasync,
	      struct file_context *ctx)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  
  if (tmp == NULL) {
    return -1;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->fsync (trav_xl, path, datasync, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;
 
  return ret;
}

static int
getattr_setxattr (struct xlator *xl,
		 const char *path,
		 const char *name,
		 const char *value,
		 size_t size,
		 int flags)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->setxattr (trav_xl, path, name, value, size, flags);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
getattr_getxattr (struct xlator *xl,
		 const char *path,
		 const char *name,
		 char *value,
		 size_t size)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->getxattr (trav_xl, path, name, value, size);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
getattr_listxattr (struct xlator *xl,
		  const char *path,
		  char *list,
		  size_t size)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->listxattr (trav_xl, path, list, size);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}
		     
static int
getattr_removexattr (struct xlator *xl,
		    const char *path,
		    const char *name)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->removexattr (trav_xl, path, name);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
getattr_opendir (struct xlator *xl,
		const char *path,
		struct file_context *ctx)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->opendir (trav_xl, path, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static char *
getattr_readdir (struct xlator *xl,
		const char *path,
		off_t offset)
{
  char *ret = NULL;
  char *buffer = NULL;
  struct getattr_private *priv = xl->private;
  struct getattr_node *prev = NULL, *head = priv->head;

  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->readdir (trav_xl, path, offset);
    trav_xl = trav_xl->next_sibling;
    buffer = ret;
  }

  /* flush the getattr ahead buffer, if any exists */
  pthread_mutex_lock (&priv->mutex);
  {
    struct getattr_node *prev = NULL, *next = NULL;
    prev = head->next;
    while (prev){
      next = prev->next;
      free (prev->stbuf);
      free (prev->pathname);
      free (prev);
      if (next)
	prev = next->next;
      else
	prev = next;
    }
    head->next = NULL;
    /* reset the timer value */
    gettimeofday (&priv->curr_tval, NULL);
  }

  /* do not continue with attr caching if readdir fails for some reason */
  if (!buffer){
    pthread_mutex_unlock (&priv->mutex);
    return buffer;
  }


  /* allocate the buffer and fill it by fetching attributes of each of the entries in the dir */
  {

    struct bulk_stat *bstbuf, *bulk_stbuf = NULL, *prev_bst = NULL;
    struct stat *stbuf = calloc (sizeof (*stbuf), 1);
    struct xlator *trav_xl = xl->first_child;
    int ret_bg = -1;
    
    bstbuf = calloc (1, sizeof (struct bulk_stat));
    prev = head;

    while (trav_xl) {
      ret_bg = trav_xl->fops->bulk_getattr (trav_xl, path, bstbuf);
      trav_xl = trav_xl->next_sibling;
      if (ret_bg >= 0)
	break;
    }

    bulk_stbuf = bstbuf->next;
    if (!bulk_stbuf){
      gf_log ("stat-prefetch", LOG_CRITICAL, "stat-prefetch.c->readdir: bulk_getattr on %s failed\n", path);
    }
    while (bulk_stbuf){
      struct getattr_node *list_node = calloc (sizeof (struct getattr_node), 1);
      /* append the stbuf to the list that we maintain */
      list_node->stbuf = bulk_stbuf->stbuf;
      /* we need the absolute pathname for this mount point */
      {
	char mount_pathname[PATH_MAX+1] = {0,};
	sprintf (mount_pathname, "%s/%s", path, bulk_stbuf->pathname);
	list_node->pathname = strdup (mount_pathname);
	gf_log ("stat-prefetch", LOG_CRITICAL, "stat-prefetch.c->readdir: %s\n", mount_pathname);
      }

      prev->next = list_node;
      prev = list_node;
      prev_bst = bulk_stbuf;
      bulk_stbuf = bulk_stbuf->next;
      
      free (prev_bst->pathname);
      free (prev_bst);
    }
  }
  pthread_mutex_unlock (&priv->mutex);

  return buffer;
}

static int
getattr_releasedir (struct xlator *xl,
		   const char *path,
		   struct file_context *ctx)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->releasedir (trav_xl, path, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
getattr_fsyncdir (struct xlator *xl,
		 const char *path,
		 int datasync,
		 struct file_context *ctx)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->fsyncdir (trav_xl, path, datasync, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}


static int
getattr_access (struct xlator *xl,
	       const char *path,
	       mode_t mode)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->access (trav_xl, path, mode);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
getattr_ftruncate (struct xlator *xl,
		  const char *path,
		  off_t offset,
		  struct file_context *ctx)
{
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);

  if (tmp == NULL) {
    return -1;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->ftruncate (trav_xl, path, offset, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
getattr_fgetattr (struct xlator *xl,
		 const char *path,
		 struct stat *buf,
		 struct file_context *ctx)
{
  
  int ret = 0;
  struct getattr_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  while (trav_xl) {
    ret = trav_xl->fops->fgetattr (trav_xl, path, buf, ctx);
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0)
      flag = ret;
  }
  ret = flag;

  return ret;
}

static int
getattr_bulk_getattr (struct xlator *xl,
		      const char *path,
		      struct bulk_stat *bstbuf)
{
  return 0;
}

static int
stat_prefetch_stats (struct xlator *xl, struct xlator_stats *stats)
{
  return 0;
}

int
init (struct xlator *xl)
{
  struct getattr_private *_private = calloc (1, sizeof (*_private));
  data_t *debug = dict_get (xl->options, "debug");
  data_t *timeout = dict_get (xl->options, "timeout");
  pthread_mutex_init (&_private->mutex, NULL);

  if (!timeout){
    gf_log ("stat-prefetch", LOG_DEBUG, 
	    "stat-prefetch.c->init: cache invalidate timeout not given,\
using default 1 millisec (1000 microsec)\n");
  }else{
    gf_log ("stat-prefetch", LOG_DEBUG, "stat-prefetch.c->init: \
using cache invalidate timeout %s microsec\n", timeout->data);
    _private->timeout.tv_usec = atol (timeout->data);
  }

  _private->head = calloc (sizeof (struct getattr_node), 1);
  
  _private->is_debug = 0;
  if (debug && (strcasecmp (debug->data, "on") == 0)) {
    _private->is_debug = 1;
    FUNCTION_CALLED;
    gf_log ("stat-prefetch", LOG_DEBUG, "stat-prefetch.c->init: debug mode on\n");
  }
  
  xl->private = (void *)_private;
  return 0;
}

void
fini (struct xlator *xl)
{
  struct getattr_private *priv = xl->private;
  free (priv);
  return;
}


struct xlator_fops fops = {
  .getattr     = getattr_getattr,
  .readlink    = getattr_readlink,
  .mknod       = getattr_mknod,
  .mkdir       = getattr_mkdir,
  .unlink      = getattr_unlink,
  .rmdir       = getattr_rmdir,
  .symlink     = getattr_symlink,
  .rename      = getattr_rename,
  .link        = getattr_link,
  .chmod       = getattr_chmod,
  .chown       = getattr_chown,
  .truncate    = getattr_truncate,
  .utime       = getattr_utime,
  .open        = getattr_open,
  .read        = getattr_read,
  .write       = getattr_write,
  .statfs      = getattr_statfs,
  .flush       = getattr_flush,
  .release     = getattr_release,
  .fsync       = getattr_fsync,
  .setxattr    = getattr_setxattr,
  .getxattr    = getattr_getxattr,
  .listxattr   = getattr_listxattr,
  .removexattr = getattr_removexattr,
  .opendir     = getattr_opendir,
  .readdir     = getattr_readdir,
  .releasedir  = getattr_releasedir,
  .fsyncdir    = getattr_fsyncdir,
  .access      = getattr_access,
  .ftruncate   = getattr_ftruncate,
  .fgetattr    = getattr_fgetattr,
  .bulk_getattr = getattr_bulk_getattr
};

struct xlator_mgmt_ops mgmt_ops = {
  .stats = stat_prefetch_stats
};
