/*
   Copyright (c) 2008-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include "guts-parse.h"
#include "guts-tables.h"

/* unavoidable usage of global data.. :'( */
static int32_t tio_fd = 0;

int32_t
guts_tio_init (const char *filename)
{
  tio_fd = open (filename, O_WRONLY | O_CREAT);
  
  if (tio_fd < 0) {
    gf_log ("guts",
	    GF_LOG_ERROR,
	    "failed to open tio file %s", filename);
  }
  
  return tio_fd;
}

void
guts_reply_dump (fuse_req_t req,
		 const void *arg,
		 int32_t len)
{
  uint8_t *buf = NULL;
  uint8_t *ibuf = NULL;
  uint32_t buf_size = REP_HEADER_FULL_LEN + len;

  ibuf = buf = CALLOC (1, buf_size);
  
  /* being paranoid, checking for both ibuf and buf.. ;) */
  if (ibuf && buf) {
    memcpy (ibuf, REP_BEGIN, strlen (REP_BEGIN));
    ibuf += strlen (REP_BEGIN);
    memcpy (ibuf, req, sizeof (struct fuse_req));
    ibuf += sizeof (struct fuse_req);
    memcpy (ibuf, &len, sizeof (len));
    ibuf += sizeof (len);
    memcpy (ibuf, arg, len);
  
    gf_full_write (tio_fd, buf, buf_size);
    
    free (buf);
  } else {
    gf_log ("glusterfs-guts", GF_LOG_DEBUG,
	    "failed to allocate memory while dumping reply");
  }
}

void
guts_req_dump (struct fuse_in_header *in,
	       const void *arg,
	       int32_t len)
{
  /* GUTS_REQUEST_BEGIN:<fuse_in_header>:<arg-len>:<args>:GUTS_REQUEST_END */
  uint8_t *buf = NULL;
  uint8_t *ibuf = NULL;
  uint32_t buf_size = REQ_HEADER_FULL_LEN + len;

  ibuf = buf = CALLOC (1, buf_size);
  
  if (ibuf && buf) {
    memcpy (ibuf, REQ_BEGIN, strlen (REQ_BEGIN));
    ibuf += strlen (REQ_BEGIN);
    memcpy (ibuf, in, sizeof (*in));
    ibuf += sizeof (*in);
    memcpy (ibuf, &len, sizeof (len));
    ibuf += sizeof (len);
    memcpy (ibuf, arg, len);
    
    gf_full_write (tio_fd, buf, buf_size);
    
    free (buf);
  } else {
    gf_log ("glusterfs-guts", GF_LOG_DEBUG,
	    "failed to allocate memory while dumping reply");
  }
}



guts_req_t *
guts_read_entry (guts_replay_ctx_t *ctx)
{
  guts_req_t *req = NULL;
  guts_reply_t *reply = NULL;
  uint8_t begin[256] = {0,};
  int32_t ret = 0;
  int32_t fd = ctx->tio_fd;

  while (!req) {
    req = guts_get_request (ctx);
    
    if (!req) {
      ret = read (fd, begin, strlen (REQ_BEGIN));
      
      if (ret == 0) {
	gf_log ("glusterfs-guts", GF_LOG_DEBUG, 
		"guts replay finished");
	req = NULL;
      }
      
      if (is_request (begin)) {
	req = CALLOC (1, sizeof (*req));
	ERR_ABORT (req);
	gf_full_read (fd, (char *)req, REQ_HEADER_LEN);
	
	req->arg = CALLOC (1, req->arg_len + 1);
	ERR_ABORT (req->arg);
	gf_full_read (fd, req->arg, req->arg_len);
	gf_log ("guts",
		GF_LOG_DEBUG,
		"%s: fop %s (%d)\n", 
		begin, guts_log[req->header.opcode].name, req->header.opcode);
	guts_add_request (ctx, req);
	req = guts_get_request (ctx);
      } else {
	/* whenever a reply is read, we put it to a hash table and we would like to retrieve it whenever
	 * we get a reply for any call
	 */
	reply = CALLOC (1, sizeof (*reply));
	ERR_ABORT (reply);
	gf_full_read (fd, (char *)reply, REP_HEADER_LEN);
	
	reply->arg = CALLOC (1, reply->arg_len + 1);
	ERR_ABORT (reply->arg);
	gf_full_read (fd, reply->arg, reply->arg_len);
	
	/* add a new reply to */
	ret = guts_add_reply (ctx, reply);
	gf_log ("guts",
		GF_LOG_DEBUG,
		"got a reply with unique: %ld", reply->req.unique);
      }
    }
  }
  return req;
}

guts_reply_t *
guts_read_reply (guts_replay_ctx_t *ctx,
		 uint64_t unique)
{
  guts_req_t *req = NULL;
  guts_reply_t *reply = NULL, *rep = NULL;
  uint8_t begin[256] = {0,};
  int32_t ret = 0;
  int32_t fd = ctx->tio_fd;

  while (!rep) {
    
    ret = read (fd, begin, strlen (REQ_BEGIN));
    
    if (ret == 0) {
      printf ("\ndone\n");
      return NULL;
    }
    
    if (is_request (begin)) {
      req = CALLOC (1, sizeof (*req));
      ERR_ABORT (req);
      gf_full_read (fd, (char *)req, REQ_HEADER_LEN);
      
      req->arg = CALLOC (1, req->arg_len + 1);
      ERR_ABORT (req->arg);
      gf_full_read (fd, req->arg, req->arg_len);
      gf_log ("guts",
	      GF_LOG_DEBUG,
	      "%s: fop %s (%d)\n", 
	      begin, guts_log[req->header.opcode].name, req->header.opcode);
      
      ret = guts_add_request (ctx, req);
      
    } else {
      /* whenever a reply is read, we put it to a hash table and we would like to retrieve it whenever
       * we get a reply for any call
       */
      reply = CALLOC (1, sizeof (*reply));
      ERR_ABORT (reply);
      gf_full_read (fd, (char *)reply, REP_HEADER_LEN);
      
      reply->arg = CALLOC (1, reply->arg_len + 1);
      ERR_ABORT (reply->arg);
      gf_full_read (fd, reply->arg, reply->arg_len);
      
      /* add a new reply to */
      if (reply->req.unique == unique) {
	return reply;
      } else {
	ret = guts_add_reply (ctx, reply);
	gf_log ("guts",
		GF_LOG_DEBUG,
		"got a reply with unique: %ld", reply->req.unique);
      }
    }
  }
  return NULL;
}
