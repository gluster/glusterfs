#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gfapi = 1;

int
main(int argc, char *argv[])
{
    glfs_t *fs = NULL;
    int ret = 0;
    int i = 0;
    glfs_fd_t *fd = NULL;
    char *topdir = "topdir", *filename = "file1";
    char *buf = NULL;
    char *logfile = NULL;
    char *hostname = NULL;
    char *basename = NULL;
    char *dir1 = NULL, *dir2 = NULL, *filename1 = NULL, *filename2 = NULL;
    struct stat sb = {
        0,
    };

    if (argc != 5) {
        fprintf(
            stderr,
            "Expect following args %s <hostname> <Vol> <log file> <basename>\n",
            argv[0]);
        return -1;
    }

    hostname = argv[1];
    logfile = argv[3];
    basename = argv[4];

    fs = glfs_new(argv[2]);
    if (!fs) {
        fprintf(stderr, "glfs_new: returned NULL (%s)\n", strerror(errno));
        return -1;
    }

    ret = glfs_set_volfile_server(fs, "tcp", hostname, 24007);
    if (ret < 0) {
        fprintf(stderr, "glfs_set_volfile_server failed ret:%d (%s)\n", ret,
                strerror(errno));
        return -1;
    }

    ret = glfs_set_logging(fs, logfile, 7);
    if (ret < 0) {
        fprintf(stderr, "glfs_set_logging failed with ret: %d (%s)\n", ret,
                strerror(errno));
        return -1;
    }

    ret = glfs_init(fs);
    if (ret < 0) {
        fprintf(stderr, "glfs_init failed with ret: %d (%s)\n", ret,
                strerror(errno));
        return -1;
    }

    ret = asprintf(&dir1, "%s-dir", basename);
    if (ret < 0) {
        fprintf(stderr, "cannot construct filename (%s)", strerror(errno));
        return ret;
    }

    ret = glfs_mkdir(fs, dir1, 0755);
    if (ret < 0) {
        fprintf(stderr, "mkdir(%s): %s\n", dir1, strerror(errno));
        return -1;
    }

    fd = glfs_opendir(fs, dir1);
    if (!fd) {
        fprintf(stderr, "/: %s\n", strerror(errno));
        return -1;
    }

    ret = glfs_fsetxattr(fd, "user.dirfattr", "fsetxattr", 9, 0);
    if (ret < 0) {
        fprintf(stderr, "fsetxattr(%s): %d (%s)\n", dir1, ret, strerror(errno));
        return -1;
    }

    ret = glfs_closedir(fd);
    if (ret < 0) {
        fprintf(stderr, "glfs_closedir failed with ret: %d (%s)\n", ret,
                strerror(errno));
        return -1;
    }

    ret = asprintf(&filename1, "%s-file", basename);
    if (ret < 0) {
        fprintf(stderr, "cannot construct filename (%s)", strerror(errno));
        return ret;
    }

    ret = asprintf(&filename2, "%s-file-renamed", basename);
    if (ret < 0) {
        fprintf(stderr, "cannot construct filename (%s)", strerror(errno));
        return ret;
    }

    fd = glfs_creat(fs, filename1, O_RDWR, 0644);
    if (!fd) {
        fprintf(stderr, "%s: (%p) %s\n", filename1, fd, strerror(errno));
        return -1;
    }

    ret = glfs_rename(fs, filename1, filename2);
    if (ret < 0) {
        fprintf(stderr, "glfs_rename failed with ret: %d (%s)\n", ret,
                strerror(errno));
        return -1;
    }

    ret = glfs_lstat(fs, filename2, &sb);
    if (ret < 0) {
        fprintf(stderr, "glfs_lstat failed with ret: %d (%s)\n", ret,
                strerror(errno));
        return -1;
    }

    ret = glfs_fsetxattr(fd, "user.filefattr", "fsetxattr", 9, 0);
    if (ret < 0) {
        fprintf(stderr, "fsetxattr(%s): %d (%s)\n", dir1, ret, strerror(errno));
        return -1;
    }

    ret = glfs_close(fd);
    if (ret < 0) {
        fprintf(stderr, "glfs_close failed with ret: %d (%s)\n", ret,
                strerror(errno));
        return -1;
    }
}
