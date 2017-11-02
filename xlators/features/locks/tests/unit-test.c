/*
   Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/
#include "glusterfs.h"
#include "compat.h"
#include "xlator.h"
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
