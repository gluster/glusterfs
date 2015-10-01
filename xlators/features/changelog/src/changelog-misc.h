/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _CHANGELOG_MISC_H
#define _CHANGELOG_MISC_H

#include "glusterfs.h"
#include "common-utils.h"

#define CHANGELOG_MAX_TYPE  3
#define CHANGELOG_FILE_NAME "CHANGELOG"
#define HTIME_FILE_NAME "HTIME"
#define CSNAP_FILE_NAME "CHANGELOG.SNAP"
#define HTIME_KEY "trusted.glusterfs.htime"
#define HTIME_CURRENT "trusted.glusterfs.current_htime"
#define HTIME_INITIAL_VALUE "0:0"

#define CHANGELOG_VERSION_MAJOR  1
#define CHANGELOG_VERSION_MINOR  2

#define CHANGELOG_UNIX_SOCK  DEFAULT_VAR_RUN_DIRECTORY"/changelog-%s.sock"
#define CHANGELOG_TMP_UNIX_SOCK  DEFAULT_VAR_RUN_DIRECTORY"/.%s%lu.sock"

/**
 * header starts with the version and the format of the changelog.
 * 'version' not much of a use now.
 */
#define CHANGELOG_HEADER                                                \
        "GlusterFS Changelog | version: v%d.%d | encoding : %d\n"

#define CHANGELOG_MAKE_SOCKET_PATH(brick_path, sockpath, len) do {      \
                char md5_sum[MD5_DIGEST_LENGTH*2+1] = {0,};             \
                md5_wrapper((unsigned char *) brick_path,               \
                            strlen(brick_path),                         \
                            md5_sum);                                   \
                (void) snprintf (sockpath, len,                         \
                                 CHANGELOG_UNIX_SOCK, md5_sum);         \
        } while (0)

#define CHANGELOG_MAKE_TMP_SOCKET_PATH(brick_path, sockpath, len) do {  \
                unsigned long pid = 0;                                  \
                char md5_sum[MD5_DIGEST_LENGTH*2+1] = {0,};             \
                pid = (unsigned long) getpid ();                        \
                md5_wrapper((unsigned char *) brick_path,               \
                            strlen(brick_path),                         \
                            md5_sum);                                   \
                (void) snprintf (sockpath,                              \
                                 len, CHANGELOG_TMP_UNIX_SOCK,          \
                                 md5_sum, pid);                         \
        } while (0)


/**
 * ... used by libgfchangelog.
 */
#define CHANGELOG_GET_HEADER_INFO(fd, buffer, len, enc, maj, min, elen) do { \
                FILE *fp;                                               \
                int fd_dup;                                             \
                                                                        \
                enc = -1;                                               \
                maj = -1;                                               \
                min = -1;                                               \
                fd_dup = dup (fd);                                      \
                                                                        \
                if (fd_dup != -1) {                                     \
                        fp = fdopen (fd_dup, "r");                      \
                        if (fp) {                                       \
                                if (fgets (buffer, len, fp)) {          \
                                        elen = strlen (buffer);         \
                                        sscanf (buffer,                 \
                                                CHANGELOG_HEADER,       \
                                                &maj, &min, &enc);      \
                                }                                       \
                                fclose (fp);                            \
                        } else {                                        \
                                sys_close (fd_dup);                     \
                        }                                               \
                }                                                       \
        } while (0)

#define CHANGELOG_FILL_HTIME_DIR(changelog_dir, path) do {              \
                strncpy (path, changelog_dir, sizeof (path) - 1);       \
                strcat (path, "/htime");                                \
        } while(0)

#define CHANGELOG_FILL_CSNAP_DIR(changelog_dir, path) do {              \
                strncpy (path, changelog_dir, sizeof (path) - 1);       \
                strcat (path, "/csnap");                                \
        } while(0)
/**
 * everything after 'CHANGELOG_TYPE_ENTRY' are internal types
 * (ie. none of the fops trigger this type of event), hence
 * CHANGELOG_MAX_TYPE = 3
 */
typedef enum {
        CHANGELOG_TYPE_DATA = 0,
        CHANGELOG_TYPE_METADATA,
        CHANGELOG_TYPE_ENTRY,
        CHANGELOG_TYPE_ROLLOVER,
        CHANGELOG_TYPE_FSYNC,
} changelog_log_type;

/* operation modes - RT for now */
typedef enum {
        CHANGELOG_MODE_RT = 0,
} changelog_mode_t;

/* encoder types */

typedef enum {
        CHANGELOG_ENCODE_MIN = 0,
        CHANGELOG_ENCODE_BINARY,
        CHANGELOG_ENCODE_ASCII,
        CHANGELOG_ENCODE_MAX,
} changelog_encoder_t;

#define CHANGELOG_VALID_ENCODING(enc)                                   \
        (enc > CHANGELOG_ENCODE_MIN && enc < CHANGELOG_ENCODE_MAX)

#define CHANGELOG_TYPE_IS_ENTRY(type)  (type == CHANGELOG_TYPE_ENTRY)
#define CHANGELOG_TYPE_IS_ROLLOVER(type)  (type == CHANGELOG_TYPE_ROLLOVER)
#define CHANGELOG_TYPE_IS_FSYNC(type)  (type == CHANGELOG_TYPE_FSYNC)

#endif /* _CHANGELOG_MISC_H */
