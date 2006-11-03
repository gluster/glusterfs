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

#ifndef _COMMON_UTILS_H
#define _COMMON_UTILS_H

#include <stdint.h>

#include "transport.h"

char *stripwhite (char *string);
char *get_token (char **line);
int32_t str2long (char *str, int32_t base, int64_t *l);
int32_t str2ulong (char *str, int32_t base, uint64_t *ul);
int32_t str2int (char *str, int32_t base, int32_t *i);
int32_t str2uint (char *str, int32_t base, uint32_t *ui);
int32_t str2double (char *str, double *d);
int32_t validate_ip_address (char *ip_address);

int32_t full_read (int32_t fd, char *buf, int32_t size);
int32_t full_write (int32_t fd, const char *buf, int32_t size);

int32_t full_read_transport (struct transport *this, char *buf, int32_t size);
int32_t full_write_transport (struct transport *this, const char *buf, int32_t size);

#endif /* _COMMON_UTILS_H */
