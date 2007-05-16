/*
  (C) 2006 Z RESEARCH Inc. <http://www.zresearch.com>
  
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License, or (at your option) any later version.
    
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
    
  You should have received a copy of the GNU General Public
  License along with this program; if not, write to the Free
  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301 USA
*/ 


#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <locale.h>
#include <string.h>
#include <stdlib.h>
#include "logging.h"

static pthread_mutex_t logfile_mutex;
static FILE *logfile;
static gf_loglevel_t loglevel = GF_LOG_MAX;

gf_loglevel_t 
gf_log_get_loglevel (void)
{
  return loglevel;
}

void
gf_log_set_loglevel (gf_loglevel_t level)
{
  loglevel = level;
}

int32_t 
gf_log_init (const char *filename)
{
  if (!filename){
    fprintf (stderr, "gf_log_init: no filename specified\n");
    return (-1);
  }

  pthread_mutex_init (&logfile_mutex, NULL);
  logfile = fopen (filename, "a");
  if (!logfile){
    fprintf (stderr,
	     "gf_log_init: failed to open logfile \"%s\" (%s)\n",
	     filename,
	     strerror (errno));
    return (-1);
  }
  return (0);
}

int32_t 
_gf_log (const char *domain,
	 const char *file,
	 const char *function,
	 int32_t line,
	 gf_loglevel_t level, const char *fmt, ...)
{
  static char *level_strings[] = {"NONE",
				  "!!CRIT!!",
				  "**ERROR**",
				  "--WARN--",
				  "<<DEBUG>>"};
  const char *basename;

  va_list ap;

  if (level > loglevel)
    return 0;

  if (!logfile) {
    fprintf (stderr, "no logfile set\n");
    return (-1);
  }

  if (!domain || !fmt)
    return (-1);

  if (level <= loglevel) {
    pthread_mutex_lock (&logfile_mutex);

    va_start (ap, fmt);
    time_t utime = time (NULL);
    struct tm *tm = localtime (&utime);
    char timestr[256];

    strftime (timestr, 256, "%Y-%m-%d %H:%M:%S", tm); 
    /* strftime (timestr, sizeof(timestr), nl_langinfo (D_T_FMT), tm); */

    basename = strrchr (file, '/');
    if (basename)
      basename++;
    else
      basename = file;
    fprintf (logfile, "%s %s [%s:%d:%s] %s: ",
	     timestr,
	     level_strings[level],
	     basename,
	     line,
	     function,
	     domain);
      
    vfprintf (logfile, fmt, ap);
    va_end (ap);
    fprintf (logfile, "\n");
    fflush (logfile);

    pthread_mutex_unlock (&logfile_mutex);
  }
  return (0);
}
