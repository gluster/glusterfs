/*
  Copyright (c) 2021 Red Hat, Inc. <https://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __GF_IO_URING_H__
#define __GF_IO_URING_H__

#include <glusterfs/gf-io.h>

#define GF_IO_URING_QUEUE_SIZE GF_IO_ID_REQ_COUNT
#define GF_IO_URING_QUEUE_MIN 4096
#define GF_IO_URING_MAX_RETRIES 100
#define GF_IO_URING_WORKER_THREADS 16

extern const gf_io_engine_t gf_io_engine_io_uring;

#endif /* __GF_IO_URING_H__ */
