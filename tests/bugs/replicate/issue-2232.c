
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <glusterfs/api/glfs.h>

int main(int argc, char **argv)
{
    char log[128];
    struct dirent entry;
    struct dirent *ent;
    glfs_xreaddirp_stat_t *xstat;
    int ret, flags;

    if (argc != 3) {
        fprintf(stderr, "Syntax: %s <hostname> <volume>\n", argv[0]);
        exit(1);
    }
    char *hostname = argv[1];
    char *volname = argv[2];

    glfs_t *fs = glfs_new(volname);
    if (!fs) {
        fprintf(stderr, "glfs_new() failed\n");
        exit(1);
    }

    ret = glfs_set_volfile_server(fs, "tcp", hostname, 24007);
    if (ret < 0) {
        fprintf(stderr, "glfs_set_volfile_server() failed\n");
        return ret;
    }

    sprintf(log, "/tmp/logs-%d.log", getpid());
    ret = glfs_set_logging(fs, log, 9);
    if (ret < 0) {
        fprintf(stderr, "glfs_set_logging() failed\n");
        return ret;
    }

    ret = glfs_init(fs);
    if (ret < 0) {
        fprintf(stderr, "glfs_init() failed\n");
        return ret;
    }

    glfs_fd_t *fd = glfs_opendir(fs, "/");
    if (fd == NULL) {
        fprintf(stderr, "glfs_opendir() failed\n");
        return 1;
    }

    flags = GFAPI_XREADDIRP_STAT | GFAPI_XREADDIRP_HANDLE;
    xstat = NULL;
    while ((ret = glfs_xreaddirplus_r(fd, flags, &xstat, &entry, &ent)) > 0) {
        if (xstat != NULL) {
            glfs_free(xstat);
        }
        if ((strcmp(ent->d_name, ".") == 0) ||
            (strcmp(ent->d_name, "..") == 0)) {
            xstat = NULL;
            continue;
        }
        if ((xstat == NULL) || ((ret & GFAPI_XREADDIRP_HANDLE) == 0)) {
            fprintf(stderr, "glfs_xreaddirplus_r() failed: %s\n",
                    strerror(errno));
            return 1;
        }

        xstat = NULL;
    }

    if (ret < 0) {
        fprintf(stderr, "glfs_xreaddirplus_r() failed\n");
        return ret;
    }

    glfs_close(fd);

    glfs_fini(fs);

    return ret;
}
