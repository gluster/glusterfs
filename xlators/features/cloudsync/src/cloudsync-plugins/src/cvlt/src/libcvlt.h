/*
  Copyright (c) 2018 Commvault Systems, Inc. <http://www.commvault.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/
#ifndef _LIBCVLT_H
#define _LIBCVLT_H

#include <semaphore.h>
#include <glusterfs/xlator.h>
#include <glusterfs/glusterfs.h>
#include <glusterfs/call-stub.h>
#include <glusterfs/syncop.h>
#include <glusterfs/compat-errno.h>
#include "cloudsync-common.h"
#include "libcvlt-mem-types.h"
#include "archivestore.h"

enum _cvlt_op {
    CVLT_READ_OP = 1,
    CVLT_WRITE_OP = 2,
    CVLT_RESTORE_OP = 3,
    CVLT_ARCHIVE_OP = 4,
    CVLT_LOOKUP_OP = 5,
    CVLT_XATTR_OP = 6,
    CVLT_STAT_OP = 7,
    CVLT_FSTAT_op = 8,
    CVLT_UNDEF_OP = 127
};
typedef enum _cvlt_op cvlt_op_t;

struct _archive;
struct _cvlt_request {
    uint64_t offset;
    uint64_t bytes;
    struct iobuf *iobuf;
    struct iobref *iobref;
    call_frame_t *frame;
    cvlt_op_t op_type;
    int32_t op_ret;
    int32_t op_errno;
    xlator_t *this;
    sem_t sem;
    archstore_info_t store_info;
    archstore_fileinfo_t file_info;
    cs_size_xattr_t szxattr;
};
typedef struct _cvlt_request cvlt_request_t;

struct _archive {
    gf_lock_t lock;                /* lock for controlling access   */
    xlator_t *xl;                  /* xlator                        */
    void *handle;                  /* handle returned from dlopen   */
    int32_t nreqs;                 /* num requests active           */
    struct mem_pool *req_pool;     /* pool for requests             */
    struct iobuf_pool *iobuf_pool; /* iobuff pool                   */
    archstore_desc_t descinfo;     /* Archive store descriptor info */
    archstore_methods_t fops;      /* function pointers             */
    char *product_id;
    char *store_id;
    char *trailer;
};
typedef struct _archive archive_t;

void *
cvlt_init(xlator_t *);

int
cvlt_reconfigure(xlator_t *, dict_t *);

void
cvlt_fini(void *);

int
cvlt_download(call_frame_t *, void *);

int
cvlt_read(call_frame_t *, void *);

#endif
