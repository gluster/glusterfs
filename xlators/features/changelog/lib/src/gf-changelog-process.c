/*
   Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <unistd.h>
#include <pthread.h>

#include "uuid.h"
#include "globals.h"
#include "glusterfs.h"

#include "gf-changelog-helpers.h"

/* from the changelog translator */
#include "changelog-misc.h"

extern int byebye;

/**
 * number of gfid records after fop number
 */
int nr_gfids[] = {
        [GF_FOP_MKNOD]   = 1,
        [GF_FOP_MKDIR]   = 1,
        [GF_FOP_UNLINK]  = 1,
        [GF_FOP_RMDIR]   = 1,
        [GF_FOP_SYMLINK] = 1,
        [GF_FOP_RENAME]  = 2,
        [GF_FOP_LINK]    = 1,
        [GF_FOP_CREATE]  = 1,
};

int nr_extra_recs[] = {
        [GF_FOP_MKNOD]   = 3,
        [GF_FOP_MKDIR]   = 3,
        [GF_FOP_UNLINK]  = 0,
        [GF_FOP_RMDIR]   = 0,
        [GF_FOP_SYMLINK] = 0,
        [GF_FOP_RENAME]  = 0,
        [GF_FOP_LINK]    = 0,
        [GF_FOP_CREATE]  = 3,
};

static char *
binary_to_ascii (uuid_t uuid)
{
        return uuid_utoa (uuid);
}

static char *
conv_noop (char *ptr) { return ptr; }

#define VERIFY_SEPARATOR(ptr, plen, perr)       \
        {                                       \
                if (*(ptr + plen) != '\0') {    \
                        perr = 1;               \
                        break;                  \
                }                               \
        }

#define MOVER_MOVE(mover, nleft, bytes)         \
        {                                       \
                mover += bytes;                 \
                nleft -= bytes;                 \
        }                                       \

#define PARSE_GFID(mov, ptr, le, fn, perr)                      \
        {                                                       \
                VERIFY_SEPARATOR (mov, le, perr);               \
                ptr = fn (mov);                                 \
                if (!ptr) {                                     \
                        perr = 1;                               \
                        break;                                  \
                }                                               \
        }

#define FILL_AND_MOVE(pt, buf, of, mo, nl, le)                          \
        {                                                               \
                GF_CHANGELOG_FILL_BUFFER (pt, buf, of, strlen (pt));    \
                MOVER_MOVE (mo, nl, le);                                \
        }


#define PARSE_GFID_MOVE(ptr, uuid, mover, nleft, perr)          \
        {                                                       \
                memcpy (uuid, mover, sizeof (uuid_t));          \
                ptr = binary_to_ascii (uuid);                   \
                if (!ptr) {                                     \
                        perr = 1;                               \
                        break;                                  \
                }                                               \
                MOVER_MOVE (mover, nleft, sizeof (uuid_t));     \
        }                                                       \

#define LINE_BUFSIZE  3*PATH_MAX /* enough buffer for extra chars too */

/**
 * using mmap() makes parsing easy. fgets() cannot be used here as
 * the binary gfid could contain a line-feed (0x0A), in that case fgets()
 * would read an incomplete line and parsing would fail. using POSIX fds
 * would result is additional code to maintain state in case of partial
 * reads of data (where multiple entries do not fit extirely in the buffer).
 *
 * mmap() gives the flexibility of pointing to an offset in the file
 * without us worrying about reading it in memory (VM does that for us for
 * free).
 */

static int
gf_changelog_parse_binary (xlator_t *this,
                           gf_changelog_t *gfc, int from_fd, int to_fd,
                           size_t start_offset, struct stat *stbuf)

{
        int     ret              = -1;
        off_t   off              = 0;
        off_t   nleft            = 0;
        uuid_t  uuid             = {0,};
        char   *ptr              = NULL;
        char   *bname_start      = NULL;
        char   *bname_end        = NULL;
        char   *mover            = NULL;
        char   *start            = NULL;
        char    current_mover    = ' ';
        size_t  blen             = 0;
        int     parse_err        = 0;
        char ascii[LINE_BUFSIZE] = {0,};

        nleft = stbuf->st_size;

        start = (char *) mmap (NULL, nleft,
                               PROT_READ, MAP_PRIVATE, from_fd, 0);
        if (!start) {
                gf_log (this->name, GF_LOG_ERROR,
                        "mmap() error (reason: %s)", strerror (errno));
                goto out;
        }

        mover = start;

        MOVER_MOVE (mover, nleft, start_offset);

        while (nleft > 0) {

                off = blen = 0;
                ptr = bname_start = bname_end = NULL;

                current_mover = *mover;

                switch (current_mover) {
                case 'D':
                case 'M':
                        MOVER_MOVE (mover, nleft, 1);
                        PARSE_GFID_MOVE (ptr, uuid, mover, nleft, parse_err);

                        break;

                case 'E':
                        MOVER_MOVE (mover, nleft, 1);
                        PARSE_GFID_MOVE (ptr, uuid, mover, nleft, parse_err);

                        bname_start = mover;
                        if ( (bname_end = strchr (mover, '\n')) == NULL ) {
                                parse_err = 1;
                                break;
                        }

                        blen = bname_end - bname_start;
                        MOVER_MOVE (mover, nleft, blen);

                        break;

                default:
                        parse_err = 1;
                }

                if (parse_err)
                        break;

                GF_CHANGELOG_FILL_BUFFER (&current_mover, ascii, off, 1);
                GF_CHANGELOG_FILL_BUFFER (" ", ascii, off, 1);
                GF_CHANGELOG_FILL_BUFFER (ptr, ascii, off, strlen (ptr));
                if (blen)
                        GF_CHANGELOG_FILL_BUFFER (bname_start,
                                                  ascii, off, blen);
                GF_CHANGELOG_FILL_BUFFER ("\n", ascii, off, 1);

                if (gf_changelog_write (to_fd, ascii, off) != off) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "processing binary changelog failed due to "
                                " error in writing ascii change (reason: %s)",
                                strerror (errno));
                        break;
                }

                MOVER_MOVE (mover, nleft, 1);
        }

        if ( (nleft == 0) && (!parse_err))
                ret = 0;

        if (munmap (start, stbuf->st_size))
                gf_log (this->name, GF_LOG_ERROR,
                        "munmap() error (reason: %s)", strerror (errno));
 out:
        return ret;
}

/**
 * ascii decoder:
 *  - separate out one entry from another
 *  - use fop name rather than fop number
 */
static int
gf_changelog_parse_ascii (xlator_t *this,
                          gf_changelog_t *gfc, int from_fd, int to_fd,
                          size_t start_offset, struct stat *stbuf)
{
        int           ng            = 0;
        int           ret           = -1;
        int           fop           = 0;
        int           len           = 0;
        off_t         off           = 0;
        off_t         nleft         = 0;
        char         *ptr           = NULL;
        char         *eptr          = NULL;
        char         *start         = NULL;
        char         *mover         = NULL;
        int           parse_err     = 0;
        char          current_mover = ' ';
        char ascii[LINE_BUFSIZE]    = {0,};
        const char   *fopname       = NULL;

        nleft = stbuf->st_size;

        start = (char *) mmap (NULL, nleft,
                               PROT_READ, MAP_PRIVATE, from_fd, 0);
        if (!start) {
                gf_log (this->name, GF_LOG_ERROR,
                        "mmap() error (reason: %s)", strerror (errno));
                goto out;
        }

        mover = start;

        MOVER_MOVE (mover, nleft, start_offset);

        while (nleft > 0) {
                off = 0;
                current_mover = *mover;

                GF_CHANGELOG_FILL_BUFFER (&current_mover, ascii, off, 1);
                GF_CHANGELOG_FILL_BUFFER (" ", ascii, off, 1);

                switch (current_mover) {
                case 'D':
                        MOVER_MOVE (mover, nleft, 1);

                        /* target gfid */
                        PARSE_GFID (mover, ptr, UUID_CANONICAL_FORM_LEN,
                                    conv_noop, parse_err);
                        FILL_AND_MOVE(ptr, ascii, off,
                                      mover, nleft, UUID_CANONICAL_FORM_LEN);
                        break;
                case 'M':
                        MOVER_MOVE (mover, nleft, 1);

                        /* target gfid */
                        PARSE_GFID (mover, ptr, UUID_CANONICAL_FORM_LEN,
                                    conv_noop, parse_err);
                        FILL_AND_MOVE (ptr, ascii, off,
                                       mover, nleft, UUID_CANONICAL_FORM_LEN);
                        FILL_AND_MOVE (" ", ascii, off, mover, nleft, 1);

                        /* fop */
                        len = strlen (mover);
                        VERIFY_SEPARATOR (mover, len, parse_err);

                        fop = atoi (mover);
                        if ( (fopname = gf_fop_list[fop]) == NULL) {
                                parse_err = 1;
                                break;
                        }

                        MOVER_MOVE (mover, nleft, len);

                        len = strlen (fopname);
                        GF_CHANGELOG_FILL_BUFFER (fopname, ascii, off, len);

                        break;

                case 'E':
                        MOVER_MOVE (mover, nleft, 1);

                        /* target gfid */
                        PARSE_GFID (mover, ptr, UUID_CANONICAL_FORM_LEN,
                                    conv_noop, parse_err);
                        FILL_AND_MOVE (ptr, ascii, off,
                                       mover, nleft, UUID_CANONICAL_FORM_LEN);
                        FILL_AND_MOVE (" ", ascii, off,
                                       mover, nleft, 1);

                        /* fop */
                        len = strlen (mover);
                        VERIFY_SEPARATOR (mover, len, parse_err);

                        fop = atoi (mover);
                        if ( (fopname = gf_fop_list[fop]) == NULL) {
                                parse_err = 1;
                                break;
                        }

                        MOVER_MOVE (mover, nleft, len);

                        len = strlen (fopname);
                        GF_CHANGELOG_FILL_BUFFER (fopname, ascii, off, len);

                        ng = nr_extra_recs[fop];
                        for (;ng > 0; ng--) {
                                MOVER_MOVE (mover, nleft, 1);
                                len = strlen (mover);
                                VERIFY_SEPARATOR (mover, len, parse_err);

                                GF_CHANGELOG_FILL_BUFFER (" ", ascii, off, 1);
                                FILL_AND_MOVE (mover, ascii,
                                               off, mover, nleft, len);
                        }

                        /* pargfid + bname */
                        ng = nr_gfids[fop];
                        while (ng-- > 0) {
                                MOVER_MOVE (mover, nleft, 1);
                                len = strlen (mover);
                                GF_CHANGELOG_FILL_BUFFER (" ", ascii, off, 1);

                                PARSE_GFID (mover, ptr, len,
                                            conv_noop, parse_err);
                                eptr = calloc (3, strlen (ptr));
                                if (!eptr) {
                                        parse_err = 1;
                                        break;
                                }

                                gf_rfc3986_encode ((unsigned char *) ptr,
                                                   eptr, gfc->rfc3986);
                                FILL_AND_MOVE (eptr, ascii, off,
                                               mover, nleft, len);
                                free (eptr);
                        }

                        break;
                default:
                        parse_err = 1;
                }

                if (parse_err)
                        break;

                GF_CHANGELOG_FILL_BUFFER ("\n", ascii, off, 1);

                if (gf_changelog_write (to_fd, ascii, off) != off) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "processing ascii changelog failed due to "
                                " error in writing change (reason: %s)",
                                strerror (errno));
                        break;
                }

                MOVER_MOVE (mover, nleft, 1);

        }

        if ( (nleft == 0) && (!parse_err))
                ret = 0;

        if (munmap (start, stbuf->st_size))
                gf_log (this->name, GF_LOG_ERROR,
                        "munmap() error (reason: %s)", strerror (errno));

 out:
        return ret;
}

#define COPY_BUFSIZE  8192
static int
gf_changelog_copy (xlator_t *this, int from_fd, int to_fd)
{
        ssize_t size                  = 0;
        char   buffer[COPY_BUFSIZE+1] = {0,};

        while (1) {
                size = read (from_fd, buffer, COPY_BUFSIZE);
                if (size <= 0)
                        break;

                if (gf_changelog_write (to_fd,
                                        buffer, size) != size) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "error processing ascii changlog");
                        size = -1;
                        break;
                }
        }

        return (size < 0 ? -1 : 0);
}

static int
gf_changelog_decode (xlator_t *this, gf_changelog_t *gfc, int from_fd,
                     int to_fd, struct stat *stbuf, int *zerob)
{
        int    ret        = -1;
        int    encoding   = -1;
        size_t elen       = 0;
        char buffer[1024] = {0,};

        CHANGELOG_GET_ENCODING (from_fd, buffer, 1024, encoding, elen);
        if (encoding == -1) /* unknown encoding */
                goto out;

        if (!CHANGELOG_VALID_ENCODING (encoding))
                goto out;

        if (elen == stbuf->st_size) {
                *zerob = 1;
                goto out;
        }

        /**
         * start processing after the header
         */
        lseek (from_fd, elen, SEEK_SET);

        switch (encoding) {
        case CHANGELOG_ENCODE_BINARY:
                /**
                 * this ideally should have been a part of changelog-encoders.c
                 * (ie. part of the changelog translator).
                 */
                ret = gf_changelog_parse_binary (this, gfc, from_fd,
                                                 to_fd, elen, stbuf);
                break;

        case CHANGELOG_ENCODE_ASCII:
                ret = gf_changelog_parse_ascii (this, gfc, from_fd,
                                                to_fd, elen, stbuf);
                break;
        default:
                ret = gf_changelog_copy (this, from_fd, to_fd);
        }

 out:
        return ret;
}

static int
gf_changelog_consume (xlator_t *this, gf_changelog_t *gfc, char *from_path)
{
        int         ret        = -1;
        int         fd1        = 0;
        int         fd2        = 0;
        int         zerob      = 0;
        struct stat stbuf      = {0,};
        char dest[PATH_MAX]    = {0,};
        char to_path[PATH_MAX] = {0,};

        ret = stat (from_path, &stbuf);
        if (ret || !S_ISREG(stbuf.st_mode)) {
                gf_log (this->name, GF_LOG_ERROR,
                        "stat failed on changelog file: %s", from_path);
                goto out;
        }

        fd1 = open (from_path, O_RDONLY);
        if (fd1 < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cannot open changelog file: %s (reason: %s)",
                        from_path, strerror (errno));
                goto out;
        }

        (void) snprintf (to_path, PATH_MAX, "%s%s",
                         gfc->gfc_current_dir, basename (from_path));
        (void) snprintf (dest, PATH_MAX, "%s%s",
                         gfc->gfc_processing_dir, basename (from_path));

        fd2 = open (to_path, O_CREAT | O_TRUNC | O_RDWR,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd2 < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cannot create ascii changelog file %s (reason %s)",
                        to_path, strerror (errno));
                goto close_fd;
        } else {
                ret = gf_changelog_decode (this, gfc, fd1,
                                           fd2, &stbuf, &zerob);

                close (fd2);

                if (!ret) {
                        /* move it to processing on a successfull
                           decode */
                        ret = rename (to_path, dest);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "error moving %s to processing dir"
                                        " (reason: %s)", to_path,
                                        strerror (errno));
                }

                /* remove it from .current if it's an empty file */
                if (zerob) {
                        ret = unlink (to_path);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "could not unlink %s (reason: %s",
                                        to_path, strerror (errno));
                }
        }

 close_fd:
        close (fd1);

 out:
        return ret;
}

static char *
gf_changelog_ext_change (xlator_t *this,
                         gf_changelog_t *gfc, char *path, size_t readlen)
{
        int     alo = 0;
        int     ret = 0;
        size_t  len = 0;
        char   *buf = NULL;

        buf = path;
        while (len < readlen) {
                if (*buf == '\0') {
                        alo = 1;
                        gf_log (this->name, GF_LOG_DEBUG,
                                "processing changelog: %s", path);
                        ret = gf_changelog_consume (this, gfc, path);
                }

                if (ret)
                        break;

                len++; buf++;
                if (alo) {
                        alo = 0;
                        path = buf;
                }
        }

        return (ret) ? NULL : path;
}

void *
gf_changelog_process (void *data)
{
        ssize_t         len      = 0;
        ssize_t         offlen   = 0;
        xlator_t       *this     = NULL;
        char           *sbuf     = NULL;
        gf_changelog_t *gfc      = NULL;
        char from_path[PATH_MAX] = {0,};

        gfc = (gf_changelog_t *) data;
        this = gfc->this;

        pthread_detach (pthread_self());

        for (;;) {
                len = gf_changelog_read_path (gfc->gfc_sockfd,
                                              from_path + offlen,
                                              PATH_MAX - offlen);
                if (len < 0)
                        continue; /* ignore it for now */

                if (len == 0) { /* close() from the changelog translator */
                        gf_log (this->name, GF_LOG_INFO, "close from changelog"
                                " notification translator.");

                        if (gfc->gfc_connretries != 1) {
                                if (!gf_changelog_notification_init(this, gfc))
                                        continue;
                        }

                        byebye = 1;
                        break;
                }

                len += offlen;
                sbuf = gf_changelog_ext_change (this, gfc, from_path, len);
                if (!sbuf) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "could not extract changelog filename");
                        continue;
                }

                offlen = 0;
                if (sbuf != (from_path + len)) {
                        offlen = from_path + len - sbuf;
                        memmove (from_path, sbuf, offlen);
                }
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "byebye (%d) from processing thread...", byebye);
        return NULL;
}
