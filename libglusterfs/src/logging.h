
#ifndef __LOGGING_H__
#define __LOGGING_H__


/* Replace gf_log with _GF_FORMAT_WARN during compile time and let gcc spit the format specificier warnings. Make sure you replace them back with gf_log call. */
#define _GF_FORMAT_WARN(domain, loglevel, format, args...)  printf ("__DEBUG__" format, ##args);

typedef enum {
  GF_LOG_CRITICAL,   /* fatal errors */
  GF_LOG_ERROR,      /* major failures (not necessarily fatal) */
  GF_LOG_NORMAL,     /* info about normal operation */
  GF_LOG_DEBUG,      /* all other junk */
} gf_loglevel_t;

#define GF_LOG_MAX GF_LOG_DEBUG

int gf_log (const char *domain, gf_loglevel_t level, const char *fmt, ...);
int gf_log_init (const char *filename);

gf_loglevel_t gf_log_get_loglevel (void);
void gf_log_set_loglevel (gf_loglevel_t level);

/* Check that the pointer is not NULL and log and return the value if it is */
#define GF_ERROR_IF_NULL(p)  \
do { \
  if ((p) == NULL) { \
    gf_log ("ERROR", LOG_ERROR, __FILE__ ": " __FUNCTION__ ": %s is NULL", #p); \
    errno = EINVAL; \
    return -1; \
  } \
} while (0);


#endif /* __LOGGING_H__ */
