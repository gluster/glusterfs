/*
   Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include "glusterfs-guts.h"
#include <signal.h>

#include "guts-parse.h"
#include "guts-tables.h"
#include "guts-trace.h"

static xlator_t *
fuse_graph (xlator_t *graph)
{
  xlator_t *top = calloc (1, sizeof (*top));
  xlator_list_t *xlchild;

  xlchild = calloc (1, sizeof(*xlchild));
  xlchild->xlator = graph;
  top->children = xlchild;
  top->ctx = graph->ctx;
  top->next = graph;
  graph->parent = top;

  return top;
}

int32_t
fuse_thread (pthread_t *thread, void *data);

int32_t
guts_trace (guts_ctx_t *guts_ctx)
{
  transport_t *mp = NULL;
  glusterfs_ctx_t ctx = {
    .poll_type = SYS_POLL_TYPE_EPOLL,
  };
  xlator_t *graph = NULL;
  call_pool_t *pool = NULL;
  int32_t ret = -1;
  pthread_t thread;
  /* Ignore SIGPIPE */
  signal (SIGPIPE, SIG_IGN);

#if HAVE_BACKTRACE
  /* Handle SIGABORT and SIGSEGV */
  signal (SIGSEGV, gf_print_trace);
  signal (SIGABRT, gf_print_trace);
#endif /* HAVE_BACKTRACE */

  ret = guts_tio_init (guts_ctx->file);

  if (ret < 0) {
    gf_log ("glusterfs-guts", GF_LOG_ERROR,
	    "running in trace mode: failed to open tio file %s", guts_ctx->file);
    return -1;
  }

  pool = ctx.pool = calloc (1, sizeof (call_pool_t));
  LOCK_INIT (&pool->lock);
  INIT_LIST_HEAD (&pool->all_frames);

  /* glusterfs_mount has to be ideally placed after all the initialisation stuff */
  if (!(mp = glusterfs_mount (&ctx, guts_ctx->mountpoint))) {
    gf_log ("glusterfs-guts", GF_LOG_ERROR, "Unable to mount glusterfs");
    return -1;
  }

  gf_timer_registry_init (&ctx);
  graph = guts_ctx->graph;

  if (!graph) {
    gf_log ("glusterfs-guts", GF_LOG_ERROR,
	    "Unable to get xlator graph for mount_point %s", guts_ctx->mountpoint);
    transport_disconnect (mp);
    return -1;
  }

  ctx.graph = graph;

  mp->xl = fuse_graph (graph);
  mp->xl->ctx = &ctx;
  
  fuse_thread (&thread, mp);

  while (!poll_iteration (&ctx));

  return 0;
}



static void
guts_name (struct fuse_in_header *in,
	   const void *inargs)
{
  char *name = (char *) inargs;

  guts_req_dump (in, name, strlen (name));
}

static void
guts_noarg (struct fuse_in_header *in,
	    const void *inargs)
{
  guts_req_dump (in, NULL, 0);
}


static void
guts_setattr (struct fuse_in_header *in,
	      const void *inargs)
{
  struct fuse_setattr_in *arg = (struct fuse_setattr_in *)inargs;
  guts_req_dump (in, arg, sizeof (*arg));
}

static void
guts_access (struct fuse_in_header *in,
	     const void *inargs)
{
  struct fuse_access_in *arg = (struct fuse_access_in *)inargs;
  guts_req_dump (in, arg, sizeof (*arg));
}


static void
guts_mknod (struct fuse_in_header *in,
	    const void *inargs)
{
  struct fuse_mknod_in *arg = (struct fuse_mknod_in *) inargs;
  guts_req_dump (in, arg, sizeof (*arg));
}

static void
guts_mkdir (struct fuse_in_header *in,
	    const void *inargs)
{
  struct fuse_mkdir_in *arg = (struct fuse_mkdir_in *) inargs;
  guts_req_dump (in, arg, sizeof (*arg));
}


static void
guts_symlink (struct fuse_in_header *in,
	    const void *inargs)
{
  char *name = (char *) inargs;
  char *linkname = ((char *) inargs) + strlen ((char *) inargs) + 1;
  struct guts_symlink_in symlink_in;

  strcpy (symlink_in.name, name);
  strcpy (symlink_in.linkname, linkname);
  guts_req_dump (in, &symlink_in, sizeof (symlink_in));
}

static void
guts_rename (struct fuse_in_header *in,
	     const void *inargs)
{
  struct fuse_rename_in *arg = (struct fuse_rename_in *) inargs;
  char *oldname = PARAM(arg);
  char *newname = oldname + strlen (oldname) + 1;
  struct guts_rename_in rename_in;
  
  memset (&rename_in, 0, sizeof (rename_in));
  memcpy (&rename_in, arg, sizeof (*arg));
  strcpy (rename_in.oldname, oldname);
  strcpy (rename_in.newname, newname);
  
  guts_req_dump (in, &rename_in, sizeof (rename_in));

}

static void
guts_link (struct fuse_in_header *in,
	   const void *inargs)
{
  struct fuse_link_in *arg = (struct fuse_link_in *) inargs;
  
  guts_req_dump (in, arg, sizeof (*arg));
}


static void
guts_open (struct fuse_in_header *in,
	   const void *inargs)
{
  struct fuse_open_in *arg = (struct fuse_open_in *) inargs;
  
  guts_req_dump (in, arg, sizeof (*arg));
}

static void
guts_create (struct fuse_in_header *in,
	     const void *inargs)
{
  struct guts_create_in create_in;
  struct fuse_open_in *arg = (struct fuse_open_in *) inargs;
  char *name = PARAM (arg);

  memset (&create_in, 0, sizeof (create_in));
  memcpy (&create_in.open_in, arg, sizeof (*arg));
  memcpy (&create_in.name, name, strlen (name));

  guts_req_dump (in, &create_in, sizeof (create_in));
}


static void 
guts_read(struct fuse_in_header *in,
	  const void *inarg)
{
    struct fuse_read_in *arg = (struct fuse_read_in *) inarg;
    guts_req_dump (in, arg, sizeof (*arg));
}

static void
guts_write(struct fuse_in_header *in,
	   const void *inarg)
{
  /* TODO: where the hell is the data to be written??? */
    struct fuse_write_in *arg = (struct fuse_write_in *) inarg;
    guts_req_dump (in, arg, sizeof (*arg));
}

static void
guts_flush(struct fuse_in_header *in,
	   const void *inarg)
{
    struct fuse_flush_in *arg = (struct fuse_flush_in *) inarg;
    guts_req_dump (in, arg, sizeof (*arg));
}

static void
guts_release(struct fuse_in_header *in,
	     const void *inarg)
{
    struct fuse_release_in *arg = (struct fuse_release_in *) inarg;
    guts_req_dump (in, arg, sizeof (*arg));
}

static void
guts_fsync(struct fuse_in_header *in,
	   const void *inarg)
{
    struct fuse_fsync_in *arg = (struct fuse_fsync_in *) inarg;
    guts_req_dump (in, arg, sizeof (*arg));
}


static void
guts_readdir(struct fuse_in_header *in,
	     const void *inarg)
{
    struct fuse_read_in *arg = (struct fuse_read_in *) inarg;
    guts_req_dump (in, arg, sizeof (*arg));
}

static void
guts_releasedir(struct fuse_in_header *in,
		const void *inarg)
{
    struct fuse_release_in *arg = (struct fuse_release_in *) inarg;
    guts_req_dump (in, arg, sizeof (*arg));
}

static void
guts_fsyncdir(struct fuse_in_header *in,
	      const void *inarg)
{
    struct fuse_fsync_in *arg = (struct fuse_fsync_in *) inarg;
    guts_req_dump (in, arg, sizeof (*arg));
}


static void
guts_setxattr(struct fuse_in_header *in,
	      const void *inarg)
{
  struct fuse_setxattr_in *arg = (struct fuse_setxattr_in *) inarg;
  char *name = PARAM(arg);
  char *value = name + strlen(name) + 1;
  struct guts_xattr_in setxattr_in;
  
  memset (&setxattr_in, 0, sizeof (setxattr_in));
  memcpy (&setxattr_in, arg, sizeof (*arg));
  strcpy (setxattr_in.name, name);
  strcpy (setxattr_in.value, value);
  
  guts_req_dump (in, &setxattr_in, sizeof (setxattr_in));

}

static void
guts_getxattr(struct fuse_in_header *in,
	      const void *inarg)
{
    struct fuse_getxattr_in *arg = (struct fuse_getxattr_in *) inarg;
    guts_req_dump (in, arg, sizeof (*arg));
}

guts_log_t guts_log[] = {
  [FUSE_LOOKUP] = { guts_name, "lookup" },
  [FUSE_GETATTR] = { guts_noarg, "getattr" },
  [FUSE_SETATTR] = { guts_setattr, "setattr" },
  [FUSE_ACCESS] = { guts_access, "access" },
  [FUSE_READLINK] = { guts_noarg, "readlink" },
  [FUSE_MKNOD] = { guts_mknod, "mknod" },
  [FUSE_MKDIR] = { guts_mkdir, "mkdir" },
  [FUSE_UNLINK] = { guts_name, "unlink" },
  [FUSE_RMDIR] = { guts_name, "rmdir" },
  [FUSE_SYMLINK] = { guts_symlink, "symlink" },
  [FUSE_RENAME] = { guts_rename, "rename" },
  [FUSE_LINK] = { guts_link, "link" },
  [FUSE_CREATE] = { guts_create, "create" },
  [FUSE_OPEN] = { guts_open, "open" },
  [FUSE_READ] = { guts_read, "read" },
  [FUSE_WRITE] = { guts_write, "write" },
  [FUSE_FLUSH] = { guts_flush, "flush" },
  [FUSE_RELEASE] = { guts_release, "release" },
  [FUSE_FSYNC] = { guts_fsync, "fsync" },
  [FUSE_OPENDIR] = { guts_open, "opendir" },
  [FUSE_READDIR] = { guts_readdir, "readdir" },
  [FUSE_RELEASEDIR] = { guts_releasedir, "releasedir" },
  [FUSE_FSYNCDIR] = { guts_fsyncdir, "fsyncdir" },
  [FUSE_STATFS] = { guts_noarg, "statfs" },
  [FUSE_SETXATTR] = { guts_setxattr, "setxattr" },
  [FUSE_GETXATTR] = { guts_getxattr, "getxattr" },
  [FUSE_LISTXATTR] = { guts_getxattr, "listxattr" },
  [FUSE_REMOVEXATTR] = { guts_name, "removexattr" },
};
  
/* used for actual tracing task */

int32_t
guts_log_req (void *buf,
	      int32_t len)
{
  struct fuse_in_header *in = buf;
  const void *inargs = NULL;
  int32_t header_len = sizeof (struct fuse_in_header);

  if (header_len < len ) {
    inargs = buf + header_len;
    gf_log ("guts-gimmik", GF_LOG_ERROR,
	      "unique: %llu, opcode: %s (%i), nodeid: %lu, insize: %zu\n",
	      (unsigned long long) in->unique, "<null>",
	      /*opname((enum fuse_opcode) in->opcode),*/ in->opcode,
	      (unsigned long) in->nodeid, len);
    if (guts_log[in->opcode].func)
      guts_log[in->opcode].func (in, inargs);

  } else {
    gf_log ("guts", GF_LOG_ERROR,
	    "header is longer than the buffer passed");
  }
  
  return 0;
}


int
guts_reply_err (fuse_req_t req, int err)
{
  if (IS_TRACE(req)) {
    /* we are tracing calls, just dump the reply to file and continue with fuse_reply_err() */
    guts_reply_dump (req, &err, sizeof (err));
    return fuse_reply_err (req, err);
  } else {
    /* we are replaying. ;) */
    guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;
    int32_t opcode = guts_get_opcode (ctx, req->unique);

    /* see if we are called by close/closedir, if yes remove do a guts_fd_delete () */    
    if (opcode == FUSE_RELEASEDIR || opcode == FUSE_RELEASE) {
      guts_req_t *request = guts_lookup_request (ctx, req->unique);
      struct fuse_release_in *arg = request->arg;

      guts_delete_fd (ctx, arg->fh);
    } else if (err == -1) {
      /* error while replaying?? just quit as of now 
       * TODO: this is not the right way */
      printf (":O - glusterfs-guts: replay failed\n");
      exit (0);
    }

    return 0;
  }
}

void
guts_reply_none (fuse_req_t req)
{
  if (IS_TRACE(req)) {
    guts_reply_dump (req, NULL, 0);
    fuse_reply_none (req);
  } else {
    return;
  }
}

int
guts_reply_entry (fuse_req_t req, 
		  const struct fuse_entry_param *e)
{
  if (IS_TRACE(req)) {
    guts_reply_dump (req, e, sizeof (*e));
    return fuse_reply_entry (req, e);
  } else {
    /* TODO: is dict_set() the best solution for this case?? */
    guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;
    guts_reply_t *reply = guts_lookup_reply (ctx, req->unique);
    struct fuse_entry_param *old_entry = (struct fuse_entry_param *)reply->arg;
    guts_inode_update (ctx, old_entry->ino, e->ino);
    return 0;
  }
}

int
guts_reply_create (fuse_req_t req, 
		   const struct fuse_entry_param *e,
		   const struct fuse_file_info *f)
{
  if (IS_TRACE(req)) {
    struct guts_create_out create_out;
    
    memset (&create_out, 0, sizeof (create_out));
    memcpy (&create_out.e, e, sizeof (*e));
    memcpy (&create_out.f, f, sizeof (*f));
    
    guts_reply_dump (req, &create_out, sizeof (create_out));
    return fuse_reply_create (req, e, f);
  } else {
    guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;
    guts_reply_t *reply = guts_lookup_reply (ctx, req->unique);
    struct guts_create_out *old_createout = (struct guts_create_out *) reply->arg;
    struct fuse_file_info *old_f = &old_createout->f;
    
    /* add a new fd and map it to the file handle, as stored in tio file */
    guts_fd_add (ctx, old_f->fh, (fd_t *)(long)f->fh);
    
    return 0;
  }
}


int
guts_reply_attr (fuse_req_t req,
		 const struct stat *attr,
		 double attr_timeout)
{
  if (IS_TRACE(req)) {
    struct guts_attr_out attr_out;
    
    memcpy (&attr_out.attr, attr, sizeof (*attr));
    attr_out.attr_timeout = attr_timeout;
    
    guts_reply_dump (req, &attr_out, sizeof (attr_out));
    return fuse_reply_attr (req, attr, attr_timeout);
  } else {
    guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;
    guts_reply_t *reply = guts_lookup_reply (ctx, req->unique);
    struct guts_attr_out *old_attrout = (struct guts_attr_out *) reply->arg;

    if (!guts_attr_cmp (attr, &old_attrout->attr))
      return 0;
    else {
      gf_log ("glusterfs-guts", GF_LOG_ERROR,
	      "attr failed.");
      return -1;
    }
  }
}

int
guts_reply_readlink (fuse_req_t req,
		     const char *linkname)
{
  if (IS_TRACE(req)) {
    guts_reply_dump (req, linkname, strlen (linkname));
    return fuse_reply_readlink (req, linkname);
  } else {
    guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;
    guts_reply_t *reply = guts_lookup_reply (ctx, req->unique);
    char *old_linkname = (char *) reply->arg;
    if (!strcmp (linkname, old_linkname))
      return 0;
    else {
      gf_log ("glusterfs-guts", GF_LOG_ERROR,
	      "readlink failed. linkname in tio file: %s \n linkname recieved on replay: %s", 
	      old_linkname, linkname);
      return -1;
    }
  }
}

int
guts_reply_open (fuse_req_t req,
		 const struct fuse_file_info *f)
{
  if (IS_TRACE(req)) {
    guts_reply_dump (req, f, sizeof (*f));
    return fuse_reply_open (req, f);
  } else {
    /* the fd we recieve here is the valid fd for our current session, map the indicative number we have
     * in mapping */
    guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;
    guts_reply_t *reply = guts_lookup_reply (ctx, req->unique);
    
    if (reply) {
      struct fuse_file_info *old_f = reply->arg;

      /* add a new fd and map it to the file handle, as stored in tio file */
      guts_fd_add (ctx, old_f->fh, (fd_t *)(long)f->fh);
    }

    return 0;
  }
}

int
guts_reply_write (fuse_req_t req,
		  size_t count)
{
  if (IS_TRACE(req)) {
    guts_reply_dump (req, &count, sizeof (count));
    return fuse_reply_write (req, count);
  } else {
    guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;
    guts_reply_t *reply = guts_lookup_reply (ctx, req->unique);
    size_t *old_count = reply->arg;
    if (count == *old_count)
      return 0;
    else {
      gf_log ("glusterfs-guts", GF_LOG_ERROR,
	      "writev failed. old writev count: %d \n writev count on replay: %d", 
	      old_count, count);
      return -1;
    }
  }
}

int
guts_reply_buf (fuse_req_t req,
		const char *buf,
		size_t size)
{
  if (IS_TRACE(req)) {
    guts_reply_dump (req, buf, size);
    return fuse_reply_buf (req, buf, size);
  } else {
    guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;
    guts_reply_t *reply = guts_lookup_reply (ctx, req->unique);
    char *old_buf = reply->arg;
    size_t old_size = reply->arg_len;
    if ((size == old_size) && (!memcmp (buf, old_buf, size)))
      return 0;
    else {
      gf_log ("glusterfs-guts", GF_LOG_ERROR,
	      "readv failed. old readv size: %d \n readv size on replay: %d", 
	      old_size, size);
      return -1;
    }
  }
}

int
guts_reply_statfs (fuse_req_t req,
		   const struct statvfs *stbuf)
{
  if (IS_TRACE(req)) {
    guts_reply_dump (req, stbuf, sizeof (*stbuf));
    return fuse_reply_statfs (req, stbuf);
  } else {
    guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;
    guts_reply_t *reply = guts_lookup_reply (ctx, req->unique);
    struct statvfs *old_stbuf = reply->arg;

    if (!guts_statvfs_cmp (old_stbuf, stbuf))
      return 0;
    else {
      gf_log ("glusterfs-guts", GF_LOG_ERROR,
	      "statfs failed.");
      return -1;
    }
  }
}

int
guts_reply_xattr (fuse_req_t req,
		  size_t count)
{
  if (IS_TRACE(req)) {
    guts_reply_dump (req, &count, sizeof (count));
    return fuse_reply_xattr (req, count);
  } else {
    guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;
    guts_reply_t *reply = guts_lookup_reply (ctx, req->unique);
    size_t *old_count = reply->arg;
    if (count == *old_count)
      return 0;
    else {
      gf_log ("glusterfs-guts", GF_LOG_ERROR,
	      "xattr failed. old xattr count: %d \n xattr count on replay: %d", 
	      old_count, count);
      return -1;
    }
  }
}

int
guts_reply_lock (fuse_req_t req,
		 struct flock *lock)
{
  if (IS_TRACE(req)) {
    guts_reply_dump (req, lock , sizeof (*lock));
    return fuse_reply_lock (req, lock);
  } else {
    guts_replay_ctx_t *ctx = (guts_replay_ctx_t *) req->u.ni.data;
    guts_reply_t *reply = guts_lookup_reply (ctx, req->unique);
    struct flock *old_lock = (struct flock *)reply->arg;
    if (!guts_flock_cmp (lock, old_lock))
      return 0;
    else {
      gf_log ("glusterfs-guts", GF_LOG_ERROR,
	      "lock failed.");
      return -1;
    }
  }
}
