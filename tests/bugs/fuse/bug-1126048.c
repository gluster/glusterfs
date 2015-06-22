#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

/*
 * This function opens a file and to trigger migration failure, unlinks the
 * file and performs graph switch (cmd passed in argv). If everything goes fine,
 * fsync should fail without crashing the mount process.
 */
int
main (int argc, char **argv)
{
        int ret = 0;
        int fd = 0;
        char    *cmd = argv[1];
        struct stat stbuf = {0, };

        printf ("cmd is: %s\n", cmd);
        fd = open("a.txt", O_CREAT|O_RDWR, 0644);
        if (fd < 0)
                printf ("open failed: %s\n", strerror(errno));

        ret = unlink("a.txt");
        if (ret < 0)
                printf ("unlink failed: %s\n", strerror(errno));
        if (write (fd, "abc", 3) < 0)
                printf ("Not able to print %s\n", strerror (errno));
        system(cmd);
        sleep(1); /* No way to confirm graph switch so sleep 1 */
        ret = fstat (fd, &stbuf);
        if (ret < 0)
                printf ("fstat failed %\n", strerror (errno));
        ret = fsync(fd);
        if (ret < 0)
                printf ("Not able to fsync %s\n", strerror (errno));
        return 0;
}
