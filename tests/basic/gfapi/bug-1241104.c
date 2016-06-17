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
#include <sys/wait.h>

int gfapi = 1;

#define LOG_ERR(func, ret) do { \
        if (ret != 0) {            \
                fprintf (stderr, "%s : returned error %d (%s)\n", \
                         func, ret, strerror (errno)); \
                goto out; \
        } else { \
                fprintf (stderr, "%s : returned %d\n", func, ret); \
        } \
        } while (0)

int
main (int argc, char *argv[])
{
        glfs_t    *fs = NULL;
        int        ret = 0, i, status = 0;
        glfs_fd_t *fd = NULL;
        char      *filename = "file_tmp";
        char      *volname = NULL;
        char      *logfile = NULL;
        char      *hostname = NULL;
        struct flock lock = {0, };

        if (argc != 4) {
                fprintf (stderr, "Invalid argument\n");
                exit(1);
        }

        hostname = argv[1];
        volname = argv[2];
        logfile = argv[3];

        fs = glfs_new (volname);
        if (!fs) {
                fprintf (stderr, "glfs_new: returned NULL\n");
                return -1;
        }

        ret = glfs_set_volfile_server (fs, "tcp", hostname, 24007);
        LOG_ERR("glfs_set_volfile_server", ret);

        ret = glfs_set_logging (fs, logfile, 7);
        LOG_ERR("glfs_set_logging", ret);

        ret = glfs_init (fs);
        LOG_ERR("glfs_init", ret);

        fd = glfs_creat(fs, filename, O_RDWR|O_SYNC, 0644);
        if (fd <= 0) {
                ret = -1;
                LOG_ERR ("glfs_creat", ret);
        }
        fprintf (stderr, "glfs-create fd - %d\n", fd);

	/* validate locks for negative range */
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 10;
        lock.l_len = -9;

        ret = glfs_posix_lock (fd, F_SETLK, &lock);
        LOG_ERR ("glfs_posix_lock", ret);

err:
        glfs_close(fd);
        LOG_ERR ("glfs_close", ret);

out:
        if (fs) {
                ret = glfs_fini(fs);
                fprintf (stderr, "glfs_fini(fs) returned %d \n", ret);
        }

        if (ret)
                exit(1);
        exit(0);
}


