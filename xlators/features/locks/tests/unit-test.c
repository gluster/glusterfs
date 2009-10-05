/*
  Copyright (c) 2006-2009 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "compat.h"
#include "xlator.h"
#include "inode.h"
#include "logging.h"
#include "common-utils.h"
#include "list.h"

#include "locks.h"
#include "common.h"

#define expect(cond) if (!(cond)) { goto out; }

extern int lock_name (pl_inode_t *, const char *, entrylk_type);
extern int unlock_name (pl_inode_t *, const char *, entrylk_type);

int main (int argc, char **argv)
{
	int ret = 1;
	int r = -1;

	pl_inode_t *pinode = CALLOC (sizeof (pl_inode_t), 1);
	pthread_mutex_init (&pinode->dir_lock_mutex, NULL);
	INIT_LIST_HEAD (&pinode->gf_dir_locks);

	r = lock_name (pinode, NULL, ENTRYLK_WRLCK); expect (r == 0);
	{
		r = lock_name (pinode, "foo", ENTRYLK_WRLCK); expect (r == -EAGAIN);
	}
	r = unlock_name (pinode, NULL, ENTRYLK_WRLCK); expect (r == 0);

	r = lock_name (pinode, "foo", ENTRYLK_RDLCK); expect (r == 0);
	{
		r = lock_name (pinode, "foo", ENTRYLK_RDLCK); expect (r == 0);
		{
			r = lock_name (pinode, "foo", ENTRYLK_WRLCK); expect (r == -EAGAIN);
		}
		r = unlock_name (pinode, "foo", ENTRYLK_RDLCK); expect (r == 0);
	}
	r = unlock_name (pinode, "foo", ENTRYLK_RDLCK); expect (r == 0);
	
	r = lock_name (pinode, "foo", ENTRYLK_WRLCK); expect (r == 0);
	r = unlock_name (pinode, "foo", ENTRYLK_WRLCK); expect (r == 0);

	r = lock_name (pinode, "baz", ENTRYLK_WRLCK); expect (r == 0);
	r = lock_name (pinode, "baz", ENTRYLK_RDLCK); expect (r == -EAGAIN);

	ret = 0;
out:
	return ret;
}
