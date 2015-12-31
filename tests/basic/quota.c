#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

ssize_t
nwrite (int fd, const void *buf, size_t count)
{
        ssize_t  ret        = 0;
        ssize_t  written    = 0;

        for (written = 0; written != count; written += ret) {
                ret = write (fd, buf + written, count - written);
                if (ret < 0) {
                        if (errno == EINTR)
                                ret = 0;
                        else
                                goto out;
                }
        }

        ret = written;
out:
        return ret;
}

int
file_write (char *filename, int bs, int count)
{
        int  fd              = 0;
        int  ret             = -1;
        int  i               = 0;
        char *buf            = NULL;

        bs = bs * 1024;

        buf = (char *) malloc (bs);
        if (buf == NULL)
                goto out;

        memset (buf, 0, bs);

        fd = open (filename, O_RDWR|O_CREAT|O_SYNC, 0600);
        while (i < count) {
                ret = nwrite(fd, buf, bs);
                if (ret == -1) {
                        close (fd);
                        goto out;
                }
                i++;
        }

        ret = fdatasync(fd);
        if (ret) {
                close (fd);
                goto out;
        }

        ret = close(fd);
        if (ret)
                goto out;

        ret = 0;

out:
        if (buf)
                free (buf);
        return ret;
}

int
main (int argc, char **argv)
{
        if (argc != 4) {
                printf("Usage: %s <filename> <block size in k> <count>\n",
                        argv[0]);
                return EXIT_FAILURE;
        }

        if (file_write (argv[1], atoi(argv[2]), atoi(argv[3])) < 0) {
                perror ("write failed");
                return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
}
