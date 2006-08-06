
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#include "logging.h"

static pthread_mutex_t logfile_mutex;
static FILE *logfile;
static loglevel = LOG_NORMAL;

gluster_loglevel 
gluster_log_get_loglevel (void)
{
  return loglevel;
}

void
gluster_log_set_loglevel (gluster_loglevel level)
{
  loglevel = level;
}

void 
gluster_log_init (const char *filename)
{
  pthread_mutex_init (&logfile_mutex, NULL);
  logfile = fopen (filename, "a");
  if (!logfile) {
    fprintf (stderr, "gluster_log_init: Could not open logfile %s: (%s)\n", logfile, strerror (errno));
    exit (1);
  }
}

void
gluster_log (const char *domain, gluster_loglevel level, const char *fmt, ...)
{
  va_list ap;
  if (level <= loglevel) {
    pthread_mutex_lock (&logfile_mutex);

    va_start (ap, fmt);
    time_t utime = time (NULL);
    struct tm *tm = localtime (&utime);
    char timestr[256];

    strftime (timestr, 256, "[%b %d %H:%M:%S]", tm);

    fprintf (logfile, level == LOG_CRITICAL ? "** CRITICAL ** %s %s: " : "%s %s: ", timestr, domain);
    vfprintf (logfile, fmt, ap);
    va_end (ap);
    fprintf (logfile, "\n");
    fflush (logfile);

    pthread_mutex_unlock (&logfile_mutex);
  }
}
