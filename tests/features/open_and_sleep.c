#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>

int
main(int argc, char **argv)
{
    pid_t pid;
    int fd;

    if (argc >= 2) {
        fd = open(argv[1], O_RDWR | O_CREAT, 0644);
        if (fd == -1) {
            fprintf(stderr, "cannot open/create %s\n", argv[1]);
            return 1;
        }
    }

    pid = getpid();
    printf("%d\n", pid);
    fflush(stdout);

    /* The test shouldn't take more than few seconds. If it takes too much
     * it's better to finish with an error. */
    sleep(30);

    return 1;
}
