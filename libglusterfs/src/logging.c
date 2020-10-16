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
#include <sys/resource.h>

#ifdef HAVE_BACKTRACE
#include <execinfo.h>
#else
#include "execinfo_compat.h"
#endif

#include <sys/stat.h>

#include "glusterfs/syscall.h"

#define GF_JSON_MSG_LENGTH 8192
#define GF_SYSLOG_CEE_FORMAT                                                   \
    "@cee: {\"msg\": \"%s\", \"gf_code\": \"%u\", \"gf_message\": \"%s\"}"
#define GF_LOG_CONTROL_FILE "/etc/glusterfs/logger.conf"
#define GF_LOG_BACKTRACE_DEPTH 5
#define GF_LOG_BACKTRACE_SIZE 4096
#define GF_MAX_SLOG_PAIR_COUNT 100

#include "glusterfs/logging.h"
#include "glusterfs/glusterfs.h"
#include "glusterfs/timer.h"
#include "glusterfs/libglusterfs-messages.h"

/* Do not replace gf_log in TEST_LOG with gf_msg, as there is a slight chance
 * that it could lead to an infinite recursion.*/
#define TEST_LOG(__msg, __args...)                                             \
    gf_log("logging-infra", GF_LOG_DEBUG, __msg, ##__args);

static void
gf_log_flush_timeout_cbk(void *data);

int
gf_log_inject_timer_event(glusterfs_ctx_t *ctx);

static void
gf_log_flush_extra_msgs(glusterfs_ctx_t *ctx, uint32_t new);

static int
log_buf_init(log_buf_t *buf, const char *domain, const char *file,
             const char *function, int32_t line, gf_loglevel_t level,
             int errnum, uint64_t msgid, char **appmsgstr, int graph_id);
static void
gf_log_rotate(glusterfs_ctx_t *ctx);

static char gf_level_strings[] = {
    ' ', /* NONE */
    'M', /* EMERGENCY */
    'A', /* ALERT */
    'C', /* CRITICAL */
    'E', /* ERROR */
    'W', /* WARNING */
    'N', /* NOTICE */
    'I', /* INFO */
    'D', /* DEBUG */
    'T', /* TRACE */
};

void
gf_log_logrotate(int signum)
{
    if (THIS->ctx) {
        THIS->ctx->log.logrotate = 1;
        THIS->ctx->log.cmd_history_logrotate = 1;
    }
}

void
gf_log_enable_syslog(void)
{
    if (THIS->ctx)
        THIS->ctx->log.gf_log_syslog = 1;
}

void
gf_log_disable_syslog(void)
{
    if (THIS->ctx)
        THIS->ctx->log.gf_log_syslog = 0;
}

gf_loglevel_t
gf_log_get_loglevel(void)
{
    if (THIS->ctx)
        return THIS->ctx->log.loglevel;
    else
        /* return global defaults (see gf_log_globals_init) */
        return GF_LOG_INFO;
}

void
gf_log_set_loglevel(glusterfs_ctx_t *ctx, gf_loglevel_t level)
{
    if (ctx)
        ctx->log.loglevel = level;
}

int
gf_log_get_localtime(void)
{
    if (THIS->ctx)
        return THIS->ctx->log.localtime;
    else
        /* return global defaults (see gf_log_globals_init) */
        return 0;
}

void
gf_log_set_localtime(int on_off)
{
    if (THIS->ctx)
        THIS->ctx->log.localtime = on_off;
}

void
gf_log_flush(void)
{
    xlator_t *this = THIS;
    glusterfs_ctx_t *ctx = this->ctx;

    if (ctx && ctx->log.logger == gf_logger_glusterlog) {
        pthread_mutex_lock(&ctx->log.logfile_mutex);
        fflush(ctx->log.gf_log_logfile);
        pthread_mutex_unlock(&ctx->log.logfile_mutex);
    }

    return;
}

void
gf_log_set_xl_loglevel(void *this, gf_loglevel_t level)
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

void
gf_log_set_logformat(gf_log_format_t format)
{
    if (THIS->ctx)
        THIS->ctx->log.logformat = format;
}

void
gf_log_set_logger(gf_log_logger_t logger)
{
    if (THIS->ctx)
        THIS->ctx->log.logger = logger;
}

gf_loglevel_t
gf_log_get_xl_loglevel(void *this)
{
    xlator_t *xl = this;
    if (!xl)
        return 0;
    return xl->loglevel;
}

void
gf_log_set_log_buf_size(uint32_t buf_size)
{
    uint32_t old = 0;
    glusterfs_ctx_t *ctx = THIS->ctx;

    pthread_mutex_lock(&ctx->log.log_buf_lock);
    {
        old = ctx->log.lru_size;
        ctx->log.lru_size = buf_size;
    }
    pthread_mutex_unlock(&ctx->log.log_buf_lock);

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
        gf_log_flush_extra_msgs(ctx, buf_size);
}

void
gf_log_set_log_flush_timeout(uint32_t timeout)
{
    THIS->ctx->log.timeout = timeout;
}

/* If log_buf_init() fails (indicated by a return value of -1),
 * call log_buf_destroy() to clean up memory allocated in heap and to return
 * the log_buf_t object back to its memory pool.
 */
static int
log_buf_init(log_buf_t *buf, const char *domain, const char *file,
             const char *function, int32_t line, gf_loglevel_t level,
             int errnum, uint64_t msgid, char **appmsgstr, int graph_id)
{
    int ret = -1;

    if (!buf || !domain || !file || !function || !appmsgstr || !*appmsgstr)
        goto out;

    buf->msg = gf_strdup(*appmsgstr);
    if (!buf->msg)
        goto out;

    buf->msg_id = msgid;
    buf->errnum = errnum;
    buf->domain = gf_strdup(domain);
    if (!buf->domain)
        goto out;

    buf->file = gf_strdup(file);
    if (!buf->file)
        goto out;

    buf->function = gf_strdup(function);
    if (!buf->function)
        goto out;

    buf->line = line;
    buf->level = level;
    buf->refcount = 0;
    buf->graph_id = graph_id;
    INIT_LIST_HEAD(&buf->msg_list);

    ret = 0;
out:
    return ret;
}

static int
log_buf_destroy(log_buf_t *buf)
{
    if (!buf)
        return -1;

    GF_FREE(buf->msg);
    GF_FREE(buf->domain);
    GF_FREE(buf->file);
    GF_FREE(buf->function);

    mem_put(buf);
    return 0;
}

static void
gf_log_rotate(glusterfs_ctx_t *ctx)
{
    int fd = -1;
    FILE *new_logfile = NULL;
    FILE *old_logfile = NULL;

    /* not involving locks on initial check to speed it up */
    if (ctx->log.logrotate) {
        /* let only one winner through on races */
        pthread_mutex_lock(&ctx->log.logfile_mutex);

        if (!ctx->log.logrotate) {
            pthread_mutex_unlock(&ctx->log.logfile_mutex);
            return;
        } else {
            ctx->log.logrotate = 0;
            pthread_mutex_unlock(&ctx->log.logfile_mutex);
        }

        fd = sys_open(ctx->log.filename, O_CREAT | O_WRONLY | O_APPEND,
                      S_IRUSR | S_IWUSR);
        if (fd < 0) {
            gf_smsg("logrotate", GF_LOG_ERROR, errno,
                    LG_MSG_OPEN_LOGFILE_FAILED, NULL);
            return;
        }

        new_logfile = fdopen(fd, "a");
        if (!new_logfile) {
            gf_smsg("logrotate", GF_LOG_CRITICAL, errno,
                    LG_MSG_OPEN_LOGFILE_FAILED, "filename=%s",
                    ctx->log.filename, NULL);
            sys_close(fd);
            return;
        }

        pthread_mutex_lock(&ctx->log.logfile_mutex);
        {
            if (ctx->log.logfile)
                old_logfile = ctx->log.logfile;

            ctx->log.gf_log_logfile = ctx->log.logfile = new_logfile;
        }
        pthread_mutex_unlock(&ctx->log.logfile_mutex);

        if (old_logfile != NULL)
            fclose(old_logfile);
    }

    return;
}

void
gf_log_globals_fini(void)
{
    /* TODO: Nobody is invoking the fini, but cleanup needs to happen here,
     * needs cleanup for, log.ident, log.filename, closelog, log file close
     * rotate state, possibly under a lock */
    pthread_mutex_destroy(&THIS->ctx->log.logfile_mutex);
    pthread_mutex_destroy(&THIS->ctx->log.log_buf_lock);
}

void
gf_log_disable_suppression_before_exit(glusterfs_ctx_t *ctx)
{
    /*
     * First set log buf size to 0. This would ensure two things:
     * i. that all outstanding log messages are flushed to disk, and
     * ii. all subsequent calls to gf_msg will result in the logs getting
     *     directly flushed to disk without being buffered.
     *
     * Then, cancel the current log timer event.
     */

    gf_log_set_log_buf_size(0);
    pthread_mutex_lock(&ctx->log.log_buf_lock);
    {
        if (ctx->log.log_flush_timer) {
            gf_timer_call_cancel(ctx, ctx->log.log_flush_timer);
            ctx->log.log_flush_timer = NULL;
        }
    }
    pthread_mutex_unlock(&ctx->log.log_buf_lock);
}

/** gf_log_fini - function to perform the cleanup of the log information
 * @data - glusterfs context
 * @return: success: 0
 *          failure: -1
 */
int
gf_log_fini(void *data)
{
    glusterfs_ctx_t *ctx = data;
    int ret = 0;
    FILE *old_logfile = NULL;

    if (ctx == NULL) {
        ret = -1;
        goto out;
    }

    gf_log_disable_suppression_before_exit(ctx);

    pthread_mutex_lock(&ctx->log.logfile_mutex);
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
    pthread_mutex_unlock(&ctx->log.logfile_mutex);

    if (old_logfile && (fclose(old_logfile) != 0))
        ret = -1;

    GF_FREE(ctx->log.ident);
    GF_FREE(ctx->log.filename);

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
static void
gf_openlog(const char *ident, int option, int facility)
{
    int _option = option;
    int _facility = facility;
    char *language = NULL;

    if (-1 == _option) {
        _option = LOG_PID | LOG_NDELAY;
    }
    if (-1 == _facility) {
        _facility = LOG_LOCAL1;
    }

    /* TODO: Should check for errors here and return appropriately */
    language = setlocale(LC_ALL, "en_US.UTF-8");
    if (!language)
        setlocale(LC_ALL, "");

    setlocale(LC_NUMERIC, "C"); /* C-locale for strtod, ... */
    /* close the previous syslog if open as we are changing settings */
    closelog();
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
static char *
_json_escape(const char *str, char *buf, size_t len)
{
    static const unsigned char json_exceptions[UCHAR_MAX + 1] = {
        [0x01] = 1, [0x02] = 1, [0x03] = 1, [0x04] = 1, [0x05] = 1, [0x06] = 1,
        [0x07] = 1, [0x08] = 1, [0x09] = 1, [0x0a] = 1, [0x0b] = 1, [0x0c] = 1,
        [0x0d] = 1, [0x0e] = 1, [0x0f] = 1, [0x10] = 1, [0x11] = 1, [0x12] = 1,
        [0x13] = 1, [0x14] = 1, [0x15] = 1, [0x16] = 1, [0x17] = 1, [0x18] = 1,
        [0x19] = 1, [0x1a] = 1, [0x1b] = 1, [0x1c] = 1, [0x1d] = 1, [0x1e] = 1,
        [0x1f] = 1, ['\\'] = 1, ['"'] = 1};
    static const char json_hex_chars[16] = "0123456789abcdef";
    unsigned char *p = NULL;
    size_t pos = 0;

    if (!str || !buf || len <= 0) {
        return NULL;
    }

    for (p = (unsigned char *)str; *p && (pos + 1) < len; p++) {
        if (json_exceptions[*p] == 0) {
            buf[pos++] = *p;
            continue;
        }

        if ((pos + 2) >= len) {
            break;
        }

        switch (*p) {
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
static void
gf_syslog(int facility_priority, char *format, ...)
{
    char *msg = NULL;
    char json_msg[GF_JSON_MSG_LENGTH];
    GF_UNUSED char *p = NULL;
    va_list ap;

    GF_ASSERT(format);

    va_start(ap, format);
    if (vasprintf(&msg, format, ap) != -1) {
        p = _json_escape(msg, json_msg, GF_JSON_MSG_LENGTH);
        syslog(facility_priority, "%s", msg);
        free(msg);
    } else
        syslog(GF_LOG_CRITICAL, "vasprintf() failed, out of memory?");
    va_end(ap);
}

void
gf_log_globals_init(void *data, gf_loglevel_t level)
{
    glusterfs_ctx_t *ctx = data;

    pthread_mutex_init(&ctx->log.logfile_mutex, NULL);

    ctx->log.loglevel = level;
    ctx->log.gf_log_syslog = 1;
    ctx->log.sys_log_level = GF_LOG_CRITICAL;
    ctx->log.logger = gf_logger_glusterlog;
    ctx->log.logformat = gf_logformat_withmsgid;
    ctx->log.lru_size = GF_LOG_LRU_BUFSIZE_DEFAULT;
    ctx->log.timeout = GF_LOG_FLUSH_TIMEOUT_DEFAULT;
    ctx->log.localtime = GF_LOG_LOCALTIME_DEFAULT;

    pthread_mutex_init(&ctx->log.log_buf_lock, NULL);

    INIT_LIST_HEAD(&ctx->log.lru_queue);

#ifdef GF_LINUX_HOST_OS
    /* For the 'syslog' output. one can grep 'GlusterFS' in syslog
       for serious logs */
    openlog("GlusterFS", LOG_PID, LOG_DAEMON);
#endif
}

int
gf_log_init(void *data, const char *file, const char *ident)
{
    glusterfs_ctx_t *ctx = data;
    int fd = -1;
    struct stat buf;

    if (ctx == NULL) {
        fprintf(stderr, "ERROR: ctx is NULL\n");
        return -1;
    }
    if (ident) {
        GF_FREE(ctx->log.ident);
        ctx->log.ident = gf_strdup(ident);
    }

    /* we keep the files and the syslog open, so that on logger change, we
     * are ready to log anywhere, that the new value specifies */
    if (ctx->log.ident) {
        gf_openlog(ctx->log.ident, -1, LOG_DAEMON);
    } else {
        gf_openlog(NULL, -1, LOG_DAEMON);
    }
    /* TODO: make FACILITY configurable than LOG_DAEMON */
    if (sys_stat(GF_LOG_CONTROL_FILE, &buf) == 0) {
        /* use syslog logging */
        ctx->log.log_control_file_found = 1;
    } else {
        /* use old style logging */
        ctx->log.log_control_file_found = 0;
    }

    if (!file) {
        fprintf(stderr, "ERROR: no filename specified\n");
        return -1;
    }

    /* free the (possible) previous filename */
    GF_FREE(ctx->log.filename);
    ctx->log.filename = NULL;

    /* close and reopen logfile for log rotate */
    if (ctx->log.logfile) {
        fclose(ctx->log.logfile);
        ctx->log.logfile = NULL;
        ctx->log.gf_log_logfile = NULL;
    }

    if (strcmp(file, "-") == 0) {
        int dupfd = -1;

        ctx->log.filename = gf_strdup("/dev/stderr");
        if (!ctx->log.filename) {
            fprintf(stderr, "ERROR: strdup failed\n");
            return -1;
        }

        dupfd = dup(fileno(stderr));
        if (dupfd == -1) {
            fprintf(stderr, "ERROR: could not dup %d (%s)\n", fileno(stderr),
                    strerror(errno));
            return -1;
        }

        ctx->log.logfile = fdopen(dupfd, "a");
        if (!ctx->log.logfile) {
            fprintf(stderr, "ERROR: could not fdopen on %d (%s)\n", dupfd,
                    strerror(errno));
            sys_close(dupfd);
            return -1;
        }
    } else {
        /* Also create parent dir */
        char *logdir = gf_strdup(file);
        if (!logdir) {
            return -1;
        }
        char *tmp_index = rindex(logdir, '/');
        if (tmp_index) {
            tmp_index[0] = '\0';
        }
        if (mkdir_p(logdir, 0755, true)) {
            /* EEXIST is handled in mkdir_p() itself */
            gf_smsg("logging", GF_LOG_ERROR, 0, LG_MSG_STRDUP_ERROR,
                    "logdir=%s", logdir, "errno=%s", strerror(errno), NULL);
            GF_FREE(logdir);
            return -1;
        }
        /* no need of this variable */
        GF_FREE(logdir);

        ctx->log.filename = gf_strdup(file);
        if (!ctx->log.filename) {
            fprintf(stderr,
                    "ERROR: updating log-filename failed: "
                    "%s\n",
                    strerror(errno));
            return -1;
        }

        fd = sys_open(file, O_CREAT | O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR);
        if (fd < 0) {
            fprintf(stderr,
                    "ERROR: failed to create logfile"
                    " \"%s\" (%s)\n",
                    file, strerror(errno));
            return -1;
        }

        ctx->log.logfile = fdopen(fd, "a");
        if (!ctx->log.logfile) {
            fprintf(stderr,
                    "ERROR: failed to open logfile \"%s\" "
                    "(%s)\n",
                    file, strerror(errno));
            sys_close(fd);
            return -1;
        }
    }

    ctx->log.gf_log_logfile = ctx->log.logfile;

    return 0;
}

void
set_sys_log_level(gf_loglevel_t level)
{
    if (THIS->ctx)
        THIS->ctx->log.sys_log_level = level;
}

/* Check if we should be logging
 * Return value: _gf_false : Print the log
 *               _gf_true : Do not Print the log
 */
static gf_boolean_t
skip_logging(xlator_t *this, gf_loglevel_t level)
{
    gf_loglevel_t existing_level = this->loglevel ? this->loglevel
                                                  : this->ctx->log.loglevel;
    if (level > existing_level) {
        return _gf_true;
    }

    if (level == GF_LOG_NONE) {
        return _gf_true;
    }

    return _gf_false;
}

int
_gf_log_callingfn(const char *domain, const char *file, const char *function,
                  int line, gf_loglevel_t level, const char *fmt, ...)
{
    const char *basename = NULL;
    xlator_t *this = THIS;
    char *logline = NULL;
    char *msg = NULL;
    char timestr[GF_TIMESTR_SIZE] = {
        0,
    };
    char *callstr = NULL;
    struct timeval tv = {
        0,
    };
    int ret = 0;
    va_list ap;
    glusterfs_ctx_t *ctx = this->ctx;

    if (!ctx)
        goto out;

    if (skip_logging(this, level))
        goto out;

    if (!domain || !file || !function || !fmt) {
        fprintf(stderr, "logging: %s:%s():%d: invalid argument\n", __FILE__,
                __PRETTY_FUNCTION__, __LINE__);
        return -1;
    }

    basename = strrchr(file, '/');
    if (basename)
        basename++;
    else
        basename = file;

    /*Saving the backtrace to pre-allocated ctx->btbuf
     * to avoid allocating memory from the heap*/
    callstr = gf_backtrace_save(NULL);

    va_start(ap, fmt);
    ret = vasprintf(&msg, fmt, ap);
    va_end(ap);
    if (-1 == ret) {
        goto out;
    }

    if (ctx->log.log_control_file_found) {
        int priority;
        /* treat GF_LOG_TRACE and GF_LOG_NONE as LOG_DEBUG and
           other level as is */
        if (GF_LOG_TRACE == level || GF_LOG_NONE == level) {
            priority = LOG_DEBUG;
        } else {
            priority = level - 1;
        }

        gf_syslog(priority, "[%s:%d:%s] %s %d-%s: %s", basename, line, function,
                  callstr, ((this->graph) ? this->graph->id : 0), domain, msg);

        goto out;
    }

    ret = gettimeofday(&tv, NULL);
    if (-1 == ret)
        goto out;

    gf_time_fmt_tv(timestr, sizeof timestr, &tv, gf_timefmt_FT);

    ret = gf_asprintf(&logline, "[%s] %c [%s:%d:%s] %s %d-%s: %s\n", timestr,
                      gf_level_strings[level], basename, line, function,
                      callstr, ((this->graph) ? this->graph->id : 0), domain,
                      msg);
    if (-1 == ret) {
        goto out;
    }

    pthread_mutex_lock(&ctx->log.logfile_mutex);
    {
        if (ctx->log.logfile) {
            fputs(logline, ctx->log.logfile);
            fflush(ctx->log.logfile);
        } else if (ctx->log.loglevel >= level) {
            fputs(logline, stderr);
            fflush(stderr);
        }

#ifdef GF_LINUX_HOST_OS
        /* We want only serious log in 'syslog', not our debug
           and trace logs */
        if (ctx->log.gf_log_syslog && level &&
            (level <= ctx->log.sys_log_level))
            syslog((level - 1), "%s", logline);
#endif
    }

    pthread_mutex_unlock(&ctx->log.logfile_mutex);

out:

    GF_FREE(logline);

    FREE(msg);

    return ret;
}

static int
_gf_msg_plain_internal(gf_loglevel_t level, const char *msg)
{
    xlator_t *this = NULL;
    glusterfs_ctx_t *ctx = NULL;
    int priority;

    this = THIS;
    ctx = this->ctx;

    /* log to the configured logging service */
    switch (ctx->log.logger) {
        case gf_logger_syslog:
            if (ctx->log.log_control_file_found && ctx->log.gf_log_syslog) {
                SET_LOG_PRIO(level, priority);

                syslog(priority, "%s", msg);
                break;
            }
            /* NOTE: If syslog control file is absent, which is another
             * way to control logging to syslog, then we will fall through
             * to the gluster log. The ideal way to do things would be to
             * not have the extra control file check */
        case gf_logger_glusterlog:
            pthread_mutex_lock(&ctx->log.logfile_mutex);
            {
                if (ctx->log.logfile) {
                    fprintf(ctx->log.logfile, "%s\n", msg);
                    fflush(ctx->log.logfile);
                } else {
                    fprintf(stderr, "%s\n", msg);
                    fflush(stderr);
                }

#ifdef GF_LINUX_HOST_OS
                /* We want only serious logs in 'syslog', not our debug
                 * and trace logs */
                if (ctx->log.gf_log_syslog && level &&
                    (level <= ctx->log.sys_log_level))
                    syslog((level - 1), "%s\n", msg);
#endif
            }
            pthread_mutex_unlock(&ctx->log.logfile_mutex);

            break;
    }

    return 0;
}

int
_gf_msg_plain(gf_loglevel_t level, const char *fmt, ...)
{
    xlator_t *this = NULL;
    int ret = 0;
    va_list ap;
    char *msg = NULL;
    glusterfs_ctx_t *ctx = NULL;

    this = THIS;
    ctx = this->ctx;

    if (!ctx)
        goto out;

    if (skip_logging(this, level))
        goto out;

    va_start(ap, fmt);
    ret = vasprintf(&msg, fmt, ap);
    va_end(ap);
    if (-1 == ret) {
        goto out;
    }

    ret = _gf_msg_plain_internal(level, msg);

    FREE(msg);

out:
    return ret;
}

int
_gf_msg_vplain(gf_loglevel_t level, const char *fmt, va_list ap)
{
    xlator_t *this = NULL;
    int ret = 0;
    char *msg = NULL;
    glusterfs_ctx_t *ctx = NULL;

    this = THIS;
    ctx = this->ctx;

    if (!ctx)
        goto out;

    if (skip_logging(this, level))
        goto out;

    ret = vasprintf(&msg, fmt, ap);
    if (-1 == ret) {
        goto out;
    }

    ret = _gf_msg_plain_internal(level, msg);

    FREE(msg);
out:
    return ret;
}

int
_gf_msg_plain_nomem(gf_loglevel_t level, const char *msg)
{
    xlator_t *this = NULL;
    int ret = 0;
    glusterfs_ctx_t *ctx = NULL;

    this = THIS;
    ctx = this->ctx;

    if (!ctx)
        goto out;

    if (skip_logging(this, level))
        goto out;

    ret = _gf_msg_plain_internal(level, msg);

out:
    return ret;
}

void
_gf_msg_backtrace_nomem(gf_loglevel_t level, int stacksize)
{
    xlator_t *this = NULL;
    glusterfs_ctx_t *ctx = NULL;
    void *array[200];
    size_t bt_size = 0;
    int fd = -1;

    this = THIS;
    ctx = this->ctx;

    if (!ctx)
        goto out;

    /* syslog does not have fd support, hence no no-mem variant */
    if (ctx->log.logger != gf_logger_glusterlog)
        goto out;

    if (skip_logging(this, level))
        goto out;

    bt_size = backtrace(array, ((stacksize <= 200) ? stacksize : 200));
    if (!bt_size)
        goto out;
    pthread_mutex_lock(&ctx->log.logfile_mutex);
    {
        fd = ctx->log.logfile ? fileno(ctx->log.logfile) : fileno(stderr);
        if (fd != -1) {
            /* print to the file fd, to prevent any
               allocations from backtrace_symbols
             */
            backtrace_symbols_fd(&array[0], bt_size, fd);
        }
    }
    pthread_mutex_unlock(&ctx->log.logfile_mutex);

out:
    return;
}

int
_gf_msg_backtrace(int stacksize, char *callstr, size_t strsize)
{
    int ret = -1;
    int i = 0;
    int size = 0;
    int savstrsize = strsize;
    void *array[200];
    char **callingfn = NULL;

    /* We chop off last 2 anyway, so if request is less than tolerance
     * nothing to do */
    if (stacksize < 3)
        goto out;

    size = backtrace(array, ((stacksize <= 200) ? stacksize : 200));
    if ((size - 3) < 0)
        goto out;
    if (size)
        callingfn = backtrace_symbols(&array[2], size - 2);
    if (!callingfn)
        goto out;

    ret = snprintf(callstr, strsize, "(");
    PRINT_SIZE_CHECK(ret, out, strsize);

    for ((i = size - 3); i >= 0; i--) {
        ret = snprintf(callstr + savstrsize - strsize, strsize, "-->%s ",
                       callingfn[i]);
        PRINT_SIZE_CHECK(ret, out, strsize);
    }

    ret = snprintf(callstr + savstrsize - strsize, strsize, ")");
    PRINT_SIZE_CHECK(ret, out, strsize);
out:
    FREE(callingfn);
    return ret;
}

int
_gf_msg_nomem(const char *domain, const char *file, const char *function,
              int line, gf_loglevel_t level, size_t size)
{
    const char *basename = NULL;
    xlator_t *this = NULL;
    struct timeval tv = {
        0,
    };
    int ret = 0;
    int fd = -1;
    char msg[2048] = {
        0,
    };
    char timestr[GF_TIMESTR_SIZE] = {
        0,
    };
    glusterfs_ctx_t *ctx = NULL;
    int wlen = 0;
    int priority;
    struct rusage r_usage;

    this = THIS;
    ctx = this->ctx;

    if (!ctx)
        goto out;

    if (skip_logging(this, level))
        goto out;

    if (!domain || !file || !function) {
        fprintf(stderr, "logging: %s:%s():%d: invalid argument\n", __FILE__,
                __PRETTY_FUNCTION__, __LINE__);
        return -1;
    }

    GET_FILE_NAME_TO_LOG(file, basename);

    ret = gettimeofday(&tv, NULL);
    if (-1 == ret)
        goto out;
    gf_time_fmt_tv(timestr, sizeof timestr, &tv, gf_timefmt_FT);

    /* TODO: Currently we print in the enhanced format, with a message ID
     * of 0. Need to enhance this to support format as configured */
    wlen = snprintf(
        msg, sizeof msg,
        "[%s] %c [MSGID: %" PRIu64
        "]"
        " [%s:%d:%s] %s: no memory "
        "available for size (%" GF_PRI_SIZET
        ") current memory usage in kilobytes %ld"
        " [call stack follows]\n",
        timestr, gf_level_strings[level], (uint64_t)0, basename, line, function,
        domain, size,
        (!getrusage(RUSAGE_SELF, &r_usage) ? r_usage.ru_maxrss : 0));
    if (-1 == wlen) {
        ret = -1;
        goto out;
    }

    /* log to the configured logging service */
    switch (ctx->log.logger) {
        case gf_logger_syslog:
            if (ctx->log.log_control_file_found && ctx->log.gf_log_syslog) {
                SET_LOG_PRIO(level, priority);

                /* if syslog allocates, then this may fail, but we
                 * cannot do much about it at the moment */
                /* There is no fd for syslog, hence no stack printed */
                syslog(priority, "%s", msg);
                break;
            }
            /* NOTE: If syslog control file is absent, which is another
             * way to control logging to syslog, then we will fall through
             * to the gluster log. The ideal way to do things would be to
             * not have the extra control file check */
        case gf_logger_glusterlog:
            pthread_mutex_lock(&ctx->log.logfile_mutex);
            {
                fd = ctx->log.logfile ? fileno(ctx->log.logfile)
                                      : fileno(stderr);
                if (fd == -1) {
                    pthread_mutex_unlock(&ctx->log.logfile_mutex);
                    goto out;
                }

                /* write directly to the fd to prevent out of order
                 * message and stack */
                ret = sys_write(fd, msg, wlen);
                if (ret == -1) {
                    pthread_mutex_unlock(&ctx->log.logfile_mutex);
                    goto out;
                }
#ifdef GF_LINUX_HOST_OS
                /* We want only serious log in 'syslog', not our debug
                 * and trace logs */
                if (ctx->log.gf_log_syslog && level &&
                    (level <= ctx->log.sys_log_level))
                    syslog((level - 1), "%s\n", msg);
#endif
            }
            pthread_mutex_unlock(&ctx->log.logfile_mutex);

            _gf_msg_backtrace_nomem(level, GF_LOG_BACKTRACE_DEPTH);

            break;
    }

out:
    return ret;
}

static int
gf_log_syslog(glusterfs_ctx_t *ctx, const char *domain, const char *file,
              const char *function, int32_t line, gf_loglevel_t level,
              int errnum, uint64_t msgid, char **appmsgstr, char *callstr,
              int graph_id, gf_log_format_t fmt)
{
    int priority;

    SET_LOG_PRIO(level, priority);

    /* log with appropriate format */
    switch (fmt) {
        case gf_logformat_traditional:
            if (!callstr) {
                if (errnum)
                    syslog(priority, "[%s:%d:%s] %d-%s: %s [%s]", file, line,
                           function, graph_id, domain, *appmsgstr,
                           strerror(errnum));
                else
                    syslog(priority, "[%s:%d:%s] %d-%s: %s", file, line,
                           function, graph_id, domain, *appmsgstr);
            } else {
                if (errnum)
                    syslog(priority,
                           "[%s:%d:%s] %s %d-%s:"
                           " %s [%s]",
                           file, line, function, callstr, graph_id, domain,
                           *appmsgstr, strerror(errnum));
                else
                    syslog(priority, "[%s:%d:%s] %s %d-%s: %s", file, line,
                           function, callstr, graph_id, domain, *appmsgstr);
            }
            break;
        case gf_logformat_withmsgid:
            if (!callstr) {
                if (errnum)
                    syslog(priority,
                           "[MSGID: %" PRIu64
                           "]"
                           " [%s:%d:%s] %d-%s: %s [%s]",
                           msgid, file, line, function, graph_id, domain,
                           *appmsgstr, strerror(errnum));
                else
                    syslog(priority,
                           "[MSGID: %" PRIu64
                           "]"
                           " [%s:%d:%s] %d-%s: %s",
                           msgid, file, line, function, graph_id, domain,
                           *appmsgstr);
            } else {
                if (errnum)
                    syslog(priority,
                           "[MSGID: %" PRIu64
                           "]"
                           " [%s:%d:%s] %s %d-%s: %s [%s]",
                           msgid, file, line, function, callstr, graph_id,
                           domain, *appmsgstr, strerror(errnum));
                else
                    syslog(priority,
                           "[MSGID: %" PRIu64
                           "]"
                           " [%s:%d:%s] %s %d-%s: %s",
                           msgid, file, line, function, callstr, graph_id,
                           domain, *appmsgstr);
            }
            break;
        case gf_logformat_cee:
            /* TODO: Enhance CEE with additional parameters */
            gf_syslog(priority, "[%s:%d:%s] %d-%s: %s", file, line, function,
                      graph_id, domain, *appmsgstr);
            break;

        default:
            /* NOTE: should not get here without logging */
            break;
    }

    /* TODO: There can be no errors from gf_syslog? */
    return 0;
}

static int
gf_log_glusterlog(glusterfs_ctx_t *ctx, const char *domain, const char *file,
                  const char *function, int32_t line, gf_loglevel_t level,
                  int errnum, uint64_t msgid, char **appmsgstr, char *callstr,
                  struct timeval tv, int graph_id, gf_log_format_t fmt)
{
    char timestr[GF_TIMESTR_SIZE] = {
        0,
    };
    char *header = NULL;
    char *footer = NULL;
    int ret = 0;

    /* rotate if required */
    gf_log_rotate(ctx);

    /* format the time stamp */
    gf_time_fmt_tv(timestr, sizeof timestr, &tv, gf_timefmt_FT);

    /* generate footer */
    if (errnum) {
        ret = gf_asprintf(&footer, " [%s]\n", strerror(errnum));
    } else {
        ret = gf_asprintf(&footer, " \n");
    }
    if (-1 == ret) {
        goto err;
    }

    /* generate message, inc. the header */
    if (fmt == gf_logformat_traditional) {
        if (!callstr) {
            ret = gf_asprintf(&header,
                              "[%s] %c [%s:%d:%s]"
                              " %d-%s: %s",
                              timestr, gf_level_strings[level], file, line,
                              function, graph_id, domain, *appmsgstr);
        } else {
            ret = gf_asprintf(&header,
                              "[%s] %c [%s:%d:%s] %s"
                              " %d-%s: %s",
                              timestr, gf_level_strings[level], file, line,
                              function, callstr, graph_id, domain, *appmsgstr);
        }
    } else { /* gf_logformat_withmsgid */
        /* CEE log format unsupported in logger_glusterlog, so just
         * print enhanced log format */
        if (!callstr) {
            ret = gf_asprintf(&header,
                              "[%s] %c [MSGID: %" PRIu64
                              "]"
                              " [%s:%d:%s] %d-%s: %s",
                              timestr, gf_level_strings[level], msgid, file,
                              line, function, graph_id, domain, *appmsgstr);
        } else {
            ret = gf_asprintf(&header,
                              "[%s] %c [MSGID: %" PRIu64
                              "]"
                              " [%s:%d:%s] %s %d-%s: %s",
                              timestr, gf_level_strings[level], msgid, file,
                              line, function, callstr, graph_id, domain,
                              *appmsgstr);
        }
    }
    if (-1 == ret) {
        goto err;
    }

    /* send the full message to log */

    pthread_mutex_lock(&ctx->log.logfile_mutex);
    {
        if (ctx->log.logfile) {
            fprintf(ctx->log.logfile, "%s%s", header, footer);
            fflush(ctx->log.logfile);
        } else if (ctx->log.loglevel >= level) {
            fprintf(stderr, "%s%s", header, footer);
            fflush(stderr);
        }

#ifdef GF_LINUX_HOST_OS
        /* We want only serious logs in 'syslog', not our debug
         * and trace logs */
        if (ctx->log.gf_log_syslog && level &&
            (level <= ctx->log.sys_log_level)) {
            syslog((level - 1), "%s%s", header, footer);
        }
#endif
    }

    /* TODO: Plugin in memory log buffer retention here. For logs not
     * flushed during cores, it would be useful to retain some of the last
     * few messages in memory */
    pthread_mutex_unlock(&ctx->log.logfile_mutex);
    ret = 0;

err:
    GF_FREE(header);
    GF_FREE(footer);

    return ret;
}

static int
gf_syslog_log_repetitions(const char *domain, const char *file,
                          const char *function, int32_t line,
                          gf_loglevel_t level, int errnum, uint64_t msgid,
                          char **appmsgstr, char *callstr, int refcount,
                          struct timeval oldest, struct timeval latest,
                          int graph_id)
{
    int priority;
    char timestr_latest[GF_TIMESTR_SIZE] = {
        0,
    };
    char timestr_oldest[GF_TIMESTR_SIZE] = {
        0,
    };

    SET_LOG_PRIO(level, priority);

    gf_time_fmt_tv(timestr_latest, sizeof timestr_latest, &latest,
                   gf_timefmt_FT);
    gf_time_fmt_tv(timestr_oldest, sizeof timestr_oldest, &oldest,
                   gf_timefmt_FT);

    if (errnum) {
        syslog(priority,
               "The message \"[MSGID: %" PRIu64
               "] [%s:%d:%s] "
               "%d-%s: %s [%s] \" repeated %d times between %s"
               " and %s",
               msgid, file, line, function, graph_id, domain, *appmsgstr,
               strerror(errnum), refcount, timestr_oldest, timestr_latest);
    } else {
        syslog(priority,
               "The message \"[MSGID: %" PRIu64
               "] [%s:%d:%s] "
               "%d-%s: %s \" repeated %d times between %s"
               " and %s",
               msgid, file, line, function, graph_id, domain, *appmsgstr,
               refcount, timestr_oldest, timestr_latest);
    }
    return 0;
}

static int
gf_glusterlog_log_repetitions(glusterfs_ctx_t *ctx, const char *domain,
                              const char *file, const char *function,
                              int32_t line, gf_loglevel_t level, int errnum,
                              uint64_t msgid, char **appmsgstr, char *callstr,
                              int refcount, struct timeval oldest,
                              struct timeval latest, int graph_id)
{
    int ret = 0;
    char timestr_latest[GF_TIMESTR_SIZE] = {
        0,
    };
    char timestr_oldest[GF_TIMESTR_SIZE] = {
        0,
    };
    char errstr[256] = {
        0,
    };
    char *header = NULL;
    char *footer = NULL;

    if (!ctx)
        goto err;

    gf_log_rotate(ctx);

    ret = gf_asprintf(&header,
                      "The message \"%c [MSGID: %" PRIu64
                      "]"
                      " [%s:%d:%s] %d-%s: %s",
                      gf_level_strings[level], msgid, file, line, function,
                      graph_id, domain, *appmsgstr);
    if (-1 == ret) {
        goto err;
    }

    gf_time_fmt_tv(timestr_latest, sizeof timestr_latest, &latest,
                   gf_timefmt_FT);

    gf_time_fmt_tv(timestr_oldest, sizeof timestr_oldest, &oldest,
                   gf_timefmt_FT);

    if (errnum)
        snprintf(errstr, sizeof(errstr) - 1, " [%s]", strerror(errnum));

    ret = gf_asprintf(&footer, "%s\" repeated %d times between [%s] and [%s]",
                      errstr, refcount, timestr_oldest, timestr_latest);
    if (-1 == ret) {
        ret = -1;
        goto err;
    }

    pthread_mutex_lock(&ctx->log.logfile_mutex);
    {
        if (ctx->log.logfile) {
            fprintf(ctx->log.logfile, "%s%s\n", header, footer);
            fflush(ctx->log.logfile);
        } else if (ctx->log.loglevel >= level) {
            fprintf(stderr, "%s%s\n", header, footer);
            fflush(stderr);
        }

#ifdef GF_LINUX_HOST_OS
        /* We want only serious logs in 'syslog', not our debug
         * and trace logs */
        if (ctx->log.gf_log_syslog && level &&
            (level <= ctx->log.sys_log_level))
            syslog((level - 1), "%s%s\n", header, footer);
#endif
    }

    /* TODO: Plugin in memory log buffer retention here. For logs not
     * flushed during cores, it would be useful to retain some of the last
     * few messages in memory */
    pthread_mutex_unlock(&ctx->log.logfile_mutex);
    ret = 0;

err:
    GF_FREE(header);
    GF_FREE(footer);

    return ret;
}

static int
gf_log_print_with_repetitions(glusterfs_ctx_t *ctx, const char *domain,
                              const char *file, const char *function,
                              int32_t line, gf_loglevel_t level, int errnum,
                              uint64_t msgid, char **appmsgstr, char *callstr,
                              int refcount, struct timeval oldest,
                              struct timeval latest, int graph_id)
{
    int ret = -1;
    gf_log_logger_t logger = ctx->log.logger;

    switch (logger) {
        case gf_logger_syslog:
            if (ctx->log.log_control_file_found && ctx->log.gf_log_syslog) {
                ret = gf_syslog_log_repetitions(
                    domain, file, function, line, level, errnum, msgid,
                    appmsgstr, callstr, refcount, oldest, latest, graph_id);
                break;
            }
            /* NOTE: If syslog control file is absent, which is another
             * way to control logging to syslog, then we will fall through
             * to the gluster log. The ideal way to do things would be to
             * not have the extra control file check */

        case gf_logger_glusterlog:
            ret = gf_glusterlog_log_repetitions(
                ctx, domain, file, function, line, level, errnum, msgid,
                appmsgstr, callstr, refcount, oldest, latest, graph_id);
            break;
    }

    return ret;
}

static int
gf_log_print_plain_fmt(glusterfs_ctx_t *ctx, const char *domain,
                       const char *file, const char *function, int32_t line,
                       gf_loglevel_t level, int errnum, uint64_t msgid,
                       char **appmsgstr, char *callstr, struct timeval tv,
                       int graph_id, gf_log_format_t fmt)
{
    int ret = -1;
    gf_log_logger_t logger = 0;

    logger = ctx->log.logger;

    /* log to the configured logging service */
    switch (logger) {
        case gf_logger_syslog:
            if (ctx->log.log_control_file_found && ctx->log.gf_log_syslog) {
                ret = gf_log_syslog(ctx, domain, file, function, line, level,
                                    errnum, msgid, appmsgstr, callstr, graph_id,
                                    fmt);
                break;
            }
            /* NOTE: If syslog control file is absent, which is another
             * way to control logging to syslog, then we will fall through
             * to the gluster log. The ideal way to do things would be to
             * not have the extra control file check */
        case gf_logger_glusterlog:
            ret = gf_log_glusterlog(ctx, domain, file, function, line, level,
                                    errnum, msgid, appmsgstr, callstr, tv,
                                    graph_id, fmt);
            break;
    }

    return ret;
}

void
gf_log_flush_message(log_buf_t *buf, glusterfs_ctx_t *ctx)
{
    if (buf->refcount == 1) {
        (void)gf_log_print_plain_fmt(ctx, buf->domain, buf->file, buf->function,
                                     buf->line, buf->level, buf->errnum,
                                     buf->msg_id, &buf->msg, NULL, buf->latest,
                                     buf->graph_id, gf_logformat_withmsgid);
    }

    if (buf->refcount > 1) {
        gf_log_print_with_repetitions(
            ctx, buf->domain, buf->file, buf->function, buf->line, buf->level,
            buf->errnum, buf->msg_id, &buf->msg, NULL, buf->refcount,
            buf->oldest, buf->latest, buf->graph_id);
    }
    return;
}

static void
gf_log_flush_list(struct list_head *copy, glusterfs_ctx_t *ctx)
{
    log_buf_t *iter = NULL;
    log_buf_t *tmp = NULL;

    list_for_each_entry_safe(iter, tmp, copy, msg_list)
    {
        gf_log_flush_message(iter, ctx);
        list_del_init(&iter->msg_list);
        log_buf_destroy(iter);
    }
}

void
gf_log_flush_msgs(glusterfs_ctx_t *ctx)
{
    struct list_head copy;

    INIT_LIST_HEAD(&copy);

    pthread_mutex_lock(&ctx->log.log_buf_lock);
    {
        list_splice_init(&ctx->log.lru_queue, &copy);
        ctx->log.lru_cur_size = 0;
    }
    pthread_mutex_unlock(&ctx->log.log_buf_lock);

    gf_log_flush_list(&copy, ctx);

    return;
}

static void
gf_log_flush_extra_msgs(glusterfs_ctx_t *ctx, uint32_t new)
{
    int count = 0;
    int i = 0;
    log_buf_t *iter = NULL;
    log_buf_t *tmp = NULL;
    struct list_head copy;

    INIT_LIST_HEAD(&copy);

    /* If the number of outstanding log messages does not cause list
     * overflow even after reducing the size of the list, then do nothing.
     * Otherwise (that is if there are more items in the list than there
     * need to be after reducing its size), move the least recently used
     * 'diff' elements to be flushed into a separate list...
     */

    pthread_mutex_lock(&ctx->log.log_buf_lock);
    {
        if (ctx->log.lru_cur_size <= new)
            goto unlock;
        count = ctx->log.lru_cur_size - new;
        list_for_each_entry_safe(iter, tmp, &ctx->log.lru_queue, msg_list)
        {
            if (i == count)
                break;

            list_del_init(&iter->msg_list);
            list_add_tail(&iter->msg_list, &copy);
            i++;
        }
        ctx->log.lru_cur_size = ctx->log.lru_cur_size - count;
    }
    // ... quickly unlock ...
unlock:
    pthread_mutex_unlock(&ctx->log.log_buf_lock);
    if (list_empty(&copy))
        return;

    TEST_LOG(
        "Log buffer size reduced. About to flush %d extra log "
        "messages",
        count);
    // ... and then flush them outside the lock.
    gf_log_flush_list(&copy, ctx);
    TEST_LOG("Just flushed %d extra log messages", count);

    return;
}

static int
__gf_log_inject_timer_event(glusterfs_ctx_t *ctx)
{
    int ret = -1;
    struct timespec timeout = {
        0,
    };

    if (!ctx)
        goto out;

    if (ctx->log.log_flush_timer) {
        gf_timer_call_cancel(ctx, ctx->log.log_flush_timer);
        ctx->log.log_flush_timer = NULL;
    }

    timeout.tv_sec = ctx->log.timeout;
    timeout.tv_nsec = 0;

    TEST_LOG("Starting timer now. Timeout = %u, current buf size = %d",
             ctx->log.timeout, ctx->log.lru_size);
    ctx->log.log_flush_timer = gf_timer_call_after(
        ctx, timeout, gf_log_flush_timeout_cbk, (void *)ctx);
    if (!ctx->log.log_flush_timer)
        goto out;

    ret = 0;

out:
    return ret;
}

int
gf_log_inject_timer_event(glusterfs_ctx_t *ctx)
{
    int ret = -1;

    if (!ctx)
        return -1;

    pthread_mutex_lock(&ctx->log.log_buf_lock);
    {
        ret = __gf_log_inject_timer_event(ctx);
    }
    pthread_mutex_unlock(&ctx->log.log_buf_lock);

    return ret;
}

void
gf_log_flush_timeout_cbk(void *data)
{
    glusterfs_ctx_t *ctx = NULL;

    ctx = (glusterfs_ctx_t *)data;

    TEST_LOG(
        "Log timer timed out. About to flush outstanding messages if "
        "present");
    gf_log_flush_msgs(ctx);

    (void)gf_log_inject_timer_event(ctx);

    return;
}

static int
_gf_msg_internal(const char *domain, const char *file, const char *function,
                 int32_t line, gf_loglevel_t level, int errnum, uint64_t msgid,
                 char **appmsgstr, char *callstr, int graph_id)
{
    int ret = -1;
    uint32_t size = 0;
    const char *basename = NULL;
    xlator_t *this = NULL;
    glusterfs_ctx_t *ctx = NULL;
    log_buf_t *iter = NULL;
    log_buf_t *buf_tmp = NULL;
    log_buf_t *buf_new = NULL;
    log_buf_t *first = NULL;
    struct timeval tv = {
        0,
    };
    gf_boolean_t found = _gf_false;
    gf_boolean_t flush_lru = _gf_false;
    gf_boolean_t flush_logged_msg = _gf_false;

    this = THIS;
    ctx = this->ctx;

    if (!ctx)
        goto out;

    GET_FILE_NAME_TO_LOG(file, basename);

    ret = gettimeofday(&tv, NULL);
    if (ret)
        goto out;

    /* If this function is called via _gf_msg_callingfn () (indicated by a
     * non-NULL callstr), or if the logformat is traditional, flush the
     * message directly to disk.
     */

    if ((callstr) || (ctx->log.logformat == gf_logformat_traditional)) {
        ret = gf_log_print_plain_fmt(ctx, domain, basename, function, line,
                                     level, errnum, msgid, appmsgstr, callstr,
                                     tv, graph_id, gf_logformat_traditional);
        goto out;
    }

    pthread_mutex_lock(&ctx->log.log_buf_lock);
    {
        /* Check if the msg being logged is already part of the list */
        list_for_each_entry_safe_reverse(iter, buf_tmp, &ctx->log.lru_queue,
                                         msg_list)
        {
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

            if (strcmp(domain, iter->domain))
                continue;

            if (strcmp(basename, iter->file))
                continue;

            if (strcmp(function, iter->function))
                continue;

            if (strcmp(*appmsgstr, iter->msg))
                continue;

            // Ah! Found a match!
            list_move_tail(&iter->msg_list, &ctx->log.lru_queue);
            iter->refcount++;
            found = _gf_true;
            // Update the 'latest' timestamp.
            memcpy((void *)&(iter->latest), (void *)&tv,
                   sizeof(struct timeval));
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
        } else if (((ctx->log.lru_cur_size + 1) > size) && (first)) {
            /* If the list is full, flush the lru msg to disk and also
             * release it after unlock, and ...
             * */
            if (first->refcount >= 1)
                TEST_LOG(
                    "Buffer overflow of a buffer whose size limit "
                    "is %d. About to flush least recently used log"
                    " message to disk",
                    size);
            list_del_init(&first->msg_list);
            ctx->log.lru_cur_size--;
            flush_lru = _gf_true;
        }
        /* create a new list element, initialise and enqueue it.
         * Additionally, this being the first occurrence of the msg,
         * log it directly to disk after unlock. */
        buf_new = mem_get0(THIS->ctx->logbuf_pool);
        if (!buf_new) {
            ret = -1;
            goto unlock;
        }
        ret = log_buf_init(buf_new, domain, basename, function, line, level,
                           errnum, msgid, appmsgstr, graph_id);
        if (ret) {
            log_buf_destroy(buf_new);
            goto unlock;
        }

        memcpy((void *)&(buf_new->latest), (void *)&tv, sizeof(struct timeval));
        memcpy((void *)&(buf_new->oldest), (void *)&tv, sizeof(struct timeval));

        list_add_tail(&buf_new->msg_list, &ctx->log.lru_queue);
        ctx->log.lru_cur_size++;
        flush_logged_msg = _gf_true;
        ret = 0;
    }
unlock:
    pthread_mutex_unlock(&ctx->log.log_buf_lock);

    /* Value of @ret is a don't-care below since irrespective of success or
     * failure post setting of @flush_lru, @first must be flushed and freed.
     */
    if (flush_lru) {
        gf_log_flush_message(first, ctx);
        log_buf_destroy(first);
    }
    /* Similarly, irrespective of whether all operations since setting of
     * @flush_logged_msg were successful or not, flush the message being
     * logged to disk in the plain format.
     */
    if (flush_logged_msg) {
        ret = gf_log_print_plain_fmt(ctx, domain, basename, function, line,
                                     level, errnum, msgid, appmsgstr, callstr,
                                     tv, graph_id, gf_logformat_withmsgid);
    }

out:
    return ret;
}

int
_gf_msg(const char *domain, const char *file, const char *function,
        int32_t line, gf_loglevel_t level, int errnum, int trace,
        uint64_t msgid, const char *fmt, ...)
{
    int ret = 0;
    char *msgstr = NULL;
    va_list ap;
    xlator_t *this = THIS;
    glusterfs_ctx_t *ctx = NULL;
    char *callstr = NULL;
    int log_inited = 0;

    if (this == NULL)
        return -1;

    ctx = this->ctx;
    if (ctx == NULL) {
        /* messages before context initialization are ignored */
        return -1;
    }

    /* check if we should be logging */
    if (skip_logging(this, level))
        goto out;

    /* in args check */
    if (!domain || !file || !function || !fmt) {
        fprintf(stderr, "logging: %s:%s():%d: invalid argument\n", __FILE__,
                __PRETTY_FUNCTION__, __LINE__);
        return -1;
    }

    /* form the message */
    va_start(ap, fmt);
    ret = vasprintf(&msgstr, fmt, ap);
    va_end(ap);

    /* log */
    if (ret != -1) {
        if (trace) {
            callstr = GF_MALLOC(GF_LOG_BACKTRACE_SIZE, gf_common_mt_char);
            if (callstr == NULL)
                return -1;

            ret = _gf_msg_backtrace(GF_LOG_BACKTRACE_DEPTH, callstr,
                                    GF_LOG_BACKTRACE_SIZE);
            if (ret < 0) {
                GF_FREE(callstr);
                callstr = NULL;
            }
        }

        pthread_mutex_lock(&ctx->log.logfile_mutex);
        {
            if (ctx->log.logfile) {
                log_inited = 1;
            }
        }
        pthread_mutex_unlock(&ctx->log.logfile_mutex);

        if (!log_inited && ctx->log.gf_log_syslog) {
            ret = gf_log_syslog(
                ctx, domain, file, function, line, level, errnum, msgid,
                &msgstr, (callstr ? callstr : NULL),
                (this->graph) ? this->graph->id : 0, gf_logformat_traditional);
        } else {
            ret = _gf_msg_internal(domain, file, function, line, level, errnum,
                                   msgid, &msgstr, (callstr ? callstr : NULL),
                                   (this->graph) ? this->graph->id : 0);
        }
    } else {
        /* man (3) vasprintf states on error strp contents
         * are undefined, be safe */
        msgstr = NULL;
    }
    if (callstr)
        GF_FREE(callstr);
    FREE(msgstr);

out:
    return ret;
}

/* TODO: Deprecate (delete) _gf_log, _gf_log_callingfn,
 * once messages are changed to use _gf_msgXXX APIs for logging */
int
_gf_log(const char *domain, const char *file, const char *function, int line,
        gf_loglevel_t level, const char *fmt, ...)
{
    const char *basename = NULL;
    FILE *new_logfile = NULL;
    va_list ap;
    char timestr[GF_TIMESTR_SIZE] = {
        0,
    };
    struct timeval tv = {
        0,
    };
    char *logline = NULL;
    char *msg = NULL;
    int ret = 0;
    int fd = -1;
    xlator_t *this = THIS;
    glusterfs_ctx_t *ctx = this->ctx;

    if (!ctx)
        goto out;

    if (skip_logging(this, level))
        goto out;

    if (!domain || !file || !function || !fmt) {
        fprintf(stderr, "logging: %s:%s():%d: invalid argument\n", __FILE__,
                __PRETTY_FUNCTION__, __LINE__);
        return -1;
    }

    basename = strrchr(file, '/');
    if (basename)
        basename++;
    else
        basename = file;

    va_start(ap, fmt);
    ret = vasprintf(&msg, fmt, ap);
    va_end(ap);
    if (-1 == ret) {
        goto err;
    }

    if (ctx->log.log_control_file_found) {
        int priority;
        /* treat GF_LOG_TRACE and GF_LOG_NONE as LOG_DEBUG and
           other level as is */
        if (GF_LOG_TRACE == level || GF_LOG_NONE == level) {
            priority = LOG_DEBUG;
        } else {
            priority = level - 1;
        }

        gf_syslog(priority, "[%s:%d:%s] %d-%s: %s", basename, line, function,
                  ((this->graph) ? this->graph->id : 0), domain, msg);
        goto err;
    }

    if (ctx->log.logrotate) {
        ctx->log.logrotate = 0;

        fd = sys_open(ctx->log.filename, O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
        if (fd < 0) {
            gf_smsg("logrotate", GF_LOG_ERROR, errno,
                    LG_MSG_OPEN_LOGFILE_FAILED, NULL);
            return -1;
        }
        sys_close(fd);

        new_logfile = fopen(ctx->log.filename, "a");
        if (!new_logfile) {
            gf_smsg("logrotate", GF_LOG_CRITICAL, errno,
                    LG_MSG_OPEN_LOGFILE_FAILED, "filename=%s",
                    ctx->log.filename, NULL);
            goto log;
        }

        pthread_mutex_lock(&ctx->log.logfile_mutex);
        {
            if (ctx->log.logfile)
                fclose(ctx->log.logfile);

            ctx->log.gf_log_logfile = ctx->log.logfile = new_logfile;
        }
        pthread_mutex_unlock(&ctx->log.logfile_mutex);
    }

log:
    ret = gettimeofday(&tv, NULL);
    if (-1 == ret)
        goto out;

    gf_time_fmt_tv(timestr, sizeof timestr, &tv, gf_timefmt_FT);

    ret = gf_asprintf(&logline, "[%s] %c [%s:%d:%s] %d-%s: %s\n", timestr,
                      gf_level_strings[level], basename, line, function,
                      ((this->graph) ? this->graph->id : 0), domain, msg);
    if (-1 == ret) {
        goto err;
    }

    pthread_mutex_lock(&ctx->log.logfile_mutex);
    {
        if (ctx->log.logfile) {
            fputs(logline, ctx->log.logfile);
            fflush(ctx->log.logfile);
        } else if (ctx->log.loglevel >= level) {
            fputs(logline, stderr);
            fflush(stderr);
        }

#ifdef GF_LINUX_HOST_OS
        /* We want only serious log in 'syslog', not our debug
           and trace logs */
        if (ctx->log.gf_log_syslog && level &&
            (level <= ctx->log.sys_log_level))
            syslog((level - 1), "%s", logline);
#endif
    }

    pthread_mutex_unlock(&ctx->log.logfile_mutex);

err:
    GF_FREE(logline);

    FREE(msg);

out:
    return (0);
}

int
_gf_log_eh(const char *function, const char *fmt, ...)
{
    int ret = -1;
    va_list ap;
    char *logline = NULL;
    char *msg = NULL;
    xlator_t *this = NULL;

    this = THIS;

    va_start(ap, fmt);
    ret = vasprintf(&msg, fmt, ap);
    va_end(ap);
    if (-1 == ret) {
        goto out;
    }

    ret = gf_asprintf(&logline, "[%d] %s: %s",
                      ((this->graph) ? this->graph->id : 0), function, msg);
    if (-1 == ret) {
        goto out;
    }

    ret = eh_save_history(this->history, logline);

out:
    GF_FREE(logline);

    FREE(msg);

    return ret;
}

int
gf_cmd_log_init(const char *filename)
{
    int fd = -1;
    xlator_t *this = NULL;
    glusterfs_ctx_t *ctx = NULL;

    this = THIS;
    ctx = this->ctx;

    if (!ctx)
        return -1;

    if (!filename) {
        gf_smsg(this->name, GF_LOG_CRITICAL, 0, LG_MSG_FILENAME_NOT_SPECIFIED,
                "gf_cmd_log_init", NULL);
        return -1;
    }

    ctx->log.cmd_log_filename = gf_strdup(filename);
    if (!ctx->log.cmd_log_filename) {
        return -1;
    }
    /* close and reopen cmdlogfile for log rotate*/
    if (ctx->log.cmdlogfile) {
        fclose(ctx->log.cmdlogfile);
        ctx->log.cmdlogfile = NULL;
    }

    fd = sys_open(ctx->log.cmd_log_filename, O_CREAT | O_WRONLY | O_APPEND,
                  S_IRUSR | S_IWUSR);
    if (fd < 0) {
        gf_smsg(this->name, GF_LOG_CRITICAL, errno, LG_MSG_OPEN_LOGFILE_FAILED,
                "cmd_log_file", NULL);
        return -1;
    }

    ctx->log.cmdlogfile = fdopen(fd, "a");
    if (!ctx->log.cmdlogfile) {
        gf_smsg(this->name, GF_LOG_CRITICAL, errno, LG_MSG_OPEN_LOGFILE_FAILED,
                "gf_cmd_log_init: %s", ctx->log.cmd_log_filename, NULL);
        sys_close(fd);
        return -1;
    }
    return 0;
}

int
gf_cmd_log(const char *domain, const char *fmt, ...)
{
    va_list ap;
    char timestr[GF_TIMESTR_SIZE];
    struct timeval tv = {
        0,
    };
    char *logline = NULL;
    char *msg = NULL;
    int ret = 0;
    int fd = -1;
    glusterfs_ctx_t *ctx = NULL;

    ctx = THIS->ctx;

    if (!ctx)
        return -1;

    if (!ctx->log.cmdlogfile)
        return -1;

    if (!domain || !fmt) {
        gf_msg_trace("glusterd", 0, "logging: invalid argument\n");
        return -1;
    }

    ret = gettimeofday(&tv, NULL);
    if (ret == -1)
        goto out;
    va_start(ap, fmt);
    ret = vasprintf(&msg, fmt, ap);
    va_end(ap);
    if (ret == -1) {
        goto out;
    }

    gf_time_fmt_tv(timestr, sizeof timestr, &tv, gf_timefmt_FT);

    ret = gf_asprintf(&logline, "[%s] %s : %s\n", timestr, domain, msg);
    if (ret == -1) {
        goto out;
    }

    /* close and reopen cmdlogfile fd for in case of log rotate*/
    if (ctx->log.cmd_history_logrotate) {
        ctx->log.cmd_history_logrotate = 0;

        if (ctx->log.cmdlogfile) {
            fclose(ctx->log.cmdlogfile);
            ctx->log.cmdlogfile = NULL;
        }

        fd = sys_open(ctx->log.cmd_log_filename, O_CREAT | O_WRONLY | O_APPEND,
                      S_IRUSR | S_IWUSR);
        if (fd < 0) {
            gf_smsg(THIS->name, GF_LOG_CRITICAL, errno,
                    LG_MSG_OPEN_LOGFILE_FAILED, "name=%s",
                    ctx->log.cmd_log_filename, NULL);
            ret = -1;
            goto out;
        }

        ctx->log.cmdlogfile = fdopen(fd, "a");
        if (!ctx->log.cmdlogfile) {
            gf_smsg(THIS->name, GF_LOG_CRITICAL, errno,
                    LG_MSG_OPEN_LOGFILE_FAILED, "name=%s",
                    ctx->log.cmd_log_filename, NULL);
            ret = -1;
            sys_close(fd);
            goto out;
        }
    }

    fputs(logline, ctx->log.cmdlogfile);
    fflush(ctx->log.cmdlogfile);

out:
    GF_FREE(logline);

    FREE(msg);

    return ret;
}

static int
_do_slog_format(int errnum, const char *event, va_list inp, char **msg)
{
    va_list valist_tmp;
    int i = 0;
    int j = 0;
    int k = 0;
    int ret = 0;
    char *fmt = NULL;
    char *buffer = NULL;
    int num_format_chars = 0;
    char format_char = '%';
    char *tmp1 = NULL;
    char *tmp2 = NULL;
    char temp_sep[3] = "";

    tmp2 = gf_strdup("");
    if (!tmp2) {
        ret = -1;
        goto out;
    }

    /* Hardcoded value for max key value pairs, exits early */
    /* from loop if found NULL */
    for (i = 0; i < GF_MAX_SLOG_PAIR_COUNT; i++) {
        fmt = va_arg(inp, char *);
        if (fmt == NULL) {
            break;
        }

        /* Get number of times % is used in input for formatting, */
        /* this count will be used to skip those many args from the */
        /* main list and will be used to format inner format */
        num_format_chars = 0;
        for (k = 0; fmt[k] != '\0'; k++) {
            /* If %% is used then that is escaped */
            if (fmt[k] == format_char && fmt[k + 1] == format_char) {
                k++;
            } else if (fmt[k] == format_char) {
                num_format_chars++;
            }
        }

        tmp1 = gf_strdup(tmp2);
        if (!tmp1) {
            ret = -1;
            goto out;
        }

        GF_FREE(tmp2);
        tmp2 = NULL;

        if (num_format_chars > 0) {
            /* Make separate valist and format the string */
            va_copy(valist_tmp, inp);
            ret = gf_vasprintf(&buffer, fmt, valist_tmp);
            if (ret < 0) {
                va_end(valist_tmp);
                goto out;
            }
            va_end(valist_tmp);

            for (j = 0; j < num_format_chars; j++) {
                /* Skip the va_arg value since these values
                   are already used for internal formatting */
                (void)va_arg(inp, void *);
            }

            ret = gf_asprintf(&tmp2, "%s%s{%s}", tmp1, temp_sep, buffer);
            if (ret < 0)
                goto out;

            GF_FREE(buffer);
            buffer = NULL;
        } else {
            ret = gf_asprintf(&tmp2, "%s%s{%s}", tmp1, temp_sep, fmt);
            if (ret < 0)
                goto out;
        }

        /* Set seperator for next iteration */
        temp_sep[0] = ',';
        temp_sep[1] = ' ';
        temp_sep[2] = 0;

        GF_FREE(tmp1);
        tmp1 = NULL;
    }

    tmp1 = gf_strdup(tmp2);
    if (!tmp1) {
        ret = -1;
        goto out;
    }
    GF_FREE(tmp2);
    tmp2 = NULL;

    if (errnum) {
        ret = gf_asprintf(&tmp2, "%s [%s%s{errno=%d}, {error=%s}]", event, tmp1,
                          temp_sep, errnum, strerror(errnum));
    } else {
        ret = gf_asprintf(&tmp2, "%s [%s]", event, tmp1);
    }

    if (ret == -1)
        goto out;

    *msg = gf_strdup(tmp2);
    if (!*msg)
        ret = -1;

out:
    if (buffer)
        GF_FREE(buffer);

    if (tmp1)
        GF_FREE(tmp1);

    if (tmp2)
        GF_FREE(tmp2);

    return ret;
}

int
_gf_smsg(const char *domain, const char *file, const char *function,
         int32_t line, gf_loglevel_t level, int errnum, int trace,
         uint64_t msgid, const char *event, ...)
{
    va_list valist;
    char *msg = NULL;
    int ret = 0;
    xlator_t *this = THIS;

    if (skip_logging(this, level))
        return ret;

    va_start(valist, event);
    ret = _do_slog_format(errnum, event, valist, &msg);
    if (ret == -1)
        goto out;

    /* Pass errnum as zero since it is already formated as required */
    ret = _gf_msg(domain, file, function, line, level, 0, trace, msgid, "%s",
                  msg);

out:
    va_end(valist);
    if (msg)
        GF_FREE(msg);
    return ret;
}
