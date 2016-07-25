#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

#define LOG_ERR(func, ret) do { \
        if (ret != 0) {            \
                fprintf (stderr, "%s : returned error %d\n", func, ret); \
                exit(1); \
        } else { \
                fprintf (stderr, "%s : returned %d\n", func, ret); \
        } \
} while (0)

int
main (int argc, char *argv[])
{
        glfs_t    *fs = NULL;
        int        ret = 0, i;
        glfs_fd_t *fd = NULL;
        char       readbuf[32];
        char      *logname = NULL;
        char      *hostname = NULL;
        char      *volname = NULL;

        fprintf (stderr, "Starting libgfapi_fini\n");

        if (argc < 4) {
                fprintf (stderr, "Invalid argument\n");
                exit(1);
        }

        hostname = argv[1];
        volname = argv[2];
        logname = argv[3];

        fs = glfs_new (volname);
        if (!fs) {
                fprintf (stderr, "glfs_new: returned NULL\n");
                exit(1);
        }

        ret = glfs_set_volfile_server (fs, "tcp", hostname, 0);
        LOG_ERR("glfs_set_volfile_server", ret);

        ret = glfs_set_logging (fs, logname, 7);
        LOG_ERR("glfs_set_logging", ret);

        /* Do not call glfs_init.
         * glfs_fini() shouldn't hang in that case*/
        ret = glfs_fini(fs);
        LOG_ERR("glfs_fini", ret);
        fprintf (stderr, "End of libgfapi_fini\n");

        exit(0);
}


