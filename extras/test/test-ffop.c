#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <attr/xattr.h>
#include <errno.h>
#include <string.h>

int
main (int argc, char *argv[])
{
        int          ret      = -1;
        int          fd       = 0;
        char        *filename = NULL;
        struct stat  stbuf    = {0,};

        if (argc > 1)
                filename = argv[1];

        if (!filename)
                filename = "temp-xattr-test-file";

        fd = open (filename, O_RDWR|O_CREAT);
        if (fd < 0) {
                fd = 0;
                fprintf (stderr, "open failed : %s\n", strerror (errno));
                goto out;
        }

        ret = unlink (filename);
        if (ret < 0) {
                fprintf (stderr, "unlink failed : %s\n", strerror (errno));
                goto out;
        }

        ret = ftruncate (fd, 0);
        if (ret < 0) {
                fprintf (stderr, "ftruncate failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fstat (fd, &stbuf);
        if (ret < 0) {
                fprintf (stderr, "fstat failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fchmod (fd, 0640);
        if (ret < 0) {
                fprintf (stderr, "fchmod failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fchown (fd, 10001, 10001);
        if (ret < 0) {
                fprintf (stderr, "fchown failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fsync (fd);
        if (ret < 0) {
                fprintf (stderr, "fsync failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fsetxattr (fd, "trusted.xattr-test", "working", 8, 0);
        if (ret < 0) {
                fprintf (stderr, "fsetxattr failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fdatasync (fd);
        if (ret < 0) {
                fprintf (stderr, "fdatasync failed : %s\n", strerror (errno));
                goto out;
        }

        ret = flistxattr (fd, NULL, 0);
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "flistxattr failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fgetxattr (fd, "trusted.xattr-test", NULL, 0);
        if (ret <= 0) {
                ret = -1;
                fprintf (stderr, "fgetxattr failed : %s\n", strerror (errno));
                goto out;
        }

        ret = fremovexattr (fd, "trusted.xattr-test");
        if (ret < 0) {
                fprintf (stderr, "fremovexattr failed : %s\n", strerror (errno));
                goto out;
        }

        ret = 0;
out:
        if (fd)
                close (fd);

        return ret;
}
