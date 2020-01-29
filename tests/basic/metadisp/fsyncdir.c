#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fcntl.h>

int
main(int argc, char **argv)
{
    int pfd;

    pfd = open(argv[1], O_RDONLY | O_DIRECTORY);
    if (pfd == (-1)) {
        perror("open");
        return EXIT_FAILURE;
    }

    if (rename(argv[2], argv[3]) == (-1)) {
        perror("rename");
        return EXIT_FAILURE;
    }

    if (fsync(pfd) == (-1)) {
        perror("fsync");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
