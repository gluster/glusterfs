/*
  (C) 2006 Gluster core team <http://www.gluster.org/>
  
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
#define _GF_FORMAT_WARN(domain, log_level, format, args...)  printf ("__DEBUG__" format, ##args);

typedef enum {
  LOG_CRITICAL,   /* major failures (not necessarily fatal) */
  LOG_NORMAL,     /* info about normal operation */
  LOG_DEBUG,      /* all other junk */
  LOG_MAX
} gf_loglevel;

int gf_log (const char *domain, gf_loglevel level, const char *fmt, ...);
int gf_log_init (const char *filename);

gf_loglevel gf_log_get_loglevel (void);
void gf_log_set_loglevel (gf_loglevel level);

#endif /* __LOGGING_H__ */
