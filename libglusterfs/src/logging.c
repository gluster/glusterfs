/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <locale.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>

#ifdef HAVE_BACKTRACE
#include <execinfo.h>
#else
#include "execinfo_compat.h"
#endif

#include <sys/stat.h>

#include "syscall.h"

#define GF_JSON_MSG_LENGTH      8192
#define GF_SYSLOG_CEE_FORMAT    \
        "@cee: {\"msg\": \"%s\", \"gf_code\": \"%u\", \"gf_message\": \"%s\"}"
#define GF_LOG_CONTROL_FILE     "/etc/glusterfs/logger.conf"
#define GF_LOG_BACKTRACE_DEPTH  5
#define GF_LOG_BACKTRACE_SIZE   4096
#define GF_LOG_TIMESTR_SIZE     256

#include "xlator.h"
#include "logging.h"
#include "defaults.h"
#include "glusterfs.h"
#include "timer.h"
#include "libglusterfs-messages.h"

/* Do not replace gf_log in TEST_LOG with gf_msg, as there is a slight chance
 * that it could lead to an infinite recursion.*/
#define TEST_LOG(__msg, __args ...)                             \
                gf_log ("logging-infra", GF_LOG_DEBUG, __msg, ##__args);

void
gf_log_flush_timeout_cbk (void *data);

int
gf_log_inject_timer_event (glusterfs_ctx_t *ctx);

static void
gf_log_flush_extra_msgs (glusterfs_ctx_t *ctx, uint32_t new);

static char *gf_level_strings[] = {"",  /* NONE */
                "M", /* EMERGENCY */
                "A", /* ALERT */
                "C", /* CRITICAL */
                "E", /* ERROR */
                "W", /* WARNING */
                "N", /* NOTICE */
                "I", /* INFO */
                "D", /* DEBUG */
                "T", /* TRACE */
                ""
};

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
        glusterfs_ctx_t *ctx = NULL;

        ctx = THIS->ctx;

        if (ctx) {
                ctx->log.logrotate = 1;
                ctx->log.cmd_history_logrotate = 1;
        }
}

void
gf_log_enable_syslog (void)
{
        glusterfs_ctx_t *ctx = NULL;

        ctx = THIS->ctx;

        if (ctx)
                ctx->log.gf_log_syslog = 1;
}

void
gf_log_disable_syslog (void)
{
        glusterfs_ctx_t *ctx = NULL;

        ctx = THIS->ctx;

        if (ctx)
                ctx->log.gf_log_syslog = 0;
}

gf_loglevel_t
gf_log_get_loglevel (void)
{
        glusterfs_ctx_t *ctx = NULL;

        ctx = THIS->ctx;

        if (ctx)
                return ctx->log.loglevel;
        else
                /* return global defaults (see gf_log_globals_init) */
                return GF_LOG_INFO;
}

void
gf_log_set_loglevel (gf_loglevel_t level)
{
        glusterfs_ctx_t *ctx = NULL;

        ctx = THIS->ctx;

        if (ctx)
                ctx->log.loglevel = level;
}

void
gf_log_flush (void)
{
        xlator_t        *this           = NULL;
        glusterfs_ctx_t *ctx            = NULL;

        this = THIS;
        ctx = this->ctx;

        if (ctx && ctx->log.logger == gf_logger_glusterlog) {
                pthread_mutex_lock (&ctx->log.logfile_mutex);
                fflush (ctx->log.gf_log_logfile);
                pthread_mutex_unlock (&ctx->log.logfile_mutex);
        }

        return;
}

void
gf_log_set_xl_loglevel (void *this, gf_loglevel_t level)
{
        xlator_t *xl = this;
        if (!xl)
                return;
        xl->loglevel = level;
}

/* TODO: The following get/set functions are yet not invoked from anywhere
 * in the code. The _intention_ is to pass CLI arguments to various daemons
 * that are started, which would then invoke these set APIs as required.
 *
 * glusterd would read the defaults from its .vol file configuration shipped
 * as a part of the packages distributed.
 *
 * For any gluster* daemon that is started the shipped configuration becomes the
 * default, if a volume has to change its logging format or logger, then a
 * gluster CLI is invoked to set this property for the volume in question.
 *
 * The property is maintained by glusterd, and passed to the daemon as a CLI
 * option, IOW persistence of the option is maintained by glusterd persistent
 * storage (i.e .vol file) only
 *
 * care needs to be taken to configure and start daemons based on the versions
 * that supports these features */
gf_log_format_t
gf_log_get_logformat (void)
{
        glusterfs_ctx_t *ctx = NULL;

        ctx = THIS->ctx;

        if (ctx)
                return ctx->log.logformat;
        else
                /* return global defaluts (see gf_log_globals_init) */
                return gf_logformat_withmsgid;
}

void
gf_log_set_logformat (gf_log_format_t format)
{
        glusterfs_ctx_t *ctx = NULL;

        ctx = THIS->ctx;

        if (ctx)
                ctx->log.logformat = format;
}

gf_log_logger_t
gf_log_get_logger (void)
{
        glusterfs_ctx_t *ctx = NULL;

        ctx = THIS->ctx;

        if (ctx)
                return ctx->log.logger;
        else
                /* return global defaluts (see gf_log_globals_init) */
                return gf_logger_glusterlog;
}

void
gf_log_set_logger (gf_log_logger_t logger)
{
        glusterfs_ctx_t *ctx = NULL;

        ctx = THIS->ctx;

        if (ctx)
                ctx->log.logger = logger;
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
gf_log_set_log_buf_size (uint32_t buf_size)
{
        uint32_t          old = 0;
        glusterfs_ctx_t  *ctx = THIS->ctx;

        pthread_mutex_lock (&ctx->log.log_buf_lock);
        {
                old = ctx->log.lru_size;
                ctx->log.lru_size = buf_size;
        }
        pthread_mutex_unlock (&ctx->log.log_buf_lock);

        /* If the old size is less than/equal to the new size, then do nothing.
         *
         * But if the new size is less than the old size, then
         *   a. If the cur size of the buf is less than or equal the new size,
         *      then do nothing.
         *   b. But if the current size of the buf is greater than the new size,
         *      then flush the least recently used (cur size - new_size) msgs
         *      to disk.
         */
        if (buf_size < old)
                gf_log_flush_extra_msgs (ctx, buf_size);
}

void
gf_log_set_log_flush_timeout (uint32_t timeout)
{
        THIS->ctx->log.timeout = timeout;
}

log_buf_t *
log_buf_new ()
{
        log_buf_t  *buf  = NULL;

        buf = mem_get0 (THIS->ctx->logbuf_pool);

        return buf;
}

/* If log_buf_init() fails (indicated by a return value of -1),
 * call log_buf_destroy() to clean up memory allocated in heap and to return
 * the log_buf_t object back to its memory pool.
 */
int
log_buf_init (log_buf_t *buf, const char *domain, const char *file,
              const char *function, int32_t line, gf_loglevel_t level,
              int errnum, uint64_t msgid, char **appmsgstr, int graph_id)
{
        int ret = -1;

        if (!buf || !domain || !file || !function || !appmsgstr || !*appmsgstr)
                goto out;

        buf->msg = gf_strdup (*appmsgstr);
        if (!buf->msg)
                goto out;

        buf->msg_id = msgid;
        buf->errnum = errnum;
        buf->domain = gf_strdup (domain);
        if (!buf->domain)
                goto out;

        buf->file = gf_strdup (file);
        if (!buf->file)
                goto out;

        buf->function = gf_strdup (function);
        if (!buf->function)
                goto out;

        buf->line = line;
        buf->level = level;
        buf->refcount = 0;
        buf->graph_id = graph_id;
        INIT_LIST_HEAD (&buf->msg_list);

        ret = 0;
out:
        return ret;
}

int
log_buf_destroy (log_buf_t *buf)
{

        if (!buf)
                return -1;

        GF_FREE (buf->msg);
        GF_FREE (buf->domain);
        GF_FREE (buf->file);
        GF_FREE (buf->function);

        mem_put (buf);
        return 0;
}

static void
gf_log_rotate(glusterfs_ctx_t *ctx)
{
        int            fd   = -1;
        FILE          *new_logfile = NULL;
        FILE          *old_logfile = NULL;

        /* not involving locks on initial check to speed it up */
        if (ctx->log.logrotate) {
                /* let only one winner through on races */
                pthread_mutex_lock (&ctx->log.logfile_mutex);

                if (!ctx->log.logrotate) {
                        pthread_mutex_unlock (&ctx->log.logfile_mutex);
                        return;
                } else {
                        ctx->log.logrotate = 0;
                        pthread_mutex_unlock (&ctx->log.logfile_mutex);
                }

                fd = open (ctx->log.filename,
                           O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
                if (fd < 0) {
                        gf_msg ("logrotate", GF_LOG_ERROR, errno,
                                LG_MSG_FILE_OP_FAILED, "failed to open "
                                "logfile");
                        return;
                }
                sys_close (fd);

                new_logfile = fopen (ctx->log.filename, "a");
                if (!new_logfile) {
                        gf_msg ("logrotate", GF_LOG_CRITICAL, errno,
                                LG_MSG_FILE_OP_FAILED, "failed to open logfile"
                                " %s", ctx->log.filename);
                        return;
                }

                pthread_mutex_lock (&ctx->log.logfile_mutex);
                {
                        if (ctx->log.logfile)
                                old_logfile = ctx->log.logfile;

                        ctx->log.gf_log_logfile = ctx->log.logfile =
                                                                new_logfile;
                }
                pthread_mutex_unlock (&ctx->log.logfile_mutex);

                if (old_logfile != NULL)
                        fclose (old_logfile);
        }

        return;
}

void
gf_log_globals_fini (void)
{
        /* TODO: Nobody is invoking the fini, but cleanup needs to happen here,
         * needs cleanup for, log.ident, log.filename, closelog, log file close
         * rotate state, possibly under a lock */
        pthread_mutex_destroy (&THIS->ctx->log.logfile_mutex);
        pthread_mutex_destroy (&THIS->ctx->log.log_buf_lock);
}

void
gf_log_disable_suppression_before_exit (glusterfs_ctx_t *ctx)
{
        /*
         * First set log buf size to 0. This would ensure two things:
         * i. that all outstanding log messages are flushed to disk, and
         * ii. all subsequent calls to gf_msg will result in the logs getting
         *     directly flushed to disk without being buffered.
         *
         * Then, cancel the current log timer event.
         */

        gf_log_set_log_buf_size (0);
        pthread_mutex_lock (&ctx->log.log_buf_lock);
        {
                if (ctx->log.log_flush_timer) {
                        gf_timer_call_cancel (ctx, ctx->log.log_flush_timer);
                        ctx->log.log_flush_timer = NULL;
                }
        }
        pthread_mutex_unlock (&ctx->log.log_buf_lock);

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
        int              ret = 0;
        FILE            *old_logfile = NULL;

        if (ctx == NULL) {
	        ret = -1;
                goto out;
        }

        gf_log_disable_suppression_before_exit (ctx);

        pthread_mutex_lock (&ctx->log.logfile_mutex);
        {
                if (ctx->log.logfile) {
                        old_logfile = ctx->log.logfile;

                        /* Logfile needs to be set to NULL, so that any
                           call to gf_log after calling gf_log_fini, will
                           log the message to stderr.
                        */
                        ctx->log.loglevel = GF_LOG_NONE;
                        ctx->log.logfile = NULL;
                }
        }
        pthread_mutex_unlock (&ctx->log.logfile_mutex);

        if (old_logfile && (fclose (old_logfile) != 0))
                ret = -1;

 out:
        return ret;
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

        /* TODO: Should check for errors here and return appropriately */
        setlocale(LC_ALL, "");
        setlocale(LC_NUMERIC, "C"); /* C-locale for strtod, ... */
        /* close the previous syslog if open as we are changing settings */
        closelog ();
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
 * @facility_priority: facility_priority of syslog()
 * @format:            optional format string to syslog()
 *
 * @return: void
 */
void
gf_syslog (int facility_priority, char *format, ...)
{
        char       *msg = NULL;
        char        json_msg[GF_JSON_MSG_LENGTH];
        GF_UNUSED char       *p = NULL;
        va_list     ap;

        GF_ASSERT (format);

        va_start (ap, format);
        if (vasprintf (&msg, format, ap) != -1) {
                p = _json_escape (msg, json_msg, GF_JSON_MSG_LENGTH);
                syslog (facility_priority, "%s", msg);
                free (msg);
        } else
                syslog (GF_LOG_CRITICAL, "vasprintf() failed, out of memory?");
        va_end (ap);
}

void
gf_log_globals_init (void *data, gf_loglevel_t level)
{
        glusterfs_ctx_t *ctx = data;

        pthread_mutex_init (&ctx->log.logfile_mutex, NULL);

        ctx->log.loglevel         = level;
        ctx->log.gf_log_syslog    = 1;
        ctx->log.sys_log_level    = GF_LOG_CRITICAL;
        ctx->log.logger           = gf_logger_glusterlog;
        ctx->log.logformat        = gf_logformat_withmsgid;
        ctx->log.lru_size         = GF_LOG_LRU_BUFSIZE_DEFAULT;
        ctx->log.timeout          = GF_LOG_FLUSH_TIMEOUT_DEFAULT;

        pthread_mutex_init (&ctx->log.log_buf_lock, NULL);

        INIT_LIST_HEAD (&ctx->log.lru_queue);

#ifdef GF_LINUX_HOST_OS
        /* For the 'syslog' output. one can grep 'GlusterFS' in syslog
           for serious logs */
        openlog ("GlusterFS", LOG_PID, LOG_DAEMON);
#endif

}

int
gf_log_init (void *data, const char *file, const char *ident)
{
        glusterfs_ctx_t *ctx = NULL;
        int              fd = -1;
        struct stat      buf;

        ctx = data;

        if (ctx == NULL) {
                fprintf (stderr, "ERROR: ctx is NULL\n");
                return -1;
        }
        if (ident) {
                ctx->log.ident = gf_strdup (ident);
        }

        /* we keep the files and the syslog open, so that on logger change, we
         * are ready to log anywhere, that the new value specifies */
        if (ctx->log.ident) {
                gf_openlog (ctx->log.ident, -1, LOG_DAEMON);
        } else {
                gf_openlog (NULL, -1, LOG_DAEMON);
        }
        /* TODO: make FACILITY configurable than LOG_DAEMON */
        if (sys_stat (GF_LOG_CONTROL_FILE, &buf) == 0) {
                /* use syslog logging */
                ctx->log.log_control_file_found = 1;
        } else {
                /* use old style logging */
                ctx->log.log_control_file_found = 0;
        }

        if (!file) {
                fprintf (stderr, "ERROR: no filename specified\n");
                return -1;
        }

        if (strcmp (file, "-") == 0) {
		int dupfd = -1;

		ctx->log.filename = gf_strdup ("/dev/stderr");
		if (!ctx->log.filename) {
			fprintf (stderr, "ERROR: strdup failed\n");
			return -1;
		}

		dupfd = dup (fileno (stderr));
		if (dupfd == -1) {
			fprintf (stderr, "ERROR: could not dup %d (%s)\n",
				 fileno (stderr), strerror (errno));
			return -1;
		}

		ctx->log.logfile = fdopen (dupfd, "a");
		if (!ctx->log.logfile) {
			fprintf (stderr, "ERROR: could not fdopen on %d (%s)\n",
				 dupfd, strerror (errno));
			return -1;
		}

		goto out;
        }

        ctx->log.filename = gf_strdup (file);
        if (!ctx->log.filename) {
                fprintf (stderr, "ERROR: updating log-filename failed: %s\n",
                         strerror (errno));
                return -1;
        }

        fd = open (file, O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
        if (fd < 0) {
                fprintf (stderr, "ERROR: failed to create logfile"
                         " \"%s\" (%s)\n", file, strerror (errno));
                return -1;
        }
        sys_close (fd);

        ctx->log.logfile = fopen (file, "a");
        if (!ctx->log.logfile) {
                fprintf (stderr, "ERROR: failed to open logfile \"%s\" (%s)\n",
                         file, strerror (errno));
                return -1;
        }
out:
        ctx->log.gf_log_logfile = ctx->log.logfile;

        return 0;
}

void
set_sys_log_level (gf_loglevel_t level)
{
        glusterfs_ctx_t *ctx = NULL;

        ctx = THIS->ctx;

        if (ctx)
                ctx->log.sys_log_level = level;
}

/* Check if we should be logging
 * Return value: _gf_false : Print the log
 *               _gf_true : Do not Print the log
 */
static gf_boolean_t
skip_logging (xlator_t *this, gf_loglevel_t level)
{
        gf_boolean_t ret = _gf_false;
        gf_loglevel_t existing_level = GF_LOG_NONE;

        if (level == GF_LOG_NONE) {
                ret = _gf_true;
                goto out;
        }

        existing_level = this->loglevel ? this->loglevel : this->ctx->log.loglevel;
        if (level > existing_level) {
                ret = _gf_true;
                goto out;
        }
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
        char           *callstr         = NULL;
        struct timeval  tv              = {0,};
        size_t          len             = 0;
        int             ret             = 0;
        va_list         ap;
        glusterfs_ctx_t *ctx = NULL;

        this = THIS;
        ctx = this->ctx;

        if (!ctx)
                goto out;

        if (skip_logging (this, level))
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

        /*Saving the backtrace to pre-allocated ctx->btbuf
         * to avoid allocating memory from the heap*/
        callstr = gf_backtrace_save (NULL);

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

                gf_syslog (priority, "[%s:%d:%s] %s %d-%s: %s",
                           basename, line, function,
                           callstr,
                           ((this->graph) ? this->graph->id:0), domain,
                           str2);

                goto out;
        }

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
        if (!msg) {
                ret = -1;
                goto out;
        }

        strcpy (msg, str1);
        strcpy (msg + len, str2);

        pthread_mutex_lock (&ctx->log.logfile_mutex);
        {
                if (ctx->log.logfile) {
                        fprintf (ctx->log.logfile, "%s\n", msg);
                        fflush (ctx->log.logfile);
                } else if (ctx->log.loglevel >= level) {
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

out:
        GF_FREE (msg);

        GF_FREE (str1);

        FREE (str2);

        va_end (ap);

        return ret;
}

int
_gf_msg_plain_internal (gf_loglevel_t level, const char *msg)
{
        xlator_t        *this           = NULL;
        glusterfs_ctx_t *ctx            = NULL;
        int              priority;

        this = THIS;
        ctx = this->ctx;

        /* log to the configured logging service */
        switch (ctx->log.logger) {
        case gf_logger_syslog:
                if (ctx->log.log_control_file_found && ctx->log.gf_log_syslog) {
                        SET_LOG_PRIO (level, priority);

                        syslog (priority, "%s", msg);
                        break;
                }
                /* NOTE: If syslog control file is absent, which is another
                 * way to control logging to syslog, then we will fall through
                 * to the gluster log. The ideal way to do things would be to
                 * not have the extra control file check */
        case gf_logger_glusterlog:
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
                        /* We want only serious logs in 'syslog', not our debug
                                * and trace logs */
                        if (ctx->log.gf_log_syslog && level &&
                                (level <= ctx->log.sys_log_level))
                                syslog ((level-1), "%s\n", msg);
#endif
                }
                pthread_mutex_unlock (&ctx->log.logfile_mutex);

                break;
        }

        return 0;
}

int
_gf_msg_plain (gf_loglevel_t level, const char *fmt, ...)
{
        xlator_t        *this            = NULL;
        int              ret             = 0;
        va_list          ap;
        char            *msg             = NULL;
        glusterfs_ctx_t *ctx = NULL;

        this = THIS;
        ctx = this->ctx;

        if (!ctx)
                goto out;

        if (skip_logging (this, level))
                goto out;

        va_start (ap, fmt);
        ret = vasprintf (&msg, fmt, ap);
        va_end (ap);
        if (-1 == ret) {
                goto out;
        }

        ret = _gf_msg_plain_internal (level, msg);

        FREE (msg);

out:
        return ret;
}

int
_gf_msg_vplain (gf_loglevel_t level, const char *fmt, va_list ap)
{
        xlator_t        *this            = NULL;
        int              ret             = 0;
        char            *msg             = NULL;
        glusterfs_ctx_t *ctx             = NULL;

        this = THIS;
        ctx = this->ctx;

        if (!ctx)
                goto out;

        if (skip_logging (this, level))
                goto out;

        ret = vasprintf (&msg, fmt, ap);
        if (-1 == ret) {
                goto out;
        }

        ret = _gf_msg_plain_internal (level, msg);

        FREE (msg);
out:
        return ret;
}

int
_gf_msg_plain_nomem (gf_loglevel_t level, const char *msg)
{
        xlator_t        *this           = NULL;
        int              ret            = 0;
        glusterfs_ctx_t *ctx            = NULL;

        this = THIS;
        ctx = this->ctx;

        if (!ctx)
                goto out;

        if (skip_logging (this, level))
                goto out;

        ret = _gf_msg_plain_internal (level, msg);

out:
        return ret;
}

void
_gf_msg_backtrace_nomem (gf_loglevel_t level, int stacksize)
{
        xlator_t        *this            = NULL;
        glusterfs_ctx_t *ctx = NULL;
        void            *array[200];
        size_t           bt_size = 0;
        int              fd = -1;

        this = THIS;
        ctx = this->ctx;

        if (!ctx)
                goto out;

        /* syslog does not have fd support, hence no no-mem variant */
        if (ctx->log.logger != gf_logger_glusterlog)
                goto out;

        if (skip_logging (this, level))
                goto out;

        bt_size = backtrace (array, ((stacksize <= 200)? stacksize : 200));
        pthread_mutex_lock (&ctx->log.logfile_mutex);
        {
                fd = ctx->log.logfile?
                        fileno (ctx->log.logfile) :
                        fileno (stderr);
                if (bt_size && (fd != -1)) {
                        /* print to the file fd, to prevent any
                           allocations from backtrace_symbols
                         */
                        backtrace_symbols_fd (&array[0], bt_size, fd);
                }
        }
        pthread_mutex_unlock (&ctx->log.logfile_mutex);

out:
        return;
}

int
_gf_msg_backtrace (int stacksize, char *callstr, size_t strsize)
{
        int       ret = -1;
        int       i = 0;
        int       size = 0;
        int       savstrsize = strsize;
        void     *array[200];
        char    **callingfn = NULL;

        /* We chop off last 2 anyway, so if request is less than tolerance
         * nothing to do */
        if (stacksize < 3)
                goto out;

        size = backtrace (array, ((stacksize <= 200)? stacksize : 200));
        if ((size - 3) < 0)
                goto out;
        if (size)
                callingfn = backtrace_symbols (&array[2], size - 2);
        if (!callingfn)
                goto out;

        ret = snprintf (callstr, strsize, "(");
        PRINT_SIZE_CHECK (ret, out, strsize);

        for ((i = size - 3); i >= 0; i--) {
                ret = snprintf (callstr + savstrsize - strsize, strsize,
                                "-->%s ", callingfn[i]);
                PRINT_SIZE_CHECK (ret, out, strsize);
        }

        ret = snprintf (callstr + savstrsize - strsize, strsize, ")");
        PRINT_SIZE_CHECK (ret, out, strsize);
out:
        FREE (callingfn);
        return ret;
}

int
_gf_msg_nomem (const char *domain, const char *file,
               const char *function, int line, gf_loglevel_t level,
               size_t size)
{
        const char      *basename        = NULL;
        xlator_t        *this            = NULL;
        struct timeval   tv              = {0,};
        int              ret             = 0;
        int              fd              = -1;
        char             msg[2048]       = {0,};
        char             timestr[GF_LOG_TIMESTR_SIZE]    = {0,};
        glusterfs_ctx_t *ctx = NULL;
        int              wlen = 0;
        int              priority;

        this = THIS;
        ctx = this->ctx;

        if (!ctx)
                goto out;

        if (skip_logging (this, level))
                goto out;

        if (!domain || !file || !function) {
                fprintf (stderr,
                         "logging: %s:%s():%d: invalid argument\n",
                         __FILE__, __PRETTY_FUNCTION__, __LINE__);
                return -1;
        }

        GET_FILE_NAME_TO_LOG (file, basename);

        ret = gettimeofday (&tv, NULL);
        if (-1 == ret)
                goto out;
        gf_time_fmt (timestr, sizeof timestr, tv.tv_sec, gf_timefmt_FT);
        ret = snprintf (timestr + strlen (timestr),
                        sizeof timestr - strlen (timestr),
                        ".%"GF_PRI_SUSECONDS, tv.tv_usec);
        if (-1 == ret) {
                goto out;
        }

        /* TODO: Currently we print in the enhanced format, with a message ID
         * of 0. Need to enhance this to support format as configured */
        ret = snprintf (msg, sizeof msg, "[%s] %s [MSGID: %"PRIu64"]"
                        " [%s:%d:%s] %s: no memory "
                        "available for size (%"GF_PRI_SIZET")"
                        " [call stack follows]\n",
                        timestr, gf_level_strings[level], (uint64_t) 0,
                        basename, line, function, domain, size);
        if (-1 == ret) {
                goto out;
        }

        /* log to the configured logging service */
        switch (ctx->log.logger) {
        case gf_logger_syslog:
                if (ctx->log.log_control_file_found && ctx->log.gf_log_syslog) {
                        SET_LOG_PRIO (level, priority);

                        /* if syslog allocates, then this may fail, but we
                         * cannot do much about it at the moment */
                        /* There is no fd for syslog, hence no stack printed */
                        syslog (priority, "%s", msg);
                        break;
                }
                /* NOTE: If syslog control file is absent, which is another
                 * way to control logging to syslog, then we will fall through
                 * to the gluster log. The ideal way to do things would be to
                 * not have the extra control file check */
        case gf_logger_glusterlog:
                pthread_mutex_lock (&ctx->log.logfile_mutex);
                {
                        fd = ctx->log.logfile? fileno (ctx->log.logfile) :
                                fileno (stderr);
                        if (fd == -1) {
                                pthread_mutex_unlock (&ctx->log.logfile_mutex);
                                goto out;
                        }

                        wlen = strlen (msg);

                        /* write directly to the fd to prevent out of order
                         * message and stack */
                        ret = sys_write (fd, msg, wlen);
                        if (ret == -1) {
                                pthread_mutex_unlock (&ctx->log.logfile_mutex);
                                goto out;
                        }
#ifdef GF_LINUX_HOST_OS
                        /* We want only serious log in 'syslog', not our debug
                         * and trace logs */
                        if (ctx->log.gf_log_syslog && level &&
                                (level <= ctx->log.sys_log_level))
                                syslog ((level-1), "%s\n", msg);
#endif
                }
                pthread_mutex_unlock (&ctx->log.logfile_mutex);

                _gf_msg_backtrace_nomem (level, GF_LOG_BACKTRACE_DEPTH);

                break;
        }

out:
        return ret;
}

static int
gf_log_syslog (glusterfs_ctx_t *ctx, const char *domain, const char *file,
               const char *function, int32_t line, gf_loglevel_t level,
               int errnum, uint64_t msgid, char **appmsgstr, char *callstr,
               int graph_id, gf_log_format_t fmt)
{
        int              priority;

        SET_LOG_PRIO (level, priority);

        /* log with appropriate format */
        switch (fmt) {
                case gf_logformat_traditional:
                if (!callstr) {
                        if (errnum)
                                syslog (priority, "[%s:%d:%s] %d-%s: %s [%s]",
                                        file, line, function, graph_id, domain,
                                        *appmsgstr, strerror(errnum));
                        else
                                syslog (priority, "[%s:%d:%s] %d-%s: %s",
                                        file, line, function, graph_id, domain,
                                        *appmsgstr);
                } else {
                        if (errnum)
                                syslog (priority, "[%s:%d:%s] %s %d-%s:"
                                        " %s [%s]",
                                        file, line, function, callstr, graph_id,
                                        domain, *appmsgstr, strerror(errnum));
                        else
                                syslog (priority, "[%s:%d:%s] %s %d-%s: %s",
                                        file, line, function, callstr, graph_id,
                                        domain, *appmsgstr);
                }
                break;
                case gf_logformat_withmsgid:
                if (!callstr) {
                        if (errnum)
                                syslog (priority, "[MSGID: %"PRIu64"]"
                                        " [%s:%d:%s] %d-%s: %s [%s]", msgid,
                                        file, line, function, graph_id, domain,
                                        *appmsgstr, strerror(errnum));
                        else
                                syslog (priority, "[MSGID: %"PRIu64"]"
                                        " [%s:%d:%s] %d-%s: %s",
                                        msgid, file, line, function, graph_id,
                                        domain, *appmsgstr);
                } else {
                        if (errnum)
                                syslog (priority, "[MSGID: %"PRIu64"]"
                                        " [%s:%d:%s] %s %d-%s: %s [%s]",
                                        msgid, file, line, function, callstr,
                                        graph_id, domain, *appmsgstr,
                                        strerror(errnum));
                        else
                                syslog (priority, "[MSGID: %"PRIu64"]"
                                        " [%s:%d:%s] %s %d-%s: %s",
                                        msgid, file, line, function, callstr,
                                        graph_id, domain, *appmsgstr);
                }
                break;
                case gf_logformat_cee:
                /* TODO: Enhance CEE with additional parameters */
                gf_syslog (priority, "[%s:%d:%s] %d-%s: %s",
                           file, line, function, graph_id, domain, *appmsgstr);
                break;

                default:
                /* NOTE: should not get here without logging */
                break;
        }

        /* TODO: There can be no errors from gf_syslog? */
        return 0;
}

static int
gf_log_glusterlog (glusterfs_ctx_t *ctx, const char *domain, const char *file,
                   const char *function, int32_t line, gf_loglevel_t level,
                   int errnum, uint64_t msgid, char **appmsgstr, char *callstr,
                   struct timeval tv, int graph_id, gf_log_format_t fmt)
{
        char             timestr[GF_LOG_TIMESTR_SIZE] = {0,};
        char            *header = NULL;
        char            *footer = NULL;
        char            *msg  = NULL;
        size_t           hlen  = 0, flen = 0, mlen = 0;
        int              ret  = 0;

        /* rotate if required */
        gf_log_rotate(ctx);

        /* format the time stamp */
        gf_time_fmt (timestr, sizeof timestr, tv.tv_sec, gf_timefmt_FT);
        snprintf (timestr + strlen (timestr), sizeof timestr - strlen (timestr),
                  ".%"GF_PRI_SUSECONDS, tv.tv_usec);

        /* generate header and footer */
        if (fmt == gf_logformat_traditional) {
                if (!callstr) {
                        ret = gf_asprintf (&header, "[%s] %s [%s:%d:%s]"
                                                    " %d-%s: ",
                                           timestr, gf_level_strings[level],
                                           file, line, function, graph_id,
                                           domain);
                } else {
                        ret = gf_asprintf (&header, "[%s] %s [%s:%d:%s] %s"
                                                    " %d-%s: ",
                                           timestr, gf_level_strings[level],
                                           file, line, function, callstr,
                                           graph_id, domain);
                }
                if (-1 == ret) {
                        goto err;
                }
        } else { /* gf_logformat_withmsgid */
                /* CEE log format unsupported in logger_glusterlog, so just
                 * print enhanced log format */
                if (!callstr) {
                        ret = gf_asprintf (&header, "[%s] %s [MSGID: %"PRIu64"]"
                                                    " [%s:%d:%s] %d-%s: ",
                                           timestr, gf_level_strings[level],
                                           msgid, file, line, function,
                                           graph_id, domain);
                } else {
                        ret = gf_asprintf (&header, "[%s] %s [MSGID: %"PRIu64"]"
                                                    " [%s:%d:%s] %s %d-%s: ",
                                           timestr, gf_level_strings[level],
                                           msgid, file, line, function, callstr,
                                           graph_id, domain);
                }
                if (-1 == ret) {
                        goto err;
                }
        }

        if (errnum) {
                ret = gf_asprintf (&footer, " [%s]",strerror(errnum));
                if (-1 == ret) {
                        goto err;
                }
        }

        /* generate the full message to log */
        hlen = strlen (header);
        flen = footer? strlen (footer) : 0;
        mlen = strlen (*appmsgstr);
        msg = GF_MALLOC (hlen + flen + mlen + 1, gf_common_mt_char);
        if (!msg) {
                ret = -1;
                goto err;
        }

        strcpy (msg, header);
        strcpy (msg + hlen, *appmsgstr);
        if (footer)
                strcpy (msg + hlen + mlen, footer);

        pthread_mutex_lock (&ctx->log.logfile_mutex);
        {
                if (ctx->log.logfile) {
                        fprintf (ctx->log.logfile, "%s\n", msg);
                        fflush (ctx->log.logfile);
                } else if (ctx->log.loglevel >= level) {
                        fprintf (stderr, "%s\n", msg);
                        fflush (stderr);
                }

#ifdef GF_LINUX_HOST_OS
                /* We want only serious logs in 'syslog', not our debug
                 * and trace logs */
                if (ctx->log.gf_log_syslog && level &&
                        (level <= ctx->log.sys_log_level))
                        syslog ((level-1), "%s\n", msg);
#endif
        }

        /* TODO: Plugin in memory log buffer retention here. For logs not
         * flushed during cores, it would be useful to retain some of the last
         * few messages in memory */
        pthread_mutex_unlock (&ctx->log.logfile_mutex);
        ret = 0;

err:
        GF_FREE (msg);
        GF_FREE (header);
        GF_FREE (footer);

        return ret;
}

static int
gf_syslog_log_repetitions (const char *domain, const char *file,
                           const char *function, int32_t line,
                           gf_loglevel_t level, int errnum, uint64_t msgid,
                           char **appmsgstr, char *callstr, int refcount,
                           struct timeval oldest, struct timeval latest,
                           int graph_id)
{
        int              priority;
        char             timestr_latest[256] = {0,};
        char             timestr_oldest[256] = {0,};

        SET_LOG_PRIO (level, priority);

        gf_time_fmt (timestr_latest, sizeof timestr_latest, latest.tv_sec,
                     gf_timefmt_FT);
        snprintf (timestr_latest + strlen (timestr_latest),
                  sizeof (timestr_latest) - strlen (timestr_latest),
                  ".%"GF_PRI_SUSECONDS, latest.tv_usec);

        gf_time_fmt (timestr_oldest, sizeof timestr_oldest, oldest.tv_sec,
                     gf_timefmt_FT);
        snprintf (timestr_oldest + strlen (timestr_oldest),
                  sizeof (timestr_oldest) - strlen (timestr_oldest),
                  ".%"GF_PRI_SUSECONDS, oldest.tv_usec);

        if (errnum) {
                syslog (priority, "The message \"[MSGID: %"PRIu64"] [%s:%d:%s] "
                        "%d-%s: %s [%s] \" repeated %d times between %s and %s",
                        msgid, file, line, function, graph_id, domain,
                        *appmsgstr, strerror(errnum), refcount, timestr_oldest,
                        timestr_latest);
        } else {
                syslog (priority, "The message \"[MSGID: %"PRIu64"] [%s:%d:%s] "
                        "%d-%s: %s \" repeated %d times between %s and %s",
                        msgid, file, line, function, graph_id, domain,
                        *appmsgstr, refcount, timestr_oldest, timestr_latest);
        }
        return 0;
}

static int
gf_glusterlog_log_repetitions (glusterfs_ctx_t *ctx, const char *domain,
                               const char *file, const char *function,
                               int32_t line, gf_loglevel_t level, int errnum,
                               uint64_t msgid, char **appmsgstr, char *callstr,
                               int refcount, struct timeval oldest,
                               struct timeval latest, int graph_id)
{
        int              ret                 = 0;
        size_t           hlen                = 0;
        size_t           flen                = 0;
        size_t           mlen                = 0;
        char             timestr_latest[256] = {0,};
        char             timestr_oldest[256] = {0,};
        char             errstr[256]         = {0,};
        char            *header              = NULL;
        char            *footer              = NULL;
        char            *msg                 = NULL;

        if (!ctx)
                goto err;

        gf_log_rotate (ctx);

        gf_time_fmt (timestr_latest, sizeof timestr_latest, latest.tv_sec,
                     gf_timefmt_FT);
        snprintf (timestr_latest + strlen (timestr_latest),
                  sizeof (timestr_latest) - strlen (timestr_latest),
                  ".%"GF_PRI_SUSECONDS, latest.tv_usec);

        gf_time_fmt (timestr_oldest, sizeof timestr_oldest, oldest.tv_sec,
                     gf_timefmt_FT);
        snprintf (timestr_oldest + strlen (timestr_oldest),
                  sizeof (timestr_oldest) - strlen (timestr_oldest),
                  ".%"GF_PRI_SUSECONDS, oldest.tv_usec);

        ret = gf_asprintf (&header, "The message \"%s [MSGID: %"PRIu64"]"
                           " [%s:%d:%s] %d-%s: ", gf_level_strings[level],
                           msgid, file, line, function, graph_id, domain);
        if (-1 == ret)
                goto err;

        if (errnum)
                snprintf (errstr, sizeof (errstr) - 1, " [%s]",
                          strerror (errnum));

        ret = gf_asprintf (&footer, "%s\" repeated %d times between"
                           " [%s] and [%s]", errstr, refcount, timestr_oldest,
                           timestr_latest);
        if (-1 == ret)
                goto err;

        /* generate the full message to log */
        hlen = strlen (header);
        flen = strlen (footer);
        mlen = strlen (*appmsgstr);
        msg = GF_MALLOC (hlen + flen + mlen + 1, gf_common_mt_char);
        if (!msg) {
                ret = -1;
                goto err;
        }

        strcpy (msg, header);
        strcpy (msg + hlen, *appmsgstr);
        strcpy (msg + hlen + mlen, footer);

        pthread_mutex_lock (&ctx->log.logfile_mutex);
        {
                if (ctx->log.logfile) {
                        fprintf (ctx->log.logfile, "%s\n", msg);
                        fflush (ctx->log.logfile);
                } else if (ctx->log.loglevel >= level) {
                        fprintf (stderr, "%s\n", msg);
                        fflush (stderr);
                }

#ifdef GF_LINUX_HOST_OS
                /* We want only serious logs in 'syslog', not our debug
                 * and trace logs */
                if (ctx->log.gf_log_syslog && level &&
                        (level <= ctx->log.sys_log_level))
                        syslog ((level-1), "%s\n", msg);
#endif
        }

        /* TODO: Plugin in memory log buffer retention here. For logs not
         * flushed during cores, it would be useful to retain some of the last
         * few messages in memory */
        pthread_mutex_unlock (&ctx->log.logfile_mutex);
        ret = 0;

err:
        GF_FREE (msg);
        GF_FREE (header);
        GF_FREE (footer);

        return ret;
}

static int
gf_log_print_with_repetitions (glusterfs_ctx_t *ctx, const char *domain,
                               const char *file, const char *function,
                               int32_t line, gf_loglevel_t level, int errnum,
                               uint64_t msgid, char **appmsgstr, char *callstr,
                               int refcount, struct timeval oldest,
                               struct timeval latest, int graph_id)
{
        int               ret    = -1;
        gf_log_logger_t   logger = 0;

        logger = ctx->log.logger;


        switch (logger) {
                case gf_logger_syslog:
                if (ctx->log.log_control_file_found && ctx->log.gf_log_syslog) {
                        ret = gf_syslog_log_repetitions (domain, file, function,
                                                         line, level, errnum,
                                                         msgid, appmsgstr,
                                                         callstr, refcount,
                                                         oldest, latest,
                                                         graph_id);
                        break;
                }
                case gf_logger_glusterlog:
                ret = gf_glusterlog_log_repetitions (ctx, domain, file,
                                                     function, line, level,
                                                     errnum, msgid, appmsgstr,
                                                     callstr, refcount, oldest,
                                                     latest, graph_id);
                break;
        }

        return ret;
}

static int
gf_log_print_plain_fmt (glusterfs_ctx_t *ctx, const char *domain,
                        const char *file, const char *function, int32_t line,
                        gf_loglevel_t level, int errnum, uint64_t msgid,
                        char **appmsgstr, char *callstr, struct timeval tv,
                        int graph_id, gf_log_format_t fmt)
{
        int               ret    = -1;
        gf_log_logger_t   logger = 0;

        logger = ctx->log.logger;

        /* log to the configured logging service */
        switch (logger) {
        case gf_logger_syslog:
                if (ctx->log.log_control_file_found && ctx->log.gf_log_syslog) {
                        ret = gf_log_syslog (ctx, domain, file, function, line,
                                             level, errnum, msgid, appmsgstr,
                                             callstr, graph_id, fmt);
                        break;
                }
                /* NOTE: If syslog control file is absent, which is another
                 * way to control logging to syslog, then we will fall through
                 * to the gluster log. The ideal way to do things would be to
                 * not have the extra control file check */
        case gf_logger_glusterlog:
                ret = gf_log_glusterlog (ctx, domain, file, function, line,
                                         level, errnum, msgid, appmsgstr,
                                         callstr, tv, graph_id, fmt);
                break;
        }

        return ret;
}

void
gf_log_flush_message (log_buf_t *buf, glusterfs_ctx_t *ctx)
{
        if (buf->refcount == 1) {
                (void) gf_log_print_plain_fmt (ctx, buf->domain, buf->file,
                                               buf->function, buf->line,
                                               buf->level, buf->errnum,
                                               buf->msg_id, &buf->msg, NULL,
                                               buf->latest, buf->graph_id,
                                               gf_logformat_withmsgid);
        }

        if (buf->refcount > 1) {
                gf_log_print_with_repetitions (ctx, buf->domain, buf->file,
                                               buf->function, buf->line,
                                               buf->level, buf->errnum,
                                               buf->msg_id, &buf->msg, NULL,
                                               buf->refcount, buf->oldest,
                                               buf->latest, buf->graph_id);
        }
        return;
}

static void
gf_log_flush_list (struct list_head *copy, glusterfs_ctx_t *ctx)
{
        log_buf_t        *iter = NULL;
        log_buf_t        *tmp  = NULL;

        list_for_each_entry_safe (iter, tmp, copy, msg_list) {
                gf_log_flush_message (iter, ctx);
                list_del_init (&iter->msg_list);
                log_buf_destroy (iter);
        }
}

void
gf_log_flush_msgs (glusterfs_ctx_t *ctx)
{
        struct list_head copy;

        INIT_LIST_HEAD (&copy);

        pthread_mutex_lock (&ctx->log.log_buf_lock);
        {
                list_splice_init (&ctx->log.lru_queue, &copy);
                ctx->log.lru_cur_size = 0;
        }
        pthread_mutex_unlock (&ctx->log.log_buf_lock);

        gf_log_flush_list (&copy, ctx);

        return;
}

static void
gf_log_flush_extra_msgs (glusterfs_ctx_t *ctx, uint32_t new)
{
        int               count = 0;
        int                   i = 0;
        log_buf_t        *iter  = NULL;
        log_buf_t        *tmp   = NULL;
        struct list_head copy;

        INIT_LIST_HEAD (&copy);

        /* If the number of outstanding log messages does not cause list
         * overflow even after reducing the size of the list, then do nothing.
         * Otherwise (that is if there are more items in the list than there
         * need to be after reducing its size), move the least recently used
         * 'diff' elements to be flushed into a separate list...
         */

        pthread_mutex_lock (&ctx->log.log_buf_lock);
        {
                if (ctx->log.lru_cur_size <= new)
                        goto unlock;
                count = ctx->log.lru_cur_size - new;
                list_for_each_entry_safe (iter, tmp, &ctx->log.lru_queue,
                                          msg_list) {
                        if (i == count)
                                break;

                        list_del_init (&iter->msg_list);
                        list_add_tail (&iter->msg_list, &copy);
                        i++;
                }
                ctx->log.lru_cur_size = ctx->log.lru_cur_size - count;
        }
        // ... quickly unlock ...
unlock:
        pthread_mutex_unlock (&ctx->log.log_buf_lock);
        if (list_empty (&copy))
                return;

        TEST_LOG("Log buffer size reduced. About to flush %d extra log "
                 "messages", count);
        // ... and then flush them outside the lock.
        gf_log_flush_list (&copy, ctx);
        TEST_LOG("Just flushed %d extra log messages", count);

        return;
}

static int
__gf_log_inject_timer_event (glusterfs_ctx_t *ctx)
{
        int              ret      = -1;
        struct timespec  timeout  = {0,};

        if (!ctx)
                goto out;

        if (ctx->log.log_flush_timer) {
                gf_timer_call_cancel (ctx, ctx->log.log_flush_timer);
                ctx->log.log_flush_timer = NULL;
        }

        timeout.tv_sec  = ctx->log.timeout;
        timeout.tv_nsec = 0;

        TEST_LOG("Starting timer now. Timeout = %u, current buf size = %d",
                 ctx->log.timeout, ctx->log.lru_size);
        ctx->log.log_flush_timer = gf_timer_call_after (ctx, timeout,
                                                      gf_log_flush_timeout_cbk,
                                                        (void *)ctx);
        if (!ctx->log.log_flush_timer)
                goto out;

        ret = 0;

out:
        return ret;
}

int
gf_log_inject_timer_event (glusterfs_ctx_t *ctx)
{
        int ret = -1;

        if (!ctx)
                return -1;

        pthread_mutex_lock (&ctx->log.log_buf_lock);
        {
                ret = __gf_log_inject_timer_event (ctx);
        }
        pthread_mutex_unlock (&ctx->log.log_buf_lock);

        return ret;
}

void
gf_log_flush_timeout_cbk (void *data)
{
        glusterfs_ctx_t  *ctx  = NULL;

        ctx = (glusterfs_ctx_t *) data;

        TEST_LOG("Log timer timed out. About to flush outstanding messages if "
                 "present");
        gf_log_flush_msgs (ctx);

        (void) gf_log_inject_timer_event (ctx);

        return;
}

static int
_gf_msg_internal (const char *domain, const char *file, const char *function,
                  int32_t line, gf_loglevel_t level, int errnum, uint64_t msgid,
                  char **appmsgstr, char *callstr, int graph_id)
{
        int              ret              = -1;
        uint32_t         size             = 0;
        const char      *basename         = NULL;
        xlator_t        *this             = NULL;
        glusterfs_ctx_t *ctx              = NULL;
        log_buf_t       *iter             = NULL;
        log_buf_t       *buf_tmp          = NULL;
        log_buf_t       *buf_new          = NULL;
        log_buf_t       *first            = NULL;
        struct timeval   tv               = {0,};
        gf_boolean_t     found            = _gf_false;
        gf_boolean_t     flush_lru        = _gf_false;
        gf_boolean_t     flush_logged_msg = _gf_false;

        this = THIS;
        ctx = this->ctx;

        if (!ctx)
                goto out;

        GET_FILE_NAME_TO_LOG (file, basename);

        ret = gettimeofday (&tv, NULL);
        if (ret)
                goto out;

        /* If this function is called via _gf_msg_callingfn () (indicated by a
         * non-NULL callstr), or if the logformat is traditional, flush the
         * message directly to disk.
         */

        if ((callstr) || (ctx->log.logformat == gf_logformat_traditional)) {
                ret = gf_log_print_plain_fmt (ctx, domain, basename, function,
                                              line, level, errnum, msgid,
                                              appmsgstr, callstr, tv, graph_id,
                                              gf_logformat_traditional);
                goto out;
        }

        pthread_mutex_lock (&ctx->log.log_buf_lock);
        {
                /* Check if the msg being logged is already part of the list */
                list_for_each_entry_safe_reverse (iter, buf_tmp,
                                                  &ctx->log.lru_queue,
                                                  msg_list) {
                        if (first == NULL)
                                // Remember the first (lru) element in first ptr
                                first = iter;

                        /* Try to fail the search early on by doing the less
                         * expensive integer comparisons and continue to string
                         * parameter comparisons only after all int parameters
                         * are found to be matching.
                         */
                        if (line != iter->line)
                                continue;

                        if (errnum != iter->errnum)
                                continue;

                        if (msgid != iter->msg_id)
                                continue;

                        if (level != iter->level)
                                continue;

                        if (graph_id != iter->graph_id)
                                continue;

                        if (strcmp (domain, iter->domain))
                                continue;

                        if (strcmp (basename, iter->file))
                                continue;

                        if (strcmp (function, iter->function))
                                continue;

                        if (strcmp (*appmsgstr, iter->msg))
                                continue;

                        //Ah! Found a match!
                        list_move_tail (&iter->msg_list, &ctx->log.lru_queue);
                        iter->refcount++;
                        found = _gf_true;
                        //Update the 'latest' timestamp.
                        memcpy ((void *)&(iter->latest), (void *)&tv,
                                sizeof (struct timeval));
                        break;
                }
                if (found) {
                        ret = 0;
                        goto unlock;
                }
                // else ...

                size = ctx->log.lru_size;
                /* If the upper limit on the log buf size is 0, flush the msg to
                 * disk directly after unlock. There's no need to buffer the
                 * msg here.
                 */
                if (size == 0) {
                        flush_logged_msg = _gf_true;
                        goto unlock;
                } else if ((ctx->log.lru_cur_size + 1) > size) {
                /* If the list is full, flush the lru msg to disk and also
                 * release it after unlock, and ...
                 * */
                if (first->refcount >= 1)
                        TEST_LOG("Buffer overflow of a buffer whose size limit "
                                 "is %d. About to flush least recently used log"
                                 " message to disk", size);
                        list_del_init (&first->msg_list);
                        ctx->log.lru_cur_size--;
                        flush_lru = _gf_true;
                }
                /* create a new list element, initialise and enqueue it.
                 * Additionally, this being the first occurrence of the msg,
                 * log it directly to disk after unlock. */
                buf_new = log_buf_new ();
                if (!buf_new) {
                        ret = -1;
                        goto unlock;
                }
                ret = log_buf_init (buf_new, domain, basename, function, line,
                                    level, errnum, msgid, appmsgstr, graph_id);
                if (ret) {
                        log_buf_destroy (buf_new);
                        goto unlock;
                }

                memcpy ((void *)&(buf_new->latest), (void *)&tv,
                        sizeof (struct timeval));
                memcpy ((void *)&(buf_new->oldest), (void *)&tv,
                        sizeof (struct timeval));

                list_add_tail (&buf_new->msg_list, &ctx->log.lru_queue);
                ctx->log.lru_cur_size++;
                flush_logged_msg = _gf_true;
                ret = 0;
        }
unlock:
        pthread_mutex_unlock (&ctx->log.log_buf_lock);

        /* Value of @ret is a don't-care below since irrespective of success or
         * failure post setting of @flush_lru, @first must be flushed and freed.
         */
        if (flush_lru) {
                gf_log_flush_message (first, ctx);
                log_buf_destroy (first);
        }
        /* Similarly, irrespective of whether all operations since setting of
         * @flush_logged_msg were successful or not, flush the message being
         * logged to disk in the plain format.
         */
        if (flush_logged_msg) {
                ret = gf_log_print_plain_fmt (ctx, domain, basename,
                                              function, line, level,
                                              errnum, msgid, appmsgstr,
                                              callstr, tv, graph_id,
                                              gf_logformat_withmsgid);
        }

out:
        return ret;
}

int
_gf_msg (const char *domain, const char *file, const char *function,
         int32_t line, gf_loglevel_t level, int errnum, int trace,
         uint64_t msgid, const char *fmt, ...)
{
        int              ret = 0;
        char            *msgstr = NULL;
        va_list          ap;
        xlator_t        *this = NULL;
        glusterfs_ctx_t *ctx = NULL;
        char             callstr[GF_LOG_BACKTRACE_SIZE] = {0,};
        int              passcallstr = 0;
        int              log_inited = 0;

        /* in args check */
        if (!domain || !file || !function || !fmt) {
                fprintf (stderr,
                         "logging: %s:%s():%d: invalid argument\n",
                         __FILE__, __PRETTY_FUNCTION__, __LINE__);
                return -1;
        }

        this = THIS;

        if (this == NULL)
                return -1;

        ctx = this->ctx;
        if (ctx == NULL) {
                /* messages before context initialization are ignored */
                return -1;
        }

        /* check if we should be logging */
        if (skip_logging (this, level))
                goto out;

        if (trace) {
                ret = _gf_msg_backtrace (GF_LOG_BACKTRACE_DEPTH, callstr,
                                         GF_LOG_BACKTRACE_SIZE);
                if (ret >= 0)
                        passcallstr = 1;
                else
                        ret = 0;
        }

        pthread_mutex_lock (&ctx->log.logfile_mutex);
        {
                if (ctx->log.logfile) {
                        log_inited = 1;
                }
        }
        pthread_mutex_unlock (&ctx->log.logfile_mutex);

        /* form the message */
        va_start (ap, fmt);
        ret = vasprintf (&msgstr, fmt, ap);
        va_end (ap);

        /* log */
        if (ret != -1) {
                if (!log_inited && ctx->log.gf_log_syslog) {
                        ret = gf_log_syslog (ctx, domain, file, function, line,
                                            level, errnum, msgid, &msgstr,
                                            (passcallstr? callstr : NULL),
                                            (this->graph)? this->graph->id : 0,
                                            gf_logformat_traditional);
                } else {
                        ret = _gf_msg_internal (domain, file, function, line,
                                            level, errnum, msgid, &msgstr,
                                            (passcallstr? callstr : NULL),
                                            (this->graph)? this->graph->id : 0);
                }
        } else {
                /* man (3) vasprintf states on error strp contents
                 * are undefined, be safe */
                msgstr = NULL;
        }

        FREE (msgstr);

out:
        return ret;
}

/* TODO: Deprecate (delete) _gf_log, _gf_log_callingfn,
 * once messages are changed to use _gf_msgXXX APIs for logging */
int
_gf_log (const char *domain, const char *file, const char *function, int line,
         gf_loglevel_t level, const char *fmt, ...)
{
        const char    *basename = NULL;
        FILE          *new_logfile = NULL;
        va_list        ap;
        char           timestr[GF_LOG_TIMESTR_SIZE] = {0,};
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

        if (!ctx)
                goto out;

        if (skip_logging (this, level))
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

                gf_syslog (priority, "[%s:%d:%s] %d-%s: %s",
                           basename, line, function,
                           ((this->graph) ? this->graph->id:0), domain, str2);
                goto err;
        }

        if (ctx->log.logrotate) {
                ctx->log.logrotate = 0;

                fd = open (ctx->log.filename,
                           O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
                if (fd < 0) {
                        gf_msg ("logrotate", GF_LOG_ERROR, errno,
                                LG_MSG_FILE_OP_FAILED,
                                "failed to open logfile");
                        return -1;
                }
                sys_close (fd);

                new_logfile = fopen (ctx->log.filename, "a");
                if (!new_logfile) {
                        gf_msg ("logrotate", GF_LOG_CRITICAL, errno,
                                LG_MSG_FILE_OP_FAILED,
                                "failed to open logfile %s",
                                ctx->log.filename);
                        goto log;
                }

                pthread_mutex_lock (&ctx->log.logfile_mutex);
                {
                        if (ctx->log.logfile)
                                fclose (ctx->log.logfile);

                        ctx->log.gf_log_logfile =
                                ctx->log.logfile = new_logfile;
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
        if (!msg) {
                goto err;
        }

        strcpy (msg, str1);
        strcpy (msg + len, str2);

        pthread_mutex_lock (&ctx->log.logfile_mutex);
        {

                if (ctx->log.logfile) {
                        fprintf (ctx->log.logfile, "%s\n", msg);
                        fflush (ctx->log.logfile);
                } else if (ctx->log.loglevel >= level) {
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
        va_end (ap);
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

        va_end (ap);

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

        if (!ctx)
                return -1;

        if (!filename){
                gf_msg (this->name, GF_LOG_CRITICAL, 0, LG_MSG_INVALID_ENTRY,
                        "gf_cmd_log_init: no filename specified\n");
                return -1;
        }

        ctx->log.cmd_log_filename = gf_strdup (filename);
        if (!ctx->log.cmd_log_filename) {
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
                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                        LG_MSG_FILE_OP_FAILED, "failed to open cmd_log_file");
                return -1;
        }
        sys_close (fd);

        ctx->log.cmdlogfile = fopen (ctx->log.cmd_log_filename, "a");
        if (!ctx->log.cmdlogfile){
                gf_msg (this->name, GF_LOG_CRITICAL, errno,
                        LG_MSG_FILE_OP_FAILED,
                        "gf_cmd_log_init: failed to open logfile \"%s\" "
                        "\n", ctx->log.cmd_log_filename);
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
        int            fd   = -1;
        glusterfs_ctx_t *ctx = NULL;

        ctx = THIS->ctx;

        if (!ctx)
                return -1;

        if (!ctx->log.cmdlogfile)
                return -1;


        if (!domain || !fmt) {
                gf_msg_trace ("glusterd", 0,
                        "logging: invalid argument\n");
                return -1;
        }

        ret = gettimeofday (&tv, NULL);
        if (ret == -1)
                goto out;
        va_start (ap, fmt);
        gf_time_fmt (timestr, sizeof timestr, tv.tv_sec, gf_timefmt_FT);
        snprintf (timestr + strlen (timestr),
                  GF_LOG_TIMESTR_SIZE - strlen (timestr),
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
        if (!msg) {
                goto out;
        }

        strcpy (msg, str1);
        strcpy (msg + len, str2);

        /* close and reopen cmdlogfile fd for in case of log rotate*/
        if (ctx->log.cmd_history_logrotate) {
                ctx->log.cmd_history_logrotate = 0;

                if (ctx->log.cmdlogfile) {
                        fclose (ctx->log.cmdlogfile);
                        ctx->log.cmdlogfile = NULL;
                }

                fd = open (ctx->log.cmd_log_filename,
                           O_CREAT | O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR);
                if (fd < 0) {
                        gf_msg (THIS->name, GF_LOG_CRITICAL, errno,
                                LG_MSG_FILE_OP_FAILED, "failed to open "
                                "logfile \"%s\" \n", ctx->log.cmd_log_filename);
                        ret = -1;
                        goto out;
                }

                ctx->log.cmdlogfile = fdopen (fd, "a");
                if (!ctx->log.cmdlogfile) {
                        gf_msg (THIS->name, GF_LOG_CRITICAL, errno,
                                LG_MSG_FILE_OP_FAILED,
                                "failed to open logfile \"%s\""
                                " \n", ctx->log.cmd_log_filename);
                        ret = -1;
                        goto out;
                }
        }

        fprintf (ctx->log.cmdlogfile, "%s\n", msg);
        fflush (ctx->log.cmdlogfile);

out:
        GF_FREE (msg);

        GF_FREE (str1);

        FREE (str2);

        va_end (ap);

        return ret;
}
