#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char *argv[])
{
        int ret = EXIT_FAILURE;
        int fd = open(argv[1], O_RDONLY|O_TRUNC);

        if (fd) {
                ret = EXIT_SUCCESS;
                close(fd);
        }

        return ret;
}
