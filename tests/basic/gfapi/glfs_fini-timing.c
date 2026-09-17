/*
 * glfs_fini-timing.c -- time glfs_fini() of a libgfapi client that has done
 * I/O. Prints the wall time of glfs_fini() in milliseconds (an integer) on
 * stdout. Usage: glfs_fini-timing <host> <volume> <logfile>
 */
#include <glusterfs/api/glfs.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

int
main(int argc, char **argv)
{
    glfs_t *fs = NULL;
    glfs_fd_t *fd = NULL;
    struct timespec t0, t1;
    char buf[4096];
    long ms;
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
        fprintf(stderr, "glfs_init failed\n");
        return 1;
    }

    /* one request is enough to arm the 10 s call_bail timer (__save_frame) */
    fd = glfs_creat(fs, "/glfs_fini-timing", O_RDWR, 0644);
    if (!fd) {
        fprintf(stderr, "glfs_creat failed\n");
        return 1;
    }
    memset(buf, 0, sizeof(buf));
    glfs_write(fd, buf, sizeof(buf), 0);
    glfs_close(fd);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    ret = glfs_fini(fs);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    printf("%ld\n", ms);
    /* a fast -1 must not read as a fast, successful fini */
    return ret ? 2 : 0;
}
