/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
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
#include <pthread.h>

#ifdef GF_DARWIN_HOST_OS
#define GF_PRI_FSBLK       "u"
#define GF_PRI_DEV         PRId32
#define GF_PRI_NLINK       PRIu16
#define GF_PRI_SUSECONDS   "06d"
#else
#define GF_PRI_FSBLK       PRIu64
#define GF_PRI_DEV         PRIu64
#define GF_PRI_NLINK       PRIu32
#define GF_PRI_SUSECONDS   "06ld"
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
        GF_LOG_DEBUG,      /* internal errors */
        GF_LOG_TRACE,      /* full trace of operation */
} gf_loglevel_t;

#define DEFAULT_LOG_FILE_DIRECTORY            DATADIR "/log/glusterfs"
#define DEFAULT_LOG_LEVEL                     GF_LOG_INFO

typedef struct gf_log_handle_ {
        pthread_mutex_t  logfile_mutex;
        uint8_t          logrotate;
        gf_loglevel_t    loglevel;
        int              gf_log_syslog;
        gf_loglevel_t    sys_log_level;
        char             gf_log_xl_log_set;
        char            *filename;
        FILE            *logfile;
        FILE            *gf_log_logfile;
        char            *cmd_log_filename;
        FILE            *cmdlogfile;
#ifdef GF_USE_SYSLOG
        int              log_control_file_found;
        char            *ident;
#endif /* GF_USE_SYSLOG */

} gf_log_handle_t;

void gf_log_globals_init (void *ctx);
int gf_log_init (void *data, const char *filename, const char *ident);

void gf_log_logrotate (int signum);

void gf_log_cleanup (void);

int _gf_log (const char *domain, const char *file,
             const char *function, int32_t line, gf_loglevel_t level,
             const char *fmt, ...)
             __attribute__ ((__format__ (__printf__, 6, 7)));
int _gf_log_callingfn (const char *domain, const char *file,
                       const char *function, int32_t line, gf_loglevel_t level,
                       const char *fmt, ...)
                       __attribute__ ((__format__ (__printf__, 6, 7)));

int _gf_log_nomem (const char *domain, const char *file,
                   const char *function, int line, gf_loglevel_t level,
                   size_t size);

int _gf_log_eh (const char *function, const char *fmt, ...);



#define FMT_WARN(fmt...) do { if (0) printf (fmt); } while (0)

#define gf_log(dom, levl, fmt...) do {                                  \
                FMT_WARN (fmt);                                         \
                _gf_log (dom, __FILE__, __FUNCTION__, __LINE__,         \
                         levl, ##fmt);                                  \
        } while (0)

#define gf_log_eh(fmt...) do {                                          \
                FMT_WARN (fmt);                                         \
                _gf_log_eh (__FUNCTION__, ##fmt);                        \
        } while (0)

#define gf_log_callingfn(dom, levl, fmt...) do {                        \
                FMT_WARN (fmt);                                         \
                _gf_log_callingfn (dom, __FILE__, __FUNCTION__, __LINE__, \
                                   levl, ##fmt);                        \
        } while (0)


/* No malloc or calloc should be called in this function */
#define gf_log_nomem(dom, levl, size) do {                              \
                _gf_log_nomem (dom, __FILE__, __FUNCTION__, __LINE__,   \
                               levl, size);                             \
        } while (0)


/* Log once in GF_UNIVERSAL_ANSWER times */
#define GF_LOG_OCCASIONALLY(var, args...) if (!(var++%GF_UNIVERSAL_ANSWER)) { \
                gf_log (args);                                          \
        }

void gf_log_disable_syslog (void);
void gf_log_enable_syslog (void);
gf_loglevel_t gf_log_get_loglevel (void);
void gf_log_set_loglevel (gf_loglevel_t level);
gf_loglevel_t gf_log_get_xl_loglevel (void *xl);
void gf_log_set_xl_loglevel (void *xl, gf_loglevel_t level);

int gf_cmd_log (const char *domain, const char *fmt, ...)
                __attribute__ ((__format__ (__printf__, 2, 3)));

int gf_cmd_log_init (const char *filename);

void set_sys_log_level (gf_loglevel_t level);

int gf_log_fini(void *data);

#define GF_DEBUG(xl, format, args...)                           \
        gf_log ((xl)->name, GF_LOG_DEBUG, format, ##args)
#define GF_INFO(xl, format, args...)                            \
        gf_log ((xl)->name, GF_LOG_INFO, format, ##args)
#define GF_WARNING(xl, format, args...)                         \
        gf_log ((xl)->name, GF_LOG_WARNING, format, ##args)
#define GF_ERROR(xl, format, args...)                           \
        gf_log ((xl)->name, GF_LOG_ERROR, format, ##args)

#endif /* __LOGGING_H__ */
