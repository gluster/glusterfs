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

#include "lock.h"
#include <stdio.h>
#include <errno.h>
#include "hashfn.h"
#include <string.h>
#include <stdlib.h>

int
lock_try_acquire (const char *path)
{
  int hashval = SuperFastHash ((char *)path, strlen (path));
  lock_inner_t *trav;

  hashval = hashval % LOCK_HASH;

  trav = global_lock[hashval];

  
  while (trav) {
    if (!strcmp (trav->path, path))
      break;
    trav = trav->next;
  }

  if (!trav) {
    trav = calloc (1, sizeof (lock_inner_t));
    trav->path = (path);

    trav->next = global_lock[hashval];
    global_lock[hashval] = trav;
    return 0;
  }

  errno = EEXIST;
  return -1;
}


int
lock_release (const char *path)
{
  int hashval = SuperFastHash ((char *)path, strlen (path));
  lock_inner_t *trav, *prev;

  hashval = hashval % LOCK_HASH;

  trav = global_lock[hashval];
  prev = NULL;

  
  while (trav) {
    if (!strcmp (trav->path, path))
      break;
    prev = trav;
    trav = trav->next;
  }

  if (trav) {
    free (trav->path);

    if (prev)
      prev->next = trav->next;
    else
      global_lock[hashval] = trav->next;

    free (trav);
    return 0;
  }

  errno = ENOENT;
  return -1;
}
