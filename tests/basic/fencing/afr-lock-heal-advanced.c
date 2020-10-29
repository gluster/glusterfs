#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

#define GF_ENFORCE_MANDATORY_LOCK "trusted.glusterfs.enforce-mandatory-lock"

FILE *logfile_fp;

#define LOG_ERR(func, err)                                                     \
    do {                                                                       \
        if (!logfile_fp) {                                                     \
            fprintf(stderr, "%\n%d %s : returned error (%s)\n", __LINE__,      \
                    func, strerror(err));                                      \
            fflush(stderr);                                                    \
        } else {                                                               \
            fprintf(logfile_fp, "\n%d %s : returned error (%s)\n", __LINE__,   \
                    func, strerror(err));                                      \
            fflush(logfile_fp);                                                \
        }                                                                      \
    } while (0)

glfs_t *
setup_client(char *hostname, char *volname, char *log_file)
{
    int ret = 0;
    glfs_t *fs = NULL;

    fs = glfs_new(volname);
    if (!fs) {
        fprintf(logfile_fp, "\nglfs_new: returned NULL (%s)\n",
                strerror(errno));
        goto error;
    }

    ret = glfs_set_volfile_server(fs, "tcp", hostname, 24007);
    if (ret < 0) {
        fprintf(logfile_fp, "\nglfs_set_volfile_server failed ret:%d (%s)\n",
                ret, strerror(errno));
        goto error;
    }

    ret = glfs_set_logging(fs, log_file, 7);
    if (ret < 0) {
        fprintf(logfile_fp, "\nglfs_set_logging failed with ret: %d (%s)\n",
                ret, strerror(errno));
        goto error;
    }

    ret = glfs_init(fs);
    if (ret < 0) {
        fprintf(logfile_fp, "\nglfs_init failed with ret: %d (%s)\n", ret,
                strerror(errno));
        goto error;
    }

out:
    return fs;
error:
    return NULL;
}

glfs_fd_t *
open_file(glfs_t *fs, char *fname)
{
    glfs_fd_t *fd = NULL;

    fd = glfs_creat(fs, fname, O_CREAT, 0644);
    if (!fd) {
        LOG_ERR("glfs_creat", errno);
        goto out;
    }
out:
    return fd;
}

int
acquire_mandatory_lock(glfs_t *fs, glfs_fd_t *fd)
{
    struct flock lock;
    int ret = 0;

    /* initialize lock */
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 100;

    ret = glfs_fsetxattr(fd, GF_ENFORCE_MANDATORY_LOCK, "set", 8, 0);
    if (ret < 0) {
        LOG_ERR("glfs_fsetxattr", errno);
        ret = -1;
        goto out;
    }

    ret = glfs_fd_set_lkowner(fd, fd, sizeof(fd));
    if (ret) {
        LOG_ERR("glfs_fd_set_lkowner", errno);
        ret = -1;
        goto out;
    }

    /* take a write mandatory lock */
    ret = glfs_file_lock(fd, F_SETLKW, &lock, GLFS_LK_MANDATORY);
    if (ret) {
        LOG_ERR("glfs_file_lock", errno);
        ret = -1;
        goto out;
    }

out:
    return ret;
}

int
perform_test(glfs_t *fs, char *file1, char *file2)
{
    int ret = 0;
    glfs_fd_t *fd1 = NULL;
    glfs_fd_t *fd2 = NULL;
    char *buf = "0123456789";

    fd1 = open_file(fs, file1);
    if (!fd1) {
        ret = -1;
        goto out;
    }
    fd2 = open_file(fs, file2);
    if (!fd2) {
        ret = -1;
        goto out;
    }

    /* Kill one brick from the .t.*/
    pause();

    ret = acquire_mandatory_lock(fs, fd1);
    if (ret) {
        goto out;
    }
    ret = acquire_mandatory_lock(fs, fd2);
    if (ret) {
        goto out;
    }

    /* Bring the brick up and let the locks heal. */
    pause();
    /*At this point, the .t would have killed and brought back 2 bricks, marking
     * the fd bad.*/

    ret = glfs_write(fd1, buf, 10, 0);
    if (ret > 0) {
        /* Write is supposed to fail with EBADFD*/
        LOG_ERR("glfs_write", ret);
        goto out;
    }

    ret = 0;
out:
    if (fd1)
        glfs_close(fd1);
    if (fd2)
        glfs_close(fd2);
    return ret;
}

static void
sigusr1_handler(int signo)
{
    /*Signal caught. Just continue with the execution.*/
}

int
main(int argc, char *argv[])
{
    int ret = 0;
    glfs_t *fs = NULL;
    char *volname = NULL;
    char log_file[100];
    char *hostname = NULL;
    char *fname1 = NULL;
    char *fname2 = NULL;

    if (argc != 7) {
        fprintf(stderr,
                "Expect following args %s <host> <volname> <file1> <file2> "
                "<log file "
                "location> <log_file_suffix>\n",
                argv[0]);
        return -1;
    }

    hostname = argv[1];
    volname = argv[2];
    fname1 = argv[3];
    fname2 = argv[4];

    /*Use SIGUSR1 and pause()as a means of hitting break-points this program
     *when signalled from the .t test case.*/
    if (signal(SIGUSR1, sigusr1_handler) == SIG_ERR) {
        LOG_ERR("SIGUSR1 handler error", errno);
        exit(EXIT_FAILURE);
    }

    sprintf(log_file, "%s/%s.%s.%s", argv[5], "lock-heal.c", argv[6], "log");
    logfile_fp = fopen(log_file, "w");
    if (!logfile_fp) {
        fprintf(stderr, "\nfailed to open %s\n", log_file);
        fflush(stderr);
        return -1;
    }

    sprintf(log_file, "%s/%s.%s.%s", argv[5], "glfs-client", argv[6], "log");
    fs = setup_client(hostname, volname, log_file);
    if (!fs) {
        LOG_ERR("setup_client", errno);
        return -1;
    }

    ret = perform_test(fs, fname1, fname2);

error:
    if (fs) {
        /*glfs_fini(fs)*/;  // glfs fini path is racy and crashes the program
    }

    fclose(logfile_fp);

    return ret;
}
