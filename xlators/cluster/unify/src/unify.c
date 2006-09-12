#include <libgen.h>

#include "glusterfs.h"
#include "unify.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"

char *
gf_basename (char *path)
{
  char *base = basename (path);
  if (base[0] == '/' && base[1] == '\0')
    base[0] = '.';
  
  return base;
}

static int
cement_mkdir (struct xlator *xl,
	      const char *path,
	      mode_t mode,
	      uid_t uid,
	      gid_t gid)
{
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int ret = -1;
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  int lock_ret = -1;
  struct xlator *lock_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;
  data_t *new_value;
  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *lock_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  lock_path[0] = '/'; lock_path[1] = '/';
  strcpy (&lock_path[2], xl->name);
  strcat (lock_path, dir);
  hash_value = SuperFastHash (lock_path, strlen (lock_path)) % priv->child_count;
  lock_xl = priv->array[hash_value];
  //lock (lock_path);
  while (lock_ret == -1) 
    lock_ret = lock_xl->mgmt_ops->lock (lock_xl, lock_path);

  /* Do actual 'mkdir' */
  while (trav_xl) {
    child_ret = trav_xl->fops->mkdir (trav_xl, path, mode, uid, gid);
    trav_xl = trav_xl->next_sibling;
    if (child_ret >= 0)
      ret = child_ret;
  }
  // Update NameServer
  if (ret == 0) {
    int ns_ret = lock_xl->mgmt_ops->nslookup (lock_xl, lock_path, &ns_dict);
    
    //update the dict with the new entry 
    if (ns_ret == 0) {
      layout.chunk_count = 1;
      layout.path = strdup (entry_name);
      layout.chunks.path = strdup (entry_name);
      layout.chunks.child = lock_xl;
      
      /* get new 'value' */
      new_value = calloc (1, sizeof (data_t));
      new_value->data = layout_to_str (&layout);
      new_value->len = strlen (new_value->data);
      dict_set (&ns_dict, entry_name, new_value); 
      lock_xl->mgmt_ops->nsupdate (lock_xl, lock_path, &ns_dict);
      dict_destroy (&ns_dict);
    }
  }

  //unlock
  lock_xl->mgmt_ops->unlock (lock_xl, lock_path);
  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (lock_path);
  return ret;

}


static int
cement_unlink (struct xlator *xl,
	       const char *path)
{
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int ret = -1;
  int ns_ret = -1;
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  int lock_ret = -1;
  struct xlator *lock_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;
  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *lock_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  lock_path[0] = '/'; lock_path[1] = '/';
  strcpy (&lock_path[2], xl->name);
  strcat (lock_path, dir);
  hash_value = SuperFastHash (lock_path, strlen (lock_path)) % priv->child_count;
  lock_xl = priv->array[hash_value];
  //lock (lock_path);
  while (lock_ret == -1) 
    lock_ret = lock_xl->mgmt_ops->lock (lock_xl, lock_path);
  
  ns_ret = lock_xl->mgmt_ops->nslookup (lock_xl, lock_path, &ns_dict);
  if (ns_ret == 0) {
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->unlink (layout.chunks.child, path);
    }
  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->unlink (trav_xl, path);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }

  // Update NameServer
  if (ns_ret == 0) {
    // delete the entry and update the nameserver
    dict_del (&ns_dict, entry_name); 
    lock_xl->mgmt_ops->nsupdate (lock_xl, lock_path, &ns_dict);
    dict_destroy (&ns_dict);
  }

  //unlock
  lock_xl->mgmt_ops->unlock (lock_xl, lock_path);
  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (lock_path);
  return ret;
}


static int
cement_rmdir (struct xlator *xl,
	      const char *path)
{
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int ret = -1;
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  int lock_ret = -1;
  struct xlator *lock_xl = NULL;
  dict_t ns_dict = STATIC_DICT;
  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *lock_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  lock_path[0] = '/'; lock_path[1] = '/';
  strcpy (&lock_path[2], xl->name);
  strcat (lock_path, dir);
  hash_value = SuperFastHash (lock_path, strlen (lock_path)) % priv->child_count;
  lock_xl = priv->array[hash_value];
  //lock (lock_path);
  while (lock_ret == -1) 
    lock_ret = lock_xl->mgmt_ops->lock (lock_xl, lock_path);

  /* Do actual 'rmdir' */
  while (trav_xl) {
    child_ret = trav_xl->fops->rmdir (trav_xl, path);
    trav_xl = trav_xl->next_sibling;
    if (child_ret >= 0)
      ret = child_ret;
  }

  // Update NameServer
  if (ret == 0) {
    int ns_ret = lock_xl->mgmt_ops->nslookup (lock_xl, lock_path, &ns_dict);
    if (ns_ret == 0) {
      //update the dict by deleting the entry
      dict_del (&ns_dict, entry_name); 
      lock_xl->mgmt_ops->nsupdate (lock_xl, lock_path, &ns_dict);
      dict_destroy (&ns_dict);
    }
  }

  //unlock
  lock_xl->mgmt_ops->unlock (lock_xl, lock_path);
  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (lock_path);
  return ret;
}

static int
cement_open (struct xlator *xl,
	     const char *path,
	     int flags,
	     mode_t mode,
	     struct file_context *ctx)
{
  int child_ret = 0;
  int ret = -1;
  int create_flag = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *cement_ctx = calloc (1, sizeof (struct file_context));
  cement_ctx->volume = xl;
  cement_ctx->next = ctx->next;
  ctx->next = cement_ctx;
  
  if ((flags & O_CREAT) == O_CREAT)
    create_flag = 1;
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  int ns_ret = -1;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;
  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *ns_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  ns_path[0] = '/'; ns_path[1] = '/';
  strcpy (&ns_path[2], xl->name);
  strcat (ns_path, dir);
  hash_value = SuperFastHash (ns_path, strlen (ns_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  ns_ret = hash_xl->mgmt_ops->nslookup (hash_xl, ns_path, &ns_dict);

  if (create_flag) {
    struct sched_ops *sched = priv->sched_ops;
    struct xlator *sched_xl = NULL;
    data_t *new_value = calloc (1, sizeof (data_t));
    int lock_ret = -1;
    // lock_path = $ns_path;
    while (lock_ret == -1) 
      lock_ret = hash_xl->mgmt_ops->lock (hash_xl, ns_path);

    //lock (lock_path);
    sched_xl = sched->schedule (xl, 0);
    ret = sched_xl->fops->open (sched_xl, path, flags, mode, ctx);

    // Update NameServer
    //update the dict with the new entry 
    if (ns_ret == 0) {
      printf ("updated with %s\n", entry_name);
      layout.path = strdup (entry_name);
      layout.chunk_count = 1;
      layout.chunks.path = strdup (entry_name);
      layout.chunks.child = sched_xl;
      new_value->data = layout_to_str (&layout);
      new_value->len = strlen (new_value->data);
      dict_set (&ns_dict, entry_name, new_value);
      hash_xl->mgmt_ops->nsupdate (hash_xl, ns_path, &ns_dict);
      dict_destroy (&ns_dict);
    }
    
    //unlock
    hash_xl->mgmt_ops->unlock (hash_xl, ns_path);

  } else if (ns_ret == 0) {
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->open (layout.chunks.child, path, flags, mode, ctx);
    }
  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->open (trav_xl, path, flags, mode, ctx);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0)
	ret = child_ret;
    }
  }
  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (ns_path);
  return ret;
}

static int
cement_read (struct xlator *xl,
	     const char *path,
	     char *buf,
	     size_t size,
	     off_t offset,
	     struct file_context *ctx)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (child_ret == 0) {
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->read (layout.chunks.child, path, buf, size, offset, ctx);
    }
    dict_destroy (&ns_dict);
  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->read (trav_xl, path, buf, size, offset, ctx);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }

  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);
  return ret;
}

static int
cement_write (struct xlator *xl,
	      const char *path,
	      const char *buf,
	      size_t size,
	      off_t offset,
	      struct file_context *ctx)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (child_ret == 0) {
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->write (layout.chunks.child, path, buf, size, offset, ctx);
    }
    dict_destroy (&ns_dict);
  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->write (trav_xl, path, buf, size, offset, ctx);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }
  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);
  
  return ret;
}

static int
cement_statfs (struct xlator *xl,
	       const char *path,
	       struct statvfs *stbuf)
{
  int ret = 0;
  struct statvfs buf = {0,};
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  int flag = -1;
  struct xlator *trav_xl = xl->first_child;
  /* Initialize structure variable */
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
    trav_xl = trav_xl->next_sibling;
    if (ret >= 0) {
      flag = ret;
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
  }
  ret = flag;

  return ret;
}


static int
cement_release (struct xlator *xl,
		const char *path,
		struct file_context *ctx)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (child_ret == 0) {
    int lock_ret = -1;
    while (lock_ret == -1)
      lock_ret = hash_xl->mgmt_ops->lock (hash_xl, hash_path);
 
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->release (layout.chunks.child, path, ctx);
    }
    //update the dict by deleting the entry
    dict_del (&ns_dict, entry_name); 
    hash_xl->mgmt_ops->nsupdate (hash_xl, hash_path, &ns_dict);
    dict_destroy (&ns_dict);
    
    hash_xl->mgmt_ops->unlock (hash_xl, hash_path);
  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->release (trav_xl, path, ctx);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }

  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);

  if (tmp != NULL) {
    RM_MY_CTX (ctx, tmp);
    free (tmp);
  }

  return ret;
}

static int
cement_fsync (struct xlator *xl,
	      const char *path,
	      int datasync,
	      struct file_context *ctx)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (child_ret == 0) {
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->fsync (layout.chunks.child, path, datasync, ctx);
    }
    dict_destroy (&ns_dict);
  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->fsync (trav_xl, path, datasync, ctx);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }

  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);
  
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
cement_readdir (struct xlator *xl,
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
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *dir = strdup (path);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
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
  } else {
    struct bulk_stat bulkstat = {NULL,};
    struct bulk_stat *travst;
    struct bulk_stat *prevst;
    layout_t layout;
    data_t *new_entry;
    layout.path = strdup (path);
    layout.chunks.path = strdup (path);
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
cement_ftruncate (struct xlator *xl,
		  const char *path,
		  off_t offset,
		  struct file_context *ctx)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (child_ret == 0) {
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->ftruncate (layout.chunks.child, path, offset, ctx);
    }
    dict_destroy (&ns_dict);
  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->ftruncate (trav_xl, path, offset, ctx);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }
  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);
 
  return ret;
}

static int
cement_fgetattr (struct xlator *xl,
		 const char *path,
		 struct stat *stbuf,
		 struct file_context *ctx)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (child_ret == 0) {
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->fgetattr (layout.chunks.child, path, stbuf, ctx);
    }
    dict_destroy (&ns_dict);
  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->fgetattr (trav_xl, path, stbuf, ctx);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }
  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);
  
  return ret;
}


static int
cement_getattr (struct xlator *xl,
		const char *path,
		struct stat *stbuf)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (child_ret == 0) {
    /* FIXME: better go for gf_gf_basename () */
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
      errno = ENOENT;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->getattr (layout.chunks.child, path, stbuf);
    }
    dict_destroy (&ns_dict);
  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->getattr (trav_xl, path, stbuf);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }
  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);
  
  return ret;
}


static int
cement_readlink (struct xlator *xl,
		 const char *path,
		 char *dest,
		 size_t size)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (child_ret == 0) {
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
      errno = ENOENT;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->readlink (layout.chunks.child, path, dest, size);
    }
    dict_destroy (&ns_dict);
  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->readlink (trav_xl, path, dest, size);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }

  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);
  
  return ret;
}


static int
cement_mknod (struct xlator *xl,
	      const char *path,
	      mode_t mode,
	      dev_t dev,
	      uid_t uid,
	      gid_t gid)
{
  int child_ret = 0;
  int ret = -1;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  int ns_ret = -1;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;
  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *ns_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  ns_path[0] = '/'; ns_path[1] = '/';
  strcpy (&ns_path[2], xl->name);
  strcat (ns_path, dir);
  hash_value = SuperFastHash (ns_path, strlen (ns_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  ns_ret = hash_xl->mgmt_ops->nslookup (hash_xl, ns_path, &ns_dict);

  if (ns_ret == 0) {
    struct sched_ops *sched = priv->sched_ops;
    struct xlator *sched_xl = NULL;
    data_t *new_value = calloc (1, sizeof (data_t));
    int lock_ret = -1;
    // lock_path = $ns_path;
    //lock (lock_path);
    while (lock_ret == -1) 
      lock_ret = hash_xl->mgmt_ops->lock (hash_xl, ns_path);

    data_t *entry_ = dict_get (&ns_dict, entry_name);
    if (!entry_) {
      sched_xl = sched->schedule (xl, 0);
      ret = sched_xl->fops->mknod (sched_xl, path, mode, dev, uid, gid);
      // Update NameServer
      if (ret == 0) {
	//update the dict with the new entry 
	layout.path = strdup (entry_name);
	layout.chunk_count = 1;
	layout.chunks.path = strdup (entry_name);
	layout.chunks.child = sched_xl;
	new_value->data = layout_to_str (&layout);
	new_value->len = strlen (new_value->data);
	dict_set (&ns_dict, entry_name, new_value);
	hash_xl->mgmt_ops->nsupdate (hash_xl, ns_path, &ns_dict);
      }
    } else {
      ret = -1;
      errno = EEXIST;
    }
    //unlock
    hash_xl->mgmt_ops->unlock (hash_xl, ns_path);
    dict_destroy (&ns_dict);
  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->mknod (trav_xl, path, mode, dev, uid, gid);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }
  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (ns_path);
  
  return ret;
}

static int
cement_symlink (struct xlator *xl,
		const char *oldpath,
		const char *newpath,
		uid_t uid,
		gid_t gid)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (newpath);
  char *tmp_path1 = strdup (newpath);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);

  if (child_ret == 0) {
    struct sched_ops *sched = priv->sched_ops;
    struct xlator *sched_xl = NULL;
    data_t *new_value = calloc (1, sizeof (data_t));
    int lock_ret = -1;
    // lock_path = $ns_path;
    //lock (lock_path);
    while (lock_ret == -1) 
      lock_ret = hash_xl->mgmt_ops->lock (hash_xl, hash_path);

    data_t *entry_ = dict_get (&ns_dict, entry_name);
    if (!entry_) {
      sched_xl = sched->schedule (xl, 0);
      ret = sched_xl->fops->symlink (sched_xl, oldpath, newpath, uid, gid);
      // Update NameServer
      if (ret == 0) {
	//update the dict with the new entry 
	layout.path = strdup (entry_name);
	layout.chunk_count = 1;
	layout.chunks.path = strdup (entry_name);
	layout.chunks.child = sched_xl;
	new_value->data = layout_to_str (&layout);
	new_value->len = strlen (new_value->data);
	dict_set (&ns_dict, entry_name, new_value);
	hash_xl->mgmt_ops->nsupdate (hash_xl, hash_path, &ns_dict);
      }
    } else {
      ret = -1;
      errno = EEXIST;
    }
    //unlock
    hash_xl->mgmt_ops->unlock (hash_xl, hash_path);
    dict_destroy (&ns_dict);
  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->symlink (trav_xl, oldpath, newpath, uid, gid);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }
  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);

  return ret;
}

static int
cement_rename (struct xlator *xl,
	       const char *oldpath,
	       const char *newpath,
	       uid_t uid,
	       gid_t gid)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  struct xlator *hash_xl = NULL;
  struct xlator *hash_xl1 = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;
  dict_t ns_dict1 = STATIC_DICT;
  int hash_value = 0;

  /* Lock the newname */
  char *tmp_path = strdup (newpath);
  char *tmp_path1 = strdup (newpath);
  char *tmp_path2 = strdup (oldpath);
  char *tmp_path3 = strdup (oldpath);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  char *dir1 = dirname (tmp_path2);
  char *entry_name1 = gf_basename (tmp_path3);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  char *hash_path1 = calloc (1, 2 + strlen (xl->name) + strlen (dir1) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];

  hash_path1[0] = '/'; hash_path1[1] = '/';
  strcpy (&hash_path1[2], xl->name);
  strcat (hash_path1, dir1);
  hash_value = SuperFastHash (hash_path1, strlen (hash_path1)) % priv->child_count;
  hash_xl1 = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);

  if (child_ret == 0) {
    struct sched_ops *sched = priv->sched_ops;
    struct xlator *sched_xl = NULL;
    data_t *new_value = calloc (1, sizeof (data_t));
    int lock_ret = -1;
    // lock_path = $ns_path;
    //lock (lock_path);
    while (lock_ret == -1) 
      lock_ret = hash_xl->mgmt_ops->lock (hash_xl, hash_path);

    while (lock_ret == -1)
      lock_ret = hash_xl1->mgmt_ops->lock (hash_xl1, hash_path1);

    sched_xl = sched->schedule (xl, 0);
    ret = sched_xl->fops->rename (sched_xl, oldpath, newpath, uid, gid);
    // Update NameServer
    if (ret == 0) {
      //update the dict with the new entry 
      layout.path = strdup (entry_name);
      layout.chunk_count = 1;
      layout.chunks.path = strdup (entry_name);
      layout.chunks.child = sched_xl;
      new_value->data = layout_to_str (&layout);
      new_value->len = strlen (new_value->data);
      dict_set (&ns_dict, entry_name, new_value);
      hash_xl->mgmt_ops->nsupdate (hash_xl, hash_path, &ns_dict);
      dict_destroy (&ns_dict);

      ret = hash_xl1->mgmt_ops->nslookup (hash_xl1, hash_path1, &ns_dict1);
      if (ret == 0) {
	dict_del (&ns_dict, entry_name1);
	hash_xl1->mgmt_ops->nsupdate (hash_xl1, hash_path1, &ns_dict1);
	dict_destroy (&ns_dict1);
      }
    }
    //unlock
    hash_xl->mgmt_ops->unlock (hash_xl, hash_path);
    hash_xl1->mgmt_ops->unlock (hash_xl1, hash_path1);
  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->rename (trav_xl, oldpath, newpath, uid, gid);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }
  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (tmp_path2);
  free (tmp_path3);
  free (hash_path);
  free (hash_path1);

  return ret;
}

static int
cement_link (struct xlator *xl,
	     const char *oldpath,
	     const char *newpath,
	     uid_t uid,
	     gid_t gid)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (newpath);
  char *tmp_path1 = strdup (newpath);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);

  if (child_ret == 0) {
    struct sched_ops *sched = priv->sched_ops;
    struct xlator *sched_xl = NULL;
    data_t *new_value = calloc (1, sizeof (data_t));
    int lock_ret = -1;
    // lock_path = $ns_path;
    //lock (lock_path);
    while (lock_ret == -1) 
      lock_ret = hash_xl->mgmt_ops->lock (hash_xl, hash_path);

    data_t *entry_ = dict_get (&ns_dict, entry_name);
    if (!entry_) {
      sched_xl = sched->schedule (xl, 0);
      ret = sched_xl->fops->link (sched_xl, oldpath, newpath, uid, gid);
      // Update NameServer
      if (ret == 0) {
	//update the dict with the new entry 
	layout.path = strdup (entry_name);
	layout.chunk_count = 1;
	layout.chunks.path = strdup (entry_name);
	layout.chunks.child = sched_xl;
	new_value->data = layout_to_str (&layout);
	new_value->len = strlen (new_value->data);
	dict_set (&ns_dict, entry_name, new_value);
	hash_xl->mgmt_ops->nsupdate (hash_xl, hash_path, &ns_dict);
      }
    } else {
      ret = -1;
      errno = ENOENT;
    }
    //unlock
    hash_xl->mgmt_ops->unlock (hash_xl, hash_path);
    dict_destroy (&ns_dict);
  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->link (trav_xl, oldpath, newpath, uid, gid);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }
  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);
  
  return ret;
}


static int
cement_chmod (struct xlator *xl,
	      const char *path,
	      mode_t mode)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (child_ret == 0) {
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
      errno = ENOENT;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->chmod (layout.chunks.child, path, mode);
    }
    dict_destroy (&ns_dict);
  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->chmod (trav_xl, path, mode);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }

  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);
  
  return ret;
}


static int
cement_chown (struct xlator *xl,
	      const char *path,
	      uid_t uid,
	      gid_t gid)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (child_ret == 0) {
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
      errno = ENOENT;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->chown (layout.chunks.child, path, uid, gid);
    }
    dict_destroy (&ns_dict);
  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->chown (trav_xl, path, uid, gid);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }
  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);
  
  return ret;
}


static int
cement_truncate (struct xlator *xl,
		 const char *path,
		 off_t offset)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (child_ret == 0) {
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
      errno = ENOENT;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->truncate (layout.chunks.child, path, offset);
    }
    dict_destroy (&ns_dict);

  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->truncate (trav_xl, path, offset);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }
  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);
  
  return ret;
}


static int
cement_utime (struct xlator *xl,
	      const char *path,
	      struct utimbuf *buf)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (child_ret == 0) {
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
      errno = ENOENT;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->utime (layout.chunks.child, path, buf);
    }
    dict_destroy (&ns_dict);

  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->utime (trav_xl, path, buf);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }

  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);
  
  return ret;
}

static int
cement_flush (struct xlator *xl,
	      const char *path,
	      struct file_context *ctx)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct file_context *tmp;
  FILL_MY_CTX (tmp, ctx, xl);
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (child_ret == 0) {
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
      errno = ENOENT;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->flush (layout.chunks.child, path, ctx);
    }
    dict_destroy (&ns_dict);

  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->flush (trav_xl, path, ctx);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }
  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);
  
  return ret;
}

static int
cement_setxattr (struct xlator *xl,
		 const char *path,
		 const char *name,
		 const char *value,
		 size_t size,
		 int flags)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (child_ret == 0) {
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
      errno = ENOENT;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->setxattr (layout.chunks.child, 
						 path, name, value, size, flags);
    }
    dict_destroy (&ns_dict);

  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->setxattr (trav_xl, path, name, value, size, flags);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }

  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);
  
  return ret;
}

static int
cement_getxattr (struct xlator *xl,
		 const char *path,
		 const char *name,
		 char *value,
		 size_t size)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (child_ret == 0) {
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
      errno = ENOENT;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->getxattr (layout.chunks.child, path, name, value, size);
    }
    dict_destroy (&ns_dict);

  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->getxattr (trav_xl, path, name, value, size);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }
  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);
  
  return ret;
}

static int
cement_listxattr (struct xlator *xl,
		  const char *path,
		  char *list,
		  size_t size)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (child_ret == 0) {
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
      errno = ENOENT;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->listxattr (layout.chunks.child, path, list, size);
    }
    dict_destroy (&ns_dict);

  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->listxattr (trav_xl, path, list, size);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }

  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);
  
  return ret;
}
		     
static int
cement_removexattr (struct xlator *xl,
		    const char *path,
		    const char *name)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (child_ret == 0) {
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
      errno = ENOENT;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->removexattr (layout.chunks.child, path, name);
    }
    dict_destroy (&ns_dict);
  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->removexattr (trav_xl, path, name);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }
  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);
  
  return ret;
}

static int
cement_opendir (struct xlator *xl,
		const char *path,
		struct file_context *ctx)
{
  int ret = -1;
  int child_ret = 0;
  struct cement_private *priv = xl->private;
  if (priv->is_debug) {
    FUNCTION_CALLED;
  }
  struct xlator *trav_xl = xl->first_child;
  int hash_value = 0;
  struct xlator *hash_xl = NULL;
  layout_t layout;
  dict_t ns_dict = STATIC_DICT;

  /* Lock the name */
  char *tmp_path = strdup (path);
  char *tmp_path1 = strdup (path);
  char *dir = dirname (tmp_path);
  char *entry_name = gf_basename (tmp_path1);
  // lock_path = "//$xl->name/$dir"
  char *hash_path = calloc (1, 2 + strlen (xl->name) + strlen (dir) + 2);
  hash_path[0] = '/'; hash_path[1] = '/';
  strcpy (&hash_path[2], xl->name);
  strcat (hash_path, dir);
  hash_value = SuperFastHash (hash_path, strlen (hash_path)) % priv->child_count;
  hash_xl = priv->array[hash_value];
  
  child_ret = hash_xl->mgmt_ops->nslookup (hash_xl, hash_path, &ns_dict);
  if (child_ret == 0) {
    data_t *_entry = dict_get (&ns_dict, entry_name);
    if (!_entry) {
      ret = -1;
      errno = ENOENT;
    } else {
      str_to_layout (data_to_str (_entry), &layout);
      layout_setchildren (&layout, xl);
      ret = layout.chunks.child->fops->opendir (layout.chunks.child, path, ctx);
    }
    dict_destroy (&ns_dict);

  } else {
    while (trav_xl) {
      child_ret = trav_xl->fops->opendir (trav_xl, path, ctx);
      trav_xl = trav_xl->next_sibling;
      if (child_ret >= 0) {
	ret = child_ret;
	break;
      }
    }
  }

  free (tmp_path); //strdup'ed
  free (tmp_path1);
  free (hash_path);
  
  return ret;
}

static int
cement_releasedir (struct xlator *xl,
		   const char *path,
		   struct file_context *ctx)
{
  /* This function is not implemented */
  return 0;
}

static int
cement_fsyncdir (struct xlator *xl,
		 const char *path,
		 int datasync,
		 struct file_context *ctx)
{
  return 0;
}


static int
cement_access (struct xlator *xl,
	       const char *path,
	       mode_t mode)
{
  return 0;
}

static int
cement_bulk_getattr (struct xlator *xl,
		     const char *path,
		     struct bulk_stat *bstbuf)
{
  return 0;
}

static int
cement_stats (struct xlator *xl,
	      struct xlator_stats *stats)
{
  return 0;
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
    gf_log ("unify", GF_LOG_ERROR, "unify.c->init: debug mode on\n");
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

struct xlator_fops fops = {
  .getattr     = cement_getattr,
  .readlink    = cement_readlink,
  .mknod       = cement_mknod,
  .mkdir       = cement_mkdir,
  .unlink      = cement_unlink,
  .rmdir       = cement_rmdir,
  .symlink     = cement_symlink,
  .rename      = cement_rename,
  .link        = cement_link,
  .chmod       = cement_chmod,
  .chown       = cement_chown,
  .truncate    = cement_truncate,
  .utime       = cement_utime,
  .open        = cement_open,
  .read        = cement_read,
  .write       = cement_write,
  .statfs      = cement_statfs,
  .flush       = cement_flush,
  .release     = cement_release,
  .fsync       = cement_fsync,
  .setxattr    = cement_setxattr,
  .getxattr    = cement_getxattr,
  .listxattr   = cement_listxattr,
  .removexattr = cement_removexattr,
  .opendir     = cement_opendir,
  .readdir     = cement_readdir,
  .releasedir  = cement_releasedir,
  .fsyncdir    = cement_fsyncdir,
  .access      = cement_access,
  .ftruncate   = cement_ftruncate,
  .fgetattr    = cement_fgetattr,
  .bulk_getattr = cement_bulk_getattr
};

struct xlator_mgmt_ops mgmt_ops = {
  .stats = cement_stats
};
