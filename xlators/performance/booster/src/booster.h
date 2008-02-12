/*
   Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
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

#ifndef __BOOSTER_H__
#define __BOOSTER_H__

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "transport.h"
#include "xlator.h"

struct file {
  void *transport;
  char handle[20];
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  uint64_t count;
};

struct glusterfs_booster_protocol_header {
  int8_t op;
  uint64_t offset;
  uint64_t size;
  char handle[20];
  int32_t op_ret;
  int32_t op_errno;
  char buffer[0];
} __attribute__ ((packed));

#endif /* __BOOSTER_H__ */
