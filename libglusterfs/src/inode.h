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

#include "xlator.h"
#include <stdint.h>
#include <sys/types.h>

struct _inode_table;
typedef struct _inode_table inode_table_t;

struct _inode_table {
};

inode_table_t *
inode_table_new (size_t hashsize);

inode_t *
inode_create (inode_table_t *table,
	      ino_t parent,
	      const char *name,
	      ino_t vinode,
	      struct stat *buf);

void
inode_unref (inode_t *inode);

inode_t *
inode_lookup (inode_table_t *table,
	      ino_t parent,
	      const char *name,
	      ino_t vinode,
	      ino_t inode);

inode_t *
inode_rename (inode_table_t *table,
	      ino_t olddir,
	      const char *oldname,
	      ino_t newdir,
	      const char *newdir);

void
inode_unlink (inode_table_t *table,
	      ino_t parent,
	      const char *name);
	      
#endif /* _INODE_H */
