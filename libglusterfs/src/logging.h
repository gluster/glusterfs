
#ifndef __LOGGING_H__
#define __LOGGING_H__

typedef enum {
  LOG_NORMAL,     /* info about normal operation */
  LOG_CRITICAL,   /* major failures (not necessarily fatal) */
  LOG_DEBUG,      /* all other junk */
} gluster_loglevel;

void gluster_log (const char *domain, gluster_loglevel level, const char *fmt, ...);
void gluster_log_init (const char *filename);

gluster_loglevel gluster_log_get_loglevel (void);
void gluster_log_set_loglevel (gluster_loglevel level);

#endif /* __LOGGING_H__ */
