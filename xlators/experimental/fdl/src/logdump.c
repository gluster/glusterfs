#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

extern int fdl_dump (char **, char **);

int
main (int argc, char **argv)
{
        int     meta_fd         = (-1);
        char    *meta_buf       = NULL;
        int     data_fd         = (-1);
        char    *data_buf       = NULL;

        meta_fd = open (argv[1], O_RDONLY);
        if (meta_fd < 0) {
                perror ("open");
                return EXIT_FAILURE;
        }

        /* TBD: get proper length */
        meta_buf = mmap (NULL, 1048576, PROT_READ, MAP_PRIVATE, meta_fd, 0);
        if (meta_buf == MAP_FAILED) {
                perror ("mmap");
                return EXIT_FAILURE;
        }

        data_fd = open (argv[2], O_RDONLY);
        if (data_fd < 0) {
                perror ("open");
                return EXIT_FAILURE;
        }

        /* TBD: get proper length */
        data_buf = mmap (NULL, 1048576, PROT_READ, MAP_PRIVATE, data_fd, 0);
        if (data_buf == MAP_FAILED) {
                perror ("mmap");
                return EXIT_FAILURE;
        }

        for (;;) {
                if (!fdl_dump(&meta_buf,&data_buf)) {
                        break;
                }
        }

        return EXIT_SUCCESS;
}
