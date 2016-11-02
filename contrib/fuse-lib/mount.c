/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>

  This program can be distributed under the terms of the GNU LGPLv2.
  See the file COPYING.LIB.
*/

#include "mount_util.h"
#include "mount-gluster-compat.h"

#ifdef GF_FUSERMOUNT
#define FUSERMOUNT_PROG FUSERMOUNT_DIR "/fusermount-glusterfs"
#else
#define FUSERMOUNT_PROG "fusermount"
#endif
#define FUSE_DEVFD_ENV "_FUSE_DEVFD"

#ifdef __FreeBSD__
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#endif /* __FreeBSD__ */

/* FUSE: function is called fuse_kern_unmount() */
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


/* gluster-specific routines */

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

static int
fuse_mount_fusermount (const char *mountpoint, char *fsname,
                       unsigned long mountflags, char *mnt_param,
                       int fd)
{
        int  pid = -1;
        int  res = 0;
        int  ret = -1;
        char *fm_mnt_params = NULL;
        char *efsname = NULL;

#ifndef GF_FUSERMOUNT
        GFFUSE_LOGERR ("Mounting via helper utility "
                       "(unprivileged mounting) is supported "
                       "only if glusterfs is compiled with "
                       "--enable-fusermount");
        return -1;
#endif

        efsname = escape (fsname);
        if (!efsname) {
                GFFUSE_LOGERR ("Out of memory");

                return -1;
        }
        ret = asprintf (&fm_mnt_params,
                        "%s%s,fsname=%s,nonempty,subtype=glusterfs",
                        (mountflags & MS_RDONLY) ? "ro," : "",
                        mnt_param, efsname);
        FREE (efsname);
        if (ret == -1) {
                GFFUSE_LOGERR ("Out of memory");

                goto out;
        }

        /* fork to exec fusermount */
        pid = fork ();
        if (pid == -1) {
                GFFUSE_LOGERR ("fork() failed: %s", strerror (errno));
                ret = -1;
                goto out;
        }

        if (pid == 0) {
                char env[10];
                const char *argv[32];
                int a = 0;

                argv[a++] = FUSERMOUNT_PROG;
                argv[a++] = "-o";
                argv[a++] = fm_mnt_params;
                argv[a++] = "--";
                argv[a++] = mountpoint;
                argv[a++] = NULL;

                snprintf (env, sizeof (env), "%i", fd);
                setenv (FUSE_DEVFD_ENV, env, 1);
                execvp (FUSERMOUNT_PROG, (char **)argv);
                GFFUSE_LOGERR ("failed to exec fusermount: %s",
                               strerror (errno));
                _exit (1);
        }

        ret = waitpid (pid, &res, 0);
        ret = (ret == pid && res == 0) ? 0 : -1;
 out:
        FREE (fm_mnt_params);
        return ret;
}

#if defined(__FreeBSD__)
void
build_iovec(struct iovec **iov, int *iovlen, const char *name, void *val,
            size_t len)
{
        int i;
        if (*iovlen < 0)
                return;
        i = *iovlen;

        *iov = realloc(*iov, sizeof **iov * (i + 2));
        if (*iov == NULL) {
                *iovlen = -1;
                return;
        }

        (*iov)[i].iov_base = strdup(name);
        (*iov)[i].iov_len = strlen(name) + 1;

        i++;
        (*iov)[i].iov_base = val;
        if (len == (size_t) -1) {
                if (val != NULL)
                        len = strlen(val) + 1;
                else
                        len = 0;
        }
        (*iov)[i].iov_len = (int)len;
        *iovlen = ++i;
}

/*
 * This function is needed for compatibility with parameters
 * which used to use the mount_argf() command for the old mount() syscall.
 */
void
build_iovec_argf(struct iovec **iov, int *iovlen, const char *name,
                 const char *fmt, ...)
{
        va_list ap;
        char val[255] = { 0 };

        va_start(ap, fmt);
        vsnprintf(val, sizeof(val), fmt, ap);
        va_end(ap);
        build_iovec(iov, iovlen, name, strdup(val), (size_t)-1);
}
#endif /* __FreeBSD__ */

static int
fuse_mount_sys (const char *mountpoint, char *fsname,
                unsigned long mountflags, char *mnt_param, int fd)
{
        int ret = -1;
        unsigned mounted = 0;
        char *mnt_param_mnt = NULL;
        char *fstype = "fuse.glusterfs";
        char *source = fsname;

        ret = asprintf (&mnt_param_mnt,
                        "%s,fd=%i,rootmode=%o,user_id=%i,group_id=%i",
                        mnt_param, fd, S_IFDIR, getuid (), getgid ());
        if (ret == -1) {
                GFFUSE_LOGERR ("Out of memory");

                goto out;
        }

#ifdef __FreeBSD__
        struct iovec *iov = NULL;
        int iovlen = 0;
        char fdstr[15];
        sprintf (fdstr, "%d", fd);

        build_iovec (&iov, &iovlen, "fstype", "fusefs", -1);
        build_iovec (&iov, &iovlen, "subtype", "glusterfs", -1);
        build_iovec (&iov, &iovlen, "fspath", __DECONST(void *, mountpoint),
                     -1);
        build_iovec (&iov, &iovlen, "from", "/dev/fuse", -1);
        build_iovec (&iov, &iovlen, "volname", source, -1);
        build_iovec (&iov, &iovlen, "fd", fdstr, -1);
        ret = nmount (iov, iovlen, mountflags);
#else
        ret = mount (source, mountpoint, fstype, mountflags,
                     mnt_param_mnt);
#endif /* __FreeBSD__ */
#ifdef GF_LINUX_HOST_OS
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
                ret = mount (source, mountpoint, fstype, mountflags,
                             mnt_param_mnt);
        }
#endif /* GF_LINUX_HOST_OS */
        if (ret == -1)
                goto out;
        else
                mounted = 1;

#ifdef GF_LINUX_HOST_OS
        if (geteuid () == 0) {
                char *newmnt = fuse_mnt_resolve_path ("fuse", mountpoint);
                char *mnt_param_mtab = NULL;

                if (!newmnt) {
                        ret = -1;

                        goto out;
                }

                ret = asprintf (&mnt_param_mtab, "%s%s",
                                mountflags & MS_RDONLY ? "ro," : "",
                                mnt_param);
                if (ret == -1)
                        GFFUSE_LOGERR ("Out of memory");
                else {
                        ret = fuse_mnt_add_mount ("fuse", source, newmnt,
                                                  fstype, mnt_param_mtab);
                        FREE (mnt_param_mtab);
                }

                FREE (newmnt);
                if (ret == -1) {
                        GFFUSE_LOGERR ("failed to add mtab entry");

                        goto out;
                }
        }
#endif /* GF_LINUX_HOST_OS */

out:
        if (ret == -1) {
                GFFUSE_LOGERR("ret = -1\n");
                if (mounted)
                        umount2 (mountpoint, 2); /* lazy umount */
        }
        FREE (mnt_param_mnt);
        if (source != fsname)
                FREE (source);

        return ret;
}

int
gf_fuse_mount (const char *mountpoint, char *fsname,
               unsigned long mountflags, char *mnt_param,
               pid_t *mnt_pid, int status_fd)
{
        int   fd  = -1;
        pid_t pid = -1;
        int   ret = -1;

        fd = open ("/dev/fuse", O_RDWR);
        if (fd == -1) {
                GFFUSE_LOGERR ("cannot open /dev/fuse (%s)",
                                strerror (errno));
                return -1;
        }

        /* start mount agent */
        pid = fork();
        switch (pid) {
        case 0:
                /* hello it's mount agent */
                if (!mnt_pid) {
                        /* daemonize mount agent, caller is
                         * not interested in waiting for it
                         */
                        pid = fork ();
                        if (pid)
                                exit (pid == -1 ? 1 : 0);
                }

                ret = fuse_mount_sys (mountpoint, fsname, mountflags, mnt_param,
                                      fd);
                if (ret == -1) {
                        gf_log ("glusterfs-fuse", GF_LOG_INFO,
                                "direct mount failed (%s) errno %d",
                                strerror (errno), errno);

                        if (errno == EPERM) {
                                gf_log ("glusterfs-fuse", GF_LOG_INFO,
                                        "retry to mount via fusermount");

                                ret = fuse_mount_fusermount (mountpoint, fsname,
                                                             mountflags,
                                                             mnt_param, fd);
                        }
                }

                if (ret == -1)
                        GFFUSE_LOGERR ("mount of %s to %s (%s) failed",
                                       fsname, mountpoint, mnt_param);

                if (status_fd >= 0)
                        (void)write (status_fd, &ret, sizeof (ret));
                exit (!!ret);
                /* bye mount agent */
        case -1:
                close (fd);
                fd = -1;
        }

        if (mnt_pid)
               *mnt_pid = pid;

        return fd;
}
