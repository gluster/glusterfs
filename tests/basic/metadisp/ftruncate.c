#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fcntl.h>

int
main(int argc, char **argv)
{
    int pfd;

    pfd = open(argv[1], O_RDWR);
    if (pfd == (-1)) {
        perror("open");
        return EXIT_FAILURE;
    }

    if (ftruncate(pfd, 0) == (-1)) {
        perror("ftruncate");
        return EXIT_FAILURE;
    }

    if (write(pfd, "hello", 5) == (-1)) {
        perror("write");
        return EXIT_FAILURE;
    }

    if (fsync(pfd) == (-1)) {
        perror("fsync");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
