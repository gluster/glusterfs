/*
   Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU Affero General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/

#ifndef _GLOBALS_H
#define _GLOBALS_H

#define GF_DEFAULT_BASE_PORT 24007

/* This corresponds to the max 16 number of group IDs that are sent through an
 * RPC request. Since NFS is the only one going to set this, we can be safe
 * in keeping this size hardcoded.
 */
#define GF_REQUEST_MAXGROUPS    16

#include "glusterfs.h"

/* CTX */
#define CTX (glusterfs_ctx_get())

glusterfs_ctx_t *glusterfs_ctx_get ();

#include "xlator.h"

/* THIS */
#define THIS (*__glusterfs_this_location())

#define GF_UUID_BUF_SIZE 50

xlator_t **__glusterfs_this_location ();
xlator_t *glusterfs_this_get ();
int glusterfs_this_set (xlator_t *);

/* central log */

void glusterfs_central_log_flag_set ();
long glusterfs_central_log_flag_get ();
void glusterfs_central_log_flag_unset ();

/* task */
void *synctask_get ();
int synctask_set (void *);

/* uuid_buf */
char *glusterfs_uuid_buf_get();

/* init */
int glusterfs_globals_init (void);

#endif /* !_GLOBALS_H */
