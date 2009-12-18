/*
   Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
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


#ifndef __LOGGING_H__
#define __LOGGING_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdio.h>  
#include <stdarg.h>

#define GF_PRI_FSBLK       PRId64
#define GF_PRI_BLKSIZE     "ld"
#if GF_LINUX_HOST_OS

#  if __WORDSIZE == 64
#    define GF_PRI_SIZET   "lu"
#    define GF_PRI_NLINK   "lu"
#  else
#    define GF_PRI_SIZET   "u"
#    define GF_PRI_NLINK   "u"
#  endif /* __WORDSIZE */

#elif GF_DARWIN_HOST_OS

/* Noticed that size_t and ino_t are different on OSX, need to fix the warnings */
#  define GF_PRI_SIZET     "lu"
#  define GF_PRI_NLINK     "u"

#  undef GF_PRI_FSBLK
#  define GF_PRI_FSBLK     "u"
 
#  undef GF_PRI_BLKSIZE
#  define GF_PRI_BLKSIZE   "u"

#  if __DARWIN_64_BIT_INO_T == 0
#    error '64 bit ino_t is must for GlusterFS to work, Compile with "CFLAGS=-D__DARWIN_64_BIT_INO_T"'
#  endif /* __DARWIN_64_BIT_INO_T */

#else /* !LINUX && !DARWIN */

/* BSD and Solaris : Change as per testing there.. */
#  define GF_PRI_SIZET     "lu"
#  define GF_PRI_NLINK     "u"

#endif /* LINUX_OS */

#define GF_PRI_DEV         GF_PRI_FSBLK

typedef enum {
	GF_LOG_NONE,
	GF_LOG_CRITICAL,   /* fatal errors */
	GF_LOG_ERROR,      /* major failures (not necessarily fatal) */
	GF_LOG_WARNING,    /* info about normal operation */
	GF_LOG_INFO,       /* Normal information */
#define GF_LOG_NORMAL GF_LOG_INFO
	GF_LOG_DEBUG,      /* internal errors */
        GF_LOG_TRACE,      /* full trace of operation */
} gf_loglevel_t;

#define GF_LOG_MAX GF_LOG_DEBUG

extern gf_loglevel_t gf_log_loglevel;

#define gf_log(dom, levl, fmt...) do {					\
		if (levl <= gf_log_loglevel)				\
			_gf_log (dom, __FILE__, __FUNCTION__, __LINE__, \
				 levl, ##fmt);				\
		if (0) {						\
			printf (fmt);					\
		}							\
} while (0)

/* Log once in GF_UNIVERSAL_ANSWER times */
#define GF_LOG_OCCASIONALLY(var, args...) if (!(var++%GF_UNIVERSAL_ANSWER)) { \
                gf_log (args);                                                \
        }

			
void 
gf_log_logrotate (int signum);

int gf_log_init (const char *filename);
void gf_log_cleanup (void);

int
_gf_log (const char *domain, const char *file, const char *function,
	 int32_t line, gf_loglevel_t level, const char *fmt, ...);

int
gf_log_from_client (const char *msg, char *identifier);

void gf_log_lock (void);
void gf_log_unlock (void);

gf_loglevel_t 
gf_log_get_loglevel (void);
void 
gf_log_set_loglevel (gf_loglevel_t level);

#define GF_DEBUG(xl, format, args...) \
	gf_log ((xl)->name, GF_LOG_DEBUG, format, ##args)
#define GF_INFO(xl, format, args...) \
	gf_log ((xl)->name, GF_LOG_INFO, format, ##args)
#define GF_WARNING(xl, format, args...) \
	gf_log ((xl)->name, GF_LOG_WARNING, format, ##args)
#define GF_ERROR(xl, format, args...) \
	gf_log ((xl)->name, GF_LOG_ERROR, format, ##args)

#endif /* __LOGGING_H__ */
