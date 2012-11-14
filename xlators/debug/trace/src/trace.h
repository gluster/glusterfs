/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <time.h>
#include <errno.h>
#include "glusterfs.h"
#include "xlator.h"
#include "common-utils.h"
#include "event-history.h"
#include "logging.h"
#include "circ-buff.h"
#include "statedump.h"
#include "options.h"

#define TRACE_DEFAULT_HISTORY_SIZE 1024

typedef struct {
        /* Since the longest fop name is fremovexattr i.e 12 characters, array size
         * is kept 24, i.e double of the maximum.
         */
        char name[24];
        int enabled;
} trace_fop_name_t;

trace_fop_name_t trace_fop_names[GF_FOP_MAXVALUE];

typedef struct {
        gf_boolean_t log_file;
        gf_boolean_t log_history;
        size_t       history_size;
        int trace_log_level;
} trace_conf_t;

#define TRACE_STACK_UNWIND(op, frame, params ...)                       \
        do {                                                            \
                frame->local = NULL;                                    \
                STACK_UNWIND_STRICT (op, frame, params);                \
        } while (0);

#define LOG_ELEMENT(_conf, _string)                                     \
        do {                                                            \
                if (_conf) {                                            \
                        if ((_conf->log_history) == _gf_true)           \
                                gf_log_eh ("%s", _string);              \
                        if ((_conf->log_file) == _gf_true)              \
                                gf_log (THIS->name, _conf->trace_log_level, \
                                        "%s", _string);                 \
                }                                                       \
        } while (0);

#define trace_stat_to_str(buf, statstr)                                 \
        do {                                                            \
                char     atime_buf[256]    = {0,};                      \
                char     mtime_buf[256]    = {0,};                      \
                char     ctime_buf[256]    = {0,};                      \
                uint64_t ia_time           = 0;                         \
                                                                        \
                if (!buf)                                               \
                        break;                                          \
                                                                        \
                ia_time = buf->ia_atime;                                \
                strftime (atime_buf, 256, "[%b %d %H:%M:%S]",           \
                          localtime ((time_t *)&ia_time));              \
                                                                        \
                ia_time = buf->ia_mtime;                                \
                strftime (mtime_buf, 256, "[%b %d %H:%M:%S]",           \
                          localtime ((time_t *)&ia_time));              \
                                                                        \
                ia_time = buf->ia_ctime;                                \
                strftime (ctime_buf, 256, "[%b %d %H:%M:%S]",           \
                          localtime ((time_t *)&ia_time));              \
                                                                        \
                snprintf (statstr, sizeof (statstr),                    \
                          "gfid=%s ino=%"PRIu64", mode=%o, "            \
                          "nlink=%"GF_PRI_NLINK", uid=%u, "             \
                          "gid=%u, size=%"PRIu64", "                    \
                          "blocks=%"PRIu64", atime=%s, "                \
                          "mtime=%s, ctime=%s",                         \
                          uuid_utoa (buf->ia_gfid), buf->ia_ino,        \
                          st_mode_from_ia (buf->ia_prot,                \
                                           buf->ia_type),               \
                          buf->ia_nlink, buf->ia_uid,                   \
                          buf->ia_gid, buf->ia_size,                    \
                          buf->ia_blocks, atime_buf,                    \
                          mtime_buf, ctime_buf);                        \
        } while (0);
