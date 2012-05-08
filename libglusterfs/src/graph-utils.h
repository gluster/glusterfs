/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _GRAPH_H_
#define _GRAPH_H_

int glusterfs_graph_print_file (FILE *file, glusterfs_graph_t *graph);

char *glusterfs_graph_print_buf (glusterfs_graph_t *graph);

int glusterfs_xlator_link (xlator_t *pxl, xlator_t *cxl);
void glusterfs_graph_set_first (glusterfs_graph_t *graph, xlator_t *xl);
#endif
