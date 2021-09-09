/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef STATEDUMP_H
#define STATEDUMP_H

#include <stdarg.h>
#include "glusterfs/inode.h"
#include "glusterfs/strfd.h"

#define GF_DUMP_MAX_BUF_LEN 4096

typedef struct gf_dump_xl_options_ {
    gf_boolean_t dump_priv;
    gf_boolean_t dump_inode;
    gf_boolean_t dump_fd;
    gf_boolean_t dump_inodectx;
    gf_boolean_t dump_fdctx;
    gf_boolean_t dump_history;
} gf_dump_xl_options_t;

typedef struct gf_dump_options_ {
    gf_boolean_t dump_mem;
    gf_boolean_t dump_iobuf;
    gf_boolean_t dump_callpool;
    gf_dump_xl_options_t xl_options;  // options for all xlators
    char *dump_path;
} gf_dump_options_t;

extern gf_dump_options_t dump_options;

__attribute__((__format__(__printf__, 3, 4))) static inline void
_gf_proc_dump_build_key(char *key, const char *prefix, const char *fmt, ...)
{
    va_list ap;
    int32_t len;

    len = snprintf(key, GF_DUMP_MAX_BUF_LEN, "%s.", prefix);
    if (len >= 0) {
        va_start(ap, fmt);
        len = vsnprintf(key + len, GF_DUMP_MAX_BUF_LEN - len, fmt, ap);
        va_end(ap);
    }
    if (len < 0) {
        *key = 0;
    }
}

#define gf_proc_dump_build_key(key, key_prefix, fmt...)                        \
    {                                                                          \
        _gf_proc_dump_build_key(key, key_prefix, ##fmt);                       \
    }

#define GF_PROC_DUMP_SET_OPTION(opt, val) opt = val

#define GF_CHECK_DUMP_OPTION_ENABLED(option_dump, var, label)                  \
    do {                                                                       \
        if (option_dump == _gf_true) {                                         \
            var = _gf_false;                                                   \
            goto label;                                                        \
        }                                                                      \
    } while (0);

void
gf_proc_dump_init();

void
gf_proc_dump_fini(void);

void
gf_proc_dump_cleanup(void);

void
gf_proc_dump_info(int signum, glusterfs_ctx_t *ctx);

int
gf_proc_dump_add_section(char *key, ...)
    __attribute__((__format__(__printf__, 1, 2)));

int
gf_proc_dump_write(char *key, char *value, ...)
    __attribute__((__format__(__printf__, 2, 3)));

void
inode_table_dump(inode_table_t *itable, char *prefix);

void
inode_table_dump_to_dict(inode_table_t *itable, char *prefix, dict_t *dict);

void
fdtable_dump(fdtable_t *fdtable, char *prefix);

void
fdtable_dump_to_dict(fdtable_t *fdtable, char *prefix, dict_t *dict);

void
inode_dump(inode_t *inode, char *prefix);

void
gf_proc_dump_mem_info_to_dict(dict_t *dict);

void
gf_proc_dump_mempool_info_to_dict(glusterfs_ctx_t *ctx, dict_t *dict);

void
glusterd_init(int sig);

void
gf_proc_dump_xlator_private(xlator_t *this, strfd_t *strfd);

void
gf_proc_dump_mallinfo(strfd_t *strfd);

void
gf_proc_dump_xlator_history(xlator_t *this, strfd_t *strfd);

void
gf_proc_dump_xlator_meminfo(xlator_t *this, strfd_t *strfd);

void
gf_proc_dump_xlator_profile(xlator_t *this, strfd_t *strfd);

void
gf_latency_statedump_and_reset(char *key, gf_latency_t *lat);
#endif /* STATEDUMP_H */
