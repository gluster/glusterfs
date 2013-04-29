/*
   Copyright (c) 2006-2011 Gluster, Inc. <http://www.gluster.com>
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

#ifndef __GLUPY_H__
#define __GLUPY_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif
#include "mem-types.h"

enum {
        GLUPY_LOOKUP = 0,
        GLUPY_CREATE,
        GLUPY_OPEN,
        GLUPY_READV,
        GLUPY_WRITEV,
        GLUPY_OPENDIR,
        GLUPY_READDIR,
        GLUPY_READDIRP,
        GLUPY_STAT,
        GLUPY_FSTAT,
        GLUPY_STATFS,
        GLUPY_SETXATTR,
        GLUPY_GETXATTR,
        GLUPY_FSETXATTR,
        GLUPY_FGETXATTR,
        GLUPY_REMOVEXATTR,
        GLUPY_FREMOVEXATTR,
        GLUPY_LINK,
        GLUPY_UNLINK,
        GLUPY_READLINK,
        GLUPY_SYMLINK,
        GLUPY_MKNOD,
        GLUPY_MKDIR,
        GLUPY_RMDIR,
        GLUPY_N_FUNCS
};

typedef struct {
        PyObject        *py_module;
        PyObject        *py_xlator;
        long            fops[GLUPY_N_FUNCS];
        long            cbks[GLUPY_N_FUNCS];
} glupy_private_t;

enum gf_glupy_mem_types_ {
        gf_glupy_mt_priv = gf_common_mt_end + 1,
        gf_glupy_mt_end
};

#endif /* __GLUPY_H__ */
