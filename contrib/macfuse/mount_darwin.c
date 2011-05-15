/*
 * Derived from mount_bsd.c from the fuse distribution.
 *
 *  FUSE: Filesystem in Userspace
 *  Copyright (C) 2005-2006 Csaba Henk <csaba.henk@creo.hu>
 *  Copyright (C) 2007-2009 Amit Singh <asingh@gmail.com>
 *  Copyright (c) 2010 Gluster, Inc. <http://www.gluster.com>
 *
 *  This program can be distributed under the terms of the GNU LGPLv2.
 *  See the file COPYING.LIB.
 */

#undef _POSIX_C_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/sysctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <paths.h>

#include <libproc.h>
#include <sys/utsname.h>

#include <sys/param.h>
#include <sys/mount.h>
#include <AssertMacros.h>

#include "fuse_param.h"
#include "fuse_ioctl.h"

#include "glusterfs.h"
#include "logging.h"
#include "common-utils.h"

#define GFFUSE_LOGERR(...) \
        gf_log ("glusterfs-fuse", GF_LOG_ERROR, ## __VA_ARGS__)

static long
fuse_os_version_major_np(void)
{
    int ret = 0;
    long major = 0;
    char *c = NULL;
    struct utsname u;
    size_t oldlen;

    oldlen = sizeof(u.release);

    ret = sysctlbyname("kern.osrelease", u.release, &oldlen, NULL, 0);
    if (ret != 0) {
        return -1;
    }

    c = strchr(u.release, '.');
    if (c == NULL) {
        return -1;
    }

    *c = '\0';

    errno = 0;
    major = strtol(u.release, NULL, 10);
    if ((errno == EINVAL) || (errno == ERANGE)) {
        return -1;
    }

    return major;
}

static int
fuse_running_under_rosetta(void)
{
    int result = 0;
    int is_native = 1;
    size_t sz = sizeof(result);

    int ret = sysctlbyname("sysctl.proc_native", &result, &sz, NULL, (size_t)0);
    if ((ret == 0) && !result) {
        is_native = 0;
    }

    return !is_native;
}

static int
loadkmod(void)
{
    int result = -1;
    int pid, terminated_pid;
    union wait status;
    long major;

    major = fuse_os_version_major_np();

    if (major < 9) { /* not Mac OS X 10.5+ */
        return EINVAL;
    }

    pid = fork();

    if (pid == 0) {
        execl(MACFUSE_LOAD_PROG, MACFUSE_LOAD_PROG, NULL);

        /* exec failed */
        exit(ENOENT);
    }

    require_action(pid != -1, Return, result = errno);

    while ((terminated_pid = wait4(pid, (int *)&status, 0, NULL)) < 0) {
        /* retry if EINTR, else break out with error */
        if (errno != EINTR) {
            break;
        }
    }

    if ((terminated_pid == pid) && (WIFEXITED(status))) {
        result = WEXITSTATUS(status);
    } else {
        result = -1;
    }

Return:
    check_noerr_string(result, strerror(errno));

    return result;
}

int
gf_fuse_mount (const char *mountpoint, char *fsname, char *mnt_param,
               pid_t *mtab_pid /* not used on OS X */)
{
    int fd, pid;
    int result;
    char *fdnam, *dev;
    const char *mountprog = MACFUSE_MOUNT_PROG;
    sig_t chldf;

    /* mount_fusefs should not try to spawn the daemon */
    setenv("MOUNT_FUSEFS_SAFE", "1", 1);

    /* to notify mount_fusefs it's called from lib */
    setenv("MOUNT_FUSEFS_CALL_BY_LIB", "1", 1);

    if (!mountpoint) {
        fprintf(stderr, "missing or invalid mount point\n");
        return -1;
    }

    if (fuse_running_under_rosetta()) {
        fprintf(stderr, "MacFUSE does not work under Rosetta\n");
        return -1;
    }

    chldf = signal(SIGCHLD, SIG_DFL); /* So that we can wait4() below. */

    result = loadkmod();
    if (result == EINVAL)
        GFFUSE_LOGERR("OS X >= 10.5 (at least Leopard) required");
    else if (result == 0 || result == ENOENT || result == EBUSY) {
        /* Module loaded, but now need to check for user<->kernel match. */

        char   version[MAXHOSTNAMELEN + 1] = { 0 };
        size_t version_len = MAXHOSTNAMELEN;
        size_t version_len_desired = 0;

        result = sysctlbyname(SYSCTL_MACFUSE_VERSION_NUMBER, version,
                              &version_len, NULL, (size_t)0);
        if (result == 0) {
            /* sysctlbyname() includes the trailing '\0' in version_len */
            version_len_desired = strlen("2.x.y") + 1;

            if (version_len != version_len_desired)
                result = -1;
        } else
            strcpy(version, "?.?.?");
        if (result == 0) {
            char *ep;
            char vstr[4];
            unsigned vval;
            int i;

            for (i = 0; i < 3; i++)
                vstr[i] = version[2*i];
            vstr[3] = '\0';

            vval = strtoul(vstr, &ep, 10);
            if (*ep || vval < 203 || vval > 217)
                result = -1;
            else
                gf_log("glusterfs-fuse", GF_LOG_INFO,
                       "MacFUSE kext version %s", version);
        }
        if (result != 0)
            GFFUSE_LOGERR("MacFUSE version %s is not supported", version);
    } else
        GFFUSE_LOGERR("cannot load MacFUSE kext");
    if (result != 0)
        return -1;

    fdnam = getenv("FUSE_DEV_FD");

    if (fdnam) {
        char *ep;

        fd = strtol(fdnam, &ep, 10);
        if (*ep != '\0' || fd < 0) {
            GFFUSE_LOGERR("invalid value given in FUSE_DEV_FD");
            return -1;
        }

        goto mount;
    }

    dev = getenv("FUSE_DEV_NAME");
    if (dev) {
        if ((fd = open(dev, O_RDWR)) < 0) {
            GFFUSE_LOGERR("failed to open device (%s)", strerror(errno));
            return -1;
        }
    } else {
        int r, devidx = -1;
        char devpath[MAXPATHLEN];

        for (r = 0; r < MACFUSE_NDEVICES; r++) {
            snprintf(devpath, MAXPATHLEN - 1,
                     _PATH_DEV MACFUSE_DEVICE_BASENAME "%d", r);
            fd = open(devpath, O_RDWR);
            if (fd >= 0) {
                dev = devpath;
                devidx = r;
                break;
            }
        }
        if (devidx == -1) {
            GFFUSE_LOGERR("failed to open device (%s)", strerror(errno));
            return -1;
        }
    }

mount:
    if (getenv("FUSE_NO_MOUNT") || ! mountpoint)
        goto out;

    signal(SIGCHLD, chldf);

    pid = fork();

    if (pid == -1) {
        GFFUSE_LOGERR("fork() failed (%s)", strerror(errno));
        close(fd);
        return -1;
    }

    if (pid == 0) {

        pid = fork();
        if (pid == -1) {
            GFFUSE_LOGERR("fork() failed (%s)", strerror(errno));
            close(fd);
            exit(1);
        }

        if (pid == 0) {
            const char *argv[32];
            int a = 0;
            char *opts = NULL;

            if (asprintf(&opts, "%s,fssubtype=glusterfs", mnt_param) == -1) {
                GFFUSE_LOGERR("Out of memory");
		exit(1);
            }

            if (! fdnam)
                asprintf(&fdnam, "%d", fd);

            argv[a++] = mountprog;
            if (opts) {
                argv[a++] = "-o";
                argv[a++] = opts;
            }
            argv[a++] = fdnam;
            argv[a++] = mountpoint;
            argv[a++] = NULL;

            {
                char title[MAXPATHLEN + 1] = { 0 };
                u_int32_t len = MAXPATHLEN;
                int ret = proc_pidpath(getpid(), title, len);
                if (ret) {
                    setenv("MOUNT_FUSEFS_DAEMON_PATH", title, 1);
                }
            }
            execvp(mountprog, (char **) argv);
            GFFUSE_LOGERR("MacFUSE: failed to exec mount program (%s)", strerror(errno));
            exit(1);
        }

        _exit(0);
    }

out:
    return fd;
}

void
gf_fuse_unmount(const char *mountpoint, int fd)
{
    int ret;
    struct stat sbuf;
    char dev[128];
    char resolved_path[PATH_MAX];
    char *ep, *rp = NULL;

    unsigned int hs_complete = 0;

    ret = ioctl(fd, FUSEDEVIOCGETHANDSHAKECOMPLETE, &hs_complete);
    if (ret || !hs_complete) {
        return;
    }
    /* XXX does this have any use here? */
    ret = ioctl(fd,  FUSEDEVIOCSETDAEMONDEAD, &fd);
    if (ret) {
        return;
    }

    if (fstat(fd, &sbuf) == -1) {
        return;
    }

    devname_r(sbuf.st_rdev, S_IFCHR, dev, 128);

    if (strncmp(dev, MACFUSE_DEVICE_BASENAME,
                sizeof(MACFUSE_DEVICE_BASENAME) - 1)) {
        return;
    }

    strtol(dev + 4, &ep, 10);
    if (*ep != '\0') {
        return;
    }

    rp = realpath(mountpoint, resolved_path);
    if (rp) {
        ret = unmount(resolved_path, 0);
    }

    close(fd);

    return;
}
