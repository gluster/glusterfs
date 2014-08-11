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

static int pos = -1;
static int mntsize = -1;
static struct mntent _mntent;

char *
hasmntopt (const struct mntent *mnt, const char *option)
{
        int found;
        char *opt, *optbuf;

        optbuf = strdup(mnt->mnt_opts);
        found = 0;
        for (opt = optbuf; (opt = strtok(opt, " ")) != NULL; opt = NULL) {
                if (!strcasecmp(opt, option)) {
                        opt = opt - optbuf + mnt->mnt_opts;
                        free (optbuf);
                        return (opt);
                }
        }
        free (optbuf);
        return (NULL);
}

static char *
concatopt (char *s0, const char *s1)
{
        size_t i;
        char *cp;

        if (s1 == NULL || *s1 == '\0')
                return s0;
        if (s0 && *s0) {
                i = strlen(s0) + strlen(s1) + 1 + 1;
                if ((cp = (char *)malloc(i)) == NULL)
                        return (NULL);
                (void)snprintf(cp, i, "%s %s", s0, s1);
        } else
                cp = strdup(s1);

        if (s0)
                free(s0);
        return (cp);
}


static char *
flags2opts (int flags)
{
        char *res;
        res = NULL;
        res = concatopt(res, (flags & MNT_RDONLY) ? "ro" : "rw");
        if (flags & MNT_SYNCHRONOUS)    res = concatopt(res, "sync");
        if (flags & MNT_NOEXEC)         res = concatopt(res, "noexec");
        if (flags & MNT_NOSUID)         res = concatopt(res, "nosuid");
#if !defined(__FreeBSD__)
        if (flags & MNT_NODEV)          res = concatopt(res, "nodev");
#endif /* __FreeBSD__ */
        if (flags & MNT_UNION)          res = concatopt(res, "union");
        if (flags & MNT_ASYNC)          res = concatopt(res, "async");
#if !defined(GF_DARWIN_HOST_OS)
        if (flags & MNT_NOATIME)        res = concatopt(res, "noatime");
#if !defined(__NetBSD__)
        if (flags & MNT_NOCLUSTERR)     res = concatopt(res, "noclusterr");
        if (flags & MNT_NOCLUSTERW)     res = concatopt(res, "noclusterw");
        if (flags & MNT_NOSYMFOLLOW)    res = concatopt(res, "nosymfollow");
        if (flags & MNT_SUIDDIR)        res = concatopt(res, "suiddir");
#endif /* !__NetBSD__ */
#endif /* !GF_DARWIN_HOS_OS */
        return res;
}

static struct mntent *
statfs_to_mntent (gf_statfs_t *mntbuf)
{
        static char opts_buf[40], *tmp;
        int f_flags;

        _mntent.mnt_fsname = mntbuf->f_mntfromname;
        _mntent.mnt_dir = mntbuf->f_mntonname;
        _mntent.mnt_type = mntbuf->f_fstypename;

#ifdef __NetBSD__
        f_flags = mntbuf->f_flag;
#else
        f_flags = mntbuf->f_flags;
#endif
        tmp = flags2opts (f_flags);
        if (tmp) {
                opts_buf[sizeof(opts_buf)-1] = '\0';
                strncpy (opts_buf, tmp, sizeof(opts_buf)-1);
                free (tmp);
        } else {
                *opts_buf = '\0';
        }
        _mntent.mnt_opts = opts_buf;
        _mntent.mnt_freq = _mntent.mnt_passno = 0;
        return (&_mntent);
}

struct mntent *
getmntent (FILE *fp)
{
        gf_statfs_t *mntbuf;

        if (!fp)
                return NULL;

        if (pos == -1 || mntsize == -1)
                mntsize = getmntinfo (&mntbuf, MNT_NOWAIT);

        ++pos;
        if (pos == mntsize) {
                pos = mntsize = -1;
                return (NULL);
        }

        return (statfs_to_mntent (&mntbuf[pos]));
}

/*
  Careful using this function ``buffer`` and ``bufsize`` are
  ignored since there is no stream with strings to populate
  them on OSX or NetBSD, if one wishes to populate them then
  perhaps a new function should be written in this source file
  which uses 'getmntinfo()' to stringify the mntent's
*/

struct mntent *getmntent_r (FILE *fp, struct mntent *result,
                            char *buffer, int bufsize)
{
        struct mntent *ment = NULL;

        if (!fp)
                return NULL;

        flockfile (fp);
        ment = getmntent (fp);
        memcpy (result, ment, sizeof(*ment));
        funlockfile (fp);

        return result;
}

FILE *
setmntent (const char *filename, const char *type)
{
        FILE *fp = NULL;
#ifdef GF_DARWIN_HOST_OS
        fp = fopen (filename, "w");
#else
        fp = fopen (filename, type);
#endif
        return fp;
}

int
endmntent (FILE *fp)
{
        if (fp)
                fclose (fp);

        return 1; /* endmntent() always returns 1 */
}

#endif /* !GF_LINUX_HOST_OS */
