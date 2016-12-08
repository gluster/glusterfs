#define _GNU_SOURCE

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

int
main (int argc, char *argv[])
{
        char        *filename = NULL, *volname = NULL, *cmd = NULL;
        char  buffer[1024]    = {0, };
        int          fd       = -1;
        int          ret      = -1;
        struct stat  statbuf  = {0, };

        if (argc != 3) {
                fprintf (stderr, "usage: %s <file-name> <volname>\n", argv[0]);
                goto out;
        }

        filename = argv[1];
        volname = argv[2];

        fd = open (filename, O_RDWR | O_CREAT, 0);
        if (fd < 0) {
                fprintf (stderr, "open (%s) failed (%s)\n", filename,
                         strerror (errno));
                goto out;
        }

        ret = write (fd, "test-content", 12);
        if (ret < 0) {
                fprintf (stderr, "write failed (%s)", strerror (errno));
                goto out;
        }

        ret = fsync (fd);
        if (ret < 0) {
                fprintf (stderr, "fsync failed (%s)", strerror (errno));
                goto out;
        }

        ret = fstat (fd, &statbuf);
        if (ret < 0) {
                fprintf (stderr, "fstat failed (%s)", strerror (errno));
                goto out;
        }

        ret = asprintf (&cmd, "gluster --mode=script volume stop %s force",
                        volname);
        if (ret < 0) {
                fprintf (stderr, "cannot construct cli command string (%s)",
                         strerror (errno));
                goto out;
        }

        ret = system (cmd);
        if (ret < 0) {
                fprintf (stderr, "stopping volume (%s) failed", volname);
                goto out;
        }

        sleep (3);

        ret = read (fd, buffer, 1024);
        if (ret >= 0) {
                fprintf (stderr, "read should've returned error, "
                         "but is successful\n");
                ret = -1;
                goto out;
        }

        ret = 0;
out:
        return ret;
}
