#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define NO_INIT 1

glfs_t *
setup_new_client(char *hostname, char *volname, char *log_file, int flag)
{
        int ret = 0;
        glfs_t *fs = NULL;

        fs = glfs_new (volname);
        if (!fs) {
                fprintf (stderr, "\nglfs_new: returned NULL (%s)\n",
                                 strerror (errno));
                goto error;
        }

        ret = glfs_set_volfile_server (fs, "tcp", hostname, 24007);
        if (ret < 0) {
                fprintf (stderr, "\nglfs_set_volfile_server failed ret:%d (%s)\n",
                                 ret, strerror (errno));
                goto error;
        }

        ret = glfs_set_logging (fs, log_file, 7);
        if (ret < 0) {
                fprintf (stderr, "\nglfs_set_logging failed with ret: %d (%s)\n",
                                 ret, strerror (errno));
                goto error;
        }

        if (flag == NO_INIT)
                goto out;

        ret = glfs_init (fs);
        if (ret < 0) {
                fprintf (stderr, "\nglfs_init failed with ret: %d (%s)\n",
                                  ret, strerror (errno));
                goto error;
        }

out:
        return fs;
error:
        return NULL;
}

int
main (int argc, char *argv[])
{
        int        ret         = 0;
        glfs_t    *fs1         = NULL;
        glfs_t    *fs2         = NULL;
        glfs_t    *fs3         = NULL;
        char      *volname     = NULL;
        char      *log_file    = NULL;
        char      *hostname    = NULL;

        if (argc != 4) {
                fprintf (stderr,
                                "Expect following args %s <hostname> <Vol> <log file location>\n"
                                , argv[0]);
                return -1;
        }

        hostname = argv[1];
        volname = argv[2];
        log_file = argv[3];

        fs1 = setup_new_client (hostname, volname, log_file, NO_INIT);
        if (!fs1) {
                fprintf (stderr, "\nsetup_new_client: returned NULL (%s)\n",
                                 strerror (errno));
                goto error;
        }

        fs2 = setup_new_client (hostname, volname, log_file, 0);
        if (!fs2) {
                fprintf (stderr, "\nsetup_new_client: returned NULL (%s)\n",
                                 strerror (errno));
                goto error;
        }

        fs3 = setup_new_client (hostname, volname, log_file, 0);
        if (!fs3) {
                fprintf (stderr, "\nsetup_new_client: returned NULL (%s)\n",
                                 strerror (errno));
                goto error;
        }

        ret = glfs_fini (fs3);
        if (ret < 0) {
                fprintf (stderr, "glfs_fini failed with ret: %d (%s)\n",
                         ret, strerror (errno));
                goto error;
        }

        /* The crash is seen in gf_log_flush_timeout_cbk(), and this gets
         * triggered when 30s timer expires, hence the sleep of 31s
         */
        sleep (31);
        ret = glfs_fini (fs2);
        if (ret < 0) {
                fprintf (stderr, "glfs_fini failed with ret: %d (%s)\n",
                         ret, strerror (errno));
                goto error;
        }

        ret = glfs_init (fs1);
        if (ret < 0) {
                fprintf (stderr, "\nglfs_init failed with ret: %d (%s)\n",
                                  ret, strerror (errno));
                goto error;
        }

        ret = glfs_fini (fs1);
        if (ret < 0) {
                fprintf (stderr, "glfs_fini failed with ret: %d (%s)\n",
                         ret, strerror (errno));
                goto error;
        }

        return 0;
error:
        return -1;
}
