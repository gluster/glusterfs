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

int8_t *stripwhite (int8_t *string);
int8_t *get_token (int8_t **line);
int32_t str2long (int8_t *str, int32_t base, int64_t *l);
int32_t str2ulong (int8_t *str, int32_t base, uint64_t *ul);
int32_t str2int (int8_t *str, int32_t base, int32_t *i);
int32_t str2uint (int8_t *str, int32_t base, uint32_t *ui);
int32_t str2double (int8_t *str, double *d);
int32_t validate_ip_address (int8_t *ip_address);

int32_t full_read (int32_t fd, int8_t *buf, int32_t size);
int32_t full_write (int32_t fd, const int8_t *buf, int32_t size);

#endif /* _COMMON_UTILS_H */
