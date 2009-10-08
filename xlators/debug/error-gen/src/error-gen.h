/*
  Copyright (c) 2008-2009 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _ERROR_GEN_H
#define _ERROR_GEN_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#define GF_FAILURE_DEFAULT 10
#define NO_OF_FOPS 42

enum {
	ERR_LOOKUP,
	ERR_STAT,
        ERR_READLINK,
	ERR_MKNOD,
	ERR_MKDIR,
	ERR_UNLINK,
	ERR_RMDIR,
	ERR_SYMLINK,
	ERR_RENAME,
	ERR_LINK,
	ERR_TRUNCATE,
	ERR_CREATE,
	ERR_OPEN,
	ERR_READV,
	ERR_WRITEV,
	ERR_STATFS,
	ERR_FLUSH,
	ERR_FSYNC,
	ERR_SETXATTR,
	ERR_GETXATTR,
	ERR_REMOVEXATTR,
	ERR_OPENDIR,
	ERR_READDIR,
	ERR_READDIRP,
	ERR_GETDENTS,
	ERR_FSYNCDIR,
	ERR_ACCESS,
	ERR_FTRUNCATE,
	ERR_FSTAT,
	ERR_LK,
	ERR_SETDENTS,
	ERR_CHECKSUM,
	ERR_XATTROP,
	ERR_FXATTROP,
	ERR_INODELK,
	ERR_FINODELK,
	ERR_ENTRYLK,
	ERR_FENTRYLK,
        ERR_SETATTR,
	ERR_FSETATTR,
	ERR_STATS,
        ERR_GETSPEC
};

typedef struct {
        int enable[NO_OF_FOPS];
        int op_count;
        int failure_iter_no;
        char *error_no;
        gf_lock_t lock;
} eg_t;

typedef struct {
        int error_no_count;
	int error_no[20];
} sys_error_t;

#endif
