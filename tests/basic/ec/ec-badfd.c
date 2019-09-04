#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

int
fill_iov(struct iovec *iov, char fillchar, int count)
{
    int ret = -1;

    iov->iov_base = malloc(count + 1);
    if (iov->iov_base == NULL) {
        return ret;
    } else {
        iov->iov_len = count;
        ret = 0;
    }
    memset(iov->iov_base, fillchar, count);
    memset(iov->iov_base + count, '\0', 1);

    return ret;
}

int
write_sync(glfs_t *fs, glfs_fd_t *glfd, int char_count)
{
    ssize_t ret = -1;
    int flags = O_RDWR;
    struct iovec iov = {0};

    ret = fill_iov(&iov, 'a', char_count);
    if (ret) {
        fprintf(stderr, "failed to create iov");
        goto out;
    }

    ret = glfs_pwritev(glfd, &iov, 1, 0, flags);
out:
    if (ret < 0) {
        fprintf(stderr, "glfs_pwritev failed, %d", errno);
    }
    return ret;
}

int
main(int argc, char *argv[])
{
    glfs_t *fs = NULL;
    glfs_fd_t *fd = NULL;
    int ret = 1;
    char volume_cmd[4096] = {0};

    if (argc != 4) {
        fprintf(stderr, "Syntax: %s <host> <volname> <file>\n", argv[0]);
        return 1;
    }

    fs = glfs_new(argv[2]);
    if (!fs) {
        fprintf(stderr, "glfs_new: returned NULL\n");
        return 1;
    }

    ret = glfs_set_volfile_server(fs, "tcp", argv[1], 24007);
    if (ret != 0) {
        fprintf(stderr, "glfs_set_volfile_server: returned %d\n", ret);
        goto out;
    }
    ret = glfs_set_logging(fs, "/tmp/ec-badfd.log", 7);
    if (ret != 0) {
        fprintf(stderr, "glfs_set_logging: returned %d\n", ret);
        goto out;
    }
    ret = glfs_init(fs);
    if (ret != 0) {
        fprintf(stderr, "glfs_init: returned %d\n", ret);
        goto out;
    }

    fd = glfs_open(fs, argv[3], O_RDWR);
    if (fd == NULL) {
        fprintf(stderr, "glfs_open: returned NULL\n");
        goto out;
    }

    ret = write_sync(fs, fd, 16);
    if (ret < 0) {
        fprintf(stderr, "write_sync failed\n");
    }

    snprintf(volume_cmd, sizeof(volume_cmd),
             "gluster --mode=script volume stop %s", argv[2]);
    /*Stop the volume so that update-size-version fails*/
    system(volume_cmd);
    sleep(8); /* 3 seconds more than eager-lock-timeout*/
    snprintf(volume_cmd, sizeof(volume_cmd),
             "gluster --mode=script volume start %s", argv[2]);
    system(volume_cmd);
    sleep(8); /*wait for bricks to come up*/
    ret = glfs_fsync(fd, NULL, NULL);
    if (ret == 0) {
        fprintf(stderr, "fsync succeeded on a BADFD\n");
        exit(1);
    }

    ret = glfs_close(fd);
    if (ret == 0) {
        fprintf(stderr, "flush succeeded on a BADFD\n");
        exit(1);
    }
    ret = 0;

out:
    unlink("/tmp/ec-badfd.log");
    glfs_fini(fs);

    return ret;
}
