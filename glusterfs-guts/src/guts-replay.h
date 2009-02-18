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

#define PARAM(inarg) (((char *)(inarg)) + sizeof(*(inarg)))

void guts_reply_err (fuse_req_t, error_t);
void guts_reply_open (fuse_req_t, struct fuse_file_info *);
void guts_reply_statfs (fuse_req_t, struct statvfs *);

typedef void (*guts_replay_fop_t)(fuse_req_t, fuse_ino_t, const void *);

typedef struct {
  guts_replay_fop_t func; 
  const char *name;
} guts_replay_t;

extern struct fuse_lowlevel_ops fuse_ops;

