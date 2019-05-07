#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

#define LOG_ERR(msg)                                                           \
    do {                                                                       \
        fprintf(stderr, "%s : Error (%s)\n", msg, strerror(errno));            \
    } while (0)

int cbk_complete = 0;
int cbk_ret_val = -1;

void
cbk_check()
{
    while (cbk_complete != 1) {
        sleep(1);
    }
    if (cbk_ret_val < 0) {
        fprintf(stderr, "cbk_ret_val is -ve\n");
    }
}

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

glfs_t *
init_glfs(const char *hostname, const char *volname, const char *logfile)
{
    int ret = -1;
    glfs_t *fs = NULL;

    fs = glfs_new(volname);
    if (!fs) {
        LOG_ERR("glfs_new failed");
        return NULL;
    }

    ret = glfs_set_volfile_server(fs, "tcp", hostname, 24007);
    if (ret < 0) {
        LOG_ERR("glfs_set_volfile_server failed");
        goto out;
    }

    ret = glfs_set_logging(fs, logfile, 7);
    if (ret < 0) {
        LOG_ERR("glfs_set_logging failed");
        goto out;
    }

    ret = glfs_init(fs);
    if (ret < 0) {
        LOG_ERR("glfs_init failed");
        goto out;
    }

    ret = 0;
out:
    if (ret) {
        glfs_fini(fs);
        fs = NULL;
    }

    return fs;
}

void
pwritev_async_cbk(glfs_fd_t *fd, ssize_t ret, struct stat *prestat,
                  struct stat *poststat, void *cookie)
{
    if (ret < 0) {
        LOG_ERR("glfs_pwritev failed");
    }
    cbk_ret_val = ret;
    cbk_complete = 1;
}

int
pwritev_async(glfs_t *fs, glfs_fd_t *glfd, int char_count)
{
    ssize_t ret = -1;
    int flags = O_RDWR;
    struct iovec iov = {0};
    void *write_cookie = NULL;

    ret = fill_iov(&iov, 'a', char_count);
    if (ret) {
        LOG_ERR("failed to create iov");
        goto out;
    }

    write_cookie = strdup("write_cookie");
    ret = glfs_pwritev_async(glfd, &iov, 1, 0, flags, pwritev_async_cbk,
                             &write_cookie);
out:
    if (ret < 0) {
        LOG_ERR("glfs_pwritev async failed");
    }
    return ret;
}

void
pwrite_async_cbk(glfs_fd_t *fd, ssize_t ret, struct stat *prestat,
                 struct stat *poststat, void *cookie)
{
    if (ret < 0) {
        LOG_ERR("glfs_pwrite_cbk failed");
    }
    cbk_ret_val = ret;
    cbk_complete = 1;
}

int
pwrite_async(glfs_fd_t *glfd)
{
    ssize_t ret = -1;
    int flags = O_RDWR;
    char buf1[10];
    char *buf2 = "ten bytes!";
    void *write_cookie = strdup("write_cookie");
    ret = glfs_pwrite_async(glfd, buf1, 10, 0, flags, pwrite_async_cbk,
                            &write_cookie);

    if (ret < 0) {
        LOG_ERR("glfs_pwrite_async failed");
    }
    return ret;
}

void
writev_async_cbk(glfs_fd_t *fd, ssize_t ret, struct stat *prestat,
                 struct stat *poststat, void *cookie)
{
    if (ret < 0) {
        LOG_ERR("glfs_writev_cbk failed");
    }
    cbk_ret_val = ret;
    cbk_complete = 1;
}

int
writev_async(glfs_t *fs, glfs_fd_t *glfd, int char_count)
{
    ssize_t ret = -1;
    int flags = O_RDWR;
    struct iovec iov = {0};
    void *write_cookie = NULL;

    ret = fill_iov(&iov, 'a', char_count);
    if (ret) {
        LOG_ERR("failed to create iov");
        goto out;
    }

    write_cookie = strdup("write_cookie");
    ret = glfs_writev_async(glfd, &iov, 1, flags, writev_async_cbk,
                            &write_cookie);
out:
    if (ret < 0) {
        LOG_ERR("glfs_writev_async failed");
    }
    return ret;
}

void
write_async_cbk(glfs_fd_t *fd, ssize_t ret, struct stat *prestat,
                struct stat *poststat, void *cookie)
{
    if (ret < 0) {
        LOG_ERR("glfs_write_cbk failed");
    }
    cbk_ret_val = ret;
    cbk_complete = 1;
}

int
write_async(glfs_fd_t *glfd)
{
    ssize_t ret = -1;
    int flags = O_RDWR;
    char buf1[10];
    char *buf2 = "ten bytes!";
    void *write_cookie = strdup("write_cookie");
    ret = glfs_write_async(glfd, buf1, 10, flags, write_async_cbk,
                           &write_cookie);

    if (ret < 0) {
        LOG_ERR("glfs_write_async failed");
    }
    return ret;
}

void
preadv_async_cbk(glfs_fd_t *fd, ssize_t ret, struct stat *prestat,
                 struct stat *poststat, void *cookie)
{
    if (ret < 0) {
        LOG_ERR("glfs_preadv_cbk failed");
    }
    cbk_ret_val = ret;
    cbk_complete = 1;
}

int
preadv_async(glfs_t *fs, glfs_fd_t *glfd, int char_count)
{
    ssize_t ret = -1;
    int flags = O_RDWR;
    struct iovec iov = {0};
    void *read_cookie = NULL;

    ret = fill_iov(&iov, 'a', char_count);
    if (ret) {
        LOG_ERR("failed to create iov");
        goto out;
    }

    read_cookie = strdup("preadv_cookie");
    ret = glfs_preadv_async(glfd, &iov, 1, 0, flags, preadv_async_cbk,
                            &read_cookie);
out:
    if (ret < 0) {
        LOG_ERR("glfs_preadv async failed");
    }
    return ret;
}

void
pread_async_cbk(glfs_fd_t *fd, ssize_t ret, struct stat *prestat,
                struct stat *poststat, void *cookie)
{
    if (ret < 0) {
        LOG_ERR("glfs_pread_cbk failed");
    }
    cbk_ret_val = ret;
    cbk_complete = 1;
}

int
pread_async(glfs_fd_t *glfd)
{
    ssize_t ret = -1;
    int flags = O_RDWR;
    char buf1[10];
    void *read_cookie = strdup("read_cookie");
    ret = glfs_pread_async(glfd, buf1, 10, 0, flags, pread_async_cbk,
                           &read_cookie);
    if (ret < 0) {
        LOG_ERR("glfs_pread_async failed");
    }

    return ret;
}

void
readv_async_cbk(glfs_fd_t *fd, ssize_t ret, struct stat *prestat,
                struct stat *poststat, void *cookie)
{
    if (ret < 0) {
        LOG_ERR("glfs_readv_cbk failed");
    }
    cbk_ret_val = ret;
    cbk_complete = 1;
}

int
readv_async(glfs_t *fs, glfs_fd_t *glfd, int char_count)
{
    ssize_t ret = -1;
    int flags = O_RDWR;
    struct iovec iov = {0};
    void *read_cookie = NULL;

    ret = fill_iov(&iov, 'a', char_count);
    if (ret) {
        LOG_ERR("failed to create iov");
        goto out;
    }

    read_cookie = strdup("read_cookie");
    ret = glfs_readv_async(glfd, &iov, 1, flags, readv_async_cbk, &read_cookie);
out:
    if (ret < 0) {
        LOG_ERR("glfs_readv_async failed");
    }
    return ret;
}

void
read_async_cbk(glfs_fd_t *fd, ssize_t ret, struct stat *prestat,
               struct stat *poststat, void *cookie)
{
    if (ret < 0) {
        LOG_ERR("glfs_read_cbk failed");
    }
    cbk_ret_val = ret;
    cbk_complete = 1;
}

int
read_async(glfs_fd_t *glfd)
{
    ssize_t ret = -1;
    int flags = O_RDWR;
    char buf1[10];
    void *read_cookie = strdup("read_cookie");
    ret = glfs_read_async(glfd, buf1, 10, flags, read_async_cbk, &read_cookie);

    if (ret < 0) {
        LOG_ERR("glfs_read_async failed");
    }
    return ret;
}

void
fsync_async_cbk(glfs_fd_t *fd, ssize_t ret, struct stat *prestat,
                struct stat *poststat, void *cookie)
{
    if (ret < 0) {
        LOG_ERR("glfs_fsync_async_cbk failed");
    }
    cbk_ret_val = ret;
    cbk_complete = 1;
}

void
fdatasync_async_cbk(glfs_fd_t *fd, ssize_t ret, struct stat *prestat,
                    struct stat *poststat, void *cookie)
{
    if (ret < 0) {
        LOG_ERR("glfs_fdatasync_async_cbk failed");
    }
    cbk_ret_val = ret;
    cbk_complete = 1;
}

void
ftruncate_async_cbk(glfs_fd_t *fd, ssize_t ret, struct stat *prestat,
                    struct stat *poststat, void *cookie)
{
    if (ret < 0) {
        LOG_ERR("glfs_ftruncate_async_cbk failed");
    }
    cbk_ret_val = ret;
    cbk_complete = 1;
}

int
main(int argc, char *argv[])
{
    int ret = 0;
    char *hostname = NULL;
    char *volname = NULL;
    char *logfile = NULL;
    glfs_t *fs = NULL;
    const char *filename = "glfs_test.txt";
    int flags = (O_RDWR | O_CREAT);
    glfs_fd_t *glfd = NULL;
    int count = 200;
    void *data = strdup("Sample_text");

    if (argc != 4) {
        fprintf(stderr, "Invalid argument\n");
        exit(1);
    }

    hostname = argv[1];
    volname = argv[2];
    logfile = argv[3];

    fs = init_glfs(hostname, volname, logfile);
    if (fs == NULL) {
        LOG_ERR("init_glfs failed");
        return -1;
    }

    glfd = glfs_creat(fs, filename, flags, 0644);
    if (glfd == NULL) {
        LOG_ERR("glfs_creat failed");
        exit(1);
    }

    ret = pwritev_async(fs, glfd, count);
    if (ret) {
        LOG_ERR("glfs_pwritev_async_test failed");
        exit(1);
    }
    cbk_check();

    ret = writev_async(fs, glfd, count);
    if (ret) {
        LOG_ERR("glfs_writev_async_test failed");
        exit(1);
    }
    cbk_check();

    ret = write_async(glfd);
    if (ret) {
        LOG_ERR("glfs_write_async_test failed");
        exit(1);
    }
    cbk_check();

    ret = preadv_async(fs, glfd, count);
    if (ret) {
        LOG_ERR("glfs_preadv_async_test failed");
        exit(1);
    }
    cbk_check();

    ret = pread_async(glfd);
    if (ret) {
        LOG_ERR("glfs_pread_async_test failed");
        exit(1);
    }
    cbk_check();

    ret = readv_async(fs, glfd, count);
    if (ret) {
        LOG_ERR("glfs_readv_async_test failed");
        exit(1);
    }
    cbk_check();

    ret = read_async(glfd);
    if (ret) {
        LOG_ERR("glfs_read_async_test failed");
        exit(1);
    }
    cbk_check();

    ret = glfs_fsync(glfd, NULL, NULL);
    if (ret < 0) {
        LOG_ERR("glfs_fsync failed");
        exit(1);
    }

    ret = glfs_fdatasync(glfd, NULL, NULL);
    if (ret < 0) {
        LOG_ERR("glfs_fdatasync failed");
        exit(1);
    }

    ret = glfs_fsync_async(glfd, fsync_async_cbk, data);
    if (ret < 0) {
        LOG_ERR("glfs_fsync_async failed");
        exit(1);
    }
    cbk_check();

    ret = glfs_fdatasync_async(glfd, fdatasync_async_cbk, data);
    if (ret < 0) {
        LOG_ERR("glfs_fdatasync_async failed");
        exit(1);
    }
    cbk_check();

    ret = glfs_ftruncate_async(glfd, 4, ftruncate_async_cbk, data);
    if (ret < 0) {
        LOG_ERR("glfs_ftruncate_async failed");
        exit(1);
    }

    ret = glfs_close(glfd);
    if (ret < 0) {
        LOG_ERR("glfs close  failed");
    }

    ret = glfs_fini(fs);

    return ret;
}
