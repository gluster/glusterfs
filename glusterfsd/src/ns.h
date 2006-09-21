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

#ifndef _NS_H
#define _NS_H

#include "hashfn.h"

#define NS_HASH 1024

typedef struct _ns_inner {
  struct _ns_inner *next;
  const char *path;
  const char *ns;
} ns_inner_t;

char * ns_lookup (const char *path);

int ns_update (const char *path, const char *ns);

#endif /* _NS_H */
