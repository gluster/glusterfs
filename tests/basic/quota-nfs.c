#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int
file_write (char *filename, int filesize)
{
        int fd, ret = 0;
        int i = 0;
        char buf[1024] = {'a',};
        fd = open (filename, O_RDWR|O_CREAT|O_APPEND, 0600);
        while (i < filesize) {
                ret = write(fd, buf, sizeof(buf));
                if (ret == -1) {
                        close (fd);
                        return ret;
                }
                i += sizeof(buf);
                ret = fdatasync(fd);
                if (ret) {
                        close (fd);
                        return ret;
                }
        }
        ret = close(fd);
        if (ret)
                return ret;

        return 0;
}

int
main (int argc, char **argv)
{
        if (argc != 3) {
                printf("Usage: %s <filename> <size(in bytes)>\n", argv[0]);
                return EXIT_FAILURE;
        }

        printf ("argv[2] is %s\n", argv[2]);
        if (file_write (argv[1], atoi(argv[2])) == -1)
                return EXIT_FAILURE;

        return EXIT_SUCCESS;
}
