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


#ifndef __LOGGING_H__
#define __LOGGING_H__


/* Replace gf_log with _GF_FORMAT_WARN during compile time and let gcc spit the format specificier warnings. Make sure you replace them back with gf_log call. */
#define _GF_FORMAT_WARN(domain, loglevel, format, args...)  printf ("__DEBUG__" format, ##args);

typedef enum {
  GF_LOG_NONE,
  GF_LOG_CRITICAL,   /* fatal errors */
  GF_LOG_ERROR,      /* major failures (not necessarily fatal) */
  GF_LOG_WARNING,     /* info about normal operation */
  GF_LOG_DEBUG,      /* all other junk */
} gf_loglevel_t;

#define GF_LOG_MAX GF_LOG_DEBUG

#define gf_log(dom, levl, fmt...) do {                          \
  _gf_log (dom, __FILE__, __FUNCTION__, __LINE__, levl, ##fmt); \
} while (0)

int32_t 
_gf_log (const char *domain,
	 const char *file,
	 const char *function,
	 int32_t line,
	 gf_loglevel_t level,
	 const char *fmt, ...);
int32_t gf_log_init (const char *filename);

gf_loglevel_t gf_log_get_loglevel (void);
void gf_log_set_loglevel (gf_loglevel_t level);

/* Check if the condition is true and log and return -1 if it is */
#define GF_ERROR_IF(cond) \
do { \
  if ((cond)) { \
    gf_log ("ERROR", GF_LOG_ERROR, "%s: %s: (%s) is true", __FILE__, __FUNCTION__, #cond); \
    errno = EINVAL; \
    return -1; \
  } \
} while (0);

/* Check if the condition is true and log if it is */
#define GF_ERROR_NO_RETURN_IF(cond) \
do { \
  if ((cond)) { \
    gf_log ("ERROR", GF_LOG_ERROR, "%s: %s: (%s) is true", __FILE__, __FUNCTION__, #cond); \
  } \
} while (0);

#define GF_ERROR_IF_NULL(p) GF_ERROR_IF((p) == NULL)
#define GF_ERROR_NO_RETURN_IF_NULL(p) GF_ERROR_NO_RETURN_IF((p) == NULL)
#define GF_ERROR_NO_RETURN_IF_FALSE(p) GF_ERROR_NO_RETURN_IF((p) == 0)
#define GF_BUG_ON(p) GF_ERROR_NO_RETURN_IF(p)
#endif /* __LOGGING_H__ */
