/*
 *  Derived from mount_bsd.c from the fuse distribution.
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

int
gf_fuse_mount (const char *mountpoint, char *fsname,
               unsigned long mountflags, char *mnt_param,
               pid_t *mnt_pid, int status_fd) /* Not used on OS X */
{
        int fd       = 0;
        int pid      = 0;
        int ret      = 0;
        char *fdnam  = NULL;
        char *dev    = NULL;
        char vstr[4];
        unsigned vval = 0;
        int i        = 0;

        const char *mountprog              = OSXFUSE_MOUNT_PROG;
        sig_t chldf                        = SIG_ERR;
        char   version[MAXHOSTNAMELEN + 1] = { 0 };
        size_t version_len                 = MAXHOSTNAMELEN;
        size_t version_len_desired         = 0;
        int r                              = 0;
        char devpath[MAXPATHLEN]           = { 0 };;

        if (!mountpoint) {
                gf_log ("glustefs-fuse", GF_LOG_ERROR,
                        "missing or invalid mount point");
                goto err;
        }

        /* mount_fusefs should not try to spawn the daemon */
        setenv("MOUNT_FUSEFS_SAFE", "1", 1);

        /* to notify mount_fusefs it's called from lib */
        setenv("MOUNT_FUSEFS_CALL_BY_LIB", "1", 1);

        chldf = signal(SIGCHLD, SIG_DFL); /* So that we can wait4() below. */

        if (chldf == SIG_ERR) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "signal() returned SIG_ERR: %s",
                        strerror(errno));
                goto err;
        }

        /* check for user<->kernel match. */
        ret = sysctlbyname(SYSCTL_OSXFUSE_VERSION_NUMBER, version,
                              &version_len, NULL, (size_t)0);
        if (ret != 0) {
                gf_log ("glustefs-fuse", GF_LOG_ERROR,
                        "sysctlbyname() returned error: %s",
                        strerror(errno));
                goto err;
        }

        /* sysctlbyname() includes the trailing '\0' in version_len */
        version_len_desired = strlen("2.x.y") + 1;

        if (version_len != version_len_desired) {
                gf_log ("glusterfs-fuse", GF_LOG_ERROR,
                        "version length mismatch for OSXFUSE %s",
                        version);
                ret = -1;
                goto err;
        }

        for (i = 0; i < 3; i++)
                vstr[i] = version[2*i];
        vstr[3] = '\0';

        vval = strtoul(vstr, NULL, 10);
        if (vval < 264) {
                GFFUSE_LOGERR("OSXFUSE version %s is not supported", version);
                ret = -1;
                goto err;
        }

        gf_log("glusterfs-fuse", GF_LOG_INFO,
               "OSXFUSE kext version supported %s", version);

        fdnam = getenv("FUSE_DEV_FD");
        if (fdnam) {
                fd = strtol(fdnam, NULL, 10);
                if (fd < 0) {
                        GFFUSE_LOGERR("invalid value given in FUSE_DEV_FD");
                        ret = -1;
                        goto err;
                }
                goto mount;
        }

        dev = getenv("FUSE_DEV_NAME");
        if (!dev) {
                for (r = 0; r < OSXFUSE_NDEVICES; r++) {
                        snprintf(devpath, MAXPATHLEN - 1,
                                 _PATH_DEV OSXFUSE_DEVICE_BASENAME "%d", r);
                        if ((fd = open(devpath, O_RDWR)) < 0) {
                                GFFUSE_LOGERR("failed to open device %s (%s)",
                                              devpath,
                                              strerror(errno));
                                goto err;
                        }
                        dev = devpath;
                        goto mount;
                }
        }

        fd = open(dev, O_RDWR);
        if (fd < 0) {
                GFFUSE_LOGERR("failed to open device %s (%s)", dev,
                              strerror(errno));
                ret = -1;
                goto err;
        }

mount:
        signal(SIGCHLD, chldf);

        pid = fork();
        if (pid == -1) {
                GFFUSE_LOGERR("fork() failed (%s)", strerror(errno));
                ret = -1;
                goto err;
        }

        if (pid == 0) {
                pid = fork();
                if (pid == -1) {
                        GFFUSE_LOGERR("fork() failed (%s)", strerror(errno));
                        ret = -1;
                        goto err;
                }

                if (pid == 0) {
                        const char *argv[32];
                        int a = 0;
                        char *opts = NULL;

                        if (asprintf(&opts, "%s,fssubtype=glusterfs",
                                     mnt_param) == -1) {
                                GFFUSE_LOGERR("asprintf() error: %s",
                                              strerror(errno));
                                ret = -1;
                                goto err;
                        }

                        if (!fdnam)
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
                                        setenv("MOUNT_FUSEFS_DAEMON_PATH",
                                               title, 1);
                                }
                        }
                        execvp(mountprog, (char **) argv);
                        GFFUSE_LOGERR("OSXFUSE: failed to exec mount"
                                      " program (%s)", strerror(errno));
                        _exit(1);
                }
                _exit(0);
        }
        ret = fd;
err:
        if (ret == -1) {
                if (fd > 0) {
                        close(fd);
                }
        }
        return ret;
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

        if (fstat(fd, &sbuf) == -1) {
                return;
        }

        devname_r(sbuf.st_rdev, S_IFCHR, dev, 128);

        if (strncmp(dev, OSXFUSE_DEVICE_BASENAME,
                    sizeof(OSXFUSE_DEVICE_BASENAME) - 1)) {
                return;
        }

        strtol(dev + sizeof(OSXFUSE_DEVICE_BASENAME) - 1, &ep, 10);
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
