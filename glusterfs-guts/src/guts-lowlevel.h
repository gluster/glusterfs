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

#ifndef _GUTS_LOWLEVEL_H_
#define _GUTS_LOWLEVEL_H_

int
guts_reply_err (fuse_req_t req,
		int err);

int
guts_reply_none (fuse_req_t req);

int
guts_reply_entry (fuse_req_t req,
		  const struct fuse_entry_param *e);

int
guts_reply_create (fuse_req_t req, 
		   const struct fuse_entry_param *e,
		   const struct fuse_file_info *f);

int
guts_reply_attr (fuse_req_t req,
		 const struct stat *attr,
		 double attr_timeout);

int
guts_reply_readlink (fuse_req_t req,
		     const char *linkname);

int
guts_reply_open (fuse_req_t req,
		 const struct fuse_file_info *f);

int
guts_reply_write (fuse_req_t req,
		  size_t count);

int
guts_reply_buf (fuse_req_t req,
		const char *buf,
		size_t size);

int
guts_reply_statfs (fuse_req_t req,
		   const struct statvfs *stbuf);

int
guts_reply_xattr (fuse_req_t req,
		  size_t count);

int
guts_reply_lock (fuse_req_t req,
		 struct flock *lock);

/* exploiting the macros to reduce coding work ;) */
#define fuse_reply_entry    guts_reply_entry
#define fuse_reply_err      guts_reply_err
#define fuse_reply_none     guts_reply_none
#define fuse_reply_attr     guts_reply_attr
#define fuse_reply_open     guts_reply_open
#define fuse_reply_readlink guts_reply_readlink
#define fuse_reply_create   guts_reply_create
#define fuse_reply_write    guts_reply_write
#define fuse_reply_buf      guts_reply_buf
#define fuse_reply_statfs   guts_reply_statfs
#define fuse_reply_xattr    guts_reply_xattr
#define fuse_reply_lock     guts_reply_lock

#endif
