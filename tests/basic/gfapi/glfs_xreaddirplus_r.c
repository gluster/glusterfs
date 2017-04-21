#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

#define VALIDATE_AND_GOTO_LABEL_ON_ERROR(func, ret, label) do { \
        if (ret < 0) {            \
                fprintf (stderr, "%s : returned error %d (%s)\n", \
                         func, ret, strerror (errno)); \
                goto label; \
        } \
        } while (0)

#define MAX_FILES_CREATE 10
#define MAXPATHNAME      512

int
main (int argc, char *argv[])
{
        int                     ret = -1;
        glfs_t                 *fs = NULL;
        char                   *volname = NULL;
        char                   *logfile = NULL;
        char                   *hostname = NULL;
        char                   *my_file = "file_";
        char                    my_file_name[MAXPATHNAME];
        struct dirent           de;
        struct dirent          *pde = NULL;
        struct glfs_xreaddirp_stat   *xstat = NULL;
        uint32_t                rflags = (GFAPI_XREADDIRP_STAT |
                                          GFAPI_XREADDIRP_HANDLE);
        uint32_t                flags = O_RDWR|O_SYNC;
        struct glfs_fd         *fd = NULL;
        int                     i = 0;

        if (argc != 4) {
                fprintf (stderr, "Invalid argument\n");
                return 1;
        }

        hostname = argv[1];
        volname = argv[2];
        logfile = argv[3];

        fs = glfs_new (volname);
        if (!fs)
                VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_new", ret, out);

        ret = glfs_set_volfile_server (fs, "tcp", hostname, 24007);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_set_volfile_server", ret, out);

        ret = glfs_set_logging (fs, logfile, 7);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_set_logging", ret, out);

        ret = glfs_init (fs);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_init", ret, out);

        for (i = 0; i < MAX_FILES_CREATE; i++) {
                sprintf (my_file_name, "%s%d", my_file, i);

                fd = glfs_creat(fs, my_file_name, flags, 0644);
                if (fd == NULL) {
                        ret = -1;
                        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_creat", ret,
                                                          out);
                }


                glfs_close (fd);
        }

        /* XXX: measure performance and memory usage of this readdirp call */
        fd = glfs_opendir (fs, "/");

        ret = glfs_xreaddirplus_r(fd, rflags, &xstat, &de, &pde);
        while (ret > 0 && pde != NULL) {
                fprintf (stderr, "%s: %lu\n", de.d_name, glfs_telldir (fd));

                if (xstat)
                        glfs_free(xstat);

                ret = glfs_xreaddirplus_r(fd, rflags, &xstat, &de, &pde);

                /* XXX: Use other APIs to fetch stat and handles */
        }

        if (xstat)
                glfs_free(xstat);

        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_xreaddirp_r", ret, out);

out:
        if (fd != NULL)
                glfs_close(fd);

        if (fs) {
                ret = glfs_fini(fs);
                if (ret)
                        fprintf (stderr, "glfs_fini(fs) returned %d\n", ret);
        }

        return ret;
}


