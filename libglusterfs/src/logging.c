/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
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

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <locale.h>
#include <string.h>
#include <stdlib.h>

#ifdef GF_USE_SYSLOG
#include <libintl.h>
#include <syslog.h>
#include <sys/stat.h>
#include "gf-error-codes.h"

#define GF_JSON_MSG_LENGTH      8192
#define GF_SYSLOG_CEE_FORMAT    \
        "@cee: {\"msg\": \"%s\", \"gf_code\": \"%u\", \"gf_message\": \"%s\"}"
#define GF_LOG_CONTROL_FILE     "/etc/glusterfs/logger.conf"
#endif /* GF_USE_SYSLOG */

#include "xlator.h"
#include "logging.h"
#include "defaults.h"
#include "glusterfs.h"

#ifdef GF_LINUX_HOST_OS
#include <syslog.h>
#endif

#ifdef HAVE_BACKTRACE
#include <execinfo.h>
#endif

/* Ideally this should get moved to logging.h */
struct _msg_queue {
        struct list_head msgs;
};

struct _log_msg {
        const char *msg;
        struct list_head queue;
};

void
gf_log_logrotate (int signum)
{
        THIS->ctx->log.logrotate = 1;
}

void
gf_log_enable_syslog (void)
{
        THIS->ctx->log.gf_log_syslog = 1;
}

void
gf_log_disable_syslog (void)
{
        THIS->ctx->log.gf_log_syslog = 0;
}

gf_loglevel_t
gf_log_get_loglevel (void)
{
        return THIS->ctx->log.loglevel;
}

void
gf_log_set_loglevel (gf_loglevel_t level)
{
        THIS->ctx->log.loglevel = level;
}


gf_loglevel_t
gf_log_get_xl_loglevel (void *this)
{
        xlator_t *xl = this;
        if (!xl)
                return 0;
        return xl->loglevel;
}

void
gf_log_set_xl_loglevel (void *this, gf_loglevel_t level)
{
        xlator_t *xl = this;
        if (!xl)
                return;
        xl->ctx->log.gf_log_xl_log_set = 1;
        xl->loglevel = level;
}

void
gf_log_globals_fini (void)
{
        pthread_mutex_destroy (&THIS->ctx->log.logfile_mutex);
}

/** gf_log_fini - function to perform the cleanup of the log information
 * @data - glusterfs context
 * @return: success: 0
 *          failure: -1
 */
int
gf_log_fini (void *data)
{
        glusterfs_ctx_t *ctx = data;
        int ret = 0;

        if (ctx == NULL) {
	        ret = -1;
                goto out;
        }

        pthread_mutex_lock (&ctx->log.logfile_mutex);
        {
                if (ctx->log.logfile) {
                        if (fclose (ctx->log.logfile) != 0)
                                ret = -1;
                        /* Logfile needs to be set to NULL, so that any
                           call to gf_log after calling gf_log_fini, will
                           log the message to stderr.
                        */
                        ctx->log.logfile = NULL;
                }
        }
        pthread_mutex_unlock (&ctx->log.logfile_mutex);

 out:
        return ret;
}

#ifdef GF_USE_SYSLOG
/**
 * gf_get_error_message -function to get error message for given error code
 * @error_code: error code defined by log book
 *
 * @return: success: string
 *          failure: NULL
 */
const char *
gf_get_error_message (int error_code) {
        return _gf_get_message (error_code);
}


/**
 * gf_openlog -function to open syslog specific to gluster based on
 *             existence of file /etc/glusterfs/logger.conf
 * @ident:    optional identification string similar to openlog()
 * @option:   optional value to option to openlog().  Passing -1 uses
 *            'LOG_PID | LOG_NDELAY' as default
 * @facility: optional facility code similar to openlog().  Passing -1
 *            uses LOG_DAEMON as default
 *
 * @return: void
 */
void
gf_openlog (const char *ident, int option, int facility)
{
        int _option = option;
        int _facility = facility;

        if (-1 == _option) {
                _option = LOG_PID | LOG_NDELAY;
        }
        if (-1 == _facility) {
                _facility = LOG_LOCAL1;
        }

        setlocale(LC_ALL, "");
        bindtextdomain("gluster", "/usr/share/locale");
        textdomain("gluster");

        openlog(ident, _option, _facility);
}


/**
 * _json_escape -function to convert string to json encoded string
 * @str: input string
 * @buf: buffer to store encoded string
 * @len: length of @buf
 *
 * @return: success: last unprocessed character position by pointer in @str
 *          failure: NULL
 *
 * Internal function. Heavily inspired by _ul_str_escape() function in
 * libumberlog
 *
 * Sample output:
 * [1] str = "devel error"
 *     buf = "devel error"
 * [2] str = "devel	error"
 *     buf = "devel\terror"
 * [3] str = "I/O error on "/tmp/foo" file"
 *     buf = "I/O error on \"/tmp/foo\" file"
 * [4] str = "I/O erroron /tmp/bar file"
 *     buf = "I/O error\u001bon /tmp/bar file"
 *
 */
char *
_json_escape(const char *str, char *buf, size_t len)
{
        static const unsigned char json_exceptions[UCHAR_MAX + 1] =
                {
                        [0x01] = 1, [0x02] = 1, [0x03] = 1, [0x04] = 1,
                        [0x05] = 1, [0x06] = 1, [0x07] = 1, [0x08] = 1,
                        [0x09] = 1, [0x0a] = 1, [0x0b] = 1, [0x0c] = 1,
                        [0x0d] = 1, [0x0e] = 1, [0x0f] = 1, [0x10] = 1,
                        [0x11] = 1, [0x12] = 1, [0x13] = 1, [0x14] = 1,
                        [0x15] = 1, [0x16] = 1, [0x17] = 1, [0x18] = 1,
                        [0x19] = 1, [0x1a] = 1, [0x1b] = 1, [0x1c] = 1,
                        [0x1d] = 1, [0x1e] = 1, [0x1f] = 1,
                        ['\\'] = 1, ['"'] = 1
                };
        static const char  json_hex_chars[16] = "0123456789abcdef";
        unsigned char     *p = NULL;
        size_t             pos = 0;

        if (!str || !buf || len <= 0) {
                return NULL;
        }

        for (p = (unsigned char *)str;
             *p && (pos + 1) < len;
             p++)
        {
                if (json_exceptions[*p] == 0) {
                        buf[pos++] = *p;
                        continue;
                }

                if ((pos + 2) >= len) {
                        break;
                }

                switch (*p)
                {
                case '\b':
                        buf[pos++] = '\\';
                        buf[pos++] = 'b';
                        break;
                case '\n':
                        buf[pos++] = '\\';
                        buf[pos++] = 'n';
                        break;
                case '\r':
                        buf[pos++] = '\\';
                        buf[pos++] = 'r';
                        break;
                case '\t':
                        buf[pos++] = '\\';
                        buf[pos++] = 't';
                        break;
                case '\\':
                        buf[pos++] = '\\';
                        buf[pos++] = '\\';
                        break;
                case '"':
                        buf[pos++] = '\\';
                        buf[pos++] = '"';
                        break;
                default:
                        if ((pos + 6) >= len) {
                                buf[pos] = '\0';
                                return (char *)p;
                        }
                        buf[pos++] = '\\';
                        buf[pos++] = 'u';
                        buf[pos++] = '0';
                        buf[pos++] = '0';
                        buf[pos++] = json_hex_chars[(*p) >> 4];
                        buf[pos++] = json_hex_chars[(*p) & 0xf];
                        break;
                }
        }

        buf[pos] = '\0';
        return (char *)p;
}


/**
 * gf_syslog -function to submit message to syslog specific to gluster
 * @error_code:        error code defined by log book
 * @facility_priority: facility_priority of syslog()
 * @format:            optional format string to syslog()
 *
 * @return: void
 */
void
gf_syslog (int error_code, int facility_priority, char *format, ...)
{
        char       *msg = NULL;
        char        json_msg[GF_JSON_MSG_LENGTH];
        GF_UNUSED char       *p = NULL;
        const char *error_message = NULL;
        char        json_error_message[GF_JSON_MSG_LENGTH];
        va_list     ap;

        error_message = gf_get_error_message (error_code);

        va_start (ap, format);
        if (format) {
                vasprintf (&msg, format, ap);
                p = _json_escape (msg, json_msg, GF_JSON_MSG_LENGTH);
                if (error_message) {
                        p = _json_escape (error_message, json_error_message,
                                          GF_JSON_MSG_LENGTH);
                        syslog (facility_priority, GF_SYSLOG_CEE_FORMAT,
                                json_msg, error_code, json_error_message);
                } else {
                        /* ignore the error code because no error message for it
                           and use normal syslog */
                        syslog (facility_priority, "%s", msg);
                }
                free (msg);
        } else {
                if (error_message) {
                        /* no user message: treat error_message as msg */
                        syslog (facility_priority, GF_SYSLOG_CEE_FORMAT,
                                json_error_message, error_code,
                                json_error_message);
                } else {
                        /* cannot produce log as neither error_message nor
                           msg available */
                }
        }
        va_end (ap);
}
#endif /* GF_USE_SYSLOG */

void
gf_log_globals_init (void *data)
{
        glusterfs_ctx_t *ctx = data;

        pthread_mutex_init (&ctx->log.logfile_mutex, NULL);

        ctx->log.loglevel         = GF_LOG_INFO;
        ctx->log.gf_log_syslog    = 1;
        ctx->log.sys_log_level    = GF_LOG_CRITICAL;

#ifndef GF_USE_SYSLOG
#ifdef GF_LINUX_HOST_OS
        /* For the 'syslog' output. one can grep 'GlusterFS' in syslog
           for serious logs */
        openlog ("GlusterFS", LOG_PID, LOG_DAEMON);
#endif
#endif
}

int
gf_log_init (void *data, const char *file, const char *ident)
{
        glusterfs_ctx_t *ctx = NULL;
        int     fd = -1;

        ctx = data;

#if defined(GF_USE_SYSLOG)
        {
                /* use default ident and option */
                /* TODO: make FACILITY configurable than LOG_DAEMON */
                struct stat buf;

                if (stat (GF_LOG_CONTROL_FILE, &buf) == 0) {
                        /* use syslog logging */
                        ctx->log.log_control_file_found = 1;
                        if (ident) {
                                /* we need to keep this value as */
                                /* syslog uses it on every logging */
                                ctx->log.ident = gf_strdup (ident);
                                gf_openlog (ctx->log.ident, -1, LOG_DAEMON);
                        } else {
                                gf_openlog (NULL, -1, LOG_DAEMON);
                        }
                } else {
                        /* use old style logging */
                        ctx->log.log_control_file_found = 0;
                }
        }
#endif

        if (!file){
                fprintf (stderr, "ERROR: no filename specified\n");
                return -1;
        }

        if (strcmp (file, "-") == 0) {
		file = "/dev/stderr";
        }

        ctx->log.filename = gf_strdup (file);
        if (!ctx->log.filename) {
                fprintf (stderr, "ERROR: updating log-filename failed: %s\n",
                         strerror (errno));
                return -1;
        }

        fd = open (file, O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
        if (fd < 0) {
                fprintf (stderr, "ERROR: failed to create logfile \"%s\" (%s)\n",
                         file, strerror (errno));
                return -1;
        }
        close (fd);

        ctx->log.logfile = fopen (file, "a");
        if (!ctx->log.logfile){
                fprintf (stderr, "ERROR: failed to open logfile \"%s\" (%s)\n",
                         file, strerror (errno));
                return -1;
        }

        ctx->log.gf_log_logfile = ctx->log.logfile;

        return 0;
}

void
set_sys_log_level (gf_loglevel_t level)
{
        THIS->ctx->log.sys_log_level = level;
}

int
_gf_log_nomem (const char *domain, const char *file,
               const char *function, int line, gf_loglevel_t level,
               size_t size)
{
        const char     *basename        = NULL;
        xlator_t       *this            = NULL;
        struct timeval  tv              = {0,};
        int             ret             = 0;
        char            msg[8092]       = {0,};
        char            timestr[256]    = {0,};
        char            callstr[4096]   = {0,};
        glusterfs_ctx_t *ctx = NULL;

        this = THIS;
        ctx = this->ctx;

        if (ctx->log.gf_log_xl_log_set) {
                if (this->loglevel && (level > this->loglevel))
                        goto out;
        }
        if (level > ctx->log.loglevel)
                goto out;

        static char *level_strings[] = {"",  /* NONE */
                                        "M", /* EMERGENCY */
                                        "A", /* ALERT */
                                        "C", /* CRITICAL */
                                        "E", /* ERROR */
                                        "W", /* WARNING */
                                        "N", /* NOTICE */
                                        "I", /* INFO */
                                        "D", /* DEBUG */
                                        "T", /* TRACE */
                                        ""};

        if (!domain || !file || !function) {
                fprintf (stderr,
                         "logging: %s:%s():%d: invalid argument\n",
                         __FILE__, __PRETTY_FUNCTION__, __LINE__);
                return -1;
        }

        basename = strrchr (file, '/');
        if (basename)
                basename++;
        else
                basename = file;

#if HAVE_BACKTRACE
        /* Print 'calling function' */
        do {
                void *array[5];
                char **callingfn = NULL;
                size_t bt_size = 0;

                bt_size = backtrace (array, 5);
                if (bt_size)
                        callingfn = backtrace_symbols (&array[2], bt_size-2);
                if (!callingfn)
                        break;

                if (bt_size == 5)
                        snprintf (callstr, 4096, "(-->%s (-->%s (-->%s)))",
                                  callingfn[2], callingfn[1], callingfn[0]);
                if (bt_size == 4)
                        snprintf (callstr, 4096, "(-->%s (-->%s))",
                                  callingfn[1], callingfn[0]);
                if (bt_size == 3)
                        snprintf (callstr, 4096, "(-->%s)", callingfn[0]);

                free (callingfn);
        } while (0);
#endif /* HAVE_BACKTRACE */

#if defined(GF_USE_SYSLOG)
        if (ctx->log.log_control_file_found)
        {
                int priority;
                /* treat GF_LOG_TRACE and GF_LOG_NONE as LOG_DEBUG and
                   other level as is */
                if (GF_LOG_TRACE == level || GF_LOG_NONE == level) {
                        priority = LOG_DEBUG;
                } else {
                        priority = level - 1;
                }
                gf_syslog (GF_ERR_DEV, priority,
                           "[%s:%d:%s] %s %s: no memory "
                           "available for size (%"GF_PRI_SIZET")",
                           basename, line, function, callstr, domain,
                           size);
                goto out;
        }
#endif /* GF_USE_SYSLOG */
        ret = gettimeofday (&tv, NULL);
        if (-1 == ret)
                goto out;
        gf_time_fmt (timestr, sizeof timestr, tv.tv_sec, gf_timefmt_FT);
        snprintf (timestr + strlen (timestr), sizeof timestr - strlen (timestr),
                  ".%"GF_PRI_SUSECONDS, tv.tv_usec);

        ret = sprintf (msg, "[%s] %s [%s:%d:%s] %s %s: no memory "
                       "available for size (%"GF_PRI_SIZET")",
                       timestr, level_strings[level],
                       basename, line, function, callstr,
                       domain, size);
        if (-1 == ret) {
                goto out;
        }

        pthread_mutex_lock (&ctx->log.logfile_mutex);
        {
                if (ctx->log.logfile) {
                        fprintf (ctx->log.logfile, "%s\n", msg);
                } else {
                        fprintf (stderr, "%s\n", msg);
                }

#ifdef GF_LINUX_HOST_OS
                /* We want only serious log in 'syslog', not our debug
                   and trace logs */
                if (ctx->log.gf_log_syslog && level &&
                    (level <= ctx->log.sys_log_level))
                        syslog ((level-1), "%s\n", msg);
#endif
        }

        pthread_mutex_unlock (&ctx->log.logfile_mutex);
out:
        return ret;
 }

int
_gf_log_callingfn (const char *domain, const char *file, const char *function,
                   int line, gf_loglevel_t level, const char *fmt, ...)
{
        const char     *basename        = NULL;
        xlator_t       *this            = NULL;
        char           *str1            = NULL;
        char           *str2            = NULL;
        char           *msg             = NULL;
        char            timestr[256]    = {0,};
        char            callstr[4096]   = {0,};
        struct timeval  tv              = {0,};
        size_t          len             = 0;
        int             ret             = 0;
        va_list         ap;
        glusterfs_ctx_t *ctx = NULL;

        this = THIS;
        ctx = this->ctx;

        if (ctx->log.gf_log_xl_log_set) {
                if (this->loglevel && (level > this->loglevel))
                        goto out;
        }
        if (level > ctx->log.loglevel)
                goto out;

        static char *level_strings[] = {"",  /* NONE */
                                        "M", /* EMERGENCY */
                                        "A", /* ALERT */
                                        "C", /* CRITICAL */
                                        "E", /* ERROR */
                                        "W", /* WARNING */
                                        "N", /* NOTICE */
                                        "I", /* INFO */
                                        "D", /* DEBUG */
                                        "T", /* TRACE */
                                        ""};

        if (!domain || !file || !function || !fmt) {
                fprintf (stderr,
                         "logging: %s:%s():%d: invalid argument\n",
                         __FILE__, __PRETTY_FUNCTION__, __LINE__);
                return -1;
        }

        basename = strrchr (file, '/');
        if (basename)
                basename++;
        else
                basename = file;

#if HAVE_BACKTRACE
        /* Print 'calling function' */
        do {
                void *array[5];
                char **callingfn = NULL;
                size_t size = 0;

                size = backtrace (array, 5);
                if (size)
                        callingfn = backtrace_symbols (&array[2], size-2);
                if (!callingfn)
                        break;

                if (size == 5)
                        snprintf (callstr, 4096, "(-->%s (-->%s (-->%s)))",
                                  callingfn[2], callingfn[1], callingfn[0]);
                if (size == 4)
                        snprintf (callstr, 4096, "(-->%s (-->%s))",
                                  callingfn[1], callingfn[0]);
                if (size == 3)
                        snprintf (callstr, 4096, "(-->%s)", callingfn[0]);

                free (callingfn);
        } while (0);
#endif /* HAVE_BACKTRACE */

#if defined(GF_USE_SYSLOG)
        if (ctx->log.log_control_file_found)
        {
                int priority;
                /* treat GF_LOG_TRACE and GF_LOG_NONE as LOG_DEBUG and
                   other level as is */
                if (GF_LOG_TRACE == level || GF_LOG_NONE == level) {
                        priority = LOG_DEBUG;
                } else {
                        priority = level - 1;
                }

                va_start (ap, fmt);
                vasprintf (&str2, fmt, ap);
                va_end (ap);

                gf_syslog (GF_ERR_DEV, priority,
                           "[%s:%d:%s] %s %d-%s: %s",
                           basename, line, function,
                           callstr,
                           ((this->graph) ? this->graph->id:0), domain,
                           str2);

                goto out;
        }
#endif /* GF_USE_SYSLOG */
        ret = gettimeofday (&tv, NULL);
        if (-1 == ret)
                goto out;
        va_start (ap, fmt);
        gf_time_fmt (timestr, sizeof timestr, tv.tv_sec, gf_timefmt_FT);
        snprintf (timestr + strlen (timestr), sizeof timestr - strlen (timestr),
                  ".%"GF_PRI_SUSECONDS, tv.tv_usec);

        ret = gf_asprintf (&str1, "[%s] %s [%s:%d:%s] %s %d-%s: ",
                           timestr, level_strings[level],
                           basename, line, function, callstr,
                           ((this->graph) ? this->graph->id:0), domain);
        if (-1 == ret) {
                goto out;
        }

        ret = vasprintf (&str2, fmt, ap);
        if (-1 == ret) {
                goto out;
        }

        va_end (ap);

        len = strlen (str1);
        msg = GF_MALLOC (len + strlen (str2) + 1, gf_common_mt_char);

        strcpy (msg, str1);
        strcpy (msg + len, str2);

        pthread_mutex_lock (&ctx->log.logfile_mutex);
        {
                if (ctx->log.logfile) {
                        fprintf (ctx->log.logfile, "%s\n", msg);
                } else {
                        fprintf (stderr, "%s\n", msg);
                }

#ifdef GF_LINUX_HOST_OS
                /* We want only serious log in 'syslog', not our debug
                   and trace logs */
                if (ctx->log.gf_log_syslog && level &&
                    (level <= ctx->log.sys_log_level))
                        syslog ((level-1), "%s\n", msg);
#endif
        }

        pthread_mutex_unlock (&ctx->log.logfile_mutex);

out:
        GF_FREE (msg);

        GF_FREE (str1);

        FREE (str2);

        return ret;
}

int
_gf_log (const char *domain, const char *file, const char *function, int line,
         gf_loglevel_t level, const char *fmt, ...)
{
        const char    *basename = NULL;
        FILE          *new_logfile = NULL;
        va_list        ap;
        char           timestr[256] = {0,};
        struct timeval tv = {0,};
        char          *str1 = NULL;
        char          *str2 = NULL;
        char          *msg  = NULL;
        size_t         len  = 0;
        int            ret  = 0;
        int            fd   = -1;
        xlator_t      *this = NULL;
        glusterfs_ctx_t *ctx = NULL;

        this = THIS;
        ctx = this->ctx;

        if (ctx->log.gf_log_xl_log_set) {
                if (this->loglevel && (level > this->loglevel))
                        goto out;
        }
        if (level > ctx->log.loglevel)
                goto out;

        static char *level_strings[] = {"",  /* NONE */
                                        "M", /* EMERGENCY */
                                        "A", /* ALERT */
                                        "C", /* CRITICAL */
                                        "E", /* ERROR */
                                        "W", /* WARNING */
                                        "N", /* NOTICE */
                                        "I", /* INFO */
                                        "D", /* DEBUG */
                                        "T", /* TRACE */
                                        ""};

        if (!domain || !file || !function || !fmt) {
                fprintf (stderr,
                         "logging: %s:%s():%d: invalid argument\n",
                         __FILE__, __PRETTY_FUNCTION__, __LINE__);
                return -1;
        }

        basename = strrchr (file, '/');
        if (basename)
                basename++;
        else
                basename = file;

#if defined(GF_USE_SYSLOG)
        if (ctx->log.log_control_file_found)
        {
                int priority;
                /* treat GF_LOG_TRACE and GF_LOG_NONE as LOG_DEBUG and
                   other level as is */
                if (GF_LOG_TRACE == level || GF_LOG_NONE == level) {
                        priority = LOG_DEBUG;
                } else {
                        priority = level - 1;
                }

                va_start (ap, fmt);
                vasprintf (&str2, fmt, ap);
                va_end (ap);

                gf_syslog (GF_ERR_DEV, priority,
                           "[%s:%d:%s] %d-%s: %s",
                           basename, line, function,
                           ((this->graph) ? this->graph->id:0), domain, str2);
                goto err;
        }
#endif /* GF_USE_SYSLOG */

        if (ctx->log.logrotate) {
                ctx->log.logrotate = 0;

                fd = open (ctx->log.filename,
                           O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
                if (fd < 0) {
                        gf_log ("logrotate", GF_LOG_ERROR,
                                "%s", strerror (errno));
                        return -1;
                }
                close (fd);

                new_logfile = fopen (ctx->log.filename, "a");
                if (!new_logfile) {
                        gf_log ("logrotate", GF_LOG_CRITICAL,
                                "failed to open logfile %s (%s)",
                                ctx->log.filename, strerror (errno));
                        goto log;
                }

                pthread_mutex_lock (&ctx->log.logfile_mutex);
                {
                        if (ctx->log.logfile)
                                fclose (ctx->log.logfile);

                        ctx->log.gf_log_logfile = ctx->log.logfile = new_logfile;
                }
                pthread_mutex_unlock (&ctx->log.logfile_mutex);

        }

log:
        ret = gettimeofday (&tv, NULL);
        if (-1 == ret)
                goto out;
        va_start (ap, fmt);
        gf_time_fmt (timestr, sizeof timestr, tv.tv_sec, gf_timefmt_FT);
        snprintf (timestr + strlen (timestr), sizeof timestr - strlen (timestr),
                  ".%"GF_PRI_SUSECONDS, tv.tv_usec);

        ret = gf_asprintf (&str1, "[%s] %s [%s:%d:%s] %d-%s: ",
                           timestr, level_strings[level],
                           basename, line, function,
                           ((this->graph)?this->graph->id:0), domain);
        if (-1 == ret) {
                goto err;
        }

        ret = vasprintf (&str2, fmt, ap);
        if (-1 == ret) {
                goto err;
        }

        va_end (ap);

        len = strlen (str1);
        msg = GF_MALLOC (len + strlen (str2) + 1, gf_common_mt_char);

        strcpy (msg, str1);
        strcpy (msg + len, str2);

        pthread_mutex_lock (&ctx->log.logfile_mutex);
        {

                if (ctx->log.logfile) {
                        fprintf (ctx->log.logfile, "%s\n", msg);
                        fflush (ctx->log.logfile);
                } else {
                        fprintf (stderr, "%s\n", msg);
                        fflush (stderr);
                }

#ifdef GF_LINUX_HOST_OS
                /* We want only serious log in 'syslog', not our debug
                   and trace logs */
                if (ctx->log.gf_log_syslog && level &&
                    (level <= ctx->log.sys_log_level))
                        syslog ((level-1), "%s\n", msg);
#endif
        }

        pthread_mutex_unlock (&ctx->log.logfile_mutex);

err:
        GF_FREE (msg);

        GF_FREE (str1);

        FREE (str2);

out:
        return (0);
}

int
_gf_log_eh (const char *function, const char *fmt, ...)
{
        int          ret   = -1;
        va_list      ap;
        char         *str1 = NULL;
        char         *str2 = NULL;
        char         *msg  = NULL;
        xlator_t     *this = NULL;

        this = THIS;

        ret = gf_asprintf (&str1, "[%d] %s: ",
                           ((this->graph)?this->graph->id:0),
                           function);
        if (-1 == ret) {
                goto out;
        }

        va_start (ap, fmt);

        ret = vasprintf (&str2, fmt, ap);
        if (-1 == ret) {
                goto out;
        }

        va_end (ap);

        msg = GF_MALLOC (strlen (str1) + strlen (str2) + 1, gf_common_mt_char);
        if (!msg) {
                ret = -1;
                goto out;
        }

        strcpy (msg, str1);
        strcat (msg, str2);

        ret = eh_save_history (this->history, msg);

out:
        GF_FREE (str1);

        /* Use FREE instead of GF_FREE since str2 was allocated by vasprintf */
        if (str2)
                FREE (str2);

         return ret;
}

int
gf_cmd_log_init (const char *filename)
{
        int         fd   = -1;
        xlator_t   *this = NULL;
        glusterfs_ctx_t *ctx = NULL;

        this = THIS;
        ctx  = this->ctx;

        if (!filename){
                gf_log (this->name, GF_LOG_CRITICAL, "gf_cmd_log_init: no "
                        "filename specified\n");
                return -1;
        }

        ctx->log.cmd_log_filename = gf_strdup (filename);
        if (!ctx->log.cmd_log_filename) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "gf_cmd_log_init: strdup error\n");
                return -1;
        }
        /* close and reopen cmdlogfile for log rotate*/
        if (ctx->log.cmdlogfile) {
                fclose (ctx->log.cmdlogfile);
                ctx->log.cmdlogfile = NULL;
        }

        fd = open (ctx->log.cmd_log_filename,
                   O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
        if (fd < 0) {
                gf_log (this->name, GF_LOG_CRITICAL,
                        "%s", strerror (errno));
                return -1;
        }
        close (fd);

        ctx->log.cmdlogfile = fopen (ctx->log.cmd_log_filename, "a");
        if (!ctx->log.cmdlogfile){
                gf_log (this->name, GF_LOG_CRITICAL,
                        "gf_cmd_log_init: failed to open logfile \"%s\" "
                        "(%s)\n", ctx->log.cmd_log_filename, strerror (errno));
                return -1;
        }
        return 0;
}

int
gf_cmd_log (const char *domain, const char *fmt, ...)
{
        va_list        ap;
        char           timestr[64];
        struct timeval tv = {0,};
        char          *str1 = NULL;
        char          *str2 = NULL;
        char          *msg  = NULL;
        size_t         len  = 0;
        int            ret  = 0;
        glusterfs_ctx_t *ctx = NULL;

        ctx = THIS->ctx;
        if (!ctx->log.cmdlogfile)
                return -1;


        if (!domain || !fmt) {
                gf_log ("glusterd", GF_LOG_TRACE,
                        "logging: invalid argument\n");
                return -1;
        }

        ret = gettimeofday (&tv, NULL);
        if (ret == -1)
                goto out;
        va_start (ap, fmt);
        gf_time_fmt (timestr, sizeof timestr, tv.tv_sec, gf_timefmt_FT);
        snprintf (timestr + strlen (timestr), 256 - strlen (timestr),
                  ".%"GF_PRI_SUSECONDS, tv.tv_usec);

        ret = gf_asprintf (&str1, "[%s] %s : ",
                           timestr, domain);
        if (ret == -1) {
                goto out;
        }

        ret = vasprintf (&str2, fmt, ap);
        if (ret == -1) {
                goto out;
        }

        va_end (ap);

        len = strlen (str1);
        msg = GF_MALLOC (len + strlen (str2) + 1, gf_common_mt_char);

        strcpy (msg, str1);
        strcpy (msg + len, str2);

        fprintf (ctx->log.cmdlogfile, "%s\n", msg);
        fflush (ctx->log.cmdlogfile);

out:
        GF_FREE (msg);

        GF_FREE (str1);

        FREE (str2);

        return (0);
}
