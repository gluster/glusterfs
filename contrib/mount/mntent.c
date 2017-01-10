/*
 * Copyright (c) 1980, 1989, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2001
 *      David Rufino <daverufino@btinternet.com>
 * Copyright (c) 2014
 *      Red Hat, Inc. <http://www.redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if !defined(GF_LINUX_HOST_OS)

#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#include "mntent_compat.h"

#ifdef __NetBSD__
typedef struct statvfs gf_statfs_t;
#else
typedef struct statfs gf_statfs_t;
#endif

typedef struct _mntent_state {
        struct mntent  mntent;
        gf_statfs_t   *statfs;
        int            count;
        int            pos;
        /* A buffer big enough to store all defined flags as a string.
         * Increase it if necessary when more flags are defined. */
        char           buf[256];
} mntent_state_t;

typedef struct _mntflag {
        unsigned long  value;
        const char    *on;
        const char    *off;
} mntflag_t;

static mntflag_t mntflags[] = {
        { MNT_RDONLY,      "ro",          "rw" },
        { MNT_SYNCHRONOUS, "sync",        NULL },
        { MNT_NOEXEC,      "noexec",      NULL },
        { MNT_NOSUID,      "nosuid",      NULL },
#if !defined(__FreeBSD__)
        { MNT_NODEV,       "nodev",       NULL },
#endif /* __FreeBSD__ */
        { MNT_UNION,       "union",       NULL },
        { MNT_ASYNC,       "async",       NULL },
#if !defined(GF_DARWIN_HOST_OS)
        { MNT_NOATIME,     "noatime",     NULL },
#if !defined(__NetBSD__)
        { MNT_NOCLUSTERR,  "noclusterr",  NULL },
        { MNT_NOCLUSTERW,  "noclusterw",  NULL },
        { MNT_NOSYMFOLLOW, "nosymfollow", NULL },
        { MNT_SUIDDIR,     "suiddir",     NULL },
#endif /* !__NetBSD__ */
#endif /* !GF_DARWIN_HOST_OS */
        { 0,               NULL,          NULL }
};

char *
hasmntopt (const struct mntent *mnt, const char *option)
{
        char *opt, *optbuf;
        int len;

        optbuf = strdup(mnt->mnt_opts);
        if (optbuf == NULL) {
                return NULL;
        }

        opt = optbuf;
        len = 0;
        while (*opt) {
                while (opt[len] != 0) {
                        if (opt[len] == ' ') {
                                opt[len++] = 0;
                                break;
                        }
                        len++;
                }
                if ((*opt != 0) && (strcasecmp(opt, option) == 0)) {
                        break;
                }
                opt += len;
                len = 0;
        }
        free(optbuf);
        if (len == 0) {
                return NULL;
        }

        return opt - optbuf + mnt->mnt_opts;
}

static int
writeopt(const char *text, char *buf, int buflen, int pos)
{
        int len;

        /* buflen must be > 0 */

        if (text == NULL) {
                return pos;
        }

        buf += pos;
        if (pos > 0) {
                /* We are sure we have at least one byte to store the space.
                 * We don't need to check buflen here. */
                *buf++ = ' ';
                pos++;
        }
        len = strlen(text) + 1;
        pos += len;
        if (pos >= buflen) {
                /* There won't be enough space for the text and the
                 * terminating null character. We copy as much as we can
                 * of the text and mark the end of the string with '...' */
                memcpy(buf, text, buflen - pos + len);
                if (buflen > 3) {
                        strcpy(buf + buflen - 4, "...");
                } else {
                        strncpy(buf, "...", buflen - 1);
                        buf[buflen - 1] = 0;
                }
                pos = buflen;
        } else {
                memcpy(buf, text, len);
        }

        return pos;
}

static char *
flags2opts (int flags, char *buf, int buflen)
{
        char other[16];
        mntflag_t *flg;
        int pos;

        if (buflen == 0) {
                return NULL;
        }

        pos = 0;
        for (flg = mntflags; flg->value != 0; flg++) {
                pos = writeopt((flags & flg->value) == 0 ? flg->off : flg->on,
                               buf, buflen, pos);
                flags &= ~flg->value;
        }

        if (flags != 0) {
                sprintf(other, "[0x%x]", flags);
                writeopt(other, buf, buflen, pos);
        }

        return buf;
}

static void
statfs_to_mntent (struct mntent *mntent, gf_statfs_t *mntbuf, char *buf,
                  int buflen)
{
        int f_flags;

        mntent->mnt_fsname = mntbuf->f_mntfromname;
        mntent->mnt_dir = mntbuf->f_mntonname;
        mntent->mnt_type = mntbuf->f_fstypename;

#ifdef __NetBSD__
        f_flags = mntbuf->f_flag;
#else
        f_flags = mntbuf->f_flags;
#endif
        mntent->mnt_opts = flags2opts (f_flags, buf, buflen);

        mntent->mnt_freq = mntent->mnt_passno = 0;
}

struct mntent *
getmntent_r (FILE *fp, struct mntent *mntent, char *buf, int buflen)
{
        mntent_state_t *state = (mntent_state_t *)fp;

        if (state->pos >= state->count) {
                return NULL;
        }

        statfs_to_mntent(mntent, &state->statfs[state->pos++], buf, buflen);

        return mntent;
}

struct mntent *
getmntent (FILE *fp)
{
        mntent_state_t *state = (mntent_state_t *)fp;

        return getmntent_r(fp, &state->mntent, state->buf,
                           sizeof(state->buf));
}

FILE *
setmntent (const char *filename, const char *type)
{
        mntent_state_t *state;

        /* We don't really need to access any file so we'll use the FILE* as
         * a fake file to store state information.
         */

        state = malloc(sizeof(mntent_state_t));
        if (state != NULL) {
                state->pos = 0;
                state->count = getmntinfo(&state->statfs, MNT_NOWAIT);
        }

        return (FILE *)state;
}

int
endmntent (FILE *fp)
{
        free(fp);

        return 1; /* endmntent() always returns 1 */
}

#endif /* !GF_LINUX_HOST_OS */
