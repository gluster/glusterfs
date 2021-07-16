/*
  Copyright (c) 2021 Red Hat, Inc. <https://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __COMPAT_IO_URING_H__
#define __COMPAT_IO_URING_H__

#include <linux/io_uring.h>

/* io_uring setup flags. */

#ifndef IORING_SETUP_IOPOLL
#define IORING_SETUP_IOPOLL     (1U << 0)
#endif

#ifndef IORING_SETUP_SQPOLL
#define IORING_SETUP_SQPOLL     (1U << 1)
#endif

#ifndef IORING_SETUP_SQ_AFF
#define IORING_SETUP_SQ_AFF     (1U << 2)
#endif

#ifndef IORING_SETUP_CQSIZE
#define IORING_SETUP_CQSIZE     (1U << 3)
#endif

#ifndef IORING_SETUP_CLAMP
#define IORING_SETUP_CLAMP      (1U << 4)
#endif

#ifndef IORING_SETUP_ATTACH_WQ
#define IORING_SETUP_ATTACH_WQ  (1U << 5)
#endif

#ifndef IORING_SETUP_R_DISABLED
#define IORING_SETUP_R_DISABLED (1U << 6)
#endif

/* io_uring features. */

#ifndef IORING_FEAT_SINGLE_MMAP
#define IORING_FEAT_SINGLE_MMAP     (1U << 0)
#endif

#ifndef IORING_FEAT_NODROP
#define IORING_FEAT_NODROP          (1U << 1)
#endif

#ifndef IORING_FEAT_SUBMIT_STABLE
#define IORING_FEAT_SUBMIT_STABLE   (1U << 2)
#endif

#ifndef IORING_FEAT_RW_CUR_POS
#define IORING_FEAT_RW_CUR_POS      (1U << 3)
#endif

#ifndef IORING_FEAT_CUR_PERSONALITY
#define IORING_FEAT_CUR_PERSONALITY (1U << 4)
#endif

#ifndef IORING_FEAT_FAST_POLL
#define IORING_FEAT_FAST_POLL       (1U << 5)
#endif

#ifndef IORING_FEAT_POLL_32BITS
#define IORING_FEAT_POLL_32BITS     (1U << 6)
#endif

#ifndef IORING_FEAT_SQPOLL_NONFIXED
#define IORING_FEAT_SQPOLL_NONFIXED (1U << 7)
#endif

#ifndef IORING_FEAT_EXT_ARG
#define IORING_FEAT_EXT_ARG         (1U << 8)
#endif

#ifndef IORING_FEAT_NATIVE_WORKERS
#define IORING_FEAT_NATIVE_WORKERS  (1U << 9)
#endif

/* The operations are defined as an enum, so we don't have any way to check
 * their existence during preprocessing, but we need to have them. So here
 * are duplicated all the ops. The '#ifndef' shouldn't be necessary, but
 * just in case some day they become #define's. */

#ifndef IORING_OP_NOP
#define IORING_OP_NOP              0U
#endif

#ifndef IORING_OP_READV
#define IORING_OP_READV            1U
#endif

#ifndef IORING_OP_WRITEV
#define IORING_OP_WRITEV           2U
#endif

#ifndef IORING_OP_FSYNC
#define IORING_OP_FSYNC            3U
#endif

#ifndef IORING_OP_READ_FIXED
#define IORING_OP_READ_FIXED       4U
#endif

#ifndef IORING_OP_WRITE_FIXED
#define IORING_OP_WRITE_FIXED      5U
#endif

#ifndef IORING_OP_POLL_ADD
#define IORING_OP_POLL_ADD         6U
#endif

#ifndef IORING_OP_POLL_REMOVE
#define IORING_OP_POLL_REMOVE      7U
#endif

#ifndef IORING_OP_SYNC_FILE_RANGE
#define IORING_OP_SYNC_FILE_RANGE  8U
#endif

#ifndef IORING_OP_SENDMSG
#define IORING_OP_SENDMSG          9U
#endif

#ifndef IORING_OP_RECVMSG
#define IORING_OP_RECVMSG          10U
#endif

#ifndef IORING_OP_TIMEOUT
#define IORING_OP_TIMEOUT          11U
#endif

#ifndef IORING_OP_TIMEOUT_REMOVE
#define IORING_OP_TIMEOUT_REMOVE   12U
#endif

#ifndef IORING_OP_ACCEPT
#define IORING_OP_ACCEPT           13U
#endif

#ifndef IORING_OP_ASYNC_CANCEL
#define IORING_OP_ASYNC_CANCEL     14U
#endif

#ifndef IORING_OP_LINK_TIMEOUT
#define IORING_OP_LINK_TIMEOUT     15U
#endif

#ifndef IORING_OP_CONNECT
#define IORING_OP_CONNECT          16U
#endif

#ifndef IORING_OP_FALLOCATE
#define IORING_OP_FALLOCATE        17U
#endif

#ifndef IORING_OP_OPENAT
#define IORING_OP_OPENAT           18U
#endif

#ifndef IORING_OP_CLOSE
#define IORING_OP_CLOSE            19U
#endif

#ifndef IORING_OP_FILES_UPDATE
#define IORING_OP_FILES_UPDATE     20U
#endif

#ifndef IORING_OP_STATX
#define IORING_OP_STATX            21U
#endif

#ifndef IORING_OP_READ
#define IORING_OP_READ             22U
#endif

#ifndef IORING_OP_WRITE
#define IORING_OP_WRITE            23U
#endif

#ifndef IORING_OP_FADVISE
#define IORING_OP_FADVISE          24U
#endif

#ifndef IORING_OP_MADVISE
#define IORING_OP_MADVISE          25U
#endif

#ifndef IORING_OP_SEND
#define IORING_OP_SEND             26U
#endif

#ifndef IORING_OP_RECV
#define IORING_OP_RECV             27U
#endif

#ifndef IORING_OP_OPENAT2
#define IORING_OP_OPENAT2          28U
#endif

#ifndef IORING_OP_EPOLL_CTL
#define IORING_OP_EPOLL_CTL        29U
#endif

#ifndef IORING_OP_SPLICE
#define IORING_OP_SPLICE           30U
#endif

#ifndef IORING_OP_PROVIDE_BUFFERS
#define IORING_OP_PROVIDE_BUFFERS  31U
#endif

#ifndef IORING_OP_REMOVE_BUFFERS
#define IORING_OP_REMOVE_BUFFERS   32U
#endif

#ifndef IORING_OP_TEE
#define IORING_OP_TEE              33U
#endif

#ifndef IORING_OP_SHUTDOWN
#define IORING_OP_SHUTDOWN         34U
#endif

#ifndef IORING_OP_RENAMEAT
#define IORING_OP_RENAMEAT         35U
#endif

#ifndef IORING_OP_UNLINKAT
#define IORING_OP_UNLINKAT         36U
#endif

#endif /* __COMPAT_IO_URING_H__ */
