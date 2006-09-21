#include <libgen.h>
#include <unistd.h>

#include "glusterfs.h"
#include "unify.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"

#define GF_LOCK(xl, path) do {                  \
  int acquired = -1;                            \
  while (acquired == -1) {                      \
    acquired = xl->mgmt_ops->lock (xl, path);   \
    if (acquired != 0)                          \
      usleep (1000);                            \
  }                                             \
} while (0);

#define GF_UNLOCK(xl, path) do {   \
  xl->mgmt_ops->unlock (xl, path); \
} while (0);


static char *
gcd_path (const char *path1, const char *path2)
{
  char *s1 = (char *)path1;
  char *s2 = (char *)path2;
  int diff = -1;

  while (*s1 && *s2 && (*s1 == *s2)) {
    if (*s1 == '/')
      diff = s1 - path1;
    s1++;
    s2++;
  }

  return (diff == -1) ? NULL : strndup (path1, diff + 1);
}

char *
gf_basename (char *path)
{
  char *base = basename (path);
  if (base[0] == '/' && base[1] == '\0')
    base[0] = '.';
  
  return base;
}

static int
unify_mkdir (struct xlator *xl,
	     const char *path,
	     mode_t mode,
	     uid_t uid,
	     gid_t gid)
{
  int final_ret = 0;
  int final_errno = 0;
  struct xlator *child = xl->first_child;

  GF_LOCK (xl->first_child, path);
  while (child) {
    int ret = child->fops->mkdir (child, 
				  path, 
				  mode, 
				  uid,
				  gid);
    if (ret != 0) {
      final_ret = ret;
      final_errno = 0;
    }
    child = child->next_sibling;
  }
  GF_UNLOCK (xl->first_child, path);
   
  errno = final_errno;
  return final_ret;
}


static int
unify_unlink (struct xlator *xl,
	      const char *path)
{
  int ret = 0;

  GF_LOCK (xl->first_child, path);
  {
    layout_t layout = {
      .path = (char *)path,
    };

    xl->getlayout (xl, &layout);

    if (!layout.chunk_count) {
      ret = -1;
      errno = ENOENT;
    } else {
      struct xlator *child = layout.chunks.child;
      ret = child->fops->unlink (child, 
				 path);
    }
  }
  GF_UNLOCK (xl->first_child, path);
  
  return ret;
}


static int
unify_rmdir (struct xlator *xl,
	     const char *path)
{
  int final_ret = 0;
  int final_errno = 0;
  struct xlator *child = xl->first_child;

  GF_LOCK (xl->first_child, path);
  while (child) {
    int ret = child->fops->rmdir (child, 
				  path);
    if (ret != 0) {
      final_ret = ret;
      final_errno = 0;
    }
    child = child->next_sibling;
  }
  GF_UNLOCK (xl->first_child, path);
   
  errno = final_errno;
  return final_ret;
}

static int
unify_open (struct xlator *xl,
	    const char *path,
	    int flags,
	    mode_t mode,
	    struct file_context *ctx)
{
  int ret = 0;
  struct xlator *child = NULL;

  GF_LOCK (xl->first_child, path);
  {
    layout_t layout = {
      .path = (char *) path,
    };
    xl->getlayout (xl, &layout);

    if (!layout.chunk_count) {
      /* file does not exist (yet) */
      if (flags & O_CREAT) {
	xl->setlayout (xl, &layout);
	if (layout.chunk_count) {
	  child = layout.chunks.child;
	} else {
	  //	  gf_log ("scheduler could not allot a node");
	}
      } else {
	ret = -1;
	errno = ENOENT;
      }
    } else {
      if (flags & O_EXCL) {
	ret = -1;
	errno = EEXIST;
      } else {
	child = layout.chunks.child;
      }
    }
    if (child) {
      ret = child->fops->open (child,
			      path,
			      flags,
			      mode,
			      ctx);
      if (ret >= 0) {
	struct file_context *newctx = calloc (1, sizeof (*newctx));
	newctx->context = (void *)child;
	newctx->next = ctx->next;
	newctx->volume = xl;
	ctx->next = newctx;
	/* fill ctx */
      }
    }
  }
  GF_UNLOCK (xl->first_child, path);

  gf_log ("unify", GF_LOG_ERROR, "path=%s, ret=%d\n, errno=%d\n", path, ret, errno);
  return ret;
}

static int
unify_read (struct xlator *xl,
	    const char *path,
	    char *buf,
	    size_t size,
	    off_t offset,
	    struct file_context *ctx)
{
  int ret = -1;
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  struct xlator *child = (struct xlator *)tmp->context;

  ret = child->fops->read (child, 
			   path, 
			   buf, 
			   size, 
			   offset, 
			   ctx);

  return ret;
}

static int
unify_write (struct xlator *xl,
	     const char *path,
	     const char *buf,
	     size_t size,
	     off_t offset,
	     struct file_context *ctx)
{
  int ret = -1;
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  struct xlator *child = (struct xlator *)tmp->context;

  ret = child->fops->write (child, 
			    path, 
			    buf, 
			    size, 
			    offset, 
			    ctx);

  return ret;
}

static int
unify_statfs (struct xlator *xl,
	      const char *path,
	      struct statvfs *stbuf)
{
  int ret = 0;
  struct statvfs buf = {0,};
  int final_ret = 0;
  int final_errno = 0;
  struct xlator *trav_xl = xl->first_child;

  stbuf->f_bsize = 0;
  stbuf->f_frsize = 0;
  stbuf->f_blocks = 0;
  stbuf->f_bfree = 0;
  stbuf->f_bavail = 0;
  stbuf->f_files = 0;
  stbuf->f_ffree = 0;
  stbuf->f_favail = 0;
  stbuf->f_fsid = 0;
  stbuf->f_flag = 0;
  stbuf->f_namemax = 0;
  
  while (trav_xl) {
    ret = trav_xl->fops->statfs (trav_xl, path, &buf);
    if (!ret) {
      final_ret = ret;
      final_errno = errno;

      stbuf->f_bsize = buf.f_bsize;
      stbuf->f_frsize = buf.f_frsize;
      stbuf->f_blocks += buf.f_blocks;
      stbuf->f_bfree += buf.f_bfree;
      stbuf->f_bavail += buf.f_bavail;
      stbuf->f_files += buf.f_files;
      stbuf->f_ffree += buf.f_ffree;
      stbuf->f_favail += buf.f_favail;
      stbuf->f_fsid = buf.f_fsid;
      stbuf->f_flag = buf.f_flag;
      stbuf->f_namemax = buf.f_namemax;
    }
    trav_xl = trav_xl->next_sibling;
  }
  ret = final_ret;
  errno = final_errno;

  return ret;
}


static int
unify_release (struct xlator *xl,
	       const char *path,
	       struct file_context *ctx)
{
  int ret = -1;
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  struct xlator *child = (struct xlator *)tmp->context;

  ret = child->fops->release (child, 
			      path,
			      ctx);

  RM_MY_CTX (ctx, tmp);
  free (tmp);

  return ret;
}

static int
unify_fsync (struct xlator *xl,
	     const char *path,
	     int datasync,
	     struct file_context *ctx)
{
  int ret = -1;
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  struct xlator *child = (struct xlator *)tmp->context;

  ret = child->fops->fsync (child, 
			    path,
			    datasync,
			    ctx);

  RM_MY_CTX (ctx, tmp);
  free (tmp);

  return ret;
}

char *
validate_buffer (char *buf, int names_len)
{
  int buf_len = strlen (buf);
  int remaining_buf_len = buf_len % MAX_DIR_ENTRY_STRING;
  
  if ((( buf_len != 0) || names_len >= MAX_DIR_ENTRY_STRING) && 
      (((remaining_buf_len + names_len) >= MAX_DIR_ENTRY_STRING) || 
       (remaining_buf_len == 0))) {
    int no_of_new_chunks = names_len/MAX_DIR_ENTRY_STRING + 1;
    int no_of_existing = buf_len/MAX_DIR_ENTRY_STRING + 1;
    char *new_buf = calloc (MAX_DIR_ENTRY_STRING, (no_of_new_chunks + no_of_existing));
    
    if (new_buf){
      strcat (new_buf, buf);
      
      free (buf);
      buf = new_buf;
    }
  }
  return buf;
}

static char *
unify_readdir (struct xlator *xl,
	       const char *path,
	       off_t offset)
{
  int ret = -1;
  int ns_ret = -1;
  char *buffer = calloc (1, MAX_DIR_ENTRY_STRING); //FIXME: How did I arrive at this value? (32k)

  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  unsigned int hash_value = 0;
  struct xlator *hash_xl = NULL;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *dir = strdup (path);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (path) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, path);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  ns_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (ns_ret == 0) {
    data_pair_t *data_trav = ns_dict.members;
    while (data_trav) {
      buffer = validate_buffer (buffer, strlen (data_trav->key));
      strcat (buffer, data_trav->key);
      buffer [strlen (buffer)] = '/';
      data_trav = data_trav->next;
    }
    dict_destroy (&ns_dict);
  } else {
    struct bulk_stat bulkstat = {NULL,};
    struct bulk_stat *travst;
    struct bulk_stat *prevst;
    layout_t layout;
    data_t *new_entry;
    memset (&layout, 0, sizeof (layout));

    layout.chunk_count = 1;
    /* Get all the directories from first node, files from all */  
    {
      ret = trav_xl->fops->bulk_getattr (trav_xl, path, &bulkstat);
      travst = bulkstat.next;
      while (travst) {
	layout.chunks.child = trav_xl;
	prevst = travst;
	buffer = validate_buffer (buffer, strlen (travst->pathname));
	strcat (buffer, travst->pathname);
	buffer [strlen (buffer)] = '/';
	layout.path = travst->pathname;
	layout.chunks.path = travst->pathname;
	
	if (S_ISDIR ((travst->stbuf)->st_mode))
	  layout.chunks.child = hash_xl;
	/* Update the dictionary with this entry */
	new_entry = calloc (1, sizeof (data_t));
	new_entry->data = layout_to_str (&layout);
	new_entry->len = strlen (new_entry->data);
	dict_set (&ns_dict, travst->pathname, new_entry);

	travst = travst->next;
	free (prevst->pathname);
	free (prevst->stbuf); // FIXME;
	free (prevst);
      }
      trav_xl = trav_xl->next_sibling;
    }
    bulkstat.next = NULL;
    bulkstat.stbuf = NULL;
    bulkstat.pathname = NULL;
    /* From the second child onwards, get only the files */
    while (trav_xl) {
      ret = trav_xl->fops->bulk_getattr (trav_xl, path, &bulkstat);
      if (ret >= 0) {
	layout.chunks.child = trav_xl;
	travst = bulkstat.next;
	while (travst) {
	  prevst = travst;
	  if (!S_ISDIR ((travst->stbuf)->st_mode)) {
	    buffer = validate_buffer (buffer, strlen (travst->pathname));
	    strcat (buffer, travst->pathname);
	    buffer [strlen (buffer)] = '/';
	    layout.path = travst->pathname;
	    layout.chunks.path = travst->pathname;
	    
	    /* Update the dictionary with this entry */
	    new_entry = calloc (1, sizeof (data_t));
	    new_entry->data = layout_to_str (&layout);
	    new_entry->len = strlen (new_entry->data);
	    dict_set (&ns_dict, travst->pathname, new_entry);
	  }
	  travst = travst->next;
	  free (prevst->pathname);
	  free (prevst->stbuf); // FIXME;
	  free (prevst);
	}
      }
      bulkstat.next = NULL;
      bulkstat.stbuf = NULL;
      bulkstat.pathname = NULL;
      trav_xl = trav_xl->next_sibling;
    }
    hash_xl->mgmt_ops->nsupdate (hash_xl, hash_path, &ns_dict);
    dict_destroy (&ns_dict);
  }
  free (dir);
  free (hash_path);

  return buffer;
}


static int
unify_ftruncate (struct xlator *xl,
		 const char *path,
		 off_t offset,
		 struct file_context *ctx)
{
  int ret = -1;
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  struct xlator *child = (struct xlator *)tmp->context;

  ret = child->fops->ftruncate (child, 
				path,
				offset,
				ctx);

  return ret;
}

static int
unify_fgetattr (struct xlator *xl,
		const char *path,
		struct stat *stbuf,
		struct file_context *ctx)
{
  int ret = -1;
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  struct xlator *child = (struct xlator *)tmp->context;

  ret = child->fops->fgetattr (child, 
			       path,
			       stbuf,
			       ctx);
  return ret;
}


static int
unify_getattr (struct xlator *xl,
	       const char *path,
	       struct stat *stbuf)
{
  int ret = 0;

  GF_LOCK (xl->first_child, path);
  {
    layout_t layout = {
      .path = (char *) path,
    };

    xl->getlayout (xl, &layout);

    if (!layout.chunk_count) {
      ret = -1;
      errno = ENOENT;
    } else {
      struct xlator *child = layout.chunks.child;
      ret = child->fops->getattr (child, 
				  path, 
				  stbuf);
    }
  }
  GF_UNLOCK (xl->first_child, path);

  return ret;
}


static int
unify_readlink (struct xlator *xl,
		const char *path,
		char *dest,
		size_t size)
{
  int ret = 0;
  struct xlator *child = xl->first_child;

  GF_LOCK (xl->first_child, path);
  {
    layout_t layout = {
      .path = (char *) path,
    };

    xl->getlayout (xl, &layout);

    if (!layout.chunk_count) {
      ret = -1;
      errno = ENOENT;
    } else {
      child = layout.chunks.child;
      ret = child->fops->readlink (child, 
				   path, 
				   dest,
				   size);
    }
  }
  GF_UNLOCK (xl->first_child, path);
  
  return ret;
}


static int
unify_mknod (struct xlator *xl,
	     const char *path,
	     mode_t mode,
	     dev_t dev,
	     uid_t uid,
	     gid_t gid)
{
  int ret = 0;
  struct xlator *child = NULL;

  GF_LOCK (xl->first_child, path);
  {
    layout_t layout = {
      .path = (char *) path,
    };
    gf_log ("unify", GF_LOG_ERROR,
	    "mknod layout.chunk_count=%d", layout.chunk_count);

    xl->getlayout (xl, &layout);
    gf_log ("unify", GF_LOG_ERROR,
	    "mknod layout.chunk_count=%d", layout.chunk_count);

    if (!layout.chunk_count) {
      /* file does not exist (yet) */
      xl->setlayout (xl, &layout);

      if (layout.chunk_count) {
	child = layout.chunks.child;
	ret = child->fops->mknod (child,
				  path,
				  mode,
				  dev,
				  uid,
				  gid);
      } else {
	//	gf_log ();
      }
    } else {
      ret = -1;
      errno = EEXIST;
    }
  }
  GF_UNLOCK (xl->first_child, path);

  return ret;
}

static int
unify_symlink (struct xlator *xl,
	       const char *oldpath,
	       const char *newpath,
	       uid_t uid,
	       gid_t gid)
{
  int ret = 0;
  struct xlator *child = NULL;

  GF_LOCK (xl->first_child, newpath);
  {
    layout_t new_layout = {
      .path = (char *) newpath,
    };
    xl->getlayout (xl, &new_layout);

    if (new_layout.chunk_count) {
      ret = -1;
      errno = EEXIST;
    } else {
      xl->setlayout (xl, &new_layout);
      if (new_layout.chunk_count) {
	child = new_layout.chunks.child;
	ret = child->fops->symlink (child,
				    oldpath,
				    newpath,
				    uid,
				    gid);
      } else {
	gf_log ("unify", GF_LOG_ERROR, "symlink: setlayout() returned 0 child_count");
      }
    }
  }
  GF_UNLOCK (xl->first_child, newpath);
  return ret;
}

static int
unify_rename (struct xlator *xl,
	      const char *oldpath,
	      const char *newpath,
	      uid_t uid,
	      gid_t gid)
{
  int ret = 0;
  struct xlator *child = NULL;

  char *lock_path = gcd_path (oldpath, newpath);
  GF_LOCK (xl->first_child, lock_path);
  {
    layout_t old_layout = {
      .path = (char *) oldpath,
    };
    layout_t new_layout = {
      .path = (char *) newpath,
    };
    xl->getlayout (xl, &old_layout);
    xl->getlayout (xl, &new_layout);

    if (!old_layout.chunk_count) {
      ret = -1;
      errno = ENOENT;
    } else if (new_layout.chunk_count) {
      ret = -1;
      errno = EEXIST;
    } else {
	child = old_layout.chunks.child;
	ret = child->fops->rename (child,
				   oldpath,
				   newpath,
				   uid,
				   gid);
    }
  }
  GF_UNLOCK (xl->first_child, lock_path);
  free (lock_path);

  return ret;
}

static int
unify_link (struct xlator *xl,
	    const char *oldpath,
	    const char *newpath,
	    uid_t uid,
	    gid_t gid)
{
  int ret = 0;
  struct xlator *child = NULL;

  char *lock_path = gcd_path (oldpath, newpath);
  GF_LOCK (xl->first_child, lock_path);
  {
    layout_t old_layout = {
      .path = (char *) oldpath,
    };
    layout_t new_layout = {
      .path = (char *) newpath,
    };
    xl->getlayout (xl, &old_layout);
    xl->getlayout (xl, &new_layout);

    if (!old_layout.chunk_count) {
      ret = -1;
      errno = ENOENT;
    } else if (new_layout.chunk_count) {
      ret = -1;
      errno = EEXIST;
    } else {
	child = old_layout.chunks.child;
	ret = child->fops->link (child,
				 oldpath,
				 newpath,
				 uid,
				 gid);
    }
  }
  GF_UNLOCK (xl->first_child, lock_path);
  free (lock_path);
  
  return ret;
}


static int
unify_chmod (struct xlator *xl,
	     const char *path,
	     mode_t mode)
{
  int ret = 0;

  GF_LOCK (xl->first_child, path);
  {
    layout_t layout = {
      .path = (char *) path,
    };

    xl->getlayout (xl, &layout);

    if (!layout.chunk_count) {
      ret = -1;
      errno = ENOENT;
    } else {
      struct xlator *child = layout.chunks.child;
      ret = child->fops->chmod (child, 
				path, 
				mode);
    }
  }
  GF_UNLOCK (xl->first_child, path);
  
  return ret;
}


static int
unify_chown (struct xlator *xl,
	     const char *path,
	     uid_t uid,
	     gid_t gid)
{
  int ret = 0;

  GF_LOCK (xl->first_child, path);
  {
    layout_t layout = {
      .path = (char *)path,
    };

    xl->getlayout (xl, &layout);

    if (!layout.chunk_count) {
      ret = -1;
      errno = ENOENT;
    } else {
      struct xlator *child = layout.chunks.child;
      ret = child->fops->chown (child, 
				path, 
				uid,
				gid);
    }
  }
  GF_UNLOCK (xl->first_child, path);
  
  return ret;
}


static int
unify_truncate (struct xlator *xl,
		const char *path,
		off_t offset)
{
  int ret = 0;

  GF_LOCK (xl->first_child, path);
  {
    layout_t layout = {
      .path = (char *)path,
    };

    xl->getlayout (xl, &layout);

    if (!layout.chunk_count) {
      ret = -1;
      errno = ENOENT;
    } else {
      struct xlator *child = layout.chunks.child;
      ret = child->fops->truncate (child, 
				   path, 
				   offset);
    }
  }
  GF_UNLOCK (xl->first_child, path);
  
  return ret;
}


static int
unify_utime (struct xlator *xl,
	     const char *path,
	     struct utimbuf *buf)
{
  int ret = 0;

  GF_LOCK (xl->first_child, path);
  {
    layout_t layout = {
      .path = (char *)path,
    };

    xl->getlayout (xl, &layout);

    if (!layout.chunk_count) {
      ret = -1;
      errno = ENOENT;
    } else {
      struct xlator *child = layout.chunks.child;
      ret = child->fops->utime (child, 
				path, 
				buf);
    }
  }
  GF_UNLOCK (xl->first_child, path);
  
  return ret;
}

static int
unify_flush (struct xlator *xl,
	     const char *path,
	     struct file_context *ctx)
{
  int ret = -1;
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  struct xlator *child = (struct xlator *)tmp->context;

  ret = child->fops->flush (child, 
			   path, 
			   ctx);

  return ret;
}

static int
unify_setxattr (struct xlator *xl,
		const char *path,
		const char *name,
		const char *value,
		size_t size,
		int flags)
{
  int ret = 0;

  GF_LOCK (xl->first_child, path);
  {
    layout_t layout = {
      .path = (char *) path,
    };

    xl->getlayout (xl, &layout);

    if (!layout.chunk_count) {
      ret = -1;
      errno = ENOENT;
    } else {
      struct xlator *child = layout.chunks.child;
      ret = child->fops->setxattr (child, 
				   path,
				   name,
				   value,
				   size,
				   flags);
    }
  }
  GF_UNLOCK (xl->first_child, path);
  
  return ret;
}

static int
unify_getxattr (struct xlator *xl,
		const char *path,
		const char *name,
		char *value,
		size_t size)
{
  int ret = 0;

  GF_LOCK (xl->first_child, path);
  {
    layout_t layout = {
      .path = (char *) path,
    };

    xl->getlayout (xl, &layout);

    if (!layout.chunk_count) {
      ret = -1;
      errno = ENOENT;
    } else {
      struct xlator *child = layout.chunks.child;
      ret = child->fops->getxattr (child, 
				   path,
				   name,
				   value,
				   size);
    }
  }
  GF_UNLOCK (xl->first_child, path);
  
  return ret;
}

static int
unify_listxattr (struct xlator *xl,
		  const char *path,
		  char *list,
		  size_t size)
{
  int ret = 0;

  GF_LOCK (xl->first_child, path);
  {
    layout_t layout = {
      .path = (char *)path,
    };

    xl->getlayout (xl, &layout);

    if (!layout.chunk_count) {
      ret = -1;
      errno = ENOENT;
    } else {
      struct xlator *child = layout.chunks.child;
      ret = child->fops->listxattr (child, 
				    path,
				    list,
				    size);
    }
  }
  GF_UNLOCK (xl->first_child, path);
  
  return ret;
}
		     
static int
unify_removexattr (struct xlator *xl,
		   const char *path,
		   const char *name)
{
  int ret = 0;

  GF_LOCK (xl->first_child, path);
  {
    layout_t layout = {
      .path = (char *) path,
    };

    xl->getlayout (xl, &layout);

    if (!layout.chunk_count) {
      ret = -1;
      errno = ENOENT;
    } else {
      struct xlator *child = layout.chunks.child;
      ret = child->fops->removexattr (child, 
				      path,
				      name);
    }
  }
  GF_UNLOCK (xl->first_child, path);
  
  return ret;
}

static int
unify_opendir (struct xlator *xl,
	       const char *path,
	       struct file_context *ctx)
{
  struct stat buf;
  int ret = xl->fops->getattr (xl, path, &buf);
  
  if (ret == 0) {
    if (!S_ISDIR (buf.st_mode)) {
      ret = -1;
      errno = ENOTDIR;
    }
  }
  return ret;
}

static int
unify_releasedir (struct xlator *xl,
		  const char *path,
		  struct file_context *ctx)
{
  /* This function is not implemented */
  return 0;
}

static int
unify_fsyncdir (struct xlator *xl,
		const char *path,
		int datasync,
		 struct file_context *ctx)
{
  return 0;
}


static int
unify_access (struct xlator *xl,
	      const char *path,
	      mode_t mode)
{
  return 0;
}

static int
unify_bulk_getattr (struct xlator *xl,
		    const char *path,
		    struct bulk_stat *bstbuf)
{
  /* Need to implement it if one wants to put stat-prefetch above it */
  errno = ENOSYS;
  return -1;

}

static int
unify_stats (struct xlator *xl,
	     struct xlator_stats *stats)
{
  errno = ENOSYS;
  return -1;
}

int
init (struct xlator *xl)
{
  struct cement_private *_private = calloc (1, sizeof (*_private));
  data_t *debug = dict_get (xl->options, "debug");
  data_t *scheduler = dict_get (xl->options, "scheduler");

  if (!scheduler) {
    gf_log ("unify", GF_LOG_ERROR, "unify.c->init: scheduler option is not provided\n");
    exit (1);
  }
  _private->sched_ops = get_scheduler (scheduler->data);

  _private->is_debug = 0;
  if (debug && strcasecmp (debug->data, "on") == 0) {
    _private->is_debug = 1;
    FUNCTION_CALLED;
    gf_log ("unify", GF_LOG_DEBUG, "unify.c->init: debug mode on\n");
  }
  
  /* update _private structure */
  {
    struct xlator *trav_xl = xl->first_child;
    int count = 0;
    /* Get the number of child count */
    while (trav_xl) {
      count++;
      trav_xl = trav_xl->next_sibling;
    }
    _private->child_count = count;
    _private->array = (struct xlator **)calloc (1, sizeof (struct xlator *) * count);
    count = 0;
    trav_xl = xl->first_child;
    /* Update the child array */
    while (trav_xl) {
      _private->array[count++] = trav_xl;
      trav_xl = trav_xl->next_sibling;
    }
  }

  xl->private = (void *)_private;
  _private->sched_ops->init (xl); // Initialize the schedular 
  return 0;
}

void
fini (struct xlator *xl)
{
  struct cement_private *priv = xl->private;
  priv->sched_ops->fini (xl);
  free (priv);
  return;
}

layout_t *
getlayout (struct xlator *xl,
	   layout_t *layout)
{
  struct xlator *child = xl->first_child;
  int ret;

  layout->chunk_count = 0;
  while (child) {
    struct stat stat;
    ret = child->fops->getattr (child, layout->path, &stat);
    if (!ret) {
      layout->chunk_count = 1;
      layout->chunks.path = layout->path;
      layout->chunks.path_dyn = 0;
      layout->chunks.child = child;
      layout->chunks.child_name = child->name;
      layout->chunks.child_name_dyn = 0;
      layout->chunks.next = NULL;
      break;
    }
    child = child->next_sibling;
  }
  return layout;
}

layout_t *
setlayout (struct xlator *xl,
	   layout_t *layout)
{
  struct xlator *sched_xl = NULL;
  struct cement_private *priv = xl->private;
  struct sched_ops *ops = priv->sched_ops;

  sched_xl = ops->schedule (xl, 0);

  if (sched_xl) {
    layout->chunk_count = 1;
    layout->chunks.path = layout->path;
    layout->chunks.path_dyn = 0;
    layout->chunks.child = sched_xl;
    layout->chunks.child_name = sched_xl->name;
    layout->chunks.child_name_dyn = 0;
    layout->chunks.next = NULL;
  }
  return NULL;
}

struct xlator_fops fops = {
  .getattr     = unify_getattr,
  .readlink    = unify_readlink,
  .mknod       = unify_mknod,
  .mkdir       = unify_mkdir,
  .unlink      = unify_unlink,
  .rmdir       = unify_rmdir,
  .symlink     = unify_symlink,
  .rename      = unify_rename,
  .link        = unify_link,
  .chmod       = unify_chmod,
  .chown       = unify_chown,
  .truncate    = unify_truncate,
  .utime       = unify_utime,
  .open        = unify_open,
  .read        = unify_read,
  .write       = unify_write,
  .statfs      = unify_statfs,
  .flush       = unify_flush,
  .release     = unify_release,
  .fsync       = unify_fsync,
  .setxattr    = unify_setxattr,
  .getxattr    = unify_getxattr,
  .listxattr   = unify_listxattr,
  .removexattr = unify_removexattr,
  .opendir     = unify_opendir,
  .readdir     = unify_readdir,
  .releasedir  = unify_releasedir,
  .fsyncdir    = unify_fsyncdir,
  .access      = unify_access,
  .ftruncate   = unify_ftruncate,
  .fgetattr    = unify_fgetattr,
  .bulk_getattr = unify_bulk_getattr
};

struct xlator_mgmt_ops mgmt_ops = {
  .stats = unify_stats
};
