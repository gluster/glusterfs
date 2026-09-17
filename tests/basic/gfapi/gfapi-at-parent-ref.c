/*
 * gfapi-at-parent-ref.c -- a failing glfs_*at() call must not release the
 * caller's reference on the parent directory handle.
 *
 * Regression test for the double GF_REF_PUT of the parent glfd in the
 * glfs_*at() family: setup_fopat_args() released the reference it took on
 * any failure other than ENOENT, and every caller released it once more at
 * its out: label, so a single call whose lookup failed (ENOTDIR here; EACCES,
 * ENOTCONN, ESTALE in production) -- or a renameat()/renameat2() whose
 * source does not exist -- freed a directory handle the caller still owned. The
 * next use of that handle read freed memory: EBADF at best, SIGSEGV once the
 * block had been recycled.
 *
 * Usage: gfapi-at-parent-ref <host> <volume> <logfile>
 * Exit status 0 iff the parent handle survives every failing call.
 */

#include <glusterfs/api/glfs.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define PARENT "/gfapi-at-parent-ref"
#define CHILD "afile"

static int
parent_alive(glfs_fd_t *pglfd, const char *after)
{
    struct stat st;
    int ret;

    errno = 0;
    ret = glfs_fstatat(pglfd, CHILD, &st, 0);
    if (ret != 0) {
        fprintf(stderr, "parent handle dead after %s: fstatat -> %d (%s)\n",
                after, ret, strerror(errno));
        return 0;
    }
    printf("parent handle alive after %s\n", after);
    return 1;
}

int
main(int argc, char **argv)
{
    glfs_t *fs = NULL;
    glfs_fd_t *fd = NULL;
    glfs_fd_t *pglfd = NULL;
    struct stat st;
    int ret;

    if (argc != 4) {
        fprintf(stderr, "usage: %s <host> <volume> <logfile>\n", argv[0]);
        return 1;
    }

    fs = glfs_new(argv[2]);
    if (!fs)
        return 1;
    if (glfs_set_volfile_server(fs, "tcp", argv[1], 24007))
        return 1;
    if (glfs_set_logging(fs, argv[3], 7))
        return 1;
    if (glfs_init(fs)) {
        fprintf(stderr, "glfs_init failed: %s\n", strerror(errno));
        return 1;
    }

    if (glfs_mkdir(fs, PARENT, 0755) && errno != EEXIST) {
        fprintf(stderr, "mkdir failed: %s\n", strerror(errno));
        return 1;
    }
    fd = glfs_creat(fs, PARENT "/" CHILD, O_CREAT | O_RDWR, 0644);
    if (!fd) {
        fprintf(stderr, "creat failed: %s\n", strerror(errno));
        return 1;
    }
    glfs_close(fd);

    /* one reference on the parent directory handle, ours */
    pglfd = glfs_opendir(fs, PARENT);
    if (!pglfd) {
        fprintf(stderr, "opendir failed: %s\n", strerror(errno));
        return 1;
    }

    /* 1. openat through a regular file: fails with ENOTDIR */
    errno = 0;
    fd = glfs_openat(pglfd, CHILD "/x", O_RDONLY, 0);
    if (fd || errno != ENOTDIR) {
        fprintf(stderr, "openat(%s/x): expected ENOTDIR, got %p %s\n", CHILD,
                (void *)fd, strerror(errno));
        return 1;
    }
    if (!parent_alive(pglfd, "openat ENOTDIR"))
        return 1;

    /* 2. fstatat through a regular file: fails with ENOTDIR */
    errno = 0;
    ret = glfs_fstatat(pglfd, CHILD "/x", &st, 0);
    if (ret == 0 || errno != ENOTDIR) {
        fprintf(stderr, "fstatat(%s/x): expected ENOTDIR, got %d %s\n", CHILD,
                ret, strerror(errno));
        return 1;
    }
    if (!parent_alive(pglfd, "fstatat ENOTDIR"))
        return 1;

    /* 3. renameat of a missing source: fails with ENOENT before the
     *    destination parent is ever set up */
    errno = 0;
    ret = glfs_renameat(pglfd, "does-not-exist", pglfd, "renamed");
    if (ret == 0 || errno != ENOENT) {
        fprintf(stderr, "renameat(missing): expected ENOENT, got %d %s\n", ret,
                strerror(errno));
        return 1;
    }
    if (!parent_alive(pglfd, "renameat ENOENT"))
        return 1;

    /* 4. the control: a missing name in openat is the one failure the
     *    library handled correctly all along */
    errno = 0;
    fd = glfs_openat(pglfd, "does-not-exist", O_RDONLY, 0);
    if (fd || errno != ENOENT) {
        fprintf(stderr, "openat(missing): expected ENOENT, got %p %s\n",
                (void *)fd, strerror(errno));
        return 1;
    }
    if (!parent_alive(pglfd, "openat ENOENT"))
        return 1;

    /* renameat2 with a missing source: the flags variant shares the same
     *    helper pair and the same destination-parent release */
    errno = 0;
    ret = glfs_renameat2(pglfd, "does-not-exist", pglfd, "renamed", 0);
    if (ret == 0 || errno != ENOENT) {
        fprintf(stderr, "renameat2(missing): expected ENOENT, got %d %s\n", ret,
                strerror(errno));
        return 1;
    }
    if (!parent_alive(pglfd, "renameat2 ENOENT"))
        return 1;

    /* our one reference is still the only one: this must succeed */
    errno = 0;
    ret = glfs_closedir(pglfd);
    if (ret != 0) {
        fprintf(stderr, "closedir of the parent handle -> %d (%s)\n", ret,
                strerror(errno));
        return 1;
    }

    glfs_unlink(fs, PARENT "/" CHILD);
    glfs_rmdir(fs, PARENT);
    glfs_fini(fs);
    return 0;
}
