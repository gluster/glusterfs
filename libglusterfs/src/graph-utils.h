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

#ifndef _GRAPH_H_
#define _GRAPH_H_

int glusterfs_graph_print_file (FILE *file, glusterfs_graph_t *graph);

char *glusterfs_graph_print_buf (glusterfs_graph_t *graph);

int glusterfs_xlator_link (xlator_t *pxl, xlator_t *cxl);
void glusterfs_graph_set_first (glusterfs_graph_t *graph, xlator_t *xl);
#endif
