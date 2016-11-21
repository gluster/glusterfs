#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NO_INIT 1

int count = 0;
void
read_cbk (glfs_fd_t *fd, ssize_t ret, void *data) {
count++;
}

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
        glfs_t    *fs          = NULL;
        struct glfs_fd *fd     = NULL;
        char      *volname     = NULL;
        char      *log_file    = NULL;
        char      *hostname    = NULL;
        char      *buf = NULL;
        struct stat stat;

        if (argc != 4) {
                fprintf (stderr,
                                "Expect following args %s <hostname> <Vol> <log file location>\n"
                                , argv[0]);
                return -1;
        }

        hostname = argv[1];
        volname = argv[2];
        log_file = argv[3];

        fs = setup_new_client (hostname, volname, log_file, 0);
        if (!fs) {
                fprintf (stderr, "\nsetup_new_client: returned NULL (%s)\n",
                                 strerror (errno));
                goto error;
        }

        fd = glfs_opendir (fs, "/");
        if (!fd) {
                fprintf (stderr, "/: %s\n", strerror (errno));
                return -1;
        }

        glfs_readdirplus (fd, &stat);

        fd = glfs_open (fs, "/test", O_RDWR);
        if (fd == NULL) {
                fprintf (stderr, "glfs_open: returned NULL\n");
                goto error;
        }

        buf = (char *) malloc (5);

        ret = glfs_pread (fd, buf, 5, 0, 0);
        if (ret < 0) {
                fprintf (stderr, "Read(%s): %d (%s)\n", "test", ret,
                         strerror (errno));
                return ret;
        }

        free (buf);
        glfs_close (fd);

        ret = glfs_fini (fs);
        if (ret < 0) {
                fprintf (stderr, "glfs_fini failed with ret: %d (%s)\n",
                         ret, strerror (errno));
                return -1;
        }

        return 0;
error:
        return -1;
}
