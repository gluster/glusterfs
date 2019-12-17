/*Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _IO_THREADS_MESSAGES_H_
#define _IO_THREADS_MESSAGES_H_

#include <glusterfs/glfs-message-id.h>

/* To add new message IDs, append new identifiers at the end of the list.
 *
 * Never remove a message ID. If it's not used anymore, you can rename it or
 * leave it as it is, but not delete it. This is to prevent reutilization of
 * IDs by other messages.
 *
 * The component name must match one of the entries defined in
 * glfs-message-id.h.
 */

GLFS_MSGID(IO_THREADS, IO_THREADS_MSG_INIT_FAILED,
           IO_THREADS_MSG_XLATOR_CHILD_MISCONFIGURED, IO_THREADS_MSG_NO_MEMORY,
           IO_THREADS_MSG_VOL_MISCONFIGURED, IO_THREADS_MSG_SIZE_NOT_SET,
           IO_THREADS_MSG_OUT_OF_MEMORY, IO_THREADS_MSG_PTHREAD_INIT_FAILED,
           IO_THREADS_MSG_WORKER_THREAD_INIT_FAILED);

#define IO_THREADS_MSG_INIT_FAILED_STR "Thread attribute initialization failed"
#define IO_THREADS_MSG_SIZE_NOT_SET_STR "Using default thread stack size"
#define IO_THREADS_MSG_NO_MEMORY_STR "Memory accounting init failed"
#define IO_THREADS_MSG_XLATOR_CHILD_MISCONFIGURED_STR                          \
    "FATAL: iot not configured with exactly one child"
#define IO_THREADS_MSG_VOL_MISCONFIGURED_STR "dangling volume. check volfile"
#define IO_THREADS_MSG_OUT_OF_MEMORY_STR "out of memory"
#define IO_THREADS_MSG_PTHREAD_INIT_FAILED_STR "init failed"
#define IO_THREADS_MSG_WORKER_THREAD_INIT_FAILED_STR                           \
    "cannot initialize worker threads, exiting init"
#endif /* _IO_THREADS_MESSAGES_H_ */
