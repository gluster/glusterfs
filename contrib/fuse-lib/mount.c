/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>

  This program can be distributed under the terms of the GNU LGPLv2.
  See the file COPYING.LIB.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#ifndef __NetBSD__
#include <mntent.h>
#endif /* __NetBSD__ */
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/mount.h>

#ifdef __NetBSD__
#include <perfuse.h>
#define umount2(dir, flags) unmount(dir, ((flags) != 0) ? MNT_FORCE : 0)
#endif

#ifdef linux
#define _PATH_MOUNT "/bin/mount"
#else /* NetBSD, MacOS X */
#define _PATH_MOUNT "/sbin/mount"
#endif

#ifdef FUSE_UTIL
#define MALLOC(size) malloc (size)
#define FREE(ptr) free (ptr)
#define GFFUSE_LOGERR(...) fprintf (stderr, ## __VA_ARGS__)
#else /* FUSE_UTIL */
#include "glusterfs.h"
#include "logging.h"
#include "common-utils.h"

#ifdef GF_FUSERMOUNT
#define FUSERMOUNT_PROG FUSERMOUNT_DIR "/fusermount-glusterfs"
#else
#define FUSERMOUNT_PROG "fusermount"
#endif
#define FUSE_COMMFD_ENV "_FUSE_COMMFD"

#define GFFUSE_LOGERR(...) \
        gf_log ("glusterfs-fuse", GF_LOG_ERROR, ## __VA_ARGS__)
#endif /* !FUSE_UTIL */

/*
 * Functions below, until following note, were taken from libfuse
 * (http://git.gluster.com/?p=users/csaba/fuse.git;a=commit;h=b988bbf9)
 * almost verbatim. What has been changed:
 * - style adopted to that of glusterfs
 * - s/fprintf/gf_log/
 * - s/free/FREE/, s/malloc/MALLOC/
 * - there are some other minor things
 */

#ifndef __NetBSD__
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
                if (S_ISLNK (stbuf.st_mode))
                        return 0;

                res = access (_PATH_MOUNTED, W_OK);
                if (res == -1 && errno == EROFS)
                        return 0;
        }

        return 1;
}
#else /* __NetBSD__ */
#define mtab_needs_update(x) 1
#endif /* __NetBSD__ */

#ifndef FUSE_UTIL
static
#endif
int
fuse_mnt_add_mount (const char *progname, const char *fsname,
                    const char *mnt, const char *type, const char *opts,
                    pid_t *mtab_pid)
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

                if (!mtab_pid) {
                        /* mtab update done async, just log if fails */
                        res = fork ();
                        if (res)
                                exit (res == -1 ? 1 : 0);
                        res = fork ();
                        if (res) {
                                if (res != -1) {
                                        if (!(res == waitpid (res, &status, 0)
                                              && status == 0))
                                                GFFUSE_LOGERR ("%s: /etc/mtab "
                                                               "update failed",
                                                               progname);
                                }
                                exit (0);
                        }
                }

                sigprocmask (SIG_SETMASK, &oldmask, NULL);
                setuid (geteuid ());

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
        if (mtab_pid) {
                *mtab_pid = res;
                res = 0;
        } else {
                if (!(res == waitpid (res, &status, 0) && status == 0))
                        res = -1;
        }
        if (res == -1)
                GFFUSE_LOGERR ("%s: waitpid: %s", progname, strerror (errno));

 out_restore:
        sigprocmask (SIG_SETMASK, &oldmask, NULL);
        return res;
}

#ifndef FUSE_UTIL
static
#endif
char
*fuse_mnt_resolve_path (const char *progname, const char *orig)
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

#ifndef FUSE_UTIL
/* return value:
 * >= 0         => fd
 * -1         => error
 */
static int
receive_fd (int fd)
{
        struct msghdr msg;
        struct iovec iov;
        char buf[1];
        int rv;
        size_t ccmsg[CMSG_SPACE (sizeof (int)) / sizeof (size_t)];
        struct cmsghdr *cmsg;
        int *recv_fd;

        iov.iov_base = buf;
        iov.iov_len = 1;

        msg.msg_name = 0;
        msg.msg_namelen = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        /* old BSD implementations should use msg_accrights instead of
         * msg_control; the interface is different. */
        msg.msg_control = ccmsg;
        msg.msg_controllen = sizeof (ccmsg);

        while (((rv = recvmsg (fd, &msg, 0)) == -1) && errno == EINTR);
        if (rv == -1) {
                GFFUSE_LOGERR ("recvmsg failed: %s", strerror (errno));
                return -1;
        }
        if (!rv) {
                /* EOF */
                return -1;
        }

        cmsg = CMSG_FIRSTHDR (&msg);
        /*
         * simplify condition expression
         */
        if (cmsg->cmsg_type != SCM_RIGHTS) {
                GFFUSE_LOGERR ("got control message of unknown type %d",
                               cmsg->cmsg_type);
                return -1;
        }

        recv_fd = (int *) CMSG_DATA (cmsg);
        return (*recv_fd);
}

static int
fuse_mount_fusermount (const char *mountpoint, const char *opts)
{
        int fds[2], pid;
        int res;
        int rv;

        res = socketpair (PF_UNIX, SOCK_STREAM, 0, fds);
        if (res == -1) {
                GFFUSE_LOGERR ("socketpair() failed: %s", strerror (errno));
                return -1;
        }

        pid = fork ();
        if (pid == -1) {
                GFFUSE_LOGERR ("fork() failed: %s", strerror (errno));
                close (fds[0]);
                close (fds[1]);
                return -1;
        }

        if (pid == 0) {
                char env[10];
                const char *argv[32];
                int a = 0;

                argv[a++] = FUSERMOUNT_PROG;
                if (opts) {
                        argv[a++] = "-o";
                        argv[a++] = opts;
                }
                argv[a++] = "--";
                argv[a++] = mountpoint;
                argv[a++] = NULL;

                close (fds[1]);
                fcntl (fds[0], F_SETFD, 0);
                snprintf (env, sizeof (env), "%i", fds[0]);
                setenv (FUSE_COMMFD_ENV, env, 1);
                execvp (FUSERMOUNT_PROG, (char **)argv);
                GFFUSE_LOGERR ("failed to exec fusermount: %s",
                               strerror (errno));
                _exit (1);
        }

        close (fds[0]);
        rv = receive_fd (fds[1]);
        close (fds[1]);
        waitpid (pid, NULL, 0); /* bury zombie */

        return rv;
}
#endif

#ifndef FUSE_UTIL
static
#endif
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
                setuid (geteuid ());
                execl ("/bin/umount", "/bin/umount", "-i", rel_mnt,
                      lazy ? "-l" : NULL, NULL);
                GFFUSE_LOGERR ("%s: failed to execute /bin/umount: %s",
                               progname, strerror (errno));
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

#ifdef FUSE_UTIL
int
fuse_mnt_check_empty (const char *progname, const char *mnt,
                      mode_t rootmode, off_t rootsize)
{
        int isempty = 1;

        if (S_ISDIR (rootmode)) {
                struct dirent *ent;
                DIR *dp = opendir (mnt);
                if (dp == NULL) {
                        fprintf (stderr,
                                 "%s: failed to open mountpoint for reading: %s\n",
                                 progname, strerror (errno));
                        return -1;
                }
                while ((ent = readdir (dp)) != NULL) {
                        if (strcmp (ent->d_name, ".") != 0 &&
                            strcmp (ent->d_name, "..") != 0) {
                                isempty = 0;
                                break;
                        }
                }
                closedir (dp);
        } else if (rootsize)
                isempty = 0;

        if (!isempty) {
                fprintf (stderr, "%s: mountpoint is not empty\n", progname);
                fprintf (stderr, "%s: if you are sure this is safe, "
                         "use the 'nonempty' mount option\n", progname);
                return -1;
        }
        return 0;
}

int
fuse_mnt_check_fuseblk (void)
{
        char buf[256];
        FILE *f = fopen ("/proc/filesystems", "r");
        if (!f)
                return 1;

        while (fgets (buf, sizeof (buf), f))
                if (strstr (buf, "fuseblk\n")) {
                        fclose (f);
                        return 1;
                }

        fclose (f);
        return 0;
}
#endif

#ifndef FUSE_UTIL
void
gf_fuse_unmount (const char *mountpoint, int fd)
{
        int res;
        int pid;

        if (!mountpoint)
                return;

        if (fd != -1) {
                struct pollfd pfd;

                pfd.fd = fd;
                pfd.events = 0;
                res = poll (&pfd, 1, 0);
                /* If file poll returns POLLERR on the device file descriptor,
                   then the filesystem is already unmounted */
                if (res == 1 && (pfd.revents & POLLERR))
                        return;

                /* Need to close file descriptor, otherwise synchronous umount
                   would recurse into filesystem, and deadlock */
                close (fd);
        }

        if (geteuid () == 0) {
                fuse_mnt_umount ("fuse", mountpoint, mountpoint, 1);
                return;
        }

        res = umount2 (mountpoint, 2);
        if (res == 0)
                return;

        pid = fork ();
        if (pid == -1)
                return;

        if (pid == 0) {
                const char *argv[] = { FUSERMOUNT_PROG, "-u", "-q", "-z",
                                       "--", mountpoint, NULL };

                execvp (FUSERMOUNT_PROG, (char **)argv);
                _exit (1);
        }
        waitpid (pid, NULL, 0);
}
#endif

/*
 * Functions below are loosely modelled after similar functions of libfuse
 */

#ifndef FUSE_UTIL
static int
fuse_mount_sys (const char *mountpoint, char *fsname, char *mnt_param, pid_t *mtab_pid)
{
        int fd = -1, ret = -1;
        unsigned mounted = 0;
        char *mnt_param_mnt = NULL;
        char *fstype = "fuse.glusterfs";
        char *source = fsname;

        fd = open ("/dev/fuse", O_RDWR);
        if (fd == -1) {
                GFFUSE_LOGERR ("cannot open /dev/fuse (%s)", strerror (errno));

                return -1;
        }

        ret = asprintf (&mnt_param_mnt,
                        "%s,fd=%i,rootmode=%o,user_id=%i,group_id=%i",
                        mnt_param, fd, S_IFDIR, getuid (), getgid ());
        if (ret == -1) {
                GFFUSE_LOGERR ("Out of memory");

                goto out;
        }
        ret = mount (source, mountpoint, fstype, 0,
                     mnt_param_mnt);
        if (ret == -1 && errno == ENODEV) {
                /* fs subtype support was added by 79c0b2df aka
                   v2.6.21-3159-g79c0b2d. Probably we have an
                   older kernel ... */
                fstype = "fuse";
                ret = asprintf (&source, "glusterfs#%s", fsname);
                if (ret == -1) {
                        GFFUSE_LOGERR ("Out of memory");

                        goto out;
                }
                ret = mount (source, mountpoint, fstype, 0,
                             mnt_param_mnt);
        }
        if (ret == -1)
                goto out;
        else
                mounted = 1;

        if (geteuid () == 0) {
                char *newmnt = fuse_mnt_resolve_path ("fuse", mountpoint);

                if (!newmnt) {
                        ret = -1;

                        goto out;
                }

                ret = fuse_mnt_add_mount ("fuse", source, newmnt, fstype,
                                          mnt_param, mtab_pid);
                FREE (newmnt);
                if (ret == -1) {
                        GFFUSE_LOGERR ("failed to add mtab entry");

                        goto out;
                }
        }

 out:
        if (ret == -1) {
                if (mounted)
                        umount2 (mountpoint, 2); /* lazy umount */
                close (fd);
                fd = -1;
        }
        FREE (mnt_param_mnt);
        if (source != fsname)
                FREE (source);
        return fd;
}

static char *
escape (char *s)
{
        size_t len = 0;
        char *p = NULL;
        char *q = NULL;
        char *e = NULL;

        for (p = s; *p; p++) {
                if (*p == ',')
                       len++;
                len++;
        }

        e = CALLOC (1, len + 1);
        if (!e)
                return NULL;

        for (p = s, q = e; *p; p++, q++) {
                if (*p == ',') {
                        *q = '\\';
                        q++;
                }
                *q = *p;
        }

        return e;
}

int
gf_fuse_mount (const char *mountpoint, char *fsname, char *mnt_param,
               pid_t *mtab_pid)
{
        int fd = -1, rv = -1;
        char *fm_mnt_params = NULL, *p = NULL;
        char *efsname = NULL;

        fd = fuse_mount_sys (mountpoint, fsname, mnt_param, mtab_pid);
        if (fd == -1) {
                gf_log ("glusterfs-fuse", GF_LOG_INFO,
                        "direct mount failed (%s), "
                        "retry to mount via fusermount",
                        strerror (errno));

                efsname = escape (fsname);
                if (!efsname) {
                        GFFUSE_LOGERR ("Out of memory");

                        return -1;
                }
                rv = asprintf (&fm_mnt_params,
                               "%s,fsname=%s,nonempty,subtype=glusterfs",
                               mnt_param, efsname);
                FREE (efsname);
                if (rv == -1) {
                        GFFUSE_LOGERR ("Out of memory");

                        return -1;
                }

                fd = fuse_mount_fusermount (mountpoint, fm_mnt_params);
                if (fd == -1) {
                        p = fm_mnt_params + strlen (fm_mnt_params);
                        while (*--p != ',');
                        *p = '\0';

                        fd = fuse_mount_fusermount (mountpoint, fm_mnt_params);
                }

                FREE (fm_mnt_params);

                if (fd == -1)
                       GFFUSE_LOGERR ("mount failed");
        }

        return fd;
}
#endif
