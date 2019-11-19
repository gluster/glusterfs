/*
 Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _LG_MESSAGES_H_
#define _LG_MESSAGES_H_

#include "glusterfs/glfs-message-id.h"

/* To add new message IDs, append new identifiers at the end of the list.
 *
 * Never remove a message ID. If it's not used anymore, you can rename it or
 * leave it as it is, but not delete it. This is to prevent reutilization of
 * IDs by other messages.
 *
 * The component name must match one of the entries defined in
 * glfs-message-id.h.
 */

GLFS_MSGID(
    LIBGLUSTERFS, LG_MSG_ASPRINTF_FAILED, LG_MSG_INVALID_ENTRY,
    LG_MSG_COUNT_LESS_THAN_ZERO, LG_MSG_COUNT_LESS_THAN_DATA_PAIRS,
    LG_MSG_VALUE_LENGTH_LESS_THAN_ZERO, LG_MSG_PAIRS_LESS_THAN_COUNT,
    LG_MSG_KEY_OR_VALUE_NULL, LG_MSG_FAILED_TO_LOG_DICT,
    LG_MSG_NULL_VALUE_IN_DICT, LG_MSG_DIR_OP_FAILED,
    LG_MSG_STORE_HANDLE_CREATE_FAILED, LG_MSG_FILE_OP_FAILED,
    LG_MSG_FILE_STAT_FAILED, LG_MSG_LOCK_FAILED, LG_MSG_UNLOCK_FAILED,
    LG_MSG_DICT_SERIAL_FAILED, LG_MSG_DICT_UNSERIAL_FAILED, LG_MSG_NO_MEMORY,
    LG_MSG_VOLUME_ERROR, LG_MSG_SUB_VOLUME_ERROR, LG_MSG_SYNTAX_ERROR,
    LG_MSG_BACKTICK_PARSE_FAILED, LG_MSG_BUFFER_ERROR, LG_MSG_STRDUP_ERROR,
    LG_MSG_HASH_FUNC_ERROR, LG_MSG_GET_BUCKET_FAILED, LG_MSG_INSERT_FAILED,
    LG_MSG_OUT_OF_RANGE, LG_MSG_VALIDATE_RETURNS, LG_MSG_VALIDATE_REC_FAILED,
    LG_MSG_RB_TABLE_CREATE_FAILED, LG_MSG_PATH_NOT_FOUND,
    LG_MSG_EXPAND_FD_TABLE_FAILED, LG_MSG_MAPPING_FAILED,
    LG_MSG_INIT_IOBUF_FAILED, LG_MSG_PAGE_SIZE_EXCEEDED, LG_MSG_ARENA_NOT_FOUND,
    LG_MSG_IOBUF_NOT_FOUND, LG_MSG_POOL_NOT_FOUND, LG_MSG_SET_ATTRIBUTE_FAILED,
    LG_MSG_READ_ATTRIBUTE_FAILED, LG_MSG_UNMOUNT_FAILED,
    LG_MSG_LATENCY_MEASUREMENT_STATE, LG_MSG_NO_PERM, LG_MSG_NO_KEY,
    LG_MSG_DICT_NULL, LG_MSG_INIT_TIMER_FAILED, LG_MSG_FD_ANONYMOUS_FAILED,
    LG_MSG_FD_CREATE_FAILED, LG_MSG_BUFFER_FULL, LG_MSG_FWRITE_FAILED,
    LG_MSG_PRINT_FAILED, LG_MSG_MEM_POOL_DESTROY,
    LG_MSG_EXPAND_CLIENT_TABLE_FAILED, LG_MSG_DISCONNECT_CLIENT,
    LG_MSG_PIPE_CREATE_FAILED, LG_MSG_SET_PIPE_FAILED,
    LG_MSG_REGISTER_PIPE_FAILED, LG_MSG_POLL_IGNORE_MULTIPLE_THREADS,
    LG_MSG_INDEX_NOT_FOUND, LG_MSG_EPOLL_FD_CREATE_FAILED,
    LG_MSG_SLOT_NOT_FOUND, LG_MSG_STALE_FD_FOUND, LG_MSG_GENERATION_MISMATCH,
    LG_MSG_PTHREAD_KEY_CREATE_FAILED, LG_MSG_TRANSLATOR_INIT_FAILED,
    LG_MSG_UUID_BUF_INIT_FAILED, LG_MSG_LKOWNER_BUF_INIT_FAILED,
    LG_MSG_SYNCTASK_INIT_FAILED, LG_MSG_SYNCOPCTX_INIT_FAILED,
    LG_MSG_GLOBAL_INIT_FAILED, LG_MSG_PTHREAD_FAILED, LG_MSG_DIR_IS_SYMLINK,
    LG_MSG_RESOLVE_HOSTNAME_FAILED, LG_MSG_GETADDRINFO_FAILED,
    LG_MSG_GETNAMEINFO_FAILED, LG_MSG_PATH_ERROR, LG_MSG_INET_PTON_FAILED,
    LG_MSG_NEGATIVE_NUM_PASSED, LG_MSG_GETHOSTNAME_FAILED,
    LG_MSG_RESERVED_PORTS_ERROR, LG_MSG_INVALID_PORT, LG_MSG_INVALID_FAMILY,
    LG_MSG_CONVERSION_FAILED, LG_MSG_SKIP_HEADER_FAILED, LG_MSG_INVALID_LOG,
    LG_MSG_UTIMES_FAILED, LG_MSG_BACKTRACE_SAVE_FAILED, LG_MSG_INIT_FAILED,
    LG_MSG_VALIDATION_FAILED, LG_MSG_GRAPH_ERROR, LG_MSG_UNKNOWN_OPTIONS_FAILED,
    LG_MSG_CTX_NULL, LG_MSG_TMPFILE_CREATE_FAILED, LG_MSG_DLOPEN_FAILED,
    LG_MSG_LOAD_FAILED, LG_MSG_DLSYM_ERROR, LG_MSG_TREE_NOT_FOUND,
    LG_MSG_PER_DENTRY, LG_MSG_DENTRY, LG_MSG_GETIFADDRS_FAILED,
    LG_MSG_REGEX_OP_FAILED, LG_MSG_FRAME_ERROR, LG_MSG_SET_PARAM_FAILED,
    LG_MSG_GET_PARAM_FAILED, LG_MSG_PREPARE_FAILED, LG_MSG_EXEC_FAILED,
    LG_MSG_BINDING_FAILED, LG_MSG_DELETE_FAILED, LG_MSG_GET_ID_FAILED,
    LG_MSG_CREATE_FAILED, LG_MSG_PARSE_FAILED, LG_MSG_GETCONTEXT_FAILED,
    LG_MSG_UPDATE_FAILED, LG_MSG_QUERY_CALL_BACK_FAILED,
    LG_MSG_GET_RECORD_FAILED, LG_MSG_DB_ERROR, LG_MSG_CONNECTION_ERROR,
    LG_MSG_NOT_MULTITHREAD_MODE, LG_MSG_SKIP_PATH, LG_MSG_INVALID_FOP,
    LG_MSG_QUERY_FAILED, LG_MSG_CLEAR_COUNTER_FAILED, LG_MSG_LOCK_LIST_FAILED,
    LG_MSG_UNLOCK_LIST_FAILED, LG_MSG_ADD_TO_LIST_FAILED, LG_MSG_INIT_DB_FAILED,
    LG_MSG_DELETE_FROM_LIST_FAILED, LG_MSG_CLOSE_CONNECTION_FAILED,
    LG_MSG_INSERT_OR_UPDATE_FAILED, LG_MSG_FIND_OP_FAILED,
    LG_MSG_CONNECTION_INIT_FAILED, LG_MSG_COMPLETED_TASK, LG_MSG_WAKE_UP_ZOMBIE,
    LG_MSG_REWAITING_TASK, LG_MSG_SLEEP_ZOMBIE, LG_MSG_SWAPCONTEXT_FAILED,
    LG_MSG_UNSUPPORTED_PLUGIN, LG_MSG_INVALID_DB_TYPE, LG_MSG_UNDERSIZED_BUF,
    LG_MSG_DATA_CONVERSION_ERROR, LG_MSG_DICT_ERROR, LG_MSG_IOBUFS_NOT_FOUND,
    LG_MSG_ENTRIES_NULL, LG_MSG_FD_NOT_FOUND_IN_FDTABLE,
    LG_MSG_REALLOC_FOR_FD_PTR_FAILED, LG_MSG_DICT_SET_FAILED, LG_MSG_NULL_PTR,
    LG_MSG_RBTHASH_INIT_BUCKET_FAILED, LG_MSG_ASSERTION_FAILED,
    LG_MSG_HOSTNAME_NULL, LG_MSG_INVALID_IPV4_FORMAT,
    LG_MSG_CTX_CLEANUP_STARTED, LG_MSG_TIMER_REGISTER_ERROR,
    LG_MSG_PTR_HEADER_CORRUPTED, LG_MSG_INVALID_UPLINK, LG_MSG_CLIENT_NULL,
    LG_MSG_XLATOR_DOES_NOT_IMPLEMENT, LG_MSG_DENTRY_NOT_FOUND,
    LG_MSG_INODE_NOT_FOUND, LG_MSG_INODE_TABLE_NOT_FOUND,
    LG_MSG_DENTRY_CREATE_FAILED, LG_MSG_INODE_CONTEXT_FREED,
    LG_MSG_UNKNOWN_LOCK_TYPE, LG_MSG_UNLOCK_BEFORE_LOCK,
    LG_MSG_LOCK_OWNER_ERROR, LG_MSG_MEMPOOL_PTR_NULL,
    LG_MSG_QUOTA_XATTRS_MISSING, LG_MSG_INVALID_STRING, LG_MSG_BIND_REF,
    LG_MSG_REF_COUNT, LG_MSG_INVALID_ARG, LG_MSG_VOL_OPTION_ADD,
    LG_MSG_XLATOR_OPTION_INVALID, LG_MSG_GETTIMEOFDAY_FAILED,
    LG_MSG_GRAPH_INIT_FAILED, LG_MSG_EVENT_NOTIFY_FAILED,
    LG_MSG_ACTIVE_GRAPH_NULL, LG_MSG_VOLFILE_PARSE_ERROR, LG_MSG_FD_INODE_NULL,
    LG_MSG_INVALID_VOLFILE_ENTRY, LG_MSG_PER_DENTRY_FAILED,
    LG_MSG_PARENT_DENTRY_NOT_FOUND, LG_MSG_DENTRY_CYCLIC_LOOP,
    LG_MSG_INVALID_POLL_IN, LG_MSG_INVALID_POLL_OUT, LG_MSG_EPOLL_FD_ADD_FAILED,
    LG_MSG_EPOLL_FD_DEL_FAILED, LG_MSG_EPOLL_FD_MODIFY_FAILED,
    LG_MSG_STARTED_EPOLL_THREAD, LG_MSG_EXITED_EPOLL_THREAD,
    LG_MSG_START_EPOLL_THREAD_FAILED, LG_MSG_FALLBACK_TO_POLL,
    LG_MSG_QUOTA_CONF_ERROR, LG_MSG_RBTHASH_GET_ENTRY_FAILED,
    LG_MSG_RBTHASH_GET_BUCKET_FAILED, LG_MSG_RBTHASH_INSERT_FAILED,
    LG_MSG_RBTHASH_INIT_ENTRY_FAILED, LG_MSG_TMPFILE_DELETE_FAILED,
    LG_MSG_MEMPOOL_INVALID_FREE, LG_MSG_LOCK_FAILURE, LG_MSG_SET_LOG_LEVEL,
    LG_MSG_VERIFY_PLATFORM, LG_MSG_RUNNER_LOG, LG_MSG_LEASEID_BUF_INIT_FAILED,
    LG_MSG_PTHREAD_ATTR_INIT_FAILED, LG_MSG_INVALID_INODE_LIST,
    LG_MSG_COMPACT_FAILED, LG_MSG_COMPACT_STATUS, LG_MSG_UTIMENSAT_FAILED,
    LG_MSG_PTHREAD_NAMING_FAILED, LG_MSG_SYSCALL_RETURNS_WRONG,
    LG_MSG_XXH64_TO_GFID_FAILED, LG_MSG_ASYNC_WARNING, LG_MSG_ASYNC_FAILURE,
    LG_MSG_GRAPH_CLEANUP_FAILED, LG_MSG_GRAPH_SETUP_FAILED,
    LG_MSG_GRAPH_DETACH_STARTED, LG_MSG_GRAPH_ATTACH_FAILED,
    LG_MSG_GRAPH_ATTACH_PID_FILE_UPDATED, LG_MSG_DUPLICATE_ENTRY,
    LG_MSG_THREAD_NAME_TOO_LONG, LG_MSG_SET_THREAD_FAILED,
    LG_MSG_THREAD_CREATE_FAILED, LG_MSG_FILE_DELETE_FAILED, LG_MSG_WRONG_VALUE,
    LG_MSG_PATH_OPEN_FAILED, LG_MSG_DISPATCH_HANDLER_FAILED,
    LG_MSG_READ_FILE_FAILED, LG_MSG_ENTRIES_NOT_PROVIDED,
    LG_MSG_ENTRIES_PROVIDED, LG_MSG_UNKNOWN_OPTION_TYPE,
    LG_MSG_OPTION_DEPRECATED, LG_MSG_INVALID_INIT, LG_MSG_OBJECT_NULL,
    LG_MSG_GRAPH_NOT_SET, LG_MSG_FILENAME_NOT_SPECIFIED, LG_MSG_STRUCT_MISS,
    LG_MSG_METHOD_MISS, LG_MSG_INPUT_DATA_NULL, LG_MSG_OPEN_LOGFILE_FAILED);

#define LG_MSG_EPOLL_FD_CREATE_FAILED_STR "epoll fd creation failed"
#define LG_MSG_INVALID_POLL_IN_STR "invalid poll_in value"
#define LG_MSG_INVALID_POLL_OUT_STR "invalid poll_out value"
#define LG_MSG_SLOT_NOT_FOUND_STR "could not find slot"
#define LG_MSG_EPOLL_FD_ADD_FAILED_STR "failed to add fd to epoll"
#define LG_MSG_EPOLL_FD_DEL_FAILED_STR "fail to delete fd to epoll"
#define LG_MSG_EPOLL_FD_MODIFY_FAILED_STR "failed to modify fd events"
#define LG_MSG_STALE_FD_FOUND_STR "stale fd found"
#define LG_MSG_GENERATION_MISMATCH_STR "generation mismatch"
#define LG_MSG_STARTED_EPOLL_THREAD_STR "Started thread with index"
#define LG_MSG_EXITED_EPOLL_THREAD_STR "Exited thread"
#define LG_MSG_DISPATCH_HANDLER_FAILED_STR "Failed to dispatch handler"
#define LG_MSG_START_EPOLL_THREAD_FAILED_STR "Failed to start thread"
#define LG_MSG_PIPE_CREATE_FAILED_STR "pipe creation failed"
#define LG_MSG_SET_PIPE_FAILED_STR "could not set pipe to non blocking mode"
#define LG_MSG_REGISTER_PIPE_FAILED_STR                                        \
    "could not register pipe fd with poll event loop"
#define LG_MSG_POLL_IGNORE_MULTIPLE_THREADS_STR                                \
    "Currently poll does not use multiple event processing threads, count "    \
    "ignored"
#define LG_MSG_INDEX_NOT_FOUND_STR "index not found"
#define LG_MSG_READ_FILE_FAILED_STR "read on file returned error"
#define LG_MSG_RB_TABLE_CREATE_FAILED_STR "Failed to create rb table bucket"
#define LG_MSG_HASH_FUNC_ERROR_STR "Hash function not given"
#define LG_MSG_ENTRIES_NOT_PROVIDED_STR                                        \
    "Both mem-pool and expected entries not provided"
#define LG_MSG_ENTRIES_PROVIDED_STR                                            \
    "Both mem-pool and expected entries are provided"
#define LG_MSG_RBTHASH_INIT_BUCKET_FAILED_STR "failed to init buckets"
#define LG_MSG_RBTHASH_GET_ENTRY_FAILED_STR "Failed to get entry from mem-pool"
#define LG_MSG_RBTHASH_GET_BUCKET_FAILED_STR "Failed to get bucket"
#define LG_MSG_RBTHASH_INSERT_FAILED_STR "Failed to insert entry"
#define LG_MSG_RBTHASH_INIT_ENTRY_FAILED_STR "Failed to init entry"
#define LG_MSG_FILE_STAT_FAILED_STR "failed to stat"
#define LG_MSG_INET_PTON_FAILED_STR "inet_pton() failed"
#define LG_MSG_INVALID_ENTRY_STR "Invalid arguments"
#define LG_MSG_NEGATIVE_NUM_PASSED_STR "negative number passed"
#define LG_MSG_PATH_ERROR_STR "Path manipulation failed"
#define LG_MSG_FILE_OP_FAILED_STR "could not open/read file, getting ports info"
#define LG_MSG_RESERVED_PORTS_ERROR_STR                                        \
    "Not able to get reserved ports, hence there is a possibility that "       \
    "glusterfs may consume reserved port"
#define LG_MSG_INVALID_PORT_STR "invalid port"
#define LG_MSG_GETNAMEINFO_FAILED_STR "Could not lookup hostname"
#define LG_MSG_GETIFADDRS_FAILED_STR "getifaddrs() failed"
#define LG_MSG_INVALID_FAMILY_STR "Invalid family"
#define LG_MSG_CONVERSION_FAILED_STR "String conversion failed"
#define LG_MSG_GETADDRINFO_FAILED_STR "error in getaddrinfo"
#define LG_MSG_DUPLICATE_ENTRY_STR "duplicate entry for volfile-server"
#define LG_MSG_PTHREAD_NAMING_FAILED_STR "Failed to compose thread name"
#define LG_MSG_THREAD_NAME_TOO_LONG_STR                                        \
    "Thread name is too long. It has been truncated"
#define LG_MSG_SET_THREAD_FAILED_STR "Could not set thread name"
#define LG_MSG_THREAD_CREATE_FAILED_STR "Thread creation failed"
#define LG_MSG_PTHREAD_ATTR_INIT_FAILED_STR                                    \
    "Thread attribute initialization failed"
#define LG_MSG_SKIP_HEADER_FAILED_STR "Failed to skip header section"
#define LG_MSG_INVALID_LOG_STR "Invalid log-format"
#define LG_MSG_UTIMENSAT_FAILED_STR "utimenstat failed"
#define LG_MSG_UTIMES_FAILED_STR "utimes failed"
#define LG_MSG_FILE_DELETE_FAILED_STR "Unable to delete file"
#define LG_MSG_BACKTRACE_SAVE_FAILED_STR "Failed to save the backtrace"
#define LG_MSG_WRONG_VALUE_STR "wrong value"
#define LG_MSG_DIR_OP_FAILED_STR "Failed to create directory"
#define LG_MSG_DIR_IS_SYMLINK_STR "dir is symlink"
#define LG_MSG_RESOLVE_HOSTNAME_FAILED_STR "couldnot resolve hostname"
#define LG_MSG_PATH_OPEN_FAILED_STR "Unable to open path"
#define LG_MSG_NO_MEMORY_STR "Error allocating memory"
#define LG_MSG_EVENT_NOTIFY_FAILED_STR "notification failed"
#define LG_MSG_PER_DENTRY_FAILED_STR "per dentry fn returned"
#define LG_MSG_PARENT_DENTRY_NOT_FOUND_STR "parent not found"
#define LG_MSG_DENTRY_CYCLIC_LOOP_STR                                          \
    "detected cyclic loop formation during inode linkage"
#define LG_MSG_CTX_NULL_STR "_ctx not found"
#define LG_MSG_DENTRY_NOT_FOUND_STR "dentry not found"
#define LG_MSG_OUT_OF_RANGE_STR "out of range"
#define LG_MSG_UNKNOWN_OPTION_TYPE_STR "unknown option type"
#define LG_MSG_VALIDATE_RETURNS_STR "validate of returned"
#define LG_MSG_OPTION_DEPRECATED_STR                                           \
    "option is deprecated, continuing with correction"
#define LG_MSG_VALIDATE_REC_FAILED_STR "validate_rec failed"
#define LG_MSG_MAPPING_FAILED_STR "mapping failed"
#define LG_MSG_INIT_IOBUF_FAILED_STR "init failed"
#define LG_MSG_ARENA_NOT_FOUND_STR "arena not found"
#define LG_MSG_PAGE_SIZE_EXCEEDED_STR                                          \
    "page_size of iobufs in arena being added is greater than max available"
#define LG_MSG_POOL_NOT_FOUND_STR "pool not found"
#define LG_MSG_IOBUF_NOT_FOUND_STR "iobuf not found"
#define LG_MSG_DLOPEN_FAILED_STR "DL open failed"
#define LG_MSG_DLSYM_ERROR_STR "dlsym missing"
#define LG_MSG_LOAD_FAILED_STR "Failed to load xlator options table"
#define LG_MSG_INPUT_DATA_NULL_STR                                             \
    "input data is null. cannot update the lru limit of the inode table. "     \
    "continuing with older value."
#define LG_MSG_INIT_FAILED_STR "No init() found"
#define LG_MSG_VOLUME_ERROR_STR                                                \
    "Initialization of volume failed. review your volfile again."
#define LG_MSG_TREE_NOT_FOUND_STR "Translator tree not found"
#define LG_MSG_SET_LOG_LEVEL_STR "setting log level"
#define LG_MSG_INVALID_INIT_STR                                                \
    "Invalid log-level. possible values are DEBUG|WARNING|ERROR|NONE|TRACE"
#define LG_MSG_OBJECT_NULL_STR "object is null, returning false."
#define LG_MSG_GRAPH_NOT_SET_STR "Graph is not set for xlator"
#define LG_MSG_OPEN_LOGFILE_FAILED_STR "failed to open logfile"
#define LG_MSG_STRDUP_ERROR_STR "failed to create metrics dir"
#define LG_MSG_FILENAME_NOT_SPECIFIED_STR "no filename specified"
#define LG_MSG_UNDERSIZED_BUF_STR "data value is smaller than expected"
#define LG_MSG_DICT_SET_FAILED_STR "unable to set dict"
#define LG_MSG_COUNT_LESS_THAN_ZERO_STR "count < 0!"
#define LG_MSG_PAIRS_LESS_THAN_COUNT_STR "less than count data pairs found"
#define LG_MSG_NULL_PTR_STR "pair->key is null!"
#define LG_MSG_VALUE_LENGTH_LESS_THAN_ZERO_STR "value->len < 0"
#define LG_MSG_INVALID_ARG_STR "buf is null"
#define LG_MSG_KEY_OR_VALUE_NULL_STR "key or value is null"
#define LG_MSG_NULL_VALUE_IN_DICT_STR "null value found in dict"
#define LG_MSG_FAILED_TO_LOG_DICT_STR "Failed to log dictionary"
#define LG_MSG_DICT_ERROR_STR "dict error"
#define LG_MSG_STRUCT_MISS_STR "struct missing"
#define LG_MSG_METHOD_MISS_STR "method missing(init)"

#endif /* !_LG_MESSAGES_H_ */
