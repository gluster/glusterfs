/*
   Copyright (c) 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "glusterfs-guts.h"
#include "guts-parse.h"
#include <signal.h>
#include "guts-tables.h"
#include "guts-replay.h"
#include "guts-trace.h"

static void
convert_attr (const struct fuse_setattr_in *attr,
	      struct stat *stbuf)
{
  stbuf->st_mode      = attr->mode;
  stbuf->st_uid       = attr->uid;
  stbuf->st_gid       = attr->gid;
  stbuf->st_size      = attr->size;
  stbuf->st_atime     = attr->atime;
  /* 
  ST_ATIM_NSEC_SET (stbuf, attr->atimensec);
  ST_MTIM_NSEC_SET (stbuf, attr->mtimensec);*/
}

static void
guts_replay_lookup (fuse_req_t req,
		 fuse_ino_t ino,
		 const void *inargs)
{
  char *name = (char *) inargs;

  if (req->f->op.lookup)
    req->f->op.lookup(req, ino, name);
  else
    guts_reply_err (req, ENOSYS);

}

static void
guts_replay_forget (fuse_req_t req,
		 fuse_ino_t ino,
		 const void *inargs)
{
  struct fuse_forget_in *arg = (struct fuse_forget_in *) inargs;
  
  if (req->f->op.forget)
    req->f->op.forget (req, ino, arg->nlookup);

}

static void
guts_replay_getattr (fuse_req_t req,
		  fuse_ino_t ino,
		  const void *inargs)
{
  (void) inargs;
  
  if (req->f->op.getattr)
    req->f->op.getattr (req, ino, NULL);
  else 
    guts_reply_err (req, ENOSYS);
}

static void
guts_replay_setattr (fuse_req_t req,
		  fuse_ino_t ino,
		  const void *inargs)
{
  struct fuse_setattr_in *arg = (struct fuse_setattr_in *)inargs;
  
  if (req->f->op.setattr) {
    struct fuse_file_info *fi = NULL;
    struct fuse_file_info fi_store;
    struct stat stbuf;
    memset (&stbuf, 0, sizeof (stbuf));
    convert_attr (arg, &stbuf);
    if (arg->valid & FATTR_FH) {
      arg->valid &= ~FATTR_FH;
      memset (&fi_store, 0, sizeof (fi_store));
      fi = &fi_store;
      fi->fh = arg->fh;
      fi->fh_old = fi->fh;
    }
    req->f->op.setattr (req, ino, &stbuf, arg->valid, fi);
  } else 
    guts_reply_err (req, ENOSYS);
}

static void
guts_replay_access (fuse_req_t req,
		 fuse_ino_t ino,
		 const void *inargs)
{
  struct fuse_access_in *arg = (struct fuse_access_in *)inargs;

  if (req->f->op.access)
    req->f->op.access (req, ino, arg->mask);
  else 
    guts_reply_err (req, ENOSYS);
}

static void
guts_replay_readlink (fuse_req_t req,
		   fuse_ino_t ino,
		   const void *inargs)
{
  (void) inargs;
  
  if (req->f->op.readlink)
    req->f->op.readlink (req, ino);
  else
    guts_reply_err (req, ENOSYS);
}


static void
guts_replay_mknod (fuse_req_t req,
		fuse_ino_t ino,
		const void *inargs)
{
  struct fuse_mknod_in *arg = (struct fuse_mknod_in *) inargs;

  if (req->f->op.mknod)
    req->f->op.mknod (req, ino, PARAM(arg), arg->mode, arg->rdev);
  else 
    guts_reply_err (req, ENOSYS);
}

static void
guts_replay_mkdir (fuse_req_t req,
		fuse_ino_t ino,
		const void *inargs)
{
  struct fuse_mkdir_in *arg = (struct fuse_mkdir_in *) inargs;

  if (req->f->op.mkdir)
    req->f->op.mkdir (req, ino, PARAM(arg), arg->mode);
  else 
    guts_reply_err (req, ENOSYS);
}

static void
guts_replay_unlink (fuse_req_t req,
		 fuse_ino_t ino,
		 const void *inargs)
{
  char *name = (char *)inargs;
  
  if (req->f->op.unlink) {

    req->f->op.unlink (req, ino, name);
  } else
    guts_reply_err (req, ENOSYS);
}

static void
guts_replay_rmdir (fuse_req_t req,
		fuse_ino_t ino,
		const void *inargs)
{
  char *name = (char *)inargs;
  
  if (req->f->op.rmdir) {
    req->f->op.rmdir (req, ino, name);
  } else
    guts_reply_err (req, ENOSYS);
}

static void
guts_replay_symlink (fuse_req_t req,
		  fuse_ino_t ino,
		  const void *inargs)
{
  char *name = (char *) inargs;
  char *linkname = ((char *) inargs) + strlen ((char *) inargs) + 1;

  if (req->f->op.symlink) {
    req->f->op.symlink (req, linkname, ino, name);
  } else 
    guts_reply_err (req, ENOSYS);
}



static void
guts_replay_rename (fuse_req_t req,
		 fuse_ino_t ino,
		 const void *inargs)
{
  struct fuse_rename_in *arg = (struct fuse_rename_in *) inargs;
  char *oldname = PARAM(arg);
  char *newname = oldname + strlen (oldname) + 1;

  if (req->f->op.rename) {
    req->f->op.rename (req, ino, oldname, arg->newdir, newname);
  } else 
    guts_reply_err (req, ENOSYS);
  
}

static void
guts_replay_link (fuse_req_t req,
		  fuse_ino_t ino,
		  const void *inargs)
{
  struct fuse_link_in *arg = (struct fuse_link_in *) inargs;

  if (req->f->op.link) {
    guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;
    fuse_ino_t old_ino = guts_inode_search (ctx, arg->oldnodeid);

    req->f->op.link (req, old_ino, ino, PARAM(arg));
  } else
    guts_reply_err (req, ENOSYS);
}


static void
guts_replay_create (fuse_req_t req,
		    fuse_ino_t ino,
		    const void *inargs)
{
  struct guts_create_in *arg = (struct guts_create_in *) inargs;
  
  if (req->f->op.create) {
    struct fuse_file_info fi;
    memset (&fi, 0, sizeof (fi));
    fi.flags = arg->open_in.flags;
    
    req->f->op.create (req, ino, arg->name, arg->open_in.mode, &fi);
  } else
    guts_reply_err (req, ENOSYS);

}

static void
guts_replay_open (fuse_req_t req,
	       fuse_ino_t ino,
	       const void *inargs)
{
  struct fuse_open_in *arg = (struct fuse_open_in *) inargs;
  struct fuse_file_info fi;
  
  memset (&fi, 0, sizeof (fi));
  fi.flags = arg->flags;
  
  if (req->f->op.open) {
    /* TODO: how efficient is using dict_get here?? */
    req->f->op.open (req, ino, &fi);
  }  else
    guts_reply_open (req, &fi);
}


static void 
guts_replay_read(fuse_req_t req,
	      fuse_ino_t ino,
	      const void *inarg)
{
  struct fuse_read_in *arg = (struct fuse_read_in *) inarg;

  if (req->f->op.read){
    struct fuse_file_info fi;
    guts_replay_ctx_t *ctx = req->u.ni.data;

    memset (&fi, 0, sizeof (fi));
    /* TODO: how efficient is using dict_get here?? */
    fi.fh = (unsigned long) guts_fd_search (ctx, arg->fh);
    if (!fi.fh) {
      /* TODO: make it more meaningful and organized */
      printf ("readv called without opening the file\n");
      guts_reply_err (req, EBADFD);
    } else {
      fi.fh_old = fi.fh;
      req->f->op.read (req, ino, arg->size, arg->offset, &fi);
    }
  } else
    guts_reply_err (req, ENOSYS);
}

static void
guts_replay_write(fuse_req_t req,
	       fuse_ino_t ino,
	       const void *inarg)
{
  struct fuse_write_in *arg = (struct fuse_write_in *) inarg;
  struct fuse_file_info fi;
  guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;

  memset (&fi, 0, sizeof (fi));
  fi.fh = (unsigned long) guts_fd_search (ctx, arg->fh);

  if (!fi.fh) {
    /* TODO: make it more meaningful and organized */
    printf ("writev called without opening the file\n");
    guts_reply_err (req, EBADFD);
  } else {
    fi.fh_old = fi.fh;
    fi.writepage = arg->write_flags & 1;
    if (req->f->op.write)
      req->f->op.write (req, ino, PARAM(arg), arg->size, arg->offset, &fi);
    else
      guts_reply_err (req, ENOSYS);
  }
}

static void
guts_replay_flush(fuse_req_t req,
	       fuse_ino_t ino,
	       const void *inarg)
{
  struct fuse_flush_in *arg = (struct fuse_flush_in *) inarg;
  struct fuse_file_info fi;
  guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;

  memset (&fi, 0, sizeof (fi));
  fi.fh = (unsigned long) guts_fd_search (ctx, arg->fh);
  if (!fi.fh) {
    printf ("flush called without calling open\n");
    guts_reply_err (req, EBADFD);
  } else {
    fi.fh_old = fi.fh;
    fi.flush = 1;
    
    if (req->f->conn.proto_minor >= 7)
      fi.lock_owner = arg->lock_owner;
    
    if (req->f->op.flush)
      req->f->op.flush (req, ino, &fi);
    else
      guts_reply_err (req, ENOSYS);
  }
}

static void
guts_replay_release(fuse_req_t req,
		 fuse_ino_t ino,
		 const void *inarg)
{
  struct fuse_release_in *arg = (struct fuse_release_in *) inarg;
  struct fuse_file_info fi;
  guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;

  memset (&fi, 0, sizeof (fi));
  fi.flags = arg->flags;
  fi.fh = (unsigned long) guts_fd_search (ctx, arg->fh);
  
  if (!fi.fh) {
    printf ("release called without calling open\n");
    guts_reply_err (req, EBADFD);
  } else {
    fi.fh_old = fi.fh;
    if (req->f->conn.proto_minor >= 8) {
      fi.flush = (arg->release_flags & FUSE_RELEASE_FLUSH) ? 1 : 0;
      fi.lock_owner = arg->lock_owner;
    }
    if (req->f->op.release)
      req->f->op.release (req, ino, &fi);
    else
      guts_reply_err (req, ENOSYS);
  }
}

static void
guts_replay_fsync(fuse_req_t req,
	       fuse_ino_t ino,
	       const void *inarg)
{
  struct fuse_fsync_in *arg = (struct fuse_fsync_in *) inarg;
  struct fuse_file_info fi;
  guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;

  memset (&fi, 0, sizeof (fi));
  fi.fh = (unsigned long) guts_fd_search (ctx, arg->fh);
  fi.fh_old = fi.fh;

  if (req->f->op.fsync)
    req->f->op.fsync (req, ino, arg->fsync_flags & 1, &fi);
  else
    guts_reply_err (req, ENOSYS);
}

static void
guts_replay_opendir (fuse_req_t req,
		  fuse_ino_t ino,
		  const void *inarg)
{
  struct fuse_open_in *arg = (struct fuse_open_in *) inarg;
  struct fuse_file_info fi;
  
  memset (&fi, 0, sizeof (fi));
  fi.flags = arg->flags;

  if (req->f->op.opendir) {
    req->f->op.opendir (req, ino, &fi);
  } else
    guts_reply_open (req, &fi);
}

static void
guts_replay_readdir(fuse_req_t req,
		 fuse_ino_t ino,
		 const void *inarg)
{
  struct fuse_read_in *arg = (struct fuse_read_in *) inarg;
  struct fuse_file_info fi;
  guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;

  memset (&fi, 0, sizeof (fi));
  fi.fh = (unsigned long) guts_fd_search (ctx, arg->fh);

  if (!fi.fh) {
    /* TODO: make it more meaningful and organized */
    printf ("readdir called without opening the file\n");
    guts_reply_err (req, EBADFD);
  } else {
    fi.fh_old = fi.fh;
    
    if (req->f->op.readdir)
      req->f->op.readdir (req, ino, arg->size, arg->offset, &fi);
    else
      guts_reply_err (req, ENOSYS);
  }

}

static void
guts_replay_releasedir(fuse_req_t req,
		    fuse_ino_t ino,
		    const void *inarg)
{
  struct fuse_release_in *arg = (struct fuse_release_in *) inarg;
  struct fuse_file_info fi;
  guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;
  
  memset (&fi, 0, sizeof (fi));
  fi.flags = arg->flags;
  fi.fh = (unsigned long) guts_fd_search (ctx, arg->fh);
  if (!fi.fh) {
    printf ("releasedir called without calling opendir\n");
    guts_reply_err (req, EBADFD);
  } else {

    fi.fh_old = fi.fh;
    if (req->f->op.releasedir)
      req->f->op.releasedir (req, ino, &fi);
    else
      guts_reply_err (req, ENOSYS);
  }
}

static void
guts_replay_fsyncdir(fuse_req_t req,
		  fuse_ino_t ino,
		  const void *inarg)
{
  struct fuse_fsync_in *arg = (struct fuse_fsync_in *) inarg;
  struct fuse_file_info fi;
  guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;

  memset (&fi, 0, sizeof (fi));
  fi.fh = (unsigned long) guts_fd_search (ctx, arg->fh);
  fi.fh_old = fi.fh;
  
  if (req->f->op.fsyncdir)
    req->f->op.fsyncdir (req, ino, arg->fsync_flags & 1, &fi);
  else
    guts_reply_err (req, ENOSYS);
}

static void
guts_replay_statfs (fuse_req_t req,
		 fuse_ino_t ino,
		 const void *inargs)
{
  (void) ino;
  (void) inargs;
  
  if (req->f->op.statfs) {
    req->f->op.statfs (req, ino);
  } else {
    struct statvfs buf = {
      .f_namemax = 255,
      .f_bsize   = 512,
    };
    guts_reply_statfs (req, &buf);
  }
}

static void
guts_replay_setxattr(fuse_req_t req,
		  fuse_ino_t ino,
		  const void *inarg)
{
  struct fuse_setxattr_in *arg = (struct fuse_setxattr_in *) inarg;
  char *name = PARAM(arg);
  char *value = name + strlen(name) + 1;
  
  if (req->f->op.setxattr)
    req->f->op.setxattr (req, ino, name, value, arg->size, arg->flags);
  else
    guts_reply_err (req, ENOSYS);
}

static void
guts_replay_getxattr(fuse_req_t req,
		  fuse_ino_t ino,
		  const void *inarg)
{
  struct fuse_getxattr_in *arg = (struct fuse_getxattr_in *) inarg;
  
  if (req->f->op.getxattr)
    req->f->op.getxattr (req, ino, PARAM(arg), arg->size);
  else
    guts_reply_err (req, ENOSYS);
}

static void
guts_replay_listxattr (fuse_req_t req,
		    fuse_ino_t ino,
		    const void *inargs)
{
  struct fuse_getxattr_in *arg = (struct fuse_getxattr_in *) inargs;
  
  if (req->f->op.listxattr)
    req->f->op.listxattr (req, ino, arg->size);
  else
    guts_reply_err (req, ENOSYS);
}

static void
guts_replay_removexattr(fuse_req_t req,
		     fuse_ino_t ino,
		     const void *inargs)
{
  char *name = (char *)inargs;
  
  if (req->f->op.removexattr)
    req->f->op.removexattr (req, ino, name);
  else
    guts_reply_err (req, ENOSYS);
}

guts_replay_t guts_replay_fop[] = {
  [FUSE_LOOKUP] = { guts_replay_lookup, "lookup" },
  [FUSE_FORGET] = { guts_replay_forget, "forget" },
  [FUSE_GETATTR] = { guts_replay_getattr, "getattr" },
  [FUSE_SETATTR] = { guts_replay_setattr, "setattr" },
  [FUSE_ACCESS] = { guts_replay_access, "access" },
  [FUSE_READLINK] = { guts_replay_readlink, "readlink" },
  [FUSE_MKNOD] = { guts_replay_mknod, "mknod" },
  [FUSE_MKDIR] = { guts_replay_mkdir, "mkdir" },
  [FUSE_UNLINK] = { guts_replay_unlink, "unlink" },
  [FUSE_RMDIR] = { guts_replay_rmdir, "rmdir" },
  [FUSE_SYMLINK] = { guts_replay_symlink, "symlink" },
  [FUSE_RENAME] = { guts_replay_rename, "rename" },
  [FUSE_LINK] = { guts_replay_link, "link" },
  [FUSE_CREATE] = { guts_replay_create, "create" },
  [FUSE_OPEN] = { guts_replay_open, "open" },
  [FUSE_READ] = { guts_replay_read, "read" },
  [FUSE_WRITE] = { guts_replay_write, "write" },
  [FUSE_FLUSH] = { guts_replay_flush, "flush" },
  [FUSE_RELEASE] = { guts_replay_release, "release" },
  [FUSE_FSYNC] = { guts_replay_fsync, "fsync" },
  [FUSE_OPENDIR] = { guts_replay_opendir, "opendir" },
  [FUSE_READDIR] = { guts_replay_readdir, "readdir" },
  [FUSE_RELEASEDIR] = { guts_replay_releasedir, "releasedir" },
  [FUSE_FSYNCDIR] = { guts_replay_fsyncdir, "fsyncdir" },
  [FUSE_STATFS] = { guts_replay_statfs, "statfs" },
  [FUSE_SETXATTR] = { guts_replay_setxattr, "setxattr" },
  [FUSE_GETXATTR] = { guts_replay_getxattr, "getxattr" },
  [FUSE_LISTXATTR] = { guts_replay_listxattr, "listxattr" },
  [FUSE_REMOVEXATTR] = { guts_replay_removexattr, "removexattr" },
};

static inline void 
list_init_req (struct fuse_req *req)
{
  req->next = req;
  req->prev = req;
}


static int32_t
guts_transport_notify (xlator_t *xl,
		       int32_t event,
		       void *data,
		       ...)
{
  /* dummy, nobody has got anything to notify me.. ;) */
  return 0;
}

static int32_t
guts_transport_init (transport_t *this,
		     dict_t *options,
		     event_notify_fn_t notify)
{
  struct fuse_private *priv = CALLOC (1, sizeof (*priv));
  ERR_ABORT (priv);
  
  this->notify = NULL;
  this->private = (void *)priv;
  
  /* fuse channel */
  priv->ch = NULL;
  
  /* fuse session */
  priv->se = NULL;
  
  /* fuse channel fd */
  priv->fd = -1;
  
  this->buf = data_ref (data_from_dynptr (NULL, 0));
  this->buf->is_locked = 1;
  
  priv->mountpoint = NULL;
  
  transport_ref (this);
  
  return 0;
}

static void
guts_transport_fini (transport_t *this)
{

}

static int32_t
guts_transport_disconnect (transport_t *this)
{
  struct fuse_private *priv = this->private;

  gf_log ("glusterfs-guts",
	  GF_LOG_DEBUG,
	  "cleaning up fuse transport in disconnect handler");

  FREE (priv);
  priv = NULL;
  this->private = NULL;

  /* TODO: need graceful exit. every xlator should be ->fini()'ed
     and come out of main poll loop cleanly
  */
  return -1;
}

static struct transport_ops guts_transport_ops = {
  .disconnect = guts_transport_disconnect,
};

static transport_t guts_transport = {
  .ops = &guts_transport_ops,
  .private = NULL,
  .xl = NULL,
  .init = guts_transport_init,
  .fini = guts_transport_fini,
  .notify = guts_transport_notify
};

static inline xlator_t *
fuse_graph (xlator_t *graph)
{
  xlator_t *top = NULL;
  xlator_list_t *xlchild;

  top = CALLOC (1, sizeof (*top));
  ERR_ABORT (top);

  xlchild = CALLOC (1, sizeof(*xlchild));
  ERR_ABORT (xlchild);
  xlchild->xlator = graph;
  top->children = xlchild;
  top->ctx = graph->ctx;
  top->next = graph;
  graph->parent = top;

  return top;
}

static guts_replay_ctx_t *
guts_replay_init (guts_thread_ctx_t *thread)
{
  guts_replay_ctx_t *ctx = NULL;
  int32_t fd = open (thread->file, O_RDONLY);

  if (fd < 0) {
    gf_log ("glusterfs-guts", GF_LOG_DEBUG,
	    "failed to open tio_file %s", thread->file);
    return ctx;
  } else {
    struct fuse_ll *guts_ll = CALLOC (1, sizeof (*guts_ll));
    ERR_ABORT (guts_ll);
    
    ctx = CALLOC (1, sizeof (*ctx));
    ERR_ABORT (ctx);
    
    if (ctx) {
      /* equivalent to fuse_new_session () */
      guts_ll->conn.async_read = 1;
      guts_ll->conn.max_write = UINT_MAX;
      guts_ll->conn.max_readahead = UINT_MAX;
      memcpy (&guts_ll->op, &fuse_ops, sizeof (struct fuse_lowlevel_ops));
      list_init_req (&guts_ll->list);
      list_init_req (&guts_ll->interrupts);      
      guts_ll->owner = getuid ();
      guts_ll->userdata = thread;
      
      /* TODO: need to create transport_t object which whole of the glusterfs
       * so desperately depends on */
      transport_t *guts_trans = CALLOC (1, sizeof (*guts_trans));
      
      if (guts_trans) {
	memcpy (guts_trans, &guts_transport, sizeof (*guts_trans));
	guts_trans->ops = &guts_transport_ops;
      } else {
	gf_log ("glusterfs-guts", GF_LOG_ERROR,
		"failed to allocate memory for guts transport object");
	return NULL;
      }
      
      glusterfs_ctx_t *glfs_ctx = CALLOC (1, sizeof (*glfs_ctx));;
      if (glfs_ctx) {
	guts_trans->xl_private = glfs_ctx;
	guts_trans->xl = fuse_graph (thread->ctx->graph);
      }else {
	gf_log ("glusterfs-guts", GF_LOG_ERROR,
		"failed to allocate memory for glusterfs_ctx_t object");
	return NULL;
      }
      
      call_pool_t *pool = CALLOC (1, sizeof (call_pool_t));
      if (pool) {
	glfs_ctx->pool = pool;
	LOCK_INIT (&pool->lock);
	INIT_LIST_HEAD (&pool->all_frames);
      } else {
	gf_log ("glusterfs-guts", GF_LOG_ERROR,
		"failed to allocate memory for guts call pool");
	return NULL;
      }
      
      guts_trans->xl->ctx = glfs_ctx;
      guts_trans->init (guts_trans, NULL, guts_transport_notify);
      guts_ll->userdata = guts_trans;
      
      /* call fuse_init */
      guts_ll->op.init (guts_trans, NULL);
      
      {
	ctx->guts_ll = guts_ll;
	ctx->tio_fd = fd;
	ctx->inodes = get_new_dict ();
	ctx->fds = get_new_dict ();
	ctx->replies = get_new_dict ();
	INIT_LIST_HEAD(&ctx->requests);
	ctx->requests_dict = get_new_dict ();
      }
    } else {
      gf_log ("glusterfs-guts", GF_LOG_ERROR,
	      "failed to allocate memory for guts_ctx_t object");
      return NULL;
    }
  }
    
  return ctx;
}

int32_t
guts_replay (guts_thread_ctx_t *thread)
{
  guts_req_t *entry = NULL;
  guts_replay_ctx_t *ctx = guts_replay_init (thread);

  if (!ctx) {
    gf_log ("glusterfs-guts", GF_LOG_ERROR, 
	    "failed to initialize guts_replay");
    return -1;
  } else {
    while ((entry = guts_read_entry (ctx))) {
      /* here we go ... execute the request */
      fuse_req_t req = CALLOC (1, sizeof (struct fuse_req));
      ino_t ino = entry->header.nodeid;
      void *arg = entry->arg;

      if (req) {
	req->f = ctx->guts_ll;
	req->unique = entry->header.unique;
	req->ctx.uid = entry->header.uid;
	req->ctx.pid = entry->header.pid;
	
	/* req->u.ni.data is unused void *, while running in replay mode. Making use of available real-estate
	 * to store useful information of thread specific guts_replay_ctx */
	req->u.ni.data = (void *) ctx;
	/* req->ch is of type 'struct fuse_chan', which fuse uses only at the 
	 * time of the response it gets and is useful in sending the reply data to correct channel
	 * in /dev/fuse. This is not useful for us, so we ignore it by keeping it NULL */
	list_init_req (req);

	fuse_ino_t new_ino = guts_inode_search (ctx, ino);

	if (guts_replay_fop[entry->header.opcode].func) {
	  printf ("operation: %s && inode: %ld\n", guts_replay_fop[entry->header.opcode].name, new_ino);
	  guts_replay_fop[entry->header.opcode].func (req, new_ino, arg);
	}
	
	if (entry->arg)
	  free (entry->arg);
	free (entry);
      } else {
	gf_log ("glusterfs-guts", GF_LOG_ERROR,
		"failed to allocate memory for fuse_req_t object");
	return -1;
      }
    }
  }
  return 0;
}
