/* seek.c - use glfs_lseek() to find holes in a file
 *
 * Author: Niels de Vos <ndevos@redhat.com>
 */

/* needed for SEEK_HOLE/SEEK_DATA */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <api/glfs.h>
#include <api/glfs-handles.h>

int
main (int argc, char **argv)
{
        glfs_t      *fs       = NULL;
        int          ret      = 0;
        glfs_fd_t   *fd       = NULL;
        char        *filename = NULL;
        char        *volname  = NULL;
        struct stat  st       = { 0, };
        off_t        hole_start = 0;
        off_t        hole_end   = 0;

        if (argc != 3) {
                fprintf (stderr, "Invalid argument, use %s <vol> <file>\n",
                                 argv[0]);
                exit (1);
        }

        volname = argv[1];
        filename = argv[2];

        fs = glfs_new (volname);
        if (!fs) {
                perror ("glfs_new() returned NULL");
                return 1;
        }

        if (glfs_set_volfile_server (fs, "tcp", "localhost", 24007)) {
                perror ("glfs_set_volfile_server");
                return 1;
        }

        if (glfs_init (fs)) {
                perror ("glfs_init");
                return 1;
        }

        fd = glfs_open (fs, filename, O_RDONLY);
        if (fd <= 0) {
                perror ("glfs_open");
                return 1;
        }

        if (glfs_fstat (fd, &st)) {
                perror ("glfs_fstat");
                return 1;
        }

        while (hole_end < st.st_size) {
                hole_start = glfs_lseek (fd, hole_end, SEEK_HOLE);
                if (hole_start == -1 && errno == ENXIO)
                        /* no more holes */
                        break;
                if (hole_start == -1) {
			perror ("no more holes");
			break;
		}

                hole_end = glfs_lseek (fd, hole_start, SEEK_DATA);
                if (hole_end == -1 && errno == ENXIO) {
                        /* no more data */
                        break;
                }

                printf ("HOLE found: %ld - %ld%s\n", hole_start, hole_end,
                         (hole_end == st.st_size) ? " (EOF)" : "");
        }

        glfs_close (fd);

        if (fs) {
                glfs_fini (fs);
        }

        return ret;
}
