/*
   Copyright (c) 2006-2014 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __GLUPY_H__
#define __GLUPY_H__

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
