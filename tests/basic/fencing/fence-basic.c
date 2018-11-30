#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NO_INIT 1
#define GF_ENFORCE_MANDATORY_LOCK "trusted.glusterfs.enforce-mandatory-lock"

FILE *fp;
char *buf = "0123456789";

#define LOG_ERR(func, err)                                                     \
    do {                                                                       \
        if (!fp) {                                                             \
            fprintf(stderr, "%\n%d %s : returned error (%s)\n", __LINE__,      \
                    func, strerror(err));                                      \
            fflush(stderr);                                                    \
        } else {                                                               \
            fprintf(fp, "\n%d %s : returned error (%s)\n", __LINE__, func,     \
                    strerror(err));                                            \
            fflush(fp);                                                        \
        }                                                                      \
    } while (0)

glfs_t *
setup_new_client(char *hostname, char *volname, char *log_file, int flag)
{
    int ret = 0;
    glfs_t *fs = NULL;

    fs = glfs_new(volname);
    if (!fs) {
        fprintf(fp, "\nglfs_new: returned NULL (%s)\n", strerror(errno));
        goto error;
    }

    ret = glfs_set_volfile_server(fs, "tcp", hostname, 24007);
    if (ret < 0) {
        fprintf(fp, "\nglfs_set_volfile_server failed ret:%d (%s)\n", ret,
                strerror(errno));
        goto error;
    }

    ret = glfs_set_logging(fs, log_file, 7);
    if (ret < 0) {
        fprintf(fp, "\nglfs_set_logging failed with ret: %d (%s)\n", ret,
                strerror(errno));
        goto error;
    }

    if (flag == NO_INIT)
        goto out;

    ret = glfs_init(fs);
    if (ret < 0) {
        fprintf(fp, "\nglfs_init failed with ret: %d (%s)\n", ret,
                strerror(errno));
        goto error;
    }

out:
    return fs;
error:
    return NULL;
}

/* test plan
 *
 *  - take mandatory lock from client 1
 *  - preempt mandatory lock from client 2
 *  - write from client 1 which should fail
 */

int
test(glfs_t *fs1, glfs_t *fs2, char *fname)
{
    struct flock lock;
    int ret = 0;
    glfs_fd_t *fd1, *fd2 = NULL;

    fd1 = glfs_creat(fs1, fname, O_RDWR, 0777);
    if (ret) {
        LOG_ERR("glfs_creat", errno);
        ret = -1;
        goto out;
    }

    fd2 = glfs_open(fs2, fname, O_RDWR | O_NONBLOCK);
    if (ret) {
        LOG_ERR("glfs_open", errno);
        ret = -1;
        goto out;
    }

    /* initialize lock */
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 100;

    ret = glfs_fsetxattr(fd1, GF_ENFORCE_MANDATORY_LOCK, "set", 8, 0);
    if (ret < 0) {
        LOG_ERR("glfs_fsetxattr", errno);
        ret = -1;
        goto out;
    }

    /* take a write mandatory lock */
    ret = glfs_file_lock(fd1, F_SETLKW, &lock, GLFS_LK_MANDATORY);
    if (ret) {
        LOG_ERR("glfs_file_lock", errno);
        goto out;
    }

    ret = glfs_write(fd1, buf, 10, 0);
    if (ret != 10) {
        LOG_ERR("glfs_write", errno);
        ret = -1;
        goto out;
    }

    /* write should fail */
    ret = glfs_write(fd2, buf, 10, 0);
    if (ret != -1) {
        LOG_ERR("glfs_write", errno);
        ret = -1;
        goto out;
    }

    /* preempt mandatory lock from client 1*/
    ret = glfs_file_lock(fd2, F_SETLKW, &lock, GLFS_LK_MANDATORY);
    if (ret) {
        LOG_ERR("glfs_file_lock", errno);
        goto out;
    }

    /* write should succeed from client 2 */
    ret = glfs_write(fd2, buf, 10, 0);
    if (ret == -1) {
        LOG_ERR("glfs_write", errno);
        goto out;
    }

    /* write should fail from client 1 */
    ret = glfs_write(fd1, buf, 10, 0);
    if (ret == 10) {
        LOG_ERR("glfs_write", errno);
        ret = -1;
        goto out;
    }

    ret = 0;

out:
    if (fd1) {
        glfs_close(fd1);
    }

    if (fd2) {
        glfs_close(fd2);
    }

    return ret;
}

int
main(int argc, char *argv[])
{
    int ret = 0;
    glfs_t *fs1 = NULL;
    glfs_t *fs2 = NULL;
    char *volname = NULL;
    char log_file[100];
    char *hostname = NULL;
    char *fname = "/file";
    glfs_fd_t *fd1 = NULL;
    glfs_fd_t *fd2 = NULL;

    if (argc != 4) {
        fprintf(
            stderr,
            "Expect following args %s <hostname> <Vol> <log file location>\n",
            argv[0]);
        return -1;
    }

    hostname = argv[1];
    volname = argv[2];

    sprintf(log_file, "%s/%s", argv[3], "fence-basic.log");
    fp = fopen(log_file, "w");
    if (!fp) {
        fprintf(stderr, "\nfailed to open %s\n", log_file);
        fflush(stderr);
        return -1;
    }

    sprintf(log_file, "%s/%s", argv[3], "glfs-client-1.log");
    fs1 = setup_new_client(hostname, volname, log_file, 0);
    if (!fs1) {
        LOG_ERR("setup_new_client", errno);
        return -1;
    }

    sprintf(log_file, "%s/%s", argv[3], "glfs-client-2.log");
    fs2 = setup_new_client(hostname, volname, log_file, 0);
    if (!fs2) {
        LOG_ERR("setup_new_client", errno);
        ret = -1;
        goto error;
    }

    ret = test(fs1, fs2, fname);

error:
    if (fs1) {
        glfs_fini(fs1);
    }

    if (fs2) {
        glfs_fini(fs2);
    }

    fclose(fp);

    return ret;
}
