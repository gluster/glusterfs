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

#ifndef _GETATTR_H_
#define _GETATTR_H_

#include <stdio.h>
#include <sys/time.h>
//#include <any_other_required_header>

struct getattr_node {
  struct getattr_node *next;
  struct stat *stbuf;
  char *pathname;
};

struct getattr_private {
  int32_t temp;
  char is_debug;
  pthread_mutex_t mutex; 
  struct timeval curr_tval;
  struct timeval timeout;
  struct getattr_node *head;
};

#endif /* _GETATTR_H_ */
