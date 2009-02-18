/*
  Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <locale.h>
#include <string.h>
#include <stdlib.h>
#include "logging.h"


static pthread_mutex_t  logfile_mutex;
static char            *filename = NULL;
static uint8_t          logrotate = 0;

static FILE            *logfile = NULL;
static gf_loglevel_t    loglevel = GF_LOG_MAX;

gf_loglevel_t           gf_log_loglevel; /* extern'd */
FILE                   *gf_log_logfile;


void 
gf_log_logrotate (int signum)
{
	logrotate = 1;
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

	filename = strdup (file);
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

	gf_log_logfile = logfile;

	return 0;
}


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
_gf_log (const char *domain, const char *file, const char *function, int line,
	 gf_loglevel_t level, const char *fmt, ...)
{
	const char  *basename = NULL;
	FILE        *new_logfile = NULL;
	va_list      ap;
	time_t       utime = 0;
	struct tm   *tm = NULL;
	char         timestr[256];
	static char *level_strings[] = {"N", /* NONE */
					"T", /* TRACE */
					"C", /* CRITICAL */
					"E", /* ERROR */
					"W", /* WARNING */
					"N", /* TRACE (GF_LOG_NORMAL) */
					"D", /* DEBUG */
					""};
  
	if (!domain || !file || !function || !fmt) {
		fprintf (stderr, 
			 "logging: %s:%s():%d: invalid argument\n", 
			 __FILE__, __PRETTY_FUNCTION__, __LINE__);
		return -1;
	}
  
	if (!logfile) {
		fprintf (stderr, "no logfile set\n");
		return (-1);
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
	utime = time (NULL);
	tm    = localtime (&utime);

	if (level > loglevel) {
		goto out;
	}

	pthread_mutex_lock (&logfile_mutex);
	{
		va_start (ap, fmt);

		strftime (timestr, 256, "%Y-%m-%d %H:%M:%S", tm); 

		basename = strrchr (file, '/');
		if (basename)
			basename++;
		else
			basename = file;

		fprintf (logfile, "%s %s [%s:%d:%s] %s: ",
			 timestr, level_strings[level],
			 basename, line, function,
			 domain);
      
		vfprintf (logfile, fmt, ap);
		va_end (ap);
		fprintf (logfile, "\n");
		fflush (logfile);
	}
	pthread_mutex_unlock (&logfile_mutex);

out:
	return (0);
}
