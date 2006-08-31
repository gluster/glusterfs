
#ifndef __LOGGING_H__
#define __LOGGING_H__

typedef enum {
  LOG_CRITICAL,   /* major failures (not necessarily fatal) */
  LOG_NORMAL,     /* info about normal operation */
  LOG_DEBUG,      /* all other junk */
} gf_loglevel;

int gf_log (const char *domain, gf_loglevel level, const char *fmt, ...);
int gf_log_init (const char *filename);

gf_loglevel gf_log_get_loglevel (void);
void gf_log_set_loglevel (gf_loglevel level);

#endif /* __LOGGING_H__ */
