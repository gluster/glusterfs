/*
   Copyright (c) 2026 GlusterFS contributors <http://www.gluster.org>
   This file is part of GlusterFS.

   Test for issue: https://github.com/gluster/glusterfs/issues/4734
   Replacing a file with an open descriptor via rename() must not break
   fchmod()/fchown()/futimens()/fstat() on the (still open) old descriptor,
   just like on a local filesystem.
*/

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int
fail(const char *what)
{
    fprintf(stderr, "%s failed: %s\n", what, strerror(errno));
    return -1;
}

static int
test_rename_over_open(const char *dir)
{
    int fd = -1;
    struct stat stbuf;
    mode_t mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
    struct timespec ts[2];
    char f1[PATH_MAX], f2[PATH_MAX];

    snprintf(f1, sizeof(f1), "%s/rename-file1", dir);
    snprintf(f2, sizeof(f2), "%s/rename-file2", dir);

    fd = creat(f1, mode);
    if (fd < 0)
        return fail("creat file1");
    close(fd);

    fd = creat(f2, mode);
    if (fd < 0)
        return fail("creat file2");
    close(fd);

    fd = open(f1, O_RDWR);
    if (fd < 0)
        return fail("open file1");

    /* Replace file1 with file2. The old inode is now unlinked, but our
     * fd on it is still open, and fchmod/fchown/futimens must operate
     * on the open inode just like on a local filesystem. */
    if (rename(f2, f1) < 0) {
        close(fd);
        return fail("rename file2 file1");
    }

    if (fchmod(fd, S_IRWXU | S_IRWXG | S_IROTH) < 0) {
        close(fd);
        return fail("fchmod on replaced file descriptor");
    }

    if (fchown(fd, getuid(), getgid()) < 0) {
        close(fd);
        return fail("fchown on replaced file descriptor");
    }

    ts[0].tv_sec = 1000000000; /* a known timestamp */
    ts[0].tv_nsec = 0;
    ts[1].tv_sec = 1000000000;
    ts[1].tv_nsec = 0;
    if (futimens(fd, ts) < 0) {
        close(fd);
        return fail("futimens on replaced file descriptor");
    }

    /* The kernel FUSE attr cache (default timeout 1s) may still serve
     * stale create-time attributes; wait past it so fstat revalidates
     * against the server. */
    sleep(2);

    if (fstat(fd, &stbuf) < 0) {
        close(fd);
        return fail("fstat on replaced file descriptor");
    }

    /* The inode is unlinked while open: no namespace links anymore, and
     * the metadata updates done above must be visible. */
    if (stbuf.st_nlink != 0) {
        fprintf(stderr, "fstat: expected st_nlink 0, got %ld\n",
                (long)stbuf.st_nlink);
        close(fd);
        return -1;
    }

    if (stbuf.st_mtime != 1000000000) {
        fprintf(stderr, "fstat: expected mtime %ld, got %ld\n",
                (long)1000000000, (long)stbuf.st_mtime);
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

/* rmdir() a directory that has an open descriptor; metadata operations
 * on that descriptor must keep working (fd-based path). */
static int
test_rmdir_over_open(const char *dir)
{
    int fd = -1;
    mode_t mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
    char d[PATH_MAX];

    snprintf(d, sizeof(d), "%s/rmdir-dir", dir);

    if (mkdir(d, mode) < 0)
        return fail("mkdir");

    fd = open(d, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return fail("open dir");

    if (rmdir(d) < 0) {
        close(fd);
        return fail("rmdir open dir");
    }

    if (fchmod(fd, S_IRWXU | S_IRWXG | S_IROTH) < 0) {
        close(fd);
        return fail("fchmod on rmdir'd dir descriptor");
    }

    close(fd);
    return 0;
}

int
main(int argc, char **argv)
{
    int status;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <testdir>\n", argv[0]);
        return 1;
    }

    if (chdir(argv[1]) < 0) {
        return fail("chdir");
    }

    status = test_rename_over_open(argv[1]);
    if (status < 0)
        return 1;

    status = test_rmdir_over_open(argv[1]);
    if (status < 0)
        return 1;

    return 0;
}
