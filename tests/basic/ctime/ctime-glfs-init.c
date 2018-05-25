#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

#define LOG_ERR(msg) do { \
        fprintf (stderr, "%s : Error (%s)\n", msg, strerror (errno)); \
        } while (0)

int
main (int argc, char *argv[])
{
        int         ret      = 0;
        char       *hostname = NULL;
        char       *volname  = NULL;
        char       *logfile  = NULL;
        glfs_t     *fs       = NULL;

        if (argc != 4) {
                fprintf (stderr, "Invalid argument\n");
                exit(1);
        }

        hostname = argv[1];
        volname = argv[2];
        logfile = argv[3];

        fs = glfs_new (volname);
        if (!fs) {
                LOG_ERR ("glfs_new failed");
                return -1;
        }

        ret = glfs_set_volfile_server (fs, "tcp", hostname, 24007);
        if (ret < 0) {
                LOG_ERR ("glfs_set_volfile_server failed");
                goto err;
        }

        ret = glfs_set_logging (fs, logfile, 7);
        if (ret < 0) {
                LOG_ERR ("glfs_set_logging failed");
                goto err;
        }

        ret = glfs_init (fs);
        if (ret < 0) {
                LOG_ERR ("glfs_init failed");
                goto err;
        }

        glfs_fini (fs);
        fs = NULL;
        return 0;
err:
        glfs_fini (fs);
        fs = NULL;

        return -1;
}
