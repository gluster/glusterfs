/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "changelog-mem-types.h"
#include "gf-changelog-helpers.h"
#include "changelog-lib-messages.h"
#include <glusterfs/syscall.h>

size_t
gf_changelog_write(int fd, char *buffer, size_t len)
{
    ssize_t size = 0;
    size_t written = 0;

    while (written < len) {
        size = sys_write(fd, buffer + written, len - written);
        if (size <= 0)
            break;

        written += size;
    }

    return written;
}

void
gf_rfc3986_encode_space_newline(unsigned char *s, char *enc, char *estr)
{
    for (; *s; s++) {
        if (estr[*s])
            sprintf(enc, "%c", estr[*s]);
        else
            sprintf(enc, "%%%02X", *s);
        while (*++enc)
            ;
    }
}

/**
 * thread safe version of readline with buffering
 * (taken from Unix Network Programming Volume I, W.R. Stevens)
 *
 * This is favoured over fgets() as we'd need to ftruncate()
 * (see gf_changelog_scan() API) to record new changelog files.
 * stream open functions does have a truncate like api (although
 * that can be done via @fflush(fp), @ftruncate(fd) and @fseek(fp),
 * but this involves mixing POSIX file descriptors and stream FILE *).
 *
 * NOTE: This implementation still does work with more than one fd's
 *       used to perform gf_readline(). For this very reason it's not
 *       made a part of libglusterfs.
 */

static __thread read_line_t thread_tsd = {};

static ssize_t
my_read(read_line_t *tsd, int fd, char *ptr)
{
    if (tsd->rl_cnt <= 0) {
        tsd->rl_cnt = sys_read(fd, tsd->rl_buf, MAXLINE);

        if (tsd->rl_cnt < 0)
            return -1;
        else if (tsd->rl_cnt == 0)
            return 0;
        tsd->rl_bufptr = tsd->rl_buf;
    }

    tsd->rl_cnt--;
    *ptr = *tsd->rl_bufptr++;
    return 1;
}

ssize_t
gf_readline(int fd, void *vptr, size_t maxlen)
{
    size_t n = 0;
    size_t rc = 0;
    char c = ' ';
    char *ptr = NULL;
    read_line_t *tsd = &thread_tsd;

    ptr = vptr;
    for (n = 1; n < maxlen; n++) {
        if ((rc = my_read(tsd, fd, &c)) == 1) {
            *ptr++ = c;
            if (c == '\n')
                break;
        } else if (rc == 0) {
            *ptr = '\0';
            return (n - 1);
        } else
            return -1;
    }

    *ptr = '\0';
    return n;
}

off_t
gf_lseek(int fd, off_t offset, int whence)
{
    off_t off = 0;
    read_line_t *tsd = &thread_tsd;

    off = sys_lseek(fd, offset, whence);
    if (off == -1)
        return -1;

    tsd->rl_cnt = 0;
    tsd->rl_bufptr = tsd->rl_buf;

    return off;
}

int
gf_ftruncate(int fd, off_t length)
{
    read_line_t *tsd = &thread_tsd;

    if (sys_ftruncate(fd, 0))
        return -1;

    tsd->rl_cnt = 0;
    tsd->rl_bufptr = tsd->rl_buf;

    return 0;
}

int
gf_thread_cleanup(xlator_t *this, pthread_t thread)
{
    int ret = 0;
    void *res = NULL;

    ret = pthread_cancel(thread);
    if (ret != 0) {
        gf_msg(this->name, GF_LOG_WARNING, 0,
               CHANGELOG_LIB_MSG_THREAD_CLEANUP_WARNING,
               "Failed to send cancellation to thread");
        goto error_return;
    }

    ret = pthread_join(thread, &res);
    if (ret != 0) {
        gf_msg(this->name, GF_LOG_WARNING, 0,
               CHANGELOG_LIB_MSG_THREAD_CLEANUP_WARNING,
               "failed to join thread");
        goto error_return;
    }

    if (res != PTHREAD_CANCELED) {
        gf_msg(this->name, GF_LOG_WARNING, 0,
               CHANGELOG_LIB_MSG_THREAD_CLEANUP_WARNING,
               "Thread could not be cleaned up");
        goto error_return;
    }

    return 0;

error_return:
    return -1;
}
