#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

#define LOG_ERR(func, ret)                                                     \
    do {                                                                       \
        if (ret != 0) {                                                        \
            fprintf(stderr, "%s : returned error ret(%d), errno(%d)\n", func,  \
                    ret, errno);                                               \
            exit(1);                                                           \
        } else {                                                               \
            fprintf(stderr, "%s : returned %d\n", func, ret);                  \
        }                                                                      \
    } while (0)
#define LOG_IF_NO_ERR(func, ret)                                               \
    do {                                                                       \
        if (ret == 0) {                                                        \
            fprintf(stderr, "%s : hasn't returned error %d\n", func, ret);     \
            exit(1);                                                           \
        } else {                                                               \
            fprintf(stderr, "%s : returned %d\n", func, ret);                  \
        }                                                                      \
    } while (0)
int
main(int argc, char *argv[])
{
    glfs_t *fs = NULL;
    int ret = 0;
    struct glfs_object *root = NULL, *leaf = NULL;
    glfs_fd_t *fd = NULL;
    char *filename = "/ro-file";
    struct stat sb = {
        0,
    };
    char *logfile = NULL;
    char *volname = NULL;
    char *hostname = NULL;
    char buf[32] = "abcdefghijklmnopqrstuvwxyz012345";

    fprintf(stderr, "Starting glfs_h_creat_open\n");

    if (argc != 4) {
        fprintf(stderr, "Invalid argument\n");
        exit(1);
    }

    hostname = argv[1];
    volname = argv[2];
    logfile = argv[3];

    fs = glfs_new(volname);
    if (!fs) {
        fprintf(stderr, "glfs_new: returned NULL\n");
        return 1;
    }

    ret = glfs_set_volfile_server(fs, "tcp", hostname, 24007);
    LOG_ERR("glfs_set_volfile_server", ret);

    ret = glfs_set_logging(fs, logfile, 7);
    LOG_ERR("glfs_set_logging", ret);

    ret = glfs_init(fs);
    LOG_ERR("glfs_init", ret);

    sleep(2);
    root = glfs_h_lookupat(fs, NULL, "/", &sb, 0);
    if (!root) {
        ret = -1;
        LOG_ERR("glfs_h_lookupat root", ret);
    }
    leaf = glfs_h_lookupat(fs, root, filename, &sb, 0);
    if (!leaf) {
        ret = -1;
        LOG_IF_NO_ERR("glfs_h_lookupat leaf", ret);
    }

    leaf = glfs_h_creat_open(fs, root, filename, O_RDONLY, 00444, &sb, &fd);
    if (!leaf || !fd) {
        ret = -1;
        LOG_ERR("glfs_h_creat leaf", ret);
    }
    fprintf(stderr, "glfs_h_create_open leaf - %p\n", leaf);

    ret = glfs_write(fd, buf, 32, 0);
    if (ret < 0) {
        fprintf(stderr, "glfs_write: error writing to file %s, %s\n", filename,
                strerror(errno));
        goto out;
    }

    ret = glfs_h_getattrs(fs, leaf, &sb);
    LOG_ERR("glfs_h_getattrs", ret);

    if (sb.st_size != 32) {
        fprintf(stderr, "glfs_write: post size mismatch\n");
        goto out;
    }

    fprintf(stderr, "Successfully opened and written to a read-only file \n");
out:
    if (fd)
        glfs_close(fd);

    ret = glfs_fini(fs);
    LOG_ERR("glfs_fini", ret);

    fprintf(stderr, "End of libgfapi_fini\n");

    exit(0);
}
