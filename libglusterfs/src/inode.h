/*
  (C) 2007 Z RESEARCH Inc. <http://www.zresearch.com>
  
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

#ifndef _INODE_H
#define _INODE_H

#include <stdint.h>
#include <sys/types.h>

struct _inode_table;
typedef struct _inode_table inode_table_t;

struct _inode;
typedef struct _inode inode_t;

#include "list.h"
#include "xlator.h"

struct _inode_table {
  pthread_mutex_t lock;
  size_t hashsize;
  char *name;
  struct list_head *inode_hash;
  struct list_head *name_hash;
  struct list_head all;
};

struct _inode {
  inode_table_t *table;   /* the view this inode belongs to */
  uint64_t nlookup;       /* number of lookups done */
  uint32_t ref;           /* references to this structure */
  ino_t ino;              /* inode number in the stroage (persistant) */
  ino_t par;              /* parent's virtual inode number */
  char *name;             /* direntry name */
  fd_t fds;               /* list head of open fd's */
  struct stat buf;        /* attributes */
  dict_t *ctx;            /* per xlator private */
  struct list_head name_hash;
  struct list_head inode_hash;
  struct list_head all;
};

inode_table_t *
inode_table_new (size_t hashsize, char *name);

inode_t *
inode_search (inode_table_t *table,
	      ino_t par,
	      const char *name,
	      ino_t ino);

inode_t *
inode_ref (inode_t *inode);

inode_t *
inode_unref (inode_t *inode);

inode_t *
inode_lookup (inode_t *inode);

inode_t *
inode_forget (inode_t *inode,
	      uint64_t nlookup);

inode_t *
inode_rename (inode_table_t *table,
	      ino_t olddir,
	      const char *oldname,
	      ino_t newdir,
	      const char *newname);

void
inode_unlink (inode_table_t *table,
	      ino_t par,
	      const char *name);
	      
#endif /* _INODE_H */
