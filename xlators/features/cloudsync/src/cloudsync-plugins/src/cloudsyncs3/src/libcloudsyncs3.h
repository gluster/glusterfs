/*
  Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/
#ifndef _LIBAWS_H
#define _LIBAWS_H

#include <glusterfs/glusterfs.h>
#include <glusterfs/call-stub.h>
#include <glusterfs/xlator.h>
#include <glusterfs/syncop.h>
#include <curl/curl.h>
#include "cloudsync-common.h"
#include "libcloudsyncs3-mem-types.h"

char *
aws_b64_encode(const unsigned char *input, int length);

size_t
aws_write_callback(void *dlbuf, size_t size, size_t nitems, void *mainframe);

int
aws_download_s3(call_frame_t *frame, void *config);

int
aws_dlwritev_cbk(call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                 int op_errno, struct iatt *prebuf, struct iatt *postbuf,
                 dict_t *xdata);

void *
aws_init(xlator_t *this);

int
aws_reconfigure(xlator_t *this, dict_t *options);

char *
aws_form_request(char *resource, char **date, char *reqtype, char *bucketid,
                 char *filepath);
char *
aws_sign_request(char *const str, char *awssekey);

void
aws_fini(void *config);

#endif
