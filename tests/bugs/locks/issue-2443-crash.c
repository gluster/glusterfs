#include <sys/file.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int
main(int argc, char *argv[])
{
    int fd = -1;
    char *filename = NULL;
    struct flock lock = {
        0,
    };
    int i = 0;
    int ret = -1;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <filename> ", argv[0]);
        goto out;
    }

    filename = argv[1];

    fd = open(filename, O_RDWR | O_CREAT, 0);
    if (fd < 0) {
        fprintf(stderr, "open (%s) failed (%s)\n", filename, strerror(errno));
        goto out;
    }

    lock.l_start = 0;
    lock.l_type = F_RDLCK;
    lock.l_whence = SEEK_SET;
    lock.l_len = 2;

    ret = fcntl(fd, F_SETLK, &lock);
    if (ret < 0) {
        fprintf(stderr, "fcntl setlk failed (%s)\n", strerror(errno));
        goto out;
    }

    lock.l_start = 2;
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_len = 2;

    ret = fcntl(fd, F_SETLK, &lock);
    if (ret < 0) {
        fprintf(stderr, "fcntl setlk failed (%s)\n", strerror(errno));
        goto out;
    }

    lock.l_start = 0;
    lock.l_type = F_RDLCK;
    lock.l_whence = SEEK_SET;
    lock.l_len = 4;

    ret = fcntl(fd, F_SETLK, &lock);
    if (ret < 0) {
        fprintf(stderr, "fcntl setlk failed (%s)\n", strerror(errno));
        goto out;
    }
out:
    return ret;
}
