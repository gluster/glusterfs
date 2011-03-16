/*
   Copyright (c) 2006-2010 Gluster, Inc. <http://www.gluster.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU Affero General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#ifndef __LOGGING_H__
#define __LOGGING_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef GF_DARWIN_HOST_OS
#define GF_PRI_FSBLK       "u"
#define GF_PRI_DEV         PRId32
#define GF_PRI_NLINK       PRIu16
#define GF_PRI_SUSECONDS   PRId32
#else
#define GF_PRI_FSBLK       PRIu64
#define GF_PRI_DEV         PRIu64
#define GF_PRI_NLINK       PRIu32
#define GF_PRI_SUSECONDS   "ld"
#endif
#define GF_PRI_BLKSIZE     PRId32
#define GF_PRI_SIZET       "zu"

#if 0
/* Syslog definitions :-) */
#define LOG_EMERG   0           /* system is unusable */
#define LOG_ALERT   1           /* action must be taken immediately */
#define LOG_CRIT    2           /* critical conditions */
#define LOG_ERR     3           /* error conditions */
#define LOG_WARNING 4           /* warning conditions */
#define LOG_NOTICE  5           /* normal but significant condition */
#define LOG_INFO    6           /* informational */
#define LOG_DEBUG   7           /* debug-level messages */
#endif

typedef enum {
        GF_LOG_NONE,
        GF_LOG_EMERG,
        GF_LOG_ALERT,
        GF_LOG_CRITICAL,   /* fatal errors */
        GF_LOG_ERROR,      /* major failures (not necessarily fatal) */
        GF_LOG_WARNING,    /* info about normal operation */
        GF_LOG_NOTICE,
        GF_LOG_INFO,       /* Normal information */
#define GF_LOG_NORMAL      GF_LOG_INFO
        GF_LOG_DEBUG,      /* internal errors */
        GF_LOG_TRACE,      /* full trace of operation */
} gf_loglevel_t;

extern gf_loglevel_t gf_log_loglevel;
extern char gf_log_xl_log_set;

#define FMT_WARN(fmt...) do { if (0) printf (fmt); } while (0)

#define gf_log(dom, levl, fmt...) do {                                  \
                FMT_WARN (fmt);                                         \
                                                                        \
                if ((levl > gf_log_loglevel) && !gf_log_xl_log_set)     \
                        break;                                          \
                _gf_log (dom, __FILE__, __FUNCTION__, __LINE__,         \
                         levl, ##fmt);                                  \
        } while (0)

#define gf_log_callingfn(dom, levl, fmt...) do {                        \
                FMT_WARN (fmt);                                         \
                                                                        \
                if ((levl > gf_log_loglevel) && !gf_log_xl_log_set)     \
                        break;                                          \
                _gf_log_callingfn (dom, __FILE__, __FUNCTION__, __LINE__, \
                                   levl, ##fmt);                        \
        } while (0)


/* No malloc or calloc should be called in this function */
#define gf_log_nomem(dom, levl, size) do {                              \
                if ((levl > gf_log_loglevel) && !gf_log_xl_log_set)     \
                        break;                                          \
                _gf_log_nomem (dom, __FILE__, __FUNCTION__, __LINE__,   \
                               levl, size);                             \
        } while (0)


/* Log once in GF_UNIVERSAL_ANSWER times */
#define GF_LOG_OCCASIONALLY(var, args...) if (!(var++%GF_UNIVERSAL_ANSWER)) { \
                gf_log (args);                                          \
        }


void gf_log_logrotate (int signum);

void gf_log_globals_init (void);
int gf_log_init (const char *filename);
void gf_log_cleanup (void);

int _gf_log (const char *domain, const char *file, const char *function,
             int32_t line, gf_loglevel_t level, const char *fmt, ...);
int _gf_log_callingfn (const char *domain, const char *file, const char *function,
                       int32_t line, gf_loglevel_t level, const char *fmt, ...);

int _gf_log_nomem (const char *domain, const char *file,
                   const char *function, int line, gf_loglevel_t level,
                   size_t size);

int gf_log_from_client (const char *msg, char *identifier);

void gf_log_lock (void);
void gf_log_unlock (void);

void gf_log_disable_syslog (void);
void gf_log_enable_syslog (void);
gf_loglevel_t gf_log_get_loglevel (void);
void gf_log_set_loglevel (gf_loglevel_t level);
gf_loglevel_t gf_log_get_xl_loglevel (void *xl);
void gf_log_set_xl_loglevel (void *xl, gf_loglevel_t level);

#define GF_DEBUG(xl, format, args...)                           \
        gf_log ((xl)->name, GF_LOG_DEBUG, format, ##args)
#define GF_INFO(xl, format, args...)                            \
        gf_log ((xl)->name, GF_LOG_INFO, format, ##args)
#define GF_WARNING(xl, format, args...)                         \
        gf_log ((xl)->name, GF_LOG_WARNING, format, ##args)
#define GF_ERROR(xl, format, args...)                           \
        gf_log ((xl)->name, GF_LOG_ERROR, format, ##args)

int gf_cmd_log (const char *domain, const char *fmt, ...);

int gf_cmd_log_init (const char *filename);
#endif /* __LOGGING_H__ */
