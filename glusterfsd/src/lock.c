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

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "hashfn.h"
#include "logging.h"
#include "lock.h"

static lock_inner_t *global_lock[LOCK_HASH];

int
gf_listlocks (void)
{
  int index = 0;
  int count = 0;
  
  while (index < LOCK_HASH) {
    if (global_lock[index]) {
      gf_log ("glusterfsd", GF_LOG_DEBUG, "lock.c->gf_listlocks: index = %d is not null");
      count++;
    }
    index++;
  }

  if (!count) {
    gf_log ("glusterfsd", GF_LOG_DEBUG, "locks.c->gf_listlocks: all the elements of array global_lock are empty");
  }

  return count;
}

int
gf_lock_try_acquire (const char *path)
{
  GF_ERROR_IF_NULL (path);
  
  gf_log ("libglusterfs/lock", GF_LOG_DEBUG, "Trying to acquire lock for %s", path);

  unsigned int hashval = SuperFastHash ((char *)path, strlen (path));
  lock_inner_t *trav;

  hashval = hashval % LOCK_HASH;

  trav = global_lock[hashval];

  while (trav) {
    int len1 = strlen (trav->path);
    int len2 = strlen (path);
    int len = len1 < len2 ? len1 : len2;
    if (!strncmp (trav->path, path, len))
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
gf_lock_release (const char *path)
{
  GF_ERROR_IF_NULL (path);

  unsigned int hashval = SuperFastHash ((char *)path, strlen (path));
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
    gf_log ("libglusterfs/lock", GF_LOG_DEBUG, "Releasing lock for %s", path);

    free ((void *)trav->path);

    if (prev)
      prev->next = trav->next;
    else
      global_lock[hashval] = trav->next;

    free ((void *)trav);
    return 0;
  }

  gf_log ("libglusterfs/lock", GF_LOG_DEBUG, "Unlock failed for %s", path);

  errno = ENOENT;
  return -1;
}
