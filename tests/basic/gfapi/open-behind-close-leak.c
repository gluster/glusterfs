/*
 * Open N existing files (/f1../fN) read-only through gfapi and close them
 * without any I/O; then, on N further files (/f(N+1)../f2N), open, glfs_dup()
 * the handle and close both, alternating the order and reading through the
 * surviving dup after the original was closed. Finally take a statedump of the
 * process. With performance.open-behind on the deferred opens must have been
 * cancelled exactly once, by the last handle: the gfapi xlator's live fd count
 * in the dump must be 0 and every read through a dup must succeed.
 *
 * usage: open-behind-close-leak <host> <volume> <N> <logfile> <pidfile>
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glusterfs/api/glfs.h>

int
main(int argc, char *argv[])
{
    glfs_t *fs = NULL;
    glfs_fd_t *fd = NULL;
    glfs_fd_t *dupfd = NULL;
    FILE *pf = NULL;
    char path[64];
    char buf[1];
    int n = 0, i = 0, ret = 0;

    if (argc != 6) {
        fprintf(stderr, "usage: %s <host> <volume> <N> <logfile> <pidfile>\n",
                argv[0]);
        return 1;
    }
    n = atoi(argv[3]);

    fs = glfs_new(argv[2]);
    if (!fs) {
        fprintf(stderr, "glfs_new: %s\n", strerror(errno));
        return 1;
    }
    ret = glfs_set_volfile_server(fs, "tcp", argv[1], 24007);
    if (ret) {
        fprintf(stderr, "glfs_set_volfile_server: %s\n", strerror(errno));
        return 1;
    }
    ret = glfs_set_logging(fs, argv[4], 7);
    if (ret) {
        fprintf(stderr, "glfs_set_logging: %s\n", strerror(errno));
        return 1;
    }
    ret = glfs_init(fs);
    if (ret) {
        fprintf(stderr, "glfs_init: %s\n", strerror(errno));
        return 1;
    }

    for (i = 1; i <= n; i++) {
        snprintf(path, sizeof(path), "/f%d", i);
        fd = glfs_open(fs, path, O_RDONLY);
        if (!fd) {
            fprintf(stderr, "glfs_open(%s): %s\n", path, strerror(errno));
            return 1;
        }
        ret = glfs_close(fd);
        if (ret) {
            fprintf(stderr, "glfs_close(%s): %s\n", path, strerror(errno));
            return 1;
        }
    }

    /* glfs_dup(): the two handles share one fd_t; the fdclose must be
     * delivered once, by whichever handle goes last. */
    for (i = n + 1; i <= 2 * n; i++) {
        snprintf(path, sizeof(path), "/f%d", i);
        fd = glfs_open(fs, path, O_RDONLY);
        if (!fd) {
            fprintf(stderr, "glfs_open(%s): %s\n", path, strerror(errno));
            return 1;
        }
        dupfd = glfs_dup(fd);
        if (!dupfd) {
            fprintf(stderr, "glfs_dup(%s): %s\n", path, strerror(errno));
            return 1;
        }
        if (i % 2) {
            /* original first, then read through the dup: a premature
             * fdclose would leave open-behind unable to serve the read */
            ret = glfs_close(fd);
            if (ret) {
                fprintf(stderr, "glfs_close(%s): %s\n", path, strerror(errno));
                return 1;
            }
            if (glfs_pread(dupfd, buf, 1, 0, 0, NULL) != 1) {
                fprintf(stderr,
                        "glfs_pread through dup of %s after closing "
                        "the original: %s\n",
                        path, strerror(errno));
                return 1;
            }
            ret = glfs_close(dupfd);
        } else {
            ret = glfs_close(dupfd);
            if (ret) {
                fprintf(stderr, "glfs_close(dup %s): %s\n", path,
                        strerror(errno));
                return 1;
            }
            ret = glfs_close(fd);
        }
        if (ret) {
            fprintf(stderr, "glfs_close(%s): %s\n", path, strerror(errno));
            return 1;
        }
    }

    pf = fopen(argv[5], "w");
    if (!pf) {
        fprintf(stderr, "fopen(%s): %s\n", argv[5], strerror(errno));
        return 1;
    }
    fprintf(pf, "%d\n", (int)getpid());
    fclose(pf);

    ret = glfs_sysrq(fs, GLFS_SYSRQ_STATEDUMP);
    if (ret) {
        fprintf(stderr, "glfs_sysrq: %s\n", strerror(errno));
        return 1;
    }

    glfs_fini(fs);
    return 0;
}
