#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/xattr.h>

static char MY_XATTR[] = "user.fxtest";
static char *PROGRAM;
#define CONSUME(v)                                                             \
    do {                                                                       \
        if (!argc) {                                                           \
            fprintf(stderr, "missing argument\n");                             \
            return EXIT_FAILURE;                                               \
        }                                                                      \
        v = argv[0];                                                           \
        ++argv;                                                                \
        --argc;                                                                \
    } while (0)

static int
do_get(int argc, char **argv, int fd)
{
    char *value;
    int ret;
    char buf[1024];

    CONSUME(value);

    ret = fgetxattr(fd, MY_XATTR, buf, sizeof(buf));
    if (ret == (-1)) {
        perror("fgetxattr");
        return EXIT_FAILURE;
    }

    if (strncmp(buf, value, ret) != 0) {
        fprintf(stderr, "data mismatch\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int
do_set(int argc, char **argv, int fd)
{
    char *value;
    int ret;

    CONSUME(value);

    ret = fsetxattr(fd, MY_XATTR, value, strlen(value), 0);
    if (ret == (-1)) {
        perror("fsetxattr");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int
do_remove(int argc, char **argv, int fd)
{
    int ret;

    ret = fremovexattr(fd, MY_XATTR);
    if (ret == (-1)) {
        perror("femovexattr");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int
main(int argc, char **argv)
{
    int fd;
    char *path;
    char *cmd;

    CONSUME(PROGRAM);
    CONSUME(path);
    CONSUME(cmd);

    fd = open(path, O_RDWR);
    if (fd == (-1)) {
        perror("open");
        return EXIT_FAILURE;
    }

    if (strcmp(cmd, "get") == 0) {
        return do_get(argc, argv, fd);
    }

    if (strcmp(cmd, "set") == 0) {
        return do_set(argc, argv, fd);
    }

    if (strcmp(cmd, "remove") == 0) {
        return do_remove(argc, argv, fd);
    }

    return EXIT_SUCCESS;
}
