
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <locale.h>
#include <langinfo.h>
#include <string.h>
#include <stdlib.h>
#include "logging.h"

static pthread_mutex_t logfile_mutex;
static FILE *logfile;
static gf_loglevel loglevel = LOG_MAX;

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
  if (!filename){
    fprintf (stderr, "gf_log_init: no filename specified\n");
    return (-1);
  }

  fprintf (stderr, "gf_log_init: using log file \"%s\"\n", filename);
  setlocale (LC_ALL, "");

  pthread_mutex_init (&logfile_mutex, NULL);
  logfile = fopen (filename, "a");
  if (!logfile){
    fprintf (stderr, "gf_log_init: failed to open logfile \"%s\"\n", filename);
    return (-1);
  }
  return (0);
}

int
gf_log (const char *domain, gf_loglevel level, const char *fmt, ...)
{
  va_list ap;

  if (!logfile){
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

    /* strftime (timestr, 256, "[%b %d %H:%M:%S]", tm); */
    strftime (timestr, sizeof(timestr), nl_langinfo (D_T_FMT), tm);
    
    if (level == LOG_CRITICAL) 
      fprintf (logfile, "** CRITICAL ** %s %s: ", timestr, domain);
    else
      fprintf (logfile, "%s %s: ", timestr, domain);
      
    vfprintf (logfile, fmt, ap);
    va_end (ap);
    fprintf (logfile, "\n");
    fflush (logfile);

    pthread_mutex_unlock (&logfile_mutex);
  }
  return (0);
}
