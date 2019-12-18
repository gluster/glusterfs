/*Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _IO_CACHE_MESSAGES_H_
#define _IO_CACHE_MESSAGES_H_

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

GLFS_MSGID(IO_CACHE, IO_CACHE_MSG_ENFORCEMENT_FAILED,
           IO_CACHE_MSG_INVALID_ARGUMENT,
           IO_CACHE_MSG_XLATOR_CHILD_MISCONFIGURED, IO_CACHE_MSG_NO_MEMORY,
           IO_CACHE_MSG_VOL_MISCONFIGURED, IO_CACHE_MSG_INODE_NULL,
           IO_CACHE_MSG_PAGE_WAIT_VALIDATE, IO_CACHE_MSG_STR_COVERSION_FAILED,
           IO_CACHE_MSG_WASTED_COPY, IO_CACHE_MSG_SET_FD_FAILED,
           IO_CACHE_MSG_TABLE_NULL, IO_CACHE_MSG_MEMORY_INIT_FAILED,
           IO_CACHE_MSG_NO_CACHE_SIZE_OPT, IO_CACHE_MSG_NOT_RECONFIG_CACHE_SIZE,
           IO_CACHE_MSG_CREATE_MEM_POOL_FAILED,
           IO_CACHE_MSG_ALLOC_MEM_POOL_FAILED, IO_CACHE_MSG_NULL_PAGE_WAIT,
           IO_CACHE_MSG_FRAME_NULL, IO_CACHE_MSG_PAGE_FAULT,
           IO_CACHE_MSG_SERVE_READ_REQUEST, IO_CACHE_MSG_LOCAL_NULL,
           IO_CACHE_MSG_DEFAULTING_TO_OLD);

#define IO_CACHE_MSG_NO_MEMORY_STR "out of memory"
#define IO_CACHE_MSG_ENFORCEMENT_FAILED_STR "inode context is NULL"
#define IO_CACHE_MSG_SET_FD_FAILED_STR "failed to set fd ctx"
#define IO_CACHE_MSG_TABLE_NULL_STR "table is NULL"
#define IO_CACHE_MSG_MEMORY_INIT_FAILED_STR "Memory accounting init failed"
#define IO_CACHE_MSG_NO_CACHE_SIZE_OPT_STR "could not get cache-size option"
#define IO_CACHE_MSG_INVALID_ARGUMENT_STR                                      \
    "file size is greater than the max size"
#define IO_CACHE_MSG_NOT_RECONFIG_CACHE_SIZE_STR "Not reconfiguring cache-size"
#define IO_CACHE_MSG_XLATOR_CHILD_MISCONFIGURED_STR                            \
    "FATAL: io-cache not configured with exactly one child"
#define IO_CACHE_MSG_VOL_MISCONFIGURED_STR "dangling volume. check volfile"
#define IO_CACHE_MSG_CREATE_MEM_POOL_FAILED_STR                                \
    "failed to create local_t's memory pool"
#define IO_CACHE_MSG_ALLOC_MEM_POOL_FAILED_STR "Unable to allocate mem_pool"
#define IO_CACHE_MSG_STR_COVERSION_FAILED_STR                                  \
    "asprintf failed while converting prt to str"
#define IO_CACHE_MSG_INODE_NULL_STR "ioc_inode is NULL"
#define IO_CACHE_MSG_PAGE_WAIT_VALIDATE_STR                                    \
    "cache validate called without any page waiting to be validated"
#define IO_CACHE_MSG_NULL_PAGE_WAIT_STR "asked to wait on a NULL page"
#define IO_CACHE_MSG_WASTED_COPY_STR "wasted copy"
#define IO_CACHE_MSG_FRAME_NULL_STR "frame>root>rsp_refs is null"
#define IO_CACHE_MSG_PAGE_FAULT_STR "page fault on a NULL frame"
#define IO_CACHE_MSG_SERVE_READ_REQUEST_STR                                    \
    "NULL page has been provided to serve read request"
#define IO_CACHE_MSG_LOCAL_NULL_STR "local is NULL"
#define IO_CACHE_MSG_DEFAULTING_TO_OLD_STR                                     \
    "minimum size of file that can be cached is greater than maximum size. "   \
    "Hence Defaulting to old value"
#endif /* _IO_CACHE_MESSAGES_H_ */
