
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include "logging.h"

static pthread_mutex_t logfile_mutex;
static FILE *logfile;
static gf_loglevel loglevel = LOG_NORMAL;

gf_loglevel 
gf_log_get_loglevel (void)
{
  return loglevel;
}

void
gf_log_set_loglevel (gf_loglevel level)
{
  loglevel = level;
}

int
gf_log_init (const char *filename)
{
  if (!filename)
    return (-1);

  pthread_mutex_init (&logfile_mutex, NULL);
  logfile = fopen (filename, "a");
  if (!logfile)
    return (-1);
  return (0);
}

int
gf_log (const char *domain, gf_loglevel level, const char *fmt, ...)
{
  va_list ap;
  
  if (!domain || !fmt)
    return (-1);

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
  return (0);
}
