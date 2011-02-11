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

#include "xlator.h"
#include "logging.h"
#include "defaults.h"

#ifdef GF_LINUX_HOST_OS
#include <syslog.h>
#endif

#ifdef HAVE_BACKTRACE
#include <execinfo.h>
#endif


static pthread_mutex_t  logfile_mutex;
static char            *filename = NULL;
static uint8_t          logrotate = 0;
static FILE            *logfile = NULL;
static gf_loglevel_t    loglevel = GF_LOG_MAX;
static int              gf_log_syslog = 0;

char                    gf_log_xl_log_set;
gf_loglevel_t           gf_log_loglevel; /* extern'd */
FILE                   *gf_log_logfile;

static char            *cmd_log_filename = NULL;
static FILE            *cmdlogfile = NULL;

void
gf_log_logrotate (int signum)
{
	logrotate = 1;
}

void
gf_log_enable_syslog (void)
{
        gf_log_syslog = 1;
}

void
gf_log_disable_syslog (void)
{
        gf_log_syslog = 0;
}

gf_loglevel_t
gf_log_get_loglevel (void)
{
	return loglevel;
}

void
gf_log_set_loglevel (gf_loglevel_t level)
{
        gf_log_loglevel = loglevel = level;
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
        gf_log_xl_log_set = 1;
        xl->loglevel = level;
}

void
gf_log_fini (void)
{
	pthread_mutex_destroy (&logfile_mutex);
}


int
gf_log_init (const char *file)
{
	if (!file){
		fprintf (stderr, "gf_log_init: no filename specified\n");
		return -1;
	}

	pthread_mutex_init (&logfile_mutex, NULL);

        filename = gf_strdup (file);
	if (!filename) {
		fprintf (stderr, "gf_log_init: strdup error\n");
		return -1;
	}

	logfile = fopen (file, "a");
	if (!logfile){
		fprintf (stderr,
			 "gf_log_init: failed to open logfile \"%s\" (%s)\n",
			 file,
			 strerror (errno));
		return -1;
	}

#ifdef GF_LINUX_HOST_OS
        /* For the 'syslog' output. one can grep 'GlusterFS' in syslog
           for serious logs */
        openlog ("GlusterFS", LOG_PID, LOG_DAEMON);
#endif

	gf_log_logfile = logfile;

	return 0;
}


/*
 * Initialize logging to a central server.
 * If successful, log messages will be written both to
 * the local file and to the remote server.
 */

static int __central_log_enabled = 0;

struct _msg_queue {
        struct list_head msgs;
};

struct _log_msg {
        const char *msg;
        struct list_head queue;
};




void
gf_log_lock (void)
{
	pthread_mutex_lock (&logfile_mutex);
}


void
gf_log_unlock (void)
{
	pthread_mutex_unlock (&logfile_mutex);
}


void
gf_log_cleanup (void)
{
	pthread_mutex_destroy (&logfile_mutex);
}

int
_gf_log_callingfn (const char *domain, const char *file, const char *function,
                   int line, gf_loglevel_t level, const char *fmt, ...)
{
	const char     *basename        = NULL;
	struct tm      *tm              = NULL;
        xlator_t       *this            = NULL;
        char           *str1            = NULL;
        char           *str2            = NULL;
        char           *msg             = NULL;
	char            timestr[256]    = {0,};
	char            callstr[4096]   = {0,};
        struct timeval  tv              = {0,};
        size_t          len             = 0;
        int             ret             = 0;
        gf_loglevel_t   xlator_loglevel = 0;
	va_list         ap;

	if (!logfile)
		return -1;

        this = THIS;

        xlator_loglevel = this->loglevel;
        if (xlator_loglevel == 0)
                xlator_loglevel = loglevel;

        if (level > xlator_loglevel)
                goto out;

	static char *level_strings[] = {"",  /* NONE */
                                        "M", /* EMERGENCY */
                                        "A", /* ALERT */
					"C", /* CRITICAL */
					"E", /* ERROR */
					"W", /* WARNING */
					"N", /* NOTICE */
                                        "I", /* INFO/NORMAL */
					"D", /* DEBUG */
                                        "T", /* TRACE */
					""};

	if (!domain || !file || !function || !fmt) {
		fprintf (stderr,
			 "logging: %s:%s():%d: invalid argument\n",
			 __FILE__, __PRETTY_FUNCTION__, __LINE__);
		return -1;
	}

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

        ret = gettimeofday (&tv, NULL);
        if (-1 == ret)
                goto out;

	tm    = localtime (&tv.tv_sec);

	pthread_mutex_lock (&logfile_mutex);
	{
		va_start (ap, fmt);

		strftime (timestr, 256, "%Y-%m-%d %H:%M:%S", tm);
                snprintf (timestr + strlen (timestr), 256 - strlen (timestr),
                          ".%"GF_PRI_SUSECONDS, tv.tv_usec);

		basename = strrchr (file, '/');
		if (basename)
			basename++;
		else
			basename = file;

                ret = gf_asprintf (&str1, "[%s] %s [%s:%d:%s] %s %s: ",
                                   timestr, level_strings[level],
                                   basename, line, function, callstr,
                                   domain);
                if (-1 == ret) {
                        goto unlock;
                }

                ret = vasprintf (&str2, fmt, ap);
                if (-1 == ret) {
                        goto unlock;
                }

		va_end (ap);

                len = strlen (str1);
                msg = GF_MALLOC (len + strlen (str2) + 1, gf_common_mt_char);

                strcpy (msg, str1);
                strcpy (msg + len, str2);

		fprintf (logfile, "%s\n", msg);
		fflush (logfile);

#ifdef GF_LINUX_HOST_OS
                /* We want only serious log in 'syslog', not our debug
                   and trace logs */
                if (gf_log_syslog && level && (level <= GF_LOG_ERROR))
                        syslog ((level-1), "%s\n", msg);
#endif
	}

unlock:
	pthread_mutex_unlock (&logfile_mutex);

        if (msg) {
                GF_FREE (msg);
        }

        if (str1)
                GF_FREE (str1);

        if (str2)
                FREE (str2);

out:
        return ret;
}

int
_gf_log (const char *domain, const char *file, const char *function, int line,
	 gf_loglevel_t level, const char *fmt, ...)
{
	const char  *basename = NULL;
	FILE        *new_logfile = NULL;
	va_list      ap;
	struct tm   *tm = NULL;
	char         timestr[256];
        struct timeval tv = {0,};

        char        *str1 = NULL;
        char        *str2 = NULL;
        char        *msg  = NULL;
        size_t       len  = 0;
        int          ret  = 0;
        xlator_t    *this = NULL;
        gf_loglevel_t xlator_loglevel = 0;

	if (!logfile)
		return -1;

        this = THIS;

        xlator_loglevel = this->loglevel;
        if (xlator_loglevel == 0)
                xlator_loglevel = loglevel;

        if (level > xlator_loglevel)
                goto out;

	static char *level_strings[] = {"",  /* NONE */
                                        "M", /* EMERGENCY */
                                        "A", /* ALERT */
					"C", /* CRITICAL */
					"E", /* ERROR */
					"W", /* WARNING */
					"N", /* NOTICE */
                                        "I", /* INFO/NORMAL */
					"D", /* DEBUG */
                                        "T", /* TRACE */
					""};

	if (!domain || !file || !function || !fmt) {
		fprintf (stderr,
			 "logging: %s:%s():%d: invalid argument\n",
			 __FILE__, __PRETTY_FUNCTION__, __LINE__);
		return -1;
	}


	if (logrotate) {
		logrotate = 0;

		new_logfile = fopen (filename, "a");
		if (!new_logfile) {
			gf_log ("logrotate", GF_LOG_CRITICAL,
				"failed to open logfile %s (%s)",
				filename, strerror (errno));
			goto log;
		}

		fclose (logfile);
		gf_log_logfile = logfile = new_logfile;
	}

log:
        ret = gettimeofday (&tv, NULL);
        if (-1 == ret)
                goto out;

	tm    = localtime (&tv.tv_sec);

	pthread_mutex_lock (&logfile_mutex);
	{
		va_start (ap, fmt);

		strftime (timestr, 256, "%Y-%m-%d %H:%M:%S", tm);
                snprintf (timestr + strlen (timestr), 256 - strlen (timestr),
                          ".%"GF_PRI_SUSECONDS, tv.tv_usec);

		basename = strrchr (file, '/');
		if (basename)
			basename++;
		else
			basename = file;

                ret = gf_asprintf (&str1, "[%s] %s [%s:%d:%s] %s: ",
                                   timestr, level_strings[level],
                                   basename, line, function,
                                   domain);
                if (-1 == ret) {
                        goto unlock;
                }

                ret = vasprintf (&str2, fmt, ap);
                if (-1 == ret) {
                        goto unlock;
                }

		va_end (ap);

                len = strlen (str1);
                msg = GF_MALLOC (len + strlen (str2) + 1, gf_common_mt_char);

                strcpy (msg, str1);
                strcpy (msg + len, str2);

		fprintf (logfile, "%s\n", msg);
		fflush (logfile);

#ifdef GF_LINUX_HOST_OS
                /* We want only serious log in 'syslog', not our debug
                   and trace logs */
                if (gf_log_syslog && level && (level <= GF_LOG_ERROR))
                        syslog ((level-1), "%s\n", msg);
#endif
	}

unlock:
	pthread_mutex_unlock (&logfile_mutex);

        if (msg) {
                if ((ret != -1) && __central_log_enabled &&
                    ((glusterfs_central_log_flag_get ()) == 0)) {

                        glusterfs_central_log_flag_set ();
                        {
                                //gf_log_central (msg);
                        }
                        glusterfs_central_log_flag_unset ();
                }
                GF_FREE (msg);
        }

        if (str1)
                GF_FREE (str1);

        if (str2)
                FREE (str2);

out:
	return (0);
}


struct _client_log {
        char *identifier;
        FILE *file;
        struct list_head list;
};

struct _client_log *client_logs = NULL;


static void
client_log_init (struct _client_log *cl, char *identifier)
{
        int   ret = 0;
        char *path = NULL;

        cl->identifier = identifier;

        ret = gf_asprintf (&path, "%s.client-%s", filename, identifier);
        if (-1 == ret) {
                return;
        }
        cl->file = fopen (path, "a");
        GF_FREE (path);

        INIT_LIST_HEAD (&cl->list);
}


static FILE *
__logfile_for_client (char *identifier)
{
        struct _client_log *client = NULL;

        if (!client_logs) {
                client = GF_CALLOC (1, sizeof (*client),
                                        gf_common_mt_client_log);
                if (!client)
                        return NULL;

                client_log_init (client, identifier);

                client_logs = client;
        }

        list_for_each_entry (client, &client_logs->list, list) {
                if (!strcmp (client->identifier, identifier))
                        break;
        }

        if (!client) {
                client = GF_CALLOC (1, sizeof (*client),
                                        gf_common_mt_client_log);
                if (!client)
                        return NULL;

                client_log_init (client, identifier);

                list_add_tail (&client->list, &client_logs->list);
        }

        return client->file;
}


int
gf_log_from_client (const char *msg, char *identifier)
{
        FILE *client_log = NULL;

        client_log = __logfile_for_client (identifier);

        fprintf (client_log, "%s\n", msg);
        fflush (client_log);

        return 0;
}

int
gf_cmd_log_init (const char *filename)
{
        if (!filename){
                gf_log ("glusterd", GF_LOG_CRITICAL, "gf_cmd_log_init: no "
                        "filename specified\n");
                return -1;
        }

        cmd_log_filename = gf_strdup (filename);
        if (!cmd_log_filename) {
                gf_log ("glusterd", GF_LOG_CRITICAL, "gf_cmd_log_init: strdup"
                        " error\n");
                return -1;
        }

        cmdlogfile = fopen (cmd_log_filename, "a");
        if (!cmdlogfile){
                gf_log ("glusterd", GF_LOG_CRITICAL,
                         "gf_cmd_log_init: failed to open logfile \"%s\" "
                         "(%s)\n", cmd_log_filename, strerror (errno));
                return -1;
        }
        return 0;
}

int
gf_cmd_log (const char *domain, const char *fmt, ...)
{
        va_list      ap;
        struct tm   *tm = NULL;
        char         timestr[256];
        struct timeval tv = {0,};
        char        *str1 = NULL;
        char        *str2 = NULL;
        char        *msg  = NULL;
        size_t       len  = 0;
        int          ret  = 0;

        if (!cmdlogfile)
                return -1;


        if (!domain || !fmt) {
                gf_log ("glusterd", GF_LOG_TRACE,
                         "logging: invalid argument\n");
                return -1;
        }

        ret = gettimeofday (&tv, NULL);
        if (ret == -1)
                goto out;

        tm = localtime (&tv.tv_sec);

        va_start (ap, fmt);
        strftime (timestr, 256, "%Y-%m-%d %H:%M:%S", tm);
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

        fprintf (cmdlogfile, "%s\n", msg);
        fflush (cmdlogfile);

out:
        if (msg) {
               GF_FREE (msg);
        }

        if (str1)
                GF_FREE (str1);

        if (str2)
                FREE (str2);

        return (0);
}
