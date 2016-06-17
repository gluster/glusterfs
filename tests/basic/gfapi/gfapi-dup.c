#include <stdio.h>
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

int
main (int argc, char *argv[])
{
        int             ret = -1;
        int             flags = O_RDWR|O_SYNC;
        glfs_t         *fs = NULL;
        glfs_fd_t      *fd1 = NULL;
        glfs_fd_t      *fd2 = NULL;
        char           *volname = NULL;
        char           *logfile = NULL;
        char           *hostname = NULL;
        const char     *filename = "file_tmp";
        const char     *buff = "An opinion should be the result of thought, "
                                "not a substitute for it.";

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

        fd1 = glfs_creat(fs, filename, flags, 0644);
        if (fd1 == NULL) {
                ret = -1;
                VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_creat", ret, out);
        }

        ret = glfs_write (fd1, buff, strlen (buff), flags);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_write", ret, out);

        fd2 = glfs_dup(fd1);
        if (fd2 == NULL) {
                ret = -1;
                VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_dup", ret, out);
        }

        ret = glfs_lseek (fd2, 0, SEEK_SET);
        VALIDATE_AND_GOTO_LABEL_ON_ERROR ("glfs_lseek", ret, out);

out:
        if (fd1 != NULL)
                glfs_close(fd1);
        if (fd2 != NULL)
                glfs_close(fd2);
        if (fs) {
                ret = glfs_fini(fs);
                if (ret)
                        fprintf (stderr, "glfs_fini(fs) returned %d\n", ret);
        }

        return ret;
}


