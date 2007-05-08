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

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#include "glusterfs.h"
#include "xlator.h"

#include "meta.h"

static int
is_meta_path (const char *path)
{
  while (*path == '/')
    path++;
  if (!strncmp (path, ".meta", strlen (".meta")))
    return 1;
  return 0;
}

struct stat *
new_stbuf (void)
{
  static int next_inode = 0;
  struct stat *stbuf = calloc (1, sizeof (struct stat));

  stbuf->st_dev = 0;
  stbuf->st_ino = next_inode++;
  stbuf->st_mode = S_IRUSR | S_IRGRP | S_IROTH;
  stbuf->st_nlink = 1;
  stbuf->st_uid = 0;
  stbuf->st_gid = 0;
  stbuf->st_rdev = 0;
  stbuf->st_size = 0;
  stbuf->st_blksize = 0;
  stbuf->st_blocks = 0;
  stbuf->st_atime = time (NULL);
  stbuf->st_atim.tv_nsec = 0;
  stbuf->st_mtime = stbuf->st_atime;
  stbuf->st_mtim.tv_nsec = 0;
  stbuf->st_ctime = stbuf->st_ctime;
  stbuf->st_ctim.tv_nsec = 0;

  return stbuf;
}

/* find an entry among the siblings of an entry */
static meta_dirent_t *
find_entry (meta_dirent_t *node, const char *dir)
{
  meta_dirent_t *trav = node;
  while (trav) {
    if (!strcmp (trav->name, dir))
      return trav;
    trav = trav->next;
  }
  return NULL;
}

/*
 * Return the meta_dirent_t corresponding to the pathname.
 *
 * If pathname does not exist in the meta tree, try to return
 * its highest parent that does exist. The part of the
 * pathname that is left over is returned in the value-result
 * variable {remain}.
 * For example, for "/.meta/xlators/brick1/view/foo/bar/baz",
 * return the entry for "/.meta/xlators/brick1/view"
 * and set remain to "/bar/baz"
 */

meta_dirent_t *
lookup_meta_entry (meta_dirent_t *root, const char *path,
		   char **remain)
{
  char *_path = strdup (path);

  if (!is_meta_path (path))
    return NULL;

  meta_dirent_t *trav = root;
  char *dir = strtok (_path, "/");
  dir = strtok (NULL, "/");

  while (dir) {
    meta_dirent_t *ntrav;
    ntrav = find_entry (trav->children, dir);
    if (!ntrav) {
      /* we have reached bottom of the meta tree. 
         Unknown dragons lie further below */
      if (remain) {
	char *piece = dir;
	while (piece) {
	  char *tmp = *remain;
	  if (*remain)
	    asprintf (remain, "/%s/%s", *remain, piece);
	  else
	    asprintf (remain, "/%s", piece);
	  if (tmp) free (tmp);
	  piece = strtok (NULL, "/");
	}
      }
      return trav;
    }
    dir = strtok (NULL, "/");
    trav = ntrav;
  }

  free (_path);
  return trav;
}

meta_dirent_t *
insert_meta_entry (meta_dirent_t *root, const char *path,
		   int type, struct stat *stbuf, struct xlator_fops *fops)
{
  if (!is_meta_path (path))
    return NULL;
  char *slashpos = strrchr (path, '/');
  char *dir = strndup (path, slashpos - path);
  meta_dirent_t *parent = lookup_meta_entry (root, dir, NULL);
  if (!dir)
    return NULL;

  meta_dirent_t *new = calloc (1, sizeof (meta_dirent_t));
  new->name        = strdup (slashpos+1);
  new->type        = type;
  new->parent      = parent;
  new->next        = parent->children;
  parent->children = new;
  if (stbuf)
    new->stbuf     = stbuf;
  else 
    new->stbuf     = new_stbuf ();

  new->stbuf->st_mode |= type;
  new->fops        = fops;
  return new;
}

int main (void)
{
  meta_dirent_t *root = calloc (1, sizeof (meta_dirent_t));
  root->name = strdup (".meta");

  insert_meta_entry (root, "/.meta/version", S_IFREG, NULL, NULL);
  return 0;
}
