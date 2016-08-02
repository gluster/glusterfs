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

#include <sys/time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include "list.h"

#ifdef GF_DARWIN_HOST_OS
#define GF_PRI_FSBLK       "u"
#define GF_PRI_DEV         PRId32
#define GF_PRI_INODE       PRIu64
#define GF_PRI_NLINK       PRIu32
#define GF_PRI_SECOND      "ld"
#define GF_PRI_SUSECONDS   "06d"
#define GF_PRI_USEC        "d"
#else
#define GF_PRI_FSBLK       PRIu64
#define GF_PRI_DEV         PRIu64
#define GF_PRI_INODE       PRIu64
#define GF_PRI_NLINK       PRIu32
#define GF_PRI_SECOND      "lu"
#define GF_PRI_SUSECONDS   "06ld"
#define GF_PRI_USEC        "ld"
#endif
#define GF_PRI_BLKSIZE     PRId32
#define GF_PRI_SIZET       "zu"

#ifdef GF_DARWIN_HOST_OS
#define GF_PRI_TIME "ld"
#else
#define GF_PRI_TIME PRIu64
#endif

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

#define GF_LOG_FORMAT_NO_MSG_ID       "no-msg-id"
#define GF_LOG_FORMAT_WITH_MSG_ID     "with-msg-id"

#define GF_LOGGER_GLUSTER_LOG         "gluster-log"
#define GF_LOGGER_SYSLOG              "syslog"

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

/* format for the logs */
typedef enum {
        gf_logformat_traditional = 0, /* Format as in gluster 3.5 */
        gf_logformat_withmsgid, /* Format enhanced with MsgID, ident, errstr */
        gf_logformat_cee /* log enhanced format in cee */
} gf_log_format_t;

/* log infrastructure to log to */
typedef enum {
        gf_logger_glusterlog = 0, /* locations and files as in gluster 3.5 */
        gf_logger_syslog /* log to (r)syslog, based on (r)syslog conf */
        /* NOTE: In the future journald, lumberjack, next new thing here */
} gf_log_logger_t;

#define DEFAULT_LOG_FILE_DIRECTORY          DATADIR "/log/glusterfs"
#define DEFAULT_QUOTA_CRAWL_LOG_DIRECTORY   DATADIR "/log/glusterfs/quota_crawl"
#define DEFAULT_LOG_LEVEL                   GF_LOG_INFO

typedef struct gf_log_handle_ {
        pthread_mutex_t   logfile_mutex;
        uint8_t           logrotate;
        uint8_t           cmd_history_logrotate;
        gf_loglevel_t     loglevel;
        int               gf_log_syslog;
        gf_loglevel_t     sys_log_level;
        char             *filename;
        FILE             *logfile;
        FILE             *gf_log_logfile;
        char             *cmd_log_filename;
        FILE             *cmdlogfile;
        gf_log_logger_t   logger;
        gf_log_format_t   logformat;
        char             *ident;
        int               log_control_file_found;
        struct list_head  lru_queue;
        uint32_t          lru_size;
        uint32_t          lru_cur_size;
        uint32_t          timeout;
        pthread_mutex_t   log_buf_lock;
        struct _gf_timer *log_flush_timer;
} gf_log_handle_t;


typedef struct log_buf_ {
        char             *msg;
        uint64_t          msg_id;
        int               errnum;
        struct timeval    oldest;
        struct timeval    latest;
        char             *domain;
        char             *file;
        char             *function;
        int32_t           line;
        gf_loglevel_t     level;
        int              refcount;
        int              graph_id;
        struct list_head msg_list;
} log_buf_t;

void gf_log_globals_init (void *ctx, gf_loglevel_t level);
int gf_log_init (void *data, const char *filename, const char *ident);

void gf_log_logrotate (int signum);

void gf_log_cleanup (void);

/* Internal interfaces to log messages with message IDs */
int _gf_msg (const char *domain, const char *file,
             const char *function, int32_t line, gf_loglevel_t level,
             int errnum, int trace, uint64_t msgid, const char *fmt, ...)
             __attribute__ ((__format__ (__printf__, 9, 10)));

void _gf_msg_backtrace_nomem (gf_loglevel_t level, int stacksize);

int _gf_msg_plain (gf_loglevel_t level, const char *fmt, ...)
                   __attribute__ ((__format__ (__printf__, 2, 3)));

int _gf_msg_plain_nomem (gf_loglevel_t level, const char *msg);

int _gf_msg_vplain (gf_loglevel_t level, const char *fmt, va_list ap);

int _gf_msg_nomem (const char *domain, const char *file,
                   const char *function, int line, gf_loglevel_t level,
                   size_t size);

int _gf_log (const char *domain, const char *file,
             const char *function, int32_t line, gf_loglevel_t level,
             const char *fmt, ...)
             __attribute__ ((__format__ (__printf__, 6, 7)));

int _gf_log_callingfn (const char *domain, const char *file,
                       const char *function, int32_t line, gf_loglevel_t level,
                       const char *fmt, ...)
                       __attribute__ ((__format__ (__printf__, 6, 7)));

int _gf_log_eh (const char *function, const char *fmt, ...);



/* treat GF_LOG_TRACE and GF_LOG_NONE as LOG_DEBUG and
 * other level as is */
#define SET_LOG_PRIO(level, priority) do {                              \
        if (GF_LOG_TRACE == (level) || GF_LOG_NONE == (level)) {        \
                priority = LOG_DEBUG;                                   \
        } else {                                                        \
                priority = (level) - 1;                                 \
        }                                                               \
        } while (0)

/* extract just the file name from the path */
#define GET_FILE_NAME_TO_LOG(file, basename) do {       \
        basename = strrchr ((file), '/');               \
        if (basename)                                   \
                basename++;                             \
        else                                            \
                basename = (file);                      \
        } while (0)

#define PRINT_SIZE_CHECK(ret, label, strsize) do {      \
        if (ret < 0)                            \
                goto label;                     \
        if ((strsize - ret) > 0) {              \
                strsize -= ret;                 \
        } else {                                \
                ret = 0;                        \
                goto label;                     \
        }                                       \
        } while (0)

#define FMT_WARN(fmt...) do { if (0) printf (fmt); } while (0)

/* Interface to log messages with message IDs */
#define gf_msg(dom, levl, errnum, msgid, fmt...) do {           \
                _gf_msg (dom, __FILE__, __FUNCTION__, __LINE__, \
                        levl, errnum, 0, msgid, ##fmt);         \
        } while (0)

/* no frills, no thrills, just a vanilla message, used to print the graph */
#define gf_msg_plain(levl, fmt...) do {         \
                _gf_msg_plain (levl, ##fmt);    \
        } while (0)

#define gf_msg_plain_nomem(levl, msg) do {              \
                _gf_msg_plain_nomem (levl, msg);        \
        } while (0)

#define gf_msg_vplain(levl, fmt, va) do {         \
                _gf_msg_vplain (levl, fmt, va);    \
        } while (0)

#define gf_msg_backtrace_nomem(level, stacksize) do {           \
                _gf_msg_backtrace_nomem (level, stacksize);     \
        } while (0)

#define gf_msg_callingfn(dom, levl, errnum, msgid, fmt...) do { \
                _gf_msg (dom, __FILE__, __FUNCTION__, __LINE__, \
                        levl, errnum, 1, msgid, ##fmt);         \
        } while (0)

/* No malloc or calloc should be called in this function */
#define gf_msg_nomem(dom, levl, size) do {                              \
                _gf_msg_nomem (dom, __FILE__, __FUNCTION__, __LINE__,   \
                                levl, size);                            \
        } while (0)

/* Debug or trace messages do not need message IDs as these are more developer
 * related. Hence, the following abstractions are provided for the same */
#define gf_msg_debug(dom, errnum, fmt...) do {                          \
                _gf_msg (dom, __FILE__, __FUNCTION__, __LINE__,         \
                        GF_LOG_DEBUG, errnum, 0, 0, ##fmt);             \
        } while (0)

#define gf_msg_trace(dom, errnum, fmt...) do {                          \
                _gf_msg (dom, __FILE__, __FUNCTION__, __LINE__,         \
                        GF_LOG_TRACE, errnum, 0, 0, ##fmt);             \
        } while (0)

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


/* Log once in GF_UNIVERSAL_ANSWER times */
#define GF_LOG_OCCASIONALLY(var, args...) if (!(var++%GF_UNIVERSAL_ANSWER)) { \
                gf_log (args);                                          \
        }

void gf_log_disable_syslog (void);
void gf_log_enable_syslog (void);
gf_loglevel_t gf_log_get_loglevel (void);
void gf_log_set_loglevel (gf_loglevel_t level);
void gf_log_flush (void);
gf_loglevel_t gf_log_get_xl_loglevel (void *xl);
void gf_log_set_xl_loglevel (void *xl, gf_loglevel_t level);

int gf_cmd_log (const char *domain, const char *fmt, ...)
                __attribute__ ((__format__ (__printf__, 2, 3)));

int gf_cmd_log_init (const char *filename);

void set_sys_log_level (gf_loglevel_t level);

int gf_log_fini(void *data);

void
gf_log_set_logger (gf_log_logger_t logger);

void
gf_log_set_logformat (gf_log_format_t format);

void
gf_log_set_log_buf_size (uint32_t buf_size);

void
gf_log_set_log_flush_timeout (uint32_t timeout);

struct _glusterfs_ctx;

void
gf_log_flush_msgs (struct _glusterfs_ctx *ctx);

int
gf_log_inject_timer_event (struct _glusterfs_ctx *ctx);

void
gf_log_disable_suppression_before_exit (struct _glusterfs_ctx *ctx);

#define GF_DEBUG(xl, format, args...)                           \
        gf_log ((xl)->name, GF_LOG_DEBUG, format, ##args)
#define GF_INFO(xl, format, args...)                            \
        gf_log ((xl)->name, GF_LOG_INFO, format, ##args)
#define GF_WARNING(xl, format, args...)                         \
        gf_log ((xl)->name, GF_LOG_WARNING, format, ##args)
#define GF_ERROR(xl, format, args...)                           \
        gf_log ((xl)->name, GF_LOG_ERROR, format, ##args)

#endif /* __LOGGING_H__ */
