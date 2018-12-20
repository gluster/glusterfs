/*
 Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>
#include <string.h>
#include <time.h>
#include <libgen.h>

static void
cleanup(glfs_t *fs)
{
    if (!fs)
        return;
#if 0
        /* glfs fini path is still racy and crashing the program. Since
         * this program any way has to die, we are not going to call fini
         * in the released versions. i.e. final builds. For all
         * internal testing lets enable this so that glfs_fini code
         * path becomes stable. */
        glfs_fini (fs);
#endif
}

int
main(int argc, char **argv)
{
    glfs_t *fs = NULL;
    int ret = -1;
    char *volname = NULL;
    char *logfilepath = NULL;
    char *path_src = NULL;
    char *path_dst = NULL;
    glfs_fd_t *glfd_in = NULL;
    glfs_fd_t *glfd_out = NULL;
    char *volfile_server = NULL;

    struct stat stbuf = {
        0,
    };
    struct glfs_stat stat_src = {
        0,
    };
    struct glfs_stat prestat_dst = {
        0,
    };
    struct glfs_stat poststat_dst = {
        0,
    };
    size_t len;

    if (argc < 6) {
        printf("%s <volume> <log file path> <source> <destination>", argv[0]);
        ret = -1;
        goto out;
    }

    volfile_server = argv[1];
    volname = argv[2];
    logfilepath = argv[3];
    path_src = argv[4];
    path_dst = argv[5];

    if (path_src[0] != '/') {
        fprintf(stderr, "source path %s is not absolute", path_src);
        errno = EINVAL;
        goto out;
    }

    if (path_dst[0] != '/') {
        fprintf(stderr, "destination path %s is not absolute", path_dst);
        errno = EINVAL;
        goto out;
    }

    fs = glfs_new(volname);
    if (!fs) {
        ret = -errno;
        fprintf(stderr, "Not able to initialize volume '%s'", volname);
        goto out;
    }

    ret = glfs_set_volfile_server(fs, "tcp", volfile_server, 24007);
    if (ret < 0) {
        ret = -errno;
        fprintf(stderr,
                "Failed to set the volfile server, "
                "%s",
                strerror(errno));
        goto out;
    }

    ret = glfs_set_logging(fs, logfilepath, 7);
    if (ret < 0) {
        ret = -errno;
        fprintf(stderr,
                "Failed to set the log file path, "
                "%s",
                strerror(errno));
        goto out;
    }

    ret = glfs_init(fs);
    if (ret < 0) {
        ret = -errno;
        if (errno == ENOENT) {
            fprintf(stderr, "Volume %s does not exist", volname);
        } else {
            fprintf(stderr,
                    "%s: Not able to fetch "
                    "volfile from glusterd",
                    volname);
        }
        goto out;
    }

    glfd_in = glfs_open(fs, path_src, O_RDONLY | O_NONBLOCK);
    if (!glfd_in) {
        ret = -errno;
        goto out;
    } else {
        printf("OPEN_SRC: opening %s is success\n", path_src);
    }

    glfd_out = glfs_creat(fs, path_dst, O_RDWR, 0644);
    if (!glfd_out) {
        fprintf(stderr,
                "FAILED_DST_OPEN: failed to "
                "open (create) %s (%s)\n",
                path_dst, strerror(errno));
        ret = -errno;
        goto out;
    } else {
        printf("OPEN_DST: opening %s is success\n", path_dst);
    }

    ret = glfs_fstat(glfd_in, &stbuf);
    if (ret < 0) {
        ret = -errno;
        goto out;
    } else {
        printf("FSTAT_SRC: fstat on %s is success\n", path_dst);
    }

    len = stbuf.st_size;

    do {
        ret = glfs_copy_file_range(glfd_in, NULL, glfd_out, NULL, len, 0,
                                   &stat_src, &prestat_dst, &poststat_dst);
        if (ret == -1) {
            fprintf(stderr, "copy_file_range failed with %s\n",
                    strerror(errno));
            ret = -errno;
            break;
        } else {
            printf("copy_file_range successful\n");
            len -= ret;
        }
    } while (len > 0);

out:
    if (glfd_in)
        glfs_close(glfd_in);
    if (glfd_out)
        glfs_close(glfd_out);

    cleanup(fs);

    return ret;
}
