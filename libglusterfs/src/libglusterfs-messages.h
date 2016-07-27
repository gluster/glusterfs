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

#include "glfs-message-id.h"

/* NOTE: Rules for message additions
 * 1) Each instance of a message is _better_ left with a unique message ID, even
 *    if the message format is the same. Reasoning is that, if the message
 *    format needs to change in one instance, the other instances are not
 *    impacted or the new change does not change the ID of the instance being
 *    modified.
 * 2) Addition of a message,
 *       - Should increment the GLFS_NUM_MESSAGES
 *       - Append to the list of messages defined, towards the end
 *       - Retain macro naming as glfs_msg_X (for redability across developers)
 * NOTE: Rules for message format modifications
 * 3) Check acorss the code if the message ID macro in question is reused
 *    anywhere. If reused then then the modifications should ensure correctness
 *    everywhere, or needs a new message ID as (1) above was not adhered to. If
 *    not used anywhere, proceed with the required modification.
 * NOTE: Rules for message deletion
 * 4) Check (3) and if used anywhere else, then cannot be deleted. If not used
 *    anywhere, then can be deleted, but will leave a hole by design, as
 *    addition rules specify modification to the end of the list and not filling
 *    holes.
 */

#define GLFS_LG_BASE            GLFS_MSGID_COMP_LIBGLUSTERFS

#define GLFS_LG_NUM_MESSAGES    209

#define GLFS_LG_MSGID_END       (GLFS_LG_BASE + GLFS_LG_NUM_MESSAGES + 1)
/* Messaged with message IDs */
#define glfs_msg_start_lg GLFS_LG_BASE, "Invalid: Start of messages"
/*------------*/

#define LG_MSG_ASPRINTF_FAILED                            (GLFS_LG_BASE + 1)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_INVALID_ENTRY                              (GLFS_LG_BASE + 2)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_COUNT_LESS_THAN_ZERO                       (GLFS_LG_BASE + 3)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_COUNT_LESS_THAN_DATA_PAIRS                 (GLFS_LG_BASE + 4)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_VALUE_LENGTH_LESS_THAN_ZERO                (GLFS_LG_BASE + 5)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_PAIRS_LESS_THAN_COUNT                      (GLFS_LG_BASE + 6)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_KEY_OR_VALUE_NULL                          (GLFS_LG_BASE + 7)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_FAILED_TO_LOG_DICT                         (GLFS_LG_BASE + 8)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_NULL_VALUE_IN_DICT                         (GLFS_LG_BASE + 9)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_DIR_OP_FAILED                              (GLFS_LG_BASE + 10)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_STORE_HANDLE_CREATE_FAILED                 (GLFS_LG_BASE + 11)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_FILE_OP_FAILED                            (GLFS_LG_BASE + 12)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_FILE_STAT_FAILED                           (GLFS_LG_BASE + 13)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_LOCK_FAILED                               (GLFS_LG_BASE + 14)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_UNLOCK_FAILED                             (GLFS_LG_BASE + 15)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_DICT_SERIAL_FAILED                         (GLFS_LG_BASE + 16)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_DICT_UNSERIAL_FAILED                       (GLFS_LG_BASE + 17)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_NO_MEMORY                                  (GLFS_LG_BASE + 18)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_VOLUME_ERROR                               (GLFS_LG_BASE + 19)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_SUB_VOLUME_ERROR                           (GLFS_LG_BASE + 20)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_SYNTAX_ERROR                               (GLFS_LG_BASE + 21)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_BACKTICK_PARSE_FAILED                      (GLFS_LG_BASE + 22)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_BUFFER_ERROR                               (GLFS_LG_BASE + 23)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_STRDUP_ERROR                               (GLFS_LG_BASE + 24)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_HASH_FUNC_ERROR                            (GLFS_LG_BASE + 25)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_GET_BUCKET_FAILED                         (GLFS_LG_BASE + 26)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_INSERT_FAILED                             (GLFS_LG_BASE + 27)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_OUT_OF_RANGE                              (GLFS_LG_BASE + 28)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_VALIDATE_RETURNS                          (GLFS_LG_BASE + 29)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_VALIDATE_REC_FAILED                       (GLFS_LG_BASE + 30)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_RB_TABLE_CREATE_FAILED                    (GLFS_LG_BASE + 31)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */


#define LG_MSG_PATH_NOT_FOUND                            (GLFS_LG_BASE + 32)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_EXPAND_FD_TABLE_FAILED                    (GLFS_LG_BASE + 33)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_MAPPING_FAILED                            (GLFS_LG_BASE + 34)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_INIT_IOBUF_FAILED                         (GLFS_LG_BASE + 35)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_PAGE_SIZE_EXCEEDED                        (GLFS_LG_BASE + 36)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_ARENA_NOT_FOUND                           (GLFS_LG_BASE + 37)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_IOBUF_NOT_FOUND                           (GLFS_LG_BASE + 38)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_POOL_NOT_FOUND                            (GLFS_LG_BASE + 39)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_SET_ATTRIBUTE_FAILED                      (GLFS_LG_BASE + 40)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_READ_ATTRIBUTE_FAILED                     (GLFS_LG_BASE + 41)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_UNMOUNT_FAILED                            (GLFS_LG_BASE + 42)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_LATENCY_MEASUREMENT_STATE                 (GLFS_LG_BASE + 43)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_NO_PERM                                   (GLFS_LG_BASE + 44)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_NO_KEY                                    (GLFS_LG_BASE + 45)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_DICT_NULL                                 (GLFS_LG_BASE + 46)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_INIT_TIMER_FAILED                         (GLFS_LG_BASE + 47)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_FD_ANONYMOUS_FAILED                       (GLFS_LG_BASE + 48)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_FD_CREATE_FAILED                          (GLFS_LG_BASE + 49)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_BUFFER_FULL                               (GLFS_LG_BASE + 50)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_FWRITE_FAILED                             (GLFS_LG_BASE + 51)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_PRINT_FAILED                              (GLFS_LG_BASE + 52)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_MEM_POOL_DESTROY                          (GLFS_LG_BASE + 53)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_EXPAND_CLIENT_TABLE_FAILED                (GLFS_LG_BASE + 54)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_DISCONNECT_CLIENT                         (GLFS_LG_BASE + 55)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_PIPE_CREATE_FAILED                        (GLFS_LG_BASE + 56)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_SET_PIPE_FAILED                           (GLFS_LG_BASE + 57)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_REGISTER_PIPE_FAILED                      (GLFS_LG_BASE + 58)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_POLL_IGNORE_MULTIPLE_THREADS              (GLFS_LG_BASE + 59)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_INDEX_NOT_FOUND                           (GLFS_LG_BASE + 60)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_EPOLL_FD_CREATE_FAILED                    (GLFS_LG_BASE + 61)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_SLOT_NOT_FOUND                            (GLFS_LG_BASE + 62)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
 #define LG_MSG_STALE_FD_FOUND                            (GLFS_LG_BASE + 63)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_GENERATION_MISMATCH                       (GLFS_LG_BASE + 64)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_PTHREAD_KEY_CREATE_FAILED                 (GLFS_LG_BASE + 65)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_TRANSLATOR_INIT_FAILED                    (GLFS_LG_BASE + 66)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_UUID_BUF_INIT_FAILED                      (GLFS_LG_BASE + 67)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_LKOWNER_BUF_INIT_FAILED                   (GLFS_LG_BASE + 68)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_SYNCTASK_INIT_FAILED                      (GLFS_LG_BASE + 69)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_SYNCOPCTX_INIT_FAILED                     (GLFS_LG_BASE + 70)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_GLOBAL_INIT_FAILED                        (GLFS_LG_BASE + 71)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_PTHREAD_FAILED                            (GLFS_LG_BASE + 72)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_DIR_IS_SYMLINK                            (GLFS_LG_BASE + 73)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_RESOLVE_HOSTNAME_FAILED                   (GLFS_LG_BASE + 74)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_GETADDRINFO_FAILED                        (GLFS_LG_BASE + 75)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_GETNAMEINFO_FAILED                        (GLFS_LG_BASE + 76)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_PATH_ERROR                                (GLFS_LG_BASE + 77)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_INET_PTON_FAILED                          (GLFS_LG_BASE + 78)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_NEGATIVE_NUM_PASSED                       (GLFS_LG_BASE + 79)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_GETHOSTNAME_FAILED                        (GLFS_LG_BASE + 80)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_RESERVED_PORTS_ERROR                      (GLFS_LG_BASE + 81)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_INVALID_PORT                              (GLFS_LG_BASE + 82)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_INVALID_FAMILY                            (GLFS_LG_BASE + 83)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_CONVERSION_FAILED                         (GLFS_LG_BASE + 84)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_SKIP_HEADER_FAILED                        (GLFS_LG_BASE + 85)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_INVALID_LOG                               (GLFS_LG_BASE + 86)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_UTIMES_FAILED                             (GLFS_LG_BASE + 87)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_BACKTRACE_SAVE_FAILED                     (GLFS_LG_BASE + 88)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_INIT_FAILED                               (GLFS_LG_BASE + 89)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_VALIDATION_FAILED                         (GLFS_LG_BASE + 90)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_GRAPH_ERROR                               (GLFS_LG_BASE + 91)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_UNKNOWN_OPTIONS_FAILED                    (GLFS_LG_BASE + 92)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_CTX_NULL                                  (GLFS_LG_BASE + 93)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_TMPFILE_CREATE_FAILED                     (GLFS_LG_BASE + 94)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_DLOPEN_FAILED                             (GLFS_LG_BASE + 95)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_LOAD_FAILED                               (GLFS_LG_BASE + 96)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_DLSYM_ERROR                               (GLFS_LG_BASE + 97)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_TREE_NOT_FOUND                            (GLFS_LG_BASE + 98)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_PER_DENTRY                                (GLFS_LG_BASE + 99)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_DENTRY                                    (GLFS_LG_BASE + 100)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_GETIFADDRS_FAILED                         (GLFS_LG_BASE + 101)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_REGEX_OP_FAILED                           (GLFS_LG_BASE + 102)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_FRAME_ERROR                               (GLFS_LG_BASE + 103)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_SET_PARAM_FAILED                          (GLFS_LG_BASE + 104)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_GET_PARAM_FAILED                          (GLFS_LG_BASE + 105)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_PREPARE_FAILED                            (GLFS_LG_BASE + 106)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_EXEC_FAILED                               (GLFS_LG_BASE + 107)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_BINDING_FAILED                            (GLFS_LG_BASE + 108)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_DELETE_FAILED                             (GLFS_LG_BASE + 109)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_GET_ID_FAILED                             (GLFS_LG_BASE + 110)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_CREATE_FAILED                             (GLFS_LG_BASE + 111)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_PARSE_FAILED                              (GLFS_LG_BASE + 112)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */


#define LG_MSG_GETCONTEXT_FAILED                         (GLFS_LG_BASE + 113)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_UPDATE_FAILED                             (GLFS_LG_BASE + 114)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_QUERY_CALL_BACK_FAILED                    (GLFS_LG_BASE + 115)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_GET_RECORD_FAILED                         (GLFS_LG_BASE + 116)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_DB_ERROR                                  (GLFS_LG_BASE + 117)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_CONNECTION_ERROR                          (GLFS_LG_BASE + 118)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_NOT_MULTITHREAD_MODE                      (GLFS_LG_BASE + 119)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_SKIP_PATH                                 (GLFS_LG_BASE + 120)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_INVALID_FOP                               (GLFS_LG_BASE + 121)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_QUERY_FAILED                              (GLFS_LG_BASE + 122)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_CLEAR_COUNTER_FAILED                      (GLFS_LG_BASE + 123)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_LOCK_LIST_FAILED                          (GLFS_LG_BASE + 124)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_UNLOCK_LIST_FAILED                        (GLFS_LG_BASE + 125)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_ADD_TO_LIST_FAILED                        (GLFS_LG_BASE + 126)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_INIT_DB_FAILED                            (GLFS_LG_BASE + 127)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_DELETE_FROM_LIST_FAILED                   (GLFS_LG_BASE + 128)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_CLOSE_CONNECTION_FAILED                   (GLFS_LG_BASE + 129)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_INSERT_OR_UPDATE_FAILED                   (GLFS_LG_BASE + 130)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_FIND_OP_FAILED                            (GLFS_LG_BASE + 131)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_CONNECTION_INIT_FAILED                    (GLFS_LG_BASE + 132)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_COMPLETED_TASK                            (GLFS_LG_BASE + 133)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_WAKE_UP_ZOMBIE                            (GLFS_LG_BASE + 134)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_REWAITING_TASK                            (GLFS_LG_BASE + 135)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_SLEEP_ZOMBIE                              (GLFS_LG_BASE + 136)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_SWAPCONTEXT_FAILED                        (GLFS_LG_BASE + 137)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_UNSUPPORTED_PLUGIN                        (GLFS_LG_BASE + 138)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_INVALID_DB_TYPE                           (GLFS_LG_BASE + 139)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_UNDERSIZED_BUF                            (GLFS_LG_BASE + 140)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_DATA_CONVERSION_ERROR                     (GLFS_LG_BASE + 141)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_DICT_ERROR                                (GLFS_LG_BASE + 142)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_IOBUFS_NOT_FOUND                          (GLFS_LG_BASE + 143)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_ENTRIES_NULL                              (GLFS_LG_BASE + 144)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_FD_NOT_FOUND_IN_FDTABLE                   (GLFS_LG_BASE + 145)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_REALLOC_FOR_FD_PTR_FAILED                 (GLFS_LG_BASE + 146)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_DICT_SET_FAILED                           (GLFS_LG_BASE + 147)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_NULL_PTR                                  (GLFS_LG_BASE + 148)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_RBTHASH_INIT_BUCKET_FAILED                (GLFS_LG_BASE + 149)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_ASSERTION_FAILED                          (GLFS_LG_BASE + 150)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_HOSTNAME_NULL                             (GLFS_LG_BASE + 151)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_INVALID_IPV4_FORMAT                       (GLFS_LG_BASE + 152)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_CTX_CLEANUP_STARTED                       (GLFS_LG_BASE + 153)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_TIMER_REGISTER_ERROR                      (GLFS_LG_BASE + 154)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_PTR_HEADER_CORRUPTED                      (GLFS_LG_BASE + 155)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_INVALID_UPLINK                            (GLFS_LG_BASE + 156)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_CLIENT_NULL                               (GLFS_LG_BASE + 157)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_XLATOR_DOES_NOT_IMPLEMENT                 (GLFS_LG_BASE + 158)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_DENTRY_NOT_FOUND                          (GLFS_LG_BASE + 159)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_INODE_NOT_FOUND                           (GLFS_LG_BASE + 160)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_INODE_TABLE_NOT_FOUND                     (GLFS_LG_BASE + 161)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_DENTRY_CREATE_FAILED                      (GLFS_LG_BASE + 162)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_INODE_CONTEXT_FREED                       (GLFS_LG_BASE + 163)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_UNKNOWN_LOCK_TYPE                         (GLFS_LG_BASE + 164)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_UNLOCK_BEFORE_LOCK                        (GLFS_LG_BASE + 165)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_LOCK_OWNER_ERROR                          (GLFS_LG_BASE + 166)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_MEMPOOL_PTR_NULL                          (GLFS_LG_BASE + 167)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_QUOTA_XATTRS_MISSING                      (GLFS_LG_BASE + 168)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_INVALID_STRING                            (GLFS_LG_BASE + 169)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_BIND_REF                                  (GLFS_LG_BASE + 170)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_REF_COUNT                                 (GLFS_LG_BASE + 171)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_INVALID_ARG                               (GLFS_LG_BASE + 172)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_VOL_OPTION_ADD                            (GLFS_LG_BASE + 173)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_XLATOR_OPTION_INVALID                     (GLFS_LG_BASE + 174)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_GETTIMEOFDAY_FAILED                       (GLFS_LG_BASE + 175)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_GRAPH_INIT_FAILED                         (GLFS_LG_BASE + 176)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_EVENT_NOTIFY_FAILED                       (GLFS_LG_BASE + 177)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_ACTIVE_GRAPH_NULL                         (GLFS_LG_BASE + 178)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_VOLFILE_PARSE_ERROR                       (GLFS_LG_BASE + 179)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_FD_INODE_NULL                             (GLFS_LG_BASE + 180)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_INVALID_VOLFILE_ENTRY                     (GLFS_LG_BASE + 181)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_PER_DENTRY_FAILED                         (GLFS_LG_BASE + 182)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_PARENT_DENTRY_NOT_FOUND                   (GLFS_LG_BASE + 183)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_DENTRY_CYCLIC_LOOP                        (GLFS_LG_BASE + 184)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_INVALID_POLL_IN                           (GLFS_LG_BASE + 185)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_INVALID_POLL_OUT                         (GLFS_LG_BASE + 186)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_EPOLL_FD_ADD_FAILED                       (GLFS_LG_BASE + 187)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_EPOLL_FD_DEL_FAILED                        (GLFS_LG_BASE + 188)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_EPOLL_FD_MODIFY_FAILED                     (GLFS_LG_BASE + 189)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_STARTED_EPOLL_THREAD                       (GLFS_LG_BASE + 190)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_EXITED_EPOLL_THREAD                        (GLFS_LG_BASE + 191)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_START_EPOLL_THREAD_FAILED                  (GLFS_LG_BASE + 192)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_FALLBACK_TO_POLL                           (GLFS_LG_BASE + 193)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_QUOTA_CONF_ERROR                           (GLFS_LG_BASE + 194)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_RBTHASH_GET_ENTRY_FAILED                  (GLFS_LG_BASE + 195)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_RBTHASH_GET_BUCKET_FAILED                 (GLFS_LG_BASE + 196)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_RBTHASH_INSERT_FAILED                     (GLFS_LG_BASE + 197)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_RBTHASH_INIT_ENTRY_FAILED                 (GLFS_LG_BASE + 198)
/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_TMPFILE_DELETE_FAILED                     (GLFS_LG_BASE + 199)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_MEMPOOL_INVALID_FREE                      (GLFS_LG_BASE + 200)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_LOCK_FAILURE                              (GLFS_LG_BASE + 201)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_SET_LOG_LEVEL                             (GLFS_LG_BASE + 202)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_VERIFY_PLATFORM                           (GLFS_LG_BASE + 203)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_RUNNER_LOG                                (GLFS_LG_BASE + 204)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_LEASEID_BUF_INIT_FAILED                   (GLFS_LG_BASE + 205)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
#define LG_MSG_PTHREAD_ATTR_INIT_FAILED                  (GLFS_LG_BASE + 206)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_INVALID_INODE_LIST                         (GLFS_LG_BASE + 207)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_COMPACT_FAILED                            (GLFS_LG_BASE + 208)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */

#define LG_MSG_COMPACT_STATUS                            (GLFS_LG_BASE + 209)

/*!
 * @messageid
 * @diagnosis
 * @recommendedaction
 *
 */
/*------------*/

#define glfs_msg_end_lg GLFS_LG_MSGID_END, "Invalid: End of messages"

#endif /* !_LG_MESSAGES_H_ */



