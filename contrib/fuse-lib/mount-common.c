/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>

  This program can be distributed under the terms of the GNU LGPLv2.
  See the file COPYING.LIB.
*/

#include "mount-gluster-compat.h"

/*
 * These functions (and gf_fuse_umount() in mount.c)
 * were originally taken from libfuse as of commit 7960e99e
 * (http://fuse.git.sourceforge.net/git/gitweb.cgi?p=fuse/fuse;a=commit;h=7960e99e)
 * almost verbatim. What has been changed upon adoption:
 * - style adopted to that of glusterfs
 * - s/fprintf/gf_log/
 * - s/free/FREE/, s/malloc/MALLOC/
 * - there are some other minor things
 *
 * For changes that were made later and syncs with upstream,
 * see the commit log and per-function comments.
 */

#ifdef GF_LINUX_HOST_OS
/* FUSE: cherry-picked bd99f9cf */
static int
mtab_needs_update (const char *mnt)
{
        int res;
        struct stat stbuf;

        /* If mtab is within new mount, don't touch it */
        if (strncmp (mnt, _PATH_MOUNTED, strlen (mnt)) == 0 &&
            _PATH_MOUNTED[strlen (mnt)] == '/')
                return 0;

        /*
         * Skip mtab update if /etc/mtab:
         *
         *  - doesn't exist,
         *  - is a symlink,
         *  - is on a read-only filesystem.
         */
        res = lstat (_PATH_MOUNTED, &stbuf);
        if (res == -1) {
                if (errno == ENOENT)
                        return 0;
        } else {
                uid_t ruid;
                int err;

                if (S_ISLNK (stbuf.st_mode))
                        return 0;

                ruid = getuid ();
                if (ruid != 0)
                        setreuid (0, -1);

                res = access (_PATH_MOUNTED, W_OK);
                err = (res == -1) ? errno : 0;
                if (ruid != 0)
                        setreuid (ruid, -1);

                if (err == EROFS)
                        return 0;
        }

        return 1;
}
#else /* GF_LINUX_HOST_OS */
#define mtab_needs_update(x) 1
#endif /* GF_LINUX_HOST_OS */

/* FUSE: called add_mount_legacy(); R.I.P. as of cbd3a2a8 */
int
fuse_mnt_add_mount (const char *progname, const char *fsname,
                    const char *mnt, const char *type, const char *opts)
{
        int res;
        int status;
        sigset_t blockmask;
        sigset_t oldmask;

        if (!mtab_needs_update (mnt))
                return 0;

        sigemptyset (&blockmask);
        sigaddset (&blockmask, SIGCHLD);
        res = sigprocmask (SIG_BLOCK, &blockmask, &oldmask);
        if (res == -1) {
                GFFUSE_LOGERR ("%s: sigprocmask: %s",
                               progname, strerror (errno));
                return -1;
        }

        res = fork ();
        if (res == -1) {
                GFFUSE_LOGERR ("%s: fork: %s", progname, strerror (errno));
                goto out_restore;
        }
        if (res == 0) {
                char templ[] = "/tmp/fusermountXXXXXX";
                char *tmp;

                sigprocmask (SIG_SETMASK, &oldmask, NULL);
                res = setuid (geteuid ());
                if (res != 0) {
                        GFFUSE_LOGERR ("%s: setuid: %s", progname, strerror (errno));
                        exit (1);
                }

                /*
                 * hide in a directory, where mount isn't able to resolve
                 * fsname as a valid path
                 */
                tmp = mkdtemp (templ);
                if (!tmp) {
                        GFFUSE_LOGERR ("%s: failed to create temporary directory",
                                       progname);
                        exit (1);
                }
                if (chdir (tmp)) {
                        GFFUSE_LOGERR ("%s: failed to chdir to %s: %s",
                                       progname, tmp, strerror (errno));
                        exit (1);
                }
                rmdir (tmp);
                execl (_PATH_MOUNT, _PATH_MOUNT, "-i", "-f", "-t", type,
                       "-o", opts, fsname, mnt, NULL);
                GFFUSE_LOGERR ("%s: failed to execute %s: %s",
                               progname, _PATH_MOUNT, strerror (errno));
                exit (1);
        }

        res = waitpid (res, &status, 0);
        if (res == -1)
                GFFUSE_LOGERR ("%s: waitpid: %s", progname, strerror (errno));
        res = (res != -1 && status == 0) ? 0 : -1;

 out_restore:
        sigprocmask (SIG_SETMASK, &oldmask, NULL);
        return res;
}

char *
fuse_mnt_resolve_path (const char *progname, const char *orig)
{
        char buf[PATH_MAX];
        char *copy;
        char *dst;
        char *end;
        char *lastcomp;
        const char *toresolv;

        if (!orig[0]) {
                GFFUSE_LOGERR ("%s: invalid mountpoint '%s'", progname, orig);
                return NULL;
        }

        copy = strdup (orig);
        if (copy == NULL) {
                GFFUSE_LOGERR ("%s: failed to allocate memory", progname);
                return NULL;
        }

        toresolv = copy;
        lastcomp = NULL;
        for (end = copy + strlen (copy) - 1; end > copy && *end == '/'; end --);
        if (end[0] != '/') {
                char *tmp;
                end[1] = '\0';
                tmp = strrchr (copy, '/');
                if (tmp == NULL) {
                        lastcomp = copy;
                        toresolv = ".";
                } else {
                        lastcomp = tmp + 1;
                        if (tmp == copy)
                                toresolv = "/";
                }
                if (strcmp (lastcomp, ".") == 0 || strcmp (lastcomp, "..") == 0) {
                        lastcomp = NULL;
                        toresolv = copy;
                }
                else if (tmp)
                        tmp[0] = '\0';
        }
        if (realpath (toresolv, buf) == NULL) {
                GFFUSE_LOGERR ("%s: bad mount point %s: %s", progname, orig,
                               strerror (errno));
                FREE (copy);
                return NULL;
        }
        if (lastcomp == NULL)
                dst = strdup (buf);
        else {
                dst = (char *) MALLOC (strlen (buf) + 1 + strlen (lastcomp) + 1);
                if (dst) {
                        unsigned buflen = strlen (buf);
                        if (buflen && buf[buflen-1] == '/')
                                sprintf (dst, "%s%s", buf, lastcomp);
                        else
                                sprintf (dst, "%s/%s", buf, lastcomp);
                }
        }
        FREE (copy);
        if (dst == NULL)
                GFFUSE_LOGERR ("%s: failed to allocate memory", progname);
        return dst;
}

/* FUSE: to support some changes that were reverted since
 * then, it was split in two (fuse_mnt_umount() and
 * exec_umount()); however the actual code is same as here
 * since 0197ce40
 */
int
fuse_mnt_umount (const char *progname, const char *abs_mnt,
                 const char *rel_mnt, int lazy)
{
        int res;
        int status;
        sigset_t blockmask;
        sigset_t oldmask;

        if (!mtab_needs_update (abs_mnt)) {
                res = umount2 (rel_mnt, lazy ? 2 : 0);
                if (res == -1)
                        GFFUSE_LOGERR ("%s: failed to unmount %s: %s",
                                       progname, abs_mnt, strerror (errno));
                return res;
        }

        sigemptyset (&blockmask);
        sigaddset (&blockmask, SIGCHLD);
        res = sigprocmask (SIG_BLOCK, &blockmask, &oldmask);
        if (res == -1) {
                GFFUSE_LOGERR ("%s: sigprocmask: %s", progname,
                               strerror (errno));
                return -1;
        }

        res = fork ();
        if (res == -1) {
                GFFUSE_LOGERR ("%s: fork: %s", progname, strerror (errno));
                goto out_restore;
        }
        if (res == 0) {
                sigprocmask (SIG_SETMASK, &oldmask, NULL);
                res = setuid (geteuid ());
                if (res != 0) {
                        GFFUSE_LOGERR ("%s: setuid: %s", progname, strerror (errno));
                        exit (1);
                }
#ifdef GF_LINUX_HOST_OS
                execl ("/bin/umount", "/bin/umount", "-i", rel_mnt,
                       lazy ? "-l" : NULL, NULL);
                GFFUSE_LOGERR ("%s: failed to execute /bin/umount: %s",
                               progname, strerror (errno));
#elif __NetBSD__
                /* exitting the filesystem causes the umount */
                exit (0);
#else
                execl ("/sbin/umount", "/sbin/umount", "-f", rel_mnt, NULL);
                GFFUSE_LOGERR ("%s: failed to execute /sbin/umount: %s",
                               progname, strerror (errno));
#endif /* GF_LINUX_HOST_OS */
                exit (1);
        }
        res = waitpid (res, &status, 0);
        if (res == -1)
                GFFUSE_LOGERR ("%s: waitpid: %s", progname, strerror (errno));

        if (status != 0)
                res = -1;

 out_restore:
        sigprocmask (SIG_SETMASK, &oldmask, NULL);
        return res;
}
