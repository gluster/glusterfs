/*
   Copyright (c) 2026 GlusterFS contributors <http://www.gluster.org>
   This file is part of GlusterFS.

   Test for the directory part of issue:
   https://github.com/gluster/glusterfs/issues/4734
   rmdir()ing a directory that has an open descriptor must keep the
   fd-based metadata operations (fchmod/futimens/fsetxattr/fgetxattr)
   working on that descriptor, like on a local filesystem, while the
   directory disappears from the namespace.
*/

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <time.h>
#include <unistd.h>

static int
fail(const char *what)
{
    fprintf(stderr, "%s failed: %s\n", what, strerror(errno));
    return -1;
}

/* rmdir a directory with an open fd; the fd must keep working for
 * metadata operations. */
static int
test_rmdir_over_open(const char *mnt)
{
    int fd = -1;
    struct timespec ts[2];
    char d[PATH_MAX];
    char val[64];

    snprintf(d, sizeof(d), "%s/rmdir-open-dir", mnt);

    if (mkdir(d, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0)
        return fail("mkdir");

    fd = open(d, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return fail("open dir");

    if (rmdir(d) < 0) {
        close(fd);
        return fail("rmdir open dir");
    }

    if (fchmod(fd, S_IRWXU | S_IRWXG) < 0) {
        close(fd);
        return fail("fchmod on rmdir'd dir descriptor");
    }

    ts[0].tv_sec = 1234567890;
    ts[0].tv_nsec = 0;
    ts[1].tv_sec = 1234567890;
    ts[1].tv_nsec = 0;
    if (futimens(fd, ts) < 0) {
        close(fd);
        return fail("futimens on rmdir'd dir descriptor");
    }

    if (fsetxattr(fd, "user.issue4734", "working", 8, 0) < 0) {
        close(fd);
        return fail("fsetxattr on rmdir'd dir descriptor");
    }

    memset(val, 0, sizeof(val));
    if (fgetxattr(fd, "user.issue4734", val, sizeof(val)) < 0) {
        close(fd);
        return fail("fgetxattr on rmdir'd dir descriptor");
    }
    if (strcmp(val, "working") != 0) {
        fprintf(stderr, "fgetxattr: expected 'working', got '%s'\n", val);
        close(fd);
        return -1;
    }

    /* The name is gone from the namespace. */
    if (access(d, F_OK) == 0) {
        fprintf(stderr, "rmdir'd dir still visible in the namespace\n");
        close(fd);
        return -1;
    }

    /* A second rmdir must fail with ENOENT. */
    if (rmdir(d) == 0 || errno != ENOENT) {
        fprintf(stderr, "second rmdir: expected ENOENT, got %d (%s)\n", errno,
                strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

/* rmdir of an open but non-empty directory must fail with ENOTEMPTY and
 * the directory must stay fully functional. */
static int
test_rmdir_nonempty(const char *mnt)
{
    int fd = -1, ffd = -1;
    char d[PATH_MAX], f[PATH_MAX];

    snprintf(d, sizeof(d), "%s/rmdir-nonempty-dir", mnt);
    snprintf(f, sizeof(f), "%s/file", d);

    if (mkdir(d, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0)
        return fail("mkdir nonempty");

    ffd = creat(f, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (ffd < 0)
        return fail("creat in dir");
    close(ffd);

    fd = open(d, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return fail("open nonempty dir");

    errno = 0;
    if (rmdir(d) == 0 || errno != ENOTEMPTY) {
        fprintf(stderr, "rmdir non-empty: expected ENOTEMPTY, got %d (%s)\n",
                errno, strerror(errno));
        close(fd);
        return -1;
    }

    /* The directory and its contents must be untouched. */
    if (access(f, F_OK) != 0) {
        close(fd);
        return fail("access file after failed rmdir");
    }

    if (unlink(f) < 0) {
        close(fd);
        return fail("unlink file");
    }

    /* Now it is empty (and still open): rmdir must succeed. */
    if (rmdir(d) < 0) {
        close(fd);
        return fail("rmdir emptied open dir");
    }

    close(fd);
    return 0;
}

/* rename() replacing an open empty directory must keep the fd-based
 * metadata operations working on the replaced directory's descriptor. */
static int
test_rename_over_open_dir(const char *mnt)
{
    int fd = -1, ffd = -1, fd2 = -1;
    mode_t mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
    char a[PATH_MAX], b[PATH_MAX], a2[PATH_MAX], cf[PATH_MAX];

    snprintf(a, sizeof(a), "%s/rename-dir-a", mnt);
    snprintf(b, sizeof(b), "%s/rename-dir-b", mnt);

    if (mkdir(a, mode) < 0)
        return fail("mkdir rename-dir-a");
    if (mkdir(b, mode) < 0)
        return fail("mkdir rename-dir-b");

    fd = open(a, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return fail("open rename-dir-a");

    /* Replace the open empty directory dir-a with dir-b. */
    if (rename(b, a) < 0) {
        close(fd);
        return fail("rename dir-b dir-a");
    }

    if (fchmod(fd, S_IRWXU | S_IRWXG) < 0) {
        close(fd);
        return fail("fchmod on replaced dir descriptor");
    }

    /* The old name of the replacement source is gone. */
    if (access(b, F_OK) == 0) {
        fprintf(stderr, "rename source still visible after rename\n");
        close(fd);
        return -1;
    }
    close(fd);

    /* Replacing an open NON-empty directory must fail with ENOTEMPTY
     * and leave the victim intact. */
    snprintf(a2, sizeof(a2), "%s/rename-dir-a2", mnt);
    snprintf(cf, sizeof(cf), "%s/file", a2);

    if (mkdir(a2, mode) < 0)
        return fail("mkdir rename-dir-a2");
    ffd = creat(cf, mode);
    if (ffd < 0)
        return fail("creat in rename-dir-a2");
    close(ffd);

    fd2 = open(a2, O_RDONLY | O_DIRECTORY);
    if (fd2 < 0)
        return fail("open rename-dir-a2");

    errno = 0;
    if (mkdir(b, mode) < 0) {
        close(fd2);
        return fail("mkdir rename-dir-b again");
    }
    if (rename(b, a2) == 0) {
        fprintf(stderr, "rename over non-empty succeeded unexpectedly\n");
        close(fd2);
        return -1;
    }
    if (errno != ENOTEMPTY && errno != EEXIST) {
        fprintf(stderr,
                "rename over non-empty: expected ENOTEMPTY/EEXIST, got "
                "%d (%s)\n",
                errno, strerror(errno));
        close(fd2);
        return -1;
    }

    /* The victim directory and its content must be untouched. */
    if (access(cf, F_OK) != 0) {
        close(fd2);
        return fail("access victim content after failed replace");
    }
    if (fchmod(fd2, mode) < 0) {
        close(fd2);
        return fail("fchmod on victim dir descriptor");
    }

    unlink(cf);
    rmdir(a2);
    close(fd2);
    return 0;
}

int
main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <testdir>\n", argv[0]);
        return 1;
    }

    if (test_rmdir_over_open(argv[1]) < 0)
        return 1;

    if (test_rmdir_nonempty(argv[1]) < 0)
        return 1;

    if (test_rename_over_open_dir(argv[1]) < 0)
        return 1;

    return 0;
}
