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

char *stripwhite (char *string);
char *get_token (char **line);
int str2long (char *str, int base, long *l);
int str2ulong (char *str, int base, unsigned long *ul);
int str2int (char *str, int base, int *i);
int str2uint (char *str, int base, unsigned int *ui);
int str2double (char *str, double *d);
int validate_ip_address (char *ip_address);

#endif
