#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

#include "glusterfs.h"
#include "fd.h"
#include "syncop.h"
#include "glfs-internal.h"

#define GFAPI_SUCCESS 0

extern int recon_execute (glfs_t *, char **, char **);

int
main (int argc, char **argv)
{
        glfs_t  *fs;
        int     ret;
        int     meta_fd         = (-1);
        char    *meta_buf       = NULL;
        int     data_fd         = (-1);
        char    *data_buf       = NULL;

        fs = glfs_new ("whocares");
        if (!fs) {
                fprintf (stderr, "glfs_new failed\n");
                return EXIT_FAILURE;
        }

        if (getenv("RECON_DEBUG")) {
                ret = glfs_set_logging (fs, "/dev/stderr", 7);
        }
        else {
                ret = glfs_set_logging (fs, "/dev/null", 0);
        }

        if (ret != GFAPI_SUCCESS) {
                fprintf (stderr, "glfs_set_logging failed (%d)\n", errno);
                return EXIT_FAILURE;
        }

        ret = glfs_set_volfile (fs, argv[1]);
        if (ret != GFAPI_SUCCESS) {
                fprintf (stderr, "glfs_set_volfile failed (%d)\n", errno);
                return EXIT_FAILURE;
        }

        ret = glfs_init (fs);
        if (ret != GFAPI_SUCCESS) {
                fprintf (stderr, "glfs_init failed (%d)\n", errno);
                return EXIT_FAILURE;
        }

        meta_fd = open (argv[2], O_RDONLY);
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

        data_fd = open (argv[3], O_RDONLY);
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
                if (!recon_execute(fs,&meta_buf,&data_buf)) {
                        break;
                }
        }

        return EXIT_SUCCESS;
}
