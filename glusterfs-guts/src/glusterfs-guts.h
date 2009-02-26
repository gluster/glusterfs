/*
   Copyright (c) 2008-2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __GLUSTERFS_GUTS_H
#define __GLUSTERFS_GUTS_H

#include "xlator.h"
#include "transport.h"
#include "glusterfs.h"
#include "glusterfs-fuse.h"
#include "timer.h"

#ifdef DEFAULT_LOG_FILE
#undef DEFAULT_LOG_FILE
#endif 

#define DEFAULT_LOG_FILE   DATADIR"/log/glusterfs/glusterfs-guts.log"


typedef struct {
  int32_t threads;   /* number of threads to start in replay mode */
  char *logfile;     /* logfile path */
  int32_t loglevel;  /* logging level */
  char *directory;   /* path to directory containing tio files, when threads > 1 */
  char *file;        /* path to tio file, when threads == 1 during replay. in trace mode, path to tio output */
  char *specfile;    /* path to specfile to load translator tree */
  xlator_t *graph;   /* translator tree after the specfile is loaded */
  int32_t trace;     /* if trace == 1, glusterfs-guts runs in trace mode, otherwise in replay mode */
  char *mountpoint;  /* valid only when trace == 1, mounpoint to mount glusterfs */
} guts_ctx_t;


typedef struct {
  struct list_head threads;
  pthread_t pthread;
  xlator_t *tree;
  char *file;
  guts_ctx_t *ctx;
} guts_thread_ctx_t;

typedef struct {  
  struct list_head threads;
} guts_threads_t;

int32_t guts_replay (guts_thread_ctx_t *);
int32_t guts_trace (guts_ctx_t *);
#endif
