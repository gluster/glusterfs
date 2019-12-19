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

int
fill_iov(struct iovec *iov, char fillchar, int count)
{
    int ret = -1;

    iov->iov_base = calloc(count + 1, sizeof(fillchar));
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

int
main(int argc, char *argv[])
{
    char *hostname = NULL;
    char *volname = NULL;
    char *logfile = NULL;
    glfs_t *fs = NULL;
    glfs_fd_t *glfd = NULL;
    int ret = 0;
    int i = 0;
    int count = 200;
    struct iovec iov = {0};
    int flags = O_RDWR;
    int bricksup = 0;
    int fdopen = 0;

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

    /* Brick is down and we are opening a file to trigger fd heal. */
    /* Bypass Write-behind */
    glfd = glfs_open(fs, "a", O_WRONLY | O_TRUNC | O_SYNC);
    if (glfd == NULL) {
        LOG_ERR("glfs_open_truncate failed");
        exit(1);
    }
    system("gluster --mode=script volume start patchy force");
    /*CHILD_UP_TIMEOUT is 20 seconds*/
    for (i = 0; i < 20; i++) {
        ret = system(
            "[ $(gluster --mode=script volume status patchy | "
            "grep \" Y \" | awk '{print $(NF-1)}' | wc -l) == 3 ]");
        if (WIFEXITED(ret) && WEXITSTATUS(ret)) {
            printf("Ret value of system: %d\n, ifexited: %d, exitstatus: %d",
                   ret, WIFEXITED(ret), WEXITSTATUS(ret));
            sleep(1);
            continue;
        }
        printf("Number of loops: %d\n", i);
        bricksup = 1;
        break;
    }
    if (!bricksup) {
        system("gluster --mode=script volume status patchy");
        LOG_ERR("Bricks didn't come up\n");
        exit(1);
    }

    /*Not sure how to check that the child-up reached EC, so sleep 3 for now*/
    sleep(3);
    ret = fill_iov(&iov, 'a', 200);
    if (ret) {
        LOG_ERR("failed to create iov");
        exit(1);
    }

    /*write will trigger re-open*/
    ret = glfs_pwritev(glfd, &iov, 1, 0, flags);
    if (ret < 0) {
        LOG_ERR("glfs_test_function failed");
        exit(1);
    }
    /*Check reopen happened by checking for open-fds on the brick*/
    for (i = 0; i < 20; i++) {
        ret = system(
            "[ $(for i in $(pgrep glusterfsd); do ls -l /proc/$i/fd | grep "
            "\"[.]glusterfs\" | grep -v \".glusterfs/[0-9a-f][0-9a-f]\" | grep "
            "-v health_check; done | wc -l) == 3 ]");
        if (WIFEXITED(ret) && WEXITSTATUS(ret)) {
            printf("Ret value of system: %d\n, ifexited: %d, exitstatus: %d",
                   ret, WIFEXITED(ret), WEXITSTATUS(ret));
            sleep(1);
            continue;
        }
        fdopen = 1;
        break;
    }

    if (!fdopen) {
        LOG_ERR("fd reopen didn't succeed");
        exit(1);
    }

    return 0;
}
