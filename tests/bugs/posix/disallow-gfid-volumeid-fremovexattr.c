#include <glusterfs/api/glfs.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main (int argc, char *argv[])
{
        glfs_t    *fs       = NULL;
        int       ret       = 0;
        int       i         = 0;
        glfs_fd_t *fd       = NULL;
        char      *logfile  = NULL;
        char      *hostname = NULL;

        if (argc != 4) {
                fprintf (stderr,
                        "Expect following args %s <hostname> <Vol> <log file>\n"
                        , argv[0]);
                return -1;
        }

        hostname = argv[1];
        logfile = argv[3];

        fs = glfs_new (argv[2]);
        if (!fs) {
                fprintf (stderr, "glfs_new: returned NULL (%s)\n",
                        strerror (errno));
                return -1;
        }

        ret = glfs_set_volfile_server (fs, "tcp", hostname, 24007);
        if (ret < 0) {
                fprintf (stderr, "glfs_set_volfile_server failed ret:%d (%s)\n",
                ret, strerror (errno));
                return -1;
        }

        ret = glfs_set_logging (fs, logfile, 7);
        if (ret < 0) {
                fprintf (stderr, "glfs_set_logging failed with ret: %d (%s)\n",
                        ret, strerror (errno));
                return -1;
        }

        ret = glfs_init (fs);
        if (ret < 0) {
                fprintf (stderr, "glfs_init failed with ret: %d (%s)\n",
                        ret, strerror (errno));
                return -1;
        }

        fd = glfs_opendir (fs, "/");
        if (!fd) {
                fprintf (stderr, "glfs_opendir failed with (%s)\n",
                         strerror (errno));
                return -1;
        }

        ret = glfs_fremovexattr (fd, "trusted.gfid");
        if (ret == 0 || errno != EPERM) {
                fprintf (stderr, "glfs_fremovexattr gfid exited with ret: "
                         "%d (%s)\n", ret, strerror (errno));
                return -1;
        }

        ret = glfs_fremovexattr (fd, "trusted.glusterfs.volume-id");
        if (ret == 0 || errno != EPERM) {
                fprintf (stderr, "glfs_fremovexattr volume-id exited with ret: "
                         "%d (%s)\n", ret, strerror (errno));
                return -1;
        }

        ret = glfs_fsetxattr (fd, "trusted.abc", "abc", 3, 0);
        if (ret < 0) {
                fprintf (stderr, "glfs_fsetxattr trusted.abc exited with ret: "
                         "%d (%s)\n", ret, strerror (errno));
                return -1;
        }

        ret = glfs_fremovexattr (fd, "trusted.abc");
        if (ret < 0) {
                fprintf (stderr, "glfs_fremovexattr trusted.abc exited with "
                         "ret: %d (%s)\n", ret, strerror (errno));
                return -1;
        }

        (void) glfs_closedir(fd);
        ret = glfs_fini (fs);
        if (ret < 0) {
                fprintf (stderr, "glfs_fini failed with ret: %d (%s)\n",
                        ret, strerror (errno));
                return -1;
        }
        return 0;
}
