/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _GF_CHANGELOG_HELPERS_H
#define _GF_CHANGELOG_HELPERS_H

#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <pthread.h>

#include <xlator.h>

#define GF_CHANGELOG_TRACKER  "tracker"

#define GF_CHANGELOG_CURRENT_DIR    ".current"
#define GF_CHANGELOG_PROCESSED_DIR  ".processed"
#define GF_CHANGELOG_PROCESSING_DIR ".processing"
#define GF_CHANGELOG_HISTORY_DIR    ".history"
#define TIMESTAMP_LENGTH 10

#ifndef MAXLINE
#define MAXLINE 4096
#endif

#define GF_CHANGELOG_FILL_BUFFER(ptr, ascii, off, len) do {     \
                memcpy (ascii + off, ptr, len);                 \
                off += len;                                     \
        } while (0)

typedef struct read_line {
        int rl_cnt;
        char *rl_bufptr;
        char rl_buf[MAXLINE];
} read_line_t;

typedef struct gf_changelog {
        xlator_t *this;

        /* 'processing' directory stream */
        DIR *gfc_dir;

        /* fd to the tracker file */
        int gfc_fd;

        /* connection retries */
        int gfc_connretries;

        char gfc_sockpath[UNIX_PATH_MAX];

        char gfc_brickpath[PATH_MAX];

        /* socket for receiving notifications */
        int gfc_sockfd;

        char *gfc_working_dir;

        /* RFC 3986 string encoding */
        char rfc3986[256];

        char gfc_current_dir[PATH_MAX];
        char gfc_processed_dir[PATH_MAX];
        char gfc_processing_dir[PATH_MAX];

        pthread_t gfc_changelog_processor;

        /* Holds gfc for History API */
        struct gf_changelog *hist_gfc;

        /* holds 0 done scanning, 1 keep scanning and -1 error */
        int hist_done;
} gf_changelog_t;

typedef struct gf_changelog_history_data {
        int           len;

        int           htime_fd;

        /* parallelism count */
        int           n_parallel;

        /* history from, to indexes */
        unsigned long from;
        unsigned long to;
} gf_changelog_history_data_t;

typedef struct gf_changelog_consume_data {
        /** set of inputs */

        /* fd to read from */
        int             fd;

        /* from @offset */
        off_t           offset;

        xlator_t       *this;
        gf_changelog_t *gfc;

        /** set of outputs */

        /* return value */
        int retval;

        /* journal processed */
        char changelog[PATH_MAX];
} gf_changelog_consume_data_t;

int
gf_changelog_notification_init (xlator_t *this, gf_changelog_t *gfc);

void *
gf_changelog_process (void *data);

ssize_t
gf_changelog_read_path (int fd, char *buffer, size_t bufsize);

void
gf_rfc3986_encode (unsigned char *s, char *enc, char *estr);

size_t
gf_changelog_write (int fd, char *buffer, size_t len);

ssize_t
gf_readline (int fd, void *vptr, size_t maxlen);

int
gf_ftruncate (int fd, off_t length);

off_t
gf_lseek (int fd, off_t offset, int whence);

int
gf_changelog_consume (xlator_t *this,
                      gf_changelog_t *gfc,
                      char *from_path, gf_boolean_t no_publish);
int
gf_changelog_publish (xlator_t *this, gf_changelog_t *gfc, char *from_path);

#endif
