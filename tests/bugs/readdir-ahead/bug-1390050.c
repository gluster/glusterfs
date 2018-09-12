#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>

int
main(int argc, char *argv[])
{
    const char *glfs_dir = NULL, *filepath = NULL;
    DIR *dirfd = NULL;
    int filefd = 0, ret = 0;
    struct stat stbuf = {
        0,
    };
    size_t size_before_write = 0;

    glfs_dir = argv[1];
    filepath = argv[2];
    dirfd = opendir(glfs_dir);
    if (dirfd == NULL) {
        fprintf(stderr, "opening directory failed (%s)\n", strerror(errno));
        goto err;
    }

    filefd = open(filepath, O_RDWR);
    if (filefd < 0) {
        fprintf(stderr, "open failed on path %s (%s)\n", filepath,
                strerror(errno));
        goto err;
    }

    ret = stat(filepath, &stbuf);
    if (ret < 0) {
        fprintf(stderr, "stat failed on path %s (%s)\n", filepath,
                strerror(errno));
        goto err;
    }

    size_before_write = stbuf.st_size;

    ret = write(filefd, "testdata", strlen("testdata123") + 1);
    if (ret <= 0) {
        fprintf(stderr, "write failed (%s)\n", strerror(errno));
        goto err;
    }

    while (readdir(dirfd)) {
        /* do nothing */
    }

    ret = stat(filepath, &stbuf);
    if (ret < 0) {
        fprintf(stderr, "stat failed on path %s (%s)\n", strerror(errno));
        goto err;
    }

    if (stbuf.st_size == size_before_write) {
        fprintf(stderr,
                "file size (%lu) has not changed even after "
                "its written to\n",
                stbuf.st_size);
        goto err;
    }

    return 0;
err:
    return -1;
}
