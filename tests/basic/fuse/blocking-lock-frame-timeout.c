#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int
create_marker(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

    if (fd < 0) {
        fprintf(stderr, "open(%s): %s\n", path, strerror(errno));
        return -1;
    }

    return close(fd);
}

static int
wait_for_release(const char *path)
{
    char byte;
    int fd = open(path, O_RDONLY);

    if (fd < 0) {
        fprintf(stderr, "open(%s): %s\n", path, strerror(errno));
        return -1;
    }

    if (read(fd, &byte, 1) < 0) {
        fprintf(stderr, "read(%s): %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }

    return close(fd);
}

int
main(int argc, char **argv)
{
    struct flock lock = {
        .l_type = F_WRLCK,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0,
    };
    int fd = -1;
    int ret = 1;

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: %s file acquired-marker [release-marker]\n",
                argv[0]);
        return 2;
    }

    fd = open(argv[1], O_RDWR | O_CREAT, 0600);
    if (fd < 0) {
        fprintf(stderr, "open(%s): %s\n", argv[1], strerror(errno));
        goto out;
    }

    if (fcntl(fd, F_SETLKW, &lock) < 0) {
        fprintf(stderr, "fcntl(F_SETLKW): %s\n", strerror(errno));
        goto out;
    }

    if (create_marker(argv[2]) < 0)
        goto out;

    if (argc == 4 && wait_for_release(argv[3]) < 0)
        goto out;

    lock.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLK, &lock) < 0) {
        fprintf(stderr, "fcntl(F_SETLK): %s\n", strerror(errno));
        goto out;
    }

    ret = 0;

out:
    if (fd >= 0)
        close(fd);
    return ret;
}
