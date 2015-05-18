/*
  Copyright (c) 2014 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/
#include "glusterfs.h"
#include "xlator.h"
#include "dht-common.h"
#include "byte-order.h"

int
dht_hash_compute (xlator_t *this, int type, const char *name, uint32_t *hash_p)
{
    return 0;
}

int
dht_inode_ctx_layout_get (inode_t *inode, xlator_t *this, dht_layout_t **layout)
{
    return 0;
}

int
dht_inode_ctx_layout_set (inode_t *inode, xlator_t *this,
                          dht_layout_t *layout_int)
{
    return 0;
}

int
dict_get_ptr (dict_t *this, char *key, void **ptr)
{
    return 0;
}

int
dict_get_ptr_and_len (dict_t *this, char *key, void **ptr, int *len)
{
    return 0;
}

int _gf_log (const char *domain, const char *file,
             const char *function, int32_t line, gf_loglevel_t level,
             const char *fmt, ...)
{
    return 0;
}

int _gf_log_callingfn (const char *domain, const char *file,
                       const char *function, int32_t line, gf_loglevel_t level,
                       const char *fmt, ...)
{
    return 0;
}

void gf_uuid_unparse(const uuid_t uu, char *out)
{
    // could call a will-return function here
    // to place the correct data in *out
}

int
_gf_msg (const char *domain, const char *file, const char *function,
         int32_t line, gf_loglevel_t level, int errnum, int trace,
         uint64_t msgid, const char *fmt, ...)
{
    return 0;
}
