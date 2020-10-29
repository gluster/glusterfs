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

int
acquire_mandatory_lock(glfs_t *fs, char *fname)
{
    struct flock lock;
    int ret = 0;
    glfs_fd_t *fd = NULL;

    fd = glfs_creat(fs, fname, O_CREAT, 0644);
    if (!fd) {
        if (errno != EEXIST) {
            LOG_ERR("glfs_creat", errno);
            ret = -1;
            goto out;
        }
        fd = glfs_open(fs, fname, O_RDWR | O_NONBLOCK);
        if (!fd) {
            LOG_ERR("glfs_open", errno);
            ret = -1;
            goto out;
        }
    }

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

    pause();

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
        goto out;
    }

    pause();

out:
    if (fd) {
        glfs_close(fd);
    }

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
    char *fname = NULL;

    if (argc != 6) {
        fprintf(stderr,
                "Expect following args %s <host> <volname> <file> <log file "
                "location> <log_file_suffix>\n",
                argv[0]);
        return -1;
    }

    hostname = argv[1];
    volname = argv[2];
    fname = argv[3];

    /*Use SIGUSR1 and pause()as a means of hitting break-points this program
     *when signalled from the .t test case.*/
    if (signal(SIGUSR1, sigusr1_handler) == SIG_ERR) {
        LOG_ERR("SIGUSR1 handler error", errno);
        exit(EXIT_FAILURE);
    }

    sprintf(log_file, "%s/%s.%s.%s", argv[4], "lock-heal-basic.c", argv[5],
            "log");
    logfile_fp = fopen(log_file, "w");
    if (!logfile_fp) {
        fprintf(stderr, "\nfailed to open %s\n", log_file);
        fflush(stderr);
        return -1;
    }

    sprintf(log_file, "%s/%s.%s.%s", argv[4], "glfs-client", argv[5], "log");
    fs = setup_client(hostname, volname, log_file);
    if (!fs) {
        LOG_ERR("setup_client", errno);
        return -1;
    }

    ret = acquire_mandatory_lock(fs, fname);

error:
    if (fs) {
        /*glfs_fini(fs)*/;  // glfs fini path is racy and crashes the program
    }

    fclose(logfile_fp);

    return ret;
}
