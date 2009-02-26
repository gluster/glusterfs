/*
   Copyright (c) 2006-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#define IS_TRACE(req) (req->ch != NULL)

#define PARAM(inarg) (((char *)(inarg)) + sizeof(*(inarg)))

struct guts_symlink_in {
    char name[NAME_MAX];
    char linkname[NAME_MAX];
};

struct guts_create_in {
  struct fuse_open_in open_in;
  char name[NAME_MAX];
};

struct guts_xattr_in {
      struct fuse_setxattr_in xattr;
      char name[NAME_MAX];
      char value[NAME_MAX];
};

struct guts_rename_in {
  struct fuse_rename_in rename;
  char oldname[NAME_MAX];
  char newname[NAME_MAX];
};

struct guts_create_out {
  struct fuse_entry_param e;
  struct fuse_file_info f;
};

struct guts_attr_out {
  struct stat attr;
  double attr_timeout;
};
