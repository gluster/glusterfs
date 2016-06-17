#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WRITE_SIZE (128)

glfs_t *
setup_new_client(char *hostname, char *volname, char *log_fileile)
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

        ret = glfs_set_logging (fs, log_fileile, 7);
        if (ret < 0) {
                fprintf (stderr, "\nglfs_set_logging failed with ret: %d (%s)\n",
                                 ret, strerror (errno));
                goto error;
        }

        ret = glfs_init (fs);
        if (ret < 0) {
                fprintf (stderr, "\nglfs_init failed with ret: %d (%s)\n",
                                  ret, strerror (errno));
                goto error;
        }
        return fs;
error:
        return NULL;
}

int
write_something (glfs_t *fs)
{
        glfs_fd_t *fd       = NULL;
        char      *buf      = NULL;
        int       ret       = 0;
        int       j         = 0;

        fd = glfs_creat (fs, "filename", O_RDWR, 0644);
        if (!fd) {
                fprintf (stderr, "%s: (%p) %s\n", "filename", fd,
                        strerror (errno));
                return -1;
        }

        buf = (char *) malloc (WRITE_SIZE);
        memset (buf, '-', WRITE_SIZE);

        for (j = 0; j < 4; j++) {
                ret = glfs_write (fd, buf, WRITE_SIZE, 0);
                if (ret < 0) {
                        fprintf (stderr, "Write(%s): %d (%s)\n", "filename", ret,
                                strerror (errno));
                        return ret;
                }
                glfs_lseek (fd, 0, SEEK_SET);
        }
        return 0;
}

static int
volfile_change (const char *volname) {
        int   ret = 0;
        char *cmd = NULL, *cmd1 = NULL;

        ret = asprintf (&cmd, "gluster volume set %s quick-read on",
                        volname);
        if (ret < 0) {
                fprintf (stderr, "cannot construct cli command string (%s)",
                         strerror (errno));
                return ret;
        }

        ret = asprintf (&cmd1, "gluster volume set %s quick-read off",
                        volname);
        if (ret < 0) {
                fprintf (stderr, "cannot construct cli command string (%s)",
                         strerror (errno));
                return ret;
        }

        ret = system (cmd);
        if (ret < 0) {
                fprintf (stderr, "quick-read off on (%s) failed", volname);
                return ret;
        }

        ret = system (cmd1);
        if (ret < 0) {
                fprintf (stderr, "quick-read on on (%s) failed", volname);
                return ret;
        }

        ret = system (cmd);
        if (ret < 0) {
                fprintf (stderr, "quick-read off on (%s) failed", volname);
                return ret;
        }

        free (cmd);
        free (cmd1);
        return ret;
}

int
main (int argc, char *argv[])
{
        int        ret      = 0;
        glfs_t    *fs       = NULL;
	char       buf[100];
        glfs_fd_t *fd       = NULL;

        if (argc != 4) {
                fprintf (stderr,
                                "Expect following args %s <hostname> <Vol> <log file location>\n"
                                , argv[0]);
                return -1;
        }

        fs = setup_new_client (argv[1], argv[2], argv[3]);
	if (!fs)
		goto error;

        ret = volfile_change (argv[2]);
        if (ret < 0)
                goto error;

        /* This is required as volfile change takes a while to reach this
         * gfapi client and precess the graph change. Without this the issue
         * cannot be reproduced as in cannot be tested.
         */
        sleep (10);

	ret = write_something (fs);
        if (ret < 0)
                goto error;

        ret = glfs_fini (fs);
        if (ret < 0) {
                fprintf (stderr, "glfs_fini failed with ret: %d (%s)\n",
                         ret, strerror (errno));
                goto error;
        }

        return 0;
error:
        return -1;
}
