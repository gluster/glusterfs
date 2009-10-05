/*
   Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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

#include "guts-parse.h"
#include "dict.h"
#include "guts-tables.h"


int32_t
guts_attr_cmp (const struct stat *attr,
	       const struct stat *old_attr)
{
  return 0;
}

int32_t
guts_statvfs_cmp (const struct statvfs *stbuf,
		  const struct statvfs *old_stbuf)
{
  return 0;
}

int32_t
guts_flock_cmp (struct flock *lock,
		struct flock *old_lock)
{
  return 0;
}


guts_req_t *
guts_lookup_request (guts_replay_ctx_t *ctx, uint64_t unique)
{
  guts_req_t *req = NULL;

  if (unique == 0) {
    if (list_empty (&ctx->requests))
      req = NULL;
    else {
      /* pick an entry from list, move it out of the list and return it to the caller */
      char *key = NULL;

      req = list_entry (ctx->requests.next, guts_req_t, list);
      list_del (&req->list);

      asprintf (&key, "%llu", req->header.unique);

      dict_set (ctx->requests_dict, key, data_from_static_ptr (req));
      
      if (key)
	free (key);
    }
  } else {
    char *key = NULL;
    data_t *req_data = NULL;

    asprintf (&key, "%llu", unique);
    
    req_data = dict_get (ctx->requests_dict, key);
    
    if (req_data) 
      req = data_to_ptr (req_data);
    
    if (key)
      free (key);
  }
  return req;
}

guts_req_t *
guts_get_request (guts_replay_ctx_t *ctx)
{
  return guts_lookup_request (ctx, 0);
}

int32_t
guts_add_request (guts_replay_ctx_t *ctx,
		  guts_req_t *req)
{
  list_add_tail (&req->list, &ctx->requests);
  return 0;
}

int32_t
guts_add_reply (guts_replay_ctx_t *ctx,
		guts_reply_t *reply)
{
  char *key = NULL;
  asprintf (&key, "%llu", reply->req.unique);

  dict_set (ctx->replies, key, data_from_static_ptr(reply));

  if (key)
    free(key);

  return 0;
}


guts_reply_t *
guts_lookup_reply (guts_replay_ctx_t *ctx,
		   uint64_t unique)
{
  char *key = NULL;
  data_t *reply_data = NULL;
  guts_reply_t *new_reply = NULL;

  asprintf (&key, "%llu", unique);
  reply_data = dict_get (ctx->replies, key);
  
  if (reply_data) {
    new_reply = data_to_ptr (reply_data);
    dict_del (ctx->replies, key);
  } else {
    /* reply has not yet been read from tio file */
    new_reply = guts_read_reply (ctx, unique);
    
    if (!new_reply) {
      /* failed to fetch reply for 'unique' from tio file */
      new_reply;
    }
  }

  if (key)
    free(key);
  
  return new_reply;
  
}

int32_t
guts_inode_update (guts_replay_ctx_t *ctx,
		   fuse_ino_t old_ino,
		   fuse_ino_t new_ino)
{
  char *key = NULL;
  asprintf (&key, "%ld", old_ino);
  dict_set (ctx->inodes, key, data_from_uint64 (new_ino));

  if (key)
    free(key);

  return 0;
}
		   
fuse_ino_t
guts_inode_search (guts_replay_ctx_t *ctx,
		   fuse_ino_t old_ino)
{
  char *key = NULL;
  data_t *ino_data = NULL;
  fuse_ino_t new_ino = 0;

  asprintf (&key, "%ld", old_ino);
  ino_data = dict_get (ctx->inodes, key);
  
  if (ino_data)
    new_ino = data_to_uint64 (ino_data);
  else if (old_ino != /* TODO: FIXME */1 ) {
    new_ino = 0;
  } else
    new_ino = old_ino;

  if (key)
    free(key);

  return new_ino;
}

int32_t
guts_fd_add (guts_replay_ctx_t *ctx,
	     unsigned long old_fd,
	     fd_t *new_fd)
{
  char *key = NULL;
  asprintf (&key, "%ld", old_fd);
  dict_set (ctx->fds, key, data_from_static_ptr (new_fd));

  if (key)
    free(key);

  return 0;
}

fd_t *
guts_fd_search (guts_replay_ctx_t *ctx,
		unsigned long old_fd)
{
  char *key = NULL;
  data_t *fd_data = NULL;
  fd_t *new_fd = NULL;

  asprintf (&key, "%ld", old_fd);
  fd_data = dict_get (ctx->fds, key);
  
  if (fd_data)
    new_fd = data_to_ptr (fd_data);

  if (key)
    free(key);
  
  return new_fd;
}

int32_t
guts_delete_fd (guts_replay_ctx_t *ctx,
		unsigned long old_fd)
{
  char *key = NULL;
  data_t *fd_data = NULL;

  asprintf (&key, "%ld", old_fd);
  fd_data = dict_get (ctx->fds, key);
  
  if (fd_data)
    dict_del (ctx->fds, key);

  if (key)
    free(key);
  
  return 0;
}

inline int32_t
guts_get_opcode (guts_replay_ctx_t *ctx,
		 uint64_t unique)
{
  guts_req_t *req = guts_lookup_request (ctx, unique);
  
  return ((req == NULL) ? -1 : req->header.opcode);

}
