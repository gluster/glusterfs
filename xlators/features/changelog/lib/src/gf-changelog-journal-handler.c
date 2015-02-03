/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include "uuid.h"
#include "globals.h"
#include "glusterfs.h"

#include "gf-changelog-helpers.h"

/* from the changelog translator */
#include "changelog-misc.h"
#include "changelog-mem-types.h"

#include "gf-changelog-journal.h"

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
                           gf_changelog_journal_t *jnl,
                           int from_fd, int to_fd,
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
        void   *start            = NULL;
        char    current_mover    = ' ';
        size_t  blen             = 0;
        int     parse_err        = 0;
        char ascii[LINE_BUFSIZE] = {0,};

        nleft = stbuf->st_size;

        start = mmap (NULL, nleft, PROT_READ, MAP_PRIVATE, from_fd, 0);
        if (start == MAP_FAILED) {
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
                          gf_changelog_journal_t *jnl,
                          int from_fd, int to_fd,
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
        void         *start         = NULL;
        char         *mover         = NULL;
        int           parse_err     = 0;
        char          current_mover = ' ';
        char ascii[LINE_BUFSIZE]    = {0,};
        const char   *fopname       = NULL;

        nleft = stbuf->st_size;

        start = mmap (NULL, nleft, PROT_READ, MAP_PRIVATE, from_fd, 0);
        if (start == MAP_FAILED) {
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
                                                   eptr, jnl->rfc3986);
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
gf_changelog_decode (xlator_t *this, gf_changelog_journal_t *jnl,
                     int from_fd, int to_fd, struct stat *stbuf, int *zerob)
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
                ret = gf_changelog_parse_binary (this, jnl, from_fd,
                                                 to_fd, elen, stbuf);
                break;

        case CHANGELOG_ENCODE_ASCII:
                ret = gf_changelog_parse_ascii (this, jnl, from_fd,
                                                to_fd, elen, stbuf);
                break;
        default:
                ret = gf_changelog_copy (this, from_fd, to_fd);
        }

 out:
        return ret;
}

int
gf_changelog_publish (xlator_t *this,
                      gf_changelog_journal_t *jnl, char *from_path)
{
        int         ret        = 0;
        char dest[PATH_MAX]    = {0,};
        char to_path[PATH_MAX] = {0,};
        struct stat stbuf      = {0,};

        (void) snprintf (to_path, PATH_MAX, "%s%s",
                         jnl->jnl_current_dir, basename (from_path));

        /* handle zerob file that wont exist in current */
        ret = stat (to_path, &stbuf);
        if (ret){
                if (errno == ENOENT)
                        ret = 0;
                goto out;
        }

        (void) snprintf (dest, PATH_MAX, "%s%s",
                         jnl->jnl_processing_dir, basename (from_path));

        ret = rename (to_path, dest);
        if (ret){
                gf_log (this->name, GF_LOG_ERROR,
                        "error moving %s to processing dir"
                        " (reason: %s)", to_path, strerror (errno));
        }

out:
        return ret;
}

int
gf_changelog_consume (xlator_t *this,
                      gf_changelog_journal_t *jnl,
                      char *from_path, gf_boolean_t no_publish)
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
                ret = -1;
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
                         jnl->jnl_current_dir, basename (from_path));
        (void) snprintf (dest, PATH_MAX, "%s%s",
                         jnl->jnl_processing_dir, basename (from_path));

        fd2 = open (to_path, O_CREAT | O_TRUNC | O_RDWR,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd2 < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "cannot create ascii changelog file %s (reason %s)",
                        to_path, strerror (errno));
                goto close_fd;
        } else {
                ret = gf_changelog_decode (this, jnl, fd1,
                                           fd2, &stbuf, &zerob);

                close (fd2);

                if (!ret) {
                        /* move it to processing on a successful
                           decode */
                        if (no_publish == _gf_true)
                                goto close_fd;
                        ret = rename (to_path, dest);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "error moving %s to processing dir"
                                        " (reason: %s)", to_path,
                                        strerror (errno));
                }

                /* remove it from .current if it's an empty file */
                if (zerob) {
                        /* zerob changelogs must be unlinked */
                        ret = unlink (to_path);
                        if (ret)
                                gf_log (this->name, GF_LOG_ERROR,
                                        "could not unlink %s (reason: %s)",
                                        to_path, strerror (errno));
                }
        }

 close_fd:
        close (fd1);

 out:
        return ret;
}

void *
gf_changelog_process (void *data)
{
        int ret = 0;
        xlator_t *this = NULL;
        gf_changelog_journal_t *jnl = NULL;
        gf_changelog_entry_t *entry = NULL;
        gf_changelog_processor_t *jnl_proc = NULL;

        this = THIS;

        jnl = data;
        jnl_proc = jnl->jnl_proc;

        while (1) {
                pthread_mutex_lock (&jnl_proc->lock);
                {
                        while (list_empty (&jnl_proc->entries)) {
                                jnl_proc->waiting = _gf_true;
                                pthread_cond_wait
                                        (&jnl_proc->cond, &jnl_proc->lock);
                        }

                        entry = list_first_entry (&jnl_proc->entries,
                                                  gf_changelog_entry_t, list);
                        list_del (&entry->list);
                        jnl_proc->waiting = _gf_false;
                }
                pthread_mutex_unlock (&jnl_proc->lock);

                if (entry) {
                        ret = gf_changelog_consume (this, jnl,
                                                    entry->path, _gf_false);
                        GF_FREE (entry);
                }
        }

        return NULL;
}

inline void
gf_changelog_queue_journal (gf_changelog_processor_t *jnl_proc,
                            changelog_event_t *event)
{
        size_t len = 0;
        gf_changelog_entry_t *entry = NULL;

        entry = GF_CALLOC (1, sizeof (gf_changelog_entry_t),
                           gf_changelog_mt_libgfchangelog_entry_t);
        if (!entry)
                return;
        INIT_LIST_HEAD (&entry->list);

        len = strlen (event->u.journal.path);
        (void)memcpy (entry->path, event->u.journal.path, len+1);

        pthread_mutex_lock (&jnl_proc->lock);
        {
                list_add_tail (&entry->list, &jnl_proc->entries);
                if (jnl_proc->waiting)
                        pthread_cond_signal (&jnl_proc->cond);
        }
        pthread_mutex_unlock (&jnl_proc->lock);

        return;
}

void
gf_changelog_handle_journal (void *xl, char *brick,
                             void *cbkdata, changelog_event_t *event)
{
        int                       ret      = 0;
        gf_changelog_journal_t   *jnl      = NULL;
        gf_changelog_processor_t *jnl_proc = NULL;

        jnl      = cbkdata;
        jnl_proc = jnl->jnl_proc;

        gf_changelog_queue_journal (jnl_proc, event);
}

void
gf_changelog_journal_disconnect (void *xl, char *brick, void *data)
{
        gf_changelog_journal_t *jnl = NULL;

        jnl = data;

        pthread_spin_lock (&jnl->lock);
        {
                JNL_SET_API_STATE (jnl, JNL_API_DISCONNECTED);
        };
        pthread_spin_unlock (&jnl->lock);
}

void
gf_changelog_journal_connect (void *xl, char *brick, void *data)
{
        gf_changelog_journal_t *jnl = NULL;

        jnl = data;

        pthread_spin_lock (&jnl->lock);
        {
                JNL_SET_API_STATE (jnl, JNL_API_CONNECTED);
        };
        pthread_spin_unlock (&jnl->lock);

        return;
}

void
gf_changelog_cleanup_processor (gf_changelog_journal_t *jnl)
{
        int ret = 0;
        xlator_t *this = NULL;
        gf_changelog_processor_t *jnl_proc = NULL;

        this = THIS;
        if (!this || !jnl || !jnl->jnl_proc)
                goto error_return;

        jnl_proc = jnl->jnl_proc;

        ret = gf_thread_cleanup (this, jnl_proc->processor);
        if (ret != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "failed to cleanup processor thread");
                goto error_return;
        }

        (void)pthread_mutex_destroy (&jnl_proc->lock);
        (void)pthread_cond_destroy (&jnl_proc->cond);

        GF_FREE (jnl_proc);

 error_return:
        return;
}

int
gf_changelog_init_processor (gf_changelog_journal_t *jnl)
{
        int ret = -1;
        gf_changelog_processor_t *jnl_proc = NULL;

        jnl_proc = GF_CALLOC (1, sizeof (gf_changelog_processor_t),
                              gf_changelog_mt_libgfchangelog_t);
        if (!jnl_proc)
                goto error_return;

        ret = pthread_mutex_init (&jnl_proc->lock, NULL);
        if (ret != 0)
                goto free_jnl_proc;
        ret = pthread_cond_init (&jnl_proc->cond, NULL);
        if (ret != 0)
                goto cleanup_mutex;

        INIT_LIST_HEAD (&jnl_proc->entries);
        ret = pthread_create (&jnl_proc->processor,
                              NULL, gf_changelog_process, jnl);
        if (ret != 0)
                goto cleanup_cond;
        jnl_proc->waiting = _gf_false;

        jnl->jnl_proc = jnl_proc;
        return 0;

 cleanup_cond:
        (void) pthread_cond_destroy (&jnl_proc->cond);
 cleanup_mutex:
        (void) pthread_mutex_destroy (&jnl_proc->lock);
 free_jnl_proc:
        GF_FREE (jnl_proc);
 error_return:
        return -1;
}

static void
gf_changelog_cleanup_fds (gf_changelog_journal_t *jnl)
{
        /* tracker fd */
        if (jnl->jnl_fd != -1)
                close (jnl->jnl_fd);
        /* processing dir */
        if (jnl->jnl_dir)
                closedir (jnl->jnl_dir);

        if (jnl->jnl_working_dir)
                free (jnl->jnl_working_dir); /* allocated by realpath */
}

static int
gf_changelog_open_dirs (xlator_t *this, gf_changelog_journal_t *jnl)
{
        int  ret                    = -1;
        DIR *dir                    = NULL;
        int  tracker_fd             = 0;
        char tracker_path[PATH_MAX] = {0,};

        /* .current */
        (void) snprintf (jnl->jnl_current_dir, PATH_MAX,
                         "%s/"GF_CHANGELOG_CURRENT_DIR"/",
                         jnl->jnl_working_dir);
        ret = recursive_rmdir (jnl->jnl_current_dir);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to rmdir: %s, err: %s",
                        jnl->jnl_current_dir, strerror (errno));
                goto out;
        }
        ret = mkdir_p (jnl->jnl_current_dir, 0600, _gf_false);
        if (ret)
                goto out;

        /* .processed */
        (void) snprintf (jnl->jnl_processed_dir, PATH_MAX,
                         "%s/"GF_CHANGELOG_PROCESSED_DIR"/",
                         jnl->jnl_working_dir);
        ret = mkdir_p (jnl->jnl_processed_dir, 0600, _gf_false);
        if (ret)
                goto out;

        /* .processing */
        (void) snprintf (jnl->jnl_processing_dir, PATH_MAX,
                         "%s/"GF_CHANGELOG_PROCESSING_DIR"/",
                         jnl->jnl_working_dir);
        ret = recursive_rmdir (jnl->jnl_processing_dir);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Failed to rmdir: %s, err: %s",
                        jnl->jnl_processing_dir, strerror (errno));
                goto out;
        }

        ret = mkdir_p (jnl->jnl_processing_dir, 0600, _gf_false);
        if (ret)
                goto out;

        dir = opendir (jnl->jnl_processing_dir);
        if (!dir) {
                gf_log ("", GF_LOG_ERROR,
                        "opendir() error [reason: %s]", strerror (errno));
                goto out;
        }

        jnl->jnl_dir = dir;

        (void) snprintf (tracker_path, PATH_MAX,
                         "%s/"GF_CHANGELOG_TRACKER, jnl->jnl_working_dir);

        tracker_fd = open (tracker_path, O_CREAT | O_APPEND | O_RDWR,
                           S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (tracker_fd < 0) {
                closedir (jnl->jnl_dir);
                ret = -1;
                goto out;
        }

        jnl->jnl_fd = tracker_fd;
        ret = 0;
 out:
        return ret;
}

int
gf_changelog_init_history (xlator_t *this,
                           gf_changelog_journal_t *jnl,
                           char *brick_path, char *scratch_dir)
{
        int i   = 0;
        int ret = 0;
        char hist_scratch_dir[PATH_MAX] = {0,};

        jnl->hist_jnl = GF_CALLOC (1, sizeof (*jnl),
                         gf_changelog_mt_libgfchangelog_t);
        if (!jnl->hist_jnl)
                goto error_return;

        jnl->hist_jnl->jnl_dir = NULL;
        jnl->hist_jnl->jnl_fd =  -1;

        (void) strncpy (hist_scratch_dir, scratch_dir, PATH_MAX);
        (void) snprintf (hist_scratch_dir, PATH_MAX,
                         "%s/"GF_CHANGELOG_HISTORY_DIR"/",
                         jnl->jnl_working_dir);

        ret = mkdir_p (hist_scratch_dir, 0600, _gf_false);
        if (ret)
                goto dealloc_hist;

        jnl->hist_jnl->jnl_working_dir = realpath (hist_scratch_dir, NULL);
        if (!jnl->hist_jnl->jnl_working_dir)
                goto dealloc_hist;

        ret = gf_changelog_open_dirs (this, jnl->hist_jnl);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "could not create entries in history scratch dir");
                goto dealloc_hist;
        }

        (void) strncpy (jnl->hist_jnl->jnl_brickpath, brick_path, PATH_MAX);

        for (i = 0; i < 256; i++) {
                jnl->hist_jnl->rfc3986[i] =
                        (isalnum(i) || i == '~' ||
                        i == '-' || i == '.' || i == '_') ? i : 0;
        }

        return 0;

 dealloc_hist:
        GF_FREE (jnl->hist_jnl);
        jnl->hist_jnl = NULL;
 error_return:
        return -1;
}

void
gf_changelog_journal_fini (void *xl, char *brick, void *data)
{
        int ret = 0;
        xlator_t *this = NULL;
        gf_changelog_journal_t *jnl = NULL;

        this = xl;
        jnl = data;

        gf_changelog_cleanup_processor (jnl);

        gf_changelog_cleanup_fds (jnl);
        if (jnl->hist_jnl)
                gf_changelog_cleanup_fds (jnl->hist_jnl);

        GF_FREE (jnl);
}

void *
gf_changelog_journal_init (void *xl, struct gf_brick_spec *brick)
{
        int                     i           = 0;
        int                     ret         = 0;
        xlator_t               *this        = NULL;
        struct stat             buf         = {0,};
        char                   *scratch_dir = NULL;
        gf_changelog_journal_t *jnl         = NULL;

        this = xl;
        scratch_dir = (char *) brick->ptr;

        jnl = GF_CALLOC (1, sizeof (gf_changelog_journal_t),
                         gf_changelog_mt_libgfchangelog_t);
        if (!jnl)
                goto error_return;

        if (stat (scratch_dir, &buf) && errno == ENOENT) {
                ret = mkdir_p (scratch_dir, 0600, _gf_true);
                if (ret)
                        goto dealloc_private;
        }

        jnl->jnl_working_dir = realpath (scratch_dir, NULL);
        if (!jnl->jnl_working_dir)
                goto dealloc_private;

        ret = gf_changelog_open_dirs (this, jnl);
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "could not create entries in scratch dir");
                goto dealloc_private;
        }

        (void) strncpy (jnl->jnl_brickpath, brick->brick_path, PATH_MAX);

        /* RFC 3986 {de,en}coding */
        for (i = 0; i < 256; i++) {
                jnl->rfc3986[i] =
                        (isalnum(i) || i == '~' ||
                        i == '-' || i == '.' || i == '_') ? i : 0;
        }

        ret = gf_changelog_init_history (this, jnl,
                                         brick->brick_path, scratch_dir);
        if (ret)
                goto cleanup_fds;

        /* initialize journal processor */
        ret = gf_changelog_init_processor (jnl);
        if (ret)
                goto cleanup_fds;

        JNL_SET_API_STATE (jnl, JNL_API_CONN_INPROGESS);
        ret = pthread_spin_init (&jnl->lock, 0);
        if (ret != 0)
                goto cleanup_processor;
        return jnl;

 cleanup_processor:
        gf_changelog_cleanup_processor (jnl);
 cleanup_fds:
        gf_changelog_cleanup_fds (jnl);
        if (jnl->hist_jnl)
                gf_changelog_cleanup_fds (jnl->hist_jnl);
 dealloc_private:
        GF_FREE (jnl);
 error_return:
        return NULL;
}
