#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main (int argc, char **argv)
{
        char *file = NULL;
        int fd = -1;
        char *buffer = NULL;
        size_t buf_size = 0;
        size_t written = 0;
        int ret = 0;
        off_t offset = 0;
        int i = 0;
        int loop_count = 5;

        if (argc < 2) {
                printf ("Usage:%s <filename>\n", argv[0]);
                return -1;
        }

        file = argv[1];
        buf_size = 1024;
        buffer = calloc(1, buf_size);
        if (!buffer) {
                perror("calloc");
                return -1;
        }
        memset (buffer, 'R', buf_size);

        fd = open(file, O_WRONLY);
        if (fd == -1) {
                perror("open");
                return -1;
        }

        for (i = 0; i < loop_count; i++) {
                ret =  write (fd, buffer, buf_size);
                if (ret == -1) {
                        perror("write");
                        return ret;
                } else {
                        written += ret;
                }
                offset = lseek (fd, 0 , SEEK_SET);
        }

        free(buffer);
        return 0;

}
