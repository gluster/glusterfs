/*Copyright (c) 2008-2010 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "xlator.h"
#include "defaults.h"
#include "uuid.h"

#define MARKER_XATTR_PREFIX "trusted.glusterfs"
#define XTIME               "xtime"
#define VOLUME_MARK         "volume-mark"
#define VOLUME_UUID         "volume-uuid"
#define TIMESTAMP_FILE      "timestamp-file"

/*initialize the local variable*/
#define MARKER_INIT_LOCAL(_frame,_local) do {                   \
                _frame->local = _local;                         \
                _local->pid = _frame->root->pid;                \
                memset (&_local->loc, 0, sizeof (loc_t));       \
                _local->oplocal = NULL;                         \
        } while (0)

/* try alloc and if it fails, goto label */
#define ALLOCATE_OR_GOTO(var, type, label) do {                  \
                var = GF_CALLOC (sizeof (type), 1,               \
                                 gf_marker_mt_##type);           \
                if (!var) {                                      \
                        gf_log (this->name, GF_LOG_ERROR,        \
                                "out of memory :(");             \
                        goto label;                              \
                }                                                \
        } while (0)

struct marker_local{
        uint32_t        timebuf[2];
        pid_t           pid;
        loc_t           loc;

        struct marker_local *oplocal;
};
typedef struct marker_local marker_local_t;

struct marker_conf{
        char        *volume_uuid;
        uuid_t      volume_uuid_bin;
        char        *timestamp_file;
        char        *marker_xattr;
};
typedef struct marker_conf marker_conf_t;
