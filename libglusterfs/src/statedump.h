/*
  Copyright (c) 2009 Gluster, Inc. <http://www.gluster.com>
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


#ifndef STATEDUMP_H
#define STATEDUMP_H   

#include <stdarg.h>
#include "inode.h"

#define GF_DUMP_MAX_BUF_LEN 4096

#define GF_DUMP_LOGFILE_ROOT "/tmp/glusterdump"
#define GF_DUMP_LOGFILE_ROOT_LEN 256

static inline
void _gf_proc_dump_build_key (char *key, const char *prefix, char *fmt,...)
{
        char buf[GF_DUMP_MAX_BUF_LEN];
        va_list ap;

        memset(buf, 0, sizeof(buf));
        va_start(ap, fmt);
        vsnprintf(buf, GF_DUMP_MAX_BUF_LEN, fmt, ap);
        va_end(ap);
        snprintf(key, GF_DUMP_MAX_BUF_LEN, "%s.%s", prefix, buf);  
}

#define gf_proc_dump_build_key(key, key_prefix, fmt...) \
{\
        _gf_proc_dump_build_key(key, key_prefix, ##fmt);\
}

void
gf_proc_dump_init();

void 
gf_proc_dump_fini(void);

void
gf_proc_dump_cleanup(void);

void
gf_proc_dump_info(int signum);

void
gf_proc_dump_add_section(char *key,...);

void
gf_proc_dump_write(char *key, char *value,...);

void
inode_table_dump(inode_table_t *itable, char *prefix);

void
fdtable_dump(fdtable_t *fdtable, char *prefix);

void
inode_dump(inode_t *inode, char *prefix);
#endif /* STATEDUMP_H */
