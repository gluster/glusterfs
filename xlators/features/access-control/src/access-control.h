/*
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
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


#ifndef __ACCESS_CONTROL_H_
#define __ACCESS_CONTROL_H_

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#define ACTRL                   "access-control"
#define ACCTEST_READ            0x1
#define ACCTEST_WRITE           0x2
#define ACCTEST_EXEC            0x4
#define ACCTEST_DONTCARE        0x8

/* Note if the caller is only interested in ownership test i.e. one of the below
+ * in combination with GF_ACCTEST_DONTCARE, then only one type of user's owner
+ * ship can be tested with one call to gf_test_access, i.e. we can only
+ * check of either owner and group, if both need to be tested for a specific
+ * (uid, gid) pair then two calls will be needed.
+ */
#define ACCTEST_OWNER           0x1
#define ACCTEST_GROUP           0x2
#define ACCTEST_OTHER           0x4

/* Signifies any user, as long as we get access. */
#define ACCTEST_ANY             (ACCTEST_OWNER | ACCTEST_GROUP | ACCTEST_OTHER)

#define ac_test_owner(acc)      ((acc) & ACCTEST_OWNER)
#define ac_test_group(acc)      ((acc) & ACCTEST_GROUP)
#define ac_test_other(acc)      ((acc) & ACCTEST_OTHER)
#define ac_test_dontcare(acc)   ((acc) & ACCTEST_DONTCARE)
#define ac_test_read(acc)       ((acc) & ACCTEST_READ)
#define ac_test_write(acc)      ((acc) & ACCTEST_WRITE)
#define ac_test_exec(acc)       ((acc) & ACCTEST_EXEC)
#endif
