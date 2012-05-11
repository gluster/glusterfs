
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
        int          loop     = 0;
        struct stat  stbuf    = {0,};
        char         string[1024] = {0,};

        if (argc > 1)
                filename = argv[1];

        if (!filename)
                filename = "temp-fd-test-file";

        fd = open (filename, O_RDWR|O_CREAT|O_TRUNC);
        if (fd < 0) {
                fd = 0;
                fprintf (stderr, "open failed : %s\n", strerror (errno));
                goto out;
        }

        while (loop < 1000) {
                /* Use it as a mechanism to test time delays */
                memset (string, 0, 1024);
                scanf ("%s", string);

                ret = write (fd, string, strlen (string));
                if (ret != strlen (string)) {
                        fprintf (stderr, "write failed : %s (%s %d)\n",
                                 strerror (errno), string, loop);
                        goto out;
                }

                ret = write (fd, "\n", 1);
                if (ret != 1) {
                        fprintf (stderr, "write failed : %s (%d)\n",
                                 strerror (errno), loop);
                        goto out;
                }

                loop++;
        }

        fprintf (stdout, "finishing the test after %d loops\n", loop);

        ret = 0;
out:
        if (fd)
                close (fd);

        return ret;
}
