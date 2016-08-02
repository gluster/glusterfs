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

#define LOG_ERR(msg) do { \
        fprintf (stderr, "%s : Error (%s)\n", msg, strerror (errno)); \
        } while (0)

glfs_t *
init_glfs (const char *hostname, const char *volname,
           const char *logfile)
{
        int     ret     = -1;
        glfs_t *fs      = NULL;

        fs = glfs_new (volname);
        if (!fs) {
                LOG_ERR ("glfs_new failed");
                return NULL;
        }

        ret = glfs_set_volfile_server (fs, "tcp", hostname, 24007);
        if (ret < 0) {
                LOG_ERR ("glfs_set_volfile_server failed");
                goto out;
        }

        ret = glfs_set_logging (fs, logfile, 7);
        if (ret < 0) {
                LOG_ERR ("glfs_set_logging failed");
                goto out;
        }

        ret = glfs_init (fs);
        if (ret < 0) {
                LOG_ERR ("glfs_init failed");
                goto out;
        }

        ret = 0;
out:
        if (ret) {
                glfs_fini (fs);
                fs = NULL;
        }

        return fs;
}

int
glfs_test_function (const char *hostname, const char *volname,
                    const char *logfile)
{
        int             ret      = -1;
        int             flags    = O_CREAT | O_RDWR;
        glfs_t         *fs       = NULL;
        glfs_fd_t      *glfd     = NULL;
        const char     *buff     = "This is from my prog\n";
        const char     *filename = "glfs_test.txt";

        fs = init_glfs (hostname, volname, logfile);
        if (fs == NULL) {
                LOG_ERR ("init_glfs failed");
                return -1;
        }

        glfd = glfs_creat (fs, filename, flags, 0644);
        if (glfd == NULL) {
                LOG_ERR ("glfs_creat failed");
                goto out;
        }

        ret = glfs_write (glfd, buff, strlen (buff), flags);
        if (ret < 0) {
                LOG_ERR ("glfs_write failed");
                goto out;
        }

        ret = glfs_close (glfd);
        if (ret < 0) {
                LOG_ERR ("glfs_write failed");
                goto out;
        }

out:
        ret = glfs_fini (fs);
        if (ret) {
                LOG_ERR ("glfs_fini failed");
        }

        return ret;
}

int
main (int argc, char *argv[])
{
        int   ret = 0;
        char *hostname = NULL;
        char *volname = NULL;
        char *logfile = NULL;

        if (argc != 4) {
                fprintf (stderr, "Invalid argument\n");
                exit(1);
        }

        hostname = argv[1];
        volname = argv[2];
        logfile = argv[3];

        ret = glfs_test_function (hostname, volname, logfile);
        if (ret) {
                LOG_ERR ("glfs_test_function failed");
        }

        return ret;
}


