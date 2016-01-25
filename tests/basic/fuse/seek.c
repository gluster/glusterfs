/* seek.c - use lseek() to find holes in a file
 *
 * Author: Niels de Vos <ndevos@redhat.com>
 */

/* needed for SEEK_HOLE/SEEK_DATA */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main (int argc, char **argv)
{
        int          ret        = EXIT_SUCCESS;
        int          fd         = -1;
        char        *filename   = NULL;
        struct stat  st         = { 0, };
        off_t        hole_start = 0;
        off_t        hole_end   = 0;

        if (argc != 2) {
                fprintf (stderr, "Invalid argument, use %s <file>\n", argv[0]);
                return EXIT_FAILURE;
        }

        filename = argv[1];

        fd = open (filename, O_RDONLY);
        if (fd <= 0) {
                perror ("open");
                return EXIT_FAILURE;
        }

        if (fstat (fd, &st)) {
                perror ("fstat");
                return EXIT_FAILURE;
        }

        while (hole_end < st.st_size) {
                hole_start = lseek (fd, hole_end, SEEK_HOLE);
                if (hole_start == -1 && errno == ENXIO) {
                        /* no more holes */
                        break;
                } else if (hole_start == -1 && errno == ENOTSUP) {
                        /* SEEK_HOLE is not supported */
                        perror ("lseek(SEEK_HOLE)");
                        ret = EXIT_FAILURE;
                        break;
                } else if (hole_start == -1) {
			perror ("no more holes");
			break;
		}

                hole_end = lseek (fd, hole_start, SEEK_DATA);
                if (hole_end == -1 && errno == ENXIO) {
                        /* no more data */
                        break;
                } else if (hole_end == -1 && errno == ENOTSUP) {
                        /* SEEK_DATA is not supported */
                        perror ("lseek(SEEK_DATA)");
                        ret = EXIT_FAILURE;
                        break;
                }

                printf ("HOLE found: %ld - %ld%s\n", hole_start, hole_end,
                         (hole_end == st.st_size) ? " (EOF)" : "");
        }

        close (fd);

        return ret;
}
